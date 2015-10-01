/*
  Copyright (c) 2012 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "defaults.h"
#include "rpcsvc.h"
#include "dict.h"
#include "xlator.h"
#include "nfs.h"
#include "mem-pool.h"
#include "logging.h"
#include "syscall.h"
#include "nfs-fops.h"
#include "inode.h"
#include "mount3.h"
#include "nfs3.h"
#include "nfs-mem-types.h"
#include "nfs3-helpers.h"
#include "nfs3-fh.h"
#include "nlm4.h"
#include "nlm4-xdr.h"
#include "msg-nfs3.h"
#include "nfs-generics.h"
#include "rpc-clnt.h"
#include "nsm-xdr.h"
#include "run.h"
#include "nfs-messages.h"
#include <unistd.h>
#include <rpc/pmap_clnt.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <statedump.h>

#ifdef __NetBSD__
#define KILLALL_CMD "pkill"
#else
#define KILLALL_CMD "killall"
#endif

/* TODO:
 * 1) 2 opens racing .. creating an fd leak.
 * 2) use mempool for nlmclnt - destroy if no fd exists, create during 1st call
 */

typedef ssize_t (*nlm4_serializer) (struct iovec outmsg, void *args);

extern void nfs3_call_state_wipe (nfs3_call_state_t *cs);

struct list_head nlm_client_list;
gf_lock_t nlm_client_list_lk;

/* race on this is harmless */
int nlm_grace_period = 50;

#define nlm4_validate_nfs3_state(request, state, status, label, retval) \
        do      {                                                       \
                state = rpcsvc_request_program_private (request);       \
                if (!state) {                                           \
                        gf_msg (GF_NLM, GF_LOG_ERROR, errno,            \
                                NFS_MSG_STATE_MISSING, "NFSv3 state "   \
                                "missing from RPC request");            \
                        rpcsvc_request_seterr (req, SYSTEM_ERR);        \
                        status = nlm4_failed;                           \
                        goto label;                                     \
                }                                                       \
        } while (0);                                                    \

nfs3_call_state_t *
nfs3_call_state_init (struct nfs3_state *s, rpcsvc_request_t *req, xlator_t *v);

#define nlm4_handle_call_state_init(nfs3state, calls, rq, opstat, errlabel)\
        do {                                                            \
                calls = nlm4_call_state_init ((nfs3state), (rq));       \
                if (!calls) {                                           \
                        gf_msg (GF_NLM, GF_LOG_ERROR, errno,            \
                                NFS_MSG_INIT_CALL_STAT_FAIL, "Failed to "\
                                "init call state");                     \
                        opstat = nlm4_failed;                           \
                        rpcsvc_request_seterr (req, SYSTEM_ERR);        \
                        goto errlabel;                                  \
                }                                                       \
        } while (0)                                                     \

#define nlm4_validate_gluster_fh(handle, status, errlabel)              \
        do {                                                            \
                if (!nfs3_fh_validate (handle)) {                       \
                        status = nlm4_stale_fh;                         \
                        goto errlabel;                                  \
                }                                                       \
        } while (0)                                                     \

xlator_t *
nfs3_fh_to_xlator (struct nfs3_state *nfs3, struct nfs3_fh *fh);

#define nlm4_map_fh_to_volume(nfs3state, handle, req, volume, status, label) \
        do {                                                            \
                char exportid[256], gfid[256];                          \
                rpc_transport_t *trans = NULL;                          \
                volume = nfs3_fh_to_xlator ((nfs3state), &handle);      \
                if (!volume) {                                          \
                        gf_uuid_unparse (handle.exportid, exportid);       \
                        gf_uuid_unparse (handle.gfid, gfid);               \
                        trans = rpcsvc_request_transport (req);         \
                        gf_msg (GF_NLM, GF_LOG_ERROR, errno,             \
                                NFS_MSG_FH_TO_VOL_FAIL, "Failed to map "  \
                                "FH to vol: client=%s, exportid=%s, gfid=%s",\
                                trans->peerinfo.identifier, exportid,   \
                                gfid);                                  \
                        gf_msg (GF_NLM, GF_LOG_ERROR, errno,            \
                                NFS_MSG_VOLUME_ERROR,                   \
                                "Stale nfs client %s must be trying to "\
                                "connect to a deleted volume, please "  \
                                "unmount it.", trans->peerinfo.identifier);\
                        status = nlm4_stale_fh;                         \
                        goto label;                                     \
                } else {                                                \
                        gf_msg_trace (GF_NLM, 0, "FH to Volume: %s"     \
                                      , volume->name);                  \
                        rpcsvc_request_set_private (req, volume);       \
                }                                                       \
        } while (0);                                                    \

#define nlm4_volume_started_check(nfs3state, vlm, rtval, erlbl)         \
        do {                                                            \
              if ((!nfs_subvolume_started (nfs_state (nfs3state->nfsx), vlm))){\
                        gf_msg (GF_NLM, GF_LOG_ERROR, 0, NFS_MSG_VOL_DISABLE, \
                                "Volume is disabled: %s",               \
                                vlm->name);                             \
                      rtval = RPCSVC_ACTOR_IGNORE;                      \
                      goto erlbl;                                       \
              }                                                         \
        } while (0)                                                     \

#define nlm4_check_fh_resolve_status(cst, nfstat, erlabl)               \
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
                        gf_msg (GF_NLM, GF_LOG_ERROR, 0,                \
                                NFS_MSG_RESOLVE_FH_FAIL, "Unable to resolve FH"\
                                ": %s", buf);                           \
                        nfstat = nlm4_errno_to_nlm4stat (cst->resolve_errno);\
                        goto erlabl;                                    \
                }                                                       \
        } while (0)                                                     \


void
nlm4_prep_nlm4_testargs (nlm4_testargs *args, struct nfs3_fh *fh,
                         nlm4_lkowner_t *oh, char *cookiebytes)
{
        memset (args, 0, sizeof (*args));
        args->alock.fh.nlm4_netobj_val = (void *)fh;
        args->alock.oh.nlm4_netobj_val = (void *)oh;
        args->cookie.nlm4_netobj_val = (void *)cookiebytes;
}

void
nlm4_prep_nlm4_lockargs (nlm4_lockargs *args, struct nfs3_fh *fh,
                         nlm4_lkowner_t *oh, char *cookiebytes)
{
        memset (args, 0, sizeof (*args));
        args->alock.fh.nlm4_netobj_val = (void *)fh;
        args->alock.oh.nlm4_netobj_val = (void *)oh;
        args->cookie.nlm4_netobj_val = (void *)cookiebytes;
}

void
nlm4_prep_nlm4_cancargs (nlm4_cancargs *args, struct nfs3_fh *fh,
                           nlm4_lkowner_t *oh, char *cookiebytes)
{
        memset (args, 0, sizeof (*args));
        args->alock.fh.nlm4_netobj_val = (void *)fh;
        args->alock.oh.nlm4_netobj_val = (void *)oh;
        args->cookie.nlm4_netobj_val = (void *)cookiebytes;
}

void
nlm4_prep_nlm4_unlockargs (nlm4_unlockargs *args, struct nfs3_fh *fh,
                           nlm4_lkowner_t *oh, char *cookiebytes)
{
        memset (args, 0, sizeof (*args));
        args->alock.fh.nlm4_netobj_val = (void *)fh;
        args->alock.oh.nlm4_netobj_val = (void *)oh;
        args->cookie.nlm4_netobj_val = (void *)cookiebytes;
}

void
nlm4_prep_shareargs (nlm4_shareargs *args, struct nfs3_fh *fh,
                     nlm4_lkowner_t *oh, char *cookiebytes)
{
        memset (args, 0, sizeof (*args));
        args->share.fh.nlm4_netobj_val = (void *)fh;
        args->share.oh.nlm4_netobj_val = (void *)oh;
        args->cookie.nlm4_netobj_val = (void *)cookiebytes;
}

void
nlm4_prep_freeallargs (nlm4_freeallargs *args, nlm4_lkowner_t *oh)
{
        memset (args, 0, sizeof (*args));
        args->name = (void *)oh;
}

void
nlm_copy_lkowner (gf_lkowner_t *dst, nlm4_netobj *src)
{
        dst->len = src->nlm4_netobj_len;
        memcpy (dst->data, src->nlm4_netobj_val, dst->len);
}

int
nlm_is_oh_same_lkowner (gf_lkowner_t *a, nlm4_netobj *b)
{
        if (!a || !b) {
                gf_msg (GF_NLM, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "invalid args");
                return -1;
        }

        return (a->len == b->nlm4_netobj_len &&
                !memcmp (a->data, b->nlm4_netobj_val, a->len));
}

nlm4_stats
nlm4_errno_to_nlm4stat (int errnum)
{
        nlm4_stats        stat = nlm4_denied;

        switch (errnum) {
        case 0:
                stat = nlm4_granted;
                break;
        case EROFS:
                stat = nlm4_rofs;
                break;
        case ESTALE:
                stat = nlm4_stale_fh;
                break;
        case ENOLCK:
                stat = nlm4_failed;
                break;
        default:
                stat = nlm4_denied;
                break;
        }

        return stat;
}

nfs3_call_state_t *
nlm4_call_state_init (struct nfs3_state *s, rpcsvc_request_t *req)
{
        nfs3_call_state_t       *cs = NULL;

        if ((!s) || (!req))
                return NULL;

        cs = (nfs3_call_state_t *) mem_get (s->localpool);
        if (!cs)
                return NULL;

        memset (cs, 0, sizeof (*cs));
        INIT_LIST_HEAD (&cs->entries.list);
        INIT_LIST_HEAD (&cs->openwait_q);
        cs->operrno = EINVAL;
        cs->req = req;
        cs->nfsx = s->nfsx;
        cs->nfs3state = s;
        cs->monitor = 1;

        return cs;
}

int
nlm_monitor (char *caller_name)
{
        nlm_client_t *nlmclnt = NULL;
        int monitor = -1;

        LOCK (&nlm_client_list_lk);
        list_for_each_entry (nlmclnt, &nlm_client_list, nlm_clients) {
                if (!strcmp(caller_name, nlmclnt->caller_name)) {
                        monitor = nlmclnt->nsm_monitor;
                        nlmclnt->nsm_monitor = 1;
                        break;
                }
        }
        UNLOCK (&nlm_client_list_lk);

        if (monitor == -1)
                gf_msg (GF_NLM, GF_LOG_ERROR, 0, NFS_MSG_CALLER_NOT_FOUND,
                        "%s was not found in the nlmclnt list", caller_name);

        return monitor;
}

