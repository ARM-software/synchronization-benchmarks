
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
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>

#include "verbose.h"
#include "lockhammer.h"
#include "args.h"
#include "alloc.h"



static void new_print_usage (const char * invoc) {
    fprintf(stderr,
    "%s [args]\n"
    "\n"
    "processor affinity selection (need at least one of -t or -o; permuted on each):\n"
    " -o | --pinning-order   m[,m[-n[:s]]...]      run on CPU m; CPU m-n; CPU m-n skipping by s\n"
    " -t | --num-threads     threads[:interleave]  number of threads to use\n"
    "        interleave = 0  Enumerate CPUs from the existing affinity mask (this is the default)\n"
    "        interleave >= 1 algorithmically increment CPU number, e.g. -t 3:2 means CPU 0,2,4\n"
    "                        Overrides processor affinity mask, or misplace on an offline CPU.\n"
    " -C | --cpuorder-file         filename        for -t/--num-threads, allocate CPUs by number in the order in this text file\n"
    "\n"
    "lock durations (at least one of both critical and parallel duration must be specified; will be permuted):\n"
    " -c | --critical              duration[ns|in] critical duration measured in nanoseconds (use \"ns\" suffix) or instructions (use \"in\" suffix; default is \"in\" if omitted)\n"
    " -p | --parallel              duration[ns|in] parallel duration measured in nanoseconds (use \"ns\" suffix) or instructions (use \"in\" suffix; default is \"in\" if omitted)\n"
    "--cn| --critical-nanoseconds  nanoseconds     upon acquiring a lock, duration to hold the lock (\"-c 1234ns\" equivalent)\n"
    "--ci| --critical-instructions instructions    upon acquiring a lock, number of spin-loop instructions to run while holding the lock (\"-c 1234in\" equivalent)\n"
    "--pn| --parallel-nanoseconds  nanoseconds     upon releasing a lock, duration to wait before attempting to reacquire the lock (\"-p 1234ns\" equivalent)\n"
    "--pi| --parallel-instructions instructions    upon releasing a lock, number of spin-loop instructions to run while before attempting to reacquire the lock (\"-p 1234in\" equivalent)\n"
    "\n"
    "experiment iterations:\n"
    " -n | --iterations            integer         number of times to run each measurement\n"
    "\n"
    "experiment length (work-based):\n"
    " -a | --num-acquires          integer         number of acquires to do per thread\n"
    "\n"
    "experiment length (time-based):\n"
    " -D | --run-limit-seconds     float_seconds   each worker thread runs for this number of seconds\n"
    " -O | --run-limit-ticks       integer         each worker thread runs for this number of hardware timer ticks\n"
    " -I | --run-limit-inner-iterations  integer   number of inner iterations of measurement between hardware timer polls\n"
    "      --hwtimer-frequency     freq_hertz      Override HW timer frequency in Hertz instead of trying to determine it\n"
    "      --estimate-hwtimer-frequency cpu_num    Estimate HW timer frequency on cpu_num\n"
    "      --timeout-usecs         integer         kill benchmark if it exceeds this number of microseconds\n"
    "\n"
    "scheduler control:\n"
    " -S | --scheduling-policy     FIFO|RR|OTHER   set explicit scheduling policy of created threads (needs root)\n"
    "\n"
    "memory placement control (hugepages):\n"
    " -M | --hugepage-size  <integer|help|default> use hugetlb page for lock memory; see \"-M help\" for sizes\n"
    "      --hugepage-offset       integer         if --hugepage-size is used, the byte offset into the hugepage for the tests' lock\n"
    "      --hugepage-physaddr     physaddr        obtain only the hugepage with the physaddr specified (must run as root)\n"
    "      --print-hugepage-physaddr               print the physical address of the hugepage obtained, and then exit (must run as root)\n"
    "\n"
    "other:\n"
#ifdef JSON_OUTPUT
    "      --json filename                         save results to filename as a json\n"
#endif
//  "      --blackhole-numtries    integer         number of binary search steps for calibrate_blackhole\n"  //undocumented
    " -Y | --ignore-unknown-scaling-governor       do not exit as error if CPU scaling driver+governor is known bad/not known good\n"
    " -Z | --suppress-cpu-frequency-warnings       suppress CPU frequecy scaling / governor warnings\n"
#ifdef __aarch64__
    "      --disable-outline-atomics-lse           disable use of LSE in outline atomics\n"
#endif
    " -T | --tag                   string          tag string to store in JSON\n"
    " -v | --verbose                               print verbose messages (use 2x for more verbose)\n"
    "      --more-verbose                          print more verbose messages\n"
    "\n"
    "lock-specific:\n"
    " -- <workload-specific arguments>             lock-specific arguments are passed after --\n"
    // TODO: provide lock-specific help message here
    "\n"
    , invoc);
}


