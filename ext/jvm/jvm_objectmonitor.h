/*
 * Copyright (c) 1998, 2016, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef JVM_OBJECT_MONITOR_H
#define JVM_OBJECT_MONITOR_H

#include <stdio.h>
#include <pthread.h>
#include <time.h>

#define initialize_lock(lock, threads) jvm_init_locks(lock, threads);

#include "atomics.h"

/*
 * jvm_objectmonitor.h: A model of the OpenJDK ObjectMonitor class.
 *
 * What Is It?
 *   In the JVM, the ObjectMonitor class is responsible for
 *   regulating which threads get access to methods and blocks
 *   protected by the "synchronized" keyword.  This prevents
 *   multiple threads from executing that region at the same time.
 *
 * Why Not C++?
 *   Because I don't feel like cutting umpteen billion dependencies
 *   running around in the JVM.
 *
 * What's Not In Here
 *   I decided to eliminate some code, mainly because it either wouldn't
 *   be exercised in the Lockhammer framework, or because it didn't make
 *   sense outside of the JVM itself.  These include:
 *     - Recursive calls by the same thread (won't happen in this framework).
 *     - Checks for Safepoints (no GC to request it, etc).
 *     - Different handling for native vs Java-based threads (no Java)
 *     - Handling for user-defined knob settings (we're using the defaults).
 *     - Handling various Java implmentations (notify, notifyAll, etc).
 *   Since the main testing for synchronized performance avoided all of these
 *   (except for the Safepoints and Java threads), this shouldn't be an
 *   issue.
 *
 * How it Works
 *   First thread to grab the owner of ObjectMonitor wins.
 *
 *   Next thread that tries to grab ownership gets denied, so it'll try
 *   to Spin for a bit (try to grab the lock, and if denied loop and try
 *   again).  That'll back off with each failure once the main spinning
 *   loop is reached.
 *
 *   If that doesn't work, the next thread drops into the EnterI() call,
 *   which tries to lock, then spin again.  If that fails, that thread will
 *   then try to park itself on a list of threads that want access, then
 *   stop on a conditional variable.
 *
 *   When the first thread completes, it cleans itself up, then checks
 *   if a thread is waiting (there are *two* queues that have candidates,
 *   due to the way the ObjectMonitor code tries to minimize locking).
 *   The first thread grabs the info for the "successor" thread, and
 *   signals that thread's conditional variable.
 *
 *   Note that this doesn't mean that the next thread gets woken up
 *   immediately -- the first thread could still have time left before
 *   being scheduled out, so it's gonna take it.  It's possible for the
 *   first thread to get a couple of free shots in before its yanked out
 *   by the scheduler, at which the next thread can get a chance to grab
 *   ownership of the ObjectMonitor and go from there.
 *
 * Notes:
 *   1) I tried to keep as much as possible the same between this code
 *      and the JVM implementation.  Some of the exchange code is nicely
 *      done with templates over there, so this gets a little uglier (but
 *      should be the same code in the end).
 *   2) This code mainly uses pthread_ts to track the individual
 *      threads, while the JVM uses various Thread-derived objects.
 *      Since ObjectWaiter has some of the data structures that the
 *      Thread classes have, this shouldn't be a big issue (and it
 *      keeps us from having to add more initialization code to
 *      generate the Thread structures).
 */

/*
 * The JVM uses a thread object pointer.  Instead, we just use the
 * pthread_t here.  This just makes the code a little nicer looking.
 */
#define NO_THREAD (pthread_t)0

/* Maximum time to park and recheck a parked thread. */
#define MAX_RECHECK_INTERVAL 1000

/* Synchronize as the JVM does it... */
#define FULL_MEM_BARRIER do { __sync_synchronize(); } while(0)
#define READ_MEM_BARRIER do { __atomic_thread_fence(__ATOMIC_ACQUIRE); } while(0)
#define WRITE_MEM_BARRIER do { __atomic_thread_fence(__ATOMIC_RELEASE); } while(0)

inline static void release_store_thread(volatile pthread_t* dest,
        pthread_t val) {
    READ_MEM_BARRIER;
    *dest = val;
    WRITE_MEM_BARRIER;
}

