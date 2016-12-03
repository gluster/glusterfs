/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "client.h"
#include "fd.h"
#include "client-messages.h"
#include "client-common.h"
#include "compat-errno.h"
#include "common-utils.h"

int
client_fd_lk_list_empty (fd_lk_ctx_t *lk_ctx, gf_boolean_t try_lock)
{
        int  ret = 1;

        if (!lk_ctx) {
                ret = -1;
                goto out;
        }

        if (try_lock) {
                ret = TRY_LOCK (&lk_ctx->lock);
                if (ret != 0) {
                        ret = -1;
                        goto out;
                }
        } else {
                LOCK (&lk_ctx->lock);
        }

        ret = list_empty (&lk_ctx->lk_list);
        UNLOCK (&lk_ctx->lock);
out:
        return ret;
}

clnt_fd_ctx_t *
this_fd_del_ctx (fd_t *file, xlator_t *this)
{
        int         dict_ret = -1;
        uint64_t    ctxaddr  = 0;

        GF_VALIDATE_OR_GOTO ("client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, file, out);

        dict_ret = fd_ctx_del (file, this, &ctxaddr);

        if (dict_ret < 0) {
                ctxaddr = 0;
        }

out:
        return (clnt_fd_ctx_t *)(unsigned long)ctxaddr;
}


clnt_fd_ctx_t *
this_fd_get_ctx (fd_t *file, xlator_t *this)
{
        int         dict_ret = -1;
        uint64_t    ctxaddr = 0;

        GF_VALIDATE_OR_GOTO ("client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, file, out);

        dict_ret = fd_ctx_get (file, this, &ctxaddr);

        if (dict_ret < 0) {
                ctxaddr = 0;
        }

out:
        return (clnt_fd_ctx_t *)(unsigned long)ctxaddr;
}


void
this_fd_set_ctx (fd_t *file, xlator_t *this, loc_t *loc, clnt_fd_ctx_t *ctx)
{
        uint64_t oldaddr = 0;
        int32_t  ret = -1;

        GF_VALIDATE_OR_GOTO ("client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, file, out);

        ret = fd_ctx_get (file, this, &oldaddr);
        if (ret >= 0) {
                if (loc)
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                PC_MSG_FD_DUPLICATE_TRY,
                                "%s (%s): trying duplicate remote fd set. ",
                                loc->path, uuid_utoa (loc->inode->gfid));
                else
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                PC_MSG_FD_DUPLICATE_TRY,
                                "%p: trying duplicate remote fd set. ", file);
        }

        ret = fd_ctx_set (file, this, (uint64_t)(unsigned long)ctx);
        if (ret < 0) {
                if (loc)
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                PC_MSG_FD_SET_FAIL,
                                "%s (%s): failed to set remote fd",
                                loc->path, uuid_utoa (loc->inode->gfid));
                else
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                PC_MSG_FD_SET_FAIL,
                                "%p: failed to set remote fd", file);
        }
out:
        return;
}


int
client_local_wipe (clnt_local_t *local)
{
        if (local) {
                loc_wipe (&local->loc);
                loc_wipe (&local->loc2);

                if (local->fd) {
                        fd_unref (local->fd);
                }

                if (local->iobref) {
                        iobref_unref (local->iobref);
                }

                GF_FREE (local->name);
                local->compound_args = NULL;
                mem_put (local);
        }

        return 0;
}

int
unserialize_rsp_dirent (xlator_t *this, struct gfs3_readdir_rsp *rsp,
                        gf_dirent_t *entries)
{
        struct gfs3_dirlist  *trav      = NULL;
	gf_dirent_t          *entry     = NULL;
        int                   entry_len = 0;
        int                   ret       = -1;
        clnt_conf_t          *conf = NULL;

        conf = this->private;

        trav = rsp->reply;
        while (trav) {
                entry_len = gf_dirent_size (trav->name);
                entry = GF_CALLOC (1, entry_len, gf_common_mt_gf_dirent_t);
                if (!entry)
                        goto out;

                entry->d_ino  = trav->d_ino;
                gf_itransform (this, trav->d_off, &entry->d_off,
                                      conf->client_id);
                entry->d_len  = trav->d_len;
                entry->d_type = trav->d_type;

                strcpy (entry->d_name, trav->name);

		list_add_tail (&entry->list, &entries->list);

                trav = trav->nextentry;
        }

        ret = 0;
out:
        return ret;
}

int
unserialize_rsp_direntp (xlator_t *this, fd_t *fd,
                         struct gfs3_readdirp_rsp *rsp, gf_dirent_t *entries)
{
        struct gfs3_dirplist *trav      = NULL;
        char                 *buf       = NULL;
	gf_dirent_t          *entry     = NULL;
        inode_table_t        *itable    = NULL;
        int                   entry_len = 0;
        int                   ret       = -1;
        clnt_conf_t          *conf      = NULL;

        trav = rsp->reply;

        if (fd)
                itable = fd->inode->table;

        conf = this->private;
        if (!conf)
                goto out;

        while (trav) {
                entry_len = gf_dirent_size (trav->name);
                entry = GF_CALLOC (1, entry_len, gf_common_mt_gf_dirent_t);
                if (!entry)
                        goto out;

                entry->d_ino  = trav->d_ino;
                gf_itransform (this, trav->d_off, &entry->d_off,
                                      conf->client_id);
                entry->d_len  = trav->d_len;
                entry->d_type = trav->d_type;

                gf_stat_to_iatt (&trav->stat, &entry->d_stat);

                strcpy (entry->d_name, trav->name);

                if (trav->dict.dict_val) {
                        /* Dictionary is sent along with response */
                        buf = memdup (trav->dict.dict_val, trav->dict.dict_len);
                        if (!buf)
                                goto out;

                        entry->dict = dict_new ();

                        ret = dict_unserialize (buf, trav->dict.dict_len,
                                                &entry->dict);
                        if (ret < 0) {
                                gf_msg (THIS->name, GF_LOG_WARNING, EINVAL,
                                        PC_MSG_DICT_UNSERIALIZE_FAIL,
                                        "failed to unserialize xattr dict");
                                goto out;
                        }
                        GF_FREE (buf);
                        buf = NULL;
                }

                entry->inode = inode_find (itable, entry->d_stat.ia_gfid);
                if (!entry->inode)
                        entry->inode = inode_new (itable);

		list_add_tail (&entry->list, &entries->list);

                trav = trav->nextentry;
        }

        ret = 0;
out:
        return ret;
}

int
clnt_readdirp_rsp_cleanup (gfs3_readdirp_rsp *rsp)
{
        gfs3_dirplist *prev = NULL;
        gfs3_dirplist *trav = NULL;

        trav = rsp->reply;
        prev = trav;
        while (trav) {
                trav = trav->nextentry;
                /* on client, the rpc lib allocates this */
                free (prev->dict.dict_val);
                free (prev->name);
                free (prev);
                prev = trav;
        }

        return 0;
}

int
clnt_readdir_rsp_cleanup (gfs3_readdir_rsp *rsp)
{
        gfs3_dirlist *prev = NULL;
        gfs3_dirlist *trav = NULL;

        trav = rsp->reply;
        prev = trav;
        while (trav) {
                trav = trav->nextentry;
                /* on client, the rpc lib allocates this */
                free (prev->name);
                free (prev);
                prev = trav;
        }

        return 0;
}

int
client_get_remote_fd (xlator_t *this, fd_t *fd, int flags, int64_t *remote_fd)
{
        clnt_fd_ctx_t *fdctx    = NULL;
        clnt_conf_t   *conf     = NULL;

        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, remote_fd, out);

        conf = this->private;
        pthread_mutex_lock (&conf->lock);
        {
                fdctx = this_fd_get_ctx (fd, this);
                if (!fdctx)
                        *remote_fd = GF_ANON_FD_NO;
                else if (__is_fd_reopen_in_progress (fdctx))
                        *remote_fd = -1;
                else
                        *remote_fd = fdctx->remote_fd;
        }
        pthread_mutex_unlock (&conf->lock);

        if ((flags & FALLBACK_TO_ANON_FD) && (*remote_fd == -1))
                *remote_fd = GF_ANON_FD_NO;

        return 0;
