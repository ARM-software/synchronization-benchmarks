Lockhammer
==========

This is a simple locks and sychronization performance evaulation tool which can
be used to characterize the performance of high core-count systems or compare
software algorithms.  Several basic primitives are included and third-party
algorithms can easily be integrated.

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
supported algorithm.  In order to more accurately characterize performance
lockhammer selects the FIFO scheduler and as such must be run as root.  Be
Aware that running FIFO scheduled threads on all cores for extended periods
of time can result in responsiveness and stability issues.  Lockhammer should
never be run on an already-deployed  system and parameters such as acquires
per thread and critical section lengths should be tuned to ensure the entire
run lasts for a short period of time.  Some simple scripts for running sweeps
of different core counts is available in the `scripts/`_. directory.

Locks or synchronization primitives are stressed by acquiring and releasing
them at a rate deterimined by a command-line selectable number of critical
and post-release wait loop iterations.  The default is to not wait between
acquire and release or re-acquire attempts (acquire and release as quickly
as possible).

Detailed information about each run is printed to stderr while a CSV summary
is printed to stdout.

Usage
=====

The build system will generate a separate lockhammer binary for each test with
the format lh_[testname]. Each lockhammer binary accepts the following options:
        [-t threads]    Number of threads to exercise, default online cores
        [-a acquires]   Number of acquisitions per thread, default 50000
        [-c critical]   Critical section in loop iterations, default 0
        [-p parallel]   Parallelizable section in loop iterations, default 0
        [-s]            Run in safe mode, default no
