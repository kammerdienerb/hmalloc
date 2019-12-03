#include "internal.h"
#include "internal_malloc.h"
#include "profile.h"
#include "heap.h"

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

internal const char* accesses_event_strs[] = {
    "MEM_INST_RETIRED:ALL_STORES",
    NULL
};

internal i32          sample_period    = 2048;
internal i32          max_sample_pages = 128;
internal float        profile_rate     = 0.5;
internal profile_info profs[2048];
internal int          n_profs;

internal u32 get_cpu_freq(void) {
    u32 eax, ebx, ecx, edx;

    __get_cpuid(0, &eax, &ebx, &ecx, &edx);
    ASSERT(eax >= 0x16, "CPUID level 16h not supported");

    __get_cpuid(0x16, &eax, &ebx, &ecx, &edx);

    return eax * 1000000; /* MHz to Hz */
}

internal u64 read_tsc(void) {
    u32 lo, hi;

    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (u64)lo)|( ((u64)hi)<<32 );
}

internal u64 gettime_ns(void) {
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);

    return 1000000000ULL * (u64)t.tv_sec + (u64)t.tv_nsec;
}

internal void profile_printf(const char *fmt, ...) {
    va_list va;

    va_start(va, fmt);

    FormatString(hmalloc_putc, (void*)(i64)prof_data.fd, fmt, va);

    va_end(va);
}

internal void write_header(void) {
    int i;
    u64 lo, hi;

    profile_printf("addr, size, user heap, allocating thread, shared, alloc timestamp, free timestamp");

    for (i = 0; i < N_BUCKETS; i += 1) {
        if (i == 0) { lo = 0;                        }
        else        { lo = bucket_max_values[i - 1]; }
        hi = bucket_max_values[i];

        if (i == N_BUCKETS - 1) {
            profile_printf(", %llu - inifinity", lo);
        } else {
            profile_printf(", %llu - %llu", lo, hi);
        }
    }
    profile_printf("\n");
}

