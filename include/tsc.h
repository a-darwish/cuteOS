#ifndef _TSC_H
#define _TSC_H

/*
 * Read the 64-bit time-stamp counter
 */
static inline uint64_t read_tsc(void)
{
	uint32_t low, high;

	asm volatile (
		"rdtsc;"
		: "=a" (low), "=d" (high));

	return ((uint64_t)high << 32) + low;
}

#endif
