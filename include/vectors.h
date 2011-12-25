#ifndef _VECTORS_H
#define _VECTORS_H

/*
 * IRQ vectors assignment to bootstrap CPU
 *
 * Copyright (C) 2010-2011 Ahmed S. Darwish <darwish.07@gmail.com>
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

// Priority 0xf - highest priority
#define TICKS_IRQ_VECTOR	0xf0
#define APIC_SPURIOUS_VECTOR	0xff	// Intel-defined default

// Priority 0x3 - APIC vectors
#define APIC_TIMER_VECTOR	0x30
#define APIC_THERMAL_VECTOR	0x31
#define APIC_PERFC_VECTOR	0x32
#define APIC_LINT0_VECTOR	0x33
#define APIC_LINT1_VECTOR	0x34

// Priority 0x2 - lowest possible priority
#define KEYBOARD_IRQ_VECTOR	0x20
#define PIT_TESTS_VECTOR	0x21
#define APIC_TESTS_VECTOR	0x22

// priority 0x1 - (System reserved)

#endif /* _VECTORS_H */
