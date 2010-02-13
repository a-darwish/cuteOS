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

/*
 * Page Allocator
 */

struct page *get_free_page(void);
struct page *get_zeroed_page(void);
void free_page(struct page *page);

void *page_address(struct page *page);
uintptr_t page_phys_address(struct page *page);
struct page *addr_to_page(void *addr);
int page_is_free(struct page *);

void pagealloc_init(void);

/*
 * Kernel memory maps
 */
void memory_map_init(void);

/*
 * Test cases driver
 */

#undef PAGEALLOC_TESTS

#ifdef PAGEALLOC_TESTS

void pagealloc_run_tests(void);

#endif /* !PAGEALLOC_TESTS */

#endif /* _MM_H */