rpc_clnt_t *
nlm_get_rpc_clnt (char *caller_name)
{
        nlm_client_t *nlmclnt = NULL;
        int nlmclnt_found = 0;
        rpc_clnt_t *rpc_clnt = NULL;

        LOCK (&nlm_client_list_lk);
        list_for_each_entry (nlmclnt, &nlm_client_list, nlm_clients) {
                if (!strcmp(caller_name, nlmclnt->caller_name)) {
                        nlmclnt_found = 1;
                        break;
                }
        }
        if (!nlmclnt_found)
                goto ret;
        if (nlmclnt->rpc_clnt)
                rpc_clnt = rpc_clnt_ref (nlmclnt->rpc_clnt);
ret:
        UNLOCK (&nlm_client_list_lk);
        return rpc_clnt;
}

int
nlm_set_rpc_clnt (rpc_clnt_t *rpc_clnt, char *caller_name)
{
        nlm_client_t *nlmclnt = NULL;
        int nlmclnt_found = 0;
        int ret = -1;

        LOCK (&nlm_client_list_lk);
        list_for_each_entry (nlmclnt, &nlm_client_list, nlm_clients) {
                if (!strcmp(caller_name, nlmclnt->caller_name)) {
                        nlmclnt_found = 1;
                        break;
                }
        }

        if (!nlmclnt_found) {
                nlmclnt = GF_CALLOC (1, sizeof(*nlmclnt),
                                     gf_nfs_mt_nlm4_nlmclnt);
                if (nlmclnt == NULL)
                        goto ret;

                INIT_LIST_HEAD(&nlmclnt->fdes);
                INIT_LIST_HEAD(&nlmclnt->nlm_clients);
                INIT_LIST_HEAD(&nlmclnt->shares);

                list_add (&nlmclnt->nlm_clients, &nlm_client_list);
                nlmclnt->caller_name = gf_strdup (caller_name);
        }

        if (nlmclnt->rpc_clnt == NULL) {
                nlmclnt->rpc_clnt = rpc_clnt_ref (rpc_clnt);
        }
        ret = 0;
ret:
        UNLOCK (&nlm_client_list_lk);
        return ret;
}

int
nlm_unset_rpc_clnt (rpc_clnt_t *rpc)
{
        nlm_client_t *nlmclnt = NULL;
        rpc_clnt_t *rpc_clnt = NULL;

        LOCK (&nlm_client_list_lk);
        list_for_each_entry (nlmclnt, &nlm_client_list, nlm_clients) {
                if (rpc == nlmclnt->rpc_clnt) {
                        rpc_clnt = nlmclnt->rpc_clnt;
                        nlmclnt->rpc_clnt = NULL;
                        break;
                }
        }
        UNLOCK (&nlm_client_list_lk);
        if (rpc_clnt == NULL) {
                return -1;
        }
        if (rpc_clnt) {
                /* cleanup the saved-frames before last unref */
                rpc_clnt_connection_cleanup (&rpc_clnt->conn);

                rpc_clnt_unref (rpc_clnt);
        }
        return 0;
}

int
nlm_add_nlmclnt (char *caller_name)
{
        nlm_client_t *nlmclnt = NULL;
        int nlmclnt_found = 0;
        int ret = -1;

        LOCK (&nlm_client_list_lk);
        list_for_each_entry (nlmclnt, &nlm_client_list, nlm_clients) {
                if (!strcmp(caller_name, nlmclnt->caller_name)) {
                        nlmclnt_found = 1;
                        break;
                }
        }
        if (!nlmclnt_found) {
                nlmclnt = GF_CALLOC (1, sizeof(*nlmclnt),
                                     gf_nfs_mt_nlm4_nlmclnt);
                if (nlmclnt == NULL) {
                        gf_msg (GF_NLM, GF_LOG_ERROR, ENOMEM,
                                NFS_MSG_NO_MEMORY, "malloc error");
                        goto ret;
                }

                INIT_LIST_HEAD(&nlmclnt->fdes);
                INIT_LIST_HEAD(&nlmclnt->nlm_clients);
                INIT_LIST_HEAD(&nlmclnt->shares);

                list_add (&nlmclnt->nlm_clients, &nlm_client_list);
                nlmclnt->caller_name = gf_strdup (caller_name);
        }
        ret = 0;
ret:
        UNLOCK (&nlm_client_list_lk);
        return ret;
}

int
nlm4svc_submit_reply (rpcsvc_request_t *req, void *arg, nlm4_serializer sfunc)
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
                gf_msg (GF_NLM, GF_LOG_ERROR, EINVAL,
                        NFS_MSG_MNT_STATE_NOT_FOUND, "mount state not found");
                goto ret;
        }

        /* First, get the io buffer into which the reply in arg will
         * be serialized.
         */
        iob = iobuf_get (nfs3->iobpool);
        if (!iob) {
                gf_msg (GF_NLM, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to get iobuf");
                goto ret;
        }

        iobuf_to_iovec (iob, &outmsg);
        /* Use the given serializer to translate the give C structure in arg
         * to XDR format which will be written into the buffer in outmsg.
         */
        msglen = sfunc (outmsg, arg);
        if (msglen < 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_ENCODE_MSG_FAIL,
                        "Failed to encode message");
                goto ret;
        }
        outmsg.iov_len = msglen;

        iobref = iobref_new ();
        if (iobref == NULL) {
                gf_msg (GF_NLM, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to get iobref");
                goto ret;
        }

        ret = iobref_add (iobref, iob);
        if (ret) {
                gf_msg (GF_NLM, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to add iob to iobref");
                goto ret;
        }

        /* Then, submit the message for transmission. */
        ret = rpcsvc_submit_message (req, &outmsg, 1, NULL, 0, iobref);
        if (ret == -1) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_REP_SUBMIT_FAIL,
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

typedef int (*nlm4_resume_fn_t) (void *cs);

int32_t
nlm4_file_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        nfs3_call_state_t *cs = frame->local;

        if (op_ret == 0)
                fd_bind (cs->fd);
        cs->resolve_ret = op_ret;
        cs->resume_fn (cs);
        frame->local = NULL;
        STACK_DESTROY (frame->root);
        return 0;
}

void *
nsm_monitor(void *arg)
{
        CLIENT *clnt = NULL;
        enum clnt_stat ret;
        struct mon nsm_mon;
        struct sm_stat_res res;
        struct timeval tout = { 5, 0 };
        char *host = NULL;

        host = arg;
        nsm_mon.mon_id.mon_name = gf_strdup(host);
        nsm_mon.mon_id.my_id.my_name = gf_strdup("localhost");
        nsm_mon.mon_id.my_id.my_prog = NLMCBK_PROGRAM;
        nsm_mon.mon_id.my_id.my_vers = NLMCBK_V1;
        nsm_mon.mon_id.my_id.my_proc = NLMCBK_SM_NOTIFY;
        /* nothing to put in the private data */
#define SM_PROG 100024
#define SM_VERS 1
#define SM_MON 2

        /* create a connection to nsm on the localhost */
        clnt = clnt_create("localhost", SM_PROG, SM_VERS, "tcp");
        if(!clnt)
        {
                gf_msg (GF_NLM, GF_LOG_ERROR, 0, NFS_MSG_CLNT_CREATE_ERROR,
                        "%s", clnt_spcreateerror ("Clnt_create()"));
                goto out;
        }

        ret = clnt_call(clnt, SM_MON,
                        (xdrproc_t) xdr_mon, (caddr_t) & nsm_mon,
                        (xdrproc_t) xdr_sm_stat_res, (caddr_t) & res, tout);
        if(ret != RPC_SUCCESS)
        {
                gf_msg (GF_NLM, GF_LOG_ERROR, 0, NFS_MSG_CLNT_CALL_ERROR,
                        "clnt_call(): %s", clnt_sperrno(ret));
                goto out;
        }
        if(res.res_stat != STAT_SUCC)
        {
                gf_msg (GF_NLM, GF_LOG_ERROR, 0, NFS_MSG_CLNT_CALL_ERROR,
                        "clnt_call(): %s", clnt_sperrno(ret));
                goto out;
        }

out:
        GF_FREE(nsm_mon.mon_id.mon_name);
        GF_FREE(nsm_mon.mon_id.my_id.my_name);
        if (clnt != NULL)
                clnt_destroy(clnt);
        return NULL;
}

nlm_client_t *
__nlm_get_uniq (char *caller_name)
{
        nlm_client_t *nlmclnt = NULL;

        if (!caller_name)
                return NULL;

        list_for_each_entry (nlmclnt, &nlm_client_list, nlm_clients) {
                if (!strcmp(caller_name, nlmclnt->caller_name))
                        return nlmclnt;
        }

        return NULL;
}

nlm_client_t *
nlm_get_uniq (char *caller_name)
{
        nlm_client_t *nlmclnt = NULL;

        LOCK (&nlm_client_list_lk);
        nlmclnt = __nlm_get_uniq (caller_name);
        UNLOCK (&nlm_client_list_lk);

        return nlmclnt;
}


int
nlm4_file_open_and_resume(nfs3_call_state_t *cs, nlm4_resume_fn_t resume)
{
        fd_t *fd = NULL;
        int ret = -1;
        int flags = 0;
        nlm_client_t *nlmclnt = NULL;
        call_frame_t *frame = NULL;

        if (cs->args.nlm4_lockargs.exclusive == _gf_false)
                flags = O_RDONLY;
        else
                flags = O_WRONLY;

        nlmclnt = nlm_get_uniq (cs->args.nlm4_lockargs.alock.caller_name);
        if (nlmclnt == NULL) {
                gf_msg (GF_NLM, GF_LOG_ERROR, ENOLCK,
                        NFS_MSG_NO_MEMORY, "nlm_get_uniq() "
                        "returned NULL");
                ret = -ENOLCK;
                goto err;
        }
        cs->resume_fn = resume;
        fd = fd_lookup_uint64 (cs->resolvedloc.inode, (uint64_t)nlmclnt);
        if (fd) {
                cs->fd = fd;
                cs->resolve_ret = 0;
                cs->resume_fn(cs);
                ret = 0;
                goto err;
        }

        fd = fd_create_uint64 (cs->resolvedloc.inode, (uint64_t)nlmclnt);
        if (fd == NULL) {
                gf_msg (GF_NLM, GF_LOG_ERROR, ENOLCK, NFS_MSG_NO_MEMORY,
                        "fd_create_uint64() returned NULL");
                ret = -ENOLCK;
                goto err;
        }

        cs->fd = fd;

        frame = create_frame (cs->nfsx, cs->nfsx->ctx->pool);
        if (!frame) {
                gf_msg (GF_NLM, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "unable to create frame");
                ret = -ENOMEM;
                goto err;
        }

        frame->root->pid = NFS_PID;
        frame->root->uid = rpcsvc_request_uid (cs->req);
        frame->root->gid = rpcsvc_request_gid (cs->req);
        frame->local = cs;
        nfs_fix_groups (cs->nfsx, frame->root);

        STACK_WIND_COOKIE (frame, nlm4_file_open_cbk, cs->vol, cs->vol,
                          cs->vol->fops->open, &cs->resolvedloc, flags,
                          cs->fd, NULL);
        ret = 0;
err:
        return ret;
}

int
nlm4_generic_reply (rpcsvc_request_t *req, nlm4_netobj cookie, nlm4_stats stat)
{
        nlm4_res res;

        memset (&res, 0, sizeof (res));
        res.cookie = cookie;
        res.stat.stat = stat;

        nlm4svc_submit_reply (req, (void *)&res,
                              (nlm4_serializer)xdr_serialize_nlm4_res);
        return 0;
}

int
nlm4svc_null (rpcsvc_request_t *req)
{
        struct iovec    dummyvec = {0, };

        if (!req) {
                gf_msg (GF_NLM, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Got NULL request!");
                return 0;
        }
        rpcsvc_submit_generic (req, &dummyvec, 1,  NULL, 0, NULL);
        return 0;
}

int
nlm4_gf_flock_to_holder (nlm4_holder *holder, struct gf_flock *flock)
{
        switch (flock->l_type) {
        case GF_LK_F_WRLCK:
                holder->exclusive = 1;
                break;
        }

        holder->svid = flock->l_pid;
        holder->l_offset = flock->l_start;
        holder->l_len = flock->l_len;
        return 0;
}

int
nlm4_lock_to_gf_flock (struct gf_flock *flock, nlm4_lock *lock, int excl)
{
        flock->l_pid = lock->svid;
        flock->l_start = lock->l_offset;
        flock->l_len = lock->l_len;
        if (excl)
                flock->l_type = F_WRLCK;
        else
                flock->l_type = F_RDLCK;
        flock->l_whence = SEEK_SET;
        nlm_copy_lkowner (&flock->l_owner, &lock->oh);
        return 0;
}

rpc_clnt_procedure_t nlm4_clnt_actors[NLM4_PROC_COUNT] = {
        [NLM4_NULL] = {"NULL", NULL},
        [NLM4_GRANTED] = {"GRANTED", NULL},
};

char *nlm4_clnt_names[NLM4_PROC_COUNT] = {
        [NLM4_NULL] = "NULL",
        [NLM4_GRANTED] = "GRANTED",
};

rpc_clnt_prog_t nlm4clntprog = {
        .progname = "NLMv4",
        .prognum = NLM_PROGRAM,
        .progver = NLM_V4,
        .numproc = NLM4_PROC_COUNT,
        .proctable = nlm4_clnt_actors,
        .procnames = nlm4_clnt_names,
};

int
nlm4_test_reply (nfs3_call_state_t *cs, nlm4_stats stat, struct gf_flock *flock)
{
        nlm4_testres res;

        memset (&res, 0, sizeof (res));
        res.cookie = cs->args.nlm4_testargs.cookie;
        res.stat.stat = stat;
        if (stat == nlm4_denied)
                nlm4_gf_flock_to_holder (&res.stat.nlm4_testrply_u.holder,
                                         flock);

        nlm4svc_submit_reply (cs->req, (void *)&res,
                              (nlm4_serializer)xdr_serialize_nlm4_testres);
        return 0;
}

int
nlm4svc_test_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct gf_flock *flock,
                  dict_t *xdata)
{
        nlm4_stats                      stat = nlm4_denied;
        nfs3_call_state_t              *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nlm4_errno_to_nlm4stat (op_errno);
                goto err;
        } else if (flock->l_type == F_UNLCK)
                stat = nlm4_granted;

err:
        nlm4_test_reply (cs, stat, flock);
        nfs3_call_state_wipe (cs);
        return 0;
}

