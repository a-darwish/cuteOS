/*
 * The Second Extended File System
 *
 * Copyright (C) 2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * Check the 'ext2.h' header before attacking this file.
 * Inodes start from 1, while block and group indices start from 0!
 */

#include <kernel.h>
#include <stdint.h>
#include <percpu.h>
#include <errno.h>
#include <ramdisk.h>
#include <ext2.h>
#include <string.h>
#include <kmalloc.h>
#include <bitmap.h>

/*
 * In-memory Super Block
 */
struct {
	union super_block	*sb;		/* On-disk Superblock */
	struct group_descriptor *bgd;		/* On-disk Group Desc Table */
	char			*buf;		/* Ramdisk buffer */
	uint64_t		block_size;	/* 1, 2, or 4K */
	uint64_t		frag_size;	/* 1, 2, or 4K */
	uint64_t		blockgroups_count;
	uint64_t		last_blockgroup;
	spinlock_t		inode_allocation_lock;
	spinlock_t		block_allocation_lock;
} isb;

/*
 * Get the On-Disk image of inode @inum
 */
struct inode *inode_get(uint64_t inum)
{
	union super_block *sb;
	struct group_descriptor *bgd;
	struct inode *inode;
	uint64_t group, groupi, inodetbl_offset, inode_offset;

	sb = isb.sb;
	assert(inum != 0);
	group  = (inum - 1) / sb->inodes_per_group;
	groupi = (inum - 1) % sb->inodes_per_group;
	if (group >= isb.blockgroups_count || inum > sb->inodes_count)
		panic("EXT2: Inode %d out of volume range", inum);

	bgd = &isb.bgd[group];
	inodetbl_offset = bgd->inode_table * isb.block_size;
	inode_offset = inodetbl_offset + groupi * sb->inode_size;
	inode = (void *)&isb.buf[inode_offset];
	return inode;
}

static void __block_read_write(uint64_t block, char *buf, uint blk_offset,
			       uint len, enum block_op operation)
{
	uint64_t final_offset, blocks_count;

	blocks_count = isb.sb->blocks_count;
	if (block >= blocks_count)
		panic("EXT2: Block %lu is out of volume boundary\n"
		      "Volume block count = %lu blocks\n", block, blocks_count);
	if (blk_offset + len > isb.block_size)
		panic("EXT2: Block-#%lu, blk_offset=%u, len=%u access exceeds "
		      "block boundaries!", block, blk_offset, len);

	final_offset = (block * isb.block_size) + blk_offset;
	switch (operation) {
	case BLOCK_READ: memcpy(buf, &isb.buf[final_offset], len); break;
	case BLOCK_WRTE: memcpy(&isb.buf[final_offset], buf, len); break;
	};
}

/*
 * Block Read - Read given disk block into buffer
 * @block       : Disk block to read from
 * @buf         : Buffer to put the read data into
 * @blk_offset  : Offset within the block  to start reading from
 * @len         : Nr of bytes to read, starting from @blk_offset
 */
STATIC void block_read(uint64_t block, char *buf, uint blk_offset, uint len)
{
	__block_read_write(block, buf, blk_offset, len, BLOCK_READ);
}

/*
 * Block Write - Write given buffer into disk block
 * @block       : Disk block to write to
 * @buf         : Buffer of data to be written
 * @blk_offset  : Offset within the block to start writing to
 * @len         : Nr of bytes to write, starting from @blk_offset
 */
STATIC void block_write(uint64_t block, char *buf, uint blk_offset, uint len)
{
	__block_read_write(block, buf, blk_offset, len, BLOCK_WRTE);
}

/*
 * Inode Alloc - Allocate a free inode from disk
 * @type        : Type of file this inode is allocated for
 * Return val   : Inode number, or 0 if no free inodes exist
 *
 * NOTE! There are obviously more SMP-friendly ways for doing this
 * than just grabbing a global lock reserved for inodes allocation.
 * An ‘optimistic locking’ scheme would access the inode bitmap
 * locklessly, but set the inode's ‘used/free’ bit using an atomic
 * test_bit_and_set method.  If another thread got that inode bef-
 * ore us, we continue searching the bitmap for yet another inode!
 */
