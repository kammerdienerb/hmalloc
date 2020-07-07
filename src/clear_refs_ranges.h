#ifndef __CLEAR_REFS_RANGES_H__
#define __CLEAR_REFS_RANGES_H__

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

struct crr_t {
    int          fd;
    pid_t        pid;
    unsigned int page_size;
};

int crr_open(struct crr_t *crr);
int crr_close(struct crr_t *crr);
int crr_range(struct crr_t *crr, void *start, void *end);
int crr_range_immediate(void *start, void *end);

#endif


#ifdef CLEAR_REFS_RANGES_IMPL

/*
 * The following definitions MUST match the ones in
 * the kernel module (clear_refs_ranges.c).
 */
#define CLEAR_REFS_MAGIC (0xC1EA77EF)

__attribute__((packed))
struct clear_refs_ranges_info {
    unsigned int  _magic;
    pid_t         pid;
    void         *range_start;
    void         *range_end;
};




static int open_proc_fs_entry(void) {
    int fd;

    errno = 0;

    fd = open("/proc/clear_refs_ranges", O_WRONLY);

    if (fd == -1) {
        fd = -errno;
    }

    errno = 0;

    return fd;
}

static int write_clear_refs_ranges_info(int fd, struct clear_refs_ranges_info *info) {
    int status;

    errno  = 0;
    status = write(fd, info, sizeof(*info));

    if (status == -1) {
        status = -errno;
    }

    errno = 0;

    return status;
}

int crr_open(struct crr_t *crr) {
    int status;

    status = open_proc_fs_entry();
    if (status >= 0) {
        crr->fd        = status;
        crr->pid       = getpid();
        crr->page_size = sysconf(_SC_PAGE_SIZE);
    }

    return status;
}

int crr_close(struct crr_t *crr) {
    int status;

    status = close(crr->fd);
    memset(crr, 0, sizeof(*crr));

    return status;
}

int crr_range(struct crr_t *crr, void *start, void *end) {
    struct clear_refs_ranges_info info;
    int                           status;

    status = 0;

    if (end <= start) {
        status = EINVAL;
        goto out;
    }

    if (((unsigned long long)start) & (crr->page_size - 1)
    ||  ((unsigned long long)end)   & (crr->page_size - 1)) {
        status = EINVAL;
        goto out;
    }

    info._magic      = CLEAR_REFS_MAGIC;
    info.pid         = crr->pid;
    info.range_start = start;
    info.range_end   = end;

    status = write_clear_refs_ranges_info(crr->fd, &info);

out:
    return status;
}

int crr_range_immediate(void *start, void *end) {
    struct crr_t crr;
    int          status;

    status = crr_open(&crr);
    if (status < 0) { goto out; }

    status = crr_range(&crr, start, end);
    if (status < 0) {
        crr_close(&crr);
        goto out;
    }

    status = crr_close(&crr);

out:
    return status;
}

#endif
