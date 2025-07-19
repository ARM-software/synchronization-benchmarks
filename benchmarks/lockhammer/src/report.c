
/*
 * Copyright (c) 2017-2025, The Linux Foundation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
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
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <locale.h>
#ifdef JSON_OUTPUT
#include <limits.h>
#include <jansson.h>
#endif

#include "verbose.h"
#include "lockhammer.h"
#include "perf_timer.h"

extern thread_args_t thread_args[];
extern unsigned long hwtimer_diff[];

extern const char * test_name;
extern const char * variant_name;
extern const char * test_type_name;

static char is_earlier_than_range(unsigned long v, thread_args_t * haystack, size_t num_threads) {
    for (size_t i = 0; i < num_threads; i++) {
        if (v < haystack[i].results.hwtimer_start) { return '<'; }
    }
    return ' ';
}

static char is_later_than_range(unsigned long v, thread_args_t * haystack, size_t num_threads) {
    for (size_t i = 0; i < num_threads; i++) {
        if (v > haystack[i].results.hwtimer_end) { return '<'; }
    }
    return ' ';
}

static char denote_end(unsigned long v, thread_args_t * haystack, size_t num_threads, unsigned long hwtimer_end_earliest, unsigned long hwtimer_end_latest) {
    for (size_t i = 0; i < num_threads; i++) {
        if (v == hwtimer_end_earliest) { return '^'; }
        if (v == hwtimer_end_latest) { return '$'; }
    }
    return ' ';
}

typedef struct {
    unsigned long thread_num;               // which worker thread is this
    unsigned long cpu_num;                  // CPU on which this thread was assigned
    unsigned long lock_acquires;            // number of lock acquires/releases this thread did
    double        lock_acquires_sigmas;     // population standard deviations from the mean
    double        lock_acquires_percent;    // percent of total_lock_acquires that this thread did
    unsigned long cpu_time_ns;              // CPU time incurred by this thread
    unsigned long walltime_ns;              // wall clock time incurred by this thread
    double        depth;                    // total_depth / lock_acquires, should be "gross depth"?
    double        critical_ns_per_loop;     // measured hold duration from calibration of this thread
    double        parallel_ns_per_loop;     // measured post duration from calibration of this thread
    unsigned long hwtimer_start;            // hwtimer start before measurement
    unsigned long hwtimer_end;              // hwtimer start after measurement
} per_thread_stats_t;

typedef struct {
    pinorder_t *  pinorder;
    per_thread_stats_t * per_thread_stats;

    unsigned long nominal_critical, nominal_parallel;
    const char * nominal_critical_unit, * nominal_parallel_unit;

    // workload/duration limit
    unsigned long run_limit_num_acquires;
    unsigned long run_limit_ticks;
    double        run_limit_seconds;

    // hwtimer frequency
    unsigned long hwtimer_frequency;        // if non-zero, then it was passed in using the --hwtimer-frequency flag
    unsigned long probed_hwtimer_frequency; // if non-zero, then it is from timer_get_timer_freq(), which could be wrong

    const char * test_name;
    const char * variant_name;
    const char * test_type_name;

    unsigned long meas_number;
    unsigned long test_number;
    unsigned long iteration;
    unsigned long num_threads;
    double        avg_critical_ns_per_loop;
    double        avg_parallel_ns_per_loop;
    double        full_concurrency_fraction;

    // basic metrics
    unsigned long total_lock_acquires;
    double        lock_acquires_mean;
    double        lock_acquires_stddev;
    double        lock_acquires_stddev_over_mean;

    // duration metrics
    unsigned long wall_elapsed_ns;
    unsigned long total_cputime_ns;
    unsigned long total_parallel_cputime_ns;
    unsigned long total_critical_cputime_ns;

    // overhead metrics
    unsigned long total_lock_overhead_cputime_ns;
    double        avg_lock_overhead_cputime_ns;
    double        lock_overhead_cputime_percent;

    // performance metrics
    double        cputime_ns_per_lock_acquire;
    double        wall_elapsed_ns_per_lock_acquire;
    double        total_lock_acquires_per_second;

    // ratio metrics
    double        cpu_to_wall_elapsed_ratio;
    double        mean_lock_depth;

#if 0
    // TODO: tick profile summary coverage data, but maybe this should be in a separate structure?
    unsigned long hwtimer_start_spread;
    unsigned long hwtimer_stop_spread;
    unsigned long hwtimer_duration_spread;
    unsigned long hwtimer_long_short;
    double        mean_hwtimer_diff;
    double        stddev_hwtimer_diff;
#endif
} report_data_t;

size_t num_reports = 0;
size_t report_capacity = 0;
report_data_t * report_summary;


void standard_report (pinorder_t * p_pinorder, unsigned long meas_number, unsigned long test_number, unsigned long iteration, unsigned long wall_elapsed_ns, test_args_t * p_test_args, const duration_t * crit, const duration_t * par) {

    // wall_elapsed_ns is wall clock time from when the marshal thread
    // starts measuring to when the parent thread joins all workers.

/*  old variable                 old output                     new code var name                          meaning
    result                       "lock loops"                   lock_acquires                              number of iterations of the lock acquire/critical_blackhole/lock release/parallel_blackhole loop
                                                                lock_acquires_mean                         average number of lock acquires per thread
    sched_elapsed                "ns scheduled"                 total_cputime_ns                           total of all worker threads' CPU time
    realcpu_elapsed              (not reported)                 total_walltime_ns                          total of all worker threads' per CPU wall-clock time (can this actually make sense to measure? maybe if the busy loops were actually idle/sleep, but that is not implemented (yet))
    real_elapsed                 "ns elapsed"                   wall_elapsed_ns                            wall-clock time elapsed
    sched_elapsed/real_elapsed   "~%f cores"                    cpu_to_wall_elapsed_ratio                  total CPU time divided by wall clock elapsed time
    sched_elapsed/result         "ns per access (scheduled)"    cputime_ns_per_lock_acquire                average CPU time per acquired lock -- includes the parallel sections!!!
    realcpu_elapsed/result       "ns per access (real)"         walltime_ns_per_lock_acquire               average CPU wall-clock time per acquired lock (does this actually make sense?)
    real_elapsed/result          "ns access rate"               wall_elapsed_ns_per_lock_acquire           wall-clock time per acquired lock
    avg_lock_depth               "average depth"                mean_lock_depth                            average lock depth (TBD)
                                                                lock_overhead_cputime_ns                   total_cputime_ns - per_thread { lock_acquires * (critial_ns + parallel_ns) }
*/

    int verbose = p_test_args->verbose;
    unsigned long num_threads = p_pinorder->num_threads;

    if (num_reports == report_capacity) {
        const size_t capacity_increase_size = 10;
        report_summary = reallocarray(report_summary, num_reports + capacity_increase_size, sizeof(report_summary[0]));
        if (! report_summary) { fprintf(stderr, "ERROR: failed report size reallocation\n"); exit(-1); }
        report_capacity += capacity_increase_size;
        // printf("increased report capacity to %zu\n", report_capacity);
    }
    report_data_t * s = &(report_summary[num_reports]);

    double mean_lock_depth = 0.0;

    unsigned long total_lock_acquires = 0;   // total lock acquires (and releases)
    unsigned long total_cputime_ns = 0;      // total CPU time
    unsigned long total_parallel_cputime_ns = 0;     // total CPU time in parallel section
    unsigned long total_critical_cputime_ns = 0;     // total CPU time in critical section
    unsigned long total_lock_overhead_cputime_ns = 0;

    for (size_t i = 0; i < num_threads; i++) {

        per_thread_results_t * p = &thread_args[i].results;

        unsigned long lock_acquires = p->lock_acquires;
        unsigned long cputime_ns = p->cputime_ns;

        total_lock_acquires += lock_acquires;
        total_cputime_ns += cputime_ns;

        // estimate lock overhead by subtracting the parallel and critical cputime from the thread cputime

        double critical_ns = p->hold_ns * lock_acquires;
        double parallel_ns = p->post_ns * lock_acquires;

        total_critical_cputime_ns += critical_ns;
        total_parallel_cputime_ns += parallel_ns;

        const char * capped = "";
        double lock_overhead_cputime_ns = cputime_ns - critical_ns - parallel_ns;
        if (lock_overhead_cputime_ns < 0) {
            // cputime_ns is measured but critical_ns and parallel_ns are estimated
            // by measuring a few iterations and scaling by lock_acquires, so the
            // result of the lock_overhead_cputime_ns can be negative if the
            // overhead is actually very small (such as in lh_empty).
            lock_overhead_cputime_ns = 0;
            capped = "*";
        }
        if (0)
        printf("thread %zu lock_overhead_cputime_ns = %f%s, cputime_ns = %lu, critical_ns = %f, parallel_ns = %f\n",
                i, lock_overhead_cputime_ns, capped, cputime_ns, critical_ns, parallel_ns);

        total_lock_overhead_cputime_ns += lock_overhead_cputime_ns;

        /* Average lock "depth" is an algorithm-specific auxiliary metric
           whereby each algorithm can report an approximation of the level
           of contention it observes.  This estimate is returned from each
           call to lock_acquire and accumulated per thread.  These results
           are then aggregated and averaged here so that an overall view
           of the run's contention level can be determined. */

        mean_lock_depth += (double) p->hmrdepth / p->lock_acquires;

    }

    mean_lock_depth /= num_threads;

    double avg_lock_overhead_cputime_ns = (double) total_lock_overhead_cputime_ns / total_lock_acquires;

    double lock_overhead_cputime_percent = (double) total_lock_overhead_cputime_ns / total_cputime_ns * 100;

    double cputime_ns_per_lock_acquire = (double) total_cputime_ns / total_lock_acquires;
    double cpu_to_wall_elapsed_ratio = (double) total_cputime_ns / wall_elapsed_ns;


    // compute the full concurrency fraction.

    // Example of using 3 threads that start/end at slightly different times
    // where x-axis is time and 'o' means the thread is running.

    //  T0 ...ooooo...
    //  T1 ...oooo....
    //  T2 ....oooo...
    //         ^^^__only these 3 cycles have the full 3-thread concurrency
    //        ^^^^^__threads ran for 5 cycles
    //        full_concurrency_fraction = 3 / 5 = 0.6


    // Longer example showing more full-thread concurrency by running longer.

    //  T0 ...ooooooooooooooooooooo...
    //  T1 ...oooooooooooooooooooooo..
    //  T2 ....ooooooooooooooooooooo..
    //         ^^^^^^^^^^^^^^^^^^^^__3 threads ran concurrently for 20 cycles
    //        ^^^^^^^^^^^^^^^^^^^^^^^___over a span of 23 cycles
    //        full_concurrency_fraction = 20 / 21 = 0.952


    // Example where thread T0 ended before the other started.

    //  T0 ...ooooo.........
    //  T1 .........oooo....
    //  T2 ..........oooo...
    //          0 cycles had all 3 threads running at the same time
    //        ^^^^^^^^^^^ span of 11 cycles
    //        full_concurrency_fraction = 0 / 11 = 0

    unsigned long earliest_hwtimer_start = -1;
    unsigned long earliest_hwtimer_end = -1;
    unsigned long latest_hwtimer_start = 0;
    unsigned long latest_hwtimer_end = 0;

    for (size_t i = 0; i < num_threads; ++i) {
        per_thread_results_t * p = &thread_args[i].results;

        earliest_hwtimer_start = (p->hwtimer_start < earliest_hwtimer_start) ? p->hwtimer_start : earliest_hwtimer_start;
        earliest_hwtimer_end   = (p->hwtimer_end   < earliest_hwtimer_end  ) ? p->hwtimer_end   : earliest_hwtimer_end  ;

        latest_hwtimer_start   = (p->hwtimer_start > latest_hwtimer_start)   ? p->hwtimer_start : latest_hwtimer_start;
        latest_hwtimer_end     = (p->hwtimer_end   > latest_hwtimer_end  )   ? p->hwtimer_end   : latest_hwtimer_end  ;
    }

    double full_concurrency_fraction = ((double) (earliest_hwtimer_end - latest_hwtimer_start)) / (latest_hwtimer_end - earliest_hwtimer_start);
    if (full_concurrency_fraction < 0) {
        // if a thread finished before another one started, there is no possibility of full concurrency.
        full_concurrency_fraction = 0.;
    }

    if (verbose >= VERBOSE_MORE) {
        printf("earliest_hwtimer_start    = %lu\n", earliest_hwtimer_start);
        printf("earliest_hwtimer_end      = %lu\n", earliest_hwtimer_end  );
        printf("latest_hwtimer_start      = %lu\n", latest_hwtimer_start);
        printf("latest_hwtimer_end        = %lu\n", latest_hwtimer_end  );
        printf("full_concurrency_fraction = %f\n", full_concurrency_fraction);
    }

    // report per thread lock acqusition fairness

    double lock_acquires_mean = (double) total_lock_acquires / num_threads;
    double lock_acquires_sum_diff_squared = 0;

    for (size_t i = 0; i < num_threads; ++i) {
        per_thread_results_t * p = &thread_args[i].results;
        double diff_mean = (p->lock_acquires - lock_acquires_mean);
        lock_acquires_sum_diff_squared += diff_mean * diff_mean;
    }

    lock_acquires_sum_diff_squared /= num_threads;  // variance

    double lock_acquires_stddev = sqrt(lock_acquires_sum_diff_squared);

    per_thread_stats_t * per_thread_stats = malloc(sizeof(per_thread_stats_t) * num_threads);   // XXX: never free'd

    if (per_thread_stats == NULL) { fprintf(stderr, "ERROR mallocing per_thread_stats\n"); exit(-1); }

    // compute average critical and parallel durations and per-thread stats

    double avg_critical_ns_per_loop = 0;
    double avg_parallel_ns_per_loop = 0;

    for (size_t i = 0; i < num_threads; ++i) {
        per_thread_results_t * p = &thread_args[i].results;
        double lock_acquires_percent = p->lock_acquires * 100. / total_lock_acquires;
        double critical_ns_per_loop = p->hold_ns;
        double parallel_ns_per_loop = p->post_ns;

        per_thread_stats[i] = (per_thread_stats_t) {
            .thread_num            = i,
            .cpu_num               = p->cpu_affined,
            .lock_acquires         = p->lock_acquires,
            .lock_acquires_sigmas  = (p->lock_acquires - lock_acquires_mean) / lock_acquires_stddev,
            .lock_acquires_percent = lock_acquires_percent,
            .cpu_time_ns           = p->cputime_ns,
            .walltime_ns           = p->walltime_ns,
            .depth                 = (double) p->hmrdepth / p->lock_acquires,
            .critical_ns_per_loop  = critical_ns_per_loop,
            .parallel_ns_per_loop  = parallel_ns_per_loop,
            .hwtimer_start         = p->hwtimer_start,
            .hwtimer_end           = p->hwtimer_end,
        };

        avg_critical_ns_per_loop += critical_ns_per_loop;
        avg_parallel_ns_per_loop += parallel_ns_per_loop;
    }

    avg_critical_ns_per_loop /= num_threads;
    avg_parallel_ns_per_loop /= num_threads;

    s->per_thread_stats = per_thread_stats;

    if (verbose >= VERBOSE_MORE)
        for (size_t i = 0; i < num_threads; ++i) {
            per_thread_stats_t * p = &per_thread_stats[i];
            printf("thread %zu (cpu %lu): lock_acquires = %lu (%.3f sigmas), lock_acquires_percent = %0.2f%%, cputime_ns = %lu, walltime_ns = %lu, depth = %0.3f, critical_ns = %.f (per loop), parallel_ns = %.f (per loop)\n",
                p->thread_num,
                p->cpu_num,
                p->lock_acquires,
                p->lock_acquires_sigmas,
                p->lock_acquires_percent,
                p->cpu_time_ns,
                p->walltime_ns,
                p->depth,
                p->critical_ns_per_loop,
                p->parallel_ns_per_loop);
        }

    double wall_elapsed_ns_per_lock_acquire = (double) wall_elapsed_ns / total_lock_acquires;
    double total_lock_acquires_per_second = total_lock_acquires * 1e9 / wall_elapsed_ns;

    // TODO: rename wall time to something else as not to be confused with wall_elapsed time (which is what we normally associated with wall clock time)

    s->pinorder = p_pinorder;
    s->iteration = iteration;
    s->meas_number = meas_number;
    s->test_number = test_number;
    s->num_threads = num_threads;
    s->total_lock_acquires = total_lock_acquires;
    s->lock_acquires_mean = lock_acquires_mean;
    s->lock_acquires_stddev = lock_acquires_stddev;
    s->lock_acquires_stddev_over_mean = lock_acquires_stddev/lock_acquires_mean;
    s->avg_critical_ns_per_loop = avg_critical_ns_per_loop;
    s->avg_parallel_ns_per_loop = avg_parallel_ns_per_loop;

    s->full_concurrency_fraction = full_concurrency_fraction;

    s->test_name = test_name;
    s->variant_name = variant_name;
    s->test_type_name = test_type_name;

    s->nominal_critical = crit->t;
    s->nominal_parallel = par->t;
    s->nominal_critical_unit = (crit->unit == NS) ? "ns" : "inst";
    s->nominal_parallel_unit = (par->unit == NS) ? "ns" : "inst";

    //printf("nominal_critical = %lu%s, nominal_parallel = %lu%s\n", s->nominal_critical, s->nominal_critical_unit, s->nominal_parallel, s->nominal_parallel_unit);

    s->run_limit_num_acquires = p_test_args->num_acquires;
    s->run_limit_ticks = p_test_args->run_limit_ticks;
    s->run_limit_seconds = p_test_args->run_limit_seconds;

    s->hwtimer_frequency = p_test_args->hwtimer_frequency;
    s->probed_hwtimer_frequency = p_test_args->probed_hwtimer_frequency;

    if (verbose >= VERBOSE_YES) {
    printf("basic metrics:___________________________________________\n");

    // number of worker threads
    printf("num_threads = %lu [thrds]\n", num_threads);

    // total number of lock acquire + release loop iterations across all threads
    printf("total_lock_acquires = %lu\n", total_lock_acquires);

    // average number of lock acquires per thread
    printf("lock_acquires_mean = %.3f per thread, stddev = %.3f, stddev/mean = %f\n", lock_acquires_mean, lock_acquires_stddev, lock_acquires_stddev/lock_acquires_mean);

    // average critical and parallel durations per loop, as measured in per-thread calibration
    printf("avg_critical_ns_per_loop = %.3f [crit_ns]\n", avg_critical_ns_per_loop);
    printf("avg_parallel_ns_per_loop = %.3f [par_ns]\n", avg_parallel_ns_per_loop);

    // fraction of the measurement time that all threads were concurrent
    printf("full_concurrency_fraction = %0.3f\n", full_concurrency_fraction);
    printf("lock_acquires_stddev_over_mean = %0.3f\n", s->lock_acquires_stddev_over_mean);
    }


    // durations

    s->wall_elapsed_ns = wall_elapsed_ns;
    s->total_cputime_ns = total_cputime_ns;
    s->total_parallel_cputime_ns = total_parallel_cputime_ns;
    s->total_critical_cputime_ns = total_critical_cputime_ns;

    if (verbose >= VERBOSE_YES) {
    printf("duration metrics:________________________________________\n");

    // wall clock elapsed time in nanoseconds -- this is the 'singular' wall clock time
    printf("wall_elapsed_ns = %lu (%.2f seconds)\n", wall_elapsed_ns, wall_elapsed_ns / 1e9);

    // total CPU time used by all threads in nanoseconds
    printf("total_cputime_ns = %lu (%0.2f seconds)\n", total_cputime_ns, total_cputime_ns / 1e9);

    // total CPU time spent in parallel (post) in nanoseconds (estimated)
    printf("total_parallel_cputime_ns = %lu (%0.2f seconds)\n", total_parallel_cputime_ns, total_parallel_cputime_ns / 1e9);

    // total CPU time spent in critical (hold) in nanoseconds (estimated)
    printf("total_critical_cputime_ns = %lu (%0.2f seconds)\n", total_critical_cputime_ns, total_critical_cputime_ns / 1e9);
    }


    // overhead metrics

    s->total_lock_overhead_cputime_ns = total_lock_overhead_cputime_ns;
    s->avg_lock_overhead_cputime_ns = avg_lock_overhead_cputime_ns;
    s->lock_overhead_cputime_percent = lock_overhead_cputime_percent;

    if (verbose >= VERBOSE_YES) {
    printf("overhead metrics:________________________________________\n");

    // total CPU time minus critical and parallel CPU time in nanoseconds
    printf("total_lock_overhead_cputime_ns = %lu (%0.2f seconds)\n", total_lock_overhead_cputime_ns, total_lock_overhead_cputime_ns / 1e9);

    // total lock overhead CPU time as a percentage of total CPU time
    printf("lock_overhead_cputime_percent = %f%%\n", lock_overhead_cputime_percent);

    // average lock overhead CPU time per lock acquire in nanoseconds
    printf("avg_lock_overhead_cputime_ns = %f per lock acquire [overhead_ns]\n", avg_lock_overhead_cputime_ns);
    }


    // performance metrics

    s->cputime_ns_per_lock_acquire = cputime_ns_per_lock_acquire;
    s->wall_elapsed_ns_per_lock_acquire = wall_elapsed_ns_per_lock_acquire;
    s->total_lock_acquires_per_second = total_lock_acquires_per_second;

    if (verbose >= VERBOSE_YES) {
    printf("performance metrics:_____________________________________\n");

    // CPU time per lock acquire (includes critical and parallel time)
    printf("cputime_ns_per_lock_acquire = %f [cpu_ns/lock]\n", cputime_ns_per_lock_acquire);

    // wall clock elapsed time per lock acquire (includes critical and parallel time)
    printf("wall_elapsed_ns_per_lock_acquire = %f\n", wall_elapsed_ns_per_lock_acquire);

    // aggregate lock acquires per second
    printf("total_lock_acquires_per_second = %f [locks/wall_sec]\n", total_lock_acquires_per_second);
    }


    // ratio metrics

    s->cpu_to_wall_elapsed_ratio = cpu_to_wall_elapsed_ratio;
    s->mean_lock_depth = mean_lock_depth;

    if (verbose >= VERBOSE_YES) {
    printf("ratio metrics:___________________________________________\n");

    // total CPU time divded by total wall clock time
    printf("cpu_to_wall_elapsed_ratio = %f\n", cpu_to_wall_elapsed_ratio);

    // average lock depth per thread, where lock_depth depends on the lock implementation
    printf("mean_lock_depth = %f\n", mean_lock_depth);
    }

    num_reports++;

    if (0) {

    // original output used
    // "scheduled" to mean cpu time
    // "real" to mean wall clock time
    // "realcpu" to mean total of all threads' wall clock time
    // "scheduled/real" to estimate cores.

    fprintf(stderr, "%ld lock loops (lock acquires)\n", total_lock_acquires);
    fprintf(stderr, "%ld ns CPU time\n", total_cputime_ns);
    fprintf(stderr, "%f ns per lock acquire (cpu)\n", cputime_ns_per_lock_acquire);
    fprintf(stderr, "%f ns access rate (wall clock time divided by total number of all lock attempts)\n", wall_elapsed_ns_per_lock_acquire);
    fprintf(stderr, "%f average depth\n", mean_lock_depth);

    printf("num_threads, cpu_ns/acquire, real/result, mean_lock_depth\n");
    printf("%lu, %f, %f, %f\n",
           num_threads,
           cputime_ns_per_lock_acquire,
           wall_elapsed_ns_per_lock_acquire,
           mean_lock_depth);
    }
}


