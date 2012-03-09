#ifndef _FILE_H
#define _FILE_H
#include <tests.h>

int sys_chdir(const char *path);

#if FILE_TESTS
void file_run_tests(void);
#else
static void __unused file_run_tests(void) { }
#endif	/* _FILE_TESTS */

#endif	/* _FILE_H */
