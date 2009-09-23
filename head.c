/*
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <segment.h>
#include <idt.h>

void setup_idt(void)
{
	for (int i = 0; i < EXCEPTION_GATES; i ++)
		set_intr_gate(i, &default_idt_stubs[i]);

	load_idt(idtdesc);
}

void kernel_start(void)
{
	setup_idt();

	printk("Cute 0.0\n");

	for (;;);
}
