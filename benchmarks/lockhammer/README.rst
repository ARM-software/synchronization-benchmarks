Lockhammer
==========

Lockhammer is a simple lock and synchronization performance evaluation tool
which can be used to characterize the performance of high core-count systems or
compare software algorithms.  Several basic primitives are included and
third-party algorithms can easily be integrated.

License
-------

The software is provided under a BSD-3-Clause `license`_.

This project contains code from other projects, the license information for
which can be found in the relevant directories or files. Any contributions to
third party open source projects are under the relevant license for that
project or file.

Build & Run
===========

Simply 'make' in this directory to produce a lockhammer executable for each
supported algorithm.

The Makefile supports multiple build configuration variants selected using
USe_* variables given to make.  The following example uses the __atomic
intrinsics provided by the compiler instead of assembly routines provided by
lockhammer (USE_BUILTIN=1) and an empty asm volatile ("") statement for the
cpu_relax() macro (USE_RELAX+empty).

	# build binaries into build.builtin.relax_empty
	make USE_BUILTIN=1 USE_RELAX=empty

	# delete binaries
	make USE_BUILTIN=1 USE_RELAX=empty clean

	# make preprocessed file.i for debugging
	make USE_BUILTIN=1 USE_RELAX=empty alli

See the comments at the top of the Makefile for a description of the variables.

Because typing out all of the combinations of variables is tedious, the
Makefile supports the phony target 'allvariants' that will build all of the
combinations.  The Makefile also supports parallel builds.

	# build all variants using 8 jobs at a time
	make -j 8 allvariants

	# make preprocessed file.i for all variants
	make allivariants

	# delete binaries of all variants
	make cleanallvariants

	# remove all files built for all variants
	make clobberallvariants

In order to more accurately characterize performance, lockhammer may be invoked
to select the FIFO scheduler (using the flag "-S FIFO").  If this flag is used,
then the test must be run as root.  Be aware that running FIFO scheduled
threads on all cores for extended periods of time can result in responsiveness
and stability issues.  When using this mode, lockhammer should not be run on an
already-deployed system and parameters such as acquires per thread and critical
section lengths should be tuned to ensure the entire run lasts for a short
period of time.

Some simple scripts for running sweeps of different core counts is available in
the `scripts/`_. directory.

- scripts/run-tests.sh is a new script that runs a series of test combinations.
  The VARIANT_LIST, TEST_LIST, CRIT_NS_LIST, and PAR_NS_LIST variables in the
  script can be edited to select which ones to run.  Each test generates a JSON
  file.

- scripts/view-results-json.sh is a new script that uses jq to summarize the
  measurements from one or more JSON files.  Run 'scripts/view-results-json.sh
  -h' for help.

Locks or synchronization primitives are stressed by acquiring and releasing
them at a rate determined by a command-line selectable number of critical and
post-release (parallel) wait loop iterations.  However, the previous default of
0 instructions for critical and 0 instructions for parallel are not realistic
configurations.  The durations for these parameters must now be explicitly
specified using the -c and -p flags.  (The values of -c 0 and -p 0 are still
allowed, but are no longer the default.)

Detailed information about each run is printed to stdout.  It can also be
saved to a JSON file.


Software Dependencies
---------------------

+ gcc or clang

Optional:
+ Jansson json library
+ jq

Optional (for older scripts):
+ python3
+ sh python3 module
+ yaml python3 module

Guide for Ubuntu 24.04
----------------------
apt install python3-sh libjansson-dev jq
git clone https://github.com/ARM-software/synchronization-benchmarks.git
cd synchronization-benchmarks/benchmarks/lockhammer
make allvariants
scripts/run-tests.sh

Guide for Redhat Enterprise Linux 8.1 (old; not recently tested)
----------------------------------------------------------------
dnf install hwloc-gui
pip3 install sh
git clone https://github.com/ARM-software/synchronization-benchmarks.git
cd synchronization-benchmarks/benchmarks/lockhammer
make
cd scripts
./runall.sh



