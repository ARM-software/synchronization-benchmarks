#!/bin/bash

# SPDX-FileCopyrightText: Copyright 2019-2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: BSD-3-Clause

# This script displays the values from one or more lockhammer json files in a table format using jq.

# XXX: can't differentiate between ns vs. inst for crit/par; please select only using the same units!

SORT_STRING='.num_threads'
REVERSE=0
DUMP_DATA=0

declare -a CRIT
declare -a PAR
declare -a NUM_THREADS
declare -a VARIANT_NAMES
declare -a ADDITIONAL_KEYS
declare -a TAG_NAMES

usage() {
cat<<"USAGE"

./view-results-json.sh [options] json [json ...]

select options:
-c crit           nominal critical time/inst parameter (repeatable)
-p par            nominal parallel time/inst parameter (repeatable)
-t num_threads    number of threads (repeatable)
-v variant_name   variant name (repeatable)
-T tag            select only results with these tags (repeatable)

sort options:
-s sort_string    sort string (default is by '.num_threads')
-s help           print short header to .key mapping
-r                reverse the sort

output options:
-a key            display values of the additional key (repeatable)
-D                dump the records in a json array

-h                print this usage help message


Example:

# list all data with threads=8, parallel=1000 or parallel=500, and critical=0
# from files *osq_lock*.json, sort by overhead %

./view-results-json.sh -s overhead_% -t 8 -p 1000 -p 500 -c 0 *osq_lock*.json

USAGE
	exit 1
}


shopt -s extglob

while getopts ":c:p:t:v:T:a:s:rDh" name; do
	case "${name}" in
		c)	CRIT+=(${OPTARG})
			;;
		p)	PAR+=(${OPTARG})
			;;
		t)	NUM_THREADS+=(${OPTARG})
			;;
		v)	VARIANT_NAMES+=(${OPTARG})
			;;
		T)	TAG_NAMES+=(${OPTARG})
			;;
		a)	ADDITIONAL_KEYS+=(${OPTARG})
			;;
		s)	SORT_STRING=${OPTARG}
			;;
		r)	REVERSE=1
			;;
		D)	DUMP_DATA=1
			;;
		h)	usage
			;;
		:)	>&2 echo "ERROR: flag -$OPTARG required an argument, but none was given"
			usage
			;;
		*)	echo "ERROR: unknown flag name=$name, OPTARG=$OPTARG"
			usage
			;;
	esac
done

shift $((OPTIND-1))

FILES="$@"

if [ -z "$FILES" ]; then
	echo "no json files given; run with -h for usage help"
        exit -1
fi

# -----------------------------------------------------------------------------
# jq filter stages. Write as separate single-quoted strings so that escapes are not needed (i.e., do not use escapes!).
#
# reducer   - puts data from all the json into an array with some modifications
# selector  - selects the data from the array that match the command line criteria
# sorter    - sort the selected data by the sorting criteria
# filter    - convert the sorted data into formatted output

# ----------------------------------
# Reducer gets the .results[] array from each json, and, for each results
# element/object, deletes the pinorder and per_thread_sets, and adds an
# .input_filename to the object.  The output is a single array of results
# elements.

REDUCER='reduce inputs as $s ([]; . += [$s.results[] | del(.pinorder) | del(.per_thread_stats) | . += {"input_filename":input_filename}])'


# ----------------------------------
# Select the records with the requested element values

