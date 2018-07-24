/*
  Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <openssl/md5.h>

#include "server.h"
#include "server-helpers.h"
#include "rpc-common-xdr.h"
#include "glusterfs4-xdr.h"
#include "glusterfs3.h"
#include "compat-errno.h"
#include "server-messages.h"
#include "defaults.h"
#include "default-args.h"
#include "server-common.h"
#include "xlator.h"
#include "compound-fop-utils.h"

#include "xdr-nfs3.h"

#define SERVER_REQ_SET_ERROR(req, ret)                          \
        do {                                                    \
                rpcsvc_request_seterr (req, GARBAGE_ARGS);      \
                ret = RPCSVC_ACTOR_ERROR;                       \
        } while (0)

extern int
server3_3_getxattr (rpcsvc_request_t *req);

extern void
set_resolve_gfid (client_t *client, uuid_t resolve_gfid,
                  char *on_wire_gfid);
extern int
_gf_server_log_setxattr_failure (dict_t *d, char *k, data_t *v,
                                 void *tmp);
extern int
rpc_receive_common (rpcsvc_request_t *req, call_frame_t **fr,
                    server_state_t **st, ssize_t *xdrlen, void *args,
                    void *xdrfn, glusterfs_fop_t fop);


/* Callback function section */
int
server4_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct statvfs *buf,
                   dict_t *xdata)
{
        gfx_statfs_rsp      rsp    = {0,};
        rpcsvc_request_t    *req    = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, op_errno, PS_MSG_STATFS,
                        "%"PRId64": STATFS, client: %s, error-xlator: %s",
                        frame->root->unique, STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_statfs (&rsp, buf);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_statfs_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   inode_t *inode, struct iatt *stbuf, dict_t *xdata,
                   struct iatt *postparent)
{
        rpcsvc_request_t    *req        = NULL;
        server_state_t      *state      = NULL;
        loc_t                fresh_loc  = {0,};
        gfx_common_2iatt_rsp      rsp        = {0,};

        state = CALL_STATE (frame);

        if (state->is_revalidate == 1 && op_ret == -1) {
                state->is_revalidate = 2;
                loc_copy (&fresh_loc, &state->loc);
                inode_unref (fresh_loc.inode);
                fresh_loc.inode = server_inode_new (state->itable,
                                                    fresh_loc.gfid);

                STACK_WIND (frame, server4_lookup_cbk,
                            frame->root->client->bound_xl,
                            frame->root->client->bound_xl->fops->lookup,
                            &fresh_loc, state->xdata);

                loc_wipe (&fresh_loc);
                return 0;
        }

        gfx_stat_from_iattx (&rsp.poststat, postparent);

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret) {
                if (state->is_revalidate && op_errno == ENOENT) {
                        if (!__is_root_gfid (state->resolve.gfid)) {
                                inode_unlink (state->loc.inode,
                                              state->loc.parent,
                                              state->loc.name);
                                /**
                                 * If the entry is not present, then just
                                 * unlinking the associated dentry is not
                                 * suffecient. This condition should be
                                 * treated as unlink of the entry. So along
                                 * with deleting the entry, its also important
                                 * to forget the inode for it (if the dentry
                                 * being considered was the last dentry).
                                 * Otherwise it might lead to inode leak.
                                 * It also might lead to wrong decisions being
                                 * taken if the future lookups on this inode are
                                 * successful since they are able to find the
                                 * inode in the inode table (atleast gfid based
                                 * lookups will be successful, if the lookup
                                 * is a soft lookup)
                                 */
                                forget_inode_if_no_dentry (state->loc.inode);
                        }
                }
                goto out;
        }

        server4_post_lookup (&rsp, frame, state, inode, stbuf);
out:
        rsp.op_ret   = op_ret;
        rsp.op_errno = gf_errno_to_error (op_errno);

        if (op_ret) {
                if (state->resolve.bname) {
                        gf_msg (this->name,
                                fop_log_level (GF_FOP_LOOKUP, op_errno),
                                op_errno, PS_MSG_LOOKUP_INFO,
                                "%"PRId64": LOOKUP %s (%s/%s), client: %s, "
                                "error-xlator: %s", frame->root->unique,
                                state->loc.path,
                                uuid_utoa (state->resolve.pargfid),
                                state->resolve.bname,
                                STACK_CLIENT_NAME (frame->root),
                                STACK_ERR_XL_NAME (frame->root));
                } else {
                        gf_msg (this->name,
                                fop_log_level (GF_FOP_LOOKUP, op_errno),
                                op_errno, PS_MSG_LOOKUP_INFO,
                                "%"PRId64": LOOKUP %s (%s), client: %s, "
                                "error-xlator: %s",
                                frame->root->unique, state->loc.path,
                                uuid_utoa (state->resolve.gfid),
                                STACK_CLIENT_NAME (frame->root),
                                STACK_ERR_XL_NAME (frame->root));
                }
        }

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_2iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_lease_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct gf_lease *lease,
                  dict_t *xdata)
{
        gfx_lease_rsp       rsp   = {0,};
        rpcsvc_request_t    *req   = NULL;
        server_state_t      *state = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_LEASE, op_errno),
                        op_errno, PS_MSG_LK_INFO,
                       "%"PRId64": LEASE %s (%s), client: %s, error-xlator: %s",
                        frame->root->unique, state->loc.path,
                        uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
        }
        server4_post_lease (&rsp, lease);

        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_lease_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
               dict_t *xdata)
{
        gfx_lk_rsp          rsp   = {0,};
        rpcsvc_request_t    *req   = NULL;
        server_state_t      *state = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_LK, op_errno),
                        op_errno, PS_MSG_LK_INFO,
                        "%"PRId64": LK %"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_lk (this, &rsp, lock);
out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_lk_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}