STATIC uint64_t inode_alloc(enum file_type type)
{
	struct inode *inode;
	struct group_descriptor *bgd;
	char *buf;
	int first_zero_bit;
	uint64_t inum;

	bgd = isb.bgd;
	buf = kmalloc(isb.block_size);
	spin_lock(&isb.inode_allocation_lock);

	for (uint i = 0; i < isb.blockgroups_count; i++, bgd++) {
		block_read(bgd->inode_bitmap, buf, 0, isb.block_size);
		first_zero_bit = bitmap_first_zero_bit(buf, isb.block_size);
		if (first_zero_bit == -1)
			continue;

		inum = i * isb.sb->inodes_per_group + first_zero_bit + 1;
		if (inum < isb.sb->first_inode)
			panic("EXT2: Reserved ino #%lu marked as free", inum);
		if (inum > isb.sb->inodes_count)
			panic("EXT2: Returned ino #%lu exceeds count", inum);

		assert(isb.sb->free_inodes_count > 0);
		assert(bgd->free_inodes_count > 0);
		isb.sb->free_inodes_count--;
		bgd->free_inodes_count--;
		if (type == EXT2_FT_DIR)
			bgd->used_dirs_count++;

		bitmap_set_bit(buf, first_zero_bit, isb.block_size);
		block_write(bgd->inode_bitmap, buf, 0, isb.block_size);

		inode = inode_get(inum);
		memset(inode, 0, sizeof(*inode));
		inode->mode |= dir_entry_type_to_inode_type(type);
		inode->mode |= EXT2_IRUSR | EXT2_IWUSR;
		inode->mode |= EXT2_IRGRP | EXT2_IWGRP;
		inode->mode |= EXT2_IROTH;
		if (type == EXT2_FT_DIR)
			inode->mode |= EXT2_IXUSR | EXT2_IXGRP | EXT2_IXOTH;
		inode->atime = inode->ctime = inode->mtime = 0xf00f;
		goto out;
	}
	assert(isb.sb->free_inodes_count == 0);
	inum = 0;
out:
	spin_unlock(&isb.inode_allocation_lock);
	kfree(buf);
	return inum;
}

/*
 * Inode Dealloc - Mark given inode as free on-disk
 * @inum        : Inode number to deallocate
 *
 * All the necessary counters are updated in the process
 */
STATIC void inode_dealloc(uint64_t inum)
{
	struct group_descriptor *bgd;
	uint64_t group, groupi;
	char *buf;

	assert(inum != 0);
	assert(inum >= isb.sb->first_inode);
	assert(inum <= isb.sb->inodes_count);

	group  = (inum - 1) / isb.sb->inodes_per_group;
	groupi = (inum - 1) % isb.sb->inodes_per_group;
	bgd = &isb.bgd[group];
	buf = kmalloc(isb.block_size);

	spin_lock(&isb.inode_allocation_lock);

	isb.sb->free_inodes_count++;
	bgd->free_inodes_count++;
	if (is_dir(inum))
		bgd->used_dirs_count--;

	block_read(bgd->inode_bitmap, buf, 0, isb.block_size);
	assert(bitmap_bit_is_set(buf, groupi, isb.block_size));
	bitmap_clear_bit(buf, groupi, isb.block_size);
	block_write(bgd->inode_bitmap, buf, 0, isb.block_size);

	spin_unlock(&isb.inode_allocation_lock);
	kfree(buf);
}

/*
 * Block Alloc - Allocate a free data block from disk
 * Return val   : Block number, or 0 if no free blocks exist
 */
STATIC uint64_t block_alloc(void)
{
	union super_block *sb;
	struct group_descriptor *bgd;
	uint64_t block, first_blk, last_blk;
	int first_zero_bit;
	char *buf;

	sb = isb.sb;
	bgd = isb.bgd;
	buf = kmalloc(isb.block_size);
	spin_lock(&isb.block_allocation_lock);

	for (uint i = 0; i < isb.blockgroups_count; i++, bgd++) {
		block_read(bgd->block_bitmap, buf, 0, isb.block_size);
		first_zero_bit = bitmap_first_zero_bit(buf, isb.block_size);
		if (first_zero_bit == -1)
			continue;

		first_blk = i * sb->blocks_per_group + sb->first_data_block;
		last_blk = (i != isb.last_blockgroup) ?
			first_blk + sb->blocks_per_group - 1 :
			sb->blocks_count - 1;
		block = first_blk + first_zero_bit;
		if (block < first_blk || block > last_blk)
			panic("EXT2: Returned block #%lu as free, although "
			      "it's outside valid [%lu,%lu] blck boundaries",
			      block, first_blk, last_blk);

		assert(isb.sb->free_blocks_count > 0);
		assert(bgd->free_blocks_count > 0);
		isb.sb->free_blocks_count--;
		bgd->free_blocks_count--;

		bitmap_set_bit(buf, first_zero_bit, isb.block_size);
		block_write(bgd->block_bitmap, buf, 0, isb.block_size);
		goto out;
	}
	assert(isb.sb->free_blocks_count == 0);
	block = 0;
out:
	spin_unlock(&isb.block_allocation_lock);
	kfree(buf);
	return block;
}

