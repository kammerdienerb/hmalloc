#include "internal.h"
#include "heap.h"
#include "os.h"
#include "thread.h"

#include <unistd.h>
#include <string.h>
#include <pthread.h>

HMALLOC_ALWAYS_INLINE
internal inline void cblock_list_make(cblock_list_t *list) {
    list->head = list->tail = NULL;
    LIST_LOCK_INIT(list);
}

HMALLOC_ALWAYS_INLINE
internal inline cblock_header_t * heap_new_aligned_cblock(heap_t *heap, u64 n_bytes, u64 alignment) {
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

    block                    = get_pages_from_os(n_pages, alignment);
    block->heap__meta        = heap->__meta;
    block->tid               = get_this_tid();
    block->block_kind        = BLOCK_KIND_CBLOCK;
    cblock                   = &(block->c);
    cblock->end              = ((void*)cblock) + (n_pages << system_info.log_2_page_size);
    cblock->prev             = NULL;
    cblock->next             = NULL;

    ASSERT((void*)cblock->end > (void*)cblock, "cblock->end wasn't set correctly");
    ASSERT(IS_ALIGNED(cblock, DEFAULT_BLOCK_SIZE), "cblock isn't aligned");

    /* Create the first and only chunk in the cblock. */
    chunk = CBLOCK_FIRST_CHUNK(cblock);

    chunk->__header = 0;

    chunk->flags |= CHUNK_IS_FREE;
#ifdef HMALLOC_DO_ASSERTIONS
    SET_CHUNK_MAGIC(chunk);
#endif
    SET_CHUNK_SIZE(chunk, avail);

    cblock->free_list_head   =
    cblock->free_list_tail   =
        chunk;

    return cblock;
}

HMALLOC_ALWAYS_INLINE
internal inline cblock_header_t * heap_new_cblock(heap_t *heap, u64 n_bytes) {
    return heap_new_aligned_cblock(heap, n_bytes, DEFAULT_BLOCK_SIZE);
}

HMALLOC_ALWAYS_INLINE
internal inline void cblock_list_remove_cblock(cblock_list_t *list, cblock_header_t *cblock) {
    if (cblock == list->head) {
        list->head = cblock->next;
    }
    if (cblock == list->tail) {
        list->tail = cblock->prev;
    }
    if (cblock->prev) {
        cblock->prev->next = cblock->next;
    }
    if (cblock->next) {
        cblock->next->prev = cblock->prev;
    }
}

HMALLOC_ALWAYS_INLINE
internal inline void release_cblock(cblock_header_t *cblock) {
    release_pages_to_os((void*)cblock, ((cblock->end - ((void*)cblock)) >> system_info.log_2_page_size));
}

HMALLOC_ALWAYS_INLINE
internal inline void cblock_list_add_cblock(cblock_list_t *list, cblock_header_t *cblock) {
    ASSERT(cblock->end - (void*)cblock == DEFAULT_BLOCK_SIZE,
           "cblock has incorrect size for this list");

    if (list->head == NULL) {
        ASSERT(list->tail == NULL, "cblock tail but no cblock head");
        list->head = list->tail = cblock;
    } else {
        list->tail->next = cblock;
        cblock->prev     = list->tail;
        list->tail       = cblock;
    }

    cblock->list = list;
}



#ifdef HMALLOC_USE_SBLOCKS

internal
sblock_header_t * heap_new_sblock(heap_t *heap, u32 size_class, u32 size_class_idx) {
    block_header_t  *block;
    sblock_header_t *sblock;
    u64              block_size;
    u64              n_pages;
    u32              i;
    u32              r;
    u32              s;

    block_size = _sblock_block_size_lookup[size_class_idx];

    ASSERT(IS_ALIGNED(block_size, system_info.page_size), "cblock size isn't aligned to page size");

    n_pages = block_size >> system_info.log_2_page_size;

    block             = get_pages_from_os(n_pages, DEFAULT_BLOCK_SIZE);
    block->heap__meta = heap->__meta;
    block->tid        = get_this_tid();
    block->block_kind = BLOCK_KIND_SBLOCK;

    sblock = &(block->s);

    sblock->prev             = NULL;
    sblock->next             = NULL;
    sblock->end              = ((void*)sblock) + (n_pages << system_info.log_2_page_size);
    sblock->n_allocations    = 0;
    sblock->max_allocations  = 4096ULL - _sblock_reserved_slots_lookup[size_class_idx];
    sblock->size_class_idx   = size_class_idx;
    sblock->size_class       = size_class;

    sblock->regions_bitfield = 0xFFFFFFFFFFFFFFFF;

    memset(&sblock->slots_bitfields, ~0, sizeof(sblock->slots_bitfields));
    for (i = 0; i < _sblock_reserved_slots_lookup[size_class_idx]; i += 1) {
        r = i >> 6;
        s = i & 63ULL;
        sblock->slots_bitfields[r] &= ~(1ULL << (63ULL - s));
        if (s == 63) {
            sblock->regions_bitfield &= ~(1ULL << (63ULL - r));
        }
    }

    return sblock;
}

internal
void heap_remove_sblock(heap_t *heap, sblock_header_t *sblock) {
    u32 size_class_idx;

    size_class_idx = sblock->size_class_idx;


    ASSERT(heap->n_sblocks[size_class_idx] > 0,
           "incorrect number of sblocks in heap");
    heap->n_sblocks[size_class_idx] -= 1;

    if (sblock == heap->sblocks_heads[size_class_idx]) {
        heap->sblocks_heads[size_class_idx] = sblock->next;
    }
    if (sblock == heap->sblocks_tails[size_class_idx]) {
        heap->sblocks_tails[size_class_idx] = sblock->prev;
    }
    if (sblock->prev) {
        sblock->prev->next = sblock->next;
    }
    if (sblock->next) {
        sblock->next->prev = sblock->prev;
    }
}

