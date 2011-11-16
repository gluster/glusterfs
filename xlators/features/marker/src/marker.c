/*Copyright (c) 2008-2011 Gluster, Inc. <http://www.gluster.com>
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

#include "xlator.h"
#include "defaults.h"
#include "libxlator.h"
#include "marker.h"
#include "marker-mem-types.h"
#include "marker-quota.h"
#include "marker-quota-helper.h"
#include "marker-common.h"
#include "byte-order.h"

#define _GF_UID_GID_CHANGED 1

void
fini (xlator_t *this);

int32_t
marker_start_setxattr (call_frame_t *, xlator_t *);

marker_local_t *
marker_local_ref (marker_local_t *local)
{
        GF_VALIDATE_OR_GOTO ("marker", local, err);

        LOCK (&local->lock);
        {
                local->ref++;
        }
        UNLOCK (&local->lock);

        return local;
err:
        return NULL;
}

int
marker_loc_fill (loc_t *loc, inode_t *inode, inode_t *parent, char *path)
{
        int     ret = -1;

        if (!loc)
                return ret;

        if (inode) {
                loc->inode = inode_ref (inode);
        }

        if (parent)
                loc->parent = inode_ref (parent);

        loc->path = gf_strdup (path);
        if (!loc->path) {
                gf_log ("loc fill", GF_LOG_ERROR, "strdup failed");
                goto loc_wipe;
        }

        loc->name = strrchr (loc->path, '/');
        if (loc->name)
                loc->name++;
        else
                goto loc_wipe;

        ret = 0;
loc_wipe:
        if (ret < 0)
                loc_wipe (loc);

        return ret;
}

int
marker_inode_loc_fill (inode_t *inode, loc_t *loc)
{
        char            *resolvedpath = NULL;
        inode_t         *parent       = NULL;
        int              ret          = -1;

        if ((!inode) || (!loc))
                return ret;

        if ((inode) && __is_root_gfid (inode->gfid)) {
                loc->parent = NULL;
                goto ignore_parent;
        }

        parent = inode_parent (inode, 0, NULL);
        if (!parent) {
                goto err;
        }

ignore_parent:
        ret = inode_path (inode, NULL, &resolvedpath);
        if (ret < 0)
                goto err;

        ret = marker_loc_fill (loc, inode, parent, resolvedpath);
        if (ret < 0)
                goto err;

err:
        if (parent)
                inode_unref (parent);

        if (resolvedpath)
                GF_FREE (resolvedpath);

        return ret;
}

int32_t
marker_trav_parent (marker_local_t *local)
{
        int32_t ret = 0;
        loc_t   loc = {0, };

        ret = marker_inode_loc_fill (local->loc.parent, &loc);

        if (ret < 0) {
                ret = -1;
                goto out;
        }

        loc_wipe (&local->loc);

        local->loc = loc;
out:
        return ret;
}

int32_t
marker_error_handler (xlator_t *this)
{
        marker_conf_t *priv = NULL;

        priv = (marker_conf_t *) this->private;

        unlink (priv->timestamp_file);

        return 0;
}

int32_t
marker_local_unref (marker_local_t *local)
{
        int32_t   var = 0;

        if (local == NULL)
                return -1;

        LOCK (&local->lock);
        {
                var = --local->ref;
        }
        UNLOCK (&local->lock);

        if (var != 0)
                goto out;

        loc_wipe (&local->loc);
        loc_wipe (&local->parent_loc);

        if (local->oplocal) {
                marker_local_unref (local->oplocal);
                local->oplocal = NULL;
        }
        GF_FREE (local);
out:
        return 0;
}

int32_t
stat_stampfile (xlator_t *this, marker_conf_t *priv,
                struct volume_mark **status)
{
        struct stat          buf      = {0, };
        struct volume_mark  *vol_mark = NULL;

        vol_mark = GF_CALLOC (sizeof (struct volume_mark), 1,
                              gf_marker_mt_volume_mark);

        vol_mark->major = 1;
        vol_mark->minor = 0;

        GF_ASSERT (sizeof (priv->volume_uuid_bin) == 16);
        memcpy (vol_mark->uuid, priv->volume_uuid_bin, 16);

        if (stat (priv->timestamp_file, &buf) != -1) {
                vol_mark->retval = 0;
                vol_mark->sec = htonl (buf.st_ctime);
                vol_mark->usec = htonl (ST_CTIM_NSEC (&buf)/1000);
        } else
                vol_mark->retval = 1;

        *status = vol_mark;

        return 0;
}

int32_t
marker_getxattr_stampfile_cbk (call_frame_t *frame, xlator_t *this,
                               const char *name, struct volume_mark *vol_mark)
{
        int32_t   ret  = -1;
        dict_t   *dict = NULL;

        if (vol_mark == NULL){
                STACK_UNWIND_STRICT (getxattr, frame, -1, ENOMEM, NULL);

                goto out;
        }

        dict = dict_new ();

        ret = dict_set_bin (dict, (char *)name, vol_mark,
                            sizeof (struct volume_mark));
        if (ret)
                gf_log (this->name, GF_LOG_WARNING, "failed to set key %s",
                        name);

        STACK_UNWIND_STRICT (getxattr, frame, 0, 0, dict);

        dict_unref (dict);
out:
        return 0;
}

int32_t
call_from_special_client (call_frame_t *frame, xlator_t *this, const char *name)
{
        struct volume_mark     *vol_mark   = NULL;
        marker_conf_t          *priv       = NULL;
        gf_boolean_t            ret        = _gf_true;

        priv = (marker_conf_t *)this->private;

        if (frame->root->pid != GF_CLIENT_PID_GSYNCD || name == NULL ||
            strcmp (name, MARKER_XATTR_PREFIX "." VOLUME_MARK) != 0) {
                ret = _gf_false;
                goto out;
        }

        stat_stampfile (this, priv, &vol_mark);

        marker_getxattr_stampfile_cbk (frame, this, name, vol_mark);
out:
        return ret;
}

int32_t
marker_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        if (cookie) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Filtering the quota extended attributes");

                dict_foreach (dict, marker_filter_quota_xattr, NULL);
        }
        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict);
        return 0;
}

int32_t
marker_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                 const char *name)
{
        gf_boolean_t   ret    = _gf_false;
        marker_conf_t *priv   = NULL;
        unsigned long  cookie = 0;

        priv = this->private;

        if (priv == NULL || (priv->feature_enabled & GF_XTIME) == 0)
                goto wind;

        gf_log (this->name, GF_LOG_DEBUG, "USER:PID = %d", frame->root->pid);

        ret = call_from_special_client (frame, this, name);
wind:
        if (ret == _gf_false) {
                if (name == NULL) {
                        /* Signifies that marker translator
                         * has to filter the quota's xattr's,
                         * this is to prevent afr from performing
                         * self healing on marker-quota xattrs'
                         */
                        cookie = 1;
                }
                STACK_WIND_COOKIE (frame, marker_getxattr_cbk, (void *)cookie,
                                   FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->getxattr, loc,
                                   name);
        }

        return 0;
}


