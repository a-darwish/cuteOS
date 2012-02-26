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
#include <ramdisk.h>
#include <ext2.h>
#include <string.h>

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
} isb;

/*
 * Block Read - Read given block into buffer
 * @block	: Block to read from
 * @buf         : Buffer to put the read data into
 * @blk_offset  : Offset within the block  to start reading from
 * @len         : Nr of bytes to read, starting from @blk_offset
 */
void block_read(uint64_t block, char *buf, uint blk_offset, uint len)
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
	memcpy(buf, &isb.buf[final_offset], len);
}

/*
 * File Read - Read given file into buffer
 * @inode	: File's inode, which will map us to the data blocks
 * @buf		: Buffer to put the read data into
 * @offset	: File's offset
 * @len		: Nr of bytes to read, starting from file @offset
 * Return value	: Nr of bytes read, or zero (out of boundary @offset)
 */
uint64_t __unused file_read(struct inode *inode, char *buf,
			    uint64_t offset, uint64_t len)
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
		panic("EXT2: Asked to read file offset=%lu, len = %lu\n"
		      "But Indirect blocks access is not yet supported!",
		      offset, len);

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
	if ((rooti->mode & EXT2_IFILE_FORMAT) != EXT2_IFDIR)
		panic("EXT2: Root inode is not a directory!");
	if (rooti->blocks_count_low == 0 || rooti->size_low == 0)
		panic("EXT2: Root inode size = 0 bytes!");
	inode_dump(rooti, EXT2_ROOT_INODE, "/");

	sb->volume_label[EXT2_LABEL_LEN - 1] = '\0';
	printk("Ext2: Passed all sanity checks!\n");
	printk("EXT2: File system label is `%s'\n", sb->volume_label);
}
