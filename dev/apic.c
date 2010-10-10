/*
 * Local APIC configuration
 *
 * Copyright (C) 2009-2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <segment.h>
#include <msr.h>
#include <apic.h>
#include <vectors.h>
#include <pit.h>
#include <tsc.h>

static int bootstrap_apic_id = -1;

/*
 * Internal and external clock frequencies
 * @cpu_clock: CPU internal clock (speed)
 * @apic_clock: CPU external bus clock (APIC timer, ..)
 */
static uint64_t cpu_clock;
static uint64_t apic_clock;

/*
 * Where the APIC registers are virtually mapped
 */
static void *apic_virt_base;

/*
 * Clock calibrations
 */

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
static uint64_t pit_calibrate_cpu(int repeat)
{
	int ms_delay;
	uint64_t tsc1, tsc2, diff, diff_min, cpu_clock;

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

	/* ticks per second = ticks / delay-in-seconds
	 *                  = ticks / (delay / 1000)
	 *                  = ticks * (1000 / delay)
	 * We use last form to avoid float arithmetic */
	cpu_clock = diff_min * (1000 / ms_delay);

	return cpu_clock;
}

/*
 * Calibrate CPU external bus clock, which is the time
 * base of the APIC timer.
 */
static uint64_t pit_calibrate_apic_timer(void)
{
	int ms_delay;
	uint32_t counter1, counter2, ticks, ticks_min;
	uint64_t apic_clock;
	union apic_lvt_timer lvt_timer;

	/* Before setting up the counter */
	lvt_timer.value = apic_read(APIC_LVTT);
	lvt_timer.timer_mode = APIC_TIMER_ONESHOT;
	lvt_timer.mask = APIC_MASK;
	apic_write(APIC_LVTT, lvt_timer.value);
	apic_write(APIC_DCR, APIC_DCR_1);

	/* Guarantee timer won't overflow */
	counter1 = ticks_min = UINT32_MAX;
	ms_delay = 5;

	for (int i = 0; i < 5; i++) {
		apic_write(APIC_TIMER_INIT_CNT, counter1);
		pit_mdelay(ms_delay);
		counter2 = apic_read(APIC_TIMER_CUR_CNT);

		assert(counter1 > counter2);
		ticks = counter1 - counter2;
		if (ticks < ticks_min)
			ticks_min = ticks;
	}

	/* ticks per second = ticks / delay-in-seconds
	 *                  = ticks / (delay / 1000)
	 *                  = ticks * (1000 / delay)
	 * We use last form to avoid float arithmetic */
	apic_clock = ticks_min * (1000 / ms_delay);

	return apic_clock;
}

/*
 * Local APIC
 */