int32_t
marker_setxattr_done (call_frame_t *frame)
{
        marker_local_t *local = NULL;

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_DESTROY (frame->root);

        marker_local_unref (local);

        return 0;
}

int
marker_specific_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                              int32_t op_ret, int32_t op_errno)
{
        int32_t         ret   = 0;
        int32_t         done  = 0;
        marker_local_t *local = NULL;

        local = (marker_local_t*) frame->local;

        if (op_ret == -1 && op_errno == ENOSPC) {
                marker_error_handler (this);
                done = 1;
                goto out;
        }

        if (strcmp (local->loc.path, "/") == 0) {
                done = 1;
                goto out;
        }

        ret = marker_trav_parent (local);

        if (ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "Error occured "
                        "while traversing to the parent, stopping marker");

                done = 1;

                goto out;
        }

        marker_start_setxattr (frame, this);

out:
        if (done) {
                marker_setxattr_done (frame);
        }

        return 0;
}

int32_t
marker_start_setxattr (call_frame_t *frame, xlator_t *this)
{
        int32_t          ret   = 0;
        dict_t          *dict  = NULL;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        local = (marker_local_t*) frame->local;

        dict = dict_new ();

        ret = dict_set_static_bin (dict, priv->marker_xattr,
                                   (void *)local->timebuf, 8);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set marker xattr (%s)", local->loc.path);

        STACK_WIND (frame, marker_specific_setxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr, &local->loc, dict, 0);

        dict_unref (dict);

        return 0;
}

void
marker_gettimeofday (marker_local_t *local)
{
        struct timeval tv = {0, };

        gettimeofday (&tv, NULL);

        local->timebuf [0] = htonl (tv.tv_sec);
        local->timebuf [1] = htonl (tv.tv_usec);

        return;
}

int32_t
marker_create_frame (xlator_t *this, marker_local_t *local)
{
        call_frame_t *frame = NULL;

        frame = create_frame (this, this->ctx->pool);

        frame->local = (void *) local;

        marker_start_setxattr (frame, this);

        return 0;
}

int32_t
marker_xtime_update_marks (xlator_t *this, marker_local_t *local)
{
        marker_gettimeofday (local);

        marker_local_ref (local);

        marker_create_frame (this, local);

        return 0;
}


int32_t
marker_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent)
{
        marker_conf_t      *priv    = NULL;
        marker_local_t     *local   = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "error occurred "
                        "while Creating a file %s", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno, inode,
                             buf, preparent, postparent);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_QUOTA)
                mq_set_inode_xattr (this, &local->loc);

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);

out:
        marker_local_unref (local);

        return 0;
}

int
marker_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
              dict_t *params)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_mkdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mkdir, loc, mode, params);

        return 0;
err:
        STACK_UNWIND_STRICT (mkdir, frame, -1, ENOMEM, NULL,
                             NULL, NULL, NULL);
        return 0;
}


int32_t
marker_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "error occurred "
                        "while Creating a file %s", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_QUOTA)
                mq_set_inode_xattr (this, &local->loc);

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);

out:
        marker_local_unref (local);

        return 0;
}

int32_t
marker_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
               mode_t mode, fd_t *fd, dict_t *params)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_create_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create, loc, flags, mode, fd,
                    params);
        return 0;
err:
        STACK_UNWIND_STRICT (create, frame, -1, ENOMEM, NULL, NULL, NULL, NULL,
                             NULL);

        return 0;
}


int32_t
marker_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf)
{
        marker_conf_t      *priv    = NULL;
        marker_local_t     *local   = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "error occurred "
                        "while write, %s", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_QUOTA)
                mq_initiate_quota_txn (this, &local->loc);

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);