internal
void release_sblock(sblock_header_t *sblock) {
    release_pages_to_os((void*)sblock, ((sblock->end - ((void*)sblock)) >> system_info.log_2_page_size));
}

internal
void heap_add_sblock(heap_t *heap, sblock_header_t *sblock) {
    u32 size_class_idx;

    size_class_idx = sblock->size_class_idx;

    if (heap->sblocks_heads[size_class_idx] == NULL) {
        ASSERT(heap->sblocks_tails[size_class_idx] == NULL,
               "sblock tail but no sblock head");
        heap->sblocks_heads[size_class_idx]
            = heap->sblocks_tails[size_class_idx]
            = sblock;
    } else {
        heap->sblocks_tails[size_class_idx]->next = sblock;
        sblock->prev = heap->sblocks_tails[size_class_idx];
        heap->sblocks_tails[size_class_idx] = sblock;
    }

    heap->n_sblocks[size_class_idx] += 1;
}

#endif /* HMALLOC_USE_SBLOCKS */



HMALLOC_ALWAYS_INLINE
internal inline void heap_make(heap_t *heap) {
    int i;

    for (i = 0; i < N_SIZE_CLASSES; i += 1) {
        cblock_list_make(&heap->lists[i]);
    }

#ifdef HMALLOC_USE_SBLOCKS
    for (i = 0; i < SBLOCK_N_SIZE_CLASSES; i += 1) {
        heap->n_sblocks[i]     = 0;
        heap->sblocks_heads[i] = NULL;
        heap->sblocks_tails[i] = NULL;
        HEAP_S_LOCK_INIT(heap, i);
    }
#endif
    (void)i;


    heap->__meta.addr   = (void*)heap;
    heap->__meta.handle = NULL;
    heap->__meta.tid    = 0;
    heap->__meta.hid    = __sync_fetch_and_add(&hid_counter, 1);
    heap->__meta.flags  = 0;

    LOG("Created a new heap (hid = %d)\n", heap->__meta.hid);
}

HMALLOC_ALWAYS_INLINE
internal inline void cblock_coalesce_left_to_right(cblock_header_t *cblock, chunk_header_t *left, chunk_header_t *right) {
    ASSERT(left->flags & CHUNK_IS_FREE,         "can't coalesce into a non-free chunk");
    ASSERT(CHECK_CHUNK_MAGIC(left),             "can't coalesce into a chunk without magic");
    ASSERT(right == SMALL_CHUNK_ADJACENT(left), "can't coalesce non-adjacent chunks");

    SET_CHUNK_SIZE(left,
                   CHUNK_SIZE(left) + sizeof(chunk_header_t) + CHUNK_SIZE(right));

    ASSERT(CHUNK_NEXT(left) == NULL ||
           (void*)CHUNK_NEXT(left) > (void*)left + CHUNK_SIZE(left),
           "next chunk in free list has been swallowed");

    ASSERT(CHUNK_PREV(left) ||
           CHUNK_NEXT(left) ||
           (left == cblock->free_list_head && left == cblock->free_list_tail),
           "chunk has been orphaned");

}

HMALLOC_ALWAYS_INLINE
internal inline void cblock_coalesce_right_to_left(cblock_header_t *cblock, chunk_header_t *left, chunk_header_t *right) {
    u64             distance;
    chunk_header_t *next_free_chunk;
    chunk_header_t *prev_free_chunk;

    ASSERT(right->flags & CHUNK_IS_FREE,        "can't coalesce into a non-free chunk");
    ASSERT(CHECK_CHUNK_MAGIC(right),            "can't coalesce into a chunk without magic");
    ASSERT(SMALL_CHUNK_ADJACENT(left) == right, "can't coalesce non-adjacent chunks");

    if (CHUNK_HAS_NEXT(right)) {
        next_free_chunk = CHUNK_NEXT_UNCHECKED(right);

        ASSERT(next_free_chunk->flags & CHUNK_IS_FREE, "next chunk in free list isn't free");
        ASSERT(next_free_chunk > left,                 "bad distance");
        ASSERT(next_free_chunk > right,                "bad distance");

        distance = CHUNK_DISTANCE(next_free_chunk, left);
        SET_CHUNK_OFFSET_NEXT(left, distance);
        SET_CHUNK_OFFSET_PREV(next_free_chunk, distance);
    } else {
        ASSERT(right == cblock->free_list_tail, "right has no next, but isn't tail");

        left->offset_next_words = 0;
        cblock->free_list_tail  = left;
    }

    if (CHUNK_HAS_PREV(right)) {
        prev_free_chunk = CHUNK_PREV_UNCHECKED(right);

        ASSERT(prev_free_chunk->flags & CHUNK_IS_FREE, "prev chunk in free list isn't free");
        ASSERT(prev_free_chunk < left,                 "bad distance");
        ASSERT(prev_free_chunk < right,                "bad distance");

        distance = CHUNK_DISTANCE(left, prev_free_chunk);
        SET_CHUNK_OFFSET_PREV(left, distance);
        SET_CHUNK_OFFSET_NEXT(prev_free_chunk, distance);
    } else {
        ASSERT(right == cblock->free_list_head, "right has no prev, but isn't head");

        left->offset_prev_words = 0;
        cblock->free_list_head  = left;
    }

    SET_CHUNK_SIZE(left,
                   CHUNK_SIZE(left) + sizeof(chunk_header_t) + CHUNK_SIZE(right));

    ASSERT(CHUNK_NEXT(left) == NULL || CHUNK_NEXT(left) > right,
           "next free chunk got swallowed somehow");
}

