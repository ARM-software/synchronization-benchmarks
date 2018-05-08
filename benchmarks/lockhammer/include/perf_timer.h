/*
 * Copyright (c) 2018, ARM Limited. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * Neither the name of ARM Limited nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Rob Golshan,
 *          James Yang (James.Yang@arm.com),
 *          Geoffrey Blake (Geoffrey.Blake@arm.com)
 */

/* 
 * perf_timer.h
 * Functions to read hardware timers, query timer frequency, and a
 * blackhole function that wastes cpu time (useful for nanosecond waits)
 * Supports x86 and AArch64 platforms
 *
 * Define DEBUG in makefile or here if you desire debug output,
 * define DDEBUG if you require detailed debug output.
 */

#ifndef __PERF_TIMER_H_
#define __PERF_TIMER_H_

#include <stdint.h>
#include <stdlib.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>    /* for access() */
#include <math.h>

extern __thread uint64_t prev_tsc;
extern __thread uint64_t cnt_freq;

/* Cautionary note about using the invariant TSC on x86:
   Depending upon the model of CPU, TSC may
   not count cycles representing the current
   operating frequency.  It may, for example,
   count cycles at the maximum frequency of the
   device, even if the CPU core is running at a
   lower frequency.
 */
#ifdef __x86_64__
static inline uint64_t __attribute__((always_inline))
rdtsc(void)
{
    union {
        uint64_t tsc_64;
        struct {
            uint32_t lo_32;
            uint32_t hi_32;
        };
    } tsc;

    asm volatile("rdtsc" :
             "=a" (tsc.lo_32),
             "=d" (tsc.hi_32));

    return tsc.tsc_64;
}

// rdtscp is serializing; rdtsc is not
// NOTE: rdtscp can not guarantee subsequent instructions do not begin execution
// before the timer is read
static inline uint64_t __attribute__((always_inline))
rdtscp(void)
{
    union {
        uint64_t tsc_64;
        struct {
            uint32_t lo_32;
            uint32_t hi_32;
        };
    } tsc;

    asm volatile("rdtscp" :
             "=a" (tsc.lo_32),
             "=d" (tsc.hi_32));

    return tsc.tsc_64;
}

static inline void __attribute__((always_inline))
cpuid(void)
{
    uint32_t a, b, c, d;
    asm volatile("CPUID":
            "=a" (a),
            "=b" (b),
            "=c" (c),
            "=d" (d));
}

/* CPUID creates a barrier to avoid out of order execution before rdtsc
 */
static inline uint64_t __attribute__((always_inline))
rdtscp_start(void)
{
    union {
        uint64_t tsc_64;
        struct {
            uint32_t lo_32;
            uint32_t hi_32;
        };
    } tsc;

    asm volatile("CPUID\n\t" /* serialize */
            "RDTSC\n\t" /*read clock */
            "mov %%edx, %0\n\t"
            "mov %%eax, %1\n\t":
             "=r" (tsc.hi_32),
             "=r" (tsc.lo_32)::"%rax", "%rbx", "%rcx", "%rdx");

    return tsc.tsc_64;
}

/* "RDTSCP instruction waits until all previous instructions have been executed
 * before reading the counter. However, subsequent instructions may begin execution
 * before the read operation is performed.”
 * CPUID creates a barrier to avoid out of order execution
 */
static inline uint64_t __attribute__((always_inline))
rdtscp_end(void)
{
    union {
        uint64_t tsc_64;
        struct {
            uint32_t lo_32;
            uint32_t hi_32;
        };
    } tsc;

    asm volatile("RDTSCP\n\t"
            "mov %%edx, %0\n\t"
            "mov %%eax, %1\n\t"
            "CPUID\n\t":
             "=r" (tsc.hi_32),
             "=r" (tsc.lo_32)::"%rax", "%rbx", "%rcx", "%rdx");

    return tsc.tsc_64;

}


