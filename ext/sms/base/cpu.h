// Copyright (c) 2017 ARM Limited. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#ifndef CACHE_LINE
// Default CPU cache line size
#define CACHE_LINE 128
#endif

#include "cpu_relax.h"

static inline void doze(void)
{
    __cpu_relax();
}

int num_cpus(void);

unsigned long cpu_hz(void);
