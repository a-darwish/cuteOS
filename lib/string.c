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
 * The AMD64 ABI guarantees a DF=0 upon function entry.
 */
static void *__memcpy_forward(void *dst, const void *src, size_t len)
{
	size_t __uninitialized(tmp);
	uintptr_t d0;

	asm volatile (
	    "mov  %[len], %[tmp];"
	    "and  $7, %[len];"			/* CCR */
	    "rep  movsb;"
	    "mov  %[tmp], %[len];"
	    "shr  $3, %[len];"			/* CCR */
	    "rep  movsq;"			/* rdi, rsi, rcx */
	    : [dst] "=&D"(d0), "+&S"(src), [len] "+&c"(len),
	      [tmp] "+&r"(tmp)
	    : "[dst]"(dst)
	    : "cc", "memory");

	return dst;
}

/*
 * We do tolerate overlapping regions here if src > dst. In
 * such case, (src - dst) must be bigger than movsq's block
 * copy factor: 8 bytes.
 */
void *memcpy_forward(void *dst, const void *src, size_t len)
{
	uintptr_t udst, usrc;
	bool bad_overlap;

	udst = (uintptr_t)dst;
	usrc = (uintptr_t)src;

	bad_overlap = (udst + 8 > usrc) && (usrc + len > udst);
	if (__unlikely(bad_overlap))
		panic("%s: badly-overlapped regions, src=0x%lx, dst"
		      "=0x%lx, len=0x%lx", __func__, src, dst, len);

	return __memcpy_forward(dst, src, len);
}

/*
 * C99-compliant, with extra sanity checks.
 */
void *memcpy(void * restrict dst, const void * restrict src, size_t len)
{
	uintptr_t udst, usrc;
	bool overlap;

	udst = (uintptr_t)dst;
	usrc = (uintptr_t)src;

	overlap = (udst + len > usrc) && (usrc + len > udst);
	if (__unlikely(overlap))
		panic("%s: overlapped regions, src=0x%lx, dst=0x"
		      "%lx, len=0x%lx", __func__, src, dst, len);

	return __memcpy_forward(dst, src, len);
}

/*
 * memcpy(), minus the checks
 *
 * Sanity checks overhead cannot be tolerated for HOT copying
 * paths like screen scrolling.
 *
 * This is also useful for code implicitly called by panic():
 * a sanity check failure there will lead to a stack overflow.
 */

void *memcpy_forward_nocheck(void *dst, const void *src, size_t len)
{
	return __memcpy_forward(dst, src, len);
}

void *memcpy_nocheck(void * restrict dst, const void * restrict src, size_t len)
{
	return __memcpy_forward(dst, src, len);
}

/*
 * "Note that a REP STOS instruction is the fastest way to
 * initialize a large block of memory." --Intel, vol. 2B
 *
 * To copy the @ch byte repetitively over an 8-byte block,
 * we multiply its value with (0xffffffffffffffff / 0xff).
 */
void *memset(void *dst, uint8_t ch, size_t len)
{
	size_t __uninitialized(tmp);
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

#if STRING_TESTS

#define _ARRAY_LEN	100
static uint8_t _arr[_ARRAY_LEN];

static void memcpy_test_overlaps(void)
{
	memset(_arr, 0x55, _ARRAY_LEN);

	/* Should succeed */

	memcpy(_arr, _arr + 20, 10);		/* Regular */
	memcpy(_arr + 20, _arr, 10);		/* Regular */
	memcpy(_arr, _arr + 20, 20);		/* Regular, bounds */
	memcpy(_arr + 20, _arr, 20);		/* Regular, bounds */

	memcpy_forward(_arr, _arr + 20, 10);	/* Regular */
	memcpy_forward(_arr + 20, _arr, 10);	/* Regular */
	memcpy_forward(_arr, _arr + 20, 20);	/* Regular, bounds */
	memcpy_forward(_arr + 20, _arr, 20);	/* Regular, bounds */
	memcpy_forward(_arr, _arr + 10, 20);	/* Good overlap */
	memcpy_forward(_arr, _arr + 10, 11);	/* Good overlap, bounds */

	/* Should fail */

	/* memcpy(_arr, _arr + 20, 40);		/\* Overlap *\/ */
	/* memcpy(_arr + 20, _arr, 40);		/\* Overlap *\/ */
	/* memcpy(_arr, _arr + 20, 21);		/\* Overlap, bounds *\/ */
	/* memcpy(_arr + 20, _arr, 21);		/\* Overlap, bounds *\/ */

	/* memcpy_forward(_arr, _arr + 7, 10);	/\* Good overlap, but < 8-byte *\/ */
	/* memcpy_forward(_arr + 20, _arr, 40);	/\* Bad overlap *\/ */
	/* memcpy_forward(_arr + 20, _arr, 21);	/\* Bad overlap, bounds *\/ */
}

void string_run_tests(void)
{
	memcpy_test_overlaps();
}

#endif /* STRING_TESTS */


