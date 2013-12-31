/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
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
#include "byte-order.h"

#include "cli-quotad-client.h"
#include "run.h"

extern struct rpc_clnt *global_quotad_rpc;
extern rpc_clnt_prog_t cli_quotad_clnt;
extern rpc_clnt_prog_t *cli_rpc_prog;
extern int              cli_op_ret;
extern int              connected;

char *cli_vol_type_str[] = {"Distribute",
                            "Stripe",
                            "Replicate",
                            "Striped-Replicate",
                            "Distributed-Stripe",
                            "Distributed-Replicate",
                            "Distributed-Striped-Replicate",
                           };

char *cli_vol_status_str[] = {"Created",
                              "Started",
                              "Stopped",
                             };

char *cli_vol_task_status_str[] = {"not started",
                                   "in progress",
                                   "stopped",
                                   "completed",
                                   "failed",
                                   "fix-layout in progress",
                                   "fix-layout stopped",
                                   "fix-layout completed",
                                   "fix-layout failed",
                                   "unknown"
};

int32_t
gf_cli_get_volume (call_frame_t *frame, xlator_t *this,
                      void *data);

int
cli_to_glusterd (gf_cli_req *req, call_frame_t *frame, fop_cbk_fn_t cbkfn,
                 xdrproc_t xdrproc, dict_t *dict, int procnum, xlator_t *this,
                 rpc_clnt_prog_t *prog, struct iobref *iobref);

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
gf_cli_probe_cbk (struct rpc_req *req, struct iovec *iov,
                        int count, void *myframe)
{
        gf_cli_rsp            rsp   = {0,};
        int                   ret   = -1;
        char                  msg[1024] = {0,};

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                //rsp.op_ret   = -1;
                //rsp.op_errno = EINVAL;
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to probe");

        if (rsp.op_errstr && (strlen (rsp.op_errstr) > 0)) {
                snprintf (msg, sizeof (msg), "%s", rsp.op_errstr);
                if (rsp.op_ret)
                        gf_log ("cli", GF_LOG_ERROR, "%s", msg);
        }

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_str (NULL,
                                          (rsp.op_ret)? NULL : msg,
                                          rsp.op_ret, rsp.op_errno,
                                          (rsp.op_ret)? msg : NULL);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (!rsp.op_ret)
                cli_out ("peer probe: success. %s", msg);
        else
                cli_err ("peer probe: failed: %s", msg);

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int
gf_cli_deprobe_cbk (struct rpc_req *req, struct iovec *iov,
                       int count, void *myframe)
{
        gf_cli_rsp            rsp   = {0,};
        int                   ret   = -1;
        char              msg[1024] = {0,};

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                //rsp.op_ret   = -1;
                //rsp.op_errno = EINVAL;
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to deprobe");

        if (rsp.op_ret) {
                if (strlen (rsp.op_errstr) > 0) {
                        snprintf (msg, sizeof (msg), "%s", rsp.op_errstr);
                        gf_log ("cli", GF_LOG_ERROR, "%s", rsp.op_errstr);
                }
        } else {
                snprintf (msg, sizeof (msg), "success");
        }

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_str (NULL,
                                          (rsp.op_ret)? NULL : msg,
                                          rsp.op_ret, rsp.op_errno,
                                          (rsp.op_ret)? msg : NULL);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (!rsp.op_ret)
                cli_out ("peer detach: %s", msg);
        else
                cli_err ("peer detach: failed: %s", msg);

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int
gf_cli_output_peer_status (dict_t *dict, int count)
{
        int                        ret   = -1;
        char                       *uuid_buf = NULL;
        char                       *hostname_buf = NULL;
        int32_t                    i = 1;
        char                       key[256] = {0,};
        char                       *state = NULL;
        int32_t                    connected = 0;
        char                       *connected_str = NULL;

        cli_out ("Number of Peers: %d", count);
        i = 1;
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


                snprintf (key, 256, "friend%d.state", i);
                ret = dict_get_str (dict, key, &state);
                if (ret)
                        goto out;

                cli_out ("\nHostname: %s\nUuid: %s\nState: %s (%s)",
                         hostname_buf, uuid_buf, state, connected_str);
                i++;
        }

        ret = 0;
out:
        return ret;
}

int
gf_cli_output_pool_list (dict_t *dict, int count)
{
        int                        ret   = -1;
        char                       *uuid_buf = NULL;
        char                       *hostname_buf = NULL;
        int32_t                    i = 1;
        char                       key[256] = {0,};
        int32_t                    connected = 0;
        char                       *connected_str = NULL;

        if (count >= 1)
                cli_out ("UUID\t\t\t\t\tHostname\tState");

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

                cli_out ("%s\t%-8s\t%s ", uuid_buf, hostname_buf,
                         connected_str);
                i++;
        }

        ret = 0;
out:
        return ret;
}

/* function pointer for gf_cli_output_{pool_list,peer_status} */
typedef int (*cli_friend_output_fn) (dict_t*, int);

int
gf_cli_list_friends_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf1_cli_peer_list_rsp      rsp   = {0,};
        int                        ret   = -1;
        dict_t                    *dict = NULL;
        char                       msg[1024] = {0,};
        char                      *cmd = NULL;
        cli_friend_output_fn       friend_output_fn;
        call_frame_t              *frame = NULL;
        unsigned long              flags = 0;

        frame = myframe;
        flags = (long)frame->local;

        if (flags == GF_CLI_LIST_POOL_NODES) {
                cmd = "pool list";
                friend_output_fn = &gf_cli_output_pool_list;
        } else {
                cmd = "peer status";
                friend_output_fn = &gf_cli_output_peer_status;
        }

        /* 'free' the flags set by gf_cli_list_friends */
        frame->local = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf1_cli_peer_list_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                //rsp.op_ret   = -1;
                //rsp.op_errno = EINVAL;
                goto out;
        }

        gf_log ("cli", GF_LOG_DEBUG, "Received resp to list: %d",
                rsp.op_ret);

        ret = rsp.op_ret;

        if (!rsp.op_ret) {

                if (!rsp.friends.friends_len) {
                        snprintf (msg, sizeof (msg),
                                  "%s: No peers present", cmd);
                        if (global_state->mode & GLUSTER_MODE_XML) {
                                ret = cli_xml_output_peer_status (dict,
                                                                  rsp.op_ret,
                                                                  rsp.op_errno,
                                                                  msg);
                                if (ret)
                                        gf_log ("cli", GF_LOG_ERROR,
                                                "Error outputting to xml");
                                goto out;
                        }
                        cli_err ("%s", msg);
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

                if (global_state->mode & GLUSTER_MODE_XML) {
                        ret = cli_xml_output_peer_status (dict, rsp.op_ret,
                                                          rsp.op_errno, msg);
                        if (ret)
                                gf_log ("cli", GF_LOG_ERROR,
                                        "Error outputting to xml");
                        goto out;
                }

                ret = dict_get_int32 (dict, "count", &count);
                if (ret) {
                        goto out;
                }

                ret = friend_output_fn (dict, count);
                if (ret) {
                        goto out;
                }
        } else {
                if (global_state->mode & GLUSTER_MODE_XML) {
                        ret = cli_xml_output_peer_status (dict, rsp.op_ret,
                                                          rsp.op_errno, NULL);
                        if (ret)
                                gf_log ("cli", GF_LOG_ERROR,
                                        "Error outputting to xml");
                } else {
                        ret = -1;
                }
                goto out;
        }


        ret = 0;

out:
        cli_cmd_broadcast_response (ret);
        if (ret)
                cli_err ("%s: failed", cmd);

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

static int
_gf_cli_output_volinfo_opts (dict_t *d, char *k,
                             data_t *v, void *tmp)
{
        int     ret   = 0;
        char   *key   = NULL;
        char   *ptr   = NULL;
        data_t *value = NULL;

        key = tmp;

        ptr = strstr (k, "option.");
        if (ptr) {
                value = v;
                if (!value) {
                        ret = -1;
                        goto out;
                }
                cli_out_options (key, k, v->data);
        }
out:
        return ret;
}


int
gf_cli_get_volume_cbk (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
        int                        ret                  = -1;
        int                        opt_count            = 0;
        int32_t                    i                    = 0;
        int32_t                    j                    = 1;
        int32_t                    status               = 0;
        int32_t                    type                 = 0;
        int32_t                    brick_count          = 0;
        int32_t                    dist_count           = 0;
        int32_t                    stripe_count         = 0;
        int32_t                    replica_count        = 0;
        int32_t                    vol_type             = 0;
        int32_t                    transport            = 0;
        char                      *volume_id_str        = NULL;
        char                      *brick                = NULL;
        char                      *volname              = NULL;
        dict_t                    *dict                 = NULL;
        cli_local_t               *local                = NULL;
        char                       key[1024]            = {0};
        char                       err_str[2048]        = {0};
        gf_cli_rsp                 rsp                  = {0};
        char                      *caps                 = NULL;
        int                        k __attribute__((unused)) = 0;

        if (-1 == req->rpc_status)
                goto out;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to get vol: %d",
                rsp.op_ret);

        if (rsp.op_ret) {
                ret = -1;
                goto out;
        }

        if (!rsp.dict.dict_len) {
                if (global_state->mode & GLUSTER_MODE_XML)
                        goto xml_output;
                cli_err ("No volumes present");
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
                gf_log ("cli", GF_LOG_ERROR,
                        "Unable to allocate memory");
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret)
                goto out;

        local = ((call_frame_t *)myframe)->local;

        if (!count) {
                switch (local->get_vol.flags) {

                case GF_CLI_GET_NEXT_VOLUME:
                        GF_FREE (local->get_vol.volname);
                        local->get_vol.volname = NULL;
                        ret = 0;
                        goto out;

                case GF_CLI_GET_VOLUME:
                        memset (err_str, 0, sizeof (err_str));
                        snprintf (err_str, sizeof (err_str),
                                  "Volume %s does not exist",
                                  local->get_vol.volname);
                        ret = -1;
                        if (!(global_state->mode & GLUSTER_MODE_XML))
                                goto out;
                }
        }

xml_output:
        if (global_state->mode & GLUSTER_MODE_XML) {
                /* For GET_NEXT_VOLUME output is already begun in
                 * and will also end in gf_cli_get_next_volume()
                 */
                if (local->get_vol.flags == GF_CLI_GET_VOLUME) {
                        ret = cli_xml_output_vol_info_begin
                                (local, rsp.op_ret, rsp.op_errno,
                                 rsp.op_errstr);
                        if (ret) {
                                gf_log ("cli", GF_LOG_ERROR,
                                        "Error outputting to xml");
                                goto out;
                        }
                }

                if (dict) {
                        ret = cli_xml_output_vol_info (local, dict);
                        if (ret) {
                                gf_log ("cli", GF_LOG_ERROR,
                                        "Error outputting to xml");
                                goto out;
                        }
                }

                if (local->get_vol.flags == GF_CLI_GET_VOLUME) {
                        ret = cli_xml_output_vol_info_end (local);
                        if (ret)
                                gf_log ("cli", GF_LOG_ERROR,
                                        "Error outputting to xml");
                }
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

                snprintf (key, 256, "volume%d.volume_id", i);
                ret = dict_get_str (dict, key, &volume_id_str);
                if (ret)
                        goto out;

                vol_type = type;

                // Distributed (stripe/replicate/stripe-replica) setups
                if ((type > 0) && ( dist_count < brick_count))
                        vol_type = type + 3;

                cli_out ("Volume Name: %s", volname);
                cli_out ("Type: %s", cli_vol_type_str[vol_type]);
                cli_out ("Volume ID: %s", volume_id_str);
                cli_out ("Status: %s", cli_vol_status_str[status]);

#ifdef HAVE_BD_XLATOR
                k = 0;
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.xlator%d", i, k);
                ret = dict_get_str (dict, key, &caps);
                if (ret)
                        goto next;
                do {
                        j = 0;
                        cli_out ("Xlator %d: %s", k + 1, caps);
                        do {
                                memset (key, 0, sizeof (key));
                                snprintf (key, sizeof (key),
                                          "volume%d.xlator%d.caps%d",
                                          i, k, j++);
                                ret = dict_get_str (dict, key, &caps);
                                if (ret)
                                        break;
                                cli_out ("Capability %d: %s", j, caps);
                        } while (1);

                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key),
                                  "volume%d.xlator%d", i, ++k);
                        ret = dict_get_str (dict, key, &caps);
                        if (ret)
                                break;
                } while (1);

next:
#else
                caps = 0; /* Avoid compiler warnings when BD not enabled */
#endif

                if (type == GF_CLUSTER_TYPE_STRIPE_REPLICATE) {
                        cli_out ("Number of Bricks: %d x %d x %d = %d",
                                 (brick_count / dist_count),
                                 stripe_count,
                                 replica_count,
                                 brick_count);
                } else if (type == GF_CLUSTER_TYPE_NONE) {
                        cli_out ("Number of Bricks: %d", brick_count);
                } else {
                        /* For both replicate and stripe, dist_count is
                           good enough */
                        cli_out ("Number of Bricks: %d x %d = %d",
                                 (brick_count / dist_count),
                                 dist_count, brick_count);
                }

                cli_out ("Transport-type: %s",
                         ((transport == 0)?"tcp":
                          (transport == 1)?"rdma":
                          "tcp,rdma"));
                j = 1;

                GF_FREE (local->get_vol.volname);
                local->get_vol.volname = gf_strdup (volname);

                if (brick_count)
                        cli_out ("Bricks:");

                while (j <= brick_count) {
                        snprintf (key, 1024, "volume%d.brick%d", i, j);
                        ret = dict_get_str (dict, key, &brick);
                        if (ret)
                                goto out;

                        cli_out ("Brick%d: %s", j, brick);
#ifdef HAVE_BD_XLATOR
                        snprintf (key, 256, "volume%d.vg%d", i, j);
                        ret = dict_get_str (dict, key, &caps);
                        if (!ret)
                                cli_out ("Brick%d VG: %s", j, caps);
#endif
                        j++;
                }

                snprintf (key, 256, "volume%d.opt_count",i);
                ret = dict_get_int32 (dict, key, &opt_count);
                if (ret)
                        goto out;

                if (!opt_count)
                        goto out;

                cli_out ("Options Reconfigured:");

                snprintf (key, 256, "volume%d.option.",i);

                ret = dict_foreach (dict, _gf_cli_output_volinfo_opts, key);
                if (ret)
                        goto out;

                i++;
        }


        ret = 0;
out:
        cli_cmd_broadcast_response (ret);
        if (ret)
                cli_err ("%s", err_str);

        if (dict)
                dict_destroy (dict);

        free (rsp.dict.dict_val);

        free (rsp.op_errstr);

        gf_log ("cli", GF_LOG_INFO, "Returning: %d", ret);
        return ret;
}

int
gf_cli_create_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp              rsp   = {0,};
        int                     ret   = -1;
        cli_local_t             *local = NULL;
        char                    *volname = NULL;
        dict_t                  *dict = NULL;
        dict_t                  *rsp_dict = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        local = ((call_frame_t *) (myframe))->local;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to create volume");

        dict = local->dict;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                goto out;

        if (global_state->mode & GLUSTER_MODE_XML) {
                if (rsp.op_ret == 0) {
                        rsp_dict = dict_new ();
                        ret = dict_unserialize (rsp.dict.dict_val,
                                                rsp.dict.dict_len,
                                                &rsp_dict);
                        if (ret) {
                                gf_log ("cli", GF_LOG_ERROR,
                                        "Failed rsp_dict unserialization");
                                goto out;
                        }
                }

                ret = cli_xml_output_vol_create (rsp_dict, rsp.op_ret,
                                                 rsp.op_errno, rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                cli_err ("volume create: %s: failed: %s", volname,
                         rsp.op_errstr);
        else if (rsp.op_ret)
                cli_err ("volume create: %s: failed", volname);
        else
                cli_out ("volume create: %s: success: "
                         "please start the volume to access data", volname);

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        free (rsp.dict.dict_val);
        free (rsp.op_errstr);
        return ret;
}

int
gf_cli_delete_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp              rsp   = {0,};
        int                     ret   = -1;
        cli_local_t             *local = NULL;
        char                    *volname = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *dict = NULL;
        dict_t                  *rsp_dict = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        frame = myframe;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        local = frame->local;

        if (local)
                dict = local->dict;
        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "dict get failed");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to delete volume");

        if (global_state->mode & GLUSTER_MODE_XML) {
                if (rsp.op_ret == 0) {
                        rsp_dict = dict_new ();
                        ret = dict_unserialize (rsp.dict.dict_val,
                                                rsp.dict.dict_len,
                                                &rsp_dict);
                        if (ret) {
                                gf_log ("cli", GF_LOG_ERROR,
                                        "Failed rsp_dict unserialization");
                                goto out;
                        }
                }

                ret = cli_xml_output_generic_volume ("volDelete", rsp_dict,
                                                     rsp.op_ret, rsp.op_errno,
                                                     rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                cli_err ("volume delete: %s: failed: %s", volname,
                         rsp.op_errstr);
        else if (rsp.op_ret)
                cli_err ("volume delete: %s: failed", volname);
        else
                cli_out ("volume delete: %s: success", volname);

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        free (rsp.dict.dict_val);

        gf_log ("", GF_LOG_INFO, "Returning with %d", ret);
        return ret;
}

int
gf_cli3_1_uuid_get_cbk (struct rpc_req *req, struct iovec *iov,
                        int count, void *myframe)
{
        char                    *uuid_str = NULL;
        gf_cli_rsp              rsp   = {0,};
        int                     ret   = -1;
        cli_local_t             *local = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *dict = NULL;

        if (-1 == req->rpc_status)
                goto out;

        frame = myframe;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        local = frame->local;
        frame->local = NULL;

        gf_log ("cli", GF_LOG_INFO, "Received resp to uuid get");

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        ret = dict_unserialize (rsp.dict.dict_val, rsp.dict.dict_len,
                                &dict);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to unserialize "
                        "response for uuid get");
                goto out;
        }

        ret = dict_get_str (dict, "uuid", &uuid_str);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get uuid "
                        "from dictionary");
                goto out;
        }

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_dict ("uuidGenerate", dict, rsp.op_ret,
                                           rsp.op_errno, rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret) {
                if (strcmp (rsp.op_errstr, "") == 0)
                        cli_err ("Get uuid was unsuccessful");
                else
                        cli_err ("%s", rsp.op_errstr);

        } else {
                cli_out ("UUID: %s", uuid_str);

        }
        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        cli_local_wipe (local);
        if (rsp.dict.dict_val)
                free (rsp.dict.dict_val);
        if (dict)
                dict_unref (dict);

        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

