/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
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

#include "xlator.h"
#include "defaults.h"
#include "libxlator.h"
#include "marker.h"
#include "marker-mem-types.h"
#include "marker-quota.h"
#include "marker-quota-helper.h"
#include "marker-common.h"
#include "byte-order.h"
#include "syncop.h"

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
                if (uuid_is_null (loc->gfid)) {
                        uuid_copy (loc->gfid, loc->inode->gfid);
                }
        }

        if (parent)
                loc->parent = inode_ref (parent);

        if (path) {
                loc->path = gf_strdup (path);
                if (!loc->path) {
                        gf_log ("loc fill", GF_LOG_ERROR, "strdup failed");
                        goto loc_wipe;
                }

                loc->name = strrchr (loc->path, '/');
                if (loc->name)
                        loc->name++;
        }

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
        int              ret          = -1;
	inode_t         *parent = NULL;

        if ((!inode) || (!loc))
                return ret;

	parent = inode_parent (inode, NULL, NULL);

        ret = inode_path (inode, NULL, &resolvedpath);
        if (ret < 0)
                goto err;

        ret = marker_loc_fill (loc, inode, parent, resolvedpath);
        if (ret < 0)
                goto err;

err:
	if (parent)
		inode_unref (parent);

        GF_FREE (resolvedpath);

        return ret;
}

int32_t
marker_trav_parent (marker_local_t *local)
{
        int32_t ret = 0;
        loc_t   loc = {0, };
        inode_t *parent = NULL;
        int8_t  need_unref = 0;

        if (!local->loc.parent) {
                parent = inode_parent (local->loc.inode, NULL, NULL);
                if (parent)
                        need_unref = 1;
        } else
                parent = local->loc.parent;

        ret = marker_inode_loc_fill (parent, &loc);

        if (ret < 0) {
                ret = -1;
                goto out;
        }

        loc_wipe (&local->loc);

        local->loc = loc;
out:
        if (need_unref)
                inode_unref (parent);

        return ret;
}

int32_t
marker_error_handler (xlator_t *this, marker_local_t *local, int32_t op_errno)
{
         marker_conf_t *priv = NULL;
         const char    *path = NULL;

         priv = (marker_conf_t *) this->private;
         path = local
                 ? (local->loc.path
                    ? local->loc.path : uuid_utoa(local->loc.gfid))
                 : "<nul>";

         gf_log (this->name, GF_LOG_CRITICAL,
                 "Indexing gone corrupt at %s (reason: %s)."
                 " Geo-replication slave content needs to be revalidated",
                 path, strerror (op_errno));
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
        if (local->xdata)
                dict_unref (local->xdata);

        if (local->oplocal) {
                marker_local_unref (local->oplocal);
                local->oplocal = NULL;
        }
        mem_put (local);
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
                               const char *name, struct volume_mark *vol_mark,
                               dict_t *xdata)
{
        int32_t   ret  = -1;
        dict_t   *dict = NULL;

        if (vol_mark == NULL){
                STACK_UNWIND_STRICT (getxattr, frame, -1, ENOMEM, NULL, NULL);

                goto out;
        }

        dict = dict_new ();

        ret = dict_set_bin (dict, (char *)name, vol_mark,
                            sizeof (struct volume_mark));
        if (ret)
                gf_log (this->name, GF_LOG_WARNING, "failed to set key %s",
                        name);

        STACK_UNWIND_STRICT (getxattr, frame, 0, 0, dict, xdata);

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

        marker_getxattr_stampfile_cbk (frame, this, name, vol_mark, NULL);
out:
        return ret;
}

