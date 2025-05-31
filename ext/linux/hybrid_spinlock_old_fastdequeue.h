/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

//
// NOTE: This file is currently unused
//


#ifdef initialize_lock
#undef initialize_lock
#endif

#define initialize_lock(lock, threads) mcs_init_locks(lock, threads)

#include "atomics.h"
#include "lk_atomics.h"

#define _Q_SET_MASK(type)       (((1U << _Q_ ## type ## _BITS) - 1)\
                                      << _Q_ ## type ## _OFFSET)

#define _Q_TAIL_IDX_OFFSET	0
#define _Q_TAIL_IDX_BITS	2
#define _Q_TAIL_IDX_MASK	_Q_SET_MASK(TAIL_IDX)

#define _Q_TAIL_CPU_OFFSET	(_Q_TAIL_IDX_OFFSET + _Q_TAIL_IDX_BITS)
#define _Q_TAIL_CPU_BITS	(16 - _Q_TAIL_CPU_OFFSET)
#define _Q_TAIL_CPU_MASK	_Q_SET_MASK(TAIL_CPU)
#define _Q_TAIL_OFFSET		_Q_TAIL_IDX_OFFSET

#define _Q_TAIL_MASK		(_Q_TAIL_CPU_MASK | _Q_TAIL_IDX_MASK)	

#define _Q_THRESHOLD		4

struct mcs_spinlock {
	struct mcs_spinlock *next;
	int locked;
	int count;
};

struct mcs_spinlock *mcs_pool;

void mcs_init_locks (uint64_t *lock, unsigned long cores)
{
	size_t n = 4 * cores * sizeof(struct mcs_spinlock);
	if (mcs_pool) { free(mcs_pool); }
	mcs_pool = (struct mcs_spinlock *) malloc(n);
	if (! mcs_pool) { fprintf(stderr, "malloc failed in " __FILE__ " %s\n", __func__); exit(-1); }
	memset(mcs_pool, 0, n);
}

static inline __attribute((pure)) u32 encode_tail(int cpu, int idx)
{
	u32 tail;

#ifdef CONFIG_DEBUG_SPINLOCK
	BUG_ON(idx > 3);
#endif
	tail  = (cpu + 1) << _Q_TAIL_CPU_OFFSET;
	tail |= idx << _Q_TAIL_IDX_OFFSET; /* assume < 4 */

	return tail;
}

static inline __attribute((pure)) struct mcs_spinlock *decode_tail(u32 tail)
{
	int cpu = ((tail & _Q_TAIL_CPU_MASK) >> _Q_TAIL_CPU_OFFSET) - 1;
	int idx = (tail & _Q_TAIL_IDX_MASK) >> _Q_TAIL_IDX_OFFSET;

	return &mcs_pool[4 * cpu + idx];
}

static __always_inline u32 xchg_tail(uint64_t *lock, u32 tail)
{
	/*
	 * Use release semantics to make sure that the MCS node is properly
	 * initialized before changing the tail code.
	 */
	return (u32)xchg_release16((uint16_t *) lock,
				 tail & _Q_TAIL_MASK);
}

void hybrid_spinlock_slowpath(uint64_t *lock, unsigned long threadnum)
{
	unsigned long depth = 0;
	struct mcs_spinlock *prev, *next, *node;

	u32 new, old, tail, val, ticketval;

	int idx;

	node = &mcs_pool[4 * threadnum];
	idx = node->count++;

	tail = encode_tail(threadnum, idx);

	node += idx;
	node->locked = 0;
	node->next = NULL;

	old = xchg_tail(lock, tail);
	next = NULL;

	if (old & _Q_TAIL_MASK) {
		prev = decode_tail(old);
		smp_read_barrier_depends();

		WRITE_ONCE(prev->next, node);

		arch_mcs_spin_lock_contended(&node->locked);

		next = READ_ONCE(node->next);
		if (next)
			prefetchw(next);
	}

	/* do ticket spin */
#if defined(__aarch64__)
	unsigned tmp, tmp2, tmp3;
asm volatile (
"	sevl\n"
"44:	wfe\n"
"5:	ldaxr	%w[ticket], %[lock]\n"
"	sub	%w[tmp3], %w[ticket], %w[qthresh]\n"
"	rev16	%w[tmp2], %w[tmp3]\n"
"	eor	%w[tmp3], %w[tmp2], %w[tmp3]\n"
"	add	%w[tmp2], %w[ticket], %w[ticket_inc]\n"
"	cbz	%w[tmp3], 44b\n"
"	stxr	%w[tmp3], %w[tmp2], %[lock]\n"
"	cbnz	%w[tmp3], 5b\n"
: [ticket] "=&r" (ticketval), [tmp2] "=&r" (tmp2),
  [tmp3] "=&r" (tmp3), [lock] "+Q" (*lock)
: [ticket_inc] "r" (0x01000000), [qthresh] "r" (_Q_THRESHOLD << 24)
: );
//		printf("%d enqueued on %d behind %d (serving %d)\n", ticketval >> 24, tail >> 2, old >> 2, (ticketval >> 16) & 0xFF);
	val = READ_ONCE(*lock);

	/* If we're the list tail then destroy the queue */
	while ((val & _Q_TAIL_MASK) == tail) {
		old = atomic_cmpxchg_relaxed32((u32 *) lock, val, val & ~_Q_TAIL_MASK);
		
		if (old == val)
			goto release;

		val = old;
	}

	if (!next) {
		while (!(next = READ_ONCE(node->next)))
			cpu_relax();
	}

	arch_mcs_spin_unlock_contended(&next->locked);

release:

	mcs_pool[4 * threadnum].count--;
asm volatile (
"	sevl\n"
"7:	wfe\n"
"	ldaxrb	%w[tmp3], %[serving]\n"
"	eor	%w[tmp2], %w[tmp], %w[tmp3]\n"
"	cbnz	%w[tmp2], 7b\n"
: [tmp2] "=&r" (tmp2), [tmp3] "=&r" (tmp3),
  [serving] "+Q" (*(((unsigned char *) lock) + 2))
: [tmp] "r" (ticketval >> 24)
: );
#else
#endif

}

