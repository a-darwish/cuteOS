#ifndef _TSC_H
#define _TSC_H

/*
 * Read the 64-bit time-stamp counter
 *
 * The `A' constraint was tried, which  supposedly represets
 * "the a and d registers, as a pair". Unfortunately, my GCC
 * build contradicts documentation: emitted code ignored the
 * %rdx part of rdtsc's result completely!
 */
static inline uint64_t read_tsc(void)
{
	uint32_t low, high;

	asm volatile (
		"rdtsc;"
		: "=a" (low), "=d" (high));

	return ((uint64_t)high << 32) + low;
}

#endif /* TSC_H */
