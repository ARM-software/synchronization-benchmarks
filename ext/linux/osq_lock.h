/* SPDX-License-Identifier: GPL-2.0 */
/* Based on Linux kernel 4.16.10
 * https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git/commit/?h=v4.16.10&id=b3fdf8284efbc5020dfbd0a28150637189076115
 */

#ifndef __LINUX_OSQ_LOCK_H
#define __LINUX_OSQ_LOCK_H

#ifdef initialize_lock
#undef initialize_lock
#endif

#ifdef parse_test_args
#undef parse_test_args
#endif

#define initialize_lock(lock, threads) osq_lock_init(lock, threads)
#define parse_test_args(args, argc, argv) osq_parse_args(args, argc, argv)

#include <stdbool.h>
#include "atomics.h"
#include "lk_atomics.h"
#include "lk_cmpxchg.h"
#include "lk_barrier.h"

#define ATOMIC_INIT(i)    { (i) }

/*
 * An MCS like lock especially tailored for optimistic spinning for sleeping
 * lock implementations (mutex, rwsem, etc).
 *
 * Use 128 bytes alignment to eliminate false sharing for various Armv8 core
 * cache line size
 */
struct optimistic_spin_node {
    struct optimistic_spin_node *next, *prev;
    int locked; /* 1 if lock acquired */
    int cpu; /* encoded CPU # + 1 value */
    int random_sleep; /* random sleep in us */
} __attribute__ ((aligned (128)));

struct optimistic_spin_queue {
    /*
     * Stores an encoded value of the CPU # of the tail node in the queue.
     * If the queue is empty, then it's set to OSQ_UNLOCKED_VAL.
     */
    atomic_t tail;
};

/* 0 means thread unlocked, 1~N represents each individual thread on core 1~N */
#define OSQ_UNLOCKED_VAL (0)

/*
 * maximum backoff sleep time in microseconds (default 0us, no sleep)
 * linux kernel scheduling intrinsic delay is less than 7us, however
 * we need to tune this parameter for different machines.
 * http://www.brendangregg.com/blog/2017-03-16/perf-sched.html
 */
#define MAX_SLEEP_US 0

/*
 * Default unqueue_retry times, most system spins at least 500~1000 times
 * before unqueue from optimistic_spin_queue. Default large value simply
 * disables unqueue path and make osq_lock more like mcs_queue_spinlock.
 */
#define UNQUEUE_RETRY 1000000000

/* Init macro and function. */
#define OSQ_LOCK_UNLOCKED { ATOMIC_INIT(OSQ_UNLOCKED_VAL) }

long long unqueue_retry;
long long max_sleep_us;
struct optimistic_spin_queue global_osq;
struct optimistic_spin_node *global_osq_nodepool_ptr;

void osq_parse_args(test_args unused, int argc, char** argv) {
    int i = 0;
    char *endptr;
    unqueue_retry = UNQUEUE_RETRY;
    max_sleep_us = MAX_SLEEP_US;

    /* extended options retrieved after '--' operator */
    while ((i = getopt(argc, argv, "u:s:")) != -1)
    {
        switch (i) {
          case 'u':
            errno = 0;
            unqueue_retry = strtoll(optarg, &endptr, 10);
            if ((errno == ERANGE && (unqueue_retry == LONG_LONG_MAX))
                    || (errno != 0 && unqueue_retry == 0) || endptr == optarg) {
                fprintf(stderr, "unqueue_retry: value unsuitable for 'long long int'\n");
                exit(1);
            }
            break;

          case 's':
            errno = 0;
            max_sleep_us = strtoll(optarg, &endptr, 10);
            if ((errno == ERANGE && (max_sleep_us == LONG_LONG_MAX))
                    || (errno != 0 && max_sleep_us == 0) || endptr == optarg) {
                fprintf(stderr, "max_sleep_us: value unsuitable for 'long long int'\n");
                exit(1);
            } else if (max_sleep_us < 0) {
                fprintf(stderr, "max_sleep_us must be a positive integer.\n");
                exit(1);
            }
            break;

          default:
            fprintf(stderr,
                    "osq_lock additional options after --:\n"
                    "\t[-h print this msg]\n"
                    "\t[-u max spin retries before unqueue, default 1000000]\n"
                    "\t[-s max unqueue sleep in microseconds, default 10]\n");
            exit(2);
        }
    }
}

/*
 * An MCS like lock especially tailored for optimistic spinning for sleeping
 * lock implementations (mutex, rwsem, etc).
 *
 * Using a single mcs node per CPU is safe because sleeping locks should not be
 * called from interrupt context and we have preemption disabled while
 * spinning.
 */
static inline void osq_lock_init(uint64_t *lock, unsigned long cores)
{
    /*
     * Allocate optimistic_spin_node from heap during main thread initialization.
     * Each cpu core will have its own spinning node, aligned to 128 bytes maximum
     * cache line, calloc will set memory to zero automatically, therefore no need
     * to bzero the nodepool.
     */
    global_osq_nodepool_ptr = calloc(cores + 1, sizeof(struct optimistic_spin_node));
    if (global_osq_nodepool_ptr == NULL) exit(errno);

    /*
     * If osq spins more than unqueue_retry times, the spinning cpu may backoff
     * and sleep for 1 ~ 10 microseconds (on average 5 microseconds). Each spinning
     * thread uses a different backoff sleep time, and we can adjust the maximum
     * sleep time by redefine the default MAX_SLEEP_US or tuning via parameter '-s'
     * By default, we disable this sleep (MAX_SLEEP_US = 0)
     *
     * Note: Avoid assigning random_sleep a negative value, otherwise usleep would
     * have a very large sleep time after implicit casting negative to uint32_t.
     */
    srand(time(0));
    for (int i = 0; i < cores; i++) {
        if (max_sleep_us > 0)
            (global_osq_nodepool_ptr + i)->random_sleep = rand() % max_sleep_us + 1;
    }

    /* Initialize global osq tail indicater to OSQ_UNLOCKED_VAL (0: unlocked) */
    atomic_set(&global_osq.tail, OSQ_UNLOCKED_VAL);
}

bool osq_lock(uint64_t *osq, unsigned long cpu_number);
void osq_unlock(uint64_t *osq, unsigned long cpu_number);

static inline bool osq_is_locked(struct optimistic_spin_queue *lock)
{
    return atomic_read(&lock->tail) != OSQ_UNLOCKED_VAL;
}

/*
 * Value 0 represents "no CPU" or "unlocked", thus the encoded value will be
 * the CPU number incremented by 1.
 */
static inline int encode_cpu(int cpu_nr)
{
    return cpu_nr + 1;
}

static inline int node_to_cpu(struct optimistic_spin_node *node)
{
    return node->cpu - 1;
}

/*
 * optimistic_spin_node for each cpu is stored linearly in main heap starting
 * from global_osq_nodepool_ptr
 */
static inline struct optimistic_spin_node * cpu_to_node(int encoded_cpu_val)
{
    int cpu_nr = encoded_cpu_val - 1;
    return global_osq_nodepool_ptr + cpu_nr;
}

/*
 * Get a stable @node->next pointer, either for unlock() or unqueue() purposes.
 * Can return NULL in case we were the last queued and we updated @lock instead.
 */
static inline struct optimistic_spin_node *
osq_wait_next(struct optimistic_spin_queue *lock,
          struct optimistic_spin_node *node,
          struct optimistic_spin_node *prev,
          unsigned long cpu_number)
{
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

/* uint64_t *osq is ignored because we use &global_osq instead */
bool osq_lock(uint64_t *osq, unsigned long cpu_number)
{
    /* each cpu core has only one thread spinning on one optimistic_spin_node */
    struct optimistic_spin_node *node = global_osq_nodepool_ptr + cpu_number;
    /* optimistic_spin_queue stores the current osq tail globally */
    struct optimistic_spin_queue *lock = &global_osq;
    struct optimistic_spin_node *prev, *next;
    int curr = encode_cpu(cpu_number);
    int old;
    long long back_off = 0;

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
         * TODO: Need to better emulate kernel rescheduling in user space.
         * Because we cannot use need_resched() in user space, we simply
         * add a upper limit named unqueue_retry to mimic need_resched().
         * If this limit has been exceeded by back_off times, we will jump
         * to unqueue path and remove the spinning node from global osq.
         */
        /*
         * If we need to reschedule bail... so we can block.
         * Use vcpu_is_preempted() to avoid waiting for a preempted
         * lock holder.
         */
        //if (need_resched() || vcpu_is_preempted(node_to_cpu(node->prev)))
        if (++back_off > unqueue_retry) /* default UNQUEUE_RETRY 1 billion */
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

    next = osq_wait_next(lock, node, prev, cpu_number);
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

/* uint64_t *osq is ignored because we use &global_osq instead */
void osq_unlock(uint64_t *osq, unsigned long cpu_number)
{
    /* optimistic_spin_queue stores the current osq tail globally */
    struct optimistic_spin_queue *lock = &global_osq;
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
     * If there is a next node, notify it.
     */
    node = global_osq_nodepool_ptr + cpu_number;
    next = xchg(&node->next, NULL);
    if (next) {
        WRITE_ONCE(next->locked, 1);
        return;
    }

    /*
     * Wait for another stable next, or get NULL if the queue is empty.
     */
    next = osq_wait_next(lock, node, NULL, cpu_number);
    if (next)
        WRITE_ONCE(next->locked, 1);
}


/* standard lockhammer lock_acquire and lock_release interfaces */
unsigned long __attribute__((noinline)) lock_acquire (uint64_t *lock, unsigned long threadnum)
{
    /*
     * Note: The linux kernel implements additional mutex slow path in mutex.c
     * __mutex_lock_common() function. We will create another workload which
     * combines osq_lock and mutex_lock_common. This workload only benchmarks
     * osq_lock itself. The osq_lock is different from mcs_queue_spinlock because
     * of tunable unqueue path and backoff sleep time.
     */
    while (!osq_lock(lock, threadnum)) {
        /*
         * If still cannot acquire the lock after spinning for unqueue_retry
         * times, try to backoff and sleep for random microseconds specified
         * by parameter '-s', by default the maximum sleep time is 0us. Then
         * reacquire the lock again infinitely until success.
         *
         * This behaves similar to kernel mutex with fine tuning sleep time.
         */
        usleep((global_osq_nodepool_ptr + threadnum)->random_sleep);
    }
    return 1;
}


static inline void lock_release (uint64_t *lock, unsigned long threadnum)
{
    osq_unlock(lock, threadnum);
}

#endif /* __LINUX_OSQ_LOCK_H */
