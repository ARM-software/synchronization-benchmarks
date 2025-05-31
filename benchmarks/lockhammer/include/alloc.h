
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


#ifndef ALLOC_H
#define ALLOC_H

enum {
    HUGEPAGES_NONE,
    HUGEPAGES_DEFAULT,
    HUGEPAGES_64K,
    HUGEPAGES_2M,
    HUGEPAGES_32M,
    HUGEPAGES_512M,
    HUGEPAGES_1G,
    HUGEPAGES_16G,
    HUGEPAGES_MAX_ENUM
};


void * do_hugepage_alloc(int use_hugepages, size_t hugepage_req_physaddr, int verbose);
void * do_alloc(size_t length, int use_hugepages, size_t nonhuge_alignment, size_t hugepage_req_physaddr, int verbose);
void print_hugepage_physaddr_and_exit(void * mmap_ret);

// hugepage flag parameter parsing
int parse_hugepage_parameter(const char * optarg);
const char * hugepage_map (int enum_param_value);

// function prototypes used by osq_lock
uintptr_t get_phys_addr(uintptr_t vaddr);

#endif

/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
