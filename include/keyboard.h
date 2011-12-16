#ifndef _I8042_H
#define _I8042_h

/*
 * Motherboard and on-keyboard i8042-compatible controllers
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <stdint.h>

#define KB_COMMAND	0x64
#define KB_DATA		0x60

/*
 * Motherboard controller's status; read from port 0x64
 */
union i8042_status {
	uint8_t raw;
	struct {
		uint8_t output_ready:1,	/* 1: a byte's waiting to be read */
			input_busy:1,	/* 1: full input buffer (0x60|0x64) */
			reset:1,	/* 1: self-test successful */
			last:1,		/* 0: data sent last. 1: command */
			__unused0:1,
			tx_timeout:1,	/* 1: transmit to keyboard timeout */
			rx_timeout:1,	/* 1: receive from keyboard timeout */
			parity_error:1;	/* 1: parity error on serial link */
	} __packed;
};

/*
 * Motherboard controller's commands; written to port 0x64
 */
enum i8042_cmd {
	READ_CMD	= 0x20,		/* Read current command byte */
	WRITE_CMD	= 0x60,		/* Write command byte */
	SELF_TEST	= 0xaa,		/* Internal diagnostic test */
	INT_TEST	= 0xab,		/* Test keyboard clock and data lines */
	READ_P1		= 0xc0,		/* Unused - Read input port (P1) */
	READ_OUTPUT	= 0xd0,		/* Read controller's output port (P2) */
	WRITE_OUTPUT	= 0xd1,		/* Write controller's output port (P2) */
};

/*
 * Motherboard controller's output port P2. Output port pins are
 * connected to system lines like the on-keyboard controller,
 * IRQ1, system reboot, and the A20 multiplexer.
 */
union i8042_output {
	uint8_t raw;
	struct {
		uint8_t reset:1,        /* Write 0 to system reset */
			a20:1,		/* If 0, A20 line is zeroed */
			__unused0:1,
			__unused1:1,
			irq1:1,		/* Full ouptut buffer (IRQ1 high) */
			input:1,	/* Input buffer empty */
			clock:1,	/* PS/2 keyboard clock line */
			data:1;		/* PS/2 keyboard data line */
	} __packed;
};

/*
 * On-keyboard controller commands; written to port 0x60.
 *
 * If the i8042 is expecting data from a previous command, we
 * write it to 0x60. Otherwise, bytes written to 0x60 are sent
 * directly to the keyboard as _commands_.
 */
enum keyboard_cmd {
	LED_WRITE	= 0xed,		/* Set keyboard LEDs states */
	ECHO		= 0xee,		/* Diagnostics: echo back byte 0xee */
	SET_TYPEMATIC	= 0xf3,		/* Set typematic info */
	KB_ENABLE	= 0xf4,		/* If in tx error, Re-enable keyboard */
	RESET		= 0xf5,		/* Reset keyboard to default state */
	FULL_RESET	= 0xff,		/* Full reset + self test */
};

enum {
	KEY_RSHIFT	= 0x36,
	KEY_LSHIFT	= 0x2a,
};

/*
 * Release code equals system scan code with bit 7 set
 */
static inline uint8_t release(uint8_t make_code)
{
	assert(make_code < 0x80);
	return make_code + 0x80;
}

void __kb_handler(void);
void keyboard_init(void);

#endif /* _I8042_H */
