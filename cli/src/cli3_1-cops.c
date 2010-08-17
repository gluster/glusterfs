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

#include "cli.h"
#include "compat-errno.h"
#include "cli-cmd.h"
#include <sys/uio.h>

#include "cli1-xdr.h"
#include "cli1.h"
#include "protocol-common.h"
#include "cli-mem-types.h"
#include "compat.h"

extern rpc_clnt_prog_t *cli_rpc_prog;
extern int      cli_op_ret;

char *cli_volume_type[] = {"None",
                           "Stripe",
                           "Replicate"
};


char *cli_volume_status[] = {"Created",
                             "Started",
                             "Stopped"
};

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
        cli_out ("Probe %s", (rsp.op_ret) ? "unsuccessful": "successful");


        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
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

        ret = gf_xdr_to_cli_deprobe_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                //rsp.op_ret   = -1;
                //rsp.op_errno = EINVAL;
                goto out;
        }

        gf_log ("cli", GF_LOG_NORMAL, "Received resp to deprobe");
        cli_out ("Detach %s", (rsp.op_ret) ? "unsuccessful": "successful");


        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
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

        ret = rsp.op_ret;

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
        cli_cmd_broadcast_response (ret);
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
        char                       key[1024] = {0,};
        int32_t                    status = 0;
        int32_t                    type = 0;
        int32_t                    brick_count = 0;
        char                       *brick = NULL;
        int32_t                    j = 1;

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
                        cli_out ("");
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

                        cli_out ("Volume Name: %s", volname);
                        cli_out ("Type: %s", cli_volume_type[type]);
                        cli_out ("Status: %s", cli_volume_status[status], brick_count);
                        cli_out ("Number of Bricks: %d", brick_count);
                        j = 1;

                        if (brick_count)
                                cli_out ("Bricks:");

                        while ( j <= brick_count) {
                                snprintf (key, 1024, "volume%d.brick%d",
                                          i, j);
                                ret = dict_get_str (dict, key, &brick);
                                if (ret)
                                        goto out;
                                cli_out ("Brick%d: %s", j, brick);
                                j++;
                        }
                        i++;
                }
        } else {
                ret = -1;
                goto out;
        }


        ret = 0;

out:
        cli_cmd_broadcast_response (ret);
        if (ret)
                cli_out ("Command Execution Failed");

        if (dict)
                dict_destroy (dict);

        gf_log ("", GF_LOG_NORMAL, "Returning: %d", ret);
        return ret;
}

int
gf_cli3_1_create_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_create_vol_rsp  rsp   = {0,};
        int                     ret   = 0;
        cli_local_t             *local = NULL;
        char                    *volname = NULL;
        dict_t                  *dict = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_create_vol_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        local = ((call_frame_t *) (myframe))->local;

        dict = local->u.create_vol.dict;

        ret = dict_get_str (dict, "volname", &volname);

        gf_log ("cli", GF_LOG_NORMAL, "Received resp to create volume");
        cli_out ("Creation of volume %s has been %s", volname,
                        (rsp.op_ret) ? "unsuccessful": "successful");

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int
gf_cli3_1_delete_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_delete_vol_rsp  rsp   = {0,};
        int                     ret   = 0;
        cli_local_t             *local = NULL;
        char                    *volname = NULL;
        call_frame_t            *frame = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_delete_vol_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        frame = myframe;
        local = frame->local;

        if (local)
                volname = local->u.delete_vol.volname;


        gf_log ("cli", GF_LOG_NORMAL, "Received resp to delete volume");
        cli_out ("Deleting volume %s has been %s", volname,
                 (rsp.op_ret) ? "unsuccessful": "successful");

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        gf_log ("", GF_LOG_NORMAL, "Returning with %d", ret);
        return ret;
}