/*
 * Block Dealloc - Mark given block as free on-disk
 * @block       : Block number to deallocate
 *
 * All the necessary counters are updated in the process
 */
STATIC void block_dealloc(uint block)
{
	union super_block *sb;
	struct group_descriptor *bgd;
	uint64_t group, groupi;
	char *buf;

	sb = isb.sb;
	assert(block >= sb->first_data_block);
	assert(block < sb->blocks_count);

	group  = (block - sb->first_data_block) / sb->blocks_per_group;
	groupi = (block - sb->first_data_block) % sb->blocks_per_group;
	bgd = &isb.bgd[group];
	buf = kmalloc(isb.block_size);

	spin_lock(&isb.block_allocation_lock);

	sb->free_blocks_count++;
	bgd->free_blocks_count++;
	assert(sb->free_blocks_count <= sb->blocks_count);

	block_read(bgd->block_bitmap, buf, 0, isb.block_size);
	assert(bitmap_bit_is_set(buf, groupi, isb.block_size));
	bitmap_clear_bit(buf, groupi, isb.block_size);
	block_write(bgd->block_bitmap, buf, 0, isb.block_size);

	spin_unlock(&isb.block_allocation_lock);
	kfree(buf);
}

/*
 * File Read - Read given file into buffer
 * @inode	: File's inode, which will map us to the data blocks
 * @buf		: Buffer to put the read data into
 * @offset	: File's offset
 * @len		: Nr of bytes to read, starting from file @offset
 * Return value	: Nr of bytes read, or zero (out of boundary @offset)
 */
uint64_t file_read(struct inode *inode, char *buf, uint64_t offset, uint64_t len)
{
	uint64_t supported_area, block, blk_offset;
	uint64_t read_len, ret_len, mode;

	mode = inode->mode & EXT2_IFILE_FORMAT;
	if (mode != EXT2_IFREG && mode != EXT2_IFDIR)
		return 0;

	supported_area = isb.block_size * EXT2_INO_NR_DIRECT_BLKS;
	if (offset >= inode->size_low)
		return 0;
	if (offset + len > inode->size_low)
		len = inode->size_low - offset;
	if (offset + len > supported_area)
		len = supported_area  - offset;

	ret_len = len;
	while (len != 0) {
		block = offset / isb.block_size;
		blk_offset = offset % isb.block_size;
		read_len = isb.block_size - blk_offset;
		read_len = min(read_len, len);

		assert(block < EXT2_INO_NR_DIRECT_BLKS);
		block_read(inode->blocks[block], buf, blk_offset, read_len);

		assert(len >= read_len);
		len -= read_len;
		buf += read_len;
		offset += read_len;
		assert(offset <= inode->size_low);
		if (offset == inode->size_low)
			assert(len == 0);
	}

	return ret_len;
}

/*
 * File Write - Write given buffer into file
 * @inode       : File's inode, which will map us to the data blocks
 * @buf         : Buffer of the data to be written
 * @offset      : File's offset
 * @len         : Nr of bytes to write
 * Return value : Nr of bytes actually written, or an errno
 *
 * FIXME! How to properly update inode's i_blocks? I dont' get it!
 */
