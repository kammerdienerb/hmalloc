#ifndef __THREAD_H__
#define __THREAD_H__

#include "internal.h"
#include "heap.h"

#include <pthread.h>


#ifndef HMALLOC_MAX_THREADS
#define HMALLOC_MAX_THREADS (512ULL)
#endif

#if HMALLOC_MAX_THREADS > (2 << 16)
    #error "Can't represent this many threads."
#endif

#if !IS_POWER_OF_TWO_PP(HMALLOC_MAX_THREADS)
    #error "HMALLOC_MAX_THREADS must be a power of two!"
#endif

#define LOG_2_HMALLOC_MAX_THREADS (LOG2_64BIT(HMALLOC_MAX_THREADS))

typedef struct {
    heap_t    heap;
    u16       idx;
    pthread_t tid;
    int       is_valid;
} thread_local_data_t;

internal thread_local_data_t thread_local_datas[HMALLOC_MAX_THREADS];
/* internal u16                 n_thread_local_datas; */
internal pthread_mutex_t     thread_local_data_lock = PTHREAD_MUTEX_INITIALIZER;

internal void thread_local_data_init(thread_local_data_t *info, u16 idx, pthread_t tid);
internal void thread_local_data_fini(thread_local_data_t *info);

internal void thread_local_init(void);
internal void thread_local_fini(void);
internal thread_local_data_t* get_thread_local_struct(void);

#endif
