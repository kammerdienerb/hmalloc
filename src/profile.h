#ifndef __PROFILE_H__
#define __PROFILE_H__

#include "internal.h"
#include "hash_table.h"
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
use_hash_table(block_addr_t, profile_obj_entry_ptr);
#undef malloc
#undef free

typedef struct {
    i32                                             thread_started;
    i32                                             should_stop;
    u32                                             nom_freq;
    hash_table(block_addr_t, profile_obj_entry_ptr) blocks;
    array_t                                         obj_buff;
    i32                                             total_allocated;
    int                                             fd;
    i32                                             tid;
    i32                                             pid;
    u64                                             pagesize;
    u64                                             total_w, total_r;
    pthread_mutex_t                                 mtx;
    pthread_t                                       profile_id;
} profile_data;

internal profile_data prof_data;

pthread_mutex_t access_profile_flush_mutex        = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t access_profile_flush_signal_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t profile_blocks_mutex              = PTHREAD_MUTEX_INITIALIZER;
#define PROF_BLOCKS_LOCK()   HMALLOC_MTX_LOCKER(&profile_blocks_mutex)
#define PROF_BLOCKS_UNLOCK() HMALLOC_MTX_UNLOCKER(&profile_blocks_mutex)

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

__attribute__((destructor))
internal void profile_dump_remaining(void);

#endif
