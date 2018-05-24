/* SPDX-License-Identifier: GPL-2.0 */

/* Based on Linux kernel 4.16.10
 * arch/arm64/include/asm/cmpxchg.h
 * arch/x86/include/asm/cmpxchg.h
 * https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git/commit/?h=v4.16.10&id=b3fdf8284efbc5020dfbd0a28150637189076115
 */

#ifndef __ASM_CMPXCHG_H
#define __ASM_CMPXCHG_H

#if defined(__x86_64__)
#define LOCK_PREFIX_HERE \
                ".pushsection .smp_locks,\"a\"\n"   \
                ".balign 4\n"                       \
                ".long 671f - .\n" /* offset */     \
                ".popsection\n"                     \
                "671:"

#define LOCK_PREFIX LOCK_PREFIX_HERE "\n\tlock; "

/*
 * Constants for operation sizes. On 32-bit, the 64-bit size it set to
 * -1 because sizeof will never return -1, thereby making those switch
 * case statements guaranteeed dead code which the compiler will
 * eliminate, and allowing the "missing symbol in the default case" to
 * indicate a usage error.
 */
#define __X86_CASE_B    1
#define __X86_CASE_W    2
#define __X86_CASE_L    4
#define __X86_CASE_Q    8

/*
 * An exchange-type operation, which takes a value and a pointer, and
 * returns the old value.
 */
#define __xchg_op(ptr, arg, op, lock)                                   \
        ({                                                              \
                __typeof__ (*(ptr)) __ret = (arg);                      \
                switch (sizeof(*(ptr))) {                               \
                case __X86_CASE_B:                                      \
                        asm volatile (lock #op "b %b0, %1\n"            \
                                      : "+q" (__ret), "+m" (*(ptr))     \
                                      : : "memory", "cc");              \
                        break;                                          \
                case __X86_CASE_W:                                      \
                        asm volatile (lock #op "w %w0, %1\n"            \
                                      : "+r" (__ret), "+m" (*(ptr))     \
                                      : : "memory", "cc");              \
                        break;                                          \
                case __X86_CASE_L:                                      \
                        asm volatile (lock #op "l %0, %1\n"             \
                                      : "+r" (__ret), "+m" (*(ptr))     \
                                      : : "memory", "cc");              \
                        break;                                          \
                case __X86_CASE_Q:                                      \
                        asm volatile (lock #op "q %q0, %1\n"            \
                                      : "+r" (__ret), "+m" (*(ptr))     \
                                      : : "memory", "cc");              \
                        break;                                          \
                }                                                       \
                __ret;                                                  \
        })

/*
 * Note: no "lock" prefix even on SMP: xchg always implies lock anyway.
 * Since this is generally used to protect other memory information, we
 * use "asm volatile" and "memory" clobbers to prevent gcc from moving
 * information around.
 */
#define xchg(ptr, v)    __xchg_op((ptr), (v), xchg, "")


/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */
#define __raw_cmpxchg(ptr, old, new, size, lock)                        \
({                                                                      \
        __typeof__(*(ptr)) __ret;                                       \
        __typeof__(*(ptr)) __old = (old);                               \
        __typeof__(*(ptr)) __new = (new);                               \
        switch (size) {                                                 \
        case __X86_CASE_B:                                              \
        {                                                               \
                volatile u8 *__ptr = (volatile u8 *)(ptr);              \
                asm volatile(lock "cmpxchgb %2,%1"                      \
                             : "=a" (__ret), "+m" (*__ptr)              \
                             : "q" (__new), "0" (__old)                 \
                             : "memory");                               \
                break;                                                  \
        }                                                               \
        case __X86_CASE_W:                                              \
        {                                                               \
                volatile u16 *__ptr = (volatile u16 *)(ptr);            \
                asm volatile(lock "cmpxchgw %2,%1"                      \
                             : "=a" (__ret), "+m" (*__ptr)              \
                             : "r" (__new), "0" (__old)                 \
                             : "memory");                               \
                break;                                                  \
        }                                                               \
        case __X86_CASE_L:                                              \
        {                                                               \
                volatile u32 *__ptr = (volatile u32 *)(ptr);            \
                asm volatile(lock "cmpxchgl %2,%1"                      \
                             : "=a" (__ret), "+m" (*__ptr)              \
                             : "r" (__new), "0" (__old)                 \
                             : "memory");                               \
                break;                                                  \
        }                                                               \
        case __X86_CASE_Q:                                              \
        {                                                               \
                volatile u64 *__ptr = (volatile u64 *)(ptr);            \
                asm volatile(lock "cmpxchgq %2,%1"                      \
                             : "=a" (__ret), "+m" (*__ptr)              \
                             : "r" (__new), "0" (__old)                 \
                             : "memory");                               \
                break;                                                  \
        }                                                               \
        }                                                               \
        __ret;                                                          \
})

