/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "rpcsvc.h"
#include "dict.h"
#include "xlator.h"
#include "mount3.h"
#include "xdr-nfs3.h"
#include "msg-nfs3.h"
#include "iobuf.h"
#include "nfs3.h"
#include "mem-pool.h"
#include "logging.h"
#include "nfs-common.h"
#include "nfs-fops.h"
#include "nfs-inodes.h"
#include "nfs-generics.h"
#include "nfs3-helpers.h"
#include "nfs-mem-types.h"
#include "nfs.h"
#include "xdr-rpc.h"
#include "xdr-generic.h"
#include "nfs-messages.h"

#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/statvfs.h>
#include <time.h>

#define nfs3_validate_strlen_or_goto(str, len, label, status, retval)   \
        do      {                                                       \
                if ((str)) {                                            \
                        if (strlen ((str)) > (len)) {                   \
                                gf_msg (GF_NFS3, GF_LOG_ERROR,          \
                                        ENAMETOOLONG,                   \
                                        NFS_MSG_STR_TOO_LONG,           \
                                        "strlen too long");             \
                                status = NFS3ERR_NAMETOOLONG;           \
                                retval = -ENAMETOOLONG;                 \
                                goto label;                             \
                        }                                               \
                }                                                       \
        } while (0);                                                    \

#define nfs3_validate_nfs3_state(request, state, status, label, retval) \
        do      {                                                       \
                state = rpcsvc_request_program_private (request);       \
                if (!state) {                                           \
                        gf_msg (GF_NFS3, GF_LOG_ERROR, EFAULT,          \
                                NFS_MSG_STATE_MISSING, "NFSv3 state "   \
                                "missing from RPC request");            \
                        status = NFS3ERR_SERVERFAULT;                   \
                        ret = -EFAULT;                                  \
                        goto label;                                     \
                }                                                       \
        } while (0);                                                    \


struct nfs3_export *
__nfs3_get_export_by_index (struct nfs3_state *nfs3, uuid_t exportid)
{
        struct nfs3_export      *exp = NULL;
        int                     index = 0;
        int                     searchindex = 0;

        searchindex = nfs3_fh_exportid_to_index (exportid);
        list_for_each_entry (exp, &nfs3->exports, explist) {
                if (searchindex == index)
                        goto found;

                ++index;
        }

        exp = NULL;
        gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_INDEX_NOT_FOUND,
                "searchindex=%d not found", searchindex);
found:
        return exp;
}


struct nfs3_export *
__nfs3_get_export_by_volumeid (struct nfs3_state *nfs3, uuid_t exportid)
{
        struct nfs3_export      *exp = NULL;

        list_for_each_entry (exp, &nfs3->exports, explist) {
                if (!gf_uuid_compare (exportid, exp->volumeid))
                        goto found;
        }

        exp = NULL;
found:
        return exp;
}


struct nfs3_export *
__nfs3_get_export_by_exportid (struct nfs3_state *nfs3, uuid_t exportid)
{
        struct nfs3_export      *exp = NULL;

        if (!nfs3)
                return exp;

        if (gf_nfs_dvm_off (nfs_state(nfs3->nfsx)))
                exp = __nfs3_get_export_by_index (nfs3, exportid);
        else
                exp = __nfs3_get_export_by_volumeid (nfs3, exportid);

        return exp;
}


int
nfs3_export_access (struct nfs3_state *nfs3, uuid_t exportid)
{
        int                     ret = GF_NFS3_VOLACCESS_RO;
        struct nfs3_export      *exp = NULL;

        GF_VALIDATE_OR_GOTO (GF_NFS3, nfs3, err);

        exp = __nfs3_get_export_by_exportid (nfs3, exportid);

        if (!exp) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_EXPORT_ID_FAIL,
                        "Failed to get export by ID");
                goto err;
        }

        ret = exp->access;

err:
        return ret;
}

#define nfs3_check_rw_volaccess(nfs3state, exid, status, label)         \
        do {                                                            \
                if (nfs3_export_access (nfs3state,exid)!=GF_NFS3_VOLACCESS_RW){\
                        gf_msg (GF_NFS3, GF_LOG_ERROR, EACCES,          \
                                NFS_MSG_NO_RW_ACCESS,                   \
                                "No read-write access");                \
                        status = NFS3ERR_ROFS;                          \
                        goto label;                                     \
                }                                                       \
        } while (0)                                                     \



xlator_t *
nfs3_fh_to_xlator (struct nfs3_state *nfs3, struct nfs3_fh *fh)
{
        xlator_t                *vol = NULL;
        struct nfs3_export      *exp = NULL;

        GF_VALIDATE_OR_GOTO (GF_NFS3, nfs3, out);
        GF_VALIDATE_OR_GOTO (GF_NFS3, fh, out);

        exp = __nfs3_get_export_by_exportid (nfs3, fh->exportid);
        if (!exp)
                goto out;

        vol = exp->subvol;
out:
        return vol;
}


int
nfs3_is_root_looked_up (struct nfs3_state *nfs3, struct nfs3_fh *rootfh)
{
        struct nfs3_export      *exp = NULL;
        int                     ret = 0;

        GF_VALIDATE_OR_GOTO (GF_NFS3, nfs3, out);
        GF_VALIDATE_OR_GOTO (GF_NFS3, rootfh, out);

        exp = __nfs3_get_export_by_exportid (nfs3, rootfh->exportid);
        if (!exp)
                goto out;

        ret = exp->rootlookedup;
out:
        return ret;
}


int
nfs3_set_root_looked_up (struct nfs3_state *nfs3, struct nfs3_fh *rootfh)
{
        struct nfs3_export      *exp = NULL;
        int                     ret = 0;

        GF_VALIDATE_OR_GOTO (GF_NFS3, nfs3, out);
        GF_VALIDATE_OR_GOTO (GF_NFS3, rootfh, out);

        exp = __nfs3_get_export_by_exportid (nfs3, rootfh->exportid);
        if (!exp)
                goto out;

        exp->rootlookedup = 1;
out:
        return ret;
}


#define nfs3_map_fh_to_volume(nfs3state, handle, req, volume, status, label) \
        do {                                                            \
                char exportid[256], gfid[256];                          \
                rpc_transport_t *trans = NULL;                          \
                volume = nfs3_fh_to_xlator ((nfs3state), handle);       \
                if (!volume) {                                          \
                        gf_uuid_unparse (handle->exportid, exportid);   \
                        gf_uuid_unparse (handle->gfid, gfid);           \
                        trans = rpcsvc_request_transport (req);         \
                        GF_LOG_OCCASIONALLY (nfs3state->occ_logger,     \
                                GF_NFS3, GF_LOG_ERROR, "Failed to map " \
                                "FH to vol: client=%s, exportid=%s, "   \
                                "gfid=%s", trans->peerinfo.identifier,  \
                                exportid, gfid);                        \
                        GF_LOG_OCCASIONALLY (nfs3state->occ_logger,     \
                                GF_NFS3, GF_LOG_ERROR, "Stale nfs "     \
                                "client %s must be trying to connect to"\
                                " a deleted volume, please unmount it.",\
                                trans->peerinfo.identifier);            \
                        status = NFS3ERR_STALE;                         \
                        goto label;                                     \
                } else {                                                \
                        gf_msg_trace (GF_NFS3, 0, "FH to Volume:"       \
                                "%s", volume->name);                    \
                        rpcsvc_request_set_private (req, volume);       \
                }                                                       \
        } while (0);                                                    \


#define nfs3_validate_gluster_fh(handle, status, errlabel)              \
        do {                                                            \
                if (!nfs3_fh_validate (handle)) {                       \
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0,               \
                                NFS_MSG_BAD_HANDLE,                     \
                                "Bad Handle");                          \
                        status = NFS3ERR_BADHANDLE;                     \
                        goto errlabel;                                  \
                }                                                       \
        } while (0)                                                     \


#define nfs3_check_fh_auth_status(cst, nfstat, is_write_op, erlabl)     \
        do {                                                            \
                int auth_ret = 0;                                       \
                int auth_errno = 0;                                     \
                xlator_t *xlatorp = NULL;                               \
                char buf[256], gfid[256];                               \
                rpc_transport_t *trans = NULL;                          \
                                                                        \
                auth_ret = auth_errno =                                 \
                        nfs3_fh_auth_nfsop (cst, is_write_op);          \
                if (auth_ret < 0) {                                     \
                        trans = rpcsvc_request_transport (cst->req);    \
                        xlatorp = nfs3_fh_to_xlator (cst->nfs3state,    \
                                                     &cst->resolvefh);  \
                        gf_uuid_unparse (cst->resolvefh.gfid, gfid);    \
                        sprintf (buf, "(%s) %s : %s",                   \
                                 trans->peerinfo.identifier,            \
                        xlatorp ? xlatorp->name : "ERR", gfid);         \
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0,               \
                                NFS_MSG_RESOLVE_FH_FAIL, "Unable to "   \
                                        "resolve FH: %s", buf);         \
                        nfstat = nfs3_errno_to_nfsstat3 (-auth_errno);  \
                        goto erlabl;                                    \
                }                                                       \
        } while (0)                                                     \

#define nfs3_check_fh_resolve_status(cst, nfstat, erlabl)               \
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
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0,               \
                                NFS_MSG_RESOLVE_STAT,                   \
                                "%s: %s", strerror(cst->resolve_errno), \
                                buf);                                   \
                        nfstat = nfs3_errno_to_nfsstat3 (cst->resolve_errno);\
                        goto erlabl;                                    \
                }                                                       \
        } while (0)                                                     \

#define nfs3_check_new_fh_resolve_status(cst, nfstat, erlabl)           \
        do {                                                            \
                xlator_t *xlatorp = NULL;                               \
                char buf[256], gfid[256];                               \
                rpc_transport_t *trans = NULL;                          \
                if (((cst)->resolve_ret < 0) &&                         \
                    ((cst)->resolve_errno != ENOENT)) {                 \
                        trans = rpcsvc_request_transport (cst->req);    \
                        xlatorp = nfs3_fh_to_xlator (cst->nfs3state,    \
                                                     &cst->resolvefh);  \
                        gf_uuid_unparse (cst->resolvefh.gfid, gfid);    \
                        snprintf (buf, sizeof (buf), "(%s) %s : %s",    \
                                  trans->peerinfo.identifier,           \
                                  xlatorp ? xlatorp->name : "ERR",      \
                                  gfid);                                \
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0,               \
                                NFS_MSG_RESOLVE_STAT, "%s: %s",         \
                                strerror(cst->resolve_errno), buf);     \
                        nfstat = nfs3_errno_to_nfsstat3 (cs->resolve_errno);\
                        goto erlabl;                                    \
                }                                                       \
        } while (0)                                                     \


int
__nfs3_get_volume_id (struct nfs3_state *nfs3, xlator_t *xl,
                      uuid_t volumeid)
{
        int                     ret = -1;
        struct nfs3_export      *exp = NULL;

        GF_VALIDATE_OR_GOTO (GF_NFS3, nfs3, out);
        GF_VALIDATE_OR_GOTO (GF_NFS3, xl, out);

        list_for_each_entry (exp, &nfs3->exports, explist) {
                if (exp->subvol == xl) {
                        gf_uuid_copy (volumeid, exp->volumeid);
                        ret = 0;
                        goto out;
                }
        }

out:
        return ret;
}


#define nfs3_funge_solaris_zerolen_fh(nfs3st, fhd, enam, nfsst, erl)    \
        do {                                                            \
                xlator_t        *fungexl = NULL;                        \
                uuid_t          zero = {0, };                           \
                fungexl =nfs_mntpath_to_xlator ((nfs3st)->exportslist,enam);\
                if (!fungexl) {                                         \
                        (nfsst) = NFS3ERR_NOENT;                        \
                        goto erl;                                       \
                }                                                       \
                                                                        \
                gf_uuid_copy ((fhd)->gfid, zero);                       \
                (fhd)->gfid[15] = 1;                                    \
                (enam) = NULL;                                          \
                if ((gf_nfs_dvm_off (nfs_state (nfs3st->nfsx))))        \
                        (fhd)->exportid[15] = nfs_xlator_to_xlid ((nfs3st)->exportslist, fungexl);                                                 \
                else {                                                  \
                        if(__nfs3_get_volume_id ((nfs3st), fungexl, (fhd)->exportid) < 0) { \
                                (nfsst) = NFS3ERR_STALE;                \
                                goto erl;                               \
                        }                                               \
                }                                                       \
        } while (0)                                                     \


#define nfs3_volume_started_check(nf3stt, vlm, rtval, erlbl)            \
        do {                                                            \
              if ((!nfs_subvolume_started (nfs_state (nf3stt->nfsx), vlm))){\
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0,               \
                                NFS_MSG_VOL_DISABLE,                    \
                                "Volume is disabled: %s",               \
                                vlm->name);                             \
                      rtval = RPCSVC_ACTOR_IGNORE;                      \
                      goto erlbl;                                       \
              }                                                         \
        } while (0)                                                     \


int
nfs3_export_sync_trusted (struct nfs3_state *nfs3, uuid_t exportid)
{
        struct nfs3_export      *exp = NULL;
        int                     ret = 0;

        GF_VALIDATE_OR_GOTO (GF_NFS3, nfs3, err);

        exp = __nfs3_get_export_by_exportid (nfs3, exportid);
        if (!exp)
                goto err;

        ret = exp->trusted_sync;
err:
        return ret;
}


int
nfs3_export_write_trusted (struct nfs3_state *nfs3, uuid_t exportid)
{
        struct nfs3_export      *exp = NULL;
        int                     ret = 0;

        GF_VALIDATE_OR_GOTO (GF_NFS3, nfs3, err);

        exp = __nfs3_get_export_by_exportid (nfs3, exportid);
        if (!exp)
                goto err;

        ret = exp->trusted_write;
err:
        return ret;
}

int
nfs3_solaris_zerolen_fh (struct nfs3_fh *fh, int fhlen)
{
        if (!fh)
                return 0;

        if (nfs3_fh_validate (fh))
                return 0;

        if (fhlen == 0)
                return 1;

        return 0;
}


/* Function pointer that represents the generic prototypes of functions used
 * to serialize NFS3 message structures into the XDR format.
 * For usage, see the nfs3svc_XXX_cbk functions.
 */
typedef ssize_t (*nfs3_serializer) (struct iovec outmsg, void *args);

nfs3_call_state_t *
nfs3_call_state_init (struct nfs3_state *s, rpcsvc_request_t *req, xlator_t *v)
{
        nfs3_call_state_t       *cs = NULL;

        GF_VALIDATE_OR_GOTO (GF_NFS3, s, err);
        GF_VALIDATE_OR_GOTO (GF_NFS3, req, err);
        GF_VALIDATE_OR_GOTO (GF_NFS3, v, err);

        cs = (nfs3_call_state_t *) mem_get (s->localpool);
        if (!cs) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "out of memory");
                return NULL;
        }

        memset (cs, 0, sizeof (*cs));
        INIT_LIST_HEAD (&cs->entries.list);
        INIT_LIST_HEAD (&cs->openwait_q);
        cs->operrno = EINVAL;
        cs->req = req;
        cs->vol = v;
        cs->nfsx = s->nfsx;
        cs->nfs3state = s;
err:
        return cs;
}

void
nfs3_call_state_wipe (nfs3_call_state_t *cs)
{
        if (!cs)
                return;

        if (cs->fd) {
                gf_msg_trace (GF_NFS3, 0, "fd 0x%lx ref: %d",
                        (long)cs->fd, cs->fd->refcount);
                fd_unref (cs->fd);
        }

        GF_FREE (cs->resolventry);

        GF_FREE (cs->pathname);

        if (!list_empty (&cs->entries.list))
                gf_dirent_free (&cs->entries);

        nfs_loc_wipe (&cs->oploc);
        nfs_loc_wipe (&cs->resolvedloc);
        if (cs->iob)
                iobuf_unref (cs->iob);
        if (cs->iobref)
                iobref_unref (cs->iobref);
        if (cs->trans)
                rpc_transport_unref (cs->trans);
        memset (cs, 0, sizeof (*cs));
        mem_put (cs);
        /* Already refd by fd_lookup, so no need to ref again. */
}


#define nfs3_handle_call_state_init(nfs3state, calls, rq, vl ,opstat, errlabel)\
        do {                                                            \
                calls = nfs3_call_state_init ((nfs3state), (rq), (vl)); \
                if (!calls) {                                           \
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0,               \
                                NFS_MSG_INIT_CALL_STAT_FAIL, "Failed to"\
                                " init call state");                    \
                        opstat = NFS3ERR_SERVERFAULT;                   \
                        goto errlabel;                                  \
                }                                                       \
        } while (0)                                                     \



struct iobuf *
nfs3_serialize_reply (rpcsvc_request_t *req, void *arg, nfs3_serializer sfunc,
                      struct iovec *outmsg)
{
        struct nfs3_state       *nfs3 = NULL;
        struct iobuf            *iob = NULL;
        ssize_t                 retlen = -1;

