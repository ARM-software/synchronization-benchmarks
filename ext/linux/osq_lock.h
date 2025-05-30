/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Based on Linux kernel 4.16.10
 * https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
 * /commit/?h=v4.16.10&id=b3fdf8284efbc5020dfbd0a28150637189076115
 *
 * Description:
 *
 *      This workload implements kernel 'optimistic spin queue' derived from mcs
 *      lock.  Tunable unqueue_retry times and max_backoff_sleep duration have
 *      also been added to simulate need_resched() condition and unqueue current
 *      cpu node from spinning queue and put to sleep.
 *
 * Changes from Linux kernel osq_lock.c
 *
 *      The original DEFINE_PER_CPU_SHARED_ALIGNED(struct optimistic_spin_node,
 *      osq_node) is modified to global_osq_nodepool_ptr, a pointer to
 *      cache-line aligned memory allocated externally in do_alloc(), which may
 *      use a hugepage.  The osq lock queue struct itself is declared as a
 *      global variable, which substitutes for the upper level mutex lock
 *      struct indicated by lock pointer.  Therefore, we don't need to get the
 *      lock pointer from lock_acquire() and lock_release() interface.  The
 *      spinning node structure is located from global_osq_nodepool_ptr with
 *      thread_num used as the index.  The tail of osq_lock is accessed by
 *      global_osq directly.
 *
 *      We haven't changed the algorithm except adding unqueue_retry and max_
 *      sleep_us as optional backoff sleep to mimic kernel rescheduling events.
 *      By default we essentially disable unqueue_retry and backoff sleep so
 *      that osq_lock performance is more stable and similar to mcs queue spin
 *      lock.
 *
 * Internals:
 *
 *      In order to port osq_lock from kernel space to user space, we added
 *      lk_barrier.h and lk_cmpxchg.h to synchronization-benchmarks/ext/linux/
 *      include. Because there are some special gcc options to restrict compiler
 *      from allocating x16/x17 registers in arch/arm64/lib/Makefile for
 *      atomic_ll_sc.o, and our osq_lock.h included from lockhammer.c will not
 *      generate any other separate object file, we have to modify cmpxchg.h
 *      and change cmpxchg LLSC/LSE implementation for aarch64.
 *
 *      Kernel arm64 cmpxchg.h supports both LLSC (load-link/store-conditional)
 *      and LSE (Armv8.1 large system extension) via dynamic binary patching.
 *      If CONFIG_AS_LSE and CONFIG_ARM64_LSE_ATOMICS have been enabled, kernel
 *      will use the Armv8.1 CAS instruction to implement the compare and swap
 *      function. This inline function has 3 instructions mov/cas/mov, which
 *      will be overwritten during system boot up if the CPU doesn't support
 *      Armv8.1 LSE. The 3 new instructions are bl/nop/nop. The branch and link
 *      instruction will redirect program flow to Armv8.0 LLSC function without
 *      saving any of the caller's local registers. These registers are
 *      guaranteed to be safe because LLSC function in atomic_ll_sc.o only uses
 *      x16/x17 and LSE caller doesn't use x16/x17.
 *
 *      Since lockhammer doesn't have runtime cpu detection, to use LSE is
 *      selected by passing USE_LSE=1 to the lockhammer Makefile.  Therefore
 *      our new cmpxchg is also statically defined without branch and link or
 *      binary patching. LLSC and LSE cmpxchg will share the same interface but
 *      use different assembly code and intrinsic function calls.
 *
 * Workings:
 *
 *      osq_lock works similar to mcs spinlock except the optional unqueue path.
 *      Linux kernel qspinlock is slightly different than original mcs spinlock.
 *
 * Tuning Parameters:
 *
 *      osq_lock tuning parameters are optional.  They are specified on the
 *      lockhammer command line after lockhammer's own flags and separated from
 *      them by "--".  These osq_lock_flags apply to all iterations.
 *
 *          lh_osq_lock  lockhammer_flags ...  --  osq_lock_flags ...
 *
 *
 *      -u unqueue_retries                 Default: 2000000000 (2 billion)
 *          Max spin retries before going to unqueue path.
 *
 *      -S cpu:backoff_wait_us
 *          For the thread that runs on the specified cpu, wait for the
 *          specified backoff_wait_us microseconds on osq_lock unqueue.
 *          Overrides the value computed from -s max_backoff_wait_us * rand().
 *
 *      -s max_backoff_wait_us             Default: 0 us
 *          For each thread on cpus that do not have -S cpu:backoff_wait_us
 *          specified, the maximum backoff wait time in microseconds after an
 *          unqueue before another osq_lock() acquisition attempt.  The actual
 *          wait time is determined at init by choosing a random duration on
 *          the interval [0, max_backoff_wait_us), and waits for that same
 *          amount of time after each unqueue.  The wait is now implemented
 *          using the blackhole function to avoid making a nanosleep syscall.
 *
 *      -R random_seed                     Default: 0 (use seconds since epoch)
 *          Specify a random seed to use.  If not specified or if 0 is
 *          specified, then the number of seconds since the epoch is used.
 *          Affects the computed backoff_wait_us = rand() * max_backoff_wait_us.
 *
 *      -D cpu:delay0[,delay1[,delay2[,delay3]]]
 *          For the thread that runs on the specified cpu, at the callsite
 *          corresponding to the delay parameter, calls CPU_RELAX() the delay
 *          number of times.  See comment at the bottom of this file for a
 *          function callstack outline showing the locations of the 4
 *          callsites.  Note: CPU_RELAX is re-implemented in osq_lock.h, so
 *          cpu_relax from ext/linux/include/lk_atomics.h is not being used.
 *          This flag needs CPU_RELAX_PARAMETERIZED_DELAY to be enabled below.
 */

