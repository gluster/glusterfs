/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

/* Widths of various columns in top read/write-perf output
 * Total width of top read/write-perf should be 80 chars
 * including one space between column
 */
#define VOL_TOP_PERF_FILENAME_DEF_WIDTH 47
#define VOL_TOP_PERF_FILENAME_ALT_WIDTH 44
#define VOL_TOP_PERF_SPEED_WIDTH 4
#define VOL_TOP_PERF_TIME_WIDTH 26

#define INDENT_MAIN_HEAD "%-25s %s "

#define RETURNING "Returning %d"
#define XML_ERROR "Error outputting to xml"
#define XDR_DECODE_FAIL "Failed to decode xdr response"
#define DICT_SERIALIZE_FAIL "Failed to serialize to data to dictionary"
#define DICT_UNSERIALIZE_FAIL "Failed to unserialize the dictionary"

/* Do not show estimates if greater than this number */
#define REBAL_ESTIMATE_SEC_UPPER_LIMIT (60 * 24 * 3600)
#define REBAL_ESTIMATE_START_TIME 600

#include "cli.h"
#include <glusterfs/compat-errno.h>
#include "cli-cmd.h"
#include <sys/uio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <glusterfs/compat.h>
#include "cli-mem-types.h"
#include <glusterfs/syscall.h>
#include "glusterfs3.h"
#include "portmap-xdr.h"
#include <glusterfs/byte-order.h>

#include <glusterfs/run.h>
#include <glusterfs/events.h>

enum gf_task_types { GF_TASK_TYPE_REBALANCE, GF_TASK_TYPE_REMOVE_BRICK };

rpc_clnt_prog_t cli_quotad_clnt;

static int32_t
gf_cli_remove_brick(call_frame_t *frame, xlator_t *this, void *data);

char *cli_vol_status_str[] = {
    "Created",
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
                                   "unknown"};

static int32_t
gf_cli_snapshot(call_frame_t *frame, xlator_t *this, void *data);

static int32_t
gf_cli_get_volume(call_frame_t *frame, xlator_t *this, void *data);

static int
cli_to_glusterd(gf_cli_req *req, call_frame_t *frame, fop_cbk_fn_t cbkfn,
                xdrproc_t xdrproc, dict_t *dict, int procnum, xlator_t *this,
                rpc_clnt_prog_t *prog, struct iobref *iobref);

static int
add_cli_cmd_timeout_to_dict(dict_t *dict);

static rpc_clnt_prog_t cli_handshake_prog = {
    .progname = "cli handshake",
    .prognum = GLUSTER_HNDSK_PROGRAM,
    .progver = GLUSTER_HNDSK_VERSION,
};

static rpc_clnt_prog_t cli_pmap_prog = {
    .progname = "cli portmap",
    .prognum = GLUSTER_PMAP_PROGRAM,
    .progver = GLUSTER_PMAP_VERSION,
};

static void
gf_free_xdr_cli_rsp(gf_cli_rsp rsp)
{
    if (rsp.dict.dict_val) {
        free(rsp.dict.dict_val);
    }
    if (rsp.op_errstr) {
        free(rsp.op_errstr);
    }
}

static void
gf_free_xdr_getspec_rsp(gf_getspec_rsp rsp)
{
    if (rsp.spec) {
        free(rsp.spec);
    }
    if (rsp.xdata.xdata_val) {
        free(rsp.xdata.xdata_val);
    }
}

static void
gf_free_xdr_fsm_log_rsp(gf1_cli_fsm_log_rsp rsp)
{
    if (rsp.op_errstr) {
        free(rsp.op_errstr);
    }
    if (rsp.fsm_log.fsm_log_val) {
        free(rsp.fsm_log.fsm_log_val);
    }
}

static int
gf_cli_probe_cbk(struct rpc_req *req, struct iovec *iov, int count,
                 void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    char msg[1024] = "success";

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        // rsp.op_ret   = -1;
        // rsp.op_errno = EINVAL;
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to probe");

    if (rsp.op_errstr && rsp.op_errstr[0] != '\0') {
        snprintf(msg, sizeof(msg), "%s", rsp.op_errstr);
        if (rsp.op_ret) {
            gf_log("cli", GF_LOG_ERROR, "%s", msg);
        }
    }

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_str(NULL, (rsp.op_ret) ? NULL : msg, rsp.op_ret,
                                 rsp.op_errno, (rsp.op_ret) ? msg : NULL);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (!rsp.op_ret)
        cli_out("peer probe: %s", msg);
    else
        cli_err("peer probe: failed: %s", msg);

    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int
gf_cli_deprobe_cbk(struct rpc_req *req, struct iovec *iov, int count,
                   void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    char msg[1024] = "success";

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        // rsp.op_ret   = -1;
        // rsp.op_errno = EINVAL;
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to deprobe");

    if (rsp.op_ret) {
        if (rsp.op_errstr[0] != '\0') {
            snprintf(msg, sizeof(msg), "%s", rsp.op_errstr);
            gf_log("cli", GF_LOG_ERROR, "%s", rsp.op_errstr);
        }
    }

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_str(NULL, (rsp.op_ret) ? NULL : msg, rsp.op_ret,
                                 rsp.op_errno, (rsp.op_ret) ? msg : NULL);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (!rsp.op_ret)
        cli_out("peer detach: %s", msg);
    else
        cli_err("peer detach: failed: %s", msg);

    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int
gf_cli_output_peer_hostnames(dict_t *dict, int count, const char *prefix)
{
    int ret = -1;
    char key[512] = {
        0,
    };
    int i = 0;
    char *hostname = NULL;

    cli_out("Other names:");
    /* Starting from friend.hostname1, as friend.hostname0 will be the same
     * as friend.hostname
     */
    for (i = 1; i < count; i++) {
        ret = snprintf(key, sizeof(key), "%s.hostname%d", prefix, i);
        ret = dict_get_strn(dict, key, ret, &hostname);
        if (ret)
            break;
        cli_out("%s", hostname);
        hostname = NULL;
    }

    return ret;
}

static int
gf_cli_output_peer_status(dict_t *dict, int count)
{
    int ret = -1;
    char *uuid_buf = NULL;
    char *hostname_buf = NULL;
    int32_t i = 1;
    char key[256] = {
        0,
    };
    int keylen;
    char *state = NULL;
    int32_t connected = 0;
    const char *connected_str = NULL;
    int hostname_count = 0;

    cli_out("Number of Peers: %d", count);
    i = 1;
    while (i <= count) {
        keylen = snprintf(key, sizeof(key), "friend%d.uuid", i);
        ret = dict_get_strn(dict, key, keylen, &uuid_buf);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "friend%d.hostname", i);
        ret = dict_get_strn(dict, key, keylen, &hostname_buf);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "friend%d.connected", i);
        ret = dict_get_int32n(dict, key, keylen, &connected);
        if (ret)
            goto out;
        if (connected)
            connected_str = "Connected";
        else
            connected_str = "Disconnected";

        keylen = snprintf(key, sizeof(key), "friend%d.state", i);
        ret = dict_get_strn(dict, key, keylen, &state);
        if (ret)
            goto out;

        cli_out("\nHostname: %s\nUuid: %s\nState: %s (%s)", hostname_buf,
                uuid_buf, state, connected_str);

        keylen = snprintf(key, sizeof(key), "friend%d.hostname_count", i);
        ret = dict_get_int32n(dict, key, keylen, &hostname_count);
        /* Print other addresses only if there are more than 1.
         */
        if ((ret == 0) && (hostname_count > 1)) {
            snprintf(key, sizeof(key), "friend%d", i);
            ret = gf_cli_output_peer_hostnames(dict, hostname_count, key);
            if (ret) {
                gf_log("cli", GF_LOG_WARNING,
                       "error outputting peer other names");
                goto out;
            }
        }
        i++;
    }

    ret = 0;
out:
    return ret;
}

static int
gf_cli_output_pool_list(dict_t *dict, int count)
{
    int ret = -1;
    char *uuid_buf = NULL;
    char *hostname_buf = NULL;
    int32_t hostname_len = 8; /*min len 8 chars*/
    int32_t i = 1;
    char key[64] = {
        0,
    };
    int keylen;
    int32_t connected = 0;
    const char *connected_str = NULL;

    if (count <= 0)
        goto out;

    while (i <= count) {
        keylen = snprintf(key, sizeof(key), "friend%d.hostname", i);
        ret = dict_get_strn(dict, key, keylen, &hostname_buf);
        if (ret)
            goto out;

        ret = strlen(hostname_buf);
        if (ret > hostname_len)
            hostname_len = ret;

        i++;
    }

    cli_out("UUID\t\t\t\t\t%-*s\tState", hostname_len, "Hostname");

    i = 1;
    while (i <= count) {
        keylen = snprintf(key, sizeof(key), "friend%d.uuid", i);
        ret = dict_get_strn(dict, key, keylen, &uuid_buf);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "friend%d.hostname", i);
        ret = dict_get_strn(dict, key, keylen, &hostname_buf);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "friend%d.connected", i);
        ret = dict_get_int32n(dict, key, keylen, &connected);
        if (ret)
            goto out;
        if (connected)
            connected_str = "Connected";
        else
            connected_str = "Disconnected";

        cli_out("%s\t%-*s\t%s ", uuid_buf, hostname_len, hostname_buf,
                connected_str);
        i++;
    }

    ret = 0;
out:
    return ret;
}

/* function pointer for gf_cli_output_{pool_list,peer_status} */
typedef int (*cli_friend_output_fn)(dict_t *, int);

static int
gf_cli_list_friends_cbk(struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
    gf1_cli_peer_list_rsp rsp = {
        0,
    };
    int ret = -1;
    dict_t *dict = NULL;
    char msg[1024] = {
        0,
    };
    const char *cmd = NULL;
    cli_friend_output_fn friend_output_fn;
    call_frame_t *frame = NULL;
    unsigned long flags = 0;

    if (-1 == req->rpc_status) {
        goto out;
    }

    GF_ASSERT(myframe);

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

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf1_cli_peer_list_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        // rsp.op_ret   = -1;
        // rsp.op_errno = EINVAL;
        goto out;
    }

    gf_log("cli", GF_LOG_DEBUG, "Received resp to list: %d", rsp.op_ret);

    if (!rsp.op_ret) {
        if (!rsp.friends.friends_len) {
            snprintf(msg, sizeof(msg), "%s: No peers present", cmd);
            if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_peer_status(dict, rsp.op_ret, rsp.op_errno,
                                                 msg);
                if (ret)
                    gf_log("cli", GF_LOG_ERROR, XML_ERROR);
                goto out;
            }
            cli_err("%s", msg);
            ret = 0;
            goto out;
        }

        dict = dict_new();

        if (!dict) {
            ret = -1;
            goto out;
        }

        ret = dict_unserialize(rsp.friends.friends_val, rsp.friends.friends_len,
                               &dict);

        if (ret) {
            gf_log("", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
            goto out;
        }

        if (global_state->mode & GLUSTER_MODE_XML) {
            ret = cli_xml_output_peer_status(dict, rsp.op_ret, rsp.op_errno,
                                             msg);
            if (ret)
                gf_log("cli", GF_LOG_ERROR, XML_ERROR);
            goto out;
        }

        ret = dict_get_int32_sizen(dict, "count", &count);
        if (ret) {
            goto out;
        }

        ret = friend_output_fn(dict, count);
        if (ret) {
            goto out;
        }
    } else {
        if (global_state->mode & GLUSTER_MODE_XML) {
            ret = cli_xml_output_peer_status(dict, rsp.op_ret, rsp.op_errno,
                                             NULL);
            if (ret)
                gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        } else {
            ret = -1;
        }
        goto out;
    }

    ret = 0;

out:
    if (ret)
        cli_err("%s: failed", cmd);

    cli_cmd_broadcast_response(ret);

    if (dict)
        dict_unref(dict);

    if (rsp.friends.friends_val) {
        free(rsp.friends.friends_val);
    }
    return ret;
}

static int
gf_cli_get_state_cbk(struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    dict_t *dict = NULL;
    char *daemon_name = NULL;
    char *ofilepath = NULL;

    GF_VALIDATE_OR_GOTO("cli", myframe, out);

    if (-1 == req->rpc_status) {
        goto out;
    }
    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    dict = dict_new();

    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);
    if (ret)
        goto out;

    if (rsp.op_ret) {
        if (strcmp(rsp.op_errstr, ""))
            cli_err("Failed to get daemon state: %s", rsp.op_errstr);
        else
            cli_err(
                "Failed to get daemon state. Check glusterd"
                " log file for more details");
    } else {
        ret = dict_get_str_sizen(dict, "daemon", &daemon_name);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, "Couldn't get daemon name");

        ret = dict_get_str_sizen(dict, "ofilepath", &ofilepath);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, "Couldn't get filepath");

        if (daemon_name && ofilepath)
            cli_out("%s state dumped to %s", daemon_name, ofilepath);
    }

    ret = rsp.op_ret;

out:
    if (dict)
        dict_unref(dict);

    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);

    return ret;
}

static void
cli_out_options(char *substr, char *optstr, char *valstr)
{
    char *ptr1 = NULL;
    char *ptr2 = NULL;

    ptr1 = substr;
    ptr2 = optstr;

    while (ptr1) {
        /* Avoiding segmentation fault. */
        if (!ptr2)
            return;
        if (*ptr1 != *ptr2)
            break;
        ptr1++;
        ptr2++;
    }

    if (*ptr2 == '\0')
        return;
    cli_out("%s: %s", ptr2, valstr);
}

static int
_gf_cli_output_volinfo_opts(dict_t *d, char *k, data_t *v, void *tmp)
{
    int ret = 0;
    char *key = NULL;
    char *ptr = NULL;
    data_t *value = NULL;

    key = tmp;

    ptr = strstr(k, "option.");
    if (ptr) {
        value = v;
        if (!value) {
            ret = -1;
            goto out;
        }
        cli_out_options(key, k, v->data);
    }
out:
    return ret;
}

static int
print_brick_details(dict_t *dict, int volcount, int start_index, int end_index,
                    int replica_count)
{
    char key[64] = {
        0,
    };
    int keylen;
    int index = start_index;
    int isArbiter = 0;
    int ret = -1;
    char *brick = NULL;

    while (index <= end_index) {
        keylen = snprintf(key, sizeof(key), "volume%d.brick%d", volcount,
                          index);
        ret = dict_get_strn(dict, key, keylen, &brick);
        if (ret)
            goto out;
        keylen = snprintf(key, sizeof(key), "volume%d.brick%d.isArbiter",
                          volcount, index);
        if (dict_getn(dict, key, keylen))
            isArbiter = 1;
        else
            isArbiter = 0;

        if (isArbiter)
            cli_out("Brick%d: %s (arbiter)", index, brick);
        else
            cli_out("Brick%d: %s", index, brick);
        index++;
    }
    ret = 0;
out:
    return ret;
}

static void
gf_cli_print_number_of_bricks(int type, int brick_count, int dist_count,
                              int stripe_count, int replica_count,
                              int disperse_count, int redundancy_count,
                              int arbiter_count)
{
    if (type == GF_CLUSTER_TYPE_NONE) {
        cli_out("Number of Bricks: %d", brick_count);
    } else if (type == GF_CLUSTER_TYPE_DISPERSE) {
        cli_out("Number of Bricks: %d x (%d + %d) = %d",
                (brick_count / dist_count), disperse_count - redundancy_count,
                redundancy_count, brick_count);
    } else {
        /* For both replicate and stripe, dist_count is
           good enough */
        if (arbiter_count == 0) {
            cli_out("Number of Bricks: %d x %d = %d",
                    (brick_count / dist_count), dist_count, brick_count);
        } else {
            cli_out("Number of Bricks: %d x (%d + %d) = %d",
                    (brick_count / dist_count), dist_count - arbiter_count,
                    arbiter_count, brick_count);
        }
    }
}

static int
gf_cli_get_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
    int ret = -1;
    int opt_count = 0;
    int32_t i = 0;
    int32_t j = 1;
    int32_t status = 0;
    int32_t type = 0;
    int32_t brick_count = 0;
    int32_t dist_count = 0;
    int32_t stripe_count = 0;
    int32_t replica_count = 0;
    int32_t disperse_count = 0;
    int32_t redundancy_count = 0;
    int32_t arbiter_count = 0;
    int32_t snap_count = 0;
    int32_t thin_arbiter_count = 0;
    int32_t vol_type = 0;
    int32_t transport = 0;
    char *volume_id_str = NULL;
    char *volname = NULL;
    char *ta_brick = NULL;
    dict_t *dict = NULL;
    cli_local_t *local = NULL;
    char key[64] = {0};
    int keylen;
    char err_str[2048] = {0};
    gf_cli_rsp rsp = {0};
    char *caps __attribute__((unused)) = NULL;
    int k __attribute__((unused)) = 0;
    call_frame_t *frame = NULL;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status)
        goto out;

    frame = myframe;

    GF_ASSERT(frame->local);

    local = frame->local;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to get vol: %d", rsp.op_ret);

    if (!rsp.dict.dict_len) {
        if (global_state->mode & GLUSTER_MODE_XML)
            goto xml_output;
        cli_err("No volumes present");
        ret = 0;
        goto out;
    }

    dict = dict_new();

    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);

    if (ret) {
        gf_log("cli", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
        goto out;
    }

    ret = dict_get_int32_sizen(dict, "count", &count);
    if (ret)
        goto out;

    if (!count) {
        switch (local->get_vol.flags) {
            case GF_CLI_GET_NEXT_VOLUME:
                GF_FREE(local->get_vol.volname);
                local->get_vol.volname = NULL;
                ret = 0;
                goto out;

            case GF_CLI_GET_VOLUME:
                snprintf(err_str, sizeof(err_str), "Volume %s does not exist",
                         local->get_vol.volname);
                ret = -1;
                if (!(global_state->mode & GLUSTER_MODE_XML))
                    goto out;
        }
    }

    if (rsp.op_ret) {
        if (global_state->mode & GLUSTER_MODE_XML)
            goto xml_output;
        ret = -1;
        goto out;
    }

xml_output:
    if (global_state->mode & GLUSTER_MODE_XML) {
        /* For GET_NEXT_VOLUME output is already begun in
         * and will also end in gf_cli_get_next_volume()
         */
        if (local->get_vol.flags == GF_CLI_GET_VOLUME) {
            ret = cli_xml_output_vol_info_begin(local, rsp.op_ret, rsp.op_errno,
                                                rsp.op_errstr);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, XML_ERROR);
                goto out;
            }
        }

        if (dict) {
            ret = cli_xml_output_vol_info(local, dict);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, XML_ERROR);
                goto out;
            }
        }

        if (local->get_vol.flags == GF_CLI_GET_VOLUME) {
            ret = cli_xml_output_vol_info_end(local);
            if (ret)
                gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        }
        goto out;
    }

    while (i < count) {
        cli_out(" ");
        keylen = snprintf(key, sizeof(key), "volume%d.name", i);
        ret = dict_get_strn(dict, key, keylen, &volname);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "volume%d.type", i);
        ret = dict_get_int32n(dict, key, keylen, &type);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "volume%d.status", i);
        ret = dict_get_int32n(dict, key, keylen, &status);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "volume%d.brick_count", i);
        ret = dict_get_int32n(dict, key, keylen, &brick_count);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "volume%d.dist_count", i);
        ret = dict_get_int32n(dict, key, keylen, &dist_count);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "volume%d.stripe_count", i);
        ret = dict_get_int32n(dict, key, keylen, &stripe_count);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "volume%d.replica_count", i);
        ret = dict_get_int32n(dict, key, keylen, &replica_count);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "volume%d.disperse_count", i);
        ret = dict_get_int32n(dict, key, keylen, &disperse_count);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "volume%d.redundancy_count", i);
        ret = dict_get_int32n(dict, key, keylen, &redundancy_count);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "volume%d.arbiter_count", i);
        ret = dict_get_int32n(dict, key, keylen, &arbiter_count);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "volume%d.transport", i);
        ret = dict_get_int32n(dict, key, keylen, &transport);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "volume%d.volume_id", i);
        ret = dict_get_strn(dict, key, keylen, &volume_id_str);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "volume%d.snap_count", i);
        ret = dict_get_int32n(dict, key, keylen, &snap_count);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "volume%d.thin_arbiter_count", i);
        ret = dict_get_int32n(dict, key, keylen, &thin_arbiter_count);
        if (ret)
            goto out;

        // Distributed (stripe/replicate/stripe-replica) setups
        vol_type = get_vol_type(type, dist_count, brick_count);

        cli_out("Volume Name: %s", volname);
        cli_out("Type: %s", vol_type_str[vol_type]);
        cli_out("Volume ID: %s", volume_id_str);
        cli_out("Status: %s", cli_vol_status_str[status]);
        cli_out("Snapshot Count: %d", snap_count);

        gf_cli_print_number_of_bricks(
            type, brick_count, dist_count, stripe_count, replica_count,
            disperse_count, redundancy_count, arbiter_count);

        cli_out("Transport-type: %s",
                ((transport == 0) ? "tcp"
                                  : (transport == 1) ? "rdma" : "tcp,rdma"));
        j = 1;

        GF_FREE(local->get_vol.volname);
        local->get_vol.volname = gf_strdup(volname);

        cli_out("Bricks:");
        ret = print_brick_details(dict, i, j, brick_count, replica_count);
        if (ret)
            goto out;

        if (thin_arbiter_count) {
            snprintf(key, sizeof(key), "volume%d.thin_arbiter_brick", i);
            ret = dict_get_str(dict, key, &ta_brick);
            if (ret)
                goto out;
            cli_out("Thin-arbiter-path: %s", ta_brick);
        }

        snprintf(key, sizeof(key), "volume%d.opt_count", i);
        ret = dict_get_int32(dict, key, &opt_count);
        if (ret)
            goto out;

        if (!opt_count)
            goto out;

        cli_out("Options Reconfigured:");

        snprintf(key, sizeof(key), "volume%d.option.", i);

        ret = dict_foreach(dict, _gf_cli_output_volinfo_opts, key);
        if (ret)
            goto out;

        i++;
    }

    ret = 0;
out:
    if (ret)
        cli_err("%s", err_str);

    cli_cmd_broadcast_response(ret);

    if (dict)
        dict_unref(dict);

    gf_free_xdr_cli_rsp(rsp);

    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    return ret;
}

static int
gf_cli_create_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    cli_local_t *local = NULL;
    char *volname = NULL;
    dict_t *rsp_dict = NULL;
    call_frame_t *frame = NULL;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    frame = myframe;

    GF_ASSERT(frame->local);

    local = frame->local;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to create volume");

    if (global_state->mode & GLUSTER_MODE_XML) {
        if (rsp.op_ret == 0) {
            rsp_dict = dict_new();
            ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len,
                                   &rsp_dict);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
                goto out;
            }
        }

        ret = cli_xml_output_vol_create(rsp_dict, rsp.op_ret, rsp.op_errno,
                                        rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    ret = dict_get_str_sizen(local->dict, "volname", &volname);
    if (ret)
        goto out;

    if (rsp.op_ret && strcmp(rsp.op_errstr, ""))
        cli_err("volume create: %s: failed: %s", volname, rsp.op_errstr);
    else if (rsp.op_ret)
        cli_err("volume create: %s: failed", volname);
    else
        cli_out(
            "volume create: %s: success: "
            "please start the volume to access data",
            volname);

    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);

    if (rsp_dict)
        dict_unref(rsp_dict);
    return ret;
}

static int
gf_cli_delete_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    cli_local_t *local = NULL;
    char *volname = NULL;
    call_frame_t *frame = NULL;
    dict_t *rsp_dict = NULL;

    if (-1 == req->rpc_status) {
        goto out;
    }

    GF_ASSERT(myframe);
    frame = myframe;

    GF_ASSERT(frame->local);

    local = frame->local;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to delete volume");

    if (global_state->mode & GLUSTER_MODE_XML) {
        if (rsp.op_ret == 0) {
            rsp_dict = dict_new();
            ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len,
                                   &rsp_dict);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
                goto out;
            }
        }

        ret = cli_xml_output_generic_volume("volDelete", rsp_dict, rsp.op_ret,
                                            rsp.op_errno, rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    ret = dict_get_str_sizen(local->dict, "volname", &volname);
    if (ret) {
        gf_log(frame->this->name, GF_LOG_ERROR, "dict get failed");
        goto out;
    }

    if (rsp.op_ret && strcmp(rsp.op_errstr, ""))
        cli_err("volume delete: %s: failed: %s", volname, rsp.op_errstr);
    else if (rsp.op_ret)
        cli_err("volume delete: %s: failed", volname);
    else
        cli_out("volume delete: %s: success", volname);

    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);

    if (rsp_dict)
        dict_unref(rsp_dict);
    gf_log("", GF_LOG_DEBUG, RETURNING, ret);
    return ret;
}

static int
gf_cli3_1_uuid_get_cbk(struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
    char *uuid_str = NULL;
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    cli_local_t *local = NULL;
    call_frame_t *frame = NULL;
    dict_t *dict = NULL;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status)
        goto out;

    frame = myframe;

    GF_ASSERT(frame->local);

    local = frame->local;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        goto out;
    }

    frame->local = NULL;

    gf_log("cli", GF_LOG_INFO, "Received resp to uuid get");

    dict = dict_new();
    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
        goto out;
    }

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_dict("uuidGenerate", dict, rsp.op_ret,
                                  rsp.op_errno, rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (rsp.op_ret) {
        if (strcmp(rsp.op_errstr, "") == 0)
            cli_err("Get uuid was unsuccessful");
        else
            cli_err("%s", rsp.op_errstr);

    } else {
        ret = dict_get_str_sizen(dict, "uuid", &uuid_str);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, "Failed to get uuid from dictionary");
            goto out;
        }
        cli_out("UUID: %s", uuid_str);
    }
    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    cli_local_wipe(local);
    gf_free_xdr_cli_rsp(rsp);

    if (dict)
        dict_unref(dict);

    gf_log("", GF_LOG_DEBUG, RETURNING, ret);
    return ret;
}

static int
gf_cli3_1_uuid_reset_cbk(struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    cli_local_t *local = NULL;
    call_frame_t *frame = NULL;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    frame = myframe;

    GF_ASSERT(frame->local);

    local = frame->local;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        goto out;
    }

    frame->local = NULL;

    gf_log("cli", GF_LOG_INFO, "Received resp to uuid reset");

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_dict("uuidReset", NULL, rsp.op_ret, rsp.op_errno,
                                  rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (rsp.op_ret && strcmp(rsp.op_errstr, ""))
        cli_err("%s", rsp.op_errstr);
    else
        cli_out("resetting the peer uuid has been %s",
                (rsp.op_ret) ? "unsuccessful" : "successful");
    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    cli_local_wipe(local);
    gf_free_xdr_cli_rsp(rsp);

    gf_log("", GF_LOG_DEBUG, RETURNING, ret);
    return ret;
}

static int
gf_cli_start_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    cli_local_t *local = NULL;
    char *volname = NULL;
    call_frame_t *frame = NULL;
    dict_t *rsp_dict = NULL;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    frame = myframe;

    GF_ASSERT(frame->local);

    local = frame->local;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to start volume");

    if (global_state->mode & GLUSTER_MODE_XML) {
        if (rsp.op_ret == 0) {
            rsp_dict = dict_new();
            ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len,
                                   &rsp_dict);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
                goto out;
            }
        }

        ret = cli_xml_output_generic_volume("volStart", rsp_dict, rsp.op_ret,
                                            rsp.op_errno, rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    ret = dict_get_str_sizen(local->dict, "volname", &volname);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "dict get failed");
        goto out;
    }

    if (rsp.op_ret && strcmp(rsp.op_errstr, ""))
        cli_err("volume start: %s: failed: %s", volname, rsp.op_errstr);
    else if (rsp.op_ret)
        cli_err("volume start: %s: failed", volname);
    else
        cli_out("volume start: %s: success", volname);

    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);

    if (rsp_dict)
        dict_unref(rsp_dict);
    return ret;
}

static int
gf_cli_stop_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    cli_local_t *local = NULL;
    char *volname = NULL;
    call_frame_t *frame = NULL;
    dict_t *rsp_dict = NULL;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    frame = myframe;

    GF_ASSERT(frame->local);

    local = frame->local;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to stop volume");

    if (global_state->mode & GLUSTER_MODE_XML) {
        if (rsp.op_ret == 0) {
            rsp_dict = dict_new();
            ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len,
                                   &rsp_dict);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
                goto out;
            }
        }

        ret = cli_xml_output_generic_volume("volStop", rsp_dict, rsp.op_ret,
                                            rsp.op_errno, rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    ret = dict_get_str_sizen(local->dict, "volname", &volname);
    if (ret) {
        gf_log(frame->this->name, GF_LOG_ERROR,
               "Unable to get volname from dict");
        goto out;
    }

    if (rsp.op_ret && strcmp(rsp.op_errstr, ""))
        cli_err("volume stop: %s: failed: %s", volname, rsp.op_errstr);
    else if (rsp.op_ret)
        cli_err("volume stop: %s: failed", volname);
    else
        cli_out("volume stop: %s: success", volname);

    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);

    if (rsp_dict)
        dict_unref(rsp_dict);

    return ret;
}

static int
gf_cli_print_rebalance_status(dict_t *dict, enum gf_task_types task_type)
{
    int ret = -1;
    int count = 0;
    int i = 1;
    char key[64] = {
        0,
    };
    int keylen;
    gf_defrag_status_t status_rcd = GF_DEFRAG_STATUS_NOT_STARTED;
    uint64_t files = 0;
    uint64_t size = 0;
    uint64_t lookup = 0;
    char *node_name = NULL;
    uint64_t failures = 0;
    uint64_t skipped = 0;
    double elapsed = 0;
    char *status_str = NULL;
    char *size_str = NULL;
    int32_t hrs = 0;
    uint32_t min = 0;
    uint32_t sec = 0;
    gf_boolean_t fix_layout = _gf_false;
    uint64_t max_time = 0;
    uint64_t max_elapsed = 0;
    uint64_t time_left = 0;
    gf_boolean_t show_estimates = _gf_false;

    ret = dict_get_int32_sizen(dict, "count", &count);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "count not set");
        goto out;
    }

    for (i = 1; i <= count; i++) {
        keylen = snprintf(key, sizeof(key), "status-%d", i);
        ret = dict_get_int32n(dict, key, keylen, (int32_t *)&status_rcd);
        /* If information from a node is missing we should skip
         * the node and try to fetch information of other nodes.
         * If information is not found for all nodes, we should
         * error out.
         */
        if (!ret)
            break;
        if (ret && i == count) {
            gf_log("cli", GF_LOG_TRACE, "failed to get status");
            goto out;
        }
    }

    /* Fix layout will be sent to all nodes for the volume
       so every status should be of type
       GF_DEFRAG_STATUS_LAYOUT_FIX*
    */

    if ((task_type == GF_TASK_TYPE_REBALANCE) &&
        (status_rcd >= GF_DEFRAG_STATUS_LAYOUT_FIX_STARTED)) {
        fix_layout = _gf_true;
    }

    if (fix_layout) {
        cli_out("%35s %41s %27s", "Node", "status", "run time in h:m:s");
        cli_out("%35s %41s %27s", "---------", "-----------", "------------");
    } else {
        cli_out("%40s %16s %13s %13s %13s %13s %20s %18s", "Node",
                "Rebalanced-files", "size", "scanned", "failures", "skipped",
                "status",
                "run time in"
                " h:m:s");
        cli_out("%40s %16s %13s %13s %13s %13s %20s %18s", "---------",
                "-----------", "-----------", "-----------", "-----------",
                "-----------", "------------", "--------------");
    }

    for (i = 1; i <= count; i++) {
        /* Reset the variables to prevent carryover of values */
        node_name = NULL;
        files = 0;
        size = 0;
        lookup = 0;
        skipped = 0;
        status_str = NULL;
        elapsed = 0;
        time_left = 0;

        /* Check if status is NOT_STARTED, and continue early */
        keylen = snprintf(key, sizeof(key), "status-%d", i);

        ret = dict_get_int32n(dict, key, keylen, (int32_t *)&status_rcd);
        if (ret == -ENOENT) {
            gf_log("cli", GF_LOG_TRACE, "count %d %d", count, i);
            gf_log("cli", GF_LOG_TRACE, "failed to get status");
            gf_log("cli", GF_LOG_ERROR, "node down and has failed to set dict");
            continue;
            /* skip this node if value not available*/
        } else if (ret) {
            gf_log("cli", GF_LOG_TRACE, "count %d %d", count, i);
            gf_log("cli", GF_LOG_TRACE, "failed to get status");
            continue;
            /* skip this node if value not available*/
        }

        if (GF_DEFRAG_STATUS_NOT_STARTED == status_rcd)
            continue;

        if (GF_DEFRAG_STATUS_STARTED == status_rcd)
            show_estimates = _gf_true;

        keylen = snprintf(key, sizeof(key), "node-name-%d", i);
        ret = dict_get_strn(dict, key, keylen, &node_name);
        if (ret)
            gf_log("cli", GF_LOG_TRACE, "failed to get node-name");

        snprintf(key, sizeof(key), "files-%d", i);
        ret = dict_get_uint64(dict, key, &files);
        if (ret)
            gf_log("cli", GF_LOG_TRACE, "failed to get file count");

        snprintf(key, sizeof(key), "size-%d", i);
        ret = dict_get_uint64(dict, key, &size);
        if (ret)
            gf_log("cli", GF_LOG_TRACE, "failed to get size of xfer");

        snprintf(key, sizeof(key), "lookups-%d", i);
        ret = dict_get_uint64(dict, key, &lookup);
        if (ret)
            gf_log("cli", GF_LOG_TRACE, "failed to get lookedup file count");

        snprintf(key, sizeof(key), "failures-%d", i);
        ret = dict_get_uint64(dict, key, &failures);
        if (ret)
            gf_log("cli", GF_LOG_TRACE, "failed to get failures count");

        snprintf(key, sizeof(key), "skipped-%d", i);
        ret = dict_get_uint64(dict, key, &skipped);
        if (ret)
            gf_log("cli", GF_LOG_TRACE, "failed to get skipped count");

        /* For remove-brick include skipped count into failure count*/
        if (task_type != GF_TASK_TYPE_REBALANCE) {
            failures += skipped;
            skipped = 0;
        }

        snprintf(key, sizeof(key), "run-time-%d", i);
        ret = dict_get_double(dict, key, &elapsed);
        if (ret)
            gf_log("cli", GF_LOG_TRACE, "failed to get run-time");

        snprintf(key, sizeof(key), "time-left-%d", i);
        ret = dict_get_uint64(dict, key, &time_left);
        if (ret)
            gf_log("cli", GF_LOG_TRACE, "failed to get time left");

        if (elapsed > max_elapsed)
            max_elapsed = elapsed;

        if (time_left > max_time)
            max_time = time_left;

        /* Check for array bound */
        if (status_rcd >= GF_DEFRAG_STATUS_MAX)
            status_rcd = GF_DEFRAG_STATUS_MAX;

        status_str = cli_vol_task_status_str[status_rcd];
        size_str = gf_uint64_2human_readable(size);
        hrs = elapsed / 3600;
        min = ((uint64_t)elapsed % 3600) / 60;
        sec = ((uint64_t)elapsed % 3600) % 60;

        if (fix_layout) {
            cli_out("%35s %50s %8d:%d:%d", node_name, status_str, hrs, min,
                    sec);
        } else {
            if (size_str) {
                cli_out("%40s %16" PRIu64
                        " %13s"
                        " %13" PRIu64 " %13" PRIu64 " %13" PRIu64
                        " %20s "
                        "%8d:%02d:%02d",
                        node_name, files, size_str, lookup, failures, skipped,
                        status_str, hrs, min, sec);
            } else {
                cli_out("%40s %16" PRIu64 " %13" PRIu64 " %13" PRIu64
                        " %13" PRIu64 " %13" PRIu64
                        " %20s"
                        " %8d:%02d:%02d",
                        node_name, files, size, lookup, failures, skipped,
                        status_str, hrs, min, sec);
            }
        }
        GF_FREE(size_str);
    }

    /* Max time will be non-zero if rebalance is still running */
    if (max_time) {
        hrs = max_time / 3600;
        min = (max_time % 3600) / 60;
        sec = (max_time % 3600) % 60;

        if (hrs < REBAL_ESTIMATE_SEC_UPPER_LIMIT) {
            cli_out(
                "Estimated time left for rebalance to "
                "complete : %8d:%02d:%02d",
                hrs, min, sec);
        } else {
            cli_out(
                "Estimated time left for rebalance to "
                "complete : > 2 months. Please try again "
                "later.");
        }
    } else {
        /* Rebalance will return 0 if it could not calculate the
         * estimates or if it is complete.
         */
        if (!show_estimates) {
            goto out;
        }
        if (max_elapsed <= REBAL_ESTIMATE_START_TIME) {
            cli_out(
                "The estimated time for rebalance to complete "
                "will be unavailable for the first 10 "
                "minutes.");
        } else {
            cli_out(
                "Rebalance estimated time unavailable. Please "
                "try again later.");
        }
    }
