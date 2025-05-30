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

#ifdef initialize_lock
#undef initialize_lock
#endif

#define initialize_lock(lock, pinorder, threads) cas_rw_lock_init(lock, threads)
#define CAS_RW_INIT_VAL 0x20000000
#define CAS_RW_THRESHOLD 0

#include "atomics.h"

void cas_rw_lock_init(uint64_t *lock, uint64_t threads) {
	*lock = CAS_RW_INIT_VAL;
}

static inline unsigned long lock_acquire (uint64_t *lock, unsigned long threadnum) {
	unsigned long val, old;

	old = *(volatile unsigned long *) lock;
	val = old - 1;

	while (*((long *) &old) > CAS_RW_THRESHOLD) {
		old = *(volatile unsigned long *) lock;
		val = old - 1;
		val = cas64_acquire(lock, val, old);

		if (val == old) {
			return CAS_RW_INIT_VAL - val;
		}
	}

	/* exclusive lock is held (should never actually happen in this test) */
	return 0;
}

static inline void lock_release (uint64_t *lock, unsigned long threadnum) {
	fetchadd64_release(lock, 1);
}

/* vim: set tabstop=8 shiftwidth=8 softtabstop=8 noexpandtab: */
