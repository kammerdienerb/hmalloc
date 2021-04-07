#ifndef __INIT_H__
#define __INIT_H__

#include "internal.h"

#include <pthread.h>

internal pthread_mutex_t hmalloc_init_lock = PTHREAD_MUTEX_INITIALIZER;
#define INIT_LOCK()   do { pthread_mutex_lock(&hmalloc_init_lock);   } while (0)
#define INIT_UNLOCK() do { pthread_mutex_unlock(&hmalloc_init_lock); } while (0)


#define HMALLOC_SITE_LAYOUT_UNKNOWN   (0)
#define HMALLOC_SITE_LAYOUT_THREAD    (1)
#define HMALLOC_SITE_LAYOUT_SITE      (2)

#define HMALLOC_MSG_MODE_UNKNOWN   (0)
#define HMALLOC_MSG_MODE_OBJECT    (1)
#define HMALLOC_MSG_MODE_USER_HEAP (2)

internal int hmalloc_is_initialized = 0;
internal int hmalloc_ignore_frees   = 0;
internal int hmalloc_site_layout    = HMALLOC_SITE_LAYOUT_UNKNOWN;
internal int hmalloc_msg_mode       = HMALLOC_MSG_MODE_UNKNOWN;


internal void hmalloc_init(void);
internal void hmalloc_fini(void);

#endif
