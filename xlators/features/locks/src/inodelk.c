/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "compat.h"
#include "xlator.h"
#include "inode.h"
#include "logging.h"
#include "common-utils.h"
#include "list.h"

#include "locks.h"
#include "common.h"

inline void
__delete_inode_lock (pl_inode_lock_t *lock)
{
        list_del (&lock->list);
}

static inline void
__pl_inodelk_ref (pl_inode_lock_t *lock)
{
        lock->ref++;
}

void
__pl_inodelk_unref (pl_inode_lock_t *lock)
{
        lock->ref--;
        if (!lock->ref) {
                GF_FREE (lock->connection_id);
                GF_FREE (lock);
        }
}

/* Check if 2 inodelks are conflicting on type. Only 2 shared locks don't conflict */
static inline int
inodelk_type_conflict (pl_inode_lock_t *l1, pl_inode_lock_t *l2)
{
        if (l2->fl_type == F_WRLCK || l1->fl_type == F_WRLCK)
                return 1;

        return 0;
}

void
pl_print_inodelk (char *str, int size, int cmd, struct gf_flock *flock, const char *domain)
{
        char *cmd_str = NULL;
        char *type_str = NULL;

        switch (cmd) {
#if F_GETLK != F_GETLK64
        case F_GETLK64:
#endif
        case F_GETLK:
                cmd_str = "GETLK";
                break;

#if F_SETLK != F_SETLK64
        case F_SETLK64:
#endif
        case F_SETLK:
                cmd_str = "SETLK";
                break;

#if F_SETLKW != F_SETLKW64
        case F_SETLKW64:
#endif
        case F_SETLKW:
                cmd_str = "SETLKW";
                break;

        default:
                cmd_str = "UNKNOWN";
                break;
        }

        switch (flock->l_type) {
        case F_RDLCK:
                type_str = "READ";
                break;
        case F_WRLCK:
                type_str = "WRITE";
                break;
        case F_UNLCK:
                type_str = "UNLOCK";
                break;
        default:
                type_str = "UNKNOWN";
                break;
        }

        snprintf (str, size, "lock=INODELK, cmd=%s, type=%s, "
                  "domain: %s, start=%llu, len=%llu, pid=%llu",
                  cmd_str, type_str, domain,
                  (unsigned long long) flock->l_start,
                  (unsigned long long) flock->l_len,
                  (unsigned long long) flock->l_pid);
}

/* Determine if the two inodelks overlap reach other's lock regions */
static int
inodelk_overlap (pl_inode_lock_t *l1, pl_inode_lock_t *l2)
{
        return ((l1->fl_end >= l2->fl_start) &&
                (l2->fl_end >= l1->fl_start));
}

/* Returns true if the 2 inodelks have the same owner */
static inline int
same_inodelk_owner (pl_inode_lock_t *l1, pl_inode_lock_t *l2)
{
        return (is_same_lkowner (&l1->owner, &l2->owner) &&
                (l1->client == l2->client));
}

/* Returns true if the 2 inodelks conflict with each other */
static int
inodelk_conflict (pl_inode_lock_t *l1, pl_inode_lock_t *l2)
{
        return (inodelk_overlap (l1, l2) &&
                inodelk_type_conflict (l1, l2));
}

/* Determine if lock is grantable or not */
static pl_inode_lock_t *
__inodelk_grantable (pl_dom_list_t *dom, pl_inode_lock_t *lock)
{
        pl_inode_lock_t *l = NULL;
        pl_inode_lock_t *ret = NULL;
        if (list_empty (&dom->inodelk_list))
                goto out;
        list_for_each_entry (l, &dom->inodelk_list, list){
                if (inodelk_conflict (lock, l) &&
                    !same_inodelk_owner (lock, l)) {
                        ret = l;
                        goto out;
                }
        }
out:
        return ret;
}

