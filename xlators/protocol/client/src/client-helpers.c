/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
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
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s (%"PRId64"): trying duplicate remote fd set. ",
                        loc->path, loc->inode->ino);
        }

        ret = fd_ctx_set (file, this, (uint64_t)(unsigned long)ctx);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s (%"PRId64"): failed to set remote fd",
                        loc->path, loc->inode->ino);
        }
out:
        return;
}


int
client_local_wipe (clnt_local_t *local)
{
        if (local) {
                loc_wipe (&local->loc);

                if (local->fd)
                        fd_unref (local->fd);

                 GF_FREE (local);
        }

        return 0;
}
