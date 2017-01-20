/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "list.h"
#include "compat.h"
#include "compat-errno.h"
#include "common-utils.h"
#include "call-stub.h"
#include "statedump.h"
#include "defaults.h"
#include "write-behind-mem-types.h"
#include "write-behind-messages.h"

#define MAX_VECTOR_COUNT          8
#define WB_AGGREGATE_SIZE         131072 /* 128 KB */
#define WB_WINDOW_SIZE            1048576 /* 1MB */

typedef struct list_head list_head_t;
struct wb_conf;
struct wb_inode;

typedef struct wb_inode {
        ssize_t      window_conf;
        ssize_t      window_current;
	ssize_t      transit; /* size of data stack_wound, and yet
				 to be fulfilled (wb_fulfill_cbk).
				 used for trickling_writes
			      */

        list_head_t  all;    /* All requests, from enqueue() till destroy().
				Used only for resetting generation
				number when empty.
			     */
        list_head_t  todo;   /* Work to do (i.e, STACK_WIND to server).
				Once we STACK_WIND, the entry is taken
				off the list. If it is non-sync write,
				then we continue to track it via @liability
				or @temptation depending on the status
				of its writeback.
			     */
        list_head_t  liability;   /* Non-sync writes which are lied
				     (STACK_UNWIND'ed to caller) but ack
				     from server not yet complete. This
				     is the "liability" which we hold, and
				     must guarantee that dependent operations
				     which arrive later (which overlap, etc.)
				     are issued only after their dependencies
				     in this list are "fulfilled".

				     Server acks for entries in this list
				     shrinks the window.

				     The sum total of all req->write_size
				     of entries in this list must be kept less
				     than the permitted window size.
				  */
        list_head_t  temptation;  /* Operations for which we are tempted
				     to 'lie' (write-behind), but temporarily
				     holding off (because of insufficient
				     window capacity, etc.)

				     This is the list to look at to grow
				     the window (in __wb_pick_unwinds()).

				     Entries typically get chosen from
				     write-behind from this list, and therefore
				     get "upgraded" to the "liability" list.
			     */
	list_head_t  wip; /* List of write calls in progress, SYNC or non-SYNC
			     which are currently STACK_WIND'ed towards the server.
			     This is for guaranteeing that no two overlapping
			     writes are in progress at the same time. Modules
			     like eager-lock in AFR depend on this behavior.
			  */
	uint64_t     gen;    /* Liability generation number. Represents
				the current 'state' of liability. Every
				new addition to the liability list bumps
				the generation number.

				a newly arrived request is only required
				to perform causal checks against the entries
				in the liability list which were present
				at the time of its addition. the generation
				number at the time of its addition is stored
				in the request and used during checks.

				the liability list can grow while the request
				waits in the todo list waiting for its
				dependent operations to complete. however
				it is not of the request's concern to depend
				itself on those new entries which arrived
				after it arrived (i.e, those that have a
				liability generation higher than itself)
			     */
	size_t       size; /* Size of the file to catch write after EOF. */
        gf_lock_t    lock;
        xlator_t    *this;
        int          dontsync; /* If positive, dont pick lies for
                                * winding. This is needed to break infinite
                                * recursion during invocation of
                                * wb_process_queue from
                                * wb_fulfill_cbk in case of an
                                * error during fulfill.
                                */

} wb_inode_t;


typedef struct wb_request {
        list_head_t           all;
        list_head_t           todo;
	list_head_t           lie;  /* either in @liability or @temptation */
        list_head_t           winds;
        list_head_t           unwinds;
        list_head_t           wip;

        call_stub_t          *stub;

        ssize_t               write_size;  /* currently held size
					      (after collapsing) */
	size_t                orig_size;   /* size which arrived with the request.
					      This is the size by which we grow
					      the window when unwinding the frame.
					   */
        size_t                total_size;  /* valid only in @head in wb_fulfill().
					      This is the size with which we perform
					      STACK_WIND to server and therefore the
					      amount by which we shrink the window.
					   */

	int                   op_ret;
	int                   op_errno;

        int32_t               refcount;
        wb_inode_t           *wb_inode;
        glusterfs_fop_t       fop;
        gf_lkowner_t          lk_owner;
	struct iobref        *iobref;
	uint64_t              gen;  /* inode liability state at the time of
				       request arrival */

	fd_t                 *fd;
        int                   wind_count;    /* number of sync-attempts. Only
                                                for debug purposes */
	struct {
		size_t        size;          /* 0 size == till infinity */
		off_t         off;
		int           append:1;      /* offset is invalid. only one
						outstanding append at a time */
		int           tempted:1;     /* true only for non-sync writes */
		int           lied:1;        /* sin committed */
		int           fulfilled:1;   /* got server acknowledgement */
		int           go:1;          /* enough aggregating, good to go */
	} ordering;

        /* for debug purposes. A request might outlive the fop it is
         * representing. So, preserve essential info for logging.
         */
        uint64_t              unique;
        uuid_t                gfid;
} wb_request_t;


typedef struct wb_conf {
        uint64_t         aggregate_size;
        uint64_t         window_size;
        gf_boolean_t     flush_behind;
        gf_boolean_t     trickling_writes;
	gf_boolean_t     strict_write_ordering;
	gf_boolean_t     strict_O_DIRECT;
        gf_boolean_t     resync_after_fsync;
} wb_conf_t;


void
wb_process_queue (wb_inode_t *wb_inode);


wb_inode_t *
__wb_inode_ctx_get (xlator_t *this, inode_t *inode)
{
        uint64_t    value    = 0;
        wb_inode_t *wb_inode = NULL;

        __inode_ctx_get (inode, this, &value);
        wb_inode = (wb_inode_t *)(unsigned long) value;

        return wb_inode;
}


