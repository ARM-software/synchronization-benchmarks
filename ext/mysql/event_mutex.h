/*****************************************************************************

Copyright (c) 2013, 2017, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2017, The Linux Foundation. All rights reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA

*****************************************************************************/

/* Based on MySQL 5.7 */
#ifdef initialize_lock
#undef initialize_lock
#endif

#define initialize_lock(lock, threads) event_mutex_init(lock, threads)

#include "atomics.h"
#include "ut_atomics.h"

unsigned long ev_generation = 0;

typedef unsigned long ulint;

/** Mutex states. */
enum mutex_state_t {
	/** Mutex is free */
	MUTEX_STATE_UNLOCKED = 0,

	/** Mutex is acquired by some thread. */
	MUTEX_STATE_LOCKED = 1,

	/** Mutex is contended and there are threads waiting on the lock. */
	MUTEX_STATE_WAITERS = 2
};

#define UT_RND1			151117737   // 901DFA9
#define UT_RND2			119785373   // 723C79D
#define UT_RND3			 85689495   // 51B8497
#define UT_RND4			 76595339   // 490C08B
#define UT_SUM_RND2		 98781234   // 5E34832
#define UT_SUM_RND3		126792457   // 78EB309
#define UT_SUM_RND4		 63498502   // 3C8E906
#define UT_XOR_RND1		187678878   // B2FC09E
#define UT_XOR_RND2		143537923   // 88E3703

/** Seed value of ut_rnd_gen_ulint() */
ulint	 ut_rnd_ulint_counter = 65654363;

/** Wakeup any waiting thread(s). */

void lock_signal(void)
{
	unsigned long version = *((volatile unsigned long *) &ev_generation);

	
	*((volatile unsigned long *) &ev_generation) = (version + 1);
}

	/** Try and acquire the lock using TestAndSet.
	@return	true if lock succeeded */
	int tas_lock(uint64_t *lock)
	{
		return(swap64(lock, MUTEX_STATE_LOCKED)
			== MUTEX_STATE_UNLOCKED);
	}

	/** In theory __sync_lock_release should be used to release the lock.
	Unfortunately, it does not work properly alone. The workaround is
	that more conservative __sync_lock_test_and_set is used instead. */
	void tas_unlock(uint64_t *lock)
	{
		swap64(lock, MUTEX_STATE_UNLOCKED);
	}



/********************************************************//**
The following function generates a series of 'random' ulint integers.
@return the next 'random' number */
static inline
ulint
ut_rnd_gen_next_ulint(
/*==================*/
	ulint	rnd)	/*!< in: the previous random number value */
{
	ulint	n_bits;

	n_bits = 8 * sizeof(ulint);

	rnd = UT_RND2 * rnd + UT_SUM_RND3;
	rnd = UT_XOR_RND1 ^ rnd;
	rnd = (rnd << 20) + (rnd >> (n_bits - 20));
	rnd = UT_RND3 * rnd + UT_SUM_RND4;
	rnd = UT_XOR_RND2 ^ rnd;
	rnd = (rnd << 20) + (rnd >> (n_bits - 20));
	rnd = UT_RND1 * rnd + UT_SUM_RND2;

	return(rnd);
}

/********************************************************//**
The following function generates 'random' ulint integers which
enumerate the value space of ulint integers in a pseudo random
fashion. Note that the same integer is repeated always after
2 to power 32 calls to the generator (if ulint is 32-bit).
@return the 'random' number */
static inline ulint
ut_rnd_gen_ulint(void)
/*==================*/
{
	ulint	rnd;

	ut_rnd_ulint_counter = UT_RND1 * ut_rnd_ulint_counter + UT_RND2;

	rnd = ut_rnd_gen_next_ulint(ut_rnd_ulint_counter);

	return(rnd);
}

/********************************************************//**
Generates a random integer from a given interval.
@return the 'random' number */
ulint
ut_rnd_interval(
/*============*/
	ulint	low,	/*!< in: low limit; can generate also this value */
	ulint	high)	/*!< in: high limit; can generate also this value */
{
	ulint	rnd;

	if (low == high) {

		return(low);
	}

	rnd = ut_rnd_gen_ulint();

	return(low + (rnd % (high - low)));
}