int
nlm4_test_fd_resume (void *carg)
{
        int                             ret = -EFAULT;
        nfs_user_t                      nfu = {0, };
        nfs3_call_state_t               *cs = NULL;
        struct gf_flock                 flock = {0, };

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs_request_user_init (&nfu, cs->req);
        nlm4_lock_to_gf_flock (&flock, &cs->args.nlm4_testargs.alock,
                               cs->args.nlm4_testargs.exclusive);
        nlm_copy_lkowner (&nfu.lk_owner, &cs->args.nlm4_testargs.alock.oh);
        ret = nfs_lk (cs->nfsx, cs->vol, &nfu, cs->fd, F_GETLK, &flock,
                      nlm4svc_test_cbk, cs);

        return ret;
}


int
nlm4_test_resume (void *carg)
{
        nlm4_stats                      stat = nlm4_failed;
        int                             ret = -1;
        nfs3_call_state_t               *cs = NULL;
        fd_t                            *fd = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nlm4_check_fh_resolve_status (cs, stat, nlm4err);
        fd = fd_anonymous (cs->resolvedloc.inode);
        if (!fd)
                goto nlm4err;
        cs->fd = fd;
        ret = nlm4_test_fd_resume (cs);

nlm4err:
        if (ret < 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, -ret, NFS_MSG_OPEN_FAIL,
                        "unable to open_and_resume");
                stat = nlm4_errno_to_nlm4stat (-ret);
                nlm4_test_reply (cs, stat, NULL);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}

int
nlm4svc_test (rpcsvc_request_t *req)
{
        xlator_t                        *vol = NULL;
        nlm4_stats                      stat = nlm4_failed;
        struct nfs_state               *nfs = NULL;
        nfs3_state_t                   *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;
        int                             ret = RPCSVC_ACTOR_ERROR;
        struct nfs3_fh                  fh = {{0}, };

        if (!req)
                return ret;

        nlm4_validate_nfs3_state (req, nfs3, stat, rpcerr, ret);
        nfs = nfs_state (nfs3->nfsx);
        nlm4_handle_call_state_init (nfs->nfs3state, cs, req,
                                     stat, rpcerr);

        nlm4_prep_nlm4_testargs (&cs->args.nlm4_testargs, &fh, &cs->lkowner,
                                 cs->cookiebytes);
        if (xdr_to_nlm4_testargs(req->msg[0], &cs->args.nlm4_testargs) <= 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        nlm4_validate_gluster_fh (&fh, stat, nlm4err);
        nlm4_map_fh_to_volume (cs->nfs3state, fh, req, vol, stat, nlm4err);

        if (nlm_grace_period) {
                gf_msg (GF_NLM, GF_LOG_WARNING, 0, NFS_MSG_NLM_GRACE_PERIOD,
                        "NLM in grace period");
                stat = nlm4_denied_grace_period;
                nlm4_test_reply (cs, stat, NULL);
                nfs3_call_state_wipe (cs);
                return 0;
        }

        cs->vol = vol;
        nlm4_volume_started_check (nfs3, vol, ret, rpcerr);

        ret = nfs3_fh_resolve_and_resume (cs, &fh,
                                          NULL, nlm4_test_resume);

nlm4err:
        if (ret < 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, -ret, NFS_MSG_RESOLVE_ERROR,
                        "unable to resolve and resume");
                nlm4_test_reply (cs, stat, NULL);
                nfs3_call_state_wipe (cs);
                return 0;
        }

rpcerr:
        if (ret < 0)
                nfs3_call_state_wipe (cs);

        return ret;
}

int
nlm4svc_send_granted_cbk (struct rpc_req *req, struct iovec *iov, int count,
                          void *myframe)
{
        STACK_DESTROY (((call_frame_t*)myframe)->root);
        return 0;
}

void
nlm4svc_send_granted (nfs3_call_state_t *cs);

int
nlm_rpcclnt_notify (struct rpc_clnt *rpc_clnt, void *mydata,
                    rpc_clnt_event_t fn, void *data)
{
        int                ret         = 0;
        char              *caller_name = NULL;
        nfs3_call_state_t *cs          = NULL;

        cs = mydata;
        caller_name = cs->args.nlm4_lockargs.alock.caller_name;

        switch (fn) {
        case RPC_CLNT_CONNECT:
                ret = nlm_set_rpc_clnt (rpc_clnt, caller_name);
                if (ret == -1) {
                        gf_msg (GF_NLM, GF_LOG_ERROR, 0,
                                NFS_MSG_RPC_CLNT_ERROR, "Failed to set "
                                "rpc clnt");
                        goto err;
                }
                rpc_clnt_unref (rpc_clnt);
                nlm4svc_send_granted (cs);

                break;

        case RPC_CLNT_MSG:
                break;

        case RPC_CLNT_DISCONNECT:
                nlm_unset_rpc_clnt (rpc_clnt);
                break;
        default:
                break;
        }

 err:
        return 0;
}

void *
nlm4_establish_callback (void *csarg)
{
        int                           ret                        = -1;
        nfs3_call_state_t            *cs                         = NULL;
        union gf_sock_union           sock_union;
        dict_t                       *options                    = NULL;
        char                          peerip[INET6_ADDRSTRLEN+1] = {0};
        char                         *portstr                    = NULL;
        char                          myip[INET6_ADDRSTRLEN+1]   = {0};
        rpc_clnt_t                   *rpc_clnt                   = NULL;
        int                           port                       = -1;


        cs = (nfs3_call_state_t *) csarg;
        glusterfs_this_set (cs->nfsx);

        rpc_transport_get_peeraddr (cs->trans, NULL, 0, &sock_union.storage,
                                    sizeof (sock_union.storage));

        switch (sock_union.sa.sa_family) {
        case AF_INET6:
        /* can not come here as NLM listens on IPv4 */
                gf_msg (GF_NLM, GF_LOG_ERROR, EAFNOSUPPORT,
                        NFS_MSG_UNSUPPORTED_VERSION,
                        "NLM is not supported on IPv6 in this release");
                goto err;
/*
                inet_ntop (AF_INET6,
                           &((struct sockaddr_in6 *)sockaddr)->sin6_addr,
                           peerip, INET6_ADDRSTRLEN+1);
                break;
*/
        case AF_INET:
                inet_ntop (AF_INET, &sock_union.sin.sin_addr, peerip,
                           INET6_ADDRSTRLEN+1);
                inet_ntop (AF_INET, &(((struct sockaddr_in *)&cs->trans->myinfo.sockaddr)->sin_addr),
                           myip, INET6_ADDRSTRLEN + 1);

                break;
        default:
                break;
                /* FIXME: handle the error */
        }

        /* looks like libc rpc supports only ipv4 */
        port = pmap_getport (&sock_union.sin, NLM_PROGRAM,
                             NLM_V4, IPPROTO_TCP);

        if (port == 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, 0, NFS_MSG_GET_PORT_ERROR,
                        "Unable to get NLM port of the client."
                        " Is the firewall running on client?"
                        " OR Are RPC services running (rpcinfo -p)?");
                goto err;
        }

        options = dict_new();
        ret = dict_set_str (options, "transport-type", "socket");
        if (ret == -1) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_DICT_SET_FAILED,
                        "dict_set_str error");
                goto err;
        }

        ret = dict_set_dynstr (options, "remote-host", gf_strdup (peerip));
        if (ret == -1) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_DICT_SET_FAILED,
                        "dict_set_str error");
                goto err;
        }

        ret = gf_asprintf (&portstr, "%d", port);
        if (ret == -1)
                goto err;

        ret = dict_set_dynstr (options, "remote-port",
                               portstr);
        if (ret == -1) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_DICT_SET_FAILED,
                        "dict_set_dynstr error");
                goto err;
        }

        /* needed in case virtual IP is used */
        ret = dict_set_dynstr (options, "transport.socket.source-addr",
                               gf_strdup (myip));
        if (ret == -1)
                goto err;

        ret = dict_set_str (options, "auth-null", "on");
        if (ret == -1) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_DICT_SET_FAILED,
                        "dict_set_dynstr error");
                goto err;
        }

        /* TODO: is 32 frames in transit enough ? */
        rpc_clnt = rpc_clnt_new (options, cs->nfsx, "NLM-client", 32);
        if (rpc_clnt == NULL) {
                gf_msg (GF_NLM, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "rpc_clnt NULL");
                goto err;
        }

        ret = rpc_clnt_register_notify (rpc_clnt, nlm_rpcclnt_notify, cs);
        if (ret == -1) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_RPC_CLNT_ERROR,
                        "rpc_clnt_register_connect error");
                goto err;
        }

        /* After this connect succeeds, granted msg is sent in notify */
        ret = rpc_transport_connect (rpc_clnt->conn.trans, port);

        if (ret == -1 && EINPROGRESS == errno)
                ret = 0;