wb_inode_t *
wb_inode_ctx_get (xlator_t *this, inode_t *inode)
{
        wb_inode_t *wb_inode = NULL;

        GF_VALIDATE_OR_GOTO ("write-behind", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        LOCK (&inode->lock);
        {
                wb_inode = __wb_inode_ctx_get (this, inode);
        }
        UNLOCK (&inode->lock);
out:
        return wb_inode;
}


/*
  Below is a succinct explanation of the code deciding whether two regions
  overlap, from Pavan <tcp@gluster.com>.

  For any two ranges to be non-overlapping, either the end of the first
  range is lesser than the start of the second, or vice versa. Example -

  <--------->       <-------------->
  p         q       x              y

  ( q < x ) or (y < p) = > No overlap.

  To check for *overlap*, we can negate this (using de morgan's laws), and
  it becomes -

  (q >= x ) and (y >= p)

  Either that, or you write the negation using -

  if (! ((q < x) or (y < p)) ) {
  "Overlap"
  }
*/

gf_boolean_t
wb_requests_overlap (wb_request_t *req1, wb_request_t *req2)
{
        uint64_t         r1_start = 0;
	uint64_t         r1_end = 0;
	uint64_t         r2_start = 0;
	uint64_t         r2_end = 0;
        enum _gf_boolean do_overlap = 0;

        r1_start = req1->ordering.off;
	if (req1->ordering.size)
		r1_end = r1_start + req1->ordering.size - 1;
	else
		r1_end = ULLONG_MAX;

        r2_start = req2->ordering.off;
	if (req2->ordering.size)
		r2_end = r2_start + req2->ordering.size - 1;
	else
		r2_end = ULLONG_MAX;

        do_overlap = ((r1_end >= r2_start) && (r2_end >= r1_start));

        return do_overlap;
}


gf_boolean_t
wb_requests_conflict (wb_request_t *lie, wb_request_t *req)
{
	wb_conf_t  *conf = NULL;

	conf = req->wb_inode->this->private;

	if (lie == req)
		/* request cannot conflict with itself */
		return _gf_false;

	if (lie->gen >= req->gen)
		/* this liability entry was behind
		   us in the todo list */
		return _gf_false;

        /* requests from different fd do not conflict with each other. */
        if (req->fd && (req->fd != lie->fd))
                return _gf_false;

	if (lie->ordering.append)
		/* all modifications wait for the completion
		   of outstanding append */
		return _gf_true;

	if (conf->strict_write_ordering)
		/* We are sure (lie->gen < req->gen) by now. So
		   skip overlap check if strict write ordering is
		   requested and always return "conflict" against a
		   lower generation lie. */
		return _gf_true;

	return wb_requests_overlap (lie, req);
}


wb_request_t *
wb_liability_has_conflict (wb_inode_t *wb_inode, wb_request_t *req)
{
        wb_request_t *each     = NULL;

        list_for_each_entry (each, &wb_inode->liability, lie) {
		if (wb_requests_conflict (each, req)
                    && (!each->ordering.fulfilled))
                        /* A fulfilled request shouldn't block another
                         * request (even a dependent one) from winding.
                         */
			return each;
        }

        return NULL;
}


wb_request_t *
wb_wip_has_conflict (wb_inode_t *wb_inode, wb_request_t *req)
{
        wb_request_t *each     = NULL;

	if (req->stub->fop != GF_FOP_WRITE)
		/* non-writes fundamentally never conflict with WIP requests */
		return NULL;

        list_for_each_entry (each, &wb_inode->wip, wip) {
		if (each == req)
			/* request never conflicts with itself,
			   though this condition should never occur.
			*/
			continue;

		if (wb_requests_overlap (each, req))
			return each;
        }

        return NULL;
}


static int
__wb_request_unref (wb_request_t *req)
{
        int         ret      = -1;
	wb_inode_t *wb_inode = NULL;
        char        gfid[64] = {0, };

	wb_inode = req->wb_inode;

        if (req->refcount <= 0) {
                uuid_utoa_r (req->gfid, gfid);

                gf_msg ("wb-request", GF_LOG_WARNING,
                        0, WRITE_BEHIND_MSG_RES_UNAVAILABLE,
                        "(unique=%"PRIu64", fop=%s, gfid=%s, gen=%"PRIu64"): "
                        "refcount(%d) is <= 0 ",
                        req->unique, gf_fop_list[req->fop], gfid, req->gen,
                        req->refcount);
                goto out;
        }

        ret = --req->refcount;
        if (req->refcount == 0) {
                uuid_utoa_r (req->gfid, gfid);

                gf_log_callingfn (wb_inode->this->name, GF_LOG_DEBUG,
                                  "(unique = %"PRIu64", fop=%s, gfid=%s, "
                                  "gen=%"PRIu64"): destroying request, "
                                  "removing from all queues", req->unique,
                                  gf_fop_list[req->fop], gfid, req->gen);

                list_del_init (&req->todo);
                list_del_init (&req->lie);
		list_del_init (&req->wip);

		list_del_init (&req->all);
		if (list_empty (&wb_inode->all)) {
			wb_inode->gen = 0;
			/* in case of accounting errors? */
			wb_inode->window_current = 0;
		}

		list_del_init (&req->winds);
		list_del_init (&req->unwinds);

                if (req->stub) {
                        call_stub_destroy (req->stub);
			req->stub = NULL;
                }

		if (req->iobref)
			iobref_unref (req->iobref);

		if (req->fd)
			fd_unref (req->fd);

                GF_FREE (req);
        }
out:
        return ret;
}


static int
wb_request_unref (wb_request_t *req)
{
        wb_inode_t *wb_inode = NULL;
        int         ret      = -1;

        GF_VALIDATE_OR_GOTO ("write-behind", req, out);

        wb_inode = req->wb_inode;

        LOCK (&wb_inode->lock);
        {
                ret = __wb_request_unref (req);
        }
        UNLOCK (&wb_inode->lock);

out:
        return ret;
}


static wb_request_t *
__wb_request_ref (wb_request_t *req)
{
        GF_VALIDATE_OR_GOTO ("write-behind", req, out);

        if (req->refcount < 0) {
                gf_msg ("wb-request", GF_LOG_WARNING, 0,
                        WRITE_BEHIND_MSG_RES_UNAVAILABLE,
                        "refcount(%d) is < 0", req->refcount);
                req = NULL;
                goto out;
        }

        req->refcount++;

out:
        return req;
}


wb_request_t *
wb_request_ref (wb_request_t *req)
{
        wb_inode_t *wb_inode = NULL;

        GF_VALIDATE_OR_GOTO ("write-behind", req, out);

        wb_inode = req->wb_inode;
        LOCK (&wb_inode->lock);
        {
                req = __wb_request_ref (req);
        }
        UNLOCK (&wb_inode->lock);

out:
        return req;
}


gf_boolean_t
wb_enqueue_common (wb_inode_t *wb_inode, call_stub_t *stub, int tempted)
{
        wb_request_t *req     = NULL;
        inode_t   *inode   = NULL;

        GF_VALIDATE_OR_GOTO ("write-behind", wb_inode, out);
        GF_VALIDATE_OR_GOTO (wb_inode->this->name, stub, out);

        req = GF_CALLOC (1, sizeof (*req), gf_wb_mt_wb_request_t);
        if (!req)
                goto out;

        INIT_LIST_HEAD (&req->all);
        INIT_LIST_HEAD (&req->todo);
        INIT_LIST_HEAD (&req->lie);
        INIT_LIST_HEAD (&req->winds);
        INIT_LIST_HEAD (&req->unwinds);
        INIT_LIST_HEAD (&req->wip);

        req->stub = stub;
        req->wb_inode = wb_inode;
        req->fop  = stub->fop;
	req->ordering.tempted = tempted;
        req->unique = stub->frame->root->unique;

        inode = ((stub->args.fd != NULL) ? stub->args.fd->inode
                 : stub->args.loc.inode);

        if (inode)
                gf_uuid_copy (req->gfid, inode->gfid);

        if (stub->fop == GF_FOP_WRITE) {
                req->write_size = iov_length (stub->args.vector,
					      stub->args.count);

		/* req->write_size can change as we collapse
		   small writes. But the window needs to grow
		   only by how much we acknowledge the app. so
		   copy the original size in orig_size for the
		   purpose of accounting.
		*/
		req->orig_size = req->write_size;

		/* Let's be optimistic that we can
		   lie about it
		*/
		req->op_ret = req->write_size;
		req->op_errno = 0;

		if (stub->args.fd->flags & O_APPEND)
			req->ordering.append = 1;
        }

        req->lk_owner = stub->frame->root->lk_owner;

	switch (stub->fop) {
	case GF_FOP_WRITE:
		LOCK (&wb_inode->lock);
		{
			if (wb_inode->size < stub->args.offset) {
				req->ordering.off = wb_inode->size;
				req->ordering.size = stub->args.offset
				                     + req->write_size
						     - wb_inode->size;
			} else {
				req->ordering.off = stub->args.offset;
				req->ordering.size = req->write_size;
			}

			if (wb_inode->size < stub->args.offset + req->write_size)
				wb_inode->size = stub->args.offset
				                 + req->write_size;
		}
		UNLOCK (&wb_inode->lock);

		req->fd = fd_ref (stub->args.fd);

		break;
	case GF_FOP_READ:
		req->ordering.off = stub->args.offset;
		req->ordering.size = stub->args.size;

		req->fd = fd_ref (stub->args.fd);

		break;
	case GF_FOP_TRUNCATE:
		req->ordering.off = stub->args.offset;
		req->ordering.size = 0; /* till infinity */
		LOCK (&wb_inode->lock);
		{
			wb_inode->size = req->ordering.off;
		}
		UNLOCK (&wb_inode->lock);
		break;
	case GF_FOP_FTRUNCATE:
		req->ordering.off = stub->args.offset;
		req->ordering.size = 0; /* till infinity */
		LOCK (&wb_inode->lock);
		{
			wb_inode->size = req->ordering.off;
		}
		UNLOCK (&wb_inode->lock);

		req->fd = fd_ref (stub->args.fd);

		break;
	default:
                if (stub && stub->args.fd)
                        req->fd = fd_ref (stub->args.fd);

		break;
	}

        LOCK (&wb_inode->lock);
        {
                list_add_tail (&req->all, &wb_inode->all);

		req->gen = wb_inode->gen;

                list_add_tail (&req->todo, &wb_inode->todo);
		__wb_request_ref (req); /* for wind */

		if (req->ordering.tempted) {
			list_add_tail (&req->lie, &wb_inode->temptation);
			__wb_request_ref (req); /* for unwind */
		}
        }
        UNLOCK (&wb_inode->lock);

out:
	if (!req)
		return _gf_false;

	return _gf_true;
}


gf_boolean_t
wb_enqueue (wb_inode_t *wb_inode, call_stub_t *stub)
{
	return wb_enqueue_common (wb_inode, stub, 0);
}


gf_boolean_t
wb_enqueue_tempted (wb_inode_t *wb_inode, call_stub_t *stub)
{
	return wb_enqueue_common (wb_inode, stub, 1);
}


wb_inode_t *
__wb_inode_create (xlator_t *this, inode_t *inode)
{
        wb_inode_t *wb_inode = NULL;
        wb_conf_t  *conf     = NULL;
        int         ret      = 0;

        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        conf = this->private;

        wb_inode = GF_CALLOC (1, sizeof (*wb_inode), gf_wb_mt_wb_inode_t);
        if (!wb_inode)
                goto out;

        INIT_LIST_HEAD (&wb_inode->all);
        INIT_LIST_HEAD (&wb_inode->todo);
        INIT_LIST_HEAD (&wb_inode->liability);
        INIT_LIST_HEAD (&wb_inode->temptation);
        INIT_LIST_HEAD (&wb_inode->wip);

        wb_inode->this = this;

        wb_inode->window_conf = conf->window_size;

        LOCK_INIT (&wb_inode->lock);

        ret = __inode_ctx_put (inode, this, (uint64_t)(unsigned long)wb_inode);
        if (ret) {
                GF_FREE (wb_inode);
                wb_inode = NULL;
        }

out:
        return wb_inode;
}


wb_inode_t *
wb_inode_create (xlator_t *this, inode_t *inode)
{
        wb_inode_t *wb_inode = NULL;

        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        LOCK (&inode->lock);
        {
                wb_inode = __wb_inode_ctx_get (this, inode);
                if (!wb_inode)
                        wb_inode = __wb_inode_create (this, inode);
        }
        UNLOCK (&inode->lock);

out:
        return wb_inode;
}


void
wb_inode_destroy (wb_inode_t *wb_inode)
{
        GF_VALIDATE_OR_GOTO ("write-behind", wb_inode, out);

        LOCK_DESTROY (&wb_inode->lock);
        GF_FREE (wb_inode);
out:
        return;
}


void
__wb_fulfill_request (wb_request_t *req)
{
	wb_inode_t *wb_inode = NULL;
        char        gfid[64] = {0, };

	wb_inode = req->wb_inode;

	req->ordering.fulfilled = 1;
	wb_inode->window_current -= req->total_size;
	wb_inode->transit -= req->total_size;

        uuid_utoa_r (req->gfid, gfid);

        gf_log_callingfn (wb_inode->this->name, GF_LOG_DEBUG,
                          "(unique=%"PRIu64", fop=%s, gfid=%s, "
                          "gen=%"PRIu64"): request fulfilled. "
                          "removing the request from liability queue? = %s",
                          req->unique, gf_fop_list[req->fop], gfid, req->gen,
                          req->ordering.lied ? "yes" : "no");

        if (req->ordering.lied) {
                /* 1. If yes, request is in liability queue and hence can be
                      safely removed from list.
                   2. If no, request is in temptation queue and hence should be
                      left in the queue so that wb_pick_unwinds picks it up
                */
                list_del_init (&req->lie);
        } else {
		/* TODO: fail the req->frame with error if
		   necessary
		*/
	}

	__wb_request_unref (req);
}


/* get a flush/fsync waiting on req */
wb_request_t *
__wb_request_waiting_on (wb_request_t *req)
{
        wb_inode_t   *wb_inode = NULL;
        wb_request_t *trav     = NULL;

        wb_inode = req->wb_inode;

        list_for_each_entry (trav, &wb_inode->todo, todo) {
                if ((trav->fd == req->fd)
                    && ((trav->stub->fop == GF_FOP_FLUSH)
                        || (trav->stub->fop == GF_FOP_FSYNC))
                    && (trav->gen >= req->gen))
                        return trav;
        }

        return NULL;
}


void
__wb_add_request_for_retry (wb_request_t *req)
{
        wb_inode_t *wb_inode = NULL;

        if (!req)
                goto out;

        wb_inode = req->wb_inode;

        /* response was unwound and no waiter waiting on this request, retry
           till a flush or fsync (subject to conf->resync_after_fsync).
        */
	wb_inode->transit -= req->total_size;

        req->total_size = 0;

        list_del_init (&req->winds);
        list_del_init (&req->todo);
        list_del_init (&req->wip);

        /* sanitize ordering flags to retry */
        req->ordering.go = 0;

        /* Add back to todo list to retry */
        list_add (&req->todo, &wb_inode->todo);

out:
        return;
}

void
__wb_add_head_for_retry (wb_request_t *head)
{
	wb_request_t *req      = NULL, *tmp = NULL;

        if (!head)
                goto out;

        list_for_each_entry_safe_reverse (req, tmp, &head->winds,
                                          winds) {
                __wb_add_request_for_retry (req);
        }

        __wb_add_request_for_retry (head);

out:
        return;
}


void
wb_add_head_for_retry (wb_request_t *head)
{
        if (!head)
                goto out;

        LOCK (&head->wb_inode->lock);
        {
                __wb_add_head_for_retry (head);
        }
        UNLOCK (&head->wb_inode->lock);

out:
        return;
}


void
__wb_fulfill_request_err (wb_request_t *req, int32_t op_errno)
{
	wb_inode_t   *wb_inode = NULL;
        wb_request_t *waiter   = NULL;
        wb_conf_t    *conf     = NULL;

        wb_inode = req->wb_inode;

        conf = wb_inode->this->private;

        req->op_ret = -1;
        req->op_errno = op_errno;

        if (req->ordering.lied)
                waiter = __wb_request_waiting_on (req);

        if (!req->ordering.lied || waiter) {
                if (!req->ordering.lied) {
                        /* response to app is still pending, send failure in
                         * response.
                         */
                } else {
                        /* response was sent, store the error in a
                         * waiter (either an fsync or flush).
                         */
                        waiter->op_ret = -1;
                        waiter->op_errno = op_errno;
                }

                if (!req->ordering.lied
                    || (waiter->stub->fop == GF_FOP_FLUSH)
                    || ((waiter->stub->fop == GF_FOP_FSYNC)
                        && !conf->resync_after_fsync)) {
                        /* No retry needed, forget the request */
                        __wb_fulfill_request (req);
                        return;
                }
        }

        __wb_add_request_for_retry (req);

        return;
}


void
wb_head_done (wb_request_t *head)
{
	wb_request_t *req = NULL;
	wb_request_t *tmp     = NULL;
	wb_inode_t   *wb_inode = NULL;

	wb_inode = head->wb_inode;

	LOCK (&wb_inode->lock);
	{
		list_for_each_entry_safe (req, tmp, &head->winds, winds) {
			__wb_fulfill_request (req);
		}

		__wb_fulfill_request (head);
	}
	UNLOCK (&wb_inode->lock);
}


void
__wb_fulfill_err (wb_request_t *head, int op_errno)
{
	wb_request_t *req      = NULL, *tmp = NULL;

        if (!head)
                goto out;

        head->wb_inode->dontsync++;

        list_for_each_entry_safe_reverse (req, tmp, &head->winds,
                                          winds) {
                __wb_fulfill_request_err (req, op_errno);
        }

        __wb_fulfill_request_err (head, op_errno);

out:
        return;
}


void
wb_fulfill_err (wb_request_t *head, int op_errno)
{
	wb_inode_t   *wb_inode = NULL;

	wb_inode = head->wb_inode;

	LOCK (&wb_inode->lock);
	{
                __wb_fulfill_err (head, op_errno);

	}
	UNLOCK (&wb_inode->lock);
}


void
__wb_modify_write_request (wb_request_t *req, int synced_size)
{
        struct iovec *vector = NULL;
        int           count  = 0;

        if (!req || synced_size == 0)
                goto out;

        req->write_size -= synced_size;
        req->stub->args.offset += synced_size;

        vector = req->stub->args.vector;
        count = req->stub->args.count;

        req->stub->args.count = iov_subset (vector, count, synced_size,
                                            iov_length (vector, count), vector);

out:
        return;
}

int
__wb_fulfill_short_write (wb_request_t *req, int size, gf_boolean_t *fulfilled)
{
        int accounted_size = 0;

        if (req == NULL)
                goto out;

        if (req->write_size <= size) {
                accounted_size = req->write_size;
                __wb_fulfill_request (req);
                *fulfilled = 1;
        } else {
                accounted_size = size;
                __wb_modify_write_request (req, size);
        }

out:
        return accounted_size;
}

void
wb_fulfill_short_write (wb_request_t *head, int size)
{
        wb_inode_t       *wb_inode       = NULL;
        wb_request_t     *req            = NULL, *next = NULL;
        int               accounted_size = 0;
        gf_boolean_t      fulfilled      = _gf_false;

        if (!head)
                goto out;

        wb_inode = head->wb_inode;

        req = head;

        LOCK (&wb_inode->lock);
        {
                /* hold a reference to head so that __wb_fulfill_short_write
                 * won't free it. We need head for a cleaner list traversal as
                 * list_for_each_entry_safe doesn't iterate over "head" member.
                 * So, if we pass "next->winds" as head to list_for_each_entry,
                 * "next" is skipped. For a simpler logic we need to traverse
                 * the list in the order. So, we start traversal from
                 * "head->winds" and hence we want head to be alive.
                 */
                __wb_request_ref (head);

                next = list_entry (head->winds.next, wb_request_t, winds);

                accounted_size = __wb_fulfill_short_write (head, size,
                                                           &fulfilled);

                size -= accounted_size;

                if (size == 0) {
                        if (fulfilled)
                                req = next;

                        goto done;
                }

                list_for_each_entry_safe (req, next, &head->winds, winds) {
                        accounted_size = __wb_fulfill_short_write (req, size,
                                                                   &fulfilled);
                        size -= accounted_size;

                        if (size == 0) {
                                if (fulfilled)
                                        req = next;
                                break;
                        }

                }
        }
done:
        UNLOCK (&wb_inode->lock);

        __wb_request_unref (head);

        wb_add_head_for_retry (req);
out:
        return;
}

int
wb_fulfill_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
		struct iatt *postbuf, dict_t *xdata)
{
        wb_inode_t   *wb_inode   = NULL;
        wb_request_t *head       = NULL;