int64_t file_write(struct inode *inode, char *buf, uint64_t offset, uint64_t len)
{
	uint64_t mode, supported_area, blk_offset, last_offset;
	uint64_t write_len, ret_len, block, new;

	mode = inode->mode & EXT2_IFILE_FORMAT;
	if (mode != EXT2_IFREG && mode != EXT2_IFDIR)
		return -EBADF;

	supported_area = isb.block_size * EXT2_INO_NR_DIRECT_BLKS;
	if (offset >= supported_area || offset >= UINT32_MAX)
		return -EFBIG;
	if (offset + len > supported_area)
		len = supported_area  - offset;
	if (offset + len > UINT32_MAX)
		len = UINT32_MAX  - offset;

	ret_len = len;
	last_offset = offset + ret_len;
	while (len != 0) {
		block = offset / isb.block_size;
		blk_offset = offset % isb.block_size;
		write_len = isb.block_size - blk_offset;
		write_len = min(write_len, len);

		assert(block < EXT2_INO_NR_DIRECT_BLKS);
		if (inode->blocks[block] == 0) {
			if ((new = block_alloc()) == 0)
				return -ENOSPC;
			inode->blocks[block] = new;
		}
		block_write(inode->blocks[block], buf, blk_offset, write_len);

		assert(len >= write_len);
		len -= write_len;
		buf += write_len;
		offset += write_len;
		assert(offset <= last_offset);
		if (offset == last_offset)
			assert(len == 0);

		inode->size_low = max(inode->size_low, (uint32_t)offset);
		inode->i512_blocks = ceil_div(inode->size_low, 512);
	}

	return ret_len;
}

/*
 * Return minimum possible length of a directory entry given
 * the length of filename it holds. Minimum record length is
 * 8 bytes; each entry offset must be 4-byte aligned.
 */
static inline int dir_entry_min_len(int filename_len)
{
	return round_up(EXT2_DIR_ENTRY_MIN_LEN + filename_len,
			EXT2_DIR_ENTRY_ALIGN);
}

/*
 * Check the validity of given directory entry. Entry's @offset
 * is relative to the directory-file start.  @inum is the inode
 * of directory file holding given @dentry.  @len is the number
 * of bytes we were able read before reaching EOF.
 */
STATIC bool dir_entry_valid(uint64_t inum, struct dir_entry *dentry,
			    uint64_t offset, uint64_t read_len)
{
	if (read_len < EXT2_DIR_ENTRY_MIN_LEN) {
		printk("EXT2: Truncated dir entry (ino %lu, offset %lu); "
		       "remaining file len = %lu < 8 bytes\n", inum, offset,
		       read_len);
		return false;
	}
	if (!is_aligned(offset, EXT2_DIR_ENTRY_ALIGN)) {
		printk("EXT2: Dir entry (ino %lu) offset %lu is not "
		       "aligned on four-byte boundary\n", inum, offset);
		return false;
	}
	if (!is_aligned(dentry->record_len, EXT2_DIR_ENTRY_ALIGN)) {
		printk("EXT2: Dir entry (ino %lu, offset %lu) length %lu is "
		       "not aligned on four-byte boundary\n", inum, offset,
		       dentry->record_len);
		return false;
	}
	if (dentry->record_len < dir_entry_min_len(1)) {
		printk("EXT2: Too small dir entry (ino %lu, offset %lu) "
		       "len of %u bytes\n", inum, offset, dentry->record_len);
		return false;
	}
	if (dentry->record_len < dir_entry_min_len(dentry->filename_len)) {
		printk("EXT2: Invalid dir entry (ino %lu, offset %lu) len "
		       "= %u, while filename len = %u bytes\n", inum, offset,
		       dentry->record_len, dentry->filename_len);
		return false;
	}
	if (dentry->record_len + (offset % isb.block_size) > isb.block_size) {
		printk("EXT2: Dir entry (ino %lu, offset %lu) span multiple "
		       "blocks (entry len = %lu bytes)\n", inum, offset,
		       dentry->record_len);
		return false;
	}
	if (dentry->record_len + offset > inode_get(inum)->size_low) {
		printk("EXT2: Dir entry (ino %lu, offset %lu) goes beyond "
		       "the dir EOF (entry len = %lu, dir len = %lu)\n", inum,
		       offset, dentry->record_len, inode_get(inum)->size_low);
		return false;
	}
	if (dentry->inode_num > isb.sb->inodes_count) {
		printk("EXT2: Dir entry (ino %lu, offset %lu) ino field %lu "
		       "is out of bounds; max ino = %lu\n", inum, offset,
		       dentry->inode_num, isb.sb->inodes_count);
		return false;
	}
	return true;
}

/*
 * Search given directory for an entry with file @name.  Return such
 * entry if found, or return an entry with zero inode number in case
 * of search failure.
 *
 * @inum        : Inode for the directory to be searched
 * @name        : Wanted file name, which may not be NULL-terminated
 * @name_len    : File name len, to avoid requiring the NULL postfix
 * Return value : Caller must free() the entry's mem area after use!
 */
