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

#define _GNU_SOURCE
#include <sched.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>

#include "lockhammer.h"
#include "perf_timer.h"

#include ATOMIC_TEST

uint64_t test_lock = 0;
uint64_t sync_lock = 0;
uint64_t calibrate_lock = 0;
uint64_t ready_lock = 0;

void* hmr(void *);

void print_usage (char *invoc) {
    fprintf(stderr,
            "Usage: %s\n\t[-t <#> threads]\n\t[-a <#> acquires per thread]\n\t"
            "[-c <#>[ns | in] critical iterations measured in ns or (in)structions, "
            "if no suffix, assumes instructions]\n\t"
            "[-p <#>[ns | in] parallelizable iterations measured in ns or (in)structions, "
            "if no suffix, assumes (in)structions]\n\t"
            "[-s safe-mode operation for running as non-root by reducing priority]\n\t"
            "[-i <#> interleave value for SMT pinning, e.g. 1: core pinning / no SMT, "
            "2: 2-way SMT pinning, 4: 4-way SMT pinning, may not work for multisocket]\n\t"
            "[-o <#:#:#:#> arbitrary pinning order separated by colon without space, "
            "command lstopo can be used to deduce the correct order]\n\t"
            "[-O <#> Run limit timer ticks\n\t"
            "[-I <#> Run limit inner loop iterations\n\t"
            "[-- <more workload specific arguments>]\n", invoc);
}

