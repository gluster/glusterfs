#ifndef _URCU_WFCQUEUE_STATIC_H
#define _URCU_WFCQUEUE_STATIC_H

/*
 * urcu/static/wfcqueue.h
 *
 * Userspace RCU library - Concurrent Queue with Wait-Free Enqueue/Blocking Dequeue
 *
 * TO BE INCLUDED ONLY IN LGPL-COMPATIBLE CODE. See urcu/wfcqueue.h for
 * linking dynamically with the userspace rcu library.
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

/* Copied from userspace-rcu 0.10 because version 0.7 doesn't contain it. */

#include <pthread.h>
#include <assert.h>
#include <poll.h>
#include <stdbool.h>
#include <urcu/compiler.h>
#include <urcu/uatomic.h>

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
 *
 * Mutual exclusion of cds_wfcq_* / __cds_wfcq_* API
 *
 * Synchronization table:
 *
 * External synchronization techniques described in the API below is
 * required between pairs marked with "X". No external synchronization
 * required between pairs marked with "-".
 *
 * Legend:
 * [1] cds_wfcq_enqueue
 * [2] __cds_wfcq_splice (destination queue)
 * [3] __cds_wfcq_dequeue
 * [4] __cds_wfcq_splice (source queue)
 * [5] __cds_wfcq_first
 * [6] __cds_wfcq_next
 *
 *     [1] [2] [3] [4] [5] [6]
 * [1]  -   -   -   -   -   -
 * [2]  -   -   -   -   -   -
 * [3]  -   -   X   X   X   X
 * [4]  -   -   X   -   X   X
 * [5]  -   -   X   X   -   -
 * [6]  -   -   X   X   -   -
 *
 * Mutual exclusion can be ensured by holding cds_wfcq_dequeue_lock().
 *
 * For convenience, cds_wfcq_dequeue_blocking() and
 * cds_wfcq_splice_blocking() hold the dequeue lock.
 *
 * Besides locking, mutual exclusion of dequeue, splice and iteration
 * can be ensured by performing all of those operations from a single
 * thread, without requiring any lock.
 */

#define WFCQ_ADAPT_ATTEMPTS		10	/* Retry if being set */
#define WFCQ_WAIT			10	/* Wait 10 ms if being set */

/*
 * cds_wfcq_node_init: initialize wait-free queue node.
 */
static inline void _cds_wfcq_node_init(struct cds_wfcq_node *node)
{
	node->next = NULL;
}

/*
 * cds_wfcq_init: initialize wait-free queue (with lock). Pair with
 * cds_wfcq_destroy().
 */
static inline void _cds_wfcq_init(struct cds_wfcq_head *head,
		struct cds_wfcq_tail *tail)
{
	int ret;

	/* Set queue head and tail */
	_cds_wfcq_node_init(&head->node);
	tail->p = &head->node;
	ret = pthread_mutex_init(&head->lock, NULL);
	assert(!ret);
}

/*
 * cds_wfcq_destroy: destroy wait-free queue (with lock). Pair with
 * cds_wfcq_init().
 */
static inline void _cds_wfcq_destroy(struct cds_wfcq_head *head,
		struct cds_wfcq_tail *tail)
{
	int ret = pthread_mutex_destroy(&head->lock);
	assert(!ret);
}

/*
 * __cds_wfcq_init: initialize wait-free queue (without lock). Don't
 * pair with any destroy function.
 */
static inline void ___cds_wfcq_init(struct __cds_wfcq_head *head,
		struct cds_wfcq_tail *tail)
{
	/* Set queue head and tail */
	_cds_wfcq_node_init(&head->node);
	tail->p = &head->node;
}

/*
 * cds_wfcq_empty: return whether wait-free queue is empty.
 *
 * No memory barrier is issued. No mutual exclusion is required.
 *
 * We perform the test on head->node.next to check if the queue is
 * possibly empty, but we confirm this by checking if the tail pointer
 * points to the head node because the tail pointer is the linearisation
 * point of the enqueuers. Just checking the head next pointer could
 * make a queue appear empty if an enqueuer is preempted for a long time
 * between xchg() and setting the previous node's next pointer.
 */
