/* SPDX-License-Identifier: GPL-2.0 */
/* Based on Linux kernel 4.16.10
 * https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git/commit/?h=v4.16.10&id=b3fdf8284efbc5020dfbd0a28150637189076115
 */

#ifndef __LINUX_OSQ_LOCK_H
#define __LINUX_OSQ_LOCK_H

#define initialize_lock(lock, threads) osq_lock_init(lock, threads)

#include <stdbool.h>
#include "atomics.h"
#include "lk_atomics.h"
#include "lk_cmpxchg.h"
#include "lk_barrier.h"

#define ATOMIC_INIT(i)    { (i) }

/*
 * An MCS like lock especially tailored for optimistic spinning for sleeping
 * lock implementations (mutex, rwsem, etc).
 */
struct optimistic_spin_node {
    struct optimistic_spin_node *next, *prev;
    int locked; /* 1 if lock acquired */
    int cpu; /* encoded CPU # + 1 value */
};

struct optimistic_spin_queue {
    /*
     * Stores an encoded value of the CPU # of the tail node in the queue.
     * If the queue is empty, then it's set to OSQ_UNLOCKED_VAL.
     */
    atomic_t tail;
};

/* 0 means thread unlocked, 1~N represents each individual thread on core 1~N */
#define OSQ_UNLOCKED_VAL (0)

/* Init macro and function. */
#define OSQ_LOCK_UNLOCKED { ATOMIC_INIT(OSQ_UNLOCKED_VAL) }

/*
 * An MCS like lock especially tailored for optimistic spinning for sleeping
 * lock implementations (mutex, rwsem, etc).
 *
 * Using a single mcs node per CPU is safe because sleeping locks should not be
 * called from interrupt context and we have preemption disabled while
 * spinning.
 */

struct optimistic_spin_node * osq_nodepool_ptr;

static inline void osq_lock_init(uint64_t *lock, unsigned long cores)
{
    struct optimistic_spin_queue *osq_ptr = (struct optimistic_spin_queue *) lock;
    atomic_set(&osq_ptr->tail, OSQ_UNLOCKED_VAL);
    osq_nodepool_ptr = calloc(cores, sizeof(struct optimistic_spin_node));
    if (osq_nodepool_ptr == NULL) exit(errno);
}

bool osq_lock(struct optimistic_spin_queue *lock);
void osq_unlock(struct optimistic_spin_queue *lock);

static inline bool osq_is_locked(struct optimistic_spin_queue *lock)
{
    return atomic_read(&lock->tail) != OSQ_UNLOCKED_VAL;
}


/*
 * We use the value 0 to represent "no CPU", thus the encoded value
 * will be the CPU number incremented by 1.
 */
static inline int encode_cpu(int cpu_nr)
{
    return cpu_nr + 1;
}

static inline int node_to_cpu(struct optimistic_spin_node *node)
{
    return node->cpu - 1;
}

static inline struct optimistic_spin_node * cpu_to_node(int encoded_cpu_val)
{
    int cpu_nr = encoded_cpu_val - 1;
    return osq_nodepool_ptr + cpu_nr;
}

/*
 * Get a stable @node->next pointer, either for unlock() or unqueue() purposes.
 * Can return NULL in case we were the last queued and we updated @lock instead.
 */
static inline struct optimistic_spin_node *
osq_wait_next(struct optimistic_spin_queue *lock,
          struct optimistic_spin_node *node,
          struct optimistic_spin_node *prev)
{
    /* sched_getcpu() return current cpu number */
    int cpu_number = sched_getcpu();
    if (cpu_number == -1) exit(errno);

    struct optimistic_spin_node *next = NULL;
    int curr = encode_cpu(cpu_number);
    int old;

    /*
     * If there is a prev node in queue, then the 'old' value will be
     * the prev node's CPU #, else it's set to OSQ_UNLOCKED_VAL since if
     * we're currently last in queue, then the queue will then become empty.
     */
    old = prev ? prev->cpu : OSQ_UNLOCKED_VAL;

    for (;;) {
        if (atomic_read(&lock->tail) == curr &&
            atomic_cmpxchg_acquire(&lock->tail, curr, old) == curr) {
            /*
             * We were the last queued, we moved @lock back. @prev
             * will now observe @lock and will complete its
             * unlock()/unqueue().
             */
            break;
        }

        /*
         * We must xchg() the @node->next value, because if we were to
         * leave it in, a concurrent unlock()/unqueue() from
         * @node->next might complete Step-A and think its @prev is
         * still valid.
         *
         * If the concurrent unlock()/unqueue() wins the race, we'll
         * wait for either @lock to point to us, through its Step-B, or
         * wait for a new @node->next from its Step-C.
         */
        if (node->next) {
            next = xchg(&node->next, NULL);
            if (next)
                break;
        }

        cpu_relax();
    }

    return next;
}

