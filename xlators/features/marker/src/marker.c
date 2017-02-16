/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
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
#include "syscall.h"

#include <fnmatch.h>

#define _GF_UID_GID_CHANGED 1

static char *mq_ext_xattrs[] = {
        QUOTA_SIZE_KEY,
        QUOTA_LIMIT_KEY,
        QUOTA_LIMIT_OBJECTS_KEY,
        NULL,
};

void
fini (xlator_t *this);

int32_t
marker_start_setxattr (call_frame_t *, xlator_t *);

/* When client/quotad request for quota xattrs,
 * replace the key-name by adding the version number
 * in end of the key-name.
 * In the cbk, result value of xattrs for original
 * key-name.
 * Below function marker_key_replace_with_ver and
 * marker_key_set_ver is used for setting/removing
 * version for the key-name
 */
int
marker_key_replace_with_ver (xlator_t *this, dict_t *dict)
{
        int                ret                     = -1;
        int                i                       = 0;
        marker_conf_t     *priv                    = NULL;
        char               key[QUOTA_KEY_MAX]      = {0, };

        priv = this->private;

        if (dict == NULL || priv->version <= 0) {
                ret = 0;
                goto out;
        }

        for (i = 0; mq_ext_xattrs[i]; i++) {
                if (dict_get (dict, mq_ext_xattrs[i])) {
                        GET_QUOTA_KEY (this, key, mq_ext_xattrs[i], ret);
                        if (ret < 0)
                                goto out;

                        ret = dict_set (dict, key,
                                        dict_get (dict, mq_ext_xattrs[i]));
                        if (ret < 0)
                                goto out;

                        dict_del (dict, mq_ext_xattrs[i]);
                }
        }

        ret = 0;

out:
        return ret;
}

int
marker_key_set_ver (xlator_t *this, dict_t *dict)
{
        int              ret                     = -1;
        int              i                       = -1;
        marker_conf_t   *priv                    = NULL;
        char             key[QUOTA_KEY_MAX]      = {0, };

        priv = this->private;

        if (dict == NULL || priv->version <= 0) {
                ret = 0;
                goto out;
        }

        for (i = 0; mq_ext_xattrs[i]; i++) {
                GET_QUOTA_KEY (this, key, mq_ext_xattrs[i], ret);
                if (ret < 0)
                        goto out;

                if (dict_get (dict, key))
                        dict_set (dict, mq_ext_xattrs[i], dict_get (dict, key));
        }

        ret = 0;
out:
        return ret;
}

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
                if (gf_uuid_is_null (loc->gfid)) {
                        gf_uuid_copy (loc->gfid, loc->inode->gfid);
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
_marker_inode_loc_fill (inode_t *inode, inode_t *parent, char *name, loc_t *loc)
{
        char            *resolvedpath = NULL;
        int              ret          = -1;
        gf_boolean_t     free_parent  = _gf_false;

        if ((!inode) || (!loc))
                return ret;

        if (parent && name)
                ret = inode_path (parent, name, &resolvedpath);
        else
                ret = inode_path (inode, NULL, &resolvedpath);
        if (ret < 0)
                goto err;

        if (parent == NULL) {
	        parent = inode_parent (inode, NULL, NULL);
                free_parent = _gf_true;
        }

        ret = marker_loc_fill (loc, inode, parent, resolvedpath);
        if (ret < 0)
                goto err;

err:
	if (free_parent)
		inode_unref (parent);

        GF_FREE (resolvedpath);

        return ret;
}

int
marker_inode_loc_fill (inode_t *inode, loc_t *loc)
{
        return _marker_inode_loc_fill (inode, NULL, NULL, loc);
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
        sys_unlink (priv->timestamp_file);

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

        if (local->lk_frame) {
                STACK_DESTROY (local->lk_frame->root);
                local->lk_frame = NULL;
        }

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

        if (sys_stat (priv->timestamp_file, &buf) != -1) {
                vol_mark->retval = 0;
                vol_mark->sec = htonl (buf.st_mtime);
                vol_mark->usec = htonl (ST_MTIM_NSEC (&buf)/1000);
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
        if (ret) {
                GF_FREE (vol_mark);
                gf_log (this->name, GF_LOG_WARNING, "failed to set key %s",
                        name);
        }

        STACK_UNWIND_STRICT (getxattr, frame, 0, 0, dict, xdata);

        if (dict)
                dict_unref (dict);
out:
        return 0;
}

gf_boolean_t
call_from_special_client (call_frame_t *frame, xlator_t *this, const char *name)
{
        struct volume_mark     *vol_mark   = NULL;
        marker_conf_t          *priv       = NULL;
        gf_boolean_t           is_true     = _gf_true;

        priv = (marker_conf_t *)this->private;

        if (frame->root->pid != GF_CLIENT_PID_GSYNCD || name == NULL ||
            strcmp (name, MARKER_XATTR_PREFIX "." VOLUME_MARK) != 0) {
                is_true = _gf_false;
                goto out;
        }

        stat_stampfile (this, priv, &vol_mark);

        marker_getxattr_stampfile_cbk (frame, this, name, vol_mark, NULL);
out:
        return is_true;
}

static gf_boolean_t
_is_quota_internal_xattr (dict_t *d, char *k, data_t *v, void *data)
{
        int     i = 0;
        char    **external_xattrs = data;

        for (i = 0; external_xattrs && external_xattrs[i]; i++) {
                if (strcmp (k, external_xattrs[i]) == 0)
                        return _gf_false;
        }

        if (fnmatch ("trusted.glusterfs.quota*", k, 0) == 0)
                return _gf_true;

        /* It would be nice if posix filters pgfid xattrs. But since marker
         * also takes up responsibility to clean these up, adding the filtering
         * here (Check 'quota_xattr_cleaner')
         */
        if (fnmatch (PGFID_XATTR_KEY_PREFIX"*", k, 0) == 0)
                return _gf_true;

        return _gf_false;
}

static void
marker_filter_internal_xattrs (xlator_t *this, dict_t *xattrs)
{
        marker_conf_t *priv   = NULL;
        char         **ext    = NULL;

        priv = this->private;
        if (priv->feature_enabled & GF_QUOTA)
                ext = mq_ext_xattrs;

        dict_foreach_match (xattrs, _is_quota_internal_xattr, ext,
                            dict_remove_foreach_fn, NULL);
}

static void
marker_filter_gsyncd_xattrs (call_frame_t *frame,
                               xlator_t *this, dict_t *xattrs)
{
        marker_conf_t *priv   = NULL;

        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (frame);

        if (xattrs && frame->root->pid != GF_CLIENT_PID_GSYNCD) {
                GF_REMOVE_INTERNAL_XATTR (GF_XATTR_XTIME_PATTERN, xattrs);
        }
        return;
}

int32_t
marker_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *dict,
                     dict_t *xdata)
{
        int32_t     ret  = -1;
        if (op_ret < 0)
                goto unwind;

        ret = marker_key_set_ver (this, dict);
        if (ret < 0) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

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
                /*
                 * Except limit-set xattr, rest of the xattrs are maintained
                 * by quota xlator. Don't expose them to other xlators.
                 * This filter makes sure quota xattrs are not healed as part of
                 * metadata self-heal
                 */
                marker_filter_internal_xattrs (frame->this, dict);
        }

        /* Filter gsyncd xtime xattr for non gsyncd clients */
        marker_filter_gsyncd_xattrs (frame, frame->this, dict);

unwind:
        MARKER_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict, xdata);
        return 0;
}

