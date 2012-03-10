#ifndef _FCNTL_H
#define _FCNTL_H

/*
 * fcntl.h - File control options
 *
 * Taken from POSIX:2001
 */

#include <stat.h>			/* Fields for open(,,@mode) */

/*
 * The following fields are used for open(,@oflags,).
 * NOTE!! "They shall be bitwise distinct" --POSIX
 */

/* File access modes: "Applications shall specify exactly one
 * of the file access modes values in open()." --POSIX */
#define	O_RDONLY	0x0001		/* Open for reading only */
#define	O_WRONLY	0x0002		/* Open for writing only */
#define	O_RDWR		0x0003		/* Open for reading and writing */
#define	O_ACCMODE	0x0003		/* Mask for file access modes */

/* File creation flags: */
#define	O_CREAT		0x0004		/* Create file if nonexistent */
#define	O_TRUNC		0x0008		/* Truncate to zero length */
#define O_NOCTTY	0x0010		/* Don't assign controlling terminal */
#define	O_EXCL		0x0020		/* Exclusive use: Error if file exists */

/* File status flags: */
#define O_APPEND	0x0040		/* Set append mode */
#define O_DSYNC		0x0080		/* ??? */
#define O_NONBLOCK	0x0100		/* No delay (for FIFOs, etc) */
#define O_RSYNC		0x0200		/* ??? */
#define O_SYNC		0x0400		/* ??? */

#endif	/* _FCNTL_H */