static inline uint64_t __attribute__((always_inline))
get_raw_counter(void) {
    return rdtsc();
}
#endif


#ifdef __aarch64__
static inline uint64_t __attribute__((always_inline))
get_cntvct_el0(void) {
    uint64_t t;
    asm volatile ("mrs %0, cntvct_el0" : "=r" (t));
    return t;
}


static inline uint64_t __attribute__((always_inline))
get_raw_counter(void) {
    return get_cntvct_el0();
}
#endif


static inline void __attribute__((always_inline))
timer_reset_counter()
{
#ifdef __aarch64__
    __asm__ __volatile__ ("isb; mrs %0, cntvct_el0" : "=r" (prev_tsc));
#elif __x86_64__
    prev_tsc = rdtscp();
#endif
}


/* Standard timer read functions */
static inline uint64_t __attribute__((always_inline))
timer_get_counter()
{
    /* this returns the cycle counter from a constant-rate timer  */
#ifdef __aarch64__
        uint64_t timer;
        __asm__ __volatile__ ("isb; mrs %0, cntvct_el0" : "=r" (timer));
#elif __x86_64__
    uint64_t timer = rdtscp();    // assume constant_tsc
#endif
    return timer;
}

/* Timer read for when at start of timing block
 */
static inline uint64_t __attribute__((always_inline))
timer_get_counter_start()
{
    /* this returns the cycle counter from a constant-rate timer  */
#ifdef __aarch64__
        uint64_t timer;
        __asm__ __volatile__ ("dsb ish; isb; mrs %0, cntvct_el0" : "=r" (timer));
#elif __x86_64__
    uint64_t timer = rdtscp_start();    // assume constant_tsc
#endif
    return timer;
}


/* Timer read for when at end of timing block
 */
static inline uint64_t __attribute__((always_inline))
timer_get_counter_end()
{
    /* this returns the cycle counter from a constant-rate timer  */
#ifdef __aarch64__
        uint64_t timer;
        __asm__ __volatile__ ("isb; mrs %0, cntvct_el0; isb" : "=r" (timer));
#elif __x86_64__
    uint64_t timer = rdtscp_end();    // assume constant_tsc
#endif
    return timer;
}

static inline void __attribute__((always_inline))
timer_reset_all()
{
    timer_reset_counter();
}

static inline void __attribute__((always_inline))
timer_init() {
}

static inline uint32_t __attribute__((always_inline))
timer_get_cnt_freq(void) {
    uint32_t cnt_freq;
#ifdef __aarch64__
        __asm__ __volatile__ ("isb; mrs %0, cntfrq_el0" : "=r" (cnt_freq));
#elif __x86_64__
    char buf[100];
    FILE * f = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
    if (f == NULL) {
        printf("Failed to open cpuinfo_max_freq, error %s\n",
            strerror(errno));
        uint64_t iterations = 2;
        uint64_t time = 0;
        for (uint64_t i = 0; i < iterations; i++) {
            uint64_t start = rdtscp_start();
            sleep(1);
            uint64_t end = rdtscp_end();
            time += end - start;
        }

        // round down cycles
        uint64_t tmp = (time/iterations);
        unsigned int len = log10(tmp);
        double div = pow(10, len-2);
        return floor(tmp/div)*div;
    }
    while (! feof(f) && ! ferror(f)) {
        size_t end = fread(buf, 1, sizeof(buf) - 1, f);
        buf[end] = 0;
    }
    fclose(f);

    /* The ACPI cpufreq driver reports 'base' (aka non-turbo) frequency
       in cpuinfo_max_freq while the intel_pstate driver reports the
       turbo frequency. Warn if ACPI cpufreq is not found. */
    if (access("/sys/devices/system/cpu/cpufreq", F_OK)) {
        printf("cpuinfo_max_freq is not from ACPI cpufreq driver! TSC frequency is probably turbo frequency.\n");
    }

    cnt_freq = strtoul(buf, NULL, 0);
    cnt_freq = ((cnt_freq + 5000) / 10000) * 10000;    /* round to nearest 10000 kHz */
    cnt_freq *= 1000;    /* convert KHz to Hz */
#endif
    return cnt_freq;
}
#endif

