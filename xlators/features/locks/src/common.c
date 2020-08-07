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

#include <glusterfs/glusterfs.h>
#include <glusterfs/compat.h>
#include <glusterfs/logging.h>
#include <glusterfs/syncop.h>

#include "locks.h"
#include "common.h"

static int
__is_lock_grantable(pl_inode_t *pl_inode, posix_lock_t *lock);
static void
__insert_and_merge(pl_inode_t *pl_inode, posix_lock_t *lock);
static int
pl_send_prelock_unlock(xlator_t *this, pl_inode_t *pl_inode,
                       posix_lock_t *old_lock);

static pl_dom_list_t *
__allocate_domain(const char *volume)
{
    pl_dom_list_t *dom = NULL;

    dom = GF_CALLOC(1, sizeof(*dom), gf_locks_mt_pl_dom_list_t);
    if (!dom)
        goto out;

    dom->domain = gf_strdup(volume);
    if (!dom->domain)
        goto out;

    gf_log("posix-locks", GF_LOG_TRACE, "New domain allocated: %s",
           dom->domain);

    INIT_LIST_HEAD(&dom->inode_list);
    INIT_LIST_HEAD(&dom->entrylk_list);
    INIT_LIST_HEAD(&dom->blocked_entrylks);
    INIT_LIST_HEAD(&dom->inodelk_list);
    INIT_LIST_HEAD(&dom->blocked_inodelks);

out:
    if (dom && (NULL == dom->domain)) {
        GF_FREE(dom);
        dom = NULL;
    }

    return dom;
}

/* Returns domain for the lock. If domain is not present,
 * allocates a domain and returns it
 */
pl_dom_list_t *
get_domain(pl_inode_t *pl_inode, const char *volume)
{
    pl_dom_list_t *dom = NULL;

    GF_VALIDATE_OR_GOTO("posix-locks", pl_inode, out);
    GF_VALIDATE_OR_GOTO("posix-locks", volume, out);

    pthread_mutex_lock(&pl_inode->mutex);
    {
        list_for_each_entry(dom, &pl_inode->dom_list, inode_list)
        {
            if (strcmp(dom->domain, volume) == 0)
                goto unlock;
        }

        dom = __allocate_domain(volume);
        if (dom)
            list_add(&dom->inode_list, &pl_inode->dom_list);
    }
unlock:
    pthread_mutex_unlock(&pl_inode->mutex);
    if (dom) {
        gf_log("posix-locks", GF_LOG_TRACE, "Domain %s found", volume);
    } else {
        gf_log("posix-locks", GF_LOG_TRACE, "Domain %s not found", volume);
    }
out:
    return dom;
}

unsigned long
fd_to_fdnum(fd_t *fd)
{
    return ((unsigned long)fd);
}

fd_t *
fd_from_fdnum(posix_lock_t *lock)
{
    return ((fd_t *)lock->fd_num);
}

int
__pl_inode_is_empty(pl_inode_t *pl_inode)
{
    return (list_empty(&pl_inode->ext_list));
}

void
pl_print_locker(char *str, int size, xlator_t *this, call_frame_t *frame)
{
    snprintf(str, size, "Pid=%llu, lk-owner=%s, Client=%p, Frame=%llu",
             (unsigned long long)frame->root->pid,
             lkowner_utoa(&frame->root->lk_owner), frame->root->client,
             (unsigned long long)frame->root->unique);
}

void
pl_print_lockee(char *str, int size, fd_t *fd, loc_t *loc)
{
    inode_t *inode = NULL;
    char *ipath = NULL;
    int ret = 0;

    if (fd)
        inode = fd->inode;
    if (loc)
        inode = loc->inode;

    if (!inode) {
        snprintf(str, size, "<nul>");
        return;
    }

    if (loc && loc->path) {
        ipath = gf_strdup(loc->path);
    } else {
        ret = inode_path(inode, NULL, &ipath);
        if (ret <= 0)
            ipath = NULL;
    }

    snprintf(str, size, "gfid=%s, fd=%p, path=%s", uuid_utoa(inode->gfid), fd,
             ipath ? ipath : "<nul>");

    GF_FREE(ipath);
}

void
pl_print_lock(char *str, int size, int cmd, struct gf_flock *flock,
              gf_lkowner_t *owner)
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

    snprintf(str, size,
             "lock=FCNTL, cmd=%s, type=%s, "
             "start=%llu, len=%llu, pid=%llu, lk-owner=%s",
             cmd_str, type_str, (unsigned long long)flock->l_start,
             (unsigned long long)flock->l_len, (unsigned long long)flock->l_pid,
             lkowner_utoa(owner));
}

void
pl_trace_in(xlator_t *this, call_frame_t *frame, fd_t *fd, loc_t *loc, int cmd,
            struct gf_flock *flock, const char *domain)
{
    posix_locks_private_t *priv = this->private;
    char pl_locker[256];
    char pl_lockee[256];
    char pl_lock[256];

    if (!priv->trace)
        return;

    pl_print_locker(pl_locker, 256, this, frame);
    pl_print_lockee(pl_lockee, 256, fd, loc);
    if (domain)
        pl_print_inodelk(pl_lock, 256, cmd, flock, domain);
    else
        pl_print_lock(pl_lock, 256, cmd, flock, &frame->root->lk_owner);

    gf_log(this->name, GF_LOG_INFO,
           "[REQUEST] Locker = {%s} Lockee = {%s} Lock = {%s}", pl_locker,
           pl_lockee, pl_lock);
}

void
pl_print_verdict(char *str, int size, int op_ret, int op_errno)
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
                verdict = strerror(op_errno);
        }
    }

    snprintf(str, size, "%s", verdict);
}

