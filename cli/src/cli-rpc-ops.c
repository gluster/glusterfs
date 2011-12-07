/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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

#ifndef GSYNC_CONF
#define GSYNC_CONF GEOREP"/gsyncd.conf"
#endif
#define DEFAULT_LOG_FILE_DIRECTORY      DATADIR "/log/glusterfs"

/* Widths of various columns in top read/write-perf output
 * Total width of top read/write-perf should be 80 chars
 * including one space between column
 */
#define VOL_TOP_PERF_FILENAME_DEF_WIDTH 47
#define VOL_TOP_PERF_FILENAME_ALT_WIDTH 44
#define VOL_TOP_PERF_SPEED_WIDTH        4
#define VOL_TOP_PERF_TIME_WIDTH         26

#include "cli.h"
#include "compat-errno.h"
#include "cli-cmd.h"
#include <sys/uio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "protocol-common.h"
#include "cli-mem-types.h"
#include "compat.h"

#include "syscall.h"
#include "glusterfs3.h"
#include "portmap-xdr.h"

#include "run.h"

extern rpc_clnt_prog_t *cli_rpc_prog;
extern int              cli_op_ret;
extern int              connected;

char *cli_volume_type[] = {"Distribute",
                           "Stripe",
                           "Replicate",
                           "Striped-Replicate",
                           "Distributed-Stripe",
                           "Distributed-Replicate",
                           "Distributed-Striped-Replicate",
};


char *cli_volume_status[] = {"Created",
                             "Started",
                             "Stopped"
};

int32_t
gf_cli3_1_get_volume (call_frame_t *frame, xlator_t *this,
                      void *data);


rpc_clnt_prog_t cli_handshake_prog = {
        .progname  = "cli handshake",
        .prognum   = GLUSTER_HNDSK_PROGRAM,
        .progver   = GLUSTER_HNDSK_VERSION,
};

rpc_clnt_prog_t cli_pmap_prog = {
        .progname   = "cli portmap",
        .prognum    = GLUSTER_PMAP_PROGRAM,
        .progver    = GLUSTER_PMAP_VERSION,
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

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf1_cli_probe_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                //rsp.op_ret   = -1;
                //rsp.op_errno = EINVAL;
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to probe");
	 if (!rsp.op_ret) {
	 	switch (rsp.op_errno) {
		 	case GF_PROBE_SUCCESS:
		      		cli_out ("Probe successful");
		      		break;
	 	 	case GF_PROBE_LOCALHOST:
		      		cli_out ("Probe on localhost not needed");
		      		break;
			case GF_PROBE_FRIEND:
				cli_out ("Probe on host %s port %d already"
					 " in peer list", rsp.hostname, rsp.port);
				break;
		 	default:
		      		cli_out ("Probe returned with unknown errno %d",
					rsp.op_errno);
		      		break;
	 	}
	 }

        if (rsp.op_ret) {
                switch (rsp.op_errno) {
                        case GF_PROBE_ANOTHER_CLUSTER:
                                cli_out ("%s is already part of "
                                         "another cluster", rsp.hostname);
                                break;
                        case GF_PROBE_VOLUME_CONFLICT:
                                cli_out ("Atleast one volume on %s conflicts "
                                         "with existing volumes in the "
                                         "cluster", rsp.hostname);
                                break;
                        case GF_PROBE_UNKNOWN_PEER:
                                cli_out ("%s responded with 'unknown peer' error, "
                                         "this could happen if %s doesn't have"
                                         " localhost in its peer database",
                                         rsp.hostname, rsp.hostname);
                                break;
                        case GF_PROBE_ADD_FAILED:
                                cli_out ("Failed to add peer information "
                                         "on %s" , rsp.hostname);
                                break;

                        default:
                                cli_out ("Probe unsuccessful\nProbe returned "
                                         "with unknown errno %d", rsp.op_errno);
                                break;
                }
                gf_log ("glusterd",GF_LOG_ERROR,"Probe failed with op_ret %d"
                        " and op_errno %d", rsp.op_ret, rsp.op_errno);
        }
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

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf1_cli_deprobe_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                //rsp.op_ret   = -1;
                //rsp.op_errno = EINVAL;
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to deprobe");
        if (rsp.op_ret) {
                switch (rsp.op_errno) {
                        case GF_DEPROBE_LOCALHOST:
                                cli_out ("%s is localhost",
                                         rsp.hostname);
                                break;
                        case GF_DEPROBE_NOT_FRIEND:
                                cli_out ("%s is not part of cluster",
                                         rsp.hostname);
                                break;
                        case GF_DEPROBE_BRICK_EXIST:
                                cli_out ("Brick(s) with the peer %s exist in "
                                         "cluster", rsp.hostname);
                                break;
                        case GF_DEPROBE_FRIEND_DOWN:
                                cli_out ("One of the peers is probably down."
                                         " Check with 'peer status'.");
                                break;
                        default:
                                cli_out ("Detach unsuccessful\nDetach returned "
                                         "with unknown errno %d",
                                         rsp.op_errno);
                                break;
                }
                gf_log ("glusterd",GF_LOG_ERROR,"Detach failed with op_ret %d"
                        " and op_errno %d", rsp.op_ret, rsp.op_errno);
        } else {
                cli_out ("Detach successful");
        }


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
        char                       *state = NULL;
        int32_t                    port = 0;
        int32_t                    connected = 0;
        char                       *connected_str = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf1_cli_peer_list_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                //rsp.op_ret   = -1;
                //rsp.op_errno = EINVAL;
                goto out;
        }


        gf_log ("cli", GF_LOG_INFO, "Received resp to list: %d",
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

                        snprintf (key, 256, "friend%d.connected", i);
                        ret = dict_get_int32 (dict, key, &connected);
                        if (ret)
                                goto out;
                        if (connected)
                                connected_str = "Connected";
                        else
                                connected_str = "Disconnected";

                        snprintf (key, 256, "friend%d.port", i);
                        ret = dict_get_int32 (dict, key, &port);
                        if (ret)
                                goto out;

                        snprintf (key, 256, "friend%d.state", i);
                        ret = dict_get_str (dict, key, &state);
                        if (ret)
                                goto out;

                        if (!port) {
                                cli_out ("\nHostname: %s\nUuid: %s\nState: %s "
                                         "(%s)",
                                         hostname_buf, uuid_buf, state,
                                         connected_str);
                        } else {
                                cli_out ("\nHostname: %s\nPort: %d\nUuid: %s\n"
                                         "State: %s (%s)", hostname_buf, port,
                                         uuid_buf, state, connected_str);
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
                cli_out ("Peer status unsuccessful");

        if (dict)
                dict_destroy (dict);

        return ret;
}

void
cli_out_options ( char *substr, char *optstr, char *valstr)
{
        char                    *ptr1 = NULL;
        char                    *ptr2 = NULL;

        ptr1 = substr;
        ptr2 = optstr;

        while (ptr1)
        {
                if (*ptr1 != *ptr2)
                        break;
                ptr1++;
                ptr2++;
                if (!ptr1)
                        return;
                if (!ptr2)
                        return;
        }

        if (*ptr2 == '\0')
                return;
        cli_out ("%s: %s",ptr2 , valstr);
}


int
gf_cli3_1_get_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp                 rsp   = {0,};
        int                        ret   = 0;
        dict_t                     *dict = NULL;
        char                       *volname = NULL;
        int32_t                    i = 0;
        char                       key[1024] = {0,};
        int32_t                    status = 0;
        int32_t                    type = 0;
        int32_t                    brick_count = 0;
        int32_t                    dist_count = 0;
        int32_t                    stripe_count = 0;
        int32_t                    replica_count = 0;
        int32_t                    vol_type = 0;
        char                       *brick = NULL;
        int32_t                    j = 1;
        cli_local_t                *local = NULL;
        int32_t                    transport = 0;
        data_pair_t                *pairs = NULL;
        char                       *ptr = NULL;
        data_t                     *value = NULL;
        int                        opt_count = 0;
        int                        k = 0;
        char                       err_str[2048] = {0};

        snprintf (err_str, sizeof (err_str), "Volume info unsuccessful");
        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                //rsp.op_ret   = -1;
                //rsp.op_errno = EINVAL;
                goto out;
        }


        gf_log ("cli", GF_LOG_INFO, "Received resp to get vol: %d",
                rsp.op_ret);

        if (!rsp.op_ret) {

                if (!rsp.dict.dict_len) {
                        cli_out ("No volumes present");
                        ret = 0;
                        goto out;
                }

                dict = dict_new ();

                if (!dict) {
                        ret = -1;
                        goto out;
                }

                ret = dict_unserialize (rsp.dict.dict_val,
                                        rsp.dict.dict_len,
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

                local = ((call_frame_t *)myframe)->local;
                //cli_out ("Number of Volumes: %d", count);

                if (!count && (local->get_vol.flags ==
                                        GF_CLI_GET_NEXT_VOLUME)) {
                        local->get_vol.volname = NULL;
                        ret = 0;
                        goto out;
                } else if (!count && (local->get_vol.flags ==
                                        GF_CLI_GET_VOLUME)) {
                        snprintf (err_str, sizeof (err_str),
                                  "Volume %s does not exist",
                                  local->get_vol.volname);
                        ret = -1;
                        goto out;
                }

                while ( i < count) {
                        cli_out (" ");
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

                        snprintf (key, 256, "volume%d.dist_count", i);
                        ret = dict_get_int32 (dict, key, &dist_count);
                        if (ret)
                                goto out;

                        snprintf (key, 256, "volume%d.stripe_count", i);
                        ret = dict_get_int32 (dict, key, &stripe_count);
                        if (ret)
                                goto out;

                        snprintf (key, 256, "volume%d.replica_count", i);
                        ret = dict_get_int32 (dict, key, &replica_count);
                        if (ret)
                                goto out;

                        snprintf (key, 256, "volume%d.transport", i);
                        ret = dict_get_int32 (dict, key, &transport);
                        if (ret)
                                goto out;

                        vol_type = type;

                        // Distributed (stripe/replicate/stripe-replica) setups
                        if ((type > 0) && ( dist_count < brick_count))
                                vol_type = type + 3;

                        cli_out ("Volume Name: %s", volname);
                        cli_out ("Type: %s", cli_volume_type[vol_type]);
                        cli_out ("Status: %s", cli_volume_status[status]);

                        if (type == GF_CLUSTER_TYPE_STRIPE_REPLICATE)
                                cli_out ("Number of Bricks: %d x %d x %d = %d",
                                         (brick_count / dist_count),
                                         stripe_count,
                                         replica_count,
                                         brick_count);
                        else if (type == GF_CLUSTER_TYPE_NONE)
                                cli_out ("Number of Bricks: %d",
                                         brick_count);
                        else
                                /* For both replicate and stripe, dist_count is
                                   good enough */
                                cli_out ("Number of Bricks: %d x %d = %d",
                                         (brick_count / dist_count),
                                         dist_count,
                                         brick_count);


                        cli_out ("Transport-type: %s",
                                 ((transport == 0)?"tcp":
                                   (transport == 1)?"rdma":
                                  "tcp,rdma"));
                        j = 1;


                        GF_FREE (local->get_vol.volname);
                        local->get_vol.volname = gf_strdup (volname);

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
                        pairs = dict->members_list;
                        if (!pairs) {
                                ret = -1;
                                goto out;
                        }

                        snprintf (key, 256, "volume%d.opt_count",i);
                        ret = dict_get_int32 (dict, key, &opt_count);
                        if (ret)
                            goto out;

                        if (!opt_count)
                            goto out;

                        cli_out ("Options Reconfigured:");
                        k = 0;
                        while ( k < opt_count) {

                                snprintf (key, 256, "volume%d.option.",i);
                                while (pairs) {
                                        ptr = strstr (pairs->key, "option.");
                                        if (ptr) {
                                                value = pairs->value;
                                                if (!value) {
                                                        ret = -1;
                                                        goto out;
                                                }
                                                cli_out_options (key, pairs->key,
                                                                 value->data);
                                        }
                                        pairs = pairs->next;
                                }
                                k++;
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
                cli_out ("%s", err_str);

        if (dict)
                dict_destroy (dict);

        gf_log ("", GF_LOG_INFO, "Returning: %d", ret);
        return ret;
}

int
gf_cli3_1_create_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp              rsp   = {0,};
        int                     ret   = 0;
        cli_local_t             *local = NULL;
        char                    *volname = NULL;
        dict_t                  *dict = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        local = ((call_frame_t *) (myframe))->local;
        ((call_frame_t *) (myframe))->local = NULL;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        dict = local->dict;

        ret = dict_get_str (dict, "volname", &volname);

        gf_log ("cli", GF_LOG_INFO, "Received resp to create volume");
	if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
	        cli_out ("%s", rsp.op_errstr);
        else
                cli_out ("Creation of volume %s has been %s", volname,
                                (rsp.op_ret) ? "unsuccessful":
                                "successful. Please start the volume to "
                                "access data.");
        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        if (dict)
                dict_unref (dict);
        if (local)
                cli_local_wipe (local);
        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);
        if (rsp.op_errstr)
                free (rsp.op_errstr);
        return ret;
}

int
gf_cli3_1_delete_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp              rsp   = {0,};
        int                     ret   = 0;
        cli_local_t             *local = NULL;
        char                    *volname = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *dict = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        frame = myframe;
        local = frame->local;
        frame->local = NULL;

        if (local)
                dict = local->dict;
        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "dict get failed");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to delete volume");

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                cli_out ("%s", rsp.op_errstr);
        else
                cli_out ("Deleting volume %s has been %s", volname,
                         (rsp.op_ret) ? "unsuccessful": "successful");
        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        cli_local_wipe (local);
        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);
        if (dict)
                dict_unref (dict);

        gf_log ("", GF_LOG_INFO, "Returning with %d", ret);
        return ret;
}

