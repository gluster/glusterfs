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
#include "glfs-internal.h"
#include "glfs.h"
#include "mount3-auth.h"
#include "hashfn.h"
#include "nfs-messages.h"

#include <errno.h>
#include <sys/socket.h>
#include <sys/uio.h>


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

/* Paths for export and netgroup files */
const char *exports_file_path   = GLUSTERD_DEFAULT_WORKDIR "/nfs/exports";
const char *netgroups_file_path = GLUSTERD_DEFAULT_WORKDIR "/nfs/netgroups";

typedef ssize_t (*mnt3_serializer) (struct iovec outmsg, void *args);

extern void *
mount3udp_thread (void *argv);

static void
mnt3_export_free (struct mnt3_export *exp)
{
        if (!exp)
                return;

        if (exp->exptype == MNT3_EXPTYPE_DIR)
                FREE_HOSTSPEC (exp);
        GF_FREE (exp->expname);
        GF_FREE (exp->fullpath);
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
                gf_msg (GF_MNT, GF_LOG_ERROR, EINVAL,
                        NFS_MSG_MNT_STATE_NOT_FOUND, "mount state not found");
                goto ret;
        }

        /* First, get the io buffer into which the reply in arg will
         * be serialized.
         */
        /* TODO: use 'xdrproc_t' instead of 'sfunc' to get the xdr-size */
        iob = iobuf_get (ms->iobpool);
        if (!iob) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to get iobuf");
                goto ret;
        }

        iobuf_to_iovec (iob, &outmsg);
        /* Use the given serializer to translate the give C structure in arg
         * to XDR format which will be written into the buffer in outmsg.
         */
        msglen = sfunc (outmsg, arg);
        if (msglen < 0) {
                gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_ENCODE_MSG_FAIL,
                        "Failed to encode message");
                goto ret;
        }
        outmsg.iov_len = msglen;

        iobref = iobref_new ();
        if (iobref == NULL) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to get iobref");
                goto ret;
        }

        ret = iobref_add (iobref, iob);
        if (ret) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to add iob to iobref");
                goto ret;
        }

        /* Then, submit the message for transmission. */
        ret = rpcsvc_submit_message (req, &outmsg, 1, NULL, 0, iobref);
        if (ret == -1) {
                gf_msg (GF_MNT, GF_LOG_ERROR, errno, NFS_MSG_REP_SUBMIT_FAIL,
                        "Reply submission failed");
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
/**
 * __mountdict_insert -- Insert a mount entry into the mount state
 *
 * @ms: The mount state holding the entries
 * @me: The mount entry to insert
 *
 * Not for external use.
 */
void
__mountdict_insert (struct mount3_state *ms, struct mountentry *me)
{
        char   *exname = NULL;
        char   *fpath  = NULL;
        data_t *medata = NULL;

        GF_VALIDATE_OR_GOTO (GF_MNT, ms, out);
        GF_VALIDATE_OR_GOTO (GF_MNT, me, out);

        /* We don't want export names with leading slashes */
        exname = me->exname;
        while (exname[0] == '/')
                exname++;

        /* Get the fullpath for the export */
        fpath = me->fullpath;
        if (me->has_full_path) {
                while (fpath[0] == '/')
                        fpath++;

                /* Export names can either be just volumes or paths inside that
                 * volume. */
                exname = fpath;
        }

        snprintf (me->hashkey, sizeof (me->hashkey), "%s:%s", exname,
                  me->hostname);

        medata = bin_to_data (me, sizeof (*me));
        dict_set (ms->mountdict, me->hashkey, medata);
        gf_msg_trace (GF_MNT, 0, "Inserted into mountdict: %s", me->hashkey);
out:
        return;
}

/**
 * __mountdict_remove -- Remove a mount entry from the mountstate.
 *
 * @ms: The mount state holding the entries
 * @me: The mount entry to remove
 *
 * Not for external use.
 */
void
__mountdict_remove (struct mount3_state *ms, struct mountentry *me)
{
        dict_del (ms->mountdict, me->hashkey);
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

        if (fh)
                fhlen = nfs3_fh_compute_size ();

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
                gf_msg (GF_MNT, GF_LOG_WARNING, 0, NFS_MSG_READ_LOCKED,
                        "Not reading unlocked %s", sh->path);
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
                        gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM,
                                NFS_MSG_NO_MEMORY, "Out of memory");
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
                gf_msg_trace (GF_MNT, 0, "Read entries %s:%s",
                              me->hostname, me->exname);
        }
        gf_msg_debug (GF_MNT, 0, "Read %d entries from '%s'", idx, sh->path);
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
                gf_msg (GF_MNT, GF_LOG_WARNING, 0, NFS_MSG_MODIFY_LOCKED,
                        "Not modifying unlocked %s", sh->path);
                return;
        }

        fd = gf_store_mkstemp (sh);
        if (fd == -1) {
                gf_msg (GF_MNT, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Failed to open %s", sh->path);
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

        gf_msg_debug (GF_MNT, 0, "Updated rmtab with %d entries", idx);

        if (gf_store_rename_tmppath (sh))
                gf_msg (GF_MNT, GF_LOG_ERROR, errno,
                        NFS_MSG_RWTAB_OVERWRITE_FAIL,
                        "Failed to overwrite rwtab %s", sh->path);

        return;

fail:
        gf_msg (GF_MNT, GF_LOG_ERROR, errno, NFS_MSG_UPDATE_FAIL,
                "Failed to update %s", sh->path);
        gf_store_unlink_tmppath (sh);
}

static gf_boolean_t
mount_open_rmtab (const char *rmtab, gf_store_handle_t **sh)
{
        int ret = -1;

        /* updating the rmtab is disabled, use in-memory only */
        if (!rmtab || rmtab[0] == '\0')
                return _gf_false;

        ret = gf_store_handle_new (rmtab, sh);
        if (ret) {
                gf_log (GF_MNT, GF_LOG_WARNING, "Failed to open '%s'", rmtab);
                return _gf_false;
        }

        return _gf_true;
}


/* Read the rmtab into a clean ms->mountlist.
 */
static void
mount_read_rmtab (struct mount3_state *ms)
{
        gf_store_handle_t       *sh = NULL;
        struct nfs_state        *nfs = NULL;
        gf_boolean_t            read_rmtab = _gf_false;

        nfs = (struct nfs_state *)ms->nfsx->private;

        read_rmtab = mount_open_rmtab (nfs->rmtab, &sh);
        if (!read_rmtab)
                return;

        if (gf_store_lock (sh)) {
                gf_msg (GF_MNT, GF_LOG_WARNING, 0, NFS_MSG_LOCK_FAIL,
                        "Failed to lock '%s'", nfs->rmtab);
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
 * 0. if opening the nfs->rmtab fails, return gracefully
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
        gf_boolean_t            got_old_rmtab = _gf_false;

        nfs = (struct nfs_state *)ms->nfsx->private;

        got_old_rmtab = mount_open_rmtab (nfs->rmtab, &sh);
        if (!got_old_rmtab && !new_rmtab)
                return;

        if (got_old_rmtab && gf_store_lock (sh)) {
                gf_msg (GF_MNT, GF_LOG_WARNING, 0, NFS_MSG_REWRITE_ERROR,
                        "Not rewriting '%s'", nfs->rmtab);
                goto free_sh;
        }

        if (new_rmtab) {
                ret = gf_store_handle_new (new_rmtab, &nsh);
                if (ret) {
                        gf_msg (GF_MNT, GF_LOG_WARNING, 0, NFS_MSG_OPEN_FAIL,
                                "Failed to open '%s'", new_rmtab);
                        goto unlock_sh;
                }

                if (gf_store_lock (nsh)) {
                        gf_msg (GF_MNT, GF_LOG_WARNING, 0,
                                NFS_MSG_REWRITE_ERROR,
                                "Not rewriting '%s'", new_rmtab);
                        goto free_nsh;
                }
        }

        /* always read the currently used rmtab */
        if (got_old_rmtab)
                __mount_read_rmtab (sh, &ms->mountlist, _gf_true);

        if (new_rmtab) {
                /* read the new rmtab and write changes to the new location */
                __mount_read_rmtab (nsh, &ms->mountlist, _gf_true);
                __mount_rewrite_rmtab (ms, nsh);

                /* replace the nfs->rmtab reference to the new rmtab */
                rmtab = gf_strdup(new_rmtab);
                if (rmtab == NULL) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, errno, NFS_MSG_NO_MEMORY,
                                "Out of memory, keeping %s as rmtab",
                                nfs->rmtab);
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
        if (got_old_rmtab)
                gf_store_unlock (sh);
free_sh:
        if (got_old_rmtab)
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
 * It is fully acceptable that the ms->mountlist is not 100% correct, this is a
 * common issue for all(?) NFS-servers.
 */
int
mnt3svc_update_mountlist (struct mount3_state *ms, rpcsvc_request_t *req,
                          const char *expname, const char *fullpath)
{
        struct mountentry       *me = NULL;
        struct mountentry       *cur = NULL;
        int                     ret = -1;
        char                    *colon = NULL;
        struct nfs_state        *nfs = NULL;
        gf_store_handle_t       *sh = NULL;
        gf_boolean_t            update_rmtab = _gf_false;

        if ((!ms) || (!req) || (!expname))
                return -1;

        me = (struct mountentry *)GF_CALLOC (1, sizeof (*me),
                                             gf_nfs_mt_mountentry);
        if (!me)
                return -1;

        nfs = (struct nfs_state *)ms->nfsx->private;

        update_rmtab = mount_open_rmtab (nfs->rmtab, &sh);

        strncpy (me->exname, expname, MNTPATHLEN);
        /* Sometimes we don't care about the full path
         * so a NULL value for fullpath is valid.
         */
        if (fullpath) {
                if (strlen (fullpath) < MNTPATHLEN) {
                        strcpy (me->fullpath, fullpath);
                        me->has_full_path = _gf_true;
                }
        }


        INIT_LIST_HEAD (&me->mlist);
        /* Must get the IP or hostname of the client so we
         * can map it into the mount entry.
         */
        ret = rpcsvc_transport_peername (req->trans, me->hostname, MNTPATHLEN);
        if (ret == -1)
                goto free_err;

        colon = strrchr (me->hostname, ':');
        if (colon) {
                *colon = '\0';
        }
        LOCK (&ms->mountlock);
        {
                /* in case locking fails, we just don't write the rmtab */
                if (update_rmtab && gf_store_lock (sh)) {
                        gf_msg (GF_MNT, GF_LOG_WARNING, 0, NFS_MSG_LOCK_FAIL,
                                "Failed to lock '%s', changes will not be "
                                "written", nfs->rmtab);
                } else if (update_rmtab) {
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
                __mountdict_insert (ms, me);

                /* only write the rmtab in case it was locked */
                if (update_rmtab && gf_store_locked_local (sh))
                        __mount_rewrite_rmtab (ms, sh);
        }
dont_add:
        if (update_rmtab && gf_store_locked_local (sh))
                gf_store_unlock (sh);

        UNLOCK (&ms->mountlock);

free_err:
        if (update_rmtab)
                gf_store_handle_destroy (sh);

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
                        gf_uuid_copy (volumeid, exp->volumeid);
                        ret = 0;
                        goto out;
                }
        }

out:
        UNLOCK (&ms->mountlock);
        return ret;
}

int
__mnt3_build_mountid_from_path (const char *path, uuid_t mountid)
{
        uint32_t hashed_path = 0;
        int      ret = -1;

        while (strlen (path) > 0 && path[0] == '/')
                path++;

        /* Clear the mountid */
        gf_uuid_clear (mountid);

        hashed_path = SuperFastHash (path,  strlen (path));
        if (hashed_path == 1) {
                gf_msg (GF_MNT, GF_LOG_WARNING, 0, NFS_MSG_HASH_PATH_FAIL,
                        "failed to hash path: %s", path);
                goto out;
        }

        memcpy (mountid, &hashed_path, sizeof (hashed_path));
        ret = 0;
out:
        return ret;
}

int
__mnt3_get_mount_id (xlator_t *mntxl, uuid_t mountid)
{
        int ret = -1;
        uint32_t hashed_path = 0;


        /* first clear the mountid */
        gf_uuid_clear (mountid);

        hashed_path = SuperFastHash (mntxl->name, strlen (mntxl->name));
        if (hashed_path == 1) {
                gf_msg (GF_MNT, GF_LOG_WARNING, 0, NFS_MSG_HASH_XLATOR_FAIL,
                        "failed to hash xlator name: %s", mntxl->name);
                goto out;
        }

        memcpy (mountid, &hashed_path, sizeof (hashed_path));
        ret = 0;
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
        char                    *path = NULL;
        uuid_t                  mountid = {1, };
        char                    fhstr[1536];

        req = (rpcsvc_request_t *)frame->local;

        if (!req)
                return -1;

        mntxl = (xlator_t *)cookie;
        ms = (struct mount3_state *)rpcsvc_request_program_private (req);
        if (!ms) {
                gf_msg (GF_MNT, GF_LOG_ERROR, EINVAL,
                        NFS_MSG_MNT_STATE_NOT_FOUND,
                        "mount state not found");
                op_ret = -1;
                op_errno = EINVAL;
        }

        if (op_ret == -1) {
                gf_msg (GF_NFS, GF_LOG_ERROR, op_errno,
                        NFS_MSG_LOOKUP_MNT_ERROR, "error=%s",
                        strerror (op_errno));
                status = mnt3svc_errno_to_mnterr (op_errno);
        }
        if (status != MNT3_OK)
                goto xmit_res;

        path = GF_CALLOC (PATH_MAX, sizeof (char), gf_nfs_mt_char);
        if (!path) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Out of memory");
                goto xmit_res;
        }

        snprintf (path, PATH_MAX, "/%s", mntxl->name);
        mnt3svc_update_mountlist (ms, req, path, NULL);
        GF_FREE (path);
        if (gf_nfs_dvm_off (nfs_state (ms->nfsx))) {
                fh = nfs3_fh_build_indexed_root_fh (ms->nfsx->children, mntxl);
                goto xmit_res;
        }

        __mnt3_get_mount_id (mntxl, mountid);
        __mnt3_get_volume_id (ms, mntxl, volumeid);
        fh = nfs3_fh_build_uuid_root_fh (volumeid, mountid);

xmit_res:
        nfs3_fh_to_str (&fh, fhstr, sizeof (fhstr));
        gf_msg_debug (GF_MNT, 0, "MNT reply: fh %s, status: %d", fhstr,
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
mnt3_match_dirpath_export (const char *expname, const char *dirpath,
                           gf_boolean_t export_parsing_match)
{
        int     ret = 0;
        size_t  dlen;
        char    *fullpath = NULL;
        char    *second_slash = NULL;
        char    *dirdup = NULL;

        if ((!expname) || (!dirpath))
                return 0;

        dirdup = strdupa (dirpath);

        /* Some clients send a dirpath for mount that includes the slash at the
         * end. String compare for searching the export will fail because our
         * exports list does not include that slash. Remove the slash to
         * compare.
         */
        dlen = strlen (dirdup);
        if (dlen && dirdup[dlen - 1] == '/')
                dirdup[dlen - 1] = '\0';

        /* Here we try to match fullpaths with export names */
        fullpath = dirdup;

        if (export_parsing_match) {
                if (dirdup[0] == '/')
                        fullpath = dirdup + 1;

                second_slash = strchr (fullpath, '/');
                if (second_slash)
                        *second_slash = '\0';
        }

        /* The export name begins with a slash so move it forward by one
         * to ignore the slash when we want to compare the fullpath and
         * export.
         */
        if (fullpath[0] != '/')
                expname++;

        if (strcmp (expname, fullpath) == 0)
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
                gf_msg (GF_MNT, GF_LOG_ERROR, ret, NFS_MSG_INODE_LOC_FILL_ERROR,
                        "Loc fill failed for export inode"
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
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOENT,
                        NFS_MSG_GET_ROOT_INODE_FAIL,
                        "Failed to get root inode");
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

int
mnt3_parse_dir_exports (rpcsvc_request_t *req, struct mount3_state *ms,
                        char *subdir);

int32_t
mnt3_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, const char *path,
                   struct iatt *buf, dict_t *xdata);

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
        gf_uuid_copy (gfid, mres->resolveloc.inode->gfid);
        nfs_loc_wipe (&mres->resolveloc);
        ret = nfs_entry_loc_fill (mres->mstate->nfsx, mres->exp->vol->itable,
                                  gfid, nextcomp, &mres->resolveloc,
                                  NFS_RESOLVE_CREATE, NULL);
        if ((ret < 0) && (ret != -2)) {
                gf_msg (GF_MNT, GF_LOG_ERROR, EFAULT,
                        NFS_MSG_RESOLVE_INODE_FAIL, "Failed to resolve and "
                        "create inode: parent gfid %s, entry %s",
                        uuid_utoa (gfid), nextcomp);
                ret = -EFAULT;
                goto err;
        }

        nfs_request_user_init (&nfu, mres->req);
        if (IA_ISLNK (mres->resolveloc.inode->ia_type)) {
                ret = nfs_readlink (mres->mstate->nfsx, mres->exp->vol, &nfu,
                                    &mres->resolveloc, mnt3_readlink_cbk, mres);
                gf_msg_debug (GF_MNT, 0, "Symlink found , need to resolve"
                              " into directory handle");
                goto err;
        }
        ret = nfs_lookup (mres->mstate->nfsx, mres->exp->vol, &nfu,
                          &mres->resolveloc, mnt3_resolve_subdir_cbk, mres);

err:
        return ret;
}

int __mnt3_resolve_subdir (mnt3_resolve_t *mres);

/*
 * Per the AFR2 comments, this function performs the "fresh" lookup
 * by deleting the inode from cache and calling __mnt3_resolve_subdir
 * again.
 */
int __mnt3_fresh_lookup (mnt3_resolve_t *mres) {
        inode_unlink (mres->resolveloc.inode,
                mres->resolveloc.parent, mres->resolveloc.name);
        strncpy (mres->remainingdir, mres->resolveloc.path,
                strlen(mres->resolveloc.path));
        nfs_loc_wipe (&mres->resolveloc);
        return __mnt3_resolve_subdir (mres);
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
        struct mount3_state     *ms = NULL;
        int                     authcode = 0;
        char                    *authorized_host = NULL;
        char                    *authorized_path = NULL;
        inode_t                 *linked_inode = NULL;

        mres = frame->local;
        ms = mres->mstate;
        mntxl = (xlator_t *)cookie;
        if (op_ret == -1 && op_errno == ESTALE) {
                /* Nuke inode from cache and try the LOOKUP
                 * request again. */
                return __mnt3_fresh_lookup (mres);
        } else if (op_ret == -1) {
                gf_msg (GF_NFS, GF_LOG_ERROR, op_errno,
                        NFS_MSG_RESOLVE_SUBDIR_FAIL, "path=%s (%s)",
                        mres->resolveloc.path, strerror (op_errno));
                mntstat = mnt3svc_errno_to_mnterr (op_errno);
                goto err;
        }

        linked_inode = inode_link (mres->resolveloc.inode,
                                   mres->resolveloc.parent,
                                   mres->resolveloc.name, buf);

        if (linked_inode)
                nfs_fix_generation (this, linked_inode);

        nfs3_fh_build_child_fh (&mres->parentfh, buf, &fh);
        if (strlen (mres->remainingdir) <= 0) {
                size_t alloclen;
                op_ret = -1;
                mntstat = MNT3_OK;

                /* Construct the full path */
                alloclen = strlen (mres->exp->expname) +
                                   strlen (mres->resolveloc.path) + 1;
                mres->exp->fullpath = GF_CALLOC (alloclen, sizeof (char),
                                                gf_nfs_mt_char);
                if (!mres->exp->fullpath) {
                        gf_msg (GF_MNT, GF_LOG_CRITICAL, ENOMEM,
                                NFS_MSG_NO_MEMORY, "Allocation failed.");
                        goto err;
                }
                snprintf (mres->exp->fullpath, alloclen, "%s%s",
                                mres->exp->expname, mres->resolveloc.path);

                /* Check if this path is authorized to be mounted */
                authcode = mnt3_authenticate_request (ms, mres->req, NULL, NULL,
                                                      mres->exp->fullpath,
                                                      &authorized_path,
                                                      &authorized_host,
                                                      FALSE);
                if (authcode != 0) {
                        mntstat = MNT3ERR_ACCES;
                        gf_msg_debug (GF_MNT, 0, "Client mount not allowed");
                        op_ret = -1;
                        goto err;
                }

                path = GF_CALLOC (PATH_MAX, sizeof (char), gf_nfs_mt_char);
                if (!path) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM,
                                NFS_MSG_NO_MEMORY,
                                "Memory allocation failed");
                        goto err;
                }
                /* Build mountid from the authorized path and stick it in the
                 * filehandle that will get passed back to the client
                 */
                __mnt3_build_mountid_from_path (authorized_path, fh.mountid);

                snprintf (path, PATH_MAX, "/%s%s", mres->exp->vol->name,
                         mres->resolveloc.path);

                mnt3svc_update_mountlist (mres->mstate, mres->req,
                                          path, mres->exp->fullpath);
                GF_FREE (path);
        } else {
                mres->parentfh = fh;
                op_ret = __mnt3_resolve_export_subdir_comp (mres);
                if (op_ret < 0)
                        mntstat = mnt3svc_errno_to_mnterr (-op_ret);
        }
err:
        if (op_ret == -1) {
                gf_msg_debug (GF_MNT, 0, "Mount reply status: %d", mntstat);
                svc = rpcsvc_request_service (mres->req);
                autharrlen = rpcsvc_auth_array (svc, mntxl->name, autharr,
                                                10);

                res = mnt3svc_set_mountres3 (mntstat, &fh, autharr, autharrlen);
                mnt3svc_submit_reply (mres->req, (void *)&res,
                                      (mnt3_serializer)xdr_serialize_mountres3);
                mnt3_resolve_state_wipe (mres);
        }

        GF_FREE (authorized_path);
        GF_FREE (authorized_host);

        return 0;
}

