#define _GNU_SOURCE

#include "hmalloc.h"
#include "internal.h"

#include "FormatString.c"
#include "internal.c"
#include "internal_malloc.c"
#include "heap.c"
#include "thread.c"
#include "os.c"
#include "locks.c"
#include "init.c"

#include <string.h>
#include <errno.h>

__attribute__((always_inline))
external inline void *hmalloc_malloc(size_t n_bytes) {
    return heap_alloc(get_this_thread_heap(), n_bytes);
}

external inline void * hmalloc_calloc(size_t count, size_t n_bytes) {
    void *addr;
    u64   new_n_bytes;

    new_n_bytes = count * n_bytes;
    addr        = hmalloc_malloc(new_n_bytes);

    memset(addr, 0, new_n_bytes);

    return addr;
}

external inline void * hmalloc_realloc(void *addr, size_t n_bytes) {
    void *new_addr;
    u64   old_size;

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

external inline void * hmalloc_reallocf(void *addr, size_t n_bytes) {
    return hmalloc_realloc(addr, n_bytes);
}

external inline void * hmalloc_valloc(size_t n_bytes) {
    heap_t *heap;
    void   *addr;

    heap = get_this_thread_heap();
    addr = heap_aligned_alloc(heap, n_bytes, system_info.page_size);

    return addr;
}

__attribute__((always_inline))
external inline void hmalloc_free(void *addr) {
    if (likely(addr != NULL)) {
        ASSERT(BLOCK_GET_HEAP_PTR(ADDR_PARENT_BLOCK(addr)) != NULL,
              "attempting to free from block that doesn't have a heap\n");

        heap_free(BLOCK_GET_HEAP_PTR(ADDR_PARENT_BLOCK(addr)), addr);
    }
}

external inline int hmalloc_posix_memalign(void **memptr, size_t alignment, size_t n_bytes) {
    heap_t *heap;

    if (unlikely(!IS_POWER_OF_TWO(alignment)
    ||  alignment < sizeof(void*))) {
        return EINVAL;
    }

    heap    = get_this_thread_heap();
    *memptr = heap_aligned_alloc(heap, n_bytes, alignment);

    if (unlikely(*memptr == NULL))    { return ENOMEM; }
    return 0;
}

external inline void * hmalloc_aligned_alloc(size_t alignment, size_t size) {
    heap_t *heap;
    void   *addr;

    heap = get_this_thread_heap();
    addr = heap_aligned_alloc(heap, size, alignment);

    return addr;
}

external inline size_t hmalloc_malloc_size(void *addr) {
    block_header_t *block;
    chunk_header_t *chunk;

    if (unlikely(addr == NULL)) { return 0; }

    block = ADDR_PARENT_BLOCK(addr);

    if (block->block_kind == BLOCK_KIND_CBLOCK) {
        chunk = CHUNK_FROM_USER_MEM(addr);

        if (unlikely(chunk->flags & CHUNK_IS_BIG)) {
            /*
             * Caculate size of cblock for big chunk.
             */
            return block->c.end - (void*)CHUNK_USER_MEM(chunk);
        }

        return CHUNK_SIZE(chunk);
    }
#ifdef HMALLOC_USE_SBLOCKS
    else if (likely(block->block_kind == BLOCK_KIND_SBLOCK)) {
        return block->s.size_class;
    }
#endif

    ASSERT(0, "couldn't determine size of allocation");

    return 0;
}


external inline void * hmalloc(heap_handle_t h, size_t n_bytes) {
    heap_t *heap;
    void   *addr;

    heap = get_user_heap(h);
    addr = heap_alloc(heap, n_bytes);

    return addr;
}

external inline void * hcalloc(heap_handle_t h, size_t count, size_t n_bytes) {
    void *addr;
    u64   new_n_bytes;

    new_n_bytes = count * n_bytes;
    addr        = hmalloc(h, new_n_bytes);

    memset(addr, 0, new_n_bytes);

    return addr;
}

external inline void * hrealloc(heap_handle_t h, void *addr, size_t n_bytes) {
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

external inline void * hreallocf(heap_handle_t h, void *addr, size_t n_bytes) {
    return hrealloc(h, addr, n_bytes);
}

external inline void * hvalloc(heap_handle_t h, size_t n_bytes) {
    heap_t *heap;
    void   *addr;

    heap = get_user_heap(h);
    addr = heap_aligned_alloc(heap, n_bytes, system_info.page_size);

    return addr;
}

external inline void * hpvalloc(heap_handle_t h, size_t n_bytes) {
    ASSERT(0, "hpvalloc");
    return NULL;
}

external inline void hfree(void *addr)    { hmalloc_free(addr); }

external inline int hposix_memalign(heap_handle_t h, void **memptr, size_t alignment, size_t size) {
    heap_t *heap;

    if (unlikely(!IS_POWER_OF_TWO(alignment)
    ||  alignment < sizeof(void*))) {
        return EINVAL;
    }

    heap    = get_user_heap(h);
    *memptr = heap_aligned_alloc(heap, size, alignment);

    if (unlikely(*memptr == NULL))    { return ENOMEM; }
    return 0;
}

external inline void * haligned_alloc(heap_handle_t h, size_t alignment, size_t size) {
    heap_t *heap;
    void   *addr;

    heap = get_user_heap(h);
    addr = heap_aligned_alloc(heap, size, alignment);

    return addr;
}

external inline void * hmemalign(heap_handle_t h, size_t alignment, size_t size) {
    return haligned_alloc(h, alignment, size);
}

size_t hmalloc_size(void *addr) {
    return hmalloc_malloc_size(addr);
}

size_t hmalloc_usable_size(void *addr) {
    return hmalloc_malloc_size(addr);
}


void * hmalloc_site_malloc(char *site, size_t n_bytes) {
    void *addr;

    /*
     * Have to make sure that we are initialized so that
     * hmalloc_site_layout has a proper value.
     */
    hmalloc_init();

    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_UNKNOWN) {
        return NULL;
    }
    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_SITE) {
        return hmalloc(site, n_bytes);
    }

    addr = hmalloc_malloc(n_bytes);

    return addr;
}