#define TOKENS_MAX_HIGH    1000000        /* good for ~41500 cntvct cycles */
#define THRESHOLD    1.05            // if the ratio of cycles to do the total eval loop  to  the sum of the individual
                                     // calls (e.g. due to context switch), rerun

void __attribute__((noinline, optimize("no-unroll-loops"))) blackhole(unsigned long iters) {
    if (! iters) { return; }
#ifdef __aarch64__
    asm volatile (".p2align 4; 1: add %0, %0, -1; cbnz  %0, 1b" : "+r" (iters) : "0" (iters));
#elif __x86_64__
    asm volatile (".p2align 4; 1: add $-1, %0; jne 1b" : "+r" (iters) );
#endif
}


__thread unsigned long LOOP_TEST_OVERHEAD = 0;
unsigned long __attribute__((noinline, optimize("no-unroll-loops"))) evaluate_loop_overhead(const unsigned long NUMTRIES) {
    if (LOOP_TEST_OVERHEAD) {return LOOP_TEST_OVERHEAD;}
    unsigned long outer_cycles_start, outer_cycles_end;
    unsigned long i, j;
    unsigned long outer_elapsed_total = 0;

    // get cpu out of low power state or into high power state
    for (i = 0; i < 10000; i++){
        blackhole(100000);
    }
    for (j = 0; j < 1000; j++) {
        unsigned long elapsed_total = 0;
        outer_cycles_start = timer_get_counter_start();
        for (i = 0; i < NUMTRIES; i++) {

            unsigned long cycles_start, cycles_end;
            cycles_start = timer_get_counter_start();
            cycles_end = timer_get_counter_end();

            unsigned long elapsed  = cycles_end - cycles_start;
                    // printf("elapsed = %lu\n", elapsed);
            elapsed_total += elapsed;
        }
        outer_cycles_end = timer_get_counter_end();
        outer_elapsed_total = outer_cycles_end - outer_cycles_start;
        LOOP_TEST_OVERHEAD += (outer_elapsed_total - elapsed_total);
    }
    LOOP_TEST_OVERHEAD = LOOP_TEST_OVERHEAD/j;
    return LOOP_TEST_OVERHEAD;
}

__thread unsigned long TIMER_OVERHEAD = 0;
unsigned long evaluate_timer_overhead(void) {
    if (TIMER_OVERHEAD) {return TIMER_OVERHEAD;}
    unsigned long outer_cycles_start, outer_cycles_end;
    outer_cycles_start = timer_get_counter_start();
    outer_cycles_end = timer_get_counter_end();
    unsigned long elapsed  = outer_cycles_end - outer_cycles_start;
    TIMER_OVERHEAD = elapsed;
    return TIMER_OVERHEAD;
}