HMALLOC_ALWAYS_INLINE
internal inline void cblock_remove_chunk_from_free_list(cblock_header_t *cblock, chunk_header_t *chunk) {
    chunk_header_t *prev_free_chunk,
                   *next_free_chunk;
    u64             distance;

/*     chunk->flags &= ~CHUNK_IS_FREE; */
    chunk->flags = 0;
#ifdef HMALLOC_DO_ASSERTIONS
    CLEAR_CHUNK_MAGIC(chunk);
#endif

    if (chunk == cblock->free_list_head) {
        if (chunk == cblock->free_list_tail) {
            cblock->free_list_head = cblock->free_list_tail = NULL;
        } else {
            ASSERT(CHUNK_NEXT(chunk) != NULL, "head is not only free chunk, but has no next");

            next_free_chunk = CHUNK_NEXT_UNCHECKED(chunk);

            next_free_chunk->offset_prev_words = 0;
            cblock->free_list_head             = next_free_chunk;
        }

    } else if (chunk == cblock->free_list_tail) {
        if (chunk == cblock->free_list_head) {
            cblock->free_list_head = cblock->free_list_tail = NULL;
        } else {
            ASSERT(CHUNK_PREV(chunk) != NULL, "tail is not only free chunk, but has no prev");

            prev_free_chunk = CHUNK_PREV_UNCHECKED(chunk);

            prev_free_chunk->offset_next_words = 0;
            cblock->free_list_tail             = prev_free_chunk;
        }

    } else {
        ASSERT(CHUNK_PREV(chunk) != NULL, "free chunk missing prev");
        ASSERT(CHUNK_NEXT(chunk) != NULL, "free chunk missing next");

        prev_free_chunk = CHUNK_PREV_UNCHECKED(chunk);
        next_free_chunk = CHUNK_NEXT_UNCHECKED(chunk);

        ASSERT(next_free_chunk > prev_free_chunk, "bad distance");

        distance = CHUNK_DISTANCE(next_free_chunk, prev_free_chunk);
        SET_CHUNK_OFFSET_PREV(next_free_chunk, distance);
        SET_CHUNK_OFFSET_NEXT(prev_free_chunk, distance);

    }

    ASSERT(!CHECK_CHUNK_MAGIC(chunk),       "non-free chunk still has magic");
    ASSERT(!(chunk->flags & CHUNK_IS_FREE), "flags incorrectly set");
}

HMALLOC_ALWAYS_INLINE
internal inline void cblock_add_chunk_to_free_list(cblock_header_t *cblock, chunk_header_t *chunk) {
    chunk_header_t *free_list_cursor,
                   *prev_free_chunk,
                   *next_free_chunk;
    u64             distance;

    ASSERT(((void*)chunk) < cblock->end, "chunk doesn't belong to this cblock");

    chunk->flags = CHUNK_IS_FREE;

#ifdef HMALLOC_DO_ASSERTIONS
    ASSERT(!CHECK_CHUNK_MAGIC(chunk), "trying to add a chunk to the free list, but it already has magic..");
    SET_CHUNK_MAGIC(chunk);
#endif

    if (cblock->free_list_head == NULL) {
        /* This will be the only chunk on the free list. */
        ASSERT(cblock->free_list_tail == NULL, "tail, but no head");

        cblock->free_list_head   = cblock->free_list_tail   = chunk;
        chunk->offset_prev_words = chunk->offset_next_words = 0;

    } else if (chunk < cblock->free_list_head) {
        /* It should be the new head. */

        /* Can we coalesce with the next chunk? */
        if (SMALL_CHUNK_ADJACENT(chunk) == cblock->free_list_head) {
            cblock_coalesce_right_to_left(cblock, chunk, cblock->free_list_head);
        } else {
            distance = CHUNK_DISTANCE(cblock->free_list_head, chunk);
            SET_CHUNK_OFFSET_PREV(cblock->free_list_head, distance);
            SET_CHUNK_OFFSET_NEXT(chunk,                  distance);
            chunk->offset_prev_words = 0;
            cblock->free_list_head   = chunk;
        }

    } else if (cblock->free_list_tail < chunk) {
        /* It should be the new tail. */

        /* Can we coalesce with the previous chunk? */
        if (SMALL_CHUNK_ADJACENT(cblock->free_list_tail) == chunk) {
            cblock_coalesce_left_to_right(cblock, cblock->free_list_tail, chunk);
            chunk = cblock->free_list_tail;
        } else {
            distance = CHUNK_DISTANCE(chunk, cblock->free_list_tail);
            SET_CHUNK_OFFSET_PREV(chunk,                  distance);
            SET_CHUNK_OFFSET_NEXT(cblock->free_list_tail, distance);
            chunk->offset_next_words = 0;
            cblock->free_list_tail   = chunk;
        }

    } else if (SMALL_CHUNK_ADJACENT(chunk)->flags == CHUNK_IS_FREE) {
        /*
         * Quickly check if we can do a right-to-left coalescing
         * and avoid the scan.
         */
        ASSERT(SMALL_CHUNK_ADJACENT(chunk) != cblock->end,
               "can't coalesce_right_to_left with last chunk");

        cblock_coalesce_right_to_left(cblock, chunk, SMALL_CHUNK_ADJACENT(chunk));

        if (CHUNK_HAS_PREV(chunk)) {
            prev_free_chunk = CHUNK_PREV_UNCHECKED(chunk);
            if (SMALL_CHUNK_ADJACENT(prev_free_chunk) == chunk) {
                cblock_remove_chunk_from_free_list(cblock, chunk);
                cblock_coalesce_left_to_right(cblock, prev_free_chunk, chunk);
                chunk = prev_free_chunk;
            }
        }

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

        ASSERT(free_list_cursor->offset_next_words != 0, "should have next link, but don't");

        next_free_chunk = CHUNK_NEXT_UNCHECKED(free_list_cursor);

        ASSERT(next_free_chunk != NULL,                "didn't find next place in free list");
        ASSERT(next_free_chunk > chunk,                "bad distance");
        ASSERT(next_free_chunk->flags & CHUNK_IS_FREE, "next chunk in free list isn't free");

        /* Patch with previous chunk. */
        if (SMALL_CHUNK_ADJACENT(free_list_cursor) == chunk) {
            cblock_coalesce_left_to_right(cblock, free_list_cursor, chunk);
            chunk = free_list_cursor;
        } else {
            distance = CHUNK_DISTANCE(chunk, free_list_cursor);
            SET_CHUNK_OFFSET_NEXT(free_list_cursor, distance);
            SET_CHUNK_OFFSET_PREV(chunk, distance);
        }

        /* Patch with next chunk. */
        if (SMALL_CHUNK_ADJACENT(chunk) == next_free_chunk) {
            if (chunk == free_list_cursor) {
                cblock_remove_chunk_from_free_list(cblock, next_free_chunk);
                cblock_coalesce_left_to_right(cblock, chunk, next_free_chunk);
            } else {
                cblock_coalesce_right_to_left(cblock, chunk, next_free_chunk);
            }
        } else {
            distance = CHUNK_DISTANCE(next_free_chunk, chunk);
            SET_CHUNK_OFFSET_NEXT(chunk, distance);
            SET_CHUNK_OFFSET_PREV(next_free_chunk, distance);
        }

    }

    ASSERT(CHUNK_PREV(chunk) ||
           CHUNK_NEXT(chunk) ||
           (chunk == cblock->free_list_head && chunk == cblock->free_list_tail),
           "chunk has been orphaned");

}