int
gf_cli3_1_start_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp              rsp   = {0,};
        int                     ret   = 0;
        cli_local_t             *local = NULL;
        char                    *volname = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *dict = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        frame = myframe;

        if (frame) {
                local = frame->local;
                frame->local = NULL;
        }

        if (local)
                dict = local->dict;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "dict get failed");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to start volume");

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                cli_out ("%s", rsp.op_errstr);
        else
                cli_out ("Starting volume %s has been %s", volname,
                        (rsp.op_ret) ? "unsuccessful": "successful");

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        if (local)
                cli_local_wipe (local);
        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);
        if (rsp.op_errstr)
                free (rsp.op_errstr);
        if (dict)
                dict_unref (dict);
        return ret;
}

int
gf_cli3_1_stop_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp            rsp   = {0,};
        int                   ret   = 0;
        cli_local_t           *local = NULL;
        char                  *volname = NULL;
        call_frame_t          *frame = NULL;
        dict_t                *dict = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        frame = myframe;

        if (frame)
                local = frame->local;

        if (local) {
                dict = local->dict;
                ret = dict_get_str (dict, "volname", &volname);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "Unable to get volname from dict");
                        goto out;
                }
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to stop volume");

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                cli_out ("%s", rsp.op_errstr);
        else
                cli_out ("Stopping volume %s has been %s", volname,
                        (rsp.op_ret) ? "unsuccessful": "successful");
        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        if (rsp.op_errstr)
                free (rsp.op_errstr);
        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);
        if (local)
                cli_local_wipe (local);
        return ret;
}

int
gf_cli3_1_defrag_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp               rsp     = {0,};
        cli_local_t             *local   = NULL;
        char                    *volname = NULL;
        call_frame_t            *frame   = NULL;
        char                    *status  = "unknown";
        int                      cmd     = 0;
        int                      ret     = 0;
        dict_t                  *dict    = NULL;
        dict_t                  *local_dict = NULL;
        uint64_t                 files   = 0;
        uint64_t                 size    = 0;
        uint64_t                 lookup  = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        frame = myframe;

        if (frame)
                local = frame->local;

        if (local) {
                local_dict = local->dict;
        }

        ret = dict_get_str (local_dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to get volname");
                goto out;
        }

        ret = dict_get_int32 (local_dict, "rebalance-command", (int32_t*)&cmd);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to get command");
                goto out;
        }

        if (rsp.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (rsp.dict.dict_val,
                                        rsp.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
        }

        ret = dict_get_uint64 (dict, "files", &files);
        if (ret)
                gf_log (THIS->name, GF_LOG_TRACE,
                        "failed to get file count");

        ret = dict_get_uint64 (dict, "size", &size);
        if (ret)
                gf_log (THIS->name, GF_LOG_TRACE,
                        "failed to get size of xfer");

        ret = dict_get_uint64 (dict, "lookups", &lookup);
        if (ret)
                gf_log (THIS->name, GF_LOG_TRACE,
                        "failed to get lookedup file count");

        if (cmd == GF_DEFRAG_CMD_STOP) {
                if (rsp.op_ret == -1) {
                        if (strcmp (rsp.op_errstr, ""))
                                cli_out ("%s", rsp.op_errstr);
                        else
                                cli_out ("rebalance volume %s stop failed",
                                         volname);
                } else {
                        cli_out ("stopped rebalance process of volume %s \n"
                                 "(after rebalancing %"PRId64" files totaling "
                                 "%"PRId64" bytes)", volname, files, size);
                }
                goto done;
        }
        if (cmd == GF_DEFRAG_CMD_STATUS) {
                if (rsp.op_ret == -1) {
                        if (strcmp (rsp.op_errstr, ""))
                                cli_out ("%s", rsp.op_errstr);
                        else
                                cli_out ("failed to get the status of "
                                         "rebalance process");
                        goto done;
                }

                switch (rsp.op_errno) {
                case GF_DEFRAG_STATUS_NOT_STARTED:
                        status = "not started";
                        break;
                case GF_DEFRAG_STATUS_LAYOUT_FIX_STARTED:
                        status = "step 1: layout fix in progress";
                        break;
                case GF_DEFRAG_STATUS_MIGRATE_DATA_STARTED:
                        status = "step 2: data migration in progress";
                        break;
                case GF_DEFRAG_STATUS_STOPPED:
                        status = "stopped";
                        break;
                case GF_DEFRAG_STATUS_COMPLETE:
                        status = "completed";
                        break;
                case GF_DEFRAG_STATUS_FAILED:
                        status = "failed";
                        break;
                case GF_DEFRAG_STATUS_LAYOUT_FIX_COMPLETE:
                        status = "step 1: layout fix complete";
                        break;
                case GF_DEFRAG_STATUS_MIGRATE_DATA_COMPLETE:
                        status = "step 2: data migration complete";
                        break;
                case GF_DEFRAG_STATUS_PAUSED:
                        status = "paused";
                        break;
                }
                if (files && (rsp.op_errno == 1)) {
                        cli_out ("rebalance %s: fixed layout %"PRId64,
                                 status, files);
                        goto done;
                }
                if (files && (rsp.op_errno == 6)) {
                        cli_out ("rebalance %s: fixed layout %"PRId64,
                                 status, files);
                        goto done;
                }
                if (files) {
                        cli_out ("rebalance %s: rebalanced %"PRId64
                                 " files of size %"PRId64" (total files"
                                 " scanned %"PRId64")", status,
                                 files, size, lookup);
                        goto done;
                }

                cli_out ("rebalance %s", status);
                goto done;
        }

        /* All other possibility is about starting a volume */
        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                cli_out ("%s", rsp.op_errstr);
        else
                cli_out ("starting rebalance on volume %s has been %s",
                         volname, (rsp.op_ret) ? "unsuccessful":
                         "successful");

done:
        ret = rsp.op_ret;

out:
        if (rsp.op_errstr)
                free (rsp.op_errstr); //malloced by xdr
        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val); //malloced by xdr
        if (dict)
                dict_unref (dict);
        if (local_dict)
                dict_unref (local_dict);
        if (local)
                cli_local_wipe (local);
        cli_cmd_broadcast_response (ret);
        return ret;
}

int
gf_cli3_1_rename_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp              rsp   = {0,};
        int                     ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }


        gf_log ("cli", GF_LOG_INFO, "Received resp to probe");
        cli_out ("Rename volume %s", (rsp.op_ret) ? "unsuccessful":
                                        "successful");

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int
gf_cli3_1_reset_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp           rsp   = {0,};
        int                  ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to reset");

        if (rsp.op_ret &&  strcmp (rsp.op_errstr, ""))
                cli_out ("%s", rsp.op_errstr);
        else
                cli_out ("reset volume %s", (rsp.op_ret) ? "unsuccessful":
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
        gf_cli_rsp           rsp   = {0,};
        int                  ret   = 0;
        dict_t               *dict = NULL;
        char                 *help_str = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to set");

        if (rsp.op_ret &&  strcmp (rsp.op_errstr, ""))
                cli_out ("%s", rsp.op_errstr);

        dict = dict_new ();

        if (!dict) {
                ret = -1;
                goto out;
        }

        ret = dict_unserialize (rsp.dict.dict_val, rsp.dict.dict_len, &dict);

        if (ret)
                goto out;

        if (dict_get_str (dict, "help-str", &help_str))
                cli_out ("Set volume %s", (rsp.op_ret) ? "unsuccessful":
                                                         "successful");
        else 
                cli_out ("%s", help_str);

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int
gf_cli3_1_add_brick_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp                  rsp   = {0,};
        int                         ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }


        gf_log ("cli", GF_LOG_INFO, "Received resp to add brick");

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                cli_out ("%s", rsp.op_errstr);
        else
                cli_out ("Add Brick %s", (rsp.op_ret) ? "unsuccessful":
                                                        "successful");
        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);
        if (rsp.op_errstr)
                free (rsp.op_errstr);
        return ret;
}

