/*
  Copyright (c) 2007-2011 Gluster, Inc. <http://www.gluster.com>
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

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif /* _CONFIG_H */

#include "glusterfs.h"
#include "stack.h"
#include "dict.h"
#include "event.h"
#include "defaults.h"

#include "rpc-clnt.h"
#include "protocol-common.h"
#include "glusterfs3.h"
#include "portmap-xdr.h"
#include "xdr-generic.h"

#include "glusterfsd.h"
#include "glusterfsd-mem-types.h"
#include "rpcsvc.h"
#include "cli1-xdr.h"

static char is_mgmt_rpc_reconnect;

int glusterfs_mgmt_pmap_signin (glusterfs_ctx_t *ctx);
int glusterfs_volfile_fetch (glusterfs_ctx_t *ctx);
int glusterfs_process_volfp (glusterfs_ctx_t *ctx, FILE *fp);
int glusterfs_graph_unknown_options (glusterfs_graph_t *graph);

int
mgmt_cbk_spec (void *data)
{
        glusterfs_ctx_t *ctx = NULL;

        ctx = glusterfs_ctx_get ();
        gf_log ("mgmt", GF_LOG_INFO, "Volume file changed");

        glusterfs_volfile_fetch (ctx);
        return 0;
}

struct iobuf *
glusterfs_serialize_reply (rpcsvc_request_t *req, void *arg,
                           struct iovec *outmsg, xdrproc_t xdrproc)
{
        struct iobuf            *iob = NULL;
        ssize_t                  retlen = -1;
        ssize_t                  xdr_size = 0;

        /* First, get the io buffer into which the reply in arg will
         * be serialized.
         */
        xdr_size = xdr_sizeof (xdrproc, arg);
        iob = iobuf_get2 (req->svc->ctx->iobuf_pool, xdr_size);
        if (!iob) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to get iobuf");
                goto ret;
        }

        iobuf_to_iovec (iob, outmsg);
        /* Use the given serializer to translate the give C structure in arg
         * to XDR format which will be written into the buffer in outmsg.
         */
        /* retlen is used to received the error since size_t is unsigned and we
         * need -1 for error notification during encoding.
         */
        retlen = xdr_serialize_generic (*outmsg, arg, xdrproc);
        if (retlen == -1) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to encode message");
                goto ret;
        }

        outmsg->iov_len = retlen;
ret:
        if (retlen == -1) {
                iobuf_unref (iob);
                iob = NULL;
        }

        return iob;
}

int
glusterfs_submit_reply (rpcsvc_request_t *req, void *arg,
                        struct iovec *payload, int payloadcount,
                        struct iobref *iobref, xdrproc_t xdrproc)
{
        struct iobuf           *iob        = NULL;
        int                     ret        = -1;
        struct iovec            rsp        = {0,};
        char                    new_iobref = 0;

        if (!req) {
                GF_ASSERT (req);
                goto out;
        }


        if (!iobref) {
                iobref = iobref_new ();
                if (!iobref) {
                        gf_log (THIS->name, GF_LOG_ERROR, "out of memory");
                        goto out;
                }

                new_iobref = 1;
        }

        iob = glusterfs_serialize_reply (req, arg, &rsp, xdrproc);
        if (!iob) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to serialize reply");
                goto out;
        }

        iobref_add (iobref, iob);

        ret = rpcsvc_submit_generic (req, &rsp, 1, payload, payloadcount,
                                     iobref);

        /* Now that we've done our job of handing the message to the RPC layer
         * we can safely unref the iob in the hope that RPC layer must have
         * ref'ed the iob on receiving into the txlist.
         */
        iobuf_unref (iob);
        if (ret == -1) {
                gf_log (THIS->name, GF_LOG_ERROR, "Reply submission failed");
                goto out;
        }

        ret = 0;
out:

        if (new_iobref) {
                iobref_unref (iobref);
        }

        return ret;
}

