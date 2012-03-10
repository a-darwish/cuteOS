#ifndef _FILE_H
#define _FILE_H

#include <stdint.h>
#include <tests.h>
#include <fcntl.h>

int sys_chdir(const char *path);
int sys_open(const char *path, int flags, __unused mode_t mode);
int64_t sys_read(int fd, void *buf, uint64_t count);

#if FILE_TESTS
void file_run_tests(void);
#else
static void __unused file_run_tests(void) { }
#endif	/* _FILE_TESTS */

#endif	/* _FILE_H */