/* This function resolves symbolic link into directory path from
 * the mount and restart the parsing process from the beginning
 *
 * Note : Path specified in the symlink should be relative to the
 *        symlink, because that is the one which is consistent through
 *        out the file system.
 *        If the symlink resolves into another symlink ,then same process
 *        will be repeated.
 *        If symbolic links points outside the file system are not considered
 *        here.
 *
 * TODO : 1.) This function cannot handle symlinks points to path which
 *            goes out of the filesystem and comes backs again to same.
 *            For example, consider vol is exported volume.It contains
 *            dir,
 *            symlink1 which points to ../vol/dir,
 *            symlink2 which points to ../mnt/../vol/dir,
 *            symlink1 and symlink2 are not handled right now.
 *
 *        2.) udp mount routine is much simpler from tcp routine and resolves
 *            symlink directly.May be ,its better we change this routine
 *            similar to udp
 */
int32_t
mnt3_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, const char *path,
                   struct iatt *buf, dict_t *xdata)
{
        mnt3_resolve_t           *mres            = NULL;
        int                      ret              = -EFAULT;
        char                     *real_loc        = NULL;
        size_t                   path_len         = 0;
        size_t                   parent_path_len  = 0;
        char                     *parent_path     = NULL;
        char                     *absolute_path   = NULL;
        char                     *relative_path   = NULL;
        int                      mntstat          = 0;