int
glusterfs_terminate_response_send (rpcsvc_request_t *req, int op_ret)
{
        gd1_mgmt_brick_op_rsp   rsp = {0,};
        dict_t                  *dict = NULL;
        int                     ret = 0;

        rsp.op_ret = op_ret;
        rsp.op_errno = 0;
        rsp.op_errstr = "";
        dict = dict_new ();

        if (dict)
                ret = dict_allocate_and_serialize (dict, &rsp.output.output_val,
                                                (size_t *)&rsp.output.output_len);


        if (ret == 0)
                ret = glusterfs_submit_reply (req, &rsp, NULL, 0, NULL,
                                              (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);

        if (rsp.output.output_val)
                GF_FREE (rsp.output.output_val);
        if (dict)
                dict_unref (dict);
        return ret;
}

int
glusterfs_listener_stop (void)
{
        glusterfs_ctx_t  *ctx = NULL;
        cmd_args_t       *cmd_args = NULL;
        int              ret = 0;
        xlator_t         *this = NULL;

        ctx = glusterfs_ctx_get ();
        GF_ASSERT (ctx);
        cmd_args = &ctx->cmd_args;
        if (cmd_args->sock_file) {
                ret = unlink (cmd_args->sock_file);
                if (ret && (ENOENT == errno)) {
                        ret = 0;
                }
        }

        if (ret) {
                this = THIS;
                gf_log (this->name, GF_LOG_ERROR, "Failed to unlink linstener "
                        "socket %s, error: %s", cmd_args->sock_file,
                        strerror (errno));
        }
        return ret;
}

int
glusterfs_handle_terminate (rpcsvc_request_t *req)
{

        (void) glusterfs_listener_stop ();
        glusterfs_terminate_response_send (req, 0);
        cleanup_and_exit (SIGTERM);
        return 0;
}

int
glusterfs_translator_info_response_send (rpcsvc_request_t *req, int ret,
                                         char *msg, dict_t *output)
{
        gd1_mgmt_brick_op_rsp    rsp = {0,};
        GF_ASSERT (msg);
        GF_ASSERT (req);
        GF_ASSERT (output);

        rsp.op_ret = ret;
        rsp.op_errno = 0;
        if (ret && msg[0])
                rsp.op_errstr = msg;
        else
                rsp.op_errstr = "";

        ret = dict_allocate_and_serialize (output, &rsp.output.output_val,
                                        (size_t *)&rsp.output.output_len);

        ret = glusterfs_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);
        if (rsp.output.output_val)
                GF_FREE (rsp.output.output_val);
        return ret;
}

int
glusterfs_handle_translator_info_get_cont (gfd_vol_top_priv_t *priv)
{
        int                     ret = -1;
        xlator_t                *any = NULL;
        xlator_t                *xlator = NULL;
        glusterfs_graph_t       *active = NULL;
        glusterfs_ctx_t         *ctx = NULL;
        char                    msg[2048] = {0,};
        dict_t                  *output = NULL;
        dict_t                  *dict = NULL;

        GF_ASSERT (priv);

        dict = dict_new ();
        ret = dict_unserialize (priv->xlator_req.input.input_val,
                                priv->xlator_req.input.input_len, &dict);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "Unable to unserialize dict");
                goto cont;
        }
        ret = dict_set_double (dict, "time", priv->time);
        if (ret)
                goto cont;
        ret = dict_set_double (dict, "throughput", priv->throughput);
        if (ret)
                goto cont;

cont:
        ctx = glusterfs_ctx_get ();
        GF_ASSERT (ctx);
        active = ctx->active;
        any = active->first;

        xlator = xlator_search_by_name (any, priv->xlator_req.name);
        if (!xlator) {
                snprintf (msg, sizeof (msg), "xlator %s is not loaded",
                          priv->xlator_req.name);
                goto out;
        }

        output = dict_new ();
        ret = xlator->notify (xlator, GF_EVENT_TRANSLATOR_INFO, dict, output);

out:
        ret = glusterfs_translator_info_response_send (priv->req, ret,
                                                       msg, output);

        if (priv->xlator_req.name)
                free (priv->xlator_req.name);
        if (priv->xlator_req.input.input_val)
                free (priv->xlator_req.input.input_val);
        if (dict)
                dict_unref (dict);
        if (output)
                dict_unref (output);
        GF_FREE (priv);

        return ret;
}

int
glusterfs_handle_translator_info_get (rpcsvc_request_t *req)
{
        int32_t                  ret     = -1;
        gd1_mgmt_brick_op_req    xlator_req = {0,};
        dict_t                   *dict    = NULL;
        xlator_t                 *this = NULL;
        gf1_cli_top_op            top_op = 0;
        int32_t                  blk_size = 0;
        int32_t                  blk_count = 0;
        gfd_vol_top_priv_t       *priv = NULL;
        pthread_t                tid = -1;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

        if (!xdr_to_generic (req->msg[0], &xlator_req,
                             (xdrproc_t)xdr_gd1_mgmt_brick_op_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        dict = dict_new ();

        ret = dict_unserialize (xlator_req.input.input_val,
                                xlator_req.input.input_len,
                                &dict);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to "
                        "unserialize req-buffer to dictionary");
                goto out;
        }

        priv = GF_MALLOC (sizeof (gfd_vol_top_priv_t), gfd_mt_vol_top_priv_t);
        if (!priv) {
                gf_log ("glusterd", GF_LOG_ERROR, "failed to allocate memory");
                goto out;
        }
        priv->xlator_req = xlator_req;
        priv->req = req;

        ret = dict_get_int32 (dict, "top-op", (int32_t *)&top_op);
        if ((!ret) && (GF_CLI_TOP_READ_PERF == top_op ||
            GF_CLI_TOP_WRITE_PERF == top_op)) {
                ret = dict_get_int32 (dict, "blk-size", &blk_size);
                if (ret)
                        goto cont;
                ret = dict_get_int32 (dict, "blk-cnt", &blk_count);
                if (ret)
                        goto cont;
                priv->blk_size = blk_size;
                priv->blk_count = blk_count;
                if (GF_CLI_TOP_READ_PERF == top_op) {
                        ret = pthread_create (&tid, NULL,
                                              glusterfs_volume_top_read_perf,
                                              priv);
                } else if ( GF_CLI_TOP_WRITE_PERF == top_op) {
                        ret = pthread_create (&tid, NULL,
                                              glusterfs_volume_top_write_perf,
                                              priv);
                }
                if (ret) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "Thread create failed");
                        goto cont;
                }
                gf_log ("glusterd", GF_LOG_DEBUG, "Created new thread with "
                        "tid %u", (unsigned int)tid);
                goto out;
        }
