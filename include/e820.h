#ifndef _E820
#define _E820

/*
 * BIOS 0xE820 - Query System Address Map service.
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * In real-mode (e820.S), we query the bios for system memory map and
 * store the result in below form, to be read by higher level kernel code:
 *
 *             Success                       Failure
 *
 *     -----------------------       -----------------------
 *    |       Checksum        |     |       Checksum        |
 *     -----------------------       -----------------------
 *    |     Err Code (0)      |     |    Err Code ( > 0)    |	Error code
 *     -----------------------       -----------------------
 *    |       E820_END        |     |       E820_END        |   Entries-end flag
 *     -----------------------       -----------------------
 *    |        ......         |     |        ......         |
 *    |      E820 Entry       |     |      E820 Entry       |	Entry N
 *     -----------------------       -----------------------
 *    |    E820 Entry Size    |     |    E820 Entry Size    |	Entry N size
 *     -----------------------       -----------------------
 *              ....                          ....		Var # of entries
 *     -----------------------       -----------------------
 *    |        ......         |     |        ......         |
 *    |      E820 Entry       |     |      E820 Entry       |   Entry 1
 *     -----------------------       -----------------------
 *    |    E820 Entry Size    |     |    E820 Entry Size    |	Entry 1 size
 *     -----------------------       -----------------------
 *    | 'C' | 'U' | 'T' | 'E' |     | 'C' | 'U' | 'T' | 'E' |	Start signature
 *     -----------------------       -----------------------
 *                            ^                             ^
 *        E820_BASE ----------|         E820_BASE ----------|
 *
 * Each entry is returned by a single INT15 BIOS query, resulting in N+1
 * queries, where the last query is marked by the BIOS returning %ebx=0,
 * and no e820 entry written; we directly put the end mark afterwards.
 *
 * If any of the N+1 queries resulted an error, we stop quering the BIOS
 * any further, directly put the end mark, and set the suitable err code.
 *
 * Check the ACPI spec v4.0, Chapter 14, "System Address Map Interfaces",
 * and below structure definitions for the e820h entries format.
 *
 * From now on, we'll refer to above composite struct as 'E820H struct'.
 */

/* E820H struct base address. Should in the first 64K
 * [0, (0xffff - 0x1000)] range to ease rmode access */
#define E820_BASE	0x1000

/* The struct shouldn't exceed a 4K page (arbitary) */
#define E820_MAX	(E820_BASE + 0x1000)

/* Struct start signature */
#define E820_INIT_SIG	('C'<<(3*8) | 'U'<<(2*8) | 'T'<<(1*8) | 'E')

/* ACPI-defined 'E820h supported' BIOS signature */
#define E820_BIOS_SIG	('S'<<(3*8) | 'M'<<(2*8) | 'A'<<(1*8) | 'P')

/* Error codes by e820.S */
#define E820_SUCCESS	0x0		/* No errors; e820 entries validated */
#define E820_NOT_SUPP	0x1		/* Bios doesn't support E820H srvice */
#define E820_BUF_FULL	0x2		/* Returned data passed size limit */
#define E820_ERROR	0x3		/* General e820 error (carry set) */
#define E820_BIOS_BUG	0x4		/* Buggy bios - violating ACPI */
#define E820_HUGE_ENTRY 0x5		/* Entry size > limit */

#ifndef __ASSEMBLY__

#include <stdint.h>
#include <kernel.h>

/*
 * E820h struct error to string map
 */
static char *e820_errors[] = {
	[E820_SUCCESS]    = "success",
	[E820_NOT_SUPP]   = "no BIOS support",
	[E820_BUF_FULL]   = "custom buffer full",
	[E820_ERROR]      = "general error (carry set)",
	[E820_BIOS_BUG]   = "BIOS bug, violating ACPI",
	[E820_HUGE_ENTRY] = "huge returned e820 entry",
};

static inline char *e820_errstr(uint32_t error)
{
	if (error > E820_HUGE_ENTRY)
		return "unknown e820.S-reported error";

	assert(error < ARRAY_SIZE(e820_errors));

	return e820_errors[error];
}

/*
 * ACPI Address Range Descriptor
 */
struct e820_range {
	uint64_t base;			/* Range base address */
	uint64_t len;			/* Range length */
	uint32_t type;			/* ACPI-defined range type */
} __packed;

/*
 * ACPI memory range types
 */
enum {
	E820_AVAIL	= 0x1,		/* Available for use by the OS */
	E820_RESERVED	= 0x2,		/* Do not use */
	E820_ACPI_TBL	= 0x3,		/* ACPI Reclaim Memory */
	E820_ACPI_NVS	= 0x4,		/* ACPI NVS Memory */
	E820_ERRORMEM	= 0x5,		/* BIOS detected errors at this range */
	E820_DISABLED	= 0x6,		/* Not BIOS chipset-enabled memory */
};

/*
 * Range types are sequential; use as index.
 */
static char *e820_types[] = {
	[E820_AVAIL]    = "available",
	[E820_RESERVED] = "reserved",
	[E820_ACPI_TBL] = "acpi tables",
	[E820_ACPI_NVS] = "acpi nvs",
	[E820_ERRORMEM] = "erroneous",
	[E820_DISABLED] = "disabled",
};

/*
 * Transform given ACPI type value to string.
 */
static inline char *e820_typestr(uint32_t type)
{
	if (type < E820_AVAIL || type > E820_DISABLED)
		return "unknown type - reserved";

	/* Don't put this on top of above return; we
	 * can have unknown values from future BIOSes */
	assert(type < ARRAY_SIZE(e820_types));

	return e820_types[type];
}

void mmap_init(void);

#define E820_END	UINT32_MAX	/* E820 list end mark */

#else /* __ASSEMBLY__ */

#define E820_END	0xffffffff

#endif /* !__ASSEMBLY__ */

#endif /* _E820 */
