// Copyright (c) 2017 ARM Limited. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "build_config.h"
#include "cpu.h"

#include <stdint.h>
#include <stdlib.h>

/******************************************************************************
 * LL/SC primitives
 *****************************************************************************/

#if __ARM_ARCH == 7 || (__ARM_ARCH == 8 && __ARM_64BIT_STATE == 0)

static inline void dmb()
{
    __asm volatile("dmb" : : : "memory");
}

static inline uint8_t ll8(uint8_t *var, int mm)
{
    uint8_t old;
    __asm volatile("ldrexb %0, [%1]"
                   : "=&r" (old)
                   : "r" (var)
                   : );
    if (mm == __ATOMIC_ACQUIRE)
        dmb();
    return old;
}

static inline uint32_t ll(uint32_t *var, int mm)
{
    uint32_t old;
    __asm volatile("ldrex %0, [%1]"
                   : "=&r" (old)
                   : "r" (var)
                   : );
    //Barrier after an acquiring load
    if (mm == __ATOMIC_ACQUIRE)
        dmb();
    return old;
}
#define ll32(a, b) ll((a), (b))

//Return 0 on success, 1 on failure
static inline uint32_t sc(uint32_t *var, uint32_t neu, int mm)
{
    uint32_t ret;
    //Barrier before a releasing store
    if (mm == __ATOMIC_RELEASE)
        dmb();
    __asm volatile("strex %0, %1, [%2]"
                   : "=&r" (ret)
                   : "r" (neu), "r" (var)
                   : );
    return ret;
}
#define sc32(a, b, c) sc((a), (b), (c))

static inline uint64_t lld(uint64_t *var, int mm)
{
    uint64_t old;
    __asm volatile("ldrexd %0, %H0, [%1]"
                   : "=&r" (old)
                   : "r" (var)
                   : );
    //Barrier after an acquiring load
    if (mm == __ATOMIC_ACQUIRE)
        dmb();
    return old;
}
#define ll64(a, b) lld((a), (b))

//Return 0 on success, 1 on failure
static inline uint32_t scd(uint64_t *var, uint64_t neu, int mm)
{
    uint32_t ret;
    //Barrier before a releasing store
    if (mm == __ATOMIC_RELEASE)
        dmb();
    __asm volatile("strexd %0, %1, %H1, [%2]"
                   : "=&r" (ret)
                   : "r" (neu), "r" (var)
                   : );
    return ret;
}
#define sc64(a, b, c) scd((a), (b), (c))

#endif

#if __ARM_ARCH == 8 && __ARM_64BIT_STATE == 1

static inline uint8_t ll8(uint8_t *var, int mm)
{
    uint8_t old;
    if (mm == __ATOMIC_ACQUIRE)
    __asm volatile("ldaxrb %w0, [%1]"
                   : "=&r" (old)
                   : "r" (var)
                   : "memory");
    else if (mm == __ATOMIC_RELAXED)
    __asm volatile("ldxrb %w0, [%1]"
                   : "=&r" (old)
                   : "r" (var)
                   : );
    else
        abort();
    return old;
}

static inline uint16_t ll16(uint16_t *var, int mm)
{
    uint16_t old;
    if (mm == __ATOMIC_ACQUIRE)
    __asm volatile("ldaxrh %w0, [%1]"
                   : "=&r" (old)
                   : "r" (var)
                   : "memory");
    else if (mm == __ATOMIC_RELAXED)
    __asm volatile("ldxrh %w0, [%1]"
                   : "=&r" (old)
                   : "r" (var)
                   : );
    else
        abort();
    return old;
}

static inline uint32_t ll32(uint32_t *var, int mm)
{
    uint32_t old;
    if (mm == __ATOMIC_ACQUIRE)
    __asm volatile("ldaxr %w0, [%1]"
                   : "=&r" (old)
                   : "r" (var)
                   : "memory");
    else if (mm == __ATOMIC_RELAXED)
    __asm volatile("ldxr %w0, [%1]"
                   : "=&r" (old)
                   : "r" (var)
                   : );
    else
        abort();
    return old;
}

//Return 0 on success, 1 on failure
static inline uint8_t sc8(uint8_t *var, uint8_t neu, int mm)
{
    uint8_t ret;
    if (mm == __ATOMIC_RELEASE)
    __asm volatile("stlxrb %w0, %w1, [%2]"
                   : "=&r" (ret)
                   : "r" (neu), "r" (var)
                   : "memory");
    else if (mm == __ATOMIC_RELAXED)
    __asm volatile("stxrb %w0, %w1, [%2]"
                   : "=&r" (ret)
                   : "r" (neu), "r" (var)
                   : );
    else
        abort();
    return ret;
}

//Return 0 on success, 1 on failure
static inline uint32_t sc32(uint32_t *var, uint32_t neu, int mm)
{
    uint32_t ret;
    if (mm == __ATOMIC_RELEASE)
    __asm volatile("stlxr %w0, %w1, [%2]"
                   : "=&r" (ret)
                   : "r" (neu), "r" (var)
                   : "memory");
    else if (mm == __ATOMIC_RELAXED)
    __asm volatile("stxr %w0, %w1, [%2]"
                   : "=&r" (ret)
                   : "r" (neu), "r" (var)
                   : );
    else
        abort();
    return ret;
}

