/*
 * printf()-like methods: vsnprintf(), etc
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <stdarg.h>			/* provided by GCC */

/*
 * Semi type-safe min macro using GNU extensions
 */
#define min(x, y) ({		    \
        typeof(x) _m1 = (x);	    \
	typeof(y) _m2 = (y);	    \
	(void) (&_m1 == &_m2);	    \
	_m1 < _m2 ? _m1 : _m2; })

/*
 * Basic string methods till we finish lib/string.c
 * which is going to be written in inline x86-64.
 */
int strlen(const char *str)
{
	const char *tmp;

	for (tmp = str; *tmp; tmp++)
		;
	return tmp - str;
}
void strncpy(char *dst, const char *src, int n)
{
	char *tmp = dst;

	while (n) {
		*tmp = *src;
		if (*tmp)
			src++;
		tmp++;
		n--;
	}
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
static int kvsnprintf(char *buf, int size, const char *fmt, va_list args)
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
 * Main kernel print method
 */
void printk(const char *fmt, ...)
{
	va_list args;
	char buf[80];
	int n;

	va_start(args, fmt);
	n = kvsnprintf(buf, sizeof(buf), fmt, args);
	/* FIXME: VGA code here */
	va_end(args);
}