internal void write_obj(profile_obj_entry *obj) {
    int i;

    profile_printf("0x%llx, %llu, '%s', %d, %d, %llu, %llu",
                   obj->addr,
                   obj->size,
                   obj->heap_handle ? obj->heap_handle : "<hmalloc thread heap>",
                   obj->tid,
                   obj->shared,
                   obj->m_ns,
                   obj->f_ns);

    for (i = 0; i < N_BUCKETS; i += 1) {
        profile_printf(", %llu", obj->write_buckets[i]);
    }
    profile_printf("\n");
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

internal void profile_get_accesses_for_cpu(int cpu) {
    uint64_t                  head, tail, buf_size;
    void                     *addr;
    char                     *base, *begin, *end;
    struct sample            *sample;
    struct perf_event_header *header;
    block_header_t           *block;
    u32                       pid;
    u32                       tid;
    u64                       tsc, tsc_diff;
    profile_obj_entry       **m_obj, *obj;
    int                       i;

    PROF_BLOCKS_LOCK(); {
        /* Get ready to read */
        head     = profs[cpu].metadata->data_head;
        tail     = profs[cpu].metadata->data_tail;
        buf_size = prof_data.pagesize * max_sample_pages;
        asm volatile("" ::: "memory"); /* Block after reading data_head, per perf docs */

        base  = (char *)profs[cpu].metadata + prof_data.pagesize;
        begin = base + tail % buf_size;
        end   = base + head % buf_size;

        /* Read all of the samples */
        while (begin != end) {
            header = (struct perf_event_header *)begin;
            if (header->size == 0) {
                break;
            } else if (header->type != PERF_RECORD_SAMPLE) {
                goto inc;
            }

            sample = (struct sample *) (begin + sizeof(struct perf_event_header));
            addr   = (void *) (sample->addr);
            tsc    = sample->time;
            pid    = sample->pid;
            tid    = OS_TID_TO_HM_TID(sample->tid);
            block  = ADDR_PARENT_BLOCK(addr);

            if (pid != prof_data.pid)    { goto inc; }

            if (!(m_obj = hash_table_get_val(prof_data.blocks, block))) {
                goto inc;
            }

            obj = *m_obj;

            tsc_diff     = tsc - obj->m_ns;
            obj->shared |= (tid != obj->tid);

            if (tsc < obj->m_ns) {
                /*
                 * This most likely indicates that we've found a sample for
                 * an object that has been freed in the time between the actual
                 * event and now. Ignore it.
                 */
                if (!prof_data.should_stop) {
                    goto inc;
                }
            }


            for (i = 0; i < N_BUCKETS; i += 1) {
                if (tsc_diff <= bucket_max_values[i]) {
                    obj->write_buckets[i] += 1;
                    break;
                }
            }

            prof_data.total++;

inc:
            /* Increment begin by the size of the sample */
            if (((char *)header + header->size) == base + buf_size) {
                begin = base;
            } else {
                begin = begin + header->size;
            }
        }

        /* Write out all of the objects in the buffer. */
        array_traverse(prof_data.obj_buff, m_obj) {
            write_obj(*m_obj);
            ifree(*m_obj);
        }

        array_clear(prof_data.obj_buff);
    } PROF_BLOCKS_UNLOCK();

    /* Let perf know that we've read this far */
    profs[cpu].metadata->data_tail = head;
    __sync_synchronize();
}

internal void profile_get_accesses(void) {
    for (int i = 0; i < n_profs; i += 1) {
        profile_get_accesses_for_cpu(i);
    }
}

void* profile_fn(void *arg) {
    struct timespec timer;
    thread_data_t *thr;


    LOG("(profile) profile_fn started\n");
    thr = acquire_this_thread();
    LOG("(profile) profiling thread has tid %d\n", thr->tid);
    release_thread(thr);

    const uint64_t one_sec_in_ns = 1000000000;
    timer.tv_sec  = (uint64_t)profile_rate;
    timer.tv_nsec = (uint64_t)((profile_rate  - (float)timer.tv_sec) * one_sec_in_ns);

    while(!profile_should_stop()) {
        profile_get_accesses();
        nanosleep(&timer, NULL);
    }
    LOG("(profile) signaled to stop\n");

    profile_get_accesses();
    /* dump */

    return NULL;
}

void profile_get_event(int cpu) {
    const char **event;
    int          err, found;

    pfm_initialize();

    /* Iterate through the array of event strs and see which one works. */
    event = accesses_event_strs;
    found = 0;
    err   = 0;
    while (*event != NULL) {
        profs[cpu].pe.size  = sizeof(struct perf_event_attr);
        profs[cpu].pfm.size = sizeof(pfm_perf_encode_arg_t);
        profs[cpu].pfm.attr = &profs[cpu].pe;
        err = pfm_get_os_event_encoding(*event, PFM_PLM2 | PFM_PLM3, PFM_OS_PERF_EVENT, &profs[cpu].pfm);

        if (err == PFM_SUCCESS) {
            found = 1;
            break;
        }

        event++;
    }

    if (!found) {
        LOG("Couldn't find an appropriate event to use. (error %d) Aborting.\n", err);
        ASSERT(0, "couldn't find perf event");
    }

    /* perf_event_open */
    profs[cpu].pe.sample_type    =   PERF_SAMPLE_TID
                                   | PERF_SAMPLE_TIME
                                   | PERF_SAMPLE_ADDR;
    profs[cpu].pe.mmap           = 1;
    profs[cpu].pe.exclude_kernel = 1;
    profs[cpu].pe.exclude_hv     = 1;
    profs[cpu].pe.precise_ip     = 2;
    profs[cpu].pe.task           = 1;
    profs[cpu].pe.use_clockid    = 1;
    profs[cpu].pe.clockid        = CLOCK_MONOTONIC;
    profs[cpu].pe.sample_period  = sample_period;
    profs[cpu].pe.disabled       = 1;
}

internal void start_profile_thread() {
    /* Start the profiling thread */
    pthread_mutex_init(&prof_data.mtx, NULL);
    pthread_mutex_lock(&prof_data.mtx);
    pthread_create(&prof_data.profile_id, NULL, &profile_fn, NULL);

    LOG("initialized profiling thread\n");
}

internal void stop_profile_thread() {
    int i;

    for (i = 0; i < n_profs; i += 1) {
        /* Stop the actual sampling */
        ioctl(profs[i].fd, PERF_EVENT_IOC_DISABLE, 0);
        close(profs[i].fd);
    }

    /* Stop the timers and join the threads */
    pthread_mutex_unlock(&prof_data.mtx);
    pthread_join(prof_data.profile_id, NULL);
}

internal void trigger_access_info_flush() {
    pthread_mutex_lock(&access_profile_flush_mutex);
        /* signal */
        pthread_mutex_unlock(&access_profile_flush_signal_mutex);
    pthread_mutex_unlock(&access_profile_flush_mutex);
}

internal void profile_init(void) {
    int fd;

    doing_profiling = !!getenv("HMALLOC_PROFILE");

    if (!doing_profiling)    { return; }

    prof_data.blocks = hash_table_make(block_addr_t, profile_obj_entry_ptr, block_addr_hash);
    LOG("(profile) created blocks hash table\n");
    prof_data.obj_buff = array_make(profile_obj_entry_ptr);
    LOG("(profile) created object buffer\n");
    prof_data.fd = open("hmalloc.profile", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT(prof_data.fd >= 0, "could not open profile output file");
    LOG("(profile) opened profile output file\n");
    write_header();

    pthread_mutex_lock(&access_profile_flush_signal_mutex);

    prof_data.tid = get_this_tid();
    prof_data.pid = getpid();

    prof_data.pagesize = (size_t) sysconf(_SC_PAGESIZE);

    /* Use libpfm to fill the pe structs */
    fd = 0;
    while (fd != -1) {
        profile_get_event(n_profs);

        /* Open the perf file descriptor */
        fd = syscall(__NR_perf_event_open, &profs[n_profs].pe, -1, n_profs, -1, 0);

        if (fd == -1) {
            if (errno != EINVAL) {
                LOG("Error opening perf event 0x%llx (%d -- %s).\n", profs[n_profs].pe.config, errno, strerror(errno));
                LOG("attr = 0x%lx\n", (u64)(profs[n_profs].pe.config));
                ASSERT(0, "perf event error");
            }
        } else {
            profs[n_profs].fd = fd;

            LOG("(profile) perf event opened from tid %d (os %d) for cpu %d\n", get_this_tid(), os_get_tid(), n_profs);
            LOG("(profile) fd for cpu %d is %d\n", n_profs, profs[n_profs].fd);

            /* mmap the file */
            profs[n_profs].metadata = mmap(NULL, prof_data.pagesize + (prof_data.pagesize * max_sample_pages),
                                PROT_READ | PROT_WRITE, MAP_SHARED, profs[n_profs].fd, 0);
            if (profs[n_profs].metadata == MAP_FAILED) {
                fprintf(stderr, "Failed to mmap room (%zu bytes) for perf samples (fd = %d). Aborting with:\n(%d) %s\n",
                        prof_data.pagesize + (prof_data.pagesize * max_sample_pages), profs[n_profs].fd, errno, strerror(errno));
                exit(1);
            }

            /* Initialize */
            ioctl(profs[n_profs].fd, PERF_EVENT_IOC_RESET, 0);
            ioctl(profs[n_profs].fd, PERF_EVENT_IOC_ENABLE, 0);

            n_profs += 1;
        }
    }

    start_profile_thread();

    prof_data.thread_started = 1;

    hmalloc_use_imalloc = 0;
}

internal void profile_add_block(void *block, u64 size) {
    heap__meta_t      *__meta;
    block_header_t    *b;
    void              *block_aligned_addr;
    profile_obj_entry *obj;

    ASSERT(doing_profiling, "can't add block when not profiling");

    if (!prof_data.thread_started) {
        return;
    }

    __meta = &((block_header_t*)block)->heap__meta;
    b      = block;

PROF_BLOCKS_LOCK(); {

    prof_data.total_allocated += 1;

    obj = imalloc(sizeof(*obj));

    obj->addr        = block;
    obj->size        = size;
    obj->heap_handle = __meta->flags & HEAP_USER ? __meta->handle : NULL;
    obj->tid         = b->tid;
    obj->shared      = 0;
    obj->m_ns        = gettime_ns();
    obj->f_ns        = 0;
    memset(obj->write_buckets, 0, sizeof(u64) * N_BUCKETS);

    block_aligned_addr = block;
    while (block_aligned_addr < (&(b->c))->end) {
        hash_table_insert(prof_data.blocks, block_aligned_addr, obj);
        block_aligned_addr += DEFAULT_BLOCK_SIZE;
    }

} PROF_BLOCKS_UNLOCK();
}

internal void profile_delete_block(void *block) {
    block_header_t    *b;
    void              *block_aligned_addr;
    profile_obj_entry **m_obj, *obj;

    ASSERT(doing_profiling, "can't add block when not profiling");

    if (!prof_data.thread_started) {
        return;
    }

PROF_BLOCKS_LOCK(); {

    b     = block;
    m_obj = hash_table_get_val(prof_data.blocks, block);
    obj   = *m_obj;

    block_aligned_addr = block;
    while (block_aligned_addr < (&(b->c))->end) {
        hash_table_delete(prof_data.blocks, block_aligned_addr);
        block_aligned_addr += DEFAULT_BLOCK_SIZE;
    }

    if (obj) {
        obj->f_ns = gettime_ns();
        array_push(prof_data.obj_buff, obj);
    }
} PROF_BLOCKS_UNLOCK();
}

internal void profile_fini(void) {
    void               *block;
    profile_obj_entry **obj;

    if (!prof_data.thread_started) {
        return;
    }

    prof_data.should_stop = 1;
    stop_profile_thread();

PROF_BLOCKS_LOCK(); {

    hash_table_traverse(prof_data.blocks, block, obj) {
        (void)block;
        (*obj)->f_ns = gettime_ns();
        array_push(prof_data.obj_buff, *obj);
    }

    /* Write out all of the objects in the buffer. */
    array_traverse(prof_data.obj_buff, obj) {
        write_obj(*obj);
        ifree(*obj);
    }
} PROF_BLOCKS_UNLOCK();

    LOG("(profile) %llu total samples\n", prof_data.total);
}