void
pl_trace_out(xlator_t *this, call_frame_t *frame, fd_t *fd, loc_t *loc, int cmd,
             struct gf_flock *flock, int op_ret, int op_errno,
             const char *domain)

{
    posix_locks_private_t *priv = NULL;
    char pl_locker[256];
    char pl_lockee[256];
    char pl_lock[256];
    char verdict[32];

    priv = this->private;

    if (!priv->trace)
        return;

    pl_print_locker(pl_locker, 256, this, frame);
    pl_print_lockee(pl_lockee, 256, fd, loc);
    if (domain)
        pl_print_inodelk(pl_lock, 256, cmd, flock, domain);
    else
        pl_print_lock(pl_lock, 256, cmd, flock, &frame->root->lk_owner);

    pl_print_verdict(verdict, 32, op_ret, op_errno);

    gf_log(this->name, GF_LOG_INFO,
           "[%s] Locker = {%s} Lockee = {%s} Lock = {%s}", verdict, pl_locker,
           pl_lockee, pl_lock);
}

void
pl_trace_block(xlator_t *this, call_frame_t *frame, fd_t *fd, loc_t *loc,
               int cmd, struct gf_flock *flock, const char *domain)

{
    posix_locks_private_t *priv = this->private;
    char pl_locker[256];
    char pl_lockee[256];
    char pl_lock[256];

    if (!priv->trace)
        return;

    pl_print_locker(pl_locker, 256, this, frame);
    pl_print_lockee(pl_lockee, 256, fd, loc);
    if (domain)
        pl_print_inodelk(pl_lock, 256, cmd, flock, domain);
    else
        pl_print_lock(pl_lock, 256, cmd, flock, &frame->root->lk_owner);

    gf_log(this->name, GF_LOG_INFO,
           "[BLOCKED] Locker = {%s} Lockee = {%s} Lock = {%s}", pl_locker,
           pl_lockee, pl_lock);
}

void
pl_trace_flush(xlator_t *this, call_frame_t *frame, fd_t *fd)
{
    posix_locks_private_t *priv = NULL;
    char pl_locker[256];
    char pl_lockee[256];
    pl_inode_t *pl_inode = NULL;

    priv = this->private;

    if (!priv->trace)
        return;

    pl_inode = pl_inode_get(this, fd->inode, NULL);

    if (pl_inode && __pl_inode_is_empty(pl_inode))
        return;

    pl_print_locker(pl_locker, 256, this, frame);
    pl_print_lockee(pl_lockee, 256, fd, NULL);

    gf_log(this->name, GF_LOG_INFO, "[FLUSH] Locker = {%s} Lockee = {%s}",
           pl_locker, pl_lockee);
}

void
pl_trace_release(xlator_t *this, fd_t *fd)
{
    posix_locks_private_t *priv = NULL;
    char pl_lockee[256];

    priv = this->private;

    if (!priv->trace)
        return;

    pl_print_lockee(pl_lockee, 256, fd, NULL);

    gf_log(this->name, GF_LOG_INFO, "[RELEASE] Lockee = {%s}", pl_lockee);
}

void
pl_update_refkeeper(xlator_t *this, inode_t *inode)
{
    pl_inode_t *pl_inode = NULL;
    int is_empty = 0;
    int need_unref = 0;
    int need_ref = 0;

    pl_inode = pl_inode_get(this, inode, NULL);
    if (!pl_inode)
        return;

    pthread_mutex_lock(&pl_inode->mutex);
    {
        is_empty = __pl_inode_is_empty(pl_inode);

        if (is_empty && pl_inode->refkeeper) {
            need_unref = 1;
            pl_inode->refkeeper = NULL;
        }

        if (!is_empty && !pl_inode->refkeeper) {
            need_ref = 1;
            pl_inode->refkeeper = inode;
        }
    }
    pthread_mutex_unlock(&pl_inode->mutex);

    if (need_unref)
        inode_unref(inode);

    if (need_ref)
        inode_ref(inode);
}

/* Get lock enforcement info from disk */
int
pl_fetch_mlock_info_from_disk(xlator_t *this, pl_inode_t *pl_inode,
                              pl_local_t *local)
{
    dict_t *xdata_rsp = NULL;
    int ret = 0;
    int op_ret = 0;

    if (!local) {
        return -1;
    }

    if (local->fd) {
        op_ret = syncop_fgetxattr(this, local->fd, &xdata_rsp,
                                  GF_ENFORCE_MANDATORY_LOCK, NULL, NULL);
    } else {
        op_ret = syncop_getxattr(this, &local->loc[0], &xdata_rsp,
                                 GF_ENFORCE_MANDATORY_LOCK, NULL, NULL);
    }

    pthread_mutex_lock(&pl_inode->mutex);
    {
        if (op_ret >= 0) {
            pl_inode->mlock_enforced = _gf_true;
            pl_inode->check_mlock_info = _gf_false;
        } else {
            gf_msg(this->name, GF_LOG_WARNING, -op_ret, 0,
                   "getxattr failed with %d", op_ret);
            pl_inode->mlock_enforced = _gf_false;

            if (-op_ret == ENODATA) {
                pl_inode->check_mlock_info = _gf_false;
            } else {
                pl_inode->check_mlock_info = _gf_true;
            }
        }
    }
    pthread_mutex_unlock(&pl_inode->mutex);

    return ret;
}

