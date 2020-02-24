#ifndef __THREAD_H__
#define __THREAD_H__

#include "internal.h"
#include "heap.h"

#include <pthread.h>


/*
 * @TODO
 *
 * HMALLOC_MAX_THREADS should be something that's configured
 * at run time -- either through an environment variable,
 * or computed on initialization by checking the number of
 * threads on the system.
 *                                                -- Brandon
 */


#ifndef HMALLOC_MAX_THREADS
#define HMALLOC_MAX_THREADS (512ULL)
#endif

#if HMALLOC_MAX_THREADS > (1 << 16)
    #error "Can't represent this many threads."
#endif

#if !IS_POWER_OF_TWO_PP(HMALLOC_MAX_THREADS)
    #error "HMALLOC_MAX_THREADS must be a power of two!"
#endif

#define LOG_2_HMALLOC_MAX_THREADS (LOG2_64BIT(HMALLOC_MAX_THREADS))

typedef u16 hm_tid_t;

typedef struct {
    heap_t           heap;
    hm_tid_t         tid;
    pthread_mutex_t  mtx;
    int              is_valid;
    char            *cur_allocating_site;
} thread_data_t;

internal thread_data_t   thread_datas[HMALLOC_MAX_THREADS];
internal pthread_mutex_t thread_datas_mtx = PTHREAD_MUTEX_INITIALIZER;

internal __thread thread_data_t *local_thr;

internal void threads_init(void);
internal void thread_init(thread_data_t *thr, hm_tid_t tid);
internal thread_data_t * acquire_this_thread(void);
internal thread_data_t * acquire_thread(hm_tid_t tid);
internal void release_thread(thread_data_t *thr);

internal heap_t * acquire_this_thread_heap(void);
internal heap_t * acquire_thread_heap(hm_tid_t tid);
internal heap_t * acquire_user_heap(heap_handle_t handle);
internal void release_heap(heap_t *heap);

internal hm_tid_t get_this_tid(void);

#define HEAP_LOCK(heap_ptr)   HMALLOC_MTX_LOCKER(&heap_ptr->mtx)
#define HEAP_UNLOCK(heap_ptr) HMALLOC_MTX_UNLOCKER(&heap_ptr->mtx)

#define THR_LOCK(thr_ptr)   HMALLOC_MTX_LOCKER(&thr_ptr->mtx)
#define THR_UNLOCK(thr_ptr) HMALLOC_MTX_UNLOCKER(&thr_ptr->mtx)

#define THR_DATA_LOCK(thr_ptr)   HMALLOC_MTX_LOCKER(&thread_datas_mtx)
#define THR_DATA_UNLOCK(thr_ptr) HMALLOC_MTX_UNLOCKER(&thread_datas_mtx)

#define OS_TID_TO_HM_TID(os_tid) ((os_tid) & (HMALLOC_MAX_THREADS - 1))

#endif
