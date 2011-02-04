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
#include "nfs-mem-types.h"
#include "nfs.h"
#include "common-utils.h"


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

        ms = (struct mount3_state *)nfs_rpcsvc_request_program_private (req);
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
        ret = nfs_rpcsvc_submit_message (req, outmsg, iob);
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
                          char  *expname)
{
        struct mountentry       *me = NULL;
        int                     ret = -1;

        if ((!ms) || (!req) || (!expname))
                return -1;

        me = (struct mountentry *)GF_CALLOC (1, sizeof (*me),
                                             gf_nfs_mt_mountentry);
        if (!me)
                return -1;

        strcpy (me->exname, expname);
        INIT_LIST_HEAD (&me->mlist);
        /* Must get the IP or hostname of the client so we
         * can map it into the mount entry.
         */
        ret = nfs_rpcsvc_conn_peername (req->conn, me->hostname, MNTPATHLEN);
        if (ret == -1)
                goto free_err;

        LOCK (&ms->mountlock);
        {
                list_add_tail (&me->mlist, &ms->mountlist);
        }
        UNLOCK (&ms->mountlock);

free_err:
        if (ret == -1)
                GF_FREE (me);

        return ret;
}


int
__mnt3_get_volume_id (struct mount3_state *ms, xlator_t *mntxl,
                      uuid_t volumeid)
{
        int                     ret = -1;
        struct mnt3_export      *exp = NULL;

        if ((!ms) || (!mntxl))
                return ret;

        list_for_each_entry (exp, &ms->exportlist, explist) {
                if (exp->vol == mntxl) {
                        uuid_copy (volumeid, exp->volumeid);
                        ret = 0;
                        goto out;
                }
        }

out:
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
        mountstat3              status = 0;
        int                     autharr[10];
        int                     autharrlen = 0;
        rpcsvc_t                *svc = NULL;
        xlator_t                *mntxl = NULL;
        uuid_t                  volumeid = {0, };
        char                    fhstr[1024];

        req = (rpcsvc_request_t *)frame->local;

        if (!req)
                return -1;

        mntxl = (xlator_t *)cookie;
        ms = (struct mount3_state *)nfs_rpcsvc_request_program_private (req);
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "mount state not found");
                op_ret = -1;
                op_errno = EINVAL;
        }

        if (op_ret == -1)
                status = mnt3svc_errno_to_mnterr (op_errno);

        if (status != MNT3_OK)
                goto xmit_res;

        mnt3svc_update_mountlist (ms, req, mntxl->name);
        if (gf_nfs_dvm_off (nfs_state (ms->nfsx))) {
                fh = nfs3_fh_build_indexed_root_fh (ms->nfsx->children, mntxl);
                goto xmit_res;
        }

        __mnt3_get_volume_id (ms, mntxl, volumeid);
        fh = nfs3_fh_build_uuid_root_fh (volumeid);

xmit_res:
        nfs3_fh_to_str (&fh, fhstr);
        gf_log (GF_MNT, GF_LOG_DEBUG, "MNT reply: fh %s, status: %d", fhstr,
                status);
        if (op_ret == 0) {
                svc = nfs_rpcsvc_request_service (req);
                autharrlen = nfs_rpcsvc_auth_array (svc, mntxl->name, autharr,
                                                    10);
        }

        res = mnt3svc_set_mountres3 (status, &fh, autharr, autharrlen);
        mnt3svc_submit_reply (req, (void *)&res,
                              (mnt3_serializer)xdr_serialize_mountres3);

        return 0;
}


int
mnt3_match_dirpath_export (char *expname, char *dirpath)
{
        int     ret = 0;
        int     dlen = 0;

        if ((!expname) || (!dirpath))
                return 0;

        /* Some clients send a dirpath for mount that includes the slash at the
         * end. String compare for searching the export will fail because our
         * exports list does not include that slash. Remove the slash to
         * compare.
         */
        dlen = strlen (dirpath);
        if (dirpath [dlen - 1] == '/')
                dirpath [dlen - 1] = '\0';

        if (dirpath[0] != '/')
                expname++;

        if (strcmp (expname, dirpath) == 0)
                ret = 1;

        return ret;
}