int
server4_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        gfx_common_rsp    rsp        = {0,};
        server_state_t   *state     = NULL;
        rpcsvc_request_t *req       = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        state = CALL_STATE (frame);

        if (op_ret < 0) {
                gf_msg (this->name, fop_log_level (GF_FOP_INODELK, op_errno),
                        op_errno, PS_MSG_INODELK_INFO,
                        "%"PRId64": INODELK %s (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->loc.path, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}


int
server4_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        gfx_common_rsp     rsp       = {0,};
        server_state_t   *state     = NULL;
        rpcsvc_request_t *req       = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        state = CALL_STATE (frame);

        if (op_ret < 0) {
                gf_msg (this->name, fop_log_level (GF_FOP_FINODELK, op_errno),
                        op_errno, PS_MSG_INODELK_INFO,
                        "%"PRId64": FINODELK %"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        gfx_common_rsp     rsp       = {0,};
        server_state_t   *state     = NULL;
        rpcsvc_request_t *req       = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        state = CALL_STATE (frame);

        if (op_ret < 0) {
                gf_msg (this->name, fop_log_level (GF_FOP_ENTRYLK, op_errno),
                        op_errno, PS_MSG_ENTRYLK_INFO,
                        "%"PRId64": ENTRYLK %s (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->loc.path, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}


int
server4_fentrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        gfx_common_rsp     rsp       = {0,};
        server_state_t   *state     = NULL;
        rpcsvc_request_t *req       = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        state = CALL_STATE (frame);

        if (op_ret < 0) {
                gf_msg (this->name, fop_log_level (GF_FOP_FENTRYLK, op_errno),
                        op_errno, PS_MSG_ENTRYLK_INFO,
                        "%"PRId64": FENTRYLK %"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req   = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}


int
server4_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        gfx_common_rsp        rsp   = {0,};
        rpcsvc_request_t    *req   = NULL;
        server_state_t      *state = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret) {
                state = CALL_STATE (frame);
                gf_msg (this->name, GF_LOG_INFO,
                        op_errno, PS_MSG_ACCESS_INFO,
                        "%"PRId64": ACCESS %s (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        (state->loc.path) ? state->loc.path : "",
                        uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        gfx_common_2iatt_rsp       rsp    = {0,};
        server_state_t      *state  = NULL;
        rpcsvc_request_t    *req    = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        state = CALL_STATE (frame);

        if (op_ret) {
                gf_msg (this->name, GF_LOG_INFO,
                        op_errno, PS_MSG_DIR_INFO,
                        "%"PRId64": RMDIR %s (%s/%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        (state->loc.path) ? state->loc.path : "",
                        uuid_utoa (state->resolve.pargfid),
                        state->resolve.bname, STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_entry_remove (state, &rsp, preparent, postparent);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_2iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *stbuf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        gfx_common_3iatt_rsp       rsp        = {0,};
        server_state_t      *state      = NULL;
        rpcsvc_request_t    *req        = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        state = CALL_STATE (frame);

        if (op_ret < 0) {
                gf_msg (this->name, fop_log_level (GF_FOP_MKDIR, op_errno),
                        op_errno, PS_MSG_DIR_INFO,
                        "%"PRId64": MKDIR %s (%s/%s) client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        (state->loc.path) ? state->loc.path : "",
                        uuid_utoa (state->resolve.pargfid),
                        state->resolve.bname,
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_common_3iatt (state, &rsp, inode, stbuf, preparent,
                                   postparent);
out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_3iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  inode_t *inode, struct iatt *stbuf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        gfx_common_3iatt_rsp       rsp        = {0,};
        server_state_t      *state      = NULL;
        rpcsvc_request_t    *req        = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        state = CALL_STATE (frame);

        if (op_ret < 0) {
                gf_msg (this->name, fop_log_level (GF_FOP_MKNOD, op_errno),
                        op_errno, PS_MSG_MKNOD_INFO,
                        "%"PRId64": MKNOD %s (%s/%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->loc.path, uuid_utoa (state->resolve.pargfid),
                        state->resolve.bname, STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_common_3iatt (state, &rsp, inode, stbuf, preparent,
                                   postparent);
out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_3iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        gfx_common_rsp        rsp   = {0,};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret < 0) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_FSYNCDIR, op_errno),
                        op_errno, PS_MSG_DIR_INFO,
                        "%"PRId64": FSYNCDIR %"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                    dict_t *xdata)
{
        gfx_readdir_rsp     rsp   = {0,};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;
        int                  ret   = 0;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret < 0) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_READDIR, op_errno),
                        op_errno, PS_MSG_DIR_INFO,
                        "%"PRId64": READDIR %"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        /* (op_ret == 0) is valid, and means EOF */
        if (op_ret) {
                ret = server4_post_readdir (&rsp, entries);
                if (ret == -1) {
                        op_ret   = -1;
                        op_errno = ENOMEM;
                        goto out;
                }
        }

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_readdir_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        readdir_rsp_cleanup_v2 (&rsp);

        return 0;
}

int
server4_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        server_state_t      *state    = NULL;
        rpcsvc_request_t    *req      = NULL;
        gfx_open_rsp         rsp      = {0,};
        uint64_t             fd_no    = 0;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret < 0) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_OPENDIR, op_errno),
                        op_errno, PS_MSG_DIR_INFO,
                        "%"PRId64": OPENDIR %s (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        (state->loc.path) ? state->loc.path : "",
                        uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }


        op_ret = server4_post_open (frame, this, &rsp, fd);
        if (op_ret)
                goto out;
out:
        if (op_ret)
                rsp.fd = fd_no;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_open_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        gfx_common_rsp        rsp   = {0,};
        rpcsvc_request_t    *req   = NULL;
        server_state_t      *state = NULL;
        gf_loglevel_t        loglevel = GF_LOG_NONE;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret == -1) {
                state = CALL_STATE (frame);
                if (ENODATA == op_errno || ENOATTR == op_errno)
                        loglevel = GF_LOG_DEBUG;
                else
                        loglevel = GF_LOG_INFO;

                gf_msg (this->name, loglevel, op_errno,
                        PS_MSG_REMOVEXATTR_INFO,
                        "%"PRId64": REMOVEXATTR %s (%s) of key %s, client: %s, "
                        "error-xlator: %s",
                        frame->root->unique, state->loc.path,
                        uuid_utoa (state->resolve.gfid),
                        state->name, STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req   = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        gfx_common_rsp        rsp   = {0,};
        rpcsvc_request_t    *req   = NULL;
        server_state_t      *state = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret == -1) {
                state = CALL_STATE (frame);
                gf_msg (this->name,
                        fop_log_level (GF_FOP_FREMOVEXATTR, op_errno), op_errno,
                        PS_MSG_REMOVEXATTR_INFO,
                        "%"PRId64": FREMOVEXATTR %"PRId64" (%s) (%s), "
                        "client: %s, error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        state->name, STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req   = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *dict,
                     dict_t *xdata)
{
        gfx_common_dict_rsp    rsp   = {0,};
        rpcsvc_request_t    *req   = NULL;
        server_state_t      *state = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret == -1) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_GETXATTR, op_errno),
                        op_errno, PS_MSG_GETXATTR_INFO,
                        "%"PRId64": GETXATTR %s (%s) (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->loc.path, uuid_utoa (state->resolve.gfid),
                        state->name, STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        dict_to_xdr (dict, &rsp.dict);
out:
        rsp.op_ret        = op_ret;
        rsp.op_errno      = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_dict_rsp);

        GF_FREE (rsp.dict.pairs.pairs_val);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}


int
server4_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict,
                      dict_t *xdata)
{
        gfx_common_dict_rsp   rsp   = {0,};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret == -1) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_FGETXATTR, op_errno),
                        op_errno, PS_MSG_GETXATTR_INFO,
                        "%"PRId64": FGETXATTR %"PRId64" (%s) (%s), "
                        "client: %s, error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        state->name, STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        dict_to_xdr (dict, &rsp.dict);
out:
        rsp.op_ret        = op_ret;
        rsp.op_errno      = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_dict_rsp);

        GF_FREE (rsp.dict.pairs.pairs_val);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        gfx_common_rsp     rsp = {0,};
        rpcsvc_request_t *req = NULL;
        server_state_t      *state = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret == -1) {
                state = CALL_STATE (frame);
                if (op_errno != ENOTSUP)
                        dict_foreach (state->dict,
                                      _gf_server_log_setxattr_failure,
                                      frame);

                if (op_errno == ENOTSUP) {
                        gf_msg_debug (THIS->name, 0, "%s",
                                      strerror (op_errno));
                } else {
                        gf_msg (THIS->name, GF_LOG_INFO, op_errno,
                                PS_MSG_SETXATTR_INFO, "client: %s, "
                                "error-xlator: %s",
                                STACK_CLIENT_NAME (frame->root),
                                STACK_ERR_XL_NAME (frame->root));
                }
                goto out;
        }

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        gfx_common_rsp     rsp = {0,};
        rpcsvc_request_t *req = NULL;
        server_state_t      *state = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret == -1) {
                state = CALL_STATE (frame);
                if (op_errno != ENOTSUP) {
                        dict_foreach (state->dict,
                                      _gf_server_log_setxattr_failure,
                                      frame);
                }
                if (op_errno == ENOTSUP) {
                        gf_msg_debug (THIS->name, 0, "%s",
                                      strerror (op_errno));
                } else {
                        gf_msg (THIS->name, GF_LOG_INFO, op_errno,
                                PS_MSG_SETXATTR_INFO, "client: %s, "
                                "error-xlator: %s",
                                STACK_CLIENT_NAME (frame->root),
                                STACK_ERR_XL_NAME (frame->root));
                }
                goto out;
        }

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                   struct iatt *preoldparent, struct iatt *postoldparent,
                   struct iatt *prenewparent, struct iatt *postnewparent,
                   dict_t *xdata)
{
        gfx_rename_rsp      rsp        = {0,};
        server_state_t      *state      = NULL;
        rpcsvc_request_t    *req        = NULL;
        char         oldpar_str[50]     = {0,};
        char         newpar_str[50]     = {0,};

        dict_to_xdr (xdata, &rsp.xdata);

        state = CALL_STATE (frame);

        if (op_ret == -1) {
                uuid_utoa_r (state->resolve.pargfid, oldpar_str);
                uuid_utoa_r (state->resolve2.pargfid, newpar_str);
                gf_msg (this->name, GF_LOG_INFO, op_errno, PS_MSG_RENAME_INFO,
                        "%"PRId64": RENAME %s (%s/%s) -> %s (%s/%s), "
                        "client: %s, error-xlator: %s", frame->root->unique,
                        state->loc.path, oldpar_str, state->resolve.bname,
                        state->loc2.path, newpar_str, state->resolve2.bname,
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_rename (frame, state, &rsp, stbuf,
                            preoldparent, postoldparent,
                            prenewparent, postnewparent);
out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req   = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_rename_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        gfx_common_2iatt_rsp      rsp    = {0,};
        server_state_t      *state  = NULL;
        rpcsvc_request_t    *req    = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        state = CALL_STATE (frame);

        if (op_ret) {
                gf_msg (this->name, fop_log_level (GF_FOP_UNLINK, op_errno),
                        op_errno, PS_MSG_LINK_INFO,
                        "%"PRId64": UNLINK %s (%s/%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->loc.path, uuid_utoa (state->resolve.pargfid),
                        state->resolve.bname,
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        /* TODO: log gfid of the inodes */
        gf_msg_trace (frame->root->client->bound_xl->name, 0, "%"PRId64": "
                      "UNLINK_CBK %s", frame->root->unique, state->loc.name);

        server4_post_entry_remove (state, &rsp, preparent, postparent);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_2iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *stbuf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
        gfx_common_3iatt_rsp     rsp        = {0,};
        server_state_t      *state      = NULL;
        rpcsvc_request_t    *req        = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        state = CALL_STATE (frame);

        if (op_ret < 0) {
                gf_msg (this->name, GF_LOG_INFO, op_errno, PS_MSG_LINK_INFO,
                        "%"PRId64": SYMLINK %s (%s/%s), client: %s, "
                        "error-xlator:%s", frame->root->unique,
                        (state->loc.path) ? state->loc.path : "",
                        uuid_utoa (state->resolve.pargfid),
                        state->resolve.bname,
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_common_3iatt (state, &rsp, inode, stbuf, preparent,
                                   postparent);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_3iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}


int
server4_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *stbuf, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
        gfx_common_3iatt_rsp rsp         = {0,};
        server_state_t      *state       = NULL;
        rpcsvc_request_t    *req         = NULL;
        char              gfid_str[50]   = {0,};
        char              newpar_str[50] = {0,};

        dict_to_xdr (xdata, &rsp.xdata);

        state = CALL_STATE (frame);

        if (op_ret) {
                uuid_utoa_r (state->resolve.gfid, gfid_str);
                uuid_utoa_r (state->resolve2.pargfid, newpar_str);

                gf_msg (this->name, GF_LOG_INFO, op_errno, PS_MSG_LINK_INFO,
                        "%"PRId64": LINK %s (%s) -> %s/%s, client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->loc.path, gfid_str, newpar_str,
                        state->resolve2.bname, STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_link (state, &rsp, inode, stbuf, preparent,
                           postparent);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_3iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
        gfx_common_2iatt_rsp    rsp   = {0,};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret) {
                state = CALL_STATE (frame);
                gf_msg (this->name, GF_LOG_INFO, op_errno,
                        PS_MSG_TRUNCATE_INFO,
                        "%"PRId64": TRUNCATE %s (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->loc.path, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_common_2iatt (&rsp, prebuf, postbuf);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_2iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                  dict_t *xdata)
{
        gfx_common_iatt_rsp  rsp   = {0,};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        state = CALL_STATE (frame);
        if (op_ret) {
                gf_msg (this->name, fop_log_level (GF_FOP_FSTAT, op_errno),
                        op_errno, PS_MSG_STAT_INFO,
                        "%"PRId64": FSTAT %"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_common_iatt (state, &rsp, stbuf);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
        gfx_common_2iatt_rsp   rsp   = {0};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_FTRUNCATE, op_errno),
                        op_errno, PS_MSG_TRUNCATE_INFO,
                        "%"PRId64": FTRUNCATE %"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_common_2iatt (&rsp, prebuf, postbuf);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_2iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        gfx_common_rsp        rsp   = {0,};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret < 0) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_FLUSH, op_errno),
                        op_errno, PS_MSG_FLUSH_INFO,
                        "%"PRId64": FLUSH %"PRId64" (%s), client: %s, "
                        "error-xlator: %s",
                        frame->root->unique, state->resolve.fd_no,
                        uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
        gfx_common_2iatt_rsp       rsp   = {0,};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret < 0) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_FSYNC, op_errno),
                        op_errno, PS_MSG_SYNC_INFO,
                        "%"PRId64": FSYNC %"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_common_2iatt (&rsp, prebuf, postbuf);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_2iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
        gfx_common_2iatt_rsp       rsp   = {0,};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret < 0) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_WRITE, op_errno),
                        op_errno, PS_MSG_WRITE_INFO,
                        "%"PRId64": WRITEV %"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_common_2iatt (&rsp, prebuf, postbuf);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_2iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}


int
server4_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iovec *vector, int32_t count,
                  struct iatt *stbuf, struct iobref *iobref, dict_t *xdata)
{
        gfx_read_rsp        rsp   = {0,};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

#ifdef GF_TESTING_IO_XDATA
        {
                int ret = 0;
                if (!xdata)
                        xdata = dict_new ();

                ret = dict_set_str (xdata, "testing-the-xdata-key",
                                       "testing-xdata-value");
        }
#endif
        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret < 0) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_READ, op_errno),
                        op_errno, PS_MSG_READ_INFO,
                        "%"PRId64": READV %"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_readv (&rsp, stbuf, op_ret);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, vector, count, iobref,
                             (xdrproc_t)xdr_gfx_read_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_rchecksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      uint32_t weak_checksum, uint8_t *strong_checksum,
                      dict_t *xdata)
{
        gfx_rchecksum_rsp   rsp   = {0,};
        rpcsvc_request_t    *req   = NULL;
        server_state_t      *state = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret < 0) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_RCHECKSUM, op_errno),
                        op_errno, PS_MSG_CHKSUM_INFO,
                        "%"PRId64": RCHECKSUM %"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_rchecksum (&rsp, weak_checksum, strong_checksum);
out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_rchecksum_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}


int
server4_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        server_state_t      *state    = NULL;
        rpcsvc_request_t    *req      = NULL;
        gfx_open_rsp        rsp      = {0,};

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret < 0) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_OPEN, op_errno),
                        op_errno, PS_MSG_OPEN_INFO,
                        "%"PRId64": OPEN %s (%s), client: %s, error-xlator: %s",
                        frame->root->unique, state->loc.path,
                        uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        op_ret = server4_post_open (frame, this, &rsp, fd);
        if (op_ret)
                goto out;
out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_open_rsp);
        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}


int
server4_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                   struct iatt *stbuf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        server_state_t      *state      = NULL;
        rpcsvc_request_t    *req        = NULL;
        uint64_t             fd_no      = 0;
        gfx_create_rsp      rsp        = {0,};

        dict_to_xdr (xdata, &rsp.xdata);

        state = CALL_STATE (frame);

        if (op_ret < 0) {
                gf_msg (this->name, GF_LOG_INFO, op_errno, PS_MSG_CREATE_INFO,
                        "%"PRId64": CREATE %s (%s/%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->loc.path, uuid_utoa (state->resolve.pargfid),
                        state->resolve.bname, STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        /* TODO: log gfid too */
        gf_msg_trace (frame->root->client->bound_xl->name, 0, "%"PRId64": "
                      "CREATE %s (%s)", frame->root->unique, state->loc.name,
                      uuid_utoa (stbuf->ia_gfid));

        op_ret = server4_post_create (frame, &rsp, state, this, fd, inode,
                                     stbuf,
                                     preparent, postparent);
        if (op_ret) {
                op_errno = -op_ret;
                op_ret = -1;
                goto out;
        }

out:
        if (op_ret)
                rsp.fd = fd_no;
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_create_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, const char *buf,
                     struct iatt *stbuf, dict_t *xdata)
{
        gfx_readlink_rsp    rsp   = {0,};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret < 0) {
                state = CALL_STATE (frame);
                gf_msg (this->name, GF_LOG_INFO, op_errno, PS_MSG_LINK_INFO,
                        "%"PRId64": READLINK %s (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->loc.path, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_readlink (&rsp, stbuf, buf);
out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);
        if (!rsp.path)
                rsp.path = "";

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_readlink_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                 dict_t *xdata)
{
        gfx_common_iatt_rsp  rsp   = {0,};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        state  = CALL_STATE (frame);
        if (op_ret) {
                gf_msg (this->name, fop_log_level (GF_FOP_STAT, op_errno),
                        op_errno, PS_MSG_STAT_INFO,
                        "%"PRId64": STAT %s (%s), client: %s, error-xlator: %s",
                        frame->root->unique,
                        (state->loc.path) ? state->loc.path : "",
                        uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_common_iatt (state, &rsp, stbuf);
out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}


int
server4_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *statpre, struct iatt *statpost, dict_t *xdata)
{
        gfx_common_2iatt_rsp     rsp   = {0,};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret) {
                state = CALL_STATE (frame);
                gf_msg (this->name, GF_LOG_INFO, op_errno, PS_MSG_SETATTR_INFO,
                        "%"PRId64": SETATTR %s (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        (state->loc.path) ? state->loc.path : "",
                        uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_common_2iatt (&rsp, statpre, statpost);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_2iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *statpre, struct iatt *statpost, dict_t *xdata)
{
        gfx_common_2iatt_rsp    rsp   = {0,};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret) {
                state  = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_FSETATTR, op_errno),
                        op_errno, PS_MSG_SETATTR_INFO,
                        "%"PRId64": FSETATTR %"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_common_2iatt (&rsp, statpre, statpost);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_2iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}


int
server4_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *dict,
                    dict_t *xdata)
{
        gfx_common_dict_rsp  rsp   = {0,};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret < 0) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_XATTROP, op_errno),
                        op_errno, PS_MSG_XATTROP_INFO,
                        "%"PRId64": XATTROP %s (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->loc.path, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        dict_to_xdr (dict, &rsp.dict);
out:
        rsp.op_ret        = op_ret;
        rsp.op_errno      = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_dict_rsp);

        GF_FREE (rsp.dict.pairs.pairs_val);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}


int
server4_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *dict,
                     dict_t *xdata)
{
        gfx_common_dict_rsp     rsp   = {0,};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret < 0) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_FXATTROP, op_errno),
                        op_errno, PS_MSG_XATTROP_INFO,
                        "%"PRId64": FXATTROP %"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        dict_to_xdr (dict, &rsp.dict);

out:
        rsp.op_ret        = op_ret;
        rsp.op_errno      = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_dict_rsp);

        GF_FREE (rsp.dict.pairs.pairs_val);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}