STATIC struct dir_entry *find_dir_entry(uint64_t inum, const char *name,
					uint name_len)
{
	struct inode *dir;
	struct dir_entry *dentry;
	uint64_t dentry_len, offset, len;

	assert(is_dir(inum));
	dir = inode_get(inum);
	dentry_len = sizeof(struct dir_entry);
	dentry = kmalloc(dentry_len);

	if (name_len == 0 || name_len > EXT2_FILENAME_LEN)
		goto notfound;

	assert(name != NULL);
	for (offset = 0;  ; offset += dentry->record_len) {
		len = file_read(dir, (char *)dentry, offset, dentry_len);
		if (len == 0)			/* EOF */
			goto notfound;
		if (!dir_entry_valid(inum, dentry, offset, len))
			goto notfound;
		if (dentry->inode_num == 0)	/* Unused entry */
			continue;
		if (dentry->filename_len != name_len)
			continue;
		if (memcmp(dentry->filename, name, name_len))
			continue;
		goto found;
	}

notfound:
	dentry->inode_num = 0;
found:
	return dentry;
}

/*
 * Create a new file entry in given parent directory. Check file_new()
 * for parameters documentation.  @entry_inum is the allocated inode
 * number for the new file.
 *
 * NOTE! This increments the entry's destination inode links count
 */
static int64_t __file_new(uint64_t parent_inum, uint64_t entry_inum,
			  const char *name, enum file_type type)
{
	struct dir_entry *dentry, *lastentry, *newentry;
	struct inode *dir, *inode;
	uint64_t offset, blk_offset, newentry_len, len;
	int64_t ret, filename_len;
	char *zeroes;

	assert(parent_inum != 0);
	assert(entry_inum != 0);
	assert(is_dir(parent_inum));
	assert(name != NULL);
	assert(type == EXT2_FT_REG_FILE || type == EXT2_FT_DIR);

	filename_len = strnlen(name, EXT2_FILENAME_LEN - 1);
	if (name[filename_len] != '\0')
		return -ENAMETOOLONG;
	if (filename_len == 0)
		return -ENOENT;

	dentry = find_dir_entry(parent_inum, name, filename_len);
	if (dentry->inode_num != 0) {
		ret = -EEXIST;
		goto free_dentry1;
	}

	/*
	 * Find the parent dir's last entry: our new entry will get
	 * appended after it.  The parent directory might still be
	 * empty, with no entries at all!
	 */

	dir = inode_get(parent_inum);
	lastentry = kmalloc(sizeof(*lastentry));
	memset(lastentry, 0, sizeof(*lastentry));
	for (offset = 0;  ; offset += lastentry->record_len) {
		len = file_read(dir,(char*)lastentry,offset,sizeof(*lastentry));
		if (len == 0)
			break;
		if (!dir_entry_valid(parent_inum, lastentry, offset, len)) {
			ret = -EIO;
			goto free_dentry2;
		}
	}

	/*
	 * If a last entry was found, we need to either:
	 * - overwite it if it's already undefined (has a 0 inode)
	 * - or trim it to the minimum possible size since the last
	 *   entry is usually extended to block end; such extension
	 *   is needed to avoid making FS dir entries iterator read
	 *   corrupt data (dir sizes are always in terms of blocks)
	 */

	if (offset == 0)
		goto no_lastentry;

	if (lastentry->inode_num == 0) {
		offset -= lastentry->record_len;
	} else {
		offset -= lastentry->record_len;
		lastentry->record_len=dir_entry_min_len(lastentry->filename_len);
		file_write(dir,(char *)lastentry,offset,lastentry->record_len);
		offset += lastentry->record_len;
	}

no_lastentry:

	/*
	 * If our new dir entry spans multiple blocks, we'll extend the
	 * last dir entry size and put our new one in a shiny new block!
	 */

	newentry_len = dir_entry_min_len(filename_len);
	blk_offset = offset % isb.block_size;
	if (newentry_len + blk_offset > isb.block_size) {
		assert(offset > lastentry->record_len);
		offset -= lastentry->record_len;
		blk_offset = offset % isb.block_size;
		lastentry->record_len = isb.block_size - blk_offset;
		assert(lastentry->record_len >=
		       dir_entry_min_len(lastentry->filename_len));
		file_write(dir, (char *)lastentry, offset, lastentry->record_len);
		offset += lastentry->record_len;
		assert(offset % isb.block_size == 0);
	}

	blk_offset = offset % isb.block_size;
	assert(newentry_len + blk_offset <= isb.block_size);

	/*
	 * Now we have a guarantee: the new dir entry is not spanning
	 * multiple blocks; write it on disk!
	 *
	 * Our new entry will be the last dir entry, thus its size
	 * need to get extended to block end. This prevents any later
	 * dir entries traversal from parsing uninitialized data.
	 */

	zeroes = kmalloc(isb.block_size);
	memset(zeroes, 0, isb.block_size);
	newentry = kmalloc(sizeof(*newentry));
	newentry->inode_num = entry_inum;
	newentry->record_len = isb.block_size - blk_offset;
	newentry->filename_len = filename_len;
	newentry->file_type = type;
	assert(filename_len < EXT2_FILENAME_LEN);
	memcpy(newentry->filename, name, filename_len);
	newentry->filename[filename_len] = '\0';	/*for 'fsck'*/
	ret = file_write(dir, (char *)newentry, offset, newentry_len);
	if (ret < 0)
		goto free_dentry3;
	assert(newentry->record_len >= newentry_len);	/*dst*/
	assert(newentry->record_len - newentry_len <= isb.block_size);	/*src*/
	ret = file_write(dir, zeroes, offset  + newentry_len,
			 newentry->record_len - newentry_len);
	if (ret < 0)
		goto free_dentry3;
	assert(dir_entry_valid(parent_inum, newentry, offset, newentry_len));

	/*
	 * Update entry's inode statistics
	 */

	inode = inode_get(entry_inum);
	spin_lock(&isb.inode_allocation_lock);
	inode->links_count++;
	spin_unlock(&isb.inode_allocation_lock);

	ret = entry_inum;
free_dentry3:
	kfree(zeroes);
	kfree(newentry);
free_dentry2:
	kfree(lastentry);
free_dentry1:
	kfree(dentry);
	return ret;
}