int32_t
marker_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *dict,
                     dict_t *xdata)
{
        int            ret    = 0;
        char           *src   = NULL;
        char           *dst   = NULL;
        int            len    = 0;
        marker_local_t *local = NULL;

        local = frame->local;


        if (cookie) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Filtering the quota extended attributes");

                /* If the getxattr is from a non special client, then do not
                   copy the quota related xattrs (except the quota limit key
                   i.e trusted.glusterfs.quota.limit-set which has been set by
                   glusterd on the directory on which quota limit is set.) for
                   directories. Let the healing of xattrs happen upon lookup.
                   NOTE: setting of trusted.glusterfs.quota.limit-set as of now
                   happens from glusterd. It should be moved to quotad. Also
                   trusted.glusterfs.quota.limit-set is set on directory which
                   is permanent till quota is removed on that directory or limit
                   is changed. So let that xattr be healed by other xlators
                   properly whenever directory healing is done.
                */
                ret = dict_get_ptr_and_len (dict, QUOTA_LIMIT_KEY,
                                            (void **)&src, &len);
                if (ret) {
                        gf_log (this->name, GF_LOG_DEBUG, "dict_get on %s "
                                "failed", QUOTA_LIMIT_KEY);
                } else {
                        dst = GF_CALLOC (len, sizeof (char), gf_common_mt_char);
                        if (dst)
                                memcpy (dst, src, len);
                }

                /*
                 * Except limit-set xattr, rest of the xattrs are maintained
                 * by quota xlator. Don't expose them to other xlators.
                 * This filter makes sure quota xattrs are not healed as part of
                 * metadata self-heal
                 */
                GF_REMOVE_INTERNAL_XATTR ("trusted.glusterfs.quota*", dict);
                if (!ret && IA_ISDIR (local->loc.inode->ia_type) && dst) {
                        ret = dict_set_dynptr (dict, QUOTA_LIMIT_KEY,
                                               dst, len);
                        if (ret)
                                gf_log (this->name, GF_LOG_WARNING, "setting "
                                        "key %s failed", QUOTA_LIMIT_KEY);
                        else
                                dst = NULL;
                }
        }

        GF_FREE (dst);

        frame->local = NULL;
        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict, xdata);
        marker_local_unref (local);
        return 0;
}

int32_t
marker_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                 const char *name, dict_t *xdata)
{
        gf_boolean_t   ret    = _gf_false;
        marker_conf_t *priv   = NULL;
        unsigned long  cookie = 0;
        marker_local_t *local = NULL;

        priv = this->private;

        frame->local = mem_get0 (this->local_pool);
        local = frame->local;
        if (local == NULL)
                goto out;

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);
        if (ret < 0)
                goto out;

        gf_log (this->name, GF_LOG_DEBUG, "USER:PID = %d", frame->root->pid);

        if (priv && priv->feature_enabled & GF_XTIME)
                ret = call_from_special_client (frame, this, name);

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
                                   name, xdata);
        }

        return 0;
out:
        frame->local = NULL;
        STACK_UNWIND_STRICT (getxattr, frame, -1, ENOMEM, NULL, NULL);
        marker_local_unref (local);
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
                              int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        int32_t         ret   = 0;
        int32_t         done  = 0;
        marker_local_t *local = NULL;

        local = (marker_local_t*) frame->local;

        if (op_ret == -1 && op_errno == ENOSPC) {
                marker_error_handler (this, local, op_errno);
                done = 1;
                goto out;
        }

        if (local) {
                if (local->loc.path && strcmp (local->loc.path, "/") == 0) {
                        done = 1;
                        goto out;
                }
                if (__is_root_gfid (local->loc.gfid)) {
                        done = 1;
                        goto out;
                }
        }

        ret = marker_trav_parent (local);

        if (ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "Error occurred "
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
        int32_t          ret   = -1;
        dict_t          *dict  = NULL;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        local = (marker_local_t*) frame->local;

        if (!local)
                goto out;

        dict = dict_new ();

        if (!dict)
                goto out;

        if (local->loc.inode && uuid_is_null (local->loc.gfid))
                uuid_copy (local->loc.gfid, local->loc.inode->gfid);

        GF_UUID_ASSERT (local->loc.gfid);

        ret = dict_set_static_bin (dict, priv->marker_xattr,
                                   (void *)local->timebuf, 8);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set marker xattr (%s)", local->loc.path);
                goto out;
        }

        STACK_WIND (frame, marker_specific_setxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr, &local->loc, dict, 0,
                    NULL);

        ret = 0;
