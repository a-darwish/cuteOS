#ifndef _SPINLOCK_H
#define _SPINLOCK_H

/*
 * SMP spinlocks
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <stdint.h>
#include <x86.h>

typedef int32_t spinlock_t;

#define SPIN_UNLOCKED()		((spinlock_t) 1)

static inline void spin_init(spinlock_t *lock)
{
	*lock = 1;
}

void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);

union x86_rflags spin_lock_irqsave(spinlock_t *lock);
void spin_unlock_irqrestore(spinlock_t *lock, union x86_rflags flags);

#endif /* _SPINLOCK_H */
