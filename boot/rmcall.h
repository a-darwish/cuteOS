#ifndef _RMCALL_H
#define _RMCALL_H

/*
 * Macros for the real-mode functions which are called using the
 * "rmcall" service from a 32-bit protected mode context.
 *
 * Check `rmcall.S' and `load_ramdisk.S' for further details.
 */

/*
 * Return @symbol address relative to the setup data segment.
 * @symbol: Label inside the passed "real-mode function"
 * @rmode_func_start: "Real-mode function" start address
 * __rmode_function_offset: The "real-mode function" start
 *  address offset out of the %cs base value PMODE16_START.
 */
#define REL_ADDR(symbol, rmode_func_start)		\
	(symbol - rmode_func_start + __rmode_function_offset)

/*
 * Return the absolute address @addr value relative to the
 * setup data segment base.
 * @addr: absolute address within the segment 64-KB region.
 */
#define ABS_ADDR(addr)					\
	(addr - PMODE16_START)

#endif /* _RMCALL_H */
