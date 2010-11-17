/*
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <string.h>
#include <sections.h>
#include <segment.h>
#include <idt.h>
#include <mptables.h>
#include <i8259.h>
#include <pit.h>
#include <apic.h>
#include <ioapic.h>
#include <keyboard.h>
#include <smpboot.h>
#include <e820.h>
#include <mm.h>
#include <vm.h>
#include <paging.h>
#include <kmalloc.h>
#include <sched.h>

static void setup_idt(void)
{
	for (int i = 0; i < EXCEPTION_GATES; i ++)
		set_intr_gate(i, &idt_exception_stubs[i]);

	load_idt(&idtdesc);
}

/*
 * Zero bss section; As said by C99: "All objects with static
 * storage duration shall be initialized before program
 * startup", and that implicit initialization is done with
 * zero. Also kernel assembly code assumes a zeroed bss space
 */
static void clear_bss(void)
{
	memset(__bss_start , 0, __bss_end - __bss_start);
}

static void print_info(void)
{
	printk("Cute 0.0\n\n");

	printk("Text start = 0x%lx\n", __text_start);
	printk("Text end   = 0x%lx\n", __text_end);
	printk("Text size  = %d bytes\n\n", __text_end - __text_start);

	printk("Data start = 0x%lx\n", __data_start);
	printk("Data end   = 0x%lx\n", __data_end);
	printk("Data size  = %d bytes\n\n", __data_end - __data_start);

	printk("BSS start  = 0x%lx\n", __bss_start);
	printk("BSS end    = 0x%lx\n", __bss_end);
	printk("BSS size   = %d bytes\n\n", __bss_end - __bss_start);
}

/*
 * Run compiled testcases, if any
 */
static void run_test_cases(void)
{
	string_run_tests();
	printk_run_tests();
	vm_run_tests();
	pagealloc_run_tests();
	kmalloc_run_tests();
	pit_run_tests();
	apic_run_tests();
	sched_run_tests();
}

/*
 * Bootstrap-CPU start
 */
void __no_return kernel_start(void)
{
	/* Before anything else */
	clear_bss();

	setup_idt();

	print_info();

	e820_init();
	pagealloc_init();

	/* Now git rid of our boot page tables
	 * and setup dynamic permanent ones */
	vm_init();

	/* Enable dynamic heap memory management
	 * to kernel services early on .. */
	kmalloc_init();

	/* Parse the MP tables for needed IRQs
	 * data before initializing the APICs */
	mptables_init();

	i8259_init();

	/* Initialize the APICs (and map their
	 * MMIO regs) before enabling IRQs, and
	 * before firing other cores using IPI */
	apic_init();
	ioapic_init();

	smpboot_init();
	sched_init();

	keyboard_init();

	/* Enable interrupts before running the
	 * test cases */
	local_irq_enable();

	/* Testcases, if compiled */
	run_test_cases();

	halt();
}
