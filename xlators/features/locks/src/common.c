/*
   Copyright (c) 2006-2012, 2015-2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>

#include "glusterfs.h"
#include "compat.h"
#include "xlator.h"
#include "inode.h"
#include "logging.h"
#include "common-utils.h"

#include "locks.h"
#include "common.h"


static int
__is_lock_grantable (pl_inode_t *pl_inode, posix_lock_t *lock);
static void
__insert_and_merge (pl_inode_t *pl_inode, posix_lock_t *lock);
static int
pl_send_prelock_unlock (xlator_t *this, pl_inode_t *pl_inode,
                        posix_lock_t *old_lock);

static pl_dom_list_t *
__allocate_domain (const char *volume)
{
        pl_dom_list_t *dom = NULL;

        dom = GF_CALLOC (1, sizeof (*dom),
                         gf_locks_mt_pl_dom_list_t);
        if (!dom)
                goto out;

        dom->domain = gf_strdup(volume);
        if (!dom->domain)
                goto out;

        gf_log ("posix-locks", GF_LOG_TRACE,
                "New domain allocated: %s", dom->domain);

        INIT_LIST_HEAD (&dom->inode_list);
        INIT_LIST_HEAD (&dom->entrylk_list);
        INIT_LIST_HEAD (&dom->blocked_entrylks);
        INIT_LIST_HEAD (&dom->inodelk_list);
        INIT_LIST_HEAD (&dom->blocked_inodelks);

out:
        if (dom && (NULL == dom->domain)) {
                GF_FREE (dom);
                dom = NULL;
        }

        return dom;
}

/* Returns domain for the lock. If domain is not present,
 * allocates a domain and returns it
 */
pl_dom_list_t *
get_domain (pl_inode_t *pl_inode, const char *volume)
{
        pl_dom_list_t *dom = NULL;

        GF_VALIDATE_OR_GOTO ("posix-locks", pl_inode, out);
        GF_VALIDATE_OR_GOTO ("posix-locks", volume, out);

        pthread_mutex_lock (&pl_inode->mutex);
        {
                list_for_each_entry (dom, &pl_inode->dom_list, inode_list) {
                        if (strcmp (dom->domain, volume) == 0)
                                goto unlock;
                }

                dom = __allocate_domain (volume);
                if (dom)
                        list_add (&dom->inode_list, &pl_inode->dom_list);
        }
unlock:
        pthread_mutex_unlock (&pl_inode->mutex);
        if (dom) {
                gf_log ("posix-locks", GF_LOG_TRACE, "Domain %s found", volume);
        } else {
                gf_log ("posix-locks", GF_LOG_TRACE, "Domain %s not found", volume);
        }
out:
        return dom;
}

unsigned long
fd_to_fdnum (fd_t *fd)
{
        return ((unsigned long) fd);
}

fd_t *
fd_from_fdnum (posix_lock_t *lock)
{
        return ((fd_t *) lock->fd_num);
}

int
__pl_inode_is_empty (pl_inode_t *pl_inode)
{
        return (list_empty (&pl_inode->ext_list));
}

void
pl_print_locker (char *str, int size, xlator_t *this, call_frame_t *frame)
{
        snprintf (str, size, "Pid=%llu, lk-owner=%s, Client=%p, Frame=%llu",
                  (unsigned long long) frame->root->pid,
                  lkowner_utoa (&frame->root->lk_owner),
                  frame->root->client,
                  (unsigned long long) frame->root->unique);
}


void
pl_print_lockee (char *str, int size, fd_t *fd, loc_t *loc)
{
        inode_t  *inode = NULL;
        char     *ipath = NULL;
        int       ret = 0;

        if (fd)
                inode = fd->inode;
        if (loc)
                inode = loc->inode;

        if (!inode) {
                snprintf (str, size, "<nul>");
                return;
        }

        if (loc && loc->path) {
                ipath = gf_strdup (loc->path);
        } else {
                ret = inode_path (inode, NULL, &ipath);
                if (ret <= 0)
                        ipath = NULL;
        }

        snprintf (str, size, "gfid=%s, fd=%p, path=%s",
                  uuid_utoa (inode->gfid), fd,
                  ipath ? ipath : "<nul>");

        GF_FREE (ipath);
}


