/* 
 * Baby linker script for our baby kernel
 * Refer to official LD documentation "Using LD - the GNU linker"
 * for more information
 */

OUTPUT_FORMAT(binary)

OUTPUT_ARCH(i386:x86-64)

ENTRY(startup_32)

SECTIONS
{
	/* Where the bootsector loads us */
	. = 0x100000;

	.text : {
	      *(.text)
	}

	. = ALIGN(8);
	.data : {
	      *(.data)
	}

	. = ALIGN(8);
	.bss : {
	      *(.bss)
	}
}
