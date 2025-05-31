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
 * Functions to read hardware timers and query timer frequency.
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

#include "atomics.h"

extern __thread uint64_t prev_tsc;

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

/* Cautionary note about using the invariant TSC on x86:
   Depending upon the model of CPU, TSC may
   not count cycles representing the current
   operating frequency.  It may, for example,
   count cycles at the maximum frequency of the
   device, even if the CPU core is running at a
   lower frequency, or it may count at a frequency
   unrelated to the operating frequency.  Use
   the --estimate-hwtimer-frequency flag to measure
   the frequency and the --hwtimer-frequency flag to
   override the value detected by the code below.
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
             "=r" (tsc.lo_32)
             ::"eax", "ebx", "ecx", "edx");

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
             "=r" (tsc.lo_32)
             ::"eax", "ebx", "ecx", "edx");

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
    asm volatile ("ISB; mrs %0, cntvct_el0" : "=r" (t));
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
    /* this returns the counter value from a constant-rate timer */
#ifdef __aarch64__
        uint64_t counter_value;
        __asm__ __volatile__ ("isb; mrs %0, cntvct_el0" : "=r" (counter_value));
#elif __x86_64__
    uint64_t counter_value = rdtscp();    // assume constant_tsc
#endif
    return counter_value;
}

/* Timer read for when at start of timing block
 */
static inline uint64_t __attribute__((always_inline))
timer_get_counter_start()
{
    /* this returns the counter value from a constant-rate timer */
#ifdef __aarch64__
        uint64_t counter_value;
        __asm__ __volatile__ ("dsb ish; isb; mrs %0, cntvct_el0" : "=r" (counter_value));
#elif __x86_64__
    uint64_t counter_value = rdtscp_start();    // assume constant_tsc
#endif
    return counter_value;
}


/* Timer read for when at end of timing block
 */
static inline uint64_t __attribute__((always_inline))
timer_get_counter_end()
{
    /* this returns the counter value from a constant-rate timer  */
#ifdef __aarch64__
        uint64_t counter_value;
        __asm__ __volatile__ ("isb; mrs %0, cntvct_el0; isb" : "=r" (counter_value));
#elif __x86_64__
    uint64_t counter_value = rdtscp_end();    // assume constant_tsc
#endif
    return counter_value;
}

static inline void __attribute__((always_inline))
timer_reset_all()
{
    timer_reset_counter();
}

static inline void __attribute__((always_inline))
timer_init() {
}

static inline uint64_t __attribute__((always_inline))
timer_get_timer_freq(void)
{
    extern unsigned long hwtimer_frequency;
    if (hwtimer_frequency) { return hwtimer_frequency; }

    uint64_t cnt_freq;
#ifdef __aarch64__
        __asm__ __volatile__ ("isb; mrs %0, cntfrq_el0" : "=r" (cnt_freq));
#elif __x86_64__
    // This code attempts to get the TSC frequency.  The assumption made
    // is TSC frequency equals the CPUFreq cpuinfo_max_freq attribute
    // value, which is the maximum operating frequency of the processor.
    // However, this equality is not always true, and less so in newer CPUs.
    // Also, the actual TSC frequency may not exactly match any nominal
    // frequency attribute value provided by CPUFreq, so the chances of
    // this returning the correct frequency have diminished.

    // If the CPUFreq cpuinfo_max_freq attribute is not available, this code
    // then tries to quickly measure it.

    // Use --timer-frequency flag to override the frequency value.
    // Use --estimate-timer-frequency to explicitly measure it.

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
        unsigned long len = log10(tmp);
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

#define TOKENS_MAX_HIGH    1000000        /* good for ~41500 cntvct cycles */
#define THRESHOLD    1.05            // if the ratio of cycles to do the total eval loop  to  the sum of the individual
                                     // calls (e.g. due to context switch), rerun


#endif

/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
