#!/bin/bash

# SPDX-FileCopyrightText: Copyright 2019-2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: BSD-3-Clause

# This script invokes lockhammer tests.
# This script is meant to be edited for customizing which tests to run.
# Edit the *_LIST variables below to choose the configuration combinations.
# A json file will be made for each variant and test combination.

set -e

usage() {
cat<<USAGE

run-tests.sh [options]

options:

-n       do a dry run
-P skip  processor skip step above 8 CPUs (default $CPU_SKIP)
         For example, -P 16 will measure 2, 4, 8, 24, 40...
-T sec   duration of each measurement in seconds (default $DURATION_SECONDS)
-h       print this usage help message

USAGE
	exit 1
}

CPU_SKIP=8
DRY_RUN=0
DURATION_SECONDS=0.5
IGNORE_UNKNOWN_SCALING_GOVERNOR=-Y

shopt -s extglob

while getopts ":nP:T:h" name; do
	case "${name}" in
		n)	DRY_RUN=1
			;;
		P)	CPU_SKIP=${OPTARG}
			;;
		T)	DURATION_SECONDS=${OPTARG}
			;;
		h)	usage
			;;
		:)	>&2 echo "ERROR: flag -$OPTARG required an argument, but none was given"
			usage
			;;
		*)	echo name=$name, OPTARG=$OPTARG
			usage
			;;
	esac
done

shift $((OPTIND-1))

# make the list of build variants to run; use # to comment out variant

# TODO: update lists by arch

VARIANT_LIST=$(grep -v -E '\#|^$' <<'EOF1'
builtin.cond_load.relax_empty
builtin.cond_load.relax_nothing
builtin.cond_load.relax_pause
builtin.relax_empty
builtin.relax_nothing
builtin.relax_pause
cond_load.relax_empty
cond_load.relax_nothing
cond_load.relax_pause
relax_empty
relax_nothing
relax_pause


#builtin.relax_nothing
#builtin.relax_isb
#builtin.cond_load.relax_nothing
#builtin.cond_load.relax_isb

#lse.builtin.relax_nothing
#lse.builtin.relax_isb
#lse.builtin.cond_load.relax_nothing
#lse.builtin.cond_load.relax_isb
EOF1
)

# make the list of tests to run; use # to comment out test

TEST_LIST=$(grep -v -E '\#|^$' <<'EOF2'
lh_cas_event_mutex
lh_cas_lockref
lh_cas_rw_lock
#lh_clh_spinlock
#lh_empty
#lh_event_mutex
#lh_hybrid_spinlock
#lh_hybrid_spinlock_fastdequeue
lh_incdec_refcount
lh_jvm_objectmonitor
lh_osq_lock
#lh_queued_spinlock
#lh_swap_mutex
#lh_tbb_spin_rw_mutex
lh_ticket_spinlock
lh_pthread_mutex_lock
EOF2
)

CRIT_NS_LIST=$(grep -v -E '\#|^$' <<'EOF_CRIT_NS'
0
500
1000
EOF_CRIT_NS
)

PAR_NS_LIST=$(grep -v -E '\#|^$' <<'EOF_PAR_NS'
0
500
1000
2000
4000
EOF_PAR_NS
)

PAR=""
for a in $PAR_NS_LIST; do PAR+="-p${a}ns "; done

CRIT=""
for a in $CRIT_NS_LIST; do CRIT+="-c${a}ns "; done


# check that a hugepage is available
#HUGEPAGE_SIZE=32MB
#HUGEPAGE_SIZE_KB=$((32*1024))
HUGEPAGE_SIZE=1GB
HUGEPAGE_SIZE_KB=$((1024*1024))
HUGEPAGES_DIR=/sys/kernel/mm/hugepages/hugepages-${HUGEPAGE_SIZE_KB}kB
FREE_HUGEPAGES_FILE=$HUGEPAGES_DIR/free_hugepages
NR_HUGEPAGES_FILE=$HUGEPAGES_DIR/nr_hugepages
NR_HUGEPAGES=$(cat "$NR_HUGEPAGES_FILE")
NR_HUGEPAGES_PLUS_ONE=$((NR_HUGEPAGES+1))
if [ ! -e "$FREE_HUGEPAGES_FILE" ] || [ $(cat "$FREE_HUGEPAGES_FILE") -eq 0 ]; then
	echo "ERROR: no free $HUGEPAGE_SIZE hugepages.  Perhaps try running:"
	echo "echo $NR_HUGEPAGES_PLUS_ONE | sudo tee -a $NR_HUGEPAGES_FILE"
	exit -1