        nfs3 = (struct nfs3_state *)rpcsvc_request_program_private (req);
        if (!nfs3) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_STATE_MISSING,
                        "NFSv3 state not found in RPC request");
                goto ret;
        }

        /* First, get the io buffer into which the reply in arg will
         * be serialized.
         */
        /* TODO: get rid of 'sfunc' and use 'xdrproc_t' so we
           can have 'xdr_sizeof' */
        iob = iobuf_get (nfs3->iobpool);
        if (!iob) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to get iobuf");
                goto ret;
        }

        iobuf_to_iovec (iob, outmsg);
        /* Use the given serializer to translate the give C structure in arg
         * to XDR format which will be written into the buffer in outmsg.
         */
        /* retlen is used to received the error since size_t is unsigned and we
         * need -1 for error notification during encoding.
         */
        retlen = sfunc (*outmsg, arg);
        if (retlen == -1) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ENCODE_FAIL,
                        "Failed to encode message");
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



/* Generic reply function for NFSv3 specific replies. */
int
nfs3svc_submit_reply (rpcsvc_request_t *req, void *arg, nfs3_serializer sfunc)
{
        struct iovec            outmsg = {0, };
        struct iobuf            *iob = NULL;
        int                     ret = -1;
        struct iobref           *iobref = NULL;

        if (!req)
                return -1;

        iob = nfs3_serialize_reply (req, arg, sfunc, &outmsg);
        if (!iob) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_SERIALIZE_REPLY_FAIL,
                        "Failed to serialize reply");
                goto ret;
        }

        iobref = iobref_new ();
        if (!iobref) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "failed on iobref_new()");
                goto ret;
        }

        ret = iobref_add (iobref, iob);
        if (ret) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to add iob to iobref");
                goto ret;
        }

        /* Then, submit the message for transmission. */
        ret = rpcsvc_submit_message (req, &outmsg, 1, NULL, 0, iobref);
        if (ret == -1) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_SUBMIT_REPLY_FAIL,
                        "Reply submission failed");
                goto ret;
        }

        ret = 0;
ret:
        /* Now that we've done our job of handing the message to the RPC layer
         * we can safely unref the iob in the hope that RPC layer must have
         * ref'ed the iob on receiving into the txlist.
         */
        if (NULL != iob)
                iobuf_unref (iob);
        if (NULL != iobref)
                iobref_unref (iobref);
        return ret;
}


int
nfs3svc_submit_vector_reply (rpcsvc_request_t *req, void *arg,
                             nfs3_serializer sfunc, struct iovec *payload,
                             int vcount, struct iobref *iobref)
{
        struct iovec            outmsg = {0, };
        struct iobuf            *iob = NULL;
        int                     ret = -1;
        int                     new_iobref = 0;

        if (!req)
                return -1;

        iob = nfs3_serialize_reply (req, arg, sfunc, &outmsg);
        if (!iob) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_SERIALIZE_REPLY_FAIL,
                        "Failed to serialize reply");
                goto ret;
        }
        if (iobref == NULL) {
                iobref = iobref_new ();
                if (!iobref) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, ENOMEM,
                                NFS_MSG_NO_MEMORY, "failed on iobref_new");
                        goto ret;
                }
                new_iobref = 1;
        }

        ret = iobref_add (iobref, iob);
        if (ret) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to add iob to iobref");
                goto ret;
        }

        /* Then, submit the message for transmission. */
        ret = rpcsvc_submit_message (req, &outmsg, 1, payload, vcount, iobref);
        if (ret == -1) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_SUBMIT_REPLY_FAIL,
                        "Reply submission failed");
                goto ret;
        }

        ret = 0;
ret:
        /* Now that we've done our job of handing the message to the RPC layer
         * we can safely unref the iob in the hope that RPC layer must have
         * ref'ed the iob on receiving into the txlist.
         */
        if (NULL != iob)
                iobuf_unref (iob);
        if (new_iobref)
                iobref_unref (iobref);
        return ret;
}

uint64_t
nfs3_request_xlator_deviceid (rpcsvc_request_t *rq)
{
        struct nfs3_state       *nfs3 = NULL;
        xlator_t                *xl = NULL;
        uint64_t                devid = 0;
        uuid_t                  volumeid = {0, };

        if (!rq)
                return 0;

        xl = rpcsvc_request_private (rq);
        nfs3 = rpcsvc_request_program_private (rq);
        if (gf_nfs_dvm_off (nfs_state (nfs3->nfsx)))
                devid = (uint64_t)nfs_xlator_to_xlid (nfs3->exportslist, xl);
        else {
                __nfs3_get_volume_id (nfs3, xl, volumeid);
                memcpy (&devid, &volumeid[8], sizeof (devid));
        }

        return devid;
}


int
nfs3svc_null (rpcsvc_request_t *req)
{
        struct iovec    dummyvec = {0, };
        if (!req)
                return RPCSVC_ACTOR_ERROR;
        rpcsvc_submit_generic (req, &dummyvec, 1,  NULL, 0, NULL);
        return RPCSVC_ACTOR_SUCCESS;
}


int
nfs3_getattr_reply (rpcsvc_request_t *req, nfsstat3 status, struct iatt *buf)
{
        getattr3res     res;
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_getattr3res (&res, status, buf, deviceid);
        nfs3svc_submit_reply (req, &res,
                              (nfs3_serializer)xdr_serialize_getattr3res);

        return 0;
}


int32_t
nfs3svc_getattr_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno, inode_t *inode,
                            struct iatt *buf, dict_t *xattr,
                            struct iatt *postparent)
{
        nfsstat3                status = NFS3_OK;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;

        /*
         * Somewhat counter-intuitively, we don't need to look for sh-failed
         * here. Failing this getattr will generate a new lookup from the
         * client, and nfs_fop_lookup_cbk will detect any self-heal failures.
         */

        if (op_ret == -1) {
                status = nfs3_cbk_errno_status (op_ret, op_errno);
        }
        else {
                nfs_fix_generation(this,inode);
        }

        nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                             NFS3_GETATTR, status, op_errno,
                             cs->resolvedloc.path);

        nfs3_getattr_reply (cs->req, status, buf);
        nfs3_call_state_wipe (cs);

        return 0;
}


int32_t
nfs3svc_getattr_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, struct iatt *buf,
                          dict_t *xdata)
{
        nfsstat3                status = NFS3_OK;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;

        if (op_ret == -1) {
                status = nfs3_cbk_errno_status (op_ret, op_errno);
        }

        nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                             NFS3_GETATTR, status, op_errno,
                             cs->resolvedloc.path);

        nfs3_getattr_reply (cs->req, status, buf);
        nfs3_call_state_wipe (cs);

        return 0;
}


int
nfs3_getattr_resume (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs_user_t                      nfu = {0, };
        nfs3_call_state_t               *cs = NULL;
        uint64_t                         raw_ctx = 0;
        struct nfs_inode_ctx            *ictx = NULL;
        struct nfs_state                *priv = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_auth_status (cs, stat, _gf_false, nfs3err);
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        nfs_request_user_init (&nfu, cs->req);
        /* If inode which is to be getattr'd is the root, we need to do a
         * lookup instead because after a server reboot, it is not necessary
         * for the root to have been looked up when the getattr on the root is
         * sent. AND, this causes a problem for stat-prefetch in that it
         * expects even the root inode to have been looked up.

        if (__is_root_gfid (cs->resolvedloc.inode->gfid))
                ret = nfs_lookup (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                  nfs3svc_getattr_lookup_cbk, cs);
        else
                ret = nfs_stat (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
        */

        if (cs->hardresolved) {
                ret = -EFAULT;
                stat = NFS3_OK;
                goto nfs3err;
        }

        /*
         * If brick state changed, we need to force a proper lookup cycle (as
         * would happen in native protocol) to do self-heal checks. We detect
         * this by comparing the generation number for the last successful
         * creation/lookup on the inode to the current number, so inodes that
         * haven't been validated since the state change are affected.
         */
        if (inode_ctx_get(cs->resolvedloc.inode,cs->nfsx,&raw_ctx) == 0) {
                ictx = (struct nfs_inode_ctx *)raw_ctx;
                priv = cs->nfsx->private;
                if (ictx->generation != priv->generation) {
                        ret = nfs_lookup (cs->nfsx, cs->vol, &nfu,
                                          &cs->resolvedloc,
                                          nfs3svc_getattr_lookup_cbk, cs);
                        goto check_err;
                }
        }

        ret = nfs_stat (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                        nfs3svc_getattr_stat_cbk, cs);

check_err:
        if (ret < 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret, NFS_MSG_STAT_FOP_FAIL,
                        "Stat fop failed: %s: %s", cs->oploc.path,
                        strerror (-ret));
                stat = nfs3_errno_to_nfsstat3 (-ret);
        }

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_GETATTR, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_getattr_reply (cs->req, stat, &cs->stbuf);
                nfs3_call_state_wipe (cs);
                ret = 0;
        }

        return ret;
}


int
nfs3_getattr (rpcsvc_request_t *req, struct nfs3_fh *fh)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cstate = NULL;

        GF_VALIDATE_OR_GOTO (GF_NFS3, req, out);
        GF_VALIDATE_OR_GOTO (GF_NFS3, fh, out);

        nfs3_log_common_call (rpcsvc_request_xid (req), "GETATTR", fh);
        nfs3_validate_gluster_fh (fh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_map_fh_to_volume (nfs3, fh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_handle_call_state_init (nfs3, cstate, req, vol, stat, nfs3err);

        ret = nfs3_fh_resolve_and_resume (cstate, fh, NULL,nfs3_getattr_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_GETATTR, stat, -ret,
                                     NULL);
                nfs3_getattr_reply (req, stat, NULL);
                ret = 0;
                nfs3_call_state_wipe (cstate);
        }
out:
        return ret;
}


int
nfs3svc_getattr (rpcsvc_request_t *req)
{
        struct nfs3_fh          fh = {{0}, };
        getattr3args            args;
        int                     ret = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;

        nfs3_prep_getattr3args (&args, &fh);
        if (xdr_to_getattr3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_getattr (req, &fh);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret, NFS_MSG_GETATTR_FAIL,
                        "GETATTR procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


int
nfs3_setattr_reply (rpcsvc_request_t *req, nfsstat3 stat, struct iatt *preop,
                    struct iatt *postop)
{
        setattr3res     res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_setattr3res (&res, stat, preop, postop, deviceid);
        nfs3svc_submit_reply (req, (void *)&res,
                              (nfs3_serializer) xdr_serialize_setattr3res);
        return 0;
}


int32_t
nfs3svc_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        struct iatt             *prestat = NULL;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto nfs3err;
        }

        /* If the first stat was got from the guarded setattr callback, or
         * from an earlier setattr call then we'll need to use that stat
         * instead of the preop returned here.
         */
        if (cs->preparent.ia_ino != 0)
                prestat = &cs->preparent;
        else
                prestat = prebuf;

        stat = NFS3_OK;
nfs3err:
        nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                             NFS3_SETATTR, stat, op_errno,
                             cs->resolvedloc.path);
        nfs3_setattr_reply (cs->req, stat, prestat, postbuf);
        nfs3_call_state_wipe (cs);

        return 0;
}


int32_t
nfs3svc_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *preop,
                     struct iatt *postop, dict_t *xdata)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     ret = -1;
        struct iatt             *prebuf = NULL;
        nfs_user_t              nfu = {0, };
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto nfs3err;
        }

        prebuf = preop;
        /* Store the current preop in case we need to send a truncate,
         * in which case the preop to be returned will be this one.
         */
        cs->preparent = *preop;

        /* Only truncate if the size is not already same as the requested
         * truncation and also only if this is not a directory.
         */
        if ((gf_attr_size_set (cs->setattr_valid)) &&
            (!IA_ISDIR (postop->ia_type)) &&
            (preop->ia_size != cs->attr_in.ia_size)) {
                nfs_request_user_init (&nfu, cs->req);
                ret = nfs_truncate (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                    cs->attr_in.ia_size, nfs3svc_truncate_cbk,
                                    cs);

                if (ret < 0)
                        stat = nfs3_errno_to_nfsstat3 (-ret);
        } else {
                ret = -1;       /* Force a reply in the branch below. */
                stat = NFS3_OK;
        }

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_SETATTR, stat, op_errno,
                                     cs->resolvedloc.path);
                nfs3_setattr_reply (cs->req, stat, prebuf, postop);
                nfs3_call_state_wipe (cs);
        }

        return 0;
}



int32_t
nfs3svc_setattr_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, struct iatt *buf,
                          dict_t *xdata)
{

        int                     ret = -EFAULT;
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        nfs_user_t              nfu = {0, };
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto nfs3err;
        }

        if (buf->ia_ctime != cs->timestamp.seconds) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_TIMESTAMP_NO_SYNC,
                        "Timestamps not in sync");
                stat = NFS3ERR_NOT_SYNC;
                goto nfs3err;
        }

        /* Not a clean way but no motivation to add a new member to local. */
        cs->preparent = *buf;
        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_setattr (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,&cs->stbuf,
                           cs->setattr_valid, nfs3svc_setattr_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_SETATTR, stat, op_errno,
                                     cs->resolvedloc.path);
                nfs3_setattr_reply (cs->req, stat, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        return 0;
}


int
nfs3_setattr_resume (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs_user_t                      nfu = {0, };
        nfs3_call_state_t               *cs = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_setattr (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                           &cs->attr_in, cs->setattr_valid,
                           nfs3svc_setattr_cbk, cs);

        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_SETATTR, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_setattr_reply (cs->req, stat, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}


int
nfs3_setattr (rpcsvc_request_t *req, struct nfs3_fh *fh, sattr3 *sattr,
              sattrguard3 *guard)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;

        GF_VALIDATE_OR_GOTO (GF_NFS3, req, out);
        GF_VALIDATE_OR_GOTO (GF_NFS3, fh, out);
        GF_VALIDATE_OR_GOTO (GF_NFS3, sattr, out);
        GF_VALIDATE_OR_GOTO (GF_NFS3, guard, out);

        nfs3_log_common_call (rpcsvc_request_xid (req), "SETATTR", fh);
        nfs3_validate_gluster_fh (fh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_map_fh_to_volume (nfs3, fh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_check_rw_volaccess (nfs3, fh->exportid, stat, nfs3err);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);

        cs->setattr_valid = nfs3_sattr3_to_setattr_valid (sattr, &cs->attr_in,
                                                          NULL);
        if (guard->check) {
                gf_msg_trace (GF_NFS3, 0, "Guard check required");
                cs->timestamp = guard->sattrguard3_u.obj_ctime;
                cs->sattrguardcheck = 1;
        } else {
                gf_msg_trace (GF_NFS3, 0, "Guard check not required");
                cs->sattrguardcheck = 0;
        }

        if (!cs->setattr_valid) {
                ret = -EINVAL;  /* Force a reply */
                stat = NFS3_OK;
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_SETATTR_INVALID,
                        "cs->setattr_valid is invalid");
                goto nfs3err;
        }

        ret = nfs3_fh_resolve_and_resume (cs, fh, NULL, nfs3_setattr_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_SETATTR, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_setattr_reply (req, stat, NULL, NULL);
                nfs3_call_state_wipe (cs);
                /* Ret must be 0 after this so that the caller does not
                 * also send an RPC reply.
                 */
                ret = 0;
        }
out:
        return ret;
}



int
nfs3svc_setattr (rpcsvc_request_t *req)
{
        struct nfs3_fh          fh = {{0}, };
        setattr3args            args;
        int                     ret = RPCSVC_ACTOR_ERROR;

        GF_VALIDATE_OR_GOTO (GF_NFS3, req, rpcerr);

        nfs3_prep_setattr3args (&args, &fh);
        if (xdr_to_setattr3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0,
                        NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_setattr (req, &fh, &args.new_attributes, &args.guard);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret, NFS_MSG_SETATTR_FAIL,
                        "SETATTR procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;

}


int
nfs3_lookup_reply (rpcsvc_request_t *req, nfsstat3 stat, struct nfs3_fh *newfh,
                   struct iatt *stbuf, struct iatt *postparent)
{
        lookup3res      res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_lookup3res (&res, stat, newfh, stbuf, postparent, deviceid);
        return nfs3svc_submit_reply (req, &res,
                                     (nfs3_serializer)xdr_serialize_lookup3res);
}

int
nfs3_lookup_resume (void *carg);


int
nfs3_fresh_lookup (nfs3_call_state_t *cs)
{
        int     ret = -EFAULT;
        char    *oldresolventry = NULL;

        GF_VALIDATE_OR_GOTO (GF_NFS3, cs, err);
        gf_msg_debug (GF_NFS3, 0, "inode needs fresh lookup");
        inode_unlink (cs->resolvedloc.inode, cs->resolvedloc.parent,
                      cs->resolventry);
        nfs_loc_wipe (&cs->resolvedloc);

        /* Store pointer to currently allocated resolventry because it gets over
         * written in fh_resolve_and_resume.
         */
        oldresolventry = cs->resolventry;
        cs->lookuptype = GF_NFS3_FRESH;
        ret = nfs3_fh_resolve_and_resume (cs, &cs->resolvefh, cs->resolventry,
                                          nfs3_lookup_resume);
        /* Allocated in the previous call to fh_resolve_and_resume using the
         * same call_state.
         */
        GF_FREE (oldresolventry);
err:
        return ret;
}

int
nfs3svc_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, dict_t *xattr, struct iatt *postparent)
{
        struct nfs3_fh                  newfh = {{0}, };
        nfsstat3                        status = NFS3_OK;
        nfs3_call_state_t               *cs = NULL;
        inode_t                         *oldinode = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                status = nfs3_cbk_errno_status (op_ret, op_errno);
                goto xmit_res;
        }

        nfs3_fh_build_child_fh (&cs->parent, buf, &newfh);
        oldinode = inode_link (inode, cs->resolvedloc.parent,
                               cs->resolvedloc.name, buf);
