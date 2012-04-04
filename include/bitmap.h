#ifndef _BITMAP_H
#define _BITMAP_H

#include <kernel.h>
#include <tests.h>

int64_t bitmap_first_set_bit(char *buf, uint len);
int64_t bitmap_first_zero_bit(char *buf, uint len);

void bitmap_set_bit(char *buf, uint bit, uint len);
void bitmap_clear_bit(char *buf, uint bit, uint len);

bool bitmap_bit_is_set(char *buf, uint bit, uint len);
bool bitmap_bit_is_clear(char *buf, uint bit, uint len);

#if BITMAP_TESTS
void bitmap_run_tests(void);
#else
static __unused void bitmap_run_tests(void) { }
#endif /* BITMAP_TESTS */

#endif	/* _BITMAP_H */
