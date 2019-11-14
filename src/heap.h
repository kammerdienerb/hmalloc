#ifndef __HEAP_H__
#define __HEAP_H__

#include "internal.h"
#include "hash_table.h"

#include <pthread.h>

/* Chunk header flags */
#define CHUNK_IS_FREE (UINT16_C(0x0001))
#define CHUNK_IS_BIG  (UINT16_C(0x0002))
/* #define              (0x0004ULL) */
/* #define              (0x0008ULL) */

typedef union {
    struct {
        u64 offset_prev_words  : 20;
        u64 offset_next_words  : 20;
        u64 size               : 22;
        u64 flags              : 2;
    };
    u64 __header;
} chunk_header_t;

#define MAX_SMALL_CHUNK  (DEFAULT_BLOCK_SIZE - sizeof(block_header_t) - sizeof(chunk_header_t))

typedef struct cblock_header {
    chunk_header_t       *free_list_head,
                         *free_list_tail;
    struct cblock_header *prev;
    void                 *end;
} cblock_header_t;

#define BLOCK_KIND_CBLOCK (0x1)
#define BLOCK_KIND_SBLOCK (0x2)

typedef struct sblock_header {
    u64                   bitfield_available_regions;
    struct sblock_header *prev;
    void                 *end;
    u32                   n_empty_regions;
} sblock_header_t;

typedef struct {
    u64 bitfield_taken_slots;
} sblock_region_header_t;

#define ALL_REGIONS_AVAILABLE (0x7FFFFFFFFFFFFFFFULL)
#define ALL_REGIONS_FULL      (0ULL)
#define ALL_SLOTS_AVAILABLE   (0ULL)
#define ALL_SLOTS_TAKEN       (0xFFFFFFFFFFFFFFFFULL)

#define SBLOCK_SLOT_SIZE      (1024ULL)
#define SBLOCK_MAX_ALLOC_SIZE (SBLOCK_SLOT_SIZE - sizeof(sblock_region_header_t))
#define SBLOCK_REGION_SIZE    (64ULL * SBLOCK_SLOT_SIZE)

#define SBLOCK_GET_REGION(sblock, N)                           \
    ((sblock_region_header_t*)(                                \
      ((void*)(sblock)) + ((N) * (64ULL * SBLOCK_SLOT_SIZE))))

#define REGION_GET_SLOT(r, N)                                 \
    (likely(((N) > 0))                                        \
        ? ((void*)(region)) + ((N) * SBLOCK_SLOT_SIZE)        \
        : ((void*)(region)) + sizeof(sblock_region_header_t))



typedef struct {
    union {
        char *handle;
        u16   tid;
    };
    u32       hid;
    u16       flags;
} heap__meta_t;


typedef struct {
    union {
        cblock_header_t c;
        sblock_header_t s;
    };
    heap__meta_t        heap__meta;
    u16                 tid;
    u8                  block_kind;
} block_header_t;


#define ADDR_PARENT_BLOCK(addr) \
    ((block_header_t*)(void*)(((u64)(void*)(addr)) & ~(DEFAULT_BLOCK_SIZE - 1)))


#define CHUNK_SIZE(addr) (((chunk_header_t*)((void*)(addr)))->size << 3ULL)

#define SET_CHUNK_SIZE(addr, sz) do {                          \
    ASSERT(IS_ALIGNED(sz, 8), "chunk size not aligned");       \
    ((chunk_header_t*)((void*)(addr)))->size = ((sz) >> 3ULL); \
} while (0)

#define SMALL_CHUNK_ADJACENT(addr) \
    ((chunk_header_t*)(((void*)addr) + sizeof(chunk_header_t) + CHUNK_SIZE(addr)))

#define SET_CHUNK_OFFSET_PREV(addr, off) do {                 \
    ASSERT(IS_ALIGNED((off), 8), "offset prev not aligned");  \
    ((chunk_header_t*)((void*)(addr)))->offset_prev_words     \
        = ((off) >> 3ULL);                                    \
} while (0)

#define SET_CHUNK_OFFSET_NEXT(addr, off) do {                 \
    ASSERT(IS_ALIGNED((off), 8), "offset next not aligned");  \
    ((chunk_header_t*)((void*)(addr)))->offset_next_words     \
        = ((off) >> 3ULL);                                    \
} while (0)

#define CHUNK_PREV(addr)  ((chunk_header_t*)(unlikely(((chunk_header_t*)(addr))->offset_prev_words == 0)) \
                             ? NULL                                                                       \
                             :   ((void*)(addr))                                                          \
                               - (((chunk_header_t*)(addr))->offset_prev_words << 3ULL))

#define CHUNK_NEXT(addr)  ((chunk_header_t*)(unlikely(((chunk_header_t*)(addr))->offset_next_words == 0) \
                             ? NULL                                                                      \
                             :   ((void*)(addr))                                                         \
                               + (((chunk_header_t*)(addr))->offset_next_words << 3ULL)))

#define CHUNK_USER_MEM(addr) (((void*)(addr)) + sizeof(chunk_header_t))

#define CHUNK_FROM_USER_MEM(addr) ((chunk_header_t*)(((void*)(addr)) - sizeof(chunk_header_t)))

#define CHUNK_DISTANCE(a, b) (((void*)(a)) - (((void*)(b))))

#define CHUNK_PARENT_BLOCK(addr) \
    ADDR_PARENT_BLOCK(addr)


#define CBLOCK_FIRST_CHUNK(addr) (((void*)(addr)) + sizeof(block_header_t))

#define LARGEST_CHUNK_IN_EMPTY_N_PAGE_BLOCK(N) \
    (((N) << system_info.log_2_page_size) - sizeof(block_header_t) - sizeof(chunk_header_t))



#define HEAP_QL_TINY_CHUNK_SIZE     (KiB(2))
#define HEAP_QL_TINY_ARRAY_SIZE     (32)
#define HEAP_QL_NOT_TINY_ARRAY_SIZE (32)

#define HEAP_THREAD (0x1)
#define HEAP_USER   (0x2)

internal u32 hid_counter;

typedef struct {
    cblock_header_t  *cblocks_head,
                     *cblocks_tail,
                     *big_chunk_cblocks_tail;
#ifdef HMALLOC_USE_SBLOCKS
    sblock_header_t  *sblocks_head,
                     *sblocks_tail;
#endif
    heap__meta_t      __meta;
    pthread_mutex_t   mtx;
} heap_t;

internal void heap_make(heap_t *heap);
internal void * heap_alloc(heap_t *heap, u64 n_bytes);

typedef char *heap_handle_t;

#define malloc imalloc
#define free   ifree
use_hash_table(heap_handle_t, heap_t);
#undef malloc
#undef free

internal hash_table(heap_handle_t, heap_t) user_heaps;

pthread_mutex_t user_heaps_lock = PTHREAD_MUTEX_INITIALIZER;
#define USER_HEAPS_LOCK()   HMALLOC_MTX_LOCKER(&user_heaps_lock)
#define USER_HEAPS_UNLOCK() HMALLOC_MTX_UNLOCKER(&user_heaps_lock)

internal void user_heaps_init(void);
internal heap_t * get_or_make_user_heap(char *handle);

/* @eden */
#undef  MAX_SMALL_CHUNK
#define MAX_SMALL_CHUNK (SBLOCK_MAX_ALLOC_SIZE)

#endif
