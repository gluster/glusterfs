/*
 * Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
 * This file is part of GlusterFS.
 *
 * This file is licensed to you under your choice of the GNU Lesser
 * General Public License, version 3 or any later version (LGPLv3 or
 * later), or the GNU General Public License, version 2 (GPLv2), in all
 * cases as published by the Free Software Foundation.
 */

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

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


typedef ssize_t (*acl3_serializer) (struct iovec outmsg, void *args);

extern void nfs3_call_state_wipe (nfs3_call_state_t *cs);

extern nfs3_call_state_t *
nfs3_call_state_init (struct nfs3_state *s, rpcsvc_request_t *req, xlator_t *v);

extern int
nfs3_fh_validate (struct nfs3_fh *fh);

extern fattr3
nfs3_stat_to_fattr3 (struct iatt *buf);

#define acl3_validate_nfs3_state(request, state, status, label, retval) \
        do      {                                                       \
                state = rpcsvc_request_program_private (request);       \
                if (!state) {                                           \
                        gf_log (GF_ACL, GF_LOG_ERROR, "NFSv3 state "    \
                                "missing from RPC request");            \
                        rpcsvc_request_seterr (req, SYSTEM_ERR);        \
                        status = NFS3ERR_SERVERFAULT;                   \
                        goto label;                                     \
                }                                                       \
        } while (0);                                                    \

#define acl3_validate_gluster_fh(handle, status, errlabel)              \
        do {                                                            \
                if (!nfs3_fh_validate (handle)) {                       \
                        status = NFS3ERR_SERVERFAULT;                   \
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
                        uuid_unparse (handle->exportid, exportid);       \
                        uuid_unparse (handle->gfid, gfid);               \
                        trans = rpcsvc_request_transport (req);         \
                        gf_log (GF_ACL, GF_LOG_ERROR, "Failed to map "  \
                                "FH to vol: client=%s, exportid=%s, gfid=%s",\
                                trans->peerinfo.identifier, exportid,   \
                                gfid);                                  \
                        gf_log (GF_ACL, GF_LOG_ERROR,                   \
                                "Stale nfs client %s must be trying to "\
                                "connect to a deleted volume, please "  \
                                "unmount it.", trans->peerinfo.identifier);\
                        status = NFS3ERR_STALE;                         \
                        goto label;                                     \
                } else {                                                \
                        gf_log (GF_ACL, GF_LOG_TRACE, "FH to Volume: %s"\
                                ,volume->name);                         \
                        rpcsvc_request_set_private (req, volume);       \
                }                                                       \
        } while (0);                                                    \

