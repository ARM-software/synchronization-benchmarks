
/*
 * Copyright (c) 2017-2025, The Linux Foundation. All rights reserved.
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

#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <limits.h>
#include <stdint.h>
#include <sys/mman.h>
#include <linux/mman.h>

#include "alloc.h"
#include "verbose.h"
#include "lockhammer.h"
#include "atomics.h"
#include "perf_timer.h"


extern locks_t locks;

unsigned long timespec_to_ns (struct timespec * ts) {
    return 1000000000ULL * ts->tv_sec + ts->tv_nsec;
}

#ifdef __aarch64__
// The --disable-outline-atomics-lse flag is only relevant to tests built
// using USE_BUILTIN=1 USE_LSE=0 such that the __atomics intrinsics are
// implemented by the compiler as a function call to the corresponding
// routine in libgcc, which will use the load-exclusive/store-exclusive
// instructions instead of the LSE instructions.

// If USE_BUILTIN=0, then lockhammer's own assembly routines are used in
// the measurement.  (The other non-measurement uses of the routines in the
// harness will still call the libgcc routines.)

// If USE_LSE=1, then -march=armv8-a+lse is passed to the compiler when
// compiling measure.c for the test, and it will emit inlined assembly
// using LSE instructions, so this flag will not be effective for the
// measurement because the libgcc routines are not called.
// (Non-measurement uses will cal the libgcc routines.)

// This function must be here because the USE_* macros now only affect the
// compilation of measure.c and included test, while the rest of the lockhammer
// harness is built for a generic target.

void handle_disable_outline_atomics_lse(void) {
#if defined(USE_BUILTIN) && !defined(USE_LSE)
    extern unsigned char __aarch64_have_lse_atomics;
    __aarch64_have_lse_atomics = 0;
    fprintf(stderr, "INFO: --disable-outline-atomics-lse turned off using LSE in outline atomics\n");
#else
    fprintf(stderr, "ERROR: --disable-outline-atomics-lse only applies to build variant USE_BUILTIN=1 and USE_LSE=0\n");
    exit(-1);
#endif
}
#endif

#ifndef TEST_NAME
#define TEST_NAME unknown_test_name
#endif
const char * test_name = stringify(TEST_NAME);

#ifndef VARIANT_NAME
#define VARIANT_NAME unknown_variant_name
#endif
const char * variant_name = stringify(VARIANT_NAME);

// Enable the following ENABLE_CALLSITE_LABEL to apply symbols to the
// lock_acquire/lock_release callsites in hmr() to help locate them when
// looking at the disassembly.

#define ENABLE_CALLSITE_LABEL

#ifdef ENABLE_CALLSITE_LABEL
// unique_callsite_symbol makes a unique symbol so that there are no duplicate
// symbols if the callsite is unrolled into multiple callsites.

// llvm-as can not call assembly macros from outside the asm() statement
// where it is used, so define and purge the macro on each call.

#define UCS_MACRO \
    ".altmacro\n"                                    \
    ".macro unique_callsite_symbol stem,c\n"         \
    ".ifdef \\stem\\c\n"                             \
    "    unique_callsite_symbol \\stem %(\\c+1)\n"   \
    ".else\n"                                        \
    "\\stem\\c\\():\n"                               \
    ".endif\n"                                       \
    ".endm\n"

#define PURGE_UCS_MACRO \
    "\n"                                             \
    ".purgem unique_callsite_symbol"

#define CALLSITE_LABEL(x) \
    asm volatile (                                   \
        UCS_MACRO                                    \
        "unique_callsite_symbol "stringify(x)        \
        PURGE_UCS_MACRO                              \
    )

#else
#define CALLSITE_LABEL(x)
#endif

// IMPORTANT: the test-specific lock routine code is inserted by this #include
#include ATOMIC_TEST

#ifndef initialize_lock
    #define initialize_lock(p_lock, p_pinorder, num_thread)
#endif
#ifndef parse_test_args
    #define parse_test_args(args, argc, argv)
#endif
#ifndef thread_local_init
    #define thread_local_init(smtid)
#endif



// call the test-specific lock initialization routine
void measure_setup_initialize_lock (locks_t * p_locks, pinorder_t * p_pinorder) {
    // p_test_lock is passed to here thru p_locks
    // TODO: list which algorithms actually use it (most do not and just take num_cores)
    // XXX: do any tests actually use p_test_lock?
    // XXX: to rerun, each ATOMIC_TEST initialize_lock() needs to detect reinitialization,
    // e.g. to free up malloced memory

    initialize_lock (p_locks->p_test_lock, p_pinorder->cpu_list, p_pinorder->num_threads);
}



// call the test-specific argument parsing routine; to be called from main()
void measure_setup_parse_test_args (test_args_t * p_test_args, int argc, char ** argv) {
    parse_test_args(p_test_args, argc, argv);
}


// Simple linear barrier routine for synchronizing threads
static void synchronize_threads(uint64_t *barrier, unsigned long num_threads)
{
    // This works by using the barrier's bits 31..1 as a counter of the number
    // of waiting threads.  When each thread enters this function, it
    // increments the counter using fetchadd64_acquire(barrier, 2).

    //   - All threads except the last one call wait32() on the upper 32-bits
    //     of the barrier.
    //   - When the last thread to enter increments the counter, it will
    //     see that the updated counter matches num_threads - 1, and then it writes
    //     a value that clears the counter and sets bit 60, the sense bit,
    //     which is in the upper 32-bit half of the 64-bit barrier value.
    //   - All the other threads' wait32() will see that write change the upper
    //     32-bits, and will then exit this function.

    // Note that the sense bit will be set after the barrier is passed, so a
    // subsequent barrier call on the same variable will block on the sense bit
    // being set until it is cleared.

    // Also, if more than 2^31 threads are synchronizing, the counter increment
    // will overflow into the upper 32-bits, resulting in the wait32() never
    // seeing a value with just the sense bit set or cleared because the parts
    // of the 32-bit value polled will have the upper bits of the non-zero
    // counter value.  No check for this failure condition is provided as it
    // is expected to be very difficult to synchronize over 2^31 threads.

    const uint64_t SENSE_BIT_MASK = (1ULL << 60);

    uint64_t global_sense = *barrier & SENSE_BIT_MASK;
    uint64_t tmp_sense = ~global_sense & SENSE_BIT_MASK;
    uint32_t local_sense = (uint32_t)(tmp_sense >> 32);

    uint64_t old_barrier = fetchadd64_acquire(barrier, 2);
    if (old_barrier == (((num_threads - 1) * 2) | global_sense)) {
        // Make sure the store gets observed by the system. Reset count
        // to zero and flip the sense bit.
        __atomic_store_n(barrier, tmp_sense, __ATOMIC_RELEASE);
    } else {
        // Wait only on the sense bit to change.  Avoids race condition
        // where a waiting thread can miss the update to the 64-bit value
        // by the thread that releases the barrier and sees an update from
        // a new thread entering, thus deadlocking us.
        // NOTE: assumes little-endian organization so that the upper 32-bits
        // of the 64-bit barrier is at barrier + 4.
        wait32((uint32_t*)((uint8_t*)barrier + 4), local_sense);
    }
}


void NOINLINE blackhole(const unsigned long iters) {
    if (! iters) { return; }
    unsigned long temp;
#ifdef __aarch64__
    asm volatile (".p2align 4; 1: add %0, %0, -1; cbnz  %0, 1b" : "=&r" (temp) : "0" (iters));
#elif __x86_64__
    asm volatile (".p2align 4; 1: add $-1, %0; jne 1b" : "=&r" (temp) : "0" (iters) : "cc");
#endif
}

#ifdef DDEBUG
int64_t NOINLINE evaluate_loop_overhead(const unsigned long NUMTRIES)
{
    uint64_t LOOP_TEST_OVERHEAD = 0;
    const size_t outer_loop_iterations = 1000;

NO_UNROLL_LOOP
    for (size_t j = 0; j < outer_loop_iterations; j++) {
        int64_t elapsed_total = 0;
        int64_t outer_cycles_start = timer_get_counter_start();

        for (size_t i = 0; i < NUMTRIES; i++) {
            uint64_t cycles_start = timer_get_counter_start();
            uint64_t cycles_end = timer_get_counter_end();

            int64_t elapsed = MAX((int64_t)(cycles_end - cycles_start), 0);
            elapsed_total += elapsed;
        }

        int64_t outer_cycles_end = timer_get_counter_end();
        int64_t outer_elapsed_total = outer_cycles_end - outer_cycles_start;
        LOOP_TEST_OVERHEAD += (outer_elapsed_total - elapsed_total);
    }

    return LOOP_TEST_OVERHEAD / outer_loop_iterations;
}
#endif


static int64_t evaluate_timer_overhead(void)
{
    const int64_t outer_cycles_start = timer_get_counter_start();
    const int64_t outer_cycles_end = timer_get_counter_end();
    // Force measurement to 0 if it somehow goes negative
    return MAX(outer_cycles_end - outer_cycles_start, 0);
}


// evaluate_blackhole: average duration in timer ticks for blackhole() called NUMTRIES times
// This is not the duration of a single blackhole() call, it's that of NUMTRIES * blackhole().
int64_t NOINLINE evaluate_blackhole(
        const unsigned long tokens_mid, const unsigned long NUMTRIES)
{
    int64_t sum_elapsed_total = 0;
    int64_t avg_elapsed_total = 0;
    const int64_t TIMER_OVERHEAD = evaluate_timer_overhead();
#ifdef DDEBUG
    const int64_t LOOP_TEST_OVERHEAD = evaluate_loop_overhead(NUMTRIES);
#endif

NO_UNROLL_LOOP
    for (size_t j = 0; j < NUMTRIES; j++) {
        int64_t elapsed_total = 0;
#ifdef DDEBUG
        int64_t outer_cycles_start = timer_get_counter_start();
#endif

NO_UNROLL_LOOP
        for (size_t i = 0; i < NUMTRIES; i++) {
            const uint64_t cycles_start = timer_get_counter_start();
            blackhole(tokens_mid);
            const uint64_t cycles_end = timer_get_counter_end();
            elapsed_total += cycles_end - cycles_start;
        }

#ifdef DDEBUG
        const int64_t outer_cycles_end = timer_get_counter_end();
        const int64_t outer_elapsed_total = outer_cycles_end - outer_cycles_start;
        const int64_t outer_inner_diff = abs(outer_elapsed_total - elapsed_total);
#endif

        // Force measurements to zero if overhead swamps loop run time, in this
        // case we can't measure this low of a requested time accurately.
        sum_elapsed_total += MAX((int64_t)(elapsed_total - TIMER_OVERHEAD*NUMTRIES), 0);
        avg_elapsed_total = sum_elapsed_total / (j + 1);

#ifdef DDEBUG
        double percent;
        const int64_t elapsed_total_diff = abs(avg_elapsed_total - elapsed_total);

        if (outer_inner_diff > LOOP_TEST_OVERHEAD) {
            percent = outer_inner_diff / (double) LOOP_TEST_OVERHEAD;
        } else {
            percent = LOOP_TEST_OVERHEAD / (double) outer_inner_diff;
        }

        printf("outer_elapsed_total = %lu "
               "elapsed_total = %lu "
               "outer_inner_diff = %lu percent_oh = %f percent_loop = %f\n",
               outer_elapsed_total, elapsed_total, outer_inner_diff, percent,
               (double) elapsed_total_diff / avg_elapsed_total);
#endif
    }

    return avg_elapsed_total;
}


typedef struct {
    int64_t tokens;         // the value passed to blackhole()
    unsigned long ticks;    // duration in units of timer ticks
} blackhole_measurement_t;


static blackhole_measurement_t calibrate_blackhole_helper(const unsigned long target_ticks,
     const unsigned long tokens_low, const unsigned long tokens_high,
     const unsigned long thread, const unsigned long NUMTRIES, const int verbose)
{
    const unsigned long tokens_diff = tokens_high - tokens_low;
    const unsigned long tokens_mid = (tokens_diff / 2) + tokens_low;
    const unsigned long target_total_ticks = NUMTRIES * target_ticks;

#ifdef DDEBUG
    printf("thread %lu: target_ticks = %lu, target_total_ticks = %lu, "
            "tokens_low = %lu, tokens_high = %lu, tokens_diff = %lu, tokens_mid = %lu\n",
            thread, target_ticks, target_total_ticks,
            tokens_low, tokens_high, tokens_diff, tokens_mid);
#endif

    if (tokens_diff == 1) {
        // the answer is either tokens_low or tokens_high

        const unsigned long low_ticks = evaluate_blackhole(tokens_low, NUMTRIES);
        const unsigned long high_ticks = evaluate_blackhole(tokens_high, NUMTRIES);
        const long low_ticks_diff = (long) (low_ticks - target_total_ticks);
        const long high_ticks_diff = (long) (high_ticks - target_total_ticks);
        const unsigned long low_ticks_diff_abs = labs(low_ticks_diff);
        const unsigned long high_ticks_diff_abs = labs(high_ticks_diff);

        if (verbose >= VERBOSE_MORE)
            printf("thread %lu: %lu tokens_low --> %lu hwticks, low_ticks_diff = %ld; "
                    "%lu tokens_high --> %lu hwticks, high_ticks_diff = %ld%s\n",
                    thread,
                    tokens_low, low_ticks, low_ticks_diff,
                    tokens_high, high_ticks, high_ticks_diff,
                    low_ticks >= high_ticks ? "; low>=high paradox" : "");

        if (low_ticks_diff_abs < high_ticks_diff_abs) {

            if (tokens_low >= (TOKENS_MAX_HIGH-1)) {
                printf("tokens is TOKENS_MAX_HIGH or TOKENS_MAX_HIGH -1.  requested delay is too long or too short.\n");
            }

            return (blackhole_measurement_t) { .tokens = tokens_low, .ticks = low_ticks };
        }

        if (tokens_high >= (TOKENS_MAX_HIGH-1)) {
            printf("tokens is TOKENS_MAX_HIGH or TOKENS_MAX_HIGH -1.  requested delay is too long or too short.\n");
        }

        return (blackhole_measurement_t) { .tokens = tokens_high, .ticks = high_ticks };
    }

    // Measure the duration of NUMTRIES calls to blackhole().
    const unsigned long t = evaluate_blackhole(tokens_mid, NUMTRIES);

    if (verbose >= VERBOSE_MORE)
        printf("thread %lu: %lu tokens --> %lu hwticks, target_total_ticks = %lu, diff = %ld\n",
            thread, tokens_mid, t, target_total_ticks, target_total_ticks - t);

    if (t > target_total_ticks) {
        return calibrate_blackhole_helper(target_ticks, tokens_low, tokens_mid, thread, NUMTRIES, verbose);
    } else if (t < target_total_ticks) {
        return calibrate_blackhole_helper(target_ticks, tokens_mid, tokens_high, thread, NUMTRIES, verbose);
    }

    return (blackhole_measurement_t) { .tokens = tokens_mid, .ticks = t };
}

static double hwticks_to_ns(const unsigned long hwticks) {
    return 1e9 * hwticks / timer_get_timer_freq();
}

static int cmp_blackhole_measurement(const void * a, const void * b) {
    return ((blackhole_measurement_t *) a)->ticks - ((blackhole_measurement_t *) b)->ticks;
}

// calibrate_blackhole:  find the target number of 'tokens' for blackole()
// to take 'target' timer ticks to run
unsigned long calibrate_blackhole(const unsigned long target_ticks,
     const unsigned long tokens_low, const unsigned long tokens_high,
     const unsigned long thread, const unsigned long NUMTRIES, const int verbose)
{

    // Searching for a blackhole parameter that runs for a target_ticks duration
    // is not 100% reliable because their relationship is nonlinear;  sometimes
    // an incrementally larger parameter results in a shorter duration, and vice
    // versa.  Thus, using binary search in calibrate_blackhole_helper() does
    // not guarantee accuracy nor repeatability.  So try to find it a few times
    // and pick the median to improve the chances that the result is reasonable.

    const size_t num_tries = 5;
    blackhole_measurement_t results[num_tries];

    for (size_t i = 0; i < num_tries; i++) {
        if (verbose >= VERBOSE_MORE)
            printf("thread %lu: target_ticks = %lu, target_total_ticks = %lu, try = %zu\n",
                thread, target_ticks, target_ticks * NUMTRIES, i);

        results[i] = calibrate_blackhole_helper(target_ticks,
            tokens_low, tokens_high, thread, NUMTRIES, verbose);
    }

    qsort(results, num_tries, sizeof(results[0]), cmp_blackhole_measurement);

    if (verbose >= VERBOSE_MORE)
        for (size_t i = 0; i < num_tries; i++) {
            double ns = hwticks_to_ns(results[i].ticks) / NUMTRIES;
            printf("%lu: results[%zu] ticks=%lu tokens=%lu ns=%.f\n",
                    thread, i, results[i].ticks, results[i].tokens, ns);
        }

    const size_t i = num_tries / 2;  // select median result
    const double ns = hwticks_to_ns(results[i].ticks) / NUMTRIES;

    if (verbose >= VERBOSE_MORE)
        printf("%lu: returning ticks=%lu tokens=%ld ns=%.f for target_ticks=%lu\n",
            thread, results[i].ticks, results[i].tokens, ns, target_ticks);

    return results[i].tokens;
}


static double measure_blackhole_duration(unsigned long count, unsigned long hwtimer_frequency) {

    uint64_t hwtimer_start = timer_get_counter_start();
    size_t n = 1000;
    for (size_t i = 0; i < n; i++) {
        blackhole(count);
    }
    uint64_t hwtimer_stop = timer_get_counter_start();

    uint64_t hwtimer_diff = hwtimer_stop - hwtimer_start;
    double ns_n = hwticks_to_ns(hwtimer_diff);
    double ns = ns_n / n;

    return ns;
}

/* Calculate timer spin-times where we do not access the clock.
 * First calibrate the wait loop by doing a binary search around
 * an estimated number of ticks. All threads participate to take
 * into account pipeline effects of multithreading or hybrid cores.
 */
