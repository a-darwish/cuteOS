/*
 * Unrolled Linked List -- A linked list of small arrays!
 *
 * Copyright (C) 2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * This is an ad-hoc UnrolledLinkedList-like structure for storing a
 * thread's File Descriptor Table.  Its core advantages are:
 *
 *	- Semi-random access using indices as the search keys,  which is
 *        suitable for the Unix file descriptors [0->N] continuous range.
 *	- Automatic and efficient reuse of deleted keys (i.e., close()-d
 *        descriptors), thus saving memory space as much as possible.
 *
 * NOTE! This is mostly how it's done on Solaris: "The process-level [file
 * descriptor table] entries are allocated dynamically in groups of 24 as
 * files are opened, up to the per-process open file limit." --Jim Mauro
 *
 * NOTE! In Linux, a dynamic array is used with an incrementing pointer
 * that wraps at end, thus minimizing search time for a free FD number/idx.
 */

#include <kernel.h>
#include <string.h>
#include <kmalloc.h>
#include <unrolled_list.h>
#include <tests.h>

/*
 * Allocate and initialize a new list node
 */
static struct __node *__unode_new(uint node_num, uint array_len)
{
	struct __node *node;

	node = kmalloc(sizeof(struct __node));
	node->array = kmalloc(array_len * sizeof(void *));
	memset(node->array, 0, array_len * sizeof(void *));
	node->array_len = array_len;
	node->array_nrfree = array_len;
	node->num = node_num;
	node->next = NULL;
	return node;
}

/*
 * Free all of a node's memory storage
 */
static void __unode_free(struct __node *node)
{
	kfree(node->array);
	kfree(node);
}

/*
 * Retun index of first free cell in given node's array
 */
static uint __unode_array_find_free_idx(struct __node *node)
{
	assert(node->array_nrfree > 0);

	for (uint i = 0; i < node->array_len; i++)
		if (node->array[i] == NULL)
			return i;

	assert(false);
}

static void __unode_store_val_in_array(struct __node *node, uint array_idx,
				       void *val)
{
	assert(val != NULL);
	assert(array_idx < node->array_len);
	assert(node->array[array_idx] == NULL);

	node->array_nrfree--;
	node->array[array_idx] = val;
}

static void __unused __unode_update_val_in_array(struct __node *node,
						 uint array_idx, void *val)
{
	assert(val != NULL);
	assert(array_idx < node->array_len);
	assert(node->array[array_idx] != NULL);

	node->array[array_idx] = val;
}

/*
 * Return the node holding given @key's value, and the index
 * within such node's array of data. Return NULL if the @key
 * was out of range.
 */
static struct __node *__get_node(struct unrolled_head *head, uint key,
				 uint *array_idx)
{
	struct __node *node;
	uint node_num;

	node = head->node;
	if (!node)				/* Empty list */
		return NULL;

	node_num = key / node->array_len;
	*array_idx = key % node->array_len;
	while (node_num--) {
		if (!node)			/* Out of Range key */
			return NULL;
		node = node->next;
	}

	return node;
}

/*
 * The Public API starts here:
 */

/*
 * unrolled_init - Initialize the unrolled list
 * @head        : Unrolled linked list head
 * @array_len   : Number of cells in each node's array
 */
void unrolled_init(struct unrolled_head *head, uint array_len)
{
	head->node = NULL;
	head->array_len = array_len;
	assert(head->array_len > 0);
}

/*
 * unrolled_free - Free _all_ of the list storage!
 */
void unrolled_free(struct unrolled_head *head)
{
	struct __node *node, *prev;

	node = head->node;
	while (node != NULL) {
		prev = node;
		node = node->next;
		__unode_free(prev);
	}
}

/*
 * unrolled_insert - Insert a given value in the mapping structure
 * @head        : Unrolled linked list head
 * @val         : value to be inserted; cannot be NULL
 * Return value : A "key" for the inserted value, forming a <key,val> pair.
 *
 * NOTE! The returned mapping structure key starts from 0 and move upwards.
 * Any deleted key [usually as a result of close()] is directly re-used
 * when inserting new values, avoiding overflows in the keys space as much
 * as possible.
 */
uint unrolled_insert(struct unrolled_head *head, void *val)
{
	struct __node *node, *prev;
	uint idx;

	if (head->node == NULL)
		head->node = __unode_new(0, head->array_len);

	assert(val != NULL);
	assert(head->node != NULL);
	for (node = head->node; node != NULL; node = node->next) {
		if (node->array_nrfree > 0) {
			idx = __unode_array_find_free_idx(node);
			__unode_store_val_in_array(node, idx, val);
			return (node->num * node->array_len) + idx;
		}
		prev = node;
	}

	/* No node with free array space was found */
	node = __unode_new(prev->num + 1, prev->array_len);
	__unode_store_val_in_array(node, 0, val);
	prev->next = node;
	return node->num * node->array_len;
}

/*
 * unrolled_lookup - Find the value attached with given key
 * @head        : Unrolled linked list head
 * @key         : Key used to search the mapping structure
 * Return value : Desired value, or NULL if key was invalid
 */
void *unrolled_lookup(struct unrolled_head *head, uint key)
{
	struct __node *node;
	uint array_idx;

	node = __get_node(head, key, &array_idx);
	if (node == NULL)
		return node;

	return node->array[array_idx];
}

/*
 * unrolled_remove_key - Remove the <key,val> pair identified by @key
 */
void unrolled_remove_key(struct unrolled_head *head, uint key)
{
	struct __node *node;
	uint array_idx;

	node = __get_node(head, key, &array_idx);
	if (node == NULL || node->array[array_idx] == NULL)
		panic("UNROLLED: Tried  to remove non-existing "
		      "mapping structure key %u", key);

	node->array_nrfree++;
	node->array[array_idx] = NULL;
}

