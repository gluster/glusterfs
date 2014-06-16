/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
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

#include "snapview-client.h"
#include "inode.h"
#include "byte-order.h"


void
svc_local_free (svc_local_t *local)
{
        if (local) {
                loc_wipe (&local->loc);
                mem_put (local);
        }
}

xlator_t *
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

int32_t
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

int
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

int
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

int32_t
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

int32_t
svc_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, inode_t *inode,
                struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        svc_local_t     *local       = NULL;
        inode_t         *parent      = NULL;
        xlator_t        *subvolume   = NULL;
        gf_boolean_t     do_unwind   = _gf_true;
        int              inode_type  = -1;
        int              parent_type = -1;
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
        */
        if (op_ret) {
                if (op_errno == ENOENT &&
                    !uuid_is_null (local->loc.gfid)) {
                        ret = svc_inode_ctx_get (this, inode, &inode_type);
                        if (ret < 0 && subvolume == FIRST_CHILD (this)) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "Lookup on normal graph failed. "
                                        "Sending lookup to snapview-server");
                                subvolume = SECOND_CHILD (this);
                                STACK_WIND (frame, svc_lookup_cbk, subvolume,
                                            subvolume->fops->lookup,
                                            &local->loc, xdata);
                                do_unwind = _gf_false;
                        }
                }

                gf_log (this->name,
                        (op_errno == ENOENT)?GF_LOG_DEBUG:GF_LOG_ERROR,
                        "Lookup on normal graph failed with error %s",
                        strerror (op_errno));
                goto out;
        }

        if (local->loc.parent)
                parent = inode_ref (local->loc.parent);
        else {
                parent = inode_parent (inode, NULL, NULL);
                if (!parent && !uuid_is_null (local->loc.pargfid)) {
                        parent = inode_find (inode->table,
                                             local->loc.pargfid);
                }
        }

        if (!__is_root_gfid (buf->ia_gfid) && parent) {
                ret = svc_inode_ctx_get (this, parent, &parent_type);
                if (ret < 0) {
                        op_ret = -1;
                        op_errno = EINVAL;
                        gf_log (this->name, GF_LOG_WARNING,
                                "Error fetching parent context");
                        goto out;
                }
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

        if (parent)
                inode_unref (parent);

        return 0;
}

int32_t
svc_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
            dict_t *xdata)
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
                if (uuid_is_null (loc->inode->gfid)) {
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
                if (parent_type == NORMAL_INODE) {
                        subvolume = SECOND_CHILD (this);
                        local->subvolume = subvolume;
                        /* Indication of whether the lookup is happening on the
                           entry point or not, to the snapview-server.
                        */
                        SVC_ENTRY_POINT_SET (this, xdata, op_ret, op_errno,
                                             new_xdata, priv, ret, out);
                } else {
                        /* Either error can be sent to application as
                           the entry point directory can exist only within
                           real directories and here the parent is a virtual
                           directory or send the call all the way to svs and
                           let it send the error back. For now it is sending
                           the error to application itself. (Saves the
                           network latency)
                        */
                        op_ret = -1;
                        op_errno = ENOENT;
                        goto out;
                }
        }

        wind = _gf_true;

out:
        if (wind)
                STACK_WIND (frame, svc_lookup_cbk,
                             subvolume, subvolume->fops->lookup, loc, xdata);
        else
                SVC_STACK_UNWIND (lookup, frame, op_ret, op_errno, NULL,
                                  NULL, NULL, NULL);
        if (new_xdata)
                dict_unref (new_xdata);

        if (parent)
                inode_unref (parent);

        return 0;
}

/* should all the fops be handled like lookup is supposed to be
   handled? i.e just based on inode type decide where the call should
   be sent and in the call back update the contexts.
*/
int32_t
svc_stat (call_frame_t *frame, xlator_t *this, loc_t *loc,
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

        STACK_WIND_TAIL (frame,subvolume, subvolume->fops->stat, loc, xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (stat, frame, op_ret, op_errno,
                                  NULL, NULL);
        return 0;
}

int32_t
svc_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
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
int32_t
svc_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
             dict_t *xdata)
{
        int32_t        ret        = -1;
        int            inode_type = -1;
        xlator_t      *subvolume  = NULL;
        int            op_ret     = -1;
        int            op_errno   = EINVAL;
        gf_boolean_t   wind       = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        SVC_GET_SUBVOL_FROM_CTX (this, op_ret, op_errno, inode_type, ret,
                                 loc->inode, subvolume, out);

        STACK_WIND_TAIL (frame, subvolume, subvolume->fops->opendir, loc, fd,
                         xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (opendir, frame, op_ret, op_errno, NULL, NULL);

        return 0;
}

int32_t
svc_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
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
                op_errno = EPERM;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (setattr, frame, op_ret, op_errno,
                                  NULL, NULL, NULL);
        return 0;
}

