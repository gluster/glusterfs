/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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
#include "store.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/uio.h>


#define IPv4_ADDR_SIZE  32

/* Macro to typecast the parameter to struct sockaddr_in
 */
#define SA(addr) ((struct sockaddr_in*)(addr))

/* Macro will mask the ip address with netmask.
 */
#define MASKED_IP(ipv4addr, netmask)                    \
                (ntohl(SA(ipv4addr)->sin_addr.s_addr) & (netmask))

/* Macro will compare two IP address after applying the mask
 */
#define COMPARE_IPv4_ADDRS(ip1, ip2, netmask)           \
                ((MASKED_IP(ip1, netmask)) == (MASKED_IP(ip2, netmask)))

/* This macro will assist in freeing up entire link list
 * of host_auth_spec structure.
 */
#define FREE_HOSTSPEC(exp) do {                                 \
                struct host_auth_spec *host= exp->hostspec;     \
                while (NULL != host){                           \
                        struct host_auth_spec* temp = host;     \
                        host = host->next;                      \
                        if (NULL != temp->host_addr) {          \
                                GF_FREE (temp->host_addr);      \
                        }                                       \
                        GF_FREE (temp);                         \
                }                                               \
                exp->hostspec = NULL;                           \
        } while (0)

typedef ssize_t (*mnt3_serializer) (struct iovec outmsg, void *args);

extern void *
mount3udp_thread (void *argv);

static inline void
mnt3_export_free (struct mnt3_export *exp)
{
        if (!exp)
                return;

        if (exp->exptype == MNT3_EXPTYPE_DIR)
                FREE_HOSTSPEC (exp);
        GF_FREE (exp->expname);
        GF_FREE (exp);
}

/* Generic reply function for MOUNTv3 specific replies. */
int
mnt3svc_submit_reply (rpcsvc_request_t *req, void *arg, mnt3_serializer sfunc)
{
        struct iovec            outmsg  = {0, };
        struct iobuf            *iob    = NULL;
        struct mount3_state     *ms     = NULL;
        int                     ret     = -1;
        ssize_t                 msglen  = 0;
        struct iobref           *iobref = NULL;

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
        /* TODO: use 'xdrproc_t' instead of 'sfunc' to get the xdr-size */
        iob = iobuf_get (ms->iobpool);
        if (!iob) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to get iobuf");
                goto ret;
        }

        iobuf_to_iovec (iob, &outmsg);
        /* Use the given serializer to translate the give C structure in arg
         * to XDR format which will be written into the buffer in outmsg.
         */
        msglen = sfunc (outmsg, arg);
        if (msglen < 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to encode message");
                goto ret;
        }
        outmsg.iov_len = msglen;

        iobref = iobref_new ();
        if (iobref == NULL) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to get iobref");
                goto ret;
        }

        ret = iobref_add (iobref, iob);
        if (ret) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to add iob to iobref");
                goto ret;
        }

        /* Then, submit the message for transmission. */
        ret = rpcsvc_submit_message (req, &outmsg, 1, NULL, 0, iobref);
        if (ret == -1) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Reply submission failed");
                goto ret;
        }

        ret = 0;
ret:
        if (NULL != iob)
                iobuf_unref (iob);
        if (NULL != iobref)
                iobref_unref (iobref);

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

/* Read the rmtab from the store_handle and append (or not) the entries to the
 * mountlist.
 *
 * Requires the store_handle to be locked.
 */
static int
__mount_read_rmtab (gf_store_handle_t *sh, struct list_head *mountlist,
                    gf_boolean_t append)
{
        int                     ret = 0;
        unsigned int            idx = 0;
        struct mountentry       *me = NULL, *tmp = NULL;
                                /* me->hostname is a char[MNTPATHLEN] */
        char                    key[MNTPATHLEN + 11];

        GF_ASSERT (sh && mountlist);

        if (!gf_store_locked_local (sh)) {
                gf_log (GF_MNT, GF_LOG_WARNING, "Not reading unlocked %s",
                        sh->path);
                return -1;
        }

        if (!append) {
                list_for_each_entry_safe (me, tmp, mountlist, mlist) {
                        list_del (&me->mlist);
                        GF_FREE (me);
                }
                me = NULL;
        }

        for (;;) {
                char *value = NULL;

                if (me && append) {
                        /* do not add duplicates */
                        list_for_each_entry (tmp, mountlist, mlist) {
                                if (!strcmp(tmp->hostname, me->hostname) &&
                                    !strcmp(tmp->exname, me->exname)) {
                                        GF_FREE (me);
                                        goto dont_add;
                                }
                        }
                        list_add_tail (&me->mlist, mountlist);
                } else if (me) {
                        list_add_tail (&me->mlist, mountlist);
                }

dont_add:
                me = GF_CALLOC (1, sizeof (*me), gf_nfs_mt_mountentry);
                if (!me) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Out of memory");
                        ret = -1;
                        goto out;
                }

                INIT_LIST_HEAD (&me->mlist);

                snprintf (key, 9 + MNTPATHLEN, "hostname-%d", idx);
                ret = gf_store_retrieve_value (sh, key, &value);
                if (ret)
                        break;
                strncpy (me->hostname, value, MNTPATHLEN);
                GF_FREE (value);

                snprintf (key, 11 + MNTPATHLEN, "mountpoint-%d", idx);
                ret = gf_store_retrieve_value (sh, key, &value);
                if (ret)
                        break;
                strncpy (me->exname, value, MNTPATHLEN);
                GF_FREE (value);

                idx++;
                gf_log (GF_MNT, GF_LOG_TRACE, "Read entries %s:%s", me->hostname, me->exname);
        }
        gf_log (GF_MNT, GF_LOG_DEBUG, "Read %d entries from '%s'", idx, sh->path);
        GF_FREE (me);
out:
        return ret;
}

/* Overwrite the contents of the rwtab with te in-memory client list.
 * Fail gracefully if the stora_handle is not locked.
 */
