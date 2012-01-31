/*
 * Multiple-Processor (MP) Initialization
 *
 * Copyright (C) 2009-2010 Ahmed S. Darwish <darwish.07@gmail.com>
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
#define SMPBOOT_IDTR		(SMPBOOT_CR3 + 8)
#define SMPBOOT_IDTR_LIMIT	(SMPBOOT_IDTR)
#define SMPBOOT_IDTR_BASE	(SMPBOOT_IDTR_LIMIT + 2)
#define SMPBOOT_GDTR		(SMPBOOT_IDTR + 10)
#define SMPBOOT_GDTR_LIMIT	(SMPBOOT_GDTR)
#define SMPBOOT_GDTR_BASE	(SMPBOOT_GDTR_LIMIT + 2)
#define SMPBOOT_STACK_PTR	(SMPBOOT_GDTR + 10)
#define SMPBOOT_PERCPU_PTR	(SMPBOOT_STACK_PTR + 8)

#define SMPBOOT_PARAMS_END	(SMPBOOT_PARAMS + SMPBOOT_PERCPU_PTR + 8)
#define SMPBOOT_PARAMS_SIZE	(SMPBOOT_PARAMS_END - SMPBOOT_PARAMS)

#ifndef __ASSEMBLY__

#include <kernel.h>
#include <segment.h>
#include <idt.h>

#define TRAMPOLINE_START	VIRTUAL(SMPBOOT_START)
#define TRAMPOLINE_PARAMS	VIRTUAL(SMPBOOT_PARAMS)

void __no_return secondary_start(void);	/* Silence-out GCC */
int smpboot_get_nr_alive_cpus(void);
void smpboot_init(void);

#endif /* !__ASSEMBLY__ */

#endif /* _SMPBOOT_H */
