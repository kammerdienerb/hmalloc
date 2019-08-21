#include "internal.h"
#include "thread.h"
#include "heap.h"
#include "init.h"

internal void thread_local_data_init(thread_local_data_t *info, u16 id) {
    info->id              = id;
    info->heap            = heap_make();
    info->heap.thread_idx = id;
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

internal __thread thread_local_data_t *t_local;

internal thread_local_data_t* get_thread_local_struct(void) {
    thread_local_data_t *info;
    u16                  idx;

    if (t_local == NULL) {
        pthread_mutex_lock(&thread_local_data_lock);
            /* Ensure our system is initialized. */
            hmalloc_init(); 

            ASSERT(n_thread_local_datas < HMALLOC_MAX_THREADS,
                   "exceeded the maximum number of threads");

            idx  = n_thread_local_datas++;
            info = thread_local_datas + idx;
            thread_local_data_init(info, idx);

            LOG("initialized thread-local data %hu\n", idx);
        pthread_mutex_unlock(&thread_local_data_lock);

        t_local = info;
    }

    return t_local;
}