out:
        marker_local_unref (local);

        return 0;
}

int32_t
marker_writev (call_frame_t *frame,
               xlator_t *this,
               fd_t *fd,
               struct iovec *vector,
               int32_t count,
               off_t offset,
               struct iobref *iobref)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ret = marker_inode_loc_fill (fd->inode, &local->loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_writev_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev, fd, vector, count, offset,
                    iobref);
        return 0;
err:
        STACK_UNWIND_STRICT (writev, frame, -1, ENOMEM, NULL, NULL);

        return 0;
}


int32_t
marker_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                  struct iatt *postparent)
{
        marker_conf_t      *priv    = NULL;
        marker_local_t     *local   = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "error occurred "
                        "rmdir %s", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno, preparent,
                             postparent);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_QUOTA)
                mq_reduce_parent_size (this, &local->loc, -1);

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);
out:
        marker_local_unref (local);

        return 0;
}

int32_t
marker_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_rmdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rmdir, loc, flags);
        return 0;
err:
        STACK_UNWIND_STRICT (rmdir, frame, -1, ENOMEM, NULL, NULL);

        return 0;
}


int32_t
marker_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent)
{
        marker_conf_t      *priv    = NULL;
        marker_local_t     *local   = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE,
                        "%s occurred in unlink", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno, preparent,
                             postparent);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if ((priv->feature_enabled & GF_QUOTA) && (local->ia_nlink == 1))
                mq_reduce_parent_size (this, &local->loc, -1);

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);
out:
        marker_local_unref (local);

        return 0;
}


int32_t
marker_unlink_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        marker_local_t *local = NULL;

        local = frame->local;
        if (op_ret < 0) {
                goto err;
        }

        if (local == NULL) {
                op_errno = EINVAL;
                goto err;
        }

        local->ia_nlink = buf->ia_nlink;

        STACK_WIND (frame, marker_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink, &local->loc);
        return 0;
err:
        frame->local = NULL;
        STACK_UNWIND_STRICT (unlink, frame, -1, op_errno, NULL, NULL);
        marker_local_unref (local);
        return 0;
}


int32_t
marker_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto unlink_wind;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;

        STACK_WIND (frame, marker_unlink_stat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat, loc);
        return 0;

unlink_wind:
        STACK_WIND (frame, marker_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink, loc);
        return 0;
err:
        frame->local = NULL;
        STACK_UNWIND_STRICT (unlink, frame, -1, ENOMEM, NULL, NULL);
        marker_local_unref (local);
        return 0;
}


int32_t
marker_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occured while "
                        "linking a file ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_QUOTA)
                mq_initiate_quota_txn (this, &local->loc);

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);
out:
        marker_local_unref (local);

        return 0;
}

int32_t
marker_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, newloc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_link_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->link, oldloc, newloc);
        return 0;
err:
        STACK_UNWIND_STRICT (link, frame, -1, ENOMEM, NULL, NULL, NULL, NULL);

        return 0;
}


int32_t
marker_rename_done (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        marker_local_t *local  = NULL, *oplocal = NULL;
        loc_t           newloc = {0, };
        marker_conf_t  *priv   = NULL;

        local = frame->local;
        oplocal = local->oplocal;

        priv = this->private;

        frame->local = NULL;

        if (op_ret < 0) {
                if (local->err == 0) {
                        local->err = op_errno;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "inodelk (UNLOCK) failed on path:%s (gfid:%s) (%s)",
                        local->parent_loc.path,
                        uuid_utoa (local->parent_loc.inode->gfid),
                        strerror (op_errno));
        }

        if (local->stub != NULL) {
                call_resume (local->stub);
                local->stub = NULL;
        } else if (local->err != 0) {
                STACK_UNWIND_STRICT (rename, frame, -1, local->err, NULL, NULL,
                                     NULL, NULL, NULL);
        }

        mq_reduce_parent_size (this, &oplocal->loc, oplocal->contribution);

        if (local->loc.inode != NULL) {
                mq_reduce_parent_size (this, &local->loc, local->contribution);
        }

        newloc.inode = inode_ref (oplocal->loc.inode);
        newloc.path = gf_strdup (local->loc.path);
        newloc.name = strrchr (newloc.path, '/');
        if (newloc.name)
                newloc.name++;
        newloc.parent = inode_ref (local->loc.parent);

        mq_rename_update_newpath (this, &newloc);

        loc_wipe (&newloc);

        if (priv->feature_enabled & GF_XTIME) {
                //update marks on oldpath
                marker_xtime_update_marks (this, oplocal);
                marker_xtime_update_marks (this, local);
        }

        marker_local_unref (local);
        marker_local_unref (oplocal);
        return 0;
}


int32_t
marker_rename_release_newp_lock (call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno)
{
        marker_local_t  *local = NULL, *oplocal = NULL;
        struct gf_flock  lock  = {0, };

        local = frame->local;
        oplocal = local->oplocal;

        if (op_ret < 0) {
                if (local->err == 0) {
                        local->err = op_errno;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "inodelk (UNLOCK) failed on %s (gfid:%s) (%s)",
                        oplocal->parent_loc.path,
                        uuid_utoa (oplocal->parent_loc.inode->gfid),
                        strerror (op_errno));
        }

        if (local->next_lock_on == NULL) {
                marker_rename_done (frame, NULL, this, 0, 0);
                goto out;
        }

        lock.l_type   = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start  = 0;
        lock.l_len    = 0;
        lock.l_pid    = 0;

        STACK_WIND (frame,
                    marker_rename_done,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &local->parent_loc, F_SETLKW, &lock);

out:
        return 0;
}


