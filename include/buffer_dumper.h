#ifndef _BUFDUMP_H
#define _BUFDUMP_H

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

/*
 * @pr: printf()-like method, determining the output device (to VGA?, serial?)
 * @formatter: how to dump buffers to output device (using hex?, ascii?, null?)
 */
struct buffer_dumper {
	void (*pr)(const char *fmt, ...);
	void (*formatter)(struct buffer_dumper *dumper, void *buf, uint len);
};

/*
 * Possible functions for the printer:
 * - printk(fmt, ...)
 * - prints(fmt, ...)
 * - and below null printer
 */
void null_printer(const char *fmt, ...);

/*
 * Possible functions for the formatter:
 */
void buf_hex_dump(struct buffer_dumper *dumper, void *given_buf, uint len);
void buf_char_dump(struct buffer_dumper *dumper, void *given_buf, uint len);
void buf_null_dump( __unused struct buffer_dumper *dumper,
		    __unused void *given_buf, __unused uint len);

/**
 * Public interface:
 */

__unused static struct buffer_dumper vga_hex_dumper = {
	.pr = printk,
	.formatter = buf_hex_dump,
};

__unused static struct buffer_dumper vga_char_dumper = {
	.pr = printk,
	.formatter = buf_char_dump,
};

__unused static struct buffer_dumper vga_null_dumper = {
	.pr = prints,
	.formatter = buf_null_dump,
};

__unused static struct buffer_dumper serial_hex_dumper = {
	.pr = prints,
	.formatter = buf_hex_dump,
};

__unused static struct buffer_dumper serial_char_dumper = {
	.pr = prints,
	.formatter = buf_char_dump,
};

__unused static struct buffer_dumper serial_null_dumper = {
	.pr = prints,
	.formatter = buf_null_dump,
};

__unused static struct buffer_dumper null_null_dumper = {
	.pr = null_printer,
	.formatter = buf_null_dump,
};

void printbuf(struct buffer_dumper*, void *buf, uint len);

#endif /* _BUFDUMP_H */