make_selector() {
local NAME="$1"
shift
local AS_STRING=0
if [ "$1" = "as_string" ]; then
	AS_STRING=1
	shift
elif [ "$1" = "as_number" ]; then
	AS_STRING=0
	shift
fi

local ARRAY=("$@")
local ARRAY_SELECTOR=

if [ ${#ARRAY[@]} -eq 0 ]; then
	return
fi

for a in ${ARRAY[@]}; do
	if [ -n "$ARRAY_SELECTOR" ]; then ARRAY_SELECTOR+=" or "; fi
	if [ $AS_STRING -eq 1 ]; then
		ARRAY_SELECTOR+=".${NAME}==\"${a}\""
	else
		ARRAY_SELECTOR+=".${NAME}==${a}"
	fi
done

echo " and ($ARRAY_SELECTOR)"
}

SELECTOR_ARGLIST="true"
SELECTOR_ARGLIST+=$(make_selector nominal_parallel "${PAR[@]}")
SELECTOR_ARGLIST+=$(make_selector nominal_critical "${CRIT[@]}")
SELECTOR_ARGLIST+=$(make_selector num_threads "${NUM_THREADS[@]}")
SELECTOR_ARGLIST+=$(make_selector variant_name as_string "${VARIANT_NAMES[@]}")
SELECTOR_ARGLIST+=$(make_selector tag as_string "${TAG_NAMES[@]}")

SELECTOR=' [.[] | select('$SELECTOR_ARGLIST')] '


# ----------------------------------
# Sort; output is an array

# for -s sort_string flag, map it to these fields.  TODO: reverse SPECIAL_HEADER array instead of hard-coding
declare -A SHORT_HEADER
SHORT_HEADER[cputime_ns/lock]=".cputime_ns_per_lock_acquire"
SHORT_HEADER[cpu_ns/lock]=".cputime_ns_per_lock_acquire"
SHORT_HEADER[wall_ns/lock]=".wall_elapsed_ns_per_lock_acquire"
SHORT_HEADER[fcf]=".full_concurrency_fraction"
SHORT_HEADER[nom_par]=".nominal_parallel"
SHORT_HEADER[nom_crit]=".nominal_critical"
SHORT_HEADER[par_ns]=".avg_parallel_ns_per_loop"
SHORT_HEADER[crit_ns]=".avg_critical_ns_per_loop"
SHORT_HEADER[overhead_ns]=".avg_lock_overhead_cputime_ns"
SHORT_HEADER[overhead_%]=".lock_overhead_cputime_percent"
SHORT_HEADER[locks/wall_sec]=".total_lock_acquires_per_second"
SHORT_HEADER[num_threads]=".num_threads"
SHORT_HEADER[json]=".input_filename"
SHORT_HEADER[host]=".hostname"
SHORT_HEADER[lasom]=".lock_acquires_stddev_over_mean"

# print SHORT_HEADER as a table
if [[ $SORT_STRING == "help" ]]; then
	(echo "sort_key sort_string";
	for key in "${!SHORT_HEADER[@]}" ; do
		echo "$key ${SHORT_HEADER[$key]}"
	done) | column -t
	exit -1
fi

if [[ -v SHORT_HEADER[$SORT_STRING] ]]; then
	SORT_STRING="${SHORT_HEADER[$SORT_STRING]}"
elif [[ ! $SORT_STRING =~ ^\. ]]; then
	# we check for this to allow for complex multikey comma-separated sort string to be passed in as an argument.
	echo "ERROR: SORT_STRING does not being with a . and is not one of the SHORT_HEADER keys, so it's probably not referring to a results variable."
	exit -1
fi

#SORTER='sort_by(.cputime_ns_per_lock_acquire) '
#SORTER='sort_by(.num_threads) '
SORTER='sort_by('$SORT_STRING')'
if [ $REVERSE -eq 1 ]; then
	SORTER+=' | reverse'
fi

# json output from jq
if [ $DUMP_DATA -eq 1 ]; then
	exec jq -n -r "$REDUCER | $SELECTOR | $SORTER | . " $FILES
fi


# the rest of this is for the tabulated output

# ----------------------------------
# Construct KEY_LIST, an array defining the order of the columns.
# These are typically keynames from entries in the .results[] of a json or, if there's a corresponding entry in SPECIAL_HEADER or SPECIAL_FILTER, what to show instead.
# If the row begins with #, the metric is omitted.
read -r -d '' -a KEY_LIST <<'EOF_KEY_LIST'
test_name
#test_type_name
variant_name
tag
num_threads
nominal_critical
nominal_parallel
cputime_ns_per_lock_acquire
avg_critical_ns_per_loop
avg_parallel_ns_per_loop
avg_lock_overhead_cputime_ns
lock_overhead_cputime_percent
full_concurrency_fraction
lock_acquires_stddev_over_mean
host
#json
wall_elapsed_ns_per_lock_acquire
total_lock_acquires_per_second
EOF_KEY_LIST

KEY_LIST+=("${ADDITIONAL_KEYS[@]}")

# SPECIAL_HEADER is what to print in the header for a key name. If the key does not exist, then the key name is used as the header.
declare -A SPECIAL_HEADER
SPECIAL_HEADER[cputime_ns_per_lock_acquire]="cpu_ns/lock"
SPECIAL_HEADER[wall_elapsed_ns_per_lock_acquire]="wall_ns/lock"
SPECIAL_HEADER[full_concurrency_fraction]="fcf"
SPECIAL_HEADER[avg_parallel_ns_per_loop]="par_ns"
SPECIAL_HEADER[avg_critical_ns_per_loop]="crit_ns"
SPECIAL_HEADER[avg_lock_overhead_cputime_ns]="overhead_ns"
SPECIAL_HEADER[lock_overhead_cputime_percent]="overhead_%"
SPECIAL_HEADER[total_lock_acquires_per_second]="locks/wall_sec"
SPECIAL_HEADER[lock_acquires_stddev_over_mean]="lasom"
SPECIAL_HEADER[nominal_critical]="nom_crit"
SPECIAL_HEADER[nominal_parallel]="nom_par"

# SPECIAL_FILTER is how to have jq format the element. If the key does not exist, then .key is used for the filter.
declare -A SPECIAL_FILTER

ROUND_IF_NORMAL=' if isnormal then round else . end '

SPECIAL_FILTER[cputime_ns_per_lock_acquire]="\(.cputime_ns_per_lock_acquire | $ROUND_IF_NORMAL)"
SPECIAL_FILTER[wall_elapsed_ns_per_lock_acquire]="\(.wall_elapsed_ns_per_lock_acquire | $ROUND_IF_NORMAL)"
SPECIAL_FILTER[full_concurrency_fraction]="\(.full_concurrency_fraction | if isnormal then (.*100|round/100) else . end)"
SPECIAL_FILTER[host]='\(.hostname // "-" | split(".") | .[0])'
SPECIAL_FILTER[json]='\(.input_filename // "-" | split(".") | .[:-1] | join("."))'
SPECIAL_FILTER[avg_critical_ns_per_loop]="\(.avg_critical_ns_per_loop | $ROUND_IF_NORMAL)"
SPECIAL_FILTER[avg_parallel_ns_per_loop]="\(.avg_parallel_ns_per_loop | $ROUND_IF_NORMAL)"
SPECIAL_FILTER[avg_lock_overhead_cputime_ns]="\(.avg_lock_overhead_cputime_ns | $ROUND_IF_NORMAL)"
SPECIAL_FILTER[lock_overhead_cputime_percent]="\(.lock_overhead_cputime_percent | $ROUND_IF_NORMAL)"
SPECIAL_FILTER[total_lock_acquires_per_second]="\(.total_lock_acquires_per_second| $ROUND_IF_NORMAL)"
SPECIAL_FILTER[lock_acquires_stddev_over_mean]='\(.lock_acquires_stddev_over_mean | if isnormal then (.*10000|round/10000) else . end)'

# constructs the header or filter
make_special() {
local -n pointer="$1"		# name reference to associative array, needs bash 4.2 or later
local normal_format_pre_eval=$2
local normal_format
local key
local list=
for key in "${KEY_LIST[@]}"
do
	if [[ $key =~ ^\# ]]; then
		continue
	fi
	if [ -n "$list" ]; then
		list="$list\t"
	fi

	normal_format=$(eval "echo \"$normal_format_pre_eval\"")

	if [[ -v pointer[$key] ]]; then
		list+="${pointer[$key]}"
	else
		list+=$normal_format
	fi
done
echo "$list"
}

HEADER=$(make_special SPECIAL_HEADER '$key')
FILTER=$(make_special SPECIAL_FILTER '\(.${key})')

# ----------------------------------
# finally invoke jq for tabulated output using 'column' to pretty print.
(
	echo -e "$HEADER"
	jq -n -r "$REDUCER | $SELECTOR | $SORTER | .[] | \"$FILTER\" " $FILES
) | column -t -o " "