int32_t
marker_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                 const char *name, dict_t *xdata)
{
        gf_boolean_t     is_true  = _gf_false;
        marker_conf_t   *priv                       = NULL;
        unsigned long    cookie                     = 0;
        marker_local_t  *local                      = NULL;
        char             key[QUOTA_KEY_MAX]         = {0, };
        int32_t          ret                        = -1;
        int32_t          i                          = 0;

        priv = this->private;

        if (name) {
                for (i = 0; mq_ext_xattrs[i]; i++) {
                        if (strcmp (name, mq_ext_xattrs[i]))
                                continue;

                        GET_QUOTA_KEY (this, key, mq_ext_xattrs[i], ret);
                        if (ret < 0)
                                goto out;
                        name = key;
                        break;
                }
        }

        frame->local = mem_get0 (this->local_pool);
        local = frame->local;
        if (local == NULL)
                goto out;

        MARKER_INIT_LOCAL (frame, local);

        if ((loc_copy (&local->loc, loc)) < 0)
		goto out;

        gf_log (this->name, GF_LOG_DEBUG, "USER:PID = %d", frame->root->pid);

        if (priv && priv->feature_enabled & GF_XTIME)
                is_true = call_from_special_client (frame, this, name);

        if (is_true == _gf_false) {
                if (name == NULL) {
                        /* Signifies that marker translator
                         * has to filter the quota's xattr's,
                         * this is to prevent afr from performing
                         * self healing on marker-quota xattrs'
                         */
                        cookie = 1;
                }
                STACK_WIND_COOKIE (frame, marker_getxattr_cbk,
				   (void *)cookie,
                                   FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->getxattr,
				   loc, name, xdata);
        }

        return 0;
out:
        MARKER_STACK_UNWIND (getxattr, frame, -1, ENOMEM, NULL, NULL);
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

        ret = (local) ? marker_trav_parent (local) : -1;

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

        if (local->loc.inode && gf_uuid_is_null (local->loc.gfid))
                gf_uuid_copy (local->loc.gfid, local->loc.inode->gfid);

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

        if (!frame)
                return -1;

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
        quota_inode_ctx_t  *ctx     = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "error occurred "
                        "while creating directory %s", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;
        priv = this->private;

        if (op_ret >= 0 && inode && (priv->feature_enabled & GF_QUOTA)) {
                ctx = mq_inode_ctx_new (inode, this);
                if (ctx == NULL) {
                        gf_log (this->name, GF_LOG_WARNING, "mq_inode_ctx_new "
                                "failed for %s", uuid_utoa (inode->gfid));
                        op_ret = -1;
                        op_errno = ENOMEM;
                }
        }

        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno, inode,
                             buf, preparent, postparent, xdata);

        if (op_ret == -1 || local == NULL)
                goto out;

        if (gf_uuid_is_null (local->loc.gfid))
                gf_uuid_copy (local->loc.gfid, buf->ia_gfid);

        if (priv->feature_enabled & GF_QUOTA)
                mq_create_xattrs_txn (this, &local->loc, NULL);

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
        MARKER_STACK_UNWIND (mkdir, frame, -1, ENOMEM, NULL,
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
        quota_inode_ctx_t  *ctx     = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "error occurred "
                        "while creating file %s", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;
        priv = this->private;

        if (op_ret >= 0 && inode && (priv->feature_enabled & GF_QUOTA)) {
                ctx = mq_inode_ctx_new (inode, this);
                if (ctx == NULL) {
                        gf_log (this->name, GF_LOG_WARNING, "mq_inode_ctx_new "
                                "failed for %s", uuid_utoa (inode->gfid));
                        op_ret = -1;
                        op_errno = ENOMEM;
                }
        }

        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent, xdata);

        if (op_ret == -1 || local == NULL)
                goto out;

        if (gf_uuid_is_null (local->loc.gfid))
                gf_uuid_copy (local->loc.gfid, buf->ia_gfid);

        if (priv->feature_enabled & GF_QUOTA)
                mq_create_xattrs_txn (this, &local->loc, buf);

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
        MARKER_STACK_UNWIND (create, frame, -1, ENOMEM, NULL, NULL, NULL, NULL,
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
                mq_initiate_quota_txn (this, &local->loc, postbuf);

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
        MARKER_STACK_UNWIND (writev, frame, -1, ENOMEM, NULL, NULL, NULL);

        return 0;
}


