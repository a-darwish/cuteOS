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

#include <ioapic.h>

/*
 * Local APIC registers base addresses = the I/O APIC
 * registers physical and virtual base + 2MB.
 */
#define APIC_PHBASE (IOAPIC_PHBASE + 0x200000)	/* Physical */
#define APIC_VRBASE (IOAPIC_VRBASE + 0x200000)	/* Virtual; head.S */

#ifndef __ASSEMBLY__

#include <msr.h>
#include <stdint.h>
#include <mptables.h>

/*
 * APIC Base Address MSR
 */
#define MSR_APICBASE 0x0000001b
#define MSR_APICBASE_ENABLE    (1UL << 11)
#define MSR_APICBASE_BSC       (1UL << 8)
#define MSR_APICBASE_ADDRMASK  (0x000ffffffffff000ULL)

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
union apic_id {
	struct {
		uint32_t reserved:24, id:8;
	} __packed;
	uint32_t value;
};

#define APIC_LVR	0x30	/* APIC Version Register */

#define APIC_TPR	0x80    /* Task Priority Register */
union apic_tpr {
	struct {
		uint32_t subclass:4, priority:4, reserved:24;
	} __packed;
	uint32_t value;
};

#define APIC_APR	0x90	/* Arbitration Priority Register */
#define APIC_PPR	0xa0	/* Processor Priority Register */
#define APIC_EOI	0xb0	/* End of Interrupt Register */
#define APIC_RRR	0xc0	/* Remote Read Register */
#define APIC_LDR	0xd0	/* Logical Desitination Register */
#define APIC_DFR	0xe0	/* Destination Format Register */

#define APIC_SPIV	0xf0	/* Spurious Interrupt Vector Register */
union apic_spiv {
	struct {
		uint32_t vector:8, apic_enable:1, focus:1, reserved:22;
	} __packed;
	uint32_t value;
};

#define APIC_ESR	0x280   /* Error Status Register */

#define APIC_ICRL	0x300   /* Interrupt Command Register Low [31:0] */
#define APIC_ICRH	0x310   /* Interrupt Command Register High [63:32] */
union apic_icr {
	struct {
		uint32_t vector:8, delivery_mode:3, dest_mode:1,
			delivery_status:1, reserved0:1, level:1,
			trigger:1, reserved1:2, dest_shorthand:2,
			reserved2:12;
		uint32_t reserved3:24, dest:8;
	} __packed;

	/* Writing the low word of the ICR causes the
	 * Inter-Process Interrupt (IPI) to be sent */
	struct {
		uint32_t value_low;
		uint32_t value_high;
	} __packed;

	uint32_t value;
};

/*
 * Local Vector Table entries
 */

#define APIC_LVTT	0x320   /* Timer LVT Entry */
union apic_lvt_timer {
	struct {
		unsigned vector:8, reserved0:4, delivery_status:1, reserved1:3,
			mask:1, timer_mode:1, reserved2:14;
	} __packed;
	uint32_t value;
};

#define APIC_LVTTHER	0x330   /* Thermal LVT Entry */
union apic_lvt_thermal {
	struct {
		unsigned vector:8, delivery_mode:3, reserved0:1,
			delivery_status:1, reserved1:3, mask:1, reserved3:15;
	} __packed;
	uint32_t value;
};

#define APIC_LVTPC	0x340   /* Performance Counter LVT Entry */
union apic_lvt_perfc {
	struct {
		unsigned vector:8, delivery_mode:3, reserved0:1,
			delivery_status:1, reserved1:3, mask:1, reserved3:15;
	} __packed;
	uint32_t value;
};

#define APIC_LVT0	0x350	/* Local Interrupt 0 LVT Entry */
#define APIC_LVT1	0x360	/* Local Interrupt 1 LVT Entry */
union apic_lvt_lint {
	struct {
		unsigned vector:8, delivery_mode:3, reserved0:1,
			delivery_status:1, reserved1:1, remote_irr:1, trigger:1,
			mask:1, reserved3:15;
	} __packed;
	uint32_t value;
};

#define APIC_LVTERR	0x370   /* Error LVT Entry */
union apic_lvt_error {
	struct {
		unsigned vector:8, delivery_mode:3, reserved0:1,
			delivery_status:1, reserved1:3, mask:1, reserved3:15;
	} __packed;
	uint32_t value;
};

/*
 * APIC registers field values
 */

/* Delivery mode for IPI and LVT entries */
enum {
	APIC_DELMOD_FIXED = 0x0,	/* deliver to core in vector field */
	APIC_DELMOD_LOWPR = 0x1,	/* to lowest cpu among dest cores */
	APIC_DELMOD_SMI   = 0x2,	/* deliver SMI; vector should be zero */
	APIC_DELMOD_NMI   = 0x4,	/* deliver NMI; vector ignored */
	APIC_DELMOD_INIT  = 0x5,	/* IPI INIT; vector should be zero */
	APIC_DELMOD_START = 0x6,	/* Startup IPI; core starts at 0xVV000 */
	APIC_DELMOD_ExtINT= 0x7,	/* Get IRQ vector by PIC's INTA cycle */
};

/* IPI destination mode */
enum {
	APIC_DESTMOD_PHYSICAL = 0x0,
	APIC_DESTMOD_LOGICAL  = 0x1,
};

/* Trigger mode for IPI, LINT0, and LINT1
 * This's only used when delivery mode == `fixed'.
 * NMI, SMI, and INIT are always edge-triggered */
enum {
	APIC_TRIGGER_EDGE  = 0x0,
	APIC_TRIGGER_LEVEL = 0x1,
};

/* Destination shorthands for IPIs
 * When in use, writing the ICR low-word is enough */
enum {
	APIC_DEST_SHORTHAND_NONE = 0x0,
	APIC_DEST_SHORTHAND_SELF = 0x1,
	APIC_DEST_SHORTHAND_ALL_AND_SELF = 0x2,
	APIC_DEST_SHORTHAND_ALL_BUT_SELF = 0x3,
};

/* Intereupt level for IPIs */
enum {
	APIC_LEVEL_DEASSERT = 0x0,	/* 82489DX Obsolete. _Never_ use */
	APIC_LEVEL_ASSERT   = 0x1,	/* Always use assert */
};

/* Delivery status for IPI and LVT entries */
enum {
	APIC_DELSTATE_IDLE    = 0,	/* No IPI action, or last IPI acked */
	APIC_DELSTATE_PENDING = 1,	/* Last IPI not yet acked */
};

/* LVT entries mask bit */
enum {
	APIC_UNMASK = 0x0,
	APIC_MASK   = 0x1,
};

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
int apic_ipi_acked(void);

/*
 * FIXME: meaningless placeholder values set till we have
 * the big picture on assigning vector numbers to IRQs.
 */

#define APIC_TIMER_VECTOR   0
#define APIC_THERMAL_VECTOR 0
#define APIC_PERFC_VECTOR   0

#endif /* !__ASSEMBLY__ */
#endif /* _APIC_H */