int32_t
marker_rename_release_oldp_lock (call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno)
{
        marker_local_t  *local = NULL, *oplocal = NULL;
        struct gf_flock  lock  = {0, };

        local = frame->local;
        oplocal = local->oplocal;

        if ((op_ret < 0) && (op_errno != ENOATTR)) {
                local->err = op_errno;
        }

        //Reset frame uid and gid if set.
        if (cookie == (void *) _GF_UID_GID_CHANGED)
                MARKER_RESET_UID_GID (frame, frame->root, local);

        lock.l_type   = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start  = 0;
        lock.l_len    = 0;
        lock.l_pid    = 0;

        STACK_WIND (frame,
                    marker_rename_release_newp_lock,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &oplocal->parent_loc, F_SETLKW, &lock);
        return 0;
}


int32_t
marker_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf,
                   struct iatt *preoldparent, struct iatt *postoldparent,
                   struct iatt *prenewparent, struct iatt *postnewparent)
{
        marker_conf_t  *priv                 = NULL;
        marker_local_t *local                = NULL;
        marker_local_t *oplocal              = NULL;
        call_stub_t    *stub                 = NULL;
        int32_t         ret                  = 0;
        char            contri_key [512]     = {0, };
        loc_t           newloc               = {0, };

        local = (marker_local_t *) frame->local;

        if (local != NULL) {
                oplocal = local->oplocal;
        }

        priv = this->private;

        if (op_ret < 0) {
                if (local != NULL) {
                        local->err = op_errno;
                }

                gf_log (this->name, GF_LOG_TRACE, "%s occured while "
                        "renaming a file ", strerror (op_errno));
        }

        if (priv->feature_enabled & GF_QUOTA) {
                if ((op_ret < 0) || (local == NULL)) {
                        goto quota_err;
                }

                stub = fop_rename_cbk_stub (frame, default_rename_cbk, op_ret,
                                            op_errno, buf, preoldparent,
                                            postoldparent, prenewparent,
                                            postnewparent);
                if (stub == NULL) {
                        local->err = ENOMEM;
                        goto quota_err;
                }

                local->stub = stub;

                GET_CONTRI_KEY (contri_key, oplocal->loc.parent->gfid, ret);
                if (ret < 0) {
                        local->err = ENOMEM;
                        goto quota_err;
                }

                /* Removexattr requires uid and gid to be 0,
                 * reset them in the callback.
                 */
                MARKER_SET_UID_GID (frame, local, frame->root);

                newloc.inode = inode_ref (oplocal->loc.inode);
                newloc.path = gf_strdup (local->loc.path);
                newloc.name = strrchr (newloc.path, '/');
                if (newloc.name)
                        newloc.name++;
                newloc.parent = inode_ref (local->loc.parent);

                STACK_WIND_COOKIE (frame, marker_rename_release_oldp_lock,
                                   frame->cookie, FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->removexattr,
                                   &newloc, contri_key);

                loc_wipe (&newloc);
        } else {
                frame->local = NULL;

                STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, buf,
                                     preoldparent, postoldparent, prenewparent,
                                     postnewparent);

                if ((op_ret < 0) || (local == NULL)) {
                        goto out;
                }

                if (priv->feature_enabled & GF_XTIME) {
                        //update marks on oldpath
                        marker_xtime_update_marks (this, oplocal);
                        marker_xtime_update_marks (this, local);
                }
        }

out:
        if (!(priv->feature_enabled & GF_QUOTA)) {
                marker_local_unref (local);
                marker_local_unref (oplocal);
        }

        return 0;

quota_err:
        marker_rename_release_oldp_lock (frame, NULL, this, 0, 0);
        return 0;
}


int32_t
marker_do_rename (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *dict)

{
        marker_local_t       *local           = NULL, *oplocal = NULL;
        char                  contri_key[512] = {0, };
        int32_t               ret             = 0;
        int64_t              *contribution    = 0;

        local = frame->local;
        oplocal = local->oplocal;

        //Reset frame uid and gid if set.
        if (cookie == (void *) _GF_UID_GID_CHANGED)
                MARKER_RESET_UID_GID (frame, frame->root, local);

        if ((op_ret < 0) && (op_errno != ENOATTR)) {
                local->err = op_errno;
                gf_log (this->name, GF_LOG_WARNING,
                        "fetching contribution values from %s (gfid:%s) "
                        "failed (%s)", local->loc.path,
                        uuid_utoa (local->loc.inode->gfid),
                        strerror (op_errno));
                goto err;
        }

        if (local->loc.inode != NULL) {
                GET_CONTRI_KEY (contri_key, local->loc.parent->gfid, ret);
                if (ret < 0) {
                        local->err = errno;
                        goto err;
                }

                if (dict_get_bin (dict, contri_key,
                                  (void **) &contribution) == 0) {
                        local->contribution = ntoh64 (*contribution);
                }
        }

        STACK_WIND (frame, marker_rename_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename, &oplocal->loc,
                    &local->loc);

        return 0;

err:
        marker_rename_release_oldp_lock (frame, NULL, this, 0, 0);
        return 0;
}


