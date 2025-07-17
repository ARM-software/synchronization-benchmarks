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
 * Portions copyright (c) 2019, ARM Limited and Contributors. All rights
 * reserved.
 */

#ifndef JVM_OBJECT_MONITOR_H
#define JVM_OBJECT_MONITOR_H

#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include <stddef.h>
#include <stdarg.h>

#ifdef initialize_lock
#undef initialize_lock
#endif

#define initialize_lock(lock, pinorder, num_threads) jvm_init_locks(lock, num_threads);

#include "atomics.h"
#include "cpu_relax.h"

/*
 * jvm_objectmonitor.h: A model of the OpenJDK ObjectMonitor class.
 *
 * based on OpenJDK9
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
 *   3) ParkEvents are made/remade per thread in an array at each
 *      measurement init instead of being recycled from a freelist.
 */

/*
 * The JVM uses a thread object pointer.  Instead, we just use the
 * pthread_t here.  This just makes the code a little nicer looking.
 */
#define NO_THREAD (pthread_t)0

/* Maximum time to park and recheck a parked thread. */
#define MAX_RECHECK_INTERVAL 1000

/* Define to use the lock pointer from lockhammer for the omonitor. */
#define USE_LOCK_POINTER

/* Define to malloc ObjectWaiters instead of how it's a stack object in jdk9 */
//#define MALLOC_OBJECTWAITER

/* Synchronize as the JVM does it... */
#define FULL_MEM_BARRIER do { __sync_synchronize(); } while(0)
#define READ_MEM_BARRIER do { __atomic_thread_fence(__ATOMIC_ACQUIRE); } while(0)
#define WRITE_MEM_BARRIER do { __atomic_thread_fence(__ATOMIC_RELEASE); } while(0)

// -----------------------
// debugging functions

static void report_vm_error(const char* detail_fmt, ...) {
    va_list detail_args;
    va_start(detail_args, detail_fmt);
    vprintf(detail_fmt, detail_args);
    va_end(detail_args);
}

// "guarantee() is like vmassert except it's always executed -- use it for
// cheap tests that catch errors that would otherwise be hard to find."

// The 'always executed' part should affect some code generation in contrast
// with using assert(), which would not execute it with NDEBUG defined.

// Note use of double-hash ##__VA_ARGS__, a GNU CPP extension that omits
// the trailing comma when no variable arguments are given to the macro.

#define BREAKPOINT  abort()

