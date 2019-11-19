#include "internal.h"
#include "heap.h"
#include "os.h"
#include "profile.h"
#include "thread.h"

#include <unistd.h>
#include <string.h>
#include <pthread.h>

internal cblock_header_t * heap_new_cblock(heap_t *heap, u64 n_bytes) {
    u64              n_pages;
    u64              avail;
    block_header_t  *block;
    cblock_header_t *cblock;
    chunk_header_t  *chunk;

    ASSERT(IS_ALIGNED(DEFAULT_BLOCK_SIZE, system_info.page_size), "cblock size isn't aligned to page size");
    n_pages = DEFAULT_BLOCK_SIZE >> system_info.log_2_page_size;

    while ((avail = LARGEST_CHUNK_IN_EMPTY_N_PAGE_BLOCK(n_pages)) < n_bytes) {
        n_pages <<= 1;
    }

    ASSERT(n_pages > 0, "n_pages is zero");
    ASSERT(IS_ALIGNED(avail, 8), "cblock memory isn't aligned properly");

    block             = get_pages_from_os(n_pages, DEFAULT_BLOCK_SIZE);
    block->heap__meta = heap->__meta;
    block->tid        = get_this_tid();
    block->block_kind = BLOCK_KIND_CBLOCK;
    cblock            = &(block->c);
    cblock->end       = ((void*)cblock) + (n_pages << system_info.log_2_page_size);
    cblock->prev      = NULL;

    ASSERT((void*)cblock->end > (void*)cblock, "cblock->end wasn't set correctly");
    ASSERT(IS_ALIGNED(cblock, DEFAULT_BLOCK_SIZE), "cblock isn't aligned");

    /* Create the first and only chunk in the cblock. */
    chunk = CBLOCK_FIRST_CHUNK(cblock);

    chunk->__header = 0;

    chunk->flags |= CHUNK_IS_FREE;
    SET_CHUNK_SIZE(chunk, avail);

    cblock->free_list_head = cblock->free_list_tail = chunk;

    return cblock;
}

internal void heap_remove_cblock(heap_t *heap, cblock_header_t *cblock) {
    cblock_header_t *cblock_cursor;

    if (cblock == heap->cblocks_head && cblock == heap->cblocks_tail) {
        heap->cblocks_head = heap->cblocks_tail = NULL;
    } else if (cblock == heap->cblocks_tail) {
        heap->cblocks_tail = heap->cblocks_tail->prev;
    } else {
        cblock_cursor = heap->cblocks_tail;

        while (cblock_cursor->prev != cblock) {
            cblock_cursor = cblock_cursor->prev;
        }

        cblock_cursor->prev = cblock->prev;

        if (cblock == heap->cblocks_head) {
            heap->cblocks_head = cblock_cursor;
        }
    }
}

internal void release_cblock(cblock_header_t *cblock) {
    release_pages_to_os((void*)cblock, ((cblock->end - ((void*)cblock)) >> system_info.log_2_page_size));
}

internal void heap_add_cblock(heap_t *heap, cblock_header_t *cblock) {
    if (heap->cblocks_head == NULL) {
        ASSERT(heap->cblocks_tail == NULL, "cblock tail but no cblock head");
        heap->cblocks_head = heap->cblocks_tail = cblock;
    } else {
        cblock->prev       = heap->cblocks_tail;
        heap->cblocks_tail = cblock;
    }
}

#ifdef HMALLOC_USE_SBLOCKS

internal sblock_header_t * heap_new_sblock(heap_t *heap) {
    block_header_t  *block;
    sblock_header_t *sblock;
    u64              n_pages;

    ASSERT(IS_ALIGNED(DEFAULT_BLOCK_SIZE, system_info.page_size), "cblock size isn't aligned to page size");
    n_pages = DEFAULT_BLOCK_SIZE >> system_info.log_2_page_size;

    block                              = get_pages_from_os(n_pages, DEFAULT_BLOCK_SIZE);
    block->heap__meta                  = heap->__meta;
    block->tid                         = get_this_tid();
    block->block_kind                  = BLOCK_KIND_SBLOCK;
    sblock                             = &(block->s);
    sblock->bitfield_available_regions = ALL_REGIONS_AVAILABLE;
    sblock->end                        = ((void*)sblock) + (n_pages << system_info.log_2_page_size);
    sblock->prev                       = NULL;
    sblock->n_empty_regions            = 63;

    /*
     * If get_pages_from_os() is implemented in terms of mmap(), which
     * it is, then our memory is zeroed for us.
     * We are done as far as initialization is concerned.
     */

    return sblock;
}

