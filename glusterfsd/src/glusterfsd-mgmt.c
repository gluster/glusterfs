/*
   Copyright (c) 2007-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <signal.h>

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
#include "rpcsvc.h"
#include "cli1-xdr.h"
#include "statedump.h"
#include "syncop.h"
#include "xlator.h"

static gf_boolean_t is_mgmt_rpc_reconnect = _gf_false;

int glusterfs_mgmt_pmap_signin (glusterfs_ctx_t *ctx);
int glusterfs_volfile_fetch (glusterfs_ctx_t *ctx);
int glusterfs_process_volfp (glusterfs_ctx_t *ctx, FILE *fp);
int glusterfs_graph_unknown_options (glusterfs_graph_t *graph);
int emancipate(glusterfs_ctx_t *ctx, int ret);

int
mgmt_cbk_spec (struct rpc_clnt *rpc, void *mydata, void *data)
{
        glusterfs_ctx_t *ctx = NULL;
        xlator_t *this = NULL;

        this = mydata;
        ctx = glusterfsd_ctx;
        gf_log ("mgmt", GF_LOG_INFO, "Volume file changed");

        glusterfs_volfile_fetch (ctx);
        return 0;
}


int
mgmt_cbk_event (struct rpc_clnt *rpc, void *mydata, void *data)
{
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
                gf_log_callingfn (THIS->name, GF_LOG_ERROR, "Failed to serialize reply");
        } else {
                iobref_add (iobref, iob);
        }

        ret = rpcsvc_submit_generic (req, &rsp, 1, payload, payloadcount,
                                     iobref);

        /* Now that we've done our job of handing the message to the RPC layer
         * we can safely unref the iob in the hope that RPC layer must have
         * ref'ed the iob on receiving into the txlist.
         */
        if (ret == -1) {
                gf_log (THIS->name, GF_LOG_ERROR, "Reply submission failed");
                goto out;
        }

        ret = 0;
out:
        if (iob)
                iobuf_unref (iob);

        if (new_iobref && iobref)
                iobref_unref (iobref);

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
                                                   &rsp.output.output_len);


        if (ret == 0)
                ret = glusterfs_submit_reply (req, &rsp, NULL, 0, NULL,
                                              (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);

        GF_FREE (rsp.output.output_val);
        if (dict)
                dict_unref (dict);
        return ret;
}

int
glusterfs_handle_terminate (rpcsvc_request_t *req)
{

        glusterfs_terminate_response_send (req, 0);
        cleanup_and_exit (SIGTERM);
        return 0;
}

