#include "hmalloc.h"

#include "heap.c"
#include "thread.c"

#include <string.h>

__attribute__((constructor))
internal void hmalloc_init(void) {
    system_info_init();
    thread_local_init();
}

__attribute__((destructor))
internal void hmalloc_fini(void) {
    thread_local_fini();
}

external void *hmalloc_malloc(size_t n_bytes) {
    thread_local_data_t *local;

    local = get_thread_local_struct();
    return heap_alloc(&local->heap, n_bytes);
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
    return heap_valloc(&local->heap, n_bytes);
}

external void hmalloc_free(void *addr) {
    chunk_header_t      *chunk;
    thread_local_data_t *thread_data;
    
    if (addr == NULL) {
        return;
    }

    chunk = CHUNK_FROM_USER_MEM(addr);

    thread_data = thread_local_datas + chunk->thread_id;

    heap_free(&thread_data->heap, chunk);
}

external void * malloc(size_t n_bytes)               { return hmalloc_malloc(n_bytes);         }
external void * calloc(size_t count, size_t n_bytes) { return hmalloc_calloc(count, n_bytes);  }
external void * realloc(void *addr, size_t n_bytes)  { return hmalloc_realloc(addr, n_bytes);  }
external void * reallocf(void *addr, size_t n_bytes) { return hmalloc_reallocf(addr, n_bytes); }
external void * valloc(size_t n_bytes)               { return hmalloc_valloc(n_bytes);         }
external void   free(void *addr)                     { hmalloc_free(addr);                     }
