/*
 * Standard C string methods
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <stdint.h>
#include <kernel.h>

/*
 * For more speed, we should let movsq and stosq read
 * arguments (source) be 8 byte aligned.
 */

/*
 * Copy @len bytes from @src to @dst. Return a pointer
 * to @dst
 */
void *memcpy(void *dst, void *src, int len)
{
	int __uninitialized(tmp);
	uintptr_t d0;

	asm volatile (
	    "movl %[len], %[tmp];"
	    "andl $7, %[len];"
	    "rep  movsb;"
	    "movl %[tmp], %[len];"
	    "shrl $3, %[len];"
	    "rep  movsq;"
	    : [dst] "=&D"(d0), [len] "=&c"(len), [tmp] "=&r"(tmp)
	    : "S"(src), "[dst]"(dst), "[len]"(len), "[tmp]"(tmp)
	    : "memory");

	return dst;
}

/*
 * Fill memory with the constant byte @ch. Returns pointer
 * to memory area @dst; no return value reserved for error
 */
void *memset(void *dst, int ch, uint32_t len)
{
	uint64_t __uninitialized(tmp);
	uint64_t uch = ch;
	uint64_t ulen = len;
	uintptr_t d0;

	uch &= 0xff;
	asm volatile (
	    "mov  %[len], %[tmp];"
	    "and  $7, %[len];"
	    "rep  stosb;"
	    "mov  %[tmp], %[len];"
	    "shr  $3, %[len];"
	    /* Copy the right-most byte to the rest 7 bytes.
	     * multiplier extracted by 0xffffffffffffffff/0xff */
	    "mov  %[ch], %[tmp];"
	    "mov  $0x0101010101010101, %[ch];"
	    "mul  %[tmp];"
	    "rep  stosq;"
	    : [tmp] "=&r"(tmp), [dst] "=&D"(d0), [ch] "=&a"(uch),
	      [len] "=&c"(ulen)
	    : "[dst]"(dst), "[ch]"(uch), "[len]"(ulen), "[tmp]"(tmp)
	    : "memory");

	return dst;
}

/*
 * Fill memory with given 4-byte value @val. For easy
 * implementation, @len is vetoed to be a multiple of 8
 */
void *memset32(void *dst, uint32_t val, uint32_t len)
{
	uint64_t uval, ulen;
	uintptr_t d0;

	assert((len % 8) == 0);
	ulen = len / 8;
	uval = ((uint64_t)val << 32) + val;

	asm volatile (
	    "rep stosq"
	    : [dst] "=&D"(d0)
	    : "[dst]"(dst), "a"(uval), "c"(ulen)
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

int memcmp(const void *s1, const void *s2, int n)
{
	const uint8_t *v1, *v2;
	uint8_t res;

	v1 = s1;
	v2 = s2;

	res = 0;
	while (n) {
		res = *v1++ - *v2++;
		if (res != 0)
			break;
		n--;
	}

	return res;
}