int
mnt3svc_mount_inode (rpcsvc_request_t *req, struct mount3_state *ms,
                     xlator_t * xl, inode_t *exportinode)
{
        int             ret = -EFAULT;
        nfs_user_t      nfu = {0, };
        loc_t           exportloc = {0, };

        if ((!req) || (!xl) || (!ms) || (!exportinode))
                return ret;

        ret = nfs_inode_loc_fill (exportinode, &exportloc);
        if (ret < 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Loc fill failed for export inode"
                        ": ino %"PRIu64", volume: %s",
                        exportinode->ino, xl->name);
                goto err;
        }

        /* To service the mount request, all we need to do
         * is to send a lookup fop that returns the stat
         * for the root of the child volume. This is
         * used to build the root fh sent to the client.
         */
        nfs_request_user_init (&nfu, req);
        ret = nfs_lookup (ms->nfsx, xl, &nfu, &exportloc,
                          mnt3svc_lookup_mount_cbk, (void *)req);

        nfs_loc_wipe (&exportloc);
err:
        return ret;
}


/* For a volume mount request, we just have to create loc on the root inode,
 * and send a lookup. In the lookup callback the mount reply is send along with
 * the file handle.
 */
int
mnt3svc_volume_mount (rpcsvc_request_t *req, struct mount3_state *ms,
                      struct mnt3_export *exp)
{
        inode_t         *exportinode = NULL;
        int             ret = -EFAULT;
        uuid_t          rootgfid = {0, };

        if ((!req) || (!exp) || (!ms))
                return ret;

        rootgfid[15] = 1;
        exportinode = inode_find (exp->vol->itable, rootgfid);
        if (!exportinode) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Faild to get root inode");
                ret = -ENOENT;
                goto err;
        }

        ret = mnt3svc_mount_inode (req, ms, exp->vol, exportinode);
        inode_unref (exportinode);

err:
        return ret;
}


/* The catch with directory exports is that the first component of the export
 * name will be the name of the volume.
 * Any lookup that needs to be performed to build the directory's file handle
 * needs to start from the directory path from the root of the volume. For that
 * we need to strip out the volume name first.
 */
char *
__volume_subdir (char *dirpath, char **volname)
{
        char    *subdir = NULL;
        int     volname_len = 0;

        if (!dirpath)
                return NULL;

        if (dirpath[0] == '/')
                dirpath++;

        subdir = index (dirpath, (int)'/');
        if (!subdir)
                goto out;

        if (!*volname)
                goto out;

        /* subdir points to the first / after the volume name while dirpath
         * points to the first char of the volume name.
         */
        volname_len = subdir - dirpath;
        strncpy (*volname, dirpath, volname_len);
        *(*volname + volname_len) = '\0';
out:
        return subdir;
}


void
mnt3_resolve_state_wipe (mnt3_resolve_t *mres)
{
        if (!mres)
                return;

        nfs_loc_wipe (&mres->resolveloc);
        GF_FREE (mres);

}


/* Sets up the component argument to contain the next component in the path and
 * sets up path as an absolute path starting from the next component.
 */
char *
__setup_next_component (char *path, char *component)
{
        char    *comp = NULL;
        char    *nextcomp = NULL;

        if ((!path) || (!component))
                return NULL;

        strcpy (component, path);
        comp = index (component, (int)'/');
        if (!comp)
                goto err;

        comp++;
        nextcomp = index (comp, (int)'/');
        if (nextcomp) {
                strcpy (path, nextcomp);
                *nextcomp = '\0';
        } else
                path[0] = '\0';

err:
        return comp;
}

int32_t
mnt3_resolve_subdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, inode_t *inode,
                         struct iatt *buf, dict_t *xattr,
                         struct iatt *postparent);

/* There are multiple components in the directory export path and each one
 * needs to be looked up one after the other.
 */
int
__mnt3_resolve_export_subdir_comp (mnt3_resolve_t *mres)
{
        char            dupsubdir[MNTPATHLEN];
        char            *nextcomp = NULL;
        int             ret = -EFAULT;
        nfs_user_t      nfu = {0, };
        uuid_t          gfid = {0, };

        if (!mres)
                return ret;

        nextcomp = __setup_next_component (mres->remainingdir, dupsubdir);
        if (!nextcomp)
                goto err;

        /* Wipe the contents of the previous component */
        uuid_copy (gfid, mres->resolveloc.inode->gfid);
        nfs_loc_wipe (&mres->resolveloc);
        ret = nfs_entry_loc_fill (mres->exp->vol->itable, gfid, nextcomp,
                                  &mres->resolveloc, NFS_RESOLVE_CREATE);
        if ((ret < 0) && (ret != -2)) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to resolve and create "
                        "inode: parent gfid %s, entry %s", uuid_utoa (mres->resolveloc.inode->gfid), nextcomp);
                ret = -EFAULT;
                goto err;
        }

        nfs_request_user_init (&nfu, mres->req);
        ret = nfs_lookup (mres->mstate->nfsx, mres->exp->vol, &nfu,
                          &mres->resolveloc, mnt3_resolve_subdir_cbk, mres);