static inline bool _cds_wfcq_empty(cds_wfcq_head_ptr_t u_head,
		struct cds_wfcq_tail *tail)
{
	struct __cds_wfcq_head *head = u_head._h;
	/*
	 * Queue is empty if no node is pointed by head->node.next nor
	 * tail->p. Even though the tail->p check is sufficient to find
	 * out of the queue is empty, we first check head->node.next as a
	 * common case to ensure that dequeuers do not frequently access
	 * enqueuer's tail->p cache line.
	 */
	return CMM_LOAD_SHARED(head->node.next) == NULL
		&& CMM_LOAD_SHARED(tail->p) == &head->node;
}

static inline void _cds_wfcq_dequeue_lock(struct cds_wfcq_head *head,
		struct cds_wfcq_tail *tail)
{
	int ret;

	ret = pthread_mutex_lock(&head->lock);
	assert(!ret);
}

static inline void _cds_wfcq_dequeue_unlock(struct cds_wfcq_head *head,
		struct cds_wfcq_tail *tail)
{
	int ret;

	ret = pthread_mutex_unlock(&head->lock);
	assert(!ret);
}

static inline bool ___cds_wfcq_append(cds_wfcq_head_ptr_t u_head,
		struct cds_wfcq_tail *tail,
		struct cds_wfcq_node *new_head,
		struct cds_wfcq_node *new_tail)
{
	struct __cds_wfcq_head *head = u_head._h;
	struct cds_wfcq_node *old_tail;

	/*
	 * Implicit memory barrier before uatomic_xchg() orders earlier
	 * stores to data structure containing node and setting
	 * node->next to NULL before publication.
	 */
	old_tail = uatomic_xchg(&tail->p, new_tail);

	/*
	 * Implicit memory barrier after uatomic_xchg() orders store to
	 * q->tail before store to old_tail->next.
	 *
	 * At this point, dequeuers see a NULL tail->p->next, which
	 * indicates that the queue is being appended to. The following
	 * store will append "node" to the queue from a dequeuer
	 * perspective.
	 */
	CMM_STORE_SHARED(old_tail->next, new_head);
	/*
	 * Return false if queue was empty prior to adding the node,
	 * else return true.
	 */
	return old_tail != &head->node;
}

/*
 * cds_wfcq_enqueue: enqueue a node into a wait-free queue.
 *
 * Issues a full memory barrier before enqueue. No mutual exclusion is
 * required.
 *
 * Returns false if the queue was empty prior to adding the node.
 * Returns true otherwise.
 */
static inline bool _cds_wfcq_enqueue(cds_wfcq_head_ptr_t head,
		struct cds_wfcq_tail *tail,
		struct cds_wfcq_node *new_tail)
{
	return ___cds_wfcq_append(head, tail, new_tail, new_tail);
}

/*
 * CDS_WFCQ_WAIT_SLEEP:
 *
 * By default, this sleeps for the given @msec milliseconds.
 * This is a macro which LGPL users may #define themselves before
 * including wfcqueue.h to override the default behavior (e.g.
 * to log a warning or perform other background work).
 */
#ifndef CDS_WFCQ_WAIT_SLEEP
#define CDS_WFCQ_WAIT_SLEEP(msec) ___cds_wfcq_wait_sleep(msec)
#endif

static inline void ___cds_wfcq_wait_sleep(int msec)
{
	(void) poll(NULL, 0, msec);
}

/*
 * ___cds_wfcq_busy_wait: adaptative busy-wait.
 *
 * Returns 1 if nonblocking and needs to block, 0 otherwise.
 */
static inline bool
___cds_wfcq_busy_wait(int *attempt, int blocking)
{
	if (!blocking)
		return 1;
	if (++(*attempt) >= WFCQ_ADAPT_ATTEMPTS) {
		CDS_WFCQ_WAIT_SLEEP(WFCQ_WAIT);		/* Wait for 10ms */
		*attempt = 0;
	} else {
		caa_cpu_relax();
	}
	return 0;
}