static inline uint64_t ll(uint64_t *var, int mm)
{
    uint64_t old;
    if (mm == __ATOMIC_ACQUIRE)
    __asm volatile("ldaxr %0, [%1]"
                   : "=&r" (old)
                   : "r" (var)
                   : "memory");
    else if (mm == __ATOMIC_RELAXED)
    __asm volatile("ldxr %0, [%1]"
                   : "=&r" (old)
                   : "r" (var)
                   : );
    else
        abort();
    return old;
}
#define ll64(a, b) ll((a), (b))

//Return 0 on success, 1 on failure
static inline uint32_t sc(uint64_t *var, uint64_t neu, int mm)
{
    uint32_t ret;
    if (mm == __ATOMIC_RELEASE)
    __asm volatile("stlxr %w0, %1, [%2]"
                   : "=&r" (ret)
                   : "r" (neu), "r" (var)
                   : "memory");
    else if (mm == __ATOMIC_RELAXED)
    __asm volatile("stxr %w0, %1, [%2]"
                   : "=&r" (ret)
                   : "r" (neu), "r" (var)
                   : );
    else
        abort();
    return ret;
}
#define sc64(a, b, c) sc((a), (b), (c))

#if defined(__clang__)
union i128
{
    __int128 i128;
    int64_t  i64[2];
};
#endif

static inline __int128 lld(__int128 *var, int mm)
{
#if defined(__clang__)
    union i128 old;
    if (mm == __ATOMIC_ACQUIRE)
    __asm volatile("ldaxp %0, %1, [%2]"
                   : "=&r" (old.i64[0]), "=&r" (old.i64[1])
                   : "r" (var)
                   : "memory");
    else if (mm == __ATOMIC_RELAXED)
    __asm volatile("ldxp %0, %1, [%2]"
                   : "=&r" (old.i64[0]), "=&r" (old.i64[1])
                   : "r" (var)
                   : );
    else
        abort();
    return old.i128;
#else
    __int128 old;
    if (mm == __ATOMIC_ACQUIRE)
    __asm volatile("ldaxp %0, %H0, [%1]"
                   : "=&r" (old)
                   : "r" (var)
                   : "memory");
    else if (mm == __ATOMIC_RELAXED)
    __asm volatile("ldxp %0, %H0, [%1]"
                   : "=&r" (old)
                   : "r" (var)
                   : );
    else
        abort();
    return old;
#endif
}

//Return 0 on success, 1 on failure
static inline uint32_t scd(__int128 *var, __int128 neu, int mm)
{
#if defined(__clang__)
    uint32_t ret;
    if (mm == __ATOMIC_RELEASE)
    __asm volatile("stlxp %w0, %1, %2, [%3]"
                   : "=&r" (ret)
                   : "r" (((union i128)neu).i64[0]),
                     "r" (((union i128)neu).i64[1]),
                     "r" (var)
                   : "memory");
    else if (mm == __ATOMIC_RELAXED)
    __asm volatile("stxp %w0, %1, %2, [%3]"
                   : "=&r" (ret)
                   : "r" (((union i128)neu).i64[0]),
                     "r" (((union i128)neu).i64[1]),
                     "r" (var)
                   : );
    else
        abort();
    return ret;
#else
    uint32_t ret;
    if (mm == __ATOMIC_RELEASE)
    __asm volatile("stlxp %w0, %1, %H1, [%2]"
                   : "=&r" (ret)
                   : "r" (neu), "r" (var)
                   : "memory");
    else if (mm == __ATOMIC_RELAXED)
    __asm volatile("stxp %w0, %1, %H1, [%2]"
                   : "=&r" (ret)
                   : "r" (neu), "r" (var)
                   : );
    else
        abort();
    return ret;
#endif
}
#endif

static inline void sevl(void)
{
#if defined __ARM_ARCH
    __asm volatile("sevl" : : : );
#endif
}

static inline void sev(void)
{
#if defined __ARM_ARCH
    __asm volatile("sev" : : : "memory");
#endif
}

static inline int wfe(void)
{
#if defined __ARM_ARCH
    __asm volatile("wfe" : : : "memory");
#endif
    return 1;
}

#ifdef USE_WFE
#define SEVL() sevl()
#define WFE() wfe()
#define SEV() do { __asm volatile ("dsb ish" ::: "memory"); sev(); } while(0)
#if __ARM_ARCH == 8 && __ARM_64BIT_STATE == 1
#define LDXR128(addr, mo) lld((addr), (mo))
#endif
#define LDXR64(addr, mo) ll64((addr), (mo))
#define LDXR32(addr, mo) ll32((addr), (mo))
#define LDXR16(addr, mo) ll16((addr), (mo))
#define LDXR8(addr, mo)  ll8((addr), (mo))
#define LDXR(addr, mo)   ll((addr), (mo))
//When using WFE we should not stall the pipeline using other means
#define DOZE() (void)0
#else
#define SEVL() (void)0
#define WFE() 1
#define SEV() (void)0
#define LDXR128(addr, mo) __atomic_load_n((addr), (mo))
#define LDXR64(addr, mo)  __atomic_load_n((addr), (mo))
#define LDXR32(addr, mo)  __atomic_load_n((addr), (mo))
#define LDXR16(addr, mo)  __atomic_load_n((addr), (mo))
#define LDXR8(addr, mo)   __atomic_load_n((addr), (mo))
#define LDXR(addr, mo)    __atomic_load_n((addr), (mo))
#define DOZE() doze()
#endif
