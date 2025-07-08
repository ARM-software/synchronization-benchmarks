
/*
 * Copyright (c) 2017-2025, The Linux Foundation. All rights reserved.
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



#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <linux/mman.h>
#include <sys/types.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "verbose.h"
#include "alloc.h"
#include "pagemap.h"

typedef struct {
    void * va;
    uintptr_t pa;
} hugepage_mapping_t;

static void unmap_hugepage_mappings(hugepage_mapping_t * mappings, size_t n, size_t length) {
    for (size_t i = 0; i < n; i++) {
        void * va = mappings[i].va;
        int ret = munmap(va, length);
        if (ret == -1) {
            fprintf(stderr, "ERROR: munmap(%p, %zu) failed!, i = %zu\n", va, length, i);
            exit(-1);
        }
    }
}

void print_hugepage_physaddr_and_exit(void * mmap_ret) {
    printf("0x%lx\n", get_phys_addr((uintptr_t) mmap_ret));
    exit(0);
}


void * do_hugepage_alloc(int use_hugepages, size_t hugepage_req_physaddr, int verbose) {

    size_t length;
    int hugepage_size_flag; // will be set to the mmap flag specifying the hugepage size

    switch (use_hugepages) {
        case HUGEPAGES_DEFAULT:
            // TODO: get the default hugepagesize from /proc/meminfo instead of hardcoding it here.
            hugepage_size_flag = MAP_HUGE_2MB;
            length = 2*1024ULL*1024ULL;
            printf("INFO: assuming default hugepage size is 2MB!\n");
            break;
        case HUGEPAGES_64K:
            hugepage_size_flag = MAP_HUGE_64KB;
            length = 64*1024ULL;
            break;
        case HUGEPAGES_2M:
            hugepage_size_flag = MAP_HUGE_2MB;
            length = 2*1024ULL*1024ULL;
            break;
        case HUGEPAGES_32M:
            hugepage_size_flag = MAP_HUGE_32MB;
            length = 32*1024ULL*1024ULL;
            break;
        case HUGEPAGES_512M:
            hugepage_size_flag = MAP_HUGE_512MB;
            length = 512*1024ULL*1024ULL;
            break;
        case HUGEPAGES_1G:
            hugepage_size_flag = MAP_HUGE_1GB;
            length = 1024ULL*1024ULL*1024ULL;
            break;
        case HUGEPAGES_16G:
            hugepage_size_flag = MAP_HUGE_16GB;
            length = 16*1024ULL*1024ULL*1024ULL;
            break;
        default:
            fprintf(stderr, "ERROR: shouldn't have gotten here!\n");
            exit(-1);
    }

#define MAX_MMAP_TRIES 10

    hugepage_mapping_t mmap_try[MAX_MMAP_TRIES];

    for (size_t mmap_tries = 0; mmap_tries < MAX_MMAP_TRIES; mmap_tries++) {

        // NOTE: mmap() will round-up length to the hugepage size
        void * mmap_ret = mmap(NULL, length,
                PROT_READ|PROT_WRITE,
                MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB|MAP_POPULATE|hugepage_size_flag,
                -1, 0);

        // if we got a hugepage mapping and we don't have a requested physaddr, we are done.
        if (mmap_ret != MAP_FAILED && ! hugepage_req_physaddr) {
            if (verbose >= VERBOSE_YES && geteuid() == 0) {
                printf("INFO: hugepage physaddr = 0x%lx\n", get_phys_addr((uintptr_t) mmap_ret));
            }
            return mmap_ret;
        }

        // if we got a hugepage mapping..
        if (mmap_ret != MAP_FAILED && hugepage_req_physaddr) {
            size_t pa = get_phys_addr((uintptr_t) mmap_ret);
            // printf("va = %p, pa = 0x%lx\n", mmap_ret, pa);
            // .. and it matched, we are done.  unmap the previous attempts ones.
            if (pa == hugepage_req_physaddr) {
                // got the pa we wanted, so unmap the other mappings
                unmap_hugepage_mappings(mmap_try, mmap_tries, length);
                if (verbose >= VERBOSE_YES)
                    printf("INFO: hugepage physaddr = 0x%lx\n", pa);
                return mmap_ret;
            }

            // .. but if it was not the physaddr we wanted, remember the mapping
            mmap_try[mmap_tries].va = mmap_ret;
            mmap_try[mmap_tries].pa = pa;

#if 0
            printf("mmap_tries = %zu\n", mmap_tries);
            for (size_t i = 0; i <mmap_tries; i++) {
                printf("mmap_try[%zu].va = %lx, mmap_try[%zu].pa = %lx\n",
                        i, (unsigned long) mmap_try[i].va, i, mmap_try[i].pa);
            }
#endif

            continue;
        }

        // didn't get a hugepage mapping.
        if (mmap_ret == MAP_FAILED) {
            printf("mmap returned %p (MAP_FAILED). Exiting!\n"
                    "Some possible reasons for failure:\n"
                    " 1. no HugeTLB hugepage support is enabled,\n"
                    " 2. no hugepages of the requested size are available,\n"
                    " 3. the hugepage with the requested physaddr is already in use,\n"
                    " 4. it is not available on a NUMA node available to us.\n"
                    "Try:\n"
                    " sudo apt-get install libhugetlbfs-bin\n"
                    " sudo hugeadm --create-global-mounts\n"
                    " sudo hugeadm --pool-pages-max DEFAULT:+1000\n"
                    "(Only the last line is needed after a reboot.)\n"
                    "Or manually allocate a hugepage:\n"
                    "  echo 1 | sudo tee -a /sys/kernel/mm/hugepages/hugepages-%llukB/nr_hugepages\n"
                    "Or use \"hugepagesz=<size> hugepage=<n>\" kernel parameters.\n",
                    mmap_ret, length / 1024ULL);
            exit(-1);
        }

        printf("ERROR: should not have gotten here!\n");
        exit(-1);
    }

    printf("After %u tries, did not find the requested hugepage with physaddr = 0x%lx\n",
            MAX_MMAP_TRIES, hugepage_req_physaddr);
    unmap_hugepage_mappings(mmap_try, MAX_MMAP_TRIES, length);
    exit(-1);

    // satisfy the compiler
    return NULL;
}



// this tracks heap-allocated memory for release at the end of main

// don't actually track the allocations and let the OS clean up
#define DYNAMIC_LOCK_MEMORY_TRACKING_NOOP

// printf messages to show the stack
//#define DEBUG_DYNAMIC_LOCK_MEMORY

size_t num_dynamic_lock_memory = 0;
void * (*dynamic_lock_memory_base) = NULL;

void push_dynamic_lock_memory(void * p) {
#ifdef DYNAMIC_LOCK_MEMORY_TRACKING_NOOP
    (void) p;
    return;
#endif

    void * b = reallocarray(dynamic_lock_memory_base,
         num_dynamic_lock_memory + 1, sizeof(dynamic_lock_memory_base[0]));

    if (b == NULL) {
        perror("reallocarray failed in append_dynamic_lock_memory");
        exit(EXIT_FAILURE);
    }

#if 0
    // for seeing if reallocarray moved it to a completely different address
    if (dynamic_lock_memory_base && dynamic_lock_memory_base != b) {
        printf("INFO: dynamic_lock_memory_base = %p -> %p\n", dynamic_lock_memory_base, b);
    }
#endif

    dynamic_lock_memory_base = b;
    dynamic_lock_memory_base[num_dynamic_lock_memory++] = p;

#ifdef DEBUG_DYNAMIC_LOCK_MEMORY
    printf("push dynamic_lock_memory_base[%zu] = %p\n", num_dynamic_lock_memory - 1, p);
    for (size_t i = 0; i < num_dynamic_lock_memory; i++) {
        printf(" dynamic_lock_memory_base[%zu] = %p\n", i, dynamic_lock_memory_base[i]);
    }
#endif
}

void pop_dynamic_lock_memory(void * p) {
#ifdef DYNAMIC_LOCK_MEMORY_TRACKING_NOOP
    (void) p;
    return;
#endif

#ifdef DEBUG_DYNAMIC_LOCK_MEMORY
    printf("pop  dynamic_lock_memory_base[%zu] = %p, expected %p\n", num_dynamic_lock_memory, dynamic_lock_memory_base[num_dynamic_lock_memory - 1], p);
#endif

    if (num_dynamic_lock_memory && dynamic_lock_memory_base[num_dynamic_lock_memory - 1] != p) {
        fprintf(stderr, "ERROR: dynamic_lock_memory_base stack did not match expected value\n");
        exit(EXIT_FAILURE);
    }

    dynamic_lock_memory_base = reallocarray(dynamic_lock_memory_base,
            num_dynamic_lock_memory - 1, sizeof(dynamic_lock_memory_base[0]));
    num_dynamic_lock_memory--;
}

void free_dynamic_lock_memory(void) {
#ifdef DYNAMIC_LOCK_MEMORY_TRACKING_NOOP
    return;
#endif

    for (size_t i = 0; i < num_dynamic_lock_memory; i++) {
#ifdef DEBUG_DYNAMIC_LOCK_MEMORY
        printf("free dynamic_lock_memory_base[%zu] = %p\n", i, dynamic_lock_memory_base[i]);
#endif
        free(dynamic_lock_memory_base[i]);
    }
}

// allocate memory, optionally using hugepages
void * do_alloc(size_t length, int use_hugepages, size_t nonhuge_alignment, size_t hugepage_req_physaddr, int verbose) {

    // XXX: length is erg_bytes * num_lock_memory_tries if not using hugepages
    // if using hugepages, the implicit length will be 1 hugepage

    if (use_hugepages != HUGEPAGES_NONE) {
        return do_hugepage_alloc(use_hugepages, hugepage_req_physaddr, verbose);
    }

    // non-hugepage allocation

    void * p;
    int ret = posix_memalign((void **) &p, nonhuge_alignment, length);

    if (ret) {
        printf("posix_memalign returned %d, exiting\n", ret);
        exit(-1);
    }

    // prefault
    memset(p, 1, length);

    push_dynamic_lock_memory(p);

    return p;
}

static const struct {
    const char * size_string;
    const int enum_param_value;
} hugepage_mapping[] = {
    { "none", HUGEPAGES_NONE },
    { "0", HUGEPAGES_NONE },
    { "default", HUGEPAGES_DEFAULT },
    { "1", HUGEPAGES_DEFAULT },
    { "64K", HUGEPAGES_64K },
    { "64KB", HUGEPAGES_64K },
    { "2M", HUGEPAGES_2M },
    { "2MB", HUGEPAGES_2M },
    { "32M", HUGEPAGES_32M },
    { "32MB", HUGEPAGES_32M },
    { "512M", HUGEPAGES_512M },
    { "512MB", HUGEPAGES_512M },
    { "1G", HUGEPAGES_1G },
    { "1GB", HUGEPAGES_1G },
    { "16G", HUGEPAGES_16G },
    { "16GB", HUGEPAGES_16G },
};

static const size_t num_hugepage_mappings = sizeof(hugepage_mapping) / sizeof(hugepage_mapping[0]);

const char * hugepage_map (int enum_param_value) {
    for (size_t i = 0; i < num_hugepage_mappings; i++) {
        if (hugepage_mapping[i].enum_param_value == enum_param_value) {
            return hugepage_mapping[i].size_string;
        }
    }
    return NULL;
}

int parse_hugepage_parameter(const char * optarg) {
    int use_hugepages = HUGEPAGES_NONE;
    size_t i;

    if (0 == strcasecmp(optarg, "help")) {
        goto PRINT_HUGEPAGES;
    }

    // search for one of the strings in hugepage_mapping[]
    for (i = 0; i < num_hugepage_mappings; i++) {
        if (0 == strcasecmp(optarg, hugepage_mapping[i].size_string)) {
            use_hugepages = hugepage_mapping[i].enum_param_value;
            break;
        }
    }

    // a string size_string was not found, parse for the enum value
    if (i == num_hugepage_mappings) {
        use_hugepages = strtoul(optarg, NULL, 0);
    }

    if (use_hugepages >= HUGEPAGES_MAX_ENUM || use_hugepages < HUGEPAGES_NONE) {
        printf("Error: unknown hugepage parameter %s\n", optarg);
PRINT_HUGEPAGES:
        printf("param  hugepage size (may not be supported by the system!)\n");
        for (i = 0; i < num_hugepage_mappings; i++) {
            printf("%-5d  %s\n", hugepage_mapping[i].enum_param_value, hugepage_mapping[i].size_string);
        }
        exit(-1);
    }

    return use_hugepages;
}


uintptr_t get_phys_addr(uintptr_t vaddr) {

    uintptr_t paddr = 0;

    static unsigned long PAGESIZE = 1;
    static unsigned long PAGE_MASK = 1;

    static unsigned long last_vpage = 1;    // ensure mismatch
    static unsigned long last_ppage = 1;

    if (PAGESIZE == 1) {
         PAGESIZE = sysconf(_SC_PAGESIZE);
         PAGE_MASK = ~(PAGESIZE - 1);
    }

#if 0
    pid_t pid = getpid();
    printf("pid = %d, vaddr = %lx\n", pid, vaddr);
    printf("vaddr & PAGE_MASK = %lx\n", vaddr & PAGE_MASK);
    printf("last_vpage        = %lx\n", last_vpage);
    printf("last_ppage        = %lx\n", last_ppage);
#endif

    if ((vaddr & PAGE_MASK) == last_vpage) {
        return last_ppage | (vaddr & ~PAGE_MASK);
    }

    if (geteuid() == 0) {
        pid_t pid = getpid();   // this process ID
        if (lkmc_pagemap_virt_to_phys_user(&paddr, pid, vaddr)) {
            fprintf(stderr, "error: virt_to_phys_user\n");
            return EXIT_FAILURE;
        }

        last_vpage = vaddr & PAGE_MASK;
        last_ppage = paddr & PAGE_MASK;

        return paddr;
    }

    fprintf(stderr, "didn't expect to get here in get_phys_addr, not running as root?\n");
    exit(-1);

    return -1;
}

/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
