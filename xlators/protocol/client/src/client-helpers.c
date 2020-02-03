/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "client.h"
#include <glusterfs/fd.h>
#include "client-messages.h"
#include "client-common.h"
#include <glusterfs/compat-errno.h>
#include <glusterfs/common-utils.h>

int
client_fd_lk_list_empty(fd_lk_ctx_t *lk_ctx, gf_boolean_t try_lock)
{
    int ret = 1;

    if (!lk_ctx) {
        ret = -1;
        goto out;
    }

    if (try_lock) {
        ret = TRY_LOCK(&lk_ctx->lock);
        if (ret != 0) {
            ret = -1;
            goto out;
        }
    } else {
        LOCK(&lk_ctx->lock);
    }

    ret = list_empty(&lk_ctx->lk_list);
    UNLOCK(&lk_ctx->lock);
out:
    return ret;
}

clnt_fd_ctx_t *
this_fd_del_ctx(fd_t *file, xlator_t *this)
{
    int dict_ret = -1;
    uint64_t ctxaddr = 0;

    GF_VALIDATE_OR_GOTO("client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, file, out);

    dict_ret = fd_ctx_del(file, this, &ctxaddr);

    if (dict_ret < 0) {
        ctxaddr = 0;
    }

out:
    return (clnt_fd_ctx_t *)(unsigned long)ctxaddr;
}

clnt_fd_ctx_t *
this_fd_get_ctx(fd_t *file, xlator_t *this)
{
    int dict_ret = -1;
    uint64_t ctxaddr = 0;

    GF_VALIDATE_OR_GOTO("client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, file, out);

    dict_ret = fd_ctx_get(file, this, &ctxaddr);

    if (dict_ret < 0) {
        ctxaddr = 0;
    }

out:
    return (clnt_fd_ctx_t *)(unsigned long)ctxaddr;
}

void
this_fd_set_ctx(fd_t *file, xlator_t *this, loc_t *loc, clnt_fd_ctx_t *ctx)
{
    uint64_t oldaddr = 0;
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, file, out);

    ret = fd_ctx_get(file, this, &oldaddr);
    if (ret >= 0) {
        if (loc)
            gf_smsg(this->name, GF_LOG_INFO, 0, PC_MSG_FD_DUPLICATE_TRY,
                    "path=%s", loc->path, "gfid=%s",
                    uuid_utoa(loc->inode->gfid), NULL);
        else
            gf_smsg(this->name, GF_LOG_INFO, 0, PC_MSG_FD_DUPLICATE_TRY,
                    "file=%p", file, NULL);
    }

    ret = fd_ctx_set(file, this, (uint64_t)(unsigned long)ctx);
    if (ret < 0) {
        if (loc)
            gf_smsg(this->name, GF_LOG_WARNING, 0, PC_MSG_FD_SET_FAIL,
                    "path=%s", loc->path, "gfid=%s",
                    uuid_utoa(loc->inode->gfid), NULL);
        else
            gf_smsg(this->name, GF_LOG_WARNING, 0, PC_MSG_FD_SET_FAIL,
                    "file=%p", file, NULL);
    }
out:
    return;
}

int
client_local_wipe(clnt_local_t *local)
{
    if (local) {
        loc_wipe(&local->loc);
        loc_wipe(&local->loc2);

        if (local->fd) {
            fd_unref(local->fd);
        }

        if (local->iobref) {
            iobref_unref(local->iobref);
        }

        GF_FREE(local->name);
        mem_put(local);
    }

    return 0;
}
int
unserialize_rsp_dirent(xlator_t *this, struct gfs3_readdir_rsp *rsp,
                       gf_dirent_t *entries)
{
    struct gfs3_dirlist *trav = NULL;
    gf_dirent_t *entry = NULL;
    int entry_len = 0;
    int ret = -1;
    clnt_conf_t *conf = NULL;

    conf = this->private;

    trav = rsp->reply;
    while (trav) {
        entry_len = gf_dirent_size(trav->name);
        entry = GF_CALLOC(1, entry_len, gf_common_mt_gf_dirent_t);
        if (!entry)
            goto out;

        entry->d_ino = trav->d_ino;
        gf_itransform(this, trav->d_off, &entry->d_off, conf->client_id);
        entry->d_len = trav->d_len;
        entry->d_type = trav->d_type;

        strcpy(entry->d_name, trav->name);

        list_add_tail(&entry->list, &entries->list);

        trav = trav->nextentry;
    }

    ret = 0;
out:
    return ret;
}

int
unserialize_rsp_direntp(xlator_t *this, fd_t *fd, struct gfs3_readdirp_rsp *rsp,
                        gf_dirent_t *entries)
{
    struct gfs3_dirplist *trav = NULL;
    gf_dirent_t *entry = NULL;
    inode_table_t *itable = NULL;
    int entry_len = 0;
    int ret = -1;
    clnt_conf_t *conf = NULL;

    trav = rsp->reply;

    if (fd)
        itable = fd->inode->table;

    conf = this->private;
    if (!conf)
        goto out;

    while (trav) {
        entry_len = gf_dirent_size(trav->name);
        entry = GF_CALLOC(1, entry_len, gf_common_mt_gf_dirent_t);
        if (!entry)
            goto out;

        entry->d_ino = trav->d_ino;
        gf_itransform(this, trav->d_off, &entry->d_off, conf->client_id);
        entry->d_len = trav->d_len;
        entry->d_type = trav->d_type;

        gf_stat_to_iatt(&trav->stat, &entry->d_stat);

        strcpy(entry->d_name, trav->name);

        if (trav->dict.dict_val) {
            entry->dict = dict_new();
            if (!entry->dict)
                goto out;

            ret = dict_unserialize(trav->dict.dict_val, trav->dict.dict_len,
                                   &entry->dict);
            if (ret < 0) {
                gf_smsg(THIS->name, GF_LOG_WARNING, EINVAL,
                        PC_MSG_DICT_UNSERIALIZE_FAIL, "xattr", NULL);
                goto out;
            }
        }

        entry->inode = inode_find(itable, entry->d_stat.ia_gfid);
        if (!entry->inode)
            entry->inode = inode_new(itable);

        list_add_tail(&entry->list, &entries->list);

        trav = trav->nextentry;
        entry = NULL;
    }

    ret = 0;
out:
    if (entry)
        gf_dirent_entry_free(entry);
    return ret;
}

int
clnt_readdirp_rsp_cleanup(gfs3_readdirp_rsp *rsp)
{
    gfs3_dirplist *prev = NULL;
    gfs3_dirplist *trav = NULL;

    trav = rsp->reply;
    prev = trav;
    while (trav) {
        trav = trav->nextentry;
        /* on client, the rpc lib allocates this */
        free(prev->dict.dict_val);
        free(prev->name);
        free(prev);
        prev = trav;
    }

    return 0;
}

int
unserialize_rsp_dirent_v2(xlator_t *this, struct gfx_readdir_rsp *rsp,
                          gf_dirent_t *entries)
{
    struct gfx_dirlist *trav = NULL;
    gf_dirent_t *entry = NULL;
    int entry_len = 0;
    int ret = -1;
    clnt_conf_t *conf = NULL;

    conf = this->private;

    trav = rsp->reply;
    while (trav) {
        entry_len = gf_dirent_size(trav->name);
        entry = GF_CALLOC(1, entry_len, gf_common_mt_gf_dirent_t);
        if (!entry)
            goto out;

        entry->d_ino = trav->d_ino;
        gf_itransform(this, trav->d_off, &entry->d_off, conf->client_id);
        entry->d_len = trav->d_len;
        entry->d_type = trav->d_type;

        strcpy(entry->d_name, trav->name);

        list_add_tail(&entry->list, &entries->list);

        trav = trav->nextentry;
    }

    ret = 0;
out:
    return ret;
}

int
unserialize_rsp_direntp_v2(xlator_t *this, fd_t *fd,
                           struct gfx_readdirp_rsp *rsp, gf_dirent_t *entries)
{
    struct gfx_dirplist *trav = NULL;
    gf_dirent_t *entry = NULL;
    inode_table_t *itable = NULL;
    int entry_len = 0;
    int ret = -1;
    clnt_conf_t *conf = NULL;

    trav = rsp->reply;

    if (fd)
        itable = fd->inode->table;

    conf = this->private;
    if (!conf)
        goto out;

    while (trav) {
        entry_len = gf_dirent_size(trav->name);
        entry = GF_CALLOC(1, entry_len, gf_common_mt_gf_dirent_t);
        if (!entry)
            goto out;

        entry->d_ino = trav->d_ino;
        gf_itransform(this, trav->d_off, &entry->d_off, conf->client_id);
        entry->d_len = trav->d_len;
        entry->d_type = trav->d_type;

        gfx_stat_to_iattx(&trav->stat, &entry->d_stat);

        strcpy(entry->d_name, trav->name);

        xdr_to_dict(&trav->dict, &entry->dict);

        entry->inode = inode_find(itable, entry->d_stat.ia_gfid);
        if (!entry->inode)
            entry->inode = inode_new(itable);

        list_add_tail(&entry->list, &entries->list);

        trav = trav->nextentry;
    }

    ret = 0;
out:
    return ret;
}

int
clnt_readdirp_rsp_cleanup_v2(gfx_readdirp_rsp *rsp)
{
    gfx_dirplist *prev = NULL;
    gfx_dirplist *trav = NULL;

    trav = rsp->reply;
    prev = trav;
    while (trav) {
        trav = trav->nextentry;
        free(prev->name);
        free(prev);
        prev = trav;
    }

    return 0;
}

int
clnt_readdir_rsp_cleanup(gfs3_readdir_rsp *rsp)
{
    gfs3_dirlist *prev = NULL;
    gfs3_dirlist *trav = NULL;

    trav = rsp->reply;
    prev = trav;
    while (trav) {
        trav = trav->nextentry;
        /* on client, the rpc lib allocates this */
        free(prev->name);
        free(prev);
        prev = trav;
    }

    return 0;
}

int
clnt_readdir_rsp_cleanup_v2(gfx_readdir_rsp *rsp)
{
    gfx_dirlist *prev = NULL;
    gfx_dirlist *trav = NULL;

    trav = rsp->reply;
    prev = trav;
    while (trav) {
        trav = trav->nextentry;
        /* on client, the rpc lib allocates this */
        free(prev->name);
        free(prev);
        prev = trav;
    }

    return 0;
}

int
client_get_remote_fd(xlator_t *this, fd_t *fd, int flags, int64_t *remote_fd)
{
    clnt_fd_ctx_t *fdctx = NULL;
    clnt_conf_t *conf = NULL;
    gf_boolean_t locks_held = _gf_false;

    GF_VALIDATE_OR_GOTO(this->name, fd, out);
    GF_VALIDATE_OR_GOTO(this->name, remote_fd, out);

    conf = this->private;
    pthread_spin_lock(&conf->fd_lock);
    {
        fdctx = this_fd_get_ctx(fd, this);
        if (!fdctx) {
            if (fd->anonymous) {
                *remote_fd = GF_ANON_FD_NO;
            } else {
                *remote_fd = -1;
                gf_msg_debug(this->name, EBADF, "not a valid fd for gfid: %s",
                             uuid_utoa(fd->inode->gfid));
            }
        } else {
            if (__is_fd_reopen_in_progress(fdctx))
                *remote_fd = -1;
            else
                *remote_fd = fdctx->remote_fd;

            locks_held = !list_empty(&fdctx->lock_list);
        }
    }
    pthread_spin_unlock(&conf->fd_lock);

    if ((flags & FALLBACK_TO_ANON_FD) && (*remote_fd == -1) && (!locks_held))
        *remote_fd = GF_ANON_FD_NO;

    return 0;
out:
    return -1;
}

gf_boolean_t
client_is_reopen_needed(fd_t *fd, xlator_t *this, int64_t remote_fd)
{
    clnt_conf_t *conf = NULL;
    clnt_fd_ctx_t *fdctx = NULL;
    gf_boolean_t res = _gf_false;

    conf = this->private;
    pthread_spin_lock(&conf->fd_lock);
    {
        fdctx = this_fd_get_ctx(fd, this);
        if (fdctx && (fdctx->remote_fd == -1) && (remote_fd == GF_ANON_FD_NO))
            res = _gf_true;
    }
    pthread_spin_unlock(&conf->fd_lock);

    return res;
}

int
client_fd_fop_prepare_local(call_frame_t *frame, fd_t *fd, int64_t remote_fd)
{
    xlator_t *this = NULL;
    clnt_local_t *local = NULL;
    int ret = 0;

    if (!frame || !fd) {
        ret = -EINVAL;
        goto out;
    }

    this = frame->this;

    frame->local = mem_get0(this->local_pool);
    if (frame->local == NULL) {
        ret = -ENOMEM;
        goto out;
    }

    local = frame->local;
    local->fd = fd_ref(fd);
    local->attempt_reopen = client_is_reopen_needed(fd, this, remote_fd);

    return 0;
out:
    return ret;
}

void
clnt_getactivelk_rsp_cleanup(gfs3_getactivelk_rsp *rsp)
{
    gfs3_locklist *trav = NULL;
    gfs3_locklist *next = NULL;

    trav = rsp->reply;

    while (trav) {
        next = trav->nextentry;
        free(trav->client_uid);
        free(trav);
        trav = next;
    }
}

void
clnt_getactivelk_rsp_cleanup_v2(gfx_getactivelk_rsp *rsp)
{
    gfs3_locklist *trav = NULL;
    gfs3_locklist *next = NULL;

    trav = rsp->reply;

    while (trav) {
        next = trav->nextentry;
        free(trav->client_uid);
        free(trav);
        trav = next;
    }
}
int
clnt_unserialize_rsp_locklist(xlator_t *this, struct gfs3_getactivelk_rsp *rsp,
                              lock_migration_info_t *lmi)
{
    struct gfs3_locklist *trav = NULL;
    lock_migration_info_t *temp = NULL;
    int ret = -1;
    clnt_conf_t *conf = NULL;

    trav = rsp->reply;

    conf = this->private;
    if (!conf)
        goto out;

    while (trav) {
        temp = GF_CALLOC(1, sizeof(*lmi), gf_common_mt_lock_mig);
        if (temp == NULL) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_NO_MEM, NULL);
            goto out;
        }

        INIT_LIST_HEAD(&temp->list);

        gf_proto_flock_to_flock(&trav->flock, &temp->flock);

        temp->lk_flags = trav->lk_flags;

        temp->client_uid = gf_strdup(trav->client_uid);

        list_add_tail(&temp->list, &lmi->list);

        trav = trav->nextentry;
    }

    ret = 0;