xmit_res:
        /* Only send fresh lookup if it was a revalidate that failed. */
        if ((op_ret ==  -1) && (nfs3_is_revalidate_lookup (cs))) {
                op_ret = nfs3_fresh_lookup (cs);
                goto out;
        }

        nfs3_log_newfh_res (rpcsvc_request_xid (cs->req), NFS3_LOOKUP,
                            status, op_errno, &newfh,
                            cs->resolvedloc.path);
        nfs3_lookup_reply (cs->req, status, &newfh, buf, postparent);
        nfs3_call_state_wipe (cs);
out:
        if (oldinode) {
                inode_lookup (oldinode);
                inode_unref (oldinode);
        }
        return 0;
}


int
nfs3svc_lookup_parentdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                              int32_t op_ret, int32_t op_errno, inode_t *inode,
                              struct iatt *buf, dict_t *xattr,
                              struct iatt *postparent)
{
        struct nfs3_fh                  newfh = {{0}, };
        nfsstat3                        status = NFS3_OK;
        nfs3_call_state_t               *cs = NULL;
        uuid_t                          volumeid = {0, };
        uuid_t                          mountid = {1, };
        struct nfs3_state               *nfs3 = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                status = nfs3_cbk_errno_status (op_ret, op_errno);
                goto xmit_res;
        }

        nfs3 = cs->nfs3state;
        /* If the buf inode shows that this is a root dir's buf, then the file
         * handle needs to be specially crafted, in all other cases, we'll just
         * create the handle normally using the buffer of the parent dir.
         */
        if (buf->ia_ino != 1) {
                nfs3_fh_build_parent_fh (&cs->fh, buf, &newfh);
                goto xmit_res;
        }

        if (gf_nfs_dvm_off (nfs_state (nfs3->nfsx)))
                newfh = nfs3_fh_build_indexed_root_fh (nfs3->exportslist,
                                                       cs->vol);
        else {
                __nfs3_get_volume_id (nfs3, cs->vol, volumeid);
                newfh = nfs3_fh_build_uuid_root_fh (volumeid, mountid);
        }

xmit_res:
        nfs3_log_newfh_res (rpcsvc_request_xid (cs->req), NFS3_LOOKUP,
                            status, op_errno, &newfh,
                            cs->resolvedloc.path);
        nfs3_lookup_reply (cs->req, status, &newfh, buf, postparent);
        nfs3_call_state_wipe (cs);

        return 0;
}



int
nfs3_lookup_parentdir_resume (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs_user_t                      nfu = {0, };
        nfs3_call_state_t               *cs = NULL;
        inode_t                         *parent = NULL;

        if (!carg) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Invalid argument, carg value NULL");
                return EINVAL;
        }

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_auth_status (cs, stat, _gf_false, nfs3err);
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);

        /* At this point now, the loc in cs is for the directory file handle
         * sent by the client. This loc needs to be transformed into a loc that
         * represents the parent dir of cs->resolvedloc.inode.
         *
         * EXCEPT in the case where the .. is a parent of the root directory.
         * In this case we'll be returning the file handle and attributes of the
         * root itself.
         */
        nfs_request_user_init (&nfu, cs->req);

        /* Save the file handle from the LOOKUP request. We'll use this to
         * build the file handle of the parent directory in case the parent is
         * not root dir.
         */
        cs->fh = cs->resolvefh;

        /* If fh is that of the root, the resolvedloc will already contain
         * the loc for root. After that, we'll send lookup for the root dir
         * itself since we cannot send the lookup on the parent of root.
         *
         * For all other cases, we'll send the lookup on the parent of the
         * given directory file handle.
         */
        if (!nfs3_fh_is_root_fh (&cs->fh)) {
                parent = inode_ref (cs->resolvedloc.parent);
                nfs_loc_wipe (&cs->resolvedloc);
                ret = nfs_inode_loc_fill (parent, &cs->resolvedloc,
                                          NFS_RESOLVE_CREATE);

                if (ret < 0) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, -ret,
                                NFS_MSG_INODE_LOC_FILL_ERROR,
                                "nfs_inode_loc_fill"
                                " error");
                        goto errtostat;
                }
        }

        ret = nfs_lookup (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                          nfs3svc_lookup_parentdir_cbk, cs);
errtostat:
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_LOOKUP, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_lookup_reply (cs->req, stat, NULL, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        if (parent)
                inode_unref (parent);

        return ret;
}


int
nfs3_lookup_resume (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs_user_t                      nfu = {0, };
        nfs3_call_state_t               *cs = NULL;
        struct nfs3_fh                  newfh = {{0},};

        if (!carg) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Invalid argument, carg value NULL");
                return EINVAL;
        }

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_auth_status (cs, stat, _gf_false, nfs3err);
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        cs->parent = cs->resolvefh;

	if (cs->hardresolved) {
		stat = NFS3_OK;
		nfs3_fh_build_child_fh (&cs->parent, &cs->stbuf, &newfh);
		goto nfs3err;
	}

        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_lookup (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                          nfs3svc_lookup_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_LOOKUP, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_lookup_reply (cs->req, stat, &newfh, &cs->stbuf,
                                   &cs->postparent);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}


int
nfs3_lookup (rpcsvc_request_t *req, struct nfs3_fh *fh, int fhlen, char *name)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;

        GF_VALIDATE_OR_GOTO (GF_NFS3, req, out);
        GF_VALIDATE_OR_GOTO (GF_NFS3, fh, out);
        GF_VALIDATE_OR_GOTO (GF_NFS3, name, out);

        nfs3_log_fh_entry_call (rpcsvc_request_xid (req), "LOOKUP", fh,
                                name);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        if (nfs3_solaris_zerolen_fh (fh, fhlen))
                nfs3_funge_solaris_zerolen_fh (nfs3, fh, name, stat, nfs3err);
        else
                nfs3_validate_gluster_fh (fh, stat, nfs3err);
        nfs3_validate_strlen_or_goto (name, NFS_NAME_MAX, nfs3err, stat, ret);
        nfs3_map_fh_to_volume (nfs3, fh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);

        cs->lookuptype = GF_NFS3_REVALIDATE;
        ret = nfs3_fh_resolve_and_resume (cs, fh, name,
                                          nfs3_lookup_resume);

        if (ret < 0) {
                gf_msg (GF_NFS, GF_LOG_ERROR, -ret,
                        NFS_MSG_HARD_RESOLVE_FAIL,
                        "failed to start hard resolve");
                stat = nfs3_errno_to_nfsstat3 (-ret);
        }

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_LOOKUP, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_lookup_reply (req, stat, NULL, NULL, NULL);
                nfs3_call_state_wipe (cs);
                /* Ret must be 0 after this so that the caller does not
                 * also send an RPC reply.
                 */
                ret = 0;
        }
out:
        return ret;
}


int
nfs3svc_lookup (rpcsvc_request_t *req)
{
        char                    name[NFS_PATH_MAX];
        struct nfs3_fh          fh = {{0}, };
        lookup3args             args;
        int                     ret = RPCSVC_ACTOR_ERROR;

        GF_VALIDATE_OR_GOTO (GF_NFS, req, rpcerr);

        nfs3_prep_lookup3args (&args, &fh, name);
        if (xdr_to_lookup3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_lookup (req, &fh, args.what.dir.data.data_len, name);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret,
                        NFS_MSG_LOOKUP_PROC_FAIL,
                        "LOOKUP procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


int
nfs3_access_reply (rpcsvc_request_t *req, nfsstat3 status, int32_t accbits,
		   int32_t reqaccbits)
{
        access3res      res;

        nfs3_fill_access3res (&res, status, accbits, reqaccbits);
        nfs3svc_submit_reply (req, &res,
                              (nfs3_serializer)xdr_serialize_access3res);
        return 0;
}


int32_t
nfs3svc_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        nfsstat3                status = NFS3_OK;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;

        if (op_ret == -1) {
                status = nfs3_cbk_errno_status (op_ret, op_errno);
        }

        nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                             NFS3_ACCESS, status, op_errno,
                             cs->resolvedloc.path);
        nfs3_access_reply (cs->req, status, op_errno, cs->accessbits);
        nfs3_call_state_wipe (cs);

        return 0;
}

int
nfs3_access_resume (void *carg)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     ret = -EFAULT;
        nfs_user_t              nfu = {0, };
        nfs3_call_state_t       *cs = NULL;

        if (!carg) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Invalid argument, carg value NULL");
                return EINVAL;
        }

        cs = (nfs3_call_state_t *)carg;

        /* Additional checks on the NFS file handle
         * go here. The path for an NFS ACCESS call
         * goes like this:
         * nfs3_access -> nfs3_fh_resolve_and_resume -> nfs3_resolve_resume ->
         * nfs3_access_resume -> <macro/function performs check on FH> ->
         * <continue or return from function based on check.> ('goto nfs3err'
         * terminates this function and writes the appropriate response to the
         * client). It is important that you do NOT stick any sort of check
         * on the file handle outside of the nfs3_##OP_resume functions.
         */
        nfs3_check_fh_auth_status (cs, stat, _gf_false, nfs3err);
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        cs->fh = cs->resolvefh;
        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_access (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                          cs->accessbits, nfs3svc_access_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_ACCESS, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_access_reply (cs->req, stat, 0, 0);
                nfs3_call_state_wipe (cs);
                ret = 0;
        }

        return ret;
}


int
nfs3_access (rpcsvc_request_t *req, struct nfs3_fh *fh, uint32_t accbits)
{
        xlator_t                *vol = NULL;
        struct nfs3_state       *nfs3 = NULL;
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     ret = -EFAULT;
        nfs3_call_state_t       *cs = NULL;

        GF_VALIDATE_OR_GOTO (GF_NFS, req, out);
        GF_VALIDATE_OR_GOTO (GF_NFS, fh, out);
        nfs3_log_common_call (rpcsvc_request_xid (req), "ACCESS", fh);
        nfs3_validate_gluster_fh (fh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_map_fh_to_volume (nfs3, fh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);
        cs->accessbits = accbits;

        ret = nfs3_fh_resolve_and_resume (cs, fh, NULL, nfs3_access_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_ACCESS, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_access_reply (req, stat, 0, 0);
                nfs3_call_state_wipe (cs);
                ret = 0;
        }
out:
        return ret;
}


int
nfs3svc_access (rpcsvc_request_t *req)
{
        struct nfs3_fh  fh = {{0}, };
        access3args     args;
        int             ret = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;

        nfs3_prep_access3args (&args, &fh);
        if (xdr_to_access3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_access (req, &fh, args.access);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret,
                        NFS_MSG_ACCESS_PROC_FAIL,
                        "ACCESS procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


int
nfs3_readlink_reply (rpcsvc_request_t *req, nfsstat3 stat, char *path,
                     struct iatt *buf)
{
        readlink3res    res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_readlink3res (&res, stat, path, buf, deviceid);
        nfs3svc_submit_reply (req, (void *)&res,
                              (nfs3_serializer)xdr_serialize_readlink3res);

        return 0;
}


int32_t
nfs3svc_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, const char *path,
                      struct iatt *buf, dict_t *xdata)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto nfs3err;
        }

        stat = NFS3_OK;

nfs3err:
        nfs3_log_readlink_res (rpcsvc_request_xid (cs->req),
                               stat, op_errno, (char *)path,
                               cs->resolvedloc.path);
        nfs3_readlink_reply (cs->req, stat, (char *)path, buf);
        nfs3_call_state_wipe (cs);

        return 0;
}


int
nfs3_readlink_resume (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs3_call_state_t               *cs = NULL;
        nfs_user_t                      nfu = {0, };

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_auth_status (cs, stat, _gf_false, nfs3err);
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_readlink (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                            nfs3svc_readlink_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_READLINK, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_readlink_reply (cs->req, stat, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}


int
nfs3_readlink (rpcsvc_request_t *req, struct nfs3_fh *fh)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;

        if ((!req) || (!fh)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Bad arguments");
                return -1;
        }

        nfs3_log_common_call (rpcsvc_request_xid (req), "READLINK", fh);
        nfs3_validate_gluster_fh (fh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_map_fh_to_volume (nfs3, fh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);

        ret = nfs3_fh_resolve_and_resume (cs, fh, NULL, nfs3_readlink_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_READLINK, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_readlink_reply (req, stat, NULL, NULL);
                nfs3_call_state_wipe (cs);
                /* Ret must be 0 after this so that the caller does not
                 * also send an RPC reply.
                 */
                ret = 0;
        }
out:
        return ret;
}


int
nfs3svc_readlink (rpcsvc_request_t *req)
{
        struct nfs3_fh          fh = {{0}, };
        readlink3args           args;
        int                     ret = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;

        nfs3_prep_readlink3args (&args, &fh);
        if (xdr_to_readlink3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_readlink (req, &fh);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret,
                        NFS_MSG_READLINK_PROC_FAIL,
                        "READLINK procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


int
nfs3_read_reply (rpcsvc_request_t *req, nfsstat3 stat, count3 count,
                 struct iovec *vec, int vcount, struct iobref *iobref,
                 struct iatt *poststat, int is_eof)
{
        read3res                res = {0, };
        uint64_t                deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_read3res (&res, stat, count, poststat, is_eof, deviceid);
        if (stat == NFS3_OK) {
                xdr_vector_round_up (vec, vcount, count);
                /* iob can be zero if the file size was zero. If so, op_ret
                 * would be 0 and count = 0.
                 */

                if (count != 0) {
                        nfs3svc_submit_vector_reply (req, (void *)&res,
                                                     (nfs3_serializer)
                                                  xdr_serialize_read3res_nocopy,
                                                    vec, vcount, iobref);
                } else

                nfs3svc_submit_reply (req, (void *)&res,
                                              (nfs3_serializer)
                                              xdr_serialize_read3res_nocopy);
        } else
                nfs3svc_submit_reply (req, (void *)&res,
                                      (nfs3_serializer)
                                      xdr_serialize_read3res_nocopy);

        return 0;
}


int32_t
nfs3svc_read_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iovec *vector,
                  int32_t count, struct iatt *stbuf, struct iobref *iobref,
                  dict_t *xdata)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     is_eof = 0;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto err;
        } else
                stat = NFS3_OK;

        if (op_errno == ENOENT)
                is_eof = 1;

err:
        nfs3_log_read_res (rpcsvc_request_xid (cs->req),
                           stat, op_errno,
                           op_ret, is_eof, vector, count,
                           cs->resolvedloc.path);
        nfs3_read_reply (cs->req, stat, op_ret, vector, count, iobref, stbuf,
                         is_eof);
        nfs3_call_state_wipe (cs);

        return 0;
}


int
nfs3_read_fd_resume (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs_user_t                      nfu = {0, };
        nfs3_call_state_t               *cs = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_read (cs->nfsx, cs->vol, &nfu, cs->fd, cs->datacount,
                        cs->dataoffset, nfs3svc_read_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);
nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_READ, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_read_reply (cs->req, stat, 0, NULL, 0, NULL, NULL, 0);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}


int
nfs3_read_resume (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs3_call_state_t               *cs = NULL;
        fd_t                            *fd = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_auth_status (cs, stat, _gf_false, nfs3err);
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        fd = fd_anonymous (cs->resolvedloc.inode);
        if (!fd) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ANONYMOUS_FD_FAIL,
                        "Failed to create anonymous fd");
                goto nfs3err;
        }

        cs->fd = fd;
        nfs3_read_fd_resume (cs);
        ret = 0;
nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_READ, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_read_reply (cs->req, stat, 0, NULL,0, NULL, NULL, 0);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}

int
nfs3_read (rpcsvc_request_t *req, struct nfs3_fh *fh, offset3 offset,
           count3 count)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;

        if ((!req) || (!fh)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Bad arguments");
                return -1;
        }

        nfs3_log_rw_call (rpcsvc_request_xid (req), "READ", fh, offset,
                          count, -1);
        nfs3_validate_gluster_fh (fh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_map_fh_to_volume (nfs3, fh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);

        cs->datacount = count;
        cs->dataoffset = offset;
        ret = nfs3_fh_resolve_and_resume (cs, fh, NULL, nfs3_read_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_READ, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_read_reply (req, stat, 0, NULL,0, NULL, NULL, 0);
                nfs3_call_state_wipe (cs);
                ret = 0;
        }
out:
        return ret;
}


