#ifndef _ATOMIC_H
#define _ATOMIC_H

#include <stdint.h>
#include <tests.h>

uint8_t atomic_bit_test_and_set(uint32_t *val);

#if    ATOMIC_TESTS
void atomic_run_tests(void);
#else
static void __unused atomic_run_tests(void) { }
#endif

#endif /* _ATOMIC_H */