internal void heap_remove_sblock(heap_t *heap, sblock_header_t *sblock) {
    sblock_header_t *sblock_cursor;

    if (sblock == heap->sblocks_head && sblock == heap->sblocks_tail) {
        heap->sblocks_head = heap->sblocks_tail = NULL;
    } else if (sblock == heap->sblocks_tail) {
        heap->sblocks_tail = heap->sblocks_tail->prev;
    } else {
        sblock_cursor = heap->sblocks_tail;

        while (sblock_cursor->prev != sblock) {
            sblock_cursor = sblock_cursor->prev;
        }

        sblock_cursor->prev = sblock->prev;

        if (sblock == heap->sblocks_head) {
            heap->sblocks_head = sblock_cursor;
        }
    }
}

internal void release_sblock(sblock_header_t *sblock) {
    if (doing_profiling) {
        profile_delete_block(sblock);
    }

    release_pages_to_os((void*)sblock, ((sblock->end - ((void*)sblock)) >> system_info.log_2_page_size));
}

internal void heap_add_sblock(heap_t *heap, sblock_header_t *sblock) {
    if (heap->sblocks_head == NULL) {
        ASSERT(heap->sblocks_tail == NULL, "sblock tail but no sblock head");
        heap->sblocks_head = heap->sblocks_tail = sblock;
    } else {
        sblock->prev       = heap->sblocks_tail;
        heap->sblocks_tail = sblock;
    }
}

#endif


internal void heap_make(heap_t *heap) {
    heap->cblocks_head = heap->cblocks_tail = NULL;
    heap->big_chunk_cblocks_tail            = NULL;

#ifdef HMALLOC_USE_SBLOCKS
    heap->sblocks_head = heap->sblocks_tail = NULL;
#endif

    heap->__meta.handle = NULL;
    heap->__meta.tid    = 0;
    heap->__meta.hid    = __sync_fetch_and_add(&hid_counter, 1);
    heap->__meta.flags  = 0;

    pthread_mutex_init(&heap->mtx, NULL);

    LOG("Created a new heap (hid = %d)\n", heap->__meta.hid);
}

internal void cblock_add_chunk_to_free_list(cblock_header_t *cblock, chunk_header_t *chunk) {
    chunk_header_t *free_list_cursor,
                   *next_free_chunk;
    u64             distance;

    ASSERT(((void*)chunk) < cblock->end, "chunk doesn't belong to this cblock");

    chunk->flags |= CHUNK_IS_FREE;
    SET_CHUNK_OFFSET_PREV(chunk, 0);
    SET_CHUNK_OFFSET_NEXT(chunk, 0);

    if (cblock->free_list_head == NULL) {
        /* This will be the only chunk on the free list. */
        ASSERT(cblock->free_list_tail == NULL, "tail, but no head");

        cblock->free_list_head = cblock->free_list_tail = chunk;

    } else if (chunk < cblock->free_list_head) {
        /* It should be the new head. */
        distance = CHUNK_DISTANCE(cblock->free_list_head, chunk);

        SET_CHUNK_OFFSET_PREV(cblock->free_list_head, distance);
        SET_CHUNK_OFFSET_NEXT(chunk, distance);

        cblock->free_list_head = chunk;

    } else if (cblock->free_list_tail < chunk) {
        /* It should be the new tail. */
        distance = CHUNK_DISTANCE(chunk, cblock->free_list_tail);

        SET_CHUNK_OFFSET_PREV(chunk, distance);
        SET_CHUNK_OFFSET_NEXT(cblock->free_list_tail, distance);

        cblock->free_list_tail = chunk;

    } else {
        /* We'll have to find the right place for it. */
        free_list_cursor = cblock->free_list_tail;

        /*
         * Catch the NULL case here because NULL < chunk, so we should exit
         * the loop.
         */
        while (chunk < free_list_cursor) {
            free_list_cursor = CHUNK_PREV(free_list_cursor);
        }

        ASSERT(free_list_cursor != NULL, "didn't find place in free list");

        next_free_chunk = CHUNK_NEXT(free_list_cursor);

        ASSERT(next_free_chunk != NULL, "didn't find next place in free list");
        ASSERT(next_free_chunk > chunk, "bad distance");

        /* Patch with previous chunk. */
        distance = CHUNK_DISTANCE(chunk, free_list_cursor);
        SET_CHUNK_OFFSET_NEXT(free_list_cursor, distance);
        SET_CHUNK_OFFSET_PREV(chunk, distance);

        /* Patch with next chunk. */
        distance = CHUNK_DISTANCE(next_free_chunk, chunk);
        SET_CHUNK_OFFSET_NEXT(chunk, distance);
        SET_CHUNK_OFFSET_PREV(next_free_chunk, distance);
    }
}

