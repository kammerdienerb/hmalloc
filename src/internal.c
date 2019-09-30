#include "internal.h"

#include <pthread.h>
#include <errno.h>

internal pthread_mutex_t log_mtx = PTHREAD_MUTEX_INITIALIZER;

internal void hmalloc_putc(char c, void *fd) {
    write((int)(i64)fd, &c, 1);
}
internal void hmalloc_printf(int fd, const char *fmt, ...) {
    va_list va;

    LOG_LOCK(); {
        va_start(va, fmt);
        FormatString(hmalloc_putc, (void*)(i64)fd, fmt, va);
        va_end(va);
    } LOG_UNLOCK();
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
