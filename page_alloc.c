/*
 * Memory management: Page allocator & the pfdtable
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * In an earlier stage, we've switched to real-mode and acquired the
 * BIOS's ACPI E820h memory map, which includes details on available and
 * reserved memory pages. The real-mode code passes the map entries to
 * the rest of the kernel in a below 1MB structure defined in e820.h
 *
 * Using this data, we build our page allocator, which returns free
 * available physical pages to the rest of the system upon request.
 */

#include <kernel.h>
#include <stdint.h>
#include <paging.h>
#include <sections.h>
#include <spinlock.h>
#include <e820.h>
#include <mm.h>

/*
 * Page frame descriptor
 */
struct page {
	uint64_t pfn;			/* Phys addr = pfn << PAGE_SHIFT */
	struct page *next;		/* If in pfdfree_head, next free page */
	int free;			/* If 0, this page is not allocated */
};

/*
 * Each physical page available for us to use and above our
 * kernel memory area is represented by a descriptor in below
 * 'page frame descriptor table' (SVR2 terminology).
 *
 * This table is stored directly after kernel's text, data,
 * and stack.
 *
 * @pfdtable_last: last added entry to the table, or NULL
 * @pfdtable_end: don't exceed this dynamically set mark
 */
static struct page *pfdtable = (struct page *)__kernel_end;
static struct page *pfdtable_last;
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
 * Watermak the end of our kernel memory area which is above
 * 1MB and which also contains the pfdtable. NOTE! 4K-align.
 */
static uint64_t kmem_end = -1;

/*
 * Create new pfdtable entries for given memory range, which
 * should be e820 available and above our kernel memory area.
 *
 * NOTE! No need to acquire the pfdfree lock as this should
 * _only_ be called from the serial memory init path.
 *
 * Prerequisite: kernel memory area end calculated
 */
static void pfdtable_add_range(struct e820_range *range)
{
	uint64_t start, end, nr_pages;
	struct page *page;

	assert(range->type == E820_AVAIL);

	start = range->base;
	end = range->base + range->len;
	assert(IS_PAGE_ALIGNED(start));
	assert(IS_PAGE_ALIGNED(end));
	assert(IS_PAGE_ALIGNED(kmem_end));
	assert(start < end);
	assert(start >= (uintptr_t)PHYS(kmem_end));

	/* New entries shouldn't overflow the table */
	page = (pfdtable_last) ? pfdtable_last + 1 : pfdtable;
	nr_pages = (end - start) / PAGE_SIZE;
	assert((page + nr_pages) <= pfdtable_end);

	while (start != end) {
		page->free = 1;
		page->pfn = start >> PAGE_SHIFT;
		page->next = pfdfree_head;
		pfdfree_head = page;

		pfdfree_count++;
		page++;
		start += PAGE_SIZE;
	}

	pfdtable_last = page - 1;
}

/*
 * E820h entries parsing
 */

/*
 * Modify given e820-available range to meet our standards:
 * - we work with memory in units of pages: page-align
 *   given range if possible, or bailout.
 * - treat ranges inside our kernel mem area as reserved.
 */
static int sanitize_e820_range(struct e820_range *range)
{
	uint64_t start, end;

	assert(range->type == E820_AVAIL);
	start = range->base;
	end = start + range->len;

	start = round_up(start, PAGE_SIZE);
	end = round_down(end, PAGE_SIZE);

	if (end <= start) {
		range->type = E820_ERRORMEM;
		return -1;
	}

	assert(IS_PAGE_ALIGNED(kmem_end));
	if (end <= (uintptr_t)PHYS(kmem_end))
		return -1;
	if (start < (uintptr_t)PHYS(kmem_end))
		start = (uintptr_t)PHYS(kmem_end);

	range->base = start;
	range->len = end - start;

	return 0;
}

/*
 * Get the number of pages marked by the BIOS E820h service
 * as available, by parsing the rmode-returned e820h struct.
 * This is needed to calculate reserved pfdtable area size.
 * Validate the rmode-returned E820h-struct in the process.
 */
static int get_e820_avail_pages(void)
{
	uint32_t *entry, entry_len, err;
	uint64_t avail_pages, avail_len;
	struct e820_range *range;

	entry = VIRTUAL(E820_BASE);
	if (*entry != E820_INIT_SIG)
		panic("E820 - Invalid buffer start signature");
	entry++;

	avail_len = 0;
	while (*entry != E820_END) {
		if ((uintptr_t)PHYS(entry) >= E820_MAX)
			panic("E820 - Unterminated buffer structure");

		entry_len = *entry++;

		range = (struct e820_range *)entry;
		if (range->type == E820_AVAIL)
			avail_len += range->len;
		printk("Memory: E820 range: 0x%lx - 0x%lx (%s)\n", range->base,
		       range->base + range->len, e820_typestr(range->type));

		entry = (typeof(entry))((char *)entry + entry_len);
	}
	entry++;

	err = *entry++;
	if (err != E820_SUCCESS)
		panic("E820 error - %s", e820_errstr(err));
	assert(entry <= (typeof(entry))VIRTUAL(E820_MAX));

	avail_pages = avail_len / PAGE_SIZE;
	return avail_pages;
}

/**
 * e820_for_each     -	iterate over the E820h-struct
 * @entry:	the iterator, struct e820_entry
 * @entry_len:	the length of last entry (to go to the next)
 *
 * Prerequisite: rmode E820h-struct already validated
 */
#define e820_for_each(entry, entry_len)				\
	for (entry = VIRTUAL(E820_BASE) + sizeof(*entry);	\
	     *entry != E820_END;				\
	     entry = (typeof(entry))((char *)entry + entry_len))

