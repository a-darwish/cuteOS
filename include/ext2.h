#ifndef _EXT2_H
#define _EXT2_H

/*
 * The Second Extended File System
 *
 * Copyright (C) 2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>
#include <stdint.h>
#include <tests.h>

enum {
	EXT2_SUPERBLOCK_SIZE	= 1024,
	EXT2_SUPERBLOCK_MAGIC	= 0xEF53,	/* EF'S' */
	EXT2_MIN_FS_SIZE	= (60*1024),	/* 60-KB */

	// Below offsets are out of disk start
	EXT2_SUPERBLOCK_OFFSET	= 1024,		/* First Kilobyte */
	EXT2_GROUP_DESC_OFFSET  = 2048,		/* Second Kilobyte */

	EXT2_LABEL_LEN		= 16,		/* A NULL suffix __may__ exist */
	EXT2_FILENAME_LEN	= 255,		/* Max filenme length, no NULL */
	EXT2_LAST_MNT_LEN	= 64,		/* Path when FS was Last mnted */
	EXT2_MAX_BLOCK_LEN	= 4096,		/* Max supported FS block size */

	EXT2_DIR_ENTRY_MIN_LEN	= 8,		/* 4 ino, 2 rec_len, 2 namelen */
	EXT2_DIR_ENTRY_ALIGN	= 4,		/* alignmnt for entries, bytes */

	EXT2_INO_NR_BLOCKS	= 15,		/* Data blocks mapped by inode */
	EXT2_INO_NR_DIRECT_BLKS	= 12,		/* Nr of inode 'direct' blocks */
};

/*
 * Superblock `revision level'
 */
enum {
	EXT2_GOOD_OLD_REVISION	= 0,		/* Unsupported */
	EXT2_DYNAMIC_REVISION	= 1,
};

/*
 * Superblock `state' field
 */
enum {
	EXT2_VALID_FS		= 1,		/* Unmounted cleanly */
	EXT2_ERROR_FS		= 2,		/* Errors detected */
};

/*
 * Reserved Inode Numbers
 */
enum {
	EXT2_BAD_INODE		= 1,		/* Bad blocks inode */
	EXT2_ROOT_INODE		= 2,		/* Inode for the root '/' */
	EXT2_ACL_IDX_INODE	= 3,		/* ACL Index (deprecated) */
	EXT2_ACL_DATA_INODE	= 4,		/* ACL Data (deprecated) */
	EXT2_BOOTLOADER_INODE	= 5,		/* ??? */
	EXT2_UNDELETE_DIR_INODE = 6,		/* Undelete directory inode */
};

/*
 * Inode 16-bit 'mode' field
 */
enum {
	// 1- File Format
	EXT2_IFILE_FORMAT	= 0xF000,	/* Mask to extract format bits */
	EXT2_IFSOCK		= 0xC000,	/* Socket */
	EXT2_IFLINK		= 0xA000,	/* Symbolic Link */
	EXT2_IFREG		= 0x8000,	/* Regular File */
	EXT2_IFBLOCK		= 0x6000,	/* Block Device */
	EXT2_IFDIR		= 0x4000,	/* Directory */
	EXT2_IFCHAR		= 0x2000,	/* Character Device */
	EXT2_IFIFO		= 0x1000,	/* FIFO File */

	// 2- Process execution user/group override
	EXT2_ISUID		= 0x0800,	/* Set Process UID bit */
	EXT2_IGUID		= 0x0400,	/* Set Group UID bit */
	EXT2_ISVTX		= 0x0200,	/* Sticky bit */

	// 3- Access rights bit mask
	EXT2_IRUSR		= 0x0100,	/* R for owner */
	EXT2_IWUSR		= 0x0080,	/* W for owner */
	EXT2_IXUSR		= 0x0040,	/* X for owner */
	EXT2_IRGRP		= 0x0020,	/* R for group */
	EXT2_IWGRP		= 0x0010,	/* W for group */
	EXT2_IXGRP		= 0x0008,	/* X for group */
	EXT2_IROTH		= 0x0004,	/* R for other */
	EXT2_IWOTH		= 0x0002,	/* W for other */
	EXT2_IXOTH		= 0x0001,	/* X for other */
};