cont:
        priv->throughput = 0;
        priv->time = 0;
        ret = glusterfs_handle_translator_info_get_cont (priv);
out:
        if (dict)
                dict_unref (dict);
        return ret;
}

void *
glusterfs_volume_top_write_perf (void *args)
{
        int32_t                 fd = -1;
        int32_t                 input_fd = -1;
        char                    export_path[PATH_MAX];
        char                    *buf = NULL;
        int32_t                 blk_size = 0;
        int32_t                 blk_count = 0;
        int32_t                 iter = 0;
        int32_t                 ret = -1;
        int64_t                 total_blks = 0;
        struct timeval          begin, end = {0,};
        double                  throughput = 0;
        double                  time = 0;
        gfd_vol_top_priv_t      *priv = NULL;

        GF_ASSERT (args);
        priv = (gfd_vol_top_priv_t *)args;

        blk_size = priv->blk_size;
        blk_count = priv->blk_count;

        snprintf (export_path, sizeof (export_path), "%s/%s",
                  priv->xlator_req.name, ".gf-tmp-stats-perf");

        fd = open (export_path, O_CREAT|O_RDWR, S_IRWXU);
        if (-1 == fd) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_ERROR, "Could not open tmp file");
                goto out;
        }

        buf = GF_MALLOC (blk_size * sizeof(*buf), gf_common_mt_char);
        if (!buf) {
                ret = -1;
                goto out;
        }

        input_fd = open ("/dev/urandom", O_RDONLY);
        if (-1 == input_fd) {
                ret = -1;
                gf_log ("glusterd",GF_LOG_ERROR, "Unable to open input file");
                goto out;
        }

        gettimeofday (&begin, NULL);
        for (iter = 0; iter < blk_count; iter++) {
                ret = read (input_fd, buf, blk_size);
                if (ret != blk_size) {
                        ret = -1;
                        goto out;
                }
                ret = write (fd, buf, blk_size);
                if (ret != blk_size) {
                        ret = -1;
                        goto out;
                }
                total_blks += ret;
        }
        ret = 0;
        if (total_blks != (blk_size * blk_count)) {
                gf_log ("glusterd", GF_LOG_WARNING, "Error in write");
                ret = -1;
                goto out;
        }

        gettimeofday (&end, NULL);
        time = (end.tv_sec - begin.tv_sec) * 1e6
                     + (end.tv_usec - begin.tv_usec);
        throughput = total_blks / time;
        gf_log ("glusterd", GF_LOG_INFO, "Throughput %.2f Mbps time %.2f secs "
                "bytes written %"PRId64, throughput, time, total_blks);

out:
        priv->throughput = throughput;
        priv->time = time;

        if (fd >= 0)
                close (fd);
        if (input_fd >= 0)
                close (input_fd);
        if (buf)
                GF_FREE (buf);
        unlink (export_path);

        (void)glusterfs_handle_translator_info_get_cont (priv);

        return NULL;
}