out:
    return ret;
}
int
clnt_unserialize_rsp_locklist_v2(xlator_t *this,
                                 struct gfx_getactivelk_rsp *rsp,
                                 lock_migration_info_t *lmi)
{
    struct gfs3_locklist *trav = NULL;
    lock_migration_info_t *temp = NULL;
    int ret = -1;
    clnt_conf_t *conf = NULL;

    trav = rsp->reply;

    conf = this->private;
    if (!conf)
        goto out;

    while (trav) {
        temp = GF_CALLOC(1, sizeof(*lmi), gf_common_mt_lock_mig);
        if (temp == NULL) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, PC_MSG_NO_MEM, NULL);
            goto out;
        }

        INIT_LIST_HEAD(&temp->list);

        gf_proto_flock_to_flock(&trav->flock, &temp->flock);

        temp->lk_flags = trav->lk_flags;

        temp->client_uid = gf_strdup(trav->client_uid);

        list_add_tail(&temp->list, &lmi->list);

        trav = trav->nextentry;
    }

    ret = 0;
out:
    return ret;
}

void
clnt_setactivelk_req_cleanup(gfs3_setactivelk_req *req)
{
    gfs3_locklist *trav = NULL;
    gfs3_locklist *next = NULL;

    trav = req->request;

    while (trav) {
        next = trav->nextentry;
        GF_FREE(trav->client_uid);
        GF_FREE(trav);
        trav = next;
    }
}

