/*
 * Serial Port, 8250/16550 UART
 *
 * Copyright (C) 2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * UART is the chip that picks a (parallel) byte and send it one bit at
 * a time, sequentially, over the serial line and vice versa. Check our
 * references repository for datasheets of 16550-compatible UARTS.
 *
 * NOTE! My laptop is void of serial ports, making that state the only
 * real use-case tested. Transmitting bytes was only tested using Qemu
 * and Bochs virtual ports.
 */

#include <kernel.h>
#include <stdint.h>
#include <paging.h>
#include <spinlock.h>
#include <serial.h>
#include <io.h>
#include <x86.h>

/*
 * A 'baud rate' is the number of bits transferred over the serial link
 * per second. To convert this to number of bytes sent per second, the
 * baud rate is divided by the number of bits in the 'serial frame'.
 *
 * One of the most common serial frames used include a start bit, 8-bits
 * of data, no parity bit, and one stop bit, totalling 10-bits. So, an
 * 8/N/1 9600 baud rate = a transfer of 960 bytes per second.
 *
 * The UART has a built-in baud rate generator, which is set to match an
 * attached device. As in all hardware timers, that crystal generator
 * has a programmed divisor, ensuring the same baud rate over any system.
 *
 * Use max baud: this is only used in a virtual machine anyway.
 */
#define MAX_BAUD	115200
#define DESIRED_BAUD	MAX_BAUD

/*
 * UART register numbers
 *
 * All PCs are capable of operating up to four serial ports using
 * standard serial RS-232 I/O adapters. Their respective UART registers
 * are accessed through 8 _sequential_ I/O ports. Each port represents
 * an UART reg accessed through the three address lines A0, A1, and A2.
 *
 * The 'base' I/O address refers to the first I/O port in a sequential
 * group (representng a single UART). To access a UART register, the
 * base port is added to the UART 3-bit register number.
 */

/*
 * When the Divisor Latch Access Bit (DLAB) is set to zero, an
 * output to this reg stores a byte into the UART's TX buffer.
 */
#define UART_TRANSMIT_BUF	0

/*
 * When DLAB=1, an output to these registers holds the least
 * and highest order bytes of the baud rate divisor latch.
 */
#define UART_DIVISOR_LATCH_LOW	0
#define UART_DIVISOR_LATCH_HIGH	1

/*
 * When DLAB=0, a serial port can be configured to operate on
 * an interrupt basis using this reg. Nonetheless, we want to
 * use serial output in regions where interrupts are disabled,
 * thus the use of polling instead.
 */
#define UART_INTERRUPT_ENABLE	1

/*
 * When DLAB=0, this is used to provide a FIFO queueing
 * discipline, buffering up to 16-bytes of received data.
 */
#define UART_FIFO_CTRL		2

/*
 * Line control register, controls DLAB and send mode
 */
#define UART_LINE_CTRL		3
union line_control_reg {
	uint8_t raw;
	struct {
		uint8_t data_len:2,	/* # data bits in frame */
			stop_bit:1,	/* # stop bits in frame */
			parity_on:1,	/* Enable parity bit? */
			even_parity:1,  /* Even parity (if parity_on = 1) */
			sticky_parity:1,/* Unused */
			break_ctrl:1,	/* Unused */
			DLAB:1;		/* the DLAB control bit (see above) */
	} __packed;
};

enum {
	DATA_LEN_8 =	0x3,		/* 8 bytes in serial frame */
	DATA_LEN_7 =	0x2,		/* 7 bytes in serial frame */
	DATA_LEN_6 =	0x1,		/* 6 bytes in serial frame */
	DATA_LEN_5 =	0x0,		/* 5 bytes in serial frame */
};

enum {
	STOP_BIT_1 =	0x0,		/* 1 stop  bit in serial frame */
	STOP_BIT_0 =	0x1,		/* 2 stop bits in serial frame */
};

/*
 * Modem control register: 'Data Terminal Ready' informs the
 * attached device that _we_ are active and ready for commun-
 * ication. 'Request to Send' tells attached device that _we_
 * want to send data.
 */
#define UART_MODEM_CTRL		4
union modem_control_reg {
	uint8_t raw;
	struct {
		uint8_t dt_ready:1,	/* RS-232 Data Termainal Ready */
			req_send:1,	/* RS-232 Request To Send */
			unused0:2,	/* Unused */
			loopback:1,	/* Loopback between tx and rx */
			unused1:3;	/* Unused */
	} __packed;
};

/*
 * Status and error info relating to rx and tx.
 */