static void
__mount_rewrite_rmtab(struct mount3_state *ms, gf_store_handle_t *sh)
{
        struct mountentry       *me = NULL;
        char                    key[16];
        int                     fd, ret;
        unsigned int            idx = 0;

        if (!gf_store_locked_local (sh)) {
                gf_log (GF_MNT, GF_LOG_WARNING, "Not modifying unlocked %s",
                        sh->path);
                return;
        }

        fd = gf_store_mkstemp (sh);
        if (fd == -1) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to open %s", sh->path);
                return;
        }

        list_for_each_entry (me, &ms->mountlist, mlist) {
                snprintf (key, 16, "hostname-%d", idx);
                ret = gf_store_save_value (fd, key, me->hostname);
                if (ret)
                        goto fail;

                snprintf (key, 16, "mountpoint-%d", idx);
                ret = gf_store_save_value (fd, key, me->exname);
                if (ret)
                        goto fail;

                idx++;
        }

        gf_log (GF_MNT, GF_LOG_DEBUG, "Updated rmtab with %d entries", idx);

        close (fd);
        if (gf_store_rename_tmppath (sh))
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to overwrite rwtab %s",
                        sh->path);

        return;

fail:
        gf_log (GF_MNT, GF_LOG_ERROR, "Failed to update %s", sh->path);
        close (fd);
        gf_store_unlink_tmppath (sh);
}

/* Read the rmtab into a clean ms->mountlist.
 */
static void
mount_read_rmtab (struct mount3_state *ms)
{
        gf_store_handle_t       *sh = NULL;
        struct nfs_state        *nfs = NULL;
        int                     ret;

        nfs = (struct nfs_state *)ms->nfsx->private;

        ret = gf_store_handle_new (nfs->rmtab, &sh);
        if (ret) {
                gf_log (GF_MNT, GF_LOG_WARNING, "Failed to open '%s'",
                        nfs->rmtab);
                return;
        }

        if (gf_store_lock (sh)) {
                gf_log (GF_MNT, GF_LOG_WARNING, "Failed to lock '%s'",
                        nfs->rmtab);
                goto out;
        }

        __mount_read_rmtab (sh, &ms->mountlist, _gf_false);
        gf_store_unlock (sh);

out:
        gf_store_handle_destroy (sh);
}

/* Write the ms->mountlist to the rmtab.
 *
 * The rmtab could be empty, or it can exists and have been updated by a
 * different storage server without our knowing.
 *
 * 1. takes the store_handle lock on the current rmtab
 *    - blocks if an other storage server rewrites the rmtab at the same time
 * 2. [if new_rmtab] takes the store_handle lock on the new rmtab
 * 3. reads/merges the entries from the current rmtab
 * 4. [if new_rmtab] reads/merges the entries from the new rmtab
 * 5. [if new_rmtab] writes the new rmtab
 * 6. [if not new_rmtab] writes the current rmtab
 * 7  [if new_rmtab] replaces nfs->rmtab to point to the new location
 * 8. [if new_rmtab] releases the store_handle lock of the new rmtab
 * 9. releases the store_handle lock of the old rmtab
 */
void
mount_rewrite_rmtab (struct mount3_state *ms, char *new_rmtab)
{
        gf_store_handle_t       *sh = NULL, *nsh = NULL;
        struct nfs_state        *nfs = NULL;
        int                     ret;
        char                    *rmtab = NULL;

        nfs = (struct nfs_state *)ms->nfsx->private;

        ret = gf_store_handle_new (nfs->rmtab, &sh);
        if (ret) {
                gf_log (GF_MNT, GF_LOG_WARNING, "Failed to open '%s'",
                        nfs->rmtab);
                return;
        }

        if (gf_store_lock (sh)) {
                gf_log (GF_MNT, GF_LOG_WARNING, "Not rewriting '%s'",
                        nfs->rmtab);
                goto free_sh;
        }

        if (new_rmtab) {
                ret = gf_store_handle_new (new_rmtab, &nsh);
                if (ret) {
                        gf_log (GF_MNT, GF_LOG_WARNING, "Failed to open '%s'",
                                new_rmtab);
                        goto unlock_sh;
                }

                if (gf_store_lock (nsh)) {
                        gf_log (GF_MNT, GF_LOG_WARNING, "Not rewriting '%s'",
                                new_rmtab);
                        goto free_nsh;
                }
        }

        /* always read the currently used rmtab */
        __mount_read_rmtab (sh, &ms->mountlist, _gf_true);

        if (new_rmtab) {
                /* read the new rmtab and write changes to the new location */
                __mount_read_rmtab (nsh, &ms->mountlist, _gf_true);
                __mount_rewrite_rmtab (ms, nsh);

                /* replace the nfs->rmtab reference to the new rmtab */
                rmtab = gf_strdup(new_rmtab);
                if (rmtab == NULL) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Out of memory, keeping "
                                "%s as rmtab", nfs->rmtab);
                } else {
                        GF_FREE (nfs->rmtab);
                        nfs->rmtab = rmtab;
                }

                gf_store_unlock (nsh);
        } else {
                /* rewrite the current (unchanged location) rmtab */
                __mount_rewrite_rmtab (ms, sh);
        }

free_nsh:
        if (new_rmtab)
                gf_store_handle_destroy (nsh);
unlock_sh:
        gf_store_unlock (sh);
free_sh:
        gf_store_handle_destroy (sh);
}

/* Add a new NFS-client to the ms->mountlist and update the rmtab if we can.
 *
 * A NFS-client will only be removed from the ms->mountlist in case the
 * NFS-client sends a unmount request. It is possible that a NFS-client
 * crashed/rebooted had network loss or something else prevented the NFS-client
 * to unmount cleanly. In this case, a duplicate entry would be added to the
 * ms->mountlist, which is wrong and we should prevent.
 *
 * It is fully acceptible that the ms->mountlist is not 100% correct, this is a
 * common issue for all(?) NFS-servers.
 */
