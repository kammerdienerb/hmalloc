#include "internal.h"
#include "internal_malloc.h"
#include "profile.h"
#include "heap.h"

#define CLEAR_REFS_RANGES_IMPL
#include "clear_refs_ranges.h"

#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <errno.h>
#include <asm/perf_regs.h>
#include <asm/unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <cpuid.h>

internal float profile_rate = 0.5;

internal u64 gettime_ns(void) {
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);

    return 1000000000ULL * (u64)t.tv_sec + (u64)t.tv_nsec;
}

#define PUTC_C_N (KiB(16))
static char putc_c_buff[PUTC_C_N];
static int  putc_c_size = 0;

internal void profile_putc_flush(void) {
    write(prof_data.fd, putc_c_buff, putc_c_size);
    putc_c_size = 0;
}

internal void profile_putc(char c, void *context) {
    (void)context;

    putc_c_buff[putc_c_size++] = c;

    if (putc_c_size == PUTC_C_N) {
        profile_putc_flush();
    }
}

internal void profile_printf(const char *fmt, ...) {
    va_list va;

    va_start(va, fmt);

    FormatString(profile_putc, NULL, fmt, va);

    va_end(va);
}

internal i32 profile_should_stop(void) {
    switch(pthread_mutex_trylock(&prof_data.mtx)) {
        case 0:
            pthread_mutex_unlock(&prof_data.mtx);
            return 1;
        case EBUSY:
            return 0;
    }
    return 1;
}

void* profile_fn(void *arg) {
    struct timespec      timer;
    thread_data_t       *thr;
    prof_thread_objects *thr_obj;
    int                  n_objects;


    LOG("(profile) profile_fn started\n");
    thr = acquire_this_thread();
    LOG("(profile) profiling thread has tid %d\n", thr->tid);
    release_thread(thr);

    const uint64_t one_sec_in_ns = 1000000000;
    timer.tv_sec  = (uint64_t)profile_rate;
    timer.tv_nsec = (uint64_t)((profile_rate  - (float)timer.tv_sec) * one_sec_in_ns);

    while(!profile_should_stop()) {
        n_objects = 0;
        LOCKING_THREAD_TRAVERSE(thr_obj) {
            if (thr_obj->is_initialized) {
                n_objects += hash_table_len(thr_obj->blocks);
            }
        }
        LOG("(profile) this is an interval -- %d objects\n", n_objects);
        /* @TODO
         * do work
         */
        nanosleep(&timer, NULL);
    }
    LOG("(profile) signaled to stop\n");

    /* @TODO
     * do last work
     */
    /* dump */

    return NULL;
}

internal void start_profile_thread() {
    /* Start the profiling thread */
    pthread_mutex_init(&prof_data.mtx, NULL);
    pthread_mutex_lock(&prof_data.mtx);
    pthread_create(&prof_data.profile_id, NULL, &profile_fn, NULL);

    LOG("initialized profiling thread\n");
}

internal void stop_profile_thread() {
    /* Stop the timers and join the threads */
    pthread_mutex_unlock(&prof_data.mtx);
    pthread_join(prof_data.profile_id, NULL);
}