void
clnt_setactivelk_req_cleanup_v2(gfx_setactivelk_req *req)
{
    gfs3_locklist *trav = NULL;
    gfs3_locklist *next = NULL;

    trav = req->request;

    while (trav) {
        next = trav->nextentry;
        GF_FREE(trav->client_uid);
        GF_FREE(trav);
        trav = next;
    }
}

int
serialize_req_locklist(lock_migration_info_t *locklist,
                       gfs3_setactivelk_req *req)
{
    lock_migration_info_t *tmp = NULL;
    gfs3_locklist *trav = NULL;
    gfs3_locklist *prev = NULL;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("server", locklist, out);
    GF_VALIDATE_OR_GOTO("server", req, out);

    list_for_each_entry(tmp, &locklist->list, list)
    {
        trav = GF_CALLOC(1, sizeof(*trav), gf_client_mt_clnt_lock_request_t);
        if (!trav)
            goto out;

        switch (tmp->flock.l_type) {
            case F_RDLCK:
                tmp->flock.l_type = GF_LK_F_RDLCK;
                break;
            case F_WRLCK:
                tmp->flock.l_type = GF_LK_F_WRLCK;
                break;
            case F_UNLCK:
                tmp->flock.l_type = GF_LK_F_UNLCK;
                break;

            default:
                gf_smsg(THIS->name, GF_LOG_ERROR, 0, PC_MSG_UNKNOWN_LOCK_TYPE,
                        "type=%" PRId32, tmp->flock.l_type, NULL);
                break;
        }

        gf_proto_flock_from_flock(&trav->flock, &tmp->flock);

        trav->lk_flags = tmp->lk_flags;

        trav->client_uid = gf_strdup(tmp->client_uid);
        if (!trav->client_uid) {
            gf_smsg(THIS->name, GF_LOG_ERROR, 0, PC_MSG_CLIENT_UID_ALLOC_FAILED,
                    NULL);
            ret = -1;
            goto out;
        }

        if (prev)
            prev->nextentry = trav;
        else
            req->request = trav;

        prev = trav;
        trav = NULL;
    }

    ret = 0;
out:
    GF_FREE(trav);

    return ret;
}

