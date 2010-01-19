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
 */

struct page;

struct page *get_free_page(void);
void free_page(struct page *page);

void pagealloc_init(void);

#undef PAGEALLOC_TESTS

#ifdef PAGEALLOC_TESTS

void pagealloc_run_tests(void);

#endif /* PAGEALLOC_TESTS */

#endif /* _MM_H */