void
pl_print_lock (char *str, int size, int cmd,
               struct gf_flock *flock, gf_lkowner_t *owner)
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

        snprintf (str, size, "lock=FCNTL, cmd=%s, type=%s, "
                  "start=%llu, len=%llu, pid=%llu, lk-owner=%s",
                  cmd_str, type_str, (unsigned long long) flock->l_start,
                  (unsigned long long) flock->l_len,
                  (unsigned long long) flock->l_pid,
                  lkowner_utoa (owner));
}


void
pl_trace_in (xlator_t *this, call_frame_t *frame, fd_t *fd, loc_t *loc,
             int cmd, struct gf_flock *flock, const char *domain)
{
        posix_locks_private_t  *priv = NULL;
        char                    pl_locker[256];
        char                    pl_lockee[256];
        char                    pl_lock[256];

        priv = this->private;

        if (!priv->trace)
                return;

        pl_print_locker (pl_locker, 256, this, frame);
        pl_print_lockee (pl_lockee, 256, fd, loc);
        if (domain)
                pl_print_inodelk (pl_lock, 256, cmd, flock, domain);
        else
                pl_print_lock (pl_lock, 256, cmd, flock, &frame->root->lk_owner);

        gf_log (this->name, GF_LOG_INFO,
                "[REQUEST] Locker = {%s} Lockee = {%s} Lock = {%s}",
                pl_locker, pl_lockee, pl_lock);
}


void
pl_print_verdict (char *str, int size, int op_ret, int op_errno)
{
        char *verdict = NULL;

        if (op_ret == 0) {
                verdict = "GRANTED";
        } else {
                switch (op_errno) {
                case EAGAIN:
                        verdict = "TRYAGAIN";
                        break;
                default:
                        verdict = strerror (op_errno);
                }
        }

        snprintf (str, size, "%s", verdict);
}


void
pl_trace_out (xlator_t *this, call_frame_t *frame, fd_t *fd, loc_t *loc,
              int cmd, struct gf_flock *flock, int op_ret, int op_errno, const char *domain)

{
        posix_locks_private_t  *priv = NULL;
        char                    pl_locker[256];
        char                    pl_lockee[256];
        char                    pl_lock[256];
        char                    verdict[32];

        priv = this->private;

        if (!priv->trace)
                return;

        pl_print_locker (pl_locker, 256, this, frame);
        pl_print_lockee (pl_lockee, 256, fd, loc);
        if (domain)
                pl_print_inodelk (pl_lock, 256, cmd, flock, domain);
        else
                pl_print_lock (pl_lock, 256, cmd, flock, &frame->root->lk_owner);

        pl_print_verdict (verdict, 32, op_ret, op_errno);

        gf_log (this->name, GF_LOG_INFO,
                "[%s] Locker = {%s} Lockee = {%s} Lock = {%s}",
                verdict, pl_locker, pl_lockee, pl_lock);
}


void
pl_trace_block (xlator_t *this, call_frame_t *frame, fd_t *fd, loc_t *loc,
                int cmd, struct gf_flock *flock, const char *domain)

{
        posix_locks_private_t  *priv = NULL;
        char                    pl_locker[256];
        char                    pl_lockee[256];
        char                    pl_lock[256];

        priv = this->private;

        if (!priv->trace)
                return;

        pl_print_locker (pl_locker, 256, this, frame);
        pl_print_lockee (pl_lockee, 256, fd, loc);
        if (domain)
                pl_print_inodelk (pl_lock, 256, cmd, flock, domain);
        else
                pl_print_lock (pl_lock, 256, cmd, flock, &frame->root->lk_owner);

        gf_log (this->name, GF_LOG_INFO,
                "[BLOCKED] Locker = {%s} Lockee = {%s} Lock = {%s}",
                pl_locker, pl_lockee, pl_lock);
}