/*
 * Directory entries 1-byte File-Type field
 */
enum {
	EXT2_FT_UNKNOWN		= 0,
	EXT2_FT_REG_FILE	= 1,
	EXT2_FT_DIR		= 2,
	EXT2_FT_CHRDEV		= 3,
	EXT2_FT_BLKDEV		= 4,
	EXT2_FT_FIFO		= 5,
	EXT2_FT_SOCK		= 6,
	EXT2_FT_SYMLINK		= 7,
	EXT2_FT_MAX
};

/*
 * On-disk Superblock Format
 *
 * @inodes_count      : Total number of inodes, used and free, in the FS.
 * @blocks_count      : Total number of blocks, used, free, and reserved.
 * @r_blocks_count    : Total number of blocks reserved for the superuser,
 *                      usable for emergency cases when the disk is full.
 * @free_blocks_count : Total # of free blocks, including reserved.
 * @free_inodes_count : Total # of free inodes; sum of b-groups free inodes.
 * @first_data_block  : First block holding data, i.e. non-bootstrap code.
 *                      NOTE! 1st block for Block Group #0 = first_data_block
 * @log_block_size    : Block size = 1024 << size_in_log (1, 2, 4, 8, .. KB)
 * @log_framgent_size : Fragments are not support, their size = block size
 * @blocks_per_group  : Number of blocks per Block Group
 * @frags_per_group   : Number of fragments per Block Group
 * @inodes_per_group  : Number of inodes per Block Group
 * @mount_time        : Last time the FS was mounted, UNIX format
 * @write_time        : Last write access to the FS
 * @mount_count       : Number of mounts since the last fsck (FS check)
 * @max_mount_count   : Max number of mounts before an fsck is performed
 * @magic_signature   : Magic value identifying the FS as ext2
 * @state             : mounted cleanly (VALID_FS) or errors detected (ERROR)
 * @error_behavior    : What the FS driver should do when detecting errors
 * @minor_revision    : Minor part of the revision level
 * @last_check        : Last time of fsck
 * @check_interval    : Maximum Unix time interval allowed between fscks
 * @creator_os        : OS that created the file system
 * @revision_level    : Revision 0 (EXT2_GOOD_OLD) or 1 (EXT2_DYNAMIC)
 * @reserved_uid      : Default user ID for reserved blocks
 * @reserved_gid      : Default group ID for reserved blocks
 * @first_inode       : Index to first useable (non-reserved) inode
 * @inode_size        : Size of the On-Disk inode structure
 * @block_group       : Block Group number hosting this superblock
 * @features_compat   : Bitmask for the 'compatible' feature set. If FS driver
 *                      doesn't know any of these features, it can still mount
 *                      and use the disk _without_ risk of damaging data.
 * @features_incompat : Bitmask for the 'incompatible' feature set. If driver
 *                      faced any unknown bit set, it should NOT mount it.
 * @features_ro_compat: Any of these features unknown? mount it as readonly!
 * @UUID[16]          : Unique 128-bit UUID for the volume
 * @volume_label[16]  : 16-byte, NULL-terminated, ISO-Latin-1 volume name
 * @last_mounted[64]  : Obsolete, directory path wher the FS was last mounted
 * @compression_bitmap: LZV1, GZIP, BZIP, LZO type of compression used
 * @prealloc_blocks   : Nr of blocks to try to preallocate (COMPAT_PRERALLOC)
 * @prealloc_dir_blcks: Nr of blocks to try to preallocate for directories
 */