/*
 * Build system's page frame descriptor table (pfdtable), the
 * core structure of the page allocator, by parsing the E820h
 * struct returned by real-mode code (e820.h)
 * @avail_pages: number of available pages in the system
 */
static void pfdtable_init(uint64_t avail_pages)
{
	uint32_t *entry, entry_len;
	struct e820_range *range;

	pfdtable_end = pfdtable + avail_pages;
	printk("Memory: Page Frame descriptor table size = %d KB\n",
	       (avail_pages * sizeof(pfdtable[0])) / 1024);

	kmem_end = round_up((uintptr_t)pfdtable_end, PAGE_SIZE);
	printk("Memory: Kernel memory area end = 0x%lx\n", kmem_end);

	/* Including add_range(), this loop is O(n), where
	 * n = number of available memory pages in the system */
	e820_for_each(entry, entry_len) {
		entry_len = *entry++;

		range = (struct e820_range *)entry;
		if (range->type != E820_AVAIL)
			continue;
		if (sanitize_e820_range(range))
			continue;

		pfdtable_add_range(range);
	}
}

/*
 * Page allocation and reclamation, in O(1)
 * NOTE! do not call those in IRQ-context
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

void free_page(struct page *page)
{
	spin_lock(&pfdfree_lock);

	page->next = pfdfree_head;
	pfdfree_head = page;
	pfdfree_count++;

	if (page->free != 0)
		panic("Memory - Freeing already free page at 0x%lx\n",
		      page->pfn << PAGE_SHIFT);
	page->free = 1;

	spin_unlock(&pfdfree_lock);
}

void pagealloc_init(void)
{
	uint64_t pages;

	pages = get_e820_avail_pages();
	printk("Memory: Available memory for use = %d MB\n",
	       ((pages * PAGE_SIZE) / 1024) / 1024);

	pfdtable_init(pages);
}

/*
 * Page allocator test cases.
 *
 * Thanks to NewOS's Travis Geiselbrecht (giest) for aiding
 * me in outlying different MM testing scenarious over IRC!
 */

#ifdef  PAGEALLOC_TESTS

#include <string.h>
#include <paging.h>

/*
 * Keep track of pages we allocate and free
 */
#define PAGES_COUNT	50000
static struct page *pages[PAGES_COUNT];
static char tmpbuf[PAGE_SIZE];

/*
 * Assure all e820-available memory spaces are covered in
 * pfdtable cells and are marked as free.
 */
static int _test_pfdfree_count(void) {
	struct e820_range *range;
	uint32_t *entry, entry_len;
	int count;

	count = 0;
	e820_for_each(entry, entry_len) {
		entry_len = *entry++;

		range = (struct e820_range *)entry;
		if (range->type != E820_AVAIL)
			continue;
		if (sanitize_e820_range(range))
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
 * Prerequisite: rmode E820h-struct previously validated
 */
static int _test_pfdtable_add_range(void) {
	uint32_t *entry, entry_len;
	int old_count, repeat;
	struct e820_range *range, subrange;

	/* Clear pfdtable structures */
	old_count = pfdfree_count;
	pfdfree_count = 0;
	pfdtable_last = NULL;
	pfdfree_head = NULL;

	/* Refill the table, page by page */
	e820_for_each(entry, entry_len) {
		entry_len = *entry++;

		range = (struct e820_range *)entry;

		if (range->type != E820_AVAIL)
			continue;
		if (sanitize_e820_range(range))
			continue;

		subrange = *range;
		repeat = subrange.len / PAGE_SIZE;
		subrange.len = PAGE_SIZE;
		while (repeat--) {
			pfdtable_add_range(&subrange);
			subrange.base += PAGE_SIZE;
		}
	}

	if (old_count != pfdfree_count)
		panic("_Memory: Refilling pfdtable found %d free pages, %d"
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
	uint32_t *entry, entry_len;
	uintptr_t start, end, addr;
	struct e820_range *range;

	addr = page->pfn << PAGE_SHIFT;
	if (addr < (uintptr_t)PHYS(kmem_end))
		return false;

	e820_for_each(entry, entry_len) {
		entry_len = *entry++;

		range = (struct e820_range *)entry;
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
static void _disrupt(void)
{
	struct page *p;

	p = get_free_page();
	free_page(p);
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

	old_pfdfree_count = pfdfree_count;

	for (i = 0; i < 100; i++)
		_disrupt();

	for (i = nr_pages - 1; i >= 0; i--) {
		_disrupt();
		pages[i] = get_free_page();
		if (!_page_is_free(pages[i]))
			panic("_Memory: Allocated invalid page address "
			      "0x%lx\n", pages[i]->pfn << PAGE_SHIFT);

		addr = VIRTUAL(pages[i]->pfn << PAGE_SHIFT);
		memset32(addr, i, PAGE_SIZE);
	}

	for (i = 0; i < nr_pages; i++) {
		memset32(tmpbuf, i, PAGE_SIZE);
		addr = VIRTUAL(pages[i]->pfn << PAGE_SHIFT);
		res = strncmp(addr, tmpbuf, PAGE_SIZE);
		if (res)
			panic("_Memory: FAIL: [%d] page at 0x%lx got corrupted\n",
			       i, PHYS(addr));

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
static void _test_pagealloc_exhaustion(void)
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
	uint64_t count;

	count = pfdfree_count;
	if (count > PAGES_COUNT)
		count = PAGES_COUNT;

	_test_pfdfree_count();
	_test_pfdtable_add_range();

	for (int i = 0; i < 100; i++) {
		printk("[%d] ", i);
		_test_pagealloc_coherency(count);
	}

	_test_pagealloc_exhaustion();
}

#endif /* PAGEALLOC_TEST */
