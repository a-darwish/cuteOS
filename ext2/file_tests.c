/*
 * Standard Unix system calls on files -- Test cases
 *
 * Copyright (C) 2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <errno.h>
#include <ext2.h>
#include <file.h>
#include <fcntl.h>
#include <unistd.h>
#include <tests.h>
#include <kmalloc.h>
#include <unrolled_list.h>
#include <percpu.h>
#include <ramdisk.h>

#if	FILE_TESTS

#define TEST_CHDIR	0
#define TEST_OPEN	0
#define TEST_READ	0
#define TEST_WRITE	0
#define TEST_LSEEK	0
#define TEST_STAT	0
#define TEST_CLOSE	0
#define TEST_CREATION	0
#define TEST_DELETION	0
#define TEST_HARD_LINKS 1

extern struct path_translation ext2_files_list[];
extern const char *ext2_root_list[];

/*
 * Test the chdir() system call: given absolute @path, chdir() to all
 * of its middle sub-components, then return inode# of the last one!
 */
static uint64_t _test_chdir_on_path(const char *path)
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

static void __unused _test_chdir(void)
{
	struct path_translation *file;
	const char *path;
	uint64_t inum;

	for (uint i = 0; ext2_files_list[i].path != NULL; i++) {
		file = &ext2_files_list[i];
		file->relative_inum = _test_chdir_on_path(file->path);
		if (file->absolute_inum &&
		    file->absolute_inum != file->relative_inum)
			panic("_FILE: Absolute pathname translation for path "
			      "'%s' = %lu, while relative = %lu", file->path,
			      file->absolute_inum, file->relative_inum);
	}

	for (uint i = 0; ext2_root_list[i] != NULL; i++) {
		path = ext2_root_list[i];
		inum = _test_chdir_on_path(path);
		if (inum != EXT2_ROOT_INODE)
			panic("_FILE: Relative pathname translation for '%s' "
			      "= %lu; while it should've been root inode 2!");
	}
}

static void __unused _test_open(void)
{
	struct path_translation *file;

	for (uint i = 0; ext2_files_list[i].path != NULL; i++) {
		file = &ext2_files_list[i];
		prints("_FILE: Open()-ing path '%s': ", file->path);
		file->fd = sys_open(file->path, O_RDONLY | O_CREAT, 0);
		if (file->fd < 0)
			panic("..error: '%s'\n", errno_to_str(file->fd));
		else
			prints("..success! fd = %d\n", file->fd);
	}
}

static void __unused _test_close(void)
{
	struct path_translation *file;
	int fd;

	for (uint i = 0; ext2_files_list[i].path != NULL; i++) {
		file = &ext2_files_list[i];
		prints("_FILE: Close()-ing path '%s': ", file->path);
		file->fd = sys_open(file->path, O_RDONLY | O_CREAT, 0);
		sys_close(file->fd);
		fd = sys_open(file->path, O_RDONLY | O_CREAT, 0);
		if (file->fd != fd)
			panic("open()=%d, close(%d), open()=%d [should be "
			      "%d]", file->fd, file->fd, fd, file->fd);
		else
			prints("..success! fd = %d\n", file->fd);
	}
}

static void __unused _test_read(int read_chunk)
{
	struct path_translation *file;
	char *buf;
	int64_t len;

	buf = kmalloc(4096);
	for (uint i = 0; ext2_files_list[i].path != NULL; i++) {
		file = &ext2_files_list[i];
		prints("\n_FILE: Read()-ing path '%s': ", file->path);
		sys_close(file->fd);
		file->fd = sys_open(file->path, O_RDONLY | O_CREAT, 0);
		assert(file->fd >= 0);
		while ((len = sys_read(file->fd, buf, read_chunk)) > 0) {
			prints("\n@@@@ returned %d bytes @@@@; Data:\n", len);
			buf_char_dump(buf, len);
			prints("\n");
		}
		if (len < 0) switch(len) {
		case -EISDIR: prints("directory!\n"); break;
		default: panic("Read()-ing path '%s' returned '%s'",
			       file->path, errno_to_str(len));
		}
		prints("----------------------- EOF -----------------------\n");
	}
	kfree(buf);
}

/*
 * A simple write-testing mechanism is used:  the first 4K bytes of all
 * files are written with a series of little-endian 4-byte integers =
 * inode number;  the second 4K must have integers of (inode + 1);  the
 * third 4K must have integers of (inode + 2).
 *
 * After all files are written, the entire directory tree is traversed
 * for regular files.  For each reg file found, test its contents using
 * the above inode# method.
 */
