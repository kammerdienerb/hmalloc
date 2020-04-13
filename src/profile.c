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




#define PHASE_DCACHE
/* #define PHASE_DCACHE_RATE */
/* #define PHASE_IPC */







/* internal i32          sample_period    = 2048; */
internal i32          sample_period    = 4;
internal i32          max_sample_pages = 128;
internal float        profile_rate     = 0.25;
internal profile_info w_profs[2048];
internal profile_info r_profs[2048];
internal profile_info p1_profs[2048];
internal profile_info p2_profs[2048];
internal int          n_profs;

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

internal void write_header(void) {
    int i;
    u64 lo, hi;

    profile_printf("size,user heap,allocating thread,shared,alloc timestamp,free timestamp");

    for (i = 0; i < N_BUCKETS; i += 1) {
        if (i == 0) { lo = 0;                        }
        else        { lo = bucket_max_values[i - 1]; }
        hi = bucket_max_values[i];

        if (i == N_BUCKETS - 1) {
            profile_printf(",%llu - inifinity", lo);
        } else {
            profile_printf(",%llu - %llu", lo, hi);
        }
    }
    profile_printf(",last write timestamp");
    profile_printf(",L1 hit write samples");
    for (i = 0; i < N_BUCKETS; i += 1) {
        if (i == 0) { lo = 0;                        }
        else        { lo = bucket_max_values[i - 1]; }
        hi = bucket_max_values[i];

        if (i == N_BUCKETS - 1) {
            profile_printf(",%llu - inifinity", lo);
        } else {
            profile_printf(",%llu - %llu", lo, hi);
        }
    }
    profile_printf(",last read timestamp");
    profile_printf("\n");
}