int
server4_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                     dict_t *xdata)
{
        gfx_readdirp_rsp    rsp   = {0,};
        server_state_t      *state = NULL;
        rpcsvc_request_t    *req   = NULL;
        int                  ret   = 0;

        state = CALL_STATE (frame);

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret < 0) {
                state = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_READDIRP, op_errno),
                        op_errno, PS_MSG_DIR_INFO,
                        "%"PRId64": READDIRP %"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        /* (op_ret == 0) is valid, and means EOF */
        if (op_ret) {
                ret = server4_post_readdirp (&rsp, entries);
                if (ret == -1) {
                        op_ret   = -1;
                        op_errno = ENOMEM;
                        goto out;
                }
        }

        gf_link_inodes_from_dirent (this, state->fd->inode, entries);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_readdirp_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        readdirp_rsp_cleanup_v2 (&rsp);

        return 0;
}

int
server4_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *statpre, struct iatt *statpost, dict_t *xdata)
{
        gfx_common_2iatt_rsp rsp   = {0,};
        server_state_t    *state = NULL;
        rpcsvc_request_t  *req   = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret) {
                state  = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_FALLOCATE, op_errno),
                        op_errno, PS_MSG_ALLOC_INFO,
                        "%"PRId64": FALLOCATE %"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_common_2iatt (&rsp, statpre, statpost);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply(frame, req, &rsp, NULL, 0, NULL,
                            (xdrproc_t) xdr_gfx_common_2iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   struct iatt *statpre, struct iatt *statpost, dict_t *xdata)
{
        gfx_common_2iatt_rsp rsp     = {0,};
        server_state_t    *state = NULL;
        rpcsvc_request_t  *req   = NULL;

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret) {
                state  = CALL_STATE (frame);
                gf_msg (this->name, fop_log_level (GF_FOP_DISCARD, op_errno),
                        op_errno, PS_MSG_DISCARD_INFO,
                        "%"PRId64": DISCARD %"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_common_2iatt (&rsp, statpre, statpost);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply(frame, req, &rsp, NULL, 0, NULL,
                            (xdrproc_t) xdr_gfx_common_2iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   struct iatt *statpre, struct iatt *statpost, dict_t *xdata)
{
        gfx_common_2iatt_rsp  rsp    = {0,};
        server_state_t    *state  = NULL;
        rpcsvc_request_t  *req    = NULL;

        req = frame->local;
        state  = CALL_STATE (frame);

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret) {
                gf_msg (this->name, fop_log_level (GF_FOP_ZEROFILL, op_errno),
                        op_errno, PS_MSG_ZEROFILL_INFO,
                        "%"PRId64": ZEROFILL%"PRId64" (%s), client: %s, "
                        "error-xlator: %s",
                        frame->root->unique, state->resolve.fd_no,
                        uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_common_2iatt (&rsp, statpre, statpost);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        server_submit_reply(frame, req, &rsp, NULL, 0, NULL,
                            (xdrproc_t) xdr_gfx_common_2iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}


int
server4_ipc_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        gfx_common_rsp      rsp   = {0,};
        server_state_t    *state = NULL;
        rpcsvc_request_t  *req   = NULL;

        req = frame->local;
        state  = CALL_STATE (frame);

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret) {
                gf_msg (this->name, GF_LOG_INFO, op_errno,
                        PS_MSG_SERVER_IPC_INFO,
                        "%"PRId64": IPC%"PRId64" (%s), client: %s, "
                        "error-xlator: %s",
                        frame->root->unique, state->resolve.fd_no,
                        uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        server_submit_reply(frame, req, &rsp, NULL, 0, NULL,
                            (xdrproc_t) xdr_gfx_common_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}


int
server4_seek_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, off_t offset, dict_t *xdata)
{
        struct gfx_seek_rsp   rsp    = {0, };
        server_state_t        *state  = NULL;
        rpcsvc_request_t      *req    = NULL;

        req = frame->local;
        state  = CALL_STATE (frame);

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret) {
                gf_msg (this->name, fop_log_level (GF_FOP_SEEK, op_errno),
                        op_errno, PS_MSG_SEEK_INFO,
                        "%"PRId64": SEEK%"PRId64" (%s), client: %s, "
                        "error-xlator: %s",
                        frame->root->unique, state->resolve.fd_no,
                        uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        server4_post_seek (&rsp, offset);
out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t) xdr_gfx_seek_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

static int
server4_setactivelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        gfx_common_rsp   rsp     = {0,};
        server_state_t          *state  = NULL;
        rpcsvc_request_t        *req    = NULL;

        state = CALL_STATE (frame);

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret < 0) {
                state = CALL_STATE (frame);
                gf_msg (this->name, GF_LOG_INFO,
                        op_errno, 0,
                        "%"PRId64": SETACTIVELK %s (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->loc.path, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_namelink_cbk (call_frame_t *frame,
                     void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        gfx_common_2iatt_rsp rsp        = {0,};
        rpcsvc_request_t    *req        = NULL;

        dict_to_xdr (xdata, &rsp.xdata);
        if (op_ret < 0)
                goto out;

        gfx_stat_from_iattx (&rsp.prestat, prebuf);
        gfx_stat_from_iattx (&rsp.poststat, postbuf);

        /**
         * no point in linking inode here -- there's no stbuf anyway and a
         * lookup() for this name entry would be a negative lookup.
         */

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_2iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_icreate_cbk (call_frame_t *frame,
                    void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    inode_t *inode, struct iatt *stbuf, dict_t *xdata)
{
        server_state_t      *state      = NULL;
        inode_t             *link_inode = NULL;
        rpcsvc_request_t    *req        = NULL;
        gfx_common_iatt_rsp  rsp        = {0,};

        dict_to_xdr (xdata, &rsp.xdata);
        state = CALL_STATE (frame);

        if (op_ret < 0) {
                gf_msg (this->name, GF_LOG_INFO, op_errno, PS_MSG_CREATE_INFO,
                        "%"PRId64": ICREATE [%s] ==> (%s)",
                        frame->root->unique, uuid_utoa (state->resolve.gfid),
                        strerror (op_errno));
                goto out;
        }

        gf_msg_trace (frame->root->client->bound_xl->name, 0, "%"PRId64": "
                      "ICREATE [%s]", frame->root->unique,
                      uuid_utoa (stbuf->ia_gfid));

        link_inode = inode_link (inode,
                                 state->loc.parent, state->loc.name, stbuf);

        if (!link_inode) {
                op_ret = -1;
                op_errno = ENOENT;
                goto out;
        }

        inode_lookup (link_inode);
        inode_unref (link_inode);

        gfx_stat_from_iattx (&rsp.stat, stbuf);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_put_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, inode_t *inode,
                struct iatt *stbuf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        server_state_t      *state      = NULL;
        rpcsvc_request_t    *req        = NULL;
        gfx_common_3iatt_rsp rsp        = {0,};

        dict_to_xdr (xdata, &rsp.xdata);

        state = CALL_STATE (frame);

        if (op_ret < 0) {
                gf_msg (this->name, GF_LOG_INFO, op_errno, PS_MSG_PUT_INFO,
                        "%"PRId64": PUT %s (%s/%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->loc.path, uuid_utoa (state->resolve.pargfid),
                        state->resolve.bname, STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
                goto out;
        }

        gf_msg_trace (frame->root->client->bound_xl->name, 0, "%"PRId64": "
                      "PUT %s (%s)", frame->root->unique, state->loc.name,
                      uuid_utoa (stbuf->ia_gfid));

        server4_post_common_3iatt (state, &rsp, inode, stbuf, preparent,
                                   postparent);

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;
        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_3iatt_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

/* Resume function section */

int
server4_rchecksum_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state    = NULL;
        int             op_ret   = 0;
        int             op_errno = EINVAL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0) {
                op_ret   = state->resolve.op_ret;
                op_errno = state->resolve.op_errno;
                goto err;
        }

        STACK_WIND (frame, server4_rchecksum_cbk, bound_xl,
                    bound_xl->fops->rchecksum, state->fd,
                    state->offset, state->size, state->xdata);

        return 0;
err:
        server4_rchecksum_cbk (frame, NULL, frame->this, op_ret, op_errno, 0,
                              NULL, NULL);

        return 0;

}

int
server4_lease_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_lease_cbk, bound_xl, bound_xl->fops->lease,
                    &state->loc, &state->lease, state->xdata);

        return 0;

err:
        server4_lease_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, NULL);
        return 0;
}

int
server4_put_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        state->loc.inode = inode_new (state->itable);

        STACK_WIND (frame, server4_put_cbk,
                    bound_xl, bound_xl->fops->put,
                    &(state->loc), state->mode, state->umask, state->flags,
                    state->payload_vector, state->payload_count, state->offset,
                    state->iobref, state->dict, state->xdata);

        return 0;
err:
        server4_put_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                        state->resolve.op_errno, NULL, NULL, NULL,
                        NULL, NULL);
        return 0;
}

int
server4_lk_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_lk_cbk, bound_xl, bound_xl->fops->lk,
                    state->fd, state->cmd, &state->flock, state->xdata);

        return 0;

err:
        server4_lk_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                       state->resolve.op_errno, NULL, NULL);
        return 0;
}

int
server4_rename_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;
        int             op_ret = 0;
        int             op_errno = 0;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0) {
                op_ret   = state->resolve.op_ret;
                op_errno = state->resolve.op_errno;
                goto err;
        }

        if (state->resolve2.op_ret != 0) {
                op_ret   = state->resolve2.op_ret;
                op_errno = state->resolve2.op_errno;
                goto err;
        }

        STACK_WIND (frame, server4_rename_cbk,
                    bound_xl, bound_xl->fops->rename,
                    &state->loc, &state->loc2, state->xdata);
        return 0;
err:
        server4_rename_cbk (frame, NULL, frame->this, op_ret, op_errno,
                           NULL, NULL, NULL, NULL, NULL, NULL);
        return 0;
}


int
server4_link_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;
        int             op_ret = 0;
        int             op_errno = 0;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0) {
                op_ret   = state->resolve.op_ret;
                op_errno = state->resolve.op_errno;
                goto err;
        }

        if (state->resolve2.op_ret != 0) {
                op_ret   = state->resolve2.op_ret;
                op_errno = state->resolve2.op_errno;
                goto err;
        }

        state->loc2.inode = inode_ref (state->loc.inode);

        STACK_WIND (frame, server4_link_cbk, bound_xl, bound_xl->fops->link,
                    &state->loc, &state->loc2, state->xdata);

        return 0;
err:
        server4_link_cbk (frame, NULL, frame->this, op_ret, op_errno,
                         NULL, NULL, NULL, NULL, NULL);
        return 0;
}

int
server4_symlink_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        state->loc.inode = inode_new (state->itable);

        STACK_WIND (frame, server4_symlink_cbk,
                    bound_xl, bound_xl->fops->symlink,
                    state->name, &state->loc, state->umask, state->xdata);

        return 0;
err:
        server4_symlink_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL, NULL, NULL, NULL, NULL);
        return 0;
}


int
server4_access_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_access_cbk,
                    bound_xl, bound_xl->fops->access,
                    &state->loc, state->mask, state->xdata);
        return 0;
err:
        server4_access_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL);
        return 0;
}

int
server4_fentrylk_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        GF_UNUSED  int  ret   = -1;
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        if (!state->xdata)
                state->xdata = dict_new ();

        if (state->xdata)
                ret = dict_set_str (state->xdata, "connection-id",
                                    frame->root->client->client_uid);

        STACK_WIND (frame, server4_fentrylk_cbk, bound_xl,
                    bound_xl->fops->fentrylk,
                    state->volume, state->fd, state->name,
                    state->cmd, state->type, state->xdata);

        return 0;
err:
        server4_fentrylk_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL);
        return 0;
}


int
server4_entrylk_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        GF_UNUSED int   ret   = -1;
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        if (!state->xdata)
                state->xdata = dict_new ();

        if (state->xdata)
                ret = dict_set_str (state->xdata, "connection-id",
                                    frame->root->client->client_uid);

        STACK_WIND (frame, server4_entrylk_cbk,
                    bound_xl, bound_xl->fops->entrylk,
                    state->volume, &state->loc, state->name,
                    state->cmd, state->type, state->xdata);
        return 0;
err:
        server4_entrylk_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL);
        return 0;
}


int
server4_finodelk_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        GF_UNUSED int   ret   = -1;
        server_state_t *state = NULL;

        gf_msg_debug (bound_xl->name, 0, "frame %p, xlator %p", frame,
                      bound_xl);

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        if (!state->xdata)
                state->xdata = dict_new ();

        if (state->xdata)
                ret = dict_set_str (state->xdata, "connection-id",
                                    frame->root->client->client_uid);

        STACK_WIND (frame, server4_finodelk_cbk, bound_xl,
                    bound_xl->fops->finodelk, state->volume, state->fd,
                    state->cmd, &state->flock, state->xdata);

        return 0;
err:
        server4_finodelk_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL);

        return 0;
}

int
server4_inodelk_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        GF_UNUSED int   ret   = -1;
        server_state_t *state = NULL;

        gf_msg_debug (bound_xl->name, 0, "frame %p, xlator %p", frame,
                      bound_xl);

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        if (!state->xdata)
                state->xdata = dict_new ();

        if (state->xdata)
                ret = dict_set_str (state->xdata, "connection-id",
                                    frame->root->client->client_uid);

        STACK_WIND (frame, server4_inodelk_cbk, bound_xl,
                    bound_xl->fops->inodelk, state->volume, &state->loc,
                    state->cmd, &state->flock, state->xdata);
        return 0;
