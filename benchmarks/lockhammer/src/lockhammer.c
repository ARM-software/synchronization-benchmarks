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
#include <fcntl.h>

#include ATOMIC_TEST

uint64_t test_lock = 0;
uint64_t sync_lock = 0;
uint64_t ready_lock = 0;

struct arg {
    unsigned long ncores;
    unsigned long nthrds;
    unsigned long iter;
    unsigned long *lock;
    unsigned long *rst;
    unsigned long *nsec;
    unsigned long *depth;
    unsigned long *nstart;
    unsigned long hold, post;
};
typedef struct arg arg;

void* hmr(void *);

int main(int argc, char** argv)
{
    struct sched_param sparam;

    unsigned long i;
    unsigned long num_cores, num_threads;
    unsigned long locks_per_thread;
    unsigned long lock_hold_work, non_lock_work;
    unsigned long result;
    unsigned long sched_elapsed = 0, real_elapsed = 0;
    unsigned long start_ns = 0;
    double avg_lock_depth = 0.0;

    num_cores = sysconf(_SC_NPROCESSORS_ONLN);

    if (argc == 1) {
        num_threads = num_cores;
        locks_per_thread = 50000;
        lock_hold_work = 0;
        non_lock_work = 0;
    }
    else if (argc == 5) {
        num_threads = atoi(argv[1]);
        /* Do not allow number of threads to exceed online cores
           in order to prevent deadlock ... */
        num_threads = num_threads > num_cores ? num_cores : num_threads;
        locks_per_thread = atoi(argv[2]);
        lock_hold_work = atoi(argv[3]);
        non_lock_work = atoi(argv[4]);
    }
    else {
        fprintf(stderr, "Usage: %s [<cores> <threads per core> <critical loops> <post-release loops>]\n", argv[0]);
        return 1;
    }

    pthread_t hmr_threads[num_threads];
    pthread_attr_t hmr_attr;
    unsigned long hmrs[num_threads];
    unsigned long hmrtime[num_threads]; /* can't touch this */
    unsigned long hmrdepth[num_threads];
    struct timespec tv_time;

    for (i = 0; i < num_threads; ++i) hmrs[i] = 0;

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
    pthread_attr_setinheritsched(&hmr_attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&hmr_attr, SCHED_FIFO);
    sparam.sched_priority = 1;
    pthread_attr_setschedparam(&hmr_attr, &sparam);

    initialize_lock(&test_lock, num_cores);

    arg args[num_threads];
    for (i = 0; i < num_threads; ++i) {
        args[i].ncores = num_cores;
        args[i].nthrds = num_threads;
        args[i].iter = locks_per_thread;
        args[i].lock = &test_lock;
        args[i].rst = &hmrs[i];
        args[i].nsec = &hmrtime[i];
        args[i].depth = &hmrdepth[i];
        args[i].nstart = &start_ns;
        args[i].hold = lock_hold_work;
        args[i].post = non_lock_work;

        pthread_create(&hmr_threads[i], &hmr_attr, hmr, (void*)(&args[i]));
    }

    for (i = 0; i < num_threads; ++i) {
        result = pthread_join(hmr_threads[i], NULL);
    }
    /* "Marshal" thread will collect start time once all threads have
        reported ready so we only need to collect the end time here */
    clock_gettime(CLOCK_MONOTONIC, &tv_time);
    real_elapsed = (1000000000ul * tv_time.tv_sec + tv_time.tv_nsec) - start_ns;

    pthread_attr_destroy(&hmr_attr);

    result = 0;
    for (i = 0; i < num_threads; ++i) {
        result += hmrs[i];
        sched_elapsed += hmrtime[i];
        /* Average lock "depth" is an algorithm-specific auxiliary metric
           whereby each algorithm can report an approximation of the level
           of contention it observes.  This estimate is returned from each
           call to lock_acquire and accumulated per-thread.  These results
           are then aggregated and averaged here so that an overall view
           of the run's contention level can be determined. */
        avg_lock_depth += ((double) hmrdepth[i] / (double) hmrs[i]) / (double) num_threads;
    }

    fprintf(stderr, "%ld lock loops\n", result);
    fprintf(stderr, "%ld ns scheduled\n", sched_elapsed);
    fprintf(stderr, "%ld ns elapsed (~%f cores)\n", real_elapsed, ((float) sched_elapsed / (float) real_elapsed));
    fprintf(stderr, "%lf ns per access\n", ((double) sched_elapsed)/ ((double) result));
    fprintf(stderr, "%lf ns access rate\n", ((double) real_elapsed) / ((double) result));
    fprintf(stderr, "%lf average depth\n", avg_lock_depth);

    printf("%ld, %f, %lf, %lf, %lf\n",
           num_threads,
           ((float) sched_elapsed / (float) real_elapsed),
           ((double) sched_elapsed)/ ((double) result),
           ((double) real_elapsed) / ((double) result),
           avg_lock_depth);
}

void* hmr(void *ptr)
{
    unsigned long nlocks = 0;
    arg *x = (arg*)ptr;
    int rval;
    unsigned long *lock = x->lock;
    unsigned long target_locks = x->iter;
    unsigned long ncores = x->ncores;
    unsigned long nthrds = x->nthrds;
    unsigned long hold_count = x->hold;
    unsigned long post_count = x->post;

    unsigned long mycore = 0;

    struct timespec tv_monot_start, tv_start, tv_end;
    unsigned long ns_elap;
    unsigned long total_depth = 0;

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
        clock_gettime(CLOCK_MONOTONIC, &tv_monot_start);
        fetchadd64_release(&sync_lock, 1);
    }
    else {
        /* Calculate affinity mask for my core and set affinity */
        CPU_SET(((mycore >> 1)) + ((ncores >> 1) * (mycore & 1)), &affin_mask);
        sched_setaffinity(0, sizeof(cpu_set_t), &affin_mask);
        fetchadd64_release(&ready_lock, 1);

        /* Spin until the "marshal" sets the appropriate bit */
        wait64(&sync_lock, (nthrds * 2) | 1);
    }

    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tv_start);

    while (!target_locks || nlocks < target_locks) {
        /* Do a lock thing */
        prefetch64(lock);
        total_depth += lock_acquire(lock, mycore);
        spin_wait(hold_count);
        lock_release(lock, mycore);
        spin_wait(post_count);

        nlocks++;
    }
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tv_end);

    if (mycore == 0)
        *(x->nstart) = (1000000000ul * tv_monot_start.tv_sec + tv_monot_start.tv_nsec);

    ns_elap = (1000000000ul * tv_end.tv_sec + tv_end.tv_nsec) - (1000000000ul * tv_start.tv_sec + tv_start.tv_nsec);

    *(x->rst) = nlocks;
    *(x->nsec) = ns_elap;
    *(x->depth) = total_depth;

    return NULL;
}
