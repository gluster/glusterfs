/*
 * Copyright (c) 2012-2013 Red Hat, Inc. <http://www.redhat.com>
 * This file is part of GlusterFS.
 *
 * This file is licensed to you under your choice of the GNU Lesser
 * General Public License, version 3 or any later version (LGPLv3 or
 * later), or the GNU General Public License, version 2 (GPLv2), in all
 * cases as published by the Free Software Foundation.
 */

#include "defaults.h"
#include "rpcsvc.h"
#include "dict.h"
#include "xlator.h"
#include "nfs.h"
#include "mem-pool.h"
#include "logging.h"
#include "nfs-fops.h"
#include "inode.h"
#include "nfs3.h"
#include "nfs-mem-types.h"
#include "nfs3-helpers.h"
#include "nfs3-fh.h"
#include "nfs-generics.h"
#include "acl3.h"
#include "byte-order.h"
#include "compat-errno.h"
#include "nfs-messages.h"

static int
acl3_nfs_acl_to_xattr (aclentry *ace, void *xattrbuf,
                       int aclcount, int defacl);

static int
acl3_nfs_acl_from_xattr (aclentry *ace, void *xattrbuf,
                         int bufsize, int defacl);

typedef ssize_t (*acl3_serializer) (struct iovec outmsg, void *args);

extern void nfs3_call_state_wipe (nfs3_call_state_t *cs);

extern nfs3_call_state_t *
nfs3_call_state_init (struct nfs3_state *s, rpcsvc_request_t *req, xlator_t *v);

extern int
nfs3_fh_validate (struct nfs3_fh *fh);

extern void
nfs3_stat_to_fattr3 (struct iatt *buf, fattr3 *fa);

#define acl3_validate_nfs3_state(request, state, status, label, retval) \
        do      {                                                       \
                state = rpcsvc_request_program_private (request);       \
                if (!state) {                                           \
                        gf_msg (GF_ACL, GF_LOG_ERROR, errno,            \
                                NFS_MSG_STATE_MISSING,                  \
                                "NFSv3 state "                          \
                                "missing from RPC request");            \
                        rpcsvc_request_seterr (req, SYSTEM_ERR);        \
                        status = NFS3ERR_SERVERFAULT;                   \
                        goto label;                                     \
                }                                                       \
        } while (0);                                                    \

#define acl3_validate_gluster_fh(handle, status, errlabel)              \
        do {                                                            \
                if (!nfs3_fh_validate (handle)) {                       \
                        gf_msg (GF_ACL, GF_LOG_ERROR, EINVAL,           \
                                NFS_MSG_BAD_HANDLE,                     \
                                "Bad Handle");                          \
                        status = NFS3ERR_BADHANDLE;                     \
                        goto errlabel;                                  \
                }                                                       \
        } while (0)                                                     \


extern xlator_t *
nfs3_fh_to_xlator (struct nfs3_state *nfs3, struct nfs3_fh *fh);

#define acl3_map_fh_to_volume(nfs3state, handle, req, volume, status, label) \
        do {                                                            \
                char exportid[256], gfid[256];                          \
                rpc_transport_t *trans = NULL;                          \
                volume = nfs3_fh_to_xlator ((nfs3state), handle);      \
                if (!volume) {                                          \
                        gf_uuid_unparse (handle->exportid, exportid);   \
                        gf_uuid_unparse (handle->gfid, gfid);           \
                        trans = rpcsvc_request_transport (req);         \
                        gf_msg (GF_ACL, GF_LOG_ERROR, 0,                \
                                NFS_MSG_FH_TO_VOL_FAIL, "Failed to map "  \
                                "FH to vol: client=%s, exportid=%s, gfid=%s",\
                                trans->peerinfo.identifier, exportid,   \
                                gfid);                                  \
                        gf_msg (GF_ACL, GF_LOG_ERROR, ESTALE,           \
                                NFS_MSG_VOLUME_ERROR,                   \
                                "Stale nfs client %s must be trying to "\
                                "connect to a deleted volume, please "  \
                                "unmount it.", trans->peerinfo.identifier);\
                        status = NFS3ERR_STALE;                         \
                        goto label;                                     \
                } else {                                                \
                        gf_msg_trace (GF_ACL, 0, "FH to Volume: %s",    \
                                      volume->name);                    \
                        rpcsvc_request_set_private (req, volume);       \
                }                                                       \
        } while (0);                                                    \

