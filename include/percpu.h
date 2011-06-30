#ifndef _PERCPU_H
#define _PERCPU_H

/*
 * Per-CPU bookkeeping
 *
 * Copyright (C) 2011 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * The fastest-way possible to access a per-CPU region is to _permanently_
 * assign one of the CPU internal registers with the virtual address of its
 * context area.
 *
 * Another method is to ask the APIC about our CPU ID, and access an array
 * using that ID as an index. That method works, but we'll need to disable
 * preemption while doing it. The APIC “registers” also have higher access
 * latency than CPU general-purpose ones by virtue of being memory mapped.
 *
 * Digging in the preliminary 2001 AMD64 architecture document, it seems
 * that AMD designers specifically kept the %fs and %gs segment registers
 * since the NT kernel used these for locating the “TEB: thread-evironment
 * block” and the “PCR: processor-control-region”. Thank you AMD!
 *
 * So here we have a free lunch from the hardware. As in NT, we'll accept
 * it and use %gs for locating the per-CPU area of each processor.
 *
 * NOTE: If the CPU was missing such a register, we would've done like VMS
 * on the VAX architecture and save a pointer to the per-CPU area in the
 * bottom of each ON_CPU thread stack. Such pointer would be reset before
 * every context switch, and easily located by masking the stack pointer.
 *
 * For further details, check:
 *	- “VMS Symmetric Multiprocessing”, Digital Technical Journal,vol.1
 *	  no.7, pp. 57-73, August 1988
 *	- “DEC OSF/1 Version 3.0 Symmetric Multiprocessing Implementation”,
 *	  Digital Technical Journal, vol. 6 no. 3, pp. 29-43, Summer 1994
 *	- “Preliminary Information: AMD 64-bit Technology” Publication no.
 *	  24108, Rev:C, January 2001
 *
 * For false-sharing and cache-line details, check:
 *	- “Intel 64 and IA-32 Optimization Reference Manual”, Chapter 8,
 *	   Multicore and Hyper-Threading Technology
 */

#ifndef __ASSEMBLY__

#include <kernel.h>
#include <stdint.h>
#include <apic.h>
#include <x86.h>
#include <proc.h>
#include <tests.h>

#define CPUS_MAX		64	/* Arbitrary */

/*
 * “Beware of false sharing within a cache line (64 bytes on Intel Pentium
 * 4, Intel Xeon, Pentium M, Intel Core Duo processors), and within a
 * sector (128 bytes on Pentium 4 and Intel Xeon processors).” --Intel
 */
#define CACHE_LINE_SIZE		128

/*
 * The Per-CPU Data Area
 *
 * To prevent “false-sharing” of CPU cache lines, we put this area within
 * unique 64-byte cache-line size boundary, and 128-byte sector boundary.
 *
 * This has a high impact: when one thread modifies a shared variable,
 * the “dirty” cache line must be written out to memory and updated for
 * each physical CPU sharing the bus (a cache snoop). Afterwards, data is
 * fetched into each target processor 128 bytes at a time, causing the
 * previously cached data of _each_ target CPU to be evicted.
 *
 * Even in the case of two logical CPUs that reside in one physical core
 * (where the cache lines are shared), sharing the data within one cache
 * line will affect perfomrance due to the x86 memory ordering model.
 *
 * Note-1! A blance must be set between these sparse requirements and the
 * size of the working set, which should be minimized.
 *
 * NOTE-2! '__current' is hardcoded to ALWAYS be the first element.
 */
struct percpu {
	struct proc *__current;		/* Descriptor of the ON_CPU thread */
	int apic_id;			/* Local APIC ID */
#if PERCPU_TESTS
	uint64_t x64;			/* A 64-bit value (testing) */
	uint32_t x32;			/* A 32-bit value (testing) */
	uint16_t x16;			/* A 16-bit value (testing) */
	uint8_t x8;			/* An 8-bit value (testing) */
#endif
} __aligned(CACHE_LINE_SIZE);

/*
 * To make '__current' available to early boot code, it's statically
 * allocated in the first slot. Thus, slot 0 is reserved for the BSC.
 */
extern struct percpu cpus[CPUS_MAX];
#define BOOTSTRAP_PERCPU_AREA	((uintptr_t)&cpus[0])