	head = frame->local;
	frame->local = NULL;

        wb_inode = head->wb_inode;

	if (op_ret == -1) {
		wb_fulfill_err (head, op_errno);
	} else if (op_ret < head->total_size) {
		wb_fulfill_short_write (head, op_ret);
	} else {
                wb_head_done (head);
        }

        wb_process_queue (wb_inode);

        STACK_DESTROY (frame->root);

        return 0;
}


#define WB_IOV_LOAD(vec, cnt, req, head) do {				\
		memcpy (&vec[cnt], req->stub->args.vector,		\
			(req->stub->args.count * sizeof(vec[0])));	\
		cnt += req->stub->args.count;				\
		head->total_size += req->write_size;			\
	} while (0)


int
wb_fulfill_head (wb_inode_t *wb_inode, wb_request_t *head)
{
	struct iovec  vector[MAX_VECTOR_COUNT];
	int           count    = 0;
	wb_request_t *req      = NULL;
	call_frame_t *frame    = NULL;

        /* make sure head->total_size is updated before we run into any
         * errors
         */

	WB_IOV_LOAD (vector, count, head, head);

	list_for_each_entry (req, &head->winds, winds) {
		WB_IOV_LOAD (vector, count, req, head);

		if (iobref_merge (head->stub->args.iobref,
				  req->stub->args.iobref))
			goto err;
	}

	frame = create_frame (wb_inode->this, wb_inode->this->ctx->pool);
	if (!frame)
		goto err;

	frame->root->lk_owner = head->lk_owner;
	frame->local = head;

	LOCK (&wb_inode->lock);
	{
		wb_inode->transit += head->total_size;
	}
	UNLOCK (&wb_inode->lock);

	STACK_WIND (frame, wb_fulfill_cbk, FIRST_CHILD (frame->this),
		    FIRST_CHILD (frame->this)->fops->writev,
		    head->fd, vector, count,
		    head->stub->args.offset,
		    head->stub->args.flags,
		    head->stub->args.iobref, NULL);

	return 0;
err:
        /* frame creation failure */
        wb_fulfill_err (head, ENOMEM);

	return ENOMEM;
}


#define NEXT_HEAD(head, req) do {                                       \
		if (head)                                               \
			ret |= wb_fulfill_head (wb_inode, head);        \
		head = req;                                             \
		expected_offset = req->stub->args.offset +              \
			req->write_size;                                \
		curr_aggregate = 0;                                     \
		vector_count = 0;                                       \
	} while (0)


int
wb_fulfill (wb_inode_t *wb_inode, list_head_t *liabilities)
{
	wb_request_t  *req     = NULL;
	wb_request_t  *head    = NULL;
	wb_request_t  *tmp     = NULL;
	wb_conf_t     *conf    = NULL;
	off_t          expected_offset = 0;
	size_t         curr_aggregate = 0;
	size_t         vector_count = 0;
        int            ret          = 0;

	conf = wb_inode->this->private;

	list_for_each_entry_safe (req, tmp, liabilities, winds) {
		list_del_init (&req->winds);

		if (!head) {
			NEXT_HEAD (head, req);
			continue;
		}

		if (req->fd != head->fd) {
			NEXT_HEAD (head, req);
			continue;
		}

		if (!is_same_lkowner (&req->lk_owner, &head->lk_owner)) {
			NEXT_HEAD (head, req);
			continue;
		}

		if (expected_offset != req->stub->args.offset) {
			NEXT_HEAD (head, req);
			continue;
		}

		if ((curr_aggregate + req->write_size) > conf->aggregate_size) {
			NEXT_HEAD (head, req);
			continue;
		}

		if (vector_count + req->stub->args.count >
		    MAX_VECTOR_COUNT) {
			NEXT_HEAD (head, req);
			continue;
		}

		list_add_tail (&req->winds, &head->winds);
		curr_aggregate += req->write_size;
		vector_count += req->stub->args.count;
	}

	if (head)
		ret |= wb_fulfill_head (wb_inode, head);

	return ret;
}


