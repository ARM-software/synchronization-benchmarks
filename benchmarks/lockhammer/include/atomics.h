/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdbool.h>

#ifndef __LH_ATOMICS_H_
#define __LH_ATOMICS_H_


static inline void spin_wait (unsigned long wait_iter) {
#if defined(__aarch64__)
	asm volatile(
	"1:	subs	%[wait_iter], %[wait_iter], #1\n"
	"	bcs	1b\n"
	: [wait_iter] "+&r" (wait_iter));
#else
	int i = 0;

	for (i = 0; i < wait_iter; i++) {
		asm("");
	}
#endif
}

static inline void wait64 (unsigned long *lock, unsigned long val) {
	#if defined(__aarch64__)
	unsigned long tmp;

	asm volatile(
	"	sevl\n"
	"1:	wfe\n"
	"	ldaxr	%[tmp], %[lock]\n"
	"	eor	%[tmp], %[tmp], %[val]\n"
	"	cbnz	%[tmp], 1b\n"
	: [tmp] "=&r" (tmp)
	: [lock] "Q" (*lock), [val] "r" (val)
	: );
	#else
	volatile unsigned long *v = lock;

	while (*v != val);
	#endif
}

static inline void wait32 (uint32_t *lock, uint32_t val) {
	#if defined(__aarch64__)
	uint32_t tmp;

	asm volatile(
	"	sevl\n"
	"1:	wfe\n"
	"	ldaxr	%w[tmp], %[lock]\n"
	"	eor	%w[tmp], %w[tmp], %w[val]\n"
	"	cbnz	%w[tmp], 1b\n"
	: [tmp] "=&r" (tmp)
	: [lock] "Q" (*lock), [val] "r" (val)
	: );
	#else
	volatile uint32_t *v = lock;

	while (*v != val);
	#endif
}

static inline void prefetch64 (unsigned long *ptr) {
#if defined(__aarch64__)
	asm volatile("	prfm	pstl1keep, %[ptr]\n"
	:
	: [ptr] "Q" (*(unsigned long *)ptr));
#endif
}

static inline unsigned long fetchadd64_acquire_release (unsigned long *ptr, unsigned long val) {
#if defined(__x86_64__) && !defined(USE_BUILTIN)
	asm volatile ("lock xaddq %q0, %1\n"
		      : "+r" (val), "+m" (*(ptr))
		      : : "memory", "cc");
#elif defined(__aarch64__) && !defined(USE_BUILTIN)
#if defined(USE_LSE)
	unsigned long old;

	asm volatile(
	"	ldaddal	%[val], %[old], %[ptr]\n"
	: [old] "=&r" (old), [ptr] "+Q" (*(unsigned long *)ptr)
	: [val] "r" (val)
	: );

	val = old;
#else
	unsigned long tmp, old, newval;

	asm volatile(
	"1:	ldaxr	%[old], %[ptr]\n"
	"	add	%[newval], %[old], %[val]\n"
	"	stlxr	%w[tmp], %[newval], %[ptr]\n"
	"	cbnz	%w[tmp], 1b\n"
	: [tmp] "=&r" (tmp), [old] "=&r" (old), [newval] "=&r" (newval),
	  [ptr] "+Q" (*(unsigned long *)ptr)
	: [val] "Lr" (val)
	: );

	val = old;
#endif
#else
	val = __atomic_fetch_add(ptr, val, __ATOMIC_ACQ_REL);
#endif

	return val;
}

static inline unsigned long fetchadd64_acquire (unsigned long *ptr, unsigned long val) {
#if defined(__x86_64__) && !defined(USE_BUILTIN)
	asm volatile ("lock xaddq %q0, %1\n"
		      : "+r" (val), "+m" (*(ptr))
		      : : "memory", "cc");
#elif defined(__aarch64__) && !defined(USE_BUILTIN)
#if defined(USE_LSE)
	unsigned long old;

	asm volatile(
	"	ldadda	%[val], %[old], %[ptr]\n"
	: [old] "=&r" (old), [ptr] "+Q" (*(unsigned long *)ptr)
	: [val] "r" (val)
	: );

	val = old;
#else
	unsigned long tmp, old, newval;

	asm volatile(
	"1:	ldaxr	%[old], %[ptr]\n"
	"	add	%[newval], %[old], %[val]\n"
	"	stxr	%w[tmp], %[newval], %[ptr]\n"
	"	cbnz	%w[tmp], 1b\n"
	: [tmp] "=&r" (tmp), [old] "=&r" (old), [newval] "=&r" (newval),
	  [ptr] "+Q" (*(unsigned long *)ptr)
	: [val] "Lr" (val)
	: );

	val = old;
#endif
#else
	val = __atomic_fetch_add(ptr, val, __ATOMIC_ACQUIRE);
#endif

	return val;
}

