#ifndef _APIC_H
#define _APIC_H

/*
 * Local APIC definitions, 8259A PIC ports, ..
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

/*
 * I/O APIC memory-mapped registers base
 */
#define IOAPIC_PHBASE 0xfec00000		/* Physical */
#define IOAPIC_VRBASE 0xffffffffffc00000	/* Virtual; head.S */

/*
 * Local APIC registers base addresses = the I/O APIC
 * registers physical and virtual base + 2MB.
 */
#define APIC_PHBASE (IOAPIC_PHBASE + 0x200000)	/* Physical */
#define APIC_VRBASE (IOAPIC_VRBASE + 0x200000)	/* Virtual; head.S */

#ifndef __ASSEMBLY__

#include <msr.h>
#include <stdint.h>

/*
 * APIC Base Address MSR
 */
#define MSR_APICBASE 0x0000001b
#define MSR_APICBASE_ENABLE    (1UL << 11)
#define MSR_APICBASE_BSC       (1UL << 8)
#define MSR_APICBASE_ADDRMASK  (0x000ffffffffff000)

static inline uint64_t msr_apicbase_getaddr(void)
{
	uint64_t msr = read_msr(MSR_APICBASE);
	return (msr & MSR_APICBASE_ADDRMASK);
}

static inline void msr_apicbase_setaddr(uint64_t addr)
{
	uint64_t msr = read_msr(MSR_APICBASE);
	msr &= ~MSR_APICBASE_ADDRMASK;
	addr &= MSR_APICBASE_ADDRMASK;
	msr |= addr;
	write_msr(MSR_APICBASE, msr);
}

static inline void msr_apicbase_enable(void)
{
	uint64_t tmp;

	tmp = read_msr(MSR_APICBASE);
	tmp |= MSR_APICBASE_ENABLE;
	write_msr(MSR_APICBASE, tmp);
}

/*
 * APIC mempory-mapped registers. Offsets are relative to the
 * the Apic Base Address and are aligned on 128-bit boundary.
 */

#define APIC_ID		0x20	/* APIC ID Register */
#define APIC_LVR	0x30	/* APIC Version Register */

#define APIC_TPR	0x80    /* Task Priority Register */
union apic_tpr {
	unsigned subclass:4, priority:4, reserved:24;
	uint32_t value;
}__attribute__((packed));

#define APIC_APR	0x90	/* Arbitration Priority Register */
#define APIC_PPR	0xa0	/* Processor Priority Register */
#define APIC_EOI	0xb0	/* End of Interrupt Register */
#define APIC_RRR	0xc0	/* Remote Read Register */
#define APIC_LDR	0xd0	/* Logical Desitination Register */
#define APIC_DFR	0xe0	/* Destination Format Register */

#define APIC_SPIV	0xf0	/* Spurious Interrupt Vector Register */
union apic_spiv {
	unsigned vector:8, apic_enable:1, focus:1, reserved:22;
	uint32_t value;
}__attribute__((packed));

#define APIC_ESR	0x280   /* Error Status Register */
#define APIC_ICRL	0x300   /* Interrupt Command Register Low [31:0] */
#define APIC_ICRH	0x310   /* Interrupt Command Register High [63:32] */

/*
 * Local Vector Table entries
 */

#define APIC_LVTT	0x320   /* Timer LVT Entry */
union apic_lvt_timer {
	unsigned vector:8, reserved0:4, delivery_status:1, reserved1:3,
		mask:1, timer_mode:1, reserved2:14;
	uint32_t value;
}__attribute__((packed));

#define APIC_LVTTHER	0x330   /* Thermal LVT Entry */
union apic_lvt_thermal {
	unsigned vector:8, message_type:3, reserved0:1, delivery_status:1,
		reserved1:3, mask:1, reserved3:15;
	uint32_t value;
}__attribute__((packed));

#define APIC_LVTPC	0x340   /* Performance Counter LVT Entry */
union apic_lvt_perfc {
	unsigned vector:8, message_type:3, reserved0:1, delivery_status:1,
		reserved1:3, mask:1, reserved3:15;
	uint32_t value;
}__attribute__((packed));

