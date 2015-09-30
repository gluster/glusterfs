/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
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

#include "client.h"
#include "fd.h"

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
                        gf_log (this->name, GF_LOG_INFO,
                                "%s (%s): trying duplicate remote fd set. ",
                                loc->path, uuid_utoa (loc->inode->gfid));
                else
                        gf_log (this->name, GF_LOG_INFO,
                                "%p: trying duplicate remote fd set. ", file);
        }

        ret = fd_ctx_set (file, this, (uint64_t)(unsigned long)ctx);
        if (ret < 0) {
                if (loc)
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s (%s): failed to set remote fd",
                                loc->path, uuid_utoa (loc->inode->gfid));
                else
                        gf_log (this->name, GF_LOG_WARNING,
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

                mem_put (local);
        }

        return 0;
}

int
unserialize_rsp_dirent (struct gfs3_readdir_rsp *rsp, gf_dirent_t *entries)
{
        struct gfs3_dirlist  *trav      = NULL;
	gf_dirent_t          *entry     = NULL;
        int                   entry_len = 0;
        int                   ret       = -1;

        trav = rsp->reply;
        while (trav) {
                entry_len = gf_dirent_size (trav->name);
                entry = GF_CALLOC (1, entry_len, gf_common_mt_gf_dirent_t);
                if (!entry)
                        goto out;

                entry->d_ino  = trav->d_ino;
                entry->d_off  = trav->d_off;
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

        trav = rsp->reply;

        if (fd)
                itable = fd->inode->table;

        while (trav) {
                entry_len = gf_dirent_size (trav->name);
                entry = GF_CALLOC (1, entry_len, gf_common_mt_gf_dirent_t);
                if (!entry)
                        goto out;

                entry->d_ino  = trav->d_ino;
                entry->d_off  = trav->d_off;
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
                                gf_log (THIS->name, GF_LOG_WARNING,
                                        "failed to unserialize xattr dict");
                                errno = EINVAL;
                                goto out;
                        }
                        entry->dict->extra_free = buf;
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
        clnt_conf_t  *conf  = NULL;
        clnt_local_t *local = NULL;
        int          ret    = 0;

        this = frame->this;
        conf = this->private;

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
