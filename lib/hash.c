/*
 * A Simple Hash Table Implementation
 *
 * Copyright (C) 2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * The modulo operator is used as the hash function.
 */

#include <kernel.h>
#include <list.h>
#include <hash.h>
#include <kmalloc.h>
#include <tests.h>

struct hash_elem {
	uint64_t id;			/* Unique ID for each hash element */
	struct list_node node;		/* List node for hash fn collision */
};

struct hash {
	int len;			/* Number of  buckets in table */
	struct list_node *nodes_array;	/* Array (table) of list nodes */
};

/*
 * Allocation.  @len is the Hash table's length; assuming balanced
 * distribution, a bigger val means less prabability for collision.
 */
struct hash *hash_new(uint len)
{
	struct hash *hash;

	hash = kmalloc(sizeof(*hash));
	hash->nodes_array = kmalloc(len * sizeof(*hash->nodes_array));
	for (uint i = 0; i < len; i++)
		list_init(&hash->nodes_array[i]);

	hash->len = len;
	return hash;
}

/*
 * Deallocation
 */
void hash_free(struct hash *hash)
{
	assert(hash != NULL);
	kfree(hash->nodes_array);
	kfree(hash);
}

/*
 * Find the element identified by @elem_id in the given hash
 * repository.  Return NULL in case of non-existence.
 */
static void *hash_find_elem(struct hash *hash, uint elem_id)
{
	struct hash_elem *helem;
	int idx;

	assert(hash != NULL);
	idx = elem_id % hash->len;
	list_for_each(&hash->nodes_array[idx], helem, node) {
		if (helem->id == elem_id)
			return helem;
	}
	return NULL;
}

/*
 * Insert given element in the hash repository. @elem is a
 * pointer to the structure to be inserted.
 */
void hash_insert(struct hash *hash, void *elem)
{
	struct hash_elem *helem;
	int idx;

	helem = elem;
	if (hash_find_elem(hash, helem->id) != NULL)
		panic("Hash: Inserting element with ID #%lu, which "
		      "already exists!", helem->id);

	idx = helem->id % hash->len;
	assert(list_empty(&helem->node));
	list_add(&hash->nodes_array[idx], &helem->node);
}

/*
 * Find the element identified by @elem_id in the given hash
 * repository.  Return NULL in case of non-existence.
 */
void *hash_find(struct hash *hash, uint64_t elem_id)
{
	return hash_find_elem(hash, elem_id);
}

/*
 * Remove element identified by @elem_id from given hash.
 */
void hash_remove(struct hash *hash, uint64_t elem_id)
{
	struct hash_elem *helem;

	helem = hash_find_elem(hash, elem_id);
	if (helem == NULL)
		panic("Hash: Removing non-existent element identified "
		      "by #%lu\n", elem_id);

	list_del(&helem->node);
}


#if HASH_TESTS

static void hash_print_info(struct hash *hash)
{
	struct hash_elem *helem;
	uint64_t count;

	prints("Printing Hash info:\n");
	prints("Hash Address: 0x%lx\n", hash);
	prints("Hash Array Length: %d\n", hash->len);
	for (int i = 0; i < hash->len; i++) {
		prints("Number of Elemenets in Hash List #%d = ", i);
		count = 0;
		list_for_each(&hash->nodes_array[i], helem, node) {
			count++;
		}
		prints("%lu\n", count);
	}
}

struct test_struct {
	uint64_t num;
	struct list_node node;
	int payload;
};

static void test_hash(int hash_size)
{
	struct hash *hash;
	struct test_struct *array;
	struct test_struct *elem;
	int count;

	count = 128;
	hash = hash_new(hash_size);
	array = kmalloc(count * sizeof(*array));

	for (int i = 0; i < count; i++) {
		array[i].num = i;
		array[i].payload = i;
		list_init(&array[i].node);
		hash_insert(hash, &array[i]);
	}
	for (int i = count - 1; i >= 0; i--) {
		elem = hash_find(hash, i);
		if (elem == NULL)
			panic("_Hash: Cannot find element #%u, although "
			      "it was previously inserted!", i);
		if (elem->num != (unsigned)i)
			panic("_Hash: Element returned by searching hash for "
			      "id #%lu returned element with id #%lu!", i,
			      elem->num);
		if (elem->payload != i)
			panic("_Hash: Element returned by searching hash for "
			      "id #%lu has a valid id, but its payload got "
			      "corrupted from %d to %d!", i, i, elem->payload);
	}
	elem = hash_find(hash, INT32_MAX);
	if (elem != NULL)
		panic("_Hash: Returning non-existing element with id %d as "
		      "found, with payload = %d", INT32_MAX, elem->payload);
	hash_print_info(hash);

	hash_free(hash);
	kfree(array);
}

void hash_run_tests(void)
{
	for (int i = 1; i <= 256; i++) {
		printk("_Hash: Testing hash with size '%d': ", i);
		test_hash(i);
		printk("Success!\n");
	}

	/* Should panic: */
	/* struct hash *hash = hash_new(5); */
	/* struct test_struct element; element.num = 5; */
	/* list_init(&element.node); */
	/* hash_insert(hash, &element); hash_insert(hash, &element); */
}

#endif /* HASH_TESTS */
