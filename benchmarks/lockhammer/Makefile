
# SPDX-FileCopyrightText: Copyright 2019-2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: BSD-3-Clause

#
# This Makefile builds lockhammer binaries by test and configuration.
#
# TLDR:
#      make -j 8 allvariants
#
#
# The list of tests is in the TEST_TARGETS variable defined below.
#
# Additional variables passed on the command line control
# build-time variants, such as use of LSE, compiler builtin
# intrinsics, etc.  Executables for each variant combination are
# placed into a separate build.xyz subdirectory, where xyz is
# named for the build-time variant combination.
#
# The following phony targets provide convenience for building binaries.
#
# make all [variant variables]
#     - builds binaries for all tests for the configuration variant
#
# make alli [variant variables]
#     - generates preprocessed .i for all tests of the configuration variant of the set of variables
#
# make clean [variant variables]
#     - remove the executables, binaries, and .i from the build directory of the variant
#
# make clobber [variant variables]
#     - remove all files and the directory of the variant
#
# make allvariants
#     - builds all binaries for all variant combinations
#
# make allivariants
#     - generates preprocessed .i for all variant combinations
#
# make cleanallvariants
#     - removes executables/binaries and .i of all variant builds
#
# make clobberallvariants
#     - removes all files and directories for all variants
#
#
# build variant variables
# -----------------------
#
# USE_LSE=0/1       // aarch64 only
#     0: do not use aarch64 LSE instructions (default if variable is not specified)
#     1: use the aarch64 LSE instructions
#
#     The output for each setting will be placed in a separate build directory.
#
# USE_BUILTIN=0/1
#     0: do not use the __atomic intrinsics and use arch-specific inline
#        assembly instructions (default if variable is not specified)
#     1: use the builtin __atomic intrinsics
#
#     The output for each setting will be placed in a separate build directory.
#
# USE_RELAX=setting
#     what to use in cpu_relax() and equivalents -- value must be lowercase and one of the following
#
#     (unspecified)  'pause' instruction on x86_64, 'nothing' on aarch64
#     pause          inline asm pause (x86_64 only)
#     isb            inline asm isb (aarch64 only)
#     nop            inline asm nop
#     empty          inline asm volatile statement with no instruction
#     nothing        no inline asm statement at all
#
#     The output for each setting will be placed in a separate build directory.
#
# USE_SMP_COND_LOAD_RELAXED=n	// effective on aarch64 only
#     0: in osq_lock(), use a while loop to poll the lock variable. (default if variable is not specified)
#     1: in osq_lock(), use the smp_cond_load_relaxed() macro instead polling between CPU_relax()
#     Do a make clean if this make parameter is changed.
#
#     The output for each setting will be placed in a separate build directory.
#
# NO_JSON=n
#     0: build with JSON output using libjansson (default if variable is not specified)
#     1: do not build with libjansson support for JSON output.
#     Do a make clean if this make parameter is changed.
#
# STATIC=n
#     0: link dynamic binary. (default if variable is not specified)
#     1: link static binary.
#     Do a make clean if this make parameter is changed.
#
# SANITIZE=n
#     0: do not use -fsanitize=address.  (default if variable is not specified)
#     1: use -fsanitize=address.  Can not combine with STATIC=1.
#     Do a make clean if this make parameter is changed.
#
# Examples:
#
# make USE_BUILTIN=1 USE_LSE=1 all
#    -> makes binaries of all tests and places them in build.lse.builtin
#
# make USE_RELAX=empty build.relax_empty/measure.lh_ticket_spinlock.i
#    -> outputs the preprocessed measure.c for ticket spinlock using the 'empty' relaxation method
#
#
# Other variables
# ---------------
#
# V=1
#     show the executed command lines, e.g. make V=1
#

ifeq ($(V),1)
empty :=
hide = $(empty)
else
hide = @
endif

# can use 'make CC=clang LD=clang' as well
LD = gcc
CC = gcc

TARGET_ARCH = $(shell $(CC) -dumpmachine | cut -d '-' -f 1)

# executable targets by lock type

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
	lh_osq_lock \
	lh_clh_spinlock \
	lh_tbb_spin_rw_mutex

