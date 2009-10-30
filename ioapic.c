/*
 * I/O APIC setup
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <mptables.h>
#include <ioapic.h>

/*
 * I/O APICs descriptors. The number of i/o apics and their
 * base addresses is read from the MP tables; the rest is
 * read from the chip itself.
 */
int nr_ioapics;
struct ioapic_desc ioapic_descs[IOAPICS_MAX];

struct ioapic_pin i8259_pin = { .apic = -1, .pin = -1 };

/*
 * Find where the 8259 INTR pin is connected to the ioapics
 * by scanning all the IOAPICs for a BIOS set unmasked routing
 * entry with a delivery mode of ExtInt.
 *
 * NOTE! Use this after all the IOAPIC descriptors have been
 * fully initialized
 */
static struct ioapic_pin ioapic_get_8259A_pin(void)
{
	union ioapic_irqentry entry;
	struct ioapic_pin pic = { .apic = -1, .pin = -1 };
	int max_irq;

	for (int apic = 0; apic < nr_ioapics; apic++) {
		max_irq = ioapic_descs[apic].max_irq;
		for (int irq = 0; irq <= max_irq; irq++) {
			entry = ioapic_read_irqentry(apic, irq);

			if (entry.delivery_mode != IOAPIC_DELMOD_EXTINT)
				continue;

			if (entry.mask != IOAPIC_UNMASK)
				continue;

			pic.apic = apic;
			pic.pin = irq;
			return pic;
		}
	}

	return pic;
}

/*
 * Through MP-tables IRQ entries, figure where given ISA
 * interrupt source is connected to the I/O APICs system
 */
static struct ioapic_pin ioapic_isa_pin(int isa_irq, enum mp_irqtype type)
{
	struct ioapic_pin pin = { .apic = -1, .pin = -1 };
	int irq;

	assert(mp_isa_busid != -1);

	for (irq = 0; irq < nr_mpcirqs; irq++) {
		if ((mp_irqs[irq].src_busid == mp_isa_busid) &&
		    (mp_irqs[irq].src_busirq == isa_irq) &&
		    (mp_irqs[irq].type == type))
			break;
	}

	/* No compatible ISA MP irq entry found */
	if (irq >= nr_mpcirqs)
		return pin;

	for (int apic = 0; apic < nr_ioapics; apic++) {
		if(mp_irqs[irq].dst_ioapicid == ioapic_descs[apic].id) {
			pin.apic = apic;
			pin.pin = mp_irqs[irq].dst_ioapicpin;
			return pin;
		}
	}

	return pin;
}

/*
 * FIXME: SMP: we just read the APIC ID of the current executing
 * core and consider it the sole delivery destination.
 */
void ioapic_setup_isairq(uint8_t irq, int vector)
{
	struct ioapic_pin pin;
	union ioapic_irqentry entry = { .value = 0 };

	pin = ioapic_isa_pin(irq, MP_INT);

	entry.vector = vector;
	entry.delivery_mode = IOAPIC_DELMOD_FIXED;
	entry.dest_mode = IOAPIC_DESTMOD_PHYSICAL;
	entry.polarity = IOAPIC_POLARITY_HIGH;
	entry.trigger = IOAPIC_TRIGGER_EDGE;
	entry.mask = IOAPIC_UNMASK;
	entry.dest = apic_read(APIC_ID);

	ioapic_write_irqentry(pin.apic, pin.pin, entry);
}

void ioapic_init(void)
{
	union ioapic_id id = { .value = 0 };
	union ioapic_ver version = { .value = 0 };
	struct ioapic_pin pin1, pin2;

	printk("APIC: %d I/O APIC(s) found\n", nr_ioapics);

	/* Initialize the system-wide IO APICs descriptors
	 * partially initialized using MP table parsers */
	for (int apic = 0; apic < nr_ioapics; apic++) {
		id.value = ioapic_read(apic, IOAPIC_ID);
		if (id.id != ioapic_descs[apic].id) {
			printk("IOAPIC[%d]: BIOS tables apic_id=0x%x, "
			       "chip's apic_id=0x%x\n", apic,
			       ioapic_descs[apic].id, id.id);
			printk("IOAPIC[%d]: Writing BIOS value to chip\n",
			       apic);
			id.id = ioapic_descs[apic].id;
			ioapic_write(apic, IOAPIC_ID, id.value);
		}

		version.value = ioapic_read(apic, IOAPIC_VER);
		ioapic_descs[apic].version = version.version;
		ioapic_descs[apic].max_irq = version.max_irq;

		printk("IOAPIC[%d]: apic_id 0x%x, version 0x%x, maxirq %d, "
		       "address 0x%x\n", apic, id.id, version.version,
		       ioapic_descs[apic].max_irq, ioapic_descs[apic].base);
	}

	/* The PIC mode described in the MP specification is an
	 * outdated way to configure the APICs that was used on
	 * some early MP boards. It's not supported in the ACPI
	 * model and is unlikely to be ever configured by any
	 * x86-64 system, thus ignore setting the IMCR */

	/* Figure where the i8259 INT pin is connected through
	 * hardware setup and through the MP-tables */
	pin1 = ioapic_get_8259A_pin();
	pin2 = ioapic_isa_pin(0, MP_ExtINT);

	if (pin1.apic != -1) {
		i8259_pin = pin1;
		printk("IOAPIC[%d]: ExtINT - i8259 INT connected to pin %d\n",
		       pin1.apic, pin1.pin);
	} else if (pin2.apic != -1) {
		i8259_pin = pin2;
		printk("IOAPIC[%d]: MP - i8259 INT connected to pin %d\n",
		       pin2.apic, pin2.pin);
		printk("IOAPIC[%d]: MP tables and routing entries differ\n",
		       pin2.apic);
	}

	/* We don't trust the BIOS setup: mask all the system
	 * IOAPICs irq routing entries */
	for (int apic = 0; apic < nr_ioapics; apic++)
		for (int irq = 0; irq < ioapic_descs[apic].max_irq; irq++)
			ioapic_mask_irq(apic, irq);
}