int32_t
marker_get_newpath_contribution (call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno, dict_t *dict)
{
        marker_local_t *local           = NULL, *oplocal = NULL;
        char            contri_key[512] = {0, };
        int32_t         ret             = 0;
        int64_t        *contribution    = 0;

        local = frame->local;
        oplocal = local->oplocal;

        //Reset frame uid and gid if set.
        if (cookie == (void *) _GF_UID_GID_CHANGED)
                MARKER_RESET_UID_GID (frame, frame->root, local);

        if ((op_ret < 0) && (op_errno != ENOATTR)) {
                local->err = op_errno;
                gf_log (this->name, GF_LOG_WARNING,
                        "fetching contribution values from %s (gfid:%s) "
                        "failed (%s)", oplocal->loc.path,
                        uuid_utoa (oplocal->loc.inode->gfid),
                        strerror (op_errno));
                goto err;
        }

        GET_CONTRI_KEY (contri_key, oplocal->loc.parent->gfid, ret);
        if (ret < 0) {
                local->err = errno;
                goto err;
        }

        if (dict_get_bin (dict, contri_key, (void **) &contribution) == 0)
                oplocal->contribution = ntoh64 (*contribution);

        if (local->loc.inode != NULL) {
                GET_CONTRI_KEY (contri_key, local->loc.parent->gfid, ret);
                if (ret < 0) {
                        local->err = errno;
                        goto err;
                }

                /* getxattr requires uid and gid to be 0,
                 * reset them in the callback.
                 */
                MARKER_SET_UID_GID (frame, local, frame->root);

                STACK_WIND_COOKIE (frame, marker_do_rename,
                                   frame->cookie, FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->getxattr,
                                   &local->loc, contri_key);
        } else {
                marker_do_rename (frame, NULL, this, 0, 0, NULL);
        }

        return 0;
err:
        marker_rename_release_oldp_lock (frame, NULL, this, 0, 0);
        return 0;
}


int32_t
marker_get_oldpath_contribution (call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno)
{
        marker_local_t *local           = NULL, *oplocal = NULL;
        char            contri_key[512] = {0, };
        int32_t         ret             = 0;

        local = frame->local;
        oplocal = local->oplocal;

        if (op_ret < 0) {
                local->err = op_errno;
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot hold inodelk on %s (gfid:%s) (%s)",
                        local->next_lock_on->path,
                        uuid_utoa (local->next_lock_on->inode->gfid),
                        strerror (op_errno));
                goto lock_err;
        }

        GET_CONTRI_KEY (contri_key, oplocal->loc.parent->gfid, ret);
        if (ret < 0) {
                local->err = errno;
                goto quota_err;
        }

        /* getxattr requires uid and gid to be 0,
         * reset them in the callback.
         */
        MARKER_SET_UID_GID (frame, local, frame->root);

        STACK_WIND_COOKIE (frame, marker_get_newpath_contribution,
                           frame->cookie, FIRST_CHILD(this),
                           FIRST_CHILD(this)->fops->getxattr,
                           &oplocal->loc, contri_key);
        return 0;

quota_err:
        marker_rename_release_oldp_lock (frame, NULL, this, 0, 0);
        return 0;

lock_err:
        if ((local->next_lock_on == NULL)
            || (local->next_lock_on == &local->parent_loc)) {
                local->next_lock_on = NULL;
                marker_rename_release_oldp_lock (frame, NULL, this, 0, 0);
        } else {
                marker_rename_release_newp_lock (frame, NULL, this, 0, 0);
        }

        return 0;
}


int32_t
marker_rename_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno)
{
        marker_local_t  *local = NULL, *oplocal = NULL;
        loc_t           *loc   = NULL;
        struct gf_flock  lock  = {0, };

        local = frame->local;
        oplocal = local->oplocal;

        if (op_ret < 0) {
                if (local->next_lock_on != &oplocal->parent_loc) {
                        loc = &oplocal->parent_loc;
                } else {
                        loc = &local->parent_loc;
                }

                local->err = op_errno;
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot hold inodelk on %s (gfid:%s) (%s)",
                        loc->path, uuid_utoa (loc->inode->gfid),
                        strerror (op_errno));
                goto err;
        }

        if (local->next_lock_on != NULL) {
                lock.l_len    = 0;
                lock.l_start  = 0;
                lock.l_type   = F_WRLCK;
                lock.l_whence = SEEK_SET;

                STACK_WIND (frame,
                            marker_get_oldpath_contribution,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->inodelk,
                            this->name, local->next_lock_on,
                            F_SETLKW, &lock);
        } else {
                marker_get_oldpath_contribution (frame, 0, this, 0, 0);
        }

        return 0;

err:
        marker_rename_done (frame, NULL, this, 0, 0);
        return 0;
}