pl_inode_t *
pl_inode_get(xlator_t *this, inode_t *inode, pl_local_t *local)
{
    uint64_t tmp_pl_inode = 0;
    pl_inode_t *pl_inode = NULL;
    int ret = 0;

    LOCK(&inode->lock);
    {
        ret = __inode_ctx_get(inode, this, &tmp_pl_inode);
        if (ret == 0) {
            pl_inode = (pl_inode_t *)(long)tmp_pl_inode;
            goto unlock;
        }

        pl_inode = GF_CALLOC(1, sizeof(*pl_inode), gf_locks_mt_pl_inode_t);
        if (!pl_inode) {
            goto unlock;
        }

        gf_log(this->name, GF_LOG_TRACE, "Allocating new pl inode");

        pthread_mutex_init(&pl_inode->mutex, NULL);
        pthread_cond_init(&pl_inode->check_fop_wind_count, 0);

        INIT_LIST_HEAD(&pl_inode->dom_list);
        INIT_LIST_HEAD(&pl_inode->ext_list);
        INIT_LIST_HEAD(&pl_inode->rw_list);
        INIT_LIST_HEAD(&pl_inode->reservelk_list);
        INIT_LIST_HEAD(&pl_inode->blocked_reservelks);
        INIT_LIST_HEAD(&pl_inode->blocked_calls);
        INIT_LIST_HEAD(&pl_inode->metalk_list);
        INIT_LIST_HEAD(&pl_inode->queued_locks);
        INIT_LIST_HEAD(&pl_inode->waiting);
        gf_uuid_copy(pl_inode->gfid, inode->gfid);

        pl_inode->check_mlock_info = _gf_true;
        pl_inode->mlock_enforced = _gf_false;

        /* -2 means never looked up. -1 means something went wrong and link
         * tracking is disabled. */
        pl_inode->links = -2;

        ret = __inode_ctx_put(inode, this, (uint64_t)(long)(pl_inode));
        if (ret) {
            pthread_mutex_destroy(&pl_inode->mutex);
            GF_FREE(pl_inode);
            pl_inode = NULL;
            goto unlock;
        }
    }
unlock:
    UNLOCK(&inode->lock);

    if ((pl_inode != NULL) && pl_is_mandatory_locking_enabled(pl_inode) &&
        pl_inode->check_mlock_info && local) {
        /* Note: The lock enforcement information per file can be stored in the
           attribute flag of stat(x) in posix. With that there won't be a need
           for doing getxattr post a reboot
        */
        pl_fetch_mlock_info_from_disk(this, pl_inode, local);
    }

    return pl_inode;
}

/* Create a new posix_lock_t */
posix_lock_t *
new_posix_lock(struct gf_flock *flock, client_t *client, pid_t client_pid,
               gf_lkowner_t *owner, fd_t *fd, uint32_t lk_flags, int blocking,
               int32_t *op_errno)
{
    posix_lock_t *lock = NULL;

    GF_VALIDATE_OR_GOTO("posix-locks", flock, out);
    GF_VALIDATE_OR_GOTO("posix-locks", client, out);
    GF_VALIDATE_OR_GOTO("posix-locks", fd, out);

    if (!pl_is_lk_owner_valid(owner, client)) {
        *op_errno = EINVAL;
        goto out;
    }

    lock = GF_CALLOC(1, sizeof(posix_lock_t), gf_locks_mt_posix_lock_t);
    if (!lock) {
        *op_errno = ENOMEM;
        goto out;
    }

    lock->fl_start = flock->l_start;
    lock->fl_type = flock->l_type;

    if (flock->l_len == 0)
        lock->fl_end = LLONG_MAX;
    else
        lock->fl_end = flock->l_start + flock->l_len - 1;

    lock->client = client;

    lock->client_uid = gf_strdup(client->client_uid);
    if (lock->client_uid == NULL) {
        GF_FREE(lock);
        lock = NULL;
        *op_errno = ENOMEM;
        goto out;
    }

    lock->fd_num = fd_to_fdnum(fd);
    lock->fd = fd;
    lock->client_pid = client_pid;
    lock->owner = *owner;
    lock->lk_flags = lk_flags;

    lock->blocking = blocking;
    memcpy(&lock->user_flock, flock, sizeof(lock->user_flock));

    INIT_LIST_HEAD(&lock->list);

out:
    return lock;
}

/* Delete a lock from the inode's lock list */
void
__delete_lock(posix_lock_t *lock)
{
    list_del_init(&lock->list);
}

/* Destroy a posix_lock */
void
__destroy_lock(posix_lock_t *lock)
{
    GF_FREE(lock->client_uid);
    GF_FREE(lock);
}

static posix_lock_t *
__copy_lock(posix_lock_t *src)
{
    posix_lock_t *dst;

    dst = GF_MALLOC(sizeof(posix_lock_t), gf_locks_mt_posix_lock_t);
    if (dst != NULL) {
        memcpy(dst, src, sizeof(posix_lock_t));
        dst->client_uid = gf_strdup(src->client_uid);
        if (dst->client_uid == NULL) {
            GF_FREE(dst);
            dst = NULL;
        }

        if (dst != NULL)
            INIT_LIST_HEAD(&dst->list);
    }

    return dst;
}

/* Convert a posix_lock to a struct gf_flock */
void
posix_lock_to_flock(posix_lock_t *lock, struct gf_flock *flock)
{
    flock->l_pid = lock->user_flock.l_pid;
    flock->l_type = lock->fl_type;
    flock->l_start = lock->fl_start;
    flock->l_owner = lock->owner;

    if (lock->fl_end == LLONG_MAX)
        flock->l_len = 0;
    else
        flock->l_len = lock->fl_end - lock->fl_start + 1;
}