HMALLOC_ALWAYS_INLINE
internal inline void cblock_split_chunk_and_replace_on_free_list(cblock_header_t *cblock, chunk_header_t *chunk, u64 chunk_size_words, u64 n_words, chunk_header_t *prev_free_chunk, int has_next) {
    chunk_header_t *new_chunk;
    u64             new_chunk_size_words;
    chunk_header_t *old_next;
    u64             distance;
#ifdef HMALLOC_DO_ASSERTIONS
    chunk_header_t *old_adjacent;

    old_adjacent = SMALL_CHUNK_ADJACENT(chunk);
#endif

    ASSERT(n_words >= CHUNK_MIN_SIZE >> 3ULL,    "n_bytes too small");
    ASSERT(cblock->free_list_head != NULL,       "should have free chunk(s) to split");
    ASSERT(chunk->flags & CHUNK_IS_FREE,         "can't split chunk that isn't free");
    ASSERT(IS_ALIGNED(CHUNK_USER_MEM(chunk), 8), "user mem is misaligned");

    new_chunk = CHUNK_USER_MEM(chunk) + (n_words << 3ULL);
    ASSERT(IS_ALIGNED(new_chunk, 8),             "new_chunk is misaligned");

/*     chunk->flags &= ~CHUNK_IS_FREE; */
    chunk->flags = 0;
#ifdef HMALLOC_DO_ASSERTIONS
    ASSERT(CHECK_CHUNK_MAGIC(chunk), "can't split a chunk that still has magic");
    CLEAR_CHUNK_MAGIC(chunk);
    SET_CHUNK_MAGIC(new_chunk);
#endif

    new_chunk_size_words = chunk_size_words - n_words - 1/* sizeof(chunk_header_t) */;
    chunk->size          = n_words;

    /*
     * This produces much better code than setting the whole header to
     * zero, setting the size, and then or'ing the CHUNK_IS_FREE flag.
     */
    *new_chunk = (chunk_header_t){
        .offset_prev_words = 0,
        .offset_next_words = 0,
        .size              = new_chunk_size_words,
        .flags             = CHUNK_IS_FREE
    };

    ASSERT(((void*)new_chunk) + sizeof(chunk_header_t) + CHUNK_SIZE(new_chunk) == (void*)old_adjacent,
           "size mismatch in cblock_split_chunk()");

    ASSERT(CHUNK_SIZE(chunk)     >= CHUNK_MIN_SIZE, "chunk too small");
    ASSERT(CHUNK_SIZE(new_chunk) >= CHUNK_MIN_SIZE, "new_chunk too small");

    if (prev_free_chunk) {
        distance = CHUNK_DISTANCE(new_chunk, prev_free_chunk);
        ASSERT(distance != 0, "bad distance");
        SET_CHUNK_OFFSET_NEXT(prev_free_chunk, distance);
        SET_CHUNK_OFFSET_PREV(new_chunk, distance);
    } else {
        ASSERT(chunk == cblock->free_list_head, "chunk has no prev, but isn't head");
        cblock->free_list_head = new_chunk;
    }

    if (has_next) {
        old_next = CHUNK_NEXT_UNCHECKED(chunk);
        distance = CHUNK_DISTANCE(old_next, new_chunk);
        ASSERT(distance != 0, "bad distance");
        SET_CHUNK_OFFSET_NEXT(new_chunk, distance);
        SET_CHUNK_OFFSET_PREV(old_next, distance);
    } else {
        ASSERT(chunk == cblock->free_list_tail, "chunk has no next, but isn't tail");
        cblock->free_list_tail = new_chunk;
    }

    ASSERT(CHUNK_PREV(new_chunk) ||
           CHUNK_NEXT(new_chunk) ||
           (new_chunk == cblock->free_list_head && new_chunk == cblock->free_list_tail),
           "split chunk has created an orphaned chunk");
}