static void calibrate_timer(thread_args_t *x, const unsigned long thread, const unsigned long NUMTRIES, const int verbose)
{
    synchronize_threads(locks.p_calibrate_lock, x->num_threads);

    if (x->hold_unit == NS) {
        // Determine how many timer ticks should happen for this wait time
        unsigned long hold_ticks = (unsigned long)((double)x->hold * x->tickspns);
        // Calibrate the number of loops (tokens) we have to do for hold_ticks
        x->hold_count = calibrate_blackhole(hold_ticks, 0, TOKENS_MAX_HIGH, thread, NUMTRIES, verbose);
    } else {
        x->hold_count = x->hold / 2;  // because there are 2 instructions in blackhole()
    }

    synchronize_threads(locks.p_calibrate_lock, x->num_threads);

    if (x->post_unit == NS) {
        unsigned long post_ticks = (unsigned long)((double)x->post * x->tickspns);
        x->post_count = calibrate_blackhole(post_ticks, 0, TOKENS_MAX_HIGH, thread, NUMTRIES, verbose);
    } else {
        x->post_count = x->post / 2;  // because there are 2 instructions in blackhole()
    }

    synchronize_threads(locks.p_calibrate_lock, x->num_threads);

    double hold_ns = measure_blackhole_duration(x->hold_count, x->hwtimer_frequency);

    synchronize_threads(locks.p_calibrate_lock, x->num_threads);

    double post_ns = measure_blackhole_duration(x->post_count, x->hwtimer_frequency);

    synchronize_threads(locks.p_calibrate_lock, x->num_threads);

    x->results.hold_ns = hold_ns;
    x->results.post_ns = post_ns;

    if (x->verbose >= VERBOSE_YES)
        printf("Calibrated thread %lu on CPU %lu with hold = %lu %s (hold_count = %lu) -> %0.2f ns; post = %lu %s (post_count = %lu) -> %0.2f ns\n",
            thread, x->run_on_this_cpu,
            x->hold, x->hold_unit == NS? "ns" : "instructions", x->hold_count, hold_ns,
            x->post, x->post_unit == NS? "ns" : "instructions", x->post_count, post_ns);

    synchronize_threads(locks.p_calibrate_lock, x->num_threads);
}