err:
        server4_inodelk_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL);
        return 0;
}

int
server4_rmdir_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_rmdir_cbk, bound_xl, bound_xl->fops->rmdir,
                    &state->loc, state->flags, state->xdata);
        return 0;
err:
        server4_rmdir_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, NULL, NULL);
        return 0;
}

int
server4_mkdir_resume (call_frame_t *frame, xlator_t *bound_xl)

{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        state->loc.inode = inode_new (state->itable);

        STACK_WIND (frame, server4_mkdir_cbk,
                    bound_xl, bound_xl->fops->mkdir,
                    &(state->loc), state->mode, state->umask, state->xdata);

        return 0;
err:
        server4_mkdir_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, NULL, NULL, NULL, NULL);
        return 0;
}


int
server4_mknod_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        state->loc.inode = inode_new (state->itable);

        STACK_WIND (frame, server4_mknod_cbk,
                    bound_xl, bound_xl->fops->mknod,
                    &(state->loc), state->mode, state->dev,
                    state->umask, state->xdata);

        return 0;
err:
        server4_mknod_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, NULL, NULL, NULL, NULL);
        return 0;
}


int
server4_fsyncdir_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_fsyncdir_cbk,
                    bound_xl,
                    bound_xl->fops->fsyncdir,
                    state->fd, state->flags, state->xdata);
        return 0;

err:
        server4_fsyncdir_cbk (frame, NULL, frame->this,
                             state->resolve.op_ret,
                             state->resolve.op_errno, NULL);
        return 0;
}


int
server4_readdir_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t  *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        GF_ASSERT (state->fd);

        STACK_WIND (frame, server4_readdir_cbk,
                    bound_xl,
                    bound_xl->fops->readdir,
                    state->fd, state->size, state->offset, state->xdata);

        return 0;
err:
        server4_readdir_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL, NULL);
        return 0;
}

int
server4_readdirp_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t  *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_readdirp_cbk, bound_xl,
                    bound_xl->fops->readdirp, state->fd, state->size,
                    state->offset, state->xdata);

        return 0;
err:
        server4_readdirp_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL, NULL);
        return 0;
}


int
server4_opendir_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t  *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        state->fd = fd_create (state->loc.inode, frame->root->pid);
        if (!state->fd) {
                gf_msg ("server", GF_LOG_ERROR, 0, PS_MSG_FD_CREATE_FAILED,
                        "could not create the fd");
                goto err;
        }

        STACK_WIND (frame, server4_opendir_cbk,
                    bound_xl, bound_xl->fops->opendir,
                    &state->loc, state->fd, state->xdata);
        return 0;
err:
        server4_opendir_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL, NULL);
        return 0;
}


int
server4_statfs_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t      *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_statfs_cbk,
                    bound_xl, bound_xl->fops->statfs,
                    &state->loc, state->xdata);
        return 0;

err:
        server4_statfs_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL, NULL);
        return 0;
}


int
server4_removexattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_removexattr_cbk,
                    bound_xl, bound_xl->fops->removexattr,
                    &state->loc, state->name, state->xdata);
        return 0;
err:
        server4_removexattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                                state->resolve.op_errno, NULL);
        return 0;
}

int
server4_fremovexattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_fremovexattr_cbk,
                    bound_xl, bound_xl->fops->fremovexattr,
                    state->fd, state->name, state->xdata);
        return 0;
err:
        server4_fremovexattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                                 state->resolve.op_errno, NULL);
        return 0;
}

int
server4_fgetxattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_fgetxattr_cbk,
                    bound_xl, bound_xl->fops->fgetxattr,
                    state->fd, state->name, state->xdata);
        return 0;
err:
        server4_fgetxattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                              state->resolve.op_errno, NULL, NULL);
        return 0;
}


int
server4_xattrop_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_xattrop_cbk,
                    bound_xl, bound_xl->fops->xattrop,
                    &state->loc, state->flags, state->dict, state->xdata);
        return 0;
err:
        server4_xattrop_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL, NULL);
        return 0;
}

int
server4_fxattrop_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_fxattrop_cbk,
                    bound_xl, bound_xl->fops->fxattrop,
                    state->fd, state->flags, state->dict, state->xdata);
        return 0;
err:
        server4_fxattrop_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL, NULL);
        return 0;
}

int
server4_fsetxattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_setxattr_cbk,
                    bound_xl, bound_xl->fops->fsetxattr,
                    state->fd, state->dict, state->flags, state->xdata);
        return 0;
err:
        server4_fsetxattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                              state->resolve.op_errno, NULL);

        return 0;
}

int
server4_unlink_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_unlink_cbk,
                    bound_xl, bound_xl->fops->unlink,
                    &state->loc, state->flags, state->xdata);
        return 0;
err:
        server4_unlink_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL, NULL, NULL);
        return 0;
}

int
server4_truncate_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_truncate_cbk,
                    bound_xl, bound_xl->fops->truncate,
                    &state->loc, state->offset, state->xdata);
        return 0;
err:
        server4_truncate_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL, NULL, NULL);
        return 0;
}



int
server4_fstat_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t     *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_fstat_cbk,
                    bound_xl, bound_xl->fops->fstat,
                    state->fd, state->xdata);
        return 0;
err:
        server4_fstat_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, NULL);
        return 0;
}


int
server4_setxattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_setxattr_cbk,
                    bound_xl, bound_xl->fops->setxattr,
                    &state->loc, state->dict, state->flags, state->xdata);
        return 0;
err:
        server4_setxattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL);

        return 0;
}


int
server4_getxattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_getxattr_cbk,
                    bound_xl, bound_xl->fops->getxattr,
                    &state->loc, state->name, state->xdata);
        return 0;
err:
        server4_getxattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL, NULL);
        return 0;
}


int
server4_ftruncate_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_ftruncate_cbk,
                    bound_xl, bound_xl->fops->ftruncate,
                    state->fd, state->offset, state->xdata);
        return 0;
err:
        server4_ftruncate_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                              state->resolve.op_errno, NULL, NULL, NULL);

        return 0;
}


int
server4_flush_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_flush_cbk,
                    bound_xl, bound_xl->fops->flush, state->fd, state->xdata);
        return 0;
err:
        server4_flush_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL);

        return 0;
}


int
server4_fsync_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_fsync_cbk,
                    bound_xl, bound_xl->fops->fsync,
                    state->fd, state->flags, state->xdata);
        return 0;
err:
        server4_fsync_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, NULL, NULL);

        return 0;
}

int
server4_writev_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t   *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_writev_cbk,
                    bound_xl, bound_xl->fops->writev,
                    state->fd, state->payload_vector, state->payload_count,
                    state->offset, state->flags, state->iobref, state->xdata);

        return 0;
err:
        server4_writev_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL, NULL, NULL);
        return 0;
}


int
server4_readv_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_readv_cbk,
                    bound_xl, bound_xl->fops->readv,
                    state->fd, state->size, state->offset, state->flags, state->xdata);

        return 0;
err:
        server4_readv_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                          state->resolve.op_errno, NULL, 0, NULL, NULL, NULL);
        return 0;
}


int
server4_create_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        state->loc.inode = inode_new (state->itable);

        state->fd = fd_create (state->loc.inode, frame->root->pid);
        if (!state->fd) {
                gf_msg ("server", GF_LOG_ERROR, 0, PS_MSG_FD_CREATE_FAILED,
                        "fd creation for the inode %s failed",
                        state->loc.inode ?
                        uuid_utoa (state->loc.inode->gfid):NULL);
                state->resolve.op_ret = -1;
                state->resolve.op_errno = ENOMEM;
                goto err;
        }
        state->fd->flags = state->flags;

        STACK_WIND (frame, server4_create_cbk,
                    bound_xl, bound_xl->fops->create,
                    &(state->loc), state->flags, state->mode,
                    state->umask, state->fd, state->xdata);

        return 0;
err:
        server4_create_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL, NULL, NULL,
                           NULL, NULL, NULL);
        return 0;
}


int
server4_open_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t  *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        state->fd = fd_create (state->loc.inode, frame->root->pid);
        state->fd->flags = state->flags;

        STACK_WIND (frame, server4_open_cbk,
                    bound_xl, bound_xl->fops->open,
                    &state->loc, state->flags, state->fd, state->xdata);

        return 0;
err:
        server4_open_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                         state->resolve.op_errno, NULL, NULL);
        return 0;
}


int
server4_readlink_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_readlink_cbk,
                    bound_xl, bound_xl->fops->readlink,
                    &state->loc, state->size, state->xdata);
        return 0;
err:
        server4_readlink_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL, NULL, NULL);
        return 0;
}


int
server4_fsetattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_fsetattr_cbk,
                    bound_xl, bound_xl->fops->fsetattr,
                    state->fd, &state->stbuf, state->valid, state->xdata);
        return 0;
err:
        server4_fsetattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL, NULL, NULL);

        return 0;
}


int
server4_setattr_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_setattr_cbk,
                    bound_xl, bound_xl->fops->setattr,
                    &state->loc, &state->stbuf, state->valid, state->xdata);
        return 0;
err:
        server4_setattr_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL, NULL, NULL);

        return 0;
}


int
server4_stat_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_stat_cbk,
                    bound_xl, bound_xl->fops->stat, &state->loc, state->xdata);
        return 0;
err:
        server4_stat_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                         state->resolve.op_errno, NULL, NULL);
        return 0;
}

int
server4_lookup_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        if (!state->loc.inode)
                state->loc.inode = server_inode_new (state->itable,
                                                     state->loc.gfid);
        else
                state->is_revalidate = 1;

        STACK_WIND (frame, server4_lookup_cbk,
                    bound_xl, bound_xl->fops->lookup,
                    &state->loc, state->xdata);

        return 0;
err:
        server4_lookup_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL, NULL, NULL, NULL);

        return 0;
}

int
server4_fallocate_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_fallocate_cbk,
                    bound_xl, bound_xl->fops->fallocate,
                    state->fd, state->flags, state->offset, state->size,
                    state->xdata);
        return 0;
err:
        server4_fallocate_cbk(frame, NULL, frame->this, state->resolve.op_ret,
                             state->resolve.op_errno, NULL, NULL, NULL);

        return 0;
}

int
server4_discard_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_discard_cbk,
                    bound_xl, bound_xl->fops->discard,
                    state->fd, state->offset, state->size, state->xdata);
        return 0;
err:
        server4_discard_cbk(frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL, NULL, NULL);

        return 0;
}

int
server4_zerofill_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_zerofill_cbk,
                    bound_xl, bound_xl->fops->zerofill,
                    state->fd, state->offset, state->size, state->xdata);
        return 0;
err:
        server4_zerofill_cbk(frame, NULL, frame->this, state->resolve.op_ret,
                           state->resolve.op_errno, NULL, NULL, NULL);

        return 0;
}

int
server4_seek_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_seek_cbk, bound_xl, bound_xl->fops->seek,
                    state->fd, state->offset, state->what, state->xdata);
        return 0;
err:
        server4_seek_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                         state->resolve.op_errno, 0, NULL);

        return 0;
}

static int
server4_getactivelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno,
                        lock_migration_info_t *locklist, dict_t *xdata)
{
        gfx_getactivelk_rsp    rsp     = {0,};
        server_state_t          *state  = NULL;
        rpcsvc_request_t        *req    = NULL;
        int                     ret     = 0;

        state = CALL_STATE (frame);

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret < 0) {
                state = CALL_STATE (frame);

                gf_msg (this->name, GF_LOG_INFO,
                        op_errno, 0,
                        "%"PRId64": GETACTIVELK %s (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->loc.path, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));

                goto out;
        }

        /* (op_ret == 0) means there are no locks on the file*/
        if (op_ret > 0) {
                ret = serialize_rsp_locklist_v2 (locklist, &rsp);
                if (ret == -1) {
                        op_ret   = -1;
                        op_errno = ENOMEM;
                        goto out;
                }
        }

out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        req = frame->local;

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_getactivelk_rsp);

        GF_FREE (rsp.xdata.pairs.pairs_val);

        getactivelkinfo_rsp_cleanup_v2 (&rsp);

        return 0;
}

int
server4_getactivelk_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_getactivelk_cbk, bound_xl,
                    bound_xl->fops->getactivelk, &state->loc, state->xdata);
        return 0;
err:
        server4_getactivelk_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                                state->resolve.op_errno, NULL, NULL);
        return 0;

}

int
server4_setactivelk_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        STACK_WIND (frame, server4_setactivelk_cbk,
                    bound_xl, bound_xl->fops->setactivelk, &state->loc,
                    &state->locklist, state->xdata);
        return 0;
err:
        server4_setactivelk_cbk (frame, NULL, frame->this,
                                 state->resolve.op_ret,
                                 state->resolve.op_errno, NULL);
        return 0;

}
int
server4_namelink_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        state->loc.inode = inode_new (state->itable);

        STACK_WIND (frame, server4_namelink_cbk,
                    bound_xl, bound_xl->fops->namelink,
                    &(state->loc), state->xdata);
        return 0;

 err:
        server4_namelink_cbk (frame, NULL,
                             frame->this,
                             state->resolve.op_ret,
                             state->resolve.op_errno, NULL, NULL, NULL);
        return 0;
}