CURRENTLY_BROKEN_TARGETS=

ifeq ($(TARGET_ARCH),aarch64)
	TEST_TARGETS+=lh_hybrid_spinlock \
		lh_hybrid_spinlock_fastdequeue
endif

# uncomment to redefine TEST_TARGETS to build just one lock test type
#TEST_TARGETS=lh_osq_lock
#TEST_TARGETS=lh_empty
#TEST_TARGETS=lh_empty lh_osq_lock
#TEST_TARGETS=lh_tbb_spin_rw_mutex

# debugging directive to show the make target
#$(info MAKECMDGOALS=$(MAKECMDGOALS))


# header files' search paths
INCLUDE = -I. -Iinclude -I../../ext/pagemap/include
INCLUDE_MYSQL = -I ../../ext/mysql/include
INCLUDE_LINUX = -I ../../ext/linux/include
INCLUDE_SMS = -I ../../ext/sms/base
INCLUDE_JVM = -I ../../ext/jvm/include
INCLUDE_TBB = -I ../../ext/tbb/include


LDFLAGS = -pthread -lm -g

ifeq ($(STATIC),1)
LDFLAGS += -static
endif

CFLAGS = -g -O2 -fno-lto -Wall $(INCLUDE) -fno-stack-protector
ifeq ($(CC),gcc)
CFLAGS += -fstack-protector-explicit
endif

# for making USE_BUILTIN=1 not call the out-of-line functions for aarch64 atomics
#CFLAGS += -mno-outline-atomics

# set NDEBUG to disable assert() expression evaluation
CFLAGS+=-DNDEBUG

# Debug level
ifneq ($(DEBUG_LEVEL),)
ifeq ($(shell test $(DEBUG_LEVEL) -ge 1; echo $$?),0)
CFLAGS+=-DDEBUG
endif

ifeq ($(shell test $(DEBUG_LEVEL) -ge 2; echo $$?),0)
CFLAGS+=-DDDEBUG
CFLAGS+=-Wuse-after-free=3
endif
endif

# address sanitizer flags
ifeq ($(SANITIZE),1)
CFLAGS+=-fsanitize=address -fno-omit-frame-pointer
LDFLAGS+=-fsanitize=address
endif

# For "--json results.json" to save results using libjansson.
# Build with NO_JSON=1 to disable, or install libjanssson-dev package.

ifeq ($(NO_JSON),1)
$(info NO_JSON=1, will not build with JSON output support)
else
CFLAGS+=-DJSON_OUTPUT
LDFLAGS+=-ljansson
endif

LOCK_CFLAGS=

# On arm, use the Large System Extensions instructions (FEAT_LSE) instead of exclusives.
ifeq ($(USE_LSE),1)
LOCK_CFLAGS+=-march=armv8-a+lse -DUSE_LSE
endif

# If USE_BUILTIN=1, use the __atomic intrinsics instead of arch-specific instructions
ifeq ($(USE_BUILTIN),1)
LOCK_CFLAGS+=-DUSE_BUILTIN
endif


# cpu_relax defaults
# ------------------

ifndef USE_RELAX
ifeq ($(TARGET_ARCH),aarch64)
$(info USE_RELAX is not defined, will use 'nothing' (no asm statement/instruction))
USE_RELAX = nothing
else ifeq ($(TARGET_ARCH),x86_64)
$(info USE_RELAX is not defined, will use 'pause' for the 'pause' instruction)
USE_RELAX = pause
endif
endif


# cpu_relax implementation variants on x86_64
# -------------------------------------------

ifeq ($(TARGET_ARCH),x86_64)

# Use the PAUSE instruction
ifeq ($(USE_RELAX),pause)
LOCK_CFLAGS+=-DRELAX_IS_PAUSE

# Use an empty but volatile asm() statement
else ifeq ($(USE_RELAX),empty)
LOCK_CFLAGS+=-DRELAX_IS_EMPTY

# Use no asm() statement at all
else ifeq ($(USE_RELAX),nothing)
LOCK_CFLAGS+=-DRELAX_IS_NOTHING

endif
endif

