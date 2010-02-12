#ifndef _PAGING_H
#define _PAGING_H

/*
 * Paging defintions
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#define VIRTUAL_START     (0xffffffff80000000)
#define VIRTUAL(address)  ((address) + VIRTUAL_START)

#ifndef __ASSEMBLY__

#undef  VIRTUAL
#undef  VIRTUAL_START

/* We need a char* cast in case someone gave us an int or long
 * pointer that can mess up the whole summation/transformation */
#define VIRTUAL_START     (0xffffffff80000000ULL)
#define VIRTUAL(address)  ((void *)((char *)(address) + VIRTUAL_START))
#define PHYS(address)     ((void *)((char *)(address) - VIRTUAL_START))

/* Maximum mapped physical address. We should get rid of our
 * ad-hoc mappings soon */
#define PHYS_MAX	0x30000000

static inline void load_cr3(uint64_t cr3)
{
	asm volatile("mov %0, %%cr3"
		     :
		     :"a"(cr3)
		     :"memory");
}

static inline uint64_t get_cr3(void)
{
	uint64_t cr3;

	asm volatile("mov %%cr3, %0"
		     :"=a"(cr3)
		     :
		     :"memory");

	return cr3;
}

/*
 * Common macros for 4K pages
 */

/* Length of the page offset field in bits */
#define PAGE_SHIFT	12

#define PAGE_SIZE	(1 << PAGE_SHIFT)
#define PAGE_MASK	~(PAGE_SIZE - 1)

#define IS_PAGE_ALIGNED(x) ((x & ~PAGE_MASK) == 0)

#endif /* !__ASSEMBLY__ */

#endif /* _PAGING_H */