int
server4_icreate_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t *state = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0)
                goto err;

        state->loc.inode = inode_new (state->itable);

        STACK_WIND (frame, server4_icreate_cbk,
                    bound_xl, bound_xl->fops->icreate,
                    &(state->loc), state->mode, state->xdata);

        return 0;
err:
        server4_icreate_cbk (frame, NULL, frame->this, state->resolve.op_ret,
                            state->resolve.op_errno, NULL, NULL, NULL);
        return 0;
}


int
server4_0_stat (rpcsvc_request_t *req)
{
        server_state_t *state    = NULL;
        call_frame_t   *frame    = NULL;
        gfx_stat_req   args     = {{0,},};
        int             ret      = -1;
        int             op_errno = 0;

        if (!req)
                return 0;

        /* Initialize args first, then decode */
        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_stat_req, GF_FOP_STAT);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_stat_resume);

out:


        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_setattr (rpcsvc_request_t *req)
{
        server_state_t   *state                 = NULL;
        call_frame_t     *frame                 = NULL;
        gfx_setattr_req  args                  = {{0,},};
        int               ret                   = -1;
        int               op_errno = 0;

        if (!req)
                return 0;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_setattr_req, GF_FOP_SETATTR);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        gfx_stat_to_iattx (&args.stbuf, &state->stbuf);
        state->valid = args.valid;

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_setattr_resume);

out:
        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);



        return ret;
}


int
server4_0_fallocate(rpcsvc_request_t *req)
{
        server_state_t    *state = NULL;
        call_frame_t      *frame = NULL;
        gfx_fallocate_req args  = {{0},};
        int                ret   = -1;
        int                op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_fallocate_req, GF_FOP_FALLOCATE);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.fd_no  = args.fd;

        state->flags = args.flags;
        state->offset = args.offset;
        state->size = args.size;
        memcpy(state->resolve.gfid, args.gfid, 16);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_fallocate_resume);

out:


        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_discard(rpcsvc_request_t *req)
{
        server_state_t    *state = NULL;
        call_frame_t      *frame = NULL;
        gfx_discard_req args    = {{0},};
        int                ret   = -1;
        int                op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_discard_req, GF_FOP_DISCARD);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.fd_no  = args.fd;

        state->offset = args.offset;
        state->size = args.size;
        memcpy(state->resolve.gfid, args.gfid, 16);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_discard_resume);

out:


        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_zerofill(rpcsvc_request_t *req)
{
        server_state_t       *state      = NULL;
        call_frame_t         *frame      = NULL;
        gfx_zerofill_req     args       = {{0},};
        int                   ret        = -1;
        int                   op_errno   = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_zerofill_req, GF_FOP_ZEROFILL);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.fd_no  = args.fd;

        state->offset = args.offset;
        state->size = args.size;
        memcpy(state->resolve.gfid, args.gfid, 16);

        xdr_to_dict (&args.xdata, &state->xdata);
        ret = 0;
        resolve_and_resume (frame, server4_zerofill_resume);

out:


        if (op_errno)
                req->rpc_err = GARBAGE_ARGS;

        return ret;
}

int
server4_0_ipc (rpcsvc_request_t *req)
{
        server_state_t  *state          = NULL;
        call_frame_t    *frame          = NULL;
        gfx_ipc_req     args           = {0,};
        int              ret            = -1;
        int              op_errno       = 0;
        xlator_t        *bound_xl       = NULL;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_ipc_req, GF_FOP_IPC);
        if (ret != 0) {
                goto out;
        }

        bound_xl = frame->root->client->bound_xl;
        xdr_to_dict (&args.xdata, &state->xdata);
        ret = 0;
        STACK_WIND (frame, server4_ipc_cbk, bound_xl, bound_xl->fops->ipc,
                    args.op, state->xdata);

out:


        if (op_errno)
                req->rpc_err = GARBAGE_ARGS;

        return ret;
}

int
server4_0_seek (rpcsvc_request_t *req)
{
        server_state_t        *state          = NULL;
        call_frame_t          *frame          = NULL;
        gfx_seek_req           args           = {{0,},};
        int                    ret            = -1;
        int                    op_errno       = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_seek_req, GF_FOP_SEEK);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.fd_no  = args.fd;

        state->offset = args.offset;
        state->what = args.what;
        memcpy(state->resolve.gfid, args.gfid, 16);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_seek_resume);

out:
        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}

int
server4_0_readlink (rpcsvc_request_t *req)
{
        server_state_t    *state                 = NULL;
        call_frame_t      *frame                 = NULL;
        gfx_readlink_req  args                  = {{0,},};
        int                ret                   = -1;
        int                op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_readlink_req, GF_FOP_READLINK);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type = RESOLVE_MUST;
        memcpy (state->resolve.gfid, args.gfid, 16);

        state->size  = args.size;

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_readlink_resume);

out:


        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_create (rpcsvc_request_t *req)
{
        server_state_t  *state    = NULL;
        call_frame_t    *frame    = NULL;
        gfx_create_req  args     = {{0,},};
        int              ret      = -1;
        int              op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_create_req, GF_FOP_CREATE);
        if (ret != 0) {
                goto out;
        }

        state->resolve.bname  = gf_strdup (args.bname);
        state->mode           = args.mode;
        state->umask          = args.umask;
        state->flags          = gf_flags_to_flags (args.flags);

        set_resolve_gfid (frame->root->client, state->resolve.pargfid,
                          args.pargfid);

        if (state->flags & O_EXCL) {
                state->resolve.type = RESOLVE_NOT;
        } else {
                state->resolve.type = RESOLVE_DONTCARE;
        }

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_create_resume);

out:
        free (args.bname);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_open (rpcsvc_request_t *req)
{
        server_state_t *state                 = NULL;
        call_frame_t   *frame                 = NULL;
        gfx_open_req   args                  = {{0,},};
        int             ret                   = -1;
        int             op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_open_req, GF_FOP_OPEN);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        memcpy (state->resolve.gfid, args.gfid, 16);

        state->flags = gf_flags_to_flags (args.flags);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_open_resume);
out:
        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);



        return ret;
}


int
server4_0_readv (rpcsvc_request_t *req)
{
        server_state_t *state = NULL;
        call_frame_t   *frame = NULL;
        gfx_read_req   args  = {{0,},};
        int             ret   = -1;
        int             op_errno = 0;

        if (!req)
                goto out;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_read_req, GF_FOP_READ);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.fd_no  = args.fd;
        state->size           = args.size;
        state->offset         = args.offset;
        state->flags          = args.flag;

        memcpy (state->resolve.gfid, args.gfid, 16);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_readv_resume);
out:
        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_writev (rpcsvc_request_t *req)
{
        server_state_t      *state  = NULL;
        call_frame_t        *frame  = NULL;
        gfx_write_req       args   = {{0,},};
        ssize_t              len    = 0;
        int                  i      = 0;
        int                  ret    = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, &len, &args,
                                  xdr_gfx_write_req, GF_FOP_WRITE);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.fd_no = args.fd;
        state->offset        = args.offset;
        state->size          = args.size;
        state->flags         = args.flag;
        state->iobref        = iobref_ref (req->iobref);
        memcpy (state->resolve.gfid, args.gfid, 16);

        if (len < req->msg[0].iov_len) {
                state->payload_vector[0].iov_base
                        = (req->msg[0].iov_base + len);
                state->payload_vector[0].iov_len
                        = req->msg[0].iov_len - len;
                state->payload_count = 1;
        }

        for (i = 1; i < req->count; i++) {
                state->payload_vector[state->payload_count++]
                        = req->msg[i];
        }

        len = iov_length (state->payload_vector, state->payload_count);

        GF_ASSERT (state->size == len);

        xdr_to_dict (&args.xdata, &state->xdata);

#ifdef GF_TESTING_IO_XDATA
        dict_dump_to_log (state->xdata);
#endif

        ret = 0;
        resolve_and_resume (frame, server4_writev_resume);
out:


        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


#define SERVER4_0_VECWRITE_START 0
#define SERVER4_0_VECWRITE_READING_HDR 1
#define SERVER4_0_VECWRITE_READING_OPAQUE 2

int
server4_0_writev_vecsizer (int state, ssize_t *readsize, char *base_addr,
                           char *curr_addr)
{
        ssize_t         size = 0;
        int             nextstate = 0;
        gfx_write_req   write_req = {{0,},};
        XDR             xdr;

        switch (state) {
        case SERVER4_0_VECWRITE_START:
                size = xdr_sizeof ((xdrproc_t) xdr_gfx_write_req,
                                   &write_req);
                *readsize = size;

                nextstate = SERVER4_0_VECWRITE_READING_HDR;
                break;
        case SERVER4_0_VECWRITE_READING_HDR:
                size = xdr_sizeof ((xdrproc_t) xdr_gfx_write_req,
                                           &write_req);

                xdrmem_create (&xdr, base_addr, size, XDR_DECODE);

                /* This will fail if there is xdata sent from client, if not,
                   well and good */
                xdr_gfx_write_req (&xdr, &write_req);

                /* need to round off to proper roof (%4), as XDR packing pads
                   the end of opaque object with '0' */
                size = roof (write_req.xdata.xdr_size, 4);

                *readsize = size;

                if (!size)
                        nextstate = SERVER4_0_VECWRITE_START;
                else
                        nextstate = SERVER4_0_VECWRITE_READING_OPAQUE;

                free (write_req.xdata.pairs.pairs_val);

                break;

        case SERVER4_0_VECWRITE_READING_OPAQUE:
                *readsize = 0;
                nextstate = SERVER4_0_VECWRITE_START;
                break;
        }

        return nextstate;
}


int
server4_0_release (rpcsvc_request_t *req)
{
        client_t         *client   = NULL;
        server_ctx_t     *serv_ctx = NULL;
        gfx_release_req  args     = {{0,},};
        gfx_common_rsp     rsp      = {0,};
        int               ret      = -1;

        ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gfx_release_req);
        if (ret < 0) {
                SERVER_REQ_SET_ERROR (req, ret);
                goto out;
        }

        client = req->trans->xl_private;
        if (!client) {
                /* Handshake is not complete yet. */
                req->rpc_err = SYSTEM_ERR;
                goto out;
        }

        serv_ctx = server_ctx_get (client, client->this);
        if (serv_ctx == NULL) {
                gf_msg (req->trans->name, GF_LOG_INFO, 0,
                        PS_MSG_SERVER_CTX_GET_FAILED, "server_ctx_get() "
                        "failed");
                req->rpc_err = SYSTEM_ERR;
                goto out;
        }

        gf_fd_put (serv_ctx->fdtable, args.fd);

        server_submit_reply (NULL, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_rsp);

        ret = 0;
out:
        return ret;
}

int
server4_0_releasedir (rpcsvc_request_t *req)
{
        client_t            *client   = NULL;
        server_ctx_t        *serv_ctx = NULL;
        gfx_releasedir_req  args     = {{0,},};
        gfx_common_rsp        rsp      = {0,};
        int                  ret      = -1;

        ret = xdr_to_generic (req->msg[0], &args,
                              (xdrproc_t)xdr_gfx_release_req);
        if (ret < 0) {
                SERVER_REQ_SET_ERROR (req, ret);
                goto out;
        }

        client = req->trans->xl_private;
        if (!client) {
                SERVER_REQ_SET_ERROR (req, ret);
                goto out;
        }

        serv_ctx = server_ctx_get (client, client->this);
        if (serv_ctx == NULL) {
                gf_msg (req->trans->name, GF_LOG_INFO, 0,
                        PS_MSG_SERVER_CTX_GET_FAILED, "server_ctx_get() "
                        "failed");
                req->rpc_err = SYSTEM_ERR;
                goto out;
        }

        gf_fd_put (serv_ctx->fdtable, args.fd);

        server_submit_reply (NULL, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_rsp);

        ret = 0;
out:
        return ret;
}


int
server4_0_fsync (rpcsvc_request_t *req)
{
        server_state_t *state = NULL;
        call_frame_t   *frame = NULL;
        gfx_fsync_req  args  = {{0,},};
        int             ret   = -1;
        int             op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_fsync_req, GF_FOP_FSYNC);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.fd_no = args.fd;
        state->flags         = args.data;
        memcpy (state->resolve.gfid, args.gfid, 16);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_fsync_resume);
out:


        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}



int
server4_0_flush (rpcsvc_request_t *req)
{
        server_state_t *state = NULL;
        call_frame_t   *frame = NULL;
        gfx_flush_req  args  = {{0,},};
        int             ret   = -1;
        int             op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_flush_req, GF_FOP_FLUSH);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.fd_no = args.fd;
        memcpy (state->resolve.gfid, args.gfid, 16);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_flush_resume);
out:


        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}



int
server4_0_ftruncate (rpcsvc_request_t *req)
{
        server_state_t     *state = NULL;
        call_frame_t       *frame = NULL;
        gfx_ftruncate_req  args  = {{0,},};
        int                 ret   = -1;
        int                 op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_ftruncate_req, GF_FOP_FTRUNCATE);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.fd_no  = args.fd;
        state->offset         = args.offset;
        memcpy (state->resolve.gfid, args.gfid, 16);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_ftruncate_resume);
out:

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_fstat (rpcsvc_request_t *req)
{
        server_state_t *state = NULL;
        call_frame_t   *frame = NULL;
        gfx_fstat_req  args  = {{0,},};
        int             ret   = -1;
        int             op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_fstat_req, GF_FOP_FSTAT);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type    = RESOLVE_MUST;
        state->resolve.fd_no   = args.fd;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_fstat_resume);
