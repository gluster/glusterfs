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

#include "cli.h"
#include "compat-errno.h"
#include "cli-cmd.h"
#include <sys/uio.h>

#include "cli1-xdr.h"
#include "cli1.h"
#include "protocol-common.h"

extern rpc_clnt_prog_t *cli_rpc_prog;

int
gf_cli3_1_probe_cbk (struct rpc_req *req, struct iovec *iov,
                        int count, void *myframe)
{
        gf1_cli_probe_rsp    rsp   = {0,};
        int                   ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_probe_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                //rsp.op_ret   = -1;
                //rsp.op_errno = EINVAL;
                goto out;
        }

        gf_log ("cli", GF_LOG_NORMAL, "Received resp to probe");
        cli_out ("Probe %s", (rsp.op_ret) ? "Unsuccessful": "Successful");

        cli_cmd_broadcast_response ();

        ret = 0;

out:
        return ret;
}

int
gf_cli3_1_deprobe_cbk (struct rpc_req *req, struct iovec *iov,
                       int count, void *myframe)
{
        gf1_cli_deprobe_rsp    rsp   = {0,};
        int                   ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_deprobe_req (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                //rsp.op_ret   = -1;
                //rsp.op_errno = EINVAL;
                goto out;
        }

        gf_log ("cli", GF_LOG_NORMAL, "Received resp to deprobe");
        cli_out ("Detach %s", (rsp.op_ret) ? "Unsuccessful": "Successful");

        cli_cmd_broadcast_response ();

        ret = 0;

out:
        return ret;
}

int
gf_cli3_1_list_friends_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_peer_list_rsp      rsp   = {0,};
        int                        ret   = 0;
        dict_t                     *dict = NULL;
        char                       *uuid_buf = NULL;
        char                       *hostname_buf = NULL;
        int32_t                    i = 1;
        char                       key[256] = {0,};
        int32_t                    state = 0;
        int32_t                    port = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_peer_list_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                //rsp.op_ret   = -1;
                //rsp.op_errno = EINVAL;
                goto out;
        }


        gf_log ("cli", GF_LOG_NORMAL, "Received resp to list: %d",
                rsp.op_ret);

        if (!rsp.op_ret) {

                if (!rsp.friends.friends_len) {
                        cli_out ("No peers present");
                        ret = 0;
                        goto out;
                }

                dict = dict_new ();

                if (!dict) {
                        ret = -1;
                        goto out;
                }

                ret = dict_unserialize (rsp.friends.friends_val,
                                        rsp.friends.friends_len,
                                        &dict);

                if (ret) {
                        gf_log ("", GF_LOG_ERROR,
                                        "Unable to allocate memory");
                        goto out;
                }

                ret = dict_get_int32 (dict, "count", &count);

                if (ret) {
                        goto out;
                }

                cli_out ("Number of Peers: %d", count);

                while ( i <= count) {
                        snprintf (key, 256, "friend%d.uuid", i);
                        ret = dict_get_str (dict, key, &uuid_buf);
                        if (ret)
                                goto out;

                        snprintf (key, 256, "friend%d.hostname", i);
                        ret = dict_get_str (dict, key, &hostname_buf);
                        if (ret)
                                goto out;

                        snprintf (key, 256, "friend%d.port", i);
                        ret = dict_get_int32 (dict, key, &port);
                        if (ret)
                                goto out;

                        snprintf (key, 256, "friend%d.state", i);
                        ret = dict_get_int32 (dict, key, &state);
                        if (ret)
                                goto out;

                        cli_out ("hostname:%s, port:%d, uuid:%s, state:%d",
                                 hostname_buf, port, uuid_buf, state);
                        i++;
                }
        } else {
                ret = -1;
                goto out;
        }


        ret = 0;

out:
        if (ret)
                cli_out ("Command Execution Failed");

        if (dict)
                dict_destroy (dict);

        return ret;
}