inline static void storeload(void) {
    FULL_MEM_BARRIER;
}

inline static int int_xchg(int exchange_value, volatile int* dest) {
    int res = __sync_lock_test_and_set(dest, exchange_value);
    FULL_MEM_BARRIER;
    return res;
}
/*
 * Couple of cmpxchg calls used by the JVM.  This filters down to the
 * os/cpu levels of the code, where we finally get to the actual
 * function call itself (__sync_val_compare_and_swap in this case).
 *
 * There's one implementation for pthread_t and the other for int.
 */
inline pthread_t o_thread_cmpxchg(pthread_t exchange_value,
        volatile pthread_t* dest,
        pthread_t compare_value) {
    return __sync_val_compare_and_swap(dest, compare_value, exchange_value);
}

inline int o_int_cmpxchg(int exchange_value, volatile int* dest,
        int compare_value) {
    return __sync_val_compare_and_swap(dest, compare_value, exchange_value);
}

inline void atomic_int_inc(volatile int* dest) {
    __sync_add_and_fetch(dest, 1);
}
inline void atomic_int_dec(volatile int* dest) {
    __sync_add_and_fetch(dest, -1);
}

/*
 * Pseudo SafepointSynchronize stuff here.  Not actually used, but
 * we need to mimic it in the spin code.
 */
volatile int SafepointSynchronize_state = 0;
inline static int doSafepointSynchronizeCallBack(void) {
    return (SafepointSynchronize_state != 0);
}

const long NANOSECS_PER_SEC = 1000000000;
const long NANOSECS_PER_MILLISEC = 1000000;

/* Used by parkObjectWaiterTimed() to determine when to stop waiting. */
static struct timespec* compute_abstime(struct timespec* abstime,
        long millis) {
    if (millis < 0)  millis = 0;

    long seconds = millis / 1000;
    millis %= 1000;
    if (seconds > 50000000) { // see man cond_timedwait(3T)
        seconds = 50000000;
    }

    struct timespec now;
    int status = clock_gettime(CLOCK_MONOTONIC, &now);
    abstime->tv_sec = now.tv_sec  + seconds;
    long nanos = now.tv_nsec + millis * NANOSECS_PER_MILLISEC;
    if (nanos >= NANOSECS_PER_SEC) {
        abstime->tv_sec += 1;
        nanos -= NANOSECS_PER_SEC;
    }
    abstime->tv_nsec = nanos;
    return abstime;
}

/* Possible ObjectWaiter states.  Not all of them are used. */
enum TStates { TS_UNDEF, TS_READY, TS_RUN, TS_WAIT, TS_ENTER, TS_CXQ };

/*
 * Used to keep track of a waiting thread.
 */
struct ObjectWaiter {
    struct ObjectWaiter* volatile _next;
    struct ObjectWaiter* volatile _prev;
    pthread_t _thread;
    volatile enum TStates _state;
    /* Merged in os::PlatformEvent below, so I don't have to
     * keep track of that separately.
     */
    volatile int _Event;
    volatile int _nParked;
    pthread_mutex_t _mutex;
    pthread_cond_t _cond;
};

inline intptr_t o_ptr_cmpxchg(intptr_t exchange_value, intptr_t* volatile dest,
        intptr_t compare_value) {
    return __sync_val_compare_and_swap(dest, compare_value, exchange_value);
}

inline struct ObjectWaiter* o_ow_cmpxchg(struct ObjectWaiter* exchange_value,
        struct ObjectWaiter* volatile *dest,
        struct ObjectWaiter* compare_value) {
    return __sync_val_compare_and_swap(dest, compare_value, exchange_value);
}

struct ObjectWaiter* initializeObjectWaiter(pthread_t thisThread) {
    /* Really miss C++ ctors about now... */
    struct ObjectWaiter *obj =
            (struct ObjectWaiter*)malloc(sizeof(struct ObjectWaiter));
    obj->_next = NULL;
    obj->_prev = NULL;
    obj->_thread = thisThread;
    obj->_state = TS_RUN;
    obj->_Event = 0;
    obj->_nParked = 0;
    pthread_mutex_init(&obj->_mutex, NULL);
    pthread_cond_init(&obj->_cond, NULL);
    return obj;
}