void
wb_do_unwinds (wb_inode_t *wb_inode, list_head_t *lies)
{
	wb_request_t *req = NULL;
	wb_request_t *tmp = NULL;
	call_frame_t *frame = NULL;
	struct iatt   buf = {0, };

        list_for_each_entry_safe (req, tmp, lies, unwinds) {
                frame = req->stub->frame;

                STACK_UNWIND_STRICT (writev, frame, req->op_ret, req->op_errno,
				     &buf, &buf, NULL); /* :O */
		req->stub->frame = NULL;

		list_del_init (&req->unwinds);
                wb_request_unref (req);
        }

        return;
}


void
__wb_pick_unwinds (wb_inode_t *wb_inode, list_head_t *lies)
{
        wb_request_t *req      = NULL;
        wb_request_t *tmp      = NULL;
        char          gfid[64] = {0,};

	list_for_each_entry_safe (req, tmp, &wb_inode->temptation, lie) {
		if (!req->ordering.fulfilled &&
		    wb_inode->window_current > wb_inode->window_conf)
			continue;

		list_del_init (&req->lie);
		list_move_tail (&req->unwinds, lies);

		wb_inode->window_current += req->orig_size;

		if (!req->ordering.fulfilled) {
			/* burden increased */
			list_add_tail (&req->lie, &wb_inode->liability);

			req->ordering.lied = 1;

			wb_inode->gen++;

                        uuid_utoa_r (req->gfid, gfid);
                        gf_msg_debug (wb_inode->this->name, 0,
                                      "(unique=%"PRIu64", fop=%s, gfid=%s, "
                                      "gen=%"PRIu64"): added req to liability "
                                      "queue. inode-generation-number=%"PRIu64,
                                      req->stub->frame->root->unique,
                                      gf_fop_list[req->fop], gfid, req->gen,
                                      wb_inode->gen);
		}
	}

        return;
}


int
__wb_collapse_small_writes (wb_request_t *holder, wb_request_t *req)
{
        char          *ptr    = NULL;
        struct iobuf  *iobuf  = NULL;
        struct iobref *iobref = NULL;
        int            ret    = -1;
        ssize_t        required_size = 0;
        size_t         holder_len = 0;
        size_t         req_len = 0;

        if (!holder->iobref) {
                holder_len = iov_length (holder->stub->args.vector,
                                         holder->stub->args.count);
                req_len = iov_length (req->stub->args.vector,
                                      req->stub->args.count);

                required_size = max ((THIS->ctx->page_size),
                                     (holder_len + req_len));
                iobuf = iobuf_get2 (req->wb_inode->this->ctx->iobuf_pool,
                                    required_size);
                if (iobuf == NULL) {
                        goto out;
                }

                iobref = iobref_new ();
                if (iobref == NULL) {
                        iobuf_unref (iobuf);
                        goto out;
                }

                ret = iobref_add (iobref, iobuf);
                if (ret != 0) {
                        gf_msg (req->wb_inode->this->name, GF_LOG_WARNING,
                                -ret, WRITE_BEHIND_MSG_INVALID_ARGUMENT,
                                "cannot add iobuf (%p) into iobref (%p)",
                                iobuf, iobref);
                        iobuf_unref (iobuf);
                        iobref_unref (iobref);
                        goto out;
                }

                iov_unload (iobuf->ptr, holder->stub->args.vector,
                            holder->stub->args.count);
                holder->stub->args.vector[0].iov_base = iobuf->ptr;
		holder->stub->args.count = 1;

                iobref_unref (holder->stub->args.iobref);
                holder->stub->args.iobref = iobref;

                iobuf_unref (iobuf);

                holder->iobref = iobref_ref (iobref);
        }

        ptr = holder->stub->args.vector[0].iov_base + holder->write_size;

        iov_unload (ptr, req->stub->args.vector,
                    req->stub->args.count);

        holder->stub->args.vector[0].iov_len += req->write_size;
        holder->write_size += req->write_size;
        holder->ordering.size += req->write_size;

        ret = 0;
out:
        return ret;
}


void
__wb_preprocess_winds (wb_inode_t *wb_inode)
{
        off_t         offset_expected = 0;
        ssize_t       space_left      = 0;
	wb_request_t *req             = NULL;
	wb_request_t *tmp             = NULL;
	wb_request_t *holder          = NULL;
	wb_conf_t    *conf            = NULL;
        int           ret             = 0;
	ssize_t       page_size       = 0;
        char          gfid[64]        = {0, };

	/* With asynchronous IO from a VM guest (as a file), there
	   can be two sequential writes happening in two regions
	   of the file. But individual (broken down) IO requests
	   can arrive interleaved.

	   TODO: cycle for each such sequence sifting
	   through the interleaved ops
	*/

	page_size = wb_inode->this->ctx->page_size;
	conf = wb_inode->this->private;

        list_for_each_entry_safe (req, tmp, &wb_inode->todo, todo) {
                if (wb_inode->dontsync && req->ordering.lied) {
                        /* sync has failed. Don't pick lies _again_ for winding
                         * as winding these lies again will trigger an infinite
                         * recursion of wb_process_queue being called from a
                         * failed fulfill. However, pick non-lied requests for
                         * winding so that application wont block indefinitely
                         * waiting for write result.
                         */

                        uuid_utoa_r (req->gfid, gfid);
                        gf_msg_debug (wb_inode->this->name, 0,
                                      "(unique=%"PRIu64", fop=%s, gfid=%s, "
                                      "gen=%"PRIu64"): not setting ordering.go"
                                      "as dontsync is set", req->unique,
                                      gf_fop_list[req->fop], gfid, req->gen);

                        continue;
                }

		if (!req->ordering.tempted) {
			if (holder) {
				if (wb_requests_conflict (holder, req))
					/* do not hold on write if a
					   dependent write is in queue */
					holder->ordering.go = 1;
			}
			/* collapse only non-sync writes */
			continue;
		} else if (!holder) {
			/* holder is always a non-sync write */
			holder = req;
			continue;
		}

		offset_expected = holder->stub->args.offset
			+ holder->write_size;

		if (req->stub->args.offset != offset_expected) {
			holder->ordering.go = 1;
			holder = req;
			continue;
		}

		if (!is_same_lkowner (&req->lk_owner, &holder->lk_owner)) {
			holder->ordering.go = 1;
			holder = req;
			continue;
		}

                if (req->fd != holder->fd) {
                        holder->ordering.go = 1;
                        holder = req;
                        continue;
                }

		space_left = page_size - holder->write_size;

		if (space_left < req->write_size) {
			holder->ordering.go = 1;
			holder = req;
			continue;
		}

		ret = __wb_collapse_small_writes (holder, req);
		if (ret)
			continue;

		/* collapsed request is as good as wound
		   (from its p.o.v)
		*/
		list_del_init (&req->todo);
		__wb_fulfill_request (req);

               /* Only the last @holder in queue which

                  - does not have any non-buffered-writes following it
                  - has not yet filled its capacity

                  does not get its 'go' set, in anticipation of the arrival
                  of consecutive smaller writes.
               */
        }

	/* but if trickling writes are enabled, then do not hold back
	   writes if there are no outstanding requests
	*/

	if (conf->trickling_writes && !wb_inode->transit && holder)
		holder->ordering.go = 1;

        if (wb_inode->dontsync > 0)
                wb_inode->dontsync--;

        return;
}

int
__wb_handle_failed_conflict (wb_request_t *req, wb_request_t *conflict,
                             list_head_t *tasks)
{
        wb_conf_t *conf     = NULL;
        char       gfid[64] = {0, };

        conf = req->wb_inode->this->private;

        uuid_utoa_r (req->gfid, gfid);

        if ((req->stub->fop != GF_FOP_FLUSH)
            && ((req->stub->fop != GF_FOP_FSYNC) || conf->resync_after_fsync)) {
                if (!req->ordering.lied && list_empty (&conflict->wip)) {
                        /* If request itself is in liability queue,
                         * 1. We cannot unwind as the response has already been
                         *    sent.
                         * 2. We cannot wind till conflict clears up.
                         * 3. So, skip the request for now.
                         * 4. Otherwise, resume (unwind) it with error.
                         */
                        req->op_ret = -1;
                        req->op_errno = conflict->op_errno;

                        list_del_init (&req->todo);
                        list_add_tail (&req->winds, tasks);

                        gf_msg_debug (req->wb_inode->this->name, 0,
                                      "(unique=%"PRIu64", fop=%s, gfid=%s, "
                                      "gen=%"PRIu64"): A conflicting write "
                                      "request in liability queue has failed "
                                      "to sync (error = \"%s\"), "
                                      "unwinding this request as a failure",
                                      req->unique, gf_fop_list[req->fop], gfid,
                                      req->gen, strerror (req->op_errno));

                        if (req->ordering.tempted) {
                                /* make sure that it won't be unwound in
                                 * wb_do_unwinds too. Otherwise there'll be
                                 * a double wind.
                                 */
                                list_del_init (&req->lie);

                                gf_msg_debug (req->wb_inode->this->name, 0,
                                              "(unique=%"PRIu64", fop=%s, "
                                              "gfid=%s, gen=%"PRIu64"): "
                                              "removed from liability queue",
                                              req->unique,
                                              gf_fop_list[req->fop], gfid,
                                              req->gen);

                                __wb_fulfill_request (req);
                        }
                }
        } else {
                gf_msg_debug (req->wb_inode->this->name, 0,
                              "(unique=%"PRIu64", fop=%s, gfid=%s, "
                              "gen=%"PRIu64"): A conflicting write request "
                              "in liability queue has failed to sync "
                              "(error = \"%s\"). This is an "
                              "FSYNC/FLUSH and we need to maintain ordering "
                              "guarantees with other writes in TODO queue. "
                              "Hence doing nothing now", req->unique,
                              gf_fop_list[req->fop], gfid, req->gen,
                              strerror (conflict->op_errno));

                /* flush and fsync (without conf->resync_after_fsync) act as
                   barriers. We cannot unwind them out of
                   order, when there are earlier generation writes just because
                   there is a conflicting liability with an error. So, wait for
                   our turn till there are no conflicting liabilities.

                   This situation can arise when there liabilities spread across
                   multiple generations. For eg., consider two writes with
                   following characterstics:

                   1. they belong to different generations gen1, gen2 and
                      (gen1 > gen2).
                   2. they overlap.
                   3. both are liabilities.
                   4. gen1 write was attempted to sync, but the attempt failed.
                   5. there was no attempt to sync gen2 write yet.
                   6. A flush (as part of close) is issued and gets a gen no
                      gen3.

                   In the above scenario, if flush is unwound without waiting
                   for gen1 and gen2 writes either to be successfully synced or
                   purged, we end up with these two writes in wb_inode->todo
                   list forever as there will be no attempt to process the queue
                   as flush is the last operation.
                */
        }

        return 0;
}