internal void cblock_remove_chunk_from_free_list(heap_t *heap, cblock_header_t *cblock, chunk_header_t *chunk) {
    chunk_header_t *prev_free_chunk,
                   *next_free_chunk;
    u64             distance;

    if (chunk == cblock->free_list_head && chunk == cblock->free_list_tail) {
        cblock->free_list_head = cblock->free_list_tail = NULL;

    } else if (chunk == cblock->free_list_head) {
        next_free_chunk = CHUNK_NEXT(chunk);

        ASSERT(next_free_chunk != NULL, "head is not only free chunk, but has no next");

        SET_CHUNK_OFFSET_PREV(next_free_chunk, 0);
        cblock->free_list_head        = next_free_chunk;

    } else if (chunk == cblock->free_list_tail) {
        prev_free_chunk = CHUNK_PREV(chunk);

        ASSERT(prev_free_chunk != NULL, "tail is not only free chunk, but has no prev");

        SET_CHUNK_OFFSET_NEXT(prev_free_chunk, 0);

        cblock->free_list_tail = prev_free_chunk;

    } else {
        prev_free_chunk = CHUNK_PREV(chunk);
        next_free_chunk = CHUNK_NEXT(chunk);

        ASSERT(prev_free_chunk != NULL, "free chunk missing prev");
        ASSERT(next_free_chunk != NULL, "free chunk missing next");
        ASSERT(next_free_chunk > prev_free_chunk, "bad distance");

        distance = CHUNK_DISTANCE(next_free_chunk, prev_free_chunk);
        SET_CHUNK_OFFSET_PREV(next_free_chunk, distance);
        SET_CHUNK_OFFSET_NEXT(prev_free_chunk, distance);
    }

    chunk->flags &= ~CHUNK_IS_FREE;
}

internal void cblock_split_chunk(cblock_header_t *cblock, chunk_header_t *chunk, u64 n_bytes) {
    chunk_header_t *new_chunk;

    ASSERT(cblock->free_list_head != NULL, "should have free chunk(s) to split");
    ASSERT(chunk->flags & CHUNK_IS_FREE, "can't split chunk that isn't free");
    ASSERT(IS_ALIGNED(CHUNK_USER_MEM(chunk), 8), "user mem is misaligned");
    ASSERT(IS_ALIGNED(n_bytes, 8), "size is misaligned");

    new_chunk           = CHUNK_USER_MEM(chunk) + n_bytes;
    new_chunk->__header = 0;

    SET_CHUNK_SIZE(new_chunk, ((void*)SMALL_CHUNK_ADJACENT(chunk)) - CHUNK_USER_MEM(new_chunk));
    SET_CHUNK_SIZE(chunk, n_bytes);

    ASSERT(CHUNK_SIZE(chunk)     >= 8, "chunk too small");
    ASSERT(CHUNK_SIZE(new_chunk) >= 8, "new_chunk too small");

    cblock_add_chunk_to_free_list(cblock, new_chunk);
}