int
nfs3svc_read (rpcsvc_request_t *req)
{
        struct nfs3_fh          fh = {{0}, };
        read3args               args;
        int                     ret = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;

        nfs3_prep_read3args (&args, &fh);
        if (xdr_to_read3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_read (req, &fh, args.offset, args.count);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret,
                        NFS_MSG_READ_FAIL,
                        "READ procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


int
nfs3_write_reply (rpcsvc_request_t *req, nfsstat3 stat, count3 count,
                  stable_how stable, uint64_t wverf, struct iatt *prestat,
                  struct iatt *poststat)
{
        write3res       res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_write3res (&res, stat, count, stable, wverf, prestat,
                             poststat, deviceid);
        nfs3svc_submit_reply (req, (void *)&res,
                              (nfs3_serializer)xdr_serialize_write3res);

        return 0;
}

int32_t
nfs3svc_write_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                         struct iatt *postbuf, dict_t *xdata)
{
        struct nfs3_state       *nfs3 = NULL;
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        nfs3 = rpcsvc_request_program_private (cs->req);

        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
        } else
                stat = NFS3_OK;

        nfs3_log_write_res (rpcsvc_request_xid (cs->req),
                            stat, op_errno,
                            cs->maxcount, cs->writetype, nfs3->serverstart,
                            cs->resolvedloc.path);
        nfs3_write_reply (cs->req, stat, cs->maxcount, cs->writetype,
                          nfs3->serverstart, &cs->stbuf, postbuf);
        nfs3_call_state_wipe (cs);
        return 0;
}



/*
 * Before going into the write reply logic, here is a matrix that shows the
 * requirements for a write reply as given by RFC1813.
 *
 * Requested Write Type ||      Possible Returns
 * ==============================================
 * FILE_SYNC            ||      FILE_SYNC
 * DATA_SYNC            ||      DATA_SYNC or FILE_SYNC
 * UNSTABLE             ||      DATA_SYNC or FILE_SYNC or UNSTABLE
 *
 * Write types other than UNSTABLE are together called STABLE.
 * RS - Return Stable
 * RU - Return Unstable
 * WS - Write Stable
 * WU - Write Unstable
 *
 *+============================================+
 *| Vol Opts -> || trusted-write| trusted-sync |
 *| Write Type  ||              |              |
 *|-------------||--------------|--------------|
 *| STABLE      ||      WS      |   WU         |
 *|             ||      RS      |   RS         |
 *|-------------||--------------|--------------|
 *| UNSTABLE    ||      WU      |   WU         |
 *|             ||      RS      |   RS         |
 *|-------------||--------------|--------------|
 *| COMMIT      ||    fsync     | getattr      |
 *+============================================+
 *
 *
 */
int32_t
nfs3svc_write_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t       *cs = NULL;
        struct nfs3_state       *nfs3 = NULL;

        cs = frame->local;
        nfs3 = rpcsvc_request_program_private (cs->req);
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto err;
        }

        stat = NFS3_OK;
        cs->maxcount = op_ret;

err:
	nfs3_log_write_res (rpcsvc_request_xid (cs->req),
                            stat, op_errno, cs->maxcount, cs->writetype,
			    nfs3->serverstart, cs->resolvedloc.path);
	nfs3_write_reply (cs->req, stat, cs->maxcount,
			  cs->writetype, nfs3->serverstart, prebuf,
			  postbuf);
	nfs3_call_state_wipe (cs);

        return 0;
}


int
__nfs3_write_resume (nfs3_call_state_t *cs)
{
        int                             ret = -EFAULT;
        nfs_user_t                      nfu = {0, };

        if (!cs)
                return ret;

        nfs_request_user_init (&nfu, cs->req);
        /* It is possible that the RPC record contains more bytes than
         * than the size of write requested in this request. This means,
         * that in the RPC message buffer, there could be more bytes
         * beyind the @count bytes. Since @payload is referring to the write
         * data directly inside the RPC request buffer(..since we performed a
         * no-copy deXDRing..), we might end up writing more data than
         * requested, because till now payload.iov_len accounts for all the
         * bytes not just the write request bytes. These extra bytes are present
         * as a requirement of the XDR encoding to round up the all string and
         * opaque data buffers to multiples of 4 bytes.
         */
        cs->datavec.iov_len = cs->datacount;
        ret = nfs_write (cs->nfsx, cs->vol, &nfu, cs->fd, cs->iobref,
                         &cs->datavec, 1, cs->dataoffset, nfs3svc_write_cbk,
                         cs);

        return ret;
}


int
nfs3_write_resume (void *carg)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     ret = -EFAULT;
        nfs3_call_state_t       *cs = NULL;
        fd_t                    *fd = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_auth_status (cs, stat, _gf_true, nfs3err);
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        fd = fd_anonymous (cs->resolvedloc.inode);
        if (!fd) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ANONYMOUS_FD_FAIL,
                        "Failed to create anonymous fd");
                goto nfs3err;
        }

        cs->fd = fd;    /* Gets unrefd when the call state is wiped. */

        ret = __nfs3_write_resume (cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);
nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_WRITE, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_write_reply (cs->req, stat, 0, cs->writetype, 0, NULL,
                                  NULL);
                nfs3_call_state_wipe (cs);
        }
        return ret;
}


int
nfs3_write (rpcsvc_request_t *req, struct nfs3_fh *fh, offset3 offset,
            count3 count, stable_how stable, struct iovec payload,
            struct iobref *iobref)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;

        if ((!req) || (!fh) || (!payload.iov_base)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Bad arguments");
                return -1;
        }

        nfs3_log_rw_call (rpcsvc_request_xid (req), "WRITE", fh, offset,
                          count, stable);
        nfs3_validate_gluster_fh (fh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_map_fh_to_volume (nfs3, fh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_check_rw_volaccess (nfs3, fh->exportid, stat, nfs3err);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);
        cs->datacount = count;
        cs->dataoffset = offset;
        cs->writetype = stable;
        cs->iobref = iobref;
        cs->datavec = payload;

        ret = nfs3_fh_resolve_and_resume (cs, fh, NULL, nfs3_write_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_WRITE, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_write_reply (req, stat, 0, stable, 0, NULL, NULL);
                nfs3_call_state_wipe (cs);
                ret = 0;
        }
out:
        return ret;
}

#define NFS3_VECWRITE_READFHLEN         1
#define NFS3_VECWRITE_READFH            2
#define NFS3_VECWRITE_READREST          3

#define NFS3_WRITE_POSTFH_SIZE          20


int
nfs3svc_write_vecsizer (int state, ssize_t *readsize, char *base_addr,
                        char *curr_addr)
{
        int      ret     = 0;
        uint32_t fhlen   = 0;
        uint32_t fhlen_n = 0;

        if (state == 0) {
                ret = NFS3_VECWRITE_READFHLEN;
                *readsize = 4;
        } else if (state == NFS3_VECWRITE_READFHLEN) {
                fhlen_n = *(uint32_t *)(curr_addr - 4);
                fhlen = ntohl (fhlen_n);
                *readsize = xdr_length_round_up (fhlen, NFS3_FHSIZE);
                ret = NFS3_VECWRITE_READFH;
        } else if (state == NFS3_VECWRITE_READFH) {
                *readsize = NFS3_WRITE_POSTFH_SIZE;
                ret = NFS3_VECWRITE_READREST;
        } else if (state == NFS3_VECWRITE_READREST) {
                ret = 0;
                *readsize = 0;
        } else
                gf_msg ("nfs", GF_LOG_ERROR, 0, NFS_MSG_STATE_WRONG,
                        "state wrong");

        return ret;
}


int
nfs3svc_write (rpcsvc_request_t *req)
{
        struct nfs3_fh          fh = {{0}, };
        write3args              args;
        int                     ret = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;
        nfs3_prep_write3args (&args, &fh);
        if (xdr_to_write3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        /* To ensure that the iobuf for the current record does not
         * get returned to the iobpool, we need to keep a reference for
         * ourselves because the RPC call handler who called us will unref its
         * own ref of the record's iobuf when it is done handling the request.
         */

        ret = nfs3_write (req, &fh, args.offset, args.count, args.stable,
                          req->msg[1], rpcsvc_request_iobref_ref (req));
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret,
                        NFS_MSG_WRITE_FAIL,
                        "WRITE procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


int
nfs3_create_reply (rpcsvc_request_t *req, nfsstat3 stat, struct nfs3_fh *newfh,
                   struct iatt *newbuf, struct iatt *preparent,
                   struct iatt *postparent)
{
        create3res      res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_create3res (&res, stat, newfh, newbuf, preparent, postparent,
                              deviceid);
        nfs3svc_submit_reply (req, (void *)&res,
                              (nfs3_serializer)xdr_serialize_create3res);
        return 0;
}


int32_t
nfs3svc_create_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno,
                            struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto nfs3err;
        }

        stat = NFS3_OK;
nfs3err:
        nfs3_log_newfh_res (rpcsvc_request_xid (cs->req),
                            NFS3_CREATE, stat, op_errno,
                            &cs->fh, cs->resolvedloc.path);
        nfs3_create_reply (cs->req, stat, &cs->fh, postop, &cs->preparent,
                           &cs->postparent);
        nfs3_call_state_wipe (cs);

        return 0;
}


int32_t
nfs3svc_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     ret = -EFAULT;
        nfs_user_t              nfu = {0, };
        nfs3_call_state_t       *cs = NULL;
        inode_t                 *oldinode = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto nfs3err;
        }

        nfs3_fh_build_child_fh (&cs->parent, buf, &cs->fh);
        oldinode = inode_link (inode, cs->resolvedloc.parent,
                               cs->resolvedloc.name, buf);

        /* Means no attributes were required to be set. */
        if (!cs->setattr_valid) {
                stat = NFS3_OK;
                ret = -1;
                goto nfs3err;
        }

        cs->preparent = *preparent;
        cs->postparent = *postparent;
        nfs_request_user_init (&nfu, cs->req);
        gf_uuid_copy (cs->resolvedloc.gfid, inode->gfid);
        ret = nfs_setattr (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,&cs->stbuf,
                           cs->setattr_valid, nfs3svc_create_setattr_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (oldinode) {
                inode_lookup (oldinode);
                inode_unref (oldinode);
        }

        if (ret < 0) {
                nfs3_log_newfh_res (rpcsvc_request_xid (cs->req),
                                    NFS3_CREATE, stat, op_errno, &cs->fh,
                                    cs->resolvedloc.path);
                nfs3_create_reply (cs->req, stat, &cs->fh, buf, preparent,
                                   postparent);
                nfs3_call_state_wipe (cs);
        }

        return 0;
}

int
nfs3_create_common (nfs3_call_state_t *cs)
{
        int                     ret = -EFAULT;
        int                     flags = 0;
        nfs_user_t              nfu = {0, };
        uid_t                   uid = 0;
        gid_t                   gid = 0;

        if (!cs)
                return ret;

        if (cs->createmode == GUARDED)
                flags = (O_RDWR | O_EXCL);
        else
                flags = O_RDWR;

        if (gf_attr_uid_set (cs->setattr_valid)) {
                uid = cs->stbuf.ia_uid;
                cs->setattr_valid &= ~GF_SET_ATTR_UID;
        } else
                uid = rpcsvc_request_uid (cs->req);

        if (gf_attr_gid_set (cs->setattr_valid)) {
                gid = cs->stbuf.ia_gid;
                cs->setattr_valid &= ~GF_SET_ATTR_GID;
        } else
                gid = rpcsvc_request_gid (cs->req);

        nfs_request_primary_user_init (&nfu, cs->req, uid, gid);
        /* We can avoid sending the setattr call later if only the mode is
         * required to be set. This is possible because the create fop allows
         * us to specify a mode arg.
         */
        if (cs->setattr_valid & GF_SET_ATTR_MODE) {
                cs->setattr_valid &= ~GF_SET_ATTR_MODE;
                ret = nfs_create (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                  flags, cs->mode, nfs3svc_create_cbk, cs);
        } else
                ret = nfs_create (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                  flags, NFS_DEFAULT_CREATE_MODE,
                                  nfs3svc_create_cbk, cs);

        return ret;
}


int32_t
nfs3svc_create_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *buf,
                         dict_t *xdata)
{
        int                     ret = -EFAULT;
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        nfs_user_t              nfu = {0, };
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        nfs_request_user_init (&nfu, cs->req);
        if (op_ret == -1) {
                ret = -op_errno;
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto nfs3err;
        }

        if ((cs->stbuf.ia_mtime == buf->ia_mtime) &&
            (cs->stbuf.ia_atime == buf->ia_atime)) {
                gf_msg_debug (GF_NFS3, 0,
                        "Create req retransmitted verf %x %x",
                        cs->stbuf.ia_mtime, cs->stbuf.ia_atime);
                stat = NFS3_OK;
                nfs3_fh_build_child_fh (&cs->parent, buf, &cs->fh);
        } else {
                gf_msg_debug (GF_NFS3, 0,
                        "File already exist new_verf %x %x"
                        "old_verf %x %x", cs->stbuf.ia_mtime,
                        cs->stbuf.ia_atime,
                        buf->ia_mtime, buf->ia_atime);
                stat = NFS3ERR_EXIST;
        }

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_CREATE, stat, op_errno,
                                     cs->resolvedloc.path);
                nfs3_create_reply (cs->req, stat, &cs->fh, buf, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        return 0;
}


int
nfs3_create_exclusive (nfs3_call_state_t *cs)
{
        int                     ret = -EFAULT;
        nfs_user_t              nfu = {0, };

        if (!cs)
                return ret;

        /* Storing verifier as a mtime and atime attribute, to store it
         * in stable storage */
        memcpy (&cs->stbuf.ia_atime, &cs->cookieverf,
                sizeof (cs->stbuf.ia_atime));
        memcpy (&cs->stbuf.ia_mtime,
                ((char *) &cs->cookieverf) + sizeof (cs->stbuf.ia_atime),
                sizeof (cs->stbuf.ia_mtime));
        cs->setattr_valid |= GF_SET_ATTR_ATIME;
        cs->setattr_valid |= GF_SET_ATTR_MTIME;
        nfs_request_user_init (&nfu, cs->req);

        /* If the file already existed we need to get that attributes so we can
         * compare and check whether a previous create operation was
         * interrupted due to server failure or dropped packets.
         */
        if ((cs->resolve_ret == 0) ||
            ((cs->resolve_ret == -1) && (cs->resolve_errno != ENOENT))) {
                ret = nfs_stat (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                nfs3svc_create_stat_cbk, cs);
                goto nfs3err;
        }

        ret = nfs3_create_common (cs);
nfs3err:
        return ret;
}


int
nfs3_create_resume (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs3_call_state_t               *cs = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_auth_status (cs, stat, _gf_true, nfs3err);
        nfs3_check_new_fh_resolve_status (cs, stat, nfs3err);
        if (cs->createmode == EXCLUSIVE)
                ret = nfs3_create_exclusive (cs);
        else
                ret = nfs3_create_common (cs);

        /* Handle a failure return from either of the create functions above. */
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_CREATE, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_create_reply (cs->req, stat, NULL, NULL, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}

int
nfs3_create (rpcsvc_request_t *req, struct nfs3_fh *dirfh, char *name,
             createmode3 mode, sattr3 *sattr, uint64_t cverf)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;

        if ((!req) || (!dirfh) || (!name) || (!sattr))
                return -1;

        nfs3_log_create_call (rpcsvc_request_xid (req), dirfh, name, mode);
        nfs3_validate_gluster_fh (dirfh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_validate_strlen_or_goto (name, NFS_NAME_MAX, nfs3err, stat, ret);
        nfs3_map_fh_to_volume (nfs3, dirfh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_check_rw_volaccess (nfs3, dirfh->exportid, stat, nfs3err);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);

        cs->cookieverf = cverf;
        /*In Exclusive create client is supposed to send cverf instead of
         * sattr*/
        if (mode != EXCLUSIVE)
                cs->setattr_valid = nfs3_sattr3_to_setattr_valid (sattr,
                                                                  &cs->stbuf,
                                                                  &cs->mode);
        cs->createmode = mode;
        cs->parent = *dirfh;

        ret = nfs3_fh_resolve_and_resume (cs, dirfh, name, nfs3_create_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_CREATE, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_create_reply (req, stat, NULL, NULL, NULL, NULL);
                nfs3_call_state_wipe (cs);
                ret = 0;
        }
out:
        return ret;
}


int
nfs3svc_create (rpcsvc_request_t *req)
{
        char            name[NFS_PATH_MAX];
        struct nfs3_fh  dirfh = {{0}, };
        create3args     args;
        int             ret   = RPCSVC_ACTOR_ERROR;
        uint64_t        cverf = 0;
        uint64_t       *cval;

        if (!req)
                return ret;

        nfs3_prep_create3args (&args, &dirfh, name);
        if (xdr_to_create3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        cval = (uint64_t *)args.how.createhow3_u.verf;
        cverf = *cval;

        ret = nfs3_create (req, &dirfh, name, args.how.mode,
                           &args.how.createhow3_u.obj_attributes, cverf);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret, NFS_MSG_CREATE_FAIL,
                        "CREATE procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


int
nfs3_mkdir_reply (rpcsvc_request_t *req, nfsstat3 stat, struct nfs3_fh *fh,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent)
{
        mkdir3res       res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_mkdir3res (&res, stat, fh, buf, preparent, postparent,
                             deviceid);
        nfs3svc_submit_reply (req, &res,
                              (nfs3_serializer)xdr_serialize_mkdir3res);
        return 0;
}

int32_t
nfs3svc_mkdir_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno,
                           struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto nfs3err;
        }

        stat = NFS3_OK;
nfs3err:
        nfs3_log_newfh_res (rpcsvc_request_xid (cs->req),
                            NFS3_MKDIR, stat, op_errno, &cs->fh,
                            cs->resolvedloc.path);
        nfs3_mkdir_reply (cs->req, stat, &cs->fh, postop, &cs->preparent,
                          &cs->postparent);
        nfs3_call_state_wipe (cs);

        return 0;
}


int32_t
nfs3svc_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs_user_t                      nfu = {0, };
        nfs3_call_state_t               *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto nfs3err;
        }

        nfs3_fh_build_child_fh (&cs->parent, buf, &cs->fh);

        /* Means no attributes were required to be set. */
        if (!cs->setattr_valid) {
                stat = NFS3_OK;
                goto nfs3err;
        }

        cs->preparent = *preparent;
        cs->postparent = *postparent;
        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_setattr (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,&cs->stbuf,
                           cs->setattr_valid, nfs3svc_mkdir_setattr_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_newfh_res (rpcsvc_request_xid (cs->req),
                                    NFS3_MKDIR, stat, op_errno, &cs->fh,
                                    cs->resolvedloc.path);
                nfs3_mkdir_reply (cs->req, stat, &cs->fh, buf, preparent,
                                  postparent);
                nfs3_call_state_wipe (cs);
        }

        return 0;
}