int
glusterfs_translator_info_response_send (rpcsvc_request_t *req, int ret,
                                         char *msg, dict_t *output)
{
        gd1_mgmt_brick_op_rsp    rsp = {0,};
        gf_boolean_t             free_ptr = _gf_false;
        GF_ASSERT (req);

        rsp.op_ret = ret;
        rsp.op_errno = 0;
        if (ret && msg && msg[0])
                rsp.op_errstr = msg;
        else
                rsp.op_errstr = "";

        ret = -1;
        if (output) {
                ret = dict_allocate_and_serialize (output,
                                                   &rsp.output.output_val,
                                                   &rsp.output.output_len);
        }
        if (!ret)
                free_ptr = _gf_true;

        glusterfs_submit_reply (req, &rsp, NULL, 0, NULL,
                                (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);
        ret = 0;
        if (free_ptr)
                GF_FREE (rsp.output.output_val);
        return ret;
}

int
glusterfs_xlator_op_response_send (rpcsvc_request_t *req, int op_ret,
                                   char *msg, dict_t *output)
{
        gd1_mgmt_brick_op_rsp    rsp = {0,};
        int                      ret = -1;
        gf_boolean_t             free_ptr = _gf_false;
        GF_ASSERT (req);

        rsp.op_ret = op_ret;
        rsp.op_errno = 0;
        if (op_ret && msg && msg[0])
                rsp.op_errstr = msg;
        else
                rsp.op_errstr = "";

        if (output) {
                ret = dict_allocate_and_serialize (output,
                                                   &rsp.output.output_val,
                                                   &rsp.output.output_len);
        }
        if (!ret)
                free_ptr = _gf_true;

        ret = glusterfs_submit_reply (req, &rsp, NULL, 0, NULL,
                                     (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);

        if (free_ptr)
                GF_FREE (rsp.output.output_val);

        return ret;
}

int
glusterfs_handle_translator_info_get (rpcsvc_request_t *req)
{
        int32_t                 ret     = -1;
        gd1_mgmt_brick_op_req   xlator_req = {0,};
        dict_t                  *dict    = NULL;
        xlator_t                *this = NULL;
        gf1_cli_top_op          top_op = 0;
        uint32_t                blk_size = 0;
        uint32_t                blk_count = 0;
        double                  time = 0;
        double                  throughput = 0;
        xlator_t                *any = NULL;
        xlator_t                *xlator = NULL;
        glusterfs_graph_t       *active = NULL;
        glusterfs_ctx_t         *ctx = NULL;
        char                    msg[2048] = {0,};
        dict_t                  *output = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

        ret = xdr_to_generic (req->msg[0], &xlator_req,
                              (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
        if (ret < 0) {
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

        ret = dict_get_int32 (dict, "top-op", (int32_t *)&top_op);
        if ((!ret) && (GF_CLI_TOP_READ_PERF == top_op ||
            GF_CLI_TOP_WRITE_PERF == top_op)) {
                ret = dict_get_uint32 (dict, "blk-size", &blk_size);
                if (ret)
                        goto cont;
                ret = dict_get_uint32 (dict, "blk-cnt", &blk_count);
                if (ret)
                        goto cont;

                if (GF_CLI_TOP_READ_PERF == top_op) {
                        ret = glusterfs_volume_top_read_perf
                                (blk_size, blk_count, xlator_req.name,
                                 &throughput, &time);
                } else if ( GF_CLI_TOP_WRITE_PERF == top_op) {
                        ret = glusterfs_volume_top_write_perf
                                (blk_size, blk_count, xlator_req.name,
                                 &throughput, &time);
                }
                ret = dict_set_double (dict, "time", time);
                if (ret)
                        goto cont;
                ret = dict_set_double (dict, "throughput", throughput);
                if (ret)
                        goto cont;
        }
cont:
        ctx = glusterfsd_ctx;
        GF_ASSERT (ctx);
        active = ctx->active;
        any = active->first;

        xlator = xlator_search_by_name (any, xlator_req.name);
        if (!xlator) {
                snprintf (msg, sizeof (msg), "xlator %s is not loaded",
                          xlator_req.name);
                goto out;
        }

        output = dict_new ();
        ret = xlator->notify (xlator, GF_EVENT_TRANSLATOR_INFO, dict, output);

out:
        ret = glusterfs_translator_info_response_send (req, ret, msg, output);

        free (xlator_req.name);
        free (xlator_req.input.input_val);
        if (output)
                dict_unref (output);
        if (dict)
                dict_unref (dict);
        return ret;
}

int
glusterfs_volume_top_write_perf (uint32_t blk_size, uint32_t blk_count,
                                 char *brick_path, double *throughput,
                                 double *time)
{
        int32_t                 fd = -1;
        int32_t                 input_fd = -1;
        char                    export_path[PATH_MAX] = {0,};
        char                    *buf = NULL;
        int32_t                 iter = 0;
        int32_t                 ret = -1;
        uint64_t                total_blks = 0;
        struct timeval          begin, end = {0,};

        GF_ASSERT (brick_path);
        GF_ASSERT (throughput);
        GF_ASSERT (time);
        if (!(blk_size > 0) || ! (blk_count > 0))
                goto out;

        snprintf (export_path, sizeof (export_path), "%s/%s",
                  brick_path, ".gf-tmp-stats-perf");

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

        input_fd = open ("/dev/zero", O_RDONLY);
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
        if (total_blks != ((uint64_t)blk_size * blk_count)) {
                gf_log ("glusterd", GF_LOG_WARNING, "Error in write");
                ret = -1;
                goto out;
        }

        gettimeofday (&end, NULL);
        *time = (end.tv_sec - begin.tv_sec) * 1e6
                     + (end.tv_usec - begin.tv_usec);
        *throughput = total_blks / *time;
        gf_log ("glusterd", GF_LOG_INFO, "Throughput %.2f Mbps time %.2f secs "
                "bytes written %"PRId64, *throughput, *time, total_blks);

out:
        if (fd >= 0)
                close (fd);
        if (input_fd >= 0)
                close (input_fd);
        GF_FREE (buf);
        unlink (export_path);

        return ret;
}

int
glusterfs_volume_top_read_perf (uint32_t blk_size, uint32_t blk_count,
                                char *brick_path, double *throughput,
                                double *time)
{
        int32_t                 fd = -1;
        int32_t                 input_fd = -1;
        int32_t                 output_fd = -1;
        char                    export_path[PATH_MAX] = {0,};
        char                    *buf = NULL;
        int32_t                 iter = 0;
        int32_t                 ret = -1;
        uint64_t                total_blks = 0;
        struct timeval          begin, end = {0,};

        GF_ASSERT (brick_path);
        GF_ASSERT (throughput);
        GF_ASSERT (time);
        if (!(blk_size > 0) || ! (blk_count > 0))
                goto out;

        snprintf (export_path, sizeof (export_path), "%s/%s",
                  brick_path, ".gf-tmp-stats-perf");
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

        input_fd = open ("/dev/zero", O_RDONLY);
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
        if (total_blks != ((uint64_t)blk_size * blk_count)) {
                ret = -1;
                gf_log ("glusterd", GF_LOG_WARNING, "Error in read");
                goto out;
        }

        gettimeofday (&end, NULL);
        *time = (end.tv_sec - begin.tv_sec) * 1e6
                + (end.tv_usec - begin.tv_usec);
        *throughput = total_blks / *time;
        gf_log ("glusterd", GF_LOG_INFO, "Throughput %.2f Mbps time %.2f secs "
                "bytes read %"PRId64, *throughput, *time, total_blks);

out:
        if (fd >= 0)
                close (fd);
        if (input_fd >= 0)
                close (input_fd);
        if (output_fd >= 0)
                close (output_fd);
        GF_FREE (buf);
        unlink (export_path);

        return ret;
}

int
glusterfs_handle_translator_op (rpcsvc_request_t *req)
{
        int32_t                  ret     = -1;
        gd1_mgmt_brick_op_req    xlator_req = {0,};
        dict_t                   *input    = NULL;
        xlator_t                 *xlator = NULL;
        xlator_t                 *any = NULL;
        dict_t                   *output = NULL;
        char                     key[2048] = {0};
        char                    *xname = NULL;
        glusterfs_ctx_t          *ctx = NULL;
        glusterfs_graph_t        *active = NULL;
        xlator_t                 *this = NULL;
        int                      i = 0;
        int                      count = 0;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

        ret = xdr_to_generic (req->msg[0], &xlator_req,
                              (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        ctx = glusterfsd_ctx;
        active = ctx->active;
        any = active->first;
        input = dict_new ();
        ret = dict_unserialize (xlator_req.input.input_val,
                                xlator_req.input.input_len,
                                &input);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to "
                        "unserialize req-buffer to dictionary");
                goto out;
        } else {
                input->extra_stdfree = xlator_req.input.input_val;
        }

        ret = dict_get_int32 (input, "count", &count);

        output = dict_new ();
        if (!output) {
                ret = -1;
                goto out;
        }

        for (i = 0; i < count; i++)  {
                snprintf (key, sizeof (key), "xl-%d", i);
                ret = dict_get_str (input, key, &xname);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Couldn't get "
                                "xlator %s ", key);
                        goto out;
                }
                xlator = xlator_search_by_name (any, xname);
                if (!xlator) {
                        gf_log (this->name, GF_LOG_ERROR, "xlator %s is not "
                                "loaded", xname);
                        goto out;
                }
        }
        for (i = 0; i < count; i++)  {
                snprintf (key, sizeof (key), "xl-%d", i);
                ret = dict_get_str (input, key, &xname);
                xlator = xlator_search_by_name (any, xname);
                XLATOR_NOTIFY (xlator, GF_EVENT_TRANSLATOR_OP, input, output);
                if (ret)
                        break;
        }
out:
        glusterfs_xlator_op_response_send (req, ret, "", output);
        if (input)
                dict_unref (input);
        if (output)
                dict_unref (output);
        free (xlator_req.name); //malloced by xdr

        return 0;
}


int
glusterfs_handle_defrag (rpcsvc_request_t *req)
{
        int32_t                  ret     = -1;
        gd1_mgmt_brick_op_req    xlator_req = {0,};
        dict_t                   *dict    = NULL;
        xlator_t                 *xlator = NULL;
        xlator_t                 *any = NULL;
        dict_t                   *output = NULL;
        char                     msg[2048] = {0};
        glusterfs_ctx_t          *ctx = NULL;
        glusterfs_graph_t        *active = NULL;
        xlator_t                 *this = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

        ctx = glusterfsd_ctx;
        GF_ASSERT (ctx);

        active = ctx->active;
        if (!active) {
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        any = active->first;
        ret = xdr_to_generic (req->msg[0], &xlator_req,
                              (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }
        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_unserialize (xlator_req.input.input_val,
                                xlator_req.input.input_len,
                                &dict);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to "
                        "unserialize req-buffer to dictionary");
                goto out;
        }
        xlator = xlator_search_by_name (any, xlator_req.name);
        if (!xlator) {
                snprintf (msg, sizeof (msg), "xlator %s is not loaded",
                          xlator_req.name);
                goto out;
        }

        output = dict_new ();
        if (!output) {
                ret = -1;
                goto out;
        }

        ret = xlator->notify (xlator, GF_EVENT_VOLUME_DEFRAG, dict, output);

        ret = glusterfs_translator_info_response_send (req, ret,
                                                       msg, output);
out:
        if (dict)
                dict_unref (dict);
        free (xlator_req.input.input_val); // malloced by xdr
        if (output)
                dict_unref (output);
        free (xlator_req.name); //malloced by xdr

        return ret;

}
int
glusterfs_handle_brick_status (rpcsvc_request_t *req)
{
        int                     ret = -1;
        gd1_mgmt_brick_op_req   brick_req = {0,};
        gd1_mgmt_brick_op_rsp   rsp = {0,};
        glusterfs_ctx_t         *ctx = NULL;
        glusterfs_graph_t       *active = NULL;
        xlator_t                *this = NULL;
        xlator_t                *any = NULL;
        xlator_t                *xlator = NULL;
        dict_t                  *dict = NULL;
        dict_t                  *output = NULL;
        char                    *volname = NULL;
        char                    *xname = NULL;
        uint32_t                cmd = 0;
        char                    *msg = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

        ret = xdr_to_generic (req->msg[0], &brick_req,
                              (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
        if (ret < 0) {
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        dict = dict_new ();
        ret = dict_unserialize (brick_req.input.input_val,
                                brick_req.input.input_len, &dict);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to unserialize "
                        "req-buffer to dictionary");
                goto out;
        }

        ret = dict_get_uint32 (dict, "cmd", &cmd);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Couldn't get status op");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Couldn't get volname");
                goto out;
        }

        ctx = glusterfsd_ctx;
        GF_ASSERT (ctx);
        active = ctx->active;
        any = active->first;

        ret = gf_asprintf (&xname, "%s-server", volname);
        if (-1 == ret) {
                gf_log (this->name, GF_LOG_ERROR, "Out of memory");
                goto out;
        }

        xlator = xlator_search_by_name (any, xname);
        if (!xlator) {
                gf_log (this->name, GF_LOG_ERROR, "xlator %s is not loaded",
                        xname);
                ret = -1;
                goto out;
        }


        output = dict_new ();
        switch (cmd & GF_CLI_STATUS_MASK) {
                case GF_CLI_STATUS_MEM:
                        ret = 0;
                        gf_proc_dump_mem_info_to_dict (output);
                        gf_proc_dump_mempool_info_to_dict (ctx, output);
                        break;

                case GF_CLI_STATUS_CLIENTS:
                        ret = xlator->dumpops->priv_to_dict (xlator, output);
                        break;

                case GF_CLI_STATUS_INODE:
                        ret = xlator->dumpops->inode_to_dict (xlator, output);
                        break;

                case GF_CLI_STATUS_FD:
                        ret = xlator->dumpops->fd_to_dict (xlator, output);
                        break;

                case GF_CLI_STATUS_CALLPOOL:
                        ret = 0;
                        gf_proc_dump_pending_frames_to_dict (ctx->pool, output);
                        break;

                default:
                        ret = -1;
                        msg = gf_strdup ("Unknown status op");
                        break;
        }
        rsp.op_ret = ret;
        rsp.op_errno = 0;
        if (ret && msg)
                rsp.op_errstr = msg;
        else
                rsp.op_errstr = "";

        ret = dict_allocate_and_serialize (output, &rsp.output.output_val,
                                           &rsp.output.output_len);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to serialize output dict to rsp");
                goto out;
        }

        glusterfs_submit_reply (req, &rsp, NULL, 0, NULL,
                                (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);
        ret = 0;

out:
        if (dict)
                dict_unref (dict);
        if (output)
                dict_unref (output);
        free (brick_req.input.input_val);
        GF_FREE (xname);
        GF_FREE (msg);
        GF_FREE (rsp.output.output_val);

        return ret;
}


int
glusterfs_handle_node_status (rpcsvc_request_t *req)
{
        int                     ret = -1;
        gd1_mgmt_brick_op_req   node_req = {0,};
        gd1_mgmt_brick_op_rsp   rsp = {0,};
        glusterfs_ctx_t         *ctx = NULL;
        glusterfs_graph_t       *active = NULL;
        xlator_t                *any = NULL;
        xlator_t                *node = NULL;
        xlator_t                *subvol = NULL;
        dict_t                  *dict = NULL;
        dict_t                  *output = NULL;
        char                    *volname = NULL;
        char                    *node_name = NULL;
        char                    *subvol_name = NULL;
        uint32_t                cmd = 0;
        char                    *msg = NULL;

        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &node_req,
                              (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
        if (ret < 0) {
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        dict = dict_new ();
        ret = dict_unserialize (node_req.input.input_val,
                                node_req.input.input_len, &dict);
        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to unserialize "
                        "req buffer to dictionary");
                goto out;
        }

        ret = dict_get_uint32 (dict, "cmd", &cmd);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Couldn't get status op");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Couldn't get volname");
                goto out;
        }

        ctx = glusterfsd_ctx;
        GF_ASSERT (ctx);
        active = ctx->active;
        any = active->first;

        if ((cmd & GF_CLI_STATUS_NFS) != 0)
                ret = gf_asprintf (&node_name, "%s", "nfs-server");
        else if ((cmd & GF_CLI_STATUS_SHD) != 0)
                ret = gf_asprintf (&node_name, "%s", "glustershd");
        else if ((cmd & GF_CLI_STATUS_QUOTAD) != 0)
                ret = gf_asprintf (&node_name, "%s", "quotad");

        else {
                ret = -1;
                goto out;
        }
        if (ret == -1) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to set node xlator name");
                goto out;
        }

        node = xlator_search_by_name (any, node_name);
        if (!node) {
                ret = -1;
                gf_log (THIS->name, GF_LOG_ERROR, "%s xlator is not loaded",
                        node_name);
                goto out;
        }

        if ((cmd & GF_CLI_STATUS_NFS) != 0)
                ret = gf_asprintf (&subvol_name, "%s", volname);
        else if ((cmd & GF_CLI_STATUS_SHD) != 0)
                ret = gf_asprintf (&subvol_name, "%s-replicate-0", volname);
        else if ((cmd & GF_CLI_STATUS_QUOTAD) != 0)
                ret = gf_asprintf (&subvol_name, "%s", volname);
        else {
                ret = -1;
                goto out;
        }
        if (ret == -1) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to set node xlator name");
                goto out;
        }

        subvol = xlator_search_by_name (node, subvol_name);
        if (!subvol) {
                ret = -1;
                gf_log (THIS->name, GF_LOG_ERROR, "%s xlator is not loaded",
                        subvol_name);
                goto out;
        }

        output = dict_new ();
        switch (cmd & GF_CLI_STATUS_MASK) {
                case GF_CLI_STATUS_MEM:
                        ret = 0;
                        gf_proc_dump_mem_info_to_dict (output);
                        gf_proc_dump_mempool_info_to_dict (ctx, output);
                        break;

                case GF_CLI_STATUS_CLIENTS:
                        // clients not availbale for SHD
                        if ((cmd & GF_CLI_STATUS_SHD) != 0)
                                break;

                        ret = dict_set_str (output, "volname", volname);
                        if (ret) {
                                gf_log (THIS->name, GF_LOG_ERROR,
                                        "Error setting volname to dict");
                                goto out;
                        }
                        ret = node->dumpops->priv_to_dict (node, output);
                        break;

                case GF_CLI_STATUS_INODE:
                        ret = 0;
                        inode_table_dump_to_dict (subvol->itable, "conn0",
                                                  output);
                        ret = dict_set_int32 (output, "conncount", 1);
                        break;

                case GF_CLI_STATUS_FD:
                        // cannot find fd-tables in nfs-server graph
                        // TODO: finish once found
                        break;

                case GF_CLI_STATUS_CALLPOOL:
                        ret = 0;
                        gf_proc_dump_pending_frames_to_dict (ctx->pool, output);
                        break;

                default:
                        ret = -1;
                        msg = gf_strdup ("Unknown status op");
                        gf_log (THIS->name, GF_LOG_ERROR, "%s", msg);
                        break;
        }
        rsp.op_ret = ret;
        rsp.op_errno = 0;
        if (ret && msg)
                rsp.op_errstr = msg;
        else
                rsp.op_errstr = "";

        ret = dict_allocate_and_serialize (output, &rsp.output.output_val,
                                           &rsp.output.output_len);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to serialize output dict to rsp");
                goto out;
        }

        glusterfs_submit_reply (req, &rsp, NULL, 0, NULL,
                                (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);
        ret = 0;

out:
        if (dict)
                dict_unref (dict);
        free (node_req.input.input_val);
        GF_FREE (msg);
        GF_FREE (rsp.output.output_val);
        GF_FREE (node_name);
        GF_FREE (subvol_name);

        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterfs_handle_nfs_profile (rpcsvc_request_t *req)
{
        int                     ret = -1;
        gd1_mgmt_brick_op_req   nfs_req = {0,};
        gd1_mgmt_brick_op_rsp   rsp = {0,};
        dict_t                  *dict = NULL;
        glusterfs_ctx_t         *ctx = NULL;
        glusterfs_graph_t       *active = NULL;
        xlator_t                *any = NULL;
        xlator_t                *nfs = NULL;
        xlator_t                *subvol = NULL;
        char                    *volname = NULL;
        dict_t                  *output = NULL;

        GF_ASSERT (req);

        ret = xdr_to_generic (req->msg[0], &nfs_req,
                              (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
        if (ret < 0) {
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        dict = dict_new ();
        ret = dict_unserialize (nfs_req.input.input_val,
                                nfs_req.input.input_len, &dict);
        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to "
                        "unserialize req-buffer to dict");
                goto out;
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Couldn't get volname");
                goto out;
        }

        ctx = glusterfsd_ctx;
        GF_ASSERT (ctx);

        active = ctx->active;
        any = active->first;

        // is this needed?
        // are problems possible by searching for subvol directly from "any"?
        nfs = xlator_search_by_name (any, "nfs-server");
        if (!nfs) {
                ret = -1;
                gf_log (THIS->name, GF_LOG_ERROR, "xlator nfs-server is "
                        "not loaded");
                goto out;
        }

        subvol = xlator_search_by_name (nfs, volname);
        if (!subvol) {
                ret = -1;
                gf_log (THIS->name, GF_LOG_ERROR, "xlator %s is no loaded",
                        volname);
                goto out;
        }

        output = dict_new ();
        ret = subvol->notify (subvol, GF_EVENT_TRANSLATOR_INFO, dict, output);

        rsp.op_ret = ret;
        rsp.op_errno = 0;
        rsp.op_errstr = "";

        ret = dict_allocate_and_serialize (output, &rsp.output.output_val,
                                           &rsp.output.output_len);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to serialize output dict to rsp");
                goto out;
        }

        glusterfs_submit_reply (req, &rsp, NULL, 0, NULL,
                                (xdrproc_t)xdr_gd1_mgmt_brick_op_rsp);
        ret = 0;

out:
        free (nfs_req.input.input_val);
        if (dict)
                dict_unref (dict);
        if (output)
                dict_unref (output);
        GF_FREE (rsp.output.output_val);

        gf_log (THIS->name, GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterfs_handle_rpc_msg (rpcsvc_request_t *req)
{
        int ret = -1;
        /* for now, nothing */
        return ret;
}

rpcclnt_cb_actor_t mgmt_cbk_actors[] = {
        [GF_CBK_FETCHSPEC] = {"FETCHSPEC", GF_CBK_FETCHSPEC, mgmt_cbk_spec },
        [GF_CBK_EVENT_NOTIFY] = {"EVENTNOTIFY", GF_CBK_EVENT_NOTIFY,
                                 mgmt_cbk_event},
};


struct rpcclnt_cb_program mgmt_cbk_prog = {
        .progname  = "GlusterFS Callback",
        .prognum   = GLUSTER_CBK_PROGRAM,
        .progver   = GLUSTER_CBK_VERSION,
        .actors    = mgmt_cbk_actors,
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
        [GF_HNDSK_EVENT_NOTIFY] = "EVENTNOTIFY",
};

rpc_clnt_prog_t clnt_handshake_prog = {
        .progname  = "GlusterFS Handshake",
        .prognum   = GLUSTER_HNDSK_PROGRAM,
        .progver   = GLUSTER_HNDSK_VERSION,
        .procnames = clnt_handshake_procs,
};

rpcsvc_actor_t glusterfs_actors[] = {
        [GLUSTERD_BRICK_NULL]          = {"NULL",              GLUSTERD_BRICK_NULL,          glusterfs_handle_rpc_msg,             NULL, 0, DRC_NA},
        [GLUSTERD_BRICK_TERMINATE]     = {"TERMINATE",         GLUSTERD_BRICK_TERMINATE,     glusterfs_handle_terminate,           NULL, 0, DRC_NA},
        [GLUSTERD_BRICK_XLATOR_INFO]   = {"TRANSLATOR INFO",   GLUSTERD_BRICK_XLATOR_INFO,   glusterfs_handle_translator_info_get, NULL, 0, DRC_NA},
        [GLUSTERD_BRICK_XLATOR_OP]     = {"TRANSLATOR OP",     GLUSTERD_BRICK_XLATOR_OP,     glusterfs_handle_translator_op,       NULL, 0, DRC_NA},
        [GLUSTERD_BRICK_STATUS]        = {"STATUS",            GLUSTERD_BRICK_STATUS,        glusterfs_handle_brick_status,        NULL, 0, DRC_NA},
        [GLUSTERD_BRICK_XLATOR_DEFRAG] = {"TRANSLATOR DEFRAG", GLUSTERD_BRICK_XLATOR_DEFRAG, glusterfs_handle_defrag,              NULL, 0, DRC_NA},
        [GLUSTERD_NODE_PROFILE]        = {"NFS PROFILE",       GLUSTERD_NODE_PROFILE,        glusterfs_handle_nfs_profile,         NULL, 0, DRC_NA},
        [GLUSTERD_NODE_STATUS]         = {"NFS STATUS",        GLUSTERD_NODE_STATUS,         glusterfs_handle_node_status,         NULL, 0, DRC_NA},
};

struct rpcsvc_program glusterfs_mop_prog = {
        .progname  = "Gluster Brick operations",
        .prognum   = GD_BRICK_PROGRAM,
        .progver   = GD_BRICK_VERSION,
        .actors    = glusterfs_actors,
        .numactors = GLUSTERD_BRICK_MAXVALUE,
	.synctask  = _gf_true,
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

        if (iobuf)
                iobuf_unref (iobuf);
        return ret;
}


/* XXX: move these into @ctx */
static char *oldvolfile = NULL;
static int oldvollen = 0;



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
        char                    *volfilebuf = NULL;

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
                ret = rsp.op_errno;
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
        if (ferror (tmpfp)) {
                ret = -1;
                goto out;
        }

        /*  Check if only options have changed. No need to reload the
        *  volfile if topology hasn't changed.
        *  glusterfs_volfile_reconfigure returns 3 possible return states
        *  return 0          =======> reconfiguration of options has succeeded
        *  return 1          =======> the graph has to be reconstructed and all the xlators should be inited
        *  return -1(or -ve) =======> Some Internal Error occurred during the operation
        */

        ret = glusterfs_volfile_reconfigure (oldvollen, tmpfp, ctx, oldvolfile);
        if (ret == 0) {
                gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG,
                        "No need to re-load volfile, reconfigure done");
                if (oldvolfile)
                        volfilebuf = GF_REALLOC (oldvolfile, size);
                else
                        volfilebuf = GF_CALLOC (1, size, gf_common_mt_char);
                if (!volfilebuf) {
                        ret = -1;
                        goto out;
                }
                oldvolfile = volfilebuf;
                oldvollen = size;
                memcpy (oldvolfile, rsp.spec, size);
                goto out;
        }

        if (ret < 0) {
                gf_log ("glusterfsd-mgmt", GF_LOG_DEBUG, "Reconfigure failed !!");
                goto out;
        }

        ret = glusterfs_process_volfp (ctx, tmpfp);
        /* tmpfp closed */
        tmpfp = NULL;
        if (ret)
                goto out;

        if (oldvolfile)
                volfilebuf = GF_REALLOC (oldvolfile, size);
        else
                volfilebuf = GF_CALLOC (1, size, gf_common_mt_char);

        if (!volfilebuf) {
                ret = -1;
                goto out;
        }
        oldvolfile = volfilebuf;
        oldvollen = size;
        memcpy (oldvolfile, rsp.spec, size);
        if (!is_mgmt_rpc_reconnect) {
                glusterfs_mgmt_pmap_signin (ctx);
                is_mgmt_rpc_reconnect =  _gf_true;
        }

out:
        STACK_DESTROY (frame->root);

        free (rsp.spec);

        emancipate (ctx, ret);

        // Stop if server is running at an unsupported op-version
        if (ENOTSUP == ret) {
                gf_log ("mgmt", GF_LOG_ERROR, "Server is operating at an "
                        "op-version which is not supported");
                cleanup_and_exit (0);
        }

        if (ret && ctx && !ctx->active) {
                /* Do it only for the first time */
                /* Failed to get the volume file, something wrong,
                   restart the process */
                gf_log ("mgmt", GF_LOG_ERROR,
                        "failed to fetch volume file (key:%s)",
                        ctx->cmd_args.volfile_id);
                cleanup_and_exit (0);
        }


        if (tmpfp)
                fclose (tmpfp);

        return 0;
}


int
glusterfs_volfile_fetch (glusterfs_ctx_t *ctx)
{
        cmd_args_t       *cmd_args = NULL;
        gf_getspec_req    req = {0, };
        int               ret = 0;
        call_frame_t     *frame = NULL;
        dict_t           *dict = NULL;

        cmd_args = &ctx->cmd_args;

        frame = create_frame (THIS, ctx->pool);

        req.key = cmd_args->volfile_id;
        req.flags = 0;

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }

        // Set the supported min and max op-versions, so glusterd can make a
        // decision
        ret = dict_set_int32 (dict, "min-op-version", GD_OP_VERSION_MIN);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to set min-op-version"
                        " in request dict");
                goto out;
        }

        ret = dict_set_int32 (dict, "max-op-version", GD_OP_VERSION_MAX);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to set max-op-version"
                        " in request dict");
                goto out;
        }

        ret = dict_allocate_and_serialize (dict, &req.xdata.xdata_val,
                                           &req.xdata.xdata_len);
        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Failed to serialize dictionary");
                goto out;
        }

        ret = mgmt_submit_request (&req, frame, ctx, &clnt_handshake_prog,
                                   GF_HNDSK_GETSPEC, mgmt_getspec_cbk,
                                   (xdrproc_t)xdr_gf_getspec_req);