void
pl_trace_flush (xlator_t *this, call_frame_t *frame, fd_t *fd)
{
        posix_locks_private_t  *priv = NULL;
        char                    pl_locker[256];
        char                    pl_lockee[256];
        pl_inode_t             *pl_inode = NULL;

        priv = this->private;

        if (!priv->trace)
                return;

        pl_inode = pl_inode_get (this, fd->inode);

        if (pl_inode && __pl_inode_is_empty (pl_inode))
                return;

        pl_print_locker (pl_locker, 256, this, frame);
        pl_print_lockee (pl_lockee, 256, fd, NULL);

        gf_log (this->name, GF_LOG_INFO,
                "[FLUSH] Locker = {%s} Lockee = {%s}",
                pl_locker, pl_lockee);
}

void
pl_trace_release (xlator_t *this, fd_t *fd)
{
        posix_locks_private_t  *priv = NULL;
        char                    pl_lockee[256];

        priv = this->private;

        if (!priv->trace)
                return;

        pl_print_lockee (pl_lockee, 256, fd, NULL);

        gf_log (this->name, GF_LOG_INFO,
                "[RELEASE] Lockee = {%s}", pl_lockee);
}


void
pl_update_refkeeper (xlator_t *this, inode_t *inode)
{
        pl_inode_t *pl_inode  = NULL;
        int         is_empty  = 0;
        int         need_unref = 0;
        int         need_ref = 0;

        pl_inode = pl_inode_get (this, inode);

        pthread_mutex_lock (&pl_inode->mutex);
        {
                is_empty = __pl_inode_is_empty (pl_inode);

                if (is_empty && pl_inode->refkeeper) {
                        need_unref = 1;
                        pl_inode->refkeeper = NULL;
                }

                if (!is_empty && !pl_inode->refkeeper) {
                        need_ref = 1;
                        pl_inode->refkeeper = inode;
                }
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        if (need_unref)
                inode_unref (inode);

        if (need_ref)
                inode_ref (inode);
}


pl_inode_t *
pl_inode_get (xlator_t *this, inode_t *inode)
{
        uint64_t    tmp_pl_inode = 0;
        pl_inode_t *pl_inode = NULL;
        int         ret = 0;

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &tmp_pl_inode);
                if (ret == 0) {
                        pl_inode = (pl_inode_t *)(long)tmp_pl_inode;
                        goto unlock;
                }
                pl_inode = GF_CALLOC (1, sizeof (*pl_inode),
                                      gf_locks_mt_pl_inode_t);
                if (!pl_inode) {
                        goto unlock;
                }

                gf_log (this->name, GF_LOG_TRACE,
                        "Allocating new pl inode");

                pthread_mutex_init (&pl_inode->mutex, NULL);

                INIT_LIST_HEAD (&pl_inode->dom_list);
                INIT_LIST_HEAD (&pl_inode->ext_list);
                INIT_LIST_HEAD (&pl_inode->rw_list);
                INIT_LIST_HEAD (&pl_inode->reservelk_list);
                INIT_LIST_HEAD (&pl_inode->blocked_reservelks);
                INIT_LIST_HEAD (&pl_inode->blocked_calls);
                INIT_LIST_HEAD (&pl_inode->metalk_list);
		INIT_LIST_HEAD (&pl_inode->queued_locks);
                gf_uuid_copy (pl_inode->gfid, inode->gfid);

                ret = __inode_ctx_put (inode, this, (uint64_t)(long)(pl_inode));
                if (ret) {
                        GF_FREE (pl_inode);
                        pl_inode = NULL;
                        goto unlock;
                }
        }
unlock:
        UNLOCK (&inode->lock);

        return pl_inode;
}