void cleanupObjectWaiter(struct ObjectWaiter *obj) {
    free(obj);
}

/* Park the thread until explicitly unparked by another. */
void parkObjectWaiter(struct ObjectWaiter *obj) {
    int v;
    for (;;) {
        v = obj->_Event;
        if (o_int_cmpxchg(v-1, &obj->_Event, v) == v) break;
    }
    if (v == 0) {
        int status = pthread_mutex_lock(&obj->_mutex);
        ++obj->_nParked;
        while (obj->_Event < 0) {
            status = pthread_cond_wait(&obj->_cond, &obj->_mutex);
            if (status == ETIME) { status = EINTR; }
        }
        --obj->_nParked;
        obj->_Event = 0;
        status = pthread_mutex_unlock(&obj->_mutex);
        FULL_MEM_BARRIER;
    }
}

/* Park the thread until either another thread wakes us up, or until
 * the interval has expired.
 */
void parkObjectWaiterTimed(struct ObjectWaiter *obj, int recheckInterval) {
    int v;
    for (;;) {
        v = obj->_Event;
        if (o_int_cmpxchg(v-1, &obj->_Event, v) == v) break;
    }
    if (v != 0) return;

    struct timespec abst;
    compute_abstime(&abst, recheckInterval);
    int status = pthread_mutex_lock(&obj->_mutex);
    ++obj->_nParked;
    while (obj->_Event < 0) {
        status = pthread_cond_timedwait(&obj->_cond, &obj->_mutex, &abst);
        if (status == ETIME || status == ETIMEDOUT) break;
        // We consume and ignore EINTR and spurious wakeups.
    }
    --obj->_nParked;
    obj->_Event = 0;
    status = pthread_mutex_unlock(&obj->_mutex);
    FULL_MEM_BARRIER;
    return;
}

/* Unpark the specified thread. */
void unparkObjectWaiter(struct ObjectWaiter *node) {
    if (int_xchg(1, &node->_Event) >= 0) {
        return;
    }
    int status = pthread_mutex_lock(&node->_mutex);
    int AnyWaiters = node->_nParked;
    pthread_mutex_unlock(&node->_mutex);
    if (AnyWaiters != 0) {
        status = pthread_cond_signal(&node->_cond);
    }
}

/*
 * Represents the ObjectMonitor object over in the JVM.  The recursion
 * checks have been stripped out to simplify the code.
 *
 * Note that some fields in here (_SpinDuration) aren't protected
 * by a mutex, because the JVM don't care.  Some (_SpinDuration) are
 * kinda-sorta protected, but again -- the JVM don't care.
 */
struct ObjectMonitor {
    pthread_t volatile _owner;
    struct ObjectWaiter* volatile _EntryList;
    struct ObjectWaiter* volatile _cxq;
    volatile int _Spinner;
    volatile int _SpinDuration; /* JVM doesn't protect this ... fiiine... */
    volatile int _count;
    pthread_t volatile _succ;
    pthread_t volatile _Responsible;
};

/* Various tunings from the ObjectMonitor class. We're taking the defaults,
 * and we're not implementing the experimental command-line options.  You'll
 * get what the JVM gives you, and you'll LIKE it. */
static int Knob_SpinBase = 0;
static int Knob_SpinEarly = 1;
static int Knob_CASPenalty = -1;
static int Knob_OXPenalty = -1;
static int Knob_SpinSetSucc = 1;
static int Knob_FixedSpin = 0;
static int Knob_PreSpin = 10;
static int Knob_SpinLimit = 5000;
static int Knob_Poverty = 1000;
static int Knob_MaxSpinners = -1;
static int Knob_Bonus = 100;
static int Knob_BonusB = 100;
static int Knob_SuccRestrict = 0;
static int Knob_SuccEnabled = 1;
static int Knob_UsePause = 1;
static int BackOffMask = 0;
static int Knob_Penalty = 200;
static int Knob_SpinBackOff = 0;
static int Knob_SpinAfterFutile = 1;
static int Knob_ResetEvent = 0;
static int Knob_ExitPolicy = 0;
static int Knob_QMode = 0;