out:
        GF_FREE (req.xdata.xdata_val);
        if (dict)
                dict_unref (dict);

        return ret;
}

int32_t
mgmt_event_notify_cbk (struct rpc_req *req, struct iovec *iov, int count,
                  void *myframe)
{
        gf_event_notify_rsp      rsp   = {0,};
        call_frame_t            *frame = NULL;
        glusterfs_ctx_t         *ctx = NULL;
        int                      ret   = 0;

        frame = myframe;
        ctx = frame->this->ctx;

        if (-1 == req->rpc_status) {
                ret = -1;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_event_notify_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR, "XDR decoding error");
                ret   = -1;
                goto out;
        }

        if (-1 == rsp.op_ret) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "failed to get the rsp from server");
                ret = -1;
                goto out;
        }
out:
        free (rsp.dict.dict_val); //malloced by xdr
        return ret;

}

int32_t
glusterfs_rebalance_event_notify_cbk (struct rpc_req *req, struct iovec *iov,
                                      int count, void *myframe)
{
        gf_event_notify_rsp      rsp   = {0,};
        call_frame_t            *frame = NULL;
        glusterfs_ctx_t         *ctx = NULL;
        int                      ret   = 0;

        frame = myframe;
        ctx = frame->this->ctx;

        if (-1 == req->rpc_status) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "failed to get the rsp from server");
                ret = -1;
                goto out;
        }

        ret = xdr_to_generic (*iov, &rsp, (xdrproc_t)xdr_gf_event_notify_rsp);
        if (ret < 0) {
                gf_log (frame->this->name, GF_LOG_ERROR, "XDR decoding error");
                ret   = -1;
                goto out;
        }

        if (-1 == rsp.op_ret) {
                gf_log (frame->this->name, GF_LOG_ERROR,
                        "Received error (%s) from server",
                        strerror (rsp.op_errno));
                ret = -1;
                goto out;
        }
