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
 * Delays were tested against wall clock time.
 *
 * Finally, remember that for legacy hardware, it typically takes about
 * 1-µs to access an I/O port.
 */

#include <kernel.h>
#include <stdint.h>
#include <io.h>
#include <pit.h>
#include <tests.h>

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
 * Set the given PIT counter with a count representing given
 * milliseconds value relative to the PIT clock rate.
 *
 * @counter_reg: PIT_COUNTER{0, 1, 2}
 *
 * Due to default oscillation frequency and the max counter
 * size of 16 bits, maximum delay is around 53 milliseconds.
 *
 * Countdown begins once counter is set and GATE-x is up.
 */
static void pit_set_counter(int ms, int counter_reg)
{
	uint32_t counter;
	uint8_t counter_low, counter_high;

	/* counter = ticks per second * seconds to delay
	 *         = PIT_CLOCK_RATE * (ms / 1000)
	 *         = PIT_CLOCK_RATE / (1000 / ms)
	 * We use last form to avoid float arithmetic */
	assert(ms > 0);
	assert((1000 / ms) > 0);
	counter = PIT_CLOCK_RATE / (1000 / ms);

	assert(counter <= UINT16_MAX);
	counter_low = counter & 0xff;
	counter_high = counter >> 8;

	outb(counter_low, counter_reg);
	outb(counter_high, counter_reg);
}

/*
 * Did we program PIT's counter0 to monotonic mode?
 */
static bool timer0_monotonic;

/*
 * Delay/busy-loop for @ms milliseconds.
 */
void pit_mdelay(int ms)
{
	union pit_cmd cmd = { .raw = 0 };

	/* GATE-2 down */
	timer2_stop();

	cmd.bcd = 0;
	cmd.mode = MODE_0;
	cmd.rw = RW_16bit;
	cmd.timer = 2;
	outb(cmd.raw, PIT_CONTROL);

	pit_set_counter(ms, PIT_COUNTER2);

	/* GATE-2 up */
	timer2_start();

	while ((inb(0x61) & PIT_OUT2) == 0)
		cpu_pause();
}

/*
 * Trigger PIT IRQ pin (OUT-0) after given timeout
 */
void pit_oneshot(int ms)
{
	union pit_cmd cmd = { .raw = 0 };

	/* No control over GATE-0: it's always positive */

	if (timer0_monotonic == true)
		panic("PIT: Programming timer0 as one-shot will "
		      "stop currently setup monotonic mode");

	cmd.bcd = 0;
	cmd.mode = MODE_0;
	cmd.rw = RW_16bit;
	cmd.timer = 0;
	outb(cmd.raw, PIT_CONTROL);

	pit_set_counter(ms, PIT_COUNTER0);
}

/*
 * Let the PIT fire at monotonic rate.
 */
void pit_monotonic(int ms)
{
	union pit_cmd cmd = { .raw = 0 };

	/* No control over GATE-0: it's always positive */

	timer0_monotonic = true;

	cmd.bcd = 0;
	cmd.mode = MODE_2;
	cmd.rw = RW_16bit;
	cmd.timer = 0;
	outb(cmd.raw, PIT_CONTROL);

	pit_set_counter(ms, PIT_COUNTER0);
}

#if	PIT_TESTS

#include <vectors.h>
#include <idt.h>
#include <ioapic.h>

/*
 * Give the observer some time ..
 */
static void pit_5secs_delay(void)
{
	for (int i = 0; i < 500; i++)
		pit_mdelay(10);
}

/*
 * Test milli-second delays against wall-clock
 * Remember to have a stopwatch close by
 */
static void pit_test_mdelay(void)
{
	printk("PIT: Testing timer delays\n\n");

	printk("Testing a 10-second delay after notice\n");
	pit_5secs_delay();

	printk("Note: Delay interval started \n");
	for (int i = 0; i < 1000; i++)
		pit_mdelay(10);
	printk("Note: Delay end \n\n");

	printk("Testing a 5-second delay after notice\n");
	pit_5secs_delay();

	printk("Note: Delay interval started \n");
	for (int i = 0; i < 5000; i++)
		pit_mdelay(1);
	printk("Note: Delay end \n\n");

	printk("Testing another 5-second delay after notice\n");
	pit_5secs_delay();

	printk("Note: Delay interval started \n");
	for (int i = 0; i < 100; i++)
		pit_mdelay(50);
	printk("Note: Delay end \n\n");
}

/*
 * Test PIT's monotonic mode code
 */

/*
 * Increase the counter for each periodic PIT tick.
 */
static volatile int ticks_count;

void __pit_periodic_handler(void)
{
	ticks_count++;
}

/*
 * ticks[i]: number of periodic timer ticks triggered after
 * the `i'th independently-programmed delay interval.
 */
#define DELAY_TESTS	100
static uint64_t ticks[DELAY_TESTS];

/*
 * Test the periodic timer by checking the number of ticks
 * produced after each delay interval.
 *
 * Delay intervals code is assumed to be correct.
 */
extern void pit_periodic_handler(void);
static void pit_test_periodic_irq(void)
{
	int i, ms, vector;

	printk("PIT: Testing periodic interrupts\n\n");

	/* FIXME: We should have an IRQ model soon */
	vector = PIT_TESTS_VECTOR;
	set_intr_gate(vector, pit_periodic_handler);
	ioapic_setup_isairq(0, vector, IRQ_BOOTSTRAP);

	/* Testing showed that big delay values catches
	 * more periodic timer accuracy errors .. */
	ms = 50;
	pit_monotonic(ms);

	/* After each delay, store ticks triggered so far */
	local_irq_enable();
	for (i = 0; i < DELAY_TESTS; i++) {
		pit_mdelay(ms);
		ticks[i] = ticks_count;
	}

	/* This should print a list of ones */
	printk("Number of ticks triggered on each delay period: ");
	for (i = 1; i < DELAY_TESTS; i++)
		printk("%ld ", ticks[i] - ticks[i - 1]);
	printk("\n\n");
}

void pit_run_tests(void)
{
	pit_test_periodic_irq();
	pit_test_mdelay();
}

#endif /* PIT_TESTS */