int
__wb_pick_winds (wb_inode_t *wb_inode, list_head_t *tasks,
		 list_head_t *liabilities)
{
	wb_request_t *req                 = NULL;
	wb_request_t *tmp                 = NULL;
        wb_request_t *conflict            = NULL;
        char          req_gfid[64]        = {0, }, conflict_gfid[64] = {0, };

	list_for_each_entry_safe (req, tmp, &wb_inode->todo, todo) {
                uuid_utoa_r (req->gfid, req_gfid);

                conflict = wb_liability_has_conflict (wb_inode, req);
                if (conflict) {
                        uuid_utoa_r (conflict->gfid, conflict_gfid);

                        gf_msg_debug (wb_inode->this->name, 0,
                                      "Not winding request due to a "
                                      "conflicting write in liability queue. "
                                      "REQ: unique=%"PRIu64", fop=%s, "
                                      "gen=%"PRIu64", gfid=%s. "
                                      "CONFLICT: unique=%"PRIu64", fop=%s, "
                                      "gen=%"PRIu64", gfid=%s, "
                                      "conflicts-sync-failed?=%s, "
                                      "conflicts-error=%s",
                                      req->unique, gf_fop_list[req->fop],
                                      req->gen, req_gfid,
                                      conflict->unique,
                                      gf_fop_list[conflict->fop], conflict->gen,
                                      conflict_gfid,
                                      (conflict->op_ret == 1) ? "yes" : "no",
                                      strerror (conflict->op_errno));

                        if (conflict->op_ret == -1) {
                                /* There is a conflicting liability which failed
                                 * to sync in previous attempts, resume the req
                                 * and fail, unless its an fsync/flush.
                                 */

                                __wb_handle_failed_conflict (req, conflict,
                                                             tasks);
                        } else {
                                /* There is a conflicting liability which was
                                 * not attempted to sync even once. Wait till
                                 * atleast one attempt to sync is made.
                                 */
                        }

                        continue;
                }

		if (req->ordering.tempted && !req->ordering.go) {
			/* wait some more */
                        gf_msg_debug (wb_inode->this->name, 0,
                                      "(unique=%"PRIu64", fop=%s, gen=%"PRIu64
                                      ", gfid=%s): ordering.go is not set, "
                                      "hence not winding", req->unique,
                                      gf_fop_list[req->fop], req->gen,
                                      req_gfid);
			continue;
                }

		if (req->stub->fop == GF_FOP_WRITE) {
                        conflict = wb_wip_has_conflict (wb_inode, req);

			if (conflict) {
                                uuid_utoa_r (conflict->gfid, conflict_gfid);

                                gf_msg_debug (wb_inode->this->name, 0,
                                              "Not winding write request as "
                                              "a conflicting write is being "
                                              "synced to backend. "
                                              "REQ: unique=%"PRIu64" fop=%s,"
                                              " gen=%"PRIu64", gfid=%s. "
                                              "CONFLICT: unique=%"PRIu64" "
                                              "fop=%s, gen=%"PRIu64", "
                                              "gfid=%s",
                                              req->unique,
                                              gf_fop_list[req->fop],
                                              req->gen, req_gfid,
                                              conflict->unique,
                                              gf_fop_list[conflict->fop],
                                              conflict->gen, conflict_gfid);
				continue;
                        }

			list_add_tail (&req->wip, &wb_inode->wip);
                        req->wind_count++;

			if (!req->ordering.tempted)
				/* unrefed in wb_writev_cbk */
				req->stub->frame->local =
					__wb_request_ref (req);
		}

                gf_msg_debug (wb_inode->this->name, 0,
                              "(unique=%"PRIu64", fop=%s, gfid=%s, "
                              "gen=%"PRIu64"): picking the request for "
                              "winding", req->unique, gf_fop_list[req->fop],
                              req_gfid, req->gen);

		list_del_init (&req->todo);

		if (req->ordering.tempted) {
			list_add_tail (&req->winds, liabilities);
                } else {
			list_add_tail (&req->winds, tasks);
                }
	}

        return 0;
}


void
wb_do_winds (wb_inode_t *wb_inode, list_head_t *tasks)
{
	wb_request_t *req  = NULL;
        wb_request_t *tmp  = NULL;

	list_for_each_entry_safe (req, tmp, tasks, winds) {
                list_del_init (&req->winds);

                if (req->op_ret == -1) {
                        call_unwind_error_keep_stub (req->stub, req->op_ret,
                                                     req->op_errno);
                } else {
                        call_resume_keep_stub (req->stub);
                }

                wb_request_unref (req);
	}
}


void
wb_process_queue (wb_inode_t *wb_inode)
{
        list_head_t tasks        = {0, };
	list_head_t lies         = {0, };
	list_head_t liabilities  = {0, };
        int         wind_failure = 0;

        INIT_LIST_HEAD (&tasks);
        INIT_LIST_HEAD (&lies);
        INIT_LIST_HEAD (&liabilities);

        do {
                gf_log_callingfn (wb_inode->this->name, GF_LOG_DEBUG,
                                  "processing queues");

                LOCK (&wb_inode->lock);
                {
                        __wb_preprocess_winds (wb_inode);

                        __wb_pick_winds (wb_inode, &tasks, &liabilities);

                        __wb_pick_unwinds (wb_inode, &lies);

                }
                UNLOCK (&wb_inode->lock);

                wb_do_unwinds (wb_inode, &lies);

                wb_do_winds (wb_inode, &tasks);

                /* If there is an error in wb_fulfill before winding write
                 * requests, we would miss invocation of wb_process_queue
                 * from wb_fulfill_cbk. So, retry processing again.
                 */
                wind_failure = wb_fulfill (wb_inode, &liabilities);
        } while (wind_failure);

        return;
}


void
wb_set_inode_size(wb_inode_t *wb_inode, struct iatt *postbuf)
{
	GF_ASSERT (wb_inode);
	GF_ASSERT (postbuf);

	LOCK (&wb_inode->lock);
	{
		wb_inode->size = postbuf->ia_size;
	}
	UNLOCK (&wb_inode->lock);
}


int
wb_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	       int32_t op_ret, int32_t op_errno,
	       struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
	wb_request_t *req = NULL;
	wb_inode_t *wb_inode;

	req = frame->local;
	frame->local = NULL;
	wb_inode = req->wb_inode;

	wb_request_unref (req);

	/* requests could be pending while this was in progress */
	wb_process_queue(wb_inode);

	STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf,
			     xdata);
	return 0;
}


int
wb_writev_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
		  struct iovec *vector, int32_t count, off_t offset,
		  uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
	STACK_WIND (frame, wb_writev_cbk,
		    FIRST_CHILD (this), FIRST_CHILD (this)->fops->writev,
		    fd, vector, count, offset, flags, iobref, xdata);
	return 0;
}


int
wb_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t offset, uint32_t flags, struct iobref *iobref,
           dict_t *xdata)
{
        wb_inode_t   *wb_inode      = NULL;
	wb_conf_t    *conf          = NULL;
        gf_boolean_t  wb_disabled   = 0;
        call_stub_t  *stub          = NULL;
        int           ret           = -1;
        int32_t       op_errno      = EINVAL;
	int           o_direct      = O_DIRECT;

	conf = this->private;

        wb_inode = wb_inode_create (this, fd->inode);
	if (!wb_inode) {
		op_errno = ENOMEM;
		goto unwind;
	}

	if (!conf->strict_O_DIRECT)
		o_direct = 0;

	if (fd->flags & (O_SYNC|O_DSYNC|o_direct))
		wb_disabled = 1;

	if (flags & (O_SYNC|O_DSYNC|o_direct))
		wb_disabled = 1;

	if (wb_disabled)
		stub = fop_writev_stub (frame, wb_writev_helper, fd, vector,
					count, offset, flags, iobref, xdata);
	else
		stub = fop_writev_stub (frame, NULL, fd, vector, count, offset,
                                        flags, iobref, xdata);
        if (!stub) {
                op_errno = ENOMEM;
                goto unwind;
        }

	if (wb_disabled)
		ret = wb_enqueue (wb_inode, stub);
	else
		ret = wb_enqueue_tempted (wb_inode, stub);

	if (!ret) {
		op_errno = ENOMEM;
		goto unwind;
	}

        wb_process_queue (wb_inode);

        return 0;

unwind:
        STACK_UNWIND_STRICT (writev, frame, -1, op_errno, NULL, NULL, NULL);

        if (stub)
                call_stub_destroy (stub);

        return 0;
}


int
wb_readv_helper (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                 off_t offset, uint32_t flags, dict_t *xdata)
{
        STACK_WIND (frame, default_readv_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readv, fd, size, offset, flags,
                    xdata);
        return 0;
}