int main(int argc, char** argv)
{
    struct sched_param sparam;

    unsigned long opt;
    unsigned long num_cores;
    unsigned long result;
    unsigned long sched_elapsed = 0, real_elapsed = 0, realcpu_elapsed = 0;
    unsigned long start_ns = 0;
    double avg_lock_depth = 0.0;
    unsigned long run_limit_ticks = 0;
    unsigned long run_limit_inner_loop_iters = 10;


    num_cores = sysconf(_SC_NPROCESSORS_ONLN);

    /* Set defaults for all command line options */
    test_args args = { .nthrds = num_cores,
                       .nacqrs = 50000,
                       .ncrit = 0,
                       .nparallel = 0,
                       .ileave = 1,
                       .safemode = 0,
                       .pinorder = NULL };

    opterr = 0;

    while ((opt = getopt(argc, argv, "t:a:c:p:i:o:sI:O:")) != -1)
    {
        long optval = 0;
        int len = 0;
        char buf[128];
        char *csv = NULL;
        switch (opt) {
          case 't':
            optval = strtol(optarg, (char **) NULL, 10);
            /* Do not allow number of threads to exceed online cores
               in order to prevent deadlock ... */
            if (optval < 0) {
                fprintf(stderr, "ERROR: thread count must be positive.\n");
                return 1;
            }
            else if (optval == 0) {
                optval = num_cores;
            }
            else if (optval <= num_cores) {
                args.nthrds = optval;
            }
            else {
                fprintf(stderr, "WARNING: limiting thread count to online cores (%ld).\n", num_cores);
            }
            break;
          case 'a':
            optval = strtol(optarg, (char **) NULL, 10);
            if (optval < 0) {
                fprintf(stderr, "ERROR: acquire count must be positive.\n");
                return 1;
            }
            else {
                args.nacqrs = optval;
            }
            break;
          case 'c':
            // Set the units for loops
            len = strlen(optarg);
            if (optarg[len - 1] == 's') {
                args.ncrit_units = NS;
            } else {
                args.ncrit_units = INSTS;
            } 
            
            optval = strtol(optarg, (char **) NULL, 10);
            if (optval < 0) {
                fprintf(stderr, "ERROR: critical iteration count must be positive.\n");
                return 1;
            }
            else {
                args.ncrit = optval;
            }
            break;
          case 'p':
            // Set the units for loops
            len = strlen(optarg);
            if (optarg[len - 1] == 's') {
                args.nparallel_units = NS;
            } else {
                args.nparallel_units = INSTS;
            }

            optval = strtol(optarg, (char **) NULL, 10);
            if (optval < 0) {
                fprintf(stderr, "ERROR: parallel iteration count must be positive.\n");
                return 1;
            }
            else {
                args.nparallel = optval;
            }
            break;
          case 'i':
            optval = strtol(optarg, (char **) NULL, 10);
            if (optval < 0) {
                fprintf(stderr, "ERROR: core interleave must be positive.\n");
                return 1;
            }
            else {
                args.ileave = optval;
            }
            break;
          case 'o':
            args.pinorder = calloc(num_cores, sizeof(int));
            if (args.pinorder == NULL) {
                fprintf(stderr, "ERROR: cannot allocate enough memory for pinorder structure.\n");
                return 1;
            }
            /* support both comma and colon as delimiter */
            csv = strtok(optarg, ",:");
            for (int i = 0; i < num_cores && csv != NULL; ++i)
            {
                optval = strtol(csv, (char **) NULL, 10);
                /* Some Arm systems may have core number larger than total cores number */
                args.pinorder[i] = optval;
                if (optval < 0 || optval > num_cores) {
                    fprintf(stderr, "WARNING: core number %ld is out of range.\n", optval);
                }
                csv = strtok(NULL, ",:");
            }
            break;
          case 's':
            args.safemode = 1;
            break;
          case 'O':
            run_limit_ticks = strtoul(optarg, NULL, 0);
            {
                uint32_t cntfrq = timer_get_cnt_freq();
                double run_limit_seconds = run_limit_ticks / (double) cntfrq;
                printf("run_limit_ticks = %lu, at %u Hz, should take %f seconds\n", run_limit_ticks, cntfrq, run_limit_seconds);
            }
            break;
          case 'I':
            run_limit_inner_loop_iters = strtoul(optarg, NULL, 0);
            printf("run_limit_inner_loop_iters = %lu\n", run_limit_inner_loop_iters);
            break;

          case '?':
          default:
            print_usage(argv[0]);
            return 1;
        }
    }

    parse_test_args(args, argc, argv);

    double tickspns;
    pthread_t hmr_threads[args.nthrds];
    pthread_attr_t hmr_attr;
    unsigned long hmrs[args.nthrds];
    unsigned long hmrtime[args.nthrds]; /* can't touch this */
    unsigned long hmrrealtime[args.nthrds];
    unsigned long hmrdepth[args.nthrds];
    struct timespec tv_time;

    /* Select the FIFO scheduler.  This prevents interruption of the
       lockhammer test threads allowing for more precise measuremnet of
       lock acquisition rate, especially for mutex type locks where
       a lock-holding or queued thread might significantly delay forward
       progress if it is rescheduled.  Additionally the FIFO scheduler allows
       for a better guarantee of the requested contention level by ensuring
       that a fixed number of threads are executing simultaneously for
       the duration of the test.  This comes at the significant cost of
       reduced responsiveness of the system under test and the possibility
       for system instability if the FIFO scheduled threads remain runnable
       for too long, starving other processes.  Care should be taken in
       invocation to ensure that a given instance of lockhammer runs for
       no more than a few milliseconds and lockhammer should never be run
       on an already-deplayed system. */

    pthread_attr_init(&hmr_attr);
    if (!args.safemode) {
        pthread_attr_setinheritsched(&hmr_attr, PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedpolicy(&hmr_attr, SCHED_FIFO);
        sparam.sched_priority = 1;
        pthread_attr_setschedparam(&hmr_attr, &sparam);
    }

    initialize_lock(&test_lock, num_cores);
    // Get frequency of clock, and divide by 1B to get # of ticks per ns
    tickspns = (double)timer_get_cnt_freq() / 1000000000.0; 

    thread_args t_args[args.nthrds];
    for (int i = 0; i < args.nthrds; ++i) {
        hmrs[i] = 0;
        t_args[i].ncores = num_cores;
        t_args[i].nthrds = args.nthrds;
        t_args[i].ileave = args.ileave;
        t_args[i].iter = args.nacqrs;
        t_args[i].lock = &test_lock;
        t_args[i].rst = &hmrs[i];
        t_args[i].nsec = &hmrtime[i];
        t_args[i].real_nsec = &hmrrealtime[i];
        t_args[i].depth = &hmrdepth[i];
        t_args[i].nstart = &start_ns;
        t_args[i].hold = args.ncrit;
        t_args[i].hold_unit = args.ncrit_units;
        t_args[i].post = args.nparallel;
        t_args[i].post_unit = args.nparallel_units;
        t_args[i].tickspns = tickspns;
        t_args[i].pinorder = args.pinorder;
        t_args[i].run_limit_ticks = run_limit_ticks;
        t_args[i].run_limit_inner_loop_iters = run_limit_inner_loop_iters;

        pthread_create(&hmr_threads[i], &hmr_attr, hmr, (void*)(&t_args[i]));
    }

    for (int i = 0; i < args.nthrds; ++i) {
        result = pthread_join(hmr_threads[i], NULL);
    }
    /* "Marshal" thread will collect start time once all threads have
        reported ready so we only need to collect the end time here */
    clock_gettime(CLOCK_MONOTONIC, &tv_time);
    real_elapsed = (1000000000ul * tv_time.tv_sec + tv_time.tv_nsec) - start_ns;

    pthread_attr_destroy(&hmr_attr);

    result = 0;
    for (int i = 0; i < args.nthrds; ++i) {
        result += hmrs[i];
        sched_elapsed += hmrtime[i];
        realcpu_elapsed += hmrrealtime[i];
        /* Average lock "depth" is an algorithm-specific auxiliary metric
           whereby each algorithm can report an approximation of the level
           of contention it observes.  This estimate is returned from each
           call to lock_acquire and accumulated per-thread.  These results
           are then aggregated and averaged here so that an overall view
           of the run's contention level can be determined. */
        avg_lock_depth += ((double) hmrdepth[i] / (double) hmrs[i]) / (double) args.nthrds;
    }

    fprintf(stderr, "%ld lock loops\n", result);
    fprintf(stderr, "%ld ns scheduled\n", sched_elapsed);
    fprintf(stderr, "%ld ns elapsed (~%f cores)\n", real_elapsed, ((float) sched_elapsed / (float) real_elapsed));
    fprintf(stderr, "%lf ns per access (scheduled)\n", ((double) sched_elapsed)/ ((double) result));
    fprintf(stderr, "%lf ns per access (real)\n", ((double) realcpu_elapsed)/ ((double) result));
    fprintf(stderr, "%lf ns access rate\n", ((double) real_elapsed) / ((double) result));
    fprintf(stderr, "%lf average depth\n", avg_lock_depth);

    printf("%ld, %f, %lf, %lf, %lf, %lf\n",
           args.nthrds,
           ((float) sched_elapsed / (float) real_elapsed),
           ((double) sched_elapsed)/ ((double) result),
           ((double) realcpu_elapsed)/ ((double) result),
           ((double) real_elapsed) / ((double) result),
           avg_lock_depth);

    return 0;
}

/* Calculate timer spin-times where we do not access the clock.  
 * First calibrate the wait loop by doing a binary search around 
 * an estimated number of ticks. All threads participate to take
 * into account pipeline effects of threading.
 */
static void calibrate_timer(thread_args *x, unsigned long mycore)
{
    if (x->hold_unit == NS) {
        /* Determine how many timer ticks would happen for this wait time */
        unsigned long hold = (unsigned long)((double)x->hold * x->tickspns);
        /* Calibrate the number of loops we have to do */
        x->hold = calibrate_blackhole(hold, 0, TOKENS_MAX_HIGH, mycore);
    } else {
        x->hold = x->hold / 2;
    }

    // Make sure to re-sync any stragglers
    synchronize_threads(&calibrate_lock, x->nthrds);

    if (x->post_unit == NS) {
        unsigned long post = (unsigned long)((double)x->post * x->tickspns);
        x->post = calibrate_blackhole(post, 0, TOKENS_MAX_HIGH, mycore);
    } else {
        x->post = x->post / 2;
    }
#ifdef DEBUG
    printf("Calibrated (%lu) with hold=%ld post=%ld\n", mycore, x->hold, x->post);
#endif
}

void* hmr(void *ptr)
{
    unsigned long nlocks = 0;
    thread_args *x = (thread_args*)ptr;

    unsigned long *lock = x->lock;
    unsigned long target_locks = x->iter;
    unsigned long ncores = x->ncores;
    unsigned long ileave = x->ileave;
    unsigned long nthrds = x->nthrds;
    unsigned long hold_count = x->hold;
    unsigned long post_count = x->post;
    double tickspns = x->tickspns;
    int *pinorder = x->pinorder;

    unsigned long mycore = 0;

    struct timespec tv_monot_start, tv_monot_end, tv_start, tv_end;
    unsigned long ns_elap, real_ns_elap;
    unsigned long total_depth = 0;
    unsigned long run_limit_ticks = x->run_limit_ticks;
    unsigned long run_limit_inner_loop_iters = x->run_limit_inner_loop_iters;

    cpu_set_t affin_mask;

    CPU_ZERO(&affin_mask);

    /* Coordinate synchronized start of all lock threads to maximize
       time under which locks are stressed to the requested contention
       level */
    mycore = fetchadd64_acquire(&sync_lock, 2) >> 1;

    if (mycore == 0) {
        /* First core to register is a "marshal" who waits for subsequent
           cores to become ready and starts all cores with a write to the
           shared memory location */

        /* Set affinity to core 0 */
        CPU_SET(0, &affin_mask);
        sched_setaffinity(0, sizeof(cpu_set_t), &affin_mask);

        /* Spin until the appropriate numer of threads have become ready */
        wait64(&ready_lock, nthrds - 1);
        fetchadd64_release(&sync_lock, 1);

        calibrate_timer(x, mycore);
        hold_count = x->hold;
        post_count = x->post;

        /* Wait for all threads to arrive from calibrating. */ 
        synchronize_threads(&calibrate_lock, nthrds);
        clock_gettime(CLOCK_MONOTONIC, &tv_monot_start);
    } else {
        /*
         * Non-zero core value indicates next core to pin, zero value means
         * fallback to default interleave mode. Note: -o and -i may have
         * conflicting pinning order that causes two or more threads to pin
         * on the same core. This feature interaction is intended by design
         * which allows 0 to serve as don't care mask and only changing the
         * pinning order we want to change for specific -i interleave mode.
         */
        if (pinorder && pinorder[mycore]) {
            CPU_SET(pinorder[mycore], &affin_mask);
            sched_setaffinity(0, sizeof(cpu_set_t), &affin_mask);
        } else { /* Calculate affinity mask for my core and set affinity */
            /*
             * The concept of "interleave" is used here to allow for specifying
             * whether increasing cores counts first populate physical cores or
             * hardware threads within the same physical core. This assumes the
             * following relationship between logical core numbers (N), hardware
             * threads per core (K), and physical cores (N/K):
             *
             *  physical core |___core_0__|___core_1__|_core_N/K-1|
             *         thread |0|1|...|K-1|0|1|...|K-1|0|1|...|K-1|
             *  --------------|-|-|---|---|-|-|---|---|-|-|---|---|
             *   logical core | | |   |   | | |   |   | | |   |   |
             *              0 |*| |   |   | | |   |   | | |   |   |
             *              1 | | |   |   |*| |   |   | | |   |   |
             *            ... |...................................|
             *          N/K-1 | | |   |   | | |   |   |*| |   |   |
             *            N/K | |*|   |   | | |   |   | | |   |   |
             *          N/K+1 | | |   |   | |*|   |   | | |   |   |
             *            ... |...................................|
             *            N-K | | |   | * | | |   |   | | |   |   |
             *          N-K+1 | | |   |   | | |   | * | | |   |   |
             *            ... |...................................|
             *            N-1 | | |   |   | | |   |   | | |   | * |
             *
             * Thus by setting the interleave value to 1 physical cores are filled
             * first with subsequent cores past N/K adding subsequent threads
             * on already populated physical cores.  On the other hand, setting
             * interleave to K causes the algorithm to populate 0, N/K, 2N/K and
             * so on filling all hardware threads in the first physical core prior
             * to populating any threads on the second physical core.
             */
            CPU_SET(((mycore * ncores / ileave) % ncores + (mycore / ileave)), &affin_mask);
            sched_setaffinity(0, sizeof(cpu_set_t), &affin_mask);
        }

        fetchadd64_release(&ready_lock, 1);

        /* Spin until the "marshal" sets the appropriate bit */
        wait64(&sync_lock, (nthrds * 2) | 1);

        /* All threads participate in calibration */
        calibrate_timer(x, mycore);
        hold_count = x->hold;
        post_count = x->post;

        /* Wait for all threads to arrive from calibrating */
        synchronize_threads(&calibrate_lock, nthrds);
    }

    thread_local_init(mycore);

#ifdef DDEBUG
    printf("%ld %ld\n", hold_count, post_count);
#endif

    if (run_limit_ticks) {

        clock_gettime(CLOCK_MONOTONIC, &tv_monot_start);
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tv_start);

        unsigned long ticks_start = get_raw_counter();
        unsigned long ticks_end;

        do {

            for (size_t i = 0; i < run_limit_inner_loop_iters; i++) {
                /* Do a lock thing */
                prefetch64(lock);
                total_depth += lock_acquire(lock, mycore);
                blackhole(hold_count);
                lock_release(lock, mycore);
                blackhole(post_count);

                nlocks++;
            }

            ticks_end = get_raw_counter();

        } while (ticks_end - ticks_start < run_limit_ticks);

        clock_gettime(CLOCK_MONOTONIC, &tv_monot_end);
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tv_end);

    } else {

        clock_gettime(CLOCK_MONOTONIC, &tv_monot_start);
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tv_start);

        while (!target_locks || nlocks < target_locks) {
            /* Do a lock thing */
            prefetch64(lock);
            total_depth += lock_acquire(lock, mycore);
            blackhole(hold_count);
            lock_release(lock, mycore);
            blackhole(post_count);

            nlocks++;
        }

        clock_gettime(CLOCK_MONOTONIC, &tv_monot_end);
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tv_end);

    }

    if (mycore == 0)
        *(x->nstart) = (1000000000ul * tv_monot_start.tv_sec + tv_monot_start.tv_nsec);

    ns_elap = (1000000000ul * tv_end.tv_sec + tv_end.tv_nsec) - (1000000000ul * tv_start.tv_sec + tv_start.tv_nsec);
    real_ns_elap = (1000000000ul * tv_monot_end.tv_sec + tv_monot_end.tv_nsec) - (1000000000ul * tv_monot_start.tv_sec + tv_monot_start.tv_nsec);

    *(x->rst) = nlocks;
    *(x->nsec) = ns_elap;
    *(x->real_nsec) = real_ns_elap;
    *(x->depth) = total_depth;

    return NULL;
}