Usage
=====

The build system will make a directory named for the build configuration and
place into it a separate lockhammer binary for each test named in the format
lh_[testname].

	$ ls -1 build.relax_pause/lh_*
	build.relax_pause/lh_cas_event_mutex
	build.relax_pause/lh_cas_lockref
	build.relax_pause/lh_cas_rw_lock
	build.relax_pause/lh_clh_spinlock
	build.relax_pause/lh_empty
	build.relax_pause/lh_event_mutex
	build.relax_pause/lh_incdec_refcount
	build.relax_pause/lh_jvm_objectmonitor
	build.relax_pause/lh_osq_lock
	build.relax_pause/lh_queued_spinlock
	build.relax_pause/lh_swap_mutex
	build.relax_pause/lh_tbb_spin_rw_mutex
	build.relax_pause/lh_ticket_spinlock


The minimum required flags to run a test:

	* critical duration, one or more of the following:
		-c instructions, --ci instructions
		-c nanoseconds, --cn nanoseconds

	* parallel duration, one or more of the following:
		-p instructions, --pi instructions
		-p nanoseconds, --pn nanoseconds

	* workload length (time-based), one of the following
		-T float_seconds
		-O hwtimer_ticks

	- OR -

	* workload length (work-based) (not preferred)
		-a num_acquires

Example:

	$ build.relax_pause/lh_ticket_spinlock -c 500 -p 500 -T 2

	Starting test_name=ticket_spinlock variant_name=relax_pause test_type=
	INFO: setting thread count to the number of available cores (24).
	Finished test_name=ticket_spinlock variant_name=relax_pause test_type=
	po  name cpus
	0        0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23

	po  meas  test  iter  thrds | cpu_ns/lock - crit_ns - par_ns  = overhead_ns % | lasom | locks/wall_sec
	0   1     1     1     24    | 7045          113       113       6820      97% | 0.004 | 3406366


When multiple -c, -p, or -o / -t flags are used, all permutations of these
settings will be run.


The supported flags are shown when a lockhammer binary is run with the -h flag:
::
build.relax_pause/lh_empty [args]

processor affinity selection (need at least one of -t or -o; permuted on each):
 -o | --pinning-order   n,[n,[n...]]          a list of CPUs on which to run
 -t | --num-threads     threads[:interleave]  number of threads to use
        interleave = 0  Enumerate CPUs from the existing affinity mask (this is the default)
        interleave >= 1 algorithmically increment CPU number, e.g. -t 3:2 means CPU 0,2,4
                        Overrides processor affinity mask, or misplace on an offline CPU.
 -C | --cpuorder-file         filename        for -t/--num-threads, allocate CPUs by number in the order in this text file

lock durations (at least one of both critical and parallel duration must be specified; will be permuted):
 -c | --critical              duration[ns|in] critical duration measured in nanoseconds (use "ns" suffix) or instructions (use "in" suffix; default is "in" if omitted)
 -p | --parallel              duration[ns|in] parallel duration measured in nanoseconds (use "ns" suffix) or instructions (use "in" suffix; default is "in" if omitted)
--cn| --critical-nanoseconds  nanoseconds     upon acquiring a lock, duration to hold the lock ("-c 1234ns" equivalent)
--ci| --critical-instructions instructions    upon acquiring a lock, number of spin-loop instructions to run while holding the lock ("-c 1234in" equivalent)
--pn| --parallel-nanoseconds  nanoseconds     upon releasing a lock, duration to wait before attempting to reacquire the lock ("-p 1234ns" equivalent)
--pi| --parallel-instructions instructions    upon releasing a lock, number of spin-loop instructions to run while before attempting to reacquire the lock ("-p 1234in" equivalent)

experiment iterations:
 -n | --iterations            integer         number of times to run each measurement

