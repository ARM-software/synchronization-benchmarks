/*
 * Queued spinlock
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * (C) Copyright 2013-2015 Hewlett-Packard Development Company, L.P.
 *
 * Authors: Waiman Long <waiman.long@hp.com>
 */

/* Based on Cavuim Queued Spinlock patches applied to Linux 4.13 */
#ifdef initialize_lock
#undef initialize_lock
#endif

#define initialize_lock(lock, threads) mcs_init_locks(lock, threads)

#include "atomics.h"
#include "lk_atomics.h"

#define CONFIG_NR_CPUS		64

#define _Q_SET_MASK(type)       (((1U << _Q_ ## type ## _BITS) - 1)\
                                      << _Q_ ## type ## _OFFSET)
#define _Q_LOCKED_OFFSET        0
#define _Q_LOCKED_BITS          8
#define _Q_LOCKED_MASK          _Q_SET_MASK(LOCKED)

#define _Q_PENDING_OFFSET       (_Q_LOCKED_OFFSET + _Q_LOCKED_BITS)
#if CONFIG_NR_CPUS < (1U << 14)
#define _Q_PENDING_BITS         8
#else
#define _Q_PENDING_BITS         1
#endif
#define _Q_PENDING_MASK         _Q_SET_MASK(PENDING)

#define _Q_TAIL_IDX_OFFSET      (_Q_PENDING_OFFSET + _Q_PENDING_BITS)
#define _Q_TAIL_IDX_BITS        2
#define _Q_TAIL_IDX_MASK        _Q_SET_MASK(TAIL_IDX)

#define _Q_TAIL_CPU_OFFSET      (_Q_TAIL_IDX_OFFSET + _Q_TAIL_IDX_BITS)
#define _Q_TAIL_CPU_BITS        (32 - _Q_TAIL_CPU_OFFSET)
#define _Q_TAIL_CPU_MASK        _Q_SET_MASK(TAIL_CPU)

#define _Q_TAIL_OFFSET          _Q_TAIL_IDX_OFFSET
#define _Q_TAIL_MASK            (_Q_TAIL_IDX_MASK | _Q_TAIL_CPU_MASK)

#define _Q_LOCKED_VAL           (1U << _Q_LOCKED_OFFSET)
#define _Q_PENDING_VAL          (1U << _Q_PENDING_OFFSET)

typedef struct qspinlock {
        atomic_t        val;
} arch_spinlock_t;

struct mcs_spinlock {
        struct mcs_spinlock *next;
        int locked; /* 1 if lock acquired */
        int count;  /* nesting count, see qspinlock.c */
};

struct mcs_spinlock *mcs_pool;

void mcs_init_locks (uint64_t *lock, unsigned long cores) {
	mcs_pool = (struct mcs_spinlock *) malloc(4 * cores * sizeof(struct mcs_spinlock));
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
	int cpu = (tail >> _Q_TAIL_CPU_OFFSET) - 1;
	int idx = (tail &  _Q_TAIL_IDX_MASK) >> _Q_TAIL_IDX_OFFSET;

	return &mcs_pool[4 * cpu + idx];
}

#define _Q_LOCKED_PENDING_MASK (_Q_LOCKED_MASK | _Q_PENDING_MASK)

/*
 * By using the whole 2nd least significant byte for the pending bit, we
 * can allow better optimization of the lock acquisition for the pending
 * bit holder.
 *
 * This internal structure is also used by the set_locked function which
 * is not restricted to _Q_PENDING_BITS == 8.
 */
struct __qspinlock {
	union {
		atomic_t val;
		struct {
			u8	locked;
			u8	pending;
		};
		struct {
			u16	locked_pending;
			u16	tail;
		};
	};
};

#if _Q_PENDING_BITS == 8
/**
 * clear_pending_set_locked - take ownership and clear the pending bit.
 * @lock: Pointer to queued spinlock structure
 *
 * *,1,0 -> *,0,1
 *
 * Lock stealing is not allowed if this function is used.
 */
static __always_inline void clear_pending_set_locked(struct qspinlock *lock)
{
	struct __qspinlock *l = (void *)lock;

	WRITE_ONCE(l->locked_pending, _Q_LOCKED_VAL);
}

/*
 * xchg_tail - Put in the new queue tail code word & retrieve previous one
 * @lock : Pointer to queued spinlock structure
 * @tail : The new queue tail code word
 * Return: The previous queue tail code word
 *
 * xchg(lock, tail)
 *
 * p,*,* -> n,*,* ; prev = xchg(lock, node)
 */
static __always_inline u32 xchg_tail(struct qspinlock *lock, u32 tail)
{
	struct __qspinlock *l = (void *)lock;

	/*
	 * Use release semantics to make sure that the MCS node is properly
	 * initialized before changing the tail code.
	 */
	return (u32)xchg_release16((uint16_t *) &l->tail,
				 tail >> _Q_TAIL_OFFSET) << _Q_TAIL_OFFSET;
}