union super_block {
	struct {
		uint32_t  inodes_count;
		uint32_t  blocks_count;
		uint32_t  r_blocks_count;
		uint32_t  free_blocks_count;
		uint32_t  free_inodes_count;
		uint32_t  first_data_block;
		uint32_t  log_block_size;
		uint32_t  log_fragment_size;
		uint32_t  blocks_per_group;
		uint32_t  frags_per_group;
		uint32_t  inodes_per_group;
		uint32_t  mount_time;
		uint32_t  write_time;
		uint16_t  mount_count;
		uint16_t  max_mount_count;
		uint16_t  magic_signature;
		uint16_t  state;
		uint16_t  errors_behavior;
		uint16_t  minor_revision;
		uint32_t  last_check;
		uint32_t  check_interval;
		uint32_t  creator_os;
		uint32_t  revision_level;
		uint16_t  reserved_uid;
		uint16_t  reserved_gid;
		uint32_t  first_inode;
		uint16_t  inode_size;
		uint16_t  block_group;
		uint32_t  features_compat;
		uint32_t  features_incompat;
		uint32_t  features_ro_compat;
		uint8_t   UUID[16];
		char      volume_label[16];
		char      last_mounted[64];
		uint32_t  compression_bitmap;
		uint8_t   prealloc_blocks;
		uint8_t   prealloc_dir_blocks;
	} __packed;
	uint8_t   raw[EXT2_SUPERBLOCK_SIZE];
};

/*
 * A Block Group descriptor
 *
 * @block_bitmap      : 32-bit Block ID of the group 'blocks bitmap'
 * @inode_bitmap      : 32-bit Block ID of the group 'inodes bitmap'
 * @inode_table       : 32-bit Block ID for the inode's table first block
 * @free_blocks_count : Nr of free blocks in this group
 * @free_inodes_count : Nr of free inodes in this group
 * @used_dirs_count   : Nr of inodes allocated for directories in this group
 */
struct group_descriptor {
	uint32_t	block_bitmap;
	uint32_t	inode_bitmap;
	uint32_t	inode_table;
	uint16_t	free_blocks_count;
	uint16_t	free_inodes_count;
	uint16_t	used_dirs_count;
	uint16_t	reserved[7];
} __packed;

/*
 * On-Disk Inode structure
 *
 * @mode              : File type and access rights
 * @uid               : User ID associated with the file
 * @size_low          : Lower 32-bits of file size, in bytes
 * @atime             : Last time this inode was accessed; UNIX format
 * @ctime             : Time when this inode was created;  UNIX format
 * @mtime             : Last time this inode was modified; UNIX format
 * @dtime             : Time when this inode was deleted (if so); UNIX
 * @gid_low           : POSIX group ID having access to this file (low 16bits)
 * @links_count       : How many times this inode is linked to by dir entries
 * @i512_blocks       : Total number of 512-byte blocks reserved to contain the
 *                      data of this inode, regardless of whether these blocks
 *                      are used or not.
 * @flags             : How should the driver behave when dealing with file's
 *                      data; SECRM_FL, UNRM_FL, APPEND_FL, IMMUTABLE_FL, etc.
 * @reserved          : Originally for other, non-Linux, private data
 * @blocks[15]        : Pointers to data blocks; blocks 0-11 are direct mapping,
 *                      12 is indirect, 13 is double indirect, 14 is triple.
 *                      0 value indicates that such part of file isn't mapped.
 * @generation        : File version, only used by NFS
 * @file_acl          : Block number containing file's extended attributes
 * @size_high         : Higher 32-bits of file size, in bytes
 * @blocks_count_high : Higher 32-bits of total number of 512-byte blocks
 */
struct inode {
	uint16_t	mode;
	uint16_t	uid;
	uint32_t	size_low;
	uint32_t	atime;
	uint32_t	ctime;
	uint32_t	mtime;
	uint32_t	dtime;
	uint16_t        gid_low;
	uint16_t	links_count;
	uint32_t	i512_blocks;
	uint32_t	flags;
	uint32_t	os_dependent;
	uint32_t	blocks[EXT2_INO_NR_BLOCKS];
	uint32_t        generation;
	uint32_t	file_acl;
	uint32_t        size_high;
	uint32_t	obsolete;
	uint16_t	blocks_count_high;
	uint16_t	file_acl_high;
	uint16_t        uid_high;
	uint16_t        gid_high;
	uint32_t        reserved;
} __packed;

