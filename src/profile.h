#ifndef __PROFILE_H__
#define __PROFILE_H__

#include "internal.h"
#include "thread.h"
#include "hash_table.h"
#include "tree.h"
#include "array.h"

#include <pthread.h>
#include <linux/perf_event.h>
#include <poll.h>
#include <perfmon/pfmlib_perf_event.h>

internal i32 doing_profiling;

typedef void *block_addr_t;
static inline u64 block_addr_hash(void *b) {
    return ((u64)b) >> LOG2_64BIT(DEFAULT_BLOCK_SIZE);
}

internal u64 bucket_max_values[] = {
    10000,
    100000,
    1000000,
    10000000,
    100000000,
    1000000000,
    10000000000,
    100000000000,
    1000000000000,
    UINT64_MAX
};

#define N_BUCKETS (sizeof(bucket_max_values) / sizeof(u64))

typedef struct {
    void  *addr;
    u64    size;
    char  *heap_handle;
    i32    tid;
    i32    shared;
    u64    m_ns;
    u64    f_ns;
    u64    write_buckets[N_BUCKETS];
    u64    last_write_ns;
    u64    l1_hit_w;
    u64    read_buckets[N_BUCKETS];
    u64    last_read_ns;
} profile_obj_entry, *profile_obj_entry_ptr;


#define malloc imalloc
#define free   ifree
use_tree(u64, u32);
use_hash_table(block_addr_t, profile_obj_entry_ptr);
#undef malloc
#undef free

typedef struct {
    hash_table(block_addr_t, profile_obj_entry_ptr) blocks;
    int                                             is_initialized;
    u16                                             tid;
} prof_thread_objects;

typedef struct {
    i32                 thread_started;
    i32                 should_stop;
    u32                 nom_freq;
    prof_thread_objects thread_objects[HMALLOC_MAX_THREADS];
    array_t             obj_buff;
    int                 fd;
    int                 phase_fd;
    i32                 tid;
    i32                 pid;
    u64                 pagesize;
    u64                 total_w, total_r;
    pthread_mutex_t     mtx, obj_buff_mtx;
    pthread_t           profile_id;
} profile_data;

internal profile_data prof_data;

internal tree(u64, u32) phase_data;

pthread_mutex_t prof_thread_mutices[HMALLOC_MAX_THREADS]
    = { [0 ... (HMALLOC_MAX_THREADS - 1)] = PTHREAD_MUTEX_INITIALIZER };

pthread_mutex_t access_profile_flush_mutex        = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t access_profile_flush_signal_mutex = PTHREAD_MUTEX_INITIALIZER;
#define PROF_THREAD_LOCK(tid)   HMALLOC_MTX_LOCKER(&prof_thread_mutices[(tid)])
#define PROF_THREAD_UNLOCK(tid) HMALLOC_MTX_UNLOCKER(&prof_thread_mutices[(tid)])
#define OBJ_BUFF_LOCK()         HMALLOC_MTX_LOCKER(&prof_data.obj_buff_mtx)
#define OBJ_BUFF_UNLOCK()       HMALLOC_MTX_UNLOCKER(&prof_data.obj_buff_mtx)

/*
 * If you use 'break' in one of these loops without unlocking first,
 * you will be sad.
 */
#define LOCKING_THREAD_TRAVERSE(thr_ptr)                            \
    for (int _thr_it = 0;                                           \
        (_thr_it < HMALLOC_MAX_THREADS)                             \
            && ((thr_ptr = prof_data.thread_objects + _thr_it),     \
                (pthread_mutex_lock(prof_thread_mutices + _thr_it)), \
                1);                                                 \
         pthread_mutex_unlock(prof_thread_mutices + _thr_it),       \
         _thr_it += 1)

struct __attribute__ ((__packed__)) sample {
    u32 pid;
    u32 tid;
    u64 time;
    u64 addr;
    union perf_mem_data_src data_src;
};

typedef struct {
    /* For perf */
    size_t                       size;
    struct perf_event_attr       pe;
    struct perf_event_mmap_page *metadata;
    int                          fd;
    struct pollfd                pfd;

    /* For libpfm */
    pfm_perf_encode_arg_t        pfm;
} profile_info;



internal void profile_init(void);
internal void profile_fini(void);
internal void profile_add_block(void *block, u64 size);
internal void profile_delete_block(void *block);
internal void profile_set_site(void *addr, char *site);

#endif