out:

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_truncate (rpcsvc_request_t *req)
{
        server_state_t    *state                 = NULL;
        call_frame_t      *frame                 = NULL;
        gfx_truncate_req  args                  = {{0,},};
        int                ret                   = -1;
        int                op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_truncate_req, GF_FOP_TRUNCATE);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        memcpy (state->resolve.gfid, args.gfid, 16);
        state->offset        = args.offset;

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_truncate_resume);
out:

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}



int
server4_0_unlink (rpcsvc_request_t *req)
{
        server_state_t  *state                  = NULL;
        call_frame_t    *frame                  = NULL;
        gfx_unlink_req  args                   = {{0,},};
        int              ret                    = -1;
        int              op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_unlink_req, GF_FOP_UNLINK);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.bname  = gf_strdup (args.bname);

        set_resolve_gfid (frame->root->client, state->resolve.pargfid,
                          args.pargfid);

        state->flags = args.xflags;

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_unlink_resume);
out:
        free (args.bname);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_setxattr (rpcsvc_request_t *req)
{
        server_state_t      *state                 = NULL;
        call_frame_t        *frame                 = NULL;
        gfx_setxattr_req    args                  = {{0,},};
        int32_t              ret                   = -1;
        int32_t              op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_setxattr_req, GF_FOP_SETXATTR);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type     = RESOLVE_MUST;
        state->flags            = args.flags;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        ret = xdr_to_dict (&args.dict, &state->dict);
        if (ret)
                gf_msg_debug (THIS->name, EINVAL,
                              "dictionary not received");

        /* There can be some commands hidden in key, check and proceed */
        gf_server_check_setxattr_cmd (frame, state->dict);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_setxattr_resume);

out:
        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}



int
server4_0_fsetxattr (rpcsvc_request_t *req)
{
        server_state_t      *state                = NULL;
        call_frame_t        *frame                = NULL;
        gfx_fsetxattr_req   args                 = {{0,},};
        int32_t              ret                  = -1;
        int32_t              op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_fsetxattr_req, GF_FOP_FSETXATTR);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type      = RESOLVE_MUST;
        state->resolve.fd_no     = args.fd;
        state->flags             = args.flags;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        ret = xdr_to_dict (&args.dict, &state->dict);
        if (ret)
                gf_msg_debug (THIS->name, EINVAL,
                              "dictionary not received");

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_fsetxattr_resume);

out:
        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}



int
server4_0_fxattrop (rpcsvc_request_t *req)
{
        server_state_t      *state                = NULL;
        call_frame_t        *frame                = NULL;
        gfx_fxattrop_req    args                 = {{0,},};
        int32_t              ret                  = -1;
        int32_t              op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_fxattrop_req, GF_FOP_FXATTROP);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type    = RESOLVE_MUST;
        state->resolve.fd_no   = args.fd;
        state->flags           = args.flags;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        ret = xdr_to_dict (&args.dict, &state->dict);
        if (ret)
                gf_msg_debug (THIS->name, EINVAL,
                              "dictionary not received");

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_fxattrop_resume);

out:
        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}

int
server4_0_xattrop (rpcsvc_request_t *req)
{
        server_state_t      *state                 = NULL;
        call_frame_t        *frame                 = NULL;
        gfx_xattrop_req     args                  = {{0,},};
        int32_t              ret                   = -1;
        int32_t              op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_xattrop_req, GF_FOP_XATTROP);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type    = RESOLVE_MUST;
        state->flags           = args.flags;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        ret = xdr_to_dict (&args.dict, &state->dict);
        if (ret)
                gf_msg_debug (THIS->name, EINVAL,
                              "dictionary not received");

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_xattrop_resume);

out:
        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_getxattr (rpcsvc_request_t *req)
{
        server_state_t      *state                 = NULL;
        call_frame_t        *frame                 = NULL;
        gfx_getxattr_req    args                  = {{0,},};
        int                  ret                   = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_getxattr_req, GF_FOP_GETXATTR);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        if (args.namelen) {
                state->name = gf_strdup (args.name);
                /* There can be some commands hidden in key, check and proceed */
                gf_server_check_getxattr_cmd (frame, state->name);
        }

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_getxattr_resume);
out:
        free (args.name);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_fgetxattr (rpcsvc_request_t *req)
{
        server_state_t      *state      = NULL;
        call_frame_t        *frame      = NULL;
        gfx_fgetxattr_req   args       = {{0,},};
        int                  ret        = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_fgetxattr_req, GF_FOP_FGETXATTR);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        state->resolve.fd_no = args.fd;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);
        if (args.namelen)
                state->name = gf_strdup (args.name);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_fgetxattr_resume);
out:
        free (args.name);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}



int
server4_0_removexattr (rpcsvc_request_t *req)
{
        server_state_t       *state                 = NULL;
        call_frame_t         *frame                 = NULL;
        gfx_removexattr_req  args                  = {{0,},};
        int                   ret                   = -1;
        int                   op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_removexattr_req,
                                  GF_FOP_REMOVEXATTR);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);
        state->name           = gf_strdup (args.name);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_removexattr_resume);
out:
        free (args.name);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}

int
server4_0_fremovexattr (rpcsvc_request_t *req)
{
        server_state_t       *state                 = NULL;
        call_frame_t         *frame                 = NULL;
        gfx_fremovexattr_req  args                  = {{0,},};
        int                   ret                   = -1;
        int                   op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_fremovexattr_req,
                                  GF_FOP_FREMOVEXATTR);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.fd_no  = args.fd;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);
        state->name           = gf_strdup (args.name);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_fremovexattr_resume);
out:
        free (args.name);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}




int
server4_0_opendir (rpcsvc_request_t *req)
{
        server_state_t   *state                 = NULL;
        call_frame_t     *frame                 = NULL;
        gfx_opendir_req  args                  = {{0,},};
        int               ret                   = -1;
        int               op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_opendir_req, GF_FOP_OPENDIR);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_opendir_resume);
out:


        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_readdirp (rpcsvc_request_t *req)
{
        server_state_t      *state        = NULL;
        call_frame_t        *frame        = NULL;
        gfx_readdirp_req    args         = {{0,},};
        size_t               headers_size = 0;
        int                  ret          = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_readdirp_req,
                                  GF_FOP_READDIRP);
        if (ret != 0) {
                goto out;
        }

        /* FIXME: this should go away when variable sized iobufs are introduced
         * and transport layer can send msgs bigger than current page-size.
         */
        headers_size = sizeof (struct rpc_msg) + sizeof (gfx_readdir_rsp);
        if ((frame->this->ctx->page_size < args.size)
            || ((frame->this->ctx->page_size - args.size) < headers_size)) {
                state->size = frame->this->ctx->page_size - headers_size;
        } else {
                state->size   = args.size;
        }

        state->resolve.type = RESOLVE_MUST;
        state->resolve.fd_no = args.fd;
        state->offset = args.offset;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        /* here, dict itself works as xdata */
        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_readdirp_resume);
out:
        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}

int
server4_0_readdir (rpcsvc_request_t *req)
{
        server_state_t      *state        = NULL;
        call_frame_t        *frame        = NULL;
        gfx_readdir_req     args         = {{0,},};
        size_t               headers_size = 0;
        int                  ret          = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_readdir_req, GF_FOP_READDIR);
        if (ret != 0) {
                goto out;
        }

        /* FIXME: this should go away when variable sized iobufs are introduced
         * and transport layer can send msgs bigger than current page-size.
         */
        headers_size = sizeof (struct rpc_msg) + sizeof (gfx_readdir_rsp);
        if ((frame->this->ctx->page_size < args.size)
            || ((frame->this->ctx->page_size - args.size) < headers_size)) {
                state->size = frame->this->ctx->page_size - headers_size;
        } else {
                state->size   = args.size;
        }

        state->resolve.type = RESOLVE_MUST;
        state->resolve.fd_no = args.fd;
        state->offset = args.offset;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_readdir_resume);
out:


        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}

int
server4_0_fsyncdir (rpcsvc_request_t *req)
{
        server_state_t      *state = NULL;
        call_frame_t        *frame = NULL;
        gfx_fsyncdir_req    args  = {{0,},};
        int                  ret   = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_fsyncdir_req, GF_FOP_FSYNCDIR);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type = RESOLVE_MUST;
        state->resolve.fd_no = args.fd;
        state->flags = args.data;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_fsyncdir_resume);
out:


        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}



int
server4_0_mknod (rpcsvc_request_t *req)
{
        server_state_t      *state                  = NULL;
        call_frame_t        *frame                  = NULL;
        gfx_mknod_req       args                   = {{0,},};
        int                  ret                    = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_mknod_req, GF_FOP_MKNOD);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type    = RESOLVE_NOT;
        set_resolve_gfid (frame->root->client, state->resolve.pargfid,
                          args.pargfid);

        state->resolve.bname   = gf_strdup (args.bname);

        state->mode  = args.mode;
        state->dev   = args.dev;
        state->umask = args.umask;

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_mknod_resume);

out:
        free (args.bname);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;

}


int
server4_0_mkdir (rpcsvc_request_t *req)
{
        server_state_t      *state                  = NULL;
        call_frame_t        *frame                  = NULL;
        gfx_mkdir_req       args                   = {{0,},};
        int                  ret                    = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_mkdir_req, GF_FOP_MKDIR);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type    = RESOLVE_NOT;
        set_resolve_gfid (frame->root->client, state->resolve.pargfid,
                          args.pargfid);
        state->resolve.bname   = gf_strdup (args.bname);

        state->mode  = args.mode;
        state->umask = args.umask;

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_mkdir_resume);

out:
        free (args.bname);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);



        return ret;
}


int
server4_0_rmdir (rpcsvc_request_t *req)
{
        server_state_t      *state                  = NULL;
        call_frame_t        *frame                  = NULL;
        gfx_rmdir_req       args                   = {{0,},};
        int                  ret                    = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_rmdir_req, GF_FOP_RMDIR);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type    = RESOLVE_MUST;
        set_resolve_gfid (frame->root->client, state->resolve.pargfid,
                          args.pargfid);
        state->resolve.bname   = gf_strdup (args.bname);

        state->flags = args.xflags;

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_rmdir_resume);
out:
        free (args.bname);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}



int
server4_0_inodelk (rpcsvc_request_t *req)
{
        server_state_t      *state                 = NULL;
        call_frame_t        *frame                 = NULL;
        gfx_inodelk_req     args                  = {{0,},};
        int                  cmd                   = 0;
        int                  ret                   = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_inodelk_req, GF_FOP_INODELK);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type    = RESOLVE_EXACT;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        cmd = args.cmd;
        switch (cmd) {
        case GF_LK_GETLK:
                state->cmd = F_GETLK;
                break;
        case GF_LK_SETLK:
                state->cmd = F_SETLK;
                break;
        case GF_LK_SETLKW:
                state->cmd = F_SETLKW;
                break;
        }

        state->type = args.type;
        state->volume = gf_strdup (args.volume);

        gf_proto_flock_to_flock (&args.flock, &state->flock);

        switch (state->type) {
        case GF_LK_F_RDLCK:
                state->flock.l_type = F_RDLCK;
                break;
        case GF_LK_F_WRLCK:
                state->flock.l_type = F_WRLCK;
                break;
        case GF_LK_F_UNLCK:
                state->flock.l_type = F_UNLCK;
                break;
        }

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_inodelk_resume);
out:
        free (args.volume);

        free (args.flock.lk_owner.lk_owner_val);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}

int
server4_0_finodelk (rpcsvc_request_t *req)
{
        server_state_t      *state        = NULL;
        call_frame_t        *frame        = NULL;
        gfx_finodelk_req    args         = {{0,},};
        int                  ret          = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_finodelk_req,
                                  GF_FOP_FINODELK);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type = RESOLVE_EXACT;
        state->volume = gf_strdup (args.volume);
        state->resolve.fd_no = args.fd;
        state->cmd = args.cmd;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        switch (state->cmd) {
        case GF_LK_GETLK:
                state->cmd = F_GETLK;
                break;
        case GF_LK_SETLK:
                state->cmd = F_SETLK;
                break;
        case GF_LK_SETLKW:
                state->cmd = F_SETLKW;
                break;
        }

        state->type = args.type;

        gf_proto_flock_to_flock (&args.flock, &state->flock);

        switch (state->type) {
        case GF_LK_F_RDLCK:
                state->flock.l_type = F_RDLCK;
                break;
        case GF_LK_F_WRLCK:
                state->flock.l_type = F_WRLCK;
                break;
        case GF_LK_F_UNLCK:
                state->flock.l_type = F_UNLCK;
                break;
        }

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_finodelk_resume);
out:
        free (args.volume);

        free (args.flock.lk_owner.lk_owner_val);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_entrylk (rpcsvc_request_t *req)
{
        server_state_t      *state                 = NULL;
        call_frame_t        *frame                 = NULL;
        gfx_entrylk_req     args                  = {{0,},};
        int                  ret                   = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_entrylk_req,
                                  GF_FOP_ENTRYLK);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type   = RESOLVE_EXACT;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        if (args.namelen)
                state->name   = gf_strdup (args.name);
        state->volume         = gf_strdup (args.volume);

        state->cmd            = args.cmd;
        state->type           = args.type;

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_entrylk_resume);
out:
        free (args.volume);
        free (args.name);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}

