/*
 * Operations on a Bitmap
 *
 * Copyright (C) 2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * We assume little-endian ordering of bytes in the bitmap, that is:
 * item #0  is represented by first  byte bit #0
 * item #7  is represented by first  byte bit #7
 * item #8  is represented by second byte bit #0
 * item #15 is represented by second byte bit #7
 * .. and so on.
 *
 * FIXME: Optimize this using x86-64 ops.
 */

#include <kernel.h>
#include <bitmap.h>
#include <tests.h>

static bool bit_is_set(uint8_t val, int bit)
{
	if ((val & (1U << bit)) == 0)
		return false;

	return true;
}

/*
 * Find the first set (or clear) bit in the given @buf of
 * @len bytes. @TEST_FUNC is used for testing bit's state.
 */
#define add_search_function(NAME, TEST_FUNC)		\
int64_t NAME(char *buf, uint len)			\
{							\
	uint bits_per_byte = 8;				\
							\
	for (uint i = 0; i < len; i++)			\
		for (uint j = 0; j < bits_per_byte; j++)\
			if (TEST_FUNC(buf[i], j))	\
				return (i * 8) + j;	\
	return -1;					\
}

add_search_function(bitmap_first_set_bit, bit_is_set)
add_search_function(bitmap_first_zero_bit, !bit_is_set)

/*
 * Given a buffer @bit offset, calculate the bit position
 * in terms of a @byte_offset within the buffer, and a
 * @bit_offset within that byte.  @len is the buf length.
 */
static void set_offsets(uint bit, uint *byte_offset, uint *bit_offset, uint len)
{
	/* 8 bits per byte */
	assert(bit < len  * 8);

	*byte_offset = bit / 8;
	*bit_offset  = bit % 8;
}

/*
 * Set given @bit number in buffer.
 */
void bitmap_set_bit(char *buf, uint bit, uint len)
{
	uint byte_offset, bit_offset;

	set_offsets(bit, &byte_offset, &bit_offset, len);
	buf += byte_offset;
	*buf |= (1 << bit_offset);
}

/*
 * Clear given @bit number in buffer.
 */
void bitmap_clear_bit(char *buf, uint bit, uint len)
{
	uint byte_offset, bit_offset;

	set_offsets(bit, &byte_offset, &bit_offset, len);
	buf += byte_offset;
	*buf &= ~(1 << bit_offset);
}

/*
 * Check if given @bit number in buffer is set.
 */
bool bitmap_bit_is_set(char *buf, uint bit, uint len)
{
	uint byte_offset, bit_offset;

	set_offsets(bit, &byte_offset, &bit_offset, len);
	buf += byte_offset;
	return bit_is_set(*buf, bit_offset);
}

/*
 * Check if given @bit number in buffer is clear.
 */
bool bitmap_bit_is_clear(char *buf, uint bit, uint len)
{
	return !bitmap_bit_is_set(buf, bit, len);
}

/*
 * Below testcases will be very useful only when the naive code
 * above get substituted with optimized x86-64 ops!
 */

#if BITMAP_TESTS

#include <string.h>
#include <kmalloc.h>

void bitmap_run_tests(void)
{
	char *buf;
	uint buflen_bytes, buflen_bits;
	int64_t bit;

	buflen_bytes = 4096;
	buflen_bits = buflen_bytes * 8;
	buf = kmalloc(buflen_bytes);

	/* An all-zeroes buffer */
	memset(buf, 0, buflen_bytes);
	bit = bitmap_first_set_bit(buf, buflen_bytes);
	if (bit != -1)
		panic("Zeroed buf at 0x%lx, but first_set_bit returned "
		      "bit #%u as set!", buf, bit);
	bit = bitmap_first_zero_bit(buf, buflen_bytes);
	if (bit != 0)
		panic("Zeroed buf at 0x%lx, but first_zero_bit returned "
		      "bit #%u instead of bit #0!", buf, bit);
	for (uint i = 0; i < buflen_bits; i++) {
		if (bitmap_bit_is_set(buf, i, buflen_bytes))
			panic("Zeroed buf at 0x%lx, but bit_is_set?() returned "
			      "bit #%u as set!", buf, i);
		assert(bitmap_bit_is_clear(buf, i, buflen_bytes));
	}

	/* Mixed buffer */
	for (uint i = 0; i < buflen_bits; i++) {
		memset(buf, 0, buflen_bytes);
		bitmap_set_bit(buf, i, buflen_bytes);
		bit = bitmap_first_set_bit(buf, buflen_bytes);
		if (bit != i)
			panic("Zeroed buf at 0x%lx, then set its #%u bit, "
			      "but first_set_bit returned #%u as set instead!",
			      buf, i, bit);
		if (bitmap_bit_is_clear(buf, i, buflen_bytes))
			panic("Zeroed buf at 0x%lx, then set its #%u bit, "
			      "but bit_is_clear?() returned the bit as clear!",
			      buf, i);
		assert(bitmap_bit_is_set(buf, i, buflen_bytes));
	}

	/* An all-ones buffer */
	memset(buf, 0xff, buflen_bytes);
	for (uint i = 0; i < buflen_bytes; i++) {
		bit = bitmap_first_set_bit(buf + i, buflen_bytes - i);
		if (bit != 0)
			panic("buf at 0x%lx+%d have all bits set, first_set_bit "
			      "should have returned bit #0, but #%u was "
			      "returned instead!", buf, i, bit);
		bit = bitmap_first_zero_bit(buf + i, buflen_bytes - i);
		if (bit != -1)
			panic("buf at 0x%lx+%d have all bits set, "
			      "first_zero_bit should've returned no bit as "
			      "clear, but bit #%u was returned!", buf, i, bit);
		if (bitmap_bit_is_clear(buf + i, 0, buflen_bytes - i))
			panic("buf at 0x%lx+%d have all bits set, bit_is_clear? "
			      "should have returned bit #0 as not clear", buf, i);
		assert(bitmap_bit_is_set(buf + i, 0, buflen_bytes - i));
	}

	printk("%s: Sucess!\n", __func__);
	kfree(buf);
}

#endif	/* BITMAP_TESTS */
