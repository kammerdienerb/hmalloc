#include "hmalloc.h"
#include "internal.h"

#include "FormatString.c"

#include "heap.c"
#include "thread.c"
#include "os.c"
#include "init.c"
#include "internal.h"

#include <string.h>
#include <errno.h>

external void *hmalloc_malloc(size_t n_bytes) {
    thread_local_data_t *local;

    local = get_thread_local_struct();
    void * p = heap_alloc(&local->heap, n_bytes);
    return p;
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
    hmalloc_free(addr);
    return hmalloc_malloc(n_bytes);
}
external void * hmalloc_reallocf(void *addr, size_t n_bytes) { return hmalloc_realloc(addr, n_bytes); }

external void * hmalloc_valloc(size_t n_bytes) {
    thread_local_data_t *local;
    
    local = get_thread_local_struct();

    return heap_aligned_alloc(&local->heap, n_bytes, system_info.page_size);
}

external void hmalloc_free(void *addr) {
    chunk_header_t      *chunk;
    thread_local_data_t *thread_data;
    
    if (addr == NULL) {
        return;
    }

    chunk = CHUNK_FROM_USER_MEM(addr);

    thread_data = thread_local_datas + chunk->thread_idx;

    heap_free(&thread_data->heap, chunk);
}

external int hmalloc_posix_memalign(void **memptr, size_t alignment, size_t n_bytes) {
    thread_local_data_t *local;
  
    if (!IS_POWER_OF_TWO(alignment)
    ||  alignment < sizeof(void*)) {
        return EINVAL;
    }

    local = get_thread_local_struct();

    *memptr = heap_aligned_alloc(&local->heap, n_bytes, alignment);

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
external void * pvalloc(size_t n_bytes) { ASSERT(0, "pvalloc"); return NULL; }
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

