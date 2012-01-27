/*
 * Memory Management: the page allocator
 *
 * Copyright (C) 2009-2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * The page allocator allocates and reclaims physical memory pages to the
 * rest of the kernel upon request.
 *
 * The algorithm implemented here is the one used in Unix SVR2 and descr-
 * ibed in Maurice Bach's work. Each page of physical memory is represnted
 * by a page frame descriptor, and collected in the 'page frame descriptor
 * table' pfdtable. Space is allocated for this table - directly after
 * kernel's memory area - once for the lifetime of the system.
 *
 * To allocate and reclaim pages, the allocator links free entries of the
 * pfdtable into a freelist. Allocation and reclamation is merely an act
 * of list pointers manipulations, similar to SVR2 buffer cache lists.
 *
 * Two major differences from the SVR2 algorithm exist. The first is
 * 'reverse mapping' an address to its page descriptor. Assuming no gaps
 * in available physical memory exist (as in the old Unix days), one could
 * 've been able to find the desired page descriptor by
 *
 *		pfdtable index = phys addr >> PAGE_SHIFT
 *
 * where PAGE_SHIFT = log2(PAGE_SIZE). But in the PC architecture, thanks
 * to the PCI hole and similar areas, reserved memory regions can reach a
 * full GB __in between__ available physical memory space, making the abo-
 * ve approach impractical.
 *
 * That approach could only be used now if descriptors for reserved memory
 * areas were also included in the pfdtable - a huge memory loss. Here is
 * an example of a sparse e820 physical memory map from an 8-GByte-RAM PC:
 *
 *   0x0000000000000000 - 0x000000000009fc00 (low-memory)
 *   0x000000000009fc00 - 0x00000000000a0000 (reserved)
 *   0x00000000000e6000 - 0x0000000000100000 (reserved)
 *   0x0000000000100000 - 0x00000000cff90000 (available)
 *   0x00000000cff90000 - 0x00000000cffa8000 (ACPI reclaim)
 *   0x00000000cffa8000 - 0x00000000cffd0000 (ACPI NVS)
 *   0x00000000cffd0000 - 0x00000000d0000000 (reserved)
 *   0x00000000fff00000 - 0x0000000100000000 (reserved)
 *   0x0000000100000000 - 0x0000000230000000 (available)
 *
 * Thus, a new reverse mapping mechanism suitable for PC/ACPI was created.
 *
 * The second difference from SVR2 is the support of 'zones': asking the
 * allocator for a page only from a certain physical region, which is a
 * feature needed by early boot setup. Instead of having a single freeli-
 * st, we now have a unique one for each physical memory zone.
 *
 * Finally, the API (not code) for allocating and reclaiming pages comes
 * from linux-2.6; these function names were there since linux-0.1!
 */

#include <kernel.h>
#include <stdint.h>
#include <string.h>
#include <paging.h>
#include <sections.h>
#include <spinlock.h>
#include <ramdisk.h>
#include <e820.h>
#include <mm.h>
#include <tests.h>

/*
 * Page Frame Descriptor Table
 *
 * Page descriptors covering kernel memory areas are not
 * included here. Space for this table is manually alloc-
 * ated directly after kernel's BSS section.
 *
 * @pfdtable_top: current table end mark
 * @pfdtable_end: don't exceed this dynamically set mark
 */
static struct page *pfdtable;
static struct page *pfdtable_top;
static struct page *pfdtable_end;

/*
 * Page allocator Zone Descriptor
 */
struct zone {
	/* Statically initialized */
	enum zone_id id;		/* Self reference (for iterators) */
	uint64_t start;			/* Physical range start address */
	uint64_t end;			/* Physical range end address */
	const char *description;	/* For kernel log messages */

	/* Dynamically initialized */
	struct page *freelist;		/* Connect zone's unallocated pages */
	spinlock_t freelist_lock;	/* Above list protection */
	uint64_t freepages_count;	/* Stats: # of free pages now */
	uint64_t boot_freepages;	/* Stats: # of free pages at boot */
};