internal void write_obj(profile_obj_entry *obj) {
    int i;

    profile_printf("%llu,'%s',%d,%d,%llu,%llu",
                   obj->size,
                   obj->heap_handle ? obj->heap_handle : "<hmalloc thread heap>",
                   obj->tid,
                   obj->shared,
                   obj->m_ns,
                   obj->f_ns);

    for (i = 0; i < N_BUCKETS; i += 1) {
        profile_printf(",%llu", obj->write_buckets[i]);
    }
    profile_printf(",%llu", obj->last_write_ns);
    profile_printf(",%llu", obj->l1_hit_w);
    for (i = 0; i < N_BUCKETS; i += 1) {
        profile_printf(",%llu", obj->read_buckets[i]);
    }
    profile_printf(",%llu", obj->last_read_ns);
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

internal void phase_putc(char c, void *context) {
    write(prof_data.phase_fd, &c, 1);
}

internal void phase_printf(const char *fmt, ...) {
    va_list va;

    va_start(va, fmt);

    FormatString(phase_putc, NULL, fmt, va);

    va_end(va);
}

static u64 p1_count,
           p2_count;

internal void profile_get_p1_samples_for_cpu(int cpu, profile_info *profs) {
    uint64_t                  head, tail, buf_size;
    char                     *base, *begin, *end;
    struct sample            *sample;
    struct perf_event_header *header;
    u32                       pid;

    /* Get ready to read */
    head     = profs[cpu].metadata->data_head;
    tail     = profs[cpu].metadata->data_tail;
    buf_size = prof_data.pagesize * max_sample_pages;
    asm volatile("" ::: "memory"); /* Block after reading data_head, per perf docs */

    base  = (char *)profs[cpu].metadata + prof_data.pagesize;
    begin = base + tail % buf_size;
    end   = base + head % buf_size;

    /* Read all of the samples */
    while (begin < end) {
        header = (struct perf_event_header *)begin;
        if (header->size == 0) {
            break;
        } else if (header->type != PERF_RECORD_SAMPLE) {
            goto inc;
        }

        sample = (struct sample *) (begin + sizeof(struct perf_event_header));
        pid    = sample->pid;

        if (pid != prof_data.pid)    { goto inc; }

        p1_count += 1;
inc:
        /* Increment begin by the size of the sample */
        if (((char *)header + header->size) == base + buf_size) {
            begin = base;
        } else {
            begin = begin + header->size;
        }
    }

    /* Let perf know that we've read this far */
    profs[cpu].metadata->data_tail = head;
    __sync_synchronize();
}

internal void profile_get_p2_samples_for_cpu(int cpu, profile_info *profs) {
    uint64_t                  head, tail, buf_size;
    char                     *base, *begin, *end;
    struct sample            *sample;
    struct perf_event_header *header;
    u32                       pid;

    /* Get ready to read */
    head     = profs[cpu].metadata->data_head;
    tail     = profs[cpu].metadata->data_tail;
    buf_size = prof_data.pagesize * max_sample_pages;
    asm volatile("" ::: "memory"); /* Block after reading data_head, per perf docs */

    base  = (char *)profs[cpu].metadata + prof_data.pagesize;
    begin = base + tail % buf_size;
    end   = base + head % buf_size;

    /* Read all of the samples */
    while (begin < end) {
        header = (struct perf_event_header *)begin;
        if (header->size == 0) {
            break;
        } else if (header->type != PERF_RECORD_SAMPLE) {
            goto inc;
        }

        sample = (struct sample *) (begin + sizeof(struct perf_event_header));
        pid    = sample->pid;

        if (pid != prof_data.pid)    { goto inc; }

        p2_count += 1;
inc:
        /* Increment begin by the size of the sample */
        if (((char *)header + header->size) == base + buf_size) {
            begin = base;
        } else {
            begin = begin + header->size;
        }
    }

    /* Let perf know that we've read this far */
    profs[cpu].metadata->data_tail = head;
    __sync_synchronize();
}



internal void profile_get_w_accesses_for_cpu(int cpu, profile_info *profs) {
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
    prof_thread_objects      *thr;

    /* Get ready to read */
    head     = profs[cpu].metadata->data_head;
    tail     = profs[cpu].metadata->data_tail;
    buf_size = prof_data.pagesize * max_sample_pages;
    asm volatile("" ::: "memory"); /* Block after reading data_head, per perf docs */

    base  = (char *)profs[cpu].metadata + prof_data.pagesize;
    begin = base + tail % buf_size;
    end   = base + head % buf_size;

    /* Read all of the samples */
    while (begin < end) {
        header = (struct perf_event_header *)begin;
        if (header->size == 0) {
            break;
        } else if (header->type != PERF_RECORD_SAMPLE) {
            goto inc;
        }

        sample = (struct sample *) (begin + sizeof(struct perf_event_header));
        pid    = sample->pid;

        if (pid != prof_data.pid)    { goto inc; }

        addr  = (void *) (sample->addr);
        block = ADDR_PARENT_BLOCK(addr);
        tid   = OS_TID_TO_HM_TID(sample->tid);

        /*
         * As an optimization, let's do the first lookup in the thread
         * that the sample occurred in.
         * The chances are high that many accesses to data will occur in
         * the thread that allocated it.
         *
         * Of course, this is not always the case, but it doesn't hurt to
         * check the sample thread first.
         */
        PROF_THREAD_LOCK(tid); {
            thr = &prof_data.thread_objects[tid];

            if (thr->is_initialized) {
                if ((m_obj = hash_table_get_val(thr->blocks, block))) {
                    /*
                     * !!! NOTE:
                     * We are intentionally not unlocking here.
                     * We need the thread to be locked and we don't
                     * want the object to be removed and freed in the
                     * time it takes us to unlock here and then lock
                     * again later.
                     */
/*                     PROF_THREAD_UNLOCK(thr->tid); */
                    goto found;
                }
            }
        } PROF_THREAD_UNLOCK(tid);

        /* Look through all the threads to find the object. */
        LOCKING_THREAD_TRAVERSE(thr) {
            if (!thr->is_initialized || thr->tid == tid) {
                /* Invalid or already checked. */
                continue;
            }

            if ((m_obj = hash_table_get_val(thr->blocks, block))) {
                /*
                 * !!! NOTE:
                 * We are intentionally not unlocking here.
                 * We need the thread to be locked and we don't
                 * want the object to be removed and freed in the
                 * time it takes us to unlock here and then lock
                 * again later.
                 */
/*                 PROF_THREAD_UNLOCK(thr->tid); */
                goto found;
            }
        }

        /* If we're here, we haven't found the object. */
        goto inc;

found:
        obj = *m_obj;
        tsc = sample->time;

        if (tsc < obj->m_ns) {
            /*
             * This most likely indicates that we've found a sample for
             * an object that has been freed in the time between the actual
             * event and now. Ignore it.
             */
            if (!prof_data.should_stop) {
                PROF_THREAD_UNLOCK(thr->tid);
                goto inc;
            }
        }

        obj->shared |= (tid != obj->tid);

        tsc_diff = tsc - obj->m_ns;

        for (i = 0; i < N_BUCKETS; i += 1) {
            if (tsc_diff <= bucket_max_values[i]) {
                obj->write_buckets[i] += 1;
                break;
            }
        }

        obj->last_write_ns = tsc;


        /* Finally, now that we've processed the object, unlock. */
        PROF_THREAD_UNLOCK(thr->tid);

        prof_data.total_w++;

inc:
        /* Increment begin by the size of the sample */
        if (((char *)header + header->size) == base + buf_size) {
            begin = base;
        } else {
            begin = begin + header->size;
        }
    }

    /* Let perf know that we've read this far */
    profs[cpu].metadata->data_tail = head;
    __sync_synchronize();
}

internal void profile_get_r_accesses_for_cpu(int cpu, profile_info *profs) {
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
    prof_thread_objects      *thr;

    /* Get ready to read */
    head     = profs[cpu].metadata->data_head;
    tail     = profs[cpu].metadata->data_tail;
    buf_size = prof_data.pagesize * max_sample_pages;
    asm volatile("" ::: "memory"); /* Block after reading data_head, per perf docs */

    base  = (char *)profs[cpu].metadata + prof_data.pagesize;
    begin = base + tail % buf_size;
    end   = base + head % buf_size;

    /* Read all of the samples */
    while (begin < end) {
        header = (struct perf_event_header *)begin;
        if (header->size == 0) {
            break;
        } else if (header->type != PERF_RECORD_SAMPLE) {
            goto inc;
        }

        sample = (struct sample *) (begin + sizeof(struct perf_event_header));
        pid    = sample->pid;

        if (pid != prof_data.pid)    { goto inc; }

        addr  = (void *) (sample->addr);
        block = ADDR_PARENT_BLOCK(addr);
        tid   = OS_TID_TO_HM_TID(sample->tid);

        /*
         * As an optimization, let's do the first lookup in the thread
         * that the sample occurred in.
         * The chances are high that many accesses to data will occur in
         * the thread that allocated it.
         *
         * Of course, this is not always the case, but it doesn't hurt to
         * check the sample thread first.
         */
        PROF_THREAD_LOCK(tid); {
            thr = &prof_data.thread_objects[tid];

            if (thr->is_initialized) {
                if ((m_obj = hash_table_get_val(thr->blocks, block))) {
                    /*
                     * !!! NOTE:
                     * We are intentionally not unlocking here.
                     * We need the thread to be locked and we don't
                     * want the object to be removed and freed in the
                     * time it takes us to unlock here and then lock
                     * again later.
                     */
/*                     PROF_THREAD_UNLOCK(thr->tid); */
                    goto found;
                }
            }
        } PROF_THREAD_UNLOCK(tid);

        /* Look through all the threads to find the object. */
        LOCKING_THREAD_TRAVERSE(thr) {
            if (!thr->is_initialized || thr->tid == tid) {
                /* Invalid or already checked. */
                continue;
            }

            if ((m_obj = hash_table_get_val(thr->blocks, block))) {
                /*
                 * !!! NOTE:
                 * We are intentionally not unlocking here.
                 * We need the thread to be locked and we don't
                 * want the object to be removed and freed in the
                 * time it takes us to unlock here and then lock
                 * again later.
                 */
/*                 PROF_THREAD_UNLOCK(thr->tid); */
                goto found;
            }
        }

        /* If we're here, we haven't found the object. */
        goto inc;

found:
        obj = *m_obj;
        tsc = sample->time;

        if (tsc < obj->m_ns) {
            /*
             * This most likely indicates that we've found a sample for
             * an object that has been freed in the time between the actual
             * event and now. Ignore it.
             */
            if (!prof_data.should_stop) {
                PROF_THREAD_UNLOCK(thr->tid);
                goto inc;
            }
        }

        obj->shared |= (tid != obj->tid);

        tsc_diff = tsc - obj->m_ns;

        for (i = 0; i < N_BUCKETS; i += 1) {
            if (tsc_diff <= bucket_max_values[i]) {
                obj->read_buckets[i] += 1;
                break;
            }
        }

        obj->last_read_ns = tsc;


        /* Finally, now that we've processed the object, unlock. */
        PROF_THREAD_UNLOCK(thr->tid);

        prof_data.total_r++;

inc:
        /* Increment begin by the size of the sample */
        if (((char *)header + header->size) == base + buf_size) {
            begin = base;
        } else {
            begin = begin + header->size;
        }
    }

    /* Let perf know that we've read this far */
    profs[cpu].metadata->data_tail = head;
    __sync_synchronize();
}

internal void profile_get_accesses(void) {
    profile_obj_entry **m_obj;
    int                 i;
    float               t;

    p1_count = p2_count = 0;

    for (i = 0; i < n_profs; i += 1) {
        profile_get_r_accesses_for_cpu(i, r_profs);
        profile_get_w_accesses_for_cpu(i, w_profs);
        profile_get_p1_samples_for_cpu(i, p1_profs);
#ifndef PHASE_DCACHE
        profile_get_p2_samples_for_cpu(i, p2_profs);
#endif
    }

    t = ((float)(gettime_ns() - phase_origin_t)) / 1000000000.0;

#if defined PHASE_DCACHE
    phase_printf("%f %llu\n", t, p1_count);
#endif

#if defined PHASE_DCACHE_RATE || defined PHASE_IPC
    if (p2_count) {
        phase_printf("%f %f\n", t, ((float)p1_count / (float)p2_count));
    } else {
        phase_printf("%f 0\n", t);
    }
#endif

    /*
     * We have a choice here.
     * We can either spend the time each profiling interval to
     * write objects and clear them from the buffer as we go
     * (thus using much less memory), OR, we can save all objects
     * into the buffer until the end and write them all at once.
     * The latter strategy tends to be faster, but comes at the
     * memory cost of holding onto all object records for the
     * duration of the run.
     *
     * To use the more memory friendly strategy, uncomment the
     * define below.
     */

    (void)m_obj;

/* #define USE_LESS_MEM */

#ifdef USE_LESS_MEM
    OBJ_BUFF_LOCK(); {
        /* Write out all of the objects in the buffer. */
        array_traverse(prof_data.obj_buff, m_obj) {
            write_obj(*m_obj);
            ifree(*m_obj);
        }

        array_clear(prof_data.obj_buff);
    } OBJ_BUFF_UNLOCK();
#endif
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

void profile_get_event(int cpu, profile_info *profs, char *event) {
    int err;

    pfm_initialize();

    profs[cpu].pe.size  = sizeof(struct perf_event_attr);
    profs[cpu].pfm.size = sizeof(pfm_perf_encode_arg_t);
    profs[cpu].pfm.attr = &profs[cpu].pe;
    err = pfm_get_os_event_encoding(event, PFM_PLM2 | PFM_PLM3, PFM_OS_PERF_EVENT, &profs[cpu].pfm);

    if (err != PFM_SUCCESS) {
        LOG("Couldn't find an appropriate event to use. (error %d) Aborting.\n", err);
        ASSERT(0, "couldn't find perf event");
    }

    /* perf_event_open */
    profs[cpu].pe.sample_type    =   PERF_SAMPLE_TID
                                   | PERF_SAMPLE_TIME
                                   | PERF_SAMPLE_ADDR
                                   | PERF_SAMPLE_DATA_SRC;
    profs[cpu].pe.mmap           = 1;
    profs[cpu].pe.exclude_kernel = 1;
    profs[cpu].pe.exclude_hv     = 1;
    profs[cpu].pe.precise_ip     = 2;
/*     profs[cpu].pe.precise_ip     = 0; */
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
        ioctl(w_profs[i].fd, PERF_EVENT_IOC_DISABLE, 0);
        close(w_profs[i].fd);
        ioctl(r_profs[i].fd, PERF_EVENT_IOC_DISABLE, 0);
        close(r_profs[i].fd);
        ioctl(p1_profs[i].fd, PERF_EVENT_IOC_DISABLE, 0);
        close(p1_profs[i].fd);
        ioctl(p2_profs[i].fd, PERF_EVENT_IOC_DISABLE, 0);
        close(p2_profs[i].fd);
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

internal void profile_init_events(profile_info *profs, char *event) {
    int fd;

    /* Use libpfm to fill the pe structs */
    fd      = 0;
    n_profs = 0;
    while (fd != -1) {
        profile_get_event(n_profs, profs, event);

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

            LOG("(profile) perf event '%s' opened from tid %d (os %d) for cpu %d\n", event, get_this_tid(), os_get_tid(), n_profs);
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
}

internal void profile_init(void) {
    doing_profiling = !!getenv("HMALLOC_PROFILE");

    if (!doing_profiling)    { return; }

    prof_data.fd = open("hmalloc.profile", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT(prof_data.fd >= 0, "could not open profile output file");
    LOG("(profile) opened profile output file\n");
    write_header();

    prof_data.phase_fd = open("hmalloc.phase_profile", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT(prof_data.phase_fd >= 0, "could not open phase profile output file");
    LOG("(profile) opened phase profile output file\n");
    phase_printf("0 0\n");
    phase_origin_t = gettime_ns();

    pthread_mutex_init(&prof_data.obj_buff_mtx, NULL);
    prof_data.obj_buff = array_make(profile_obj_entry_ptr);
    LOG("(profile) created object buffer\n");

    pthread_mutex_lock(&access_profile_flush_signal_mutex);

    prof_data.tid = get_this_tid();
    prof_data.pid = getpid();

    prof_data.pagesize = (size_t) sysconf(_SC_PAGESIZE);

    profile_init_events(w_profs, "MEM_INST_RETIRED:ALL_STORES");
    profile_init_events(r_profs, "MEM_LOAD_UOPS_RETIRED.L3_MISS");

#ifdef PHASE_DCACHE
    profile_init_events(p1_profs, "MEM_LOAD_RETIRED.L1_MISS");
#endif
#ifdef PHASE_DCACHE_RATE
    profile_init_events(p1_profs, "MEM_LOAD_RETIRED.L1_HIT");
    profile_init_events(p2_profs, "MEM_LOAD_RETIRED.L3_HIT");
#endif
#ifdef PHASE_IPC
    profile_init_events(p1_profs, "INST_RETIRED.ALL");
/*     profile_init_events(p2_profs, "CPU_CLK_UNHALTED.THREAD"); */
    profile_init_events(p2_profs, "CPU_CLK_THREAD_UNHALTED.THREAD_P");
#endif

/*     profile_init_events(p_profs, "ICACHE_16B:IFTAG_MISS"); */
/*     profile_init_events(p_profs, "ICACHE_16B:IFDATA_STALL"); */
/*     profile_init_events(p_profs, "FRONTEND_RETIRED.L1I_MISS_PS"); */
/*     profile_init_events(p_profs, "FRONTEND_RETIRED.L1I_MISS"); */
/*     profile_init_events(p_profs, "MEM_LOAD_UOPS_RETIRED.L3_MISS"); */

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
    obj->m_ns        = gettime_ns();

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
        obj->f_ns = gettime_ns();
        OBJ_BUFF_LOCK(); {
            array_push(prof_data.obj_buff, obj);
        } OBJ_BUFF_UNLOCK();
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
    prof_thread_objects *thr;
    u64                  n_objects;

    if (!prof_data.thread_started) {
        return;
    }

    prof_data.should_stop = 1;
    stop_profile_thread();

    OBJ_BUFF_LOCK(); {
        LOCKING_THREAD_TRAVERSE(thr) {
            if (!thr->is_initialized) {
                continue;
            }

            hash_table_traverse(thr->blocks, block, obj) {
                (void)block;
                (*obj)->f_ns = gettime_ns();
                array_push(prof_data.obj_buff, *obj);
            }
        }

        n_objects = array_len(prof_data.obj_buff);

        /* Write out all of the objects in the buffer. */
        array_traverse(prof_data.obj_buff, obj) {
            write_obj(*obj);
            /*
             * I don't care about freeing the memory for the
             * objects. We're going away anyways.
             */
        }
    } OBJ_BUFF_UNLOCK();

    profile_putc_flush();


    LOG("(profile) %llu objects\n", n_objects);
    LOG("(profile) %llu total read samples\n", prof_data.total_r);
    LOG("(profile) %llu total write samples\n", prof_data.total_w);
}