out:
        if (dict)
                dict_unref (dict);

        return ret;
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
        marker_conf_t *priv = NULL;

        GF_VALIDATE_OR_GOTO ("marker", this, out);
        GF_VALIDATE_OR_GOTO (this->name, local, out);

        priv = this->private;

        if ((local->pid == GF_CLIENT_PID_GSYNCD
             && !(priv->feature_enabled & GF_XTIME_GSYNC_FORCE))
            || (local->pid == GF_CLIENT_PID_DEFRAG))
                goto out;

        marker_gettimeofday (local);

        marker_local_ref (local);

        marker_create_frame (this, local);
out:
        return 0;
}


int32_t
marker_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
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
                             buf, preparent, postparent, xdata);

        if (op_ret == -1 || local == NULL)
                goto out;

        if (uuid_is_null (local->loc.gfid))
                uuid_copy (local->loc.gfid, buf->ia_gfid);

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
              mode_t umask, dict_t *xdata)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_mkdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mkdir, loc, mode, umask, xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (mkdir, frame, -1, ENOMEM, NULL,
                             NULL, NULL, NULL, NULL);
        return 0;
}


int32_t
marker_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
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
                             preparent, postparent, xdata);

        if (op_ret == -1 || local == NULL)
                goto out;

        if (uuid_is_null (local->loc.gfid))
                uuid_copy (local->loc.gfid, buf->ia_gfid);

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
               mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_create_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create, loc, flags, mode, umask,
                    fd, xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (create, frame, -1, ENOMEM, NULL, NULL, NULL, NULL,
                             NULL, NULL);

        return 0;
}


int32_t
marker_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
        marker_conf_t      *priv    = NULL;
        marker_local_t     *local   = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "error occurred "
                        "while write, %s", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf,
                             xdata);

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
               off_t offset, uint32_t flags,
               struct iobref *iobref, dict_t *xdata)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = marker_inode_loc_fill (fd->inode, &local->loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_writev_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev, fd, vector, count, offset,
                    flags, iobref, xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (writev, frame, -1, ENOMEM, NULL, NULL, NULL);

        return 0;
}


int32_t
marker_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
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
                             postparent, xdata);

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
marker_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
              dict_t *xdata)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_rmdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rmdir, loc, flags, xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (rmdir, frame, -1, ENOMEM, NULL, NULL, NULL);

        return 0;
}


int32_t
marker_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
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
                             postparent, xdata);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_QUOTA) {
                if (!local->skip_txn)
                        mq_reduce_parent_size (this, &local->loc, -1);
        }

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);
out:
        marker_local_unref (local);

        return 0;
}


int32_t
marker_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
               dict_t *xdata)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto unlink_wind;

        local = mem_get0 (this->local_pool);
        local->xflag = xflag;
        if (xdata)
                local->xdata = dict_ref (xdata);
        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;

        if (xdata && dict_get (xdata, GLUSTERFS_MARKER_DONT_ACCOUNT_KEY)) {
                local->skip_txn = 1;
                goto unlink_wind;
        }

unlink_wind:
        STACK_WIND (frame, marker_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink, loc, xflag, xdata);
        return 0;
err:
        frame->local = NULL;
        STACK_UNWIND_STRICT (unlink, frame, -1, ENOMEM, NULL, NULL, NULL);
        marker_local_unref (local);
        return 0;
}


int32_t
marker_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occurred while "
                        "linking a file ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_QUOTA) {
                if (!local->skip_txn)
                        mq_set_inode_xattr (this, &local->loc);
        }


        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);
out:
        marker_local_unref (local);

        return 0;
}

int32_t
marker_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
             dict_t *xdata)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, newloc);

        if (ret == -1)
                goto err;

        if (xdata && dict_get (xdata, GLUSTERFS_MARKER_DONT_ACCOUNT_KEY))
                local->skip_txn = 1;
