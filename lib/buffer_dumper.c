/*
 * BufferDumper Class - Log messages (and bufs) to custom output devices
 *
 * Copyright (C) 2013 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <kmalloc.h>
#include <buffer_dumper.h>

/*
 * Possible functions for the printer:
 * - printk(fmt, ...)
 * - prints(fmt, ...)
 * - Below null printer
 */

void null_printer(__unused const char *fmt, ...)
{
}

/*
 * Possible functions for the buffer-dump formatter:
 */

/*
 * Print @given_buf, with length of @len bytes, in the format:
 *
 *	$ od --format=x1 --address-radix=none --output-duplicates
 *
 */
void buf_hex_dump(struct buffer_dumper *dumper, void *given_buf, uint len)
{
	unsigned int bytes_perline = 16, n = 0;
	uint8_t *buf = given_buf;

	for (uint i = 0; i < len; i++) {
		dumper->pr(" ");
		if (buf[i] < 0x10)
			dumper->pr("0");
		dumper->pr("%x", buf[i]);

		n++;
		if (n == bytes_perline || i == len - 1) {
			dumper->pr("\n");
			n = 0;
		}
	}
}

/*
 * Print @given_buf as ASCII text.
 */
void buf_char_dump(struct buffer_dumper *dumper, void *given_buf, uint len)
{
	char *buf = given_buf;

	for (uint i = 0; i < len; i++)
		dumper->pr("%c", buf[i]);
}

/*
 * NULL buffer printer. Useful for ignoring big debugging dumps, etc.
 */
void buf_null_dump(__unused struct buffer_dumper *dumper,
		   __unused void __unused *given_buf, __unused uint len)
{
}

/**
 * Public interface:
 */

void printbuf(struct buffer_dumper* dumper, void *buf, uint len)
{
	assert(dumper != NULL);
	dumper->formatter(dumper, buf, len);
}
