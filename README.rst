Synchronization Benchmarks
==========================

This is a micro-benchmarks suite targeting evaluation of synchronization primitives used primarily
in data-center application and system software by evaluating their scalability and code overhead.  It contains synchronization 
primitives that are both independently developed and extracted from real software applications.

License
-------

The software is provided under a BSD-3-Clause `license`_. Contributions to this
project are accepted under the same license with developer sign-off as
described in the `Contributing Guidelines`_.

This project contains code from other projects, the license information for which
can be found in the relevant directories or files. Any contributions to third party
open source projects are under the relevant license for that project or file.

Repository Contents
===================

The synchronization-benchmarks repository is divided up into multiple directories with the following semantics:

- tools/ -- Contains support tools for the micro-benchmarks contained in benchmarks/ such as application profilers or code
  analyzers.  In general, support code that applies to multiple benchmarks should go here.
- benchmarks/ -- Broken up into sub-directories, one for each micro-benchmark.  Each sub-directory should general be structured
  as:

  - / -- The root of the directory should contain a README with build instructions, and a detailed
    description of the test: what it is testing, how it is testing, and how to interpret the results. The root
    directory should also contain the build system files.
  - src/
  - include/
  - scripts/ -- Automation scripts for running and parsing the output of your micro-benchmark

- ext/ -- This is a directory for third party code taken from other projects if for instance your micro-benchmark is
  meant for testing example synchronization primitives for various sources.  For each third party source, a sub-directory
  should be created that is descriptive of the origin of the imported code and the imported code placed in that sub-directory.  
  All imported code needs to retain the original license and copyright information from the source location.  
  For more detail on how to include third party code, please consult the `Contributing Guidelines`_.

Getting Started
===============

Clone this repository and add the commit-msg hook from the hooks/ directory into your .git/hooks directory.  To build
the microbenchmarks, follow the build and run instructions in the individual test sub-directories contained
in benchmarks/.

Feedback and support
--------------------

Arm welcomes any feedback on this benchmark suite.  If you find that this suite lacks important
tests, please use the `Github issue tracker`_ to log the issue and initiate a pull request with your fixes as outlined in
the `Contributing Guidelines`_.

--------------

*Copyright (c) 2018, ARM Limited and Contributors. All rights reserved.*

.. _GitHub: https://www.github.com/ARM-software/synchronization-benchmarks
.. _GitHub issue tracker: https://github.com/ARM-software/synchronization-benchmarks/issues
.. _license: ./license.rst
.. _Contributing Guidelines: ./contributing.rst
