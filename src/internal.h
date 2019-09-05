#ifndef __INTERNAL_H__
#define __INTERNAL_H__

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>



#define HMALLOC_ANSI_C
/* #define HMALLOC_DO_LOGGING */
/* #define HMALLOC_DO_ASSERTIONS */
#define HMALLOC_USE_SBLOCKS

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

#ifdef HMALLOC_DO_LOGGING
internal int log_fd = 1;
#define LOG(fmt, ...) \
    hmalloc_printf(log_fd, "[ hmalloc :: %-15s :: %3d ] " fmt "", __FILE__, __LINE__, ##__VA_ARGS__)

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


#define KiB(x) ((x) * 1024ULL)
#define MiB(x) ((x) * 1024ULL * KiB(1ULL))
#define GiB(x) ((x) * 1024ULL * MiB(1ULL))
#define TiB(x) ((x) * 1024ULL * GiB(1ULL))

#define DEFAULT_BLOCK_SIZE (MiB(2))

#define HMALLOC_MTX_LOCKER(mtx_ptr)   do { pthread_mutex_lock(mtx_ptr);   } while (0)
#define HMALLOC_MTX_UNLOCKER(mtx_ptr) do { pthread_mutex_unlock(mtx_ptr); } while (0)

#define LOG_LOCK()   HMALLOC_MTX_LOCKER(&log_mtx)
#define LOG_UNLOCK() HMALLOC_MTX_UNLOCKER(&log_mtx)

#endif
