/*
 * Multiple-Processor (MP) Initialization
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#ifndef _SMPBOOT_H
#define _SMPBOOT_H


#define SMPBOOT_START		0x10000	/* Trampoline start; 4k-aligned */

/*
 * AP cores parameters base and their offsets. To be used
 * in trampoline assembly code.
 */

#define SMPBOOT_PARAMS		0x20000	/* AP parameters base */

#define SMPBOOT_CR3		(0)
#define SMPBOOT_IDT		(SMPBOOT_CR3 + 8)
#define SMPBOOT_IDT_LIMIT	(SMPBOOT_IDT)
#define SMPBOOT_IDT_BASE	(SMPBOOT_IDT_LIMIT + 2)
#define SMPBOOT_GDT		(SMPBOOT_IDT + 10)
#define SMPBOOT_GDT_LIMIT	(SMPBOOT_GDT)
#define SMPBOOT_GDT_BASE	(SMPBOOT_GDT_LIMIT + 2)

#define SMPBOOT_PARAMS_END	(SMPBOOT_PARAMS + SMPBOOT_GDT_BASE + 8)

#ifndef __ASSEMBLY__

#include <kernel.h>
#include <segment.h>
#include <idt.h>

/* Number of cpu cores started so far */
extern int nr_alive_cpus;

/*
 * System-wide logical CPUs descriptors. Data is gathered
 * from either MP-tables, ACPI MADT tables, or self probing.
 */

/* MP or ACPI reported usable cores count. The BIOS knows if
 * a core is usable by checking its Builtin-self-test (BIST)
 * result in the %rax register after RESET# */
extern int nr_cpus;

struct cpu_desc {
	int apic_id;			/* local APIC ID */
	int bsc;			/* bootstrap core? */
};

#define CPUS_MAX	32
extern struct cpu_desc cpu_descs[CPUS_MAX];

/*
 * Parameters to be sent to other AP cores.
 */

struct smpboot_params {
	uintptr_t cr3;
	struct idt_descriptor idt_desc;
	struct gdt_descriptor gdt_desc;
} __packed;

/* Compile-time validation of parameters offsets sent to
 * assembly trampoline code */
static inline void smpboot_params_validate_offsets(void)
{
	assert(SMPBOOT_CR3 == offsetof(struct smpboot_params, cr3));

	assert(SMPBOOT_IDT == offsetof(struct smpboot_params, idt_desc));

	assert(SMPBOOT_IDT_LIMIT == offsetof(struct smpboot_params, idt_desc) +
	       offsetof(struct idt_descriptor, limit));

	assert(SMPBOOT_IDT_BASE == offsetof(struct smpboot_params, idt_desc) +
	       offsetof(struct idt_descriptor, base));

	assert(SMPBOOT_GDT == offsetof(struct smpboot_params, gdt_desc));

	assert(SMPBOOT_GDT_LIMIT == offsetof(struct smpboot_params, gdt_desc) +
	       offsetof(struct gdt_descriptor, limit));

	assert(SMPBOOT_GDT_BASE == offsetof(struct smpboot_params, gdt_desc) +
	       offsetof(struct gdt_descriptor, base));

	assert((SMPBOOT_PARAMS_END - SMPBOOT_PARAMS) ==
	       sizeof(struct smpboot_params));
}

void smpboot_init(void);

#endif /* !__ASSEMBLY__ */

#endif /* _SMPBOOT_H */
