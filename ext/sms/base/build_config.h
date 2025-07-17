// Copyright (c) 2017 ARM Limited. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// Architecture detection is inferred from the toolchain. This relies on
// the C compiler's system-specific macros.
#if defined(__aarch64__)
#define CONFIG_ARCH_ARM_V8
#define CONFIG_ARCH_64BIT
#elif defined(__arm__)
#define CONFIG_ARCH_ARM_V7
#define CONFIG_ARCH_32BIT
#elif defined(__x86_64__)
#define CONFIG_ARCH_X86_64
#define CONFIG_ARCH_64BIT
#elif defined(__i386__)
#define CONFIG_ARCH_X86
#define CONFIG_ARCH_32BIT
#elif defined(__riscv)
#define CONFIG_ARCH_RISCV64
#define CONFIG_ARCH_64BIT
#endif

#if !defined(CONFIG_ARCH_64BIT) && !defined(CONFIG_ARCH_32BIT)
#error Please add support for N-bit computing to build_config.h
// If you experience this C pre-processor error, take a look at the place
// in this file where CONFIG_ARCH_64/32BIT are defined. If there are no issues
// there and you are needing to add support for a new N-bit processor, please
// search the source code for all occurances of CONFIG_ARCH_64BIT and
// CONFIG_ARCH_32BIT to check whether further modification is necessary.
// These places will not necessarily #error for unsupported N-bit computing.
#endif

// OS detection is also inferred from the toolchain.
#if defined(__APPLE__)
#define OS_MACOSX 1
#elif defined(__linux__)
#define OS_LINUX 1
#elif defined(__FreeBSD__)
#define OS_FREEBSD 1
#endif

#if defined(OS_MACOSX) || defined(OS_LINUX) || defined(OS_FREEBSD)
#define OS_POSIX 1
#endif

#define MAX_THREADS 32

//Use LL/SC atomic primitives instead of __atomic_compare_exchange built-ins
//This seems to be the most performant option on ARM but may violate
//recommendations by the ARM architecture (e.g. no memory accesses between
//LL and SC)
//USE_LLSC overrides the use of __atomic_compare_exchange
#ifdef __ARM_ARCH
#define USE_LLSC
#endif

//Use barrier + relaxed store (DMB;STR) instead of store-release (STRL)
//This is more performant on Cortex-A57 and possibly also on Cortex-A53
#if defined(__aarch64__)
#define USE_DMB
#endif

#if defined(USE_DMB) && defined(__arm__)
#error USE_DMB optimization only applies to select ARMv8 processors
#endif

//Use ARM wait-for-event mechanism when busy polling
//This will minimise interconnect transactions and often increase system-wide
//performance
#if defined __ARM_ARCH
#define USE_WFE
#if defined(__arm__)
//TODO: WFE on ARMv7
#undef USE_WFE
#endif
#endif
