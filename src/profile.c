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

internal i32          sample_period    = 1024;
internal i32          max_sample_pages = 128;
internal float        profile_rate     = 0.5;
internal profile_info prof;

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
    switch(pthread_mutex_trylock(&prof.mtx)) {
        case 0:
            pthread_mutex_unlock(&prof.mtx);
            return 1;
        case EBUSY:
            return 0;
    }
    return 1;
}

internal void profile_get_accesses(void) {
    uint64_t                  head, tail, buf_size, id, *n_reads, tmp_id, *tmp_n_reads;
    void                     *addr, *alloc_addr;
    char                     *base, *begin, *end;
    struct sample            *sample;
    struct perf_event_header *header;
    block_header_t           *block;
    u32                       tid;
    u64                       tsc, tsc_diff;
    profile_obj_entry        *obj;
    int                       i;

    (void)alloc_addr;
    (void)tmp_n_reads;
    (void)tmp_id;
    (void)n_reads;
    (void)id;

    PROF_BLOCKS_LOCK(); {
        /* Get ready to read */
        head     = prof.metadata->data_head;
        tail     = prof.metadata->data_tail;
        buf_size = prof.pagesize * max_sample_pages;
        asm volatile("" ::: "memory"); /* Block after reading data_head, per perf docs */

        base  = (char *)prof.metadata + prof.pagesize;
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

            if (addr) {
                tsc    = sample->time;
                tid    = sample->tid & (HMALLOC_MAX_THREADS - 1);
                block  = ADDR_PARENT_BLOCK(addr);

                if (!(obj = hash_table_get_val(prof_data.blocks, block))) {
                    goto inc;
                }

                obj->shared |= (tid != obj->tid);

                if (tsc < obj->m_ns) {
                    /*
                     * This most likely indicates that we've found a sample for
                     * an object taht has been freed in the time between the actual
                     * event and now. Ignore it.
                     */
                    goto inc;
                }

                tsc_diff = tsc - obj->m_ns;
                for (i = 0; i < N_BUCKETS; i += 1) {
                    if (tsc_diff <= bucket_max_values[i]) {
                        obj->write_buckets[i] += 1;
                        break;
                    }
                }

                prof.total++;
            }
inc:
            /* Increment begin by the size of the sample */
            if (((char *)header + header->size) == base + buf_size) {
                begin = base;
            } else {
                begin = begin + header->size;
            }
        }

        /* Write out all of the objects in the buffer. */
        array_traverse(prof_data.obj_buff, obj) {
            write_obj(obj);
        }

        array_clear(prof_data.obj_buff);

    } PROF_BLOCKS_UNLOCK();

    /* Let perf know that we've read this far */
    prof.metadata->data_tail = head;
    __sync_synchronize();
}

