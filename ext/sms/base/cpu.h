// Copyright (c) 2017 ARM Limited. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#ifndef CACHE_LINE
// Default CPU cache line size
#define CACHE_LINE 128
#endif

static inline void doze(void)
{
#if defined(__ARM_ARCH)
    // YIELD hints the CPU to switch to another thread if available
    // but otherwise executes as a NOP
    // ISB flushes the pipeline, then restarts. This is guaranteed to stall
    // the CPU a number of cycles
    __asm__ volatile("isb" : : : "memory");
#elif defined(__x86_64__)
    __asm__ volatile("pause" : : : "memory");
#else
#error Please add support for your CPU in cpu.h
#endif
}

int num_cpus(void);

unsigned long cpu_hz(void);
