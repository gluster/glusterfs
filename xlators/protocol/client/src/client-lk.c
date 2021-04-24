/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <glusterfs/common-utils.h>
#include <glusterfs/xlator.h>
#include "client.h"
#include <glusterfs/lkowner.h>
#include "client-messages.h"

static void
__insert_and_merge(clnt_fd_ctx_t *fdctx, client_posix_lock_t *lock);

static void
__dump_client_lock(client_posix_lock_t *lock)
{
    xlator_t *this = NULL;

    this = THIS;

    gf_smsg(
        this->name, GF_LOG_INFO, 0, PC_MSG_CLIENT_LOCK_INFO, "fd=%p", lock->fd,
        "fl_type=%s", lock->fl_type == F_WRLCK ? "Write-Lock" : "Read-Lock",
        "lk-owner=%s", lkowner_utoa(&lock->owner), "l_start=%" PRId64,
        lock->user_flock.l_start, "l_len=%" PRId64, lock->user_flock.l_len,
        "start=%" PRId64, lock->fl_start, "end=%" PRId64, lock->fl_end, NULL);
}

static int
dump_client_locks_fd(clnt_fd_ctx_t *fdctx)
{
    client_posix_lock_t *lock = NULL;
    int count = 0;

    list_for_each_entry(lock, &fdctx->lock_list, list)
    {
        __dump_client_lock(lock);
        count++;
    }

    return count;
}

int
dump_client_locks(inode_t *inode)
{
    fd_t *fd = NULL;
    xlator_t *this = NULL;
    clnt_fd_ctx_t *fdctx = NULL;
    clnt_conf_t *conf = NULL;

    int total_count = 0;
    int locks_fd_count = 0;

    this = THIS;
    conf = this->private;

    LOCK(&inode->lock);
    {
        list_for_each_entry(fd, &inode->fd_list, inode_list)
        {
            locks_fd_count = 0;

            pthread_spin_lock(&conf->fd_lock);
            fdctx = this_fd_get_ctx(fd, this);
            if (fdctx)
                locks_fd_count = dump_client_locks_fd(fdctx);
            pthread_spin_unlock(&conf->fd_lock);

            total_count += locks_fd_count;
        }
    }
    UNLOCK(&inode->lock);

    return total_count;
}

static off_t
__get_lock_length(off_t start, off_t end)
{
    if (end == LLONG_MAX)
        return 0;
    else
        return (end - start + 1);
}

/* Add two locks */
static client_posix_lock_t *
add_locks(client_posix_lock_t *l1, client_posix_lock_t *l2)
{
    client_posix_lock_t *sum = NULL;

    sum = GF_CALLOC(1, sizeof(*sum), gf_client_mt_clnt_lock_t);
    if (!sum)
        return NULL;

    sum->fl_start = min(l1->fl_start, l2->fl_start);
    sum->fl_end = max(l1->fl_end, l2->fl_end);

    sum->user_flock.l_start = sum->fl_start;
    sum->user_flock.l_len = __get_lock_length(sum->fl_start, sum->fl_end);

    return sum;
}

/* Return true if the locks overlap, false otherwise */
static int
locks_overlap(client_posix_lock_t *l1, client_posix_lock_t *l2)
{
    /*
       Note:
       FUSE always gives us absolute offsets, so no need to worry
       about SEEK_CUR or SEEK_END
    */

    return ((l1->fl_end >= l2->fl_start) && (l2->fl_end >= l1->fl_start));
}

static void
__delete_client_lock(client_posix_lock_t *lock)
{
    list_del_init(&lock->list);
}

/* Destroy a posix_lock */
static void
__destroy_client_lock(client_posix_lock_t *lock)
{
    GF_FREE(lock);
}

/* Subtract two locks */
struct _values {
    client_posix_lock_t *locks[3];
};