#define __cmpxchg(ptr, old, new, size)                                  \
        __raw_cmpxchg((ptr), (old), (new), (size), LOCK_PREFIX)

#define __sync_cmpxchg(ptr, old, new, size)                             \
        __raw_cmpxchg((ptr), (old), (new), (size), "lock; ")

#define __cmpxchg_local(ptr, old, new, size)                            \
        __raw_cmpxchg((ptr), (old), (new), (size), "")

#define cmpxchg(ptr, old, new)                                          \
        __cmpxchg(ptr, old, new, sizeof(*(ptr)))

#define sync_cmpxchg(ptr, old, new)                                     \
        __sync_cmpxchg(ptr, old, new, sizeof(*(ptr)))

#define cmpxchg_local(ptr, old, new)                                    \
        __cmpxchg_local(ptr, old, new, sizeof(*(ptr)))

static __always_inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
    return cmpxchg(&v->counter, old, new);
}

static inline int atomic_xchg(atomic_t *v, int new)
{
    return xchg(&v->counter, new);
}

#define  atomic_cmpxchg_relaxed     atomic_cmpxchg
#define  atomic_cmpxchg_acquire     atomic_cmpxchg
#define  atomic_cmpxchg_release     atomic_cmpxchg
#define  atomic_xchg_relaxed        atomic_xchg
#define  atomic_xchg_acquire        atomic_xchg
#define  atomic_xchg_release        atomic_xchg

#elif defined(__aarch64__)
#if defined(USE_LSE) /* ARMv8.1 with LSE */
#define ARM64_LSE_ATOMIC_INSN(llsc, lse)        lse
__asm__(".arch_extension        lse");
#else /* ARMv8.0 without LSE */
#define ARM64_LSE_ATOMIC_INSN(llsc, lse)        llsc
#endif


#define unreachable()                                   \
    do {                                                \
        asm volatile("");                               \
        __builtin_unreachable();                        \
        } while (0)

/* Indirect stringification.  Doing two levels allows the parameter to be a
 * macro itself.  For example, compile with -DFOO=bar, __stringify(FOO)
 * converts to "bar".
 */
#define __stringify_1(x...)    #x
#define __stringify(x...)      __stringify_1(x)
#define notrace __attribute__((no_instrument_function))

#define __nops(n)   ".rept  " #n "\nnop\n.endr\n"
#define nops(n)     asm volatile(__nops(n))

/* Move the ll/sc atomics out-of-line */
#define __LL_SC_INLINE          notrace
#define __LL_SC_PREFIX(x)       __ll_sc_##x

/* Macro for constructing calls to out-of-line ll/sc atomics */
#define __LL_SC_CALL(op)        "bl\t" __stringify(__LL_SC_PREFIX(op)) "\n"
#define __LL_SC_CLOBBERS        "x16", "x17", "x30"

