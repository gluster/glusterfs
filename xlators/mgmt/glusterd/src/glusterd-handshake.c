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

#include "xlator.h"
#include "glusterfs.h"
#include "compat-errno.h"

#include "glusterd.h"
#include "glusterd-utils.h"

#include "glusterfs3.h"
#include "protocol-common.h"
#include "rpcsvc.h"


typedef ssize_t (*gfs_serialize_t) (struct iovec outmsg, void *data);


static size_t
build_volfile_path (const char *volname, char *path,
                    size_t path_len)
{
        struct stat         stbuf       = {0,};
        int32_t             ret         = -1;
        glusterd_conf_t    *priv        = NULL;
        char               *vol         = NULL;
        char               *dup_volname = NULL;
        char               *free_ptr    = NULL;
        char               *tmp         = NULL;
        glusterd_volinfo_t *volinfo     = NULL;

        priv    = THIS->private;
        dup_volname = gf_strdup (volname);
        free_ptr = dup_volname;

        ret = glusterd_volinfo_find (dup_volname, &volinfo);
        if (ret) {
                /* Split the volume name */
                vol = strtok_r (dup_volname, ".", &tmp);
                if (!vol)
                        goto out;
                ret = glusterd_volinfo_find (vol, &volinfo);
                if (ret)
                        goto out;
        }
        ret = snprintf (path, path_len, "%s/vols/%s/%s.vol",
                        priv->workdir, volinfo->volname, volname);
        if (ret == -1)
                goto out;

        ret = stat (path, &stbuf);
        if ((ret == -1) && (errno == ENOENT)) {
                ret = snprintf (path, path_len, "%s/vols/%s/%s-fuse.vol",
                                priv->workdir, volinfo->volname, volname);
                ret = stat (path, &stbuf);
        }
        if ((ret == -1) && (errno == ENOENT)) {
                ret = snprintf (path, path_len, "%s/vols/%s/%s-tcp.vol",
                                priv->workdir, volinfo->volname, volname);
        }

        ret = 1;
out:
        if (free_ptr)
                GF_FREE (free_ptr);
        return ret;
}

static int
xdr_to_glusterfs_req (rpcsvc_request_t *req, void *arg, gfs_serialize_t sfunc)
{
        int                     ret = -1;

        if (!req)
                return -1;

        ret = sfunc (req->msg[0], arg);

        if (ret > 0)
                ret = 0;

        return ret;
}


int
server_getspec (rpcsvc_request_t *req)
{
        int32_t               ret = -1;
        int32_t               op_errno = 0;
        int32_t               spec_fd = -1;
        size_t                file_len = 0;
        char                  filename[ZR_PATH_MAX] = {0,};
        struct stat           stbuf = {0,};
        char                 *volume = NULL;
        int                   cookie = 0;

        gf_getspec_req    args = {0,};
        gf_getspec_rsp    rsp  = {0,};


        if (xdr_to_glusterfs_req (req, &args, xdr_to_getspec_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto fail;
        }

        volume = args.key;

        ret = build_volfile_path (volume, filename, sizeof (filename));

        if (ret > 0) {
                /* to allocate the proper buffer to hold the file data */
                ret = stat (filename, &stbuf);
                if (ret < 0){
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "Unable to stat %s (%s)",
                                filename, strerror (errno));
                        goto fail;
                }

                spec_fd = open (filename, O_RDONLY);
                if (spec_fd < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR,
                                "Unable to open %s (%s)",
                                filename, strerror (errno));
                        goto fail;
                }
                ret = file_len = stbuf.st_size;
        } else {
                op_errno = ENOENT;
        }

        if (file_len) {
                rsp.spec = CALLOC (file_len, sizeof (char));
                if (!rsp.spec) {
                        ret = -1;
                        op_errno = ENOMEM;
                        goto fail;
                }
                ret = read (spec_fd, rsp.spec, file_len);

                close (spec_fd);
        }

        /* convert to XDR */
fail:
        rsp.op_ret   = ret;

        if (op_errno)
                rsp.op_errno = gf_errno_to_error (op_errno);
        if (cookie)
                rsp.op_errno = cookie;

        if (!rsp.spec)
                rsp.spec = "";

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (gd_serialize_t)xdr_serialize_getspec_rsp);
        if (args.key)
                free (args.key);//malloced by xdr
        if (rsp.spec)
                free (rsp.spec);

        return 0;
}


rpcsvc_actor_t gluster_handshake_actors[] = {
        [GF_HNDSK_NULL]      = {"NULL",      GF_HNDSK_NULL,      NULL, NULL, NULL },
        [GF_HNDSK_GETSPEC]   = {"GETSPEC",   GF_HNDSK_GETSPEC,   server_getspec, NULL, NULL },
};


struct rpcsvc_program gluster_handshake_prog = {
        .progname  = "GlusterFS Handshake",
        .prognum   = GLUSTER_HNDSK_PROGRAM,
        .progver   = GLUSTER_HNDSK_VERSION,
        .actors    = gluster_handshake_actors,
        .numactors = GF_HNDSK_MAXVALUE,
};
