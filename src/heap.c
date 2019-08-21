#include "internal.h"
#include "heap.h"
#include "os.h"

#include <unistd.h>
#include <string.h>
#include <pthread.h>

internal void zero_chunk_header(chunk_header_t *chunk) {
    memset(chunk, 0, sizeof(*chunk));
}

internal block_header_t * heap_new_block(heap_t *heap, u64 n_bytes) {
    u32             n_pages;
    u64             avail;
    block_header_t *block;
    chunk_header_t *chunk;

    n_pages = DEFAULT_BLOCK_PAGES;

    while ((avail = LARGEST_CHUNK_IN_EMPTY_N_PAGE_BLOCK(n_pages)) < n_bytes) {
        n_pages += 1;
    }

    block       = get_pages_from_os(n_pages);
    block->end  = ((void*)block) + (n_pages * system_info.page_size); 
    block->next = NULL;

    /* Create the first and only chunk in the block. */
    chunk = BLOCK_FIRST_CHUNK(block);

    zero_chunk_header(chunk);

    chunk->flags     |= CHUNK_IS_FREE;
    chunk->size       = avail;
    chunk->thread_idx = heap->thread_idx;

    block->free_list_head = block->free_list_tail = chunk;

    return block;
}

internal void heap_add_block(heap_t *heap, block_header_t *block) {
    if (heap->blocks_head == NULL) {
        ASSERT(heap->blocks_tail == NULL, "block tail but no block head");
        heap->blocks_head = heap->blocks_tail = block;
    } else {
        heap->blocks_tail->next = block;
        heap->blocks_tail       = block;
    }
}

internal heap_t heap_make(void) {
    heap_t heap;

    heap.blocks_head = heap.blocks_tail = NULL;
    pthread_mutex_init(&heap.mtx, NULL);

    LOG("Created a new heap\n");
    return heap;
}

internal void heap_split_chunk(chunk_header_t *chunk, u64 n_bytes) {
    chunk_header_t *adjacent_chunk,
                   *new_chunk;
    u64             distance;
    int             last_in_block;

    last_in_block = chunk->offset_next == 0;

    /*
     * DANGER!!!
     * If last_in_block is true, we should not be touching adjacent_chunk.
     */
    adjacent_chunk = SMALL_CHUNK_ADJACENT(chunk);
    new_chunk      = CHUNK_USER_MEM(chunk) + n_bytes;
    
    distance = CHUNK_DISTANCE(new_chunk, chunk);

    chunk->size        = n_bytes;
    chunk->offset_next = distance;

    new_chunk->offset_prev = distance;
    new_chunk->size        = ((void*)adjacent_chunk) - CHUNK_USER_MEM(new_chunk);

    if (last_in_block) {
        new_chunk->offset_next = 0;
    } else {
        distance                    = CHUNK_DISTANCE(adjacent_chunk, new_chunk);
        new_chunk->offset_next      = distance;
        adjacent_chunk->offset_prev = distance;
    }

    new_chunk->flags |= CHUNK_IS_FREE;
}

internal chunk_header_t * heap_get_chunk_from_block_if_free(heap_t *heap, block_header_t *block, u64 n_bytes) {
    chunk_header_t *chunk,
                   *prev_free_chunk,
                   *next_free_chunk;
    u64             distance;
    int             only_free_chunk;

    /* Scan until we find a chunk big enough. */
    chunk = block->free_list_head;

    while (chunk != NULL) {
        if (chunk->size >= n_bytes) {
            break;
        }
        chunk = CHUNK_NEXT(chunk);
    }

    if (chunk != NULL) {
        /* Can we split this into two chunks? */
        if ((chunk->size - n_bytes) > sizeof(chunk_header_t)) {
            heap_split_chunk(chunk, n_bytes);
        }

        only_free_chunk = (   block->free_list_head == block->free_list_tail
                           && block->free_list_tail == chunk);

        ASSERT(chunk->size >= n_bytes, "split chunk is no longer big enough");

        /* Remove from free list and patch up. */
        if (only_free_chunk) {
            block->free_list_head = block->free_list_tail = NULL;
        } else {
            prev_free_chunk = CHUNK_PREV(chunk);
            next_free_chunk = CHUNK_NEXT(chunk);

            if (chunk == block->free_list_head) {
                block->free_list_head = next_free_chunk;
            } else if (chunk == block->free_list_tail) {
                ASSERT(next_free_chunk == NULL, "chunk is tail, but has next");
                ASSERT(prev_free_chunk != NULL, "tail is not head, but has no prev");
                block->free_list_tail = prev_free_chunk;
            }

            if (prev_free_chunk != NULL && next_free_chunk != NULL) {
                distance = CHUNK_DISTANCE(next_free_chunk, prev_free_chunk);
                prev_free_chunk->offset_next = distance;
                next_free_chunk->offset_prev = distance;
            } else if (prev_free_chunk != NULL) {
                prev_free_chunk->offset_next = 0;
            } else if (next_free_chunk != NULL) {
                next_free_chunk->offset_prev = 0;
            }
        }

        chunk->offset_block = CHUNK_DISTANCE(chunk, block);
        chunk->flags       &= ~CHUNK_IS_FREE;
    }

    return chunk;
}

