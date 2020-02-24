#include "internal.h"
#include "internal_malloc.h"

#include <errno.h>
#include <string.h>

internal void hmalloc_putc(char c, void *fd) {
    write((int)(i64)fd, &c, 1);
}
internal void hmalloc_printf(int fd, const char *fmt, ...) {
    va_list va;

    va_start(va, fmt);

    FormatString(hmalloc_putc, (void*)(i64)fd, fmt, va);

    va_end(va);
}

#ifdef HMALLOC_DO_LOGGING
void log_init(void) {
    int fd;

    fd = open("hmalloc.log", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

    ASSERT(fd != -1, "could not open log file");

    log_fd = fd;

    LOG("intialized logging to 'hmalloc.log'\n");
}
#endif

#ifdef HMALLOC_DO_ASSERTIONS
internal void hmalloc_assert_fail(const char *msg, const char *fname, int line, const char *cond_str) {
    volatile int *trap;

    hmalloc_printf(2, "Assertion failed -- %s\n"
                   "at  %s :: line %d\n"
                   "    Condition: '%s'\n"
                   , msg, fname, line, cond_str);

    trap = 0;
    (void)*trap;
}
#endif


internal u64 next_power_of_2(u64 x) {
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;
    x++;
    return x;
}

internal char * istrdup(char *s) {
    int   len;
    char *out;

    len = strlen(s);
    out = imalloc(len + 1);
    memcpy(out, s, len + 1);

    return out;
}
