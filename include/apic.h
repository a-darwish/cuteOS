#ifndef _APIC_H
#define _APIC_H

/*
 * Local APIC definitions
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

/*
 * Base addresses for the APIC register set
 */
#define APIC_PHBASE 0xfee00000		/* Physical */
#define APIC_VRBASE 0xffffffffffe00000	/* Virtual; mapped at head.S */

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
#define APIC_APR	0x90	/* Arbitration Priority Register */
#define APIC_PPR	0xa0	/* Processor Priority Register */
#define APIC_EOI	0xb0	/* End of Interrupt Register */
#define APIC_RRR	0xc0	/* Remote Read Register */
#define APIC_LDR	0xd0	/* Logical Desitination Register */
#define APIC_DFR	0xe0	/* Destination Format Register */
#define APIC_SPIV	0xf0	/* Spurious Interrupt Vector Register */
#define APIC_SPIV_ENABLE (1<<8) /* APIC Software Enable bit */

#define APIC_ESR	0x280   /* Error Status Register */
#define APIC_ICRL	0x300   /* Interrupt Command Register Low [31:0] */
#define APIC_ICRH	0x310   /* Interrupt Command Register High [63:32] */

#define APIC_LVTT	0x320   /* Timer LVT (Local Vector Table) Entry */
#define APIC_LVTTHER	0x330   /* Thermal LVT Entry */
#define APIC_LVTPC	0x340   /* Performance Counter LVT Entry */
#define APIC_LVT0	0x350	/* Local Interrupt 0 LVT Entry */
#define APIC_LVT1	0x360	/* Local Interrupt 1 LVT Entry */
#define APIC_LVTERR	0x370   /* Error LVT Entry */

/*
 * APIC registers accessors
 */

extern void *apic_baseaddr;

static inline void apic_write(uint32_t reg, uint32_t val)
{
	volatile uint32_t *addr = apic_baseaddr + reg;
	*addr = val;
}

static inline uint32_t apic_read(uint32_t reg)
{
	volatile uint32_t *addr = apic_baseaddr + reg;
	return *addr;
}

void apic_init(void);

#endif /* !__ASSEMBLY__ */
#endif /* _APIC_H */