int
gf_cli3_1_get_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_get_vol_rsp        rsp   = {0,};
        int                        ret   = 0;
        dict_t                     *dict = NULL;
        char                       *volname = NULL;
        int32_t                    i = 1;
        char                       key[256] = {0,};
        int32_t                    status = 0;
        int32_t                    type = 0;
        int32_t                    brick_count = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_get_vol_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                //rsp.op_ret   = -1;
                //rsp.op_errno = EINVAL;
                goto out;
        }


        gf_log ("cli", GF_LOG_NORMAL, "Received resp to get vol: %d",
                rsp.op_ret);

        if (!rsp.op_ret) {

                if (!rsp.volumes.volumes_len) {
                        cli_out ("No volumes present");
                        ret = 0;
                        goto out;
                }

                dict = dict_new ();

                if (!dict) {
                        ret = -1;
                        goto out;
                }

                ret = dict_unserialize (rsp.volumes.volumes_val,
                                        rsp.volumes.volumes_len,
                                        &dict);

                if (ret) {
                        gf_log ("", GF_LOG_ERROR,
                                        "Unable to allocate memory");
                        goto out;
                }

                ret = dict_get_int32 (dict, "count", &count);

                if (ret) {
                        goto out;
                }

                cli_out ("Number of Volumes: %d", count);

                while ( i <= count) {
                        snprintf (key, 256, "volume%d.name", i);
                        ret = dict_get_str (dict, key, &volname);
                        if (ret)
                                goto out;

                        snprintf (key, 256, "volume%d.type", i);
                        ret = dict_get_int32 (dict, key, &type);
                        if (ret)
                                goto out;

                        snprintf (key, 256, "volume%d.status", i);
                        ret = dict_get_int32 (dict, key, &status);
                        if (ret)
                                goto out;

                        snprintf (key, 256, "volume%d.brick_count", i);
                        ret = dict_get_int32 (dict, key, &brick_count);
                        if (ret)
                                goto out;

                        cli_out ("Volume Name:%s, type:%d, status:%d,"
                                  "brick_count: %d",
                                  volname, type, status, brick_count);
                        i++;
                }
        } else {
                ret = -1;
                goto out;
        }


        ret = 0;

out:
        cli_cmd_broadcast_response ();
        if (ret)
                cli_out ("Command Execution Failed");

        if (dict)
                dict_destroy (dict);

        return ret;
}
int
gf_cli3_1_create_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_create_vol_rsp  rsp   = {0,};
        int                     ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_create_vol_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }


        gf_log ("cli", GF_LOG_NORMAL, "Received resp to create volume");
        cli_out ("Create Volume %s", (rsp.op_ret) ? "Unsuccessful":
                                        "Successful");

        ret = 0;

out:
        return ret;
}

int
gf_cli3_1_delete_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_delete_vol_rsp  rsp   = {0,};
        int                     ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_delete_vol_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }


        gf_log ("cli", GF_LOG_NORMAL, "Received resp to delete volume");
        cli_out ("Delete Volume %s", (rsp.op_ret) ? "Unsuccessful":
                                        "Successful");

        ret = 0;

out:
        return ret;
}

int
gf_cli3_1_start_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_start_vol_rsp  rsp   = {0,};
        int                     ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_start_vol_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }


        gf_log ("cli", GF_LOG_NORMAL, "Received resp to start volume");
        cli_out ("Start Volume %s", (rsp.op_ret) ? "Unsuccessful":
                                        "Successful");

        ret = 0;

out:
        return ret;
}

int
gf_cli3_1_stop_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_stop_vol_rsp  rsp   = {0,};
        int                   ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_stop_vol_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }


        gf_log ("cli", GF_LOG_NORMAL, "Received resp to stop volume");
        cli_out ("Delete Volume %s", (rsp.op_ret) ? "Unsuccessful":
                                        "Successful");

        ret = 0;

out:
        return ret;
}

int
gf_cli3_1_defrag_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_defrag_vol_rsp  rsp   = {0,};
        int                     ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_defrag_vol_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }


        gf_log ("cli", GF_LOG_NORMAL, "Received resp to probe");
        cli_out ("Defrag Volume %s", (rsp.op_ret) ? "Unsuccessful":
                                        "Successful");

        ret = 0;

out:
        return ret;
}

int
gf_cli3_1_rename_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_rename_vol_rsp  rsp   = {0,};
        int                     ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_rename_vol_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }


        gf_log ("cli", GF_LOG_NORMAL, "Received resp to probe");
        cli_out ("Rename Volume %s", (rsp.op_ret) ? "Unsuccessful":
                                        "Successful");

        ret = 0;

out:
        return ret;
}

int
gf_cli3_1_set_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_set_vol_rsp  rsp   = {0,};
        int                  ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_set_vol_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }


        gf_log ("cli", GF_LOG_NORMAL, "Received resp to set");
        cli_out ("Set Volume %s", (rsp.op_ret) ? "Unsuccessful":
                                        "Successful");

        ret = 0;