int
mnt3svc_update_mountlist (struct mount3_state *ms, rpcsvc_request_t *req,
                          char  *expname)
{
        struct mountentry       *me = NULL;
        struct mountentry       *cur = NULL;
        int                     ret = -1;
        char                    *colon = NULL;
        struct nfs_state        *nfs = NULL;
        gf_store_handle_t       *sh = NULL;

        if ((!ms) || (!req) || (!expname))
                return -1;

        me = (struct mountentry *)GF_CALLOC (1, sizeof (*me),
                                             gf_nfs_mt_mountentry);
        if (!me)
                return -1;

        nfs = (struct nfs_state *)ms->nfsx->private;

        ret = gf_store_handle_new (nfs->rmtab, &sh);
        if (ret) {
                gf_log (GF_MNT, GF_LOG_WARNING, "Failed to open '%s'",
                        nfs->rmtab);
                goto free_err;
        }

        strncpy (me->exname, expname, MNTPATHLEN);

        INIT_LIST_HEAD (&me->mlist);
        /* Must get the IP or hostname of the client so we
         * can map it into the mount entry.
         */
        ret = rpcsvc_transport_peername (req->trans, me->hostname, MNTPATHLEN);
        if (ret == -1)
                goto free_err2;

        colon = strrchr (me->hostname, ':');
        if (colon) {
                *colon = '\0';
        }
        LOCK (&ms->mountlock);
        {
                /* in case locking fails, we just don't write the rmtab */
                if (gf_store_lock (sh)) {
                        gf_log (GF_MNT, GF_LOG_WARNING, "Failed to lock '%s'"
                                ", changes will not be written", nfs->rmtab);
                } else {
                        __mount_read_rmtab (sh, &ms->mountlist, _gf_false);
                }

                /* do not add duplicates */
                list_for_each_entry (cur, &ms->mountlist, mlist) {
                        if (!strcmp(cur->hostname, me->hostname) &&
                            !strcmp(cur->exname, me->exname)) {
                                GF_FREE (me);
                                goto dont_add;
                        }
                }
                list_add_tail (&me->mlist, &ms->mountlist);

                /* only write the rmtab in case it was locked */
                if (gf_store_locked_local (sh))
                        __mount_rewrite_rmtab (ms, sh);
        }
dont_add:
        if (gf_store_locked_local (sh))
                gf_store_unlock (sh);

        UNLOCK (&ms->mountlock);

free_err2:
        gf_store_handle_destroy (sh);

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

        LOCK (&ms->mountlock);
        list_for_each_entry (exp, &ms->exportlist, explist) {
                if (exp->vol == mntxl) {
                        uuid_copy (volumeid, exp->volumeid);
                        ret = 0;
                        goto out;
                }
        }

out:
        UNLOCK (&ms->mountlock);
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
        char                    fhstr[1024], *path = NULL;

        req = (rpcsvc_request_t *)frame->local;

        if (!req)
                return -1;

        mntxl = (xlator_t *)cookie;
        ms = (struct mount3_state *)rpcsvc_request_program_private (req);
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "mount state not found");
                op_ret = -1;
                op_errno = EINVAL;
        }

        if (op_ret == -1) {
                gf_log (GF_NFS, GF_LOG_ERROR, "error=%s", strerror (op_errno));
                status = mnt3svc_errno_to_mnterr (op_errno);
        }
        if (status != MNT3_OK)
                goto xmit_res;

        path = GF_CALLOC (PATH_MAX, sizeof (char), gf_nfs_mt_char);
        if (!path) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Out of memory");
                goto xmit_res;
        }

        snprintf (path, PATH_MAX, "/%s", mntxl->name);
        mnt3svc_update_mountlist (ms, req, path);
        GF_FREE (path);
        if (gf_nfs_dvm_off (nfs_state (ms->nfsx))) {
                fh = nfs3_fh_build_indexed_root_fh (ms->nfsx->children, mntxl);
                goto xmit_res;
        }

        __mnt3_get_volume_id (ms, mntxl, volumeid);
        fh = nfs3_fh_build_uuid_root_fh (volumeid);

