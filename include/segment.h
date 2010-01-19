#ifndef _SEGMENT_H
#define _SEGMENT_H

/*
 * Segmentation definitions; minimal by the nature of x86-64
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#define KERNEL_CS 0x08
#define KERNEL_DS 0x10

#ifndef __ASSEMBLY__

#include <kernel.h>
#include <stdint.h>

struct gdt_descriptor {
	uint16_t limit;
	uint64_t base;
} __packed;

static inline void load_gdt(const struct gdt_descriptor *gdt_desc)
{
	asm volatile("lgdt %0"
		     :
		     :"m"(*gdt_desc));
}

static inline struct gdt_descriptor get_gdt(void)
{
	struct gdt_descriptor gdt_desc;

	asm volatile("sgdt %0"
		     :"=m"(gdt_desc)
		     :);

	return gdt_desc;
}

#endif /* !__ASSEMBLY__ */

#endif /* _SEGMENT_H */