#if defined(__LINUX_OSQ_LOCK_H) && defined(OSQ_LOCK_COUNT_LOOPS)

#undef lock_acquire
#define lock_acquire(lock, thread) osq_lock_acquire(lock, thread, &osq_lock_wait_next_spins, &osq_lock_locked_spins, &osq_lock_unqueue_spins, &osq_lock_acquire_backoffs)

#undef lock_release
#define lock_release(lock, thread) osq_lock_release(lock, thread, &osq_unlock_wait_next_spins)

#elif defined(__LINUX_OSQ_LOCK_H) && !defined(OSQ_LOCK_COUNT_LOOPS)

#undef lock_acquire
#define lock_acquire(lock, thread) osq_lock_acquire(lock, thread)

#undef lock_release
#define lock_release(lock, thread) osq_lock_release(lock, thread)

#endif



#ifdef PROGRESS_TICK_PROFILE
static void update_timer_tick_progress(unsigned long lock_acquires,
        unsigned long target_10p, unsigned long * __restrict__ hwtimer_10p,
        unsigned long target_25p, unsigned long * __restrict__ hwtimer_25p,
        unsigned long target_50p, unsigned long * __restrict__ hwtimer_50p,
        unsigned long target_75p, unsigned long * __restrict__ hwtimer_75p,
        unsigned long target_90p, unsigned long * __restrict__ hwtimer_90p) {

    if (lock_acquires > target_10p && *hwtimer_10p == 0) {
        *hwtimer_10p = get_raw_counter();
        return;
    }

    if (lock_acquires > target_25p && *hwtimer_25p == 0) {
        *hwtimer_25p = get_raw_counter();
        return;
    }

    if (lock_acquires > target_50p && *hwtimer_50p == 0) {
        *hwtimer_50p = get_raw_counter();
        return;
    }

    if (lock_acquires > target_75p && *hwtimer_75p == 0) {
        *hwtimer_75p = get_raw_counter();
        return;
    }

    if (lock_acquires > target_90p && *hwtimer_90p == 0) {
        *hwtimer_90p = get_raw_counter();
        return;
    }
}
#endif