static inline unsigned long fetchadd64_release (unsigned long *ptr, unsigned long val) {
#if defined(__x86_64__) && !defined(USE_BUILTIN)
	asm volatile ("lock xaddq %q0, %1\n"
		      : "+r" (val), "+m" (*(ptr))
		      : : "memory", "cc");
#elif defined(__aarch64__) && !defined(USE_BUILTIN)
#if defined(USE_LSE)
	unsigned long old;

	asm volatile(
	"	ldaddl	%[val], %[old], %[ptr]\n"
	: [old] "=&r" (old), [ptr] "+Q" (*(unsigned long *)ptr)
	: [val] "r" (val)
	: );

	val = old;
#else
	unsigned long tmp, old, newval;

	asm volatile(
	"1:	ldxr	%[old], %[ptr]\n"
	"	add	%[newval], %[old], %[val]\n"
	"	stlxr	%w[tmp], %[newval], %[ptr]\n"
	"	cbnz	%w[tmp], 1b\n"
	: [tmp] "=&r" (tmp), [old] "=&r" (old), [newval] "=&r" (newval),
	  [ptr] "+Q" (*(unsigned long *)ptr)
	: [val] "Lr" (val)
	: );

#endif
	val = old;
#else
	val = __atomic_fetch_add(ptr, val, __ATOMIC_RELEASE);
#endif

	return val;
}

static inline unsigned long fetchadd64 (unsigned long *ptr, unsigned long val) {
#if defined(__x86_64__) && !defined(USE_BUILTIN)
	asm volatile ("lock xaddq %q0, %1\n"
		      : "+r" (val), "+m" (*(ptr))
		      : : "memory", "cc");
#elif defined(__aarch64__) && !defined(USE_BUILTIN)
#if defined(USE_LSE)
	unsigned long old;

	asm volatile(
	"	ldadd	%[val], %[old], %[ptr]\n"
	: [old] "=&r" (old), [ptr] "+Q" (*(unsigned long *)ptr)
	: [val] "r" (val)
	: );

	val = old;
#else
	unsigned long tmp, old, newval;

	asm volatile(
	"1:	ldxr	%[old], %[ptr]\n"
	"	add	%[newval], %[old], %[val]\n"
	"	stxr	%w[tmp], %[newval], %[ptr]\n"
	"	cbnz	%w[tmp], 1b\n"
	: [tmp] "=&r" (tmp), [old] "=&r" (old), [newval] "=&r" (newval),
	  [ptr] "+Q" (*(unsigned long *)ptr)
	: [val] "Lr" (val)
	: );

	val = old;
#endif
#else
	val = __atomic_fetch_add(ptr, val, __ATOMIC_RELAXED);
#endif

	return val;
}

static inline unsigned long fetchsub64 (unsigned long *ptr, unsigned long val) {
#if defined(__x86_64__) && !defined(USE_BUILTIN)
	val = (unsigned long) (-(long) val);

	asm volatile ("lock xaddq %q0, %1\n"
		      : "+r" (val), "+m" (*(ptr))
		      : : "memory", "cc");
#elif defined(__aarch64__) && !defined(USE_BUILTIN)
#if defined(USE_LSE)
	unsigned long old;
	val = (unsigned long) (-(long) val);

	asm volatile(
	"	ldadd	%[val], %[old], %[ptr]\n"
	: [old] "=&r" (old), [ptr] "+Q" (*(unsigned long *)ptr)
	: [val] "r" (val)
	: );

	val = old;
#else
	unsigned long tmp, old, newval;

	asm volatile(
	"1:	ldxr	%[old], %[ptr]\n"
	"	sub	%[newval], %[old], %[val]\n"
	"	stxr	%w[tmp], %[newval], %[ptr]\n"
	"	cbnz	%w[tmp], 1b\n"
	: [tmp] "=&r" (tmp), [old] "=&r" (old), [newval] "=&r" (newval),
	  [ptr] "+Q" (*(unsigned long *)ptr)
	: [val] "Lr" (val)
	: );

	val = old;
#endif
#else
	val = __atomic_fetch_sub(ptr, val, __ATOMIC_RELAXED);
#endif

	return val;
}

