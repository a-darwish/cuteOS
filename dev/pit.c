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
 * for more details.
 *
 * In a PC compatible machine, port 0x61 bit 0 controls the PIT's GATE
 * 2: the gate input pin of counter 2. Bit 5 is the OUT 2 pin: the output
 * pin of counter 2. Bit 1 is an input pin to a glue AND logic with the
 * OUT 2 pin, outputting counter 2 ticks to the system speaker if set.
 *
 * The 0x61 port is output port B of the 8255 peripheral interface chip.
 * Ofcourse all of this is just emulated in the mobo southbridge.
 *
 * Delays are tested against wall clock time.
 */

#include <kernel.h>
#include <stdint.h>
#include <io.h>
#include <tsc.h>
#include <pit.h>

#define PIT_CLOCK_RATE	1193182ul	/* Hz (ticks per second) */

/*
 * PIT-related port 0x61 bits
 */
enum {
	PIT_GATE2   = 0x1,		/* PIT's GATE 2 pin */
	PIT_SPEAKER = 0x2,		/* Speaker enabled? */
	PIT_OUT2    = 0x20,		/* PIT's Counter 2 output pin */
};

/*
 * The PIT's system interface: an array of peripheral I/O ports.
 * The peripherals chip translates accessing below ports to the
 * right PIT's A0 and A1 address pins, and RD/WR combinations.
 */
enum {
	PIT_COUNTER0 =	0x40,		/* read/write Counter 0 latch */
	PIT_COUNTER1 =	0x41,		/* read/write Counter 1 latch */
	PIT_COUNTER2 =	0x42,		/* read/write Counter 2 latch */
	PIT_CONTROL  =	0x43,		/* read/write Chip's Control Word */
};

/*
 * Control Word format
 */
union pit_cmd {
	uint8_t raw;
	struct {
		uint8_t bcd:1,		/* BCD format for counter divisor? */
			mode:3,		/* Counter modes 0 to 5 */
			rw:2,		/* Read/Write latch, LSB, MSB, or 16-bit */
			counter:2;	/* Which counter of the three (0-2) */
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
	MODE_0 =	0x0,		/* Single interrupt on timeout */
	MODE_1 =	0x1,		/* Hardware retriggerable one shot */
	MODE_2 =	0x2,		/* Rate generator */
	MODE_3 =	0x3,		/* Square-wave mode */
};

/*
 * Start timer 2 counter: raise the PIT's GATE 2 input pin.
 * Disable the glue between timer2 output and the speaker in
 * the process.
 */
static inline void timer2_start(void)
{
	uint8_t val;

	val = (inb(0x61) | PIT_GATE2) & ~PIT_SPEAKER;
	outb(val, 0x61);
}

/*
 * Freeze timer 2 counter by clearing the GATE2 pin.
 */
static inline void timer2_stop(void)
{
	uint8_t val;

	val = inb(0x61) & ~PIT_GATE2;
	outb(val, 0x61);
}

/*
 * Delay/busy-loop for @ms milliseconds.
 * Due to default oscillation frequency and the max counter
 * size of 16 bits, maximum delay is around 53 milliseconds.
 */
void pit_mdelay(int ms)
{
	union pit_cmd cmd = { .raw = 0 };
	uint32_t counter;
	uint8_t counter_low, counter_high;

	timer2_stop();

	cmd.bcd = 0;
	cmd.mode = MODE_0;
	cmd.rw = RW_16bit;
	cmd.counter = 2;
	outb(cmd.raw, PIT_CONTROL);

	/* counter = ticks per second * seconds to delay
	 * counter = PIT_CLOCK_RATE * (ms / 1000)
	 * We use division to avoid float arithmetic */
	counter = PIT_CLOCK_RATE / (1000 / ms);

	assert(counter <= UINT16_MAX);
	counter_low = counter & 0xff;
	counter_high = counter >> 8;

	outb(counter_low, PIT_COUNTER2);
	outb(counter_high, PIT_COUNTER2);

	timer2_start();

	while ((inb(0x61) & PIT_OUT2) == 0)
		asm ("pause");
}

/*
 * Calculate the processor clock using the PIT and the time-
 * stamp counter: it's increased by one for each clock cycle.
 *
 * There's a possibility of being hit by a massive SMI, we
 * repeat the calibration to hope this wasn't the case. Some
 * SMI handlers can take many milliseconds to complete!
 *
 * FIXME: For Intel core2duo cpus and up, ask the architect-
 * ural event monitoring interface to calculate cpu clock.
 *
 * Return cpu clock ticks per second.
 */
uint64_t pit_calibrate_cpu(int repeat)
{
	uint64_t tsc1, tsc2, diff, diff_min, cpu_clock;
	int ms_delay;

	ms_delay = 5;
	diff_min = UINT64_MAX;
	for (int i = 0; i < repeat; i ++) {
		tsc1 = read_tsc();
		pit_mdelay(ms_delay);
		tsc2 = read_tsc();

		diff = tsc2 - tsc1;
		if (diff < diff_min)
			diff_min = diff;
	}

	/* cpu ticks per 1 second =
	 * ticks per delay * (1 / delay in seconds) */
	cpu_clock = diff_min * (1000 / ms_delay);
	return cpu_clock;
}