static pl_inode_lock_t *
__blocked_lock_conflict (pl_dom_list_t *dom, pl_inode_lock_t *lock)
{
        pl_inode_lock_t *l   = NULL;
        pl_inode_lock_t *ret = NULL;

        if (list_empty (&dom->blocked_inodelks))
                return NULL;

        list_for_each_entry (l, &dom->blocked_inodelks, blocked_locks) {
                if (inodelk_conflict (lock, l)) {
                        ret = l;
                        goto out;
                }
        }

out:
        return ret;
}

static int
__owner_has_lock (pl_dom_list_t *dom, pl_inode_lock_t *newlock)
{
        pl_inode_lock_t *lock = NULL;

        list_for_each_entry (lock, &dom->inodelk_list, list) {
                if (same_inodelk_owner (lock, newlock))
                        return 1;
        }

        list_for_each_entry (lock, &dom->blocked_inodelks, blocked_locks) {
                if (same_inodelk_owner (lock, newlock))
                        return 1;
        }

        return 0;
}


/* Determines if lock can be granted and adds the lock. If the lock
 * is blocking, adds it to the blocked_inodelks list of the domain.
 */
static int
__lock_inodelk (xlator_t *this, pl_inode_t *pl_inode, pl_inode_lock_t *lock,
                int can_block,  pl_dom_list_t *dom)
{
        pl_inode_lock_t *conf = NULL;
        int ret = -EINVAL;

        conf = __inodelk_grantable (dom, lock);
        if (conf) {
                ret = -EAGAIN;
                if (can_block == 0)
                        goto out;

                gettimeofday (&lock->blkd_time, NULL);
                list_add_tail (&lock->blocked_locks, &dom->blocked_inodelks);

                gf_log (this->name, GF_LOG_TRACE,
                        "%s (pid=%d) lk-owner:%s %"PRId64" - %"PRId64" => Blocked",
                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                        lock->client_pid,
                        lkowner_utoa (&lock->owner),
                        lock->user_flock.l_start,
                        lock->user_flock.l_len);


                goto out;
        }

        if (__blocked_lock_conflict (dom, lock) && !(__owner_has_lock (dom, lock))) {
                ret = -EAGAIN;
                if (can_block == 0)
                        goto out;

                gettimeofday (&lock->blkd_time, NULL);
                list_add_tail (&lock->blocked_locks, &dom->blocked_inodelks);

                gf_log (this->name, GF_LOG_DEBUG,
                        "Lock is grantable, but blocking to prevent starvation");
                gf_log (this->name, GF_LOG_TRACE,
                        "%s (pid=%d) (lk-owner=%s) %"PRId64" - %"PRId64" => Blocked",
                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                        lock->client_pid,
                        lkowner_utoa (&lock->owner),
                        lock->user_flock.l_start,
                        lock->user_flock.l_len);


                goto out;
        }
        __pl_inodelk_ref (lock);
        gettimeofday (&lock->granted_time, NULL);
        list_add (&lock->list, &dom->inodelk_list);

        ret = 0;

out:
        return ret;
}

/* Return true if the two inodelks have exactly same lock boundaries */
static int
inodelks_equal (pl_inode_lock_t *l1, pl_inode_lock_t *l2)
{
        if ((l1->fl_start == l2->fl_start) &&
            (l1->fl_end == l2->fl_end))
                return 1;

        return 0;
}


static pl_inode_lock_t *
find_matching_inodelk (pl_inode_lock_t *lock, pl_dom_list_t *dom)
{
        pl_inode_lock_t *l = NULL;
        list_for_each_entry (l, &dom->inodelk_list, list) {
                if (inodelks_equal (l, lock) &&
                    same_inodelk_owner (l, lock))
                        return l;
        }
        return NULL;
}

/* Set F_UNLCK removes a lock which has the exact same lock boundaries
 * as the UNLCK lock specifies. If such a lock is not found, returns invalid
 */