static inline unsigned long swap64 (unsigned long *ptr, unsigned long val) {
#if defined(__x86_64__) && !defined(USE_BUILTIN)
	asm volatile ("xchgq %q0, %1\n"
		      : "+r" (val), "+m" (*(ptr))
		      : : "memory", "cc");
#elif defined(__aarch64__) && !defined(USE_BUILTIN)
#if defined(USE_LSE)
	unsigned long old;

	asm volatile(
	"	swpal	%[val], %[old], %[ptr]\n"
	: [old] "=&r" (old), [ptr] "+Q" (*(unsigned long *)ptr)
	: [val] "r" (val)
	: );

	val = old;
#else
	unsigned long tmp, old;

	asm volatile(
	"1:	ldaxr	%[old], %[ptr]\n"
	"	stlxr	%w[tmp], %[val], %[ptr]\n"
	"	cbnz	%w[tmp], 1b\n"
	: [tmp] "=&r" (tmp), [old] "=&r" (old),
	  [ptr] "+Q" (*(unsigned long *)ptr)
	: [val] "r" (val)
	: );

	val = old;
#endif
#else
	val = __atomic_exchange_n(ptr, val, __ATOMIC_ACQ_REL);
#endif

	return val;
}

static inline unsigned long cas64 (unsigned long *ptr, unsigned long val, unsigned long exp) {
	unsigned long old;

#if defined(__x86_64__) && !defined(USE_BUILTIN)
	asm volatile ("lock cmpxchgq %2, %1\n"
		      : "=a" (old), "+m" (*(ptr))
		      : "r" (val), "0" (exp)
		      : "memory");
#elif defined(__aarch64__) && !defined(USE_BUILTIN)
#if defined(USE_LSE)
	asm volatile(
	"	mov	%[old], %[exp]\n"
	"	cas	%[old], %[val], %[ptr]\n"
	: [old] "=&r" (old), [ptr] "+Q" (*(unsigned long *)ptr)
	: [exp] "Lr" (exp), [val] "r" (val)
	: );
#else
	unsigned long tmp;

	asm volatile(
	"1:	ldxr	%[old], %[ptr]\n"
	"	eor	%[tmp], %[old], %[exp]\n"
	"	cbnz	%[tmp], 2f\n"
	"	stxr	%w[tmp], %[val], %[ptr]\n"
	"	cbnz	%w[tmp], 1b\n"
	"2:"
	: [tmp] "=&r" (tmp), [old] "=&r" (old),
	  [ptr] "+Q" (*(unsigned long *)ptr)
	: [exp] "Lr" (exp), [val] "r" (val)
	: );
#endif
#else
	old = exp;
	__atomic_compare_exchange_n(ptr, &old, val, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
#endif

	return old;
}

static inline unsigned long cas64_acquire (unsigned long *ptr, unsigned long val, unsigned long exp) {
	unsigned long old;

#if defined(__x86_64__) && !defined(USE_BUILTIN)
	asm volatile ("lock cmpxchgq %2, %1\n"
		      : "=a" (old), "+m" (*(ptr))
		      : "r" (val), "0" (exp)
		      : "memory");
#elif defined(__aarch64__) && !defined(USE_BUILTIN)
#if defined(USE_LSE)
	asm volatile(
	"	mov	%[old], %[exp]\n"
	"	casa	%[old], %[val], %[ptr]\n"
	: [old] "=&r" (old), [ptr] "+Q" (*(unsigned long *)ptr)
	: [exp] "Lr" (exp), [val] "r" (val)
	: );
#else
	unsigned long tmp;

	asm volatile(
	"1:	ldaxr	%[old], %[ptr]\n"
	"	eor	%[tmp], %[old], %[exp]\n"
	"	cbnz	%[tmp], 2f\n"
	"	stxr	%w[tmp], %[val], %[ptr]\n"
	"	cbnz	%w[tmp], 1b\n"
	"2:"
	: [tmp] "=&r" (tmp), [old] "=&r" (old),
	  [ptr] "+Q" (*(unsigned long *)ptr)
	: [exp] "Lr" (exp), [val] "r" (val)
	: );
#endif
#else
	old = exp;
	__atomic_compare_exchange_n(ptr, &old, val, true, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
#endif

	return old;
}

static inline unsigned long cas64_release (unsigned long *ptr, unsigned long val, unsigned long exp) {
	unsigned long old;

#if defined(__x86_64__) && !defined(USE_BUILTIN)
	asm volatile ("lock cmpxchgq %2, %1\n"
		      : "=a" (old), "+m" (*(ptr))
		      : "r" (val), "0" (exp)
		      : "memory");
#elif defined(__aarch64__) && !defined(USE_BUILTIN)
#if defined(USE_LSE)
	asm volatile(
	"	mov	%[old], %[exp]\n"
	"	casl	%[old], %[val], %[ptr]\n"
	: [old] "=&r" (old), [ptr] "+Q" (*(unsigned long *)ptr)
	: [exp] "Lr" (exp), [val] "r" (val)
	: );
#else
	unsigned long tmp;

	asm volatile(
	"1:	ldxr	%[old], %[ptr]\n"
	"	eor	%[tmp], %[old], %[exp]\n"
	"	cbnz	%[tmp], 2f\n"
	"	stlxr	%w[tmp], %[val], %[ptr]\n"
	"	cbnz	%w[tmp], 1b\n"
	"2:"
	: [tmp] "=&r" (tmp), [old] "=&r" (old),
	  [ptr] "+Q" (*(unsigned long *)ptr)
	: [exp] "Lr" (exp), [val] "r" (val)
	: );
#endif
#else
	old = exp;
	__atomic_compare_exchange_n(ptr, &old, val, true, __ATOMIC_RELEASE, __ATOMIC_RELEASE);
#endif

	return old;
}

static inline unsigned long cas64_acquire_release (unsigned long *ptr, unsigned long val, unsigned long exp) {
	unsigned long old;

#if defined(__x86_64__) && !defined(USE_BUILTIN)
	asm volatile ("lock cmpxchgq %2, %1\n"
		      : "=a" (old), "+m" (*(ptr))
		      : "r" (val), "0" (exp)
		      : "memory");
#elif defined(__aarch64__) && !defined(USE_BUILTIN)
#if defined(USE_LSE)
	asm volatile(
	"	mov	%[old], %[exp]\n"
	"	casal	%[old], %[val], %[ptr]\n"
	: [old] "=&r" (old), [ptr] "+Q" (*(unsigned long *)ptr)
	: [exp] "Lr" (exp), [val] "r" (val)
	: );
#else
	unsigned long tmp;

	asm volatile(
	"1:	ldaxr	%[old], %[ptr]\n"
	"	eor	%[tmp], %[old], %[exp]\n"
	"	cbnz	%[tmp], 2f\n"
	"	stlxr	%w[tmp], %[val], %[ptr]\n"
	"	cbnz	%w[tmp], 1b\n"
	"2:"
	: [tmp] "=&r" (tmp), [old] "=&r" (old),
	  [ptr] "+Q" (*(unsigned long *)ptr)
	: [exp] "Lr" (exp), [val] "r" (val)
	: );
#endif
#else
	old = exp;
	__atomic_compare_exchange_n(ptr, &old, val, true, __ATOMIC_ACQ_REL, __ATOMIC_ACQ_REL);
#endif

	return old;
}

/* Simple linear barrier routine for synchronizing threads */
#define SENSE_BIT_MASK 0x1000000000000000

void synchronize_threads(uint64_t *barrier, unsigned long nthrds)
{
    uint64_t global_sense = *barrier & SENSE_BIT_MASK;
    uint64_t tmp_sense = ~global_sense & SENSE_BIT_MASK;
    uint32_t local_sense = (uint32_t)(tmp_sense >> 32);

    fetchadd64_acquire(barrier, 2);
    if (*barrier == ((nthrds * 2) | global_sense)) {
        // Make sure the store gets observed by the system. Reset count
        // to zero and flip the sense bit.
        __atomic_store_n(barrier, tmp_sense, __ATOMIC_RELEASE);
    } else {
        // Wait only on the sense bit to change.  Avoids race condition
        // where a waiting thread can miss the update to the 64-bit value
        // by the thread that releases the barrier and sees an update from
        // a new thread entering, thus deadlocking us.
        wait32((uint32_t*)((uint8_t*)barrier + 4), local_sense);
    }
}

#endif // __LH_ATOMICS_H_
