/*
 * BIOS E820h - Query System Address Map service.
 *
 * Copyright (C) 2009 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * Send our real-mode method (which query the BIOS E820h service) to the
 * "rmcall" function; "rmcall" will switch to real-mode, call the passed
 * method on our behalf, then return back!
 *
 * Check the ACPI spec v4.0, Chapter 14, "System Address Map Interfaces"
 * and critical comments on top of e820.h for further details.
 */

#include <e820.h>
#include "rmcall.h"

.text

.code32

	.globl e820
	.type  e820, @function
e820:
	pushl  $get_mmap_end
	pushl  $get_mmap
	call   rmcall
	addl   $8, %esp
	ret
	.size  e820, . - e820

.code16

/*
 * Get system memory map through the E820 BIOS ACPI service
 * and store the result in the system structure described at
 * e820.h
 *
 * NOTE! ACPI specifies a minimum e820 entry size of 20 bytes;
 * it does NOT specify a max. Nonetheless, "Sony VAIO" BIOSes
 * discard any E820h call with an entry size > 24.  Let it be.
 */
	.type  get_mmap, @function
	.equ   e820_sig, E820_BIOS_SIG
	.equ   init_sig, E820_INIT_SIG
	.equ   entry_sz, 24		# ACPI-v4/SonyVaio
	.equ   width_sz, 4		# width field size
	.equ   error_sz, 4		# error field size
	.equ   chksm_sz, 4		# chksm field size
	.equ   end_sz,   4		# end mark size
	.equ   max_entry, E820_PHYS_MAX - \
		(entry_sz+width_sz+end_sz+error_sz+chksm_sz)
get_mmap:
	xorw   %ax, %ax
	movw   %ax, %es
	movw   %ax, %ds
	movl   $E820_PHYS_BASE, %edi	# buffer start

	/* First write our start signature */
	movl   $init_sig, %es:(%di)
	addl   $4, %edi			# signature size

	/* Iteratively call bios service */
	xorl   %ecx, %ecx		# previous entry size
	xorl   %ebx, %ebx		# continuation; 0 on start
1:
	addl   %ecx, %edi		# previous entry size
	addl   $width_sz, %edi		# size field width
	cmpl   $max_entry, %edi		# no more space?
	jge    e820_full		# buffer full
	movl   $entry_sz, %ecx		# max supported entry size
	movl   $e820_sig, %edx		# 'SMAP'
	movl   $0xe820, %eax		# bios function E280h
	pushl  %edi			# may get trashed
	int    $0x15
	popl   %edi

	/* Result sanity checks */
	cmpl   $e820_sig, %eax		# verify bios revision
	jne    e820_none		# e820 not supported :(
	cmpl   $0x14, %ecx		# 20 bytes minimum (acpi)
	jl     e820_bug			# bios bug
	jc     e820_err			# general error
	cmpl   $entry_sz, %ecx		# max entry size exceeded?
	jg     e820_huge

	/* Store returned entry size */
	movl   %ecx, %es:-width_sz(%di)

	/* Last entry? */
	testl  %ebx, %ebx
	je     e820_done		# no more entries

	/* Read next entry */
	jmp    1b

	/* Choose error code (%eax) */
e820_full:
	movl   $E820_BUF_FULL, %eax
	jmp    end
e820_none:
	movl   $E820_NOT_SUPP, %eax
	jmp    end
e820_err:
	movl   $E820_ERROR, %eax
	jmp    end
e820_bug:
	movl   $E820_BIOS_BUG, %eax
	jmp    end
e820_huge:
	movl   $E820_HUGE_ENTRY, %eax
	jmp    end
e820_done:
	movl   $E820_SUCCESS, %eax
	addl   %ecx, %es:(%di)
	addl   $end_sz, %es:(%di)
	jmp    end

	/* Error code stored at %eax; %edi
	 * points to 'err code' field start */
end:	movl   $E820_END, %es:-end_sz(%di)
	movl   %eax, %es:(%di)

	/* Finally, calculate checksum */
	movl   $E820_PHYS_BASE, %esi
	addl   $error_sz, %edi
	call   get_checksum
	movl   %eax, %es:(%di)

	/* Restore segments state */
	movw   %cs, %ax
	movw   %ax, %ds
	movw   %ax, %es
	ret

/*
 * Calculate a memory area checksum where %esi is
 * buffer start and %edi points to buffer end.
 * NOTE! %ds must have a 0 base value
 */
	.type  get_checksum, @function
get_checksum:
	xorl   %eax, %eax
1:	cmpl   %esi, %edi
	je     2f
	movzbl (%esi), %ebx
	addl   %ebx, %eax
	incl   %esi
	jmp    1b
2:	ret

get_mmap_end:
	.size  get_mmap, . - get_mmap