err:
        if (ret == -1 && rpc_clnt) {
                rpc_clnt_unref (rpc_clnt);
        }

        return rpc_clnt;
}

void
nlm4svc_send_granted (nfs3_call_state_t *cs)
{
        int ret = -1;
        rpc_clnt_t *rpc_clnt = NULL;
        struct iovec            outmsg = {0, };
        nlm4_testargs testargs;
        struct iobuf *iobuf = NULL;
        struct iobref *iobref = NULL;
        char peerip[INET6_ADDRSTRLEN+1];
        union gf_sock_union sock_union;

        rpc_clnt = nlm_get_rpc_clnt (cs->args.nlm4_lockargs.alock.caller_name);
        if (rpc_clnt == NULL) {
                nlm4_establish_callback ((void*)cs);
                return;
        }

        rpc_transport_get_peeraddr (cs->trans, NULL, 0, &sock_union.storage,
                                    sizeof (sock_union.storage));

        switch (sock_union.sa.sa_family) {
        case AF_INET6:
                inet_ntop (AF_INET6, &sock_union.sin6.sin6_addr, peerip,
                           INET6_ADDRSTRLEN+1);
                break;
        case AF_INET:
                inet_ntop (AF_INET, &sock_union.sin.sin_addr, peerip,
                           INET6_ADDRSTRLEN+1);
                break;
        default:
                break;
        }

        testargs.cookie = cs->args.nlm4_lockargs.cookie;
        testargs.exclusive = cs->args.nlm4_lockargs.exclusive;
        testargs.alock = cs->args.nlm4_lockargs.alock;

        iobuf = iobuf_get (cs->nfs3state->iobpool);
        if (!iobuf) {
                gf_msg (GF_NLM, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to get iobuf");
                goto ret;
        }

        iobuf_to_iovec (iobuf, &outmsg);
        /* Use the given serializer to translate the give C structure in arg
         * to XDR format which will be written into the buffer in outmsg.
         */
        outmsg.iov_len = xdr_serialize_nlm4_testargs (outmsg, &testargs);

        iobref = iobref_new ();
        if (iobref == NULL) {
                gf_msg (GF_NLM, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to get iobref");
                goto ret;
        }

        ret = iobref_add (iobref, iobuf);
        if (ret) {
                gf_msg (GF_NLM, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to add iob to iobref");
                goto ret;
        }

        ret = rpc_clnt_submit (rpc_clnt, &nlm4clntprog, NLM4_GRANTED,
                               nlm4svc_send_granted_cbk, &outmsg, 1,
                               NULL, 0, iobref, cs->frame, NULL, 0,
                               NULL, 0, NULL);

        if (ret < 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, -ret, NFS_MSG_RPC_CLNT_ERROR,
                        "rpc_clnt_submit error");
                goto ret;
        }
ret:
        if (iobref)
                iobref_unref (iobref);
        if (iobuf)
                iobuf_unref (iobuf);

        rpc_clnt_unref (rpc_clnt);
        nfs3_call_state_wipe (cs);
        return;
}

int
nlm_cleanup_fds (char *caller_name)
{
        int nlmclnt_found = 0;
        nlm_fde_t *fde = NULL, *tmp = NULL;
        nlm_client_t *nlmclnt = NULL;

        LOCK (&nlm_client_list_lk);
        list_for_each_entry (nlmclnt,
                             &nlm_client_list, nlm_clients) {
                if (!strcmp(caller_name, nlmclnt->caller_name)) {
                        nlmclnt_found = 1;
                        break;
                }
        }

        if (!nlmclnt_found)
                goto ret;

        if (list_empty (&nlmclnt->fdes))
                goto ret;

        list_for_each_entry_safe (fde, tmp, &nlmclnt->fdes, fde_list) {
                fd_unref (fde->fd);
                list_del (&fde->fde_list);
                GF_FREE (fde);
        }

ret:
        UNLOCK (&nlm_client_list_lk);
        return 0;
}

void
nlm_search_and_delete (fd_t *fd, char *caller_name)
{
        nlm_fde_t *fde = NULL;
        nlm_client_t *nlmclnt = NULL;
        int nlmclnt_found = 0;
        int fde_found = 0;
        int transit_cnt = 0;

        LOCK (&nlm_client_list_lk);
        list_for_each_entry (nlmclnt,
                             &nlm_client_list, nlm_clients) {
                if (!strcmp(caller_name, nlmclnt->caller_name)) {
                        nlmclnt_found = 1;
                        break;
                }
        }

        if (!nlmclnt_found)
                goto ret;

        list_for_each_entry (fde, &nlmclnt->fdes, fde_list) {
                if (fde->fd == fd) {
                        fde_found = 1;
                        break;
                }
        }

        if (!fde_found)
                goto ret;
        transit_cnt = fde->transit_cnt;
        if (transit_cnt)
                goto ret;
        list_del (&fde->fde_list);

ret:
        UNLOCK (&nlm_client_list_lk);

        if (fde_found && !transit_cnt) {
                fd_unref (fde->fd);
                GF_FREE (fde);
        }
        return;
}

int
nlm_dec_transit_count (fd_t *fd, char *caller_name)
{
        nlm_fde_t *fde = NULL;
        nlm_client_t *nlmclnt = NULL;
        int nlmclnt_found = 0;
        int fde_found = 0;
        int transit_cnt = -1;

        LOCK (&nlm_client_list_lk);
        list_for_each_entry (nlmclnt,
                             &nlm_client_list, nlm_clients) {
                if (!strcmp(caller_name, nlmclnt->caller_name)) {
                        nlmclnt_found = 1;
                        break;
                }
        }

        if (!nlmclnt_found) {
                gf_msg (GF_NLM, GF_LOG_ERROR, 0, NFS_MSG_NLMCLNT_NOT_FOUND,
                        "nlmclnt not found");
                nlmclnt = NULL;
                goto ret;
        }

        list_for_each_entry (fde, &nlmclnt->fdes, fde_list) {
                if (fde->fd == fd) {
                        fde_found = 1;
                        break;
                }
        }

        if (fde_found) {
                transit_cnt = --fde->transit_cnt;
                goto ret;
        }
ret:

        UNLOCK (&nlm_client_list_lk);
        return transit_cnt;
}


nlm_client_t *
nlm_search_and_add (fd_t *fd, char *caller_name)
{
        nlm_fde_t *fde = NULL;
        nlm_client_t *nlmclnt = NULL;
        int nlmclnt_found = 0;
        int fde_found = 0;

        LOCK (&nlm_client_list_lk);
        list_for_each_entry (nlmclnt,
                             &nlm_client_list, nlm_clients) {
                if (!strcmp(caller_name, nlmclnt->caller_name)) {
                        nlmclnt_found = 1;
                        break;
                }
        }

        if (!nlmclnt_found) {
                gf_msg (GF_NLM, GF_LOG_ERROR, 0, NFS_MSG_NLMCLNT_NOT_FOUND,
                        "nlmclnt not found");
                nlmclnt = NULL;
                goto ret;
        }

        list_for_each_entry (fde, &nlmclnt->fdes, fde_list) {
                if (fde->fd == fd) {
                        fde_found = 1;
                        break;
                }
        }

        if (fde_found)
                goto ret;

        fde = GF_CALLOC (1, sizeof (*fde), gf_nfs_mt_nlm4_fde);

        fde->fd = fd_ref (fd);
        list_add (&fde->fde_list, &nlmclnt->fdes);
ret:
        if (nlmclnt_found && fde)
                fde->transit_cnt++;
        UNLOCK (&nlm_client_list_lk);
        return nlmclnt;
}

int
nlm4svc_lock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct gf_flock *flock,
                  dict_t *xdata)
{
        nlm4_stats                       stat        = nlm4_denied;
        int                              transit_cnt = -1;
        char                            *caller_name = NULL;
        nfs3_call_state_t               *cs          = NULL;
        pthread_t                        thr;

        cs = frame->local;
        caller_name = cs->args.nlm4_lockargs.alock.caller_name;
        transit_cnt = nlm_dec_transit_count (cs->fd, caller_name);

        if (op_ret == -1) {
                if (transit_cnt == 0)
                        nlm_search_and_delete (cs->fd, caller_name);
                stat = nlm4_errno_to_nlm4stat (op_errno);
                goto err;
        } else {
                stat = nlm4_granted;
                if (cs->monitor && !nlm_monitor (caller_name)) {
                        /* FIXME: handle nsm_monitor failure */
                        pthread_create (&thr, NULL, nsm_monitor, (void*)caller_name);
                }
        }

err:
        if (cs->args.nlm4_lockargs.block) {
                cs->frame = copy_frame (frame);
                frame->local = NULL;
                nlm4svc_send_granted (cs);
        } else {
                nlm4_generic_reply (cs->req, cs->args.nlm4_lockargs.cookie,
                                    stat);
                nfs3_call_state_wipe (cs);
        }
        return 0;
}

int
nlm4_lock_fd_resume (void *carg)
{
        nlm4_stats                      stat = nlm4_denied;
        int                             ret = -EFAULT;
        nfs_user_t                      nfu = {0, };
        nfs3_call_state_t               *cs = NULL;
        struct gf_flock                 flock = {0, };

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nlm4_check_fh_resolve_status (cs, stat, nlm4err);
        (void) nlm_search_and_add (cs->fd,
                                   cs->args.nlm4_lockargs.alock.caller_name);
        nfs_request_user_init (&nfu, cs->req);
        nlm4_lock_to_gf_flock (&flock, &cs->args.nlm4_lockargs.alock,
                               cs->args.nlm4_lockargs.exclusive);
        nlm_copy_lkowner (&nfu.lk_owner, &cs->args.nlm4_lockargs.alock.oh);
        if (cs->args.nlm4_lockargs.block) {
                nlm4_generic_reply (cs->req, cs->args.nlm4_lockargs.cookie,
                                    nlm4_blocked);
                ret = nfs_lk (cs->nfsx, cs->vol, &nfu, cs->fd, F_SETLKW,
                              &flock, nlm4svc_lock_cbk, cs);
                /* FIXME: handle error from nfs_lk() specially  by just
                 * cleaning up cs and unblock the client lock request.
                 */
                ret = 0;
        } else
                ret = nfs_lk (cs->nfsx, cs->vol, &nfu, cs->fd, F_SETLK,
                              &flock, nlm4svc_lock_cbk, cs);

nlm4err:
        if (ret < 0) {
                stat = nlm4_errno_to_nlm4stat (-ret);
                gf_msg (GF_NLM, GF_LOG_ERROR, stat, NFS_MSG_LOCK_FAIL,
                        "unable to call lk()");
                nlm4_generic_reply (cs->req, cs->args.nlm4_lockargs.cookie,
                                    stat);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}


int
nlm4_lock_resume (void *carg)
{
        nlm4_stats                      stat = nlm4_failed;
        int                             ret  = -1;
        nfs3_call_state_t              *cs   = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nlm4_check_fh_resolve_status (cs, stat, nlm4err);
        ret = nlm4_file_open_and_resume (cs, nlm4_lock_fd_resume);

nlm4err:
        if (ret < 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, -ret, NFS_MSG_OPEN_FAIL,
                        "unable to open and resume");
                stat = nlm4_errno_to_nlm4stat (-ret);
                nlm4_generic_reply (cs->req, cs->args.nlm4_lockargs.cookie,
                                    stat);
                nfs3_call_state_wipe (cs);
        }

        return ret;
}

int
nlm4svc_lock_common (rpcsvc_request_t *req, int mon)
{
        int                              ret      = RPCSVC_ACTOR_ERROR;
        nlm4_stats                       stat     = nlm4_failed;
        struct nfs3_fh                   fh       = {{0}, };
        xlator_t                        *vol      = NULL;
        nfs3_state_t                    *nfs3     = NULL;
        nfs3_call_state_t               *cs       = NULL;
        struct nfs_state                *nfs      = NULL;

        if (!req)
                return ret;

        nlm4_validate_nfs3_state (req, nfs3, stat, rpcerr, ret);
        nfs = nfs_state (nfs3->nfsx);
        nlm4_handle_call_state_init (nfs->nfs3state, cs, req,
                                     stat, rpcerr);

        nlm4_prep_nlm4_lockargs (&cs->args.nlm4_lockargs, &cs->lockfh,
                                 &cs->lkowner, cs->cookiebytes);
        if (xdr_to_nlm4_lockargs(req->msg[0], &cs->args.nlm4_lockargs) <= 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        fh = cs->lockfh;
        cs->monitor = mon;
        nlm4_validate_gluster_fh (&fh, stat, nlm4err);
        nlm4_map_fh_to_volume (cs->nfs3state, fh, req, vol, stat, nlm4err);

        if (nlm_grace_period && !cs->args.nlm4_lockargs.reclaim) {
                gf_msg (GF_NLM, GF_LOG_WARNING, 0, NFS_MSG_NLM_GRACE_PERIOD,
                        "NLM in grace period");
                stat = nlm4_denied_grace_period;
                nlm4_generic_reply (req, cs->args.nlm4_unlockargs.cookie, stat);
                nfs3_call_state_wipe (cs);
                return 0;
        }

        cs->vol = vol;
        cs->trans = rpcsvc_request_transport_ref(req);
        nlm4_volume_started_check (nfs3, vol, ret, rpcerr);

        ret = nlm_add_nlmclnt (cs->args.nlm4_lockargs.alock.caller_name);

        ret = nfs3_fh_resolve_and_resume (cs, &fh,
                                          NULL, nlm4_lock_resume);

nlm4err:
        if (ret < 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, -ret, NFS_MSG_RESOLVE_ERROR,
                        "unable to resolve and resume");
                nlm4_generic_reply (cs->req, cs->args.nlm4_lockargs.cookie,
                                    stat);
                nfs3_call_state_wipe (cs);
                return 0;
        }

rpcerr:
        if (ret < 0) {
                nfs3_call_state_wipe (cs);
        }

        return ret;
}

int
nlm4svc_lock (rpcsvc_request_t *req)
{
        return nlm4svc_lock_common (req, 1);
}

int
nlm4svc_nm_lock (rpcsvc_request_t *req)
{
        return nlm4svc_lock_common (req, 0);
}

int
nlm4svc_cancel_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct gf_flock *flock,
                    dict_t *xdata)
{
        nlm4_stats                      stat = nlm4_denied;
        nfs3_call_state_t              *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nlm4_errno_to_nlm4stat (op_errno);
                goto err;
        } else
                stat = nlm4_granted;

