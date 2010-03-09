/*
 * The i8259A PIC
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * We exlusively use the I/O APICs for interrupt control. The PIC is
 * just a disturbance to be completely masked and ignored afterwards.
 *
 * Check Intel's "8259A Programmable Interrupt Controller" datasheet
 * for more details.
 */

#include <io.h>
#include <i8259.h>
#include <idt.h>

/*
 * AT+ standard PIC ports
 */
#define PIC_MASTER_CMD		0x20
#define PIC_SLAVE_CMD		0xa0
#define PIC_MASTER_DATA		0x21
#define PIC_SLAVE_DATA		0xa1

/*
 * Where the slave PIC is connected
 */
#define PIC_CASCADE_IRQ		2

/*
 * The PICs will be entirely masked. Map the IRQs
 * just in case of a triggered 'spurious' interrupt.
 *
 * Assign the least priority possible to those IRQs
 * (biggest vector number = least priority)
 */
#define PIC_IRQ0_VECTOR		0xf0
#define PIC_IRQ7_VECTOR		(PIC_IRQ0_VECTOR + 7)
#define PIC_IRQ8_VECTOR		0xf8
#define PIC_IRQ15_VECTOR	(PIC_IRQ8_VECTOR + 7)

extern void PIC_handler(void);

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
 * Mask the now-obsolete 8259A PICs by setting all the bits
 * of the Interrupt Mask Register.
 */
static inline void i8259_mask(void)
{
	outb_pic(0xff, PIC_MASTER_DATA);   /* IMR, OCW1 master */
	outb_pic(0xff, PIC_SLAVE_DATA);    /* IMR, OCW1 slave */
}

/*
 * Unfortunately spuruious PIC interrupts do occur even if the
 * PIC is entirely masked. Thus, we remap the chips away from
 * IBM programmed reserved Intel exception numbers 0x8-0xF to
 * saner values at IRQ0_VECTOR and mask it afterwards.
 */
void i8259_init(void)
{
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

	i8259_mask();

	/* Now assure that any misbheaving IRQ that get triggered
	 * by the PIC, despite of its masked status, get ignored */

	for (int i = PIC_IRQ0_VECTOR; i <= PIC_IRQ7_VECTOR; i++)
		set_intr_gate(i, PIC_handler);

	for (int i = PIC_IRQ8_VECTOR; i <= PIC_IRQ15_VECTOR; i++)
		set_intr_gate(i, PIC_handler);
}
