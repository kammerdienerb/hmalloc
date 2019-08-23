#ifndef __HEAP_H__
#define __HEAP_H__

#include "internal.h"

#include <pthread.h>


/* Chunk header flags */
#define CHUNK_IS_FREE (UINT16_C(0x0001))
#define CHUNK_IS_BIG  (UINT16_C(0x0002))
/* #define              (0x0004ULL) */
/* #define              (0x0008ULL) */

typedef struct {
    /*
     * For small allocations (<= MAX_SMALL_ALLOC), size is the
     * number of bytes in the user's memory.
     * This is also the offset from the end of the header to the next header.
     * For big allocations, big_n_pages represents the number of pages that 
     * make up the allocation.
     */
    union {
        u32 size;
        u32 big_n_pages;
    };

    /*
     * Chunk metadata.
     */
    union {
        struct {
            u16 flags;
            u16 thread_idx;
        };
        u32 __meta;
    };

    /*
     * Offsets to the previous and next chunks in the free list for this
     * block.
     * 0 if at end.
     * If the chunk is NOT in the free list, offset_block is the distance 
     * from the start of the block.
     */
    union {
        struct {
            u32 offset_prev;
            u32 offset_next;
        };
        u64 offset_block;
    };
} chunk_header_t;

#define MAX_SMALL_CHUNK (((u64)UINT32_MAX) - sizeof(chunk_header_t))
#define MAX_BIG_CHUNK   ((((u64)UINT32_MAX) * system_info.page_size) - sizeof(chunk_header_t))

#define SMALL_CHUNK_ADJACENT(addr) ((chunk_header_t*)(((void*)addr) + sizeof(chunk_header_t) + ((chunk_header_t*)addr)->size))

#define CHUNK_PREV(addr)  (((chunk_header_t*)addr)->offset_prev == 0 \
                             ? NULL                                  \
                             : ((void*)addr) - (((chunk_header_t*)addr)->offset_prev))
    
#define CHUNK_NEXT(addr)  (((chunk_header_t*)addr)->offset_next == 0 \
                             ? NULL                                  \
                             : ((void*)addr) + (((chunk_header_t*)addr)->offset_next))
        
#define CHUNK_USER_MEM(addr) (((void*)addr) + sizeof(chunk_header_t))

#define CHUNK_FROM_USER_MEM(addr) ((chunk_header_t*)(((void*)addr) - sizeof(chunk_header_t)))

#define CHUNK_DISTANCE(a, b) (((void*)(a)) - (((void*)b)))

#define CHUNK_PARENT_BLOCK(addr) ((block_header_t*)(((void*)(addr)) - ((chunk_header_t*)addr)->offset_block))


typedef struct block_header {
    chunk_header_t      *free_list_head,
                        *free_list_tail;
    void                *end;
    struct block_header *next; 
} block_header_t;

#define BLOCK_FIRST_CHUNK(addr) (((void*)addr) + sizeof(block_header_t))

#define LARGEST_CHUNK_IN_EMPTY_N_PAGE_BLOCK(N) \
    (((system_info.page_size) * (N)) - sizeof(block_header_t) - sizeof(chunk_header_t))

typedef struct {
    u16              thread_idx;
    block_header_t  *blocks_head,
                    *blocks_tail;
    pthread_mutex_t  mtx;
} heap_t;

#define HEAP_LOCK(heap_ptr)   do { pthread_mutex_lock(&((heap_ptr)->mtx));   } while (0)
#define HEAP_UNLOCK(heap_ptr) do { pthread_mutex_unlock(&((heap_ptr)->mtx)); } while (0)

internal heap_t heap_make(void);
internal void * heap_alloc(heap_t *heap, u64 n_bytes);

#endif