int
server4_0_fentrylk (rpcsvc_request_t *req)
{
        server_state_t      *state        = NULL;
        call_frame_t        *frame        = NULL;
        gfx_fentrylk_req    args         = {{0,},};
        int                  ret          = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_fentrylk_req,
                                  GF_FOP_FENTRYLK);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type = RESOLVE_EXACT;
        state->resolve.fd_no = args.fd;
        state->cmd  = args.cmd;
        state->type = args.type;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        if (args.namelen)
                state->name = gf_strdup (args.name);
        state->volume = gf_strdup (args.volume);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_fentrylk_resume);
out:
        free (args.volume);
        free (args.name);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}

int
server4_0_access (rpcsvc_request_t *req)
{
        server_state_t      *state                 = NULL;
        call_frame_t        *frame                 = NULL;
        gfx_access_req      args                  = {{0,},};
        int                  ret                   = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_access_req, GF_FOP_ACCESS);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type  = RESOLVE_MUST;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);
        state->mask          = args.mask;

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_access_resume);
out:


        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}



int
server4_0_symlink (rpcsvc_request_t *req)
{
        server_state_t      *state                 = NULL;
        call_frame_t        *frame                 = NULL;
        gfx_symlink_req     args                  = {{0,},};
        int                  ret                   = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_symlink_req, GF_FOP_SYMLINK);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type   = RESOLVE_NOT;
        set_resolve_gfid (frame->root->client, state->resolve.pargfid,
                          args.pargfid);
        state->resolve.bname  = gf_strdup (args.bname);
        state->name           = gf_strdup (args.linkname);
        state->umask          = args.umask;

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_symlink_resume);

out:
        free (args.bname);
        free (args.linkname);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_link (rpcsvc_request_t *req)
{
        server_state_t      *state                     = NULL;
        call_frame_t        *frame                     = NULL;
        gfx_link_req        args                      = {{0,},};
        int                  ret                       = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_link_req, GF_FOP_LINK);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type    = RESOLVE_MUST;
        memcpy (state->resolve.gfid, args.oldgfid, 16);

        state->resolve2.type   = RESOLVE_NOT;
        state->resolve2.bname  = gf_strdup (args.newbname);
        set_resolve_gfid (frame->root->client, state->resolve2.pargfid,
                          args.newgfid);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_link_resume);
out:
        free (args.newbname);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_rename (rpcsvc_request_t *req)
{
        server_state_t      *state                     = NULL;
        call_frame_t        *frame                     = NULL;
        gfx_rename_req      args                      = {{0,},};
        int                  ret                       = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_rename_req, GF_FOP_RENAME);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.bname  = gf_strdup (args.oldbname);
        set_resolve_gfid (frame->root->client, state->resolve.pargfid,
                          args.oldgfid);

        state->resolve2.type  = RESOLVE_MAY;
        state->resolve2.bname = gf_strdup (args.newbname);
        set_resolve_gfid (frame->root->client, state->resolve2.pargfid,
                          args.newgfid);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_rename_resume);
out:
        free (args.oldbname);
        free (args.newbname);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}

int
server4_0_lease (rpcsvc_request_t *req)
{
        server_state_t      *state = NULL;
        call_frame_t        *frame = NULL;
        gfx_lease_req       args  = {{0,},};
        int                  ret   = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_lease_req, GF_FOP_LEASE);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type = RESOLVE_MUST;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);
        gf_proto_lease_to_lease (&args.lease, &state->lease);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_lease_resume);
out:


        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}

int
server4_0_lk (rpcsvc_request_t *req)
{
        server_state_t      *state = NULL;
        call_frame_t        *frame = NULL;
        gfx_lk_req          args  = {{0,},};
        int                  ret   = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_lk_req, GF_FOP_LK);
        if (ret != 0) {
                goto out;
        }

        state->resolve.fd_no = args.fd;
        state->cmd =  args.cmd;
        state->type = args.type;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        switch (state->cmd) {
        case GF_LK_GETLK:
                state->cmd = F_GETLK;
                break;
        case GF_LK_SETLK:
                state->cmd = F_SETLK;
                break;
        case GF_LK_SETLKW:
                state->cmd = F_SETLKW;
                break;
        case GF_LK_RESLK_LCK:
                state->cmd = F_RESLK_LCK;
                break;
        case GF_LK_RESLK_LCKW:
                state->cmd = F_RESLK_LCKW;
                break;
        case GF_LK_RESLK_UNLCK:
                state->cmd = F_RESLK_UNLCK;
                break;
        case GF_LK_GETLK_FD:
                state->cmd = F_GETLK_FD;
                break;

        }


        gf_proto_flock_to_flock (&args.flock, &state->flock);

        switch (state->type) {
        case GF_LK_F_RDLCK:
                state->flock.l_type = F_RDLCK;
                break;
        case GF_LK_F_WRLCK:
                state->flock.l_type = F_WRLCK;
                break;
        case GF_LK_F_UNLCK:
                state->flock.l_type = F_UNLCK;
                break;
        default:
                gf_msg (frame->root->client->bound_xl->name, GF_LOG_ERROR,
                        0, PS_MSG_LOCK_ERROR, "fd - %"PRId64" (%s): Unknown "
                        "lock type: %"PRId32"!", state->resolve.fd_no,
                        uuid_utoa (state->fd->inode->gfid), state->type);
                break;
        }


        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_lk_resume);
out:


        free (args.flock.lk_owner.lk_owner_val);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_null (rpcsvc_request_t *req)
{
        gfx_common_rsp rsp = {0,};

        /* Accepted */
        rsp.op_ret = 0;

        server_submit_reply (NULL, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t)xdr_gfx_common_rsp);

        return 0;
}

int
server4_0_lookup (rpcsvc_request_t *req)
{
        call_frame_t        *frame    = NULL;
        server_state_t      *state    = NULL;
        gfx_lookup_req      args     = {{0,},};
        int                  ret      = -1;

        GF_VALIDATE_OR_GOTO ("server", req, err);

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_lookup_req, GF_FOP_LOOKUP);
        if (ret != 0) {
                goto err;
        }

        state->resolve.type   = RESOLVE_DONTCARE;

        if (args.bname && strcmp (args.bname, "")) {
                set_resolve_gfid (frame->root->client, state->resolve.pargfid,
                                  args.pargfid);
                state->resolve.bname = gf_strdup (args.bname);
        } else {
                set_resolve_gfid (frame->root->client,
                                  state->resolve.gfid, args.gfid);
        }

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_lookup_resume);

err:
        free (args.bname);

        return ret;
}

int
server4_0_statfs (rpcsvc_request_t *req)
{
        server_state_t      *state = NULL;
        call_frame_t        *frame = NULL;
        gfx_statfs_req      args  = {{0,},};
        int                  ret   = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_statfs_req, GF_FOP_STATFS);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_statfs_resume);
out:

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}

int
server4_0_getactivelk (rpcsvc_request_t *req)
{
        server_state_t          *state          = NULL;
        call_frame_t            *frame          = NULL;
        gfx_getactivelk_req    args            = {{0,},};
        int                     ret             = -1;
        int                     op_errno        = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_getactivelk_req,
                                  GF_FOP_GETACTIVELK);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type = RESOLVE_MUST;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        /* here, dict itself works as xdata */
        xdr_to_dict (&args.xdata, &state->xdata);


        ret = 0;
        resolve_and_resume (frame, server4_getactivelk_resume);
out:


        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


int
server4_0_setactivelk (rpcsvc_request_t *req)
{
        server_state_t          *state          = NULL;
        call_frame_t            *frame          = NULL;
        gfx_setactivelk_req   args            = {{0,},};
        int                     ret             = -1;
        int                     op_errno        = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_setactivelk_req,
                                  GF_FOP_SETACTIVELK);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type = RESOLVE_MUST;
        set_resolve_gfid (frame->root->client, state->resolve.gfid, args.gfid);

        /* here, dict itself works as xdata */
        xdr_to_dict (&args.xdata, &state->xdata);

        ret = unserialize_req_locklist_v2 (&args, &state->locklist);
        if (ret)
                goto out;

        ret = 0;

        resolve_and_resume (frame, server4_setactivelk_resume);
out:
        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}

int
server4_0_namelink (rpcsvc_request_t *req)
{
        server_state_t    *state    = NULL;
        call_frame_t      *frame    = NULL;
        gfx_namelink_req  args     = {{0,},};
        int                ret      = -1;
        int                op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_namelink_req, GF_FOP_NAMELINK);

        if (ret != 0)
                goto out;

        state->resolve.bname = gf_strdup (args.bname);
        memcpy (state->resolve.pargfid, args.pargfid, sizeof (uuid_t));

        state->resolve.type = RESOLVE_NOT;

        xdr_to_dict (&args.xdata, &state->xdata);
        ret = 0;
        resolve_and_resume (frame, server4_namelink_resume);

out:
        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;

}

int
server4_0_icreate (rpcsvc_request_t *req)
{
        server_state_t   *state    = NULL;
        call_frame_t     *frame    = NULL;
        gfx_icreate_req  args     = {{0,},};
        int               ret      = -1;
        int               op_errno = 0;
        uuid_t            gfid     = {0,};

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_icreate_req, GF_FOP_ICREATE);

        if (ret != 0)
                goto out;

        memcpy (gfid, args.gfid, sizeof (uuid_t));

        state->mode  = args.mode;
        gf_asprintf (&state->resolve.bname, INODE_PATH_FMT, uuid_utoa (gfid));

        /* parent is an auxillary inode number */
        memset (state->resolve.pargfid, 0, sizeof (uuid_t));
        state->resolve.pargfid[15] = GF_AUXILLARY_PARGFID;

        state->resolve.type = RESOLVE_NOT;

        xdr_to_dict (&args.xdata, &state->xdata);
        ret = 0;
        resolve_and_resume (frame, server4_icreate_resume);

out:
        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}

int
server4_0_fsetattr (rpcsvc_request_t *req)
{
        server_state_t       *state    = NULL;
        call_frame_t         *frame    = NULL;
        gfx_fsetattr_req      args     = {{0},};
        int                   ret      = -1;
        int                   op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_fsetattr_req,
                                  GF_FOP_FSETATTR);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type   = RESOLVE_MUST;
        state->resolve.fd_no  = args.fd;
        memcpy (state->resolve.gfid, args.gfid, 16);

        gfx_stat_to_iattx (&args.stbuf, &state->stbuf);
        state->valid = args.valid;

        xdr_to_dict (&args.xdata, &state->xdata);
        ret = 0;
        resolve_and_resume (frame, server4_fsetattr_resume);

out:
        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}

int
server4_0_rchecksum (rpcsvc_request_t *req)
{
        server_state_t        *state    = NULL;
        call_frame_t          *frame    = NULL;
        gfx_rchecksum_req      args     = {{0},};
        int                    ret      = -1;
        int                    op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, NULL, &args,
                                  xdr_gfx_rchecksum_req,
                                  GF_FOP_RCHECKSUM);
        if (ret != 0) {
                goto out;
        }

        state->resolve.type  = RESOLVE_MAY;
        state->resolve.fd_no = args.fd;
        state->offset        = args.offset;
        state->size          = args.len;

        memcpy (state->resolve.gfid, args.gfid, 16);

        xdr_to_dict (&args.xdata, &state->xdata);
        ret = 0;
        resolve_and_resume (frame, server4_rchecksum_resume);
out:
        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}

int
server4_0_put (rpcsvc_request_t *req)
{
        server_state_t  *state    = NULL;
        call_frame_t    *frame    = NULL;
        gfx_put_req      args     = {{0,},};
        int              ret      = -1;
        int              op_errno = 0;
        ssize_t          len      = 0;
        int              i        = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, &len, &args,
                                  xdr_gfx_put_req, GF_FOP_PUT);
        if (ret != 0) {
                goto out;
        }

        state->resolve.bname  = gf_strdup (args.bname);
        state->mode           = args.mode;
        state->umask          = args.umask;
        state->flags          = gf_flags_to_flags (args.flag);
        state->offset         = args.offset;
        state->size           = args.size;
        state->iobref         = iobref_ref (req->iobref);

        if (len < req->msg[0].iov_len) {
                state->payload_vector[0].iov_base
                        = (req->msg[0].iov_base + len);
                state->payload_vector[0].iov_len
                        = req->msg[0].iov_len - len;
                state->payload_count = 1;
        }

        for (i = 1; i < req->count; i++) {
                state->payload_vector[state->payload_count++]
                        = req->msg[i];
        }

        len = iov_length (state->payload_vector, state->payload_count);

        GF_ASSERT (state->size == len);

        set_resolve_gfid (frame->root->client, state->resolve.pargfid,
                          args.pargfid);

        if (state->flags & O_EXCL) {
                state->resolve.type = RESOLVE_NOT;
        } else {
                state->resolve.type = RESOLVE_DONTCARE;
        }

        xdr_to_dict (&args.xattr, &state->dict);
        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_put_resume);

out:
        free (args.bname);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}

