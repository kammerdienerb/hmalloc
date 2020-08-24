#include "internal.h"
#include "thread.h"
#include "heap.h"
#include "os.h"
#include "init.h"

internal thread_data_t           thread_datas[HMALLOC_MAX_THREADS];
internal mutex_t                 thread_datas_lock;
internal u32                     num_cores;
internal __thread thread_data_t *local_thr;

internal void threads_init(void) {
    mutex_init(&thread_datas_lock);
    num_cores = os_get_num_cpus() + 1;
    LOG("using %u thread local heaps\n", num_cores);
    LOG("initialized threads\n");
}

HMALLOC_ALWAYS_INLINE
internal inline hm_tid_t get_this_tid(void) {
    return OS_TID_TO_HM_TID(os_get_tid());
}

internal void dec_thr_ref(void *_thr) {
    thread_data_t *thr;

    thr = _thr;

    mutex_lock(&thread_datas_lock); {
        ASSERT(thr->ref_count > 0, "invalid thread_data_t ref_count");
        thr->ref_count -= 1;
    } mutex_unlock(&thread_datas_lock);
}

HMALLOC_ALWAYS_INLINE
internal inline u32 get_next_cpu_idx(void) {
    u32 i;
    int all_same;
    u32 min_ref;
    u32 min_ref_idx;

    /* Try to find the thread_data_t with the lowest ref count. */
    all_same    = 1;
    min_ref     = thread_datas[0].ref_count;
    min_ref_idx = 0;
    for (i = 1; i < num_cores; i += 1) {
        if (thread_datas[i].ref_count != min_ref) {
            all_same = 0;
        }
        if (thread_datas[i].ref_count < min_ref) {
            min_ref     = thread_datas[i].ref_count;
            min_ref_idx = i;
        }
    }

    if (all_same) { return 0; }

    return min_ref_idx;
}

HMALLOC_ALWAYS_INLINE
internal inline thread_data_t * acquire_thr(void) {
    int            cpu_idx;
    thread_data_t *thr;
    hm_tid_t       tid;

    /* Ensure our system is initialized. */
    hmalloc_init();

    mutex_lock(&thread_datas_lock); {

        cpu_idx = get_next_cpu_idx();

        ASSERT(cpu_idx != -1,                 "sched_getcpu() failed");
        ASSERT(cpu_idx < HMALLOC_MAX_THREADS, "all thread_data locations taken");

        thr = thread_datas + cpu_idx;

        if (unlikely(!thr->is_valid)) {
            heap_make(&thr->heap);
            thr->heap.__meta.flags |= HEAP_THREAD;
            thr->is_valid           = 1;
            thr->id                 = cpu_idx;
            thr->ref_count          = 0;
            LOG("initialized a new thread with id %llu\n", thr->id);
        }

        tid                   = get_this_tid();
        thr->tid              = tid;
        thr->ref_count       += 1;
        thr->heap.__meta.tid  = tid;
        LOG("hid %d is a thread heap (tid = '%d')\n", thr->heap.__meta.hid, thr->tid);

    } mutex_unlock(&thread_datas_lock);

    return thr;
}

HMALLOC_ALWAYS_INLINE
internal inline thread_data_t * get_this_thread(void) {
    if (unlikely(local_thr == NULL)) {
        local_thr = acquire_thr();
        pthread_key_create(&local_thr->key, dec_thr_ref);
        pthread_setspecific(local_thr->key, (void*)local_thr);
    }

    return local_thr;
}

HMALLOC_ALWAYS_INLINE
internal inline heap_t * get_this_thread_heap(void) {
    return &get_this_thread()->heap;
}

HMALLOC_ALWAYS_INLINE
internal inline heap_t * get_user_heap(heap_handle_t handle) {
    /* Ensure our system is initialized. */
    hmalloc_init();
    return get_or_make_user_heap(handle);
}
