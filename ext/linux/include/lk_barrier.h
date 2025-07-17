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

#elif defined(__riscv)

#define RISCV_FENCE_ASM(p, s)       "\tfence " #p "," #s "\n"
#define RISCV_FENCE(p, s) \
	({ __asm__ __volatile__ (RISCV_FENCE_ASM(p, s) : : : "memory"); })

#define mb()      RISCV_FENCE(iorw, iorw)
#define rmb()     RISCV_FENCE(ir, ir)
#define wmb()     RISCV_FENCE(ow, ow)
#define smp_mb()  RISCV_FENCE(rw, rw)
#define smp_rmb() RISCV_FENCE(r, r)
#define smp_wmb() RISCV_FENCE(w, w)

#else /* No Arch */
    /* TODO: No Arch Default */
#endif /* __x86_64__ */

#endif	/* __ASM_BARRIER_H */