out:
        return ret;
}

int
gf_cli3_1_add_brick_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_add_brick_rsp       rsp   = {0,};
        int                         ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_add_brick_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }


        gf_log ("cli", GF_LOG_NORMAL, "Received resp to add brick");
        cli_out ("Add Brick %s", (rsp.op_ret) ? "Unsuccessful":
                                        "Successful");

        ret = 0;

out:
        return ret;
}


int
gf_cli3_1_remove_brick_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_remove_brick_rsp        rsp   = {0,};
        int                             ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_remove_brick_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        gf_log ("cli", GF_LOG_NORMAL, "Received resp to remove brick");
        cli_out ("Remove Brick %s", (rsp.op_ret) ? "Unsuccessful":
                                        "Successful");

        ret = 0;

out:
        return ret;
}


int
gf_cli3_1_replace_brick_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_replace_brick_rsp       rsp   = {0,};
        int                             ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_replace_brick_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }


        gf_log ("cli", GF_LOG_NORMAL, "Received resp to replace brick");
        cli_out ("Replace Brick %s", (rsp.op_ret) ? "Unsuccessful":
                                        "Successful");

        ret = 0;

out:
        return ret;
}

int32_t
gf_cli3_1_probe (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        gf1_cli_probe_req  req      = {0,};
        int                ret      = 0;
        dict_t            *dict     = NULL;
        char              *hostname = NULL;
        int                port     = 0;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;
        ret = dict_get_str (dict, "hostname", &hostname);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "port", &port);
        if (ret)
                port = CLI_GLUSTERD_PORT;

        req.hostname = hostname;
        req.port     = port;

        ret = cli_submit_request (&req, frame, cli_rpc_prog,
                                   GD_MGMT_CLI_PROBE, NULL, gf_xdr_from_cli_probe_req,
                                   this, gf_cli3_1_probe_cbk);

        if (!ret) {
                //ret = cli_cmd_await_response ();
        }
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
gf_cli3_1_deprobe (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        gf1_cli_deprobe_req  req      = {0,};
        int                  ret      = 0;
        dict_t              *dict     = NULL;
        char                *hostname = NULL;
        int                  port     = 0;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;
        ret = dict_get_str (dict, "hostname", &hostname);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "port", &port);
        if (ret)
                port = CLI_GLUSTERD_PORT;

        req.hostname = hostname;
        req.port     = port;

        ret = cli_submit_request (&req, frame, cli_rpc_prog,
                                   GD_MGMT_CLI_DEPROBE, NULL,
                                   gf_xdr_from_cli_deprobe_req,
                                   this, gf_cli3_1_deprobe_cbk);

        if (!ret) {
                //ret = cli_cmd_await_response ();
        }
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
gf_cli3_1_list_friends (call_frame_t *frame, xlator_t *this,
                        void *data)
{
        gf1_cli_peer_list_req   req = {0,};
        int                     ret = 0;

        if (!frame || !this) {
                ret = -1;
                goto out;
        }

        req.flags = GF_CLI_LIST_ALL;

        ret = cli_submit_request (&req, frame, cli_rpc_prog,
                                   GD_MGMT_CLI_LIST_FRIENDS, NULL,
                                   gf_xdr_from_cli_peer_list_req,
                                   this, gf_cli3_1_list_friends_cbk);

        if (!ret) {
                //ret = cli_cmd_await_response ();
        }
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
gf_cli3_1_get_volume (call_frame_t *frame, xlator_t *this,
                        void *data)
{
        gf1_cli_get_vol_req     req = {0,};
        int                     ret = 0;

        if (!frame || !this) {
                ret = -1;
                goto out;
        }

        req.flags = GF_CLI_GET_VOLUME_ALL;

        ret = cli_submit_request (&req, frame, cli_rpc_prog,
                                   GD_MGMT_CLI_GET_VOLUME, NULL,
                                   gf_xdr_from_cli_get_vol_req,
                                   this, gf_cli3_1_get_volume_cbk);

        if (!ret) {
                ret = cli_cmd_await_response ();
        }
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
gf_cli3_1_create_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf1_cli_create_vol_req  req = {0,};
        int                     ret = 0;
        dict_t                  *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_get_str (dict, "volname", &req.volname);

        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "type", (int32_t *)&req.type);

        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "count", &req.count);

        if (ret)
                goto out;

        ret = dict_allocate_and_serialize (dict,
                                           &req.bricks.bricks_val,
                                           (size_t *)&req.bricks.bricks_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get serialized length of dict");
                goto out;
        }

        ret = cli_submit_request (&req, frame, cli_rpc_prog,
                                   GD_MGMT_CLI_CREATE_VOLUME, NULL,
                                   gf_xdr_from_cli_create_vol_req,
                                   this, gf_cli3_1_create_volume_cbk);

        if (!ret) {
                //ret = cli_cmd_await_response ();
        }