/* Create a new posix_lock_t */
posix_lock_t *
new_posix_lock (struct gf_flock *flock, client_t *client, pid_t client_pid,
                gf_lkowner_t *owner, fd_t *fd, uint32_t lk_flags, int blocking)
{
        posix_lock_t *lock = NULL;

        GF_VALIDATE_OR_GOTO ("posix-locks", flock, out);
        GF_VALIDATE_OR_GOTO ("posix-locks", client, out);
        GF_VALIDATE_OR_GOTO ("posix-locks", fd, out);

        lock = GF_CALLOC (1, sizeof (posix_lock_t),
                          gf_locks_mt_posix_lock_t);
        if (!lock) {
                goto out;
        }

        lock->fl_start = flock->l_start;
        lock->fl_type  = flock->l_type;

        if (flock->l_len == 0)
                lock->fl_end = LLONG_MAX;
        else
                lock->fl_end = flock->l_start + flock->l_len - 1;

        lock->client     = client;

        lock->client_uid = gf_strdup (client->client_uid);
        if (lock->client_uid == NULL) {
                GF_FREE (lock);
                goto out;
        }

        lock->fd_num     = fd_to_fdnum (fd);
        lock->fd         = fd;
        lock->client_pid = client_pid;
        lock->owner      = *owner;
        lock->lk_flags   = lk_flags;

        lock->blocking  = blocking;

        INIT_LIST_HEAD (&lock->list);

out:
        return lock;
}


/* Delete a lock from the inode's lock list */
void
__delete_lock (posix_lock_t *lock)
{
        list_del_init (&lock->list);
}


/* Destroy a posix_lock */
void
__destroy_lock (posix_lock_t *lock)
{
        GF_FREE (lock);
}


/* Convert a posix_lock to a struct gf_flock */
void
posix_lock_to_flock (posix_lock_t *lock, struct gf_flock *flock)
{
        flock->l_pid   = lock->client_pid;
        flock->l_type  = lock->fl_type;
        flock->l_start = lock->fl_start;
        flock->l_owner = lock->owner;

        if (lock->fl_end == LLONG_MAX)
                flock->l_len = 0;
        else
                flock->l_len = lock->fl_end - lock->fl_start + 1;
}

/* Insert the lock into the inode's lock list */
static void
__insert_lock (pl_inode_t *pl_inode, posix_lock_t *lock)
{
        if (lock->blocked)
                gettimeofday (&lock->blkd_time, NULL);
        else
                gettimeofday (&lock->granted_time, NULL);

        list_add_tail (&lock->list, &pl_inode->ext_list);

        return;
}


/* Return true if the locks overlap, false otherwise */
int
locks_overlap (posix_lock_t *l1, posix_lock_t *l2)
{
        /*
           Note:
           FUSE always gives us absolute offsets, so no need to worry
           about SEEK_CUR or SEEK_END
        */

        return ((l1->fl_end >= l2->fl_start) &&
                (l2->fl_end >= l1->fl_start));
}


/* Return true if the locks have the same owner */
int
same_owner (posix_lock_t *l1, posix_lock_t *l2)
{

        return (is_same_lkowner (&l1->owner, &l2->owner) &&
                (l1->client == l2->client));

}


/* Delete all F_UNLCK locks */
void
__delete_unlck_locks (pl_inode_t *pl_inode)
{
        posix_lock_t *l = NULL;
        posix_lock_t *tmp = NULL;

        list_for_each_entry_safe (l, tmp, &pl_inode->ext_list, list) {
                if (l->fl_type == F_UNLCK) {
                        __delete_lock (l);
                        __destroy_lock (l);
                }
        }
}


/* Add two locks */
static posix_lock_t *
add_locks (posix_lock_t *l1, posix_lock_t *l2)
{
        posix_lock_t *sum = NULL;

        sum = GF_CALLOC (1, sizeof (posix_lock_t),
                         gf_locks_mt_posix_lock_t);
        if (!sum)
                return NULL;

        sum->fl_start = min (l1->fl_start, l2->fl_start);
        sum->fl_end   = max (l1->fl_end, l2->fl_end);

        return sum;
}

/* Subtract two locks */
struct _values {
        posix_lock_t *locks[3];
};

