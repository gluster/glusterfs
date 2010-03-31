/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "rpcsvc.h"
#include "dict.h"
#include "xlator.h"
#include "mount3.h"
#include "xdr-nfs3.h"
#include "msg-nfs3.h"
#include "iobuf.h"
#include "nfs-common.h"
#include "nfs3-fh.h"
#include "nfs-fops.h"
#include "nfs-inodes.h"
#include "nfs-generics.h"
#include "locking.h"
#include "iatt.h"


#include <errno.h>
#include <sys/socket.h>
#include <sys/uio.h>

typedef ssize_t (*mnt3_serializer) (struct iovec outmsg, void *args);


/* Generic reply function for MOUNTv3 specific replies. */
int
mnt3svc_submit_reply (rpcsvc_request_t *req, void *arg, mnt3_serializer sfunc)
{
        struct iovec            outmsg = {0, };
        struct iobuf            *iob = NULL;
        struct mount3_state     *ms = NULL;
        int                     ret = -1;

        if (!req)
                return -1;

        ms = (struct mount3_state *)rpcsvc_request_program_private (req);
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "mount state not found");
                goto ret;
        }

        /* First, get the io buffer into which the reply in arg will
         * be serialized.
         */
        iob = iobuf_get (ms->iobpool);
        if (!iob) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to get iobuf");
                goto ret;
        }

        iobuf_to_iovec (iob, &outmsg);
        /* Use the given serializer to translate the give C structure in arg
         * to XDR format which will be written into the buffer in outmsg.
         */
        outmsg.iov_len = sfunc (outmsg, arg);

        /* Then, submit the message for transmission. */
        ret = rpcsvc_submit_message (req, outmsg, iob);
        iobuf_unref (iob);
        if (ret == -1) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Reply submission failed");
                goto ret;
        }

        ret = 0;
ret:
        return ret;
}


/* Generic error reply function, just pass the err status
 * and it will do the rest, including transmission.
 */
int
mnt3svc_mnt_error_reply (rpcsvc_request_t *req, int mntstat)
{
        mountres3       res;

        if (!req)
                return -1;

        res.fhs_status = mntstat;
        mnt3svc_submit_reply (req, (void *)&res,
                              (mnt3_serializer)xdr_serialize_mountres3);

        return 0;
}


mountstat3
mnt3svc_errno_to_mnterr (int32_t errnum)
{
        mountstat3      stat;

        switch (errnum) {

                case 0:
                        stat = MNT3_OK;
                        break;
                case ENOENT:
                        stat = MNT3ERR_NOENT;
                        break;
                case EPERM:
                        stat = MNT3ERR_PERM;
                        break;
                case EIO:
                        stat = MNT3ERR_IO;
                        break;
                case EACCES:
                        stat = MNT3ERR_ACCES;
                        break;
                case ENOTDIR:
                        stat = MNT3ERR_NOTDIR;
                        break;
                case EINVAL:
                        stat = MNT3ERR_INVAL;
                        break;
                case ENOSYS:
                        stat = MNT3ERR_NOTSUPP;
                        break;
                case ENOMEM:
                        stat = MNT3ERR_SERVERFAULT;
                        break;
                default:
                        stat = MNT3ERR_SERVERFAULT;
                        break;
        }

        return stat;
}


mountres3
mnt3svc_set_mountres3 (mountstat3 stat, struct nfs3_fh *fh, int *authflavor,
                       u_int aflen)
{
        mountres3       res = {0, };
        uint32_t        fhlen = 0;

        res.fhs_status = stat;
        fhlen = nfs3_fh_compute_size (fh);
        res.mountres3_u.mountinfo.fhandle.fhandle3_len = fhlen;
        res.mountres3_u.mountinfo.fhandle.fhandle3_val = (char *)fh;
        res.mountres3_u.mountinfo.auth_flavors.auth_flavors_val = authflavor;
        res.mountres3_u.mountinfo.auth_flavors.auth_flavors_len = aflen;

        return res;
}