out:
        return -1;
}

gf_boolean_t
client_is_reopen_needed (fd_t *fd, xlator_t *this, int64_t remote_fd)
{
        clnt_fd_ctx_t   *fdctx = NULL;

        fdctx = this_fd_get_ctx (fd, this);
        if (fdctx && (fdctx->remote_fd == -1) &&
            (remote_fd == GF_ANON_FD_NO))
                return _gf_true;
        return _gf_false;
}

int
client_fd_fop_prepare_local (call_frame_t *frame, fd_t *fd, int64_t remote_fd)
{
        xlator_t     *this  = NULL;
        clnt_local_t *local = NULL;
        int          ret    = 0;

        this = frame->this;

        if (!frame || !fd) {
                ret = -EINVAL;
                goto out;
        }

        frame->local = mem_get0 (this->local_pool);
        if (frame->local == NULL) {
                ret = -ENOMEM;
                goto out;
        }

        local = frame->local;
        local->fd = fd_ref (fd);
        local->attempt_reopen = client_is_reopen_needed (fd, this, remote_fd);

        return 0;
out:
        return ret;
}

int
client_process_response (call_frame_t *frame, xlator_t *this,
                         struct rpc_req *req, gfs3_compound_rsp *rsp,
                         compound_args_cbk_t *args_cbk,
                         int index)
{
        int                      ret            = 0;
        dict_t                  *xdata          = NULL;
        dict_t                  *xattr          = NULL;
        struct iovec             vector[MAX_IOVEC] = {{0}, };
        gf_dirent_t              entries;
        default_args_cbk_t      *this_args_cbk  = &args_cbk->rsp_list[index];
        clnt_local_t            *local          = frame->local;
        compound_rsp            *this_rsp       = NULL;
        compound_args_t         *args           = local->compound_args;

        this_rsp = &rsp->compound_rsp_array.compound_rsp_array_val[index];
        args_cbk->enum_list[index] = this_rsp->fop_enum;

        INIT_LIST_HEAD (&entries.list);

        switch (args_cbk->enum_list[index]) {

        case GF_FOP_STAT:
        {
                gfs3_stat_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_stat_rsp;

                client_post_stat (this, tmp_rsp, &this_args_cbk->stat, &xdata);

                CLIENT_POST_FOP_TYPE (stat, this_rsp, this_args_cbk,
                                      &this_args_cbk->stat, xdata);
                break;
        }
        case GF_FOP_READLINK:
        {
                gfs3_readlink_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_readlink_rsp;

                client_post_readlink (this, tmp_rsp, &this_args_cbk->stat,
                                      &xdata);
                CLIENT_POST_FOP_TYPE (readlink, this_rsp, this_args_cbk,
                                      tmp_rsp->path, &this_args_cbk->stat,
                                      xdata);
                break;
        }
        case GF_FOP_MKNOD:
        {
                gfs3_mknod_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_mknod_rsp;

                client_post_mknod (this, tmp_rsp, &this_args_cbk->stat,
                                   &this_args_cbk->preparent,
                                   &this_args_cbk->postparent, &xdata);
                CLIENT_POST_FOP_TYPE (mknod, this_rsp, this_args_cbk,
                                      local->loc.inode, &this_args_cbk->stat,
                                      &this_args_cbk->preparent,
                                      &this_args_cbk->postparent, xdata);
                break;
        }
        case GF_FOP_MKDIR:
        {
                gfs3_mkdir_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_mkdir_rsp;

                client_post_mkdir (this, tmp_rsp, &this_args_cbk->stat,
                                   &this_args_cbk->preparent,
                                   &this_args_cbk->postparent, &xdata);
                CLIENT_POST_FOP_TYPE (mkdir, this_rsp, this_args_cbk,
                                      local->loc.inode, &this_args_cbk->stat,
                                      &this_args_cbk->preparent,
                                      &this_args_cbk->postparent, xdata);
                break;
        }
        case GF_FOP_UNLINK:
        {
                gfs3_unlink_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_unlink_rsp;

                client_post_unlink (this, tmp_rsp, &this_args_cbk->preparent,
                                    &this_args_cbk->postparent, &xdata);
                CLIENT_POST_FOP_TYPE (unlink, this_rsp, this_args_cbk,
                                      &this_args_cbk->preparent,
                                      &this_args_cbk->postparent, xdata);
                break;
        }
        case GF_FOP_RMDIR:
        {
                gfs3_rmdir_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_rmdir_rsp;

                client_post_rmdir (this, tmp_rsp, &this_args_cbk->preparent,
                                   &this_args_cbk->postparent, &xdata);
                CLIENT_POST_FOP_TYPE (rmdir, this_rsp, this_args_cbk,
                                      &this_args_cbk->preparent,
                                      &this_args_cbk->postparent, xdata);
                break;
        }
        case GF_FOP_SYMLINK:
        {
                gfs3_symlink_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_symlink_rsp;

                client_post_symlink (this, tmp_rsp, &this_args_cbk->stat,
                                     &this_args_cbk->preparent,
                                     &this_args_cbk->postparent, &xdata);
                CLIENT_POST_FOP_TYPE (symlink, this_rsp, this_args_cbk, NULL,
                                      &this_args_cbk->stat,
                                      &this_args_cbk->preparent,
                                      &this_args_cbk->postparent, xdata);
                break;
        }
        case GF_FOP_RENAME:
        {
                gfs3_rename_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_rename_rsp;

                client_post_rename (this, tmp_rsp, &this_args_cbk->stat,
                                    &this_args_cbk->preparent,
                                    &this_args_cbk->postparent,
                                    &this_args_cbk->preparent2,
                                    &this_args_cbk->postparent2, &xdata);
                CLIENT_POST_FOP_TYPE (rename, this_rsp, this_args_cbk,
                                      &this_args_cbk->stat,
                                      &this_args_cbk->preparent,
                                      &this_args_cbk->postparent,
                                      &this_args_cbk->preparent2,
                                      &this_args_cbk->postparent2, xdata);
                break;
        }
        case GF_FOP_LINK:
        {
                gfs3_link_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_link_rsp;

                client_post_link (this, tmp_rsp, &this_args_cbk->stat,
                                  &this_args_cbk->preparent,
                                  &this_args_cbk->postparent, &xdata);
                CLIENT_POST_FOP_TYPE (link, this_rsp, this_args_cbk, NULL,
                                      &this_args_cbk->stat,
                                      &this_args_cbk->preparent,
                                      &this_args_cbk->postparent, xdata);
                break;
        }
        case GF_FOP_TRUNCATE:
        {
                gfs3_truncate_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_truncate_rsp;

                client_post_truncate (this, tmp_rsp, &this_args_cbk->prestat,
                                      &this_args_cbk->poststat, &xdata);
                CLIENT_POST_FOP_TYPE (truncate, this_rsp, this_args_cbk,
                                      &this_args_cbk->prestat,
                                      &this_args_cbk->poststat, xdata);
                break;
        }
        case GF_FOP_OPEN:
        {
                gfs3_open_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_open_rsp;

                client_post_open (this, tmp_rsp, &xdata);
                CLIENT_POST_FOP_TYPE (open, this_rsp, this_args_cbk, local->fd,
                                      xdata);
                if (-1 != this_args_cbk->op_ret)
                        ret = client_add_fd_to_saved_fds (this, local->fd,
                                                          &local->loc,
                                                          args->req_list[index].flags,
                                                          tmp_rsp->fd,
                                                          0);
                break;
        }
        case GF_FOP_READ:
        {
                gfs3_read_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_read_rsp;

                client_post_readv (this, tmp_rsp, &this_args_cbk->iobref,
                                  req->rsp_iobref, &this_args_cbk->stat,
                                  vector, &req->rsp[1], &this_args_cbk->count,
                                  &xdata);

                /* Each read should be given read response that only
                 * corresponds to its request.
                 * Modify the iovecs accordingly.
                 * After each read, store the length of data already read
                 * so that the next ones can continue from there.
                 */
                if (local->read_length) {
                        vector[0].iov_base += local->read_length;
                        local->read_length += tmp_rsp->op_ret;
                } else {
                        local->read_length = tmp_rsp->op_ret;
                }

                args_readv_cbk_store (this_args_cbk, tmp_rsp->op_ret,
                                      gf_error_to_errno (tmp_rsp->op_errno),
                                      vector, this_args_cbk->count,
                                      &this_args_cbk->stat,
                                      this_args_cbk->iobref, xdata);

                if (tmp_rsp->op_ret >= 0)
                        if (local->attempt_reopen)
                                client_attempt_reopen (local->fd, this);

                break;
        }
        case GF_FOP_WRITE:
        {
                gfs3_write_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_write_rsp;

                client_post_writev (this, tmp_rsp, &this_args_cbk->prestat,
                                   &this_args_cbk->poststat, &xdata);
                args_writev_cbk_store (this_args_cbk, tmp_rsp->op_ret,
                                       gf_error_to_errno (tmp_rsp->op_errno),
                                       &this_args_cbk->prestat,
                                       &this_args_cbk->poststat, xdata);

                if (tmp_rsp->op_ret == 0)
                        if (local->attempt_reopen)
                                client_attempt_reopen (local->fd, this);
                break;
        }
        case GF_FOP_STATFS:
        {
                gfs3_statfs_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_statfs_rsp;

                client_post_statfs (this, tmp_rsp, &this_args_cbk->statvfs,
                                    &xdata);

                CLIENT_POST_FOP_TYPE (statfs, this_rsp, this_args_cbk,
                                      &this_args_cbk->statvfs, xdata);
                break;
         }
        case GF_FOP_FLUSH:
        {
                gf_common_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_flush_rsp;

                client_post_flush (this, tmp_rsp, &xdata);

                CLIENT_POST_FOP (flush, this_rsp, this_args_cbk, xdata);
                if (this_args_cbk->op_ret >= 0 && !fd_is_anonymous (local->fd)) {
                        /* Delete all saved locks of the owner issuing flush */
                        ret = delete_granted_locks_owner (local->fd, &local->owner);
                        gf_msg_trace (this->name, 0,
                                      "deleting locks of owner (%s) returned %d",
                                      lkowner_utoa (&local->owner), ret);
                }
                break;
         }
        case GF_FOP_FSYNC:
        {
                gfs3_fsync_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_fsync_rsp;

                client_post_fsync (this, tmp_rsp, &this_args_cbk->prestat,
                                   &this_args_cbk->poststat, &xdata);

                CLIENT_POST_FOP_TYPE (fsync, this_rsp, this_args_cbk,
                                      &this_args_cbk->prestat,
                                      &this_args_cbk->poststat, xdata);
                break;
        }
        case GF_FOP_SETXATTR:
        {
                gf_common_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_setxattr_rsp;

                client_post_setxattr (this, tmp_rsp, &xdata);

                CLIENT_POST_FOP (setxattr, this_rsp, this_args_cbk, xdata);
                break;
        }
        case GF_FOP_GETXATTR:
        {
                gfs3_getxattr_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_getxattr_rsp;

                client_post_getxattr (this, tmp_rsp, &xattr, &xdata);

                CLIENT_POST_FOP_TYPE (getxattr, this_rsp, this_args_cbk, xattr,
                                      xdata);
                break;
        }
        case GF_FOP_REMOVEXATTR:
         {
                gf_common_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_removexattr_rsp;

                client_post_removexattr (this, tmp_rsp, &xdata);

                CLIENT_POST_FOP (removexattr, this_rsp, this_args_cbk, xdata);
                break;
        }
        case GF_FOP_OPENDIR:
        {
                gfs3_opendir_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_opendir_rsp;

                client_post_opendir (this, tmp_rsp, &xdata);

                CLIENT_POST_FOP_TYPE (opendir, this_rsp, this_args_cbk,
                                      local->fd, xdata);
                if (-1 != this_args_cbk->op_ret)
                        ret = client_add_fd_to_saved_fds (this, local->fd,
                                                          &local->loc,
                                                          args->req_list[index].flags,
                                                          tmp_rsp->fd, 0);
                break;
        }
        case GF_FOP_FSYNCDIR:
        {
                gf_common_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_fsyncdir_rsp;

                client_post_fsyncdir (this, tmp_rsp, &xdata);

                CLIENT_POST_FOP (fsyncdir, this_rsp, this_args_cbk, xdata);
                break;
        }
        case GF_FOP_ACCESS:
        {
                gf_common_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_access_rsp;

                client_post_access (this, tmp_rsp, &xdata);

                CLIENT_POST_FOP (access, this_rsp, this_args_cbk, xdata);
                break;
        }
        case GF_FOP_CREATE:
        {
                gfs3_create_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_create_rsp;

                client_post_create (this, tmp_rsp, &this_args_cbk->stat,
                                   &this_args_cbk->preparent,
                                   &this_args_cbk->postparent, local, &xdata);

                CLIENT_POST_FOP_TYPE (create, this_rsp, this_args_cbk,
                                      local->fd, local->loc.inode,
                                      &this_args_cbk->stat,
                                      &this_args_cbk->preparent,
                                      &this_args_cbk->postparent, xdata);
                if (-1 != this_args_cbk->op_ret)
                        ret = client_add_fd_to_saved_fds (this, local->fd,
                                                          &local->loc,
                                                          args->req_list[index].flags,
                                                          tmp_rsp->fd, 0);
                break;
        }
        case GF_FOP_FTRUNCATE:
        {
                gfs3_ftruncate_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_ftruncate_rsp;

                client_post_ftruncate (this, tmp_rsp, &this_args_cbk->prestat,
                                      &this_args_cbk->poststat, &xdata);
                CLIENT_POST_FOP_TYPE (ftruncate, this_rsp, this_args_cbk,
                                      &this_args_cbk->prestat,
                                      &this_args_cbk->poststat, xdata);
                break;
        }
        case GF_FOP_FSTAT:
        {
                gfs3_fstat_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_fstat_rsp;

                client_post_fstat (this, tmp_rsp, &this_args_cbk->stat, &xdata);

                CLIENT_POST_FOP_TYPE (fstat, this_rsp, this_args_cbk,
                                      &this_args_cbk->stat, xdata);
                break;
        }
        case GF_FOP_LK:
        {
                gfs3_lk_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_lk_rsp;

                client_post_lk (this, tmp_rsp, &this_args_cbk->lock, &xdata);

                CLIENT_POST_FOP_TYPE (lk, this_rsp, this_args_cbk,
                                      &this_args_cbk->lock, xdata);
                break;
        }
        case GF_FOP_LOOKUP:
        {
                gfs3_lookup_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_lookup_rsp;

                client_post_lookup (this, tmp_rsp, &this_args_cbk->stat,
                                    &this_args_cbk->postparent, &xdata);
                CLIENT_POST_FOP_TYPE (lookup, this_rsp, this_args_cbk,
                                      local->loc.inode, &this_args_cbk->stat,
                                      xdata, &this_args_cbk->postparent);
                break;
        }
        case GF_FOP_READDIR:
        {
                gfs3_readdir_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_readdir_rsp;

                client_post_readdir (this, tmp_rsp, &entries, &xdata);

                CLIENT_POST_FOP_TYPE (readdir, this_rsp, this_args_cbk,
                                      &entries, xdata);
                break;
        }
        case GF_FOP_INODELK:
        {
                gf_common_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_inodelk_rsp;

                client_post_inodelk (this, tmp_rsp, &xdata);

                CLIENT_POST_FOP (inodelk, this_rsp, this_args_cbk, xdata);
                break;
        }
        case GF_FOP_FINODELK:
        {
                gf_common_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_finodelk_rsp;

                client_post_finodelk (this, tmp_rsp, &xdata);

                CLIENT_POST_FOP (finodelk, this_rsp, this_args_cbk, xdata);
                if (tmp_rsp->op_ret == 0)
                        if (local->attempt_reopen)
                                client_attempt_reopen (local->fd, this);
                break;
        }
        case GF_FOP_ENTRYLK:
        {
                gf_common_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_entrylk_rsp;

                client_post_entrylk (this, tmp_rsp, &xdata);

                CLIENT_POST_FOP (entrylk, this_rsp, this_args_cbk, xdata);
                break;
        }
        case GF_FOP_FENTRYLK:
        {
                gf_common_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_fentrylk_rsp;

                client_post_fentrylk (this, tmp_rsp, &xdata);

                CLIENT_POST_FOP (fentrylk, this_rsp, this_args_cbk, xdata);
                break;
        }
        case GF_FOP_XATTROP:
        {
                gfs3_xattrop_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_xattrop_rsp;

                client_post_xattrop (this, tmp_rsp, &xattr, &xdata);

                CLIENT_POST_FOP_TYPE (xattrop, this_rsp, this_args_cbk, xattr,
                                      xdata);
                break;
        }
        case GF_FOP_FXATTROP:
        {
                gfs3_fxattrop_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_fxattrop_rsp;

                client_post_fxattrop (this, tmp_rsp, &xattr, &xdata);

                CLIENT_POST_FOP_TYPE (fxattrop, this_rsp, this_args_cbk, xattr,
                                      xdata);
                if (rsp->op_ret == 0)
                        if (local->attempt_reopen)
                                client_attempt_reopen (local->fd, this);
                break;
        }
        case GF_FOP_FGETXATTR:
        {
                gfs3_fgetxattr_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_fgetxattr_rsp;

                client_post_fgetxattr (this, tmp_rsp, &xattr, &xdata);

                CLIENT_POST_FOP_TYPE (fgetxattr, this_rsp, this_args_cbk, xattr,
                                      xdata);
                break;
        }
        case GF_FOP_FSETXATTR:
        {
                gf_common_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_fsetxattr_rsp;

                client_post_fsetxattr (this, tmp_rsp, &xdata);

                CLIENT_POST_FOP (fsetxattr, this_rsp, this_args_cbk, xdata);
                break;
        }
        case GF_FOP_RCHECKSUM:
        {
                gfs3_rchecksum_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_rchecksum_rsp;

                client_post_rchecksum (this, tmp_rsp, &xdata);

                break;
                CLIENT_POST_FOP_TYPE (rchecksum, this_rsp, this_args_cbk,
                                      tmp_rsp->weak_checksum,
                                      (uint8_t*)tmp_rsp->strong_checksum.strong_checksum_val,
                                      xdata);
                break;
        }
        case GF_FOP_SETATTR:
        {
                gfs3_setattr_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_setattr_rsp;

                client_post_setattr (this, tmp_rsp, &this_args_cbk->prestat,
                                    &this_args_cbk->poststat, &xdata);

                CLIENT_POST_FOP_TYPE (setattr, this_rsp, this_args_cbk,
                                      &this_args_cbk->prestat,
                                      &this_args_cbk->poststat, xdata);
                break;
        }
        case GF_FOP_FSETATTR:
        {
                gfs3_fsetattr_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_fsetattr_rsp;

                client_post_fsetattr (this, tmp_rsp, &this_args_cbk->prestat,
                                      &this_args_cbk->poststat, &xdata);

                CLIENT_POST_FOP_TYPE (fsetattr, this_rsp, this_args_cbk,
                                      &this_args_cbk->prestat,
                                      &this_args_cbk->poststat, xdata);
                break;
        }
        case GF_FOP_READDIRP:
        {
                gfs3_readdirp_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_readdirp_rsp;

                client_post_readdirp (this, tmp_rsp, local->fd, &entries,
                                      &xdata);

                CLIENT_POST_FOP_TYPE (readdirp, this_rsp, this_args_cbk,
                                      &entries, xdata);
                break;
        }
        case GF_FOP_FREMOVEXATTR:
        {
                gf_common_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_fremovexattr_rsp;

                client_post_fremovexattr (this, tmp_rsp, &xdata);

                CLIENT_POST_FOP (fremovexattr, this_rsp, this_args_cbk, xdata);
                break;
        }
        case GF_FOP_FALLOCATE:
        {
                gfs3_fallocate_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_fallocate_rsp;

                client_post_fallocate (this, tmp_rsp, &this_args_cbk->prestat,
                                       &this_args_cbk->poststat, &xdata);

                CLIENT_POST_FOP_TYPE (fallocate, this_rsp, this_args_cbk,
                                      &this_args_cbk->prestat,
                                      &this_args_cbk->poststat, xdata);
                break;
        }
        case GF_FOP_DISCARD:
        {
                gfs3_discard_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_discard_rsp;

                client_post_discard (this, tmp_rsp, &this_args_cbk->prestat,
                                     &this_args_cbk->poststat, &xdata);

                CLIENT_POST_FOP_TYPE (discard, this_rsp, this_args_cbk,
                                      &this_args_cbk->prestat,
                                      &this_args_cbk->poststat, xdata);
                break;
        }
        case GF_FOP_ZEROFILL:
        {
                gfs3_zerofill_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_zerofill_rsp;

                client_post_zerofill (this, tmp_rsp, &this_args_cbk->prestat,
                                      &this_args_cbk->poststat, &xdata);

                CLIENT_POST_FOP_TYPE (zerofill, this_rsp, this_args_cbk,
                                      &this_args_cbk->prestat,
                                      &this_args_cbk->poststat, xdata);
                break;
        }
        case GF_FOP_IPC:
        {
                gfs3_ipc_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_ipc_rsp;

                client_post_ipc (this, tmp_rsp, &xdata);

                CLIENT_POST_FOP_TYPE (ipc, this_rsp, this_args_cbk, xdata);
                break;
        }
        case GF_FOP_SEEK:
        {
                gfs3_seek_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_seek_rsp;

                client_post_seek (this, tmp_rsp, &xdata);

                CLIENT_POST_FOP_TYPE (seek, this_rsp, this_args_cbk,
                                      tmp_rsp->offset, xdata);
                break;
        }
        case GF_FOP_LEASE:
        {
                gfs3_lease_rsp *tmp_rsp = NULL;
                tmp_rsp = &this_rsp->compound_rsp_u.compound_lease_rsp;

                client_post_lease (this, tmp_rsp, &this_args_cbk->lease,
                                   &xdata);

                CLIENT_POST_FOP_TYPE (lease, this_rsp, this_args_cbk,
                                      &this_args_cbk->lease, xdata);
                break;
        }
        default:
                return -ENOTSUP;
        }

        if (xdata)
                dict_unref (xdata);
        if (xattr)
                dict_unref (xattr);
        gf_dirent_free (&entries);
        return 0;
}