void * hmalloc_site_calloc(char *site, size_t count, size_t n_bytes) {
    void *addr;

    /*
     * Have to make sure that we are initialized so that
     * hmalloc_site_layout has a proper value.
     */
    hmalloc_init();

    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_UNKNOWN) {
        return NULL;
    }
    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_SITE) {
        return hcalloc(site, count, n_bytes);
    }

    addr = hmalloc_calloc(count, n_bytes);

    return addr;
}

void * hmalloc_site_realloc(char *site, void *addr, size_t n_bytes) {
    void *new_addr;

    /*
     * Have to make sure that we are initialized so that
     * hmalloc_site_layout has a proper value.
     */
    hmalloc_init();

    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_UNKNOWN) {
        return NULL;
    }
    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_SITE) {
        return hrealloc(site, addr, n_bytes);
    }

    new_addr = hmalloc_realloc(addr, n_bytes);

    return new_addr;
}

void * hmalloc_site_reallocf(char *site, void *addr, size_t n_bytes) {
    void *new_addr;

    /*
     * Have to make sure that we are initialized so that
     * hmalloc_site_layout has a proper value.
     */
    hmalloc_init();

    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_UNKNOWN) {
        return NULL;
    }
    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_SITE) {
        return hreallocf(site, addr, n_bytes);
    }

    new_addr = hmalloc_reallocf(addr, n_bytes);

    return new_addr;
}

void * hmalloc_site_valloc(char *site, size_t n_bytes) {
    void *addr;

    /*
     * Have to make sure that we are initialized so that
     * hmalloc_site_layout has a proper value.
     */
    hmalloc_init();

    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_UNKNOWN) {
        return NULL;
    }
    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_SITE) {
        return hvalloc(site, n_bytes);
    }

    addr = hmalloc_valloc(n_bytes);

    return addr;
}

