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


/* #define TEST_PHASE (0) */
/* #define TEST_PHASE (1) */
#define TEST_PHASE (2)



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

/* @bad
 * To avoid heap allocations, we're going to assume that the page size
 * is 4096 bytes even though we have the correct size in system_info.page_size.
 * This should always be correct, but it is an assumption, so be careful.
 */
#define ASSUMED_PAGE_SIZE (4096)

static u32 count_written_pages(int pagemap_fd, profile_obj_entry *obj) {
    void               *start;
    void               *end;
    unsigned long long  page_nr;
    unsigned long long  total_pages;
    u32                 pte_bytes;
    int                 n_read;
    u64                 pte_buff[DEFAULT_BLOCK_SIZE / ASSUMED_PAGE_SIZE];
    u32                 n_written;
    int                 i;

    start       = obj->addr;
    end         = obj->end_page;
    page_nr     = (((unsigned long long)start) / ASSUMED_PAGE_SIZE);
    total_pages = (((unsigned long long)end) - ((unsigned long long)start) / ASSUMED_PAGE_SIZE);
    pte_bytes   = sizeof(u64) * total_pages;

    while (n_read < pte_bytes) {
        n_read += pread(pagemap_fd,
                        ((void*)pte_buff) + n_read,
                        pte_bytes - n_read,
                        (sizeof(u64) * page_nr) + n_read);
    }

    n_written = 0;
    for (i = 0; i < total_pages; i += 1) {
        n_written += ((pte_buff[i] >> 55) & 1);
    }

    return n_written;
}

static u64 interval;

static void process_objects(void) {
    int                   pagemap_fd;
    prof_thread_objects  *thr_obj;
    void                 *block;
    profile_obj_entry   **obj_it;
    profile_obj_entry    *obj;
    u32                   n_written_pages;
    int                   status;

    interval += 1;

    pagemap_fd = open(prof_data.pagemap_path, O_RDONLY);
    ASSERT(pagemap_fd >= 0, "Could not open pagemap file!\n");

    LOCKING_THREAD_TRAVERSE(thr_obj) {
        if (!thr_obj->is_initialized) { continue; }

        hash_table_traverse(thr_obj->blocks, block, obj_it) {
            obj = *obj_it;

            ASSERT(block == obj->addr, "profile object key/address mismatch");

            n_written_pages = count_written_pages(pagemap_fd, obj);

            obj->running_num_written_pages += n_written_pages;
            obj->num_blocks_processed      += 1;

            /*
             * If we've seen every block that belongs to this object,
             * then we'll go ahead and do the profile computation.
             */
            if (obj->num_blocks_processed == obj->num_blocks) {
                obj->live_intervals += 1;

                if (obj->running_num_written_pages == 0) {
                    obj->cold_intervals += 1;
                } else {
                    if (obj->cold_intervals > obj->max_cold_intervals) {
                        obj->max_cold_intervals = obj->cold_intervals;
                    }
                    obj->cold_intervals = 0;
                }

                obj->running_num_written_pages = 0;
                obj->num_blocks_processed      = 0;

                /*
                 * Done processing the object for this interval.
                 * Clear all the refs now.
                 *
                 * @cleanup
                 * I think it's okay that we're using prof_data.crr
                 * here even though it could possibly be accessed by
                 * another thread in profile_add_block().
                 * For now, the data in that struct is read only
                 * (it's just a pid and fd), but just be careful that
                 * that fact doesn't change.
                 * If it does, you must either protect the access, or
                 * create per-thread versions.
                 *
                 * @performance
                 * Do keep in mind, however, that this may be a performance
                 * bottleneck depending on how the kernel does with
                 * multithreaded writes to the same proc/fs file descriptor.
                 * So, it may be a good idea to create per-thread crr's
                 * anyways.
                 */
                status = crr_range(&prof_data.crr, block, obj->end_page);
                if (status < 0) {
                    LOG("(profile) crr_range() failed with error %d\n", status);
                    ASSERT(0, "crr_range() failed.. check log for more info");
                }
            }
        }
    }

    close(pagemap_fd);
}

void* profile_fn(void *arg) {
    thread_data_t       *thr;
    struct timespec      timer;

    LOG("(profile) profile_fn started\n");
    thr = acquire_this_thread();
    LOG("(profile) profiling thread has tid %d\n", thr->tid);
    release_thread(thr);

    const uint64_t one_sec_in_ns = 1000000000;
    timer.tv_sec  = (uint64_t)profile_rate;
    timer.tv_nsec = (uint64_t)((profile_rate  - (float)timer.tv_sec) * one_sec_in_ns);

    while(!profile_should_stop()) {
        process_objects();
        nanosleep(&timer, NULL);
    }
    LOG("(profile) signaled to stop\n");

    process_objects();

    /* @TODO dump */

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

    sprintf(prof_data.pagemap_path, "/proc/%d/pagemap", prof_data.pid);

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
    void                *end_page;
    int                  status;

    ASSERT(doing_profiling, "can't add block when not profiling");

    if (!prof_data.thread_started) {
        return;
    }

    __meta = &((block_header_t*)block)->heap__meta;
    b      = block;
    tid    = b->tid;

    obj = imalloc(sizeof(*obj));

    memset(obj, 0, sizeof(*obj));

    obj->addr                      = block;
    obj->size                      = size;
    obj->heap_handle               = __meta->flags & HEAP_USER ? __meta->handle : NULL;
    obj->tid                       = b->tid;
    obj->num_blocks                = 0;
    obj->num_blocks_processed      = 0;
    obj->cold_intervals            = 0;
    obj->max_cold_intervals        = 0;
    obj->live_intervals            = 0;
    obj->running_num_written_pages = 0;

PROF_THREAD_LOCK(tid); {
    thr = &prof_data.thread_objects[tid];

    if (!thr->is_initialized) {
        profile_thr_init(thr, tid);
    }

    block_aligned_addr = block;
    while (block_aligned_addr < (&(b->c))->end) {
        hash_table_insert(thr->blocks, block_aligned_addr, obj);
        obj->num_blocks    += 1;
        block_aligned_addr += DEFAULT_BLOCK_SIZE;
    }

#if TEST_PHASE >= 1
    end_page = block + size;
    if (!IS_ALIGNED(end_page, system_info.page_size)) {
        end_page = ALIGN(end_page, system_info.page_size);
    }
    obj->end_page = end_page;
    status = crr_range(&prof_data.crr, block, end_page);
    if (status < 0) {
        LOG("(profile) crr_range() failed with error %d\n", status);
        ASSERT(0, "crr_range() failed.. check log for more info");
    }
#else
    (void)end_page;
    (void)status;
#endif

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
         * poop out object profile info
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