        GF_ASSERT (frame);

        mres = frame->local;
        if (!mres || !path || (path[0] == '/') || (op_ret < 0))
                goto mnterr;

        /* Finding current location of symlink */
        parent_path_len = strlen (mres->resolveloc.path) - strlen (mres->resolveloc.name);
        parent_path = gf_strndup (mres->resolveloc.path, parent_path_len);
        if (!parent_path) {
                ret = -ENOMEM;
                goto mnterr;
        }

        relative_path = gf_strdup (path);
        if (!relative_path) {
                ret = -ENOMEM;
                goto mnterr;
        }
        /* Resolving into absolute path */
        ret = gf_build_absolute_path (parent_path, relative_path, &absolute_path);
        if (ret < 0) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ret,
                        NFS_MSG_RESOLVE_SYMLINK_ERROR,
                        "Cannot resolve symlink, path is out of boundary "
                        "from current location %s and with relative path "
                        "%s pointed by symlink", parent_path, relative_path);

                goto mnterr;
        }

        /* Building the actual mount path to be mounted */
        path_len = strlen (mres->exp->vol->name) +  strlen (absolute_path)
                   + strlen (mres->remainingdir) + 1;
        real_loc = GF_CALLOC (1, path_len, gf_nfs_mt_char);
        if (!real_loc) {
                ret = -ENOMEM;
                goto mnterr;
        }
        sprintf (real_loc , "%s%s", mres->exp->vol->name, absolute_path);
        gf_path_strip_trailing_slashes (real_loc);

        /* There may entries after symlink in the mount path,
         * we should include remaining entries too */
        if (strlen (mres->remainingdir) > 0)
                strcat (real_loc, mres->remainingdir);

        gf_msg_debug (GF_MNT, 0, "Resolved path is : %s%s "
                      "and actual mount path is %s",
                      absolute_path, mres->remainingdir, real_loc);

        /* After the resolving the symlink , parsing should be done
         * for the populated mount path
         */
        ret = mnt3_parse_dir_exports (mres->req, mres->mstate, real_loc);

        if (ret) {
                gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_RESOLVE_ERROR,
                        "Resolved into an unknown path %s%s "
                        "from the current location of symlink %s",
                         absolute_path, mres->remainingdir, parent_path);
        }

        GF_FREE (real_loc);
        GF_FREE (absolute_path);
        GF_FREE (parent_path);
        GF_FREE (relative_path);

        return ret;

mnterr:
        if (mres) {
                mntstat = mnt3svc_errno_to_mnterr (-ret);
                mnt3svc_mnt_error_reply (mres->req, mntstat);
        } else
                gf_msg (GF_MNT, GF_LOG_CRITICAL, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "mres == NULL, this should *never* happen");
        if (absolute_path)
                GF_FREE (absolute_path);
        if (parent_path)
                GF_FREE (parent_path);
        if (relative_path)
                GF_FREE (relative_path);
        return ret;
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
        ret = nfs_entry_loc_fill (mres->mstate->nfsx, mres->exp->vol->itable,
                                  rootgfid, firstcomp, &mres->resolveloc,
                                  NFS_RESOLVE_CREATE, NULL);
        if ((ret < 0) && (ret != -2)) {
                gf_msg (GF_MNT, GF_LOG_ERROR, EFAULT,
                        NFS_MSG_RESOLVE_INODE_FAIL, "Failed to resolve and "
                        "create inode for volume root: %s",
                        mres->exp->vol->name);
                ret = -EFAULT;
                goto err;
        }

        nfs_request_user_init (&nfu, mres->req);
        if (IA_ISLNK (mres->resolveloc.inode->ia_type)) {
                ret = nfs_readlink (mres->mstate->nfsx, mres->exp->vol, &nfu,
                                    &mres->resolveloc, mnt3_readlink_cbk, mres);
                gf_msg_debug (GF_MNT, 0, "Symlink found , need to resolve "
                              "into directory handle");
                goto err;
        }
        ret = nfs_lookup (mres->mstate->nfsx, mres->exp->vol, &nfu,
                          &mres->resolveloc, mnt3_resolve_subdir_cbk, mres);

err:
        return ret;
}


static gf_boolean_t
mnt3_match_subnet_v4 (struct addrinfo *ai, uint32_t saddr, uint32_t mask)
{
        for (; ai; ai = ai->ai_next) {
                struct sockaddr_in *sin = (struct sockaddr_in *)ai->ai_addr;

                if (sin->sin_family != AF_INET)
                        continue;

                if (mask_match (saddr, sin->sin_addr.s_addr, mask))
                        return _gf_true;
        }

        return _gf_false;
}


/**
 * This function will verify if the client is allowed to mount
 * the directory or not. Client's IP address will be compared with
 * allowed IP list or range present in mnt3_export structure.
 *
 * @param client_addr - This structure contains client's IP address.
 * @param export - mnt3_export structure. Contains allowed IP list/range.
 *
 * @return 0 - on Success and -EACCES on failure.
 *
 * TODO: Support IPv6 subnetwork
 */
int
mnt3_verify_auth (struct sockaddr_in *client_addr, struct mnt3_export *export)
{
        int                     retvalue = -EACCES;
        int                     ret = 0;
        struct host_auth_spec   *host = NULL;
        struct sockaddr_in      *allowed_addr = NULL;
        struct addrinfo         *allowed_addrinfo = NULL;

        struct addrinfo         hint = {
                .ai_family      = AF_INET,
                .ai_protocol    = (int)IPPROTO_TCP,
                .ai_flags       = AI_CANONNAME,
        };

        /* Sanity check */
        if ((NULL == client_addr) ||
            (NULL == export) ||
            (NULL == export->hostspec)) {
                gf_msg (GF_MNT, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Invalid argument");
                return retvalue;
        }

        host = export->hostspec;

        /*
         * Currently IPv4 subnetwork is supported i.e. AF_INET.
         * TODO: IPv6 subnetwork i.e. AF_INET6.
         */
        if (client_addr->sin_family != AF_INET) {
                gf_msg (GF_MNT, GF_LOG_ERROR, EAFNOSUPPORT,
                        NFS_MSG_UNSUPPORTED_VERSION,
                        "Only IPv4 is supported for subdir-auth");
                return retvalue;
        }

        /* Try to see if the client IP matches the allowed IP list.*/
        while (NULL != host){
                GF_ASSERT (host->host_addr);

                if (NULL != allowed_addrinfo) {
                        freeaddrinfo (allowed_addrinfo);
                        allowed_addrinfo = NULL;
                }

                /* Get the addrinfo for the allowed host (host_addr). */
                ret = getaddrinfo (host->host_addr, NULL,
                                   &hint, &allowed_addrinfo);
                if (0 != ret){
                        /*
                         * getaddrinfo() FAILED for the host IP addr. Continue
                         * to search other allowed hosts in the  hostspec list.
                         */
                        gf_msg_debug (GF_MNT, 0, "getaddrinfo: %s\n",
                                      gai_strerror (ret));
                        host = host->next;
                        continue;
                }

                allowed_addr = (struct sockaddr_in *)(allowed_addrinfo->ai_addr);
                if (NULL == allowed_addr) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, EINVAL,
                                NFS_MSG_INVALID_ENTRY, "Invalid structure");
                        break;
                }

                /* Check if the network addr of both IPv4 socket match */
                if (mnt3_match_subnet_v4 (allowed_addrinfo,
                                          client_addr->sin_addr.s_addr,
                                          host->netmask)) {
                        retvalue = 0;
                        break;
                }

                /* No match yet, continue the search */
               host = host->next;
        }

        /* FREE the dynamic memory allocated by getaddrinfo() */
        if (NULL != allowed_addrinfo) {
               freeaddrinfo (allowed_addrinfo);
        }

        return retvalue;
}

int
mnt3_resolve_subdir (rpcsvc_request_t *req, struct mount3_state *ms,
                     struct mnt3_export *exp, char *subdir)
{
        mnt3_resolve_t        *mres = NULL;
        int                   ret = -EFAULT;
        struct nfs3_fh        pfh = GF_NFS3FH_STATIC_INITIALIZER;
        struct sockaddr_in    *sin = NULL;

        if ((!req) || (!ms) || (!exp) || (!subdir))
                return ret;

        sin = (struct sockaddr_in *)(&(req->trans->peerinfo.sockaddr));

        /* Need to check AUTH */
        if (NULL != exp->hostspec) {
                ret = mnt3_verify_auth (sin, exp);
                if (0 != ret) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, EACCES,
                                NFS_MSG_AUTH_VERIFY_FAILED,
                                "AUTH verification failed");
                        return ret;
                }
        }

        mres = GF_CALLOC (1, sizeof (mnt3_resolve_t), gf_nfs_mt_mnt3_resolve);
        if (!mres) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Memory allocation failed");
                goto err;
        }

        mres->exp = exp;
        mres->mstate = ms;
        mres->req = req;

        strncpy (mres->remainingdir, subdir, MNTPATHLEN);
        gf_path_strip_trailing_slashes (mres->remainingdir);

        if (gf_nfs_dvm_off (nfs_state (ms->nfsx)))
                pfh = nfs3_fh_build_indexed_root_fh (
                                            mres->mstate->nfsx->children,
                                            mres->exp->vol);
        else
                pfh = nfs3_fh_build_uuid_root_fh (exp->volumeid, exp->mountid);

        mres->parentfh = pfh;
        ret = __mnt3_resolve_subdir (mres);
        if (ret < 0) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ret, NFS_MSG_RESOLVE_SUBDIR_FAIL,
                        "Failed to resolve export dir: %s", mres->exp->expname);
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
                gf_msg (GF_MNT, GF_LOG_ERROR, ret, NFS_MSG_RESOLVE_SUBDIR_FAIL,
                        "Failed to resolve export dir: %s", exp->expname);
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
*
* The parameter 'export_parsing_match' indicates whether this function
* is being called by an exports parser or whether it is being called
* during mount. The behavior is different since we don't have to resolve
* the path when doing the parse.
*/
struct mnt3_export *
mnt3_mntpath_to_export (struct mount3_state *ms, const char *dirpath,
                        gf_boolean_t export_parsing_match)
{
        struct mnt3_export      *exp = NULL;
        struct mnt3_export      *found = NULL;

        if ((!ms) || (!dirpath))
                return NULL;

        LOCK (&ms->mountlock);
        list_for_each_entry (exp, &ms->exportlist, explist) {

                /* Search for the an exact match with the volume */
                if (mnt3_match_dirpath_export (exp->expname, dirpath,
                                               export_parsing_match)) {
                        found = exp;
                        gf_msg_debug (GF_MNT, 0, "Found export volume: "
                                      "%s", exp->vol->name);
                        goto foundexp;
                }
        }

        gf_msg_debug (GF_MNT, 0, "Export not found");
foundexp:
        UNLOCK (&ms->mountlock);
        return found;
}