int
client_handle_fop_requirements (xlator_t *this, call_frame_t *frame,
                                gfs3_compound_req *req,
                                clnt_local_t *local,
                                struct iobref **req_iobref,
                                struct iobref **rsp_iobref,
                                struct iovec *req_vector,
                                struct iovec *rsp_vector, int *req_count,
                                int *rsp_count, default_args_t *args,
                                int fop_enum, int index)
{
        int             ret           = 0;
        int             op_errno      = ENOMEM;
        struct iobuf   *rsp_iobuf     = NULL;
        int64_t         remote_fd     = -1;
        compound_req    *this_req     = &req->compound_req_array.compound_req_array_val[index];

        this_req->fop_enum = fop_enum;

        switch (fop_enum) {
        case GF_FOP_STAT:
                CLIENT_PRE_FOP (stat, this,
                                &this_req->compound_req_u.compound_stat_req,
                                op_errno, out,
                                &args->loc, args->xdata);
                break;
        case GF_FOP_READLINK:
                CLIENT_PRE_FOP (readlink, this,
                                &this_req->compound_req_u.compound_readlink_req,
                                op_errno, out,
                                &args->loc, args->size, args->xdata);
                break;
        case GF_FOP_MKNOD:
                CLIENT_PRE_FOP (mknod, this,
                                &this_req->compound_req_u.compound_mknod_req,
                                op_errno, out,
                                &args->loc, args->mode, args->rdev,
                                args->umask, args->xdata);
                loc_copy (&local->loc, &args->loc);
                loc_path (&local->loc, NULL);
                break;
        case GF_FOP_MKDIR:
                CLIENT_PRE_FOP (mkdir, this,
                                &this_req->compound_req_u.compound_mkdir_req,
                                op_errno, out,
                                &args->loc, args->mode,
                                args->umask, args->xdata);
                loc_copy (&local->loc, &args->loc);
                loc_path (&local->loc, NULL);
                break;
        case GF_FOP_UNLINK:
                CLIENT_PRE_FOP (unlink, this,
                                &this_req->compound_req_u.compound_unlink_req,
                                op_errno, out,
                                &args->loc, args->xflag, args->xdata);
                break;
        case GF_FOP_RMDIR:
                CLIENT_PRE_FOP (rmdir, this,
                                &this_req->compound_req_u.compound_rmdir_req,
                                op_errno, out,
                                &args->loc, args->flags, args->xdata);
                break;
        case GF_FOP_SYMLINK:
                CLIENT_PRE_FOP (symlink, this,
                                &this_req->compound_req_u.compound_symlink_req,
                                op_errno, out,
                                &args->loc, args->linkname, args->umask,
                                args->xdata);
                loc_copy (&local->loc, &args->loc);
                loc_path (&local->loc, NULL);
                break;
        case GF_FOP_RENAME:
                CLIENT_PRE_FOP (rename, this,
                                &this_req->compound_req_u.compound_rename_req,
                                op_errno, out,
                                &args->loc, &args->loc2, args->xdata);
                break;
        case GF_FOP_LINK:
                CLIENT_PRE_FOP (link, this,
                                &this_req->compound_req_u.compound_link_req,
                                op_errno, out,
                                &args->loc, &args->loc2, args->xdata);
                break;
        case GF_FOP_TRUNCATE:
                CLIENT_PRE_FOP (truncate, this,
                                &this_req->compound_req_u.compound_truncate_req,
                                op_errno, out,
                                &args->loc, args->offset, args->xdata);
                break;
        case GF_FOP_OPEN:
                CLIENT_PRE_FOP (open, this,
                                &this_req->compound_req_u.compound_open_req,
                                op_errno, out,
                                &args->loc, args->fd, args->flags,
                                args->xdata);
                if (!local->fd)
                        local->fd = fd_ref (args->fd);
                break;
        case GF_FOP_READ:
                op_errno = client_pre_readv (this,
                                  &this_req->compound_req_u.compound_read_req,
                                  args->fd, args->size, args->offset,
                                  args->flags, args->xdata);

                if (op_errno) {
                        op_errno = -op_errno;
                        goto out;
                }
                if (!local->fd)
                        local->fd = fd_ref (args->fd);
                local->attempt_reopen = client_is_reopen_needed
                                        (args->fd, this, remote_fd);
                rsp_iobuf = iobuf_get2 (this->ctx->iobuf_pool, args->size);
                if (rsp_iobuf == NULL) {
                        op_errno = ENOMEM;
                        goto out;
                }

                if (!*rsp_iobref) {
                        *rsp_iobref = iobref_new ();
                        if (*rsp_iobref == NULL) {
                                op_errno = ENOMEM;
                                goto out;
                        }
                }

                iobref_add (*rsp_iobref, rsp_iobuf);
                iobuf_unref (rsp_iobuf);

                if (*rsp_count + 1 >= MAX_IOVEC) {
                        op_errno = ENOMEM;
                        goto out;
                }
                rsp_vector[*rsp_count].iov_base = iobuf_ptr (rsp_iobuf);
                rsp_vector[*rsp_count].iov_len = iobuf_pagesize (rsp_iobuf);
                rsp_iobuf = NULL;
                if (args->size > rsp_vector[*rsp_count].iov_len) {
                        gf_msg (this->name, GF_LOG_WARNING, ENOMEM,
                                PC_MSG_NO_MEMORY,
                                "read-size (%lu) is bigger than iobuf size "
                                "(%lu)",
                                (unsigned long)args->size,
                                (unsigned long)rsp_vector[*rsp_count].iov_len);
                        op_errno = EINVAL;
                        goto out;
                }
                *rsp_count += 1;

                break;
        case GF_FOP_WRITE:
                op_errno = client_pre_writev (this,
                           &this_req->compound_req_u.compound_write_req,
                           args->fd, iov_length (args->vector, args->count),
                           args->offset, args->flags, &args->xdata);

                if (op_errno) {
                        op_errno = -op_errno;
                        goto out;
                }
                if (!local->fd)
                        local->fd = fd_ref (args->fd);
                local->attempt_reopen = client_is_reopen_needed
                                        (args->fd, this, remote_fd);

                if (*req_count + args->count >= MAX_IOVEC) {
                        op_errno = ENOMEM;
                        goto out;
                }
                memcpy (&req_vector[*req_count], args->vector,
                        (args->count * sizeof(req_vector[0])));
                *req_count += args->count;

                if (!*req_iobref)
                        *req_iobref = args->iobref;
                else
                        if (iobref_merge (*req_iobref, args->iobref))
                                goto out;
                break;
        case GF_FOP_STATFS:
                CLIENT_PRE_FOP (statfs, this,
                                &this_req->compound_req_u.compound_statfs_req,
                                op_errno, out,
                                &args->loc, args->xdata);
                break;
        case GF_FOP_FLUSH:
                CLIENT_PRE_FOP (flush, this,
                                &this_req->compound_req_u.compound_flush_req,
                                op_errno, out,
                                args->fd, args->xdata);
                if (!local->fd)
                        local->fd = fd_ref (args->fd);
                local->owner = frame->root->lk_owner;
                break;
        case GF_FOP_FSYNC:
                CLIENT_PRE_FOP (fsync, this,
                                &this_req->compound_req_u.compound_fsync_req,
                                op_errno, out,
                                args->fd, args->datasync, args->xdata);
                break;
        case GF_FOP_SETXATTR:
                CLIENT_PRE_FOP (setxattr, this,
                                &this_req->compound_req_u.compound_setxattr_req,
                                op_errno, out,
                                &args->loc, args->xattr, args->flags,
                                args->xdata);
                break;
        case GF_FOP_GETXATTR:
                CLIENT_PRE_FOP (getxattr, this,
                                &this_req->compound_req_u.compound_getxattr_req,
                                op_errno, out,
                                &args->loc, args->name, args->xdata);
                loc_copy (&local->loc, &args->loc);
                loc_path (&local->loc, NULL);
                break;
        case GF_FOP_REMOVEXATTR:
                CLIENT_PRE_FOP (removexattr, this,
                                &this_req->compound_req_u.compound_removexattr_req,
                                op_errno, out,
                                &args->loc, args->name, args->xdata);
                break;
        case GF_FOP_OPENDIR:
                CLIENT_PRE_FOP (opendir, this,
                                &this_req->compound_req_u.compound_opendir_req,
                                op_errno, out,
                                &args->loc, args->fd, args->xdata);
                if (!local->fd)
                        local->fd = fd_ref (args->fd);
                loc_copy (&local->loc, &args->loc);
                loc_path (&local->loc, NULL);
                break;
        case GF_FOP_FSYNCDIR:
                CLIENT_PRE_FOP (fsyncdir, this,
                                &this_req->compound_req_u.compound_fsyncdir_req,
                                op_errno, out,
                                args->fd, args->datasync, args->xdata);
                break;
        case GF_FOP_ACCESS:
                CLIENT_PRE_FOP (access, this,
                                &this_req->compound_req_u.compound_access_req,
                                op_errno, out,
                                &args->loc, args->mask, args->xdata);
                break;
        case GF_FOP_CREATE:
                CLIENT_PRE_FOP (create, this,
                                &this_req->compound_req_u.compound_create_req,
                                op_errno, out,
                                &args->loc, args->fd, args->mode, args->flags,
                                args->umask, args->xdata);
                if (!local->fd)
                        local->fd = fd_ref (args->fd);

                loc_copy (&local->loc, &args->loc);
                loc_path (&local->loc, NULL);
                break;
        case GF_FOP_FTRUNCATE:
                CLIENT_PRE_FOP (ftruncate, this,
                                &this_req->compound_req_u.compound_ftruncate_req,
                                op_errno, out,
                                args->fd, args->offset, args->xdata);
                break;
        case GF_FOP_FSTAT:
                CLIENT_PRE_FOP (fstat, this,
                                &this_req->compound_req_u.compound_fstat_req,
                                op_errno, out,
                                args->fd, args->xdata);
                break;
        case GF_FOP_LK:
                CLIENT_PRE_FOP (lk, this,
                                &this_req->compound_req_u.compound_lk_req,
                                op_errno, out,
                                args->cmd, &args->lock, args->fd, args->xdata);
                if (!local->fd)
                        local->fd    = fd_ref (args->fd);
                local->owner = frame->root->lk_owner;
                break;
        case GF_FOP_LOOKUP:
                CLIENT_PRE_FOP (lookup, this,
                                &this_req->compound_req_u.compound_lookup_req,
                                op_errno, out,
                                &args->loc, args->xdata);
                loc_copy (&local->loc, &args->loc);
                loc_path (&local->loc, NULL);
                break;
        case GF_FOP_READDIR:
                CLIENT_PRE_FOP (readdir, this,
                                &this_req->compound_req_u.compound_readdir_req,
                                op_errno, out,
                                args->fd, args->size, args->offset,
                                args->xdata);
                break;
        case GF_FOP_INODELK:
                CLIENT_PRE_FOP (inodelk, this,
                                &this_req->compound_req_u.compound_inodelk_req,
                                op_errno, out,
                                &args->loc, args->cmd, &args->lock,
                                args->volume, args->xdata);
                break;
        case GF_FOP_FINODELK:
                CLIENT_PRE_FOP (finodelk, this,
                                &this_req->compound_req_u.compound_finodelk_req,
                                op_errno, out,
                                args->fd, args->cmd, &args->lock,
                                args->volume, args->xdata);
                if (!local->fd)
                        local->fd = fd_ref (args->fd);
                local->attempt_reopen = client_is_reopen_needed
                                        (args->fd, this, remote_fd);
                break;
        case GF_FOP_ENTRYLK:
                CLIENT_PRE_FOP (entrylk, this,
                                &this_req->compound_req_u.compound_entrylk_req,
                                op_errno, out,
                                &args->loc, args->entrylkcmd,
                                args->entrylktype, args->volume,
                                args->name, args->xdata);
                break;
        case GF_FOP_FENTRYLK:
                CLIENT_PRE_FOP (fentrylk, this,
                                &this_req->compound_req_u.compound_fentrylk_req,
                                op_errno, out,
                                args->fd, args->entrylkcmd,
                                args->entrylktype, args->volume,
                                args->name, args->xdata);
                break;
        case GF_FOP_XATTROP:
                CLIENT_PRE_FOP (xattrop, this,
                                &this_req->compound_req_u.compound_xattrop_req,
                                op_errno, out,
                                &args->loc, args->xattr, args->optype,
                                args->xdata);
                break;
        case GF_FOP_FXATTROP:
                CLIENT_PRE_FOP (fxattrop, this,
                                &this_req->compound_req_u.compound_fxattrop_req,
                                op_errno, out,
                                args->fd, args->xattr, args->optype,
                                args->xdata);
                if (!local->fd)
                        local->fd = fd_ref (args->fd);
                local->attempt_reopen = client_is_reopen_needed
                                        (args->fd, this, remote_fd);
                break;
        case GF_FOP_FGETXATTR:
                CLIENT_PRE_FOP (fgetxattr, this,
                                &this_req->compound_req_u.compound_fgetxattr_req,
                                op_errno, out,
                                args->fd, args->name, args->xdata);
                break;
        case GF_FOP_FSETXATTR:
                CLIENT_PRE_FOP (fsetxattr, this,
                                &this_req->compound_req_u.compound_fsetxattr_req,
                                op_errno, out,
                                args->fd, args->flags, args->xattr,
                                args->xdata);
                break;
        case GF_FOP_RCHECKSUM:
                CLIENT_PRE_FOP (rchecksum, this,
                                &this_req->compound_req_u.compound_rchecksum_req,
                                op_errno, out,
                                args->fd, args->size, args->offset,
                                args->xdata);
                break;
        case GF_FOP_SETATTR:
                CLIENT_PRE_FOP (setattr, this,
                                &this_req->compound_req_u.compound_setattr_req,
                                op_errno, out,
                                &args->loc, args->valid, &args->stat,
                                args->xdata);
                break;
        case GF_FOP_FSETATTR:
                CLIENT_PRE_FOP (fsetattr, this,
                                &this_req->compound_req_u.compound_fsetattr_req,
                                op_errno, out,
                                args->fd, args->valid, &args->stat,
                                args->xdata);
                break;
        case GF_FOP_READDIRP:
                CLIENT_PRE_FOP (readdirp, this,
                                &this_req->compound_req_u.compound_readdirp_req,
                                op_errno, out,
                                args->fd, args->size, args->offset,
                                args->xdata);
                if (!local->fd)
                        local->fd = fd_ref (args->fd);
                break;
        case GF_FOP_FREMOVEXATTR:
                CLIENT_PRE_FOP (fremovexattr, this,
                                &this_req->compound_req_u.compound_fremovexattr_req,
                                op_errno, out,
                                args->fd, args->name, args->xdata);
                break;
	case GF_FOP_FALLOCATE:
                CLIENT_PRE_FOP (fallocate, this,
                                &this_req->compound_req_u.compound_fallocate_req,
                                op_errno, out,
                                args->fd, args->flags, args->offset,
                                args->size, args->xdata);
                break;
	case GF_FOP_DISCARD:
                CLIENT_PRE_FOP (discard, this,
                                &this_req->compound_req_u.compound_discard_req,
                                op_errno, out,
                                args->fd, args->offset, args->size,
                                args->xdata);
                break;
        case GF_FOP_ZEROFILL:
                CLIENT_PRE_FOP (zerofill, this,
                                &this_req->compound_req_u.compound_zerofill_req,
                                op_errno, out,
                                args->fd, args->offset, args->size,
                                args->xdata);
                break;
        case GF_FOP_IPC:
                CLIENT_PRE_FOP (ipc, this,
                                &this_req->compound_req_u.compound_ipc_req,
                                op_errno, out,
                                args->cmd, args->xdata);
                break;
        case GF_FOP_SEEK:
                CLIENT_PRE_FOP (seek, this,
                                &this_req->compound_req_u.compound_seek_req,
                                op_errno, out,
                                args->fd, args->offset, args->what,
                                args->xdata);
                break;
        case GF_FOP_LEASE:
                CLIENT_PRE_FOP (lease, this,
                                &this_req->compound_req_u.compound_lease_req,
                                op_errno, out, &args->loc, &args->lease,
                                args->xdata);
        default:
                return ENOTSUP;
        }
        return 0;
out:
        return op_errno;
}