int
wb_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset, uint32_t flags, dict_t *xdata)
{
        wb_inode_t   *wb_inode     = NULL;
        call_stub_t  *stub         = NULL;

        wb_inode = wb_inode_ctx_get (this, fd->inode);
	if (!wb_inode)
		goto noqueue;

	stub = fop_readv_stub (frame, wb_readv_helper, fd, size,
			       offset, flags, xdata);
	if (!stub)
		goto unwind;

	if (!wb_enqueue (wb_inode, stub))
		goto unwind;

	wb_process_queue (wb_inode);

        return 0;

unwind:
        STACK_UNWIND_STRICT (readv, frame, -1, ENOMEM, NULL, 0, NULL, NULL,
                             NULL);
        return 0;

noqueue:
        STACK_WIND (frame, default_readv_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readv, fd, size, offset, flags,
                    xdata);
        return 0;
}


int
wb_flush_bg_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        STACK_DESTROY (frame->root);
        return 0;
}


int
wb_flush_helper (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        wb_conf_t    *conf        = NULL;
        wb_inode_t   *wb_inode    = NULL;
        call_frame_t *bg_frame    = NULL;
	int32_t       op_errno    = 0;
	int           op_ret      = 0;

        conf = this->private;

	wb_inode = wb_inode_ctx_get (this, fd->inode);
	if (!wb_inode) {
		op_ret = -1;
		op_errno = EINVAL;
		goto unwind;
	}


        if (conf->flush_behind)
		goto flushbehind;

	STACK_WIND (frame, default_flush_cbk, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->flush, fd, xdata);
	return 0;

flushbehind:
	bg_frame = copy_frame (frame);
	if (!bg_frame) {
		op_ret = -1;
		op_errno = ENOMEM;
		goto unwind;
	}

	STACK_WIND (bg_frame, wb_flush_bg_cbk, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->flush, fd, xdata);
	/* fall through */
unwind:
        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno, NULL);

        return 0;
}


int
wb_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        wb_inode_t   *wb_inode     = NULL;
        call_stub_t  *stub         = NULL;

        wb_inode = wb_inode_ctx_get (this, fd->inode);
	if (!wb_inode)
		goto noqueue;

	stub = fop_flush_stub (frame, wb_flush_helper, fd, xdata);
	if (!stub)
		goto unwind;

	if (!wb_enqueue (wb_inode, stub))
		goto unwind;

        wb_process_queue (wb_inode);

        return 0;

unwind:
        STACK_UNWIND_STRICT (flush, frame, -1, ENOMEM, NULL);

        return 0;

noqueue:
        STACK_WIND (frame, default_flush_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->flush, fd, xdata);
        return 0;
}



int
wb_fsync_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 int32_t datasync, dict_t *xdata)
{
        STACK_WIND (frame, default_fsync_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsync, fd, datasync, xdata);
        return 0;
}


int
wb_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
          dict_t *xdata)
{
        wb_inode_t   *wb_inode     = NULL;
        call_stub_t  *stub         = NULL;
	int32_t       op_errno     = EINVAL;

        wb_inode = wb_inode_ctx_get (this, fd->inode);
	if (!wb_inode)
		goto noqueue;

	stub = fop_fsync_stub (frame, wb_fsync_helper, fd, datasync, xdata);
	if (!stub)
		goto unwind;

	if (!wb_enqueue (wb_inode, stub))
		goto unwind;

	wb_process_queue (wb_inode);

        return 0;

unwind:
        STACK_UNWIND_STRICT (fsync, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;

noqueue:
        STACK_WIND (frame, default_fsync_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsync, fd, datasync, xdata);
        return 0;
}


int
wb_stat_helper (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        STACK_WIND (frame, default_stat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat, loc, xdata);
        return 0;
}


int
wb_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        wb_inode_t   *wb_inode     = NULL;
        call_stub_t  *stub         = NULL;


	wb_inode = wb_inode_ctx_get (this, loc->inode);
        if (!wb_inode)
		goto noqueue;

	stub = fop_stat_stub (frame, wb_stat_helper, loc, xdata);
	if (!stub)
		goto unwind;

	if (!wb_enqueue (wb_inode, stub))
		goto unwind;

	wb_process_queue (wb_inode);

        return 0;

unwind:
        STACK_UNWIND_STRICT (stat, frame, -1, ENOMEM, NULL, NULL);

        if (stub)
                call_stub_destroy (stub);
        return 0;

noqueue:
        STACK_WIND (frame, default_stat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat, loc, xdata);
	return 0;
}


int
wb_fstat_helper (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        STACK_WIND (frame, default_fstat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat, fd, xdata);
        return 0;
}


int
wb_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        wb_inode_t   *wb_inode     = NULL;
	call_stub_t  *stub         = NULL;


        wb_inode = wb_inode_ctx_get (this, fd->inode);
	if (!wb_inode)
		goto noqueue;

	stub = fop_fstat_stub (frame, wb_fstat_helper, fd, xdata);
	if (!stub)
		goto unwind;

	if (!wb_enqueue (wb_inode, stub))
		goto unwind;

	wb_process_queue (wb_inode);

        return 0;

unwind:
        STACK_UNWIND_STRICT (fstat, frame, -1, ENOMEM, NULL, NULL);

        if (stub)
                call_stub_destroy (stub);
        return 0;

noqueue:
        STACK_WIND (frame, default_fstat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat, fd, xdata);
        return 0;
}


int32_t
wb_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf, dict_t *xdata)
{
        GF_ASSERT (frame->local);

        if (op_ret == 0)
                wb_set_inode_size (frame->local, postbuf);

        frame->local = NULL;

        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);
        return 0;
}


int
wb_truncate_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    off_t offset, dict_t *xdata)
{
        STACK_WIND (frame, wb_truncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);
        return 0;
}


int
wb_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
             dict_t *xdata)
{
        wb_inode_t   *wb_inode     = NULL;
	call_stub_t  *stub         = NULL;

	wb_inode = wb_inode_create (this, loc->inode);
	if (!wb_inode)
		goto unwind;

	frame->local = wb_inode;

	stub = fop_truncate_stub (frame, wb_truncate_helper, loc,
				  offset, xdata);
	if (!stub)
		goto unwind;

	if (!wb_enqueue (wb_inode, stub))
		goto unwind;

	wb_process_queue (wb_inode);

        return 0;

unwind:
        STACK_UNWIND_STRICT (truncate, frame, -1, ENOMEM, NULL, NULL, NULL);

        if (stub)
                call_stub_destroy (stub);

        return 0;
}


int32_t
wb_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
        GF_ASSERT (frame->local);

        if (op_ret == 0)
                wb_set_inode_size (frame->local, postbuf);

        frame->local = NULL;

        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);
        return 0;
}


int
wb_ftruncate_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     off_t offset, dict_t *xdata)
{
        STACK_WIND (frame, wb_ftruncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);
        return 0;
}


int
wb_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              dict_t *xdata)
{
        wb_inode_t   *wb_inode     = NULL;
        call_stub_t  *stub         = NULL;
        int32_t       op_errno     = 0;

        wb_inode = wb_inode_create (this, fd->inode);
	if (!wb_inode) {
                op_errno = ENOMEM;
		goto unwind;
        }

	frame->local = wb_inode;

	stub = fop_ftruncate_stub (frame, wb_ftruncate_helper, fd,
				   offset, xdata);
	if (!stub) {
                op_errno = ENOMEM;
		goto unwind;
        }

	if (!wb_enqueue (wb_inode, stub)) {
                op_errno = ENOMEM;
		goto unwind;
        }

	wb_process_queue (wb_inode);

        return 0;

unwind:
	frame->local = NULL;

        STACK_UNWIND_STRICT (ftruncate, frame, -1, op_errno, NULL, NULL, NULL);

        if (stub)
                call_stub_destroy (stub);
        return 0;
}


int
wb_setattr_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                   struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        STACK_WIND (frame, default_setattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setattr, loc, stbuf, valid, xdata);
        return 0;
}


int
wb_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
            struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        wb_inode_t   *wb_inode     = NULL;
        call_stub_t  *stub         = NULL;

	wb_inode = wb_inode_ctx_get (this, loc->inode);
	if (!wb_inode)
		goto noqueue;

	stub = fop_setattr_stub (frame, wb_setattr_helper, loc, stbuf,
                                         valid, xdata);
	if (!stub)
		goto unwind;

	if (!wb_enqueue (wb_inode, stub))
		goto unwind;

	wb_process_queue (wb_inode);

        return 0;
unwind:
        STACK_UNWIND_STRICT (setattr, frame, -1, ENOMEM, NULL, NULL, NULL);

        if (stub)
                call_stub_destroy (stub);
	return 0;

noqueue:
        STACK_WIND (frame, default_setattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setattr, loc, stbuf, valid, xdata);
        return 0;
}


int
wb_fsetattr_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
		    struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        STACK_WIND (frame, default_fsetattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetattr, fd, stbuf, valid, xdata);
        return 0;
}


int
wb_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
	     struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        wb_inode_t   *wb_inode     = NULL;
        call_stub_t  *stub         = NULL;

	wb_inode = wb_inode_ctx_get (this, fd->inode);
	if (!wb_inode)
		goto noqueue;

	stub = fop_fsetattr_stub (frame, wb_fsetattr_helper, fd, stbuf,
				  valid, xdata);
	if (!stub)
		goto unwind;

	if (!wb_enqueue (wb_inode, stub))
		goto unwind;

	wb_process_queue (wb_inode);

        return 0;
unwind:
        STACK_UNWIND_STRICT (fsetattr, frame, -1, ENOMEM, NULL, NULL, NULL);

        if (stub)
                call_stub_destroy (stub);
	return 0;

noqueue:
        STACK_WIND (frame, default_fsetattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetattr, fd, stbuf, valid, xdata);
        return 0;
}