wind:
        STACK_WIND (frame, marker_link_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->link, oldloc, newloc, xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (link, frame, -1, ENOMEM, NULL, NULL, NULL, NULL,
                             NULL);

        return 0;
}


int32_t
marker_rename_done (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
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
                                     NULL, NULL, NULL, NULL);
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

        mq_set_inode_xattr (this, &newloc);

        loc_wipe (&newloc);

        if (priv->feature_enabled & GF_XTIME) {
                //update marks on oldpath
                uuid_copy (local->loc.gfid, oplocal->loc.inode->gfid);
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
                                 int32_t op_errno, dict_t *xdata)
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
                marker_rename_done (frame, NULL, this, 0, 0, NULL);
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
                    this->name, &local->parent_loc, F_SETLKW, &lock, NULL);

out:
        return 0;
}


int32_t
marker_rename_release_oldp_lock (call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno, dict_t *xdata)
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
                    this->name, &oplocal->parent_loc, F_SETLKW, &lock, NULL);
        return 0;
}


int32_t
marker_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf,
                   struct iatt *preoldparent, struct iatt *postoldparent,
                   struct iatt *prenewparent, struct iatt *postnewparent,
                   dict_t *xdata)
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

                gf_log (this->name, GF_LOG_TRACE, "%s occurred while "
                        "renaming a file ", strerror (op_errno));
        }

        if (priv->feature_enabled & GF_QUOTA) {
                if ((op_ret < 0) || (local == NULL)) {
                        goto quota_err;
                }

                stub = fop_rename_cbk_stub (frame, default_rename_cbk, op_ret,
                                            op_errno, buf, preoldparent,
                                            postoldparent, prenewparent,
                                            postnewparent, xdata);
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
                uuid_copy (newloc.gfid, oplocal->loc.inode->gfid);

                STACK_WIND_COOKIE (frame, marker_rename_release_oldp_lock,
                                   frame->cookie, FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->removexattr,
                                   &newloc, contri_key, NULL);

                loc_wipe (&newloc);
        } else {
                frame->local = NULL;

                STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, buf,
                                     preoldparent, postoldparent, prenewparent,
                                     postnewparent, xdata);

                if ((op_ret < 0) || (local == NULL)) {
                        goto out;
                }

                if (priv->feature_enabled & GF_XTIME) {
                        //update marks on oldpath
                        uuid_copy (local->loc.gfid, oplocal->loc.inode->gfid);
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
        marker_rename_release_oldp_lock (frame, NULL, this, 0, 0, NULL);
        return 0;
}


int32_t
marker_do_rename (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
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
                    &local->loc, NULL);

        return 0;

err:
        marker_rename_release_oldp_lock (frame, NULL, this, 0, 0, NULL);
        return 0;
}


int32_t
marker_get_newpath_contribution (call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno, dict_t *dict, dict_t *xdata)
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
                if (uuid_is_null (local->loc.gfid))
                        uuid_copy (local->loc.gfid, local->loc.inode->gfid);

                GF_UUID_ASSERT (local->loc.gfid);

                STACK_WIND_COOKIE (frame, marker_do_rename,
                                   frame->cookie, FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->getxattr,
                                   &local->loc, contri_key, NULL);
        } else {
                marker_do_rename (frame, NULL, this, 0, 0, NULL, NULL);
        }

        return 0;
err:
        marker_rename_release_oldp_lock (frame, NULL, this, 0, 0, NULL);
        return 0;
}


int32_t
marker_get_oldpath_contribution (call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno, dict_t *xdata)
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

        if (uuid_is_null (oplocal->loc.gfid))
                        uuid_copy (oplocal->loc.gfid,
                                   oplocal->loc.inode->gfid);

        GF_UUID_ASSERT (oplocal->loc.gfid);

        STACK_WIND_COOKIE (frame, marker_get_newpath_contribution,
                           frame->cookie, FIRST_CHILD(this),
                           FIRST_CHILD(this)->fops->getxattr,
                           &oplocal->loc, contri_key, NULL);
        return 0;

