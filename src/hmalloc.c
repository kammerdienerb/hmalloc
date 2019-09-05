#define _GNU_SOURCE

#include "hmalloc.h"
#include "internal.h"

#include "FormatString.c"
#include "internal.c"
#include "heap.c"
#include "thread.c"
#include "os.c"
#include "init.c"
#include "internal.h"

#include <string.h>
#include <errno.h>

external void *hmalloc_malloc(size_t n_bytes) {
    thread_data_t *thr;
    void          *addr;

    thr  = acquire_this_thread();
    addr = heap_alloc(&thr->heap, n_bytes);
    release_thread(thr);

    return addr;
}

external void * hmalloc_calloc(size_t count, size_t n_bytes) {
    void *addr;
    u64   new_n_bytes;

    new_n_bytes = count * n_bytes;
    addr        = hmalloc_malloc(new_n_bytes);

    memset(addr, 0, new_n_bytes);
    
    return addr;
}

external void * hmalloc_realloc(void *addr, size_t n_bytes) {
    void *new_addr;

    new_addr = NULL;

    if (addr == NULL) {
        new_addr = hmalloc_malloc(n_bytes);
    } else {
        if (likely(n_bytes > 0)) {
            /*
             * This is done for us in heap_alloc, but we'll
             * need the aligned value when we get the copy length.
             */
            n_bytes  = ALIGN(n_bytes, 8);
            new_addr = hmalloc_malloc(n_bytes);

            memcpy(new_addr, addr,
                   MIN(hmalloc_malloc_size(addr), n_bytes));
        }

        hmalloc_free(addr);
    }

    return new_addr;
}
external void * hmalloc_reallocf(void *addr, size_t n_bytes) { return hmalloc_realloc(addr, n_bytes); }

external void * hmalloc_valloc(size_t n_bytes) {
    thread_data_t *thr;
    void          *addr;
    
    thr  = acquire_this_thread();
    addr = heap_aligned_alloc(&thr->heap, n_bytes, system_info.page_size);
    release_thread(thr);

    return addr;
}

external void hmalloc_free(void *addr) {
    thread_data_t  *thr;
    block_header_t *block;

    if (unlikely(addr == NULL)) {
        return;
    }
    
    block = ADDR_PARENT_BLOCK(addr);

    thr = acquire_thread(block->tid);
    heap_free(&thr->heap, addr);
    release_thread(thr);
}

external int hmalloc_posix_memalign(void **memptr, size_t alignment, size_t n_bytes) {
    thread_data_t *thr;
  
    if (unlikely(!IS_POWER_OF_TWO(alignment)
    ||  alignment < sizeof(void*))) {
        return EINVAL;
    }

    thr     = acquire_this_thread();
    *memptr = heap_aligned_alloc(&thr->heap, n_bytes, alignment);
    release_thread(thr);

    if (unlikely(*memptr == NULL))    { return ENOMEM; }
    return 0;
}

external size_t hmalloc_malloc_size(void *addr) {
    block_header_t *block;

    if (unlikely(addr == NULL)) {
        return 0;
    }

    /* 
     * @incomplete
     * What about big allocs???
     */

    block = ADDR_PARENT_BLOCK(addr);
    
    if (likely(block->block_kind == BLOCK_KIND_CBLOCK)) {
        return CHUNK_SIZE(CHUNK_FROM_USER_MEM(addr));
    } else if (likely(block->block_kind == BLOCK_KIND_SBLOCK)) {
        return SBLOCK_SLOT_SIZE;
    }

    ASSERT(0, "couldn't determine size of allocation");

    return 0;
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
    ASSERT(0, "aligned_alloc");
    return NULL;
}

external void * memalign(size_t alignment, size_t size) {
    ASSERT(0, "memalign");
    return NULL;
}

external size_t malloc_size(void *addr)        { return hmalloc_malloc_size(addr); }
external size_t malloc_usable_size(void *addr) { return hmalloc_malloc_size(addr); }