# cpu_relax implementation variants on aarch64
# --------------------------------------------

ifeq ($(TARGET_ARCH),aarch64)

# Use the ISB instruction
ifeq ($(USE_RELAX),isb)
LOCK_CFLAGS+=-DRELAX_IS_ISB

# Use the NOP instruction
else ifeq ($(USE_RELAX),nop)
LOCK_CFLAGS+=-DRELAX_IS_NOP

# Use an empty but volatile asm() statement
else ifeq ($(USE_RELAX),empty)
LOCK_CFLAGS+=-DRELAX_IS_EMPTY

# Use no asm() statement at all
else ifeq ($(USE_RELAX),nothing)
LOCK_CFLAGS+=-DRELAX_IS_NOTHING

endif
endif


# osq_lock - USE_SMP_COND_LOAD_RELAXED for primary loop
# -----------------------------------------------------

ifeq ($(USE_SMP_COND_LOAD_RELAXED),1)
LOCK_CFLAGS+=-DUSE_SMP_COND_LOAD_RELAXED
endif



# ========================================================
# output directory name construction

VARIANT_NAME=

ifeq ($(USE_LSE),1)
VARIANT_NAME:=$(VARIANT_NAME).lse
endif

ifeq ($(USE_BUILTIN),1)
VARIANT_NAME:=$(VARIANT_NAME).builtin
endif

ifeq ($(USE_SMP_COND_LOAD_RELAXED),1)
VARIANT_NAME:=$(VARIANT_NAME).cond_load
endif

#ifeq ($(USE_RELAX),testme)
#$(info lc USE_RELAX=$(USE_RELAX))
#else ifeq ($(USE_RELAX),TESTME)
#$(info uc USE_RELAX=$(USE_RELAX))
#endif

ifeq ($(USE_RELAX),isb)
VARIANT_NAME:=$(VARIANT_NAME).relax_isb
else ifeq ($(USE_RELAX),nop)
VARIANT_NAME:=$(VARIANT_NAME).relax_nop
else ifeq ($(USE_RELAX),pause)
VARIANT_NAME:=$(VARIANT_NAME).relax_pause
else ifeq ($(USE_RELAX),empty)
VARIANT_NAME:=$(VARIANT_NAME).relax_empty
else ifeq ($(USE_RELAX),nothing)
VARIANT_NAME:=$(VARIANT_NAME).relax_nothing
endif

# strip the leading . in VARIANT_NAME, if any
space:=$(subst ,, )
VARIANT_NAME:=$(subst ., ,$(VARIANT_NAME))
VARIANT_NAME:=$(strip $(VARIANT_NAME))
VARIANT_NAME:=$(subst $(space),.,$(VARIANT_NAME))

BUILD_DIR=build.$(VARIANT_NAME)
LOCK_CFLAGS+=-DVARIANT_NAME=$(VARIANT_NAME)

# ========================================================
# build variant enumerations

BUILTIN_VARIANTS = 0 1

LSE_VARIANTS = 0
ifeq ($(TARGET_ARCH),aarch64)
LSE_VARIANTS += 1
endif

SMP_COND_LOAD_VARIANTS = 0 1

ifeq ($(TARGET_ARCH),aarch64)
RELAX_VARIANTS = isb nop empty nothing
else
RELAX_VARIANTS = empty pause nothing
endif

# compute all variant permutations
ALL_VARIANTS = \
	$(foreach BUILTIN,$(BUILTIN_VARIANTS), \
		$(foreach LSE,$(LSE_VARIANTS), \
			$(foreach SMP_COND_LOAD,$(SMP_COND_LOAD_VARIANTS), \
				$(foreach RELAX,$(RELAX_VARIANTS), \
					builtin_$(BUILTIN).lse_$(LSE).cond_load_$(SMP_COND_LOAD).relax_$(RELAX)))))

# ---------------------
# phony targets

BUILD_ALL_VARIANTS = $(addprefix all.,$(ALL_VARIANTS))
BUILD_ALLI_VARIANTS = $(addprefix alli.,$(ALL_VARIANTS))
CLEAN_ALL_VARIANTS = $(addprefix clean.,$(ALL_VARIANTS))
CLOBBER_ALL_VARIANTS = $(addprefix clobber.,$(ALL_VARIANTS))

