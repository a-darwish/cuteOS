/* 
 * Kernel linker script
 *
 * Copyright (C) 2009-2010 Ahmed S. Darwish <darwish.07@gmail.com>
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

	/*
	 * Bootstrap sections can't be merged with the rest
	 * of the kernel as they're physical-addresses based
	 */

	.text.head : {
	      boot/head.o(.text)
	      boot/e820.o(.text)
	}

	.data.head : {
	      boot/head.o(.data)
	      boot/e820.o(.data)
	      boot/head.o(.rodata*)
	      boot/e820.o(.rodata*)
	}

	. = .;
	__headbss_start = .;
	.bss.head (NOLOAD) : {
	      boot/head.o(.bss)
	      boot/e820.o(.bss)
	}

	/*
	 * Use virtual addresses for the rest of the kernel
	 */

	. += KTEXT_PAGE_OFFSET;

	/*
	 * So far, the linker will have to allocate executable size
	 * to bootstrap's BSS as it resides between other sections.
	 * This's caused by LD's default behaviour of having virtual
	 * memory addresses (VMA) = load memory addresses (LMA)
	 *
	 * To save kernel's binary image size, override bootstrap's
	 * bss section with kernel code and data. i.e., set kernel
	 * Load memory address to bootstrap's BSS section start.
	 *
	 * On the very start, we'll move such code to its place at
	 * __kern_phys_start, letting bootstrap use its BSS freely.
	 */
	.text : AT(__headbss_start) {
	       __text_start = .;
	       *(EXCLUDE_FILE (*boot/head.o *boot/e820.o) .text)
	       __text_end = .;
	}

	.data : {
	       __data_start = .;
	       *(EXCLUDE_FILE (*boot/head.o *boot/e820.o) .data)
	       *(EXCLUDE_FILE (*boot/head.o *boot/e820.o) .rodata*)
	       __data_end = .;
        }

	.bss : {
		__bss_start = .;
	       *(EXCLUDE_FILE (*boot/head.o *boot/e820.o) .bss)
	       *(EXCLUDE_FILE (*boot/head.o *boot/e820.o) COMMON)
		__bss_end = .;
	}

	__kernel_end = .;

	/* Binaries dont include BSS .. */
	__kernel_bin_size = ADDR(.bss) - ADDR(.text);
	__kernel_phys_bin_end = ADDR(.bss) - KTEXT_PAGE_OFFSET;

	/* Kernel edges */
	__kernel_phys_start = ADDR(.text)  - KTEXT_PAGE_OFFSET;
	__kernel_phys_end   = __kernel_end - KTEXT_PAGE_OFFSET;

	/* After moving the kernel code away from bootstrap's BSS,
	 * be sure our exported _end symbols really reflect reality */
	__kernel_bss_size = __bss_end - __bss_start;
	ASSERT(__kernel_phys_end == __kernel_phys_start +
	       __kernel_bin_size + __kernel_bss_size, "Error: \
	Code relocation out of bootstrap's BSS will lead to information loss")

	/DISCARD/ : {
		*(.eh_frame)
		*(.comment)
		*(.note)
	}
}