int32_t
svc_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iatt *stbuf,
             int32_t valid, dict_t *xdata)
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
                op_errno = EPERM;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (fsetattr, frame, op_ret, op_errno,
                                  NULL, NULL, NULL);
        return 0;
}

int32_t
svc_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, const char *name,
              dict_t *xdata)
{
        int32_t        ret        = -1;
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

        STACK_WIND_TAIL (frame, subvolume, subvolume->fops->getxattr, loc, name,
                         xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (getxattr, frame, op_ret, op_errno,
                                  NULL, NULL);

        return 0;
}

int32_t
svc_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
              dict_t *xdata)
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

int32_t
svc_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
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
                op_errno = EPERM;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (setxattr, frame, op_ret, op_errno,
                                  NULL);

        return 0;
}

int32_t
svc_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
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
                op_errno = EPERM;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno,
                                     NULL);

        return 0;
}

int32_t
svc_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
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
                op_errno = EPERM;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (rmdir, frame, op_ret, op_errno,
                                  NULL, NULL, NULL);
        return 0;
}

int32_t
svc_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
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

int32_t
svc_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
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
                STACK_WIND (frame, svc_mkdir_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->mkdir, loc, mode,
                            umask, xdata);
        } else {
                op_ret = -1;
                op_errno = EPERM;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (mkdir, frame, op_ret, op_errno, NULL, NULL,
                                  NULL, NULL, NULL);
        return 0;
}

int32_t
svc_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
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

int32_t
svc_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
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
                STACK_WIND (frame, svc_mknod_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->mknod, loc, mode,
                            rdev, umask, xdata);
        } else {
                op_ret = -1;
                op_errno = EPERM;
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
   a virtual inode, then unwind the call back with EPERM. Otherwise simply
   STACK_WIND the call to the first child of svc xlator.
*/
int32_t
svc_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
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

int32_t
svc_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
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

int32_t
svc_create (call_frame_t *frame, xlator_t *this,
            loc_t *loc, int32_t flags, mode_t mode,
            mode_t umask, fd_t *fd, dict_t *xdata)
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
                STACK_WIND (frame, svc_create_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->create, loc, flags,
                            mode, umask, fd, xdata);
        } else {
                op_ret = -1;
                op_errno = EPERM;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (create, frame, op_ret, op_errno,
                                  NULL, NULL, NULL, NULL, NULL, NULL);
        return 0;
}

int32_t
svc_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
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

int32_t
svc_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
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
                STACK_WIND (frame, svc_symlink_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->symlink, linkpath, loc,
                            umask, xdata);
        } else {
                op_ret = -1;
                op_errno = EPERM;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (symlink, frame, op_ret, op_errno,
                                  NULL, NULL, NULL, NULL, NULL);
        return 0;
}

int32_t
svc_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
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
                op_errno = EPERM;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (unlink, frame, op_ret, op_errno, NULL, NULL,
                                  NULL);
        return 0;
}

int32_t
svc_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
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

int32_t
svc_readlink (call_frame_t *frame, xlator_t *this,
              loc_t *loc, size_t size, dict_t *xdata)
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

int32_t
svc_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
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
svc_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
             size_t size, off_t off,
             dict_t *xdata)
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

        STACK_WIND_TAIL (frame, subvolume, subvolume->fops->readdir, fd, size,
                         off, xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (readdir, frame, op_ret, op_errno, NULL,
                                  NULL);
        return 0;
}