/*
 * Zone Descriptor Table
 *
 * If a physical page got covered by more than one zone
 * cause of overlapping zone boundaries, such page will get
 * assigned to the highest priority zone covering its area.
 *
 * After MM init, each page will get assigned to _one_, and
 * _only_ one, zone. Each page descriptor will then have an
 * assigned zone ID for its entire lifetime.
 */
static struct zone zones[] = {
	[ZONE_1GB] = {
		.id = ZONE_1GB,
		.start = 0x100000,	/* 1-MByte */
		.end = 0x40000000,	/* 1-GByte */
		.description = "Early-boot Zone",
	},

	[ZONE_ANY] = {
		.id = ZONE_ANY,
		.start = 0x0,
		.end = KERN_PHYS_END_MAX,
		.description = "Any Zone",
	},
};

/*
 * Do not reference the zones[] table directly
 *
 * Use below iterators in desired priority order, or check
 * get_zone(), which does the necessery ID sanity checks.
 */

#define descending_prio_for_each(zone)					\
	for (zone = &zones[0]; zone <= &zones[ZONE_ANY]; zone++)

#define ascending_prio_for_each(zone)					\
	for (zone = &zones[ZONE_ANY]; zone >= &zones[0]; zone--)

static inline struct zone *get_zone(enum zone_id zid)
{
	if (zid != ZONE_1GB && zid != ZONE_ANY)
		panic("Memory - invalid zone id = %d", zid);

	return &zones[zid];
}

static inline void zones_init(void)
{
	struct zone *zone;

	descending_prio_for_each(zone) {
		zone->freelist = NULL;
		spin_init(&zone->freelist_lock);
		zone->freepages_count = 0;
		zone->boot_freepages = 0;
	}
}

/*
 * Reverse Mapping Descriptor
 *
 * This structure aids in reverse-mapping a virtual address
 * to its respective page descriptor.
 *
 * We store page descriptors representing a specific e820-
 * available range sequentially, thus a reference to a range
 * and the pfdtable cell representing its first page is
 * enough for reverse-mapping any address in such a range.
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
 * 1MB and which also contains the pfdtable and the pfdrmap.
 *
 * By this action, we've permanently allocated space for
 * these tables for the entire system's lifetime.
 */
static uint64_t kmem_end = -1;

/*
 * Assign a zone to given page
 *
 * FIXME: we need to mark early boot functions like this,
 * and statically assure not being called afterwards.
 */
static struct zone *page_assign_zone(struct page* page)
{
	uint64_t start, end;
	struct zone *zone;

	assert(page->free == 1);
	assert(page->zone_id == ZONE_UNASSIGNED);

	start = page_phys_addr(page);
	end = start + PAGE_SIZE;

	descending_prio_for_each(zone) {
		assert(page_aligned(zone->start));
		assert(page_aligned(zone->end));

		if(start >= zone->start && end <= zone->end) {
			page->zone_id = zone->id;
			return zone;
		}
	}

	panic("Memory - Physical page 0x%lx cannot be attached "
	      "to any zone", start);
}

/*
 * Create (and initialize) new pfdtable entries for given
 * memory range, which should be e820 available and above
 * our kernel memory area.
 *
 * NOTE! No need to acquire zones freelist locks as this
 * should _only_ be called from the serial init path.
 *
 * Prerequisite: kernel memory area end pre-calculated
 */
