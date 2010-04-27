#ifndef _PIT_H
#define _PIT_H

#include <stdint.h>
#include <tests.h>

void pit_mdelay(int ms);
void pit_oneshot(int ms);
void pit_monotonic(int ms);

/*
 * Test cases
 */

#if	PIT_TESTS

void pit_run_tests(void);

#else

static void __unused pit_run_tests(void) { }

#endif	/* PIT_TESTS */

#endif /* _PIT_H */
