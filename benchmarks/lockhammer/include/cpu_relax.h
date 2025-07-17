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

#ifndef CPU_RELAX_H
#define CPU_RELAX_H


#ifndef CPU_RELAX_ITERATIONS
#define CPU_RELAX_ITERATIONS 1
#endif

static inline void __cpu_relax(void) {
    for (unsigned long i = 0; i < CPU_RELAX_ITERATIONS; i++) {
#ifdef __aarch64__
#if defined(RELAX_IS_ISB)
        asm volatile ("isb" : : : "memory" );
#elif defined(RELAX_IS_NOP)
        asm volatile ("nop" : : : "memory");
#elif defined(RELAX_IS_EMPTY)
        asm volatile ("" : : : "memory");
#elif defined(RELAX_IS_NOTHING)

#endif
#endif // __aarch64__

#ifdef __x86_64__

#if defined(RELAX_IS_PAUSE)
    // RELAX_IS_PAUSE is the implementation for x86 in jdk-9
    asm volatile ("rep; nop"); // aka pause
#elif defined(RELAX_IS_EMPTY)
    asm volatile ("" : : : "memory");
#elif defined(RELAX_IS_NOTHING)

#endif
#endif // __x86_64__

#ifdef __riscv
#if defined(RELAX_IS_EMPTY)
	asm volatile ("" : : : "memory");
#elif defined(RELAX_IS_NOP)
	asm volatile ("nop" : : : "memory");
#elif defined(RELAX_IS_NOTHING)
	
#endif
#endif // __riscv

    }
}
#endif // CPU_RELAX_H

/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