#define acl3_volume_started_check(nfs3state, vlm, rtval, erlbl)         \
        do {                                                            \
              if ((!nfs_subvolume_started (nfs_state (nfs3state->nfsx), vlm))){\
                        gf_msg (GF_ACL, GF_LOG_ERROR, 0, NFS_MSG_VOL_DISABLE, \
                                "Volume is disabled: %s",                 \
                                vlm->name);                               \
                      rtval = RPCSVC_ACTOR_IGNORE;                      \
                      goto erlbl;                                       \
              }                                                         \
        } while (0)                                                     \

#define acl3_check_fh_resolve_status(cst, nfstat, erlabl)               \
        do {                                                            \
                xlator_t *xlatorp = NULL;                               \
                char buf[256], gfid[256];                               \
                rpc_transport_t *trans = NULL;                          \
                if ((cst)->resolve_ret < 0) {                           \
                        trans = rpcsvc_request_transport (cst->req);    \
                        xlatorp = nfs3_fh_to_xlator (cst->nfs3state,    \
                                                     &cst->resolvefh);  \
                        gf_uuid_unparse (cst->resolvefh.gfid, gfid);    \
                        snprintf (buf, sizeof (buf), "(%s) %s : %s",    \
                                  trans->peerinfo.identifier,           \
                                  xlatorp ? xlatorp->name : "ERR",      \
                                  gfid);                                \
                        gf_msg (GF_ACL, GF_LOG_ERROR, cst->resolve_errno, \
                                NFS_MSG_RESOLVE_FH_FAIL, "Unable to resolve "\
                                "FH: %s", buf);                           \
                        nfstat = nfs3_errno_to_nfsstat3 (cst->resolve_errno);\
                        goto erlabl;                                    \
                }                                                       \
        } while (0)                                                     \

#define acl3_handle_call_state_init(nfs3state, calls, rq, v, opstat, errlabel)\
        do {                                                            \
                calls = nfs3_call_state_init ((nfs3state), (rq), v);    \
                if (!calls) {                                           \
                        gf_msg (GF_ACL, GF_LOG_ERROR, 0,                \
                                NFS_MSG_INIT_CALL_STAT_FAIL, "Failed to " \
                                "init call state");                     \
                        opstat = NFS3ERR_SERVERFAULT;                   \
                        rpcsvc_request_seterr (req, SYSTEM_ERR);        \
                        goto errlabel;                                  \
                }                                                       \
        } while (0)                                                     \


