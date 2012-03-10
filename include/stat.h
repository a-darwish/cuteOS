#ifndef _STAT_H
#define _STAT_H

/*
 * stat.h - Data returned by the stat() function
 *
 * Taken from POSIX:2001
 */

/*
 * File permission bits, (or) creation mode specified at open(,,@mode)
 *
 * I_[R,W,X][USR,GRP,OTH], I_[S,G]UID, & I_SVTX "shall be unique." --POSIX
 */
typedef enum {
	S_ISUID		= 0x0800,	/* Set Process UID bit */
	S_IGUID		= 0x0400,	/* Set Group UID bit */
	S_ISVTX		= 0x0200,	/* Sticky bit */

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
} mode_t;

#endif /* _STAT_H */