int
serialize_req_locklist_v2(lock_migration_info_t *locklist,
                          gfx_setactivelk_req *req)
{
    lock_migration_info_t *tmp = NULL;
    gfs3_locklist *trav = NULL;
    gfs3_locklist *prev = NULL;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("server", locklist, out);
    GF_VALIDATE_OR_GOTO("server", req, out);

    list_for_each_entry(tmp, &locklist->list, list)
    {
        trav = GF_CALLOC(1, sizeof(*trav), gf_client_mt_clnt_lock_request_t);
        if (!trav)
            goto out;

        switch (tmp->flock.l_type) {
            case F_RDLCK:
                tmp->flock.l_type = GF_LK_F_RDLCK;
                break;
            case F_WRLCK:
                tmp->flock.l_type = GF_LK_F_WRLCK;
                break;
            case F_UNLCK:
                tmp->flock.l_type = GF_LK_F_UNLCK;
                break;

            default:
                gf_smsg(THIS->name, GF_LOG_ERROR, 0, PC_MSG_UNKNOWN_LOCK_TYPE,
                        "type=%" PRId32, tmp->flock.l_type, NULL);
                break;
        }

        gf_proto_flock_from_flock(&trav->flock, &tmp->flock);

        trav->lk_flags = tmp->lk_flags;

        trav->client_uid = gf_strdup(tmp->client_uid);
        if (!trav->client_uid) {
            gf_smsg(THIS->name, GF_LOG_ERROR, 0, PC_MSG_CLIENT_UID_ALLOC_FAILED,
                    NULL);
            ret = -1;
            goto out;
        }

        if (prev)
            prev->nextentry = trav;
        else
            req->request = trav;

        prev = trav;
        trav = NULL;
    }

    ret = 0;
out:
    GF_FREE(trav);

    return ret;
}