xmit_res:
        nfs3_fh_to_str (&fh, fhstr, sizeof (fhstr));
        gf_log (GF_MNT, GF_LOG_DEBUG, "MNT reply: fh %s, status: %d", fhstr,
                status);
        if (op_ret == 0) {
                svc = rpcsvc_request_service (req);
                autharrlen = rpcsvc_auth_array (svc, mntxl->name, autharr,
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
        size_t  dlen;

        if ((!expname) || (!dirpath))
                return 0;

        /* Some clients send a dirpath for mount that includes the slash at the
         * end. String compare for searching the export will fail because our
         * exports list does not include that slash. Remove the slash to
         * compare.
         */
        dlen = strlen (dirpath);
        if (dlen && dirpath [dlen - 1] == '/')
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

        ret = nfs_inode_loc_fill (exportinode, &exportloc, NFS_RESOLVE_EXIST);
        if (ret < 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Loc fill failed for export inode"
                        ": gfid %s, volume: %s",
                        uuid_utoa (exportinode->gfid), xl->name);
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
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to get root inode");
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

        if (!volname)
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
static char *
setup_next_component (char *path, size_t plen, char *component, size_t clen)
{
        char    *comp = NULL;
        char    *nextcomp = NULL;

        if ((!path) || (!component))
                return NULL;

        strncpy (component, path, clen);
        comp = index (component, (int)'/');
        if (!comp)
                goto err;

        comp++;
        nextcomp = index (comp, (int)'/');
        if (nextcomp) {
                strncpy (path, nextcomp, plen);
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

        nextcomp = setup_next_component (mres->remainingdir,
                                         sizeof (mres->remainingdir),
                                         dupsubdir, sizeof (dupsubdir));
        if (!nextcomp)
                goto err;

        /* Wipe the contents of the previous component */
        uuid_copy (gfid, mres->resolveloc.inode->gfid);
        nfs_loc_wipe (&mres->resolveloc);
        ret = nfs_entry_loc_fill (mres->exp->vol->itable, gfid, nextcomp,
                                  &mres->resolveloc, NFS_RESOLVE_CREATE);
        if ((ret < 0) && (ret != -2)) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to resolve and create "
                        "inode: parent gfid %s, entry %s",
                        uuid_utoa (gfid), nextcomp);
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
        char                    *path = NULL;

        mres = frame->local;
        mntxl = (xlator_t *)cookie;
        if (op_ret == -1) {
                gf_log (GF_NFS, GF_LOG_ERROR, "path=%s (%s)",
                        mres->resolveloc.path, strerror (op_errno));
                mntstat = mnt3svc_errno_to_mnterr (op_errno);
                goto err;
        }

        inode_link (mres->resolveloc.inode, mres->resolveloc.parent,
                    mres->resolveloc.name, buf);

        nfs3_fh_build_child_fh (&mres->parentfh, buf, &fh);
        if (strlen (mres->remainingdir) <= 0) {
                op_ret = -1;
                mntstat = MNT3_OK;
                path = GF_CALLOC (PATH_MAX, sizeof (char), gf_nfs_mt_char);
                if (!path) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation "
                                "failed");
                        goto err;
                }
                snprintf (path, PATH_MAX, "/%s%s", mres->exp->vol->name,
                         mres->resolveloc.path);
                mnt3svc_update_mountlist (mres->mstate, mres->req,
                                          path);
                GF_FREE (path);
        } else {
                mres->parentfh = fh;
                op_ret = __mnt3_resolve_export_subdir_comp (mres);
                if (op_ret < 0)
                        mntstat = mnt3svc_errno_to_mnterr (-op_ret);
        }
err:
        if (op_ret == -1) {
                gf_log (GF_MNT, GF_LOG_DEBUG, "Mount reply status: %d",
                        mntstat);
                svc = rpcsvc_request_service (mres->req);
                autharrlen = rpcsvc_auth_array (svc, mntxl->name, autharr,
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

        firstcomp = setup_next_component (mres->remainingdir,
                                          sizeof (mres->remainingdir),
                                          dupsubdir, sizeof (dupsubdir));
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


/**
 * This function will verify if the client is allowed to mount
 * the directory or not. Client's IP address will be compared with
 * allowed IP list or range present in mnt3_export structure.
 *
 * @param req - RPC request. This structure contains client's IP address.
 * @param export - mnt3_export structure. Contains allowed IP list/range.
 *
 * @return 0 - on Success and -EACCES on failure.
 */
int
mnt3_verify_auth (rpcsvc_request_t *req, struct mnt3_export *export)
{
        int                     retvalue = -EACCES;
        int                     ret = 0;
        int                     shiftbits = 0;
        uint32_t                ipv4netmask = 0;
        uint32_t                routingprefix = 0;
        struct host_auth_spec   *host = NULL;
        struct sockaddr_in      *client_addr = NULL;
        struct sockaddr_in      *allowed_addr = NULL;
        struct addrinfo         *allowed_addrinfo = NULL;

        /* Sanity check */
        if ((NULL == req) ||
            (NULL == req->trans) ||
            (NULL == export) ||
            (NULL == export->hostspec)) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Invalid argument");
                return retvalue;
        }

        host = export->hostspec;


        /* Client's IP address. */
        client_addr = (struct sockaddr_in *)(&(req->trans->peerinfo.sockaddr));

        /* Try to see if the client IP matches the allowed IP list.*/
        while (NULL != host){
                GF_ASSERT (host->host_addr);

                if (NULL != allowed_addrinfo) {
                        freeaddrinfo (allowed_addrinfo);
                        allowed_addrinfo = NULL;
                }

                /* Get the addrinfo for the allowed host (host_addr). */
                ret = getaddrinfo (host->host_addr,
                                NULL,
                                NULL,
                                &allowed_addrinfo);
                if (0 != ret){
                        gf_log (GF_MNT, GF_LOG_ERROR, "getaddrinfo: %s\n",
                                gai_strerror (ret));
                        host = host->next;

                        /* Failed to get IP addrinfo. Continue to check other
                         * allowed IPs in the list.
                         */
                        continue;
                }

                allowed_addr = (struct sockaddr_in *)(allowed_addrinfo->ai_addr);

                if (NULL == allowed_addr) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Invalid structure");
                        break;
                }

                if (AF_INET == allowed_addr->sin_family){
                        if (IPv4_ADDR_SIZE < host->routeprefix) {
                                gf_log (GF_MNT, GF_LOG_ERROR, "invalid IP "
                                        "configured for export-dir AUTH");
                                host = host->next;
                                continue;
                        }

                        /* -1 means no route prefix is provided. In this case
                         * the IP should be an exact match. Which is same as
                         * providing a route prefix of IPv4_ADDR_SIZE.
                         */
                        if (-1 == host->routeprefix) {
                                routingprefix = IPv4_ADDR_SIZE;
                        } else {
                                routingprefix = host->routeprefix;
                        }

                        /* Create a mask from the routing prefix. User provided
                         * CIDR address is split into IP address (host_addr) and
                         * routing prefix (routeprefix). This CIDR address may
                         * denote a single, distinct interface address or the
                         * beginning address of an entire network.
                         *
                         * e.g. the IPv4 block 192.168.100.0/24 represents the
                         * 256 IPv4 addresses from 192.168.100.0 to
                         * 192.168.100.255.
                         * Therefore to check if an IP matches 192.168.100.0/24
                         * we should mask the IP with FFFFFF00 and compare it
                         * with host address part of CIDR.
                         */
                        shiftbits = IPv4_ADDR_SIZE - routingprefix;
                        ipv4netmask = 0xFFFFFFFFUL << shiftbits;

                        /* Mask both the IPs and then check if they match
                         * or not. */
                        if (COMPARE_IPv4_ADDRS (allowed_addr,
                                                client_addr,
                                                ipv4netmask)){
                                retvalue = 0;
                                break;
                        }
                }

                /* Client IP didn't match the allowed IP.
                 * Check with the next allowed IP.*/
               host = host->next;
        }

        if (NULL != allowed_addrinfo) {
               freeaddrinfo (allowed_addrinfo);
        }

        return retvalue;
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

        /* Need to check AUTH */
        if (NULL != exp->hostspec) {
                ret = mnt3_verify_auth (req, exp);
                if (0 != ret) {
                        gf_log (GF_MNT,GF_LOG_ERROR,
                                        "AUTH verification failed");
                        return ret;
                }
        }

        mres = GF_CALLOC (1, sizeof (mnt3_resolve_t), gf_nfs_mt_mnt3_resolve);
        if (!mres) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation failed");
                goto err;
        }

        mres->exp = exp;
        mres->mstate = ms;
        mres->req = req;
        strncpy (mres->remainingdir, subdir, MNTPATHLEN);
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

        ret = mnt3_resolve_subdir (req, ms, exp, volume_subdir);
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

        LOCK (&ms->mountlock);
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
        UNLOCK (&ms->mountlock);
        return found;
}