/* {big} must always be contained inside {small} */
static struct _values
subtract_locks (posix_lock_t *big, posix_lock_t *small)
{

        struct _values v = { .locks = {0, 0, 0} };

        if ((big->fl_start == small->fl_start) &&
            (big->fl_end   == small->fl_end)) {
                /* both edges coincide with big */
                v.locks[0] = GF_CALLOC (1, sizeof (posix_lock_t),
                                        gf_locks_mt_posix_lock_t);
                if (!v.locks[0])
                        goto out;
                memcpy (v.locks[0], big, sizeof (posix_lock_t));
                v.locks[0]->fl_type = small->fl_type;
                goto done;
        }

        if ((small->fl_start > big->fl_start) &&
            (small->fl_end   < big->fl_end)) {
                /* both edges lie inside big */
                v.locks[0] = GF_CALLOC (1, sizeof (posix_lock_t),
                                        gf_locks_mt_posix_lock_t);
                if (!v.locks[0])
                        goto out;

                v.locks[1] = GF_CALLOC (1, sizeof (posix_lock_t),
                                        gf_locks_mt_posix_lock_t);
                if (!v.locks[1])
                        goto out;

                v.locks[2] = GF_CALLOC (1, sizeof (posix_lock_t),
                                        gf_locks_mt_posix_lock_t);
                if (!v.locks[1])
                        goto out;

                memcpy (v.locks[0], big, sizeof (posix_lock_t));
                v.locks[0]->fl_end = small->fl_start - 1;

                memcpy (v.locks[1], small, sizeof (posix_lock_t));

                memcpy (v.locks[2], big, sizeof (posix_lock_t));
                v.locks[2]->fl_start = small->fl_end + 1;
                goto done;

        }

        /* one edge coincides with big */
        if (small->fl_start == big->fl_start) {
                v.locks[0] = GF_CALLOC (1, sizeof (posix_lock_t),
                                        gf_locks_mt_posix_lock_t);
                if (!v.locks[0])
                        goto out;

                v.locks[1] = GF_CALLOC (1, sizeof (posix_lock_t),
                                        gf_locks_mt_posix_lock_t);
                if (!v.locks[1])
                        goto out;

                memcpy (v.locks[0], big, sizeof (posix_lock_t));
                v.locks[0]->fl_start = small->fl_end + 1;

                memcpy (v.locks[1], small, sizeof (posix_lock_t));
                goto done;
        }

        if (small->fl_end  == big->fl_end) {
                v.locks[0] = GF_CALLOC (1, sizeof (posix_lock_t),
                                        gf_locks_mt_posix_lock_t);
                if (!v.locks[0])
                        goto out;

                v.locks[1] = GF_CALLOC (1, sizeof (posix_lock_t),
                                        gf_locks_mt_posix_lock_t);
                if (!v.locks[1])
                        goto out;

                memcpy (v.locks[0], big, sizeof (posix_lock_t));
                v.locks[0]->fl_end = small->fl_start - 1;

                memcpy (v.locks[1], small, sizeof (posix_lock_t));
                goto done;
        }

        GF_ASSERT (0);
        gf_log ("posix-locks", GF_LOG_ERROR, "Unexpected case in subtract_locks");

out:
        if (v.locks[0]) {
                GF_FREE (v.locks[0]);
                v.locks[0] = NULL;
        }
        if (v.locks[1]) {
                GF_FREE (v.locks[1]);
                v.locks[1] = NULL;
        }
        if (v.locks[2]) {
                GF_FREE (v.locks[2]);
                v.locks[2] = NULL;
        }

done:
        return v;
}

static posix_lock_t *
first_conflicting_overlap (pl_inode_t *pl_inode, posix_lock_t *lock)
{
        posix_lock_t *l = NULL;
        posix_lock_t *conf = NULL;

        pthread_mutex_lock (&pl_inode->mutex);
        {
                list_for_each_entry (l, &pl_inode->ext_list, list) {
                        if (l->blocked)
                                continue;

                        if (locks_overlap (l, lock)) {
                                if (same_owner (l, lock))
                                        continue;

                                if ((l->fl_type == F_WRLCK) ||
                                    (lock->fl_type == F_WRLCK)) {
                                        conf = l;
                                        goto unlock;
                                }
                        }
                }
        }
unlock:
        pthread_mutex_unlock (&pl_inode->mutex);

        return conf;
}