/* Insert the lock into the inode's lock list */
static void
__insert_lock(pl_inode_t *pl_inode, posix_lock_t *lock)
{
    if (lock->blocked)
        lock->blkd_time = gf_time();
    else
        lock->granted_time = gf_time();

    list_add_tail(&lock->list, &pl_inode->ext_list);
}

/* Return true if the locks overlap, false otherwise */
int
locks_overlap(posix_lock_t *l1, posix_lock_t *l2)
{
    /*
       Note:
       FUSE always gives us absolute offsets, so no need to worry
       about SEEK_CUR or SEEK_END
    */

    return ((l1->fl_end >= l2->fl_start) && (l2->fl_end >= l1->fl_start));
}

/* Return true if the locks have the same owner */
int
same_owner(posix_lock_t *l1, posix_lock_t *l2)
{
    return (is_same_lkowner(&l1->owner, &l2->owner) &&
            (l1->client == l2->client));
}

/* Delete all F_UNLCK locks */
void
__delete_unlck_locks(pl_inode_t *pl_inode)
{
    posix_lock_t *l = NULL;
    posix_lock_t *tmp = NULL;

    list_for_each_entry_safe(l, tmp, &pl_inode->ext_list, list)
    {
        if (l->fl_type == F_UNLCK) {
            __delete_lock(l);
            __destroy_lock(l);
        }
    }
}

/* Add two locks */
static posix_lock_t *
add_locks(posix_lock_t *l1, posix_lock_t *l2, posix_lock_t *dst)
{
    posix_lock_t *sum = NULL;

    sum = __copy_lock(dst);
    if (!sum)
        return NULL;

    sum->fl_start = min(l1->fl_start, l2->fl_start);
    sum->fl_end = max(l1->fl_end, l2->fl_end);

    posix_lock_to_flock(sum, &sum->user_flock);

    return sum;
}

/* Subtract two locks */
struct _values {
    posix_lock_t *locks[3];
};

/* {big} must always be contained inside {small} */
static struct _values
subtract_locks(posix_lock_t *big, posix_lock_t *small)
{
    struct _values v = {.locks = {0, 0, 0}};

    if ((big->fl_start == small->fl_start) && (big->fl_end == small->fl_end)) {
        /* both edges coincide with big */
        v.locks[0] = __copy_lock(big);
        if (!v.locks[0]) {
            goto out;
        }

        v.locks[0]->fl_type = small->fl_type;
        v.locks[0]->user_flock.l_type = small->fl_type;
        goto done;
    }

    if ((small->fl_start > big->fl_start) && (small->fl_end < big->fl_end)) {
        /* both edges lie inside big */
        v.locks[0] = __copy_lock(big);
        v.locks[1] = __copy_lock(small);
        v.locks[2] = __copy_lock(big);
        if ((v.locks[0] == NULL) || (v.locks[1] == NULL) ||
            (v.locks[2] == NULL)) {
            goto out;
        }

        v.locks[0]->fl_end = small->fl_start - 1;
        v.locks[2]->fl_start = small->fl_end + 1;
        posix_lock_to_flock(v.locks[0], &v.locks[0]->user_flock);
        posix_lock_to_flock(v.locks[2], &v.locks[2]->user_flock);
        goto done;
    }

    /* one edge coincides with big */
    if (small->fl_start == big->fl_start) {
        v.locks[0] = __copy_lock(big);
        v.locks[1] = __copy_lock(small);
        if ((v.locks[0] == NULL) || (v.locks[1] == NULL)) {
            goto out;
        }

        v.locks[0]->fl_start = small->fl_end + 1;
        posix_lock_to_flock(v.locks[0], &v.locks[0]->user_flock);
        goto done;
    }

    if (small->fl_end == big->fl_end) {
        v.locks[0] = __copy_lock(big);
        v.locks[1] = __copy_lock(small);
        if ((v.locks[0] == NULL) || (v.locks[1] == NULL)) {
            goto out;
        }

        v.locks[0]->fl_end = small->fl_start - 1;
        posix_lock_to_flock(v.locks[0], &v.locks[0]->user_flock);
        goto done;
    }

    GF_ASSERT(0);
    gf_log("posix-locks", GF_LOG_ERROR, "Unexpected case in subtract_locks");

out:
    if (v.locks[0]) {
        __destroy_lock(v.locks[0]);
        v.locks[0] = NULL;
    }
    if (v.locks[1]) {
        __destroy_lock(v.locks[1]);
        v.locks[1] = NULL;
    }
    if (v.locks[2]) {
        __destroy_lock(v.locks[2]);
        v.locks[2] = NULL;
    }

done:
    return v;
}

static posix_lock_t *
first_conflicting_overlap(pl_inode_t *pl_inode, posix_lock_t *lock)
{
    posix_lock_t *l = NULL;
    posix_lock_t *conf = NULL;

    pthread_mutex_lock(&pl_inode->mutex);
    {
        list_for_each_entry(l, &pl_inode->ext_list, list)
        {
            if (l->blocked)
                continue;

            if (locks_overlap(l, lock)) {
                if (same_owner(l, lock))
                    continue;

                if ((l->fl_type == F_WRLCK) || (lock->fl_type == F_WRLCK)) {
                    conf = l;
                    goto unlock;
                }
            }
        }
    }
unlock:
    pthread_mutex_unlock(&pl_inode->mutex);

    return conf;
}

/*
  Start searching from {begin}, and return the first lock that
  conflicts, NULL if no conflict
  If {begin} is NULL, then start from the beginning of the list
*/
static posix_lock_t *
first_overlap(pl_inode_t *pl_inode, posix_lock_t *lock)
{
    posix_lock_t *l = NULL;

    list_for_each_entry(l, &pl_inode->ext_list, list)
    {
        if (l->blocked)
            continue;

        if (locks_overlap(l, lock))
            return l;
    }

    return NULL;
}