int
mnt3_check_client_net (struct mount3_state *ms, rpcsvc_request_t *req,
                       xlator_t *targetxl)
{

        rpcsvc_t                *svc = NULL;
        rpc_transport_t         *trans = NULL;
        struct sockaddr_storage sastorage = {0,};
        char                    peer[RPCSVC_PEER_STRLEN] = {0,};
        int                     ret = -1;

        if ((!ms) || (!req) || (!targetxl))
                return -1;

        svc = rpcsvc_request_service (req);

        trans = rpcsvc_request_transport (req);
        ret = rpcsvc_transport_peeraddr (trans, peer, RPCSVC_PEER_STRLEN,
                                         &sastorage, sizeof (sastorage));
        if (ret != 0) {
                gf_log (GF_MNT, GF_LOG_WARNING, "Failed to get peer addr: %s",
                        gai_strerror (ret));
        }

        ret = rpcsvc_auth_check (svc, targetxl->name, trans);
        if (ret == RPCSVC_AUTH_REJECT) {
                gf_log (GF_MNT, GF_LOG_INFO, "Peer %s  not allowed", peer);
                goto err;
        }

        ret = rpcsvc_transport_privport_check (svc, targetxl->name,
                                               rpcsvc_request_transport (req));
        if (ret == RPCSVC_AUTH_REJECT) {
                gf_log (GF_MNT, GF_LOG_INFO, "Peer %s rejected. Unprivileged "
                        "port not allowed", peer);
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
        int                     ret = -ENOENT;
        struct nfs_state        *nfs = NULL;

        if ((!ms) || (!subdir))
                return -1;

        volname_ptr = volname;
        subdir = __volume_subdir (subdir, &volname_ptr);
        if (!subdir)
                goto err;

        exp = mnt3_mntpath_to_export (ms, volname);
        if (!exp)
                goto err;

        nfs = (struct nfs_state *)ms->nfsx->private;
        if (!nfs)
                goto err;

        if (!nfs_subvolume_started (nfs, exp->vol)) {
                gf_log (GF_MNT, GF_LOG_DEBUG,
                        "Volume %s not started", exp->vol->name);
                goto err;
        }

        if (mnt3_check_client_net (ms, req, exp->vol) == RPCSVC_AUTH_REJECT) {
                gf_log (GF_MNT, GF_LOG_DEBUG, "Client mount not allowed");
                ret = -EACCES;
                goto err;
        }

        ret = mnt3_resolve_subdir (req, ms, exp, subdir);
        if (ret < 0) {
                gf_log (GF_MNT, GF_LOG_ERROR,
                        "Failed to resolve export dir: %s", subdir);
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

        if ((!req) || (!path) || (!e))
                return -1;

        ms = (struct mount3_state *) rpcsvc_request_program_private (req);
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Mount state not present");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                goto err;
        }

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
        ret = xdr_to_mountpath (pvec, req->msg[0]);
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

        nfs = (struct nfs_state *)ms->nfsx->private;
        gf_log (GF_MNT, GF_LOG_DEBUG, "dirpath: %s", path);
        ret = mnt3_find_export (req, path, &exp);
        if (ret < 0) {
                mntstat = mnt3svc_errno_to_mnterr (-ret);
                goto mnterr;
        } else if (!exp) {
                /*
                 * SPECIAL CASE: exp is NULL if "path" is subdir in
                 * call to mnt3_find_export().
                 *
                 * This is subdir mount, we are already DONE!
                 * nfs_subvolume_started() and mnt3_check_client_net()
                 * validation are done in mnt3_parse_dir_exports()
                 * which is invoked through mnt3_find_export().
                 *
                 * TODO: All mount should happen thorugh mnt3svc_mount()
                 *       It needs more clean up.
                 */
                return (0);
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
        rpcsvc_submit_generic (req, &dummyvec, 1,  NULL, 0, NULL);
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

        /* read rmtab, other peers might have updated it */
        mount_read_rmtab(ms);

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
                if (!first)
                        first = mlist;

                mlist->ml_directory = GF_CALLOC (namelen + 2, sizeof (char),
                                                 gf_nfs_mt_char);
                if (!mlist->ml_directory) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation"
                                " failed");
                        goto free_list;
                }

                strcpy (mlist->ml_directory, me->exname);

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
        arg = &mlist;

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
mnt3svc_umount (struct mount3_state *ms, char *dirpath, char *hostname)
{
        struct mountentry       *me = NULL;
        int                     ret = -1;
        gf_store_handle_t       *sh = NULL;
        struct nfs_state        *nfs = NULL;

        if ((!ms) || (!dirpath) || (!hostname))
                return -1;

        nfs = (struct nfs_state *)ms->nfsx->private;

        ret = gf_store_handle_new (nfs->rmtab, &sh);
        if (ret) {
                gf_log (GF_MNT, GF_LOG_WARNING, "Failed to open '%s'",
                        nfs->rmtab);
                return 0;
        }

        ret = gf_store_lock (sh);
        if (ret) {
                goto out_free;
        }

        LOCK (&ms->mountlock);
        {
                __mount_read_rmtab (sh, &ms->mountlist, _gf_false);
                if (list_empty (&ms->mountlist)) {
                        ret = 0;
                        goto out_unlock;
                }

                ret = -1;
                list_for_each_entry (me, &ms->mountlist, mlist) {
                       if ((strcmp (me->exname, dirpath) == 0) &&
                            (strcmp (me->hostname, hostname) == 0)) {
                               ret = 0;
                               break;
                       }
                }

                /* Need this check here because at the end of the search me
                 * might still be pointing to the last entry, which may not be
                 * the one we're looking for.
                 */
                if (ret == -1)  {/* Not found in list. */
                        gf_log (GF_MNT, GF_LOG_TRACE, "Export not found");
                        goto out_unlock;
                }

                if (!me)
                        goto out_unlock;

                gf_log (GF_MNT, GF_LOG_DEBUG, "Unmounting: dir %s, host: %s",
                        me->exname, me->hostname);

                list_del (&me->mlist);
                GF_FREE (me);
                __mount_rewrite_rmtab (ms, sh);
        }
out_unlock:
        UNLOCK (&ms->mountlock);
        gf_store_unlock (sh);
out_free:
        gf_store_handle_destroy (sh);
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
        char                    *colon = NULL;

        if (!req)
                return -1;

        /* Remove the mount point from the exports list. */
        pvec.iov_base = dirpath;
        pvec.iov_len = MNTPATHLEN;
        ret = xdr_to_mountpath (pvec, req->msg[0]);
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

        ret = rpcsvc_transport_peername (req->trans, hostname, MNTPATHLEN);
        if (ret != 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to get remote name: %s",
                        gai_strerror (ret));
                goto rpcerr;
        }

        colon = strrchr (hostname, ':');
        if (colon) {
                *colon= '\0';
        }
        gf_log (GF_MNT, GF_LOG_DEBUG, "dirpath: %s, hostname: %s", dirpath,
                hostname);
        ret = mnt3svc_umount (ms, dirpath, hostname);

        if (ret == -1) {
                ret = 0;
                mstat = MNT3ERR_NOENT;
        }
        /* FIXME: also take care of the corner case where the
         * client was resolvable at mount but not at the umount - vice-versa.
         */
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

        ms = (struct mount3_state *)rpcsvc_request_program_private (req);
        if (!ms) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Mount state not present");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
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
        if (!nfs)
                return NULL;

        LOCK (&ms->mountlock);
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
                 if (!first)
                         first = elist;
                elist->ex_dir = GF_CALLOC (namelen + 2, sizeof (char),
                                           gf_nfs_mt_char);
                if (!elist->ex_dir) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation"
                                " failed");
                        goto free_list;
                }
                strcpy (elist->ex_dir, ent->expname);

                addrstr = rpcsvc_volume_allowed (svc->options,
                                                 ent->vol->name);
                elist->ex_groups = GF_CALLOC (1, sizeof (struct groupnode),
                                              gf_nfs_mt_groupnode);
                if (!elist->ex_groups) {
                        gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation"
                                " failed");
                        goto free_list;
                }
                /*This check has to be done after checking
                 * elist->ex_groups allocation check to avoid resource leak;
                */
                if (addrstr)
                        addrstr = gf_strdup (addrstr);
                else
                        addrstr = gf_strdup ("No Access");

                if (!addrstr) {
                        goto free_list;
                }
                elist->ex_groups->gr_name = addrstr;
                if (prev) {
                        prev->ex_next = elist;
                        prev = elist;
                } else
                        prev = elist;
        }

        ret = 0;

