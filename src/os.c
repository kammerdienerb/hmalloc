#include "os.h"
#include "heap.h"
#include "init.h"
#include "internal.h"

#include <sys/mman.h>
#if defined(__linux__)
#include <linux/mman.h> /* linux mmap flags */
#endif


internal void system_info_init(void) {
    i64 page_size;

    ASSERT(hmalloc_is_initialized, "premature system_info_init()");

    page_size = sysconf(_SC_PAGE_SIZE);
    ASSERT(page_size > (sizeof(chunk_header_t) + sizeof(block_header_t)),
           "invalid page size");
    ASSERT(IS_POWER_OF_TWO(page_size),
           "invalid page size -- must be a power of two");

    system_info.page_size       = page_size;
    system_info.log_2_page_size = LOG2_64BIT(page_size);

    LOG("page_size:          %lu\n", system_info.page_size);
    LOG("MAX_SMALL_CHUNK:    %lu\n", MAX_SMALL_CHUNK);
    LOG("MAX_BIG_CHUNK:      %lu\n", MAX_BIG_CHUNK);
    LOG("DEFAULT_BLOCK_SIZE: %lu\n", DEFAULT_BLOCK_SIZE);

    LOG("initialized system info\n");
}

internal void * get_pages_from_os(u32 n_pages) {
    void *addr;

    addr = mmap(NULL,
                n_pages * system_info.page_size,
                PROT_READ   | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS,
                -1,
                (off_t)0);

    if (addr == MAP_FAILED) {
        LOG("ERROR -- could not get %u pages (%llu bytes) from OS\n", n_pages, n_pages * system_info.page_size);
        return NULL;
    }

    return addr;
}

internal void release_pages_to_os(void *addr, u32 n_pages) {
    int err_code;

    err_code = munmap(addr, n_pages << system_info.log_2_page_size);

    ASSERT(err_code == 0, "munmap() failed!");

    (void)err_code;
}