int
gf_cli3_1_uuid_reset_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp              rsp   = {0,};
        int                     ret   = -1;
        cli_local_t             *local = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *dict = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        frame = myframe;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        local = frame->local;
        frame->local = NULL;

        gf_log ("cli", GF_LOG_INFO, "Received resp to uuid reset");

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_dict ("uuidReset", dict, rsp.op_ret,
                                           rsp.op_errno, rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                cli_err ("%s", rsp.op_errstr);
        else
                cli_out ("resetting the peer uuid has been %s",
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
gf_cli_start_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp              rsp   = {0,};
        int                     ret   = -1;
        cli_local_t             *local = NULL;
        char                    *volname = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *dict = NULL;
        dict_t                  *rsp_dict = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        frame = myframe;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        if (frame)
                local = frame->local;

        if (local)
                dict = local->dict;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (frame->this->name, GF_LOG_ERROR, "dict get failed");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to start volume");

        if (global_state->mode & GLUSTER_MODE_XML) {
                if (rsp.op_ret == 0) {
                        rsp_dict = dict_new ();
                        ret = dict_unserialize (rsp.dict.dict_val,
                                                rsp.dict.dict_len,
                                                &rsp_dict);
                        if (ret) {
                                gf_log ("cli", GF_LOG_ERROR,
                                        "Failed rsp_dict unserialization");
                                goto out;
                        }
                }

                ret = cli_xml_output_generic_volume ("volStart", rsp_dict,
                                                     rsp.op_ret, rsp.op_errno,
                                                     rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                cli_err ("volume start: %s: failed: %s", volname,
                         rsp.op_errstr);
        else if (rsp.op_ret)
                cli_err ("volume start: %s: failed", volname);
        else
                cli_out ("volume start: %s: success", volname);

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        free (rsp.dict.dict_val);
        free (rsp.op_errstr);
        return ret;
}

int
gf_cli_stop_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp            rsp   = {0,};
        int                   ret   = -1;
        cli_local_t           *local = NULL;
        char                  *volname = NULL;
        call_frame_t          *frame = NULL;
        dict_t                *dict = NULL;
        dict_t                *rsp_dict = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        frame = myframe;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        if (frame)
                local = frame->local;

        if (local) {
                dict = local->dict;
                ret = dict_get_str (dict, "volname", &volname);
                if (ret) {
                        gf_log (frame->this->name, GF_LOG_ERROR,
                                "Unable to get volname from dict");
                        goto out;
                }
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to stop volume");

        if (global_state->mode & GLUSTER_MODE_XML) {
                if (rsp.op_ret == 0) {
                        rsp_dict = dict_new ();
                        ret = dict_unserialize (rsp.dict.dict_val,
                                                rsp.dict.dict_len,
                                                &rsp_dict);
                        if (ret) {
                                gf_log ("cli", GF_LOG_ERROR,
                                        "Failed rsp_dict unserialization");
                                goto out;
                        }
                }

                ret = cli_xml_output_generic_volume ("volStop", rsp_dict,
                                                     rsp.op_ret, rsp.op_errno,
                                                     rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                cli_err ("volume stop: %s: failed: %s", volname, rsp.op_errstr);
        else if (rsp.op_ret)
                cli_err ("volume stop: %s: failed", volname);
        else
                cli_out ("volume stop: %s: success", volname);

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        free (rsp.op_errstr);
        free (rsp.dict.dict_val);

        return ret;
}

int
gf_cli_print_rebalance_status (dict_t *dict)
{
        int                ret          = -1;
        int                count        = 0;
        int                i            = 1;
        char               key[256]     = {0,};
        gf_defrag_status_t status_rcd   = GF_DEFRAG_STATUS_NOT_STARTED;
        uint64_t           files        = 0;
        uint64_t           size         = 0;
        uint64_t           lookup       = 0;
        char               *node_name   = NULL;
        uint64_t           failures     = 0;
        uint64_t           skipped      = 0;
        double             elapsed      = 0;
        char               *status_str  = NULL;
        char               *size_str    = NULL;

        ret = dict_get_int32 (dict, "count", &count);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "count not set");
                goto out;
        }


        cli_out ("%40s %16s %13s %13s %13s %13s %20s %18s", "Node",
                 "Rebalanced-files", "size", "scanned", "failures", "skipped",
                 "status", "run time in secs");
        cli_out ("%40s %16s %13s %13s %13s %13s %20s %18s", "---------",
                 "-----------", "-----------", "-----------", "-----------",
                 "-----------", "------------", "--------------");
        for (i = 1; i <= count; i++) {
                /* Reset the variables to prevent carryover of values */
                node_name = NULL;
                files = 0;
                size = 0;
                lookup = 0;
                skipped = 0;
                status_str = NULL;
                elapsed = 0;

                /* Check if status is NOT_STARTED, and continue early */
                memset (key, 0, 256);
                snprintf (key, 256, "status-%d", i);
                ret = dict_get_int32 (dict, key, (int32_t *)&status_rcd);
                if (ret) {
                        gf_log ("cli", GF_LOG_TRACE, "failed to get status");
                        goto out;
                }
                if (GF_DEFRAG_STATUS_NOT_STARTED == status_rcd)
                        continue;


                snprintf (key, 256, "node-name-%d", i);
                ret = dict_get_str (dict, key, &node_name);
                if (ret)
                        gf_log ("cli", GF_LOG_TRACE, "failed to get node-name");

                memset (key, 0, 256);
                snprintf (key, 256, "files-%d", i);
                ret = dict_get_uint64 (dict, key, &files);
                if (ret)
                        gf_log ("cli", GF_LOG_TRACE,
                                "failed to get file count");

                memset (key, 0, 256);
                snprintf (key, 256, "size-%d", i);
                ret = dict_get_uint64 (dict, key, &size);
                if (ret)
                        gf_log ("cli", GF_LOG_TRACE,
                                "failed to get size of xfer");

                memset (key, 0, 256);
                snprintf (key, 256, "lookups-%d", i);
                ret = dict_get_uint64 (dict, key, &lookup);
                if (ret)
                        gf_log ("cli", GF_LOG_TRACE,
                                "failed to get lookedup file count");

                memset (key, 0, 256);
                snprintf (key, 256, "failures-%d", i);
                ret = dict_get_uint64 (dict, key, &failures);
                if (ret)
                        gf_log ("cli", GF_LOG_TRACE,
                                "failed to get failures count");

                memset (key, 0, 256);
                snprintf (key, 256, "skipped-%d", i);
                ret = dict_get_uint64 (dict, key, &skipped);
                if (ret)
                        gf_log ("cli", GF_LOG_TRACE,
                                "failed to get skipped count");
                memset (key, 0, 256);
                snprintf (key, 256, "run-time-%d", i);
                ret = dict_get_double (dict, key, &elapsed);
                if (ret)
                        gf_log ("cli", GF_LOG_TRACE, "failed to get run-time");

                /* Check for array bound */
                if (status_rcd >= GF_DEFRAG_STATUS_MAX)
                        status_rcd = GF_DEFRAG_STATUS_MAX;

                status_str = cli_vol_task_status_str[status_rcd];
                size_str = gf_uint64_2human_readable(size);
                if (size_str) {
                        cli_out ("%40s %16"PRIu64 " %13s" " %13"PRIu64 " %13"
                                 PRIu64" %13"PRIu64 " %20s %18.2f", node_name,
                                 files, size_str, lookup, failures, skipped,
                                 status_str, elapsed);
                } else {
                        cli_out ("%40s %16"PRIu64 " %13"PRIu64 " %13"PRIu64
                                 " %13"PRIu64" %13"PRIu64 " %20s %18.2f",
                                 node_name, files, size, lookup, failures,
                                 skipped, status_str, elapsed);
                }
                GF_FREE(size_str);
        }
out:
        return ret;
}

int
gf_cli_defrag_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp   rsp          = {0,};
        cli_local_t  *local       = NULL;
        char         *volname     = NULL;
        call_frame_t *frame       = NULL;
        int          cmd          = 0;
        int          ret          = -1;
        dict_t       *dict        = NULL;
        dict_t       *local_dict  = NULL;
        char         msg[1024]    = {0,};
        char         *task_id_str = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        frame = myframe;

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        if (frame)
                local = frame->local;

        if (local)
                local_dict = local->dict;

        ret = dict_get_str (local_dict, "volname", &volname);
        if (ret) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to get volname");
                goto out;
        }

        ret = dict_get_int32 (local_dict, "rebalance-command", (int32_t*)&cmd);
        if (ret) {
                gf_log (frame->this->name, GF_LOG_ERROR,
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

        if (!((cmd == GF_DEFRAG_CMD_STOP) || (cmd == GF_DEFRAG_CMD_STATUS)) &&
             !(global_state->mode & GLUSTER_MODE_XML)) {
                /* All other possibilites are about starting a rebalance */
                ret = dict_get_str (dict, GF_REBALANCE_TID_KEY, &task_id_str);
                if (rsp.op_ret && strcmp (rsp.op_errstr, "")) {
                        snprintf (msg, sizeof (msg), "%s", rsp.op_errstr);
                } else {
                        if (!rsp.op_ret) {
                                snprintf (msg, sizeof (msg),
                                          "Starting rebalance on volume %s has "
                                          "been successful.\nID: %s", volname,
                                          task_id_str);
                        } else {
                                snprintf (msg, sizeof (msg),
                                          "Starting rebalance on volume %s has "
                                          "been unsuccessful.", volname);
                        }
                }
                goto done;
        }

        if (cmd == GF_DEFRAG_CMD_STOP) {
                if (rsp.op_ret == -1) {
                        if (strcmp (rsp.op_errstr, ""))
                                snprintf (msg, sizeof (msg),
                                          "%s", rsp.op_errstr);
                        else
                                snprintf (msg, sizeof (msg),
                                          "rebalance volume %s stop failed",
                                          volname);
                        goto done;
                } else {
                        snprintf (msg, sizeof (msg),
                                  "rebalance process may be in the middle of a "
                                  "file migration.\nThe process will be fully "
                                  "stopped once the migration of the file is "
                                  "complete.\nPlease check rebalance process "
                                  "for completion before doing any further "
                                  "brick related tasks on the volume.");
                }
        }
        if (cmd == GF_DEFRAG_CMD_STATUS) {
                if (rsp.op_ret == -1) {
                        if (strcmp (rsp.op_errstr, ""))
                                snprintf (msg, sizeof (msg),
                                          "%s", rsp.op_errstr);
                        else
                                snprintf (msg, sizeof (msg),
                                          "Failed to get the status of "
                                          "rebalance process");
                        goto done;
                }
        }

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_vol_rebalance (cmd, dict, rsp.op_ret,
                                                    rsp.op_errno,
                                                    rsp.op_errstr);
                goto out;
        }

        ret = gf_cli_print_rebalance_status (dict);
        if (ret)
                gf_log ("cli", GF_LOG_ERROR,
                        "Failed to print rebalance status");

done:
        if (global_state->mode & GLUSTER_MODE_XML)
                cli_xml_output_str ("volRebalance", msg,
                                    rsp.op_ret, rsp.op_errno,
                                    rsp.op_errstr);
        else {
                if (rsp.op_ret)
                        cli_err ("volume rebalance: %s: failed: %s", volname,
                                 msg);
                else
                        cli_out ("volume rebalance: %s: success: %s", volname,
                                 msg);
        }
        ret = rsp.op_ret;

out:
        free (rsp.op_errstr); //malloced by xdr
        free (rsp.dict.dict_val); //malloced by xdr
        if (dict)
                dict_unref (dict);
        cli_cmd_broadcast_response (ret);
        return ret;
}

int
gf_cli_rename_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp              rsp   = {0,};
        int                     ret   = -1;
        char                    msg[1024] = {0,};

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }


        gf_log ("cli", GF_LOG_INFO, "Received resp to probe");
        snprintf (msg, sizeof (msg), "Rename volume %s",
                  (rsp.op_ret) ? "unsuccessful": "successful");

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_str ("volRename", msg, rsp.op_ret,
                                          rsp.op_errno, rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret)
                cli_err ("volume rename: failed");
        else
                cli_out ("volume rename: success");

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int
gf_cli_reset_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp           rsp   = {0,};
        int                  ret   = -1;
        char                 msg[1024] = {0,};

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to reset");

        if (strcmp (rsp.op_errstr, ""))
                snprintf (msg, sizeof (msg), "%s", rsp.op_errstr);
        else
                snprintf (msg, sizeof (msg), "reset volume %s",
                          (rsp.op_ret) ? "unsuccessful": "successful");

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_str ("volReset", msg, rsp.op_ret,
                                          rsp.op_errno, rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret)
                cli_err ("volume reset: failed: %s", msg);
        else
                cli_out ("volume reset: success: %s", msg);

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

char *
is_server_debug_xlator (void *myframe)
{
        call_frame_t         *frame        = NULL;
        cli_local_t          *local        = NULL;
        char                 **words       = NULL;
        char                 *key          = NULL;
        char                 *value        = NULL;
        char                 *debug_xlator = NULL;

        frame = myframe;
        local = frame->local;
        words = (char **)local->words;

        while (*words != NULL) {
                if (strstr (*words, "trace") == NULL &&
                    strstr (*words, "error-gen") == NULL) {
                        words++;
                        continue;
                }

                key = *words;
                words++;
                value = *words;
                if (value == NULL)
                        break;
                if (strstr (value, "client")) {
                        words++;
                        continue;
                } else {
                        if (!(strstr (value, "posix") || strstr (value, "acl")
                              || strstr (value, "locks") ||
                              strstr (value, "io-threads") ||
                              strstr (value, "marker") ||
                              strstr (value, "index"))) {
                                words++;
                                continue;
                        } else {
                                debug_xlator = gf_strdup (key);
                                break;
                        }
                }
        }

        return debug_xlator;
}

int
gf_cli_set_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp           rsp   = {0,};
        int                  ret   = -1;
        dict_t               *dict = NULL;
        char                 *help_str = NULL;
        char                 msg[1024] = {0,};
        char                 *debug_xlator = _gf_false;
        char                 tmp_str[512] = {0,};

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to set");

        dict = dict_new ();

        if (!dict) {
                ret = -1;
                goto out;
        }

        ret = dict_unserialize (rsp.dict.dict_val, rsp.dict.dict_len, &dict);

        /* For brick processes graph change does not happen on the fly.
         * The proces has to be restarted. So this is a check from the
         * volume set option such that if debug xlators such as trace/errorgen
         * are provided in the set command, warn the user.
         */
        debug_xlator = is_server_debug_xlator (myframe);

        if (dict_get_str (dict, "help-str", &help_str) && !msg[0])
                snprintf (msg, sizeof (msg), "Set volume %s",
                          (rsp.op_ret) ? "unsuccessful": "successful");
        if (rsp.op_ret == 0 && debug_xlator) {
                snprintf (tmp_str, sizeof (tmp_str), "\n%s translator has been "
                          "added to the server volume file. Please restart the"
                          " volume for enabling the translator", debug_xlator);
        }

        if ((global_state->mode & GLUSTER_MODE_XML) && (help_str == NULL)) {
                ret = cli_xml_output_str ("volSet", msg, rsp.op_ret,
                                          rsp.op_errno, rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret) {
                if (strcmp (rsp.op_errstr, ""))
                        cli_err ("volume set: failed: %s", rsp.op_errstr);
                else
                        cli_err ("volume set: failed");
        } else {
                if (help_str == NULL) {
                        if (debug_xlator == NULL)
                                cli_out ("volume set: success");
                        else
                                cli_out ("volume set: success%s", tmp_str);
                }else {
                        cli_out ("%s", help_str);
                }
        }

        ret = rsp.op_ret;

out:
        if (dict)
                dict_unref (dict);
        GF_FREE (debug_xlator);
        cli_cmd_broadcast_response (ret);
        return ret;
}

int
gf_cli_add_brick_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp                  rsp   = {0,};
        int                         ret   = -1;
        char                        msg[1024] = {0,};

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }


        gf_log ("cli", GF_LOG_INFO, "Received resp to add brick");

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                snprintf (msg, sizeof (msg), "%s", rsp.op_errstr);
        else
                snprintf (msg, sizeof (msg), "Add Brick %s",
                          (rsp.op_ret) ? "unsuccessful": "successful");

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_str ("volAddBrick", msg, rsp.op_ret,
                                          rsp.op_errno, rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret)
                cli_err ("volume add-brick: failed: %s", rsp.op_errstr);
        else
                cli_out ("volume add-brick: success");
        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        free (rsp.dict.dict_val);
        free (rsp.op_errstr);
        return ret;
}

int
gf_cli3_remove_brick_status_cbk (struct rpc_req *req, struct iovec *iov,
                                 int count, void *myframe)
{
        gf_cli_rsp               rsp     = {0,};
        int                      ret     = -1;
        dict_t                  *dict    = NULL;
        char                     msg[1024] = {0,};
        int32_t                  command = 0;
        gf1_op_commands          cmd = GF_OP_CMD_NONE;
        cli_local_t             *local = NULL;
        call_frame_t            *frame = NULL;
        char                    *cmd_str = "unknown";

        if (-1 == req->rpc_status) {
                goto out;
        }

        frame = myframe;

        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        if (frame)
                local = frame->local;
        ret = dict_get_int32 (local->dict, "command", &command);
        if (ret)
                goto out;
        cmd = command;

        switch (cmd) {
        case GF_OP_CMD_STOP:
                cmd_str = "stop";
                break;
        case GF_OP_CMD_STATUS:
                cmd_str = "status";
                break;
        default:
                break;
        }

        ret = rsp.op_ret;
        if (rsp.op_ret == -1) {
                if (strcmp (rsp.op_errstr, ""))
                        snprintf (msg, sizeof (msg), "volume remove-brick %s: "
                                  "failed: %s", cmd_str, rsp.op_errstr);
                else
                        snprintf (msg, sizeof (msg), "volume remove-brick %s: "
                                  "failed", cmd_str);

                if (global_state->mode & GLUSTER_MODE_XML)
                        goto xml_output;

                cli_err ("%s", msg);
                goto out;
        }

        if (rsp.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (rsp.dict.dict_val,
                                        rsp.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        strncpy (msg, "failed to unserialize req-buffer to "
                                 "dictionary", sizeof (msg));

                        if (global_state->mode & GLUSTER_MODE_XML) {
                                rsp.op_ret = -1;
                                goto xml_output;
                        }

                        gf_log ("cli", GF_LOG_ERROR, "%s", msg);
                        goto out;
                }
        }

xml_output:
        if (global_state->mode & GLUSTER_MODE_XML) {
                if (strcmp (rsp.op_errstr, "")) {
                        ret = cli_xml_output_vol_remove_brick (_gf_true, dict,
                                                               rsp.op_ret,
                                                               rsp.op_errno,
                                                               rsp.op_errstr);
                } else {
                        ret = cli_xml_output_vol_remove_brick (_gf_true, dict,
                                                               rsp.op_ret,
                                                               rsp.op_errno,
                                                               msg);
                }
                goto out;
        }

        ret = gf_cli_print_rebalance_status (dict);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to print remove-brick "
                        "rebalance status");
                goto out;
        }

        if ((cmd == GF_OP_CMD_STOP) && (rsp.op_ret == 0)) {
                cli_out ("'remove-brick' process may be in the middle of a "
                         "file migration.\nThe process will be fully stopped "
                         "once the migration of the file is complete.\nPlease "
                         "check remove-brick process for completion before "
                         "doing any further brick related tasks on the "
                         "volume.");
        }

out:
        free (rsp.dict.dict_val); //malloced by xdr
        if (dict)
                dict_unref (dict);
        cli_cmd_broadcast_response (ret);
        return ret;
}


int
gf_cli_remove_brick_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp                      rsp   = {0,};
        int                             ret   = -1;
        char                            msg[1024] = {0,};
        gf1_op_commands                 cmd = GF_OP_CMD_NONE;
        char                           *cmd_str = "unknown";
        cli_local_t                    *local = NULL;
        call_frame_t                   *frame = NULL;
        char                           *task_id_str = NULL;
        dict_t                         *rsp_dict = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        frame = myframe;
        local = frame->local;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        ret = dict_get_int32 (local->dict, "command", (int32_t *)&cmd);
        if (ret) {
                 gf_log ("", GF_LOG_ERROR, "failed to get command");
                 goto out;
        }

        if (rsp.dict.dict_len) {
                rsp_dict = dict_new ();
                if (!rsp_dict) {
                        ret = -1;
                        goto out;
                }

                ret = dict_unserialize (rsp.dict.dict_val, rsp.dict.dict_len,
                                        &rsp_dict);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "Failed to unserialize rsp_dict");
                        goto out;
                }
        }

        switch (cmd) {
        case GF_OP_CMD_START:
                cmd_str = "start";

                ret = dict_get_str (rsp_dict, GF_REMOVE_BRICK_TID_KEY, &task_id_str);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "remove-brick-id is not present in dict");
                }
                break;
        case GF_OP_CMD_COMMIT:
                cmd_str = "commit";
                break;
        case GF_OP_CMD_COMMIT_FORCE:
                cmd_str = "commit force";
                break;
        default:
                cmd_str = "unknown";
                break;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to remove brick");

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                snprintf (msg, sizeof (msg), "%s", rsp.op_errstr);
        else
                snprintf (msg, sizeof (msg), "Remove Brick %s %s", cmd_str,
                          (rsp.op_ret) ? "unsuccessful": "successful");

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_vol_remove_brick (_gf_false, rsp_dict,
                                                       rsp.op_ret, rsp.op_errno,
                                                       msg);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret) {
                cli_err ("volume remove-brick %s: failed: %s", cmd_str,
                         msg);
        } else {
                cli_out ("volume remove-brick %s: success", cmd_str);
                if (GF_OP_CMD_START == cmd && task_id_str != NULL)
                        cli_out ("ID: %s", task_id_str);
        }

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        free (rsp.dict.dict_val);
        free (rsp.op_errstr);

        return ret;
}