int32_t
wb_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        wb_inode_t   *wb_inode     = NULL;

        wb_inode = wb_inode_create (this, fd->inode);
	if (!wb_inode)
		goto unwind;

	if (((flags & O_RDWR) || (flags & O_WRONLY)) && (flags & O_TRUNC))
		wb_inode->size = 0;

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->create, loc, flags, mode,
                         umask, fd, xdata);
        return 0;

unwind:
	STACK_UNWIND_STRICT (create, frame, -1, ENOMEM, NULL, NULL, NULL, NULL,
                             NULL, NULL);
	return 0;
}


int32_t
wb_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, dict_t *xdata)
{
        wb_inode_t   *wb_inode     = NULL;

        wb_inode = wb_inode_create (this, fd->inode);
	if (!wb_inode)
		goto unwind;

	if (((flags & O_RDWR) || (flags & O_WRONLY)) && (flags & O_TRUNC))
		wb_inode->size = 0;

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->open, loc, flags, fd, xdata);
        return 0;

unwind:
	STACK_UNWIND_STRICT (open, frame, -1, ENOMEM, NULL, NULL);
	return 0;
}


int32_t
wb_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        if (op_ret == 0) {
                wb_inode_t *wb_inode = wb_inode_ctx_get (this, inode);
                if (wb_inode)
                        wb_set_inode_size (wb_inode, buf);
        }

        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf,
                             xdata, postparent);
        return 0;
}


int
wb_lookup_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                  dict_t *xdata)
{
        STACK_WIND (frame, wb_lookup_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, loc, xdata);
        return 0;
}


int32_t
wb_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
           dict_t *xdata)
{
        wb_inode_t   *wb_inode     = NULL;
        call_stub_t  *stub         = NULL;

        wb_inode = wb_inode_ctx_get (this, loc->inode);
	if (!wb_inode)
		goto noqueue;

	stub = fop_lookup_stub (frame, wb_lookup_helper, loc, xdata);
	if (!stub)
		goto unwind;

	if (!wb_enqueue (wb_inode, stub))
		goto unwind;

	wb_process_queue (wb_inode);

        return 0;

unwind:
        if (stub)
                call_stub_destroy (stub);

        STACK_UNWIND_STRICT (lookup, frame, -1, ENOMEM, NULL, NULL, NULL, NULL);
        return 0;

noqueue:
        STACK_WIND (frame, wb_lookup_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, loc, xdata);
        return 0;
}


int32_t
wb_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                 dict_t *xdata)
{
        wb_inode_t  *wb_inode = NULL;
        gf_dirent_t *entry    = NULL;
        inode_t     *inode    = NULL;

        if (op_ret <= 0)
                goto unwind;

        list_for_each_entry (entry, &entries->list, list) {
                if (!entry->inode || !IA_ISREG (entry->d_stat.ia_type))
                        continue;

                wb_inode = wb_inode_ctx_get (this, entry->inode);
                if (!wb_inode)
                        continue;

                LOCK (&wb_inode->lock);
                {
                        if (!list_empty (&wb_inode->liability)) {
                                /* We cannot guarantee integrity of
                                   entry->d_stat as there are cached writes.
                                   The stat is most likely stale as it doesn't
                                   account the cached writes. However, checking
                                   for non-empty liability list here is not a
                                   fool-proof solution as there can be races
                                   like,
                                   1. readdirp is successful on posix
                                   2. sync of cached write is successful on
                                      posix
                                   3. write-behind received sync response and
                                      removed the request from liability queue
                                   4. readdirp response is processed at
                                      write-behind

                                   In the above scenario, stat for the file is
                                   sent back in readdirp response but it is
                                   stale.

                                   For lack of better solutions I am sticking
                                   with current solution.
                                */
                                inode = entry->inode;

                                entry->inode = NULL;
                                memset (&entry->d_stat, 0,
                                        sizeof (entry->d_stat));

                                inode_unref (inode);
                        }
                }
                UNLOCK (&wb_inode->lock);
        }

unwind:
	STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno,
			     entries, xdata);
	return 0;
}


int32_t
wb_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t off, dict_t *xdata)
{
        STACK_WIND (frame, wb_readdirp_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdirp,
                    fd, size, off, xdata);

        return 0;
}


int32_t
wb_link_helper (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                loc_t *newloc, dict_t *xdata)
{
	STACK_WIND_TAIL (frame,
			 FIRST_CHILD(this), FIRST_CHILD(this)->fops->link,
			 oldloc, newloc, xdata);
	return 0;
}


int32_t
wb_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
         dict_t *xdata)
{
        wb_inode_t   *wb_inode     = NULL;
        call_stub_t  *stub         = NULL;


	wb_inode = wb_inode_ctx_get (this, oldloc->inode);
        if (!wb_inode)
		goto noqueue;

	stub = fop_link_stub (frame, wb_link_helper, oldloc, newloc, xdata);
	if (!stub)
		goto unwind;

	if (!wb_enqueue (wb_inode, stub))
		goto unwind;

	wb_process_queue (wb_inode);

        return 0;

unwind:
        STACK_UNWIND_STRICT (link, frame, -1, ENOMEM, NULL, NULL, NULL, NULL,
                             NULL);

        if (stub)
                call_stub_destroy (stub);

        return 0;

noqueue:
	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->link,
			 oldloc, newloc, xdata);
	return 0;
}


int32_t
wb_fallocate_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     int32_t keep_size, off_t offset, size_t len, dict_t *xdata)
{
	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->fallocate, fd, keep_size,
                         offset, len, xdata);
	return 0;
}


int32_t
wb_fallocate (call_frame_t *frame, xlator_t *this, fd_t *fd,
              int32_t keep_size, off_t offset, size_t len, dict_t *xdata)
{
        wb_inode_t   *wb_inode     = NULL;
        call_stub_t  *stub         = NULL;


	wb_inode = wb_inode_ctx_get (this, fd->inode);
        if (!wb_inode)
		goto noqueue;

	stub = fop_fallocate_stub (frame, wb_fallocate_helper, fd, keep_size,
                                   offset, len, xdata);
	if (!stub)
		goto unwind;

	if (!wb_enqueue (wb_inode, stub))
		goto unwind;

	wb_process_queue (wb_inode);

        return 0;

unwind:
        STACK_UNWIND_STRICT (fallocate, frame, -1, ENOMEM, NULL, NULL, NULL);

        if (stub)
                call_stub_destroy (stub);

        return 0;

noqueue:
	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->fallocate, fd, keep_size,
                         offset, len, xdata);
	return 0;
}


int32_t
wb_discard_helper (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                   size_t len, dict_t *xdata)
{
	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->discard,
			 fd, offset, len, xdata);
	return 0;
}


int32_t
wb_discard (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            size_t len, dict_t *xdata)
{
        wb_inode_t   *wb_inode     = NULL;
        call_stub_t  *stub         = NULL;

	wb_inode = wb_inode_ctx_get (this, fd->inode);
        if (!wb_inode)
		goto noqueue;

	stub = fop_discard_stub (frame, wb_discard_helper, fd, offset, len,
                                 xdata);
	if (!stub)
		goto unwind;

	if (!wb_enqueue (wb_inode, stub))
		goto unwind;

	wb_process_queue (wb_inode);

        return 0;

unwind:
        STACK_UNWIND_STRICT (discard, frame, -1, ENOMEM, NULL, NULL, NULL);

        if (stub)
                call_stub_destroy (stub);
        return 0;

noqueue:
	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->discard,
			 fd, offset, len, xdata);

        return 0;
}


int32_t
wb_zerofill_helper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                    off_t offset, off_t len, dict_t *xdata)
{
	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->zerofill,
			 fd, offset, len, xdata);
	return 0;
}

int32_t
wb_zerofill (call_frame_t *frame, xlator_t *this, fd_t *fd,
             off_t offset, off_t len, dict_t *xdata)
{
        wb_inode_t   *wb_inode     = NULL;
        call_stub_t  *stub         = NULL;

	wb_inode = wb_inode_ctx_get (this, fd->inode);
        if (!wb_inode)
		goto noqueue;

	stub = fop_zerofill_stub (frame, wb_zerofill_helper, fd, offset, len,
                                  xdata);
	if (!stub)
		goto unwind;

	if (!wb_enqueue (wb_inode, stub))
		goto unwind;

	wb_process_queue (wb_inode);

        return 0;

unwind:
        STACK_UNWIND_STRICT (zerofill, frame, -1, ENOMEM, NULL, NULL, NULL);

        if (stub)
                call_stub_destroy (stub);

noqueue:
	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                         FIRST_CHILD(this)->fops->zerofill,
			 fd, offset, len, xdata);
        return 0;
}


int
wb_forget (xlator_t *this, inode_t *inode)
{
        uint64_t    tmp      = 0;
        wb_inode_t *wb_inode = NULL;

        inode_ctx_del (inode, this, &tmp);

        wb_inode = (wb_inode_t *)(long)tmp;

	if (!wb_inode)
		return 0;

        GF_ASSERT (list_empty (&wb_inode->todo));
        GF_ASSERT (list_empty (&wb_inode->liability));
        GF_ASSERT (list_empty (&wb_inode->temptation));

        GF_FREE (wb_inode);

        return 0;
}


int
wb_release (xlator_t *this, fd_t *fd)
{
        uint64_t    tmp      = 0;

        fd_ctx_del (fd, this, &tmp);

        return 0;
}


int
wb_priv_dump (xlator_t *this)
{
        wb_conf_t      *conf                            = NULL;
        char            key_prefix[GF_DUMP_MAX_BUF_LEN] = {0, };
        int             ret                             = -1;

        GF_VALIDATE_OR_GOTO ("write-behind", this, out);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        gf_proc_dump_build_key (key_prefix, "xlator.performance.write-behind",
                                "priv");

        gf_proc_dump_add_section (key_prefix);

        gf_proc_dump_write ("aggregate_size", "%d", conf->aggregate_size);
        gf_proc_dump_write ("window_size", "%d", conf->window_size);
        gf_proc_dump_write ("flush_behind", "%d", conf->flush_behind);
        gf_proc_dump_write ("trickling_writes", "%d", conf->trickling_writes);

        ret = 0;
out:
        return ret;
}