/*
  Start searching from {begin}, and return the first lock that
  conflicts, NULL if no conflict
  If {begin} is NULL, then start from the beginning of the list
*/
static posix_lock_t *
first_overlap (pl_inode_t *pl_inode, posix_lock_t *lock)
{
        posix_lock_t *l = NULL;

        list_for_each_entry (l, &pl_inode->ext_list, list) {
                if (l->blocked)
                        continue;

                if (locks_overlap (l, lock))
                        return l;
        }

        return NULL;
}



/* Return true if lock is grantable */
static int
__is_lock_grantable (pl_inode_t *pl_inode, posix_lock_t *lock)
{
        posix_lock_t *l = NULL;
        int           ret = 1;

        list_for_each_entry (l, &pl_inode->ext_list, list) {
                if (!l->blocked && locks_overlap (lock, l)) {
                        if (((l->fl_type == F_WRLCK)
                             || (lock->fl_type == F_WRLCK))
                            && (lock->fl_type != F_UNLCK)
                            && !same_owner (l, lock)) {
                                ret = 0;
                                break;
                        }
                }
        }
        return ret;
}


extern void do_blocked_rw (pl_inode_t *);


static void
__insert_and_merge (pl_inode_t *pl_inode, posix_lock_t *lock)
{
        posix_lock_t  *conf = NULL;
        posix_lock_t  *t = NULL;
        posix_lock_t  *sum = NULL;
        int            i = 0;
        struct _values v = { .locks = {0, 0, 0} };
        client_t      *client = NULL;

        list_for_each_entry_safe (conf, t, &pl_inode->ext_list, list) {
                if (conf->blocked)
                        continue;
                if (!locks_overlap (conf, lock))
                        continue;

                if (same_owner (conf, lock)) {
                        if (conf->fl_type == lock->fl_type &&
                                        conf->lk_flags == lock->lk_flags) {
                                sum = add_locks (lock, conf);

                                sum->fl_type    = lock->fl_type;
                                sum->client     = lock->client;
                                client          = sum->client;
                                sum->client_uid =
                                         gf_strdup (client->client_uid);
                                sum->fd_num     = lock->fd_num;
                                sum->client_pid = lock->client_pid;
                                sum->owner      = lock->owner;
                                sum->lk_flags   = lock->lk_flags;

                                __delete_lock (conf);
                                __destroy_lock (conf);

                                __destroy_lock (lock);
                                INIT_LIST_HEAD (&sum->list);
                                posix_lock_to_flock (sum, &sum->user_flock);
                                __insert_and_merge (pl_inode, sum);

                                return;
                        } else {
                                sum = add_locks (lock, conf);

                                sum->fl_type    = conf->fl_type;
                                sum->client     = conf->client;
                                client          = sum->client;
                                sum->client_uid =
                                         gf_strdup (client->client_uid);

                                sum->fd_num     = conf->fd_num;
                                sum->client_pid = conf->client_pid;
                                sum->owner      = conf->owner;
                                sum->lk_flags   = conf->lk_flags;

                                v = subtract_locks (sum, lock);

                                __delete_lock (conf);
                                __destroy_lock (conf);

                                __delete_lock (lock);
                                __destroy_lock (lock);

                                __destroy_lock (sum);

                                for (i = 0; i < 3; i++) {
                                        if (!v.locks[i])
                                                continue;

                                        INIT_LIST_HEAD (&v.locks[i]->list);
                                        posix_lock_to_flock (v.locks[i],
                                                       &v.locks[i]->user_flock);
                                        __insert_and_merge (pl_inode,
                                                            v.locks[i]);
                                }

                                __delete_unlck_locks (pl_inode);
                                return;
                        }
                }

                if (lock->fl_type == F_UNLCK) {
                        continue;
                }

                if ((conf->fl_type == F_RDLCK) && (lock->fl_type == F_RDLCK)) {
                        __insert_lock (pl_inode, lock);
                        return;
                }
        }

        /* no conflicts, so just insert */
        if (lock->fl_type != F_UNLCK) {
                __insert_lock (pl_inode, lock);
        } else {
                __destroy_lock (lock);
        }
}