HMALLOC_ALWAYS_INLINE
internal inline chunk_header_t * cblock_get_chunk_if_free(cblock_header_t *cblock, u64 n_bytes) {
    u64             n_words;
    chunk_header_t *chunk;
    chunk_header_t *prev_chunk;
    int             has_next;
    u64             chunk_size_words;
    u64             chunk_size;

    ASSERT(IS_ALIGNED(n_bytes, 8), "n_bytes not aligned");

    n_words = n_bytes >> 3ULL;

#if HMALLOC_DO_ASSERTIONS
    chunk_header_t *cblock_first_chunk;
    cblock_first_chunk = CBLOCK_FIRST_CHUNK(cblock);
    ASSERT(!(cblock_first_chunk->flags & CHUNK_IS_BIG),
           "trying to get a chunk from a cblock dedicated to a single big chunk");
#endif

    if (cblock->free_list_head == NULL) {
        ASSERT(cblock->free_list_tail == NULL,
               "head, but no tail");
        return NULL;
    }

    prev_chunk = NULL;
    has_next   = 1;

    /* Scan until we find a chunk big enough. */
    for (chunk = cblock->free_list_head;
         chunk != cblock->free_list_tail;
         chunk = CHUNK_NEXT_UNCHECKED(chunk)) {

        chunk_size_words = chunk->size;
        if (chunk_size_words >= n_words) { goto found; }
        prev_chunk = chunk;
    }

    /* Check the tail. */
    ASSERT(chunk == cblock->free_list_tail, "last chunk isn't tail");
    if ((chunk_size_words = chunk->size) >= n_words) {
        has_next = 0;
        goto found;
    }

    /* Nothing viable in this cblock. */
    return NULL;

found:;

    chunk_size = chunk_size_words << 3ULL;

    /* Can we split this into two chunks? */
    if (chunk_size - n_bytes >= (sizeof(chunk_header_t) + CHUNK_MIN_SIZE)) {
        cblock_split_chunk_and_replace_on_free_list(cblock, chunk, chunk_size_words, n_words, prev_chunk, has_next);
    } else {
        cblock_remove_chunk_from_free_list(cblock, chunk);
    }

    ASSERT(!CHECK_CHUNK_MAGIC(chunk), "attempting to release a chunk that still has magic");
    ASSERT(!(chunk->flags & CHUNK_IS_FREE), "chunk never became released");
    ASSERT(CHUNK_SIZE(chunk) >= n_bytes,    "split chunk is no longer big enough");

    return chunk;
}

HMALLOC_ALWAYS_INLINE
internal inline chunk_header_t * heap_get_free_chunk_from_cblock_list(heap_t *heap, cblock_list_t *list, u64 n_bytes) {
    chunk_header_t  *chunk;
    cblock_header_t *cblock;

    chunk = NULL;

    LIST_LOCK(list);

    cblock = list->head;

    while (cblock != NULL) {
        chunk = cblock_get_chunk_if_free(cblock, n_bytes);

        if (chunk != NULL) { goto found; }

        cblock = cblock->next;
    }

    /*
     * We've gone through all of the cblocks and haven't found a
     * big enough chunk.
     * So, we'll have to add a new cblock.
     */
    cblock = heap_new_cblock(heap, n_bytes);
    cblock_list_add_cblock(list, cblock);
    chunk = cblock_get_chunk_if_free(cblock, n_bytes);

found:;
    LIST_UNLOCK(list);

    ASSERT(chunk != NULL, "invalid chunk -- could not allocate memory");

    return chunk;
}

HMALLOC_ALWAYS_INLINE
internal inline void * heap_alloc_from_cblock_list(heap_t *heap, cblock_list_t *list, u64 n_bytes) {
    chunk_header_t *chunk;
    void           *mem;

    chunk = heap_get_free_chunk_from_cblock_list(heap, list, n_bytes);
    mem   = CHUNK_USER_MEM(chunk);

    ASSERT(IS_ALIGNED(mem, 8), "user memory is not properly aligned");

    return mem;
}


#ifdef HMALLOC_USE_SBLOCKS

