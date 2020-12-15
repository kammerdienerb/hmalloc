#ifndef __KERNEL_OBJMAP_H__
#define __KERNEL_OBJMAP_H__

#include "internal.h"
#include "proc_object_map.h"

internal void kernel_objmap_init(void);
internal void kernel_objmap_add_object(void *obj, u64 len);
internal void kernel_objmap_del_object(void *obj);
internal void kernel_objmap_write_site(void *obj, u64 obj_size, char *site);

#endif
