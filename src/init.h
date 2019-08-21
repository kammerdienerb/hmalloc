#ifndef __INIT_H__
#define __INIT_H__

#include "internal.h"

#include <pthread.h>

internal pthread_mutex_t hmalloc_init_lock = PTHREAD_MUTEX_INITIALIZER;
#define INIT_LOCK()   do { pthread_mutex_lock(&hmalloc_init_lock);   } while (0)
#define INIT_UNLOCK() do { pthread_mutex_unlock(&hmalloc_init_lock); } while (0)


internal int hmalloc_is_initialized = 0;


__attribute__((constructor)) internal void hmalloc_init(void);
__attribute__((destructor))  internal void hmalloc_fini(void);

#endif