internal chunk_header_t * heap_get_chunk_from_cblock_if_free(heap_t *heap,
                                                            cblock_header_t *cblock,
                                                            u64 n_bytes) {
    chunk_header_t *chunk;

    ASSERT(IS_ALIGNED(n_bytes, 8), "n_bytes not aligned");

    /* Scan until we find a chunk big enough. */
    chunk = cblock->free_list_tail;

    while (chunk != NULL) {
        if (CHUNK_SIZE(chunk) >= n_bytes) {
            break;
        }
        chunk = CHUNK_PREV(chunk);
    }

    /* Nothing viable in this cblock. */
    if (chunk == NULL)    { return NULL; }

    /* Can we split this into two chunks? */
    if ((CHUNK_SIZE(chunk) - n_bytes) >= (sizeof(chunk_header_t) + 8)) {
        cblock_split_chunk(cblock, chunk, n_bytes);
    }

    ASSERT(CHUNK_SIZE(chunk) >= n_bytes, "split chunk is no longer big enough");

    cblock_remove_chunk_from_free_list(heap, cblock, chunk);

    return chunk;
}

#ifdef HMALLOC_USE_SBLOCKS

internal void * region_get_free_slot(sblock_region_header_t *region) {
    int   first_available_slot;
    void *slot;

    ASSERT(region->bitfield_taken_slots != ALL_SLOTS_TAKEN, "region has no available slots");

    first_available_slot = __builtin_clzll(~(region->bitfield_taken_slots));
    slot                 = REGION_GET_SLOT(region, first_available_slot);

    /*
     * Set this slot's 'taken' bit.
     */
    region->bitfield_taken_slots |= (1ULL << (63ULL - first_available_slot));

    return slot;
}

internal void * sblock_get_slot_if_free(sblock_header_t *sblock) {
    int                     first_available_region;
    sblock_region_header_t *region;
    void                   *slot;
    u64                     save_region_bitfield;

    if (sblock->bitfield_available_regions == ALL_REGIONS_FULL) {
        return NULL;
    }

    first_available_region = __builtin_clzll(sblock->bitfield_available_regions);

    ASSERT(first_available_region > 0, "invalid region number");

    region = SBLOCK_GET_REGION(sblock, first_available_region);
    ASSERT(IS_ALIGNED(region, 64ULL*SBLOCK_SLOT_SIZE), "region is misaligned");

    save_region_bitfield = region->bitfield_taken_slots;

    slot = region_get_free_slot(region);

    /*
     * If the region was previously completely empty, then
     * we should indicate that it is no longer an empty
     * region in the sblock.
     */
    if (save_region_bitfield == ALL_SLOTS_AVAILABLE) {
        sblock->n_empty_regions -= 1;
    }

    if (region->bitfield_taken_slots == ALL_SLOTS_TAKEN) {
        /*
         * Clear the bit to indicate that this region is full.
         */
        sblock->bitfield_available_regions &= ~(1ULL << (63ULL - first_available_region));
    }

    return slot;
}

internal void * heap_alloc_from_sblocks(heap_t *heap, u64 n_bytes) {
    sblock_header_t *sblock;
    void            *mem;

    ASSERT(IS_ALIGNED(n_bytes, 8), "n_bytes is not aligned");
    ASSERT(n_bytes <= SBLOCK_MAX_ALLOC_SIZE, "requesting too many bytes from sblock");

    mem    = NULL;
    sblock = heap->sblocks_tail;

    while (sblock != NULL) {
        mem = sblock_get_slot_if_free(sblock);

        if (mem != NULL)    { break; }

        sblock = sblock->prev;
    }

    if (mem == NULL) {
        sblock = heap_new_sblock(heap);
        heap_add_sblock(heap, sblock);
        mem = sblock_get_slot_if_free(sblock);
    }

    ASSERT(mem != NULL, "did not get slot from sblock");
    ASSERT(IS_ALIGNED(mem, 8), "mem is not aligned");

    return mem;
}

#endif

internal void heap_free_big_chunk(heap_t *heap, chunk_header_t *big_chunk) {
    cblock_header_t *cblock;

    big_chunk->flags |= CHUNK_IS_FREE;

    cblock = &((block_header_t*)ADDR_PARENT_BLOCK(big_chunk))->c;

    cblock->prev                 = heap->big_chunk_cblocks_tail;
    heap->big_chunk_cblocks_tail = cblock;

    if (doing_profiling) {
        profile_delete_block(cblock);
    }
}

