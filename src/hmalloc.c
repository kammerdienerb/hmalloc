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
    u64   old_size,
          copy_size;

    n_bytes += (n_bytes == 0);

    new_addr = hmalloc_malloc(n_bytes);

    if (addr != NULL) {
        old_size  = hmalloc_malloc_size(addr);
        copy_size = old_size;
        if (n_bytes < copy_size) {
            copy_size = n_bytes;
        }
        memcpy(new_addr, addr, copy_size);
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
    chunk_header_t *chunk;
    thread_data_t  *thr;

    if (addr == NULL) {
        return;
    }
    
    chunk = CHUNK_FROM_USER_MEM(addr);

    thr = acquire_thread(chunk->tid);
    heap_free(&thr->heap, chunk);
    release_thread(thr);
}

external int hmalloc_posix_memalign(void **memptr, size_t alignment, size_t n_bytes) {
    thread_data_t *thr;
  
    if (!IS_POWER_OF_TWO(alignment)
    ||  alignment < sizeof(void*)) {
        return EINVAL;
    }

    thr     = acquire_this_thread();
    *memptr = heap_aligned_alloc(&thr->heap, n_bytes, alignment);
    release_thread(thr);

    if (*memptr == NULL)    { return ENOMEM; }
    return 0;
}

external size_t hmalloc_malloc_size(void *addr) {
    /* 
     * @incomplete
     * What about big allocs???
     */
    return CHUNK_FROM_USER_MEM(addr)->size;
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