out:
    return ret;
}

static int
gf_cli_defrag_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    cli_local_t *local = NULL;
    char *volname = NULL;
    call_frame_t *frame = NULL;
    int cmd = 0;
    int ret = -1;
    dict_t *dict = NULL;
    char msg[1024] = {
        0,
    };
    char *task_id_str = NULL;

    if (-1 == req->rpc_status) {
        goto out;
    }

    GF_ASSERT(myframe);

    frame = myframe;

    GF_ASSERT(frame->local);

    local = frame->local;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        goto out;
    }

    ret = dict_get_str_sizen(local->dict, "volname", &volname);
    if (ret) {
        gf_log(frame->this->name, GF_LOG_ERROR, "Failed to get volname");
        goto out;
    }

    ret = dict_get_int32_sizen(local->dict, "rebalance-command",
                               (int32_t *)&cmd);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Failed to get command");
        goto out;
    }

    if (rsp.dict.dict_len) {
        /* Unserialize the dictionary */
        dict = dict_new();

        ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);
        if (ret < 0) {
            gf_log("glusterd", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
            goto out;
        }
    }

    if (!((cmd == GF_DEFRAG_CMD_STOP) || (cmd == GF_DEFRAG_CMD_STATUS)) &&
        !(global_state->mode & GLUSTER_MODE_XML)) {
        ret = dict_get_str_sizen(dict, GF_REBALANCE_TID_KEY, &task_id_str);
        if (ret) {
            gf_log("cli", GF_LOG_WARNING, "failed to get %s from dict",
                   GF_REBALANCE_TID_KEY);
        }
        if (rsp.op_ret && strcmp(rsp.op_errstr, "")) {
            snprintf(msg, sizeof(msg), "%s", rsp.op_errstr);
        } else {
            if (!rsp.op_ret) {
                /* append errstr in the cli msg for successful
                 * case since unlock failures can be highlighted
                 * event though rebalance command was successful
                 */
                snprintf(msg, sizeof(msg),
                         "Rebalance on %s has been "
                         "started successfully. Use "
                         "rebalance status command to"
                         " check status of the "
                         "rebalance process.\nID: %s",
                         volname, task_id_str);
            } else {
                snprintf(msg, sizeof(msg),
                         "Starting rebalance on volume %s has "
                         "been unsuccessful.",
                         volname);
            }
        }
        goto done;
    }

    if (cmd == GF_DEFRAG_CMD_STOP) {
        if (rsp.op_ret == -1) {
            if (strcmp(rsp.op_errstr, ""))
                snprintf(msg, sizeof(msg), "%s", rsp.op_errstr);
            else
                snprintf(msg, sizeof(msg), "rebalance volume %s stop failed",
                         volname);
            goto done;
        } else {
            /* append errstr in the cli msg for successful case
             * since unlock failures can be highlighted event though
             * rebalance command was successful */
            snprintf(msg, sizeof(msg),
                     "rebalance process may be in the middle of a "
                     "file migration.\nThe process will be fully "
                     "stopped once the migration of the file is "
                     "complete.\nPlease check rebalance process "
                     "for completion before doing any further "
                     "brick related tasks on the volume.\n%s",
                     rsp.op_errstr);
        }
    }
    if (cmd == GF_DEFRAG_CMD_STATUS) {
        if (rsp.op_ret == -1) {
            if (strcmp(rsp.op_errstr, ""))
                snprintf(msg, sizeof(msg), "%s", rsp.op_errstr);
            else
                snprintf(msg, sizeof(msg),
                         "Failed to get the status of rebalance process");
            goto done;
        } else {
            snprintf(msg, sizeof(msg), "%s", rsp.op_errstr);
        }
    }

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_vol_rebalance(cmd, dict, rsp.op_ret, rsp.op_errno,
                                           rsp.op_errstr);
        goto out;
    }

    ret = gf_cli_print_rebalance_status(dict, GF_TASK_TYPE_REBALANCE);
    if (ret)
        gf_log("cli", GF_LOG_ERROR, "Failed to print rebalance status");

done:
    if (global_state->mode & GLUSTER_MODE_XML)
        cli_xml_output_str("volRebalance", msg, rsp.op_ret, rsp.op_errno,
                           rsp.op_errstr);
    else {
        if (rsp.op_ret)
            cli_err("volume rebalance: %s: failed%s%s", volname,
                    strlen(msg) ? ": " : "", msg);
        else
            cli_out("volume rebalance: %s: success%s%s", volname,
                    strlen(msg) ? ": " : "", msg);
    }
    ret = rsp.op_ret;

out:
    gf_free_xdr_cli_rsp(rsp);
    if (dict)
        dict_unref(dict);
    cli_cmd_broadcast_response(ret);
    return ret;
}

static int
gf_cli_rename_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    char msg[1024] = {
        0,
    };

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to probe");
    snprintf(msg, sizeof(msg), "Rename volume %s",
             (rsp.op_ret) ? "unsuccessful" : "successful");

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_str("volRename", msg, rsp.op_ret, rsp.op_errno,
                                 rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (rsp.op_ret)
        cli_err("volume rename: failed");
    else
        cli_out("volume rename: success");

    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int
gf_cli_reset_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    char msg[1024] = {
        0,
    };

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to reset");

    if (strcmp(rsp.op_errstr, ""))
        snprintf(msg, sizeof(msg), "%s", rsp.op_errstr);
    else
        snprintf(msg, sizeof(msg), "reset volume %s",
                 (rsp.op_ret) ? "unsuccessful" : "successful");

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_str("volReset", msg, rsp.op_ret, rsp.op_errno,
                                 rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (rsp.op_ret)
        cli_err("volume reset: failed: %s", msg);
    else
        cli_out("volume reset: success: %s", msg);

    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int
gf_cli_ganesha_cbk(struct rpc_req *req, struct iovec *iov, int count,
                   void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    dict_t *dict = NULL;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    gf_log("cli", GF_LOG_DEBUG, "Received resp to ganesha");

    dict = dict_new();

    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);
    if (ret)
        goto out;

    if (rsp.op_ret) {
        if (strcmp(rsp.op_errstr, ""))
            cli_err("nfs-ganesha: failed: %s", rsp.op_errstr);
        else
            cli_err("nfs-ganesha: failed");
    }

    else {
        cli_out("nfs-ganesha : success ");
    }

    ret = rsp.op_ret;

out:
    if (dict)
        dict_unref(dict);
    cli_cmd_broadcast_response(ret);
    return ret;
}

static char *
is_server_debug_xlator(void *myframe)
{
    call_frame_t *frame = NULL;
    cli_local_t *local = NULL;
    char **words = NULL;
    char *key = NULL;
    char *value = NULL;
    char *debug_xlator = NULL;

    frame = myframe;
    local = frame->local;
    words = (char **)local->words;

    while (*words != NULL) {
        if (strstr(*words, "trace") == NULL &&
            strstr(*words, "error-gen") == NULL) {
            words++;
            continue;
        }

        key = *words;
        words++;
        value = *words;
        if (value == NULL)
            break;
        if (strstr(value, "client")) {
            words++;
            continue;
        } else {
            if (!(strstr(value, "posix") || strstr(value, "acl") ||
                  strstr(value, "locks") || strstr(value, "io-threads") ||
                  strstr(value, "marker") || strstr(value, "index"))) {
                words++;
                continue;
            } else {
                debug_xlator = gf_strdup(key);
                break;
            }
        }
    }

    return debug_xlator;
}

static int
gf_cli_set_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    dict_t *dict = NULL;
    char *help_str = NULL;
    char msg[1024] = {
        0,
    };
    char *debug_xlator = NULL;
    char tmp_str[512] = {
        0,
    };

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to set");

    dict = dict_new();

    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
        goto out;
    }

    /* For brick processes graph change does not happen on the fly.
     * The process has to be restarted. So this is a check from the
     * volume set option such that if debug xlators such as trace/errorgen
     * are provided in the set command, warn the user.
     */
    debug_xlator = is_server_debug_xlator(myframe);

    if (dict_get_str_sizen(dict, "help-str", &help_str) && !msg[0])
        snprintf(msg, sizeof(msg), "Set volume %s",
                 (rsp.op_ret) ? "unsuccessful" : "successful");
    if (rsp.op_ret == 0 && debug_xlator) {
        snprintf(tmp_str, sizeof(tmp_str),
                 "\n%s translator has been "
                 "added to the server volume file. Please restart the"
                 " volume for enabling the translator",
                 debug_xlator);
    }

    if ((global_state->mode & GLUSTER_MODE_XML) && (help_str == NULL)) {
        ret = cli_xml_output_str("volSet", msg, rsp.op_ret, rsp.op_errno,
                                 rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (rsp.op_ret) {
        if (strcmp(rsp.op_errstr, ""))
            cli_err("volume set: failed: %s", rsp.op_errstr);
        else
            cli_err("volume set: failed");
    } else {
        if (help_str == NULL) {
            if (debug_xlator == NULL)
                cli_out("volume set: success");
            else
                cli_out("volume set: success%s", tmp_str);
        } else {
            cli_out("%s", help_str);
        }
    }

    ret = rsp.op_ret;

out:
    if (dict)
        dict_unref(dict);
    GF_FREE(debug_xlator);
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

int
gf_cli_add_brick_cbk(struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    char msg[1024] = {
        0,
    };

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to add brick");

    if (rsp.op_ret && strcmp(rsp.op_errstr, ""))
        snprintf(msg, sizeof(msg), "%s", rsp.op_errstr);
    else
        snprintf(msg, sizeof(msg), "Add Brick %s",
                 (rsp.op_ret) ? "unsuccessful" : "successful");

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_str("volAddBrick", msg, rsp.op_ret, rsp.op_errno,
                                 rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (rsp.op_ret)
        cli_err("volume add-brick: failed: %s", rsp.op_errstr);
    else
        cli_out("volume add-brick: success");
    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int
gf_cli3_remove_brick_status_cbk(struct rpc_req *req, struct iovec *iov,
                                int count, void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    dict_t *dict = NULL;
    char msg[1024] = {
        0,
    };
    int32_t command = 0;
    gf1_op_commands cmd = GF_OP_CMD_NONE;
    cli_local_t *local = NULL;
    call_frame_t *frame = NULL;
    const char *cmd_str;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    frame = myframe;

    GF_ASSERT(frame->local);

    local = frame->local;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        goto out;
    }

    ret = dict_get_int32_sizen(local->dict, "command", &command);
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
            cmd_str = "unknown";
            break;
    }

    ret = rsp.op_ret;
    if (rsp.op_ret == -1) {
        if (strcmp(rsp.op_errstr, ""))
            snprintf(msg, sizeof(msg), "volume remove-brick %s: failed: %s",
                     cmd_str, rsp.op_errstr);
        else
            snprintf(msg, sizeof(msg), "volume remove-brick %s: failed",
                     cmd_str);

        if (global_state->mode & GLUSTER_MODE_XML)
            goto xml_output;

        cli_err("%s", msg);
        goto out;
    }

    if (rsp.dict.dict_len) {
        /* Unserialize the dictionary */
        dict = dict_new();

        ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);
        if (ret < 0) {
            strncpy(msg, DICT_UNSERIALIZE_FAIL, sizeof(msg));

            if (global_state->mode & GLUSTER_MODE_XML) {
                rsp.op_ret = -1;
                goto xml_output;
            }

            gf_log("cli", GF_LOG_ERROR, "%s", msg);
            goto out;
        }
    }

xml_output:
    if (global_state->mode & GLUSTER_MODE_XML) {
        if (strcmp(rsp.op_errstr, "")) {
            ret = cli_xml_output_vol_remove_brick(_gf_true, dict, rsp.op_ret,
                                                  rsp.op_errno, rsp.op_errstr,
                                                  "volRemoveBrick");
        } else {
            ret = cli_xml_output_vol_remove_brick(_gf_true, dict, rsp.op_ret,
                                                  rsp.op_errno, msg,
                                                  "volRemoveBrick");
        }
        goto out;
    }

    ret = gf_cli_print_rebalance_status(dict, GF_TASK_TYPE_REMOVE_BRICK);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR,
               "Failed to print remove-brick rebalance status");
        goto out;
    }

    if ((cmd == GF_OP_CMD_STOP) && (rsp.op_ret == 0)) {
        cli_out(
            "'remove-brick' process may be in the middle of a "
            "file migration.\nThe process will be fully stopped "
            "once the migration of the file is complete.\nPlease "
            "check remove-brick process for completion before "
            "doing any further brick related tasks on the "
            "volume.");
    }

out:
    if (dict)
        dict_unref(dict);
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int
gf_cli_remove_brick_cbk(struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    char msg[1024] = {
        0,
    };
    gf1_op_commands cmd = GF_OP_CMD_NONE;
    char *cmd_str = "unknown";
    cli_local_t *local = NULL;
    call_frame_t *frame = NULL;
    char *task_id_str = NULL;
    dict_t *rsp_dict = NULL;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    frame = myframe;

    GF_ASSERT(frame->local);

    local = frame->local;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        goto out;
    }

    ret = dict_get_int32_sizen(local->dict, "command", (int32_t *)&cmd);
    if (ret) {
        gf_log("", GF_LOG_ERROR, "failed to get command");
        goto out;
    }

    if (rsp.dict.dict_len) {
        rsp_dict = dict_new();
        if (!rsp_dict) {
            ret = -1;
            goto out;
        }

        ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &rsp_dict);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
            goto out;
        }
    }

    switch (cmd) {
        case GF_OP_CMD_DETACH_START:
        case GF_OP_CMD_START:
            cmd_str = "start";

            ret = dict_get_str_sizen(rsp_dict, GF_REMOVE_BRICK_TID_KEY,
                                     &task_id_str);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR,
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

    gf_log("cli", GF_LOG_INFO, "Received resp to remove brick");

    if (rsp.op_ret && strcmp(rsp.op_errstr, ""))
        snprintf(msg, sizeof(msg), "%s", rsp.op_errstr);
    else
        snprintf(msg, sizeof(msg), "Remove Brick %s %s", cmd_str,
                 (rsp.op_ret) ? "unsuccessful" : "successful");

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_vol_remove_brick(_gf_false, rsp_dict, rsp.op_ret,
                                              rsp.op_errno, msg,
                                              "volRemoveBrick");
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (rsp.op_ret) {
        cli_err("volume remove-brick %s: failed: %s", cmd_str, msg);
    } else {
        cli_out("volume remove-brick %s: success", cmd_str);
        if (GF_OP_CMD_START == cmd && task_id_str != NULL)
            cli_out("ID: %s", task_id_str);
        if (GF_OP_CMD_COMMIT == cmd)
            cli_out(
                "Check the removed bricks to ensure all files "
                "are migrated.\nIf files with data are "
                "found on the brick path, copy them via a "
                "gluster mount point before re-purposing the "
                "removed brick. ");
    }

    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);

    if (rsp_dict)
        dict_unref(rsp_dict);

    return ret;
}

static int
gf_cli_reset_brick_cbk(struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    cli_local_t *local = NULL;
    call_frame_t *frame = NULL;
    const char *rb_operation_str = NULL;
    dict_t *rsp_dict = NULL;
    char msg[1024] = {
        0,
    };
    char *reset_op = NULL;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    frame = myframe;

    GF_ASSERT(frame->local);

    local = frame->local;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        goto out;
    }

    ret = dict_get_str_sizen(local->dict, "operation", &reset_op);
    if (ret) {
        gf_log(frame->this->name, GF_LOG_ERROR, "dict_get on operation failed");
        goto out;
    }

    if (strcmp(reset_op, "GF_RESET_OP_START") &&
        strcmp(reset_op, "GF_RESET_OP_COMMIT") &&
        strcmp(reset_op, "GF_RESET_OP_COMMIT_FORCE")) {
        ret = -1;
        goto out;
    }

    if (rsp.dict.dict_len) {
        /* Unserialize the dictionary */
        rsp_dict = dict_new();

        ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &rsp_dict);
        if (ret < 0) {
            gf_log(frame->this->name, GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
            goto out;
        }
    }

    if (rsp.op_ret && (strcmp(rsp.op_errstr, ""))) {
        rb_operation_str = rsp.op_errstr;
    } else {
        if (!strcmp(reset_op, "GF_RESET_OP_START")) {
            if (rsp.op_ret)
                rb_operation_str = "reset-brick start operation failed";
            else
                rb_operation_str = "reset-brick start operation successful";
        } else if (!strcmp(reset_op, "GF_RESET_OP_COMMIT")) {
            if (rsp.op_ret)
                rb_operation_str = "reset-brick commit operation failed";
            else
                rb_operation_str = "reset-brick commit operation successful";
        } else if (!strcmp(reset_op, "GF_RESET_OP_COMMIT_FORCE")) {
            if (rsp.op_ret)
                rb_operation_str = "reset-brick commit force operation failed";
            else
                rb_operation_str =
                    "reset-brick commit force operation successful";
        }
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to reset brick");
    snprintf(msg, sizeof(msg), "%s",
             rb_operation_str ? rb_operation_str : "Unknown operation");

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_vol_replace_brick(rsp_dict, rsp.op_ret,
                                               rsp.op_errno, msg);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (rsp.op_ret)
        cli_err("volume reset-brick: failed: %s", msg);
    else
        cli_out("volume reset-brick: success: %s", msg);
    ret = rsp.op_ret;

out:
    if (frame)
        frame->local = NULL;

    if (local)
        cli_local_wipe(local);

    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    if (rsp_dict)
        dict_unref(rsp_dict);

    return ret;
}
static int
gf_cli_replace_brick_cbk(struct rpc_req *req, struct iovec *iov, int count,
                         void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    cli_local_t *local = NULL;
    call_frame_t *frame = NULL;
    const char *rb_operation_str = NULL;
    dict_t *rsp_dict = NULL;
    char msg[1024] = {
        0,
    };
    char *replace_op = NULL;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    frame = myframe;

    GF_ASSERT(frame->local);

    local = frame->local;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        goto out;
    }

    ret = dict_get_str_sizen(local->dict, "operation", &replace_op);
    if (ret) {
        gf_log(frame->this->name, GF_LOG_ERROR, "dict_get on operation failed");
        goto out;
    }

    if (rsp.dict.dict_len) {
        /* Unserialize the dictionary */
        rsp_dict = dict_new();

        ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &rsp_dict);
        if (ret < 0) {
            gf_log(frame->this->name, GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
            goto out;
        }
    }

    if (!strcmp(replace_op, "GF_REPLACE_OP_COMMIT_FORCE")) {
        if (rsp.op_ret || ret)
            rb_operation_str = "replace-brick commit force operation failed";
        else
            rb_operation_str =
                "replace-brick commit force operation successful";
    } else {
        gf_log(frame->this->name, GF_LOG_DEBUG, "Unknown operation");
    }

    if (rsp.op_ret && (strcmp(rsp.op_errstr, ""))) {
        rb_operation_str = rsp.op_errstr;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to replace brick");
    snprintf(msg, sizeof(msg), "%s",
             rb_operation_str ? rb_operation_str : "Unknown operation");

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_vol_replace_brick(rsp_dict, rsp.op_ret,
                                               rsp.op_errno, msg);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (rsp.op_ret)
        cli_err("volume replace-brick: failed: %s", msg);
    else
        cli_out("volume replace-brick: success: %s", msg);
    ret = rsp.op_ret;

out:
    if (frame)
        frame->local = NULL;

    if (local)
        cli_local_wipe(local);

    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    if (rsp_dict)
        dict_unref(rsp_dict);

    return ret;
}

static int
gf_cli_log_rotate_cbk(struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    char msg[1024] = {
        0,
    };

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    gf_log("cli", GF_LOG_DEBUG, "Received resp to log rotate");

    if (rsp.op_ret && strcmp(rsp.op_errstr, ""))
        snprintf(msg, sizeof(msg), "%s", rsp.op_errstr);
    else
        snprintf(msg, sizeof(msg), "log rotate %s",
                 (rsp.op_ret) ? "unsuccessful" : "successful");

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_str("volLogRotate", msg, rsp.op_ret, rsp.op_errno,
                                 rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (rsp.op_ret)
        cli_err("volume log-rotate: failed: %s", msg);
    else
        cli_out("volume log-rotate: success");
    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);

    return ret;
}

static int
gf_cli_sync_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    char msg[1024] = {
        0,
    };

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    gf_log("cli", GF_LOG_DEBUG, "Received resp to sync");

    if (rsp.op_ret && strcmp(rsp.op_errstr, ""))
        snprintf(msg, sizeof(msg), "volume sync: failed: %s", rsp.op_errstr);
    else
        snprintf(msg, sizeof(msg), "volume sync: %s",
                 (rsp.op_ret) ? "failed" : "success");

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_str("volSync", msg, rsp.op_ret, rsp.op_errno,
                                 rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (rsp.op_ret)
        cli_err("%s", msg);
    else
        cli_out("%s", msg);
    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int
print_quota_list_usage_output(cli_local_t *local, char *path, int64_t avail,
                              char *sl_str, quota_limits_t *limits,
                              quota_meta_t *used_space, gf_boolean_t sl,
                              gf_boolean_t hl, double sl_num,
                              gf_boolean_t limit_set)
{
    int32_t ret = -1;
    char *used_str = NULL;
    char *avail_str = NULL;
    char *hl_str = NULL;
    char *sl_val = NULL;

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_quota_xml_output(local, path, limits->hl, sl_str, sl_num,
                                   used_space->size, avail, sl ? "Yes" : "No",
                                   hl ? "Yes" : "No", limit_set);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "Failed to output in xml format for quota list command");
        }
        goto out;
    }

    used_str = gf_uint64_2human_readable(used_space->size);

    if (limit_set) {
        hl_str = gf_uint64_2human_readable(limits->hl);
        sl_val = gf_uint64_2human_readable(sl_num);

        if (!used_str) {
            cli_out("%-40s %7s %7s(%s) %8" PRIu64 "%9" PRIu64
                    ""
                    "%15s %18s",
                    path, hl_str, sl_str, sl_val, used_space->size, avail,
                    sl ? "Yes" : "No", hl ? "Yes" : "No");
        } else {
            avail_str = gf_uint64_2human_readable(avail);
            cli_out("%-40s %7s %7s(%s) %8s %7s %15s %20s", path, hl_str, sl_str,
                    sl_val, used_str, avail_str, sl ? "Yes" : "No",
                    hl ? "Yes" : "No");
        }
    } else {
        cli_out("%-36s %10s %10s %14s %9s %15s %18s", path, "N/A", "N/A",
                used_str, "N/A", "N/A", "N/A");
    }

    ret = 0;
out:
    GF_FREE(hl_str);
    GF_FREE(used_str);
    GF_FREE(avail_str);
    GF_FREE(sl_val);

    return ret;
}

static int
print_quota_list_object_output(cli_local_t *local, char *path, int64_t avail,
                               char *sl_str, quota_limits_t *limits,
                               quota_meta_t *used_space, gf_boolean_t sl,
                               gf_boolean_t hl, double sl_num,
                               gf_boolean_t limit_set)
{
    int32_t ret = -1;
    int64_t sl_val = sl_num;

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_quota_object_xml_output(local, path, sl_str, sl_val, limits,
                                          used_space, avail, sl ? "Yes" : "No",
                                          hl ? "Yes" : "No", limit_set);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "Failed to output in xml format for quota list command");
        }
        goto out;
    }

    if (limit_set) {
        cli_out("%-40s %9" PRIu64 " %9s(%" PRId64 ") %10" PRIu64
                ""
                "%10" PRIu64 " %11" PRIu64 " %15s %20s",
                path, limits->hl, sl_str, sl_val, used_space->file_count,
                used_space->dir_count, avail, sl ? "Yes" : "No",
                hl ? "Yes" : "No");
    } else {
        cli_out("%-40s %9s %9s %10" PRIu64 " %10" PRIu64 " %11s %15s %20s",
                path, "N/A", "N/A", used_space->file_count,
                used_space->dir_count, "N/A", "N/A", "N/A");
    }
    ret = 0;

out:

    return ret;
}

static int
print_quota_list_output(cli_local_t *local, char *path, char *default_sl,
                        quota_limits_t *limits, quota_meta_t *used_space,
                        int type, gf_boolean_t limit_set)
{
    int64_t avail = 0;
    char percent_str[20] = {0};
    char *sl_final = NULL;
    int ret = -1;
    double sl_num = 0;
    gf_boolean_t sl = _gf_false;
    gf_boolean_t hl = _gf_false;
    int64_t used_size = 0;

    GF_ASSERT(local);
    GF_ASSERT(path);

    if (limit_set) {
        if (limits->sl < 0) {
            ret = gf_string2percent(default_sl, &sl_num);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR,
                       "could not convert default soft limit to percent");
                goto out;
            }
            sl_num = (sl_num * limits->hl) / 100;
            sl_final = default_sl;
        } else {
            sl_num = (limits->sl * limits->hl) / 100;
            ret = snprintf(percent_str, sizeof(percent_str), "%" PRIu64 "%%",
                           limits->sl);
            if (ret < 0)
                goto out;
            sl_final = percent_str;
        }
        if (type == GF_QUOTA_OPTION_TYPE_LIST)
            used_size = used_space->size;
        else
            used_size = used_space->file_count + used_space->dir_count;

        if (limits->hl > used_size) {
            avail = limits->hl - used_size;
            hl = _gf_false;
            if (used_size > sl_num)
                sl = _gf_true;
            else
                sl = _gf_false;
        } else {
            avail = 0;
            hl = sl = _gf_true;
        }
    }

    if (type == GF_QUOTA_OPTION_TYPE_LIST)
        ret = print_quota_list_usage_output(local, path, avail, sl_final,
                                            limits, used_space, sl, hl, sl_num,
                                            limit_set);
    else
        ret = print_quota_list_object_output(local, path, avail, sl_final,
                                             limits, used_space, sl, hl, sl_num,
                                             limit_set);
out:
    return ret;
}

static int
print_quota_list_from_mountdir(cli_local_t *local, char *mountdir,
                               char *default_sl, char *path, int type)
{
    int ret = -1;
    ssize_t xattr_size = 0;
    quota_limits_t limits = {
        0,
    };
    quota_meta_t used_space = {
        0,
    };
    char *key = NULL;
    gf_boolean_t limit_set = _gf_true;

    GF_ASSERT(local);
    GF_ASSERT(mountdir);
    GF_ASSERT(path);

    if (type == GF_QUOTA_OPTION_TYPE_LIST)
        key = QUOTA_LIMIT_KEY;
    else
        key = QUOTA_LIMIT_OBJECTS_KEY;

    ret = sys_lgetxattr(mountdir, key, (void *)&limits, sizeof(limits));
    if (ret < 0) {
        gf_log("cli", GF_LOG_ERROR,
               "Failed to get the xattr %s on %s. Reason : %s", key, mountdir,
               strerror(errno));

        switch (errno) {
#if defined(ENODATA)
            case ENODATA:
#endif
#if defined(ENOATTR) && (ENOATTR != ENODATA)
            case ENOATTR:
#endif
                /* If it's an ENOATTR, quota/inode-quota is
                 * configured(limit is set at least for one directory).
                 * The user is trying to issue 'list/list-objects'
                 * command for a directory on which quota limit is
                 * not set and we are showing the used-space in case
                 * of list-usage and showing (dir_count, file_count)
                 * in case of list-objects. Other labels are
                 * shown "N/A".
                 */

                limit_set = _gf_false;
                goto enoattr;
                break;

            default:
                cli_err("%-40s %s", path, strerror(errno));
                break;
        }

        goto out;
    }

    limits.hl = ntoh64(limits.hl);
    limits.sl = ntoh64(limits.sl);

enoattr:
    xattr_size = sys_lgetxattr(mountdir, QUOTA_SIZE_KEY, NULL, 0);
    if (xattr_size < (sizeof(int64_t) * 2) &&
        type == GF_QUOTA_OPTION_TYPE_LIST_OBJECTS) {
        ret = -1;

        /* This can happen when glusterfs is upgraded from 3.6 to 3.7
         * and the xattr healing is not completed.
         */
    } else if (xattr_size > (sizeof(int64_t) * 2)) {
        ret = sys_lgetxattr(mountdir, QUOTA_SIZE_KEY, &used_space,
                            sizeof(used_space));
    } else if (xattr_size > 0) {
        /* This is for compatibility.
         * Older version had only file usage
         */
        ret = sys_lgetxattr(mountdir, QUOTA_SIZE_KEY, &(used_space.size),
                            sizeof(used_space.size));
        used_space.file_count = 0;
        used_space.dir_count = 0;
    } else {
        ret = -1;
    }

    if (ret < 0) {
        gf_log("cli", GF_LOG_ERROR, "Failed to get quota size on path %s: %s",
               mountdir, strerror(errno));
        print_quota_list_empty(path, type);
        goto out;
    }

    used_space.size = ntoh64(used_space.size);
    used_space.file_count = ntoh64(used_space.file_count);
    used_space.dir_count = ntoh64(used_space.dir_count);

    ret = print_quota_list_output(local, path, default_sl, &limits, &used_space,
                                  type, limit_set);
out:
    return ret;
}

static int
gluster_remove_auxiliary_mount(char *volname)
{
    int ret = -1;
    char mountdir[PATH_MAX] = {
        0,
    };
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);

    GLUSTERD_GET_QUOTA_LIST_MOUNT_PATH(mountdir, volname, "/");
    ret = gf_umount_lazy(this->name, mountdir, 1);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "umount on %s failed, reason : %s",
               mountdir, strerror(errno));
    }

    return ret;
}

static int
gf_cli_print_limit_list_from_dict(cli_local_t *local, char *volname,
                                  dict_t *dict, char *default_sl, int count,
                                  int op_ret, int op_errno, char *op_errstr)
{
    int ret = -1;
    int i = 0;
    char key[32] = {
        0,
    };
    int keylen;
    char mountdir[PATH_MAX] = {
        0,
    };
    char *path = NULL;
    int type = -1;

    if (!dict || count <= 0)
        goto out;

    ret = dict_get_int32_sizen(dict, "type", &type);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Failed to get quota type");
        goto out;
    }

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_vol_quota_limit_list_begin(local, op_ret, op_errno,
                                                        op_errstr);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
            goto out;
        }
    } else {
        print_quota_list_header(type);
    }

    while (count--) {
        keylen = snprintf(key, sizeof(key), "path%d", i++);

        ret = dict_get_strn(dict, key, keylen, &path);
        if (ret < 0) {
            gf_log("cli", GF_LOG_DEBUG, "Path not present in limit list");
            continue;
        }

        ret = gf_canonicalize_path(path);
        if (ret)
            goto out;
        GLUSTERD_GET_QUOTA_LIST_MOUNT_PATH(mountdir, volname, path);
        ret = print_quota_list_from_mountdir(local, mountdir, default_sl, path,
                                             type);
    }

out:
    return ret;
}