int
gf_cli_replace_brick_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp                       rsp              = {0,};
        int                              ret              = -1;
        cli_local_t                     *local            = NULL;
        call_frame_t                    *frame            = NULL;
        dict_t                          *dict             = NULL;
        char                            *src_brick        = NULL;
        char                            *dst_brick        = NULL;
        char                            *status_reply     = NULL;
        gf1_cli_replace_op               replace_op       = 0;
        char                            *rb_operation_str = NULL;
        dict_t                          *rsp_dict         = NULL;
        char                             msg[1024]        = {0,};
        char                            *task_id_str      = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        frame = (call_frame_t *) myframe;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
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

        if (rsp.dict.dict_len) {
                /* Unserialize the dictionary */
                rsp_dict  = dict_new ();

                ret = dict_unserialize (rsp.dict.dict_val,
                                rsp.dict.dict_len,
                                &rsp_dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                        "failed to "
                                        "unserialize rsp buffer to dictionary");
                        goto out;
                }
        }

        switch (replace_op) {
        case GF_REPLACE_OP_START:
                if (rsp.op_ret) {
                        rb_operation_str = gf_strdup ("replace-brick failed to"
                                                      " start");
                } else {
                        ret = dict_get_str (rsp_dict, GF_REPLACE_BRICK_TID_KEY,
                                            &task_id_str);
                        if (ret) {
                                gf_log ("cli", GF_LOG_ERROR, "Failed to get "
                                        "\"replace-brick-id\" from dict");
                                goto out;
                        }
                        ret = gf_asprintf (&rb_operation_str,
                                           "replace-brick started successfully"
                                           "\nID: %s", task_id_str);
                        if (ret < 0)
                                goto out;
                }
                break;

        case GF_REPLACE_OP_STATUS:

                if (rsp.op_ret || ret) {
                        rb_operation_str = gf_strdup ("replace-brick status "
                                                      "unknown");
                } else {
                        ret = dict_get_str (rsp_dict, "status-reply",
                                            &status_reply);
                        if (ret) {
                                gf_log (frame->this->name, GF_LOG_ERROR, "failed to"
                                        "get status");
                                goto out;
                        }

                        rb_operation_str = gf_strdup (status_reply);
                }

                break;

        case GF_REPLACE_OP_PAUSE:
                if (rsp.op_ret)
                        rb_operation_str = gf_strdup ("replace-brick pause "
                                                      "failed");
                else
                        rb_operation_str = gf_strdup ("replace-brick paused "
                                                      "successfully");
                break;

        case GF_REPLACE_OP_ABORT:
                if (rsp.op_ret)
                        rb_operation_str = gf_strdup ("replace-brick abort "
                                                      "failed");
                else
                        rb_operation_str = gf_strdup ("replace-brick aborted "
                                                      "successfully");
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
                        rb_operation_str = gf_strdup ("replace-brick commit "
                                                      "failed");
                else
                        rb_operation_str = gf_strdup ("replace-brick commit "
                                                      "successful");

                break;

        default:
                gf_log ("", GF_LOG_DEBUG,
                        "Unknown operation");
                break;
        }

        if (rsp.op_ret && (strcmp (rsp.op_errstr, ""))) {
                rb_operation_str = gf_strdup (rsp.op_errstr);
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to replace brick");
        snprintf (msg, sizeof (msg), "%s",
                  rb_operation_str ? rb_operation_str : "Unknown operation");

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_vol_replace_brick (replace_op, rsp_dict,
                                                        rsp.op_ret,
                                                        rsp.op_errno, msg);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret)
                cli_err ("volume replace-brick: failed: %s", msg);
        else
                cli_out ("volume replace-brick: success: %s", msg);
        ret = rsp.op_ret;

out:
        if (frame)
                frame->local = NULL;

        if (local) {
                dict_unref (local->dict);
                cli_local_wipe (local);
        }

        if (rb_operation_str)
                GF_FREE (rb_operation_str);

        cli_cmd_broadcast_response (ret);
        free (rsp.dict.dict_val);
        if (rsp_dict)
                dict_unref (rsp_dict);

        return ret;
}


static int
gf_cli_log_rotate_cbk (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
        gf_cli_rsp             rsp   = {0,};
        int                    ret   = -1;
        char                   msg[1024] = {0,};

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        gf_log ("cli", GF_LOG_DEBUG, "Received resp to log rotate");

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                snprintf (msg, sizeof (msg), "%s", rsp.op_errstr);
        else
                snprintf (msg, sizeof (msg), "log rotate %s",
                          (rsp.op_ret) ? "unsuccessful": "successful");

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_str ("volLogRotate", msg, rsp.op_ret,
                                          rsp.op_errno, rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret)
                cli_err ("volume log-rotate: failed: %s", msg);
        else
                cli_out ("volume log-rotate: success");
        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        free (rsp.dict.dict_val);

        return ret;
}

static int
gf_cli_sync_volume_cbk (struct rpc_req *req, struct iovec *iov,
                           int count, void *myframe)
{
        gf_cli_rsp                     rsp   = {0,};
        int                            ret   = -1;
        char                           msg[1024] = {0,};

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        gf_log ("cli", GF_LOG_DEBUG, "Received resp to sync");

        if (rsp.op_ret && strcmp (rsp.op_errstr, ""))
                snprintf (msg, sizeof (msg), "volume sync: failed: %s",
                          rsp.op_errstr);
        else
                snprintf (msg, sizeof (msg), "volume sync: %s",
                          (rsp.op_ret) ? "failed": "success");

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_str ("volSync", msg, rsp.op_ret,
                                          rsp.op_errno, rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret)
                cli_err ("%s", msg);
        else
                cli_out ("%s", msg);
        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

static int
print_quota_list_output (char *mountdir, char *default_sl, char *path)
{
        int64_t used_space       = 0;
        int64_t avail            = 0;
        char    *used_str         = NULL;
        char    *avail_str        = NULL;
        int     ret               = -1;
        char    *sl_final         = NULL;
        char     percent_str[20]  = {0,};
        char    *hl_str           = NULL;

        struct quota_limit {
                int64_t hl;
                int64_t sl;
        } __attribute__ ((__packed__)) existing_limits;

        ret = sys_lgetxattr (mountdir, "trusted.glusterfs.quota.limit-set",
                             (void *)&existing_limits,
                             sizeof (existing_limits));
        if (ret < 0) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get the xattr "
                        "trusted.glusterfs.quota.limit-set on %s. Reason : %s",
                        mountdir, strerror (errno));
                switch (errno) {
#if defined(ENODATA)
                case ENODATA:
#endif
#if defined(ENOATTR) && (ENOATTR != ENODATA)
                case ENOATTR:
#endif
                        cli_err ("%-40s %s", path, "Limit not set");
                        break;
                default:
                        cli_err ("%-40s %s", path, strerror (errno));
                        break;
                }

                goto out;
        }

        existing_limits.hl = ntoh64 (existing_limits.hl);
        existing_limits.sl = ntoh64 (existing_limits.sl);

        hl_str = gf_uint64_2human_readable (existing_limits.hl);

        if (existing_limits.sl < 0) {
                sl_final = default_sl;
        } else {
                snprintf (percent_str, sizeof (percent_str), "%"PRIu64"%%",
                          existing_limits.sl);
                sl_final = percent_str;
        }

        ret = sys_lgetxattr (mountdir, "trusted.glusterfs.quota.size",
                             &used_space, sizeof (used_space));

        if (ret < 0) {
                cli_out ("%-40s %7s %9s %11s %7s", path, hl_str, sl_final,
                         "N/A", "N/A");
        } else {
                used_space = ntoh64 (used_space);

                used_str = gf_uint64_2human_readable (used_space);

                if (existing_limits.hl > used_space)
                        avail = existing_limits.hl - used_space;
                else
                        avail = 0;

                avail_str = gf_uint64_2human_readable (avail);
                if (used_str == NULL)
                        cli_out ("%-40s %7s %9s %11"PRIu64
                                                 "%9"PRIu64, path, hl_str,
                                                 sl_final, used_space, avail);
                else
                        cli_out ("%-40s %7s %9s %11s %7s", path, hl_str,
                                 sl_final, used_str, avail_str);
        }

out:
        GF_FREE (used_str);
        GF_FREE (avail_str);
        GF_FREE (hl_str);
        return ret;
}

int
gf_cli_print_limit_list_from_dict (char *volname, dict_t *dict,
                                   char *default_sl, int count, char *op_errstr)
{
        int  ret               = -1;
        int  i                 = 0;
        char key[1024]         = {0,};
        char mountdir[PATH_MAX] = {0,};
        char *path              = NULL;

        if (!dict|| count <= 0)
                goto out;

        /* Need to check if any quota limits are set on the volume before trying
         * to list them
         */
        if (!_limits_set_on_volume (volname)) {
                ret = 0;
                cli_out ("quota: No quota configured on volume %s", volname);
                goto out;
        }

        /* Check if the mount is online before doing any listing */
        if (!_quota_aux_mount_online (volname)) {
                ret = -1;
                goto out;
        }

        cli_out ("                  Path                   Hard-limit "
                 "Soft-limit   Used  Available");
        cli_out ("-----------------------------------------------------"
                 "---------------------------");

        while (count--) {
                snprintf (key, sizeof (key), "path%d", i++);

                ret = dict_get_str (dict, key, &path);
                if (ret < 0) {
                        gf_log ("cli", GF_LOG_DEBUG, "Path not present in limit"
                                " list");
                        continue;
                }

                ret = gf_canonicalize_path (path);
                if (ret)
                        goto out;
                GLUSTERD_GET_QUOTA_AUX_MOUNT_PATH (mountdir, volname, path);
                ret = print_quota_list_output (mountdir, default_sl, path);

        }
out:
        return ret;
}

int
print_quota_list_from_quotad (call_frame_t *frame, dict_t *rsp_dict)
{
        int64_t used_space    = 0;
        int64_t avail         = 0;
        int64_t *limit         = NULL;
        char    *used_str      = NULL;
        char    *avail_str     = NULL;
        char    percent_str[20]= {0};
        char    *hl_str        = NULL;
        char    *sl_final      = NULL;
        char    *path          = NULL;
        char    *default_sl = NULL;
        int     ret            = -1;
        cli_local_t *local     = NULL;
        dict_t *gd_rsp_dict    = NULL;

        local = frame->local;
        gd_rsp_dict = local->dict;

        struct quota_limit {
                int64_t hl;
                int64_t sl;
        } __attribute__ ((__packed__)) *existing_limits = NULL;

        ret = dict_get_str (rsp_dict, GET_ANCESTRY_PATH_KEY, &path);
        if (ret) {
                gf_log ("cli", GF_LOG_WARNING, "path key is not present "
                        "in dict");
                goto out;
        }

        ret = dict_get_bin (rsp_dict, QUOTA_LIMIT_KEY, (void**)&limit);
        if (ret) {
                gf_log ("cli", GF_LOG_WARNING,
                        "limit key not present in dict");
                goto out;
        }

        ret = dict_get_str (gd_rsp_dict, "default-soft-limit", &default_sl);
        if (ret) {
                gf_log (frame->this->name, GF_LOG_ERROR, "failed to "
                        "get default soft limit");
                goto out;
        }
        existing_limits = (struct quota_limit *)limit;
        existing_limits->hl = ntoh64 (existing_limits->hl);
        existing_limits->sl = ntoh64 (existing_limits->sl);

        hl_str = gf_uint64_2human_readable (existing_limits->hl);

        if (existing_limits->sl < 0) {
                sl_final = default_sl;
        } else {
                snprintf (percent_str, sizeof (percent_str), "%"PRIu64"%%",
                          existing_limits->sl);
                sl_final = percent_str;
        }

        ret = dict_get_bin (rsp_dict, QUOTA_SIZE_KEY, (void**)&limit);
        if (ret < 0) {
                gf_log ("cli", GF_LOG_WARNING,
                        "size key not present in dict");
                cli_out ("%-40s %7s %9s %11s %7s", path, hl_str, sl_final,
                         "N/A", "N/A");
        } else {
                used_space = *limit;
                used_space = ntoh64 (used_space);
                used_str = gf_uint64_2human_readable (used_space);

                if (existing_limits->hl > used_space)
                        avail = existing_limits->hl - used_space;
                else
                        avail = 0;

                avail_str = gf_uint64_2human_readable (avail);
                if (used_str == NULL)
                        cli_out ("%-40s %7s %9s %11"PRIu64
                                                 "%9"PRIu64, path, hl_str,
                                                 sl_final, used_space, avail);
                else
                        cli_out ("%-40s %7s %9s %11s %7s", path, hl_str,
                                 sl_final, used_str, avail_str);
        }

        ret = 0;
out:
        GF_FREE (used_str);
        GF_FREE (avail_str);
        GF_FREE (hl_str);
        return ret;
}