int32_t
marker_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        marker_conf_t      *priv    = NULL;
        marker_local_t     *local   = NULL;
        call_stub_t        *stub    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "error occurred "
                        "rmdir %s", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;
        priv = this->private;

        if (op_ret == -1 || local == NULL)
                goto out;

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);

        if (priv->feature_enabled & GF_QUOTA) {
                /* If a 'rm -rf' is performed by a client, rmdir can be faster
                   than marker background mq_reduce_parent_size_txn.
                   In this case, as part of rmdir parent child association
                   will be removed in the server protocol.
                   This can lead to mq_reduce_parent_size_txn failures.

                   So perform mq_reduce_parent_size_txn in foreground
                   and unwind to server once txn is complete
                 */

                stub = fop_rmdir_cbk_stub (frame, default_rmdir_cbk, op_ret,
                                           op_errno, preparent, postparent,
                                           xdata);
                mq_reduce_parent_size_txn (this, &local->loc, NULL, 1, stub);

                if (stub) {
                        marker_local_unref (local);
                        return 0;
                }
        }

out:
        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno, preparent,
                             postparent, xdata);

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
        MARKER_STACK_UNWIND (rmdir, frame, -1, ENOMEM, NULL, NULL, NULL);

        return 0;
}


int32_t
marker_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        marker_conf_t      *priv    = NULL;
        marker_local_t     *local   = NULL;
        uint32_t            nlink   = -1;
        GF_UNUSED int32_t   ret     = 0;
        call_stub_t        *stub    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE,
                        "%s occurred in unlink", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;
        priv = this->private;

        if (op_ret == -1 || local == NULL)
                goto out;

        if (priv->feature_enabled & GF_XTIME)
                marker_xtime_update_marks (this, local);

        if (priv->feature_enabled & GF_QUOTA) {
                if (local->skip_txn)
                        goto out;

                if (xdata) {
                        ret = dict_get_uint32 (xdata,
                                GF_RESPONSE_LINK_COUNT_XDATA, &nlink);
                        if (ret) {
                                gf_log (this->name, GF_LOG_TRACE,
                                        "dict get failed %s ",
                                        strerror (-ret));
                        }
                }

                /* If a 'rm -rf' is performed by a client, unlink can be faster
                   than marker background mq_reduce_parent_size_txn.
                   In this case, as part of unlink parent child association
                   will be removed in the server protocol.
                   This can lead to mq_reduce_parent_size_txn failures.

                  So perform mq_reduce_parent_size_txn in foreground
                  and unwind to server once txn is complete
                */

                stub = fop_unlink_cbk_stub (frame, default_unlink_cbk, op_ret,
                                            op_errno, preparent, postparent,
                                            xdata);
                mq_reduce_parent_size_txn (this, &local->loc, NULL, nlink,
                                           stub);

                if (stub) {
                        marker_local_unref (local);
                        return 0;
                }
        }

out:
        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno, preparent,
                             postparent, xdata);

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
        gf_boolean_t     dict_free = _gf_false;

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

        if (xdata == NULL) {
                xdata = dict_new ();
                dict_free = _gf_true;
        }

        ret = dict_set_int32 (xdata, GF_REQUEST_LINK_COUNT_XDATA, 1);
        if (ret < 0)
                goto err;

unlink_wind:
        STACK_WIND (frame, marker_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink, loc, xflag, xdata);
        goto out;

err:
        MARKER_STACK_UNWIND (unlink, frame, -1, ENOMEM, NULL, NULL, NULL);

out:
        if (dict_free)
                dict_unref (xdata);
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
                        mq_create_xattrs_txn (this, &local->loc, buf);
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
        MARKER_STACK_UNWIND (link, frame, -1, ENOMEM, NULL, NULL, NULL, NULL,
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
                gf_log (this->name, GF_LOG_WARNING,
                        "inodelk (UNLOCK) failed on path:%s (gfid:%s) (%s)",
                        oplocal->parent_loc.path,
                        uuid_utoa (oplocal->parent_loc.inode->gfid),
                        strerror (op_errno));
        }

        if (local->err != 0)
                goto err;

        mq_reduce_parent_size_txn (this, &oplocal->loc, &oplocal->contribution,
                                   -1, NULL);

        if (local->loc.inode != NULL) {
                /* If destination file exits before rename, it would have
                 * been unlinked while renaming a file
                 */
                mq_reduce_parent_size_txn (this, &local->loc, NULL,
                                           local->ia_nlink, NULL);
        }

        newloc.inode = inode_ref (oplocal->loc.inode);
        newloc.path = gf_strdup (local->loc.path);
        newloc.name = strrchr (newloc.path, '/');
        if (newloc.name)
                newloc.name++;
        newloc.parent = inode_ref (local->loc.parent);

        mq_create_xattrs_txn (this, &newloc, &local->buf);

        loc_wipe (&newloc);

        if (priv->feature_enabled & GF_XTIME) {
                if (!local->loc.inode)
                        local->loc.inode = inode_ref (oplocal->loc.inode);
                //update marks on oldpath
                gf_uuid_copy (local->loc.gfid, oplocal->loc.inode->gfid);
                marker_xtime_update_marks (this, oplocal);
                marker_xtime_update_marks (this, local);
        }

err:
        marker_local_unref (local);
        marker_local_unref (oplocal);

        return 0;
}


void
marker_rename_release_oldp_lock (marker_local_t *local, xlator_t *this)
{
        marker_local_t        *oplocal  = NULL;
        call_frame_t          *lk_frame = NULL;
        struct gf_flock        lock     = {0, };

        oplocal = local->oplocal;
        lk_frame = local->lk_frame;

        if (lk_frame == NULL)
                goto err;

        lock.l_type   = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start  = 0;
        lock.l_len    = 0;
        lock.l_pid    = 0;

        STACK_WIND (lk_frame,
                    marker_rename_done,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &oplocal->parent_loc, F_SETLKW, &lock, NULL);

        return;

err:
        marker_local_unref (local);
        marker_local_unref (oplocal);
}


