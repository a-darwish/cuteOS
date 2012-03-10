#ifndef _UNROLLED_H
#define _UNROLLED_H

/*
 * Unrolled Linked List -- A linked list of small arrays!
 *
 * Copyright (C) 2012 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 */

#include <kernel.h>

/*
 * Singly-linked unrolled list node
 *
 * NOTE! the  data array could've been embedded in the node using
 * declaration ‘void *array[]’, and putting that at structure end?
 */
struct __node {
	void **array;		/* Array of void pointers to data! */
	uint array_len;		/* Number of cells in the @array (Redundant!)*/
	uint array_nrfree;	/* Number of free cells in the @array */
	uint num;		/* Node number in the linked list, from 0 */
	struct __node *next;	/* Next node in this list, or NULL */
};

/*
 * To use unrolled lists, embed this descriptor inside the
 * desired kernel structure.
 */
struct unrolled_head {
	struct __node *node;	/* Singly-linked list of unrolled nodes */
	uint array_len;		/* Number of cells in each node array */
};

/*
 * The exported API:
 * -----------------
 * unrolled_init(head, len)     : Initialize given unrolled list
 * unrolled_free(head)          : Free all list storage
 * unrolled_insert(head, val)   : Insert @val; generate new @key for it!
 * unrolled_lookup(head, key)   : given @key, get its '<key, val>' pair
 * unrolled_rm_key(head, key)   : Remove <@key, val> pair from list
 */
void unrolled_init(struct unrolled_head *head, uint array_len);
void unrolled_free(struct unrolled_head *head);
uint unrolled_insert(struct unrolled_head *head, void *val);
void *unrolled_lookup(struct unrolled_head *head, uint key);
void unrolled_remove_key(struct unrolled_head *head, uint key);

/*
 * unrolled_for_each - iterate over all the unrolled list values
 * @head        : Unrolled linked list head
 * @val         : of type ‘void *’, this's where the value will be
 *                put in each iteration.
 *
 * Take care while modifying this code, we're quite bending C
 * and GNU extensions syntax to achieve it.
 */
#define unrolled_for_each(head, val)					\
	for (struct __node *__node = (head)->node;			\
	     __node != NULL;						\
	     __node = __node->next)					\
		for (uint __i = 0,					\
		     __unused *_____c =					\
				  ({					\
val = NULL;								\
while (__i < __node->array_len && (val = __node->array[__i]) == NULL)	\
__i++; val;								\
				  });					\
		     __i < __node->array_len;				\
		     __i++,						\
				  ({					\
val = NULL;								\
while (__i < __node->array_len && (val = __node->array[__i]) == NULL)	\
__i++; val;								\
				  }))

#if UNROLLED_TESTS
void unrolled_run_tests(void);
#else
static void __unused unrolled_run_tests(void) { }
#endif /* UNROLLED_TESTS */

#endif /* _UNROLLED_H */
