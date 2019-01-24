#ifndef _URCU_WFCQUEUE_H
#define _URCU_WFCQUEUE_H

/*
 * urcu/wfcqueue.h
 *
 * Userspace RCU library - Concurrent Queue with Wait-Free Enqueue/Blocking Dequeue
 *
 * Copyright 2010-2012 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 * Copyright 2011-2012 - Lai Jiangshan <laijs@cn.fujitsu.com>
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

/* Adapted from userspace-rcu 0.10 because version 0.7 doesn't contain it.
 * The non-LGPL section has been removed. */

#include <pthread.h>
#include <assert.h>
#include <stdbool.h>
#include <urcu/compiler.h>
#include <urcu/arch.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Concurrent queue with wait-free enqueue/blocking dequeue.
 *
 * This queue has been designed and implemented collaboratively by
 * Mathieu Desnoyers and Lai Jiangshan. Inspired from
 * half-wait-free/half-blocking queue implementation done by Paul E.
 * McKenney.
 */

#define CDS_WFCQ_WOULDBLOCK	((struct cds_wfcq_node *) -1UL)

enum cds_wfcq_ret {
	CDS_WFCQ_RET_WOULDBLOCK =	-1,
	CDS_WFCQ_RET_DEST_EMPTY =	0,
	CDS_WFCQ_RET_DEST_NON_EMPTY =	1,
	CDS_WFCQ_RET_SRC_EMPTY = 	2,
};

enum cds_wfcq_state {
	CDS_WFCQ_STATE_LAST =		(1U << 0),
};

struct cds_wfcq_node {
	struct cds_wfcq_node *next;
};

/*
 * Do not put head and tail on the same cache-line if concurrent
 * enqueue/dequeue are expected from many CPUs. This eliminates
 * false-sharing between enqueue and dequeue.
 */
struct __cds_wfcq_head {
	struct cds_wfcq_node node;
};

struct cds_wfcq_head {
	struct cds_wfcq_node node;
	pthread_mutex_t lock;
};

#ifndef __cplusplus
/*
 * The transparent union allows calling functions that work on both
 * struct cds_wfcq_head and struct __cds_wfcq_head on any of those two
 * types.
 */
typedef union {
	struct __cds_wfcq_head *_h;
	struct cds_wfcq_head *h;
} __attribute__((__transparent_union__)) cds_wfcq_head_ptr_t;

/*
 * This static inline is only present for compatibility with C++. It is
 * effect-less in C.
 */
static inline struct __cds_wfcq_head *__cds_wfcq_head_cast(struct __cds_wfcq_head *head)
{
	return head;
}

/*
 * This static inline is only present for compatibility with C++. It is
 * effect-less in C.
 */
static inline struct cds_wfcq_head *cds_wfcq_head_cast(struct cds_wfcq_head *head)
{
	return head;
}
#else /* #ifndef __cplusplus */

/* C++ ignores transparent union. */
typedef union {
	struct __cds_wfcq_head *_h;
	struct cds_wfcq_head *h;
} cds_wfcq_head_ptr_t;

/* C++ ignores transparent union. Requires an explicit conversion. */
static inline cds_wfcq_head_ptr_t __cds_wfcq_head_cast(struct __cds_wfcq_head *head)
{
	cds_wfcq_head_ptr_t ret = { ._h = head };
	return ret;
}
/* C++ ignores transparent union. Requires an explicit conversion. */
static inline cds_wfcq_head_ptr_t cds_wfcq_head_cast(struct cds_wfcq_head *head)
{
	cds_wfcq_head_ptr_t ret = { .h = head };
	return ret;
}
#endif /* #else #ifndef __cplusplus */

struct cds_wfcq_tail {
	struct cds_wfcq_node *p;
};

#include "static-wfcqueue.h"

#define cds_wfcq_node_init		_cds_wfcq_node_init
#define cds_wfcq_init			_cds_wfcq_init
#define __cds_wfcq_init			___cds_wfcq_init
#define cds_wfcq_destroy		_cds_wfcq_destroy
#define cds_wfcq_empty			_cds_wfcq_empty
#define cds_wfcq_enqueue		_cds_wfcq_enqueue