internal chunk_header_t * heap_get_big_chunk(heap_t *heap, u64 n_bytes) {
    block_header_t  *block;
    cblock_header_t *cblock,
                    *next_cblock;
    chunk_header_t  *chunk;
    u64              cblock_avail, cblock_n_pages;

    cblock      = heap->big_chunk_cblocks_tail;
    next_cblock = NULL;
    chunk       = NULL;

    while (cblock != NULL) {
        cblock_n_pages = (cblock->end - (void*)ADDR_PARENT_BLOCK(cblock)) >> system_info.log_2_page_size;
        cblock_avail   = LARGEST_CHUNK_IN_EMPTY_N_PAGE_BLOCK(cblock_n_pages);

        if (cblock_avail >= n_bytes) {
            /*
             * Found a big enough cblock.
             * Remove it from the list.
             */

            chunk = CBLOCK_FIRST_CHUNK(cblock);

            if (next_cblock == NULL) {
                heap->big_chunk_cblocks_tail = cblock->prev;
            } else {
                next_cblock->prev = cblock->prev;
            }
            break;
        }

        next_cblock = cblock;
        cblock      = cblock->prev;
    }

    if (cblock == NULL) {
        cblock = heap_new_cblock(heap, n_bytes);
        chunk  = CBLOCK_FIRST_CHUNK(cblock);
    }

    block      = ADDR_PARENT_BLOCK(cblock);
    block->tid = get_this_tid();

    if (doing_profiling) {
        profile_add_block(cblock, n_bytes);
    }

    chunk->flags &= ~(CHUNK_IS_FREE);
    chunk->flags |=   CHUNK_IS_BIG;

    return chunk;
}

internal void * heap_big_alloc(heap_t *heap, u64 n_bytes) {
    chunk_header_t *chunk;
    void           *mem;

    chunk = heap_get_big_chunk(heap, n_bytes);

    ASSERT(chunk != NULL, "did not get big chunk");

    mem = CHUNK_USER_MEM(chunk);

    ASSERT(IS_ALIGNED(mem, 8), "user memory is not properly aligned for performance");

    return mem;
}

internal void * heap_alloc(heap_t *heap, u64 n_bytes) {
    cblock_header_t *cblock;
    chunk_header_t *chunk;
    void           *mem;

    if (n_bytes == 0) {
        return  NULL;
    }

    chunk = NULL;

    /*
     * Round n_bytes to the nearest multiple of 8 so that
     * we get the best alignment.
     */
    n_bytes = ALIGN(n_bytes, 8);

    if (n_bytes > MAX_SMALL_CHUNK) {
        return heap_big_alloc(heap, n_bytes);
    }

#ifdef HMALLOC_USE_SBLOCKS
    if (n_bytes <= SBLOCK_MAX_ALLOC_SIZE) {
        return heap_alloc_from_sblocks(heap, n_bytes);
    }
    /*
     * We are allocating something a little bigger.
     * So, we'll use the regular cblocks.
     */
#endif

    /* @eden */
    ASSERT(0, "shouldn't get here -- eden testing");

    cblock = heap->cblocks_tail;

    while (cblock != NULL) {
        chunk = heap_get_chunk_from_cblock_if_free(heap, cblock, n_bytes);

        if (chunk != NULL)    { break; }

        cblock = cblock->prev;
    }

    if (chunk == NULL) {
        /*
         * We've gone through all of the cblocks and haven't found a
         * big enough chunk.
         * So, we'll have to add a new cblock.
         */
        cblock = heap_new_cblock(heap, n_bytes);
        heap_add_cblock(heap, cblock);
        chunk = heap_get_chunk_from_cblock_if_free(heap, cblock, n_bytes);
    }

    ASSERT(chunk != NULL, "invalid chunk -- could not allocate memory");

    mem = CHUNK_USER_MEM(chunk);

    ASSERT(IS_ALIGNED(mem, 8), "user memory is not properly aligned for performance");

    return mem;
}