int
mnt3svc_update_mountlist (struct mount3_state *ms, rpcsvc_request_t *req,
                          xlator_t *exportxl)
{
        struct mountentry       *me = NULL;
        int                     ret = -1;

        if ((!ms) || (!req) || (!exportxl))
                return -1;

        me = (struct mountentry *)CALLOC (1, sizeof (*me));
        if (!me)
                return -1;

        strcpy (me->exname, exportxl->name);
        INIT_LIST_HEAD (&me->mlist);
        /* Must get the IP or hostname of the client so we
         * can map it into the mount entry.
         */
        ret = rpcsvc_conn_peername (req->conn, me->hostname, MNTPATHLEN);
        if (ret == -1)
                goto free_err;

        LOCK (&ms->mountlock);
        {
                list_add_tail (&me->mlist, &ms->mountlist);
        }
        UNLOCK (&ms->mountlock);

free_err:
        if (ret == -1)
                FREE (me);

        return ret;
}


int32_t
mnt3svc_lookup_mount_cbk (call_frame_t *frame, void  *cookie,
                          xlator_t *this, int32_t op_ret, int32_t op_errno,
                          inode_t *inode, struct iatt *buf, dict_t *xattr,
                          struct iatt *postparent)
{
        mountres3               res = {0, };
        rpcsvc_request_t        *req = NULL;
        struct nfs3_fh          fh = {{0}, };
        struct mount3_state     *ms = NULL;
        xlator_t                *exportxl = NULL;
        mountstat3              status = 0;
        int                     autharr[10];
        int                     autharrlen = 0;
        rpcsvc_t                *svc = NULL;

        req = (rpcsvc_request_t *)frame->local;

        if (!req)
                return -1;

        ms = (struct mount3_state *)rpcsvc_request_program_private (req);
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "mount state not found");
                op_ret = -1;
                op_errno = EINVAL;
        }

        if (op_ret == -1)
                status = mnt3svc_errno_to_mnterr (op_errno);

        if (status != MNT3_OK)
                goto xmit_res;

        exportxl = (xlator_t *)cookie;
        fh = nfs3_fh_build_root_fh (ms->nfsx->children, exportxl, *buf);
        mnt3svc_update_mountlist (ms, req, exportxl);
xmit_res:
        gf_log (GF_MNT, GF_LOG_DEBUG, "Mount reply status: %d", status);
        if (op_ret == 0) {
                svc = rpcsvc_request_service (req);
                autharrlen = rpcsvc_auth_array (svc, exportxl->name, autharr,
                                                10);
        }

        res = mnt3svc_set_mountres3 (status, &fh, autharr, autharrlen);
        mnt3svc_submit_reply (req, (void *)&res,
                              (mnt3_serializer)xdr_serialize_mountres3);

        return 0;
}


int
mnt3svc_mount (rpcsvc_request_t *req, xlator_t * xl)
{
        loc_t           oploc = {0, };
        int             ret = -1;
        nfs_user_t      nfu = {0, };

        if ((!req) || (!xl))
                return ret;

        ret = nfs_ino_loc_fill (xl->itable, 1, 0, &oploc);
        /* To service the mount request, all we need to do
         * is to send a lookup fop that returns the stat
         * for the root of the child volume. This is
         * used to build the root fh sent to the client.
         */
        nfs_request_user_init (&nfu, req);
        ret = nfs_lookup (xl, &nfu, &oploc, mnt3svc_lookup_mount_cbk,
                          (void *)req);
        nfs_loc_wipe (&oploc);

        return ret;
}