void
compound_request_cleanup (gfs3_compound_req *req)
{
        int i       = 0;
        int length  = req->compound_req_array.compound_req_array_len;
        compound_req   *curr_req = NULL;


        if (!req->compound_req_array.compound_req_array_val)
                return;

        for (i = 0; i < length; i++) {
                curr_req = &req->compound_req_array.compound_req_array_val[i];

                switch (curr_req->fop_enum) {
                case GF_FOP_STAT:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, stat);
                        break;
                case GF_FOP_READLINK:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, readlink);
                        break;
                case GF_FOP_MKNOD:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, mknod);
                        break;
                case GF_FOP_MKDIR:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, mkdir);
                        break;
                case GF_FOP_UNLINK:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, unlink);
                        break;
                case GF_FOP_RMDIR:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, rmdir);
                        break;
                case GF_FOP_SYMLINK:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, symlink);
                        break;
                case GF_FOP_RENAME:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, rename);
                        break;
                case GF_FOP_LINK:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, link);
                        break;
                case GF_FOP_TRUNCATE:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, truncate);
                        break;
                case GF_FOP_OPEN:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, open);
                        break;
                case GF_FOP_READ:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, read);
                        break;
                case GF_FOP_WRITE:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, write);
                        break;
                case GF_FOP_STATFS:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, statfs);
                        break;
                case GF_FOP_FLUSH:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, flush);
                        break;
                case GF_FOP_FSYNC:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, fsync);
                        break;
                case GF_FOP_SETXATTR:
                {
                        gfs3_setxattr_req *args = &CPD_REQ_FIELD (curr_req,
                                                  setxattr);
                        GF_FREE (args->dict.dict_val);
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, setxattr);
                        break;
                }
                case GF_FOP_GETXATTR:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, getxattr);
                        break;
                case GF_FOP_REMOVEXATTR:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, removexattr);
                        break;
                case GF_FOP_OPENDIR:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, opendir);
                        break;
                case GF_FOP_FSYNCDIR:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, fsyncdir);
                        break;
                case GF_FOP_ACCESS:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, access);
                        break;
                case GF_FOP_CREATE:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, create);
                        break;
                case GF_FOP_FTRUNCATE:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, ftruncate);
                        break;
                case GF_FOP_FSTAT:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, fstat);
                        break;
                case GF_FOP_LK:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, lk);
                        break;
                case GF_FOP_LOOKUP:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, lookup);
                        break;
                case GF_FOP_READDIR:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, readdir);
                        break;
                case GF_FOP_INODELK:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, inodelk);
                        break;
                case GF_FOP_FINODELK:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, finodelk);
                        break;
                case GF_FOP_ENTRYLK:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, entrylk);
                        break;
                case GF_FOP_FENTRYLK:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, fentrylk);
                        break;
                case GF_FOP_XATTROP:
                {
                        gfs3_xattrop_req *args = &CPD_REQ_FIELD (curr_req,
                                                 xattrop);
                        GF_FREE (args->dict.dict_val);
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, xattrop);
                        break;
                }
                case GF_FOP_FXATTROP:
                {
                        gfs3_fxattrop_req *args = &CPD_REQ_FIELD (curr_req,
                                                  fxattrop);
                        GF_FREE (args->dict.dict_val);
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, fxattrop);
                        break;
                }
                case GF_FOP_FGETXATTR:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, fgetxattr);
                        break;
                case GF_FOP_FSETXATTR:
                {
                        gfs3_fsetxattr_req *args = &CPD_REQ_FIELD(curr_req,
                                                   fsetxattr);
                        GF_FREE (args->dict.dict_val);
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, fsetxattr);
                        break;
                }
                case GF_FOP_RCHECKSUM:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, rchecksum);
                        break;
                case GF_FOP_SETATTR:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, setattr);
                        break;
                case GF_FOP_FSETATTR:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, fsetattr);
                        break;
                case GF_FOP_READDIRP:
                {
                        gfs3_readdirp_req *args = &CPD_REQ_FIELD(curr_req,
                                                  readdirp);
                        GF_FREE (args->dict.dict_val);
                        break;
                }
                case GF_FOP_FREMOVEXATTR:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, fremovexattr);
                        break;
                case GF_FOP_FALLOCATE:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, fallocate);
                        break;
                case GF_FOP_DISCARD:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, discard);
                        break;
                case GF_FOP_ZEROFILL:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, zerofill);
                        break;
                case GF_FOP_IPC:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, ipc);
                        break;
                case GF_FOP_SEEK:
                        CLIENT_COMPOUND_FOP_CLEANUP (curr_req, seek);
                        break;
                default:
                        break;
                }
        }

        GF_FREE (req->compound_req_array.compound_req_array_val);
        return;
}

