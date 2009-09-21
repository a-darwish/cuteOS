#ifndef KERNEL_H
#define KERNEL_H

/*
 * Common methods and definitions
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

/*
 * Semi type-safe min macro using GNU extensions
 */
#define min(x, y) ({		    \
        typeof(x) _m1 = (x);	    \
	typeof(y) _m2 = (y);	    \
	(void) (&_m1 == &_m2);	    \
	_m1 < _m2 ? _m1 : _m2; })

/*
 * Main kernel print method
 */
void printk(const char *fmt, ...);

#endif