#define APIC_LVT0	0x350	/* Local Interrupt 0 LVT Entry */
#define APIC_LVT1	0x360	/* Local Interrupt 1 LVT Entry */
union apic_lvt_lint {
	unsigned vector:8, message_type:3, reserved0:1, delivery_status:1,
		reserved1:1, remote_irr:1, trigger_mode:1, mask:1,
		reserved3:15;
	uint32_t value;
}__attribute__((packed));

#define APIC_LVTERR	0x370   /* Error LVT Entry */
union apic_lvt_error {
	unsigned vector:8, message_type:3, reserved0:1, delivery_status:1,
		reserved1:3, mask:1, reserved3:15;
	uint32_t value;
}__attribute__((packed));

/*
 * LVT entries fields values
 */

#define APIC_LVT_MASK	1
#define APIC_LVT_UNMASK	0

#define APIC_TM_LEVEL	1	/* Trigger Mode: level */
#define APIC_TM_EDGE	0	/* Trigger Mode: edge */

#define APIC_MT_FIXED	0x0	/* Message Type: fixed */
#define APIC_MT_SMI	0x2	/* Message Type: SMI; vector = 00 */
#define APIC_MT_NMI	0x4	/* Message Type: NMI; vector ignored */
#define APIC_MT_EXTINT	0x7	/* Message Type: external interrupt */

/*
 * APIC register accessors
 */

static inline void apic_write(uint32_t reg, uint32_t val)
{
	volatile uint32_t *addr = (uint32_t *)(APIC_VRBASE + reg);
	*addr = val;
}

static inline uint32_t apic_read(uint32_t reg)
{
	volatile uint32_t *addr = (uint32_t *)(APIC_VRBASE + reg);
	return *addr;
}

void apic_init(void);

/*
 * I/O APIC registers and register accessors
 */

#define IOAPIC_ID	0x00
union ioapic_id {
	uint32_t reserved0:24, id:8;
	uint32_t value;
};

#define IOAPIC_VER	0x01
union ioapic_ver {
	uint32_t version:8, reserved0:8, max_entry:8, reserved1:8;
	uint32_t value;
};

#define IOAPIC_ARB	0x02
union ioapic_arb {
	uint32_t reserved0:24, arbitration:4, reserved1:4;
	uint32_t value;
};

static inline uint32_t ioapic_read(uint8_t reg)
{
	volatile uint32_t *ioregsel = (uint32_t *)IOAPIC_VRBASE;
	volatile uint32_t *iowin = (uint32_t *)(IOAPIC_VRBASE + 0x10);
	*ioregsel = reg;
	return *iowin;
}

static inline void ioapic_write(uint8_t reg, uint32_t value)
{
	volatile uint32_t *ioregsel = (uint32_t *)IOAPIC_VRBASE;
	volatile uint32_t *iowin = (uint32_t *)(IOAPIC_VRBASE + 0x10);
	*ioregsel = reg;
	*iowin = value;
}

void ioapic_init(void);

/*
 * Ports for an 8259A-equivalent PIC chip
 */
#define PIC_MASTER_CMD	0x20
#define PIC_SLAVE_CMD	0xa0
#define PIC_MASTER_DATA	0x21
#define PIC_SLAVE_DATA	0xa1
#define PIC_CASCADE_IRQ	2

/*
 * Vector numbers for all IRQ types
 * FIXME: meaningless placeholder values set till we have
 * the big picture on assigning vector numbers to IRQs.
 */

#define PIC_IRQ0_VECTOR	240
#define PIC_IRQ8_VECTOR (PIC_IRQ0_VECTOR + 8)

#define APIC_TIMER_VECTOR   0
#define APIC_THERMAL_VECTOR 0
#define APIC_PERFC_VECTOR   0

#endif /* !__ASSEMBLY__ */
#endif /* _APIC_H */