internal void profile_init(void) {
    int status;

    doing_profiling = !!getenv("HMALLOC_PROFILE");

    if (!doing_profiling)    { return; }

    prof_data.fd = open("hmalloc.profile", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT(prof_data.fd >= 0, "could not open profile output file");
    LOG("(profile) opened profile output file\n");

    prof_data.tid = get_this_tid();
    prof_data.pid = getpid();

    prof_data.pagesize = (size_t) sysconf(_SC_PAGESIZE);


    status = crr_open(&prof_data.crr);
    if (status < 0) {
        LOG("(profile) crr_open() failed with error %d\n", status);
        ASSERT(0, "crr_open() failed");
    } else {
        LOG("(profile) initialized clear_refs_ranges\n");
    }

    start_profile_thread();

    prof_data.thread_started = 1;

    hmalloc_use_imalloc = 0;
}

internal void profile_thr_init(prof_thread_objects *thr, u16 tid) {
    ASSERT(!thr->is_initialized, "profile thread data is already initialized!");

    thr->blocks = hash_table_make(block_addr_t, profile_obj_entry_ptr, block_addr_hash);
    LOG("(profile) created blocks hash table for thread %u\n", tid);

    thr->tid            = tid;
    thr->is_initialized = 1;
}

internal void profile_add_block(void *block, u64 size) {
    heap__meta_t        *__meta;
    block_header_t      *b;
    void                *block_aligned_addr;
    u16                  tid;
    profile_obj_entry   *obj;
    prof_thread_objects *thr;

    ASSERT(doing_profiling, "can't add block when not profiling");

    if (!prof_data.thread_started) {
        return;
    }

    __meta = &((block_header_t*)block)->heap__meta;
    b      = block;
    tid    = b->tid;

    obj = imalloc(sizeof(*obj));

    memset(obj, 0, sizeof(*obj));

    obj->addr        = block;
    obj->size        = size;
    obj->heap_handle = __meta->flags & HEAP_USER ? __meta->handle : NULL;
    obj->tid         = b->tid;

PROF_THREAD_LOCK(tid); {
    thr = &prof_data.thread_objects[tid];

    if (!thr->is_initialized) {
        profile_thr_init(thr, tid);
    }

    block_aligned_addr = block;
    while (block_aligned_addr < (&(b->c))->end) {
        hash_table_insert(thr->blocks, block_aligned_addr, obj);
        block_aligned_addr += DEFAULT_BLOCK_SIZE;
    }

} PROF_THREAD_UNLOCK(tid);
}

internal void profile_delete_block(void *block) {
    block_header_t      *b;
    void                *block_aligned_addr;
    profile_obj_entry  **m_obj, *obj;
    u16                  tid;
    prof_thread_objects *thr;

    ASSERT(doing_profiling, "can't delete block when not profiling");

    if (!prof_data.thread_started) {
        return;
    }

    b   = block;
    tid = b->tid;

PROF_THREAD_LOCK(tid); {
    thr = &prof_data.thread_objects[tid];

    ASSERT(thr->is_initialized,
           "attempting to delete a block from a profile thread data"
           "that hasn't been created yet.");

    m_obj = hash_table_get_val(thr->blocks, block);
    ASSERT(m_obj, "object info not found for block");
    obj   = *m_obj;

    block_aligned_addr = block;
    while (block_aligned_addr < (&(b->c))->end) {
        hash_table_delete(thr->blocks, block_aligned_addr);
        block_aligned_addr += DEFAULT_BLOCK_SIZE;
    }

    if (obj) {
        /* @TODO
         * process object
         */
    }
} PROF_THREAD_UNLOCK(tid);
}

internal void profile_set_site(void *addr, char *site) {
    void                *block_addr;
    block_header_t      *block;
    profile_obj_entry  **m_obj, *obj;
    u16                  tid;
    prof_thread_objects *thr;

    block_addr = ADDR_PARENT_BLOCK(addr);
    block      = block_addr;
    tid        = block->tid;

PROF_THREAD_LOCK(tid); {
    thr = &prof_data.thread_objects[tid];

    ASSERT(thr->is_initialized,
           "attempting to set site for a block from a profile thread data"
           "that hasn't been created yet.");

    m_obj = hash_table_get_val(thr->blocks, block);
} PROF_THREAD_UNLOCK(tid);

    ASSERT(m_obj, "object info not found for block");
    obj = *m_obj;

    obj->heap_handle = istrdup(site);
}

internal void profile_fini(void) {
    void                *block;
    profile_obj_entry  **obj;

    if (!prof_data.thread_started) {
        return;
    }

    (void)block;
    (void)obj;

    prof_data.should_stop = 1;
    stop_profile_thread();

    crr_close(&prof_data.crr);

    profile_putc_flush();
}
