/*
 * Memory management: the page allocator
 *
 * Copyright (C) 2009-2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * The page allocator returns free available physical memory
 * pages to the rest of the kernel upon request.
 */

#include <kernel.h>
#include <stdint.h>
#include <string.h>
#include <paging.h>
#include <sections.h>
#include <spinlock.h>
#include <e820.h>
#include <mm.h>
#include <tests.h>

/*
 * Each physical page available for us to use and above our
 * kernel memory area is represented by a descriptor in below
 * 'page frame descriptor table' (SVR2 terminology).
 *
 * This table is stored directly after kernel's text, data,
 * and stack.
 *
 * @pfdtable_top: current table end mark
 * @pfdtable_end: don't exceed this dynamically set mark
 */
static struct page *pfdtable;
static struct page *pfdtable_top;
static struct page *pfdtable_end;

/*
 * All pages in the pfdtable are available for use by the OS.
 * This list head connects the pfdtable entries which are also
 * free. Pages are popped and pushed to this list by in-place
 * changing of the links connecting the descriptors.
 */
static spinlock_t pfdfree_lock = SPIN_UNLOCKED();
static struct page *pfdfree_head;
static uint64_t pfdfree_count;

/*
 * Virtual-address to a page-descriptor map
 *
 * This structure aids in reverse-mapping a virtual address
 * to its relative page descriptor. We store e820 page descs
 * sequentially, thus a reference to a range and the first
 * pfdtable cell it's represented by, is enough.
 */
struct rmap {
	struct e820_range range;
	struct page *pfd_start;
};

/*
 * @pfdrmap: The global table used to reverse-map an address
 * @pfdrmap_top, @pfdrmap_end: similar to @pfdtable above
 */
static struct rmap *pfdrmap;
static struct rmap *pfdrmap_top;
static struct rmap *pfdrmap_end;

static void rmap_add_range(struct e820_range *, struct page *);

/*
 * Watermak the end of our kernel memory area which is above
 * 1MB and which also contains the pfdtable. NOTE! 4K-align.
 */
static uint64_t kmem_end = -1;

/*
 * Create new pfdtable entries for given memory range, which
 * should be e820 available and above our kernel memory area.
 * @pfdtable_top: current pfdtable end mark
 *
 * NOTE! No need to acquire the pfdfree lock as this should
 * _only_ be called from the serial memory init path.
 *
 * Return the new new pfdtable top mark.
 * Prerequisite: kernel memory area end calculated
 */
static void pfdtable_add_range(struct e820_range *range)
{
	uint64_t start, end, nr_pages;
	struct page *page;

	assert(range->type == E820_AVAIL);

	start = range->base;
	end = range->base + range->len;
	assert(page_aligned(start));
	assert(page_aligned(end));
	assert(page_aligned(kmem_end));
	assert(start < end);
	assert(start >= (uintptr_t)PHYS(kmem_end));

	/* New entries shouldn't overflow the table */
	page = pfdtable_top;
	nr_pages = (end - start) / PAGE_SIZE;
	assert((page + nr_pages) <= pfdtable_end);

	rmap_add_range(range, page);

	while (start != end) {
		page_init(page, start);

		page->next = pfdfree_head;
		pfdfree_head = page;
		pfdfree_count++;

		page++;
		start += PAGE_SIZE;
	}

	pfdtable_top = page;
}

/*
 * Add a reverse-mapping entry for given e820 range.
 * @start: first pfdtable cell represenging @range.
 * NOTE! no locks; access this only from init paths.
 */
static void rmap_add_range(struct e820_range *range,
			   struct page *start)
{
	struct rmap *rmap;

	rmap = pfdrmap_top;

	assert(rmap + 1 <= pfdrmap_end);
	rmap->range = *range;
	rmap->pfd_start = start;

	pfdrmap_top = rmap + 1;
}

/*
 * Page allocation and reclamation, in O(1)
 * NOTE! do not call those in IRQ-context!
 */

