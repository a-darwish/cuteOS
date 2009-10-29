#ifndef _IOAPIC_H
#define _IOAPIC_H

/*
 * I/O APIC definitions
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

/*
 * I/O APIC registers address space
 */

#define IOAPIC_SPACE	0x40		/* Address space size */
#define IOAPIC_PHBASE	0xfec00000	/* Physical base */
#define IOAPIC_PHBASE_MAX (IOAPIC_PHBASE + 0x200000 - IOAPIC_SPACE)

/* FIXME: use a dynamic kmap() to get rid of our physical base
 * address limitations. Current assumption is buggy if the I/O
 * APICs have more redirection table entries than expected */
#define IOAPIC_VRBASE	0xffffffffffc00000

#ifndef __ASSEMBLY__

#include <stdint.h>
#include <kernel.h>
#include <apic.h>
#include <mptables.h>

/*
 * System-wide I/O APIC descriptors for each I/O APIC reported
 * by the BIOS as usable. At lease one ioapic should be enabled.
 */

struct ioapic_desc {
	uint8_t	id;			/* Chip APIC ID */
	uint8_t version;		/* Chip version: 0x11, 0x20, .. */
	uint32_t base;			/* This IOAPIC physical base address */
	uint8_t max_irq;		/* IRQs = (0 - max_irq) inclusive */
};

extern int nr_ioapics;			/* (MP, ACPI) usable I/O APICs count */
#define IOAPICS_MAX	8		/* Arbitary */

/* FIXME: locks around the global resource array once SMP
 * primitive locking is done */
extern struct ioapic_desc ioapic_descs[IOAPICS_MAX];

/*
 * I/O APIC registers and register accessors
 */

#define IOAPIC_ID	0x00
union ioapic_id {
	uint32_t value;
	struct {
		uint32_t reserved0:24, id:8;
	} __packed;
};

#define IOAPIC_VER	0x01
union ioapic_ver {
	uint32_t value;
	struct {
		uint32_t version:8, reserved0:8,
			max_irq:8, reserved1:8;
	} __packed;
};

#define IOAPIC_ARB	0x02
union ioapic_arb {
	uint32_t value;
	struct {
		uint32_t reserved0:24, arbitration:4,
			reserved1:4;
	} __packed;
};

/*
 * Return the virtual mapping of given IOAPIC physical base
 * FIXME: get rid of those ad-hoc mappings using an iomap()
 */
static inline uintptr_t ioapic_virtual(uint32_t addr)
{
	assert(IOAPIC_PHBASE <= addr);
	assert(IOAPIC_PHBASE_MAX >= addr);

	return IOAPIC_VRBASE + IOAPIC_PHBASE - addr;
}

/*
 * Get the IO APIC virtual base from the IOAPICs repository.
 * The data was found either by parsing the MP-tables or ACPI.
 */
static inline uintptr_t ioapic_base(int apic)
{
	assert(apic < nr_ioapics);
	return ioapic_virtual(ioapic_descs[apic].base);
}

static inline uint32_t ioapic_read(int apic, uint8_t reg)
{
	volatile uint32_t *ioregsel = (uint32_t *)ioapic_base(apic);
	volatile uint32_t *iowin = (uint32_t *)(ioapic_base(apic) + 0x10);

	*ioregsel = reg;
	return *iowin;
}

static inline void ioapic_write(int apic, uint8_t reg, uint32_t value)
{
	volatile uint32_t *ioregsel = (uint32_t *)ioapic_base(apic);
	volatile uint32_t *iowin = (uint32_t *)(ioapic_base(apic) + 0x10);

	*ioregsel = reg;
	*iowin = value;
}

#define IOAPIC_REDTBL0	0x10
/* Don't use a single uint64_t element here. All APIC registers are
 * accessed using 32 bit loads and stores. Registers that are
 * described as 64 bits wide are accessed as multiple independent
 * 32 bit registers -- Intel 82093AA datasheet */
union ioapic_irqentry {
	struct {
		uint32_t vector:8, delivery_mode:3, dest_mode:1,
			delivery_status:1, polarity:1, remote_irr:1,
			trigger:1, mask:1, reserved0:15;

		uint32_t reserved1:24, dest:8;
	} __packed;
	struct {
		uint32_t value_low;
		uint32_t value_high;
	} __packed;
	uint64_t value;
};
/* Delivery mode (R/W) */
enum ioapic_delmod {
	IOAPIC_DELMOD_FIXED = 0x0,
	IOAPIC_DELMOD_LOWPR = 0x1,
	IOAPIC_DELMOD_SMI   = 0x2,
	IOAPIC_DELMOD_NMI   = 0x4,
	IOAPIC_DELMOD_INIT  = 0x5,
	IOAPIC_DELMOD_EXTINT= 0x7,
};
/* Destination mode (R/W) */
enum ioapic_destmod {
	IOAPIC_DESTMOD_PHYSICAL = 0x0,
	IOAPIC_DESTMOD_LOGICAL  = 0x1,
};
/* Interrupt Input Pin Polarity (R/W) */
enum ioapic_polarity {
	IOAPIC_POLARITY_HIGH = 0x0,
	IOAPIC_POLARITY_LOW  = 0x1,
};
/* Trigger Mode (R/W) */
enum ioapic_trigger {
	IOAPIC_TRIGGER_EDGE  = 0x0,
	IOAPIC_TRIGGER_LEVEL = 0x1,
};
/* Interrupt Mask (R/W) */
enum {
	IOAPIC_UNMASK = 0x0,
	IOAPIC_MASK   = 0x1,
};

static inline union ioapic_irqentry ioapic_read_irqentry(int apic, uint8_t irq)
{
	union ioapic_irqentry entry = { .value = 0 };
	entry.value_low = ioapic_read(apic, IOAPIC_REDTBL0 + 2*irq);
	entry.value_high = ioapic_read(apic, IOAPIC_REDTBL0 + 2*irq + 1);
	return entry;
}

/*
 * NOTE! Write the upper half _before_ writing the lower half.
 * The low word contains the mask bit, and we want to be sure
 * of the irq entry integrity if the irq is going to be enabled.
 */
static inline void ioapic_write_irqentry(int apic, uint8_t irq,
					 union ioapic_irqentry entry)
{
	ioapic_write(apic, IOAPIC_REDTBL0 + 2*irq + 1, entry.value_high);
	ioapic_write(apic, IOAPIC_REDTBL0 + 2*irq, entry.value_low);
}

static inline void ioapic_mask_irq(int apic, uint8_t irq)
{
	union ioapic_irqentry entry = { .value = 0 };
	entry.value_low = ioapic_read(apic, IOAPIC_REDTBL0 + 2*irq);
	entry.mask = IOAPIC_MASK;
	ioapic_write(apic, IOAPIC_REDTBL0 + 2*irq, entry.value_low);
}

/*
 * Represents where an interrupt source is connected to the
 * I/O APICs system
 */
struct ioapic_pin {
	int apic;			/* which ioapic? */
	int pin;			/* which pin in this ioapic */
};

void ioapic_setup_isairq(uint8_t irq, int vector);
void ioapic_init(void);

#endif /* __ASSEMBLY__ */

#endif /* _IOAPIC_H */