int
server4_compound_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, void *data,
                     dict_t *xdata)
{
        struct gfx_compound_rsp   rsp    = {0,};
        server_state_t             *state  = NULL;
        rpcsvc_request_t           *req    = NULL;
        compound_args_cbk_t        *args_cbk = data;
        int                        i       = 0;

        req = frame->local;
        state  = CALL_STATE (frame);

        dict_to_xdr (xdata, &rsp.xdata);

        if (op_ret) {
                gf_msg (this->name, fop_log_level (GF_FOP_COMPOUND, op_errno),
                        op_errno, PS_MSG_COMPOUND_INFO,
                        "%"PRId64": COMPOUND%"PRId64" (%s), client: %s, "
                        "error-xlator: %s", frame->root->unique,
                        state->resolve.fd_no, uuid_utoa (state->resolve.gfid),
                        STACK_CLIENT_NAME (frame->root),
                        STACK_ERR_XL_NAME (frame->root));
        }

        rsp.compound_rsp_array.compound_rsp_array_val = GF_CALLOC
                                                        (args_cbk->fop_length,
                                                         sizeof (compound_rsp_v2),
                                                  gf_server_mt_compound_rsp_t);

        if (!rsp.compound_rsp_array.compound_rsp_array_val) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }
        rsp.compound_rsp_array.compound_rsp_array_len = args_cbk->fop_length;

        for (i = 0; i < args_cbk->fop_length; i++) {
                op_ret = server_populate_compound_response_v2 (this, &rsp,
                                                               frame,
                                                               args_cbk, i);

                if (op_ret) {
                        op_errno = op_ret;
                        op_ret = -1;
                        goto out;
                }
        }
out:
        rsp.op_ret    = op_ret;
        rsp.op_errno  = gf_errno_to_error (op_errno);

        server_submit_reply (frame, req, &rsp, NULL, 0, NULL,
                             (xdrproc_t) xdr_gfx_compound_rsp);

        server_compound_rsp_cleanup_v2 (&rsp, args_cbk);
        GF_FREE (rsp.xdata.pairs.pairs_val);

        return 0;
}

int
server4_compound_resume (call_frame_t *frame, xlator_t *bound_xl)
{
        server_state_t          *state  = NULL;
        gfx_compound_req        *req    = NULL;
        compound_args_t         *args   = NULL;
        int                     i       = 0;
        int                     ret     = -1;
        int                     length  = 0;
        int                     op_errno = ENOMEM;
        compound_req_v2        *c_req  = NULL;

        state = CALL_STATE (frame);

        if (state->resolve.op_ret != 0) {
                ret = state->resolve.op_ret;
                op_errno = state->resolve.op_errno;
                goto err;
        }

        req = &state->req_v2;

        length = req->compound_req_array.compound_req_array_len;
        state->args = compound_fop_alloc (length, req->compound_fop_enum,
                                          state->xdata);
        args = state->args;

        if (!args)
                goto err;

        for (i = 0; i < length; i++) {
                c_req = &req->compound_req_array.compound_req_array_val[i];
                args->enum_list[i] = c_req->fop_enum;

                ret = server_populate_compound_request_v2 (req, frame,
                                                           &args->req_list[i],
                                                           i);

                if (ret) {
                        op_errno = ret;
                        ret = -1;
                        goto err;
                }
        }

        STACK_WIND (frame, server4_compound_cbk,
                    bound_xl, bound_xl->fops->compound,
                    args, state->xdata);

        return 0;
err:
        server4_compound_cbk (frame, NULL, frame->this, ret, op_errno,
                             NULL, NULL);

        return ret;
}
int
server4_0_compound (rpcsvc_request_t *req)
{
        server_state_t      *state    = NULL;
        call_frame_t        *frame    = NULL;
        gfx_compound_req     args     = {0,};
        ssize_t              len      = 0;
        int                  length   = 0;
        int                  i        = 0;
        int                  ret      = -1;
        int                  op_errno = 0;

        if (!req)
                return ret;

        ret = rpc_receive_common (req, &frame, &state, &len, &args,
                                  xdr_gfx_compound_req,
                                  GF_FOP_COMPOUND);
        if (ret != 0) {
                goto out;
        }

        state->req_v2        = args;
        state->iobref        = iobref_ref (req->iobref);

        if (len < req->msg[0].iov_len) {
                state->payload_vector[0].iov_base
                        = (req->msg[0].iov_base + len);
                state->payload_vector[0].iov_len
                        = req->msg[0].iov_len - len;
                state->payload_count = 1;
        }

        for (i = 1; i < req->count; i++) {
                state->payload_vector[state->payload_count++]
                        = req->msg[i];
        }

        state->size = iov_length (state->payload_vector, state->payload_count);

        ret = server_get_compound_resolve_v2 (state, &args);

        if (ret) {
                SERVER_REQ_SET_ERROR (req, ret);
                goto out;
        }

        xdr_to_dict (&args.xdata, &state->xdata);

        ret = 0;
        resolve_and_resume (frame, server4_compound_resume);
out:

        length = args.compound_req_array.compound_req_array_len;
        server_compound_req_cleanup_v2 (&args, length);
        free (args.compound_req_array.compound_req_array_val);

        if (op_errno)
                SERVER_REQ_SET_ERROR (req, ret);

        return ret;
}


rpcsvc_actor_t glusterfs4_0_fop_actors[] = {
        [GFS3_OP_NULL]        = { "NULL",       GFS3_OP_NULL, server_null, NULL, 0},
        [GFS3_OP_STAT]        = { "STAT",       GFS3_OP_STAT, server4_0_stat, NULL, 0},
        [GFS3_OP_READLINK]    = { "READLINK",   GFS3_OP_READLINK, server4_0_readlink, NULL, 0},
        [GFS3_OP_MKNOD]       = { "MKNOD",      GFS3_OP_MKNOD, server4_0_mknod, NULL, 0},
        [GFS3_OP_MKDIR]       = { "MKDIR",      GFS3_OP_MKDIR, server4_0_mkdir, NULL, 0},
        [GFS3_OP_UNLINK]      = { "UNLINK",     GFS3_OP_UNLINK, server4_0_unlink, NULL, 0},
        [GFS3_OP_RMDIR]       = { "RMDIR",      GFS3_OP_RMDIR, server4_0_rmdir, NULL, 0},
        [GFS3_OP_SYMLINK]     = { "SYMLINK",    GFS3_OP_SYMLINK, server4_0_symlink, NULL, 0},
        [GFS3_OP_RENAME]      = { "RENAME",     GFS3_OP_RENAME, server4_0_rename, NULL, 0},
        [GFS3_OP_LINK]        = { "LINK",       GFS3_OP_LINK, server4_0_link, NULL, 0},
        [GFS3_OP_TRUNCATE]    = { "TRUNCATE",   GFS3_OP_TRUNCATE, server4_0_truncate, NULL, 0},
        [GFS3_OP_OPEN]        = { "OPEN",       GFS3_OP_OPEN, server4_0_open, NULL, 0},
        [GFS3_OP_READ]        = { "READ",       GFS3_OP_READ, server4_0_readv, NULL, 0},
        [GFS3_OP_WRITE]       = { "WRITE",      GFS3_OP_WRITE, server4_0_writev, server4_0_writev_vecsizer, 0},
        [GFS3_OP_STATFS]      = { "STATFS",     GFS3_OP_STATFS, server4_0_statfs, NULL, 0},
        [GFS3_OP_FLUSH]       = { "FLUSH",      GFS3_OP_FLUSH, server4_0_flush, NULL, 0},
        [GFS3_OP_FSYNC]       = { "FSYNC",      GFS3_OP_FSYNC, server4_0_fsync, NULL, 0},
        [GFS3_OP_GETXATTR]    = { "GETXATTR",   GFS3_OP_GETXATTR, server4_0_getxattr, NULL, 0},
        [GFS3_OP_SETXATTR]    = { "SETXATTR",   GFS3_OP_SETXATTR, server4_0_setxattr, NULL, 0},
        [GFS3_OP_REMOVEXATTR] = { "REMOVEXATTR", GFS3_OP_REMOVEXATTR, server4_0_removexattr, NULL, 0},
        [GFS3_OP_OPENDIR]     = { "OPENDIR",    GFS3_OP_OPENDIR, server4_0_opendir, NULL, 0},
        [GFS3_OP_FSYNCDIR]    = { "FSYNCDIR",   GFS3_OP_FSYNCDIR, server4_0_fsyncdir, NULL, 0},
        [GFS3_OP_ACCESS]      = { "ACCESS",     GFS3_OP_ACCESS, server4_0_access, NULL, 0},
        [GFS3_OP_CREATE]      = { "CREATE",     GFS3_OP_CREATE, server4_0_create, NULL, 0},
        [GFS3_OP_FTRUNCATE]   = { "FTRUNCATE",  GFS3_OP_FTRUNCATE, server4_0_ftruncate, NULL, 0},
        [GFS3_OP_FSTAT]       = { "FSTAT",      GFS3_OP_FSTAT, server4_0_fstat, NULL, 0},
        [GFS3_OP_LK]          = { "LK",         GFS3_OP_LK, server4_0_lk, NULL, 0},
        [GFS3_OP_LOOKUP]      = { "LOOKUP",     GFS3_OP_LOOKUP, server4_0_lookup, NULL, 0},
        [GFS3_OP_READDIR]     = { "READDIR",    GFS3_OP_READDIR, server4_0_readdir, NULL, 0},
        [GFS3_OP_INODELK]     = { "INODELK",    GFS3_OP_INODELK, server4_0_inodelk, NULL, 0},
        [GFS3_OP_FINODELK]    = { "FINODELK",   GFS3_OP_FINODELK, server4_0_finodelk, NULL, 0},
        [GFS3_OP_ENTRYLK]     = { "ENTRYLK",    GFS3_OP_ENTRYLK, server4_0_entrylk, NULL, 0},
        [GFS3_OP_FENTRYLK]    = { "FENTRYLK",   GFS3_OP_FENTRYLK, server4_0_fentrylk, NULL, 0},
        [GFS3_OP_XATTROP]     = { "XATTROP",    GFS3_OP_XATTROP, server4_0_xattrop, NULL, 0},
        [GFS3_OP_FXATTROP]    = { "FXATTROP",   GFS3_OP_FXATTROP, server4_0_fxattrop, NULL, 0},
        [GFS3_OP_FGETXATTR]   = { "FGETXATTR",  GFS3_OP_FGETXATTR, server4_0_fgetxattr, NULL, 0},
        [GFS3_OP_FSETXATTR]   = { "FSETXATTR",  GFS3_OP_FSETXATTR, server4_0_fsetxattr, NULL, 0},
        [GFS3_OP_RCHECKSUM]   = { "RCHECKSUM",  GFS3_OP_RCHECKSUM, server4_0_rchecksum, NULL, 0},
        [GFS3_OP_SETATTR]     = { "SETATTR",    GFS3_OP_SETATTR, server4_0_setattr, NULL, 0},
        [GFS3_OP_FSETATTR]    = { "FSETATTR",   GFS3_OP_FSETATTR, server4_0_fsetattr, NULL, 0},
        [GFS3_OP_READDIRP]    = { "READDIRP",   GFS3_OP_READDIRP, server4_0_readdirp, NULL, 0},
        [GFS3_OP_RELEASE]     = { "RELEASE",    GFS3_OP_RELEASE, server4_0_release, NULL, 0},
        [GFS3_OP_RELEASEDIR]  = { "RELEASEDIR", GFS3_OP_RELEASEDIR, server4_0_releasedir, NULL, 0},
        [GFS3_OP_FREMOVEXATTR] = { "FREMOVEXATTR", GFS3_OP_FREMOVEXATTR, server4_0_fremovexattr, NULL, 0},
        [GFS3_OP_FALLOCATE]    = {"FALLOCATE",    GFS3_OP_FALLOCATE,    server4_0_fallocate,    NULL, 0, DRC_NA},
        [GFS3_OP_DISCARD]      = {"DISCARD",      GFS3_OP_DISCARD,      server4_0_discard,      NULL, 0, DRC_NA},
        [GFS3_OP_ZEROFILL]     = {"ZEROFILL",     GFS3_OP_ZEROFILL,     server4_0_zerofill,     NULL, 0, DRC_NA},
        [GFS3_OP_IPC]          = {"IPC",          GFS3_OP_IPC,          server4_0_ipc,          NULL, 0, DRC_NA},
        [GFS3_OP_SEEK]         = {"SEEK",         GFS3_OP_SEEK,         server4_0_seek,         NULL, 0, DRC_NA},
        [GFS3_OP_LEASE]       =  {"LEASE",        GFS3_OP_LEASE,        server4_0_lease,        NULL, 0, DRC_NA},
        [GFS3_OP_GETACTIVELK]  = {"GETACTIVELK",  GFS3_OP_GETACTIVELK,  server4_0_getactivelk,  NULL, 0, DRC_NA},
        [GFS3_OP_SETACTIVELK]  = {"SETACTIVELK",  GFS3_OP_SETACTIVELK,  server4_0_setactivelk,  NULL, 0, DRC_NA},
        [GFS3_OP_COMPOUND]     = {"COMPOUND",     GFS3_OP_COMPOUND,     server4_0_compound,     NULL, 0, DRC_NA},
        [GFS3_OP_ICREATE]  = {"ICREATE",      GFS3_OP_ICREATE,      server4_0_icreate,  NULL, 0, DRC_NA},
        [GFS3_OP_NAMELINK]     = {"NAMELINK",     GFS3_OP_NAMELINK,     server4_0_namelink,     NULL, 0, DRC_NA},
};


struct rpcsvc_program glusterfs4_0_fop_prog = {
        .progname  = "GlusterFS 4.x v1",
        .prognum   = GLUSTER_FOP_PROGRAM,
        .progver   = GLUSTER_FOP_VERSION_v2,
        .numactors = GLUSTER_FOP_PROCCNT,
        .actors    = glusterfs4_0_fop_actors,
        .ownthread = _gf_true,
};