int
acl3svc_submit_reply (rpcsvc_request_t *req, void *arg, acl3_serializer sfunc)
{
        struct iovec            outmsg  = {0, };
        struct iobuf            *iob    = NULL;
        struct nfs3_state       *nfs3   = NULL;
        int                     ret     = -1;
        ssize_t                 msglen  = 0;
        struct iobref           *iobref = NULL;

        if (!req)
                return -1;

        nfs3 = (struct nfs3_state *)rpcsvc_request_program_private (req);
        if (!nfs3) {
                gf_msg (GF_ACL, GF_LOG_ERROR, EINVAL,
                        NFS_MSG_MNT_STATE_NOT_FOUND, "mount state not found");
                goto ret;
        }

        /* First, get the io buffer into which the reply in arg will
         * be serialized.
         */
        iob = iobuf_get (nfs3->iobpool);
        if (!iob) {
                gf_msg (GF_ACL, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to get iobuf");
                goto ret;
        }

        iobuf_to_iovec (iob, &outmsg);
        /* Use the given serializer to translate the give C structure in arg
         * to XDR format which will be written into the buffer in outmsg.
         */
        msglen = sfunc (outmsg, arg);
        if (msglen < 0) {
                gf_msg (GF_ACL, GF_LOG_ERROR, errno, NFS_MSG_ENCODE_MSG_FAIL,
                        "Failed to encode message");
                goto ret;
        }
        outmsg.iov_len = msglen;

        iobref = iobref_new ();
        if (iobref == NULL) {
                gf_msg (GF_ACL, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to get iobref");
                goto ret;
        }

        ret = iobref_add (iobref, iob);
        if (ret) {
                gf_msg (GF_ACL, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to add iob to iobref");
                goto ret;
        }

        /* Then, submit the message for transmission. */
        ret = rpcsvc_submit_message (req, &outmsg, 1, NULL, 0, iobref);
        if (ret == -1) {
                gf_msg (GF_ACL, GF_LOG_ERROR, errno, NFS_MSG_REP_SUBMIT_FAIL,
                        "Reply submission failed");
                goto ret;
        }

        ret = 0;
ret:
        if (iob)
                iobuf_unref (iob);
        if (iobref)
                iobref_unref (iobref);

        return ret;
}


int
acl3svc_null (rpcsvc_request_t *req)
{
        struct iovec    dummyvec = {0, };

        if (!req) {
                gf_msg (GF_ACL, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Got NULL request!");
                return 0;
        }
        rpcsvc_submit_generic (req, &dummyvec, 1,  NULL, 0, NULL);
        return 0;
}

int
acl3_getacl_reply (rpcsvc_request_t *req, getaclreply *reply)
{
        acl3svc_submit_reply (req, (void *)reply,
                              (acl3_serializer)xdr_serialize_getaclreply);
        return 0;
}

int
acl3_setacl_reply (rpcsvc_request_t *req, setaclreply *reply)
{
        acl3svc_submit_reply (req, (void *)reply,
                              (acl3_serializer)xdr_serialize_setaclreply);
        return 0;
}

/* acl3_getacl_cbk: fetch and decode the ACL in the POSIX_ACL_ACCESS_XATTR
 *
 * The POSIX_ACL_ACCESS_XATTR can be set on files and directories.
 */
int
acl3_getacl_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *dict,
                 dict_t *xdata)
{
        nfsstat3                 stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t        *cs = NULL;
        data_t                   *data = NULL;
        getaclreply              *getaclreply = NULL;
        int                      aclcount = 0;
        int                      defacl = 1; /* DEFAULT ACL */

        if (!frame->local) {
                gf_msg (GF_ACL, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Invalid argument, frame->local NULL");
                return -EINVAL;
        }
        cs = frame->local;
        getaclreply = &cs->args.getaclreply;
        if ((op_ret < 0) && (op_errno != ENODATA && op_errno != ENOATTR)) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto err;
        } else if (!dict) {
                /* no ACL has been set */
                stat = NFS3_OK;
                goto err;
        }

        getaclreply->aclentry.aclentry_val = cs->aclentry;

        /* getfacl: NFS USER ACL */
        data = dict_get (dict, POSIX_ACL_ACCESS_XATTR);
        if (data && data->data) {
                aclcount = acl3_nfs_acl_from_xattr (cs->aclentry,
                                                    data->data,
                                                    data->len,
                                                    !defacl);
                if (aclcount < 0) {
                        gf_msg (GF_ACL, GF_LOG_ERROR, aclcount,
                                NFS_MSG_GET_USER_ACL_FAIL,
                                "Failed to get USER ACL");
                        stat = nfs3_errno_to_nfsstat3 (-aclcount);
                        goto err;
                }
                getaclreply->aclcount = aclcount;
                getaclreply->aclentry.aclentry_len = aclcount;
        }

        acl3_getacl_reply (cs->req, getaclreply);
        nfs3_call_state_wipe (cs);
        return 0;

err:
        if (getaclreply)
                getaclreply->status = stat;
        acl3_getacl_reply (cs->req, getaclreply);
        nfs3_call_state_wipe (cs);
        return 0;
}

/* acl3_default_getacl_cbk: fetch and decode the ACL set in the
 * POSIX_ACL_DEFAULT_XATTR xattr.
 *
 * The POSIX_ACL_DEFAULT_XATTR xattr is only set on directories, not on files.
 *
 * When done with POSIX_ACL_DEFAULT_XATTR, we also need to get and decode the
 * ACL that can be set in POSIX_ACL_DEFAULT_XATTR.
 */
int
acl3_default_getacl_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *dict,
                         dict_t *xdata)
{
        nfsstat3                 stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t        *cs = NULL;
        data_t                   *data = NULL;
        getaclreply              *getaclreply = NULL;
        int                      aclcount = 0;
        int                      defacl = 1; /* DEFAULT ACL */
        nfs_user_t               nfu = {0, };
        int                      ret = -1;

        if (!frame->local) {
                gf_msg (GF_ACL, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Invalid argument, frame->local NULL");
                return -EINVAL;
        }
        cs = frame->local;
        getaclreply = &cs->args.getaclreply;
        if ((op_ret < 0) && (op_errno != ENODATA && op_errno != ENOATTR)) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto err;
        } else if (!dict) {
                /* no ACL has been set */
                stat = NFS3_OK;
                goto err;
        }

        getaclreply->daclentry.daclentry_val = cs->daclentry;

        /* getfacl: NFS DEFAULT ACL */
        data = dict_get (dict, POSIX_ACL_DEFAULT_XATTR);
        if (data && data->data) {
                aclcount = acl3_nfs_acl_from_xattr (cs->daclentry,
                                                    data->data,
                                                    data->len,
                                                    defacl);
                if (aclcount < 0) {
                        gf_msg (GF_ACL, GF_LOG_ERROR, aclcount,
                                NFS_MSG_GET_DEF_ACL_FAIL,
                                "Failed to get DEFAULT ACL");
                        stat = nfs3_errno_to_nfsstat3 (-aclcount);
                        goto err;
                }

                getaclreply->daclcount = aclcount;
                getaclreply->daclentry.daclentry_len = aclcount;
        }

        getaclreply->attr_follows = TRUE;
        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_getxattr (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                            POSIX_ACL_ACCESS_XATTR, NULL, acl3_getacl_cbk, cs);
        if (ret < 0) {
                stat = nfs3_errno_to_nfsstat3 (-ret);
                goto err;
        }

        return 0;

err:
        if (getaclreply)
                getaclreply->status = stat;
        acl3_getacl_reply (cs->req, getaclreply);
        nfs3_call_state_wipe (cs);
        return 0;
}


int
acl3_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *buf,
                  dict_t *xdata)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t              *cs = NULL;
        getaclreply                     *getaclreply = NULL;
        int                             ret = -1;
        nfs_user_t                      nfu = {0, };
        uint64_t                        deviceid = 0;

        if (!frame->local) {
                gf_msg (GF_ACL, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Invalid argument, frame->local NULL");
                return EINVAL;
        }

        cs = frame->local;
        getaclreply = &cs->args.getaclreply;

        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto err;
        }

        /* Fill the attrs before xattrs */
        getaclreply->attr_follows = TRUE;
        deviceid = nfs3_request_xlator_deviceid (cs->req);
        nfs3_map_deviceid_to_statdev (buf, deviceid);
        nfs3_stat_to_fattr3 (buf, &(getaclreply->attr));

        nfs_request_user_init (&nfu, cs->req);
        if (buf->ia_type == IA_IFDIR) {
                ret = nfs_getxattr (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                    POSIX_ACL_DEFAULT_XATTR, NULL,
                                    acl3_default_getacl_cbk, cs);
        } else {
                ret = nfs_getxattr (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                    POSIX_ACL_ACCESS_XATTR, NULL,
                                    acl3_getacl_cbk, cs);
        }

        if (ret < 0) {
                stat = nfs3_errno_to_nfsstat3 (-ret);
                goto err;
        }

        return 0;
err:
        getaclreply->status = stat;
        acl3_getacl_reply (cs->req, getaclreply);
        nfs3_call_state_wipe (cs);
        return 0;
}