#define acl3_volume_started_check(nfs3state, vlm, rtval, erlbl)         \
        do {                                                            \
              if ((!nfs_subvolume_started (nfs_state (nfs3state->nfsx), vlm))){\
                      gf_log (GF_ACL, GF_LOG_ERROR, "Volume is disabled: %s",\
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
                        uuid_unparse (cst->resolvefh.gfid, gfid);       \
                        sprintf (buf, "(%s) %s : %s", trans->peerinfo.identifier,\
                        xlatorp ? xlatorp->name : "ERR", gfid);         \
                        gf_log (GF_ACL, GF_LOG_ERROR, "Unable to resolve FH"\
                                ": %s", buf);                           \
                        nfstat = nfs3_errno_to_nfsstat3 (cst->resolve_errno);\
                        goto erlabl;                                    \
                }                                                       \
        } while (0)                                                     \

#define acl3_handle_call_state_init(nfs3state, calls, rq, v, opstat, errlabel)\
        do {                                                            \
                calls = nfs3_call_state_init ((nfs3state), (rq), v); \
                if (!calls) {                                           \
                        gf_log (GF_ACL, GF_LOG_ERROR, "Failed to "      \
                                "init call state");                     \
                        opstat = NFS3ERR_SERVERFAULT;                   \
                        rpcsvc_request_seterr (req, SYSTEM_ERR);        \
                        goto errlabel;                                  \
                }                                                       \
        } while (0)                                                     \


int
acl3svc_submit_reply (rpcsvc_request_t *req, void *arg, acl3_serializer sfunc)
{
        struct iovec            outmsg = {0, };
        struct iobuf            *iob = NULL;
        struct nfs3_state       *nfs3 = NULL;
        int                     ret = -1;
        struct iobref           *iobref = NULL;

        if (!req)
                return -1;

        nfs3 = (struct nfs3_state *)rpcsvc_request_program_private (req);
        if (!nfs3) {
                gf_log (GF_ACL, GF_LOG_ERROR, "mount state not found");
                goto ret;
        }

        /* First, get the io buffer into which the reply in arg will
         * be serialized.
         */
        iob = iobuf_get (nfs3->iobpool);
        if (!iob) {
                gf_log (GF_ACL, GF_LOG_ERROR, "Failed to get iobuf");
                goto ret;
        }

        iobuf_to_iovec (iob, &outmsg);
        /* Use the given serializer to translate the give C structure in arg
         * to XDR format which will be written into the buffer in outmsg.
         */
        outmsg.iov_len = sfunc (outmsg, arg);

        iobref = iobref_new ();
        if (iobref == NULL) {
                gf_log (GF_ACL, GF_LOG_ERROR, "Failed to get iobref");
                goto ret;
        }

        iobref_add (iobref, iob);

        /* Then, submit the message for transmission. */
        ret = rpcsvc_submit_message (req, &outmsg, 1, NULL, 0, iobref);
        if (ret == -1) {
                gf_log (GF_ACL, GF_LOG_ERROR, "Reply submission failed");
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
                gf_log (GF_ACL, GF_LOG_ERROR, "Got NULL request!");
                return 0;
        }
        rpcsvc_submit_generic (req, &dummyvec, 1,  NULL, 0, NULL);
        return 0;
}

int
acl3_getacl_reply (nfs3_call_state_t *cs, getaclreply *reply)
{
        acl3svc_submit_reply (cs->req, (void *)reply,
                              (acl3_serializer)xdr_serialize_getaclreply);
        return 0;
}


int
acl3_getacl_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *dict,
                 dict_t *xdata)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t              *cs = NULL;
        data_t                         *data = NULL;
        int                            *p = NULL;
        int                             i = 0;
        getaclreply                     *getaclreply = NULL;

        cs = frame->local;
        if (cs)
                getaclreply = &cs->args.getaclreply;

        if (op_ret == -1) {
                stat = nfs3_errno_to_nfsstat3 (op_errno);
                goto err;
        }

        getaclreply->aclentry.aclentry_val = cs->aclentry;
        getaclreply->daclentry.daclentry_val = cs->daclentry;

        /* FIXME: use posix_acl_from_xattr() */
        data = dict_get (dict, "system.posix_acl_access");
        if (data && (p = data_to_bin (data))) {
                /* POSIX_ACL_XATTR_VERSION */
                p++;
                while ((char *)p < (data->data + data->len)) {
                        getaclreply->aclentry.aclentry_val[i].type = *(*(short **)&p)++;
                        getaclreply->aclentry.aclentry_val[i].perm = *(*(short **)&p)++;
                        getaclreply->aclentry.aclentry_val[i].uid = *(*(int **)&p)++;
                        i++;
                }
                getaclreply->aclcount = getaclreply->aclentry.aclentry_len = i;
        }
        i = 0;

        data = dict_get (dict, "system.posix_acl_default");
        if (data && (p = data_to_bin (data))) {
                /* POSIX_ACL_XATTR_VERSION */
                p++;
                while ((char *)p < (data->data + data->len)) {
                        getaclreply->daclentry.daclentry_val[i].type = *(*(short **)&p)++;
                        getaclreply->daclentry.daclentry_val[i].perm = *(*(short **)&p)++;
                        getaclreply->daclentry.daclentry_val[i].uid = *(*(int **)&p)++;
                        i++;
                }
                getaclreply->daclcount = getaclreply->daclentry.daclentry_len = i;
        }

        acl3_getacl_reply (cs, getaclreply);
        nfs3_call_state_wipe (cs);
        return 0;

err:
        if (getaclreply)
                getaclreply->status = stat;
        acl3_getacl_reply (cs, getaclreply);
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

        cs = frame->local;
        if (cs)
                getaclreply = &cs->args.getaclreply;

        if (op_ret == -1) {
                stat = nfs3_errno_to_nfsstat3 (op_errno);
                goto err;
        }

        getaclreply->attr_follows = 1;
        getaclreply->attr = nfs3_stat_to_fattr3 (buf);
        getaclreply->mask = 0xf;
        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_getxattr (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc, NULL, NULL,
                            acl3_getacl_cbk, cs);
        if (ret == -1) {
                stat = nfs3_errno_to_nfsstat3 (op_errno);
                goto err;
        }
        return 0;
