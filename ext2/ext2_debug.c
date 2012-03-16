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
#include <kmalloc.h>
#include <string.h>

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
void __unused buf_hex_dump(void *given_buf, uint len)
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
void __unused buf_char_dump(void *given_buf, uint len)
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

void dentry_dump(struct dir_entry *dentry)
{
	char *name;

	assert(dentry->filename_len != 0);
	assert(dentry->filename_len <= EXT2_FILENAME_LEN);
	name = kmalloc(dentry->filename_len + 1);
	memcpy(name, dentry->filename, dentry->filename_len);
	name[dentry->filename_len] = '\0';

	assert(pr != NULL);
	(*pr)("Dumping Directory Entry contents:\n");
	(*pr)(".. Inode number = %u\n", dentry->inode_num);
	(*pr)(".. Record len = %d bytes\n", dentry->record_len);
	(*pr)(".. Filename len = %d bytes\n", dentry->filename_len);
	(*pr)(".. File type = %d\n", dentry->file_type);
	(*pr)(".. Filename = '%s'\n", name);
	(*pr)("\n");
}

/*
 * File System test cases
 */

#if EXT2_TESTS

#define	TEST_INODES		0
#define TEST_BLOCK_READS	0
#define TEST_FILE_READS		0
#define TEST_DIR_ENTRIES	0
#define TEST_PATH_CONVERSION	1
#define TEST_INODE_ALLOC	0
#define EXT2_DUMP_METHOD	buf_char_dump

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

/*
 * For good testing of the code which matches Unix file pathes to
 * inodes, a comprehensive list of file  paths should be put here.
 * Check the 'ext2/files_list.c' file for further information.
 */
extern struct path_translation ext2_files_list[];

/*
 * Different paths for the Unix root inode; i.e. '/', '/..', etc.
 */
extern const char *ext2_root_list[];

void ext2_run_tests(void)
{
	union super_block __unused *sb;
	struct inode __unused *inode;
	struct dir_entry __unused *dentry;
	char *buf;
	uint64_t __unused len, inum;

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
		if (inode->links_count == 0) {
			(*pr)("Free inode!\n");
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

#if TEST_DIR_ENTRIES
	/* Most of these fields are invalid, on purpose */
	dentry = kmalloc(sizeof(*dentry));
	dentry->inode_num = 0xffffffff;
	dentry->record_len = 3;
	dentry->filename_len = 5;
	dentry->file_type = 7;
	memcpy(dentry->filename, "testFile", 8);
	dir_entry_valid(15, dentry, 10, 5);
	kfree(dentry);
#endif

#if TEST_PATH_CONVERSION
	/* Different forms of EXT2_ROOT_INODE */
	for (uint i = 0; ext2_root_list[i] != NULL; i++) {
		const char *path = ext2_root_list[i];
		inum = name_i(path);

		(*pr)("Path: '%s', Inode = %lu\n", path, inum);
		if (inum != EXT2_ROOT_INODE)
			panic("_EXT2: Path '%s' returned invalid inode #%lu",
			      path, inum);
	}

	/* Custom files list, should be manually checked */
	struct path_translation *file;
	for (uint i = 0; ext2_files_list[i].path != NULL; i++) {
		file = &ext2_files_list[i];
		file->absolute_inum = name_i(file->path);
		(*pr)("Path: '%s', Inode = %lu\n",file->path,file->absolute_inum);
	}

	/* Path file name length tests */
	char *path = kmalloc(EXT2_FILENAME_LEN + 4);
	path[0] = '/';
	char *p = &path[1];
	for (int i = 0; i < EXT2_FILENAME_LEN + 1; i++)
		p[i] = 'A';
	p[EXT2_FILENAME_LEN + 1] = '\0';	// Should warn (A)
	(*pr)("Path: '%s', Inode = %lu\n", path, name_i(path));
	for (int i = 0; i < EXT2_FILENAME_LEN; i++)
		p[i] = 'B';
	p[EXT2_FILENAME_LEN] = '\0';		// Should NOT warn (B)
	(*pr)("Path: '%s', Inode = %lu\n", path, name_i(path));
#endif

#if TEST_INODE_ALLOC
	uint64_t nfree = isb.sb->free_inodes_count;
	for (uint i = 0; i < nfree; i++) {
		inum = inode_alloc();
		if (inum == 0)
			panic("Reported free inodes count = %lu, but our "
			      "%u-th allocation returned NULL!", nfree, i);

		(*pr)("Returned inode = %ld\n", inum);
		inode = inode_get(inum);
		inode_dump(inode, inum, ".");

		if (inode->links_count > 0 && inode->dtime != 0)
			panic("Allocated used inode #%lu, its links count "
			      "= %d!", inum, inode->links_count);
	}
	inum = inode_alloc();
	if (inum != 0)				// Boundary case
		panic("We've allocated all %lu inodes, how can a new "
		      "allocation returns inode #%lu?", nfree, inum);
#endif

	(*pr)("_EXT2_TESTS: Sucess!");
	kfree(buf);
}

#endif /* EXT2_TESTS */
