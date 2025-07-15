/*
    Copyright (c) 2005-2018 Intel Corporation

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.




*/

/*
 *  Based on:
 *
 *      Project: github.com/01org/tbb, File: tbb/include/tbb/spin_rw_mutex.h
 *      Tag: 2018_U3-0-g633b01a
 *
 *  Description:
 *
 *      This file implements 'Fast, unfair, spinning reader-writer lock with
 *      back-off and writer-preference'. The algorithm is based on
 *      'spin_rw_mutex' from Intel TBB library.
 *
 *  Internals:
 *
 *      - Cutting through layers of abstractions in the original source code, I
 *      made things not as clean as it was. However, during the porting
 *      process, I tried to keep things as similar as possible to the setup in
 *      the Intel TBB library. I ported only required things for this
 *      synchronization scheme to work.
 *
 *      - The ext/tbb/include/tbb.h file tries to provide similar __TBB level
 *      abstractions as tbb/include/tbb/tbb_machine.h but it is limited to only
 *      the definitions needed for this particular test.  The OS is Linux
 *      (for sched_yield). The x86-64 (no TSX) primitives are from
 *      tbb/include/tbb/machine/linux_intel64.h.
 *
 *      - In ext/tbb/include/tbb.h are several macros for the variants of
 *      atomics to use.  These are coordinated with the lockhammer Makefile
 *      build-time defines by variant.
 *
 *  Changes from TBB:
 *
 *      - One main change is in the definition of 'machine_pause()'. Here, it
 *      would first spin and then sched_yield() unlike the default in TBB where
 *      it would sched_yield() immediately (at least for Aarch64).
 *
 *      - Does not implement upgrade() or downgrade() methods
 *
 *      - Not using C++ because it is difficult given this benchmark framework
 *      as well as the other complexities which comes from pulling out a set of
 *      classes from a class tree in tbb.
 *
 *  Workings:
 *
 *      This implements classical reader-writer lock. Which means a lock can be
 *      held by a single writer or a group of readers at the same time but not
 *      both.
 *
 *      From tbb docs: " Mutual exclusion is necessary when at least one thread
 *      writes to a shared variable. But it does no harm to permit multiple
 *      readers into a protected region. The reader-writer variants of the
 *      mutexes [...] enable multiple readers by distinguishing reader locks
 *      from writer locks. There can be more than one reader lock on a given
 *      mutex."
 *
 *      When a writer first tries to acquire the lock, if there are no readers
 *      already holding the lock, it will acquire it else in the presence of
 *      readers it will set a writer pending bit if not set. If this bit is
 *      already set or after setting the bit the writer will start backing off
 *      eventually yielding the CPU until obtaining the lock.
 *
 *      In case of readers, more than one of them can go in the exclusive
 *      section simultaneously. If no writer is holding the lock or no pending
 *      writers, a reader even in presence of other reader can acquire the lock.
 *      It will back off and eventually yield the CPU when writer is holding a
 *      lock until the lock becomes available again.
 *
 *  Readers/Writers ratio (-r) and Pure readers (-o):
 *
 *      - 'rw_mask' variable defines the ratio between readers and writers per
 *      thread. It is controlled using log2_ratio variable, cmdline args -r.
 *
 *      - Given the ratio, a thread will perform that many 'read_acquire' and
 *      'read_release' calls and then it will do one 'write_acquire' and
 *      'write_release'. And then if more work to be done, repeat.
 *
 *      For a thread:
 *
 *      num readers
 *      ----------- = 2^(log2_ratio) - 1;
 *      num writers
 *
 *       > log2_ratio of  0 means all writers
 *       > log2_ratio of ~0 means all readers
 *       > default log2_ratio is 6 e.g 63 reads per write.
 *
 *      - Pure readers are CPUs which will never perform a write acq/rel. The
 *        cmdline arg is a list of CPUs that will be a pure reader if the CPU
 *        is part of the measurement's pinorder.  Default is no pure readers.
 *
 */

#ifndef __TBB_spin_mutex_H
#define __TBB_spin_mutex_H

#ifdef initialize_lock
#undef initialize_lock
#endif

#ifdef parse_test_args
#undef parse_test_args
#endif

#define initialize_lock(lock, cpulist, threads) tbb_init_locks(lock, cpulist, threads)
#define parse_test_args(args, argc, argv) tbb_parse_args(args, argc, argv)

#include "tbb.h"

#define WRITER          1
#define WRITER_PENDING  2
#define READERS         ~(WRITER | WRITER_PENDING)
#define ONE_READER      4
#define BUSY            (WRITER | READERS)

unsigned long log2_ratio = 0;
unsigned long rw_mask = 0;

cpu_set_t pure_reader_cpus;

struct tbb_spin_rw_mutex_parameters_t {
    unsigned long verbose;
} tbb_spin_rw_mutex_parameters = {
    .verbose = VERBOSE_NONE,
};


typedef struct {
    unsigned long c;
    uint8_t pure_reader;
} __attribute__((aligned(64))) rw_count_t;

rw_count_t *rw_counts;

inline uint8_t is_writer(unsigned long i, uint8_t val) {
    if (rw_counts[i].pure_reader)
        return 0;
    rw_counts[i].c += val;
    return !(rw_counts[i].c & rw_mask);
}

void tbb_print_usage() {
    fprintf(stderr, "tbb_spin_rw_mutex additional options:\n");
    fprintf(stderr, "\t -h print this msg\n");
    fprintf(stderr, "\t -r reader/writer log ratio, default: 6 (2^(6)-1 readers per writer)\n");
    fprintf(stderr, "\t -o cpu[,cpu...] pure reader cpu list\n");
}