err:
        nlm4_generic_reply (cs->req, cs->args.nlm4_cancargs.cookie,
                            stat);
        nfs3_call_state_wipe (cs);
        return 0;
}

int
nlm4_cancel_fd_resume (void *carg)
{
        int                             ret = -EFAULT;
        nfs_user_t                      nfu = {0, };
        nfs3_call_state_t               *cs = NULL;
        struct gf_flock                 flock = {0, };

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nfs_request_user_init (&nfu, cs->req);
        nlm4_lock_to_gf_flock (&flock, &cs->args.nlm4_cancargs.alock,
                                cs->args.nlm4_cancargs.exclusive);
        nlm_copy_lkowner (&nfu.lk_owner, &cs->args.nlm4_cancargs.alock.oh);
        flock.l_type = F_UNLCK;
        ret = nfs_lk (cs->nfsx, cs->vol, &nfu, cs->fd, F_SETLK,
                      &flock, nlm4svc_cancel_cbk, cs);

        return ret;
}

int
nlm4_cancel_resume (void *carg)
{
        nlm4_stats                      stat = nlm4_failed;
        int                             ret = -EFAULT;
        nfs3_call_state_t               *cs = NULL;
        nlm_client_t                    *nlmclnt = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nlm4_check_fh_resolve_status (cs, stat, nlm4err);

        nlmclnt = nlm_get_uniq (cs->args.nlm4_cancargs.alock.caller_name);
        if (nlmclnt == NULL) {
                gf_msg (GF_NLM, GF_LOG_ERROR, ENOLCK, NFS_MSG_NO_MEMORY,
                        "nlm_get_uniq() returned NULL");
                goto nlm4err;
        }
        cs->fd = fd_lookup_uint64 (cs->resolvedloc.inode, (uint64_t)nlmclnt);
        if (cs->fd == NULL) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_FD_LOOKUP_NULL,
                        "fd_lookup_uint64 retrned NULL");
                goto nlm4err;
        }
        ret = nlm4_cancel_fd_resume (cs);

nlm4err:
        if (ret < 0) {
                gf_msg (GF_NLM, GF_LOG_WARNING, -ret, NFS_MSG_LOCK_FAIL,
                        "unable to unlock_fd_resume()");
                stat = nlm4_errno_to_nlm4stat (-ret);
                nlm4_generic_reply (cs->req, cs->args.nlm4_cancargs.cookie,
                                    stat);

                nfs3_call_state_wipe (cs);
        }
        /* clean up is taken care of */
        return 0;
}

int
nlm4svc_cancel (rpcsvc_request_t *req)
{
        xlator_t                        *vol = NULL;
        nlm4_stats                      stat = nlm4_failed;
        struct nfs_state               *nfs = NULL;
        nfs3_state_t                   *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;
        int                             ret = RPCSVC_ACTOR_ERROR;
        struct nfs3_fh                  fh = {{0}, };

        if (!req)
                return ret;

        nlm4_validate_nfs3_state (req, nfs3, stat, rpcerr, ret);
        nfs = nfs_state (nfs3->nfsx);
        nlm4_handle_call_state_init (nfs->nfs3state, cs, req,
                                     stat, rpcerr);

        nlm4_prep_nlm4_cancargs (&cs->args.nlm4_cancargs, &fh, &cs->lkowner,
                                 cs->cookiebytes);
        if (xdr_to_nlm4_cancelargs(req->msg[0], &cs->args.nlm4_cancargs) <= 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        nlm4_validate_gluster_fh (&fh, stat, nlm4err);
        nlm4_map_fh_to_volume (cs->nfs3state, fh, req, vol, stat, nlm4err);

        if (nlm_grace_period) {
                gf_msg (GF_NLM, GF_LOG_WARNING, 0, NFS_MSG_NLM_GRACE_PERIOD,
                        "NLM in grace period");
                stat = nlm4_denied_grace_period;
                nlm4_generic_reply (req, cs->args.nlm4_unlockargs.cookie, stat);
                nfs3_call_state_wipe (cs);
                return 0;
        }

        cs->vol = vol;
        nlm4_volume_started_check (nfs3, vol, ret, rpcerr);

        ret = nfs3_fh_resolve_and_resume (cs, &fh,
                                          NULL, nlm4_cancel_resume);

nlm4err:
        if (ret < 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, -ret, NFS_MSG_RESOLVE_ERROR,
                        "unable to resolve and resume");
                nlm4_generic_reply (cs->req, cs->args.nlm4_cancargs.cookie,
                                    stat);
                nfs3_call_state_wipe (cs);
                return 0;
        }

rpcerr:
        if (ret < 0) {
                nfs3_call_state_wipe (cs);
        }
        return ret;
}