out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        if (req.bricks.bricks_val) {
                GF_FREE (req.bricks.bricks_val);
        }

        return ret;
}

int32_t
gf_cli3_1_delete_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf1_cli_delete_vol_req  req = {0,};
        int                     ret = 0;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        req.volname = data;

        ret = cli_submit_request (&req, frame, cli_rpc_prog,
                                   GD_MGMT_CLI_DELETE_VOLUME, NULL,
                                   gf_xdr_from_cli_delete_vol_req,
                                   this, gf_cli3_1_delete_volume_cbk);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli3_1_start_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf1_cli_start_vol_req   req = {0,};
        int                     ret = 0;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        req.volname = data;

        ret = cli_submit_request (&req, frame, cli_rpc_prog,
                                   GD_MGMT_CLI_START_VOLUME, NULL,
                                   gf_xdr_from_cli_start_vol_req,
                                   this, gf_cli3_1_start_volume_cbk);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli3_1_stop_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf1_cli_stop_vol_req   req = {0,};
        int                    ret = 0;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        req.volname = data;

        ret = cli_submit_request (&req, frame, cli_rpc_prog,
                                   GD_MGMT_CLI_STOP_VOLUME, NULL,
                                   gf_xdr_from_cli_stop_vol_req,
                                   this, gf_cli3_1_stop_volume_cbk);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli3_1_defrag_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf1_cli_defrag_vol_req   req = {0,};
        int                    ret = 0;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        req.volname = data;

        ret = cli_submit_request (&req, frame, cli_rpc_prog,
                                   GD_MGMT_CLI_DEFRAG_VOLUME, NULL,
                                   gf_xdr_from_cli_defrag_vol_req,
                                   this, gf_cli3_1_defrag_volume_cbk);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli3_1_rename_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf1_cli_rename_vol_req  req = {0,};
        int                     ret = 0;
        dict_t                  *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_get_str (dict, "old-volname", &req.old_volname);

        if (ret)
                goto out;

        ret = dict_get_str (dict, "new-volname", &req.new_volname);

        if (ret)
                goto out;

        ret = cli_submit_request (&req, frame, cli_rpc_prog,
                                   GD_MGMT_CLI_RENAME_VOLUME, NULL,
                                   gf_xdr_from_cli_rename_vol_req,
                                   this, gf_cli3_1_rename_volume_cbk);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli3_1_set_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf1_cli_set_vol_req     req = {0,};
        int                     ret = 0;
        dict_t                  *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_get_str (dict, "volname", &req.volname);

        if (ret)
                goto out;

        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get serialized length of dict");
                goto out;
        }


        ret = cli_submit_request (&req, frame, cli_rpc_prog,
                                   GD_MGMT_CLI_SET_VOLUME, NULL,
                                   gf_xdr_from_cli_set_vol_req,
                                   this, gf_cli3_1_set_volume_cbk);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli3_1_add_brick (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf1_cli_add_brick_req  req = {0,};
        int                     ret = 0;
        dict_t                  *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_get_str (dict, "volname", &req.volname);

        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "type", (int32_t *)&req.type);

        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "count", &req.count);

        if (ret)
                goto out;

        ret = dict_allocate_and_serialize (dict,
                                           &req.bricks.bricks_val,
                                           (size_t *)&req.bricks.bricks_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get serialized length of dict");
                goto out;
        }

        ret = cli_submit_request (&req, frame, cli_rpc_prog,
                                   GD_MGMT_CLI_ADD_BRICK, NULL,
                                   gf_xdr_from_cli_add_brick_req,
                                   this, gf_cli3_1_add_brick_cbk);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        if (req.bricks.bricks_val) {
                GF_FREE (req.bricks.bricks_val);
        }

        return ret;
}