int
nfs3_mkdir_resume (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs_user_t                      nfu = {0, };
        nfs3_call_state_t               *cs = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_new_fh_resolve_status (cs, stat, nfs3err);
        nfs_request_user_init (&nfu, cs->req);

        if (gf_attr_mode_set (cs->setattr_valid)) {
                cs->setattr_valid &= ~GF_SET_ATTR_MODE;
                ret = nfs_mkdir (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                 cs->mode, nfs3svc_mkdir_cbk, cs);
        } else
                ret = nfs_mkdir (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                 cs->mode, nfs3svc_mkdir_cbk, cs);

        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_MKDIR, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_mkdir_reply (cs->req, stat, NULL, NULL, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        return 0;
}



int
nfs3_mkdir (rpcsvc_request_t *req, struct nfs3_fh *dirfh, char *name,
            sattr3 *sattr)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;

        if ((!req) || (!dirfh) || (!name) || (!sattr)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Bad arguments");
                return -1;
        }

        nfs3_log_fh_entry_call (rpcsvc_request_xid (req), "MKDIR", dirfh,
                                name);
        nfs3_validate_gluster_fh (dirfh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_validate_strlen_or_goto (name, NFS_NAME_MAX, nfs3err, stat, ret);
        nfs3_map_fh_to_volume (nfs3, dirfh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_check_rw_volaccess (nfs3, dirfh->exportid, stat, nfs3err);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);

        cs->parent = *dirfh;
        cs->setattr_valid = nfs3_sattr3_to_setattr_valid (sattr, &cs->stbuf,
                                                          &cs->mode);
        ret = nfs3_fh_resolve_and_resume (cs, dirfh, name, nfs3_mkdir_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_MKDIR, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_mkdir_reply (req, stat, NULL, NULL, NULL, NULL);
                nfs3_call_state_wipe (cs);
                ret = 0;
        }
out:
        return ret;
}


int
nfs3svc_mkdir (rpcsvc_request_t *req)
{
        char                    name[NFS_PATH_MAX];
        struct nfs3_fh          dirfh = {{0}, };
        mkdir3args              args;
        int                     ret = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;
        nfs3_prep_mkdir3args (&args, &dirfh, name);
        if (xdr_to_mkdir3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_mkdir (req, &dirfh, name, &args.attributes);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret, NFS_MSG_DIR_OP_FAIL,
                        "MKDIR procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


int
nfs3_symlink_reply (rpcsvc_request_t *req, nfsstat3 stat, struct nfs3_fh *fh,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent)
{
        symlink3res     res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_symlink3res (&res, stat, fh, buf, preparent, postparent,
                               deviceid);
        nfs3svc_submit_reply (req, (void *)&res,
                              (nfs3_serializer)xdr_serialize_symlink3res);

        return 0;
}


int32_t
nfs3svc_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t               *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto nfs3err;
        }

        nfs3_fh_build_child_fh (&cs->parent, buf, &cs->fh);
        stat = NFS3_OK;

nfs3err:
        nfs3_log_newfh_res (rpcsvc_request_xid (cs->req),
                            NFS3_SYMLINK, stat, op_errno, &cs->fh,
                            cs->resolvedloc.path);
        nfs3_symlink_reply (cs->req, stat, &cs->fh, buf, preparent,
                            postparent);
        nfs3_call_state_wipe (cs);
        return 0;
}


int
nfs3_symlink_resume (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs3_call_state_t               *cs = NULL;
        nfs_user_t                      nfu = {0, };

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_new_fh_resolve_status (cs, stat, nfs3err);
        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_symlink (cs->nfsx, cs->vol, &nfu, cs->pathname,
                           &cs->resolvedloc, nfs3svc_symlink_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_SYMLINK, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_symlink_reply (cs->req, stat, NULL, NULL, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}


int
nfs3_symlink (rpcsvc_request_t *req, struct nfs3_fh *dirfh, char *name,
              char *target, sattr3 *sattr)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;

        if ((!req) || (!dirfh) || (!name) || (!target) || (!sattr)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Bad arguments");
                return -1;
        }

        nfs3_log_symlink_call (rpcsvc_request_xid (req), dirfh, name,
                               target);
        nfs3_validate_gluster_fh (dirfh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_validate_strlen_or_goto (name, NFS_NAME_MAX, nfs3err, stat, ret);
        nfs3_map_fh_to_volume (nfs3, dirfh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_check_rw_volaccess (nfs3, dirfh->exportid, stat, nfs3err);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);

        cs->parent = *dirfh;
        cs->pathname = gf_strdup (target);
        if (!cs->pathname) {
                ret = -1;
                stat = NFS3ERR_SERVERFAULT;
                goto nfs3err;
        }

        ret = nfs3_fh_resolve_and_resume (cs, dirfh, name, nfs3_symlink_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_SYMLINK, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_symlink_reply (req, stat, NULL, NULL, NULL, NULL);
                nfs3_call_state_wipe (cs);
                /* Ret must be 0 after this so that the caller does not
                 * also send an RPC reply.
                 */
                ret = 0;
        }
out:
        return ret;
}


int
nfs3svc_symlink (rpcsvc_request_t *req)
{
        char                    name[NFS_PATH_MAX];
        struct nfs3_fh          dirfh = {{0}, };
        char                    target[NFS_PATH_MAX];
        symlink3args            args;
        int                     ret = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;
        nfs3_prep_symlink3args (&args, &dirfh, name, target);
        if (xdr_to_symlink3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_symlink (req, &dirfh, name, target,
                            &args.symlink.symlink_attributes);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EXDEV, NFS_MSG_SYMLINK_FAIL,
                        "SYMLINK procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


static int
nfs3_mknod_reply (rpcsvc_request_t *req, nfsstat3 stat, struct nfs3_fh *fh,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent)
{
        mknod3res       res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_mknod3res (&res, stat, fh, buf, preparent, postparent,
                             deviceid);
        nfs3svc_submit_reply (req, (void *)&res,
                              (nfs3_serializer)xdr_serialize_mknod3res);

        return 0;
}

int32_t
nfs3svc_mknod_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno,
                           struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto nfs3err;
        }

        stat = NFS3_OK;
nfs3err:
        nfs3_log_newfh_res (rpcsvc_request_xid (cs->req),
                            NFS3_MKNOD, stat, op_errno, &cs->fh,
                            cs->resolvedloc.path);
        nfs3_mknod_reply (cs->req, stat, &cs->fh, postop, &cs->preparent,
                          &cs->postparent);
        nfs3_call_state_wipe (cs);
        return 0;
}



int32_t
nfs3svc_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -1;
        nfs_user_t                      nfu = {0, };
        nfs3_call_state_t               *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto nfs3err;
        }

        nfs3_fh_build_child_fh (&cs->parent, buf, &cs->fh);

        /* Means no attributes were required to be set. */
        if (!cs->setattr_valid) {
                stat = NFS3_OK;
                ret = -1;
                goto nfs3err;
        }

        cs->preparent = *preparent;
        cs->postparent = *postparent;
        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_setattr (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,&cs->stbuf,
                           cs->setattr_valid, nfs3svc_mknod_setattr_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);
nfs3err:
        if (ret < 0) {
                nfs3_log_newfh_res (rpcsvc_request_xid (cs->req),
                                    NFS3_MKNOD, stat, op_errno, &cs->fh,
                                    cs->resolvedloc.path);
                nfs3_mknod_reply (cs->req, stat, &cs->fh, buf, preparent,
                                  postparent);
                nfs3_call_state_wipe (cs);
        }

        return 0;
}


static int
nfs3_mknod_device (nfs3_call_state_t *cs)
{
        int                             ret = -EFAULT;
        dev_t                           devnum = 0;
        mode_t                          mode = 0;
        nfs_user_t                      nfu = {0, };

        if (!cs)
                return ret;

        devnum = makedev (cs->devnums.specdata1, cs->devnums.specdata2);
        if (cs->mknodtype == NF3CHR)
                mode = S_IFCHR;
        else
                mode = S_IFBLK;

        nfs_request_user_init (&nfu, cs->req);
        if (gf_attr_mode_set (cs->setattr_valid)) {
                cs->setattr_valid &= ~GF_SET_ATTR_MODE;
                mode |= cs->mode;
                ret = nfs_mknod (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                 mode, devnum, nfs3svc_mknod_cbk, cs);
        } else
                ret = nfs_mknod (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                 mode, devnum, nfs3svc_mknod_cbk, cs);

        return ret;
}


static int
nfs3_mknod_fifo (nfs3_call_state_t *cs, mode_t mode)
{
        int                             ret = -EFAULT;
        nfs_user_t                      nfu = {0, };

        if (!cs)
                return ret;

        nfs_request_user_init (&nfu, cs->req);
        if (gf_attr_mode_set (cs->setattr_valid)) {
                cs->setattr_valid &= ~GF_SET_ATTR_MODE;
                mode |= cs->mode;
                ret = nfs_mknod (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                 mode, 0, nfs3svc_mknod_cbk, cs);
        } else
                ret = nfs_mknod (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                 mode, 0, nfs3svc_mknod_cbk, cs);

        return ret;
}


static int
nfs3_mknod_resume (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs3_call_state_t               *cs = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_new_fh_resolve_status (cs, stat, nfs3err);
        switch (cs->mknodtype) {

        case NF3CHR:
        case NF3BLK:
                ret = nfs3_mknod_device (cs);
                break;
        case NF3SOCK:
                ret = nfs3_mknod_fifo (cs, S_IFSOCK);
                break;
        case NF3FIFO:
                ret = nfs3_mknod_fifo (cs, S_IFIFO);
                break;
        default:
                ret = -EBADF;
                break;
        }

        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_MKNOD, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_mknod_reply (cs->req, stat, NULL, NULL, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}



int
nfs3_mknod (rpcsvc_request_t *req, struct nfs3_fh *fh, char *name,
            mknoddata3 *nodedata)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;
        sattr3                          *sattr = NULL;

        if ((!req) || (!fh) || (!name) || (!nodedata)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Bad arguments");
                return -1;
        }

        nfs3_log_mknod_call (rpcsvc_request_xid (req), fh, name,
                             nodedata->type);
        nfs3_validate_gluster_fh (fh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_validate_strlen_or_goto (name, NFS_NAME_MAX, nfs3err, stat, ret);
        nfs3_map_fh_to_volume (nfs3, fh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_check_rw_volaccess (nfs3, fh->exportid, stat, nfs3err);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);

        cs->mknodtype = nodedata->type;
        switch (nodedata->type) {
        case NF3CHR:
        case NF3BLK:
                cs->devnums = nodedata->mknoddata3_u.device.spec;
                sattr = &nodedata->mknoddata3_u.device.dev_attributes;
                cs->setattr_valid = nfs3_sattr3_to_setattr_valid (sattr,
                                                                  &cs->stbuf,
                                                                  &cs->mode);
                break;
        case NF3SOCK:
        case NF3FIFO:
                sattr = &nodedata->mknoddata3_u.pipe_attributes;
                cs->setattr_valid = nfs3_sattr3_to_setattr_valid (sattr,
                                                                  &cs->stbuf,
                                                                  &cs->mode);
                break;
        default:
                ret = -EBADF;
                break;
        }

        cs->parent = *fh;
        ret = nfs3_fh_resolve_and_resume (cs, fh, name, nfs3_mknod_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_MKNOD, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_mknod_reply (req, stat, NULL, NULL, NULL, NULL);
                /* Ret must be 0 after this so that the caller does not
                 * also send an RPC reply.
                 */
                nfs3_call_state_wipe (cs);
                ret = 0;
        }
out:
        return ret;
}


int
nfs3svc_mknod (rpcsvc_request_t *req)
{
        char                    name[NFS_PATH_MAX];
        struct nfs3_fh          fh = {{0}, };
        mknod3args              args;
        int                     ret = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;
        nfs3_prep_mknod3args (&args, &fh, name);
        if (xdr_to_mknod3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_mknod (req, &fh, name, &args.what);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret, NFS_MSG_MKNOD_FAIL,
                        "MKNOD procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}

int
nfs3_remove_reply (rpcsvc_request_t *req, nfsstat3 stat, struct iatt *preparent
                   , struct iatt *postparent)
{
        remove3res      res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_remove3res (&res, stat, preparent, postparent, deviceid);
        nfs3svc_submit_reply (req, (void *)&res,
                              (nfs3_serializer)xdr_serialize_remove3res);
        return 0;
}



int32_t
nfs3svc_remove_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
        }

        if (op_ret == 0)
                stat = NFS3_OK;

        nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                             NFS3_REMOVE, stat, op_errno,
                             cs->resolvedloc.path);
        nfs3_remove_reply (cs->req, stat, preparent, postparent);
        nfs3_call_state_wipe (cs);

        return 0;
}


int
__nfs3_remove (nfs3_call_state_t *cs)
{
        int                             ret = -EFAULT;
        nfs_user_t                      nfu = {0, };
        ia_type_t                       type = 0;

        if (!cs)
                return ret;
        type = cs->resolvedloc.inode->ia_type;
        nfs_request_user_init (&nfu, cs->req);
        if (IA_ISDIR (type))
                ret = nfs_rmdir (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                 nfs3svc_remove_cbk, cs);
        else
                ret = nfs_unlink (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                  nfs3svc_remove_cbk, cs);

        return ret;
}


int
nfs3_remove_resume (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs3_call_state_t               *cs = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        ret = __nfs3_remove (cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_REMOVE, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_remove_reply (cs->req, stat, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}


int
nfs3_remove (rpcsvc_request_t *req, struct nfs3_fh *fh, char *name)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;

        if ((!req) || (!fh) || (!name)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Bad arguments");
                return -1;
        }

        nfs3_log_fh_entry_call (rpcsvc_request_xid (req), "REMOVE", fh,
                                name);
        nfs3_validate_gluster_fh (fh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_validate_strlen_or_goto (name, NFS_NAME_MAX, nfs3err, stat, ret);
        nfs3_map_fh_to_volume (nfs3, fh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_check_rw_volaccess (nfs3, fh->exportid, stat, nfs3err);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);

        ret = nfs3_fh_resolve_and_resume (cs, fh, name, nfs3_remove_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_REMOVE, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_remove_reply (req, stat, NULL, NULL);
                nfs3_call_state_wipe (cs);
                /* Ret must be 0 after this so that the caller does not
                 * also send an RPC reply.
                 */
                ret = 0;
        }
out:
        return ret;
}


int
nfs3svc_remove (rpcsvc_request_t *req)
{
        char                    name[NFS_PATH_MAX];
        struct nfs3_fh          fh = {{0}, };
        remove3args             args;
        int                     ret = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;
        nfs3_prep_remove3args (&args, &fh, name);
        if (xdr_to_remove3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_remove (req, &fh, name);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_REMOVE_FAIL,
                        "REMOVE procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


int
nfs3_rmdir_reply (rpcsvc_request_t *req, nfsstat3 stat, struct iatt *preparent,
                  struct iatt *postparent)
{
        rmdir3res       res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_rmdir3res (&res, stat, preparent, postparent, deviceid);
        nfs3svc_submit_reply (req, (void *)&res,
                              (nfs3_serializer)xdr_serialize_rmdir3res);
        return 0;
}


int32_t
nfs3svc_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                gf_msg (GF_NFS, GF_LOG_WARNING, op_errno, NFS_MSG_RMDIR_CBK,
                        "%x: %s => -1 (%s)", rpcsvc_request_xid (cs->req),
                        cs->resolvedloc.path, strerror (op_errno));
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
        } else {
                stat = NFS3_OK;
        }

        nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                             NFS3_RMDIR, stat, op_errno,
                             cs->resolvedloc.path);
        nfs3_rmdir_reply (cs->req, stat, preparent, postparent);
        nfs3_call_state_wipe (cs);

        return 0;
}

int
nfs3_rmdir_resume (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs3_call_state_t               *cs = NULL;
        nfs_user_t                      nfu = {0, };

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_rmdir (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                         nfs3svc_rmdir_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_RMDIR, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_rmdir_reply (cs->req, stat, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}



int
nfs3_rmdir (rpcsvc_request_t *req, struct nfs3_fh *fh, char *name)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;

        if ((!req) || (!fh) || (!name)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Bad arguments");
                return -1;
        }

        nfs3_log_fh_entry_call (rpcsvc_request_xid (req), "RMDIR", fh,
                                name);
        nfs3_validate_gluster_fh (fh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_validate_strlen_or_goto (name, NFS_NAME_MAX, nfs3err, stat, ret);
        nfs3_map_fh_to_volume (nfs3, fh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_check_rw_volaccess (nfs3, fh->exportid, stat, nfs3err);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);

        ret = nfs3_fh_resolve_and_resume (cs, fh, name, nfs3_rmdir_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_RMDIR, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_rmdir_reply (req, stat, NULL, NULL);
                nfs3_call_state_wipe (cs);
                /* Ret must be 0 after this so that the caller does not
                 * also send an RPC reply.
                 */
                ret = 0;
        }
out:
        return ret;
}


int
nfs3svc_rmdir (rpcsvc_request_t *req)
{
        char                    name[NFS_PATH_MAX];
        struct nfs3_fh          fh = {{0}, };
        rmdir3args              args;
        int                     ret = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;
        nfs3_prep_rmdir3args (&args, &fh, name);
        if (xdr_to_rmdir3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_rmdir (req, &fh, name);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret, NFS_MSG_DIR_OP_FAIL,
                        "RMDIR procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


int
nfs3_rename_reply (rpcsvc_request_t *req, nfsstat3 stat, struct iatt *buf,
                   struct iatt *preoldparent, struct iatt *postoldparent,
                   struct iatt *prenewparent, struct iatt *postnewparent)
{
        rename3res      res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_rename3res (&res, stat, buf, preoldparent, postoldparent,
                              prenewparent, postnewparent, deviceid);

        nfs3svc_submit_reply (req, (void *)&res,
                              (nfs3_serializer) xdr_serialize_rename3res);

        return 0;
}



int32_t
nfs3svc_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *buf,
                    struct iatt *preoldparent, struct iatt *postoldparent,
                    struct iatt *prenewparent, struct iatt *postnewparent,
                    dict_t *xdata)
{
        int                     ret = -EFAULT;
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto nfs3err;
        }

        stat = NFS3_OK;
nfs3err:
        nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                             NFS3_RENAME, stat,
                             -ret, cs->resolvedloc.path);
        nfs3_rename_reply (cs->req, stat, buf, preoldparent, postoldparent,
                           prenewparent, postnewparent);
        nfs3_call_state_wipe (cs);
        return 0;
}


int
nfs3_rename_resume_dst (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs3_call_state_t               *cs = NULL;
        nfs_user_t                      nfu = {0, };

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_new_fh_resolve_status (cs, stat, nfs3err);
        cs->parent = cs->resolvefh;
        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_rename (cs->nfsx, cs->vol, &nfu, &cs->oploc, &cs->resolvedloc,
                          nfs3svc_rename_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_RENAME, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_rename_reply (cs->req, stat, NULL, NULL, NULL, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}



int
nfs3_rename_resume_src (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs3_call_state_t               *cs = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        /* Copy the resolved loc for the source file into another loc
         * for safekeeping till we resolve the dest loc.
         */
        nfs_loc_copy (&cs->oploc, &cs->resolvedloc);
        nfs_loc_wipe (&cs->resolvedloc);
        GF_FREE (cs->resolventry);

        ret = nfs3_fh_resolve_and_resume (cs, &cs->fh, cs->pathname,
                                          nfs3_rename_resume_dst);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_RENAME, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_rename_reply (cs->req, stat, NULL, NULL, NULL, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}


int
nfs3_rename (rpcsvc_request_t *req, struct nfs3_fh *olddirfh, char *oldname,
             struct nfs3_fh *newdirfh, char *newname)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;

        if ((!req) || (!olddirfh) || (!oldname) || (!newdirfh) || (!newname)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Bad arguments");
                return -1;
        }

        nfs3_log_rename_call (rpcsvc_request_xid (req), olddirfh, oldname,
                              newdirfh, newname);
        nfs3_validate_gluster_fh (olddirfh, stat, nfs3err);
        nfs3_validate_gluster_fh (newdirfh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_validate_strlen_or_goto(oldname, NFS_NAME_MAX, nfs3err, stat, ret);
        nfs3_validate_strlen_or_goto(newname, NFS_NAME_MAX, nfs3err, stat, ret);
        nfs3_map_fh_to_volume (nfs3, olddirfh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_check_rw_volaccess (nfs3, olddirfh->exportid, stat, nfs3err);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);

        /* While we resolve the source (fh, name) pair, we need to keep a copy
         * of the dest (fh,name) pair.
         */
        cs->fh = *newdirfh;
        cs->pathname = gf_strdup (newname);
        if (!cs->pathname) {
                stat = NFS3ERR_SERVERFAULT;
                ret = -1;
                goto nfs3err;
        }

        ret = nfs3_fh_resolve_and_resume (cs, olddirfh, oldname,
                                          nfs3_rename_resume_src);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_RENAME, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_rename_reply (req, stat, NULL, NULL, NULL, NULL, NULL);
                nfs3_call_state_wipe (cs);
                /* Ret must be 0 after this so that the caller does not
                 * also send an RPC reply.
                 */
                ret = 0;
        }
out:
        return ret;
}


int
nfs3svc_rename (rpcsvc_request_t *req)
{
        char                    newname[NFS_PATH_MAX];
        char                    oldname[NFS_PATH_MAX];
        struct nfs3_fh          olddirfh = {{0}, };
        struct nfs3_fh          newdirfh = {{0}, };
        rename3args             args;
        int                     ret = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;
        nfs3_prep_rename3args (&args, &olddirfh, oldname, &newdirfh, newname);
        if (xdr_to_rename3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_rename (req, &olddirfh, oldname, &newdirfh, newname);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_RENAME_FAIL,
                        "RENAME procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


int
nfs3_link_reply (rpcsvc_request_t *req, nfsstat3 stat, struct iatt *buf,
                 struct iatt *preparent, struct iatt *postparent)
{
        link3res        res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_link3res (&res, stat, buf, preparent, postparent, deviceid);
        nfs3svc_submit_reply (req, (void *)&res,
                              (nfs3_serializer)xdr_serialize_link3res);

        return 0;
}


int32_t
nfs3svc_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
        } else
                stat = NFS3_OK;

        nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                             NFS3_LINK, stat, op_errno,
                             cs->resolvedloc.path);
        nfs3_link_reply (cs->req, stat, buf, preparent, postparent);
        nfs3_call_state_wipe (cs);

        return 0;
}


int
nfs3_link_resume_lnk (void *carg)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     ret = -EFAULT;
        nfs3_call_state_t       *cs = NULL;
        nfs_user_t              nfu = {0, };

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_new_fh_resolve_status (cs, stat, nfs3err);

        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_link (cs->nfsx, cs->vol, &nfu, &cs->oploc, &cs->resolvedloc,
                        nfs3svc_link_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_LINK, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_link_reply (cs->req, stat, NULL, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }
        return ret;
}


int
nfs3_link_resume_tgt (void *carg)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     ret = -EFAULT;
        nfs3_call_state_t       *cs = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        nfs_loc_copy (&cs->oploc, &cs->resolvedloc);
        nfs_loc_wipe (&cs->resolvedloc);

        ret = nfs3_fh_resolve_and_resume (cs, &cs->fh, cs->pathname,
                                          nfs3_link_resume_lnk);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_LINK, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_link_reply (cs->req, stat, NULL, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}


int
nfs3_link (rpcsvc_request_t *req, struct nfs3_fh *targetfh,
           struct nfs3_fh *dirfh, char *newname)
{
        xlator_t                *vol = NULL;
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     ret = -EFAULT;
        struct nfs3_state       *nfs3 = NULL;
        nfs3_call_state_t       *cs = NULL;

        if ((!req) || (!targetfh) || (!dirfh) || (!newname)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Bad arguments");
                return -1;
        }

        nfs3_validate_gluster_fh (dirfh, stat, nfs3err);
        nfs3_validate_gluster_fh (targetfh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_validate_strlen_or_goto(newname, NFS_NAME_MAX, nfs3err, stat, ret);
        nfs3_map_fh_to_volume (nfs3, dirfh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_check_rw_volaccess (nfs3, dirfh->exportid, stat, nfs3err);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);

        cs->fh = *dirfh;
        cs->pathname = gf_strdup (newname);
        if (!cs->pathname) {
                stat = NFS3ERR_SERVERFAULT;
                ret = -1;
                goto nfs3err;
        }

        ret = nfs3_fh_resolve_and_resume (cs, targetfh, NULL,
                                          nfs3_link_resume_tgt);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_LINK, stat,
                                     -ret, cs ? cs->pathname : NULL);
                nfs3_link_reply (req, stat, NULL, NULL, NULL);
                nfs3_call_state_wipe (cs);
                /* Ret must be 0 after this so that the caller does not
                 * also send an RPC reply.
                 */
                ret = 0;
        }
out:
        return ret;
}

int
nfs3svc_link (rpcsvc_request_t *req)
{
        char                    newpath[NFS_PATH_MAX];
        struct nfs3_fh          dirfh = {{0}, };
        struct nfs3_fh          targetfh = {{0}, };
        link3args               args;
        int                     ret = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;
        nfs3_prep_link3args (&args, &targetfh, &dirfh, newpath);
        if (xdr_to_link3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_link (req, &targetfh, &dirfh, newpath);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EXDEV, NFS_MSG_LINK_FAIL,
                        "LINK procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


int
nfs3_readdirp_reply (rpcsvc_request_t *req, nfsstat3 stat,struct nfs3_fh *dirfh,
                     uint64_t cverf, struct iatt *dirstat, gf_dirent_t *entries,
                     count3 dircount, count3 maxcount, int is_eof)
{
        readdirp3res    res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_readdirp3res (&res, stat, dirfh, cverf, dirstat, entries,
                                dircount, maxcount, is_eof, deviceid);
        nfs3svc_submit_reply (req, (void *)&res,
                              (nfs3_serializer) xdr_serialize_readdirp3res);
        nfs3_free_readdirp3res (&res);

        return 0;
}


int
nfs3_readdir_reply (rpcsvc_request_t *req, nfsstat3 stat, struct nfs3_fh *dirfh,
                    uint64_t cverf, struct iatt *dirstat, gf_dirent_t *entries,
                    count3 count, int is_eof)
{
        readdir3res     res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_readdir3res (&res, stat, dirfh, cverf, dirstat, entries, count
                               , is_eof, deviceid);
        nfs3svc_submit_reply (req, (void *)&res,
                              (nfs3_serializer) xdr_serialize_readdir3res);
        nfs3_free_readdir3res (&res);

        return 0;
}


int32_t
nfs3svc_readdir_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, struct iatt *buf,
                           dict_t *xdata)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     is_eof = 0;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto nfs3err;
        }

        /* Check whether we encountered a end of directory stream while
         * readdir'ing.
         */
        if (cs->operrno == ENOENT) {
                gf_msg_trace (GF_NFS3, 0, "Reached end-of-directory");
                is_eof = 1;
        }

        stat = NFS3_OK;

        /* do inode linking here */
        gf_link_inodes_from_dirent (this, cs->fd->inode, &cs->entries);

nfs3err:
        if (cs->maxcount == 0) {
                nfs3_log_readdir_res (rpcsvc_request_xid (cs->req),
                                      stat, op_errno, (uintptr_t)cs->fd,
                                      cs->dircount, is_eof,
                                      cs->resolvedloc.path);
                nfs3_readdir_reply (cs->req, stat, &cs->parent,
                                    (uintptr_t)cs->fd, buf, &cs->entries,
                                    cs->dircount, is_eof);
        } else {
                nfs3_log_readdirp_res (rpcsvc_request_xid (cs->req),
                                       stat, op_errno, (uintptr_t)cs->fd,
                                       cs->dircount, cs->maxcount, is_eof,
                                       cs->resolvedloc.path);
                nfs3_readdirp_reply (cs->req, stat, &cs->parent,
                                     (uintptr_t)cs->fd, buf,
                                     &cs->entries, cs->dircount,
                                     cs->maxcount, is_eof);
        }

        if (is_eof) {
                /* do nothing */
        }

        nfs3_call_state_wipe (cs);
        return 0;
}


int32_t
nfs3svc_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                     dict_t *xdata)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     ret = -EFAULT;
        nfs_user_t              nfu = {0, };
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto err;
        }

        cs->operrno = op_errno;
        list_splice_init (&entries->list, &cs->entries.list);
        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_fstat (cs->nfsx, cs->vol, &nfu, cs->fd,
                         nfs3svc_readdir_fstat_cbk, cs);
        if (ret < 0) {
                op_ret = -1;
                stat = nfs3_errno_to_nfsstat3 (-ret);
                op_errno = -ret;
        }