int
cli_quotad_getlimit_cbk (struct rpc_req *req, struct iovec *iov,
                          int count, void *myframe)
{
    //TODO: we need to gather the path, hard-limit, soft-limit and used space
        gf_cli_rsp         rsp         = {0,};
        int                ret         = -1;
        dict_t            *dict        = NULL;
        call_frame_t      *frame       = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        frame = myframe;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        if (rsp.op_ret) {
                ret = -1;
                if (strcmp (rsp.op_errstr, ""))
                        cli_err ("quota command failed : %s", rsp.op_errstr);
                else
                        cli_err ("quota command : failed");
                goto out;
        }

        if (rsp.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (rsp.dict.dict_val,
                                        rsp.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
                print_quota_list_from_quotad (frame, dict);
        }

out:
        cli_cmd_broadcast_response (ret);
        if (dict)
                dict_unref (dict);

        free (rsp.dict.dict_val);
        return ret;
}

int
cli_quotad_getlimit (call_frame_t *frame, xlator_t *this, void *data)
{
        gf_cli_req          req = {{0,}};
        int                 ret = 0;
        dict_t             *dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;
        ret = dict_allocate_and_serialize (dict, &req.dict.dict_val,
                                           &req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to serialize the data");

                goto out;
        }

        ret = cli_cmd_submit (global_quotad_rpc, &req, frame, &cli_quotad_clnt,
                              GF_AGGREGATOR_GETLIMIT, NULL,
                              this, cli_quotad_getlimit_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;


}

void
gf_cli_quota_list (char *volname, dict_t *dict, int count, char *op_errstr,
                   char *default_sl)
{
        GF_VALIDATE_OR_GOTO ("cli", volname, out);

        if (!connected)
                goto out;

        if (count > 0)
                gf_cli_print_limit_list_from_dict (volname, dict, default_sl,
                                                   count, op_errstr);
out:
        return;
}

int
gf_cli_quota_cbk (struct rpc_req *req, struct iovec *iov,
                     int count, void *myframe)
{
        gf_cli_rsp         rsp         = {0,};
        int                ret         = -1;
        dict_t            *dict        = NULL;
        char              *volname     = NULL;
        int32_t            type        = 0;
        call_frame_t      *frame       = NULL;
        char              *default_sl  = NULL;
        char              *limit_list  = NULL;
        cli_local_t       *local       = NULL;
        dict_t            *aggr        = NULL;
        char              *default_sl_dup  = NULL;
        int32_t            entry_count      = 0;
        if (-1 == req->rpc_status) {
                goto out;
        }

        frame = myframe;
        local = frame->local;
        aggr  = local->dict;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        if (rsp.op_ret) {
                ret = -1;
                if (global_state->mode & GLUSTER_MODE_XML)
                        goto xml_output;

                if (strcmp (rsp.op_errstr, ""))
                        cli_err ("quota command failed : %s", rsp.op_errstr);
                else
                        cli_err ("quota command : failed");

                goto out;
        }

        if (rsp.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (rsp.dict.dict_val,
                                        rsp.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                }
        }

        gf_log ("cli", GF_LOG_DEBUG, "Received resp to quota command");

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "failed to get volname");

        ret = dict_get_str (dict, "default-soft-limit", &default_sl);
        if (ret)
                gf_log (frame->this->name, GF_LOG_TRACE, "failed to get "
                        "default soft limit");

        // default-soft-limit is part of rsp_dict only iff we sent
        // GLUSTER_CLI_QUOTA with type being GF_QUOTA_OPTION_TYPE_LIST
        if (default_sl) {
                default_sl_dup = gf_strdup (default_sl);
                if (!default_sl_dup) {
                        ret = -1;
                        goto out;
                }
                ret = dict_set_dynstr (aggr, "default-soft-limit",
                                       default_sl_dup);
                if (ret) {
                        gf_log (frame->this->name, GF_LOG_TRACE,
                                "failed to set default soft limit");
                        GF_FREE (default_sl_dup);
                }
        }

        ret = dict_get_int32 (dict, "type", &type);
        if (ret)
                gf_log (frame->this->name, GF_LOG_TRACE,
                        "failed to get type");

        ret = dict_get_int32 (dict, "count", &entry_count);
        if (ret)
                gf_log (frame->this->name, GF_LOG_TRACE, "failed to get count");

        if (type == GF_QUOTA_OPTION_TYPE_LIST) {
                if (global_state->mode & GLUSTER_MODE_XML) {
                        ret = cli_xml_output_vol_quota_limit_list
                                (volname, limit_list, rsp.op_ret,
                                 rsp.op_errno, rsp.op_errstr);
                        if (ret)
                                gf_log ("cli", GF_LOG_ERROR,
                                        "Error outputting to xml");
                        goto out;
                }

                gf_cli_quota_list (volname, dict, entry_count, rsp.op_errstr,
                                   default_sl);
        }

xml_output:
        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_str ("volQuota", NULL, rsp.op_ret,
                                          rsp.op_errno, rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (!rsp.op_ret && type != GF_QUOTA_OPTION_TYPE_LIST)
                cli_out ("volume quota : success");

        ret = rsp.op_ret;
out:
        cli_cmd_broadcast_response (ret);
        if (dict)
                dict_unref (dict);

        free (rsp.dict.dict_val);

        return ret;
}

int
gf_cli_getspec_cbk (struct rpc_req *req, struct iovec *iov,
                       int count, void *myframe)
{
        gf_getspec_rsp          rsp   = {0,};
        int                     ret   = -1;
        char                   *spec  = NULL;
        call_frame_t           *frame = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        frame = myframe;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_getspec_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        if (rsp.op_ret == -1) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "getspec failed");
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
gf_cli_pmap_b2p_cbk (struct rpc_req *req, struct iovec *iov,
                        int count, void *myframe)
{
        pmap_port_by_brick_rsp rsp = {0,};
        int                     ret   = -1;
        char                   *spec  = NULL;
        call_frame_t           *frame = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        frame = myframe;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_pmap_port_by_brick_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        if (rsp.op_ret == -1) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "pump_b2p failed");
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
gf_cli_probe (call_frame_t *frame, xlator_t *this,
                 void *data)
{
        gf_cli_req         req      = {{0,},};
        int                ret      = 0;
        dict_t            *dict     = NULL;
        int                port     = 0;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_get_int32 (dict, "port", &port);
        if (ret) {
                ret = dict_set_int32 (dict, "port", CLI_GLUSTERD_PORT);
                if (ret)
                        goto out;
        }

        ret = cli_to_glusterd (&req, frame, gf_cli_probe_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_PROBE, this, cli_rpc_prog, NULL);

out:
        GF_FREE (req.dict.dict_val);
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli_deprobe (call_frame_t *frame, xlator_t *this,
                   void *data)
{
        gf_cli_req           req      = {{0,},};
        int                  ret      = 0;
        dict_t              *dict     = NULL;
        int                  port     = 0;
        int                  flags    = 0;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;
        ret = dict_get_int32 (dict, "port", &port);
        if (ret) {
                ret = dict_set_int32 (dict, "port", CLI_GLUSTERD_PORT);
                if (ret)
                        goto out;
        }

        ret = dict_get_int32 (dict, "flags", &flags);
        if (ret) {
                ret = dict_set_int32 (dict, "flags", 0);
                if (ret)
                        goto out;
        }

        ret = cli_to_glusterd (&req, frame, gf_cli_deprobe_cbk,
                               (xdrproc_t)xdr_gf_cli_req, dict,
                              GLUSTER_CLI_DEPROBE, this, cli_rpc_prog, NULL);

out:
        GF_FREE (req.dict.dict_val);
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli_list_friends (call_frame_t *frame, xlator_t *this,
                     void *data)
{
        gf1_cli_peer_list_req   req = {0,};
        int                     ret = 0;
        unsigned long           flags = 0;

        if (!frame || !this) {
                ret = -1;
                goto out;
        }

        GF_ASSERT (frame->local == NULL);

        flags = (long)data;
        req.flags = flags;
        frame->local = (void*)flags;
        ret = cli_cmd_submit (NULL, &req, frame, cli_rpc_prog,
                              GLUSTER_CLI_LIST_FRIENDS, NULL,
                              this, gf_cli_list_friends_cbk,
                              (xdrproc_t) xdr_gf1_cli_peer_list_req);

out:
        if (ret) {
                /*
                 * If everything goes fine, gf_cli_list_friends_cbk()
                 * [invoked through cli_cmd_submit()]resets the
                 * frame->local to NULL. In case cli_cmd_submit()
                 * fails in between, RESET frame->local here.
                 */
                frame->local = NULL;
        }
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
gf_cli_get_next_volume (call_frame_t *frame, xlator_t *this,
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
        local = frame->local;

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_vol_info_begin (local, 0, 0, "");
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Error outputting to xml");
                        goto out;
                }
        }

        ret = gf_cli_get_volume (frame, this, data);


        if (!local || !local->get_vol.volname) {
                if ((global_state->mode & GLUSTER_MODE_XML))
                        goto end_xml;

                cli_err ("No volumes present");
                goto out;
        }


        ctx->volname = local->get_vol.volname;

        while (ctx->volname) {
                ret = gf_cli_get_volume (frame, this, ctx);
                if (ret)
                        goto out;
                ctx->volname = local->get_vol.volname;
        }

end_xml:
        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_vol_info_end (local);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR, "Error outputting to xml");
        }

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
gf_cli_get_volume (call_frame_t *frame, xlator_t *this,
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
                gf_log (frame->this->name, GF_LOG_ERROR, "failed to set flags");
                goto out;
        }

        ret = dict_allocate_and_serialize (dict, &req.dict.dict_val,
                                           &req.dict.dict_len);

        ret = cli_cmd_submit (NULL, &req, frame, cli_rpc_prog,
                              GLUSTER_CLI_GET_VOLUME, NULL,
                              this, gf_cli_get_volume_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

out:
        if (dict)
                dict_unref (dict);

        GF_FREE (req.dict.dict_val);

        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
gf_cli3_1_uuid_get (call_frame_t *frame, xlator_t *this,
                      void *data)
{
        gf_cli_req                      req = {{0,}};
        int                             ret = 0;
        dict_t                          *dict = NULL;

        if (!frame || !this || !data) {
                ret = -1;
                goto out;
        }

        dict = data;
        ret = cli_to_glusterd (&req, frame, gf_cli3_1_uuid_get_cbk,
                               (xdrproc_t)xdr_gf_cli_req, dict,
                               GLUSTER_CLI_UUID_GET, this, cli_rpc_prog,
                               NULL);
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
gf_cli3_1_uuid_reset (call_frame_t *frame, xlator_t *this,
                      void *data)
{
        gf_cli_req                      req = {{0,}};
        int                             ret = 0;
        dict_t                          *dict = NULL;

        if (!frame || !this || !data) {
                ret = -1;
                goto out;
        }

        dict = data;
        ret = cli_to_glusterd (&req, frame, gf_cli3_1_uuid_reset_cbk,
                               (xdrproc_t)xdr_gf_cli_req, dict,
                               GLUSTER_CLI_UUID_RESET, this, cli_rpc_prog,
                               NULL);
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
gf_cli_create_volume (call_frame_t *frame, xlator_t *this,
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

        ret = cli_to_glusterd (&req, frame, gf_cli_create_volume_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_CREATE_VOLUME, this, cli_rpc_prog,
                               NULL);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        GF_FREE (req.dict.dict_val);

        return ret;
}

int32_t
gf_cli_delete_volume (call_frame_t *frame, xlator_t *this,
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

        ret = cli_to_glusterd (&req, frame, gf_cli_delete_volume_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_DELETE_VOLUME, this, cli_rpc_prog,
                               NULL);

out:
        GF_FREE (req.dict.dict_val);
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli_start_volume (call_frame_t *frame, xlator_t *this,
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

        ret = cli_to_glusterd (&req, frame, gf_cli_start_volume_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_START_VOLUME, this, cli_rpc_prog,
                               NULL);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli_stop_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_cli_req             req = {{0,}};
        int                    ret = 0;
        dict_t                 *dict = data;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = cli_to_glusterd (&req, frame, gf_cli_stop_volume_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_STOP_VOLUME, this, cli_rpc_prog,
                               NULL);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli_defrag_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_cli_req              req     =  {{0,}};
        int                     ret     = 0;
        dict_t                 *dict    = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = cli_to_glusterd (&req, frame, gf_cli_defrag_volume_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_DEFRAG_VOLUME, this, cli_rpc_prog,
                               NULL);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli_rename_volume (call_frame_t *frame, xlator_t *this,
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

        ret = dict_allocate_and_serialize (dict, &req.dict.dict_val,
                                           &req.dict.dict_len);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to serialize the data");

                goto out;
        }


        ret = cli_cmd_submit (NULL, &req, frame, cli_rpc_prog,
                              GLUSTER_CLI_RENAME_VOLUME, NULL,
                              this, gf_cli_rename_volume_cbk,
                              (xdrproc_t) xdr_gf_cli_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli_reset_volume (call_frame_t *frame, xlator_t *this,
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

        ret = cli_to_glusterd (&req, frame, gf_cli_reset_volume_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_RESET_VOLUME, this, cli_rpc_prog,
                               NULL);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
gf_cli_set_volume (call_frame_t *frame, xlator_t *this,
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

        ret = cli_to_glusterd (&req, frame, gf_cli_set_volume_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_SET_VOLUME, this, cli_rpc_prog,
                               NULL);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli_add_brick (call_frame_t *frame, xlator_t *this,
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

        ret = cli_to_glusterd (&req, frame, gf_cli_add_brick_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_ADD_BRICK, this, cli_rpc_prog, NULL);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        GF_FREE (req.dict.dict_val);

        return ret;
}

int32_t
gf_cli_remove_brick (call_frame_t *frame, xlator_t *this,
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

        if ((command != GF_OP_CMD_STATUS) &&
            (command != GF_OP_CMD_STOP)) {


                ret = cli_to_glusterd (&req, frame, gf_cli_remove_brick_cbk,
                                       (xdrproc_t) xdr_gf_cli_req, dict,
                                       GLUSTER_CLI_REMOVE_BRICK, this,
                                       cli_rpc_prog, NULL);
        } else {
                /* Need rebalance status to be sent :-) */
                req_dict = dict_new ();
                if (!req_dict) {
                        ret = -1;
                        goto out;
                }

                ret = dict_set_str (req_dict, "volname", volname);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to set dict");
                        goto out;
                }

                if (command == GF_OP_CMD_STATUS)
                        cmd |= GF_DEFRAG_CMD_STATUS;
                else
                        cmd |= GF_DEFRAG_CMD_STOP;

                ret = dict_set_int32 (req_dict, "rebalance-command", (int32_t) cmd);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to set dict");
                        goto out;
                }

                ret = cli_to_glusterd (&status_req, frame,
                                       gf_cli3_remove_brick_status_cbk,
                                       (xdrproc_t) xdr_gf_cli_req, req_dict,
                                       GLUSTER_CLI_DEFRAG_VOLUME, this,
                                       cli_rpc_prog, NULL);

                }

out:
        if (req_dict)
                dict_unref (req_dict);
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        GF_FREE (req.dict.dict_val);

        GF_FREE (status_req.dict.dict_val);

        return ret;
}

int32_t
gf_cli_replace_brick (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_cli_req                  req        =  {{0,}};
        int                         ret        = 0;
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

        ret = cli_to_glusterd (&req, frame, gf_cli_replace_brick_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_REPLACE_BRICK, this, cli_rpc_prog,
                               NULL);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        GF_FREE (req.dict.dict_val);

        return ret;
}


int32_t
gf_cli_log_rotate (call_frame_t *frame, xlator_t *this,
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

        ret = cli_to_glusterd (&req, frame, gf_cli_log_rotate_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_LOG_ROTATE, this, cli_rpc_prog,
                               NULL);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        GF_FREE (req.dict.dict_val);
        return ret;
}

int32_t
gf_cli_sync_volume (call_frame_t *frame, xlator_t *this,
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

        ret = cli_to_glusterd (&req, frame, gf_cli_sync_volume_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_SYNC_VOLUME, this, cli_rpc_prog,
                               NULL);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        GF_FREE (req.dict.dict_val);

        return ret;
}

int32_t
gf_cli_getspec (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_getspec_req          req = {0,};
        int                     ret = 0;
        dict_t                  *dict = NULL;
        dict_t                  *op_dict = NULL;

        if (!frame || !this ||  !data) {
                ret = -1;
                goto out;
        }

        dict = data;

        ret = dict_get_str (dict, "volid", &req.key);
        if (ret)
                goto out;

        op_dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        // Set the supported min and max op-versions, so glusterd can make a
        // decision
        ret = dict_set_int32 (op_dict, "min-op-version", GD_OP_VERSION_MIN);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to set min-op-version"
                        " in request dict");
                goto out;
        }

        ret = dict_set_int32 (op_dict, "max-op-version", GD_OP_VERSION_MAX);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to set max-op-version"
                        " in request dict");
                goto out;
        }

        ret = dict_allocate_and_serialize (op_dict, &req.xdata.xdata_val,
                                           &req.xdata.xdata_len);
        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to serialize dictionary");
                goto out;
        }

        ret = cli_cmd_submit (NULL, &req, frame, &cli_handshake_prog,
                              GF_HNDSK_GETSPEC, NULL,
                              this, gf_cli_getspec_cbk,
                              (xdrproc_t) xdr_gf_getspec_req);

out:
        if (op_dict) {
                dict_unref(op_dict);
        }
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int32_t
gf_cli_quota (call_frame_t *frame, xlator_t *this,
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

        ret = cli_to_glusterd (&req, frame, gf_cli_quota_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_QUOTA, this, cli_rpc_prog, NULL);

out:
        GF_FREE (req.dict.dict_val);

        return ret;
}

int32_t
gf_cli_pmap_b2p (call_frame_t *frame, xlator_t *this, void *data)
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

        ret = cli_cmd_submit (NULL, &req, frame, &cli_pmap_prog,
                              GF_PMAP_PORTBYBRICK, NULL,
                              this, gf_cli_pmap_b2p_cbk,
                              (xdrproc_t) xdr_pmap_port_by_brick_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

static int
gf_cli_fsm_log_cbk (struct rpc_req *req, struct iovec *iov,
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
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        if (rsp.op_ret) {
                if (strcmp (rsp.op_errstr, ""))
                        cli_err ("%s", rsp.op_errstr);
                cli_err ("fsm log unsuccessful");
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
                cli_err ("bad response");
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &tr_count);
        if (tr_count)
                cli_out("number of transitions: %d", tr_count);
        else
                cli_err("No transitions");
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
gf_cli_fsm_log (call_frame_t *frame, xlator_t *this, void *data)
{
        int                        ret = -1;
        gf1_cli_fsm_log_req        req = {0,};

        GF_ASSERT (frame);
        GF_ASSERT (this);
        GF_ASSERT (data);

        if (!frame || !this || !data)
                goto out;
        req.name = data;
        ret = cli_cmd_submit (NULL, &req, frame, cli_rpc_prog,
                              GLUSTER_CLI_FSM_LOG, NULL,
                              this, gf_cli_fsm_log_cbk,
                              (xdrproc_t) xdr_gf1_cli_fsm_log_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

int
gf_cli_gsync_config_command (dict_t *dict)
{
        runner_t runner     = {0,};
        char *subop         = NULL;
        char *gwd           = NULL;
        char *slave         = NULL;
        char *confpath      = NULL;
        char *master        = NULL;
        char *op_name       = NULL;
        int   ret           = -1;
        char  conf_path[PATH_MAX] = "";

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

        ret = dict_get_str (dict, "conf_path", &confpath);
        if (!confpath) {
                ret = snprintf (conf_path, sizeof(conf_path) - 1,
                                "%s/"GEOREP"/gsyncd_template.conf", gwd);
                conf_path[ret] = '\0';
                confpath = conf_path;
        }

        runinit (&runner);
        runner_add_args (&runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (&runner, "%s", confpath);
        if (master)
                runner_argprintf (&runner, ":%s", master);
        runner_add_arg (&runner, slave);
        runner_argprintf (&runner, "--config-%s", subop);
        if (op_name)
                runner_add_arg (&runner, op_name);

        return runner_run (&runner);
}

char*
get_struct_variable (int mem_num, gf_gsync_status_t *sts_val)
{
        switch (mem_num) {
        case 0:  return (sts_val->node);
        case 1:  return (sts_val->master);
        case 2:  return (sts_val->brick);
        case 3:  return (sts_val->slave_node);
        case 4:  return (sts_val->worker_status);
        case 5:  return (sts_val->checkpoint_status);
        case 6:  return (sts_val->crawl_status);
        case 7:  return (sts_val->files_syncd);
        case 8:  return (sts_val->files_remaining);
        case 9:  return (sts_val->bytes_remaining);
        case 10: return (sts_val->purges_remaining);
        case 11: return (sts_val->total_files_skipped);
        default:
                 goto out;
        }

out:
        return NULL;
}

int
gf_cli_print_status (char **title_values,
                     gf_gsync_status_t **sts_vals,
                     int *spacing, int gsync_count,
                     int number_of_fields, int is_detail)
{
        int     i                        = 0;
        int     j                        = 0;
        int     ret                      = 0;
        int     status_fields            = 6; /* Indexed at 0 */
        int     total_spacing            = 0;
        char  **output_values            = NULL;
        char   *tmp                      = NULL;
        char   *hyphens                  = NULL;

        /* calculating spacing for hyphens */
        for (i = 0; i < number_of_fields; i++) {
                /* Suppressing detail output for status */
                if ((!is_detail) && (i > status_fields)) {
                       /* Suppressing detailed output for
                        * status */
                        continue;
                }
                spacing[i] += 3; /* Adding extra space to
                                    distinguish between fields */
                total_spacing += spacing[i];
        }
        total_spacing += 4; /* For the spacing between the fields */

        /* char pointers for each field */
        output_values = GF_CALLOC (number_of_fields, sizeof (char *),
                                   gf_common_mt_char);
        if (!output_values) {
                ret = -1;
                goto out;
        }
        for (i = 0; i < number_of_fields; i++) {
                output_values[i] = GF_CALLOC (spacing[i] + 1, sizeof (char),
                                              gf_common_mt_char);
                if (!output_values[i]) {
                        ret = -1;
                        goto out;
                }
        }

        hyphens = GF_CALLOC (total_spacing + 1, sizeof (char),
                             gf_common_mt_char);
        if (!hyphens) {
                ret = -1;
                goto out;
        }

        cli_out (" ");

        /* setting the title "NODE", "MASTER", etc. from title_values[]
           and printing the same */
        for (j = 0; j < number_of_fields; j++) {
                if ((!is_detail) && (j > status_fields)) {
                       /* Suppressing detailed output for
                        * status */
                       output_values[j][0] = '\0';
                       continue;
                }
                memset (output_values[j], ' ', spacing[j]);
                memcpy (output_values[j], title_values[j],
                        strlen(title_values[j]));
                output_values[j][spacing[j]] = '\0';
        }
        cli_out ("%s %s %s %s %s %s %s %s %s %s %s %s",
                 output_values[0], output_values[1],
                 output_values[2], output_values[3],
                 output_values[4], output_values[5],
                 output_values[6], output_values[7],
                 output_values[8], output_values[9],
                 output_values[10], output_values[11]);

        /* setting and printing the hyphens */
        memset (hyphens, '-', total_spacing);
        hyphens[total_spacing] = '\0';
        cli_out ("%s", hyphens);

        for (i = 0; i < gsync_count; i++) {
                for (j = 0; j < number_of_fields; j++) {
                        if ((!is_detail) && (j > status_fields)) {
                                /* Suppressing detailed output for
                                 * status */
                                output_values[j][0] = '\0';
                                continue;
                        }
                        tmp = get_struct_variable(j, sts_vals[i]);
                        if (!tmp) {
                                gf_log ("", GF_LOG_ERROR,
                                        "struct member empty.");
                                ret = -1;
                                goto out;
                        }
                        memset (output_values[j], ' ', spacing[j]);
                        memcpy (output_values[j], tmp, strlen (tmp));
                        output_values[j][spacing[j]] = '\0';
                }

                cli_out ("%s %s %s %s %s %s %s %s %s %s %s %s",
                         output_values[0], output_values[1],
                         output_values[2], output_values[3],
                         output_values[4], output_values[5],
                         output_values[6], output_values[7],
                         output_values[8], output_values[9],
                         output_values[10], output_values[11]);
        }

out:
        if (output_values) {
                for (i = 0; i < number_of_fields; i++) {
                        if (output_values[i])
                                GF_FREE (output_values[i]);
                }
                GF_FREE (output_values);
        }

        if (hyphens)
                GF_FREE (hyphens);

        return ret;
}

int
gf_cli_read_status_data (dict_t *dict,
                         gf_gsync_status_t **sts_vals,
                         int *spacing, int gsync_count,
                         int number_of_fields)
{
        char   *tmp                    = NULL;
        char    sts_val_name[PATH_MAX] = "";
        int     ret                    = 0;
        int     i                      = 0;
        int     j                      = 0;

        /* Storing per node status info in each object */
        for (i = 0; i < gsync_count; i++) {
                snprintf (sts_val_name, sizeof(sts_val_name), "status_value%d", i);

                /* Fetching the values from dict, and calculating
                   the max length for each field */
                ret = dict_get_bin (dict, sts_val_name, (void **)&(sts_vals[i]));
                if (ret)
                        goto out;

                for (j = 0; j < number_of_fields; j++) {
                        tmp = get_struct_variable(j, sts_vals[i]);
                        if (!tmp) {
                                gf_log ("", GF_LOG_ERROR,
                                        "struct member empty.");
                                ret = -1;
                                goto out;
                        }
                        if (strlen (tmp) > spacing[j])
                                spacing[j] = strlen (tmp);
                }
        }

out:
        return ret;
}

int
gf_cli_gsync_status_output (dict_t *dict, gf_boolean_t is_detail)
{
        int                     gsync_count    = 0;
        int                     i              = 0;
        int                     ret            = 0;
        int                     spacing[13]    = {0};
        int                     num_of_fields  = 12;
        char                    errmsg[1024]   = "";
        char                   *master         = NULL;
        char                   *slave          = NULL;
        char                   *title_values[] = {"MASTER NODE", "MASTER VOL",
                                                  "MASTER BRICK", "SLAVE",
                                                  "STATUS", "CHECKPOINT STATUS",
                                                  "CRAWL STATUS", "FILES SYNCD",
                                                  "FILES PENDING", "BYTES PENDING",
                                                  "DELETES PENDING", "FILES SKIPPED"};
        gf_gsync_status_t     **sts_vals       = NULL;

        /* Checks if any session is active or not */
        ret = dict_get_int32 (dict, "gsync-count", &gsync_count);
        if (ret) {
                ret = dict_get_str (dict, "master", &master);

                ret = dict_get_str (dict, "slave", &slave);

                if (master) {
                        if (slave)
                                snprintf (errmsg, sizeof(errmsg), "No active "
                                          "geo-replication sessions between %s"
                                          " and %s", master, slave);
                        else
                                snprintf (errmsg, sizeof(errmsg), "No active "
                                          "geo-replication sessions for %s",
                                          master);
                } else
                        snprintf (errmsg, sizeof(errmsg), "No active "
                                  "geo-replication sessions");

                gf_log ("cli", GF_LOG_INFO, "%s", errmsg);
                cli_out ("%s", errmsg);
                ret = 0;
                goto out;
        }

        for (i = 0; i < num_of_fields; i++)
                spacing[i] = strlen(title_values[i]);

        /* gsync_count = number of nodes reporting output.
           each sts_val object will store output of each
           node */
        sts_vals = GF_CALLOC (gsync_count, sizeof (gf_gsync_status_t *),
                              gf_common_mt_char);
        if (!sts_vals) {
                ret = -1;
                goto out;
        }
        for (i = 0; i < gsync_count; i++) {
                sts_vals[i] = GF_CALLOC (1, sizeof (gf_gsync_status_t),
                                         gf_common_mt_char);
                if (!sts_vals[i]) {
                        ret = -1;
                        goto out;
                }
        }

        ret = gf_cli_read_status_data (dict, sts_vals, spacing,
                                       gsync_count, num_of_fields);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to read status data");
                goto out;
        }

        ret = gf_cli_print_status (title_values, sts_vals, spacing, gsync_count,
                                   num_of_fields, is_detail);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to print status output");
                goto out;
        }

out:
        if (sts_vals)
                GF_FREE (sts_vals);

        return ret;
}

static int32_t
write_contents_to_common_pem_file (dict_t *dict, int output_count)
{
        char            *workdir                   = NULL;
        char             common_pem_file[PATH_MAX] = "";
        char            *output                    = NULL;
        char             output_name[PATH_MAX]     = "";
        int              bytes_writen              = 0;
        int              fd                        = -1;
        int              ret                       = -1;
        int              i                         = -1;

        ret = dict_get_str (dict, "glusterd_workdir", &workdir);
        if (ret || !workdir) {
                gf_log ("", GF_LOG_ERROR, "Unable to fetch workdir");
                ret = -1;
                goto out;
        }

        snprintf (common_pem_file, sizeof(common_pem_file),
                  "%s/geo-replication/common_secret.pem.pub",
                  workdir);

        unlink (common_pem_file);

        fd = open (common_pem_file, O_WRONLY | O_CREAT, 0600);
        if (fd == -1) {
                gf_log ("", GF_LOG_ERROR, "Failed to open %s"
                        " Error : %s", common_pem_file,
                        strerror (errno));
                ret = -1;
                goto out;
        }

        for (i = 1; i <= output_count; i++) {
                memset (output_name, '\0', sizeof (output_name));
                snprintf (output_name, sizeof (output_name),
                          "output_%d", i);
                ret = dict_get_str (dict, output_name, &output);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Failed to get %s.",
                                output_name);
                        cli_out ("Unable to fetch output.");
                }
                if (output) {
                        bytes_writen = write (fd, output, strlen(output));
                        if (bytes_writen != strlen(output)) {
                                gf_log ("", GF_LOG_ERROR, "Failed to write "
                                        "to %s", common_pem_file);
                                ret = -1;
                                goto out;
                        }
                        /* Adding the new line character */
                        bytes_writen = write (fd, "\n", strlen("\n"));
                        if (bytes_writen != strlen("\n")) {
                                gf_log ("", GF_LOG_ERROR,
                                        "Failed to add new line char");
                                ret = -1;
                                goto out;
                        }
                        output = NULL;
                }
        }

        cli_out ("Common secret pub file present at %s", common_pem_file);
        ret = 0;
out:
        if (fd)
                close (fd);

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
gf_cli_sys_exec_cbk (struct rpc_req *req, struct iovec *iov,
                     int count, void *myframe)
{
        int                     ret     = -1;
        int                     output_count     = -1;
        int                     i     = -1;
        char                   *output  = NULL;
        char                   *command = NULL;
        char                    output_name[PATH_MAX] = "";
        gf_cli_rsp              rsp     = {0, };
        dict_t                  *dict   = NULL;
        call_frame_t            *frame  = NULL;

        if (req->rpc_status == -1) {
                ret = -1;
                goto out;
        }

        frame = myframe;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
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
                cli_err ("%s", rsp.op_errstr ? rsp.op_errstr :
                         "Command failed.");
                ret = rsp.op_ret;
                goto out;
        }

        ret = dict_get_int32 (dict, "output_count", &output_count);
        if (ret) {
                cli_out ("Command executed successfully.");
                ret = 0;
                goto out;
        }

        ret = dict_get_str (dict, "command", &command);
        if (ret) {
                gf_log ("", GF_LOG_ERROR,
                        "Unable to get command from dict");
                goto out;
        }

        if (!strcmp (command, "gsec_create")) {
                ret = write_contents_to_common_pem_file (dict, output_count);
                if (!ret)
                        goto out;
        }

        for (i = 1; i <= output_count; i++) {
                memset (output_name, '\0', sizeof (output_name));
                snprintf (output_name, sizeof (output_name),
                          "output_%d", i);
                ret = dict_get_str (dict, output_name, &output);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Failed to get %s.",
                                output_name);
                        cli_out ("Unable to fetch output.");
                }
                if (output) {
                        cli_out ("%s", output);
                        output = NULL;
                }
        }

        ret = 0;
out:
        if (dict)
                dict_unref (dict);
        cli_cmd_broadcast_response (ret);

        free (rsp.dict.dict_val);

        return ret;
}

int
gf_cli_copy_file_cbk (struct rpc_req *req, struct iovec *iov,
                      int count, void *myframe)
{
        int                     ret     = -1;
        gf_cli_rsp              rsp     = {0, };
        dict_t                  *dict   = NULL;
        call_frame_t            *frame  = NULL;

        if (req->rpc_status == -1) {
                ret = -1;
                goto out;
        }

        frame = myframe;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
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
                cli_err ("%s", rsp.op_errstr ? rsp.op_errstr :
                         "Copy unsuccessful");
                ret = rsp.op_ret;
                goto out;
        }

        cli_out ("Successfully copied file.");

out:
        if (dict)
                dict_unref (dict);
        cli_cmd_broadcast_response (ret);

        free (rsp.dict.dict_val);

        return ret;
}

int
gf_cli_gsync_set_cbk (struct rpc_req *req, struct iovec *iov,
                         int count, void *myframe)
{
        int                     ret     = -1;
        gf_cli_rsp              rsp     = {0, };
        dict_t                  *dict   = NULL;
        char                    *gsync_status = NULL;
        char                    *master = NULL;
        char                    *slave  = NULL;
        int32_t                  type   = 0;
        call_frame_t            *frame  = NULL;
        gf_boolean_t             status_detail = _gf_false;


        if (req->rpc_status == -1) {
                ret = -1;
                goto out;
        }

        frame = myframe;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
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

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_vol_gsync (dict, rsp.op_ret, rsp.op_errno,
                                                rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret) {
                cli_err ("%s", rsp.op_errstr ? rsp.op_errstr :
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
                gf_log (frame->this->name, GF_LOG_ERROR, "failed to get type");
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
                        ret = gf_cli_gsync_config_command (dict);
                break;

                case GF_GSYNC_OPTION_TYPE_STATUS:
                        status_detail = dict_get_str_boolean (dict,
                                                              "status-detail",
                                                              _gf_false);
                        if (status_detail)
                                ret = gf_cli_gsync_status_output (dict, status_detail);
                        else
                                ret = gf_cli_gsync_status_output (dict, status_detail);
                break;

                case GF_GSYNC_OPTION_TYPE_DELETE:
                        if (dict_get_str (dict, "master", &master) != 0)
                                master = "???";
                        if (dict_get_str (dict, "slave", &slave) != 0)
                                slave = "???";
                        cli_out ("Deleting " GEOREP " session between %s & %s"
                                 " has been successful", master, slave);
                break;

                case GF_GSYNC_OPTION_TYPE_CREATE:
                        if (dict_get_str (dict, "master", &master) != 0)
                                master = "???";
                        if (dict_get_str (dict, "slave", &slave) != 0)
                                slave = "???";
                        cli_out ("Creating " GEOREP " session between %s & %s"
                                 " has been successful", master, slave);
                break;

                default:
                        cli_out (GEOREP" command executed successfully");
        }

out:
        if (dict)
                dict_unref (dict);
        cli_cmd_broadcast_response (ret);

        free (rsp.dict.dict_val);

        return ret;
}

int32_t
gf_cli_sys_exec (call_frame_t *frame, xlator_t *this, void *data)
{
        int                      ret    = 0;
        dict_t                  *dict   = NULL;
        gf_cli_req               req = {{0,}};

        if (!frame || !this || !data) {
                ret = -1;
                gf_log ("cli", GF_LOG_ERROR, "Invalid data");
                goto out;
        }

        dict = data;

        ret = cli_to_glusterd (&req, frame, gf_cli_sys_exec_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_SYS_EXEC, this, cli_rpc_prog,
                               NULL);
out:
        GF_FREE (req.dict.dict_val);
        return ret;
}

int32_t
gf_cli_copy_file (call_frame_t *frame, xlator_t *this, void *data)
{
        int                      ret    = 0;
        dict_t                  *dict   = NULL;
        gf_cli_req               req = {{0,}};

        if (!frame || !this || !data) {
                ret = -1;
                gf_log ("cli", GF_LOG_ERROR, "Invalid data");
                goto out;
        }

        dict = data;

        ret = cli_to_glusterd (&req, frame, gf_cli_copy_file_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_COPY_FILE, this, cli_rpc_prog,
                               NULL);
out:
        GF_FREE (req.dict.dict_val);
        return ret;
}

int32_t
gf_cli_gsync_set (call_frame_t *frame, xlator_t *this,
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

        ret = cli_to_glusterd (&req, frame, gf_cli_gsync_set_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_GSYNC_SET, this, cli_rpc_prog,
                               NULL);

out:
        GF_FREE (req.dict.dict_val);

        return ret;
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
cmd_profile_volume_brick_out (dict_t *dict, int count, int interval)
{
        char                    key[256] = {0};
        int                     i = 0;
        uint64_t                sec = 0;
        uint64_t                r_count = 0;
        uint64_t                w_count = 0;
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
                profile_info[i].fop_name = (char *)gf_fop_list[i];

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
gf_cli_profile_volume_cbk (struct rpc_req *req, struct iovec *iov,
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
        char                              *brick = NULL;
        char                              str[1024] = {0,};
        int                               stats_cleared = 0;
        gf1_cli_info_op                   info_op = GF_CLI_INFO_NONE;

        if (-1 == req->rpc_status) {
                goto out;
        }

        gf_log ("cli", GF_LOG_DEBUG, "Received resp to profile");
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
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

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_vol_profile (dict, rsp.op_ret,
                                                  rsp.op_errno,
                                                  rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "op", (int32_t*)&op);
        if (ret)
                goto out;

        if (rsp.op_ret && strcmp (rsp.op_errstr, "")) {
                cli_err ("%s", rsp.op_errstr);
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

        if (GF_CLI_STATS_INFO != op) {
                ret = 0;
                goto out;
        }

        ret = dict_get_int32 (dict, "info-op", (int32_t*)&info_op);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret)
                goto out;

        if (!brick_count) {
                cli_err ("All bricks of volume %s are down.", volname);
                goto out;
        }

        while (i <= brick_count) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%d-brick", i);
                ret = dict_get_str (dict, key, &brick);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Couldn't get brick name");
                        goto out;
                }

                ret = dict_get_str_boolean (dict, "nfs", _gf_false);

                if (ret)
                        snprintf (str, sizeof (str), "NFS Server : %s", brick);
                else
                        snprintf (str, sizeof (str), "Brick: %s", brick);
                cli_out ("%s", str);
                memset (str, '-', strlen (str));
                cli_out ("%s", str);

                if (GF_CLI_INFO_CLEAR == info_op) {
                        snprintf (key, sizeof (key), "%d-stats-cleared", i);
                        ret = dict_get_int32 (dict, key, &stats_cleared);
                        if (ret)
                                goto out;
                        cli_out (stats_cleared ? "Cleared stats." :
                                                 "Failed to clear stats.");
                } else {
                        snprintf (key, sizeof (key), "%d-cumulative", i);
                        ret = dict_get_int32 (dict, key, &interval);
                        if (ret == 0)
                                cmd_profile_volume_brick_out (dict, i,
                                                              interval);

                        snprintf (key, sizeof (key), "%d-interval", i);
                        ret = dict_get_int32 (dict, key, &interval);
                        if (ret == 0)
                                cmd_profile_volume_brick_out (dict, i,
                                                              interval);
                }
                i++;
        }
        ret = rsp.op_ret;

out:
        if (dict)
                dict_unref (dict);
        free (rsp.op_errstr);
        cli_cmd_broadcast_response (ret);
        return ret;
}

int32_t
gf_cli_profile_volume (call_frame_t *frame, xlator_t *this, void *data)
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

        ret = cli_to_glusterd (&req, frame, gf_cli_profile_volume_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_PROFILE_VOLUME, this, cli_rpc_prog,
                               NULL);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        GF_FREE (req.dict.dict_val);
        return ret;
}

int32_t
gf_cli_top_volume_cbk (struct rpc_req *req, struct iovec *iov,
                              int count, void *myframe)
{
        gf_cli_rsp                        rsp   = {0,};
        int                               ret   = -1;
        dict_t                           *dict = NULL;
        gf1_cli_stats_op                  op = GF_CLI_STATS_NONE;
        char                              key[256] = {0};
        int                               i = 0;
        int32_t                           brick_count = 0;
        char                              brick[1024];
        int32_t                           members = 0;
        char                             *filename;
        char                             *bricks;
        uint64_t                          value = 0;
        int32_t                           j = 0;
        gf1_cli_top_op                    top_op = GF_CLI_TOP_NONE;
        uint64_t                          nr_open = 0;
        uint64_t                          max_nr_open = 0;
        double                            throughput = 0;
        double                            time = 0;
        int32_t                           time_sec = 0;
        long int                          time_usec = 0;
        char                              timestr[256] = {0, };
        char                             *openfd_str = NULL;
        gf_boolean_t                      nfs = _gf_false;
        gf_boolean_t                      clear_stats = _gf_false;
        int                               stats_cleared = 0;

        if (-1 == req->rpc_status) {
                goto out;
        }

        gf_log ("cli", GF_LOG_DEBUG, "Received resp to top");
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        if (rsp.op_ret) {
                if (strcmp (rsp.op_errstr, ""))
                        cli_err ("%s", rsp.op_errstr);
                cli_err ("volume top unsuccessful");
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

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_vol_top (dict, rsp.op_ret,
                                              rsp.op_errno,
                                              rsp.op_errstr);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                }
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret)
                goto out;
        snprintf (key, sizeof (key), "%d-top-op", 1);
        ret = dict_get_int32 (dict, key, (int32_t*)&top_op);
        if (ret)
                goto out;

        clear_stats = dict_get_str_boolean (dict, "clear-stats", _gf_false);

        while (i < brick_count) {
                i++;
                snprintf (brick, sizeof (brick), "%d-brick", i);
                ret = dict_get_str (dict, brick, &bricks);
                if (ret)
                        goto out;

                nfs = dict_get_str_boolean (dict, "nfs", _gf_false);

                if (clear_stats) {
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "%d-stats-cleared", i);
                        ret = dict_get_int32 (dict, key, &stats_cleared);
                        if (ret)
                                goto out;
                        cli_out (stats_cleared ? "Cleared stats for %s %s" :
                                 "Failed to clear stats for %s %s",
                                 nfs ? "NFS server on" : "brick", bricks);
                        continue;
                }

                if (nfs)
                        cli_out ("NFS Server : %s", bricks);
                else
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
                                gf_time_fmt (timestr, sizeof timestr,
                                             time_sec, gf_timefmt_FT);
                                snprintf (timestr + strlen (timestr), sizeof timestr - strlen (timestr),
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

        free (rsp.dict.dict_val);
        return ret;
}

int32_t
gf_cli_top_volume (call_frame_t *frame, xlator_t *this, void *data)
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

        ret = cli_to_glusterd (&req, frame, gf_cli_top_volume_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_PROFILE_VOLUME, this, cli_rpc_prog,
                               NULL);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        GF_FREE (req.dict.dict_val);
        return ret;
}


int
gf_cli_getwd_cbk (struct rpc_req *req, struct iovec *iov,
                       int count, void *myframe)
{
        gf1_cli_getwd_rsp rsp   = {0,};
        int               ret   = -1;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf1_cli_getwd_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        if (rsp.op_ret == -1) {
                cli_err ("getwd failed");
                ret = rsp.op_ret;
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
gf_cli_getwd (call_frame_t *frame, xlator_t *this, void *data)
{
        int                      ret = -1;
        gf1_cli_getwd_req        req = {0,};

        GF_ASSERT (frame);
        GF_ASSERT (this);

        if (!frame || !this)
                goto out;

        ret = cli_cmd_submit (NULL, &req, frame, cli_rpc_prog,
                              GLUSTER_CLI_GETWD, NULL,
                              this, gf_cli_getwd_cbk,
                              (xdrproc_t) xdr_gf1_cli_getwd_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}

void
cli_print_volume_status_mempool (dict_t *dict, char *prefix)
{
        int             ret = -1;
        int32_t         mempool_count = 0;
        char            *name = NULL;
        int32_t         hotcount = 0;
        int32_t         coldcount = 0;
        uint64_t        paddedsizeof = 0;
        uint64_t        alloccount = 0;
        int32_t         maxalloc = 0;
        uint64_t        pool_misses = 0;
        int32_t         maxstdalloc = 0;
        char            key[1024] = {0,};
        int             i = 0;

        GF_ASSERT (dict);
        GF_ASSERT (prefix);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.mempool-count",prefix);
        ret = dict_get_int32 (dict, key, &mempool_count);
        if (ret)
                goto out;

        cli_out ("Mempool Stats\n-------------");
        cli_out ("%-30s %9s %9s %12s %10s %8s %8s %12s", "Name", "HotCount",
                 "ColdCount", "PaddedSizeof", "AllocCount", "MaxAlloc",
                 "Misses", "Max-StdAlloc");
        cli_out ("%-30s %9s %9s %12s %10s %8s %8s %12s", "----", "--------",
                 "---------", "------------", "----------",
                 "--------", "--------", "------------");

        for (i = 0; i < mempool_count; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.pool%d.name", prefix, i);
                ret = dict_get_str (dict, key, &name);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.pool%d.hotcount", prefix, i);
                ret = dict_get_int32 (dict, key, &hotcount);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.pool%d.coldcount", prefix, i);
                ret = dict_get_int32 (dict, key, &coldcount);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.pool%d.paddedsizeof",
                          prefix, i);
                ret = dict_get_uint64 (dict, key, &paddedsizeof);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.pool%d.alloccount", prefix, i);
                ret = dict_get_uint64 (dict, key, &alloccount);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.pool%d.max_alloc", prefix, i);
                ret = dict_get_int32 (dict, key, &maxalloc);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.pool%d.max-stdalloc", prefix, i);
                ret = dict_get_int32 (dict, key, &maxstdalloc);
                if (ret)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.pool%d.pool-misses", prefix, i);
                ret = dict_get_uint64 (dict, key, &pool_misses);
                if (ret)
                        goto out;

                cli_out ("%-30s %9d %9d %12"PRIu64" %10"PRIu64" %8d %8"PRIu64
                         " %12d", name, hotcount, coldcount, paddedsizeof,
                         alloccount, maxalloc, pool_misses, maxstdalloc);
        }

out:
        return;

}

void
cli_print_volume_status_mem (dict_t *dict, gf_boolean_t notbrick)
{
        int             ret = -1;
        char            *volname = NULL;
        char            *hostname = NULL;
        char            *path = NULL;
        int             online = -1;
        char            key[1024] = {0,};
        int             brick_index_max = -1;
        int             other_count = 0;
        int             index_max = 0;
        int             val = 0;
        int             i = 0;

        GF_ASSERT (dict);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                goto out;
        cli_out ("Memory status for volume : %s", volname);

        ret = dict_get_int32 (dict, "brick-index-max", &brick_index_max);
        if (ret)
                goto out;
        ret = dict_get_int32 (dict, "other-count", &other_count);
        if (ret)
                goto out;

        index_max = brick_index_max + other_count;

        for (i = 0; i <= index_max; i++) {
                cli_out ("----------------------------------------------");

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.hostname", i);
                ret = dict_get_str (dict, key, &hostname);
                if (ret)
                        continue;
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.path", i);
                ret = dict_get_str (dict, key, &path);
                if (ret)
                        continue;
                if (notbrick)
                        cli_out ("%s : %s", hostname, path);
                else
                        cli_out ("Brick : %s:%s", hostname, path);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.status", i);
                ret = dict_get_int32 (dict, key, &online);
                if (ret)
                        goto out;
                if (!online) {
                        if (notbrick)
                                cli_out ("%s is offline", hostname);
                        else
                                cli_out ("Brick is offline");
                        continue;
                }

                cli_out ("Mallinfo\n--------");

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.mallinfo.arena", i);
                ret = dict_get_int32 (dict, key, &val);
                if (ret)
                        goto out;
                cli_out ("%-8s : %d","Arena", val);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.mallinfo.ordblks", i);
                ret = dict_get_int32 (dict, key, &val);
                if(ret)
                        goto out;
                cli_out ("%-8s : %d","Ordblks", val);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.mallinfo.smblks", i);
                ret = dict_get_int32 (dict, key, &val);
                if(ret)
                        goto out;
                cli_out ("%-8s : %d","Smblks", val);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.mallinfo.hblks", i);
                ret = dict_get_int32 (dict, key, &val);
                if(ret)
                        goto out;
                cli_out ("%-8s : %d", "Hblks", val);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.mallinfo.hblkhd", i);
                ret = dict_get_int32 (dict, key, &val);
                if (ret)
                        goto out;
                cli_out ("%-8s : %d", "Hblkhd", val);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.mallinfo.usmblks", i);
                ret = dict_get_int32 (dict, key, &val);
                if (ret)
                        goto out;
                cli_out ("%-8s : %d", "Usmblks", val);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.mallinfo.fsmblks", i);
                ret = dict_get_int32 (dict, key, &val);
                if (ret)
                        goto out;
                cli_out ("%-8s : %d", "Fsmblks", val);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.mallinfo.uordblks", i);
                ret = dict_get_int32 (dict, key, &val);
                if (ret)
                        goto out;
                cli_out ("%-8s : %d", "Uordblks", val);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.mallinfo.fordblks", i);
                ret = dict_get_int32 (dict, key, &val);
                if (ret)
                        goto out;
                cli_out ("%-8s : %d", "Fordblks", val);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.mallinfo.keepcost", i);
                ret = dict_get_int32 (dict, key, &val);
                if (ret)
                        goto out;
                cli_out ("%-8s : %d", "Keepcost", val);

                cli_out (" ");
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d", i);
                cli_print_volume_status_mempool (dict, key);
        }
out:
        cli_out ("----------------------------------------------\n");
        return;
}

void
cli_print_volume_status_clients (dict_t *dict, gf_boolean_t notbrick)
{
        int             ret = -1;
        char            *volname = NULL;
        int             brick_index_max = -1;
        int             other_count = 0;
        int             index_max = 0;
        char            *hostname = NULL;
        char            *path = NULL;
        int             online = -1;
        int             client_count = 0;
        char            *clientname = NULL;
        uint64_t        bytesread = 0;
        uint64_t        byteswrite = 0;
        char            key[1024] = {0,};
        int             i = 0;
        int             j = 0;

        GF_ASSERT (dict);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                goto out;
        cli_out ("Client connections for volume %s", volname);

        ret = dict_get_int32 (dict, "brick-index-max", &brick_index_max);
        if (ret)
                goto out;
        ret = dict_get_int32 (dict, "other-count", &other_count);
        if (ret)
                goto out;

        index_max = brick_index_max + other_count;

        for (i = 0; i <= index_max; i++) {
                cli_out ("----------------------------------------------");

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

                if (notbrick)
                        cli_out ("%s : %s", hostname, path);
                else
                        cli_out ("Brick : %s:%s", hostname, path);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.status", i);
                ret = dict_get_int32 (dict, key, &online);
                if (ret)
                        goto out;
                if (!online) {
                        if (notbrick)
                                cli_out ("%s is offline", hostname);
                        else
                                cli_out ("Brick is offline");
                        continue;
                }

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.clientcount", i);
                ret = dict_get_int32 (dict, key, &client_count);
                if (ret)
                        goto out;

                cli_out ("Clients connected : %d", client_count);
                if (client_count == 0)
                        continue;

                cli_out ("%-48s %15s %15s", "Hostname", "BytesRead",
                         "BytesWritten");
                cli_out ("%-48s %15s %15s", "--------", "---------",
                         "------------");
                for (j =0; j < client_count; j++) {
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key),
                                  "brick%d.client%d.hostname", i, j);
                        ret = dict_get_str (dict, key, &clientname);
                        if (ret)
                                goto out;

                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key),
                                  "brick%d.client%d.bytesread", i, j);
                        ret = dict_get_uint64 (dict, key, &bytesread);
                        if (ret)
                                goto out;

                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key),
                                 "brick%d.client%d.byteswrite", i, j);
                        ret = dict_get_uint64 (dict, key, &byteswrite);
                        if (ret)
                                goto out;

                        cli_out ("%-48s %15"PRIu64" %15"PRIu64,
                                 clientname, bytesread, byteswrite);
                }
        }