int
mnt3svc_mnt (rpcsvc_request_t *req)
{
        struct iovec            pvec = {0, };
        char                    path[MNTPATHLEN];
        int                     ret = -1;
        xlator_t                *targetxl = NULL;
        struct mount3_state     *ms = NULL;
        rpcsvc_t                *svc = NULL;
        mountstat3              mntstat = MNT3ERR_SERVERFAULT;

        if (!req)
                return -1;

        pvec.iov_base = path;
        pvec.iov_len = MNTPATHLEN;
        ret = xdr_to_mountpath (pvec, req->msg);
        if (ret == -1) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to decode args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ms = (struct mount3_state *)rpcsvc_request_program_private (req);
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Mount state not present");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = -1;
                goto rpcerr;
        }

        ret = 0;
        gf_log (GF_MNT, GF_LOG_DEBUG, "dirpath: %s", path);
        targetxl = nfs_mntpath_to_xlator (ms->nfsx->children, path);
        if (!targetxl) {
                ret = -1;
                mntstat = MNT3ERR_NOENT;
                goto mnterr;
        }

        svc = rpcsvc_request_service (req);
        ret = rpcsvc_conn_peer_check (svc->options, targetxl->name,
                                      rpcsvc_request_conn (req));
        if (ret == RPCSVC_AUTH_REJECT) {
                mntstat = MNT3ERR_ACCES;
                ret = -1;
                gf_log (GF_MNT, GF_LOG_TRACE, "Peer not allowed");
                goto mnterr;
        }

        ret = rpcsvc_conn_privport_check (svc, targetxl->name,
                                          rpcsvc_request_conn (req));
        if (ret == RPCSVC_AUTH_REJECT) {
                mntstat = MNT3ERR_ACCES;
                ret = -1;
                gf_log (GF_MNT, GF_LOG_TRACE, "Unprivileged port not allowed");
                goto rpcerr;
        }

        mnt3svc_mount (req, targetxl);
mnterr:
        if (ret == -1) {
                mnt3svc_mnt_error_reply (req, mntstat);
                ret = 0;
        }

rpcerr:
        return ret;
}


int
mnt3svc_null (rpcsvc_request_t *req)
{
        struct iovec    dummyvec = {0, };

        if (!req) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Got NULL request!");
                return 0;
        }

        rpcsvc_submit_generic (req, dummyvec, NULL);
        return 0;
}


mountlist
__build_mountlist (struct mount3_state *ms, int *count)
{
        struct mountbody        *mlist = NULL;
        struct mountbody        *prev = NULL;
        struct mountbody        *first = NULL;
        size_t                  namelen = 0;
        int                     ret = -1;
        struct mountentry       *me = NULL;

        if ((!ms) || (!count))
                return NULL;

        *count = 0;
        gf_log (GF_MNT, GF_LOG_DEBUG, "Building mount list:");
        list_for_each_entry (me, &ms->mountlist, mlist) {
                namelen = strlen (me->exname);
                mlist = CALLOC (1, sizeof (*mlist));
                if (!mlist) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation"
                                " failed");
                        goto free_list;
                }

                mlist->ml_directory = CALLOC (namelen + 2, sizeof (char));
                if (!mlist->ml_directory) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation"
                                " failed");
                        goto free_list;
                }

                strcpy (mlist->ml_directory, "/");
                strcat (mlist->ml_directory, me->exname);

                namelen = strlen (me->hostname);
                mlist->ml_hostname = CALLOC (namelen + 2, sizeof (char));
                if (!mlist->ml_hostname) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation"
                                " failed");
                        goto free_list;
                }

                strcat (mlist->ml_hostname, me->hostname);

                gf_log (GF_MNT, GF_LOG_DEBUG, "mount entry: dir: %s, host: %s",
                        mlist->ml_directory, mlist->ml_hostname);
                if (prev) {
                        prev->ml_next = mlist;
                        prev = mlist;
                } else
                        prev = mlist;

                if (!first)
                        first = mlist;

                (*count)++;
        }

        ret = 0;

free_list:
        if (ret == -1) {
                xdr_free_mountlist (first);
                first = NULL;
        }

        return first;
}


mountlist
mnt3svc_build_mountlist (struct mount3_state *ms, int *count)
{
        struct mountbody        *first = NULL;

        LOCK (&ms->mountlock);
        {
                first = __build_mountlist (ms, count);
        }
        UNLOCK (&ms->mountlock);

        return first;
}


