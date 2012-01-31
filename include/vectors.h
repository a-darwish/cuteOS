#ifndef _VECTORS_H
#define _VECTORS_H

/*
 * IRQ vectors assignment to bootstrap CPU
 *
 * Copyright (C) 2010-2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * We manually assign vector numbers to different IRQ sources till we
 * have a dynamic IRQ model.
 *
 * NOTE! The 32 vectors <= 0x1f are reserved for architecture-defined
 * exceptions and interrupts; don't use them.
 *
 * NOTE! Local & IO APICs only support vectors in range [0x10 -> 0xff].
 * "When an interrupt vector in  the range 0 to 15 is sent or received
 * through the local APIC, the APIC indicates an illegal vector in its
 * Error Status Register. Although vectors [0x10 -> 0x1f] are reserved,
 * the local APIC does not treat them as illegeal."  --Intel
 *
 * Several interrupts might be pending on a CPU instruction boundary;
 * APIC prioritize IRQs based on their IOAPIC-setup vector numbers:
 *
 *	IRQ priority = vector number / 0x10 (highest 4 bits)
 *
 * where the [0x0 -> 0xff] possible vector values lead to:
 *
 *	0x100 / 0x10 = 0x10        (16) IRQ priority classes
 *
 * with `1' being the lowest priority, and `15' being the highest.
 */

// Priority 0xf - Highest priority
#define TICKS_IRQ_VECTOR	0xf0
#define HALT_CPU_IPI_VECTOR	0xf1
#define APIC_SPURIOUS_VECTOR	0xff	// Intel-defined default

// Priority 0x4 - APIC vectors
#define APIC_TIMER_VECTOR	0x40
#define APIC_THERMAL_VECTOR	0x41
#define APIC_PERFC_VECTOR	0x42
#define APIC_LINT0_VECTOR	0x43
#define APIC_LINT1_VECTOR	0x44

// Priority 0x3 - External interrupts
#define KEYBOARD_IRQ_VECTOR	0x30
#define PIT_TESTS_VECTOR	0x31
#define APIC_TESTS_VECTOR	0x32

// Priority 0x2 - Lowest possible priority (PIC spurious IRQs)
#define PIC_IRQ0_VECTOR		0x20
#define PIC_IRQ7_VECTOR		(PIC_IRQ0_VECTOR + 7)
#define PIC_IRQ8_VECTOR		0x28
#define PIC_IRQ15_VECTOR	(PIC_IRQ8_VECTOR + 7)

// priority 0x1 - (System reserved)

#endif /* _VECTORS_H */