typedef struct {
    thread_args_t * x;
    volatile unsigned long lock_acquires;
    volatile unsigned long total_depth;
    unsigned long thread;
    struct timespec * ptv_monot_start;
    struct timespec * ptv_start;
    unsigned long ticks_start;
    unsigned long ticks_end;

#ifdef OSQ_LOCK_COUNT_LOOPS
    unsigned long * posq_lock_wait_next_spins;
    unsigned long * posq_unlock_wait_next_spins;
    unsigned long * posq_lock_locked_spins;
    unsigned long * posq_lock_unqueue_spins;
    unsigned long * posq_lock_acquire_backoffs;
#endif

} cleanup_struct_t;

static void thread_cleanup_routine(cleanup_struct_t * pcs) {
    printf("thread_cleanup_routine called\n");

    thread_args_t *x = pcs->x;
    struct timespec tv_monot_end, tv_end;

    unsigned long ticks_end = get_raw_counter();

    clock_gettime(CLOCK_MONOTONIC, &tv_monot_end);      // wall-clock time since epoch, but XXX should use CLOCK_MONOTONIC_RAW???
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tv_end);    // CPU time spent by this thread

    if (pcs->thread == 0)
        *(x->p_start_ns) = timespec_to_ns(pcs->ptv_monot_start);

    unsigned long cputime_ns = timespec_to_ns(&tv_end) - timespec_to_ns(pcs->ptv_start);
    unsigned long walltime_ns = timespec_to_ns(&tv_monot_end) - timespec_to_ns(pcs->ptv_monot_start);

    per_thread_results_t * p = &x->results;

    p->lock_acquires = pcs->lock_acquires;

    p->cputime_ns = cputime_ns;
    p->walltime_ns = walltime_ns;

    p->hmrdepth = pcs->total_depth;

    p->hwtimer_start = pcs->ticks_start;
    p->hwtimer_end = ticks_end;

