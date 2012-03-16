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
 * Return val   : inode number, or 0 if no free inodes exist
 *
 * NOTE! There are obviously more SMP-friendly ways for doing this
 * than just grabbing a global lock reserved for inodes allocation.
 * An ‘optimistic locking’ scheme would access the inode bitmap
 * locklessly, but set the inode's ‘used/free’ bit using an atomic
 * test_bit_and_set method.  If another thread got that inode bef-
 * ore us, we continue searching the bitmap for yet another inode!
 */
STATIC __unused uint64_t inode_alloc(void)
{
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

		bitmap_set_bit(buf, first_zero_bit, isb.block_size);
		block_write(bgd->inode_bitmap, buf, 0, isb.block_size);
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
 * Block Alloc - Allocate a free data block from disk
 * Return val   : Block number, or 0 if no free blocks exist
 */
STATIC __unused uint64_t block_alloc(void)
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
	uint64_t first, last, ramdisk_len;
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
	isb.bgd = (void *)&isb.buf[EXT2_GROUP_DESC_OFFSET];
	isb.block_size = 1024U << isb.sb->log_block_size;
	isb.frag_size  = 1024U << isb.sb->log_fragment_size;

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
	if (rooti->blocks_count_low == 0 || rooti->size_low == 0)
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