internal void * heap_alloc(heap_t *heap, u64 n_bytes) {
    block_header_t *block;
    chunk_header_t *chunk;
    void           *mem;

    chunk = NULL;

    /* 
     * Round n_bytes to the nearest multiple of 8 so that
     * we get the best alignment.
     */
    n_bytes = ALIGN(n_bytes, 8);

    /*
     * @incomplete
     *
     * We need to handle the case were n_bytes won't fit in the 32-bit
     * size field of the chunk_header_t.
     * We'll just have a separate list of "BIG" chunkations for each 
     * thread and handle them as a special case.
     *
     */ ASSERT(n_bytes < MAX_SMALL_CHUNK, "see above @incomplete");

    if (heap->blocks_head == NULL) {
        ASSERT(heap->blocks_tail == NULL, "block tail but no block head");
        block = heap_new_block(heap, n_bytes);
        heap_add_block(heap, block);
    }
        
    ASSERT(heap->blocks_head != NULL, "no blocks");

    block = heap->blocks_head;

    while (block != NULL) {
        chunk = heap_get_chunk_from_block_if_free(heap, block, n_bytes);

        if (chunk != NULL)    { break; }

        block = block->next;
    }

    if (chunk == NULL) {
        /*
         * We've gone through all of the blocks and haven't found a 
         * big enough chunk.
         * So, we'll have to add a new block.
         */
        block = heap_new_block(heap, n_bytes);
        heap_add_block(heap, block);
        chunk = heap_get_chunk_from_block_if_free(heap, block, n_bytes);
    }

    ASSERT(chunk != NULL, "invalid chunk -- could not allocate memory");

    mem = CHUNK_USER_MEM(chunk);

    return mem;
}

internal chunk_header_t * coalesce_free_chunk_back(block_header_t *block, chunk_header_t *chunk) {
    chunk_header_t *prev_free_chunk,
                   *next_free_chunk,
                   *new_chunk;

    prev_free_chunk = CHUNK_PREV(chunk);
    next_free_chunk = CHUNK_NEXT(chunk);
    new_chunk       = chunk;

    if (prev_free_chunk != NULL
    &&  SMALL_CHUNK_ADJACENT(prev_free_chunk) == chunk) {

        prev_free_chunk->size += sizeof(chunk_header_t) + chunk->size;

        if (next_free_chunk != NULL) {
            prev_free_chunk->offset_next = CHUNK_DISTANCE(next_free_chunk, prev_free_chunk);
        } else {
            prev_free_chunk->offset_next = 0;
        }

        new_chunk = prev_free_chunk;
    }

    return new_chunk;
}

internal void coalesce_free_chunk(block_header_t *block, chunk_header_t *chunk) {
    chunk_header_t *new_chunk,
                   *next_free_chunk;

    new_chunk       = coalesce_free_chunk_back(block, chunk);
    next_free_chunk = CHUNK_NEXT(new_chunk);

    if (next_free_chunk != NULL
    &&  next_free_chunk == SMALL_CHUNK_ADJACENT(new_chunk)) {

        coalesce_free_chunk_back(block, next_free_chunk);
    }
}