#define __CMPXCHG_CASE(w, sz, name, mb, acq, rel, cl)                   \
__LL_SC_INLINE unsigned long                                            \
__LL_SC_PREFIX(__cmpxchg_case_##name(volatile void *ptr,                \
                                     unsigned long old,                 \
                                     unsigned long new))                \
{                                                                       \
        unsigned long tmp, oldval;                                      \
                                                                        \
        asm volatile(                                                   \
        "       prfm    pstl1strm, %[v]\n"                              \
        "1:     ld" #acq "xr" #sz "\t%" #w "[oldval], %[v]\n"           \
        "       eor     %" #w "[tmp], %" #w "[oldval], %" #w "[old]\n"  \
        "       cbnz    %" #w "[tmp], 2f\n"                             \
        "       st" #rel "xr" #sz "\t%w[tmp], %" #w "[new], %[v]\n"     \
        "       cbnz    %w[tmp], 1b\n"                                  \
        "       " #mb "\n"                                              \
        "2:"                                                            \
        : [tmp] "=&r" (tmp), [oldval] "=&r" (oldval),                   \
          [v] "+Q" (*(unsigned long *)ptr)                              \
        : [old] "Lr" (old), [new] "r" (new)                             \
        : cl);                                                          \
                                                                        \
        return oldval;                                                  \
}                                                                       \

__CMPXCHG_CASE(w, b,     1,        ,  ,  ,         )
__CMPXCHG_CASE(w, h,     2,        ,  ,  ,         )
__CMPXCHG_CASE(w,  ,     4,        ,  ,  ,         )
__CMPXCHG_CASE( ,  ,     8,        ,  ,  ,         )
__CMPXCHG_CASE(w, b, acq_1,        , a,  , "memory")
__CMPXCHG_CASE(w, h, acq_2,        , a,  , "memory")
__CMPXCHG_CASE(w,  , acq_4,        , a,  , "memory")
__CMPXCHG_CASE( ,  , acq_8,        , a,  , "memory")
__CMPXCHG_CASE(w, b, rel_1,        ,  , l, "memory")
__CMPXCHG_CASE(w, h, rel_2,        ,  , l, "memory")
__CMPXCHG_CASE(w,  , rel_4,        ,  , l, "memory")
__CMPXCHG_CASE( ,  , rel_8,        ,  , l, "memory")
__CMPXCHG_CASE(w, b,  mb_1, dmb ish,  , l, "memory")
__CMPXCHG_CASE(w, h,  mb_2, dmb ish,  , l, "memory")
__CMPXCHG_CASE(w,  ,  mb_4, dmb ish,  , l, "memory")
__CMPXCHG_CASE( ,  ,  mb_8, dmb ish,  , l, "memory")


#define __LL_SC_CMPXCHG(op) __LL_SC_CALL(__cmpxchg_case_##op)