int
nlm4svc_unlock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct gf_flock *flock,
                    dict_t *xdata)
{
        nlm4_stats                      stat = nlm4_denied;
        nfs3_call_state_t              *cs = NULL;

        cs = frame->local;
        if (op_ret == -1) {
                stat = nlm4_errno_to_nlm4stat (op_errno);
                goto err;
        } else {
                stat = nlm4_granted;
                if (flock->l_type == F_UNLCK)
                        nlm_search_and_delete (cs->fd,
                                               cs->args.nlm4_unlockargs.alock.caller_name);
        }

err:
        nlm4_generic_reply (cs->req, cs->args.nlm4_unlockargs.cookie, stat);
        nfs3_call_state_wipe (cs);
        return 0;
}

int
nlm4_unlock_fd_resume (void *carg)
{
        int                             ret = -EFAULT;
        nfs_user_t                      nfu = {0, };
        nfs3_call_state_t               *cs = NULL;
        struct gf_flock                 flock = {0, };

        if (!carg)
                return ret;
        cs = (nfs3_call_state_t *)carg;
        nfs_request_user_init (&nfu, cs->req);
        nlm4_lock_to_gf_flock (&flock, &cs->args.nlm4_unlockargs.alock, 0);
        nlm_copy_lkowner (&nfu.lk_owner, &cs->args.nlm4_unlockargs.alock.oh);
        flock.l_type = F_UNLCK;
        ret = nfs_lk (cs->nfsx, cs->vol, &nfu, cs->fd, F_SETLK,
                      &flock, nlm4svc_unlock_cbk, cs);

        return ret;
}

int
nlm4_unlock_resume (void *carg)
{
        nlm4_stats                      stat = nlm4_failed;
        int                             ret = -1;
        nfs3_call_state_t               *cs = NULL;
        nlm_client_t                    *nlmclnt = NULL;

        if (!carg)
                return ret;

        cs = (nfs3_call_state_t *)carg;
        nlm4_check_fh_resolve_status (cs, stat, nlm4err);

        nlmclnt = nlm_get_uniq (cs->args.nlm4_unlockargs.alock.caller_name);
        if (nlmclnt == NULL) {
                stat = nlm4_granted;
                gf_msg (GF_NLM, GF_LOG_WARNING, ENOLCK, NFS_MSG_NO_MEMORY,
                        "nlm_get_uniq() returned NULL");
                goto nlm4err;
        }
        cs->fd = fd_lookup_uint64 (cs->resolvedloc.inode, (uint64_t)nlmclnt);
        if (cs->fd == NULL) {
                stat = nlm4_granted;
                gf_msg (GF_NLM, GF_LOG_WARNING, 0, NFS_MSG_FD_LOOKUP_NULL,
                        "fd_lookup_uint64() returned NULL");
                goto nlm4err;
        }
        ret = nlm4_unlock_fd_resume (cs);

nlm4err:
        if (ret < 0) {
                gf_msg (GF_NLM, GF_LOG_WARNING, -ret, NFS_MSG_LOCK_FAIL,
                        "unable to unlock_fd_resume");
                stat = nlm4_errno_to_nlm4stat (-ret);
                nlm4_generic_reply (cs->req, cs->args.nlm4_unlockargs.cookie,
                                    stat);

                nfs3_call_state_wipe (cs);
        }
        /* we have already taken care of cleanup */
        return 0;
}

int
nlm4svc_unlock (rpcsvc_request_t *req)
{
        xlator_t                        *vol = NULL;
        nlm4_stats                      stat = nlm4_failed;
        struct nfs_state               *nfs = NULL;
        nfs3_state_t                   *nfs3 = NULL;
        nfs3_call_state_t               *cs = NULL;
        int                             ret = RPCSVC_ACTOR_ERROR;
        struct nfs3_fh                  fh = {{0}, };

        if (!req)
                return ret;

        nlm4_validate_nfs3_state (req, nfs3, stat, rpcerr, ret);
        nfs = nfs_state (nfs3->nfsx);
        nlm4_handle_call_state_init (nfs->nfs3state, cs, req,
                                     stat, rpcerr);

        nlm4_prep_nlm4_unlockargs (&cs->args.nlm4_unlockargs, &fh, &cs->lkowner,
                                   cs->cookiebytes);
        if (xdr_to_nlm4_unlockargs(req->msg[0], &cs->args.nlm4_unlockargs) <= 0)
        {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        nlm4_validate_gluster_fh (&fh, stat, nlm4err);
        nlm4_map_fh_to_volume (cs->nfs3state, fh, req, vol, stat, nlm4err);

        if (nlm_grace_period) {
                gf_msg (GF_NLM, GF_LOG_WARNING, 0, NFS_MSG_NLM_GRACE_PERIOD,
                        "NLM in grace period");
                stat = nlm4_denied_grace_period;
                nlm4_generic_reply (req, cs->args.nlm4_unlockargs.cookie, stat);
                nfs3_call_state_wipe (cs);
                return 0;
        }

        cs->vol = vol;
        /* FIXME: check if trans is being used at all for unlock */
        cs->trans = rpcsvc_request_transport_ref(req);
        nlm4_volume_started_check (nfs3, vol, ret, rpcerr);

        ret = nfs3_fh_resolve_and_resume (cs, &fh,
                                          NULL, nlm4_unlock_resume);

nlm4err:
        if (ret < 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, -ret, NFS_MSG_RESOLVE_ERROR,
                        "unable to resolve and resume");
                nlm4_generic_reply (req, cs->args.nlm4_unlockargs.cookie, stat);
                nfs3_call_state_wipe (cs);
                return 0;
        }

rpcerr:
        if (ret < 0) {
                nfs3_call_state_wipe (cs);
        }
        return ret;
}

int
nlm4_share_reply (nfs3_call_state_t *cs, nlm4_stats stat)
{
        nlm4_shareres  res = {{0}, 0, 0};

        if (!cs)
                return -1;

        res.cookie = cs->args.nlm4_shareargs.cookie;
        res.stat = stat;
        res.sequence = 0;

        nlm4svc_submit_reply (cs->req, (void *)&res,
                              (nlm4_serializer)xdr_serialize_nlm4_shareres);
        return 0;
}

nlm_share_t *
nlm4_share_new ()
{
        nlm_share_t      *share     = NULL;

        share = GF_CALLOC (1, sizeof (nlm_share_t),
                           gf_nfs_mt_nlm4_share);
        if (!share)
                goto out;

        INIT_LIST_HEAD (&share->client_list);
        INIT_LIST_HEAD (&share->inode_list);
 out:
        return share;
}

int
nlm4_add_share_to_inode (nlm_share_t *share)
{
        int                    ret     = -1;
        uint64_t               ctx     = 0;
        struct list_head      *head    = NULL;
        xlator_t              *this    = NULL;
        inode_t               *inode   = NULL;
        struct nfs_inode_ctx  *ictx    = NULL;
        struct nfs_state      *priv    = NULL;

        this = THIS;
        priv = this->private;
        inode = share->inode;
        ret = inode_ctx_get (inode, this, &ctx);

        if (ret == -1) {
                ictx = GF_CALLOC (1, sizeof (struct nfs_inode_ctx),
                                  gf_nfs_mt_inode_ctx);
                if (!ictx ) {
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                NFS_MSG_NO_MEMORY,
                                "could not allocate nfs inode ctx");
                        ret = -1;
                        goto out;
                }
                ictx->generation = priv->generation;

                head = &ictx->shares;
                INIT_LIST_HEAD (head);

                ret = inode_ctx_put (inode, this, (uint64_t)ictx);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                NFS_MSG_SHARE_LIST_STORE_FAIL,
                                "could not store share list");
                        goto out;
                }
        }
        else {
                ictx = (struct nfs_inode_ctx *)ctx;
                head = &ictx->shares;
        }

        list_add (&share->inode_list, head);

 out:
        if (ret && head)
                GF_FREE (head);

        return ret;
}

int
nlm4_approve_share_reservation (nfs3_call_state_t *cs)
{
        int                       ret               = -1;
        uint64_t                  ctx               = 0;
        fsh_mode                  req_mode          = 0;
        fsh_access                req_access        = 0;
        inode_t                  *inode             = NULL;
        nlm_share_t              *share             = NULL;
        struct list_head         *head              = NULL;
        struct nfs_inode_ctx     *ictx              = NULL;

        if (!cs)
                goto out;

        inode = cs->resolvedloc.inode;

        ret = inode_ctx_get (inode, THIS, &ctx);
        if (ret) {
                ret = 0;
                goto out;
        }
        ictx = (struct nfs_inode_ctx *)ctx;

        head = &ictx->shares;
        if (!head || list_empty (head))
                goto out;

        req_mode = cs->args.nlm4_shareargs.share.mode;
        req_access = cs->args.nlm4_shareargs.share.access;

        list_for_each_entry (share, head, inode_list) {
                ret = (((req_mode & share->access) == 0) &&
                       ((req_access & share->mode) == 0));
                if (!ret) {
                        ret = -1;
                        goto out;
                }
        }
        ret = 0;

 out:
         return ret;
}

int
nlm4_create_share_reservation (nfs3_call_state_t *cs)
{
        int                       ret        = -1;
        nlm_share_t              *share      = NULL;
        nlm_client_t             *client     = NULL;
        inode_t                  *inode      = NULL;

        LOCK (&nlm_client_list_lk);

        inode = inode_ref (cs->resolvedloc.inode);
        if (!inode) {
                gf_msg (GF_NLM, GF_LOG_ERROR, 0, NFS_MSG_INODE_NOT_FOUND,
                        "inode not found");
                goto out;
        }

        client = __nlm_get_uniq (cs->args.nlm4_shareargs.share.caller_name);
        if (!client) {
                /* DO NOT add client. the client is supposed
                   to be here, since nlm4svc_share adds it */
                gf_msg (GF_NLM, GF_LOG_ERROR, 0, NFS_MSG_CLIENT_NOT_FOUND,
                        "client not found");
                goto out;
        }

        ret = nlm4_approve_share_reservation (cs);
        if (ret)
                goto out;

        share = nlm4_share_new ();
        if (!share) {
                ret = -1;
                goto out;
        }

        share->inode  = inode;
        share->mode   = cs->args.nlm4_shareargs.share.mode;
        share->access = cs->args.nlm4_shareargs.share.access;
        nlm_copy_lkowner (&share->lkowner,
                          &cs->args.nlm4_shareargs.share.oh);

        ret = nlm4_add_share_to_inode (share);
        if (ret)
                goto out;

        list_add (&share->client_list, &client->shares);

 out:
        if (ret && inode) {
                inode_unref (inode);
                GF_FREE (share);
        }

        UNLOCK (&nlm_client_list_lk);
        return ret;
}

