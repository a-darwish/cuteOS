/*
 * Kernel address-space management
 *
 * Copyright (C) 2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * So far, we've depended on early head.S boot page tables setup.
 * Here we build and apply our permanent kernel mappings.
 */

#include <kernel.h>
#include <paging.h>
#include <mm.h>
#include <ioapic.h>

/*
 * Kernel's address-space master page table.
 */
static struct pml4e *kernel_pml4_table;

/*
 * Fill given PML2 table with entries mapping the virtual
 * range (@vstart - @vend) to physical @pstart upwards.
 * Prerequisite: valid table; unused entries zero memset
 */
static void map_pml2_range(struct pml2e *pml2_base, uintptr_t vstart,
			   uintptr_t vend, uintptr_t pstart)
{
	struct pml2e *pml2e;

	assert(is_aligned((uintptr_t)pml2_base, PAGE_SIZE));
	assert(is_aligned(vstart, PAGE_SIZE_2MB));
	assert(is_aligned(vend, PAGE_SIZE_2MB));
	assert(is_aligned(pstart, PAGE_SIZE_2MB));

	if ((vend - vstart) > (0x1ULL << 30))
		panic("A PML2 table cant map ranges > 1-GByte. "
		      "Given range: 0x%lx - 0x%lx", vstart, vend);

	for (pml2e = pml2_base + pml2_index(vstart);
	     pml2e <= pml2_base + pml2_index(vend - 1);
	     pml2e++) {
		assert((char *)pml2e < (char *)pml2_base + PAGE_SIZE);
		if (pml2e->present)
			panic("Mapping virtual 0x%lx to already mapped physical "
			      "page at 0x%lx", vstart, pml2e->page_base);

		pml2e->present = 1;
		pml2e->read_write = 1;
		pml2e->user_supervisor = 0;
		pml2e->__reserved1 = 1;
		pml2e->page_base = (uintptr_t)pstart >> PAGE_SHIFT_2MB;

		pstart += PML2_ENTRY_MAPPING_SIZE;
		vstart += PML2_ENTRY_MAPPING_SIZE;
	}
}

/*
 * Fill given PML3 table with entries mapping the virtual
 * range (@vstart - @vend) to physical @pstart upwards.
 * Prerequisite: valid table; unused entries zero memset
 */
static void map_pml3_range(struct pml3e *pml3_base, uintptr_t vstart,
			   uintptr_t vend, uintptr_t pstart)
{
	struct pml3e *pml3e;
	struct pml2e *pml2_base;
	struct page *page;
	uintptr_t end;

	assert(is_aligned((uintptr_t)pml3_base, PAGE_SIZE));
	assert(is_aligned(vstart, PAGE_SIZE_2MB));
	assert(is_aligned(vend, PAGE_SIZE_2MB));
	assert(is_aligned(pstart, PAGE_SIZE_2MB));

	if ((vend - vstart) > PML3_MAPPING_SIZE)
		panic("A PML3 table can't map ranges > 512-GBytes. "
		      "Given range: 0x%lx - 0x%lx", vstart, vend);

	for (pml3e = pml3_base + pml3_index(vstart);
	     pml3e <= pml3_base + pml3_index(vend - 1);
	     pml3e++) {
		assert((char *)pml3e < (char *)pml3_base + PAGE_SIZE);
		if (!pml3e->present) {
			pml3e->present = 1;
			pml3e->read_write = 1;
			pml3e->user_supervisor = 1;
			page = get_zeroed_page();
			pml3e->pml2_base = page_phys_address(page) >> PAGE_SHIFT;
		}

		pml2_base = VIRTUAL((uintptr_t)pml3e->pml2_base << PAGE_SHIFT);

		if (pml3e == pml3_base + pml3_index(vend - 1)) /* Last entry */
			end = vend;
		else
			end = vstart + PML3_ENTRY_MAPPING_SIZE;

		map_pml2_range(pml2_base, vstart, end, pstart);

		pstart += PML3_ENTRY_MAPPING_SIZE;
		vstart += PML3_ENTRY_MAPPING_SIZE;
	}
}

/*
 * Fill given PML4 table with entries mapping the virtual
 * range (@vstart - @vend) to physical @pstart upwards.
 * Prerequisite: valid table; unused entries zero memset
 */
static void map_pml4_range(struct pml4e *pml4_base, uintptr_t vstart,
			   uintptr_t vend, uintptr_t pstart)
{
	struct pml4e *pml4e;
	struct pml3e *pml3_base;
	struct page *page;
	uintptr_t end;

	assert(is_aligned((uintptr_t)pml4_base, PAGE_SIZE));
	assert(is_aligned(vstart, PAGE_SIZE_2MB));
	assert(is_aligned(vend, PAGE_SIZE_2MB));
	assert(is_aligned(pstart, PAGE_SIZE_2MB));

	if ((vend - vstart) > PML4_MAPPING_SIZE)
		panic("Mapping a virtual range that exceeds the 48-bit "
		      "architectural limit: 0x%lx - 0x%lx", vstart, vend);

	for (pml4e = pml4_base + pml4_index(vstart);
	     pml4e <= pml4_base + pml4_index(vend - 1);
	     pml4e++) {
		assert((char *)pml4e < (char *)pml4_base + PAGE_SIZE);

		if (!pml4e->present) {
			pml4e->present = 1;
			pml4e->read_write = 1;
			pml4e->user_supervisor = 1;
			page = get_zeroed_page();
			pml4e->pml3_base = page_phys_address(page) >> PAGE_SHIFT;
		}

		pml3_base = VIRTUAL((uintptr_t)pml4e->pml3_base << PAGE_SHIFT);

		if (pml4e == pml4_base + pml4_index(vend - 1)) /* Last entry */
			end = vend;
		else
			end = vstart + PML4_ENTRY_MAPPING_SIZE;

		map_pml3_range(pml3_base, vstart, end, pstart);

		pstart += PML4_ENTRY_MAPPING_SIZE;
		vstart += PML4_ENTRY_MAPPING_SIZE;
	}
}

/*
 * Map given kernel virtual region to physical @pstart upwards.
 * @vstart is region start, while @vlen is its length. All
 * sanity checks are done in the map_pml{2,3,4}_range() code
 * where all the work is really done.
 */
static void map_kernel_range(uintptr_t vstart, uint64_t vlen, uintptr_t pstart)
{
	assert(is_aligned(vstart, PAGE_SIZE_2MB));
	assert(is_aligned(vlen, PAGE_SIZE_2MB));
	assert(is_aligned(pstart, PAGE_SIZE_2MB));

	map_pml4_range(kernel_pml4_table, vstart, vstart + vlen, pstart);
}

void memory_map_init(void)
{
	struct page *pml4_page;

	pml4_page = get_zeroed_page();
	kernel_pml4_table = page_address(pml4_page);

	map_kernel_range(KTEXT_BASE, 0x40000000, 0);

	/* Map first physical 1GB till we have the right
	 * abstractions to get physical memory end */
	map_kernel_range(VIRTUAL_BASE, 0x40000000, 0);

	/* Temporarily map APIC and IOAPIC MMIO addresses */
	map_kernel_range(IOAPIC_VRBASE, 2 * PAGE_SIZE_2MB, IOAPIC_PHBASE);

	/* Heaven be with us .. */
	load_cr3(page_phys_address(pml4_page));
}
