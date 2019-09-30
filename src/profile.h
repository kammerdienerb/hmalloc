#ifndef __PROFILE_H__
#define __PROFILE_H__

#include "internal.h"

#include <pthread.h>


internal i32 doing_profiling;

typedef struct {
    i32 thread_started;
} profile_data;

internal profile_data prof_data;

internal void profile_init(void);

#endif
