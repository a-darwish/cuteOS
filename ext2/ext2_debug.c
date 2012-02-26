/*
 * The Second Extended File System - Test cases & Debugging methods
 *
 * Copyright (C) 2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <stdint.h>
#include <ext2.h>

/*
 * Desired printf()-like method to use for dumping FS state.
 * Useful for directing output to either serial port or VGA.
 */
static void (*pr)(const char *fmt, ...);

void ext2_debug_init(void (*printf)(const char *fmt, ...))
{
	pr = printf;
}

/*
 * Print @given_buf, with length of @len bytes, in the format:
 *	$ od --format=x1 --address-radix=none --output-duplicates
 */
static __unused void buf_hex_dump(void *given_buf, uint len)
{
	unsigned int bytes_perline = 16, n = 0;
	uint8_t *buf = given_buf;

	assert(pr  != NULL);
	assert(buf != NULL);

	for (uint i = 0; i < len; i++) {
		(*pr)(" ");
		if (buf[i] < 0x10)
			(*pr)("0");
		(*pr)("%x", buf[i]);

		n++;
		if (n == bytes_perline || i == len - 1) {
			(*pr)("\n");
			n = 0;
		}
	}
}

/*
 * Print @given_buf as ASCII text.
 */
static __unused void buf_char_dump(void *given_buf, uint len)
{
	char *buf = given_buf;

	assert(pr  != NULL);
	assert(buf != NULL);

	for (uint i = 0; i < len; i++)
		(*pr)("%c", buf[i]);
}

void superblock_dump(union super_block *sb)
{
	assert(pr != NULL);
	sb->volume_label[EXT2_LABEL_LEN - 1] = '\0';
	sb->last_mounted[EXT2_LAST_MNT_LEN - 1] = '\0';
	(*pr)("Dumping Superblock contents:\n");
	(*pr)(".. Inodes count = %u inode\n", sb->inodes_count);
	(*pr)(".. Blocks count = %u block\n", sb->blocks_count);
	(*pr)(".. Reserved blocks count = %u block\n", sb->r_blocks_count);
	(*pr)(".. Free blocks count = %u block\n", sb->free_blocks_count);
	(*pr)(".. Free inodes count = %u inode\n", sb->free_inodes_count);
	(*pr)(".. First data block = #%u\n", sb->first_data_block);
	(*pr)(".. Block size = %u bytes\n", 1024U << sb->log_block_size);
	(*pr)(".. Fragment size = %u bytes\n", 1024U << sb->log_fragment_size);
	(*pr)(".. Blocks per group = %u block\n", sb->blocks_per_group);
	(*pr)(".. Fragments per group = %u frag\n", sb->frags_per_group);
	(*pr)(".. Inodes per group = %u inode\n", sb->inodes_per_group);
	(*pr)(".. Latest mount time = 0x%x\n", sb->mount_time);
	(*pr)(".. Latest write access = 0x%x\n", sb->write_time);
	(*pr)(".. Number of mounts since last fsck = %d\n", sb->mount_count);
	(*pr)(".. Max num of mounts before fsck = %d\n", sb->max_mount_count);
	(*pr)(".. FS Magic value = 0x%x\n", sb->magic_signature);
	(*pr)(".. FS State = %d\n", sb->state);
	(*pr)(".. Error behaviour = %d\n", sb->errors_behavior);
	(*pr)(".. Minor revision = %d\n", sb->minor_revision);
	(*pr)(".. Last time of fsck = 0x%x\n", sb->last_check);
	(*pr)(".. Time allowed between fscks = 0x%x\n", sb->check_interval);
	(*pr)(".. Creator OS = %u\n", sb->creator_os);
	(*pr)(".. Revision level = %u\n", sb->revision_level);
	(*pr)(".. UID for reserved blocks = %d\n", sb->reserved_uid);
	(*pr)(".. GID for reserved blocks = %d\n", sb->reserved_gid);
	(*pr)(".. First non-reserved inode = %u\n", sb->first_inode);
	(*pr)(".. Inode size = %d bytes\n", sb->inode_size);
	(*pr)(".. Block group # hosting this super: %d\n", sb->block_group);
	(*pr)(".. Compatible features bitmask = 0x%x\n", sb->features_compat);
	(*pr)(".. Incompatible features mask = 0x%x\n", sb->features_incompat);
	(*pr)(".. RO-compatible features = 0x%x\n", sb->features_ro_compat);
	(*pr)(".. Volume label = `%s'\n", sb->volume_label);
	(*pr)(".. Directory path of last mount = `%s'\n", sb->last_mounted);
	(*pr)("\n");
}

