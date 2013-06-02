#ifndef _ERRNO_H
#define _ERRNO_H

/*
 * Standard Unix symbols for Error Numbers (errno)
 *
 * Copyright (C) 2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * “No function in this document sets errno to 0 to indicate an error.
 * Only these symbolic names should be used in programs, since the actual
 * val of the err number is unspecified.” --Single UNIX Specv3, POSIX:2001
 *
 * The errno values range 1->34 seems to be standardized _only_ as a his-
 * torical artifact in the UNIX jungle. Even Linus in linux-0.01 wonders
 * about such values origin, .. and just copies them verbatim from Minix.
 *
 * In light of the above, we just copy the _values_ from Linux & BSD :-)
 */

// The historical Unix errno 1->34 Range:
#define	EPERM		 1	/* Operation not permitted */
#define	ENOENT		 2	/* No such file or directory */
#define	ESRCH		 3	/* No such process */
#define	EINTR		 4	/* Interrupted system call */
#define	EIO		 5	/* I/O error */
#define	ENXIO		 6	/* No such device or address */
#define	E2BIG		 7	/* Argument list too long */
#define	ENOEXEC		 8	/* Exec format error */
#define	EBADF		 9	/* Bad file number */
#define	ECHILD		10	/* No child processes */
#define	EAGAIN		11	/* Try again */
#define	ENOMEM		12	/* Out of memory */
#define	EACCES		13	/* Permission denied */
#define	EFAULT		14	/* Bad address */
#define	ENOTBLK		15	/* Block device required */
#define	EBUSY		16	/* Device or resource busy */
#define	EEXIST		17	/* File exists */
#define	EXDEV		18	/* Cross-device link */
#define	ENODEV		19	/* No such device */
#define	ENOTDIR		20	/* Not a directory */
#define	EISDIR		21	/* Is a directory */
#define	EINVAL		22	/* Invalid argument */
#define	ENFILE		23	/* File table overflow */
#define	EMFILE		24	/* Too many open files */
#define	ENOTTY		25	/* Not a typewriter */
#define	ETXTBSY		26	/* Text file busy */
#define	EFBIG		27	/* File too large */
#define	ENOSPC		28	/* No space left on device */
#define	ESPIPE		29	/* Illegal seek */
#define	EROFS		30	/* Read-only file system */
#define	EMLINK		31	/* Too many links */
#define	EPIPE		32	/* Broken pipe */
#define	EDOM		33	/* Math argument out of domain of func */
#define	ERANGE		34	/* Math result not representable (too large) */

// Newer errnos:
#define	ELOOP		35	/* Too many symbolic links encountered */
#define	ENAMETOOLONG	36	/* File name too long */
#define EOVERFLOW	37	/* Value too large to be stored in data type */

/*
 * Descriptions are copied  verbatim from the “Single Unix Specification,
 * Version 1”, Namely the “X/Open CAE Specification”: ‘System Interfaces
 * and Headers, Issue 4, Version 2 (1994)’.
 */
static inline const char *errno_description(int err) {
	switch(err) {
	case 0:
		return "Success";
	case -ENOENT:
		return "ENOENT: A component of a specified pathname does not "
		       "exist, or the pathname is an empty string";
	case -ENOTDIR:
		return "ENOTDIR: A component of the specified pathname exists, "
		       "but it is not a directory, where a directory was "
		       "expected";
	case -ENAMETOOLONG:
		return "ENAMETOOLONG: The length of a pathname exceeds "
		       "{PATH_MAX}, or a pathname component is longer than "
		       "{NAME_MAX}";
	case -EBADF:
		return "EBADF:  A file descriptor argument is out of range, "
		       "refers to no open file, or a read (write) request is "
		       "made to a file that is only open for writing (reading)";
	case -EFBIG:
		return "EFBIG: The size of a file would exceed the maximum "
		       "file size of an implementation";
	case -ENOSPC:
		return "ENOSPC: During the write( ) function on a regular "
		       "file or when extending a directory, there is no free "
		       "space left on the device";
	default:
		return "Un-stringified error";
	}
}

static inline const char *errno_to_str(int err) {
	switch(err) {
	case 0:			return "Success";
	case -EPERM:		return "EPERM";
	case -EINVAL:		return "EINVAL";
	case -ENOENT:		return "ENOENT";
	case -ENOTDIR:		return "ENOTDIR";
	case -EISDIR:		return "EISDIR";
	case -ENAMETOOLONG:	return "ENAMETOOLONG";
	case -EBADF:		return "EBADF";
	case -EEXIST:		return "EEXIST";
	case -EFBIG:		return "EFBIG";
	case -ENOSPC:		return "ENOSPC";
	case -ESPIPE:		return "ESPIPE";
	case -EOVERFLOW:	return "EOVERFLOW";
	default:		return "Un-stringified";
	}
}

#define errno(err)	errno_to_str(err)

#endif /* _ERRNO_H */
