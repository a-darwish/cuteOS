#ifndef IDT_H
#define IDT_H

/*
 * IDT table descriptor definitions and accessor methods
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#define IDT_GATES 256
#define EXCEPTION_GATES 32

#define GATE_INTERRUPT 0xe
#define GATE_TRAP 0xf

#ifndef __ASSEMBLY__

#include <stdint.h>
#include <segment.h>
#include <paging.h>

struct idt_gate {
	uint16_t offset_low;
	uint16_t selector;
	unsigned ist: 3, reserved0: 5, type: 5, dpl: 2, p: 1;
	uint16_t offset_middle;
	uint32_t offset_high;
	uint32_t reserved1;
} __packed;

struct idt_descriptor {
	uint16_t limit;
	uint64_t base;
} __packed;

/*
 * Symbols from head.S
 *
 * Note that 'extern <type> *SYMBOL;' won't work since it'd mean we
 * don't point to meaningful data yet, which isn't the case. We use
 * 'SYMBOL[]' since in a declaration, [] just leaves it open to the
 * number of base type objects which are present, not *where* they are.
 * SYMBOL[n] just adds more static-time safety; SYMBOL[n][size] lets
 * the compiler automatically calculate an entry index for us.
 * @IDT_STUB_SIZE: exception stub _code_ size.
 */
extern const struct idt_descriptor idtdesc[];
extern struct idt_gate idt[IDT_GATES];
#define IDT_STUB_SIZE 12
extern const char default_idt_stubs[EXCEPTION_GATES][IDT_STUB_SIZE];

static inline void pack_idt_gate(struct idt_gate *gate, uint8_t type, void *addr)
{
	gate->offset_low = (uintptr_t)addr & 0xffff;
	gate->selector = KERNEL_CS;
	gate->ist = 0;
	gate->reserved0 = 0;
	gate->type = type;
	gate->dpl = 0;
	gate->p = 1;
	gate->offset_middle = ((uintptr_t)addr >> 16) & 0xffff;
	gate->offset_high = (uintptr_t)addr >> 32;
	gate->reserved1 = 0;
}

static inline void write_idt_gate(struct idt_gate *gate, struct idt_gate *idt,
				  int offset)
{
	idt[offset] = *gate;
}

static inline void set_intr_gate(unsigned int n, void *addr)
{
	struct idt_gate gate;
	pack_idt_gate(&gate, GATE_INTERRUPT, addr);
	write_idt_gate(&gate, idt, n);
}

static inline void load_idt(const struct idt_descriptor *idt_desc)
{
	asm volatile("lidt %0"
		     :
		     :"m"(*idt_desc));
}

static inline struct idt_descriptor get_idt(void)
{
	struct idt_descriptor idt_desc;

	asm volatile("sidt %0"
		     :"=m"(idt_desc)
		     :);

	return idt_desc;
}

static inline void local_irq_disable(void)
{
	asm volatile ("cli"
		      ::
		      :"memory");
}

static inline void local_irq_enable(void)
{
	asm volatile ("sti"
		      ::
		      :"memory");
}

#endif /* !__ASSEMBLY__ */

#endif