int32_t
marker_rename_unwind (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        marker_local_t       *local    = NULL;
        marker_local_t       *oplocal  = NULL;
        quota_inode_ctx_t    *ctx      = NULL;
        inode_contribution_t *contri   = NULL;

        local = frame->local;
        oplocal = local->oplocal;
        frame->local = NULL;

        //Reset frame uid and gid if set.
        if (cookie == (void *) _GF_UID_GID_CHANGED)
                MARKER_RESET_UID_GID (frame, frame->root, local);

        if (op_ret < 0)
                local->err =  op_errno ? op_errno : EINVAL;

        if (local->stub != NULL) {
                /* Remove contribution node from in-memory even if
                 * remove-xattr has failed as the rename is already performed
                 * if local->stub is set, which means rename was sucessful
                 */
                mq_inode_ctx_get (oplocal->loc.inode, this, &ctx);
                if (ctx) {
                        contri = mq_get_contribution_node (oplocal->loc.parent,
                                                           ctx);
                        if (contri) {
                                QUOTA_FREE_CONTRIBUTION_NODE (ctx, contri);
                                GF_REF_PUT (contri);
                        }
                }

                call_resume (local->stub);
                local->stub = NULL;
                local->err = 0;
        } else if (local->err != 0) {
                STACK_UNWIND_STRICT (rename, frame, -1, local->err, NULL, NULL,
                                     NULL, NULL, NULL, NULL);
        } else {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "continuation stub to unwind the call is absent, hence "
                        "call will be hung (call-stack id = %"PRIu64")",
                        frame->root->unique);
        }

        /* If there are in-progress writes on old-path when during rename
         * operation, update txn will update the wrong path if lock
         * is released before rename unwind.
         * So release lock only after rename unwind
         */
        marker_rename_release_oldp_lock (local, this);

        return 0;
}


