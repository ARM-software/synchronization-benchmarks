#!/bin/bash

# SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: BSD-3-Clause

# This program recovers a lockhammer lh command from a results json and the meas_number to rerun a measurement.

usage() {
echo "$0 json_file [meas_number]"
exit -1
}

rebuild_flags() {
	local FILE=$1
	local MEAS_NUMBER=$2

	local LH_COMMON_FLAGS='-Y -D 1 -M 1G'

	local LH_EXE='build.\(.variant_name)\(if .tag then "."+.tag else "" end)/lh_\(.test_name)'
	local LH_CRIT='-c \(.nominal_critical)\(.nominal_critical_unit)'
	local LH_PAR='-p \(.nominal_parallel)\(.nominal_parallel_unit)'

# the old way to detect pinorder string is not a thread_num, but pinorder_string may now be a range and not always a pure number
#	local LH_PINORDER='\(if ((.pinorder_string | startswith("t")) or ((.pinorder_string | tonumber) == .num_threads)) then "-t"+.pinorder_string else "-o"+.pinorder_string end)'

	local LH_PINORDER='\(if ((.pinorder_string | startswith("t"))) then "-t"+.pinorder_string else "-o"+.pinorder_string end)'

	local LH_COMMAND="$LH_EXE $LH_CRIT $LH_PAR $LH_PINORDER $LH_COMMON_FLAGS"

	# if no measurement number if given, then all of the results records are processed
	if [ -z "$MEAS_NUMBER" ]; then
		jq -e -r ".results[] | \"$LH_COMMAND\"" "$FILE"
	else
		jq -e --argjson i "$MEAS_NUMBER" -r ".results[] | select(.meas_number == \$i) | \"$LH_COMMAND\"" "$FILE"
	fi
}

MEAS_NUMBER=$2
FILE=$1

if [ -z "$FILE" ]; then
	echo "ERROR: missing json file"
	usage
fi

if [ -n "$MEAS_NUMBER" ]; then

	# check if there is a record with the value of the meas_number
	if ! jq -e --argjson i "$MEAS_NUMBER" -r ".results[] | select(.meas_number == \$i)" "$FILE" > /dev/null; then
		echo "ERROR: did not find a result with .meas_number == $MEAS_NUMBER in $FILE"
		exit -1
	fi
fi

rebuild_flags "$FILE" $MEAS_NUMBER

exit 0