err:
        return ret;
}


int32_t
mnt3_resolve_subdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, inode_t *inode,
                         struct iatt *buf, dict_t *xattr,
                         struct iatt *postparent)
{
        mnt3_resolve_t          *mres = NULL;
        mountstat3              mntstat = MNT3ERR_SERVERFAULT;
        struct nfs3_fh          fh = {{0}, };
        int                     autharr[10];
        int                     autharrlen = 0;
        rpcsvc_t                *svc = NULL;
        mountres3               res = {0, };
        xlator_t                *mntxl = NULL;

        mres = frame->local;
        mntxl = (xlator_t *)cookie;
        if (op_ret == -1) {
                mntstat = mnt3svc_errno_to_mnterr (op_errno);
                goto err;
        }

        inode_link (mres->resolveloc.inode, mres->resolveloc.parent,
                    mres->resolveloc.name, buf);

        nfs3_fh_build_child_fh (&mres->parentfh, buf, &fh);
        if (strlen (mres->remainingdir) <= 0) {
                op_ret = -1;
                mntstat = MNT3_OK;
                mnt3svc_update_mountlist (mres->mstate, mres->req,
                                          mres->exp->expname);
                goto err;
        }

        mres->parentfh = fh;
        op_ret = __mnt3_resolve_export_subdir_comp (mres);
        if (op_ret < 0)
                mntstat = mnt3svc_errno_to_mnterr (-op_ret);
err:
        if (op_ret == -1) {
                gf_log (GF_MNT, GF_LOG_DEBUG, "Mount reply status: %d",
                        mntstat);
                svc = nfs_rpcsvc_request_service (mres->req);
                autharrlen = nfs_rpcsvc_auth_array (svc, mntxl->name, autharr,
                                                    10);

                res = mnt3svc_set_mountres3 (mntstat, &fh, autharr, autharrlen);
                mnt3svc_submit_reply (mres->req, (void *)&res,
                                      (mnt3_serializer)xdr_serialize_mountres3);
                mnt3_resolve_state_wipe (mres);
        }

        return 0;
}



/* We will always have to perform a hard lookup on all the components of a
 * directory export for a mount request because in the mount reply we need the
 * file handle of the directory. Our file handle creation code is designed with
 * the assumption that to build a child file/dir fh, we'll always have the
 * parent dir's fh available so that we may copy the hash array of the previous
 * dir levels.
 *
 * Since we do not store the file handles anywhere, for every mount request we
 * must resolve the file handles of every component so that the parent dir file
 * of the exported directory can be built.
 */
int
__mnt3_resolve_subdir (mnt3_resolve_t *mres)
{
        char            dupsubdir[MNTPATHLEN];
        char            *firstcomp = NULL;
        int             ret = -EFAULT;
        nfs_user_t      nfu = {0, };
        uuid_t          rootgfid = {0, };

        if (!mres)
                return ret;

        firstcomp = __setup_next_component (mres->remainingdir, dupsubdir);
        if (!firstcomp)
                goto err;

        rootgfid[15] = 1;
        ret = nfs_entry_loc_fill (mres->exp->vol->itable, rootgfid, firstcomp,
                                  &mres->resolveloc, NFS_RESOLVE_CREATE);
        if ((ret < 0) && (ret != -2)) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to resolve and create "
                        "inode for volume root: %s", mres->exp->vol->name);
                ret = -EFAULT;
                goto err;
        }

        nfs_request_user_init (&nfu, mres->req);
        ret = nfs_lookup (mres->mstate->nfsx, mres->exp->vol, &nfu,
                          &mres->resolveloc, mnt3_resolve_subdir_cbk, mres);

err:
        return ret;
}


int
mnt3_resolve_subdir (rpcsvc_request_t *req, struct mount3_state *ms,
                     struct mnt3_export *exp, char *subdir)
{
        mnt3_resolve_t  *mres = NULL;
        int             ret = -EFAULT;
        struct nfs3_fh  pfh = GF_NFS3FH_STATIC_INITIALIZER;

        if ((!req) || (!ms) || (!exp) || (!subdir))
                return ret;