quota_err:
        marker_rename_release_oldp_lock (frame, NULL, this, 0, 0, NULL);
        return 0;

lock_err:
        if ((local->next_lock_on == NULL)
            || (local->next_lock_on == &local->parent_loc)) {
                local->next_lock_on = NULL;
                marker_rename_release_oldp_lock (frame, NULL, this, 0, 0, NULL);
        } else {
                marker_rename_release_newp_lock (frame, NULL, this, 0, 0, NULL);
        }

        return 0;
}


int32_t
marker_rename_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, dict_t *xdata)
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
                            F_SETLKW, &lock, NULL);
        } else {
                marker_get_oldpath_contribution (frame, 0, this, 0, 0, NULL);
        }

        return 0;

err:
        marker_rename_done (frame, NULL, this, 0, 0, NULL);
        return 0;
}


int32_t
marker_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
               loc_t *newloc, dict_t *xdata)
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

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        oplocal = mem_get0 (this->local_pool);

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
                    F_SETLKW, &lock, NULL);

        return 0;

rename_wind:
        STACK_WIND (frame, marker_rename_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename, oldloc, newloc, xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (rename, frame, -1, ENOMEM, NULL,
                             NULL, NULL, NULL, NULL, NULL);

        return 0;
}


int32_t
marker_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occurred while "
                        "truncating a file ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);

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
marker_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
                 dict_t *xdata)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_truncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (truncate, frame, -1, ENOMEM, NULL, NULL, NULL);

        return 0;
}


int32_t
marker_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occurred while "
                        "truncating a file ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);

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
marker_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                  dict_t *xdata)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = marker_inode_loc_fill (fd->inode, &local->loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_ftruncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (ftruncate, frame, -1, ENOMEM, NULL, NULL, NULL);

        return 0;
}


int32_t
marker_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
        marker_conf_t      *priv    = NULL;
        marker_local_t     *local   = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occurred while "
                        "creating symlinks ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);

        if (op_ret == -1 || local == NULL)
                goto out;

        if (uuid_is_null (local->loc.gfid))
                uuid_copy (local->loc.gfid, buf->ia_gfid);

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
                loc_t *loc, mode_t umask, dict_t *xdata)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_symlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->symlink, linkpath, loc, umask,
                    xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (symlink, frame, -1, ENOMEM, NULL,
                             NULL, NULL, NULL, NULL);
        return 0;
}


int32_t
marker_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occurred while "
                        "creating symlinks ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno, inode,
                             buf, preparent, postparent, xdata);

        if (op_ret == -1 ||  local == NULL)
                goto out;

        if (uuid_is_null (local->loc.gfid))
                uuid_copy (local->loc.gfid, buf->ia_gfid);

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
              dev_t rdev, mode_t umask, dict_t *xdata)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        local->mode = mode;

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_mknod_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mknod, loc, mode, rdev, umask,
                    xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (mknod, frame, -1, ENOMEM, NULL,
                             NULL, NULL, NULL, NULL);
        return 0;
}


int32_t
marker_fallocate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occurred while "
                        "fallocating a file ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (fallocate, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);

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
marker_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
		 off_t offset, size_t len, dict_t *xdata)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = marker_inode_loc_fill (fd->inode, &local->loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_fallocate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fallocate, fd, mode, offset, len,
		    xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (fallocate, frame, -1, ENOMEM, NULL, NULL, NULL);

        return 0;
}


int32_t
marker_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occurred during discard",
                        strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (discard, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);

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
marker_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
	       size_t len, dict_t *xdata)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = marker_inode_loc_fill (fd->inode, &local->loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_discard_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->discard, fd, offset, len, xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (discard, frame, -1, ENOMEM, NULL, NULL, NULL);

        return 0;
}

