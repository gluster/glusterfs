/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <glusterfs/xlator.h>
#include <glusterfs/defaults.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/syscall.h>
#include <glusterfs/compat-errno.h>

#include "glusterd.h"
#include "glusterd-messages.h"
#include "glusterfs3.h"
#include "protocol-common.h"
#include "rpcsvc.h"
#include "rpc-common-xdr.h"

typedef ssize_t (*gfs_serialize_t)(struct iovec outmsg, void *data);

size_t
build_volfile_path(char *volume_id, char *path, size_t path_len)
{
    struct stat stbuf = {
        0,
    };
    int32_t ret = -1;
    char *volid_ptr = NULL;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;

    priv = this->private;
    GF_ASSERT(priv);
    GF_ASSERT(volume_id);
    GF_ASSERT(path);

    if (volume_id[0] == '/') {
        /* Normal behavior */
        volid_ptr = volume_id;
        volid_ptr++;

    } else {
        /* Bringing in NFS like behavior for mount command, */
        /* With this, one can mount a volume with below cmd */
        /* bash# mount -t glusterfs server:/volume /mnt/pnt */
        volid_ptr = volume_id;
    }

    ret = snprintf(path, path_len, "%s/%s.vol", priv->workdir, volid_ptr);
    if (ret == -1) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_COPY_FAIL, NULL);
        goto out;
    }

    ret = sys_stat(path, &stbuf);
out:
    return ret;
}

struct iobuf *
glusterd_serialize_reply(rpcsvc_request_t *req, void *arg, struct iovec *outmsg,
                         xdrproc_t xdrproc)
{
    struct iobuf *iob = NULL;
    ssize_t retlen = -1;
    ssize_t rsp_size = 0;

    /* First, get the io buffer into which the reply in arg will
     * be serialized.
     */
    rsp_size = xdr_sizeof(xdrproc, arg);
    iob = iobuf_get2(req->svc->ctx->iobuf_pool, rsp_size);
    if (!iob) {
        gf_msg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Failed to get iobuf");
        goto ret;
    }

    iobuf_to_iovec(iob, outmsg);
    /* Use the given serializer to translate the give C structure in arg
     * to XDR format which will be written into the buffer in outmsg.
     */
    /* retlen is used to received the error since size_t is unsigned and we
     * need -1 for error notification during encoding.
     */
    retlen = xdr_serialize_generic(*outmsg, arg, xdrproc);
    if (retlen == -1) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_ENCODE_FAIL,
               "Failed to encode message");
        goto ret;
    }

    outmsg->iov_len = retlen;
ret:
    if (retlen == -1) {
        iobuf_unref(iob);
        iob = NULL;
    }

    return iob;
}

int
glusterd_submit_reply(rpcsvc_request_t *req, void *arg, struct iovec *payload,
                      int payloadcount, struct iobref *iobref,
                      xdrproc_t xdrproc)
{
    struct iobuf *iob = NULL;
    int ret = -1;
    struct iovec rsp = {
        0,
    };
    char new_iobref = 0;

    if (!req) {
        GF_ASSERT(req);
        goto out;
    }

    if (!iobref) {
        iobref = iobref_new();
        if (!iobref) {
            gf_msg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                   "out of memory");
            goto out;
        }

        new_iobref = 1;
    }

    iob = glusterd_serialize_reply(req, arg, &rsp, xdrproc);
    if (!iob) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_SERIALIZE_MSG_FAIL,
               "Failed to serialize reply");
    } else {
        iobref_add(iobref, iob);
    }

    ret = rpcsvc_submit_generic(req, &rsp, 1, payload, payloadcount, iobref);

    /* Now that we've done our job of handing the message to the RPC layer
     * we can safely unref the iob in the hope that RPC layer must have
     * ref'ed the iob on receiving into the txlist.
     */
    if (ret == -1) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_REPLY_SUBMIT_FAIL,
               "Reply submission failed");
        goto out;
    }

    ret = 0;
out:

    if (new_iobref) {
        iobref_unref(iobref);
    }

    if (iob)
        iobuf_unref(iob);
    return ret;
}

