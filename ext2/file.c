/*
 * Standard Unix system calls for the file system
 *
 * Copyright (C) 2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <percpu.h>
#include <errno.h>
#include <ext2.h>
#include <file.h>
#include <tests.h>

int sys_chdir(const char *path)
{
	int64_t inum;

	inum = name_i(path);
	if (inum < 0)
		return inum;
	if (!is_dir(inum))
		return -ENOTDIR;

	assert(inum != 0);
	current->working_dir = inum;
	return 0;
}

#if	FILE_TESTS

#include <kmalloc.h>

extern struct path_translation ext2_files_list[];
extern const char *ext2_root_list[];

/*
 * Test the chdir() system call: given absolute @path, chdir() to all
 * of its middle sub-components, then return inode# of the last one!
 */
static uint64_t _test_chdir(const char *path)
{
	const char *ch;
	char *str;
	int i, ret;
	int64_t inum;

	assert(path != NULL);
	prints("Testing path: '%s'\n", path);

	assert(*path == '/');
	while (*path == '/')
		path++;
	prints("Changing to dir: '/' .");
	ret = sys_chdir("/");
	prints(". returned '%s'\n", errno_to_str(ret));
	if (ret < 0) return ret;
	str = kmalloc(EXT2_FILENAME_LEN);

	/* Special case for '/' */
	if (*path == '\0') {
		inum = name_i("/");
		str[0] = '/', str[1] = '\0';
		goto out;
	}

	/* Mini-Stateful parser */
	for (i = 0, ch = path; *ch != '\0';) {
		if (*ch == '/') {
			while (*ch == '/') ch++;
			str[i] = '\0';
			if (*ch != '\0') i = 0;
			prints("Changing to dir: '%s/' .", str);
			ret = sys_chdir(str);
			prints(". returned '%s'\n", errno_to_str(ret));
			if (ret < 0) return ret;
		} else {
			if (i == EXT2_FILENAME_LEN)
				panic("_FILE: Too long file name in '%s'", path);
			str[i] = *ch, ch++, i++;
		}
	}

	str[i] = '\0', inum = name_i(str);
	if (inum < 0)
		panic("_FILE: path translation for relative path '%s': '%s'",
		      str, errno_to_str(inum));
out:
	prints("Inode num for relative path '%s' = %lu\n\n", str, inum);
	kfree(str);
	return inum;
}

void file_run_tests(void)
{
	struct path_translation *file;
	const char *path;
	uint64_t inum;

	for (uint i = 0; ext2_files_list[i].path != NULL; i++) {
		file = &ext2_files_list[i];
		file->relative_inum = _test_chdir(file->path);
		if (file->absolute_inum &&
		    file->absolute_inum != file->relative_inum)
			panic("_FILE: Absolute pathname translation for path "
			      "'%s' = %lu, while relative = %lu", file->path,
			      file->absolute_inum, file->relative_inum);
	}

	for (uint i = 0; ext2_root_list[i] != NULL; i++) {
		path = ext2_root_list[i];
		inum = _test_chdir(path);
		if (inum != EXT2_ROOT_INODE)
			panic("_FILE: Relative pathname translation for '%s' "
			      "= %lu; while it should've been root inode 2!");
	}
}

#endif	/* FILE_TESTS */