int32_t
marker_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occurred during zerofill",
                        strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (zerofill, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);

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
marker_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               off_t len, dict_t *xdata)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = marker_inode_loc_fill (fd->inode, &local->loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_zerofill_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->zerofill, fd, offset, len, xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (zerofill, frame, -1, ENOMEM, NULL, NULL, NULL);

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
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, NULL);

        return 0;
}


int32_t
marker_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occurred in "
                        "setxattr ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, xdata);

        if (op_ret == -1 || local == NULL)
                goto out;

        priv = this->private;

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);
out:
        marker_local_unref (local);

        return 0;
}

int
remove_quota_keys (dict_t *dict, char *k, data_t *v, void *data)
{
	call_frame_t 	*frame = data;
	marker_local_t 	*local = frame->local;
	xlator_t 	*this = frame->this;
	int 		ret = -1;

	ret = syncop_removexattr (FIRST_CHILD (this), &local->loc, k);
	if (ret) {
		gf_log (this->name, GF_LOG_ERROR, "%s: Failed to remove "
			"extended attribute: %s", local->loc.path, k);
                return -1;
	}
	return 0;
}

int
quota_xattr_cleaner_cbk (int ret, call_frame_t *frame, void *args)
{
        dict_t *xdata = args;
        int op_ret = -1;
        int op_errno = 0;
	marker_local_t *local = NULL;

	local = frame->local;
	frame->local = NULL;

        op_ret   = (ret < 0)? -1: 0;
        op_errno = -ret;

        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, xdata);
	marker_local_unref (local);
        return ret;
}

int
quota_xattr_cleaner (void *args)
{
        struct synctask *task  = NULL;
        call_frame_t    *frame = NULL;
        xlator_t        *this  = NULL;
        marker_local_t   *local = NULL;
        dict_t          *xdata = NULL;
        int             ret    = -1;

        task = synctask_get ();
        if (!task)
                goto out;

        frame = task->frame;
        this  = frame->this;
        local = frame->local;

        ret = syncop_listxattr (FIRST_CHILD(this), &local->loc, &xdata);
        if (ret == -1) {
                ret = -errno;
                goto out;
        }

	ret = dict_foreach_fnmatch (xdata, "trusted.glusterfs.quota.*",
                                    remove_quota_keys, frame);
        if (ret == -1) {
                ret = -errno;
                goto out;
        }
	ret = dict_foreach_fnmatch (xdata, PGFID_XATTR_KEY_PREFIX"*",
                                    remove_quota_keys, frame);
        if (ret == -1) {
                ret = -errno;
                goto out;
        }

        ret = 0;
out:
        if (xdata)
                dict_unref (xdata);

        return ret;
}

int
marker_do_xattr_cleanup (call_frame_t *frame, xlator_t *this, dict_t *xdata,
                        loc_t *loc)
{
        int           ret       = -1;
        marker_local_t *local    = NULL;

        local = mem_get0 (this->local_pool);
	if (!local)
		goto out;

        MARKER_INIT_LOCAL (frame, local);

        loc_copy (&local->loc, loc);
        ret = synctask_new (this->ctx->env, quota_xattr_cleaner,
			    quota_xattr_cleaner_cbk, frame, xdata);
        if (ret) {
		gf_log (this->name, GF_LOG_ERROR, "Failed to create synctask "
			"for cleaning up quota extended attributes");
                goto out;
	}

        ret = 0;
out:
        if (ret) {
		frame->local = NULL;
                STACK_UNWIND_STRICT (setxattr, frame, -1, ENOMEM, xdata);
		marker_local_unref (local);
        }
        return ret;
}

static inline gf_boolean_t
marker_xattr_cleanup_cmd (dict_t *dict)
{
        return (dict_get (dict, VIRTUAL_QUOTA_XATTR_CLEANUP_KEY) != NULL);
}