#ifdef JSON_OUTPUT

// floating point values not handled by standard JSON is recorded as strings
static json_t * json_real_helper(double x) {
    if (isnan(x)) {
        return json_string("NaN");
    }

    switch(isinf(x)) {
        case -1: return json_string("-Inf");
        case  1: return json_string("+Inf");
        default: break;
    }
    return json_real(x);
}

// invokes the corresponding function based on the type of x
#define JOBJ(x) _Generic(x, \
    double          : json_real_helper, \
    const char *    : json_string, \
    char *          : json_string, \
    unsigned long   : json_integer ) (x)


static json_t * json_get_one_pinorder_list(const pinorder_t * p) {
    json_t * po_list = json_array();

    for (size_t j = 0; j < p->num_threads; j++) {
        json_t * json_cpu = json_integer(p->cpu_list[j]);
        json_array_append_new(po_list, json_cpu);
    }

    return po_list;
}


static void json_output(test_args_t * args) {
    const pinorder_t * p_pinorders = args->pinorders;
    const size_t num_pinorders = args->num_pinorders;
    const char * json_output_filename = args->json_output_filename;

    if (json_output_filename == NULL) return;

    char hostname[HOST_NAME_MAX+1];

    gethostname(hostname, HOST_NAME_MAX);
    hostname[HOST_NAME_MAX] = '\0'; // ensure null-termination

    // setup pinorders json array object
    // the index into this array is the pinorder number

    json_t * json_pinorders = json_array();

    for (size_t i = 0; i < num_pinorders; i++) {
        const pinorder_t * p = &(p_pinorders[i]);
        json_t * po_list = json_get_one_pinorder_list(p);

        json_array_append_new(json_pinorders, po_list);
    }

    // setup results json array object

    json_t * json_results = json_array();

// this_json_root["dest_element"] = source_var
#define JOS(this_json_root, dest_element, source_var) \
    if (json_object_set_new(this_json_root, stringify(dest_element), JOBJ(source_var))) {\
        fprintf(stderr, "json_object_set returned failure when setting " stringify(this_json_root) " " stringify(dest_element) "\n"); exit(-1); }

// json_result["x"] = s->x
#define S(x) \
    JOS(json_result, x, s->x)               // XXX: note explicit use of a pointer named s

// json_per_thread_stat["x"] = s->per_thread_stats[j].x
#define T(x) \
    JOS(json_per_thread_stat, x, s->per_thread_stats[j].x)  // XXX: note iteration using j

    for (size_t i = 0; i < num_reports; i++) {
        const report_data_t * s = &report_summary[i];

        json_t * json_result = json_object();

        // store the full CPU pinorder list in a result so that results between jsons can be compared a little bit more easily
        const pinorder_t * p_pinorder = s->pinorder;
        json_t * po_list = json_get_one_pinorder_list(p_pinorder);
        json_object_set_new(json_result, "pinorder", po_list);

        // store the pinorder string
        json_t * po_string = p_pinorder->pinorder_string ?
            json_string(p_pinorder->pinorder_string) : json_null();
        json_object_set_new(json_result, "pinorder_string", po_string);

        // store the pinorder number
        json_object_set_new(json_result, "pinorder_number",
             json_integer(p_pinorder - p_pinorders));

        // store the cpuorder filename
        json_t * cpuorder_filename = args->cpuorder_filename ?
            json_string(args->cpuorder_filename) : json_null();
        json_object_set_new(json_result, "cpuorder_filename", cpuorder_filename);

        // store the per-thread stats as an array
        json_t * json_per_thread_stats = json_array();

        for (size_t j = 0; j < s->num_threads; j++) {
            json_t * json_per_thread_stat = json_object();

//          T(thread_num);    // instead of storing the thread_num, the index into this array is the thread_num.
            T(cpu_num);
            T(lock_acquires);
            T(lock_acquires_sigmas);
            T(lock_acquires_percent);
            T(cpu_time_ns);
            T(walltime_ns);
            T(depth);
            T(critical_ns_per_loop);
            T(parallel_ns_per_loop);

            // for precision, store the counter as a hex string instead of risking truncation by JSON parsers
            char hwtimer_start_hex[32];
            char hwtimer_end_hex[32];
            snprintf(hwtimer_start_hex, sizeof(hwtimer_start_hex), "0x%lx", s->per_thread_stats[j].hwtimer_start);
            snprintf(hwtimer_end_hex, sizeof(hwtimer_end_hex), "0x%lx", s->per_thread_stats[j].hwtimer_end);
            JOS(json_per_thread_stat, hwtimer_start_hex, hwtimer_start_hex);
            JOS(json_per_thread_stat, hwtimer_end_hex, hwtimer_end_hex);

            json_array_append_new(json_per_thread_stats, json_per_thread_stat);
        }
        json_object_set_new(json_result, "per_thread_stats", json_per_thread_stats);

        // test configuration
        JOS(json_result, hostname, hostname);
        S(test_name);
        S(test_type_name);
        S(variant_name);
        S(meas_number);
        S(test_number);
        S(iteration);
        S(num_threads);
        S(nominal_critical);
        S(nominal_parallel);
        S(nominal_critical_unit);
        S(nominal_parallel_unit);
        S(run_limit_num_acquires);
        S(run_limit_ticks);
        S(run_limit_seconds);
        S(hwtimer_frequency);
        S(probed_hwtimer_frequency);

        // measurement accuracy metrics
        S(avg_critical_ns_per_loop);
        S(avg_parallel_ns_per_loop);
        S(full_concurrency_fraction);
        S(total_lock_acquires);
        S(lock_acquires_mean);
        S(lock_acquires_stddev);
        S(lock_acquires_stddev_over_mean);

        // duration metrics
        S(wall_elapsed_ns);
        S(total_cputime_ns);
        S(total_parallel_cputime_ns);
        S(total_critical_cputime_ns);

        // derived lock overhead metrics
        S(total_lock_overhead_cputime_ns);
        S(avg_lock_overhead_cputime_ns);
        S(lock_overhead_cputime_percent);

        // performance metrics
        S(cputime_ns_per_lock_acquire);
        S(wall_elapsed_ns_per_lock_acquire);
        S(total_lock_acquires_per_second);

        // ratio metrics
        S(cpu_to_wall_elapsed_ratio);
        S(mean_lock_depth);

        json_array_append_new(json_results, json_result);
    }

    // pack pinorders and results into root
    json_t * root = json_pack("{soso}",
            "pinorders", json_pinorders,
            "results", json_results);

    // output root as json file
    printf("results are also saved to %s\n", json_output_filename);
    int ret = json_dump_file(root, json_output_filename, JSON_SORT_KEYS | JSON_INDENT(4));
    if (ret) { printf("json_dump_file returned %d\n", ret); }

    json_decref(root);  // free everything
}

