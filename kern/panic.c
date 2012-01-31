/*
 * Panic("msg"): to be called on unresolvable fatal errors!
 *
 * Copyright (C) 2009, 2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <apic.h>
#include <idt.h>
#include <vectors.h>
#include <spinlock.h>
#include <percpu.h>
#include <smpboot.h>

static char buf[256];
static spinlock_t panic_lock = SPIN_UNLOCKED();

/*
 * Quickly disable system interrupts upon entrance! Now the
 * kernel is in an inconsistent state, just gracefully stop
 * the machine and halt :-(
 *
 * An interesting case faced from not disabling interrupts
 * early on (disabling them at the function end instead) was
 * having other threads getting scheduled between printk()
 * and disabling interrupts, scrolling-away the caller panic
 * message and losing information FOREVER.
 */
void __no_return panic(const char *fmt, ...)
{
	va_list args;
	int n;

	/* NOTE! Do not put anything above this */
	local_irq_disable();

	/*
	 * NOTE! Manually assure that all the functions called
	 * below are void of any asserts or panics.
	 */

	/* Avoid concurrent panic()s: first call holds the most
	 * important facts; the rest are usually side-effects. */
	if (!spin_trylock(&panic_lock))
		goto halt;

	/* If other cores are alive, send them a fixed IPI, which
	 * intentionally avoids interrupting cores with IF=0 till
	 * they re-accept interrupts. Why?
	 *
	 * An interrupted critical region may  deadlock our panic
	 * code if we tried to  acquire the same lock.  The other
	 * cores may not also be in  long-mode before they enable
	 * interrupts (e.g. in the 16-bit SMP trampoline step.)
	 *
	 * IPIs are sent only if more than one core is alive:  we
	 * might be on so early a stage that our APIC registers
	 * are not yet memory mapped, leading to memory faults if
	 * locally accessed!
	 *
	 * If destination CPUs were alive but have not yet inited
	 * their local APICs, they will not be able to catch this
	 * IPI and will normally continue execution.  Beware.
	 */
	if (smpboot_get_nr_alive_cpus() > 1)
		apic_broadcast_ipi(APIC_DELMOD_FIXED, HALT_CPU_IPI_VECTOR);

	va_start(args, fmt);
	n = vsnprintf(buf, sizeof(buf) - 1, fmt, args);
	va_end(args);

	buf[n] = 0;
	printk("\nCPU#%d-PANIC: %s", percpu_get(apic_id), buf);

	/* Since the  other cores are stopped only after they re-
	 * accept interrupts, they may print on-screen and scroll
	 * away our message.  Acquire all screen locks, forever. */
	printk_bust_all_locks();

halt:
	halt();
}