/*
 * Waiting for enqueuer to complete enqueue and return the next node.
 */
static inline struct cds_wfcq_node *
___cds_wfcq_node_sync_next(struct cds_wfcq_node *node, int blocking)
{
	struct cds_wfcq_node *next;
	int attempt = 0;

	/*
	 * Adaptative busy-looping waiting for enqueuer to complete enqueue.
	 */
	while ((next = CMM_LOAD_SHARED(node->next)) == NULL) {
		if (___cds_wfcq_busy_wait(&attempt, blocking))
			return CDS_WFCQ_WOULDBLOCK;
	}

	return next;
}

static inline struct cds_wfcq_node *
___cds_wfcq_first(cds_wfcq_head_ptr_t u_head,
		struct cds_wfcq_tail *tail,
		int blocking)
{
	struct __cds_wfcq_head *head = u_head._h;
	struct cds_wfcq_node *node;

	if (_cds_wfcq_empty(__cds_wfcq_head_cast(head), tail))
		return NULL;
	node = ___cds_wfcq_node_sync_next(&head->node, blocking);
	/* Load head->node.next before loading node's content */
	cmm_smp_read_barrier_depends();
	return node;
}

/*
 * __cds_wfcq_first_blocking: get first node of a queue, without dequeuing.
 *
 * Content written into the node before enqueue is guaranteed to be
 * consistent, but no other memory ordering is ensured.
 * Dequeue/splice/iteration mutual exclusion should be ensured by the
 * caller.
 *
 * Used by for-like iteration macros in urcu/wfqueue.h:
 * __cds_wfcq_for_each_blocking()
 * __cds_wfcq_for_each_blocking_safe()
 *
 * Returns NULL if queue is empty, first node otherwise.
 */
static inline struct cds_wfcq_node *
___cds_wfcq_first_blocking(cds_wfcq_head_ptr_t head,
		struct cds_wfcq_tail *tail)
{
	return ___cds_wfcq_first(head, tail, 1);
}


/*
 * __cds_wfcq_first_nonblocking: get first node of a queue, without dequeuing.
 *
 * Same as __cds_wfcq_first_blocking, but returns CDS_WFCQ_WOULDBLOCK if
 * it needs to block.
 */
static inline struct cds_wfcq_node *
___cds_wfcq_first_nonblocking(cds_wfcq_head_ptr_t head,
		struct cds_wfcq_tail *tail)
{
	return ___cds_wfcq_first(head, tail, 0);
}

static inline struct cds_wfcq_node *
___cds_wfcq_next(cds_wfcq_head_ptr_t head,
		struct cds_wfcq_tail *tail,
		struct cds_wfcq_node *node,
		int blocking)
{
	struct cds_wfcq_node *next;

	/*
	 * Even though the following tail->p check is sufficient to find
	 * out if we reached the end of the queue, we first check
	 * node->next as a common case to ensure that iteration on nodes
	 * do not frequently access enqueuer's tail->p cache line.
	 */
	if ((next = CMM_LOAD_SHARED(node->next)) == NULL) {
		/* Load node->next before tail->p */
		cmm_smp_rmb();
		if (CMM_LOAD_SHARED(tail->p) == node)
			return NULL;
		next = ___cds_wfcq_node_sync_next(node, blocking);
	}
	/* Load node->next before loading next's content */
	cmm_smp_read_barrier_depends();
	return next;
}

/*
 * __cds_wfcq_next_blocking: get next node of a queue, without dequeuing.
 *
 * Content written into the node before enqueue is guaranteed to be
 * consistent, but no other memory ordering is ensured.
 * Dequeue/splice/iteration mutual exclusion should be ensured by the
 * caller.
 *
 * Used by for-like iteration macros in urcu/wfqueue.h:
 * __cds_wfcq_for_each_blocking()
 * __cds_wfcq_for_each_blocking_safe()
 *
 * Returns NULL if reached end of queue, non-NULL next queue node
 * otherwise.
 */
