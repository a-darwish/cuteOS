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

/*
 * This should be written as inline x86-64.
 */

int strlen(const char *str)
{
	const char *tmp;

	for (tmp = str; *tmp; tmp++)
		;
	return tmp - str;
}
void strncpy(char *dst, const char *src, int n)
{
	char *tmp = dst;

	while (n) {
		*tmp = *src;
		if (*tmp)
			src++;
		tmp++;
		n--;
	}
}
void memcpy(void *dst, void *src, int len)
{
	char *csrc = (char *)src;
	char *cdst = (char *)dst;

	while (len--)
		*cdst++ = *csrc++;
}
