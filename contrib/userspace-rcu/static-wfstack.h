#ifndef _URCU_STATIC_WFSTACK_H
#define _URCU_STATIC_WFSTACK_H

/*
 * urcu/static/wfstack.h
 *
 * Userspace RCU library - Stack with with wait-free push, blocking traversal.
 *
 * TO BE INCLUDED ONLY IN LGPL-COMPATIBLE CODE. See urcu/wfstack.h for
 * linking dynamically with the userspace rcu library.
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
 * without mutex. */

#include <pthread.h>
#include <assert.h>
#include <poll.h>
#include <stdbool.h>
#include <urcu/compiler.h>
#include <urcu/uatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CDS_WFS_END			((void *) 0x1UL)
#define CDS_WFS_ADAPT_ATTEMPTS		10	/* Retry if being set */
#define CDS_WFS_WAIT			10	/* Wait 10 ms if being set */

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

/*
 * cds_wfs_node_init: initialize wait-free stack node.
 */
static inline
void _cds_wfs_node_init(struct cds_wfs_node *node)
{
	node->next = NULL;
}

/*
 * __cds_wfs_init: initialize wait-free stack. Don't pair with
 * any destroy function.
 */
static inline void ___cds_wfs_init(struct __cds_wfs_stack *s)
{
	s->head = CDS_WFS_END;
}

/*
 * cds_wfs_init: initialize wait-free stack. Pair with
 * cds_wfs_destroy().
 */
static inline
void _cds_wfs_init(struct cds_wfs_stack *s)
{
	int ret;

	s->head = CDS_WFS_END;
	ret = pthread_mutex_init(&s->lock, NULL);
	assert(!ret);
}

/*
 * cds_wfs_destroy: destroy wait-free stack. Pair with
 * cds_wfs_init().
 */
static inline
void _cds_wfs_destroy(struct cds_wfs_stack *s)
{
	int ret = pthread_mutex_destroy(&s->lock);
	assert(!ret);
}

static inline bool ___cds_wfs_end(void *node)
{
	return node == CDS_WFS_END;
}

/*
 * cds_wfs_empty: return whether wait-free stack is empty.
 *
 * No memory barrier is issued. No mutual exclusion is required.
 */
static inline bool _cds_wfs_empty(cds_wfs_stack_ptr_t u_stack)
{
	struct __cds_wfs_stack *s = u_stack._s;

	return ___cds_wfs_end(CMM_LOAD_SHARED(s->head));
}

/*
 * cds_wfs_push: push a node into the stack.
 *
 * Issues a full memory barrier before push. No mutual exclusion is
 * required.
 *
 * Returns 0 if the stack was empty prior to adding the node.
 * Returns non-zero otherwise.
 */
static inline
int _cds_wfs_push(cds_wfs_stack_ptr_t u_stack, struct cds_wfs_node *node)
{
	struct __cds_wfs_stack *s = u_stack._s;
	struct cds_wfs_head *old_head, *new_head;

	assert(node->next == NULL);
	new_head = caa_container_of(node, struct cds_wfs_head, node);
	/*
	 * uatomic_xchg() implicit memory barrier orders earlier stores
	 * to node (setting it to NULL) before publication.
	 */
	old_head = uatomic_xchg(&s->head, new_head);
	/*
	 * At this point, dequeuers see a NULL node->next, they should
	 * busy-wait until node->next is set to old_head.
	 */
	CMM_STORE_SHARED(node->next, &old_head->node);
	return !___cds_wfs_end(old_head);
}

/*
 * Waiting for push to complete enqueue and return the next node.
 */
static inline struct cds_wfs_node *
___cds_wfs_node_sync_next(struct cds_wfs_node *node, int blocking)
{
	struct cds_wfs_node *next;
	int attempt = 0;

	/*
	 * Adaptative busy-looping waiting for push to complete.
	 */
	while ((next = CMM_LOAD_SHARED(node->next)) == NULL) {
		if (!blocking)
			return CDS_WFS_WOULDBLOCK;
		if (++attempt >= CDS_WFS_ADAPT_ATTEMPTS) {
			(void) poll(NULL, 0, CDS_WFS_WAIT);	/* Wait for 10ms */
			attempt = 0;
		} else {
			caa_cpu_relax();
		}
	}

	return next;
}

