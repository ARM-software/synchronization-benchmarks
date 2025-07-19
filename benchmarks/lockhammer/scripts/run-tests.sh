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

run-tests.sh [options] [-- remaining_args]

options:

Must choose one of -n or -N to run the tests:
-n              do a dry run (shows the commands that would run)
-N              actually run the tests

Override script-internal list:
-v variant      run only this variant (repeatable); use "-v list" to list
-e test_name    run only this test name (repeatable); use "-e list" to list

-c crit         critical duration flags (repeatable)
-p par          parallel duration flags (repeatable)

-o pinorder     run this pinorder instead of derived list (repeatable) (overrides -P)
-t num_threads  num_threads to use instead of derived list (repeatable) (overrides -P)
-P skip         processor skip step above 8 CPUs (default $CPU_SKIP)
                For example, -P 16 will measure 2, 4, 8, 24, 40...
-M hugepageflag Override --hugepage-size flag; give "-M none" to disable using a hugepage

-D sec          duration of each measurement in seconds (default $DURATION_SECONDS)
-T tag          run from build dirs that match this tag
-h              print this usage help message

-J              do not pass --json flag, and run even if the JSON file exists

remaining arguments after the "--" separator are passed to lockhammer

USAGE
	exit 1
}

CPU_SKIP=8
DRY_RUN=
DURATION_SECONDS=0.5
IGNORE_UNKNOWN_SCALING_GOVERNOR=-Y
TAG=
NO_JSON=0
HUGEPAGE_SIZE=

declare -a NUM_THREADS_NAMES
declare -a VARIANT_NAMES
declare -a TEST_NAMES
declare -a CRIT_NAMES
declare -a PAR_NAMES
declare -a PINORDER_NAMES

shopt -s extglob

while getopts ":NnP:M:o:t:v:e:D:T:c:p:Jh" name; do
	case "${name}" in
		N)	DRY_RUN=0
			;;
		n)	DRY_RUN=1
			;;
		P)	CPU_SKIP=${OPTARG}
			;;
		M)	HUGEPAGE_SIZE=${OPTARG}
			;;
		o)	PINORDER_NAMES+=(${OPTARG})
			;;
		t)	NUM_THREADS_NAMES+=(${OPTARG})
			;;
		v)	VARIANT_NAMES+=(${OPTARG})
			;;
		e)	TEST_NAMES+=(${OPTARG})
			;;
		D)	DURATION_SECONDS=${OPTARG}
			;;
		T)	TAG=${OPTARG}
			;;
		c)	CRIT_NAMES+=(${OPTARG})
			;;
		p)	PAR_NAMES+=(${OPTARG})
			;;
		J)	NO_JSON=1
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

REMAINING_ARGS="$@"

# make the list of build variants to run; use # to comment out variant

declare -A VARIANT_LIST

# x86_64 variants
VARIANT_LIST["x86_64"]=$(grep -v -E '\#|^$' <<'EOF_X86_64_VARIANT_LIST'
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
EOF_X86_64_VARIANT_LIST
)

# aarch64 variants
VARIANT_LIST["aarch64"]=$(grep -v -E '\#|^$' <<'EOF_AARCH64_VARIANT_LIST'
builtin.cond_load.relax_empty
builtin.cond_load.relax_isb
builtin.cond_load.relax_nop
builtin.cond_load.relax_nothing
builtin.relax_empty
builtin.relax_isb
builtin.relax_nop
builtin.relax_nothing
cond_load.relax_empty
cond_load.relax_isb
cond_load.relax_nop
cond_load.relax_nothing
lse.builtin.cond_load.relax_empty
lse.builtin.cond_load.relax_isb
lse.builtin.cond_load.relax_nop
lse.builtin.cond_load.relax_nothing
lse.builtin.relax_empty
lse.builtin.relax_isb
lse.builtin.relax_nop
lse.builtin.relax_nothing
lse.cond_load.relax_empty
lse.cond_load.relax_isb
lse.cond_load.relax_nop
lse.cond_load.relax_nothing
lse.relax_empty
lse.relax_isb
lse.relax_nop
lse.relax_nothing
relax_empty
relax_isb
relax_nop
relax_nothing
EOF_AARCH64_VARIANT_LIST
)