static int
print_quota_list_from_quotad(call_frame_t *frame, dict_t *rsp_dict)
{
    char *path = NULL;
    char *default_sl = NULL;
    int ret = -1;
    cli_local_t *local = NULL;
    dict_t *gd_rsp_dict = NULL;
    quota_meta_t used_space = {
        0,
    };
    quota_limits_t limits = {
        0,
    };
    quota_limits_t *size_limits = NULL;
    int32_t type = 0;
    int32_t success_count = 0;

    GF_ASSERT(frame);

    local = frame->local;
    gd_rsp_dict = local->dict;

    ret = dict_get_int32_sizen(rsp_dict, "type", &type);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Failed to get type");
        goto out;
    }

    ret = dict_get_str_sizen(rsp_dict, GET_ANCESTRY_PATH_KEY, &path);
    if (ret) {
        gf_log("cli", GF_LOG_WARNING, "path key is not present in dict");
        goto out;
    }

    ret = dict_get_str_sizen(gd_rsp_dict, "default-soft-limit", &default_sl);
    if (ret) {
        gf_log(frame->this->name, GF_LOG_ERROR,
               "failed to get default soft limit");
        goto out;
    }

    if (type == GF_QUOTA_OPTION_TYPE_LIST) {
        ret = dict_get_bin(rsp_dict, QUOTA_LIMIT_KEY, (void **)&size_limits);
        if (ret) {
            gf_log("cli", GF_LOG_WARNING, "limit key not present in dict on %s",
                   path);
            goto out;
        }
    } else {
        ret = dict_get_bin(rsp_dict, QUOTA_LIMIT_OBJECTS_KEY,
                           (void **)&size_limits);
        if (ret) {
            gf_log("cli", GF_LOG_WARNING,
                   "object limit key not present in dict on %s", path);
            goto out;
        }
    }

    limits.hl = ntoh64(size_limits->hl);
    limits.sl = ntoh64(size_limits->sl);

    if (type == GF_QUOTA_OPTION_TYPE_LIST)
        ret = quota_dict_get_meta(rsp_dict, QUOTA_SIZE_KEY,
                                  SLEN(QUOTA_SIZE_KEY), &used_space);
    else
        ret = quota_dict_get_inode_meta(rsp_dict, QUOTA_SIZE_KEY,
                                        SLEN(QUOTA_SIZE_KEY), &used_space);

    if (ret < 0) {
        gf_log("cli", GF_LOG_WARNING, "size key not present in dict");
        print_quota_list_empty(path, type);
        goto out;
    }

    LOCK(&local->lock);
    {
        ret = dict_get_int32_sizen(gd_rsp_dict, "quota-list-success-count",
                                   &success_count);
        if (ret)
            success_count = 0;

        ret = dict_set_int32_sizen(gd_rsp_dict, "quota-list-success-count",
                                   success_count + 1);
    }
    UNLOCK(&local->lock);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR,
               "Failed to set quota-list-success-count in dict");
        goto out;
    }

    if (success_count == 0) {
        if (!(global_state->mode & GLUSTER_MODE_XML)) {
            print_quota_list_header(type);
        } else {
            ret = cli_xml_output_vol_quota_limit_list_begin(local, 0, 0, NULL);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, XML_ERROR);
                goto out;
            }
        }
    }

    ret = print_quota_list_output(local, path, default_sl, &limits, &used_space,
                                  type, _gf_true);
out:
    return ret;
}

static void *
cli_cmd_broadcast_response_detached(void *opaque)
{
    int32_t ret = 0;

    ret = (intptr_t)opaque;
    cli_cmd_broadcast_response(ret);

    return NULL;
}

static int32_t
cli_quota_compare_path(struct list_head *list1, struct list_head *list2)
{
    struct list_node *node1 = NULL;
    struct list_node *node2 = NULL;
    dict_t *dict1 = NULL;
    dict_t *dict2 = NULL;
    char *path1 = NULL;
    char *path2 = NULL;
    int ret = 0;

    node1 = list_entry(list1, struct list_node, list);
    node2 = list_entry(list2, struct list_node, list);

    dict1 = node1->ptr;
    dict2 = node2->ptr;

    ret = dict_get_str_sizen(dict1, GET_ANCESTRY_PATH_KEY, &path1);
    if (ret < 0)
        return 0;

    ret = dict_get_str_sizen(dict2, GET_ANCESTRY_PATH_KEY, &path2);
    if (ret < 0)
        return 0;

    return strcmp(path1, path2);
}

static int
cli_quotad_getlimit_cbk(struct rpc_req *req, struct iovec *iov, int count,
                        void *myframe)
{
    /*TODO: we need to gather the path, hard-limit, soft-limit and used space*/
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    dict_t *dict = NULL;
    struct list_node *node = NULL;
    struct list_node *tmpnode = NULL;
    call_frame_t *frame = NULL;
    cli_local_t *local = NULL;
    int32_t list_count = 0;
    pthread_t th_id = {
        0,
    };
    int32_t max_count = 0;

    GF_ASSERT(myframe);

    frame = myframe;

    GF_ASSERT(frame->local);

    local = frame->local;

    LOCK(&local->lock);
    {
        ret = dict_get_int32_sizen(local->dict, "quota-list-count",
                                   &list_count);
        if (ret)
            list_count = 0;

        list_count++;
        ret = dict_set_int32_sizen(local->dict, "quota-list-count", list_count);
    }
    UNLOCK(&local->lock);

    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Failed to set quota-list-count in dict");
        goto out;
    }

    if (-1 == req->rpc_status) {
        if (list_count == 0)
            cli_err(
                "Connection failed. Please check if quota "
                "daemon is operational.");
        ret = -1;
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        goto out;
    }

    if (rsp.op_ret) {
        ret = -1;
        if (strcmp(rsp.op_errstr, ""))
            cli_err("quota command failed : %s", rsp.op_errstr);
        else
            cli_err("quota command : failed");
        goto out;
    }

    if (rsp.dict.dict_len) {
        /* Unserialize the dictionary */
        dict = dict_new();

        ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);
        if (ret < 0) {
            gf_log("cli", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
            goto out;
        }

        node = list_node_add_order(dict, &local->dict_list,
                                   cli_quota_compare_path);
        if (node == NULL) {
            gf_log("cli", GF_LOG_ERROR, "failed to add node to the list");
            dict_unref(dict);
            ret = -1;
            goto out;
        }
    }

    ret = dict_get_int32_sizen(local->dict, "max_count", &max_count);
    if (ret < 0) {
        gf_log("cli", GF_LOG_ERROR, "failed to get max_count");
        goto out;
    }

    if (list_count == max_count) {
        list_for_each_entry_safe(node, tmpnode, &local->dict_list, list)
        {
            dict = node->ptr;
            print_quota_list_from_quotad(frame, dict);
            list_node_del(node);
            dict_unref(dict);
        }
    }

out:
    /* Bad Fix: CLI holds the lock to process a command.
     * When processing quota list command, below sequence of steps executed
     * in the same thread and causing deadlock
     *
     * 1) CLI holds the lock
     * 2) Send rpc_clnt_submit request to quotad for quota usage
     * 3) If quotad is down, rpc_clnt_submit invokes cbk function with error
     * 4) cbk function cli_quotad_getlimit_cbk invokes
     *    cli_cmd_broadcast_response which tries to hold lock to broadcast
     *    the results and hangs, because same thread has already holding
     *    the lock
     *
     * Broadcasting response in a separate thread which is not a
     * good fix. This needs to be re-visted with better solution
     */
    if (ret == -1) {
        ret = pthread_create(&th_id, NULL, cli_cmd_broadcast_response_detached,
                             (void *)-1);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, "pthread_create failed: %s",
                   strerror(errno));
    } else {
        cli_cmd_broadcast_response(ret);
    }
    gf_free_xdr_cli_rsp(rsp);

    return ret;
}

static int
cli_quotad_getlimit(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;

    if (!frame || !this || !data) {
        ret = -1;
        goto out;
    }

    dict = data;
    ret = add_cli_cmd_timeout_to_dict(dict);

    ret = dict_allocate_and_serialize(dict, &req.dict.dict_val,
                                      &req.dict.dict_len);
    if (ret < 0) {
        gf_log(this->name, GF_LOG_ERROR, DICT_SERIALIZE_FAIL);

        goto out;
    }

    ret = cli_cmd_submit(global_quotad_rpc, &req, frame, &cli_quotad_clnt,
                         GF_AGGREGATOR_GETLIMIT, NULL, this,
                         cli_quotad_getlimit_cbk, (xdrproc_t)xdr_gf_cli_req);

out:
    GF_FREE(req.dict.dict_val);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    return ret;
}

static void
gf_cli_quota_list(cli_local_t *local, char *volname, dict_t *dict,
                  char *default_sl, int count, int op_ret, int op_errno,
                  char *op_errstr)
{
    if (!cli_cmd_connected())
        goto out;

    if (count > 0) {
        GF_VALIDATE_OR_GOTO("cli", volname, out);

        gf_cli_print_limit_list_from_dict(local, volname, dict, default_sl,
                                          count, op_ret, op_errno, op_errstr);
    }
out:
    return;
}

static int
gf_cli_quota_cbk(struct rpc_req *req, struct iovec *iov, int count,
                 void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    dict_t *dict = NULL;
    char *volname = NULL;
    int32_t type = 0;
    call_frame_t *frame = NULL;
    char *default_sl = NULL;
    cli_local_t *local = NULL;
    char *default_sl_dup = NULL;
    int32_t entry_count = 0;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    frame = myframe;

    GF_ASSERT(frame->local);

    local = frame->local;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        goto out;
    }

    if (rsp.op_ret) {
        ret = -1;
        if (global_state->mode & GLUSTER_MODE_XML)
            goto xml_output;

        if (strcmp(rsp.op_errstr, "")) {
            cli_err("quota command failed : %s", rsp.op_errstr);
            if (rsp.op_ret == -ENOENT)
                cli_err("please enter the path relative to the volume");
        } else {
            cli_err("quota command : failed");
        }

        goto out;
    }

    if (rsp.dict.dict_len) {
        /* Unserialize the dictionary */
        dict = dict_new();

        ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);
        if (ret < 0) {
            gf_log("cli", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
            goto out;
        }
    }

    gf_log("cli", GF_LOG_DEBUG, "Received resp to quota command");

    ret = dict_get_str_sizen(dict, "default-soft-limit", &default_sl);
    if (ret)
        gf_log(frame->this->name, GF_LOG_TRACE,
               "failed to get default soft limit");

    // default-soft-limit is part of rsp_dict only iff we sent
    // GLUSTER_CLI_QUOTA with type being GF_QUOTA_OPTION_TYPE_LIST
    if (default_sl) {
        default_sl_dup = gf_strdup(default_sl);
        if (!default_sl_dup) {
            ret = -1;
            goto out;
        }
        ret = dict_set_dynstr_sizen(local->dict, "default-soft-limit",
                                    default_sl_dup);
        if (ret) {
            gf_log(frame->this->name, GF_LOG_TRACE,
                   "failed to set default soft limit");
            GF_FREE(default_sl_dup);
        }
    }

    ret = dict_get_str_sizen(dict, "volname", &volname);
    if (ret)
        gf_log(frame->this->name, GF_LOG_ERROR, "failed to get volname");

    ret = dict_get_int32_sizen(dict, "type", &type);
    if (ret)
        gf_log(frame->this->name, GF_LOG_TRACE, "failed to get type");

    ret = dict_get_int32_sizen(dict, "count", &entry_count);
    if (ret)
        gf_log(frame->this->name, GF_LOG_TRACE, "failed to get count");

    if ((type == GF_QUOTA_OPTION_TYPE_LIST) ||
        (type == GF_QUOTA_OPTION_TYPE_LIST_OBJECTS)) {
        gf_cli_quota_list(local, volname, dict, default_sl, entry_count,
                          rsp.op_ret, rsp.op_errno, rsp.op_errstr);

        if (global_state->mode & GLUSTER_MODE_XML) {
            ret = cli_xml_output_vol_quota_limit_list_end(local);
            if (ret < 0) {
                ret = -1;
                gf_log("cli", GF_LOG_ERROR, XML_ERROR);
            }
            goto out;
        }
    }

xml_output:

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_str("volQuota", NULL, rsp.op_ret, rsp.op_errno,
                                 rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (!rsp.op_ret && type != GF_QUOTA_OPTION_TYPE_LIST &&
        type != GF_QUOTA_OPTION_TYPE_LIST_OBJECTS)
        cli_out("volume quota : success");

    ret = rsp.op_ret;
out:

    if ((type == GF_QUOTA_OPTION_TYPE_LIST) ||
        (type == GF_QUOTA_OPTION_TYPE_LIST_OBJECTS)) {
        gluster_remove_auxiliary_mount(volname);
    }

    cli_cmd_broadcast_response(ret);
    if (dict)
        dict_unref(dict);

    gf_free_xdr_cli_rsp(rsp);

    return ret;
}

static int
gf_cli_getspec_cbk(struct rpc_req *req, struct iovec *iov, int count,
                   void *myframe)
{
    gf_getspec_rsp rsp = {
        0,
    };
    int ret = -1;
    char *spec = NULL;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_getspec_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    if (rsp.op_ret == -1) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               "getspec failed");
        ret = -1;
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to getspec");

    spec = GF_MALLOC(rsp.op_ret + 1, cli_mt_char);
    if (!spec) {
        gf_log("", GF_LOG_ERROR, "out of memory");
        ret = -1;
        goto out;
    }
    memcpy(spec, rsp.spec, rsp.op_ret);
    spec[rsp.op_ret] = '\0';
    cli_out("%s", spec);
    GF_FREE(spec);

    ret = 0;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_getspec_rsp(rsp);
    return ret;
}

static int
gf_cli_pmap_b2p_cbk(struct rpc_req *req, struct iovec *iov, int count,
                    void *myframe)
{
    pmap_port_by_brick_rsp rsp = {
        0,
    };
    int ret = -1;
    char *spec = NULL;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_pmap_port_by_brick_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    if (rsp.op_ret == -1) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               "pump_b2p failed");
        ret = -1;
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to pmap b2p");

    cli_out("%d", rsp.port);
    GF_FREE(spec);

    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    return ret;
}

static int32_t
gf_cli_probe(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {
        {
            0,
        },
    };
    int ret = 0;
    dict_t *dict = NULL;
    int port = 0;

    if (!frame || !this || !data) {
        ret = -1;
        goto out;
    }

    dict = data;

    ret = dict_get_int32_sizen(dict, "port", &port);
    if (ret) {
        ret = dict_set_int32_sizen(dict, "port", CLI_GLUSTERD_PORT);
        if (ret)
            goto out;
    }

    ret = cli_to_glusterd(&req, frame, gf_cli_probe_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict, GLUSTER_CLI_PROBE,
                          this, cli_rpc_prog, NULL);

out:
    GF_FREE(req.dict.dict_val);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    return ret;
}

static int32_t
gf_cli_deprobe(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {
        {
            0,
        },
    };
    int ret = 0;
    dict_t *dict = NULL;
    int port = 0;
    int flags = 0;

    if (!frame || !this || !data) {
        ret = -1;
        goto out;
    }

    dict = data;
    ret = dict_get_int32_sizen(dict, "port", &port);
    if (ret) {
        ret = dict_set_int32_sizen(dict, "port", CLI_GLUSTERD_PORT);
        if (ret)
            goto out;
    }

    ret = dict_get_int32_sizen(dict, "flags", &flags);
    if (ret) {
        ret = dict_set_int32_sizen(dict, "flags", 0);
        if (ret)
            goto out;
    }

    ret = cli_to_glusterd(&req, frame, gf_cli_deprobe_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict, GLUSTER_CLI_DEPROBE,
                          this, cli_rpc_prog, NULL);

out:
    GF_FREE(req.dict.dict_val);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    return ret;
}

static int32_t
gf_cli_list_friends(call_frame_t *frame, xlator_t *this, void *data)
{
    gf1_cli_peer_list_req req = {
        0,
    };
    int ret = 0;
    unsigned long flags = 0;

    if (!frame || !this) {
        ret = -1;
        goto out;
    }

    GF_ASSERT(frame->local == NULL);

    flags = (long)data;
    req.flags = flags;
    frame->local = (void *)flags;
    ret = cli_cmd_submit(
        NULL, &req, frame, cli_rpc_prog, GLUSTER_CLI_LIST_FRIENDS, NULL, this,
        gf_cli_list_friends_cbk, (xdrproc_t)xdr_gf1_cli_peer_list_req);

out:
    if (ret && frame) {
        /*
         * If everything goes fine, gf_cli_list_friends_cbk()
         * [invoked through cli_cmd_submit()]resets the
         * frame->local to NULL. In case cli_cmd_submit()
         * fails in between, RESET frame->local here.
         */
        frame->local = NULL;
    }
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    return ret;
}

static int32_t
gf_cli_get_state(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {
        {
            0,
        },
    };
    int ret = 0;
    dict_t *dict = NULL;

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_get_state_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_GET_STATE, this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    GF_FREE(req.dict.dict_val);

    return ret;
}

static int32_t
gf_cli_get_next_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    int ret = 0;
    cli_cmd_volume_get_ctx_t *ctx = NULL;
    cli_local_t *local = NULL;

    if (!frame || !this || !data) {
        ret = -1;
        goto out;
    }

    ctx = data;
    local = frame->local;

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_vol_info_begin(local, 0, 0, "");
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
            goto out;
        }
    }

    ret = gf_cli_get_volume(frame, this, data);

    if (!local || !local->get_vol.volname) {
        if ((global_state->mode & GLUSTER_MODE_XML))
            goto end_xml;

        if (ret)
            cli_err("Failed to get volume info");
        else
            cli_err("No volumes present");
        goto out;
    }

    ctx->volname = local->get_vol.volname;

    while (ctx->volname) {
        ret = gf_cli_get_volume(frame, this, ctx);
        if (ret)
            goto out;
        ctx->volname = local->get_vol.volname;
    }

end_xml:
    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_vol_info_end(local);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
    }

out:
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    return ret;
}

static int32_t
gf_cli_get_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    cli_cmd_volume_get_ctx_t *ctx = NULL;
    dict_t *dict = NULL;
    int32_t flags = 0;

    if (!this || !data) {
        ret = -1;
        goto out;
    }

    ctx = data;

    dict = dict_new();
    if (!dict) {
        gf_log(THIS->name, GF_LOG_ERROR, "Failed to create the dict");
        ret = -1;
        goto out;
    }

    if (ctx->volname) {
        ret = dict_set_str_sizen(dict, "volname", ctx->volname);
        if (ret)
            goto out;
    }

    flags = ctx->flags;
    ret = dict_set_int32_sizen(dict, "flags", flags);
    if (ret) {
        gf_log(frame->this->name, GF_LOG_ERROR, "failed to set flags");
        goto out;
    }

    ret = dict_allocate_and_serialize(dict, &req.dict.dict_val,
                                      &req.dict.dict_len);
    if (ret) {
        gf_log(frame->this->name, GF_LOG_ERROR, DICT_SERIALIZE_FAIL);
        goto out;
    }

    ret = cli_cmd_submit(NULL, &req, frame, cli_rpc_prog,
                         GLUSTER_CLI_GET_VOLUME, NULL, this,
                         gf_cli_get_volume_cbk, (xdrproc_t)xdr_gf_cli_req);

out:
    if (dict)
        dict_unref(dict);

    GF_FREE(req.dict.dict_val);

    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    return ret;
}

static int32_t
gf_cli3_1_uuid_get(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;

    dict = data;
    ret = cli_to_glusterd(&req, frame, gf_cli3_1_uuid_get_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict, GLUSTER_CLI_UUID_GET,
                          this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    GF_FREE(req.dict.dict_val);
    return ret;
}

static int32_t
gf_cli3_1_uuid_reset(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;

    dict = data;
    ret = cli_to_glusterd(&req, frame, gf_cli3_1_uuid_reset_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_UUID_RESET, this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    GF_FREE(req.dict.dict_val);
    return ret;
}

static int32_t
gf_cli_create_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_create_volume_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_CREATE_VOLUME, this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    GF_FREE(req.dict.dict_val);

    return ret;
}

static int32_t
gf_cli_delete_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_delete_volume_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_DELETE_VOLUME, this, cli_rpc_prog, NULL);
    GF_FREE(req.dict.dict_val);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    return ret;
}

static int32_t
gf_cli_start_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_start_volume_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_START_VOLUME, this, cli_rpc_prog, NULL);

    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    GF_FREE(req.dict.dict_val);

    return ret;
}

static int32_t
gf_cli_stop_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = data;

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_stop_volume_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_STOP_VOLUME, this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    GF_FREE(req.dict.dict_val);

    return ret;
}

static int32_t
gf_cli_defrag_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_defrag_volume_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_DEFRAG_VOLUME, this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    GF_FREE(req.dict.dict_val);

    return ret;
}

static int32_t
gf_cli_rename_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;

    if (!frame || !this || !data) {
        ret = -1;
        goto out;
    }

    dict = data;

    ret = dict_allocate_and_serialize(dict, &req.dict.dict_val,
                                      &req.dict.dict_len);
    if (ret < 0) {
        gf_log(this->name, GF_LOG_ERROR, DICT_SERIALIZE_FAIL);

        goto out;
    }

    ret = cli_cmd_submit(NULL, &req, frame, cli_rpc_prog,
                         GLUSTER_CLI_RENAME_VOLUME, NULL, this,
                         gf_cli_rename_volume_cbk, (xdrproc_t)xdr_gf_cli_req);

out:
    GF_FREE(req.dict.dict_val);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    return ret;
}

static int32_t
gf_cli_reset_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_reset_volume_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_RESET_VOLUME, this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    GF_FREE(req.dict.dict_val);
    return ret;
}

static int32_t
gf_cli_ganesha(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_ganesha_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict, GLUSTER_CLI_GANESHA,
                          this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    return ret;
}

static int32_t
gf_cli_set_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_set_volume_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_SET_VOLUME, this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    GF_FREE(req.dict.dict_val);

    return ret;
}

int32_t
gf_cli_add_brick(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_add_brick_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_ADD_BRICK, this, cli_rpc_prog, NULL);

    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    GF_FREE(req.dict.dict_val);

    return ret;
}

static int32_t
gf_cli_remove_brick(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    ;
    gf_cli_req status_req = {{
        0,
    }};
    ;
    int ret = 0;
    dict_t *dict = NULL;
    int32_t command = 0;
    int32_t cmd = 0;

    if (!frame || !this) {
        ret = -1;
        goto out;
    }

    dict = data;

    ret = dict_get_int32_sizen(dict, "command", &command);
    if (ret)
        goto out;

    if ((command != GF_OP_CMD_STATUS) && (command != GF_OP_CMD_STOP)) {
        ret = cli_to_glusterd(
            &req, frame, gf_cli_remove_brick_cbk, (xdrproc_t)xdr_gf_cli_req,
            dict, GLUSTER_CLI_REMOVE_BRICK, this, cli_rpc_prog, NULL);
    } else {
        /* Need rebalance status to be sent :-) */
        if (command == GF_OP_CMD_STATUS)
            cmd |= GF_DEFRAG_CMD_STATUS;
        else
            cmd |= GF_DEFRAG_CMD_STOP;

        ret = dict_set_int32_sizen(dict, "rebalance-command", (int32_t)cmd);
        if (ret) {
            gf_log(this->name, GF_LOG_ERROR, "Failed to set dict");
            goto out;
        }

        ret = cli_to_glusterd(
            &status_req, frame, gf_cli3_remove_brick_status_cbk,
            (xdrproc_t)xdr_gf_cli_req, dict, GLUSTER_CLI_DEFRAG_VOLUME, this,
            cli_rpc_prog, NULL);
    }

out:
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    GF_FREE(req.dict.dict_val);

    GF_FREE(status_req.dict.dict_val);

    return ret;
}

static int32_t
gf_cli_reset_brick(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;
    char *dst_brick = NULL;
    char *op = NULL;

    if (!frame || !this || !data) {
        ret = -1;
        goto out;
    }

    dict = data;

    ret = dict_get_str_sizen(dict, "operation", &op);
    if (ret) {
        gf_log(this->name, GF_LOG_DEBUG, "dict_get on operation failed");
        goto out;
    }

    if (!strcmp(op, "GF_RESET_OP_COMMIT") ||
        !strcmp(op, "GF_RESET_OP_COMMIT_FORCE")) {
        ret = dict_get_str_sizen(dict, "dst-brick", &dst_brick);
        if (ret) {
            gf_log(this->name, GF_LOG_DEBUG, "dict_get on dst-brick failed");
            goto out;
        }
    }

    ret = cli_to_glusterd(&req, frame, gf_cli_reset_brick_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_RESET_BRICK, this, cli_rpc_prog, NULL);

out:
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    GF_FREE(req.dict.dict_val);

    return ret;
}

static int32_t
gf_cli_replace_brick(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;
    int32_t op = 0;

    if (!frame || !this || !data) {
        ret = -1;
        goto out;
    }

    dict = data;

    ret = dict_get_int32_sizen(dict, "operation", &op);
    if (ret) {
        gf_log(this->name, GF_LOG_DEBUG, "dict_get on operation failed");
        goto out;
    }

    ret = cli_to_glusterd(&req, frame, gf_cli_replace_brick_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_REPLACE_BRICK, this, cli_rpc_prog, NULL);

out:
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    GF_FREE(req.dict.dict_val);

    return ret;
}

static int32_t
gf_cli_log_rotate(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_log_rotate_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_LOG_ROTATE, this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    GF_FREE(req.dict.dict_val);
    return ret;
}

static int32_t
gf_cli_sync_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    int ret = 0;
    gf_cli_req req = {{
        0,
    }};
    dict_t *dict = NULL;

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_sync_volume_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_SYNC_VOLUME, this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    GF_FREE(req.dict.dict_val);

    return ret;
}

static int32_t
gf_cli_getspec(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_getspec_req req = {
        0,
    };
    int ret = 0;
    dict_t *dict = NULL;
    dict_t *op_dict = NULL;

    if (!frame || !this) {
        ret = -1;
        goto out;
    }

    dict = data;

    ret = dict_get_str_sizen(dict, "volid", &req.key);
    if (ret)
        goto out;

    op_dict = dict_new();
    if (!op_dict) {
        ret = -1;
        goto out;
    }

    // Set the supported min and max op-versions, so glusterd can make a
    // decision
    ret = dict_set_int32_sizen(op_dict, "min-op-version", GD_OP_VERSION_MIN);
    if (ret) {
        gf_log(THIS->name, GF_LOG_ERROR,
               "Failed to set min-op-version in request dict");
        goto out;
    }

    ret = dict_set_int32_sizen(op_dict, "max-op-version", GD_OP_VERSION_MAX);
    if (ret) {
        gf_log(THIS->name, GF_LOG_ERROR,
               "Failed to set max-op-version in request dict");
        goto out;
    }

    ret = dict_allocate_and_serialize(op_dict, &req.xdata.xdata_val,
                                      &req.xdata.xdata_len);
    if (ret < 0) {
        gf_log(THIS->name, GF_LOG_ERROR, DICT_SERIALIZE_FAIL);
        goto out;
    }

    ret = cli_cmd_submit(NULL, &req, frame, &cli_handshake_prog,
                         GF_HNDSK_GETSPEC, NULL, this, gf_cli_getspec_cbk,
                         (xdrproc_t)xdr_gf_getspec_req);

out:
    if (op_dict) {
        dict_unref(op_dict);
    }
    GF_FREE(req.xdata.xdata_val);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    return ret;
}

static int32_t
gf_cli_quota(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_quota_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict, GLUSTER_CLI_QUOTA,
                          this, cli_rpc_prog, NULL);
    GF_FREE(req.dict.dict_val);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    return ret;
}

static int32_t
gf_cli_pmap_b2p(call_frame_t *frame, xlator_t *this, void *data)
{
    pmap_port_by_brick_req req = {
        0,
    };
    int ret = 0;
    dict_t *dict = NULL;

    if (!frame || !this) {
        ret = -1;
        goto out;
    }

    dict = data;

    ret = dict_get_str_sizen(dict, "brick", &req.brick);
    if (ret)
        goto out;

    ret = cli_cmd_submit(NULL, &req, frame, &cli_pmap_prog, GF_PMAP_PORTBYBRICK,
                         NULL, this, gf_cli_pmap_b2p_cbk,
                         (xdrproc_t)xdr_pmap_port_by_brick_req);

out:
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    return ret;
}

static int
gf_cli_fsm_log_cbk(struct rpc_req *req, struct iovec *iov, int count,
                   void *myframe)
{
    gf1_cli_fsm_log_rsp rsp = {
        0,
    };
    int ret = -1;
    dict_t *dict = NULL;
    int tr_count = 0;
    char key[64] = {0};
    int keylen;
    int i = 0;
    char *old_state = NULL;
    char *new_state = NULL;
    char *event = NULL;
    char *time = NULL;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf1_cli_fsm_log_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    if (rsp.op_ret) {
        if (strcmp(rsp.op_errstr, ""))
            cli_err("%s", rsp.op_errstr);
        cli_err("fsm log unsuccessful");
        ret = rsp.op_ret;
        goto out;
    }

    dict = dict_new();
    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(rsp.fsm_log.fsm_log_val, rsp.fsm_log.fsm_log_len,
                           &dict);

    if (ret) {
        cli_err(DICT_UNSERIALIZE_FAIL);
        goto out;
    }

    ret = dict_get_int32_sizen(dict, "count", &tr_count);
    if (!ret && tr_count)
        cli_out("number of transitions: %d", tr_count);
    else
        cli_err("No transitions");
    for (i = 0; i < tr_count; i++) {
        keylen = snprintf(key, sizeof(key), "log%d-old-state", i);
        ret = dict_get_strn(dict, key, keylen, &old_state);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "log%d-event", i);
        ret = dict_get_strn(dict, key, keylen, &event);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "log%d-new-state", i);
        ret = dict_get_strn(dict, key, keylen, &new_state);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "log%d-time", i);
        ret = dict_get_strn(dict, key, keylen, &time);
        if (ret)
            goto out;
        cli_out(
            "Old State: [%s]\n"
            "New State: [%s]\n"
            "Event    : [%s]\n"
            "timestamp: [%s]\n",
            old_state, new_state, event, time);
    }

    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    if (dict) {
        dict_unref(dict);
    }
    gf_free_xdr_fsm_log_rsp(rsp);

    return ret;
}

static int32_t
gf_cli_fsm_log(call_frame_t *frame, xlator_t *this, void *data)
{
    int ret = -1;
    gf1_cli_fsm_log_req req = {
        0,
    };

    GF_ASSERT(frame);
    GF_ASSERT(this);
    GF_ASSERT(data);

    if (!frame || !this || !data)
        goto out;
    req.name = data;
    ret = cli_cmd_submit(NULL, &req, frame, cli_rpc_prog, GLUSTER_CLI_FSM_LOG,
                         NULL, this, gf_cli_fsm_log_cbk,
                         (xdrproc_t)xdr_gf1_cli_fsm_log_req);

out:
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    return ret;
}

static int
gf_cli_gsync_config_command(dict_t *dict)
{
    runner_t runner = {
        0,
    };
    char *subop = NULL;
    char *gwd = NULL;
    char *slave = NULL;
    char *confpath = NULL;
    char *master = NULL;
    char *op_name = NULL;
    int ret = -1;
    char conf_path[PATH_MAX] = "";

    if (dict_get_str_sizen(dict, "subop", &subop) != 0)
        return -1;

    if (strcmp(subop, "get") != 0 && strcmp(subop, "get-all") != 0) {
        cli_out(GEOREP " config updated successfully");
        return 0;
    }

    if (dict_get_str_sizen(dict, "glusterd_workdir", &gwd) != 0 ||
        dict_get_str_sizen(dict, "slave", &slave) != 0)
        return -1;

    if (dict_get_str_sizen(dict, "master", &master) != 0)
        master = NULL;
    if (dict_get_str_sizen(dict, "op_name", &op_name) != 0)
        op_name = NULL;

    ret = dict_get_str_sizen(dict, "conf_path", &confpath);
    if (ret || !confpath) {
        ret = snprintf(conf_path, sizeof(conf_path) - 1,
                       "%s/" GEOREP "/gsyncd_template.conf", gwd);
        conf_path[ret] = '\0';
        confpath = conf_path;
    }

    runinit(&runner);
    runner_add_args(&runner, GSYNCD_PREFIX "/gsyncd", "-c", NULL);
    runner_argprintf(&runner, "%s", confpath);
    runner_argprintf(&runner, "--iprefix=%s", DATADIR);
    if (master)
        runner_argprintf(&runner, ":%s", master);
    runner_add_arg(&runner, slave);
    runner_argprintf(&runner, "--config-%s", subop);
    if (op_name)
        runner_add_arg(&runner, op_name);

    return runner_run(&runner);
}

static int
gf_cli_print_status(char **title_values, gf_gsync_status_t **sts_vals,
                    int *spacing, int gsync_count, int number_of_fields,
                    int is_detail)
{
    int i = 0;
    int j = 0;
    int ret = 0;
    int status_fields = 8; /* Indexed at 0 */
    int total_spacing = 0;
    char **output_values = NULL;
    char *tmp = NULL;
    char *hyphens = NULL;

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
    output_values = GF_MALLOC(number_of_fields * sizeof(char *),
                              gf_common_mt_char);
    if (!output_values) {
        ret = -1;
        goto out;
    }
    for (i = 0; i < number_of_fields; i++) {
        output_values[i] = GF_CALLOC(spacing[i] + 1, sizeof(char),
                                     gf_common_mt_char);
        if (!output_values[i]) {
            ret = -1;
            goto out;
        }
    }

    cli_out(" ");

    /* setting the title "NODE", "MASTER", etc. from title_values[]
       and printing the same */
    for (j = 0; j < number_of_fields; j++) {
        if ((!is_detail) && (j > status_fields)) {
            /* Suppressing detailed output for
             * status */
            output_values[j][0] = '\0';
            continue;
        }
        memset(output_values[j], ' ', spacing[j]);
        memcpy(output_values[j], title_values[j], strlen(title_values[j]));
        output_values[j][spacing[j]] = '\0';
    }
    cli_out("%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s", output_values[0],
            output_values[1], output_values[2], output_values[3],
            output_values[4], output_values[5], output_values[6],
            output_values[7], output_values[8], output_values[9],
            output_values[10], output_values[11], output_values[12],
            output_values[13], output_values[14], output_values[15]);

    hyphens = GF_MALLOC((total_spacing + 1) * sizeof(char), gf_common_mt_char);
    if (!hyphens) {
        ret = -1;
        goto out;
    }

    /* setting and printing the hyphens */
    memset(hyphens, '-', total_spacing);
    hyphens[total_spacing] = '\0';
    cli_out("%s", hyphens);

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
                gf_log("", GF_LOG_ERROR, "struct member empty.");
                ret = -1;
                goto out;
            }
            memset(output_values[j], ' ', spacing[j]);
            memcpy(output_values[j], tmp, strlen(tmp));
            output_values[j][spacing[j]] = '\0';
        }

        cli_out("%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s",
                output_values[0], output_values[1], output_values[2],
                output_values[3], output_values[4], output_values[5],
                output_values[6], output_values[7], output_values[8],
                output_values[9], output_values[10], output_values[11],
                output_values[12], output_values[13], output_values[14],
                output_values[15]);
    }

out:
    if (output_values) {
        for (i = 0; i < number_of_fields; i++) {
            if (output_values[i])
                GF_FREE(output_values[i]);
        }
        GF_FREE(output_values);
    }

    if (hyphens)
        GF_FREE(hyphens);

    return ret;
}

