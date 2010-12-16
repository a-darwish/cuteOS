#ifndef _STDINT_H
#define _STDINT_H

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
typedef uint32_t uint;

/*
 * Integral types large enough to hold any pointer
 */
typedef signed long int	intptr_t;
typedef unsigned long int uintptr_t;

typedef uint64_t size_t;
typedef uint64_t clock_t;

/*
 * MAXs and MINs
 */

#define INT8_MAX	(0x7f)
#define INT16_MAX	(0x7fff)
#define INT32_MAX	(0x7fffffff)
#define INT64_MAX	(0x7fffffffffffffffL)

#define UINT8_MAX	(0xff)
#define UINT16_MAX	(0xffff)
#define UINT32_MAX	(0xffffffff)
#define UINT64_MAX	(0xffffffffffffffffUL)

/* MAX value of given unsigned variable type */
#define UTYPE_MAXVAL(x)	((typeof(x))UINT64_MAX)

#endif /* _STDINT_H */
