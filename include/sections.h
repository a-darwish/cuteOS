#ifndef SECTIONS_H
#define SECTIONS_H

/*
 * Section boundaries symbols
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

/*
 * ELF section boundaries are provided by the kernel
 * linker script kernel.ld. We could've declared them as
 * char -- not char[] -- and use the symbols as &symbol,
 * but the current method is more convenient.
 */

extern char __text_start[];
extern char __text_end[];

extern char __data_start[];
extern char __data_end[];

extern char __bss_start[];
extern char __bss_end[];

#endif /* SECTIONS_H */
