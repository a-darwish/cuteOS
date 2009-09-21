#ifndef STDINT_H
#define STDINT_Y

/*
 * Standard C99 integer types
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * We only target x86-64 and use the LP64 model. Check the SUS paper
 * '64-bit and Data Size Neutrality' for more details on why did the
 * UNIX world standardize on LP64, and for below definitions rational.
 */

/*
 * Exact width integer types
 */
typedef signed char int8_t;
typedef signed short int int16_t;
typedef signed int  int32_t;
typedef signed long int64_t;
typedef unsigned char uint8_t;
typedef unsigned short int uint16_t;
typedef unsigned int  uint32_t;
typedef unsigned long uint64_t;

/*
 * Integral types large enough to hold any pointer
 */
typedef signed long int	intptr_t;
typedef unsigned long int uintptr_t;

#endif