int
gf_cli3_remove_brick_status_cbk (struct rpc_req *req, struct iovec *iov,
                                 int count, void *myframe)
{
        gf_cli_rsp               rsp     = {0,};
        char                    *status  = "unknown";
        int                      ret     = 0;
        uint64_t                 files   = 0;
        uint64_t                 size    = 0;
        dict_t                  *dict    = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        ret = rsp.op_ret;
        if (rsp.op_ret == -1) {
                if (strcmp (rsp.op_errstr, ""))
                        cli_out ("%s", rsp.op_errstr);
                else
                        cli_out ("failed to get the status of "
                                 "remove-brick process");
                goto out;
        }

        switch (rsp.op_errno) {
        case GF_DEFRAG_STATUS_NOT_STARTED:
                status = "not started";
                break;
        case GF_DEFRAG_STATUS_LAYOUT_FIX_STARTED:
        case GF_DEFRAG_STATUS_MIGRATE_DATA_STARTED:
        case GF_DEFRAG_STATUS_LAYOUT_FIX_COMPLETE:
                status = "in progress";
                break;
        case GF_DEFRAG_STATUS_STOPPED:
                status = "stopped";
                break;
        case GF_DEFRAG_STATUS_COMPLETE:
        case GF_DEFRAG_STATUS_MIGRATE_DATA_COMPLETE:
                status = "completed";
                break;
        case GF_DEFRAG_STATUS_FAILED:
                status = "failed";
                break;
        case GF_DEFRAG_STATUS_PAUSED:
                status = "paused";
                break;
        }

        if (rsp.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (rsp.dict.dict_val,
                                        rsp.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
        }

        ret = dict_get_uint64 (dict, "files", &files);
        if (ret)
                gf_log (THIS->name, GF_LOG_TRACE,
                        "failed to get file count");

        ret = dict_get_uint64 (dict, "size", &size);
        if (ret)
                gf_log (THIS->name, GF_LOG_TRACE,
                        "failed to get size of xfer");

        if (files && (rsp.op_errno == 1)) {
                cli_out ("remove-brick %s: fixed layout %"PRId64,
                         status,files);
                goto out;
        }
        if (files && (rsp.op_errno == 6)) {
                cli_out ("remove-brick %s: fixed layout %"PRId64,
                         status, files);
                goto out;
        }
        if (files) {
                cli_out ("remove-brick %s: decommissioned %"PRId64
                         " files of size %"PRId64, status,
                         files, size);
                goto out;
        }

        cli_out ("remove-brick %s", status);

out:
        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val); //malloced by xdr
        if (dict)
                dict_unref (dict);
        cli_cmd_broadcast_response (ret);
        return ret;
}


int
gf_cli3_1_remove_brick_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp                      rsp   = {0,};
        int                             ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to remove brick");

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                cli_out ("%s", rsp.op_errstr);
        else
                cli_out ("Remove Brick %s", (rsp.op_ret) ? "unsuccessful":
                                                           "successful");

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);
        if (rsp.op_errstr)
                free (rsp.op_errstr);
        return ret;
}



int
gf_cli3_1_replace_brick_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp                       rsp              = {0,};
        int                              ret              = 0;
        cli_local_t                     *local            = NULL;
        call_frame_t                    *frame            = NULL;
        dict_t                          *dict             = NULL;
        char                            *src_brick        = NULL;
        char                            *dst_brick        = NULL;
        char                            *status_reply     = NULL;
        gf1_cli_replace_op               replace_op       = 0;
        char                            *rb_operation_str = NULL;
        dict_t                          *rsp_dict         = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        frame = (call_frame_t *) myframe;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        local = frame->local;
        GF_ASSERT (local);
        dict = local->dict;

        ret = dict_get_int32 (dict, "operation", (int32_t *)&replace_op);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG,
                        "dict_get on operation failed");
                goto out;
        }

        switch (replace_op) {
        case GF_REPLACE_OP_START:
                if (rsp.op_ret)
                        rb_operation_str = "replace-brick failed to start";
                else
                        rb_operation_str = "replace-brick started successfully";
                break;

        case GF_REPLACE_OP_STATUS:

                if (rsp.op_ret || ret)
                        rb_operation_str = "replace-brick status unknown";
                else {
                        if (rsp.dict.dict_len) {
                                /* Unserialize the dictionary */
                                rsp_dict  = dict_new ();

                                ret = dict_unserialize (rsp.dict.dict_val,
                                                        rsp.dict.dict_len,
                                                        &rsp_dict);
                                if (ret < 0) {
                                        gf_log ("glusterd", GF_LOG_ERROR,
                                                "failed to "
                                                "unserialize req-buffer to dictionary");
                                        goto out;
                                }
                        }
                        ret = dict_get_str (rsp_dict, "status-reply",
                                            &status_reply);
                        if (ret) {
                                gf_log (THIS->name, GF_LOG_ERROR, "failed to"
                                        "get status");
                                goto out;
                        }

                        rb_operation_str = status_reply;
                }

                break;

        case GF_REPLACE_OP_PAUSE:
                if (rsp.op_ret)
                        rb_operation_str = "replace-brick pause failed";
                else
                        rb_operation_str = "replace-brick paused successfully";
                break;

        case GF_REPLACE_OP_ABORT:
                if (rsp.op_ret)
                        rb_operation_str = "replace-brick abort failed";
                else
                        rb_operation_str = "replace-brick aborted successfully";
                break;

        case GF_REPLACE_OP_COMMIT:
        case GF_REPLACE_OP_COMMIT_FORCE:
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


                if (rsp.op_ret || ret)
                        rb_operation_str = "replace-brick commit failed";
                else
                        rb_operation_str = "replace-brick commit successful";

                break;

        default:
                gf_log ("", GF_LOG_DEBUG,
                        "Unknown operation");
                break;
        }

        if (rsp.op_ret && (strcmp (rsp.op_errstr, ""))) {
                rb_operation_str = rsp.op_errstr;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to replace brick");
        cli_out ("%s",
                 rb_operation_str ? rb_operation_str : "Unknown operation");

        ret = rsp.op_ret;

out:
        if (local) {
                dict_unref (local->dict);
                cli_local_wipe (local);
        }

        cli_cmd_broadcast_response (ret);
        if (rsp.dict.dict_val)
               free (rsp.dict.dict_val);
        if (rsp_dict)
                dict_unref (rsp_dict);

        return ret;
}


static int
gf_cli3_1_log_rotate_cbk (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
        gf_cli_rsp             rsp   = {0,};
        int                    ret   = -1;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        gf_log ("cli", GF_LOG_DEBUG, "Received resp to log rotate");

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                cli_out ("%s", rsp.op_errstr);
        else
                cli_out ("log rotate %s", (rsp.op_ret) ? "unsuccessful":
                                                         "successful");

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);

        return ret;
}

static int
gf_cli3_1_sync_volume_cbk (struct rpc_req *req, struct iovec *iov,
                           int count, void *myframe)
{
        gf_cli_rsp                     rsp   = {0,};
        int                            ret   = -1;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        gf_log ("cli", GF_LOG_DEBUG, "Received resp to sync");

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                cli_out ("%s", rsp.op_errstr);
        else
                cli_out ("volume sync: %s",
                         (rsp.op_ret) ? "unsuccessful": "successful");
        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int32_t
gf_cli3_1_print_limit_list (char *volname, char *limit_list,
                            char *op_errstr)
{
        int64_t  size            = 0;
        int64_t  limit_value     = 0;
        int32_t  i, j, k;
        int32_t  len = 0, ret    = -1;
        char     *size_str       = NULL;
        char     path [PATH_MAX] = {0, };
        char     ret_str [1024]  = {0, };
        char     value [1024]    = {0, };
        char     mountdir []     = "/tmp/mntXXXXXX";
        char     abspath [PATH_MAX] = {0, };
        runner_t runner          = {0,};

        GF_VALIDATE_OR_GOTO ("cli", volname, out);
        GF_VALIDATE_OR_GOTO ("cli", limit_list, out);

        if (!connected)
                goto out;

        len = strlen (limit_list);
        if (len == 0) {
                cli_out ("%s", op_errstr?op_errstr:"quota limit not set ");
                goto out;
        }

        if (mkdtemp (mountdir) == NULL) {
                gf_log ("cli", GF_LOG_WARNING, "failed to create a temporary "
                        "mount directory");
                ret = -1;
                goto out;
        }

        /* Mount a temporary client to fetch the disk usage
         * of the directory on which the limit is set.
         */
        ret = runcmd (SBIN_DIR"/glusterfs", "-s",
                      "localhost", "--volfile-id", volname, "-l",
                      DEFAULT_LOG_FILE_DIRECTORY"/quota-list.log",
                      mountdir, NULL);
        if (ret) {
                gf_log ("cli", GF_LOG_WARNING, "failed to mount glusterfs client");
                ret = -1;
                goto rm_dir;
        }

        len = strlen (limit_list);
        if (len == 0) {
                cli_out ("quota limit not set ");
                goto unmount;
        }

        i = 0;

        cli_out ("\tpath\t\t  limit_set\t     size");
        cli_out ("-----------------------------------------------------------"
                 "-----------------------");
        while (i < len) {
                j = 0;
                k = 0;

                while (limit_list [i] != ':') {
                        path [k++] = limit_list [i++];
                }
                path [k] = '\0';

                i++;  //skip ':'

                while (limit_list [i] != ',' && limit_list [i] != '\0') {
                        value [j++] = limit_list[i++];
                }
                value [j] = '\0';

                snprintf (abspath, sizeof (abspath), "%s/%s", mountdir, path);

                ret = sys_lgetxattr (abspath, "trusted.limit.list", (void *) ret_str, 4096);
                if (ret < 0) {
                        cli_out ("%-20s %10s", path, value);
                } else {
                        sscanf (ret_str, "%"PRId64",%"PRId64, &size,
                                &limit_value);
                        size_str = gf_uint64_2human_readable ((uint64_t) size);
                        if (size_str == NULL) {
                                cli_out ("%-20s %10s %20"PRId64, path,
                                         value, size);
                        } else {
                                cli_out ("%-20s %10s %20s", path,
                                         value, size_str);
                                GF_FREE (size_str);
                        }
                }
                i++;
        }

unmount:

        runinit (&runner);
        runner_add_args (&runner, "umount",
#if GF_LINUX_HOST_OS
                         "-l",
#endif
                         mountdir, NULL);
        ret = runner_run_reuse (&runner);
        if (ret)
                runner_log (&runner, "cli", GF_LOG_WARNING, "error executing");
        runner_end (&runner);

rm_dir:
        rmdir (mountdir);
out:
        return ret;
}

int
gf_cli3_1_quota_cbk (struct rpc_req *req, struct iovec *iov,
                     int count, void *myframe)
{
        gf_cli_rsp         rsp        = {0,};
        int                ret        = 0;
        dict_t            *dict       = NULL;
        char              *volname    = NULL;
        char              *limit_list = NULL;
        int32_t            type       = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        if (rsp.op_ret &&
            strcmp (rsp.op_errstr, "") == 0) {
                cli_out ("command unsuccessful %s", rsp.op_errstr);
                goto out;
        }

        if (rsp.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (rsp.dict.dict_val,
                                        rsp.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                gf_log (THIS->name, GF_LOG_TRACE,
                        "failed to get volname");

        ret = dict_get_str (dict, "limit_list", &limit_list);
        if (ret)
                gf_log (THIS->name, GF_LOG_TRACE,
                        "failed to get limit_list");

        ret = dict_get_int32 (dict, "type", &type);
        if (ret)
                gf_log (THIS->name, GF_LOG_TRACE,
                        "failed to get type");

        if (type == GF_QUOTA_OPTION_TYPE_LIST) {
                if (limit_list) {
                        gf_cli3_1_print_limit_list (volname,
                                                    limit_list,
                                                    rsp.op_errstr);
                }
        } else {
                gf_log ("cli", GF_LOG_INFO, "Received resp to quota command ");
                if (rsp.op_errstr)
                        cli_out ("%s", rsp.op_errstr);
                else
                        cli_out ("%s", "successful");
        }

out:
        ret = rsp.op_ret;

        cli_cmd_broadcast_response (ret);

        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);

        return ret;
}

int
gf_cli3_1_getspec_cbk (struct rpc_req *req, struct iovec *iov,
                       int count, void *myframe)
{
        gf_getspec_rsp          rsp   = {0,};
        int                     ret   = 0;
        char                   *spec  = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_getspec_rsp);
        if (ret < 0 || rsp.op_ret == -1) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to getspec");

        spec = GF_MALLOC (rsp.op_ret + 1, cli_mt_char);
        if (!spec) {
                gf_log("", GF_LOG_ERROR, "out of memory");
                goto out;
        }
        memcpy (spec, rsp.spec, rsp.op_ret);
        spec[rsp.op_ret] = '\0';
        cli_out ("%s", spec);
        GF_FREE (spec);

        ret = 0;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int
gf_cli3_1_pmap_b2p_cbk (struct rpc_req *req, struct iovec *iov,
                        int count, void *myframe)
{
        pmap_port_by_brick_rsp rsp = {0,};
        int                     ret   = 0;
        char                   *spec  = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_pmap_port_by_brick_rsp);
        if (ret < 0 || rsp.op_ret == -1) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to pmap b2p");

        cli_out ("%d", rsp.port);
        GF_FREE (spec);

        ret = rsp.op_ret;

out:
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
                              GLUSTER_CLI_PROBE, NULL,
                              this, gf_cli3_1_probe_cbk,
                              (xdrproc_t)xdr_gf1_cli_probe_req);

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
        int                  flags    = 0;

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

        ret = dict_get_int32 (dict, "flags", &flags);
        if (ret)
                flags = 0;

        req.hostname = hostname;
        req.port     = port;
        req.flags    = flags;
        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_DEPROBE, NULL,
                              this, gf_cli3_1_deprobe_cbk,
                              (xdrproc_t)xdr_gf1_cli_deprobe_req);

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
                              GLUSTER_CLI_LIST_FRIENDS, NULL,
                              this, gf_cli3_1_list_friends_cbk,
                              (xdrproc_t) xdr_gf1_cli_peer_list_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