void *
glusterfs_volume_top_read_perf (void *args)
{
        int32_t                 fd = -1;
        int32_t                 input_fd = -1;
        int32_t                 output_fd = -1;
        char                    export_path[PATH_MAX];
        char                    *buf = NULL;
        int32_t                 blk_size = 0;
        int32_t                 blk_count = 0;
        int32_t                 iter = 0;
        int32_t                 ret = -1;
        int64_t                 total_blks = 0;
        struct timeval          begin, end = {0,};
        double                  throughput = 0;
        double                  time = 0;
        gfd_vol_top_priv_t      *priv = NULL;

        GF_ASSERT (args);
        priv = (gfd_vol_top_priv_t *)args;

        blk_size = priv->blk_size;
        blk_count = priv->blk_count;

        snprintf (export_path, sizeof (export_path), "%s/%s",
                  priv->xlator_req.name, ".gf-tmp-stats-perf");
        fd = open (export_path, O_CREAT|O_RDWR, S_IRWXU);
        if (-1 == fd) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_ERROR, "Could not open tmp file");
                goto out;
        }

        buf = GF_MALLOC (blk_size * sizeof(*buf), gf_common_mt_char);
        if (!buf) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_ERROR, "Could not allocate memory");
                goto out;
        }

        input_fd = open ("/dev/urandom", O_RDONLY);
        if (-1 == input_fd) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_ERROR, "Could not open input file");
                goto out;
        }

        output_fd = open ("/dev/null", O_RDWR);
        if (-1 == output_fd) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_ERROR, "Could not open output file");
                goto out;
        }

        for (iter = 0; iter < blk_count; iter++) {
                ret = read (input_fd, buf, blk_size);
                if (ret != blk_size) {
                        ret = -1;
                        goto out;
                }
                ret = write (fd, buf, blk_size);
                if (ret != blk_size) {
                        ret = -1;
                        goto out;
                }
        }

        ret = fsync (fd);
        if (ret) {
                gf_log ("glusterd", GF_LOG_ERROR, "could not flush cache");
                goto out;
        }
        ret = lseek (fd, 0L, 0);
        if (ret != 0) {
                gf_log ("glusterd", GF_LOG_ERROR,
                        "could not seek back to start");
                ret = -1;
                goto out;
        }
        gettimeofday (&begin, NULL);
        for (iter = 0; iter < blk_count; iter++) {
                ret = read (fd, buf, blk_size);
                if (ret != blk_size) {
                        ret = -1;
                        goto out;
                }
                ret = write (output_fd, buf, blk_size);
                if (ret != blk_size) {
                        ret = -1;
                        goto out;
                }
                total_blks += ret;
        }
        ret = 0;
        if ((blk_size * blk_count) != total_blks) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_WARNING, "Error in write");
                goto out;
        }

        gettimeofday (&end, NULL);
        time = (end.tv_sec - begin.tv_sec) * 1e6
               + (end.tv_usec - begin.tv_usec);
        throughput = total_blks / time;
        gf_log ("glusterd", GF_LOG_INFO, "Throughput %.2f Mbps time %.2f secs "
                "bytes read %"PRId64, throughput, time, total_blks);

out:
        priv->throughput = throughput;
        priv->time = time;

        if (fd >= 0)
                close (fd);
        if (input_fd >= 0)
                close (input_fd);
        if (output_fd >= 0)
                close (output_fd);
        if (buf)
                GF_FREE (buf);
        unlink (export_path);

        (void)glusterfs_handle_translator_info_get_cont (priv);

        return NULL;
}

int
glusterfs_handle_rpc_msg (rpcsvc_request_t *req)
{
        int     ret = -1;
        xlator_t *this = THIS;
        GF_ASSERT (this);
        switch (req->procnum) {
        case GF_BRICK_TERMINATE:
                ret = glusterfs_handle_terminate (req);
                break;
        case GF_BRICK_XLATOR_INFO:
                ret = glusterfs_handle_translator_info_get (req);
                break;
        default:
                break;
        }

        return ret;
}

rpcclnt_cb_actor_t gluster_cbk_actors[] = {
        [GF_CBK_FETCHSPEC] = {"FETCHSPEC", GF_CBK_FETCHSPEC, mgmt_cbk_spec },
};


struct rpcclnt_cb_program mgmt_cbk_prog = {
        .progname  = "GlusterFS Callback",
        .prognum   = GLUSTER_CBK_PROGRAM,
        .progver   = GLUSTER_CBK_VERSION,
        .actors    = gluster_cbk_actors,
        .numactors = GF_CBK_MAXVALUE,
};

char *clnt_pmap_procs[GF_PMAP_MAXVALUE] = {
        [GF_PMAP_NULL]        = "NULL",
        [GF_PMAP_PORTBYBRICK] = "PORTBYBRICK",
        [GF_PMAP_BRICKBYPORT] = "BRICKBYPORT",
        [GF_PMAP_SIGNIN]      = "SIGNIN",
        [GF_PMAP_SIGNOUT]     = "SIGNOUT",
        [GF_PMAP_SIGNUP]      = "SIGNUP",
};


rpc_clnt_prog_t clnt_pmap_prog = {
        .progname  = "Gluster Portmap",
        .prognum   = GLUSTER_PMAP_PROGRAM,
        .progver   = GLUSTER_PMAP_VERSION,
        .procnames = clnt_pmap_procs,
};

char *clnt_handshake_procs[GF_HNDSK_MAXVALUE] = {
        [GF_HNDSK_NULL]         = "NULL",
        [GF_HNDSK_SETVOLUME]    = "SETVOLUME",
        [GF_HNDSK_GETSPEC]      = "GETSPEC",
        [GF_HNDSK_PING]         = "PING",
};