int32_t
svc_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  gf_dirent_t *entries, dict_t *xdata)
{
        gf_dirent_t   *entry      = NULL;
        svc_local_t   *local      = NULL;
        gf_boolean_t   real       = _gf_true;
        int            inode_type = -1;
        int            ret        = -1;

        if (op_ret < 0)
                goto out;

        local = frame->local;
        frame->local = NULL;

        if (local->subvolume == FIRST_CHILD (this))
                real = _gf_true;
        else
                real = _gf_false;

        list_for_each_entry (entry, &entries->list, list) {
                if (!entry->inode)
                        continue;

                if (real)
                        inode_type = NORMAL_INODE;
                else
                        inode_type = VIRTUAL_INODE;

                ret = svc_inode_ctx_set (this, entry->inode, inode_type);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR, "failed to set inode "
                                "context");
        }


out:
        SVC_STACK_UNWIND (readdirp, frame, op_ret, op_errno, entries, xdata);

        return 0;
}

int32_t
svc_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd,
              size_t size, off_t off,
              dict_t *xdata)
{
        int            inode_type = -1;
        xlator_t      *subvolume  = NULL;
        svc_local_t   *local      = NULL;
        int            ret        = -1;
        int            op_ret     = -1;
        int            op_errno   = EINVAL;
        gf_boolean_t   wind       = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);

        local = mem_get0 (this->local_pool);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate local");
                goto out;
        }

        SVC_GET_SUBVOL_FROM_CTX (this, op_ret, op_errno, inode_type, ret,
                                 fd->inode, subvolume, out);

        local->subvolume = subvolume;
        frame->local = local;

        STACK_WIND (frame, svc_readdirp_cbk, subvolume,
                    subvolume->fops->readdirp, fd, size, off, xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (readdirp, frame, op_ret, op_errno, NULL, NULL);

        return 0;
}

/* Renaming the entries from or to snapshots is not allowed as the snapshots
   are read-only.
*/
int32_t
svc_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
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
                op_errno = EPERM;
                goto out;
        }

        if (newloc->inode) {
                ret = svc_inode_ctx_get (this, newloc->inode, &dst_inode_type);
                if (!ret && dst_inode_type == VIRTUAL_INODE) {
                        gf_log (this->name, GF_LOG_ERROR, "rename of %s "
                                "happening to a entry %s residing in snapshot",
                                oldloc->name, newloc->name);
                        op_ret = -1;
                        op_errno = EPERM;
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
                        op_errno = EPERM;
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
int32_t
svc_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
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
                op_errno = EPERM;
                goto out;
        }

        ret = svc_inode_ctx_get (this, newloc->parent, &dst_parent_type);
        if (!ret && dst_parent_type == VIRTUAL_INODE) {
                gf_log (this->name, GF_LOG_ERROR, "rename of %s "
                        "happening to a entry %s residing in snapshot",
                        oldloc->name, newloc->name);
                op_ret = -1;
                op_errno = EPERM;
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

int32_t
svc_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
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
                op_errno = EPERM;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (removexattr, frame, op_ret, op_errno,
                                  NULL);

        return 0;
}

int32_t
svc_flush (call_frame_t *frame, xlator_t *this,
           fd_t *fd, dict_t *xdata)
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

int32_t
svc_forget (xlator_t *this, inode_t *inode)
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

        GF_FREE (priv->path);
        GF_FREE (priv);

        return;
}

struct xlator_fops fops = {
        .lookup        = svc_lookup,
        .opendir       = svc_opendir,
        .stat          = svc_stat,
        .fstat         = svc_fstat,
        .rmdir         = svc_rmdir,
        .rename        = svc_rename,
        .mkdir         = svc_mkdir,
        .open          = svc_open,
        .unlink        = svc_unlink,
        .setattr       = svc_setattr,
        .getxattr      = svc_getxattr,
        .setxattr      = svc_setxattr,
        .fsetxattr     = svc_fsetxattr,
        .readv         = svc_readv,
        .readdir       = svc_readdir,
        .readdirp      = svc_readdirp,
        .create        = svc_create,
        .readlink      = svc_readlink,
        .mknod         = svc_mknod,
        .symlink       = svc_symlink,
        .flush         = svc_flush,
        .link          = svc_link,
        .access        = svc_access,
        .removexattr   = svc_removexattr,
};

struct xlator_cbks cbks = {
        .forget = svc_forget,
};

struct volume_options options[] = {
        { .key = {"snapshot-directory"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = ".snaps",
        },
        { .key  = {NULL} },
};
