/*
 * Barebones PS/2 keyboard
 * Motherboard and on-keyboard i8042-compatible controllers
 *
 * Copyright (C) 2009-2011 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <stdint.h>
#include <kernel.h>
#include <ioapic.h>
#include <io.h>
#include <idt.h>
#include <keyboard.h>
#include <apic.h>
#include <vectors.h>

enum {
	KBD_STATUS_REG	= 0x64,		/* Status register (R) */
	KBD_COMMAND_REG = 0x64,		/* Command register (W) */
	KBD_DATA_REG	= 0x60,		/* Data register (R/W) */
};

/*
 * Motherboard controller's status; (R) from port 0x64
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
 * Motherboard controller's commands; (W) to port 0x64
 */
enum i8042_cmd {
	READ_CMD	= 0x20,		/* Read current command byte */
	WRITE_CMD	= 0x60,		/* Write command byte */
	SELF_TEST	= 0xaa,		/* Internal diagnostic test */
	INT_TEST	= 0xab,		/* Test keyboard clock and data lines */
	READ_P1		= 0xc0,		/* Unused - Read input port (P1) */
	READ_OUTPUT	= 0xd0,		/* Read controller's P2 output port */
	WRITE_OUTPUT	= 0xd1,		/* Write controller's P2 output port */
};

/*
 * Motherboard controller's output port (P2) pins description.
 * Such pins are connected to system lines like the on-keyboard
 * controller, IRQ1, system reboot, and the A20 multiplexer.
 */
union i8042_p2 {
	uint8_t raw;
	struct {
		uint8_t reset:1,        /* Write 0 to system reset */
			a20:1,		/* If 0, A20 line is zeroed */
			__unused0:1,
			__unused1:1,
			irq1:1,		/* Output Buffer Full (IRQ1 high) */
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

/*
 * Special-keys scan codes
 */
enum {
	KEY_RSHIFT	= 0x36,		/* Right Shift */
	KEY_LSHIFT	= 0x2a,		/* Left Shift */
	KEY_NONE	= 0xff,		/* No key was pressed - NULL mark */
};

/*
 * Release code equals system scan code with bit 7 set
 */
#define RELEASE(code)	(code | 0x80)

/*
 * AT+ (set 2) keyboard scan codes table
 */
static uint8_t scancodes[][2] = {
	[0x01] = {  0 ,  0 , },	/* escape (ESC) */
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
	[0x0e] = { '\b', 0 , },	/* FIXME: VGA backspace support */
	[0x0f] = { '\t', 0 , },	/* FIXME: VGA tab support */
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
	[0x1c] = { '\n', 0 , },	/* Enter */
	[0x1d] = {  0 ,  0 , },	/* Ctrl; good old days position */
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
	[0x28] = { '\'', '"' }, /* Quote; doubelquote */
	[0x29] = { '`', '~', }, /* Backquote; tilde */
	[0x2a] = {  0,   0,  },	/* Left shift */
	[0x2b] = { '\\', '|' }, /* Backslash; pipe */
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
	[0x36] = {  0 ,  0 , },	/* Right shift */
	[0x39] = { ' ', ' ', },	/* Space */
};

/*
 * Get a pressed key from the keyboard buffer, if any
 */
static uint8_t kbd_read_input(void)
{
	union i8042_status status;

	status.raw = inb(KBD_STATUS_REG);
	if (status.output_ready)
		return inb(KBD_DATA_REG);

	return KEY_NONE;
}

/*
 * Hardware initialization: flush the keyboard buffer. Standard
 * buffer size is 16-bytes; bigger sizes are handled for safety.
 */
static void kbd_flush_buffer(void)
{
	int trials = 128;

	while (trials--) {
		if (kbd_read_input() == KEY_NONE)
			break;
		apic_udelay(50);
	}
}

/*
 * The real handler
 */
static int shifted;		/* Shift keys pressed? */
void __kb_handler(void) {
	uint8_t code, ascii;

	/* Implicit ACK: reading the scan code empties the
	 * controller's output buffer, making it clear its
	 * P2 'output buffer full' pin  (which is actually
	 * the  IRQ1 pin) to low -- deasserting the IRQ. */
	code = kbd_read_input();

	switch(code) {
	case KEY_LSHIFT:
	case KEY_RSHIFT:
		shifted = 1;
		break;
	case RELEASE(KEY_LSHIFT):
	case RELEASE(KEY_RSHIFT):
		shifted = 0;
		break;
	};

	if (code >= ARRAY_SIZE(scancodes))
		return;

	ascii = scancodes[code][shifted];
	if (ascii)
		putc(ascii);
}

void keyboard_init(void) {
	extern void kb_handler(void);
	uint8_t vector;

	vector = KEYBOARD_IRQ_VECTOR;
	set_intr_gate(vector, kb_handler);
	ioapic_setup_isairq(1, vector, IRQ_BOOTSTRAP);

	/*
	 * Keyboard-Initialization Races:
	 *
	 * After the first key press, an edge IRQ1 get triggered and
	 * the  pressed char get  buffered. Remaining  key  presses get
	 * sliently buffered (without further edge IRQs) as long as the
	 * the buffer is already non-empty. After consuming a char from
	 * the buffer, and if the buffer is still non-empty after that
	 * consumption, a new edge IRQ1 will get triggered.
	 *
	 * We may reach here with several chars  buffered, but with the
	 * original edge IRQ  lost (not queued in the local APIC's IRR)
	 * since the relevant IOAPIC entry was not yet setup, or masked.
	 * Thus, make new keypresses  trigger an edge  IRQ1 by flushing
	 * the kbd buffer, or they won't get detected in that case!
	 *
	 * (Test the above race by directly using the keyboard once the
	 * kernel boots, with the buffer-flushing code off.)
	 *
	 * Doing such flush  before unmasking the  IOAPIC IRQ1 entry is
	 * racy: an interrupt may occur directly after the flush but
	 * before the IOAPIC setup, filling the keyboard buffer without
	 * queuing IRQ1 in the local APIC IRR & hence making  us unable
	 * to detect any further keypresses!
	 *
	 * (Test the above race by flushing the  buf before IOAPIC IRQ1
	 * ummasking, with 5 seconds of delay in-between to press keys.)
	 *
	 * Thus, flush the keyboard buffer _after_ the IOAPIC setup.
	 *
	 * Due to i8042 firmware semantics, the flushing process itself
	 * will trigger an interrupt if more than one scancode exist in
	 * the buffer (a single keypress = 2 scancodes). A keyboard IRQ
	 * may also get triggered after the IOAPIC setup but before the
	 * flush. In these cases  an  IRQ1 will get queued in the local
	 * APIC's IRR but the relevant scancodes will get flushed, i.e.,
	 * after enabling interrupts  the keyboard  ISR will get called
	 * with an empty i8042 buffer. That case is handled by checking
	 * the "Output Buffer Full" bit before reading any kbd input.
	 */
	kbd_flush_buffer();
}
