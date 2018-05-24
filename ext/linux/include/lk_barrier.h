/* SPDX-License-Identifier: GPL-2.0 */

/* Based on Linux kernel 4.16.10
 * arch/arm64/include/asm/barrier.h
 * arch/x86/include/asm/barrier.h
 * https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git/commit/?h=v4.16.10&id=b3fdf8284efbc5020dfbd0a28150637189076115
 */

#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

#include "lk_cmpxchg.h"

#if defined(__x86_64__)

#define mb() 	asm volatile("mfence":::"memory")
#define rmb()	asm volatile("lfence":::"memory")
#define wmb()	asm volatile("sfence" ::: "memory")
#define dma_rmb()	barrier()
#define dma_wmb()	barrier()
#define smp_mb()	asm volatile("lock; addl $0,-4(%%rsp)" ::: "memory", "cc")
#define smp_rmb()	dma_rmb()
#define smp_wmb()	barrier()
#define smp_store_mb(var, value) do { (void)xchg(&var, value); } while (0)

/*
#define smp_store_release(p, v)						\
do {									\
	barrier();							\
	WRITE_ONCE(*p, v);						\
} while (0)

#define smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1 = READ_ONCE(*p);				\
	barrier();							\
	___p1;								\
})
*/

/* Atomic operations are already serializing on x86 */
#define __smp_mb__before_atomic()	barrier()
#define __smp_mb__after_atomic()	barrier()


#elif defined(__aarch64__)

#define isb()		asm volatile("isb" : : : "memory")
#define dmb(opt)	asm volatile("dmb " #opt : : : "memory")
#define dsb(opt)	asm volatile("dsb " #opt : : : "memory")
#define psb_csync()	asm volatile("hint #17" : : : "memory")
#define csdb()		asm volatile("hint #20" : : : "memory")
#define mb()		dsb(sy)
#define rmb()		dsb(ld)
#define wmb()		dsb(st)
#define dma_rmb()	dmb(oshld)
#define dma_wmb()	dmb(oshst)
#define smp_mb()	dmb(ish)
#define smp_rmb()	dmb(ishld)
#define smp_wmb()	dmb(ishst)

/*
#define __smp_store_release(p, v)					\
do {									\
	union { typeof(*p) __val; char __c[1]; } __u =			\
		{ .__val = (__force typeof(*p)) (v) }; 			\
	switch (sizeof(*p)) {						\
	case 1:								\
		asm volatile ("stlrb %w1, %0"				\
				: "=Q" (*p)				\
				: "r" (*(__u8 *)__u.__c)		\
				: "memory");				\
		break;							\
	case 2:								\
		asm volatile ("stlrh %w1, %0"				\
				: "=Q" (*p)				\
				: "r" (*(__u16 *)__u.__c)		\
				: "memory");				\
		break;							\
	case 4:								\
		asm volatile ("stlr %w1, %0"				\
				: "=Q" (*p)				\
				: "r" (*(__u32 *)__u.__c)		\
				: "memory");				\
		break;							\
	case 8:								\
		asm volatile ("stlr %1, %0"				\
				: "=Q" (*p)				\
				: "r" (*(__u64 *)__u.__c)		\
				: "memory");				\
		break;							\
	}								\
} while (0)

#define __smp_load_acquire(p)						\
({									\
	union { typeof(*p) __val; char __c[1]; } __u;			\
	switch (sizeof(*p)) {						\
	case 1:								\
		asm volatile ("ldarb %w0, %1"				\
			: "=r" (*(__u8 *)__u.__c)			\
			: "Q" (*p) : "memory");				\
		break;							\
	case 2:								\
		asm volatile ("ldarh %w0, %1"				\
			: "=r" (*(__u16 *)__u.__c)			\
			: "Q" (*p) : "memory");				\
		break;							\
	case 4:								\
		asm volatile ("ldar %w0, %1"				\
			: "=r" (*(__u32 *)__u.__c)			\
			: "Q" (*p) : "memory");				\
		break;							\
	case 8:								\
		asm volatile ("ldar %0, %1"				\
			: "=r" (*(__u64 *)__u.__c)			\
			: "Q" (*p) : "memory");				\
		break;							\
	}								\
	__u.__val;							\
})
*/

#else /* No Arch */
    /* TODO: No Arch Default */
#endif /* __x86_64__ */

#endif	/* __ASM_BARRIER_H */
