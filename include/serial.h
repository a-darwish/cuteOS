#ifndef _SERIAL_H
#define _SERIAL_H

/*
 * Serial port, 8250/16550 UART
 *
 * Copyright (C) 2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

void serial_init(void);

void serial_write(const char *buf, int len);

void serial_putc(char ch);

#endif /* _SERIAL_H */