int32_t
marker_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
                 int32_t flags, dict_t *xdata)
{
        int32_t          ret            = 0;
        marker_local_t  *local          = NULL;
        marker_conf_t   *priv           = NULL;
        int              op_errno       = ENOMEM;

        priv = this->private;

        if (marker_xattr_cleanup_cmd (dict)) {
                if (frame->root->uid != 0 || frame->root->gid != 0) {
                        op_errno = EPERM;
                        ret = -1;
                        goto err;
                }

                /* The following function does the cleanup and then unwinds the
                 * corresponding call*/
                loc_path (loc, NULL);
                marker_do_xattr_cleanup (frame, this, xdata, loc);
                return 0;
        }

        if (priv->feature_enabled == 0)
                goto wind;

        ret = call_from_sp_client_to_reset_tmfile (frame, this, dict);
        if (ret == 0)
                return 0;

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_setxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr, loc, dict, flags, xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (setxattr, frame, -1, op_errno, NULL);

        return 0;
}


int32_t
marker_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occurred while "
                        "creating symlinks ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, xdata);

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
                  int32_t flags, dict_t *xdata)
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

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = marker_inode_loc_fill (fd->inode, &local->loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_fsetxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetxattr, fd, dict, flags, xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (fsetxattr, frame, -1, ENOMEM, NULL);

        return 0;
}


int32_t
marker_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                     struct iatt *statpost, dict_t *xdata)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "%s occurred while "
                        "creating symlinks ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (fsetattr, frame, op_ret, op_errno, statpre,
                             statpost, xdata);

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
                 struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = marker_inode_loc_fill (fd->inode, &local->loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_fsetattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetattr, fd, stbuf, valid, xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (fsetattr, frame, -1, ENOMEM, NULL, NULL, NULL);

        return 0;
}


int32_t
marker_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                    struct iatt *statpost, dict_t *xdata)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        if (op_ret == -1) {
                gf_log (this->name, ((op_errno == ENOENT) ? GF_LOG_DEBUG :
                                     GF_LOG_ERROR),
                        "%s occurred during setattr of %s",
                        strerror (op_errno),
                        (local ? local->loc.path : "<nul>"));
        }

        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, statpre,
                             statpost, xdata);

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
                struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_setattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setattr, loc, stbuf, valid, xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (setattr, frame, -1, ENOMEM, NULL, NULL, NULL);

        return 0;
}


int32_t
marker_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "%s occurred while "
                        "creating symlinks ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;

        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno, xdata);

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
                    const char *name, dict_t *xdata)
{
        int32_t          ret   = 0;
        marker_local_t  *local = NULL;
        marker_conf_t   *priv  = NULL;

        priv = this->private;

        if (priv->feature_enabled == 0)
                goto wind;

        local = mem_get0 (this->local_pool);

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);

        if (ret == -1)
                goto err;
wind:
        STACK_WIND (frame, marker_removexattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr, loc, name, xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (removexattr, frame, -1, ENOMEM, NULL);

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

        /* copy the gfid from the stat structure instead of inode,
         * since if the lookup is fresh lookup, then the inode
         * would have not yet linked to the inode table which happens
         * in protocol/server.
         */
        if (uuid_is_null (local->loc.gfid))
                uuid_copy (local->loc.gfid, buf->ia_gfid);


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

        local = mem_get0 (this->local_pool);

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


int
marker_build_ancestry_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int op_ret, int op_errno, gf_dirent_t *entries,
                           dict_t *xdata)
{
        gf_dirent_t *entry = NULL;
        loc_t        loc    = {0, };
        inode_t     *parent = NULL;

        if ((op_ret <= 0) || (entries == NULL)) {
                goto out;
        }


        list_for_each_entry (entry, &entries->list, list) {
                if (entry->inode == entry->inode->table->root) {
                        loc.path = gf_strdup ("/");
                        inode_unref (parent);
                        parent = NULL;
                }

                loc.inode = inode_ref (entry->inode);

                if (parent != NULL) {
                        loc.parent = inode_ref (parent);
                        uuid_copy (loc.pargfid, parent->gfid);
                }

                uuid_copy (loc.gfid, entry->d_stat.ia_gfid);

                mq_xattr_state (this, &loc, entry->dict, entry->d_stat);

                inode_unref (parent);
                parent = inode_ref (entry->inode);
                loc_wipe (&loc);
        }

        if (parent)
                inode_unref (parent);

out:
        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, entries, xdata);
        return 0;
}

