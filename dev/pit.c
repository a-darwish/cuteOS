/*
 * i8253/i8254-compatible PIT
 *
 * Copyright (C) 2009-2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * Check Intel's "82C54 CHMOS Programmable Interval Timer" datasheet
 * for more details.
 *
 * The PIT contains three timers (counters). Each timer is attached to a
 * GATE and an OUT pin, leading to the pins GATE-0, OUT-0, GATE-1, OUT-1,
 * and GATE-2, OUT-2.
 *
 * If GATE-x is positive, it enables the timer x to count, otherwise the
 * timer's count value stand still.
 *
 * In a PC compatible machine, GATE-0 and GATE-1 are always positive.
 * GATE-2 is controlled by Port 0x61 bit 0.
 *
 * OUT-x is the output pin of timer x. OUT-0 is traditionally mapped to
 * IRQ 0. OUT-1 was typically connected to DRAM refresh logic and is now
 * obsolete. OUT-2 is connected to port 0x61 bit 5.
 *
 * NOTE! Port 0x61 bit 1 is an input pin to a glue AND logic with the
 * OUT-2 pin, which leads to outputting counter 2 ticks to the system
 * speaker if set.
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
 * Extract a PIT-related bit from port 0x61
 */
enum {
	PIT_GATE2   = 0x1,		/* bit 0 - PIT's GATE-2 input */
	PIT_SPEAKER = 0x2,		/* bit 1 - enable/disable speaker */
	PIT_OUT2    = 0x20,		/* bit 5 - PIT's OUT-2 */
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
			timer:2;	/* Which timer of the three (0-2) */
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
 * Start counter 2: raise the GATE-2 pin.
 * Disable glue between OUT-2 and the speaker in the process
 */
static inline void timer2_start(void)
{
	uint8_t val;

	val = (inb(0x61) | PIT_GATE2) & ~PIT_SPEAKER;
	outb(val, 0x61);
}

/*
 * Freeze counter 2: clear the GATE-2 pin.
 */
static inline void timer2_stop(void)
{
	uint8_t val;

	val = inb(0x61) & ~PIT_GATE2;
	outb(val, 0x61);
}

/*
 * Did we program PIT's counter0 to monotonic mode?
 */
static bool timer0_monotonic;

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

	/* GATE-2 down */
	timer2_stop();

	cmd.bcd = 0;
	cmd.mode = MODE_0;
	cmd.rw = RW_16bit;
	cmd.timer = 2;
	outb(cmd.raw, PIT_CONTROL);

	/* counter = ticks per second * seconds to delay
	 * counter = PIT_CLOCK_RATE * (ms / 1000)
	 * We use division to avoid float arithmetic */
	assert(ms > 0);
	counter = PIT_CLOCK_RATE / (1000 / ms);

	assert(counter <= UINT16_MAX);
	counter_low = counter & 0xff;
	counter_high = counter >> 8;

	outb(counter_low, PIT_COUNTER2);
	outb(counter_high, PIT_COUNTER2);

	/* GATE-2 up */
	timer2_start();

	while ((inb(0x61) & PIT_OUT2) == 0)
		asm ("pause");
}

/*
 * Trigger PIT IRQ pin (OUT-0) after given timeout
 */
void pit_oneshot(int ms)
{
	union pit_cmd cmd = { .raw = 0 };
	uint32_t counter;
	uint8_t counter_low, counter_high;

	/* No control over GATE-0: it's always positive */

	if (timer0_monotonic == true)
		panic("PIT: Programming timer0 as one-shot will "
		      "stop currently setup monotonic mode");

	cmd.bcd = 0;
	cmd.mode = MODE_0;
	cmd.rw = RW_16bit;
	cmd.timer = 0;
	outb(cmd.raw, PIT_CONTROL);

	/* FIXME: we may like to put this division in a
	 * header file to let GCC do it at compile-time */
	assert(ms > 0);
	counter = PIT_CLOCK_RATE / (1000 / ms);

	assert(counter <= UINT16_MAX);
	counter_low = counter & 0xff;
	counter_high = counter >> 8;

	outb(counter_low, PIT_COUNTER0);
	outb(counter_high, PIT_COUNTER0);
}

/*
 * Let the PIT fire at monotonic rate.
 *
 * OUT-2 actually starts high and stands high half the period,
 * go low the other half, then return high.
 *
 * Since we program the PIT IRQ as edge-triggered, this gives
 * us the desirable effect of an IRQ every @ms since we  only
 * notice the last rise.
 */
void pit_monotonic(int ms)
{
	union pit_cmd cmd = { .raw = 0 };
	uint32_t counter;
	uint8_t counter_low, counter_high;

	/* No control over GATE-0: it's always positive */

	timer0_monotonic = true;

	cmd.bcd = 0;
	cmd.mode = MODE_3;
	cmd.rw = RW_16bit;
	cmd.timer = 0;
	outb(cmd.raw, PIT_CONTROL);

	assert(ms > 0);
	counter = PIT_CLOCK_RATE / (1000 / ms);

	assert(counter <= UINT16_MAX);
	counter_low = counter & 0xff;
	counter_high = counter >> 8;

	outb(counter_low, PIT_COUNTER0);
	outb(counter_high, PIT_COUNTER0);
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