void
__grant_blocked_locks (xlator_t *this, pl_inode_t *pl_inode, struct list_head *granted)
{
        struct list_head  tmp_list;
        posix_lock_t     *l = NULL;
        posix_lock_t     *tmp = NULL;
        posix_lock_t     *conf = NULL;

        INIT_LIST_HEAD (&tmp_list);

        list_for_each_entry_safe (l, tmp, &pl_inode->ext_list, list) {
                if (l->blocked) {
                        conf = first_overlap (pl_inode, l);
                        if (conf)
                                continue;

                        l->blocked = 0;
                        list_move_tail (&l->list, &tmp_list);
                }
        }

        list_for_each_entry_safe (l, tmp, &tmp_list, list) {
                list_del_init (&l->list);

                if (__is_lock_grantable (pl_inode, l)) {
                        conf = GF_CALLOC (1, sizeof (*conf),
                                          gf_locks_mt_posix_lock_t);

                        if (!conf) {
                                l->blocked = 1;
                                __insert_lock (pl_inode, l);
                                continue;
                        }

                        conf->frame = l->frame;
                        l->frame = NULL;

                        posix_lock_to_flock (l, &conf->user_flock);

                        gf_log (this->name, GF_LOG_TRACE,
                                "%s (pid=%d) lk-owner:%s %"PRId64" - %"PRId64" => Granted",
                                l->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                l->client_pid, lkowner_utoa (&l->owner),
                                l->user_flock.l_start,
                                l->user_flock.l_len);

                        __insert_and_merge (pl_inode, l);

                        list_add (&conf->list, granted);
                } else {
                        l->blocked = 1;
                        __insert_lock (pl_inode, l);
                }
        }
}