experiment length (work-based):
 -a | --num-acquires          integer         number of acquires to do per thread

experiment length (time-based):
 -T | --run-limit-seconds     float_seconds   each worker thread runs for this number of seconds
 -O | --run-limit-ticks       integer         each worker thread runs for this number of hardware timer ticks
 -I | --run-limit-inner-iterations  integer   number of inner iterations of measurement between hardware timer polls
      --hwtimer-frequency     freq_hertz      Override HW timer frequency in Hertz instead of trying to determine it
      --estimate-hwtimer-frequency cpu_num    Estimate HW timer frequency on cpu_num
      --timeout-usecs         integer         kill benchmark if it exceeds this number of microseconds

scheduler control:
 -S | --scheduling-policy     FIFO|RR|OTHER   set explicit scheduling policy of created threads (needs root)

memory placement control (hugepages):
 -M | --hugepage-size  <integer|help|default> use hugetlb page for lock memory; see "-M help" for sizes
      --hugepage-offset       integer         if --hugepage-size is used, the byte offset into the hugepage for the tests' lock
      --hugepage-physaddr     physaddr        obtain only the hugepage with the physaddr specified (must run as root)
      --print-hugepage-physaddr               print the physical address of the hugepage obtained, and then exit (must run as root)

other:
      --json filename                         save results to filename as a json
 -Y | --ignore-unknown-scaling-governor       do not exit as error if CPU scaling driver+governor is known bad/not known good
 -Z | --suppress-cpu-frequency-warnings       suppress CPU frequecy scaling / governor warnings
 -v | --verbose                               print verbose messages (use 2x for more verbose)
      --more-verbose                          print more verbose messages

lock-specific:
 -- <workload-specific arguments>             lock-specific arguments are passed after --


Plotting
========

The default plotting script utilizes jupyter-notebook, matplotlib, seaborn
and pandas under python3 environment. For Ubuntu on x86_64 machine, the
following packages have to be installed:
apt install build-essential python3 python3-pip jupyter-notebook

For aarch64 machine, additional packages are also needed:
apt install pkg-config libfreetype6-dev python3-scipy

Then pip3 can install all plotting related libraries with the following cmd:
pip3 install matplotlib seaborn pandas numpy

Note, seaborn has to be installed without scipy as dependency on aarch64:
pip3 install seaborn --no-dependencies

The jupyter-notebook can be started with:
jupyter-notebook --ip 0.0.0.0 --port=8888

Now any browser should be able to access the jupyter notebook called:
lockhammer-jupyter-notebook.ipynb

Start a browser, with IP address set to the jupyter server IP and port 8888:
e.g. http://example.test.com:8888

Click the notebook named lockhammer-jupyter-notebook.ipynb, run each cell one
by one and jupyter should be able to generate the png graph locally.


Using run-tests.sh and view-results-json.sh
-------------------------------------------

run-tests.sh and view-results-json.sh can be found in the scripts subdirectory.
These scripts facilitate running many tests and summarizing the measurements.

run-tests.sh runs the test binaries and variants found by permuting the entries
in its VARIANT_LIST, TEST_LIST, CRIT_NS_LIST, and PAR_NS_LIST variables.  The
script can be run with no arguments, but this will then try to run many
permutations which will take hours.  Editing the variables in the script is
advised to help focus the intent of measurements.


	scripts/run-tests.sh


Each test's results are stored in a json file named $HOSTNAME_S.$test.$BUILD_VARIANT.json

These jsons can be summarized using the view-results-json.sh script, which will
display a summary of all of the jsons given to it in one table.


	scripts/view-results-json.sh \*.json


The view-results-json.sh help message:

./view-results-jsons.sh [options] json [json ...]

select options:
-c crit           nominal critical time/inst parameter (repeatable)
-p par            nominal parallel time/inst parameter (repeatable)
-t num_threads    number of threads (repeatable)
-v variant_name   variant name (repeatable)

