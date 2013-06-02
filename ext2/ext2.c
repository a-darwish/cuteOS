/*
 * The Second Extended File System
 *
 * Copyright (C) 2012-2013 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * NOTES:
 *
 * - Check the 'ext2.h' header __before__ attacking this file.
 * - Inodes start from 1, while block and group indices start from 0!
 * - Set the inode's dirty flag after modifying any of its fields.
 *
 * SMP-Locking README (1):
 * -----------------------
 * Every used ‘on-disk’ inode is buffered on RAM as an ‘in-core’ inode.
 * Such buffered images are tracked by a hash table inode repository.
 * Each in-core inode has a reference count, guarding it from deletion
 * while being in-use.  This design is taken from the classical SVR2
 * implementation, but with some SMP tweaks.
 *
 * "An Object Cannot Synchronize its Own Visibility", thus the inode
 * reference count is _ONLY_ protected by the inodes hash repository
 * lock instead of the inode's _own_ lock. This avoids the following
 * race condition, assuming that the reference count is protected by
 * the inode's own lock:
 *
 * - Process A holds inode's lock, then get what it needs from inode
 * - While holding the inode's lock, process A calls inode_put()
 * - [A] thus decrements the inode's refcount,  finds it to be zero,
 *   and kfree()s the inode buffered image!
 * - While A was holding the lock,  a process B could have requested
 *   the same inode image from the repository, acquiring the inode's
 *   lock and waiting (spinning or sleeping) for it to be free.  It
 *   will increment the refcount after acquiring such lock (!)
 * - While B was waiting, process A has already freed the entire ino-
 *   de image, along with its lock!
 * - Process B is now referencing a _stale_ inode pointer and lock.
 *
 * Even if the refcount was of an atomic type, and thus incrementing
 * it before acquiring the inode's lock, there's a small failure time-
 * frame between acquiring the inode's reference, and increasing the
 * refcount.  Between these two lines, again, another process might
 * free() the entire inode image! That is:
 *
 *	inode = hash_find(&repository, 5); // Search for inode #5
 *	atomic_inc(&inode->refcount);      // Stale inode reference!
 *
 * What if the lock had a mechanism to detect the number of threads
 * waiting on its lock, and thus process A will not free() the inode
 * image unless the refcount _and_ the number of threads waiting on
 * the lock equals 0? This'll be similar to the case above, between
 * the two lines, another process might free() the inode image:
 *
 *	inode = hash_find(&repository, 5); // Search for inode #5
 *	acquire_lock(&inode->lock);        // Stale inode reference!
 *
 * Meanwhile, having the refcount and the inod image visibility (as
 * a whole, but not its internal elements) be proteced  by the hash
 * repository lock leads to the following:
 *
 * - Process A holds inode's lock, then get what it needs from inode
 * - While holding the inode's lock, process A calls inode_put()
 * - Before A minimize the reference count, hash lock is acquired.
 *
 * Two cases emerge: either proc B locks  the hash before A acquires
 * the hash lock, thus increasing the refcount, and making A's inode_
 * put() not to free() the buffered image.  Or proc B locks the hash
 * after A acquires the hash lock,  making proc A free the ino image,
 * but having B reallocate a new image for the inode since it is now
 * entirely removed from the hash repository.
 *
 * SMP-Locking README (2):
 * -----------------------
 * To ensure consistency, we never delete an __on disk__ inode upon
 * direct request. Several other threads might already be holding a
 * reference to that same inode. Legit examples include:
 *
 * - Thread [A] has a current working dir = d3. Another thread [B]
 *   deletes the directory d3's parent. Thread A's inode pointer to
 *   d3 should function as usual, without causing any kernel memory
 *   faults.
 * - An editor having a text file open; the same file gets deleted
 *   from another thread, .. and so on.
 *
 * To solve this case, an inode on-disk deletion is delegated to the
 * last thread standing holding that inode. Note that while inode
 * deletion is delegated, the deletion of its parent directory
 * __dir entry__ is immediately executed. So from a user's point of
 * view, the file will 'disappear' once a remove command was issued.
 *
 * Even if the FS was cleanly unmounted before we complete the inode
 * removal, the sitiuation can be easily fixed by fsck utilities.
 */

#include <kernel.h>
#include <stdint.h>
#include <percpu.h>
#include <errno.h>
#include <ramdisk.h>
#include <ext2.h>
#include <string.h>
#include <kmalloc.h>
#include <hash.h>
#include <bitmap.h>

