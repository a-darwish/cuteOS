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

#ifndef __ASSEMBLY__

#include <kernel.h>
#include <stdint.h>
#include <paging.h>
#include <mmio.h>
#include <vm.h>
#include <msr.h>
#include <mptables.h>
#include <tests.h>

/*
 * APIC Base Address MSR
 */
#define MSR_APICBASE		0x0000001b
#define MSR_APICBASE_ENABLE	(1UL << 11)
#define MSR_APICBASE_BSC	(1UL << 8)
#define MSR_APICBASE_ADDRMASK	0x000ffffffffff000ULL

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
	uint32_t raw;
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
union apic_ldr {
	struct {
		uint32_t reserved:24, logical_id:8;
	} __packed;
	uint32_t value;
};

#define APIC_DFR	0xe0	/* Destination Format Register */
union apic_dfr {
	struct {
		uint32_t reserved:28, apic_model:4;
	} __packed;
	uint32_t value;
};

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

	uint64_t value;
};

/*
 * Local Vector Table entries
 */

#define APIC_LVTT	0x320   /* Timer LVT Entry */
union apic_lvt_timer {
	struct {
		uint32_t vector:8,
			reserved0:4,
			delivery_status:1,	/* read-only */
			reserved1:3,
			mask:1,
			timer_mode:1,
			reserved2:14;
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

#define APIC_TIMER_INIT_CNT	0x380	/* Timer Initial Count register */
#define APIC_TIMER_CUR_CNT	0x390	/* Timer Current Count register */

#define APIC_DCR		0x3e0	/* Timer Divide Configuration register */
union apic_dcr {
	struct {
		uint32_t divisor:4,	/* NOTE! bit-2 MUST be zero */
			reserved0:28;
	} __packed;
	uint32_t value;
};

/* Timer Divide Register divisor; only APIC_DCR_1 was tested */
enum {
	APIC_DCR_2   = 0x0,		/* Divide by 2   */
	APIC_DCR_4   = 0x1,		/* Divide by 4   */
	APIC_DCR_8   = 0x2,		/* Divide by 8   */
	APIC_DCR_16  = 0x3,		/* Divide by 16  */
	APIC_DCR_32  = 0x8,		/* Divide by 32  */
	APIC_DCR_64  = 0x9,		/* Divide by 64  */
	APIC_DCR_128 = 0xa,		/* Divide by 128 */
	APIC_DCR_1   = 0xb,		/* Divide by 1!  */
};

/*
 * APIC registers field values
 */

/* TPR priority and subclass */
enum {
	APIC_TPR_DISABLE_IRQ_BALANCE = 0,/* Disable hardware IRQ balancing */
};

/* Logical Destination Mode model (DFR) */
enum {
	APIC_MODEL_CLUSTER = 0x0,	/* Hierarchial cluster */
	APIC_MODEL_FLAT    = 0xf,	/* Unique APIC ID for up to 8 cores */
};

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

/* APIC timer modes */
enum {
	APIC_TIMER_ONESHOT  = 0x0,	/* Trigger timer as one shot */
	APIC_TIMER_PERIODIC = 0x1,	/* Trigger timer monotonically */
};

/* APIC entries hardware-reset values, Intel-defined */
enum {
	APIC_TPR_RESET  = 0x00000000,	/* priority & priority subclass = 0 */
	APIC_LDR_RESET  = 0x00000000,	/* destination logical id = 0 */
	APIC_DFR_RESET  = UINT32_MAX,	/* Flat model, reserved bits all 1s */
	APIC_SPIV_RESET = 0x000000ff,	/* vector=ff, apic disabled, rsrved=0 */
	APIC_LVT_RESET  = 0x00010000,	/* All 0s, while setting the mask bit */
};

/*
 * APIC register accessors
 */

#define APIC_PHBASE	0xfee00000	/* Physical */
#define APIC_MMIO_SPACE	PAGE_SIZE	/* 4-KBytes */
void *apic_vrbase(void);		/* Virtual */

static inline void apic_write(uint32_t reg, uint32_t val)
{
	void *vaddr;

	vaddr = apic_vrbase();
	vaddr = (char *)vaddr + reg;
	writel(val, vaddr);
}

static inline uint32_t apic_read(uint32_t reg)
{
	void *vaddr;

	vaddr = apic_vrbase();
	vaddr = (char *)vaddr + reg;
	return readl(vaddr);
}

void apic_init(void);
void apic_local_regs_init(void);

uint8_t apic_bootstrap_id(void);

void apic_udelay(uint64_t us);
void apic_mdelay(int ms);
void apic_monotonic(int ms, uint8_t vector);

void apic_send_ipi(int dst_id, int del_mode, int vector);
bool apic_ipi_acked(void);

#if	APIC_TESTS

void apic_run_tests(void);

#else

static void __unused apic_run_tests(void) { }

#endif	/* APIC_TESTS */

#else

#define APIC_PHBASE	0xfee00000	/* Physical */
#define APIC_EOI	0xb0		/* End of Interrupt Register */

#endif /* !__ASSEMBLY__ */

#endif /* _APIC_H */
