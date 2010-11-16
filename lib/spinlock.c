/*
 * SMP spinlocks
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * Textbook spinlocks: a value of one indicates the lock is available.
 * The more negative the value of the lock, the more lock contention.
 *
 * The LOCK prefix insures that a read-modify-write operation on memory
 * is carried out atomically. While the LOCK# signal is asserted,
 * requests from other processors or bus agents for control of the bus
 * are blocked.
 *
 * If the area of memory being locked is cached in the processor and is
 * completely contained in a cache line, the CPU may not assert LOCK# on
 * the bus. Instead, it will modify the location internally and depend
 * on the "cache locking" feature of the cache coherency mechanism.
 *
 * During "cache locking", the cache coherency mechanism automatically
 * prevents two or more cores that have cached the same area of memory
 * from simultaneously modifying data in that area (cache line).
 */

#include <stdint.h>
#include <spinlock.h>
#include <idt.h>
#include <x86.h>

/*
 * Always try to acquire (decrement) the lock while LOCK# is asserted.
 * Should the decrement result be negative, busy loop till the lock is
 * marked by its owner as free (positive). Several cores might have
 * observed this free state, let each one try to reacquire (decrement)
 * the lock again under LOCK#: only one CPU will be able to see the
 * positive value and win. Others will re-enter the busy-loop state.
 *
 * Intel recommends the use of the 'pause' instruction in busy wait
 * loops: it serves as a hint to the CPU to avoid memory order
 * violations and to reduce power usage in the busy loop state. It's
 * an agnostic REP NOP for older cores.
 *
 * NOTE! To avoid deadlocks, do not use this in any code that may get
 * invoked in an irq context, see spin_lock_irqsave().
 */
void spin_lock(spinlock_t *lock)
{
	asm volatile (
		"1: \n"
		"lock decl %0;"		/* rflags */
		"jns 3f;"

		"2: \n"
		"pause;"
		"cmpl $0, %0;"		/* rflags */
		"jle 2b;"

		"jmp 1b;"

		"3: \n"
		: "+m"(*lock)
		:
		: "cc", "memory");
}

/*
 * Mark the lock as available
 */
void spin_unlock(spinlock_t *lock)
{
	asm volatile (
		"movl $1, %0;"
		: "=m"(*lock)
		:
		: "memory");
}

/*
 * Spinlocks for code which can be called in irq handlers.
 *
 * Using regular spinlocks, it's possible to interrupt the very core
 * holding the lock: the irq handler will busy-loop waiting for the
 * core to release the lock, but the core is already interrupted and
 * can't release the lock till the irq handler finishes [deadlock].
 *
 * Disabling local interrupts before holding the lock is enough: it's
 * ok if another core's interrupt handler tries to acquire the lock.
 *
 * FIXME: Performance: if irqs are enabled in %rflags, we should re-
 * enable them temporarily while spinning.
 */
union x86_rflags spin_lock_irqsave(spinlock_t *lock)
{
	union x86_rflags flags;

	flags = get_rflags();
	if (flags.irqs_enabled)
		local_irq_disable();

	spin_lock(lock);

	return flags;
}

/*
 * Restore irqs state after lock release.
 */
void spin_unlock_irqrestore(spinlock_t *lock, union x86_rflags flags)
{
	spin_unlock(lock);
	set_rflags(flags);
}