// returns number of bytes per reservation granule (usually cache line length)
static size_t get_ctr_erg_bytes(void) {
#if defined(__aarch64__)
    // Exclusive reservation granule ranges from 4 to 512 words.  Read from CTR_EL0.
    size_t CTR, ERG, ERG_words;
    asm volatile ("mrs %0, CTR_EL0" : "=r" (CTR));
    ERG = (CTR >> 20) & 0xF;
    if (ERG == 0) {
        // According to Arm ARM, if CTR[ERG] == 0, assume 512 words (2KB)
        ERG_words = 512;
    } else {
        ERG_words = 1 << ERG;
    }
    return ERG_words * 4;
#elif defined(__x86_64__)
    return 64;
#else
#error neither __aarch64__ nor __x86_64__ are defined in get_ctr_erg_bytes()
#endif
}


// init_sysinfo - probe system configuration
int init_sysinfo(system_info_t * psysinfo) {

    // get the number of all possible processors
    psysinfo->num_cores = sysconf(_SC_NPROCESSORS_CONF);
    psysinfo->num_online_cores = sysconf(_SC_NPROCESSORS_ONLN);

    // cache line size
    psysinfo->erg_bytes = get_ctr_erg_bytes();

    // page size
    psysinfo->page_size_bytes = sysconf(_SC_PAGE_SIZE);

    // sched_getaffinity() returns the affinity mask of the calling process,
    // which are the CPUs it can be scheduled on, but that doesn't mean the
    // CPU isn't online.  For example, isolcpus are online but not schedulable.

    // get the set of schedulable processors allowed
    CPU_ZERO(&psysinfo->avail_cores);

    int ret = sched_getaffinity(0, sizeof(cpu_set_t), &psysinfo->avail_cores);
    if (ret == -1) { perror("sched_getaffinity"); exit(EXIT_FAILURE); }

    psysinfo->num_avail_cores = CPU_COUNT(&psysinfo->avail_cores);

#if 0
    // prints the CPUs on which that we can be scheduled
    printf("scheduleable:");
    for (size_t i = 0; i < psysinfo->num_cores; i++) {
        if (CPU_ISSET(i, &psysinfo->avail_cores)) {
            printf(" %zu", i);
        } else {
            printf(" !%zu", i);
        }
    }
    printf("\n");
#endif

    return 0;
}

static int equals_in (const char * s) {
    return (strcmp(s, "in") == 0);
}

static int equals_ns (const char * s) {
    return (strcmp(s, "ns") == 0);
}

static int duration_parse_error (const char * endptr, const char * optarg) {
    const int debug = 0;

    if (*endptr) {
        if (equals_ns(endptr) || equals_in(endptr)) {
            if (optarg == endptr) {
                if (debug) printf("ends with ns or in, but optarg == endptr (i.e. the whole optarg is \"ns\" or \"in\"), so error\n");
                return 1;
            }
            if (debug) printf("ends with ns, so no error\n");
            return 0;
        }
        if (debug) printf("does not end with ns, so error\n");
        return 1;
    }

    if (debug) printf("endptr points to a null char, no error\n");
    return 0;
}

static struct { const char * name; int value; } scheduling_policy_map[] = {
    { "NOT SET", -1 },
    { "FIFO", SCHED_FIFO },
    { "RR", SCHED_RR },
    { "OTHER", SCHED_OTHER },
};

static int parse_scheduling_policy(const char * optarg) {
    size_t num_policies = sizeof(scheduling_policy_map) / sizeof(scheduling_policy_map[0]);
    for (size_t i = 0; i < num_policies; i++) {
        if (0 == strcasecmp(optarg, scheduling_policy_map[i].name)) {
            printf("INFO: using explict scheduling policy %s\n", scheduling_policy_map[i].name);
            return scheduling_policy_map[i].value;
        }
    }

    fprintf(stderr, "ERROR: unknown scheduling policy %s\n", optarg);
    exit(-1);
    return -1; // shouldn't get here
}