/*
  SHARE and UNSHARE calls DO NOT perform STACK_WIND,
  the (non-monitored) share reservations are maintained
  at *nfs xlator level only*, in memory
*/
int
nlm4_share_resume (void *call_state)
{
        int                             ret             = -1;
        nlm4_stats                      stat            = nlm4_failed;
        nfs3_call_state_t              *cs              = NULL;

        if (!call_state)
                return ret;

        cs = (nfs3_call_state_t *)call_state;
        nlm4_check_fh_resolve_status (cs, stat, out);

        ret = nlm4_create_share_reservation (cs);
        if (!ret)
                stat = nlm4_granted;

 out:
        nlm4_share_reply (cs, stat);
        nfs3_call_state_wipe (cs);
        return 0;
}

int
nlm4svc_share (rpcsvc_request_t *req)
{
        nlm4_stats                    stat      = nlm4_failed;
        xlator_t                     *vol       = NULL;
        nfs3_state_t                 *nfs3      = NULL;
        nfs3_call_state_t            *cs        = NULL;
        struct nfs_state             *nfs       = NULL;
        struct nfs3_fh                fh        = {{0}, };
        int                           ret       = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;

        nlm4_validate_nfs3_state (req, nfs3, stat, rpcerr, ret);
        nfs = nfs_state (nfs3->nfsx);
        nlm4_handle_call_state_init (nfs->nfs3state, cs, req,
                                     stat, rpcerr);

        nlm4_prep_shareargs (&cs->args.nlm4_shareargs, &cs->lockfh,
                             &cs->lkowner, cs->cookiebytes);

        if (xdr_to_nlm4_shareargs (req->msg[0],
                                   &cs->args.nlm4_shareargs) <= 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding SHARE args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        fh = cs->lockfh;
        nlm4_validate_gluster_fh (&fh, stat, nlm4err);
        nlm4_map_fh_to_volume (cs->nfs3state, fh, req,
                               vol, stat, nlm4err);

        if (nlm_grace_period && !cs->args.nlm4_shareargs.reclaim) {
                gf_msg_debug (GF_NLM, 0, "NLM in grace period");
                stat = nlm4_denied_grace_period;
                nlm4_share_reply (cs, stat);
                nfs3_call_state_wipe (cs);
                return 0;
        }

        cs->vol = vol;
        cs->trans = rpcsvc_request_transport_ref(req);
        nlm4_volume_started_check (nfs3, vol, ret, rpcerr);

        ret = nlm_add_nlmclnt (cs->args.nlm4_shareargs.share.caller_name);

        ret = nfs3_fh_resolve_and_resume (cs, &fh, NULL, nlm4_share_resume);

 nlm4err:
        if (ret < 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, -ret, NFS_MSG_SHARE_CALL_FAIL,
                        "SHARE call failed");
                nlm4_share_reply (cs, stat);
                nfs3_call_state_wipe (cs);
                return 0;
        }

 rpcerr:
        if (ret < 0)
                nfs3_call_state_wipe (cs);

        return ret;
}

int
nlm4_remove_share_reservation (nfs3_call_state_t *cs)
{
        int                      ret        = -1;
        uint64_t                 ctx        = 0;
        fsh_mode                 req_mode   = 0;
        fsh_access               req_access = 0;
        nlm_share_t             *share      = NULL;
        nlm_share_t             *tmp        = NULL;
        nlm_client_t            *client     = NULL;
        char                    *caller     = NULL;
        inode_t                 *inode      = NULL;
        xlator_t                *this       = NULL;
        struct list_head        *head       = NULL;
        nlm4_shareargs          *args       = NULL;
        struct nfs_inode_ctx    *ictx       = NULL;

        LOCK (&nlm_client_list_lk);

        args = &cs->args.nlm4_shareargs;
        caller = args->share.caller_name;

        client = __nlm_get_uniq (caller);
        if (!client) {
                gf_msg (GF_NLM, GF_LOG_ERROR, 0, NFS_MSG_CLIENT_NOT_FOUND,
                        "client not found: %s", caller);
                goto out;
        }

        inode = cs->resolvedloc.inode;
        if (!inode) {
                gf_msg (GF_NLM, GF_LOG_ERROR, 0, NFS_MSG_INODE_NOT_FOUND,
                        "inode not found: client: %s", caller);
                goto out;
        }

        this = THIS;
        ret = inode_ctx_get (inode, this, &ctx);
        if (ret) {
                gf_msg (GF_NLM, GF_LOG_ERROR, 0,
                        NFS_MSG_INODE_SHARES_NOT_FOUND,
                        "no shares found for inode:"
                        "gfid: %s; client: %s",
                        inode->gfid, caller);
                goto out;
        }
        ictx = (struct nfs_inode_ctx *)ctx;

        head = &ictx->shares;
        if (list_empty (head)) {
                ret = -1;
                goto out;
        }

        ret = 0;
        req_mode = args->share.mode;
        req_access = args->share.access;

        list_for_each_entry_safe (share, tmp, head, inode_list) {
                ret = ((req_mode == share->mode) &&
                       (req_access == share->access) &&
                       nlm_is_oh_same_lkowner (&share->lkowner, &args->share.oh));
                if (ret) {
                        list_del (&share->client_list);
                        list_del (&share->inode_list);
                        inode_unref (share->inode);
                        GF_FREE (share);
                        break;
                }
        }

        ret = 0;
 out:
        UNLOCK (&nlm_client_list_lk);
        return ret;

}

int
nlm4_unshare_resume (void *call_state)
{
        int                ret        = -1;
        nlm4_stats         stat       = nlm4_failed;
        nfs3_call_state_t *cs         = NULL;

        if (!call_state)
                return ret;

        cs = (nfs3_call_state_t *)call_state;

        nlm4_check_fh_resolve_status (cs, stat, out);
        ret = nlm4_remove_share_reservation (cs);
        if (!ret)
                stat = nlm4_granted;

 out:
        nlm4_share_reply (cs, stat);
        nfs3_call_state_wipe (cs);
        return 0;
}

int
nlm4svc_unshare (rpcsvc_request_t *req)
{
        nlm4_stats                    stat      = nlm4_failed;
        xlator_t                     *vol       = NULL;
        nfs3_state_t                 *nfs3      = NULL;
        nfs3_call_state_t            *cs        = NULL;
        struct nfs_state             *nfs       = NULL;
        struct nfs3_fh                fh        = {{0}, };
        int                           ret       = RPCSVC_ACTOR_ERROR;

        if (!req)
                return ret;

        nlm4_validate_nfs3_state (req, nfs3, stat, rpcerr, ret);
        nfs = nfs_state (nfs3->nfsx);
        nlm4_handle_call_state_init (nfs->nfs3state, cs, req,
                                     stat, rpcerr);

        nlm4_prep_shareargs (&cs->args.nlm4_shareargs, &cs->lockfh,
                             &cs->lkowner, cs->cookiebytes);

        if (xdr_to_nlm4_shareargs (req->msg[0],
                                   &cs->args.nlm4_shareargs) <= 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding UNSHARE args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        fh = cs->lockfh;
        nlm4_validate_gluster_fh (&fh, stat, nlm4err);
        nlm4_map_fh_to_volume (cs->nfs3state, fh, req,
                               vol, stat, nlm4err);

        if (nlm_grace_period && !cs->args.nlm4_shareargs.reclaim) {
                gf_msg_debug (GF_NLM, 0, "NLM in grace period");
                stat = nlm4_denied_grace_period;
                nlm4_share_reply (cs, stat);
                nfs3_call_state_wipe (cs);
                return 0;
        }

        cs->vol = vol;
        cs->trans = rpcsvc_request_transport_ref(req);
        nlm4_volume_started_check (nfs3, vol, ret, rpcerr);

        ret = nfs3_fh_resolve_and_resume (cs, &fh, NULL,
                                          nlm4_unshare_resume);

 nlm4err:
        if (ret < 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, -ret, NFS_MSG_UNSHARE_CALL_FAIL,
                        "UNSHARE call failed");
                nlm4_share_reply (cs, stat);
                ret = 0;
                return 0;
        }

 rpcerr:
        if (ret < 0)
                nfs3_call_state_wipe (cs);

        return ret;
}

int
nlm4_free_all_shares (char *caller_name)
{
        nlm_share_t             *share      = NULL;
        nlm_share_t             *tmp        = NULL;
        nlm_client_t            *client     = NULL;

        LOCK (&nlm_client_list_lk);

        client = __nlm_get_uniq (caller_name);
        if (!client) {
                gf_msg_debug (GF_NLM, 0, "client not found: %s", caller_name);
                goto out;
        }

        list_for_each_entry_safe (share, tmp, &client->shares, client_list) {
                list_del (&share->inode_list);
                list_del (&share->client_list);
                inode_unref (share->inode);
                GF_FREE (share);
        }
 out:
        UNLOCK (&nlm_client_list_lk);
        return 0;
}

int
nlm4svc_free_all (rpcsvc_request_t *req)
{
        int                           ret       = RPCSVC_ACTOR_ERROR;
        nlm4_stats                    stat      = nlm4_failed;
        nfs3_state_t                 *nfs3      = NULL;
        nfs3_call_state_t            *cs        = NULL;
        struct nfs_state             *nfs       = NULL;

        nlm4_validate_nfs3_state (req, nfs3, stat, err, ret);
        nfs = nfs_state (nfs3->nfsx);
        nlm4_handle_call_state_init (nfs->nfs3state, cs,
                                     req, stat, err);

        nlm4_prep_freeallargs (&cs->args.nlm4_freeallargs,
                               &cs->lkowner);

        if (xdr_to_nlm4_freeallargs (req->msg[0],
                                     &cs->args.nlm4_freeallargs) <= 0) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_ARGS_DECODE_ERROR,
                        "Error decoding FREE_ALL args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto err;
        }

        ret = nlm4_free_all_shares (cs->args.nlm4_freeallargs.name);
        if (ret)
                goto err;

        ret = nlm_cleanup_fds (cs->args.nlm4_freeallargs.name);
        if (ret)
                goto err;

 err:
        nfs3_call_state_wipe (cs);
        if (ret)
                gf_msg_debug (GF_NLM, 0, "error in free all; stat: %d", stat);
        return ret;

}

void
nlm4svc_sm_notify (struct nlm_sm_status *status)
{
        gf_msg (GF_NLM, GF_LOG_INFO, 0, NFS_MSG_SM_NOTIFY, "sm_notify: "
                "%s, state: %d", status->mon_name, status->state);
        nlm_cleanup_fds (status->mon_name);
}