rpc_clnt_prog_t clnt_handshake_prog = {
        .progname  = "GlusterFS Handshake",
        .prognum   = GLUSTER_HNDSK_PROGRAM,
        .progver   = GLUSTER_HNDSK_VERSION,
        .procnames = clnt_handshake_procs,
};

rpcsvc_actor_t glusterfs_actors[] = {
        [GF_BRICK_NULL]        = { "NULL",    GF_BRICK_NULL, glusterfs_handle_rpc_msg, NULL, NULL},
        [GF_BRICK_TERMINATE] = { "TERMINATE", GF_BRICK_TERMINATE, glusterfs_handle_rpc_msg, NULL, NULL},
        [GF_BRICK_XLATOR_INFO] = { "TRANSLATOR INFO", GF_BRICK_XLATOR_INFO, glusterfs_handle_rpc_msg, NULL, NULL}
};

struct rpcsvc_program glusterfs_mop_prog = {
        .progname  = "GlusterFS Mops",
        .prognum   = GLUSTERFS_PROGRAM,
        .progver   = GLUSTERFS_VERSION,
        .numactors = GLUSTERFS_PROCCNT,
        .actors    = glusterfs_actors,
};

int
mgmt_submit_request (void *req, call_frame_t *frame,
                     glusterfs_ctx_t *ctx,
                     rpc_clnt_prog_t *prog, int procnum,
                     fop_cbk_fn_t cbkfn, xdrproc_t xdrproc)
{
        int                     ret         = -1;
        int                     count      = 0;
        struct iovec            iov         = {0, };
        struct iobuf            *iobuf = NULL;
        struct iobref           *iobref = NULL;
        ssize_t                 xdr_size = 0;

        iobref = iobref_new ();
        if (!iobref) {
                goto out;
        }

        if (req) {
                xdr_size = xdr_sizeof (xdrproc, req);

                iobuf = iobuf_get2 (ctx->iobuf_pool, xdr_size);
                if (!iobuf) {
                        goto out;
                };

                iobref_add (iobref, iobuf);

                iov.iov_base = iobuf->ptr;
                iov.iov_len  = iobuf_pagesize (iobuf);

                /* Create the xdr payload */
                ret = xdr_serialize_generic (iov, req, xdrproc);
                if (ret == -1) {
                        gf_log (THIS->name, GF_LOG_WARNING, "failed to create XDR payload");
                        goto out;
                }
                iov.iov_len = ret;
                count = 1;
        }

        /* Send the msg */
        ret = rpc_clnt_submit (ctx->mgmt, prog, procnum, cbkfn,
                               &iov, count,
                               NULL, 0, iobref, frame, NULL, 0, NULL, 0, NULL);

out:
        if (iobref)
                iobref_unref (iobref);

        return ret;
}


/* XXX: move these into @ctx */
static char oldvolfile[131072];
static int oldvollen = 0;

static int
xlator_equal_rec (xlator_t *xl1, xlator_t *xl2)
{
        xlator_list_t *trav1 = NULL;
        xlator_list_t *trav2 = NULL;
        int            ret   = 0;

        if (xl1 == NULL || xl2 == NULL) {
                gf_log ("xlator", GF_LOG_DEBUG, "invalid argument");
                return -1;
        }

        trav1 = xl1->children;
        trav2 = xl2->children;

        while (trav1 && trav2) {
                ret = xlator_equal_rec (trav1->xlator, trav2->xlator);
                if (ret) {
                        gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG,
                                "xlators children not equal");
                        goto out;
                }

                trav1 = trav1->next;
                trav2 = trav2->next;
        }

        if (trav1 || trav2) {
                ret = -1;
                goto out;
        }

        if (strcmp (xl1->name, xl2->name)) {
                ret = -1;
                goto out;
        }
out :
        return ret;
}

static gf_boolean_t
is_graph_topology_equal (glusterfs_graph_t *graph1,
                                glusterfs_graph_t *graph2)
{
        xlator_t    *trav1    = NULL;
        xlator_t    *trav2    = NULL;
        gf_boolean_t ret      = _gf_true;

        trav1 = graph1->first;
        trav2 = graph2->first;

        ret = xlator_equal_rec (trav1, trav2);

        if (ret) {
                gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG,
                        "graphs are not equal");
                ret = _gf_false;
                goto out;
        }

        ret = _gf_true;
        gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG,
                "graphs are equal");

out:
        return ret;
}

/* Function has 3types of return value 0, -ve , 1
 *   return 0          =======> reconfiguration of options has succeded
 *   return 1          =======> the graph has to be reconstructed and all the xlators should be inited
 *   return -1(or -ve) =======> Some Internal Error occured during the operation
 */
