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
 * Kernel-space mappings
 *
 * Macros returning physical addresses intentionally return
 * an unsigned integer instead of a pointer; we do not want
 * to have invalid pointers dangling around.
 */

/*
 * Mappings for kernel text, data, and bss (-2GB)
 *
 * This is the virtual base for %rip, %rsp, and kernel global
 * symbols. Check the logic behind this at head.S comments.
 *
 * @KTEXT_OFFSET is equivalent to Linux's __START_KERNEL_MAP
 */
#define KTEXT_PAGE_OFFSET	0xffffffff80000000ULL
#define KTEXT_PHYS_OFFSET	0x0
#define KTEXT_PAGE_END		0xffffffffa0000000ULL
#define KTEXT_AREA_SIZE		(KTEXT_PAGE_END - KTEXT_PAGE_OFFSET)
#define KTEXT_PHYS_END		(KTEXT_PHYS_OFFSET + KTEXT_AREA_SIZE)
#define KTEXT_VIRTUAL(phys_address)				\
({								\
	assert((uintptr_t)(phys_address) >= KTEXT_PHYS_OFFSET);	\
	assert((uintptr_t)(phys_address) < KTEXT_PHYS_END);	\
								\
	(void *)((char *)(phys_address) + KTEXT_PAGE_OFFSET);	\
})
#define KTEXT_PHYS(virt_address)				\
({								\
	assert((uintptr_t)(virt_address) >= KTEXT_PAGE_OFFSET);	\
	assert((uintptr_t)(virt_address) < KTEXT_PAGE_END);	\
								\
	(uintptr_t)((char *)(virt_address) - KTEXT_PAGE_OFFSET);\
})

/*
 * Kernel-space mappings for all system physical memory
 *
 * This is the standard kernel virtual base for everything
 * except kernel text and data areas (%rip, %rsp, symbols).
 * Check the logic behind this at the head.S comments.
 *
 * @KERN_PAGE_END_MAX, @KERN_PHYS_END_MAX: those are only
 * rached if the system has max supported phys memory, 64TB!
 */
#define KERN_PAGE_OFFSET	0xffff800000000000ULL
#define KERN_PHYS_OFFSET	0x0ULL
#define KERN_PAGE_END_MAX	0xffffc00000000000ULL
#define KERN_AREA_MAX_SIZE	(KERN_PAGE_END_MAX - KERN_PAGE_OFFSET)
#define KERN_PHYS_END_MAX	(KERN_PHYS_OFFSET + KERN_AREA_MAX_SIZE)
#define VIRTUAL(phys_address)					\
({								\
	assert((uintptr_t)(phys_address) >= KERN_PHYS_OFFSET);	\
	assert((uintptr_t)(phys_address) < KERN_PHYS_END_MAX);	\
								\
	(void *)((char *)(phys_address) + KERN_PAGE_OFFSET);	\
})
#define PHYS(virt_address)					\
({								\
	assert((uintptr_t)(virt_address) >= KERN_PAGE_OFFSET);	\
	assert((uintptr_t)(virt_address) < KERN_PAGE_END_MAX);	\
								\
	(uintptr_t)((char *)(virt_address) - KERN_PAGE_OFFSET);	\
})

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
#define pml4_index(virt_addr)					\
	(((uintptr_t)(virt_addr) >> PML4_ENTRY_SHIFT) & 0x1ffULL)

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
#define pml3_index(virt_addr)					\
	(((uintptr_t)(virt_addr) >> PML3_ENTRY_SHIFT) & 0x1ffULL)

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
#define pml2_index(virt_addr)					\
	(((uintptr_t)(virt_addr) >> PML2_ENTRY_SHIFT) & 0x1ffULL)

/*
 * 4-KByte pages
 */

#define PAGE_SHIFT		12
#define PAGE_SIZE		(1 << PAGE_SHIFT)

#define page_aligned(addr)	(is_aligned((uintptr_t)(addr), PAGE_SIZE))

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

static inline void *pml3_base(struct pml4e *pml4e)
{
	return VIRTUAL((uintptr_t)pml4e->pml3_base << PAGE_SHIFT);
}

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

static inline void *pml2_base(struct pml3e *pml3e)
{
	return VIRTUAL((uintptr_t)pml3e->pml2_base << PAGE_SHIFT);
}

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

static inline void *page_base(struct pml2e *pml2e)
{
	return VIRTUAL((uintptr_t)pml2e->page_base << PAGE_SHIFT_2MB);
}

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

#else /* __ASSEMBLY__ */

#define KTEXT_PAGE_OFFSET	0xffffffff80000000
#define KTEXT_VIRTUAL(address)	((address) + KTEXT_PAGE_OFFSET)
#define KTEXT_PHYS(address)	((address) - KTEXT_PAGE_OFFSET)

#define KERN_PAGE_OFFSET	0xffff800000000000
#define VIRTUAL(address)	((address) + KERN_PAGE_OFFSET)

#endif /* !__ASSEMBLY__ */

#endif /* _PAGING_H */