static pl_inode_lock_t *
__inode_unlock_lock (xlator_t *this, pl_inode_lock_t *lock, pl_dom_list_t *dom)
{

        pl_inode_lock_t *conf = NULL;

        conf = find_matching_inodelk (lock, dom);
        if (!conf) {
                gf_log (this->name, GF_LOG_ERROR,
                        " Matching lock not found for unlock %llu-%llu, by %s "
                        "on %p", (unsigned long long)lock->fl_start,
                        (unsigned long long)lock->fl_end,
                        lkowner_utoa (&lock->owner), lock->client);
                goto out;
        }
        __delete_inode_lock (conf);
        gf_log (this->name, GF_LOG_DEBUG,
                " Matching lock found for unlock %llu-%llu, by %s on %p",
                (unsigned long long)lock->fl_start,
                (unsigned long long)lock->fl_end, lkowner_utoa (&lock->owner),
                lock->client);

out:
        return conf;
}


static void
__grant_blocked_inode_locks (xlator_t *this, pl_inode_t *pl_inode,
                             struct list_head *granted, pl_dom_list_t *dom)
{
        int              bl_ret = 0;
        pl_inode_lock_t *bl = NULL;
        pl_inode_lock_t *tmp = NULL;

        struct list_head blocked_list;

        INIT_LIST_HEAD (&blocked_list);
        list_splice_init (&dom->blocked_inodelks, &blocked_list);

        list_for_each_entry_safe (bl, tmp, &blocked_list, blocked_locks) {

                list_del_init (&bl->blocked_locks);

                bl_ret = __lock_inodelk (this, pl_inode, bl, 1, dom);

                if (bl_ret == 0) {
                        list_add (&bl->blocked_locks, granted);
                }
        }
        return;
}