static int
glusterfs_volfile_reconfigure (FILE *newvolfile_fp)
{
        glusterfs_graph_t *oldvolfile_graph = NULL;
        glusterfs_graph_t *newvolfile_graph = NULL;
        FILE              *oldvolfile_fp    = NULL;
        glusterfs_ctx_t   *ctx              = NULL;

        int ret = -1;

        oldvolfile_fp = tmpfile ();
        if (!oldvolfile_fp)
                goto out;

        if (!oldvollen) {
                ret = 1; // Has to call INIT for the whole graph
                goto out;
        }
        fwrite (oldvolfile, oldvollen, 1, oldvolfile_fp);
        fflush (oldvolfile_fp);


        oldvolfile_graph = glusterfs_graph_construct (oldvolfile_fp);
        if (!oldvolfile_graph) {
                goto out;
        }

        newvolfile_graph = glusterfs_graph_construct (newvolfile_fp);
        if (!newvolfile_graph) {
                goto out;
        }

        if (!is_graph_topology_equal (oldvolfile_graph,
                                      newvolfile_graph)) {

                ret = 1;
                gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG,
                        "Graph topology not equal(should call INIT)");
                goto out;
        }

        gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG,
                "Only options have changed in the new "
                "graph");

        ctx = glusterfs_ctx_get ();

        if (!ctx) {
                gf_log ("glusterfsd-mgmt", GF_LOG_ERROR,
                        "glusterfs_ctx_get() returned NULL");
                goto out;
        }

        oldvolfile_graph = ctx->active;

        if (!oldvolfile_graph) {
                gf_log ("glusterfsd-mgmt", GF_LOG_ERROR,
                        "glsuterfs_ctx->active is NULL");
                goto out;
        }

        /* */
        ret = glusterfs_graph_reconfigure (oldvolfile_graph,
                                           newvolfile_graph);
        if (ret) {
                gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG,
                        "Could not reconfigure new options in old graph");
                goto out;
        }

        ret = 0;
out:
        return ret;
}

int
mgmt_getspec_cbk (struct rpc_req *req, struct iovec *iov, int count,
                  void *myframe)
{
        gf_getspec_rsp           rsp   = {0,};
        call_frame_t            *frame = NULL;
        glusterfs_ctx_t         *ctx = NULL;
        int                      ret   = 0;
        ssize_t                  size = 0;
        FILE                    *tmpfp = NULL;

        frame = myframe;
        ctx = frame->this->ctx;

        if (-1 == req->rpc_status) {
                ret = -1;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_getspec_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR, "XDR decoding error");
                ret   = -1;
                goto out;
        }

        if (-1 == rsp.op_ret) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "failed to get the 'volume file' from server");
                ret = -1;
                goto out;
        }

        ret = 0;
        size = rsp.op_ret;

        if (size == oldvollen && (memcmp (oldvolfile, rsp.spec, size) == 0)) {
                gf_log (frame->this->name, GF_LOG_INFO,
                        "No change in volfile, continuing");
                goto out;
        }

        tmpfp = tmpfile ();
        if (!tmpfp) {
                ret = -1;
                goto out;
        }

        fwrite (rsp.spec, size, 1, tmpfp);
        fflush (tmpfp);

        /*  Check if only options have changed. No need to reload the
        *  volfile if topology hasn't changed.
        *  glusterfs_volfile_reconfigure returns 3 possible return states
        *  return 0          =======> reconfiguration of options has succeded
        *  return 1          =======> the graph has to be reconstructed and all the xlators should be inited
        *  return -1(or -ve) =======> Some Internal Error occured during the operation
        */

        ret = glusterfs_volfile_reconfigure (tmpfp);
        if (ret == 0) {
                gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG,
                        "No need to re-load volfile, reconfigure done");
                oldvollen = size;
                memcpy (oldvolfile, rsp.spec, size);
                goto out;
        }

        if (ret < 0) {
                gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG, "Reconfigure failed !!");
                goto out;
        }

        ret = glusterfs_process_volfp (ctx, tmpfp);
        if (ret)
                goto out;

        oldvollen = size;
        memcpy (oldvolfile, rsp.spec, size);
        if (!is_mgmt_rpc_reconnect) {
                glusterfs_mgmt_pmap_signin (ctx);
                is_mgmt_rpc_reconnect = 1;
        }

out:
        STACK_DESTROY (frame->root);

        if (rsp.spec)
                free (rsp.spec);

        if (ret && ctx && !ctx->active) {
                /* Do it only for the first time */
                /* Failed to get the volume file, something wrong,
                   restart the process */
                gf_log ("mgmt", GF_LOG_ERROR,
                        "failed to fetch volume file (key:%s)",
                        ctx->cmd_args.volfile_id);
                cleanup_and_exit (0);
        }
        return 0;
}


