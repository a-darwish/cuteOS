/*
 * Standard Unix system calls for the file system
 *
 * Copyright (C) 2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * We don't repeat what's written in the POSIX spec here: check the sta-
 * ndard to make sense of all the syscalls parameters and return values.
 */

#include <kernel.h>
#include <percpu.h>
#include <errno.h>
#include <ext2.h>
#include <file.h>
#include <fcntl.h>
#include <stat.h>
#include <unistd.h>
#include <tests.h>
#include <unrolled_list.h>
#include <spinlock.h>
#include <kmalloc.h>
#include <string.h>

/*
 * File Table Entry
 *
 * Each call to open() results in a _new_ allocation of the file
 * structure below.  Each instance mainly contains a byte offset
 * field where the kernel expects the next read() or write() op-
 * eration to begin.
 *
 * Classical Unix allocated a table of below structure at system
 * boot, calling it the system 'file table'.  We don't need such
 * table anymore: we can allocate each entry dynamically!
 *
 * Ken Thompson rightfully notes  that below elements could have
 * been embedded in each entry of the file descriptor table, but
 * a separate structure  is used to  allow sharing of the offset
 * pointer between several user FDs, mainly for fork() and dup().
 *
 * FIXME: Use an atomic variable for the reference count instead
 * of using a spinlock. That lock is already used for protecting
 * the file offset pointer, an unrelated shared resource!  There
 * there is no SMP protection in making close() wait for another
 * thread's read() or write() operation.
 */
struct file {
	uint64_t inum;		/* Inode# of the open()-ed file */
	int flags;		/* Flags passed  to open() call */
	spinlock_t lock;	/* ONLY FOR offset and refcount */
	uint64_t offset;	/* MAIN FIELD: File byte offset */
	int refcount;		/* Reference count; fork,dup,.. */
};

static void file_init(struct file *file, uint64_t inum, int flags)
{
	file->inum = inum;
	file->flags = flags;
	spin_init(&file->lock);
	file->offset = 0;
	file->refcount = 1;
}

static void fill_statbuf(uint64_t inum, struct stat *buf)
{
	struct inode *inode;

	assert(inum > 0);
	assert(buf != NULL);
	inode = inode_get(inum);

	memset(buf, 0, sizeof(*buf));
	buf->st_ino = inum;
	buf->st_mode = inode->mode;
	buf->st_nlink = inode->links_count;
	buf->st_uid = inode->uid;
	buf->st_gid = inode->gid_low;
	buf->st_size = inode->size_low;
	buf->st_atime = inode->atime;
	buf->st_mtime = inode->mtime;
	buf->st_ctime = inode->ctime;
}

/*
 * -ENOENT, -ENOTDIR, -ENAMETOOLONG
 */
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

/*
 * -EINVAL, -ENOENT, -ENOTDIR, -ENAMETOOLONG, -EISDIR
 */
int sys_open(const char *path, int flags, __unused mode_t mode)
{
	int64_t inum, fd, ret;
	struct file *file;

	if ((flags & O_ACCMODE) == 0)
		return -EINVAL;
	if (flags & O_CREAT || flags & O_EXCL)
		return -EINVAL;

	inum = name_i(path);
	if (inum < 0)
		return inum;

	if (flags & O_WRONLY) {
		/* All UNIX kernels keep the write permission
		 * of directories exclusively for themselves: */
		if (is_dir(inum))
			return -EISDIR;

		/* Truncate file iff write mode was requested
		 * and the file is not a directory: */
		if (flags & O_TRUNC)
			file_truncate(inum);
	}

	file = kmalloc(sizeof(*file));
	file_init(file, inum, flags);
	fd = unrolled_insert(&current->fdtable, file);

	if (flags & O_APPEND) {
		ret = sys_lseek(fd, 0, SEEK_END);
		assert(ret > 0);
	}
	return fd;
}

