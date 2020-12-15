#include "kernel_objmap.h"

static struct proc_object_map_t proc_object_map;

internal void kernel_objmap_init(void) {
    int status;

    status = objmap_open(&proc_object_map);

    if (status < 0) {
        LOG("there was an error opening the proc object_map (%d)\n", status);
    } else {
        LOG("initialized kernel proc object_map interface\n");
    }
}

internal void kernel_objmap_add_object(void *obj, u64 len) {
    int status;

    status = objmap_add_range(&proc_object_map, obj, obj + len);

    if (status < 0) {
        LOG("there was an error adding to the proc object_map (%d)\n", status);
    } else {
        LOG("added range 0x%lx-0x%lx\n", obj, obj + len);
    }
}

internal void kernel_objmap_del_object(void *obj) {
    int status;

    status = objmap_del_range(&proc_object_map, obj);

    if (status < 0) {
        LOG("there was an error removing from the proc object_map (%d)\n", status);
    } else {
        LOG("removed the range starting at 0x%lx\n", obj);
    }
}

internal void kernel_objmap_write_site(void *obj, u64 obj_size, char *site) {
    int  len;
    char path[4096];
    int  err;

    if (site == NULL) { return; }

    len = strlen(site);

    snprintf(path, sizeof(path),
             "/proc/%d/object_map/%lx-%lx",
             proc_object_map.pid, (unsigned long)obj, (unsigned long)(obj + obj_size));

    err = objmap_entry_write_record_coop_buff(path, site, len);

    if (err) {
        LOG("failed to write site to coop buff -- error %d '%s'\n", err, path);
    }
}