int
acl3_getacl_resume (void *carg)
{
        int                             ret = -1;
        nfs3_call_state_t               *cs = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        nfs_user_t                      nfu = {0, };

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        acl3_check_fh_resolve_status (cs, stat, acl3err);
        nfs_request_user_init (&nfu, cs->req);

        ret = nfs_stat (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                        acl3_stat_cbk, cs);
        stat = -ret;
acl3err:
        if (ret < 0) {
                gf_msg (GF_ACL, GF_LOG_ERROR, stat, NFS_MSG_OPEN_FAIL,
                        "unable to open_and_resume");
                cs->args.getaclreply.status = nfs3_errno_to_nfsstat3 (stat);
                acl3_getacl_reply (cs->req, &cs->args.getaclreply);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}


int
acl3svc_getacl (rpcsvc_request_t *req)
{
        xlator_t                        *vol = NULL;
        struct nfs_state               *nfs = NULL;
        nfs3_state_t                   *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;
        int                             ret = RPCSVC_ACTOR_ERROR;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        struct nfs3_fh                  fh, *fhp = NULL;
        getaclargs                      getaclargs;
        getaclreply                     getaclreply;

        if (!req)
                return ret;

        acl3_validate_nfs3_state (req, nfs3, stat, rpcerr, ret);
        nfs = nfs_state (nfs3->nfsx);
        memset (&getaclargs, 0, sizeof (getaclargs));
        memset (&getaclreply, 0, sizeof (getaclreply));
        getaclargs.fh.n_bytes = (char *)&fh;
        if (xdr_to_getaclargs(req->msg[0], &getaclargs) <= 0) {
                gf_msg (GF_ACL, GF_LOG_ERROR, errno, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        /* Validate ACL mask */
        if (getaclargs.mask & ~(NFS_ACL|NFS_ACLCNT|NFS_DFACL|NFS_DFACLCNT)) {
                stat = NFS3ERR_INVAL;
                goto acl3err;
        }

        fhp = &fh;
        acl3_validate_gluster_fh (&fh, stat, acl3err);
        acl3_map_fh_to_volume (nfs->nfs3state, fhp, req, vol, stat, acl3err);
        acl3_volume_started_check (nfs3, vol, ret, rpcerr);
        acl3_handle_call_state_init (nfs->nfs3state, cs, req,
                                     vol, stat, acl3err);

        cs->vol = vol;
        cs->args.getaclreply.mask = getaclargs.mask;

        ret = nfs3_fh_resolve_and_resume (cs, fhp, NULL, acl3_getacl_resume);
        stat = nfs3_errno_to_nfsstat3 (-ret);

acl3err:
        if (ret < 0) {
                gf_msg (GF_ACL, GF_LOG_ERROR, -ret, NFS_MSG_RESOLVE_ERROR,
                        "unable to resolve and resume");
                getaclreply.status = stat;
                acl3_getacl_reply (req, &getaclreply);
                nfs3_call_state_wipe (cs);
                return 0;
        }

rpcerr:
        return ret;
}

int
acl3_setacl_cbk (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno,
                 dict_t *xdata)
{
        nfs3_call_state_t               *cs = NULL;
        cs = frame->local;
        if (op_ret < 0) {
                nfsstat3 status = nfs3_cbk_errno_status (op_ret, op_errno);
                cs->args.setaclreply.status = status;
        }

        acl3_setacl_reply (cs->req, &cs->args.setaclreply);

        nfs3_call_state_wipe (cs);

        return 0;
}

int
acl3_setacl_resume (void *carg)
{
        int                             ret = -1;
        nfs3_call_state_t               *cs = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        nfs_user_t                      nfu = {0, };
        dict_t                          *xattr = NULL;

        if (!carg)
                return ret;
        cs = (nfs3_call_state_t *)carg;
        acl3_check_fh_resolve_status (cs, stat, acl3err);
        nfs_request_user_init (&nfu, cs->req);
        xattr = dict_new();
        if (cs->aclcount)
        ret = dict_set_static_bin (xattr, POSIX_ACL_ACCESS_XATTR,
                                   cs->aclxattr,
                                   posix_acl_xattr_size (cs->aclcount));
        if (cs->daclcount)
        ret = dict_set_static_bin (xattr, POSIX_ACL_DEFAULT_XATTR,
                                   cs->daclxattr,
                                   posix_acl_xattr_size (cs->daclcount));

        ret = nfs_setxattr (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc, xattr,
                            0, NULL, acl3_setacl_cbk, cs);
        dict_unref (xattr);

acl3err:
        if (ret < 0) {
                stat = -ret;
                gf_msg (GF_ACL, GF_LOG_ERROR, stat, NFS_MSG_OPEN_FAIL,
                        "unable to open_and_resume");
                cs->args.setaclreply.status = nfs3_errno_to_nfsstat3 (stat);
                acl3_setacl_reply (cs->req, &cs->args.setaclreply);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}


int
acl3svc_setacl (rpcsvc_request_t *req)
{
        xlator_t                       *vol = NULL;
        struct nfs_state               *nfs = NULL;
        nfs3_state_t                   *nfs3 = NULL;
        nfs3_call_state_t              *cs = NULL;
        int                            ret = RPCSVC_ACTOR_ERROR;
        nfsstat3                       stat = NFS3ERR_SERVERFAULT;
        struct nfs3_fh                 fh;
        struct nfs3_fh                 *fhp = NULL;
        setaclargs                     setaclargs;
        setaclreply                    setaclreply;
        aclentry                       *daclentry = NULL;
        aclentry                       *aclentry = NULL;
        int                            aclerrno = 0;
        int                            defacl = 1;

        if (!req)
                return ret;
        aclentry =  GF_CALLOC (NFS_ACL_MAX_ENTRIES, sizeof(*aclentry),
                               gf_nfs_mt_arr);
        if (!aclentry) {
                goto rpcerr;
        }
        daclentry = GF_CALLOC (NFS_ACL_MAX_ENTRIES, sizeof(*daclentry),
                               gf_nfs_mt_arr);
        if (!daclentry) {
                goto rpcerr;
        }

        acl3_validate_nfs3_state (req, nfs3, stat, rpcerr, ret);
        nfs = nfs_state (nfs3->nfsx);
        memset (&setaclargs, 0, sizeof (setaclargs));
        memset (&setaclreply, 0, sizeof (setaclreply));
        memset (&fh, 0, sizeof (fh));
        setaclargs.fh.n_bytes = (char *)&fh;
        setaclargs.aclentry.aclentry_val = aclentry;
        setaclargs.daclentry.daclentry_val = daclentry;
        if (xdr_to_setaclargs(req->msg[0], &setaclargs) <= 0) {
                gf_msg (GF_ACL, GF_LOG_ERROR, errno, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        /* Validate ACL mask */
        if (setaclargs.mask & ~(NFS_ACL|NFS_ACLCNT|NFS_DFACL|NFS_DFACLCNT)) {
                stat = NFS3ERR_INVAL;
                goto acl3err;
        }

        fhp = &fh;
        acl3_validate_gluster_fh (fhp, stat, acl3err);
        acl3_map_fh_to_volume (nfs->nfs3state, fhp, req, vol, stat, acl3err);
        acl3_volume_started_check (nfs3, vol, ret, rpcerr);
        acl3_handle_call_state_init (nfs->nfs3state, cs, req,
                                     vol, stat, acl3err);

        cs->vol = vol;
        cs->aclcount = setaclargs.aclcount;
        cs->daclcount = setaclargs.daclcount;

        /* setfacl: NFS USER ACL */
        aclerrno = acl3_nfs_acl_to_xattr (aclentry,
                                          cs->aclxattr,
                                          cs->aclcount,
                                          !defacl);
        if (aclerrno < 0) {
                gf_msg (GF_ACL, GF_LOG_ERROR, -aclerrno,
                        NFS_MSG_SET_USER_ACL_FAIL,
                        "Failed to set USER ACL");
                stat = nfs3_errno_to_nfsstat3 (-aclerrno);
                goto acl3err;
        }

        /* setfacl: NFS DEFAULT ACL */
        aclerrno = acl3_nfs_acl_to_xattr (daclentry,
                                          cs->daclxattr,
                                          cs->daclcount,
                                          defacl);
        if (aclerrno < 0) {
                gf_msg (GF_ACL, GF_LOG_ERROR, -aclerrno,
                        NFS_MSG_SET_DEF_ACL_FAIL,
                        "Failed to set DEFAULT ACL");
                stat = nfs3_errno_to_nfsstat3 (-aclerrno);
                goto acl3err;
        }

        ret = nfs3_fh_resolve_and_resume (cs, fhp, NULL, acl3_setacl_resume);
        stat = nfs3_errno_to_nfsstat3 (-ret);

acl3err:
        if (ret < 0) {
                gf_msg (GF_ACL, GF_LOG_ERROR, -ret, NFS_MSG_RESOLVE_ERROR,
                        "unable to resolve and resume");
                setaclreply.status = stat;
                acl3_setacl_reply (req, &setaclreply);
                nfs3_call_state_wipe (cs);
                GF_FREE(aclentry);
                GF_FREE(daclentry);
                return 0;
        }

rpcerr:
        if (ret < 0)
                nfs3_call_state_wipe (cs);
        if (aclentry)
                GF_FREE (aclentry);
        if (daclentry)
                GF_FREE (daclentry);
        return ret;
}



rpcsvc_actor_t  acl3svc_actors[ACL3_PROC_COUNT] = {
        {"NULL",       ACL3_NULL,      acl3svc_null,   NULL,   0, DRC_NA},
        {"GETACL",     ACL3_GETACL,    acl3svc_getacl, NULL,   0, DRC_NA},
        {"SETACL",     ACL3_SETACL,    acl3svc_setacl, NULL,   0, DRC_NA},
};

rpcsvc_program_t        acl3prog = {
        .progname       = "ACL3",
        .prognum        = ACL_PROGRAM,
        .progver        = ACLV3_VERSION,
        .progport       = GF_NFS3_PORT,
        .actors         = acl3svc_actors,
        .numactors      = ACL3_PROC_COUNT,
        .min_auth       = AUTH_NULL,
};

rpcsvc_program_t *
acl3svc_init(xlator_t *nfsx)
{
        struct nfs3_state *ns = NULL;
        struct nfs_state *nfs = NULL;
        dict_t *options = NULL;
        int ret = -1;
        char *portstr = NULL;
        static gf_boolean_t acl3_inited = _gf_false;

        /* Already inited */
        if (acl3_inited)
                return &acl3prog;

        nfs = (struct nfs_state*)nfsx->private;

        ns = nfs->nfs3state;
        if (!ns) {
                gf_msg (GF_ACL, GF_LOG_ERROR, EINVAL, NFS_MSG_ACL_INIT_FAIL,
                        "ACL3 init failed");
                goto err;
        }
        acl3prog.private = ns;

        options = dict_new ();

        ret = gf_asprintf (&portstr, "%d", GF_ACL3_PORT);
        if (ret == -1)
                goto err;

        ret = dict_set_dynstr (options, "transport.socket.listen-port",
                               portstr);
        if (ret == -1)
                goto err;
        ret = dict_set_str (options, "transport-type", "socket");
        if (ret == -1) {
                gf_msg (GF_ACL, GF_LOG_ERROR, errno, NFS_MSG_DICT_SET_FAILED,
                        "dict_set_str error");
                goto err;
        }

        if (nfs->allow_insecure) {
                ret = dict_set_str (options, "rpc-auth-allow-insecure", "on");
                if (ret == -1) {
                        gf_msg (GF_ACL, GF_LOG_ERROR, errno,
                                 NFS_MSG_DICT_SET_FAILED,
                                 "dict_set_str error");
                        goto err;
                }
                ret = dict_set_str (options, "rpc-auth.ports.insecure", "on");
                if (ret == -1) {
                        gf_msg (GF_ACL, GF_LOG_ERROR, errno,
                                NFS_MSG_DICT_SET_FAILED,
                                "dict_set_str error");
                        goto err;
                }
        }

        ret = dict_set_str (options, "transport.address-family", "inet");
        if (ret == -1) {
                gf_msg (GF_ACL, GF_LOG_ERROR, errno,
                        NFS_MSG_DICT_SET_FAILED,
                        "dict_set_str error");
                goto err;
        }

        ret = rpcsvc_create_listeners (nfs->rpcsvc, options, "ACL");
        if (ret == -1) {
                gf_msg (GF_ACL, GF_LOG_ERROR, errno,
                        NFS_MSG_LISTENERS_CREATE_FAIL,
                        "Unable to create listeners");
                dict_unref (options);
                goto err;
        }

        acl3_inited = _gf_true;
        return &acl3prog;
err:
        return NULL;
}

static int
acl3_nfs_acl_to_xattr (aclentry *ace,      /* ACL entries to be read */
                       void     *xattrbuf, /* XATTR buf to be populated */
                       int      aclcount,  /* No of ACLs to be read */
                       int      defacl)    /* 1 if DEFAULT ACL */
{
        int                       idx      = 0;
        posix_acl_xattr_header    *xheader = NULL;
        posix_acl_xattr_entry     *xentry  = NULL;

        if ((!ace) || (!xattrbuf))
                return (-EINVAL);

        /* ACL count is ZERO, nothing to do */
        if (!aclcount)
                return (0);

        if ((aclcount < 0) || (aclcount > NFS_ACL_MAX_ENTRIES))
                return (-EINVAL);

        xheader = (posix_acl_xattr_header *) (xattrbuf);
        xentry  = (posix_acl_xattr_entry  *) (xheader + 1);

        /*
         * For "default ACL", NFSv3 handles the 'type' differently
         * i.e. by logical OR'ing 'type' with NFS_ACL_DEFAULT.
         * Which the backend File system does not understand and
         * that needs to be masked OFF.
         */
        xheader->version = POSIX_ACL_XATTR_VERSION;

        for (idx = 0; idx < aclcount; idx++) {
                xentry->tag  = ace->type;
                if (defacl)
                        xentry->tag &= ~NFS_ACL_DEFAULT;
                xentry->perm = ace->perm;

                switch (xentry->tag) {
                case POSIX_ACL_USER:
                case POSIX_ACL_GROUP:
                        if (xentry->perm & ~S_IRWXO)
                                return (-EINVAL);
                        xentry->id = ace->uid;
                        break;
                case POSIX_ACL_USER_OBJ:
                case POSIX_ACL_GROUP_OBJ:
                case POSIX_ACL_OTHER:
                        if (xentry->perm & ~S_IRWXO)
                                return (-EINVAL);
                        xentry->id = POSIX_ACL_UNDEFINED_ID;
                        break;
                case POSIX_ACL_MASK:
                        /* Solaris sometimes sets additional bits in
                         * the mask.
                         */
                        xentry->perm &= S_IRWXO;
                        xentry->id = POSIX_ACL_UNDEFINED_ID;
                        break;
                default:
                        return (-EINVAL);
                }

                xentry++;
                ace++;
        }

        /* SUCCESS */
        return (0);
}

static int
acl3_nfs_acl_from_xattr (aclentry  *ace,      /* ACL entries to be filled */
                         void      *xattrbuf, /* XATTR buf to be read */
                         int       bufsize,   /* Size of XATTR buffer */
                         int       defacl)    /*  1 if DEFAULT ACL */
{
        int                       idx       = 0;
        ssize_t                   aclcount  = 0;
        posix_acl_xattr_header    *xheader  = NULL;
        posix_acl_xattr_entry     *xentry   = NULL;

        if ((!xattrbuf) || (!ace))
                return (-EINVAL);

        aclcount = posix_acl_xattr_count (bufsize);
        if ((aclcount < 0) || (aclcount > NFS_ACL_MAX_ENTRIES))
                return (-EINVAL);

        xheader = (posix_acl_xattr_header *) (xattrbuf);
        xentry  = (posix_acl_xattr_entry  *) (xheader + 1);

        /* Check for supported POSIX ACL xattr version */
        if (xheader->version != POSIX_ACL_XATTR_VERSION)
                return (-ENOSYS);

        for (idx = 0; idx < (int)aclcount; idx++) {
                ace->type = xentry->tag;
                if (defacl) {
                        /*
                         * SET the NFS_ACL_DEFAULT flag for default
                         * ACL which was masked OFF during setfacl().
                         */
                        ace->type |= NFS_ACL_DEFAULT;
                }
                ace->perm = (xentry->perm & S_IRWXO);

                switch (xentry->tag) {
                case POSIX_ACL_USER:
                case POSIX_ACL_GROUP:
                        ace->uid = xentry->id;
                        break;
                case POSIX_ACL_USER_OBJ:
                case POSIX_ACL_GROUP_OBJ:
                case POSIX_ACL_MASK:
                case POSIX_ACL_OTHER:
                        ace->uid = POSIX_ACL_UNDEFINED_ID;
                        break;
                default:
                        return (-EINVAL);
                }


                xentry++;
                ace++;
        }

        /* SUCCESS: ACL count */
        return aclcount;
}