free_list:
        UNLOCK (&ms->mountlock);
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

/* just declaring, definition is way down below */
rpcsvc_program_t        mnt3prog;

/* nfs3_rootfh used by mount3udp thread needs to access mount3prog.private
 * directly as we don't have nfs xlator pointer to dereference it. But thats OK
 */

struct nfs3_fh *
nfs3_rootfh (char* path)
{
        struct mount3_state     *ms = NULL;
        struct nfs3_fh          *fh = NULL;
        struct mnt3_export      *exp = NULL;
        inode_t                 *inode = NULL;
        char                    *tmp = NULL;

        ms = mnt3prog.private;
        exp = mnt3_mntpath_to_export (ms, path);
        if (exp == NULL)
                goto err;

        tmp = (char *)path;
        tmp = strchr (tmp, '/');
        if (tmp == NULL)
                tmp = "/";

        inode = inode_from_path (exp->vol->itable, tmp);
        if (inode == NULL)
                goto err;

        fh = GF_CALLOC (1, sizeof(*fh), gf_nfs_mt_nfs3_fh);
        if (fh == NULL)
                goto err;
        nfs3_build_fh (inode, exp->volumeid, fh);

err:
        if (inode)
                inode_unref (inode);
        return fh;
}

int
mount3udp_add_mountlist (char *host, dirpath *expname)
{
        struct mountentry       *me = NULL;
        struct mount3_state     *ms = NULL;
        char                    *export = NULL;

        ms = mnt3prog.private;
        me = GF_CALLOC (1, sizeof (*me), gf_nfs_mt_mountentry);
        if (!me)
                return -1;
        export = (char *)expname;
        while (*export == '/')
                export++;

        strncpy (me->exname, export, MNTPATHLEN);
        strncpy (me->hostname, host, MNTPATHLEN);
        INIT_LIST_HEAD (&me->mlist);
        LOCK (&ms->mountlock);
        {
                list_add_tail (&me->mlist, &ms->mountlist);
                mount_rewrite_rmtab(ms, NULL);
        }
        UNLOCK (&ms->mountlock);
        return 0;
}

int
mount3udp_delete_mountlist (char *hostname, dirpath *expname)
{
        struct mount3_state     *ms = NULL;
        char                    *export = NULL;

        ms = mnt3prog.private;
        export = (char *)expname;
        while (*export == '/')
                export++;
        mnt3svc_umount (ms, export, hostname);
        return 0;
}

/**
 * This function will parse the hostip (IP addres, IP range, or hostname)
 * and fill the host_auth_spec structure.
 *
 * @param hostspec - struct host_auth_spec
 * @param hostip   - IP address, IP range (CIDR format) or hostname
 *
 * @return 0 - on success and -1 on failure
 */
int
mnt3_export_fill_hostspec (struct host_auth_spec* hostspec, const char* hostip)
{
        char *ipdupstr = NULL;
        char *savptr = NULL;
        char *ip = NULL;
        char *token = NULL;
        int  ret = -1;

        /* Create copy of the string so that the source won't change
         */
        ipdupstr = gf_strdup (hostip);
        if (NULL == ipdupstr) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation failed");
                goto err;
        }

        ip = strtok_r (ipdupstr, "/", &savptr);
        hostspec->host_addr = gf_strdup (ip);
        if (NULL == hostspec->host_addr) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation failed");
                goto err;
        }

        /* Check if the IP is in <IP address> / <Range> format.
         * If yes, then strip the range and store it separately.
         */
        token = strtok_r (NULL, "/", &savptr);

        if (NULL == token) {
              hostspec->routeprefix = -1;
        } else {
              hostspec->routeprefix = atoi (token);
        }

        // success
        ret = 0;
err:
        if (NULL != ipdupstr) {
                GF_FREE (ipdupstr);
        }
        return ret;
}


/**
 * This function will parse the AUTH parameter passed along with
 * "export-dir" option. If AUTH parameter is present then it will be
 * stripped from exportpath and stored in mnt3_export (exp) structure.
 *
 * @param exp - mnt3_export structure. Holds information needed for mount.
 * @param exportpath - Value of "export-dir" key. Holds both export path
 *                     and AUTH parameter for the path.
 *                     exportpath format: <abspath>[(hostdesc[|hostspec|...])]
 *
 * @return This function will return 0 on success and -1 on failure.
 */