/* Dequeue locking */
#define cds_wfcq_dequeue_lock		_cds_wfcq_dequeue_lock
#define cds_wfcq_dequeue_unlock		_cds_wfcq_dequeue_unlock

/* Locking performed within cds_wfcq calls. */
#define cds_wfcq_dequeue_blocking	_cds_wfcq_dequeue_blocking
#define cds_wfcq_dequeue_with_state_blocking	\
					_cds_wfcq_dequeue_with_state_blocking
#define cds_wfcq_splice_blocking	_cds_wfcq_splice_blocking
#define cds_wfcq_first_blocking		_cds_wfcq_first_blocking
#define cds_wfcq_next_blocking		_cds_wfcq_next_blocking

/* Locking ensured by caller by holding cds_wfcq_dequeue_lock() */
#define __cds_wfcq_dequeue_blocking	___cds_wfcq_dequeue_blocking
#define __cds_wfcq_dequeue_with_state_blocking	\
					___cds_wfcq_dequeue_with_state_blocking
#define __cds_wfcq_splice_blocking	___cds_wfcq_splice_blocking
#define __cds_wfcq_first_blocking	___cds_wfcq_first_blocking
#define __cds_wfcq_next_blocking	___cds_wfcq_next_blocking

/*
 * Locking ensured by caller by holding cds_wfcq_dequeue_lock().
 * Non-blocking: deque, first, next return CDS_WFCQ_WOULDBLOCK if they
 * need to block. splice returns nonzero if it needs to block.
 */
#define __cds_wfcq_dequeue_nonblocking	___cds_wfcq_dequeue_nonblocking
#define __cds_wfcq_dequeue_with_state_nonblocking	\
				___cds_wfcq_dequeue_with_state_nonblocking
#define __cds_wfcq_splice_nonblocking	___cds_wfcq_splice_nonblocking
#define __cds_wfcq_first_nonblocking	___cds_wfcq_first_nonblocking
#define __cds_wfcq_next_nonblocking	___cds_wfcq_next_nonblocking

/*
 * __cds_wfcq_for_each_blocking: Iterate over all nodes in a queue,
 * without dequeuing them.
 * @head: head of the queue (struct cds_wfcq_head or __cds_wfcq_head pointer).
 * @tail: tail of the queue (struct cds_wfcq_tail pointer).
 * @node: iterator on the queue (struct cds_wfcq_node pointer).
 *
 * Content written into each node before enqueue is guaranteed to be
 * consistent, but no other memory ordering is ensured.
 * Dequeue/splice/iteration mutual exclusion should be ensured by the
 * caller.
 */
#define __cds_wfcq_for_each_blocking(head, tail, node)		\
	for (node = __cds_wfcq_first_blocking(head, tail);	\
		node != NULL;					\
		node = __cds_wfcq_next_blocking(head, tail, node))

/*
 * __cds_wfcq_for_each_blocking_safe: Iterate over all nodes in a queue,
 * without dequeuing them. Safe against deletion.
 * @head: head of the queue (struct cds_wfcq_head or __cds_wfcq_head pointer).
 * @tail: tail of the queue (struct cds_wfcq_tail pointer).
 * @node: iterator on the queue (struct cds_wfcq_node pointer).
 * @n: struct cds_wfcq_node pointer holding the next pointer (used
 *     internally).
 *
 * Content written into each node before enqueue is guaranteed to be
 * consistent, but no other memory ordering is ensured.
 * Dequeue/splice/iteration mutual exclusion should be ensured by the
 * caller.
 */
#define __cds_wfcq_for_each_blocking_safe(head, tail, node, n)		       \
	for (node = __cds_wfcq_first_blocking(head, tail),		       \
			n = (node ? __cds_wfcq_next_blocking(head, tail, node) : NULL); \
		node != NULL;						       \
		node = n, n = (node ? __cds_wfcq_next_blocking(head, tail, node) : NULL))

#ifdef __cplusplus
}
#endif

#endif /* _URCU_WFCQUEUE_H */
