#include "internal_malloc.h"
#include "os.h"

#include <string.h>

internal inline void imalloc_init(void) {
    u64 size;
    u64 pages;

    /* @bad @todo
     * Since user heaps use a tree which allocates using this internal inline
     * allocator, we should be able to expand this storage at runtime.
     */
    size  = KB(512);
    pages = size >> system_info.log_2_page_size;

    dumb_malloc_info.start    = get_pages_from_os(pages, system_info.page_size);
    dumb_malloc_info.end      = dumb_malloc_info.start + size;
    dumb_malloc_info.bump_ptr = dumb_malloc_info.start;

    LOG("initialized imalloc\n");
}

internal inline void * imalloc(u64 size) {
    return dumb_malloc(size);
}

internal inline void * icalloc(u64 n, u64 size) {
    void *addr;

    addr = dumb_malloc(n * size);
    memset(addr, 0, n * size);
    return addr;
}

internal inline void * irealloc(void *addr, u64 size) {
    void *new_addr;

    new_addr = dumb_malloc(size);
    memcpy(new_addr, addr, size);
    return new_addr;
}

internal inline void * ivalloc(u64 size) {
    ASSERT(0, "not supported with dumb_malloc");
    return NULL;
}

internal inline void ifree(void *addr) {
    dumb_free(addr);
    return;
}

internal inline int iposix_memalign(void **memptr, u64 alignment, size_t n_bytes) {
    ASSERT(0, "not supported with dumb_malloc");
    return 0;
}

internal inline void * ialigned_alloc(u64 alignment, u64 size) {
    ASSERT(0, "not supported with dumb_malloc");
    return NULL;
}

internal inline size_t imalloc_size(void *addr) {
    ASSERT(0, "not supported with dumb_malloc");
    return 0;
}

internal inline void *dumb_malloc(u64 size) {
    void *addr;

IMALLOC_LOCK(); {

    ASSERT(dumb_malloc_info.bump_ptr + size < dumb_malloc_info.end,
           "dumb_malloc unable to service request");

    LOG("dumb allocation of %lu bytes\n", size);
    addr                       = dumb_malloc_info.bump_ptr;
    dumb_malloc_info.bump_ptr += size;

} IMALLOC_UNLOCK();

    return addr;
}

internal inline void dumb_free(void *addr) { /* Ignore. */ }