extern int
client3_3_releasedir_cbk(struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe);
extern int
client3_3_release_cbk(struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe);
extern int
client4_0_releasedir_cbk(struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe);
extern int
client4_0_release_cbk(struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe);

static int
send_release4_0_over_wire(xlator_t *this, clnt_fd_ctx_t *fdctx,
                          call_frame_t *fr)
{
    clnt_conf_t *conf = NULL;
    conf = (clnt_conf_t *)this->private;
    if (fdctx->is_dir) {
        gfx_releasedir_req req = {
            {
                0,
            },
        };
        memcpy(req.gfid, fdctx->gfid, 16);
        req.fd = fdctx->remote_fd;

        gf_msg_trace(this->name, 0, "sending releasedir on fd");
        (void)client_submit_request(
            this, &req, fr, conf->fops, GFS3_OP_RELEASEDIR,
            client4_0_releasedir_cbk, NULL, (xdrproc_t)xdr_gfx_releasedir_req);
    } else {
        gfx_release_req req = {
            {
                0,
            },
        };
        memcpy(req.gfid, fdctx->gfid, 16);
        req.fd = fdctx->remote_fd;
        gf_msg_trace(this->name, 0, "sending release on fd");
        (void)client_submit_request(this, &req, fr, conf->fops, GFS3_OP_RELEASE,
                                    client4_0_release_cbk, NULL,
                                    (xdrproc_t)xdr_gfx_release_req);
    }

    return 0;
}