ALL_PHONY_VARIANTS = $(BUILD_ALL_VARIANTS) $(BUILD_ALLI_VARIANTS) $(CLEAN_ALL_VARIANTS) $(CLOBBER_ALL_VARIANTS)
ALL_PHONY_TARGETS = $(ALL_PHONY_VARIANTS) $(ALL_CLEAN_TARGETS) info all allvariants allivariants Makefile info_variants
ALL_CLEAN_TARGETS = $(CLEAN_ALL_VARIANTS) $(CLOBBER_ALL_VARIANTS) clean clobber cleanallvariants clobberallvariants

# ---------------------
# real target list

ALL_BINARIES = $(addprefix $(BUILD_DIR)/,$(TEST_TARGETS))

ALL_OBJS = $(HARNESS_OBJS) $(LOCK_OBJS)
ALL_DEPS = $(HARNESS_DEPS) $(LOCK_DEPS)
ALL_I = $(HARNESS_I) $(LOCK_I)

# ---------------------
# source file lists

MEASURE_C_DEP_HEADERS = include/alloc.h include/lockhammer.h include/atomics.h include/perf_timer.h

HARNESS_C_SRC = src/lockhammer.c src/alloc.c src/args.c src/report.c src/cpufreq-scaling-detect.c

# lock code is inlined into measure.c using the -DATOMIC_TEST variable passed in as a compiler flag.
# TODO: separate actual lock code from measure.c; also, LOCK_C_SRC must be just 1 file
LOCK_C_SRC = src/measure.c

C_SRC = $(HARNESS_C_SRC) $(LOCK_C_SRC)

# ---------------------
# compute the object names

HARNESS_OBJS = $(addprefix $(BUILD_DIR)/,$(notdir $(HARNESS_C_SRC:%.c=%.o)))
HARNESS_DEPS = $(addprefix $(BUILD_DIR)/,$(notdir $(HARNESS_C_SRC:%.c=%.d)))
HARNESS_I = $(addprefix $(BUILD_DIR)/,$(notdir $(HARNESS_C_SRC:%.c=%.i)))

LOCK_BASENAME = $(basename $(notdir $(LOCK_C_SRC)))
LOCK_OBJS = $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(addprefix $(LOCK_BASENAME).,$(TEST_TARGETS))))
LOCK_DEPS = $(addprefix $(BUILD_DIR)/,$(addsuffix .d,$(addprefix $(LOCK_BASENAME).,$(TEST_TARGETS))))
LOCK_I = $(addprefix $(BUILD_DIR)/,$(addsuffix .i,$(addprefix $(LOCK_BASENAME).,$(TEST_TARGETS))))


# ---------------------
# info for debugging make

$(info BUILD_DIR = $(BUILD_DIR))                  # output build-dir
#$(info HARNESS_OBJS = $(HARNESS_OBJS))           # benchmark harness object files
#$(info LOCK_OBJS = $(LOCK_OBJS))                 # lock measurement object files
#$(info HARNESS_DEPS = $(HARNESS_DEPS))           # benchmark harness dep files
#$(info LOCK_DEPS = $(LOCK_DEPS))                 # lock measurement dep files
#$(info $(BUILD_DIR)/$(notdir $(C_SRC:.c=.d)))

# ---------------------
# target expansion/selection functions
# these save on the number of explicit recipes for the different variations

# vartargs:  create build*, clean*, clobber* targets from 1st parameter.
# e.g. $(call vartargs,builtin_0.lse_0) ->  build.builtin_0.lse_0 clean.builtin_0.lse_0 clobber.builtin_0.lse_0

vartargs = $(addsuffix $1, all. alli. clean. clobber.)

# targvar:  extract the first word from the argument.  It will be used for the nested make target.
# e.g. $(call vartargs,all.builtin_0.lse_0) ---> all, for use as "make all"

targvar = $(word 1, $(subst ., ,$1))

