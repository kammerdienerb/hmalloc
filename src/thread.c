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

internal thread_data_t * acquire_locally(void) {
    THR_LOCK(local_thr);
    return local_thr;
}

internal thread_data_t * acquire_this_thread(void) {
    pid_t          os_tid;
    hm_tid_t       tid;
    thread_data_t *thr;
    int            count;

    if (local_thr != NULL) {
        return acquire_locally();
    }

    /*
     * A thread_data_t for this thread hasn't been
     * assigned for this thread yet.
     */

    /* Ensure our system is initialized. */
    hmalloc_init();

    /*
     * Starting point in the thread_datas array.
     */
    os_tid = os_get_tid();
    tid    = os_tid & (HMALLOC_MAX_THREADS - 1);

    THR_DATA_LOCK(); {
        /*
         * Walk through the thread_data slots until we find one that
         * is vacant.
         */
        for (count = 0; count < HMALLOC_MAX_THREADS; count += 1) {
            thr = thread_datas + tid;

            if (!thr->is_valid) {
                /*
                 * Found one. Initialize it.
                 */
                thread_init(thr, tid);
                local_thr = thr;
                break;
            }

            tid = (tid + 1) & (HMALLOC_MAX_THREADS - 1);
        }

        ASSERT(count < HMALLOC_MAX_THREADS, "exceeded HMALLOC_MAX_THREADS");
    } THR_DATA_UNLOCK();

    THR_LOCK(local_thr);

    return local_thr;
}

internal thread_data_t * acquire_thread(hm_tid_t tid) {
    thread_data_t *thr;

    /* @temp */
    /* tid = 0; */

    /* Ensure our system is initialized. */
    hmalloc_init();

    thr = thread_datas + tid;

    THR_LOCK(thr);

    ASSERT(thr->is_valid, "invalid thread_data");

    return thr;
}

internal void release_thread(thread_data_t *thr) {
    THR_UNLOCK(thr);
}