static int
mnt3_check_client_net_check (rpcsvc_t *svc, char *expvol,
                             char *ipaddr, uint16_t port)
{
        int ret = RPCSVC_AUTH_REJECT;

        if ((!svc) || (!expvol) || (!ipaddr))
                goto err;

        ret = rpcsvc_auth_check (svc, expvol, ipaddr);
        if (ret == RPCSVC_AUTH_REJECT) {
                gf_msg (GF_MNT, GF_LOG_INFO, 0, NFS_MSG_PEER_NOT_ALLOWED,
                        "Peer %s  not allowed", ipaddr);
                goto err;
        }

        ret = rpcsvc_transport_privport_check (svc, expvol, port);
        if (ret == RPCSVC_AUTH_REJECT) {
                gf_msg (GF_MNT, GF_LOG_INFO, errno, NFS_MSG_PEER_NOT_ALLOWED,
                        "Peer %s rejected. Unprivileged "
                        "port %d not allowed", ipaddr, port);
                goto err;
        }

        ret = RPCSVC_AUTH_ACCEPT;
err:
        return ret;
}

static int
mnt3_check_client_net_tcp (rpcsvc_request_t *req, char *volname)
{
        rpcsvc_t                *svc = NULL;
        rpc_transport_t         *trans = NULL;
        union gf_sock_union     sock_union;
        socklen_t               socksize = sizeof (struct sockaddr_in);
        char                    peer[RPCSVC_PEER_STRLEN] = {0,};
        char                    *ipaddr = NULL;
        uint16_t                port = 0;
        int                     ret = RPCSVC_AUTH_REJECT;

        if ((!req) || (!volname))
                goto err;

        svc = rpcsvc_request_service (req);
        trans = rpcsvc_request_transport (req);
        if ((!svc) || (!trans))
                goto err;

        ret = rpcsvc_transport_peeraddr (trans, peer, RPCSVC_PEER_STRLEN,
                                         &sock_union.storage, socksize);
        if (ret != 0) {
                gf_msg (GF_MNT, GF_LOG_WARNING, ENOENT,
                        NFS_MSG_GET_PEER_ADDR_FAIL, "Failed to get peer "
                        "addr: %s", gai_strerror (ret));
                ret = RPCSVC_AUTH_REJECT;
                goto err;
        }

        /* peer[] gets IP:PORT formar, slash the port out */
        if (!get_host_name ((char *)peer, &ipaddr))
                ipaddr = peer;

        port = ntohs (sock_union.sin.sin_port);

        ret = mnt3_check_client_net_check (svc, volname, ipaddr, port);
err:
        return ret;
}

static int
mnt3_check_client_net_udp (struct svc_req *req, char *volname, xlator_t *nfsx)
{
        rpcsvc_t                *svc = NULL;
        struct sockaddr_in      *sin = NULL;
        char                    ipaddr[INET_ADDRSTRLEN + 1] = {0, };
        uint16_t                port = 0;
        int                     ret = RPCSVC_AUTH_REJECT;
        struct nfs_state        *nfs = NULL;

        if ((!req) || (!volname) || (!nfsx))
                goto err;

        sin = svc_getcaller (req->rq_xprt);
        if (!sin)
                goto err;

        (void) inet_ntop (AF_INET, &sin->sin_addr, ipaddr, INET_ADDRSTRLEN);

        port = ntohs (sin->sin_port);

        nfs = (struct nfs_state *)nfsx->private;
        if (nfs != NULL)
                svc = nfs->rpcsvc;

        ret = mnt3_check_client_net_check (svc, volname, ipaddr, port);
err:
        return ret;
}


int
mnt3_parse_dir_exports (rpcsvc_request_t *req, struct mount3_state *ms,
                        char *subdir)
{
        char                    volname[1024] = {0, };
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

        exp = mnt3_mntpath_to_export (ms, volname, _gf_false);
        if (!exp)
                goto err;

        nfs = (struct nfs_state *)ms->nfsx->private;
        if (!nfs)
                goto err;

        if (!nfs_subvolume_started (nfs, exp->vol)) {
                gf_msg_debug (GF_MNT, 0, "Volume %s not started",
                              exp->vol->name);
                goto err;
        }

        ret = mnt3_check_client_net_tcp (req, exp->vol->name);
        if (ret == RPCSVC_AUTH_REJECT) {
                gf_msg_debug (GF_MNT, 0, "Client mount not allowed");
                ret = -EACCES;
                goto err;
        }

        ret = mnt3_resolve_subdir (req, ms, exp, subdir);
        if (ret < 0) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ret, NFS_MSG_RESOLVE_SUBDIR_FAIL,
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
                gf_msg (GF_MNT, GF_LOG_ERROR, EINVAL,
                        NFS_MSG_MNT_STATE_NOT_FOUND, "Mount state not present");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                goto err;
        }

        gf_msg_debug (GF_MNT, 0, "dirpath: %s", path);
        exp = mnt3_mntpath_to_export (ms, path, _gf_false);
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

/**
 * _mnt3_get_peer_addr -- Take an rpc request object and return an allocated
 *                        peer address. A peer address is host:port.
 *
 * @req: An rpc svc request object to extract the peer address from
 *
 * @return: success: Pointer to an allocated string containing the peer address
 *          failure: NULL
 */
char *
_mnt3_get_peer_addr (const rpcsvc_request_t *req)
{
        rpc_transport_t         *trans = NULL;
        struct sockaddr_storage sastorage = {0, };
        char                    peer[RPCSVC_PEER_STRLEN] = {0, };
        char                    *peerdup = NULL;
        int                     ret = 0;

        GF_VALIDATE_OR_GOTO (GF_NFS, req, out);

        trans = rpcsvc_request_transport (req);
        ret = rpcsvc_transport_peeraddr (trans, peer, RPCSVC_PEER_STRLEN,
                                         &sastorage, sizeof (sastorage));
        if (ret != 0)
                goto out;

        peerdup = gf_strdup (peer);
out:
        return peerdup;
}

/**
 * _mnt3_get_host_from_peer -- Take a peer address and get an allocated
 *                             hostname. The hostname is the string on the
 *                             left side of the colon.
 *
 * @peer_addr: The peer address to get a hostname from
 *
 * @return: success: Allocated string containing the hostname
 *          failure: NULL
 *
 */
char *
_mnt3_get_host_from_peer (const char *peer_addr)
{
        char   *part       = NULL;
        size_t host_len    = 0;
        char   *colon      = NULL;

        colon = strchr (peer_addr, ':');
        if (!colon) {
                gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_BAD_PEER,
                        "Bad peer %s", peer_addr);
                goto out;
        }

        host_len = colon - peer_addr;
        if (host_len < RPCSVC_PEER_STRLEN)
                part = gf_strndup (peer_addr, host_len);
        else
                gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_PEER_TOO_LONG,
                        "Peer too long %s", peer_addr);
out:
        return part;
}

/**
 * mnt3_check_cached_fh -- Check if FH is cached.
 *
 * Calls auxiliary functions based on whether we are checking
 * a write operation.
 *
 */
int
mnt3_check_cached_fh (struct mount3_state *ms, struct nfs3_fh *fh,
                      const char *host_addr, gf_boolean_t is_write_op)
{
        if (!is_write_op)
                return is_nfs_fh_cached (ms->authcache, fh, host_addr);

        return is_nfs_fh_cached_and_writeable (ms->authcache, fh, host_addr);
}

/**
 * _mnt3_authenticate_req -- Given an RPC request and a path OR a filehandle
 *                           check if the host is authorized to make the
 *                           request. Uses exports/netgroups auth model to
 *                           do this check.
 *
 * @ms  : The mount state
 * @req : The RPC request
 * @fh  : The NFS FH to authenticate (set when authenticating an FOP)
 * @path: The path to authenticate (set when authenticating a mount req)
 * @authorized_export: Allocate and fill this value when an export is authorized
 * @authorized_host: Allocate and fill this value when a host is authorized
 * @is_write_op: Is this a write op that we are authenticating?
 *
 * @return: 0 if authorized
 *          -EACCES for completely unauthorized fop
 *          -EROFS  for unauthorized write operations (rm, mkdir, write)
 */
int
_mnt3_authenticate_req (struct mount3_state *ms, rpcsvc_request_t *req,
                        struct nfs3_fh *fh, const char *path,
                        char **authorized_export, char **authorized_host,
                        gf_boolean_t is_write_op)
{
        char                    *peer_addr       = NULL;
        char                    *host_addr_ip    = NULL;
        char                    *host_addr_fqdn  = NULL;
        int                     auth_status_code = -EACCES;
        char                    *pathdup         = NULL;
        size_t                  dlen             = 0;
        char                    *auth_host       = NULL;
        gf_boolean_t            fh_cached        = _gf_false;
        struct export_item      *expitem         = NULL;

        GF_VALIDATE_OR_GOTO (GF_MNT, ms, out);
        GF_VALIDATE_OR_GOTO (GF_MNT, req, out);

        peer_addr    = _mnt3_get_peer_addr (req);
        host_addr_ip = _mnt3_get_host_from_peer (peer_addr);

        if (!host_addr_ip || !peer_addr)
                goto free_and_out;

        if (path) {
                /* Need to strip out trailing '/' */
                pathdup = strdupa (path);
                dlen = strlen (pathdup);
                if (dlen > 0 && pathdup[dlen-1] == '/')
                        pathdup[dlen-1] = '\0';
        }

        /* Check if the filehandle is cached */
        fh_cached = mnt3_check_cached_fh (ms, fh, host_addr_ip, is_write_op);
        if (fh_cached) {
                gf_msg_trace (GF_MNT, 0, "Found cached FH for %s",
                              host_addr_ip);
                auth_status_code = 0;
                goto free_and_out;
        }

        /* Check if the IP is authorized */
        auth_status_code = mnt3_auth_host (ms->auth_params, host_addr_ip,
                                           fh, pathdup, is_write_op, &expitem);

        gf_msg_debug (GF_MNT, 0, "access from IP %s is %s", host_addr_ip,
                      auth_status_code ? "denied" : "allowed");

        if (auth_status_code != 0) {
                /* If not, check if the FQDN is authorized */
                host_addr_fqdn = gf_rev_dns_lookup (host_addr_ip);
                auth_status_code = mnt3_auth_host (ms->auth_params,
                                                   host_addr_fqdn,
                                                   fh, pathdup, is_write_op,
                                                   &expitem);

                gf_msg_debug (GF_MNT, 0, "access from FQDN %s is %s",
                              host_addr_fqdn, auth_status_code ? "denied" :
                                                                 "allowed");

                if (auth_status_code == 0)
                        auth_host = host_addr_fqdn;
        } else
                auth_host = host_addr_ip;

        /* Skip the lines that set authorized export &
         * host if they are null.
         */
        if (!authorized_export || !authorized_host) {
                /* Cache the file handle if it was authorized */
                if (fh && auth_status_code == 0)
                        cache_nfs_fh (ms->authcache, fh, host_addr_ip, expitem);

                goto free_and_out;
        }

        if (!fh && auth_status_code == 0) {
                *authorized_export = gf_strdup (pathdup);
                if (!*authorized_export)
                        gf_msg (GF_MNT, GF_LOG_CRITICAL, ENOMEM,
                                NFS_MSG_NO_MEMORY,
                                "Allocation error when copying "
                                "authorized path");

                *authorized_host = gf_strdup (auth_host);
                if (!*authorized_host)
                        gf_msg (GF_MNT, GF_LOG_CRITICAL, ENOMEM,
                                NFS_MSG_NO_MEMORY,
                                "Allocation error when copying "
                                "authorized host");
        }

free_and_out:
        /* Free allocated strings after doing the auth */
        GF_FREE (peer_addr);
        GF_FREE (host_addr_fqdn);
        GF_FREE (host_addr_ip);
out:
        return auth_status_code;
}