gf_cli3_1_get_next_volume (call_frame_t *frame, xlator_t *this,
                           void *data)
{

        int                             ret = 0;
        cli_cmd_volume_get_ctx_t        *ctx = NULL;
        cli_local_t                     *local = NULL;

        if (!frame || !this || !data) {
                ret = -1;
                goto out;
        }

        ctx = data;

        ret = gf_cli3_1_get_volume (frame, this, data);

        local = frame->local;

        if (!local || !local->get_vol.volname) {
                cli_out ("No volumes present");
                goto out;
        }

        ctx->volname = local->get_vol.volname;

        while (ctx->volname) {
                ret = gf_cli3_1_get_volume (frame, this, ctx);
                if (ret)
                        goto out;
                ctx->volname = local->get_vol.volname;
        }

out:
        return ret;
}

int32_t
gf_cli3_1_get_volume (call_frame_t *frame, xlator_t *this,
                      void *data)
{
        gf_cli_req                      req = {{0,}};
        int                             ret = 0;
        cli_cmd_volume_get_ctx_t        *ctx = NULL;
        dict_t                          *dict = NULL;
        int32_t                         flags = 0;

        if (!frame || !this || !data) {
                ret = -1;
                goto out;
        }

        ctx = data;

        dict = dict_new ();
        if (!dict)
                goto out;

        if (ctx->volname) {
                ret = dict_set_str (dict, "volname", ctx->volname);
                if (ret)
                        goto out;
        }

        flags = ctx->flags;
        ret = dict_set_int32 (dict, "flags", flags);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to set flags");
                goto out;
        }

        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_GET_VOLUME, NULL,
                              this, gf_cli3_1_get_volume_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


int32_t
gf_cli3_1_create_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_cli_req              req = {{0,}};
        int                     ret = 0;
        dict_t                  *dict = NULL;
        cli_local_t             *local = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = dict_ref ((dict_t *)data);

        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get serialized length of dict");
                goto out;
        }

        local = cli_local_get ();

        if (local) {
                local->dict = dict_ref (dict);
                frame->local = local;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_CREATE_VOLUME, NULL,
                              this, gf_cli3_1_create_volume_cbk,
                              (xdrproc_t) xdr_gf_cli_req);



out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        if (dict)
                dict_unref (dict);

        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }

        return ret;
}

int32_t
gf_cli3_1_delete_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_cli_req              req = {{0,}};
        int                     ret = 0;
        cli_local_t             *local = NULL;
        dict_t                  *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        local = cli_local_get ();

        dict = dict_new ();
        ret = dict_set_str (dict, "volname", data);
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING, "dict set failed");
                goto out;
        }
        if (local) {
                local->dict = dict_ref (dict);
                frame->local = local;
        }

        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to get serialize dict");
                goto out;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_DELETE_VOLUME, NULL,
                              this, gf_cli3_1_delete_volume_cbk,
                              (xdrproc_t)xdr_gf_cli_req);

out:
        if (dict)
                dict_unref (dict);
        if (req.dict.dict_val)
                GF_FREE (req.dict.dict_val);
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli3_1_start_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_cli_req              req = {{0,}};
        int                     ret = 0;
        cli_local_t             *local = NULL;
        dict_t                  *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;
        local = cli_local_get ();

        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to serialize dict");
                goto out;
        }


        if (local) {
                local->dict = dict_ref (dict);
                frame->local = local;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_START_VOLUME, NULL,
                              this, gf_cli3_1_start_volume_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli3_1_stop_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_cli_req             req = {{0,}};
        int                    ret = 0;
        cli_local_t            *local = NULL;
        dict_t                 *dict = data;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        local = cli_local_get ();
        dict = data;

        if (local) {
                local->dict = dict_ref (dict);
                frame->local = local;
        }

        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *) &req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to serialize the data");

                goto out;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_STOP_VOLUME, NULL,
                              this, gf_cli3_1_stop_volume_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli3_1_defrag_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_cli_req              req     =  {{0,}};
        int                     ret     = 0;
        cli_local_t            *local   = NULL;
        char                   *volname = NULL;
        char                   *cmd_str = NULL;
        dict_t                 *dict    = NULL;
        gf_cli_defrag_type      cmd     = 0;
        dict_t                  *req_dict = NULL;

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

        if (strcmp (cmd_str, "start") == 0) {
                cmd = GF_DEFRAG_CMD_START;
                ret = dict_get_str (dict, "start-type", &cmd_str);
                if (!ret) {
                        if (strcmp (cmd_str, "fix-layout") == 0) {
                                cmd = GF_DEFRAG_CMD_START_LAYOUT_FIX;
                        }
                        if (strcmp (cmd_str, "migrate-data") == 0) {
                                cmd = GF_DEFRAG_CMD_START_MIGRATE_DATA;
                        }
                        if (strcmp (cmd_str, "migrate-data-force") == 0) {
                                cmd = GF_DEFRAG_CMD_START_MIGRATE_DATA_FORCE;
                        }
                }
                goto done;
        }
        if (strcmp (cmd_str, "stop") == 0) {
                cmd = GF_DEFRAG_CMD_STOP;
                goto done;
        }
        if (strcmp (cmd_str, "status") == 0) {
                cmd = GF_DEFRAG_CMD_STATUS;
        }

done:
        local = cli_local_get ();

        req_dict = dict_new ();
        if (!req_dict) {
                ret = -1;
                goto out;
        }

        ret = dict_set_str (req_dict, "volname", volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to set dict");
                goto out;
        }

        ret = dict_set_int32 (req_dict, "rebalance-command", (int32_t) cmd);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to set dict");
                goto out;
        }

        if (local) {
                local->dict = dict_ref (req_dict);
                frame->local = local;
        }

        ret = dict_allocate_and_serialize (req_dict,
                                           &req.dict.dict_val,
                                           (size_t *) &req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to serialize the data");

                goto out;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_DEFRAG_VOLUME, NULL,
                              this, gf_cli3_1_defrag_volume_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli3_1_rename_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_cli_req              req = {{0,}};
        int                     ret = 0;
        dict_t                  *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *) &req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to serialize the data");

                goto out;
        }


        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_RENAME_VOLUME, NULL,
                              this, gf_cli3_1_rename_volume_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli3_1_reset_volume (call_frame_t *frame, xlator_t *this, 
                        void *data)
{
        gf_cli_req              req =  {{0,}};
        int                     ret = 0;
        dict_t                  *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to get serialized length of dict");
                goto out;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                               GLUSTER_CLI_RESET_VOLUME, NULL,
                               this, gf_cli3_1_reset_volume_cbk,
                               (xdrproc_t) xdr_gf_cli_req);

out:
                gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli3_1_set_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_cli_req              req =  {{0,}};
        int                     ret = 0;
        dict_t                  *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get serialized length of dict");
                goto out;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_SET_VOLUME, NULL,
                              this, gf_cli3_1_set_volume_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli3_1_add_brick (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_cli_req              req =  {{0,}};
        int                     ret = 0;
        dict_t                  *dict = NULL;
        char                    *volname = NULL;
        int32_t                 count = 0;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_get_str (dict, "volname", &volname);

        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "count", &count);
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
                              GLUSTER_CLI_ADD_BRICK, NULL,
                              this, gf_cli3_1_add_brick_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }

        return ret;
}

int32_t
gf_cli3_1_remove_brick (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_cli_req                req =  {{0,}};;
        gf_cli_req                status_req =  {{0,}};;
        int                       ret = 0;
        dict_t                   *dict = NULL;
        int32_t                   command = 0;
        char                     *volname = NULL;
        dict_t                   *req_dict = NULL;
        int32_t                   cmd = 0;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "command", &command);
        if (ret)
                goto out;

        if (command != GF_OP_CMD_STATUS) {

                ret = dict_allocate_and_serialize (dict,
                                                   &req.dict.dict_val,
                                                   (size_t *)&req.dict.dict_len);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to get serialized length of dict");
                        goto out;
                }

                ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                                      GLUSTER_CLI_REMOVE_BRICK, NULL,
                                      this, gf_cli3_1_remove_brick_cbk,
                                      (xdrproc_t) xdr_gf_cli_req);
        } else {
                /* Need rebalance status to e sent :-) */
                req_dict = dict_new ();
                if (!req_dict) {
                        ret = -1;
                        goto out;
                }

                ret = dict_set_str (req_dict, "volname", volname);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "Failed to set dict");
                        goto out;
                }

                cmd |= GF_DEFRAG_CMD_STATUS;

                ret = dict_set_int32 (req_dict, "rebalance-command", (int32_t) cmd);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "Failed to set dict");
                        goto out;
                }

                ret = dict_allocate_and_serialize (req_dict,
                                                   &status_req.dict.dict_val,
                                                   (size_t *) &status_req.dict.dict_len);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to serialize the data");

                        goto out;
                }

                ret = cli_cmd_submit (&status_req, frame, cli_rpc_prog,
                                      GLUSTER_CLI_DEFRAG_VOLUME, NULL,
                                      this, gf_cli3_remove_brick_status_cbk,
                                      (xdrproc_t) xdr_gf_cli_req);

        }

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }

        if (status_req.dict.dict_val)
                GF_FREE (status_req.dict.dict_val);
 
        if (req_dict)
                dict_unref (req_dict);

        return ret;
}