static void __unused _test_write(void)
{
	struct stat *statbuf;
	struct path_translation *file;
	struct inode *inode;
	void (*pr)(const char *fmt, ...);
	int64_t inum, last_file = -1, fd, ilen, ret;
	const int BUF_LEN = 4096;
	char *buf, *buf2;

	pr = prints;
	buf = kmalloc(BUF_LEN);
	buf2 = kmalloc(BUF_LEN);
	statbuf = kmalloc(sizeof(*statbuf));

	for (uint j = 0; ext2_files_list[j].path != NULL; j++) {
		file = &ext2_files_list[j];
		(*pr)("Writing to file '%s': ", file->path);
		if ((ret = sys_stat(file->path, statbuf)) < 0) {
			(*pr)("Stat() error: '%s'\n", errno_to_str(ret));
			continue;
		}
		if (S_ISDIR(statbuf->st_mode)) {
			(*pr)("Directory!\n");
			continue;
		}
		if (S_ISLNK(statbuf->st_mode)) {
			(*pr)("Symbolic link!\n");
			continue;
		}
		fd = sys_open(file->path, O_WRONLY | O_TRUNC | O_CREAT, 0);
		if (fd < 0) {
			(*pr)("Open() error: '%s'\n", errno_to_str(fd));
			continue;
		}
		if (last_file != -1) {
			(*pr)("Ignoring file!\n");
			continue;
		}

		/* Forcibly transform the file to a 'regular file' type */
		inum = name_i(file->path);
		inode = inode_get(inum);
		inode->mode &= ~S_IFMT;
		inode->mode |= S_IFREG;
		assert(S_ISREG(inode->mode));

		for (int k = 0, offset = 0; k < 3; k++, offset += BUF_LEN) {
			memset32(buf, inum + k, BUF_LEN);
			ilen = sys_write(fd, buf, BUF_LEN);
			assert(ilen != 0);
			if (ilen == -ENOSPC) {
				(*pr)("Filled the entire disk!\n");
				(*pr)("Ignoring file!");
				last_file = j;
				goto out1;
			}
			if (ilen < 0) {
				(*pr)("Write() err: '%s\\n", errno_to_str(ilen));
				continue;
			}
			assert(ilen == BUF_LEN);
			assert(inode->size_low == offset + ilen);
		}
		assert(inode->size_low == BUF_LEN*3);
		(*pr)("Done!\n", inum);
	}
out1:
	(*pr)("**** NOW TESTING THE WRITTEN DATA!\n");

	for (uint j = 0; ext2_files_list[j].path != NULL; j++) {
		file = &ext2_files_list[j];
		(*pr)("Verifying file '%s': ", file->path);
		if (last_file != -1 && j >= last_file) {

			/* TODO: Delete the unmodified file not to confuse
			 * the userspace testing tool with unrwitten files */
			(*pr)("Ignoring file!\n");
			continue;
		}
		if ((ret = sys_stat(file->path, statbuf)) < 0) {
			(*pr)("Stat() error: '%s'\n", errno_to_str(ret));
			continue;
		}
		if (S_ISDIR(statbuf->st_mode)) {
			(*pr)("Directory!\n");
			continue;
		}
		if (S_ISLNK(statbuf->st_mode)) {
			(*pr)("Symbolic link!\n");
			continue;
		}
		fd = sys_open(file->path, O_RDWR | O_CREAT, 0);
		if (fd < 0) {
			(*pr)("Open() error: '%s'\n", errno_to_str(fd));
			continue;
		}

		inum = name_i(file->path);
		for (int i = 0; i < 3; i++) {
			memset32(buf2, inum + i, BUF_LEN);
			ilen = sys_read(fd, buf, BUF_LEN);
			if (ilen < 0) {
				(*pr)("Read() err: '%s\\n", errno_to_str(ilen));
				continue;
			}
			if (ilen < BUF_LEN)
				panic("We've written %d bytes to file %s, but "
				      "returned only %d bytes on read", BUF_LEN,
				      file->path, ilen);
			if (memcmp(buf, buf2, BUF_LEN) != 0) {
				(*pr)("Data written differs from what's read!\n");
				(*pr)("We've written the following into file:\n");
				buf_hex_dump(buf2, BUF_LEN);
				(*pr)("But found the following when reading:\n");
				buf_hex_dump(buf, BUF_LEN);
				panic("Data corruption detected!");
			}
		}
		(*pr)("Validated!\n");
		inode_dump(inode_get(inum), inum, file->path);
	}

	kfree(statbuf);
	kfree(buf2);
	kfree(buf);

	/* TODO: Create a new file, and fill it with bytes of ramdisk
	 * length.  Assure that -ENOSPC is returned */
}