void* profile_fn(void *arg) {
    struct timespec timer;
    uint64_t site, *n_reads;
    thread_data_t *thr;


    (void)n_reads;
    (void)site;

    LOG("(profile) profile_fn started\n");
    thr = acquire_this_thread();
    LOG("(profile) profiling thread has tid %d\n", thr->tid);
    release_thread(thr);

    /* mmap the file */
    prof.metadata = mmap(NULL, prof.pagesize + (prof.pagesize * max_sample_pages),
                         PROT_READ | PROT_WRITE, MAP_SHARED, prof.fd, 0);
    if (prof.metadata == MAP_FAILED) {
        fprintf(stderr, "Failed to mmap room (%zu bytes) for perf samples. Aborting with:\n%s\n",
                prof.pagesize + (prof.pagesize * max_sample_pages), strerror(errno));
        exit(1);
    }

    /* Initialize */
    ioctl(prof.fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(prof.fd, PERF_EVENT_IOC_ENABLE, 0);
    prof.consumed = 0;
    prof.total    = 0;
    prof.oops     = 0;

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

void profile_get_event() {
    const char **event, *buf;
    int          err, found;

    (void)buf;

    pfm_initialize();

    /* Iterate through the array of event strs and see which one works.
     * For should_profile_one, just use the first given IMC. */
    event = accesses_event_strs;
    found = 0;
    while(*event != NULL) {
        prof.pe.size  = sizeof(struct perf_event_attr);
        prof.pfm.size = sizeof(pfm_perf_encode_arg_t);
        prof.pfm.attr = &prof.pe;
        err = pfm_get_os_event_encoding(*event, PFM_PLM2 | PFM_PLM3, PFM_OS_PERF_EVENT, &prof.pfm);

        if(err == PFM_SUCCESS) {
            found = 1;
            break;
        }

        event++;
    }

    if(!found) {
        LOG("Couldn't find an appropriate event to use. (error %d) Aborting.\n", err);
        ASSERT(0, "couldn't find perf event");
    }

    /* perf_event_open */
    prof.pe.sample_type    =   PERF_SAMPLE_TID
                             | PERF_SAMPLE_TIME
                             | PERF_SAMPLE_ADDR;
    prof.pe.sample_period  = sample_period;
    prof.pe.mmap           = 1;
    prof.pe.disabled       = 1;
    prof.pe.exclude_kernel = 1;
    prof.pe.exclude_hv     = 1;
    prof.pe.precise_ip     = 2;
    prof.pe.task           = 1;
    prof.pe.sample_period  = sample_period;
}

internal void start_profile_thread() {
    prof.pagesize = (size_t) sysconf(_SC_PAGESIZE);

    prof.fd  = 0;

    /* Use libpfm to fill the pe struct */
    profile_get_event();

    /* Open the perf file descriptor */
    prof.fd = syscall(__NR_perf_event_open, &prof.pe, 0, -1, -1, 0);
    if (prof.fd == -1) {
        LOG("Error opening perf event 0x%llx (%d -- %s).\n", prof.pe.config, errno, strerror(errno));
        LOG("attr = 0x%lx\n", (u64)(prof.pe.config));
        ASSERT(0, "perf event error");
    }
    LOG("(profile) fd is %d\n", prof.fd);

    /* Start the profiling threads */
    pthread_mutex_init(&prof.mtx, NULL);
    pthread_mutex_lock(&prof.mtx);
    pthread_create(&prof.profile_id, NULL, &profile_fn, NULL);

    LOG("initialized profiling thread\n");
}

internal void stop_profile_thread() {
    size_t i, associated;

    (void)i;
    (void)associated;

    /* Stop the actual sampling */
    ioctl(prof.fd, PERF_EVENT_IOC_DISABLE, 0);

    /* Stop the timers and join the threads */
    pthread_mutex_unlock(&prof.mtx);
    pthread_join(prof.profile_id, NULL);

    close(prof.fd);

    printf("Done profiling.\n");
}

internal void trigger_access_info_flush() {
    pthread_mutex_lock(&access_profile_flush_mutex);
        /* signal */
        pthread_mutex_unlock(&access_profile_flush_signal_mutex);
    pthread_mutex_unlock(&access_profile_flush_mutex);
}

internal void profile_init(void) {
    doing_profiling = 1;

    prof_data.blocks = hash_table_make(block_addr_t, profile_obj_entry, block_addr_hash);
    LOG("(profile) created blocks hash table\n");
    prof_data.obj_buff = array_make(profile_obj_entry);
    LOG("(profile) created object buffer\n");
    prof_data.fd = open("hmalloc.profile", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT(prof_data.fd >= 0, "could not open profile output file");
    LOG("(profile) opened profile output file\n");
    write_header();

    pthread_mutex_lock(&access_profile_flush_signal_mutex);

    start_profile_thread();

    prof_data.thread_started = 1;
}

internal void profile_add_block(void *block, u64 size) {
    heap__meta_t      *__meta;
    profile_obj_entry  obj;

    ASSERT(doing_profiling, "can't add block when not profiling");

    if (!prof_data.thread_started) {
        return;
    }

    __meta = &((block_header_t*)block)->heap__meta;

PROF_BLOCKS_LOCK(); {

    prof_data.total_allocated += 1;

    obj.addr        = block;
    obj.size        = size;
    obj.heap_handle = __meta->flags & HEAP_USER ? __meta->handle : NULL;
    obj.tid         = ((block_header_t*)block)->tid;
    obj.shared      = 0;
    obj.m_ns        = gettime_ns();
    obj.f_ns        = 0;
    memset(obj.write_buckets, 0, sizeof(u64) * N_BUCKETS);
    hash_table_insert(prof_data.blocks, block, obj);

} PROF_BLOCKS_UNLOCK();
}

internal void profile_delete_block(void *block) {
    profile_obj_entry *obj;

    ASSERT(doing_profiling, "can't add block when not profiling");

    if (!prof_data.thread_started) {
        return;
    }

PROF_BLOCKS_LOCK(); {

    obj = hash_table_get_val(prof_data.blocks, block);
    if (obj) {
        obj->f_ns = gettime_ns();
        array_push(prof_data.obj_buff, *obj);
        hash_table_delete(prof_data.blocks, block);
    }

} PROF_BLOCKS_UNLOCK();
}

__attribute__((destructor))
internal void profile_dump_remaining(void) {
    profile_obj_entry *obj;

    if (!prof_data.thread_started) {
        return;
    }

    /* Write out all of the objects in the buffer. */
    array_traverse(prof_data.obj_buff, obj) {
        write_obj(obj);
    }
}
