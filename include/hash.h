#ifndef _HASH_H
#define _HASH_H

#include <kernel.h>
#include <tests.h>

struct hash;
struct hash *hash_new(uint len);
void hash_free(struct hash *hash);
void hash_insert(struct hash *hash, void *elem);
void *hash_find(struct hash *hash, uint64_t elem_id);
void hash_remove(struct hash *hash, uint64_t elem_id);

#if	HASH_TESTS
void hash_run_tests(void);
#else
static void __unused hash_run_tests(void) { }
#endif /* HASH_TESTS */

#endif /* _HASH_H */