static inline
struct cds_wfs_node *
___cds_wfs_pop(cds_wfs_stack_ptr_t u_stack, int *state, int blocking)
{
	struct cds_wfs_head *head, *new_head;
	struct cds_wfs_node *next;
	struct __cds_wfs_stack *s = u_stack._s;

	if (state)
		*state = 0;
	for (;;) {
		head = CMM_LOAD_SHARED(s->head);
		if (___cds_wfs_end(head)) {
			return NULL;
		}
		next = ___cds_wfs_node_sync_next(&head->node, blocking);
		if (!blocking && next == CDS_WFS_WOULDBLOCK) {
			return CDS_WFS_WOULDBLOCK;
		}
		new_head = caa_container_of(next, struct cds_wfs_head, node);
		if (uatomic_cmpxchg(&s->head, head, new_head) == head) {
			if (state && ___cds_wfs_end(new_head))
				*state |= CDS_WFS_STATE_LAST;
			return &head->node;
		}
		if (!blocking) {
			return CDS_WFS_WOULDBLOCK;
		}
		/* busy-loop if head changed under us */
	}
}

/*
 * __cds_wfs_pop_with_state_blocking: pop a node from the stack, with state.
 *
 * Returns NULL if stack is empty.
 *
 * __cds_wfs_pop_blocking needs to be synchronized using one of the
 * following techniques:
 *
 * 1) Calling __cds_wfs_pop_blocking under rcu read lock critical
 *    section. The caller must wait for a grace period to pass before
 *    freeing the returned node or modifying the cds_wfs_node structure.
 * 2) Using mutual exclusion (e.g. mutexes) to protect
 *     __cds_wfs_pop_blocking and __cds_wfs_pop_all callers.
 * 3) Ensuring that only ONE thread can call __cds_wfs_pop_blocking()
 *    and __cds_wfs_pop_all(). (multi-provider/single-consumer scheme).
 *
 * "state" saves state flags atomically sampled with pop operation.
 */
static inline
struct cds_wfs_node *
___cds_wfs_pop_with_state_blocking(cds_wfs_stack_ptr_t u_stack, int *state)
{
	return ___cds_wfs_pop(u_stack, state, 1);
}

static inline
struct cds_wfs_node *
___cds_wfs_pop_blocking(cds_wfs_stack_ptr_t u_stack)
{
	return ___cds_wfs_pop_with_state_blocking(u_stack, NULL);
}

/*
 * __cds_wfs_pop_with_state_nonblocking: pop a node from the stack.
 *
 * Same as __cds_wfs_pop_with_state_blocking, but returns
 * CDS_WFS_WOULDBLOCK if it needs to block.
 *
 * "state" saves state flags atomically sampled with pop operation.
 */
static inline
struct cds_wfs_node *
___cds_wfs_pop_with_state_nonblocking(cds_wfs_stack_ptr_t u_stack, int *state)
{
	return ___cds_wfs_pop(u_stack, state, 0);
}

/*
 * __cds_wfs_pop_nonblocking: pop a node from the stack.
 *
 * Same as __cds_wfs_pop_blocking, but returns CDS_WFS_WOULDBLOCK if
 * it needs to block.
 */
static inline
struct cds_wfs_node *
___cds_wfs_pop_nonblocking(cds_wfs_stack_ptr_t u_stack)
{
	return ___cds_wfs_pop_with_state_nonblocking(u_stack, NULL);
}

/*
 * __cds_wfs_pop_all: pop all nodes from a stack.
 *
 * __cds_wfs_pop_all does not require any synchronization with other
 * push, nor with other __cds_wfs_pop_all, but requires synchronization
 * matching the technique used to synchronize __cds_wfs_pop_blocking:
 *
 * 1) If __cds_wfs_pop_blocking is called under rcu read lock critical
 *    section, both __cds_wfs_pop_blocking and cds_wfs_pop_all callers
 *    must wait for a grace period to pass before freeing the returned
 *    node or modifying the cds_wfs_node structure. However, no RCU
 *    read-side critical section is needed around __cds_wfs_pop_all.
 * 2) Using mutual exclusion (e.g. mutexes) to protect
 *     __cds_wfs_pop_blocking and __cds_wfs_pop_all callers.
 * 3) Ensuring that only ONE thread can call __cds_wfs_pop_blocking()
 *    and __cds_wfs_pop_all(). (multi-provider/single-consumer scheme).
 */