// undefine helper macros that are to be used only in json_output()
#undef S
#undef T
#undef JOS

#endif

void print_summary(test_args_t * args) {
    const pinorder_t * p_pinorders = args->pinorders;
    const size_t num_pinorders = args->num_pinorders;
    const int verbose = args->verbose;

    printf("Finished test_name=%s variant_name=%s test_type=%s\n",
            test_name, variant_name, test_type_name);

    if (verbose >= VERBOSE_YES) {
        printf("\n");
        printf("---------------\n");
        printf("Results Summary\n");
        printf("---------------\n\n");

        printf("pinorders:\n");
    }

    // find the longest pinorder string length
    size_t max_pinorder_strlen = 0;
    for (size_t i = 0; i < num_pinorders; i++) {
        const pinorder_t * p = &(p_pinorders[i]);
        if (p->pinorder_string) {
            size_t l = strlen(p->pinorder_string);
            max_pinorder_strlen = MAX(max_pinorder_strlen, l);
        }
    }

    // don't be less than strlen("name") = 4
    max_pinorder_strlen = MAX(max_pinorder_strlen, 4);

    printf("%-3s %-*s  cpus\n", "po", (int) max_pinorder_strlen, "name");

    for (size_t i = 0; i < num_pinorders; i++) {
        const pinorder_t * p = &(p_pinorders[i]);
        printf("%-3zu %-*s  ", i, (int) max_pinorder_strlen, p->pinorder_string ? p->pinorder_string : "");

        for (size_t j = 0; j < p->num_threads; j++) {
            printf("%*s%d", j!=0, "", p->cpu_list[j]);  // infix whitespace join
        }

        printf("\n");
    }
    printf("\n");

    if (verbose >= VERBOSE_YES)
        printf("results by pinorder:\n");

    printf("po  meas  test  iter  thrds | cpu_ns/lock - crit_ns - par_ns  = overhead_ns %% | lasom | locks/wall_sec\n");
    for (size_t i = 0; i < num_reports; i++) {
        report_data_t * s = &report_summary[i];
        printf("%-2zu  ", (s->pinorder - p_pinorders));
        printf("%-4zu  ", s->meas_number);
        printf("%-4zu  ", s->test_number);
        printf("%-4zu  ", s->iteration);
        printf("%-5lu | ", s->num_threads);
        printf("%-11.f   ", s->cputime_ns_per_lock_acquire);
        printf("%-7.f   ", s->avg_critical_ns_per_loop);
        printf("%-7.f   ", s->avg_parallel_ns_per_loop);
//        printf("%8lu  ", s->total_lock_acquires);
        printf("%-8.f ", s->avg_lock_overhead_cputime_ns);
        printf("%3.f%% | ", s->lock_overhead_cputime_percent);
        printf("%.3f | ", s->lock_acquires_stddev_over_mean);
        printf("%.f", s->total_lock_acquires_per_second);
        printf("\n");
    }

#ifdef JSON_OUTPUT
    json_output(args);
#endif

    for (size_t i = 0; i < num_reports; i++) {
        free(report_summary[i].per_thread_stats);
    }
    free(report_summary);
}