/* Grant all inodelks blocked on a lock */
void
grant_blocked_inode_locks (xlator_t *this, pl_inode_t *pl_inode,
                           pl_dom_list_t *dom)
{
        struct list_head granted;
        pl_inode_lock_t *lock;
        pl_inode_lock_t *tmp;

        INIT_LIST_HEAD (&granted);

        pthread_mutex_lock (&pl_inode->mutex);
        {
                __grant_blocked_inode_locks (this, pl_inode, &granted, dom);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        list_for_each_entry_safe (lock, tmp, &granted, blocked_locks) {
                gf_log (this->name, GF_LOG_TRACE,
                        "%s (pid=%d) (lk-owner=%s) %"PRId64" - %"PRId64" => Granted",
                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                        lock->client_pid,
                        lkowner_utoa (&lock->owner),
                        lock->user_flock.l_start,
                        lock->user_flock.l_len);

                pl_trace_out (this, lock->frame, NULL, NULL, F_SETLKW,
                              &lock->user_flock, 0, 0, lock->volume);

                STACK_UNWIND_STRICT (inodelk, lock->frame, 0, 0, NULL);
		lock->frame = NULL;
        }

        pthread_mutex_lock (&pl_inode->mutex);
        {
                list_for_each_entry_safe (lock, tmp, &granted, blocked_locks) {
                        list_del_init (&lock->blocked_locks);
                        __pl_inodelk_unref (lock);
                }
        }
        pthread_mutex_unlock (&pl_inode->mutex);
}


static void
pl_inodelk_log_cleanup (pl_inode_lock_t *lock)
{
	pl_inode_t *pl_inode = NULL;
        char *path = NULL;
        char *file = NULL;

	pl_inode = lock->pl_inode;

	inode_path (pl_inode->refkeeper, NULL, &path);

	if (path)
		file = path;
	else
		file = uuid_utoa (pl_inode->refkeeper->gfid);

	gf_log (THIS->name, GF_LOG_WARNING,
		"releasing lock on %s held by "
		"{client=%p, pid=%"PRId64" lk-owner=%s}",
		file, lock->client, (uint64_t) lock->client_pid,
		lkowner_utoa (&lock->owner));
	GF_FREE (path);
}


/* Release all entrylks from this client */
int
pl_inodelk_client_cleanup (xlator_t *this, pl_ctx_t *ctx)
{
        pl_inode_lock_t *tmp = NULL;
        pl_inode_lock_t *l = NULL;
	pl_dom_list_t *dom = NULL;
        pl_inode_t *pl_inode = NULL;

        struct list_head released;

        INIT_LIST_HEAD (&released);

	pthread_mutex_lock (&ctx->lock);
        {
                list_for_each_entry_safe (l, tmp, &ctx->inodelk_lockers,
					  client_list) {
                        list_del_init (&l->client_list);
			list_add_tail (&l->client_list, &released);

			pl_inodelk_log_cleanup (l);

			pl_inode = l->pl_inode;

			pthread_mutex_lock (&pl_inode->mutex);
			{
				__delete_inode_lock (l);
                        }
			pthread_mutex_unlock (&pl_inode->mutex);
                }
	}
        pthread_mutex_unlock (&ctx->lock);

        list_for_each_entry_safe (l, tmp, &released, client_list) {
                list_del_init (&l->client_list);

		if (l->frame)
			STACK_UNWIND_STRICT (inodelk, l->frame, -1, EAGAIN,
					     NULL);

		pl_inode = l->pl_inode;

		dom = get_domain (pl_inode, l->volume);

		grant_blocked_inode_locks (this, pl_inode, dom);

		pthread_mutex_lock (&pl_inode->mutex);
		{
			__pl_inodelk_unref (l);
		}
		pthread_mutex_unlock (&pl_inode->mutex);
        }

        return 0;
}


static int
pl_inode_setlk (xlator_t *this, pl_ctx_t *ctx, pl_inode_t *pl_inode,
		pl_inode_lock_t *lock, int can_block, pl_dom_list_t *dom)
{
        int ret = -EINVAL;
        pl_inode_lock_t *retlock = NULL;
        gf_boolean_t    unref = _gf_true;

	lock->pl_inode = pl_inode;

	if (ctx)
		pthread_mutex_lock (&ctx->lock);
        pthread_mutex_lock (&pl_inode->mutex);
        {
                if (lock->fl_type != F_UNLCK) {
                        ret = __lock_inodelk (this, pl_inode, lock, can_block, dom);
                        if (ret == 0) {
				lock->frame = NULL;
                                gf_log (this->name, GF_LOG_TRACE,
                                        "%s (pid=%d) (lk-owner=%s) %"PRId64" - %"PRId64" => OK",
                                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                        lock->client_pid,
                                        lkowner_utoa (&lock->owner),
                                        lock->fl_start,
                                        lock->fl_end);
                        } else if (ret == -EAGAIN) {
                                gf_log (this->name, GF_LOG_TRACE,
                                        "%s (pid=%d) (lk-owner=%s) %"PRId64" - %"PRId64" => NOK",
                                        lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                        lock->client_pid,
                                        lkowner_utoa (&lock->owner),
                                        lock->user_flock.l_start,
                                        lock->user_flock.l_len);
                                if (can_block)
                                        unref = _gf_false;
                        }

			if (ctx && (!ret || can_block))
				list_add_tail (&lock->client_list,
					       &ctx->inodelk_lockers);
                } else {
                        retlock = __inode_unlock_lock (this, lock, dom);
                        if (!retlock) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "Bad Unlock issued on Inode lock");
                                ret = -EINVAL;
                                goto out;
                        }
			list_del_init (&retlock->client_list);
			__pl_inodelk_unref (retlock);

                        ret = 0;
                }
out:
		if (unref)
			__pl_inodelk_unref (lock);
        }
        pthread_mutex_unlock (&pl_inode->mutex);
	if (ctx)
		pthread_mutex_unlock (&ctx->lock);

        grant_blocked_inode_locks (this, pl_inode, dom);

        return ret;
}