/*
 * In-memory Super Block - Global State for our FS code
 *
 * NOTE! The inodes repository hash lock could be fine-grained
 * by having a lock on each hash collision linked-list instead.
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
	struct hash		*inodes_hash;
	spinlock_t		inodes_hash_lock;
} isb;

/*
 * Return a pointer to the on-disk image of inode #inum.
 */
static void *inode_diskimage(uint64_t inum)
{
	union super_block *sb;
	struct group_descriptor *bgd;
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
	return (void *)&isb.buf[inode_offset];
}

/*
 * Inode Get - Allocate a locked in-core copy of inode #inum.
 * The allocated image must be freed with inode_put() after use!
 *
 * FIXME: Since a mutex/semaphore mechanism is not yet implemented,
 * so far the returned copy is not actually 'locked'. For now, we
 * can workaround this by implementing a recursive spinlock.
 */
struct inode *inode_get(uint64_t inum)
{
	struct inode *inode;

	spin_lock(&isb.inodes_hash_lock);
	inode = hash_find(isb.inodes_hash, inum);
	if (inode == NULL) {
		inode = kmalloc(sizeof(*inode));
		inode_init(inode, inum);
		memcpy(dino_off(inode), inode_diskimage(inum), dino_len());
		hash_insert(isb.inodes_hash, inode);
	} else {
		assert(inode->refcount >= 1);
		inode->refcount++;
	}
	spin_unlock(&isb.inodes_hash_lock);

	return inode;
}

static void __inode_dealloc(struct inode *inode);

/*
 * Inode Put - Release (put) access to given in-core inode.
 */