internal chunk_header_t * coalesce_free_chunk_back(cblock_header_t *cblock, chunk_header_t *chunk) {
    chunk_header_t *prev_free_chunk,
                   *next_free_chunk,
                   *new_chunk;
    u64             distance;

    prev_free_chunk = CHUNK_PREV(chunk);
    next_free_chunk = CHUNK_NEXT(chunk);
    new_chunk       = chunk;

    if (prev_free_chunk != NULL
    &&  SMALL_CHUNK_ADJACENT(prev_free_chunk) == chunk) {

        ASSERT(prev_free_chunk->flags & CHUNK_IS_FREE, "can't coalesce a chunk that isn't free");

        SET_CHUNK_SIZE(prev_free_chunk,
            CHUNK_SIZE(prev_free_chunk) + sizeof(chunk_header_t) + CHUNK_SIZE(chunk));

        if (next_free_chunk != NULL) {
            ASSERT(next_free_chunk->flags & CHUNK_IS_FREE, "next chunk in free list isn't free");
            ASSERT(next_free_chunk > prev_free_chunk, "bad distance");
            distance = CHUNK_DISTANCE(next_free_chunk, prev_free_chunk);
            SET_CHUNK_OFFSET_NEXT(prev_free_chunk, distance);
            SET_CHUNK_OFFSET_PREV(next_free_chunk, distance);
        } else {
            ASSERT(chunk == cblock->free_list_tail, "chunk has no next, but isn't tail");
            SET_CHUNK_OFFSET_NEXT(prev_free_chunk, 0);
            cblock->free_list_tail = prev_free_chunk;
        }

        new_chunk = prev_free_chunk;
    }

    return new_chunk;
}

internal void coalesce_free_chunk(cblock_header_t *cblock, chunk_header_t *chunk) {
    chunk_header_t *new_chunk,
                   *next_free_chunk;

    ASSERT(chunk->flags & CHUNK_IS_FREE, "can't coalesce a chunk that isn't free");

    new_chunk       = coalesce_free_chunk_back(cblock, chunk);
    next_free_chunk = CHUNK_NEXT(new_chunk);

    if (next_free_chunk != NULL
    &&  next_free_chunk == SMALL_CHUNK_ADJACENT(new_chunk)) {

        coalesce_free_chunk_back(cblock, next_free_chunk);
    }
}

internal void heap_free_from_cblock(heap_t *heap, cblock_header_t *cblock, chunk_header_t *chunk) {
    chunk_header_t *cblock_first_chunk;

    ASSERT(!(chunk->flags & CHUNK_IS_FREE), "double free error");

    cblock_add_chunk_to_free_list(cblock, chunk);

    coalesce_free_chunk(cblock, chunk);

    /* If the cblock only has one free chunk... */
    if (cblock->free_list_head == cblock->free_list_tail) {
        cblock_first_chunk = CBLOCK_FIRST_CHUNK(cblock);
        /* and if that chunk spans the whole cblock -- release it. */
        if ((((void*)cblock_first_chunk) + sizeof(chunk_header_t) +  CHUNK_SIZE(cblock_first_chunk)) == cblock->end) {
            /* If this cblock isn't the only cblock in the heap... */
            if (cblock != heap->cblocks_head || cblock != heap->cblocks_tail) {
                heap_remove_cblock(heap, cblock);
                release_cblock(cblock);
            }
        }
    }
}

#ifdef HMALLOC_USE_SBLOCKS