#define __LSE_CMPXCHG_CASE(w, sz, name, mb, cl...)              \
static inline unsigned long __cmpxchg_case_##name(volatile void *ptr,   \
                          unsigned long old,    \
                          unsigned long new)    \
{                                   \
    register unsigned long x0 asm ("x0") = (unsigned long)ptr;  \
    register unsigned long x1 asm ("x1") = old;         \
    register unsigned long x2 asm ("x2") = new;         \
                                    \
    asm volatile(ARM64_LSE_ATOMIC_INSN(             \
    /* LL/SC */                         \
    __LL_SC_CMPXCHG(name)                       \
    __nops(2),                          \
    /* LSE atomics */                       \
    "   mov " #w "30, %" #w "[old]\n"           \
    "   cas" #mb #sz "\t" #w "30, %" #w "[new], %[v]\n"     \
    "   mov %" #w "[ret], " #w "30")            \
    : [ret] "+r" (x0), [v] "+Q" (*(unsigned long *)ptr)     \
    : [old] "r" (x1), [new] "r" (x2)                \
    : __LL_SC_CLOBBERS, ##cl);                  \
                                    \
    return x0;                          \
}

__LSE_CMPXCHG_CASE(w, b,     1,   )
__LSE_CMPXCHG_CASE(w, h,     2,   )
__LSE_CMPXCHG_CASE(w,  ,     4,   )
__LSE_CMPXCHG_CASE(x,  ,     8,   )
__LSE_CMPXCHG_CASE(w, b, acq_1,  a, "memory")
__LSE_CMPXCHG_CASE(w, h, acq_2,  a, "memory")
__LSE_CMPXCHG_CASE(w,  , acq_4,  a, "memory")
__LSE_CMPXCHG_CASE(x,  , acq_8,  a, "memory")
__LSE_CMPXCHG_CASE(w, b, rel_1,  l, "memory")
__LSE_CMPXCHG_CASE(w, h, rel_2,  l, "memory")
__LSE_CMPXCHG_CASE(w,  , rel_4,  l, "memory")
__LSE_CMPXCHG_CASE(x,  , rel_8,  l, "memory")
__LSE_CMPXCHG_CASE(w, b,  mb_1, al, "memory")
__LSE_CMPXCHG_CASE(w, h,  mb_2, al, "memory")
__LSE_CMPXCHG_CASE(w,  ,  mb_4, al, "memory")
__LSE_CMPXCHG_CASE(x,  ,  mb_8, al, "memory")

#undef __CMPXCHG_CASE
#undef __LL_SC_CMPXCHG
#undef __LSE_CMPXCHG_CASE


#define __CMPXCHG_GEN(sfx)                      \
static inline unsigned long __cmpxchg##sfx(volatile void *ptr,      \
                       unsigned long old,       \
                       unsigned long new,       \
                       int size)            \
{                                   \
    switch (size) {                         \
    case 1:                             \
        return __cmpxchg_case##sfx##_1(ptr, (u8)old, new);  \
    case 2:                             \
        return __cmpxchg_case##sfx##_2(ptr, (u16)old, new); \
    case 4:                             \
        return __cmpxchg_case##sfx##_4(ptr, old, new);      \
    case 8:                             \
        return __cmpxchg_case##sfx##_8(ptr, old, new);      \
    }                               \
                                    \
    unreachable();                  \
}

__CMPXCHG_GEN()
__CMPXCHG_GEN(_acq)
__CMPXCHG_GEN(_rel)
__CMPXCHG_GEN(_mb)

#undef __CMPXCHG_GEN

#define __cmpxchg_wrapper(sfx, ptr, o, n)               \
({                                  \
    __typeof__(*(ptr)) __ret;                   \
    __ret = (__typeof__(*(ptr)))                    \
        __cmpxchg##sfx((ptr), (unsigned long)(o),       \
                (unsigned long)(n), sizeof(*(ptr)));    \
    __ret;                              \
})

/* cmpxchg */
#define cmpxchg_relaxed(...)    __cmpxchg_wrapper(    , __VA_ARGS__)
#define cmpxchg_acquire(...)    __cmpxchg_wrapper(_acq, __VA_ARGS__)
#define cmpxchg_release(...)    __cmpxchg_wrapper(_rel, __VA_ARGS__)
#define cmpxchg(...)        __cmpxchg_wrapper( _mb, __VA_ARGS__)
#define cmpxchg_local       cmpxchg_relaxed



/*
 * We need separate acquire parameters for ll/sc and lse, since the full
 * barrier case is generated as release+dmb for the former and
 * acquire+release for the latter.
 */
