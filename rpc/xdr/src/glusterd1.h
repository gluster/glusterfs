/*
  Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
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


#ifndef _GLUSTERD1_H
#define _GLUSTERD1_H

#include <sys/uio.h>

#include "xdr-generic.h"
#include "glusterd1-xdr.h"

ssize_t
gd_xdr_to_mgmt_probe_req (struct iovec inmsg, void *args);

ssize_t
gd_xdr_to_mgmt_probe_rsp (struct iovec inmsg, void *args);

ssize_t
gd_xdr_serialize_mgmt_probe_rsp (struct iovec outmsg, void *rsp);

ssize_t
gd_xdr_from_mgmt_probe_req (struct iovec outmsg, void *req);

ssize_t
gd_xdr_to_mgmt_friend_req (struct iovec inmsg, void *args);

ssize_t
gd_xdr_to_mgmt_friend_rsp (struct iovec inmsg, void *args);

ssize_t
gd_xdr_serialize_mgmt_friend_rsp (struct iovec outmsg, void *rsp);

ssize_t
gd_xdr_from_mgmt_friend_req (struct iovec outmsg, void *req);

ssize_t
gd_xdr_to_mgmt_cluster_lock_req (struct iovec inmsg, void *args);

ssize_t
gd_xdr_to_mgmt_cluster_lock_rsp (struct iovec inmsg, void *args);

ssize_t
gd_xdr_serialize_mgmt_cluster_lock_rsp (struct iovec outmsg, void *rsp);

ssize_t
gd_xdr_from_mgmt_cluster_lock_req (struct iovec outmsg, void *req);

ssize_t
gd_xdr_to_mgmt_cluster_unlock_req (struct iovec inmsg, void *args);

ssize_t
gd_xdr_to_mgmt_cluster_unlock_rsp (struct iovec inmsg, void *args);

ssize_t
gd_xdr_serialize_mgmt_cluster_unlock_rsp (struct iovec outmsg, void *rsp);

ssize_t
gd_xdr_from_mgmt_cluster_unlock_req (struct iovec outmsg, void *req);

ssize_t
gd_xdr_to_mgmt_stage_op_req (struct iovec inmsg, void *args);

ssize_t
gd_xdr_to_mgmt_stage_op_rsp (struct iovec inmsg, void *args);

ssize_t
gd_xdr_serialize_mgmt_stage_op_rsp (struct iovec outmsg, void *rsp);

ssize_t
gd_xdr_from_mgmt_stage_op_req (struct iovec outmsg, void *req);

ssize_t
gd_xdr_to_mgmt_commit_op_req (struct iovec inmsg, void *args);

ssize_t
gd_xdr_to_mgmt_commit_op_rsp (struct iovec inmsg, void *args);

ssize_t
gd_xdr_serialize_mgmt_commit_op_rsp (struct iovec outmsg, void *rsp);

ssize_t
gd_xdr_from_mgmt_commit_op_req (struct iovec outmsg, void *req);

ssize_t
gd_xdr_to_mgmt_friend_update (struct iovec outmsg, void *req);

ssize_t
gd_xdr_from_mgmt_friend_update (struct iovec outmsg, void *req);

ssize_t
gd_xdr_serialize_mgmt_friend_update_rsp (struct iovec outmsg, void *rsp);

ssize_t
gd_xdr_to_mgmt_friend_update_rsp (struct iovec inmsg, void *args);

ssize_t
gd_xdr_to_mgmt_brick_op_req (struct iovec inmsg, void *args);

ssize_t
gd_xdr_from_mgmt_brick_op_req (struct iovec outmsg, void *req);

ssize_t
gd_xdr_to_mgmt_brick_op_rsp (struct iovec inmsg, void *args);

ssize_t
gd_xdr_serialize_mgmt_brick_op_rsp (struct iovec outmsg, void *rsp);

#endif /* !_MSG_GD_XDR_H */
