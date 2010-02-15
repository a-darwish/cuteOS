/* 
 * Kernel linker script
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * Refer to official LD documentation "Using LD - the GNU linker"
 * for more information.
 *
 * Symbols prefixed by `__' are used by our C and assembly files;
 * modify them cautiously
 */

#include <paging.h>

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)

ENTRY(startup_32)

SECTIONS
{
	/* Where the bootsector loads us */
	. = 0x100000;

	.text.head : {
	      head.o(.text)
	      e820.o(.text)
	}

	.data.head : {
	      head.o(.data)
	      e820.o(.data)
	      head.o(.rodata)
	      e820.o(.rodata)
	}

	__headbss_start = .;
	.bss.head (NOLOAD) : {
	      head.o(.bss)
	      e820.o(.bss)
	}

	/* High virtual-area mapped kernel code and data start */
	__phys_start = .;
	PROVIDE_HIDDEN(virt_start = __phys_start + KTEXT_BASE);

	/* We override the head.o bss section to save executable
	 * size. On the very start, we'll move the code away to
	 * phys_start to let head.S use its bss section freely */
	.text virt_start : AT(__headbss_start) {
	       __text_start = .;
	       *(EXCLUDE_FILE (*head.o *e820.o) .text)
	       __text_end = .;
	}

	.data : {
	       __data_start = .;
	       *(EXCLUDE_FILE (*head.o *e820.o) .data)
	       *(EXCLUDE_FILE (*head.o *e820.o) .rodata)
	       __data_end = .;
        }

	.bss : {
		__bss_start = .;
	       *(EXCLUDE_FILE (*head.o *e820.o) .bss)
	       *(EXCLUDE_FILE (*head.o *e820.o) COMMON)
		__bss_end = .;
	}

	__kernel_end = .;

	/DISCARD/ : {
		*(.eh_frame)
		*(.comment)
	}
}