/* {big} must always be contained inside {small} */
static struct _values
subtract_locks(client_posix_lock_t *big, client_posix_lock_t *small)
{
    struct _values v = {.locks = {0, 0, 0}};

    if ((big->fl_start == small->fl_start) && (big->fl_end == small->fl_end)) {
        /* both edges coincide with big */
        v.locks[0] = GF_MALLOC(sizeof(client_posix_lock_t),
                               gf_client_mt_clnt_lock_t);
        GF_ASSERT(v.locks[0]);
        memcpy(v.locks[0], big, sizeof(client_posix_lock_t));
        v.locks[0]->fl_type = small->fl_type;
    } else if ((small->fl_start > big->fl_start) &&
               (small->fl_end < big->fl_end)) {
        /* both edges lie inside big */
        v.locks[0] = GF_MALLOC(sizeof(client_posix_lock_t),
                               gf_client_mt_clnt_lock_t);
        GF_ASSERT(v.locks[0]);
        memcpy(v.locks[0], big, sizeof(client_posix_lock_t));
        v.locks[0]->fl_end = small->fl_start - 1;
        v.locks[0]->user_flock.l_len = __get_lock_length(v.locks[0]->fl_start,
                                                         v.locks[0]->fl_end);
        v.locks[1] = GF_MALLOC(sizeof(client_posix_lock_t),
                               gf_client_mt_clnt_lock_t);
        GF_ASSERT(v.locks[1]);
        memcpy(v.locks[1], small, sizeof(client_posix_lock_t));
        v.locks[2] = GF_MALLOC(sizeof(client_posix_lock_t),
                               gf_client_mt_clnt_lock_t);
        GF_ASSERT(v.locks[2]);
        memcpy(v.locks[2], big, sizeof(client_posix_lock_t));
        v.locks[2]->fl_start = small->fl_end + 1;
        v.locks[2]->user_flock.l_start = small->fl_end + 1;
    }
    /* one edge coincides with big */
    else if (small->fl_start == big->fl_start) {
        v.locks[0] = GF_MALLOC(sizeof(client_posix_lock_t),
                               gf_client_mt_clnt_lock_t);
        GF_ASSERT(v.locks[0]);
        memcpy(v.locks[0], big, sizeof(client_posix_lock_t));
        v.locks[0]->fl_start = small->fl_end + 1;
        v.locks[0]->user_flock.l_start = small->fl_end + 1;
        v.locks[1] = GF_MALLOC(sizeof(client_posix_lock_t),
                               gf_client_mt_clnt_lock_t);
        GF_ASSERT(v.locks[1]);
        memcpy(v.locks[1], small, sizeof(client_posix_lock_t));
    } else if (small->fl_end == big->fl_end) {
        v.locks[0] = GF_MALLOC(sizeof(client_posix_lock_t),
                               gf_client_mt_clnt_lock_t);
        GF_ASSERT(v.locks[0]);
        memcpy(v.locks[0], big, sizeof(client_posix_lock_t));
        v.locks[0]->fl_end = small->fl_start - 1;
        v.locks[0]->user_flock.l_len = __get_lock_length(v.locks[0]->fl_start,
                                                         v.locks[0]->fl_end);

        v.locks[1] = GF_MALLOC(sizeof(client_posix_lock_t),
                               gf_client_mt_clnt_lock_t);
        GF_ASSERT(v.locks[1]);
        memcpy(v.locks[1], small, sizeof(client_posix_lock_t));
    } else {
        /* LOG-TODO : decide what more info is required here*/
        gf_smsg("client-protocol", GF_LOG_CRITICAL, 0, PC_MSG_LOCK_ERROR, NULL);
    }

    return v;
}

static void
__delete_unlck_locks(clnt_fd_ctx_t *fdctx)
{
    client_posix_lock_t *l = NULL;
    client_posix_lock_t *tmp = NULL;

    list_for_each_entry_safe(l, tmp, &fdctx->lock_list, list)
    {
        if (l->fl_type == F_UNLCK) {
            __delete_client_lock(l);
            __destroy_client_lock(l);
        }
    }
}

static void
__insert_lock(clnt_fd_ctx_t *fdctx, client_posix_lock_t *lock)
{
    list_add_tail(&lock->list, &fdctx->lock_list);

    return;
}

