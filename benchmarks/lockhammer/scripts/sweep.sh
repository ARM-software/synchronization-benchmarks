#!/bin/bash

# Copyright (c) 2017, The Linux Foundation. All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of The Linux Foundation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
# ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
# BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
# IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

cores=$(grep -c "^processor" /proc/cpuinfo)
cores_q1=$(($cores / 4))
cores_q2=$(($cores / 2))
cores_q3=$(($cores_q1 + $cores_q2))
cores_all="`seq 48` `seq 8 8 $(($cores))` $cores_q1 $cores_q2 $cores_q3 $cores"
cores_sort=$(echo $cores_all | tr ' ' '\n' | sort -nu)
for c in $cores_sort
do
	if (( $c <= $cores ))
	then
		acquires=50000
		if (( $c > 8 ))
		then
			acquires=$((${acquires}*8/$c))
			if (( $acquires < 1000 ))
			then
				acquires=1000
			fi
		fi

		echo Test: ${1} CPU: exectx=$c Date: `date` 1>&2
		sudo ../build/lh_${1} -t $c -a ${acquires} -c ${2} -p ${3}
		sleep 5s
	fi
done