/*
 * -------------------- Testcases! --------------------
 */

#if UNROLLED_TESTS

/*
 * Assure that list iterator pass on all list values
 */
static void _test_N_elements(struct unrolled_head *head, uint len)
{
	void *val;
	uintptr_t ret;

	printk("_UNROLLED: _test_N_elements(%u): ", len);
	for (uintptr_t i = 0; i < len; i++)
		unrolled_insert(head, (void *)(i + 5));
	uint i = 0;
	unrolled_for_each(head, val) {
		if ((ret = (uintptr_t)unrolled_lookup(head, i)) != i + 5)
			panic("_UNROLLED: Value for key %u got corrupted; "
			      "returned = %u, actual = %u", i, ret, val);
		i++;
	}
	if (i != len)
		panic("_UNROLLED: List iterator returned %u element(s) "
		      "although actual length is %u!", i, len);
	printk("Success!\n");
}

/*
 * Assure that generated keys move from 0 upwards
 */
static void _test_generated_keys(struct unrolled_head *head)
{
	uint idx, nr_elements;
	void *val;

	printk("_UNROLLED: _test_generated_keys(): ");
	nr_elements = head->array_len * 10;
	for (uintptr_t i = 0; i < nr_elements; i++) {
		idx = unrolled_insert(head, (void *)(i + 5));
		if (idx != i)
			panic("_UNROLLED: Generated keys does not start "
			      "from 0 upwards!");
	}
	uint i = 0;
	unrolled_for_each(head, val) {
		if ((uintptr_t)val != i + 5)
			panic("_UNROLLED: Value for key %u got corrupted; "
			      "returned = %u, actual = %u", i, val, i + 5);
		i++;
	}
	if (i != nr_elements)
		panic("_UNROLLED: List iterator returned %u elements "
		      "although actual length is %u!", i, nr_elements);
	printk("Success!\n");
}

/*
 * Assure that deleted keys get properly re-used
 */
static void _test_keys_removal(struct unrolled_head *head)
{
	uint key, nr_elements = 10000;
	void *val;

	printk("_UNROLLED: _test_keys_removal(): ");
	for (uintptr_t i = 0; i < nr_elements; i++)
		unrolled_insert(head, (void *)(i+1));
	for (intptr_t i = nr_elements - 1; i >= 0; i--) {
		val = unrolled_lookup(head, i);
		if (i+1 != (intptr_t)val)
			panic("_UNROLLED: Value for key %u got corrupted; "
			      "returned = %u, actual = %u", i, val, i+1);
		unrolled_remove_key(head, i);

		uint j = 0;
		unrolled_for_each(head, val) {
			j++;
		}
		if (j != nr_elements - 1)
			panic("_UNROLLED: List iterator returned %u elements al"
			      "though actual len is %u!", j, nr_elements - 1);
		key = unrolled_insert(head, (void *)(i+1));
		if (key != i)
			panic("_UNROLLED: Returned key should've been %u, "
			      " but it's %u", i, key);
	}
	printk("Success!\n");
}

/*
 * Like above, but delete several keys at once.
 */
static void _test_keys_removal2(struct unrolled_head *head)
{
	uint key, nr_elements = 10000, nr_deleted_keys;
	void *val;

	printk("_UNROLLED: _test_keys_removal2(): ");
	nr_deleted_keys = 0;
	for (uintptr_t i = 0; i < nr_elements; i++)
		unrolled_insert(head, (void *)(i+1));
	for (uint key = 0; key < nr_elements; key++)
		if (key % 2 == 0) {
			unrolled_remove_key(head, key);
			nr_deleted_keys++;
		}
	uint j = 0;
	unrolled_for_each(head, val) {
		j++;
	}
	if (j != nr_elements - nr_deleted_keys)
			panic("_UNROLLED: List iterator returned %u elements al"
			      "though actual len is %u!", j, nr_elements / 2);
	while (nr_deleted_keys--) {
		key = unrolled_insert(head, (void *)3);
		if (key % 2 != 0 || key >= nr_elements)
			panic("_UNROLLED: Allocated new key %u, while %u keys "
			      "were deleted and not yet re-used!", key,
			      nr_deleted_keys);
	}
	printk("Success!\n");
}

/*
 * Run all the test-cases using nodes with @array_len cells.
 * @array_len of 1 cell is equivalent to a singly-linked list!
 */
static void _unrolled_run_tests(uint array_len)
{
	struct unrolled_head head;
	uint i;
	void *val;

	unrolled_init(&head, array_len);
	unrolled_free(&head);

	for (i = 0; i < 1500; i++) {
		unrolled_init(&head, array_len);
		_test_N_elements(&head, i);
		unrolled_free(&head);
	}

	unrolled_init(&head, array_len);
	_test_generated_keys(&head);
	i = 0;
	unrolled_for_each(&head, val) {	/* Delete all keys */
		unrolled_remove_key(&head, i);
		i++;
	}
	_test_generated_keys(&head);	/* Keys should start from 0 again */
	unrolled_free(&head);

	unrolled_init(&head, array_len);
	_test_keys_removal(&head);
	unrolled_free(&head);

	unrolled_init(&head, array_len);
	_test_keys_removal2(&head);
	unrolled_free(&head);
}

void unrolled_run_tests(void)
{
	/* Note the performance increase with bigger node array sizes! */
	for (uint array_len = 1; array_len <= 32; array_len++)
		_unrolled_run_tests(array_len);
}

#endif /* UNROLLED_TESTS */