int
glusterfs_volfile_fetch (glusterfs_ctx_t *ctx)
{
        cmd_args_t       *cmd_args = NULL;
        gf_getspec_req    req = {0, };
        int               ret = 0;
        call_frame_t     *frame = NULL;

        cmd_args = &ctx->cmd_args;

        frame = create_frame (THIS, ctx->pool);

        req.key = cmd_args->volfile_id;
        req.flags = 0;

        ret = mgmt_submit_request (&req, frame, ctx, &clnt_handshake_prog,
                                   GF_HNDSK_GETSPEC, mgmt_getspec_cbk,
                                   (xdrproc_t)xdr_gf_getspec_req);
        return ret;
}


static int
mgmt_rpc_notify (struct rpc_clnt *rpc, void *mydata, rpc_clnt_event_t event,
                 void *data)
{
        xlator_t        *this = NULL;
        cmd_args_t      *cmd_args = NULL;
        glusterfs_ctx_t *ctx = NULL;
        int              ret = 0;

        this = mydata;
        ctx = this->ctx;
        cmd_args = &ctx->cmd_args;
        switch (event) {
        case RPC_CLNT_DISCONNECT:
                if (!ctx->active) {
                        cmd_args->max_connect_attempts--;
                        gf_log ("glusterfsd-mgmt", GF_LOG_ERROR,
                                "failed to connect with remote-host: %s",
                                strerror (errno));
                        gf_log ("glusterfsd-mgmt", GF_LOG_INFO,
                                "%d connect attempts left",
                                cmd_args->max_connect_attempts);
                        if (0 >= cmd_args->max_connect_attempts)
                                cleanup_and_exit (1);
                }
                break;
        case RPC_CLNT_CONNECT:
                rpc_clnt_set_connected (&((struct rpc_clnt*)ctx->mgmt)->conn);

                ret = glusterfs_volfile_fetch (ctx);
                if (ret && ctx && (ctx->active == NULL)) {
                        /* Do it only for the first time */
                        /* Exit the process.. there is some wrong options */
                        gf_log ("mgmt", GF_LOG_ERROR,
                                "failed to fetch volume file (key:%s)",
                                ctx->cmd_args.volfile_id);
                        cleanup_and_exit (0);
                }

                if (is_mgmt_rpc_reconnect)
                        glusterfs_mgmt_pmap_signin (ctx);
                break;
        default:
                break;
        }

        return 0;
}

int
glusterfs_rpcsvc_notify (rpcsvc_t *rpc, void *xl, rpcsvc_event_t event,
                     void *data)
{
        if (!xl || !data) {
                goto out;
        }

        switch (event) {
        case RPCSVC_EVENT_ACCEPT:
        {
                break;
        }
        case RPCSVC_EVENT_DISCONNECT:
        {
                break;
        }

        default:
                break;
        }

out:
        return 0;
}

int
glusterfs_listener_init (glusterfs_ctx_t *ctx)
{
        cmd_args_t              *cmd_args = NULL;
        rpcsvc_t                *rpc = NULL;
        dict_t                  *options = NULL;
        int                     ret = -1;

        cmd_args = &ctx->cmd_args;

        if (ctx->listener)
                return 0;

        if (!cmd_args->sock_file)
                return 0;

        ret = rpcsvc_transport_unix_options_build (&options,
                                                   cmd_args->sock_file);
        if (ret)
                goto out;

        rpc = rpcsvc_init (THIS, ctx, options);
        if (rpc == NULL) {
                goto out;
        }

        ret = rpcsvc_register_notify (rpc, glusterfs_rpcsvc_notify, THIS);
        if (ret) {
                goto out;
        }

        ret = rpcsvc_create_listeners (rpc, options, "glusterfsd");
        if (ret < 1) {
                ret = -1;
                goto out;
        }

        ret = rpcsvc_program_register (rpc, &glusterfs_mop_prog);
        if (ret) {
                goto out;
        }

        ctx->listener = rpc;

out:
        return ret;
}

int
glusterfs_mgmt_init (glusterfs_ctx_t *ctx)
{
        cmd_args_t              *cmd_args = NULL;
        struct rpc_clnt         *rpc = NULL;
        dict_t                  *options = NULL;
        int                     ret = -1;
        int                     port = GF_DEFAULT_BASE_PORT;
        char                    *host = NULL;

        cmd_args = &ctx->cmd_args;

        if (ctx->mgmt)
                return 0;

        if (cmd_args->volfile_server_port)
                port = cmd_args->volfile_server_port;

        host = "localhost";
        if (cmd_args->volfile_server)
                host = cmd_args->volfile_server;

        ret = rpc_transport_inet_options_build (&options, host, port);
        if (ret)
                goto out;

        rpc = rpc_clnt_new (options, THIS->ctx, THIS->name);
        if (!rpc) {
                ret = -1;
                gf_log (THIS->name, GF_LOG_WARNING, "failed to create rpc clnt");
                goto out;
        }

        ret = rpc_clnt_register_notify (rpc, mgmt_rpc_notify, THIS);
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING, "failed to register notify function");
                goto out;
        }

        ret = rpcclnt_cbk_program_register (rpc, &mgmt_cbk_prog);
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING, "failed to register callback function");
                goto out;
        }

        /* This value should be set before doing the 'rpc_clnt_start()' as
           the notify function uses this variable */
        ctx->mgmt = rpc;

        ret = rpc_clnt_start (rpc);
