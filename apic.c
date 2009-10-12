/*
 * Local APIC configuration
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <segment.h>
#include <msr.h>
#include <apic.h>
#include <io.h>

void *apic_baseaddr;

/*
 * As said in a linux kernel comment, delay for access to PIC on
 * motherboard or in chipset must be at least one microsecnod.
 * FIXME: use a time-based delay function once it's ready.
 */
static inline void outb_pic(uint8_t val, uint16_t port)
{
	outb(val, port);
	io_delay();
}

/*
 * Mask the 8259A PIC: we'll exclusively use the APICs
 * Unfortunately spuruious PIC interrupts do occur even if the
 * PIC is entirely masked. Thus, we remap the chip away from
 * IBM programmed reserved Intel exception numbers 0x8-0xF to
 * saner values at 0xF0.
 */
static void mask_8259A(void)
{
	outb_pic(0xff, PIC_MASTER_DATA);   /* IMR, OCW1 master */
	outb_pic(0xff, PIC_SLAVE_DATA);    /* IMR, OCW1 slave */

	/* Init command 1, cascade mode (D1 = 0), init mode
	 * (D4 = 1), requires init command 4 (D0 = 1), other
	 * bits useless in AT 80x86 mode */
	outb_pic(0x11, PIC_MASTER_CMD);
	outb_pic(0x11, PIC_SLAVE_CMD);

	/* Init command 2, switch IRQs programmed vector number
	 * to desired pic_irq0_vector + irq-number-offset */
	outb_pic(PIC_IRQ0_VECTOR, PIC_MASTER_DATA);
	outb_pic(PIC_IRQ8_VECTOR, PIC_SLAVE_DATA);

	/* Init command 3, in master mode, a "1" is set for each
	 * slave in the system. Through the cascade lines, the
	 * master will enable the relevant slave chip to send
	 * its vectoring data.
	 *
	 * In slave mode, bits 2-0 identifies the slave. Slave
	 * compares its cascade input with those bits, and if
	 * they're equal, it releases the vectoring data to the
	 * data bus */
	outb_pic(1 << PIC_CASCADE_IRQ, PIC_MASTER_DATA);
	outb_pic(PIC_CASCADE_IRQ, PIC_SLAVE_DATA);

	/* Init command 4, 80x86 mode (D0 = 1), Automatic EOI
	 * (D1 = 1), nonbuffered (D3 = 0) */
	outb_pic(0x3, PIC_MASTER_DATA);
	outb_pic(0x3, PIC_SLAVE_DATA);

	/* FIXME: wait for the chip to initialize */

	/* Mask all the chips IRQs */
	outb_pic(0xff, PIC_MASTER_DATA);
	outb_pic(0xff, PIC_SLAVE_DATA);
}

void apic_init(void)
{
	uint64_t val;

	mask_8259A();

	apic_baseaddr = (char *)APIC_VRBASE;
	msr_apicbase_setaddr(APIC_PHBASE);
	msr_apicbase_enable();

	val = apic_read(APIC_SPIV);
	val |= APIC_SPIV_ENABLE;
	apic_write(APIC_SPIV, val);

	printk("APIC: bootstrap core local apic enabled\n");
}
