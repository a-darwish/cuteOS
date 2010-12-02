#ifndef _MM_H
#define _MM_H

/*
 * Memory Management (MM) bare bones
 *
 * Copyright (C) 2009-2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * MM terminology:
 *
 * o Page Frame: a physical 4-KBytes page, naturally 0x1000-aligned.
 *
 * o Available Page: a page frame marked as available (not reserved) for
 *   system use by the ACPI e820h service.
 *
 * o Page Frame Descriptor: one for each available page frame in the sy-
 *   stem, collecting important details for that page, like whether it's
 *   allocated (by the page allocator), or free.
 *
 * o Page Frame Descriptor Table (pfdtable): A table (array) collecting
 *   all the system's page frame descriptors, and thus statically repre-
 *   senting all system's available RAM.
 *
 * o Page Allocator: an MM module which allocates and reclaims (manages)
 *   memory pages for the rest of the system. Naturally, it manages
 *   these 4K-pages through their descriptors state.
 *
 * o Zone: A special area of physical memory; e.g., we can request a page
 *   from the page allocator, but only from the first physical GB 'zone'.
 *
 * o Bucket Allocator: our 'malloc' service, managing dynamic variable-
 *   sized memory regions (the heap) upon request. First, it allocates
 *   memory - in pages - from the page allocator, and then it slices and
 *   dices them as it sees fit.
 *
 * o Slab (object-caching) Allocator: we don't have one; the bucket all-
 *   ocator is more than enough for our purposes so far.
 */

#include <kernel.h>
#include <stdint.h>
#include <paging.h>
#include <tests.h>

/*
 * Page allocator Zones
 *
 * We divide the physical memory space to 'zones' according to
 * need. The main rational for adding zones support to the page
 * allocator so far is ZONE_1GB.
 *
 * Please order the zones IDs, beginning from 0 upwards, with
 * no gaps, according to their prioririty: the smaller the ID,
 * the higher the zone's priority. Each ID except NULL is used
 * as an index in the zones descriptors table.
 *
 * If the number of zones (including NULL) exceeded four, don't
 * forget to extend the 'struct page' zone_id field from 2 to 3
 * bits: it's essential to save space in that struct.
 */
enum zone_id {
	/* Allocate pages from the first GByte
	 *
	 * At early boot, where permanent kernel page tables
	 * are not yet ready, PAGE_OFFSET-based addresses are
	 * only set-up for the first physical GB by head.S
	 *
	 * At that state, the subset of virtual adddresses
	 * representing phys regions > 1-GB are unmapped. If
	 * the system has > 1-GB RAM, the page allocator will
	 * then return some unmapped virt addresses, leading
	 * to #PFs at early boot - Zones_1GB raison d'être.
	 *
	 * IMP: Before and while building kernel's permanent
	 * page tables at vm_init(), _only_ use this zone! */
	ZONE_1GB = 0,

	/* Allocate pages from any zone
	 *
	 * Beside acting as a flag, this zone also represents
	 * all pages in the system with no special semantics
	 * to us. Thus, always make it the least-priority one.
	 *
	 * If we requested a page from this zone, the page
	 * allocator will get free pages from the least prio-
	 * rity zone first, reserving precious memory areas
	 * as far as possible */
	ZONE_ANY = 1,

	/* Undefined; the `NULL' zone! */
	ZONE_UNASSIGNED = 2,
};

/*
 * Page Frame Descriptor
 *
 * It's essential to save space in this struct since there's
 * one for each available page frame in the system.
 *
 * Additionally, a smaller pfd means a _much_ smaller pfdta-
 * ble, lessening this table's probability of hitting a res-
 * erved memory area like the legacy ISA and PCI holes.
 */
struct page {
	uint64_t pfn:(64 - PAGE_SHIFT),	/* Phys addr = pfn << PAGE_SHIFT */
		free:1,			/* Not allocated? */
		in_bucket:1,		/* Used by the bucket-allocator? */
		zone_id:2;		/* The zone we're assigned to */

	union {
		struct page *next;	/* If in pfdfree_head, next free page */
		uint8_t bucket_idx;	/* If allocated for the bucket allocator,
					   bucket index in the kmembuckets table */
	};
};

static inline void page_init(struct page *page, uintptr_t phys_addr)
{
	page->pfn = phys_addr >> PAGE_SHIFT;
	page->free = 1;
	page->in_bucket = 0;
	page->zone_id = ZONE_UNASSIGNED;
	page->next = NULL;
}

/*
 * Return virtual address of given page
 *
 * In the function name, we didn't add a 'virt' prefix to
 * the 'address' part cause dealing with virtual addresses
 * is the default action throughout the kernel's C part.
 */
static inline void *page_address(struct page *page)
{
	return VIRTUAL((uintptr_t)page->pfn << PAGE_SHIFT);
}

/*
 * Return physical address of given page
 *
 * The return type is intentionally set to int instead of a
 * pointer; we don't want to have invalid pointers dangling
 * around.
 *
 * Physical addresses are to be ONLY used at early boot
 * setup and while filling paging tables entries.
 */
static inline uintptr_t page_phys_addr(struct page *page)
{
	return (uintptr_t)page->pfn << PAGE_SHIFT;
}

static inline int page_is_free(struct page *page)
{
	return page->free;
}

/*
 * Page Allocator
 */

struct page *get_free_page(enum zone_id zid);
struct page *get_zeroed_page(enum zone_id zid);
void free_page(struct page *page);

struct page *addr_to_page(void *addr);

void pagealloc_init(void);

/*
 * Test cases driver
 */

#if	PAGEALLOC_TESTS

void pagealloc_run_tests(void);

#else

static void __unused pagealloc_run_tests(void) { }

#endif /* !PAGEALLOC_TESTS */

#endif /* _MM_H */
