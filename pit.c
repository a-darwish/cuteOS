/*
 * i8253/i8254-compatible PIT
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * Check Intel's "82C54 CHMOS Programmable Interval Timer" datasheet
 * for more details
 */

#include <kernel.h>
#include <stdint.h>
#include <io.h>

#define PIT_CLOCK_RATE	1193182ul	/* Hz */

/*
 * The PIT's system interface: an array of peripheral
 * I/O ports.
 */
enum {
	PIT_TIMER0  =	0x40,		/* Timer 0 counter latch */
	PIT_TIMER1  =	0x41,		/* Timer 1 counter latch */
	PIT_TIMER2  =	0x42,		/* Timer 2 counter latch */
	PIT_CONTROL =	0x43,		/* Chip's Control Word (modes + cmds) */
};

/*
 * Control Word format
 */
union pit_cmd {
	uint8_t raw;
	struct {
		uint8_t bcd:1,		/* 0: binary counter 16-bits */
			mode:3,		/* Counter modes 0 to 5 */
			rw:2,		/* Read/Write: latch, LSB, MSB, 16-bit */
			counter:2;	/* Which counter of the three */
	} __packed;
};

/*
 * Read/Write control bits
 */
enum {
	RW_LATCH =	0x0,		/* Counter latch command */
	RW_LSB   =	0x1,		/* Read/write least sig. byte only */
	RW_MSB   =	0x2,		/* Read/write most sig. byte only */
	RW_16bit =	0x3,		/* Read/write least then most sig byte */
};

/*
 * Counters modes
 */
enum {
	MODE_0 =	0x0,		/* Interrupt on terminal count */
	MODE_1 =	0x1,		/* Hardware retriggerable one shot */
	MODE_2 =	0x2,		/* Rate generator */
	MODE_3 =	0x3,		/* Square-wave mode */
};

/*
 * Raise the PIT's GATE 2 input pin to enable timer2 counting.
 *
 * Port 0x61 - which was originally output port B of the i8255
 * periphal interface chip - bit0 controls GATE2. We'll also
 * clear bit 1, the glue between timer2 output and the speaker.
 */
static void timer2_start(void)
{
	uint8_t port;

	port = (inb(0x61) & ~0x02) | 0x01;
	outb(port, 0x61);
}

/*
 * Stop the PIT's timer2 by clearing the GATE2 pin.
 */
static void timer2_stop(void)
{
	uint8_t port;

	port = inb(0x61) & 0xfe;
	outb(port, 0x61);
}

/*
 * Delay/busy-loop for @ms milliseconds using PIT. Port
 * 0x61 bit 5 will be set as soon as timer2 overflows.
 */
void mdelay(int ms)
{
	union pit_cmd cmd = { .raw = 0 };
	uint32_t frequency, divisor;
	uint8_t div_low, div_high;

	timer2_stop();

	cmd.bcd = 0;
	cmd.mode = MODE_0;
	cmd.rw = RW_16bit;
	cmd.counter = 2;
	outb(cmd.raw, PIT_CONTROL);

	/* freq = 1 / (ms / 1000); */
	frequency = 1000 / ms;
	divisor = PIT_CLOCK_RATE / frequency;

	assert(divisor <= UINT16_MAX);
	div_low = divisor && 0xff;
	div_high = divisor >> 8;

	outb(div_low, PIT_TIMER2);
	outb(div_high, PIT_TIMER2);

	timer2_start();

	while ((inb(0x61) & 0x20) == 0);
}
