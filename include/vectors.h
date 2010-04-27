#ifndef _VECTORS_H
#define _VECTORS_H

/*
 * IRQ vectors assignment to bootstrap CPU
 *
 * Copyright (C) 2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * We manually assign vector numbers to different IRQ sources till we
 * have a dynamic IRQ model.
 *
 * NOTE! Don't use reserved vector numbers <= 0x1f.
 */

#define TICKS_IRQ_VECTOR	0x20
#define KEYBOARD_IRQ_VECTOR	0x21
#define PIT_TESTS_VECTOR	0x22
#define APIC_TESTS_VECTOR	0x23

/*
 * Placeholder values till we know how will we
 * setup those apic vectors
 */
#define APIC_TIMER_VECTOR	0
#define APIC_THERMAL_VECTOR	0
#define APIC_PERFC_VECTOR	0
#define APIC_LINT0_VECTOR	0
#define APIC_LINT1_VECTOR	0

#endif /* _VECTORS_H */
