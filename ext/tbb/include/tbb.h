/*
    Copyright (c) 2005-2018 Intel Corporation

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.




*/

/*
 * Based on: 
 *
 *      project: github.com/01org/tbb, files:
 *      tbb/include/tbb/machine/gcc_generic.h,
 *      tbb/inclide/tbb/machine/linux-intel64.h
 *
 * __TBB mappings:
 *
 *      Only added logic that is needed for spin_rw_mutex - only support for
 *      64b wide data (wordsize == 8) and Linux
 *
 *      for Aarch64: default is GCC built-ins (based on gcc version),
 *      alternative: lockhammer local atomics via USE_LOCAL, with or w/o USE_LSE
 *
 *      for x86-64: default is lockhammer local atomics (which should be same
 *      as machine/linux_intel64.h), aternative: GCC built-ins via
 *      USE_GCC_BUILTINS (based on gcc version)
 *
 * For both ISAs, USE_LOCAL has higher priority over USE_GCC_BUILTINS if used
 * together.
 */


#ifndef __TBB_H
#define __TBB_H

#define _GNU_SOURCE

#include "atomics.h"
#include "cpu_relax.h"

/* Non default configurations */
// #define USE_LOCAL
// #define USE_LSE
// #define USE_GCC_BUILTINS


#ifndef NDEBUG
#pragma message("Using debug build!!")
#define DBG(fmt,...) \
    do { fprintf(stderr, "tbb>%s:%d " fmt, \
        __func__, __LINE__, ##__VA_ARGS__); } while (0);

#define __TBB_ASSERT(b, msg) \
    do { if (!(b)) { DBG("Assert: %s\n", msg); exit (1); } } while(0);

#else  /* NDEBUG */

#define DBG(fmt, ...) do {} while (0);
#define __TBB_ASSERT(b, msg) do { } while(0);

#endif  /* NDEBUG */

/*
 * spin; do not yield
 */
static inline void machine_pause (int32_t delay) {
    while(delay>0) {
        __cpu_relax();
        delay--;
    }
}

#if defined(USE_LOCAL) || !defined(USE_GCC_BUILTINS)
#ifndef NDEBUG
#pragma message("Using local atomics in tbb.h")
#endif  /* NDEBUG */

/*
 * this really needs to be fetchadd64_release, however we want to be same as
 * how intel-tbb uses gcc built ins
 *
 * The atomics.h is aware of USE_LSE configuration
 * So no need to do anything here.
 */
#define __TBB_machine_cmpswp8(P,V,C)        cas64_acquire_release((unsigned long *) P,V,C)
#define __TBB_machine_fetchadd8(P,V)        fetchadd64_acquire_release((unsigned long *) P,V)
#define __TBB_machine_fetchadd8release(P,V) fetchadd64_acquire_release((unsigned long *) P,V)

static inline void __TBB_machine_or(volatile void* operand, uint64_t addend) {
#if defined(__x86_64__) && !defined(USE_BUILTIN)
    asm volatile(
            "lock\norq %1,%0"
            : "=m"(*(volatile uint64_t*)operand)
            : "r"(addend), "m"(*(volatile uint64_t*)operand)
            : "cc", "memory");
#elif defined(__aarch64__) && !defined(USE_BUILTIN)
#ifndef USE_LSE
    unsigned long old, newval, tmp;
    asm volatile(
            "1: ldaxr  %[old], %[ptr]\n"
            "   orr    %[newval], %[old], %[val]\n"
            "   stlxr  %w[tmp], %[newval], %[ptr]\n"
            "   cbnz   %w[tmp], 1b\n"
            : [tmp] "=&r" (tmp), [old] "=&r" (old), [newval] "=&r" (newval),
              [ptr] "+Q" (*(unsigned long *)operand)
            : [val] "Lr" (addend)
            : );
#else  /* USE_LSE */
    // clobbering addend - to match gcc
    asm volatile(
    "ldsetal   %[val], %[val], %[ptr]\n"
    : [val] "+&r" (addend), [ptr] "+Q" (*(unsigned long *)operand)
    : );
#endif  /* USE_LSE */
#elif defined(USE_BUILTIN)
    /* Arch independent implementation */
    for(;;) {
        uintptr_t tmp = *(volatile uintptr_t *)operand;
        uintptr_t result = __TBB_machine_cmpswp8(operand, tmp|addend, tmp);
        if( result==tmp ) break;
    }
#endif  /* ARCH */
}

static inline void __TBB_machine_and(volatile void* operand, uint64_t addend) {
#if defined(__x86_64__) && !defined(USE_BUILTIN)
    asm volatile(
            "lock\nandq %1,%0"
            : "=m"(*(volatile uint64_t*)operand)
            : "r"(addend), "m"(*(volatile uint64_t*)operand)
            : "cc", "memory");
#elif defined(__aarch64__) && !defined(USE_BUILTIN)
#ifndef USE_LSE
    unsigned long old, newval, tmp;
    asm volatile(
            "1: ldaxr   %[old], %[ptr]\n"
            "   and     %[newval], %[old], %[val]\n"
            "   stlxr   %w[tmp], %[newval], %[ptr]\n"
            "   cbnz    %w[tmp], 1b\n"
            : [tmp] "=&r" (tmp), [old] "=&r" (old), [newval] "=&r" (newval),
              [ptr] "+Q" (*(unsigned long *)operand)
            : [val] "Lr" (addend)
            : );
#else  /* USE_LSE */
    // clobbering addend - to match gcc
    asm volatile(
        "mvn %[val], %[val]\n"
        "ldclral   %[val], %[val], %[ptr]\n"
    : [val] "+&r" (addend), [ptr] "+Q" (*(unsigned long *)operand)
    : );
#endif  /* USE_LSE */
#elif defined(USE_BUILTIN)
    /* Arch independent implementation */
    for(;;) {
        uintptr_t tmp = *(volatile uintptr_t *)operand;
        uintptr_t result = __TBB_machine_cmpswp8(operand, tmp&addend, tmp);
        if( result==tmp ) break;
    }
#endif  /* ARCH */
}

#else  /* GCC Built-ins */

#define __GCC_VERSION \
    (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

#if __GCC_VERSION < 40700 /* use __sync* built-ins */
#ifndef NDEBUG
#pragma message("Using old gcc (<4.7.0) built-ins!!")
#endif  /* NDEBUG */

#define __TBB_MACHINE_DEFINE_ATOMICS(S,T)                                      \
inline T __TBB_machine_cmpswp##S( volatile void *ptr, T value, T comparand ) { \
    return __sync_val_compare_and_swap((volatile T *)ptr,comparand,value);     \
}                                                                              \
inline T __TBB_machine_fetchadd##S( volatile void *ptr, T value ) {            \
    return __sync_fetch_and_add((volatile T *)ptr,value);                      \
}

static inline void __TBB_machine_or( volatile void *ptr, uintptr_t addend ) {
    __sync_fetch_and_or((volatile uintptr_t *)ptr,addend);
}

static inline void __TBB_machine_and( volatile void *ptr, uintptr_t addend ) {
    __sync_fetch_and_and((volatile uintptr_t *)ptr,addend);
}

#else  /* __GCC_VERSION >= 40700; use __atomic* built-ins */
#ifndef NDEBUG
#pragma message("Using new gcc (>=4.7.0) built-ins!!")
#endif  /* NDEBUG */

#define __TBB_MACHINE_DEFINE_ATOMICS(S,T)                                      \
inline T __TBB_machine_cmpswp##S( volatile void *ptr, T value, T comparand ) { \
    (void)__atomic_compare_exchange_n((volatile T *)ptr, &comparand, value,    \
                                      0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);  \
    return comparand;                                                          \
}                                                                              \
inline T __TBB_machine_fetchadd##S( volatile void *ptr, T value ) {            \
    return __atomic_fetch_add((volatile T *)ptr, value, __ATOMIC_SEQ_CST);     \
}

