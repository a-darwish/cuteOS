/*
 * Standard C string methods
 *
 * Copyright (C) 2009-2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * Optimized string methods are tested using LD_PRELOAD with heavy
 * programs like Firefox, Google Chrome and OpenOffice.
 */

#include <stdint.h>
#include <kernel.h>
#include <string.h>

/*
 * GCC "does not parse the assembler instruction template and does not
 * know what it means or even whether it is valid assembler input." Thus,
 * it needs to be told the list of registers __implicitly__ clobbered by
 * such inlined assembly snippets.
 *
 * A good way to stress-test GCC extended assembler constraints is to
 * always inline the assembler snippets (C99 'static inline'), compile
 * the kernel at -O3 or -Os, and roll!
 *
 * Output registers are (explicitly) clobbered by definition. 'CCR' is
 * the x86's condition code register %rflags.
 */

/*
 * Copy @len bytes from @src to @dst. Return a pointer
 * to @dst
 *
 * The AMD64 ABI guarantees a DF=0 upon function entry.
 */
void *memcpy(void *dst, const void *src, int len)
{
	int __uninitialized(tmp);
	uintptr_t d0;

	asm volatile (
	    "movl %[len], %[tmp];"
	    "andl $7, %[len];"			/* CCR */
	    "rep  movsb;"
	    "movl %[tmp], %[len];"
	    "shrl $3, %[len];"			/* CCR */
	    "rep  movsq;"			/* rdi, rsi, rcx */
	    : [dst] "=&D"(d0), "+&S"(src), [len] "+&c"(len),
	      [tmp] "+&r"(tmp)
	    : "[dst]"(dst)
	    : "cc", "memory");

	return dst;
}

/*
 * Fill memory with the constant byte @ch. Returns pointer
 * to memory area @dst; no return value reserved for error
 *
 * "Note that a REP STOS instruction is the fastest way to
 * initialize a large block of memory." --Intel, vol. 2B
 *
 * To copy the @ch byte repetitively over an 8-byte block,
 * we multiply its value with (0xffffffffffffffff / 0xff).
 */
void *memset(void *dst, uint8_t ch, uint32_t len)
{
	uint64_t __uninitialized(tmp);
	uint64_t uch = ch;
	uint64_t ulen = len;
	uintptr_t d0;

	uch &= 0xff;
	asm volatile (
	    "mov  %[len], %[tmp];"
	    "and  $7, %[len];"			/* CCR */
	    "rep  stosb;"			/* rdi, rcx */
	    "mov  %[tmp], %[len];"
	    "shr  $3, %[len];"			/* CCR */
	    "mov  %[ch], %[tmp];"
	    "mov  $0x0101010101010101, %[ch];"
	    "mul  %[tmp];"			/* rdx! CCR */
	    "rep  stosq;"			/* rdi, rcx */
	    : [tmp] "+&r"(tmp), [dst] "=&D"(d0), [ch] "+&a"(uch),
	      [len] "+&c"(ulen)
	    : "[dst]"(dst)
	    : "rdx", "cc", "memory");

	return dst;
}

/*
 * Fill memory with given 4-byte value @val. For easy
 * implementation, @len is vetoed to be a multiple of 8
 */
void *memset32(void *dst, uint32_t val, uint64_t len)
{
	uint64_t uval;
	uintptr_t d0;

	assert((len % 8) == 0);
	len = len / 8;
	uval = ((uint64_t)val << 32) + val;

	asm volatile (
	    "rep stosq"				/* rdi, rcx */
	    : [dst] "=&D"(d0), "+&c"(len)
	    : "[dst]"(dst), "a"(uval)
	    : "memory");

	return dst;
}

/*
 * Fill memory with given 8-byte value @val. For easy
 * implementation, @len is vetoed to be a multiple of 8
 */
void *memset64(void *dst, uint64_t val, uint64_t len)
{
	uintptr_t d0;

	assert((len % 8) == 0);
	len = len / 8;

	asm volatile (
	    "rep stosq"				/* rdi, rcx */
	    : [dst] "=&D"(d0), "+&c"(len)
	    : "[dst]"(dst), "a"(val)
	    : "memory");

	return dst;
}

/*
 * Yet-to-be-optimized string ops
 */

int strlen(const char *str)
{
	const char *tmp;

	for (tmp = str; *tmp; tmp++)
		;
	return tmp - str;
}

char *strncpy(char *dst, const char *src, int n)
{
	char *tmp = dst;

	while (n) {
		*tmp = *src;
		if (*tmp)
			src++;
		tmp++;
		n--;
	}

	return dst;
}

int strncmp(const char *c1, const char *c2, int n)
{
	uint8_t s1, s2, res;

	res = 0;
	while (n) {
		s1 = *c1++;
		s2 = *c2++;

		res = s1 - s2;
		if (res != 0 || s1 == 0)
			break;
		n--;
	}

	return res;
}

int memcmp(const void *s1, const void *s2, uint32_t n)
{
	const uint8_t *v1, *v2;
	uint8_t res;

	v1 = s1;
	v2 = s2;

	res = 0;
	while (n--) {
		res = *v1++ - *v2++;
		if (res != 0)
			break;
	}

	return res;
}