int32_t
marker_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf,
                   struct iatt *preoldparent, struct iatt *postoldparent,
                   struct iatt *prenewparent, struct iatt *postnewparent,
                   dict_t *xdata)
{
        marker_conf_t  *priv                            = NULL;
        marker_local_t *local                           = NULL;
        marker_local_t *oplocal                         = NULL;
        call_stub_t    *stub                            = NULL;
        int32_t         ret                             = 0;
        char            contri_key[QUOTA_KEY_MAX]       = {0, };
        loc_t           newloc                          = {0, };

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

                local->ia_nlink = 0;
                if (xdata)
                        ret = dict_get_uint32 (xdata,
                                               GF_RESPONSE_LINK_COUNT_XDATA,
                                               &local->ia_nlink);

                local->buf = *buf;
                stub = fop_rename_cbk_stub (frame, default_rename_cbk, op_ret,
                                            op_errno, buf, preoldparent,
                                            postoldparent, prenewparent,
                                            postnewparent, xdata);
                if (stub == NULL) {
                        local->err = ENOMEM;
                        goto quota_err;
                }

                local->stub = stub;

                GET_CONTRI_KEY (this, contri_key, oplocal->loc.parent->gfid,
                                ret);
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
                gf_uuid_copy (newloc.gfid, oplocal->loc.inode->gfid);

                STACK_WIND_COOKIE (frame, marker_rename_unwind,
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
                        if (!local->loc.inode)
                                local->loc.inode = inode_ref (oplocal->loc.inode);
                        gf_uuid_copy (local->loc.gfid, oplocal->loc.inode->gfid);
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
        marker_rename_unwind (frame, NULL, this, 0, 0, NULL);
        return 0;
}


int32_t
marker_do_rename (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        marker_local_t       *local                      = NULL;
        marker_local_t       *oplocal                    = NULL;
        char                  contri_key[QUOTA_KEY_MAX]  = {0, };
        int32_t               ret                        = 0;
        quota_meta_t          contribution               = {0, };

        local = frame->local;
        oplocal = local->oplocal;

        //Reset frame uid and gid if set.
        if (cookie == (void *) _GF_UID_GID_CHANGED)
                MARKER_RESET_UID_GID (frame, frame->root, local);

        if ((op_ret < 0) && (op_errno != ENOATTR) && (op_errno != ENODATA)) {
                local->err = op_errno ? op_errno : EINVAL;
                gf_log (this->name, GF_LOG_WARNING,
                        "fetching contribution values from %s (gfid:%s) "
                        "failed (%s)", oplocal->loc.path,
                        uuid_utoa (oplocal->loc.inode->gfid),
                        strerror (op_errno));
                goto err;
        }

        GET_CONTRI_KEY (this, contri_key, oplocal->loc.parent->gfid, ret);
        if (ret < 0) {
                local->err = errno ? errno : ENOMEM;
                goto err;
        }
        quota_dict_get_meta (dict, contri_key, &contribution);
        oplocal->contribution = contribution;

        STACK_WIND (frame, marker_rename_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename, &oplocal->loc,
                    &local->loc, local->xdata);

        return 0;

err:
        marker_rename_unwind (frame, NULL, this, 0, 0, NULL);
        return 0;
}

int32_t
marker_get_oldpath_contribution (call_frame_t *lk_frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno, dict_t *xdata)
{
        call_frame_t    *frame                      = NULL;
        marker_local_t  *local                      = NULL;
        marker_local_t  *oplocal                    = NULL;
        char             contri_key[QUOTA_KEY_MAX]  = {0, };
        int32_t          ret                        = 0;

        local = lk_frame->local;
        oplocal = local->oplocal;
        frame = local->frame;

        if (op_ret < 0) {
                local->err = op_errno ? op_errno : EINVAL;
                gf_log (this->name, GF_LOG_WARNING,
                        "cannot hold inodelk on %s (gfid:%s) (%s)",
                        oplocal->loc.path, uuid_utoa (oplocal->loc.inode->gfid),
                        strerror (op_errno));
                goto err;

                STACK_DESTROY (local->lk_frame->root);
                local->lk_frame = NULL;
        }

        GET_CONTRI_KEY (this, contri_key, oplocal->loc.parent->gfid, ret);
        if (ret < 0) {
                local->err = errno ? errno : ENOMEM;
                goto err;
        }

        /* getxattr requires uid and gid to be 0,
         * reset them in the callback.
         */
        MARKER_SET_UID_GID (frame, local, frame->root);

        if (gf_uuid_is_null (oplocal->loc.gfid))
                        gf_uuid_copy (oplocal->loc.gfid,
                                   oplocal->loc.inode->gfid);

        GF_UUID_ASSERT (oplocal->loc.gfid);

        STACK_WIND_COOKIE (frame, marker_do_rename,
                           frame->cookie, FIRST_CHILD(this),
                           FIRST_CHILD(this)->fops->getxattr,
                           &oplocal->loc, contri_key, NULL);

        return 0;
err:
        marker_rename_unwind (frame, NULL, this, 0, 0, NULL);
        return 0;
}


/* For a marker_rename FOP, following is the algorithm used for Quota
 * accounting. The use-case considered is:
 * 1. rename (src, dst)
 * 2. both src and dst exist
 * 3. there are parallel operations on src and dst (lets say through fds
 *    opened on them before rename was initiated).
 *
 * PS: We've not thought through whether this algo works in the presence of
 *     hardlinks to src and/or dst.
 *
 * Algorithm:
 * ==========
 *
 * 1) set inodelk on src-parent
 *    As part of rename operation, parent can change for the file.
 *    We need to remove contribution (both on disk xattr and in-memory one)
 *    to src-parent (and its ancestors) and add the contribution to dst-parent
 *    (and its ancestors). While we are doing these operations, contribution of
 *    the file/directory shouldn't be changing as we want to be sure that
 *      a) what we subtract from src-parent is exactly what we add to dst-parent
 *      b) we should subtract from src-parent exactly what we contributed to
 *         src-parent
 *    So, We hold a lock on src-parent to block any parallel transcations on
 *    src-inode (since thats the one which survives rename).
 *
 *    If there are any parallel transactions on dst-inode they keep succeeding
 *    till the association of dst-inode with dst-parent is broken because of an
 *    inode_rename after unwind of rename fop from marker. Only after unwind
 *    (and hence inode_rename), we delete and subtract the contribution of
 *    dst-inode to dst-parent. That way we are making sure we subtract exactly
 *    what dst-inode contributed to dst-parent.
 *
 * 2) lookup contribution to src-parent on src-inode.
 *    We need to save the contribution info for use at step-8.
 *
 * 3) wind rename
 *    Perform rename on disk
 *
 * 4) remove xattr on src-loc
 *    After rename, parent can change, so
 *    need to remove xattrs storing contribution to src-parent.
 *
 * 5) remove contribution node corresponding to src-parent from the in-memory
 *    list.
 *    After rename, contri gfid can change and we have
 *    also removed xattr from file.
 *    We need to remove in-memory contribution node to prevent updations to
 *    src-parent even after a successful rename
 *
 * 6) unwind rename
 *    This will ensure that rename is done in the server
 *    inode table. An inode_rename disassociates src-inode from src-parent and
 *    associates it with dst-parent. It also disassociates dst-inode from
 *    dst-parent. After inode_rename, inode_parent on src-inode will give
 *    dst-parent and inode_parent on dst-inode will return NULL (assuming
 *    dst-inode doesn't have any hardlinks).
 *
 * 7) release inodelk on src-parent
 *    Lock on src-parent should be released only after
 *    rename on disk, remove xattr and rename_unwind (and hence inode_rename)
 *    operations. If lock is released before inode_rename, a parallel
 *    transaction on src-inode can still update src-parent (as inode_parent on
 *    src-inode can still return src-parent). This would make the
 *    contribution from src-inode to src-parent stored in step-2 stale.
 *
 * 8) Initiate mq_reduce_parent_size_txn on src-parent to remove contribution
 *    of src-inode to src-parent. We use the contribution stored in step-2.
 *    Since, we had acquired the lock on src-parent all along step-2 through
 *    inode_rename, we can be sure that a parallel transaction wouldn't have
 *    added a delta to src-parent.
 *
 * 9) Initiate mq_reduce_parent_size_txn on dst-parent if dst-inode exists.
 *    The size reduced from dst-parent and its ancestors is the
 *    size stored as contribution to dst-parent in dst-inode.
 *    If the destination file had existed, rename will unlink the
 *    destination file as part of its operation.
 *    We need to reduce the size on the dest parent similarly to
 *    unlink. Since, we are initiating reduce-parent-size transaction after
 *    inode_rename, we can be sure that a parallel transaction wouldn't add
 *    delta to dst-parent while we are reducing the contribution of dst-inode
 *    from its ancestors before rename.
 *
 * 10) create contribution xattr to dst-parent on src-inode.
 */
int32_t
marker_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
               loc_t *newloc, dict_t *xdata)
{
        int32_t         ret              = 0;
        marker_local_t *local            = NULL;
        marker_local_t *oplocal          = NULL;
        marker_conf_t  *priv             = NULL;
        struct gf_flock lock             = {0, };

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

        lock.l_len    = 0;
        lock.l_start  = 0;
        lock.l_type   = F_WRLCK;
        lock.l_whence = SEEK_SET;

        local->xdata = xdata ? dict_ref (xdata) : dict_new ();
        ret = dict_set_int32 (local->xdata, GF_REQUEST_LINK_COUNT_XDATA, 1);
        if (ret < 0)
                goto err;

        local->frame = frame;
        local->lk_frame = create_frame (this, this->ctx->pool);
        if (local->lk_frame == NULL)
                goto err;

        local->lk_frame->root->uid = 0;
        local->lk_frame->root->gid = 0;
        local->lk_frame->local = local;
        set_lk_owner_from_ptr (&local->lk_frame->root->lk_owner,
                               local->lk_frame->root);

        STACK_WIND (local->lk_frame,
                    marker_get_oldpath_contribution,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->inodelk,
                    this->name, &oplocal->parent_loc,
                    F_SETLKW, &lock, NULL);

        return 0;

rename_wind:
        STACK_WIND (frame, marker_rename_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename, oldloc, newloc, xdata);

        return 0;
err:
        MARKER_STACK_UNWIND (rename, frame, -1, ENOMEM, NULL,
                             NULL, NULL, NULL, NULL, NULL);
        marker_local_unref (oplocal);

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

        if (priv->feature_enabled & GF_QUOTA) {
                /* DHT Rebalance process, at the end of migration will
                 * first make the src file as a linkto file and then
                 * truncate the file. By doing a truncate after making the
                 * src file as linkto file, the contri which is already
                 * accounted is left over.
                 * So, we need to account for the linkto file when a truncate
                 * happens, thereby updating the contri properly.
                 * By passing NULL for postbuf, mq_prevalidate does not check
                 * for linkto file.
                 * Same happens with ftruncate as well.
                 */
                if (postbuf && IS_DHT_LINKFILE_MODE (postbuf))
                        mq_initiate_quota_txn (this, &local->loc, NULL);
                else
                        mq_initiate_quota_txn (this, &local->loc, postbuf);
        }

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
        MARKER_STACK_UNWIND (truncate, frame, -1, ENOMEM, NULL, NULL, NULL);

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

        if (priv->feature_enabled & GF_QUOTA) {
                if (postbuf && IS_DHT_LINKFILE_MODE (postbuf))
                        mq_initiate_quota_txn (this, &local->loc, NULL);
                else
                        mq_initiate_quota_txn (this, &local->loc, postbuf);
        }

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
        MARKER_STACK_UNWIND (ftruncate, frame, -1, ENOMEM, NULL, NULL, NULL);

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
        quota_inode_ctx_t  *ctx     = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occurred while "
                        "creating symlinks ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;
        priv = this->private;

        if (op_ret >= 0 && inode && (priv->feature_enabled & GF_QUOTA)) {
                ctx = mq_inode_ctx_new (inode, this);
                if (ctx == NULL) {
                        gf_log (this->name, GF_LOG_WARNING, "mq_inode_ctx_new "
                                "failed for %s", uuid_utoa (inode->gfid));
                        op_ret = -1;
                        op_errno = ENOMEM;
                }
        }

        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);

        if (op_ret == -1 || local == NULL)
                goto out;

        if (gf_uuid_is_null (local->loc.gfid))
                gf_uuid_copy (local->loc.gfid, buf->ia_gfid);

        if (priv->feature_enabled & GF_QUOTA) {
                mq_create_xattrs_txn (this, &local->loc, buf);
        }

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
        MARKER_STACK_UNWIND (symlink, frame, -1, ENOMEM, NULL,
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
        quota_inode_ctx_t  *ctx     = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occurred with "
                        "mknod ", strerror (op_errno));
        }

        local = (marker_local_t *) frame->local;

        frame->local = NULL;
        priv = this->private;

        if (op_ret >= 0 && inode && (priv->feature_enabled & GF_QUOTA)) {
                ctx = mq_inode_ctx_new (inode, this);
                if (ctx == NULL) {
                        gf_log (this->name, GF_LOG_WARNING, "mq_inode_ctx_new "
                                "failed for %s", uuid_utoa (inode->gfid));
                        op_ret = -1;
                        op_errno = ENOMEM;
                }
        }

        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno, inode,
                             buf, preparent, postparent, xdata);

        if (op_ret == -1 ||  local == NULL)
                goto out;

        if (gf_uuid_is_null (local->loc.gfid))
                gf_uuid_copy (local->loc.gfid, buf->ia_gfid);

        if ((priv->feature_enabled & GF_QUOTA) && (S_ISREG (local->mode))) {
                mq_create_xattrs_txn (this, &local->loc, buf);
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
        MARKER_STACK_UNWIND (mknod, frame, -1, ENOMEM, NULL,
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
                mq_initiate_quota_txn (this, &local->loc, postbuf);

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
        MARKER_STACK_UNWIND (fallocate, frame, -1, ENOMEM, NULL, NULL, NULL);

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
                mq_initiate_quota_txn (this, &local->loc, postbuf);

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
        MARKER_STACK_UNWIND (discard, frame, -1, ENOMEM, NULL, NULL, NULL);

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
                mq_initiate_quota_txn (this, &local->loc, postbuf);

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
        MARKER_STACK_UNWIND (zerofill, frame, -1, ENOMEM, NULL, NULL, NULL);

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
                        sys_close (fd);
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
        call_frame_t    *frame              = data;
        marker_local_t  *local              = frame->local;
        xlator_t        *this               = frame->this;
        marker_conf_t   *priv               = NULL;
        char             ver_str[NAME_MAX]  = {0,};
        char            *dot                = NULL;
        int              ret                = -1;

        priv = this->private;

        /* If quota is enabled immediately after disable.
         * quota healing starts creating new xattrs
         * before completing the cleanup operation.
         * So we should check if the xattr is the new.
         * Do not remove xattr if its xattr
         * version is same as current version
         */
        if ((priv->feature_enabled & GF_QUOTA) && priv->version > 0) {
                snprintf (ver_str, sizeof (ver_str), ".%d", priv->version);
                dot = strrchr (k, '.');
                if (dot && !strcmp(dot, ver_str))
                        return 0;
        }

	ret = syncop_removexattr (FIRST_CHILD (this), &local->loc, k, 0, NULL);
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

        op_ret   = (ret < 0)? -1: 0;
        op_errno = -ret;

        MARKER_STACK_UNWIND (setxattr, frame, op_ret, op_errno, xdata);
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

        ret = syncop_listxattr (FIRST_CHILD(this), &local->loc, &xdata, NULL,
                                NULL);
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
        if (ret)
                MARKER_STACK_UNWIND (setxattr, frame, -1, ENOMEM, xdata);

        return ret;
}

static gf_boolean_t
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

        ret = marker_key_replace_with_ver (this, dict);
        if (ret < 0)
                goto err;

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
        MARKER_STACK_UNWIND (setxattr, frame, -1, op_errno, NULL);

        return 0;
}


