#!/bin/bash

# SPDX-FileCopyrightText: Copyright 2019-2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: BSD-3-Clause

# This script shows the per-thread fairness of lock acquires in the result json(s).

# show-per-thread-lock-acquires.sh result1.json [result2.json ...]

# add this to filter only one set of crit/par
#.results[]|select(.nominal_critical==0 and .nominal_parallel==0)|

read -r -d '' CMD <<'EOF'
.results[]|"\(.nominal_critical)\t\(.nominal_parallel)\t\(.num_threads)\t\(.full_concurrency_fraction*10000|round/10000)\t\(.lock_acquires_mean | round )\t\(.lock_acquires_stddev_over_mean * 10000 | round / 10000)\t\(.per_thread_stats | map(.lock_acquires) | sort | join(","))"
EOF

#echo "$CMD"
#exit

(
echo -e "crit\tpar\tnthrds\tfcf\tlock_acquires_mean\tlock_acquires_stddev/mean\tlock_acquires_each_thread\n"
jq -r "$CMD" "$@"
) | column -t
