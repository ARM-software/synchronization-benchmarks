
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
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/mman.h>
#include <linux/mman.h>
#include <pthread.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/time.h>
#include <math.h>
#include <locale.h>
#include <signal.h>
#include <assert.h>


#include "args.h"
#include "verbose.h"
#include "lockhammer.h"
#include "perf_timer.h"
#include "alloc.h"


locks_t locks;

#define handle_error_en(en, msg) \
    do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

void* hmr(void *);

#define MAXTHREADS 513

pthread_t hmr_threads[MAXTHREADS];  // threads.

per_thread_results_t per_thread_results[MAXTHREADS];

unsigned long hwtimer_diff[MAXTHREADS];

thread_args_t thread_args[MAXTHREADS];       // thread arguments, and results

unsigned long cpu_order_count = 0;
unsigned long cpu_order[MAXTHREADS];

unsigned long hwtimer_frequency;

uint64_t * p_other_lock_memory;     // for locks.p_ready_lock, p_sync_lock, p_calibrate_lock


void disable_itimer (void);

static unsigned long calculate_affinity(const unsigned long thread_num, const system_info_t * psysinfo, const unsigned long ileave);
void setup_hmr_attr (pthread_attr_t * p_hmr_attr, test_args_t * pargs);


static unsigned long estimate_hwtimer_freq(long cpu_num);

// src/cpufreq-scaling-detect.c
int check_cpufreq_governor_is_OK_on_cpunum (unsigned long cpunum, int ignore, int verbose, int suppress);
int check_cpufreq_boost_is_OK(int ignore, int verbose, int suppress);

// in src/report.c:
void standard_report (pinorder_t * p_pinorder, unsigned long meas_number, unsigned long test_number, unsigned long iteration, unsigned long wall_elapsed_ns, test_args_t * p_test_args, const duration_t * crit, const duration_t * par);
void starting_stopping_time_report (unsigned long num_threads, unsigned long verbose, unsigned long num_acquires, pinorder_t * p_pinorder, unsigned long run_limit_ticks);
void print_summary(test_args_t * args);

// not really alarm, but itimer
void main_alarm_handler (int x) {

    size_t num_threads = thread_args[0].num_threads;

    printf("main_alarm_handler called.  terminating %zu threads.\n", num_threads);

    for (size_t i = 0; i < num_threads; i++) {
        int s = pthread_cancel(hmr_threads[i]);
        if (s) handle_error_en(x, "pthread_cancel");
    }
}

struct sigaction main_alarm_sa = {
    .sa_handler = main_alarm_handler,
    .sa_mask = {{0}},
    .sa_flags = 0
};


static int check_if_cpufreq_governors_of_pinorder_are_ok(const pinorder_t * p, int ignore, int verbose, int suppress);

static void print_iteration_string(size_t iteration, size_t total_iterations,
        unsigned long hold, Units hold_unit, unsigned long post,
        Units post_unit, unsigned long pinorder_enum,
        unsigned long num_threads, size_t measurement_counter,
        size_t total_measurements, size_t test_counter, size_t total_tests, int verbose);


static void run_one_experiment (test_args_t * args,
        struct itimerval * deadline, unsigned long * p_start_ns,
        pinorder_t * p_pinorder, unsigned long meas_number,
        unsigned long test_number, unsigned long iteration,
        const duration_t * crit, const duration_t * par);

static int get_next_available_cpu (cpu_set_t * p_avail_cpus, int num_cores, int last_cpu_seen);


static void print_system_info(system_info_t * p) {
    printf(
        "system_info:\n"
        "  num_cores = %lu\n"
        "  page_size_bytes = %zu\n"
        "  erg_bytes = %zu\n"
        "  CPU_COUNT(avail_cores) = %d\n"
        "  num_avail_cores = %zu\n"
        "  num_online_cores = %zu\n",
        p->num_cores,
        p->page_size_bytes,
        p->erg_bytes,
        CPU_COUNT(&p->avail_cores),
        p->num_avail_cores,
        p->num_online_cores
    );
}


extern const char * test_name;
extern const char * variant_name;
const char * test_type_name __attribute__((weak)) = "";