void
clnt_getactivelk_rsp_cleanup (gfs3_getactivelk_rsp *rsp)
{
        gfs3_locklist   *trav = NULL;
        gfs3_locklist   *next = NULL;

        trav = rsp->reply;

        while (trav) {
                next = trav->nextentry;
                free (trav->client_uid);
                free (trav);
                trav = next;
        }
}

int
clnt_unserialize_rsp_locklist (xlator_t *this, struct gfs3_getactivelk_rsp *rsp,
                               lock_migration_info_t *lmi)
{
        struct gfs3_locklist            *trav           = NULL;
        lock_migration_info_t           *temp           = NULL;
        int                             ret             = -1;
        clnt_conf_t                     *conf           = NULL;

        trav = rsp->reply;

        conf = this->private;
        if (!conf)
                goto out;

        while (trav) {
                temp = GF_CALLOC (1, sizeof (*lmi), gf_common_mt_lock_mig);
                if (temp == NULL) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, 0, "No memory");
                        goto out;
                }

                INIT_LIST_HEAD (&temp->list);

                gf_proto_flock_to_flock (&trav->flock, &temp->flock);

                temp->lk_flags = trav->lk_flags;

                temp->client_uid =  gf_strdup (trav->client_uid);

                list_add_tail (&temp->list, &lmi->list);

                trav = trav->nextentry;
        }

        ret = 0;