internal void heap_free_from_sblock(heap_t *heap, sblock_header_t *sblock, void *slot) {
    sblock_region_header_t *region;
    void                   *truncated_addr;
    u64                     distance,
                            slot_number,
                            region_number;
    int                     is_taken;

    truncated_addr = (void*)(((u64)slot) & ~(SBLOCK_SLOT_SIZE - 1ULL));
    region         = (void*)(((u64)slot) & ~((64ULL * SBLOCK_SLOT_SIZE) - 1ULL));
    distance       = truncated_addr - (void*)region;

    ASSERT(IS_ALIGNED(distance, SBLOCK_SLOT_SIZE), "incorrect slot distance");

    slot_number = distance >> LOG2_64BIT(SBLOCK_SLOT_SIZE);

    is_taken = !!(region->bitfield_taken_slots & (1ULL << (63ULL - slot_number)));

    ASSERT(is_taken, "attempting to free a sblock slot that isn't taken");

    (void)is_taken;

    region->bitfield_taken_slots &= ~(1ULL << (63ULL - slot_number));

    /*
     * If this region has just become completely empty, then
     * we should indicate that to the sblock.
     */
    if (region->bitfield_taken_slots == ALL_SLOTS_AVAILABLE) {
        sblock->n_empty_regions += 1;
    }

    distance      = (void*)region - (void*)sblock;
    region_number = (distance >> LOG2_64BIT(SBLOCK_SLOT_SIZE * 64ULL));

    ASSERT(region_number > 0, "incorrect region number");

    sblock->bitfield_available_regions |= (1ULL << (63ULL - region_number));

    /*
     * If all regions are empty (i.e., the sblock contains no allocations),
     * then we can consider releasing the sblock.
     */
    if (sblock->n_empty_regions == 63) {
        /* If this sblock isn't the only sblock in the heap... */
        if (sblock != heap->sblocks_head || sblock != heap->sblocks_tail) {
            heap_remove_sblock(heap, sblock);
            release_sblock(sblock);
        }
    }
}

#endif


internal void heap_free(heap_t *heap, void *addr) {
    block_header_t  *block;
    cblock_header_t *cblock;
    chunk_header_t  *chunk;

    block = ADDR_PARENT_BLOCK(addr);

#ifdef HMALLOC_USE_SBLOCKS
    if (block->block_kind == BLOCK_KIND_SBLOCK) {
        heap_free_from_sblock(heap, &(block->s), addr);
    } else
#endif
    {
        chunk = CHUNK_FROM_USER_MEM(addr);
        ASSERT(block->block_kind == BLOCK_KIND_CBLOCK, "block isn't cblock");
        cblock = &(block->c);

        if (chunk->flags & CHUNK_IS_BIG) {
            heap_free_big_chunk(heap, chunk);
        } else {
            heap_free_from_cblock(heap, cblock, chunk);
        }
    }
}

