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

#ifndef __LOCKHAMMER_H__
#define __LOCKHAMMER_H__


// PROGRESS_TICK_PROFILE - prints each thread's timer value at lock_acquires milestones to show thread concurrency
#define PROGRESS_TICK_PROFILE

enum units { NS,
             INSTS, NOT_SET };
typedef enum units Units;

#define _stringify(x) #x
#define stringify(x) _stringify(x)

// per_thread_results_t - each thread returns its results in this struct (inside thread_args_t)
typedef struct {
    unsigned long cpu_affined;  // which CPU this was pinned on.

    unsigned long lock_acquires;// number of locks acquired-and-released per thread
    unsigned long cputime_ns;   // this thread's CPU time in nanoseconds
    unsigned long walltime_ns;  // this thread's wall clock time in nanoseconds
    unsigned long hmrdepth;     // depth=lock-specific notion of contention

    unsigned long hwtimer_start; // timer value at start of measurement loop
    unsigned long hwtimer_end;   //         "'  at end

    unsigned long hwtimer_10p;   // timer value at 10% of work completion
    unsigned long hwtimer_25p;   //         ""  at 25%
    unsigned long hwtimer_50p;   //         ""  at 50%
    unsigned long hwtimer_75p;   //         ""  at 75%
    unsigned long hwtimer_90p;   //         ""  at 90%

    // hold/post durations from calibrate_timer()
    double hold_ns, post_ns;

    // metrics only for osq_lock
    unsigned long osq_lock_wait_next_spins;
    unsigned long osq_unlock_wait_next_spins;
    unsigned long osq_lock_locked_spins;
    unsigned long osq_lock_unqueue_spins;
    unsigned long osq_lock_acquire_backoffs;

} per_thread_results_t;


// thread_args_t -- pointer to an instance of this is passed to each thread
typedef struct {
    unsigned long thread_num;       // thread number, ordinal 0
    unsigned long num_threads;      // number of worker threads in total for experiment
    unsigned long num_acquires;     // -a flag, aka nacqrs, aka number of acquires per thread to do
    unsigned long *lock;            // pointer to the lock variable

    unsigned long *p_start_ns;      // marshal thread's monotonic start time, in ns, for computing wall_elapsed_ns; only marshall thread sets this
    unsigned long hold, post;       // ncrit, nparallel
    Units hold_unit, post_unit;     // NS or INSTS, hold_unit = ncrit_units, post_unit = nparallel_units
    unsigned long hold_count;
    unsigned long post_count;

    double tickspns;                // number of ticks_per_ns

    unsigned long run_on_this_cpu;  // logical CPU on which a worker thread is to run

    unsigned long run_limit_ticks;  // if non-zero, the number of timer ticks to run for when using --run-limit-ticks or --run-limit-seconds
    unsigned long run_limit_inner_loop_iters;  // the number of lock acquire/release sequences to run before checking the hwtimer when using --run-limit-ticks or --run-limit-seconds
    unsigned long hwtimer_frequency;

    int verbose;
    unsigned long blackhole_numtries;

    per_thread_results_t results;   // output data structure

} thread_args_t;

// pinorder_t - describes a set of CPUs on which to run worker threads
typedef struct {
    int * cpu_list;     // pointer to an array of int.  index into this array is the thread number, each element is the logical CPU on which that thread is to run.
    size_t num_threads; // number of threads defined for this pinorder (i.e. length of the number of valid entries in the pinorder array).
} pinorder_t;


typedef struct {
    unsigned long t;   // duration time, either in nanoseconds or iterations
    Units unit;        // duration unit, either NS or INSTS
} duration_t;

// test_args_t - mostly command line parameters
typedef struct {
    unsigned long num_acquires;  // -a    number of acquires (not documented?)
    duration_t * crits;     // -c, --cn=, --ci=    critical duration
    duration_t * pars;      // -p, --pn=, --pi=    parallel duration
    size_t num_crits;
    size_t num_pars;
    unsigned long ileave;    // -i    interleave value for SMT pinning
    int scheduling_policy;   // -S    use explicit scheduling policy
    size_t num_pinorders;
    pinorder_t * pinorders;  // -o    CPU pinning order
    unsigned long  timeout_usec;   // -A  timeout_usec

    int hugepagesz;
    int use_mmap;
    int mmap_hugepage_offset_exists;
    int print_hugepage_physaddr;
    size_t mmap_hugepage_offset;
    size_t mmap_hugepage_physaddr;
    unsigned long hwtimer_frequency;
    unsigned long probed_hwtimer_frequency;
    long estimate_hwtimer_freq_cpu;

    double run_limit_seconds;
    unsigned long run_limit_ticks;
    unsigned long run_limit_inner_loop_iters;
    int ignore_unknown_scaling_governor;
    int suppress_cpu_frequency_warnings;
    const char * cpuorder_filename;
#ifdef JSON_OUTPUT
    const char * json_output_filename;
#endif
#ifdef __aarch64__
    char disable_outline_atomics_lse;
#endif
    int verbose;
    size_t iterations;
    size_t blackhole_numtries;
} test_args_t;

// system_info_t - system configuration data
typedef struct {
    unsigned long num_cores;    // number of processors configured by the operating system
    size_t page_size_bytes;     // page size in bytes
    size_t erg_bytes;           // number of bytes per exclusive reservation granule (e.g. cache line/block)

    cpu_set_t avail_cores;      // cores that the CPU affinity mask allows us to run on
    size_t num_avail_cores;     // number of cores that the CPU affinity mask allows us to run on
    size_t num_online_cores;    // the number of cores that getconf _NPROCESSORS_ONLN returns

    // num_online_cores can be less than num_cores because some may be offline or not permitted by affinity mask
    // num_avail_cores may be less than num_online_cores because some online cores may be isolated
} system_info_t;

// locks_t -- pointers to the actual locks to be used
typedef struct {
    unsigned long * p_test_lock;        // address of main lock
    unsigned long * p_ready_lock;       // lock to synchronize all threads' entry into hmr()
    unsigned long * p_sync_lock;        // lock to synchronize before blackhole cabliration
    unsigned long * p_calibrate_lock;   // lock to synchronize after blackhole calibration
} locks_t;

// calibrate_blackhole -- (used in osq_lock)
unsigned long calibrate_blackhole(unsigned long target, unsigned long tokens_low, unsigned long tokens_high, unsigned long core_id, unsigned long NUMTRIES);

// evaluate_blackhole -- returns average duration of NUMTRIES
int64_t evaluate_blackhole( const unsigned long tokens_mid, const unsigned long NUMTRIES);

// blackhole() -- runs a small loop to consume time (also used in osq_lock)
void blackhole(unsigned long iters);

// measure_setup_initialize_lock() -- calls lock-specific setup routine if it exists
void measure_setup_initialize_lock(locks_t * p_locks, pinorder_t * pinorder);

// measure_setup_parse_test_args() -- calls lock-specific parsing routine if it exists
void measure_setup_parse_test_args(test_args_t * p_test_args, int argc, char ** argv);

// convert the struct timespec to only nanoseconds
unsigned long timespec_to_ns (struct timespec * ts);

// selectively disable LSE instructions in outline atomics/libgcc; in measure.c
void handle_disable_outline_atomics_lse(void);

#if __GNUC__==1
#define NOINLINE __attribute__((noinline))
#elif __clang__==1
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

#if __GNUC__==1
#define NO_UNROLL_LOOP _Pragma("GCC unroll 0")
#elif __clang__==1
#define NO_UNROLL_LOOP _Pragma("clang loop unroll(disable)")
#else
#define NO_UNROLL_LOOP
#endif


#endif

/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