err:
        if (op_ret >= 0)
                goto ret;

        if (cs->maxcount == 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_READDIR, stat, op_errno,
                                     cs->resolvedloc.path);
                nfs3_readdir_reply (cs->req, stat, NULL, 0, NULL, NULL, 0, 0);
        } else {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_READDIRP, stat, op_errno,
                                     cs->resolvedloc.path);
                nfs3_readdirp_reply (cs->req, stat, NULL, 0, NULL, NULL,
                                     0, 0, 0);
        }

        /* For directories, we force a purge from the fd cache on close
         * so that next time the dir is read, we'll get any changed directory
         * entries.
         */
        nfs3_call_state_wipe (cs);
ret:
        return 0;
}

int
nfs3_readdir_process (nfs3_call_state_t *cs)
{
        int                     ret = -EFAULT;
        nfs_user_t              nfu = {0, };

        if (!cs)
                return ret;

        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_readdirp (cs->nfsx, cs->vol, &nfu, cs->fd, cs->dircount,
                            cs->cookie, nfs3svc_readdir_cbk, cs);
        return ret;
}


int
nfs3_readdir_read_resume (void *carg)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     ret = -EFAULT;
        nfs3_call_state_t       *cs = NULL;
        struct nfs3_state       *nfs3 = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        nfs3 = rpcsvc_request_program_private (cs->req);
        ret = nfs3_verify_dircookie (nfs3, cs->fd, cs->cookie, cs->cookieverf,
                                     &stat);
        if (ret < 0)    /* Stat already set by verifier function above. */
                goto nfs3err;

        ret = nfs3_readdir_process (cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);
nfs3err:
        if (ret < 0) {
                if (cs->maxcount == 0) {
                        nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                             NFS3_READDIR, stat, -ret,
                                             cs->resolvedloc.path);
                        nfs3_readdir_reply (cs->req, stat, NULL, 0, NULL, NULL,
                                            0, 0);
                } else {
                        nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                             NFS3_READDIRP, stat, -ret,
                                             cs->resolvedloc.path);
                        nfs3_readdirp_reply (cs->req, stat, NULL, 0, NULL, NULL,
                                             0, 0, 0);
                }
                nfs3_call_state_wipe (cs);
        }

        return 0;
}


int32_t
nfs3svc_readdir_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno, fd_t *fd,
                             dict_t *xdata)
{
        /*
         * We don't really need this, it's just an artifact of forcing the
         * opendir to happen.
         */
        if (fd) {
                fd_unref(fd);
        }

        return 0;
}


int
nfs3_readdir_open_resume (void *carg)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     ret = -EFAULT;
        nfs3_call_state_t       *cs = NULL;
        nfs_user_t               nfu = {0, };

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        cs->fd = fd_anonymous (cs->resolvedloc.inode);
        if (!cs->fd) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ANONYMOUS_FD_FAIL,
                        "Fail to create anonymous fd");
                goto nfs3err;
        }

        /*
         * NFS client will usually send us a readdirp without an opendir,
         * which would cause us to skip our usual self-heal checks which occur
         * in opendir for native protocol. To make sure those checks do happen,
         * our most reliable option is to do our own opendir for any readdirp
         * at the beginning of the directory.
         */
        if (cs->cookie == 0) {
                nfs_request_user_init (&nfu, cs->req);
                ret = nfs_opendir (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                                   nfs3svc_readdir_opendir_cbk, cs);
                if (ret < 0) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, -ret,
                                NFS_MSG_DIR_OP_FAIL,
                                "auto-opendir failed");
                }
        }

        ret = nfs3_readdir_read_resume (cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                if (cs->maxcount == 0) {
                        nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                             NFS3_READDIR, stat, -ret,
                                             cs->resolvedloc.path);
                        nfs3_readdir_reply (cs->req, stat, NULL, 0, NULL, NULL,
                                            0, 0);
                } else {
                        nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                             NFS3_READDIRP, stat, -ret,
                                             cs->resolvedloc.path);
                        nfs3_readdirp_reply (cs->req, stat, NULL, 0, NULL, NULL,
                                             0, 0, 0);
                }
                nfs3_call_state_wipe (cs);
        }

        return ret;
}