int32_t
gf_cli3_1_replace_brick (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_cli_req                  req        =  {{0,}};
        int                         ret        = 0;
        cli_local_t                *local      = NULL;
        dict_t                     *dict       = NULL;
        char                       *src_brick  = NULL;
        char                       *dst_brick  = NULL;
        char                       *volname    = NULL;
        int32_t                     op         = 0;

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

        local->dict                 = dict_ref (dict);
        frame->local                = local;

        ret = dict_get_int32 (dict, "operation", &op);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "dict_get on operation failed");
                goto out;
        }
        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "dict_get on volname failed");
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
                "Received command replace-brick %s with "
                "%s with operation=%d", src_brick,
                dst_brick, op);


        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get serialized length of dict");
                goto out;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_REPLACE_BRICK, NULL,
                              this, gf_cli3_1_replace_brick_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        if (req.dict.dict_val) {
                GF_FREE (req.dict.dict_val);
        }

        return ret;
}


int32_t
gf_cli3_1_log_rotate (call_frame_t *frame, xlator_t *this,
                      void *data)
{
        gf_cli_req                req = {{0,}};
        int                       ret = 0;
        dict_t                   *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);

        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to serialize dict");
                goto out;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_LOG_ROTATE, NULL,
                              this, gf_cli3_1_log_rotate_cbk,
                              (xdrproc_t) xdr_gf_cli_req);


out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        if (req.dict.dict_val)
                GF_FREE (req.dict.dict_val);
        return ret;
}

int32_t
gf_cli3_1_sync_volume (call_frame_t *frame, xlator_t *this,
                       void *data)
{
        int               ret = 0;
        gf_cli_req        req = {{0,}};
        dict_t            *dict = NULL;

        if (!frame || !this || !data) {
                ret = -1;
                goto out;
        }

        dict = data;
        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);

        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to serialize dict");
                goto out;
        }

        ret = cli_cmd_submit (&req, frame,
                              cli_rpc_prog, GLUSTER_CLI_SYNC_VOLUME,
                              NULL, this, gf_cli3_1_sync_volume_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        if (req.dict.dict_val)
                GF_FREE (req.dict.dict_val);

        return ret;
}

int32_t
gf_cli3_1_getspec (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_getspec_req          req = {0,};
        int                     ret = 0;
        dict_t                  *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_get_str (dict, "volid", &req.key);
        if (ret)
                goto out;

        ret = cli_cmd_submit (&req, frame, &cli_handshake_prog,
                              GF_HNDSK_GETSPEC, NULL,
                              this, gf_cli3_1_getspec_cbk,
                              (xdrproc_t) xdr_gf_getspec_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli3_1_quota (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        gf_cli_req          req = {{0,}};
        int                 ret = 0;
        dict_t             *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to get serialized length of dict");
                goto out;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_QUOTA, NULL,
                              this, gf_cli3_1_quota_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

        GF_FREE (req.dict.dict_val);
out:
        return ret;
}

int32_t
gf_cli3_1_pmap_b2p (call_frame_t *frame, xlator_t *this, void *data)
{
        pmap_port_by_brick_req  req = {0,};
        int                     ret = 0;
        dict_t                  *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_get_str (dict, "brick", &req.brick);
        if (ret)
                goto out;

        ret = cli_cmd_submit (&req, frame, &cli_pmap_prog,
                              GF_PMAP_PORTBYBRICK, NULL,
                              this, gf_cli3_1_pmap_b2p_cbk,
                              (xdrproc_t) xdr_pmap_port_by_brick_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
gf_cli3_1_fsm_log_cbk (struct rpc_req *req, struct iovec *iov,
                       int count, void *myframe)
{
        gf1_cli_fsm_log_rsp        rsp   = {0,};
        int                        ret   = -1;
        dict_t                     *dict = NULL;
        int                        tr_count = 0;
        char                       key[256] = {0};
        int                        i = 0;
        char                       *old_state = NULL;
        char                       *new_state = NULL;
        char                       *event = NULL;
        char                       *time = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf1_cli_fsm_log_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        if (rsp.op_ret) {
                if (strcmp (rsp.op_errstr, ""))
                        cli_out ("%s", rsp.op_errstr);
                cli_out ("fsm log unsuccessful");
                ret = rsp.op_ret;
                goto out;
        }

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        ret = dict_unserialize (rsp.fsm_log.fsm_log_val,
                                rsp.fsm_log.fsm_log_len,
                                &dict);

        if (ret) {
                cli_out ("bad response");
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &tr_count);
        if (tr_count)
                cli_out("number of transitions: %d", tr_count);
        else
                cli_out("No transitions");
        for (i = 0; i < tr_count; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "log%d-old-state", i);
                ret = dict_get_str (dict, key, &old_state);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "log%d-event", i);
                ret = dict_get_str (dict, key, &event);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "log%d-new-state", i);
                ret = dict_get_str (dict, key, &new_state);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "log%d-time", i);
                ret = dict_get_str (dict, key, &time);
                if (ret)
                        goto out;
                cli_out ("Old State: [%s]\n"
                         "New State: [%s]\n"
                         "Event    : [%s]\n"
                         "timestamp: [%s]\n", old_state, new_state, event, time);
        }

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int32_t
gf_cli3_1_fsm_log (call_frame_t *frame, xlator_t *this, void *data)
{
        int                        ret = -1;
        gf1_cli_fsm_log_req        req = {0,};

        GF_ASSERT (frame);
        GF_ASSERT (this);
        GF_ASSERT (data);

        if (!frame || !this || !data)
                goto out;
        req.name = data;
        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_FSM_LOG, NULL,
                              this, gf_cli3_1_fsm_log_cbk,
                              (xdrproc_t) xdr_gf1_cli_fsm_log_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
gf_cli3_1_gsync_config_command (dict_t *dict)
{
        runner_t runner     = {0,};
        char *subop         = NULL;
        char *gwd           = NULL;
        char *slave         = NULL;
        char *master        = NULL;
        char *op_name       = NULL;

        if (dict_get_str (dict, "subop", &subop) != 0)
                return -1;

        if (strcmp (subop, "get") != 0 && strcmp (subop, "get-all") != 0) {
                cli_out (GEOREP" config updated successfully");
                return 0;
        }

        if (dict_get_str (dict, "glusterd_workdir", &gwd) != 0 ||
            dict_get_str (dict, "slave", &slave) != 0)
                return -1;

        if (dict_get_str (dict, "master", &master) != 0)
                master = NULL;
        if (dict_get_str (dict, "op_name", &op_name) != 0)
                op_name = NULL;

        runinit (&runner);
        runner_add_args (&runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (&runner, "%s/"GSYNC_CONF, gwd);
        if (master)
                runner_argprintf (&runner, ":%s", master);
        runner_add_arg (&runner, slave);
        runner_argprintf (&runner, "--config-%s", subop);
        if (op_name)
                runner_add_arg (&runner, op_name);

        return runner_run (&runner);
}

int
gf_cli3_1_gsync_out_status (dict_t *dict)
{
        int              gsync_count = 0;
        int              i = 0;
        int              ret = 0;
        char             mst[PATH_MAX] = {0, };
        char             slv[PATH_MAX]= {0, };
        char             sts[PATH_MAX] = {0, };
        char             hyphens[81] = {0, };
        char             *mst_val = NULL;
        char             *slv_val = NULL;
        char             *sts_val = NULL;

        cli_out ("%-20s %-50s %-10s", "MASTER", "SLAVE", "STATUS");

        for (i=0; i<sizeof(hyphens)-1; i++)
                hyphens[i] = '-';

        cli_out ("%s", hyphens);


        ret = dict_get_int32 (dict, "gsync-count", &gsync_count);
        if (ret) {
                gf_log ("cli", GF_LOG_INFO, "No active geo-replication sessions"
                        "present for the selected");
                ret = 0;
                goto out;
        }

        for (i = 1; i <= gsync_count; i++) {
                snprintf (mst, sizeof(mst), "master%d", i);
                snprintf (slv, sizeof(slv), "slave%d", i);
                snprintf (sts, sizeof(sts), "status%d", i);

                ret = dict_get_str (dict, mst, &mst_val);
                if (ret)
                        goto out;

                ret = dict_get_str (dict, slv, &slv_val);
                if (ret)
                        goto out;

                ret = dict_get_str (dict, sts, &sts_val);
                if (ret)
                        goto out;

                cli_out ("%-20s %-50s %-10s", mst_val,
                         slv_val, sts_val);

        }

 out:
        return ret;

}

int
gf_cli3_1_gsync_set_cbk (struct rpc_req *req, struct iovec *iov,
                         int count, void *myframe)
{
        int                     ret     = 0;
        gf_cli_rsp              rsp     = {0, };
        dict_t                  *dict   = NULL;
        char                    *gsync_status = NULL;
        char                    *master = NULL;
        char                    *slave  = NULL;
        int32_t                  type    = 0;

        if (req->rpc_status == -1) {
                ret = -1;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR,
                        "Unable to get response structure");
                goto out;
        }

        dict = dict_new ();

        if (!dict) {
                ret = -1;
                goto out;
        }

        ret = dict_unserialize (rsp.dict.dict_val, rsp.dict.dict_len, &dict);

        if (ret)
                goto out;

        if (rsp.op_ret) {
                cli_out ("%s", rsp.op_errstr ? rsp.op_errstr :
                         GEOREP" command unsuccessful");
                ret = rsp.op_ret;
                goto out;
        }

        ret = dict_get_str (dict, "gsync-status", &gsync_status);
        if (!ret)
                cli_out ("%s", gsync_status);
        else
                ret = 0;

        ret = dict_get_int32 (dict, "type", &type);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to get type");
                goto out;
        }

        switch (type) {
                case GF_GSYNC_OPTION_TYPE_START:
                case GF_GSYNC_OPTION_TYPE_STOP:
                        if (dict_get_str (dict, "master", &master) != 0)
                                master = "???";
                        if (dict_get_str (dict, "slave", &slave) != 0)
                                slave = "???";

                        cli_out ("%s " GEOREP " session between %s & %s"
                                 " has been successful",
                                 type == GF_GSYNC_OPTION_TYPE_START ?
                                  "Starting" : "Stopping",
                                 master, slave);
                break;

                case GF_GSYNC_OPTION_TYPE_CONFIG:
                        ret = gf_cli3_1_gsync_config_command (dict);
                break;

                case GF_GSYNC_OPTION_TYPE_STATUS:
                        ret = gf_cli3_1_gsync_out_status (dict);
                        goto out;
                default:
                        cli_out (GEOREP" command executed successfully");
        }

out:

        cli_cmd_broadcast_response (ret);

        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);

        return ret;
}

int32_t
gf_cli3_1_gsync_set (call_frame_t *frame, xlator_t *this,
                     void *data)
{
        int                      ret    = 0;
        dict_t                  *dict   = NULL;
        gf_cli_req               req = {{0,}};

        if (!frame || !this || !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *) &req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to serialize the data");

                goto out;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_GSYNC_SET, NULL,
                              this, gf_cli3_1_gsync_set_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

out:
        if (req.dict.dict_val)
                GF_FREE (req.dict.dict_val);

        return ret;
}

void*
cli_profile_info_elem (void *a, int index)
{
        return ((cli_profile_info_t *)a) + index;
}