#else /* _Q_PENDING_BITS == 8 */

/**
 * clear_pending_set_locked - take ownership and clear the pending bit.
 * @lock: Pointer to queued spinlock structure
 *
 * *,1,0 -> *,0,1
 */
static __always_inline void clear_pending_set_locked(struct qspinlock *lock)
{
	atomic_add(-_Q_PENDING_VAL + _Q_LOCKED_VAL, &lock->val);
}

/**
 * xchg_tail - Put in the new queue tail code word & retrieve previous one
 * @lock : Pointer to queued spinlock structure
 * @tail : The new queue tail code word
 * Return: The previous queue tail code word
 *
 * xchg(lock, tail)
 *
 * p,*,* -> n,*,* ; prev = xchg(lock, node)
 */
static __always_inline u32 xchg_tail(struct qspinlock *lock, u32 tail)
{
	u32 old, new, val = atomic_read(&lock->val);

	for (;;) {
		new = (val & _Q_LOCKED_PENDING_MASK) | tail;
		/*
		 * Use release semantics to make sure that the MCS node is
		 * properly initialized before changing the tail code.
		 */
		old = atomic_cmpxchg_release32((uint32_t *) &lock->val, val, new);
		if (old == val)
			break;

		val = old;
	}
	return old;
}
#endif /* _Q_PENDING_BITS == 8 */

/**
 * set_locked - Set the lock bit and own the lock
 * @lock: Pointer to queued spinlock structure
 *
 * *,*,0 -> *,0,1
 */
static __always_inline void set_locked(struct qspinlock *lock)
{
	struct __qspinlock *l = (void *)lock;

	WRITE_ONCE(l->locked, _Q_LOCKED_VAL);
}


/*
 * Generate the native code for queued_spin_unlock_slowpath(); provide NOPs for
 * all the PV callbacks.
 */

static __always_inline void __pv_init_node(struct mcs_spinlock *node) { }
static __always_inline void __pv_wait_node(struct mcs_spinlock *node,
					   struct mcs_spinlock *prev) { }
static __always_inline void __pv_kick_node(struct qspinlock *lock,
					   struct mcs_spinlock *node) { }
static __always_inline u32  __pv_wait_head_or_lock(struct qspinlock *lock,
						   struct mcs_spinlock *node)
						   { return 0; }

#define pv_enabled()		false

#define pv_init_node		__pv_init_node
#define pv_wait_node		__pv_wait_node
#define pv_kick_node		__pv_kick_node
#define pv_wait_head_or_lock	__pv_wait_head_or_lock

static inline int queued_spin_trylock(struct qspinlock *lock)
{
	if (!atomic_read(&lock->val) &&
	   (atomic_cmpxchg_acquire32((uint32_t *) &lock->val, 0, _Q_LOCKED_VAL) == 0))
		return 1;
	return 0;
}