static inline void __TBB_machine_or( volatile void *ptr, uintptr_t addend ) {
    __atomic_fetch_or((volatile uintptr_t *)ptr,addend,__ATOMIC_SEQ_CST);
}

static inline void __TBB_machine_and( volatile void *ptr, uintptr_t addend ) {
    __atomic_fetch_and((volatile uintptr_t *)ptr,addend,__ATOMIC_SEQ_CST);
}

#endif  /* __GCC_VERSION */

/* only intptr_t for now */
__TBB_MACHINE_DEFINE_ATOMICS(8, intptr_t)

/*
 * func: fetchaddNrelese
 * Scope for optimization on AArch64 side: we may not need acquire semantics?
 */
#define  __TBB_machine_fetchadd8release(P,V)  __TBB_machine_fetchadd8(P,V)

#endif  /* USE_LOCAL, !USE_GCC_BUILTINS */


/*
 * Top level abstraction
 */
#define __TBB_machine_pause(C)              machine_pause(C)
#define __TBB_Yield()                       sched_yield()
#define __TBB_Pause(C)                      __TBB_machine_pause(C)
#define __TBB_CompareAndSwapW(P,V,C)        __TBB_machine_cmpswp8(P,V,C)
#define __TBB_FetchAndAddW(P,V)             __TBB_machine_fetchadd8(P,V)
#define __TBB_FetchAndAddWrelease(P,V)      __TBB_machine_fetchadd8release(P,V)
#define __TBB_AtomicOR(P,V)                 __TBB_machine_or(P,V)
#define __TBB_AtomicAND(P,V)                __TBB_machine_and(P,V)