        mres = GF_CALLOC (1, sizeof (mnt3_resolve_t), gf_nfs_mt_mnt3_resolve);
        if (!mres) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation failed");
                goto err;
        }

        mres->exp = exp;
        mres->mstate = ms;
        mres->req = req;
        strcpy (mres->remainingdir, subdir);
        if (gf_nfs_dvm_off (nfs_state (ms->nfsx)))
                pfh = nfs3_fh_build_indexed_root_fh (mres->mstate->nfsx->children, mres->exp->vol);
        else
                pfh = nfs3_fh_build_uuid_root_fh (exp->volumeid);

        mres->parentfh = pfh;
        ret = __mnt3_resolve_subdir (mres);
        if (ret < 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to resolve export dir: %s"
                        , mres->exp->expname);
                GF_FREE (mres);
        }

err:
        return ret;
}


int
mnt3_resolve_export_subdir (rpcsvc_request_t *req, struct mount3_state *ms,
                            struct mnt3_export *exp)
{
        char            *volume_subdir = NULL;
        int             ret = -EFAULT;

        if ((!req) || (!ms) || (!exp))
                return ret;

        volume_subdir = __volume_subdir (exp->expname, NULL);
        if (!volume_subdir)
                goto err;

        ret = mnt3_resolve_subdir (req, ms, exp, exp->expname);
        if (ret < 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to resolve export dir: %s"
                        , exp->expname);
                goto err;
        }

err:
        return ret;
}


int
mnt3svc_mount (rpcsvc_request_t *req, struct mount3_state *ms,
               struct mnt3_export *exp)
{
        int     ret = -EFAULT;

        if ((!req) || (!ms) || (!exp))
                return ret;

        if (exp->exptype == MNT3_EXPTYPE_VOLUME)
                ret = mnt3svc_volume_mount (req, ms, exp);
        else if (exp->exptype == MNT3_EXPTYPE_DIR)
                ret = mnt3_resolve_export_subdir (req, ms, exp);

        return ret;
}


/* mnt3_mntpath_to_xlator sets this to 1 if the mount is for a full
* volume or 2 for a subdir in the volume.
*/
struct mnt3_export *
mnt3_mntpath_to_export (struct mount3_state *ms, char *dirpath)
{
        struct mnt3_export      *exp = NULL;
        struct mnt3_export      *found = NULL;

        if ((!ms) || (!dirpath))
                return NULL;

        list_for_each_entry (exp, &ms->exportlist, explist) {

                /* Search for the an exact match with the volume */
                if (mnt3_match_dirpath_export (exp->expname, dirpath)) {
                        found = exp;
                        gf_log (GF_MNT, GF_LOG_DEBUG, "Found export volume: "
                                "%s", exp->vol->name);
                        goto foundexp;
                }
        }

        gf_log (GF_MNT, GF_LOG_DEBUG, "Export not found");
foundexp:
        return found;
}


int
mnt3_check_client_net (struct mount3_state *ms, rpcsvc_request_t *req,
                       xlator_t *targetxl)
{
        rpcsvc_t        *svc = NULL;
        int             ret = -1;

        if ((!ms) || (!req) || (!targetxl))
                return -1;

        svc = nfs_rpcsvc_request_service (req);
        ret = nfs_rpcsvc_conn_peer_check (svc->options, targetxl->name,
                                          nfs_rpcsvc_request_conn (req));
        if (ret == RPCSVC_AUTH_REJECT) {
                gf_log (GF_MNT, GF_LOG_TRACE, "Peer not allowed");
                goto err;
        }

        ret = nfs_rpcsvc_conn_privport_check (svc, targetxl->name,
                                              nfs_rpcsvc_request_conn (req));
        if (ret == RPCSVC_AUTH_REJECT) {
                gf_log (GF_MNT, GF_LOG_TRACE, "Unprivileged port not allowed");
                goto err;
        }

        ret = 0;
err:
        return ret;
}


int
mnt3_parse_dir_exports (rpcsvc_request_t *req, struct mount3_state *ms,
                        char *subdir)
{
        char                    volname[1024];
        struct mnt3_export      *exp = NULL;
        char                    *volname_ptr = NULL;
        int                     ret = -1;

        if ((!ms) || (!subdir))
                return -1;

        volname_ptr = volname;
        subdir = __volume_subdir (subdir, &volname_ptr);
        if (!subdir)
                goto err;

        exp = mnt3_mntpath_to_export (ms, volname);
        if (!exp)
                goto err;

        ret = mnt3_resolve_subdir (req, ms, exp, subdir);
        if (ret < 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to resolve export dir: %s"
                        , subdir);
                goto err;
        }

err:
        return ret;
}