int main(int argc, char** argv)
{
    system_info_t sysinfo = {};

    // init system info
    init_sysinfo(&sysinfo);

    const unsigned long num_avail_cores = sysinfo.num_avail_cores;    // the number of cpus that the affinity mask allows.


    /* Set defaults for all command line options */
    test_args_t args =
    {
        .num_acquires = 0,     // -a number of acquires
        .crits = NULL,         // dynamic array of critical durations requested
        .pars = NULL,          // dynamic array of parallel durations requested
        .num_crits = 0,
        .num_pars = 0,
        .scheduling_policy = 0,  // -S FIFO|RR|OTHER
        .num_pinorders = 0,
        .pinorders = NULL,      // -o
        .timeout_usec = 0,
        .hugepagesz = 0,
        .use_mmap = 0,
        .mmap_hugepage_offset = 0,
        .mmap_hugepage_offset_exists = 0,
        .print_hugepage_physaddr = 0,
        .mmap_hugepage_physaddr = 0,
        .hwtimer_frequency = 0,      // default=0 means to try to probe for it
        .probed_hwtimer_frequency = 0,  // later on, if this is not 0, it means the value has been probed
        .run_limit_ticks = 0,
        .run_limit_seconds = 0,
        .run_limit_inner_loop_iters = 10,
#ifdef JSON_OUTPUT
        .json_output_filename = NULL,
#endif
        .ignore_unknown_scaling_governor = 0,
        .suppress_cpu_frequency_warnings = 0,
        .estimate_hwtimer_freq_cpu = -1,
#if defined(__aarch64__) && defined(USE_BUILTIN) && !defined(USE_LSE)
        .disable_outline_atomics_lse = 0,
#endif
        .cpuorder_filename = NULL,
        .tag = NULL,
        .verbose = VERBOSE_LOW,
        .iterations = 1,
        .blackhole_numtries = 15,   // number of binary search steps for calibrate_blackhole; undocumented
    };

    if (parse_args(argc, argv, &args, &sysinfo)) {
        return -1;
    }

    // call the test-specific argument parser, if it exists
    measure_setup_parse_test_args(&args, argc, argv);

    printf("Starting test_name=%s variant_name=%s test_type=%s tag=%s\n",
            test_name, variant_name, test_type_name, args.tag ? args.tag : "");

    if (args.verbose >= VERBOSE_MORE) {
        print_system_info(&sysinfo);
        print_test_args(&args);
    }

#ifdef __aarch64__
    if (args.disable_outline_atomics_lse) {
        handle_disable_outline_atomics_lse();
    }
#endif

    if (args.estimate_hwtimer_freq_cpu != -1) {
        unsigned long freq = estimate_hwtimer_freq(args.estimate_hwtimer_freq_cpu);
        printf("the estimated hwtimer frequency on CPU %ld in Hz is %lu\n", args.estimate_hwtimer_freq_cpu, freq);
        return 0;
    }

    if (args.print_hugepage_physaddr) {
        if (args.hugepagesz == HUGEPAGES_NONE) {
            fprintf(stderr, "ERROR: --print-hugepage-physaddr requires a hugepage size to be specified using --hugepage-size.\n");
            return -1;
        }

        if (geteuid() != 0) {
            fprintf(stderr, "ERROR: --print-hugepage-physaddr must be run as root\n");
            return -1;
        }

        void * hugepage_addr = do_hugepage_alloc(args.hugepagesz, args.mmap_hugepage_physaddr, args.verbose);
        printf("0x%lx\n", get_phys_addr((uintptr_t) hugepage_addr));
        return 0;
    }


    if (args.num_crits == 0) {
        fprintf(stderr, "ERROR: no critical time is specified (use -c flag)\n");
        return -1;
    }

    if (args.num_pars == 0) {
        fprintf(stderr, "ERROR: no parallel time is specified (use -p flag)\n");
        return -1;
    }

    if (args.run_limit_seconds && args.run_limit_ticks) {
        printf("ERROR:  can't specify both --run-limit-seconds and --run-limit-ticks\n");
        return -1;
    }

    if ((args.run_limit_seconds || args.run_limit_ticks) && args.num_acquires) {
        printf("ERROR:  can't specify both --num-acquires and one of --run-limit-seconds or --run-limit-ticks\n");
        return -1;
    }

    if (args.num_acquires == 0 && !(args.run_limit_seconds || args.run_limit_ticks)) {
        printf("ERROR:  no -a/--num-acquires, -T/--run-limit-seconds, or -O/--run-limit-ticks flags were used, or had a value of 0\n");
        return -1;
    }

    if (args.verbose >= VERBOSE_YES) {
        printf("page_size_bytes = %zu\n", sysinfo.page_size_bytes);
    }

    if (args.verbose >= VERBOSE_YES) {
        printf("Exclusive Reservation Granule size in bytes = %zu\n", sysinfo.erg_bytes);
    }

    if (args.mmap_hugepage_offset_exists && args.mmap_hugepage_offset % sysinfo.erg_bytes) {
        printf("WARNING: mmap_hugepage_offset=%zu is not an exact multiple of erg_bytes=%zu.\n",
            args.mmap_hugepage_offset, sysinfo.erg_bytes);
    }

    if (args.mmap_hugepage_physaddr) {
        if (geteuid() != 0) {
            printf("ERROR: --hugepage-physaddr must be run as root to validate the physical address obtained\n");
            exit(-1);
        }

        if (! args.use_mmap) {
            printf("ERROR: --hugepage-physaddr requires --hugepage-size to be specified\n");
            exit(-1);
        }
    }

    // try to determine hardware timer frequency through hardware or kernel-exposed means
    if (args.hwtimer_frequency == 0) {
        if (args.verbose >= VERBOSE_YES) printf("Determining timer frequency ...\n");
        hwtimer_frequency = timer_get_timer_freq();
        if (args.verbose >= VERBOSE_YES) printf("Found it as %lu Hz (which could be wrong, use --estimate-hwtimer-frequency to measure and --hwtimer-frequency to override)\n", hwtimer_frequency);
        args.probed_hwtimer_frequency = hwtimer_frequency;
    } else {
        hwtimer_frequency = args.hwtimer_frequency;
        if (args.verbose >= VERBOSE_YES) printf("Using HW timer frequency = %lu Hz from --hwtimer-frequency flag\n", hwtimer_frequency);
    }

    // p_test_lock is the lock used for the performance measurement.

    size_t alloc_length = MAXTHREADS * sysinfo.erg_bytes;

    uint64_t * p_lock_memory = do_alloc(alloc_length, args.hugepagesz, sysinfo.erg_bytes,
            args.mmap_hugepage_physaddr, args.verbose);

    // XXX: there is no check that mmap_hugepage_offset doesn't go past
    // alloc_length because hugepages may allocate more than requested so it is OK.
    p_lock_memory += args.mmap_hugepage_offset / sizeof(uint64_t);

    locks.p_test_lock      = p_lock_memory;


    // p_calibrate_lock is the last of the locks for synchronization right before the start of the measurement.
    p_other_lock_memory = aligned_alloc(sysinfo.erg_bytes, sysinfo.erg_bytes * 3);
    if (p_other_lock_memory == NULL) {
        fprintf(stderr, "could not allocate p_other_lock_memory\n"); exit (-1);
    }

    locks.p_ready_lock     = p_other_lock_memory;
    locks.p_sync_lock      = p_other_lock_memory + 1 * (sysinfo.erg_bytes / (sizeof(uint64_t)));
    locks.p_calibrate_lock = p_other_lock_memory + 2 * (sysinfo.erg_bytes / (sizeof(uint64_t)));

    push_dynamic_lock_memory(p_other_lock_memory);

    if (args.verbose >= VERBOSE_MORE) {
        if (geteuid() == 0) {
            // if root, we can show the physical address as well
            printf("locks.p_test_lock      = %p, physaddr = %p\n", locks.p_test_lock      , (void *) get_phys_addr((uintptr_t) locks.p_test_lock));
            printf("locks.p_ready_lock     = %p, physaddr = %p\n", locks.p_ready_lock     , (void *) get_phys_addr((uintptr_t) locks.p_ready_lock));
            printf("locks.p_sync_lock      = %p, physaddr = %p\n", locks.p_sync_lock      , (void *) get_phys_addr((uintptr_t) locks.p_sync_lock));
            printf("locks.p_calibrate_lock = %p, physaddr = %p\n", locks.p_calibrate_lock , (void *) get_phys_addr((uintptr_t) locks.p_calibrate_lock));
        } else {
            printf("locks.p_test_lock      = %p\n", locks.p_test_lock      );
            printf("locks.p_ready_lock     = %p\n", locks.p_ready_lock     );
            printf("locks.p_sync_lock      = %p\n", locks.p_sync_lock      );
            printf("locks.p_calibrate_lock = %p\n", locks.p_calibrate_lock );
        }
    }


    // Get frequency of hwtimer, and divide by 1B to get # of ticks per ns
    uint64_t counter_frequency = timer_get_timer_freq();

    if (args.verbose >= VERBOSE_MORE)
        printf("counter_frequency = %lu, args.hwtimer_frequency = %lu\n", counter_frequency, args.hwtimer_frequency);

    double ticks_per_ns = counter_frequency / 1000000000.0;


    // run limit is the expected duration to run for.

    double run_limit_seconds = 0;

    if (args.run_limit_seconds) {
        run_limit_seconds = args.run_limit_seconds;
        args.run_limit_ticks = run_limit_seconds * counter_frequency;
        if (args.verbose >= VERBOSE_YES)
            printf("run_limit_ticks = %lu, at %lu Hz, estimated from --run-limit-seconds %f\n",
                    args.run_limit_ticks, counter_frequency, run_limit_seconds);
    } else if (args.run_limit_ticks) {
        run_limit_seconds = args.run_limit_ticks / (double) counter_frequency;
        if (args.verbose >= VERBOSE_YES)
            printf("--run-limit-ticks %lu, at %lu Hz, is estimated to take %f seconds\n",
                    args.run_limit_ticks, counter_frequency, run_limit_seconds);
    } else {
        if (args.verbose >= VERBOSE_YES)
            printf("--num-acquires %lu per thread\n", args.num_acquires);
    }


    // --timeout-usecs is the deadline, the maximum allowed time to run for.

    struct itimerval deadline = {
        .it_interval = { .tv_sec = 0, .tv_usec = 0 },
        .it_value = { .tv_sec = 0, .tv_usec = 0 }       // time until next expiration
    };

    if (args.timeout_usec) {
        // convert usec into sec.usec
        deadline.it_value.tv_sec = args.timeout_usec / 1000000;
        deadline.it_value.tv_usec = args.timeout_usec % 1000000;
        printf("setitimer timeout = --timeout-usecs %lu   ---> %lu.%06lu seconds\n",
            args.timeout_usec,
            deadline.it_value.tv_sec,
            deadline.it_value.tv_usec);
    }

    if (args.run_limit_ticks && args.timeout_usec) {
        double deadline_it_value_sec = deadline.it_value.tv_sec + deadline.it_value.tv_usec * 1e-6;
        if (deadline_it_value_sec < run_limit_seconds) {
            printf("WARNING: setitimer timeout is shorter than run_limit_ticks, so the benchmark will end before the specified duration from -O/--run-limit-ticks\n");
        }
    }

    // read CPU ordering list
    if (args.cpuorder_filename) {
        FILE * f = fopen(args.cpuorder_filename, "r");
        if (!f) { fprintf(stderr, "ERROR: cannot open file %s\n", args.cpuorder_filename); exit(-1); }
        while (! feof(f)) {
            unsigned long cpu_number;
            int n = fscanf(f, "%lu", &cpu_number);
            if (n != 1) {
                if (feof(f)) {
                    break;
                }
                printf("ERROR: from %s, token %lu, didn't get expected number of elements and is not eof, instead got n = %d\n", args.cpuorder_filename, cpu_order_count, n);
                exit(-1);
            }
            cpu_order[cpu_order_count++] = cpu_number;
        }
        fclose(f);

        if (args.verbose >= VERBOSE_YES) {
            printf("cpu_order from %s, length = %zu\nindex\tcpu\n", args.cpuorder_filename, cpu_order_count);
            for (size_t i = 0; i < cpu_order_count; i++) {
                printf("%zu\t%lu\n", i, cpu_order[i]);
            }
        }
    }

    // create a pinorder of all CPUs if no -o pinorder / -t num_threads was given
    if (args.num_pinorders == 0) {

        fprintf(stderr, "INFO: setting thread count to the number of available cores (%zu).\n", num_avail_cores);

        args.num_pinorders = 1;
        assert(args.pinorders == NULL);
        args.pinorders = malloc(sizeof(args.pinorders[0]));
        args.pinorders[0].num_threads = num_avail_cores;
        args.pinorders[0].cpu_list = NULL;
        args.pinorders[0].ileave = 0;
        args.pinorders[0].pinorder_string = strdup("0");  // XXX: since free is called on this
    }

    // generate cpu_list if it isn't already made
    for (size_t i = 0; i < args.num_pinorders; i++) {

        pinorder_t * po = &(args.pinorders[i]);

        if (po->num_threads == 0) {             // "-t 0" was given on command line or "-i interleave"
            po->num_threads = num_avail_cores;  // number of threads allowed by sched mask
            assert(po->cpu_list == NULL);       // shouldn't have been allocated
        }

        // global data structures are sized using MAXTHREADS
        if (po->num_threads > MAXTHREADS) {
            fprintf(stderr, "ERROR: po->num_threads=%zu is greater than MAXTHREADS=%u\n"
                "Increase the value of MAXTHREADS in lockhammer.c and recompile.\n",
                po->num_threads, MAXTHREADS);
            exit(-1);
        }

        if (po->cpu_list != NULL) {
            continue;
        }

        // if here, didn't already have a cpu_list, so allocate it.
        po->cpu_list = malloc(sizeof(po->cpu_list[0]) * po->num_threads);
        if (po->cpu_list == NULL) {
             fprintf(stderr, "ERROR: can't allocate memory for a pinorder CPU list\n");
             exit(-1);
        }

        if (po->ileave) {
            // This pinorder is from a -i interleave_order
            // NOTE: This is calculated using sysinfo.num_cores (all CPUs configured by OS)
            // which can result in a placement on an offline CPU/not allowed by affinity mask.
            for (size_t j = 0; j < po->num_threads; j++) {
                po->cpu_list[j] = calculate_affinity(j, &sysinfo, po->ileave);
            }
            // printf("ileave pinorder string = %s\n", po->pinorder_string);
        } else if (cpu_order_count) {
            // There is a cpu_order read from cpuorder_filename, so assign cpu_list from it.
            // For -t num_threads, but allocated in the order from cpuorder_filename
            // This will not place on CPUs that are not available in the sysinfo.avail_cores mask
            int cpu_order_index = 0;

            for (size_t j = 0; j < po->num_threads; j++) {
                int found = 0;
                do {
                    int cpu_candidate = cpu_order[cpu_order_index++];
                    if (CPU_ISSET(cpu_candidate, &sysinfo.avail_cores)) {
                        po->cpu_list[j] = cpu_candidate;
                        found = 1;
                    } else if (cpu_order_index >= cpu_order_count) {
                        fprintf(stderr, "ERROR: no CPU is available after assigning %zu CPUs\n", j);
                        exit(-1);
                    }
                } while (! found);
            }
            // printf("cpu_order_count pinorder string = %s\n", po->pinorder_string);
        } else {
            // For -t num_threads allocated in CPU numerical order of sysinfo.avail_cores mask.
            int cpu = -1;
            for (size_t j = 0; j < po->num_threads; j++) {
                cpu = get_next_available_cpu(&sysinfo.avail_cores, sysinfo.num_cores, cpu);
                if (cpu == -1) {
                    fprintf(stderr, "ERROR: no CPU is available after assigning %zu CPUs\n", j);
                    exit(-1);
                }
                po->cpu_list[j] = cpu;
            }
            // printf("-t pinorder string = %s\n", po->pinorder_string);
        }

        if (args.verbose >= VERBOSE_YES) {
            printf("thread-to-cpu assignment for pinorder number %zu:", i);
            for (size_t j = 0; j < po->num_threads; j++) {
                printf(" %zu->%d", j, po->cpu_list[j]);
            }
            printf("\n");
        }

    }


    // check if boost is enabled

    if (! check_cpufreq_boost_is_OK(args.ignore_unknown_scaling_governor, args.verbose, args.suppress_cpu_frequency_warnings)) {
        if (! args.ignore_unknown_scaling_governor) {
            fprintf(stderr, "ERROR: boost is enabled, but --ignore-unknown-scaling-governor / -Y was NOT set.  Aborting!\n");
            exit(-1);
        }
        if (! args.suppress_cpu_frequency_warnings) {
            fprintf(stderr, "WARNING: boost is enabled, but --ignore-unknown-scaling-governor / -Y was specified, so continuing anyway!\n");
        }
    }


    // for each defined pinorder, run one experiment.

    // if there are any pinorders with num_threads > 0, run them

    if (! args.pinorders[0].num_threads) {
        fprintf(stderr, "ERROR: no pinorders have been defined! This should never happen!\n");
        exit(-1);
    }

    if (args.verbose >= VERBOSE_YES)
        printf("processing pinorders\n");

    // check all pinorders' CPUs' frequency scaling setting

    int a_pinorder_failed = 0;

    for (size_t i = 0; i < args.num_pinorders; i++) {
        pinorder_t * p = &(args.pinorders[i]);
        a_pinorder_failed |= ! check_if_cpufreq_governors_of_pinorder_are_ok(p, args.ignore_unknown_scaling_governor, args.verbose, args.ignore_unknown_scaling_governor);
    }

    if (a_pinorder_failed) {
        if (! args.ignore_unknown_scaling_governor) {
            fprintf(stderr, "ERROR: At least one pinorder's CPU frequency scaling setting has issues, but --ignore-unknown-scaling-governor / -Y was NOT set.  Aborting!\n");
            exit(-1);
        }
        if (! args.suppress_cpu_frequency_warnings) {
            fprintf(stderr, "WARNING: At least one pinorder's CPU frequency scaling setting has issues, but --ignore-unknown-scaling-governor / -Y was specified, so continuing anyway!\n");
        }
    }

    size_t total_measurements = 0;
    size_t measurement_counter = 0;
    size_t total_tests = 0;
    size_t test_counter = 0;

    for (size_t ipinorder = 0; ipinorder < args.num_pinorders; ipinorder++) {
    for (size_t icrit = 0; icrit < args.num_crits; icrit++) {
    for (size_t ipar = 0; ipar < args.num_pars; ipar++) {
        total_tests++;
    for (size_t iteration = 1; iteration <= args.iterations; iteration++) {
        total_measurements++;
    }
    }
    }
    }


    for (size_t ipinorder = 0; ipinorder < args.num_pinorders; ipinorder++) {
        pinorder_t * p = &(args.pinorders[ipinorder]);
    for (size_t icrit = 0; icrit < args.num_crits; icrit++) {
    for (size_t ipar = 0; ipar < args.num_pars; ipar++) {
        unsigned long crit = args.crits[icrit].t;
        unsigned long par = args.pars[ipar].t;
        Units crit_unit = args.crits[icrit].unit;
        Units par_unit = args.pars[ipar].unit;

        test_counter++;
        for (size_t iteration = 1; iteration <= args.iterations; iteration++) {
            unsigned long start_ns = 0;
            measurement_counter++;
            print_iteration_string(iteration, args.iterations, crit, crit_unit, par, par_unit, ipinorder, p->num_threads, measurement_counter, total_measurements, test_counter, total_tests, args.verbose);

            for (size_t i = 0; i < p->num_threads; i++) {
                thread_args[i].thread_num = i;
                thread_args[i].num_threads = p->num_threads;
                thread_args[i].num_acquires = args.num_acquires;
                thread_args[i].lock = locks.p_test_lock;    // this is the main lock.

                thread_args[i].p_start_ns = &start_ns;      // only marshal thread sets this.
                thread_args[i].hold = crit;
                thread_args[i].post = par;
                thread_args[i].hold_unit = crit_unit;
                thread_args[i].post_unit = par_unit;
                thread_args[i].tickspns = ticks_per_ns;

                thread_args[i].run_on_this_cpu = p->cpu_list[i];
                thread_args[i].run_limit_ticks = args.run_limit_ticks;
                thread_args[i].run_limit_inner_loop_iters = args.run_limit_inner_loop_iters;
                thread_args[i].results.cpu_affined = 0;  // XXX should this line exist since results is just output?

                thread_args[i].hwtimer_frequency = hwtimer_frequency;

                thread_args[i].verbose = args.verbose;
                thread_args[i].blackhole_numtries = args.blackhole_numtries;
            }

            run_one_experiment(&args, &deadline, &start_ns, p, measurement_counter, test_counter, iteration, &args.crits[icrit], &args.pars[ipar]);
        }
    }
    }
    }

    // clear iteration string line
    if (args.verbose == VERBOSE_LOW)
        fprintf(stdout, "\33[2K\r");

    print_summary(&args);

    // clean up all malloc'd memory tracked in dynamic_lock_memory_base
    // TODO: track all heap memory allocated by tests' initialize_lock()
    free_dynamic_lock_memory();

    free(args.crits);
    free(args.pars);
    for (size_t i = 0; i < args.num_pinorders; i++) {
        free(args.pinorders[i].cpu_list);
        if (args.pinorders[i].pinorder_string) {
            free(args.pinorders[i].pinorder_string);
        }
    }
    free(args.pinorders);
    return 0;
}