rpcsvc_actor_t  nlm4svc_actors[NLM4_PROC_COUNT] = {
        /* 0 */
        {"NULL",       NLM4_NULL,         nlm4svc_null,      NULL, 0, DRC_IDEMPOTENT},
        {"TEST",       NLM4_TEST,         nlm4svc_test,      NULL, 0, DRC_IDEMPOTENT},
        {"LOCK",       NLM4_LOCK,         nlm4svc_lock,      NULL, 0, DRC_NON_IDEMPOTENT},
        {"CANCEL",     NLM4_CANCEL,       nlm4svc_cancel,    NULL, 0, DRC_NON_IDEMPOTENT},
        {"UNLOCK",     NLM4_UNLOCK,       nlm4svc_unlock,    NULL, 0, DRC_NON_IDEMPOTENT},
        /* 5 */
        {"GRANTED",    NLM4_GRANTED,      NULL,              NULL, 0, DRC_NA},
        {"TEST",       NLM4_TEST_MSG,     NULL,              NULL, 0, DRC_NA},
        {"LOCK",       NLM4_LOCK_MSG,     NULL,              NULL, 0, DRC_NA},
        {"CANCEL",     NLM4_CANCEL_MSG,   NULL,              NULL, 0, DRC_NA},
        {"UNLOCK",     NLM4_UNLOCK_MSG,   NULL,              NULL, 0, DRC_NA},
        /* 10 */
        {"GRANTED",    NLM4_GRANTED_MSG,  NULL,              NULL, 0, DRC_NA},
        {"TEST",       NLM4_TEST_RES,     NULL,              NULL, 0, DRC_NA},
        {"LOCK",       NLM4_LOCK_RES,     NULL,              NULL, 0, DRC_NA},
        {"CANCEL",     NLM4_CANCEL_RES,   NULL,              NULL, 0, DRC_NA},
        {"UNLOCK",     NLM4_UNLOCK_RES,   NULL,              NULL, 0, DRC_NA},
        /* 15 ; procedures 17,18,19 are not defined by nlm */
        {"GRANTED",    NLM4_GRANTED_RES,  NULL,              NULL, 0, DRC_NA},
        {"SM_NOTIFY",  NLM4_SM_NOTIFY,    NULL,              NULL, 0, DRC_NA},
        {"SEVENTEEN",  NLM4_SEVENTEEN,    NULL,              NULL, 0, DRC_NA},
        {"EIGHTEEN",   NLM4_EIGHTEEN,     NULL,              NULL, 0, DRC_NA},
        {"NINETEEN",   NLM4_NINETEEN,     NULL,              NULL, 0, DRC_NA},
        /* 20 */
        {"SHARE",      NLM4_SHARE,        nlm4svc_share,     NULL, 0, DRC_NON_IDEMPOTENT},
        {"UNSHARE",    NLM4_UNSHARE,      nlm4svc_unshare,   NULL, 0, DRC_NON_IDEMPOTENT},
        {"NM_LOCK",    NLM4_NM_LOCK,      nlm4svc_nm_lock,   NULL, 0, DRC_NON_IDEMPOTENT},
        {"FREE_ALL",   NLM4_FREE_ALL,     nlm4svc_free_all,  NULL, 0, DRC_IDEMPOTENT},
};

rpcsvc_program_t        nlm4prog = {
        .progname       = "NLM4",
        .prognum        = NLM_PROGRAM,
        .progver        = NLM_V4,
        .progport       = GF_NLM4_PORT,
        .actors         = nlm4svc_actors,
        .numactors      = NLM4_PROC_COUNT,
        .min_auth       = AUTH_NULL,
};


int
nlm4_init_state (xlator_t *nfsx)
{
        return 0;
}

extern void *nsm_thread (void *argv);

void nlm_grace_period_over(void *arg)
{
        nlm_grace_period = 0;
}

rpcsvc_program_t *
nlm4svc_init(xlator_t *nfsx)
{
        struct nfs3_state *ns = NULL;
        struct nfs_state *nfs = NULL;
        dict_t *options = NULL;
        int ret = -1;
        char *portstr = NULL;
        pthread_t thr;
        struct timespec timeout = {0,};
        FILE   *pidfile = NULL;
        pid_t   pid     = -1;
        static gf_boolean_t nlm4_inited = _gf_false;

        /* Already inited */
        if (nlm4_inited)
                return &nlm4prog;

        nfs = (struct nfs_state*)nfsx->private;

        ns = nfs->nfs3state;
        if (!ns) {
                gf_msg (GF_NLM, GF_LOG_ERROR, EINVAL, NFS_MSG_NLM_INIT_FAIL,
                        "NLM4 init failed");
                goto err;
        }
        nlm4prog.private = ns;

        options = dict_new ();

        ret = gf_asprintf (&portstr, "%d", GF_NLM4_PORT);
        if (ret == -1)
                goto err;

        ret = dict_set_dynstr (options, "transport.socket.listen-port",
                               portstr);
        if (ret == -1)
                goto err;
        ret = dict_set_str (options, "transport-type", "socket");
        if (ret == -1) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno,
                        NFS_MSG_DICT_SET_FAILED, "dict_set_str error");
                goto err;
        }

        if (nfs->allow_insecure) {
                ret = dict_set_str (options, "rpc-auth-allow-insecure", "on");
                if (ret == -1) {
                        gf_msg (GF_NLM, GF_LOG_ERROR, errno,
                                NFS_MSG_DICT_SET_FAILED, "dict_set_str error");
                        goto err;
                }
                ret = dict_set_str (options, "rpc-auth.ports.insecure", "on");
                if (ret == -1) {
                        gf_msg (GF_NLM, GF_LOG_ERROR, errno,
                                NFS_MSG_DICT_SET_FAILED, "dict_set_str error");
                        goto err;
                }
        }

        ret = dict_set_str (options, "transport.address-family", "inet");
        if (ret == -1) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_DICT_SET_FAILED,
                        "dict_set_str error");
                goto err;
        }

        ret = rpcsvc_create_listeners (nfs->rpcsvc, options, "NLM");
        if (ret == -1) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno,
                        NFS_MSG_LISTENERS_CREATE_FAIL,
                        "Unable to create listeners");
                dict_unref (options);
                goto err;
        }
        INIT_LIST_HEAD(&nlm_client_list);
        LOCK_INIT (&nlm_client_list_lk);

        /* unlink sm-notify.pid so that when we restart rpc.statd/sm-notify
         * it thinks that the machine has restarted and sends NOTIFY to clients.
         */

        /* TODO:
           notify/rpc.statd is done differently on OSX

           On OSX rpc.statd is controlled by rpc.lockd and are part for launchd
           (unified service management framework)

           A runcmd() should be invoking "launchctl start com.apple.lockd"
           instead. This is still a theory but we need to thoroughly test it
           out. Until then NLM support is non-existent on OSX.
        */
        ret = sys_unlink (GF_SM_NOTIFY_PIDFILE);
        if (ret == -1 && errno != ENOENT) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_UNLINK_ERROR,
                        "unable to unlink %s: %d",
                        GF_SM_NOTIFY_PIDFILE, errno);
                goto err;
        }
        /* temporary work around to restart statd, not distro/OS independent.
         * Need to figure out a more graceful way
         * killall will cause problems on solaris.
         */

        char *pid_file = GF_RPC_STATD_PIDFILE;
        if (nfs->rpc_statd_pid_file)
                pid_file = nfs->rpc_statd_pid_file;
        pidfile = fopen (pid_file, "r");
        if (pidfile) {
                ret = fscanf (pidfile, "%d", &pid);
                if (ret <= 0) {
                        gf_msg (GF_NLM, GF_LOG_WARNING, errno,
                                NFS_MSG_GET_PID_FAIL, "unable to get pid of "
                                "rpc.statd from %s ", GF_RPC_STATD_PIDFILE);
                        ret = runcmd (KILLALL_CMD, "-9", "rpc.statd", NULL);
                } else
                        kill (pid, SIGKILL);

                fclose (pidfile);
        } else {
                gf_msg (GF_NLM, GF_LOG_WARNING, errno, NFS_MSG_OPEN_FAIL,
                        "opening %s of rpc.statd failed (%s)",
                        pid_file, strerror (errno));
                /* if ret == -1, do nothing - case either statd was not
                 * running or was running in valgrind mode
                 */
                ret = runcmd (KILLALL_CMD, "-9", "rpc.statd", NULL);
        }

        ret = sys_unlink (GF_RPC_STATD_PIDFILE);
        if (ret == -1 && errno != ENOENT) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_UNLINK_ERROR,
                        "unable to unlink %s", pid_file);
                goto err;
        }

        ret = runcmd (nfs->rpc_statd, NULL);
        if (ret == -1) {
                gf_msg (GF_NLM, GF_LOG_ERROR, errno, NFS_MSG_START_ERROR,
                        "unable to start %s", nfs->rpc_statd);
                goto err;
        }


        pthread_create (&thr, NULL, nsm_thread, (void*)NULL);

        timeout.tv_sec = nlm_grace_period;
        timeout.tv_nsec = 0;

        gf_timer_call_after (nfsx->ctx, timeout, nlm_grace_period_over, NULL);
        nlm4_inited = _gf_true;
        return &nlm4prog;
err:
        return NULL;
}

int32_t
nlm_priv (xlator_t *this)
{
        int32_t                  ret                       = -1;
        uint32_t                 client_count              = 0;
        uint64_t                 file_count                = 0;
        nlm_client_t            *client                    = NULL;
        nlm_fde_t               *fde                       = NULL;
        char                     key[GF_DUMP_MAX_BUF_LEN]  = {0};
        char                     gfid_str[64]              = {0};

        gf_proc_dump_add_section("nfs.nlm");

        if (TRY_LOCK (&nlm_client_list_lk))
                goto out;

        list_for_each_entry (client, &nlm_client_list, nlm_clients) {

                gf_proc_dump_build_key (key, "client", "%d.hostname", client_count);
                gf_proc_dump_write (key, "%s\n", client->caller_name);

                file_count = 0;
                list_for_each_entry (fde, &client->fdes, fde_list) {
                        gf_proc_dump_build_key (key, "file", "%ld.gfid", file_count);
                        memset (gfid_str, 0, 64);
                        uuid_utoa_r (fde->fd->inode->gfid, gfid_str);
                        gf_proc_dump_write (key, "%s", gfid_str);
                        file_count++;
                }

                gf_proc_dump_build_key (key, "client", "files-locked");
                gf_proc_dump_write (key, "%ld\n", file_count);
                client_count++;
        }

        gf_proc_dump_build_key (key, "nlm", "client-count");
        gf_proc_dump_write (key, "%d", client_count);
        ret = 0;
        UNLOCK (&nlm_client_list_lk);

 out:
        if (ret) {
                gf_proc_dump_build_key (key, "nlm", "statedump_error");
                gf_proc_dump_write (key, "Unable to dump nlm state because "
                                    "nlm_client_list_lk lock couldn't be acquired");
        }

        return ret;
}