void
__wb_dump_requests (struct list_head *head, char *prefix)
{
        char          key[GF_DUMP_MAX_BUF_LEN]        = {0, };
        char          key_prefix[GF_DUMP_MAX_BUF_LEN] = {0, }, flag = 0;
        wb_request_t *req                             = NULL;

        list_for_each_entry (req, head, all) {
                gf_proc_dump_build_key (key_prefix, key,
                                        (char *)gf_fop_list[req->fop]);

                gf_proc_dump_add_section(key_prefix);

                gf_proc_dump_write ("unique", "%"PRIu64, req->unique);

                gf_proc_dump_write ("refcount", "%d", req->refcount);

		if (list_empty (&req->todo))
			gf_proc_dump_write ("wound", "yes");
		else
			gf_proc_dump_write ("wound", "no");

                gf_proc_dump_write ("generation-number", "%d", req->gen);

                gf_proc_dump_write ("req->op_ret", "%d", req->op_ret);
                gf_proc_dump_write ("req->op_errno", "%d", req->op_errno);
                gf_proc_dump_write ("sync-attempts", "%d", req->wind_count);

                if (req->fop == GF_FOP_WRITE) {
                        if (list_empty (&req->wip))
                                gf_proc_dump_write ("sync-in-progress", "no");
                        else
                                gf_proc_dump_write ("sync-in-progress", "yes");

                        gf_proc_dump_write ("size", "%"GF_PRI_SIZET,
                                            req->write_size);

                        if (req->stub)
                                gf_proc_dump_write ("offset", "%"PRId64,
                                                    req->stub->args.offset);

                        flag = req->ordering.lied;
                        gf_proc_dump_write ("lied", "%d", flag);

                        flag = req->ordering.append;
                        gf_proc_dump_write ("append", "%d", flag);

                        flag = req->ordering.fulfilled;
                        gf_proc_dump_write ("fulfilled", "%d", flag);

                        flag = req->ordering.go;
                        gf_proc_dump_write ("go", "%d", flag);

                }
        }
}


int
wb_inode_dump (xlator_t *this, inode_t *inode)
{
        wb_inode_t *wb_inode                       = NULL;
        int32_t     ret                            = -1;
        char       *path                           = NULL;
        char       key_prefix[GF_DUMP_MAX_BUF_LEN] = {0, };
        char       uuid_str[64] = {0,};

        if ((inode == NULL) || (this == NULL)) {
                ret = 0;
                goto out;
        }

        wb_inode = wb_inode_ctx_get (this, inode);
        if (wb_inode == NULL) {
                ret = 0;
                goto out;
        }

        uuid_utoa_r (inode->gfid, uuid_str);

        gf_proc_dump_build_key (key_prefix, "xlator.performance.write-behind",
                                "wb_inode");

        gf_proc_dump_add_section (key_prefix);

        __inode_path (inode, NULL, &path);
        if (path != NULL) {
                gf_proc_dump_write ("path", "%s", path);
                GF_FREE (path);
        }

        gf_proc_dump_write ("inode", "%p", inode);

        gf_proc_dump_write ("gfid", "%s", uuid_str);

        gf_proc_dump_write ("window_conf", "%"GF_PRI_SIZET,
                            wb_inode->window_conf);

        gf_proc_dump_write ("window_current", "%"GF_PRI_SIZET,
                            wb_inode->window_current);


        gf_proc_dump_write ("transit-size", "%"GF_PRI_SIZET,
                            wb_inode->transit);

        gf_proc_dump_write ("dontsync", "%d", wb_inode->dontsync);

        ret = TRY_LOCK (&wb_inode->lock);
        if (!ret)
        {
                if (!list_empty (&wb_inode->all)) {
                        __wb_dump_requests (&wb_inode->all, key_prefix);
                }
                UNLOCK (&wb_inode->lock);
        }

        if (ret && wb_inode)
                gf_proc_dump_write ("Unable to dump the inode information",
                                    "(Lock acquisition failed) %p (gfid: %s)",
                                    wb_inode, uuid_str);

        ret = 0;
out:
        return ret;
}


int
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this) {
                goto out;
        }

        ret = xlator_mem_acct_init (this, gf_wb_mt_end + 1);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        WRITE_BEHIND_MSG_NO_MEMORY,
                        "Memory accounting init"
                        "failed");
        }

out:
        return ret;
}


int
reconfigure (xlator_t *this, dict_t *options)
{
        wb_conf_t *conf = NULL;
        int        ret  = -1;

        conf = this->private;

        GF_OPTION_RECONF ("cache-size", conf->window_size, options, size_uint64,
                          out);

        GF_OPTION_RECONF ("flush-behind", conf->flush_behind, options, bool,
                          out);

        GF_OPTION_RECONF ("trickling-writes", conf->trickling_writes, options,
			  bool, out);

        GF_OPTION_RECONF ("strict-O_DIRECT", conf->strict_O_DIRECT, options,
			  bool, out);

        GF_OPTION_RECONF ("strict-write-ordering", conf->strict_write_ordering,
			  options, bool, out);
        GF_OPTION_RECONF ("resync-failed-syncs-after-fsync",
                          conf->resync_after_fsync, options, bool, out);

        ret = 0;
out:
        return ret;
}


int32_t
init (xlator_t *this)
{
        wb_conf_t *conf    = NULL;
        int32_t    ret     = -1;

        if ((this->children == NULL)
            || this->children->next) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        WRITE_BEHIND_MSG_INIT_FAILED,
                        "FATAL: write-behind (%s) not configured with exactly "
                        "one child", this->name);
                goto out;
        }

        if (this->parents == NULL) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        WRITE_BEHIND_MSG_VOL_MISCONFIGURED,
                        "dangling volume. check volfilex");
        }

        conf = GF_CALLOC (1, sizeof (*conf), gf_wb_mt_wb_conf_t);
        if (conf == NULL) {
                goto out;
        }

        /* configure 'options aggregate-size <size>' */
        conf->aggregate_size = WB_AGGREGATE_SIZE;

        /* configure 'option window-size <size>' */
        GF_OPTION_INIT ("cache-size", conf->window_size, size_uint64, out);

        if (!conf->window_size && conf->aggregate_size) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        WRITE_BEHIND_MSG_SIZE_NOT_SET,
                        "setting window-size to be equal to "
                        "aggregate-size(%"PRIu64")",
                        conf->aggregate_size);
                conf->window_size = conf->aggregate_size;
        }

        if (conf->window_size < conf->aggregate_size) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        WRITE_BEHIND_MSG_EXCEEDED_MAX_SIZE,
                        "aggregate-size(%"PRIu64") cannot be more than "
                        "window-size(%"PRIu64")", conf->aggregate_size,
                        conf->window_size);
                goto out;
        }

        /* configure 'option flush-behind <on/off>' */
        GF_OPTION_INIT ("flush-behind", conf->flush_behind, bool, out);

        GF_OPTION_INIT ("trickling-writes", conf->trickling_writes, bool, out);

        GF_OPTION_INIT ("strict-O_DIRECT", conf->strict_O_DIRECT, bool, out);

        GF_OPTION_INIT ("strict-write-ordering", conf->strict_write_ordering,
			bool, out);

        GF_OPTION_INIT ("resync-failed-syncs-after-fsync",
                        conf->resync_after_fsync, bool, out);

        this->private = conf;
        ret = 0;

out:
        if (ret) {
                GF_FREE (conf);
        }
        return ret;
}


void
fini (xlator_t *this)
{
        wb_conf_t *conf = NULL;

        GF_VALIDATE_OR_GOTO ("write-behind", this, out);

        conf = this->private;
        if (!conf) {
                goto out;
        }

        this->private = NULL;
        GF_FREE (conf);

out:
        return;
}


struct xlator_fops fops = {
        .writev      = wb_writev,
        .readv       = wb_readv,
        .flush       = wb_flush,
        .fsync       = wb_fsync,
        .stat        = wb_stat,
        .fstat       = wb_fstat,
        .truncate    = wb_truncate,
        .ftruncate   = wb_ftruncate,
        .setattr     = wb_setattr,
        .fsetattr    = wb_fsetattr,
        .lookup      = wb_lookup,
        .readdirp    = wb_readdirp,
        .link        = wb_link,
        .fallocate   = wb_fallocate,
        .discard     = wb_discard,
        .zerofill    = wb_zerofill,
};


struct xlator_cbks cbks = {
        .forget   = wb_forget,
        .release  = wb_release
};


struct xlator_dumpops dumpops = {
        .priv      =  wb_priv_dump,
        .inodectx  =  wb_inode_dump,
};


struct volume_options options[] = {
        { .key  = {"flush-behind"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "If this option is set ON, instructs write-behind "
                          "translator to perform flush in background, by "
                          "returning success (or any errors, if any of "
                          "previous  writes were failed) to application even "
                          "before flush FOP is sent to backend filesystem. "
        },
        { .key  = {"cache-size", "window-size"},
          .type = GF_OPTION_TYPE_SIZET,
          .min  = 512 * GF_UNIT_KB,
          .max  = 1 * GF_UNIT_GB,
          .default_value = "1MB",
          .description = "Size of the write-behind buffer for a single file "
                         "(inode)."
        },
        { .key = {"trickling-writes"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
        },
        { .key = {"strict-O_DIRECT"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "This option when set to off, ignores the "
          "O_DIRECT flag."
        },
        { .key = {"strict-write-ordering"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
	  .description = "Do not let later writes overtake earlier writes even "
	                  "if they do not overlap",
        },
        { .key = {"resync-failed-syncs-after-fsync"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "If sync of \"cached-writes issued before fsync\" "
                         "(to backend) fails, this option configures whether "
                         "to retry syncing them after fsync or forget them. "
                         "If set to on, cached-writes are retried "
                         "till a \"flush\" fop (or a successful sync) on sync "
                         "failures. "
                         "fsync itself is failed irrespective of the value of "
                         "this option. ",
        },
        { .key = {NULL} },
};