/*
 * Create and initialize a new file in given directory
 * @parent_inum : Parent directory where the file will be located
 * @name        : File's name
 * @type        : File's type, in Ext2 dir entry type format
 * Return value : New file's inode number, or an errno
 */
int64_t file_new(uint64_t parent_inum, const char *name, enum file_type type)
{
	int64_t inum, ret;

	inum = inode_alloc(type);
	if (inum == 0)
		return -ENOSPC;

	ret = __file_new(parent_inum, inum, name, type);
	if (ret < 0)
		goto dealloc_inode;

	if (type == EXT2_FT_DIR) {
		ret = __file_new(inum, inum, ".", EXT2_FT_DIR);
		if (ret < 0)
			goto dealloc_inode;
		ret = __file_new(inum, parent_inum, "..", EXT2_FT_DIR);
		if (ret < 0)
			goto dealloc_inode;
	}

	return inum;
dealloc_inode:
	inode_dealloc(inum);
	return ret;
}

/*
 * Deallocate given indirect, double, or triple indirect data block, and
 * each of its mapped blocks. An indirect block entry maps a plain block,
 * a double indirect block entry maps an indirect block, and a triple
 * indirect block entry maps a double indirect block, thus the recursion.
 *
 * NOTE! This is a recursive depth-first block-tree traversal
 *
 * @block       : indirect, double, or triple indir block to deallocate
 * @level       : "Single", "Double", or "Triple" level of indirection
 */
static void indirect_block_dealloc(uint64_t block, enum indirection_level level)
{
	char *buf;
	uint32_t *entry;
	int entries_count;

	if (block == 0)
		return;

	assert(level >= 0);
	assert(level < INDIRECTION_LEVEL_MAX);
	if (level == 0) {
		block_dealloc(block);
		return;
	}

	buf = kmalloc(isb.block_size);
	entries_count = isb.block_size / sizeof(*entry);
	block_read(block, buf, 0, isb.block_size);

	for (entry = (uint32_t *)buf; entries_count--; entry++) {
		if (*entry != 0)
			indirect_block_dealloc(*entry, level - 1);
	}
	assert((char *)entry == buf + isb.block_size);

	block_dealloc(block);
	kfree(buf);
}

/*
 * File Truncate - Truncate given file to zero (0) bytes
 * @inum	: File's inode, which will map us to the data blocks
 */
