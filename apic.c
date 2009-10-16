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
 * Mask the now-obsolete 8259A PICs
 * Unfortunately spuruious PIC interrupts do occur even if the
 * PIC is entirely masked. Thus, we remap the chip away from
 * IBM programmed reserved Intel exception numbers 0x8-0xF to
 * saner values at IRQ0_VECTOR.
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

	/* Init command 2, set the most significant five bits
	 * of the vectoring byte. The PIC sets the least three
	 * bits accoding to the interrupt level */
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
	union apic_tpr tpr = { .value = 0 };
	union apic_lvt_timer timer = { .value = 0 };
	union apic_lvt_thermal thermal = { .value = 0 };
	union apic_lvt_perfc perfc = { .value = 0 };
	union apic_lvt_lint lint0 = { .value = 0 };
	union apic_lvt_lint lint1 = { .value = 0 };
	union apic_spiv spiv = { .value = 0 };

	/* No need for the 8259A PIC, we'll exclusivly use
	 * the I/O APIC for interrupt control */
	mask_8259A();

	/* Before doing any apic operation, assure the APIC
	 * registers base address is set as we expect it */
	msr_apicbase_setaddr(APIC_PHBASE);

	/* No complexitis; set the task priority register to
	 * zero: "all interrupts are allowed" */
	tpr.subclass = 0;
	tpr.priority = 0;
	apic_write(APIC_TPR, tpr.value);

	/*
	 * Intialize LVT entries
	 */

	timer.vector = APIC_TIMER_VECTOR;
	timer.mask = APIC_LVT_MASK;
	apic_write(APIC_LVTT, timer.value);

	thermal.vector = APIC_THERMAL_VECTOR;
	thermal.mask = APIC_LVT_MASK;
	apic_write(APIC_LVTTHER, thermal.value);

	perfc.vector = APIC_PERFC_VECTOR;
	perfc.mask = APIC_LVT_MASK;
	apic_write(APIC_LVTPC, perfc.value);

	lint0.vector = 0;
	lint0.trigger_mode = APIC_TM_EDGE;
	lint0.message_type = APIC_MT_EXTINT;
	lint0.mask = APIC_LVT_UNMASK;
	apic_write(APIC_LVT0, lint0.value);

	lint1.vector = 0;
	lint1.trigger_mode = APIC_TM_EDGE;
	lint1.message_type = APIC_MT_NMI;
	lint1.mask = APIC_LVT_UNMASK;
	apic_write(APIC_LVT1, lint1.value);

	/*
	 * Enable local APIC
	 */

	spiv.value = apic_read(APIC_SPIV);
	spiv.apic_enable = 1;
	apic_write(APIC_SPIV, spiv.value);

	msr_apicbase_enable();

	printk("APIC: bootstrap core local apic enabled; ID = %d\n",
	       apic_read(APIC_ID));
}