int
gf_cli3_1_start_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_start_vol_rsp   rsp   = {0,};
        int                     ret   = 0;
        cli_local_t             *local = NULL;
        char                    *volname = NULL;
        call_frame_t            *frame = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_start_vol_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        frame = myframe;

        if (frame)
                local = frame->local;

        if (local)
                volname = local->u.start_vol.volname;

        gf_log ("cli", GF_LOG_NORMAL, "Received resp to start volume");
        cli_out ("Starting volume %s has been %s", volname,
                (rsp.op_ret) ? "unsuccessful": "successful");

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int
gf_cli3_1_stop_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_stop_vol_rsp  rsp   = {0,};
        int                   ret   = 0;
        cli_local_t           *local = NULL;
        char                  *volname = NULL;
        call_frame_t          *frame = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_stop_vol_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        frame = myframe;

        if (frame)
                local = frame->local;

        if (local)
                volname = local->u.start_vol.volname;

        gf_log ("cli", GF_LOG_NORMAL, "Received resp to stop volume");
        cli_out ("Stopping volume %s has been %s", volname,
                (rsp.op_ret) ? "unsuccessful": "successful");

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int
gf_cli3_1_defrag_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_defrag_vol_rsp  rsp     = {0,};
        cli_local_t            *local   = NULL;
        char                   *volname = NULL;
        call_frame_t           *frame   = NULL;
        int                     cmd     = 0;
        int                     ret     = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = gf_xdr_to_cli_defrag_vol_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        frame = myframe;

        if (frame)
                local = frame->local;

        if (local) {
                volname = local->u.defrag_vol.volname;
                cmd = local->u.defrag_vol.cmd;
        }
        if (cmd == GF_DEFRAG_CMD_START) {
                cli_out ("starting defrag on volume %s has been %s", volname,
                         (rsp.op_ret) ? "unsuccessful": "successful");
        }
        if (cmd == GF_DEFRAG_CMD_STOP) {
                if (rsp.op_ret == -1)
                        cli_out ("'defrag volume %s stop' failed", volname);
                else
                        cli_out ("stopped defrag process of volume %s \n"
                                 "(after rebalancing %"PRId64" files totaling "
                                 "%"PRId64" bytes)", volname, rsp.files, rsp.size);
        }
        if (cmd == GF_DEFRAG_CMD_STATUS) {
                if (rsp.op_ret == -1)
                        cli_out ("failed to get the status of defrag process");
                else {
                        cli_out ("rebalanced %"PRId64" files of size %"PRId64
                                 " (total files scanned %"PRId64")",
                                 rsp.files, rsp.size, rsp.lookedup_files);
                }
        }

        if (volname)
                GF_FREE (volname);

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
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
        cli_out ("Rename volume %s", (rsp.op_ret) ? "unsuccessful":
                                        "successful");

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
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
        cli_out ("Set volume %s", (rsp.op_ret) ? "unsuccessful":
                                        "successful");

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
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
        cli_out ("Add Brick %s", (rsp.op_ret) ? "unsuccessful":
                                        "successful");

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
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
        cli_out ("Remove Brick %s", (rsp.op_ret) ? "unsuccessful":
                                        "successful");

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}



int
gf_cli3_1_replace_brick_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_replace_brick_rsp        rsp              = {0,};
        int                              ret              = 0;
        cli_local_t                     *local            = NULL;
        call_frame_t                    *frame            = NULL;
        dict_t                          *dict             = NULL;
        char                            *src_brick        = NULL;
        char                            *dst_brick        = NULL;
        gf1_cli_replace_op               replace_op       = 0;
        char                            *rb_operation_str = NULL;
        char                             cmd_str[8192]    = {0,};

        if (-1 == req->rpc_status) {
                goto out;
        }

        frame = (call_frame_t *) myframe;

        ret = gf_xdr_to_cli_replace_brick_rsp (*iov, &rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        local = frame->local;
        GF_ASSERT (local);
        dict = local->u.replace_brick.dict;

        ret = dict_get_int32 (dict, "operation", (int32_t *)&replace_op);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "dict_get on operation failed");
                goto out;
        }

        switch (replace_op) {
        case GF_REPLACE_OP_START:
                rb_operation_str = "Replace brick start operation";
                break;

        case GF_REPLACE_OP_STATUS:
                rb_operation_str = "Replace brick status operation";
                break;

        case GF_REPLACE_OP_PAUSE:
                rb_operation_str = "Replace brick pause operation";
                break;

        case GF_REPLACE_OP_ABORT:
                rb_operation_str = "Replace brick abort operation";
                break;

        case GF_REPLACE_OP_COMMIT:
                rb_operation_str = "Replace brick commit operation";

                ret = dict_get_str (dict, "src-brick", &src_brick);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "dict_get on src-brick failed");
                        goto out;
                }

                ret = dict_get_str (dict, "dst-brick", &dst_brick);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "dict_get on dst-brick failed");
                        goto out;
                }

                snprintf (cmd_str, 4096, "gluster volume replace-brick %s %s %s abort >/dev/null",
                          local->u.replace_brick.volname, src_brick, dst_brick);

                ret = system (cmd_str);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "add brick failed");
                        goto out;
                }

                snprintf (cmd_str, 4096, "gluster volume add-brick %s %s >/dev/null",
                          local->u.replace_brick.volname, dst_brick);

                ret = system (cmd_str);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "add brick failed");
                        goto out;
                }

                snprintf (cmd_str, 4096, "gluster volume remove-brick %s %s >/dev/null",
                          local->u.replace_brick.volname, src_brick);

                ret = system (cmd_str);
                if (ret) {
                        gf_log ("", GF_LOG_DEBUG,
                                "remove brick failed");
                        goto out;
                }

                break;

        default:
                gf_log ("", GF_LOG_DEBUG,
                        "Unknown operation");
                break;
        }


        gf_log ("cli", GF_LOG_NORMAL, "Received resp to replace brick");
        cli_out ("%s %s",
                 rb_operation_str ? rb_operation_str : "Unknown operation",
                 (rsp.op_ret) ? "unsuccessful":
                 "successful");

        ret = rsp.op_ret;

