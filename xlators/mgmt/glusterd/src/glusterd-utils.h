/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _GLUSTERD_UTILS_H
#define _GLUSTERD_UTILS_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <pthread.h>
#include "uuid.h"

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "call-stub.h"
#include "fd.h"
#include "byte-order.h"
#include "glusterd.h"
#include "rpc-clnt.h"
#include "protocol-common.h"

struct glusterd_lock_ {
        uuid_t  owner;
        time_t  timestamp;
};

typedef struct glusterd_lock_ glusterd_lock_t;

int32_t
glusterd_lock (uuid_t new_owner);

int32_t
glusterd_unlock (uuid_t owner);

int32_t
glusterd_get_uuid (uuid_t *uuid);

int
glusterd_submit_reply (rpcsvc_request_t *req, void *arg,
                       struct iovec *payload, int payloadcount,
                       struct iobref *iobref, gd_serialize_t sfunc);

int
glusterd_submit_request (glusterd_peerinfo_t *peerinfo, void *req,
                         call_frame_t *frame, struct rpc_clnt_program *prog,
                         int procnum, struct iobref *iobref,
                         gd_serialize_t sfunc, xlator_t *this,
                         fop_cbk_fn_t cbkfn);

int32_t
glusterd_volinfo_new (glusterd_volinfo_t **volinfo);

gf_boolean_t
glusterd_check_volume_exists (char *volname);

int32_t
glusterd_brickinfo_new (glusterd_brickinfo_t **brickinfo);

int32_t
glusterd_brickinfo_from_brick (char *brick, glusterd_brickinfo_t **brickinfo);

int32_t
glusterd_friend_cleanup (glusterd_peerinfo_t *peerinfo);

int32_t
glusterd_peer_destroy (glusterd_peerinfo_t *peerinfo);

int32_t
glusterd_peer_hostname_new (char *hostname, glusterd_peer_hostname_t **name);

int32_t
glusterd_volinfo_find (char *volname, glusterd_volinfo_t **volinfo);

int32_t
glusterd_resolve_brick (glusterd_brickinfo_t *brickinfo);

int32_t
glusterd_volume_start_glusterfs (glusterd_volinfo_t  *volinfo,
                                 glusterd_brickinfo_t   *brickinfo,
                                 int32_t count);

int32_t
glusterd_volume_stop_glusterfs (glusterd_volinfo_t  *volinfo,
                                glusterd_brickinfo_t   *brickinfo,
                                int32_t count);

int32_t
glusterd_volinfo_delete (glusterd_volinfo_t *volinfo);

int32_t
glusterd_brickinfo_delete (glusterd_brickinfo_t *brickinfo);

gf_boolean_t
glusterd_is_cli_op_req (int32_t op);

int32_t
glusterd_brickinfo_get (char *brick, glusterd_volinfo_t *volinfo,
                        glusterd_brickinfo_t **brickinfo);
int32_t
glusterd_is_local_addr (char *hostname);
#endif