#ifdef OSQ_LOCK_COUNT_LOOPS
    p->osq_lock_wait_next_spins   = *(pcs->posq_lock_wait_next_spins);
    p->osq_unlock_wait_next_spins = *(pcs->posq_unlock_wait_next_spins);
    p->osq_lock_locked_spins      = *(pcs->posq_lock_locked_spins);
    p->osq_lock_unqueue_spins     = *(pcs->posq_lock_unqueue_spins);
    p->osq_lock_acquire_backoffs  = *(pcs->posq_lock_acquire_backoffs);
#endif
}


void set_cpu_affinity(const unsigned long run_on_this_cpu) {
    cpu_set_t affin_mask;
    CPU_ZERO(&affin_mask);
    CPU_SET(run_on_this_cpu, &affin_mask);

    const int ret = sched_setaffinity(0, sizeof(cpu_set_t), &affin_mask);
    if (ret == -1) {
        fprintf(stderr, "\nERROR: sched_setaffinity() returned -1 when trying to run on CPU%lu; it is probably not online.\n", run_on_this_cpu);
        exit(-1);
    }
}

// hmr is called by pthread_create()

void* hmr(void *ptr)
{
    unsigned long lock_acquires = 0;
    thread_args_t *x = (thread_args_t *) ptr;

#ifdef OSQ_LOCK_COUNT_LOOPS
    unsigned long osq_lock_wait_next_spins = 0;
    unsigned long osq_unlock_wait_next_spins = 0;
    unsigned long osq_lock_locked_spins = 0;
    unsigned long osq_lock_unqueue_spins = 0;
    unsigned long osq_lock_acquire_backoffs = 0;
#endif

    unsigned long *lock = x->lock;          // address of the lock variable
    unsigned long num_acquires = x->num_acquires;   // number of acquires (per thread)

    unsigned long run_on_this_cpu = x->run_on_this_cpu;
    unsigned long num_threads = x->num_threads;

    unsigned long hold_count = x->hold;
    unsigned long post_count = x->post;

    unsigned long thread = x->thread_num;   // thread is hmr thread number, starting with 0.  Not a core or CPU.  Super confusing.

    struct timespec tv_monot_start, tv_monot_end, tv_cputime_start, tv_cputime_end;
    unsigned long total_depth = 0;
    unsigned long run_limit_ticks = x->run_limit_ticks;
    unsigned long run_limit_inner_loop_iters = x->run_limit_inner_loop_iters;
    unsigned long ticks_start;
    unsigned long ticks_end;

    per_thread_results_t * presults = &x->results;

    // all threads increment the counter part of p_sync_lock
    fetchadd64_acquire(locks.p_sync_lock, 2);

    cleanup_struct_t cs = {
        .x = x,
        .lock_acquires = 0,
        .ptv_monot_start = &tv_monot_start,
        .ptv_start = &tv_cputime_start,
        .thread = thread,
#ifdef OSQ_LOCK_COUNT_LOOPS
        .posq_lock_wait_next_spins   = &osq_lock_wait_next_spins,
        .posq_unlock_wait_next_spins = &osq_unlock_wait_next_spins,
        .posq_lock_locked_spins      = &osq_lock_locked_spins,
        .posq_lock_unqueue_spins     = &osq_lock_unqueue_spins,
        .posq_lock_acquire_backoffs  = &osq_lock_acquire_backoffs,
#endif
    };

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    pthread_cleanup_push((void (*)(void *)) thread_cleanup_routine, &cs);

    thread_local_init(thread);  // if defined, call the lock algorithm test-specific init function by thread

    // set up CPU affinity --------------------------------------------

    set_cpu_affinity(run_on_this_cpu);
    presults->cpu_affined = run_on_this_cpu;

    // synchronize to calculate blackhole -----------------------------

    if (thread == 0) {
        wait64(locks.p_ready_lock, num_threads - 1); // wait until p_ready_lock has the value num_threads-1 by subordinate threads' incrementing

        // marshal thread sets lsb of p_sync_lock
        unsigned long p_sync_lock_value = fetchadd64_release(locks.p_sync_lock, 1);   // p_sync_lock should be ((num_threads * 2) | 1) after this
#ifdef DDEBUG
        if (p_sync_lock_value != (num_threads << 1)) {
            fprintf(stderr, "unexpectedly, p_sync_lock did not have the expected value.\n");
            exit(-1);
        }
#else
        (void) p_sync_lock_value;
#endif
    } else {
        fetchadd64_release(locks.p_ready_lock, 1);  // all subordinate threads add 1 to p_ready_lock

        // Spin until the "marshal" sets the lsb of p_sync_lock
        wait64(locks.p_sync_lock, (num_threads * 2) | 1);
    }

    // All threads calibrate their own blackhole timer -----

    calibrate_timer(x, thread, x->blackhole_numtries, x->verbose);
    hold_count = x->hold_count;
    post_count = x->post_count;

#ifdef __LINUX_OSQ_LOCK_H
    synchronize_threads(locks.p_calibrate_lock, num_threads);
    osq_lock_compute_blackhole_interval(thread, x->tickspns, run_on_this_cpu, x->blackhole_numtries);
#endif

    if (thread == 0 && x->verbose >= VERBOSE_YES) {
        printf("Measurement is about to start...\n");
    }

    // Wait for all threads to arrive from calibration ----------------
    synchronize_threads(locks.p_calibrate_lock, num_threads);

#ifdef DDEBUG
    printf("thread %lu: hold_count=%ld post_count=%ld\n", thread, hold_count, post_count);
#endif

    // Finally do the measurement ------------------------------------

    if (run_limit_ticks) {

        // run for an amount of time

        // TODO: for run_limit_ticks, count lock_acquires completed at tick milestones instead of ticks at lock_acquires milestones

        clock_gettime(CLOCK_MONOTONIC, &tv_monot_start);
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tv_cputime_start);

        cs.ticks_start = ticks_start = get_raw_counter();

        do {

            for (size_t i = 0; i < run_limit_inner_loop_iters; i++) {
                // Do a lock thing
#ifndef __LINUX_OSQ_LOCK_H
                // osq_lock does not use the lock pointer because each node has
                // its own osq in a separate cacheline, so this prefetch is redundant
                prefetch64(lock);
#endif
                CALLSITE_LABEL(lock_acquire_timed);
                total_depth += lock_acquire(lock, thread);
                blackhole(hold_count);
                CALLSITE_LABEL(lock_release_timed);
                lock_release(lock, thread);
                blackhole(post_count);
                lock_acquires++;
            }

            cs.ticks_end = ticks_end = get_raw_counter();
            cs.total_depth = total_depth;
            cs.lock_acquires = lock_acquires;
            // XXX: the problem with this is that lock_acquire() / lock_release() could livelock
            // before the for loop finishes, so cs.lock_acquires never gets updated.

        } while (ticks_end - ticks_start < run_limit_ticks);

        clock_gettime(CLOCK_MONOTONIC, &tv_monot_end);
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tv_cputime_end);

    } else {

        // run for a number of acquires

#ifdef PROGRESS_TICK_PROFILE
        unsigned long target_25p = num_acquires / 4;
        unsigned long target_50p = target_25p * 2;
        unsigned long target_75p = target_25p * 3;

        unsigned long target_10p = num_acquires * 0.1;
        unsigned long target_90p = num_acquires * 0.9;

        unsigned long hwtimer_10p = 0;
        unsigned long hwtimer_25p = 0;
        unsigned long hwtimer_50p = 0;
        unsigned long hwtimer_75p = 0;
        unsigned long hwtimer_90p = 0;
#endif

        clock_gettime(CLOCK_MONOTONIC, &tv_monot_start);
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tv_cputime_start);

        cs.ticks_start = ticks_start = get_raw_counter();

        while (!num_acquires || lock_acquires < num_acquires) {
            // Do a lock thing
#ifndef __LINUX_OSQ_LOCK_H      // osq_lock does not use the lock pointer, and these prefetches of it are redundant
            prefetch64(lock);
#endif
            CALLSITE_LABEL(lock_acquire_counted);
            total_depth += lock_acquire(lock, thread);
            blackhole(hold_count);
            CALLSITE_LABEL(lock_release_counted);
            lock_release(lock, thread);
            blackhole(post_count);

#ifdef PROGRESS_TICK_PROFILE
            // records ticks at lock_acquires milestones
            update_timer_tick_progress(lock_acquires,
                    target_10p, &hwtimer_10p,
                    target_25p, &hwtimer_25p,
                    target_50p, &hwtimer_50p,
                    target_75p, &hwtimer_75p,
                    target_90p, &hwtimer_90p);
#endif

            lock_acquires++;
            cs.lock_acquires = lock_acquires; // XXX: will doing this be too much?
            cs.total_depth = total_depth;
        }

        cs.ticks_end = ticks_end = get_raw_counter();

        clock_gettime(CLOCK_MONOTONIC, &tv_monot_end);
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tv_cputime_end);