fi
HUGEPAGE_FLAGS="--hugepage-size $HUGEPAGE_SIZE"


# determine cpuorder file to use based on hostname.
HOSTNAME_S=$(hostname -s)
CPUORDER_FLAGS=
if [ -e hostname_to_cpuorder_type.sh ]; then
	. hostname_to_cpuorder_type.sh

	CPUORDER_TYPE=$(hostname_to_cpuorder_type $HOSTNAME_S)
	CPUORDER=cpuorders/$CPUORDER_TYPE.cpuorder

	if [ ! -e "$CPUORDER" ]; then
		echo "ERROR:  $CPUORDER does not exist!"
		exit -1
	fi

	CPUORDER_FLAGS="-C $CPUORDER"
fi

# compute the number of threads using the number of available processors
NPROC=$(nproc)
TLIST=
for num_threads in 2 4 $(eval echo "{8..$NPROC..$CPU_SKIP}")
do
	if [ $num_threads -gt $NPROC ]; then
		break
	fi

	TLIST+="-t $num_threads "
done


# compute number of test and variants
NUM_TESTS=$(echo $TEST_LIST | wc -w)
NUM_VARIANTS=$(echo $VARIANT_LIST | wc -w)
#echo NUM_VARIANTS=$NUM_VARIANTS NUM_TESTS=$NUM_TESTS
NUM_TEST_AND_VARIANTS=$((NUM_TESTS*NUM_VARIANTS))
TEST_AND_VARIANT_COUNT=0
#echo NUM_TEST_AND_VARIANTS=$NUM_TEST_AND_VARIANTS

#exit 0

# change newline to space for the summary
TEST_LIST=${TEST_LIST//$'\n'/ }
VARIANT_LIST=${VARIANT_LIST//$'\n'/ }
PAR_NS_LIST=${PAR_NS_LIST//$'\n'/ }
CRIT_NS_LIST=${CRIT_NS_LIST//$'\n'/ }

cat<<SUMMARY
hostname            = $HOSTNAME_S
cpuorder            = $CPUORDER
num_threads         = ${TLIST//-t /}
variants            = $VARIANT_LIST
tests               = $TEST_LIST
critical times (ns) = $CRIT_NS_LIST
parallel times (ns) = $PAR_NS_LIST
duration (sec)      = $DURATION_SECONDS
hugepage flags      = $HUGEPAGE_FLAGS
SUMMARY

echo
echo ------------------------------------------------------
echo beginning measurements at: $(date)
echo ------------------------------------------------------

START_EPOCHSECONDS=$EPOCHSECONDS

for BUILD_VARIANT in $VARIANT_LIST
do

for test in $TEST_LIST
do

	TEST_AND_VARIANT_COUNT=$((TEST_AND_VARIANT_COUNT+1))
	echo
	echo running $TEST_AND_VARIANT_COUNT of $NUM_TEST_AND_VARIANTS test+variant combinations
	echo

	EXE=build.$BUILD_VARIANT/$test
	JSON=$HOSTNAME_S.$test.$BUILD_VARIANT.json
	CMD="$EXE $IGNORE_UNKNOWN_SCALING_GOVERNOR $PAR $CRIT -T $DURATION_SECONDS $CPUORDER_FLAGS $HUGEPAGE_FLAGS $TLIST --json $JSON"

	if [ ! -x "$EXE" ]; then
		echo ERROR: $EXE is not found or not executable, skipping test
		continue
	fi

	if [ -e "$JSON" ]; then
		echo ERROR: $JSON already exists, will not overwrite, skipping test
		continue
	fi

	echo
	if [ $DRY_RUN -eq 0 ]; then
		echo $CMD
		echo
		$CMD
	else
		echo This is a DRY RUN -- the following command will not be executed:
		echo
		echo $CMD
	fi
done
done

FINISH_EPOCHSECONDS=$EPOCHSECONDS
ELAPSED_SECONDS=$((FINISH_EPOCHSECONDS-START_EPOCHSECONDS))
if [ $ELAPSED_SECONDS -ge 86400 ]; then
ELAPSED_TIME=$(date -u -d @$ELAPSED_SECONDS +%j:%T)
else
ELAPSED_TIME=$(date -u -d @$ELAPSED_SECONDS +%T)
fi

echo
echo ------------------------------------------------------
echo finished measurements at: $(date)
echo elapsed time: $ELAPSED_TIME
echo ------------------------------------------------------