/**
 * mnt3_authenticate_request -- Given an RPC request and a path, check if the
 *                              host is authorized to make the request. This
 *                              function calls _mnt3_authenticate_req_path ()
 *                              in a loop for the parent of each path while
 *                              the authentication check for that path is
 *                              failing.
 *
 * E.g. If the requested path is /patchy/L1, and /patchy is authorized, but
 * /patchy/L1 is not, it follows this code path :
 *
 * _mnt3_authenticate_req ("/patchy/L1") -> F
 * _mnt3_authenticate_req ("/patchy");   -> T
 * return T;
 *
 * @ms  : The mount state
 * @req : The RPC request
 * @path: The requested path
 * @authorized_path: This gets allocated and populated with the authorized path
 * @authorized_host: This gets allocated and populated with the authorized host
 * @return: 0 if authorized
 *          -EACCES for completely unauthorized fop
 *          -EROFS  for unauthorized write operations (rm, mkdir, write)
 */
int
mnt3_authenticate_request (struct mount3_state *ms, rpcsvc_request_t *req,
                           struct nfs3_fh *fh, const char *volname,
                           const char *path, char **authorized_path,
                           char **authorized_host, gf_boolean_t is_write_op)
{
        int          auth_status_code       = -EACCES;
        char         *parent_path = NULL;
        const char   *parent_old  = NULL;

        GF_VALIDATE_OR_GOTO (GF_MNT, ms, out);
        GF_VALIDATE_OR_GOTO (GF_MNT, req, out);

        /* If this option is not set, just allow it through */
        if (!ms->nfs->exports_auth) {
                /* This function is called in a variety of use-cases (mount
                 * + each fop) so path/authorized_path are not always present.
                 * For the cases which it _is_ present we need to populate the
                 * authorized_path. */
                if (path && authorized_path)
                        *authorized_path = gf_strdup (path);

                auth_status_code = 0;
                goto out;
        }

        /* First check if the path is allowed */
        auth_status_code = _mnt3_authenticate_req (ms, req, fh, path,
                                                   authorized_path,
                                                   authorized_host,
                                                   is_write_op);

        /* If the filehandle is set, just exit since we have to make only
         * one call to the function above
         */
        if (fh)
                goto out;

        parent_old = path;
        while (auth_status_code != 0) {
                /* Get the path's parent */
                parent_path = gf_resolve_path_parent (parent_old);
                if (!parent_path) /* Nothing left in the path to resolve */
                        goto out;

                /* Authenticate it */
                auth_status_code = _mnt3_authenticate_req (ms, req, fh,
                                                           parent_path,
                                                           authorized_path,
                                                           authorized_host,
                                                           is_write_op);

                parent_old = strdupa (parent_path); /* Copy the parent onto the
                                                     * stack.
                                                     */

                GF_FREE (parent_path); /* Free the allocated parent string */
        }

out:
        return auth_status_code;
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
        int                     authcode = 0;

        if (!req)
                return -1;

        pvec.iov_base = path;
        pvec.iov_len = MNTPATHLEN;
        ret = xdr_to_mountpath (pvec, req->msg[0]);
        if (ret == -1) {
                gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Failed to decode args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ms = (struct mount3_state *)rpcsvc_request_program_private (req);
        if (!ms) {
                gf_msg (GF_MNT, GF_LOG_ERROR, EINVAL,
                        NFS_MSG_MNT_STATE_NOT_FOUND,
                        "Mount state not present");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = -1;
                goto rpcerr;
        }

        nfs = (struct nfs_state *)ms->nfsx->private;
        gf_msg_debug (GF_MNT, 0, "dirpath: %s", path);
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
                 * nfs_subvolume_started() and mnt3_check_client_net_tcp()
                 * validation are done in mnt3_parse_dir_exports()
                 * which is invoked through mnt3_find_export().
                 *
                 * TODO: All mount should happen thorugh mnt3svc_mount()
                 *       It needs more clean up.
                 */
                return (0);
        }

        if (!nfs_subvolume_started (nfs, exp->vol)) {
                gf_msg_debug (GF_MNT, 0, "Volume %s not started",
                              exp->vol->name);
                ret = -1;
                mntstat = MNT3ERR_NOENT;
                goto mnterr;
        }

        ret = mnt3_check_client_net_tcp (req, exp->vol->name);
        if (ret == RPCSVC_AUTH_REJECT) {
                mntstat = MNT3ERR_ACCES;
                gf_msg_debug (GF_MNT, 0, "Client mount not allowed");
                ret = -1;
                goto mnterr;
        }

        /* The second authentication check is the exports/netgroups
         * check.
         */
        authcode = mnt3_authenticate_request (ms, req, NULL, NULL, path, NULL,
                                              NULL, _gf_false);
        if (authcode != 0) {
                mntstat = MNT3ERR_ACCES;
                gf_msg_debug (GF_MNT, 0, "Client mount not allowed");
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
                gf_msg (GF_MNT, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Got NULL request!");
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
        gf_msg_debug (GF_MNT, 0, "Building mount list:");
        list_for_each_entry (me, &ms->mountlist, mlist) {
                namelen = strlen (me->exname);
                mlist = GF_CALLOC (1, sizeof (*mlist), gf_nfs_mt_mountbody);
                if (!mlist) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM,
                                NFS_MSG_NO_MEMORY, "Memory allocation failed");
                        goto free_list;
                }
                if (!first)
                        first = mlist;

                mlist->ml_directory = GF_CALLOC (namelen + 2, sizeof (char),
                                                 gf_nfs_mt_char);
                if (!mlist->ml_directory) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM,
                                NFS_MSG_NO_MEMORY, "Memory allocation failed");
                        goto free_list;
                }

                strcpy (mlist->ml_directory, me->exname);

                namelen = strlen (me->hostname);
                mlist->ml_hostname = GF_CALLOC (namelen + 2, sizeof (char),
                                                gf_nfs_mt_char);
                if (!mlist->ml_hostname) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM,
                                NFS_MSG_NO_MEMORY, "Memory allocation failed");
                        goto free_list;
                }

                strcat (mlist->ml_hostname, me->hostname);

                gf_msg_debug (GF_MNT, 0, "mount entry: dir: %s, host: %s",
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
        gf_boolean_t            update_rmtab = _gf_false;

        if ((!ms) || (!dirpath) || (!hostname))
                return -1;

        nfs = (struct nfs_state *)ms->nfsx->private;

        update_rmtab = mount_open_rmtab (nfs->rmtab, &sh);
        if (update_rmtab) {
                ret = gf_store_lock (sh);
                if (ret)
                        goto out_free;
        }

        LOCK (&ms->mountlock);
        {
                if (update_rmtab)
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
                        gf_msg_trace (GF_MNT, 0, "Export not found");
                        goto out_unlock;
                }

                if (!me)
                        goto out_unlock;

                gf_msg_debug (GF_MNT, 0, "Unmounting: dir %s, host: %s",
                              me->exname, me->hostname);

                list_del (&me->mlist);
                GF_FREE (me);

                if (update_rmtab)
                        __mount_rewrite_rmtab (ms, sh);
        }
out_unlock:
        UNLOCK (&ms->mountlock);

        if (update_rmtab)
                gf_store_unlock (sh);

out_free:
        if (update_rmtab)
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
                gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_ARGS_DECODE_ERROR,
                        "Failed decode args");
                rpcsvc_request_seterr (req, GARBAGE_ARGS);
                goto rpcerr;
        }

        ms = (struct mount3_state *)rpcsvc_request_program_private (req);
        if (!ms) {
                gf_msg (GF_MNT, GF_LOG_ERROR, EINVAL,
                        NFS_MSG_MNT_STATE_NOT_FOUND, "Mount state not present");
                rpcsvc_request_seterr (req, SYSTEM_ERR);
                ret = -1;
                goto rpcerr;
        }

        ret = rpcsvc_transport_peername (req->trans, hostname, MNTPATHLEN);
        if (ret != 0) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOENT,
                        NFS_MSG_GET_REMOTE_NAME_FAIL,
                        "Failed to get remote name: %s", gai_strerror (ret));
                goto rpcerr;
        }

        colon = strrchr (hostname, ':');
        if (colon) {
                *colon= '\0';
        }
        gf_path_strip_trailing_slashes (dirpath);
        gf_msg_debug (GF_MNT, 0, "dirpath: %s, hostname: %s", dirpath,
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
                list_del (&me->mlist);       /* Remove from the mount list */
                __mountdict_remove (ms, me); /* Remove from the mount dict */
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
                gf_msg (GF_MNT, GF_LOG_ERROR, EINVAL,
                        NFS_MSG_MNT_STATE_NOT_FOUND, "Mount state not present");
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
                        gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM,
                                NFS_MSG_NO_MEMORY, "Memory allocation failed");
                        goto free_list;
                }
                 if (!first)
                         first = elist;
                elist->ex_dir = GF_CALLOC (namelen + 2, sizeof (char),
                                           gf_nfs_mt_char);
                if (!elist->ex_dir) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM,
                                NFS_MSG_NO_MEMORY, "Memory allocation failed");
                        goto free_list;
                }
                strcpy (elist->ex_dir, ent->expname);

                addrstr = rpcsvc_volume_allowed (svc->options, ent->vol->name);
                if (addrstr) {
                        /* create a groupnode per allowed client */
                        char             *pos        = NULL;
                        char             *addr       = NULL;
                        char             *addrs      = NULL;
                        struct groupnode *group      = NULL;
                        struct groupnode *prev_group = NULL;

                        /* strtok_r() modifies the string, dup it */
                        addrs = gf_strdup (addrstr);
                        if (!addrs)
                                goto free_list;

                        while (1) {
                                /* only pass addrs on the 1st call */
                                addr = strtok_r (group ? NULL : addrs, ",",
                                                 &pos);
                                if (addr == NULL)
                                        /* no mode clients */
                                        break;

                                group = GF_CALLOC (1, sizeof (struct groupnode),
                                                   gf_nfs_mt_groupnode);
                                if (!group) {
                                        gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM,
                                                NFS_MSG_NO_MEMORY, "Memory "
                                                "allocation failed");
                                        GF_FREE (addrs);
                                        goto free_list;
                                }

                                group->gr_name = gf_strdup (addr);
                                if (!group->gr_name) {
                                        gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM,
                                                NFS_MSG_NO_MEMORY, "Memory "
                                                "allocation failed");
                                        GF_FREE (group);
                                        GF_FREE (addrs);
                                        goto free_list;
                                }

                                /* chain the groups together */
                                if (!elist->ex_groups)
                                        elist->ex_groups = group;
                                else
                                        prev_group->gr_next = group;
                                prev_group = group;
                        }

                        GF_FREE (addrs);
                } else {
                        elist->ex_groups = GF_CALLOC (1,
                                                      sizeof (struct groupnode),
                                                      gf_nfs_mt_groupnode);
                        if (!elist->ex_groups) {
                                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM,
                                        NFS_MSG_NO_MEMORY, "Memory allocation "
                                        "failed");
                                goto free_list;
                        }

                        addrstr = gf_strdup ("No Access");
                        if (!addrstr)
                                goto free_list;

                        elist->ex_groups->gr_name = addrstr;
                }

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
                gf_msg (GF_MNT, GF_LOG_ERROR, EINVAL,
                        NFS_MSG_MNT_STATE_NOT_FOUND, "mount state not found");
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