/* The actual, real ObjectMonitor we'll use. */
struct ObjectMonitor *omonitor;

/* Yeah -- this is the actual implementation over in the JVM... */
static inline int SpinPause(void) {
    return 0;
}

/* If we're in one of the queues, then we'll want to get out of it.
 * This handles removing our ObjectWaiter from the linked list.  Note
 * that cleanup of the ObjectWaiter that gets removed is done by the
 * caller.
 */
void UnlinkAfterAcquire(pthread_t thisThread, struct ObjectWaiter *node) {
    if (node->_state == TS_ENTER) {
        struct ObjectWaiter *nxt = node->_next;
        struct ObjectWaiter *prv = node->_prev;
        if (nxt != NULL) nxt->_prev = prv;
        if (prv != NULL) prv->_next = nxt;
        if (node == omonitor->_EntryList) omonitor->_EntryList = nxt;
    } else {
        struct ObjectWaiter *v = omonitor->_cxq;
        if ((v != node) || (o_ow_cmpxchg(node->_next, &omonitor->_cxq, v) != v)) {
            if (v == node) {
                v = omonitor->_cxq;
            }
            struct ObjectWaiter *p;
            struct ObjectWaiter *q = NULL;
            for (p = v; p != NULL && p != node; p = p->_next) {
                q = p;
            }
            q->_next = p->_next;
        }
    }
}

/*
 * Adjust the value by the specified amount.  The JVM doesn't care if this
 * doesn't quite work as expected...
 */
static int Adjust(volatile int *adr, int dx) {
    int v;
    for (v = *adr; o_int_cmpxchg(v + dx, adr, v) != v; v = *adr) /* empty */;
    return v;
}

/*
 * Try to grab the lock.  See ObjectMonitor::TryLock().
 */
static int jvmObjectMonitorTryLock(pthread_t thisThread) {
    pthread_t own = omonitor->_owner;
    if (own != NO_THREAD) return 0; /* Already grabbed */
    if (o_thread_cmpxchg(thisThread, &omonitor->_owner,
                    NO_THREAD) == NO_THREAD) {
        /* Got it */
        return 1;
    }
    /* Close -- but failed... */
    return -1;
}

/* 
 * Try to spin before trying to grab the lock.  Lotta try in that code.
 * See ObjectMonitor::TrySpin().
 */