out:
        cli_out ("----------------------------------------------\n");
        return;
}

void
cli_print_volume_status_inode_entry (dict_t *dict, char *prefix)
{
        int             ret = -1;
        char            key[1024] = {0,};
        char            *gfid = NULL;
        uint64_t        nlookup = 0;
        uint32_t        ref = 0;
        int             ia_type = 0;
        char            inode_type;

        GF_ASSERT (dict);
        GF_ASSERT (prefix);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.gfid", prefix);
        ret = dict_get_str (dict, key, &gfid);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.nlookup", prefix);
        ret = dict_get_uint64 (dict, key, &nlookup);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.ref", prefix);
        ret = dict_get_uint32 (dict, key, &ref);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.ia_type", prefix);
        ret = dict_get_int32 (dict, key, &ia_type);
        if (ret)
                goto out;

        switch (ia_type) {
        case IA_IFREG:
                inode_type = 'R';
                break;
        case IA_IFDIR:
                inode_type = 'D';
                break;
        case IA_IFLNK:
                inode_type = 'L';
                break;
        case IA_IFBLK:
                inode_type = 'B';
                break;
        case IA_IFCHR:
                inode_type = 'C';
                break;
        case IA_IFIFO:
                inode_type = 'F';
                break;
        case IA_IFSOCK:
                inode_type = 'S';
                break;
        default:
                inode_type = 'I';
                break;
        }

        cli_out ("%-40s %14"PRIu64" %14"PRIu32" %9c",
                 gfid, nlookup, ref, inode_type);

out:
        return;

}