struct page *get_free_page(void)
{
	struct page *page;

	spin_lock(&pfdfree_lock);

	if (!pfdfree_head)
		panic("Memory - No more free pages available");

	page = pfdfree_head;
	pfdfree_head = pfdfree_head->next;
	pfdfree_count--;

	assert(page->free == 1);
	page->free = 0;

	spin_unlock(&pfdfree_lock);

	return page;
}

struct page *get_zeroed_page(void)
{
	struct page *page;

	page = get_free_page();
	memset64(page_address(page), 0, PAGE_SIZE);

	return page;
}

void free_page(struct page *page)
{
	spin_lock(&pfdfree_lock);

	page->next = pfdfree_head;
	pfdfree_head = page;
	pfdfree_count++;

	if (page->free != 0)
		panic("Memory - Freeing already free page at 0x%lx\n",
		      page_address(page));
	page->free = 1;

	spin_unlock(&pfdfree_lock);
}

/*
 * Return the page descriptor representing @addr.
 * FIXME: Can't we optimize this more?
 */
struct page *addr_to_page(void *addr)
{
	struct rmap *rmap;
	struct page *page;
	struct e820_range *range;
	uint64_t paddr, start, end, offset;

	paddr = (uint64_t)PHYS(addr);
	paddr = round_down(paddr, PAGE_SIZE);
	for (rmap = pfdrmap; rmap != pfdrmap_top; rmap++) {
		range = &(rmap->range);
		start = range->base;
		end = start + range->len;

		if (paddr < start)
			continue;
		if (paddr >= end)
			continue;

		page = rmap->pfd_start;
		offset = (paddr - start) / PAGE_SIZE;
		page += offset;
		assert(page_phys_addr(page) < end);
		return page;
	}

	panic("Memory - No page descriptor found for given 0x%lx "
	      "address", addr);

	return NULL;
}

/*
 * Initialize the page allocator by building its core
 * struct, the page frame descriptor table (pfdtable)
 */
void pagealloc_init(void)
{
	struct e820_range *range;
	struct e820_setup *setup;
	uint64_t avail_pages, avail_ranges;

	/*
	 * While building the page descriptors in the pfdtable,
	 * we want to be sure not to include pages that override
	 * our own pfdtable area. i.e. we want to know the
	 * pfdtable area length _before_ forming its entries.
	 *
	 * Since the pfdtable length depends on available phys
	 * memory, we move over the e820 map in two passes.
	 *
	 * First pass: estimate pfdtable area length by counting
	 * provided e820-availale pages and ranges.
	 */

	setup = e820_get_memory_setup();
	avail_pages = setup->avail_pages;
	avail_ranges = setup->avail_ranges;

	printk("Memory: Available physical memory = %d MB\n",
	       ((avail_pages * PAGE_SIZE) / 1024) / 1024);

	pfdtable = VIRTUAL(KTEXT_PHYS(__kernel_end));
	pfdtable_top = pfdtable;
	pfdtable_end = pfdtable + avail_pages;

	printk("Memory: Page Frame descriptor table size = %d KB\n",
	       (avail_pages * sizeof(pfdtable[0])) / 1024);

	pfdrmap = (struct rmap *)pfdtable_end;
	pfdrmap_top = pfdrmap;
	pfdrmap_end = pfdrmap + avail_ranges;

	kmem_end = round_up((uintptr_t)pfdrmap_end, PAGE_SIZE);
	printk("Memory: Kernel memory area end = 0x%lx\n", kmem_end);

	/*
	 * Second Pass: actually fill the pfdtable entries
	 *
	 * Including add_range(), this loop is O(n), where
	 * n = number of available memory pages in the system
	 */

	e820_for_each(range) {
		if (range->type != E820_AVAIL)
			continue;
		if (e820_sanitize_range(range, kmem_end))
			continue;

		pfdtable_add_range(range);
	}
}