int32_t
marker_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
               loc_t *newloc)
{
        int32_t         ret              = 0;
        marker_local_t *local            = NULL;
        marker_local_t *oplocal          = NULL;
        marker_conf_t  *priv             = NULL;
        struct gf_flock lock             = {0, };
        loc_t          *lock_on          = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto rename_wind;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ALLOCATE_OR_GOTO (oplocal, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, oplocal);

        frame->local = local;

        local->oplocal = marker_local_ref (oplocal);

        ret = loc_copy (&local->loc, newloc);
        if (ret < 0)
                goto err;

        ret = loc_copy (&oplocal->loc, oldloc);
        if (ret < 0)
                goto err;

        if (!(priv->feature_enabled & GF_QUOTA)) {
                goto rename_wind;
        }

        ret = mq_inode_loc_fill (NULL, newloc->parent, &local->parent_loc);
        if (ret < 0)
                goto err;

        ret = mq_inode_loc_fill (NULL, oldloc->parent, &oplocal->parent_loc);
        if (ret < 0)
                goto err;

        if ((newloc->inode != NULL) && (newloc->parent != oldloc->parent)
            && (uuid_compare (newloc->parent->gfid,
                              oldloc->parent->gfid) < 0)) {
                lock_on = &local->parent_loc;
                local->next_lock_on = &oplocal->parent_loc;
        } else {
                lock_on = &oplocal->parent_loc;
                if ((newloc->inode != NULL) && (newloc->parent
                                                != oldloc->parent)) {
                        local->next_lock_on = &local->parent_loc;
                }
        }

        lock.l_len    = 0;
        lock.l_start  = 0;
        lock.l_type   = F_WRLCK;
        lock.l_whence = SEEK_SET;

        STACK_WIND (frame,
                    marker_rename_inodelk_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, lock_on,
                    F_SETLKW, &lock);

        return 0;

rename_wind:
        STACK_WIND (frame, marker_rename_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename, oldloc, newloc);

        return 0;
err:
        STACK_UNWIND_STRICT (rename, frame, -1, ENOMEM, NULL,
                             NULL, NULL, NULL, NULL);

        return 0;
}


int32_t
marker_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occured while "
                        "truncating a file ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf,
                             postbuf);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_QUOTA)
                mq_initiate_quota_txn (this, &local->loc);

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);

out:
        marker_local_unref (local);

        return 0;
}

int32_t
marker_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_truncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate, loc, offset);
        return 0;
err:
        STACK_UNWIND_STRICT (truncate, frame, -1, ENOMEM, NULL, NULL);

        return 0;
}


int32_t
marker_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occured while "
                        "truncating a file ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, prebuf,
                             postbuf);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_QUOTA)
                mq_initiate_quota_txn (this, &local->loc);

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);
out:
        marker_local_unref (local);

        return 0;
}

int32_t
marker_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ret = marker_inode_loc_fill (fd->inode, &local->loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_ftruncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate, fd, offset);
        return 0;
err:
        STACK_UNWIND_STRICT (ftruncate, frame, -1, ENOMEM, NULL, NULL);

        return 0;
}


int32_t
marker_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent)
{
        marker_conf_t      *priv    = NULL;
        marker_local_t     *local   = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occured while "
                        "creating symlinks ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_QUOTA)
                mq_set_inode_xattr (this, &local->loc);

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);
out:
        marker_local_unref (local);

        return 0;
}

int
marker_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
                loc_t *loc, dict_t *params)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_symlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->symlink, linkpath, loc, params);
        return 0;
err:
        STACK_UNWIND_STRICT (symlink, frame, -1, ENOMEM, NULL,
                             NULL, NULL, NULL);
        return 0;
}


int32_t
marker_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occured while "
                        "creating symlinks ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno, inode,
                             buf, preparent, postparent);

        if (op_ret == -1 ||  local == NULL)
                goto out;

        priv = this->private;

        if ((priv->feature_enabled & GF_QUOTA) && (S_ISREG (local->mode))) {
                mq_set_inode_xattr (this, &local->loc);
        }

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);
out:
        marker_local_unref (local);

        return 0;
}

int
marker_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
              dev_t rdev, dict_t *parms)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        local->mode = mode;

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_mknod_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mknod, loc, mode, rdev, parms);
        return 0;
err:
        STACK_UNWIND_STRICT (mknod, frame, -1, ENOMEM, NULL,
                             NULL, NULL, NULL);
        return 0;
}


/* when a call from the special client is received on
 * key trusted.glusterfs.volume-mark with value "RESET"
 * or if the value is 0length, update the change the
 * access time and modification time via touching the
 * timestamp file.
 */
int32_t
call_from_sp_client_to_reset_tmfile (call_frame_t *frame,
                                     xlator_t     *this,
                                     dict_t       *dict)
{
        int32_t          fd       = 0;
        int32_t          op_ret   = 0;
        int32_t          op_errno = 0;
        data_t          *data     = NULL;
        marker_conf_t   *priv     = NULL;

        if (frame == NULL || this == NULL || dict == NULL)
                return -1;

        priv = this->private;

        data = dict_get (dict, "trusted.glusterfs.volume-mark");
        if (data == NULL)
                return -1;

        if (frame->root->pid != GF_CLIENT_PID_GSYNCD) {
                op_ret = -1;
                op_errno = EPERM;

                goto out;
        }

        if (data->len == 0 || (data->len == 5 &&
                               memcmp (data->data, "RESET", 5) == 0)) {
                fd = open (priv->timestamp_file, O_WRONLY|O_TRUNC);
                if (fd != -1) {
                        /* TODO check  whether the O_TRUNC would update the
                         * timestamps on a zero length file on all machies.
                         */
                        close (fd);
                }

                if (fd != -1 || errno == ENOENT) {
                        op_ret = 0;
                        op_errno = 0;
                } else {
                        op_ret = -1;
                        op_errno = errno;
                }
        } else {
                op_ret = -1;
                op_errno = EINVAL;
        }
out:
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno);

        return 0;
}


int32_t
marker_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occured while "
                        "creating symlinks ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);
