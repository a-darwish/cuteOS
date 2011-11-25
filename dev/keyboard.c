/*
 * Barebones PS/2 keyboard
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * This is only for testing our APICs setup. We have no driver
 * model yet, and the code is not SMP-safe.
 */

#include <stdint.h>
#include <kernel.h>
#include <ioapic.h>
#include <io.h>
#include <idt.h>
#include <keyboard.h>
#include <vectors.h>

/*
 * AT+ (set 2) keyboard scan codes table
 */
static struct {
	uint8_t unshifted;
	uint8_t shifted;
} scancodes[] = {
	[0x01] = { 0x1b, 0, },	/* escape (ESC) */
	[0x02] = { '1', '!', },
	[0x03] = { '2', '@', },
	[0x04] = { '3', '#', },
	[0x05] = { '4', '$', },
	[0x06] = { '5', '%', },
	[0x07] = { '6', '^', },
	[0x08] = { '7', '&', },
	[0x09] = { '8', '*', },
	[0x0a] = { '9', '(', },
	[0x0b] = { '0', ')', },
	[0x0c] = { '-', '_', },
	[0x0d] = { '=', '+', },
	[0x0e] = { '\b', 0, },	/* FIXME: VGA backspace support */
	[0x0f] = { '\t', 0, },	/* FIXME: VGA tab support */
	[0x10] = { 'q', 'Q', },
	[0x11] = { 'w', 'W', },
	[0x12] = { 'e', 'E', },
	[0x13] = { 'r', 'R', },
	[0x14] = { 't', 'T', },
	[0x15] = { 'y', 'Y', },
	[0x16] = { 'u', 'U', },
	[0x17] = { 'i', 'I', },
	[0x18] = { 'o', 'O', },
	[0x19] = { 'p', 'P', },
	[0x1a] = { '[', '{', },
	[0x1b] = { ']', '}', },
	[0x1c] = { '\n', 0, },	/* Enter */
	[0x1d] = { 0, 0, },	/* Ctrl; good old days position */
	[0x1e] = { 'a', 'A', },
	[0x1f] = { 's', 'S', },
	[0x20] = { 'd', 'D', },
	[0x21] = { 'f', 'F', },
	[0x22] = { 'g', 'G', },
	[0x23] = { 'h', 'H', },
	[0x24] = { 'j', 'J', },
	[0x25] = { 'k', 'K', },
	[0x26] = { 'l', 'L', },
	[0x27] = { ';', ':', },	/* Semicolon; colon */
	[0x28] = { '\'', '"', },/* Quote; doubelquote */
	[0x29] = { '`', '~', }, /* Backquote; tilde */
	[0x2a] = { 0, 0, },	/* Left shift */
	[0x2b] = { '\\', '|', },/* Backslash; pipe */
	[0x2c] = { 'z', 'Z', },
	[0x2d] = { 'x', 'X', },
	[0x2e] = { 'c', 'C', },
	[0x2f] = { 'v', 'V', },
	[0x30] = { 'b', 'B', },
	[0x31] = { 'n', 'N', },
	[0x32] = { 'm', 'M', },
	[0x33] = { ',', '<', },
	[0x34] = { '.', '>', },
	[0x35] = { '/', '?', },
	[0x36] = { 0, 0, },	/* Right shift */
	[0x39] = { ' ', 0, },	/* Space */
};

/*
 * IRQ handler stub
 */
extern void kb_handler(void);

/*
 * The real handler; FIXME: SMP.
 */
static int shifted;		/* Shift keys pressed? */
void __kb_handler(void) {
	uint8_t code, ascii;

	/* Implicit ACK: reading the scan code empties the
	 * controller's output buffer. Thus it makes it clear
	 * its output port 'full output buffer' pin, which
	 * is actually the IRQ1 pin, to low - ending the IRQ */
	code = inb(KB_DATA);

	if (code == KEY_LSHIFT || code == KEY_RSHIFT)
		shifted = 1;

	if (code == release(KEY_LSHIFT) || code == release(KEY_RSHIFT))
		shifted = 0;

	if ((int)code >= ARRAY_SIZE(scancodes))
		return;

	if (shifted)
		ascii = scancodes[code].shifted;
	else
		ascii = scancodes[code].unshifted;

	if (ascii)
		putc(ascii);
}

void keyboard_init(void) {
	int vector;

	vector = KEYBOARD_IRQ_VECTOR;
	set_intr_gate(vector, kb_handler);
	ioapic_setup_isairq(1, vector, IRQ_BOOTSTRAP);
}
