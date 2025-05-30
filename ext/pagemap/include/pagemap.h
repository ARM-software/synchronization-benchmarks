/*
 * SPDX-License-Identifier: BSD-3-Clause
 * SPDX-FileCopyrightText: Copyright 2020 Ciro Santilli
 *
 * pagemap.h is retrieved from
 * https://raw.githubusercontent.com/cirosantilli/linux-kernel-module-cheat/master/lkmc/pagemap.h
 */

/* https://cirosantilli.com/linux-kernel-module-cheat#userland-physical-address-experiments
 * https://cirosantilli.com/linux-kernel-module-cheat#pagemap-dump-out
 *
 * This file is dual licensed as both 3-Clause BSD and GPLv3.
 */

#ifndef LKMC_PAGEMAP_H
#define LKMC_PAGEMAP_H

#define _XOPEN_SOURCE 700
#include <fcntl.h> /* open */
#include <stdint.h> /* uint64_t  */
#include <stdio.h> /* snprintf */
#include <sys/types.h>
#include <unistd.h> /* pread, sysconf */

/* Format documented at:
 * https://github.com/torvalds/linux/blob/v4.9/Documentation/vm/pagemap.txt
 */
typedef struct {
    uint64_t pfn : 55;
    unsigned int soft_dirty : 1;
    unsigned int file_page : 1;
    unsigned int swapped : 1;
    unsigned int present : 1;
} LkmcPagemapEntry;

/* Parse the pagemap entry for the given virtual address.
 *
 * @param[out] entry      the parsed entry
 * @param[in]  pagemap_fd file descriptor to an open /proc/pid/pagemap file
 * @param[in]  vaddr      virtual address to get entry for
 * @return                0 for success, 1 for failure
 */
int lkmc_pagemap_get_entry(LkmcPagemapEntry *entry, int pagemap_fd, uintptr_t vaddr) {
    size_t nread;
    ssize_t ret;
    uint64_t data;
    uintptr_t vpn;

    vpn = vaddr / sysconf(_SC_PAGE_SIZE);
    nread = 0;
    while (nread < sizeof(data)) {
        ret = pread(
            pagemap_fd,
            ((uint8_t*)&data) + nread,
            sizeof(data) - nread,
            vpn * sizeof(data) + nread
        );
        nread += ret;
        if (ret <= 0) {
            return 1;
        }
    }
    entry->pfn = data & (((uint64_t)1 << 55) - 1);
    entry->soft_dirty = (data >> 55) & 1;
    entry->file_page = (data >> 61) & 1;
    entry->swapped = (data >> 62) & 1;
    entry->present = (data >> 63) & 1;
    return 0;
}

/* Convert the given virtual address to physical using /proc/PID/pagemap.
 *
 * @param[out] paddr physical address
 * @param[in]  pid   process to convert for
 * @param[in]  vaddr virtual address to get entry for
 * @return           0 for success, 1 for failure
 */
int lkmc_pagemap_virt_to_phys_user(uintptr_t *paddr, pid_t pid, uintptr_t vaddr) {
    char pagemap_file[BUFSIZ];
    int pagemap_fd;

    snprintf(pagemap_file, sizeof(pagemap_file), "/proc/%ju/pagemap", (uintmax_t)pid);
    pagemap_fd = open(pagemap_file, O_RDONLY);
    if (pagemap_fd < 0) {
        return 1;
    }
    LkmcPagemapEntry entry;
    if (lkmc_pagemap_get_entry(&entry, pagemap_fd, vaddr)) {
        return 1;
    }
    close(pagemap_fd);
    *paddr = (entry.pfn * sysconf(_SC_PAGE_SIZE)) + (vaddr % sysconf(_SC_PAGE_SIZE));
    return 0;
}

#endif