out:
        return ret;
}

void
clnt_setactivelk_req_cleanup (gfs3_setactivelk_req *req)
{
        gfs3_locklist   *trav = NULL;
        gfs3_locklist   *next = NULL;

        trav = req->request;

        while (trav) {
                next = trav->nextentry;
                GF_FREE (trav->client_uid);
                GF_FREE (trav);
                trav = next;
        }
}

int
serialize_req_locklist (lock_migration_info_t *locklist,
                        gfs3_setactivelk_req *req)
{
        lock_migration_info_t   *tmp    = NULL;
        gfs3_locklist           *trav   = NULL;
        gfs3_locklist           *prev   = NULL;
        int                     ret     = -1;

        GF_VALIDATE_OR_GOTO ("server", locklist, out);
        GF_VALIDATE_OR_GOTO ("server", req, out);

        list_for_each_entry (tmp, &locklist->list, list) {
                trav = GF_CALLOC (1, sizeof (*trav),
                                  gf_client_mt_clnt_lock_request_t);
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
                        gf_msg (THIS->name, GF_LOG_ERROR, 0, 0,
                                "Unknown lock type: %"PRId32"!",
                                tmp->flock.l_type);
                        break;
                }

                gf_proto_flock_from_flock (&trav->flock, &tmp->flock);

                trav->lk_flags = tmp->lk_flags;

                trav->client_uid = gf_strdup (tmp->client_uid);
                if (!trav->client_uid) {
                        gf_msg (THIS->name, GF_LOG_ERROR, 0, 0,
                                "client_uid could not be allocated");
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
        GF_FREE (trav);

        return ret;
}

void
client_compound_rsp_cleanup (gfs3_compound_rsp *rsp, int len)
{
        int i = 0;
        compound_rsp            *this_rsp       = NULL;

        for (i = 0; i < len; i++) {
                this_rsp = &rsp->compound_rsp_array.compound_rsp_array_val[i];
                switch (this_rsp->fop_enum) {
                case GF_FOP_STAT:
                        CLIENT_FOP_RSP_CLEANUP (rsp, stat, i);
                        break;
                case GF_FOP_MKNOD:
                        CLIENT_FOP_RSP_CLEANUP (rsp, mknod, i);
                        break;
                case GF_FOP_MKDIR:
                        CLIENT_FOP_RSP_CLEANUP (rsp, mkdir, i);
                        break;
                case GF_FOP_UNLINK:
                        CLIENT_FOP_RSP_CLEANUP (rsp, unlink, i);
                        break;
                case GF_FOP_RMDIR:
                        CLIENT_FOP_RSP_CLEANUP (rsp, rmdir, i);
                        break;
                case GF_FOP_SYMLINK:
                        CLIENT_FOP_RSP_CLEANUP (rsp, symlink, i);
                        break;
                case GF_FOP_RENAME:
                        CLIENT_FOP_RSP_CLEANUP (rsp, rename, i);
                        break;
                case GF_FOP_LINK:
                        CLIENT_FOP_RSP_CLEANUP (rsp, link, i);
                        break;
                case GF_FOP_TRUNCATE:
                        CLIENT_FOP_RSP_CLEANUP (rsp, truncate, i);
                        break;
                case GF_FOP_OPEN:
                        CLIENT_FOP_RSP_CLEANUP (rsp, open, i);
                        break;
                case GF_FOP_READ:
                        CLIENT_FOP_RSP_CLEANUP (rsp, read, i);
                        break;
                case GF_FOP_WRITE:
                        CLIENT_FOP_RSP_CLEANUP (rsp, write, i);
                        break;
                case GF_FOP_STATFS:
                        CLIENT_FOP_RSP_CLEANUP (rsp, statfs, i);
                        break;
                case GF_FOP_FSYNC:
                        CLIENT_FOP_RSP_CLEANUP (rsp, fsync, i);
                        break;
                case GF_FOP_OPENDIR:
                        CLIENT_FOP_RSP_CLEANUP (rsp, opendir, i);
                        break;
                case GF_FOP_CREATE:
                        CLIENT_FOP_RSP_CLEANUP (rsp, create, i);
                        break;
                case GF_FOP_FTRUNCATE:
                        CLIENT_FOP_RSP_CLEANUP (rsp, ftruncate, i);
                        break;
                case GF_FOP_FSTAT:
                        CLIENT_FOP_RSP_CLEANUP (rsp, fstat, i);
                        break;
                case GF_FOP_LOOKUP:
                        CLIENT_FOP_RSP_CLEANUP (rsp, lookup, i);
                        break;
                case GF_FOP_SETATTR:
                        CLIENT_FOP_RSP_CLEANUP (rsp, setattr, i);
                        break;
                case GF_FOP_FSETATTR:
                        CLIENT_FOP_RSP_CLEANUP (rsp, fsetattr, i);
                        break;
                case GF_FOP_FALLOCATE:
                        CLIENT_FOP_RSP_CLEANUP (rsp, fallocate, i);
                        break;
                case GF_FOP_DISCARD:
                        CLIENT_FOP_RSP_CLEANUP (rsp, discard, i);
                        break;
                case GF_FOP_ZEROFILL:
                        CLIENT_FOP_RSP_CLEANUP (rsp, zerofill, i);
                        break;
                case GF_FOP_IPC:
                        CLIENT_FOP_RSP_CLEANUP (rsp, ipc, i);
                        break;
                case GF_FOP_SEEK:
                        CLIENT_FOP_RSP_CLEANUP (rsp, seek, i);
                        break;
                case GF_FOP_LEASE:
                        CLIENT_FOP_RSP_CLEANUP (rsp, lease, i);
                        break;
                /* fops that use gf_common_rsp */
                case GF_FOP_FLUSH:
                        CLIENT_COMMON_RSP_CLEANUP (rsp, flush, i);
                        break;
                case GF_FOP_SETXATTR:
                        CLIENT_COMMON_RSP_CLEANUP (rsp, setxattr, i);
                        break;
                case GF_FOP_REMOVEXATTR:
                        CLIENT_COMMON_RSP_CLEANUP (rsp, removexattr, i);
                        break;
                case GF_FOP_FSETXATTR:
                        CLIENT_COMMON_RSP_CLEANUP (rsp, fsetxattr, i);
                        break;
                case GF_FOP_FREMOVEXATTR:
                        CLIENT_COMMON_RSP_CLEANUP (rsp, fremovexattr, i);
                        break;
                case GF_FOP_FSYNCDIR:
                        CLIENT_COMMON_RSP_CLEANUP (rsp, fsyncdir, i);
                        break;
                case GF_FOP_ACCESS:
                        CLIENT_COMMON_RSP_CLEANUP (rsp, access, i);
                        break;
                case GF_FOP_INODELK:
                        CLIENT_COMMON_RSP_CLEANUP (rsp, inodelk, i);
                        break;
                case GF_FOP_FINODELK:
                        CLIENT_COMMON_RSP_CLEANUP (rsp, finodelk, i);
                        break;
                case GF_FOP_ENTRYLK:
                        CLIENT_COMMON_RSP_CLEANUP (rsp, entrylk, i);
                        break;
                case GF_FOP_FENTRYLK:
                        CLIENT_COMMON_RSP_CLEANUP (rsp, fentrylk, i);
                        break;
                /* fops that need extra cleanup */
                case GF_FOP_LK:
                {
                        CLIENT_FOP_RSP_CLEANUP (rsp, lk, i);
                        gfs3_lk_rsp *tmp_rsp = &CPD_RSP_FIELD(this_rsp, lk);
                        free (tmp_rsp->flock.lk_owner.lk_owner_val);
                        break;
                }
                case GF_FOP_READLINK:
                {
                        CLIENT_FOP_RSP_CLEANUP (rsp, readlink, i);
                        gfs3_readlink_rsp *tmp_rsp = &CPD_RSP_FIELD(this_rsp,
                                                                    readlink);
                        free (tmp_rsp->path);
                        break;
                }
                case GF_FOP_XATTROP:
                {
                        CLIENT_FOP_RSP_CLEANUP (rsp, xattrop, i);
                        gfs3_xattrop_rsp *tmp_rsp = &CPD_RSP_FIELD(this_rsp,
                                                                   xattrop);
                        free (tmp_rsp->dict.dict_val);
                        break;
                }
                case GF_FOP_FXATTROP:
                {
                        CLIENT_FOP_RSP_CLEANUP (rsp, fxattrop, i);
                        gfs3_fxattrop_rsp *tmp_rsp = &CPD_RSP_FIELD(this_rsp,
                                                                    fxattrop);
                        free (tmp_rsp->dict.dict_val);
                        break;
                }
                case GF_FOP_READDIR:
                {
                        CLIENT_FOP_RSP_CLEANUP (rsp, readdir, i);
                        gfs3_readdir_rsp *tmp_rsp = &CPD_RSP_FIELD(this_rsp,
                                                                   readdir);
                        clnt_readdir_rsp_cleanup (tmp_rsp);
                        break;
                }
                case GF_FOP_READDIRP:
                {
                        CLIENT_FOP_RSP_CLEANUP (rsp, readdirp, i);
                        gfs3_readdirp_rsp *tmp_rsp = &CPD_RSP_FIELD(this_rsp,
                                                                    readdirp);
                        clnt_readdirp_rsp_cleanup (tmp_rsp);
                        break;
                }
                case GF_FOP_GETXATTR:
                {
                        CLIENT_FOP_RSP_CLEANUP (rsp, getxattr, i);
                        gfs3_getxattr_rsp *tmp_rsp = &CPD_RSP_FIELD(this_rsp,
                                                                    getxattr);
                        free (tmp_rsp->dict.dict_val);
                        break;
                }
                case GF_FOP_FGETXATTR:
                {
                        CLIENT_FOP_RSP_CLEANUP (rsp, fgetxattr, i);
                        gfs3_fgetxattr_rsp *tmp_rsp = &CPD_RSP_FIELD(this_rsp,
                                                      fgetxattr);
                        free (tmp_rsp->dict.dict_val);
                        break;
                }
                case GF_FOP_RCHECKSUM:
                {
                        CLIENT_FOP_RSP_CLEANUP (rsp, rchecksum, i);
                        gfs3_rchecksum_rsp *rck = &CPD_RSP_FIELD(this_rsp,
                                                  rchecksum);
                        if (rck->strong_checksum.strong_checksum_val) {
                                free (rck->strong_checksum.strong_checksum_val);
                        }
                        break;
                }
                default:
                        break;
                }
        }
        return;
}