HMALLOC_ALWAYS_INLINE
internal inline void * sblock_get_slot_if_free(sblock_header_t *sblock) {
    u32   first_free_region;
    u32   first_free_slot;
    u32   absolute_slot_idx;
    void *slot;

    /* We might not have any free slots to offer. */
    if (sblock->n_allocations == sblock->max_allocations) {
        ASSERT(sblock->regions_bitfield == 0, "all allocations taken, but not all regions marked");
        return NULL;
    }

    ASSERT(sblock->n_allocations < sblock->max_allocations,
           "sblock->n_allocations mismatch");

    sblock->n_allocations += 1;

    ASSERT(sblock->regions_bitfield != 0ULL, "regions bitfield is zero");
    first_free_region = __builtin_clzll(sblock->regions_bitfield);

    ASSERT(sblock->slots_bitfields[first_free_region] != 0ULL, "slots bitfield is zero");
    first_free_slot = __builtin_clzll(sblock->slots_bitfields[first_free_region]);

    sblock->slots_bitfields[first_free_region] &= ~(1ULL << (63ULL - first_free_slot));

    if (sblock->slots_bitfields[first_free_region] == 0ULL) {
        sblock->regions_bitfield &= ~(1ULL << (63ULL - first_free_region));
    }

    absolute_slot_idx = (first_free_region << 6ULL) + first_free_slot;

    slot =   ((void*)sblock)
           + (absolute_slot_idx * sblock->size_class);

    ASSERT(slot >= (((void*)(sblock)) + 4096ULL), "slot address too small");
    ASSERT(slot < sblock->end,                    "slot address too large");

    return slot;
}

HMALLOC_ALWAYS_INLINE
internal inline void * heap_alloc_from_sblocks(heap_t *heap, u64 size_class, u32 size_class_idx) {
    sblock_header_t *sblock;
    void            *mem;

    mem    = NULL;
    sblock = heap->sblocks_heads[size_class_idx];

    while (sblock != NULL) {
        ASSERT(sblock->size_class == size_class,
               "sblock isn't in the right size_class list");

        if ((mem = sblock_get_slot_if_free(sblock))) {
            goto out;
        }

        sblock = sblock->next;
    }

    sblock = heap_new_sblock(heap, size_class, size_class_idx);
    heap_add_sblock(heap, sblock);
    mem = sblock_get_slot_if_free(sblock);

out:
    ASSERT(sblock != NULL,     "did not find an sblock");
    ASSERT(mem != NULL,        "did not get slot from sblock");
    ASSERT(IS_ALIGNED(mem, 8), "mem is not aligned");

    return mem;
}

#endif /* HMALLOC_USE_SBLOCKS */



HMALLOC_ALWAYS_INLINE
internal inline void heap_free_big_chunk(heap_t *heap, chunk_header_t *big_chunk) {
    cblock_header_t *cblock;

    cblock = &((block_header_t*)ADDR_PARENT_BLOCK(big_chunk))->c;
    release_cblock(cblock);
}

HMALLOC_ALWAYS_INLINE
internal inline chunk_header_t * _heap_get_big_chunk(heap_t *heap, cblock_header_t *cblock) {
    chunk_header_t  *chunk;

    chunk         = CBLOCK_FIRST_CHUNK(cblock);
    chunk->flags &= ~(CHUNK_IS_FREE);
    chunk->flags |=   CHUNK_IS_BIG;

    return chunk;
}

HMALLOC_ALWAYS_INLINE
internal inline chunk_header_t * heap_get_big_chunk(heap_t *heap, u64 n_bytes) {
    cblock_header_t *cblock;

    cblock = heap_new_cblock(heap, n_bytes);
    cblock->free_list_head = cblock->free_list_tail = NULL;
    return _heap_get_big_chunk(heap, cblock);
}

HMALLOC_ALWAYS_INLINE
internal inline void * heap_big_alloc(heap_t *heap, u64 n_bytes) {
    chunk_header_t *chunk;
    void           *mem;

    chunk = heap_get_big_chunk(heap, n_bytes);

    ASSERT(chunk != NULL, "did not get big chunk");

    mem = CHUNK_USER_MEM(chunk);

    ASSERT(IS_ALIGNED(mem, 8), "user memory is not properly aligned for performance");

    return mem;
}

HMALLOC_ALWAYS_INLINE
internal inline void * heap_alloc(heap_t *heap, u64 n_bytes) {
    void *mem;
    u64   size_class;
    u32   size_class_idx;

#ifdef HMALLOC_USE_SBLOCKS
    /*
     * No need to do the manual alignment because the sblock slots are
     * always aligned to at least the minimum required aligment.
     */

    if (n_bytes <= SBLOCK_MAX_ALLOC_SIZE) {
        if (unlikely(n_bytes == 0)) {
            n_bytes        = size_class = SBLOCK_SMALLEST_CLASS;
            size_class_idx = 0;
        } else {
            size_class     = ALIGN(n_bytes, SBLOCK_INTERVAL);
            size_class_idx = (size_class >> LOG2_64BIT(SBLOCK_INTERVAL)) - 1ULL;
        }

        HEAP_S_LOCK(heap, size_class_idx); {
            mem = heap_alloc_from_sblocks(heap, size_class, size_class_idx);
        } HEAP_S_UNLOCK(heap, size_class_idx);

        goto out;
    }

    /*
     * We are allocating something a little bigger.
     * So, we'll use the regular cblocks.
     */

    /*
     * Round n_bytes to the nearest multiple of 8 so that
     * chunk sizes are word aligned.
     */

    n_bytes = ALIGN(n_bytes, 8);
#else
    n_bytes = unlikely(n_bytes == 0)
                ? 8
                : ALIGN(n_bytes, 8);

    (void)size_class;
    (void)size_class_idx;
#endif

    ASSERT(n_bytes >= CHUNK_MIN_SIZE, "chunk request too small");

#ifdef HMALLOC_USE_SBLOCKS
    ASSERT(n_bytes >  SBLOCK_MAX_ALLOC_SIZE, "CHUNK_MIN_SIZE and SBLOCK_MAX_ALLOC_SIZE are in conflict");
#endif

    if (unlikely(n_bytes > MAX_SMALL_CHUNK)) {
        /*
         * Currently, big allocations require no locking
         * because they don't touch any block lists or anything like that.
         * If this changes, it will probably be necessary to add the
         * the approriate locking here.
         */
        mem = heap_big_alloc(heap, n_bytes);

        goto out;
    }

    size_class     = SMALLEST_CLASS;
    size_class_idx = 0;
    while (n_bytes > size_class) {
        size_class     <<= 2;
        size_class_idx  += 1;

        if (size_class == LARGEST_CLASS) {
            break;
        }
    }

    mem = heap_alloc_from_cblock_list(heap, &heap->lists[size_class_idx], n_bytes);

out:;
    ASSERT(IS_ALIGNED(mem, 8), "user memory is not properly aligned for performance");

    return mem;
}