int
mnt3_find_export (rpcsvc_request_t *req, char *path, struct mnt3_export **e)
{
        int                     ret = -EFAULT;
        struct mount3_state     *ms = NULL;
        struct mnt3_export      *exp = NULL;
        struct nfs_state        *nfs = NULL;

        if ((!req) || (!path) || (!e))
                return -1;

        ms = (struct mount3_state *)nfs_rpcsvc_request_program_private (req);
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Mount state not present");
                nfs_rpcsvc_request_seterr (req, SYSTEM_ERR);
                goto err;
        }

        nfs = (struct nfs_state *)ms->nfsx->private;
        gf_log (GF_MNT, GF_LOG_DEBUG, "dirpath: %s", path);
        exp = mnt3_mntpath_to_export (ms, path);
        if (exp) {
                ret = 0;
                *e = exp;
                goto err;
        }

        if (!gf_mnt3_export_dirs(ms)) {
                ret = -1;
                goto err;
        }

        ret = mnt3_parse_dir_exports (req, ms, path);
        if (ret == 0) {
                ret = -2;
                goto err;
        }

err:
        return ret;
}


int
mnt3svc_mnt (rpcsvc_request_t *req)
{
        struct iovec            pvec = {0, };
        char                    path[MNTPATHLEN];
        int                     ret = -1;
        struct mount3_state     *ms = NULL;
        mountstat3              mntstat = MNT3ERR_SERVERFAULT;
        struct mnt3_export      *exp = NULL;
        struct nfs_state        *nfs = NULL;

        if (!req)
                return -1;

        pvec.iov_base = path;
        pvec.iov_len = MNTPATHLEN;
        ret = xdr_to_mountpath (pvec, req->msg);
        if (ret == -1) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to decode args");
                nfs_rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ms = (struct mount3_state *)nfs_rpcsvc_request_program_private (req);
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Mount state not present");
                nfs_rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = -1;
                goto rpcerr;
        }

        ret = 0;
        nfs = (struct nfs_state *)ms->nfsx->private;
        gf_log (GF_MNT, GF_LOG_DEBUG, "dirpath: %s", path);
        ret = mnt3_find_export (req, path, &exp);
        if (ret == -2) {
                ret = 0;
                goto rpcerr;
        } else if (ret < 0) {
                ret = -1;
                mntstat = MNT3ERR_NOENT;
                goto mnterr;
        }

        if (!nfs_subvolume_started (nfs, exp->vol)) {
                gf_log (GF_MNT, GF_LOG_DEBUG, "Volume %s not started",
                        exp->vol->name);
                ret = -1;
                mntstat = MNT3ERR_NOENT;
                goto mnterr;
        }

        ret = mnt3_check_client_net (ms, req, exp->vol);
        if (ret == RPCSVC_AUTH_REJECT) {
                mntstat = MNT3ERR_ACCES;
                gf_log (GF_MNT, GF_LOG_DEBUG, "Client mount not allowed");
                ret = -1;
                goto mnterr;
        }

        ret = mnt3svc_mount (req, ms, exp);
        if (ret < 0)
                mntstat = mnt3svc_errno_to_mnterr (-ret);