int
nfs3_readdir (rpcsvc_request_t *req, struct nfs3_fh *fh, cookie3 cookie,
              uint64_t cverf, count3 dircount, count3 maxcount)
{
        xlator_t                *vol = NULL;
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     ret = -EFAULT;
        struct nfs3_state       *nfs3 = NULL;
        nfs3_call_state_t       *cs = NULL;
        struct nfs_state        *nfs = NULL;
        gf_boolean_t            is_readdirp = !!maxcount;

        if ((!req) || (!fh)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Bad arguments");
                return -1;
        }

        nfs3_log_readdir_call (rpcsvc_request_xid (req), fh, dircount,
                               maxcount);
        nfs3_validate_gluster_fh (fh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_map_fh_to_volume (nfs3, fh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);
        nfs = nfs_state (nfs3->nfsx);

        if (is_readdirp && !nfs->rdirplus) {
                ret = -ENOTSUP;
                stat = nfs3_errno_to_nfsstat3 (-ret);
                goto nfs3err;
        }

        cs->cookieverf = cverf;
        cs->dircount = dircount;
        cs->maxcount = maxcount;
        cs->cookie = cookie;
        cs->parent = *fh;
        ret = nfs3_fh_resolve_and_resume (cs, fh, NULL,
                                          nfs3_readdir_open_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                if (!is_readdirp) {
                        nfs3_log_common_res (rpcsvc_request_xid (req),
                                             NFS3_READDIR, stat, -ret,
                                             cs ? cs->resolvedloc.path : NULL);
                        nfs3_readdir_reply (req, stat, NULL, 0, NULL, NULL, 0,
                                            0);
                } else {
                        nfs3_log_common_res (rpcsvc_request_xid (req),
                                             NFS3_READDIRP, stat, -ret,
                                             cs ? cs->resolvedloc.path : NULL);
                        nfs3_readdirp_reply (req, stat, NULL, 0, NULL, NULL, 0,
                                             0, 0);
                }
                /* Ret must be NULL after this so that the caller does not
                 * also send an RPC reply.
                 */
                ret = 0;
                nfs3_call_state_wipe (cs);
        }
out:
        return ret;
}


int
nfs3svc_readdir (rpcsvc_request_t *req)
{
        readdir3args    ra;
        struct nfs3_fh  fh = {{0},};
        int             ret = RPCSVC_ACTOR_ERROR;
        uint64_t        verf = 0;
        uint64_t       *cval;

        if (!req)
                return ret;
        nfs3_prep_readdir3args (&ra, &fh);
        if (xdr_to_readdir3args (req->msg[0], &ra) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }
        cval = (uint64_t *) ra.cookieverf;
        verf =  *cval;

        ret = nfs3_readdir (req, &fh, ra.cookie, verf, ra.count, 0);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret,
                        NFS_MSG_READDIR_FAIL,
                        "READDIR procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


int
nfs3svc_readdirp (rpcsvc_request_t *req)
{
        readdirp3args   ra;
        struct nfs3_fh  fh = {{0},};
        int             ret = RPCSVC_ACTOR_ERROR;
        uint64_t        cverf = 0;
        uint64_t       *cval;

        if (!req)
                return ret;
        nfs3_prep_readdirp3args (&ra, &fh);
        if (xdr_to_readdirp3args (req->msg[0], &ra) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }
        cval = (uint64_t *) ra.cookieverf;
        cverf = *cval;

        ret = nfs3_readdir (req, &fh, ra.cookie, cverf, ra.dircount,
                            ra.maxcount);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret,
                        NFS_MSG_READDIRP_FAIL,
                        "READDIRP procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


int
nfs3_fsstat_reply (rpcsvc_request_t *req, nfsstat3 stat, struct statvfs *fsbuf,
                   struct iatt *postbuf)
{
        fsstat3res      res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_fsstat3res (&res, stat, fsbuf, postbuf, deviceid);
        return nfs3svc_submit_reply (req, &res,
                                     (nfs3_serializer)xdr_serialize_fsstat3res);

}


int32_t
nfs3_fsstat_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *buf,
                      dict_t *xdata)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
        } else
                stat = NFS3_OK;

        nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                             NFS3_FSSTAT, stat,
                             op_errno, cs->resolvedloc.path);
        nfs3_fsstat_reply (cs->req, stat, &cs->fsstat, buf);
        nfs3_call_state_wipe (cs);
        return 0;
}


int32_t
nfs3_fsstat_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, struct statvfs *buf,
                        dict_t *xdata)
{
        nfs_user_t              nfu = {0, };
        int                     ret = -EFAULT;
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                ret = -op_errno;
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
                goto err;
        }

        /* Then get the stat for the fs root in order to fill in the
         * post_op_attr.
         */
        cs->fsstat = *buf;
        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_stat (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                        nfs3_fsstat_stat_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_FSSTAT, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_fsstat_reply (cs->req, stat, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        return 0;
}


int
nfs3_fsstat_resume (void *carg)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     ret = -EFAULT;
        nfs3_call_state_t       *cs = NULL;
        nfs_user_t              nfu = {0, };

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        nfs_request_user_init (&nfu, cs->req);
        /* First, we need to get the statvfs for the subvol */
        ret = nfs_statfs (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                          nfs3_fsstat_statfs_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_FSSTAT, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_fsstat_reply (cs->req, stat, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}



int
nfs3_fsstat (rpcsvc_request_t *req, struct nfs3_fh *fh)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;

        if ((!req) || (!fh)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Bad arguments");
                return -1;
        }

        nfs3_log_common_call (rpcsvc_request_xid (req), "FSSTAT", fh);
        nfs3_validate_gluster_fh (fh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_map_fh_to_volume (nfs3, fh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);

        ret = nfs3_fh_resolve_and_resume (cs, fh, NULL, nfs3_fsstat_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_FSSTAT, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_fsstat_reply (req, stat, NULL, NULL);
                nfs3_call_state_wipe (cs);
                /* Ret must be 0 after this so that the caller does not
                 * also send an RPC reply.
                 */
                ret = 0;
        }
out:
        return ret;
}


int
nfs3svc_fsstat (rpcsvc_request_t *req)
{
        struct nfs3_fh          fh = {{0}, };
        fsstat3args             args;
        int                     ret = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;
        nfs3_prep_fsstat3args (&args, &fh);
        if (xdr_to_fsstat3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_fsstat (req, &fh);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_FSTAT_FAIL,
                        "FSTAT procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


int
nfs3_fsinfo_reply (rpcsvc_request_t *req, nfsstat3 status, struct iatt *fsroot)
{
        fsinfo3res              res;
        struct nfs3_state       *nfs3 = NULL;
        uint64_t                deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3 = rpcsvc_request_program_private (req);
        nfs3_fill_fsinfo3res (nfs3, &res, status, fsroot, deviceid);

        nfs3svc_submit_reply (req, &res,
                              (nfs3_serializer)xdr_serialize_fsinfo3res);
        return 0;
}


int32_t
nfs3svc_fsinfo_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *buf,
                    dict_t *xdata)
{
        nfsstat3                status = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t       *cs = NULL;

        cs = frame->local;

        if (op_ret == -1) {
                status = nfs3_cbk_errno_status (op_ret, op_errno);
        }else
                status = NFS3_OK;

        nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                             NFS3_FSINFO, status,
                             op_errno, cs->resolvedloc.path);

        nfs3_fsinfo_reply (cs->req, status, buf);
        nfs3_call_state_wipe (cs);

        return 0;
}


int
nfs3_fsinfo_resume (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs_user_t                      nfu = {0, };
        nfs3_call_state_t               *cs = NULL;


        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        nfs_request_user_init (&nfu, cs->req);

        ret = nfs_stat (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                        nfs3svc_fsinfo_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_FSINFO, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_fsinfo_reply (cs->req, stat, NULL);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}


int
nfs3_fsinfo (rpcsvc_request_t *req, struct nfs3_fh *fh)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;

        if ((!req) || (!fh)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Bad arguments");
                return -1;
        }

        nfs3_log_common_call (rpcsvc_request_xid (req), "FSINFO", fh);
        nfs3_validate_gluster_fh (fh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_map_fh_to_volume (nfs3, fh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);

        ret = nfs3_fh_resolve_and_resume (cs, fh, NULL, nfs3_fsinfo_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_FSINFO, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_fsinfo_reply (req, stat, NULL);
                nfs3_call_state_wipe (cs);
                ret = 0;
        }
out:
        return ret;
}


int
nfs3svc_fsinfo (rpcsvc_request_t *req)
{
        int             ret = RPCSVC_ACTOR_ERROR;
        fsinfo3args     args;
        struct nfs3_fh  root = {{0}, };

        if (!req)
                return ret;

        nfs3_prep_fsinfo3args (&args, &root);
        if (xdr_to_fsinfo3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding arguments");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_fsinfo (req, &root);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_FSINFO_FAIL,
                        "FSINFO procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


int
nfs3_pathconf_reply (rpcsvc_request_t *req, nfsstat3 stat, struct iatt *buf)
{
        pathconf3res    res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_pathconf3res (&res, stat, buf, deviceid);
        nfs3svc_submit_reply (req, (void *)&res,
                              (nfs3_serializer)xdr_serialize_pathconf3res);
        return 0;
}


int32_t
nfs3svc_pathconf_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *buf,
                      dict_t *xdata)
{
        struct iatt             *sbuf = NULL;
        nfs3_call_state_t       *cs = NULL;
        nfsstat3                stat = NFS3ERR_SERVERFAULT;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
        } else {
                /* If stat fop failed, we can still send the other components
                 * in a pathconf reply.
                 */
                sbuf = buf;
                stat = NFS3_OK;
        }

        nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                             NFS3_PATHCONF, stat,
                             op_errno, cs->resolvedloc.path);
        nfs3_pathconf_reply (cs->req, stat, sbuf);
        nfs3_call_state_wipe (cs);

        return 0;
}


int
nfs3_pathconf_resume (void *carg)
{
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        nfs_user_t                      nfu = {0, };
        nfs3_call_state_t               *cs = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_stat (cs->nfsx, cs->vol, &nfu, &cs->resolvedloc,
                        nfs3svc_pathconf_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);
nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_PATHCONF, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_pathconf_reply (cs->req, stat, NULL);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}

int
nfs3_pathconf (rpcsvc_request_t *req, struct nfs3_fh *fh)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;

        if ((!req) || (!fh)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Bad arguments");
                return -1;
        }

        nfs3_log_common_call (rpcsvc_request_xid (req), "PATHCONF", fh);
        nfs3_validate_gluster_fh (fh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_map_fh_to_volume (nfs3, fh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);

        ret = nfs3_fh_resolve_and_resume (cs, fh, NULL, nfs3_pathconf_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_PATHCONF, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_pathconf_reply (req, stat, NULL);
                nfs3_call_state_wipe (cs);
                /* Ret must be 0 after this so that the caller does not
                 * also send an RPC reply.
                 */
                ret = 0;
        }
out:
        return ret;
}


int
nfs3svc_pathconf (rpcsvc_request_t *req)
{
        struct nfs3_fh          fh = {{0}, };
        pathconf3args           args;
        int                     ret = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;
        nfs3_prep_pathconf3args (&args, &fh);
        if (xdr_to_pathconf3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_pathconf (req, &fh);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, -ret,
                        NFS_MSG_PATHCONF_FAIL,
                        "PATHCONF procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}

int
nfs3_commit_reply (rpcsvc_request_t *req, nfsstat3 stat, uint64_t wverf,
                   struct iatt *prestat, struct iatt *poststat)
{
        commit3res      res = {0, };
        uint64_t        deviceid = 0;

        deviceid = nfs3_request_xlator_deviceid (req);
        nfs3_fill_commit3res (&res, stat, wverf, prestat, poststat, deviceid);
        nfs3svc_submit_reply (req, (void *)&res,
                              (nfs3_serializer)xdr_serialize_commit3res);

        return 0;
}


int32_t
nfs3svc_commit_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        nfs3_call_state_t       *cs = NULL;
        struct nfs3_state       *nfs3 = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nfs3_cbk_errno_status (op_ret, op_errno);
        } else
                stat = NFS3_OK;

        nfs3 = rpcsvc_request_program_private (cs->req);
        nfs3_log_commit_res (rpcsvc_request_xid (cs->req),
                             stat, op_errno, nfs3->serverstart,
                             cs->resolvedloc.path);
        nfs3_commit_reply (cs->req, stat, nfs3->serverstart, NULL, NULL);
        nfs3_call_state_wipe (cs);

        return 0;
}

int
nfs3_commit_resume (void *carg)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     ret = -EFAULT;
        nfs_user_t              nfu = {0, };
        nfs3_call_state_t       *cs = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);

        if (nfs3_export_sync_trusted (cs->nfs3state, cs->resolvefh.exportid)) {
                ret = -1;
                stat = NFS3_OK;
                goto nfs3err;
        }

        nfs_request_user_init (&nfu, cs->req);
        ret = nfs_flush (cs->nfsx, cs->vol, &nfu, cs->fd,
                         nfs3svc_commit_cbk, cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_COMMIT, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_commit_reply (cs->req, stat, cs->nfs3state->serverstart,
                                   NULL, NULL);
                nfs3_call_state_wipe (cs);
                ret = 0;
        }

        return 0;
}


int
nfs3_commit_open_resume (void *carg)
{
        nfsstat3                stat = NFS3ERR_SERVERFAULT;
        int                     ret = -EFAULT;
        nfs3_call_state_t       *cs = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs3_check_fh_resolve_status (cs, stat, nfs3err);
        cs->fd = fd_anonymous (cs->resolvedloc.inode);
        if (!cs->fd) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ANONYMOUS_FD_FAIL,
                        "Failed to create anonymous fd.");
                goto nfs3err;
        }

        ret = nfs3_commit_resume (cs);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);
nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (cs->req),
                                     NFS3_COMMIT, stat, -ret,
                                     cs->resolvedloc.path);
                nfs3_commit_reply (cs->req, stat, 0, NULL, NULL);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}



int
nfs3_commit (rpcsvc_request_t *req, struct nfs3_fh *fh, offset3 offset,
             count3 count)
{
        xlator_t                        *vol = NULL;
        nfsstat3                        stat = NFS3ERR_SERVERFAULT;
        int                             ret = -EFAULT;
        struct nfs3_state               *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;

        if ((!req) || (!fh)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Bad arguments");
                return -1;
        }

        nfs3_log_rw_call (rpcsvc_request_xid (req), "COMMIT", fh, offset,
                          count, -1);
        nfs3_validate_gluster_fh (fh, stat, nfs3err);
        nfs3_validate_nfs3_state (req, nfs3, stat, nfs3err, ret);
        nfs3_map_fh_to_volume (nfs3, fh, req, vol, stat, nfs3err);
        nfs3_volume_started_check (nfs3, vol, ret, out);
        nfs3_check_rw_volaccess (nfs3, fh->exportid, stat, nfs3err);
        nfs3_handle_call_state_init (nfs3, cs, req, vol, stat, nfs3err);

        cs->datacount = count;
        cs->dataoffset = offset;
        ret = nfs3_fh_resolve_and_resume (cs, fh, NULL,
                                          nfs3_commit_open_resume);
        if (ret < 0)
                stat = nfs3_errno_to_nfsstat3 (-ret);

nfs3err:
        if (ret < 0) {
                nfs3_log_common_res (rpcsvc_request_xid (req),
                                     NFS3_COMMIT, stat, -ret,
                                     cs ? cs->resolvedloc.path : NULL);
                nfs3_commit_reply (req, stat, 0, NULL, NULL);
                nfs3_call_state_wipe (cs);
                ret = 0;
        }
out:
        return ret;
}



int
nfs3svc_commit (rpcsvc_request_t *req)
{
        struct nfs3_fh          fh = {{0}, };
        commit3args             args;
        int                     ret = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;
        nfs3_prep_commit3args (&args, &fh);
        if (xdr_to_commit3args (req->msg[0], &args) <= 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ret = nfs3_commit (req, &fh, args.offset, args.count);
        if ((ret < 0) && (ret != RPCSVC_ACTOR_IGNORE)) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_COMMIT_FAIL,
                        "COMMIT procedure failed");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = RPCSVC_ACTOR_ERROR;
        }

rpcerr:
        return ret;
}