internal void * heap_aligned_alloc(heap_t *heap, size_t n_bytes, size_t alignment) {
    cblock_header_t *cblock;
    sblock_header_t *sblock;
    block_header_t  *block;
    chunk_header_t  *first_chunk,
                    *first_chunk_check,
                    *chunk;
    void            *mem, *sblock_not_aligned,
                    *aligned_addr;
    u64              new_cblock_size_request,
                     first_chunk_size;

    ASSERT(alignment > 0, "invalid alignment -- must be > 0");
    ASSERT(IS_POWER_OF_TWO(alignment), "invalid alignment -- must be a power of two");

    if (unlikely(n_bytes == 0))    { return NULL; }

    /*
     * All sblock slots *EXCEPT FOR THE FIRST ONE* are aligned on
     * SBLOCK_SLOT_SIZE boundaries.
     * If size will fit in a slot, we'll try to get one.
     * On the off chance that the one we get is the first one, we'll
     * hold on to it and try again.
     * We won't be able to keep doing this unless we store all of the
     * slots we've tried and free them all afterwards.
     * So, we'll just give up after the second try and continue to the
     * primary code path.
     */
    if (n_bytes <= SBLOCK_MAX_ALLOC_SIZE && alignment <= SBLOCK_SLOT_SIZE){
        mem = heap_alloc_from_sblocks(heap, n_bytes);
        if (!IS_ALIGNED(mem, SBLOCK_SLOT_SIZE)) {
            sblock_not_aligned = mem;
            sblock = &((block_header_t*)ADDR_PARENT_BLOCK(sblock_not_aligned))->s;
            mem = heap_alloc_from_sblocks(heap, n_bytes);
            heap_free_from_sblock(heap, sblock, sblock_not_aligned);
        }
        if (IS_ALIGNED(mem, SBLOCK_SLOT_SIZE)) {
            ASSERT(IS_ALIGNED(mem, alignment), "failed to align from slot allocation");
            return mem;
        } else {
            mem = NULL;
        }
    }

    /*
     * All allocations are guaranteed to be aligned on 8 byte
     * boundaries, so just do normal allocation if we can.
     */
    if (alignment <= 8) {
        return heap_alloc(heap, n_bytes);
    }

    /*
     * If size is big:
     * Use the regular big chunk procedure to get memory, but
     * force the big chunk to have its chunk_header_t in the right place.
     * free() won't work correctly if we don't do this.
     */
    if (n_bytes + alignment > MAX_SMALL_CHUNK) {
        chunk = heap_get_big_chunk(heap, n_bytes + alignment);
        mem   = ALIGN(CHUNK_USER_MEM(chunk), alignment);
        memcpy(mem - sizeof(chunk_header_t), chunk, sizeof(chunk_header_t));
        return mem;
    }



    new_cblock_size_request =   n_bytes                /* The bytes we need to give the user. */
                             + alignment               /* Make sure there's space to align. */
                             + sizeof(chunk_header_t); /* We're going to put another chunk in there. */

    cblock = heap_new_cblock(heap, new_cblock_size_request);
    block  = (block_header_t*)cblock;
    heap_add_cblock(heap, cblock);

    first_chunk_check = ((void*)block) + sizeof(block_header_t);

    if (IS_ALIGNED(CHUNK_USER_MEM(first_chunk_check), alignment)) {
        aligned_addr = CHUNK_USER_MEM(first_chunk_check);

        chunk = heap_get_chunk_from_cblock_if_free(heap, cblock, n_bytes);

        ASSERT(first_chunk_check == chunk, "first chunk mismatch");
    } else {
        aligned_addr = ALIGN(CHUNK_USER_MEM(first_chunk_check), alignment);

        first_chunk_size =   (aligned_addr - sizeof(chunk_header_t))
                           - (((void*)block) + sizeof(block_header_t) + sizeof(chunk_header_t));
        first_chunk      = heap_get_chunk_from_cblock_if_free(heap, cblock, first_chunk_size);
        chunk            = heap_get_chunk_from_cblock_if_free(heap, cblock, n_bytes);

        ASSERT(first_chunk != NULL, "did not get first chunk");
        ASSERT(first_chunk_check == first_chunk, "first chunk mismatch");

        heap_free(heap, CHUNK_USER_MEM(first_chunk));
    }


    ASSERT(chunk != NULL, "did not get aligned chunk");
    ASSERT(aligned_addr < cblock->end, "aligned address is outside of cblock");

    mem = CHUNK_USER_MEM(chunk);

    ASSERT(mem == aligned_addr, "memory acquired from chunk is not the expected aligned address");

    return mem;
}

internal i32 heap_handle_equ(heap_handle_t a, heap_handle_t b) { return strcmp(a, b) == 0; }
internal u64 heap_handle_hash(heap_handle_t h) {
    unsigned long hash = 5381;
    int           c;

    while ((c = *h++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

internal void user_heaps_init(void) {
    USER_HEAPS_LOCK(); {
        user_heaps = hash_table_make_e(heap_handle_t, heap_t, heap_handle_hash, heap_handle_equ);
    } USER_HEAPS_UNLOCK();
    LOG("initialized user heaps table\n");
}

internal char * istrdup(char *s) {
    int   len;
    char *out;

    len = strlen(s);
    out = imalloc(len + 1);
    memcpy(out, s, len + 1);

    return out;
}

internal heap_t * get_or_make_user_heap(char *handle) {
    heap_t *heap,
            new_heap;
    char   *cpy;

    USER_HEAPS_LOCK(); {
        heap = hash_table_get_val(user_heaps, handle);
        if (!heap) {
            cpy = istrdup(handle);

            heap_make(&new_heap);
            new_heap.__meta.handle = cpy;
            new_heap.__meta.flags |= HEAP_USER;

            hash_table_insert(user_heaps, cpy, new_heap);
            heap = hash_table_get_val(user_heaps, handle);

            ASSERT(heap, "error creating new user heap");

            LOG("hid %d is a user heap created by tid %d (handle = '%s')\n", heap->__meta.hid, get_this_tid(), heap->__meta.handle);
        }
    } USER_HEAPS_UNLOCK();

    return heap;
}