void tbb_check_strtoul(int rval, char* endptr) {
    if ((errno == ERANGE && (rval == ULONG_MAX))
            || (errno != 0 && rval == 0) || endptr == optarg) {
        fprintf(stderr, "tbb_spin_rw_mutex: value unsuitable for 'unsigned long'\n\n");
        tbb_print_usage();
        exit(1);
    }
}

void tbb_parse_args(test_args_t * t, int argc, char** argv) {
    int i = 0;
    char *endptr;

    tbb_spin_rw_mutex_parameters.verbose = t->verbose;

    log2_ratio = 6;

    while ((i = getopt(argc, argv, "hr:o:")) != -1)
    {
        switch (i) {
          case 'r':
            errno = 0;
            log2_ratio = strtoul(optarg, &endptr, 10);
            tbb_check_strtoul(log2_ratio, endptr);
            if (log2_ratio >= 64) {
                fprintf(stderr, "tbb_spin_rw_mutex: -r can not be >= 64\n");
                exit(1);
            }
            break;
          case 'o':
            {
                const char * pinorder_delim = ",";
                CPU_ZERO(&pure_reader_cpus);

                char * csv = strtok(optarg, pinorder_delim);
                while (csv) {
                    int cpu = strtol(csv, (char **) NULL, 0);
                    CPU_SET(cpu, &pure_reader_cpus);
                    csv = strtok(NULL, pinorder_delim);
                    if (tbb_spin_rw_mutex_parameters.verbose >= VERBOSE_YES) {
                        printf("tbb_spin_rw_mutex: cpu %d is a pure reader\n", cpu);
                    }
                }
            }
            break;
          case 'h':
            tbb_print_usage();
            exit(0);
          case '?':
          default:
            tbb_print_usage();
            exit(3);
        }
    }
}

void tbb_init_locks (unsigned long *lock, int * cpulist, unsigned long threads) {
    rw_mask = ((1UL<<log2_ratio)-1);
    if (rw_counts) { free(rw_counts); }
    rw_counts = (rw_count_t*) malloc(threads * sizeof(rw_count_t));

    DBG("On each thread, for every %lu readers there will be 1 writer\n", rw_mask);

    for (unsigned long i = 0; i < threads; ++i) {
        rw_counts[i].pure_reader = CPU_ISSET(cpulist[i], &pure_reader_cpus) ? 1 : 0;
        rw_counts[i].c = 0;
        DBG("\t CPU[%u] is a pure reader %u\n", cpulist[i], rw_counts[i].pure_reader);
    }
}

//! State of lock
/** Bit 0 = writer is holding lock
    Bit 1 = request by a writer to acquire lock (hint to readers to wait)
    Bit 2..N = number of readers holding lock */
typedef intptr_t state_t;

static inline state_t CAS(state_t *s, state_t new_val, state_t old_val) {
   return (state_t)__TBB_CompareAndSwapW(s, new_val, old_val);
}

static inline void internal_acquire_writer(state_t * pstate, unsigned long t) {
    int32_t count;
    DBG("init [%ld]: 0x%lx\n", t, *pstate);
    for(count = 1;;atomic_backoff__pause(&count)) {
        state_t s = *(volatile state_t *) pstate;
        if( !(s & BUSY) ) { // no readers, no writers
            if( CAS(pstate, WRITER, s)==s ) {
                break; // successfully stored writer flag
            }
            count = 1; // we could be very close to complete op.
        } else if( !(s & WRITER_PENDING) ) { // no pending writers
            __TBB_AtomicOR(pstate, WRITER_PENDING);
        }
    }
    DBG("final [%ld]: 0x%lx\n", t, *pstate);
}

static void internal_release_writer(state_t * pstate, unsigned long t) {
    DBG("init [%ld]: 0x%lx\n", t, *pstate);
    __TBB_AtomicAND( pstate, READERS );
    DBG("final [%ld]: 0x%lx\n", t, *pstate);
}

static inline void internal_acquire_reader(state_t * pstate, unsigned long t) {
    int32_t count;
    DBG("init [%ld]: 0x%lx\n", t, *pstate);
    for(count = 1;;atomic_backoff__pause(&count)) {
        state_t s = *(volatile state_t *) pstate; // ensure reloading
        if( !(s & (WRITER|WRITER_PENDING)) ) { // no writer or write requests
            state_t t = \
                (state_t)__TBB_FetchAndAddW( pstate, (state_t) ONE_READER );
            if( !( t&WRITER ))
                break; // successfully stored increased number of readers
            // writer got there first, undo the increment
            __TBB_FetchAndAddW( pstate, -(state_t)ONE_READER );
        }
    }
    __TBB_ASSERT( *pstate & READERS, "invalid state of a read lock: no readers" );
    DBG("final [%ld]: 0x%lx\n", t, *pstate);
}

static void internal_release_reader(state_t * pstate, unsigned long t) {
    DBG("init [%ld]: 0x%lx\n", t, *pstate);
    __TBB_FetchAndAddWrelease(pstate, -(state_t)ONE_READER);
    DBG("final [%ld]: 0x%lx\n", t, *pstate);
}

static inline unsigned long
lock_acquire (unsigned long *lock, unsigned long threadnum) {
    (is_writer(threadnum,1))
        ? internal_acquire_writer((state_t *) lock, threadnum)
        : internal_acquire_reader((state_t *) lock, threadnum);
    /* average depth will always = 1 */
    return 1;
}

static inline void
lock_release (unsigned long *lock, unsigned long threadnum) {
    (is_writer(threadnum,0))
        ? internal_release_writer((state_t *) lock, threadnum)
        : internal_release_reader((state_t *) lock, threadnum);
    return;
}
#endif /* __TBB_spin_mutex_H */

/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
