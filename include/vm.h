#ifndef _VM_H
#define _VM_H

/*
 * Kernel virtual memory
 *
 * Copyright (C) 2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <tests.h>

void vm_init(void);
void *vm_kmap(uintptr_t pstart, uint64_t len);

#if	VM_TESTS

void vm_run_tests(void);

#else /* !VM_TESTS */

static inline void vm_run_tests(void) { }

#endif /* VM_TESTS */

#endif /* _VM_H */
