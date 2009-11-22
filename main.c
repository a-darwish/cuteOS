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
#include <apic.h>
#include <ioapic.h>
#include <keyboard.h>
#include <smpboot.h>

void setup_idt(void)
{
	for (int i = 0; i < EXCEPTION_GATES; i ++)
		set_intr_gate(i, &default_idt_stubs[i]);

	load_idt(idtdesc);
}

/*
 * Zero bss section; As said by C99: "All objects with static
 * storage duration shall be initialized before program
 * startup", and that implicit initialization is done with
 * zero. Also kernel assembly code assumes a zeroed bss space
 */
void clear_bss(void)
{
	memset(__bss_start , 0, __bss_end - __bss_start);
}

void print_sections(void)
{
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
 * Bootstrap core start
 */
void kernel_start(void)
{
	/* Before anything else */
	clear_bss();

	setup_idt();

	printk("Cute 0.0\n\n");

	print_sections();

	/* Parse the MP tables for needed IRQs data
	 * before initializing the APICs */
	mptables_init();

	i8259_init();
	apic_init();
	ioapic_init();

	keyboard_init();

	smpboot_init();

	asm volatile ("sti");

	while (true)
		asm volatile ("hlt");
}
