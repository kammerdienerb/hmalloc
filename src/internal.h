#ifndef __INTERNAL_H__
#define __INTERNAL_H__

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>





/* #define HMALLOC_DO_LOGGING */
/* #define HMALLOC_DO_ASSERTIONS */
#define HMALLOC_ANSI_C

#ifdef HMALLOC_ANSI_C
#define inline
#endif




#define UINT(w) uint##w##_t
#define SINT(w) int##w##_t

#define u8  UINT(8 )
#define u16 UINT(16)
#define u32 UINT(32)
#define u64 UINT(64)

#define i8  SINT(8 )
#define i16 SINT(16)
#define i32 SINT(32)
#define i64 SINT(64)

#define internal static
#define external extern

#include "FormatString.h"
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
#define ASSERT(cond, msg)                                \
if (!(cond)) {                                           \
    hmalloc_assert_fail(msg, __FILE__, __LINE__, #cond); \
}
#else
#define ASSERT(cond, mst) ;
#endif

#ifdef HMALLOC_DO_LOGGING
#define LOG(fmt, ...) \
    hmalloc_printf("[ hmalloc :: %-12s :: %3d ] " fmt "", __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define LOG(fmt, ...) ;
#endif

#define XOR_SWAP_64(a, b) do {    \
    a = ((u64)(a)) ^ ((u64)(b)); \
    b = ((u64)(b)) ^ ((u64)(a)); \
    a = ((u64)(a)) ^ ((u64)(b)); \
} while (0);

#define XOR_SWAP_PTR(a, b) do {           \
    a = (void*)(((u64)(a)) ^ ((u64)(b))); \
    b = (void*)(((u64)(b)) ^ ((u64)(a))); \
    a = (void*)(((u64)(a)) ^ ((u64)(b))); \
} while (0);

#define ALIGN(x, align)         ((__typeof(x))((((u64)(x)) + (((u64)align) - 1ULL)) & ~(((u64)align) - 1ULL)))
#define IS_ALIGNED(x, align)    (!(((u64)(x)) & (((u64)align) - 1ULL)))
#define IS_ALIGNED_PP(x, align) (!((x) & ((align) - 1ULL)))
#define IS_POWER_OF_TWO(x)      ((x) != 0 && IS_ALIGNED((x), (x)))
#define IS_POWER_OF_TWO_PP(x)   ((x) != 0 && IS_ALIGNED_PP((x), (x)))

#define LOG2_8BIT(v)  (8 - 90/(((v)/4+14)|1) - 2/((v)/2+1))
#define LOG2_16BIT(v) (8*((v)>255) + LOG2_8BIT((v) >>8*((v)>255)))
#define LOG2_32BIT(v) \
    (16*((v)>65535L) + LOG2_16BIT((v)*1L >>16*((v)>65535L)))
#define LOG2_64BIT(v)\
    (32*((v)/2L>>31 > 0) \
     + LOG2_32BIT((v)*1L >>16*((v)/2L>>31 > 0) \
                         >>16*((v)/2L>>31 > 0)))

#endif