out:
        marker_local_unref (local);

        return 0;
}

int32_t
marker_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
                 int32_t flags)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        ret = call_from_sp_client_to_reset_tmfile (frame, this, dict);
        if (ret == 0)
                return 0;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_setxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr, loc, dict, flags);
        return 0;
err:
        STACK_UNWIND_STRICT (setxattr, frame, -1, ENOMEM);

        return 0;
}


int32_t
marker_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occured while "
                        "creating symlinks ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);
out:
        marker_local_unref (local);

        return 0;
}

int32_t
marker_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
                  int32_t flags)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        ret = call_from_sp_client_to_reset_tmfile (frame, this, dict);
        if (ret == 0)
                return 0;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ret = marker_inode_loc_fill (fd->inode, &local->loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_fsetxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetxattr, fd, dict, flags);
        return 0;
err:
        STACK_UNWIND_STRICT (fsetxattr, frame, -1, ENOMEM);

        return 0;
}


int32_t
marker_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                     struct iatt *statpost)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "%s occured while "
                        "creating symlinks ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (fsetattr, frame, op_ret, op_errno, statpre,
                             statpost);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);
out:
        marker_local_unref (local);

        return 0;
}


int32_t
marker_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 struct iatt *stbuf, int32_t valid)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ret = marker_inode_loc_fill (fd->inode, &local->loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_fsetattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetattr, fd, stbuf, valid);
        return 0;
err:
        STACK_UNWIND_STRICT (fsetattr, frame, -1, ENOMEM, NULL, NULL);

        return 0;
}


int32_t
marker_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                    struct iatt *statpost)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        if (op_ret == -1) {
                gf_log (this->name, ((op_errno == ENOENT) ? GF_LOG_DEBUG :
                                     GF_LOG_ERROR),
                        "%s occured during setattr of %s",
                        strerror (op_errno),
                        (local ? local->loc.path : "<nul>"));
        }

        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, statpre,
                             statpost);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);
out:
        marker_local_unref (local);

        return 0;
}

int32_t
marker_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                struct iatt *stbuf, int32_t valid)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_setattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setattr, loc, stbuf, valid);
        return 0;
err:
        STACK_UNWIND_STRICT (setattr, frame, -1, ENOMEM, NULL, NULL);

        return 0;
}


int32_t
marker_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "%s occured while "
                        "creating symlinks ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);
out:
        marker_local_unref (local);

        return 0;
}

int32_t
marker_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    const char *name)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_removexattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr, loc, name);
        return 0;
err:
        STACK_UNWIND_STRICT (removexattr, frame, -1, ENOMEM);

        return 0;
}


int32_t
marker_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, dict_t *dict, struct iatt *postparent)
{
        marker_conf_t  *priv    = NULL;
        marker_local_t *local   = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "lookup failed with %s",
                        strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf,
                             dict, postparent);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_QUOTA) {
                mq_xattr_state (this, &local->loc, dict, *buf);
        }

out:
        marker_local_unref (local);

        return 0;
}

int32_t
marker_lookup (call_frame_t *frame, xlator_t *this,
               loc_t *loc, dict_t *xattr_req)
{
        int32_t         ret     = 0;
        marker_local_t *local   = NULL;
        marker_conf_t  *priv    = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        ALLOCATE_OR_GOTO (local, marker_local_t, err);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);
        if (ret == -1)
                goto err;

        if ((priv->feature_enabled & GF_QUOTA) && xattr_req)
                mq_req_xattr (this, loc, xattr_req);
wind:
        STACK_WIND (frame, marker_lookup_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, loc, xattr_req);
        return 0;
err:
        STACK_UNWIND_STRICT (lookup, frame, -1, 0, NULL, NULL, NULL, NULL);

        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_marker_mt_end + 1);

        if (ret != 0) {
                gf_log(this->name, GF_LOG_ERROR, "Memory accounting init"
                       "failed");
                return ret;
        }

        return ret;
}


int32_t
init_xtime_priv (xlator_t *this, dict_t *options)
{
        data_t          *data    = NULL;
        int32_t          ret     = -1;
        marker_conf_t   *priv    = NULL;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO (this->name, options, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);

        priv = this->private;

        if((data = dict_get (options, VOLUME_UUID)) != NULL) {
                priv->volume_uuid = data->data;

                ret = uuid_parse (priv->volume_uuid, priv->volume_uuid_bin);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "invalid volume uuid %s", priv->volume_uuid);
                        goto out;
                }

                ret = gf_asprintf (& (priv->marker_xattr), "%s.%s.%s",
                                   MARKER_XATTR_PREFIX, priv->volume_uuid,
                                   XTIME);

                if (ret == -1){
                        priv->marker_xattr = NULL;

                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to allocate memory");
                        goto out;
                }

                gf_log (this->name, GF_LOG_DEBUG,
                        "the volume-uuid = %s", priv->volume_uuid);
        } else {
                priv->volume_uuid = NULL;

                gf_log (this->name, GF_LOG_ERROR,
                        "please specify the volume-uuid"
                        "in the translator options");

                return -1;
        }

        if ((data = dict_get (options, TIMESTAMP_FILE)) != NULL) {
                priv->timestamp_file = data->data;

                gf_log (this->name, GF_LOG_DEBUG,
                        "the timestamp-file is = %s",
                        priv->timestamp_file);

        } else {
                priv->timestamp_file = NULL;

                gf_log (this->name, GF_LOG_ERROR,
                        "please specify the timestamp-file"
                        "in the translator options");

                goto out;
        }

        ret = 0;