HMALLOC_ALWAYS_INLINE
internal inline void cblock_free_chunk(cblock_header_t *cblock, chunk_header_t *chunk) {
    cblock_list_t *list;

    ASSERT(!(chunk->flags & CHUNK_IS_FREE), "double free error");

    list = cblock->list;

    LIST_LOCK(list);

    cblock_add_chunk_to_free_list(cblock, chunk);

#define WHOLE_BLOCK_CHUNK_SIZE_IN_WORDS \
    ((DEFAULT_BLOCK_SIZE - sizeof(chunk_header_t) - sizeof(block_header_t)) >> 3ULL)

    /* If the cblock only has one free chunk... */
    if (unlikely(cblock->free_list_head == cblock->free_list_tail)) {
        /* and if that chunk spans the whole cblock -- release it. */
        if (cblock->free_list_head->size == WHOLE_BLOCK_CHUNK_SIZE_IN_WORDS) {
            /* If this cblock isn't the only cblock in the heap... */
            if (cblock != list->head || cblock != list->tail) {
                cblock_list_remove_cblock(list, cblock);
                release_cblock(cblock);
            }
        }
    }

    LIST_UNLOCK(list);
#undef WHOLE_BLOCK_CHUNK_SIZE
}



#ifdef HMALLOC_USE_SBLOCKS

HMALLOC_ALWAYS_INLINE
internal inline void heap_free_from_sblock(heap_t *heap, sblock_header_t *sblock, void *slot) {
    u32 absolute_slot_idx;
    u32 r;
    u32 s;

    switch (sblock->size_class_idx) {
        case SBLOCK_CLASS_NANO_IDX:
            absolute_slot_idx =
                ((__sblock_slot_nano_t*)slot) - ((__sblock_slot_nano_t*)(void*)sblock);
            break;
        case SBLOCK_CLASS_MICRO_IDX:
            absolute_slot_idx =
                ((__sblock_slot_micro_t*)slot) - ((__sblock_slot_micro_t*)(void*)sblock);
            break;
        case SBLOCK_CLASS_TINY_IDX:
            absolute_slot_idx =
                ((__sblock_slot_tiny_t*)slot) - ((__sblock_slot_tiny_t*)(void*)sblock);
            break;
        case SBLOCK_CLASS_SMALL_IDX:
            absolute_slot_idx =
                ((__sblock_slot_small_t*)slot) - ((__sblock_slot_small_t*)(void*)sblock);
            break;
        case SBLOCK_CLASS_MEDIUM_IDX:
            absolute_slot_idx =
                ((__sblock_slot_medium_t*)slot) - ((__sblock_slot_medium_t*)(void*)sblock);
            break;
        case SBLOCK_CLASS_LARGE_IDX:
            absolute_slot_idx =
                ((__sblock_slot_large_t*)slot) - ((__sblock_slot_large_t*)(void*)sblock);
            break;
        case SBLOCK_CLASS_HUGE_IDX:
            absolute_slot_idx =
                ((__sblock_slot_huge_t*)slot) - ((__sblock_slot_huge_t*)(void*)sblock);
            break;
        case SBLOCK_CLASS_MEGA_IDX:
            absolute_slot_idx =
                ((__sblock_slot_mega_t*)slot) - ((__sblock_slot_mega_t*)(void*)sblock);
            break;
        default:
            ASSERT(0, "invalid size_class");
    }

    r = absolute_slot_idx >> 6ULL;
    s = absolute_slot_idx & 63ULL;

    ASSERT(!(sblock->slots_bitfields[r] & (1ULL << (63ULL - s))), "slot was not taken");

    if (sblock->slots_bitfields[r] == 0ULL) {
        sblock->regions_bitfield |= (1ULL << (63ULL - r));
    }
    sblock->slots_bitfields[r] |= (1ULL << (63ULL - s));

    ASSERT(sblock->n_allocations > 0, "sblock->n_allocations mismatch");
    sblock->n_allocations -= 1;

    /*
     * If all regions are empty (i.e., the sblock contains no allocations),
     * then we can consider releasing the sblock.
     */
    if (sblock->n_allocations == 0) {
        if (heap->n_sblocks[sblock->size_class_idx] > 1) {
            heap_remove_sblock(heap, sblock);
            release_sblock(sblock);
        }
    }
}

#endif /* HMALLOC_USE_SBLOCKS */



