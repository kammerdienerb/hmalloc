#ifndef __HMALLOC_H__
#define __HMALLOC_H__

#include <stddef.h>

typedef char *heap_handle_t;

void * hmalloc_malloc(size_t n_bytes);
void * hmalloc_calloc(size_t count, size_t n_bytes);
void * hmalloc_realloc(void *addr, size_t n_bytes);
void * hmalloc_reallocf(void *addr, size_t n_bytes);
void * hmalloc_valloc(size_t n_bytes);
void   hmalloc_free(void *addr);
int    hmalloc_posix_memalign(void **memptr, size_t alignment, size_t n_bytes);
void * hmalloc_aligned_alloc(size_t alignment, size_t size);
size_t hmalloc_malloc_size(void *addr);


void * hmalloc(heap_handle_t h, size_t n_bytes);
void * hcalloc(heap_handle_t h, size_t count, size_t n_bytes);
void * hrealloc(heap_handle_t h, void *addr, size_t n_bytes);
void * hreallocf(heap_handle_t h, void *addr, size_t n_bytes);
void * hvalloc(heap_handle_t h, size_t n_bytes);
void * hpvalloc(heap_handle_t h, size_t n_bytes);
void   hfree(void *addr);
int    hposix_memalign(heap_handle_t h, void **memptr, size_t alignment, size_t size);
void * haligned_alloc(heap_handle_t h, size_t alignment, size_t size);
void * hmemalign(heap_handle_t h, size_t alignment, size_t size);
size_t hmalloc_size(void *addr);
size_t hmalloc_usable_size(void *addr);

void * hmalloc_site_malloc(char *site, size_t n_bytes);
void * hmalloc_site_calloc(char *site, size_t count, size_t n_bytes);
void * hmalloc_site_realloc(char *site, void *addr, size_t n_bytes);
void * hmalloc_site_reallocf(char *site, void *addr, size_t n_bytes);
void * hmalloc_site_valloc(char *site, size_t n_bytes);
void * hmalloc_site_pvalloc(char *site, size_t n_bytes);
void   hmalloc_site_free(void *addr);
int    hmalloc_site_posix_memalign(char *site, void **memptr, size_t alignment, size_t size);
void * hmalloc_site_aligned_alloc(char *site, size_t alignment, size_t size);
void * hmalloc_site_memalign(char *site, size_t alignment, size_t size);
size_t hmalloc_site_malloc_size(void *addr);
size_t hmalloc_site_malloc_usable_size(void *addr);

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