void apic_init(void)
{
	union apic_tpr tpr = { .value = 0 };
	union apic_lvt_timer timer = { .value = 0 };
	union apic_lvt_thermal thermal = { .value = 0 };
	union apic_lvt_perfc perfc = { .value = 0 };
	union apic_lvt_lint lint0 = { .value = 0 };
	union apic_lvt_lint lint1 = { .value = 0 };
	union apic_spiv spiv = { .value = 0 };

	/*
	 * Basic APIC initialization:
	 * - Before doing any apic operation, assure the APIC
	 *   registers base address is set where we expect it
	 * - Map the MMIO registers region before accessing it
	 * - Find CPU's internal clock frequency: calibrate TSC
	 * - Find APIC timer frequency: calibrate CPU's bus clock
	 */

	msr_apicbase_setaddr(APIC_PHBASE);

	apic_virt_base = vm_kmap(APIC_PHBASE, APIC_MMIO_SPACE);

	cpu_clock = pit_calibrate_cpu(10);
	printk("APIC: Detected %d.%d MHz processor\n",
	       cpu_clock / 1000000, (uint8_t)(cpu_clock % 1000000));

	apic_clock = pit_calibrate_apic_timer();
	printk("APIC: Detected %d.%d MHz bus clock\n",
	       apic_clock / 1000000, (uint8_t)(apic_clock % 1000000));

	/*
	 * Intialize now-mapped APIC registers
	 */

	tpr.subclass = 0;
	tpr.priority = 0;
	apic_write(APIC_TPR, tpr.value);

	timer.vector = APIC_TIMER_VECTOR;
	timer.mask = APIC_MASK;
	apic_write(APIC_LVTT, timer.value);

	thermal.vector = APIC_THERMAL_VECTOR;
	thermal.mask = APIC_MASK;
	apic_write(APIC_LVTTHER, thermal.value);

	perfc.vector = APIC_PERFC_VECTOR;
	perfc.mask = APIC_MASK;
	apic_write(APIC_LVTPC, perfc.value);

	lint0.vector = APIC_LINT0_VECTOR;
	lint0.mask = APIC_MASK;
	apic_write(APIC_LVT0, lint0.value);

	lint1.vector = APIC_LINT1_VECTOR;
	lint1.mask = APIC_MASK;
	apic_write(APIC_LVT1, lint1.value);

	/*
	 * Enable local APIC
	 */

	spiv.value = apic_read(APIC_SPIV);
	spiv.apic_enable = 1;
	apic_write(APIC_SPIV, spiv.value);

	msr_apicbase_enable();

	bootstrap_apic_id = apic_read(APIC_ID);

	printk("APIC: bootstrap core lapic enabled, apic_id=0x%x\n",
	       bootstrap_apic_id);
}

/*
 * APIC Timer
 */

/*
 * Set the APIC timer counter with a count representing
 * given µ-seconds value relative to the bus clock.
 */
static void apic_set_counter_us(uint64_t us)
{
	union apic_dcr dcr;
	uint64_t counter;

	dcr.value = 0;
	dcr.divisor = APIC_DCR_1;
	apic_write(APIC_DCR, dcr.value);

	/* counter = ticks per second * seconds to delay
	 *         = apic_clock * (us / 1000000)
	 *         = apic_clock / (1000000 / us)
	 * We use division to avoid float arithmetic */
	assert(us > 0);
	assert((1000000 / us) > 0);
	counter = apic_clock / (1000000 / us);

	assert(counter <= UINT32_MAX);
	apic_write(APIC_TIMER_INIT_CNT, counter);
}

/*
 * µ-second delay
 */
void apic_udelay(uint64_t us)
{
	union apic_lvt_timer lvt_timer;

	/* Before setting up the counter */
	lvt_timer.value = 0;
	lvt_timer.timer_mode = APIC_TIMER_ONESHOT;
	lvt_timer.mask = APIC_MASK;
	apic_write(APIC_LVTT, lvt_timer.value);

	apic_set_counter_us(us);

	while (apic_read(APIC_TIMER_CUR_CNT) != 0)
		cpu_pause();
}

/*
 * Milli-second delay
 */
void apic_mdelay(int ms)
{
	apic_udelay(ms * 1000);
}

/*
 * Trigger local APIC timer IRQs at periodic rate
 * @ms: milli-second delay between each IRQ
 * @vector: IRQ vector where ticks handler is setup
 */
void apic_monotonic(int ms, uint8_t vector)
{
	union apic_lvt_timer lvt_timer;

	/* Before setting up the counter */
	lvt_timer.value = 0;
	lvt_timer.vector = vector;
	lvt_timer.mask = APIC_UNMASK;
	lvt_timer.timer_mode = APIC_TIMER_PERIODIC;
	apic_write(APIC_LVTT, lvt_timer.value);

	apic_set_counter_us(ms * 1000);
}

/*
 * Inter-Processor Interrupts
 */

