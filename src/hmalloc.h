#ifndef __HMALLOC_H__
#define __HMALLOC_H__

#include <stddef.h>

void * hmalloc_malloc(size_t n_bytes);
void * hmalloc_calloc(size_t count, size_t n_bytes);
void * hmalloc_realloc(void *addr, size_t n_bytes);
void * hmalloc_reallocf(void *addr, size_t n_bytes);
void * hmalloc_valloc(size_t n_bytes);
void   hmalloc_free(void *addr);

void * malloc(size_t n_bytes);
void * calloc(size_t count, size_t n_bytes);
void * realloc(void *addr, size_t n_bytes);
void * reallocf(void *addr, size_t n_bytes);
void * valloc(size_t n_bytes);
void   free(void *addr);

#endif