mnterr:
        if (ret < 0) {
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

        nfs_rpcsvc_submit_generic (req, dummyvec, NULL);
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
                mlist = GF_CALLOC (1, sizeof (*mlist), gf_nfs_mt_mountbody);
                if (!mlist) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation"
                                " failed");
                        goto free_list;
                }

                mlist->ml_directory = GF_CALLOC (namelen + 2, sizeof (char),
                                                 gf_nfs_mt_char);
                if (!mlist->ml_directory) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation"
                                " failed");
                        goto free_list;
                }

                strcpy (mlist->ml_directory, "/");
                strcat (mlist->ml_directory, me->exname);

                namelen = strlen (me->hostname);
                mlist->ml_hostname = GF_CALLOC (namelen + 2, sizeof (char),
                                                gf_nfs_mt_char);
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

        ms = (struct mount3_state *)nfs_rpcsvc_request_program_private (req);
        if (!ms) {
                nfs_rpcsvc_request_seterr (req, SYSTEM_ERR);
                goto rpcerr;
        }

        sfunc = (mnt3_serializer)xdr_serialize_mountlist;
        mlist = mnt3svc_build_mountlist (ms, &ret);
        arg = mlist;
        
        if (!mlist) {
                if (ret != 0) {
                        nfs_rpcsvc_request_seterr (req, SYSTEM_ERR);
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

        if (dirpath[0] == '/')
                exname = dirpath+1;
        else
                exname = dirpath;

        list_for_each_entry (me, &ms->mountlist, mlist) {
               if ((strcmp (me->exname, exname) == 0) &&
                    (strcmp (me->hostname, hostname) == 0)) {
                       ret = 0;
                       break;
               }
        }

        /* Need this check here because at the end of the search me might still
         * be pointing to the last entry, which may not be the one we're
         * looking for.
         */
        if (ret == -1)  {/* Not found in list. */
                gf_log (GF_MNT, GF_LOG_DEBUG, "Export not found");
                goto ret;
        }

        if (!me)
                goto ret;

        gf_log (GF_MNT, GF_LOG_DEBUG, "Unmounting: dir %s, host: %s",
                me->exname, me->hostname);
        list_del (&me->mlist);
        GF_FREE (me);
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
                nfs_rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ms = (struct mount3_state *)nfs_rpcsvc_request_program_private (req);
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Mount state not present");
                nfs_rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = -1;
                goto rpcerr;
        }

        ret = nfs_rpcsvc_conn_peername (req->conn, hostname, MNTPATHLEN);
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
                ret = nfs_rpcsvc_conn_peeraddr (req->conn, hostname, MNTPATHLEN,
                                                NULL, 0);

        if (ret != 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to get remote addr: %s",
                        gai_strerror (ret));
                nfs_rpcsvc_request_seterr (req, SYSTEM_ERR);
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
        struct mountentry       *tmp = NULL;

        if (!ms)
                return -1;

        if (list_empty (&ms->mountlist))
                return 0;

        list_for_each_entry_safe (me, tmp, &ms->mountlist, mlist) {
                list_del (&me->mlist);
                GF_FREE (me);
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
        int                     ret = RPCSVC_ACTOR_ERROR;
        struct mount3_state     *ms = NULL;
        mountstat3              mstat = MNT3_OK;

        if (!req)
                return ret;

        ms = (struct mount3_state *)nfs_rpcsvc_request_program_private (req);
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Mount state not present");
                nfs_rpcsvc_request_seterr (req, SYSTEM_ERR);
                goto rpcerr;
        }

        mnt3svc_umountall (ms);
        mnt3svc_submit_reply (req, &mstat,
                              (mnt3_serializer)xdr_serialize_mountstat3);

        ret = RPCSVC_ACTOR_SUCCESS;
rpcerr:
        return ret;
}


exports
mnt3_xlchildren_to_exports (rpcsvc_t *svc, struct mount3_state *ms)
{
        struct exportnode       *elist = NULL;
        struct exportnode       *prev = NULL;
        struct exportnode       *first = NULL;
        size_t                  namelen = 0;
        int                     ret = -1;
        char                    *addrstr = NULL;
        struct mnt3_export      *ent = NULL;
        struct nfs_state        *nfs = NULL;

        if ((!ms) || (!svc))
                return NULL;

        nfs = (struct nfs_state *)ms->nfsx->private;
        list_for_each_entry(ent, &ms->exportlist, explist) {

                /* If volume is not started yet, do not list it for tools like
                 * showmount.
                 */
                if (!nfs_subvolume_started (nfs, ent->vol))
                        continue;

                namelen = strlen (ent->expname) + 1;
                elist = GF_CALLOC (1, sizeof (*elist), gf_nfs_mt_exportnode);
                if (!elist) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation"
                                " failed");
                        goto free_list;
                }

                elist->ex_dir = GF_CALLOC (namelen + 2, sizeof (char),
                                           gf_nfs_mt_char);
                if (!elist->ex_dir) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation"
                                " failed");
                        goto free_list;
                }

                strcpy (elist->ex_dir, ent->expname);

                addrstr = nfs_rpcsvc_volume_allowed (svc->options,
                                                     ent->vol->name);
                if (addrstr)
                        addrstr = gf_strdup (addrstr);
                else
                        addrstr = gf_strdup ("No Access");

                elist->ex_groups = GF_CALLOC (1, sizeof (struct groupnode),
                                              gf_nfs_mt_groupnode);
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

        ms = (struct mount3_state *)nfs_rpcsvc_request_program_private (req);
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "mount state not found");
                nfs_rpcsvc_request_seterr (req, SYSTEM_ERR);
                goto err;
        }

        /* Using the children translator names, build the export list */
        elist = mnt3_xlchildren_to_exports (nfs_rpcsvc_request_service (req),
                                            ms);
        /* Do not return error when exports list is empty. An exports list can
         * be empty when no subvolumes have come up. No point returning error
         * and confusing the user.
        if (!elist) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to build exports list");
                nfs_rpcsvc_request_seterr (req, SYSTEM_ERR);
                goto err;
        }
        */

        /* Note how the serializer is passed to the generic reply function. */
        mnt3svc_submit_reply (req, &elist,
                              (mnt3_serializer)xdr_serialize_exports);

        xdr_free_exports_list (elist);
        ret = 0;