int
server_getspec(rpcsvc_request_t *req)
{
    int32_t ret = -1;
    int32_t op_errno = 0;
    int32_t spec_fd = -1;
    size_t file_len = 0;
    char filename[PATH_MAX] = {
        0,
    };
    struct stat stbuf = {
        0,
    };
    char *volume = NULL;
    gf_getspec_req args = {
        0,
    };
    gf_getspec_rsp rsp = {
        0,
    };
    xlator_t *this = THIS;

    ret = xdr_to_generic(req->msg[0], &args, (xdrproc_t)xdr_gf_getspec_req);
    if (ret < 0) {
        // failed to decode msg;
        req->rpc_err = GARBAGE_ARGS;
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_REQ_DECODE_FAIL,
               "Failed to decode the message");
        goto fail;
    }

    volume = args.key;

    if (strlen(volume) >= (NAME_MAX)) {
        op_errno = EINVAL;
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_NAME_TOO_LONG,
               "volume name too long (%s)", volume);
        goto fail;
    }

    gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_MOUNT_REQ_RCVD,
           "Received mount request for volume %s", volume);

    /* Need to strip leading '/' from volnames. This was introduced to
     * support nfs style mount parameters for native gluster mount
     */

    ret = build_volfile_path(volume, filename, sizeof(filename));

    if (ret == 0) {
        /* to allocate the proper buffer to hold the file data */
        ret = sys_stat(filename, &stbuf);
        if (ret < 0) {
            gf_msg("glusterd", GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
                   "Unable to stat %s (%s)", filename, strerror(errno));
            goto fail;
        }

        spec_fd = open(filename, O_RDONLY);
        if (spec_fd < 0) {
            gf_msg("glusterd", GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
                   "Unable to open %s (%s)", filename, strerror(errno));
            goto fail;
        }
        ret = file_len = stbuf.st_size;
    }

    if (file_len) {
        rsp.spec = CALLOC(file_len + 1, sizeof(char));
        if (!rsp.spec) {
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY, NULL);
            ret = -1;
            op_errno = ENOMEM;
            goto fail;
        }
        ret = sys_read(spec_fd, rsp.spec, file_len);
    }

    /* convert to XDR */
fail:
    if (spec_fd >= 0)
        sys_close(spec_fd);

    rsp.op_ret = ret;
    if (rsp.op_ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MOUNT_REQ_FAIL,
               "Failed to mount the volume");
        if (!op_errno)
            op_errno = ENOENT;
    }

    if (op_errno)
        rsp.op_errno = gf_errno_to_error(op_errno);

    if (!rsp.spec)
        rsp.spec = strdup("");

    glusterd_submit_reply(req, &rsp, NULL, 0, NULL,
                          (xdrproc_t)xdr_gf_getspec_rsp);

    free(args.key);  // malloced by xdr
    free(rsp.spec);

    if (args.xdata.xdata_val)
        free(args.xdata.xdata_val);

    if (rsp.xdata.xdata_val)
        GF_FREE(rsp.xdata.xdata_val);

    return 0;
}

static rpcsvc_actor_t gluster_handshake_actors[GF_HNDSK_MAXVALUE] = {
    [GF_HNDSK_NULL] = {"NULL", NULL, NULL, GF_HNDSK_NULL, DRC_NA, 0},
    [GF_HNDSK_GETSPEC] = {"GETSPEC", server_getspec, NULL, GF_HNDSK_GETSPEC,
                          DRC_NA, 0},
};

struct rpcsvc_program gluster_handshake_prog = {
    .progname = "Gluster Handshake",
    .prognum = GLUSTER_HNDSK_PROGRAM,
    .progver = GLUSTER_HNDSK_VERSION,
    .actors = gluster_handshake_actors,
    .numactors = GF_HNDSK_MAXVALUE,
};

/* A minimal RPC program just for the cli getspec command */
static rpcsvc_actor_t gluster_cli_getspec_actors[GF_HNDSK_MAXVALUE] = {
    [GF_HNDSK_GETSPEC] = {"GETSPEC", server_getspec, NULL, GF_HNDSK_GETSPEC,
                          DRC_NA, 0},
};

struct rpcsvc_program gluster_cli_getspec_prog = {
    .progname = "Gluster Handshake (CLI Getspec)",
    .prognum = GLUSTER_HNDSK_PROGRAM,
    .progver = GLUSTER_HNDSK_VERSION,
    .actors = gluster_cli_getspec_actors,
    .numactors = GF_HNDSK_MAXVALUE,
};
