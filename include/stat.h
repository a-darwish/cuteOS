#ifndef _STAT_H
#define _STAT_H

/*
 * stat.h - Data returned by the stat() function
 *
 * Taken from POSIX:2001
 */

#include <stdint.h>

typedef uint64_t dev_t;		/* "Shall be _unsigned_ integer type" */
typedef uint64_t ino_t;		/* ... */
typedef uint64_t nlink_t;	/* "Shall be an integer type" */
typedef uint32_t uid_t;		/* "Shall be an integer type" */
typedef uint32_t gid_t;		/* "Shall be an integer type" */
typedef int64_t  off_t;		/* "Shall be _signed_ integer type" */
typedef int64_t  blkcnt_t;	/* "Shall be _signed_ integer type" */
typedef uint64_t blksize_t;	/* "Shall be an integer type" */
typedef uint64_t time_t;	/* "Shall be an integer,or real-floating type" */

/*
 * File permission bits, (or) creation mode specified at open(,,@mode)
 *
 * I_[R,W,X][USR,GRP,OTH], I_[S,G]UID, & I_SVTX "shall be unique." --SUSv3
 *
 * NOTE! The symbol values are taken from Ext2 specification to ease up
 * stat() system call implementation.
 */
typedef enum {

	// 1- Process execution user/group override
	S_ISUID		= 0x0800,	/* Set Process UID bit */
	S_IGUID		= 0x0400,	/* Set Group UID bit */
	S_ISVTX		= 0x0200,	/* Sticky bit */

	// 2- Access rights bit mask
	S_IRUSR		= 0x0100,	/* R for owner */
	S_IWUSR		= 0x0080,	/* W for owner */
	S_IXUSR		= 0x0040,	/* X for owner */
	S_IRGRP		= 0x0020,	/* R for group */
	S_IWGRP		= 0x0010,	/* W for group */
	S_IXGRP		= 0x0008,	/* X for group */
	S_IROTH		= 0x0004,	/* R for other */
	S_IWOTH		= 0x0002,	/* W for other */
	S_IXOTH		= 0x0001,	/* X for other */

	S_IRWXU		= S_IRUSR | S_IWUSR | S_IXUSR,
	S_IRWXG		= S_IRGRP | S_IWGRP | S_IXGRP,
	S_IRWXO		= S_IROTH | S_IWOTH | S_IXOTH,

	// 3- File Type
	S_IFMT		= 0xF000,	/* Mask for the 'file type' mode bits */
	S_IFSOCK	= 0xC000,	/* Socket */
	S_IFLNK		= 0xA000,	/* Symbolic link */
	S_IFREG		= 0x8000,	/* Regular file */
	S_IFBLK		= 0x6000,	/* Block device */
	S_IFDIR		= 0x4000,	/* Directory */
	S_IFCHR		= 0x2000,	/* Character device */
	S_IFIFO		= 0x1000,	/* FIFO */

} mode_t;			/* "Shall be an integer type" */

#define S_ISDIR(mode)	(((mode) & S_IFMT) == S_IFDIR)
#define S_ISCHR(mode)	(((mode) & S_IFMT) == S_IFCHR)
#define S_ISBLK(mode)	(((mode) & S_IFMT) == S_IFBLK)
#define S_ISREG(mode)	(((mode) & S_IFMT) == S_IFREG)
#define S_ISFIFO(mode)	(((mode) & S_IFMT) == S_IFIFO)
#define S_ISLNK(mode)	(((mode) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(mode)	(((mode) & S_IFMT) == S_IFSOCK)

/*
 * Inode information returned by the stat() group of functions
 */
struct stat {
	dev_t	st_dev;		/* Device ID of device containing file */
	ino_t	st_ino;		/* File serial (inode) number */
	mode_t	st_mode;	/* Inode mode bits (see above) */
	nlink_t	st_nlink;	/* Number of hard links to the file */
	uid_t	st_uid;		/* User ID of file */
	gid_t	st_gid;		/* Group ID of file */
	dev_t	st_rdev;	/* Device ID, if file is char or block special */
	off_t	st_size;	/* For regular files, the file size in bytes.
				   For symbolic links, the length in bytes of
				   the pathname contained in the symbolic link */
	time_t	st_atime;	/* Time of last access */
	time_t	st_mtime;	/* Time of last data modification */
	time_t	st_ctime;	/* Time of last file status change */
};

#endif /* _STAT_H */