#define guarantee(p, ...)                                                         \
do {                                                                              \
  if (__builtin_expect(!(p), 0)) {                                                \
    report_vm_error(stringify(__FILE__) " line " stringify(__LINE__) " guarantee(" #p ") failed\n", ##__VA_ARGS__); \
    BREAKPOINT;                                                                   \
  }                                                                               \
} while (0)

// XXX: define NDEBUG_GUARANTEE to remove the compulsory evaluations for guarantee().
//#define NDEBUG_GUARANTEE
#ifdef NDEBUG_GUARANTEE
#undef guarantee
#define guarantee(p, ...)    (void) (0)
#endif


// XXX: redefine assert() to take a message argument
// Note: NDEBUG is set in Makefile
#ifdef NDEBUG
#undef assert
#define assert(p, ...)    (void) (0)
#else
#undef assert
#define assert(p, ...) \
do {                                                                              \
  if (__builtin_expect(!(p), 0)) {                                                \
    report_vm_error(stringify(__FILE__) " line " stringify(__LINE__) " assert(" #p ") failed\n", ##__VA_ARGS__); \
    BREAKPOINT;                                                                   \
  }                                                                               \
} while (0)
#endif


// ---------------------------
// data structure definitions

// ParkEvent is in the Thread object, so it is persistent through a measurement.
struct ParkEvent {
    // os::PlatformEvent base class members
    double CachePad[4]; // increase odds that _mutex is sole occupant of cache line (dubious)
    volatile int _Event;
    volatile int _nParked;
    pthread_mutex_t _mutex[1];
    pthread_cond_t _cond[1];
    double PostPad[2];
    pthread_t _Assoc;

    // ParkEvent members
    struct ParkEvent * FreeNext;
    pthread_t AssociatedWith;
    struct ParkEvent volatile * ListNext;

    volatile intptr_t OnList;
    volatile int TState;
    volatile int Notified;
};

/* Possible ObjectWaiter states.  Not all of them are used. */
enum TStates { TS_UNDEF, TS_READY, TS_RUN, TS_WAIT, TS_ENTER, TS_CXQ };
//             0         1         2       3          4       5


/*
 * ObjectWaiter keeps track of a thread over a lock_acquire/lock_release pair.
 */

struct ObjectWaiter {
    struct ObjectWaiter* volatile _next;
    struct ObjectWaiter* volatile _prev;
    pthread_t          _thread;
    long               _notifier_tid;   // unused
    struct ParkEvent * _event;
    volatile int       _notified;       // unused
    volatile enum TStates TState;
    int                _Sorted;         // unused
    char               _active;         // unused
};

/*
 * Represents the ObjectMonitor object over in the JVM.  The recursion
 * checks have been stripped out to simplify the code.
 *
 * Note that some fields in here (_SpinDuration) aren't protected
 * by a mutex, because the JVM don't care.  Some (_SpinDuration) are
 * kinda-sorta protected, but again -- the JVM don't care.
 *
 * There are more fields in the class that are not defined here.
 *
 * The relative offset of certain fields is important for functionality
 * and performance in the VM, but the nuances have not been exactly reproduced.
 */

struct ObjectMonitor {
    pthread_t volatile _owner;                  // monitor-owning thread
    struct ObjectWaiter* volatile _EntryList;   // (Waitnodes acting as proxy for) Threads blocked on entry or reentry.
    struct ObjectWaiter* volatile _cxq;         // LL of recently-arrived threads blocked on entry.
    pthread_t volatile _succ;                   // Heir presumptive thread - used for futile wakeup throttling
    pthread_t volatile _Responsible;            // thread that will periodically unpark

    volatile int _Spinner;                      // for exit->spinner handoff optimization (not used in this benchmark)
    volatile int _SpinDuration;                 /* JVM doesn't protect this ... fiiine... */
    volatile int _count;                        // count of threads who have entered monitor
};

size_t park_events_length = 0;
struct ParkEvent * park_events = NULL;

inline static void release_store_thread(volatile pthread_t* dest,
        pthread_t val) {
    __atomic_store_n(dest, val, __ATOMIC_RELEASE);
}

#if __x86_64__
inline static void OrderAccess_compiler_barrier(void) {
    __asm__ volatile ("" : : : "memory");
}

inline static void OrderAccess_fence(void) {
    __asm__ volatile ("lock; addl $0,0(%%rsp)" : : : "cc", "memory");
    OrderAccess_compiler_barrier();
}
#endif

#ifdef __aarch64__
inline static void OrderAccess_fence(void) {
    FULL_MEM_BARRIER;
}
#endif

#ifdef __riscv
inline static void OrderAccess_fence(void) {
	__asm__ volatile ("fence rw,rw" : : : "memory");	
}
#endif

inline static void storeload(void) {
    OrderAccess_fence();
}

// for Atomic::xchg()
#if __x86_64__
inline int     Atomic__xchg    (int     exchange_value, volatile int*     dest) {
    __asm__ volatile (  "xchgl (%2),%0"
                      : "=r" (exchange_value)
                      : "0" (exchange_value), "r" (dest)
                      : "memory");
    return exchange_value;
}

inline static int int_xchg(int exchange_value, volatile int* dest) {
    return Atomic__xchg(exchange_value, dest);
}
#elif defined(__aarch64__)
inline static int int_xchg(int exchange_value, volatile int* dest) {
    int res = __sync_lock_test_and_set(dest, exchange_value);
    FULL_MEM_BARRIER;
    return res;
}
#elif defined(__riscv)
inline static int int_xchg(int exchange_value, volatile int* dest) {
	int result;
    __asm__ __volatile__ (
        "amoswap.w.aqrl %0, %1, (%2)"
        : "=r" (result)
        : "r" (exchange_value), "r" (dest)
        : "memory"
    );
    return result;
}
#endif

/*
 * Couple of cmpxchg calls used by the JVM.  This filters down to the
 * os/cpu levels of the code, where we finally get to the actual
 * function call itself (__sync_val_compare_and_swap in this case).
 *
 * There's one implementation for pthread_t and the other for int.
 */

// Atomic::cmpxchg_ptr  --> o_thread_cmpxchg
inline pthread_t o_thread_cmpxchg(pthread_t exchange_value,
        volatile pthread_t* dest, pthread_t compare_value) {
    return __sync_val_compare_and_swap(dest, compare_value, exchange_value);
}

// Atomic::cmpxchg --> o_int_cmpxchg
inline int o_int_cmpxchg(int exchange_value, volatile int* dest,
        int compare_value) {
    return __sync_val_compare_and_swap(dest, compare_value, exchange_value);
}

// returns the updated value
inline int atomic_int_inc(volatile int* dest) {
    return __sync_add_and_fetch(dest, 1);
}

inline int atomic_int_dec(volatile int* dest) {
    return __sync_add_and_fetch(dest, -1);
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
    (void) status;
    abstime->tv_sec = now.tv_sec  + seconds;
    long nanos = now.tv_nsec + millis * NANOSECS_PER_MILLISEC;
    if (nanos >= NANOSECS_PER_SEC) {
        abstime->tv_sec += 1;
        nanos -= NANOSECS_PER_SEC;
    }
    abstime->tv_nsec = nanos;
    return abstime;
}


#if 0
// unused
void parkevent_reset(struct ParkEvent * p) {
    // actually PlatformEvent::reset()
    p->_Event = 0;
}

void parkevent_associate(struct ParkEvent * p, pthread_t thisThread) {
    parkevent_reset(p);
    p->AssociatedWith = thisThread;
}
#endif

void parkevent_init(struct ParkEvent *p) {
    int status; (void) status;

    // os::PlatformEvent constructor init
    pthread_condattr_t * attr = NULL;
    status = pthread_cond_init(p->_cond, attr); // XXX: is attr=NULL OK?
    assert(status == 0, "cond_init");
    status = pthread_mutex_init(p->_mutex, NULL);
    assert(status == 0, "mutex_init");
    p->_Event = 0;
    p->_nParked = 0;
    p->_Assoc = NO_THREAD;

    // ParkEvent constructor init
    p->FreeNext = NULL;
    p->AssociatedWith = NO_THREAD;
    p->ListNext = NULL;
    p->OnList = 0;
    p->TState = 0;
    p->Notified = 0;
}

void parkevent_destroy(struct ParkEvent *p) {
    int status; (void) status;
    status = pthread_cond_destroy(p->_cond);
    assert(status == 0, "cond_destroy");
    status = pthread_mutex_destroy(p->_mutex);
    assert(status == 0, "mutex_destroy");
    parkevent_init(p);
}


/* -- this function is not used
inline intptr_t o_ptr_cmpxchg(intptr_t exchange_value, intptr_t* volatile dest,
        intptr_t compare_value) {
    return __sync_val_compare_and_swap(dest, compare_value, exchange_value);
}
*/

inline struct ObjectWaiter* o_ow_cmpxchg(struct ObjectWaiter* exchange_value,
        struct ObjectWaiter* volatile *dest,
        struct ObjectWaiter* compare_value) {
    return __sync_val_compare_and_swap(dest, compare_value, exchange_value);
}

#ifdef MALLOC_OBJECTWAITER
struct ObjectWaiter* initializeObjectWaiter(pthread_t thisThread, struct ParkEvent * pe) {
    /* Really miss C++ ctors about now... */
    struct ObjectWaiter *obj;
    if (posix_memalign((void *) &obj, 64, sizeof(struct ObjectWaiter))) {
        fprintf(stderr, "jvm_objectmonitor posix_memalign failed allocating struct ObjectWaiter\n");
        exit(-1);
    }

    obj->_next = NULL;
    obj->_prev = NULL;
    obj->_thread = thisThread;
    obj->TState = TS_RUN;
    obj->_event = pe;
    parkevent_init(pe);
    return obj;
}
#endif

void cleanupObjectWaiter(struct ObjectWaiter *obj) {
#ifdef MALLOC_OBJECTWAITER
    free(obj);
#else
    // do nothing, because ObjectWaiter is allocated on the stack.
#endif
}

// ParkEvent base class is os::PlatformEvent.

// XXX: ParkEvent is 256 byte-aligned using placement new.
// In LH, this is effected using posix_memalign in jvm_init_locks().

// hotspot/src/os/linux/vm/os_linux.cpp void os::PlatformEvent::park()
// refers to the comments in os_solaris.cpp.

// From: hotspot/src/os/solaris/vm/os_solaris.cpp

// ObjectMonitor park-unpark infrastructure ...
//
// We implement Solaris and Linux PlatformEvents with the
// obvious condvar-mutex-flag triple.
// Another alternative that works quite well is pipes:
// Each PlatformEvent consists of a pipe-pair.
// The thread associated with the PlatformEvent
// calls park(), which reads from the input end of the pipe.
// Unpark() writes into the other end of the pipe.
// The write-side of the pipe must be set NDELAY.
// Unfortunately pipes consume a large # of handles.
// Native solaris lwp_park() and lwp_unpark() work nicely, too.
// Using pipes for the 1st few threads might be workable, however.
//
// park() is permitted to return spuriously.
// Callers of park() should wrap the call to park() in
// an appropriate loop.  A litmus test for the correct
// usage of park is the following: if park() were modified
// to immediately return 0 your code should still work,
// albeit degenerating to a spin loop.
//
// In a sense, park()-unpark() just provides more polite spinning
// and polling with the key difference over naive spinning being
// that a parked thread needs to be explicitly unparked() in order
// to wake up and to poll the underlying condition.
//
// Assumption:
//    Only one parker can exist on an event, which is why we allocate
//    them per-thread. Multiple unparkers can coexist.
//
// _Event transitions in park()
//   -1 => -1 : illegal
//    1 =>  0 : pass - return immediately
//    0 => -1 : block; then set _Event to 0 before returning
//
// _Event transitions in unpark()
//    0 => 1 : just return
//    1 => 1 : just return
//   -1 => either 0 or 1; must signal target thread
//         That is, we can safely transition _Event from -1 to either
//         0 or 1.
//
// _Event serves as a restricted-range semaphore.
//   -1 : thread is blocked, i.e. there is a waiter
//    0 : neutral: thread is running or ready,
//        could have been signaled after a wait started
//    1 : signaled - thread is running or ready
//



// Park the thread until explicitly unparked by another.
// Only the thread associated with the Event/PlatformEvent may call park().

void park(struct ParkEvent * pe) {
    assert(pe->_nParked == 0, "invariant");
    int v;
    for (;;) {
        v = pe->_Event;
        // store v-1 into pe->_Event if *pe->_Event has the value of v.
        if (o_int_cmpxchg(v-1, &pe->_Event, v) == v) break;
    }
    guarantee(v >= 0, "invariant");
    if (v == 0) {   // i.e. pe->_Event was 0.
        int status = pthread_mutex_lock(pe->_mutex);
        assert(status == 0, "mutex_lock");
        guarantee(pe->_nParked == 0, "invariant");
        ++pe->_nParked;
        while (pe->_Event < 0) {
            status = pthread_cond_wait(pe->_cond, pe->_mutex);
            // NB: ETIME is not a documented return value for pthread_cond_wait, but jdk-9 checks for it anyway....
            if (status == ETIME) { status = EINTR; }
            assert(status == 0 || status == EINTR, "cond_wait");
        }
        --pe->_nParked;
        pe->_Event = 0;
        status = pthread_mutex_unlock(pe->_mutex);
        assert(status == 0, "mutex_unlock");
        OrderAccess_fence();
    }
    guarantee(pe->_Event >= 0, "invariant");
}

/* Park the thread until either another thread wakes us up, or until
 * the interval has expired.
 */
void parkTimed(struct ParkEvent * pe, long recheckInterval) {
    guarantee(pe->_nParked == 0, "invariant");
    int v;
    for (;;) {
        v = pe->_Event;
        if (o_int_cmpxchg(v-1, &pe->_Event, v) == v) break;
    }
    guarantee(v >= 0, "invariant");
    if (v != 0) return; // XXX: the original returns OS_OK here

    struct timespec abst;
    compute_abstime(&abst, recheckInterval);
    int status = pthread_mutex_lock(pe->_mutex);
    assert(status == 0, "mutex_lock");
    guarantee(pe->_nParked == 0, "invariant");
    ++pe->_nParked;
    while (pe->_Event < 0) {
        status = pthread_cond_timedwait(pe->_cond, pe->_mutex, &abst);
        assert(status == 0 || status == EINTR ||
                  status == ETIME || status == ETIMEDOUT,
                  status, "cond_timedwait");
        if (status == ETIME || status == ETIMEDOUT) break;
        // We consume and ignore EINTR and spurious wakeups.
    }
    --pe->_nParked;
    pe->_Event = 0;
    status = pthread_mutex_unlock(pe->_mutex);
    assert(status == 0, "mutex_unlock");
    assert(pe->_nParked == 0, "invariant");
    OrderAccess_fence();
    return;
}

/* Unpark the specified ParkEvent. */
void unpark(struct ParkEvent * pe) {
    if (int_xchg(1, &pe->_Event) >= 0) {
        return;
    }
    int status; (void) status;
    status = pthread_mutex_lock(pe->_mutex);
    assert(status == 0, "mutex_lock");
    int AnyWaiters = pe->_nParked;
    assert(AnyWaiters == 0 || AnyWaiters == 1, "invariant");
    status = pthread_mutex_unlock(pe->_mutex);
    assert(status == 0, "mutex_lock");
    if (AnyWaiters != 0) {
        status = pthread_cond_signal(pe->_cond);
        assert(status == 0, "cond_signal");
    }
}

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
struct ObjectMonitor *omonitor = NULL;

static inline int SpinPause(void) {
    __cpu_relax();
#ifdef __aarch64__
    return 0;
#elif __x86_64__
    return 1;
#elif __riscv
	return 2;
#else
#error "unsupported instruction set architecture"
#endif
}

/* If we're in one of the queues, then we'll want to get out of it.
 * This handles removing our ObjectWaiter from the linked list.  Note
 * that cleanup of the ObjectWaiter that gets removed is done by the
 * caller.
 */
void UnlinkAfterAcquire(pthread_t thisThread, struct ObjectWaiter *SelfNode) {

    assert(omonitor->_owner == thisThread, "invariant");
    assert(SelfNode->_thread == thisThread, "invariant");

    if (SelfNode->TState == TS_ENTER) {
        struct ObjectWaiter *nxt = SelfNode->_next;
        struct ObjectWaiter *prv = SelfNode->_prev;
        if (nxt != NULL) nxt->_prev = prv;
        if (prv != NULL) prv->_next = nxt;
        if (SelfNode == omonitor->_EntryList) omonitor->_EntryList = nxt;
        assert(nxt == NULL || nxt->TState == TS_ENTER, "invariant");
        assert(prv == NULL || prv->TState == TS_ENTER, "invariant");
    } else {
        assert(SelfNode->TState == TS_CXQ, "invariant");
        struct ObjectWaiter *v = omonitor->_cxq;
        assert(v != NULL, "invariant");
        if ((v != SelfNode) || (o_ow_cmpxchg(SelfNode->_next, &omonitor->_cxq, v) != v)) {
            if (v == SelfNode) {
                assert(omonitor->_cxq != v, "invariant");
                v = omonitor->_cxq;
            }
            struct ObjectWaiter *p;
            struct ObjectWaiter *q = NULL;
            for (p = v; p != NULL && p != SelfNode; p = p->_next) {
                q = p;
                assert(p->TState == TS_CXQ, "invariant");
            }
            assert(v != SelfNode, "invariant");
            assert(p == SelfNode, "Node not found on cxq");
            assert(p != omonitor->_cxq, "invariant");
            assert(q != NULL, "invariant");
            assert(q->_next == p, "invariant");
            q->_next = p->_next;
        }
    }

#ifndef NDEBUG
    // if assert() is enabled
    SelfNode->_prev  = (struct ObjectWaiter *) 0xBAD;
    SelfNode->_next  = (struct ObjectWaiter *) 0xBAD;
    SelfNode->TState = TS_RUN;
#endif
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
 * Try to grab the lock (i.e., insert thisThread into monitor->_owner).
 * See ObjectMonitor::TryLock().
 */
static int jvmObjectMonitorTryLock(pthread_t thisThread) {
    pthread_t own = omonitor->_owner;
    if (own != NO_THREAD) return 0; /* Already grabbed */
    if (o_thread_cmpxchg(thisThread, &omonitor->_owner,
                    NO_THREAD) == NO_THREAD) {
        assert(omonitor->_owner == thisThread, "invariant");
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
int jvmObjectMonitorTrySpin(pthread_t thisThread) {
    int ctr = Knob_FixedSpin;   // is 0 in this code
    if (ctr != 0) {
        while (--ctr >= 0) {
            if (jvmObjectMonitorTryLock(thisThread) > 0) return 1;
            SpinPause();
        }
        return 0;
    }

    // try to cmpxchg thisThread into omonitor->_owner up to 10 times.
    // increase omonitor->_SpinDuration for each successful TryLock insertion.
    for (ctr = Knob_PreSpin + 1; --ctr >= 0;) {             // Knob_PreSpin = 10
        if (jvmObjectMonitorTryLock(thisThread) > 0) {  // TryLock returns > 0 if thisThread wins
            int x = omonitor->_SpinDuration;                // initially 0
            if (x < Knob_SpinLimit) {                       // Knob_SpinLimit = 5000
                if (x < Knob_Poverty) x = Knob_Poverty;     // Knob_Poverty = 1000
                omonitor->_SpinDuration = x + Knob_BonusB;  // Knob_BonusB = 100; first time it will be 1100
            }
            return 1;
        }
        SpinPause();
    }

    /*
     * Admission control - verify preconditions for spinning
     */
    ctr = omonitor->_SpinDuration;
    if (ctr < Knob_SpinBase) ctr = Knob_SpinBase;           // Knob_SpinBase == 0
    if (ctr <= 0) return 0;

    if (Knob_SuccRestrict && omonitor->_succ != NO_THREAD) return 0;    // Knob_SuccRestrict == 0

    int MaxSpin = Knob_MaxSpinners;     // Knob_MaxSpinners == -1
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
    int caspty  = Knob_CASPenalty;          // -1
    int oxpty   = Knob_OXPenalty;           // -1
    int sss     = Knob_SpinSetSucc;         // 1
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
            if (Knob_UsePause & 1) SpinPause(); // Knob_UsePause == 1, so always
        }

        if (Knob_UsePause & 2) SpinPause(); // Knob_UsePause == 1, so never

        /* Exponential backoff */
        if (ctr & msk) continue;
        ++hits;
        if ((hits & 0xF) == 0) {
            msk = ((msk << 2)|3) & BackOffMask;
        }

        /* Probe owner.  Haven't they suffered enough? */
        pthread_t ox = omonitor->_owner;
        if (ox == NO_THREAD) {      // if monitor has no owner
            ox = o_thread_cmpxchg(thisThread, &omonitor->_owner, NO_THREAD);
            if (ox == NO_THREAD) {
                /* The CAS succeeded, so the thread has ownership.
                 * Do some bookkeeping to exit spin state.
                 */
                if (sss && (omonitor->_succ == thisThread)) {   // sss == 1 (Knob_SpinSetSucc)
                    omonitor->_succ = NO_THREAD;
                }
                if (MaxSpin > 0) Adjust(&omonitor->_Spinner, -1);   // MaxSpin == -1, so never this

                /* Increase SpinDuration */
                int x = omonitor->_SpinDuration;
                if (x < Knob_SpinLimit) {
                    if (x < Knob_Poverty) x = Knob_Poverty;
                    omonitor->_SpinDuration = x + Knob_Bonus;
                }
                return 1;
            }

            /* The CAS failed. */
            prv = ox;       // ox is the current monitor owner.
            if (caspty == -2) break;        // caspty == Knob_CASPenalty == -1
            if (caspty == -1) goto Abort;   // caspty == Knob_CASPenalty == -1, so Abort
            ctr -= caspty;
            continue;
        }

        /* Did lock ownership change hands ? */
        if (ox != prv && prv != NO_THREAD) {
            if (oxpty == -2) break;
            if (oxpty == -1) goto Abort;
            ctr -= oxpty;       // oxpty == Knob_OXPenalty, so this increases ctr?
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
    if (MaxSpin >= 0) Adjust(&omonitor->_Spinner, -1);  // MaxSpin == -1, so never this
    if (sss && omonitor->_succ == thisThread) { // sss == 1
        omonitor->_succ = NO_THREAD;
        // Invariant: after setting succ=null a contending thread
        // must recheck-retry _owner before parking.  This usually happens
        // in the normal usage of TrySpin(), but it's safest
        // to make TrySpin() as foolproof as possible.
        OrderAccess_fence();
        if (jvmObjectMonitorTryLock(thisThread) > 0) return 1;
    }
    return 0;
}

/* This is called by the jvmObjectMonitorEnter() call to handle cases where
 * someone else already has ownership of ObjectMonitor.  The calling thread
 * will try to lock, try to spin, and if that gets to be too much, park
 * itself until told to wake up by another thread leaving the ObjectMonitor.
 */
void jvmObjectMonitorEnterI(pthread_t thisThread, const unsigned long threadnum) {

    struct ParkEvent * Self_ParkEvent = &park_events[threadnum];

    /* Try the lock ... again.*/
    if (jvmObjectMonitorTryLock(thisThread) > 0) {
        assert(omonitor->_succ != thisThread, "invariant");
        assert(omonitor->_owner == thisThread, "invariant");
        assert(omonitor->_Responsible != thisThread, "invariant");
        return;
    }

    /* Try one round of spinning before enqueing self... */
    if (jvmObjectMonitorTrySpin(thisThread) > 0) {
        assert(omonitor->_owner == thisThread, "invariant");
        assert(omonitor->_succ != thisThread, "invariant");
        assert(omonitor->_Responsible != thisThread, "invariant");
        return;
    }

    /* The Spin failed -- enqueue and park this thread. */
    assert(omonitor->_succ != thisThread, "invariant");
    assert(omonitor->_owner != thisThread, "invariant");
    assert(omonitor->_Responsible != thisThread, "invariant");

#ifdef MALLOC_OBJECTWAITER
    struct ObjectWaiter *node = initializeObjectWaiter(thisThread, Self_ParkEvent);
    node->_prev = (struct ObjectWaiter *) 0xBAD;
    node->TState = TS_CXQ;
#else
    // ObjectWaiter is a stack object.
    struct ObjectWaiter node_ __attribute__((aligned(64))) = {
        ._next = NULL,
        ._prev = (struct ObjectWaiter *) 0xBAD,
        ._thread = thisThread,
        .TState = TS_CXQ,
        ._event = Self_ParkEvent,
    };
    parkevent_init(Self_ParkEvent);
    struct ObjectWaiter * node = &node_;
#endif

    // push self onto _cxq
    struct ObjectWaiter *nxt;
    for (;;) {
        node->_next = nxt = omonitor->_cxq;
        if (o_ow_cmpxchg(node, &omonitor->_cxq, nxt) == nxt) break;

        // if here, CAS failed because _cxq changed

        if (jvmObjectMonitorTryLock(thisThread) > 0) {
            assert(omonitor->_succ != thisThread, "invariant");
            assert(omonitor->_owner == thisThread, "invariant");
            assert(omonitor->_Responsible != thisThread, "invariant");
            cleanupObjectWaiter(node);
            return;
        }
    }

    // insert thisThread as _Responsible if no thread currently is.
    if ((nxt == NULL) && (omonitor->_EntryList == NULL)) {
        o_thread_cmpxchg(thisThread, &omonitor->_Responsible, NO_THREAD);
    }

    int nWakeups = 0;
    int recheckInterval = 1;
    for (;;) {
        if (jvmObjectMonitorTryLock(thisThread) > 0) break;
        assert(omonitor->_owner != thisThread, "invariant");

        // park self
        if (omonitor->_Responsible == thisThread) {
            // the responsible thread uses the timed parking
            parkTimed(Self_ParkEvent, (long) recheckInterval);
            recheckInterval *= 8;
            if (recheckInterval > MAX_RECHECK_INTERVAL) {
                recheckInterval = MAX_RECHECK_INTERVAL;
            }
        } else {
            // other threads use the indefinite parking
            park(Self_ParkEvent);
        }

        // if here, thisThread has awakened
        if (jvmObjectMonitorTryLock(thisThread) > 0) break;
        ++nWakeups;
        if ((Knob_SpinAfterFutile & 1) &&                   // is 1
                (jvmObjectMonitorTrySpin(thisThread) > 0)) break;
        if ((Knob_ResetEvent & 1) && (Self_ParkEvent->_Event != 0)) { // never
            Self_ParkEvent->_Event = 0;       // XXX: Self->_ParkEvent->reset()
            OrderAccess_fence();
        }
        if (omonitor->_succ == thisThread) omonitor->_succ = NO_THREAD;
        OrderAccess_fence();  // after clearing _succ, a thread must retry _owner before parking.
    }

    /* Egress: Self has acquired the lock.  Note that we unlink
     * the node from the queue, then destroys it (since we own
     * the ObjectMonitor -- we don't need that ObjectWaiter anymore...
     */

    assert(omonitor->_owner == thisThread, "invariant");

    UnlinkAfterAcquire(thisThread, node);   // this unlink is supposed to update EntryList
    if (omonitor->_succ == thisThread) { omonitor->_succ = NO_THREAD; }

    assert(omonitor->_succ != thisThread, "invariant");

    if (omonitor->_Responsible == thisThread) {
        omonitor->_Responsible = NO_THREAD;
        OrderAccess_fence();
    }

    cleanupObjectWaiter(node);
}

static int jvmObjectMonitorExitSuspendEquivalent(pthread_t thisThread) {
    return 0;
}

/* This represents ObjectMonitor::enter() over in the JVM.  It basically
 * just tries to grab ownership of the ObjectMonitor.  If that fails, then
 * the thread tries to spin and see if it can grab ownership after a few
 * iterations.  After that, it calls jvmObjectMonitorEnterI() to try that
 * again and then queue itself to the list of threads waiting for ownership
 * and blocks until woken up. So the only way out of here is to actually
 * get ownership of the ObjectMonitor itself.
 */
unsigned long jvmObjectMonitorEnter(pthread_t thisThread, const unsigned long threadnum) {
    pthread_t cur = o_thread_cmpxchg(thisThread, &omonitor->_owner, NO_THREAD);
    if (cur == NO_THREAD) {
        assert(omonitor->_owner == thisThread, "invariant");
        return 1;
    }

    // the "Try one round of spinning *before* enqueuing self and before
    // going through the awkward and expensive state transitions."
    if (Knob_SpinEarly && jvmObjectMonitorTrySpin(thisThread) > 0) {    // Knob_SpinEarly == 1
        assert(omonitor->_owner == thisThread, "invariant");
        return 1;
    }

    assert(omonitor->_owner != thisThread, "invariant");
    assert(omonitor->_succ != thisThread, "invariant");
    assert(omonitor->_count >= 0, "invariant");

    atomic_int_inc(&omonitor->_count);

    {
        for (;;) {
            jvmObjectMonitorEnterI(thisThread, threadnum);
            if (!jvmObjectMonitorExitSuspendEquivalent(thisThread)) break;
        }
    }

    int depth = atomic_int_dec(&omonitor->_count);

    assert(omonitor->_count >= 0, "invariant");
    assert(omonitor->_owner == thisThread, "invariant");
    assert(omonitor->_succ != thisThread, "invariant");

    return depth;   // XXX: maybe this should be depth + 1?
}

/* Final cleanup code when a thread's releasing the ObjectMonitor.
 * It'll set the successor thread, release ownership of the ObjectMonitor,
 * then unpark the successor thread.
 */
void jvmObjectMonitorExitEpilog(pthread_t thisThread,
        struct ObjectWaiter* Wakee) {
    assert(omonitor->_owner == thisThread, "invariant");
    omonitor->_succ = (Knob_SuccEnabled ? Wakee->_thread : NO_THREAD);  // Knob_SuccEnabled = 1
    struct ParkEvent * Trigger = Wakee->_event;

    Wakee = NULL;       // hygiene
    release_store_thread(&omonitor->_owner, NO_THREAD); // drop the lock
    OrderAccess_fence();  // ST _owner vs LD in unpark()

    // In the original, unpark() is not called on Wakee because at this point
    // the thread is no longer the monitor owner.  Instead, Trigger->unpark()
    // is called using a retained pointer (Trigger) to its ParkEvent.
    unpark(Trigger);
}

/* Called when the owning thread's ready to release the ObjectMonitor.
 * If there are no outstanding ObjectWaiters hanging in the queues, it'll
 * just return once it's cleaned up.  If there are, it'll do the
 * post-run maintenance on those queues and call jvmObjectMonitorExitEpilog()
 * to wake that next thread up.
 */
void jvmObjectMonitorExit(const pthread_t thisThread, const unsigned long threadnum) {

    if (thisThread != omonitor->_owner) {
        omonitor->_owner = thisThread;
        /* Removed code having to do with recursions -- not tracking that. */
    }

    omonitor->_Responsible = NO_THREAD;

    for (;;) {
        assert(omonitor->_owner == thisThread);

        if (Knob_ExitPolicy == 0) { // INFO: this is always true
            release_store_thread(&omonitor->_owner, NO_THREAD);  // drop the lock
            storeload();

            // See if we need to wake a successor
            if ((((intptr_t)omonitor->_EntryList | (intptr_t)omonitor->_cxq) == 0) ||
                    (omonitor->_succ != NO_THREAD)) {
                return;
            }

            // reacquire the lock so that entrylist or cxq can be manipulated.
            if (o_thread_cmpxchg(thisThread, &omonitor->_owner, NO_THREAD) != NO_THREAD) {
                // if here, the monitor got owned by someone else just now.
                return;
            }

            // if here, then the monitor has been reacquired
        } else {
            //assert(0);  // this basic block should never be called in this benchmark
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

        guarantee(omonitor->_owner == thisThread, "invariant");

        struct ObjectWaiter *w = NULL;
        int QMode = Knob_QMode;
        (void) QMode;

        /* Bunch of experimental stuff handling various QMode settings
         * removed for simplification -- never gets executed here...
         */

        w = omonitor->_EntryList;
        if (w != NULL) {
            assert(w->TState == TS_ENTER, "invariant");
            jvmObjectMonitorExitEpilog(thisThread, w);
            return;
        }

        w = omonitor->_cxq;
        if (w == NULL) continue;

        for (;;) {
            assert(w != NULL, "Invariant");
            // u = *omonitor->_cxq; if *omonitor->_cxq == w, insert NULL.
            struct ObjectWaiter *u = o_ow_cmpxchg(NULL, &omonitor->_cxq, w);
            if (u == w) break;
            w = u;
        }

        assert(w != NULL, "invariant");
        assert(omonitor->_EntryList == NULL, "invariant");

        /* Was a check here for QMode == 1, but what follows is QMode == 0 */
        omonitor->_EntryList = w;
        struct ObjectWaiter *q = NULL;
        struct ObjectWaiter *p;
        for (p = w; p != NULL; p = p->_next) {
            guarantee(p->TState == TS_CXQ, "Invariant");
            p->TState = TS_ENTER;
            p->_prev = q;
            q = p;
        }

        if (omonitor->_succ != NO_THREAD) continue;

        // if here, there was no successor.
        w = omonitor->_EntryList;
        if (w != NULL) {
            guarantee(w->TState == TS_ENTER, "invariant");
            jvmObjectMonitorExitEpilog(thisThread, w);
            return;
        }
    }
}

// -------------------------------

/* Called once per measurement to initialize the ObjectMonitor and ParkEvents. */
void jvm_init_locks(uint64_t *lock, unsigned long num_threads) {

//#define PRINT_STRUCT_LAYOUTS
#ifdef PRINT_STRUCT_LAYOUTS

    printf("\n");

#define OOSO_(f, g) \
    printf("%30s :  offsetof %-3zu  sizeof %-3zu\n", \
        stringify(g), \
        offsetof(typeof(*(f)), g), \
        sizeof(f->g))

#define PE_OOSO(g) \
    OOSO_(((struct ParkEvent *)0), g)

    printf("ParkEvent:\n");
    PE_OOSO(CachePad);
    PE_OOSO(_Event);
    PE_OOSO(_nParked);
    PE_OOSO(_mutex);

    PE_OOSO(_mutex[0].__data.__lock);
    PE_OOSO(_mutex[0].__data.__count);
    PE_OOSO(_mutex[0].__data.__owner);
    PE_OOSO(_mutex[0].__data.__nusers);
    PE_OOSO(_mutex[0].__data.__kind);
    PE_OOSO(_mutex[0].__data.__spins);
#ifdef __x86_64__
    PE_OOSO(_mutex[0].__data.__elision);
#endif
    PE_OOSO(_mutex[0].__data.__list);
    PE_OOSO(_mutex[0].__data);
    PE_OOSO(_mutex[0].__size);
    PE_OOSO(_mutex[0].__align);

    PE_OOSO(_cond);

    PE_OOSO(_cond[0].__data.__wseq);
    PE_OOSO(_cond[0].__data.__g1_start);
    PE_OOSO(_cond[0].__data.__g_refs);
    PE_OOSO(_cond[0].__data.__g_size);
    PE_OOSO(_cond[0].__data.__g1_orig_size);
    PE_OOSO(_cond[0].__data.__wrefs);
    PE_OOSO(_cond[0].__data.__g_signals);
    PE_OOSO(_cond[0].__size);
    PE_OOSO(_cond[0].__align);

    PE_OOSO(PostPad);
    PE_OOSO(_Assoc);
    PE_OOSO(FreeNext);
    PE_OOSO(AssociatedWith);
    PE_OOSO(ListNext);
    PE_OOSO(OnList);
    PE_OOSO(TState);
    PE_OOSO(Notified);
    printf("total = %zu\n", sizeof(struct ParkEvent));
    printf("\n");


#define OM_OOSO(g) \
    OOSO_(((struct ObjectMonitor *)0), g)

    printf("ObjectMonitor:\n");
    OM_OOSO(_owner);
    OM_OOSO(_EntryList);
    OM_OOSO(_cxq);
    OM_OOSO(_cxq);
    OM_OOSO(_succ);
    OM_OOSO(_Responsible);
    OM_OOSO(_Spinner);
    OM_OOSO(_SpinDuration);
    OM_OOSO(_count);
    printf("total = %zu\n", sizeof(struct ObjectMonitor));
    printf("\n");

#define OW_OOSO(g) \
    OOSO_(((struct ObjectWaiter *)0), g)

    printf("ObjectWaiter:\n");
    OW_OOSO(_next);
    OW_OOSO(_prev);
    OW_OOSO(_thread);
    OW_OOSO(_notifier_tid);
    OW_OOSO(_event);
    OW_OOSO(_notified);
    OW_OOSO(TState);
    OW_OOSO(_Sorted);
    OW_OOSO(_active);
    printf("total = %zu\n", sizeof(struct ObjectWaiter));

#endif

    // free/make/remake an array of ParkEvent and initialize them.
    // TODO: allocate park_events using the framework lock pointer. For now, malloc/free it.
    if (park_events) {
        for (size_t i = 0; i < park_events_length; i++) {
            parkevent_destroy(&park_events[i]);
        }
        free(park_events);
        park_events = NULL;
        park_events_length = 0;
    }

    if (park_events == NULL) {
        // ParkEvent are allocated on 256-byte alignment.
        if (posix_memalign((void *) &park_events, 256, num_threads * sizeof(struct ParkEvent))) {
            fprintf(stderr, "jvm_objectmonitor posix_memalign failed allocating park_events array\n");
            exit(-1);
        }

        for (size_t i = 0; i < num_threads; i++) {
            parkevent_init(&(park_events[i]));
        }

        park_events_length = num_threads;
    }

    if (!omonitor) {
#ifdef USE_LOCK_POINTER
        // first iter: set it to the lock pointer from test harness
        omonitor = (struct ObjectMonitor *) lock;
#else
        // first iter: align the omonitor so that it fits into a 64 byte cache line.
        if (posix_memalign((void *) &omonitor, 64, sizeof(struct ObjectMonitor))) {
            fprintf(stderr, "jvm_objectmonitor posix_memalign failed allocating struct ObjectMonitor\n");
            exit(-1);
        }
#endif
    } else {
        // second iter:  already have a pointer to the ObjectMonitor

#ifdef USE_LOCK_POINTER
        // on subsequent iterations, omonitor should already be set to the lock pointer.
        if (omonitor != (struct ObjectMonitor *) lock) {
            fprintf(stderr, "jvm_objectmonitor jvm_init_locks: omonitor != lock pointer!  omonitor = %p, expected %p\n", omonitor, lock);
            exit(-1);
        }
#endif

#ifdef MALLOC_OBJECTWAITER
        // Free the ObjectWaiter lists, if any.  Not expecting there to be any.

        struct ObjectWaiter *node = omonitor->_EntryList;
        struct ObjectWaiter *next;
        size_t num_freed = 0;

        while (node) {
            next = node->_next;
            free(node);
            node = next;
            num_freed++;
        }

        if (num_freed) {
            fprintf(stderr, "WARNING: jvm_init_locks had to free %zu nodes from omonitor->_EntryList\n", num_freed);
        }

        num_freed = 0;
        node = omonitor->_cxq;
        while (node) {
            next = node->_next;
            free(node);
            node = next;
            num_freed++;
        }

        if (num_freed) {
            fprintf(stderr, "WARNING: jvm_init_locks had to free %zu nodes from omonitor->_cxq\n", num_freed);
        }
#endif
    }

    // initialize the object monitor
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
        const unsigned long threadnum) {
    return jvmObjectMonitorEnter(pthread_self(), threadnum);
}

/* Unlock interface back to the framework. */
static inline void lock_release(uint64_t *lock,
        const unsigned long threadnum) {
    jvmObjectMonitorExit(pthread_self(), threadnum);
}

#endif

/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab : */
