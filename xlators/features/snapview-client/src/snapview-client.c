 /*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "snapview-client.h"
#include "inode.h"
#include "byte-order.h"


static void
svc_local_free (svc_local_t *local)
{
        if (local) {
                loc_wipe (&local->loc);
                if (local->fd)
                        fd_unref (local->fd);
                if (local->xdata)
                        dict_unref (local->xdata);
                mem_put (local);
        }
}

static xlator_t *
svc_get_subvolume (xlator_t *this, int inode_type)
{
        xlator_t *subvolume = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-client", this, out);

        if (inode_type == VIRTUAL_INODE)
                subvolume = SECOND_CHILD (this);
        else
                subvolume = FIRST_CHILD (this);

out:
        return subvolume;
}

static int32_t
__svc_inode_ctx_set (xlator_t *this, inode_t *inode, int inode_type)
{
        uint64_t    value = 0;
        int32_t       ret = -1;

        GF_VALIDATE_OR_GOTO ("snapview-client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        value = inode_type;

        ret = __inode_ctx_set (inode, this, &value);

out:
        return ret;
}

static int
__svc_inode_ctx_get (xlator_t *this, inode_t *inode, int *inode_type)
{
        uint64_t    value      = 0;
        int         ret        = -1;

        GF_VALIDATE_OR_GOTO ("snapview-client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        ret = __inode_ctx_get (inode, this, &value);
        if (ret < 0)
                goto out;

        *inode_type = (int)(value);

out:
        return ret;
}

static int
svc_inode_ctx_get (xlator_t *this, inode_t *inode, int *inode_type)
{
        int          ret        = -1;

        GF_VALIDATE_OR_GOTO ("snapview-client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        LOCK (&inode->lock);
        {
                ret = __svc_inode_ctx_get (this, inode, inode_type);
        }
        UNLOCK (&inode->lock);

out:
        return ret;
}

static int32_t
svc_inode_ctx_set (xlator_t *this, inode_t *inode, int inode_type)
{
        int32_t   ret = -1;

        GF_VALIDATE_OR_GOTO ("snapview-client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        LOCK (&inode->lock);
        {
                ret = __svc_inode_ctx_set (this, inode, inode_type);
        }
        UNLOCK (&inode->lock);

out:
        return ret;
}

static svc_fd_t *
svc_fd_new (void)
{
        svc_fd_t    *svc_fd = NULL;

        svc_fd = GF_CALLOC (1, sizeof (*svc_fd), gf_svc_mt_svc_fd_t);

        return svc_fd;
}

static svc_fd_t *
__svc_fd_ctx_get (xlator_t *this, fd_t *fd)
{
        svc_fd_t *svc_fd = NULL;
        uint64_t  value  = 0;
        int       ret    = -1;

        GF_VALIDATE_OR_GOTO ("snapview-client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        ret = __fd_ctx_get (fd, this, &value);
        if (ret)
                return NULL;

        svc_fd = (svc_fd_t *) ((long) value);

out:
        return svc_fd;
}

static svc_fd_t *
svc_fd_ctx_get (xlator_t *this, fd_t *fd)
{
        svc_fd_t *svc_fd = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        LOCK (&fd->lock);
        {
                svc_fd = __svc_fd_ctx_get (this, fd);
        }
        UNLOCK (&fd->lock);

out:
        return svc_fd;
}

static int
__svc_fd_ctx_set (xlator_t *this, fd_t *fd, svc_fd_t *svc_fd)
{
        uint64_t    value = 0;
        int         ret   = -1;

        GF_VALIDATE_OR_GOTO ("snapview-client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, svc_fd, out);

        value = (uint64_t)(long) svc_fd;

        ret = __fd_ctx_set (fd, this, value);

out:
        return ret;
}

static svc_fd_t *
__svc_fd_ctx_get_or_new (xlator_t *this, fd_t *fd)
{
        svc_fd_t        *svc_fd    = NULL;
        int              ret       = -1;
        inode_t         *inode     = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        inode = fd->inode;
        svc_fd = __svc_fd_ctx_get (this, fd);
        if (svc_fd) {
                ret = 0;
                goto out;
        }

        svc_fd = svc_fd_new ();
        if (!svc_fd) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate new fd "
                        "context for gfid %s", uuid_utoa (inode->gfid));
                goto out;
        }

        ret = __svc_fd_ctx_set (this, fd, svc_fd);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to set fd context "
                        "for gfid %s", uuid_utoa (inode->gfid));
                ret = -1;
        }

out:
        if (ret) {
                GF_FREE (svc_fd);
                svc_fd = NULL;
        }

        return svc_fd;
}

static svc_fd_t *
svc_fd_ctx_get_or_new (xlator_t *this, fd_t *fd)
{
        svc_fd_t  *svc_fd = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        LOCK (&fd->lock);
        {
                svc_fd = __svc_fd_ctx_get_or_new (this, fd);
        }
        UNLOCK (&fd->lock);

out:
        return svc_fd;
}


static int32_t
gf_svc_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        svc_local_t     *local       = NULL;
        xlator_t        *subvolume   = NULL;
        gf_boolean_t     do_unwind   = _gf_true;
        int              inode_type  = -1;
        int              ret         = -1;

        local = frame->local;
        subvolume = local->subvolume;
        if (!subvolume) {
                gf_log_callingfn (this->name, GF_LOG_ERROR, "path: %s, "
                                  "gfid: %s ", local->loc.path,
                                  inode?uuid_utoa (inode->gfid):"");
                GF_ASSERT (0);
        }

        /* There is a possibility that, the client process just came online
           and does not have the inode on which the lookup came. In that case,
           the fresh inode created from fuse for the lookup fop, wont have
           the inode context set without which svc cannot decide where to
           STACK_WIND to. So by default it decides to send the fop to the
           regular subvolume (i.e first child of the xlator). If lookup fails
           on the regular volume, then there is a possibility that the lookup
           is happening on a virtual inode (i.e history data residing in snaps).
           So if lookup fails with ENOENT and the inode context is not there,
           then send the lookup to the 2nd child of svc.

           If there are any changes in volfile/client-restarted then inode-ctx
           is lost. In this case if nameless lookup fails with ESTALE,
           then send the lookup to the 2nd child of svc.
        */
        if (op_ret) {
                if (subvolume == FIRST_CHILD (this)) {
                        gf_log (this->name,
                                (op_errno == ENOENT || op_errno == ESTALE)
                                ? GF_LOG_DEBUG:GF_LOG_ERROR,
                                "Lookup failed on normal graph with error %s",
                                strerror (op_errno));
                } else {
                        gf_log (this->name,
                                (op_errno == ENOENT || op_errno == ESTALE)
                                ? GF_LOG_DEBUG:GF_LOG_ERROR,
                                "Lookup failed on snapview graph with error %s",
                                strerror (op_errno));
                        goto out;
                }

                if ((op_errno == ENOENT || op_errno == ESTALE) &&
                    !gf_uuid_is_null (local->loc.gfid)) {
                        if (inode != NULL)
                                ret = svc_inode_ctx_get (this, inode,
                                                                &inode_type);

                        if (ret < 0 || inode == NULL) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "Lookup on normal graph failed. "
                                        "Sending lookup to snapview-server");

                                subvolume = SECOND_CHILD (this);
                                local->subvolume = subvolume;
                                STACK_WIND (frame, gf_svc_lookup_cbk,
                                            subvolume, subvolume->fops->lookup,
                                            &local->loc, xdata);
                                do_unwind = _gf_false;
                        }
                }

                goto out;
        }

        if (subvolume == FIRST_CHILD (this))
                inode_type = NORMAL_INODE;
        else
                inode_type = VIRTUAL_INODE;

        ret = svc_inode_ctx_set (this, inode, inode_type);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "failed to set inode type"
                        "into the context");

out:
        if (do_unwind) {
                SVC_STACK_UNWIND (lookup, frame, op_ret, op_errno, inode, buf,
                                  xdata, postparent);
        }

        return 0;
}

static int32_t
gf_svc_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        int32_t        ret         =   -1;
        svc_local_t   *local       = NULL;
        xlator_t      *subvolume   = NULL;
        int            op_ret      = -1;
        int            op_errno    = EINVAL;
        inode_t       *parent      = NULL;
        svc_private_t *priv        = NULL;
        dict_t        *new_xdata   = NULL;
        int            inode_type  = -1;
        int            parent_type = -1;
        gf_boolean_t   wind        = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        priv = this->private;

        ret = svc_inode_ctx_get (this, loc->inode, &inode_type);
        if (!__is_root_gfid (loc->gfid)) {
                if (loc->parent) {
                        parent = inode_ref (loc->parent);
                        ret = svc_inode_ctx_get (this, loc->parent,
                                                 &parent_type);
                } else {
                        parent = inode_parent (loc->inode, loc->pargfid, NULL);
                        if (parent)
                                ret = svc_inode_ctx_get (this, parent,
                                                         &parent_type);
                }
        }

        local = mem_get0 (this->local_pool);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate local");
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        frame->local = local;
        loc_copy (&local->loc, loc);

        if (__is_root_gfid (loc->inode->gfid)) {
                subvolume = FIRST_CHILD (this);
                GF_ASSERT (subvolume);
                local->subvolume = subvolume;
                wind = _gf_true;
                goto out;
        }

        /* nfs sends nameless lookups directly using the gfid. In that case
           loc->name will be NULL. So check if loc->name is NULL. If so, then
           try to get the subvolume using inode context. But if the inode has
           not been looked up yet, then send the lookup call to the first
           subvolume.
        */

        if (!loc->name) {
                if (gf_uuid_is_null (loc->inode->gfid)) {
                        subvolume = FIRST_CHILD (this);
                        local->subvolume = subvolume;
                        wind = _gf_true;
                        goto out;
                } else {
                        if (inode_type >= 0)
                                subvolume = svc_get_subvolume (this,
                                                               inode_type);
                        else
                                subvolume = FIRST_CHILD (this);
                        local->subvolume = subvolume;
                        wind = _gf_true;
                        goto out;
                }
        }

        if (strcmp (loc->name, priv->path)) {
                if (parent_type == NORMAL_INODE) {
                        subvolume = FIRST_CHILD (this);
                        local->subvolume = subvolume;
                } else {
                        subvolume = SECOND_CHILD (this);
                        local->subvolume = subvolume;
                }
        } else {
                subvolume = SECOND_CHILD (this);
                local->subvolume = subvolume;
                if (parent_type == NORMAL_INODE) {
                        /* Indication of whether the lookup is happening on the
                           entry point or not, to the snapview-server.
                        */
                        SVC_ENTRY_POINT_SET (this, xdata, op_ret, op_errno,
                                             new_xdata, priv, ret, out);
                }
        }

        wind = _gf_true;

out:
        if (wind)
                STACK_WIND (frame, gf_svc_lookup_cbk, subvolume,
                            subvolume->fops->lookup, loc, xdata);
        else
                SVC_STACK_UNWIND (lookup, frame, op_ret, op_errno, NULL,
                                  NULL, NULL, NULL);
        if (new_xdata)
                dict_unref (new_xdata);

        if (parent)
                inode_unref (parent);

        return 0;
}

static int32_t
gf_svc_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        xlator_t      *subvolume  = NULL;
        int32_t        ret        = -1;
        int            inode_type = -1;
        int32_t        op_ret     = -1;
        int32_t        op_errno   = EINVAL;
        gf_boolean_t   wind       = _gf_false;
        svc_private_t  *priv      = NULL;
        const char     *path      = NULL;
        int             path_len  = -1;
        int             snap_len  = -1;
        loc_t           root_loc  = {0,};
        loc_t          *temp_loc  = NULL;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        priv = this->private;
        SVC_GET_SUBVOL_FROM_CTX (this, op_ret, op_errno, inode_type, ret,
                                 loc->inode, subvolume, out);
        path_len = strlen (loc->path);
        snap_len = strlen (priv->path);
        temp_loc = loc;

        if (path_len >= snap_len && inode_type == VIRTUAL_INODE) {
                path = &loc->path[path_len - snap_len];
                if (!strcmp (path, priv->path)) {
                        /*
                         * statfs call for virtual snap directory.
                         * Sent the fops to parent volume by removing
                         * virtual directory from path
                         */
                        subvolume = FIRST_CHILD (this);
                        root_loc.path = gf_strdup("/");
                        gf_uuid_clear(root_loc.gfid);
                        root_loc.gfid[15] = 1;
                        root_loc.inode = inode_ref (loc->inode->table->root);
                        temp_loc = &root_loc;
                }
        }

        STACK_WIND_TAIL (frame, subvolume, subvolume->fops->statfs,
                         temp_loc, xdata);
        if (temp_loc == &root_loc)
                loc_wipe (temp_loc);

        wind = _gf_true;
out:
        if (!wind)
                SVC_STACK_UNWIND (statfs, frame, op_ret, op_errno,
                                  NULL, NULL);
        return 0;
}

static int32_t
gf_svc_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *buf,
                 dict_t *xdata)
{
        /* Consider a testcase:
         * #mount -t nfs host1:/vol1 /mnt
         * #ls /mnt
         * #ls /mnt/.snaps (As expected this fails)
         * #gluster volume set vol1 features.uss enable
         * Now `ls /mnt/.snaps` should work,
         * but fails with No such file or directory.
         * This is because NFS client caches the list of files in
         * a directory. This cache is updated if there are any changes
         * in the directory attributes. To solve this problem change
         * a attribute 'ctime' when USS is enabled
         */
        if (op_ret == 0 && IA_ISDIR(buf->ia_type))
                buf->ia_ctime_nsec++;

        SVC_STACK_UNWIND (stat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}

/* should all the fops be handled like lookup is supposed to be
   handled? i.e just based on inode type decide where the call should
   be sent and in the call back update the contexts.
*/
static int32_t
gf_svc_stat (call_frame_t *frame, xlator_t *this, loc_t *loc,
             dict_t *xdata)
{
        int32_t      ret        = -1;
        int          inode_type = -1;
        xlator_t    *subvolume  = NULL;
        int32_t      op_ret     = -1;
        int32_t      op_errno   = EINVAL;
        gf_boolean_t wind       = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        SVC_GET_SUBVOL_FROM_CTX (this, op_ret, op_errno, inode_type, ret,
                                 loc->inode, subvolume, out);

        STACK_WIND (frame, gf_svc_stat_cbk, subvolume,
                    subvolume->fops->stat, loc, xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (stat, frame, op_ret, op_errno,
                                  NULL, NULL);
        return 0;
}

static int32_t
gf_svc_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        int32_t      ret        = -1;
        int          inode_type = -1;
        xlator_t    *subvolume  = NULL;
        int32_t      op_ret     = -1;
        int32_t      op_errno   = EINVAL;
        gf_boolean_t wind       = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);

        SVC_GET_SUBVOL_FROM_CTX (this, op_ret, op_errno, inode_type, ret,
                                 fd->inode, subvolume, out);

        STACK_WIND_TAIL (frame, subvolume, subvolume->fops->fstat, fd, xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (fstat, frame, op_ret, op_errno, NULL, NULL);

        return ret;
}

static int32_t
gf_svc_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        svc_fd_t        *svc_fd          = NULL;
        svc_local_t     *local           = NULL;
        svc_private_t   *priv            = NULL;
        gf_boolean_t     special_dir     = _gf_false;
        char             path[PATH_MAX]  = {0, };

        GF_VALIDATE_OR_GOTO ("snapview-client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        if (op_ret)
                goto out;

        priv = this->private;
        local = frame->local;

        if (local->subvolume == FIRST_CHILD (this) && priv->special_dir
            && strcmp (priv->special_dir, "")) {
                if (!__is_root_gfid (fd->inode->gfid))
                        snprintf (path, sizeof (path), "%s/.",
                                  priv->special_dir);
                else
                        snprintf (path, sizeof (path), "/.");

                if (!strcmp (local->loc.path, priv->special_dir) ||
                    !strcmp (local->loc.path, path)) {
                        gf_log_callingfn (this->name, GF_LOG_DEBUG,
                                          "got opendir on special "
                                          "directory %s (%s)", path,
                                          uuid_utoa (fd->inode->gfid));
                        special_dir = _gf_true;
                }
        }

        if (special_dir) {
                svc_fd = svc_fd_ctx_get_or_new (this, fd);
                if (!svc_fd) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "fd context not found for %s",
                                uuid_utoa (fd->inode->gfid));
                        goto out;
                }

                svc_fd->last_offset = -1;
                svc_fd->special_dir = special_dir;
        }

out:
        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd, xdata);

        return 0;
}


/* If the inode represents a directory which is actually
   present in a snapshot, then opendir on that directory
   should be sent to the snap-view-server which opens
   the directory in the corresponding graph.
   In fact any opendir call on a virtual directory
   should be sent to svs. Because if it fakes success
   here, then later when readdir on that fd comes, there
   will not be any corresponding fd opened on svs and
   svc has to do things that open-behind is doing.
*/
static int32_t
gf_svc_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
                dict_t *xdata)
{
        int32_t        ret        = -1;
        int            inode_type = -1;
        xlator_t      *subvolume  = NULL;
        int            op_ret     = -1;
        int            op_errno   = EINVAL;
        gf_boolean_t   wind       = _gf_false;
        svc_local_t   *local      = NULL;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        local = mem_get0 (this->local_pool);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate memory "
                        "for local (path: %s, gfid: %s)", loc->path,
                        uuid_utoa (fd->inode->gfid));
                op_errno = ENOMEM;
                goto out;
        }

        SVC_GET_SUBVOL_FROM_CTX (this, op_ret, op_errno, inode_type, ret,
                                 loc->inode, subvolume, out);

        loc_copy (&local->loc, loc);
        local->subvolume = subvolume;
        frame->local = local;

        STACK_WIND (frame, gf_svc_opendir_cbk, subvolume,
                    subvolume->fops->opendir, loc, fd, xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (opendir, frame, op_ret, op_errno, NULL, NULL);

        return 0;
}

static int32_t
gf_svc_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        int32_t      ret        = -1;
        int          inode_type = -1;
        int          op_ret     = -1;
        int          op_errno   = EINVAL;
        gf_boolean_t wind       = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        ret = svc_inode_ctx_get (this, loc->inode, &inode_type);
        if (ret < 0) {
                op_ret = -1;
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_ERROR, "failed to get the inode "
                        "context for %s (gfid: %s)", loc->path,
                        uuid_utoa (loc->inode->gfid));
                goto out;
        }

        if (inode_type == NORMAL_INODE) {
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD (this)->fops->setattr, loc, stbuf,
                                 valid, xdata);
        } else {
                op_ret = -1;
                op_errno = EROFS;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (setattr, frame, op_ret, op_errno,
                                  NULL, NULL, NULL);
        return 0;
}

/* XXX: This function is currently not used. Remove "#if 0" when required */
#if 0
static int32_t
gf_svc_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        int32_t      ret        = -1;
        int          inode_type = -1;
        int          op_ret     = -1;
        int          op_errno   = EINVAL;
        gf_boolean_t wind       = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);

        ret = svc_inode_ctx_get (this, fd->inode, &inode_type);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the inode "
                        "context for %s", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (inode_type == NORMAL_INODE) {
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD (this)->fops->fsetattr, fd, stbuf,
                                 valid, xdata);
        } else {
                op_ret = -1;
                op_errno = EROFS;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (fsetattr, frame, op_ret, op_errno,
                                  NULL, NULL, NULL);
        return 0;
}
#endif /* gf_svc_fsetattr() is not used */

static int32_t
gf_svc_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                 const char *name, dict_t *xdata)
{
        int32_t          ret                    = -1;
        int              inode_type             = -1;
        xlator_t        *subvolume              = NULL;
        int              op_ret                 = -1;
        int              op_errno               = EINVAL;
        gf_boolean_t     wind                   = _gf_false;
        svc_private_t   *priv                   = NULL;
        char             attrname[PATH_MAX]     = "";
        char             attrval[64]            = "";
        dict_t          *dict                   = NULL;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);
        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        /*
         * Samba sends this special key for case insensitive
         * filename check. This request comes with a parent
         * path and with a special key GF_XATTR_GET_REAL_FILENAME_KEY.
         * e.g. "glusterfs.get_real_filename:.snaps".
         * If the name variable matches this key then we have
         * to send back .snaps as the real filename.
         */
        if (!name)
                goto stack_wind;

        sscanf (name, "%[^:]:%[^@]", attrname, attrval);
        strcat (attrname, ":");

        if (!strcmp (attrname, GF_XATTR_GET_REAL_FILENAME_KEY)) {
                if (!strcasecmp (attrval, priv->path)) {
                        dict = dict_new ();
                        if (NULL == dict) {
                                op_errno = ENOMEM;
                                goto out;
                        }

                        ret = dict_set_dynstr_with_alloc (dict,
                                        (char *)name,
                                        priv->path);

                        if (ret) {
                                op_errno = ENOMEM;
                                goto out;
                        }

                        op_errno = 0;
                        op_ret = strlen (priv->path) + 1;
                        /* We should return from here */
                        goto out;
                }
        }
stack_wind:
        SVC_GET_SUBVOL_FROM_CTX (this, op_ret, op_errno, inode_type, ret,
                                 loc->inode, subvolume, out);

        STACK_WIND_TAIL (frame, subvolume, subvolume->fops->getxattr, loc, name,
                         xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (getxattr, frame, op_ret, op_errno,
                                  dict, NULL);

        if (dict)
                dict_unref (dict);

        return 0;
}

/* XXX: This function is currently not used. Mark it '#if 0' when required */
#if 0
static int32_t
gf_svc_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  const char *name, dict_t *xdata)
{
        int32_t       ret        = -1;
        int           inode_type = -1;
        xlator_t     *subvolume  = NULL;
        gf_boolean_t  wind       = _gf_false;
        int           op_ret     = -1;
        int           op_errno   = EINVAL;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);

        SVC_GET_SUBVOL_FROM_CTX (this, op_ret, op_errno, inode_type, ret,
                                 fd->inode, subvolume, out);

        STACK_WIND_TAIL (frame, subvolume,
                         subvolume->fops->fgetxattr, fd, name, xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (fgetxattr, frame, op_ret, op_errno,
                                  NULL, NULL);
        return 0;
}
#endif /* gf_svc_fgetxattr() is not used */

static int32_t
gf_svc_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
                 int32_t flags, dict_t *xdata)
{
        int32_t      ret         = -1;
        int          inode_type  = -1;
        int          op_ret      = -1;
        int          op_errno    = EINVAL;
        gf_boolean_t wind        = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        ret = svc_inode_ctx_get (this, loc->inode, &inode_type);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get inode context "
                        "for %s (gfid: %s)", loc->name,
                        uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (inode_type == NORMAL_INODE) {
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD (this)->fops->setxattr, loc, dict,
                                 flags, xdata);
        } else {
                op_ret = -1;
                op_errno = EROFS;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (setxattr, frame, op_ret, op_errno,
                                  NULL);

        return 0;
}

static int32_t
gf_svc_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
                  int32_t flags, dict_t *xdata)
{
        int32_t      ret        = -1;
        int          inode_type = -1;
        int          op_ret     = -1;
        int          op_errno   = EINVAL;
        gf_boolean_t wind       = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);

        ret = svc_inode_ctx_get (this, fd->inode, &inode_type);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get inode context "
                        "for %s", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (inode_type == NORMAL_INODE) {
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD (this)->fops->fsetxattr, fd, dict,
                                 flags, xdata);
        } else {
                op_ret = -1;
                op_errno = EROFS;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno,
                                     NULL);

        return 0;
}

static int32_t
gf_svc_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
              dict_t *xdata)
{
        int          inode_type = -1;
        int          ret        = -1;
        int          op_ret     = -1;
        int          op_errno   = EINVAL;
        gf_boolean_t wind       = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        ret = svc_inode_ctx_get (this, loc->inode, &inode_type);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the inode "
                        "context for %s (gfid: %s)", loc->name,
                        uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (inode_type == NORMAL_INODE) {
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD (this)->fops->rmdir, loc, flags,
                                 xdata);
        } else {
                op_ret = -1;
                op_errno = EROFS;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (rmdir, frame, op_ret, op_errno,
                                  NULL, NULL, NULL);
        return 0;
}

static int32_t
gf_svc_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        int inode_type = -1;
        int ret        = -1;

        if (op_ret < 0)
                goto out;

        inode_type = NORMAL_INODE;
        ret = svc_inode_ctx_set (this, inode, inode_type);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "failed to set inode "
                        "context");


out:
        SVC_STACK_UNWIND (mkdir, frame, op_ret, op_errno, inode,
                          buf, preparent, postparent, xdata);
        return 0;
}

static int32_t
gf_svc_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
              mode_t umask, dict_t *xdata)
{
        int            parent_type = -1;
        int            ret         = -1;
        int            op_ret      = -1;
        int            op_errno    = EINVAL;
        svc_private_t *priv        = NULL;
        gf_boolean_t   wind        = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        priv = this->private;

        ret = svc_inode_ctx_get (this, loc->parent, &parent_type);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the inode "
                        "context for %s", uuid_utoa (loc->parent->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (strcmp (loc->name, priv->path) && parent_type == NORMAL_INODE) {
                STACK_WIND (frame, gf_svc_mkdir_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->mkdir, loc, mode,
                            umask, xdata);
        } else {
                op_ret = -1;
                op_errno = EROFS;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (mkdir, frame, op_ret, op_errno, NULL, NULL,
                                  NULL, NULL, NULL);
        return 0;
}

static int32_t
gf_svc_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        int inode_type = -1;
        int ret        = -1;

        if (op_ret < 0)
                goto out;

        inode_type = NORMAL_INODE;
        ret = svc_inode_ctx_set (this, inode, inode_type);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "failed to set inode "
                        "context");

out:
        SVC_STACK_UNWIND (mknod, frame, op_ret, op_errno, inode,
                          buf, preparent, postparent, xdata);
        return 0;
}

static int32_t
gf_svc_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
              dev_t rdev, mode_t umask, dict_t *xdata)
{
        int            parent_type = -1;
        int            ret         = -1;
        int            op_ret      = -1;
        int            op_errno    = EINVAL;
        svc_private_t *priv        = NULL;
        gf_boolean_t   wind        = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        priv = this->private;

        ret = svc_inode_ctx_get (this, loc->parent, &parent_type);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the inode "
                        "context for %s", uuid_utoa (loc->parent->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (strcmp (loc->name, priv->path) && parent_type == NORMAL_INODE) {
                STACK_WIND (frame, gf_svc_mknod_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->mknod, loc, mode,
                            rdev, umask, xdata);
        } else {
                op_ret = -1;
                op_errno = EROFS;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (mknod, frame, op_ret, op_errno, NULL, NULL,
                                  NULL, NULL, NULL);
        return 0;
}

/* If the flags of the open call contain O_WRONLY or O_RDWR and the inode is
   a virtual inode, then unwind the call back with EROFS. Otherwise simply
   STACK_WIND the call to the first child of svc xlator.
*/
static int32_t
gf_svc_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
             fd_t *fd, dict_t *xdata)
{
        xlator_t    *subvolume  = NULL;
        int          inode_type = -1;
        int          op_ret     = -1;
        int          op_errno   = EINVAL;
        int          ret        = -1;
        gf_boolean_t wind       = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        /* Another way is to STACK_WIND to normal subvolume, if inode
           type is not there in the context. If the file actually resides
           in snapshots, then ENOENT would be returned. Needs more analysis.
        */
        SVC_GET_SUBVOL_FROM_CTX (this, op_ret, op_errno, inode_type, ret,
                                 loc->inode, subvolume, out);

        if (((flags & O_ACCMODE) == O_WRONLY) ||
            ((flags & O_ACCMODE) == O_RDWR)) {
                if (subvolume != FIRST_CHILD (this)) {
                        op_ret = -1;
                        op_errno = EINVAL;
                        goto out;
                }
        }

        STACK_WIND_TAIL (frame, subvolume, subvolume->fops->open, loc,
                         flags, fd, xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (open, frame, op_ret, op_errno, NULL,
                                  NULL);
        return 0;
}

static int32_t
gf_svc_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                   struct iatt *stbuf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        int  inode_type = -1;
        int  ret        = -1;

        if (op_ret < 0)
                goto out;

        inode_type = NORMAL_INODE;
        ret = svc_inode_ctx_set (this, inode, inode_type);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "failed to set inode "
                        "context");

out:
        SVC_STACK_UNWIND (create, frame, op_ret, op_errno, fd,
                          inode, stbuf, preparent, postparent, xdata);

        return 0;
}

static int32_t
gf_svc_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
               mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        int            parent_type = -1;
        int            ret         = -1;
        int            op_ret      = -1;
        int            op_errno    = EINVAL;
        svc_private_t *priv        = NULL;
        gf_boolean_t   wind        = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        priv = this->private;

        ret = svc_inode_ctx_get (this, loc->parent, &parent_type);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the inode "
                        "context for %s", uuid_utoa (loc->parent->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (strcmp (loc->name, priv->path) && parent_type == NORMAL_INODE) {
                STACK_WIND (frame, gf_svc_create_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->create, loc, flags,
                            mode, umask, fd, xdata);
        } else {
                op_ret = -1;
                op_errno = EROFS;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (create, frame, op_ret, op_errno,
                                  NULL, NULL, NULL, NULL, NULL, NULL);
        return 0;
}

static int32_t
gf_svc_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
        int inode_type = -1;
        int ret        = -1;

        if (op_ret < 0)
                goto out;

        inode_type = NORMAL_INODE;
        ret = svc_inode_ctx_set (this, inode, inode_type);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "failed to set inode "
                        "context");

out:
        SVC_STACK_UNWIND (symlink, frame, op_ret, op_errno, inode,
                          buf, preparent, postparent, xdata);

        return 0;
}

static int32_t
gf_svc_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
                loc_t *loc, mode_t umask, dict_t *xdata)
{
        int            parent_type = -1;
        int            op_ret      = -1;
        int            op_errno    = EINVAL;
        int            ret         = -1;
        svc_private_t *priv        = NULL;
        gf_boolean_t   wind        = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        priv = this->private;

        ret = svc_inode_ctx_get (this, loc->parent, &parent_type);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the inode "
                        "context for %s", uuid_utoa (loc->parent->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (strcmp (loc->name, priv->path) && parent_type == NORMAL_INODE) {
                STACK_WIND (frame, gf_svc_symlink_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->symlink, linkpath, loc,
                            umask, xdata);
        } else {
                op_ret = -1;
                op_errno = EROFS;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (symlink, frame, op_ret, op_errno,
                                  NULL, NULL, NULL, NULL, NULL);
        return 0;
}

static int32_t
gf_svc_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
               dict_t *xdata)
{
        int            inode_type   = -1;
        int            op_ret       = -1;
        int            op_errno     = EINVAL;
        int            ret          = -1;
        gf_boolean_t   wind         = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        ret = svc_inode_ctx_get (this, loc->inode, &inode_type);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the inode "
                        "context for %s", uuid_utoa (loc->parent->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (inode_type == NORMAL_INODE) {
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD (this)->fops->unlink, loc, flags,
                                 xdata);
        } else {
                op_ret = -1;
                op_errno = EROFS;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (unlink, frame, op_ret, op_errno, NULL, NULL,
                                  NULL);
        return 0;
}

static int32_t
gf_svc_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset, uint32_t flags, dict_t *xdata)
{
        int           inode_type = -1;
        xlator_t     *subvolume  = NULL;
        int           ret        = -1;
        int           op_ret     = -1;
        int           op_errno   = EINVAL;
        gf_boolean_t  wind       = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);

        SVC_GET_SUBVOL_FROM_CTX (this, op_ret, op_errno, inode_type, ret,
                                 fd->inode, subvolume, out);

        STACK_WIND_TAIL (frame, subvolume, subvolume->fops->readv,
                         fd, size, offset, flags, xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (readv, frame, op_ret, op_errno, NULL, 0, NULL,
                                  NULL, NULL);
        return 0;
}

static int32_t
gf_svc_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
                 dict_t *xdata)
{
        int              inode_type = -1;
        xlator_t        *subvolume  = NULL;
        int              ret        = -1;
        int              op_ret     = -1;
        int              op_errno   = EINVAL;
        gf_boolean_t     wind       = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        SVC_GET_SUBVOL_FROM_CTX (this, op_ret, op_errno, inode_type, ret,
                                 loc->inode, subvolume, out);

        STACK_WIND_TAIL (frame, subvolume, subvolume->fops->readlink, loc, size,
                         xdata);

        wind = _gf_true;

out:
        if (!wind)
                STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, NULL, NULL,
                                     NULL);
        return 0;
}

static int32_t
gf_svc_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
               dict_t *xdata)
{
        int            ret        = -1;
        int            inode_type = -1;
        xlator_t      *subvolume  = NULL;
        int            op_ret     = -1;
        int            op_errno   = EINVAL;
        gf_boolean_t   wind       = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        SVC_GET_SUBVOL_FROM_CTX (this, op_ret, op_errno, inode_type, ret,
                                 loc->inode, subvolume, out);

        STACK_WIND_TAIL (frame, subvolume, subvolume->fops->access, loc, mask,
                         xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (access, frame, op_ret, op_errno, NULL);

        return 0;
}

int32_t
gf_svc_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                    dict_t *xdata)
{
        gf_dirent_t   *entry      = NULL;
        gf_dirent_t   *tmpentry  = NULL;
        svc_local_t   *local      = NULL;
        svc_private_t *priv       = NULL;

        if (op_ret < 0)
                goto out;

        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        priv = this->private;
        local = frame->local;

        /* If .snaps pre-exists, then it should not be listed
         * in the NORMAL INODE directory when USS is enabled,
         * so filter the .snaps entry if exists.
         * However it is OK to list .snaps in VIRTUAL world
         */
        if (local->subvolume != FIRST_CHILD (this))
                goto out;

        list_for_each_entry_safe (entry, tmpentry, &entries->list, list) {
                if (strcmp(priv->path, entry->d_name) == 0)
                        gf_dirent_entry_free (entry);
        }

out:
        SVC_STACK_UNWIND (readdir, frame, op_ret, op_errno, entries, xdata);
        return 0;
}

static int32_t
gf_svc_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t off, dict_t *xdata)
{
        int           inode_type = -1;
        xlator_t     *subvolume  = NULL;
        svc_local_t  *local      = NULL;
        int           ret        = -1;
        int           op_ret     = -1;
        int           op_errno   = EINVAL;
        gf_boolean_t  wind       = _gf_false;
        svc_fd_t     *svc_fd     = NULL;
        gf_dirent_t   entries;

        INIT_LIST_HEAD (&entries);

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);

        svc_fd = svc_fd_ctx_get_or_new (this,  fd);
        if (!svc_fd)
                gf_log (this->name, GF_LOG_ERROR, "failed to get the fd "
                        "context for the inode %s",
                        uuid_utoa (fd->inode->gfid));
        else {
                if (svc_fd->entry_point_handled && off == svc_fd->last_offset) {
                        op_ret = 0;
                        op_errno = ENOENT;
                        goto out;
                }
        }

        SVC_GET_SUBVOL_FROM_CTX (this, op_ret, op_errno, inode_type, ret,
                                 fd->inode, subvolume, out);

        local = mem_get0 (this->local_pool);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate local");
                goto out;
        }
        local->subvolume = subvolume;
        frame->local = local;

	STACK_WIND (frame, gf_svc_readdir_cbk, subvolume,
                    subvolume->fops->readdir, fd, size, off, xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (readdir, frame, op_ret, op_errno, &entries,
                                  NULL);

        gf_dirent_free (&entries);

        return 0;
}

/*
 * This lookup if mainly for supporting USS for windows.
 * Since the dentry for the entry-point directory is not sent in
 * the readdir response, from windows explorer, there is no way
 * to access the snapshots. If the explicit path of the entry-point
 * directory is mentioned in the address bar, then windows sends
 * readdir on the parent directory and compares if the entry point
 * directory's name is there in readdir response. If it is not there
 * then access to snapshot world is denied. And windows users cannot
 * access snapshots via samba.
 * So, to handle this a new option called special-directory is created,
 * which if set, snapview-client will send the entry-point's dentry
 * in readdirp o/p for the special directory, so that it will be
 * visible from windows explorer.
 * But to send that virtual entry, the following mechanism is used.
 * 1) Check if readdir from posix is over.
 * 2) If so, then send a lookup on entry point directory to snap daemon
 * (this is needed because in readdirp inodes are linked, so we need to
 * maintain 1:1 mapping between inodes (gfids) from snapview server to
 * snapview client).
 * 3) Once successful lookup response received, send a new entry to
 * windows.
 */

static int32_t
gf_svc_readdirp_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno, inode_t *inode,
                            struct iatt *buf, dict_t *xdata,
                            struct iatt *postparent)
{
        gf_dirent_t    entries;
        gf_dirent_t   *entry      = NULL;
        svc_private_t *private    = NULL;
        svc_fd_t      *svc_fd     = NULL;
        svc_local_t   *local      = NULL;
        int            inode_type = -1;
        int            ret        = -1;

        GF_VALIDATE_OR_GOTO ("snapview-client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        private = this->private;
        INIT_LIST_HEAD (&entries.list);

        local = frame->local;

        if (local->xdata != NULL)
                dict_unref (xdata);

        if (op_ret) {
                op_ret = 0;
                op_errno = ENOENT;
                goto out;
        }

        svc_fd = svc_fd_ctx_get (this, local->fd);
        if (!svc_fd) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the fd "
                        "context for the inode %s",
                        uuid_utoa (local->fd->inode->gfid));
                op_ret = 0;
                op_errno = ENOENT;
                goto out;
        }

        entry = gf_dirent_for_name (private->path);
        if (!entry) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate memory "
                        "for the entry %s", private->path);
                op_ret = 0;
                op_errno = ENOMEM;
                goto out;
        }

        entry->inode = inode_ref (inode);
        entry->d_off = svc_fd->last_offset + 22;
        entry->d_ino = buf->ia_ino;
        entry->d_type = DT_DIR;
        entry->d_stat = *buf;
        inode_type = VIRTUAL_INODE;
        ret = svc_inode_ctx_set (this, entry->inode, inode_type);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "failed to set the inode "
                        "context");

        list_add_tail (&entry->list, &entries.list);
        op_ret = 1;
        svc_fd->last_offset = entry->d_off;
        svc_fd->entry_point_handled = _gf_true;

out:
        SVC_STACK_UNWIND (readdirp, frame, op_ret, op_errno, &entries,
                          local->xdata);

        gf_dirent_free (&entries);

        return 0;
}

static gf_boolean_t
gf_svc_readdir_on_special_dir (call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret,
                               int32_t op_errno, gf_dirent_t *entries,
                               dict_t *xdata)
{
        svc_local_t   *local      = NULL;
        svc_private_t *private    = NULL;
        inode_t       *inode      = NULL;
        fd_t          *fd         = NULL;
        char          *path       = NULL;
        loc_t         *loc        = NULL;
        dict_t        *tmp_xdata  = NULL;
        int            ret        = -1;
        gf_boolean_t   unwind     = _gf_true;
        svc_fd_t      *svc_fd     = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        private = this->private;
        local = frame->local;

        loc = &local->loc;
        fd = local->fd;
        svc_fd = svc_fd_ctx_get (this, fd);
        if (!svc_fd) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the fd "
                        "context for the inode %s",
                        uuid_utoa (fd->inode->gfid));
                goto out;
        }

        /*
         * check if its end of readdir operation from posix, if special_dir
         * option is set, if readdir is done on special directory and if
         * readdirp is from normal regular graph.
         */

        if (!private->show_entry_point)
                goto out;

        if (op_ret == 0 && op_errno == ENOENT && private->special_dir &&
            strcmp (private->special_dir, "") && svc_fd->special_dir &&
            local->subvolume == FIRST_CHILD (this)) {
                inode = inode_grep (fd->inode->table, fd->inode,
                                    private->path);
                if (!inode) {
                        inode = inode_new (fd->inode->table);
                        if (!inode) {
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "allocate new inode");
                                goto out;
                        }
                }

                gf_uuid_copy (local->loc.pargfid, fd->inode->gfid);
                gf_uuid_copy (local->loc.gfid, inode->gfid);
                if (gf_uuid_is_null (inode->gfid))
                        ret = inode_path (fd->inode, private->path, &path);
                else
                        ret = inode_path (inode, NULL, &path);

                if (ret < 0)
                        goto out;
                loc->path = gf_strdup (path);
                if (loc->path) {
                        if (!loc->name ||
                            (loc->name && !strcmp (loc->name, ""))) {
                                loc->name = strrchr (loc->path, '/');
                                if (loc->name)
                                        loc->name++;
                        }
                }

                loc->inode = inode;
                loc->parent = inode_ref (fd->inode);
                tmp_xdata = dict_new ();
                if (!tmp_xdata)
                        goto out;
                ret = dict_set_str (tmp_xdata, "entry-point", "true");
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to set dict");
                        goto out;
                }

                local->cookie = cookie;
                if (xdata == NULL)
                        local->xdata = NULL;
                else
                        local->xdata = dict_ref (xdata);
                STACK_WIND (frame, gf_svc_readdirp_lookup_cbk,
                            SECOND_CHILD (this),
                            SECOND_CHILD (this)->fops->lookup, loc, tmp_xdata);
                unwind = _gf_false;
        }

out:
        if (tmp_xdata)
                dict_unref (tmp_xdata);

        GF_FREE (path);
        return unwind;
}

static int32_t
gf_svc_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     gf_dirent_t *entries, dict_t *xdata)
{
        gf_dirent_t   *entry      = NULL;
        gf_dirent_t   *tmpentry   = NULL;
        svc_local_t   *local      = NULL;
        int            inode_type = -1;
        int            ret        = -1;
        svc_fd_t      *svc_fd     = NULL;
        gf_boolean_t   unwind     = _gf_true;
        svc_private_t *priv       = NULL;

        if (op_ret < 0)
                goto out;

        GF_VALIDATE_OR_GOTO ("snapview-client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        priv = this->private;
        local = frame->local;

        svc_fd = svc_fd_ctx_get (this, local->fd);
        if (!svc_fd) {
                gf_log (this->name, GF_LOG_WARNING, "failed to get the fd "
                        "context for the gfid %s",
                        uuid_utoa (local->fd->inode->gfid));
        }

        if (local->subvolume == FIRST_CHILD (this))
                inode_type = NORMAL_INODE;
        else
                inode_type = VIRTUAL_INODE;

        list_for_each_entry_safe (entry, tmpentry, &entries->list, list) {
                /* If .snaps pre-exists, then it should not be listed
                 * in the NORMAL INODE directory when USS is enabled,
                 * so filter the .snaps entry if exists.
                 * However it is OK to list .snaps in VIRTUAL world
                 */
                if (inode_type == NORMAL_INODE &&
                    !strcmp(priv->path, entry->d_name)) {
                        gf_dirent_entry_free (entry);
                        continue;
                }

                if (!entry->inode)
                        continue;

                ret = svc_inode_ctx_set (this, entry->inode, inode_type);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR, "failed to set inode "
                                "context");
                if (svc_fd)
                        svc_fd->last_offset = entry->d_off;
        }

        unwind = gf_svc_readdir_on_special_dir (frame, cookie, this, op_ret,
                                                op_errno, entries, xdata);

out:
        if (unwind)
                SVC_STACK_UNWIND (readdirp, frame, op_ret, op_errno, entries,
                                  xdata);

        return 0;
}

static int32_t
gf_svc_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                 off_t off, dict_t *xdata)
{
        int            inode_type = -1;
        xlator_t      *subvolume  = NULL;
        svc_local_t   *local      = NULL;
        int            ret        = -1;
        int            op_ret     = -1;
        int            op_errno   = EINVAL;
        gf_boolean_t   wind       = _gf_false;
        svc_fd_t      *svc_fd     = NULL;
        gf_dirent_t    entries;

        INIT_LIST_HEAD (&entries.list);

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);

        local = mem_get0 (this->local_pool);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate local");
                op_errno = ENOMEM;
                goto out;
        }

        /*
         * This is mainly for samba shares (or windows clients). As part of
         * readdirp on the directory used as samba share, the entry point
         * directory would have been added at the end. So when a new readdirp
         * request comes, we have to check if the entry point has been handled
         * or not in readdirp. That information and the offset used for it
         * is remembered in fd context. If it has been handled, then simply
         * unwind indication end of readdir operation.
         */
        svc_fd = svc_fd_ctx_get_or_new (this,  fd);
        if (!svc_fd)
                gf_log (this->name, GF_LOG_ERROR, "failed to get the fd "
                        "context for the inode %s",
                        uuid_utoa (fd->inode->gfid));
        else {
                if (svc_fd->entry_point_handled && off == svc_fd->last_offset) {
                        op_ret = 0;
                        op_errno = ENOENT;
                        goto out;
                }
        }

        SVC_GET_SUBVOL_FROM_CTX (this, op_ret, op_errno, inode_type, ret,
                                 fd->inode, subvolume, out);

        local->subvolume = subvolume;
        local->fd = fd_ref (fd);
        frame->local = local;

        STACK_WIND (frame, gf_svc_readdirp_cbk, subvolume,
                    subvolume->fops->readdirp, fd, size, off, xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (readdirp, frame, op_ret, op_errno, &entries,
                                  NULL);

        gf_dirent_free (&entries);

        return 0;
}

/* Renaming the entries from or to snapshots is not allowed as the snapshots
   are read-only.
*/
static int32_t
gf_svc_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
               loc_t *newloc, dict_t *xdata)
{
        int          src_inode_type  = -1;
        int          dst_inode_type  = -1;
        int          dst_parent_type = -1;
        int32_t      op_ret          = -1;
        int32_t      op_errno        = 0;
        int32_t      ret             = -1;
        gf_boolean_t wind            = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, oldloc, out);
        GF_VALIDATE_OR_GOTO (this->name, oldloc->inode, out);
        GF_VALIDATE_OR_GOTO (this->name, newloc, out);

        ret = svc_inode_ctx_get (this, oldloc->inode, &src_inode_type);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the inode "
                        "context for the inode %s",
                        uuid_utoa (oldloc->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (src_inode_type == VIRTUAL_INODE) {
                gf_log (this->name, GF_LOG_ERROR, "rename happening on a entry"
                        " %s residing in snapshot", oldloc->name);
                op_ret = -1;
                op_errno = EROFS;
                goto out;
        }

        if (newloc->inode) {
                ret = svc_inode_ctx_get (this, newloc->inode, &dst_inode_type);
                if (!ret && dst_inode_type == VIRTUAL_INODE) {
                        gf_log (this->name, GF_LOG_ERROR, "rename of %s "
                                "happening to a entry %s residing in snapshot",
                                oldloc->name, newloc->name);
                        op_ret = -1;
                        op_errno = EROFS;
                        goto out;
                }
        }

        if (dst_inode_type < 0) {
                ret = svc_inode_ctx_get (this, newloc->parent,
                                         &dst_parent_type);
                if (!ret && dst_parent_type == VIRTUAL_INODE) {
                        gf_log (this->name, GF_LOG_ERROR, "rename of %s "
                                "happening to a entry %s residing in snapshot",
                                oldloc->name, newloc->name);
                        op_ret = -1;
                        op_errno = EROFS;
                        goto out;
                }
        }

        STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                         FIRST_CHILD (this)->fops->rename, oldloc, newloc,
                         xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (rename, frame, op_ret, op_errno, NULL,
                                  NULL, NULL, NULL, NULL, NULL);
        return 0;
}

/* Creating hardlinks for the files from the snapshot is not allowed as it
   will be equivalent of creating hardlinks across different filesystems.
   And so is vise versa.
*/
static int32_t
gf_svc_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
             dict_t *xdata)
{
        int          src_inode_type  = -1;
        int          dst_parent_type = -1;
        int32_t      op_ret          = -1;
        int32_t      op_errno        = 0;
        int32_t      ret             = -1;
        gf_boolean_t wind            = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, oldloc, out);
        GF_VALIDATE_OR_GOTO (this->name, oldloc->inode, out);
        GF_VALIDATE_OR_GOTO (this->name, newloc, out);

        ret = svc_inode_ctx_get (this, oldloc->inode, &src_inode_type);
        if (!ret && src_inode_type == VIRTUAL_INODE) {
                gf_log (this->name, GF_LOG_ERROR, "rename happening on a entry"
                        " %s residing in snapshot", oldloc->name);
                op_ret = -1;
                op_errno = EROFS;
                goto out;
        }

        ret = svc_inode_ctx_get (this, newloc->parent, &dst_parent_type);
        if (!ret && dst_parent_type == VIRTUAL_INODE) {
                gf_log (this->name, GF_LOG_ERROR, "rename of %s "
                        "happening to a entry %s residing in snapshot",
                        oldloc->name, newloc->name);
                op_ret = -1;
                op_errno = EROFS;
                goto out;
        }

        STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                         FIRST_CHILD (this)->fops->link, oldloc, newloc, xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (link, frame, op_ret, op_errno,
                                  NULL, NULL, NULL, NULL, NULL);
        return 0;
}

static int32_t
gf_svc_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    const char *name, dict_t *xdata)
{
        int          ret        = -1;
        int          inode_type = -1;
        int          op_ret     = -1;
        int          op_errno   = EINVAL;
        gf_boolean_t wind       = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        ret = svc_inode_ctx_get (this, loc->inode, &inode_type);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get te inode "
                        "context for %s (gfid: %s)", loc->path,
                        uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (inode_type == NORMAL_INODE) {
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD (this)->fops->removexattr, loc,
                                 name, xdata);
        } else {
                op_ret = -1;
                op_errno = EROFS;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (removexattr, frame, op_ret, op_errno,
                                  NULL);

        return 0;
}

static int
gf_svc_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync,
              dict_t *xdata)
{
        int             inode_type = -1;
        int             ret        = -1;
        int             op_ret     = -1;
        int             op_errno   = EINVAL;
        gf_boolean_t    wind       = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);

        ret = svc_inode_ctx_get (this, fd->inode, &inode_type);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get inode context "
                        "for %s", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (inode_type == NORMAL_INODE) {
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD (this)->fops->fsync, fd, datasync,
                                 xdata);
        } else {
                op_ret = -1;
                op_errno = EROFS;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (fsync, frame, op_ret, op_errno, NULL, NULL,
                                  NULL);

        return 0;
}

static int32_t
gf_svc_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        int32_t           op_ret     = -1;
        int32_t           op_errno   = 0;
        int               ret        = -1;
        int               inode_type = -1;
        xlator_t         *subvolume  = NULL;
        gf_boolean_t      wind       = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);

        SVC_GET_SUBVOL_FROM_CTX (this, op_ret, op_errno, inode_type, ret,
                                 fd->inode, subvolume, out);

        STACK_WIND_TAIL (frame, subvolume, subvolume->fops->flush, fd, xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (flush, frame, op_ret, op_errno, NULL);

        return 0;
}

static int32_t
gf_svc_releasedir (xlator_t *this, fd_t *fd)
{
        svc_fd_t *sfd      = NULL;
        uint64_t          tmp_pfd  = 0;
        int               ret      = 0;

        GF_VALIDATE_OR_GOTO ("snapview-client", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        ret = fd_ctx_del (fd, this, &tmp_pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd from fd=%p is NULL", fd);
                goto out;
        }

        GF_FREE (sfd);

out:
        return 0;
}

static int32_t
gf_svc_forget (xlator_t *this, inode_t *inode)
{
        int            ret      = -1;
        uint64_t       value    = 0;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        ret = inode_ctx_del (inode, this, &value);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to delete inode "
                        "context for %s", uuid_utoa (inode->gfid));
                goto out;
        }

out:
        return 0;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        svc_private_t  *priv = NULL;

        priv = this->private;

        GF_OPTION_RECONF ("snapshot-directory", priv->path, options, str, out);
        GF_OPTION_RECONF ("show-snapshot-directory", priv->show_entry_point,
                          options, bool, out);

out:
        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int32_t     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_svc_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_WARNING, "Memory accounting"
                        " init failed");
                return ret;
        }

        return ret;
}

int32_t
init (xlator_t *this)
{
        svc_private_t *private  = NULL;
        int            ret      = -1;
        int            children = 0;
        xlator_list_t *xl       = NULL;

        if (!this->children) {
                gf_log (this->name, GF_LOG_ERROR,
                        "configured without any child");
                goto out;
        }

        xl = this->children;
        while (xl) {
                children++;
                xl = xl->next;
        }

        if (children != 2) {
                gf_log (this->name, GF_LOG_ERROR, "snap-view-client has got "
                        "%d subvolumes. It can have only 2 subvolumes.",
                        children);
                goto out;
        }

        /* This can be the top of graph in certain cases */
        if (!this->parents) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "dangling volume. check volfile ");
        }

        private = GF_CALLOC (1, sizeof (*private), gf_svc_mt_svc_private_t);
        if (!private)
                goto out;

        GF_OPTION_INIT ("snapshot-directory", private->path, str, out);
        GF_OPTION_INIT ("snapdir-entry-path", private->special_dir, str,
                        out);
        GF_OPTION_INIT ("show-snapshot-directory", private->show_entry_point,
                        bool, out);

        if (strstr (private->special_dir, private->path)) {
                gf_log (this->name, GF_LOG_ERROR, "entry point directory "
                        "cannot be part of the special directory");
                GF_FREE (private->special_dir);
                private->special_dir = NULL;
                goto out;
        }

        this->private = private;
        this->local_pool = mem_pool_new (svc_local_t, 128);
        if (!this->local_pool) {
                gf_log (this->name, GF_LOG_ERROR, "could not get mem pool for "
                        "frame->local");
                goto out;
        }

        ret = 0;

out:
        if (ret)
                GF_FREE (private);

        return ret;
}

void
fini (xlator_t *this)
{
        svc_private_t *priv = NULL;

        if (!this)
                return;

        priv = this->private;
        if (!priv)
                return;

        this->private = NULL;

        GF_FREE (priv);

        return;
}

int
notify (xlator_t *this, int event, void *data, ...)
{
        xlator_t       *subvol     = NULL;
        int             ret        = 0;

        subvol = data;

        /* As there are two subvolumes in snapview-client, there is
         * a possibility that the regular subvolume is still down and
         * snapd subvolume come up first. So if we don't handle this situation
         * CHILD_UP event will be propagated upwards to fuse when
         * regular subvolume is still down.
         * This can cause data unavailable for the application.
         * So for now send notifications up only for regular subvolume.
         *
         * TODO: In future if required we may need to handle
         * notifications from virtual subvolume
         */
        if (subvol != SECOND_CHILD (this))
                ret = default_notify (this, event, data);

        return ret;
}

struct xlator_fops fops = {
        .lookup        = gf_svc_lookup,
        .opendir       = gf_svc_opendir,
        .stat          = gf_svc_stat,
        .fstat         = gf_svc_fstat,
        .statfs        = gf_svc_statfs,
        .rmdir         = gf_svc_rmdir,
        .rename        = gf_svc_rename,
        .mkdir         = gf_svc_mkdir,
        .open          = gf_svc_open,
        .unlink        = gf_svc_unlink,
        .setattr       = gf_svc_setattr,
        .getxattr      = gf_svc_getxattr,
        .setxattr      = gf_svc_setxattr,
        .fsetxattr     = gf_svc_fsetxattr,
        .readv         = gf_svc_readv,
        .readdir       = gf_svc_readdir,
        .readdirp      = gf_svc_readdirp,
        .create        = gf_svc_create,
        .readlink      = gf_svc_readlink,
        .mknod         = gf_svc_mknod,
        .symlink       = gf_svc_symlink,
        .flush         = gf_svc_flush,
        .link          = gf_svc_link,
        .access        = gf_svc_access,
        .removexattr   = gf_svc_removexattr,
        .fsync         = gf_svc_fsync,
};

struct xlator_cbks cbks = {
        .forget = gf_svc_forget,
        .releasedir = gf_svc_releasedir,
};

struct volume_options options[] = {
        { .key = {"snapshot-directory"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = ".snaps",
        },
        { .key = {"snapdir-entry-path"},
          .type = GF_OPTION_TYPE_STR,
          .description = "An option to set the path of a directory on which "
                         "when readdir comes, dentry for the snapshot-directory"
                         " should be created and added in the readdir response",
          .default_value = "",
        },
        { .key = {"show-snapshot-directory"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "If this option is set, and the option "
                         "\"snapdir-entry-path\" is set (which is set by samba "
                         "vfs plugin for glusterfs, then send the entry point "
                         "when readdir comes on the snapdir-entry-path",
          .default_value = "off",
        },
        { .key  = {NULL} },
};