int
gf_gsync_status_t_comparator(const void *p, const void *q)
{
    char *slavekey1 = NULL;
    char *slavekey2 = NULL;

    slavekey1 = get_struct_variable(20, (*(gf_gsync_status_t **)p));
    slavekey2 = get_struct_variable(20, (*(gf_gsync_status_t **)q));
    if (!slavekey1 || !slavekey2) {
        gf_log("cli", GF_LOG_ERROR, "struct member empty.");
        return 0;
    }

    return strcmp(slavekey1, slavekey2);
}

static int
gf_cli_read_status_data(dict_t *dict, gf_gsync_status_t **sts_vals,
                        int *spacing, int gsync_count, int number_of_fields)
{
    char *tmp = NULL;
    char sts_val_name[PATH_MAX] = "";
    int ret = 0;
    int i = 0;
    int j = 0;

    /* Storing per node status info in each object */
    for (i = 0; i < gsync_count; i++) {
        snprintf(sts_val_name, sizeof(sts_val_name), "status_value%d", i);

        /* Fetching the values from dict, and calculating
           the max length for each field */
        ret = dict_get_bin(dict, sts_val_name, (void **)&(sts_vals[i]));
        if (ret)
            goto out;

        for (j = 0; j < number_of_fields; j++) {
            tmp = get_struct_variable(j, sts_vals[i]);
            if (!tmp) {
                gf_log("", GF_LOG_ERROR, "struct member empty.");
                ret = -1;
                goto out;
            }
            if (strlen(tmp) > spacing[j])
                spacing[j] = strlen(tmp);
        }
    }

    /* Sort based on Session Slave */
    qsort(sts_vals, gsync_count, sizeof(gf_gsync_status_t *),
          gf_gsync_status_t_comparator);

out:
    return ret;
}

static int
gf_cli_gsync_status_output(dict_t *dict, gf_boolean_t is_detail)
{
    int gsync_count = 0;
    int i = 0;
    int ret = 0;
    int spacing[16] = {0};
    int num_of_fields = 16;
    char errmsg[1024] = "";
    char *master = NULL;
    char *slave = NULL;
    static char *title_values[] = {"MASTER NODE",
                                   "MASTER VOL",
                                   "MASTER BRICK",
                                   "SLAVE USER",
                                   "SLAVE",
                                   "SLAVE NODE",
                                   "STATUS",
                                   "CRAWL STATUS",
                                   "LAST_SYNCED",
                                   "ENTRY",
                                   "DATA",
                                   "META",
                                   "FAILURES",
                                   "CHECKPOINT TIME",
                                   "CHECKPOINT COMPLETED",
                                   "CHECKPOINT COMPLETION TIME"};
    gf_gsync_status_t **sts_vals = NULL;

    /* Checks if any session is active or not */
    ret = dict_get_int32_sizen(dict, "gsync-count", &gsync_count);
    if (ret) {
        ret = dict_get_str_sizen(dict, "master", &master);

        ret = dict_get_str_sizen(dict, "slave", &slave);

        if (master) {
            if (slave)
                snprintf(errmsg, sizeof(errmsg),
                         "No active geo-replication sessions between %s"
                         " and %s",
                         master, slave);
            else
                snprintf(errmsg, sizeof(errmsg),
                         "No active geo-replication sessions for %s", master);
        } else
            snprintf(errmsg, sizeof(errmsg),
                     "No active geo-replication sessions");

        gf_log("cli", GF_LOG_INFO, "%s", errmsg);
        cli_out("%s", errmsg);
        ret = -1;
        goto out;
    }

    for (i = 0; i < num_of_fields; i++)
        spacing[i] = strlen(title_values[i]);

    /* gsync_count = number of nodes reporting output.
       each sts_val object will store output of each
       node */
    sts_vals = GF_MALLOC(gsync_count * sizeof(gf_gsync_status_t *),
                         gf_common_mt_char);
    if (!sts_vals) {
        ret = -1;
        goto out;
    }

    ret = gf_cli_read_status_data(dict, sts_vals, spacing, gsync_count,
                                  num_of_fields);
    if (ret) {
        gf_log("", GF_LOG_ERROR, "Unable to read status data");
        goto out;
    }

    ret = gf_cli_print_status(title_values, sts_vals, spacing, gsync_count,
                              num_of_fields, is_detail);
    if (ret) {
        gf_log("", GF_LOG_ERROR, "Unable to print status output");
        goto out;
    }

out:
    if (sts_vals)
        GF_FREE(sts_vals);

    return ret;
}

static int32_t
write_contents_to_common_pem_file(dict_t *dict, int output_count)
{
    char *workdir = NULL;
    char common_pem_file[PATH_MAX] = "";
    char *output = NULL;
    char output_name[32] = "";
    int bytes_written = 0;
    int fd = -1;
    int ret = -1;
    int i = -1;

    ret = dict_get_str_sizen(dict, "glusterd_workdir", &workdir);
    if (ret || !workdir) {
        gf_log("", GF_LOG_ERROR, "Unable to fetch workdir");
        ret = -1;
        goto out;
    }

    snprintf(common_pem_file, sizeof(common_pem_file),
             "%s/geo-replication/common_secret.pem.pub", workdir);

    sys_unlink(common_pem_file);

    fd = open(common_pem_file, O_WRONLY | O_CREAT, 0600);
    if (fd == -1) {
        gf_log("", GF_LOG_ERROR, "Failed to open %s Error : %s",
               common_pem_file, strerror(errno));
        ret = -1;
        goto out;
    }

    for (i = 1; i <= output_count; i++) {
        ret = snprintf(output_name, sizeof(output_name), "output_%d", i);
        ret = dict_get_strn(dict, output_name, ret, &output);
        if (ret) {
            gf_log("", GF_LOG_ERROR, "Failed to get %s.", output_name);
            cli_out("Unable to fetch output.");
        }
        if (output) {
            bytes_written = sys_write(fd, output, strlen(output));
            if (bytes_written != strlen(output)) {
                gf_log("", GF_LOG_ERROR, "Failed to write to %s",
                       common_pem_file);
                ret = -1;
                goto out;
            }
            /* Adding the new line character */
            bytes_written = sys_write(fd, "\n", 1);
            if (bytes_written != 1) {
                gf_log("", GF_LOG_ERROR, "Failed to add new line char");
                ret = -1;
                goto out;
            }
            output = NULL;
        }
    }

    cli_out("Common secret pub file present at %s", common_pem_file);
    ret = 0;
out:
    if (fd >= 0)
        sys_close(fd);

    gf_log("", GF_LOG_DEBUG, RETURNING, ret);
    return ret;
}

static int
gf_cli_sys_exec_cbk(struct rpc_req *req, struct iovec *iov, int count,
                    void *myframe)
{
    int ret = -1;
    int output_count = -1;
    int i = -1;
    char *output = NULL;
    char *command = NULL;
    char output_name[32] = "";
    gf_cli_rsp rsp = {
        0,
    };
    dict_t *dict = NULL;

    GF_ASSERT(myframe);

    if (req->rpc_status == -1) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    dict = dict_new();

    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);

    if (ret)
        goto out;

    if (rsp.op_ret) {
        cli_err("%s", rsp.op_errstr ? rsp.op_errstr : "Command failed.");
        ret = rsp.op_ret;
        goto out;
    }

    ret = dict_get_int32_sizen(dict, "output_count", &output_count);
    if (ret) {
        cli_out("Command executed successfully.");
        ret = 0;
        goto out;
    }

    ret = dict_get_str_sizen(dict, "command", &command);
    if (ret) {
        gf_log("", GF_LOG_ERROR, "Unable to get command from dict");
        goto out;
    }

    if (!strcmp(command, "gsec_create")) {
        ret = write_contents_to_common_pem_file(dict, output_count);
        if (!ret)
            goto out;
    }

    for (i = 1; i <= output_count; i++) {
        ret = snprintf(output_name, sizeof(output_name), "output_%d", i);
        ret = dict_get_strn(dict, output_name, ret, &output);
        if (ret) {
            gf_log("", GF_LOG_ERROR, "Failed to get %s.", output_name);
            cli_out("Unable to fetch output.");
        }
        if (output) {
            cli_out("%s", output);
            output = NULL;
        }
    }

    ret = 0;
out:
    if (dict)
        dict_unref(dict);
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int
gf_cli_copy_file_cbk(struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
    int ret = -1;
    gf_cli_rsp rsp = {
        0,
    };
    dict_t *dict = NULL;

    GF_ASSERT(myframe);

    if (req->rpc_status == -1) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    dict = dict_new();

    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);

    if (ret)
        goto out;

    if (rsp.op_ret) {
        cli_err("%s", rsp.op_errstr ? rsp.op_errstr : "Copy unsuccessful");
        ret = rsp.op_ret;
        goto out;
    }

    cli_out("Successfully copied file.");

out:
    if (dict)
        dict_unref(dict);
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int
gf_cli_gsync_set_cbk(struct rpc_req *req, struct iovec *iov, int count,
                     void *myframe)
{
    int ret = -1;
    gf_cli_rsp rsp = {
        0,
    };
    dict_t *dict = NULL;
    char *gsync_status = NULL;
    char *master = NULL;
    char *slave = NULL;
    int32_t type = 0;
    gf_boolean_t status_detail = _gf_false;

    GF_ASSERT(myframe);

    if (req->rpc_status == -1) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    dict = dict_new();

    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);

    if (ret)
        goto out;

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_vol_gsync(dict, rsp.op_ret, rsp.op_errno,
                                       rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (rsp.op_ret) {
        cli_err("%s",
                rsp.op_errstr ? rsp.op_errstr : GEOREP " command unsuccessful");
        ret = rsp.op_ret;
        goto out;
    }

    ret = dict_get_str_sizen(dict, "gsync-status", &gsync_status);
    if (!ret)
        cli_out("%s", gsync_status);

    ret = dict_get_int32_sizen(dict, "type", &type);
    if (ret) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               "failed to get type");
        goto out;
    }

    switch (type) {
        case GF_GSYNC_OPTION_TYPE_START:
        case GF_GSYNC_OPTION_TYPE_STOP:
            if (dict_get_str_sizen(dict, "master", &master) != 0)
                master = "???";
            if (dict_get_str_sizen(dict, "slave", &slave) != 0)
                slave = "???";

            cli_out(
                "%s " GEOREP " session between %s & %s has been successful",
                type == GF_GSYNC_OPTION_TYPE_START ? "Starting" : "Stopping",
                master, slave);
            break;

        case GF_GSYNC_OPTION_TYPE_PAUSE:
        case GF_GSYNC_OPTION_TYPE_RESUME:
            if (dict_get_str_sizen(dict, "master", &master) != 0)
                master = "???";
            if (dict_get_str_sizen(dict, "slave", &slave) != 0)
                slave = "???";

            cli_out("%s " GEOREP " session between %s & %s has been successful",
                    type == GF_GSYNC_OPTION_TYPE_PAUSE ? "Pausing" : "Resuming",
                    master, slave);
            break;

        case GF_GSYNC_OPTION_TYPE_CONFIG:
            ret = gf_cli_gsync_config_command(dict);
            break;

        case GF_GSYNC_OPTION_TYPE_STATUS:
            status_detail = dict_get_str_boolean(dict, "status-detail",
                                                 _gf_false);
            ret = gf_cli_gsync_status_output(dict, status_detail);
            break;

        case GF_GSYNC_OPTION_TYPE_DELETE:
            if (dict_get_str_sizen(dict, "master", &master) != 0)
                master = "???";
            if (dict_get_str_sizen(dict, "slave", &slave) != 0)
                slave = "???";
            cli_out("Deleting " GEOREP
                    " session between %s & %s has been successful",
                    master, slave);
            break;

        case GF_GSYNC_OPTION_TYPE_CREATE:
            if (dict_get_str_sizen(dict, "master", &master) != 0)
                master = "???";
            if (dict_get_str_sizen(dict, "slave", &slave) != 0)
                slave = "???";
            cli_out("Creating " GEOREP
                    " session between %s & %s has been successful",
                    master, slave);
            break;

        default:
            cli_out(GEOREP " command executed successfully");
    }

out:
    if (dict)
        dict_unref(dict);
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int32_t
gf_cli_sys_exec(call_frame_t *frame, xlator_t *this, void *data)
{
    int ret = 0;
    dict_t *dict = NULL;
    gf_cli_req req = {{
        0,
    }};

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_sys_exec_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict, GLUSTER_CLI_SYS_EXEC,
                          this, cli_rpc_prog, NULL);
    if (ret)
        if (!frame || !this || !data) {
            ret = -1;
            gf_log("cli", GF_LOG_ERROR, "Invalid data");
        }

    GF_FREE(req.dict.dict_val);
    return ret;
}

static int32_t
gf_cli_copy_file(call_frame_t *frame, xlator_t *this, void *data)
{
    int ret = 0;
    dict_t *dict = NULL;
    gf_cli_req req = {{
        0,
    }};

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_copy_file_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_COPY_FILE, this, cli_rpc_prog, NULL);
    if (ret)
        if (!frame || !this || !data) {
            ret = -1;
            gf_log("cli", GF_LOG_ERROR, "Invalid data");
        }

    GF_FREE(req.dict.dict_val);
    return ret;
}

static int32_t
gf_cli_gsync_set(call_frame_t *frame, xlator_t *this, void *data)
{
    int ret = 0;
    dict_t *dict = NULL;
    gf_cli_req req = {{
        0,
    }};

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_gsync_set_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_GSYNC_SET, this, cli_rpc_prog, NULL);
    GF_FREE(req.dict.dict_val);

    return ret;
}

int
cli_profile_info_percentage_cmp(void *a, void *b)
{
    cli_profile_info_t *ia = NULL;
    cli_profile_info_t *ib = NULL;

    ia = a;
    ib = b;
    if (ia->percentage_avg_latency < ib->percentage_avg_latency)
        return -1;
    else if (ia->percentage_avg_latency > ib->percentage_avg_latency)
        return 1;

    return 0;
}

static void
cmd_profile_volume_brick_out(dict_t *dict, int count, int interval)
{
    char key[256] = {0};
    int i = 0;
    uint64_t sec = 0;
    uint64_t r_count = 0;
    uint64_t w_count = 0;
    uint64_t rb_counts[32] = {0};
    uint64_t wb_counts[32] = {0};
    cli_profile_info_t profile_info[GF_FOP_MAXVALUE] = {{0}};
    cli_profile_info_t upcall_info[GF_UPCALL_FLAGS_MAXVALUE] = {
        {0},
    };
    char output[128] = {0};
    int per_line = 0;
    char read_blocks[128] = {0};
    char write_blocks[128] = {0};
    int index = 0;
    int is_header_printed = 0;
    int ret = 0;
    double total_percentage_latency = 0;

    for (i = 0; i < 32; i++) {
        snprintf(key, sizeof(key), "%d-%d-read-%" PRIu32, count, interval,
                 (1U << i));
        ret = dict_get_uint64(dict, key, &rb_counts[i]);
        if (ret) {
            gf_log("cli", GF_LOG_DEBUG, "failed to get %s from dict", key);
        }
    }

    for (i = 0; i < 32; i++) {
        snprintf(key, sizeof(key), "%d-%d-write-%" PRIu32, count, interval,
                 (1U << i));
        ret = dict_get_uint64(dict, key, &wb_counts[i]);
        if (ret) {
            gf_log("cli", GF_LOG_DEBUG, "failed to get %s from dict", key);
        }
    }

    for (i = 0; i < GF_UPCALL_FLAGS_MAXVALUE; i++) {
        snprintf(key, sizeof(key), "%d-%d-%d-upcall-hits", count, interval, i);
        ret = dict_get_uint64(dict, key, &upcall_info[i].fop_hits);
        if (ret) {
            gf_log("cli", GF_LOG_DEBUG, "failed to get %s from dict", key);
        }
        upcall_info[i].fop_name = (char *)gf_upcall_list[i];
    }

    for (i = 0; i < GF_FOP_MAXVALUE; i++) {
        snprintf(key, sizeof(key), "%d-%d-%d-hits", count, interval, i);
        ret = dict_get_uint64(dict, key, &profile_info[i].fop_hits);
        if (ret) {
            gf_log("cli", GF_LOG_DEBUG, "failed to get %s from dict", key);
        }

        snprintf(key, sizeof(key), "%d-%d-%d-avglatency", count, interval, i);
        ret = dict_get_double(dict, key, &profile_info[i].avg_latency);
        if (ret) {
            gf_log("cli", GF_LOG_DEBUG, "failed to get %s from dict", key);
        }

        snprintf(key, sizeof(key), "%d-%d-%d-minlatency", count, interval, i);
        ret = dict_get_double(dict, key, &profile_info[i].min_latency);
        if (ret) {
            gf_log("cli", GF_LOG_DEBUG, "failed to get %s from dict", key);
        }

        snprintf(key, sizeof(key), "%d-%d-%d-maxlatency", count, interval, i);
        ret = dict_get_double(dict, key, &profile_info[i].max_latency);
        if (ret) {
            gf_log("cli", GF_LOG_DEBUG, "failed to get %s from dict", key);
        }
        profile_info[i].fop_name = (char *)gf_fop_list[i];

        total_percentage_latency += (profile_info[i].fop_hits *
                                     profile_info[i].avg_latency);
    }
    if (total_percentage_latency) {
        for (i = 0; i < GF_FOP_MAXVALUE; i++) {
            profile_info[i]
                .percentage_avg_latency = 100 * ((profile_info[i].avg_latency *
                                                  profile_info[i].fop_hits) /
                                                 total_percentage_latency);
        }
        gf_array_insertionsort(profile_info, 1, GF_FOP_MAXVALUE - 1,
                               sizeof(cli_profile_info_t),
                               cli_profile_info_percentage_cmp);
    }
    snprintf(key, sizeof(key), "%d-%d-duration", count, interval);
    ret = dict_get_uint64(dict, key, &sec);
    if (ret) {
        gf_log("cli", GF_LOG_DEBUG, "failed to get %s from dict", key);
    }

    snprintf(key, sizeof(key), "%d-%d-total-read", count, interval);
    ret = dict_get_uint64(dict, key, &r_count);
    if (ret) {
        gf_log("cli", GF_LOG_DEBUG, "failed to get %s from dict", key);
    }

    snprintf(key, sizeof(key), "%d-%d-total-write", count, interval);
    ret = dict_get_uint64(dict, key, &w_count);
    if (ret) {
        gf_log("cli", GF_LOG_DEBUG, "failed to get %s from dict", key);
    }

    if (interval == -1)
        cli_out("Cumulative Stats:");
    else
        cli_out("Interval %d Stats:", interval);
    snprintf(output, sizeof(output), "%14s", "Block Size:");
    snprintf(read_blocks, sizeof(read_blocks), "%14s", "No. of Reads:");
    snprintf(write_blocks, sizeof(write_blocks), "%14s", "No. of Writes:");
    index = 14;
    for (i = 0; i < 32; i++) {
        if ((rb_counts[i] == 0) && (wb_counts[i] == 0))
            continue;
        per_line++;
        snprintf(output + index, sizeof(output) - index, "%19" PRIu32 "b+ ",
                 (1U << i));
        if (rb_counts[i]) {
            snprintf(read_blocks + index, sizeof(read_blocks) - index,
                     "%21" PRId64 " ", rb_counts[i]);
        } else {
            snprintf(read_blocks + index, sizeof(read_blocks) - index, "%21s ",
                     "0");
        }
        if (wb_counts[i]) {
            snprintf(write_blocks + index, sizeof(write_blocks) - index,
                     "%21" PRId64 " ", wb_counts[i]);
        } else {
            snprintf(write_blocks + index, sizeof(write_blocks) - index,
                     "%21s ", "0");
        }
        index += 22;
        if (per_line == 3) {
            cli_out("%s", output);
            cli_out("%s", read_blocks);
            cli_out("%s", write_blocks);
            cli_out(" ");
            per_line = 0;
            snprintf(output, sizeof(output), "%14s", "Block Size:");
            snprintf(read_blocks, sizeof(read_blocks), "%14s", "No. of Reads:");
            snprintf(write_blocks, sizeof(write_blocks), "%14s",
                     "No. of Writes:");
            index = 14;
        }
    }

    if (per_line != 0) {
        cli_out("%s", output);
        cli_out("%s", read_blocks);
        cli_out("%s", write_blocks);
    }
    for (i = 0; i < GF_FOP_MAXVALUE; i++) {
        if (profile_info[i].fop_hits == 0)
            continue;
        if (is_header_printed == 0) {
            cli_out("%10s %13s %13s %13s %14s %11s", "%-latency", "Avg-latency",
                    "Min-Latency", "Max-Latency", "No. of calls", "Fop");
            cli_out("%10s %13s %13s %13s %14s %11s", "---------", "-----------",
                    "-----------", "-----------", "------------", "----");
            is_header_printed = 1;
        }
        if (profile_info[i].fop_hits) {
            cli_out(
                "%10.2lf %10.2lf us %10.2lf us %10.2lf us"
                " %14" PRId64 " %11s",
                profile_info[i].percentage_avg_latency,
                profile_info[i].avg_latency, profile_info[i].min_latency,
                profile_info[i].max_latency, profile_info[i].fop_hits,
                profile_info[i].fop_name);
        }
    }

    for (i = 0; i < GF_UPCALL_FLAGS_MAXVALUE; i++) {
        if (upcall_info[i].fop_hits == 0)
            continue;
        if (upcall_info[i].fop_hits) {
            cli_out(
                "%10.2lf %10.2lf us %10.2lf us %10.2lf us"
                " %14" PRId64 " %11s",
                upcall_info[i].percentage_avg_latency,
                upcall_info[i].avg_latency, upcall_info[i].min_latency,
                upcall_info[i].max_latency, upcall_info[i].fop_hits,
                upcall_info[i].fop_name);
        }
    }

    cli_out(" ");
    cli_out("%12s: %" PRId64 " seconds", "Duration", sec);
    cli_out("%12s: %" PRId64 " bytes", "Data Read", r_count);
    cli_out("%12s: %" PRId64 " bytes", "Data Written", w_count);
    cli_out(" ");
}

static int32_t
gf_cli_profile_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                          void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    dict_t *dict = NULL;
    gf1_cli_stats_op op = GF_CLI_STATS_NONE;
    char key[64] = {0};
    int len;
    int interval = 0;
    int i = 1;
    int32_t brick_count = 0;
    char *volname = NULL;
    char *brick = NULL;
    char str[1024] = {
        0,
    };
    int stats_cleared = 0;
    gf1_cli_info_op info_op = GF_CLI_INFO_NONE;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    gf_log("cli", GF_LOG_DEBUG, "Received resp to profile");
    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    dict = dict_new();

    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);

    if (ret) {
        gf_log("", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
        goto out;
    }

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_vol_profile(dict, rsp.op_ret, rsp.op_errno,
                                         rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    ret = dict_get_int32_sizen(dict, "op", (int32_t *)&op);
    if (ret)
        goto out;

    ret = dict_get_str_sizen(dict, "volname", &volname);
    if (ret)
        goto out;

    if (rsp.op_ret && strcmp(rsp.op_errstr, "")) {
        cli_err("%s", rsp.op_errstr);
    } else {
        switch (op) {
            case GF_CLI_STATS_START:
                cli_out("Starting volume profile on %s has been %s ", volname,
                        (rsp.op_ret) ? "unsuccessful" : "successful");
                break;
            case GF_CLI_STATS_STOP:
                cli_out("Stopping volume profile on %s has been %s ", volname,
                        (rsp.op_ret) ? "unsuccessful" : "successful");
                break;
            case GF_CLI_STATS_INFO:
                break;
            default:
                cli_out("volume profile on %s has been %s ", volname,
                        (rsp.op_ret) ? "unsuccessful" : "successful");
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

    ret = dict_get_int32_sizen(dict, "count", &brick_count);
    if (ret)
        goto out;

    if (!brick_count) {
        cli_err("All bricks of volume %s are down.", volname);
        goto out;
    }

    ret = dict_get_int32_sizen(dict, "info-op", (int32_t *)&info_op);
    if (ret)
        goto out;

    while (i <= brick_count) {
        len = snprintf(key, sizeof(key), "%d-brick", i);
        ret = dict_get_strn(dict, key, len, &brick);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, "Couldn't get brick name");
            goto out;
        }

        ret = dict_get_str_boolean(dict, "nfs", _gf_false);

        if (ret)
            len = snprintf(str, sizeof(str), "NFS Server : %s", brick);
        else
            len = snprintf(str, sizeof(str), "Brick: %s", brick);
        cli_out("%s", str);
        memset(str, '-', len);
        cli_out("%s", str);

        if (GF_CLI_INFO_CLEAR == info_op) {
            len = snprintf(key, sizeof(key), "%d-stats-cleared", i);
            ret = dict_get_int32n(dict, key, len, &stats_cleared);
            if (ret)
                goto out;
            cli_out(stats_cleared ? "Cleared stats."
                                  : "Failed to clear stats.");
        } else {
            len = snprintf(key, sizeof(key), "%d-cumulative", i);
            ret = dict_get_int32n(dict, key, len, &interval);
            if (ret == 0)
                cmd_profile_volume_brick_out(dict, i, interval);

            len = snprintf(key, sizeof(key), "%d-interval", i);
            ret = dict_get_int32n(dict, key, len, &interval);
            if (ret == 0)
                cmd_profile_volume_brick_out(dict, i, interval);
        }
        i++;
    }
    ret = rsp.op_ret;

out:
    if (dict)
        dict_unref(dict);
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int32_t
gf_cli_profile_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    int ret = -1;
    gf_cli_req req = {{
        0,
    }};
    dict_t *dict = NULL;

    GF_ASSERT(frame);
    GF_ASSERT(this);
    GF_ASSERT(data);

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_profile_volume_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_PROFILE_VOLUME, this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    GF_FREE(req.dict.dict_val);
    return ret;
}

static int32_t
gf_cli_top_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    dict_t *dict = NULL;
    gf1_cli_stats_op op = GF_CLI_STATS_NONE;
    char key[256] = {0};
    int keylen;
    int i = 0;
    int32_t brick_count = 0;
    char brick[1024];
    int32_t members = 0;
    char *filename;
    char *bricks;
    uint64_t value = 0;
    int32_t j = 0;
    gf1_cli_top_op top_op = GF_CLI_TOP_NONE;
    uint64_t nr_open = 0;
    uint64_t max_nr_open = 0;
    double throughput = 0;
    double time = 0;
    int32_t time_sec = 0;
    long int time_usec = 0;
    char timestr[GF_TIMESTR_SIZE] = {
        0,
    };
    char *openfd_str = NULL;
    gf_boolean_t nfs = _gf_false;
    gf_boolean_t clear_stats = _gf_false;
    int stats_cleared = 0;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    gf_log("cli", GF_LOG_DEBUG, "Received resp to top");
    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    if (rsp.op_ret) {
        if (strcmp(rsp.op_errstr, ""))
            cli_err("%s", rsp.op_errstr);
        cli_err("volume top unsuccessful");
        ret = rsp.op_ret;
        goto out;
    }

    dict = dict_new();

    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);

    if (ret) {
        gf_log("", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
        goto out;
    }

    ret = dict_get_int32_sizen(dict, "op", (int32_t *)&op);

    if (op != GF_CLI_STATS_TOP) {
        ret = 0;
        goto out;
    }

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_vol_top(dict, rsp.op_ret, rsp.op_errno,
                                     rsp.op_errstr);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        }
        goto out;
    }

    ret = dict_get_int32_sizen(dict, "count", &brick_count);
    if (ret)
        goto out;
    keylen = snprintf(key, sizeof(key), "%d-top-op", 1);
    ret = dict_get_int32n(dict, key, keylen, (int32_t *)&top_op);
    if (ret)
        goto out;

    clear_stats = dict_get_str_boolean(dict, "clear-stats", _gf_false);

    while (i < brick_count) {
        i++;
        keylen = snprintf(brick, sizeof(brick), "%d-brick", i);
        ret = dict_get_strn(dict, brick, keylen, &bricks);
        if (ret)
            goto out;

        nfs = dict_get_str_boolean(dict, "nfs", _gf_false);

        if (clear_stats) {
            keylen = snprintf(key, sizeof(key), "%d-stats-cleared", i);
            ret = dict_get_int32n(dict, key, keylen, &stats_cleared);
            if (ret)
                goto out;
            cli_out(stats_cleared ? "Cleared stats for %s %s"
                                  : "Failed to clear stats for %s %s",
                    nfs ? "NFS server on" : "brick", bricks);
            continue;
        }

        if (nfs)
            cli_out("NFS Server : %s", bricks);
        else
            cli_out("Brick: %s", bricks);

        keylen = snprintf(key, sizeof(key), "%d-members", i);
        ret = dict_get_int32n(dict, key, keylen, &members);

        switch (top_op) {
            case GF_CLI_TOP_OPEN:
                snprintf(key, sizeof(key), "%d-current-open", i);
                ret = dict_get_uint64(dict, key, &nr_open);
                if (ret)
                    break;
                snprintf(key, sizeof(key), "%d-max-open", i);
                ret = dict_get_uint64(dict, key, &max_nr_open);
                if (ret)
                    goto out;
                keylen = snprintf(key, sizeof(key), "%d-max-openfd-time", i);
                ret = dict_get_strn(dict, key, keylen, &openfd_str);
                if (ret)
                    goto out;
                cli_out("Current open fds: %" PRIu64 ", Max open fds: %" PRIu64
                        ", Max openfd time: %s",
                        nr_open, max_nr_open, openfd_str);
            case GF_CLI_TOP_READ:
            case GF_CLI_TOP_WRITE:
            case GF_CLI_TOP_OPENDIR:
            case GF_CLI_TOP_READDIR:
                if (!members) {
                    continue;
                }
                cli_out("Count\t\tfilename\n=======================");
                break;
            case GF_CLI_TOP_READ_PERF:
            case GF_CLI_TOP_WRITE_PERF:
                snprintf(key, sizeof(key), "%d-throughput", i);
                ret = dict_get_double(dict, key, &throughput);
                if (!ret) {
                    snprintf(key, sizeof(key), "%d-time", i);
                    ret = dict_get_double(dict, key, &time);
                }
                if (!ret)
                    cli_out("Throughput %.2f MBps time %.4f secs", throughput,
                            time / 1e6);

                if (!members) {
                    continue;
                }
                cli_out("%*s %-*s %-*s", VOL_TOP_PERF_SPEED_WIDTH, "MBps",
                        VOL_TOP_PERF_FILENAME_DEF_WIDTH, "Filename",
                        VOL_TOP_PERF_TIME_WIDTH, "Time");
                cli_out("%*s %-*s %-*s", VOL_TOP_PERF_SPEED_WIDTH,
                        "====", VOL_TOP_PERF_FILENAME_DEF_WIDTH,
                        "========", VOL_TOP_PERF_TIME_WIDTH, "====");
                break;
            default:
                goto out;
        }

        for (j = 1; j <= members; j++) {
            keylen = snprintf(key, sizeof(key), "%d-filename-%d", i, j);
            ret = dict_get_strn(dict, key, keylen, &filename);
            if (ret)
                break;
            snprintf(key, sizeof(key), "%d-value-%d", i, j);
            ret = dict_get_uint64(dict, key, &value);
            if (ret)
                goto out;
            if (top_op == GF_CLI_TOP_READ_PERF ||
                top_op == GF_CLI_TOP_WRITE_PERF) {
                keylen = snprintf(key, sizeof(key), "%d-time-sec-%d", i, j);
                ret = dict_get_int32n(dict, key, keylen, (int32_t *)&time_sec);
                if (ret)
                    goto out;
                keylen = snprintf(key, sizeof(key), "%d-time-usec-%d", i, j);
                ret = dict_get_int32n(dict, key, keylen, (int32_t *)&time_usec);
                if (ret)
                    goto out;
                gf_time_fmt(timestr, sizeof timestr, time_sec, gf_timefmt_FT);
                snprintf(timestr + strlen(timestr),
                         sizeof timestr - strlen(timestr), ".%ld", time_usec);
                if (strlen(filename) < VOL_TOP_PERF_FILENAME_DEF_WIDTH)
                    cli_out("%*" PRIu64 " %-*s %-*s", VOL_TOP_PERF_SPEED_WIDTH,
                            value, VOL_TOP_PERF_FILENAME_DEF_WIDTH, filename,
                            VOL_TOP_PERF_TIME_WIDTH, timestr);
                else
                    cli_out("%*" PRIu64 " ...%-*s %-*s",
                            VOL_TOP_PERF_SPEED_WIDTH, value,
                            VOL_TOP_PERF_FILENAME_ALT_WIDTH,
                            filename + strlen(filename) -
                                VOL_TOP_PERF_FILENAME_ALT_WIDTH,
                            VOL_TOP_PERF_TIME_WIDTH, timestr);
            } else {
                cli_out("%" PRIu64 "\t\t%s", value, filename);
            }
        }
    }
    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);

    if (dict)
        dict_unref(dict);

    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int32_t
gf_cli_top_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    int ret = -1;
    gf_cli_req req = {{
        0,
    }};
    dict_t *dict = NULL;

    GF_ASSERT(frame);
    GF_ASSERT(this);
    GF_ASSERT(data);

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_top_volume_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_PROFILE_VOLUME, this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    GF_FREE(req.dict.dict_val);
    return ret;
}

