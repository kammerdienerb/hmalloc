#ifndef __HMALLOC_H__
#define __HMALLOC_H__

#include <stddef.h>

void * hmalloc_malloc(size_t n_bytes);
void * hmalloc_calloc(size_t count, size_t n_bytes);
void * hmalloc_realloc(void *addr, size_t n_bytes);
void * hmalloc_reallocf(void *addr, size_t n_bytes);
void * hmalloc_valloc(size_t n_bytes);
void   hmalloc_free(void *addr);
int    hmalloc_posix_memalign(void **memptr, size_t alignment, size_t n_bytes);
size_t hmalloc_malloc_size(void *addr);

void * malloc(size_t n_bytes);
void * calloc(size_t count, size_t n_bytes);
void * realloc(void *addr, size_t n_bytes);
void * reallocf(void *addr, size_t n_bytes);
void * valloc(size_t n_bytes);
void * pvalloc(size_t n_bytes);
void   free(void *addr);
int    posix_memalign(void **memptr, size_t alignment, size_t size);
void * aligned_alloc(size_t alignment, size_t size);
void * memalign(size_t alignment, size_t size);
size_t malloc_size(void *addr);
size_t malloc_usable_size(void *addr);

#endif
