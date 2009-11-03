#ifndef _SMPBOOT_H
#define _SMPBOOT_H

/*
 * System-wide logical CPUs descriptors. Data is gathered
 * from either MP-tables, ACPI MADT tables, or self probing.
 */

#define CPUS_MAX	32

struct cpu_desc {
	int apic_id;			/* local APIC ID */
	int bsc;			/* bootstrap core? */
};

extern int nr_cpus;			/* (MP, ACPI) usable cores count */
extern struct cpu_desc cpu_descs[CPUS_MAX];

void smpboot_init(void);

#endif /* _SMPBOOT_H */
