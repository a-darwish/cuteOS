/*
 * Multiple-Processor (MP) Initialization
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <smpboot.h>
#include <paging.h>
#include <string.h>
#include <apic.h>
#include <idt.h>
#include <pit.h>

/*
 * System-wide cpu descriptors. Number of CPUs and their APIC
 * IDs is gathered from MP or ACPI MADT tables.
 */
int nr_cpus;
struct cpu_desc cpu_descs[CPUS_MAX];

int nr_alive_cpus = 1;

/*
 * Assembly trampoline code start and end pointers, defined
 * in trampoline.S
 */
extern const char trampoline[];
extern const char trampoline_end[];

/*
 * Common Inter-Processor Interrupts
 */

/*
 * Send an INIT IPI to all cores, except self.
 */
static void ipi_broadcast_init(void)
{
	union apic_icr icr = { .value = 0 };

	/* Zero for "future compatibility" */
	icr.vector = 0;

	icr.delivery_mode = APIC_DELMOD_INIT;

	/* "Edge" and "deassert" are for 82489DX */
	icr.level = APIC_LEVEL_ASSERT;
	icr.trigger = APIC_TRIGGER_EDGE;

	icr.dest_mode = APIC_DESTMOD_PHYSICAL;
	icr.dest_shorthand = APIC_DEST_SHORTHAND_ALL_BUT_SELF;

	/* Using shorthand, writing ICR low-word is enough */
	apic_write(APIC_ICRL, icr.value_low);
}

/*
 * Send a startup IPI
 */
static void ipi_sipi(uint32_t start_vector, int apic_id)
{
	union apic_icr icr = { .value = 0 };

	/* Start vector should be 4K-page aligned */
	assert((start_vector & 0xfff) == 0);
	assert(start_vector >= 0x10000 && start_vector <= 0x90000);
	icr.vector = start_vector >> 12;

	icr.delivery_mode = APIC_DELMOD_START;

	/* "Edge" and "deassert" are for 82489DX */
	icr.level = APIC_LEVEL_ASSERT;
	icr.trigger = APIC_TRIGGER_EDGE;

	icr.dest_mode = APIC_DESTMOD_PHYSICAL;
	icr.dest = apic_id;

	/* Writing the low doubleword of the ICR causes
	 * the IPI to be sent: prepare high-word first. */
	apic_write(APIC_ICRH, icr.value_high);
	apic_write(APIC_ICRL, icr.value_low);
}

/*
 * Start secondary cores. Init each AP core iteratively as
 * the trampoline code can't be executed in parallel.
 *
 * FIXME: 200 micro-second delay between the two SIPIs
 * FIXME: fine-grained timeouts using micro-seconds.
 */
static int start_secondary_cpus(void)
{
	int count, apic_id, ack1, ack2, timeout;

	for (int i = 0; i < nr_cpus; i++) {
		if (cpu_descs[i].bsc)
			continue;

		count = nr_alive_cpus;
		apic_id = cpu_descs[i].apic_id;

		ipi_sipi(SMPBOOT_START, apic_id);
		ack1 = apic_ipi_acked();

		ipi_sipi(SMPBOOT_START, apic_id);
		ack2 = apic_ipi_acked();

		if (!ack1 || !ack2) {
			printk("SMP: SIPI not delivered\n");
			return ack1;
		}

		timeout = 1000;
		while (timeout-- && count == nr_alive_cpus)
			pit_mdelay(1);

		if (!timeout) {
			printk("SMP: Timeout waiting for AP core\n");
			return timeout;
		}
	}

	return 1;
}

/*
 * AP core C code start. We come here from the trampoline,
 * which has assigned bootstrap's gdt, idt, and page tables
 * to that core.
 */
void __no_return secondary_start(void)
{
	union apic_id id;

	/* Quickly till the parent we're alive */
	++nr_alive_cpus;

	id.value = apic_read(APIC_ID);
	printk("SMP: CPU apic_id=%d started\n", id.id);

	halt();
}

void smpboot_init(void)
{
	int res;
	struct smpboot_params params;

	printk("SMP: %d usable CPUs found\n", nr_cpus);

	/* Copy trampoline and params to AP's boot vector */
	smpboot_params_validate_offsets();
	params.cr3 = get_cr3();
	params.idt_desc = get_idt();
	params.gdt_desc = get_gdt();

	memcpy(VIRTUAL(SMPBOOT_START), trampoline, trampoline_end - trampoline);
	memcpy(VIRTUAL(SMPBOOT_PARAMS), &params, sizeof(params));

	/* Broadcast INIT IPI: wakeup AP cores from their deep
	 * halted state (IF=0) and let them wait for the SIPI */
	ipi_broadcast_init();
	res = apic_ipi_acked();
	if (!res)
		panic("Couldn't deliver broadcast INIT IPI\n");

	pit_mdelay(10);

	/* Send the double SIPI sequence */
	res = start_secondary_cpus();
	if (!res)
		panic("Couldn't start-up AP cores\n");

	assert(nr_alive_cpus == nr_cpus);
}
