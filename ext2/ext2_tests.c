/*
 * The Second Extended File System - Test cases & Debugging methods
 *
 * Copyright (C) 2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * NOTE! The venerable e2fsprogs 'fsck' tool is one hell of a utility
 * for checking FS drivers write-support validity.  Use it often.
 */

#include <kernel.h>
#include <stdint.h>
#include <ext2.h>
#include <kmalloc.h>
#include <string.h>
#include <errno.h>
#include <ramdisk.h>
#include <unrolled_list.h>
#include <percpu.h>

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
	(*pr)(".. 512-byte Blocks count = %u blocks\n", inode->i512_blocks);
	(*pr)(".. Block number for ACL file = #%u\n", inode->file_acl);
	(*pr)(".. Data Blocks:\n");
	for (int i = 0; i < EXT2_INO_NR_BLOCKS; i++)
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
 * Given UNIX @path, put its leaf node in @child, and the dir
 * path leading to that leaf in @parent (Mini-stateful parser)
 */
enum state { NONE, SLASH, FILENAME, EOL };
static __unused void path_get_parent(const char *path, char *parent, char *child)
{
	enum state state, prev_state;
	int sub_idx;

	sub_idx = 0;
	state = NONE;
	for (int i = 0; i <= strlen(path); i++) {
		prev_state = state;
		if (path[i] == '/') {
			state = SLASH;
			assert(prev_state != SLASH);
			if (prev_state == NONE) {
				sub_idx = i + 1;
				continue;
			}
		} else if (path[i] == '\0') {
			state = EOL;
			if (prev_state == SLASH)
				continue;
		} else {
			state = FILENAME;
			if (i - sub_idx > EXT2_FILENAME_LEN)
				panic("File name in path '%s' too long", path);
		}
		if (path[i] == '/' || path[i] == '\0') {
			memcpy(child, &path[sub_idx], i - sub_idx);
			memcpy(parent, path, sub_idx);
			child[i - sub_idx] = '\0';
			parent[sub_idx] = '\0';
			sub_idx = i + 1;
		}
	}
}

/*
 * File System test cases
 */

#if EXT2_TESTS

#define TEST_INODES			0
#define TEST_BLOCK_READS		0
#define TEST_FILE_READS			0
#define TEST_DIR_ENTRIES		0
#define TEST_PATH_CONVERSION		0
#define TEST_INODE_ALLOC_DEALLOC	0
#define TEST_BLOCK_ALLOC_DEALLOC	0
#define TEST_FILE_WRITES		0
#define TEST_FILE_CREATION		0
#define TEST_FILE_TRUNCATE		0
#define TEST_FILE_DELETION		1
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

/*
 * List all files located under given directory
 */
static void __unused list_files(uint64_t dir_inum)
{
	struct inode *dir;
	struct dir_entry *dentry;
	uint64_t offset, len;
	char *name;

	dir = inode_get(dir_inum);
	dentry = kmalloc(sizeof(*dentry));
	name = kmalloc(EXT2_FILENAME_LEN + 1);

	for (offset = 0;  ; offset += dentry->record_len) {
		len = file_read(dir, (char *)dentry, offset, sizeof(*dentry));
		if (len == 0)
			break;
		if (dentry->inode_num == 0)
			continue;

		if (!dir_entry_valid(dir_inum, dentry, offset, len)) {
			(*pr)("Directory inode = %lu\n", dir_inum);
			panic("Invalid directory entry, check log!");
		}
		memcpy(name, dentry->filename, dentry->filename_len);
		name[dentry->filename_len] = '\0';
		(*pr)("File: '%s', inode = %lu\n", name, dentry->inode_num);
	}

	kfree(name);
	kfree(dentry);
}

