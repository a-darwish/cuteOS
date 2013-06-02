#if 0
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
 *
 * FIXME: Handle FS calls (open, unlink, stat, etc) with a file name of
 * the empty string "".
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
	struct inode *inode;	/* Inode# of the open()-ed file */
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
 * Get start position of leaf node in given Unix path string.
 * A return value of zero indicates unavailability of a leaf
 * node (e.g. '/', '//', '///', ...) or a relative path.
 *
 * path         : Hierarchial, UNIX format, file path
 * leaf_type    : Return val, type of leaf node (reg file or dir)
 */
FSTATIC uint path_get_leaf(const char *path, mode_t *leaf_type)
{
	enum parsing_state state, prev_state;
	int leaf_start;

	leaf_start = 0;
	*leaf_type = 0;
	state = START;
	for (int i = 0; i <= strlen(path); i++) {
		prev_state = state;
		switch(path[i]) {
		case '/':
			state = SLASH; break;
		case '\0':
			state = EOL;
			switch (prev_state) {
			case SLASH:    *leaf_type = S_IFDIR; break;
			case FILENAME: *leaf_type = S_IFREG; break;
			default: assert(false);
			} break;
		default:
			state = FILENAME;
			switch(prev_state) {
			case START: case SLASH: leaf_start = i; break;
			case FILENAME: break;
			default: assert(false);
			} break;
		}
	}
	assert((*leaf_type & S_IFMT) != 0);
	return leaf_start;
}

/*
 * Represent given Unix path in the form of:
 * - inode number of the leaf node's parent (return value)
 * - a string of the leaf node file name (return param)
 *
 * @flags	: Accept the path being a directory?
 */
enum { NO_DIR = 0x1, OK_DIR = 0x2, };
static int64_t path_parent_child(const char *path, const char **child, int flags)
{
	int64_t parent_inum; uint max_len, leaf_idx;
	mode_t leaf_type;

	max_len = PAGE_SIZE;
	leaf_idx = path_get_leaf(path, &leaf_type);
	if ((flags & NO_DIR) && S_ISDIR(leaf_type))
		return -EISDIR;
	if (path[0] == '/')
		assert(leaf_idx != 0);
	if (leaf_idx == 0)
		parent_inum = current->working_dir;
	else if (leaf_idx >= max_len)
		return -ENAMETOOLONG;
	else {
		char *parent = kmalloc(leaf_idx + 1);
		memcpy(parent, path, leaf_idx);
		parent[leaf_idx] = '\0';
		parent_inum = name_i(parent);
		kfree(parent);
		if (parent_inum < 0)
			return parent_inum;
	}
	*child = &path[leaf_idx];
	return parent_inum;
}

/*
 * -EINVAL, -EEXIST, -ENOENT, -ENOTDIR, -ENAMETOOLONG, -EISDIR
 */
int sys_open(const char *path, int flags, __unused mode_t mode)
{
	int64_t parent_inum, inum, fd;
	struct file *file;
	const char *child;

	if ((flags & O_ACCMODE) == 0)
		return -EINVAL;
	if ((flags & O_TRUNC) &&
	    (flags & O_APPEND || (flags & O_WRONLY) == 0))
		return -EINVAL;

	inum = name_i(path);
	if (flags & O_CREAT) {
		if (inum > 0 && (flags & O_EXCL))
			return -EEXIST;
		if (inum == -ENOENT) {
			parent_inum = path_parent_child(path, &child, NO_DIR);
			inum = (parent_inum < 0) ? parent_inum :
				file_new(parent_inum, child, EXT2_FT_REG_FILE);
		}
	}
	if (inum < 0)
		return inum;
	if (is_dir(inum))
		return -EISDIR;

	file = kmalloc(sizeof(*file));
	file_init(file, inum, flags);
	fd = unrolled_insert(&current->fdtable, file);
	if (flags & O_TRUNC)
		file_truncate(inum);
	if (flags & O_APPEND)
		assert(sys_lseek(fd, 0, SEEK_END) > 0);
	return fd;
}

int sys_creat(const char *path, __unused mode_t mode)
{
	return sys_open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);
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

int sys_unlink(const char *path)
{
	int64_t parent_inum;
	const char *child;

	parent_inum = path_parent_child(path, &child, NO_DIR);
	if (parent_inum < 0)
		return parent_inum;

	return file_delete(parent_inum, child);
}

int sys_link(const char *oldpath, const char *newpath)
{
	int64_t inum, parent_inum;
	const char *child;
	struct inode *inode;

	parent_inum = path_parent_child(newpath, &child, OK_DIR);
	if (parent_inum < 0)
		return parent_inum;
	inum = name_i(oldpath);
	if (inum < 0)
		return inum;
	inode = inode_get(inum);

	return ext2_new_dir_entry(parent_inum, inum, child,
				  inode_mode_to_dir_entry_type(inode->mode));
}
#endif