void blockgroup_dump(int idx, struct group_descriptor *bgd,
		     uint32_t firstb, uint32_t lastb, uint64_t inodetbl_blocks)
{
	assert(pr != NULL);
	(*pr)("Group #%d: (Blocks %u-%u)\n", idx, firstb, lastb);
	(*pr)(".. Block bitmap at %u\n", bgd->block_bitmap);
	(*pr)(".. Inode bitmap at %u\n", bgd->inode_bitmap);
	(*pr)(".. Inode table at %u-%u\n", bgd->inode_table,
	       bgd->inode_table + inodetbl_blocks - 1);
	(*pr)(".. %d free blocks, %d free inodes, %d directories\n",
	       bgd->free_blocks_count, bgd->free_inodes_count,
	       bgd->used_dirs_count);
	(*pr)("\n");
}

void inode_dump(struct inode *inode, uint64_t inum, const char *path)
{
	assert(pr != NULL);
	(*pr)("Dumping inode contents, #%d, for '%s':\n", inum, path);
	(*pr)(".. Mode = 0x%x, Flags = 0x%x\n", inode->mode, inode->flags);
	(*pr)(".. UID = %d, GID = %d\n", inode->uid, inode->gid_low);
	(*pr)(".. Last time this inode was accessed = 0x%x\n", inode->atime);
	(*pr)(".. Last time this inode was modified = 0x%x\n", inode->mtime);
	(*pr)(".. Time when this inode was deleted = 0x%x\n", inode->dtime);
	(*pr)(".. Links count = %d links\n", inode->links_count);
	(*pr)(".. File size = %d bytes\n", inode->size_low);
	(*pr)(".. 512-byte Blocks count = %u blocks\n", inode->blocks_count_low);
	(*pr)(".. Block number for ACL file = #%u\n", inode->file_acl);
	(*pr)(".. First 12 Data Blocks: ");
	for (int i = 0; i < 12; i++)
		(*pr)("%u ", inode->blocks[i]);
	(*pr)("\n\n");
}

/*
 * File System test cases
 */

#if EXT2_TESTS

#define	TEST_INODES		0
#define TEST_BLOCK_READS	0
#define TEST_FILE_READS		0
#define EXT2_DUMP_METHOD	buf_char_dump

#include <kmalloc.h>

extern struct {
	union super_block	*sb;		/* On-disk Superblock */
	struct group_descriptor *bgd;		/* On-disk Group Desc Table */
	char			*buf;		/* Ramdisk buffer */
	uint64_t		block_size;	/* 1, 2, or 4K */
	uint64_t		frag_size;	/* 1, 2, or 4K */
	uint64_t		blockgroups_count;
	uint64_t		last_blockgroup;
} isb;

enum {
	BUF_LEN			= 4096,
};

void ext2_run_tests(void)
{
	union super_block __unused *sb;
	struct inode __unused *inode;
	char *buf;
	int __unused len;

	assert(pr != NULL);
	buf = kmalloc(BUF_LEN);
	sb = isb.sb;

#if TEST_INODES
	/* All volume inodes! */
	for (uint i = 1; i <= sb->inodes_count; i++) {
		inode = inode_get(i);
		inode_dump(inode, i, "");
	}
#endif

#if TEST_BLOCK_READS
	/* All possible permumations: Burn, baby, Burn! */
	for (uint i = 0; i < sb->blocks_count; i++)
		for (uint off = 0; off < isb.block_size; off++)
			for (uint len = 0; len <= (isb.block_size - off); len++) {
				(*pr)("Reading Block #%d, offset = %d, "
				      "len = %d:\n", i, off, len);
				block_read(i, buf, off, len);
				EXT2_DUMP_METHOD(buf, len);
			}
#endif

#if TEST_FILE_READS
	for (uint i = 1; i <= sb->inodes_count; i++) {
		(*pr)("Trying inode #%u: ", i);
		inode = inode_get(i);
		if ((inode->mode & EXT2_IFILE_FORMAT) == EXT2_IFDIR) {
			(*pr)("Directory!\n");
			continue;
		}
		len = file_read(inode, buf, 0, BUF_LEN);
		if (len == 0) {
			(*pr)("No data!\n");
			continue;
		}
		(*pr)("It contains data:\n", i);
		EXT2_DUMP_METHOD(buf, len);
		(*pr)("\n");
	}
#endif

	kfree(buf);
}

#endif /* EXT2_TESTS */