static void run_one_experiment (test_args_t * args,
        struct itimerval * deadline, unsigned long * p_start_ns,
        pinorder_t * p_pinorder, unsigned long meas_number,
        unsigned long test_number, unsigned long iteration,
        const duration_t * crit, const duration_t * par)
{
    unsigned long num_threads = p_pinorder->num_threads;

    *p_start_ns = 0;

    pthread_attr_t hmr_attr;
    setup_hmr_attr(&hmr_attr, args);

    long thread_return_code[num_threads];
    for (size_t i = 0; i < num_threads; i++) {
        thread_return_code[i] = 0;
    }

    // TODOs for being able to rerun with different pinorder and thread count
    // TODO: one-time blackhole calibration optional
    // TODO: save/restore thread scheduling affinity after an experiment

    // ensure that locks are cleared before reuse
    *(locks.p_test_lock) = 0;
    *(locks.p_sync_lock) = 0;
    *(locks.p_calibrate_lock) = 0;
    *(locks.p_ready_lock) = 0;

    measure_setup_initialize_lock(&locks, p_pinorder);

    // ------ time is somewhat important starting here ------

    if (args->run_limit_ticks || args->timeout_usec) {
        setitimer(ITIMER_REAL, deadline, NULL);
        sigaction(SIGALRM, &main_alarm_sa, NULL);
    }

    // launch threads
    for (size_t i = 0; i < num_threads; ++i) {
        int s = pthread_create(&hmr_threads[i], &hmr_attr, hmr, (void*)(&thread_args[i]));
        if (s)
             handle_error_en(s, "pthread_create");
    }

    // wait for threads
    for (size_t i = 0; i < num_threads; ++i) {
        void * pthread_exit_status = (void *) &(thread_return_code[i]);
        int s = pthread_join(hmr_threads[i], &pthread_exit_status);
        if (s)
             handle_error_en(s, "pthread_join");
        if (pthread_exit_status == PTHREAD_CANCELED) {
            printf("thread %zu was cancelled, lock_acquires = %lu\n", i, thread_args[i].results.lock_acquires);
            thread_return_code[i] = 0;
        }
    }

    /* "Marshal" thread will collect start time once all threads have
        reported ready so we only need to collect the end time here */
    struct timespec wall_time_end;
    clock_gettime(CLOCK_MONOTONIC, &wall_time_end);

    // ------ time is important ending here ------

    disable_itimer();

    if (args->verbose >= VERBOSE_YES)
    printf("Measurement completed.\n");

    if (args->verbose >= VERBOSE_MORE)
        for (size_t i = 0; i < num_threads; i++) {
            printf("thread_return_code[%zu] = %ld\n", i, thread_return_code[i]);
        }

    // wall_elapsed_ns is the wallclock time from when the marshal thread
    // started its measurement until the clock_gettime() a few lines above.
    // These are measured by separate processes so the assumption is
    // clock_gettime(CLOCK_MONOTONIC) returns a true wall clock time.

    unsigned long wall_elapsed_ns = timespec_to_ns(&wall_time_end) - *p_start_ns;

    // report results

    standard_report(p_pinorder, meas_number, test_number, iteration, wall_elapsed_ns, args, crit, par);
    starting_stopping_time_report(num_threads, args->verbose, args->num_acquires, p_pinorder, args->run_limit_ticks);

    pthread_attr_destroy(&hmr_attr);

    return;
}