/* TBB helper routines */

/*
 * From: class atomic_backoff : no_copy
 *
 * //! Class that implements exponential backoff.
 * 16 is approximately how many 'pause' x86 instruction takes for
 * their context switch.  This is hard-coded in tbb_machine.h,
 * but we make it optionally a test parameter using the -y flag
 * if TBB_LOOPS_BEFORE_YIELD_HARDCODED is not defined below.
 */

//#define TBB_LOOPS_BEFORE_YIELD_HARDCODED
#ifdef TBB_LOOPS_BEFORE_YIELD_HARDCODED
#define LOOPS_BEFORE_YIELD 16
#else
unsigned long LOOPS_BEFORE_YIELD = 16;
#endif

static inline unsigned long atomic_backoff__pause(unsigned long count) {
    if( count<=LOOPS_BEFORE_YIELD ) {
        __TBB_Pause(count);
        // Pause twice as long the next time.
        count *= 2;
    } else {
        // Pause is so long that we might as well yield CPU to scheduler.
        __TBB_Yield();
    }
    return count;
}

/*
 * Generic versions of helper functions if not defined by now
 */
#ifndef __TBB_AtomicOR
#ifndef NDEBUG
#pragma message("Using backoff based AtomicOR!!")
#endif  /* NDEBUG */
static inline void __TBB_AtomicOR(void* operand, uintmax_t addend) {
    int32_t count;
    for(count = 1;;atomic_backoff__pause(&count)) {
        uintptr_t tmp = *(volatile uintptr_t *)operand;
        uintptr_t result = __TBB_CompareAndSwapW(operand, tmp|addend, tmp);
        if( result==tmp ) break;
    }
}
#endif  /* __TBB_AtomicOR */

#ifndef __TBB_AtomicAND
#ifndef NDEBUG
#pragma message("Using backoff based AtomicAND!!")
#endif  /* NDEBUG */
static inline void __TBB_AtomicAND(void* operand, uintptr_t addend) {
    int32_t count;
    for(count = 1;;atomic_backoff__pause(&count)) {
        uintptr_t tmp = *(volatile uintptr_t *)operand;
        uintptr_t result = __TBB_CompareAndSwapW(operand, tmp&addend, tmp);
        if( result==tmp ) break;
    }
}
#endif  /* __TBB_AtomicAND */
#endif  /* __TBB_H */


/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