struct inode *inode_get(uint64_t inum);

static inline bool is_dir(uint64_t inum)
{
	return (inode_get(inum)->mode & EXT2_IFILE_FORMAT) == EXT2_IFDIR;
}

static inline bool is_regular_file(uint64_t inum)
{
	return (inode_get(inum)->mode & EXT2_IFILE_FORMAT) == EXT2_IFREG;
}

static inline bool is_socket(uint64_t inum)
{
	return (inode_get(inum)->mode & EXT2_IFILE_FORMAT) == EXT2_IFSOCK;
}

static inline bool is_fifo(uint64_t inum)
{
	return (inode_get(inum)->mode & EXT2_IFILE_FORMAT) == EXT2_IFIFO;
}

/*
 * Directory Entry Format.
 *
 * A directory is a file holding variable-sized records;
 * each record represents a file contained in that folder.
 *
 * @inode             : Inode # of the file entry, 0 indicates a non-used entry
 * @record_len        : Unsigned displacement to the next directory entry
 * @filename_len      : File name len must never be larger than record_len - 8
 * @file_type	      : Must match the type defined in the inode entry above
 * @filename[255]     : Name of entry, in ISO-LATIN-1 character set.
 */
struct dir_entry {
	uint32_t	inode_num;
	uint16_t	record_len;
	uint8_t		filename_len;
	uint8_t		file_type;
	char		filename[EXT2_FILENAME_LEN];
} __packed;

void ext2_init(void);
uint64_t file_read(struct inode *, char *buf, uint64_t offset, uint64_t len);
int64_t file_write(struct inode *, char *buf, uint64_t offset, uint64_t len);
int64_t name_i(const char *path);
void buf_hex_dump(void *given_buf, uint len);
void buf_char_dump(void *given_buf, uint len);

enum block_op {
	BLOCK_READ,
	BLOCK_WRTE,
};

#if EXT2_TESTS || FILE_TESTS
/*
 * Pahtname translation - Used for testing ext2 code.
 *
 * @path              : Absolute, hierarchial, UNIX format, file path
 * @relative_inum     : Inode# found using name_i() on _each_ subcomponent
 * @absolute_inum     : Inode# found using name_i() on the path as a whole.
 * @fd                : File descriptor returned by open(@path, O_RDONLY);
 */
struct path_translation {
	const char *path;
	uint64_t relative_inum;
	uint64_t absolute_inum;
	int fd;
};
#endif	/* EXT2_TESTS || FILE_TESTS */

/*
 * Globally export some internal methods if the test-cases
 * driver was enabled.
 */
#if EXT2_TESTS

#define STATIC	extern
void block_read(uint64_t block, char *buf, uint blk_offset, uint len);
void block_write(uint64_t block, char *buf, uint blk_offset, uint len);
uint64_t inode_alloc(void);
uint64_t block_alloc(void);
bool dir_entry_valid(uint64_t, struct dir_entry *, uint64_t off, uint64_t len);
struct dir_entry *find_dir_entry(uint64_t inum, const char *name,uint name_len);
void ext2_run_tests(void);

#else

#define STATIC	static
static void __unused ext2_run_tests(void) { }

#endif	/* EXT2_TESTS */

/*
 * Dump file system On-Disk structures;  useful for testing.
 */
void ext2_debug_init(void (*printf)(const char *fmt, ...));
void superblock_dump(union super_block *);
void blockgroup_dump(int bg_idx, struct group_descriptor *,
		     uint32_t firstb, uint32_t lastb, uint64_t inodetbl_blocks);
void inode_dump(struct inode *, uint64_t inum, const char *path);
void dentry_dump(struct dir_entry *dentry);

#endif	/* _EXT2_H */