rpcsvc_actor_t          nfs3svc_actors[NFS3_PROC_COUNT] = {
        {"NULL",        NFS3_NULL,      nfs3svc_null,      NULL,   0, DRC_IDEMPOTENT},
        {"GETATTR",     NFS3_GETATTR,   nfs3svc_getattr,   NULL,   0, DRC_IDEMPOTENT},
        {"SETATTR",     NFS3_SETATTR,   nfs3svc_setattr,   NULL,   0, DRC_NON_IDEMPOTENT},
        {"LOOKUP",      NFS3_LOOKUP,    nfs3svc_lookup,    NULL,   0, DRC_IDEMPOTENT},
        {"ACCESS",      NFS3_ACCESS,    nfs3svc_access,    NULL,   0, DRC_IDEMPOTENT},
        {"READLINK",    NFS3_READLINK,  nfs3svc_readlink,  NULL,   0, DRC_IDEMPOTENT},
        {"READ",        NFS3_READ,      nfs3svc_read,      NULL,   0, DRC_IDEMPOTENT},
        {"WRITE",       NFS3_WRITE,     nfs3svc_write,     nfs3svc_write_vecsizer, 0, DRC_NON_IDEMPOTENT},
        {"CREATE",      NFS3_CREATE,    nfs3svc_create,    NULL,   0, DRC_NON_IDEMPOTENT},
        {"MKDIR",       NFS3_MKDIR,     nfs3svc_mkdir,     NULL,   0, DRC_NON_IDEMPOTENT},
        {"SYMLINK",     NFS3_SYMLINK,   nfs3svc_symlink,   NULL,   0, DRC_NON_IDEMPOTENT},
        {"MKNOD",       NFS3_MKNOD,     nfs3svc_mknod,     NULL,   0, DRC_NON_IDEMPOTENT},
        {"REMOVE",      NFS3_REMOVE,    nfs3svc_remove,    NULL,   0, DRC_NON_IDEMPOTENT},
        {"RMDIR",       NFS3_RMDIR,     nfs3svc_rmdir,     NULL,   0, DRC_NON_IDEMPOTENT},
        {"RENAME",      NFS3_RENAME,    nfs3svc_rename,    NULL,   0, DRC_NON_IDEMPOTENT},
        {"LINK",        NFS3_LINK,      nfs3svc_link,      NULL,   0, DRC_NON_IDEMPOTENT},
        {"READDIR",     NFS3_READDIR,   nfs3svc_readdir,   NULL,   0, DRC_IDEMPOTENT},
        {"READDIRPLUS", NFS3_READDIRP,  nfs3svc_readdirp,  NULL,   0, DRC_IDEMPOTENT},
        {"FSSTAT",      NFS3_FSSTAT,    nfs3svc_fsstat,    NULL,   0, DRC_IDEMPOTENT},
        {"FSINFO",      NFS3_FSINFO,    nfs3svc_fsinfo,    NULL,   0, DRC_IDEMPOTENT},
        {"PATHCONF",    NFS3_PATHCONF,  nfs3svc_pathconf,  NULL,   0, DRC_IDEMPOTENT},
        {"COMMIT",      NFS3_COMMIT,    nfs3svc_commit,    NULL,   0, DRC_IDEMPOTENT}
};


rpcsvc_program_t        nfs3prog = {
                        .progname       = "NFS3",
                        .prognum        = NFS_PROGRAM,
                        .progver        = NFS_V3,
                        .progport       = GF_NFS3_PORT,
                        .actors         = nfs3svc_actors,
                        .numactors      = NFS3_PROC_COUNT,

                        /* Requests like FSINFO are sent before an auth scheme
                         * is inited by client. See RFC 2623, Section 2.3.2. */
                        .min_auth       = AUTH_NULL,
};

/*
 * This function rounds up the input value to multiple of 4096. Min and Max
 * supported I/O size limits are 4KB (GF_NFS3_FILE_IO_SIZE_MIN) and
 * 1MB (GF_NFS3_FILE_IO_SIZE_MAX).
 */
void
nfs3_iosize_roundup_4KB (uint64_t *ioszptr)
{
        uint64_t iosize;
        uint64_t iopages;

        if (!ioszptr)
                return;

        iosize  = *ioszptr;
        iopages = (iosize + GF_NFS3_IO_SIZE -1) >> GF_NFS3_IO_SHIFT;
        iosize  = (iopages * GF_NFS3_IO_SIZE);

        /* Double check - boundary conditions */
        if (iosize < GF_NFS3_FILE_IO_SIZE_MIN) {
                iosize = GF_NFS3_FILE_IO_SIZE_MIN;
        } else if (iosize > GF_NFS3_FILE_IO_SIZE_MAX) {
                iosize = GF_NFS3_FILE_IO_SIZE_MAX;
        }

        *ioszptr = iosize;
}

int
nfs3_init_options (struct nfs3_state *nfs3, dict_t *options)
{
        int      ret = -1;
        char     *optstr = NULL;
        uint64_t size64 = 0;

        if ((!nfs3) || (!options))
                return -1;

        /* nfs3.read-size */
        nfs3->readsize = GF_NFS3_RTPREF;
        if (dict_get (options, "nfs3.read-size")) {
                ret = dict_get_str (options, "nfs3.read-size", &optstr);
                if (ret < 0) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_READ_FAIL,
                                "Failed to read option: nfs3.read-size");
                        ret = -1;
                        goto err;
                }

                ret = gf_string2uint64 (optstr, &size64);
                if (ret == -1) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_FORMAT_FAIL,
                                "Failed to format option: nfs3.read-size");
                        ret = -1;
                        goto err;
                }

                nfs3_iosize_roundup_4KB (&size64);
                nfs3->readsize = size64;
        }

        /* nfs3.write-size */
        nfs3->writesize = GF_NFS3_WTPREF;
        if (dict_get (options, "nfs3.write-size")) {
                ret = dict_get_str (options, "nfs3.write-size", &optstr);
                if (ret < 0) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_READ_FAIL,
                                "Failed to read  option: nfs3.write-size");
                        ret = -1;
                        goto err;
                }

                ret = gf_string2uint64 (optstr, &size64);
                if (ret == -1) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_FORMAT_FAIL,
                                "Failed to format option: nfs3.write-size");
                        ret = -1;
                        goto err;
                }

                nfs3_iosize_roundup_4KB (&size64);
                nfs3->writesize = size64;
        }

        /* nfs3.readdir.size */
        nfs3->readdirsize = GF_NFS3_DTPREF;
        if (dict_get (options, "nfs3.readdir-size")) {
                ret = dict_get_str (options,"nfs3.readdir-size", &optstr);
                if (ret < 0) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_READ_FAIL,
                                "Failed to read option: nfs3.readdir-size");
                        ret = -1;
                        goto err;
                }

                ret = gf_string2uint64 (optstr, &size64);
                if (ret == -1) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_FORMAT_FAIL,
                                "Failed to format option: nfs3.readdir-size");
                        ret = -1;
                        goto err;
                }

                nfs3_iosize_roundup_4KB (&size64);
                nfs3->readdirsize = size64;
        }

        /* We want to use the size of the biggest param for the io buffer size.
         */
        nfs3->iobsize = nfs3->readsize;
        if (nfs3->iobsize < nfs3->writesize)
                nfs3->iobsize = nfs3->writesize;
        if (nfs3->iobsize < nfs3->readdirsize)
                nfs3->iobsize = nfs3->readdirsize;

        /* But this is the true size of each iobuf. We need this size to
         * accommodate the NFS headers also in the same buffer. */
        nfs3->iobsize = nfs3->iobsize * 2;

        ret = 0;
err:
        return ret;
}

int
nfs3_init_subvolume_options (xlator_t *nfsx,
                             struct nfs3_export *exp,
                             dict_t *options)
{
        int             ret = -1;
        char            *optstr = NULL;
        char            searchkey[1024];
        char            *name = NULL;
        gf_boolean_t    boolt = _gf_false;
        uuid_t          volumeid = {0, };

        if ((!nfsx) || (!exp))
                return -1;

        /* For init, fetch options from xlator but for
         * reconfigure, take the parameter */
        if (!options)
                options = nfsx->options;

        if (!options)
                return (-1);

        gf_uuid_clear (volumeid);
        if (gf_nfs_dvm_off (nfs_state (nfsx)))
                goto no_dvm;

        ret = snprintf (searchkey, 1024, "nfs3.%s.volume-id",exp->subvol->name);
        if (ret < 0) {
                gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_SNPRINTF_FAIL,
                        "snprintf failed");
                ret = -1;
                goto err;
        }

        if (dict_get (options, searchkey)) {
                ret = dict_get_str (options, searchkey, &optstr);
                if (ret < 0) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_READ_FAIL,
                                "Failed to read option: %s", searchkey);
                        ret = -1;
                        goto err;
                }
        } else {
                gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_VOLID_MISSING, "DVM is"
                        " on but volume-id not given for volume: %s",
                        exp->subvol->name);
                ret = -1;
                goto err;
        }

        if (optstr) {
                ret = gf_uuid_parse (optstr, volumeid);
                if (ret < 0) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, 0,
                                NFS_MSG_PARSE_VOL_UUID_FAIL,
                                "Failed to parse volume UUID");
                        ret = -1;
                        goto err;
                }
                gf_uuid_copy (exp->volumeid, volumeid);
        }

no_dvm:
        /* Volume Access */
        name = exp->subvol->name;
        ret = snprintf (searchkey, 1024, "nfs3.%s.volume-access", name);
        if (ret < 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_SNPRINTF_FAIL,
                        "snprintf failed");
                ret = -1;
                goto err;
        }

        exp->access = GF_NFS3_DEFAULT_VOLACCESS;
        if (dict_get (options, searchkey)) {
                ret = dict_get_str (options, searchkey, &optstr);
                if (ret < 0) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_READ_FAIL,
                                "Failed to read option: %s", searchkey);
                        ret = -1;
                        goto err;
                }

                if (strcmp (optstr, "read-only") == 0)
                        exp->access = GF_NFS3_VOLACCESS_RO;
        }

        ret = snprintf (searchkey, 1024, "rpc-auth.%s.unix", name);
        if (ret < 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_SNPRINTF_FAIL,
                        "snprintf failed");
                ret = -1;
                goto err;
        }

        if (dict_get (options, searchkey)) {
                ret = dict_get_str (options, searchkey, &optstr);
                if (ret < 0) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_READ_FAIL,
                                "Failed to read option: %s", searchkey);
                        ret = -1;
                        goto err;
                }
        }

        exp->trusted_sync = 0;
        ret = snprintf (searchkey, 1024, "nfs3.%s.trusted-sync", name);
        if (ret < 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_SNPRINTF_FAIL,
                        "snprintf failed");
                ret = -1;
                goto err;
        }

        if (dict_get (options, searchkey)) {
                ret = dict_get_str (options, searchkey, &optstr);
                if (ret < 0) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_READ_FAIL,
                                "Failed to read option: %s", searchkey);
                        ret = -1;
                        goto err;
                }

                ret = gf_string2boolean (optstr, &boolt);
                if (ret < 0) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0,
                                NFS_MSG_STR2BOOL_FAIL, "Failed to convert str "
                                "to gf_boolean_t");
                        ret = -1;
                        goto err;
                }

                if (boolt == _gf_true)
                        exp->trusted_sync = 1;
        }

        exp->trusted_write = 0;
        ret = snprintf (searchkey, 1024, "nfs3.%s.trusted-write", name);
        if (ret < 0) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_SNPRINTF_FAIL,
                        "snprintf failed");
                ret = -1;
                goto err;
        }

        if (dict_get (options, searchkey)) {
                ret = dict_get_str (options, searchkey, &optstr);
                if (ret < 0) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_READ_FAIL,
                                "Failed to read option: %s", searchkey);
                        ret = -1;
                        goto err;
                }

                ret = gf_string2boolean (optstr, &boolt);
                if (ret < 0) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0,
                                NFS_MSG_STR2BOOL_FAIL, "Failed to convert str"
                                " to gf_boolean_t");
                        ret = -1;
                        goto err;
                }

                if (boolt == _gf_true)
                        exp->trusted_write = 1;
        }

        /* If trusted-sync is on, then we also switch on trusted-write because
         * tw is included in ts. In write logic, we're then only checking for
         * tw.
         */
        if (exp->trusted_sync)
                exp->trusted_write = 1;

        gf_msg_trace (GF_NFS3, 0, "%s: %s, %s, %s", exp->subvol->name,
                (exp->access == GF_NFS3_VOLACCESS_RO)?"read-only":"read-write",
                (exp->trusted_sync == 0)?"no trusted_sync":"trusted_sync",
                (exp->trusted_write == 0)?"no trusted_write":"trusted_write");
        ret = 0;
err:
        return ret;
}


struct nfs3_export *
nfs3_init_subvolume (struct nfs3_state *nfs3, xlator_t *subvol)
{
        int                     ret = -1;
        struct nfs3_export      *exp = NULL;

        if ((!nfs3) || (!subvol))
                return NULL;

        exp = GF_CALLOC (1, sizeof (*exp), gf_nfs_mt_nfs3_export);
        exp->subvol = subvol;
        INIT_LIST_HEAD (&exp->explist);
        gf_msg_trace (GF_NFS3, 0, "Initing state: %s", exp->subvol->name);

        ret = nfs3_init_subvolume_options (nfs3->nfsx, exp, NULL);
        if (ret == -1) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_SUBVOL_INIT_FAIL,
                        "Failed to init subvol");
                goto exp_free;
        }

        ret = 0;
exp_free:
        if (ret < 0) {
                GF_FREE (exp);
                exp = NULL;
        }

        return exp;
}


int
nfs3_init_subvolumes (struct nfs3_state *nfs3)
{
        int                     ret = -1;
        struct xlator_list      *xl_list = NULL;
        struct nfs3_export      *exp = NULL;

        if (!nfs3)
                return -1;

        xl_list = nfs3->nfsx->children;

        while (xl_list) {
                exp = nfs3_init_subvolume (nfs3, xl_list->xlator);
                if (!exp) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0,
                                NFS_MSG_SUBVOL_INIT_FAIL, "Failed to init "
                                "subvol: %s", xl_list->xlator->name);
                        goto err;
                }
                list_add_tail (&exp->explist, &nfs3->exports);
                xl_list = xl_list->next;
        }

        ret = 0;
err:
        return ret;
}


struct nfs3_state *
nfs3_init_state (xlator_t *nfsx)
{
        struct nfs3_state       *nfs3 = NULL;
        int                     ret = -1;
        unsigned int            localpool = 0;
        struct nfs_state        *nfs = NULL;

        if ((!nfsx) || (!nfsx->private))
                return NULL;

        nfs3 = (struct nfs3_state *)GF_CALLOC (1, sizeof (*nfs3),
                                    gf_nfs_mt_nfs3_state);
        if (!nfs3) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Memory allocation failed");
                return NULL;
        }

        nfs = nfsx->private;
        ret = nfs3_init_options (nfs3, nfsx->options);
        if (ret == -1) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_OPT_INIT_FAIL,
                        "Failed to init options");
                goto ret;
        }

        nfs3->iobpool = nfsx->ctx->iobuf_pool;

        localpool = nfs->memfactor * GF_NFS_CONCURRENT_OPS_MULT;
        gf_msg_trace (GF_NFS3, 0, "local pool: %d", localpool);
        nfs3->localpool = mem_pool_new (nfs3_call_state_t, localpool);
        if (!nfs3->localpool) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "local mempool creation failed");
                ret = -1;
                goto ret;
        }

        nfs3->nfsx = nfsx;
        nfs3->exportslist = nfsx->children;
        INIT_LIST_HEAD (&nfs3->exports);
        ret = nfs3_init_subvolumes (nfs3);
        if (ret == -1) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_SUBVOL_INIT_FAIL,
                        "Failed to init per-subvolume state");
                goto free_localpool;
        }

        nfs3->serverstart = (uint64_t)time (NULL);
        INIT_LIST_HEAD (&nfs3->fdlru);
        LOCK_INIT (&nfs3->fdlrulock);
        nfs3->fdcount = 0;

        ret = rpcsvc_create_listeners (nfs->rpcsvc, nfsx->options, nfsx->name);
        if (ret == -1) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_LISTENERS_CREATE_FAIL,
                        "Unable to create listeners");
                goto free_localpool;
        }

        nfs->nfs3state = nfs3;
        ret = 0;

free_localpool:
        if (ret == -1)
                mem_pool_destroy (nfs3->localpool);

ret:
        if (ret == -1) {
                GF_FREE (nfs3);
                nfs3 = NULL;
        }

        return nfs3;
}


rpcsvc_program_t *
nfs3svc_init (xlator_t *nfsx)
{
        struct nfs3_state       *nfs3 = NULL;

        if (!nfsx)
                return NULL;

        nfs3 = nfs3_init_state (nfsx);
        if (!nfs3) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_STATE_INIT_FAIL,
                        "NFSv3 state init failed");
                return NULL;
        }

        nfs3prog.private = nfs3;

        return &nfs3prog;
}

int
nfs3_reconfigure_state (xlator_t *nfsx, dict_t *options)
{
        int                   ret   = -1;
        struct nfs3_export    *exp  = NULL;
        struct nfs_state      *nfs  = NULL;
        struct nfs3_state     *nfs3 = NULL;

        if ((!nfsx) || (!nfsx->private) || (!options))
                goto out;

        nfs = (struct nfs_state *)nfsx->private;
        nfs3 = nfs->nfs3state;
        if (!nfs3)
                goto out;

        ret = nfs3_init_options (nfs3, options);
        if (ret) {
                gf_msg (GF_NFS3, GF_LOG_ERROR, 0, NFS_MSG_RECONF_FAIL,
                        "Failed to reconfigure options");
                goto out;
        }

        list_for_each_entry (exp, &nfs3->exports, explist) {
                ret = nfs3_init_subvolume_options (nfsx, exp, options);
                if (ret) {
                        gf_msg (GF_NFS3, GF_LOG_ERROR, 0,
                                NFS_MSG_RECONF_SUBVOL_FAIL,
                                "Failed to reconfigure subvol options");
                        goto out;
                }
        }

        ret = 0;
out:
        return ret;
}