sort options:
-s sort_string    sort string (default is by '.num_threads')
-s help           print short header to .key mapping
-r                reverse the sort

output options:
-D                dump the records in a json array

-h                print this usage help message


Example:

# list all data with threads=8, parallel=1000 or parallel=500, and critical=0
# from files \*osq_lock\*.json, sort by overhead %

./view-results-json.sh -s overhead_% -t 8 -p 1000 -p 500 -c 0 \*osq_lock\*.json



Persistent Physical Memory for Locks Using Persistent Hugepages
===============================================================

The physical address of a lock variable may affect its
performance.  For example, the address may be in a different NUMA
domain than the originating domain.  Furthermore, memory
performance can be different at granularities much smaller than a
NUMA domain, such as a cache line.  However, the OS will provide
a different virtual-to-physical memory mapping between
invocations of a program.  Obtaining the same physical memory
between runs can help to eliminate the randomization as a source
of run-to-run performance variability.

One way to reobtain the same physical memory is to reuse a
persistent hugepage.  A "hugepage" in this context refers to the
type of huge page that is associated with hugetlbfs (see [1]) and
NOT the type provided by Transparent HugePage Support (THP).

[1] https://docs.kernel.org/admin-guide/mm/hugetlbpage.html

* A reserved, persistent hugepage is not movable nor decomposable
  into base pages, so it maps to the same contiguous physical
  memory between runs.

* A single hugepage makes it easy for the same hugepage (and
  therefore the same physical memory) to be mapped again
  between runs.

* Alternatively, if root access is available, the
  --hugepage-physaddr flag can be used to try to request a hugepage
  with a specific physical address, which will help reproducibility.


Allocating a single hugepage
----------------------------

A hugepage can either be reserved at kernel boot or allocated
afterwards if there is sufficient unfragmented memory.

A hugepage at kernel boot can be allocated using the kernel
parameters hugepagesz=<size> and hugepages=<N>.  The following
parameters allocate one 1GB hugepage.  Note that these parameters
are position-sensitive, and must be specified in the order shown.

   hugepagesz=1G hugepages=1

However, the kernel distributes the allocation of hugepages
across NUMA nodes, so if only 1 hugepage is allocated, it will be
only in one node that has the hugepage.

Alternatively, a hugepage can be allocated after boot on a
desired NUMA node.  The following instructions allocate 1 (and
only 1) persistent hugepage of a supported size on the desired
NUMA node, leaving 0 hugepages on all other nodes.

Commands to run as root:

# deallocate all 1GB hugepages on all NUMA nodes
for a in /sys/devices/system/node/node*/hugepages/hugepages-1048576kB/nr_hugepages
do
    echo 0 > $a
done

# allocate a single 1 1GB hugepage on NUMA node 0
echo 1 > /sys/devices/system/node/node0/hugepages/hugepages-1048576kB/nr_hugepages


Running Lockhammer with a Hugepage
----------------------------------

Invoke lockhammer with the --hugepage-size flag for the size
of the single hugepage allocated.  This will cause the benchmark
to map memory for the locks using mmap() with the MAP_HUGETLB flag.

    lh_cas_lockref --hugepage-size 1g --hugepage-offset 64

The --hugepage-offset flag provides even finer control over the
physical address within the hugepage by specifying the byte offset
in the page for the position of the lock.  The byte offset must
be a multiple of 8; the default offset is 0 bytes.


Requesting a hugepage by physical address
-----------------------------------------

When lockhammer is run as root (e.g., by invoking it with sudo),
the physical address of the hugepage allocated will be printed in the
output.

    $ sudo build.relax_pause/lh_empty  -M default  -T 10 -o 8,9,10 --ignore-unknown-scaling-governor
    using mmap with hugepagesz = default
    determining timer frequency ...
    found it as 2300000000 Hz (which could be wrong, use --estimate-timer-frequency to measure and --timer-frequency to override)
    INFO: assuming default hugepage size is 2MB!
    INFO: hugepage physaddr = 0x25c400000
                              ^^^^^^ physical address of the hugepage