internal void heap_free_l(heap_t *locked_heap, chunk_header_t *chunk) {
    block_header_t *block;
    chunk_header_t *free_list_cursor,
                   *prev_free_chunk,
                   *next_free_chunk;
    u64             distance;

    ASSERT(!(chunk->flags & CHUNK_IS_FREE), "double free error");

    block            = CHUNK_PARENT_BLOCK(chunk);
    free_list_cursor = block->free_list_head;
        
    ASSERT(((void*)chunk) < block->end, "chunk doesn't belong to this block");

    chunk->offset_prev = chunk->offset_next = 0;

    if (free_list_cursor == NULL) {
        block->free_list_head = chunk;
    } else {
        while (free_list_cursor != NULL && free_list_cursor < chunk) {
            free_list_cursor = CHUNK_NEXT(free_list_cursor);  
        }
       
        ASSERT(free_list_cursor != chunk, "can't free chunk that's in the free list");

        if (free_list_cursor == NULL) {
            /* This chunk is the last one. */
            prev_free_chunk = block->free_list_tail;
            next_free_chunk = NULL; /* No next chunk. */
        } else {
            prev_free_chunk = CHUNK_PREV(free_list_cursor);
            next_free_chunk = free_list_cursor;
        }

        if (prev_free_chunk == NULL && next_free_chunk == NULL) {
            chunk->offset_prev = chunk->offset_next = 0;
        } else {
            if (prev_free_chunk != NULL) {
                distance                     = CHUNK_DISTANCE(chunk, prev_free_chunk);
                prev_free_chunk->offset_next = distance;
                chunk->offset_prev           = distance;
            } else if (next_free_chunk != NULL) {
                distance                     = CHUNK_DISTANCE(next_free_chunk, chunk);
                next_free_chunk->offset_prev = distance;
                chunk->offset_next           = distance;
            }
        }

        if (block->free_list_head != NULL) {
            if (chunk < block->free_list_head) {
                block->free_list_head = chunk;
            }
            if (chunk > block->free_list_tail) {
                block->free_list_tail = chunk;
            }

            ASSERT(block->free_list_tail, "head, but no tail");
        } else {
            block->free_list_head = block->free_list_tail = chunk;
        }
    }

    chunk->flags |= CHUNK_IS_FREE;

    coalesce_free_chunk(block, chunk);
}

internal void heap_free(heap_t *heap, chunk_header_t *chunk) {
    HEAP_LOCK(heap); {
        heap_free_l(heap, chunk);
    } HEAP_UNLOCK(heap);
}

internal void * heap_aligned_alloc(heap_t *heap, size_t n_bytes, size_t alignment) {
    block_header_t *block;
    chunk_header_t *first_chunk,
                   *first_chunk_check,
                   *chunk;
    void           *mem,
                   *aligned_addr;
    u64             new_block_size_request,
                    first_chunk_size;

    ASSERT(alignment > 0, "invalid alignment -- must be > 0");
    ASSERT(IS_POWER_OF_TWO(alignment), "invalid alignment -- must be a power of two");

    if (n_bytes == 0)    { return NULL; }

    new_block_size_request =   n_bytes                 /* The bytes we need to give the user. */
                             + alignment               /* Make sure there's space to align. */
                             + sizeof(chunk_header_t); /* We're going to put another chunk in there. */
    
    HEAP_LOCK(heap); {
        block             = heap_new_block(heap, new_block_size_request);
        first_chunk_check = ((void*)block) + sizeof(block_header_t);
        aligned_addr      = ALIGN(CHUNK_USER_MEM(first_chunk_check), alignment);
        first_chunk_size  =   (aligned_addr - sizeof(chunk_header_t))
                           - (((void*)block) + sizeof(block_header_t) + sizeof(chunk_header_t));
        first_chunk      = heap_get_chunk_from_block_if_free(heap, block, first_chunk_size);
        chunk            = heap_get_chunk_from_block_if_free(heap, block, n_bytes);
        mem              = CHUNK_USER_MEM(chunk);
        
        ASSERT(first_chunk_check == first_chunk, "first chunk mismatch");
        ASSERT(aligned_addr < block->end, "aligned address is outside of block");
        ASSERT(mem == aligned_addr, "memory acquired from chunk is not the expected aligned address");

        heap_free_l(heap, first_chunk);
    } HEAP_UNLOCK(heap);

    mem = aligned_addr;

    return mem;
}
