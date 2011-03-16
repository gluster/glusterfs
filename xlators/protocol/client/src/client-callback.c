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
#include "rpc-clnt.h"

int
client_cbk_null (void *data)
{
        gf_log (THIS->name, GF_LOG_WARNING,
                "this function should not be called");
        return 0;
}

int
client_cbk_fetchspec (void *data)
{
        gf_log (THIS->name, GF_LOG_WARNING,
                "this function should not be called");
        return 0;
}

int
client_cbk_ino_flush (void *data)
{
        gf_log (THIS->name, GF_LOG_WARNING,
                "this function should not be called");
        return 0;
}

rpcclnt_cb_actor_t gluster_cbk_actors[] = {
        [GF_CBK_NULL]      = {"NULL",      GF_CBK_NULL,      client_cbk_null },
        [GF_CBK_FETCHSPEC] = {"FETCHSPEC", GF_CBK_FETCHSPEC, client_cbk_fetchspec },
        [GF_CBK_INO_FLUSH] = {"INO_FLUSH", GF_CBK_INO_FLUSH, client_cbk_ino_flush },
};


struct rpcclnt_cb_program gluster_cbk_prog = {
        .progname  = "GlusterFS Callback",
        .prognum   = GLUSTER_CBK_PROGRAM,
        .progver   = GLUSTER_CBK_VERSION,
        .actors    = gluster_cbk_actors,
        .numactors = GF_CBK_MAXVALUE,
};