int32_t
marker_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "%s occurred in "
                        "fsetxattr", strerror (op_errno));
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
        MARKER_STACK_UNWIND (fsetxattr, frame, -1, ENOMEM, NULL);

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
                gf_log (this->name, GF_LOG_TRACE, "%s occurred in "
                        "fsetattr ", strerror (op_errno));
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
        MARKER_STACK_UNWIND (fsetattr, frame, -1, ENOMEM, NULL, NULL, NULL);

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
                gf_log (this->name, GF_LOG_TRACE,
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
        MARKER_STACK_UNWIND (setattr, frame, -1, ENOMEM, NULL, NULL, NULL);

        return 0;
}


int32_t
marker_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        marker_local_t     *local   = NULL;
        marker_conf_t      *priv    = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE,
                                "%s occurred while "
                                "removing extended attribute",
                                     strerror (op_errno));
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
        int32_t          ret                  = -1;
        int32_t          i                    = 0;
        marker_local_t  *local                = NULL;
        marker_conf_t   *priv                 = NULL;
        char             key[QUOTA_KEY_MAX]   = {0, };

        priv = this->private;

        if (name) {
                for (i = 0; mq_ext_xattrs[i]; i++) {
                        if (strcmp (name, mq_ext_xattrs[i]))
                                continue;

                        GET_QUOTA_KEY (this, key, mq_ext_xattrs[i], ret);
                        if (ret < 0)
                                goto err;
                        name = key;
                        break;
                }
        }

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
        MARKER_STACK_UNWIND (removexattr, frame, -1, ENOMEM, NULL);

        return 0;
}

