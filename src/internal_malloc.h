#ifndef __INTERNAL_MALLOC_H__
#define __INTERNAL_MALLOC_H__

#include "internal.h"

#include <pthread.h>

internal pthread_mutex_t internal_malloc_lock = PTHREAD_MUTEX_INITIALIZER;
#define IMALLOC_LOCK()   HMALLOC_MTX_LOCKER(&internal_malloc_lock)
#define IMALLOC_UNLOCK() HMALLOC_MTX_UNLOCKER(&internal_malloc_lock)

typedef struct {
    int   initialized;
    void *page;
    void *bump_ptr;
} dumb_malloc_t;

internal dumb_malloc_t dumb_malloc_info;
internal void  *(*libc_malloc)(size_t);
internal void  *(*libc_calloc)(size_t, size_t);
internal void  *(*libc_realloc)(void*, size_t);
internal void  *(*libc_valloc)(size_t);
internal void   (*libc_free)(void*);
internal int    (*libc_posix_memalign)(void**, size_t, size_t);
internal void  *(*libc_aligned_alloc)(size_t, size_t);
internal size_t (*libc_malloc_size)(void*);

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
