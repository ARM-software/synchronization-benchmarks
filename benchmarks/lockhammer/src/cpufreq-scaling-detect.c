
/*
 * Copyright (c) 2024-2025, The Linux Foundation. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "verbose.h"


/*
 * Check if the cpufreq driver and governor are acceptable configurations.
 *
 * A good configuration runs all CPUs at scaling_max_freq regardless of
 * the number of threads (CPUs) active.
 *
 * A bad configuration reduces the CPU frequency based on load and then
 * takes some time to return to a higher frequency.
 *
 * Dynamic CPU frequency scaling is a complicated subject because there are
 * many different behaviors for driver and governor combinations.  These
 * behaviors also vary by processor and platform, so the frequency behavior
 * while under load must be examined using external analysis tools, such as
 * "perf stat -e cycles -I 1000 -C $cpu taskset -c $cpu busy-loop-program".
 *
 * In any event, warn if "conservative" or "ondemand" is used.
 */

typedef struct {
  const char * scaling_driver;
  const char * scaling_governor;
} scaling_t;

scaling_t known_good_cpufreq_scalers[] = {
  {.scaling_driver = "",             .scaling_governor = ""},  // for the case where there is no cpu scaling
  {.scaling_driver = "cppc_cpufreq", .scaling_governor = "performance"},
  {.scaling_driver = "acpi-cpufreq", .scaling_governor = "performance"},
};

scaling_t known_bad_cpufreq_scalers[] = {
  {.scaling_driver = NULL,           .scaling_governor = "conservative"},
  {.scaling_driver = NULL,           .scaling_governor = "ondemand"},
};



// get_proc_file_first_line - return the first line from the filename in a malloc'ed buffer, which must be free'd later
static char * get_proc_file_first_line (const char * filename) {
    FILE * f = fopen (filename, "r");
    if (f == NULL) {
//      system may not have cpufreq available
//      fprintf(stderr, "couldn't open %s for reading\n", filename);
        goto return_empty_string;
    }

    size_t line_len = 0;
    char * line = NULL; // have getline mallocate a buffer
    ssize_t nread;

    while ((nread = getline(&line, &line_len, f)) != -1) {
        //printf("nread = %zd, line_len = %zu, line = %s\n", nread, line_len, line);

        // getline includes the newline if it was there
        // nread includes the newline

        if (nread == 0) {
            break;
        }

        // strip newline if it exists
        if (line[nread-1] == '\n') {
            line[nread-1] = '\0';
        }

        fclose(f);
        return line;
    }

    // An offline CPU still has the cpufreq pseudo file present with read
    // permissions, but "Device or resource busy" when accessed, so getline
    // returns -1.

    fprintf(stderr, "unexpectedly getline() returned -1 from reading %s\n", filename);

    free(line);
    fclose(f);

return_empty_string:
    line = malloc(1);
    *line = '\0';
    return line;
}


static char * get_scaling_string(unsigned long cpunum, const char * filename) {
    char full_path[200];

    int r = snprintf(full_path, sizeof(full_path),
                 "/sys/devices/system/cpu/cpufreq/policy%lu/%s", cpunum, filename);

    if (r >= sizeof(full_path)) {
        fprintf(stderr, "couldn't construct full_path to read %s\n", filename);
        exit(-1);
    }

    return get_proc_file_first_line(full_path);
}

static char * get_scaling_governor(unsigned long cpunum) {
    return get_scaling_string(cpunum, "scaling_governor");
}

static char * get_scaling_driver(unsigned long cpunum) {
    return get_scaling_string(cpunum, "scaling_driver");
}

static char * get_scaling_max_freq(unsigned long cpunum) {
    return get_scaling_string(cpunum, "scaling_max_freq");
}

static char * get_scaling_min_freq(unsigned long cpunum) {
    return get_scaling_string(cpunum, "scaling_min_freq");
}

static int get_boost_value(void) {
    char * pc = get_proc_file_first_line("/sys/devices/system/cpu/cpufreq/boost");
    long x = strtol(pc, NULL, 0);   // XXX: if pc is empty string (due to the boost file not existing) this will parse to 0.
    free(pc);
    return x;
}

// return 1 if found in scaling_list
static int search_cpufreq_list(scaling_t * scaling_list, size_t list_len, char * scaling_driver, char * scaling_governor) {
    for (size_t i = 0; i < list_len; i++) {
        if (scaling_list[i].scaling_driver &&
            strcmp(scaling_driver, scaling_list[i].scaling_driver)) {
            continue;
        }

        if (scaling_list[i].scaling_governor &&
            strcmp(scaling_governor, scaling_list[i].scaling_governor)) {
            continue;
        }

        return 1;
    }

    return 0;
}

static int check_scaling_min_max_freq_are_equal(unsigned long cpunum, int ignore, int verbose, int suppress) {
    char * scaling_min_freq = get_scaling_min_freq(cpunum);
    char * scaling_max_freq = get_scaling_max_freq(cpunum);
    int are_equal = 1;

    if (strcmp(scaling_min_freq, scaling_max_freq)) {
        are_equal = 0;
        if (! suppress)
            printf("%s: CPU %lu scaling_min_freq != scaling_max_freq (%s to %s)\n",
                ignore? "WARNING" : "ERROR", cpunum, scaling_min_freq, scaling_max_freq);
    } else if (verbose >= VERBOSE_MORE)
        if (! suppress)
            printf("%s: CPU %lu scaling_min_freq == scaling_max_freq (%s to %s)\n",
                "INFO", cpunum, scaling_min_freq, scaling_max_freq);

    free(scaling_min_freq);
    free(scaling_max_freq);

    return are_equal;
}

