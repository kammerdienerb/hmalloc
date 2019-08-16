#include "thread.h"
#include "heap.h"

internal void thread_local_data_init(thread_local_data_t *info, u16 id) {
    info->id             = id;
    info->heap           = heap_make();
    info->heap.thread_id = id;
}

internal void thread_local_data_fini(thread_local_data_t *info) {

}

internal void thread_local_init(void) {
    pthread_mutex_init(&thread_local_data_lock, NULL);
    pthread_key_create(&thread_local_data_key, (void(*)(void*))thread_local_data_fini);
    LOG("initialized thread-local data management\n");
}

internal void thread_local_fini(void) {
    pthread_key_delete(thread_local_data_key);
    pthread_mutex_destroy(&thread_local_data_lock);
    LOG("finished thread-local data\n");
}

internal thread_local_data_t* get_thread_local_struct(void) {
    thread_local_data_t *info;
    u16                  idx;
    
    info = pthread_getspecific(thread_local_data_key);

    if (info == NULL) {
        pthread_mutex_lock(&thread_local_data_lock);
        idx  = n_thread_local_datas++;
        ASSERT(idx < HMALLOC_MAX_THREADS, "exceeded the maximum number of threads"); 
        info = thread_local_datas + idx;
        pthread_setspecific(thread_local_data_key, info);
        thread_local_data_init(info, idx);
        LOG("initialized thread-local data %hu\n", idx);
        pthread_mutex_unlock(&thread_local_data_lock);
    }

    return info;
}
