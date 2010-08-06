/*
 * Kernel virtual memory
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
#include <e820.h>
#include <vm.h>
#include <tests.h>

/*
 * Kernel's address-space master page table.
 */
static struct pml4e *kernel_pml4_table;

/*
 * Fill given PML2 table with entries mapping the virtual
 * range (@vstart - @vend) to physical @pstart upwards.
 *
 * Note-1! pass a valid table; unused entries must be zero
 * Note-2! range edges, and @pstart must be 2-MBytes aligned
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
 *
 * Note-1! pass a valid table; unused entries must be zero
 * Note-2! range edges, and @pstart must be 2-MBytes aligned
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
			pml3e->pml2_base = page_phys_addr(page) >> PAGE_SHIFT;
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
 *
 * Note-1! pass a valid table; unused entries must be zero
 * Note-2! range edges, and @pstart must be 2-MBytes aligned
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
			pml4e->pml3_base = page_phys_addr(page) >> PAGE_SHIFT;
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
 *
 * Note-1! Given range, and its subranges, must be unmapped
 * Note-2! region edges, and @pstart must be 2-MBytes aligned
 */
static void map_kernel_range(uintptr_t vstart, uint64_t vlen, uintptr_t pstart)
{
	assert(is_aligned(vstart, PAGE_SIZE_2MB));
	assert(is_aligned(vlen, PAGE_SIZE_2MB));
	assert(is_aligned(pstart, PAGE_SIZE_2MB));

	map_pml4_range(kernel_pml4_table, vstart, vstart + vlen, pstart);
}

/*
 * Check if given virtual address is mapped at our permanent
 * kernel page tables. If so, also assure that given address
 * is mapped to the expected physical address.
 */
static bool vaddr_is_mapped(void *vaddr)
{
	struct pml4e *pml4e;
	struct pml3e *pml3e;
	struct pml2e *pml2e;

	assert(kernel_pml4_table != NULL);
	assert((uintptr_t)vaddr >= KERN_PAGE_OFFSET);
	assert((uintptr_t)vaddr < KERN_PAGE_END_MAX);

	pml4e = kernel_pml4_table + pml4_index(vaddr);
	if (!pml4e->present)
		return false;

	pml3e = pml3_base(pml4e);
	pml3e += pml3_index(vaddr);
	if (!pml3e->present)
		return false;

	pml2e = pml2_base(pml3e);
	pml2e += pml2_index(vaddr);
	if (!pml2e->present)
		return false;

	assert((uintptr_t)page_base(pml2e) ==
	       round_down((uintptr_t)vaddr, PAGE_SIZE_2MB));
	return true;
}

/*
 * Map given physical range (@pstart -> @pstart+@len) at
 * kernel physical mappings space. Return mapped virtual
 * address.
 */
void *vm_kmap(uintptr_t pstart, uint64_t len)
{
	uintptr_t pend;
	void *vstart, *ret;

	assert(len > 0);
	pend = pstart + len;

	if (pend >= KERN_PHYS_END_MAX)
		panic("VM - Mapping physical region [0x%lx - 0x%lx] "
		      ">= max supported physical addresses end 0x%lx",
		      pstart, pend, KERN_PHYS_END_MAX);

	ret = VIRTUAL(pstart);
	pstart = round_down(pstart, PAGE_SIZE_2MB);
	pend = round_up(pend, PAGE_SIZE_2MB);

	while (pstart < pend) {
		vstart = VIRTUAL(pstart);
		if (!vaddr_is_mapped(vstart))
			map_kernel_range((uintptr_t)vstart,
					 PAGE_SIZE_2MB, pstart);

		pstart += PAGE_SIZE_2MB;
	}

	return ret;
}

/*
 * Ditch boot page tables and build kernel permanent,
 * dynamically handled, ones.
 */