On a subsequent run, the --hugepage-physaddr flag can then be used to
map the same hugepage by physical address.  Lockhammer will try to
llocate up to 10 hugepages to find one that has the requested physical
address.

    $ sudo build.relax_pause/lh_cas_rw_lock  -M default  -T 10 -o 8,9,10 --ignore-unknown-scaling-governor --hugepage-physaddr 0x25c400000
    using mmap with hugepagesz = default
    determining timer frequency ...
    found it as 2300000000 Hz (which could be wrong, use --estimate-timer-frequency to measure and --timer-frequency to override)
    INFO: assuming default hugepage size is 2MB!
    INFO: hugepage physaddr = 0x25c400000
                              ^^^^^^ the physical address is reused


Allocation order of CPUs
========================

Each worker thread is pinned to a CPU using sched_setaffinity().

The CPU number is determined based on the following:

* explicit CPU pinorder (-o pinorder)

    This places threads on CPU1, CPU2, and CPU3:

        -o 1,2,3

* number of threads (-t/--num-threads) starting from CPU0

    This places threads on CPU0, CPU1, and CPU2:

        -t 3

    Note: this is implicitly -t num_cpus:interleaving with
    interleaving=0, and will not allocate on an offline CPU

* number of threads with interleaving (-t num_cpus:interleaving)

    This changes the ordering of -t by skipping CPU numbers.

    For example, this runs on CPU0, CPU2, and CPU4.

        -t 3:2

    Note the CPU number is calculated by a formula and may
    select a CPU that is offline or not schedulable.

    The special case of interleaving=0 allocates CPUs using the existing
    CPU affinity mask to allow for discontiguous CPU numbers, such as a
    system with disabled/offline CPUs.

When -t/--num-threads is used, CPUs are allocated starting from CPU 0
and up.  This can be changed by using the --cpuorder-file flag with a text
file that contains the CPU numbers from which to allocate.  For example,
if the file contains:

        0 4 2 6 1 5 3 7

Then invoking the benchmark with the flag -t 4 will allocate threads on
CPU0, CPU4, CPU8, and CPU2 (instead of sequentially from CPU0-CPU3).



Importance of Disabling Frequency Scaling
=========================================

set cpu frequency governor to performance

for cpu in {0..127}; do
sudo cpufreq-set -g performance -c $cpu
done

CPU frequency scaling
---------------------

Dynamic CPU frequency scaling may opportunistically increase performance by
running certain cores at a higher frequency when they are loaded.  On some
systems, the frequency under load depends on the number of other loaded CPUs
and other factors.  Thus, the per-thread performance reported by the
lockhammer benchmark may be higher when running on a few CPUs than when
running on a large number of CPUs.  The per-thread performance may also be
different when running within or between a logical or physical partition,
such as a NUMA node or chiplet.

Lockhammer inspects the CPU frequency scaling configuration to warn if the
governor and frequency limits are not uniform across the target CPUs.  It
also detects if the processor boosting control is enabled, which may
increase the frequency above an all-core base frequency specified by the
CPU frequency governor.  When enabled, these features may require further
inspection and analysis to comprehend the benchmark's results.

Here are some example commands to disable CPU frequency scaling features.

Using cpufreq-utils to set base frequency to 2.2 GHz on CPUs 0-63 (acpi-cpufreq driver)
	$ sudo bash -c 'for a in {0..63}; do cpufreq-set -g performance -d 2.2g -u 2.2g -c $a ; done'

Disable processor boosting control (depends on the system and driver)
	$ echo 0 | sudo tee -a /sys/devices/system/cpu/cpufreq/boost

Note that some CPU frequency drivers expose controls to operate above the
base core frequency, while other dirvers do not.  The configuration of
the CPU frequency setting is platform specific, so the commands shown
above may not work.