unsigned long __attribute__((noinline)) lock_acquire (uint64_t *lock, unsigned long threadnum) {
	unsigned long depth = 0;

	u32 ticketval;

	unsigned enqueue;

#if defined(__aarch64__)
	unsigned tmp, tmp2, tmp3;
asm volatile (
"1:	ldaxr	%w[ticket], %[lock]\n"
"	add	%w[tmp2], %w[ticket], %w[ticket_inc]\n"
"	rev16	%w[enqueue], %w[ticket]\n"
"	eor	%w[enqueue], %w[enqueue], %w[ticket]\n"
"	cbnz	%w[enqueue], 2f\n"
"	stxr	%w[enqueue], %w[tmp2], %[lock]\n"
"	cbnz	%w[enqueue], 1b\n"
"2:\n"
: [ticket] "=&r" (ticketval), [tmp2] "=&r" (tmp2),
  [enqueue] "=&r" (enqueue), [lock] "+Q" (*lock)
: [ticket_inc] "r" (0x01000000), [qthresh] "r" (_Q_THRESHOLD << 24)
: );
	if (!enqueue)
		return 0; /* Ticket acquired immediately */

	depth = ((ticketval >> 24) - (ticketval >> 16)) & 0xFF;
#else
	/* TODO: Generic C implementation of fastpath */
	val = READ_ONCE(*lock);

	enqueue = val & _Q_TAIL_MASK;

	if (!(val & _Q_TAIL_MASK)
	{
	}
#endif

#if defined (__aarch64__)
asm volatile (
"	mov	%[enqueue], #1\n"
"	sub	%w[tmp3], %w[ticket], %w[qthresh]\n"
"	rev16	%w[tmp2], %w[tmp3]\n"
"	eor	%w[tmp3], %w[tmp2], %w[tmp3]\n"
"	add	%w[tmp2], %w[ticket], %w[ticket_inc]\n"
"	cbz	%w[tmp3], 4f\n"
"	and	%w[tmp3], %w[ticket], %w[qtailmask]\n"
"	cbnz	%w[tmp3], 4f\n"
"3:	ldaxr	%w[ticket], %[lock]\n"
"	sub	%w[tmp3], %w[ticket], %w[qthresh]\n"
"	rev16	%w[tmp2], %w[tmp3]\n"
"	eor	%w[tmp3], %w[tmp2], %w[tmp3]\n"
"	add	%w[tmp2], %w[ticket], %w[ticket_inc]\n"
"	cbz	%w[tmp3], 4f\n"
"	and	%w[tmp3], %w[ticket], %w[qtailmask]\n"
"	cbnz	%w[tmp3], 4f\n"
"	stxr	%w[enqueue], %w[tmp2], %[lock]\n"
"	cbnz	%w[enqueue], 3b\n"
"4:\n"
: [ticket] "+&r" (ticketval), [tmp2] "=&r" (tmp2), [tmp3] "=&r" (tmp3),
  [enqueue] "=&r" (enqueue), [lock] "+Q" (*lock)
: [ticket_inc] "r" (0x01000000), [qthresh] "r" (_Q_THRESHOLD << 24),
  [qtailmask] "i" (_Q_TAIL_MASK)
: );
#else
#endif

	if (enqueue)
	{
		hybrid_spinlock_slowpath(lock, threadnum);
	}
	else
	{
		depth = 0;
#if defined(__aarch64__)
asm volatile (
"	sevl\n"
"9:	wfe\n"
"	ldaxrb	%w[tmp3], %[serving]\n"
"	eor	%w[tmp2], %w[tmp], %w[tmp3]\n"
"	cbnz	%w[tmp2], 9b\n"
: [tmp2] "=&r" (tmp2), [tmp3] "=&r" (tmp3),
  [serving] "+Q" (*(((unsigned char *) lock) + 2))
: [tmp] "r" (ticketval >> 24)
: );
#else
#endif
	}

	return depth;
}

static inline void lock_release (uint64_t *lock, unsigned long threadnum) {
#if defined(__x86_64__)
asm volatile (
"	addw	$0x2,%[lock]\n"
: [lock] "+m" (*lock)
:
: "cc" );
#elif defined(__aarch64__)
	unsigned long tmp;
asm volatile (
"	ldrb	%w[tmp], %[lock]\n"
"	add	%w[tmp], %w[tmp], #0x1\n"
"	stlrb	%w[tmp], %[lock]\n"
: [tmp] "=&r" (tmp), [lock] "+Q" (*(((unsigned char *) lock) + 2))
:
: );

#endif
}

/* vim: set tabstop=8 shiftwidth=8 softtabstop=8 noexpandtab : */
