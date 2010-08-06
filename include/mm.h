#ifndef _MM_H
#define _MM_H

/*
 * Memory management: The page allocator
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * Please check page_alloc.c for context ..
 */

#include <stdint.h>
#include <tests.h>

/*
 * Page frame descriptor; one for each e820-available
 * physical page in the system. Check the @pfdtable.
 * Note! The 'bucket allocator' is our kmalloc algorithm.
 */
struct page {
	uint64_t pfn;			/* Phys addr = pfn << PAGE_SHIFT */

	/* Page flags */
	unsigned free:1,		/* Not allocated? */
		in_bucket:1;		/* Used by the bucket-allocator? */

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

struct page *get_free_page(void);
struct page *get_zeroed_page(void);
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
