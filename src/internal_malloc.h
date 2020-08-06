#ifndef __INTERNAL_MALLOC_H__
#define __INTERNAL_MALLOC_H__

#include "internal.h"
#include "locks.h"

internal mutex_t internal_malloc_lock = MUTEX_INITIALIZER;
#define IMALLOC_LOCK()   mutex_lock(&internal_malloc_lock)
#define IMALLOC_UNLOCK() mutex_unlock(&internal_malloc_lock)

typedef struct {
    int   initialized;
    void *start;
    void *end;
    void *bump_ptr;
} dumb_malloc_t;

internal dumb_malloc_t dumb_malloc_info;

internal void imalloc_init(void);

internal void * imalloc(u64 size);
internal void * icalloc(u64 n, u64 size);
internal void * irealloc(void *addr, u64 size);
internal void * ivalloc(u64 size);
internal void   ifree(void *addr);
internal int    iposix_memalign(void **memptr, u64 alignment, size_t n_bytes);
internal void * ialigned_alloc(u64 alignment, u64 size);

internal void *dumb_malloc(u64 size);
internal void dumb_free(void *addr);

#endif
