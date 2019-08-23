#ifndef __OS_H__
#define __OS_H__

#include "internal.h"

typedef struct {
    u64 page_size;
    u64 log_2_page_size;
} system_info_t;

internal system_info_t system_info;

internal void system_info_init(void);

internal void * get_pages_from_os(u32 n_pages);
internal void   release_pages_to_os(void *addr, u32 n_pages);

#endif