err:
        return ret;
}


struct mnt3_export *
mnt3_init_export_ent (struct mount3_state *ms, xlator_t *xl, char *exportpath,
                      uuid_t volumeid)
{
        struct mnt3_export      *exp = NULL;
        int                     alloclen = 0;
        int                     ret = -1;

        if ((!ms) || (!xl))
                return NULL;

        exp = GF_CALLOC (1, sizeof (*exp), gf_nfs_mt_mnt3_export);
        if (!exp) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation failed");
                return NULL;
        }

        INIT_LIST_HEAD (&exp->explist);
        if (exportpath)
                alloclen = strlen (xl->name) + 2 + strlen (exportpath);
        else
                alloclen = strlen (xl->name) + 2;

        exp->expname = GF_CALLOC (alloclen, sizeof (char), gf_nfs_mt_char);
        if (!exp->expname) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation failed");
                GF_FREE (exp);
                exp = NULL;
                goto err;
        }

        if (exportpath) {
                gf_log (GF_MNT, GF_LOG_TRACE, "Initing dir export: %s:%s",
                        xl->name, exportpath);
                exp->exptype = MNT3_EXPTYPE_DIR;
                ret = snprintf (exp->expname, alloclen, "/%s%s", xl->name,
                                exportpath);
        } else {
                gf_log (GF_MNT, GF_LOG_TRACE, "Initing volume export: %s",
                        xl->name);
                exp->exptype = MNT3_EXPTYPE_VOLUME;
                ret = snprintf (exp->expname, alloclen, "/%s", xl->name);
        }

        /* Just copy without discrimination, we'll determine whether to
         * actually use it when a mount request comes in and a file handle
         * needs to be built.
         */
        uuid_copy (exp->volumeid, volumeid);
        exp->vol = xl;
err:
        return exp;
}


int
__mnt3_init_volume_direxports (struct mount3_state *ms, xlator_t *xlator,
                               char *optstr, uuid_t volumeid)
{
        struct mnt3_export      *newexp = NULL;
        int                     ret = -1;
        char                    *savptr = NULL;
        char                    *dupopt = NULL;
        char                    *token = NULL;

        if ((!ms) || (!xlator) || (!optstr))
                return -1;

        dupopt = gf_strdup (optstr);
        if (!dupopt) {
                gf_log (GF_MNT, GF_LOG_ERROR, "gf_strdup failed");
                goto err;
        }

        token = strtok_r (dupopt, ",", &savptr);
        while (token) {
                newexp = mnt3_init_export_ent (ms, xlator, token, volumeid);
                if (!newexp) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Failed to init dir "
                                "export: %s", token);
                        ret = -1;
                        goto err;
                }

                list_add_tail (&newexp->explist, &ms->exportlist);
                token = strtok_r (NULL, ",", &savptr);
        }

        ret = 0;
err:
        if (dupopt)
                GF_FREE (dupopt);

        return ret;
}


int
__mnt3_init_volume (struct mount3_state *ms, dict_t *opts, xlator_t *xlator)
{
        struct mnt3_export      *newexp = NULL;
        int                     ret = -1;
        char                    searchstr[1024];
        char                    *optstr  = NULL;
        uuid_t                  volumeid = {0, };

        if ((!ms) || (!xlator) || (!opts))
                return -1;

        uuid_clear (volumeid);
        if (gf_nfs_dvm_off (nfs_state (ms->nfsx)))
                goto no_dvm;

        ret = snprintf (searchstr, 1024, "nfs3.%s.volume-id", xlator->name);
        if (ret < 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "snprintf failed");
                ret = -1;
                goto err;
        }

        if (dict_get (opts, searchstr)) {
                ret = dict_get_str (opts, searchstr, &optstr);
                if (ret < 0) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Failed to read option"
                                ": %s", searchstr);
                        ret = -1;
                        goto err;
                }
        } else {
                gf_log (GF_MNT, GF_LOG_ERROR, "DVM is on but volume-id not "
                        "given for volume: %s", xlator->name);
                ret = -1;
                goto err;
        }

        if (optstr) {
                ret = uuid_parse (optstr, volumeid);
                if (ret < 0) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Failed to parse volume "
                                "UUID");
                        ret = -1;
                        goto err;
                }
        }