int
mnt3_export_parse_auth_param (struct mnt3_export* exp, char* exportpath)
{
        char *token = NULL;
        char *savPtr = NULL;
        char *hostip = NULL;
        struct host_auth_spec *host = NULL;
        int ret = 0;

        /* Using exportpath directly in strtok_r because we want
         * to strip off AUTH parameter from exportpath. */
        token = strtok_r (exportpath, "(", &savPtr);

        /* Get the next token, which will be the AUTH parameter. */
        token = strtok_r (NULL, ")", &savPtr);

        if (NULL == token) {
                /* If AUTH is not present then we should return success. */
                return 0;
        }

        /* Free any previously allocated hostspec structure. */
        if (NULL != exp->hostspec) {
                GF_FREE (exp->hostspec);
                exp->hostspec = NULL;
        }

        exp->hostspec = GF_CALLOC (1,
                        sizeof (*(exp->hostspec)),
                        gf_nfs_mt_auth_spec);
        if (NULL == exp->hostspec){
                gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation failed");
                return -1;
        }

        /* AUTH parameter can have multiple entries. For each entry
         * a host_auth_spec structure is created. */
        host = exp->hostspec;

        hostip = strtok_r (token, "|", &savPtr);

        /* Parse all AUTH parameters separated by '|' */
        while (NULL != hostip){
                ret = mnt3_export_fill_hostspec (host, hostip);
                if (0 != ret) {
                        gf_log(GF_MNT, GF_LOG_WARNING,
                                        "Failed to parse hostspec: %s", hostip);
                        goto err;
                }

                hostip = strtok_r (NULL, "|", &savPtr);
                if (NULL == hostip) {
                        break;
                }

                host->next = GF_CALLOC (1, sizeof (*(host)),
                                gf_nfs_mt_auth_spec);
                if (NULL == host->next){
                        gf_log (GF_MNT,GF_LOG_ERROR,
                                        "Memory allocation failed");
                        goto err;
                }
                host = host->next;
        }

        /* In case of success return from here */
        return 0;
err:
        /* In case of failure free up hostspec structure.  */
        FREE_HOSTSPEC (exp);

        return -1;
}

/**
 * exportpath will also have AUTH options (ip address, subnet address or
 * hostname) mentioned.
 * exportpath format: <abspath>[(hostdesc[|hostspec|...])]
 */
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

        if (NULL != exportpath) {
                /* If exportpath is not NULL then we should check if AUTH
                 * parameter is present or not. If AUTH parameter is present
                 * then it will be stripped and stored in mnt3_export (exp)
                 * structure.
                 */
                if (0 != mnt3_export_parse_auth_param (exp, exportpath)){
                        gf_log (GF_MNT, GF_LOG_ERROR,
                                                "Failed to parse auth param");
                        goto err;
                }
        }


        INIT_LIST_HEAD (&exp->explist);
        if (exportpath)
                alloclen = strlen (xl->name) + 2 + strlen (exportpath);
        else
                alloclen = strlen (xl->name) + 2;

        exp->expname = GF_CALLOC (alloclen, sizeof (char), gf_nfs_mt_char);
        if (!exp->expname) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Memory allocation failed");
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
        if (ret < 0) {
                gf_log (xl->name, GF_LOG_ERROR,
                        "Failed to set the export name");
                goto err;
        }
        /* Just copy without discrimination, we'll determine whether to
         * actually use it when a mount request comes in and a file handle
         * needs to be built.
         */
        uuid_copy (exp->volumeid, volumeid);
        exp->vol = xl;

        /* On success we should return from here*/
        return exp;
err:
        /* On failure free exp and it's members.*/
        if (NULL != exp) {
                mnt3_export_free (exp);
                exp = NULL;
        }

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

        ret = gf_string2boolean (optstr, &boolt);
        if (ret < 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to convert"
                        " string to boolean");
        }

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

        ret = gf_string2boolean (optstr, &boolt);
        if (ret < 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Failed to convert"
                        " string to boolean");
         }

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

int
mount_init_state (xlator_t *nfsx)
{
        int              ret = -1;
        struct nfs_state *nfs = NULL;

        if (!nfsx)
                goto out;

        nfs = (struct nfs_state *)nfs_state (nfsx);
        /*Maintaining global state for MOUNT1 and MOUNT3*/
        nfs->mstate =  mnt3_init_state (nfsx);
        if (!nfs->mstate) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to allocate"
                        "mount state");
                goto out;
        }
        ret = 0;
out:
        return ret;
}

rpcsvc_actor_t  mnt3svc_actors[MOUNT3_PROC_COUNT] = {
        {"NULL",    MOUNT3_NULL,    mnt3svc_null,    NULL, 0, DRC_NA},
        {"MNT",     MOUNT3_MNT,     mnt3svc_mnt,     NULL, 0, DRC_NA},
        {"DUMP",    MOUNT3_DUMP,    mnt3svc_dump,    NULL, 0, DRC_NA},
        {"UMNT",    MOUNT3_UMNT,    mnt3svc_umnt,    NULL, 0, DRC_NA},
        {"UMNTALL", MOUNT3_UMNTALL, mnt3svc_umntall, NULL, 0, DRC_NA},
        {"EXPORT",  MOUNT3_EXPORT,  mnt3svc_export,  NULL, 0, DRC_NA}
};



/* Static init parts are assigned here, dynamic ones are done in
 * mnt3svc_init and mnt3_init_state.
 * Making MOUNT3 a synctask so that the blocking DNS calls during rpc auth
 * gets offloaded to syncenv, keeping the main/poll thread unblocked
 */
rpcsvc_program_t        mnt3prog = {
                        .progname       = "MOUNT3",
                        .prognum        = MOUNT_PROGRAM,
                        .progver        = MOUNT_V3,
                        .progport       = GF_MOUNTV3_PORT,
                        .actors         = mnt3svc_actors,
                        .numactors      = MOUNT3_PROC_COUNT,
                        .min_auth       = AUTH_NULL,
                        .synctask       = _gf_true,
};