int
mnt3svc_dump (rpcsvc_request_t *req)
{
        int                     ret = -1;
        struct mount3_state     *ms = NULL;
        mountlist               mlist;
        mountstat3              mstat = 0;
        mnt3_serializer         sfunc = NULL;
        void                    *arg = NULL;


        if (!req)
                return -1;

        ms = (struct mount3_state *)rpcsvc_request_program_private (req);
        if (!ms) {
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                goto rpcerr;
        }

        sfunc = (mnt3_serializer)xdr_serialize_mountlist;
        mlist = mnt3svc_build_mountlist (ms, &ret);
        arg = mlist;
        sfunc = (mnt3_serializer)xdr_serialize_mountlist;
        if (!mlist) {
                if (ret != 0) {
                        rpcsvc_request_seterr (req, SYSTEM_ERR);
                        ret = -1;
                        goto rpcerr;
                } else {
                        arg = &mstat;
                        sfunc = (mnt3_serializer)xdr_serialize_mountstat3;
                }
        }

        mnt3svc_submit_reply (req, arg, sfunc);

        xdr_free_mountlist (mlist);
        ret = 0;

rpcerr:
        return ret;
}


int
__mnt3svc_umount (struct mount3_state *ms, char *dirpath, char *hostname)
{
        struct mountentry       *me = NULL;
        char                    *exname = NULL;
        int                     ret = -1;

        if ((!ms) || (!dirpath) || (!hostname))
                return -1;

        if (list_empty (&ms->mountlist))
                return 0;

        list_for_each_entry (me, &ms->mountlist, mlist) {
                exname = dirpath+1;
                if ((strcmp (me->exname, exname) == 0) &&
                    (strcmp (me->hostname, hostname) == 0))
                        break;
        }

        if (!me)
                goto ret;

        gf_log (GF_MNT, GF_LOG_DEBUG, "Unmounting: dir %s, host: %s",
                me->exname, me->hostname);
        list_del (&me->mlist);
        FREE (me);
        ret = 0;
ret:
        return ret;
}



int
mnt3svc_umount (struct mount3_state *ms, char *dirpath, char *hostname)
{
        int ret = -1;
        if ((!ms) || (!dirpath) || (!hostname))
                return -1;

        LOCK (&ms->mountlock);
        {
               ret = __mnt3svc_umount (ms, dirpath, hostname);
        }
        UNLOCK (&ms->mountlock);

        return ret;
}


int
mnt3svc_umnt (rpcsvc_request_t *req)
{
        char                    hostname[MNTPATHLEN];
        char                    dirpath[MNTPATHLEN];
        struct iovec            pvec = {0, };
        int                     ret = -1;
        struct mount3_state     *ms = NULL;
        mountstat3              mstat = MNT3_OK;

        if (!req)
                return -1;

        /* Remove the mount point from the exports list. */
        pvec.iov_base = dirpath;
        pvec.iov_len = MNTPATHLEN;
        ret = xdr_to_mountpath (pvec, req->msg);;
        if (ret == -1) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed decode args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ms = (struct mount3_state *)rpcsvc_request_program_private (req);
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Mount state not present");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = -1;
                goto rpcerr;
        }

        ret = rpcsvc_conn_peername (req->conn, hostname, MNTPATHLEN);
        if (ret != 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to get remote name: %s",
                        gai_strerror (ret));
                goto try_umount_with_addr;
        }

        gf_log (GF_MNT, GF_LOG_DEBUG, "dirpath: %s, hostname: %s", dirpath,
                hostname);
        ret = mnt3svc_umount (ms, dirpath, hostname);

        /* Unmount succeeded with the given hostname. */
        if (ret == 0)
                goto snd_reply;