err:
        getaclreply->status = stat;
        acl3_getacl_reply (cs, getaclreply);
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
                gf_log (GF_ACL, GF_LOG_ERROR, "unable to open_and_resume");
                cs->args.getaclreply.status = nfs3_errno_to_nfsstat3 (stat);
                acl3_getacl_reply (cs, &cs->args.getaclreply);
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

        if (!req)
                return ret;

        acl3_validate_nfs3_state (req, nfs3, stat, rpcerr, ret);
        nfs = nfs_state (nfs3->nfsx);
        memset (&getaclargs, 0, sizeof (getaclargs));
        getaclargs.fh.n_bytes = (char *)&fh;
        if (xdr_to_getaclargs(req->msg[0], &getaclargs) <= 0) {
                gf_log (GF_ACL, GF_LOG_ERROR, "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }
        fhp = &fh;
        acl3_validate_gluster_fh (&fh, stat, acl3err);
        acl3_map_fh_to_volume (nfs->nfs3state, fhp, req,
                               vol, stat, acl3err);
        acl3_handle_call_state_init (nfs->nfs3state, cs, req,
                                     vol, stat, rpcerr);

        cs->vol = vol;
        acl3_volume_started_check (nfs3, vol, ret, acl3err);

        ret = nfs3_fh_resolve_and_resume (cs, fhp,
                                          NULL, acl3_getacl_resume);

acl3err:
        if (ret < 0) {
                gf_log (GF_ACL, GF_LOG_ERROR, "unable to resolve and resume");
                if (cs) {
                        cs->args.getaclreply.status = stat;
                        acl3_getacl_reply (cs, &cs->args.getaclreply);
                        nfs3_call_state_wipe (cs);
                }
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
                cs->args.setaclreply.status = nfs3_errno_to_nfsstat3 (op_errno);
        }

        acl3svc_submit_reply (cs->req, (void *)&cs->args.setaclreply,
                              (acl3_serializer)xdr_serialize_setaclreply);
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
        ret = dict_set_static_bin (xattr, "system.posix_acl_access", cs->aclxattr,
                                   cs->aclcount * 8 + 4);
        if (cs->daclcount)
        ret = dict_set_static_bin (xattr, "system.posix_acl_default", cs->daclxattr,
                                   cs->daclcount * 8 + 4);

        ret = nfs_setxattr (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc, xattr,
                            0, NULL, acl3_setacl_cbk, cs);
        dict_unref (xattr);

acl3err:
        if (ret < 0) {
                stat = -ret;
                gf_log (GF_ACL, GF_LOG_ERROR, "unable to open_and_resume");
                cs->args.setaclreply.status = nfs3_errno_to_nfsstat3 (stat);
                acl3svc_submit_reply (cs->req, (void *)&cs->args.setaclreply,
                                      (acl3_serializer)xdr_serialize_setaclreply);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}


int
acl3svc_setacl (rpcsvc_request_t *req)
{
        xlator_t                        *vol = NULL;
        struct nfs_state               *nfs = NULL;
        nfs3_state_t                   *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;
        int                             ret = RPCSVC_ACTOR_ERROR;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        struct nfs3_fh                  fh;
        struct nfs3_fh                 *fhp = NULL;
        setaclargs                      setaclargs;
        aclentry                        aclentry[NFS_ACL_MAX_ENTRIES];
        struct aclentry                 daclentry[NFS_ACL_MAX_ENTRIES];
        int                             *p = NULL, i = 0;

        if (!req)
                return ret;

        acl3_validate_nfs3_state (req, nfs3, stat, rpcerr, ret);
        nfs = nfs_state (nfs3->nfsx);
        memset (&setaclargs, 0, sizeof (setaclargs));
        memset (&fh, 0, sizeof (fh));
        setaclargs.fh.n_bytes = (char *)&fh;
        setaclargs.aclentry.aclentry_val = aclentry;
        setaclargs.daclentry.daclentry_val = daclentry;
        if (xdr_to_setaclargs(req->msg[0], &setaclargs) <= 0) {
                gf_log (GF_ACL, GF_LOG_ERROR, "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }
        fhp = &fh;
        acl3_validate_gluster_fh (fhp, stat, acl3err);
        acl3_map_fh_to_volume (nfs->nfs3state, fhp, req,
                               vol, stat, acl3err);
        acl3_handle_call_state_init (nfs->nfs3state, cs, req,
                                     vol, stat, rpcerr);

        cs->vol = vol;
        acl3_volume_started_check (nfs3, vol, ret, rpcerr);

        cs->aclcount = setaclargs.aclcount;
        cs->daclcount = setaclargs.daclcount;

        if ((cs->aclcount > NFS_ACL_MAX_ENTRIES) ||
                        (cs->daclcount > NFS_ACL_MAX_ENTRIES))
                goto acl3err;
        /* FIXME: use posix_acl_to_xattr() */
        p = (int *)cs->aclxattr;
        *(*(int **)&p)++ = POSIX_ACL_XATTR_VERSION;
        for (i = 0; i < cs->aclcount; i++) {
                *(*(short **)&p)++ = aclentry[i].type;
                *(*(short **)&p)++ = aclentry[i].perm;
                *(*(int **)&p)++ = aclentry[i].uid;
        }
        p = (int *)cs->daclxattr;
        *(*(int **)&p)++ = POSIX_ACL_XATTR_VERSION;
        for (i = 0; i < cs->daclcount; i++) {
                *(*(short **)&p)++ = daclentry[i].type;
                *(*(short **)&p)++ = daclentry[i].perm;
                *(*(int **)&p)++ = daclentry[i].uid;
        }


        ret = nfs3_fh_resolve_and_resume (cs, fhp,
                                          NULL, acl3_setacl_resume);

acl3err:
        if (ret < 0) {
                gf_log (GF_ACL, GF_LOG_ERROR, "unable to resolve and resume");
                cs->args.setaclreply.status = stat;
                acl3svc_submit_reply (cs->req, (void *)&cs->args.setaclreply,
                                      (acl3_serializer)xdr_serialize_setaclreply);
                nfs3_call_state_wipe (cs);
                return 0;
        }

rpcerr:
        if (ret < 0)
                nfs3_call_state_wipe (cs);

        return ret;
}




rpcsvc_actor_t  acl3svc_actors[ACL3_PROC_COUNT] = {
        {"NULL",       ACL3_NULL,      acl3svc_null,   NULL,   0},
        {"GETACL",     ACL3_GETACL,    acl3svc_getacl, NULL,   0},
        {"SETACL",     ACL3_SETACL,    acl3svc_setacl, NULL,   0},
};

rpcsvc_program_t        acl3prog = {
        .progname       = "ACL3",
        .prognum        = ACL_PROGRAM,
        .progver        = ACL_V3,
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

        nfs = (struct nfs_state*)nfsx->private;

        ns = nfs->nfs3state;
        if (!ns) {
                gf_log (GF_ACL, GF_LOG_ERROR, "ACL3 init failed");
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
                gf_log (GF_ACL, GF_LOG_ERROR, "dict_set_str error");
                goto err;
        }

        if (nfs->allow_insecure) {
                ret = dict_set_str (options, "rpc-auth-allow-insecure", "on");
                if (ret == -1) {
                        gf_log (GF_ACL, GF_LOG_ERROR, "dict_set_str error");
                        goto err;
                }
                ret = dict_set_str (options, "rpc-auth.ports.insecure", "on");
                if (ret == -1) {
                        gf_log (GF_ACL, GF_LOG_ERROR, "dict_set_str error");
                        goto err;
                }
        }

        ret = dict_set_str (options, "transport.address-family", "inet");
        if (ret == -1) {
                gf_log (GF_ACL, GF_LOG_ERROR, "dict_set_str error");
                goto err;
        }

        rpcsvc_create_listeners (nfs->rpcsvc, options, "ACL");
        if (ret == -1) {
                gf_log (GF_ACL, GF_LOG_ERROR, "Unable to create listeners");
                dict_unref (options);
                goto err;
        }

        return &acl3prog;
err:
        return NULL;
}