static int jvmObjectMonitorTrySpin(pthread_t thisThread) {
    int ctr = Knob_FixedSpin;
    if (ctr != 0) {
        while (--ctr >= 0) {
            if (jvmObjectMonitorTryLock(thisThread) > 0) return 1;
            SpinPause();
        }
        return 0;
    }

    for (ctr = Knob_PreSpin + 1; --ctr >= 0;) {
        if (jvmObjectMonitorTryLock(thisThread) > 0) {
            int x = omonitor->_SpinDuration;
            if (x < Knob_SpinLimit) {
                if (x < Knob_Poverty) x = Knob_Poverty;
                omonitor->_SpinDuration = x + Knob_BonusB;
            }
            return 1;
        }
        SpinPause();
    }

    /*
     * Admission control - verify preconditions for spinning
     */
    ctr = omonitor->_SpinDuration;
    if (ctr < Knob_SpinBase) ctr = Knob_SpinBase;
    if (ctr <= 0) return 0;
    
    if (Knob_SuccRestrict && omonitor->_succ != NO_THREAD) return 0;

    int MaxSpin = Knob_MaxSpinners;
    if (MaxSpin >= 0) {
        if (omonitor->_Spinner > MaxSpin) {
            fprintf(stderr, "Spin abort -- too many spinners\n");
            return 0;
        }
        /* "Slightly racy, but benign" according to the JVM... */
        Adjust(&omonitor->_Spinner, 1);
    }

    /* Time to start spinning... */
    int hits    = 0;
    int msk     = 0;
    int caspty  = Knob_CASPenalty;
    int oxpty   = Knob_OXPenalty;
    int sss     = Knob_SpinSetSucc;
    if (sss && omonitor->_succ == NO_THREAD) omonitor->_succ = thisThread;
    pthread_t prv = NO_THREAD;

    /*
     * There are three ways to exit the following loop:
     * 1.  A successful spin where this thread has acquired the lock.
     * 2.  Spin failure with prejudice
     * 3.  Spin failure without prejudice
     */
    while (--ctr >= 0) {
        /* Here, we'd check if we need to bust out to hit a SafePoint,
         * but there's no GC here, so... just mimic the code flow.
         */
        if ((ctr & 0xFF) == 0) {
            if (doSafepointSynchronizeCallBack()) {
                goto Abort;
            }
            if (Knob_UsePause & 1) SpinPause();
        }

        if (Knob_UsePause & 2) SpinPause();

        /* Exponential backoff */
        if (ctr & msk) continue;
        ++hits;
        if ((hits & 0xF) == 0) {
            msk = ((msk << 2)|3) & BackOffMask;
        }

        /* Probe owner.  Haven't they suffered enough? */
        pthread_t ox = omonitor->_owner;
        if (ox == NO_THREAD) {
            ox = o_thread_cmpxchg(thisThread, &omonitor->_owner, NO_THREAD);
            if (ox == NO_THREAD) {
                /* The CAS succeeded, so the thread has ownership.
                 * Do some bookkeeping to exit spin state.
                 */
                if (sss && (omonitor->_succ == thisThread)) {
                    omonitor->_succ = NO_THREAD;
                }
                if (MaxSpin > 0) Adjust(&omonitor->_Spinner, -1);

                /* Increase SpinDuration */
                int x = omonitor->_SpinDuration;
                if (x < Knob_SpinLimit) {
                    if (x < Knob_Poverty) x = Knob_Poverty;
                    omonitor->_SpinDuration = x + Knob_Bonus;
                }
                return 1;
            }

            /* The CAS failed. */
            prv = ox;
            if (caspty == -2) break;
            if (caspty == -1) goto Abort;
            ctr -= caspty;
            continue;
        }

        /* Did lock ownership change hands ? */
        if (ox != prv && prv != NO_THREAD) {
            if (oxpty == -2) break;
            if (oxpty == -1) goto Abort;
            ctr -= oxpty;
        }
        prv = ox;

        if (sss && omonitor->_succ == NO_THREAD) omonitor->_succ = thisThread;

        /* Sync failed with prejudice -- reduce spin duration. */
        {
            int x = omonitor->_SpinDuration;
            if (x > 0) {
                // Consider an AIMD scheme like: x -= (x >> 3) + 100
                // This is globally sample and tends to damp the response.
                x -= Knob_Penalty;
                if (x < 0) x = 0;
                omonitor->_SpinDuration = x;
            }
        }
    }
 Abort:
    if (MaxSpin >= 0) Adjust(&omonitor->_Spinner, -1);
    if (sss && omonitor->_succ == thisThread) {
        omonitor->_succ = NO_THREAD;
        // Invariant: after setting succ=null a contending thread
        // must recheck-retry _owner before parking.  This usually happens
        // in the normal usage of TrySpin(), but it's safest
        // to make TrySpin() as foolproof as possible.
        FULL_MEM_BARRIER;
        if (jvmObjectMonitorTryLock(thisThread) > 0) return 1;
    }
    return 0;
    
}

/* This is called by the jvmObjectMonitorEnter() call to handle cases where
 * someone else already has ownership of ObjectMonitor.  The calling thread
 * will try to lock, try to spin, and if that gets to be too much, park
 * itself until told to wake up by another thread leaving the ObjectMonitor.
 */