static int
gf_cli_getwd_cbk(struct rpc_req *req, struct iovec *iov, int count,
                 void *myframe)
{
    gf1_cli_getwd_rsp rsp = {
        0,
    };
    int ret = -1;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf1_cli_getwd_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    if (rsp.op_ret == -1) {
        cli_err("getwd failed");
        ret = rsp.op_ret;
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to getwd");

    cli_out("%s", rsp.wd);

    ret = 0;

out:
    cli_cmd_broadcast_response(ret);
    if (rsp.wd) {
        free(rsp.wd);
    }

    return ret;
}

static int32_t
gf_cli_getwd(call_frame_t *frame, xlator_t *this, void *data)
{
    int ret = -1;
    gf1_cli_getwd_req req = {
        0,
    };

    GF_ASSERT(frame);
    GF_ASSERT(this);

    if (!frame || !this)
        goto out;

    ret = cli_cmd_submit(NULL, &req, frame, cli_rpc_prog, GLUSTER_CLI_GETWD,
                         NULL, this, gf_cli_getwd_cbk,
                         (xdrproc_t)xdr_gf1_cli_getwd_req);

out:
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    return ret;
}

static void
cli_print_volume_status_mempool(dict_t *dict, char *prefix)
{
    int ret = -1;
    int32_t mempool_count = 0;
    char *name = NULL;
    int32_t hotcount = 0;
    int32_t coldcount = 0;
    uint64_t paddedsizeof = 0;
    uint64_t alloccount = 0;
    int32_t maxalloc = 0;
    uint64_t pool_misses = 0;
    int32_t maxstdalloc = 0;
    char key[128] = {
        /* prefix is really small 'brick%d' really */
        0,
    };
    int keylen;
    int i = 0;

    GF_ASSERT(dict);
    GF_ASSERT(prefix);

    keylen = snprintf(key, sizeof(key), "%s.mempool-count", prefix);
    ret = dict_get_int32n(dict, key, keylen, &mempool_count);
    if (ret)
        goto out;

    cli_out("Mempool Stats\n-------------");
    cli_out("%-30s %9s %9s %12s %10s %8s %8s %12s", "Name", "HotCount",
            "ColdCount", "PaddedSizeof", "AllocCount", "MaxAlloc", "Misses",
            "Max-StdAlloc");
    cli_out("%-30s %9s %9s %12s %10s %8s %8s %12s", "----", "--------",
            "---------", "------------", "----------", "--------", "--------",
            "------------");

    for (i = 0; i < mempool_count; i++) {
        keylen = snprintf(key, sizeof(key), "%s.pool%d.name", prefix, i);
        ret = dict_get_strn(dict, key, keylen, &name);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "%s.pool%d.hotcount", prefix, i);
        ret = dict_get_int32n(dict, key, keylen, &hotcount);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "%s.pool%d.coldcount", prefix, i);
        ret = dict_get_int32n(dict, key, keylen, &coldcount);
        if (ret)
            goto out;

        snprintf(key, sizeof(key), "%s.pool%d.paddedsizeof", prefix, i);
        ret = dict_get_uint64(dict, key, &paddedsizeof);
        if (ret)
            goto out;

        snprintf(key, sizeof(key), "%s.pool%d.alloccount", prefix, i);
        ret = dict_get_uint64(dict, key, &alloccount);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "%s.pool%d.max_alloc", prefix, i);
        ret = dict_get_int32n(dict, key, keylen, &maxalloc);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "%s.pool%d.max-stdalloc", prefix,
                          i);
        ret = dict_get_int32n(dict, key, keylen, &maxstdalloc);
        if (ret)
            goto out;

        snprintf(key, sizeof(key), "%s.pool%d.pool-misses", prefix, i);
        ret = dict_get_uint64(dict, key, &pool_misses);
        if (ret)
            goto out;

        cli_out("%-30s %9d %9d %12" PRIu64 " %10" PRIu64 " %8d %8" PRIu64
                " %12d",
                name, hotcount, coldcount, paddedsizeof, alloccount, maxalloc,
                pool_misses, maxstdalloc);
    }

out:
    return;
}

static void
cli_print_volume_status_mem(dict_t *dict, gf_boolean_t notbrick)
{
    int ret = -1;
    char *volname = NULL;
    char *hostname = NULL;
    char *path = NULL;
    int online = -1;
    char key[64] = {
        0,
    };
    int brick_index_max = -1;
    int other_count = 0;
    int index_max = 0;
    int val = 0;
    int i = 0;

    GF_ASSERT(dict);

    ret = dict_get_str_sizen(dict, "volname", &volname);
    if (ret)
        goto out;
    cli_out("Memory status for volume : %s", volname);

    ret = dict_get_int32_sizen(dict, "brick-index-max", &brick_index_max);
    if (ret)
        goto out;
    ret = dict_get_int32_sizen(dict, "other-count", &other_count);
    if (ret)
        goto out;

    index_max = brick_index_max + other_count;

    for (i = 0; i <= index_max; i++) {
        cli_out("----------------------------------------------");

        snprintf(key, sizeof(key), "brick%d.hostname", i);
        ret = dict_get_str(dict, key, &hostname);
        if (ret)
            continue;
        snprintf(key, sizeof(key), "brick%d.path", i);
        ret = dict_get_str(dict, key, &path);
        if (ret)
            continue;
        if (notbrick)
            cli_out("%s : %s", hostname, path);
        else
            cli_out("Brick : %s:%s", hostname, path);

        snprintf(key, sizeof(key), "brick%d.status", i);
        ret = dict_get_int32(dict, key, &online);
        if (ret)
            goto out;
        if (!online) {
            if (notbrick)
                cli_out("%s is offline", hostname);
            else
                cli_out("Brick is offline");
            continue;
        }

        cli_out("Mallinfo\n--------");

        snprintf(key, sizeof(key), "brick%d.mallinfo.arena", i);
        ret = dict_get_int32(dict, key, &val);
        if (ret)
            goto out;
        cli_out("%-8s : %d", "Arena", val);

        snprintf(key, sizeof(key), "brick%d.mallinfo.ordblks", i);
        ret = dict_get_int32(dict, key, &val);
        if (ret)
            goto out;
        cli_out("%-8s : %d", "Ordblks", val);

        snprintf(key, sizeof(key), "brick%d.mallinfo.smblks", i);
        ret = dict_get_int32(dict, key, &val);
        if (ret)
            goto out;
        cli_out("%-8s : %d", "Smblks", val);

        snprintf(key, sizeof(key), "brick%d.mallinfo.hblks", i);
        ret = dict_get_int32(dict, key, &val);
        if (ret)
            goto out;
        cli_out("%-8s : %d", "Hblks", val);

        snprintf(key, sizeof(key), "brick%d.mallinfo.hblkhd", i);
        ret = dict_get_int32(dict, key, &val);
        if (ret)
            goto out;
        cli_out("%-8s : %d", "Hblkhd", val);

        snprintf(key, sizeof(key), "brick%d.mallinfo.usmblks", i);
        ret = dict_get_int32(dict, key, &val);
        if (ret)
            goto out;
        cli_out("%-8s : %d", "Usmblks", val);

        snprintf(key, sizeof(key), "brick%d.mallinfo.fsmblks", i);
        ret = dict_get_int32(dict, key, &val);
        if (ret)
            goto out;
        cli_out("%-8s : %d", "Fsmblks", val);

        snprintf(key, sizeof(key), "brick%d.mallinfo.uordblks", i);
        ret = dict_get_int32(dict, key, &val);
        if (ret)
            goto out;
        cli_out("%-8s : %d", "Uordblks", val);

        snprintf(key, sizeof(key), "brick%d.mallinfo.fordblks", i);
        ret = dict_get_int32(dict, key, &val);
        if (ret)
            goto out;
        cli_out("%-8s : %d", "Fordblks", val);

        snprintf(key, sizeof(key), "brick%d.mallinfo.keepcost", i);
        ret = dict_get_int32(dict, key, &val);
        if (ret)
            goto out;
        cli_out("%-8s : %d", "Keepcost", val);

        cli_out(" ");
        snprintf(key, sizeof(key), "brick%d", i);
        cli_print_volume_status_mempool(dict, key);
    }
out:
    cli_out("----------------------------------------------\n");
    return;
}

static void
cli_print_volume_status_client_list(dict_t *dict, gf_boolean_t notbrick)
{
    int ret = -1;
    char *volname = NULL;
    int client_count = 0;
    int current_count = 0;
    char key[64] = {
        0,
    };
    int i = 0;
    int total = 0;
    char *name = NULL;
    gf_boolean_t is_fuse_done = _gf_false;
    gf_boolean_t is_gfapi_done = _gf_false;
    gf_boolean_t is_rebalance_done = _gf_false;
    gf_boolean_t is_glustershd_done = _gf_false;
    gf_boolean_t is_quotad_done = _gf_false;
    gf_boolean_t is_snapd_done = _gf_false;

    GF_ASSERT(dict);

    ret = dict_get_str_sizen(dict, "volname", &volname);
    if (ret)
        goto out;
    cli_out("Client connections for volume %s", volname);

    ret = dict_get_int32_sizen(dict, "client-count", &client_count);
    if (ret)
        goto out;

    cli_out("%-48s %15s", "Name", "count");
    cli_out("%-48s %15s", "-----", "------");
    for (i = 0; i < client_count; i++) {
        name = NULL;
        snprintf(key, sizeof(key), "client%d.name", i);
        ret = dict_get_str(dict, key, &name);

        if (!strncmp(name, "fuse", 4)) {
            if (!is_fuse_done) {
                is_fuse_done = _gf_true;
                ret = dict_get_int32_sizen(dict, "fuse-count", &current_count);
                if (ret)
                    goto out;
                total = total + current_count;
                goto print;
            }
            continue;
        } else if (!strncmp(name, "gfapi", 5)) {
            if (!is_gfapi_done) {
                is_gfapi_done = _gf_true;
                ret = dict_get_int32_sizen(dict, "gfapi-count", &current_count);
                if (ret)
                    goto out;
                total = total + current_count;
                goto print;
            }
            continue;
        } else if (!strcmp(name, "rebalance")) {
            if (!is_rebalance_done) {
                is_rebalance_done = _gf_true;
                ret = dict_get_int32_sizen(dict, "rebalance-count",
                                           &current_count);
                if (ret)
                    goto out;
                total = total + current_count;
                goto print;
            }
            continue;
        } else if (!strcmp(name, "glustershd")) {
            if (!is_glustershd_done) {
                is_glustershd_done = _gf_true;
                ret = dict_get_int32_sizen(dict, "glustershd-count",
                                           &current_count);
                if (ret)
                    goto out;
                total = total + current_count;
                goto print;
            }
            continue;
        } else if (!strcmp(name, "quotad")) {
            if (!is_quotad_done) {
                is_quotad_done = _gf_true;
                ret = dict_get_int32_sizen(dict, "quotad-count",
                                           &current_count);
                if (ret)
                    goto out;
                total = total + current_count;
                goto print;
            }
            continue;
        } else if (!strcmp(name, "snapd")) {
            if (!is_snapd_done) {
                is_snapd_done = _gf_true;
                ret = dict_get_int32_sizen(dict, "snapd-count", &current_count);
                if (ret)
                    goto out;
                total = total + current_count;
                goto print;
            }
            continue;
        }

    print:
        cli_out("%-48s %15d", name, current_count);
    }
out:
    cli_out("\ntotal clients for volume %s : %d ", volname, total);
    cli_out(
        "-----------------------------------------------------------------\n");
    return;
}

static void
cli_print_volume_status_clients(dict_t *dict, gf_boolean_t notbrick)
{
    int ret = -1;
    char *volname = NULL;
    int brick_index_max = -1;
    int other_count = 0;
    int index_max = 0;
    char *hostname = NULL;
    char *path = NULL;
    int online = -1;
    int client_count = 0;
    char *clientname = NULL;
    uint64_t bytesread = 0;
    uint64_t byteswrite = 0;
    uint32_t opversion = 0;
    char key[128] = {
        0,
    };
    int i = 0;
    int j = 0;

    GF_ASSERT(dict);

    ret = dict_get_str_sizen(dict, "volname", &volname);
    if (ret)
        goto out;
    cli_out("Client connections for volume %s", volname);

    ret = dict_get_int32_sizen(dict, "brick-index-max", &brick_index_max);
    if (ret)
        goto out;
    ret = dict_get_int32_sizen(dict, "other-count", &other_count);
    if (ret)
        goto out;

    index_max = brick_index_max + other_count;

    for (i = 0; i <= index_max; i++) {
        hostname = NULL;
        path = NULL;
        online = -1;
        client_count = 0;
        clientname = NULL;
        bytesread = 0;
        byteswrite = 0;

        cli_out("----------------------------------------------");

        snprintf(key, sizeof(key), "brick%d.hostname", i);
        ret = dict_get_str(dict, key, &hostname);

        snprintf(key, sizeof(key), "brick%d.path", i);
        ret = dict_get_str(dict, key, &path);

        if (hostname && path) {
            if (notbrick)
                cli_out("%s : %s", hostname, path);
            else
                cli_out("Brick : %s:%s", hostname, path);
        }
        snprintf(key, sizeof(key), "brick%d.status", i);
        ret = dict_get_int32(dict, key, &online);
        if (!online) {
            if (notbrick)
                cli_out("%s is offline", hostname);
            else
                cli_out("Brick is offline");
            continue;
        }

        snprintf(key, sizeof(key), "brick%d.clientcount", i);
        ret = dict_get_int32(dict, key, &client_count);

        if (hostname && path)
            cli_out("Clients connected : %d", client_count);
        if (client_count == 0)
            continue;

        cli_out("%-48s %15s %15s %15s", "Hostname", "BytesRead", "BytesWritten",
                "OpVersion");
        cli_out("%-48s %15s %15s %15s", "--------", "---------", "------------",
                "---------");
        for (j = 0; j < client_count; j++) {
            snprintf(key, sizeof(key), "brick%d.client%d.hostname", i, j);
            ret = dict_get_str(dict, key, &clientname);

            snprintf(key, sizeof(key), "brick%d.client%d.bytesread", i, j);
            ret = dict_get_uint64(dict, key, &bytesread);

            snprintf(key, sizeof(key), "brick%d.client%d.byteswrite", i, j);
            ret = dict_get_uint64(dict, key, &byteswrite);

            snprintf(key, sizeof(key), "brick%d.client%d.opversion", i, j);
            ret = dict_get_uint32(dict, key, &opversion);

            cli_out("%-48s %15" PRIu64 " %15" PRIu64 " %15" PRIu32, clientname,
                    bytesread, byteswrite, opversion);
        }
    }
out:
    cli_out("----------------------------------------------\n");
    return;
}

#ifdef DEBUG /* this function is only used in debug */
static void
cli_print_volume_status_inode_entry(dict_t *dict, char *prefix)
{
    int ret = -1;
    char key[1024] = {
        0,
    };
    char *gfid = NULL;
    uint64_t nlookup = 0;
    uint32_t ref = 0;
    int ia_type = 0;
    char inode_type;

    GF_ASSERT(dict);
    GF_ASSERT(prefix);

    snprintf(key, sizeof(key), "%s.gfid", prefix);
    ret = dict_get_str(dict, key, &gfid);
    if (ret)
        goto out;

    snprintf(key, sizeof(key), "%s.nlookup", prefix);
    ret = dict_get_uint64(dict, key, &nlookup);
    if (ret)
        goto out;

    snprintf(key, sizeof(key), "%s.ref", prefix);
    ret = dict_get_uint32(dict, key, &ref);
    if (ret)
        goto out;

    snprintf(key, sizeof(key), "%s.ia_type", prefix);
    ret = dict_get_int32(dict, key, &ia_type);
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

    cli_out("%-40s %14" PRIu64 " %14" PRIu32 " %9c", gfid, nlookup, ref,
            inode_type);

out:
    return;
}
#endif

static void
cli_print_volume_status_itables(dict_t *dict, char *prefix)
{
    int ret = -1;
    char key[1024] = {
        0,
    };
    uint32_t active_size = 0;
    uint32_t lru_size = 0;
    uint32_t purge_size = 0;
    uint32_t lru_limit = 0;
#ifdef DEBUG
    int i = 0;
#endif
    GF_ASSERT(dict);
    GF_ASSERT(prefix);

    snprintf(key, sizeof(key), "%s.lru_limit", prefix);
    ret = dict_get_uint32(dict, key, &lru_limit);
    if (ret)
        goto out;
    cli_out("LRU limit     : %u", lru_limit);

    snprintf(key, sizeof(key), "%s.active_size", prefix);
    ret = dict_get_uint32(dict, key, &active_size);
    if (ret)
        goto out;

#ifdef DEBUG
    if (active_size != 0) {
        cli_out("Active inodes:");
        cli_out("%-40s %14s %14s %9s", "GFID", "Lookups", "Ref", "IA type");
        cli_out("%-40s %14s %14s %9s", "----", "-------", "---", "-------");
    }
    for (i = 0; i < active_size; i++) {
        snprintf(key, sizeof(key), "%s.active%d", prefix, i);
        cli_print_volume_status_inode_entry(dict, key);
    }
    cli_out(" ");
#else
    cli_out("Active Inodes : %u", active_size);

#endif

    snprintf(key, sizeof(key), "%s.lru_size", prefix);
    ret = dict_get_uint32(dict, key, &lru_size);
    if (ret)
        goto out;

#ifdef DEBUG
    if (lru_size != 0) {
        cli_out("LRU inodes:");
        cli_out("%-40s %14s %14s %9s", "GFID", "Lookups", "Ref", "IA type");
        cli_out("%-40s %14s %14s %9s", "----", "-------", "---", "-------");
    }
    for (i = 0; i < lru_size; i++) {
        snprintf(key, sizeof(key), "%s.lru%d", prefix, i);
        cli_print_volume_status_inode_entry(dict, key);
    }
    cli_out(" ");
#else
    cli_out("LRU Inodes    : %u", lru_size);
#endif

    snprintf(key, sizeof(key), "%s.purge_size", prefix);
    ret = dict_get_uint32(dict, key, &purge_size);
    if (ret)
        goto out;
#ifdef DEBUG
    if (purge_size != 0) {
        cli_out("Purged inodes:");
        cli_out("%-40s %14s %14s %9s", "GFID", "Lookups", "Ref", "IA type");
        cli_out("%-40s %14s %14s %9s", "----", "-------", "---", "-------");
    }
    for (i = 0; i < purge_size; i++) {
        snprintf(key, sizeof(key), "%s.purge%d", prefix, i);
        cli_print_volume_status_inode_entry(dict, key);
    }
#else
    cli_out("Purge Inodes  : %u", purge_size);
#endif

out:
    return;
}

static void
cli_print_volume_status_inode(dict_t *dict, gf_boolean_t notbrick)
{
    int ret = -1;
    char *volname = NULL;
    int brick_index_max = -1;
    int other_count = 0;
    int index_max = 0;
    char *hostname = NULL;
    char *path = NULL;
    int online = -1;
    int conn_count = 0;
    char key[64] = {
        0,
    };
    int i = 0;
    int j = 0;

    GF_ASSERT(dict);

    ret = dict_get_str_sizen(dict, "volname", &volname);
    if (ret)
        goto out;
    cli_out("Inode tables for volume %s", volname);

    ret = dict_get_int32_sizen(dict, "brick-index-max", &brick_index_max);
    if (ret)
        goto out;
    ret = dict_get_int32_sizen(dict, "other-count", &other_count);
    if (ret)
        goto out;

    index_max = brick_index_max + other_count;

    for (i = 0; i <= index_max; i++) {
        cli_out("----------------------------------------------");

        snprintf(key, sizeof(key), "brick%d.hostname", i);
        ret = dict_get_str(dict, key, &hostname);
        if (ret)
            goto out;
        snprintf(key, sizeof(key), "brick%d.path", i);
        ret = dict_get_str(dict, key, &path);
        if (ret)
            goto out;
        if (notbrick)
            cli_out("%s : %s", hostname, path);
        else
            cli_out("Brick : %s:%s", hostname, path);

        snprintf(key, sizeof(key), "brick%d.status", i);
        ret = dict_get_int32(dict, key, &online);
        if (ret)
            goto out;
        if (!online) {
            if (notbrick)
                cli_out("%s is offline", hostname);
            else
                cli_out("Brick is offline");
            continue;
        }

        snprintf(key, sizeof(key), "brick%d.conncount", i);
        ret = dict_get_int32(dict, key, &conn_count);
        if (ret)
            goto out;

        for (j = 0; j < conn_count; j++) {
            if (conn_count > 1)
                cli_out("Connection %d:", j + 1);

            snprintf(key, sizeof(key), "brick%d.conn%d.itable", i, j);
            cli_print_volume_status_itables(dict, key);
            cli_out(" ");
        }
    }
out:
    cli_out("----------------------------------------------");
    return;
}

void
cli_print_volume_status_fdtable(dict_t *dict, char *prefix)
{
    int ret = -1;
    char key[256] = {
        0,
    };
    int refcount = 0;
    uint32_t maxfds = 0;
    int firstfree = 0;
    int openfds = 0;
    int fd_pid = 0;
    int fd_refcount = 0;
    int fd_flags = 0;
    int i = 0;

    GF_ASSERT(dict);
    GF_ASSERT(prefix);

    snprintf(key, sizeof(key), "%s.refcount", prefix);
    ret = dict_get_int32(dict, key, &refcount);
    if (ret)
        goto out;

    snprintf(key, sizeof(key), "%s.maxfds", prefix);
    ret = dict_get_uint32(dict, key, &maxfds);
    if (ret)
        goto out;

    snprintf(key, sizeof(key), "%s.firstfree", prefix);
    ret = dict_get_int32(dict, key, &firstfree);
    if (ret)
        goto out;

    cli_out("RefCount = %d  MaxFDs = %d  FirstFree = %d", refcount, maxfds,
            firstfree);

    snprintf(key, sizeof(key), "%s.openfds", prefix);
    ret = dict_get_int32(dict, key, &openfds);
    if (ret)
        goto out;
    if (0 == openfds) {
        cli_err("No open fds");
        goto out;
    }

    cli_out("%-19s %-19s %-19s %-19s", "FD Entry", "PID", "RefCount", "Flags");
    cli_out("%-19s %-19s %-19s %-19s", "--------", "---", "--------", "-----");

    for (i = 0; i < maxfds; i++) {
        snprintf(key, sizeof(key), "%s.fdentry%d.pid", prefix, i);
        ret = dict_get_int32(dict, key, &fd_pid);
        if (ret)
            continue;

        snprintf(key, sizeof(key), "%s.fdentry%d.refcount", prefix, i);
        ret = dict_get_int32(dict, key, &fd_refcount);
        if (ret)
            continue;

        snprintf(key, sizeof(key), "%s.fdentry%d.flags", prefix, i);
        ret = dict_get_int32(dict, key, &fd_flags);
        if (ret)
            continue;

        cli_out("%-19d %-19d %-19d %-19d", i, fd_pid, fd_refcount, fd_flags);
    }

out:
    return;
}

static void
cli_print_volume_status_fd(dict_t *dict, gf_boolean_t notbrick)
{
    int ret = -1;
    char *volname = NULL;
    int brick_index_max = -1;
    int other_count = 0;
    int index_max = 0;
    char *hostname = NULL;
    char *path = NULL;
    int online = -1;
    int conn_count = 0;
    char key[64] = {
        0,
    };
    int i = 0;
    int j = 0;

    GF_ASSERT(dict);

    ret = dict_get_str_sizen(dict, "volname", &volname);
    if (ret)
        goto out;
    cli_out("FD tables for volume %s", volname);

    ret = dict_get_int32_sizen(dict, "brick-index-max", &brick_index_max);
    if (ret)
        goto out;
    ret = dict_get_int32_sizen(dict, "other-count", &other_count);
    if (ret)
        goto out;

    index_max = brick_index_max + other_count;

    for (i = 0; i <= index_max; i++) {
        cli_out("----------------------------------------------");

        snprintf(key, sizeof(key), "brick%d.hostname", i);
        ret = dict_get_str(dict, key, &hostname);
        if (ret)
            goto out;
        snprintf(key, sizeof(key), "brick%d.path", i);
        ret = dict_get_str(dict, key, &path);
        if (ret)
            goto out;

        if (notbrick)
            cli_out("%s : %s", hostname, path);
        else
            cli_out("Brick : %s:%s", hostname, path);

        snprintf(key, sizeof(key), "brick%d.status", i);
        ret = dict_get_int32(dict, key, &online);
        if (ret)
            goto out;
        if (!online) {
            if (notbrick)
                cli_out("%s is offline", hostname);
            else
                cli_out("Brick is offline");
            continue;
        }

        snprintf(key, sizeof(key), "brick%d.conncount", i);
        ret = dict_get_int32(dict, key, &conn_count);
        if (ret)
            goto out;

        for (j = 0; j < conn_count; j++) {
            cli_out("Connection %d:", j + 1);

            snprintf(key, sizeof(key), "brick%d.conn%d.fdtable", i, j);
            cli_print_volume_status_fdtable(dict, key);
            cli_out(" ");
        }
    }
out:
    cli_out("----------------------------------------------");
    return;
}

static void
cli_print_volume_status_call_frame(dict_t *dict, char *prefix)
{
    int ret = -1;
    char key[1024] = {
        0,
    };
    int ref_count = 0;
    char *translator = 0;
    int complete = 0;
    char *parent = NULL;
    char *wind_from = NULL;
    char *wind_to = NULL;
    char *unwind_from = NULL;
    char *unwind_to = NULL;

    if (!dict || !prefix)
        return;

    snprintf(key, sizeof(key), "%s.refcount", prefix);
    ret = dict_get_int32(dict, key, &ref_count);
    if (ret)
        return;

    snprintf(key, sizeof(key), "%s.translator", prefix);
    ret = dict_get_str(dict, key, &translator);
    if (ret)
        return;

    snprintf(key, sizeof(key), "%s.complete", prefix);
    ret = dict_get_int32(dict, key, &complete);
    if (ret)
        return;

    cli_out("  Ref Count   = %d", ref_count);
    cli_out("  Translator  = %s", translator);
    cli_out("  Completed   = %s", (complete ? "Yes" : "No"));

    snprintf(key, sizeof(key), "%s.parent", prefix);
    ret = dict_get_str(dict, key, &parent);
    if (!ret)
        cli_out("  Parent      = %s", parent);

    snprintf(key, sizeof(key), "%s.windfrom", prefix);
    ret = dict_get_str(dict, key, &wind_from);
    if (!ret)
        cli_out("  Wind From   = %s", wind_from);

    snprintf(key, sizeof(key), "%s.windto", prefix);
    ret = dict_get_str(dict, key, &wind_to);
    if (!ret)
        cli_out("  Wind To     = %s", wind_to);

    snprintf(key, sizeof(key), "%s.unwindfrom", prefix);
    ret = dict_get_str(dict, key, &unwind_from);
    if (!ret)
        cli_out("  Unwind From = %s", unwind_from);

    snprintf(key, sizeof(key), "%s.unwindto", prefix);
    ret = dict_get_str(dict, key, &unwind_to);
    if (!ret)
        cli_out("  Unwind To   = %s", unwind_to);
}

static void
cli_print_volume_status_call_stack(dict_t *dict, char *prefix)
{
    int ret = -1;
    char key[256] = {
        0,
    };
    int uid = 0;
    int gid = 0;
    int pid = 0;
    uint64_t unique = 0;
    // char            *op = NULL;
    int count = 0;
    int i = 0;

    if (!prefix)
        return;

    snprintf(key, sizeof(key), "%s.uid", prefix);
    ret = dict_get_int32(dict, key, &uid);
    if (ret)
        return;

    snprintf(key, sizeof(key), "%s.gid", prefix);
    ret = dict_get_int32(dict, key, &gid);
    if (ret)
        return;

    snprintf(key, sizeof(key), "%s.pid", prefix);
    ret = dict_get_int32(dict, key, &pid);
    if (ret)
        return;

    snprintf(key, sizeof(key), "%s.unique", prefix);
    ret = dict_get_uint64(dict, key, &unique);
    if (ret)
        return;

    /*
    snprintf (key, sizeof (key), "%s.op", prefix);
    ret = dict_get_str (dict, key, &op);
    if (ret)
            return;
    */

    snprintf(key, sizeof(key), "%s.count", prefix);
    ret = dict_get_int32(dict, key, &count);
    if (ret)
        return;

    cli_out(" UID    : %d", uid);
    cli_out(" GID    : %d", gid);
    cli_out(" PID    : %d", pid);
    cli_out(" Unique : %" PRIu64, unique);
    // cli_out ("\tOp     : %s", op);
    cli_out(" Frames : %d", count);

    for (i = 0; i < count; i++) {
        cli_out(" Frame %d", i + 1);

        snprintf(key, sizeof(key), "%s.frame%d", prefix, i);
        cli_print_volume_status_call_frame(dict, key);
    }

    cli_out(" ");
}

static void
cli_print_volume_status_callpool(dict_t *dict, gf_boolean_t notbrick)
{
    int ret = -1;
    char *volname = NULL;
    int brick_index_max = -1;
    int other_count = 0;
    int index_max = 0;
    char *hostname = NULL;
    char *path = NULL;
    int online = -1;
    int call_count = 0;
    char key[64] = {
        0,
    };
    int i = 0;
    int j = 0;

    GF_ASSERT(dict);

    ret = dict_get_str_sizen(dict, "volname", &volname);
    if (ret)
        goto out;
    cli_out("Pending calls for volume %s", volname);

    ret = dict_get_int32_sizen(dict, "brick-index-max", &brick_index_max);
    if (ret)
        goto out;
    ret = dict_get_int32_sizen(dict, "other-count", &other_count);
    if (ret)
        goto out;

    index_max = brick_index_max + other_count;

    for (i = 0; i <= index_max; i++) {
        cli_out("----------------------------------------------");

        snprintf(key, sizeof(key), "brick%d.hostname", i);
        ret = dict_get_str(dict, key, &hostname);
        if (ret)
            goto out;
        snprintf(key, sizeof(key), "brick%d.path", i);
        ret = dict_get_str(dict, key, &path);
        if (ret)
            goto out;

        if (notbrick)
            cli_out("%s : %s", hostname, path);
        else
            cli_out("Brick : %s:%s", hostname, path);

        snprintf(key, sizeof(key), "brick%d.status", i);
        ret = dict_get_int32(dict, key, &online);
        if (ret)
            goto out;
        if (!online) {
            if (notbrick)
                cli_out("%s is offline", hostname);
            else
                cli_out("Brick is offline");
            continue;
        }

        snprintf(key, sizeof(key), "brick%d.callpool.count", i);
        ret = dict_get_int32(dict, key, &call_count);
        if (ret)
            goto out;
        cli_out("Pending calls: %d", call_count);

        if (0 == call_count)
            continue;

        for (j = 0; j < call_count; j++) {
            cli_out("Call Stack%d", j + 1);

            snprintf(key, sizeof(key), "brick%d.callpool.stack%d", i, j);
            cli_print_volume_status_call_stack(dict, key);
        }
    }

out:
    cli_out("----------------------------------------------");
    return;
}

static void
cli_print_volume_status_tasks(dict_t *dict)
{
    int ret = -1;
    int i = 0;
    int j = 0;
    int count = 0;
    int task_count = 0;
    int status = 0;
    char *op = NULL;
    char *task_id_str = NULL;
    char *volname = NULL;
    char key[64] = {
        0,
    };
    char task[32] = {
        0,
    };
    char *brick = NULL;

    ret = dict_get_int32_sizen(dict, "tasks", &task_count);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Failed to get tasks count");
        return;
    }

    ret = dict_get_str_sizen(dict, "volname", &volname);
    if (ret)
        goto out;

    cli_out("Task Status of Volume %s", volname);
    cli_print_line(CLI_BRICK_STATUS_LINE_LEN);

    if (task_count == 0) {
        cli_out("There are no active volume tasks");
        cli_out(" ");
        return;
    }

    for (i = 0; i < task_count; i++) {
        snprintf(key, sizeof(key), "task%d.type", i);
        ret = dict_get_str(dict, key, &op);
        if (ret)
            return;
        cli_out("%-20s : %-20s", "Task", op);

        snprintf(key, sizeof(key), "task%d.id", i);
        ret = dict_get_str(dict, key, &task_id_str);
        if (ret)
            return;
        cli_out("%-20s : %-20s", "ID", task_id_str);

        snprintf(key, sizeof(key), "task%d.status", i);
        ret = dict_get_int32(dict, key, &status);
        if (ret)
            return;

        snprintf(task, sizeof(task), "task%d", i);

        if (!strcmp(op, "Remove brick")) {
            snprintf(key, sizeof(key), "%s.count", task);
            ret = dict_get_int32(dict, key, &count);
            if (ret)
                goto out;

            cli_out("%-20s", "Removed bricks:");

            for (j = 1; j <= count; j++) {
                snprintf(key, sizeof(key), "%s.brick%d", task, j);
                ret = dict_get_str(dict, key, &brick);
                if (ret)
                    goto out;

                cli_out("%-20s", brick);
            }
        }
        cli_out("%-20s : %-20s", "Status", cli_vol_task_status_str[status]);
        cli_out(" ");
    }

out:
    return;
}

