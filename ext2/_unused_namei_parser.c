/*
 * This is a stateful parser for hierarchial file system paths.
 * For simplicity, an ad-hoc parser is now used instead.
 *
 * This code is STILL kept in the repository: we may use it if
 * more complex parsing requirements arose!
 *
 * Copyright (C) 2012 Ahmed S. Darwish <darwish.07@gmail.co>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <kmalloc.h>
#include <string.h>
#include <ext2.h>

enum state {
	NONE,			/* .. Start State */
	SLASH,			/* The '/' path separator */
	DIRECTORY,		/* Parsing a directory name */
	FILE,			/* Parsing a regular file name */
	NAME,			/* File name (either dir or file) */
	EOL,			/* .. End State */
};

static uint64_t handle(char *buf, uint64_t *buf_len, uint64_t inum) {
	struct dir_entry *dentry;

	buf[*buf_len] = '\0';
	dentry = find_dir_entry(inode_get(inum), buf, *buf_len);
	inum = dentry->inode_num;
	kfree(dentry);

	*buf_len = 0;
	return inum;
}

/*
 * Namei - Find the inode of given file path
 * path         : Absolute, hierarchial, UNIX format, file path
 * return value : Inode number, or 0 in case of search failure.
 */
uint64_t name_i(const char *path)
{
	enum state state, prev_state;
	char *buf;
	uint64_t buf_len, inum;

	state = NONE;
	buf_len = 0;
	inum = 0;
	buf = kmalloc(EXT2_FILENAME_LEN + 2);
	for (int i = 0; i <= strlen(path); i++) {
		prev_state = state;
		switch (path[i]) {
		case '/':
			state = SLASH;
			if (prev_state == SLASH)
				break;
			if (prev_state == NONE)
				inum = EXT2_ROOT_INODE;		/* Absolute */
			if (prev_state == NAME) {
				inum = handle(buf, &buf_len, inum);
				if (inum == 0 || !is_dir(inum))
					goto notfound;
			}
			break;
		case '\0':
			state = EOL;
			if (prev_state == NONE)
				goto notfound;
			if (prev_state == NAME) {
				inum = handle(buf, &buf_len, inum);
				if (inum == 0)
					goto notfound;
			}
			break;
		default:
			state = NAME;
			if (prev_state == NONE)			/* Relative */
				panic("EXT2: Relative paths aren't supported!");
			if (buf_len > EXT2_FILENAME_LEN)
				goto notfound;
			buf[buf_len] = path[i];
			buf_len++;
		}
	}
	goto found;

notfound:
	inum = 0;
found:
	kfree(buf);
	return inum;
}
