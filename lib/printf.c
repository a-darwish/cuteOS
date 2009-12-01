/*
 * printf()-like methods: vsnprintf(), etc
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <stdarg.h>			/* provided by GCC */
#include <string.h>
#include <segment.h>
#include <spinlock.h>

/*
 * The casts from (char *) to (unsigned short *) looks
 * ugly; this should do the trick. Maybe we should
 * implement this as an x86 'movw' someday.
 */
static void writew(unsigned short val, void *addr)
{
	unsigned short *p = addr;
	*p = val;
}

/*
 * Convert given unsigned long integer (@num) to ascii using
 * desired radix. Return the number of ascii chars printed.
 * @size: output buffer size
 */
static int ultoa(unsigned long num, char *buf, int size, int radix)
{
	int ret, digits = 0;
	char digit[16] = "0123456789abcdef";

	if (radix < 2 || radix > sizeof(digit))
		return 0;

	if (!num)
		digits++;
	for (typeof(num) c = num; c != 0; c /= radix)
                digits++;

	ret = min(digits, size);

	while (digits && size) {
		buf[digits - 1] = digit[num % radix];
		num = num / radix;
		--digits;
		--size;
	}

	return ret;
}

/*
 * Convert given signed long integer (@num) to ascii using
 * desired radix. Return the number of ascii chars printed.
 * @size: output buffer size
 */
static int ltoa(signed long num, char *buf, int size, int radix)
{
	if (radix < 2 || radix > 16)
		return 0;
	if (num < 0 && size < 2)
		return 0;
	if (num < 0) {
		num *= -1;
		buf[0] = '-';
		return ultoa(num, buf+1, size-1, radix) + 1;
	}
	return ultoa(num, buf, size, radix);
}

/*
 * Definitions for parsing printk arguments. Each argument
 * is described by its descriptor (argdesc) structure.
 */
enum printf_arglen {
	INT = 0,
	LONG,
};
enum printf_argtype {
	NONE = 0,
	SIGNED,
	UNSIGNED,
	STRING,
	CHAR,
	PERCENT,
};
struct printf_argdesc {
	int radix;
	enum printf_arglen len;
	enum printf_argtype type;
};

/*
 * Parse given printf argument expression (@fmt) and save
 * the results to argument descriptor @desc. Input is in
 * in the form: %ld, %d, %x, %lx, etc.
 */
static const char *parse_arg(const char *fmt, struct printf_argdesc *desc)
{
	desc->len = INT;
	desc->type = NONE;
	desc->radix = 10;

	while (*fmt++) {
		switch (*fmt) {
		case 'l': desc->len = LONG; break;
		case 'd': desc->type = SIGNED; goto end;
		case 'u': desc->type = UNSIGNED; goto end;
		case 'x': desc->type = UNSIGNED; desc->radix = 16; goto end;
		case 's': desc->type = STRING; goto end;
		case 'c': desc->type = CHAR; goto end;
		case '%': desc->type = PERCENT; goto end;
		default: desc->type = NONE; goto end;
		}
	}
end:
	return ++fmt;
}

/*
 * Print to @buf the printk argument stored in the untyped
 * @va_list with the the help of type info from the argument
 * descriptor @desc. @size: output buffer size
 */
static int print_arg(char *buf, int size, const char *fmt,
	      struct printf_argdesc *desc, va_list args)
{
	long num;
	unsigned long unum;
	const char *str;
	unsigned char ch;
	int len = 0;

	if (size <= 0)
		return 0;

	switch (desc->type) {
	case SIGNED:
		if (desc->len == LONG)
			num = va_arg(args, long);
		else
			num = va_arg(args, int);
		len = ltoa(num, buf, size, desc->radix);
		break;
	case UNSIGNED:
		if (desc->len == LONG)
			unum = va_arg(args, unsigned long);
		else
			unum = va_arg(args, unsigned int);
		len = ultoa(unum, buf, size, desc->radix);
		break;
	case STRING:
		str = va_arg(args, char *);
		if (!str)
			str = "<*NULL*>";
		len = strlen(str);
		len = min(size, len);
		strncpy(buf, str, len);
		break;
	case CHAR:
		ch = (unsigned char)va_arg(args, int);
		*buf++ = ch;
		len = 1;
		break;
	case PERCENT:
		*buf++ = '%';
		len = 1;
		break;
	default:
		break;
		/* No-op */
	}

