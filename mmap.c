/*
 * System memory map
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <stdint.h>
#include <segment.h>
#include <e820.h>

/*
 * Checksum buffer marked by start.
 */
static inline uint32_t e820_checksum(void *start, void *end)
{
	uint8_t *p = start;
	uint32_t sum = 0;
	uint64_t len;

	assert(start < end);
	len = end - start;

	while (len--)
		sum += *p++;

	return sum;
}

/*
 * Build system memory map by parsing the custom 'E820h struct'
 * returned by real-mode code. This struct contains ACPI memory
 * map entries returned by the BIOS; check e820h.h for details.
 */
void mmap_init(void)
{
	uint32_t *entry, e820_len, err, checksum;
	uint64_t start, end;
	struct e820_range *range;

	/* Check buffer start signature */
	entry = VIRTUAL(E820_BASE);
	if (*entry != E820_INIT_SIG)
		panic("E820 error - Invalid buffer start");
	++entry;

	/* Parse given e820 ranges */
	while (*entry != E820_END) {
		e820_len = *entry++;
		range = (struct e820_range *)entry;

		start = range->base;
		end = start + range->len;
		if (end < start) {
			printk("Warning: Overflowed e820 range\n");
			printk(".. start = 0x%lx, end = 0x%lx\n", start, end);
		} else {
			printk("E820 range: %lx - %lx (%s)\n", start, end,
			       e820_typestr(range->type));
		}

		entry = (uint32_t *)(e820_len + (uintptr_t)entry);
	}
	++entry;

	/* Parse error field */
	err = *entry++;
	if (err)
		panic("E820 error - %s", e820_errstr(err));

	/* Lastly, checksum buffer */
	checksum = e820_checksum(VIRTUAL(E820_BASE), entry);
	if (checksum != *entry++)
		panic("E820 error - invalid buffer checksum");
	assert(entry <= (uint32_t *)VIRTUAL(E820_MAX));
}