static unsigned long calculate_affinity(const unsigned long thread_num, const system_info_t * psysinfo, const unsigned long ileave) {

    /*
     * The "interleave" parameter given through -t num_threads:interleave is
     * used to place worker threads on algorithmically selected logical CPUs
     * with the intent of using separate physical cores vs. hardware threads
     * within the same physical core.
     *
     * This algorithm computes a target logical CPU number, but it does not
     * confirm whether that CPU is online or not, and if it is not, an error
     * will occur when the CPU affinity scheduling is attempted.
     *
     * This algorithm does not work well for systems of hybrid CPUs consisting
     * of threaded vs. non-threaded cores, systems with disabled CPUs, and
     * multisocket systems, so the usability of the interleave parameter is
     * highly system-dependent.
     *
     * A more flexible alternative is to use the pinorder -o cpulist flag to
     * assign worker threads onto individually-chosen logical CPUs.
     *
     * For this discussion, consider the following relationship between logical
     * core numbers (N), hardware threads per core (K), and physical cores
     * (N/K), where sequentially increasing logical CPU numbers map to a
     * hwthread on separate physical cores and then wrap around to another
     * hwthread on an already-visited physical core.
     *
     *  physical core |___core_0__|___core_1__|_core_N/K-1|
     *       hwthread |0|1|...|K-1|0|1|...|K-1|0|1|...|K-1|
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
     * Thus, by setting interleave=1, physical cores are filled first with
     * subsequent cores past N/K adding subsequent threads on already populated
     * physical cores.  On the other hand, setting interleave=K to K causes the
     * algorithm to populate 0, N/K, 2N/K and so on filling all hardware
     * threads in the first physical core prior to populating any threads on
     * the second physical core.
     *
     * Now, consider a logical to physical CPU arrangement where sequentially
     * numbered CPUs map to hwthreads first and then to physical cores;
     * interleave=1 would place as such, so for physical cores first, use
     * interleave=K.
     *
     *  physical core |___core_0__|___core_1__|_core_N/K-1|
     *         thread |0|1|...|K-1|0|1|...|K-1|0|1|...|K-1|
     *  --------------|-|-|---|---|-|-|---|---|-|-|---|---|
     *   logical core | | |   |   | | |   |   | | |   |   |
     *              0 |*| |   |   | | |   |   | | |   |   |
     *              1 | |*|   |   | | |   |   | | |   |   |
     *            ... |...................................|
     *            K-1 | | |   |  *| | |   |   | | |   |   |
     *              K | | |   |   |*| |   |   | | |   |   |
     *            K+1 | | |   |   | |*|   |   | | |   |   |
     *            ... |...................................|
     *           2K-1 | | |   |   | | |   |  *| | |   |   |
     *            ... |...................................|
     *      (N/K-1)*K | | |   |   | | |   |   |*| |   |   |
     *    1+(N/K-1)*K | | |   |   | | |   |   | |*|   |   |
     *            ... |...................................|
     *  K-1+(N/K-1)*K | | |   |   | | |   |   | | |   |  *| // this logical CPU number would be equal to N-1
     *
     */

    // num_cores = number of CPUs configured by the OS, basically all CPUs
    // num_online_cores = getconf(_NPROCESSORS_ONLN) (includes online isolcpus)
    // num_avail_cores = number of cores allowed by CPU affinity mask (excludes online isolcpus), basically CPU_COUNT(avail_cores)
    // avail_cores = the CPU affinity mask

    if (!thread_num && psysinfo->num_avail_cores != psysinfo->num_online_cores) {
        // What we want to do is detect if there might be holes in the
        // avail_cores schedmask because that may mean calculate_affinity()
        // schedules onto a masked-off processor.  numactl --cpunodebind
        // uses the modified CPU affinity mask to set affinity, which will
        // also cause this warning to be printed.

        fprintf(stderr, "WARNING: in interleaving mode, the number of available cores from processor affinity schedmask (%zu) does not equal the number of online cores (%zu)\n",
            psysinfo->num_avail_cores, psysinfo->num_online_cores);
    }

    if (!thread_num && psysinfo->num_cores != psysinfo->num_online_cores) {
        // This detects if there are CPUs that are offline on which
        // calculate_affinity() may improperly try to place a thread.
        fprintf(stderr, "WARNING: in interleaving mode, the number of configured cores (%zu) does not equal the number of online cores (%zu)\n",
            psysinfo->num_cores, psysinfo->num_online_cores);
    }

    if (thread_num >= psysinfo->num_cores) {
        // the code below does not assign to threads correctly if the number of threads to assign is greater than the number of cores.
        fprintf(stderr, "ERROR: unexpectedly thread_num >= num_cores in calculate_affinity().\n");
        exit(-1);
    }

    unsigned long thread_offset = 0;
    unsigned long base_core = 0;

    for (unsigned long i = 0; i < thread_num; i++) {
        base_core += ileave;
        if (base_core >= psysinfo->num_cores) {
            base_core = 0;
            thread_offset++;
        }
    }

    unsigned long target_core = base_core + thread_offset;

//  printf("thread_num = %lu, base_core = %lu, thread_offset = %lu, target_core = %lu\n",
//         thread_num, base_core, thread_offset, target_core);

    return target_core;
}




