/* 
 * Baby linker script for our baby kernel
 * Refer to official LD documentation "Using LD - the GNU linker"
 * for more information
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)

ENTRY(startup_32)

SECTIONS
{
	/* Where the bootsector loads us */
	. = 0x100000;
	.iarea : {
	      head.o(.text)
	      head.o(.data)
	      head.o(.rodata)
	      head.o(.bss)
	}

	/* Virtual area code and data */
	. += 0xffffffff80000000;

	/* '*' should've been enough as LD won't repeat
	 * sections already consumed, but I'm not ready
	 * for any surprises */
	.varea : {
	       *(EXCLUDE_FILE (*head.o) .text)
	       *(EXCLUDE_FILE (*head.o) .data)
	       *(EXCLUDE_FILE (*head.o) .rodata)
	       *(EXCLUDE_FILE (*head.o) .bss)
	}

	/DISCARD/ : {
		*(.eh_frame)
		*(.comment)
	}
}