out:
        free (rsp.dict.dict_val); //malloced by xdr
        return ret;

}

int32_t
glusterfs_rebalance_event_notify (dict_t *dict)
{
        glusterfs_ctx_t         *ctx = NULL;
        gf_event_notify_req      req = {0,};
        int32_t                  ret = -1;
        cmd_args_t              *cmd_args = NULL;
        call_frame_t            *frame = NULL;

        ctx = glusterfsd_ctx;
        cmd_args = &ctx->cmd_args;

        frame = create_frame (THIS, ctx->pool);

        req.op = GF_EN_DEFRAG_STATUS;

        if (dict) {
                ret = dict_set_str (dict, "volname", cmd_args->volfile_id);
                if (ret)
                        gf_log ("", GF_LOG_ERROR, "failed to set volname");

                ret = dict_allocate_and_serialize (dict, &req.dict.dict_val,
                                                   &req.dict.dict_len);
        }

        ret = mgmt_submit_request (&req, frame, ctx, &clnt_handshake_prog,
                                   GF_HNDSK_EVENT_NOTIFY,
                                   glusterfs_rebalance_event_notify_cbk,
                                   (xdrproc_t)xdr_gf_event_notify_req);

        GF_FREE (req.dict.dict_val);

        if (frame) {
              STACK_DESTROY (frame->root);
        }
        return ret;
}