/* Break some Software Engineering rules to minimize code duplication: */
#define _test_lseek_state(SEEK_WHENCE, EXPECTED_VALUE)			\
sys_lseek(p->fd, 0, SEEK_SET);						\
for (uint64_t i = 0; i < inode_get(file->inum)->size_low; i++) {	\
	prints("seek(%d, %lu, " #SEEK_WHENCE "): ", p->fd, i);		\
	uint64_t old_offset = file->offset;				\
	if ((ret = sys_lseek(p->fd, i, SEEK_WHENCE)) < 0)		\
		panic("failure: '%s'", errno_to_str(ret));		\
	if (file->offset != (EXPECTED_VALUE))				\
		panic("lseek failure, path='%s', lseek(" #SEEK_WHENCE	\
		      ",%lu), old offset = %lu, returned offset = %lu",	\
		      p->path, i, old_offset, file->offset);		\
	prints("offset = %lu, Success!\n", file->offset);		\
}

static void __unused _test_lseek(void)
{
	struct test_file *file;
	struct path_translation *p;
	struct inode *inode;
	int64_t ret, fd;

	ret = sys_lseek(0xffffffff, 3, SEEK_SET);
	assert(ret == -EBADF);

	fd = sys_open("/", O_RDONLY, 0);
	ret = sys_lseek(fd, 3, 4);		/* Wrong 'whence' parameter */
	assert(ret == -EINVAL);

	ret = sys_lseek(fd, UINT64_MAX / 2, SEEK_SET);
	assert(ret == UINT64_MAX / 2);

	ret = sys_lseek(fd, (UINT64_MAX / 2) + 2, SEEK_CUR);
	assert(ret == -EOVERFLOW);

	for (uint i = 0; ext2_files_list[i].path != NULL; i++) {
		p = &ext2_files_list[i];
		prints("\n_FILE: Lseek()-ing path '%s': ", p->path);
		p->fd = sys_open(p->path, O_RDONLY | O_CREAT, 0);
		assert(p->fd >= 0);
		file = unrolled_lookup(&current->fdtable, p->fd);
		assert(file != NULL);
		inode = inode_get(file->inum);

		_test_lseek_state(SEEK_SET, i);
		_test_lseek_state(SEEK_CUR, i + old_offset);
		_test_lseek_state(SEEK_END, i + inode->size_low);
	}
}

static void __validate_statbuf(int64_t inum, struct stat *statbuf)
{
	struct inode *inode;

	assert(inum > 0);
	inode = inode_get(inum);
	assert((ino_t)inum == statbuf->st_ino);
	assert(inode->mode == statbuf->st_mode);
	assert(inode->links_count == statbuf->st_nlink);
	assert(inode->uid == statbuf->st_uid);
	assert(inode->gid_low == statbuf->st_gid);
	assert(inode->size_low == statbuf->st_size);
	assert(inode->atime == statbuf->st_atime);
	assert(inode->ctime == statbuf->st_ctime);
	assert(inode->mtime == statbuf->st_mtime);
}

static void __unused _test_stat(void)
{
	struct path_translation *file;
	struct stat *statbuf;
	int ret, fd;
	int64_t inum;

	statbuf = kmalloc(sizeof(*statbuf));
	for (uint i = 0; ext2_files_list[i].path != NULL; i++) {
		file = &ext2_files_list[i];
		prints("_FILE: stat()-ing path '%s': ", file->path);
		if ((ret = sys_stat(file->path, statbuf)) < 0)
			panic("stat('%s', buf=0x%lx) = '%s'", file->path,
			      statbuf, errno_to_str(ret));
		if ((inum = name_i(file->path)) < 0)
			panic("name_i('%s') = '%s'", file->path,
			      errno_to_str(inum));
		__validate_statbuf(inum, statbuf);
		prints("Success!\n");

		fd = sys_open(file->path, O_RDONLY | O_CREAT, 0);
		prints("_FILE: Fstat()-ing path '%s': ", file->path);
		if ((ret = sys_fstat(fd, statbuf)) < 0)
			panic("stat('%s', buf=0x%lx) = '%s'", file->path,
			      statbuf, errno_to_str(ret));
		__validate_statbuf(statbuf->st_ino, statbuf);
		sys_close(fd);
		prints("Success!\n");
	}
	kfree(statbuf);
}

static void __unused file_test_path_parsing(void)
{
	struct path_translation *file;
	int leaf_idx;
	mode_t file_type;
	char tmp, *path;

	path = kmalloc(PAGE_SIZE);
	for (uint i = 0; ext2_files_list[i].path != NULL; i++) {
		file = &ext2_files_list[i];
		strncpy(path, file->path, PAGE_SIZE - 1);
		path[PAGE_SIZE - 1] = '\0';

		leaf_idx = path_get_leaf(path, &file_type);
		prints("_FILE: Checking path '%s':\n", path);
		if (leaf_idx == 0) {
			prints("NO Leaf node exist!\n");
			prints("Parent = '/'\n");
		} else {
			prints("Leaf node name = '%s'\n", &path[leaf_idx]);
			tmp = path[leaf_idx];
			path[leaf_idx] = '\0';
			prints("Parent of that node: '%s'\n", path);
			path[leaf_idx] = tmp;
		}
	}
	kfree(path);
}

static void __unused file_test_creation(void)
{
	struct path_translation *file;
	int fd;
#if 1
	/* Assure -EEXIST on all existing Ext2 volume files */
	for (uint j = 0; ext2_files_list[j].path != NULL; j++) {
		file = &ext2_files_list[j];
		prints("Testing Path '%s':\n", file->path);
		fd = sys_open(file->path, O_CREAT | O_EXCL | O_RDWR, 0);
		if (fd >= 0) {
			panic("File with path '%s' already exists, but "
			      "open(EXCL) allocated a new fd %d for it!",
			      file->path, fd);
		}
		prints("Success: file creation returned %s\n",
		       errno_to_str(fd));
	}

	/* Now just create a random set of regular files */
	char name[3] = "00";
	for (char p = 'a'; p <= 'z'; p++)
		for (char ch = '0'; ch <= '~'; ch++) {
			name[0] = p;
			name[1] = ch;
			prints("Creating new file '%s': ", name);
			fd = sys_open(name, O_CREAT | O_EXCL | O_RDWR, 0);
			if (fd < 0) {
				prints("Returned %s", errno_to_str(fd));
				panic("File creation error; check log");
			}
			prints("Success!\n");
		}

	/* Assure -EEXIST on recreation of files created above */
	for (char p = 'a'; p <= 'z'; p++)
		for (char ch = '0'; ch <= '~'; ch++) {
			name[0] = p;
			name[1] = ch;
			fd = sys_open(name, O_CREAT | O_EXCL | O_RDWR, 0);
			if (fd != -EEXIST)
				panic("File '%s' already exists, but file_new "
				      "allocated a new fd %lu for it", name, fd);
		}

	/* Boundary Case: what about files creation with long names? */
	char longname[EXT2_FILENAME_LEN + 1];
	memset(longname, 'a', EXT2_FILENAME_LEN);
	longname[EXT2_FILENAME_LEN] = '\0';
	fd = sys_open(longname, O_CREAT | O_RDWR | O_EXCL, 0);
	prints("Creating file '%s': ", longname);
	if (fd > 0)
		panic("Tried to create long file name of len %d, but it was "
		      "accepted and inode %lu returned;  ENAMETOOLONG should"
		      "'ve been returned!", EXT2_FILENAME_LEN, fd);
	prints("returned %s\n", errno_to_str(fd));
	longname[EXT2_FILENAME_LEN - 1] = '\0';
	fd = sys_open(longname, O_CREAT | O_RDWR | O_EXCL, 0);
	prints("Creating file '%s': ", longname);
	if (fd < 0)
		panic("Tried to create max possible len (%d) file name, but "
		      "error %s was returned!", EXT2_FILENAME_LEN-1,
		      errno_to_str(fd));
	prints("returned %d\n", fd);
#else
	/* Enable this code if and only if the files list have all of
	 * their parent directories already created */
	for (uint j = 0; ext2_files_list[j].path != NULL; j++) {
		file = &ext2_files_list[j];
		prints("Creating file '%s':\n", file->path);
		fd = sys_open(file->path, O_CREAT|O_EXCL|O_RDWR|O_TRUNC, 0);
		if (fd < 0)
			prints("FAILURE! Error %s\n", errno_to_str(fd));
		else
			prints("SUCCESS! returned fd = %d\n", fd);
		sys_close(fd);
	}
#endif
}

static void __unused file_test_deletion(void)
{
	struct path_translation *file;
	struct stat *statbuf;
	char *name;
	int ret;

	name = kmalloc(2); name[1] = '\0';
	for (char ch = 'A'; ch <= 'z'; ch++) {
		name[0] = ch;
		prints("Deleting non-existing file '%s': ", name);
		ret = sys_unlink(name);
		if (ret != -ENOENT)
			prints("FAILURE: returned '%s'\n", errno_to_str(ret));
		else
			prints("SUCCESS: returned '-ENOENT'\n");
	}
	kfree(name);

	statbuf = kmalloc(sizeof(*statbuf));
	for (uint i = 0; ext2_files_list[i].path != NULL; i++) {
		file = &ext2_files_list[i];
		prints("Deleting file '%s': ", file->path);
		ret = sys_stat(file->path, statbuf);
		if (ret < 0) {
			prints("Stat() FAILURE: '%s'\n", errno_to_str(ret));
			continue;
		}
		if (S_ISDIR(statbuf->st_mode)) {
			prints("Directory!\n");
			continue;
		}
		if (S_ISLNK(statbuf->st_mode)) {
			prints("Symbolic Link!\n");
			continue;
		}
		ret = sys_unlink(file->path);
		if (ret < 0) {
			prints("Unlink() FAILURE: '%s'\n", errno_to_str(ret));
			continue;
		}
		prints("Success!\n");
	}
	kfree(statbuf);
}

static void file_test_hard_links(void)
{
	const char *path, *dst;
	char *name;
	int ret, fd;
	bool test_dirs_only = true;

	/* Hard links to directories */
	name = kmalloc(2); name[1] = '\0'; name[0] = '@';
	prints("Creating hard link to root directory:\n");
	for (uint i = 0;
	     ext2_root_list[i] != NULL && name[0] != 0x7F;
	     i++, name[0]++) {
		path = ext2_root_list[i];
		prints("Creating hard link from '%s' to '%s': ", path, name);
		ret = sys_link(path, name);
		if (ret < 0)
			prints("FAILURE: %s\n", errno_to_str(ret));
		else
			prints("Success!\n");
	}
	kfree(name);
	if (test_dirs_only)
		return;

	/* Hard links to a regular file */
	dst = "destination";
	prints("Creating hard link to regular file:\n");
	fd = sys_open(dst, O_WRONLY|O_CREAT|O_EXCL, 0);
	if (fd < 0) {
		prints("FAILURE: cannot creat reg file '%s': %s\n", dst,
		       errno_to_str(fd));
		return;
	}
	name = kmalloc(2);
	for (name[0] = '@', name[1] = '\0'; name[0] != 0x7f; name[0]++) {
		ret = sys_unlink(name);
		prints("Deleting older link '%s': ", name);
		if (ret < 0) {
			prints("Error %s\n", errno_to_str(ret));
			return;
		}
		prints("Success!\n");
		prints("Creating hard link from '%s' to '%s': ", dst, name);
		ret = sys_link(dst, name);
		if (ret < 0)
			prints("FAILURE: %s\n", errno_to_str(ret));
		else
			prints("Success!\n");
	}
	kfree(name);
}

void file_run_tests(void)
{
	/* Extract the modified ext2 volume out of the virtual machine: */
	prints("Ramdisk start at: 0x%lx, with len = %ld\n",
	       ramdisk_get_buf(), ramdisk_get_len());
#if TEST_CHDIR
	_test_chdir();
#endif
#if TEST_OPEN
	_test_open();
#endif
#if TEST_READ
	for (int chunk = 4096; chunk != 0; chunk /= 2) {
		prints("*** Issuing read()s with chunk len of %d bytes!", chunk);
		_test_read(chunk);
	}
#endif
#if TEST_WRITE
	_test_write();
#endif
#if TEST_LSEEK
	_test_lseek();
#endif
#if TEST_STAT
	_test_stat();
#endif
#if TEST_CLOSE
	_test_close();
#endif
#if TEST_CREATION
	file_test_path_parsing();
	file_test_creation();
#endif
#if TEST_DELETION
	file_test_deletion();
#endif
#if TEST_HARD_LINKS
	file_test_hard_links();
#endif

	prints("%s: Sucess!", __func__);
	printk("%s: Sucess!", __func__);
}

#endif	/* FILE_TESTS */
