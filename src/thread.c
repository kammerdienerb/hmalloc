#include "internal.h"
#include "thread.h"
#include "heap.h"
#include "os.h"
#include "init.h"

internal void threads_init(void) {
    int i;

    for (i = 0; i < HMALLOC_MAX_THREADS; i += 1) {
        pthread_mutex_init(&(thread_datas[i].mtx), NULL);
    }

    LOG("initialized threads\n");
}

internal void thread_init(thread_data_t *thr, hm_tid_t tid) {
    heap_make(&thr->heap);
    thr->heap.tid = tid;
    thr->tid      = tid;
    thr->is_valid = 1;

    LOG("initialized a new thread with tid %hu\n", tid);
}

internal thread_data_t * acquire_this_thread(void) {
    pid_t    os_tid;
    hm_tid_t tid;

    /* Ensure our system is initialized. */
    hmalloc_init();

    os_tid = os_get_tid();
    tid    = os_tid & (HMALLOC_MAX_THREADS - 1);

    return acquire_thread(tid);
}

internal thread_data_t * acquire_thread(hm_tid_t tid) {
    thread_data_t *thr;

    /* Ensure our system is initialized. */
    hmalloc_init();

    thr = thread_datas + tid;

    THR_LOCK(thr);

    if (!thr->is_valid) {
        thread_init(thr, tid);
    }

    return thr;
}

internal void release_thread(thread_data_t *thr) {
    THR_UNLOCK(thr);
}