int
marker_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, gf_dirent_t *entries,
                     dict_t *xdata)
{
        gf_dirent_t    *entry = NULL;
        marker_conf_t  *priv  = NULL;
        marker_local_t *local = NULL;
        loc_t           loc   = {0, };

        if (op_ret <= 0)
                goto unwind;

        priv = this->private;
        local = frame->local;

        if (!(priv->feature_enabled & GF_QUOTA) || (local == NULL)) {
                goto unwind;
        }

        list_for_each_entry (entry, &entries->list, list) {
                if ((strcmp (entry->d_name, ".") == 0) ||
                    (strcmp (entry->d_name, "..") == 0))
                        continue;

                loc.inode = inode_ref (entry->inode);
                loc.parent = inode_ref (local->loc.inode);

                uuid_copy (loc.gfid, entry->d_stat.ia_gfid);
                uuid_copy (loc.pargfid, loc.parent->gfid);

                mq_xattr_state (this, &loc, entry->dict, entry->d_stat);

                loc_wipe (&loc);
        }

unwind:
        local = frame->local;
        frame->local = NULL;

        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, entries, xdata);
        marker_local_unref (local);

        return 0;
}

int
marker_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                 off_t offset, dict_t *dict)
{
        marker_conf_t  *priv  = NULL;
        loc_t           loc   = {0, };
        marker_local_t *local = NULL;

        priv = this->private;

        if ((dict != NULL) && dict_get (dict, GET_ANCESTRY_DENTRY_KEY)) {
                STACK_WIND (frame, marker_build_ancestry_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdirp,
                            fd, size, offset, dict);
        } else {
                if (priv->feature_enabled & GF_QUOTA) {
                        local = mem_get0 (this->local_pool);

                        MARKER_INIT_LOCAL (frame, local);

                        loc.parent = local->loc.inode = inode_ref (fd->inode);

                        if (dict == NULL)
                                dict = dict_new ();

                        mq_req_xattr (this, &loc, dict);
                }

                STACK_WIND (frame, marker_readdirp_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdirp,
                            fd, size, offset, dict);
        }

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

        GF_FREE (priv->volume_uuid);

        GF_FREE (priv->timestamp_file);

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
        int32_t         ret     = 0;
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
                                data = dict_get (options, "gsync-force-xtime");
                                if (!data)
                                        goto out;
                                ret = gf_string2boolean (data->data, &flag);
                                if (ret == 0 && flag)
                                        priv->feature_enabled |= GF_XTIME_GSYNC_FORCE;
                        }
                }
        }
out:
        return ret;
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
                        data = dict_get (options, "gsync-force-xtime");
                        if (!data)
                                goto cont;
                        ret = gf_string2boolean (data->data, &flag);
                        if (ret == 0 && flag)
                                priv->feature_enabled |= GF_XTIME_GSYNC_FORCE;
                }
        }

 cont:
        this->local_pool = mem_pool_new (marker_local_t, 128);
        if (!this->local_pool) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create local_t's memory pool");
                goto err;
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
        .getxattr    = marker_getxattr,
        .readdirp    = marker_readdirp,
	.fallocate   = marker_fallocate,
	.discard     = marker_discard,
        .zerofill    = marker_zerofill,
};

struct xlator_cbks cbks = {
        .forget = marker_forget
};

struct volume_options options[] = {
        {.key = {"volume-uuid"}},
        {.key = {"timestamp-file"}},
        {.key = {"quota"}},
        {.key = {"xtime"}},
        {.key = {"gsync-force-xtime"}},
        {.key = {NULL}}
};
