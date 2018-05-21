/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Based on Linux 4.13 */

#include "atomics.h"

unsigned long __attribute__((noinline)) lock_acquire (uint64_t *lock, unsigned long threadnum) {
	unsigned long depth = 0;
#if defined(__x86_64__)
asm volatile (
"	movw	$0,%[depth]\n"
"	nop\n"
"	nop\n"
"	nop\n"
"	mov    $0x20000,%%eax\n"
"	lock xadd %%eax,%[lock]\n"
"	mov    %%eax,%%edx\n"
"	mov    %%eax,%[depth]\n"
"	shr    $0x10,%%edx\n"
"	cmp    %%ax,%%dx\n"
"	jne    2f\n"
"1:	nop\n"
"	jmp 4f\n"
"2:	movzwl %[lock],%%eax\n"
"	mov    %%edx,%%ecx\n"
"	cmp    %%dx,%%ax\n"
"	je     1b\n"
"3:	pause\n"
"	movzwl %[lock],%%eax\n"
"	cmp    %%cx,%%ax\n"
"	jne    3b\n"
"4:\n"
: [lock] "+m" (*lock), [depth] "=m" (depth)
:
: "cc" );
	depth = (((depth >> 16) - (depth & 0xFFFF)) & 0xFFFF) >> 2;
#elif defined(__aarch64__)
	unsigned tmp, tmp2, tmp3;
asm volatile (
"	mov	%w[depth], #0\n"
#if defined(USE_LSE)
"	mov	%w[tmp3], #0x10000\n"
"	ldadda	%w[tmp3], %w[tmp], %[lock]\n"
"	nop\n"
"	nop\n"
#else
"1:	ldaxr	%w[tmp], %[lock]\n"
"	add	%w[tmp2], %w[tmp], #0x10, lsl #12\n"
"	stxr	%w[tmp3], %w[tmp2], %[lock]\n"
"	cbnz	%w[tmp3], 1b\n"
#endif
"	eor	%w[tmp2], %w[tmp], %w[tmp], ror #16\n"
"	cbz	%w[tmp2], 3f\n"
"	and	%w[tmp3], %w[tmp], #0xFFFF\n"
"	lsr	%w[depth], %w[tmp], #16\n"
"	sub	%w[depth], %w[depth], %w[tmp3]\n"
"	and	%w[depth], %w[depth], #0xFFFF\n"
"	sevl\n"
"2:	wfe\n"
"	ldaxrh	%w[tmp3], %[lock]\n"
"	eor	%w[tmp2], %w[tmp3], %w[tmp], lsr #16\n"
"	cbnz	%w[tmp2], 2b\n"
"3:\n"
: [tmp] "=&r" (tmp), [tmp2] "=&r" (tmp2),
  [tmp3] "=&r" (tmp3), [lock] "+Q" (*lock),
  [depth] "=&r" (depth)
: 
: );
#endif

	return depth;
}

static inline void lock_release (uint64_t *lock, unsigned long threadnum) {
#if defined(__x86_64__)
asm volatile (
"	addw	$0x2,%[lock]\n"
: [lock] "+m" (*lock)
:
: "cc" );
#elif defined(__aarch64__)
	unsigned long tmp;
asm volatile (
#if defined(USE_LSE)
"	mov	%w[tmp], #1\n"
"	staddlh	%w[tmp], %[lock]\n"
"	nop\n"
#else
"	ldrh	%w[tmp], %[lock]\n"
"	add	%w[tmp], %w[tmp], #0x1\n"
"	stlrh	%w[tmp], %[lock]\n"
#endif
: [tmp] "=&r" (tmp), [lock] "+Q" (*lock)
:
: );
#endif
}
