#ifndef _LIST_H
#define _LIST_H

/*
 * Type-generic doubly-linked lists
 *
 * Copyright (C) 2010 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 * This API is very close to the Linux-2.6 one, which I've used back in
 * the days and found it to be very flexible. The code itself was
 * "cleanroomed" from Bovet & Cesati's `Understanding the Linux Kernel'
 * book to avoid copyrights mismatch.
 *
 * A good feature of this API is letting us allocate and deallocate both
 * the structure _and_ its linked-list pointers in one-shot. This avoids
 * unnecessary fragmentation in kmalloc() buffers, lessens the chance of
 * memory leaks, and improves locality of reference.
 *
 * Such lists typically look like this:
 *
 *
 *     .--------------------------------------------------------.
 *     |                                                        |
 *     |            struct A        struct B        struct C    |
 *     |           ..........      ..........      ..........   |
 *     |           .        .      .        .      .        .   |
 *     v           .        .      .        .      .        .   |
 *    ---          .  ---   .      .  ---   .      .  ---   .   |
 *    |@| --------->  |@| --------->  |@| --------->  |@| ------.
 *    | |          .  | |   .      .  | |   .      .  | |   .
 *    |*|  <--------- |*|  <--------- |*|  <--------- |*|  <----.
 *    ---          .  ---   .      .  ---   .      .  ---   .   |
 *    `H'          .  `n'   .      .  `n'   .      .  `n'   .   |
 *     |           ..........      ..........      ..........   |
 *     |                                                        |
 *     .--------------------------------------------------------.
 *
 *
 * where 'H' and 'n' are list_node structures, and 'H' is the list's
 * head. '@' is a node's next pointer, while '*' is the same node's
 * prev pointer. All of the next and prev pointers point to _other_
 * list_node objects, not to the super-objects A, B, or C.
 *
 * Check the test-cases for usage examples.
 */

#include <kernel.h>
#include <stdint.h>

/*
 * Doubly-linked list node
 */
struct list_node {
	struct list_node *next;
	struct list_node *prev;
};

/*
 * Static init, for inside-structure nodes
 */
#define LIST_INIT(n)			\
	{				\
		.next = &(n),		\
		.prev = &(n),		\
	}

/*
 * Global declaration with a static init
 */
#define LIST_NODE(n)			\
	struct list_node n = LIST_INIT(n)

/*
 * Dynamic init, for run-time
 */
static inline void list_init(struct list_node *node)
{
	node->next = node;
	node->prev = node;
}

/*
 * Is this node connected with any neighbours?
 */
static inline bool list_empty(const struct list_node *node)
{
	if (node->next == node) {
		assert(node->prev == node);
		return true;
	}

	assert(node->prev != node);
	return false;
}

/*
 * Insert @new right after @node
 */
static inline void list_add(struct list_node *node, struct list_node *new)
{
	new->next = node->next;
	new->next->prev = new;

	node->next = new;
	new->prev = node;
}

/*
 * Insert @new right before @node
 */
static inline void list_add_tail(struct list_node *node, struct list_node *new)
{
	new->prev = node->prev;
	new->prev->next = new;

	node->prev = new;
	new->next = node;
}

/*
 * Return the address of the data structure of type @type
 * that includes given @node. @node_name is the node's
 * name inside that structure declaration.
 *
 * The "useless" pointer assignment is for type-checking.
 * `Make it hard to misuse' -- a golden APIs advice.
 */
#define list_entry(node, type, node_name)		\
({							\
	size_t offset;					\
	struct list_node __unused *m;			\
							\
	m = (node);					\
							\
	offset = offsetof(type, node_name);		\
	(type *)((uint8_t *)(node) - offset);		\
})

/*
 * Scan the list, beginning from @node, using the iterator
 * @struc. @struc is of type pointer to the structure
 * containing @node. @node_name is the node's name inside
 * that structure (the one containing @node) declaration.
 *
 * I won't like to see the preprocessor output of this :)
 */
#define list_for_each(node, struc, name)				\
	for (struc = list_entry((node)->next, typeof(*struc), name);	\
	     &(struc->name) != (node);					\
	     struc = list_entry(struc->name.next, typeof(*struc), name))

/*
 * Pop @node out of its connected neighbours.
 */
static inline void list_del(struct list_node *node)
{
	struct list_node *prevn;
	struct list_node *nextn;

	prevn = node->prev;
	nextn = node->next;

	assert(prevn != NULL);
	assert(nextn != NULL);
	assert(prevn->next == node);
	assert(nextn->prev == node);

	prevn->next = node->next;
	nextn->prev = node->prev;

	node->next = node;
	node->prev = node;
}


#if	LIST_TESTS

void list_run_tests(void);

#else

static void __unused list_run_tests(void) { }

#endif /* LIST_TESTS */

#endif /* _LIST_H */