/*
 * Page allocator test cases.
 *
 * Thanks to NewOS's Travis Geiselbrecht (giest) for aiding
 * me in outlying different MM testing scenarious over IRC!
 */

#if	PAGEALLOC_TESTS

#include <string.h>
#include <paging.h>

/*
 * Keep track of pages we allocate and free
 */
#define PAGES_COUNT	100000
static struct page *pages[PAGES_COUNT];
static char tmpbuf[PAGE_SIZE];

/*
 * Assure all e820-available memory spaces are covered in
 * pfdtable cells and are marked as free.
 */
static int __unused _test_pfdfree_count(void) {
	struct e820_range *range;
	int count;

	count = 0;
	e820_for_each(range) {
		if (range->type != E820_AVAIL)
			continue;
		if (e820_sanitize_range(range, kmem_end))
			continue;

		count += range->len / PAGE_SIZE;
	}

	if (count != pfdfree_count)
		panic("_Memory: Available e820 pages = %d, pfdtable avail "
		      "pages = %d\n", count, pfdfree_count);

	printk("_Memory: %s: Success\n", __FUNCTION__);
	return true;
}

/*
 * Most BIOSes represent the above-1MB available memory pages
 * in one contiguous e820 range. We stress add_avail_range()
 * by chunking the range additions per page basis, leading to
 * emulating addition of non-contiguous e820 available ranges.
 *
 * This 'test' messes _heavily_ with the allocator internals.
 *
 * Prerequisite: rmode E820h-struct previously validated
 */
static int __unused _test_pfdtable_add_range(void) {
	uint64_t old_end, old_count, repeat, ranges;
	struct e820_range *range, subrange;
	struct rmap *rmap;

	/* Each range is to be transformed to x ranges, where x =
	 * the number of pages in that range. Modify the pfdrmap
	 * table end mark accordingly */
	ranges = 0;
	for (rmap = pfdrmap; rmap != pfdrmap_top; rmap++) {
		range = &(rmap->range);
		ranges += range->len / PAGE_SIZE;
	}
	pfdrmap_top = pfdrmap;
	pfdrmap_end = pfdrmap + ranges;

	/* The pfdrmap became much larger: extend our memory area */
	old_count = pfdfree_count;
	old_end = kmem_end;
	kmem_end = round_up((uintptr_t)pfdrmap_end, PAGE_SIZE);

	/* Clear pfdtable structures */
	pfdtable_top = pfdtable;
	pfdfree_count = 0;
	pfdfree_head = NULL;

	/* Refill the table, page by page */
	e820_for_each(range) {
		if (range->type != E820_AVAIL)
			continue;
		if (e820_sanitize_range(range, kmem_end))
			continue;

		subrange = *range;
		repeat = subrange.len / PAGE_SIZE;
		subrange.len = PAGE_SIZE;
		while (repeat--) {
			pfdtable_add_range(&subrange);
			subrange.base += PAGE_SIZE;
		}
	}

	/* More kernel memory area led to less available pages.
	 * Compare old and new count with this fact in mind */
	old_count -= ((kmem_end - old_end) / PAGE_SIZE);
	if (old_count != pfdfree_count)
		panic("_Memory: Refilling pfdtable found %d free pages; %d "
		      "pages originally reported", pfdfree_count, old_count);

	printk("_Memory: %s: Success\n", __FUNCTION__);
	return true;
}

/*
 * Check if given page frame is allocation-valid.
 * Prerequisite: rmode E820h-struct previously validated
 */
static int _page_is_free(struct page *page)
{
	uintptr_t start, end, addr;
	struct e820_range *range;

	addr = page_phys_addr(page);
	if (addr < (uintptr_t)PHYS(kmem_end))
		return false;

	e820_for_each(range) {
		if (range->type != E820_AVAIL)
			continue;
		start = range->base;
		end = start + range->len;
		if (addr >= start && addr < end)
			return true;
	}

	return false;
}

/*
 * As a way of page allocator disruption, allocate
 * then free a memory page. Is this really effective?
 */