static int
gf_cli_status_cbk(struct rpc_req *req, struct iovec *iov, int count,
                  void *myframe)
{
    int ret = -1;
    int brick_index_max = -1;
    int other_count = 0;
    int index_max = 0;
    int i = 0;
    int type = -1;
    int hot_brick_count = -1;
    int pid = -1;
    uint32_t cmd = 0;
    gf_boolean_t notbrick = _gf_false;
    char key[64] = {
        0,
    };
    char *hostname = NULL;
    char *path = NULL;
    char *volname = NULL;
    dict_t *dict = NULL;
    gf_cli_rsp rsp = {
        0,
    };
    cli_volume_status_t status = {0};
    cli_local_t *local = NULL;
    gf_boolean_t wipe_local = _gf_false;
    char msg[1024] = {
        0,
    };

    GF_ASSERT(myframe);

    if (req->rpc_status == -1)
        goto out;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    gf_log("cli", GF_LOG_DEBUG, "Received response to status cmd");

    local = ((call_frame_t *)myframe)->local;
    if (!local) {
        local = cli_local_get();
        if (!local) {
            ret = -1;
            gf_log("cli", GF_LOG_ERROR, "Failed to get local");
            goto out;
        }
        wipe_local = _gf_true;
    }

    if (rsp.op_ret) {
        if (strcmp(rsp.op_errstr, ""))
            snprintf(msg, sizeof(msg), "%s", rsp.op_errstr);
        else
            snprintf(msg, sizeof(msg),
                     "Unable to obtain volume status information.");

        if (global_state->mode & GLUSTER_MODE_XML) {
            if (!local->all)
                cli_xml_output_str("volStatus", msg, rsp.op_ret, rsp.op_errno,
                                   rsp.op_errstr);
            ret = 0;
            goto out;
        }

        cli_err("%s", msg);
        if (local && local->all) {
            ret = 0;
            cli_out(" ");
        } else
            ret = -1;

        goto out;
    }

    dict = dict_new();
    if (!dict) {
        gf_log(THIS->name, GF_LOG_ERROR, "Failed to create the dict");
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);
    if (ret)
        goto out;

    ret = dict_get_uint32(dict, "cmd", &cmd);
    if (ret)
        goto out;

    if ((cmd & GF_CLI_STATUS_ALL)) {
        if (local && local->dict) {
            dict_ref(dict);
            ret = dict_set_static_ptr(local->dict, "rsp-dict", dict);
            ret = 0;
        } else {
            gf_log("cli", GF_LOG_ERROR, "local not found");
            ret = -1;
        }
        goto out;
    }

    if (global_state->mode & GLUSTER_MODE_XML) {
        if (!local->all) {
            ret = cli_xml_output_vol_status_begin(local, rsp.op_ret,
                                                  rsp.op_errno, rsp.op_errstr);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, XML_ERROR);
                goto xml_end;
            }
        }
        if (cmd & GF_CLI_STATUS_TASKS) {
            ret = cli_xml_output_vol_status_tasks_detail(local, dict);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, "Error outputting to xml");
                goto xml_end;
            }
        } else {
            ret = cli_xml_output_vol_status(local, dict);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, XML_ERROR);
                goto xml_end;
            }
        }

    xml_end:
        if (!local->all) {
            ret = cli_xml_output_vol_status_end(local);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, XML_ERROR);
            }
        }
        goto out;
    }

    if ((cmd & GF_CLI_STATUS_NFS) || (cmd & GF_CLI_STATUS_SHD) ||
        (cmd & GF_CLI_STATUS_QUOTAD) || (cmd & GF_CLI_STATUS_SNAPD) ||
        (cmd & GF_CLI_STATUS_BITD) || (cmd & GF_CLI_STATUS_SCRUB))
        notbrick = _gf_true;

    switch (cmd & GF_CLI_STATUS_MASK) {
        case GF_CLI_STATUS_MEM:
            cli_print_volume_status_mem(dict, notbrick);
            goto cont;
            break;
        case GF_CLI_STATUS_CLIENTS:
            cli_print_volume_status_clients(dict, notbrick);
            goto cont;
            break;
        case GF_CLI_STATUS_CLIENT_LIST:
            cli_print_volume_status_client_list(dict, notbrick);
            goto cont;
            break;
        case GF_CLI_STATUS_INODE:
            cli_print_volume_status_inode(dict, notbrick);
            goto cont;
            break;
        case GF_CLI_STATUS_FD:
            cli_print_volume_status_fd(dict, notbrick);
            goto cont;
            break;
        case GF_CLI_STATUS_CALLPOOL:
            cli_print_volume_status_callpool(dict, notbrick);
            goto cont;
            break;
        case GF_CLI_STATUS_TASKS:
            cli_print_volume_status_tasks(dict);
            goto cont;
            break;
        default:
            break;
    }

    ret = dict_get_str_sizen(dict, "volname", &volname);
    if (ret)
        goto out;

    ret = dict_get_int32_sizen(dict, "brick-index-max", &brick_index_max);
    if (ret)
        goto out;

    ret = dict_get_int32_sizen(dict, "other-count", &other_count);
    if (ret)
        goto out;

    index_max = brick_index_max + other_count;

    ret = dict_get_int32_sizen(dict, "type", &type);
    if (ret)
        goto out;

    ret = dict_get_int32_sizen(dict, "hot_brick_count", &hot_brick_count);
    if (ret)
        goto out;

    cli_out("Status of volume: %s", volname);

    if ((cmd & GF_CLI_STATUS_DETAIL) == 0) {
        cli_out("%-*s %s  %s  %s  %s", CLI_VOL_STATUS_BRICK_LEN,
                "Gluster process", "TCP Port", "RDMA Port", "Online", "Pid");
        cli_print_line(CLI_BRICK_STATUS_LINE_LEN);
    }

    status.brick = GF_MALLOC(PATH_MAX + 256, gf_common_mt_strdup);
    if (!status.brick) {
        errno = ENOMEM;
        ret = -1;
        goto out;
    }

    for (i = 0; i <= index_max; i++) {
        status.rdma_port = 0;

        snprintf(key, sizeof(key), "brick%d.hostname", i);
        ret = dict_get_str(dict, key, &hostname);
        if (ret)
            continue;

        snprintf(key, sizeof(key), "brick%d.path", i);
        ret = dict_get_str(dict, key, &path);
        if (ret)
            continue;

        /* Brick/not-brick is handled separately here as all
         * types of nodes are contained in the default output
         */
        status.brick[0] = '\0';
        if (!strcmp(hostname, "NFS Server") ||
            !strcmp(hostname, "Self-heal Daemon") ||
            !strcmp(hostname, "Quota Daemon") ||
            !strcmp(hostname, "Snapshot Daemon") ||
            !strcmp(hostname, "Scrubber Daemon") ||
            !strcmp(hostname, "Bitrot Daemon"))
            snprintf(status.brick, PATH_MAX + 255, "%s on %s", hostname, path);
        else {
            snprintf(key, sizeof(key), "brick%d.rdma_port", i);
            ret = dict_get_int32(dict, key, &(status.rdma_port));
            if (ret)
                continue;
            snprintf(status.brick, PATH_MAX + 255, "Brick %s:%s", hostname,
                     path);
        }

        snprintf(key, sizeof(key), "brick%d.port", i);
        ret = dict_get_int32(dict, key, &(status.port));
        if (ret)
            continue;

        snprintf(key, sizeof(key), "brick%d.status", i);
        ret = dict_get_int32(dict, key, &(status.online));
        if (ret)
            continue;

        snprintf(key, sizeof(key), "brick%d.pid", i);
        ret = dict_get_int32(dict, key, &pid);
        if (ret)
            continue;
        if (pid == -1)
            ret = gf_asprintf(&(status.pid_str), "%s", "N/A");
        else
            ret = gf_asprintf(&(status.pid_str), "%d", pid);

        if (ret == -1)
            goto out;

        if ((cmd & GF_CLI_STATUS_DETAIL)) {
            ret = cli_get_detail_status(dict, i, &status);
            if (ret)
                goto out;
            cli_print_line(CLI_BRICK_STATUS_LINE_LEN);
            cli_print_detailed_status(&status);
        } else {
            cli_print_brick_status(&status);
        }

        /* Allocatated memory using gf_asprintf*/
        GF_FREE(status.pid_str);
    }
    cli_out(" ");

    if ((cmd & GF_CLI_STATUS_MASK) == GF_CLI_STATUS_NONE)
        cli_print_volume_status_tasks(dict);
cont:
    ret = rsp.op_ret;

out:
    if (dict)
        dict_unref(dict);
    GF_FREE(status.brick);
    if (local && wipe_local) {
        cli_local_wipe(local);
    }

    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int32_t
gf_cli_status_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = -1;
    dict_t *dict = NULL;

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_status_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_STATUS_VOLUME, this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    GF_FREE(req.dict.dict_val);
    return ret;
}

static int
gf_cli_status_volume_all(call_frame_t *frame, xlator_t *this, void *data)
{
    int i = 0;
    int ret = -1;
    int vol_count = -1;
    uint32_t cmd = 0;
    char key[1024] = {0};
    char *volname = NULL;
    void *vol_dict = NULL;
    dict_t *dict = NULL;
    cli_local_t *local = NULL;

    if (!frame)
        goto out;

    if (!frame->local)
        goto out;

    local = frame->local;

    ret = dict_get_uint32(local->dict, "cmd", &cmd);
    if (ret)
        goto out;

    local->all = _gf_true;

    ret = gf_cli_status_volume(frame, this, data);

    if (ret)
        goto out;

    ret = dict_get_ptr(local->dict, "rsp-dict", &vol_dict);
    if (ret)
        goto out;

    ret = dict_get_int32_sizen((dict_t *)vol_dict, "vol_count", &vol_count);
    if (ret) {
        cli_err("Failed to get names of volumes");
        goto out;
    }

    /* remove the "all" flag in cmd */
    cmd &= ~GF_CLI_STATUS_ALL;
    cmd |= GF_CLI_STATUS_VOL;

    if (global_state->mode & GLUSTER_MODE_XML) {
        // TODO: Pass proper op_* values
        ret = cli_xml_output_vol_status_begin(local, 0, 0, NULL);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
            goto xml_end;
        }
    }

    if (vol_count == 0 && !(global_state->mode & GLUSTER_MODE_XML)) {
        cli_err("No volumes present");
        ret = 0;
        goto out;
    }

    for (i = 0; i < vol_count; i++) {
        dict = dict_new();
        if (!dict)
            goto out;

        ret = snprintf(key, sizeof(key), "vol%d", i);
        ret = dict_get_strn(vol_dict, key, ret, &volname);
        if (ret)
            goto out;

        ret = dict_set_str_sizen(dict, "volname", volname);
        if (ret)
            goto out;

        ret = dict_set_uint32(dict, "cmd", cmd);
        if (ret)
            goto out;

        ret = gf_cli_status_volume(frame, this, dict);
        if (ret)
            goto out;

        dict_unref(dict);
    }

xml_end:
    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_vol_status_end(local);
    }

out:
    if (ret)
        gf_log("cli", GF_LOG_ERROR, "status all failed");

    if (vol_dict)
        dict_unref(vol_dict);

    if (ret && dict)
        dict_unref(dict);

    if (local)
        cli_local_wipe(local);

    if (frame)
        frame->local = NULL;

    return ret;
}

static int
gf_cli_mount_cbk(struct rpc_req *req, struct iovec *iov, int count,
                 void *myframe)
{
    gf1_cli_mount_rsp rsp = {
        0,
    };
    int ret = -1;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf1_cli_mount_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to mount");

    if (rsp.op_ret == 0) {
        ret = 0;
        cli_out("%s", rsp.path);
    } else {
        /* weird sounding but easy to parse... */
        cli_err("%d : failed with this errno (%s)", rsp.op_errno,
                strerror(rsp.op_errno));
        ret = -1;
    }

out:
    cli_cmd_broadcast_response(ret);
    if (rsp.path) {
        free(rsp.path);
    }
    return ret;
}

static int32_t
gf_cli_mount(call_frame_t *frame, xlator_t *this, void *data)
{
    gf1_cli_mount_req req = {
        0,
    };
    int ret = -1;
    void **dataa = data;
    char *label = NULL;
    dict_t *dict = NULL;

    if (!frame || !this || !data)
        goto out;

    label = dataa[0];
    dict = dataa[1];

    req.label = label;
    ret = dict_allocate_and_serialize(dict, &req.dict.dict_val,
                                      &req.dict.dict_len);
    if (ret) {
        ret = -1;
        goto out;
    }

    ret = cli_cmd_submit(NULL, &req, frame, cli_rpc_prog, GLUSTER_CLI_MOUNT,
                         NULL, this, gf_cli_mount_cbk,
                         (xdrproc_t)xdr_gf1_cli_mount_req);

out:
    GF_FREE(req.dict.dict_val);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    return ret;
}

static int
gf_cli_umount_cbk(struct rpc_req *req, struct iovec *iov, int count,
                  void *myframe)
{
    gf1_cli_umount_rsp rsp = {
        0,
    };
    int ret = -1;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf1_cli_umount_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to mount");

    if (rsp.op_ret == 0)
        ret = 0;
    else {
        cli_err("umount failed");
        ret = -1;
    }

out:
    cli_cmd_broadcast_response(ret);
    return ret;
}

static int32_t
gf_cli_umount(call_frame_t *frame, xlator_t *this, void *data)
{
    gf1_cli_umount_req req = {
        0,
    };
    int ret = -1;
    dict_t *dict = NULL;

    if (!frame || !this || !data)
        goto out;

    dict = data;

    ret = dict_get_str_sizen(dict, "path", &req.path);
    if (ret == 0)
        ret = dict_get_int32_sizen(dict, "lazy", &req.lazy);

    if (ret) {
        ret = -1;
        goto out;
    }

    ret = cli_cmd_submit(NULL, &req, frame, cli_rpc_prog, GLUSTER_CLI_UMOUNT,
                         NULL, this, gf_cli_umount_cbk,
                         (xdrproc_t)xdr_gf1_cli_umount_req);

out:
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    return ret;
}

static void
cmd_heal_volume_statistics_out(dict_t *dict, int brick)
{
    uint64_t num_entries = 0;
    int ret = 0;
    char key[256] = {0};
    char *hostname = NULL;
    uint64_t i = 0;
    uint64_t healed_count = 0;
    uint64_t split_brain_count = 0;
    uint64_t heal_failed_count = 0;
    char *start_time_str = NULL;
    char *end_time_str = NULL;
    char *crawl_type = NULL;
    int progress = -1;

    snprintf(key, sizeof key, "%d-hostname", brick);
    ret = dict_get_str(dict, key, &hostname);
    if (ret)
        goto out;
    cli_out("------------------------------------------------");
    cli_out("\nCrawl statistics for brick no %d", brick);
    cli_out("Hostname of brick %s", hostname);

    snprintf(key, sizeof key, "statistics-%d-count", brick);
    ret = dict_get_uint64(dict, key, &num_entries);
    if (ret)
        goto out;

    for (i = 0; i < num_entries; i++) {
        snprintf(key, sizeof key, "statistics_crawl_type-%d-%" PRIu64, brick,
                 i);
        ret = dict_get_str(dict, key, &crawl_type);
        if (ret)
            goto out;

        snprintf(key, sizeof key, "statistics_healed_cnt-%d-%" PRIu64, brick,
                 i);
        ret = dict_get_uint64(dict, key, &healed_count);
        if (ret)
            goto out;

        snprintf(key, sizeof key, "statistics_sb_cnt-%d-%" PRIu64, brick, i);
        ret = dict_get_uint64(dict, key, &split_brain_count);
        if (ret)
            goto out;
        snprintf(key, sizeof key, "statistics_heal_failed_cnt-%d-%" PRIu64,
                 brick, i);
        ret = dict_get_uint64(dict, key, &heal_failed_count);
        if (ret)
            goto out;
        snprintf(key, sizeof key, "statistics_strt_time-%d-%" PRIu64, brick, i);
        ret = dict_get_str(dict, key, &start_time_str);
        if (ret)
            goto out;
        snprintf(key, sizeof key, "statistics_end_time-%d-%" PRIu64, brick, i);
        ret = dict_get_str(dict, key, &end_time_str);
        if (ret)
            goto out;
        snprintf(key, sizeof key, "statistics_inprogress-%d-%" PRIu64, brick,
                 i);
        ret = dict_get_int32(dict, key, &progress);
        if (ret)
            goto out;

        cli_out("\nStarting time of crawl: %s", start_time_str);
        if (progress == 1)
            cli_out("Crawl is in progress");
        else
            cli_out("Ending time of crawl: %s", end_time_str);

        cli_out("Type of crawl: %s", crawl_type);
        cli_out("No. of entries healed: %" PRIu64, healed_count);
        cli_out("No. of entries in split-brain: %" PRIu64, split_brain_count);
        cli_out("No. of heal failed entries: %" PRIu64, heal_failed_count);
    }

out:
    return;
}

static void
cmd_heal_volume_brick_out(dict_t *dict, int brick)
{
    uint64_t num_entries = 0;
    int ret = 0;
    char key[64] = {0};
    char *hostname = NULL;
    char *path = NULL;
    char *status = NULL;
    uint64_t i = 0;
    uint32_t time = 0;
    char timestr[GF_TIMESTR_SIZE] = {0};
    char *shd_status = NULL;

    snprintf(key, sizeof key, "%d-hostname", brick);
    ret = dict_get_str(dict, key, &hostname);
    if (ret)
        goto out;
    snprintf(key, sizeof key, "%d-path", brick);
    ret = dict_get_str(dict, key, &path);
    if (ret)
        goto out;
    cli_out("\nBrick %s:%s", hostname, path);

    snprintf(key, sizeof key, "%d-status", brick);
    ret = dict_get_str(dict, key, &status);
    if (status && status[0] != '\0')
        cli_out("Status: %s", status);

    snprintf(key, sizeof key, "%d-shd-status", brick);
    ret = dict_get_str(dict, key, &shd_status);

    if (!shd_status) {
        snprintf(key, sizeof key, "%d-count", brick);
        ret = dict_get_uint64(dict, key, &num_entries);
        cli_out("Number of entries: %" PRIu64, num_entries);

        for (i = 0; i < num_entries; i++) {
            snprintf(key, sizeof key, "%d-%" PRIu64, brick, i);
            ret = dict_get_str(dict, key, &path);
            if (ret)
                continue;
            time = 0;
            snprintf(key, sizeof key, "%d-%" PRIu64 "-time", brick, i);
            ret = dict_get_uint32(dict, key, &time);
            if (ret || !time) {
                cli_out("%s", path);
            } else {
                gf_time_fmt(timestr, sizeof timestr, time, gf_timefmt_FT);
                if (i == 0) {
                    cli_out("at                    path on brick");
                    cli_out("-----------------------------------");
                }
                cli_out("%s %s", timestr, path);
            }
        }
    }

out:
    return;
}

static void
cmd_heal_volume_statistics_heal_count_out(dict_t *dict, int brick)
{
    uint64_t num_entries = 0;
    int ret = 0;
    char key[64] = {0};
    char *hostname = NULL;
    char *path = NULL;
    char *status = NULL;
    char *shd_status = NULL;

    snprintf(key, sizeof key, "%d-hostname", brick);
    ret = dict_get_str(dict, key, &hostname);
    if (ret)
        goto out;
    snprintf(key, sizeof key, "%d-path", brick);
    ret = dict_get_str(dict, key, &path);
    if (ret)
        goto out;
    cli_out("\nBrick %s:%s", hostname, path);

    snprintf(key, sizeof key, "%d-status", brick);
    ret = dict_get_str(dict, key, &status);
    if (status && strlen(status))
        cli_out("Status: %s", status);

    snprintf(key, sizeof key, "%d-shd-status", brick);
    ret = dict_get_str(dict, key, &shd_status);

    if (!shd_status) {
        snprintf(key, sizeof key, "%d-hardlinks", brick);
        ret = dict_get_uint64(dict, key, &num_entries);
        if (ret)
            cli_out("No gathered input for this brick");
        else
            cli_out("Number of entries: %" PRIu64, num_entries);
    }

out:
    return;
}

static int
gf_is_cli_heal_get_command(gf_xl_afr_op_t heal_op)
{
    /* If the command is get command value is 1 otherwise 0, for
       invalid commands -1 */
    static int get_cmds[GF_SHD_OP_HEAL_DISABLE + 1] = {
        [GF_SHD_OP_INVALID] = -1,
        [GF_SHD_OP_HEAL_INDEX] = 0,
        [GF_SHD_OP_HEAL_FULL] = 0,
        [GF_SHD_OP_INDEX_SUMMARY] = 1,
        [GF_SHD_OP_SPLIT_BRAIN_FILES] = 1,
        [GF_SHD_OP_STATISTICS] = 1,
        [GF_SHD_OP_STATISTICS_HEAL_COUNT] = 1,
        [GF_SHD_OP_STATISTICS_HEAL_COUNT_PER_REPLICA] = 1,
        [GF_SHD_OP_HEAL_ENABLE] = 0,
        [GF_SHD_OP_HEAL_DISABLE] = 0,
    };

    if (heal_op > GF_SHD_OP_INVALID && heal_op <= GF_SHD_OP_HEAL_DISABLE)
        return get_cmds[heal_op] == 1;
    return _gf_false;
}

int
gf_cli_heal_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    cli_local_t *local = NULL;
    char *volname = NULL;
    call_frame_t *frame = NULL;
    dict_t *dict = NULL;
    int brick_count = 0;
    int i = 0;
    gf_xl_afr_op_t heal_op = GF_SHD_OP_INVALID;
    const char *operation = NULL;
    const char *substr = NULL;
    const char *heal_op_str = NULL;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status) {
        goto out;
    }

    frame = myframe;

    GF_ASSERT(frame->local);

    local = frame->local;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        goto out;
    }

    ret = dict_get_int32_sizen(local->dict, "heal-op", (int32_t *)&heal_op);
    // TODO: Proper XML output
    //#if (HAVE_LIB_XML)
    //        if (global_state->mode & GLUSTER_MODE_XML) {
    //                ret = cli_xml_output_dict ("volHeal", dict, rsp.op_ret,
    //                                           rsp.op_errno, rsp.op_errstr);
    //                if (ret)
    //                        gf_log ("cli", GF_LOG_ERROR, XML_ERROR);
    //                goto out;
    //        }
    //#endif

    ret = dict_get_str_sizen(local->dict, "volname", &volname);
    if (ret) {
        gf_log(frame->this->name, GF_LOG_ERROR, "failed to get volname");
        goto out;
    }

    gf_log("cli", GF_LOG_INFO, "Received resp to heal volume");

    operation = "Gathering ";
    substr = "";
    switch (heal_op) {
        case GF_SHD_OP_HEAL_INDEX:
            operation = "Launching heal operation ";
            heal_op_str = "to perform index self heal";
            substr = "\nUse heal info commands to check status.";
            break;
        case GF_SHD_OP_HEAL_FULL:
            operation = "Launching heal operation ";
            heal_op_str = "to perform full self heal";
            substr = "\nUse heal info commands to check status.";
            break;
        case GF_SHD_OP_INDEX_SUMMARY:
            heal_op_str = "list of entries to be healed";
            break;
        case GF_SHD_OP_SPLIT_BRAIN_FILES:
            heal_op_str = "list of split brain entries";
            break;
        case GF_SHD_OP_STATISTICS:
            heal_op_str = "crawl statistics";
            break;
        case GF_SHD_OP_STATISTICS_HEAL_COUNT:
            heal_op_str = "count of entries to be healed";
            break;
        case GF_SHD_OP_STATISTICS_HEAL_COUNT_PER_REPLICA:
            heal_op_str = "count of entries to be healed per replica";
            break;
        case GF_SHD_OP_SBRAIN_HEAL_FROM_BIGGER_FILE:
        case GF_SHD_OP_SBRAIN_HEAL_FROM_LATEST_MTIME:
        case GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK:
        case GF_SHD_OP_HEAL_SUMMARY:
        case GF_SHD_OP_HEALED_FILES:
        case GF_SHD_OP_HEAL_FAILED_FILES:
            /* These cases are never hit; they're coded just to silence the
             * compiler warnings.*/
            break;

        case GF_SHD_OP_INVALID:
            heal_op_str = "invalid heal op";
            break;
        case GF_SHD_OP_HEAL_ENABLE:
            operation = "";
            heal_op_str = "Enable heal";
            break;
        case GF_SHD_OP_HEAL_DISABLE:
            operation = "";
            heal_op_str = "Disable heal";
            break;
        case GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE:
            operation = "";
            heal_op_str = "Enable granular entry heal";
            break;
        case GF_SHD_OP_GRANULAR_ENTRY_HEAL_DISABLE:
            operation = "";
            heal_op_str = "Disable granular entry heal";
            break;
    }

    if (rsp.op_ret) {
        if (strcmp(rsp.op_errstr, "")) {
            cli_err("%s%s on volume %s has been unsuccessful:", operation,
                    heal_op_str, volname);
            cli_err("%s", rsp.op_errstr);
        }
        ret = rsp.op_ret;
        goto out;
    } else {
        cli_out("%s%s on volume %s has been successful %s", operation,
                heal_op_str, volname, substr);
    }

    ret = rsp.op_ret;
    if (!gf_is_cli_heal_get_command(heal_op))
        goto out;

    dict = dict_new();
    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);

    if (ret) {
        gf_log("", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
        goto out;
    }

    ret = dict_get_int32_sizen(dict, "count", &brick_count);
    if (ret)
        goto out;

    if (!brick_count) {
        cli_err("All bricks of volume %s are down.", volname);
        ret = -1;
        goto out;
    }

    switch (heal_op) {
        case GF_SHD_OP_STATISTICS:
            for (i = 0; i < brick_count; i++)
                cmd_heal_volume_statistics_out(dict, i);
            break;
        case GF_SHD_OP_STATISTICS_HEAL_COUNT:
        case GF_SHD_OP_STATISTICS_HEAL_COUNT_PER_REPLICA:
            for (i = 0; i < brick_count; i++)
                cmd_heal_volume_statistics_heal_count_out(dict, i);
            break;
        case GF_SHD_OP_INDEX_SUMMARY:
        case GF_SHD_OP_SPLIT_BRAIN_FILES:
            for (i = 0; i < brick_count; i++)
                cmd_heal_volume_brick_out(dict, i);
            break;
        default:
            break;
    }

    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    if (dict)
        dict_unref(dict);
    return ret;
}

static int32_t
gf_cli_heal_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = 0;
    dict_t *dict = NULL;

    dict = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_heal_volume_cbk,
                          (xdrproc_t)xdr_gf_cli_req, dict,
                          GLUSTER_CLI_HEAL_VOLUME, this, cli_rpc_prog, NULL);

    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    GF_FREE(req.dict.dict_val);

    return ret;
}

static int32_t
gf_cli_statedump_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                            void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    char msg[1024] = "Volume statedump successful";

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status)
        goto out;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }
    gf_log("cli", GF_LOG_DEBUG, "Received response to statedump");
    if (rsp.op_ret)
        snprintf(msg, sizeof(msg), "%s", rsp.op_errstr);

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_str("volStatedump", msg, rsp.op_ret, rsp.op_errno,
                                 rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (rsp.op_ret)
        cli_err("volume statedump: failed: %s", msg);
    else
        cli_out("volume statedump: success");
    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int32_t
gf_cli_statedump_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    dict_t *options = NULL;
    int ret = -1;

    options = data;

    ret = cli_to_glusterd(
        &req, frame, gf_cli_statedump_volume_cbk, (xdrproc_t)xdr_gf_cli_req,
        options, GLUSTER_CLI_STATEDUMP_VOLUME, this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    GF_FREE(req.dict.dict_val);
    return ret;
}

static int32_t
gf_cli_list_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
    int ret = -1;
    gf_cli_rsp rsp = {
        0,
    };
    dict_t *dict = NULL;
    int vol_count = 0;
    ;
    char *volname = NULL;
    char key[1024] = {
        0,
    };
    int i = 0;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status)
        goto out;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    dict = dict_new();
    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
        goto out;
    }

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_vol_list(dict, rsp.op_ret, rsp.op_errno,
                                      rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (rsp.op_ret)
        cli_err("%s", rsp.op_errstr);
    else {
        ret = dict_get_int32_sizen(dict, "count", &vol_count);
        if (ret)
            goto out;

        if (vol_count == 0) {
            cli_err("No volumes present in cluster");
            goto out;
        }
        for (i = 0; i < vol_count; i++) {
            ret = snprintf(key, sizeof(key), "volume%d", i);
            ret = dict_get_strn(dict, key, ret, &volname);
            if (ret)
                goto out;
            cli_out("%s", volname);
        }
    }

    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);

    if (dict)
        dict_unref(dict);
    return ret;
}

static int32_t
gf_cli_list_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    int ret = -1;
    gf_cli_req req = {{
        0,
    }};

    ret = cli_cmd_submit(NULL, &req, frame, cli_rpc_prog,
                         GLUSTER_CLI_LIST_VOLUME, NULL, this,
                         gf_cli_list_volume_cbk, (xdrproc_t)xdr_gf_cli_req);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);
    return ret;
}

static int32_t
gf_cli_clearlocks_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                             void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    char *lk_summary = NULL;
    dict_t *dict = NULL;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status)
        goto out;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }
    gf_log("cli", GF_LOG_DEBUG, "Received response to clear-locks");

    if (rsp.op_ret) {
        cli_err("Volume clear-locks unsuccessful");
        cli_err("%s", rsp.op_errstr);

    } else {
        if (!rsp.dict.dict_len) {
            cli_err("Possibly no locks cleared");
            ret = 0;
            goto out;
        }

        dict = dict_new();

        if (!dict) {
            ret = -1;
            goto out;
        }

        ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);

        if (ret) {
            gf_log("cli", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
            goto out;
        }

        ret = dict_get_str(dict, "lk-summary", &lk_summary);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "Unable to get lock summary from dictionary");
            goto out;
        }
        cli_out("Volume clear-locks successful");
        cli_out("%s", lk_summary);
    }

    ret = rsp.op_ret;

out:
    if (dict)
        dict_unref(dict);
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int32_t
gf_cli_clearlocks_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    dict_t *options = NULL;
    int ret = -1;

    options = data;

    ret = cli_to_glusterd(
        &req, frame, gf_cli_clearlocks_volume_cbk, (xdrproc_t)xdr_gf_cli_req,
        options, GLUSTER_CLI_CLRLOCKS_VOLUME, this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    GF_FREE(req.dict.dict_val);
    return ret;
}

static int32_t
cli_snapshot_remove_reply(gf_cli_rsp *rsp, dict_t *dict, call_frame_t *frame)
{
    int32_t ret = -1;
    char *snap_name = NULL;
    int32_t delete_cmd = -1;
    cli_local_t *local = NULL;

    GF_ASSERT(frame);
    GF_ASSERT(rsp);
    GF_ASSERT(dict);

    local = frame->local;

    ret = dict_get_int32_sizen(dict, "sub-cmd", &delete_cmd);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Could not get sub-cmd");
        goto end;
    }

    if ((global_state->mode & GLUSTER_MODE_XML) &&
        (delete_cmd == GF_SNAP_DELETE_TYPE_SNAP)) {
        ret = cli_xml_output_snap_delete_begin(local, rsp->op_ret,
                                               rsp->op_errno, rsp->op_errstr);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "Failed to create xml output for delete");
            goto end;
        }
    }

    if (rsp->op_ret && !(global_state->mode & GLUSTER_MODE_XML)) {
        cli_err("snapshot delete: failed: %s",
                rsp->op_errstr ? rsp->op_errstr
                               : "Please check log file for details");
        ret = rsp->op_ret;
        goto out;
    }

    if (delete_cmd == GF_SNAP_DELETE_TYPE_ALL ||
        delete_cmd == GF_SNAP_DELETE_TYPE_VOL) {
        local = ((call_frame_t *)frame)->local;
        if (!local) {
            ret = -1;
            gf_log("cli", GF_LOG_ERROR, "frame->local is NULL");
            goto out;
        }

        /* During first call back of snapshot delete of type
         * ALL and VOL, We will get the snapcount and snapnames.
         * Hence to make the subsequent rpc calls for individual
         * snapshot delete, We need to save it in local dictionary.
         */
        dict_copy(dict, local->dict);
        ret = 0;
        goto out;
    }

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_snapshot_delete(local, dict, rsp);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "Failed to create xml output for snapshot delete command");
            goto out;
        }
        /* Error out in case of the op already failed */
        if (rsp->op_ret) {
            ret = rsp->op_ret;
            goto out;
        }
    } else {
        ret = dict_get_str_sizen(dict, "snapname", &snap_name);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, "Failed to get snapname");
            goto out;
        }

        cli_out("snapshot delete: %s: snap removed successfully", snap_name);
    }
    ret = 0;

out:
    if ((global_state->mode & GLUSTER_MODE_XML) &&
        (delete_cmd == GF_SNAP_DELETE_TYPE_SNAP)) {
        ret = cli_xml_output_snap_delete_end(local);
    }
end:
    return ret;
}

static int
cli_snapshot_config_display(dict_t *dict, gf_cli_rsp *rsp)
{
    char buf[PATH_MAX] = "";
    char *volname = NULL;
    int ret = -1;
    int config_command = 0;
    uint64_t value = 0;
    uint64_t hard_limit = 0;
    uint64_t soft_limit = 0;
    uint64_t i = 0;
    uint64_t voldisplaycount = 0;
    char *auto_delete = NULL;
    char *snap_activate = NULL;

    GF_ASSERT(dict);
    GF_ASSERT(rsp);

    if (rsp->op_ret) {
        cli_err("Snapshot Config : failed: %s",
                rsp->op_errstr ? rsp->op_errstr
                               : "Please check log file for details");
        ret = rsp->op_ret;
        goto out;
    }

    ret = dict_get_int32_sizen(dict, "config-command", &config_command);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Could not fetch config type");
        goto out;
    }

    ret = dict_get_uint64(dict, "snap-max-hard-limit", &hard_limit);
    /* Ignore the error, as the key specified is optional */
    ret = dict_get_uint64(dict, "snap-max-soft-limit", &soft_limit);

    ret = dict_get_str_sizen(dict, "auto-delete", &auto_delete);

    ret = dict_get_str_sizen(dict, "snap-activate-on-create", &snap_activate);

    if (!hard_limit && !soft_limit &&
        config_command != GF_SNAP_CONFIG_DISPLAY && !auto_delete &&
        !snap_activate) {
        ret = -1;
        gf_log(THIS->name, GF_LOG_ERROR, "Could not fetch config-key");
        goto out;
    }

    ret = dict_get_str_sizen(dict, "volname", &volname);
    /* Ignore the error, as volname is optional */

    if (!volname) {
        volname = "System";
    }

    switch (config_command) {
        case GF_SNAP_CONFIG_TYPE_SET:
            if (hard_limit && soft_limit) {
                cli_out(
                    "snapshot config: snap-max-hard-limit "
                    "& snap-max-soft-limit for system set successfully");
            } else if (hard_limit) {
                cli_out(
                    "snapshot config: snap-max-hard-limit "
                    "for %s set successfully",
                    volname);
            } else if (soft_limit) {
                cli_out(
                    "snapshot config: snap-max-soft-limit "
                    "for %s set successfully",
                    volname);
            } else if (auto_delete) {
                cli_out("snapshot config: auto-delete successfully set");
            } else if (snap_activate) {
                cli_out("snapshot config: activate-on-create successfully set");
            }
            break;

        case GF_SNAP_CONFIG_DISPLAY:
            cli_out("\nSnapshot System Configuration:");
            ret = dict_get_uint64(dict, "snap-max-hard-limit", &value);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR,
                       "Could not fetch snap_max_hard_limit for %s", volname);
                ret = -1;
                goto out;
            }
            cli_out("snap-max-hard-limit : %" PRIu64, value);

            ret = dict_get_uint64(dict, "snap-max-soft-limit", &soft_limit);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR,
                       "Could not fetch snap-max-soft-limit for %s", volname);
                ret = -1;
                goto out;
            }
            cli_out("snap-max-soft-limit : %" PRIu64 "%%", soft_limit);

            cli_out("auto-delete : %s", auto_delete);

            cli_out("activate-on-create : %s\n", snap_activate);

            cli_out("Snapshot Volume Configuration:");

            ret = dict_get_uint64(dict, "voldisplaycount", &voldisplaycount);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, "Could not fetch voldisplaycount");
                ret = -1;
                goto out;
            }

            for (i = 0; i < voldisplaycount; i++) {
                ret = snprintf(buf, sizeof(buf), "volume%" PRIu64 "-volname",
                               i);
                ret = dict_get_strn(dict, buf, ret, &volname);
                if (ret) {
                    gf_log("cli", GF_LOG_ERROR, "Could not fetch %s", buf);
                    ret = -1;
                    goto out;
                }
                cli_out("\nVolume : %s", volname);

                snprintf(buf, sizeof(buf),
                         "volume%" PRIu64 "-snap-max-hard-limit", i);
                ret = dict_get_uint64(dict, buf, &value);
                if (ret) {
                    gf_log("cli", GF_LOG_ERROR, "Could not fetch %s", buf);
                    ret = -1;
                    goto out;
                }
                cli_out("snap-max-hard-limit : %" PRIu64, value);

                snprintf(buf, sizeof(buf),
                         "volume%" PRIu64 "-active-hard-limit", i);
                ret = dict_get_uint64(dict, buf, &value);
                if (ret) {
                    gf_log("cli", GF_LOG_ERROR,
                           "Could not fetch"
                           " effective snap_max_hard_limit for %s",
                           volname);
                    ret = -1;
                    goto out;
                }
                cli_out("Effective snap-max-hard-limit : %" PRIu64, value);

                snprintf(buf, sizeof(buf),
                         "volume%" PRIu64 "-snap-max-soft-limit", i);
                ret = dict_get_uint64(dict, buf, &value);
                if (ret) {
                    gf_log("cli", GF_LOG_ERROR, "Could not fetch %s", buf);
                    ret = -1;
                    goto out;
                }
                cli_out("Effective snap-max-soft-limit : %" PRIu64
                        " "
                        "(%" PRIu64 "%%)",
                        value, soft_limit);
            }
            break;
        default:
            break;
    }

    ret = 0;