static inline struct cds_wfcq_node *
___cds_wfcq_next_blocking(cds_wfcq_head_ptr_t head,
		struct cds_wfcq_tail *tail,
		struct cds_wfcq_node *node)
{
	return ___cds_wfcq_next(head, tail, node, 1);
}

/*
 * __cds_wfcq_next_blocking: get next node of a queue, without dequeuing.
 *
 * Same as __cds_wfcq_next_blocking, but returns CDS_WFCQ_WOULDBLOCK if
 * it needs to block.
 */
static inline struct cds_wfcq_node *
___cds_wfcq_next_nonblocking(cds_wfcq_head_ptr_t head,
		struct cds_wfcq_tail *tail,
		struct cds_wfcq_node *node)
{
	return ___cds_wfcq_next(head, tail, node, 0);
}

static inline struct cds_wfcq_node *
___cds_wfcq_dequeue_with_state(cds_wfcq_head_ptr_t u_head,
		struct cds_wfcq_tail *tail,
		int *state,
		int blocking)
{
	struct __cds_wfcq_head *head = u_head._h;
	struct cds_wfcq_node *node, *next;

	if (state)
		*state = 0;

	if (_cds_wfcq_empty(__cds_wfcq_head_cast(head), tail)) {
		return NULL;
	}

	node = ___cds_wfcq_node_sync_next(&head->node, blocking);
	if (!blocking && node == CDS_WFCQ_WOULDBLOCK) {
		return CDS_WFCQ_WOULDBLOCK;
	}

	if ((next = CMM_LOAD_SHARED(node->next)) == NULL) {
		/*
		 * @node is probably the only node in the queue.
		 * Try to move the tail to &q->head.
		 * q->head.next is set to NULL here, and stays
		 * NULL if the cmpxchg succeeds. Should the
		 * cmpxchg fail due to a concurrent enqueue, the
		 * q->head.next will be set to the next node.
		 * The implicit memory barrier before
		 * uatomic_cmpxchg() orders load node->next
		 * before loading q->tail.
		 * The implicit memory barrier before uatomic_cmpxchg
		 * orders load q->head.next before loading node's
		 * content.
		 */
		_cds_wfcq_node_init(&head->node);
		if (uatomic_cmpxchg(&tail->p, node, &head->node) == node) {
			if (state)
				*state |= CDS_WFCQ_STATE_LAST;
			return node;
		}
		next = ___cds_wfcq_node_sync_next(node, blocking);
		/*
		 * In nonblocking mode, if we would need to block to
		 * get node's next, set the head next node pointer
		 * (currently NULL) back to its original value.
		 */
		if (!blocking && next == CDS_WFCQ_WOULDBLOCK) {
			head->node.next = node;
			return CDS_WFCQ_WOULDBLOCK;
		}
	}

	/*
	 * Move queue head forward.
	 */
	head->node.next = next;

	/* Load q->head.next before loading node's content */
	cmm_smp_read_barrier_depends();
	return node;
}

/*
 * __cds_wfcq_dequeue_with_state_blocking: dequeue node from queue, with state.
 *
 * Content written into the node before enqueue is guaranteed to be
 * consistent, but no other memory ordering is ensured.
 * It is valid to reuse and free a dequeued node immediately.
 * Dequeue/splice/iteration mutual exclusion should be ensured by the
 * caller.
 */
static inline struct cds_wfcq_node *
___cds_wfcq_dequeue_with_state_blocking(cds_wfcq_head_ptr_t head,
		struct cds_wfcq_tail *tail, int *state)
{
	return ___cds_wfcq_dequeue_with_state(head, tail, state, 1);
}

/*
 * ___cds_wfcq_dequeue_blocking: dequeue node from queue.
 *
 * Same as __cds_wfcq_dequeue_with_state_blocking, but without saving
 * state.
 */
static inline struct cds_wfcq_node *
___cds_wfcq_dequeue_blocking(cds_wfcq_head_ptr_t head,
		struct cds_wfcq_tail *tail)
{
	return ___cds_wfcq_dequeue_with_state_blocking(head, tail, NULL);
}

