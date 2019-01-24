#ifndef _URCU_WFSTACK_H
#define _URCU_WFSTACK_H

/*
 * urcu/wfstack.h
 *
 * Userspace RCU library - Stack with wait-free push, blocking traversal.
 *
 * Copyright 2010-2012 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/* Adapted from userspace-rcu 0.10 because version 0.7 doesn't support a stack
 * without mutex. The non-LGPL section has been removed. */

#include <pthread.h>
#include <assert.h>
#include <stdbool.h>
#include <urcu/compiler.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Stack with wait-free push, blocking traversal.
 *
 * Stack implementing push, pop, pop_all operations, as well as iterator
 * on the stack head returned by pop_all.
 *
 * Wait-free operations: cds_wfs_push, __cds_wfs_pop_all, cds_wfs_empty,
 *                       cds_wfs_first.
 * Blocking operations: cds_wfs_pop, cds_wfs_pop_all, cds_wfs_next,
 *                      iteration on stack head returned by pop_all.
 *
 * Synchronization table:
 *
 * External synchronization techniques described in the API below is
 * required between pairs marked with "X". No external synchronization
 * required between pairs marked with "-".
 *
 *                      cds_wfs_push  __cds_wfs_pop  __cds_wfs_pop_all
 * cds_wfs_push               -              -                  -
 * __cds_wfs_pop              -              X                  X
 * __cds_wfs_pop_all          -              X                  -
 *
 * cds_wfs_pop and cds_wfs_pop_all use an internal mutex to provide
 * synchronization.
 */

#define CDS_WFS_WOULDBLOCK	((void *) -1UL)

enum cds_wfs_state {
	CDS_WFS_STATE_LAST =		(1U << 0),
};

/*
 * struct cds_wfs_node is returned by __cds_wfs_pop, and also used as
 * iterator on stack. It is not safe to dereference the node next
 * pointer when returned by __cds_wfs_pop_blocking.
 */
struct cds_wfs_node {
	struct cds_wfs_node *next;
};

/*
 * struct cds_wfs_head is returned by __cds_wfs_pop_all, and can be used
 * to begin iteration on the stack. "node" needs to be the first field of
 * cds_wfs_head, so the end-of-stack pointer value can be used for both
 * types.
 */
struct cds_wfs_head {
	struct cds_wfs_node node;
};

struct __cds_wfs_stack {
	struct cds_wfs_head *head;
};

struct cds_wfs_stack {
	struct cds_wfs_head *head;
	pthread_mutex_t lock;
};

/*
 * The transparent union allows calling functions that work on both
 * struct cds_wfs_stack and struct __cds_wfs_stack on any of those two
 * types.
 */
typedef union {
	struct __cds_wfs_stack *_s;
	struct cds_wfs_stack *s;
} __attribute__((__transparent_union__)) cds_wfs_stack_ptr_t;

#include "static-wfstack.h"

#define cds_wfs_node_init		_cds_wfs_node_init
#define cds_wfs_init			_cds_wfs_init
#define cds_wfs_destroy			_cds_wfs_destroy
#define __cds_wfs_init			___cds_wfs_init
#define cds_wfs_empty			_cds_wfs_empty
#define cds_wfs_push			_cds_wfs_push

/* Locking performed internally */
#define cds_wfs_pop_blocking		_cds_wfs_pop_blocking
#define cds_wfs_pop_with_state_blocking	_cds_wfs_pop_with_state_blocking
#define cds_wfs_pop_all_blocking	_cds_wfs_pop_all_blocking

/*
 * For iteration on cds_wfs_head returned by __cds_wfs_pop_all or
 * cds_wfs_pop_all_blocking.
 */
#define cds_wfs_first			_cds_wfs_first
#define cds_wfs_next_blocking		_cds_wfs_next_blocking
#define cds_wfs_next_nonblocking	_cds_wfs_next_nonblocking

/* Pop locking with internal mutex */
#define cds_wfs_pop_lock		_cds_wfs_pop_lock
#define cds_wfs_pop_unlock		_cds_wfs_pop_unlock

/* Synchronization ensured by the caller. See synchronization table. */
#define __cds_wfs_pop_blocking		___cds_wfs_pop_blocking
#define __cds_wfs_pop_with_state_blocking	\
					___cds_wfs_pop_with_state_blocking
#define __cds_wfs_pop_nonblocking	___cds_wfs_pop_nonblocking
#define __cds_wfs_pop_with_state_nonblocking	\
					___cds_wfs_pop_with_state_nonblocking
#define __cds_wfs_pop_all		___cds_wfs_pop_all

#ifdef __cplusplus
}
#endif

/*
 * cds_wfs_for_each_blocking: Iterate over all nodes returned by
 * __cds_wfs_pop_all().
 * @head: head of the queue (struct cds_wfs_head pointer).
 * @node: iterator (struct cds_wfs_node pointer).
 *
 * Content written into each node before enqueue is guaranteed to be
 * consistent, but no other memory ordering is ensured.
 */
#define cds_wfs_for_each_blocking(head, node)			\
	for (node = cds_wfs_first(head);			\
		node != NULL;					\
		node = cds_wfs_next_blocking(node))

/*
 * cds_wfs_for_each_blocking_safe: Iterate over all nodes returned by
 * __cds_wfs_pop_all(). Safe against deletion.
 * @head: head of the queue (struct cds_wfs_head pointer).
 * @node: iterator (struct cds_wfs_node pointer).
 * @n: struct cds_wfs_node pointer holding the next pointer (used
 *     internally).
 *
 * Content written into each node before enqueue is guaranteed to be
 * consistent, but no other memory ordering is ensured.
 */
#define cds_wfs_for_each_blocking_safe(head, node, n)			   \
	for (node = cds_wfs_first(head),				   \
			n = (node ? cds_wfs_next_blocking(node) : NULL);   \
		node != NULL;						   \
		node = n, n = (node ? cds_wfs_next_blocking(node) : NULL))

#endif /* _URCU_WFSTACK_H */
