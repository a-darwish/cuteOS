#ifndef SEGMENT_H
#define SEGMENT_H

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

#define VIRTUAL_START     (0xffffffff80000000)

#ifndef __ASSEMBLY__

#include <kernel.h>
#include <stdint.h>

/* We need a char* cast in case someone gave us an int or long
 * pointer that can mess up the whole summation/transformation */
#define VIRTUAL(address)  ((void *)((char *)(address) + VIRTUAL_START))
#define PHYS(address)     ((void *)((char *)(address) - VIRTUAL_START))

/* Maximum mapped physical address. We should get rid of our
 * ad-hoc mappings soon */
#define PHYS_MAX	0x30000000

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

static inline void load_cr3(uint64_t cr3)
{
	asm volatile("mov %0, %%cr3"
		     :
		     :"a"(cr3));
}

static inline uint64_t get_cr3(void)
{
	uint64_t cr3;

	asm volatile("mov %%cr3, %0"
		     :"=a"(cr3)
		     :);

	return cr3;
}

#else

#define VIRTUAL(address)  ((address) + VIRTUAL_START)

#endif /* !__ASSEMBLY__ */

#endif