void * hmalloc_site_pvalloc(char *site, size_t n_bytes) {
    void *addr;

    /*
     * Have to make sure that we are initialized so that
     * hmalloc_site_layout has a proper value.
     */
    hmalloc_init();

    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_UNKNOWN) {
        return NULL;
    }
    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_SITE) {
        return hpvalloc(site, n_bytes);
    }

    ASSERT(0, "hpvalloc");
    return NULL;
    (void) addr;
/*     addr = hmalloc_pvalloc(n_bytes); */

/*     return addr; */
}

void hmalloc_site_free(void *addr) {
    /*
     * Have to make sure that we are initialized so that
     * hmalloc_site_layout has a proper value.
     */
    hmalloc_init();

    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_UNKNOWN) {
        return;
    }
    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_SITE) {
        hfree(addr);
        return;
    }

    hmalloc_free(addr);
}

int hmalloc_site_posix_memalign(char *site, void **memptr, size_t alignment, size_t size) {
    int err;

    /*
     * Have to make sure that we are initialized so that
     * hmalloc_site_layout has a proper value.
     */
    hmalloc_init();

    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_UNKNOWN) {
        return EINVAL;
    }
    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_SITE) {
        return hposix_memalign(site, memptr, alignment, size);
    }

    err = hmalloc_posix_memalign(memptr, alignment, size);

    return err;
}

void * hmalloc_site_aligned_alloc(char *site, size_t alignment, size_t size) {
    void *addr;

    /*
     * Have to make sure that we are initialized so that
     * hmalloc_site_layout has a proper value.
     */
    hmalloc_init();

    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_UNKNOWN) {
        return NULL;
    }
    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_SITE) {
        return haligned_alloc(site, alignment, size);
    }

    addr = hmalloc_aligned_alloc(alignment, size);

    return addr;
}

void * hmalloc_site_memalign(char *site, size_t alignment, size_t size) {
    void *addr;

    /*
     * Have to make sure that we are initialized so that
     * hmalloc_site_layout has a proper value.
     */
    hmalloc_init();

    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_UNKNOWN) {
        return NULL;
    }
    if (hmalloc_site_layout == HMALLOC_SITE_LAYOUT_SITE) {
        return hmemalign(site, alignment, size);
    }

    addr = hmalloc_aligned_alloc(alignment, size);

    return addr;
}

size_t hmalloc_site_malloc_size(void *addr) {
    return hmalloc_size(addr);
}

size_t hmalloc_site_malloc_usable_size(void *addr) {
    return hmalloc_usable_size(addr);
}


external inline void * malloc(size_t n_bytes)               { return hmalloc_malloc(n_bytes);         }
external inline void * calloc(size_t count, size_t n_bytes) { return hmalloc_calloc(count, n_bytes);  }
external inline void * realloc(void *addr, size_t n_bytes)  { return hmalloc_realloc(addr, n_bytes);  }
external inline void * reallocf(void *addr, size_t n_bytes) { return hmalloc_reallocf(addr, n_bytes); }
external inline void * valloc(size_t n_bytes)               { return hmalloc_valloc(n_bytes);         }
external inline void * pvalloc(size_t n_bytes)              { ASSERT(0, "pvalloc"); return NULL;      }
external inline void   free(void *addr)                     { hmalloc_free(addr);                     }

external inline int posix_memalign(void **memptr, size_t alignment, size_t size) {
    return hmalloc_posix_memalign(memptr, alignment, size);
}

external inline void * aligned_alloc(size_t alignment, size_t size) {
    return hmalloc_aligned_alloc(alignment, size);
}

external inline void * memalign(size_t alignment, size_t size) {
    return hmalloc_aligned_alloc(alignment, size);
}

external inline size_t malloc_size(void *addr)        { return hmalloc_malloc_size(addr); }
external inline size_t malloc_usable_size(void *addr) { return hmalloc_malloc_size(addr); }