// returns 1 if OK, 0 if not
int check_cpufreq_boost_is_OK(int ignore, int verbose, int suppress) {
    // OK == boost is off

    int boost = get_boost_value();
    if (verbose >= VERBOSE_MORE) {
        printf("boost = %d\n", boost);
    }

    if (0 == boost) {
        return 1;
    }

    if (! suppress)
        printf("%s: boost is enabled (value = %d), so CPU will adjust frequency based on load, which may affect performance.\n",
            ignore ? "WARNING" : "ERROR", boost);
    return ignore ? 1 : 0;
}

// returns 1 if OK, 0 if not
int check_cpufreq_governor_is_OK_on_cpunum (unsigned long cpunum, int ignore, int verbose, int suppress) {
    char * scaling_driver = get_scaling_driver(cpunum);
    char * scaling_governor = get_scaling_governor(cpunum);

    const size_t known_good_cpufreq_scalers_list_len = sizeof(known_good_cpufreq_scalers)/sizeof(known_good_cpufreq_scalers[0]);

    if (search_cpufreq_list(known_good_cpufreq_scalers, known_good_cpufreq_scalers_list_len, scaling_driver, scaling_governor)) {
        if (verbose >= VERBOSE_YES)
            printf("found on CPU %lu scaling {driver=%s, governor=%s}\n",
                    cpunum, scaling_driver, scaling_governor);
        free(scaling_driver);
        free(scaling_governor);

        if (check_scaling_min_max_freq_are_equal(cpunum, ignore, verbose, suppress) || ignore) {
            return 1;
        }

        return 0;
    }

    const size_t known_bad_cpufreq_scalers_list_len = sizeof(known_bad_cpufreq_scalers)/sizeof(known_bad_cpufreq_scalers[0]);

    if (search_cpufreq_list(known_bad_cpufreq_scalers, known_bad_cpufreq_scalers_list_len, scaling_driver, scaling_governor)) {
        if (! suppress)
            printf("%s: On CPU %lu, cpu scaling {driver=%s, governor=%s} is known bad\n",
                ignore ? "WARNING" : "ERROR", cpunum, scaling_driver, scaling_governor);
        free(scaling_driver);
        free(scaling_governor);
        return 0;
    }

    // The intel_pstate driver's "performance" governor adjusts the frequency
    // based on load.  This may be done by the operating system or autonomously
    // by the processor itself.  Other cpufreq "performance" governors have the
    // CPU run at or in between the scaling_min/max_freq limit parameters, and
    // by setting them equal, the CPU should run at that freq.  However, with
    // intel_pstate, setting the min/max frequency limits to be equal does not
    // ensure that the CPU operates at that specific frequency.  For example, a
    // CPU may run at an operating frequency lower than the min frequency if
    // enough cores are active.

    if ((0 == strcmp(scaling_driver, "intel_pstate"))) {
        if (! suppress)
            printf("%s: intel_pstate governor detected on CPU %lu may change frequency depending on the load\n",
                ignore ? "WARNING" : "ERROR", cpunum);

        if (check_scaling_min_max_freq_are_equal(cpunum, ignore, verbose, suppress)) {
            if (! suppress)
                printf("%s: intel_pstate CPU %lu frequency limits are equal, but the actual operating frequency may be autonomously determined by the hardware. Please check using perf stat!\n",
                ignore ? "WARNING" : "ERROR", cpunum);
        }

        // Either way, we can't really do anything about it, other than inform which driver+governor is in use

        free(scaling_driver);
        free(scaling_governor);
        return ignore;
    }

    if (! suppress)
        printf("%s: On CPU %lu, cpu scaling {driver=%s, governor=%s} is not known good nor known bad\n",
            ignore ? "WARNING" : "ERROR", cpunum, scaling_driver, scaling_governor);

    // TODO: for {driver!=intel_pstate,governor=any} and {driver=intel_pstate,governor=any},
    // check that scaling_max_freq and scaling_min_freq are equal so that frequency is not
    // changed due to load.

    free(scaling_driver);
    free(scaling_governor);

    return 0;
}

#ifdef TEST
int is_scaling_governor(unsigned long cpunum, const char * expect_governor) {
    char * governor_string = get_scaling_governor(cpunum);

    if (governor_string) {
        int retval = strcmp(governor_string, expect_governor);
        free(governor_string);
        return retval;
    }

    return 0;
}

int main (int argc, char ** argv) {

    const char * expect_governor = "ondemand";
    unsigned long cpunum = 0;

    if (argc > 1) {
        cpunum = strtoul(argv[1], NULL, 0);
        if (argc > 2) {
            expect_governor = argv[2];
        }
    }

    if (is_scaling_governor(cpunum, expect_governor)) {
        printf("scaling_governor for cpu%lu is %s\n", cpunum, expect_governor);
    } else {
        printf("scaling_governor for cpu%lu is not %s\n", cpunum, expect_governor);
    }

    char *test_string = get_proc_file_first_line("/proc/this-does-not-exist");
    printf("test_string = %s, strlen = %zu\n", test_string, strlen(test_string));
    free(test_string);

    return 0;
}
#endif

/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