void queued_spin_lock_slowpath(struct qspinlock *lock, u32 val, unsigned long threadnum)
{
	struct mcs_spinlock *prev, *next, *node;
	u32 new, old, tail;
	int idx;

	/*
	 * wait for in-progress pending->locked hand-overs
	 *
	 * 0,1,0 -> 0,0,1
	 */
	if (val == _Q_PENDING_VAL) {
		while ((val = atomic_read(&lock->val)) == _Q_PENDING_VAL)
			cpu_relax();
	}

	/*
	 * trylock || pending
	 *
	 * 0,0,0 -> 0,0,1 ; trylock
	 * 0,0,1 -> 0,1,1 ; pending
	 */
	for (;;) {
		/*
		 * If we observe any contention; queue.
		 */
		if (val & ~_Q_LOCKED_MASK)
			goto queue;

		new = _Q_LOCKED_VAL;
		if (val == new)
			new |= _Q_PENDING_VAL;

		/*
		 * Acquire semantic is required here as the function may
		 * return immediately if the lock was free.
		 */
		old = atomic_cmpxchg_acquire32((uint32_t *) &lock->val, val, new);
		if (old == val)
			break;

		val = old;
	}

	/*
	 * we won the trylock
	 */
	if (new == _Q_LOCKED_VAL)
		return;

	/*
	 * we're pending, wait for the owner to go away.
	 *
	 * *,1,1 -> *,1,0
	 *
	 * this wait loop must be a load-acquire such that we match the
	 * store-release that clears the locked bit and create lock
	 * sequentiality; this is because not all clear_pending_set_locked()
	 * implementations imply full barriers.
	 */
	smp_cond_load_acquire(&lock->val.counter, !(VAL & _Q_LOCKED_MASK));

	/*
	 * take ownership and clear the pending bit.
	 *
	 * *,1,0 -> *,0,1
	 */
	clear_pending_set_locked(lock);
	return;

	/*
	 * End of pending bit optimistic spinning and beginning of MCS
	 * queuing.
	 */
queue:
	node = &mcs_pool[4 * threadnum];
	idx = node->count++;
	tail = encode_tail(threadnum, idx);

	node += idx;
	node->locked = 0;
	node->next = NULL;
	pv_init_node(node);

	/*
	 * We touched a (possibly) cold cacheline in the per-cpu queue node;
	 * attempt the trylock once more in the hope someone let go while we
	 * weren't watching.
	 */
	if (queued_spin_trylock(lock))
		goto release;

	/*
	 * We have already touched the queueing cacheline; don't bother with
	 * pending stuff.
	 *
	 * p,*,* -> n,*,*
	 *
	 * RELEASE, such that the stores to @node must be complete.
	 */
	old = xchg_tail(lock, tail);
	next = NULL;

	/*
	 * if there was a previous node; link it and wait until reaching the
	 * head of the waitqueue.
	 */
	if (old & _Q_TAIL_MASK) {
		prev = decode_tail(old);
		/*
		 * The above xchg_tail() is also a load of @lock which generates,
		 * through decode_tail(), a pointer.
		 *
		 * The address dependency matches the RELEASE of xchg_tail()
		 * such that the access to @prev must happen after.
		 */
		smp_read_barrier_depends();

		WRITE_ONCE(prev->next, node);

		pv_wait_node(node, prev);
		arch_mcs_spin_lock_contended(&node->locked);

		/*
		 * While waiting for the MCS lock, the next pointer may have
		 * been set by another lock waiter. We optimistically load
		 * the next pointer & prefetch the cacheline for writing
		 * to reduce latency in the upcoming MCS unlock operation.
		 */
		next = READ_ONCE(node->next);
		if (next)
			prefetchw(next);
	}

	/*
	 * we're at the head of the waitqueue, wait for the owner & pending to
	 * go away.
	 *
	 * *,x,y -> *,0,0
	 *
	 * this wait loop must use a load-acquire such that we match the
	 * store-release that clears the locked bit and create lock
	 * sequentiality; this is because the set_locked() function below
	 * does not imply a full barrier.
	 *
	 * The PV pv_wait_head_or_lock function, if active, will acquire
	 * the lock and return a non-zero value. So we have to skip the
	 * smp_cond_load_acquire() call. As the next PV queue head hasn't been
	 * designated yet, there is no way for the locked value to become
	 * _Q_SLOW_VAL. So both the set_locked() and the
	 * atomic_cmpxchg_relaxed() calls will be safe.
	 *
	 * If PV isn't active, 0 will be returned instead.
	 *
	 */
	if ((val = pv_wait_head_or_lock(lock, node)))
		goto locked;

	val = smp_cond_load_acquire(&lock->val.counter, !(VAL & _Q_LOCKED_PENDING_MASK));

locked:
	/*
	 * claim the lock:
	 *
	 * n,0,0 -> 0,0,1 : lock, uncontended
	 * *,0,0 -> *,0,1 : lock, contended
	 *
	 * If the queue head is the only one in the queue (lock value == tail),
	 * clear the tail code and grab the lock. Otherwise, we only need
	 * to grab the lock.
	 */
	for (;;) {
		/* In the PV case we might already have _Q_LOCKED_VAL set */
		if ((val & _Q_TAIL_MASK) != tail) {
			set_locked(lock);
			break;
		}
		/*
		 * The smp_cond_load_acquire() call above has provided the
		 * necessary acquire semantics required for locking. At most
		 * two iterations of this loop may be ran.
		 */
		old = atomic_cmpxchg_relaxed32((uint32_t *) &lock->val, val, _Q_LOCKED_VAL);
		if (old == val)
			goto release;	/* No contention */

		val = old;
	}

	/*
	 * contended path; wait for next if not observed yet, release.
	 */
	if (!next) {
		while (!(next = READ_ONCE(node->next)))
			cpu_relax();
	}

	arch_mcs_spin_unlock_contended(&next->locked);
	pv_kick_node(lock, next);

release:
	/*
	 * release the node
	 */
	mcs_pool[4 * threadnum].count--;
}

unsigned long __attribute__((noinline)) lock_acquire (uint64_t *lock, unsigned long threadnum)
{
	u32 val;

	val = atomic_cmpxchg_acquire32((uint32_t *) lock, 0, _Q_LOCKED_VAL);
	if (val == 0)
		return 0;
	queued_spin_lock_slowpath((struct qspinlock *) lock, val, threadnum);

	return 1;
}


static inline void lock_release (uint64_t *lock, unsigned long threadnum)
{
	smp_store_release((u8 *) lock, 0);
}