int
cli_profile_info_percentage_cmp (void *a, void *b)
{
        cli_profile_info_t *ia = NULL;
        cli_profile_info_t *ib = NULL;
        int                ret = 0;

        ia = a;
        ib = b;
        if (ia->percentage_avg_latency < ib->percentage_avg_latency)
                ret = -1;
        else if (ia->percentage_avg_latency > ib->percentage_avg_latency)
                ret = 1;
        else
                ret = 0;
        return ret;
}

void
cli_profile_info_swap (void *a, void *b)
{
        cli_profile_info_t *ia = NULL;
        cli_profile_info_t *ib = NULL;
        cli_profile_info_t tmp = {0};

        ia = a;
        ib = b;
        tmp = *ia;
        *ia = *ib;
        *ib = tmp;
}

void
cmd_profile_volume_brick_out (dict_t *dict, int count, int interval)
{
        char                    key[256] = {0};
        int                     i = 0;
        uint64_t                sec = 0;
        uint64_t                r_count = 0;
        uint64_t                w_count = 0;
        char                    *brick = NULL;
        uint64_t                rb_counts[32] = {0};
        uint64_t                wb_counts[32] = {0};
        cli_profile_info_t      profile_info[GF_FOP_MAXVALUE] = {{0}};
        char                    output[128] = {0};
        int                     per_line = 0;
        char                    read_blocks[128] = {0};
        char                    write_blocks[128] = {0};
        int                     index = 0;
        int                     is_header_printed = 0;
        int                     ret = 0;
        double                  total_percentage_latency = 0;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%d-brick", count);
        ret = dict_get_str (dict, key, &brick);
        for (i = 0; i < 32; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%d-%d-read-%d", count,
                          interval, (1 << i));
                ret = dict_get_uint64 (dict, key, &rb_counts[i]);
        }

        for (i = 0; i < 32; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%d-%d-write-%d", count, interval,
                          (1<<i));
                ret = dict_get_uint64 (dict, key, &wb_counts[i]);
        }

        for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%d-%d-%d-hits", count,
                          interval, i);
                ret = dict_get_uint64 (dict, key, &profile_info[i].fop_hits);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%d-%d-%d-avglatency", count,
                          interval, i);
                ret = dict_get_double (dict, key, &profile_info[i].avg_latency);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%d-%d-%d-minlatency", count,
                          interval, i);
                ret = dict_get_double (dict, key, &profile_info[i].min_latency);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%d-%d-%d-maxlatency", count,
                          interval, i);
                ret = dict_get_double (dict, key, &profile_info[i].max_latency);
                profile_info[i].fop_name = gf_fop_list[i];

                total_percentage_latency +=
                       (profile_info[i].fop_hits * profile_info[i].avg_latency);
        }
        if (total_percentage_latency) {
                for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                        profile_info[i].percentage_avg_latency = 100 * (
                     (profile_info[i].avg_latency* profile_info[i].fop_hits) /
                                total_percentage_latency);
                }
                gf_array_insertionsort (profile_info, 1, GF_FOP_MAXVALUE - 1,
                                    sizeof (cli_profile_info_t),
                                    cli_profile_info_percentage_cmp);
        }
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%d-%d-duration", count, interval);
        ret = dict_get_uint64 (dict, key, &sec);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%d-%d-total-read", count, interval);
        ret = dict_get_uint64 (dict, key, &r_count);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%d-%d-total-write", count, interval);
        ret = dict_get_uint64 (dict, key, &w_count);

        if (ret == 0) {
                cli_out ("Brick: %s", brick);
        }

        if (interval == -1)
                cli_out ("Cumulative Stats:");
        else
                cli_out ("Interval %d Stats:", interval);
        snprintf (output, sizeof (output), "%14s", "Block Size:");
        snprintf (read_blocks, sizeof (read_blocks), "%14s", "No. of Reads:");
        snprintf (write_blocks, sizeof (write_blocks), "%14s", "No. of Writes:");
        index = 14;
        for (i = 0; i < 32; i++) {
                if ((rb_counts[i] == 0) && (wb_counts[i] == 0))
                        continue;
                per_line++;
                snprintf (output+index, sizeof (output)-index, "%19db+ ", (1<<i));
                if (rb_counts[i]) {
                        snprintf (read_blocks+index, sizeof (read_blocks)-index,
                                  "%21"PRId64" ", rb_counts[i]);
                } else {
                        snprintf (read_blocks+index, sizeof (read_blocks)-index,
                                  "%21s ", "0");
                }
                if (wb_counts[i]) {
                        snprintf (write_blocks+index, sizeof (write_blocks)-index,
                                  "%21"PRId64" ", wb_counts[i]);
                } else {
                        snprintf (write_blocks+index, sizeof (write_blocks)-index,
                                  "%21s ", "0");
                }
                index += 22;
                if (per_line == 3) {
                        cli_out ("%s", output);
                        cli_out ("%s", read_blocks);
                        cli_out ("%s", write_blocks);
                        cli_out (" ");
                        per_line = 0;
                        memset (output, 0, sizeof (output));
                        memset (read_blocks, 0, sizeof (read_blocks));
                        memset (write_blocks, 0, sizeof (write_blocks));
                        snprintf (output, sizeof (output), "%14s", "Block Size:");
                        snprintf (read_blocks, sizeof (read_blocks), "%14s",
                                  "No. of Reads:");
                        snprintf (write_blocks, sizeof (write_blocks), "%14s",
                                  "No. of Writes:");
                        index = 14;
                }
        }

        if (per_line != 0) {
                cli_out ("%s", output);
                cli_out ("%s", read_blocks);
                cli_out ("%s", write_blocks);
        }
        for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                if (profile_info[i].fop_hits == 0)
                        continue;
                if (is_header_printed == 0) {
                        cli_out ("%10s %13s %13s %13s %14s %11s", "%-latency",
                                 "Avg-latency", "Min-Latency", "Max-Latency",
                                 "No. of calls", "Fop");
                        cli_out ("%10s %13s %13s %13s %14s %11s", "---------",
                                 "-----------", "-----------", "-----------",
                                 "------------", "----");
                        is_header_printed = 1;
                }
                if (profile_info[i].fop_hits) {
                        cli_out ("%10.2lf %10.2lf us %10.2lf us %10.2lf us"
                                 " %14"PRId64" %11s",
                                 profile_info[i].percentage_avg_latency,
                                 profile_info[i].avg_latency,
                                 profile_info[i].min_latency,
                                 profile_info[i].max_latency,
                                 profile_info[i].fop_hits,
                                 profile_info[i].fop_name);
                }
        }
        cli_out (" ");
        cli_out ("%12s: %"PRId64" seconds", "Duration", sec);
        cli_out ("%12s: %"PRId64" bytes", "Data Read", r_count);
        cli_out ("%12s: %"PRId64" bytes", "Data Written", w_count);
        cli_out (" ");
}

int32_t
gf_cli3_1_profile_volume_cbk (struct rpc_req *req, struct iovec *iov,
                              int count, void *myframe)
{
        gf_cli_rsp                        rsp   = {0,};
        int                               ret   = -1;
        dict_t                            *dict = NULL;
	gf1_cli_stats_op                  op = GF_CLI_STATS_NONE;
        char                              key[256] = {0};
        int                               interval = 0;
        int                               i = 1;
        int32_t                           brick_count = 0;
        char                              *volname = NULL;
        if (-1 == req->rpc_status) {
                goto out;
        }

        gf_log ("cli", GF_LOG_DEBUG, "Received resp to profile");
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        dict = dict_new ();

        if (!dict) {
                ret = -1;
                goto out;
        }

        ret = dict_unserialize (rsp.dict.dict_val,
                                rsp.dict.dict_len,
                                &dict);

        if (ret) {
                gf_log ("", GF_LOG_ERROR,
                                "Unable to allocate memory");
                goto out;
        } else {
                dict->extra_stdfree = rsp.dict.dict_val;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "op", (int32_t*)&op);
        if (ret)
                goto out;

        if (rsp.op_ret && strcmp (rsp.op_errstr, "")) {
                cli_out ("%s", rsp.op_errstr);
        } else {
                switch (op) {
                case GF_CLI_STATS_START:
                        cli_out ("Starting volume profile on %s has been %s ",
                                 volname,
                                 (rsp.op_ret) ? "unsuccessful": "successful");
                        break;
                case GF_CLI_STATS_STOP:
                        cli_out ("Stopping volume profile on %s has been %s ",
                                 volname,
                                 (rsp.op_ret) ? "unsuccessful": "successful");
                        break;
                case GF_CLI_STATS_INFO:
                        break;
                default:
                        cli_out ("volume profile on %s has been %s ",
                                 volname,
                                 (rsp.op_ret) ? "unsuccessful": "successful");
                        break;
                }
        }

        if (rsp.op_ret) {
                ret = rsp.op_ret;
                goto out;
        }

        if (op != GF_CLI_STATS_INFO) {
                ret = 0;
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret)
                goto out;

        if (!brick_count) {
                cli_out ("All bricks of volume %s are down.", volname);
                goto out;
        }

        while (i <= brick_count) {
                snprintf (key, sizeof (key), "%d-cumulative", i);
                ret = dict_get_int32 (dict, key, &interval);
                if (ret == 0) {
                        cmd_profile_volume_brick_out (dict, i, interval);
                }
                snprintf (key, sizeof (key), "%d-interval", i);
                ret = dict_get_int32 (dict, key, &interval);
                if (ret == 0) {
                        cmd_profile_volume_brick_out (dict, i, interval);
                }
                i++;
        }
        ret = rsp.op_ret;

out:
        if (dict)
                dict_unref (dict);
        if (rsp.op_errstr)
                free (rsp.op_errstr);
        cli_cmd_broadcast_response (ret);
        return ret;
}

int32_t
gf_cli3_1_profile_volume (call_frame_t *frame, xlator_t *this, void *data)
{
        int                        ret   = -1;
        gf_cli_req                 req   = {{0,}};
        dict_t			   *dict = NULL;

        GF_ASSERT (frame);
        GF_ASSERT (this);
        GF_ASSERT (data);

        if (!frame || !this || !data)
                goto out;
        dict = data;

        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);

        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to serialize the data");

                goto out;
        }


        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_PROFILE_VOLUME, NULL,
                              this, gf_cli3_1_profile_volume_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        if (req.dict.dict_val)
                GF_FREE (req.dict.dict_val);
        return ret;
}