unsigned long __attribute__((noinline, optimize("no-unroll-loops"))) evaluate_blackhole(const unsigned long tokens_mid, const unsigned long NUMTRIES) {
    unsigned long i, j;
    unsigned long outer_cycles_start, outer_cycles_end;
    unsigned long sum_elapsed_total = 0;
    unsigned long avg_elapsed_total = 0;
    unsigned long outer_elapsed_total;
    unsigned long outer_inner_diff;
    double percent;

    unsigned long LOOP_TEST_OVERHEAD = evaluate_loop_overhead(NUMTRIES);
    unsigned long TIMER_OVERHEAD = evaluate_timer_overhead();

    for (j = 0; j < 5; j++) {

        unsigned long elapsed_total = 0;

        outer_cycles_start = timer_get_counter_start();
        for (i = 0; i < NUMTRIES; i++) {

            unsigned long cycles_start, cycles_end;
            cycles_start = timer_get_counter_start();
            blackhole(tokens_mid);
            cycles_end = timer_get_counter_end();

            unsigned long elapsed  = cycles_end - cycles_start;
                    // printf("elapsed = %lu\n", elapsed);

            elapsed_total += elapsed;
        }
        outer_cycles_end = timer_get_counter_end();

        outer_elapsed_total = outer_cycles_end - outer_cycles_start;
        outer_inner_diff = (outer_elapsed_total - elapsed_total);
        if (outer_inner_diff > LOOP_TEST_OVERHEAD) {
            percent = outer_inner_diff / (double) LOOP_TEST_OVERHEAD;
        } else {
            percent =  LOOP_TEST_OVERHEAD/ (double) outer_inner_diff;
        }

#ifdef DDEBUG
        printf("outer_elapsed_total = %lu "
               "elapsed_total = %lu "
               "outer_inner_diff = %lu percent = %f\n",
               outer_elapsed_total, elapsed_total, outer_inner_diff, percent);
#endif

        if (percent > THRESHOLD) {
            // try again because loop overhead was more than expected
            j--;
            continue;
        }

        if (sum_elapsed_total == 0) {
            // first time around
            sum_elapsed_total = elapsed_total - TIMER_OVERHEAD*NUMTRIES;
            continue;
        }

        avg_elapsed_total = sum_elapsed_total / j;

        long elapsed_total_diff = abs(avg_elapsed_total - elapsed_total);

        if (((double) elapsed_total_diff / avg_elapsed_total) > THRESHOLD) {
            // ignore outliers
            j--;
            continue;
        }

        sum_elapsed_total += elapsed_total - TIMER_OVERHEAD*NUMTRIES;
        avg_elapsed_total = sum_elapsed_total / (j + 1);

    }

    // returns average duration of NUMTRIES calls to blackhole with tokens_mid
    long result = avg_elapsed_total;
    return result;
}


unsigned long calibrate_blackhole(unsigned long target, unsigned long tokens_low, unsigned long tokens_high) {
    unsigned long tokens_diff = tokens_high - tokens_low;
    unsigned long tokens_mid = (tokens_diff / 2) + tokens_low;
    unsigned long NUMTRIES = 10;
    unsigned long target_elapsed_total = NUMTRIES * target;

#ifdef DDEBUG
    printf("target = %lu, target_elapsed_total = %lu, tokens_low = %lu, tokens_high = %lu, tokens_diff = %lu, tokens_mid = %lu\n",
            target, target_elapsed_total, tokens_low, tokens_high, tokens_diff, tokens_mid);
#endif

    if (tokens_diff == 1) {
        // the answer is either tokens_low or tokens_high

        unsigned long ret_low = evaluate_blackhole(tokens_low, NUMTRIES);
        unsigned long ret_high = evaluate_blackhole(tokens_high, NUMTRIES);

        long low_diff = abs(ret_low - target_elapsed_total);    
        long high_diff = abs(ret_high - target_elapsed_total);

        if (low_diff < high_diff) {
            if (tokens_low >= (TOKENS_MAX_HIGH-1)) {
                printf("tokens is TOKENS_MAX_HIGH or TOKENS_MAX_HIGH -1.  requested delay is too long or too short.\n");
            }

            return tokens_low;
        }

        if (tokens_high >= (TOKENS_MAX_HIGH-1)) {
            printf("tokens is TOKENS_MAX_HIGH or TOKENS_MAX_HIGH -1.  requested delay is too long or too short.\n");
        }

        return tokens_high;
    }


    unsigned long t = evaluate_blackhole(tokens_mid, NUMTRIES);

#ifdef DEBUG
    printf("t = %lu, target_elapsed_total = %lu\n", t, target_elapsed_total);
#endif

    if (t > target_elapsed_total) {
        tokens_mid = calibrate_blackhole(target, tokens_low, tokens_mid);
    } else if (t < target_elapsed_total) {
        tokens_mid = calibrate_blackhole(target, tokens_mid, tokens_high);
    }

    return tokens_mid;
}