#ifdef PROGRESS_TICK_PROFILE
        presults->hwtimer_10p = hwtimer_10p;
        presults->hwtimer_25p = hwtimer_25p;
        presults->hwtimer_50p = hwtimer_50p;
        presults->hwtimer_75p = hwtimer_75p;
        presults->hwtimer_90p = hwtimer_90p;
#endif
    }

    // Measurement done; record results per thread -------------------

    if (thread == 0)
        *(x->p_start_ns) = timespec_to_ns(&tv_monot_start);

    unsigned long cputime_ns = timespec_to_ns(&tv_cputime_end) - timespec_to_ns(&tv_cputime_start);
    unsigned long walltime_ns = timespec_to_ns(&tv_monot_end) - timespec_to_ns(&tv_monot_start);

    presults->lock_acquires = lock_acquires;
    presults->cputime_ns = cputime_ns;
    presults->walltime_ns = walltime_ns;
    presults->hmrdepth = total_depth;        // writes to hmrdepth[]

    presults->hwtimer_start = ticks_start;
    presults->hwtimer_end = ticks_end;

#ifdef OSQ_LOCK_COUNT_LOOPS
    presults->osq_lock_wait_next_spins   = osq_lock_wait_next_spins;
    presults->osq_unlock_wait_next_spins = osq_unlock_wait_next_spins;
    presults->osq_lock_locked_spins      = osq_lock_locked_spins;
    presults->osq_lock_unqueue_spins     = osq_lock_unqueue_spins;
    presults->osq_lock_acquire_backoffs  = osq_lock_acquire_backoffs;
#endif

    pthread_cleanup_pop(0);

    return NULL;
}

/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