/*
 * __cds_wfcq_dequeue_with_state_nonblocking: dequeue node, with state.
 *
 * Same as __cds_wfcq_dequeue_blocking, but returns CDS_WFCQ_WOULDBLOCK
 * if it needs to block.
 */
static inline struct cds_wfcq_node *
___cds_wfcq_dequeue_with_state_nonblocking(cds_wfcq_head_ptr_t head,
		struct cds_wfcq_tail *tail, int *state)
{
	return ___cds_wfcq_dequeue_with_state(head, tail, state, 0);
}

/*
 * ___cds_wfcq_dequeue_nonblocking: dequeue node from queue.
 *
 * Same as __cds_wfcq_dequeue_with_state_nonblocking, but without saving
 * state.
 */
static inline struct cds_wfcq_node *
___cds_wfcq_dequeue_nonblocking(cds_wfcq_head_ptr_t head,
		struct cds_wfcq_tail *tail)
{
	return ___cds_wfcq_dequeue_with_state_nonblocking(head, tail, NULL);
}

/*
 * __cds_wfcq_splice: enqueue all src_q nodes at the end of dest_q.
 *
 * Dequeue all nodes from src_q.
 * dest_q must be already initialized.
 * Mutual exclusion for src_q should be ensured by the caller as
 * specified in the "Synchronisation table".
 * Returns enum cds_wfcq_ret which indicates the state of the src or
 * dest queue.
 */
static inline enum cds_wfcq_ret
___cds_wfcq_splice(
		cds_wfcq_head_ptr_t u_dest_q_head,
		struct cds_wfcq_tail *dest_q_tail,
		cds_wfcq_head_ptr_t u_src_q_head,
		struct cds_wfcq_tail *src_q_tail,
		int blocking)
{
	struct __cds_wfcq_head *dest_q_head = u_dest_q_head._h;
	struct __cds_wfcq_head *src_q_head = u_src_q_head._h;
	struct cds_wfcq_node *head, *tail;
	int attempt = 0;

	/*
	 * Initial emptiness check to speed up cases where queue is
	 * empty: only require loads to check if queue is empty.
	 */
	if (_cds_wfcq_empty(__cds_wfcq_head_cast(src_q_head), src_q_tail))
		return CDS_WFCQ_RET_SRC_EMPTY;

	for (;;) {
		/*
		 * Open-coded _cds_wfcq_empty() by testing result of
		 * uatomic_xchg, as well as tail pointer vs head node
		 * address.
		 */
		head = uatomic_xchg(&src_q_head->node.next, NULL);
		if (head)
			break;	/* non-empty */
		if (CMM_LOAD_SHARED(src_q_tail->p) == &src_q_head->node)
			return CDS_WFCQ_RET_SRC_EMPTY;
		if (___cds_wfcq_busy_wait(&attempt, blocking))
			return CDS_WFCQ_RET_WOULDBLOCK;
	}

	/*
	 * Memory barrier implied before uatomic_xchg() orders store to
	 * src_q->head before store to src_q->tail. This is required by
	 * concurrent enqueue on src_q, which exchanges the tail before
	 * updating the previous tail's next pointer.
	 */
	tail = uatomic_xchg(&src_q_tail->p, &src_q_head->node);

	/*
	 * Append the spliced content of src_q into dest_q. Does not
	 * require mutual exclusion on dest_q (wait-free).
	 */
	if (___cds_wfcq_append(__cds_wfcq_head_cast(dest_q_head), dest_q_tail,
			head, tail))
		return CDS_WFCQ_RET_DEST_NON_EMPTY;
	else
		return CDS_WFCQ_RET_DEST_EMPTY;
}

/*
 * __cds_wfcq_splice_blocking: enqueue all src_q nodes at the end of dest_q.
 *
 * Dequeue all nodes from src_q.
 * dest_q must be already initialized.
 * Mutual exclusion for src_q should be ensured by the caller as
 * specified in the "Synchronisation table".
 * Returns enum cds_wfcq_ret which indicates the state of the src or
 * dest queue. Never returns CDS_WFCQ_RET_WOULDBLOCK.
 */
