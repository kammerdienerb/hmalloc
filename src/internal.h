#ifndef __INTERNAL_H__
#define __INTERNAL_H__

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/sysinfo.h>


#define HMALLOC_ANSI_C
/* #define HMALLOC_DO_LOGGING */
/* #define HMALLOC_DO_ASSERTIONS */
/* #define HMALLOC_USE_SBLOCKS */

#define EXPAND(a) a
#define CAT2(x, y) _CAT2(x, y)
#define _CAT2(x, y) x##y

#define likely(x)   (__builtin_expect(!!(x), 1))
#define unlikely(x) (__builtin_expect(!!(x), 0))

#define MIN(a, b) ((a) <= (b) ? (a) : (b))
#define MAX(a, b) ((a) >= (b) ? (a) : (b))

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

#ifdef HMALLOC_DEBUG
#define HMALLOC_ALWAYS_INLINE
#else
#define HMALLOC_ALWAYS_INLINE __attribute__((always_inline))
#endif /* HMALLOC_DEBUG */

#include "FormatString.h"
internal void hmalloc_putc(char c, void *fd);
internal void hmalloc_printf(int fd, const char *fmt, ...);

#ifdef HMALLOC_DO_ASSERTIONS
internal void hmalloc_assert_fail(const char *msg, const char *fname, int line, const char *cond_str);
#define ASSERT(cond, msg)                                \
do { if (unlikely(!(cond))) {                            \
    hmalloc_assert_fail(msg, __FILE__, __LINE__, #cond); \
} } while (0)
#else
#define ASSERT(cond, mst) ;
#endif

#include "locks.h"
#ifdef HMALLOC_DO_LOGGING
internal mutex_t log_mtx = MUTEX_INITIALIZER;
internal int     log_fd  = 1;

#define LOG_LOCK()   mutex_lock(&log_mtx)
#define LOG_UNLOCK() mutex_unlock(&log_mtx)

#define LOG(fmt, ...)                                         \
do {                                                          \
    LOG_LOCK(); {                                             \
        hmalloc_printf(log_fd,                                \
                       "[ hmalloc :: %-21s :: %3d ] " fmt "", \
                       __FILE__, __LINE__, ##__VA_ARGS__);    \
    } LOG_UNLOCK();                                           \
} while (0)

void log_init(void);

#else
#define LOG(fmt, ...) ;
#endif

#define XOR_SWAP_64(a, b) do {   \
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
#define LOG2_32BIT(v)                                        \
    (16*((v)>65535L) + LOG2_16BIT((v)*1L >>16*((v)>65535L)))
#define LOG2_64BIT(v)                                        \
    (32*((v)/2L>>31 > 0)                                     \
     + LOG2_32BIT((v)*1L >>16*((v)/2L>>31 > 0)               \
                         >>16*((v)/2L>>31 > 0)))


internal u64 next_power_of_2(u64 x);

#define KB(x) ((x) * 1024ULL)
#define MB(x) ((x) * 1024ULL * KB(1ULL))
#define GB(x) ((x) * 1024ULL * MB(1ULL))
#define TB(x) ((x) * 1024ULL * GB(1ULL))

#define DEFAULT_BLOCK_SIZE (MB(4))

internal char * istrdup(char *s);

#endif