static gf_boolean_t
__has_quota_xattrs (dict_t *xattrs)
{
        if (dict_foreach_match (xattrs, _is_quota_internal_xattr, NULL,
                                dict_null_foreach_fn, NULL) > 0)
                return _gf_true;

        return _gf_false;
}

int32_t
marker_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, dict_t *dict, struct iatt *postparent)
{
        marker_conf_t      *priv                       = NULL;
        marker_local_t     *local                      = NULL;
        dict_t             *xattrs                     = NULL;
        quota_inode_ctx_t  *ctx                        = NULL;
        int32_t             ret                        = -1;

        priv = this->private;
        local = (marker_local_t *) frame->local;
        frame->local = NULL;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_TRACE, "lookup failed with %s",
                        strerror (op_errno));
                goto unwind;
        }

        ret = marker_key_set_ver (this, dict);
        if (ret < 0) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        if (dict && __has_quota_xattrs (dict)) {
                xattrs = dict_copy_with_ref (dict, NULL);
                if (!xattrs) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                } else {
                        marker_filter_internal_xattrs (this, xattrs);
                }
        } else if (dict) {
                xattrs = dict_ref (dict);
        }

        if (op_ret >= 0 && inode && (priv->feature_enabled & GF_QUOTA)) {
                ctx = mq_inode_ctx_new (inode, this);
                if (ctx == NULL) {
                        gf_log (this->name, GF_LOG_WARNING, "mq_inode_ctx_new "
                                "failed for %s", uuid_utoa (inode->gfid));
                        op_ret = -1;
                        op_errno = ENOMEM;
                }
        }

unwind:
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf,
                             xattrs, postparent);

        if (op_ret == -1 || local == NULL)
                goto out;

        /* copy the gfid from the stat structure instead of inode,
         * since if the lookup is fresh lookup, then the inode
         * would have not yet linked to the inode table which happens
         * in protocol/server.
         */
        if (gf_uuid_is_null (local->loc.gfid))
                gf_uuid_copy (local->loc.gfid, buf->ia_gfid);


        if (priv->feature_enabled & GF_QUOTA) {
                mq_xattr_state (this, &local->loc, dict, *buf);
        }

out:
        marker_local_unref (local);
        if (xattrs)
                dict_unref (xattrs);

        return 0;
}

int32_t
marker_lookup (call_frame_t *frame, xlator_t *this,
               loc_t *loc, dict_t *xattr_req)
{
        int32_t         ret                     = 0;
        marker_local_t *local                   = NULL;
        marker_conf_t  *priv                    = NULL;

        priv = this->private;

        xattr_req = xattr_req ? dict_ref (xattr_req) : dict_new ();
        if (!xattr_req)
                goto err;

        ret = marker_key_replace_with_ver (this, xattr_req);
        if (ret < 0)
                goto err;

        if (priv->feature_enabled == 0)
                goto wind;

        local = mem_get0 (this->local_pool);
        if (local == NULL)
                goto err;

        MARKER_INIT_LOCAL (frame, local);

        ret = loc_copy (&local->loc, loc);
        if (ret == -1)
                goto err;

        if ((priv->feature_enabled & GF_QUOTA))
                mq_req_xattr (this, loc, xattr_req, NULL, NULL);

wind:
        STACK_WIND (frame, marker_lookup_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, loc, xattr_req);

        dict_unref (xattr_req);

        return 0;
err:
        MARKER_STACK_UNWIND (lookup, frame, -1, ENOMEM, NULL, NULL, NULL, NULL);

        if (xattr_req)
                dict_unref (xattr_req);

        return 0;
}