out:
        return ret;
}

void
marker_xtime_priv_cleanup (xlator_t *this)
{
        marker_conf_t *priv = NULL;

        GF_VALIDATE_OR_GOTO ("marker", this, out);

        priv = (marker_conf_t *) this->private;

        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        if (priv->volume_uuid != NULL)
                GF_FREE (priv->volume_uuid);

        if (priv->timestamp_file != NULL)
                GF_FREE (priv->timestamp_file);

        if (priv->marker_xattr != NULL)
                GF_FREE (priv->marker_xattr);
out:
        return;
}

void
marker_priv_cleanup (xlator_t *this)
{
        marker_conf_t *priv = NULL;

        GF_VALIDATE_OR_GOTO ("marker", this, out);

        priv = (marker_conf_t *) this->private;

        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        marker_xtime_priv_cleanup (this);

        LOCK_DESTROY (&priv->lock);

        GF_FREE (priv);
out:
        return;
}

int32_t
reconfigure (xlator_t *this, dict_t *options)
{
        int32_t         ret     = -1;
        data_t         *data    = NULL;
        gf_boolean_t    flag    = _gf_false;
        marker_conf_t  *priv    = NULL;

        GF_ASSERT (this);
        GF_ASSERT (this->private);

        priv = this->private;

        priv->feature_enabled = 0;

        GF_VALIDATE_OR_GOTO (this->name, options, out);

        data = dict_get (options, "quota");
        if (data) {
                ret = gf_string2boolean (data->data, &flag);
                if (ret == 0 && flag == _gf_true) {
                        ret = init_quota_priv (this);
                        if (ret < 0) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "failed to initialize quota private");
                        } else {
                                priv->feature_enabled |= GF_QUOTA;
                        }
                }
        }

        data = dict_get (options, "xtime");
        if (data) {
                ret = gf_string2boolean (data->data, &flag);
                if (ret == 0 && flag == _gf_true) {
                        marker_xtime_priv_cleanup (this);

                        ret = init_xtime_priv (this, options);
                        if (ret < 0) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "failed to initialize xtime private, "
                                        "xtime updation will fail");
                        } else {
                                priv->feature_enabled |= GF_XTIME;
                        }
                }
        }
out:
        return 0;
}


int32_t
init (xlator_t *this)
{
        dict_t        *options = NULL;
        data_t        *data    = NULL;
        int32_t        ret     = 0;
        gf_boolean_t   flag    = _gf_false;
        marker_conf_t *priv    = NULL;

        if (!this->children) {
                gf_log (this->name, GF_LOG_ERROR,
                        "marker translator needs subvolume defined.");
                return -1;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Volume is dangling.");
                return -1;
        }

        options = this->options;

        ALLOCATE_OR_GOTO (this->private, marker_conf_t, err);

        priv = this->private;

        priv->feature_enabled = 0;

        LOCK_INIT (&priv->lock);

        data = dict_get (options, "quota");
        if (data) {
                ret = gf_string2boolean (data->data, &flag);
                if (ret == 0 && flag == _gf_true) {
                        ret = init_quota_priv (this);
                        if (ret < 0)
                                goto err;

                        priv->feature_enabled |= GF_QUOTA;
                }
        }

        data = dict_get (options, "xtime");
        if (data) {
                ret = gf_string2boolean (data->data, &flag);
                if (ret == 0 && flag == _gf_true) {
                        ret = init_xtime_priv (this, options);
                        if (ret < 0)
                                goto err;

                        priv->feature_enabled |= GF_XTIME;
                }
        }

        return 0;
err:
        marker_priv_cleanup (this);

        return -1;
}

int32_t
marker_forget (xlator_t *this, inode_t *inode)
{
        marker_inode_ctx_t *ctx   = NULL;
        uint64_t            value = 0;

        if (inode_ctx_del (inode, this, &value) != 0)
                goto out;

        ctx = (marker_inode_ctx_t *)(unsigned long)value;
        if (ctx == NULL) {
                goto out;
        }

        mq_forget (this, ctx->quota_ctx);

        GF_FREE (ctx);
out:
        return 0;
}

void
fini (xlator_t *this)
{
        marker_priv_cleanup (this);
}

struct xlator_fops fops = {
        .lookup      = marker_lookup,
        .create      = marker_create,
        .mkdir       = marker_mkdir,
        .writev      = marker_writev,
        .truncate    = marker_truncate,
        .ftruncate   = marker_ftruncate,
        .symlink     = marker_symlink,
        .link        = marker_link,
        .unlink      = marker_unlink,
        .rmdir       = marker_rmdir,
        .rename      = marker_rename,
        .mknod       = marker_mknod,
        .setxattr    = marker_setxattr,
        .fsetxattr   = marker_fsetxattr,
        .setattr     = marker_setattr,
        .fsetattr    = marker_fsetattr,
        .removexattr = marker_removexattr,
        .getxattr    = marker_getxattr
};

struct xlator_cbks cbks = {
        .forget = marker_forget
};

struct volume_options options[] = {
        {.key = {"volume-uuid"}},
        {.key = {"timestamp-file"}},
        {.key = {"quota"}},
        {.key = {"xtime"}},
        {.key = {NULL}}
};