void file_truncate(uint64_t inum)
{
	struct inode *inode;

	assert(inum != 0);
	assert(inum >= isb.sb->first_inode);
	assert(inum <= isb.sb->inodes_count);
	assert(is_regular_file(inum));
	inode = inode_get(inum);

	inode->size_low = 0;
	inode->i512_blocks = 0;
	for (int i = 0; i < EXT2_INO_NR_DIRECT_BLKS; i++) {
		if (inode->blocks[i] == 0)
			continue;
		block_dealloc(inode->blocks[i]);
		inode->blocks[i] = 0;
	}

	indirect_block_dealloc(inode->blocks[EXT2_INO_INDIRECT], SINGLE_INDIR);
	inode->blocks[EXT2_INO_INDIRECT] = 0;

	indirect_block_dealloc(inode->blocks[EXT2_INO_DOUBLEIN], DOUBLE_INDIR);
	inode->blocks[EXT2_INO_DOUBLEIN] = 0;

	indirect_block_dealloc(inode->blocks[EXT2_INO_TRIPLEIN], TRIPLE_INDIR);
	inode->blocks[EXT2_INO_TRIPLEIN] = 0;
}

/*
 * Namei - Find the inode of given file path
 * path         : Absolute, hierarchial, UNIX format, file path
 * return value : Inode num, or -ENOENT, -ENOTDIR, -ENAMETOOLONG
 */
int64_t name_i(const char *path)
{
	const char *p1, *p2;
	struct dir_entry *dentry;
	uint64_t inum, prev_inum;

	assert(path != NULL);
	switch (*path) {
	case '\0':
		inum = 0; break;
	case '/':
		inum = EXT2_ROOT_INODE; break;
	default:
		inum = current->working_dir;
		assert(inum != 0);
	}

	p1 = p2 = path;
	while (*p2 != '\0' && inum != 0) {
		prev_inum = inum;
		if (*p2 == '/') {
			if (!is_dir(prev_inum))
				return -ENOTDIR;
			while (*p2 == '/')
				p1 = ++p2;
		}
		if (*p2 == '\0')
			break;

		while (*p2 != '\0' && *p2 != '/' && p2 - p1 < EXT2_FILENAME_LEN)
			p2++;
		if (*p2 != '\0' && *p2 != '/')
			return -ENAMETOOLONG;

		assert(is_dir(prev_inum));
		dentry = find_dir_entry(prev_inum, p1, p2 - p1);
		inum = dentry->inode_num;
		kfree(dentry);
	}

	return inum ? (int64_t)inum : -ENOENT;
}

/*
 * Mount the ramdisk File System
 */