int32_t
gf_cli3_1_remove_brick (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf1_cli_remove_brick_req  req = {0,};
        int                       ret = 0;
        dict_t                    *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_get_str (dict, "volname", &req.volname);

        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "type", (int32_t *)&req.type);

        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "count", &req.count);

        if (ret)
                goto out;

        ret = dict_allocate_and_serialize (dict,
                                           &req.bricks.bricks_val,
                                           (size_t *)&req.bricks.bricks_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get serialized length of dict");
                goto out;
        }

        ret = cli_submit_request (&req, frame, cli_rpc_prog,
                                   GD_MGMT_CLI_REMOVE_BRICK, NULL,
                                   gf_xdr_from_cli_remove_brick_req,
                                   this, gf_cli3_1_remove_brick_cbk);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        if (req.bricks.bricks_val) {
                GF_FREE (req.bricks.bricks_val);
        }

        return ret;
}

int32_t
gf_cli3_1_replace_brick (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf1_cli_replace_brick_req  req = {0,};
        int                        ret = 0;
        dict_t                     *dict = NULL;
        char                       *src_brick = NULL;
        char                       *dst_brick = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_get_str (dict, "volname", &req.volname);

        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "operation", (int32_t *)&req.op);

        if (ret)
                goto out;

        if (GF_REPLACE_OP_START == req.op) {
                ret = dict_get_str (dict, "src-brick", &src_brick);

                if (ret)
                        goto out;

                req.src_brick.src_brick_len = strlen (src_brick);
                req.src_brick.src_brick_val = src_brick;

                ret = dict_get_str (dict, "src-brick", &dst_brick);

                if (ret)
                        goto out;

                req.dst_brick.dst_brick_len = strlen (dst_brick);
                req.dst_brick.dst_brick_val = dst_brick;
        }

        ret = cli_submit_request (&req, frame, cli_rpc_prog,
                                   GD_MGMT_CLI_REPLACE_BRICK, NULL,
                                   gf_xdr_from_cli_replace_brick_req,
                                   this, gf_cli3_1_replace_brick_cbk);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        if (req.src_brick.src_brick_val) {
                GF_FREE (req.src_brick.src_brick_val);
        }

        if (req.dst_brick.dst_brick_val) {
                GF_FREE (req.dst_brick.dst_brick_val);
        }

        return ret;
}

struct rpc_clnt_procedure gluster3_1_cli_actors[GF1_CLI_MAXVALUE] = {
        [GF1_CLI_NULL]        = {"NULL", NULL },
        [GF1_CLI_PROBE]  = { "PROBE_QUERY",  gf_cli3_1_probe},
        [GF1_CLI_DEPROBE]  = { "DEPROBE_QUERY",  gf_cli3_1_deprobe},
        [GF1_CLI_LIST_FRIENDS]  = { "LIST_FRIENDS",  gf_cli3_1_list_friends},
        [GF1_CLI_CREATE_VOLUME] = {"CREATE_VOLUME", gf_cli3_1_create_volume},
        [GF1_CLI_DELETE_VOLUME] = {"DELETE_VOLUME", gf_cli3_1_delete_volume},
        [GF1_CLI_START_VOLUME] = {"START_VOLUME", gf_cli3_1_start_volume},
        [GF1_CLI_STOP_VOLUME] = {"STOP_VOLUME", gf_cli3_1_stop_volume},
        [GF1_CLI_RENAME_VOLUME] = {"RENAME_VOLUME", gf_cli3_1_rename_volume},
        [GF1_CLI_DEFRAG_VOLUME] = {"DEFRAG_VOLUME", gf_cli3_1_defrag_volume},
        [GF1_CLI_GET_VOLUME] = {"GET_VOLUME", gf_cli3_1_get_volume},
        [GF1_CLI_SET_VOLUME] = {"SET_VOLUME", gf_cli3_1_set_volume},
        [GF1_CLI_ADD_BRICK] = {"ADD_BRICK", gf_cli3_1_add_brick},
        [GF1_CLI_REMOVE_BRICK] = {"REMOVE_BRICK", gf_cli3_1_remove_brick},
        [GF1_CLI_REPLACE_BRICK] = {"REPLACE_BRICK", gf_cli3_1_replace_brick},
};

struct rpc_clnt_program cli3_1_prog = {
        .progname = "CLI 3.1",
        .prognum  = GLUSTER3_1_CLI_PROGRAM,
        .progver  = GLUSTER3_1_CLI_VERSION,
        .proctable    = gluster3_1_cli_actors,
        .numproc  = GLUSTER3_1_CLI_PROCCNT,
};