try_umount_with_addr:
        if (ret != 0)
                ret = rpcsvc_conn_peeraddr (req->conn, hostname, MNTPATHLEN,
                                            NULL, 0);

        if (ret != 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to get remote addr: %s",
                        gai_strerror (ret));
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                goto rpcerr;
        }

        gf_log (GF_MNT, GF_LOG_DEBUG, "dirpath: %s, hostname: %s", dirpath,
                hostname);
        ret = mnt3svc_umount (ms, dirpath, hostname);
        if (ret == -1)
                mstat = MNT3ERR_INVAL;

        ret = 0;
snd_reply:
        mnt3svc_submit_reply (req, &mstat,
                              (mnt3_serializer)xdr_serialize_mountstat3);

rpcerr:
        return ret;
}


int
__mnt3svc_umountall (struct mount3_state *ms)
{
        struct mountentry       *me = NULL;

        if (!ms)
                return -1;

        list_for_each_entry (me, &ms->mountlist, mlist) {
                list_del (&me->mlist);
                FREE (me);
        }

        return 0;
}


int
mnt3svc_umountall (struct mount3_state *ms)
{
        int     ret = -1;
        if (!ms)
                return -1;

        LOCK (&ms->mountlock);
        {
               ret = __mnt3svc_umountall (ms);
        }
        UNLOCK (&ms->mountlock);

        return ret;
}


int
mnt3svc_umntall (rpcsvc_request_t *req)
{
        int                     ret = -1;
        struct mount3_state     *ms = NULL;
        mountstat3              mstat = MNT3_OK;

        if (!req)
                return -1;

        ms = (struct mount3_state *)rpcsvc_request_program_private (req);
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Mount state not present");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = -1;
                goto rpcerr;
        }

        mnt3svc_umountall (ms);
        mnt3svc_submit_reply (req, &mstat,
                              (mnt3_serializer)xdr_serialize_mountstat3);

rpcerr:
        return ret;
}


exports
mnt3_xlchildren_to_exports (rpcsvc_t *svc, xlator_list_t *cl)
{
        struct exportnode       *elist = NULL;
        struct exportnode       *prev = NULL;
        struct exportnode       *first = NULL;
        size_t                  namelen = 0;
        int                     ret = -1;
        char                    *addrstr = NULL;

        if ((!cl) || (!svc))
                return NULL;

        while (cl) {
                namelen = strlen (cl->xlator->name);
                elist = CALLOC (1, sizeof (*elist));
                if (!elist) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation"
                                " failed");
                        goto free_list;
                }

                elist->ex_dir = CALLOC (namelen + 2, sizeof (char));
                if (!elist->ex_dir) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation"
                                " failed");
                        goto free_list;
                }

                strcpy (elist->ex_dir, "/");
                strcat (elist->ex_dir, cl->xlator->name);

                addrstr = rpcsvc_volume_allowed (svc->options,cl->xlator->name);
                if (addrstr)
                        addrstr = strdup (addrstr);
                else
                        addrstr = strdup ("No Access");

                elist->ex_groups = CALLOC (1, sizeof (struct groupnode));
                if (!elist->ex_groups) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation"
                                " failed");
                        goto free_list;
                }

                elist->ex_groups->gr_name = addrstr;
                if (prev) {
                        prev->ex_next = elist;
                        prev = elist;
                } else
                        prev = elist;

                if (!first)
                        first = elist;

                cl = cl->next;
        }

        ret = 0;

free_list:
        if (ret == -1) {
                xdr_free_exports_list (first);
                first = NULL;
        }

        return first;
}


int
mnt3svc_export (rpcsvc_request_t *req)
{
        struct mount3_state     *ms = NULL;
        exports                 elist = NULL;
        int                     ret = -1;

        if (!req)
                return -1;

        ms = (struct mount3_state *)rpcsvc_request_program_private (req);
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "mount state not found");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                goto err;
        }

        /* Using the children translator names, build the export list */
        elist = mnt3_xlchildren_to_exports (rpcsvc_request_service (req),
                                            ms->nfsx->children);
        if (!elist) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to build exports list");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                goto err;
        }

        /* Note how the serializer is passed to the generic reply function. */
        mnt3svc_submit_reply (req, &elist,
                              (mnt3_serializer)xdr_serialize_exports);

        xdr_free_exports_list (elist);
        ret = 0;
