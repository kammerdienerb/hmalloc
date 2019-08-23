#include "internal.h"
#include "thread.h"
#include "heap.h"
#include "init.h"

internal void thread_local_data_init(thread_local_data_t *info, u16 idx, pthread_t tid) {
    info->heap            = heap_make();
    info->heap.thread_idx = idx;
    info->idx             = idx;
    info->tid             = tid;
    info->is_valid        = 1;
}

internal void thread_local_data_fini(thread_local_data_t *info) {

}

internal void thread_local_init(void) {
    LOG("initialized thread-local data management\n");
}

internal void thread_local_fini(void) {
    pthread_mutex_destroy(&thread_local_data_lock);
    LOG("finished thread-local data\n");
}

internal thread_local_data_t* get_thread_local_struct(void) {
    thread_local_data_t *info;
    u16                  idx;
    pthread_t            tid;

    /* I know this isn't techinically portable but... */
    tid  = pthread_self();
    idx  = ((u64)tid) & (HMALLOC_MAX_THREADS - 1);
    info = thread_local_datas + idx;

    while (info->is_valid) {
        if (info->tid == tid) {
            return info;
        }
        info = thread_local_datas + idx++;
    }

    /* Create the new thread_local_data_t. */
    pthread_mutex_lock(&thread_local_data_lock);
        /* Ensure our system is initialized. */
        hmalloc_init(); 

        /* ASSERT(n_thread_local_datas < HMALLOC_MAX_THREADS, */
        /*        "exceeded the maximum number of threads"); */

        thread_local_data_init(info, idx, tid);

        LOG("initialized thread-local data %hu\n", idx);
    pthread_mutex_unlock(&thread_local_data_lock);

    return info;
}
