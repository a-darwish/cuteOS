/*
 * Standard C string methods
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * This should be transformed to inline x86 assembly as appropriate,
 * let's just have them for now.
 */

#include <stdint.h>
#include <kernel.h>

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

int strncmp(char *s1, char *s2, int n)
{
	uint8_t res = 0;

	while (n) {
		res = *s1 - *s2;
		if (res != 0 || *s1 == 0)
			break;
		n--;
	}

	return res;
}

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
	int tmp;
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
void *memset(void *dst, int ch, int len)
{
	uint64_t tmp;
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