no_dvm:
        ret = snprintf (searchstr, 1024, "nfs3.%s.export-dir", xlator->name);
        if (ret < 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "snprintf failed");
                ret = -1;
                goto err;
        }

        if (dict_get (opts, searchstr)) {
                ret = dict_get_str (opts, searchstr, &optstr);
                if (ret < 0) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Failed to read option: "
                                "%s", searchstr);
                        ret = -1;
                        goto err;
                }

                ret = __mnt3_init_volume_direxports (ms, xlator, optstr,
                                                     volumeid);
                if (ret == -1) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Dir export setup failed"
                                " for volume: %s", xlator->name);
                        goto err;
                }
        }

        if (ms->export_volumes) {
                newexp = mnt3_init_export_ent (ms, xlator, NULL, volumeid);
                if (!newexp) {
                        ret = -1;
                        goto err;
                }

                list_add_tail (&newexp->explist, &ms->exportlist);
        }
        ret = 0;


err:
        return ret;
}


int
__mnt3_init_volume_export (struct mount3_state *ms, dict_t *opts)
{
        int                     ret = -1;
        char                    *optstr  = NULL;
        /* On by default. */
        gf_boolean_t            boolt = _gf_true;

        if ((!ms) || (!opts))
                return -1;

        if (!dict_get (opts, "nfs3.export-volumes")) {
                ret = 0;
                goto err;
        }

        ret = dict_get_str (opts, "nfs3.export-volumes", &optstr);
        if (ret < 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to read option: "
                        "nfs3.export-volumes");
                ret = -1;
                goto err;
        }

        gf_string2boolean (optstr, &boolt);
        ret = 0;

err:
        if (boolt == _gf_false) {
                gf_log (GF_MNT, GF_LOG_TRACE, "Volume exports disabled");
                ms->export_volumes = 0;
        } else {
                gf_log (GF_MNT, GF_LOG_TRACE, "Volume exports enabled");
                ms->export_volumes = 1;
        }

        return ret;
}


int
__mnt3_init_dir_export (struct mount3_state *ms, dict_t *opts)
{
        int                     ret = -1;
        char                    *optstr  = NULL;
        /* On by default. */
        gf_boolean_t            boolt = _gf_true;

        if ((!ms) || (!opts))
                return -1;

        if (!dict_get (opts, "nfs3.export-dirs")) {
                ret = 0;
                goto err;
        }

        ret = dict_get_str (opts, "nfs3.export-dirs", &optstr);
        if (ret < 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to read option: "
                        "nfs3.export-dirs");
                ret = -1;
                goto err;
        }

        gf_string2boolean (optstr, &boolt);
        ret = 0;

err:
        if (boolt == _gf_false) {
                gf_log (GF_MNT, GF_LOG_TRACE, "Dir exports disabled");
                ms->export_dirs = 0;
        } else {
                gf_log (GF_MNT, GF_LOG_TRACE, "Dir exports enabled");
                ms->export_dirs = 1;
        }

        return ret;
}


int
mnt3_init_options (struct mount3_state *ms, dict_t *options)
{
        xlator_list_t   *volentry = NULL;
        int             ret = -1;

        if ((!ms) || (!options))
                return -1;

        __mnt3_init_volume_export (ms, options);
        __mnt3_init_dir_export (ms, options);
        volentry = ms->nfsx->children;
        while (volentry) {
                gf_log (GF_MNT, GF_LOG_TRACE, "Initing options for: %s",
                        volentry->xlator->name);
                ret = __mnt3_init_volume (ms, options, volentry->xlator);
                if (ret < 0) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Volume init failed");
                        goto err;
                }

                volentry = volentry->next;
        }

        ret = 0;
err:
        return ret;
}


struct mount3_state *
mnt3_init_state (xlator_t *nfsx)
{
        struct mount3_state     *ms = NULL;
        int                     ret = -1;

        if (!nfsx)
                return NULL;

        ms = GF_CALLOC (1, sizeof (*ms), gf_nfs_mt_mount3_state);
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation failed");
                return NULL;
        }

        ms->iobpool = nfsx->ctx->iobuf_pool;
        ms->nfsx = nfsx;
        INIT_LIST_HEAD (&ms->exportlist);
        ret = mnt3_init_options (ms, nfsx->options);
        if (ret < 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Options init failed");
                return NULL;
        }

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