/*
 * Per-CPU data accessors
 *
 * There's a long story behind these small number of lines!
 *
 * First, all the operations are done in one x86 op by design. This makes
 * us avoid disabling and re-enabling kernel preemption in each macro.
 * For two-ops-or-more per-CPU accessors, disabling preemption is needed:
 *	a) We might get scheduled to another CPU in the middle
 *	b) The preempting path might access the same per-CPU variable,
 *	   leading to classical race conditions.
 *
 * Second, we could've got the per-CPU variable value by just passing
 * its offset from the per-CPU area base %gs:
 *	percpu_get(var) {
 *		asm ("mov %%gs:%1, %0"
 *		     : "=r" (res),
 *		     : "i" (offsetof(struct percpu, var)));
 *		return res;
 *	}
 * but this doesn't inform GCC about the per-CPU variable area accessed,
 * loosing explicit dependency information between per-CPU accessors. It
 * also misleads GCC into thinking we have no run-time input at all.
 *
 * For example, compiling below code at -O3:
 *
 *	y = percpu_get(A);	// [1]
 *	percpu_set(A, 0xcafe);	// [2] same variable
 *	z = percpu_get(A);	// [3] same variable
 *
 * lets GCC store ‘A’s value below to _both_ y and z before setting it
 * to 0xcafe, violating the order needed for correctness: ‘[3]’ reads
 * from a memory area written by ‘[2]’, thus ‘[3]’ depends on ‘[2]’.
 *
 * Optimizing compilers need to know such run-time depdendencies between
 * ops to translate code while maintaining correctness and sequentiality.
 * An illusion of sequentiality can be maintained by "only preserving the
 * sequential order among memory operations to the same location." i.e.
 * maintaing program order for _dependent_ per-CPU accessors in our case.
 *
 * Unfortunately though, we cannot precisely tell GCC about the per-CPU
 * area accessed since standard C has no support for segmented memory. As
 * an alternative, we try to overload other addresses with that purpose.
 * The set of addresses formed by the construct
 *
 *	“((struct percpu *) NULL)->PerCpuVariableName”
 *
 * fits our goal nicely: they are unique for each kind of per-CPU variable,
 * and they correctly calculate relative offsets from the %gs segment base.
 * We do not care about their validity, they just act as markers informing
 * GCC about the implicit data dependencies between pCPU reads and writes.
 *
 * Third, we do _not_ want compilers to cache the following sequence:
 *
 *	y = percpu_get(A);
 *	z = percpu_get(A);
 *	w = percpu_get(A);
 *
 * in registers (%eax=*A, y=z=w=%eax) since we may move to a different CPU
 * in the middle. A simple solution was to to mark the memory we read from
 * and write to as “volatile”.
 *
 * NOTE-1! As shown in the second point, we in fact mark different memory
 * than the one accessed as volatile. We don't care since we've chosen a
 * unique address representing each per-CPU variable defined.
 *
 * NOTE_2! Casting to a volatile-qualified type as in
 *
 *	“(volatile __percpu_type(var))__percpu(var))”
 *
 * won't work. As said by the C standard: “Preceding an expression by a
 * parenthesized type name converts the _value_ of the expression to the
 * named type.” .. “A cast does not yield an lvalue. Thus, a cast to a
 * qualified type has the same effect as a cast to the unqualified
 * version of the type.”
 *
 * So such volatile-qualified cast is meaningless: object value (rvalue)
 * will get extracted using non-volatile semantics then such rvalue will
 * get aimlessly casted to a volatile. Check section 14 of our notes,
 * ‘On the C volatile type qualifier’ for further details, especially
 * the section appendix.
 *
 * Using above tricks, we do not suffer the performance of a full compiler
 * barrier, which would unnecesserily evict unrelated register-cached data.
 * This has high-impact since per-CPU variables are used allover the place.
 *
 * NOTE-3! Know what you're really doing if you fsck with any of this.
 */

#define __percpu(var)		(((struct percpu *) NULL)->var)
#define __percpu_offset(var)	(&__percpu(var))
#define __percpu_type(var)	typeof(__percpu(var))
#define __percpu_marker(var)	((volatile __percpu_type(var) *)&__percpu(var))

#define percpu_get(var)							\
({									\
	__percpu_type(var) res;						\
									\
	asm ("mov %%gs:%1, %0"						\
	     : "=r" (res)						\
	     : "m" (*__percpu_marker(var)));				\
									\
	res;								\
})

/*
 * GCC heuristics for deciding the instruction size will fail us here.
 * They depend on the size of the register input instead of the whole
 * size of the memory area to be modified. Roll our-own implementation!
 *
 * Such hueristics worked for the 'get' case cause there, the size of
 * the output register always equals the size of the whole memory area
 * being read.
 */

#define __percpu_set(suffix, var, val)					\
({									\
	asm ("mov" suffix " %1, %%gs:%0"				\
	     : "=m" (*__percpu_marker(var))				\
	     : "ir" (val));						\
})

#define percpu_set(var, val)						\
({									\
	switch (sizeof(__percpu_type(var))) {				\
	case 1: __percpu_set("b", var, val); break;			\
	case 2: __percpu_set("w", var, val); break;			\
	case 4: __percpu_set("l", var, val); break;			\
	case 8: __percpu_set("q", var, val); break;			\
	default: compiler_assert(false);				\
	}								\
})

/*
 * A thread descriptor address does not change for the lifetime of that
 * thread, even if it moved to another CPU. Thus, inform GCC to _cache_
 * below result in registers as much as possible!
 */
static __always_inline __pure_const struct proc *percpu_current_proc(void)
{
	struct proc *curproc;

	asm ("mov %%gs:0, %0" : "=r" (curproc));

	return curproc;
}

/*
 * Descriptor of thread representing ‘self’, applicable anywhere.
 */
#define current		(percpu_current_proc())

#else

#define current		%gs:0x0

#endif /* __ASSEMBLY */

#endif /* _PERCPU_H */