void setup_hmr_attr (pthread_attr_t * p_hmr_attr, test_args_t * pargs) {

    /* Select the FIFO scheduler.  This prevents interruption of the
       lockhammer test threads allowing for more precise measurement of
       lock acquisition rate, especially for mutex type locks where
       a lock-holding or queued thread might significantly delay forward
       progress if it is rescheduled.  Additionally, the FIFO scheduler allows
       for a better guarantee of the requested contention level by ensuring
       that a fixed number of threads are executing simultaneously for
       the duration of the test.  This comes at the significant cost of
       reduced responsiveness of the system under test and the possibility
       for system instability if the FIFO scheduled threads remain runnable
       for too long, starving other processes.  Care should be taken in
       invocation to ensure that a given instance of lockhammer runs for
       no more than a few milliseconds and lockhammer should never be run
       on an already-deplayed system. */

    int s = pthread_attr_init(p_hmr_attr);
    if (s) handle_error_en(s, "pthread_attr_init");

    // if root and a policy is set thru -S, apply it; otherwise, don't change it.
    if (pargs->scheduling_policy) {
        if (0 != getuid()) {
            fprintf(stderr, "ERROR: need to be root to use -S/--scheduling-policy flag\n");
            exit(-1);
        }

#if 0
        int policy;
        struct sched_param current_sched_param;
        s = pthread_getschedparam(pthread_self(), &policy, &current_sched_param);
        if (s) handle_error_en(s, "pthread_getschedparam");
        printf("policy = %d, current_sched_param.sched_priority = %d\n",
                policy, current_sched_param.sched_priority);
#endif

        s = pthread_attr_setinheritsched(p_hmr_attr, PTHREAD_EXPLICIT_SCHED);
        if (s) handle_error_en(s, "pthread_attr_setinheritsched");

        // SCHED_FIFO, SCHED_RR, and SCHED_OTHER (OTHER is 0)
        s = pthread_attr_setschedpolicy(p_hmr_attr, pargs->scheduling_policy);
        if (s) handle_error_en(s, "pthread_attr_setschedpolicy");

        struct sched_param sparam = { .sched_priority = 1 };
        s = pthread_attr_setschedparam(p_hmr_attr, &sparam);
        if (s) handle_error_en(s, "pthread_attr_setschedparam");
    }
}



