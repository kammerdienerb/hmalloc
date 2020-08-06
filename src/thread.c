#include "internal.h"
#include "thread.h"
#include "heap.h"
#include "os.h"
#include "init.h"

internal thread_data_t           thread_datas[HMALLOC_MAX_THREADS];
internal mutex_t                 thread_datas_lock;
internal __thread thread_data_t *local_thr;

internal void threads_init(void) {
    mutex_init(&thread_datas_lock);
    LOG("initialized threads\n");
}

internal void release_thr(void *_thr) {
    thread_data_t *thr;

    thr = _thr;

    mutex_lock(&thread_datas_lock); {
        thr->is_owned = 0;
    } mutex_unlock(&thread_datas_lock);
}

HMALLOC_ALWAYS_INLINE
hm_tid_t get_this_tid(void) {
    return OS_TID_TO_HM_TID(os_get_tid());
}

HMALLOC_ALWAYS_INLINE
thread_data_t * acquire_thr(void) {
    u32            idx;
    thread_data_t *thr;
    hm_tid_t       tid;

    /* Ensure our system is initialized. */
    hmalloc_init();

    mutex_lock(&thread_datas_lock); {

        for (idx = 0; idx < HMALLOC_MAX_THREADS; idx += 1) {
            thr = &thread_datas[idx];
            if (!thr->is_owned) { goto found; }
        }

        ASSERT(0, "all thread_data locations taken");
        /* This should cause obvious crashes on non-assertion builds. */
        thr = NULL;

found:;
        if (unlikely(!thr->is_valid)) {
            heap_make(&thr->heap);
            thr->heap.__meta.flags |= HEAP_THREAD;
            thr->is_valid           = 1;
            thr->id                 = idx;
            LOG("initialized a new thread with id %llu\n", thr->id);
        }

        tid                  = get_this_tid();
        thr->tid             = tid;
        thr->heap.__meta.tid = tid;
        thr->is_owned        = 1;
        LOG("hid %d is a thread heap (tid = '%d')\n", thr->heap.__meta.hid, thr->tid);

    } mutex_unlock(&thread_datas_lock);

    return thr;
}

HMALLOC_ALWAYS_INLINE
thread_data_t * get_this_thread(void) {
    if (unlikely(local_thr == NULL)) {
        local_thr = acquire_thr();

        /* @note
         * It is VERY important that this pthread code that sets up
         * the call to release_thr() on thread exit be called _after_
         * local_thr is set.
         * At this point, the thread and its heap are set up and can
         * service an allocation.
         * This must be the case because both pthread_key_create() and
         * pthread_setspecific() may allocate.
         * If the thread and its heap aren't set up yet, this will recurse
         * infinitely.
         */
        pthread_key_create(&local_thr->junk_key, release_thr);
        pthread_setspecific(local_thr->junk_key, local_thr);
    }

    return local_thr;
}

HMALLOC_ALWAYS_INLINE
heap_t * get_this_thread_heap(void) {
    return &get_this_thread()->heap;
}

HMALLOC_ALWAYS_INLINE
heap_t * get_user_heap(heap_handle_t handle) {
    /* Ensure our system is initialized. */
    hmalloc_init();
    return get_or_make_user_heap(handle);
}
