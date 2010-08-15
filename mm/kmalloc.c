/*
 * Kernel Memory Allocator
 *
 * Copyright (C) 2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * This is an implementation of the McKusick-Karels kernel memory
 * allocator (with slight modifications) as described in:
 *
 *	'Design of a General Purpose Memory Allocator for the 4.3BSD
 *	UNIX Kernel', Kirk McKusick and J. Karels, Berkely, Usenix 1988
 *
 * and as also clearly described and compared to other allocators in the
 * greatest Unix book ever written:
 *
 *	'UNIX Internals: The New Frontiers', Uresh Vahalia, Prentice
 *	Hall, 1996
 *
 * One of the weakest points of classical power-of-2 allocators like the
 * one used in 4.2BSD was keeping memory blocks metadata inside the blocks
 * themselves. This led to worst-case behaviour of 50% memory wastage for
 * power-of-2-sized allocations.
 *
 * Due to compilers data structures alignments and the word sizes of cur-
 * rent processors, a huge number of kernel structures were in fact power
 * of 2 sized. This lead to unacceptable wastage of half kernel memory.
 *
 * McKusick solved this problem by keeping the metadata out of the blocks
 * themselves to another global data structure (kmemsize). We do something
 * similar by storing blocks metadata in the page frame descriptors (mm.h).
 * This happens at the price of mandating one single size of buffers for
 * each physical memory page frame requested from the page allocator.
 *
 * The big picture of the algorithm is that the kernel heap memory is org-
 * anized in power-of-2 free allocation units lists from 16 to 4096 bytes.
 * Those allocation units (buffers) are created on demand. If no empty buf
 * of a specific size exist (as in the initial state), a page is requested
 * from the page allocator and tokenized to buffers of the requested size.
 *
 * Terminology:
 * - 'Bucket': Free memory is organized in 'buckets', where each bucket
 * contains one head of the power-of-2 lists + its bookkeeping information.
 *
 * - 'Buffer': The smallest unit of allocation, with sizes ranging from 16
 * to 4096 bytes. Pages are tokenized to groups of equaly sized buffers.
 *
 * Finally:
 * I chose this algorithm for its elegance and simplicity. It sufferes
 * from some drawbacks like the inability of coalescing free memory blocks
 * as in the buddy system, leading to external fragmentation in case of a
 * bursty usage pattern of one size. There's also no simple way for retur-
 * ning pages to the page allocator. Nonetheless, the algorithm is still a
 * super-fast O(1), and it does handle small requests effeciently :)
 */

#include <kernel.h>
#include <spinlock.h>
#include <paging.h>
#include <string.h>
#include <mm.h>
#include <kmalloc.h>
#include <tests.h>

/*
 * A kernel memory bucket for each power-of-2 list
 *
 * A Bucket with index 'x' holds all the information for the
 * free bufs list of size (1 << x) bytes, including its head.
 */
static struct bucket {
	spinlock_t lock;		/* Bucket lock */
	void *head;			/* Free blocks list head */
	int totalpages;			/* # of pages requested from page allocator */
	int totalfree;			/* # of free buffers */
} kmembuckets[MAXBUCKET_IDX + 1];

static void *get_tokenized_page(int bucket_idx);

void *__kmalloc(int bucket_idx)
{
	struct bucket *bucket;
	char *buf;

	assert(bucket_idx <= MAXBUCKET_IDX);
	bucket = &kmembuckets[bucket_idx];

	spin_lock(&bucket->lock);

	if (bucket->head) {
		buf = bucket->head;
		bucket->head = *(void **)(bucket->head);
		goto out;
	}

	assert(bucket->totalfree == 0);
	buf = get_tokenized_page(bucket_idx);
	bucket->head = *(void **)buf;
	bucket->totalpages++;
	bucket->totalfree = PAGE_SIZE / (1 << bucket_idx);

out:
	bucket->totalfree--;
	spin_unlock(&bucket->lock);

	assert(is_free_buf(buf));
	sign_buf(buf, ALLOCBUF_SIG);
	return buf;
}

/*
 * Request a page from the page allocator and tokenize
 * it to buffers of the given bucket's buffer sizes.
 *
 * BIG-FAT-NOTE! Call with bucket lock held
 */
static void *get_tokenized_page(int bucket_idx)
{
	struct page *page;
	char *buf, *start, *end;
	int buf_len;

	page = get_free_page(ZONE_ANY);
	page->in_bucket = 1;
	page->bucket_idx = bucket_idx;

	start = page_address(page);
	end = start + PAGE_SIZE;

	buf = start;
	buf_len = 1 << bucket_idx;
	while (buf < (end - buf_len)) {
		*(void **)buf = buf + buf_len;
		sign_buf(buf, FREEBUF_SIG);
		buf += buf_len;
	}
	*(void **)buf = NULL;
	sign_buf(buf, FREEBUF_SIG);

	return start;
}

/*
 * Sanity checks to assure we're passed a valid address:
 * - Does given address reside in an allocated page?
 * - If yes, does it reside in a page allocated by us?
 * - If yes, does it reside in a buffer-aligned address?
 * - If yes, does it reside in a not-already-freed buffer?
 * Any NO above means an invalid address, thus a kernel bug
 */
