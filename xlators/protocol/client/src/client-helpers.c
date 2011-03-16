/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "client.h"
#include "fd.h"


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
                                "%s (%"PRId64"): trying duplicate remote fd set. ",
                                loc->path, loc->inode->ino);
                else
                        gf_log (this->name, GF_LOG_INFO,
                                "%p: trying duplicate remote fd set. ", file);
        }

        ret = fd_ctx_set (file, this, (uint64_t)(unsigned long)ctx);
        if (ret < 0) {
                if (loc)
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s (%"PRId64"): failed to set remote fd",
                                loc->path, loc->inode->ino);
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

                if (local->fd) {
                        fd_unref (local->fd);
                }

                if (local->iobref) {
                        iobref_unref (local->iobref);
                }

                 GF_FREE (local);
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
unserialize_rsp_direntp (struct gfs3_readdirp_rsp *rsp, gf_dirent_t *entries)
{
        struct gfs3_dirplist *trav      = NULL;
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

                gf_stat_to_iatt (&trav->stat, &entry->d_stat);

                strcpy (entry->d_name, trav->name);

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