/* Return true if lock is grantable */
static int
__is_lock_grantable(pl_inode_t *pl_inode, posix_lock_t *lock)
{
    posix_lock_t *l = NULL;
    int ret = 1;

    list_for_each_entry(l, &pl_inode->ext_list, list)
    {
        if (!l->blocked && locks_overlap(lock, l)) {
            if (((l->fl_type == F_WRLCK) || (lock->fl_type == F_WRLCK)) &&
                (lock->fl_type != F_UNLCK) && !same_owner(l, lock)) {
                ret = 0;
                break;
            }
        }
    }
    return ret;
}

extern void
do_blocked_rw(pl_inode_t *);

static void
__insert_and_merge(pl_inode_t *pl_inode, posix_lock_t *lock)
{
    posix_lock_t *conf = NULL;
    posix_lock_t *t = NULL;
    posix_lock_t *sum = NULL;
    int i = 0;
    struct _values v = {.locks = {0, 0, 0}};

    list_for_each_entry_safe(conf, t, &pl_inode->ext_list, list)
    {
        if (conf->blocked)
            continue;
        if (!locks_overlap(conf, lock))
            continue;

        if (same_owner(conf, lock)) {
            if (conf->fl_type == lock->fl_type &&
                conf->lk_flags == lock->lk_flags) {
                sum = add_locks(lock, conf, lock);

                __delete_lock(conf);
                __destroy_lock(conf);

                __destroy_lock(lock);
                INIT_LIST_HEAD(&sum->list);
                posix_lock_to_flock(sum, &sum->user_flock);
                __insert_and_merge(pl_inode, sum);

                return;
            } else {
                sum = add_locks(lock, conf, conf);

                v = subtract_locks(sum, lock);

                __delete_lock(conf);
                __destroy_lock(conf);

                __delete_lock(lock);
                __destroy_lock(lock);

                __destroy_lock(sum);

                for (i = 0; i < 3; i++) {
                    if (!v.locks[i])
                        continue;

                    __insert_and_merge(pl_inode, v.locks[i]);
                }

                __delete_unlck_locks(pl_inode);
                return;
            }
        }

        if (lock->fl_type == F_UNLCK) {
            continue;
        }

        if ((conf->fl_type == F_RDLCK) && (lock->fl_type == F_RDLCK)) {
            __insert_lock(pl_inode, lock);
            return;
        }
    }

    /* no conflicts, so just insert */
    if (lock->fl_type != F_UNLCK) {
        __insert_lock(pl_inode, lock);
    } else {
        __destroy_lock(lock);
    }
}

void
__grant_blocked_locks(xlator_t *this, pl_inode_t *pl_inode,
                      struct list_head *granted)
{
    struct list_head tmp_list;
    posix_lock_t *l = NULL;
    posix_lock_t *tmp = NULL;
    posix_lock_t *conf = NULL;

    INIT_LIST_HEAD(&tmp_list);

    list_for_each_entry_safe(l, tmp, &pl_inode->ext_list, list)
    {
        if (l->blocked) {
            conf = first_overlap(pl_inode, l);
            if (conf)
                continue;

            l->blocked = 0;
            list_move_tail(&l->list, &tmp_list);
        }
    }

    list_for_each_entry_safe(l, tmp, &tmp_list, list)
    {
        list_del_init(&l->list);

        if (__is_lock_grantable(pl_inode, l)) {
            conf = GF_CALLOC(1, sizeof(*conf), gf_locks_mt_posix_lock_t);

            if (!conf) {
                l->blocked = 1;
                __insert_lock(pl_inode, l);
                continue;
            }

            conf->frame = l->frame;
            l->frame = NULL;

            posix_lock_to_flock(l, &conf->user_flock);

            gf_log(this->name, GF_LOG_TRACE,
                   "%s (pid=%d) lk-owner:%s %" PRId64 " - %" PRId64
                   " => Granted",
                   l->fl_type == F_UNLCK ? "Unlock" : "Lock", l->client_pid,
                   lkowner_utoa(&l->owner), l->user_flock.l_start,
                   l->user_flock.l_len);

            __insert_and_merge(pl_inode, l);

            list_add(&conf->list, granted);
        } else {
            l->blocked = 1;
            __insert_lock(pl_inode, l);
        }
    }
}

void
grant_blocked_locks(xlator_t *this, pl_inode_t *pl_inode)
{
    struct list_head granted_list;
    posix_lock_t *tmp = NULL;
    posix_lock_t *lock = NULL;
    pl_local_t *local = NULL;
    INIT_LIST_HEAD(&granted_list);

    pthread_mutex_lock(&pl_inode->mutex);
    {
        __grant_blocked_locks(this, pl_inode, &granted_list);
    }
    pthread_mutex_unlock(&pl_inode->mutex);

    list_for_each_entry_safe(lock, tmp, &granted_list, list)
    {
        list_del_init(&lock->list);

        pl_trace_out(this, lock->frame, NULL, NULL, F_SETLKW, &lock->user_flock,
                     0, 0, NULL);
        local = lock->frame->local;
        PL_STACK_UNWIND_AND_FREE(local, lk, lock->frame, 0, 0,
                                 &lock->user_flock, NULL);
        __destroy_lock(lock);
    }

    return;
}