void kfree(void *addr)
{
	struct page *page;
	struct bucket *bucket;
	int buf_size;
	char *buf;

	buf = addr;
	page = addr_to_page(buf);
	bucket = &kmembuckets[page->bucket_idx];

	if (page_is_free(page))
		panic("Bucket: Freeing address 0x%lx which resides in "
		      "an unallocated page frame", buf);

	if (!page->in_bucket)
		panic("Bucket: Freeing address 0x%lx which resides in "
		      "a foreign page frame (not allocated by us)", buf);

	buf_size = 1 << page->bucket_idx;
	if (!is_aligned((uintptr_t)buf, buf_size))
		panic("Bucket: Freeing invalidly-aligned 0x%lx address; "
		      "bucket buffer size = 0x%lx\n", buf, buf_size);

	if (is_free_buf(buf))
		panic("Bucket: Freeing already free buffer at 0x%lx, "
		      "with size = 0x%lx bytes", buf, buf_size);

	sign_buf(buf, FREEBUF_SIG);

	spin_lock(&bucket->lock);

	*(void **)buf = bucket->head;
	bucket->head = buf;
	bucket->totalfree++;

	spin_unlock(&bucket->lock);
}

void kmalloc_init(void)
{
	for (int i = 0; i <= MAXBUCKET_IDX; i++)
		kmembuckets[i].lock = SPIN_UNLOCKED();
}

/*
 * Kernel memory allocator test cases.
 *
 * Thanks to NewOS's Travis Geiselbrecht (giest) for aiding
 * me in outlying different MM testing scenarious over IRC!
 *
 * Those tests can get more harnessed once we have a random
 * number generator ready.
 */

#if	KMALLOC_TESTS

#include <string.h>
#include <paging.h>

/*
 * Max # of allocations to do during testing
 */
#define ALLOCS_COUNT	100000

/*
 * Big array of pointers to allocated addresses and
 * their area sizes.
 */
static struct {
	int size;
	void *p;
} p[ALLOCS_COUNT];

static char tmpbuf[PAGE_SIZE];

/*
 * As a way of memory allocator disruption, allocate,
 * memset, then free a given @(size)ed memory buffer.
 */
static void _disrupt(int size)
{
	char *p;

	p = kmalloc(size);
	memset(p, 0xff, size);
	kfree(p);
}

/*
 * @count: number of allocations to perform
 * @rounded: if true, only do power-of-2-rounded allocs
 */
void _test_allocs(int count, int rounded)
{
	int i, size;

	size = (rounded) ? MINALLOC_SZ : 1;

	for (i = 0; i < count; i++) {
		_disrupt(size);

		p[i].p = kmalloc(size);
		assert(is_aligned((uintptr_t)p[i].p, 16));
		p[i].size = size;

		if (rounded) {
			memset32(p[i].p, i, size);
			size *= 2;
			size = (size > MAXALLOC_SZ) ? MINALLOC_SZ : size;
		} else {
			memset(p[i].p, i, size);
			size++;
			size = (size > MAXALLOC_SZ) ? 1 : size;
		}
	}

	for (i = 0; i < count; i++) {
		size = p[i].size;
		_disrupt(size);

		(rounded) ? memset32(tmpbuf, i, size) : memset(tmpbuf, i, size);
		if (__builtin_memcmp(p[i].p, tmpbuf, size))
			panic("_Bucket: FAIL: [%d] buffer at 0x%lx, with size "
			      "%d bytes got corrupted", i, p[i].p, size);

		kfree(p[i].p);

		size = ((size / 2) > 1) ? size / 2 : MINALLOC_SZ;
		p[i].p = kmalloc(size);
		assert(is_aligned((uintptr_t)p[i].p, 16));
		p[i].size = size;
		(rounded) ? memset32(p[i].p, i, size) : memset(p[i].p, i, size);

		_disrupt(size);
	}

	for (i = 0; i < count; i++) {
		_disrupt(45);

		size = p[i].size;
		(rounded) ? memset32(tmpbuf, i, size) : memset(tmpbuf, i, size);
		if (__builtin_memcmp(p[i].p, tmpbuf, size))
			panic("_Bucket: FAIL: [%d] buffer at 0x%lx, with size "
			      "%d bytes got corrupted", i, p[i].p, size);

		kfree(p[i].p);
		_disrupt(32);
	}

	printk("_Bucket: %s: Success\n", __FUNCTION__);
}

/*
 * Memory allocator tests driver
 *
 * For extra fun, after removing the thread-unsafe
 * globals, call this 3 or 4 times in parallel.
 */
void kmalloc_run_tests(void)
{
	uint64_t i, count, repeat;

	count = ALLOCS_COUNT;
	repeat = 100;

	for (i = 0; i < repeat; i++) {
		printk("[%d] ", i);
		_test_allocs(count, 1);
	}

	memset(p, 0, sizeof(p));
	for (i = 0; i < repeat; i++) {
		printk("[%d] ", i);
		_test_allocs(count, 0);
	}

	for (i = MINBUCKET_IDX; i <= MAXBUCKET_IDX; i++)
		printk("Buf size = %d: free bufs = %d, total pages requested "
		       "= %d\n", 1 << i, kmembuckets[i].totalfree,
		       kmembuckets[i].totalpages);
}

#endif /* KMALLOC_TESTS */