static void
__insert_and_merge(clnt_fd_ctx_t *fdctx, client_posix_lock_t *lock)
{
    client_posix_lock_t *conf = NULL;
    client_posix_lock_t *t = NULL;
    client_posix_lock_t *sum = NULL;
    int i = 0;
    struct _values v = {.locks = {0, 0, 0}};

    list_for_each_entry_safe(conf, t, &fdctx->lock_list, list)
    {
        if (!locks_overlap(conf, lock))
            continue;

        if (is_same_lkowner(&conf->owner, &lock->owner)) {
            if (conf->fl_type == lock->fl_type) {
                sum = add_locks(lock, conf);

                sum->fd = lock->fd;
                sum->owner = conf->owner;

                __delete_client_lock(conf);
                __destroy_client_lock(conf);

                __destroy_client_lock(lock);
                __insert_and_merge(fdctx, sum);

                return;
            } else {
                sum = add_locks(lock, conf);

                sum->fd = conf->fd;
                sum->owner = conf->owner;

                v = subtract_locks(sum, lock);

                __delete_client_lock(conf);
                __destroy_client_lock(conf);

                __delete_client_lock(lock);
                __destroy_client_lock(lock);

                __destroy_client_lock(sum);

                for (i = 0; i < 3; i++) {
                    if (!v.locks[i])
                        continue;

                    INIT_LIST_HEAD(&v.locks[i]->list);
                    __insert_and_merge(fdctx, v.locks[i]);
                }

                __delete_unlck_locks(fdctx);
                return;
            }
        }

        if (lock->fl_type == F_UNLCK) {
            continue;
        }

        if ((conf->fl_type == F_RDLCK) && (lock->fl_type == F_RDLCK)) {
            __insert_lock(fdctx, lock);
            return;
        }
    }

    /* no conflicts, so just insert */
    if (lock->fl_type != F_UNLCK) {
        __insert_lock(fdctx, lock);
    } else {
        __destroy_client_lock(lock);
    }
}

static void
client_setlk(clnt_fd_ctx_t *fdctx, client_posix_lock_t *lock)
{
    __insert_and_merge(fdctx, lock);
}

static void
destroy_client_lock(client_posix_lock_t *lock)
{
    GF_FREE(lock);
}

void
destroy_client_locks_from_list(struct list_head *deleted)
{
    client_posix_lock_t *lock = NULL;
    client_posix_lock_t *tmp = NULL;
    xlator_t *this = THIS;
    int count = 0;

    list_for_each_entry_safe(lock, tmp, deleted, list)
    {
        list_del_init(&lock->list);
        destroy_client_lock(lock);
        count++;
    }

    /* FIXME: Need to actually print the locks instead of count */
    gf_msg_trace(this->name, 0, "Number of locks cleared=%d", count);
}

void
__delete_granted_locks_owner_from_fdctx(clnt_fd_ctx_t *fdctx,
                                        gf_lkowner_t *owner,
                                        struct list_head *deleted)
{
    client_posix_lock_t *lock = NULL;
    client_posix_lock_t *tmp = NULL;

    gf_boolean_t is_null_lkowner = _gf_false;

    if (is_lk_owner_null(owner)) {
        is_null_lkowner = _gf_true;
    }

    list_for_each_entry_safe(lock, tmp, &fdctx->lock_list, list)
    {
        if (is_null_lkowner || is_same_lkowner(&lock->owner, owner)) {
            list_del_init(&lock->list);
            list_add_tail(&lock->list, deleted);
        }
    }
}

int32_t
delete_granted_locks_owner(fd_t *fd, gf_lkowner_t *owner)
{
    clnt_fd_ctx_t *fdctx = NULL;
    xlator_t *this = NULL;
    clnt_conf_t *conf = NULL;
    int ret = 0;
    struct list_head deleted_locks;

    this = THIS;
    conf = this->private;
    INIT_LIST_HEAD(&deleted_locks);

    pthread_spin_lock(&conf->fd_lock);
    {
        fdctx = this_fd_get_ctx(fd, this);
        if (!fdctx) {
            pthread_spin_unlock(&conf->fd_lock);

            gf_smsg(this->name, GF_LOG_WARNING, EINVAL, PC_MSG_FD_CTX_INVALID,
                    NULL);
            ret = -1;
            goto out;
        }
        __delete_granted_locks_owner_from_fdctx(fdctx, owner, &deleted_locks);
    }
    pthread_spin_unlock(&conf->fd_lock);

    destroy_client_locks_from_list(&deleted_locks);
out:
    return ret;
}

