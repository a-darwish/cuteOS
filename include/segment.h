#ifndef SEGMENT_H
#define SEGMENT_H

/*
 * Segmentation definitions; minimal by the nature of x86-64
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#define KERNEL_CS 0x08
#define KERNEL_DS 0x10

#define VIRTUAL_START     (0xffffffff80000000)

#ifdef __ASSEMBLY__

#define VIRTUAL(address)  ((address) + VIRTUAL_START)

#else

/* We need a char* cast in case someone gave us an int or long
 * pointer that can mess up the whole summation/transformation */
#define VIRTUAL(address)  ((void *)((char *)(address) + VIRTUAL_START))
#define PHYS(address)     ((void *)((char *)(address) - VIRTUAL_START))

/* Maximum mapped physical address. We should get rid of our
 * ad-hoc mappings soon */
#define PHYS_MAX	0x30000000

#endif /* !__ASSEMBLY__ */

#endif