void
cli_print_volume_status_itables (dict_t *dict, char *prefix)
{
        int             ret = -1;
        char            key[1024] = {0,};
        uint32_t        active_size = 0;
        uint32_t        lru_size = 0;
        uint32_t        purge_size = 0;
        int             i =0;

        GF_ASSERT (dict);
        GF_ASSERT (prefix);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.active_size", prefix);
        ret = dict_get_uint32 (dict, key, &active_size);
        if (ret)
                goto out;
        if (active_size != 0) {
                cli_out ("Active inodes:");
                cli_out ("%-40s %14s %14s %9s", "GFID", "Lookups", "Ref",
                         "IA type");
                cli_out ("%-40s %14s %14s %9s", "----", "-------", "---",
                         "-------");
        }
        for (i = 0; i < active_size; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.active%d", prefix, i);
                cli_print_volume_status_inode_entry (dict, key);
        }
        cli_out (" ");

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.lru_size", prefix);
        ret = dict_get_uint32 (dict, key, &lru_size);
        if (ret)
                goto out;
        if (lru_size != 0) {
                cli_out ("LRU inodes:");
                cli_out ("%-40s %14s %14s %9s", "GFID", "Lookups", "Ref",
                         "IA type");
                cli_out ("%-40s %14s %14s %9s", "----", "-------", "---",
                         "-------");
        }
        for (i = 0; i < lru_size; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.lru%d", prefix, i);
                cli_print_volume_status_inode_entry (dict, key);
        }
        cli_out (" ");

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.purge_size", prefix);
        ret = dict_get_uint32 (dict, key, &purge_size);
        if (ret)
                goto out;
        if (purge_size != 0) {
                cli_out ("Purged inodes:");
                cli_out ("%-40s %14s %14s %9s", "GFID", "Lookups", "Ref",
                         "IA type");
                cli_out ("%-40s %14s %14s %9s", "----", "-------", "---",
                         "-------");
        }
        for (i = 0; i < purge_size; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.purge%d", prefix, i);
                cli_print_volume_status_inode_entry (dict, key);
        }

out:
        return;
}

void
cli_print_volume_status_inode (dict_t *dict, gf_boolean_t notbrick)
{
        int             ret = -1;
        char            *volname = NULL;
        int             brick_index_max = -1;
        int             other_count = 0;
        int             index_max = 0;
        char            *hostname = NULL;
        char            *path = NULL;
        int             online = -1;
        int             conn_count = 0;
        char            key[1024] = {0,};
        int             i = 0;
        int             j = 0;

        GF_ASSERT (dict);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                goto out;
        cli_out ("Inode tables for volume %s", volname);

        ret = dict_get_int32 (dict, "brick-index-max", &brick_index_max);
        if (ret)
                goto out;
        ret = dict_get_int32 (dict, "other-count", &other_count);
        if (ret)
                goto out;

        index_max = brick_index_max + other_count;

        for ( i = 0; i <= index_max; i++) {
                cli_out ("----------------------------------------------");

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
                if (notbrick)
                        cli_out ("%s : %s", hostname, path);
                else
                        cli_out ("Brick : %s:%s", hostname, path);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.status", i);
                ret = dict_get_int32 (dict, key, &online);
                if (ret)
                        goto out;
                if (!online) {
                        if (notbrick)
                                cli_out ("%s is offline", hostname);
                        else
                                cli_out ("Brick is offline");
                        continue;
                }

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.conncount", i);
                ret = dict_get_int32 (dict, key, &conn_count);
                if (ret)
                        goto out;

                for (j = 0; j < conn_count; j++) {
                        if (conn_count > 1)
                                cli_out ("Connection %d:", j+1);

                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "brick%d.conn%d.itable",
                                  i, j);
                        cli_print_volume_status_itables (dict, key);
                        cli_out (" ");
                }
        }
out:
        cli_out ("----------------------------------------------");
        return;
}

void
cli_print_volume_status_fdtable (dict_t *dict, char *prefix)
{
        int             ret = -1;
        char            key[1024] = {0,};
        int             refcount = 0;
        uint32_t        maxfds = 0;
        int             firstfree = 0;
        int             openfds = 0;
        int             fd_pid = 0;
        int             fd_refcount = 0;
        int             fd_flags = 0;
        int             i = 0;

        GF_ASSERT (dict);
        GF_ASSERT (prefix);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.refcount", prefix);
        ret = dict_get_int32 (dict, key, &refcount);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.maxfds", prefix);
        ret = dict_get_uint32 (dict, key, &maxfds);
        if (ret)
                goto out;

        memset (key, 0 ,sizeof (key));
        snprintf (key, sizeof (key), "%s.firstfree", prefix);
        ret = dict_get_int32 (dict, key, &firstfree);
        if (ret)
                goto out;

        cli_out ("RefCount = %d  MaxFDs = %d  FirstFree = %d",
                 refcount, maxfds, firstfree);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.openfds", prefix);
        ret = dict_get_int32 (dict, key, &openfds);
        if (ret)
                goto out;
        if (0 == openfds) {
                cli_err ("No open fds");
                goto out;
        }

        cli_out ("%-19s %-19s %-19s %-19s", "FD Entry", "PID",
                 "RefCount", "Flags");
        cli_out ("%-19s %-19s %-19s %-19s", "--------", "---",
                 "--------", "-----");

        for (i = 0; i < maxfds ; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.fdentry%d.pid", prefix, i);
                ret = dict_get_int32 (dict, key, &fd_pid);
                if (ret)
                        continue;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.fdentry%d.refcount",
                          prefix, i);
                ret = dict_get_int32 (dict, key, &fd_refcount);
                if (ret)
                        continue;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.fdentry%d.flags", prefix, i);
                ret = dict_get_int32 (dict, key, &fd_flags);
                if (ret)
                        continue;

                cli_out ("%-19d %-19d %-19d %-19d", i, fd_pid, fd_refcount,
                         fd_flags);
        }

out:
        return;
}

void
cli_print_volume_status_fd (dict_t *dict, gf_boolean_t notbrick)
{
        int             ret = -1;
        char            *volname = NULL;
        int             brick_index_max = -1;
        int             other_count = 0;
        int             index_max = 0;
        char            *hostname = NULL;
        char            *path = NULL;
        int             online = -1;
        int             conn_count = 0;
        char            key[1024] = {0,};
        int             i = 0;
        int             j = 0;

        GF_ASSERT (dict);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                goto out;
        cli_out ("FD tables for volume %s", volname);

        ret = dict_get_int32 (dict, "brick-index-max", &brick_index_max);
        if (ret)
                goto out;
        ret = dict_get_int32 (dict, "other-count", &other_count);
        if (ret)
                goto out;

        index_max = brick_index_max + other_count;

        for (i = 0; i <= index_max; i++) {
                cli_out ("----------------------------------------------");

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

                if (notbrick)
                        cli_out ("%s : %s", hostname, path);
                else
                        cli_out ("Brick : %s:%s", hostname, path);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.status", i);
                ret = dict_get_int32 (dict, key, &online);
                if (ret)
                        goto out;
                if (!online) {
                        if (notbrick)
                                cli_out ("%s is offline", hostname);
                        else
                                cli_out ("Brick is offline");
                        continue;
                }

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.conncount", i);
                ret = dict_get_int32 (dict, key, &conn_count);
                if (ret)
                        goto out;

                for (j = 0; j < conn_count; j++) {
                        cli_out ("Connection %d:", j+1);

                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "brick%d.conn%d.fdtable",
                                  i, j);
                        cli_print_volume_status_fdtable (dict, key);
                        cli_out (" ");
                }
        }
out:
        cli_out ("----------------------------------------------");
        return;
}

void
cli_print_volume_status_call_frame (dict_t *dict, char *prefix)
{
        int             ret = -1;
        char            key[1024] = {0,};
        int             ref_count = 0;
        char            *translator = 0;
        int             complete = 0;
        char            *parent = NULL;
        char            *wind_from = NULL;
        char            *wind_to = NULL;
        char            *unwind_from = NULL;
        char            *unwind_to = NULL;

        if (!dict || !prefix)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.refcount", prefix);
        ret = dict_get_int32 (dict, key, &ref_count);
        if (ret)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.translator", prefix);
        ret = dict_get_str (dict, key, &translator);
        if (ret)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.complete", prefix);
        ret = dict_get_int32 (dict, key, &complete);
        if (ret)
                return;

        cli_out ("  Ref Count   = %d", ref_count);
        cli_out ("  Translator  = %s", translator);
        cli_out ("  Completed   = %s", (complete ? "Yes" : "No"));

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.parent", prefix);
        ret = dict_get_str (dict, key, &parent);
        if (!ret)
                cli_out ("  Parent      = %s", parent);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.windfrom", prefix);
        ret = dict_get_str (dict, key, &wind_from);
        if (!ret)
                cli_out ("  Wind From   = %s", wind_from);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.windto", prefix);
        ret = dict_get_str (dict, key, &wind_to);
        if (!ret)
                cli_out ("  Wind To     = %s", wind_to);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.unwindfrom", prefix);
        ret = dict_get_str (dict, key, &unwind_from);
        if (!ret)
                cli_out ("  Unwind From = %s", unwind_from);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.unwindto", prefix);
        ret = dict_get_str (dict, key, &unwind_to);
        if (!ret)
                cli_out ("  Unwind To   = %s", unwind_to);
}

void
cli_print_volume_status_call_stack (dict_t *dict, char *prefix)
{
        int             ret = -1;
        char            key[1024] = {0,};
        int             uid = 0;
        int             gid = 0;
        int             pid = 0;
        uint64_t        unique = 0;
        //char            *op = NULL;
        int             count = 0;
        int             i = 0;

        if (!dict || !prefix)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.uid", prefix);
        ret = dict_get_int32 (dict, key, &uid);
        if (ret)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.gid", prefix);
        ret = dict_get_int32 (dict, key, &gid);
        if (ret)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.pid", prefix);
        ret = dict_get_int32 (dict, key, &pid);
        if (ret)
                return;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.unique", prefix);
        ret = dict_get_uint64 (dict, key, &unique);
        if (ret)
                return;

        /*
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.op", prefix);
        ret = dict_get_str (dict, key, &op);
        if (ret)
                return;
        */


        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.count", prefix);
        ret = dict_get_int32 (dict, key, &count);
        if (ret)
                return;

        cli_out (" UID    : %d", uid);
        cli_out (" GID    : %d", gid);
        cli_out (" PID    : %d", pid);
        cli_out (" Unique : %"PRIu64, unique);
        //cli_out ("\tOp     : %s", op);
        cli_out (" Frames : %d", count);

        for (i = 0; i < count; i++) {
                cli_out (" Frame %d", i+1);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.frame%d", prefix, i);
                cli_print_volume_status_call_frame (dict, key);
        }

        cli_out (" ");
}

void
cli_print_volume_status_callpool (dict_t *dict, gf_boolean_t notbrick)
{
        int             ret = -1;
        char            *volname = NULL;
        int             brick_index_max = -1;
        int             other_count = 0;
        int             index_max = 0;
        char            *hostname = NULL;
        char            *path = NULL;
        int             online = -1;
        int             call_count = 0;
        char            key[1024] = {0,};
        int             i = 0;
        int             j = 0;

        GF_ASSERT (dict);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                goto out;
        cli_out ("Pending calls for volume %s", volname);

        ret = dict_get_int32 (dict, "brick-index-max", &brick_index_max);
        if (ret)
                goto out;
        ret = dict_get_int32 (dict, "other-count", &other_count);
        if (ret)
                goto out;

        index_max = brick_index_max + other_count;

        for (i = 0; i <= index_max; i++) {
                cli_out ("----------------------------------------------");

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

                if (notbrick)
                        cli_out ("%s : %s", hostname, path);
                else
                        cli_out ("Brick : %s:%s", hostname, path);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.status", i);
                ret = dict_get_int32 (dict, key, &online);
                if (ret)
                        goto out;
                if (!online) {
                        if (notbrick)
                                cli_out ("%s is offline", hostname);
                        else
                                cli_out ("Brick is offline");
                        continue;
                }

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.callpool.count", i);
                ret = dict_get_int32 (dict, key, &call_count);
                if (ret)
                        goto out;
                cli_out ("Pending calls: %d", call_count);

                if (0 == call_count)
                        continue;

                for (j = 0; j < call_count; j++) {
                        cli_out ("Call Stack%d", j+1);

                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key),
                                  "brick%d.callpool.stack%d", i, j);
                        cli_print_volume_status_call_stack (dict, key);
                }
        }