bool osq_lock(struct optimistic_spin_queue *lock)
{
    /* sched_getcpu() return current cpu number */
    int cpu_number = sched_getcpu();
    if (cpu_number == -1) exit(errno);

    /* each core should have only one thread and one spin_node */
    struct optimistic_spin_node *node = osq_nodepool_ptr + cpu_number;
    struct optimistic_spin_node *prev, *next;
    int curr = encode_cpu(cpu_number);
    int old;
    int back_off = 0;

    node->locked = 0;
    node->next = NULL;
    node->cpu = curr;

    /*
     * We need both ACQUIRE (pairs with corresponding RELEASE in
     * unlock() uncontended, or fastpath) and RELEASE (to publish
     * the node fields we just initialised) semantics when updating
     * the lock tail.
     */
    old = atomic_xchg(&lock->tail, curr);
    if (old == OSQ_UNLOCKED_VAL)
        return true;

    prev = cpu_to_node(old);
    node->prev = prev;

    /*
     * osq_lock()            unqueue
     *
     * node->prev = prev        osq_wait_next()
     * WMB                MB
     * prev->next = node        next->prev = prev // unqueue-C
     *
     * Here 'node->prev' and 'next->prev' are the same variable and we need
     * to ensure these stores happen in-order to avoid corrupting the list.
     */
    smp_wmb();

    WRITE_ONCE(prev->next, node);

    /*
     * Normally @prev is untouchable after the above store; because at that
     * moment unlock can proceed and wipe the node element from stack.
     *
     * However, since our nodes are static per-cpu storage, we're
     * guaranteed their existence -- this allows us to apply
     * cmpxchg in an attempt to undo our queueing.
     */

    while (!READ_ONCE(node->locked)) {
        /*
         * If we need to reschedule bail... so we can block.
         * Use vcpu_is_preempted() to avoid waiting for a preempted
         * lock holder:
         */
        /* TODO: How to emulate rescheduling? */
        //if (need_resched() || vcpu_is_preempted(node_to_cpu(node->prev)))
        if (++back_off > 5000000)
            goto unqueue;

        cpu_relax();
    }
    return true;

unqueue:
    /*
     * Step - A  -- stabilize @prev
     *
     * Undo our @prev->next assignment; this will make @prev's
     * unlock()/unqueue() wait for a next pointer since @lock points to us
     * (or later).
     */

    for (;;) {
        if (prev->next == node &&
            cmpxchg(&prev->next, node, NULL) == node)
            break;

        /*
         * We can only fail the cmpxchg() racing against an unlock(),
         * in which case we should observe @node->locked becomming
         * true.
         */
        if (smp_load_acquire(&node->locked))
            return true;

        cpu_relax();

        /*
         * Or we race against a concurrent unqueue()'s step-B, in which
         * case its step-C will write us a new @node->prev pointer.
         */
        prev = READ_ONCE(node->prev);
    }

    /*
     * Step - B -- stabilize @next
     *
     * Similar to unlock(), wait for @node->next or move @lock from @node
     * back to @prev.
     */

    next = osq_wait_next(lock, node, prev);
    if (!next)
        return false;

    /*
     * Step - C -- unlink
     *
     * @prev is stable because its still waiting for a new @prev->next
     * pointer, @next is stable because our @node->next pointer is NULL and
     * it will wait in Step-A.
     */

    WRITE_ONCE(next->prev, prev);
    WRITE_ONCE(prev->next, next);

    return false;
}

void osq_unlock(struct optimistic_spin_queue *lock)
{
    /* sched_getcpu() return current cpu number */
    int cpu_number = sched_getcpu();
    if (cpu_number == -1) exit(errno);

    struct optimistic_spin_node *node, *next;
    int curr = encode_cpu(cpu_number);

    /*
     * Fast path for the uncontended case.
     */
    if (atomic_cmpxchg_release(&lock->tail, curr,
                      OSQ_UNLOCKED_VAL) == curr)
        return;

    /*
     * Second most likely case.
     */
    node = osq_nodepool_ptr + cpu_number;
    next = xchg(&node->next, NULL);
    if (next) {
        WRITE_ONCE(next->locked, 1);
        return;
    }

    next = osq_wait_next(lock, node, NULL);
    if (next)
        WRITE_ONCE(next->locked, 1);
}

unsigned long __attribute__((noinline)) lock_acquire (uint64_t *lock, unsigned long threadnum)
{
    /* TODO/BUG: need to implement mutex slow path like
     * __mutex_lock_common() and mutex_optimistic_spin()
     * in linux/kernel/locking/mutex.c
     */
    srand(time(0));
    while (!osq_lock((struct optimistic_spin_queue *)lock)) {
        usleep(rand() % 10);
    }
    return 1;
}


static inline void lock_release (uint64_t *lock, unsigned long threadnum)
{
    osq_unlock((struct optimistic_spin_queue *)lock);
}

#endif /* __LINUX_OSQ_LOCK_H */