#$(info call targvar,clean.builtin_0.lse_0 ---> $(call targvar,clean.builtin_0.lse_0))
#$(info call targvar,clobber.builtin_0.lse_0 ---> $(call targvar,clobber.builtin_0.lse_0))
#$(info call targvar,all.builtin_0.lse_0 ---> $(call targvar,all.builtin_0.lse_0))




# --------------------------------------------------------
# target recipes begin here

.PHONY: $(ALL_PHONY_TARGETS)

#info:  # this "info" target is used to show the values of variables and not actually build anything
#	$(info $(ALL_VARIANTS))
#
#info_variants:
#	$(info $(ALL_VARIANTS))

all: $(ALL_BINARIES)

alli: $(ALL_I)

clean:
	@echo [CLEAN] all binaries, objects, preprocessed files in $(BUILD_DIR)
	$(hide) rm -f $(ALL_BINARIES) $(ALL_OBJS) $(ALL_I)

clobber: clean
	@echo [CLOBBER] remove all dep files in $(BUILD_DIR) and the directory itself
	$(hide) rm -f $(ALL_DEPS)
	$(hide) rm -rf $(BUILD_DIR)

allvariants: $(BUILD_ALL_VARIANTS)
#	$(info BUILD_ALL_VARIANTS=$(BUILD_ALL_VARIANTS))

allivariants: $(BUILD_ALLI_VARIANTS)
#	$(info BUILD_ALLI_VARIANTS=$(BUILD_ALLI_VARIANTS))

cleanallvariants: $(CLEAN_ALL_VARIANTS)

clobberallvariants: $(CLOBBER_ALL_VARIANTS)

# Explanation: if the goal is one of the clean/clobber targets,
# then the clean/clobber target will precipiate from filter,
# then this ifeq will fail, so the dep files will not be included,
# so they will not be rebuilt.  This prevents make clean/clobber
# from rebuilding the dep files.

ifeq ($(filter $(ALL_CLEAN_TARGETS),$(MAKECMDGOALS)),)
include $(HARNESS_DEPS)
include $(LOCK_DEPS)
endif

# --------------------------------------------------------
# variant-build targets

# This template and the following loop generate recipes for the variant-build phony targets.
# Each generated recipe recursively calls make with the variables for that variant-build.
# e.g., this command
#	make alli.builtin_1.lse_0.cond_load_1.relax_isb
# runs this command in turn:
#	make alli USE_BUILTIN=1 USE_LSE=0 USE_SMP_COND_LOAD_RELAXED=1 USE_RELAX=isb

define TEMPLATE
#$$(info $$(call vartargs,builtin_$(1).lse_$(2).cond_load_$(3).relax_$(4)))
$$(call vartargs,builtin_$(1).lse_$(2).cond_load_$(3).relax_$(4)):
	@echo $$(MAKE) $$(call targvar,$$@) USE_BUILTIN=$(1) USE_LSE=$(2) USE_SMP_COND_LOAD_RELAXED=$(3) USE_RELAX=$(4)
	     @$$(MAKE) $$(call targvar,$$@) USE_BUILTIN=$(1) USE_LSE=$(2) USE_SMP_COND_LOAD_RELAXED=$(3) USE_RELAX=$(4)
endef

$(foreach BUILTIN,$(BUILTIN_VARIANTS), \
	$(foreach LSE,$(LSE_VARIANTS), \
		$(foreach SMP_COND_LOAD,$(SMP_COND_LOAD_VARIANTS), \
			$(foreach RELAX,$(RELAX_VARIANTS), \
				$(eval $(call TEMPLATE,$(BUILTIN),$(LSE),$(SMP_COND_LOAD),$(RELAX)) \
				)))))


# make output dir for this test variant
$(BUILD_DIR):
	$(hide) mkdir -p $@




# ----------------------------------------------------------------------
# recipes for building measure object files of specific locks


# measure objects (.o) for specific external locks, i.e. $(BUILD_DIR)/measure.$lock_name.o

$(BUILD_DIR)/measure.lh_event_mutex.o: ../../ext/mysql/event_mutex.h | $(BUILD_DIR)
	@echo [CC -c] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=event_mutex src/measure.c -c -o $@ $(INCLUDE_MYSQL)

