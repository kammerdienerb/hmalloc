#include "internal.h"
#include "thread.h"
#include "heap.h"
#include "os.h"
#include "init.h"

internal thread_data_t           thread_datas[HMALLOC_MAX_THREADS];
internal mutex_t                 thread_datas_lock;
internal u32                     cur_cpu_idx;
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

HMALLOC_ALWAYS_INLINE
internal inline u32 get_next_cpu_idx(void) {
    u32 i;

    i = cur_cpu_idx;

    if (cur_cpu_idx == num_cores - 1) {
        cur_cpu_idx = 0;
    } else {
        cur_cpu_idx += 1;
    }

    return i;
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
            LOG("initialized a new thread with id %llu\n", thr->id);
        }

        tid                  = get_this_tid();
        thr->tid             = tid;
        thr->heap.__meta.tid = tid;
        LOG("hid %d is a thread heap (tid = '%d')\n", thr->heap.__meta.hid, thr->tid);

    } mutex_unlock(&thread_datas_lock);

    return thr;
}

HMALLOC_ALWAYS_INLINE
internal inline thread_data_t * get_this_thread(void) {
    if (unlikely(local_thr == NULL)) {
        local_thr = acquire_thr();
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
