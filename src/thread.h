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

typedef u16 hm_tid_t;

typedef struct {
    heap_t          heap;
    hm_tid_t        tid;
    pthread_mutex_t mtx;
    int             is_valid;
} thread_data_t;

internal thread_data_t thread_datas[HMALLOC_MAX_THREADS];

internal void threads_init(void);
internal void thread_init(thread_data_t *thr, hm_tid_t tid);
internal thread_data_t * acquire_this_thread(void);
internal thread_data_t * acquire_thread(hm_tid_t tid);
internal void release_thread(thread_data_t *thr);

#define THR_LOCK(thr_ptr)   HMALLOC_MTX_LOCKER(&thr_ptr->mtx)
#define THR_UNLOCK(thr_ptr) HMALLOC_MTX_UNLOCKER(&thr_ptr->mtx)

#endif