out:
    return ret;
}

/* This function is used to print the volume related information
 * of a snap.
 *
 * arg - 0, dict       : Response Dictionary.
 * arg - 1, prefix str : snaplist.snap{0..}.vol{0..}.*
 */
static int
cli_get_each_volinfo_in_snap(dict_t *dict, char *keyprefix,
                             gf_boolean_t snap_driven)
{
    char key[PATH_MAX] = "";
    char *get_buffer = NULL;
    int value = 0;
    int ret = -1;
    char indent[5] = "\t";
    char *volname = NULL;

    GF_ASSERT(dict);
    GF_ASSERT(keyprefix);

    if (snap_driven) {
        ret = snprintf(key, sizeof(key), "%s.volname", keyprefix);
        if (ret < 0) {
            goto out;
        }

        ret = dict_get_str(dict, key, &get_buffer);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, "Failed to get %s", key);
            goto out;
        }
        cli_out("%s" INDENT_MAIN_HEAD "%s", indent, "Snap Volume Name", ":",
                get_buffer);

        ret = snprintf(key, sizeof(key), "%s.origin-volname", keyprefix);
        if (ret < 0) {
            goto out;
        }

        ret = dict_get_str(dict, key, &volname);
        if (ret) {
            gf_log("cli", GF_LOG_WARNING, "Failed to get %s", key);
            cli_out("%-12s", "Origin:");
        }
        cli_out("%s" INDENT_MAIN_HEAD "%s", indent, "Origin Volume name", ":",
                volname);

        ret = snprintf(key, sizeof(key), "%s.snapcount", keyprefix);
        if (ret < 0) {
            goto out;
        }

        ret = dict_get_int32(dict, key, &value);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, "Failed to get %s", key);
            goto out;
        }
        cli_out("%s%s %s      %s %d", indent, "Snaps taken for", volname, ":",
                value);

        ret = snprintf(key, sizeof(key), "%s.snaps-available", keyprefix);
        if (ret < 0) {
            goto out;
        }

        ret = dict_get_int32(dict, key, &value);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, "Failed to get %s", key);
            goto out;
        }
        cli_out("%s%s %s  %s %d", indent, "Snaps available for", volname, ":",
                value);
    }

    ret = snprintf(key, sizeof(key), "%s.vol-status", keyprefix);
    if (ret < 0) {
        goto out;
    }

    ret = dict_get_str(dict, key, &get_buffer);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Failed to get %s", key);
        goto out;
    }
    cli_out("%s" INDENT_MAIN_HEAD "%s", indent, "Status", ":", get_buffer);
out:
    return ret;
}

/* This function is used to print snap related information
 * arg - 0, dict       : Response dictionary.
 * arg - 1, prefix_str : snaplist.snap{0..}.*
 */
static int
cli_get_volinfo_in_snap(dict_t *dict, char *keyprefix)
{
    char key[PATH_MAX] = "";
    int i = 0;
    int volcount = 0;
    int ret = -1;

    GF_ASSERT(dict);
    GF_ASSERT(keyprefix);

    ret = snprintf(key, sizeof(key), "%s.vol-count", keyprefix);
    if (ret < 0) {
        goto out;
    }

    ret = dict_get_int32(dict, key, &volcount);
    for (i = 1; i <= volcount; i++) {
        ret = snprintf(key, sizeof(key), "%s.vol%d", keyprefix, i);
        if (ret < 0) {
            goto out;
        }
        ret = cli_get_each_volinfo_in_snap(dict, key, _gf_true);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "Could not list details of volume in a snap");
            goto out;
        }
        cli_out(" ");
    }

out:
    return ret;
}

static int
cli_get_each_snap_info(dict_t *dict, char *prefix_str, gf_boolean_t snap_driven)
{
    char key_buffer[PATH_MAX] = "";
    char *get_buffer = NULL;
    int ret = -1;
    char indent[5] = "";

    GF_ASSERT(dict);
    GF_ASSERT(prefix_str);

    if (!snap_driven)
        strcat(indent, "\t");

    ret = snprintf(key_buffer, sizeof(key_buffer), "%s.snapname", prefix_str);
    if (ret < 0) {
        goto out;
    }

    ret = dict_get_str(dict, key_buffer, &get_buffer);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Unable to fetch snapname %s ", key_buffer);
        goto out;
    }
    cli_out("%s" INDENT_MAIN_HEAD "%s", indent, "Snapshot", ":", get_buffer);

    ret = snprintf(key_buffer, sizeof(key_buffer), "%s.snap-id", prefix_str);
    if (ret < 0) {
        goto out;
    }

    ret = dict_get_str(dict, key_buffer, &get_buffer);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Unable to fetch snap-id %s ", key_buffer);
        goto out;
    }
    cli_out("%s" INDENT_MAIN_HEAD "%s", indent, "Snap UUID", ":", get_buffer);

    ret = snprintf(key_buffer, sizeof(key_buffer), "%s.snap-desc", prefix_str);
    if (ret < 0) {
        goto out;
    }

    ret = dict_get_str(dict, key_buffer, &get_buffer);
    if (!ret) {
        /* Ignore error for description */
        cli_out("%s" INDENT_MAIN_HEAD "%s", indent, "Description", ":",
                get_buffer);
    }

    ret = snprintf(key_buffer, sizeof(key_buffer), "%s.snap-time", prefix_str);
    if (ret < 0) {
        goto out;
    }

    ret = dict_get_str(dict, key_buffer, &get_buffer);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Unable to fetch snap-time %s ",
               prefix_str);
        goto out;
    }
    cli_out("%s" INDENT_MAIN_HEAD "%s", indent, "Created", ":", get_buffer);

    if (snap_driven) {
        cli_out("%-12s", "Snap Volumes:\n");
        ret = cli_get_volinfo_in_snap(dict, prefix_str);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, "Unable to list details of the snaps");
            goto out;
        }
    }
out:
    return ret;
}

/* This is a generic function to print snap related information.
 * arg - 0, dict : Response Dictionary
 */
static int
cli_call_snapshot_info(dict_t *dict, gf_boolean_t bool_snap_driven)
{
    int snap_count = 0;
    char key[32] = "";
    int ret = -1;
    int i = 0;

    GF_ASSERT(dict);

    ret = dict_get_int32_sizen(dict, "snapcount", &snap_count);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Unable to get snapcount");
        goto out;
    }

    if (snap_count == 0) {
        cli_out("No snapshots present");
    }

    for (i = 1; i <= snap_count; i++) {
        ret = snprintf(key, sizeof(key), "snap%d", i);
        if (ret < 0) {
            goto out;
        }
        ret = cli_get_each_snap_info(dict, key, bool_snap_driven);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, "Unable to print snap details");
            goto out;
        }
    }
out:
    return ret;
}

static int
cli_get_snaps_in_volume(dict_t *dict)
{
    int ret = -1;
    int i = 0;
    int count = 0;
    int avail = 0;
    char key[32] = "";
    char *get_buffer = NULL;

    GF_ASSERT(dict);

    ret = dict_get_str_sizen(dict, "origin-volname", &get_buffer);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Could not fetch origin-volname");
        goto out;
    }
    cli_out(INDENT_MAIN_HEAD "%s", "Volume Name", ":", get_buffer);

    ret = dict_get_int32_sizen(dict, "snapcount", &avail);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Could not fetch snapcount");
        goto out;
    }
    cli_out(INDENT_MAIN_HEAD "%d", "Snaps Taken", ":", avail);

    ret = dict_get_int32_sizen(dict, "snaps-available", &count);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Could not fetch snaps-available");
        goto out;
    }
    cli_out(INDENT_MAIN_HEAD "%d", "Snaps Available", ":", count);

    for (i = 1; i <= avail; i++) {
        snprintf(key, sizeof(key), "snap%d", i);
        ret = cli_get_each_snap_info(dict, key, _gf_false);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, "Unable to print snap details");
            goto out;
        }

        ret = snprintf(key, sizeof(key), "snap%d.vol1", i);
        if (ret < 0) {
            goto out;
        }
        ret = cli_get_each_volinfo_in_snap(dict, key, _gf_false);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "Could not get volume related information");
            goto out;
        }

        cli_out(" ");
    }
out:
    return ret;
}

static int
cli_snapshot_list(dict_t *dict)
{
    int snapcount = 0;
    char key[32] = "";
    int ret = -1;
    int i = 0;
    char *get_buffer = NULL;

    GF_ASSERT(dict);

    ret = dict_get_int32_sizen(dict, "snapcount", &snapcount);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Could not fetch snap count");
        goto out;
    }

    if (snapcount == 0) {
        cli_out("No snapshots present");
    }

    for (i = 1; i <= snapcount; i++) {
        ret = snprintf(key, sizeof(key), "snapname%d", i);
        if (ret < 0) {
            goto out;
        }

        ret = dict_get_strn(dict, key, ret, &get_buffer);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, "Could not get %s ", key);
            goto out;
        } else {
            cli_out("%s", get_buffer);
        }
    }
out:
    return ret;
}

static int
cli_get_snap_volume_status(dict_t *dict, char *key_prefix)
{
    int ret = -1;
    char key[PATH_MAX] = "";
    char *buffer = NULL;
    int brickcount = 0;
    int i = 0;
    int pid = 0;

    GF_ASSERT(dict);
    GF_ASSERT(key_prefix);

    ret = snprintf(key, sizeof(key), "%s.brickcount", key_prefix);
    if (ret < 0) {
        goto out;
    }
    ret = dict_get_int32(dict, key, &brickcount);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Failed to fetch brickcount");
        goto out;
    }

    for (i = 0; i < brickcount; i++) {
        ret = snprintf(key, sizeof(key), "%s.brick%d.path", key_prefix, i);
        if (ret < 0) {
            goto out;
        }

        ret = dict_get_str(dict, key, &buffer);
        if (ret) {
            gf_log("cli", GF_LOG_INFO, "Unable to get Brick Path");
            continue;
        }
        cli_out("\n\t%-17s %s   %s", "Brick Path", ":", buffer);

        ret = snprintf(key, sizeof(key), "%s.brick%d.vgname", key_prefix, i);
        if (ret < 0) {
            goto out;
        }

        ret = dict_get_str(dict, key, &buffer);
        if (ret) {
            gf_log("cli", GF_LOG_INFO, "Unable to get Volume Group");
            cli_out("\t%-17s %s   %s", "Volume Group", ":", "N/A");
        } else
            cli_out("\t%-17s %s   %s", "Volume Group", ":", buffer);

        ret = snprintf(key, sizeof(key), "%s.brick%d.status", key_prefix, i);
        if (ret < 0) {
            goto out;
        }

        ret = dict_get_str(dict, key, &buffer);
        if (ret) {
            gf_log("cli", GF_LOG_INFO, "Unable to get Brick Running");
            cli_out("\t%-17s %s   %s", "Brick Running", ":", "N/A");
        } else
            cli_out("\t%-17s %s   %s", "Brick Running", ":", buffer);

        ret = snprintf(key, sizeof(key), "%s.brick%d.pid", key_prefix, i);
        if (ret < 0) {
            goto out;
        }

        ret = dict_get_int32(dict, key, &pid);
        if (ret) {
            gf_log("cli", GF_LOG_INFO, "Unable to get pid");
            cli_out("\t%-17s %s   %s", "Brick PID", ":", "N/A");
        } else
            cli_out("\t%-17s %s   %d", "Brick PID", ":", pid);

        ret = snprintf(key, sizeof(key), "%s.brick%d.data", key_prefix, i);
        if (ret < 0) {
            goto out;
        }

        ret = dict_get_str(dict, key, &buffer);
        if (ret) {
            gf_log("cli", GF_LOG_INFO, "Unable to get Data Percent");
            cli_out("\t%-17s %s   %s", "Data Percentage", ":", "N/A");
        } else
            cli_out("\t%-17s %s   %s", "Data Percentage", ":", buffer);

        ret = snprintf(key, sizeof(key), "%s.brick%d.lvsize", key_prefix, i);
        if (ret < 0) {
            goto out;
        }
        ret = dict_get_str(dict, key, &buffer);
        if (ret) {
            gf_log("cli", GF_LOG_INFO, "Unable to get LV Size");
            cli_out("\t%-17s %s   %s", "LV Size", ":", "N/A");
        } else
            cli_out("\t%-17s %s   %s", "LV Size", ":", buffer);
    }

    ret = 0;
out:
    return ret;
}

static int
cli_get_single_snap_status(dict_t *dict, char *keyprefix)
{
    int ret = -1;
    char key[64] = ""; /* keyprefix is ""status.snap0" */
    int i = 0;
    int volcount = 0;
    char *get_buffer = NULL;

    GF_ASSERT(dict);
    GF_ASSERT(keyprefix);

    ret = snprintf(key, sizeof(key), "%s.snapname", keyprefix);
    if (ret < 0) {
        goto out;
    }

    ret = dict_get_str(dict, key, &get_buffer);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Unable to get snapname");
        goto out;
    }
    cli_out("\nSnap Name : %s", get_buffer);

    ret = snprintf(key, sizeof(key), "%s.uuid", keyprefix);
    if (ret < 0) {
        goto out;
    }

    ret = dict_get_str(dict, key, &get_buffer);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Unable to get snap UUID");
        goto out;
    }
    cli_out("Snap UUID : %s", get_buffer);

    ret = snprintf(key, sizeof(key), "%s.volcount", keyprefix);
    if (ret < 0) {
        goto out;
    }

    ret = dict_get_int32(dict, key, &volcount);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Unable to get volume count");
        goto out;
    }

    for (i = 0; i < volcount; i++) {
        ret = snprintf(key, sizeof(key), "%s.vol%d", keyprefix, i);
        if (ret < 0) {
            goto out;
        }

        ret = cli_get_snap_volume_status(dict, key);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, "Could not get snap volume status");
            goto out;
        }
    }
out:
    return ret;
}

static int32_t
cli_populate_req_dict_for_delete(dict_t *snap_dict, dict_t *dict, size_t index)
{
    int32_t ret = -1;
    char key[PATH_MAX] = "";
    char *buffer = NULL;

    GF_ASSERT(snap_dict);
    GF_ASSERT(dict);

    ret = dict_set_int32_sizen(snap_dict, "sub-cmd", GF_SNAP_DELETE_TYPE_ITER);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR,
               "Could not save command type in snap dictionary");
        goto out;
    }

    ret = snprintf(key, sizeof(key), "snapname%zu", index);
    if (ret < 0) {
        goto out;
    }

    ret = dict_get_str(dict, key, &buffer);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Failed to get snapname");
        goto out;
    }

    ret = dict_set_dynstr_with_alloc(snap_dict, "snapname", buffer);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Failed to save snapname");
        goto out;
    }

    ret = dict_set_int32_sizen(snap_dict, "type", GF_SNAP_OPTION_TYPE_DELETE);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Failed to save command type");
        goto out;
    }

    ret = dict_set_dynstr_with_alloc(snap_dict, "cmd-str", "snapshot delete");
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Could not save command string as delete");
        goto out;
    }
out:
    return ret;
}

static int
cli_populate_req_dict_for_status(dict_t *snap_dict, dict_t *dict, int index)
{
    int ret = -1;
    char key[PATH_MAX] = "";
    char *buffer = NULL;

    GF_ASSERT(snap_dict);
    GF_ASSERT(dict);

    ret = dict_set_uint32(snap_dict, "sub-cmd", GF_SNAP_STATUS_TYPE_ITER);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Could not save command type in snap dict");
        goto out;
    }

    ret = snprintf(key, sizeof(key), "status.snap%d.snapname", index);
    if (ret < 0) {
        goto out;
    }

    ret = dict_get_strn(dict, key, ret, &buffer);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Could not get snapname");
        goto out;
    }

    ret = dict_set_str_sizen(snap_dict, "snapname", buffer);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Could not save snapname in snap dict");
        goto out;
    }

    ret = dict_set_int32_sizen(snap_dict, "type", GF_SNAP_OPTION_TYPE_STATUS);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Could not save command type");
        goto out;
    }

    ret = dict_set_dynstr_with_alloc(snap_dict, "cmd-str", "snapshot status");
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Could not save command string as status");
        goto out;
    }

    ret = dict_set_int32_sizen(snap_dict, "hold_vol_locks", _gf_false);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Setting volume lock flag failed");
        goto out;
    }

out:
    return ret;
}

static int
cli_snapshot_status(dict_t *dict, gf_cli_rsp *rsp, call_frame_t *frame)
{
    int ret = -1;
    int status_cmd = -1;
    cli_local_t *local = NULL;

    GF_ASSERT(dict);
    GF_ASSERT(rsp);
    GF_ASSERT(frame);

    local = ((call_frame_t *)frame)->local;
    if (!local) {
        gf_log("cli", GF_LOG_ERROR, "frame->local is NULL");
        goto out;
    }

    if (rsp->op_ret) {
        if (rsp->op_errstr) {
            ret = dict_set_dynstr_with_alloc(local->dict, "op_err_str",
                                             rsp->op_errstr);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR,
                       "Failed to set op_errstr in local dictionary");
                goto out;
            }
        }
        ret = rsp->op_ret;
        goto out;
    }

    ret = dict_get_int32_sizen(dict, "sub-cmd", &status_cmd);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Could not fetch status type");
        goto out;
    }

    if ((status_cmd != GF_SNAP_STATUS_TYPE_SNAP) &&
        (status_cmd != GF_SNAP_STATUS_TYPE_ITER)) {
        dict_copy(dict, local->dict);
        goto out;
    }

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_snapshot_status_single_snap(local, dict, "status.snap0");
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "Failed to create xml output for snapshot status");
        }
    } else {
        ret = cli_get_single_snap_status(dict, "status.snap0");
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, "Could not fetch status of snap");
        }
    }

out:
    return ret;
}

static int
gf_cli_generate_snapshot_event(gf_cli_rsp *rsp, dict_t *dict, int32_t type,
                               char *snap_name, char *volname, char *snap_uuid,
                               char *clone_name)
{
    int ret = -1;
    int config_command = 0;
    int32_t delete_cmd = -1;
    uint64_t hard_limit = 0;
    uint64_t soft_limit = 0;
    char *auto_delete = NULL;
    char *snap_activate = NULL;
    char msg[PATH_MAX] = {
        0,
    };
    char option[512] = {
        0,
    };

    GF_VALIDATE_OR_GOTO("cli", dict, out);
    GF_VALIDATE_OR_GOTO("cli", rsp, out);

    switch (type) {
        case GF_SNAP_OPTION_TYPE_CREATE:
            if (!snap_name) {
                gf_log("cli", GF_LOG_ERROR, "Failed to get snap name");
                goto out;
            }

            if (!volname) {
                gf_log("cli", GF_LOG_ERROR, "Failed to get volume name");
                goto out;
            }

            if (rsp->op_ret != 0) {
                gf_event(EVENT_SNAPSHOT_CREATE_FAILED,
                         "snapshot_name=%s;volume_name=%s;error=%s", snap_name,
                         volname,
                         rsp->op_errstr ? rsp->op_errstr
                                        : "Please check log file for details");
                ret = 0;
                break;
            }

            if (!snap_uuid) {
                gf_log("cli", GF_LOG_ERROR, "Failed to get snap uuid");
                goto out;
            }

            gf_event(EVENT_SNAPSHOT_CREATED,
                     "snapshot_name=%s;volume_name=%s;snapshot_uuid=%s",
                     snap_name, volname, snap_uuid);

            ret = 0;
            break;

        case GF_SNAP_OPTION_TYPE_ACTIVATE:
            if (!snap_name) {
                gf_log("cli", GF_LOG_ERROR, "Failed to get snap name");
                goto out;
            }

            if (rsp->op_ret != 0) {
                gf_event(EVENT_SNAPSHOT_ACTIVATE_FAILED,
                         "snapshot_name=%s;error=%s", snap_name,
                         rsp->op_errstr ? rsp->op_errstr
                                        : "Please check log file for details");
                ret = 0;
                break;
            }

            if (!snap_uuid) {
                gf_log("cli", GF_LOG_ERROR, "Failed to get snap uuid");
                goto out;
            }

            gf_event(EVENT_SNAPSHOT_ACTIVATED,
                     "snapshot_name=%s;snapshot_uuid=%s", snap_name, snap_uuid);

            ret = 0;
            break;

        case GF_SNAP_OPTION_TYPE_DEACTIVATE:
            if (!snap_name) {
                gf_log("cli", GF_LOG_ERROR, "Failed to get snap name");
                goto out;
            }

            if (rsp->op_ret != 0) {
                gf_event(EVENT_SNAPSHOT_DEACTIVATE_FAILED,
                         "snapshot_name=%s;error=%s", snap_name,
                         rsp->op_errstr ? rsp->op_errstr
                                        : "Please check log file for details");
                ret = 0;
                break;
            }

            if (!snap_uuid) {
                gf_log("cli", GF_LOG_ERROR, "Failed to get snap uuid");
                goto out;
            }

            gf_event(EVENT_SNAPSHOT_DEACTIVATED,
                     "snapshot_name=%s;snapshot_uuid=%s", snap_name, snap_uuid);

            ret = 0;
            break;

        case GF_SNAP_OPTION_TYPE_RESTORE:
            if (!snap_name) {
                gf_log("cli", GF_LOG_ERROR, "Failed to get snap name");
                goto out;
            }

            if (rsp->op_ret != 0) {
                gf_event(EVENT_SNAPSHOT_RESTORE_FAILED,
                         "snapshot_name=%s;error=%s", snap_name,
                         rsp->op_errstr ? rsp->op_errstr
                                        : "Please check log file for details");
                ret = 0;
                break;
            }

            if (!snap_uuid) {
                ret = -1;
                gf_log("cli", GF_LOG_ERROR, "Failed to get snap uuid");
                goto out;
            }

            if (!volname) {
                ret = -1;
                gf_log("cli", GF_LOG_ERROR, "Failed to get volname");
                goto out;
            }

            gf_event(EVENT_SNAPSHOT_RESTORED,
                     "snapshot_name=%s;snapshot_uuid=%s;volume_name=%s",
                     snap_name, snap_uuid, volname);

            ret = 0;
            break;

        case GF_SNAP_OPTION_TYPE_DELETE:
            ret = dict_get_int32_sizen(dict, "sub-cmd", &delete_cmd);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, "Could not get sub-cmd");
                goto out;
            }

            /*
             * Need not generate any event (success or failure) for delete *
             * all, as it will trigger individual delete for all snapshots *
             */
            if (delete_cmd == GF_SNAP_DELETE_TYPE_ALL) {
                ret = 0;
                break;
            }

            if (!snap_name) {
                gf_log("cli", GF_LOG_ERROR, "Failed to get snap name");
                goto out;
            }

            if (rsp->op_ret != 0) {
                gf_event(EVENT_SNAPSHOT_DELETE_FAILED,
                         "snapshot_name=%s;error=%s", snap_name,
                         rsp->op_errstr ? rsp->op_errstr
                                        : "Please check log file for details");
                ret = 0;
                break;
            }

            if (!snap_uuid) {
                ret = -1;
                gf_log("cli", GF_LOG_ERROR, "Failed to get snap uuid");
                goto out;
            }

            gf_event(EVENT_SNAPSHOT_DELETED,
                     "snapshot_name=%s;snapshot_uuid=%s", snap_name, snap_uuid);

            ret = 0;
            break;

        case GF_SNAP_OPTION_TYPE_CLONE:
            if (!clone_name) {
                gf_log("cli", GF_LOG_ERROR, "Failed to get clone name");
                goto out;
            }

            if (!snap_name) {
                gf_log("cli", GF_LOG_ERROR, "Failed to get snapname name");
                goto out;
            }

            if (rsp->op_ret != 0) {
                gf_event(EVENT_SNAPSHOT_CLONE_FAILED,
                         "snapshot_name=%s;clone_name=%s;error=%s", snap_name,
                         clone_name,
                         rsp->op_errstr ? rsp->op_errstr
                                        : "Please check log file for details");
                ret = 0;
                break;
            }

            if (!snap_uuid) {
                ret = -1;
                gf_log("cli", GF_LOG_ERROR, "Failed to get snap uuid");
                goto out;
            }

            gf_event(EVENT_SNAPSHOT_CLONED,
                     "snapshot_name=%s;clone_name=%s;clone_uuid=%s", snap_name,
                     clone_name, snap_uuid);

            ret = 0;
            break;

        case GF_SNAP_OPTION_TYPE_CONFIG:
            if (rsp->op_ret != 0) {
                gf_event(EVENT_SNAPSHOT_CONFIG_UPDATE_FAILED, "error=%s",
                         rsp->op_errstr ? rsp->op_errstr
                                        : "Please check log file for details");
                ret = 0;
                break;
            }

            ret = dict_get_int32_sizen(dict, "config-command", &config_command);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, "Could not fetch config type");
                goto out;
            }

            if (config_command == GF_SNAP_CONFIG_DISPLAY) {
                ret = 0;
                break;
            }

            /* These are optional parameters therefore ignore the error */
            ret = dict_get_uint64(dict, "snap-max-hard-limit", &hard_limit);
            ret = dict_get_uint64(dict, "snap-max-soft-limit", &soft_limit);
            ret = dict_get_str_sizen(dict, "auto-delete", &auto_delete);
            ret = dict_get_str_sizen(dict, "snap-activate-on-create",
                                     &snap_activate);

            if (!hard_limit && !soft_limit && !auto_delete && !snap_activate) {
                ret = -1;
                gf_log("cli", GF_LOG_ERROR,
                       "At least one option from "
                       "snap-max-hard-limit, snap-max-soft-limit, "
                       "auto-delete and snap-activate-on-create "
                       "should be set");
                goto out;
            }

            if (hard_limit || soft_limit) {
                snprintf(option, sizeof(option), "%s=%" PRIu64,
                         hard_limit ? "hard_limit" : "soft_limit",
                         hard_limit ? hard_limit : soft_limit);
            } else if (auto_delete || snap_activate) {
                snprintf(option, sizeof(option), "%s=%s",
                         auto_delete ? "auto-delete" : "snap-activate",
                         auto_delete ? auto_delete : snap_activate);
            }

            volname = NULL;
            ret = dict_get_str_sizen(dict, "volname", &volname);

            snprintf(msg, sizeof(msg), "config_type=%s;%s",
                     volname ? "volume_config" : "system_config", option);

            gf_event(EVENT_SNAPSHOT_CONFIG_UPDATED, "%s", msg);

            ret = 0;
            break;

        default:
            gf_log("cli", GF_LOG_WARNING,
                   "Cannot generate event for unknown type.");
            ret = 0;
            goto out;
    }

out:
    return ret;
}

/*
 * Fetch necessary data from dict at one place instead of *
 * repeating the same code again and again.               *
 */
static int
gf_cli_snapshot_get_data_from_dict(dict_t *dict, char **snap_name,
                                   char **volname, char **snap_uuid,
                                   int8_t *soft_limit_flag, char **clone_name)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO("cli", dict, out);

    if (snap_name) {
        ret = dict_get_str_sizen(dict, "snapname", snap_name);
        if (ret) {
            gf_log("cli", GF_LOG_DEBUG, "failed to get snapname from dict");
        }
    }

    if (volname) {
        ret = dict_get_str_sizen(dict, "volname1", volname);
        if (ret) {
            gf_log("cli", GF_LOG_DEBUG, "failed to get volname1 from dict");
        }
    }

    if (snap_uuid) {
        ret = dict_get_str_sizen(dict, "snapuuid", snap_uuid);
        if (ret) {
            gf_log("cli", GF_LOG_DEBUG, "failed to get snapuuid from dict");
        }
    }

    if (soft_limit_flag) {
        ret = dict_get_int8(dict, "soft-limit-reach", soft_limit_flag);
        if (ret) {
            gf_log("cli", GF_LOG_DEBUG,
                   "failed to get soft-limit-reach from dict");
        }
    }

    if (clone_name) {
        ret = dict_get_str_sizen(dict, "clonename", clone_name);
        if (ret) {
            gf_log("cli", GF_LOG_DEBUG, "failed to get clonename from dict");
        }
    }

    ret = 0;
out:
    return ret;
}

static int
gf_cli_snapshot_cbk(struct rpc_req *req, struct iovec *iov, int count,
                    void *myframe)
{
    int ret = -1;
    gf_cli_rsp rsp = {
        0,
    };
    dict_t *dict = NULL;
    char *snap_name = NULL;
    char *clone_name = NULL;
    int32_t type = 0;
    call_frame_t *frame = NULL;
    gf_boolean_t snap_driven = _gf_false;
    int8_t soft_limit_flag = -1;
    char *volname = NULL;
    char *snap_uuid = NULL;

    GF_ASSERT(myframe);

    if (req->rpc_status == -1) {
        goto out;
    }

    frame = myframe;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, XDR_DECODE_FAIL);
        goto out;
    }

    dict = dict_new();

    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);

    if (ret)
        goto out;

    ret = dict_get_int32_sizen(dict, "type", &type);
    if (ret) {
        gf_log(frame->this->name, GF_LOG_ERROR, "failed to get type");
        goto out;
    }

    ret = gf_cli_snapshot_get_data_from_dict(
        dict, &snap_name, &volname, &snap_uuid, &soft_limit_flag, &clone_name);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Failed to fetch data from dict.");
        goto out;
    }

#if (USE_EVENTS)
    ret = gf_cli_generate_snapshot_event(&rsp, dict, type, snap_name, volname,
                                         snap_uuid, clone_name);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Failed to generate snapshot event");
        goto out;
    }
#endif

    /* Snapshot status and delete command is handled separately */
    if (global_state->mode & GLUSTER_MODE_XML &&
        GF_SNAP_OPTION_TYPE_STATUS != type &&
        GF_SNAP_OPTION_TYPE_DELETE != type) {
        ret = cli_xml_output_snapshot(type, dict, rsp.op_ret, rsp.op_errno,
                                      rsp.op_errstr);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        }

        goto out;
    }

    switch (type) {
        case GF_SNAP_OPTION_TYPE_CREATE:
            if (rsp.op_ret) {
                cli_err("snapshot create: failed: %s",
                        rsp.op_errstr ? rsp.op_errstr
                                      : "Please check log file for details");
                ret = rsp.op_ret;
                goto out;
            }

            if (!snap_name) {
                ret = -1;
                gf_log("cli", GF_LOG_ERROR, "Failed to get snap name");
                goto out;
            }

            if (!volname) {
                ret = -1;
                gf_log("cli", GF_LOG_ERROR, "Failed to get volume name");
                goto out;
            }

            cli_out("snapshot create: success: Snap %s created successfully",
                    snap_name);

            if (soft_limit_flag == 1) {
                cli_out(
                    "Warning: Soft-limit of volume (%s) is "
                    "reached. Snapshot creation is not possible "
                    "once hard-limit is reached.",
                    volname);
            }
            ret = 0;
            break;

        case GF_SNAP_OPTION_TYPE_CLONE:
            if (rsp.op_ret) {
                cli_err("snapshot clone: failed: %s",
                        rsp.op_errstr ? rsp.op_errstr
                                      : "Please check log file for details");
                ret = rsp.op_ret;
                goto out;
            }

            if (!clone_name) {
                gf_log("cli", GF_LOG_ERROR, "Failed to get clone name");
                goto out;
            }

            if (!snap_name) {
                gf_log("cli", GF_LOG_ERROR, "Failed to get snapname name");
                goto out;
            }

            cli_out("snapshot clone: success: Clone %s created successfully",
                    clone_name);

            ret = 0;
            break;

        case GF_SNAP_OPTION_TYPE_RESTORE:
            if (rsp.op_ret) {
                cli_err("snapshot restore: failed: %s",
                        rsp.op_errstr ? rsp.op_errstr
                                      : "Please check log file for details");
                ret = rsp.op_ret;
                goto out;
            }

            if (!snap_name) {
                gf_log("cli", GF_LOG_ERROR, "Failed to get snap name");
                goto out;
            }

            cli_out("Snapshot restore: %s: Snap restored successfully",
                    snap_name);

            ret = 0;
            break;
        case GF_SNAP_OPTION_TYPE_ACTIVATE:
            if (rsp.op_ret) {
                cli_err("snapshot activate: failed: %s",
                        rsp.op_errstr ? rsp.op_errstr
                                      : "Please check log file for details");
                ret = rsp.op_ret;
                goto out;
            }

            if (!snap_name) {
                gf_log("cli", GF_LOG_ERROR, "Failed to get snap name");
                goto out;
            }

            cli_out("Snapshot activate: %s: Snap activated successfully",
                    snap_name);

            ret = 0;
            break;

        case GF_SNAP_OPTION_TYPE_DEACTIVATE:
            if (rsp.op_ret) {
                cli_err("snapshot deactivate: failed: %s",
                        rsp.op_errstr ? rsp.op_errstr
                                      : "Please check log file for details");
                ret = rsp.op_ret;
                goto out;
            }

            if (!snap_name) {
                gf_log("cli", GF_LOG_ERROR, "Failed to get snap name");
                goto out;
            }

            cli_out("Snapshot deactivate: %s: Snap deactivated successfully",
                    snap_name);

            ret = 0;
            break;
        case GF_SNAP_OPTION_TYPE_INFO:
            if (rsp.op_ret) {
                cli_err("Snapshot info : failed: %s",
                        rsp.op_errstr ? rsp.op_errstr
                                      : "Please check log file for details");
                ret = rsp.op_ret;
                goto out;
            }

            snap_driven = dict_get_str_boolean(dict, "snap-driven", _gf_false);
            if (snap_driven == _gf_true) {
                ret = cli_call_snapshot_info(dict, snap_driven);
                if (ret) {
                    gf_log("cli", GF_LOG_ERROR, "Snapshot info failed");
                    goto out;
                }
            } else if (snap_driven == _gf_false) {
                ret = cli_get_snaps_in_volume(dict);
                if (ret) {
                    gf_log("cli", GF_LOG_ERROR, "Snapshot info failed");
                    goto out;
                }
            }
            break;

        case GF_SNAP_OPTION_TYPE_CONFIG:
            ret = cli_snapshot_config_display(dict, &rsp);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR,
                       "Failed to display snapshot config output.");
                goto out;
            }
            break;

        case GF_SNAP_OPTION_TYPE_LIST:
            if (rsp.op_ret) {
                cli_err("Snapshot list : failed: %s",
                        rsp.op_errstr ? rsp.op_errstr
                                      : "Please check log file for details");
                ret = rsp.op_ret;
                goto out;
            }

            ret = cli_snapshot_list(dict);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, "Failed to display snapshot list");
                goto out;
            }
            break;

        case GF_SNAP_OPTION_TYPE_DELETE:
            ret = cli_snapshot_remove_reply(&rsp, dict, frame);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, "Failed to delete snap");
                goto out;
            }
            break;

        case GF_SNAP_OPTION_TYPE_STATUS:
            ret = cli_snapshot_status(dict, &rsp, frame);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR,
                       "Failed to display snapshot status output.");
                goto out;
            }
            break;

        default:
            cli_err("Unknown command executed");
            ret = -1;
            goto out;
    }
