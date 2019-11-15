#define _GNU_SOURCE

#include "hmalloc.h"
#include "internal.h"

#include "FormatString.c"
#include "internal.c"
#include "internal_malloc.c"
#include "heap.c"
#include "thread.c"
#include "os.c"
#include "init.c"
#include "profile.c"

#define malloc imalloc
#define free ifree
#include "array.c"
#undef malloc
#undef free

#include <string.h>
#include <errno.h>

external void *hmalloc_malloc(size_t n_bytes) {
    heap_t *heap;
    void   *addr;

    if (unlikely(hmalloc_use_imalloc)) {
        return imalloc(n_bytes);
    }

    heap = acquire_this_thread_heap();
    addr = heap_alloc(heap, n_bytes);
    release_heap(heap);

    return addr;
}

external void * hmalloc_calloc(size_t count, size_t n_bytes) {
    void *addr;
    u64   new_n_bytes;

    if (unlikely(hmalloc_use_imalloc)) {
        return icalloc(count, n_bytes);
    }

    new_n_bytes = count * n_bytes;
    addr        = hmalloc_malloc(new_n_bytes);

    memset(addr, 0, new_n_bytes);

    return addr;
}

external void * hmalloc_realloc(void *addr, size_t n_bytes) {
    void *new_addr;
    u64   old_size;

    if (unlikely(hmalloc_use_imalloc)) {
        return irealloc(addr, n_bytes);
    }

    new_addr = NULL;

    if (addr == NULL) {
        new_addr = hmalloc_malloc(n_bytes);
    } else {
        if (likely(n_bytes > 0)) {
            old_size = hmalloc_malloc_size(addr);
            /*
             * This is done for us in heap_alloc, but we'll
             * need the aligned value when we get the copy length.
             */
            n_bytes  = ALIGN(n_bytes, 8);

            /*
             * If it's already big enough, just leave it.
             * We won't worry about shrinking it.
             * Saves us an alloc, free, and memcpy.
             * Plus, we don't have to lock the thread.
             */
            if (old_size >= n_bytes) {
                return addr;
            }

            new_addr = hmalloc_malloc(n_bytes);
            memcpy(new_addr, addr, old_size);
        }

        hmalloc_free(addr);
    }

    return new_addr;
}

external void * hmalloc_reallocf(void *addr, size_t n_bytes) {
    return hmalloc_realloc(addr, n_bytes);
}

external void * hmalloc_valloc(size_t n_bytes) {
    heap_t *heap;
    void   *addr;

    if (unlikely(hmalloc_use_imalloc)) {
        return ivalloc(n_bytes);
    }

    heap = acquire_this_thread_heap();
    addr = heap_aligned_alloc(heap, n_bytes, system_info.page_size);
    release_heap(heap);

    return addr;
}

external void hmalloc_free(void *addr) {
    heap_t         *heap;
    block_header_t *block;

    if (unlikely(hmalloc_use_imalloc)) {
        ifree(addr);
        return;
    }

    if (unlikely(addr == NULL)) {
        return;
    }

    block = ADDR_PARENT_BLOCK(addr);

    if (block->heap__meta.flags & HEAP_THREAD) {
        heap = acquire_thread_heap(block->heap__meta.tid);
    } else if (block->heap__meta.flags & HEAP_USER) {
        heap = acquire_user_heap(block->heap__meta.handle);
    } else {
        heap = NULL;
        ASSERT(0, "invalid block->heap__meta.flags\n");
    }
    heap_free(heap, addr);
    release_heap(heap);
}

external int hmalloc_posix_memalign(void **memptr, size_t alignment, size_t n_bytes) {
    heap_t *heap;

    if (unlikely(hmalloc_use_imalloc)) {
        return iposix_memalign(memptr, alignment, n_bytes);
    }

    if (unlikely(!IS_POWER_OF_TWO(alignment)
    ||  alignment < sizeof(void*))) {
        return EINVAL;
    }

    heap    = acquire_this_thread_heap();
    *memptr = heap_aligned_alloc(heap, n_bytes, alignment);
    release_heap(heap);

    if (unlikely(*memptr == NULL))    { return ENOMEM; }
    return 0;
}

external void * hmalloc_aligned_alloc(size_t alignment, size_t size) {
    heap_t *heap;
    void   *addr;

    if (unlikely(hmalloc_use_imalloc)) {
        return ialigned_alloc(alignment, size);
    }

    heap = acquire_this_thread_heap();
    addr = heap_aligned_alloc(heap, size, alignment);
    release_heap(heap);

    return addr;
}

