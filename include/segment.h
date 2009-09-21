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
#define VIRTUAL(address)  (address + VIRTUAL_START)
#else
/* We need a char* cast in case someone gave us an int or long
 * pointer that can mess up the whole summation/transformation */
#define VIRTUAL(address)  ((char *)address + VIRTUAL_START)
#endif

#endif

