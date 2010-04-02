#ifndef _VGA_H
#define _VGA_H

/*
 * VGA Colors
 *
 * Copyright (C) 2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * Reference: 'System BIOS for IBM PCs, compatibles, and EISA computers',
 * second edition, Phoenix press
 */

/*
 * VGA color attribute is an 8 bit value, low 4 bits sets
 * foreground color, while the high 4 sets the background.
 */
#define VGA_COLOR(bg, fg)	(((bg) << 4) | (fg))

#define VGA_BLACK		0x0
#define VGA_BLUE		0x1
#define VGA_GREEN		0x2
#define VGA_CYAN		0x3
#define VGA_RED			0x4
#define VGA_MAGNETA		0x5
#define VGA_BROWN		0x6
#define VGA_LIGHT_GRAY		0x7
#define VGA_GRAY		0x8
#define VGA_LIGHT_BLUE		0x9
#define VGA_LIGHT_GREEN		0xa
#define VGA_LIGHT_CYAN		0xb
#define VGA_LIGHT_RED		0xc
#define VGA_LIGHT_MAGNETA	0xd
#define VGA_YELLOW		0xe
#define VGA_WHITE		0xf

/* Max color value, 4 bytes */
#define VGA_COLOR_MAX		0xf

#endif /* _VGA_H */
