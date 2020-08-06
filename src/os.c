#include "os.h"
#include "heap.h"
#include "init.h"
#include "internal.h"

#include <sys/mman.h>
#if defined(__linux__)
#include <linux/mman.h> /* linux mmap flags */
#endif

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>


internal void system_info_init(void) {
    i64 page_size;

    page_size = sysconf(_SC_PAGE_SIZE);
    ASSERT(page_size > (sizeof(chunk_header_t) + sizeof(block_header_t)),
           "invalid page size");
    ASSERT(IS_POWER_OF_TWO(page_size),
           "invalid page size -- must be a power of two");

    system_info.page_size       = page_size;
    system_info.log_2_page_size = LOG2_64BIT(page_size);

    LOG("page_size:          %lu\n", system_info.page_size);
    LOG("MAX_SMALL_CHUNK:    %llu\n", MAX_SMALL_CHUNK);
    LOG("DEFAULT_BLOCK_SIZE: %llu\n", DEFAULT_BLOCK_SIZE);

    LOG("initialized system info\n");
}

HMALLOC_ALWAYS_INLINE
void * get_pages_from_os(u64 n_pages, u64 alignment) {
    void *aligned_start,
         *aligned_end,
         *mem_start,
         *mem_end;
    u64   desired_size,
          first_map_size;

    ASSERT(n_pages > 0, "n_pages is zero");

    desired_size = (n_pages << system_info.log_2_page_size);

    /*
     * Ask for twice the desired size so that we can get aligned
     * memory.
     */
    first_map_size = MAX(desired_size, alignment) << 1ULL;

    errno = 0;
    mem_start = mmap(NULL,
                first_map_size,
                PROT_READ   | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS,
                -1,
                (off_t)0);

    if (unlikely(mem_start == MAP_FAILED || mem_start == NULL)) {
        LOG("ERROR -- could not get %lu pages (%lu bytes) from OS\n", n_pages, desired_size);
        ASSERT(0, "mmap() failed");
        return NULL;
    }
    ASSERT(errno == 0, "errno non-zero after mmap()");

    aligned_start = ALIGN(mem_start, alignment);
    aligned_end   = aligned_start + desired_size;
    mem_end       = mem_start + first_map_size;

    /*
     * Trim off the edges we don't need.
     */
    if (mem_start != aligned_start) {
        munmap(mem_start, aligned_start - mem_start);
    }
    if (mem_end != aligned_end) {
        munmap(aligned_end, mem_end - aligned_end);
    }

    return aligned_start;
}

HMALLOC_ALWAYS_INLINE
void release_pages_to_os(void *addr, u64 n_pages) {
    int err_code;

    ASSERT(n_pages > 0, "n_pages is zero");

    err_code = munmap(addr, n_pages << system_info.log_2_page_size);

    ASSERT(err_code == 0, "munmap() failed!");

    (void)err_code;
}

HMALLOC_ALWAYS_INLINE
pid_t os_get_tid(void) {
    pid_t tid;

    tid = syscall(SYS_gettid);
    ASSERT(tid != -1, "did not get tid");

    return tid;
}