void disable_itimer (void) {

    // disable interval timer by setting it to 0
    struct itimerval disable_timer = {
        .it_interval = { .tv_sec = 0, .tv_usec = 0 },
        .it_value = { .tv_sec = 0, .tv_usec = 0 }
    };

    setitimer(ITIMER_REAL, &disable_timer, NULL);    // XXX: check return value
}


static unsigned long estimate_hwtimer_freq(long cpu_num) {

    const unsigned long n = 10;
    const struct timeval duration = { .tv_sec = 1, .tv_usec = 0 };

    printf("Estimating HW timer frequency on CPU %ld for %lu iterations of %lu.%06lu seconds each\n",
            cpu_num, n, duration.tv_sec, duration.tv_usec);

    cpu_set_t cpu_mask;

    CPU_ZERO(&cpu_mask);
    CPU_SET(cpu_num, &cpu_mask);

    if (0 != sched_setaffinity(0, sizeof(cpu_mask), &cpu_mask)) {
        perror("sched_setaffinity");
        exit(-1);
    }

    unsigned long hwtimer_average = estimate_hwclock_freq(n, 1, duration);

    return hwtimer_average;
}

static int check_if_cpufreq_governors_of_pinorder_are_ok(const pinorder_t * p, int ignore, int verbose, int suppress) {
    // XXX: this checks CPUs reused between pinorders multiple times
    int all_ok = 1;
    for (size_t i = 0; i < p->num_threads; ++i) {
        // check_cpufreq_governor_is_OK_on_cpunum() returns 0 if the CPU p->pinorder[i] is not OK
        all_ok &= check_cpufreq_governor_is_OK_on_cpunum(p->cpu_list[i], ignore, verbose, suppress);
    }
    return all_ok;
}