int32_t
gf_cli3_1_top_volume_cbk (struct rpc_req *req, struct iovec *iov,
                              int count, void *myframe)
{
        gf_cli_rsp                        rsp   = {0,};
        int                               ret   = -1;
        dict_t                            *dict = NULL;
        gf1_cli_stats_op                op = GF_CLI_STATS_NONE;
        char                              key[256] = {0};
        int                               i = 0;
        int32_t                           brick_count = 0;
        char                              brick[1024];
        int32_t                           members = 0;
        char                              *filename;
        char                              *bricks;
        uint64_t                           value = 0;
        int32_t                           j = 0;
        gf1_cli_top_op                    top_op = GF_CLI_TOP_NONE;
        uint64_t                          nr_open = 0;
        uint64_t                          max_nr_open = 0;
        double                            throughput = 0;
        double                            time = 0;
        long int                          time_sec = 0;
        long int                           time_usec = 0;
        struct tm                         *tm = NULL;
        char                              timestr[256] = {0, };
        char                              *openfd_str = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        gf_log ("cli", GF_LOG_DEBUG, "Received resp to top");
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "Unable to decode response");
                goto out;
        }

        if (rsp.op_ret) {
                if (strcmp (rsp.op_errstr, ""))
                        cli_out ("%s", rsp.op_errstr);
                cli_out ("volume top unsuccessful");
                ret = rsp.op_ret;
                goto out;
        }

        dict = dict_new ();

        if (!dict) {
                ret = -1;
                goto out;
        }

        ret = dict_unserialize (rsp.dict.dict_val,
                                rsp.dict.dict_len,
                                &dict);

        if (ret) {
                gf_log ("", GF_LOG_ERROR,
                                "Unable to allocate memory");
                goto out;
        }

        ret = dict_get_int32 (dict, "op", (int32_t*)&op);

        if (op != GF_CLI_STATS_TOP) {
                ret = 0;
                goto out;
        }
        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret)
                goto out;
        snprintf (key, sizeof (key), "%d-top-op", 1);
        ret = dict_get_int32 (dict, key, (int32_t*)&top_op);
        if (ret)
                goto out;
        while (i < brick_count) {
                i++;
                snprintf (brick, sizeof (brick), "%d-brick", i);
                ret = dict_get_str (dict, brick, &bricks);
                if (ret)
                        goto out;
                cli_out ("Brick: %s", bricks);
                snprintf(key, sizeof (key), "%d-members", i);
                ret = dict_get_int32 (dict, key, &members);

                switch (top_op) {
                case GF_CLI_TOP_OPEN:
                        snprintf (key, sizeof (key), "%d-current-open", i);
                        ret = dict_get_uint64 (dict, key, &nr_open);
                        if (ret)
                                break;
                        snprintf (key, sizeof (key), "%d-max-open", i);
                        ret = dict_get_uint64 (dict, key, &max_nr_open);
                        if (ret)
                                goto out;
                        snprintf (key, sizeof (key), "%d-max-openfd-time", i);
                        ret = dict_get_str (dict, key, &openfd_str);
                        if (ret)
                                goto out;
                        cli_out ("Current open fds: %"PRIu64", Max open"
                                " fds: %"PRIu64", Max openfd time: %s", nr_open,
                                 max_nr_open, openfd_str);
                case GF_CLI_TOP_READ:
                case GF_CLI_TOP_WRITE:
                case GF_CLI_TOP_OPENDIR:
                case GF_CLI_TOP_READDIR:
                        if (!members) {
                                continue;
                        }
                        cli_out ("Count\t\tfilename\n=======================");
                        break;
                case GF_CLI_TOP_READ_PERF:
                case GF_CLI_TOP_WRITE_PERF:
                        snprintf (key, sizeof (key), "%d-throughput", i);
                        ret = dict_get_double (dict, key, &throughput);
                        if (!ret) {
                                snprintf (key, sizeof (key), "%d-time", i);
                                ret = dict_get_double (dict, key, &time);
                        }
                        if (!ret)
                                cli_out ("Throughput %.2f MBps time %.4f secs", throughput,
                                          time / 1e6);

                        if (!members) {
                                continue;
                        }
                        cli_out ("%*s %-*s %-*s",
                                 VOL_TOP_PERF_SPEED_WIDTH, "MBps",
                                 VOL_TOP_PERF_FILENAME_DEF_WIDTH, "Filename",
                                 VOL_TOP_PERF_TIME_WIDTH, "Time");
                        cli_out ("%*s %-*s %-*s",
                                 VOL_TOP_PERF_SPEED_WIDTH, "====",
                                 VOL_TOP_PERF_FILENAME_DEF_WIDTH, "========",
                                 VOL_TOP_PERF_TIME_WIDTH, "====");
                        break;
                default:
                        goto out;
                }

                for (j = 1; j <= members; j++) {
                        snprintf (key, sizeof (key), "%d-filename-%d", i, j);
                        ret = dict_get_str (dict, key, &filename);
                        if (ret)
                                break;
                        snprintf (key, sizeof (key), "%d-value-%d", i, j);
                        ret = dict_get_uint64 (dict, key, &value);
                        if (ret)
                                goto out;
                        if ( top_op == GF_CLI_TOP_READ_PERF ||
                                top_op == GF_CLI_TOP_WRITE_PERF) {
                                snprintf (key, sizeof (key), "%d-time-sec-%d", i, j);
                                ret = dict_get_int32 (dict, key, (int32_t *)&time_sec);
                                if (ret)
                                        goto out;
                                snprintf (key, sizeof (key), "%d-time-usec-%d", i, j);
                                ret = dict_get_int32 (dict, key, (int32_t *)&time_usec);
                                if (ret)
                                        goto out;
                                tm    = localtime (&time_sec);
                                if (!tm)
                                        goto out;
                                strftime (timestr, 256, "%Y-%m-%d %H:%M:%S", tm);
                                snprintf (timestr + strlen (timestr), 256 - strlen (timestr),
                                  ".%"GF_PRI_SUSECONDS, time_usec);
                                if (strlen (filename) < VOL_TOP_PERF_FILENAME_DEF_WIDTH)
                                        cli_out ("%*"PRIu64" %-*s %-*s",
                                                 VOL_TOP_PERF_SPEED_WIDTH,
                                                 value,
                                                 VOL_TOP_PERF_FILENAME_DEF_WIDTH,
                                                 filename,
                                                 VOL_TOP_PERF_TIME_WIDTH,
                                                 timestr);
                                else
                                        cli_out ("%*"PRIu64" ...%-*s %-*s",
                                                 VOL_TOP_PERF_SPEED_WIDTH,
                                                 value,
                                                 VOL_TOP_PERF_FILENAME_ALT_WIDTH ,
                                                 filename + strlen (filename) -
                                                 VOL_TOP_PERF_FILENAME_ALT_WIDTH,
                                                 VOL_TOP_PERF_TIME_WIDTH,
                                                 timestr);
                        } else {
                                cli_out ("%"PRIu64"\t\t%s", value, filename);
                        }
                }
        }
        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);

        if (dict)
                dict_unref (dict);

        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);
        return ret;
}

int32_t
gf_cli3_1_top_volume (call_frame_t *frame, xlator_t *this, void *data)
{
        int                        ret   = -1;
        gf_cli_req                 req   = {{0,}};
        dict_t                     *dict = NULL;

        GF_ASSERT (frame);
        GF_ASSERT (this);
        GF_ASSERT (data);

        if (!frame || !this || !data)
                goto out;
        dict = data;

        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to serialize the data");

                goto out;
        }


        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_PROFILE_VOLUME, NULL,
                              this, gf_cli3_1_top_volume_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        if (req.dict.dict_val)
                GF_FREE (req.dict.dict_val);
        return ret;
}


int
gf_cli3_1_getwd_cbk (struct rpc_req *req, struct iovec *iov,
                       int count, void *myframe)
{
        gf1_cli_getwd_rsp rsp   = {0,};
        int               ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf1_cli_getwd_rsp);
        if (ret < 0 || rsp.op_ret == -1) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to getwd");

        cli_out ("%s", rsp.wd);

        ret = 0;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int32_t
gf_cli3_1_getwd (call_frame_t *frame, xlator_t *this, void *data)
{
        int                      ret = -1;
        gf1_cli_getwd_req        req = {0,};

        GF_ASSERT (frame);
        GF_ASSERT (this);

        if (!frame || !this)
                goto out;

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_GETWD, NULL,
                              this, gf_cli3_1_getwd_cbk,
                              (xdrproc_t) xdr_gf1_cli_getwd_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
gf_cli3_1_status_cbk (struct rpc_req *req, struct iovec *iov,
                      int count, void *myframe)
{
        int                             ret            = -1;
        int                             i              = 0;
        int                             pid            = -1;
        uint32_t                        cmd            = 0;
        char                            key[1024]      = {0,};
        char                           *hostname       = NULL;
        char                           *path           = NULL;
        char                           *volname        = NULL;
        dict_t                         *dict           = NULL;
        gf_cli_rsp                      rsp            = {0,};
        cli_volume_status_t                 status         = {0};

        if (req->rpc_status == -1)
                goto out;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("cli", GF_LOG_ERROR, "Volume status response error");
                goto out;
        }

        gf_log ("cli", GF_LOG_DEBUG, "Received response to status cmd");

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                cli_out ("%s", rsp.op_errstr);
        else if (rsp.op_ret)
                cli_out ("Unable to obtain volume status information.");

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_unserialize (rsp.dict.dict_val,
                                rsp.dict.dict_len,
                                &dict);
        if (ret)
                goto out;

        ret = dict_get_uint32 (dict, "cmd", &cmd);
        if (ret)
                goto out;

        if ((cmd & GF_CLI_STATUS_ALL)) {
                ((call_frame_t *)myframe)->local = dict;
                ret = 0;
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret)
                goto out;
        if (count == 0) {
                ret = -1;
                goto out;
        }

        status.brick = GF_CALLOC (1, PATH_MAX + 256, gf_common_mt_strdup);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                goto out;

        cli_out ("\nSTATUS OF VOLUME: %s", volname);

        if ((cmd & GF_CLI_STATUS_DETAIL) == 0)
                cli_out ("BRICK\t\t\t\t\t\t\tPORT\tONLINE\tPID");

        for (i = 0; i < count; i++) {

                cli_print_line (CLI_BRICK_STATUS_LINE_LEN);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.hostname", i);
                ret = dict_get_str (dict, key, &hostname);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.path", i);
                ret = dict_get_str (dict, key, &path);
                if (ret)
                        goto out;

                memset (status.brick, 0, PATH_MAX + 255);
                snprintf (status.brick, PATH_MAX + 255, "%s:%s",
                          hostname, path);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.port", i);
                ret = dict_get_int32 (dict, key, &(status.port));
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.status", i);
                ret = dict_get_int32 (dict, key, &(status.online));
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.pid", i);
                ret = dict_get_int32 (dict, key, &pid);
                if (ret)
                        goto out;
                if (pid == -1)
                        ret = gf_asprintf (&(status.pid_str), "%s", "N/A");
                else
                        ret = gf_asprintf (&(status.pid_str), "%d", pid);

                if (ret == -1)
                        goto out;

                if ((cmd & GF_CLI_STATUS_DETAIL)) {
                        ret = cli_get_detail_status (dict, i, &status);
                        if (ret)
                                goto out;
                        cli_print_detailed_status (&status);

                } else {
                        cli_print_brick_status (&status);
                }
        }

        ret = rsp.op_ret;

 out:
        if (status.brick)
                GF_FREE (status.brick);

        cli_cmd_broadcast_response (ret);
        return ret;
}

int32_t
gf_cli3_1_status_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_cli_req                      req  = {{0,}};
        int                             ret  = -1;
        dict_t                         *dict = NULL;

        if (!frame || !this || !data)
                goto out;

        dict = data;

        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);
        if (ret < 0) {
                gf_log ("cli", GF_LOG_ERROR,
                        "failed to serialize the data");

                goto out;
        }
        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_STATUS_VOLUME, NULL,
                              this, gf_cli3_1_status_cbk,
                              (xdrproc_t)xdr_gf_cli_req);

 out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning: %d", ret);
        return ret;
}