out:
    if (dict)
        dict_unref(dict);
    cli_cmd_broadcast_response(ret);

    free(rsp.dict.dict_val);
    free(rsp.op_errstr);

    return ret;
}

int32_t
gf_cli_snapshot_for_delete(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int32_t ret = -1;
    int32_t cmd = -1;
    cli_local_t *local = NULL;
    dict_t *snap_dict = NULL;
    int32_t snapcount = 0;
    int i = 0;
    char question[PATH_MAX] = "";
    char *volname = NULL;
    gf_answer_t answer = GF_ANSWER_NO;

    GF_VALIDATE_OR_GOTO("cli", frame, out);
    GF_VALIDATE_OR_GOTO("cli", frame->local, out);
    GF_VALIDATE_OR_GOTO("cli", this, out);
    GF_VALIDATE_OR_GOTO("cli", data, out);

    local = frame->local;

    ret = dict_get_int32_sizen(local->dict, "sub-cmd", &cmd);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Failed to get sub-cmd");
        goto out;
    }

    /* No need multiple RPCs for individual snapshot delete*/
    if (cmd == GF_SNAP_DELETE_TYPE_SNAP) {
        ret = 0;
        goto out;
    }

    ret = dict_get_int32_sizen(local->dict, "snapcount", &snapcount);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Could not get snapcount");
        goto out;
    }

    if (global_state->mode & GLUSTER_MODE_XML) {
#ifdef HAVE_LIB_XML
        ret = xmlTextWriterWriteFormatElement(
            local->writer, (xmlChar *)"snapCount", "%d", snapcount);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "Failed to write xml element \"snapCount\"");
            goto out;
        }
#endif /* HAVE_LIB_XML */
    } else if (snapcount == 0) {
        cli_out("No snapshots present");
        goto out;
    }

    if (cmd == GF_SNAP_DELETE_TYPE_ALL) {
        snprintf(question, sizeof(question),
                 "System contains %d snapshot(s).\nDo you still "
                 "want to continue and delete them? ",
                 snapcount);
    } else {
        ret = dict_get_str_sizen(local->dict, "volname", &volname);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "Failed to fetch volname from local dictionary");
            goto out;
        }

        snprintf(question, sizeof(question),
                 "Volume (%s) contains %d snapshot(s).\nDo you still want to "
                 "continue and delete them? ",
                 volname, snapcount);
    }

    answer = cli_cmd_get_confirmation(global_state, question);
    if (GF_ANSWER_NO == answer) {
        ret = 0;
        gf_log("cli", GF_LOG_DEBUG,
               "User cancelled snapshot delete operation for snap delete");
        goto out;
    }

    for (i = 1; i <= snapcount; i++) {
        ret = -1;

        snap_dict = dict_new();
        if (!snap_dict)
            goto out;

        ret = cli_populate_req_dict_for_delete(snap_dict, local->dict, i);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "Could not populate snap request dictionary");
            goto out;
        }

        ret = cli_to_glusterd(&req, frame, gf_cli_snapshot_cbk,
                              (xdrproc_t)xdr_gf_cli_req, snap_dict,
                              GLUSTER_CLI_SNAP, this, cli_rpc_prog, NULL);
        if (ret) {
            /* Fail the operation if deleting one of the
             * snapshots is failed
             */
            gf_log("cli", GF_LOG_ERROR,
                   "cli_to_glusterd for snapshot delete failed");
            goto out;
        }
        dict_unref(snap_dict);
        snap_dict = NULL;
    }

out:
    if (snap_dict)
        dict_unref(snap_dict);
    GF_FREE(req.dict.dict_val);

    return ret;
}

static int32_t
gf_cli_snapshot_for_status(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    int ret = -1;
    int32_t cmd = -1;
    cli_local_t *local = NULL;
    dict_t *snap_dict = NULL;
    int snapcount = 0;
    int i = 0;

    GF_VALIDATE_OR_GOTO("cli", frame, out);
    GF_VALIDATE_OR_GOTO("cli", frame->local, out);
    GF_VALIDATE_OR_GOTO("cli", this, out);
    GF_VALIDATE_OR_GOTO("cli", data, out);

    local = frame->local;

    ret = dict_get_int32_sizen(local->dict, "sub-cmd", &cmd);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Failed to get sub-cmd");
        goto out;
    }

    /* Snapshot status of single snap (i.e. GF_SNAP_STATUS_TYPE_SNAP)
     * is already handled. Therefore we can return from here.
     * If want to get status of all snaps in the system or volume then
     * we should get them one by one.*/
    if ((cmd == GF_SNAP_STATUS_TYPE_SNAP) ||
        (cmd == GF_SNAP_STATUS_TYPE_ITER)) {
        ret = 0;
        goto out;
    }

    ret = dict_get_int32_sizen(local->dict, "status.snapcount", &snapcount);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Could not get snapcount");
        goto out;
    }

    if (snapcount == 0 && !(global_state->mode & GLUSTER_MODE_XML)) {
        cli_out("No snapshots present");
    }

    for (i = 0; i < snapcount; i++) {
        ret = -1;

        snap_dict = dict_new();
        if (!snap_dict)
            goto out;

        ret = cli_populate_req_dict_for_status(snap_dict, local->dict, i);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "Could not populate snap request dictionary");
            goto out;
        }

        ret = cli_to_glusterd(&req, frame, gf_cli_snapshot_cbk,
                              (xdrproc_t)xdr_gf_cli_req, snap_dict,
                              GLUSTER_CLI_SNAP, this, cli_rpc_prog, NULL);

        /* Ignore the return value and error for snapshot
         * status of type "ALL" or "VOL"
         *
         * Scenario : There might be case where status command
         * and delete command might be issued at the same time.
         * In that case when status tried to fetch detail of
         * snap which has been deleted by concurrent command,
         * then it will show snapshot not present. Which will
         * not be appropriate.
         */
        if (ret && (cmd != GF_SNAP_STATUS_TYPE_ALL &&
                    cmd != GF_SNAP_STATUS_TYPE_VOL)) {
            gf_log("cli", GF_LOG_ERROR,
                   "cli_to_glusterd for snapshot status failed");
            goto out;
        }
        dict_unref(snap_dict);
        snap_dict = NULL;
    }

    ret = 0;
out:
    if (snap_dict)
        dict_unref(snap_dict);
    GF_FREE(req.dict.dict_val);

    return ret;
}

static int32_t
gf_cli_snapshot(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    dict_t *options = NULL;
    int ret = -1;
    int tmp_ret = -1;
    cli_local_t *local = NULL;
    char *err_str = NULL;
    int type = -1;

    if (!frame || !frame->local || !this || !data)
        goto out;

    local = frame->local;

    options = data;

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_snapshot_begin_composite_op(local);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "Failed to begin snapshot xml composite op");
            goto out;
        }
    }

    ret = cli_to_glusterd(&req, frame, gf_cli_snapshot_cbk,
                          (xdrproc_t)xdr_gf_cli_req, options, GLUSTER_CLI_SNAP,
                          this, cli_rpc_prog, NULL);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "cli_to_glusterd for snapshot failed");
        goto xmlend;
    }

    ret = dict_get_int32_sizen(local->dict, "type", &type);

    if (GF_SNAP_OPTION_TYPE_STATUS == type) {
        ret = gf_cli_snapshot_for_status(frame, this, data);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "cli to glusterd for snapshot status command failed");
        }

        goto xmlend;
    }

    if (GF_SNAP_OPTION_TYPE_DELETE == type) {
        ret = gf_cli_snapshot_for_delete(frame, this, data);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "cli to glusterd for snapshot delete command failed");
        }

        goto xmlend;
    }

    ret = 0;

xmlend:
    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_snapshot_end_composite_op(local);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "Failed to end snapshot xml composite op");
            goto out;
        }
    }
out:
    if (ret && local && GF_SNAP_OPTION_TYPE_STATUS == type) {
        tmp_ret = dict_get_str_sizen(local->dict, "op_err_str", &err_str);
        if (tmp_ret || !err_str) {
            cli_err("Snapshot Status : failed: %s",
                    "Please check log file for details");
        } else {
            cli_err("Snapshot Status : failed: %s", err_str);
            dict_del_sizen(local->dict, "op_err_str");
        }
    }

    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    GF_FREE(req.dict.dict_val);

    if (global_state->mode & GLUSTER_MODE_XML) {
        /* XML mode handles its own error */
        ret = 0;
    }
    return ret;
}

static int32_t
gf_cli_barrier_volume_cbk(struct rpc_req *req, struct iovec *iov, int count,
                          void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status)
        goto out;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }
    gf_log("cli", GF_LOG_DEBUG, "Received response to barrier");

    if (rsp.op_ret) {
        if (rsp.op_errstr && (strlen(rsp.op_errstr) > 1)) {
            cli_err("volume barrier: command unsuccessful : %s", rsp.op_errstr);
        } else {
            cli_err("volume barrier: command unsuccessful");
        }
    } else {
        cli_out("volume barrier: command successful");
    }
    ret = rsp.op_ret;

out:
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int
gf_cli_barrier_volume(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    dict_t *options = NULL;
    int ret = -1;

    options = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_barrier_volume_cbk,
                          (xdrproc_t)xdr_gf_cli_req, options,
                          GLUSTER_CLI_BARRIER_VOLUME, this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    GF_FREE(req.dict.dict_val);
    return ret;
}

static int32_t
gf_cli_get_vol_opt_cbk(struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
    gf_cli_rsp rsp = {
        0,
    };
    int ret = -1;
    dict_t *dict = NULL;
    char *key = NULL;
    char *value = NULL;
    char msg[1024] = {
        0,
    };
    int i = 0;
    char dict_key[50] = {
        0,
    };

    GF_ASSERT(myframe);

    if (-1 == req->rpc_status)
        goto out;

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }
    gf_log("cli", GF_LOG_DEBUG, "Received response to get volume option");

    if (rsp.op_ret) {
        if (strcmp(rsp.op_errstr, ""))
            snprintf(msg, sizeof(msg), "volume get option: failed: %s",
                     rsp.op_errstr);
        else
            snprintf(msg, sizeof(msg), "volume get option: failed");

        if (global_state->mode & GLUSTER_MODE_XML) {
            ret = cli_xml_output_str("volGetopts", msg, rsp.op_ret,
                                     rsp.op_errno, rsp.op_errstr);
            if (ret) {
                gf_log("cli", GF_LOG_ERROR, XML_ERROR);
            }
        } else {
            cli_err("%s", msg);
        }
        ret = rsp.op_ret;
        goto out_nolog;
    }
    dict = dict_new();

    if (!dict) {
        ret = -1;
        goto out;
    }

    ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
        goto out;
    }

    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_vol_getopts(dict, rsp.op_ret, rsp.op_errno,
                                         rsp.op_errstr);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
            ret = 0;
        }
        goto out;
    }

    ret = dict_get_str_sizen(dict, "warning", &value);
    if (!ret) {
        cli_out("%s", value);
    }

    ret = dict_get_int32_sizen(dict, "count", &count);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR,
               "Failed to retrieve count from the dictionary");
        goto out;
    }

    if (count <= 0) {
        gf_log("cli", GF_LOG_ERROR, "Value of count :%d is invalid", count);
        ret = -1;
        goto out;
    }

    cli_out("%-40s%-40s", "Option", "Value");
    cli_out("%-40s%-40s", "------", "-----");
    for (i = 1; i <= count; i++) {
        ret = snprintf(dict_key, sizeof dict_key, "key%d", i);
        ret = dict_get_strn(dict, dict_key, ret, &key);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "Failed to retrieve %s from the dictionary", dict_key);
            goto out;
        }
        ret = snprintf(dict_key, sizeof dict_key, "value%d", i);
        ret = dict_get_strn(dict, dict_key, ret, &value);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR,
                   "Failed to retrieve key value for %s from the dictionary",
                   dict_key);
            goto out;
        }
        cli_out("%-40s%-40s", key, value);
    }

out:
    if (ret) {
        cli_out(
            "volume get option failed. Check the cli/glusterd log "
            "file for more details");
    }

out_nolog:
    if (dict)
        dict_unref(dict);
    cli_cmd_broadcast_response(ret);
    gf_free_xdr_cli_rsp(rsp);
    return ret;
}

static int
gf_cli_get_vol_opt(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    dict_t *options = NULL;
    int ret = -1;

    options = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_get_vol_opt_cbk,
                          (xdrproc_t)xdr_gf_cli_req, options,
                          GLUSTER_CLI_GET_VOL_OPT, this, cli_rpc_prog, NULL);
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    GF_FREE(req.dict.dict_val);
    return ret;
}

static int
add_cli_cmd_timeout_to_dict(dict_t *dict)
{
    int ret = 0;

    if (cli_default_conn_timeout > 120) {
        ret = dict_set_uint32(dict, "timeout", cli_default_conn_timeout);
        if (ret) {
            gf_log("cli", GF_LOG_INFO, "Failed to save timeout to dict");
        }
    }
    return ret;
}

static int
cli_to_glusterd(gf_cli_req *req, call_frame_t *frame, fop_cbk_fn_t cbkfn,
                xdrproc_t xdrproc, dict_t *dict, int procnum, xlator_t *this,
                rpc_clnt_prog_t *prog, struct iobref *iobref)
{
    int ret = 0;
    size_t len = 0;
    char *cmd = NULL;
    int i = 0;
    const char **words = NULL;
    cli_local_t *local = NULL;

    if (!this || !frame || !frame->local || !dict) {
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
        len += strlen(words[i++]) + 1;

    cmd = GF_MALLOC(len + 1, gf_common_mt_char);
    if (!cmd) {
        ret = -1;
        goto out;
    }
    cmd[0] = '\0';

    for (i = 0; words[i]; i++) {
        strncat(cmd, words[i], len - 1);
        if (words[i + 1] != NULL)
            strncat(cmd, " ", len - 1);
    }

    ret = dict_set_dynstr_sizen(dict, "cmd-str", cmd);
    if (ret)
        goto out;

    ret = add_cli_cmd_timeout_to_dict(dict);

    ret = dict_allocate_and_serialize(dict, &(req->dict).dict_val,
                                      &(req->dict).dict_len);

    if (ret < 0) {
        gf_log(this->name, GF_LOG_DEBUG,
               "failed to get serialized length of dict");
        goto out;
    }

    ret = cli_cmd_submit(NULL, req, frame, prog, procnum, iobref, this, cbkfn,
                         (xdrproc_t)xdrproc);
out:
    return ret;
}

static int
gf_cli_print_bitrot_scrub_status(dict_t *dict)
{
    int i = 1;
    int j = 0;
    int ret = -1;
    int count = 0;
    char key[64] = {
        0,
    };
    char *volname = NULL;
    char *node_name = NULL;
    char *scrub_freq = NULL;
    char *state_scrub = NULL;
    char *scrub_impact = NULL;
    char *bad_file_str = NULL;
    char *scrub_log_file = NULL;
    char *bitrot_log_file = NULL;
    uint64_t scrub_files = 0;
    uint64_t unsigned_files = 0;
    uint64_t scrub_time = 0;
    uint64_t days = 0;
    uint64_t hours = 0;
    uint64_t minutes = 0;
    uint64_t seconds = 0;
    char *last_scrub = NULL;
    uint64_t error_count = 0;
    int8_t scrub_running = 0;
    char *scrub_state_op = NULL;

    ret = dict_get_int32_sizen(dict, "count", &count);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR,
               "failed to get count value from dictionary");
        goto out;
    }

    ret = dict_get_str_sizen(dict, "volname", &volname);
    if (ret)
        gf_log("cli", GF_LOG_TRACE, "failed to get volume name");

    ret = dict_get_str_sizen(dict, "features.scrub", &state_scrub);
    if (ret)
        gf_log("cli", GF_LOG_TRACE, "failed to get scrub state value");

    ret = dict_get_str_sizen(dict, "features.scrub-throttle", &scrub_impact);
    if (ret)
        gf_log("cli", GF_LOG_TRACE, "failed to get scrub impact value");

    ret = dict_get_str_sizen(dict, "features.scrub-freq", &scrub_freq);
    if (ret)
        gf_log("cli", GF_LOG_TRACE, "failed to get scrub -freq value");

    ret = dict_get_str_sizen(dict, "bitrot_log_file", &bitrot_log_file);
    if (ret)
        gf_log("cli", GF_LOG_TRACE, "failed to get bitrot log file location");

    ret = dict_get_str_sizen(dict, "scrub_log_file", &scrub_log_file);
    if (ret)
        gf_log("cli", GF_LOG_TRACE, "failed to get scrubber log file location");

    for (i = 1; i <= count; i++) {
        snprintf(key, sizeof(key), "scrub-running-%d", i);
        ret = dict_get_int8(dict, key, &scrub_running);
        if (ret)
            gf_log("cli", GF_LOG_TRACE, "failed to get scrubbed files");
        if (scrub_running)
            break;
    }

    if (scrub_running)
        gf_asprintf(&scrub_state_op, "%s (In Progress)", state_scrub);
    else
        gf_asprintf(&scrub_state_op, "%s (Idle)", state_scrub);

    cli_out("\n%s: %s\n", "Volume name ", volname);

    cli_out("%s: %s\n", "State of scrub", scrub_state_op);

    cli_out("%s: %s\n", "Scrub impact", scrub_impact);

    cli_out("%s: %s\n", "Scrub frequency", scrub_freq);

    cli_out("%s: %s\n", "Bitrot error log location", bitrot_log_file);

    cli_out("%s: %s\n", "Scrubber error log location", scrub_log_file);

    for (i = 1; i <= count; i++) {
        /* Reset the variables to prevent carryover of values */
        node_name = NULL;
        last_scrub = NULL;
        scrub_time = 0;
        error_count = 0;
        scrub_files = 0;
        unsigned_files = 0;

        snprintf(key, sizeof(key), "node-name-%d", i);
        ret = dict_get_str(dict, key, &node_name);
        if (ret)
            gf_log("cli", GF_LOG_TRACE, "failed to get node-name");

        snprintf(key, sizeof(key), "scrubbed-files-%d", i);
        ret = dict_get_uint64(dict, key, &scrub_files);
        if (ret)
            gf_log("cli", GF_LOG_TRACE, "failed to get scrubbed files");

        snprintf(key, sizeof(key), "unsigned-files-%d", i);
        ret = dict_get_uint64(dict, key, &unsigned_files);
        if (ret)
            gf_log("cli", GF_LOG_TRACE, "failed to get unsigned files");

        snprintf(key, sizeof(key), "scrub-duration-%d", i);
        ret = dict_get_uint64(dict, key, &scrub_time);
        if (ret)
            gf_log("cli", GF_LOG_TRACE, "failed to get last scrub duration");

        snprintf(key, sizeof(key), "last-scrub-time-%d", i);
        ret = dict_get_str(dict, key, &last_scrub);
        if (ret)
            gf_log("cli", GF_LOG_TRACE, "failed to get last scrub time");
        snprintf(key, sizeof(key), "error-count-%d", i);
        ret = dict_get_uint64(dict, key, &error_count);
        if (ret)
            gf_log("cli", GF_LOG_TRACE, "failed to get error count");

        cli_out("\n%s\n",
                "=========================================================");

        cli_out("%s: %s\n", "Node", node_name);

        cli_out("%s: %" PRIu64 "\n", "Number of Scrubbed files", scrub_files);

        cli_out("%s: %" PRIu64 "\n", "Number of Skipped files", unsigned_files);

        if ((!last_scrub) || !strcmp(last_scrub, ""))
            cli_out("%s: %s\n", "Last completed scrub time",
                    "Scrubber pending to complete.");
        else
            cli_out("%s: %s\n", "Last completed scrub time", last_scrub);

        /* Printing last scrub duration time in human readable form*/
        seconds = scrub_time % 60;
        minutes = (scrub_time / 60) % 60;
        hours = (scrub_time / 3600) % 24;
        days = scrub_time / 86400;
        cli_out("%s: %" PRIu64 ":%" PRIu64 ":%" PRIu64 ":%" PRIu64 "\n",
                "Duration of last scrub (D:M:H:M:S)", days, hours, minutes,
                seconds);

        cli_out("%s: %" PRIu64 "\n", "Error count", error_count);

        if (error_count) {
            cli_out("%s:\n", "Corrupted object's [GFID]");
            /* Printing list of bad file's (Corrupted object's)*/
            for (j = 0; j < error_count; j++) {
                snprintf(key, sizeof(key), "quarantine-%d-%d", j, i);
                ret = dict_get_str(dict, key, &bad_file_str);
                if (!ret) {
                    cli_out("%s\n", bad_file_str);
                }
            }
        }
    }
    cli_out("%s\n",
            "=========================================================");

out:
    GF_FREE(scrub_state_op);
    return 0;
}

static int
gf_cli_bitrot_cbk(struct rpc_req *req, struct iovec *iov, int count,
                  void *myframe)
{
    int ret = -1;
    int type = 0;
    gf_cli_rsp rsp = {
        0,
    };
    dict_t *dict = NULL;
    char *scrub_cmd = NULL;
    char *volname = NULL;
    char *cmd_str = NULL;
    char *cmd_op = NULL;

    GF_ASSERT(myframe);

    if (req->rpc_status == -1) {
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_cli_rsp);
    if (ret < 0) {
        gf_log(((call_frame_t *)myframe)->this->name, GF_LOG_ERROR,
               XDR_DECODE_FAIL);
        goto out;
    }

    if (rsp.op_ret) {
        ret = -1;
        if (global_state->mode & GLUSTER_MODE_XML)
            goto xml_output;

        if (strcmp(rsp.op_errstr, ""))
            cli_err("Bitrot command failed : %s", rsp.op_errstr);
        else
            cli_err("Bitrot command : failed");

        goto out;
    }

    if (rsp.dict.dict_len) {
        /* Unserialize the dictionary */
        dict = dict_new();

        if (!dict) {
            ret = -1;
            goto out;
        }

        ret = dict_unserialize(rsp.dict.dict_val, rsp.dict.dict_len, &dict);

        if (ret) {
            gf_log("cli", GF_LOG_ERROR, DICT_UNSERIALIZE_FAIL);
            goto out;
        }
    }

    gf_log("cli", GF_LOG_DEBUG, "Received resp to bit rot command");

    ret = dict_get_int32_sizen(dict, "type", &type);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "Failed to get command type");
        goto out;
    }

    if ((type == GF_BITROT_CMD_SCRUB_STATUS) &&
        !(global_state->mode & GLUSTER_MODE_XML)) {
        ret = gf_cli_print_bitrot_scrub_status(dict);
        if (ret) {
            gf_log("cli", GF_LOG_ERROR, "Failed to print bitrot scrub status");
        }
        goto out;
    }

    /* Ignoring the error, as using dict val for cli output only */
    ret = dict_get_str_sizen(dict, "volname", &volname);
    if (ret)
        gf_log("cli", GF_LOG_TRACE, "failed to get volume name");

    ret = dict_get_str_sizen(dict, "scrub-value", &scrub_cmd);
    if (ret)
        gf_log("cli", GF_LOG_TRACE, "Failed to get scrub command");

    ret = dict_get_str_sizen(dict, "cmd-str", &cmd_str);
    if (ret)
        gf_log("cli", GF_LOG_TRACE, "failed to get command string");

    if (cmd_str)
        cmd_op = strrchr(cmd_str, ' ') + 1;

    switch (type) {
        case GF_BITROT_OPTION_TYPE_ENABLE:
            cli_out("volume bitrot: success bitrot enabled for volume %s",
                    volname);
            ret = 0;
            goto out;
        case GF_BITROT_OPTION_TYPE_DISABLE:
            cli_out("volume bitrot: success bitrot disabled for volume %s",
                    volname);
            ret = 0;
            goto out;
        case GF_BITROT_CMD_SCRUB_ONDEMAND:
            cli_out("volume bitrot: scrubber started ondemand for volume %s",
                    volname);
            ret = 0;
            goto out;
        case GF_BITROT_OPTION_TYPE_SCRUB:
            if (!strncmp("pause", scrub_cmd, sizeof("pause")))
                cli_out("volume bitrot: scrubber paused for volume %s",
                        volname);
            if (!strncmp("resume", scrub_cmd, sizeof("resume")))
                cli_out("volume bitrot: scrubber resumed for volume %s",
                        volname);
            ret = 0;
            goto out;
        case GF_BITROT_OPTION_TYPE_SCRUB_FREQ:
            cli_out(
                "volume bitrot: scrub-frequency is set to %s "
                "successfully for volume %s",
                cmd_op, volname);
            ret = 0;
            goto out;
        case GF_BITROT_OPTION_TYPE_SCRUB_THROTTLE:
            cli_out(
                "volume bitrot: scrub-throttle is set to %s "
                "successfully for volume %s",
                cmd_op, volname);
            ret = 0;
            goto out;
    }

xml_output:
    if (global_state->mode & GLUSTER_MODE_XML) {
        ret = cli_xml_output_vol_profile(dict, rsp.op_ret, rsp.op_errno,
                                         rsp.op_errstr);
        if (ret)
            gf_log("cli", GF_LOG_ERROR, XML_ERROR);
        goto out;
    }

    if (!rsp.op_ret)
        cli_out("volume bitrot: success");

    ret = rsp.op_ret;

out:
    if (dict)
        dict_unref(dict);

    gf_free_xdr_cli_rsp(rsp);
    cli_cmd_broadcast_response(ret);
    return ret;
}

static int32_t
gf_cli_bitrot(call_frame_t *frame, xlator_t *this, void *data)
{
    gf_cli_req req = {{
        0,
    }};
    dict_t *options = NULL;
    int ret = -1;

    options = data;

    ret = cli_to_glusterd(&req, frame, gf_cli_bitrot_cbk,
                          (xdrproc_t)xdr_gf_cli_req, options,
                          GLUSTER_CLI_BITROT, this, cli_rpc_prog, NULL);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "cli_to_glusterd for bitrot failed");
        goto out;
    }

out:
    gf_log("cli", GF_LOG_DEBUG, RETURNING, ret);

    GF_FREE(req.dict.dict_val);

    return ret;
}

static struct rpc_clnt_procedure gluster_cli_actors[GLUSTER_CLI_MAXVALUE] = {
    [GLUSTER_CLI_NULL] = {"NULL", NULL},
    [GLUSTER_CLI_PROBE] = {"PROBE_QUERY", gf_cli_probe},
    [GLUSTER_CLI_DEPROBE] = {"DEPROBE_QUERY", gf_cli_deprobe},
    [GLUSTER_CLI_LIST_FRIENDS] = {"LIST_FRIENDS", gf_cli_list_friends},
    [GLUSTER_CLI_UUID_RESET] = {"UUID_RESET", gf_cli3_1_uuid_reset},
    [GLUSTER_CLI_UUID_GET] = {"UUID_GET", gf_cli3_1_uuid_get},
    [GLUSTER_CLI_CREATE_VOLUME] = {"CREATE_VOLUME", gf_cli_create_volume},
    [GLUSTER_CLI_DELETE_VOLUME] = {"DELETE_VOLUME", gf_cli_delete_volume},
    [GLUSTER_CLI_START_VOLUME] = {"START_VOLUME", gf_cli_start_volume},
    [GLUSTER_CLI_STOP_VOLUME] = {"STOP_VOLUME", gf_cli_stop_volume},
    [GLUSTER_CLI_RENAME_VOLUME] = {"RENAME_VOLUME", gf_cli_rename_volume},
    [GLUSTER_CLI_DEFRAG_VOLUME] = {"DEFRAG_VOLUME", gf_cli_defrag_volume},
    [GLUSTER_CLI_GET_VOLUME] = {"GET_VOLUME", gf_cli_get_volume},
    [GLUSTER_CLI_GET_NEXT_VOLUME] = {"GET_NEXT_VOLUME", gf_cli_get_next_volume},
    [GLUSTER_CLI_SET_VOLUME] = {"SET_VOLUME", gf_cli_set_volume},
    [GLUSTER_CLI_ADD_BRICK] = {"ADD_BRICK", gf_cli_add_brick},
    [GLUSTER_CLI_REMOVE_BRICK] = {"REMOVE_BRICK", gf_cli_remove_brick},
    [GLUSTER_CLI_REPLACE_BRICK] = {"REPLACE_BRICK", gf_cli_replace_brick},
    [GLUSTER_CLI_LOG_ROTATE] = {"LOG ROTATE", gf_cli_log_rotate},
    [GLUSTER_CLI_GETSPEC] = {"GETSPEC", gf_cli_getspec},
    [GLUSTER_CLI_PMAP_PORTBYBRICK] = {"PMAP PORTBYBRICK", gf_cli_pmap_b2p},
    [GLUSTER_CLI_SYNC_VOLUME] = {"SYNC_VOLUME", gf_cli_sync_volume},
    [GLUSTER_CLI_RESET_VOLUME] = {"RESET_VOLUME", gf_cli_reset_volume},
    [GLUSTER_CLI_FSM_LOG] = {"FSM_LOG", gf_cli_fsm_log},
    [GLUSTER_CLI_GSYNC_SET] = {"GSYNC_SET", gf_cli_gsync_set},
    [GLUSTER_CLI_PROFILE_VOLUME] = {"PROFILE_VOLUME", gf_cli_profile_volume},
    [GLUSTER_CLI_QUOTA] = {"QUOTA", gf_cli_quota},
    [GLUSTER_CLI_TOP_VOLUME] = {"TOP_VOLUME", gf_cli_top_volume},
    [GLUSTER_CLI_GETWD] = {"GETWD", gf_cli_getwd},
    [GLUSTER_CLI_STATUS_VOLUME] = {"STATUS_VOLUME", gf_cli_status_volume},
    [GLUSTER_CLI_STATUS_ALL] = {"STATUS_ALL", gf_cli_status_volume_all},
    [GLUSTER_CLI_MOUNT] = {"MOUNT", gf_cli_mount},
    [GLUSTER_CLI_UMOUNT] = {"UMOUNT", gf_cli_umount},
    [GLUSTER_CLI_HEAL_VOLUME] = {"HEAL_VOLUME", gf_cli_heal_volume},
    [GLUSTER_CLI_STATEDUMP_VOLUME] = {"STATEDUMP_VOLUME",
                                      gf_cli_statedump_volume},
    [GLUSTER_CLI_LIST_VOLUME] = {"LIST_VOLUME", gf_cli_list_volume},
    [GLUSTER_CLI_CLRLOCKS_VOLUME] = {"CLEARLOCKS_VOLUME",
                                     gf_cli_clearlocks_volume},
    [GLUSTER_CLI_COPY_FILE] = {"COPY_FILE", gf_cli_copy_file},
    [GLUSTER_CLI_SYS_EXEC] = {"SYS_EXEC", gf_cli_sys_exec},
    [GLUSTER_CLI_SNAP] = {"SNAP", gf_cli_snapshot},
    [GLUSTER_CLI_BARRIER_VOLUME] = {"BARRIER VOLUME", gf_cli_barrier_volume},
    [GLUSTER_CLI_GET_VOL_OPT] = {"GET_VOL_OPT", gf_cli_get_vol_opt},
    [GLUSTER_CLI_BITROT] = {"BITROT", gf_cli_bitrot},
    [GLUSTER_CLI_GET_STATE] = {"GET_STATE", gf_cli_get_state},
    [GLUSTER_CLI_RESET_BRICK] = {"RESET_BRICK", gf_cli_reset_brick},
    [GLUSTER_CLI_GANESHA] = {"GANESHA", gf_cli_ganesha},
};

struct rpc_clnt_program cli_prog = {
    .progname = "Gluster CLI",
    .prognum = GLUSTER_CLI_PROGRAM,
    .progver = GLUSTER_CLI_VERSION,
    .numproc = GLUSTER_CLI_MAXVALUE,
    .proctable = gluster_cli_actors,
};

static struct rpc_clnt_procedure cli_quotad_procs[GF_AGGREGATOR_MAXVALUE] = {
    [GF_AGGREGATOR_NULL] = {"NULL", NULL},
    [GF_AGGREGATOR_LOOKUP] = {"LOOKUP", NULL},
    [GF_AGGREGATOR_GETLIMIT] = {"GETLIMIT", cli_quotad_getlimit},
};

struct rpc_clnt_program cli_quotad_clnt = {
    .progname = "CLI Quotad client",
    .prognum = GLUSTER_AGGREGATOR_PROGRAM,
    .progver = GLUSTER_AGGREGATOR_VERSION,
    .numproc = GF_AGGREGATOR_MAXVALUE,
    .proctable = cli_quotad_procs,
};