err:
        return ret;
}


struct mount3_state *
mnt3_init_state (xlator_t *nfsx)
{
        struct mount3_state     *ms = NULL;

        if (!nfsx)
                return NULL;

        ms = CALLOC (1, sizeof (*ms));
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation failed");
                return NULL;
        }

        ms->iobpool = nfsx->ctx->iobuf_pool;
        ms->nfsx = nfsx;
        ms->exports = nfsx->children;
        INIT_LIST_HEAD (&ms->mountlist);
        LOCK_INIT (&ms->mountlock);

        return ms;
}

rpcsvc_actor_t  mnt3svc_actors[MOUNT3_PROC_COUNT] = {
        {"NULL", MOUNT3_NULL, mnt3svc_null, NULL, NULL},
        {"MNT", MOUNT3_MNT, mnt3svc_mnt, NULL, NULL},
        {"DUMP", MOUNT3_DUMP, mnt3svc_dump, NULL, NULL},
        {"UMNT", MOUNT3_UMNT, mnt3svc_umnt, NULL, NULL},
        {"UMNTALL", MOUNT3_UMNTALL, mnt3svc_umntall, NULL, NULL},
        {"EXPORT", MOUNT3_EXPORT, mnt3svc_export, NULL, NULL}
};



/* Static init parts are assigned here, dynamic ones are done in
 * mnt3svc_init and mnt3_init_state.
 */
rpcsvc_program_t        mnt3prog = {
                        .progname       = "MOUNT3",
                        .prognum        = MOUNT_PROGRAM,
                        .progver        = MOUNT_V3,
                        .progport       = GF_MOUNTV3_PORT,
                        .progaddrfamily = AF_INET,
                        .proghost       = NULL,
                        .actors         = mnt3svc_actors,
                        .numactors      = MOUNT3_PROC_COUNT,
};


rpcsvc_program_t *
mnt3svc_init (xlator_t *nfsx)
{
        struct mount3_state     *mstate = NULL;

        if (!nfsx)
                return NULL;

        gf_log (GF_MNT, GF_LOG_DEBUG, "Initing Mount v3 state");
        mstate = mnt3_init_state (nfsx);
        if (!mstate) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Mount v3 state init failed");
                goto err;
        }

        mnt3prog.private = mstate;

        return &mnt3prog;
err:
        return NULL;
}


rpcsvc_actor_t  mnt1svc_actors[MOUNT1_PROC_COUNT] = {
        {"NULL", MOUNT1_NULL, mnt3svc_null, NULL, NULL},
        {{0}, },
        {"DUMP", MOUNT1_DUMP, mnt3svc_dump, NULL, NULL},
        {"UMNT", MOUNT1_UMNT, mnt3svc_umnt, NULL, NULL},
        {{0}, },
        {"EXPORT", MOUNT1_EXPORT, mnt3svc_export, NULL, NULL}
};

rpcsvc_program_t        mnt1prog = {
                        .progname       = "MOUNT1",
                        .prognum        = MOUNT_PROGRAM,
                        .progver        = MOUNT_V1,
                        .progport       = GF_MOUNTV1_PORT,
                        .progaddrfamily = AF_INET,
                        .proghost       = NULL,
                        .actors         = mnt1svc_actors,
                        .numactors      = MOUNT1_PROC_COUNT,
};


rpcsvc_program_t *
mnt1svc_init (xlator_t *nfsx)
{
        struct mount3_state     *mstate = NULL;

        if (!nfsx)
                return NULL;

        gf_log (GF_MNT, GF_LOG_DEBUG, "Initing Mount v1 state");
        mstate = mnt3_init_state (nfsx);
        if (!mstate) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Mount v3 state init failed");
                goto err;
        }

        mnt1prog.private = mstate;

        return &mnt1prog;
err:
        return NULL;
}


