#ifndef STRING_H
#define STRING_H

/*
 * Standard C string definitions
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <stdint.h>
#include <tests.h>

int strlen(const char *str);
char *strncpy(char *dst, const char *src, int n);
int strncmp(const char *s1, const char *s2, int n);

void *memcpy(void * restrict dst, const void * restrict src, size_t len);
void *memcpy_nocheck(void * restrict dst, const void * restrict src, size_t len);
void *memcpy_forward(void *dst, const void *src, size_t len);
void *memcpy_forward_nocheck(void *dst, const void *src, size_t len);

void *memset(void *dst, uint8_t ch, size_t len);
void *memset32(void *dst, uint32_t val, uint64_t len);
void *memset64(void *dst, uint64_t val, uint64_t len);

int memcmp(const void *s1, const void *s2, uint32_t len);

#if    STRING_TESTS
void string_run_tests(void);
#else
#include <kernel.h>
static void __unused string_run_tests(void) { }
#endif

#endif