void vm_init(void)
{
	struct page *pml4_page;
	uint64_t phys_end;

	pml4_page = get_zeroed_page();
	kernel_pml4_table = page_address(pml4_page);

	/* Map 512-MByte kernel text area */
	map_kernel_range(KTEXT_PAGE_OFFSET, KTEXT_AREA_SIZE, KTEXT_PHYS_OFFSET);

	/* Map the entire available physical space */
	phys_end = e820_get_phys_addr_end();
	phys_end = round_up(phys_end, PAGE_SIZE_2MB);
	map_kernel_range(KERN_PAGE_OFFSET, phys_end, KERN_PHYS_OFFSET);
	printk("Memory: Mappnig range 0x%lx -> 0x%lx to physical 0x0\n",
	       KERN_PAGE_OFFSET, KERN_PAGE_OFFSET + phys_end);

	/* Heaven be with us .. */
	load_cr3(page_phys_addr(pml4_page));
}

/*
 * Kernel virtual memory test cases
 */

#if	VM_TESTS

/*
 * Check if all physical memory is really
 * mapped as supossed to be; bailout otherwise
 */
static void vm_check_phys_memory(void)
{
	uint64_t phys_end;

	phys_end = e820_get_phys_addr_end();

	for (uintptr_t vaddr = KERN_PAGE_OFFSET;
	     vaddr < (KERN_PAGE_OFFSET + phys_end); vaddr++) {

		/* This also assures that given address is mapped
		 * to the expected physical address, according to
		 * our kernel address space mapping rules */
		if (!vaddr_is_mapped((char *)vaddr))
			panic("_VM: Reporting supposedly mapped address 0x%lx "
			      "as unmapped", vaddr);

		/* Limit the output a bit .. */
		if (vaddr > (KERN_PAGE_OFFSET + 0x20000) &&
		    is_aligned(vaddr, 0x200000))
			printk("Success: e820-avail phys range [0x%lx - 0x%lx] "
			       "mapped\n", PHYS(vaddr - 0x200000), PHYS(vaddr));
	}
}

/*
 * Check if 1-byte regions mapped using kmap()
 * are really mapped, with right values
 */
static void vm_check_kmap1(void)
{
	uintptr_t paddr;
	void *vaddr;
	int count;

	count = PAGE_SIZE_2MB * 10;
	paddr = 0x100000000000;

	while (count--) {
		vaddr = vm_kmap(paddr, 1);
		assert(vaddr == VIRTUAL(paddr));

		if (!vaddr_is_mapped(vaddr))
			panic("_VM: Reporting supposedly mapped address 0x%lx "
			      "as unmapped", vaddr);

		if (is_aligned(paddr, 0x200000))
			printk("Success: phys addrs [0x%lx - 0x%lx] mapped\n",
			       paddr - 0x200000, paddr);

		paddr++;
	}
}

/*
 * Check if variable length regions mapped using
 * kmap() are really mapped, with right values
 */
static void vm_check_kmap2(void)
{
	uintptr_t paddr;
	void *vaddr;
	int count, len;

	count = PAGE_SIZE_2MB * 10;
	paddr = 0x200000000000;

	for (len = 1; len <= count; len += PAGE_SIZE_2MB/8) {

		/* To let the test be effective, assure we're
		 * mapping previously unmapped address */
		assert(!vaddr_is_mapped((void *)round_up((uintptr_t)
		       VIRTUAL(paddr), PAGE_SIZE_2MB)));

		vaddr = vm_kmap(paddr, len);
		assert(vaddr == VIRTUAL(paddr));

		for (int i = 0; i < len; i++) {
			if (!vaddr_is_mapped(vaddr))
				panic("_VM: Reporting supposedly mapped "
				      "address 0x%lx as unmapped", vaddr);
			vaddr++;
		}

		printk("Success: [region len=0x%lx] phys [0x%lx - 0x%lx] "
		       "mapped\n", len, paddr, paddr + len);

		paddr += len;
	}
}

/*
 * Test cases driver
 */
void vm_run_tests(void)
{
	vm_check_phys_memory();
	vm_check_kmap1();
	vm_check_kmap2();
}

#endif	/* VM_TESTS */
