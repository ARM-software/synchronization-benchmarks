override CFLAGS += -g -O3 -I. -I./include -I../../ext/mysql/include -I../../ext/linux/include -I../../ext/tbb/include

ifneq ($(DEBUG_LEVEL),)
ifeq ($(shell test $(DEBUG_LEVEL) -gt 0; echo $$?),0)
CFLAGS+=-DDEBUG
endif
ifeq ($(shell test $(DEBUG_LEVEL) -gt 1; echo $$?),0)
CFLAGS+=-DDDEBUG
endif
endif
# LSE support is experimental, please enable the below CFLAGS with caution
ifneq ($(LSE_ENABLE),)
CFLAGS+=-march=armv8-a+lse -DUSE_LSE
endif


LDFLAGS=-lpthread -lm

lh_%: tests/%.h include/atomics.h src/lockhammer.c 
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o build/$@ ${LDFLAGS}

TARGET_ARCH:=$(shell ${CC} -dumpmachine | cut -d '-' -f 1)

TEST_TARGETS=lh_swap_mutex \
	lh_event_mutex \
	lh_cas_event_mutex \
	lh_cas_lockref \
	lh_cas_rw_lock \
	lh_incdec_refcount \
	lh_ticket_spinlock \
	lh_queued_spinlock \
	lh_empty \
	lh_jvm_objectmonitor \
	lh_tbb_spin_rw_mutex \
	lh_osq_lock

ifeq ($(TARGET_ARCH),aarch64)
	TEST_TARGETS+=lh_hybrid_spinlock \
		lh_hybrid_spinlock_fastdequeue
endif

all: ${TEST_TARGETS}

lh_event_mutex: ../../ext/mysql/event_mutex.h include/atomics.h ../../ext/mysql/include/ut_atomics.h src/lockhammer.c
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o build/$@ ${LDFLAGS}

lh_cas_event_mutex: ../../ext/mysql/cas_event_mutex.h include/atomics.h ../../ext/mysql/include/ut_atomics.h src/lockhammer.c
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o build/$@ ${LDFLAGS}

lh_ticket_spinlock: ../../ext/linux/ticket_spinlock.h include/atomics.h ../../ext/linux/include/lk_atomics.h src/lockhammer.c
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o build/$@ ${LDFLAGS}

lh_hybrid_spinlock: ../../ext/linux/hybrid_spinlock.h include/atomics.h ../../ext/linux/include/lk_atomics.h src/lockhammer.c
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o build/$@ ${LDFLAGS}

lh_hybrid_spinlock_fastdequeue: ../../ext/linux/hybrid_spinlock_fastdequeue.h include/atomics.h ../../ext/linux/include/lk_atomics.h src/lockhammer.c
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o build/$@ ${LDFLAGS}

lh_osq_lock: ../../ext/linux/osq_lock.h ../../ext/linux/include/lk_atomics.h ../../ext/linux/include/lk_barrier.h ../../ext/linux/include/lk_cmpxchg.h include/atomics.h src/lockhammer.c
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o build/$@ ${LDFLAGS}

lh_queued_spinlock: ../../ext/linux/queued_spinlock.h include/atomics.h ../../ext/linux/include/lk_atomics.h src/lockhammer.c
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o build/$@ ${LDFLAGS}

lh_jvm_objectmonitor: ../../ext/jvm/jvm_objectmonitor.h include/atomics.h src/lockhammer.c
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o build/$@ ${LDFLAGS}

lh_tbb_spin_rw_mutex: ../../ext/tbb/tbb_spin_rw_mutex.h ../../ext/tbb/include/tbb.h include/atomics.h src/lockhammer.c
	${CC} ${CFLAGS} -DNDEBUG -DATOMIC_TEST=\"$<\" src/lockhammer.c -o build/$@ ${LDFLAGS}

clean:
	rm -f build/lh_*