#ifndef __LINUX_OSQ_LOCK_H
#define __LINUX_OSQ_LOCK_H


//#define OSQ_LOCK_COUNT_LOOPS           // enable to count per-cpu loop iterations; also edit src/report.c to enable
//#define CPU_RELAX_PARAMETERIZED_DELAY  // enable osq_lock_cpu_relax() that can repeatedly call cpu_relax()


/* redefine initialize_lock and parse_test_args with local functions */
#ifdef initialize_lock
#undef initialize_lock
#endif
#define initialize_lock(p_lock, pinorder, num_threads) osq_lock_init(p_lock, pinorder, num_threads)

#ifdef parse_test_args
#undef parse_test_args
#endif
#define parse_test_args(args, argc, argv) osq_parse_args(args, argc, argv)


// these are also used in lk_atomics.h
#ifdef CPU_RELAX_PARAMETERIZED_DELAY
#define CPU_RELAX(D) osq_lock_cpu_relax(D)
#else
#define CPU_RELAX(D) cpu_relax()
#endif

#ifdef OSQ_LOCK_COUNT_LOOPS
#define INCREMENT_COUNTER(x) ((*(x))++)
#else
#define INCREMENT_COUNTER(x)
#endif


#include <inttypes.h>
#include <stdbool.h>
#include "atomics.h"
#include "cpu_relax.h"
#include "lk_atomics.h"
#include "lk_cmpxchg.h"
#include "lk_barrier.h"

#define ATOMIC_INIT(i)    { (i) }

#ifndef LONG_LONG_MAX
#define LONG_LONG_MAX   LLONG_MAX
#endif


// thread_to_sleep_time_us[] and thread_to_relax[] are remade each time
// osq_lock_init() is run because different pinorders may place a thread on
// different CPUs.

#define OSQ_DEFAULT_SLEEP_TIME -1
#define OSQ_DEFAULT_RELAX_ITER 1


// For thread i, thread_to_sleep_time_us[i] contains the duration in usecs
// between attempts at aquiring the lock after unqueue_retries times.
long * thread_to_sleep_time_us;


// thread_to_relax[i] has the number of cpu_relax() iterations to run for the thread
// in osq_lock_cpu_relax() at the four callsites:
//     [0] = osq_lock   - fast-path loop
//     [1] = osq_lock   - unqueue step A loop
//     [2] = osq_lock   - unqueue step B osq_wait_next loop
//     [3] = osq_unlock - osq_wait_next loop
long (*thread_to_relax)[4];


// osq_per_cpu_parameters is filled by osq_parse_args() to record each
// per-cpu -S and/or -D parameter.  This is done only once.
struct {
    size_t num_entries;
    struct {
        long cpu;
        long sleep_time_us; // for thread_to_sleep_time_us[]
        long iters[4];      // for thread_to_relax
    } * params;
} osq_per_cpu_parameters = {
    .num_entries = 0,
    .params = NULL,
};


static inline void osq_lock_cpu_relax (long relax) {
    while (relax--) {
        __cpu_relax();
    }
}


/*
 * An MCS like lock especially tailored for optimistic spinning for sleeping
 * lock implementations (mutex, rwsem, etc).
 *
 * Using a single mcs node per CPU is safe because sleeping locks should not be
 * called from interrupt context and we have preemption disabled while
 * spinning.
 *
 * The important elements are in the first 64 bytes of each optimistic_spin_node.
 * This code previously set SPIN_NODE_ALIGNMENT to 128 to avoid false sharing
 * on CPUs that had 128-byte cache line lengths, but 64 bytes is the common cache
 * line length now.  This should match the value from get_ctr_erg_bytes().
 *
 * cpu_to_node() returns a pointer to each optimistic_spin_node element from
 * the linear virtual address for the requested CPU.
 */

#define SPIN_NODE_ALIGNMENT 64UL

struct optimistic_spin_node {
    struct optimistic_spin_node *next;
    struct optimistic_spin_node *prev;
    long locked; /* 1 if lock acquired */
    long cpu; /* encoded CPU # + 1 value */
    unsigned long sleep_us; /* random sleep in us */
    unsigned long sleep_blackhole;
} __attribute__ ((aligned (SPIN_NODE_ALIGNMENT)));

struct optimistic_spin_queue {
    /*
     * Stores an encoded value of the CPU # of the tail node in the queue.
     * If the queue is empty, then it's set to OSQ_UNLOCKED_VAL.
     */
    atomic_t tail;
} __attribute__ ((aligned (SPIN_NODE_ALIGNMENT)));

/* 0 means thread unlocked, 1~N represents each individual thread on core 1~N */
#define OSQ_UNLOCKED_VAL (0)

/*
 * maximum backoff sleep time in microseconds (default 0us, no sleep)
 * linux kernel scheduling intrinsic delay is less than 7us, however
 * we need to tune this parameter for different machines.
 * http://www.brendangregg.com/blog/2017-03-16/perf-sched.html
 */
#define MAX_BACKOFF_SLEEP_US 0

/*
 * Default unqueue retry times, most system spins at least 500~1000 times
 * before unqueue from optimistic_spin_queue. Default large value simply
 * disables unqueue path and make osq_lock more like mcs_queue_spinlock.
 */
#define DEFAULT_UNQUEUE_RETRY 2000000000

/* Init macro and function. */
#define OSQ_LOCK_UNLOCKED { ATOMIC_INIT(OSQ_UNLOCKED_VAL) }


/* Newly added global variables used by osq_lock algorithm */
static long long unqueue_retry;
static long long max_sleep_us;
static struct optimistic_spin_queue global_osq;
static struct optimistic_spin_node *global_osq_nodepool_ptr;
static unsigned int srand_seed = 0;

struct osq_lock_parameters_t {
    unsigned long verbose;
} osq_lock_parameters = {
    .verbose = VERBOSE_NONE,
};


static void osq_parse_multicpu_parameter (char opt, char * optarg) {
    char * pc;
    char * saveptr;
    size_t cpu;
    const char * param_name;
    size_t max_params;

    switch (opt) {
        case 'D':
            param_name = "cpu_relax_iters";
            max_params = 4;
            break;
        case 'S':
            param_name = "sleep_time_us";
            max_params = 1;
            break;
        default:
            fprintf(stderr, "should never have gotten here\n");
            exit(-1);
            break;
    }

    // parse for the cpu number
    pc = strtok_r(optarg, ":", &saveptr);

    if (pc == NULL) {
        fprintf(stderr, "expected -%c %s to be in the form cpu:%s\n", opt, optarg, param_name);
        exit(-1);
    }

    cpu = strtol(pc, NULL, 0);

    if (osq_lock_parameters.verbose)
        printf("osq_parse_multicpu_parameter: parsing -%c %s for cpu = %zu\n", opt, optarg, cpu);


    // find the entry for the cpu
    int found = 0;
    size_t i = 0;

    for (; i < osq_per_cpu_parameters.num_entries; i++) {
        if (osq_per_cpu_parameters.params[i].cpu == cpu) {
            found = 1;
            break;
        }
    }

    if (!found) {
        osq_per_cpu_parameters.params = reallocarray(
            osq_per_cpu_parameters.params,
            osq_per_cpu_parameters.num_entries + 1,
            sizeof(osq_per_cpu_parameters.params[0])
            );

        if (osq_per_cpu_parameters.params == NULL) {
            fprintf(stderr, "ERROR: reallocarray on osq_per_cpu_parameters.params failed\n");
            exit(-1);
        }

        osq_per_cpu_parameters.params[i].iters[0] = 1;
        osq_per_cpu_parameters.params[i].iters[1] = 1;
        osq_per_cpu_parameters.params[i].iters[2] = 1;
        osq_per_cpu_parameters.params[i].iters[3] = 1;
        osq_per_cpu_parameters.params[i].sleep_time_us = -1;
        osq_per_cpu_parameters.params[i].cpu = cpu;
        osq_per_cpu_parameters.num_entries++;
    }

    // parse the arglist and update osq_per_cpu_parameters
    for (size_t j = 0; j < max_params; j++) {
        pc = strtok_r(NULL, ",", &saveptr);

        if (pc == NULL) {
            break;
        }

        long val = strtoul(pc, NULL, 0);

        if (osq_lock_parameters.verbose)
            printf("token = %s, val = %ld\n", pc, val);

        switch (opt) {
            case 'D':
                osq_per_cpu_parameters.params[i].iters[j] = val;
                break;
            case 'S':
                osq_per_cpu_parameters.params[i].sleep_time_us = val;
                break;
            default:
                break;
        }

    }

}