$(BUILD_DIR)/measure.lh_cas_event_mutex.o: ../../ext/mysql/cas_event_mutex.h | $(BUILD_DIR)
	@echo [CC -c] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=cas_event_mutex src/measure.c -c -o $@ $(INCLUDE_MYSQL)

$(BUILD_DIR)/measure.lh_ticket_spinlock.o: ../../ext/linux/ticket_spinlock.h | $(BUILD_DIR)
	@echo [CC -c] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=ticket_spinlock src/measure.c -c -o $@ $(INCLUDE_LINUX)

$(BUILD_DIR)/measure.lh_hybrid_spinlock.o: ../../ext/linux/hybrid_spinlock.h | $(BUILD_DIR)
	@echo [CC -c] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=hybrid_spinlock src/measure.c -c -o $@ $(INCLUDE_LINUX)

$(BUILD_DIR)/measure.lh_hybrid_spinlock_fastdequeue.o: ../../ext/linux/hybrid_spinlock_fastdequeue.h | $(BUILD_DIR)
	@echo [CC -c] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=hybrid_spinlock_fastdequeue src/measure.c -c -o $@ $(INCLUDE_LINUX)

$(BUILD_DIR)/measure.lh_osq_lock.o: ../../ext/linux/osq_lock.h | $(BUILD_DIR)
	@echo [CC -c] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=osq_lock src/measure.c -c -o $@ $(INCLUDE_LINUX) -DOSQ_LOCK

$(BUILD_DIR)/measure.lh_clh_spinlock.o: ../../ext/sms/clh_spinlock.h | $(BUILD_DIR)
	@echo [CC -c] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=clh_spinlock src/measure.c -c -o $@ $(INCLUDE_SMS)

$(BUILD_DIR)/measure.lh_queued_spinlock.o: ../../ext/linux/queued_spinlock.h | $(BUILD_DIR)
	@echo [CC -c] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=queued_spinlock src/measure.c -c -o $@ $(INCLUDE_LINUX)

$(BUILD_DIR)/measure.lh_jvm_objectmonitor.o: ../../ext/jvm/jvm_objectmonitor.h | $(BUILD_DIR)
	@echo [CC -c] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=jvm_objectmonitor src/measure.c -c -o $@ $(INCLUDE_JVM)

$(BUILD_DIR)/measure.lh_tbb_spin_rw_mutex.o: ../../ext/tbb/tbb_spin_rw_mutex.h | $(BUILD_DIR)
	@echo [CC -c] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=tbb_spin_rw_mutex src/measure.c -c -o $@ $(INCLUDE_TBB) -DNDEBUG



# measure object depfiles (.d) for specific locks

$(BUILD_DIR)/measure.lh_event_mutex.d: ../../ext/mysql/event_mutex.h src/measure.c | $(BUILD_DIR)
	@echo [CC -MM] $@
	$(hide) $(CC) -o $@ -MM -MT '$(@:.d=.o) $(@:.d=.i) $@' $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=event_mutex src/measure.c $(INCLUDE_MYSQL)

$(BUILD_DIR)/measure.lh_cas_event_mutex.d: ../../ext/mysql/cas_event_mutex.h src/measure.c | $(BUILD_DIR)
	@echo [CC -MM] $@
	$(hide) $(CC) -o $@ -MM -MT '$(@:.d=.o) $(@:.d=.i) $@' $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=cas_event_mutex src/measure.c $(INCLUDE_MYSQL)

$(BUILD_DIR)/measure.lh_ticket_spinlock.d: ../../ext/linux/ticket_spinlock.h src/measure.c | $(BUILD_DIR)
	@echo [CC -MM] $@
	$(hide) $(CC) -o $@ -MM -MT '$(@:.d=.o) $(@:.d=.i) $@' $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=ticket_spinlock src/measure.c $(INCLUDE_LINUX)

$(BUILD_DIR)/measure.lh_hybrid_spinlock.d: ../../ext/linux/hybrid_spinlock.h src/measure.c | $(BUILD_DIR)
	@echo [CC -MM] $@
	$(hide) $(CC) -o $@ -MM -MT '$(@:.d=.o) $(@:.d=.i) $@' $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=hybrid_spinlock src/measure.c $(INCLUDE_LINUX)

