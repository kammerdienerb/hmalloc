#include "internal_malloc.h"
#include "os.h"

#include <string.h>

internal int use_dumb_malloc = 0;

internal void imalloc_init(void) {
    char *dlerr;

    (void)dlerr;

    dumb_malloc_info.page     = get_pages_from_os(2, 8);
    dumb_malloc_info.bump_ptr = dumb_malloc_info.page;
    LOG("initialized dumb malloc\n");

    use_dumb_malloc = 1;

    libc_malloc = dlsym(RTLD_NEXT, "malloc");
    if (!libc_malloc) {
        dlerr = dlerror();
        LOG("%s\n", dlerr);
        ASSERT(0, "could not load libc_malloc");
    }

    libc_calloc = dlsym(RTLD_NEXT, "calloc");
    if (!libc_calloc) {
        dlerr = dlerror();
        LOG("%s\n", dlerr);
        ASSERT(0, "could not load libc_calloc");
    }

    libc_realloc = dlsym(RTLD_NEXT, "realloc");
    if (!libc_realloc) {
        dlerr = dlerror();
        LOG("%s\n", dlerr);
        ASSERT(0, "could not load libc_realloc");
    }

    libc_valloc = dlsym(RTLD_NEXT, "valloc");
    if (!libc_valloc) {
        dlerr = dlerror();
        LOG("%s\n", dlerr);
        ASSERT(0, "could not load libc_valloc");
    }

    libc_free   = dlsym(RTLD_NEXT, "free");
    if (!libc_free) {
        dlerr = dlerror();
        LOG("%s\n", dlerr);
        ASSERT(0, "could not load libc_free");
    }

    libc_posix_memalign = dlsym(RTLD_NEXT, "posix_memalign");
    if (!libc_posix_memalign) {
        dlerr = dlerror();
        LOG("%s\n", dlerr);
        ASSERT(0, "could not load libc_posix_memalign");
    }

    libc_aligned_alloc = dlsym(RTLD_NEXT, "aligned_alloc");
    if (!libc_aligned_alloc) {
        dlerr = dlerror();
        LOG("%s\n", dlerr);
        ASSERT(0, "could not load libc_aligned_alloc");
    }

    libc_malloc_size = dlsym(RTLD_NEXT, "malloc_size");
    if (!libc_malloc_size) {
        libc_malloc_size = dlsym(RTLD_NEXT, "malloc_usable_size");
        if (!libc_malloc_size) {
            dlerr = dlerror();
            LOG("%s\n", dlerr);
            ASSERT(0, "could not load libc_malloc_size");
        }
    }

    use_dumb_malloc = 0;
    LOG("initialized imalloc\n");
}

internal void * imalloc(u64 size) {
    if (use_dumb_malloc) {
        return dumb_malloc(size);
    }

    ASSERT(libc_malloc, "missing internal malloc");
    return libc_malloc(size);
}

internal void * icalloc(u64 n, u64 size) {
    void *addr;

    if (use_dumb_malloc) {
        addr = dumb_malloc(n * size);
        memset(addr, 0, n * size);
        return addr;
    }

    ASSERT(libc_calloc, "missing internal calloc");
    return libc_calloc(n, size);
}

internal void * irealloc(void *addr, u64 size) {
    void *new_addr;
    if (use_dumb_malloc) {
        new_addr = dumb_malloc(size);
        memcpy(new_addr, addr, size);
        return new_addr;
    }
    ASSERT(libc_realloc, "missing internal realloc");
    return libc_realloc(addr, size);
}

internal void * ivalloc(u64 size) {
    ASSERT(!use_dumb_malloc, "not supported with dumb_malloc");
    ASSERT(libc_valloc, "missing internal valloc");
    return libc_valloc(size);
}

internal void ifree(void *addr) {
    if (use_dumb_malloc) {
        dumb_free(addr);
        return;
    }

    ASSERT(libc_free, "missing internal free");
    libc_free(addr);
    return;
}

internal int iposix_memalign(void **memptr, u64 alignment, size_t n_bytes) {
    ASSERT(!use_dumb_malloc, "not supported with dumb_malloc");
    ASSERT(libc_posix_memalign, "missing internal posix_memalign");
    return libc_posix_memalign(memptr, alignment, n_bytes);
}

internal void * ialigned_alloc(u64 alignment, u64 size) {
    ASSERT(!use_dumb_malloc, "not supported with dumb_malloc");
    ASSERT(libc_posix_memalign, "missing internal aligned_alloc");
    return libc_aligned_alloc(alignment, size);
}

internal size_t imalloc_size(void *addr) {
    ASSERT(!use_dumb_malloc, "not supported with dumb_malloc");
    ASSERT(libc_malloc_size, "missing internal malloc_size");
    return libc_malloc_size(addr);
}

internal void *dumb_malloc(u64 size) {
    void *addr;

IMALLOC_LOCK(); {

    ASSERT(dumb_malloc_info.bump_ptr + size < dumb_malloc_info.page + system_info.page_size,
           "dumb_malloc unable to service request");

    LOG("dumb allocation of %lu bytes\n", size);
    addr                       = dumb_malloc_info.bump_ptr;
    dumb_malloc_info.bump_ptr += size;

} IMALLOC_UNLOCK();

    return addr;
}

internal void dumb_free(void *addr) { /* Ignore. */ }