void ext2_init(void)
{
	union super_block *sb;
	struct group_descriptor *bgd;
	struct inode *rooti;
	int bits_per_byte;
	uint64_t first, last, ramdisk_len, bgd_start;
	uint64_t inodetbl_size, inodetbl_blocks, inodetbl_last_block;

	/* Ramdisk sanity checks */
	ramdisk_len = ramdisk_get_len();
	if (ramdisk_len == 0)
		return;
	if (ramdisk_len < EXT2_MIN_FS_SIZE) {
		printk("FS: Loaded ramdisk is too small for an EXT2 volume!\n");
		return;
	}

	ext2_debug_init(prints);
	spin_init(&isb.inode_allocation_lock);
	spin_init(&isb.block_allocation_lock);

	/* In-Memory Super Block init */
	isb.buf = ramdisk_get_buf();
	isb.sb  = (void *)&isb.buf[EXT2_SUPERBLOCK_OFFSET];
	isb.block_size = 1024U << isb.sb->log_block_size;
	isb.frag_size  = 1024U << isb.sb->log_fragment_size;
	bgd_start = ceil_div(EXT2_SUPERBLOCK_OFFSET+sizeof(*sb),isb.block_size);
	isb.bgd = (void *)&isb.buf[bgd_start * isb.block_size];

	sb = isb.sb;
	bgd = isb.bgd;
	bits_per_byte = 8;
	if (sb->blocks_count * isb.block_size > ramdisk_len)
		panic("FS: Truncated EXT2 volume image!");

	/* Superblock sanity checks */
	if (sb->magic_signature != EXT2_SUPERBLOCK_MAGIC)
		panic("FS: Loaded image is not an EXT2 file system!");
	if (sb->revision_level != EXT2_DYNAMIC_REVISION)
		panic("Ext2: Obsolete, un-supported, file system version!");
	if (sb->state != EXT2_VALID_FS)
		panic("Ext2: Erroneous file system state; run fsck!");
	if (!is_aligned(sb->inode_size, 2))
		panic("Ext2: Invalid inode size = %d bytes!", sb->inode_size);
	if (sb->inode_size > isb.block_size)
		panic("Ext2: Inode size > file system block size!");
	if (isb.block_size != isb.frag_size)
		panic("Ext2: Fragment size is not equal to block size!");
	if (isb.block_size > EXT2_MAX_BLOCK_LEN)
		panic("Ext2: Huge block size of %ld bytes!", isb.block_size);
	if (sb->blocks_per_group > isb.block_size * bits_per_byte)
		panic("Ext2: Block Groups block bitmap must fit in 1 block!");
	if (sb->inodes_per_group > isb.block_size * bits_per_byte)
		panic("Ext2: Block Groups inode bitmap must fit in 1 block!");
	if (sb->blocks_per_group == 0)
		panic("Ext2: A Block Group cannot have 0 blocks!");
	if (sb->inodes_per_group == 0)
		panic("Ext2: A Block Group cannot have 0 inodes!");
	superblock_dump(sb);

	/* Use ceil division: the last Block Group my have a
	 * smaller number of blocks than the rest!  */
	isb.blockgroups_count = ceil_div(sb->blocks_count -
		sb->first_data_block, sb->blocks_per_group);
	isb.last_blockgroup = isb.blockgroups_count - 1;
	inodetbl_size = sb->inodes_per_group * sb->inode_size;
	inodetbl_blocks = ceil_div(inodetbl_size, isb.block_size);

	/* Block Group Descriptor Table sanity checks */
	if (isb.blockgroups_count > 1 &&	// Last group special case
	    sb->blocks_per_group > sb->blocks_count)
		panic("Ext2: Block Groups num of blocks > all disk ones!");
	if (sb->inodes_per_group > sb->inodes_count)
		panic("Ext2: Block Groups num of inodes > all disk ones!");
	for (uint i = 0; i < isb.blockgroups_count; i++) {
		first = i * sb->blocks_per_group + sb->first_data_block;
		if (i == isb.last_blockgroup)
			last = sb->blocks_count - 1;
		else
			last  = first + sb->blocks_per_group - 1;
		inodetbl_last_block = bgd->inode_table + inodetbl_blocks - 1;
		if (bgd->block_bitmap < first || bgd->block_bitmap > last)
			panic("EXT2: Group %d bitmap block out of range", i);
		if (bgd->inode_bitmap < first || bgd->inode_bitmap > last)
			panic("EXT2: Group %d inode bitmap out of range", i);
		if (bgd->inode_table < first || bgd->inode_table > last)
			panic("EXT2: Group %d inode table  out of range", i);
		if (inodetbl_last_block < first || inodetbl_last_block > last)
			panic("EXT2: Group %d i-table end block out of range", i);
		if (bgd->free_blocks_count > sb->blocks_per_group)
			panic("EXT2: Group %d free blocks count out of range", i);
		if (bgd->free_inodes_count > sb->inodes_per_group)
			panic("EXT2: Group %d free inodes count out of range", i);
		if (bgd->used_dirs_count > sb->inodes_per_group)
			panic("EXT2: Group %d used dirs count out of range", i);
		blockgroup_dump(i, bgd, first, last, inodetbl_blocks);
		bgd++;
	}

	/* Root Inode sanity checks */
	rooti = inode_get(EXT2_ROOT_INODE);
	if (!is_dir(EXT2_ROOT_INODE))
		panic("EXT2: Root inode ('/') is not a directory!");
	if (rooti->i512_blocks == 0 || rooti->size_low == 0)
		panic("EXT2: Root inode ('/') size = 0 bytes!");
	if (name_i("/.") != EXT2_ROOT_INODE)
		panic("EXT2: Corrupt root directory '.'  entry!");
	if (name_i("/..") != EXT2_ROOT_INODE)
		panic("EXT2: Corrupt root directory '..' entry!");
	inode_dump(rooti, EXT2_ROOT_INODE, "/");

	sb->volume_label[EXT2_LABEL_LEN - 1] = '\0';
	printk("Ext2: Passed all sanity checks!\n");
	printk("EXT2: File system label is `%s'\n", sb->volume_label);
}