$(BUILD_DIR)/measure.lh_hybrid_spinlock_fastdequeue.d: ../../ext/linux/hybrid_spinlock_fastdequeue.h src/measure.c | $(BUILD_DIR)
	@echo [CC -MM] $@
	$(hide) $(CC) -o $@ -MM -MT '$(@:.d=.o) $(@:.d=.i) $@' $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=hybrid_spinlock_fastdequeue src/measure.c $(INCLUDE_LINUX)

$(BUILD_DIR)/measure.lh_osq_lock.d: ../../ext/linux/osq_lock.h src/measure.c | $(BUILD_DIR)
	@echo [CC -MM] $@
	$(hide) $(CC) -o $@ -MM -MT '$(@:.d=.o) $(@:.d=.i) $@' $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=osq_lock src/measure.c $(INCLUDE_LINUX) -DOSQ_LOCK

$(BUILD_DIR)/measure.lh_clh_spinlock.d: ../../ext/sms/clh_spinlock.h src/measure.c | $(BUILD_DIR)
	@echo [CC -MM] $@
	$(hide) $(CC) -o $@ -MM -MT '$(@:.d=.o) $(@:.d=.i) $@' $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=clh_spinlock src/measure.c $(INCLUDE_SMS)

$(BUILD_DIR)/measure.lh_queued_spinlock.d: ../../ext/linux/queued_spinlock.h src/measure.c | $(BUILD_DIR)
	@echo [CC -MM] $@
	$(hide) $(CC) -o $@ -MM -MT '$(@:.d=.o) $(@:.d=.i) $@' $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=queued_spinlock src/measure.c $(INCLUDE_LINUX)

$(BUILD_DIR)/measure.lh_jvm_objectmonitor.d: ../../ext/jvm/jvm_objectmonitor.h src/measure.c | $(BUILD_DIR)
	@echo [CC -MM] $@
	$(hide) $(CC) -o $@ -MM -MT '$(@:.d=.o) $(@:.d=.i) $@' $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=jvm_objectmonitor src/measure.c $(INCLUDE_JVM)

$(BUILD_DIR)/measure.lh_tbb_spin_rw_mutex.d: ../../ext/tbb/tbb_spin_rw_mutex.h src/measure.c | $(BUILD_DIR)
	@echo [CC -MM] $@
	$(hide) $(CC) -o $@ -MM -MT '$(@:.d=.o) $(@:.d=.i) $@' $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=tbb_spin_rw_mutex src/measure.c $(INCLUDE_TBB) -DNDEBUG


# preprocessed output (.i) for specific locks

$(BUILD_DIR)/measure.lh_event_mutex.i: ../../ext/mysql/event_mutex.h | $(BUILD_DIR)
	@echo [CC -E] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=event_mutex src/measure.c -E -o $@ $(INCLUDE_MYSQL)

$(BUILD_DIR)/measure.lh_cas_event_mutex.i: ../../ext/mysql/cas_event_mutex.h | $(BUILD_DIR)
	@echo [CC -E] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=cas_event_mutex src/measure.c -E -o $@ $(INCLUDE_MYSQL)

$(BUILD_DIR)/measure.lh_ticket_spinlock.i: ../../ext/linux/ticket_spinlock.h | $(BUILD_DIR)
	@echo [CC -E] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=ticket_spinlock src/measure.c -E -o $@ $(INCLUDE_LINUX)

$(BUILD_DIR)/measure.lh_hybrid_spinlock.i: ../../ext/linux/hybrid_spinlock.h | $(BUILD_DIR)
	@echo [CC -E] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=hybrid_spinlock src/measure.c -E -o $@ $(INCLUDE_LINUX)

$(BUILD_DIR)/measure.lh_hybrid_spinlock_fastdequeue.i: ../../ext/linux/hybrid_spinlock_fastdequeue.h | $(BUILD_DIR)
	@echo [CC -E] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=hybrid_spinlock_fastdequeue src/measure.c -E -o $@ $(INCLUDE_LINUX)