size_t parse_num_threads(char * s, unsigned long * p_numbers);
size_t parse_cpulist_range(char * s, unsigned long * p_numbers);

extern char * optarg;
extern int opterr;

int parse_args(int argc, char ** argv, test_args_t * pargs, const system_info_t * psysinfo) {

    if (argc == 1) {
        fprintf(stderr, "ERROR: no flags have been specified.  Use -h to see help.\n");
        return -1;
    }

    enum {
        longopt_hugepage_offset,
        longopt_hugepage_physaddr,
        longopt_print_hugepage_physaddr,
        longopt_verbose,
        longopt_timeout_usecs,
        longopt_critical_instructions,
        longopt_critical_nanoseconds,
        longopt_parallel_instructions,
        longopt_parallel_nanoseconds,
        longopt_run_limit_seconds,
#ifdef JSON_OUTPUT
        longopt_json_filename,
#endif
        longopt_ignore_unknown_scaling_governor,
        longopt_suppress_cpu_frequency_warnings,
        longopt_hwtimer_frequency,
        longopt_estimate_hwtimer_freq_cpu,
        longopt_cpuorder_filename,
        longopt_more_verbose,
        longopt_blackhole_numtries,
        longopt_disable_outline_atomics_lse,
        longopt_tag,
    };

    static struct option long_options[] = {
        // *name                has_arg             *flag       val (returned or stored thru *flag)
        {"num-threads",         required_argument,  NULL,         't'},
        {"cpuorder-file",       required_argument,  NULL,         longopt_cpuorder_filename},
        {"critical-instructions", required_argument,NULL,         longopt_critical_instructions},
        {"ci",                    required_argument,NULL,         longopt_critical_instructions},
        {"critical-nanoseconds",  required_argument,NULL,         longopt_critical_nanoseconds},
        {"cn",                    required_argument,NULL,         longopt_critical_nanoseconds},
        {"parallel-instructions", required_argument,NULL,         longopt_parallel_instructions},
        {"pi",                    required_argument,NULL,         longopt_parallel_instructions},
        {"parallel-nanoseconds",  required_argument,NULL,         longopt_parallel_nanoseconds},
        {"pn",                    required_argument,NULL,         longopt_parallel_nanoseconds},
        {"critical",            required_argument,  NULL,         'c'},
        {"parallel",            required_argument,  NULL,         'p'},
        {"scheduling-policy",   required_argument,  NULL,         'S'},
        {"pinning-order",       required_argument,  NULL,         'o'},
        {"iterations",          required_argument,  NULL,         'n'},
        {"run-limit-ticks",     required_argument,  NULL,         'O'},
        {"run-limit-seconds",   required_argument,  NULL,         longopt_run_limit_seconds},
        {"run-limit-inner-iterations", required_argument, NULL,   'I'},
        {"timeout-usecs",       required_argument,  NULL,         longopt_timeout_usecs},
        {"hugepage-size",       required_argument,  NULL,         'M'},
        {"hugepage-offset",     required_argument,  NULL,         longopt_hugepage_offset},
        {"hugepage-physaddr",   required_argument,  NULL,         longopt_hugepage_physaddr},
        {"print-hugepage-physaddr", no_argument,    NULL,         longopt_print_hugepage_physaddr},
        {"num-acquires",        required_argument,  NULL,         'a'},
#ifdef JSON_OUTPUT
        {"json",                required_argument,  NULL,         longopt_json_filename},
#endif
        {"ignore-unknown-scaling-governor", no_argument, NULL,    longopt_ignore_unknown_scaling_governor},
        {"suppress-cpu-frequency-warnings", no_argument, NULL,    longopt_suppress_cpu_frequency_warnings},
        {"hwtimer-frequency",   required_argument,  NULL,         longopt_hwtimer_frequency},
        {"estimate-hwtimer-frequency", required_argument, NULL,   longopt_estimate_hwtimer_freq_cpu},
#ifdef __aarch64__
        {"disable-outline-atomics-lse", no_argument,NULL,         longopt_disable_outline_atomics_lse},
#endif
        {"tag",                 required_argument,  NULL,         longopt_tag},
        {"help",                no_argument,        NULL,         'h'},
        {"verbose",             no_argument,        NULL,         longopt_verbose},
        {"more-verbose",        no_argument,        NULL,         longopt_more_verbose},
        {"blackhole-numtries",  required_argument,  NULL,         longopt_blackhole_numtries},  // undocumented
        {0,                     0,                  NULL,         0}
    };


    char * this_arg = NULL; // point to the current string to be processed by getopt_long()
    opterr = 0;

    while (1) {
        this_arg = argv[optind];
        // printf("before getopt_long, argv[0] = %s, this_arg = argv[optind] = %s\n", argv[0], this_arg);
        int opt = getopt_long(argc, argv, ":t:a:c:p:o:S:C:I:O:M:hn:D:T:vYZ", long_options, NULL);
        long optval;
        char * endptr = NULL;

        // printf("after: opt = %d (%c), opterr = %d, this_arg = %s, next argv[optind] = %s\n", opt, opt, opterr, this_arg, argv[optind]);

        if (opt == -1) {
            // end of parsing
            // printf("end of parsing on %s\n", this_arg);
            break;
        }

        // FIXME: sanity check the values parsed by strtoul() and strtod(); examine endptr

#define REALLOCARRAY(dest) \
    if (!(pargs->dest = reallocarray(pargs->dest, pargs->num_##dest + 1, sizeof(pargs->dest[0])))) { \
        fprintf(stderr, "ERROR: can't reallocate pargs->" stringify(dest) "on line " stringify(__LINE__) "\n"); exit(-1); }

        switch (opt) {
          case longopt_timeout_usecs:
            pargs->timeout_usec = strtoul(optarg, NULL, 0);
            break;
          case 'a': // num_acquires
            optval = strtoul(optarg, (char **) &endptr, 10);
            if (!(*optarg != '\0' && *endptr == '\0')) {
                fprintf(stderr, "ERROR: -a / --num-acquires argument '%s' is invalid\n", optarg);
                return -1;
            }
            pargs->num_acquires = optval;
            break;
          case longopt_parallel_nanoseconds:
          case longopt_parallel_instructions:
            REALLOCARRAY(pars);
            pargs->pars[pargs->num_pars].t = strtoul(optarg, (char **) NULL, 10);
            pargs->pars[pargs->num_pars].unit = (opt == longopt_parallel_instructions) ? INSTS : NS;
            pargs->num_pars++;
            break;
          case longopt_critical_nanoseconds:
          case longopt_critical_instructions:
            REALLOCARRAY(crits);
            pargs->crits[pargs->num_crits].t = strtoul(optarg, (char **) NULL, 10);
            pargs->crits[pargs->num_crits].unit = (opt == longopt_critical_instructions) ? INSTS : NS;
            pargs->num_crits++;
            break;
          case 'c': // .hold, hold_unit ns or insts
            optval = strtoul(optarg, &endptr, 10);

            if (duration_parse_error(endptr, optarg)) {
                fprintf(stderr, "ERROR: could not parse critical duration \"%s\" correctly\n", optarg);
                return -1;
            }

            REALLOCARRAY(crits);
            pargs->crits[pargs->num_crits].t = optval;
            pargs->crits[pargs->num_crits].unit = equals_ns(endptr) ? NS : equals_in(endptr) ? INSTS : INSTS;
            pargs->num_crits++;
            break;
          case 'p': // .post, .post_unit ns or insts
            optval = strtoul(optarg, &endptr, 10);

            if (duration_parse_error(endptr, optarg)) {
                fprintf(stderr, "ERROR: could not parse parallel duration \"%s\" correctly\n", optarg);
                return -1;
            }

            REALLOCARRAY(pars);
            pargs->pars[pargs->num_pars].t = optval;
            pargs->pars[pargs->num_pars].unit = equals_ns(endptr) ? NS : equals_in(endptr) ? INSTS : INSTS;
            pargs->num_pars++;
            break;
          case 'n': // --iterations
            pargs->iterations = strtoul(optarg, (char **) NULL, 10);
            break;
          case 't': // -t number_of_threads[:interleaving]
          case 'o': // -o pinorder - cpulist on which to run
            {
                // instead of iterating over cpu_set_t bitmaks,
                // store in a temp array to preserve the order from -o pinorder

                char * this_pinorder = NULL;
                int * p = NULL;
                size_t num_cpus_specified = 0;
                unsigned long ileave = 0;
                int cpus_specified[psysinfo->num_cores]; // the index into this is the thread number, so the maximum number of elements is the maximum number of cores in the system.

                if (opt == 'o') {

                    cpu_set_t pinorder_specified_cores; // track if this -o pinorder has duplicate cpu
                    CPU_ZERO(&pinorder_specified_cores);

                    const char * pinorder_delim = ",";  // for tokenizing CPUs and CPU ranges from optarg
                    this_pinorder = strdup(optarg);     // this is a copy of the cpu,cpu,cpu... argument

                    char * csv;
                    while ((csv = strsep(&optarg, pinorder_delim))) {

                        char * this_csv = strdup(csv);  // a copy of the CPU range (cpu, cpu-cpu, cpu-cpu:skip) currently being parsed
                        long cpu_skip = 1;
                        unsigned long cpu_range[3];

                        size_t num_count = parse_cpulist_range(csv, cpu_range);
#if 0
                        for (size_t i = 0; i < num_count; i++) {
                            printf("cpu_range[%zu] = %lu\n", i, cpu_range[i]);;
                        }
#endif

                        if (num_count == 0) {
                            fprintf(stderr, "ERROR: could not parse the -o pinorder CPU range %s as m[-n[:s]], or one of them is not a non-negative number or ends with an unexpected character.\n", this_csv);
                            exit(-1);
                        }

                        if (num_count == 1) {
                            cpu_range[1] = cpu_range[0];
                        }

                        if (num_count == 3) {
                            cpu_skip = (long) cpu_range[2];
                        }

                        if (cpu_range[1] >= psysinfo->num_cores) {
                            fprintf(stderr, "ERROR: in pinorder CPU range %s, the specified CPU %lu is higher than the number of logical CPUs.\n", this_csv, cpu_range[1]);
                            exit(-1);
                        }

                        if (cpu_skip < 1) {
                            fprintf(stderr, "ERROR: in pinorder CPU range %s, the CPU interleave skip is less than 1.\n", this_csv);
                            exit(-1);
                        }

                        free(this_csv);

#if 0
                        // not sure if this check is really necessary, but it helped to detect misparsing cpulist ranges
                        size_t cpu_range_count = 1 + labs(cpu_range[1] - cpu_range[0]) / cpu_skip;
                        if (num_cpus_specified + cpu_range_count > psysinfo->num_cores) {
                            fprintf(stderr, "ERROR: pinorder %s specifies more CPUs than the number of CPUs configured in the system.  (cpu_range_count = %zu)\n", this_pinorder, cpu_range_count);
                            exit(-1);
                        }
#endif

                        if (cpu_range[1] >= cpu_range[0]) {
                            // counting upwards
                            for (int cpu = cpu_range[0]; cpu <= cpu_range[1]; cpu += cpu_skip) {
                                if (CPU_ISSET(cpu, &pinorder_specified_cores)) {
                                    fprintf(stderr, "ERROR: CPU number %d was previously specified in --pinning-order/-o pinorder list.  It must be specified only once.\n", cpu);
                                    exit(-1);
                                }

                                CPU_SET(cpu, &pinorder_specified_cores);
                                cpus_specified[num_cpus_specified++] = cpu;
                            }
                        } else {
                            // counting downwards
                            for (int cpu = cpu_range[0]; cpu >= cpu_range[1]; cpu -= cpu_skip) {
                                if (CPU_ISSET(cpu, &pinorder_specified_cores)) {
                                    fprintf(stderr, "ERROR: CPU number %d was previously specified in --pinning-order/-o pinorder list.  It must be specified only once.\n", cpu);
                                    exit(-1);
                                }

                                CPU_SET(cpu, &pinorder_specified_cores);
                                cpus_specified[num_cpus_specified++] = cpu;
                            }
                        }

                    }

                    // NOTE: if the CPU number is not online, sched_affinity will fail later
                    size_t pinorder_cpu_count = CPU_COUNT(&pinorder_specified_cores);
                    if (pinorder_cpu_count > psysinfo->num_cores) {
                        fprintf(stderr, "ERROR: the pinorder %s specifies %zu CPUs, but only %zu CPUs are configured.\n",
                            this_pinorder, pinorder_cpu_count, psysinfo->num_cores);
                        exit(-1);
                    }

                    p = malloc(sizeof(int) * num_cpus_specified);

                    if (!p) {
                        fprintf(stderr, "ERROR: cannot allocate enough memory for pinorder structure.\n");
                        return 1;
                    }

                    for (size_t i = 0; i < num_cpus_specified; i++) {
                        p[i] = cpus_specified[i];
                    }

#if 0               // shouldn't use pargs->verbose here because we might not yet have parsed -v
                    printf("INFO: pinorder %s uses %zu CPUs\n", this_pinorder, num_cpus_specified);
#endif
                } else {

                    // -t num_threads[:interleave]
                    size_t pinorder_string_buf_len = strlen(optarg) + 2;
                    this_pinorder = malloc(pinorder_string_buf_len);
                    if (! this_pinorder) { fprintf(stderr, "ERROR: could not malloc the pinorder string for -t %s\n", optarg); exit(-1); }
                    this_pinorder[0] = 't';
                    this_pinorder[1] = '\0';
                    strcpy(this_pinorder + 1, optarg);  // this_pinorder = "t$(num_threads)"

                    long numbers[2];

                    size_t num_count = parse_num_threads(optarg, (unsigned long *) numbers);
                    if (num_count == 0) {
                        fprintf(stderr, "ERROR: could not parse -t %s as m[:i], or one of them is not a number or ends in an unexpected character\n", this_pinorder);
                        exit(-1);
                    }

#if 0
                    for (size_t i = 0; i < num_count; i++) {
                        printf("numbers[%zu] = %lu\n", i, numbers[i]);;
                    }
#endif

                    num_cpus_specified = numbers[0];
                    ileave = num_count == 1 ? 0 : numbers[1];

                    if ((long) ileave < 0) {
                        fprintf(stderr, "ERROR: in -t %s, interleave is negative, which is not supported\n", this_pinorder);
                        exit(-1);
                    }

                    if (num_cpus_specified > psysinfo->num_online_cores) {
                        // TODO:  move this kind of validation out of arg parsing and into later arg validation phase
                        fprintf(stderr, "ERROR: in -t %s, the thread count must not be more than the number of online cores, %zu.\n", this_pinorder, psysinfo->num_online_cores);
                        exit(-1);
                    }

                    // for -t num_threads, pinorders[].cpu_list = NULL to indicate allocation in main()

#if 0               // shouldn't use pargs->verbose here because we might not yet have parsed -v
                    printf("INFO: -t %s uses %zu CPUs with interleave = %zu\n",
                            this_pinorder, num_cpus_specified, ileave);
#endif
                }

                REALLOCARRAY(pinorders);
                pargs->pinorders[pargs->num_pinorders].pinorder_string = this_pinorder;    // this is the strdup'd optarg
                pargs->pinorders[pargs->num_pinorders].cpu_list = p;        // for -t, this is NULL; for -o, this is the malloc'd cpu list
                pargs->pinorders[pargs->num_pinorders].num_threads = num_cpus_specified;
                pargs->pinorders[pargs->num_pinorders].ileave = ileave;
                pargs->num_pinorders++;
            }
            break;
          case 'S':
            pargs->scheduling_policy = parse_scheduling_policy(optarg);
            break;
          case 'D':
          case longopt_run_limit_seconds:
            pargs->run_limit_seconds = strtod(optarg, NULL);
            break;
          case 'O':
            pargs->run_limit_ticks = strtoul(optarg, NULL, 0);
            break;
          case 'I':
            pargs->run_limit_inner_loop_iters = strtoul(optarg, NULL, 0);
            //printf("run_limit_inner_loop_iters = %lu\n", pargs->run_limit_inner_loop_iters);
            break;
          case 'M':     // -M has an op
            pargs->use_mmap = 1;
            //printf("optarg = %s, argv[optind] = %s\n", optarg, argv[optind]);
            if (optarg) {   // e.g. -Mhelp
                pargs->hugepagesz = parse_hugepage_parameter(optarg);
            } else if (argv[optind]) {
                if (argv[optind][0] == '-') {   // e.g. -M -nextflag => default
                                                // argv[optind] is next flag
                    pargs->hugepagesz = HUGEPAGES_DEFAULT;
                } else if (argv[optind][0] != '\0') { // e.g. -M 2m => 2M
                    pargs->hugepagesz = parse_hugepage_parameter(argv[optind]);
                    optind++;
                }
            }
            //printf("using mmap with hugepagesz = %s\n", hugepage_map(pargs->hugepagesz));
            break;
          case longopt_hugepage_offset:
            // XXX: this offset is used for all of the locks for all of the hugepages, but this may not really be needed nor desired.
            pargs->mmap_hugepage_offset_exists = 1;
            pargs->mmap_hugepage_offset = strtoul(optarg, NULL, 0);
            break;
          case longopt_hugepage_physaddr:
            // XXX: assume that physaddr 0 will never be the physical address of a hugepage
            pargs->mmap_hugepage_physaddr = strtoul(optarg, NULL, 0);
            break;
          case longopt_print_hugepage_physaddr:
            pargs->print_hugepage_physaddr = 1;
            break;
          case longopt_hwtimer_frequency:
            pargs->hwtimer_frequency = strtoul(optarg, NULL, 0);
            break;
#ifdef JSON_OUTPUT
          case longopt_json_filename:
            pargs->json_output_filename = optarg;
            break;
#endif
          case 'Y':
          case longopt_ignore_unknown_scaling_governor:
            pargs->ignore_unknown_scaling_governor = 1;
            break;
          case 'Z':
          case longopt_suppress_cpu_frequency_warnings:
            pargs->suppress_cpu_frequency_warnings = 1;
            break;
          case longopt_estimate_hwtimer_freq_cpu:
            pargs->estimate_hwtimer_freq_cpu = optarg ? strtoul(optarg, NULL, 0) : 0;
            break;
          case 'C':
          case longopt_cpuorder_filename:
            pargs->cpuorder_filename = optarg;
            break;
          case longopt_more_verbose:
            pargs->verbose = VERBOSE_MORE;
            break;
          case longopt_blackhole_numtries:  // undocumented
            pargs->blackhole_numtries = strtoul(optarg, NULL, 0);
            break;
#ifdef __aarch64__
          case longopt_disable_outline_atomics_lse:
            pargs->disable_outline_atomics_lse = 1;
            break;
#endif
          case 'T':
          case longopt_tag:
            pargs->tag = optarg;
            break;
          case 'v':
          case longopt_verbose:
            if (pargs->verbose >= VERBOSE_YES) {
                pargs->verbose = VERBOSE_MORE;
            } else {
                pargs->verbose = VERBOSE_YES;
            }
            break;
          case '?':
          case ':':
            if (opt == '?')
                printf("Option flag %s is unknown.\n\n", argv[optind-1]);
            else if (opt == ':')
                printf("Option flag %s is missing an argument.\n\n", argv[optind-1]);
            printf("Use -h to print usage flags.\n");
            exit(-1);
          case 'h':
            new_print_usage(argv[0]);
            exit(-1);
        }
    }

    if (argc > optind && this_arg) {    // XXX: for --, optind is the first unknown arg or the first test argument (to the right of the --)
        if (0 != strcmp(this_arg, "--")) {
            fprintf(stderr, "ERROR: (main parser) unknown argument %s, opterr=%d\n", argv[optind], opterr);
            exit(-1);
        }

        printf("INFO: There are test-specific args after the -- that will be processed later.\n");
    }

    return 0;
}


static void print_pinorder(const pinorder_t * p) {
    if (p->cpu_list == NULL) {
        printf("cpu_list not yet calculated\n");
        return;
    }
    size_t num_threads = p->num_threads;
    for (size_t i = 0; i < num_threads; i++) {
        if (i) { printf(", "); }
        printf("[%zu] = %d", i, p->cpu_list[i]);
    }
    printf("\n");
}


static size_t parse_helper(char * s, unsigned long * p_numbers, const char ** delimits, const size_t nums_to_parse_max) {

    char * token, * endptr;

//    printf("parse_helper got nums_to_parse_max = %zu\n", nums_to_parse_max);

    for (size_t j = 0; j < nums_to_parse_max; j++) {

        const char * delim = delimits[j];
        int not_last_iter = j < (nums_to_parse_max - 1);

//        printf("s = %p, s = %s\n", s, s);

        token = not_last_iter ? strsep(&s, delim) : s;

        p_numbers[j] = strtoul(token, &endptr, 0);

        if (endptr == token) {
            // ERROR: strtoul found no digits
            //printf("strotul found no digits in token=%s\n", token);
            return 0;
        }

        if (endptr && *endptr != '\0') {
            // strtoul ended on a non-NULL character
            if (! not_last_iter) {
                // ERROR: strotul found a non-NULL character on the last possible position
                return 0;
            }

            if (! strchr(delim, *endptr)) {
                // ERROR: strtoul did not end on an expected delimiter character when not in the last possible position
                return 0;
            }
        }

        // only m was found
        if (not_last_iter && s == NULL) {
            return j + 1;
        }

    }

    return nums_to_parse_max;
}


size_t parse_num_threads(char * s, unsigned long * p_numbers) {

    // -t num_threads[:interleave]  -> -t m     -t m:n

    const char * num_threads_delimits[] = {
        ":",    // -t m[:n]
    };

    const size_t num_num_threads_delimits = sizeof(num_threads_delimits) / sizeof(num_threads_delimits[0]);
    const size_t max_num_params = num_num_threads_delimits + 1;

    return parse_helper(s, p_numbers, num_threads_delimits, max_num_params);
}


size_t parse_cpulist_range(char * s, unsigned long * p_numbers) {

    // -o m-n[:skip]    --> -o m        -o m-n      -o m-n:skip

    const char * cpu_range_delimits[] = {
        "-",    // -t m-n
        ":",    // -t m-n:s
    };
    const size_t num_cpu_range_delimits = sizeof(cpu_range_delimits) / sizeof(cpu_range_delimits[0]);
    const size_t max_num_params = num_cpu_range_delimits + 1;

    return parse_helper(s, p_numbers, cpu_range_delimits, max_num_params);
}


void print_test_args(const test_args_t * p) {
    printf("test_args:\n");
    printf("num_acquires = %lu\n", p->num_acquires);  // -a    number of acquires (not documented?)

    printf("crits =");
    for (size_t i = 0; i < p->num_crits; i++) {
        printf(" %lu%s%c", p->crits[i].t, p->crits[i].unit == NS ? "ns" : "inst", (i == p->num_crits - 1) ? '\n' : ',');
    }

    printf("pars =");
    for (size_t i = 0; i < p->num_pars; i++) {
        printf(" %lu%s%c", p->pars[i].t, p->pars[i].unit == NS ? "ns" : "inst", (i == p->num_pars - 1) ? '\n' : ',');
    }
    printf("scheduling_policy = %d (%s)\n",
            scheduling_policy_map[p->scheduling_policy].value,
            scheduling_policy_map[p->scheduling_policy].name);

    printf("tag = %s\n", p->tag);
    printf("cpuorder_filename = %s\n", p->cpuorder_filename);
    printf("num_pinorders = %zu\n", p->num_pinorders);
    for (size_t i = 0; i < p->num_pinorders; i++) {
        printf("pinorder %zu num_threads=%zu, ileave=%zu, pinorder_string=%s: ",
                i, p->pinorders[i].num_threads, p->pinorders[i].ileave, p->pinorders[i].pinorder_string);
        print_pinorder(&(p->pinorders[i]));
    }
    printf("timeout_usec = %lu\n", p-> timeout_usec);
    printf("hugepagesz = %d\n", p->hugepagesz);
    printf("use_mmap = %d\n", p->use_mmap);
    printf("mmap_hugepage_offset_exists = %d\n", p->mmap_hugepage_offset_exists);
    printf("mmap_hugepage_offset = %zu\n", p->mmap_hugepage_offset);
    printf("mmap_hugepage_physaddr = %p\n", (void *) p->mmap_hugepage_physaddr);
    printf("hwtimer_frequency = %lu\n", p->hwtimer_frequency);
    printf("estimate_hwtimer_freq_cpu = %ld\n", p->estimate_hwtimer_freq_cpu);

    printf("run_limit_seconds = %f\n", p->run_limit_seconds);
    printf("run_limit_ticks = %lu\n", p->run_limit_ticks);
    printf("run_limit_inner_loop_iters = %lu\n", p->run_limit_inner_loop_iters);
#ifdef JSON_OUTPUT
    printf("json_output_filename = %s\n", p->json_output_filename);
#endif
    printf("ignore_unknown_scaling_governor = %d\n", p->ignore_unknown_scaling_governor);
    printf("suppress_cpu_frequency_warnings = %d\n", p->suppress_cpu_frequency_warnings);
    printf("verbose = %d\n", p->verbose);
    printf("blackhole_numtries = %lu\n", p->blackhole_numtries);
    printf("iterations = %zu\n", p->iterations);
    printf("\n");
}

/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