void apic_send_ipi(int dst_apic_id, int delivery_mode, int vector)
{
	union apic_icr icr = { .value = 0 };

	icr.vector = vector;
	icr.delivery_mode = delivery_mode;

	icr.dest_mode = APIC_DESTMOD_PHYSICAL;
	icr.dest = dst_apic_id;

	/* "Edge" and "deassert" are for 82489DX */
	icr.level = APIC_LEVEL_ASSERT;
	icr.trigger = APIC_TRIGGER_EDGE;

	/* Writing the low doubleword of the ICR causes
	 * the IPI to be sent: prepare high-word first. */
	apic_write(APIC_ICRH, icr.value_high);
	apic_write(APIC_ICRL, icr.value_low);
}

/*
 * Poll the delivery status bit till the latest IPI is acked
 * by the destination core, or timeout. As advised by Intel,
 * this should be checked after sending each IPI.
 *
 * Return 'true' in case of delivery success.
 * FIXME: fine-grained timeouts using micro-seconds.
 */
bool apic_ipi_acked(void)
{
	union apic_icr icr = { .value = 0 };
	int timeout = 100;

	while (timeout--) {
		icr.value_low = apic_read(APIC_ICRL);

		if (icr.delivery_status == APIC_DELSTATE_IDLE)
			return true;

		pit_mdelay(1);
	}

	return false;
}

/*
 * Basic state accessors
 */

int apic_bootstrap_id(void)
{
	assert(bootstrap_apic_id != -1);

	return bootstrap_apic_id;
}

void *apic_vrbase(void)
{
	assert(apic_virt_base != NULL);

	return apic_virt_base;
}

#if	APIC_TESTS

#include <vectors.h>
#include <idt.h>
#include <pit.h>

/*
 * Give the observer some time ..
 */
static void apic_5secs_delay(void)
{
	for (int i = 0; i < 500; i++)
		apic_mdelay(10);
}

/*
 * Test APIC delays against wall-clock time
 * Remember to have a stopwatch close by
 */
static void apic_test_delay(void)
{
	printk("APIC: Testing timer delays\n\n");

	printk("Testing a 10-second delay after notice\n");
	apic_5secs_delay();

	printk("Note: Delay interval started \n");
	for (int i = 0; i < 1000; i++)
		apic_mdelay(10);
	printk("Note: Delay end \n\n");

	printk("Testing a 10-second delay using u-seconds\n");
	apic_5secs_delay();

	printk("Note: Delay interval started \n");
	for (int i = 0; i < 100000; i++)
		apic_udelay(100);
	printk("Note: Delay end \n\n");

	printk("Testing a 5-second delay after notice\n");
	apic_5secs_delay();

	printk("Note: Delay interval started \n");
	for (int i = 0; i < 5000; i++)
		apic_mdelay(1);
	printk("Note: Delay end \n\n");

	printk("Testing another 5-second delay after notice\n");
	apic_5secs_delay();

	printk("Note: Delay interval started \n");
	for (int i = 0; i < 5; i++)
		apic_mdelay(1000);
	printk("Note: Delay end \n\n");
}

/*
 * Test APIC timer periodic ticks against PIT delays
 */

/*
 * Increase the counter for each periodic timer tick.
 */
static volatile int ticks_count;

void __apic_timer_handler(void)
{
	ticks_count++;
}

/*
 * ticks[i]: number of periodic timer ticks triggered after
 * the `i'th delay interval.
 */
#define DELAY_TESTS	100
static uint64_t ticks[DELAY_TESTS];

/*
 * Test the APIC periodic timer against equivalent-time
 * PIT-programmed delays.
 */
extern void apic_timer_handler(void);
static void apic_test_periodic_mode(void)
{
	int i, ms, vector;

	printk("APIC: Testing periodic interrupts\n\n");

	/* FIXME: We should have an IRQ model soon */
	vector = APIC_TESTS_VECTOR;
	set_intr_gate(vector, apic_timer_handler);

	/* Testing showed that big delay values catches
	 * more periodic timer accuracy errors .. */
	ms = 50;
	apic_monotonic(ms, vector);

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

void apic_run_tests(void)
{
	apic_test_periodic_mode();
	apic_test_delay();
}

#endif	/* APIC_TESTS */