int
gf_cli_status_volume_all (call_frame_t *frame, xlator_t *this, void *data)
{
        int              i            = 0;
        int              ret          = -1;
        int              vol_count    = -1;
        uint32_t         cmd          = 0;
        char             key[1024]    = {0};
        char            *volname      = NULL;
        dict_t          *vol_dict     = NULL;
        dict_t          *dict         = NULL;

        dict = (dict_t *)data;
        ret = dict_get_uint32 (dict, "cmd", &cmd);
        if (ret)
                goto out;

        ret = gf_cli3_1_status_volume (frame, this, data);
        if (ret)
                goto out;

        vol_dict =  (dict_t *)(frame->local);

        ret = dict_get_int32 (vol_dict, "vol_count", &vol_count);
        if (ret) {
                cli_out ("Failed to get names of volumes");
                goto out;
        }

        if (vol_count == 0) {
                cli_out ("No volumes present");
                ret = 0;
                goto out;
        }

        /* remove the "all" flag in cmd */
        cmd &= ~GF_CLI_STATUS_ALL;
        cmd |= GF_CLI_STATUS_VOL;

        for (i = 0; i < vol_count; i++) {

                dict = dict_new ();
                if (!dict)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "vol%d", i);
                ret = dict_get_str (vol_dict, key, &volname);
                if (ret)
                        goto out;

                ret = dict_set_dynstr (dict, "volname", volname);
                if (ret)
                        goto out;

                ret = dict_set_uint32 (dict, "cmd", cmd);
                if (ret)
                        goto out;

                ret = gf_cli3_1_status_volume (frame, this, dict);
                if (ret)
                        goto out;

                dict_unref (dict);
        }

 out:
        if (ret)
                gf_log ("cli", GF_LOG_ERROR, "status all failed");
        if (ret && dict)
                dict_unref (dict);
        return ret;
}

static int
gf_cli3_1_mount_cbk (struct rpc_req *req, struct iovec *iov,
                  int count, void *myframe)
{
        gf1_cli_mount_rsp rsp   = {0,};
        int               ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf1_cli_mount_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to mount");

        if (rsp.op_ret == 0) {
                ret = 0;
                cli_out ("%s", rsp.path);
        } else {
                /* weird sounding but easy to parse... */
                cli_out ("%d : failed with this errno (%s)",
                         rsp.op_errno, strerror (rsp.op_errno));
                ret = 1;
        }

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int32_t
gf_cli3_1_mount (call_frame_t *frame, xlator_t *this, void *data)
{
        gf1_cli_mount_req  req  = {0,};
        int                ret  = -1;
        void            **dataa = data;
        char             *label = NULL;
        dict_t            *dict = NULL;

        if (!frame || !this || !data)
                goto out;

        label = dataa[0];
        dict  = dataa[1];

        req.label = label;
        ret = dict_allocate_and_serialize (dict, &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);
        if (ret) {
                ret = -1;
                goto out;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_MOUNT, NULL,
                              this, gf_cli3_1_mount_cbk,
                              (xdrproc_t)xdr_gf1_cli_mount_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
gf_cli3_1_umount_cbk (struct rpc_req *req, struct iovec *iov,
                   int count, void *myframe)
{
        gf1_cli_umount_rsp rsp   = {0,};
        int               ret   = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf1_cli_umount_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to mount");

        if (rsp.op_ret == 0)
                ret = 0;
        else {
                cli_out ("umount failed");
                ret = 1;
        }

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int32_t
gf_cli3_1_umount (call_frame_t *frame, xlator_t *this, void *data)
{
        gf1_cli_umount_req  req  = {0,};
        int                ret  = -1;
        dict_t            *dict = NULL;

        if (!frame || !this || !data)
                goto out;

        dict = data;

        ret = dict_get_str (dict, "path", &req.path);
        if (ret == 0)
                ret = dict_get_int32 (dict, "lazy", &req.lazy);

        if (ret) {
                ret = -1;
                goto out;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_UMOUNT, NULL,
                              this, gf_cli3_1_umount_cbk,
                              (xdrproc_t)xdr_gf1_cli_umount_req);

 out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
gf_cli3_1_heal_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp              rsp   = {0,};
        int                     ret   = 0;
        cli_local_t             *local = NULL;
        char                    *volname = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *dict = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, "error");
                goto out;
        }

        frame = myframe;

        if (frame) {
                local = frame->local;
                frame->local = NULL;
        }

        if (local)
                dict = local->dict;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to get volname");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to heal volume");

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                cli_out ("%s", rsp.op_errstr);
        else
                cli_out ("Starting heal on volume %s has been %s", volname,
                        (rsp.op_ret) ? "unsuccessful": "successful");

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        if (local)
                cli_local_wipe (local);
        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);
        if (rsp.op_errstr)
                free (rsp.op_errstr);
        if (dict)
                dict_unref (dict);
        return ret;
}

int32_t
gf_cli3_1_heal_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_cli_req              req = {{0,}};
        int                     ret = 0;
        cli_local_t             *local = NULL;
        dict_t                  *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;
        local = cli_local_get ();

        if (local) {
                local->dict = dict_ref (dict);
                frame->local = local;
        }

        ret = dict_allocate_and_serialize (dict,
                                           &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to serialize the data");

                goto out;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_HEAL_VOLUME, NULL,
                              this, gf_cli3_1_heal_volume_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        if (req.dict.dict_val)
                GF_FREE (req.dict.dict_val);

        return ret;
}

int32_t
gf_cli3_1_statedump_volume_cbk (struct rpc_req *req, struct iovec *iov,
                                int count, void *myframe)
{
        gf_cli_rsp                      rsp = {0,};
        int                             ret = -1;

        if (-1 == req->rpc_status)
                goto out;
        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_ERROR, "XDR decoding failed");
                goto out;
        }
        gf_log ("cli", GF_LOG_DEBUG, "Recieved response to statedump");
        if (rsp.op_ret)
                cli_out ("%s", rsp.op_errstr);
        else
                cli_out ("Volume statedump sucessful");

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int32_t
gf_cli3_1_statedump_volume (call_frame_t *frame, xlator_t *this,
                            void *data)
{
        gf_cli_req                      req = {{0,}};
        dict_t                          *options = NULL;
        int                             ret = -1;

        if (!frame || !this || !data)
                goto out;

        options = data;

        ret = dict_allocate_and_serialize (options,
                                           &req.dict.dict_val,
                                           (size_t *)&req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to serialize the data");

                goto out;
        }

        ret = cli_cmd_submit (&req, frame, cli_rpc_prog,
                              GLUSTER_CLI_STATEDUMP_VOLUME, NULL,
                              this, gf_cli3_1_statedump_volume_cbk,
                              (xdrproc_t)xdr_gf_cli_req);

out:
        if (options)
                dict_destroy (options);
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        if (req.dict.dict_val)
                GF_FREE (req.dict.dict_val);
        return ret;
}

struct rpc_clnt_procedure gluster_cli_actors[GLUSTER_CLI_MAXVALUE] = {
        [GLUSTER_CLI_NULL]             = {"NULL", NULL },
        [GLUSTER_CLI_PROBE]            = {"PROBE_QUERY", gf_cli3_1_probe},
        [GLUSTER_CLI_DEPROBE]          = {"DEPROBE_QUERY", gf_cli3_1_deprobe},
        [GLUSTER_CLI_LIST_FRIENDS]     = {"LIST_FRIENDS", gf_cli3_1_list_friends},
        [GLUSTER_CLI_CREATE_VOLUME]    = {"CREATE_VOLUME", gf_cli3_1_create_volume},
        [GLUSTER_CLI_DELETE_VOLUME]    = {"DELETE_VOLUME", gf_cli3_1_delete_volume},
        [GLUSTER_CLI_START_VOLUME]     = {"START_VOLUME", gf_cli3_1_start_volume},
        [GLUSTER_CLI_STOP_VOLUME]      = {"STOP_VOLUME", gf_cli3_1_stop_volume},
        [GLUSTER_CLI_RENAME_VOLUME]    = {"RENAME_VOLUME", gf_cli3_1_rename_volume},
        [GLUSTER_CLI_DEFRAG_VOLUME]    = {"DEFRAG_VOLUME", gf_cli3_1_defrag_volume},
        [GLUSTER_CLI_GET_VOLUME]       = {"GET_VOLUME", gf_cli3_1_get_volume},
        [GLUSTER_CLI_GET_NEXT_VOLUME]  = {"GET_NEXT_VOLUME", gf_cli3_1_get_next_volume},
        [GLUSTER_CLI_SET_VOLUME]       = {"SET_VOLUME", gf_cli3_1_set_volume},
        [GLUSTER_CLI_ADD_BRICK]        = {"ADD_BRICK", gf_cli3_1_add_brick},
        [GLUSTER_CLI_REMOVE_BRICK]     = {"REMOVE_BRICK", gf_cli3_1_remove_brick},
        [GLUSTER_CLI_REPLACE_BRICK]    = {"REPLACE_BRICK", gf_cli3_1_replace_brick},
        [GLUSTER_CLI_LOG_ROTATE]       = {"LOG ROTATE", gf_cli3_1_log_rotate},
        [GLUSTER_CLI_GETSPEC]          = {"GETSPEC", gf_cli3_1_getspec},
        [GLUSTER_CLI_PMAP_PORTBYBRICK] = {"PMAP PORTBYBRICK", gf_cli3_1_pmap_b2p},
        [GLUSTER_CLI_SYNC_VOLUME]      = {"SYNC_VOLUME", gf_cli3_1_sync_volume},
        [GLUSTER_CLI_RESET_VOLUME]     = {"RESET_VOLUME", gf_cli3_1_reset_volume},
        [GLUSTER_CLI_FSM_LOG]          = {"FSM_LOG", gf_cli3_1_fsm_log},
        [GLUSTER_CLI_GSYNC_SET]        = {"GSYNC_SET", gf_cli3_1_gsync_set},
        [GLUSTER_CLI_PROFILE_VOLUME]   = {"PROFILE_VOLUME", gf_cli3_1_profile_volume},
        [GLUSTER_CLI_QUOTA]            = {"QUOTA", gf_cli3_1_quota},
        [GLUSTER_CLI_TOP_VOLUME]       = {"TOP_VOLUME", gf_cli3_1_top_volume},
        [GLUSTER_CLI_GETWD]            = {"GETWD", gf_cli3_1_getwd},
        [GLUSTER_CLI_STATUS_VOLUME]    = {"STATUS_VOLUME", gf_cli3_1_status_volume},
        [GLUSTER_CLI_STATUS_ALL]       = {"STATUS_ALL", gf_cli_status_volume_all},
        [GLUSTER_CLI_MOUNT]            = {"MOUNT", gf_cli3_1_mount},
        [GLUSTER_CLI_UMOUNT]           = {"UMOUNT", gf_cli3_1_umount},
        [GLUSTER_CLI_HEAL_VOLUME]      = {"HEAL_VOLUME", gf_cli3_1_heal_volume},
        [GLUSTER_CLI_STATEDUMP_VOLUME] = {"STATEDUMP_VOLUME", gf_cli3_1_statedump_volume},
};

struct rpc_clnt_program cli_prog = {
        .progname  = "Gluster CLI",
        .prognum   = GLUSTER_CLI_PROGRAM,
        .progver   = GLUSTER_CLI_VERSION,
        .numproc   = GLUSTER_CLI_MAXVALUE,
        .proctable = gluster_cli_actors,
};
