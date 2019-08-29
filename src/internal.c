#include "internal.h"

#include <pthread.h>

internal pthread_mutex_t log_mtx = PTHREAD_MUTEX_INITIALIZER;

internal void hmalloc_putc(char c, void *context) {
    (void)context;
    write(2, &c, 1);
}
internal void hmalloc_printf(const char *fmt, ...) {
    va_list va;

    pthread_mutex_lock(&log_mtx);
    
    va_start(va, fmt);
    FormatString(hmalloc_putc, NULL, fmt, va);
    va_end(va);
    
    pthread_mutex_unlock(&log_mtx);
}

#ifdef HMALLOC_DO_ASSERTIONS
internal void hmalloc_assert_fail(const char *msg, const char *fname, int line, const char *cond_str) {
    volatile int *trap;

    hmalloc_printf("Assertion failed -- %s\n"
                   "at  %s :: line %d\n"
                   "    Condition: '%s'\n"
                   , msg, fname, line, cond_str);
    
    trap = 0;
    (void)*trap;
}
#endif