static inline void _disrupt(void)
{
	struct page *p1, *p2;
	void *addr;

	p1 = get_zeroed_page();

	/* Test reverse mapping */
	addr = page_address(p1);
	p2 = addr_to_page(addr);
	if (p1 != p2)
		panic("_Memory: FAIL: Reverse mapping addr=0x%lx lead to "
		      "page descriptor 0x%lx; it's actually for page %lx\n",
		      addr, p2, p1);

	memset32(addr, 0xffffffff, PAGE_SIZE);
	free_page(p1);
}

/*
 * Check page allocator results coherency by allocating given
 * number of pages and checking them against corruption at
 * different points.
 *
 * If max available number of free pages is given, then
 * iterative calls will also test against any page leaks.
 */
static int _test_pagealloc_coherency(int nr_pages)
{
	int i, res;
	uint64_t old_pfdfree_count;
	void *addr;
	struct page *page;

	old_pfdfree_count = pfdfree_count;

	for (i = 0; i < 100; i++)
		_disrupt();

	for (i = nr_pages - 1; i >= 0; i--) {
		_disrupt();
		pages[i] = get_free_page();
		if (!_page_is_free(pages[i]))
			panic("_Memory: Allocated invalid page address "
			      "0x%lx\n", page_phys_addr(pages[i]));

		/* Test reverse mapping */
		addr = page_address(pages[i]);
		page = addr_to_page(addr);
		if (page != pages[i])
			panic("_Memory: FAIL: Reverse mapping addr=0x%lx lead to "
			      "page descriptor 0x%lx, while it's actually on %lx\n",
			      addr, page, pages[i]);

		addr = page_address(pages[i]);
		memset32(addr, i, PAGE_SIZE);
	}

	for (i = 0; i < nr_pages; i++) {
		memset32(tmpbuf, i, PAGE_SIZE);
		addr = page_address(pages[i]);
		res = memcmp(addr, tmpbuf, PAGE_SIZE);
		if (res)
			panic("_Memory: FAIL: [%d] page at 0x%lx got corrupted\n",
			       i, PHYS(addr));

		/* Test reverse mapping */
		page = addr_to_page(addr);
		if (page != pages[i])
			panic("_Memory: FAIL: Reverse mapping addr=0x%lx lead to "
			      "page descriptor 0x%lx, while it's actually on %lx\n",
			      addr, page, pages[i]);

		free_page(pages[i]);
		_disrupt();
	}

	for (i = 0; i < 100; i++)
		_disrupt();

	/* We've freed everything we allocated, pfdfree_count
	 * should not change */
	if (pfdfree_count != old_pfdfree_count)
		panic("_Memory: page leak; pfdfree_count = %ld, older value = "
		       "%ld\n", pfdfree_count, old_pfdfree_count);

	printk("_Memory: %s: Success\n", __FUNCTION__);
	return 0;
}

/*
 * Consume all system memory then observe the result
 * of overallocation.
 */
static void __unused _test_pagealloc_exhaustion(void)
{
	uint64_t count;

	count = pfdfree_count;
	printk("_Memory: %d free pages found\n", count);
	while (count--)
		(void)get_free_page();

	printk("_Memory: Allocated all free pages. System should panic from "
	       "the next allocation");
	(void)get_free_page();
}

/*
 * Page allocation tests driver
 */
void pagealloc_run_tests(void)
{
	uint64_t count, repeat;

	repeat = 100;

	/* _test_pfdfree_count(); */
	/* _test_pfdtable_add_range(); */

	count = pfdfree_count;
	if (count > PAGES_COUNT)
		count = PAGES_COUNT;

	/* Burn, baby, burn */
	for (int i = 0; i < repeat; i++) {
		printk("[%d] ", i);
		_test_pagealloc_coherency(count);
	}

	/* _test_pagealloc_exhaustion(); */
}

#endif /* PAGEALLOC_TESTS */