void
grant_blocked_locks (xlator_t *this, pl_inode_t *pl_inode)
{
        struct list_head granted_list;
        posix_lock_t     *tmp = NULL;
        posix_lock_t     *lock = NULL;

        INIT_LIST_HEAD (&granted_list);

        pthread_mutex_lock (&pl_inode->mutex);
        {
                __grant_blocked_locks (this, pl_inode, &granted_list);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        list_for_each_entry_safe (lock, tmp, &granted_list, list) {
                list_del_init (&lock->list);

                pl_trace_out (this, lock->frame, NULL, NULL, F_SETLKW,
                              &lock->user_flock, 0, 0, NULL);

                STACK_UNWIND_STRICT (lk, lock->frame, 0, 0,
                                     &lock->user_flock, NULL);

                GF_FREE (lock);
        }

        return;
}

static int
pl_send_prelock_unlock (xlator_t *this, pl_inode_t *pl_inode,
                        posix_lock_t *old_lock)
{
        struct gf_flock  flock       = {0,};
        posix_lock_t *unlock_lock = NULL;

        struct list_head granted_list;
        posix_lock_t     *tmp = NULL;
        posix_lock_t     *lock = NULL;

        int ret = -1;

        INIT_LIST_HEAD (&granted_list);

        flock.l_type   = F_UNLCK;
        flock.l_whence = old_lock->user_flock.l_whence;
        flock.l_start  = old_lock->user_flock.l_start;
        flock.l_len    = old_lock->user_flock.l_len;


        unlock_lock = new_posix_lock (&flock, old_lock->client,
                                      old_lock->client_pid, &old_lock->owner,
                                      old_lock->fd, old_lock->lk_flags, 0);
        GF_VALIDATE_OR_GOTO (this->name, unlock_lock, out);
        ret = 0;

        __insert_and_merge (pl_inode, unlock_lock);

        __grant_blocked_locks (this, pl_inode, &granted_list);

        list_for_each_entry_safe (lock, tmp, &granted_list, list) {
                list_del_init (&lock->list);

                pl_trace_out (this, lock->frame, NULL, NULL, F_SETLKW,
                              &lock->user_flock, 0, 0, NULL);

                STACK_UNWIND_STRICT (lk, lock->frame, 0, 0,
                                     &lock->user_flock, NULL);

                GF_FREE (lock);
        }

out:
        return ret;
}

int
pl_setlk (xlator_t *this, pl_inode_t *pl_inode, posix_lock_t *lock,
          int can_block)
{
        int              ret = 0;

        errno = 0;

        pthread_mutex_lock (&pl_inode->mutex);
        {
                /* Send unlock before the actual lock to
                   prevent lock upgrade / downgrade
                   problems only if:
                   - it is a blocking call
                   - it has other conflicting locks
                */

                if (can_block &&
                    !(__is_lock_grantable (pl_inode, lock))) {
                        ret = pl_send_prelock_unlock (this, pl_inode,
                                                      lock);
                        if (ret)
                                gf_log (this->name, GF_LOG_DEBUG,
                                "Could not send pre-lock "
                                        "unlock");
                }

                if (__is_lock_grantable (pl_inode, lock)) {
                        if (pl_metalock_is_active (pl_inode)) {
                                __pl_queue_lock (pl_inode, lock, can_block);
                                pthread_mutex_unlock (&pl_inode->mutex);
                                ret = -2;
                                goto out;
                        }
                        gf_log (this->name, GF_LOG_TRACE,
                                "%s (pid=%d) lk-owner:%s %"PRId64" - %"PRId64" => OK",
                                lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                lock->client_pid,
                                lkowner_utoa (&lock->owner),
                                lock->user_flock.l_start,
                                lock->user_flock.l_len);
                        __insert_and_merge (pl_inode, lock);
                } else if (can_block) {
                        if (pl_metalock_is_active (pl_inode)) {
                                __pl_queue_lock (pl_inode, lock, can_block);
                                pthread_mutex_unlock (&pl_inode->mutex);
                                ret = -2;
                                goto out;
                        }
                        gf_log (this->name, GF_LOG_TRACE,
                                "%s (pid=%d) lk-owner:%s %"PRId64" - %"PRId64" => Blocked",
                                lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                lock->client_pid,
                                lkowner_utoa (&lock->owner),
                                lock->user_flock.l_start,
                                lock->user_flock.l_len);
                        lock->blocked = 1;
                        __insert_lock (pl_inode, lock);
                        ret = -1;
                } else {
                        gf_log (this->name, GF_LOG_TRACE,
                                "%s (pid=%d) lk-owner:%s %"PRId64" - %"PRId64" => NOK",
                                lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                                lock->client_pid,
                                lkowner_utoa (&lock->owner),
                                lock->user_flock.l_start,
                                lock->user_flock.l_len);
                        errno = EAGAIN;
                        ret = -1;
                }
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        grant_blocked_locks (this, pl_inode);

        do_blocked_rw (pl_inode);

out:
        return ret;
}


posix_lock_t *
pl_getlk (pl_inode_t *pl_inode, posix_lock_t *lock)
{
        posix_lock_t *conf = NULL;

        conf = first_conflicting_overlap (pl_inode, lock);

        if (conf == NULL) {
                lock->fl_type = F_UNLCK;
                return lock;
        }

        return conf;
}

gf_boolean_t
pl_does_monkey_want_stuck_lock()
{
        long int          monkey_unlock_rand = 0;
        long int          monkey_unlock_rand_rem = 0;

        monkey_unlock_rand = random ();
        monkey_unlock_rand_rem = monkey_unlock_rand % 100;
        if (monkey_unlock_rand_rem == 0)
                return _gf_true;
        return _gf_false;
}