/*
 * __mnt3udp_get_mstate() Fetches mount3_state from xlator
 * Linkage: Static
 * Usage: Used only for UDP MOUNT codepath
 */
static struct mount3_state *
__mnt3udp_get_mstate (xlator_t *nfsx)
{
        struct nfs_state        *nfs = NULL;
        struct mount3_state     *ms = NULL;

        if (nfsx == NULL)
                return NULL;

        nfs = (struct nfs_state *)nfsx->private;
        if (nfs == NULL)
                return NULL;

        ms = (struct mount3_state *)nfs->mstate;
        return ms;
}

extern int
glfs_resolve_at (struct glfs *, xlator_t *, inode_t *,
                 const char *, loc_t *, struct iatt *, int, int);

extern struct glfs *
glfs_new_from_ctx (glusterfs_ctx_t *);

extern void
glfs_free_from_ctx (struct glfs *);

static inode_t *
__mnt3udp_get_export_subdir_inode (struct svc_req *req, char *subdir,
                                   char *expname, /* OUT */
                                   struct mnt3_export *exp)
{
        inode_t                 *inode = NULL;
        loc_t                   loc = {0, };
        struct iatt             buf = {0, };
        int                     ret = -1;
        glfs_t                  *fs = NULL;

        if ((!req) || (!subdir) || (!expname) || (!exp))
                return NULL;

        /* AUTH check for subdir i.e. nfs.export-dir */
        if (exp->hostspec) {
                struct sockaddr_in *sin = svc_getcaller (req->rq_xprt);
                ret = mnt3_verify_auth (sin, exp);
                if (ret) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, EACCES,
                                NFS_MSG_AUTH_VERIFY_FAILED,
                                "AUTH(nfs.export-dir) verification failed");
                        errno = EACCES;
                        return NULL;
                }
        }

        /*
         * IMP: glfs_t fs object is not used by glfs_resolve_at (). The main
         * purpose is to not change the ABI of glfs_resolve_at () and not to
         * pass a NULL object.
         *
         * TODO: Instead of linking against libgfapi.so, just for one API
         * i.e. glfs_resolve_at(), It would be cleaner if PATH name to
         * inode resolution code can be moved to libglusterfs.so or so.
         * refer bugzilla for more details :
         * https://bugzilla.redhat.com/show_bug.cgi?id=1161573
         */
        fs = glfs_new_from_ctx (exp->vol->ctx);
        if (!fs)
                return NULL;

        ret = glfs_resolve_at (fs, exp->vol, NULL, subdir,
                               &loc, &buf, 1 /* Follow link */,
                               0 /* Hard lookup */);

        glfs_free_from_ctx (fs);

        if (ret != 0) {
                loc_wipe (&loc);
                return NULL;
        }

        inode = inode_ref (loc.inode);
        snprintf (expname, PATH_MAX, "/%s%s", exp->vol->name, loc.path);

        loc_wipe (&loc);

        return inode;
}

static inode_t *
__mnt3udp_get_export_volume_inode (struct svc_req *req, char *volpath,
                                   char *expname, /* OUT */
                                   struct mnt3_export *exp)
{
        char                    *rpath = NULL;
        inode_t                 *inode = NULL;

        if ((!req) || (!volpath) || (!expname) || (!exp))
                return NULL;

        rpath = strchr (volpath, '/');
        if (rpath == NULL)
                rpath = "/";

        inode = inode_from_path (exp->vol->itable, rpath);
        snprintf (expname, PATH_MAX, "/%s", exp->vol->name);

        return inode;
}

/*
 * nfs3_rootfh() is used for NFS MOUNT over UDP i.e. mountudpproc3_mnt_3_svc().
 * Especially in mount3udp_thread() THREAD. Gluster NFS starts this thread
 * when nfs.mount-udp is ENABLED (set to TRUE/ON).
 */
struct nfs3_fh *
nfs3_rootfh (struct svc_req *req, xlator_t *nfsx,
             char *path, char *expname /* OUT */)
{
        struct nfs3_fh          *fh = NULL;
        inode_t                 *inode = NULL;
        struct mnt3_export      *exp = NULL;
        struct mount3_state     *ms = NULL;
        struct nfs_state        *nfs = NULL;
        int                     mnt3type = MNT3_EXPTYPE_DIR;
        int                     ret = RPCSVC_AUTH_REJECT;

        if ((!req) || (!nfsx) || (!path) || (!expname)) {
                errno = EFAULT;
                return NULL;
        }

        /*
         * 1. First check if the MOUNT is for whole volume.
         *      i.e. __mnt3udp_get_export_volume_inode ()
         * 2. If NOT, then TRY for SUBDIR MOUNT.
         *      i.e. __mnt3udp_get_export_subdir_inode ()
         * 3. If a subdir is exported using nfs.export-dir,
         *      then the mount type would be MNT3_EXPTYPE_DIR,
         *      so make sure to find the proper path to be
         *      resolved using __volume_subdir()
         * 3. Make sure subdir export is allowed.
         */
        ms = __mnt3udp_get_mstate(nfsx);
        if (!ms) {
                errno = EFAULT;
                return NULL;
        }

        exp = mnt3_mntpath_to_export (ms, path , _gf_false);
        if (exp != NULL)
                mnt3type = exp->exptype;

        if (mnt3type == MNT3_EXPTYPE_DIR) {
                char    volname [MNTPATHLEN] = {0, };
                char    *volptr = volname;

                /* Subdir export (nfs3.export-dirs) check */
                if (!gf_mnt3_export_dirs(ms)) {
                        errno = EACCES;
                        return NULL;
                }

                path = __volume_subdir (path, &volptr);
                if (exp == NULL)
                        exp = mnt3_mntpath_to_export (ms, volname , _gf_false);
        }

        if (exp == NULL) {
                errno = ENOENT;
                return NULL;
        }

        nfs = (struct nfs_state *)nfsx->private;
        if (!nfs_subvolume_started (nfs, exp->vol)) {
                errno = ENOENT;
                return NULL;
        }

        /* AUTH check: respect nfs.rpc-auth-allow/reject */
        ret = mnt3_check_client_net_udp (req, exp->vol->name, nfsx);
        if (ret == RPCSVC_AUTH_REJECT) {
                errno = EACCES;
                return NULL;
        }

        switch (mnt3type) {

        case MNT3_EXPTYPE_VOLUME:
                inode = __mnt3udp_get_export_volume_inode (req, path,
                                                           expname, exp);
                break;

        case MNT3_EXPTYPE_DIR:
                inode = __mnt3udp_get_export_subdir_inode (req, path,
                                                           expname, exp);
                break;

        default:
                /* Never reachable */
                gf_msg (GF_MNT, GF_LOG_ERROR, EFAULT, NFS_MSG_UNKNOWN_MNT_TYPE,
                        "Unknown MOUNT3 type");
                errno = EFAULT;
                goto err;
        }

        if (inode == NULL) {
                /* Don't over-write errno */
                if (!errno)
                        errno = ENOENT;
                goto err;
        }

        /* Build the inode from FH */
        fh = GF_CALLOC (1, sizeof(*fh), gf_nfs_mt_nfs3_fh);
        if (fh == NULL) {
                errno = ENOMEM;
                goto err;
        }

        (void) nfs3_build_fh (inode, exp->volumeid, fh);

err:
        if (inode)
                inode_unref (inode);

        return fh;
}