static int get_length(unsigned long x) {
    char hwtimer_len_testbuf[100];
    int length = snprintf(hwtimer_len_testbuf, sizeof(hwtimer_len_testbuf), "%lu", x);
    return length;
}

static int calculate_hwtimer_len(unsigned long hwtimer_start_base, size_t num_threads) {
    int max_len = 0;

    for (size_t i = 0; i < num_threads; i++) {
        per_thread_results_t * p = &thread_args[i].results;
        unsigned long ticks_10p = p->hwtimer_10p - hwtimer_start_base;
        unsigned long ticks_25p = p->hwtimer_25p - hwtimer_start_base;
        unsigned long ticks_50p = p->hwtimer_50p - hwtimer_start_base;
        unsigned long ticks_75p = p->hwtimer_75p - hwtimer_start_base;
        unsigned long ticks_90p = p->hwtimer_90p - hwtimer_start_base;
        unsigned long ticks_100p= p->hwtimer_end - hwtimer_start_base;

        int len_10p = get_length(ticks_10p );
        int len_25p = get_length(ticks_25p );
        int len_50p = get_length(ticks_50p );
        int len_75p = get_length(ticks_75p );
        int len_90p = get_length(ticks_90p );
        int len_100p= get_length(ticks_100p);

        if (len_10p  > max_len) max_len = len_10p ;
        if (len_25p  > max_len) max_len = len_25p ;
        if (len_50p  > max_len) max_len = len_50p ;
        if (len_75p  > max_len) max_len = len_75p ;
        if (len_90p  > max_len) max_len = len_90p ;
        if (len_100p > max_len) max_len = len_100p;
    }

#if 0
    // the old way of estimating the width by measuring the counter magnitude
    unsigned long hwtimer_now = timer_get_counter(); // XXX: this is just to get a display length of the hwtimer around now
    int hwtimer_len = snprintf(hwtimer_len_testbuf, sizeof(hwtimer_len_testbuf), "%lu", hwtimer_now);
#endif

    return max_len;
}


