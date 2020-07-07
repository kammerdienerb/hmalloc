#ifndef __PROFILE_H__
#define __PROFILE_H__

#include "internal.h"
#include "thread.h"
#include "hash_table.h"
#include "array.h"
#include "clear_refs_ranges.h"

#include <pthread.h>

internal i32 doing_profiling;

typedef void *block_addr_t;
static inline u64 block_addr_hash(void *b) {
    return ((u64)b) >> LOG2_64BIT(DEFAULT_BLOCK_SIZE);
}

typedef struct {
    void  *addr;
    u64    size;
    char  *heap_handle;
    i32    tid;
} profile_obj_entry, *profile_obj_entry_ptr;

#define malloc imalloc
#define free   ifree
use_hash_table(block_addr_t, profile_obj_entry_ptr);
#undef malloc
#undef free

typedef struct {
    hash_table(block_addr_t, profile_obj_entry_ptr) blocks;
    int                                             is_initialized;
    u16                                             tid;
} prof_thread_objects;

typedef struct {
    i32                  thread_started;
    i32                  should_stop;
    prof_thread_objects  thread_objects[HMALLOC_MAX_THREADS];
    int                  fd;
    i32                  tid;
    i32                  pid;
    u64                  pagesize;
    pthread_mutex_t      mtx;
    pthread_t            profile_id;
    struct crr_t         crr;
} profile_data;

internal profile_data prof_data;

pthread_mutex_t prof_thread_mutices[HMALLOC_MAX_THREADS]
    = { [0 ... (HMALLOC_MAX_THREADS - 1)] = PTHREAD_MUTEX_INITIALIZER };

#define PROF_THREAD_LOCK(tid)   HMALLOC_MTX_LOCKER(&prof_thread_mutices[(tid)])
#define PROF_THREAD_UNLOCK(tid) HMALLOC_MTX_UNLOCKER(&prof_thread_mutices[(tid)])

/*
 * If you use 'break' in one of these loops without unlocking first,
 * you will be sad.
 */
#define LOCKING_THREAD_TRAVERSE(thr_ptr)                             \
    for (int _thr_it = 0;                                            \
        (_thr_it < HMALLOC_MAX_THREADS)                              \
            && ((thr_ptr = prof_data.thread_objects + _thr_it),      \
                (pthread_mutex_lock(prof_thread_mutices + _thr_it)), \
                1);                                                  \
         pthread_mutex_unlock(prof_thread_mutices + _thr_it),        \
         _thr_it += 1)

internal void profile_init(void);
internal void profile_fini(void);
internal void profile_add_block(void *block, u64 size);
internal void profile_delete_block(void *block);
internal void profile_set_site(void *addr, char *site);

#endif