ulint
ut_delay(
/*=====*/
	ulint	delay)	/*!< in: delay in microseconds on 100 MHz Pentium */
{
	ulint	i, j;

	j = 0;

	for (i = 0; i < delay * 50; i++) {
		j += i;
		UT_RELAX_CPU();
	}

	return(j);
}

	/** @return true if locked by some thread */
	int is_locked(uint64_t *lock)
	{
		return(*lock != MUTEX_STATE_UNLOCKED);
	}

	/** Spin and wait for the mutex to become free.
	@param[in]	max_spins	max spins
	@param[in]	max_delay	max delay per spin
	@param[in,out]	n_spins		spin start index
	@return true if unlocked */
	int is_free(
		uint64_t	*lock,
		uint32_t	max_spins,
		uint32_t	max_delay,
		uint32_t	*n_spins)
	{
		/* Spin waiting for the lock word to become zero. Note
		that we do not have to assume that the read access to
		the lock word is atomic, as the actual locking is always
		committed with atomic test-and-set. In reality, however,
		all processors probably have an atomic read of a memory word. */

		do {
			if (!is_locked(lock)) {
				return(1);
			}

			ut_delay(ut_rnd_interval(0, max_delay));

			++(*n_spins);

		} while (*n_spins < max_spins);

		return(0);
	}

void event_mutex_init(uint64_t *lock, uint64_t threads) {
	*lock = MUTEX_STATE_UNLOCKED;
}

	/** Try and lock the mutex. Note: POSIX returns 0 on success.
	@return true on success */
	int try_lock(uint64_t *lock)
	{
		return(tas_lock(lock));
	}

	/** Release the mutex. */
	void lock_exit(uint64_t *lock)
	{
		/* A problem: we assume that mutex_reset_lock word
		is a memory barrier, that is when we read the waiters
		field next, the read must be serialized in memory
		after the reset. A speculative processor might
		perform the read first, which could leave a waiting
		thread hanging indefinitely.

		Our current solution call every second
		sync_arr_wake_threads_if_sema_free()
		to wake up possible hanging threads if they are missed
		in mutex_signal_object. */

		tas_unlock(lock);

		lock_signal();
	}

	/** Spin while trying to acquire the mutex
	@param[in]	max_spins	max number of spins
	@param[in]	max_delay	max delay per spin
	@param[in]	filename	from where called
	@param[in]	line		within filename */
	unsigned long spin_and_try_lock(
		uint64_t	*lock,
		uint32_t	max_spins,
		uint32_t	max_delay)
	{
		uint32_t	n_spins = 0;
		uint32_t	n_waits = 0;
		const uint32_t	step = max_spins;
		unsigned long	wait_state;

		os_rmb;

		for (;;) {

			/* If the lock was free then try and acquire it. */

			if (is_free(lock, max_spins, max_delay, &n_spins)) {

				if (try_lock(lock)) {

					break;
				} else {

					continue;
				}

			} else {
				max_spins = n_spins + step;
			}

			++n_waits;

			wait_state = *((volatile unsigned long *) &ev_generation);

			// Try lock one last time to avoid race with releaser
			if (try_lock(lock)) {
				break;
			}

			// Spin until generation changes
			while(*((volatile unsigned long *) &ev_generation) == wait_state);
		}

		return n_spins;
	}


	/** Acquire the mutex.
	@param[in]	max_spins	max number of spins
	@param[in]	max_delay	max delay per spin
	@param[in]	filename	from where called
	@param[in]	line		within filename */
	unsigned long lock_enter(uint64_t *lock,
		uint32_t	max_spins,
		uint32_t	max_delay)
	{
		if (!try_lock(lock)) {
			return spin_and_try_lock(lock, max_spins, max_delay);
		}

		return 0;
	}


static inline unsigned long lock_acquire (uint64_t *lock, unsigned long threadnum) {
	return lock_enter(lock, 30, 600);
}

static inline void lock_release (uint64_t *lock, unsigned long threadnum) {
	lock_exit(lock);
}