external size_t hmalloc_malloc_size(void *addr) {
    block_header_t *block;
    chunk_header_t *chunk;

    if (unlikely(hmalloc_use_imalloc)) {
        return imalloc_size(addr);
    }

    if (unlikely(addr == NULL)) {
        return 0;
    }

    block = ADDR_PARENT_BLOCK(addr);

    if (likely(block->block_kind == BLOCK_KIND_CBLOCK)) {
        chunk = CHUNK_FROM_USER_MEM(addr);

        if (unlikely(chunk->flags & CHUNK_IS_BIG)) {
            /*
             * Caculate size of cblock for big chunk.
             */
            return block->c.end - (void*)CHUNK_USER_MEM(chunk);
        }

        return CHUNK_SIZE(chunk);
    } else if (likely(block->block_kind == BLOCK_KIND_SBLOCK)) {
        return SBLOCK_SLOT_SIZE;
    }

    ASSERT(0, "couldn't determine size of allocation");

    return 0;
}


external void * hmalloc(heap_handle_t h, size_t n_bytes) {
    heap_t *heap;
    void   *addr;

    heap = acquire_user_heap(h);
    addr = heap_alloc(heap, n_bytes);
    release_heap(heap);

    return addr;
}

external void * hcalloc(heap_handle_t h, size_t count, size_t n_bytes) {
    void *addr;
    u64   new_n_bytes;

    new_n_bytes = count * n_bytes;
    addr        = hmalloc(h, new_n_bytes);

    memset(addr, 0, new_n_bytes);

    return addr;
}

external void * hrealloc(heap_handle_t h, void *addr, size_t n_bytes) {
    void *new_addr;
    u64   old_size;

    new_addr = NULL;

    if (addr == NULL) {
        new_addr = hmalloc(h, n_bytes);
    } else {
        if (likely(n_bytes > 0)) {
            old_size = hmalloc_malloc_size(addr);
            /*
             * This is done for us in heap_alloc, but we'll
             * need the aligned value when we get the copy length.
             */
            n_bytes  = ALIGN(n_bytes, 8);

            /*
             * If it's already big enough, just leave it.
             * We won't worry about shrinking it.
             * Saves us an alloc, free, and memcpy.
             * Plus, we don't have to lock the thread.
             */
            if (old_size >= n_bytes) {
                return addr;
            }

            new_addr = hmalloc(h, n_bytes);
            memcpy(new_addr, addr, old_size);
        }

        hmalloc_free(addr);
    }

    return new_addr;
}

external void * hreallocf(heap_handle_t h, void *addr, size_t n_bytes) {
    return hrealloc(h, addr, n_bytes);
}

external void * hvalloc(heap_handle_t h, size_t n_bytes) {
    heap_t *heap;
    void   *addr;

    heap = acquire_user_heap(h);
    addr = heap_aligned_alloc(heap, n_bytes, system_info.page_size);
    release_heap(heap);

    return addr;
}

external void hfree(void *addr)    { hmalloc_free(addr); }

external int hposix_memalign(heap_handle_t h, void **memptr, size_t alignment, size_t size) {
    heap_t *heap;

    if (unlikely(!IS_POWER_OF_TWO(alignment)
    ||  alignment < sizeof(void*))) {
        return EINVAL;
    }

    heap    = acquire_user_heap(h);
    *memptr = heap_aligned_alloc(heap, size, alignment);
    release_heap(heap);

    if (unlikely(*memptr == NULL))    { return ENOMEM; }
    return 0;
}

external void * haligned_alloc(heap_handle_t h, size_t alignment, size_t size) {
    heap_t *heap;
    void   *addr;

    heap = acquire_user_heap(h);
    addr = heap_aligned_alloc(heap, size, alignment);
    release_heap(heap);

    return addr;
}

external void * hmemalign(heap_handle_t h, size_t alignment, size_t size) {
    return haligned_alloc(h, alignment, size);
}

size_t hmalloc_size(void *addr) {
    return hmalloc_malloc_size(addr);
}

size_t hmalloc_usable_size(void *addr) {
    return hmalloc_malloc_size(addr);
}


external void * malloc(size_t n_bytes)               { return hmalloc_malloc(n_bytes);         }
external void * calloc(size_t count, size_t n_bytes) { return hmalloc_calloc(count, n_bytes);  }
external void * realloc(void *addr, size_t n_bytes)  { return hmalloc_realloc(addr, n_bytes);  }
external void * reallocf(void *addr, size_t n_bytes) { return hmalloc_reallocf(addr, n_bytes); }
external void * valloc(size_t n_bytes)               { return hmalloc_valloc(n_bytes);         }
external void * pvalloc(size_t n_bytes)              { ASSERT(0, "pvalloc"); return NULL;      }
external void   free(void *addr)                     { hmalloc_free(addr);                     }

external int posix_memalign(void **memptr, size_t alignment, size_t size) {
    return hmalloc_posix_memalign(memptr, alignment, size);
}

external void * aligned_alloc(size_t alignment, size_t size) {
    return hmalloc_aligned_alloc(alignment, size);
}

external void * memalign(size_t alignment, size_t size) {
    return hmalloc_aligned_alloc(alignment, size);
}

external size_t malloc_size(void *addr)        { return hmalloc_malloc_size(addr); }
external size_t malloc_usable_size(void *addr) { return hmalloc_malloc_size(addr); }