void jvmObjectMonitorEnterI(pthread_t thisThread) {
    /* Try the lock ... again.*/
    if (jvmObjectMonitorTryLock(thisThread) > 0) {
        return;
    }

    /* Try one round of spinning before enqueing self... */
    if (jvmObjectMonitorTrySpin(thisThread) > 0) {
        return;
    }

    /* The Spin failed -- enqueue and park this thread. */
    struct ObjectWaiter *node = initializeObjectWaiter(thisThread);
    node->_state = TS_CXQ;
    struct ObjectWaiter *nxt;
    for (;;) {
        node->_next = nxt = omonitor->_cxq;
        if (o_ow_cmpxchg(node, &omonitor->_cxq, nxt) == nxt) break;
        if (jvmObjectMonitorTryLock(thisThread) > 0) {
            cleanupObjectWaiter(node);
            return;
        }
    }

    if ((nxt == NULL) && (omonitor->_EntryList == NULL)) {
        o_thread_cmpxchg(thisThread, &omonitor->_Responsible, NO_THREAD);
    }

    int nWakeups = 0;
    int recheckInterval = 1;
    for (;;) {
        if (jvmObjectMonitorTryLock(thisThread) > 0) break;
        if (omonitor->_Responsible == thisThread) {
            parkObjectWaiterTimed(node, recheckInterval);
            recheckInterval *= 8;
            if (recheckInterval > MAX_RECHECK_INTERVAL) {
                recheckInterval = MAX_RECHECK_INTERVAL;
            }
        } else {
            parkObjectWaiter(node);
        }

        if (jvmObjectMonitorTryLock(thisThread) > 0) break;
        ++nWakeups;
        if ((Knob_SpinAfterFutile & 1) &&
                (jvmObjectMonitorTrySpin(thisThread) > 0)) break;
        if ((Knob_ResetEvent & 1) && (node->_Event != 0)) {
            node->_Event = 0;
            FULL_MEM_BARRIER;
        }
        if (omonitor->_succ == thisThread) omonitor->_succ = NO_THREAD;
        FULL_MEM_BARRIER;
    }

    /* Egress: Self has acquired the lock.  Note that we unlink
     * the node from the queue, then destroys it (since we own
     * the ObjectMonitor -- we don't need that ObjectWaiter anymore...
     */
    UnlinkAfterAcquire(thisThread, node);
    cleanupObjectWaiter(node);
    if (omonitor->_succ == thisThread) omonitor->_succ = NO_THREAD;
    if (omonitor->_Responsible == thisThread) {
        omonitor->_Responsible = NO_THREAD;
        FULL_MEM_BARRIER;
    }
}

int jvmObjectMonitorExitSuspendEquivalent(pthread_t thisThread) {
    return 1;
}

/* This represents ObjectMonitor::enter() over in the JVM.  It basically
 * just tries to grab ownership of the ObjectMonitor.  If that fails, then
 * the thread tries to spin and see if it can grab ownership after a few
 * iterations.  After that, it calls jvmObjectMonitorEnterI() to try that
 * again and then queue itself to the list of threads waiting for ownership
 * and blocks until woken up. So the only way out of here is to actually
 * get ownership of the ObjectMonitor itself.
 */
static unsigned long jvmObjectMonitorEnter(pthread_t thisThread) {
    pthread_t cur = o_thread_cmpxchg(thisThread, &omonitor->_owner, NO_THREAD);
    if (cur == NO_THREAD) {
        return 1;
    }
    if (Knob_SpinEarly && jvmObjectMonitorTrySpin(thisThread) > 0) {
        return 1;
    }

    atomic_int_inc(&omonitor->_count);
    {
        for (;;) {
            jvmObjectMonitorEnterI(thisThread);
            break;
        }
    }
    atomic_int_dec(&omonitor->_count);
    return 1;
}

/* Final cleanup code when a thread's releasing the ObjectMonitor.
 * It'll set the successor thread, release ownership of the ObjectMonitor,
 * then unpark the successor thread.
 */
static void jvmObjectMonitorExitEpilog(pthread_t thisThread,
        struct ObjectWaiter* Wakee) {
    omonitor->_succ = (Knob_SuccEnabled ? Wakee->_thread : NO_THREAD);
    release_store_thread(&omonitor->_owner, NO_THREAD);
    FULL_MEM_BARRIER;
    unparkObjectWaiter(Wakee);
}

