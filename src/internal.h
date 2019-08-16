#ifndef __INTERNAL_H__
#define __INTERNAL_H__

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>




#define HMALLOC_DO_LOGGING
#define HMALLOC_DO_ASSERTIONS
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

#ifdef HMALLOC_DO_LOGGING
#include <assert.h>
#define ASSERT(cond, msg) assert((cond) && "[hmalloc]" msg)
#else
#define ASSERT(cond, mst) ;
#endif

#ifdef HMALLOC_DO_LOGGING
#define LOG(fmt, ...) fprintf(stderr, "[ hmalloc :: %-12s :: %3d ] " fmt "", __FILE__, __LINE__, ##__VA_ARGS__)
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

#endif
