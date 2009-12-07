#ifndef _PIT_H
#define _PIT_H

#include <stdint.h>

void pit_mdelay(int ms);
uint64_t pit_calibrate_cpu(int repeat);

#endif /* _PIT_H */
