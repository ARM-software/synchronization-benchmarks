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

./sweep.sh incdec_refcount 0 0 > incdec_refcount_0_0_$HOSTNAME.csv
./sweep.sh cas_lockref 0 0 > cas_lockref_0_0_$HOSTNAME.csv
./sweep.sh cas_lockref 2000 1000 > cas_lockref_2000_1000_$HOSTNAME.csv
./sweep.sh ticket_spinlock 0 0 > ticket_spinlock_0_0_$HOSTNAME.csv
./sweep.sh ticket_spinlock 1000 5000 > ticket_spinlock_1000_5000_$HOSTNAME.csv
./sweep.sh queued_spinlock 0 0 > queued_spinlock_0_0_$HOSTNAME.csv
./sweep.sh queued_spinlock 1000 5000 > queued_spinlock_1000_5000_$HOSTNAME.csv
./sweep.sh event_mutex 0 0 > event_mutex_0_0_$HOSTNAME.csv
./sweep.sh event_mutex 1000 5000 > event_mutex_1000_5000_$HOSTNAME.csv
./sweep.sh cas_event_mutex 0 0 > cas_event_mutex_0_0_$HOSTNAME.csv
./sweep.sh cas_event_mutex 1000 5000 > cas_event_mutex_1000_5000_$HOSTNAME.csv
./sweep.sh cas_rw_lock 0 0 > cas_rw_lock_0_0_$HOSTNAME.csv
./sweep.sh cas_rw_lock 2000 1000 > cas_rw_lock_2000_1000_$HOSTNAME.csv
./sweep.sh hybrid_spinlock 0 0 > hybrid_spinlock_0_0_$HOSTNAME.csv
./sweep.sh hybrid_spinlock 1000 5000 > hybrid_spinlock_1000_5000_$HOSTNAME.csv
./sweep.sh hybrid_spinlock_fastdequeue 0 0 > hybrid_spinlock_fastdequeue_0_0_$HOSTNAME.csv
./sweep.sh hybrid_spinlock_fastdequeue 1000 5000 > hybrid_spinlock_fastdequeue_1000_5000_$HOSTNAME.csv
./sweep.sh empty 0 0 > empty_0_0_$HOSTNAME.csv
./sweep.sh jvm_objectmonitor 0 0 > jvm_objectmonitor_0_0_$HOSTNAME.csv
./sweep.sh jvm_objectmonitor 1000 5000 > jvm_objectmonitor_1000_5000_$HOSTNAME.csv
./sweep.sh swap_mutex 0 0 > swap_mutex_0_0_$HOSTNAME.csv
./sweep.sh swap_mutex 1000 5000 > swap_mutex_1000_5000_$HOSTNAME.csv
