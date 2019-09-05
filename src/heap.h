#ifndef __HEAP_H__
#define __HEAP_H__

#include "internal.h"

#include <pthread.h>

/* Chunk header flags */
#define CHUNK_IS_FREE (UINT16_C(0x0001))
#define CHUNK_IS_BIG  (UINT16_C(0x0002))
/* #define              (0x0004ULL) */
/* #define              (0x0008ULL) */

typedef union {
    struct { /* When chunk is in free list: */
        u64 offset_prev_words  : 18;
        u64 offset_next_words  : 18;
        u64 size               : 26;
        u64 flags              : 2;
    };
    u64 __header;
} chunk_header_t;

#define MAX_SMALL_CHUNK  (DEFAULT_BLOCK_SIZE - sizeof(block_header_t) - sizeof(chunk_header_t))
#define MAX_BIG_CHUNK    ((((1ULL << 26ULL) - 1ULL) << system_info.log_2_page_size) - sizeof(chunk_header_t))

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

#define SBLOCK_SLOT_SIZE      (512ULL)
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
        cblock_header_t c;
        sblock_header_t s;
    };
    u8  block_kind;
    u16 tid;
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

typedef struct {
    u16               tid;
    cblock_header_t  *cblocks_head,
                     *cblocks_tail,
                     *big_chunk_cblocks_tail;
#ifdef HMALLOC_USE_SBLOCKS
    sblock_header_t  *sblocks_head,
                     *sblocks_tail;
#endif
} heap_t;

internal void heap_make(heap_t *heap);
internal void * heap_alloc(heap_t *heap, u64 n_bytes);

#endif