#define UART_LINE_STATUS	5
union line_status_reg {
	uint8_t raw;
	struct {
		uint8_t rx_avail:1,	/* Received data available */
			err_overrun:1,	/* A byte was lost (busy CPU) */
			err_parity:1,	/* Parity mismatch */
			err_frame:1,	/* frame corruption; noisy signal */
			rx_break:1,	/* If 1, a break signal received */
			tx_empty:1,	/* ALL tx buffers are empty */
			tx_has_byte:1,	/* holding reg or shift reg has a byte */
			unused:1;	/* Unused */
	} __packed;
};

/*
 * Modem Status: 'Data Set Ready' indicates that the remote
 * device is powered up and ready. 'Clear to send' informs
 * us that the attached device is ready for receiving data.
 */
#define UART_MODEM_STATUS	6
union modem_status_reg {
	uint8_t raw;
	struct {
		uint8_t unused0:4,	/* Unused */
			clr_to_send:1,	/* RS-232 Clear to Send */
			remote_ready:1,	/* RS-232 Data Set Ready */
			unused1:2;	/* Unused */
	} __packed;
};

/*
 * Port #1 base I/O address, from BIOS Data Area.
 * If the BDA returned a zero value, there's no active
 * serial port attached, and thus no data can be sent.
 */
#define BDA_COM1_ENTRY	0x400
static uint16_t port_base;

static void reset_port_set_8n1_mode(void)
{
	union line_control_reg reg;

	reg.raw = 0;
	reg.stop_bit = STOP_BIT_1;
	reg.data_len = DATA_LEN_8;
	outb(reg.raw, port_base + UART_LINE_CTRL);
}

static void enable_DLAB(void)
{
	union line_control_reg reg;

	reg.raw = inb(port_base + UART_LINE_CTRL);
	reg.DLAB = 1;
	outb(reg.raw, port_base + UART_LINE_CTRL);
}

static void disable_DLAB(void)
{
	union line_control_reg reg;

	reg.raw = inb(port_base + UART_LINE_CTRL);
	reg.DLAB = 0;
	outb(reg.raw, port_base + UART_LINE_CTRL);
}

void serial_init(void)
{
	union modem_control_reg reg;
	uint16_t divisor;

	divisor = MAX_BAUD / DESIRED_BAUD;
	assert(divisor <= UINT16_MAX);

	port_base = *(uint16_t *)VIRTUAL(BDA_COM1_ENTRY);
	if (port_base == 0) {
		printk("COM1: BIOS-reported I/O address = 0; "
		       "no serial port attached\n");
		return;
	}

	printk("COM1: BIOS-reported I/O address = 0x%x\n", port_base);
	reset_port_set_8n1_mode();
	outb(0x00, port_base + UART_INTERRUPT_ENABLE);

	enable_DLAB();
	outb(divisor & 0xff, port_base + UART_DIVISOR_LATCH_LOW);
	outb(divisor >> 8, port_base + UART_DIVISOR_LATCH_HIGH);
	disable_DLAB();

	outb(0x00, port_base + UART_FIFO_CTRL);
	reg.raw = 0, reg.dt_ready = 1, reg.req_send = 1;
	outb(reg.raw, port_base + UART_MODEM_CTRL);
}

static bool tx_buffer_empty(void)
{
	union line_status_reg reg;

	reg.raw = inb(port_base + UART_LINE_STATUS);

	return reg.tx_empty;
}

static bool remote_ready(void)
{
	union modem_status_reg reg;

	/* Safest approach is to poll the DSR and CTS
	 * status lines for 2ms till both are high */
	reg.raw = inb(port_base + UART_MODEM_STATUS);

	return reg.remote_ready && reg.clr_to_send;
}

static int port_is_broken;
spinlock_t port_lock = SPIN_UNLOCKED();

static int __putc(uint8_t byte)
{
	int timeout;

	/* Next byte may have a better luck */
	if (!remote_ready())
		return 0;

	timeout = 0xfffff;
	while (!tx_buffer_empty() && timeout--)
		cpu_pause();

	if (__unlikely(timeout == -1)) {
		port_is_broken = 1;
		return 1;
	}

	outb(byte, port_base + UART_TRANSMIT_BUF);
	return 0;
}

void serial_write(const char *buf, int len)
{
	int ret;

	if (port_base == 0)
		return;

	spin_lock(&port_lock);

	if (port_is_broken)
		goto out;

	ret = 0;
	while (*buf && len-- && ret == 0)
		ret = __putc(*buf++);

out:	spin_unlock(&port_lock);
}

void serial_putc(char ch)
{
	serial_write(&ch, 1);
}