int32_t
client_cmd_to_gf_cmd(int32_t cmd, int32_t *gf_cmd)
{
    int ret = 0;

    if (cmd == F_GETLK || cmd == F_GETLK64)
        *gf_cmd = GF_LK_GETLK;
    else if (cmd == F_SETLK || cmd == F_SETLK64)
        *gf_cmd = GF_LK_SETLK;
    else if (cmd == F_SETLKW || cmd == F_SETLKW64)
        *gf_cmd = GF_LK_SETLKW;
    else if (cmd == F_RESLK_LCK)
        *gf_cmd = GF_LK_RESLK_LCK;
    else if (cmd == F_RESLK_LCKW)
        *gf_cmd = GF_LK_RESLK_LCKW;
    else if (cmd == F_RESLK_UNLCK)
        *gf_cmd = GF_LK_RESLK_UNLCK;
    else if (cmd == F_GETLK_FD)
        *gf_cmd = GF_LK_GETLK_FD;
    else
        ret = -1;

    return ret;
}

static client_posix_lock_t *
new_client_lock(struct gf_flock *flock, gf_lkowner_t *owner, int32_t cmd,
                fd_t *fd)
{
    client_posix_lock_t *new_lock = NULL;

    new_lock = GF_CALLOC(1, sizeof(*new_lock), gf_client_mt_clnt_lock_t);
    if (!new_lock) {
        goto out;
    }

    INIT_LIST_HEAD(&new_lock->list);
    new_lock->fd = fd;
    memcpy(&new_lock->user_flock, flock, sizeof(struct gf_flock));

    new_lock->fl_type = flock->l_type;
    new_lock->fl_start = flock->l_start;

    if (flock->l_len == 0)
        new_lock->fl_end = LLONG_MAX;
    else
        new_lock->fl_end = flock->l_start + flock->l_len - 1;

    new_lock->owner = *owner;

    new_lock->cmd = cmd; /* Not really useful */

out:
    return new_lock;
}

void
client_save_number_fds(clnt_conf_t *conf, int count)
{
    LOCK(&conf->rec_lock);
    {
        conf->reopen_fd_count = count;
    }
    UNLOCK(&conf->rec_lock);
}

int
client_add_lock_for_recovery(fd_t *fd, struct gf_flock *flock,
                             gf_lkowner_t *owner, int32_t cmd)
{
    clnt_fd_ctx_t *fdctx = NULL;
    xlator_t *this = NULL;
    client_posix_lock_t *lock = NULL;
    clnt_conf_t *conf = NULL;

    int ret = 0;

    this = THIS;
    conf = this->private;

    pthread_spin_lock(&conf->fd_lock);

    fdctx = this_fd_get_ctx(fd, this);
    if (!fdctx) {
        pthread_spin_unlock(&conf->fd_lock);

        gf_smsg(this->name, GF_LOG_WARNING, 0, PC_MSG_FD_GET_FAIL, NULL);
        ret = -EBADFD;
        goto out;
    }

    lock = new_client_lock(flock, owner, cmd, fd);
    if (!lock) {
        pthread_spin_unlock(&conf->fd_lock);

        ret = -ENOMEM;
        goto out;
    }

    client_setlk(fdctx, lock);

    pthread_spin_unlock(&conf->fd_lock);

out:
    return ret;
}

int32_t
client_dump_locks(char *name, inode_t *inode, dict_t *dict)
{
    int ret = 0;
    dict_t *new_dict = NULL;
    char dict_string[256];

    GF_ASSERT(dict);
    new_dict = dict;

    ret = dump_client_locks(inode);
    snprintf(dict_string, 256, "%d locks dumped in log file", ret);

    ret = dict_set_dynstr(new_dict, CLIENT_DUMP_LOCKS, dict_string);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_WARNING, 0, PC_MSG_DICT_SET_FAIL, "lock=%s",
                CLIENT_DUMP_LOCKS, NULL);
        goto out;
    }

out:

    return ret;
}

int32_t
is_client_dump_locks_cmd(char *name)
{
    int ret = 0;

    if (strcmp(name, CLIENT_DUMP_LOCKS) == 0)
        ret = 1;

    return ret;
}