out:
        cli_out ("----------------------------------------------");
        return;
}

static void
cli_print_volume_status_tasks (dict_t *dict)
{
        int             ret         = -1;
        int             i           = 0;
        int             j           = 0;
        int             count       = 0;
        int             task_count  = 0;
        int             status      = 0;
        char           *op          = NULL;
        char           *task_id_str = NULL;
        char           *volname     = NULL;
        char            key[1024]   = {0,};
        char            task[1024]  = {0,};
        char           *brick       = NULL;
        char           *src_brick   = NULL;
        char           *dest_brick  = NULL;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "tasks", &task_count);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get tasks count");
                return;
        }

        cli_out ("Task Status of Volume %s", volname);
        cli_print_line (CLI_BRICK_STATUS_LINE_LEN);

        if (task_count == 0) {
                cli_out ("There are no active volume tasks");
                cli_out (" ");
                return;
        }

        for (i = 0; i < task_count; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "task%d.type", i);
                ret = dict_get_str(dict, key, &op);
                if (ret)
                        return;
                cli_out ("%-20s : %-20s", "Task", op);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "task%d.id", i);
                ret = dict_get_str (dict, key, &task_id_str);
                if (ret)
                        return;
                cli_out ("%-20s : %-20s", "ID", task_id_str);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "task%d.status", i);
                ret = dict_get_int32 (dict, key, &status);
                if (ret)
                        return;

                snprintf (task, sizeof (task), "task%d", i);

                /*
                   Replace brick only has two states - In progress and Complete
                   Ref: xlators/mgmt/glusterd/src/glusterd-replace-brick.c
                */

                if (!strcmp (op, "Replace brick")) {
                        if (status)
                                status = GF_DEFRAG_STATUS_COMPLETE;
                        else
                                status = GF_DEFRAG_STATUS_STARTED;

                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "%s.src-brick", task);
                        ret = dict_get_str (dict, key, &src_brick);
                        if (ret)
                                goto out;

                        cli_out ("%-20s : %-20s", "Source Brick", src_brick);

                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "%s.dst-brick", task);
                        ret = dict_get_str (dict, key, &dest_brick);
                        if (ret)
                                goto out;

                        cli_out ("%-20s : %-20s", "Destination Brick",
                                 dest_brick);

                } else if (!strcmp (op, "Remove brick")) {
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "%s.count", task);
                        ret = dict_get_int32 (dict, key, &count);
                        if (ret)
                                goto out;

                        cli_out ("%-20s", "Removed bricks:");

                        for (j = 1; j <= count; j++) {
                                memset (key, 0, sizeof (key));
                                snprintf (key, sizeof (key),"%s.brick%d",
                                          task, j);
                                ret = dict_get_str (dict, key, &brick);
                                if (ret)
                                        goto out;

                                cli_out ("%-20s", brick);
                        }
                }
                cli_out ("%-20s : %-20s", "Status",
                         cli_vol_task_status_str[status]);
                cli_out (" ");
        }

out:
        return;
}

static int
gf_cli_status_cbk (struct rpc_req *req, struct iovec *iov,
                      int count, void *myframe)
{
        int                             ret             = -1;
        int                             brick_index_max = -1;
        int                             other_count     = 0;
        int                             index_max       = 0;
        int                             i               = 0;
        int                             pid             = -1;
        uint32_t                        cmd             = 0;
        gf_boolean_t                    notbrick        = _gf_false;
        char                            key[1024]       = {0,};
        char                           *hostname        = NULL;
        char                           *path            = NULL;
        char                           *volname         = NULL;
        dict_t                         *dict            = NULL;
        gf_cli_rsp                      rsp             = {0,};
        cli_volume_status_t             status          = {0};
        cli_local_t                    *local           = NULL;
        gf_boolean_t                    wipe_local      = _gf_false;
        char                            msg[1024]       = {0,};

        if (req->rpc_status == -1)
                goto out;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        gf_log ("cli", GF_LOG_DEBUG, "Received response to status cmd");

        local = ((call_frame_t *)myframe)->local;
        if (!local) {
                local = cli_local_get ();
                if (!local) {
                        ret = -1;
                        gf_log ("cli", GF_LOG_ERROR, "Failed to get local");
                        goto out;
                }
                wipe_local = _gf_true;
        }

        if (rsp.op_ret) {
                if (strcmp (rsp.op_errstr, ""))
                        snprintf (msg, sizeof (msg), "%s", rsp.op_errstr);
                else
                        snprintf (msg, sizeof (msg), "Unable to obtain volume "
                                  "status information.");

                if (global_state->mode & GLUSTER_MODE_XML) {
                        if (!local->all)
                                cli_xml_output_str ("volStatus", msg,
                                                    rsp.op_ret, rsp.op_errno,
                                                    rsp.op_errstr);
                        ret = 0;
                        goto out;
                }

                cli_err ("%s", msg);
                if (local && local->all) {
                        ret = 0;
                        cli_out (" ");
                } else
                        ret = -1;

                goto out;
        }

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
                if (local && local->dict) {
                        dict_ref (dict);
                        ret = dict_set_static_ptr (local->dict, "rsp-dict", dict);
                        ret = 0;
                } else {
                        gf_log ("cli", GF_LOG_ERROR, "local not found");
                        ret = -1;
                }
                goto out;
        }

        if ((cmd & GF_CLI_STATUS_NFS) || (cmd & GF_CLI_STATUS_SHD) ||
            (cmd & GF_CLI_STATUS_QUOTAD))
                notbrick = _gf_true;

        if (global_state->mode & GLUSTER_MODE_XML) {
                if (!local->all) {
                        ret = cli_xml_output_vol_status_begin (local,
                                                               rsp.op_ret,
                                                               rsp.op_errno,
                                                               rsp.op_errstr);
                        if (ret) {
                                gf_log ("cli", GF_LOG_ERROR,
                                        "Error outputting to xml");
                                goto out;
                        }
                }
                if (cmd & GF_CLI_STATUS_TASKS) {
                        ret = cli_xml_output_vol_status_tasks_detail (local,
                                                                      dict);
                        if (ret) {
                                gf_log ("cli", GF_LOG_ERROR,"Error outputting "
                                        "to xml");
                                goto out;
                        }
                } else {
                        ret = cli_xml_output_vol_status (local, dict);
                        if (ret) {
                                gf_log ("cli", GF_LOG_ERROR,
                                        "Error outputting to xml");
                                goto out;
                        }
                }

                if (!local->all) {
                        ret = cli_xml_output_vol_status_end (local);
                        if (ret) {
                                gf_log ("cli", GF_LOG_ERROR,
                                        "Error outputting to xml");
                        }
                }
                goto out;
        }

        status.brick = GF_CALLOC (1, PATH_MAX + 256, gf_common_mt_strdup);

        switch (cmd & GF_CLI_STATUS_MASK) {
                case GF_CLI_STATUS_MEM:
                        cli_print_volume_status_mem (dict, notbrick);
                        goto cont;
                        break;
                case GF_CLI_STATUS_CLIENTS:
                        cli_print_volume_status_clients (dict, notbrick);
                        goto cont;
                        break;
                case GF_CLI_STATUS_INODE:
                        cli_print_volume_status_inode (dict, notbrick);
                        goto cont;
                        break;
                case GF_CLI_STATUS_FD:
                        cli_print_volume_status_fd (dict, notbrick);
                        goto cont;
                        break;
                case GF_CLI_STATUS_CALLPOOL:
                        cli_print_volume_status_callpool (dict, notbrick);
                        goto cont;
                        break;
                case GF_CLI_STATUS_TASKS:
                        cli_print_volume_status_tasks (dict);
                        goto cont;
                        break;
                default:
                        break;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "brick-index-max", &brick_index_max);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "other-count", &other_count);
        if (ret)
                goto out;

        index_max = brick_index_max + other_count;


        cli_out ("Status of volume: %s", volname);

        if ((cmd & GF_CLI_STATUS_DETAIL) == 0) {
                cli_out ("Gluster process\t\t\t\t\t\tPort\tOnline\tPid");
                cli_print_line (CLI_BRICK_STATUS_LINE_LEN);
        }

        for (i = 0; i <= index_max; i++) {


                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.hostname", i);
                ret = dict_get_str (dict, key, &hostname);
                if (ret)
                        continue;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.path", i);
                ret = dict_get_str (dict, key, &path);
                if (ret)
                        continue;

                /* Brick/not-brick is handled seperately here as all
                 * types of nodes are contained in the default output
                 */
                memset (status.brick, 0, PATH_MAX + 255);
                if (!strcmp (hostname, "NFS Server") ||
                    !strcmp (hostname, "Self-heal Daemon") ||
                    !strcmp (hostname, "Quota Daemon"))
                        snprintf (status.brick, PATH_MAX + 255, "%s on %s",
                                  hostname, path);
                else
                        snprintf (status.brick, PATH_MAX + 255, "Brick %s:%s",
                                  hostname, path);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.port", i);
                ret = dict_get_int32 (dict, key, &(status.port));
                if (ret)
                        continue;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.status", i);
                ret = dict_get_int32 (dict, key, &(status.online));
                if (ret)
                        continue;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.pid", i);
                ret = dict_get_int32 (dict, key, &pid);
                if (ret)
                        continue;
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
                        cli_print_line (CLI_BRICK_STATUS_LINE_LEN);
                        cli_print_detailed_status (&status);

                } else {
                        cli_print_brick_status (&status);
                }
        }
        cli_out (" ");

        if ((cmd & GF_CLI_STATUS_MASK) == GF_CLI_STATUS_NONE)
                cli_print_volume_status_tasks (dict);
cont:
        ret = rsp.op_ret;

out:
        if (dict)
                dict_unref (dict);
        GF_FREE (status.brick);
        if (local && wipe_local) {
                cli_local_wipe (local);
        }

        cli_cmd_broadcast_response (ret);
        return ret;
}

int32_t
gf_cli_status_volume (call_frame_t *frame, xlator_t *this,
                         void *data)
{
        gf_cli_req                      req  = {{0,}};
        int                             ret  = -1;
        dict_t                         *dict = NULL;

        if (!frame || !this || !data)
                goto out;

        dict = data;

        ret = cli_to_glusterd (&req, frame, gf_cli_status_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_STATUS_VOLUME, this, cli_rpc_prog,
                               NULL);
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
        void            *vol_dict     = NULL;
        dict_t          *dict         = NULL;
        cli_local_t     *local        = NULL;

        if (frame->local) {
                local = frame->local;
                local->all = _gf_true;
        } else
                goto out;

        ret = dict_get_uint32 (local->dict, "cmd", &cmd);
        if (ret)
                goto out;


        ret = gf_cli_status_volume (frame, this, data);
        if (ret)
                goto out;

        ret = dict_get_ptr (local->dict, "rsp-dict", &vol_dict);
        if (ret)
                goto out;

        ret = dict_get_int32 ((dict_t *)vol_dict, "vol_count", &vol_count);
        if (ret) {
                cli_err ("Failed to get names of volumes");
                goto out;
        }

        /* remove the "all" flag in cmd */
        cmd &= ~GF_CLI_STATUS_ALL;
        cmd |= GF_CLI_STATUS_VOL;

        if (global_state->mode & GLUSTER_MODE_XML) {
                //TODO: Pass proper op_* values
                ret = cli_xml_output_vol_status_begin (local, 0,0, NULL);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                        goto out;
                }
        }

        if (vol_count == 0 && !(global_state->mode & GLUSTER_MODE_XML)) {
                cli_err ("No volumes present");
                ret = 0;
                goto out;
        }

        for (i = 0; i < vol_count; i++) {

                dict = dict_new ();
                if (!dict)
                        goto out;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "vol%d", i);
                ret = dict_get_str (vol_dict, key, &volname);
                if (ret)
                        goto out;

                ret = dict_set_str (dict, "volname", volname);
                if (ret)
                        goto out;

                ret = dict_set_uint32 (dict, "cmd", cmd);
                if (ret)
                        goto out;

                ret = gf_cli_status_volume (frame, this, dict);
                if (ret)
                        goto out;

                dict_unref (dict);
        }

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_vol_status_end (local);
        }

 out:
        if (ret)
                gf_log ("cli", GF_LOG_ERROR, "status all failed");

        if (vol_dict)
                dict_unref (vol_dict);

        if (ret && dict)
                dict_unref (dict);

        if (local)
                cli_local_wipe (local);

        if (frame)
                frame->local = NULL;

        return ret;
}

