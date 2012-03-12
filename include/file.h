#ifndef _FILE_H
#define _FILE_H

#include <stdint.h>
#include <tests.h>
#include <fcntl.h>
#include <stat.h>

int sys_chdir(const char *path);
int sys_open(const char *path, int flags, __unused mode_t mode);
int64_t sys_read(int fd, void *buf, uint64_t count);
int64_t sys_lseek(int fd, uint64_t offset, uint whence);
int sys_fstat(int fd, struct stat *buf);
int sys_stat(const char *path, struct stat *buf);

#if FILE_TESTS

#include <spinlock.h>

struct test_file {
	uint64_t inum;		/* Inode# of the open()-ed file */
	int flags;		/* Flags passed  to open() call */
	spinlock_t lock;	/* ONLY FOR offset and refcount */
	uint64_t offset;	/* MAIN FIELD: File byte offset */
	int refcount;		/* Reference count; fork,dup,.. */
};

void file_run_tests(void);
#else
static void __unused file_run_tests(void) { }
#endif	/* _FILE_TESTS */

#endif	/* _FILE_H */
