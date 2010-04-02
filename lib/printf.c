/*
 * printf()-like methods: vsnprintf(), etc
 *
 * Copyright (C) 2009-2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <stdarg.h>			/* provided by GCC */
#include <string.h>
#include <paging.h>
#include <spinlock.h>
#include <mmio.h>
#include <vga.h>
#include <tests.h>

/*
 * We cannot use assert() for below printk() code as
 * the assert code istelf internally calls printk().
 */
#define printk_assert(condition)						\
	do {									\
		if (__unlikely(!(condition)))					\
			printk_panic("!(" #condition ")");			\
	} while (0);

/*
 * A panic() that can be safely used by printk().
 * We use putc() as it directly invokes the VGA code.
 * NOTE! Don't use any assert()s in this function!
 */
static char panic_prefix[] = "PANIC: printk: ";
static __no_return void printk_panic(const char *str)
{
	const char *prefix;

	prefix = panic_prefix;
	while (*prefix != 0)
		putc(*prefix++);

	while (*str != 0)
		putc(*str++);

	halt();
}

#define PRINTK_MAX_RADIX	16

/*
 * Convert given unsigned long integer (@num) to ascii using
 * desired radix. Return the number of ascii chars printed.
 * @size: output buffer size
 */
static int ultoa(unsigned long num, char *buf, int size, unsigned radix)
{
	int ret, digits;
	char digit[PRINTK_MAX_RADIX + 1] = "0123456789abcdef";

	printk_assert(radix > 2 && radix <= PRINTK_MAX_RADIX);

	digits = 0;
	if (num == 0)
		digits++;

	for (typeof(num) c = num; c != 0; c /= radix)
                digits++;

	ret = digits;

	printk_assert(digits > 0);
	printk_assert(digits <= size);
	for (; digits != 0; digits--) {
		buf[digits - 1] = digit[num % radix];
		num = num / radix;
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
	printk_assert(radix > 2 && radix <= PRINTK_MAX_RADIX);

	if (num < 0) {
		/* Make room for the '-' */
		printk_assert(size >= 2);

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
 * the results to argument descriptor @desc.
 *
 * Input is in in the form: %ld, %d, %x, %lx, etc.
 *
 * Return @fmt after bypassing the '%' expression.
 * FIXME: Better only return printf-expression # of chars.
 */
static const char *parse_arg(const char *fmt, struct printf_argdesc *desc)
{
	int complete;

	printk_assert(*fmt == '%');

	complete = 0;
	desc->len = INT;
	desc->type = NONE;
	desc->radix = 10;

	while (*++fmt) {
		switch (*fmt) {
		case 'l':
			desc->len = LONG;
			break;
		case 'd':
			desc->type = SIGNED, complete = 1;
			goto out;
		case 'u':
			desc->type = UNSIGNED, complete = 1;
			goto out;
		case 'x':
			desc->type = UNSIGNED, desc->radix = 16, complete = 1;
			goto out;
		case 's':
			desc->type = STRING, complete = 1;
			goto out;
		case 'c':
			desc->type = CHAR, complete = 1;
			goto out;
		case '%':
			desc->type = PERCENT, complete = 1;
			goto out;
		default:
			/* Unknown mark: complete by definition */
			desc->type = NONE;
			complete = 1;
			goto out;
		}
	}

out:
	if (complete != 1)
		printk_panic("Unknown/incomplete expression");

	/* Bypass last expression char */
	if (*fmt != 0)
		fmt++;

	return fmt;
}

/*
 * Print to @buf the printk argument stored in the untyped
 * @va_list with the the help of type info from the argument
 * descriptor @desc. @size: output buffer size
 */
static int print_arg(char *buf, int size, struct printf_argdesc *desc,
		     va_list args)
{
	long num;
	unsigned long unum;
	const char *str;
	unsigned char ch;
	int len;

	len = 0;
	printk_assert(size > 0);

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
	struct printf_argdesc desc = { 0 };
	char *str;
	int len;

	if (size < 1)
		return 0;

	str = buf;
	while (*fmt) {
		while (*fmt != 0 && *fmt != '%' && size != 0) {
			*str++ = *fmt++;
			--size;
		}

		/* Mission complete */
		if (*fmt == 0 || size == 0)
			break;

		printk_assert(*fmt == '%');
		fmt = parse_arg(fmt, &desc);

		len = print_arg(str, size, &desc, args);
		str += len;
		size -= len;
	}

	printk_assert(str >= buf);
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
 *
 * Do not use any assert()s in VGA code! (stack overflow)
 */

#define VGA_BASE		((char *)VIRTUAL(0xb8000))
#define VGA_MAXROWS		25
#define VGA_MAXCOLS		80
#define VGA_DEFAULT_COLOR	VGA_COLOR(VGA_BLACK, VGA_WHITE)
#define VGA_AREA		(VGA_MAXROWS * VGA_MAXCOLS * 2)

static spinlock_t vga_lock = SPIN_UNLOCKED();
static int vga_xpos, vga_ypos;
static char vga_buffer[VGA_AREA];

/*
 * Scroll the screen up by one row.
 * NOTE! only call while the vga lock is held
 */
static void vga_scrollup(int color) {
	char *src = vga_buffer + 2*VGA_MAXCOLS;
	char *dst = vga_buffer;
	int len = 2*((VGA_MAXROWS - 1) * VGA_MAXCOLS);
	unsigned short *p = (unsigned short *)(vga_buffer + len);

	memcpy(dst, src, len);

	for (int i = 0; i < VGA_MAXCOLS; i++)
		*p++ = (color << 8) + ' ';

	memcpy(VGA_BASE, vga_buffer, VGA_AREA);
	vga_xpos = 0;
	vga_ypos--;
}

/*
 * Write given buffer to VGA ram and scroll the screen
 * up as necessary.
 */
static void vga_write(char *buf, int n, int color)
{
	union x86_rflags flags;
	int max_xpos = VGA_MAXCOLS;
	int max_ypos = VGA_MAXROWS;
	int old_xpos, old_ypos;
	int area, offset;

	/* NOTE! This will deadlock if the code enclosed
	 * by this lock triggered exceptions: the default
	 * exception handlers implicitly call vga_write() */
	flags = spin_lock_irqsave(&vga_lock);

	old_xpos = vga_xpos;
	old_ypos = vga_ypos;

	while (*buf && n--) {
		if (vga_ypos == max_ypos) {
			vga_scrollup(color);
			old_ypos = vga_ypos;
			old_xpos = vga_xpos;
		}

		if (*buf != '\n') {
			writew((color << 8) + *buf,
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
 * Without any formatting overhead, write a single
 * charactor to screen
 */

/*
 * @color: foreground color; check vga.h
 */
void putc_colored(char c, int color)
{
	printk_assert(color <= VGA_COLOR_MAX);
	vga_write(&c, 1, VGA_COLOR(VGA_BLACK, color));
}

void putc(char c)
{
	putc_colored(c, VGA_DEFAULT_COLOR);
}

/*
 * Main kernel print method
 */

static char buf[1024];
static spinlock_t printk_lock = SPIN_UNLOCKED();

void printk(const char *fmt, ...)
{
	union x86_rflags flags;
	va_list args;
	int n;

	/* NOTE! This will deadlock if the code enclosed
	 * by this lock triggered exceptions: the default
	 * exception handlers already call printk() */
	flags = spin_lock_irqsave(&printk_lock);

	va_start(args, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	vga_write(buf, n, VGA_DEFAULT_COLOR);

	spin_unlock_irqrestore(&printk_lock, flags);
}

/*
 * Minimal printk test cases
 */

#if	PRINTK_TESTS

static void printk_test_int(void)
{
	printk("(-10, 10): ");
	for (int i = -10; i <= 10; i++)
		printk("%d ", i);
	printk("\n");

	printk("(INT64_MIN, 0xINT64_MIN + 10): ");
	int64_t start = (INT64_MAX * -1) - 1;
	for (int64_t i = start; i <= start + 10; i++)
		printk("%ld ", i);
	printk("\n");
}

static void printk_test_hex(void)
{
	printk("(0x0, 0x100): ");
	for (int i = 0; i <= 0x100; i++)
		printk("0x%x ", i);
	printk("\n");

	printk("(0xUINT64_MAX, 0xUINT64_MAX - 0x10): ");
	for (uint64_t i = UINT64_MAX; i >= UINT64_MAX - 0x10; i--)
		printk("0x%lx ", i);
	printk("\n");
}

static void printk_test_string(void)
{
	const char *test1, *test2, *test3;

	printk("(a, d): ");
	printk("a");
	printk("b");
	printk("c");
	printk("d");
	printk("\n");

	printk("(a, z): ");
	for (char c = 'a'; c <= 'z'; c++)
		printk("%c ", c);
	printk("\n");

	test1 = "Test1";
	test2 = "Test2";
	test3 = NULL;
	printk("Tests: %s %s %s\n", test1, test2, test3);
}

/*
 * Test printk reaction to NULL and incomplete
 * C printf expressions
 */
static char tmpbuf[100];
static void printk_test_format(void)
{
	const char *fmt;
	int len;

	for (int i = 0; i < 0x1000; i++)
		printk("");

	/* This code is expected to panic.
	 * Modify loop counter manually */
	fmt = "%d %x %ld";
	len = strlen(fmt);
	for (int i = 0; i <= len; i++) {
		printk("[%d] ", i);

		strncpy(tmpbuf, fmt, len);
		tmpbuf[i] = 0;
		printk(tmpbuf, 1, 2, 3);

		printk("\n");
	}
}

static void printk_test_colors(void)
{
	printk("Colored text: ");
	putc_colored('A', VGA_BLACK);
	putc_colored('A', VGA_BLUE);
	putc_colored('A', VGA_GREEN);
	putc_colored('A', VGA_CYAN);
	putc_colored('A', VGA_RED);
	putc_colored('A', VGA_MAGNETA);
	putc_colored('A', VGA_BROWN);
	putc_colored('A', VGA_LIGHT_GRAY);
	putc_colored('A', VGA_GRAY);
	putc_colored('A', VGA_LIGHT_BLUE);
	putc_colored('A', VGA_LIGHT_GREEN);
	putc_colored('A', VGA_LIGHT_CYAN);
	putc_colored('A', VGA_LIGHT_RED);
	putc_colored('A', VGA_LIGHT_MAGNETA);
	putc_colored('A', VGA_YELLOW);
	putc_colored('A', VGA_WHITE);
	printk("\n");
}

void printk_run_tests(void)
{
	printk_test_int();
	printk_test_hex();
	printk_test_string();
//	printk_test_format();
	printk_test_colors();
}

#endif /* PRINTK_TESTS */
