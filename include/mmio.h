#ifndef _MMIO_H
#define _MMIO_H

/*
 * Memory-mapped I/O registers accessors
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * Originally, the 'volatile' keyword was meant for memory-mapped I/O,
 * but this keyword is one of the very vague parts of ANSI C, and it
 * seems no compiler is able to get them completely right.[1][2]
 *
 * We force our own interpretation of volatile, where we explicitly use
 * compiler barriers which will cause GCC not to keep memory values
 * cached in registers across the assembler instruction and not optimize
 * stores or loads to that memory. We also use the mov instruction under
 * "asm volatile" where gcc won't optimize it away.
 *
 * GCC manual notes that "even a volatile asm instruction can be moved
 * relative to other code, including across jump instructions". Thus, we
 * explicitly inform GCC about the memory addresses read from and written
 * to (in the constraints), so it does not reorder data-dependent ops.
 *
 * Constraints are volatile-casted to assert safety of passing a pointer
 * to volatile memory in those accessors, and as a second marker to GCC
 * not to let it cache the result in regs (a conformant compiler does not
 * cache a volatile memory value in regs). For other interesting details
 * on GCC optimizations, see our percpu_get()/set() comments @ 'percpu.h'.
 *
 * Finally, using those accessors won't clutter the top codebase with
 * volatiles, and will make it explicit we're accessing MMIO.
 *
 * [1] "Volatiles Are Miscompiled, and What to Do about It", Proceedings
 * of the Eighth ACM and IEEE International Conference.
 * [2] Attached Linux kernel's "volatile-considered-harmful.txt" with
 * various notes from core developers over LKML.
 */

#include <stdint.h>

#define __read__(type, addr) ({					\
	type val;						\
	asm volatile ("mov %[addr], %[val];"			\
		      : [val] "=r"(val)				\
		      : [addr] "m"(*(const volatile type *)addr)\
		      : "memory");				\
	val;							\
})
static inline uint8_t  readb(const volatile void *addr) {
	return __read__(uint8_t,  addr);
}
static inline uint16_t readw(const volatile void *addr) {
	return __read__(uint16_t, addr);
}
static inline uint32_t readl(const volatile void *addr) {
	return __read__(uint32_t, addr);
}
static inline uint64_t readq(const volatile void *addr) {
	return __read__(uint64_t, addr);
}
#undef __read__

#define __write__(type, val, addr) ({				\
	asm volatile ("mov %[val], %[addr];"			\
		      : [addr] "=m"(*(volatile type *)addr)	\
		      : [val] "r"(val)				\
		      : "memory");				\
})
static inline void writeb(uint8_t val, volatile void *addr) {
return __write__(uint8_t, val, addr);
}
static inline void writew(uint16_t val, volatile void *addr) {
	return __write__(uint16_t, val, addr);
}
static inline void writel(uint32_t val, volatile void *addr) {
	return __write__(uint32_t, val, addr);
}
static inline void writeq(uint64_t val, volatile void *addr) {
	return __write__(uint64_t, val, addr);
}
#undef __write__

#endif /* __MMIO_H */