int sys_close(int fd)
{
	struct file *file;

	file = unrolled_lookup(&current->fdtable, fd);
	if (file == NULL)
		return -EBADF;

	/* Take care of a concurrent close() */
	spin_lock(&file->lock);
	assert(file->refcount > 0);
	if (--file->refcount == 0)
		kfree(file);
	spin_unlock(&file->lock);

	unrolled_remove_key(&current->fdtable, fd);
	return 0;
}

int sys_fstat(int fd, struct stat *buf)
{
	struct file *file;

	file = unrolled_lookup(&current->fdtable, fd);
	if (file == NULL)
		return -EBADF;

	fill_statbuf(file->inum, buf);
	return 0;
}

/*
 * -ENOENT, -ENOTDIR, -ENAMETOOLONG
 */
int sys_stat(const char *path, struct stat *buf)
{
	uint64_t inum;

	inum = name_i(path);
	if (inum < 0)
		return inum;

	fill_statbuf(inum, buf);
	return 0;
}

/*
 * -EBADF, -EISDIR
 */
int64_t sys_read(int fd, void *buf, uint64_t count)
{
	struct file *file;
	struct inode *inode;
	int64_t read_len;

	file = unrolled_lookup(&current->fdtable, fd);
	if (file == NULL)
		return -EBADF;
	if ((file->flags & O_RDONLY) == 0)
		return -EBADF;

	assert(file->inum > 0);
	if (is_dir(file->inum))
		return -EISDIR;
	if (!is_regular_file(file->inum))
		return -EBADF;
	inode = inode_get(file->inum);

	spin_lock(&file->lock);
	read_len = file_read(inode, buf, file->offset, count);
	assert(file->offset + read_len <= inode->size_low);
	file->offset += read_len;
	spin_unlock(&file->lock);

	return read_len;
}

/*
 * -EBADF, -EISDIR, -EFBIG, -ENOSPC
 */
int64_t sys_write(int fd, void *buf, uint64_t count)
{
	struct file *file;
	struct inode *inode;
	int64_t write_len;

	file = unrolled_lookup(&current->fdtable, fd);
	if (file == NULL)
		return -EBADF;
	if ((file->flags & O_WRONLY) == 0)
		return -EBADF;

	assert(file->inum > 0);
	if (is_dir(file->inum))
		return -EISDIR;
	if (!is_regular_file(file->inum))
		return -EBADF;
	inode = inode_get(file->inum);

	spin_lock(&file->lock);
	write_len = file_write(inode, buf, file->offset, count);
	if (write_len < 0)
		goto out;
	assert(file->offset + write_len <= inode->size_low);
	file->offset += write_len;

out:	spin_unlock(&file->lock);
	return write_len;
}

/*
 * "The l in the name lseek() derives from the fact that the
 * offset argument and the return value were both originally
 * typed as long. Early UNIX implementations provided a seek()
 * system call, which typed these values as int" --M. Kerrisk
 */
int64_t sys_lseek(int fd, uint64_t offset, uint whence)
{
	struct file *file;
	struct inode *inode;
	uint64_t offset_base;
	int error = 0;

	file = unrolled_lookup(&current->fdtable, fd);
	if (file == NULL)
		return -EBADF;

	assert(file->inum > 0);
	if (is_fifo(file->inum) || is_socket(file->inum))
		return -ESPIPE;
	inode = inode_get(file->inum);

	spin_lock(&file->lock);

	switch (whence) {
	case SEEK_SET: offset_base = 0; break;
	case SEEK_CUR: offset_base = file->offset; break;
	case SEEK_END: offset_base = inode->size_low; break;
	default: error = -EINVAL; goto out;
	}

	if ((offset_base + offset) < offset_base) {
		error = -EOVERFLOW;
		goto out;
	}

	file->offset = offset_base + offset;
out:	spin_unlock(&file->lock);
	return error ? error : (int64_t)file->offset;
}