	return len;
}

/*
 * Formt given printf-like string (@fmt) and store the result
 * within at most @size bytes. This version does *NOT* append
 * a NULL to output buffer @buf; it's for internal use only.
 */
int vsnprintf(char *buf, int size, const char *fmt, va_list args)
{
	struct printf_argdesc desc = {0};
	char *str = buf;
	int len;

	if (size <= 1)
		return 0;

	while (*fmt) {
		while (*fmt && *fmt != '%' && size) {
			*str++ = *fmt++;
			--size;
		}

		/* At least '%' + a character */
		if (!*fmt || size < 2)
			break;

		fmt = parse_arg(fmt, &desc);
		len = print_arg(str, size, fmt, &desc, args);
		str += len;
		size -= len;
	}

	return str - buf;
}

/*
 * VGA text-mode memory (0xb8000-0xbffff) access
 * @vga_xpos and @vga_ypos forms the current cursor position
 * @vga_buffer: used to access vga ram in a write-only mode. For
 * scrolling, we need to copy the last 24 rows up one row, but
 * reading from vga ram is pretty darn slow and buggy[1], thus the
 * need for a dedicated buffer. "It's also much easier to support
 * multiple terminals that way since we'll have everything on the
 * screen backed up." Thanks travis!
 * [1] 20 seconds to write and scroll 53,200 rows on my core2duo
 * laptop.
 */

#define VGA_BASE    ((char *)VIRTUAL(0xb8000))
#define VGA_MAXROWS 25
#define VGA_MAXCOLS 80
#define VGA_COLOR   0x0f
#define VGA_AREA    (VGA_MAXROWS * VGA_MAXCOLS * 2)

static spinlock_t vga_lock = SPIN_UNLOCKED();
static int vga_xpos, vga_ypos;
static char vga_buffer[VGA_AREA];

/*
 * Scroll the screen up by one row.
 * NOTE! only call while the vga lock is held
 */
static void vga_scrollup(void) {
	char *src = vga_buffer + 2*VGA_MAXCOLS;
	char *dst = vga_buffer;
	int len = 2*((VGA_MAXROWS - 1) * VGA_MAXCOLS);
	unsigned short *p = (unsigned short *)(vga_buffer + len);

	memcpy(dst, src, len);

	for (int i = 0; i < VGA_MAXCOLS; i++)
		*p++ = (VGA_COLOR << 8) + ' ';

	memcpy(VGA_BASE, vga_buffer, VGA_AREA);
	vga_xpos = 0;
	vga_ypos--;
}

/*
 * Write given buffer to VGA ram and scroll the screen
 * up as necessary.
 */
static void vga_write(char *buf, int n)
{
	union x86_rflags flags;
	int max_xpos = VGA_MAXCOLS;
	int max_ypos = VGA_MAXROWS;
	int old_xpos, old_ypos;
	int area, offset;

	/* We may get called from irq context */
	flags = spin_lock_irqsave(&vga_lock);

	old_xpos = vga_xpos;
	old_ypos = vga_ypos;

	while (*buf && n--) {
		if (vga_ypos == max_ypos) {
			vga_scrollup();
			old_ypos = vga_ypos;
			old_xpos = vga_xpos;
		}

		if (*buf != '\n') {
			writew((VGA_COLOR << 8) + *buf,
			       vga_buffer + 2*(vga_xpos + vga_ypos * max_xpos));
			++vga_xpos;
		}

		if (vga_xpos == max_xpos || *buf == '\n') {
			vga_xpos = 0;
			vga_ypos++;
		}

		buf++;
	}

	offset = 2*(old_ypos * max_xpos + old_xpos);
	area = 2*((vga_ypos - old_ypos) * max_xpos + vga_xpos);
	memcpy(VGA_BASE + offset, vga_buffer + offset, area);
	spin_unlock_irqrestore(&vga_lock, flags);
}

/*
 * Without any formatting overhead, write a charactor
 * to screen
 */
void putc(char c)
{
	vga_write(&c, 1);
}

/*
 * Main kernel print method
 */
void printk(const char *fmt, ...)
{
	va_list args;
	char buf[256];
	int n;

	va_start(args, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, args);
	vga_write(buf, n);
	va_end(args);
}