out:
        return ret;
}

static int
mgmt_pmap_signin2_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        pmap_signin_rsp  rsp   = {0,};
        call_frame_t    *frame = NULL;
        int              ret   = 0;

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_pmap_signin_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR, "XDR decode error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 == rsp.op_ret) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "failed to register the port with glusterd");
                goto out;
        }
out:
        STACK_DESTROY (frame->root);
        return 0;

}

static int
mgmt_pmap_signin_cbk (struct rpc_req *req, struct iovec *iov, int count,
                      void *myframe)
{
        pmap_signin_rsp  rsp   = {0,};
        call_frame_t    *frame = NULL;
        int              ret   = 0;
        pmap_signin_req  pmap_req = {0, };
        cmd_args_t      *cmd_args = NULL;
        glusterfs_ctx_t *ctx      = NULL;
        char             brick_name[PATH_MAX] = {0,};

        frame = myframe;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_pmap_signin_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR, "XDR decode error");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 == rsp.op_ret) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "failed to register the port with glusterd");
                goto out;
        }

        ctx = glusterfs_ctx_get ();
        cmd_args = &ctx->cmd_args;

        if (!cmd_args->brick_port2) {
                /* We are done with signin process */
                goto out;
        }

        snprintf (brick_name, PATH_MAX, "%s.rdma", cmd_args->brick_name);
        pmap_req.port  = cmd_args->brick_port2;
        pmap_req.brick = brick_name;

        ret = mgmt_submit_request (&pmap_req, frame, ctx, &clnt_pmap_prog,
                                   GF_PMAP_SIGNIN, mgmt_pmap_signin2_cbk,
                                   (xdrproc_t)xdr_pmap_signin_req);
        if (ret)
                goto out;

        return 0;

out:

        STACK_DESTROY (frame->root);
        return 0;
}

int
glusterfs_mgmt_pmap_signin (glusterfs_ctx_t *ctx)
{
        call_frame_t     *frame = NULL;
        pmap_signin_req   req = {0, };
        int               ret = -1;
        cmd_args_t       *cmd_args = NULL;

        frame = create_frame (THIS, ctx->pool);
        cmd_args = &ctx->cmd_args;

        if (!cmd_args->brick_port || !cmd_args->brick_name) {
                gf_log ("fsd-mgmt", GF_LOG_DEBUG,
                        "portmapper signin arguments not given");
                goto out;
        }

        req.port  = cmd_args->brick_port;
        req.brick = cmd_args->brick_name;

        ret = mgmt_submit_request (&req, frame, ctx, &clnt_pmap_prog,
                                   GF_PMAP_SIGNIN, mgmt_pmap_signin_cbk,
                                   (xdrproc_t)xdr_pmap_signin_req);

out:
        return ret;
}


static int
mgmt_pmap_signout_cbk (struct rpc_req *req, struct iovec *iov, int count,
                       void *myframe)
{
        pmap_signout_rsp  rsp   = {0,};
        int              ret   = 0;
        glusterfs_ctx_t  *ctx = NULL;

        if (-1 == req->rpc_status) {
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        ctx = glusterfs_ctx_get ();
        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_pmap_signout_rsp);
        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_ERROR, "XDR decoding failed");
                rsp.op_ret   = -1;
                rsp.op_errno = EINVAL;
                goto out;
        }

        if (-1 == rsp.op_ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "failed to register the port with glusterd");
                goto out;
        }
out:
        return 0;
}


int
glusterfs_mgmt_pmap_signout (glusterfs_ctx_t *ctx)
{
        int               ret = 0;
        pmap_signout_req  req = {0, };
        call_frame_t     *frame = NULL;
        cmd_args_t       *cmd_args = NULL;

        frame = create_frame (THIS, ctx->pool);
        cmd_args = &ctx->cmd_args;

        if (!cmd_args->brick_port || !cmd_args->brick_name) {
                gf_log ("fsd-mgmt", GF_LOG_DEBUG,
                        "portmapper signout arguments not given");
                goto out;
        }

        req.port  = cmd_args->brick_port;
        req.brick = cmd_args->brick_name;

        ret = mgmt_submit_request (&req, frame, ctx, &clnt_pmap_prog,
                                   GF_PMAP_SIGNOUT, mgmt_pmap_signout_cbk,
                                   (xdrproc_t)xdr_pmap_signout_req);
out:
        return ret;
}
