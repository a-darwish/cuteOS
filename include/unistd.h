#ifndef _UNISTD_H
#define _UNISTD_H

/*
 * unistd.h - Standard symbolic constants and types
 *
 * Taken from POSIX:2001
 */

/*
 * Seek base for lseek(@offset) & fcntl(); "shall have distinct values" --POSIX
 * "In earlier UNIX implementations, the integers 0, 1, and 2 were used, rather
 * than the SEEK_* constants" --M. Kerrisk
 */
#define SEEK_SET	0	/* Set file offset to @offset */
#define SEEK_CUR	1	/* Set file offset to current + @offset */
#define SEEK_END	2	/* Set file offset to EOF + @offset */

#endif	/* _UNISTD_H */