rpcsvc_program_t *
mnt3svc_init (xlator_t *nfsx)
{
        struct mount3_state     *mstate = NULL;
        struct nfs_state        *nfs = NULL;
        dict_t                  *options = NULL;
        char                    *portstr = NULL;
        int                      ret = -1;
        pthread_t                udp_thread;

        if (!nfsx || !nfsx->private)
                return NULL;

        nfs = (struct nfs_state *)nfsx->private;

        gf_log (GF_MNT, GF_LOG_DEBUG, "Initing Mount v3 state");
        mstate = (struct mount3_state *)nfs->mstate;
        if (!mstate) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Mount v3 state init failed");
                goto err;
        }

        mnt3prog.private = mstate;
        options = dict_new ();

        ret = gf_asprintf (&portstr, "%d", GF_MOUNTV3_PORT);
        if (ret == -1)
                goto err;

        ret = dict_set_dynstr (options, "transport.socket.listen-port", portstr);
        if (ret == -1)
                goto err;
        ret = dict_set_str (options, "transport-type", "socket");
        if (ret == -1) {
                gf_log (GF_NFS, GF_LOG_ERROR, "dict_set_str error");
                goto err;
        }

        if (nfs->allow_insecure) {
                ret = dict_set_str (options, "rpc-auth-allow-insecure", "on");
                if (ret == -1) {
                        gf_log (GF_NFS, GF_LOG_ERROR, "dict_set_str error");
                        goto err;
                }
                ret = dict_set_str (options, "rpc-auth.ports.insecure", "on");
                if (ret == -1) {
                        gf_log (GF_NFS, GF_LOG_ERROR, "dict_set_str error");
                        goto err;
                }
        }

        ret= rpcsvc_create_listeners (nfs->rpcsvc, options, nfsx->name);
        if (ret == -1) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Unable to create listeners");
                dict_unref (options);
                goto err;
        }

        if (nfs->mount_udp) {
                pthread_create (&udp_thread, NULL, mount3udp_thread, NULL);
        }
        return &mnt3prog;
err:
        return NULL;
}


rpcsvc_actor_t  mnt1svc_actors[MOUNT1_PROC_COUNT] = {
        {"NULL",    MOUNT1_NULL,    mnt3svc_null,   NULL, 0, DRC_NA},
        {"MNT",     MOUNT1_MNT,     NULL,           NULL, 0, DRC_NA },
        {"DUMP",    MOUNT1_DUMP,    mnt3svc_dump,   NULL, 0, DRC_NA},
        {"UMNT",    MOUNT1_UMNT,    mnt3svc_umnt,   NULL, 0, DRC_NA},
        {"UMNTALL", MOUNT1_UMNTALL, NULL,           NULL, 0, DRC_NA},
        {"EXPORT",  MOUNT1_EXPORT,  mnt3svc_export, NULL, 0, DRC_NA}
};

rpcsvc_program_t        mnt1prog = {
                        .progname       = "MOUNT1",
                        .prognum        = MOUNT_PROGRAM,
                        .progver        = MOUNT_V1,
                        .progport       = GF_MOUNTV1_PORT,
                        .actors         = mnt1svc_actors,
                        .numactors      = MOUNT1_PROC_COUNT,
                        .min_auth       = AUTH_NULL,
                        .synctask       = _gf_true,
};


rpcsvc_program_t *
mnt1svc_init (xlator_t *nfsx)
{
        struct mount3_state     *mstate = NULL;
        struct nfs_state        *nfs = NULL;
        dict_t                  *options = NULL;
        char                    *portstr = NULL;
        int                      ret = -1;

        if (!nfsx || !nfsx->private)
                return NULL;

        nfs = (struct nfs_state *)nfsx->private;

        gf_log (GF_MNT, GF_LOG_DEBUG, "Initing Mount v1 state");
        mstate = (struct mount3_state *)nfs->mstate;
        if (!mstate) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Mount v3 state init failed");
                goto err;
        }

        mnt1prog.private = mstate;

        options = dict_new ();

        ret = gf_asprintf (&portstr, "%d", GF_MOUNTV1_PORT);
        if (ret == -1)
                goto err;

        ret = dict_set_dynstr (options, "transport.socket.listen-port", portstr);
        if (ret == -1)
                goto err;
        ret = dict_set_str (options, "transport-type", "socket");
        if (ret == -1) {
                gf_log (GF_NFS, GF_LOG_ERROR, "dict_set_str error");
                goto err;
        }

        if (nfs->allow_insecure) {
                ret = dict_set_str (options, "rpc-auth-allow-insecure", "on");
                if (ret == -1) {
                        gf_log (GF_NFS, GF_LOG_ERROR, "dict_set_str error");
                        goto err;
                }
                ret = dict_set_str (options, "rpc-auth.ports.insecure", "on");
                if (ret == -1) {
                        gf_log (GF_NFS, GF_LOG_ERROR, "dict_set_str error");
                        goto err;
                }
        }

        ret = rpcsvc_create_listeners (nfs->rpcsvc, options, nfsx->name);
        if (ret == -1) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Unable to create listeners");
                dict_unref (options);
                goto err;
        }

        return &mnt1prog;
err:
        return NULL;
}

int
mount_reconfigure_state (xlator_t *nfsx, dict_t *options)
{
        int                    ret      = -1;
        struct nfs_state       *nfs     = NULL;
        struct mount3_state    *ms      = NULL;
        struct mnt3_export     *exp     = NULL;
        struct mnt3_export     *texp  = NULL;

        if ((!nfsx) || (!options))
                return (-1);

        nfs = (struct nfs_state *)nfs_state (nfsx);
        if (!nfs)
                return (-1);

        ms = nfs->mstate;
        if (!ms)
                return (-1);

        /*
         * Free() up the old export list. mnt3_init_options() will
         * rebuild the export list from scratch. Do it with locking
         * to avoid unnecessary race conditions.
         */
        LOCK (&ms->mountlock);
        list_for_each_entry_safe (exp, texp, &ms->exportlist, explist) {
                list_del (&exp->explist);
                mnt3_export_free (exp);
        }
        ret = mnt3_init_options (ms, options);
        UNLOCK (&ms->mountlock);

        if (ret < 0) {
                gf_log (GF_MNT, GF_LOG_ERROR, "Options reconfigure failed");
                return (-1);
        }

        return (0);
}