static void pfdtable_add_range(struct e820_range *range)
{
	uint64_t start, end, nr_pages;
	struct zone *zone;
	struct page *page;

	assert(range->type == E820_AVAIL);

	start = range->base;
	end = range->base + range->len;
	assert(page_aligned(start));
	assert(page_aligned(end));
	assert(page_aligned(kmem_end));
	assert(start >= PHYS(kmem_end));
	assert(start < end);

	/* New entries shouldn't overflow the table */
	page = pfdtable_top;
	nr_pages = (end - start) / PAGE_SIZE;
	assert((page + nr_pages) <= pfdtable_end);

	rmap_add_range(range, page);

	while (start != end) {
		page_init(page, start);
		zone = page_assign_zone(page);

		page->next = zone->freelist;
		zone->freelist = page;
		zone->freepages_count++;

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
 *
 * NOTE! do not call these in IRQ-context!
 */

static struct page *__get_free_page(enum zone_id zid)
{
	struct zone *zone;
	struct page *page;

	zone = get_zone(zid);

	spin_lock(&zone->freelist_lock);

	if (!zone->freelist) {
		page = NULL;
		goto out;
	}

	page = zone->freelist;
	zone->freelist = zone->freelist->next;
	zone->freepages_count--;

	assert(page->free == 1);
	page->free = 0;

out:
	spin_unlock(&zone->freelist_lock);
	return page;
}

struct page *get_free_page(enum zone_id zid)
{
	struct zone *zone;
	struct page *page;
	uint64_t start, end;

	if (zid == ZONE_ANY)
		ascending_prio_for_each(zone) {
			page = __get_free_page(zone->id);
			if (page != NULL)
				break;
		}
	else
		page = __get_free_page(zid);

	if (page == NULL)
		panic("Memory - No more free pages available at "
		      "`%s'", get_zone(zid)->description);

	start = page_phys_addr(page);
	end = start + PAGE_SIZE;
	zone = get_zone(zid);

	assert(start >= zone->start);
	assert(end <= zone->end);
	return page;
}

struct page *get_zeroed_page(enum zone_id zid)
{
	struct page *page;

	page = get_free_page(zid);
	memset64(page_address(page), 0, PAGE_SIZE);

	return page;
}

void free_page(struct page *page)
{
	struct zone *zone;

	zone = get_zone(page->zone_id);

	spin_lock(&zone->freelist_lock);

	page->next = zone->freelist;
	zone->freelist = page;
	zone->freepages_count++;

	if (page->free != 0)
		panic("Memory - Freeing already free page at 0x%lx\n",
		      page_address(page));
	page->free = 1;

	spin_unlock(&zone->freelist_lock);
}

/*
 * Reverse mapping, in O(n)
 *
 * 'N' is the # of 'available' e820 ranges.
 */

/*
 * Return the page descriptor representing @addr.
 */
struct page *addr_to_page(void *addr)
{
	struct rmap *rmap;
	struct page *page;
	struct e820_range *range;
	uint64_t paddr, start, end, offset;

	paddr = PHYS(addr);
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
}

/*
 * Initialize the page allocator structures
 */
void pagealloc_init(void)
{
	struct e820_range *range;
	struct e820_setup *setup;
	struct zone *zone;
	uint64_t avail_pages, avail_ranges;

	zones_init();

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

	pfdtable = ramdisk_memory_area_end();
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

	/*
	 * Statistics
	 */

	ascending_prio_for_each(zone) {
		zone->boot_freepages = zone->freepages_count;
	}
}

/*
 * Page allocator test cases
 *
 * Thanks to NewOS's Travis Geiselbrecht (giest) for aiding
 * me in outlying different MM testing scenarios over IRC!
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
 * Sanity checks for the zones table
 */
static inline void _validate_zones_data(void)
{
	int zid;

	for (zid = 0; zid < ARRAY_SIZE(zones); zid++) {
		assert((int)zones[zid].id == zid);
		assert(zones[zid].description != NULL);
		assert(zones[zid].start < zones[zid].end);
		assert(zones[zid].boot_freepages >= zones[zid].freepages_count);
	}

	/* ZONE_ANY must be the least priority */
	assert(ZONE_ANY == ARRAY_SIZE(zones) - 1);

	printk("_Memory: %s: Success\n", __FUNCTION__);
}

enum _count_type {
	_BOOT,			/* # free pages available at boot */
	_CURRENT,		/* # free pages available now */
};

static uint64_t _get_all_freepages_count(enum _count_type type)
{
	uint64_t count;
	struct zone *zone;

	count = 0;
	ascending_prio_for_each(zone) {
		if (type == _BOOT)
			count += zone->boot_freepages;
		else
			count += zone->freepages_count;
	}

	return count;
}

/*
 * Assure all e820-available memory spaces are covered in
 * pfdtable cells and are marked as free.
 */
static int _test_boot_freepages_count(void)
{
	struct e820_range *range;
	uint64_t count, reported_count, pfdtable_count;

	count = 0;
	e820_for_each(range) {
		if (range->type != E820_AVAIL)
			continue;
		if (e820_sanitize_range(range, kmem_end))
			continue;

		count += range->len / PAGE_SIZE;
	}

	reported_count = _get_all_freepages_count(_BOOT);
	if (count != reported_count)
		panic("_Memory: Available e820 pages = %d, pfdtable boot "
		      "avial-pages counter = %d\n", count, reported_count);

	pfdtable_count = pfdtable_top - pfdtable;
	if (count != pfdtable_count)
		panic("_Memory: Available e820 pages = %d, pfdtable # of "
		      "elements = %d\n", count, pfdtable_count);

	printk("_Memory: %s: Success\n", __FUNCTION__);
	return true;
}

/*
 * Most BIOSes represent the above-1MB available memory pages
 * as one contiguous e820 range, leading to a not very good
 * test coverage of the important add_avail_range() method.
 *
 * Here, we extremely stress that method by emulating the ad-
 * dition of a huge number of available e820 ranges. We do so
 * by transforming _each_ available page to an e820 range in
 * itself; e.g. transforming a 1MB e820 range to 0x100 (256)
 * ranges, each range representing a single 0x1000 (4K) page.
 *
 * WARNING: Because addr_to_page() is O(n), where n is the #
 * of e820 ranges in the system, this 'test' - which messes
 * heavily with the guts of the allocator - makes the perfo-
 * rmance of that important function (addr_..) extremely bad.
 *
 * Prerequisite-1: rmode E820h-struct previously validated
 *
 * Prerequisite-2: Do NOT call this after the allocator got
 * used: it _erases_ and re-creates all of its sturcutres!!
 */
static int __unused _torture_pfdtable_add_range(void) {
	uint64_t old_end, old_count, count, repeat, ranges;
	struct e820_range *range, subrange;
	struct rmap *rmap;
	struct zone *zone;

	ascending_prio_for_each(zone) {
		assert(zone->freepages_count == zone->boot_freepages);
	}

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
	old_count = _get_all_freepages_count(_BOOT);
	old_end = kmem_end;
	kmem_end = round_up((uintptr_t)pfdrmap_end, PAGE_SIZE);

	/* Clear pfdtable structures */
	pfdtable_top = pfdtable;
	ascending_prio_for_each(zone) {
		zone->freelist = NULL;
		zone->freepages_count = 0;
		zone->boot_freepages = 0;
	}

	/* Refill the table, page by page! */
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

	descending_prio_for_each(zone) {
		zone->boot_freepages = zone->freepages_count;
	}

	/* More kernel memory area led to less available pages.
	 * Compare old and new count with this fact in mind */
	old_count -= ((kmem_end - old_end) / PAGE_SIZE);
	count = _get_all_freepages_count(_BOOT);
	if (old_count != count)
		panic("_Memory: Refilling pfdtable found %d free pages; %d "
		      "pages originally reported", count, old_count);

	printk("_Memory: %s: Success!\n", __FUNCTION__);
	return true;
}

/*
 * Check if given page frame is allocation-valid.
 * Prerequisite: rmode E820h-struct previously validated
 */
static int _page_is_free(struct page *page)
{
	uintptr_t start, end, paddr;
	struct e820_range *range;

	paddr = page_phys_addr(page);
	if (paddr < PHYS(kmem_end))
		return false;

	e820_for_each(range) {
		if (range->type != E820_AVAIL)
			continue;
		start = range->base;
		end = start + range->len;
		if (paddr >= start && (paddr+PAGE_SIZE) <= end)
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

	p1 = get_zeroed_page(ZONE_1GB);

	/* Test reverse mapping */
	addr = page_address(p1);
	p2 = addr_to_page(addr);
	if (p1 != p2)
		panic("_Memory: FAIL: Reverse mapping addr=0x%lx lead to "
		      "page descriptor 0x%lx; it's actually for page %lx\n",
		      addr, p2, p1);

	memset64(addr, UINT64_MAX, PAGE_SIZE);
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
	uint64_t old_count, count;
	void *addr;
	struct page *page;

	old_count = _get_all_freepages_count(_CURRENT);

	for (i = 0; i < 100; i++)
		_disrupt();

	for (i = 0; i < nr_pages; i++) {
		_disrupt();
		pages[i] = get_free_page(ZONE_ANY);
		if (!_page_is_free(pages[i]))
			panic("_Memory: Allocated invalid page address "
			      "0x%lx\n", page_phys_addr(pages[i]));

		/* Test reverse mapping */
		addr = page_address(pages[i]);
		page = addr_to_page(addr);
		if (page != pages[i])
			panic("_Memory: FAIL: Reverse mapping addr=0x%lx lead "
			      "to page descriptor 0x%lx, while it's actually "
			      "on %lx\n", addr, page, pages[i]);

		memset32(addr, i, PAGE_SIZE);
	}

	for (i = 0; i < nr_pages; i++) {
		memset32(tmpbuf, i, PAGE_SIZE);
		addr = page_address(pages[i]);
		res = memcmp(addr, tmpbuf, PAGE_SIZE);
		if (res)
			panic("_Memory: FAIL: [%d] page at 0x%lx got "
			      "corrupted\n", i, PHYS(addr));

		/* Test reverse mapping */
		page = addr_to_page(addr);
		if (page != pages[i])
			panic("_Memory: FAIL: Reverse mapping addr=0x%lx lead "
			      "to page descriptor 0x%lx, while it's actually "
			      "on %lx\n", addr, page, pages[i]);

		free_page(pages[i]);
		_disrupt();
	}

	for (i = 0; i < 100; i++)
		_disrupt();

	/* We've freed everything we allocated, number of free
	 * pages should not change */
	count = _get_all_freepages_count(_CURRENT);
	if (old_count != count)
		panic("_Memory: free pages leak; original number = %ld, "
		      "current number = %ld\n", old_count, count);

	printk("_Memory: %s: Success\n", __FUNCTION__);
	return 0;
}

/*
 * Consume all given zone's pages then observe the result
 * of overallocation. Use ZONE_ANY to consume all memory.
 */
static void __unused _test_zone_exhaustion(enum zone_id zid)
{
	struct page *page;
	struct zone *zone;
	uint64_t count;
	void* addr;

	zone = get_zone(zid);
	if (zid == ZONE_ANY)
		count = _get_all_freepages_count(_CURRENT);
	else
		count = zone->freepages_count;

	while (count--) {
		page = get_free_page(zid);
		assert(page_phys_addr(page) >= zone->start);
		assert((page_phys_addr(page) + PAGE_SIZE) <= zone->end);

		addr = page_address(page);
		memset64(addr, UINT64_MAX, PAGE_SIZE);

		printk("_Memory: Zone `%s': page %lx, %d free pages\n",
		       zone->description, addr, count);
	}

	printk("_Memory: Allocated all zone's pages. System "
	       "should panic from next allocation\n");

	(void) get_free_page(zid);
}

/*
 * Page allocation tests driver
 */
void pagealloc_run_tests(void)
{
	uint64_t count;

	_validate_zones_data();
	_test_boot_freepages_count();

	/* Beware of the pre-requisites first */
	/* _torture_pfdtable_add_range(); */

	count = _get_all_freepages_count(_CURRENT);
	if (count > PAGES_COUNT)
		count = PAGES_COUNT;

	printk("_Memory: Allocating (and seeding) %ld pages on "
	       "each run\n", count);

	/* Burn, baby, burn */
	for (int i = 0; i < 100; i++) {
		printk("[%d] ", i);
		_test_pagealloc_coherency(count);
	}

	/* Pick one: */
	/* _test_zone_exhaustion(ZONE_1GB); */
	/* _test_zone_exhaustion(ZONE_ANY); */
}

#endif /* PAGEALLOC_TESTS */