static int
gf_cli_mount_cbk (struct rpc_req *req, struct iovec *iov,
                  int count, void *myframe)
{
        gf1_cli_mount_rsp rsp   = {0,};
        int               ret   = -1;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf1_cli_mount_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to mount");

        if (rsp.op_ret == 0) {
                ret = 0;
                cli_out ("%s", rsp.path);
        } else {
                /* weird sounding but easy to parse... */
                cli_err ("%d : failed with this errno (%s)",
                         rsp.op_errno, strerror (rsp.op_errno));
                ret = -1;
        }

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int32_t
gf_cli_mount (call_frame_t *frame, xlator_t *this, void *data)
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
                                           &req.dict.dict_len);
        if (ret) {
                ret = -1;
                goto out;
        }

        ret = cli_cmd_submit (NULL, &req, frame, cli_rpc_prog,
                              GLUSTER_CLI_MOUNT, NULL,
                              this, gf_cli_mount_cbk,
                              (xdrproc_t)xdr_gf1_cli_mount_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
gf_cli_umount_cbk (struct rpc_req *req, struct iovec *iov,
                   int count, void *myframe)
{
        gf1_cli_umount_rsp rsp   = {0,};
        int               ret   = -1;

        if (-1 == req->rpc_status) {
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf1_cli_umount_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to mount");

        if (rsp.op_ret == 0)
                ret = 0;
        else {
                cli_err ("umount failed");
                ret = -1;
        }

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int32_t
gf_cli_umount (call_frame_t *frame, xlator_t *this, void *data)
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

        ret = cli_cmd_submit (NULL, &req, frame, cli_rpc_prog,
                              GLUSTER_CLI_UMOUNT, NULL,
                              this, gf_cli_umount_cbk,
                              (xdrproc_t)xdr_gf1_cli_umount_req);

 out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

void
cmd_heal_volume_statistics_out (dict_t *dict, int  brick)
{

        uint64_t        num_entries = 0;
        int             ret = 0;
        char            key[256] = {0};
        char            *hostname = NULL;
        uint64_t        i = 0;
        uint64_t        healed_count = 0;
        uint64_t        split_brain_count = 0;
        uint64_t        heal_failed_count = 0;
        char            *start_time_str = NULL;
        char            *end_time_str = NULL;
        char            *crawl_type = NULL;
        int             progress = -1;

        snprintf (key, sizeof key, "%d-hostname", brick);
        ret = dict_get_str (dict, key, &hostname);
        if (ret)
                goto out;
        cli_out ("------------------------------------------------");
        cli_out ("\nCrawl statistics for brick no %d", brick);
        cli_out ("Hostname of brick %s", hostname);

        snprintf (key, sizeof key, "statistics-%d-count", brick);
        ret = dict_get_uint64 (dict, key, &num_entries);
        if (ret)
                goto out;

        for (i = 0; i < num_entries; i++)
        {
                snprintf (key, sizeof key, "statistics_crawl_type-%d-%"PRIu64,
                          brick, i);
                ret = dict_get_str (dict, key, &crawl_type);
                if (ret)
                        goto out;

                snprintf (key, sizeof key, "statistics_healed_cnt-%d-%"PRIu64,
                          brick,i);
                ret = dict_get_uint64 (dict, key, &healed_count);
                if (ret)
                        goto out;

                snprintf (key, sizeof key, "statistics_sb_cnt-%d-%"PRIu64,
                          brick, i);
                ret = dict_get_uint64 (dict, key, &split_brain_count);
                if (ret)
                        goto out;
                snprintf (key, sizeof key, "statistics_heal_failed_cnt-%d-%"PRIu64,
                          brick, i);
                ret = dict_get_uint64 (dict, key, &heal_failed_count);
                if (ret)
                        goto out;
                snprintf (key, sizeof key, "statistics_strt_time-%d-%"PRIu64,
                          brick, i);
                ret = dict_get_str (dict, key,  &start_time_str);
                if (ret)
                        goto out;
                snprintf (key, sizeof key, "statistics_end_time-%d-%"PRIu64,
                          brick, i);
                ret = dict_get_str (dict, key, &end_time_str);
                if (ret)
                        goto out;
                snprintf (key, sizeof key, "statistics_inprogress-%d-%"PRIu64,
                          brick, i);
                ret = dict_get_int32 (dict, key, &progress);
                if (ret)
                        goto out;

                cli_out ("\nStarting time of crawl: %s", start_time_str);
                if (progress == 1)
                        cli_out ("Crawl is in progress");
                else
                        cli_out ("Ending time of crawl: %s", end_time_str);

                cli_out ("Type of crawl: %s", crawl_type);
                cli_out ("No. of entries healed: %"PRIu64,
                         healed_count);
                cli_out ("No. of entries in split-brain: %"PRIu64,
                        split_brain_count);
                cli_out ("No. of heal failed entries: %"PRIu64,
                         heal_failed_count);

        }


out:
        return;
}

void
cmd_heal_volume_brick_out (dict_t *dict, int brick)
{
        uint64_t        num_entries = 0;
        int             ret = 0;
        char            key[256] = {0};
        char           *hostname = NULL;
        char           *path = NULL;
        char           *status = NULL;
        uint64_t        i = 0;
        uint32_t        time = 0;
        char            timestr[32] = {0};
        char            *shd_status = NULL;

        snprintf (key, sizeof key, "%d-hostname", brick);
        ret = dict_get_str (dict, key, &hostname);
        if (ret)
                goto out;
        snprintf (key, sizeof key, "%d-path", brick);
        ret = dict_get_str (dict, key, &path);
        if (ret)
                goto out;
        cli_out ("\nBrick %s:%s", hostname, path);

        snprintf (key, sizeof key, "%d-status", brick);
        ret = dict_get_str (dict, key, &status);
        if (status && strlen (status))
                cli_out ("Status: %s", status);

        snprintf (key, sizeof key, "%d-shd-status",brick);
        ret = dict_get_str (dict, key, &shd_status);

        if(!shd_status)
        {
                snprintf (key, sizeof key, "%d-count", brick);
                ret = dict_get_uint64 (dict, key, &num_entries);
                cli_out ("Number of entries: %"PRIu64, num_entries);


                for (i = 0; i < num_entries; i++) {
                        snprintf (key, sizeof key, "%d-%"PRIu64, brick, i);
                        ret = dict_get_str (dict, key, &path);
                        if (ret)
                                continue;
                        time = 0;
                        snprintf (key, sizeof key, "%d-%"PRIu64"-time",
                                  brick, i);
                        ret = dict_get_uint32 (dict, key, &time);
                        if (!time) {
                                cli_out ("%s", path);
                        } else {
                                gf_time_fmt (timestr, sizeof timestr,
                                             time, gf_timefmt_FT);
                                if (i == 0) {
                                cli_out ("at                    path on brick");
                                cli_out ("-----------------------------------");
                                }
                                cli_out ("%s %s", timestr, path);
                        }
                }
        }

out:
        return;
}


void
cmd_heal_volume_statistics_heal_count_out (dict_t *dict, int brick)
{
        uint64_t        num_entries = 0;
        int             ret = 0;
        char            key[256] = {0};
        char           *hostname = NULL;
        char           *path = NULL;
        char           *status = NULL;
        char            *shd_status = NULL;

        snprintf (key, sizeof key, "%d-hostname", brick);
        ret = dict_get_str (dict, key, &hostname);
        if (ret)
                goto out;
        snprintf (key, sizeof key, "%d-path", brick);
        ret = dict_get_str (dict, key, &path);
        if (ret)
                goto out;
        cli_out ("\nBrick %s:%s", hostname, path);

        snprintf (key, sizeof key, "%d-status", brick);
        ret = dict_get_str (dict, key, &status);
        if (status && strlen (status))
                cli_out ("Status: %s", status);

        snprintf (key, sizeof key, "%d-shd-status",brick);
        ret = dict_get_str (dict, key, &shd_status);

        if(!shd_status)
        {
                snprintf (key, sizeof key, "%d-hardlinks", brick);
                ret = dict_get_uint64 (dict, key, &num_entries);
                if (ret)
                        cli_out ("No gathered input for this brick");
                else
                        cli_out ("Number of entries: %"PRIu64, num_entries);


        }

out:
        return;
}


int
gf_cli_heal_volume_cbk (struct rpc_req *req, struct iovec *iov,
                             int count, void *myframe)
{
        gf_cli_rsp              rsp   = {0,};
        int                     ret   = -1;
        cli_local_t             *local = NULL;
        char                    *volname = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *input_dict = NULL;
        dict_t                  *dict = NULL;
        int                     brick_count = 0;
        int                     i = 0;
        gf_xl_afr_op_t          heal_op = GF_AFR_OP_INVALID;
        char                    *operation = NULL;
        char                    *substr = NULL;
        char                    *heal_op_str = NULL;

        if (-1 == req->rpc_status) {
                goto out;
        }

        frame = myframe;

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        if (frame)
                local = frame->local;

        if (local) {
                input_dict = local->dict;
                ret = dict_get_int32 (input_dict, "heal-op",
                                      (int32_t*)&heal_op);
        }
//TODO: Proper XML output
//#if (HAVE_LIB_XML)
//        if (global_state->mode & GLUSTER_MODE_XML) {
//                ret = cli_xml_output_dict ("volHeal", dict, rsp.op_ret,
//                                           rsp.op_errno, rsp.op_errstr);
//                if (ret)
//                        gf_log ("cli", GF_LOG_ERROR,
//                                "Error outputting to xml");
//                goto out;
//        }
//#endif

        ret = dict_get_str (input_dict, "volname", &volname);
        if (ret) {
                gf_log (frame->this->name, GF_LOG_ERROR, "failed to get volname");
                goto out;
        }

        gf_log ("cli", GF_LOG_INFO, "Received resp to heal volume");

        switch (heal_op) {
                case    GF_AFR_OP_HEAL_INDEX:
                        heal_op_str = "to perform index self heal";
                        break;
                case    GF_AFR_OP_HEAL_FULL:
                        heal_op_str = "to perform full self heal";
                        break;
                case    GF_AFR_OP_INDEX_SUMMARY:
                        heal_op_str = "list of entries to be healed";
                        break;
                case    GF_AFR_OP_HEALED_FILES:
                        heal_op_str = "list of healed entries";
                        break;
                case    GF_AFR_OP_HEAL_FAILED_FILES:
                        heal_op_str = "list of heal failed entries";
                        break;
                case    GF_AFR_OP_SPLIT_BRAIN_FILES:
                        heal_op_str = "list of split brain entries";
                        break;
                case    GF_AFR_OP_STATISTICS:
                        heal_op_str =  "crawl statistics";
                        break;
                case    GF_AFR_OP_STATISTICS_HEAL_COUNT:
                        heal_op_str = "count of entries to be healed";
                        break;
                case    GF_AFR_OP_STATISTICS_HEAL_COUNT_PER_REPLICA:
                        heal_op_str = "count of entries to be healed per replica";
                        break;
                case    GF_AFR_OP_INVALID:
                        heal_op_str = "invalid heal op";
                        break;
        }

        if ((heal_op == GF_AFR_OP_HEAL_FULL) ||
            (heal_op == GF_AFR_OP_HEAL_INDEX)) {
                operation = "Launching heal operation";
                substr = "\nUse heal info commands to check status";
        } else {
                operation = "Gathering";
                substr = "";
        }

        if (rsp.op_ret) {
                if (strcmp (rsp.op_errstr, "")) {
                        cli_err ("%s", rsp.op_errstr);
                } else {
                        cli_err ("%s %s on volume %s has been unsuccessful",
                                 operation, heal_op_str, volname);
                }

                ret = rsp.op_ret;
                goto out;
        } else {
                cli_out ("%s %s on volume %s has been successful %s", operation,
                         heal_op_str, volname, substr);
        }

        ret = rsp.op_ret;
        if ((heal_op == GF_AFR_OP_HEAL_FULL) ||
            (heal_op == GF_AFR_OP_HEAL_INDEX))
                goto out;

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
        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret)
                goto out;

        if (!brick_count) {
                cli_err ("All bricks of volume %s are down.", volname);
                goto out;
        }

        switch (heal_op) {
                case GF_AFR_OP_STATISTICS:
                        for (i = 0; i < brick_count; i++)
                                cmd_heal_volume_statistics_out (dict, i);
                        break;
                case GF_AFR_OP_STATISTICS_HEAL_COUNT:
                case GF_AFR_OP_STATISTICS_HEAL_COUNT_PER_REPLICA:
                        for (i = 0; i < brick_count; i++)
                                cmd_heal_volume_statistics_heal_count_out (dict,
                                                                           i);
                        break;
                case GF_AFR_OP_INDEX_SUMMARY:
                case GF_AFR_OP_HEALED_FILES:
                case GF_AFR_OP_HEAL_FAILED_FILES:
                case GF_AFR_OP_SPLIT_BRAIN_FILES:
                        for (i = 0; i < brick_count; i++)
                                cmd_heal_volume_brick_out (dict, i);
                        break;
                default:
                        break;
        }

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        free (rsp.op_errstr);
        if (dict)
                dict_unref (dict);
        return ret;
}

int32_t
gf_cli_heal_volume (call_frame_t *frame, xlator_t *this,
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

        ret = cli_to_glusterd (&req, frame, gf_cli_heal_volume_cbk,
                               (xdrproc_t) xdr_gf_cli_req, dict,
                               GLUSTER_CLI_HEAL_VOLUME, this, cli_rpc_prog,
                               NULL);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        GF_FREE (req.dict.dict_val);

        return ret;
}

int32_t
gf_cli_statedump_volume_cbk (struct rpc_req *req, struct iovec *iov,
                                int count, void *myframe)
{
        gf_cli_rsp                      rsp = {0,};
        int                             ret = -1;
        char                            msg[1024] = {0,};

        if (-1 == req->rpc_status)
                goto out;
        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }
        gf_log ("cli", GF_LOG_DEBUG, "Received response to statedump");
        if (rsp.op_ret)
                snprintf (msg, sizeof(msg), "%s", rsp.op_errstr);
        else
                snprintf (msg, sizeof (msg), "Volume statedump successful");

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_str ("volStatedump", msg, rsp.op_ret,
                                          rsp.op_errno, rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret)
                cli_err ("volume statedump: failed: %s", msg);
        else
                cli_out ("volume statedump: success");
        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int32_t
gf_cli_statedump_volume (call_frame_t *frame, xlator_t *this,
                            void *data)
{
        gf_cli_req                      req = {{0,}};
        dict_t                          *options = NULL;
        int                             ret = -1;

        if (!frame || !this || !data)
                goto out;

        options = data;

        ret = cli_to_glusterd (&req, frame, gf_cli_statedump_volume_cbk,
                               (xdrproc_t) xdr_gf_cli_req, options,
                               GLUSTER_CLI_STATEDUMP_VOLUME, this, cli_rpc_prog,
                               NULL);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        GF_FREE (req.dict.dict_val);
        return ret;
}

int32_t
gf_cli_list_volume_cbk (struct rpc_req *req, struct iovec *iov,
                                int count, void *myframe)
{
        int             ret = -1;
        gf_cli_rsp      rsp = {0,};
        dict_t          *dict = NULL;
        int             vol_count = 0;;
        char            *volname = NULL;
        char            key[1024] = {0,};
        int             i = 0;

        if (-1 == req->rpc_status)
                goto out;
        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        ret = dict_unserialize (rsp.dict.dict_val, rsp.dict.dict_len, &dict);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to allocate memory");
                goto out;
        }

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_vol_list (dict, rsp.op_ret, rsp.op_errno,
                                               rsp.op_errstr);
                if (ret)
                        gf_log ("cli", GF_LOG_ERROR,
                                "Error outputting to xml");
                goto out;
        }

        if (rsp.op_ret)
                cli_err ("%s", rsp.op_errstr);
        else {
                ret = dict_get_int32 (dict, "count", &vol_count);
                if (ret)
                        goto out;

                if (vol_count == 0) {
                        cli_err ("No volumes present in cluster");
                        goto out;
                }
                for (i = 0; i < vol_count; i++) {
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "volume%d", i);
                        ret = dict_get_str (dict, key, &volname);
                        if (ret)
                                goto out;
                        cli_out ("%s", volname);
                }
        }

        ret = rsp.op_ret;

out:
        cli_cmd_broadcast_response (ret);
        return ret;
}

int32_t
gf_cli_list_volume (call_frame_t *frame, xlator_t *this, void *data)
{
        int             ret = -1;
        gf_cli_req      req = {{0,}};

        if (!frame || !this)
                goto out;

        ret = cli_cmd_submit (NULL, &req, frame, cli_rpc_prog,
                              GLUSTER_CLI_LIST_VOLUME, NULL,
                              this, gf_cli_list_volume_cbk,
                              (xdrproc_t)xdr_gf_cli_req);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
gf_cli_clearlocks_volume_cbk (struct rpc_req *req, struct iovec *iov,
                                  int count, void *myframe)
{
        gf_cli_rsp                      rsp = {0,};
        int                             ret = -1;
        char                            *lk_summary = NULL;
        char                            *volname = NULL;
        dict_t                          *dict = NULL;

        if (-1 == req->rpc_status)
                goto out;
        ret = xdr_to_generic (*iov, &rsp,
                              (xdrproc_t)xdr_gf_cli_rsp);
        if (ret < 0) {
                gf_log (((call_frame_t *) myframe)->this->name, GF_LOG_ERROR,
                        "Failed to decode xdr response");
                goto out;
        }
        gf_log ("cli", GF_LOG_DEBUG, "Received response to clear-locks");

        if (rsp.op_ret) {
                cli_err ("Volume clear-locks unsuccessful");
                cli_err ("%s", rsp.op_errstr);

        } else {
                if (!rsp.dict.dict_len) {
                        cli_err ("Possibly no locks cleared");
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
                        gf_log ("cli", GF_LOG_ERROR,
                                "Unable to serialize response dictionary");
                        goto out;
                }

                ret = dict_get_str (dict, "volname", &volname);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Unable to get volname "
                                "from dictionary");
                        goto out;
                }

                ret = dict_get_str (dict, "lk-summary", &lk_summary);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Unable to get lock "
                                "summary from dictionary");
                        goto out;
                }
                cli_out ("Volume clear-locks successful");
                cli_out ("%s", lk_summary);

        }

        ret = rsp.op_ret;

out:
        if (dict)
                dict_unref (dict);
        cli_cmd_broadcast_response (ret);
        return ret;
}

int32_t
gf_cli_clearlocks_volume (call_frame_t *frame, xlator_t *this,
                             void *data)
{
        gf_cli_req                      req = {{0,}};
        dict_t                          *options = NULL;
        int                             ret = -1;

        if (!frame || !this || !data)
                goto out;

        options = data;

        ret = cli_to_glusterd (&req, frame, gf_cli_clearlocks_volume_cbk,
                               (xdrproc_t) xdr_gf_cli_req, options,
                               GLUSTER_CLI_CLRLOCKS_VOLUME, this, cli_rpc_prog,
                               NULL);
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);

        GF_FREE (req.dict.dict_val);
        return ret;
}

int
cli_to_glusterd (gf_cli_req *req, call_frame_t *frame,
                 fop_cbk_fn_t cbkfn, xdrproc_t xdrproc, dict_t *dict,
                 int procnum, xlator_t *this, rpc_clnt_prog_t *prog,
                 struct iobref *iobref)
{
        int                ret = 0;
        size_t             len = 0;
        char               *cmd = NULL;
        int                i = 0;
        const char         **words = NULL;
        cli_local_t        *local = NULL;

        if (!this || !frame || !dict) {
                ret = -1;
                goto out;
        }

        if (!frame->local) {
                ret = -1;
                goto out;
        }

        local = frame->local;

        if (!local->words) {
                ret = -1;
                goto out;
        }

        words = local->words;

        while (words[i])
                len += strlen (words[i++]) + 1;

        cmd = GF_CALLOC (1, len, gf_common_mt_char);

        if (!cmd) {
                ret = -1;
                goto out;
        }

        for (i = 0; words[i]; i++) {
                strncat (cmd, words[i], strlen (words[i]));
                if (words[i+1] != NULL)
                        strncat (cmd, " ", strlen (" "));
        }

        cmd [len - 1] = '\0';

        ret = dict_set_dynstr (dict, "cmd-str", cmd);
        if (ret)
                goto out;

        ret = dict_allocate_and_serialize (dict, &(req->dict).dict_val,
                                           &(req->dict).dict_len);

        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get serialized length of dict");
                goto out;
        }

        ret = cli_cmd_submit (NULL, req, frame, prog, procnum, iobref, this,
                              cbkfn, (xdrproc_t) xdrproc);

out:
        return ret;

}

struct rpc_clnt_procedure gluster_cli_actors[GLUSTER_CLI_MAXVALUE] = {
        [GLUSTER_CLI_NULL]             = {"NULL", NULL },
        [GLUSTER_CLI_PROBE]            = {"PROBE_QUERY", gf_cli_probe},
        [GLUSTER_CLI_DEPROBE]          = {"DEPROBE_QUERY", gf_cli_deprobe},
        [GLUSTER_CLI_LIST_FRIENDS]     = {"LIST_FRIENDS", gf_cli_list_friends},
        [GLUSTER_CLI_UUID_RESET]       = {"UUID_RESET", gf_cli3_1_uuid_reset},
        [GLUSTER_CLI_UUID_GET]       = {"UUID_GET", gf_cli3_1_uuid_get},
        [GLUSTER_CLI_CREATE_VOLUME]    = {"CREATE_VOLUME", gf_cli_create_volume},
        [GLUSTER_CLI_DELETE_VOLUME]    = {"DELETE_VOLUME", gf_cli_delete_volume},
        [GLUSTER_CLI_START_VOLUME]     = {"START_VOLUME", gf_cli_start_volume},
        [GLUSTER_CLI_STOP_VOLUME]      = {"STOP_VOLUME", gf_cli_stop_volume},
        [GLUSTER_CLI_RENAME_VOLUME]    = {"RENAME_VOLUME", gf_cli_rename_volume},
        [GLUSTER_CLI_DEFRAG_VOLUME]    = {"DEFRAG_VOLUME", gf_cli_defrag_volume},
        [GLUSTER_CLI_GET_VOLUME]       = {"GET_VOLUME", gf_cli_get_volume},
        [GLUSTER_CLI_GET_NEXT_VOLUME]  = {"GET_NEXT_VOLUME", gf_cli_get_next_volume},
        [GLUSTER_CLI_SET_VOLUME]       = {"SET_VOLUME", gf_cli_set_volume},
        [GLUSTER_CLI_ADD_BRICK]        = {"ADD_BRICK", gf_cli_add_brick},
        [GLUSTER_CLI_REMOVE_BRICK]     = {"REMOVE_BRICK", gf_cli_remove_brick},
        [GLUSTER_CLI_REPLACE_BRICK]    = {"REPLACE_BRICK", gf_cli_replace_brick},
        [GLUSTER_CLI_LOG_ROTATE]       = {"LOG ROTATE", gf_cli_log_rotate},
        [GLUSTER_CLI_GETSPEC]          = {"GETSPEC", gf_cli_getspec},
        [GLUSTER_CLI_PMAP_PORTBYBRICK] = {"PMAP PORTBYBRICK", gf_cli_pmap_b2p},
        [GLUSTER_CLI_SYNC_VOLUME]      = {"SYNC_VOLUME", gf_cli_sync_volume},
        [GLUSTER_CLI_RESET_VOLUME]     = {"RESET_VOLUME", gf_cli_reset_volume},
        [GLUSTER_CLI_FSM_LOG]          = {"FSM_LOG", gf_cli_fsm_log},
        [GLUSTER_CLI_GSYNC_SET]        = {"GSYNC_SET", gf_cli_gsync_set},
        [GLUSTER_CLI_PROFILE_VOLUME]   = {"PROFILE_VOLUME", gf_cli_profile_volume},
        [GLUSTER_CLI_QUOTA]            = {"QUOTA", gf_cli_quota},
        [GLUSTER_CLI_TOP_VOLUME]       = {"TOP_VOLUME", gf_cli_top_volume},
        [GLUSTER_CLI_GETWD]            = {"GETWD", gf_cli_getwd},
        [GLUSTER_CLI_STATUS_VOLUME]    = {"STATUS_VOLUME", gf_cli_status_volume},
        [GLUSTER_CLI_STATUS_ALL]       = {"STATUS_ALL", gf_cli_status_volume_all},
        [GLUSTER_CLI_MOUNT]            = {"MOUNT", gf_cli_mount},
        [GLUSTER_CLI_UMOUNT]           = {"UMOUNT", gf_cli_umount},
        [GLUSTER_CLI_HEAL_VOLUME]      = {"HEAL_VOLUME", gf_cli_heal_volume},
        [GLUSTER_CLI_STATEDUMP_VOLUME] = {"STATEDUMP_VOLUME", gf_cli_statedump_volume},
        [GLUSTER_CLI_LIST_VOLUME]      = {"LIST_VOLUME", gf_cli_list_volume},
        [GLUSTER_CLI_CLRLOCKS_VOLUME]  = {"CLEARLOCKS_VOLUME", gf_cli_clearlocks_volume},
        [GLUSTER_CLI_COPY_FILE]        = {"COPY_FILE", gf_cli_copy_file},
        [GLUSTER_CLI_SYS_EXEC]         = {"SYS_EXEC", gf_cli_sys_exec},
};

struct rpc_clnt_program cli_prog = {
        .progname  = "Gluster CLI",
        .prognum   = GLUSTER_CLI_PROGRAM,
        .progver   = GLUSTER_CLI_VERSION,
        .numproc   = GLUSTER_CLI_MAXVALUE,
        .proctable = gluster_cli_actors,
};

struct rpc_clnt_procedure cli_quotad_procs[GF_AGGREGATOR_MAXVALUE] = {
        [GF_AGGREGATOR_NULL]     = {"NULL", NULL},
        [GF_AGGREGATOR_LOOKUP]   = {"LOOKUP", NULL},
        [GF_AGGREGATOR_GETLIMIT]   = {"GETLIMIT", cli_quotad_getlimit},
};

struct rpc_clnt_program cli_quotad_clnt = {
        .progname  = "CLI Quotad client",
        .prognum   = GLUSTER_AGGREGATOR_PROGRAM,
        .progver   = GLUSTER_AGGREGATOR_VERSION,
        .numproc   = GF_AGGREGATOR_MAXVALUE,
        .proctable = cli_quotad_procs,
};
