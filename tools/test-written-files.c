/*
 * Quick-n-Dirty Linux x86-64 Userspace Tool ..
 *
 * .. for testing files written by the ‘Cute!’ kernel Ext2 driver.
 *
 * A simple write-testing mechanism is used:  the first 4K bytes of all
 * files must have a series of little-endian 4-byte integers = inode
 * number; the second 4K must have integers of (inode + 1); the third
 * 4K must have integers of (inode + 2).
 *
 * Thus, traverse the entire directory tree for regular files. For each
 * regular file found, test its contents using the above inode# method.
 *
 * TODO: Let the kernel ext2 testing code also write an "IGNORED" file,
 * which will list all the files that no data were written to, possibly
 * due to reaching disk limit.
 *
 * Build by:	$ gcc --std=gnu99 <program-name>.c
 */

#define _XOPEN_SOURCE 500			/* for nftw() */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ftw.h>

/* ********** Library Functions taken from our 'Cute!' Kernel: **********
 *
 * Copyright (C) 2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

/*
 * Fill memory with given 4-byte value @val. For easy
 * implementation, @len is vetoed to be a multiple of 8
 */
void *memset32(void *dst, uint32_t val, uint64_t len)
{
	uint64_t uval;
	uintptr_t d0;

	assert((len % 8) == 0);
	len = len / 8;

	uval = ((uint64_t)val << 32) + val;
	__asm__ __volatile__ (
		"rep stosq"			/* rdi, rcx */
		:"=&D" (d0), "+&c" (len)
		:"0" (dst), "a" (uval)
		:"memory");

	return dst;
}

/*
 * Print @given_buf, with length of @len bytes, in the format:
 *	$ od --format=x1 --address-radix=none --output-duplicates
 */
void buf_hex_dump(void *given_buf, int len)
{
	unsigned int bytes_perline = 16, n = 0;
	uint8_t *buf = given_buf;

	assert(buf != NULL);

	for (int i = 0; i < len; i++) {
		printf(" ");
		if (buf[i] < 0x10)
			printf("0");
		printf("%x", buf[i]);

		n++;
		if (n == bytes_perline || i == len - 1) {
			printf("\n");
			n = 0;
		}
	}
}

/* ********** End of the Library Functions ********** */

static int
dirTree(const char *pathname, const struct stat *sbuf, int type, struct FTW *ftwb)
{
	char *buf, *readbuf;
	int len, n, fd, ret;

	if (!S_ISREG(sbuf->st_mode))
		return 0;
	printf("Testing file '%s' with ino %lu: ", pathname, sbuf->st_ino);

	len = 4096 * 3;
	if ((buf = malloc(len)) == NULL) {
		perror("malloc");
		return -1;
	}
	if ((readbuf = malloc(len)) == NULL) {
		perror("malloc");
		return -1;
	}
	memset32(buf, sbuf->st_ino, 4096);		/* Check top comment */
	memset32(buf + 4096, sbuf->st_ino + 1, 4096);	/* Check top comment */
	memset32(buf + 8192, sbuf->st_ino + 2, 4096);	/* Check top comment */

	if ((fd = open(pathname, O_RDONLY)) < 0) {
		perror("open");
		return -1;
	}

	n = 0; do {
		if ((ret = read(fd, readbuf, len - n)) < 0) {
			perror("read");
			return -1;
		}
		n += ret;
	} while (n < len && ret != 0);

	if (memcmp(buf, readbuf, len) != 0) {
		printf("Data corruption: Buffer should be:\n");
		buf_hex_dump(buf, len);
		printf("But we found this:\n");
		buf_hex_dump(readbuf, len);
		printf("Failure!");
		return 0;
	}
	printf("Success!\n");

	close(fd);
	free(buf), free(readbuf);
	return 0;
}

int main(int argc, char **argv)
{
	if (nftw(argc > 1 ? argv[1] : ".", dirTree, 10, FTW_PHYS) == -1) {
		perror("nftw");
		return -1;
	}

	return 0;
}