static int
pl_send_prelock_unlock(xlator_t *this, pl_inode_t *pl_inode,
                       posix_lock_t *old_lock)
{
    struct gf_flock flock = {
        0,
    };
    posix_lock_t *unlock_lock = NULL;
    int32_t op_errno = 0;

    struct list_head granted_list;
    posix_lock_t *tmp = NULL;
    posix_lock_t *lock = NULL;
    pl_local_t *local = NULL;

    int ret = -1;

    INIT_LIST_HEAD(&granted_list);

    flock.l_type = F_UNLCK;
    flock.l_whence = old_lock->user_flock.l_whence;
    flock.l_start = old_lock->user_flock.l_start;
    flock.l_len = old_lock->user_flock.l_len;
    flock.l_pid = old_lock->user_flock.l_pid;

    unlock_lock = new_posix_lock(&flock, old_lock->client, old_lock->client_pid,
                                 &old_lock->owner, old_lock->fd,
                                 old_lock->lk_flags, 0, &op_errno);
    GF_VALIDATE_OR_GOTO(this->name, unlock_lock, out);
    ret = 0;

    __insert_and_merge(pl_inode, unlock_lock);

    __grant_blocked_locks(this, pl_inode, &granted_list);

    list_for_each_entry_safe(lock, tmp, &granted_list, list)
    {
        list_del_init(&lock->list);

        pl_trace_out(this, lock->frame, NULL, NULL, F_SETLKW, &lock->user_flock,
                     0, 0, NULL);
        local = lock->frame->local;
        PL_STACK_UNWIND_AND_FREE(local, lk, lock->frame, 0, 0,
                                 &lock->user_flock, NULL);
        __destroy_lock(lock);
    }

out:
    return ret;
}

int
pl_setlk(xlator_t *this, pl_inode_t *pl_inode, posix_lock_t *lock,
         int can_block)
{
    int ret = 0;

    errno = 0;

    pthread_mutex_lock(&pl_inode->mutex);
    {
        /* Send unlock before the actual lock to
           prevent lock upgrade / downgrade
           problems only if:
           - it is a blocking call
           - it has other conflicting locks
        */

        if (can_block && !(__is_lock_grantable(pl_inode, lock))) {
            ret = pl_send_prelock_unlock(this, pl_inode, lock);
            if (ret)
                gf_log(this->name, GF_LOG_DEBUG,
                       "Could not send pre-lock "
                       "unlock");
        }

        if (__is_lock_grantable(pl_inode, lock)) {
            if (pl_metalock_is_active(pl_inode)) {
                __pl_queue_lock(pl_inode, lock);
                pthread_mutex_unlock(&pl_inode->mutex);
                ret = -2;
                goto out;
            }
            gf_log(this->name, GF_LOG_TRACE,
                   "%s (pid=%d) lk-owner:%s %" PRId64 " - %" PRId64 " => OK",
                   lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                   lock->client_pid, lkowner_utoa(&lock->owner),
                   lock->user_flock.l_start, lock->user_flock.l_len);
            __insert_and_merge(pl_inode, lock);
        } else if (can_block) {
            if (pl_metalock_is_active(pl_inode)) {
                __pl_queue_lock(pl_inode, lock);
                pthread_mutex_unlock(&pl_inode->mutex);
                ret = -2;
                goto out;
            }
            gf_log(this->name, GF_LOG_TRACE,
                   "%s (pid=%d) lk-owner:%s %" PRId64 " - %" PRId64
                   " => Blocked",
                   lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                   lock->client_pid, lkowner_utoa(&lock->owner),
                   lock->user_flock.l_start, lock->user_flock.l_len);

            pl_trace_block(this, lock->frame, NULL, NULL, F_SETLKW,
                           &lock->user_flock, NULL);

            lock->blocked = 1;
            __insert_lock(pl_inode, lock);
            ret = -1;
        } else {
            gf_log(this->name, GF_LOG_TRACE,
                   "%s (pid=%d) lk-owner:%s %" PRId64 " - %" PRId64 " => NOK",
                   lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
                   lock->client_pid, lkowner_utoa(&lock->owner),
                   lock->user_flock.l_start, lock->user_flock.l_len);
            errno = EAGAIN;
            ret = -1;
        }
    }
    pthread_mutex_unlock(&pl_inode->mutex);

    grant_blocked_locks(this, pl_inode);

    do_blocked_rw(pl_inode);

out:
    return ret;
}

posix_lock_t *
pl_getlk(pl_inode_t *pl_inode, posix_lock_t *lock)
{
    posix_lock_t *conf = first_conflicting_overlap(pl_inode, lock);
    if (conf == NULL) {
        lock->fl_type = F_UNLCK;
        return lock;
    }

    return conf;
}

gf_boolean_t
pl_does_monkey_want_stuck_lock()
{
    long int monkey_unlock_rand = 0;
    long int monkey_unlock_rand_rem = 0;

    /* coverity[DC.WEAK_CRYPTO] */
    monkey_unlock_rand = random();
    monkey_unlock_rand_rem = monkey_unlock_rand % 100;
    if (monkey_unlock_rand_rem == 0)
        return _gf_true;
    return _gf_false;
}

