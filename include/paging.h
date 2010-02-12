#ifndef _PAGING_H
#define _PAGING_H

/*
 * X86-64 paging and kernel address-space control
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#ifndef __ASSEMBLY__

#include <kernel.h>
#include <stdint.h>

/*
 * Page Map Level 4
 *
 * A PML4 Table can map the entire virtual 48-bit x86-64
 * address space. Each table entry maps a 512-GB region.
 */

#define PML4_ENTRY_SHIFT	(21 + 9 + 9)                       /* 39 */
#define PML4_ENTRY_MAPPING_SIZE	(0x1ULL << PML4_ENTRY_SHIFT)	   /* 512-GByte */
#define PML4_MAPPING_SIZE	(0x1ULL << (PML4_ENTRY_SHIFT + 9)) /* 256-TByte */
#define PML4_ENTRIES		512                                /* 4KB / 8 */
/*
 * Extract the 9-bit PML4 index/offset from given
 * virtual address
 */
#define pml4_index(virt_addr)	(((virt_addr) >> PML4_ENTRY_SHIFT) & 0x1ffULL)

/*
 * Page Map Level 3 - the Page Directory Pointer
 *
 * A PML3 Table can map a 512-GByte virtual space by
 * virtue of its entries, which can map 1-GByte each.
 */

#define PML3_ENTRY_SHIFT	(21 + 9)                           /* 30 */
#define PML3_ENTRY_MAPPING_SIZE (0x1ULL << PML3_ENTRY_SHIFT)       /* 1-GByte */
#define PML3_MAPPING_SIZE	(0x1ULL << (PML3_ENTRY_SHIFT + 9)) /* 512-GByte */
#define PML3_ENTRIES		512                                /* 4K / 8 */
/*
 * Extract the 9-bit PML3 index/offset from given
 * virtual address
 */
#define pml3_index(virt_addr)	(((virt_addr) >> PML3_ENTRY_SHIFT) & 0x1ffULL)

/*
 * Page Map Level 2 - the Page Directory
 *
 * A PML2 Table can map a 1-GByte virtual space by
 * virtue of its entries, which can map 2-MB each.
 */

#define PML2_ENTRY_SHIFT	(21)
#define PML2_ENTRY_MAPPING_SIZE	(0x1ULL << PML2_ENTRY_SHIFT)       /* 2-MByte */
#define PML2_MAPPING_SIZE	(0x1ULL << (PML2_ENTRY_SHIFT + 9)  /* 1-GByte */
#define PML2_ENTRIES		512                                /* 4K / 8 */
/*
 * Extract the 9-bit PML2 index/offset from given
 * virtual address
 */
#define pml2_index(virt_addr)	(((virt_addr) >> PML2_ENTRY_SHIFT) & 0x1ffULL)

/*
 * 4-KByte pages
 */

#define PAGE_SHIFT		12
#define PAGE_SIZE		(1 << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE - 1))

/*
 * 2-MByte pages
 */

#define PAGE_SHIFT_2MB		21
#define PAGE_SIZE_2MB		(1 << PAGE_SHIFT_2MB)

/*
 * x86-64 Page Table entries
 */

/*
 * Page-map level 4 entry
 * Format is common for 2-MB and 4-KB pages
 */
struct pml4e {
	uint64_t present:1,		/* Present PML4 entry */
		read_write:1,		/* 0: write-disable this 512-GB region */
		user_supervisor:1,	/* 0: no access for CPL=3 code */
		pwt:1,			/* Page-level write-through */
		pcd:1,			/* Page-level cache disable */

		/* Has this entry been used for virtual address translation?
		 * Whenever the cpu uses this paging-entry as a part of virtual
		 * address translation, it sets this flag. Since the cpu uses
		 * the TLB afterwards, this flag is set only the first time the
		 * table or physical page is read or written to. The bit is
		 * 'sticky': it never get cleared by the CPU afterwards */
		accessed:1,

		__ignored:1,		/* Play it safe and don't use this */

		/* AMD and Intel conflict: only one bit is reserved (must be
		 * zero) for Intel cpus; they're actually 2 bits for AMDs */
		__reserved0:2,

		avail0:3,		/* Use those as we wish */
		pml3_base:40,		/* Page Directory Pointer base >> 12 */
		avail1:11,		/* AMD was really generous .. */
		nx:1;			/* No-Execute for this 512-GB region */
} __packed;

/*
 * Page Directory Pointer entry
 * Format is common for 2-MB and 4-KB pages
 */
struct pml3e {
	uint64_t present:1,		/* Present PDP entry */
		read_write:1,		/* 0: write-disable this 1-GB region */
		user_supervisor:1,	/* 0: no access for CPL=3 code */
		pwt:1,			/* Page-level write-through */
		pcd:1,			/* Page-level cache disable */
		accessed:1,		/* Accessed bit (see pml4e comment) */
		__ignored:1,		/* Ignored; don't use */
		__reserved0:2,		/* Must-be-Zero (see pml4e comment) */
		avail0:3,		/* Available for use */
		pml2_base:40,		/* Page Directory base >> 12 */
		avail1:11,		/* Available area */
		nx:1;			/* No-Execute for this 1-GB region */
} __packed;

/*
 * Page Directory entry, 2-MB pages
 * NOTE!! set the page size bit to 1
 */
struct pml2e {
	unsigned present:1,		/* Present referenced 2-MB page */
		read_write:1,		/* 0: write-disable this 2-MB page */
		user_supervisor:1,	/* 0: no access for CPL=3 code */
		pwt:1,			/* Page-level write-through */
		pcd:1,			/* Page-level cache disable */
		accessed:1,		/* Accessed bit (see pml4e comment) */

		/* The dirty flag is only available in the lowest-level table.
		 * If set, it indicates the physical page this entry points
		 * has been written. This bit is never cleared by the CPU */
		dirty:1,

		/* Page Size bit; must be set to one. When this bit is set,
		 * in a third (1-GB page) or second level (2-MB page) page
		 * table, the entry is the lowest level of the page translation
		 * hierarchy */
		__reserved1:1,

		/* Global page bit; only available in the lowest-level page
		 * translation hieararchy. If set, referenced page TLB entry
		 * is not invalidated when CR3 is loaded */
		global:1,

		avail0:3,		/* Use those as we wish */
		pat:1,			/* Page-Attribute Table bit */
		__reserved0:8,		/* Must be zero */
		page_base:31,		/* Page base >> 21 */
		avail1:11,		/* Available for use */
		nx:1;			/* No-Execute for this 2-MB page */
} __packed;

/*
 * %CR3
 */

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
 * Kernel-space mappings
 */

#define VIRTUAL_START     (0xffffffff80000000ULL)

/* We need a char* cast in case someone gave us an int or long
 * pointer that can mess up the whole summation/transformation */
#define VIRTUAL(address)  ((void *)((char *)(address) + VIRTUAL_START))
#define PHYS(address)     ((void *)((char *)(address) - VIRTUAL_START))

/* Maximum mapped physical address. We should get rid of our
 * ad-hoc mappings very soon */
#define PHYS_MAX	0x30000000

#else /* __ASSEMBLY__ */

#undef  VIRTUAL
#undef  VIRTUAL_START

#define VIRTUAL_START     (0xffffffff80000000)

#define VIRTUAL(address)  ((address) + VIRTUAL_START)

#endif /* !__ASSEMBLY__ */

#endif /* _PAGING_H */