VARIANT_LIST=${VARIANT_LIST[$(uname -m)]}

if [[ ${#VARIANT_NAMES[@]} -ne 0 ]]; then
	for a in ${VARIANT_NAMES[@]}; do
	if [[ $a == "list" ]]; then
		echo "variant list:"
		for b in ${VARIANT_LIST[@]}; do
			echo $b
		done
		exit -1
	fi
	done
	VARIANT_LIST=${VARIANT_NAMES[@]}
fi

# make the list of tests to run; use # to comment out test

TEST_LIST=$(grep -v -E '\#|^$' <<'EOF_TEST_LIST'
lh_cas_event_mutex
lh_cas_lockref
lh_cas_rw_lock
lh_clh_spinlock
lh_empty
lh_event_mutex
lh_hybrid_spinlock
lh_hybrid_spinlock_fastdequeue
lh_incdec_refcount
lh_jvm_objectmonitor
lh_osq_lock
lh_pthread_mutex_lock
lh_pthread_mutex_trylock
lh_queued_spinlock
lh_swap_mutex
lh_tbb_spin_rw_mutex
lh_ticket_spinlock
EOF_TEST_LIST
)


if [[ ${#TEST_NAMES[@]} -ne 0 ]]; then
	for a in ${TEST_NAMES[@]}; do
	if [[ $a == "list" ]]; then
		echo "test list:"
		for b in ${TEST_LIST[@]}; do
			echo $b
		done
		exit -1
	fi
	done
	TEST_LIST=${TEST_NAMES[@]}
fi


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

NUM_PAR=0
PAR=""
for a in $PAR_NS_LIST; do
	PAR+="-p${a}ns "
	NUM_PAR=$((NUM_PAR+1))
done

CRIT=""
for a in $CRIT_NS_LIST; do
	CRIT+="-c${a}ns "
	NUM_CRIT=$((NUM_CRIT+1))
done

if [[ ${#PAR_NAMES[@]} -ne 0 ]]; then
	PAR=""
	for a in ${PAR_NAMES[@]}; do
		PAR+="-p $a "
	done
	NUM_PAR=${#PAR_NAMES[@]}
fi

if [[ ${#CRIT_NAMES[@]} -ne 0 ]]; then
	CRIT=""
	for a in ${CRIT_NAMES[@]}; do
		CRIT+="-c $a "
	done
	NUM_CRIT=${#CRIT_NAMES[@]}
fi

#echo "NUM_CRIT=$NUM_CRIT"
#echo "NUM_PAR=$NUM_PAR"

if [ -z "$HUGEPAGE_SIZE" ]; then
	# check that a hugepage is available
	#HUGEPAGE_SIZE=32MB
	#HUGEPAGE_SIZE_KB=$((32*1024))
	HUGEPAGE_SIZE=1GB
	HUGEPAGE_SIZE_KB=$((1024*1024))
	HUGEPAGES_DIR=/sys/kernel/mm/hugepages/hugepages-${HUGEPAGE_SIZE_KB}kB
	if [ ! -d "$HUGEPAGES_DIR" ]; then
		echo -e "\nWARNING: no hugepage support found for size $HUGEPAGE_SIZE\n"
		HUGEPAGE_SIZE="none"
	else
		FREE_HUGEPAGES_FILE=$HUGEPAGES_DIR/free_hugepages
		FREE_HUGEPAGES=$(cat "$FREE_HUGEPAGES_FILE")
		if [ $FREE_HUGEPAGES -eq 0 ]; then
			NR_HUGEPAGES_FILE=$HUGEPAGES_DIR/nr_hugepages
			NR_HUGEPAGES=$(cat "$NR_HUGEPAGES_FILE")
			NR_HUGEPAGES_PLUS_ONE=$((NR_HUGEPAGES+1))
			echo "ERROR: no free $HUGEPAGE_SIZE hugepages.  Perhaps try running:"
			echo "echo $NR_HUGEPAGES_PLUS_ONE | sudo tee -a $NR_HUGEPAGES_FILE"
			exit -1
		fi
	fi
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
NUM_TLIST=0
TLIST=
for num_threads in 2 4 $(eval echo "{8..$NPROC..$CPU_SKIP}")
do
	if [ $num_threads -gt $NPROC ]; then
		break
	fi

	TLIST+="-t $num_threads "
	NUM_TLIST=$((NUM_TLIST+1))
done


# override TLIST if specified
if [[ ${#NUM_THREADS_NAMES[@]} -ne 0 ]]; then
	TLIST=""
	for a in ${NUM_THREADS_NAMES[@]}; do
		TLIST+="-t $a "
	done
	NUM_TLIST=${#NUM_THREADS_NAMES[@]}
fi

NUM_PINORDERS=0
PINORDERS=""
if [[ ${#PINORDER_NAMES[@]} -ne 0 ]]; then
	for a in ${PINORDER_NAMES[@]}; do
		PINORDERS+="-o $a "
	done
	NUM_PINORDERS=${#PINORDER_NAMES[@]}
fi


# compute number of tests and variants
NUM_TESTS=$(echo $TEST_LIST | wc -w)
NUM_VARIANTS=$(echo $VARIANT_LIST | wc -w)
#echo NUM_VARIANTS=$NUM_VARIANTS NUM_TESTS=$NUM_TESTS
NUM_TEST_AND_VARIANTS=$((NUM_TESTS*NUM_VARIANTS))
TEST_AND_VARIANT_COUNT=0
#echo NUM_TEST_AND_VARIANTS=$NUM_TEST_AND_VARIANTS

function time_remaining () {
	local eta_seconds=$1
	date -d@$eta_seconds -u +%H:%M:%S
}

function eta () {
	local eta_seconds=$1
	date -d "$eta_seconds sec"
}


NUM_PERMUTATIONS=$(((NUM_TLIST + NUM_PINORDERS)* NUM_CRIT * NUM_PAR))
DURATION_OVERHEAD=1		# TODO: dynamically adjust this by the run
PER_TEST_DURATION=$(echo "$NUM_PERMUTATIONS * ($DURATION_SECONDS + $DURATION_OVERHEAD)" | bc)
TOTAL_TIME_EST=$(echo "$NUM_TEST_AND_VARIANTS * $PER_TEST_DURATION" | bc)


# change newline to space for the summary
TEST_LIST=${TEST_LIST//$'\n'/ }
VARIANT_LIST=${VARIANT_LIST//$'\n'/ }
PAR_NS_LIST=${PAR_NS_LIST//$'\n'/ }
CRIT_NS_LIST=${CRIT_NS_LIST//$'\n'/ }

cat<<SUMMARY
hostname            = $HOSTNAME_S
cpuorder            = $CPUORDER
num_threads         = ${TLIST//-t /}
pinorders           = ${PINORDERS//-o /}
tag                 = $TAG
variants            = $VARIANT_LIST
tests               = $TEST_LIST
critical durations  = $CRIT
parallel durations  = $PAR
duration            = $DURATION_SECONDS (sec)
exec overhead       = $DURATION_OVERHEAD (sec)
hugepage flags      = $HUGEPAGE_FLAGS

tests * variants    = $NUM_TEST_AND_VARIANTS
permutations        = $NUM_PERMUTATIONS (crit * par * (num_threads + pinorders))
expected time/exec  = $PER_TEST_DURATION (sec)
expected total time = $TOTAL_TIME_EST (sec)
expected finish     = $(eta $TOTAL_TIME_EST)

SUMMARY

# point of no return, must have -n or -N for go/no-go
if [ -z "$DRY_RUN" ]; then
	echo
	echo "ERROR: neither -n (dry run) nor -N (not dry run, actually run) were specified.  Use -h to show help."
	echo
	exit -1
fi

echo
echo ----------------------------------------------------------
echo Beginning measurements at: $(date)
echo ----------------------------------------------------------

CMD_OVERHEAD=0
START_EPOCHSECONDS=$EPOCHSECONDS

for BUILD_VARIANT in $VARIANT_LIST
do

for test in $TEST_LIST
do
	TESTS_LEFT_COUNT=$((NUM_TEST_AND_VARIANTS - TEST_AND_VARIANT_COUNT))
	TEST_AND_VARIANT_COUNT=$((TEST_AND_VARIANT_COUNT+1))
	SECONDS_LEFT=$(echo "scale=0; ($TESTS_LEFT_COUNT * ( $PER_TEST_DURATION + $CMD_OVERHEAD ) + 0.5) / 1" | bc)

	if let 0; then
	echo
	echo ----------------------------------------------------------------------
	echo    "Running  : $TEST_AND_VARIANT_COUNT of $NUM_TEST_AND_VARIANTS"
	echo    "Timestamp: $(date)"
	echo -e "Finishing: $(eta $SECONDS_LEFT), $(time_remaining $SECONDS_LEFT) remaining"
	echo ----------------------------------------------------------------------
	else
	echo
	echo ----------------------------------------------------------------------
	echo    "Timestamp: $(date)     Running: $TEST_AND_VARIANT_COUNT of $NUM_TEST_AND_VARIANTS"
	echo -e "Finishing: $(eta $SECONDS_LEFT)   Time left: $(time_remaining $SECONDS_LEFT)"
	echo ----------------------------------------------------------------------
	fi

	TAG_FLAG=
	BUILD_DIR=build.$BUILD_VARIANT
	if [ -n "$TAG" ]; then
		BUILD_DIR="build.$BUILD_VARIANT.$TAG"
		TAG_FLAG="-T $TAG"
	fi
	EXE=$BUILD_DIR/$test
	JSON=$HOSTNAME_S.$test.$BUILD_VARIANT.json
	if [ -n "$TAG" ]; then
		JSON=$HOSTNAME_S.$test.$BUILD_VARIANT.$TAG.json
	fi

	if [ $NO_JSON -eq 0 ]; then
		JSON_FLAG="--json $JSON"
	fi

	CMD="$EXE $IGNORE_UNKNOWN_SCALING_GOVERNOR $PAR $CRIT -D $DURATION_SECONDS $CPUORDER_FLAGS $HUGEPAGE_FLAGS $TLIST $PINORDERS $JSON_FLAG $TAG_FLAG $REMAINING_ARGS"

	if [ ! -x "$EXE" ]; then
		echo ERROR: $EXE is not found or not executable, skipping test
		continue
	fi

	if [ -e "$JSON" ]; then
		if [ $NO_JSON -eq 1 ]; then
			echo WARNING: $JSON already exists, but NO_JSON=1, so will run
		else
			echo ERROR: $JSON already exists, will not overwrite, skipping test
			continue
		fi
	fi

	echo
	if [ $DRY_RUN -eq 0 ]; then
		CMD_SECONDS_START=$SECONDS
		echo $CMD
		echo

		$CMD

		CMD_SECONDS_STOP=$SECONDS
		CMD_SECONDS_DIFF=$((CMD_SECONDS_STOP - CMD_SECONDS_START))
		CMD_OVERHEAD=$( echo "scale = 0; $CMD_SECONDS_DIFF - $PER_TEST_DURATION" | bc )
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
echo ----------------------------------------------------------
echo Finished measurements at: $(date)
echo Elapsed time: $ELAPSED_TIME
echo ----------------------------------------------------------