const char * unit_to_string[] = {
    [NS] = "ns",
    [INSTS] = "inst",
    [NOT_SET] = "not set"   // should never be able to print this
};

static void print_iteration_string(size_t iteration, size_t total_iterations,
        unsigned long hold, Units hold_unit, unsigned long post,
        Units post_unit, unsigned long pinorder_enum,
        unsigned long num_threads, size_t measurement_counter,
        size_t total_measurements, size_t test_counter, size_t total_tests, int verbose) {

    // at this point, an iteration line has been printed on a prior call, or this is the first and none have been printed yet.

    if (verbose < VERBOSE_LOW) return;

    // ONLY in VERBOSE_LOW mode, clear the line and reset the cursor to the row start
    if (verbose == VERBOSE_LOW)
        fprintf(stdout, "\33[2K\r");
    else
        printf("\n");

    if (verbose >= VERBOSE_YES)
        printf("---------------------------------------------------------------------------------\n");

    fprintf(stdout, "measurement %zu/%zu (test %zu/%zu iteration %zu/%zu), critical=%lu%s parallel=%lu%s, pinorder=%lu num_threads=%lu",
        measurement_counter, total_measurements, test_counter, total_tests, iteration, total_iterations,
        hold, unit_to_string[hold_unit], post, unit_to_string[post_unit], pinorder_enum, num_threads);

    // ONLY in VERBOSE_LOW mode, force the printing of the iteration string without a newline
    if (verbose == VERBOSE_LOW)
        fflush(stdout);
    else
        fprintf(stdout, "\n");

    if (verbose >= VERBOSE_YES)
        printf("---------------------------------------------------------------------------------\n");
}

