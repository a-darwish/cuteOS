#ifndef _I8259_H
#define _I8259_H

/*
 * The i8259A PIC
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

/*
 * AT+ standard PIC ports
 */
#define PIC_MASTER_CMD		0x20
#define PIC_SLAVE_CMD		0xa0
#define PIC_MASTER_DATA		0x21
#define PIC_SLAVE_DATA		0xa1

/* Where the slave PIC is connected */
#define PIC_CASCADE_IRQ		2

/* The PICs are entirely masked. Map the IRQs just
 * in case of a triggered spurious interrupt */
#define PIC_IRQ0_VECTOR		240
#define PIC_IRQ8_VECTOR		(PIC_IRQ0_VECTOR + 8)

void i8259_init(void);

#endif /* _I8259_H */
