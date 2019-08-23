#include "internal.h"

internal void hmalloc_putc(char c, void *context) {
    (void)context;
    write(2, &c, 1);
}
internal void hmalloc_printf(const char *fmt, ...) {
    va_list va;
    
    va_start(va, fmt);
    FormatString(hmalloc_putc, NULL, fmt, va);
    va_end(va);
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