int
mount3udp_add_mountlist (xlator_t *nfsx, char *host, char *export)
{
        struct mountentry       *me = NULL;
        struct mount3_state     *ms = NULL;

        if ((!host) || (!export) || (!nfsx))
                return -1;

        ms = __mnt3udp_get_mstate (nfsx);
        if (!ms)
                return -1;

        me = GF_CALLOC (1, sizeof (*me), gf_nfs_mt_mountentry);
        if (!me)
                return -1;

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
mount3udp_delete_mountlist (xlator_t *nfsx, char *hostname, char *export)
{
        struct mount3_state     *ms = NULL;

        if ((!hostname) || (!export) || (!nfsx))
                return -1;

        ms = __mnt3udp_get_mstate (nfsx);
        if (!ms)
                return -1;

        mnt3svc_umount (ms, export, hostname);
        return 0;
}

/**
 * This function will parse the hostip (IP address, IP range, or hostname)
 * and fill the host_auth_spec structure.
 *
 * @param hostspec - struct host_auth_spec
 * @param hostip   - IP address, IP range (CIDR format) or hostname
 *
 * @return 0 - on success and -1 on failure
 *
 * NB: This does not support IPv6 currently.
 */
int
mnt3_export_fill_hostspec (struct host_auth_spec* hostspec, const char* hostip)
{
        char     *ipdupstr = NULL;
        char     *savptr = NULL;
        char     *endptr = NULL;
        char     *ip = NULL;
        char     *token = NULL;
        int      ret = -1;
        long     prefixlen = IPv4_ADDR_SIZE; /* default */
        uint32_t shiftbits = 0;
        size_t   length = 0;

        /* Create copy of the string so that the source won't change
         */
        ipdupstr = gf_strdup (hostip);
        if (NULL == ipdupstr) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Memory allocation failed");
                goto err;
        }

        ip = strtok_r (ipdupstr, "/", &savptr);
        /* Validate the Hostname or IPv4 address
         * TODO: IPv6 support for subdir auth.
         */
        length = strlen (ip);
        if ((!valid_ipv4_address (ip, (int)length, _gf_false)) &&
            (!valid_host_name (ip, (int)length))) {
                gf_msg (GF_MNT, GF_LOG_ERROR, EINVAL, NFS_MSG_INVALID_ENTRY,
                        "Invalid hostname or IPv4 address: %s", ip);
                goto err;
        }

        hostspec->host_addr = gf_strdup (ip);
        if (NULL == hostspec->host_addr) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Memory allocation failed");
                goto err;
        }

        /**
         * User provided CIDR address (xx.xx.xx.xx/n format) is split
         * into HOST (IP addr or hostname) and network prefix(n) from
         * which netmask would be calculated. This CIDR address may
         * denote a single, distinct interface address or the beginning
         * address of an entire network.
         *
         * e.g. the IPv4 block 192.168.100.0/24 represents the 256
         * IPv4 addresses from 192.168.100.0 to 192.168.100.255.
         * Therefore to check if an IP matches 192.168.100.0/24
         * we should mask the IP with FFFFFF00 and compare it with
         * host address part of CIDR.
         *
         * Refer: mask_match() in common-utils.c.
         */
        token = strtok_r (NULL, "/", &savptr);
        if (token != NULL) {
              prefixlen = strtol (token, &endptr, 10);
              if ((errno != 0) || (*endptr != '\0') ||
                  (prefixlen < 0) || (prefixlen > IPv4_ADDR_SIZE)) {
                        gf_msg (THIS->name, GF_LOG_WARNING, EINVAL,
                                NFS_MSG_INVALID_ENTRY,
                                "Invalid IPv4 subnetwork mask");
                      goto err;
              }
        }

        /*
         * 1. Calculate the network mask address.
         * 2. Convert it into Big-Endian format.
         * 3. Store it in hostspec netmask.
         */
        shiftbits = IPv4_ADDR_SIZE - prefixlen;
        hostspec->netmask = htonl ((uint32_t)~0 << shiftbits);

        ret = 0; /* SUCCESS */
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
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Memory allocation failed");
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
                        gf_msg (GF_MNT, GF_LOG_WARNING, 0,
                                NFS_MSG_PARSE_HOSTSPEC_FAIL,
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
                        gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM,
                                NFS_MSG_NO_MEMORY,
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
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Memory allocation failed");
                return NULL;
        }

        if (NULL != exportpath) {
                /* If exportpath is not NULL then we should check if AUTH
                 * parameter is present or not. If AUTH parameter is present
                 * then it will be stripped and stored in mnt3_export (exp)
                 * structure.
                 */
                if (0 != mnt3_export_parse_auth_param (exp, exportpath)){
                        gf_msg (GF_MNT, GF_LOG_ERROR, 0,
                                NFS_MSG_PARSE_AUTH_PARAM_FAIL,
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
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Memory allocation failed");
                goto err;
        }

        if (exportpath) {
                gf_msg_trace (GF_MNT, 0, "Initing dir export: %s:%s",
                              xl->name, exportpath);
                exp->exptype = MNT3_EXPTYPE_DIR;
                ret = snprintf (exp->expname, alloclen, "/%s%s", xl->name,
                                exportpath);
        } else {
                gf_msg_trace (GF_MNT, 0, "Initing volume export: %s",
                              xl->name);
                exp->exptype = MNT3_EXPTYPE_VOLUME;
                ret = snprintf (exp->expname, alloclen, "/%s", xl->name);
        }
        if (ret < 0) {
                gf_msg (xl->name, GF_LOG_ERROR, ret, NFS_MSG_SET_EXP_FAIL,
                        "Failed to set the export name");
                goto err;
        }
        /* Just copy without discrimination, we'll determine whether to
         * actually use it when a mount request comes in and a file handle
         * needs to be built.
         */
        gf_uuid_copy (exp->volumeid, volumeid);
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

        dupopt = strdupa (optstr);

        token = strtok_r (dupopt, ",", &savptr);
        while (token) {
                newexp = mnt3_init_export_ent (ms, xlator, token, volumeid);
                if (!newexp) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, 0,
                                NFS_MSG_INIT_DIR_EXP_FAIL, "Failed to init dir "
                                "export: %s", token);
                        ret = -1;
                        goto err;
                }

                list_add_tail (&newexp->explist, &ms->exportlist);
                token = strtok_r (NULL, ",", &savptr);
        }

        ret = 0;
err:
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

        gf_uuid_clear (volumeid);
        if (gf_nfs_dvm_off (nfs_state (ms->nfsx)))
                goto no_dvm;

        ret = snprintf (searchstr, 1024, "nfs3.%s.volume-id", xlator->name);
        if (ret < 0) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ret, NFS_MSG_SNPRINTF_FAIL,
                        "snprintf failed");
                ret = -1;
                goto err;
        }

        if (dict_get (opts, searchstr)) {
                ret = dict_get_str (opts, searchstr, &optstr);
                if (ret < 0) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, ret,
                                NFS_MSG_DICT_GET_FAILED, "Failed to read "
                                "option: %s", searchstr);
                        ret = -1;
                        goto err;
                }
        } else {
                gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_VOLID_MISSING,
                        "DVM is on but volume-id not "
                        "given for volume: %s", xlator->name);
                ret = -1;
                goto err;
        }

        if (optstr) {
                ret = gf_uuid_parse (optstr, volumeid);
                if (ret < 0) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, ret,
                                NFS_MSG_PARSE_VOL_UUID_FAIL, "Failed to parse "
                                "volume UUID");
                        ret = -1;
                        goto err;
                }
        }

no_dvm:
        ret = snprintf (searchstr, 1024, "nfs3.%s.export-dir", xlator->name);
        if (ret < 0) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ret, NFS_MSG_SNPRINTF_FAIL,
                        "snprintf failed");
                ret = -1;
                goto err;
        }

        if (dict_get (opts, searchstr)) {
                ret = dict_get_str (opts, searchstr, &optstr);
                if (ret < 0) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, ret,
                                NFS_MSG_DICT_GET_FAILED, "Failed to read "
                                "option: %s", searchstr);
                        ret = -1;
                        goto err;
                }

                ret = __mnt3_init_volume_direxports (ms, xlator, optstr,
                                                     volumeid);
                if (ret == -1) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, 0,
                                NFS_MSG_DIR_EXP_SETUP_FAIL, "Dir export "
                                "setup failed for volume: %s", xlator->name);
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
                gf_msg (GF_MNT, GF_LOG_ERROR, ret, NFS_MSG_DICT_GET_FAILED,
                        "Failed to read option: nfs3.export-volumes");
                ret = -1;
                goto err;
        }

        ret = gf_string2boolean (optstr, &boolt);
        if (ret < 0) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ret, NFS_MSG_STR2BOOL_FAIL,
                        "Failed to convert string to boolean");
        }

err:
        if (boolt == _gf_false) {
                gf_msg_trace (GF_MNT, 0, "Volume exports disabled");
                ms->export_volumes = 0;
        } else {
                gf_msg_trace (GF_MNT, 0, "Volume exports enabled");
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
                gf_msg (GF_MNT, GF_LOG_ERROR, ret, NFS_MSG_DICT_GET_FAILED,
                        "Failed to read option: nfs3.export-dirs");
                ret = -1;
                goto err;
        }

        ret = gf_string2boolean (optstr, &boolt);
        if (ret < 0) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ret, NFS_MSG_STR2BOOL_FAIL,
                        "Failed to convert string to boolean");
         }

err:
        if (boolt == _gf_false) {
                gf_msg_trace (GF_MNT, 0, "Dir exports disabled");
                ms->export_dirs = 0;
        } else {
                gf_msg_trace (GF_MNT, 0, "Dir exports enabled");
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
                gf_msg_trace (GF_MNT, 0, "Initing options for: %s",
                              volentry->xlator->name);
                ret = __mnt3_init_volume (ms, options, volentry->xlator);
                if (ret < 0) {
                        gf_msg (GF_MNT, GF_LOG_ERROR, ret,
                                NFS_MSG_VOL_INIT_FAIL,
                                "Volume init failed");
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
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Memory allocation failed");
                return NULL;
        }

        ms->iobpool = nfsx->ctx->iobuf_pool;
        ms->nfsx = nfsx;
        INIT_LIST_HEAD (&ms->exportlist);
        ret = mnt3_init_options (ms, nfsx->options);
        if (ret < 0) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ret, NFS_MSG_OPT_INIT_FAIL,
                        "Options init failed");
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
                gf_msg (GF_NFS, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to allocate mount state");
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

/**
 * __mnt3_mounted_exports_walk -- Walk through the mounted export directories
 *                                and unmount the directories that are no
 *                                longer authorized to be mounted.
 * @dict: The dict to walk
 * @key : The key we are on
 * @val : The value associated with that key
 * @tmp : Additional params (pointer to an auth params struct passed here)
 *
 */
int
__mnt3_mounted_exports_walk (dict_t *dict, char *key, data_t *val, void *tmp)
{
        char                     *path             = NULL;
        char                     *host_addr_ip     = NULL;
        char                     *keydup           = NULL;
        char                     *colon            = NULL;
        struct mnt3_auth_params  *auth_params      = NULL;
        int                       auth_status_code = 0;

        gf_msg_trace (GF_MNT, 0, "Checking if key %s is authorized.", key);

        auth_params = (struct mnt3_auth_params *)tmp;

        /* Since we haven't obtained a lock around the mount dict
         * here, we want to duplicate the key and then process it.
         * Otherwise we would potentially have a race condition
         * by modifying the key in the dict when other threads
         * are accessing it.
         */
        keydup = strdupa (key);

        colon = strchr (keydup, ':');
        if (!colon)
                return 0;

        *colon = '\0';

        path = alloca (strlen (keydup) + 2);
        snprintf (path, strlen (keydup) + 2, "/%s", keydup);

        /* Host is one character after ':' */
        host_addr_ip = colon + 1;
        auth_status_code = mnt3_auth_host (auth_params, host_addr_ip, NULL,
                                           path, _gf_false, NULL);
        if (auth_status_code != 0) {
                gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_AUTH_ERROR,
                        "%s is no longer authorized for %s",
                        host_addr_ip, path);
                mnt3svc_umount (auth_params->ms, path, host_addr_ip);
        }
        return 0;
}

/**
 * _mnt3_invalidate_old_mounts -- Calls __mnt3_mounted_exports_walk which checks
 *                                checks if hosts are authorized to be mounted
 *                                and umounts them.
 *
 * @ms: The mountstate for this service that holds all the information we need
 *
 */
void
_mnt3_invalidate_old_mounts (struct mount3_state *ms)
{
        gf_msg_debug (GF_MNT, 0, "Invalidating old mounts ...");
        dict_foreach (ms->mountdict, __mnt3_mounted_exports_walk,
                      ms->auth_params);
}


/**
 * _mnt3_has_file_changed -- Checks if a file has changed on disk
 *
 * @path: The path of the file on disk
 * @oldmtime: The previous mtime of the file
 *
 * @return: file changed: TRUE
 *          otherwise   : FALSE
 *
 * Uses get_file_mtime () in common-utils.c
 */
