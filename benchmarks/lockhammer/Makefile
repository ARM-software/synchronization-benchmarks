CFLAGS=-O2 -I. -I./include -I../../ext/mysql/include -I../../ext/linux/include
# LSE support is experimental, please enable the below CFLAGS with caution
# CFLAGS=-O2 -march=armv8-a+lse -DUSE_LSE -I. -I./include -I../../ext/mysql/include -I../../ext/linux/include

lh_%: tests/%.h include/atomics.h src/lockhammer.c 
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o $@ -lpthread

all: \
	lh_swap_mutex \
	lh_event_mutex \
	lh_cas_event_mutex \
	lh_cas_lockref \
	lh_cas_rw_lock \
	lh_incdec_refcount \
	lh_ticket_spinlock \
	lh_hybrid_spinlock \
	lh_hybrid_spinlock_fastdequeue \
	lh_queued_spinlock \
	lh_empty \
	lh_jvm_objectmonitor

lh_event_mutex: ../../ext/mysql/event_mutex.h include/atomics.h ../../ext/mysql/include/ut_atomics.h src/lockhammer.c
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o $@ -lpthread

lh_cas_event_mutex: ../../ext/mysql/cas_event_mutex.h include/atomics.h ../../ext/mysql/include/ut_atomics.h src/lockhammer.c
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o $@ -lpthread

lh_ticket_spinlock: ../../ext/linux/ticket_spinlock.h include/atomics.h ../../ext/linux/include/lk_atomics.h src/lockhammer.c
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o $@ -lpthread

lh_hybrid_spinlock: ../../ext/linux/hybrid_spinlock.h include/atomics.h ../../ext/linux/include/lk_atomics.h src/lockhammer.c
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o $@ -lpthread

lh_hybrid_spinlock_fastdequeue: ../../ext/linux/hybrid_spinlock_fastdequeue.h include/atomics.h ../../ext/linux/include/lk_atomics.h src/lockhammer.c
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o $@ -lpthread

lh_queued_spinlock: ../../ext/linux/queued_spinlock.h include/atomics.h ../../ext/linux/include/lk_atomics.h src/lockhammer.c
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o $@ -lpthread

lh_jvm_objectmonitor: ../../ext/jvm/jvm_objectmonitor.h include/atomics.h src/lockhammer.c
	${CC} ${CFLAGS} -DATOMIC_TEST=\"$<\" src/lockhammer.c -o $@ -lpthread

clean:
	rm -f lh_*