static inline enum cds_wfcq_ret
___cds_wfcq_splice_blocking(
		cds_wfcq_head_ptr_t dest_q_head,
		struct cds_wfcq_tail *dest_q_tail,
		cds_wfcq_head_ptr_t src_q_head,
		struct cds_wfcq_tail *src_q_tail)
{
	return ___cds_wfcq_splice(dest_q_head, dest_q_tail,
		src_q_head, src_q_tail, 1);
}

/*
 * __cds_wfcq_splice_nonblocking: enqueue all src_q nodes at the end of dest_q.
 *
 * Same as __cds_wfcq_splice_blocking, but returns
 * CDS_WFCQ_RET_WOULDBLOCK if it needs to block.
 */
static inline enum cds_wfcq_ret
___cds_wfcq_splice_nonblocking(
		cds_wfcq_head_ptr_t dest_q_head,
		struct cds_wfcq_tail *dest_q_tail,
		cds_wfcq_head_ptr_t src_q_head,
		struct cds_wfcq_tail *src_q_tail)
{
	return ___cds_wfcq_splice(dest_q_head, dest_q_tail,
		src_q_head, src_q_tail, 0);
}

/*
 * cds_wfcq_dequeue_with_state_blocking: dequeue a node from a wait-free queue.
 *
 * Content written into the node before enqueue is guaranteed to be
 * consistent, but no other memory ordering is ensured.
 * Mutual exclusion with cds_wfcq_splice_blocking and dequeue lock is
 * ensured.
 * It is valid to reuse and free a dequeued node immediately.
 */
static inline struct cds_wfcq_node *
_cds_wfcq_dequeue_with_state_blocking(struct cds_wfcq_head *head,
		struct cds_wfcq_tail *tail, int *state)
{
	struct cds_wfcq_node *retval;

	_cds_wfcq_dequeue_lock(head, tail);
	retval = ___cds_wfcq_dequeue_with_state_blocking(cds_wfcq_head_cast(head),
			tail, state);
	_cds_wfcq_dequeue_unlock(head, tail);
	return retval;
}

/*
 * cds_wfcq_dequeue_blocking: dequeue node from queue.
 *
 * Same as cds_wfcq_dequeue_blocking, but without saving state.
 */
static inline struct cds_wfcq_node *
_cds_wfcq_dequeue_blocking(struct cds_wfcq_head *head,
		struct cds_wfcq_tail *tail)
{
	return _cds_wfcq_dequeue_with_state_blocking(head, tail, NULL);
}

/*
 * cds_wfcq_splice_blocking: enqueue all src_q nodes at the end of dest_q.
 *
 * Dequeue all nodes from src_q.
 * dest_q must be already initialized.
 * Content written into the node before enqueue is guaranteed to be
 * consistent, but no other memory ordering is ensured.
 * Mutual exclusion with cds_wfcq_dequeue_blocking and dequeue lock is
 * ensured.
 * Returns enum cds_wfcq_ret which indicates the state of the src or
 * dest queue. Never returns CDS_WFCQ_RET_WOULDBLOCK.
 */
static inline enum cds_wfcq_ret
_cds_wfcq_splice_blocking(
		struct cds_wfcq_head *dest_q_head,
		struct cds_wfcq_tail *dest_q_tail,
		struct cds_wfcq_head *src_q_head,
		struct cds_wfcq_tail *src_q_tail)
{
	enum cds_wfcq_ret ret;

	_cds_wfcq_dequeue_lock(src_q_head, src_q_tail);
	ret = ___cds_wfcq_splice_blocking(cds_wfcq_head_cast(dest_q_head), dest_q_tail,
			cds_wfcq_head_cast(src_q_head), src_q_tail);
	_cds_wfcq_dequeue_unlock(src_q_head, src_q_tail);
	return ret;
}

#ifdef __cplusplus
}
#endif

#endif /* _URCU_WFCQUEUE_STATIC_H */
