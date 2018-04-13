/*****************************************************************************

Copyright (c) 1994, 2016, Oracle and/or its affiliates. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA

*****************************************************************************/

#define os_rmb __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define os_wmb __atomic_thread_fence(__ATOMIC_RELEASE)

#if defined(__x86_64__)
#define UT_RELAX_CPU() asm volatile ("rep; nop")
#elif defined(__AARCH64__)
// Theoretically we could emit a yield here but MySQL doesn't do it
// and most ARM cores are likely to NOP it anyway
#define UT_RELAX_CPU() asm volatile ("":::"memory")
#else
#define UT_RELAX_CPU() asm volatile ("":::"memory")
#endif