HMALLOC_ALWAYS_INLINE
internal inline void heap_free(heap_t *heap, void *addr) {
    block_header_t *block;
    u32             size_class_idx;
    chunk_header_t *chunk;

    block = ADDR_PARENT_BLOCK(addr);

#ifdef HMALLOC_USE_SBLOCKS
    if (block->block_kind == BLOCK_KIND_SBLOCK) {
        size_class_idx = block->s.size_class_idx;
        HEAP_S_LOCK(heap, size_class_idx); {
            heap_free_from_sblock(heap, &(block->s), addr);
        } HEAP_S_UNLOCK(heap, size_class_idx);
    } else
#endif
    { (void)size_class_idx;

        ASSERT(block->block_kind == BLOCK_KIND_CBLOCK, "block isn't cblock");

        chunk = CHUNK_FROM_USER_MEM(addr);

        if (unlikely(chunk->flags & CHUNK_IS_BIG)) {
            heap_free_big_chunk(heap, chunk);
        } else {
            cblock_free_chunk(&block->c, chunk);
        }
    }
}

HMALLOC_ALWAYS_INLINE
internal inline void * heap_aligned_alloc(heap_t *heap, size_t n_bytes, size_t alignment) {
    u64              sblock_n_bytes;
    u64              cblock_n_bytes;
    u64              size_class;
    u32              size_class_idx;
    chunk_header_t  *chunk;
    void            *chunk_end;
    void            *aligned_addr;
    void            *mem;
    chunk_header_t  *split_chunk;

    ASSERT(alignment > 0,                   "invalid alignment -- must be > 0");
    ASSERT(IS_POWER_OF_TWO(alignment),      "invalid alignment -- must be a power of two");
    ASSERT(alignment <= DEFAULT_BLOCK_SIZE, "alignment is too large");

    if (n_bytes < 8) { n_bytes = 8; }

    mem = NULL;

    /*
     * All allocations are guaranteed to be aligned on 8 byte
     * boundaries, so just do normal allocation if we can.
     */
    if (alignment <= 8) {
        return heap_alloc(heap, n_bytes);
    }

#ifdef HMALLOC_USE_SBLOCKS
    /*
     * All sblock slots are aligned on boundaries equal to their size class.
     * If size will fit in a slot, we'll try to get one.
     */
    if (n_bytes <= SBLOCK_MAX_ALLOC_SIZE && alignment <= SBLOCK_MAX_ALLOC_SIZE) {
        sblock_n_bytes = MAX(n_bytes, alignment);
        size_class     = ALIGN(sblock_n_bytes, SBLOCK_INTERVAL);
        size_class_idx = (size_class >> LOG2_64BIT(SBLOCK_INTERVAL)) - 1ULL;

        HEAP_S_LOCK(heap, size_class_idx); {
            mem = heap_alloc_from_sblocks(heap, size_class, size_class_idx);
        } HEAP_S_UNLOCK(heap, size_class_idx);

        if (mem) { goto out; }
    }
#endif
    (void)sblock_n_bytes;

    cblock_n_bytes = alignment + n_bytes;

    /*
     * If size is big:
     * Use the regular big chunk procedure to get memory, but
     * force the big chunk to have its chunk_header_t in the right place.
     * free() won't work correctly if we don't do this.
     */
    if (cblock_n_bytes > MAX_SMALL_CHUNK) {
        chunk = heap_get_big_chunk(heap, cblock_n_bytes);
        mem   = ALIGN(CHUNK_USER_MEM(chunk), alignment);
        memcpy(mem - sizeof(chunk_header_t), chunk, sizeof(chunk_header_t));
        goto out;
    }

    size_class     = SMALLEST_CLASS;
    size_class_idx = 0;
    while (cblock_n_bytes > size_class) {
        size_class     <<= 2;
        size_class_idx  += 1;

        if (size_class == LARGEST_CLASS) {
            break;
        }
    }

    chunk = heap_get_free_chunk_from_cblock_list(heap, &heap->lists[size_class_idx], cblock_n_bytes);

    if (IS_ALIGNED(CHUNK_USER_MEM(chunk), alignment)) {
        mem = CHUNK_USER_MEM(chunk);
        goto out;
    }

    chunk_end    = ((void*)chunk) + CHUNK_SIZE(chunk);
    aligned_addr = ALIGN(CHUNK_USER_MEM(chunk), alignment);

    ASSERT(aligned_addr < chunk_end,           "could not get aligned memory from the single chunk");
    ASSERT(aligned_addr + n_bytes < chunk_end, "not enough room in chunk for aligned memory");

    split_chunk = chunk;
    chunk       = CHUNK_FROM_USER_MEM(aligned_addr);
    *chunk      = *split_chunk;
    SET_CHUNK_SIZE(chunk, chunk_end - aligned_addr);
    SET_CHUNK_SIZE(split_chunk, ((void*)chunk) - CHUNK_USER_MEM(split_chunk));

    heap_free(heap, CHUNK_USER_MEM(split_chunk));

    mem = aligned_addr;

out:
    ASSERT(IS_ALIGNED(mem, alignment), "failed to get aligned memory");

    return mem;
}

HMALLOC_ALWAYS_INLINE
internal inline i32 heap_handle_equ(heap_handle_t a, heap_handle_t b) { return strcmp(a, b) == 0; }

HMALLOC_ALWAYS_INLINE
internal inline u64 heap_handle_hash(heap_handle_t h) {
    unsigned long hash = 5381;
    int           c;

    while ((c = *h++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

HMALLOC_ALWAYS_INLINE
internal inline void user_heaps_init(void) {
    USER_HEAPS_LOCK(); {
        user_heaps = hash_table_make_e(heap_handle_t, heap_t, heap_handle_hash, heap_handle_equ);
    } USER_HEAPS_UNLOCK();
    LOG("initialized user heaps table\n");
}

HMALLOC_ALWAYS_INLINE
internal inline heap_t * get_or_make_user_heap(char *handle) {
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