/* Newly added additional tuning parameters for optional backoff sleep */
static void osq_parse_args(test_args_t * t, int argc, char** argv) {
    int i = 0;
    char *endptr;
    unqueue_retry = DEFAULT_UNQUEUE_RETRY;
    max_sleep_us = MAX_BACKOFF_SLEEP_US;

    /* extended options retrieved after '--' operator */
    while ((i = getopt(argc, argv, "u:s:S:R:D:vh")) != -1)
    {
        switch (i) {
          case 'u':         // maximum number of spin retries before unqueue path
            errno = 0;
            unqueue_retry = strtoll(optarg, &endptr, 10);
            if ((errno == ERANGE && (unqueue_retry == LONG_LONG_MAX))
                    || (errno != 0 && unqueue_retry == 0) || endptr == optarg) {
                fprintf(stderr, "unqueue_retry: value unsuitable "
                                "for 'long long int'\n");
                exit(1);
            }
            break;

          case 's':         // maximum interval between lock_acquire retries
            errno = 0;
            max_sleep_us = strtoll(optarg, &endptr, 10);
            if ((errno == ERANGE && (max_sleep_us == LONG_LONG_MAX))
                    || (errno != 0 && max_sleep_us == 0) || endptr == optarg) {
                fprintf(stderr, "max_sleep_us: value unsuitable "
                                "for 'long long int'\n");
                exit(1);
            } else if (max_sleep_us < 0) {
                fprintf(stderr, "max_sleep_us must be a positive integer.\n");
                exit(1);
            }
            break;

          case 'D':         // per-cpu cpu_relax delay/iterations
#ifdef CPU_RELAX_PARAMETERIZED_DELAY
            osq_parse_multicpu_parameter('D', optarg);
            break;
#else
            fprintf(stderr, "ERROR: lh_osq_lock was run with -D flag CPU_RELAX delays, but CPU_RELAX_PARAMETERIZED_DELAY is not enabled.\n");
            exit(-1);
            break;
#endif

          case 'S':         // per-cpu unqueue-to-acquire interval wait-time in microseconds
            osq_parse_multicpu_parameter('S', optarg);
            break;

          case 'R':         // specify random seed for rand()
            srand_seed = strtoumax(optarg, NULL, 0);
            break;

          case '?':
          case ':':
            if (i == '?')
                printf("option flag %s is unknown\n\n", argv[optind-1]);
            else if (i == ':')
                printf("option flag %s is missing an argument\n\n", argv[optind-1]);
          // fall-through
          case 'h':
            fprintf(stderr,
                    "osq_lock additional options after --:\n"
                    "\t[-h print this msg]\n"
                    "\t[-v copy verbosity from main]\n"
                    "\t[-u max spin retries before unqueue, default 2 billion]\n"
                    "\t[-s max unqueue sleep in microseconds, default is 0]\n"
                    "\t[-R specify random seed]\n"
#ifdef CPU_RELAX_PARAMETERIZED_DELAY
                    "\t[-D i:0[,1[,2[,3]]]] for CPUi, cpu_relax() iterations at callsites 0/1/2/3\n"
#endif
                    "\t[-S i:usecs] for CPUi, use a unqueue-to-acquire interval\n");
            exit(2);
          case 'v':
            osq_lock_parameters.verbose = t->verbose;
            break;
          default:
            fprintf(stderr, "osq_parse_args: shouldn't get here with i = %d\n", i);
            exit(-1);
        }
    }

    if (argc > optind) {
        fprintf(stderr, "ERROR: (osq_parse_args) unknown argument %s, opterr=%d\n", argv[optind], opterr);
        exit(-1);
    }

    if (osq_lock_parameters.verbose)
        for (size_t i = 0; i < osq_per_cpu_parameters.num_entries; i++) {
            printf("%zu: cpu=%ld, sleep_time_us=%ld, iters={%ld,%ld,%ld,%ld}\n",
                i,
                osq_per_cpu_parameters.params[i].cpu,
                osq_per_cpu_parameters.params[i].sleep_time_us,
                osq_per_cpu_parameters.params[i].iters[0],
                osq_per_cpu_parameters.params[i].iters[1],
                osq_per_cpu_parameters.params[i].iters[2],
                osq_per_cpu_parameters.params[i].iters[3]);
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
static inline void osq_lock_init(unsigned long * p_test_lock, int * pinorder, unsigned long num_threads)
{
    if (pinorder == NULL) {
        fprintf(stderr, "ERROR: pinorder (cpu_list) should never be a null pointer!\n"); exit(-1);
    }

    if (thread_to_sleep_time_us)
        free(thread_to_sleep_time_us);

    thread_to_sleep_time_us = malloc(sizeof(thread_to_sleep_time_us[0]) * num_threads);
    if (! thread_to_sleep_time_us) {
        fprintf(stderr, "ERROR: thread_to_sleep_time_us malloc failure\n"); exit(-1);
    }

    if (thread_to_relax)
        free(thread_to_relax);

    thread_to_relax = malloc(sizeof(thread_to_relax[0]) * num_threads);
    if (! thread_to_relax) {
        fprintf(stderr, "ERROR: thread_to_relax malloc failure\n"); exit(-1);
    }

    for (size_t thread_number = 0; thread_number < num_threads; thread_number++) {

        // initialize the per-thread parameters
        thread_to_sleep_time_us[thread_number] = OSQ_DEFAULT_SLEEP_TIME;

        for (size_t k = 0; k < 4; k++)
            thread_to_relax[thread_number][k] = OSQ_DEFAULT_RELAX_ITER;

        // update per-thread parameters for CPUs specified using the -S or -D flags
        long cpu = pinorder[thread_number];

        for (size_t i = 0; i < osq_per_cpu_parameters.num_entries; i++) {
            if (osq_per_cpu_parameters.params[i].cpu == cpu) {
                thread_to_sleep_time_us[thread_number] = osq_per_cpu_parameters.params[i].sleep_time_us;
                for (size_t k = 0; k < 4; k++)
                    thread_to_relax[thread_number][k] = osq_per_cpu_parameters.params[i].iters[k];
            }
        }

    }

    if (osq_lock_parameters.verbose >= VERBOSE_MORE) {
        printf("lh_osq_lock: sizeof(struct optimistic_spin_node) = %zu\n",
                sizeof(struct optimistic_spin_node));

        for (size_t i = 0; i < num_threads; i++) {
            long cpu = pinorder[i];
            printf("lh_osq_lock: thread_to_sleep_time_us[thread %zu/cpu %ld]: %ld\n",
                i, cpu, thread_to_sleep_time_us[i]);
        }

        for (size_t i = 0; i < num_threads; i++) {
            long cpu = pinorder[i];
            printf("lh_osq_lock: thread_to_relax[thread %zu/cpu %ld]: %ld %ld %ld %ld\n",
                i, cpu, thread_to_relax[i][0], thread_to_relax[i][1],
                thread_to_relax[i][2], thread_to_relax[i][3]);
        }

        printf("lh_osq_lock: max_sleep_us = %lld\n", max_sleep_us);
    }

    // Note:  osq_lock does not use the lock pointer passed into osq_lock_acquire()
    // and osq_lock_release().  Instead, each thread gets an optimistic_spin_node
    // from the array pointed to by global_osq_nodepool_ptr.  global_osq_nodepool_ptr
    // is set to the memory allocated by the lockhammer harness through p_test_lock
    // passed into this function.

    size_t size = (num_threads + 1) * sizeof(struct optimistic_spin_node);

    if (size % SPIN_NODE_ALIGNMENT) {
        fprintf(stderr, "lh_osq_lock: ERROR: size = %zu, is not a multiple of %zu\n",
                size, SPIN_NODE_ALIGNMENT);
        exit(-1);
    }

    /*
     * Each cpu core will have its own spinning node, aligned to SPIN_NODE_ALIGNMENT.
     */

    global_osq_nodepool_ptr = (struct optimistic_spin_node *) p_test_lock;

    if (osq_lock_parameters.verbose >= VERBOSE_MORE)
        printf("global_osq_nodepool_ptr = %p\n", global_osq_nodepool_ptr);

    if (global_osq_nodepool_ptr == NULL) exit(errno);

    memset(global_osq_nodepool_ptr, 0, size);

    /*
     * If osq spins more than unqueue_retry times, the spinning cpu may backoff
     * and sleep for 1 ~ 10 microseconds (on average 5 microseconds).  Each
     * spinning thread uses a randomly selected backoff sleep time.  The maximum
     * sleep time given using the '-s' flag, but the default is to disable
     * this sleep (MAX_BACKOFF_SLEEP_US = 0).
     */

    if (max_sleep_us > 0) {
        if (srand_seed == 0) {
            srand_seed = time(0);   // seconds since the epoch
        }

        if (osq_lock_parameters.verbose >= VERBOSE_MORE)
            printf("lh_osq_lock: srand_seed = %u\n", srand_seed);

        srand(srand_seed);
    }

    for (size_t i = 0; i < num_threads; i++) {
        char * random_string = "";
        long sleep_time_us = thread_to_sleep_time_us[i];

        if (sleep_time_us != OSQ_DEFAULT_SLEEP_TIME) {
            global_osq_nodepool_ptr[i].sleep_us = sleep_time_us;
        } else if (max_sleep_us > 0) {
            global_osq_nodepool_ptr[i].sleep_us = rand() % max_sleep_us + 1;
            random_string = " (randomly selected)";
        } else {
            global_osq_nodepool_ptr[i].sleep_us = 0;
        }

        if (osq_lock_parameters.verbose >= VERBOSE_MORE)
            printf("lh_osq_lock: global_osq_nodepool_ptr[thread %zu].sleep_us = %lu%s\n",
                    i, global_osq_nodepool_ptr[i].sleep_us, random_string);
    }

    /* Initialize global osq tail indicater to OSQ_UNLOCKED_VAL (0: unlocked) */
    atomic_set(&global_osq.tail, OSQ_UNLOCKED_VAL);
}

static void osq_lock_compute_blackhole_interval(unsigned long thread_number, double tickspns, unsigned long run_on_this_cpu, unsigned long numtries) {
    unsigned long sleep_us = global_osq_nodepool_ptr[thread_number].sleep_us;
    unsigned long sleep_blackhole = sleep_us ? calibrate_blackhole(tickspns * sleep_us * 1000, 0, TOKENS_MAX_HIGH, thread_number, numtries) : 0;

    global_osq_nodepool_ptr[thread_number].sleep_blackhole = sleep_blackhole;

    unsigned long cpu = run_on_this_cpu;

    if (osq_lock_parameters.verbose >= VERBOSE_MORE)
        printf("lh_osq_lock:  thread %lu cpu %lu: tickspns = %f, sleep_us = %lu, sleep_blackhole = %lu\n",
                thread_number, cpu, tickspns, sleep_us, sleep_blackhole);
}

// ------------------- lock code starts here -------------------

#if 0
// this function is currently unused.
static inline bool osq_is_locked(struct optimistic_spin_queue *lock)
{
    return atomic_read(&lock->tail) != OSQ_UNLOCKED_VAL;
}
#endif

/*
 * Value 0 represents "no CPU" or "unlocked", thus the encoded value will be
 * the CPU number incremented by 1.
 */
static inline int encode_cpu(int cpu_nr)
{
    return cpu_nr + 1;
}

#if 0
// this function is currently unused.
static inline int node_to_cpu(struct optimistic_spin_node *node)
{
    return node->cpu - 1;
}
#endif

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
#ifdef OSQ_LOCK_COUNT_LOOPS
static inline struct optimistic_spin_node *
osq_wait_next(struct optimistic_spin_queue *lock,   // aka global_osq
          struct optimistic_spin_node *node,        // aka us
          struct optimistic_spin_node *prev,        // aka prev, the previous tail of the queue
          unsigned long thread_number,              // our thread number
          unsigned long * counter,                  // pointer to loop counter
          long relax_delay)
#else
static inline struct optimistic_spin_node *
osq_wait_next(struct optimistic_spin_queue *lock,   // aka global_osq
          struct optimistic_spin_node *node,        // aka us
          struct optimistic_spin_node *prev,        // aka prev, the previous tail of the queue
          unsigned long thread_number,              // our thread number
          long relax_delay)
#endif
{
    struct optimistic_spin_node *next = NULL;
    int curr = encode_cpu(thread_number);

    /*
     * If there is a prev node in queue, then the 'old' value will be
     * the prev node's CPU #, else it's set to OSQ_UNLOCKED_VAL since if
     * we're currently last in queue, then the queue will then become empty.
     */
    int old = prev ? prev->cpu : OSQ_UNLOCKED_VAL;

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

        CPU_RELAX(relax_delay);

        INCREMENT_COUNTER(counter);
    }

    return next;
}


#ifdef OSQ_LOCK_COUNT_LOOPS
static bool osq_lock(uint64_t *osq, unsigned long thread_number, unsigned long * osq_lock_wait_next_spins, unsigned long * osq_lock_locked_spins, unsigned long * osq_lock_unqueue_spins)
#else
static bool osq_lock(uint64_t *osq, unsigned long thread_number)
#endif
{
    (void) osq; // osq is unused; global_osq_nodepool_ptr should have the same value.

    /* each thread spins on one optimistic_spin_node */
    struct optimistic_spin_node *node = global_osq_nodepool_ptr + thread_number;

    /* optimistic_spin_queue points to the current osq tail */
    struct optimistic_spin_queue *lock = &global_osq;
    struct optimistic_spin_node *prev, *next;
    int curr = encode_cpu(thread_number);
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

    old = atomic_xchg(&lock->tail, curr);   // _unconditionally_ put our cpu into tail.
    if (old == OSQ_UNLOCKED_VAL)            // if tail _was_ empty, then we have acquired the lock.
        return true;

    // if we are here, then there is already someone else ahead of us in the queue.

    // prev is a pointer to the node of the cpu 'old' that obtained the lock before us

    prev = cpu_to_node(old);
    node->prev = prev;

    /*
     * osq_lock()               unqueue
     *
     * node->prev = prev        osq_wait_next()
     * WMB                      MB
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

#if defined(USE_SMP_COND_LOAD_RELAXED)
        /*
         * Wait to acquire the lock or cancellation. Note that need_resched()
         * will come with an IPI, which will wake smp_cond_load_relaxed() if it
         * is implemented with a monitor-wait. vcpu_is_preempted() relies on
         * polling, be careful.
         */

    // INCREMENT_COUNTER(osq_lock_locked_spins) is inside include/lk_atomics.h:smp_cond_load_relaxed

    if (smp_cond_load_relaxed(&node->locked, VAL || (++back_off > unqueue_retry)))
        return true;
#else
    while (!READ_ONCE(node->locked)) {

        /*
         * TODO: Need to better emulate kernel rescheduling in user space.
         * Because we cannot use need_resched() in user space, we simply
         * add a upper limit named unqueue_retry (-s max_backoff_wait_us)
         * to mimic need_resched() or the per-cpu fixed backoff_wait_us (-S
         * cpu:backoff_wait_us).  If this limit has been exceeded by
         * back_off times, we will jump to unqueue path and remove the
         * spinning node from global osq.
         */

        /*
         * If we need to reschedule bail... so we can block.
         * Use vcpu_is_preempted() to avoid waiting for a preempted
         * lock holder.
         */

        //if (need_resched() || vcpu_is_preempted(node_to_cpu(node->prev)))

        if (++back_off > unqueue_retry) /* DEFAULT_UNQUEUE_RETRY is 2 billion */
            goto unqueue;

        CPU_RELAX(thread_to_relax[thread_number][0]);

        INCREMENT_COUNTER(osq_lock_locked_spins);
    }
    return true;    // we got the lock before unqueue_retry number of times

unqueue:
#endif

    //
    // If we are here, we polled node->locked for unqueue_retry number of times
    // and found it was always 0, i.e. we never won the lock. so we are
    // attempting to give up.
    //
    // step A = remove us from prev's ->next if it points to us.  if we somehow
    // get granted ->locked=1, then abort at this step (because we obtained the
    // lock).  for loop continues if prev's -> next is not us, or we are not
    // serendiptiously locked.
    //
    // step B = our prev's ->next WAS pointed to us and we cleared it to NULL
    // successfully in step A.  Now, using osq_wait_next, we repeatedly try to
    // clear us from the tail of the lock qeueue if we are the tail of the queue
    // (in which case osq_wait_next returns NULL and we return false from
    // osq_lock()), or we clear our ->next and osq_wait_next returns that
    // pointer that was in our ->next for step C.
    //
    // step C = assign  next->prev <---  prev, and assign prev->next to next,
    // basically removing us from the link.  finally return false.
    //

    /*
     * Step - A  -- stabilize @prev
     *
     * Undo our @prev->next assignment; this will make @prev's
     * unlock()/unqueue() wait for a next pointer since @lock points to us
     * (or later).
     */

    for (;;) {     // make ourselves not in the queue (prev does not point to us)
        if (prev->next == node &&                      // if prev->next is us (node)
            cmpxchg(&prev->next, node, NULL) == node)  // then try to clear it out
            break;

        /*
         * We can only fail the cmpxchg() racing against an unlock(),
         * in which case we should observe @node->locked becoming
         * true.
         */
        if (smp_load_acquire(&node->locked))
            return true;

        CPU_RELAX(thread_to_relax[thread_number][1]);

        INCREMENT_COUNTER(osq_lock_unqueue_spins);

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

#ifdef OSQ_LOCK_COUNT_LOOPS
    next = osq_wait_next(lock, node, prev, thread_number,
                         osq_lock_wait_next_spins,
                         thread_to_relax[thread_number][2]);
#else
    next = osq_wait_next(lock, node, prev, thread_number,
                         thread_to_relax[thread_number][2]);
#endif
    if (!next)
        return false;

    /*
     * Step - C -- unlink
     *
     * @prev is stable because it's still waiting for a new @prev->next
     * pointer, @next is stable because our @node->next pointer is NULL and
     * it will wait in Step-A.
     */

    WRITE_ONCE(next->prev, prev);  // we did not get the lock, and we were not at end of queue
    WRITE_ONCE(prev->next, next);  // so take us out of the queue

    return false;
}


#ifdef OSQ_LOCK_COUNT_LOOPS
void osq_unlock(uint64_t * osq, unsigned long thread_number, unsigned long * osq_unlock_wait_next_spins)
#else
void osq_unlock(uint64_t * osq, unsigned long thread_number)
#endif
{
    (void) osq; // osq is unused, but global_osq_nodepool_ptr should point to the same address.

    /* optimistic_spin_queue stores the current osq tail globally */
    struct optimistic_spin_queue *lock = &global_osq;
    struct optimistic_spin_node *node, *next;
    int curr = encode_cpu(thread_number);

    /*
     * Fast path for the uncontended case.
     */

    // This writes OSQ_UNLOCKED_VAL to the tail if this node is the tail.  If
    // this node isn't the tail, then the cmpxchg fails.

    if (atomic_cmpxchg_release(&lock->tail, curr,
                OSQ_UNLOCKED_VAL) == curr) {
        return;
    }

    /*
     * Second most likely case.
     * If there is a next node, notify it.
     */

    node = global_osq_nodepool_ptr + thread_number;     // node = us
    next = xchg(&node->next, NULL);   // unconditionally clear our node->next.
    if (next) {                       // if there was a node->next, set its locked to 1.
        WRITE_ONCE(next->locked, 1);  //     (i.e. the next node now has the lock.)
        return;
    }

    /*
     * Wait for another stable next, or get NULL if the queue is empty.
     */

#ifdef OSQ_LOCK_COUNT_LOOPS
    next = osq_wait_next(lock, node, NULL, thread_number,
                         osq_unlock_wait_next_spins,
                         thread_to_relax[thread_number][3]);
#else
    next = osq_wait_next(lock, node, NULL, thread_number,
                         thread_to_relax[thread_number][3]);
#endif
    if (next)
        WRITE_ONCE(next->locked, 1);
}

// -----------------------------------------------------------------------------

#ifdef OSQ_LOCK_COUNT_LOOPS
unsigned long osq_lock_acquire (uint64_t * p_test_lock, unsigned long threadnum, unsigned long * osq_lock_wait_next_spins, unsigned long * osq_lock_locked_spins, unsigned long * osq_lock_unqueue_spins, unsigned long * osq_lock_acquire_backoffs)
#else
unsigned long osq_lock_acquire (uint64_t * p_test_lock, unsigned long threadnum)
#endif
{
    /*
     * Note: The linux kernel implements additional mutex slow path in mutex.c
     * __mutex_lock_common() function. We may create another test which
     * combines osq_lock and mutex_lock_common. However, this test only benchmarks
     * osq_lock itself. The osq_lock is different from mcs_queue_spinlock
     * because of tunable unqueue path and backoff sleep time.
     */
    unsigned long sleep_blackhole = global_osq_nodepool_ptr[threadnum].sleep_blackhole;

#ifdef OSQ_LOCK_COUNT_LOOPS
    while (!osq_lock(p_test_lock, threadnum, osq_lock_wait_next_spins, osq_lock_locked_spins, osq_lock_unqueue_spins)) {
#else
    while (!osq_lock(p_test_lock, threadnum)) {
#endif
        /*
         * If still cannot acquire the lock after spinning for unqueue_retry
         * times, try to backoff for a predetermined number of microseconds
         * specified by parameter '-s'.  The default maximum sleep time is 0us.
         * Then attempt to reacquire the lock again infinitely until success.
         *
         * This behaves similar to kernel mutex with fine tuning sleep time.
         */

        blackhole(sleep_blackhole);

        INCREMENT_COUNTER(osq_lock_acquire_backoffs);
    }
    return 1;
}


#ifdef OSQ_LOCK_COUNT_LOOPS
inline void osq_lock_release (uint64_t * p_test_lock, unsigned long threadnum, unsigned long *osq_unlock_wait_next_spins)
{
    osq_unlock(p_test_lock, threadnum, osq_unlock_wait_next_spins);
}
#else
inline void osq_lock_release (uint64_t * p_test_lock, unsigned long threadnum)
{
    osq_unlock(p_test_lock, threadnum);
}
#endif

#endif /* __LINUX_OSQ_LOCK_H */



/////////////////////////////////////////////////////////
/*  outline of osq_lock and where the instrumented counters reside cpu_to_relax[]

    osq_lock_acquire
        osq_lock
            fast-path loop:
                while ! node->locked        [[[ spins on our own node ]]]
                    osq_lock_locked_spins++     // [0] = osq_lock - fast-path loop
            unqueue path:
                    step A loop:
                        looping on prev->next to be us to clear it AND we are ! node->locked
                            [[[ spins on prev->next and our own node->locked ]]]]
                            osq_lock_unqueue_spins++    // [1] = osq_lock - unqueue step A loop
                        go to step B iff we were able to clear prev->next
                    step B loop:
                        inside osq_wait_next    [[[ spins on global tail and our own node->next ]]]
                            if we are the (tail) end of the queue, remove us from the queue. return false.
                            if we are not the end of the queue, try to clear our -> next pointer.
                            if our -> next pointer was clear:
                                osq_lock_wait_next_spins++   // [2] = osq_lock - unqueue step B osq_wait_next loop


    osq_lock_release
        osq_unlock
            osq_wait_next
                inside osq_wait_next    [[[ spins on global tail and our own node->next ]]]
                    if we are the (tail) end of the queue, remove us from the queue.
                    if we are not the end of the queue, try to clear our ->next pointer.
                    if our -> next pointer was clear:
                        osq_unlock_wait_next_spins++    // [3] = osq_unlock - osq_wait_next loop

*/


/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