int
pl_lock_preempt(pl_inode_t *pl_inode, posix_lock_t *reqlock)
{
    posix_lock_t *lock = NULL;
    posix_lock_t *i = NULL;
    pl_rw_req_t *rw = NULL;
    pl_rw_req_t *itr = NULL;
    struct list_head unwind_blist = {
        0,
    };
    struct list_head unwind_rw_list = {
        0,
    };
    int ret = 0;

    INIT_LIST_HEAD(&unwind_blist);
    INIT_LIST_HEAD(&unwind_rw_list);

    pthread_mutex_lock(&pl_inode->mutex);
    {
        /*
            - go through the lock list
            - remove all locks from different owners
            - same owner locks will be added or substracted based on
              the new request
            - add the new lock
        */
        list_for_each_entry_safe(lock, i, &pl_inode->ext_list, list)
        {
            if (lock->blocked) {
                list_del_init(&lock->list);
                list_add(&lock->list, &unwind_blist);
                continue;
            }

            if (locks_overlap(lock, reqlock)) {
                if (same_owner(lock, reqlock))
                    continue;

                /* remove conflicting locks */
                list_del_init(&lock->list);
                __delete_lock(lock);
                __destroy_lock(lock);
            }
        }

        __insert_and_merge(pl_inode, reqlock);

        list_for_each_entry_safe(rw, itr, &pl_inode->rw_list, list)
        {
            list_del_init(&rw->list);
            list_add(&rw->list, &unwind_rw_list);
        }
    }
    pthread_mutex_unlock(&pl_inode->mutex);

    /* unwind blocked locks */
    list_for_each_entry_safe(lock, i, &unwind_blist, list)
    {
        PL_STACK_UNWIND_AND_FREE(((pl_local_t *)lock->frame->local), lk,
                                 lock->frame, -1, EBUSY, &lock->user_flock,
                                 NULL);
        __destroy_lock(lock);
    }

    /* unwind blocked IOs */
    list_for_each_entry_safe(rw, itr, &unwind_rw_list, list)
    {
        pl_clean_local(rw->stub->frame->local);
        call_unwind_error(rw->stub, -1, EBUSY);
    }

    return ret;
}

/* Return true in case we need to ensure mandatory-locking
 * semantics under different modes.
 */
gf_boolean_t
pl_is_mandatory_locking_enabled(pl_inode_t *pl_inode)
{
    posix_locks_private_t *priv = THIS->private;

    if (priv->mandatory_mode == MLK_FILE_BASED && pl_inode->mandatory)
        return _gf_true;
    else if (priv->mandatory_mode == MLK_FORCED ||
             priv->mandatory_mode == MLK_OPTIMAL)
        return _gf_true;

    return _gf_false;
}

void
pl_clean_local(pl_local_t *local)
{
    if (!local)
        return;

    if (local->inodelk_dom_count_req)
        data_unref(local->inodelk_dom_count_req);
    loc_wipe(&local->loc[0]);
    loc_wipe(&local->loc[1]);
    if (local->fd)
        fd_unref(local->fd);
    if (local->inode)
        inode_unref(local->inode);
    mem_put(local);
}

/*
TODO: detach local initialization from PL_LOCAL_GET_REQUESTS and add it here
*/
int
pl_local_init(call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
    pl_local_t *local = NULL;

    if (!loc && !fd) {
        return -1;
    }

    if (!frame->local) {
        local = mem_get0(this->local_pool);
        if (!local) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0,
                   "mem allocation failed");
            return -1;
        }

        local->inode = (loc ? inode_ref(loc->inode) : inode_ref(fd->inode));

        frame->local = local;
    }

    return 0;
}

gf_boolean_t
pl_is_lk_owner_valid(gf_lkowner_t *owner, client_t *client)
{
    if (client && (client->opversion < GD_OP_VERSION_7_0)) {
        return _gf_true;
    }

    if (is_lk_owner_null(owner)) {
        return _gf_false;
    }
    return _gf_true;
}

static int32_t
pl_inode_from_loc(loc_t *loc, inode_t **pinode)
{
    inode_t *inode = NULL;
    int32_t error = 0;

    if (loc->inode != NULL) {
        inode = inode_ref(loc->inode);
        goto done;
    }

    if (loc->parent == NULL) {
        error = EINVAL;
        goto done;
    }

    if (!gf_uuid_is_null(loc->gfid)) {
        inode = inode_find(loc->parent->table, loc->gfid);
        if (inode != NULL) {
            goto done;
        }
    }

    if (loc->name == NULL) {
        error = EINVAL;
        goto done;
    }

    inode = inode_grep(loc->parent->table, loc->parent, loc->name);
    if (inode == NULL) {
        /* We haven't found any inode. This means that the file doesn't exist
         * or that even if it exists, we don't have any knowledge about it, so
         * we don't have locks on it either, which is fine for our purposes. */
        goto done;
    }

done:
    *pinode = inode;

    return error;
}

static gf_boolean_t
pl_inode_has_owners(xlator_t *xl, client_t *client, pl_inode_t *pl_inode,
                    struct timespec *now, struct list_head *contend)
{
    pl_dom_list_t *dom;
    pl_inode_lock_t *lock;
    gf_boolean_t has_owners = _gf_false;

    list_for_each_entry(dom, &pl_inode->dom_list, inode_list)
    {
        list_for_each_entry(lock, &dom->inodelk_list, list)
        {
            /* If the lock belongs to the same client, we assume it's related
             * to the same operation, so we allow the removal to continue. */
            if (lock->client == client) {
                continue;
            }
            /* If the lock belongs to an internal process, we don't block the
             * removal. */
            if (lock->client_pid < 0) {
                continue;
            }
            if (contend == NULL) {
                return _gf_true;
            }
            has_owners = _gf_true;
            inodelk_contention_notify_check(xl, lock, now, contend);
        }
    }

    return has_owners;
}