static int
send_release3_3_over_wire(xlator_t *this, clnt_fd_ctx_t *fdctx,
                          call_frame_t *fr)
{
    clnt_conf_t *conf = NULL;
    conf = (clnt_conf_t *)this->private;
    if (fdctx->is_dir) {
        gfs3_releasedir_req req = {
            {
                0,
            },
        };
        memcpy(req.gfid, fdctx->gfid, 16);
        req.fd = fdctx->remote_fd;
        gf_msg_trace(this->name, 0, "sending releasedir on fd");
        (void)client_submit_request(
            this, &req, fr, conf->fops, GFS3_OP_RELEASEDIR,
            client3_3_releasedir_cbk, NULL, (xdrproc_t)xdr_gfs3_releasedir_req);
    } else {
        gfs3_release_req req = {
            {
                0,
            },
        };
        memcpy(req.gfid, fdctx->gfid, 16);
        req.fd = fdctx->remote_fd;
        gf_msg_trace(this->name, 0, "sending release on fd");
        (void)client_submit_request(this, &req, fr, conf->fops, GFS3_OP_RELEASE,
                                    client3_3_release_cbk, NULL,
                                    (xdrproc_t)xdr_gfs3_release_req);
    }

    return 0;
}

int
client_fdctx_destroy(xlator_t *this, clnt_fd_ctx_t *fdctx)
{
    clnt_conf_t *conf = NULL;
    call_frame_t *fr = NULL;
    int32_t ret = -1;
    char parent_down = 0;
    fd_lk_ctx_t *lk_ctx = NULL;

    GF_VALIDATE_OR_GOTO("client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fdctx, out);

    conf = (clnt_conf_t *)this->private;

    if (fdctx->remote_fd == -1) {
        gf_msg_debug(this->name, 0, "not a valid fd");
        goto out;
    }

    pthread_mutex_lock(&conf->lock);
    {
        parent_down = conf->parent_down;
    }
    pthread_mutex_unlock(&conf->lock);
    lk_ctx = fdctx->lk_ctx;
    fdctx->lk_ctx = NULL;

    if (lk_ctx)
        fd_lk_ctx_unref(lk_ctx);

    if (!parent_down)
        rpc_clnt_ref(conf->rpc);
    else
        goto out;

    fr = create_frame(this, this->ctx->pool);
    if (fr == NULL) {
        goto out;
    }

    ret = 0;

    if (conf->fops->progver == GLUSTER_FOP_VERSION)
        send_release3_3_over_wire(this, fdctx, fr);
    else
        send_release4_0_over_wire(this, fdctx, fr);

    rpc_clnt_unref(conf->rpc);
out:
    if (fdctx) {
        fdctx->remote_fd = -1;
        GF_FREE(fdctx);
    }

    return ret;
}
