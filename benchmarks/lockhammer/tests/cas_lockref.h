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

#include "atomics.h"

static inline unsigned long lock_acquire (uint64_t *lock, unsigned long threadnum) {
	unsigned long val, old;

	do {
		old = *(volatile unsigned long *) lock;
		val = old + 0x100000000;

		while ((old & 0xFFFFFFFF) && ((val >> 32) <= 32)) {
			old = *(volatile unsigned long *) lock;
			val = old + 0x100000000;
		}

		val = cas64(lock, val, old);
	} while (val != old);

	return val >> 32;
}

static inline void lock_release (uint64_t *lock, unsigned long threadnum) {
	unsigned long val, old;

	do {
		old = *(volatile unsigned long *) lock;
		val = old - 0x100000000;

		while ((old & 0xFFFFFFFF) && ((val >> 32) > 0)) {
			old = *(volatile unsigned long *) lock;
			val = old - 0x100000000;
		}

		val = cas64(lock, val, old);
	} while (val != old);
}