static inline
struct cds_wfs_head *
___cds_wfs_pop_all(cds_wfs_stack_ptr_t u_stack)
{
	struct __cds_wfs_stack *s = u_stack._s;
	struct cds_wfs_head *head;

	/*
	 * Implicit memory barrier after uatomic_xchg() matches implicit
	 * memory barrier before uatomic_xchg() in cds_wfs_push. It
	 * ensures that all nodes of the returned list are consistent.
	 * There is no need to issue memory barriers when iterating on
	 * the returned list, because the full memory barrier issued
	 * prior to each uatomic_cmpxchg, which each write to head, are
	 * taking care to order writes to each node prior to the full
	 * memory barrier after this uatomic_xchg().
	 */
	head = uatomic_xchg(&s->head, CDS_WFS_END);
	if (___cds_wfs_end(head))
		return NULL;
	return head;
}

/*
 * cds_wfs_pop_lock: lock stack pop-protection mutex.
 */
static inline void _cds_wfs_pop_lock(struct cds_wfs_stack *s)
{
	int ret;

	ret = pthread_mutex_lock(&s->lock);
	assert(!ret);
}

/*
 * cds_wfs_pop_unlock: unlock stack pop-protection mutex.
 */
static inline void _cds_wfs_pop_unlock(struct cds_wfs_stack *s)
{
	int ret;

	ret = pthread_mutex_unlock(&s->lock);
	assert(!ret);
}

/*
 * Call __cds_wfs_pop_with_state_blocking with an internal pop mutex held.
 */
static inline
struct cds_wfs_node *
_cds_wfs_pop_with_state_blocking(struct cds_wfs_stack *s, int *state)
{
	struct cds_wfs_node *retnode;

	_cds_wfs_pop_lock(s);
	retnode = ___cds_wfs_pop_with_state_blocking(s, state);
	_cds_wfs_pop_unlock(s);
	return retnode;
}

/*
 * Call _cds_wfs_pop_with_state_blocking without saving any state.
 */
static inline
struct cds_wfs_node *
_cds_wfs_pop_blocking(struct cds_wfs_stack *s)
{
	return _cds_wfs_pop_with_state_blocking(s, NULL);
}

/*
 * Call __cds_wfs_pop_all with an internal pop mutex held.
 */
static inline
struct cds_wfs_head *
_cds_wfs_pop_all_blocking(struct cds_wfs_stack *s)
{
	struct cds_wfs_head *rethead;

	_cds_wfs_pop_lock(s);
	rethead = ___cds_wfs_pop_all(s);
	_cds_wfs_pop_unlock(s);
	return rethead;
}

/*
 * cds_wfs_first: get first node of a popped stack.
 *
 * Content written into the node before enqueue is guaranteed to be
 * consistent, but no other memory ordering is ensured.
 *
 * Used by for-like iteration macros in urcu/wfstack.h:
 * cds_wfs_for_each_blocking()
 * cds_wfs_for_each_blocking_safe()
 *
 * Returns NULL if popped stack is empty, top stack node otherwise.
 */
static inline struct cds_wfs_node *
_cds_wfs_first(struct cds_wfs_head *head)
{
	if (___cds_wfs_end(head))
		return NULL;
	return &head->node;
}

static inline struct cds_wfs_node *
___cds_wfs_next(struct cds_wfs_node *node, int blocking)
{
	struct cds_wfs_node *next;

	next = ___cds_wfs_node_sync_next(node, blocking);
	/*
	 * CDS_WFS_WOULDBLOCK != CSD_WFS_END, so we can check for end
	 * even if ___cds_wfs_node_sync_next returns CDS_WFS_WOULDBLOCK,
	 * and still return CDS_WFS_WOULDBLOCK.
	 */
	if (___cds_wfs_end(next))
		return NULL;
	return next;
}

/*
 * cds_wfs_next_blocking: get next node of a popped stack.
 *
 * Content written into the node before enqueue is guaranteed to be
 * consistent, but no other memory ordering is ensured.
 *
 * Used by for-like iteration macros in urcu/wfstack.h:
 * cds_wfs_for_each_blocking()
 * cds_wfs_for_each_blocking_safe()
 *
 * Returns NULL if reached end of popped stack, non-NULL next stack
 * node otherwise.
 */
static inline struct cds_wfs_node *
_cds_wfs_next_blocking(struct cds_wfs_node *node)
{
	return ___cds_wfs_next(node, 1);
}


/*
 * cds_wfs_next_nonblocking: get next node of a popped stack.
 *
 * Same as cds_wfs_next_blocking, but returns CDS_WFS_WOULDBLOCK if it
 * needs to block.
 */
static inline struct cds_wfs_node *
_cds_wfs_next_nonblocking(struct cds_wfs_node *node)
{
	return ___cds_wfs_next(node, 0);
}

#ifdef __cplusplus
}
#endif

#endif /* _URCU_STATIC_WFSTACK_H */