int32_t
pl_inode_remove_prepare(xlator_t *xl, call_frame_t *frame, loc_t *loc,
                        pl_inode_t **ppl_inode, struct list_head *contend)
{
    struct timespec now;
    inode_t *inode;
    pl_inode_t *pl_inode;
    int32_t error;

    pl_inode = NULL;

    error = pl_inode_from_loc(loc, &inode);
    if ((error != 0) || (inode == NULL)) {
        goto done;
    }

    pl_inode = pl_inode_get(xl, inode, NULL);
    if (pl_inode == NULL) {
        inode_unref(inode);
        error = ENOMEM;
        goto done;
    }

    /* pl_inode_from_loc() already increments ref count for inode, so
     * we only assign here our reference. */
    pl_inode->inode = inode;

    timespec_now(&now);

    pthread_mutex_lock(&pl_inode->mutex);

    if (pl_inode->removed) {
        error = ESTALE;
        goto unlock;
    }

    if (pl_inode_has_owners(xl, frame->root->client, pl_inode, &now, contend)) {
        error = -1;
        /* We skip the unlock here because the caller must create a stub when
         * we return -1 and do a call to pl_inode_remove_complete(), which
         * assumes the lock is still acquired and will release it once
         * everything else is prepared. */
        goto done;
    }

    pl_inode->is_locked = _gf_true;
    pl_inode->remove_running++;

unlock:
    pthread_mutex_unlock(&pl_inode->mutex);

done:
    *ppl_inode = pl_inode;

    return error;
}

int32_t
pl_inode_remove_complete(xlator_t *xl, pl_inode_t *pl_inode, call_stub_t *stub,
                         struct list_head *contend)
{
    pl_inode_lock_t *lock;
    int32_t error = -1;

    if (stub != NULL) {
        list_add_tail(&stub->list, &pl_inode->waiting);
        pl_inode->is_locked = _gf_true;
    } else {
        error = ENOMEM;

        while (!list_empty(contend)) {
            lock = list_first_entry(contend, pl_inode_lock_t, list);
            list_del_init(&lock->list);
            __pl_inodelk_unref(lock);
        }
    }

    pthread_mutex_unlock(&pl_inode->mutex);

    if (error < 0) {
        inodelk_contention_notify(xl, contend);
    }

    inode_unref(pl_inode->inode);

    return error;
}

void
pl_inode_remove_wake(struct list_head *list)
{
    call_stub_t *stub;

    while (!list_empty(list)) {
        stub = list_first_entry(list, call_stub_t, list);
        list_del_init(&stub->list);

        call_resume(stub);
    }
}

void
pl_inode_remove_cbk(xlator_t *xl, pl_inode_t *pl_inode, int32_t error)
{
    struct list_head contend, granted;
    struct timespec now;
    pl_dom_list_t *dom;

    if (pl_inode == NULL) {
        return;
    }

    INIT_LIST_HEAD(&contend);
    INIT_LIST_HEAD(&granted);
    timespec_now(&now);

    pthread_mutex_lock(&pl_inode->mutex);

    if (error == 0) {
        if (pl_inode->links >= 0) {
            pl_inode->links--;
        }
        if (pl_inode->links == 0) {
            pl_inode->removed = _gf_true;
        }
    }

    pl_inode->remove_running--;

    if ((pl_inode->remove_running == 0) && list_empty(&pl_inode->waiting)) {
        pl_inode->is_locked = _gf_false;

        list_for_each_entry(dom, &pl_inode->dom_list, inode_list)
        {
            __grant_blocked_inode_locks(xl, pl_inode, &granted, dom, &now,
                                        &contend);
        }
    }

    pthread_mutex_unlock(&pl_inode->mutex);

    unwind_granted_inodes(xl, pl_inode, &granted);

    inodelk_contention_notify(xl, &contend);

    inode_unref(pl_inode->inode);
}

void
pl_inode_remove_unlocked(xlator_t *xl, pl_inode_t *pl_inode,
                         struct list_head *list)
{
    call_stub_t *stub, *tmp;

    if (!pl_inode->is_locked) {
        return;
    }

    list_for_each_entry_safe(stub, tmp, &pl_inode->waiting, list)
    {
        if (!pl_inode_has_owners(xl, stub->frame->root->client, pl_inode, NULL,
                                 NULL)) {
            list_move_tail(&stub->list, list);
        }
    }
}

/* This function determines if an inodelk attempt can be done now or it needs
 * to wait.
 *
 * Possible return values:
 *   < 0: An error occurred. Currently only -ESTALE can be returned if the
 *        inode has been deleted previously by unlink/rmdir/rename
 *   = 0: The lock can be attempted.
 *   > 0: The lock needs to wait because a conflicting remove operation is
 *        ongoing.
 */
int32_t
pl_inode_remove_inodelk(pl_inode_t *pl_inode, pl_inode_lock_t *lock)
{
    pl_dom_list_t *dom;
    pl_inode_lock_t *ilock;

    /* If the inode has been deleted, we won't allow any lock. */
    if (pl_inode->removed) {
        return -ESTALE;
    }

    /* We only synchronize with locks made for regular operations coming from
     * the user. Locks done for internal purposes are hard to control and could
     * lead to long delays or deadlocks quite easily. */
    if (lock->client_pid < 0) {
        return 0;
    }
    if (!pl_inode->is_locked) {
        return 0;
    }
    if (pl_inode->remove_running > 0) {
        return 1;
    }

    list_for_each_entry(dom, &pl_inode->dom_list, inode_list)
    {
        list_for_each_entry(ilock, &dom->inodelk_list, list)
        {
            /* If a lock from the same client is already granted, we allow this
             * one to continue. This is necessary to prevent deadlocks when
             * multiple locks are taken for the same operation.
             *
             * On the other side it's unlikely that the same client sends
             * completely unrelated locks for the same inode.
             */
            if (ilock->client == lock->client) {
                return 0;
            }
        }
    }

    return 1;
}