int
marker_build_ancestry_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int op_ret, int op_errno, gf_dirent_t *entries,
                           dict_t *xdata)
{
        gf_dirent_t        *entry  = NULL;
        quota_inode_ctx_t  *ctx    = NULL;
        int                 ret    = -1;

        if ((op_ret <= 0) || (entries == NULL)) {
                goto out;
        }

        list_for_each_entry (entry, &entries->list, list) {
                if (entry->inode == NULL)
                        continue;

                ret = marker_key_set_ver (this, entry->dict);
                if (ret < 0) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        break;
                }

                ctx = mq_inode_ctx_new (entry->inode, this);
                if (ctx == NULL)
                        gf_log (this->name, GF_LOG_WARNING, "mq_inode_ctx_new "
                                "failed for %s",
                                uuid_utoa (entry->inode->gfid));
        }

out:
        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, entries, xdata);
        return 0;
}

int
marker_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, gf_dirent_t *entries,
                     dict_t *xdata)
{
        gf_dirent_t        *entry         = NULL;
        marker_conf_t      *priv          = NULL;
        marker_local_t     *local         = NULL;
        loc_t               loc           = {0, };
        int                 ret           = -1;
        char               *resolvedpath  = NULL;
        quota_inode_ctx_t  *ctx           = NULL;

        if (op_ret <= 0)
                goto unwind;

        priv = this->private;
        local = frame->local;

        if (!(priv->feature_enabled & GF_QUOTA) || (local == NULL)) {
                goto unwind;
        }

        list_for_each_entry (entry, &entries->list, list) {
                if ((strcmp (entry->d_name, ".") == 0)  ||
                    (strcmp (entry->d_name, "..") == 0) ||
                    entry->inode == NULL)
                        continue;

                loc.parent = inode_ref (local->loc.inode);
                loc.inode = inode_ref (entry->inode);
                ret = inode_path (loc.parent, entry->d_name, &resolvedpath);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to get the "
                                "path for the entry %s", entry->d_name);
                        loc_wipe (&loc);
                        continue;
                }

                loc.path = resolvedpath;
                resolvedpath = NULL;

                ctx = mq_inode_ctx_new (loc.inode, this);
                if (ctx == NULL)
                        gf_log (this->name, GF_LOG_WARNING, "mq_inode_ctx_new "
                                "failed for %s", uuid_utoa (loc.inode->gfid));

                mq_xattr_state (this, &loc, entry->dict, entry->d_stat);
                loc_wipe (&loc);

                ret = marker_key_set_ver (this, entry->dict);
                if (ret < 0) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto unwind;
                }
        }

unwind:
        MARKER_STACK_UNWIND (readdirp, frame, op_ret, op_errno, entries, xdata);

        return 0;
}

int
marker_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                 off_t offset, dict_t *dict)
{
        marker_conf_t  *priv  = NULL;
        loc_t           loc   = {0, };
        marker_local_t *local = NULL;
        int             ret   = -1;

        priv = this->private;

        dict = dict ? dict_ref(dict) : dict_new();
        if (!dict)
                goto unwind;

        ret = marker_key_replace_with_ver (this, dict);
        if (ret < 0)
                goto unwind;

        if (dict_get (dict, GET_ANCESTRY_DENTRY_KEY)) {
                STACK_WIND (frame, marker_build_ancestry_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdirp,
                            fd, size, offset, dict);
        } else {
                if (priv->feature_enabled & GF_QUOTA) {
                        local = mem_get0 (this->local_pool);

                        MARKER_INIT_LOCAL (frame, local);

                        loc.parent = local->loc.inode = inode_ref (fd->inode);

                        mq_req_xattr (this, &loc, dict, NULL, NULL);
                }

                STACK_WIND (frame, marker_readdirp_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdirp,
                            fd, size, offset, dict);
        }

        dict_unref (dict);
        return 0;
unwind:
        MARKER_STACK_UNWIND (readdirp, frame, -1, ENOMEM, NULL, NULL);
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
                       " failed");
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

                ret = gf_uuid_parse (priv->volume_uuid, priv->volume_uuid_bin);
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
                        goto out;
                }

                gf_log (this->name, GF_LOG_DEBUG,
                        "volume-uuid = %s", priv->volume_uuid);
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
        int32_t         version = 0;

        GF_ASSERT (this);
        GF_ASSERT (this->private);

        priv = this->private;

        priv->feature_enabled = 0;

        GF_VALIDATE_OR_GOTO (this->name, options, out);

        data = dict_get (options, "quota");
        if (data) {
                ret = gf_string2boolean (data->data, &flag);
                if (ret == 0 && flag == _gf_true)
                        priv->feature_enabled |= GF_QUOTA;
        }

        data = dict_get (options, "inode-quota");
        if (data) {
                ret = gf_string2boolean (data->data, &flag);
                if (ret == 0 && flag == _gf_true)
                        priv->feature_enabled |= GF_INODE_QUOTA;
        }

        data = dict_get (options, "quota-version");
        if (data)
                ret = gf_string2int32 (data->data, &version);

        if (priv->feature_enabled) {
                if (version >= 0)
                        priv->version = version;
                else
                        gf_log (this->name, GF_LOG_ERROR, "Invalid quota "
                                "version %d", priv->version);
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
        priv->version = 0;

        LOCK_INIT (&priv->lock);

        data = dict_get (options, "quota");
        if (data) {
                ret = gf_string2boolean (data->data, &flag);
                if (ret == 0 && flag == _gf_true)
                        priv->feature_enabled |= GF_QUOTA;
        }

        data = dict_get (options, "inode-quota");
        if (data) {
                ret = gf_string2boolean (data->data, &flag);
                if (ret == 0 && flag == _gf_true)
                        priv->feature_enabled |= GF_INODE_QUOTA;
        }

        data = dict_get (options, "quota-version");
        if (data)
                ret = gf_string2int32 (data->data, &priv->version);

        if (priv->feature_enabled && priv->version < 0) {
                gf_log (this->name, GF_LOG_ERROR, "Invalid quota version %d",
                        priv->version);
                goto err;
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
        {.key = {"inode-quota"} },
        {.key = {"xtime"}},
        {.key = {"gsync-force-xtime"}},
        {.key = {"quota-version"} },
        {.key = {NULL}}
};