void ext2_run_tests(void)
{
	union super_block __unused *sb;
	struct inode __unused *inode;
	struct dir_entry __unused *dentry;
	struct path_translation __unused *file;
	struct unrolled_head __unused head;
	uint64_t __unused len, block, nblocks, nfree, mode;
	int64_t __unused ilen, inum, count, parent_inum;
	char __unused *buf, *buf2, *parent, *child;

	assert(pr != NULL);
	buf = kmalloc(BUF_LEN);
	buf2 = kmalloc(BUF_LEN);
	sb = isb.sb;

	/* Extract the modified ext2 volume out of the virtual machine: */
	(*pr)("Ramdisk start at: 0x%lx, with len = %ld\n", isb.buf,
	      ramdisk_get_len());

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
		if (S_ISDIR(inode->mode)) {
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

#if TEST_INODE_ALLOC_DEALLOC
	nfree = isb.sb->free_inodes_count;
again: 	unrolled_init(&head, 64);
	for (uint i = 0; i < nfree; i++) {
		inum = inode_alloc(EXT2_FT_REG_FILE);
		if (inum == 0)
			panic("Reported free inodes count = %lu, but our "
			      "%u-th allocation returned NULL!", nfree, i);

		(*pr)("Returned inode = %lu\n", inum);
		inode = inode_get(inum);
		/* inode_dump(inode, inum, "."); */
		unrolled_insert(&head, (void *)inum);

		if (inode->links_count > 0 && inode->dtime != 0)
			panic("Allocated used inode #%lu, its links count "
			      "= %d!", inum, inode->links_count);
	}
	inum = inode_alloc(EXT2_FT_REG_FILE);
	if (inum != 0)				// Boundary case
		panic("We've allocated all %lu inodes, how can a new "
		      "allocation returns inode #%lu?", nfree, inum);

	/* Deallocate half of the allocated inodes */
	for (uint i = 0; i < nfree / 2; i++) {
		inum = (int64_t) unrolled_lookup(&head, i);
		assert(inum != 0);

		(*pr)("Deallocating inode %ld\n", inum);
		inode_dealloc(inum);
	}
	if (isb.sb->free_inodes_count != nfree / 2)
		panic("We've allocated all inodes, then deallocated %u "
		      "of them. Nonetheless, reported num of free inos "
		      "= %u instead of %u", nfree/2, isb.sb->free_inodes_count,
		      nfree / 2);

	nfree /= 2;
	unrolled_free(&head);

	if (nfree != 0) {
		(*pr)("Trying to allocate %u inodes again:\n", nfree);
		goto again;
	}
#endif

#if TEST_BLOCK_ALLOC_DEALLOC
	nfree = isb.sb->free_blocks_count;
	count = 5;
bagain:	unrolled_init(&head, 64);
	for (uint i = 0; i < nfree; i++) {
		block = block_alloc();
		if (block == 0)
			panic("Reported free blocks count = %lu, but our "
			      "%u-th allocation returned NULL!", nfree, i);

		(*pr)("Returned block = %lu as free\n", block);
		unrolled_insert(&head, (void *)block);

		(*pr)("Verifying it's not really allocated: ");
		for (uint ino = 1; ino <= isb.sb->inodes_count; ino++) {
			inode = inode_get(ino);
			if (ino < sb->first_inode &&
			    ino > EXT2_UNDELETE_DIR_INODE)
				continue;
			if (inode->links_count == 0 || inode->dtime != 0)
				continue;
			nblocks = ceil_div(inode->size_low, isb.block_size);
			nblocks = min(nblocks, (uint64_t)EXT2_INO_NR_BLOCKS);
			for (uint ino_blk = 0; ino_blk < nblocks; ino_blk++) {
				if (inode->blocks[ino_blk] == 0)
					continue;
				if (inode->blocks[ino_blk] != block)
					continue;
				(*pr)("\nInode %lu contains that block!\n", ino);
				inode_dump(inode, ino, "N/A");
				panic("Returned block %lu as free, but inode "
				      "%lu contain that block!", block, ino);
			}
		}
		(*pr)("Success!\n\n", block);
	}
	block = block_alloc();
	if (block != 0)				// Boundary case
		panic("We've allocated all %lu blocks, how can a new "
		      "allocation returns block #%lu?", nfree, block);

	/* Deallocate all of the allocated blocks */
	for (uint i = 0; i < nfree; i++) {
		block = (uint64_t)unrolled_lookup(&head, i);
		assert(block != 0);

		(*pr)("Deallocating block %ld\n", block);
		block_dealloc(block);
	}
	if (isb.sb->free_blocks_count != nfree)
		panic("We've allocated all blocks, then deallocated all of "
		      "them. Nonetheless, reported num of free blocks = %u "
		      "instead of %u", isb.sb->free_blocks_count, nfree);
	unrolled_free(&head);
	count--;

	if (count != 0) {
		(*pr)("Trying to allocate %u blocks again:\n", nfree);
		goto bagain;
	}
#endif

#if TEST_FILE_WRITES
	int64_t last_file = -1;
	for (uint j = 0; ext2_files_list[j].path != NULL; j++) {
		file = &ext2_files_list[j];
		inum = name_i(file->path);
		assert(inum > 0);
		(*pr)("Writing to file '%s': ", file->path);
		if (last_file != -1) {
			(*pr)("Ignoring file!\n");
			continue;
		}
		inode = inode_get(inum);
		if (S_ISDIR(inode->mode) || S_ISLNK(inode->mode)) {
			(*pr)("Dir or a symlnk!\n");
			continue;
		}
		inode->mode &= ~S_IFMT;
		inode->mode |= S_IFREG;
		memset32(buf, inum, BUF_LEN);
		for (uint offset = 0; offset < BUF_LEN*2; offset += BUF_LEN) {
			ilen = file_write(inode, buf, offset, BUF_LEN);
			assert(ilen != 0);
			if (ilen == -ENOSPC) {
				(*pr)("Filled the entire disk!\n");
				(*pr)("Ignoring file!");
				last_file = j;
				goto out1;
			}
			if (ilen < 0) {
				(*pr)("%s\n", errno_to_str(ilen));
				continue;
			}
			assert(ilen == BUF_LEN);
			assert(inode->size_low >= offset+ilen);
			memset32(buf, inum+1, BUF_LEN);
		}
		assert(inode->size_low >= BUF_LEN*2);
		(*pr)("Done!\n", inum);
	}
out1:
	(*pr)("**** NOW TESTING THE WRITTEN DATA!\n");

	for (uint j = 0; ext2_files_list[j].path != NULL; j++) {
		file = &ext2_files_list[j];
		inum = name_i(file->path);
		assert(inum > 0);
		inode = inode_get(inum);
		(*pr)("Verifying file '%s': ", file->path);
		if (last_file != -1 && j >= last_file) {
			(*pr)("Ignoring file!\n");
			continue;
		}
		if (S_ISDIR(inode->mode) || S_ISLNK(inode->mode)) {
			(*pr)("Dir or a symlink!\n");
			continue;
		}
		memset32(buf2, inum, BUF_LEN);
		for (uint offset = 0; offset < BUF_LEN*2; offset += BUF_LEN) {
			len = file_read(inode, buf, offset, BUF_LEN);
			if (len < BUF_LEN)
				panic("We've written %d bytes to inode %lu, but "
				      "returned only %d bytes on read", BUF_LEN,
				      inum, len);
			if (memcmp(buf, buf2, BUF_LEN) != 0) {
				(*pr)("Data written differs from what's read!\n");
				(*pr)("We've written the following into file:\n");
				buf_hex_dump(buf2, BUF_LEN);
				(*pr)("But found the following when reading:\n");
				buf_hex_dump(buf, BUF_LEN);
				panic("Data corruption detected!");
			}
			memset32(buf2, inum+1, BUF_LEN);
		}
		(*pr)("Validated!\n");
	}
#endif

#if TEST_FILE_CREATION
	/* Assure -EEXIST on all existing Ext2 volume files */
	parent = kmalloc(4096);
	child = kmalloc(EXT2_FILENAME_LEN + 1);
	for (uint j = 0; ext2_files_list[j].path != NULL; j++) {
		file = &ext2_files_list[j];
		prints("Testing Path '%s':\n", file->path);
		path_get_parent(file->path, parent, child);
		prints("Parent: '%s'\n", parent);
		prints("Child: '%s'\n", child);
		parent_inum = (*parent == '\0') ?
			(int64_t)current->working_dir : name_i(parent);
		inum = file_new(parent_inum, child, EXT2_FT_REG_FILE);
		if (inum != -EEXIST) {
			panic("File with path '%s' already exists, but "
			      "file_new allocated a new ino %u for it!",
			      file->path, inum);
		}
		(*pr)("Success: file creation returned -EEXIST\n");
	}
	kfree(parent);
	kfree(child);

	/* Now just create a random set of regular files */
	char name[3] = "00";
	for (char p = 'a'; p <= 'z'; p++)
		for (char ch = '0'; ch <= '~'; ch++) {
			name[0] = p;
			name[1] = ch;
			(*pr)("Creating new file '%s': ", name);
			inum = file_new(EXT2_ROOT_INODE, name, EXT2_FT_REG_FILE);
			if (inum < 0) {
				(*pr)("Returned %s", errno_to_str(inum));
				panic("File creation error; check log");
			}
			(*pr)("Success!\n");
		}

	/* Assure -EEXIST on recreation of files created above */
	for (char p = 'a'; p <= 'z'; p++)
		for (char ch = '0'; ch <= '~'; ch++) {
			name[0] = p;
			name[1] = ch;
			inum = file_new(EXT2_ROOT_INODE, name, EXT2_FT_DIR);
			if (inum != -EEXIST)
				panic("File '%s' already exists, but file_new "
				      "allocated a new ino %lu for it", name, inum);
		}

	/* Test directories creation */
	char pname[4], ch;
	pname[0] = '/', pname[1] = 'A', pname[3] = '\0', ch = '0';
	for (int i = 0; i < 40; i++, ch++) {
		pname[2] = ch;
		(*pr)("Starting from root directory '/':\n");
		inum = EXT2_ROOT_INODE;

		/* For each dir, create a 50-level deep dir heiararchy!! */
		for (int i = 0; i < 50; i++) {
			(*pr)("Creating new sub-dir '%s': ", pname + 1);
			inum = file_new(inum, pname + 1, EXT2_FT_DIR);
			if (inum < 0) {
				(*pr)("Returned %ld", inum);
				panic("File creation error; check log");
			}
			(*pr)("Success!\n");
		}
		(*pr)("\n");
	}

	(*pr)("Listing contents of folder /:\n");
	list_files(EXT2_ROOT_INODE);

	/* Test the contents of created directories contents */
	ch = '0';
	for (int i = 0; i < 40; i++, ch++) {
		pname[2] = ch;
		(*pr)("Listing contents of folder %s:\n", pname);
		list_files(name_i(pname));
		(*pr)("\n");
	}

	/* Boundary Case: what about files creation with long names? */
	char longname[EXT2_FILENAME_LEN + 1];
	memset(longname, 'a', EXT2_FILENAME_LEN);
	longname[EXT2_FILENAME_LEN] = '\0';
	inum = file_new(EXT2_ROOT_INODE, longname, EXT2_FT_REG_FILE);
	(*pr)("Creating file '%s', returned %ld\n", longname, inum);
	if (inum > 0)
		panic("Tried to create long file name of len %d, but it was "
		      "accepted and inode %lu returned;  ENAMETOOLONG should"
		      "'ve been returned!", EXT2_FILENAME_LEN, inum);
	longname[EXT2_FILENAME_LEN - 1] = '\0';
	inum = file_new(EXT2_ROOT_INODE, longname, EXT2_FT_REG_FILE);
	(*pr)("Creating file '%s', returned %ld\n", longname, inum);
	if (inum < 0)
		panic("Tried to create max possible len (%d) file name, but "
		      "error %ld was returned!", EXT2_FILENAME_LEN-1, inum);
#endif

#if TEST_FILE_TRUNCATE
	for (uint i = 0; ext2_files_list[i].path != NULL; i++) {
		file = &ext2_files_list[i];
		inum = name_i(file->path);
		(*pr)("Truncating file '%s': ", file->path);
		if (is_dir(inum)) {
			(*pr)("Directory!\n");
			continue;
		}

		inode = inode_get(inum);
		inode_dump(inode, inum, file->path);
		file_truncate(inum);
		assert(inode->size_low == 0);
		assert(inode->i512_blocks == 0);
		for (int i = 0; i < EXT2_INO_NR_BLOCKS; i++)
			assert(inode->blocks[i] == 0);
		(*pr)("\nSuccess!\n");
	}
#endif

#if TEST_FILE_DELETION
	parent = kmalloc(4096);
	child = kmalloc(EXT2_FILENAME_LEN + 1);
	for (uint i = 0; ext2_files_list[i].path != NULL; i++) {
		file = &ext2_files_list[i];
		(*pr)("Deleting file '%s'\n", file->path);
		inum = name_i(file->path);
		if (is_dir(inum) || is_symlink(inum)) {
			(*pr)("Directory or symlink!\n");
			continue;
		}
		path_get_parent(file->path, parent, child);
		prints("Parent: '%s'\n", parent);
		prints("Child: '%s'\n", child);
		parent_inum = (*parent == '\0') ?
			(int64_t)current->working_dir : name_i(parent);
		if (parent_inum < 0) {
			(*pr)("FAILURE: Parent pathname resolution returned "
			      "%s\n", errno_to_str(parent_inum));
			continue;
		}
		int ret = file_delete(parent_inum, child);
		if (ret < 0)
			(*pr)("FAILURE: Returned %s\n", errno_to_str(ret));
		else
			(*pr)("Success!\n");
	}
	kfree(child), kfree(parent);
#endif

	(*pr)("%s: Sucess!", __func__);
	if (pr != printk)
		printk("%s: Sucess!", __func__);

	kfree(buf);
	kfree(buf2);
}

#endif /* EXT2_TESTS */