/* Called when the owning thread's ready to release the ObjectMonitor.
 * If there are no outstanding ObjectWaiters hanging in the queues, it'll
 * just return once it's cleaned up.  If there are, it'll do the
 * post-run maintenance on those queues and call jvmObjectMonitorExitEpilog()
 * to wake that next thread up. 
 */
static void jvmObjectMonitorExit(pthread_t thisThread) {
    if (thisThread == omonitor->_owner) {
        omonitor->_owner = thisThread;
        /* Removed code having to do with recursions -- not tracking that. */
    }

    omonitor->_Responsible = NO_THREAD;

    for (;;) {
        if (Knob_ExitPolicy == 0) {
            release_store_thread(&omonitor->_owner, NO_THREAD);
            storeload();
            if ((((intptr_t)omonitor->_EntryList | (intptr_t)omonitor->_cxq) == 0) ||
                    (omonitor->_succ != NO_THREAD)) {
                return;
            }

            if (o_thread_cmpxchg(thisThread, &omonitor->_owner, NO_THREAD) != NO_THREAD) {
                return;
            }
        } else {
            if ((((intptr_t)omonitor->_EntryList | (intptr_t)omonitor->_cxq) == 0) ||
                    (omonitor->_succ != NO_THREAD)) {
                release_store_thread(&omonitor->_owner, NO_THREAD);
                storeload();
                if (omonitor->_cxq == NO_THREAD || omonitor->_succ != NO_THREAD) {
                    return;
                }
                if (o_thread_cmpxchg(thisThread, &omonitor->_owner, NO_THREAD)
                        != NO_THREAD) {
                    return;
                }
            } else {
            }
        }

        struct ObjectWaiter *w = NULL;
        int QMode = Knob_QMode;
        /* Bunch of experimental stuff handling various QMode settings
         * removed for simplification -- never gets executed here...
         */
        w = omonitor->_EntryList;
        if (w != NULL) {
            jvmObjectMonitorExitEpilog(thisThread, w);
            return;
        }

        w = omonitor->_cxq;
        if (w == NULL) continue;

        for (;;) {
            struct ObjectWaiter *u = o_ow_cmpxchg(NULL, &omonitor->_cxq, w);
            if (u == w) break;
            w = u;
        }

        /* Was a check here for QMode == 1, but what follows is QMode == 0 */
        omonitor->_EntryList = w;
        struct ObjectWaiter *q = NULL;
        struct ObjectWaiter *p;
        for (p = w; p != NULL; p = p->_next) {
            p->_state = TS_ENTER;
            p->_prev = q;
            q = p;
        }

        if (omonitor->_succ != NO_THREAD) continue;
        w = omonitor->_EntryList;
        if (w != NULL) {
            jvmObjectMonitorExitEpilog(thisThread, w);
            return;
        }
    }
}

/* Called once to initialize the ObjectMonitor. */
static void jvm_init_locks(uint64_t *lock, unsigned long cores) {
    // Initialize the ObjectMonitor
    omonitor = (struct ObjectMonitor*)malloc(sizeof(struct ObjectMonitor));
    omonitor->_owner = NO_THREAD;
    omonitor->_EntryList = NULL;
    omonitor->_cxq = NULL;
    omonitor->_Spinner = 0;
    omonitor->_SpinDuration = 0;
    omonitor->_count = 0;
    omonitor->_succ = NO_THREAD;
    omonitor->_Responsible = NO_THREAD;

    /* This was over in DeferredInitialization() in the ObjectMonitor
     * code, so just shuffle it over here before we fire up the threads.*/
    BackOffMask = (1 << Knob_SpinBackOff) - 1;
}

/* Lock interface back to the framework. */
static inline unsigned long lock_acquire(uint64_t *lock,
        unsigned long threadnum) {
    return jvmObjectMonitorEnter(pthread_self());
}

/* Unlock interface back to the framework. */
static inline void lock_release(uint64_t *lock,
        unsigned long threadnum) {
    jvmObjectMonitorExit(pthread_self());
}

#endif