// If the CNTVCT or equivalent hardware clock is globally synchronous
// such that a simultaneous observation of it on one CPU returns the same
// value as on another CPU, use it to compare the start and stop of
// each thread's measurement to measure the workload overlap.
void starting_stopping_time_report (unsigned long num_threads, unsigned long verbose, unsigned long num_acquires, pinorder_t * p_pinorder, unsigned long run_limit_ticks) {

    size_t hwtimer_start_earliest_thread = -1;
    size_t hwtimer_start_latest_thread = -1;
    size_t hwtimer_end_earliest_thread = -1;
    size_t hwtimer_end_latest_thread = -1;
    unsigned long hwtimer_start_earliest = -1;
    unsigned long hwtimer_start_latest = 0;
    unsigned long hwtimer_end_earliest = -1;
    unsigned long hwtimer_end_latest = 0;

    unsigned long hwtimer_diff_shortest = -1;
    unsigned long hwtimer_diff_longest = 0;

    size_t hwtimer_diff_shortest_thread = -1;
    size_t hwtimer_diff_longest_thread = -1;

    for (size_t i = 0; i < num_threads; i++) {
        hwtimer_diff[i] = thread_args[i].results.hwtimer_end - thread_args[i].results.hwtimer_start;

        if (hwtimer_diff[i] > hwtimer_diff_longest) {
            hwtimer_diff_longest = hwtimer_diff[i];
            hwtimer_diff_longest_thread = i;
        }

        if (hwtimer_diff[i] < hwtimer_diff_shortest) {
            hwtimer_diff_shortest = hwtimer_diff[i];
            hwtimer_diff_shortest_thread = i;
        }

        if (thread_args[i].results.hwtimer_end > hwtimer_end_latest) {
            hwtimer_end_latest = thread_args[i].results.hwtimer_end;
            hwtimer_end_latest_thread = i;
        }

        if (thread_args[i].results.hwtimer_start > hwtimer_start_latest) {
            hwtimer_start_latest = thread_args[i].results.hwtimer_start;
            hwtimer_start_latest_thread = i;
        }

        if (thread_args[i].results.hwtimer_end < hwtimer_end_earliest) {
            hwtimer_end_earliest = thread_args[i].results.hwtimer_end;
            hwtimer_end_earliest_thread = i;
        }

        if (thread_args[i].results.hwtimer_start < hwtimer_start_earliest) {
            hwtimer_start_earliest = thread_args[i].results.hwtimer_start;
            hwtimer_start_earliest_thread = i;
        }
    }

    setlocale(LC_NUMERIC, "en_US.UTF-8");

    unsigned long hwtimer_frequency = timer_get_timer_freq();

    if (verbose >= VERBOSE_MORE) {
    printf("starting/stopping time report:___________________________\n");
    printf("hwtimer_frequency = %'lu Hz (%.3f ns per tick)\n", hwtimer_frequency, 1e9/hwtimer_frequency);

    // compute the spread of the measurement start of all threads
    printf("hwtimer_start:  0x%lx .. 0x%lx (thread %zu .. %zu), spread = %'lu (%'.2f ns)\n",
            hwtimer_start_earliest, hwtimer_start_latest,
            hwtimer_start_earliest_thread, hwtimer_start_latest_thread,
            hwtimer_start_latest - hwtimer_start_earliest,
            (hwtimer_start_latest - hwtimer_start_earliest) * 1e9 / hwtimer_frequency
          );

    // compute the spread of the completion of all threads
    printf("hwtimer_end:    0x%lx .. 0x%lx (thread %zu .. %zu), spread = %'lu (%'.2f ns)\n",
            hwtimer_end_earliest, hwtimer_end_latest,
            hwtimer_end_earliest_thread, hwtimer_end_latest_thread,
            hwtimer_end_latest - hwtimer_end_earliest,
            (hwtimer_end_latest - hwtimer_end_earliest) * 1e9 / hwtimer_frequency
          );

    // compute the spread of the durations of all threads
    printf("hwtimer_diff: %lu (%'.3f ns) .. %lu (%'.3f ns) (thread %zu .. %zu), spread = %'lu (%'.3f ns), long/short = %0.3f\n",
            hwtimer_diff_shortest, hwtimer_diff_shortest * 1e9 / hwtimer_frequency,
            hwtimer_diff_longest, hwtimer_diff_longest * 1e9 / hwtimer_frequency,
            hwtimer_diff_shortest_thread, hwtimer_diff_longest_thread,
            hwtimer_diff_longest - hwtimer_diff_shortest,
            (hwtimer_diff_longest - hwtimer_diff_shortest) * 1e9 / hwtimer_frequency,
            hwtimer_diff_longest / (double) hwtimer_diff_shortest);
    }

    // compute statistics on the durations

    double mean_hwtimer_diff = 0;
    unsigned long hwtimer_diff_sum = 0;

    for (size_t i = 0; i < num_threads; i++) {
        hwtimer_diff_sum += hwtimer_diff[i];
    }

    mean_hwtimer_diff = ((double) hwtimer_diff_sum) / num_threads;

    double sum_diff_squared = 0;
    double mean_abs_diff = 0;

    for (size_t i = 0; i < num_threads; i++) {
        double diff = (hwtimer_diff[i] - mean_hwtimer_diff);
        sum_diff_squared += diff * diff;

        mean_abs_diff += fabs(diff);
    }

    sum_diff_squared /= num_threads;
    mean_abs_diff /= num_threads;

    double std_dev = sqrt(sum_diff_squared);

    if (verbose >= VERBOSE_MORE)
    printf("hwtimer_diff_stats:  mean = %.f (%'0.3f ns), mean_abs_diff = %0.3f, std_dev = %0.3f\n",
            mean_hwtimer_diff, mean_hwtimer_diff * 1e9 / hwtimer_frequency, mean_abs_diff, std_dev);

    // compute buckets against normal distribution

    size_t neg_buckets[4] = {0};
    size_t pos_buckets[4] = {0};

    for (size_t i = 0; i < num_threads; i++) {
        double diff = (hwtimer_diff[i] - mean_hwtimer_diff);
        double sigmas = diff / std_dev;

        long bucket = (long) sigmas;
        if (bucket > 3)  { bucket = 3; }
        if (bucket < -3) { bucket = -3; }

        if (sigmas >= 0) {
            pos_buckets[bucket]++;
        } else {
            neg_buckets[-bucket]++;
        }

        // printf("sigmas = %f, bucket = %ld\n", sigmas, bucket);
    }

    if (verbose >= VERBOSE_MORE) {
    printf("hwtimer_diff_distribution: ");
    printf("<-3sigma:%zu, ", neg_buckets[4 - 1]);
    for (long sigma = 3; sigma >= 1; sigma--) {
        printf("-%ld:%zu, ", sigma, neg_buckets[sigma - 1]);
    }
    for (long sigma = 1; sigma <  4; sigma++) {
        printf("+%ld:%zu, ", sigma, pos_buckets[sigma - 1]);
    }
    printf( ">+3sigma:%zu\n", pos_buckets[4 - 1]);
    }


#ifdef PROGRESS_TICK_PROFILE
    // print hwtimer tick on each thread at quarter intervals

    const int print_ticks_relative_to_earliest = 1;
    unsigned long hwtimer_start_base = print_ticks_relative_to_earliest ? hwtimer_start_earliest : 0;
    int hwtimer_len = calculate_hwtimer_len(hwtimer_start_base, num_threads);

    if (run_limit_ticks == 0 && verbose >= VERBOSE_MORE) { // only do this for -a num_acquires mode
        printf("progress tick profile; < is printed for ticks that are later than the first finishing worker's 100%% tick\n");
        printf("for 0%% and 100%%: ^ for earliest, $ for latest\n");
        printf("thread\t%*s  %*s  %*s  %*s  %*s  %*s  %*s  75%% rate (ns/acq)\n",
            hwtimer_len+1,"0%",
            hwtimer_len,  "10%",
            hwtimer_len,  "25%",
            hwtimer_len,  "50%",
            hwtimer_len+1,"75%",
            hwtimer_len+1,"90%",
            hwtimer_len+1,"100%");

        for (size_t i = 0; i < num_threads; i++) {
            per_thread_results_t * p = &thread_args[i].results;
            unsigned long ticks_10p = p->hwtimer_10p - p->hwtimer_start;
            unsigned long ticks_25p = p->hwtimer_25p - p->hwtimer_start;
            unsigned long ticks_50p = p->hwtimer_50p - p->hwtimer_start;
            unsigned long ticks_75p = p->hwtimer_75p - p->hwtimer_start;
            unsigned long ticks_90p = p->hwtimer_90p - p->hwtimer_start;
            unsigned long ticks_100p= p->hwtimer_end - p->hwtimer_start;

            unsigned long ticks_10p_90p = p->hwtimer_90p - p->hwtimer_10p;
            double lock_acquire_rate = ticks_10p_90p * 1e9 / (0.8 * num_acquires * hwtimer_frequency);

            double lock_acquire_rate_75p = (p->hwtimer_75p - p->hwtimer_start) * 1e9 / (0.75 * num_acquires * hwtimer_frequency);
            // "lock acquire rate" is not acquires per second, it is ns per acquire.

            printf("%zu\t", i);

            if (0)  // don't want to remove it just yet, maybe useful in the future
            printf("%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%.f\t\t",
                 ticks_10p, ticks_25p, ticks_50p, ticks_75p, ticks_90p, ticks_100p, lock_acquire_rate);

            printf("%*lu%c %*lu%c %*lu%c %*lu%c %*lu%c %*lu%c %*lu%c %.f\n",
                hwtimer_len+1, p->hwtimer_start - hwtimer_start_base, denote_end(p->hwtimer_start, thread_args, num_threads, hwtimer_start_earliest, hwtimer_start_latest),
                hwtimer_len, p->hwtimer_10p - hwtimer_start_base, is_earlier_than_range(p->hwtimer_10p, thread_args, num_threads),   // XXX: does this ever happen?
                hwtimer_len, p->hwtimer_25p - hwtimer_start_base, is_later_than_range(p->hwtimer_25p, thread_args, num_threads),
                hwtimer_len, p->hwtimer_50p - hwtimer_start_base, is_later_than_range(p->hwtimer_50p, thread_args, num_threads),
                hwtimer_len+1, p->hwtimer_75p - hwtimer_start_base, is_later_than_range(p->hwtimer_75p, thread_args, num_threads),
                hwtimer_len+1, p->hwtimer_90p - hwtimer_start_base, is_later_than_range(p->hwtimer_90p, thread_args, num_threads),
                hwtimer_len+1, p->hwtimer_end - hwtimer_start_base, denote_end(p->hwtimer_end, thread_args, num_threads, hwtimer_end_earliest, hwtimer_end_latest),
                lock_acquire_rate_75p);
        }
    }
#endif

// #define OSQ_LOCK_COUNT_LOOPS   // enable this here and in osq_lock.h to show loop counts
#ifdef OSQ_LOCK_COUNT_LOOPS
    printf("osq_lock per-cpu loop counts:\n");
    printf("thread cpu  %20s %22s %17s %18s %22s\n", "lock_wait_next_spins", "unlock_wait_next_spins", "lock_locked_spins", "lock_unqueue_spins", "lock_acquire_backoffs");
    for (size_t i = 0; i < num_threads; i++) {
        per_thread_results_t * p = &thread_args[i].results;
        printf("%6zu %3d  %20lu %22lu %17lu %18lu %22lu\n",
                i, p_pinorder->cpu_list[i],
                p->osq_lock_wait_next_spins,
                p->osq_unlock_wait_next_spins,
                p->osq_lock_locked_spins,
                p->osq_lock_unqueue_spins,
                p->osq_lock_acquire_backoffs);
    }
#endif

}

/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