#define __XCHG_CASE(w, sz, name, mb, nop_lse, acq, acq_lse, rel, cl)    \
static inline unsigned long __xchg_case_##name(unsigned long x,     \
                           volatile void *ptr)  \
{                                   \
    unsigned long ret, tmp;                     \
                                    \
    asm volatile(ARM64_LSE_ATOMIC_INSN(             \
    /* LL/SC */                         \
    "   prfm    pstl1strm, %2\n"                \
    "1: ld" #acq "xr" #sz "\t%" #w "0, %2\n"            \
    "   st" #rel "xr" #sz "\t%w1, %" #w "3, %2\n"       \
    "   cbnz    %w1, 1b\n"                  \
    "   " #mb,                          \
    /* LSE atomics */                       \
    "   swp" #acq_lse #rel #sz "\t%" #w "3, %" #w "0, %2\n" \
        __nops(3)                       \
    "   " #nop_lse)                     \
    : "=&r" (ret), "=&r" (tmp), "+Q" (*(unsigned long *)ptr)    \
    : "r" (x)                           \
    : cl);                              \
                                    \
    return ret;                         \
}

__XCHG_CASE(w, b,     1,        ,    ,  ,  ,  ,         )
__XCHG_CASE(w, h,     2,        ,    ,  ,  ,  ,         )
__XCHG_CASE(w,  ,     4,        ,    ,  ,  ,  ,         )
__XCHG_CASE( ,  ,     8,        ,    ,  ,  ,  ,         )
__XCHG_CASE(w, b, acq_1,        ,    , a, a,  , "memory")
__XCHG_CASE(w, h, acq_2,        ,    , a, a,  , "memory")
__XCHG_CASE(w,  , acq_4,        ,    , a, a,  , "memory")
__XCHG_CASE( ,  , acq_8,        ,    , a, a,  , "memory")
__XCHG_CASE(w, b, rel_1,        ,    ,  ,  , l, "memory")
__XCHG_CASE(w, h, rel_2,        ,    ,  ,  , l, "memory")
__XCHG_CASE(w,  , rel_4,        ,    ,  ,  , l, "memory")
__XCHG_CASE( ,  , rel_8,        ,    ,  ,  , l, "memory")
__XCHG_CASE(w, b,  mb_1, dmb ish, nop,  , a, l, "memory")
__XCHG_CASE(w, h,  mb_2, dmb ish, nop,  , a, l, "memory")
__XCHG_CASE(w,  ,  mb_4, dmb ish, nop,  , a, l, "memory")
__XCHG_CASE( ,  ,  mb_8, dmb ish, nop,  , a, l, "memory")

#undef __XCHG_CASE

#define __XCHG_GEN(sfx)                         \
static inline unsigned long __xchg##sfx(unsigned long x,        \
                    volatile void *ptr,     \
                    int size)           \
{                                   \
    switch (size) {                         \
    case 1:                             \
        return __xchg_case##sfx##_1(x, ptr);            \
    case 2:                             \
        return __xchg_case##sfx##_2(x, ptr);            \
    case 4:                             \
        return __xchg_case##sfx##_4(x, ptr);            \
    case 8:                             \
        return __xchg_case##sfx##_8(x, ptr);            \
    }                               \
                                    \
    unreachable();                          \
}

__XCHG_GEN()
__XCHG_GEN(_acq)
__XCHG_GEN(_rel)
__XCHG_GEN(_mb)

#undef __XCHG_GEN

#define __xchg_wrapper(sfx, ptr, x)                 \
({                                  \
    __typeof__(*(ptr)) __ret;                   \
    __ret = (__typeof__(*(ptr)))                    \
        __xchg##sfx((unsigned long)(x), (ptr), sizeof(*(ptr))); \
    __ret;                              \
})

/* xchg */
#define xchg_relaxed(...)   __xchg_wrapper(    , __VA_ARGS__)
#define xchg_acquire(...)   __xchg_wrapper(_acq, __VA_ARGS__)
#define xchg_release(...)   __xchg_wrapper(_rel, __VA_ARGS__)
#define xchg(...)       __xchg_wrapper( _mb, __VA_ARGS__)

#define atomic_cmpxchg_relaxed(v, old, new)             \
    cmpxchg_relaxed(&((v)->counter), (old), (new))
#define atomic_cmpxchg_acquire(v, old, new)             \
    cmpxchg_acquire(&((v)->counter), (old), (new))
#define atomic_cmpxchg_release(v, old, new)             \
    cmpxchg_release(&((v)->counter), (old), (new))
#define atomic_cmpxchg(v, old, new) cmpxchg(&((v)->counter), (old), (new))

#define atomic_xchg_relaxed(v, new) xchg_relaxed(&((v)->counter), (new))
#define atomic_xchg_acquire(v, new) xchg_acquire(&((v)->counter), (new))
#define atomic_xchg_release(v, new) xchg_release(&((v)->counter), (new))
#define atomic_xchg(v, new)     xchg(&((v)->counter), (new))

#else /* Unknown Arch */
    /* TODO: No Arch Default */
#endif /* __x86_64__ */

#endif  /* __ASM_CMPXCHG_H */
