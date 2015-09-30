/*
  Copyright (c) 2012 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "xdr-nfs3.h"
#include "logging.h"
#include "mem-pool.h"
#include "nfs-mem-types.h"
#include "nfs-messages.h"
#include "mount3.h"
#include <stdio.h>
#include <stdlib.h>
#include <rpc/pmap_clnt.h>
#include <string.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>


extern struct nfs3_fh*
nfs3_rootfh (struct svc_req *req, xlator_t *nfsx, char *dp, char *expname);

extern mountres3
mnt3svc_set_mountres3 (mountstat3 stat, struct nfs3_fh *fh,
                       int *authflavor, u_int aflen);
extern int
mount3udp_add_mountlist (xlator_t *nfsx, char *host, char *expname);

extern int
mount3udp_delete_mountlist (xlator_t *nfsx, char *host, char *expname);

extern mountstat3
mnt3svc_errno_to_mnterr (int32_t errnum);


/* only this thread will use this, no locking needed */
char mnthost[INET_ADDRSTRLEN+1];

#define MNT3UDP_AUTH_LEN 1 /* Only AUTH_UNIX for now */

mountres3 *
mountudpproc3_mnt_3_svc(dirpath **dpp, struct svc_req *req)
{
        struct mountres3        *res = NULL;
        int                     *autharr = NULL;
        struct nfs3_fh          *fh = NULL;
        char                    *mpath = NULL;
        xlator_t                *nfsx = THIS;
        char                    expname[PATH_MAX] = {0, };
        mountstat3              stat = MNT3ERR_SERVERFAULT;

        errno = 0; /* RESET errno */

        mpath = (char *)*dpp;
        while (*mpath == '/')
                mpath++;

        res = GF_CALLOC (1, sizeof(*res), gf_nfs_mt_mountres3);
        if (res == NULL) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Unable to allocate memory");
                goto err;
        }
        autharr = GF_CALLOC (MNT3UDP_AUTH_LEN, sizeof(int), gf_nfs_mt_int);
        if (autharr == NULL) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Unable to allocate memory");
                goto err;
        }

        autharr[0] = AUTH_UNIX;

        fh = nfs3_rootfh (req, nfsx, mpath, (char *)expname);

        /* FAILURE: No FH */
        if (fh == NULL) {
                gf_msg (GF_MNT, GF_LOG_ERROR, errno, NFS_MSG_GET_FH_FAIL,
                        "Unable to get fh for %s", mpath);
                if (errno)
                        stat = mnt3svc_errno_to_mnterr (errno);
                *res = mnt3svc_set_mountres3 (stat, NULL /* fh */,
                                              autharr, MNT3UDP_AUTH_LEN);
                return res;
        }

        /* SUCCESS */
        stat = MNT3_OK;
        *res = mnt3svc_set_mountres3 (stat, fh, autharr, MNT3UDP_AUTH_LEN);
        (void) mount3udp_add_mountlist (nfsx, mnthost, (char *) expname);
        return res;

 err:
        GF_FREE (fh);
        GF_FREE (res);
        GF_FREE (autharr);
        return NULL;
}

mountstat3 *
mountudpproc3_umnt_3_svc(dirpath **dp, struct svc_req *req)
{
        mountstat3 *stat = NULL;
        char       *mpath = (char *) *dp;
        xlator_t   *nfsx = THIS;

        stat = GF_CALLOC (1, sizeof(mountstat3), gf_nfs_mt_mountstat3);
        if (stat == NULL) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Unable to allocate memory");
                return NULL;
        }
        *stat = MNT3_OK;
        (void) mount3udp_delete_mountlist (nfsx, mnthost, mpath);
        return stat;
}

static void
mountudp_program_3(struct svc_req *rqstp, register SVCXPRT *transp)
{
        union {
                dirpath mountudpproc3_mnt_3_arg;
        } argument;
        char                    *result = NULL;
        xdrproc_t               _xdr_argument = NULL, _xdr_result = NULL;
        char *(*local)(char *, struct svc_req *) = NULL;
        mountres3               *res = NULL;
        struct sockaddr_in      *sin = NULL;

        sin = svc_getcaller (transp);
        inet_ntop (AF_INET, &sin->sin_addr, mnthost, INET_ADDRSTRLEN+1);

        switch (rqstp->rq_proc) {
        case NULLPROC:
                (void) svc_sendreply (transp, (xdrproc_t) xdr_void,
                                      (char *)NULL);
                return;

        case MOUNT3_MNT:
                _xdr_argument = (xdrproc_t) xdr_dirpath;
                _xdr_result = (xdrproc_t) xdr_mountres3;
                local = (char *(*)(char *,
                                   struct svc_req *)) mountudpproc3_mnt_3_svc;
                break;

        case MOUNT3_UMNT:
                _xdr_argument = (xdrproc_t) xdr_dirpath;
                _xdr_result = (xdrproc_t) xdr_mountstat3;
                local = (char *(*)(char *,
                                   struct svc_req *)) mountudpproc3_umnt_3_svc;
                break;

        default:
                svcerr_noproc (transp);
                return;
        }
        memset ((char *)&argument, 0, sizeof (argument));
        if (!svc_getargs (transp, (xdrproc_t) _xdr_argument,
                          (caddr_t) &argument)) {
                svcerr_decode (transp);
                return;
        }
        result = (*local)((char *)&argument, rqstp);
        if (result == NULL) {
                gf_msg_debug (GF_MNT, 0, "PROC returned error");
                svcerr_systemerr (transp);
        }
        if (result != NULL && !svc_sendreply(transp, (xdrproc_t) _xdr_result,
                                             result)) {
                gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_SVC_ERROR,
                        "svc_sendreply returned error");
                svcerr_systemerr (transp);
        }
        if (!svc_freeargs (transp, (xdrproc_t) _xdr_argument,
                           (caddr_t) &argument)) {
                gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_ARG_FREE_FAIL,
                        "Unable to free arguments");
        }
        if (result == NULL)
                return;
        /* free the result */
        switch (rqstp->rq_proc) {
        case MOUNT3_MNT:
                res = (mountres3 *) result;
                GF_FREE (res->mountres3_u.mountinfo.fhandle.fhandle3_val);
                GF_FREE (res->mountres3_u.mountinfo.auth_flavors.auth_flavors_val);
                GF_FREE (res);
                break;

        case MOUNT3_UMNT:
                GF_FREE (result);
                break;
        }
        return;
}

void *
mount3udp_thread (void *argv)
{
        xlator_t         *nfsx   = argv;
        register SVCXPRT *transp = NULL;

        GF_ASSERT (nfsx);

        if (glusterfs_this_set(nfsx)) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_XLATOR_SET_FAIL,
                        "Failed to set xlator, nfs.mount-udp will not work");
                return NULL;
        }

        transp = svcudp_create(RPC_ANYSOCK);
        if (transp == NULL) {
                gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_SVC_ERROR,
                        "svcudp_create error");
                return NULL;
        }
        if (!svc_register(transp, MOUNT_PROGRAM, MOUNT_V3,
                          mountudp_program_3, IPPROTO_UDP)) {
                gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_SVC_ERROR,
                        "svc_register error");
                return NULL;
        }

        svc_run ();
        gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_SVC_RUN_RETURNED,
                "svc_run returned");
        return NULL;
}
