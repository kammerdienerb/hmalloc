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

internal void * get_pages_from_os(u64 n_pages, u64 alignment) {
    void *aligned_start,
         *aligned_end,
         *mem_start,
         *mem_end;
    u64   desired_size,
          first_map_size;

    ASSERT(n_pages > 0, "n_pages is zero");

    desired_size = (n_pages << system_info.log_2_page_size);

    ASSERT(desired_size >= alignment, "alignment greater than desired memory size");

    /*
     * Ask for twice the desired size so that we can get aligned
     * memory.
     */
    first_map_size = desired_size << 1ULL;

    mem_start = mmap(NULL,
                first_map_size,
                PROT_READ   | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS,
                -1,
                (off_t)0);

    if (unlikely(mem_start == MAP_FAILED || mem_start == NULL)) {
        LOG("ERROR -- could not get %lu pages (%lu bytes) from OS\n", n_pages, desired_size);
        return NULL;
    }

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

internal void release_pages_to_os(void *addr, u64 n_pages) {
    int err_code;

    ASSERT(n_pages > 0, "n_pages is zero");

    err_code = munmap(addr, n_pages << system_info.log_2_page_size);

    ASSERT(err_code == 0, "munmap() failed!");

    (void)err_code;
}

__thread int thr_handle;

internal pid_t os_get_tid(void) {
    /* pid_t tid; */

    /*
     * Use the address of a thread-local storage
     * variable to give us a value that changes per
     * thread.
     *
     * We shift by eleven because that number seemed to
     * align with what the addresses were multiples of
     * on the test OS.
     *
     * Should be pretty fast.
     */
    /* tid = (pid_t)((u64)&thr_handle >> 11); */

    /*
     * This method is slower because of the system call
     * overhead, but it gives us less collisions..
     */
    return syscall(SYS_gettid);

    /* return tid; */
}