gf_boolean_t
_mnt3_has_file_changed (const char *path, time_t *oldmtime)
{
        gf_boolean_t    changed = _gf_false;
        time_t          mtime   = {0};
        int             ret     = 0;

        GF_VALIDATE_OR_GOTO (GF_MNT, path, out);
        GF_VALIDATE_OR_GOTO (GF_MNT, oldmtime, out);

        ret = get_file_mtime (path, &mtime);
        if (ret < 0)
                goto out;

        if (mtime != *oldmtime) {
                changed = _gf_true;
                *oldmtime = mtime;
        }
out:
        return changed;
}

/**
 * _mnt_auth_param_refresh_thread - Started using pthread_create () in
 *                                  mnt3svc_init (). Reloads exports/netgroups
 *                                  files from disk and sets the auth params
 *                                  structure in the mount state to reflect
 *                                  any changes from disk.
 * @argv: Unused argument
 * @return: Always returns NULL
 */
void *
_mnt3_auth_param_refresh_thread (void *argv)
{
        struct          mount3_state *mstate    = (struct mount3_state *)argv;
        char            *exp_file_path          = NULL;
        char            *ng_file_path           = NULL;
        size_t          nbytes                  = 0;
        time_t          exp_time                = 0;
        time_t          ng_time                 = 0;
        gf_boolean_t    any_file_changed        = _gf_false;
        int             ret                     = 0;

        nbytes = strlen (exports_file_path) + 1;
        exp_file_path = alloca (nbytes);
        snprintf (exp_file_path, nbytes, "%s", exports_file_path);

        nbytes = strlen (netgroups_file_path) + 1;
        ng_file_path = alloca (nbytes);
        snprintf (ng_file_path, nbytes, "%s", netgroups_file_path);

        /* Set the initial timestamps to avoid reloading right after
         * mnt3svc_init () spawns this thread */
        get_file_mtime (exp_file_path, &exp_time);
        get_file_mtime (ng_file_path, &ng_time);

        while (_gf_true) {
                if (mstate->stop_refresh)
                        break;
                any_file_changed = _gf_false;

                /* Sleep before checking the file again */
                sleep (mstate->nfs->auth_refresh_time_secs);

                if (_mnt3_has_file_changed (exp_file_path, &exp_time)) {
                        gf_msg (GF_MNT, GF_LOG_INFO, 0, NFS_MSG_UPDATING_EXP,
                                "File %s changed, updating exports,",
                                exp_file_path);

                        ret = mnt3_auth_set_exports_auth (mstate->auth_params,
                                                          exp_file_path);
                        if (ret)
                                gf_msg (GF_MNT, GF_LOG_ERROR, 0,
                                        NFS_MSG_SET_EXP_AUTH_PARAM_FAIL,
                                        "Failed to set export auth params.");
                        else
                                any_file_changed = _gf_true;
                }

                if (_mnt3_has_file_changed (ng_file_path, &ng_time)) {
                        gf_msg (GF_MNT, GF_LOG_INFO, 0,
                                NFS_MSG_UPDATING_NET_GRP, "File %s changed,"
                                "updating netgroups", ng_file_path);

                        ret = mnt3_auth_set_netgroups_auth (mstate->auth_params,
                                                            ng_file_path);
                        if (ret)
                                gf_msg (GF_MNT, GF_LOG_ERROR, 0,
                                        NFS_MSG_SET_NET_GRP_FAIL,
                                        "Failed to set netgroup auth params.");
                        else
                                any_file_changed = _gf_true;
                }

                /* If no files changed, go back to sleep */
                if (!any_file_changed)
                        continue;

                gf_msg (GF_MNT, GF_LOG_INFO, 0, NFS_MSG_PURGING_AUTH_CACHE,
                        "Purging auth cache.");
                auth_cache_purge (mstate->authcache);

                /* Walk through mounts that are no longer authorized
                 * and unmount them on the server side. This will
                 * cause subsequent file ops to fail with access denied.
                 */
                _mnt3_invalidate_old_mounts (mstate);
        }

        return NULL;
}

/**
 * _mnt3_init_auth_params -- Initialize authentication parameters by allocating
 *                           the struct and setting the exports & netgroups
 *                           files as parameters.
 *
 * @mstate : The mount state we are going to set the auth parameters in it.
 *
 * @return : success: 0 for success
 *           failure: -EINVAL for bad args, -ENOMEM for allocation errors, < 0
 *                    for other errors (parsing the files, etc.) These are
 *                    bubbled up from the functions we call to set the params.
 */
int
_mnt3_init_auth_params (struct mount3_state *mstate)
{
        int      ret            = -EINVAL;
        char     *exp_file_path = NULL;
        char     *ng_file_path  = NULL;
        size_t   nbytes         = 0;

        GF_VALIDATE_OR_GOTO (GF_MNT, mstate, out);

        mstate->auth_params = mnt3_auth_params_init (mstate);
        if (!mstate->auth_params) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to init mount auth params.");
                ret = -ENOMEM;
                goto out;
        }

        nbytes = strlen (exports_file_path) + 1;
        exp_file_path = alloca (nbytes);
        snprintf (exp_file_path, nbytes, "%s", exports_file_path);

        ret = mnt3_auth_set_exports_auth (mstate->auth_params, exp_file_path);
        if (ret < 0) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ret,
                        NFS_MSG_SET_EXP_AUTH_PARAM_FAIL,
                        "Failed to set export auth params.");
                goto out;
        }

        nbytes = strlen (netgroups_file_path) + 1;
        ng_file_path = alloca (nbytes);
        snprintf (ng_file_path, nbytes, "%s", netgroups_file_path);

        ret = mnt3_auth_set_netgroups_auth (mstate->auth_params, ng_file_path);
        if (ret < 0) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ret,
                        NFS_MSG_SET_EXP_AUTH_PARAM_FAIL,
                        "Failed to set netgroup auth params.");
                goto out;
        }

        ret = 0;
out:
        return ret;
}


/**
 * mnt3svc_deinit -- Function called by the nfs translator to cleanup all state
 *
 * @nfsx : The NFS translator used to perform the cleanup
 *         This structure holds all the pointers to memory that we need to free
 *         as well as the threads that have been started.
 */
void
mnt3svc_deinit (xlator_t *nfsx)
{
        struct mount3_state *mstate = NULL;
        struct nfs_state    *nfs    = NULL;

        if (!nfsx || !nfsx->private)
                return;

        nfs = (struct nfs_state *)nfsx->private;
        mstate = (struct mount3_state *)nfs->mstate;

        if (nfs->refresh_auth) {
                /* Mark as true and wait for thread to exit */
                mstate->stop_refresh = _gf_true;
                pthread_join (mstate->auth_refresh_thread, NULL);
        }

        if (nfs->exports_auth)
                mnt3_auth_params_deinit (mstate->auth_params);

        /* Unmount everything and clear mountdict */
        LOCK (&mstate->mountlock);
        {
                __mnt3svc_umountall (mstate);
                dict_unref (mstate->mountdict);
        }
        UNLOCK (&mstate->mountlock);

}

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

        gf_msg_debug (GF_MNT, 0, "Initing Mount v3 state");
        mstate = (struct mount3_state *)nfs->mstate;
        if (!mstate) {
                gf_msg (GF_MNT, GF_LOG_ERROR, 0, NFS_MSG_MNT_STATE_INIT_FAIL,
                        "Mount v3 state init failed");
                goto err;
        }

        mstate->nfs = nfs;

        mstate->mountdict = dict_new ();
        if (!mstate->mountdict) {
                gf_msg (GF_MNT, GF_LOG_ERROR, ENOMEM, NFS_MSG_NO_MEMORY,
                        "Failed to setup mount dict. Allocation error.");
                goto err;
        }

        if (nfs->exports_auth) {
                ret = _mnt3_init_auth_params (mstate);
                if (ret < 0)
                        goto err;

                mstate->authcache = auth_cache_init (nfs->auth_cache_ttl_sec);
                if (!mstate->authcache) {
                        ret = -ENOMEM;
                        goto err;
                }

                mstate->stop_refresh = _gf_false; /* Allow thread to run */
                pthread_create (&mstate->auth_refresh_thread, NULL,
                                _mnt3_auth_param_refresh_thread, mstate);
        } else
                gf_msg (GF_MNT, GF_LOG_INFO, 0, NFS_MSG_EXP_AUTH_DISABLED,
                        "Exports auth has been disabled!");

        mnt3prog.private = mstate;
        options = dict_new ();

        ret = gf_asprintf (&portstr, "%d", GF_MOUNTV3_PORT);
        if (ret == -1)
                goto err;

        ret = dict_set_dynstr (options, "transport.socket.listen-port",
                               portstr);
        if (ret == -1)
                goto err;

        ret = dict_set_str (options, "transport-type", "socket");
        if (ret == -1) {
                gf_msg (GF_NFS, GF_LOG_ERROR, errno, NFS_MSG_DICT_SET_FAILED,
                        "dict_set_str error");
                goto err;
        }

        if (nfs->allow_insecure) {
                ret = dict_set_str (options, "rpc-auth-allow-insecure", "on");
                if (ret == -1) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, errno,
                                NFS_MSG_DICT_SET_FAILED, "dict_set_str error");
                        goto err;
                }
                ret = dict_set_str (options, "rpc-auth.ports.insecure", "on");
                if (ret == -1) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, errno,
                                NFS_MSG_DICT_SET_FAILED, "dict_set_str error");
                        goto err;
                }
        }

        ret= rpcsvc_create_listeners (nfs->rpcsvc, options, nfsx->name);
        if (ret == -1) {
                gf_msg (GF_NFS, GF_LOG_ERROR, errno,
                        NFS_MSG_LISTENERS_CREATE_FAIL,
                        "Unable to create listeners");
                dict_unref (options);
                goto err;
        }

        if (nfs->mount_udp) {
                pthread_create (&udp_thread, NULL, mount3udp_thread, nfsx);
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

        gf_msg_debug (GF_MNT, GF_LOG_DEBUG, "Initing Mount v1 state");
        mstate = (struct mount3_state *)nfs->mstate;
        if (!mstate) {
                gf_msg (GF_MNT, GF_LOG_ERROR, EINVAL,
                        NFS_MSG_MNT_STATE_INIT_FAIL,
                        "Mount v3 state init failed");
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
                gf_msg (GF_NFS, GF_LOG_ERROR, errno, NFS_MSG_DICT_SET_FAILED,
                        "dict_set_str error");
                goto err;
        }

        if (nfs->allow_insecure) {
                ret = dict_set_str (options, "rpc-auth-allow-insecure", "on");
                if (ret == -1) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, errno,
                                NFS_MSG_DICT_SET_FAILED,
                                "dict_set_str error");
                        goto err;
                }
                ret = dict_set_str (options, "rpc-auth.ports.insecure", "on");
                if (ret == -1) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, errno,
                                NFS_MSG_DICT_SET_FAILED,
                                "dict_set_str error");
                        goto err;
                }
        }

        ret = rpcsvc_create_listeners (nfs->rpcsvc, options, nfsx->name);
        if (ret == -1) {
                gf_msg (GF_NFS, GF_LOG_ERROR, errno,
                        NFS_MSG_LISTENERS_CREATE_FAIL,
                        "Unable to create listeners");
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
                gf_msg (GF_MNT, GF_LOG_ERROR, ret, NFS_MSG_RECONF_FAIL,
                        "Options reconfigure failed");
                return (-1);
        }

        return (0);
}