out:
        if (local) {
                dict_unref (local->u.replace_brick.dict);
                GF_FREE (local->u.replace_brick.volname);
                cli_local_wipe (local);
        }

        cli_cmd_broadcast_response (ret);
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

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GD_MGMT_CLI_PROBE, NULL, gf_xdr_from_cli_probe_req,
                              this, gf_cli3_1_probe_cbk);

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

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GD_MGMT_CLI_DEPROBE, NULL,
                              gf_xdr_from_cli_deprobe_req,
                              this, gf_cli3_1_deprobe_cbk);

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

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GD_MGMT_CLI_LIST_FRIENDS, NULL,
                              gf_xdr_from_cli_peer_list_req,
                              this, gf_cli3_1_list_friends_cbk);

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

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GD_MGMT_CLI_GET_VOLUME, NULL,
                              gf_xdr_from_cli_get_vol_req,
                              this, gf_cli3_1_get_volume_cbk);

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
        cli_local_t             *local = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = dict_ref ((dict_t *)data);

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

        local = cli_local_get ();

        if (local) {
                local->u.create_vol.dict = dict;
                frame->local = local;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GD_MGMT_CLI_CREATE_VOLUME, NULL,
                              gf_xdr_from_cli_create_vol_req,
                              this, gf_cli3_1_create_volume_cbk);



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
        cli_local_t             *local = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        local = cli_local_get ();

        if (local) {
                local->u.delete_vol.volname = data;
                frame->local = local;
        }

        req.volname = data;

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
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
        cli_local_t             *local = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        local = cli_local_get ();

        if (local) {
                local->u.start_vol.volname = data;
                frame->local = local;
        }

        req.volname = data;

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
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
        cli_local_t            *local = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        req.volname = data;

        local = cli_local_get ();

        if (local) {
                local->u.stop_vol.volname = data;
                frame->local = local;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
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
        gf1_cli_defrag_vol_req  req     = {0,};
        int                     ret     = 0;
        cli_local_t            *local   = NULL;
        char                   *volname = NULL;
        char                   *cmd_str = NULL;
        dict_t                 *dict    = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                gf_log ("", GF_LOG_DEBUG, "error");

        ret = dict_get_str (dict, "command", &cmd_str);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "error");
                goto out;
        }

        if (strncasecmp (cmd_str, "start", 6) == 0) {
                req.cmd = GF_DEFRAG_CMD_START;
        } else if (strncasecmp (cmd_str, "stop", 5) == 0) {
                req.cmd = GF_DEFRAG_CMD_STOP;
        } else if (strncasecmp (cmd_str, "status", 7) == 0) {
                req.cmd = GF_DEFRAG_CMD_STATUS;
        }


        local = cli_local_get ();

        if (local) {
                local->u.defrag_vol.volname = gf_strdup (volname);
                local->u.defrag_vol.cmd = req.cmd;
                frame->local = local;
        }

        req.volname = volname;

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
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

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
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


        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
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

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
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

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
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
        gf1_cli_replace_brick_req   req        = {0,};
        int                         ret        = 0;
        cli_local_t                *local      = NULL;
        dict_t                     *dict       = NULL;
        char                       *src_brick  = NULL;
        char                       *dst_brick  = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

	dict = data;

        local = cli_local_get ();
        if (!local) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                goto out;
        }

        local->u.replace_brick.dict = dict_ref (dict);
        frame->local                = local;

        ret = dict_get_int32 (dict, "operation", (int32_t *)&req.op);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "dict_get on operation failed");
                goto out;
        }
        ret = dict_get_str (dict, "volname", &req.volname);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "dict_get on volname failed");
                goto out;
        }

        local->u.replace_brick.volname = gf_strdup (req.volname);
        if (!local->u.replace_brick.volname) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "src-brick", &src_brick);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "dict_get on src-brick failed");
                goto out;
        }

        ret = dict_get_str (dict, "dst-brick", &dst_brick);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "dict_get on dst-brick failed");
                goto out;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "Recevied command replace-brick %s with "
                "%s with operation=%d", src_brick,
                dst_brick, req.op);


        ret = dict_allocate_and_serialize (dict,
                                           &req.bricks.bricks_val,
                                           (size_t *)&req.bricks.bricks_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get serialized length of dict");
                goto out;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GD_MGMT_CLI_REPLACE_BRICK, NULL,
                              gf_xdr_from_cli_replace_brick_req,
                              this, gf_cli3_1_replace_brick_cbk);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        if (req.bricks.bricks_val) {
                GF_FREE (req.bricks.bricks_val);
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