static int
mgmt_rpc_notify (struct rpc_clnt *rpc, void *mydata, rpc_clnt_event_t event,
                 void *data)
{
        xlator_t         *this = NULL;
        glusterfs_ctx_t  *ctx = NULL;
        int              ret = 0;
        server_cmdline_t *server = NULL;
        rpc_transport_t  *rpc_trans = NULL;
        int              need_term = 0;
        int              emval = 0;

        this = mydata;
        rpc_trans = rpc->conn.trans;
        ctx = this->ctx;

        switch (event) {
        case RPC_CLNT_DISCONNECT:
                if (!ctx->active) {
                        gf_log ("glusterfsd-mgmt", GF_LOG_ERROR,
                                "failed to connect with remote-host: %s (%s)",
                                ctx->cmd_args.volfile_server,
                                strerror (errno));
                        server = ctx->cmd_args.curr_server;
                        if (server->list.next == &ctx->cmd_args.volfile_servers) {
                                need_term = 1;
                                emval = ENOTCONN;
                                gf_log("glusterfsd-mgmt", GF_LOG_INFO,
                                       "Exhausted all volfile servers");
                                break;
                        }
                        server = list_entry (server->list.next, typeof(*server),
                                             list);
                        ctx->cmd_args.curr_server = server;
                        ctx->cmd_args.volfile_server = server->volfile_server;

                        ret = dict_set_str (rpc_trans->options,
                                            "remote-host",
                                            server->volfile_server);
                        if (ret != 0) {
                                gf_log ("glusterfsd-mgmt", GF_LOG_ERROR,
                                        "failed to set remote-host: %s",
                                        server->volfile_server);
                                need_term = 1;
                                emval = ENOTCONN;
                                break;
                        }
                        gf_log ("glusterfsd-mgmt", GF_LOG_INFO,
                                "connecting to next volfile server %s",
                                server->volfile_server);
                }
                break;
        case RPC_CLNT_CONNECT:
                rpc_clnt_set_connected (&((struct rpc_clnt*)ctx->mgmt)->conn);

                ret = glusterfs_volfile_fetch (ctx);
                if (ret) {
                        emval = ret;
                        if (!ctx->active) {
                                need_term = 1;
                                gf_log ("glusterfsd-mgmt", GF_LOG_ERROR,
                                        "failed to fetch volume file (key:%s)",
                                        ctx->cmd_args.volfile_id);
                                break;

                        }
                }

                if (is_mgmt_rpc_reconnect)
                        glusterfs_mgmt_pmap_signin (ctx);

                break;
        default:
                break;
        }

        if (need_term) {
                emancipate (ctx, emval);
                cleanup_and_exit (1);
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

        rpc = rpcsvc_init (THIS, ctx, options, 8);
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
glusterfs_listener_stop (glusterfs_ctx_t *ctx)
{
        cmd_args_t              *cmd_args = NULL;
        rpcsvc_t                *rpc = NULL;
        rpcsvc_listener_t       *listener = NULL;
        rpcsvc_listener_t       *next = NULL;
        int                     ret = 0;
        xlator_t                *this = NULL;

        GF_ASSERT (ctx);

        rpc = ctx->listener;
        ctx->listener = NULL;

        (void) rpcsvc_program_unregister(rpc, &glusterfs_mop_prog);

        list_for_each_entry_safe (listener, next, &rpc->listeners, list) {
                rpcsvc_listener_destroy (listener);
        }

        (void) rpcsvc_unregister_notify (rpc, glusterfs_rpcsvc_notify, THIS);

        GF_FREE (rpc);

        cmd_args = &ctx->cmd_args;
        if (cmd_args->sock_file) {
                ret = unlink (cmd_args->sock_file);
                if (ret && (ENOENT == errno)) {
                        ret = 0;
                }
        }

        if (ret) {
                this = THIS;
                gf_log (this->name, GF_LOG_ERROR, "Failed to unlink listener "
                        "socket %s, error: %s", cmd_args->sock_file,
                        strerror (errno));
        }
        return ret;
}

int
glusterfs_mgmt_notify (int32_t op, void *data, ...)
{
        int ret = 0;
        switch (op)
        {
                case GF_EN_DEFRAG_STATUS:
                        ret = glusterfs_rebalance_event_notify ((dict_t*) data);
                        break;

                default:
                        gf_log ("", GF_LOG_ERROR, "Invalid op");
                        break;
        }

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

        rpc = rpc_clnt_new (options, THIS->ctx, THIS->name, 8);
        if (!rpc) {
                ret = -1;
                gf_log (THIS->name, GF_LOG_WARNING, "failed to create rpc clnt");
                goto out;
        }

        ret = rpc_clnt_register_notify (rpc, mgmt_rpc_notify, THIS);
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to register notify function");
                goto out;
        }

        ret = rpcclnt_cbk_program_register (rpc, &mgmt_cbk_prog, THIS);
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to register callback function");
                goto out;
        }

        ctx->notify = glusterfs_mgmt_notify;

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

        ctx = glusterfsd_ctx;
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

        ctx = glusterfsd_ctx;
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
