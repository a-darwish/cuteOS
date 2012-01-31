/*
 * Copyright (C) 2009-2011 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <list.h>
#include <string.h>
#include <sections.h>
#include <segment.h>
#include <idt.h>
#include <mptables.h>
#include <serial.h>
#include <pit.h>
#include <pic.h>
#include <apic.h>
#include <ioapic.h>
#include <keyboard.h>
#include <smpboot.h>
#include <ramdisk.h>
#include <e820.h>
#include <mm.h>
#include <vm.h>
#include <paging.h>
#include <kmalloc.h>
#include <percpu.h>
#include <atomic.h>
#include <sched.h>

static void setup_idt(void)
{
	for (int i = 0; i < EXCEPTION_GATES; i ++)
		set_intr_gate(i, &idt_exception_stubs[i]);

	load_idt(&idtdesc);
}

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
	list_run_tests();
	string_run_tests();
	printk_run_tests();
	vm_run_tests();
	pagealloc_run_tests();
	kmalloc_run_tests();
	pit_run_tests();
	apic_run_tests();
	percpu_run_tests();
	atomic_run_tests();
	sched_run_tests();
}

/*
 * Bootstrap-CPU start; we came from head.S
 */
void __no_return kernel_start(void)
{
	/* Before anything else, zero the bss section. As said by C99:
	 * “All objects with static storage duration shall be inited
	 * before program startup”, and that the implicit init is done
	 * with zero. Kernel assembly code also assumes a zeroed BSS
	 * space */
	clear_bss();

	/*
	 * Very-early setup: Do not call any code that will use
	 * printk(), `current', per-CPU vars, or a spin lock.
	 */

	setup_idt();

	schedulify_this_code_path(BOOTSTRAP);

	/*
	 * Memory Management init
	 */

	print_info();

	/* First, don't override the ramdisk area (if any) */
	ramdisk_init();

	/* Then discover our physical memory map .. */
	e820_init();

	/* and tokenize the available memory into allocatable pages */
	pagealloc_init();

	/* With the page allocator in place, git rid of our temporary
	 * early-boot page tables and setup dynamic permanent ones */
	vm_init();

	/* MM basics done, enable dynamic heap memory to kernel code
	 * early on .. */
	kmalloc_init();

	/*
	 * Secondary-CPUs startup
	 */

	/* Discover our secondary-CPUs and system IRQs layout before
	 * initializing the local APICs */
	mptables_init();

	/* Remap and mask the PIC; it's just a disturbance */
	serial_init();
	pic_init();

	/* Initialize the APICs (and map their MMIO regs) before enabling
	 * IRQs, and before firing other cores using Inter-CPU Interrupts */
	apic_init();
	ioapic_init();

	/* SMP infrastructure ready, fire the CPUs! */
	smpboot_init();

	keyboard_init();

	/*
	 * Startup finished, roll-in the scheduler!
	 */

	sched_init();
	local_irq_enable();

	run_test_cases();
	halt();
}