void inode_put(struct inode *inode)
{
	/*
	 * FIXME: This assert may fail since we did not implement a locking
	 * mechanism over inodes yet. Our thread may call inode_put()
	 * after another thread [B] changed the same inode copy behind our
	 * back, but before that thread [B] marking the inode as dirty.
	 *
	 * if (memcmp(inode_diskimage(inode->inum), dino_off(inode), dino_len())) {
	 *     assert(inode->dirty == true);
	 * }
	 */

	if (inode->dirty == true)
		memcpy(inode_diskimage(inode->inum), dino_off(inode), dino_len());

	spin_lock(&isb.inodes_hash_lock);
	assert(inode->refcount > 0);
	--inode->refcount;
	if (inode->refcount == 0) {
		hash_remove(isb.inodes_hash, inode->inum);
		/*
		 * If the inode has been marked for __on disk__ deletion,
		 * and now since its has been "removed from visibility"
		 * (a ref count of zero, visibility inodes hash lock being
		 * already held + the object now removed from global repo),
		 * it can synchronize its own destruction and basically
		 * deallocate itself from disk!
		 */
		if (inode->delete_on_last_use == true)
			__inode_dealloc(inode);
		kfree(inode);
		inode = NULL;
	}
	spin_unlock(&isb.inodes_hash_lock);
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
 * Inode Alloc - Assign a free disk inode to newly created files
 * @type        : Type of file this inode is allocated for
 * Return val   : Locked in-core copy of allocated disk inode, or NULL
 *
 * NOTE! There are obviously more SMP-friendly ways for doing this
 * than just grabbing a global lock reserved for inodes allocation.
 * An ‘optimistic locking’ scheme would access the inode bitmap
 * locklessly, but set the inode's ‘used/free’ bit using an atomic
 * test_bit_and_set method.  If another thread got that inode bef-
 * ore us, we continue searching the bitmap for yet another inode!
 */
STATIC struct inode *inode_alloc(enum file_type type)
{
	struct inode *inode;
	struct group_descriptor *bgd;
	char *buf;
	int first_zero_bit;
	uint64_t inum;

	bgd = isb.bgd;
	buf = kmalloc(isb.block_size);

	for (uint i = 0; i < isb.blockgroups_count; i++, bgd++) {
		spin_lock(&isb.inode_allocation_lock);
		block_read(bgd->inode_bitmap, buf, 0, isb.block_size);
		first_zero_bit = bitmap_first_zero_bit(buf, isb.block_size);
		if (first_zero_bit == -1) {
			spin_unlock(&isb.inode_allocation_lock);
			continue;
		}

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
		spin_unlock(&isb.inode_allocation_lock);

		inode = inode_get(inum);
		memset(dino_off(inode), 0, dino_len());
		inode->mode |= dir_entry_type_to_inode_mode(type);
		inode->mode |= S_IRUSR | S_IWUSR;
		inode->mode |= S_IRGRP | S_IWGRP;
		inode->mode |= S_IROTH;
		if (type == EXT2_FT_DIR)
			inode->mode |= S_IXUSR | S_IXGRP | S_IXOTH;
		inode->atime = inode->ctime = inode->mtime = 0xf00f;
		inode->dirty = true;
		goto out;
	}
	inode = NULL;
out:
	kfree(buf);
	return inode;
}

/*
 * Inode delete - Mark given inode for deletion
 */
STATIC void inode_mark_delete(struct inode *inode)
{
	inode->delete_on_last_use = true;
}

/*
 * Inode Dealloc - Delete given inode from disk
 *
 * NOTE! Don't call this directly, use inode_mark_delete()
 */
static void __inode_dealloc(struct inode *inode)
{
	struct group_descriptor *bgd;
	uint64_t group, groupi;
	char *buf;

	assert(inode->inum != 0);
	assert(inode->inum >= isb.sb->first_inode);
	assert(inode->inum <= isb.sb->inodes_count);
	assert(inode->links_count == 0);
	assert(inode->refcount == 0);

	group  = (inode->inum - 1) / isb.sb->inodes_per_group;
	groupi = (inode->inum - 1) % isb.sb->inodes_per_group;
	bgd = &isb.bgd[group];
	buf = kmalloc(isb.block_size);

	spin_lock(&isb.inode_allocation_lock);
	isb.sb->free_inodes_count++;
	bgd->free_inodes_count++;
	if (S_ISDIR(inode->mode))
		bgd->used_dirs_count--;
	block_read(bgd->inode_bitmap, buf, 0, isb.block_size);
	assert(bitmap_bit_is_set(buf, groupi, isb.block_size));
	bitmap_clear_bit(buf, groupi, isb.block_size);
	block_write(bgd->inode_bitmap, buf, 0, isb.block_size);
	spin_unlock(&isb.inode_allocation_lock);

	memset(inode_diskimage(inode->inum), 0, dino_len());
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

	for (uint i = 0; i < isb.blockgroups_count; i++, bgd++) {
		spin_lock(&isb.block_allocation_lock);
		block_read(bgd->block_bitmap, buf, 0, isb.block_size);
		first_zero_bit = bitmap_first_zero_bit(buf, isb.block_size);
		if (first_zero_bit == -1) {
			spin_unlock(&isb.block_allocation_lock);
			continue;
		}

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
		spin_unlock(&isb.block_allocation_lock);
		goto out;
	}
	block = 0;
out:
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
 *
 * NOTE! If this code was later modified so that errors are returned,
 * remember to check such errors in all of the callers.
 */
uint64_t file_read(struct inode *inode, char *buf, uint64_t offset, uint64_t len)
{
	uint64_t supported_area, block, blk_offset;
	uint64_t read_len, ret_len;

	if (!S_ISREG(inode->mode) && !S_ISDIR(inode->mode))
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
 */
int64_t file_write(struct inode *inode, char *buf, uint64_t offset, uint64_t len)
{
	uint64_t supported_area, blk_offset, last_offset;
	uint64_t write_len, ret_len, block, new;

	if (!S_ISREG(inode->mode) && !S_ISDIR(inode->mode))
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
			inode->dirty = true;
		}
		block_write(inode->blocks[block], buf, blk_offset, write_len);

		assert(len >= write_len);
		len -= write_len;
		buf += write_len;
		offset += write_len;
		assert(offset <= last_offset);
		if (offset == last_offset)
			assert(len == 0);

		if (offset > inode->size_low) {
			inode->size_low = offset;
			block = ceil_div(offset, isb.block_size);
			inode->i512_blocks = (block * isb.block_size) / 512;
			inode->dirty = true;
		}
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
 * is relative to the directory-file start.  @dir is the inode
 * of directory file holding given @dentry.  @len is the number
 * of bytes we were able read before reaching EOF.
 *
 * FIXME: Assure entry's type == its destination inode mode.
 */
STATIC bool dir_entry_valid(struct inode *dir, struct dir_entry *dentry,
			    uint64_t offset, uint64_t read_len)
{
	uint64_t inum;

	inum = dir->inum;
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
	if (dentry->record_len + offset > dir->size_low) {
		printk("EXT2: Dir entry (ino %lu, offset %lu) goes beyond "
		       "the dir EOF (entry len = %lu, dir len = %lu)\n", inum,
		       offset, dentry->record_len, dir->size_low);
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
 * entry if found, or return an errno.
 *
 * @dir         : Inode for the directory to be searched
 * @name        : Wanted file name, which may not be NULL-terminated
 * @name_len    : File name len, to avoid requiring the NULL postfix
 * @dentry	: Return val, the found directory entry (if any)
 * @offset	: Return val, offset of found dentry wrt to dir file
 * Return val	: Inode number of file, or an errorno
 */
STATIC int64_t find_dir_entry(struct inode *dir, const char *name, uint name_len,
			      struct dir_entry **entry, int64_t *roffset)
{
	struct dir_entry *dentry;
	uint64_t dentry_len, offset, len;

	assert(S_ISDIR(dir->mode));
	dentry_len = sizeof(struct dir_entry);
	dentry = *entry = kmalloc(dentry_len);

	if (name_len == 0 || name_len > EXT2_FILENAME_LEN)
		return -ENOENT;

	assert(name != NULL);
	for (offset = 0;  ; offset += dentry->record_len) {
		len = file_read(dir, (char *)dentry, offset, dentry_len);
		if (len == 0)			/* EOF */
			return -ENOENT;
		if (!dir_entry_valid(dir, dentry, offset, len))
			return -EIO;
		if (dentry->inode_num == 0)	/* Unused entry */
			continue;
		if (dentry->filename_len != name_len)
			continue;
		if (memcmp(dentry->filename, name, name_len))
			continue;
		*roffset = offset;
		return dentry->inode_num;
	}
	assert(false);
}

/*
 * Search given directory for an entry with file @name: mark it on
 * disk as deleted.  Target inode links count will get decremented.
 */
static int64_t remove_dir_entry(struct inode *dir, const char *name)
{
	struct inode *entry_ino;
	struct dir_entry *dentry;
	int64_t ret, offset, dentry_inum;

	assert(S_ISDIR(dir->mode));
	assert(name != NULL);
	ret = find_dir_entry(dir, name, strlen(name), &dentry, &offset);
	if (ret < 0)
		goto out;

	dentry_inum = dentry->inode_num;
	dentry->inode_num = 0;
	ret = file_write(dir, (char *)dentry, offset, dentry->record_len);
	if (ret < 0)
		goto out;

	entry_ino = inode_get(dentry_inum);
	assert(entry_ino->links_count > 0);
	entry_ino->links_count--;
	entry_ino->dirty = true;
	inode_put(entry_ino);
	ret = dentry_inum;

out:	kfree(dentry);
	return ret;
}

/*
 * File Delete - Delete given file.
 * @parent      : Parent directory where the file will be located
 * @name        : File's name
 *
 * NOTE! If given file's inode was linked by several hard links, then
 * instead of the default behaviour of deleting the file's inode and
 * truncating its entire data, only the file's directory entry will
 * get deleted.
 */
int file_delete(struct inode *parent, const char *name)
{
	struct inode *inode;
	int64_t entry_inum;

	assert(S_ISDIR(parent->mode));
	assert(name != NULL);

	entry_inum = remove_dir_entry(parent, name);
	if (entry_inum < 0)
		return entry_inum;

	inode = inode_get(entry_inum);
	assert(S_ISREG(inode->mode));
	if (inode->links_count == 0) {
		file_truncate(inode);
		inode_mark_delete(inode);
	}
	inode_put(inode);
	return 0;
}

/*
 * Create a new file entry in given parent directory. Check file_new()
 * for parameters documentation.  @entry_inum is the allocated inode
 * number for the new file.
 *
 * NOTE! This increments the entry's destination inode links count
 * NOTE! @dir may be equal to @entry_ino if we are adding a '.' entry
 * to a new folder.
 */
int64_t ext2_new_dir_entry(struct inode *dir, struct inode *entry_ino,
			   const char *name, enum file_type type)
{
	struct dir_entry *dentry, *lastentry, *newentry;
	uint64_t offset, blk_offset, newentry_len, len;
	int64_t ret, filename_len, null;
	char *zeroes;

	assert(S_ISDIR(dir->mode));
	assert(entry_ino->inum != 0);
	assert(name != NULL);
	assert(type == EXT2_FT_REG_FILE || type == EXT2_FT_DIR);

	filename_len = strnlen(name, EXT2_FILENAME_LEN - 1);
	if (name[filename_len] != '\0')
		return -ENAMETOOLONG;
	if (filename_len == 0)
		return -ENOENT;

	ret = find_dir_entry(dir, name, filename_len, &dentry, &null);
	if (ret > 0)
		ret = -EEXIST;
	if (ret < 0 && ret != -ENOENT)
		goto free_dentry1;

	/*
	 * Find the parent dir's last entry: our new entry will get
	 * appended after it.  The parent directory might still be
	 * empty, with no entries at all!
	 */

	lastentry = kmalloc(sizeof(*lastentry));
	memset(lastentry, 0, sizeof(*lastentry));
	for (offset = 0;  ; offset += lastentry->record_len) {
		len = file_read(dir,(char*)lastentry,offset,sizeof(*lastentry));
		if (len == 0)
			break;
		if (!dir_entry_valid(dir, lastentry, offset, len)) {
			ret = -EIO;
			goto free_dentry2;
		}
	}
	dir->flags &= !EXT2_INO_DIR_INDEX_FL;
	dir->dirty = true;

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
	newentry->inode_num = entry_ino->inum;
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
	assert(dir_entry_valid(dir, newentry, offset, newentry_len));

	/*
	 * Update entry's inode statistics
	 */

	entry_ino->links_count++;
	entry_ino->dirty = true;

	ret = 0;
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
 * File New - Create and initialize a new file in given directory
 * @dir		: Parent directory where the file will be located
 * @name        : File's name
 * @type        : File's type, in Ext2 dir entry type format
 * Return value : New file's inode number, or an errno
 */
int64_t file_new(struct inode *dir, const char *name, enum file_type type)
{
	int64_t ret, ret2, inum;
	struct inode *inode;

	assert(S_ISDIR(dir->mode));
	inode = inode_alloc(type);
	if (inode == NULL)
		return -ENOSPC;

	ret = ext2_new_dir_entry(dir, inode, name, type);
	if (ret < 0)
		goto dealloc_inode;

	if (type == EXT2_FT_DIR) {
		ret = ext2_new_dir_entry(inode, inode, ".", EXT2_FT_DIR);
		if (ret < 0)
			goto remove_newly_created_entry;
		ret = ext2_new_dir_entry(inode, dir, "..", EXT2_FT_DIR);
		if (ret < 0)
			goto remove_new_dir_dot_entry;
	}

	inum = inode->inum;
	inode_put(inode);
	return inum;

remove_new_dir_dot_entry:
	if ((ret2 = remove_dir_entry(inode, ".")) <= 0)
		panic("Removing just created directory inode #%lu dot "
		      "entry returned -%s", inode->inum, errno(ret2));
remove_newly_created_entry:
	if ((ret2 = remove_dir_entry(dir, name)) <= 0)
		panic("Removing just created directory inode #%lu entry for "
		      "file '%s' returned -%s", dir->inum, name, errno(ret2));
dealloc_inode:
	inode_mark_delete(inode);
	inode_put(inode);
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
 * @inode	: File's inode, which will map us to the data blocks
 */
void file_truncate(struct inode *inode)
{
	assert(S_ISREG(inode->mode));
	assert(inode->inum != 0);
	assert(inode->inum >= isb.sb->first_inode);
	assert(inode->inum <= isb.sb->inodes_count);

	inode->dirty = true;
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
	struct inode *parent;
	struct dir_entry *dentry;
	int64_t inum, prev_inum, null;

	assert(path != NULL);
	switch (*path) {
	case '\0':
		return -ENOENT;
	case '/':
		inum = EXT2_ROOT_INODE; break;
	default:
		inum = current->working_dir;
		assert(inum != 0);
	}

	p1 = p2 = path;
	while (*p2 != '\0' && inum > 0) {
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

		parent = inode_get(prev_inum);
		assert(S_ISDIR(parent->mode));
		inum = find_dir_entry(parent, p1, p2 - p1, &dentry, &null);
		inode_put(parent);
		kfree(dentry);
	}

	assert(inum != 0);
	return inum;
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

	ext2_debug_init(&serial_char_dumper);
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

	/* Prepare the In-core Inodes hash repository */
	isb.inodes_hash = hash_new(256);
	spin_init(&isb.inodes_hash_lock);

	/* Root Inode sanity checks */
	rooti = inode_get(EXT2_ROOT_INODE);
	if (!S_ISDIR(rooti->mode))
		panic("EXT2: Root inode ('/') is not a directory!");
	if (rooti->i512_blocks == 0 || rooti->size_low == 0)
		panic("EXT2: Root inode ('/') size = 0 bytes!");
	if (name_i("/.") != EXT2_ROOT_INODE)
		panic("EXT2: Corrupt root directory '.'  entry!");
	if (name_i("/..") != EXT2_ROOT_INODE)
		panic("EXT2: Corrupt root directory '..' entry!");
	inode_dump(rooti, "/");
	inode_put(rooti);

	sb->volume_label[EXT2_LABEL_LEN - 1] = '\0';
	printk("Ext2: Passed all sanity checks!\n");
	printk("EXT2: File system label is `%s'\n", sb->volume_label);
}
