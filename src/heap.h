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
        u64 flags              : 2;
        u64 size               : 26;
    };
    struct { /* When chunk is being used: */
        u64 offset_block_pages : 18;
        u64 tid                : 18;
        u64 __flags            : 2;
        u64 __size             : 26;
    };
    u64 __header;
} chunk_header_t;

typedef struct block_header {
    chunk_header_t      *free_list_head,
                        *free_list_tail;
    void                *end;
    struct block_header *next; 
} block_header_t;

#define MAX_SMALL_CHUNK  (DEFAULT_BLOCK_SIZE - sizeof(block_header_t) - sizeof(chunk_header_t))
#define MAX_BIG_CHUNK    (((1 << 26) * system_info.page_size) - sizeof(chunk_header_t))

#define CHUNK_SIZE(addr) (((chunk_header_t*)((void*)(addr)))->__size << 3ULL)

#define SET_CHUNK_SIZE(addr, size) do {                            \
    ASSERT(IS_ALIGNED(size, 8), "chunk size not aligned");         \
    ((chunk_header_t*)((void*)(addr)))->__size = ((size) >> 3ULL); \
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

#define SET_CHUNK_OFFSET_BLOCK(addr, block) do {               \
    ASSERT(IS_ALIGNED((block), system_info.page_size),         \
        "block not aligned");                                  \
    ((chunk_header_t*)((void*)(addr)))->offset_block_pages     \
        =   ((((void*)ALIGN((addr), system_info.page_size))    \
          - ((void*)(block)))                                  \
          >> system_info.log_2_page_size);                     \
} while (0)

#define CHUNK_PREV(addr)  ((chunk_header_t*)(((chunk_header_t*)(addr))->offset_prev_words == 0 \
                             ? NULL                                                            \
                             :   ((void*)(addr))                                               \
                               - (((chunk_header_t*)(addr))->offset_prev_words << 3ULL)))
 
#define CHUNK_NEXT(addr)  ((chunk_header_t*)(((chunk_header_t*)(addr))->offset_next_words == 0 \
                             ? NULL                                                            \
                             :   ((void*)(addr))                                               \
                               + (((chunk_header_t*)(addr))->offset_next_words << 3ULL)))

#define CHUNK_USER_MEM(addr) (((void*)(addr)) + sizeof(chunk_header_t))

#define CHUNK_FROM_USER_MEM(addr) ((chunk_header_t*)(((void*)(addr)) - sizeof(chunk_header_t)))

#define CHUNK_DISTANCE(a, b) (((void*)(a)) - (((void*)(b))))

#define CHUNK_PARENT_BLOCK(addr)                           \
    ((block_header_t*)(                                    \
          ALIGN(((void*)(addr)), system_info.page_size)    \
        - ((((chunk_header_t*)(addr))->offset_block_pages) \
            << system_info.log_2_page_size)))



#define BLOCK_FIRST_CHUNK(addr) (((void*)(addr)) + sizeof(block_header_t))

#define LARGEST_CHUNK_IN_EMPTY_N_PAGE_BLOCK(N) \
    (((system_info.page_size) * (N)) - sizeof(block_header_t) - sizeof(chunk_header_t))



#define HEAP_QL_TINY_CHUNK_SIZE     (KiB(2))
#define HEAP_QL_TINY_ARRAY_SIZE     (512)
#define HEAP_QL_NOT_TINY_ARRAY_SIZE (128)

typedef struct {
    u16               tid;
    block_header_t   *blocks_head,
                     *blocks_tail;
#ifdef HMALLOC_USE_QUICKLIST
    chunk_header_t   *quick_list_tiny[HEAP_QL_TINY_ARRAY_SIZE],
                    **quick_list_tiny_p,
                     *quick_list_not_tiny[HEAP_QL_NOT_TINY_ARRAY_SIZE],
                    **quick_list_not_tiny_p;
#endif
} heap_t;

internal void heap_make(heap_t *heap);
internal void * heap_alloc(heap_t *heap, u64 n_bytes);

#endif
