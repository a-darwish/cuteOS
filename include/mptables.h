#ifndef _MPTABLES_H
#define _MPTABLES_H

/*
 * Intel MultiProcessor Specification v1.4 tables
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * NOTE! MP strings are coded in ASCII, but are not NULL-terminated.
 */

#include <stdint.h>
#include <kernel.h>

/*
 * Dump the MP tables in case of a critical error
 */
#define MP_DEBUG	1

/*
 * Intel MP Floating Pointer structure
 *
 * ASCII signature serves as a search key for locating the
 * structure. Note the little-endian byte ordering.
 */

#define MPF_SIGNATURE ('_'<<(3 * 8) | 'P'<<(2 * 8) | 'M'<<(1 * 8) | '_')

struct mpf_struct {
	uint32_t signature;		/* "_MP_" */
	uint32_t conf_physaddr;		/* MP configuration table pointer */
	uint8_t length;			/* Structure length in 16-byte units */
	uint8_t version;		/* version1.1=0x01, version1.4=0x04 */
	uint8_t checksum;		/* Sum of structure's bytes */
	uint8_t feature1;		/* Default configuration used? */
	uint8_t feature2_reserved:7,	/* Reserved */
		imcr:1;			/* Obsolete PIC mode implemented? */
	uint8_t feature3;		/* Reserved */
	uint8_t feature4;		/* Reserved */
	uint8_t feature5;		/* Reserved */
} __packed;

/*
 * MP Configuration table header
 */

#define MPC_SIGNATURE ('P'<<(3 * 8) | 'M'<<(2 * 8) | 'C'<<(1 * 8) | 'P')

struct mpc_table {
	uint32_t signature;		/* "PCMP" */
	uint16_t length;		/* Base Table length + header */
	uint8_t version;		/* version1.1=0x01, version1.4=0x04 */
	uint8_t checksum;		/* mpf checksum */
	char oem[8];			/* OEM ID */
	char product[12];		/* Product ID/family */
	uint32_t oem_physaddr;		/* OEM table physical pointer, or 0 */
	uint16_t oem_size;		/* OEM table size in bytes, or 0 */
	uint16_t entries;		/* Num of table's entries */
	uint32_t lapic_base;		/* Obsolete LAPIC base - use MSRs */
	uint16_t ext_length;		/* Extended entries length, or 0 */
	uint8_t ext_checksum;		/* Extended entries checksum, or 0 */
	uint8_t reserved;
} __packed;

/*
 * MP Configuration table entries
 */

struct mpc_cpu {
	uint8_t entry;			/* Entry type (processor) */
	uint8_t lapic_id;		/* This processor's lapic ID */
	uint8_t lapic_ver;		/* This proecessor's lapic version */
	uint8_t enabled:1,		/* Set if this processor is usable */
		bsc:1,			/* Set for the bootstrap processor */
		flags_reserved:6;	/* Reserved */
	uint32_t signature;		/* Signature (stepping, model, family */
	uint32_t flags;			/* Flags as returned by CPUID */
	uint64_t reserved;		/* Reserved */
} __packed;

struct mpc_bus {
	uint8_t entry;			/* Entry type (bus) */
	uint8_t id;			/* Bus ID */
	char type[6];			/* Bus Type string */
} __packed;

struct mpc_ioapic {
	uint8_t entry;			/* Entry type (ioapic) */
	uint8_t id;			/* The ID of this I/O APIC */
	uint8_t version;		/* I/O APIC's version register */
	uint8_t enabled:1,		/* If zero, this I/O APIC is unusable */
		flags_reserved:7;	/* Reserved */
	uint32_t base;			/* This I/O APIC base address */
} __packed;

struct mpc_irq {
	uint8_t entry;			/* Entry type (I/O interrupt entry) */
	uint8_t type;			/* Interrupt type */
	uint16_t polarity:2,		/* Polarity of APIC I/O input signal */
		trigger:2,		/* Trigger mode */
		reserved:12;		/* Reserved */
	uint8_t src_busid;		/* Interrupt source bus */
	uint8_t src_busirq;		/* Source bus irq */
	uint8_t dst_ioapicid;		/* Destination I/O APIC ID */
	uint8_t dst_ioapicpin;		/* Destination I/O APIC INTINn pin */
} __packed;

struct mpc_linterrupt {
	uint8_t entry;			/* Entry type (local interrupt entry) */
	uint8_t type;			/* Interrupt type */
	uint16_t polarity:2,		/* Polarity of APIC I/O input signal */
		trigger:2,		/* Trigger mode */
		reserved:12;		/* Reserved */
	uint8_t src_busid;		/* Interrupt source bus */
	uint8_t src_busirq;		/* Source bus irq */
	uint8_t dst_lapic;		/* Destination local APIC ID */
	uint8_t dst_lapicpin;		/* Destination local APIC LINTINn pin */
} __packed;

#define MPC_ENTRY_MAX_LEN		(sizeof(struct mpc_cpu))

/* Compile-time MP tables sizes sanity checks */
static inline void mptables_check(void) {
	compiler_assert(sizeof(struct mpf_struct) == 4 * 4);
	compiler_assert(sizeof(struct mpc_table) == 11 * 4);
	compiler_assert(sizeof(struct mpc_cpu) == 5 * 4);
	compiler_assert(sizeof(struct mpc_bus) == 2 * 4);
	compiler_assert(sizeof(struct mpc_ioapic) == 2 * 4);
	compiler_assert(sizeof(struct mpc_irq) == 2 * 4);
	compiler_assert(sizeof(struct mpc_linterrupt) == 2 * 4);

	compiler_assert(MPC_ENTRY_MAX_LEN >= sizeof(struct mpc_cpu));
	compiler_assert(MPC_ENTRY_MAX_LEN >= sizeof(struct mpc_bus));
	compiler_assert(MPC_ENTRY_MAX_LEN >= sizeof(struct mpc_ioapic));
	compiler_assert(MPC_ENTRY_MAX_LEN >= sizeof(struct mpc_irq));
	compiler_assert(MPC_ENTRY_MAX_LEN >= sizeof(struct mpc_linterrupt));
}

/*
 * Variable entries types
 */
enum mp_entry {
	MP_PROCESSOR = 0,
	MP_BUS,
	MP_IOAPIC,
	MP_IOINTERRUPT,
	MP_LINTERRUPT,
};

enum mp_irqtype {
	MP_INT = 0,			/* IOAPIC provided vector */
	MP_NMI,
	MP_SMI,
	MP_ExtINT,			/* 8259A provided vector */
};

/*
 * Parsed MP tables data exported to the rest of
 * of the system
 */

extern int mp_isa_busid;

extern int nr_mpcirqs;
extern struct mpc_irq mp_irqs[];

void mptables_init(void);

/*
 * Dump tables in case of critical errors
 */

#ifndef MP_DEBUG

static void __unused mpc_dump(struct mpc_table *mpc) { }

#endif /* MP_DEBUG */

#endif /* _MPTABLES_H */