$(BUILD_DIR)/measure.lh_osq_lock.i: ../../ext/linux/osq_lock.h | $(BUILD_DIR)
	@echo [CC -E] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=osq_lock src/measure.c -E -o $@ $(INCLUDE_LINUX) -DOSQ_LOCK

$(BUILD_DIR)/measure.lh_clh_spinlock.i: ../../ext/sms/clh_spinlock.h | $(BUILD_DIR)
	@echo [CC -E] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=clh_spinlock src/measure.c -E -o $@ $(INCLUDE_SMS)

$(BUILD_DIR)/measure.lh_queued_spinlock.i: ../../ext/linux/queued_spinlock.h | $(BUILD_DIR)
	@echo [CC -E] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=queued_spinlock src/measure.c -E -o $@ $(INCLUDE_LINUX)

$(BUILD_DIR)/measure.lh_jvm_objectmonitor.i: ../../ext/jvm/jvm_objectmonitor.h | $(BUILD_DIR)
	@echo [CC -E] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=jvm_objectmonitor src/measure.c -E -o $@ $(INCLUDE_JVM)

$(BUILD_DIR)/measure.lh_tbb_spin_rw_mutex.i: ../../ext/tbb/tbb_spin_rw_mutex.h | $(BUILD_DIR)
	@echo [CC -E] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=tbb_spin_rw_mutex src/measure.c -E -o $@ $(INCLUDE_TBB) -DNDEBUG




# measure object (.o) for generic locks -- preserve intermediate file that would otherwise be removed by make
.PRECIOUS: $(BUILD_DIR)/measure.lh_%.o
$(BUILD_DIR)/measure.lh_%.o: tests/%.h $(MEASURE_C_DEP_HEADERS) src/measure.c | $(BUILD_DIR)
	@echo [CC -c] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=$(basename $(notdir $<)) src/measure.c -c -o $@

# preprocessed output (.i) for generic locks
$(BUILD_DIR)/measure.lh_%.i: tests/%.h $(MEASURE_C_DEP_HEADERS) src/measure.c | $(BUILD_DIR)
	@echo [CC -E] $@
	$(hide) $(CC) $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=$(basename $(notdir $<)) src/measure.c -E -o $@

# measure dep file (.d) for generic locks
$(BUILD_DIR)/measure.lh_%.d: tests/%.h src/measure.c | $(BUILD_DIR)
	@echo [CC -MM] $@
	$(hide) $(CC) -o $@ -MM -MT '$(@:.d=.o) $(@:.d=.i) $@' $(CFLAGS) $(LOCK_CFLAGS) -DATOMIC_TEST=\"$<\" -DTEST_NAME=$(basename $(notdir $<)) src/measure.c


# all other dep files (.d), including harness dep files
$(BUILD_DIR)/%.d: src/%.c | $(BUILD_DIR)
	@echo [CC -MM] $@
	$(hide) $(CC) -o $@ -MM -MT '$(@:.d=.o) $(@:.d=.i) $@' $(CFLAGS) $(LOCK_CFLAGS) $<

# all other binary objects (.o), including harness objects
$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	@echo [CC -c] $@
	$(hide) $(CC) $(CFLAGS) $< -c -o $@

# all other code preprocessed files (.i), including harness objects
$(BUILD_DIR)/%.i: src/%.c | $(BUILD_DIR)
	@echo [CC -E] $@
	$(hide) $(CC) $(CFLAGS) $< -E -o $@



# ----------------------------------------------------------------------
# recipe for building executables

.SECONDEXPANSION:

# Explanation for using .SECONDEXPANSION:
# If the target $@ will be something like lh_empty, then "measure.$$(notdir $$@).o"
# makes "measure.lh_empty.o" a dependency.  Evaluating $$(nodir $$@) needs to be
# done as a secondary expansion, which is why this is defined here.

$(addprefix $(BUILD_DIR)/,$(TEST_TARGETS)): $(HARNESS_OBJS) $(BUILD_DIR)/measure.$$(notdir $$@).o | $(BUILD_DIR)
	@echo [LD] $@
	$(hide) $(LD) $^ $(LDFLAGS) -o $@