/* Create a new inode_lock_t */
pl_inode_lock_t *
new_inode_lock (struct gf_flock *flock, client_t *client, pid_t client_pid,
                call_frame_t *frame, xlator_t *this, const char *volume,
                char *conn_id)

{
        pl_inode_lock_t *lock = NULL;

        lock = GF_CALLOC (1, sizeof (*lock),
                          gf_locks_mt_pl_inode_lock_t);
        if (!lock) {
                return NULL;
        }

        lock->fl_start = flock->l_start;
        lock->fl_type  = flock->l_type;

        if (flock->l_len == 0)
                lock->fl_end = LLONG_MAX;
        else
                lock->fl_end = flock->l_start + flock->l_len - 1;

        lock->client     = client;
        lock->client_pid = client_pid;
        lock->volume     = volume;
        lock->owner      = frame->root->lk_owner;
        lock->frame      = frame;
        lock->this       = this;

        if (conn_id) {
                lock->connection_id = gf_strdup (conn_id);
        }

        INIT_LIST_HEAD (&lock->list);
        INIT_LIST_HEAD (&lock->blocked_locks);
	INIT_LIST_HEAD (&lock->client_list);
        __pl_inodelk_ref (lock);

        return lock;
}

int32_t
_pl_convert_volume (const char *volume, char **res)
{
        char    *mdata_vol = NULL;
        int     ret = 0;

        mdata_vol = strrchr (volume, ':');
        //if the volume already ends with :metadata don't bother
        if (mdata_vol && (strcmp (mdata_vol, ":metadata") == 0))
                return 0;

        ret = gf_asprintf (res, "%s:metadata", volume);
        if (ret <= 0)
                return ENOMEM;
        return 0;
}

int32_t
_pl_convert_volume_for_special_range (struct gf_flock *flock,
                                      const char *volume, char **res)
{
        int32_t     ret = 0;

        if ((flock->l_start == LLONG_MAX -1) &&
            (flock->l_len == 0)) {
                ret = _pl_convert_volume (volume, res);
        }

        return ret;
}

/* Common inodelk code called from pl_inodelk and pl_finodelk */
int
pl_common_inodelk (call_frame_t *frame, xlator_t *this,
                   const char *volume, inode_t *inode, int32_t cmd,
                   struct gf_flock *flock, loc_t *loc, fd_t *fd, dict_t *xdata)
{
        int32_t           op_ret     = -1;
        int32_t           op_errno   = 0;
        int               ret        = -1;
        GF_UNUSED int     dict_ret   = -1;
        int               can_block  = 0;
        pl_inode_t *      pinode     = NULL;
        pl_inode_lock_t * reqlock    = NULL;
        pl_dom_list_t *   dom        = NULL;
        char             *res        = NULL;
        char             *res1       = NULL;
        char             *conn_id    = NULL;
        pl_ctx_t         *ctx        = NULL;

        if (xdata)
                dict_ret = dict_get_str (xdata, "connection-id", &conn_id);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (inode, unwind);
        VALIDATE_OR_GOTO (flock, unwind);

        if ((flock->l_start < 0) || (flock->l_len < 0)) {
                op_errno = EINVAL;
                goto unwind;
        }

        op_errno = _pl_convert_volume_for_special_range (flock, volume, &res);
        if (op_errno)
                goto unwind;
        if (res)
                volume = res;

        pl_trace_in (this, frame, fd, loc, cmd, flock, volume);

	if (frame->root->client) {
		ctx = pl_ctx_get (frame->root->client, this);
		if (!ctx) {
			op_errno = ENOMEM;
			gf_log (this->name, GF_LOG_INFO, "pl_ctx_get() failed");
			goto unwind;
		}
	}

        pinode = pl_inode_get (this, inode);
        if (!pinode) {
                op_errno = ENOMEM;
                goto unwind;
        }

        dom = get_domain (pinode, volume);
        if (!dom) {
                op_errno = ENOMEM;
                goto unwind;
        }

        reqlock = new_inode_lock (flock, frame->root->client, frame->root->pid,
                                  frame, this, volume, conn_id);

