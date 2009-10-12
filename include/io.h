/*
 * x86-64 I/O instructions
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

static inline uint8_t inb(uint16_t port)
{
	uint8_t val;

	asm volatile (
		"inb %[port], %[val]"
		: [val] "=a" (val)
		: [port] "Nd" (port));

	return val;
}

static inline void outb(uint8_t val, uint16_t port)
{
	asm volatile (
		"outb %[val], %[port]"
		:
		: [val] "a" (val), [port] "Nd" (port));

}

/*
 * A (hopefully) free port for I/O delay. Port 0x80 causes
 * problems on HP Pavilion laptops.
 */
static inline void io_delay(void)
{
	asm volatile ("outb %al, $0xed");
}