static int get_next_available_cpu (cpu_set_t * p_avail_cpus, int num_cores, int last_cpu_seen) {
    int cpu = last_cpu_seen + 1;

    for (; cpu < num_cores; cpu++) {
        if (CPU_ISSET(cpu, p_avail_cpus)) {
            //printf("found cpu=%d to be available\n", cpu);
            return cpu;
        }
    }

    return -1;
}


unsigned long estimate_hwclock_freq(size_t n, int verbose, struct timeval target_measurement_duration) {

    unsigned long hwcounter_start, hwcounter_stop, hwcounter_diff;
    unsigned long hwcounter_average = 0;

    assert(n != 0); // can't handle only 1 sample

    size_t high_i = 0, low_i = 0;

    unsigned long hwcounter_freq_high = 0;
    unsigned long hwcounter_freq_low = -1;

    for (size_t i = 0; i < n + 2; i++) {

        struct timeval ts_a, ts_b, ts_target, ts_diff;

        do {
            hwcounter_start = get_raw_counter();
            gettimeofday(&ts_a, NULL);

            timeradd(&ts_a, &target_measurement_duration, &ts_target);

            do {
                gettimeofday(&ts_b, NULL);
                hwcounter_stop = get_raw_counter();
            } while (timercmp(&ts_b, &ts_target, < ));


            timersub(&ts_b, &ts_target, &ts_diff);

            if (0)  // expect 0.000000
                printf("ts_diff = %lu.%06lu\n", ts_diff.tv_sec, ts_diff.tv_usec);

        } while (ts_diff.tv_sec > 0 || ts_diff.tv_usec > 100);

        hwcounter_diff = hwcounter_stop - hwcounter_start;

        timersub(&ts_b, &ts_a, &ts_diff);

        unsigned long hwcounter_freq =
                    hwcounter_diff / (ts_diff.tv_sec + ts_diff.tv_usec * 0.000001);

        if (verbose) {
            printf("sample %zu, hwcounter_diff = %lu, freq = %lu\n",
                    i, hwcounter_diff, hwcounter_freq);
        }

        hwcounter_average += hwcounter_freq;

        if (hwcounter_freq > hwcounter_freq_high) {
            hwcounter_freq_high = hwcounter_freq;
            high_i = i;
        }

        if (hwcounter_freq < hwcounter_freq_low) {
            hwcounter_freq_low = hwcounter_freq;
            low_i = i;
        }

    }

    if (verbose) {
        printf("dropped sample %zu, hwcounter_freq_low = %lu\n", low_i, hwcounter_freq_low);
        printf("dropped sample %zu, hwcounter_freq_high = %lu\n", high_i, hwcounter_freq_high);
    }

    hwcounter_average -= hwcounter_freq_low;
    hwcounter_average -= hwcounter_freq_high;

    hwcounter_average /= (double) n;

    // printf("hwcounter_average = %lu\n", hwcounter_average);

    return hwcounter_average;
}



/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