        if (!reqlock) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }


        switch (cmd) {
        case F_SETLKW:
                can_block = 1;

                /* fall through */

        case F_SETLK:
                memcpy (&reqlock->user_flock, flock, sizeof (struct gf_flock));
                ret = pl_inode_setlk (this, ctx, pinode, reqlock, can_block,
				      dom);

                if (ret < 0) {
                        if ((can_block) && (F_UNLCK != flock->l_type)) {
                                pl_trace_block (this, frame, fd, loc,
                                                cmd, flock, volume);
                                goto out;
                        }
                        gf_log (this->name, GF_LOG_TRACE, "returning EAGAIN");
                        op_errno = -ret;
                        goto unwind;
                }
                break;

        default:
                op_errno = ENOTSUP;
                gf_log (this->name, GF_LOG_DEBUG,
                        "Lock command F_GETLK not supported for [f]inodelk "
                        "(cmd=%d)",
                        cmd);
                goto unwind;
        }

        op_ret = 0;

unwind:
        if ((inode != NULL) && (flock !=NULL)) {
                pl_update_refkeeper (this, inode);
                pl_trace_out (this, frame, fd, loc, cmd, flock, op_ret, op_errno, volume);
        }

        STACK_UNWIND_STRICT (inodelk, frame, op_ret, op_errno, NULL);
out:
        GF_FREE (res);
        GF_FREE (res1);
        return 0;
}

int
pl_inodelk (call_frame_t *frame, xlator_t *this,
            const char *volume, loc_t *loc, int32_t cmd, struct gf_flock *flock,
            dict_t *xdata)
{
        pl_common_inodelk (frame, this, volume, loc->inode, cmd, flock,
                           loc, NULL, xdata);

        return 0;
}

int
pl_finodelk (call_frame_t *frame, xlator_t *this,
             const char *volume, fd_t *fd, int32_t cmd, struct gf_flock *flock,
             dict_t *xdata)
{
        pl_common_inodelk (frame, this, volume, fd->inode, cmd, flock,
                           NULL, fd, xdata);

        return 0;

}

static inline int32_t
__get_inodelk_dom_count (pl_dom_list_t *dom)
{
        pl_inode_lock_t     *lock   = NULL;
        int32_t             count   = 0;

        list_for_each_entry (lock, &dom->inodelk_list, list) {
                count++;
        }
        list_for_each_entry (lock, &dom->blocked_inodelks, blocked_locks) {
                count++;
        }
        return count;
}

/* Returns the no. of locks (blocked/granted) held on a given domain name
 * If @domname is NULL, returns the no. of locks in all the domains present.
 * If @domname is non-NULL and non-existent, returns 0 */
int32_t
__get_inodelk_count (xlator_t *this, pl_inode_t *pl_inode, char *domname)
{
        int32_t            count  = 0;
        pl_dom_list_t     *dom    = NULL;

        list_for_each_entry (dom, &pl_inode->dom_list, inode_list) {
                if (domname) {
                        if (strcmp (domname, dom->domain) == 0) {
                                count = __get_inodelk_dom_count (dom);
                                goto out;
                        }

                } else {
                    /* Counting locks from all domains */
                        count += __get_inodelk_dom_count (dom);

                }
        }

out:
        return count;
}

int32_t
get_inodelk_count (xlator_t *this, inode_t *inode, char *domname)
{
        pl_inode_t   *pl_inode = NULL;
        uint64_t      tmp_pl_inode = 0;
        int           ret      = 0;
        int32_t       count    = 0;

        ret = inode_ctx_get (inode, this, &tmp_pl_inode);
        if (ret != 0) {
                goto out;
        }

        pl_inode = (pl_inode_t *)(long) tmp_pl_inode;

        pthread_mutex_lock (&pl_inode->mutex);
        {
                count = __get_inodelk_count (this, pl_inode, domname);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

out:
        return count;
}
