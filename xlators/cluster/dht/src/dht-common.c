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

/* TODO: add NS locking */

#include "glusterfs.h"
#include "xlator.h"
#include "libxlator.h"
#include "dht-common.h"
#include "defaults.h"
#include "byte-order.h"
#include "glusterfs-acl.h"

#include <sys/time.h>
#include <libgen.h>

int
dht_aggregate (dict_t *this, char *key, data_t *value, void *data)
{
        dict_t  *dst  = NULL;
        int64_t *ptr  = 0, *size = NULL;
        int32_t  ret  = -1;
        data_t  *dict_data = NULL;

        dst = data;

        if (strcmp (key, GF_XATTR_QUOTA_SIZE_KEY) == 0) {
                ret = dict_get_bin (dst, key, (void **)&size);
                if (ret < 0) {
                        size = GF_CALLOC (1, sizeof (int64_t),
                                          gf_common_mt_char);
                        if (size == NULL) {
                                gf_log ("dht", GF_LOG_WARNING,
                                        "memory allocation failed");
                                return -1;
                        }
                        ret = dict_set_bin (dst, key, size, sizeof (int64_t));
                        if (ret < 0) {
                                gf_log ("dht", GF_LOG_WARNING,
                                        "dht aggregate dict set failed");
                                GF_FREE (size);
                                return -1;
                        }
                }

                ptr = data_to_bin (value);
                if (ptr == NULL) {
                        gf_log ("dht", GF_LOG_WARNING, "data to bin failed");
                        return -1;
                }

                *size = hton64 (ntoh64 (*size) + ntoh64 (*ptr));

        } else if (fnmatch (GF_XATTR_STIME_PATTERN, key, FNM_NOESCAPE) == 0) {
                ret = gf_get_min_stime (THIS, dst, key, value);
                if (ret < 0)
                        return ret;
        } else {
                /* compare user xattrs only */
                if (!strncmp (key, "user.", strlen ("user."))) {
                        ret = dict_lookup (dst, key, &dict_data);
                        if (!ret && dict_data && value) {
                                ret = is_data_equal (dict_data, value);
                                if (!ret)
                                        gf_log ("dht", GF_LOG_DEBUG,
                                                "xattr mismatch for %s", key);
                        }
                }
                ret = dict_set (dst, key, value);
                if (ret)
                        gf_log ("dht", GF_LOG_WARNING, "xattr dict set failed");
        }

        return 0;
}


void
dht_aggregate_xattr (dict_t *dst, dict_t *src)
{
        if ((dst == NULL) || (src == NULL)) {
                goto out;
        }

        dict_foreach (src, dht_aggregate, dst);
out:
        return;
}

/* TODO:
   - use volumename in xattr instead of "dht"
   - use NS locks
   - handle all cases in self heal layout reconstruction
   - complete linkfile selfheal
*/


int
dht_lookup_selfheal_cbk (call_frame_t *frame, void *cookie,
                         xlator_t *this,
                         int op_ret, int op_errno, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        dht_layout_t *layout = NULL;
	int           ret = -1;

        GF_VALIDATE_OR_GOTO ("dht", frame, out);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);

        local = frame->local;
        ret = op_ret;

        FRAME_SU_UNDO (frame, dht_local_t);

        if (ret == 0) {
                layout = local->selfheal.layout;
                ret = dht_layout_set (this, local->inode, layout);
        }

        if (local->loc.parent) {
                dht_inode_ctx_time_update (local->loc.parent, this,
                                           &local->postparent, 1);
        }

        DHT_STRIP_PHASE1_FLAGS (&local->stbuf);

        DHT_STACK_UNWIND (lookup, frame, ret, local->op_errno, local->inode,
                          &local->stbuf, local->xattr, &local->postparent);

out:
        return ret;
}


int
dht_discover_complete (xlator_t *this, call_frame_t *discover_frame)
{
        dht_local_t     *local = NULL;
        call_frame_t    *main_frame = NULL;
        int              op_errno = 0;
        int              ret = -1;
        dht_layout_t    *layout = NULL;
        dht_conf_t      *conf = NULL;

        local = discover_frame->local;
        layout = local->layout;
        conf = this->private;

        LOCK(&discover_frame->lock);
        {
                main_frame = local->main_frame;
                local->main_frame = NULL;
        }
        UNLOCK(&discover_frame->lock);

        if (!main_frame)
                return 0;

        if (local->file_count && local->dir_count) {
                gf_log (this->name, GF_LOG_ERROR,
                        "path %s exists as a file on one subvolume "
                        "and directory on another. "
                        "Please fix it manually",
                        local->loc.path);
                op_errno = EIO;
                goto out;
        }

        if (local->cached_subvol) {
                ret = dht_layout_preset (this, local->cached_subvol,
                                         local->inode);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to set layout for subvolume %s",
                                local->cached_subvol ? local->cached_subvol->name : "<nil>");
                        op_errno = EINVAL;
                        goto out;
                }
        } else {
                ret = dht_layout_normalize (this, &local->loc, layout);
                if ((ret < 0) || ((ret > 0) && (local->op_ret != 0))) {
                        /* either the layout is incorrect or the directory is
                         * not found even in one subvolume.
                         */
                        gf_log (this->name, GF_LOG_DEBUG,
                                "normalizing failed on %s "
                                "(overlaps/holes present: %s, "
                                "ENOENT errors: %d)", local->loc.path,
                                (ret < 0) ? "yes" : "no", (ret > 0) ? ret : 0);
                        if ((ret > 0) && (ret == conf->subvolume_cnt)) {
                                op_errno = ESTALE;
                                goto out;
                        }
                }

                if (local->inode)
                        dht_layout_set (this, local->inode, layout);
        }

        DHT_STACK_UNWIND (lookup, main_frame, local->op_ret, local->op_errno,
                          local->inode, &local->stbuf, local->xattr,
                          &local->postparent);
        return 0;
out:
        DHT_STACK_UNWIND (lookup, main_frame, -1, op_errno, NULL, NULL, NULL,
                          NULL);

        return ret;
}


int
dht_discover_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno,
                  inode_t *inode, struct iatt *stbuf, dict_t *xattr,
                  struct iatt *postparent)
{
        dht_local_t  *local                   = NULL;
        int           this_call_cnt           = 0;
        call_frame_t *prev                    = NULL;
        dht_layout_t *layout                  = NULL;
        int           ret                     = -1;
        int           is_dir                  = 0;
        int           is_linkfile             = 0;
        int           attempt_unwind          = 0;
        dht_conf_t   *conf                    = 0;

        GF_VALIDATE_OR_GOTO ("dht", frame, out);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", this->private, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);

        local = frame->local;
        prev  = cookie;
        conf = this->private;

        layout = local->layout;

        /* Check if the gfid is different for file from other node */
        if (!op_ret && uuid_compare (local->gfid, stbuf->ia_gfid)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: gfid different on %s",
                        local->loc.path, prev->this->name);
        }


        LOCK (&frame->lock);
        {
                /* TODO: assert equal mode on stbuf->st_mode and
                   local->stbuf->st_mode

                   else mkdir/chmod/chown and fix
                */
                ret = dht_layout_merge (this, layout, prev->this,
                                        op_ret, op_errno, xattr);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: failed to merge layouts", local->loc.path);

                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "lookup of %s on %s returned error (%s)",
                                local->loc.path, prev->this->name,
                                strerror (op_errno));

                        goto unlock;
                }

                is_linkfile = check_is_linkfile (inode, stbuf, xattr,
                                                 conf->link_xattr_name);
                is_dir = check_is_dir (inode, stbuf, xattr);

                if (is_dir) {
                        local->dir_count ++;
                } else {
                        local->file_count ++;

                        if (!is_linkfile) {
                                /* real file */
                                local->cached_subvol = prev->this;
                                attempt_unwind = 1;
                        } else {
                                goto unlock;
                        }
                }

                local->op_ret = 0;

                if (local->xattr == NULL) {
                        local->xattr = dict_ref (xattr);
                } else {
                        dht_aggregate_xattr (local->xattr, xattr);
                }

                if (local->inode == NULL)
                        local->inode = inode_ref (inode);

                dht_iatt_merge (this, &local->stbuf, stbuf, prev->this);
                dht_iatt_merge (this, &local->postparent, postparent,
                                prev->this);
        }
unlock:
        UNLOCK (&frame->lock);
out:
        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt) || attempt_unwind) {
                dht_discover_complete (this, frame);
        }

        if (is_last_call (this_call_cnt))
                DHT_STACK_DESTROY (frame);

        return 0;
}


int
dht_discover (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        int          ret;
        dht_local_t *local = NULL;
        dht_conf_t  *conf = NULL;
        int          call_cnt = 0;
        int          op_errno = EINVAL;
        int          i = 0;
        call_frame_t *discover_frame = NULL;

        conf = this->private;
        local = frame->local;

        ret = dict_set_uint32 (local->xattr_req, conf->xattr_name, 4 * 4);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to set '%s' key",
                        loc->path, conf->xattr_name);

        ret = dict_set_uint32 (local->xattr_req, conf->link_xattr_name, 256);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to set '%s' key",
                        loc->path, conf->link_xattr_name);

        call_cnt        = conf->subvolume_cnt;
        local->call_cnt = call_cnt;

        local->layout = dht_layout_new (this, conf->subvolume_cnt);

        if (!local->layout) {
                op_errno = ENOMEM;
                goto err;
        }

        uuid_copy (local->gfid, loc->gfid);

        discover_frame = copy_frame (frame);
        if (!discover_frame) {
                op_errno = ENOMEM;
                goto err;
        }

        discover_frame->local = local;
        frame->local = NULL;
        local->main_frame = frame;

        for (i = 0; i < call_cnt; i++) {
                STACK_WIND (discover_frame, dht_discover_cbk,
                            conf->subvolumes[i],
                            conf->subvolumes[i]->fops->lookup,
                            &local->loc, local->xattr_req);
        }

        return 0;

err:
        DHT_STACK_UNWIND (lookup, frame, -1, op_errno, NULL, NULL, NULL,
                          NULL);

        return 0;
}


int
dht_lookup_dir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno,
                    inode_t *inode, struct iatt *stbuf, dict_t *xattr,
                    struct iatt *postparent)
{
        dht_local_t  *local                   = NULL;
        int           this_call_cnt           = 0;
        call_frame_t *prev                    = NULL;
        dht_layout_t *layout                  = NULL;
        int           ret                     = -1;
        int           is_dir                  = 0;

        GF_VALIDATE_OR_GOTO ("dht", frame, out);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", this->private, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);

        local = frame->local;
        prev  = cookie;

        layout = local->layout;

        if (!op_ret && uuid_is_null (local->gfid))
                memcpy (local->gfid, stbuf->ia_gfid, 16);

        /* Check if the gfid is different for file from other node */
        if (!op_ret && uuid_compare (local->gfid, stbuf->ia_gfid)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: gfid different on %s",
                        local->loc.path, prev->this->name);
        }

        LOCK (&frame->lock);
        {
                /* TODO: assert equal mode on stbuf->st_mode and
                   local->stbuf->st_mode

                   else mkdir/chmod/chown and fix
                */
                ret = dht_layout_merge (this, layout, prev->this,
                                        op_ret, op_errno, xattr);

                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "lookup of %s on %s returned error (%s)",
                                local->loc.path, prev->this->name,
                                strerror (op_errno));

                        goto unlock;
                }

                is_dir = check_is_dir (inode, stbuf, xattr);
                if (!is_dir) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "lookup of %s on %s returned non dir 0%o",
                                local->loc.path, prev->this->name,
                                stbuf->ia_type);
                        local->need_selfheal = 1;
                        goto unlock;
                }

                local->op_ret = 0;
                if (local->xattr == NULL) {
                        local->xattr = dict_ref (xattr);
                } else {
                        dht_aggregate_xattr (local->xattr, xattr);
                }

                if (local->inode == NULL)
                        local->inode = inode_ref (inode);

                dht_iatt_merge (this, &local->stbuf, stbuf, prev->this);
                dht_iatt_merge (this, &local->postparent, postparent,
                                prev->this);
        }
unlock:
        UNLOCK (&frame->lock);


        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                if (local->need_selfheal) {
                        local->need_selfheal = 0;
                        dht_lookup_everywhere (frame, this, &local->loc);
                        return 0;
                }

                if (local->op_ret == 0) {
                        ret = dht_layout_normalize (this, &local->loc, layout);

                        if (ret != 0) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "fixing assignment on %s",
                                        local->loc.path);
                                goto selfheal;
                        }

                        dht_layout_set (this, local->inode, layout);
                }

                if (local->loc.parent) {
                        dht_inode_ctx_time_update (local->loc.parent, this,
                                                   &local->postparent, 1);
                }

                DHT_STRIP_PHASE1_FLAGS (&local->stbuf);
                DHT_STACK_UNWIND (lookup, frame, local->op_ret, local->op_errno,
                                  local->inode, &local->stbuf, local->xattr,
                                  &local->postparent);
        }

        return 0;

selfheal:
        FRAME_SU_DO (frame, dht_local_t);
        uuid_copy (local->loc.gfid, local->gfid);
        ret = dht_selfheal_directory (frame, dht_lookup_selfheal_cbk,
                                      &local->loc, layout);
out:
        return ret;
}

int
dht_revalidate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno,
                    inode_t *inode, struct iatt *stbuf, dict_t *xattr,
                    struct iatt *postparent)
{
        dht_local_t  *local         = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev          = NULL;
        dht_layout_t *layout        = NULL;
        dht_conf_t   *conf          = NULL;
        int           ret  = -1;
        int           is_dir = 0;
        int           is_linkfile = 0;
        call_frame_t *copy          = NULL;
        dht_local_t  *copy_local    = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO ("dht", this, err);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, err);
        GF_VALIDATE_OR_GOTO ("dht", cookie, err);

        local = frame->local;
        prev  = cookie;
        conf = this->private;
        if (!conf)
                goto out;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_errno = op_errno;

                        if ((op_errno != ENOTCONN)
                            && (op_errno != ENOENT)
                            && (op_errno != ESTALE)) {
				gf_log (this->name, GF_LOG_INFO,
					"subvolume %s for %s returned -1 (%s)",
					prev->this->name, local->loc.path,
                                        strerror (op_errno));
			}
                        if (op_errno == ESTALE) {
                                /* propagate the ESTALE to parent.
                                 * setting local->return_estale would send
                                 * ESTALE to parent. */
                                local->return_estale = 1;
                        }

                        /* if it is ENOENT, we may have to do a
                         * 'lookup_everywhere()' to make sure
                         * the file is not migrated */
                        if (op_errno == ENOENT) {
                                if (IA_ISREG (local->loc.inode->ia_type)) {
                                        local->need_lookup_everywhere = 1;
                                }
                        }
                        goto unlock;
                }

                if (stbuf->ia_type != local->inode->ia_type) {
                        gf_log (this->name, GF_LOG_INFO,
                                "mismatching filetypes 0%o v/s 0%o for %s",
                                (stbuf->ia_type), (local->inode->ia_type),
                                local->loc.path);

                        local->op_ret = -1;
                        local->op_errno = EINVAL;

                        goto unlock;
                }

                layout = local->layout;

                is_dir = check_is_dir (inode, stbuf, xattr);
                is_linkfile = check_is_linkfile (inode, stbuf, xattr,
                                                 conf->link_xattr_name);

                if (is_linkfile) {
                        gf_log (this->name, GF_LOG_INFO,
                                "linkfile found in revalidate for %s",
                                local->loc.path);
                        local->return_estale = 1;

                        goto unlock;
                }

                if (is_dir) {
                        ret = dht_dir_has_layout (xattr, conf->xattr_name);
                        if (ret >= 0) {
                                if (is_greater_time(local->stbuf.ia_ctime,
                                                    local->stbuf.ia_ctime_nsec,
                                                    stbuf->ia_ctime,
                                                    stbuf->ia_ctime_nsec)) {
                                        local->prebuf.ia_gid = stbuf->ia_gid;
                                        local->prebuf.ia_uid = stbuf->ia_uid;
                                }
                        }
                        if (local->stbuf.ia_type != IA_INVAL)
                        {
                                if ((local->stbuf.ia_gid != stbuf->ia_gid) ||
                                    (local->stbuf.ia_uid != stbuf->ia_uid)) {
                                        local->need_selfheal = 1;
                                }
                        }
                        ret = dht_layout_dir_mismatch (this, layout,
                                                       prev->this, &local->loc,
                                                       xattr);
                        if (ret != 0) {
                                gf_log (this->name, GF_LOG_INFO,
                                        "mismatching layouts for %s",
                                        local->loc.path);

                                local->layout_mismatch = 1;

                                goto unlock;
                        }
                }

                dht_iatt_merge (this, &local->stbuf, stbuf, prev->this);
                dht_iatt_merge (this, &local->postparent, postparent,
                                prev->this);

                local->op_ret = 0;

                if (!local->xattr) {
                        local->xattr = dict_ref (xattr);
                } else if (is_dir) {
                        dht_aggregate_xattr (local->xattr, xattr);
                }
        }
unlock:
        UNLOCK (&frame->lock);
out:
        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                if (!IA_ISDIR (local->stbuf.ia_type)
                    && (local->hashed_subvol != local->cached_subvol)
                    && (local->stbuf.ia_nlink == 1)
                    && (conf && conf->unhashed_sticky_bit)) {
                        local->stbuf.ia_prot.sticky = 1;
                }
                if (local->need_selfheal) {
                        local->need_selfheal = 0;
                        uuid_copy (local->gfid, local->stbuf.ia_gfid);
                        local->stbuf.ia_gid = local->prebuf.ia_gid;
                        local->stbuf.ia_uid = local->prebuf.ia_uid;
                        copy = create_frame (this, this->ctx->pool);
                        if (copy) {
                                copy_local = dht_local_init (copy, &local->loc,
                                                             NULL, 0);
                                if (!copy_local)
                                        goto cont;
                                copy_local->stbuf = local->stbuf;
                                copy->local = copy_local;
                                FRAME_SU_DO (copy, dht_local_t);
                                ret = synctask_new (this->ctx->env,
                                                    dht_dir_attr_heal,
                                                    dht_dir_attr_heal_done,
                                                    copy, copy);
                        }
                }
cont:
		if (local->layout_mismatch) {
                        /* Found layout mismatch in the directory, need to
                           fix this in the inode context */
                        dht_layout_unref (this, local->layout);
                        local->layout = NULL;
                        dht_lookup_directory (frame, this, &local->loc);
                        return 0;
                }

                if (local->need_lookup_everywhere) {
                        /* As the current layout gave ENOENT error, we would
                           need a new layout */
                        dht_layout_unref (this, local->layout);
                        local->layout = NULL;

                        /* We know that current cached subvol is no more
                           valid, get the new one */
                        local->cached_subvol = NULL;
                        dht_lookup_everywhere (frame, this, &local->loc);
                        return 0;
                }
                if (local->return_estale) {
                        local->op_ret = -1;
                        local->op_errno = ESTALE;
                }

                if (local->loc.parent) {
                        dht_inode_ctx_time_update (local->loc.parent, this,
                                                   &local->postparent, 1);
                }

                DHT_STRIP_PHASE1_FLAGS (&local->stbuf);
                DHT_STACK_UNWIND (lookup, frame, local->op_ret, local->op_errno,
                                  local->inode, &local->stbuf, local->xattr,
                                  &local->postparent);
        }

err:
        return ret;
}


int
dht_lookup_linkfile_create_cbk (call_frame_t *frame, void *cookie,
                                xlator_t *this,
                                int32_t op_ret, int32_t op_errno,
                                inode_t *inode, struct iatt *stbuf,
                                struct iatt *preparent, struct iatt *postparent,
                                dict_t *xdata)
{
        dht_local_t  *local = NULL;
        xlator_t     *cached_subvol = NULL;
        dht_conf_t   *conf = NULL;
        int           ret = -1;

        GF_VALIDATE_OR_GOTO ("dht", frame, out);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", this->private, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);

        local = frame->local;
        cached_subvol = local->cached_subvol;
        conf = this->private;

        ret = dht_layout_preset (this, local->cached_subvol, local->loc.inode);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to set layout for subvolume %s",
                        cached_subvol ? cached_subvol->name : "<nil>");
                local->op_ret = -1;
                local->op_errno = EINVAL;
                goto unwind;
        }

        local->op_ret = 0;
        if ((local->stbuf.ia_nlink == 1)
            && (conf && conf->unhashed_sticky_bit)) {
                local->stbuf.ia_prot.sticky = 1;
        }

        if (local->loc.parent) {
                dht_inode_ctx_time_update (local->loc.parent, this,
                                           postparent, 1);
        }

unwind:
        if (local->linked == _gf_true)
                dht_linkfile_attr_heal (frame, this);

        DHT_STRIP_PHASE1_FLAGS (&local->stbuf);
        DHT_STACK_UNWIND (lookup, frame, local->op_ret, local->op_errno,
                          local->inode, &local->stbuf, local->xattr,
                          &local->postparent);
out:
        return ret;
}


int
dht_lookup_everywhere_done (call_frame_t *frame, xlator_t *this)
{
        int           ret = 0;
        dht_local_t  *local = NULL;
        xlator_t     *hashed_subvol = NULL;
        xlator_t     *cached_subvol = NULL;
        dht_layout_t *layout = NULL;

        local = frame->local;
        hashed_subvol = local->hashed_subvol;
        cached_subvol = local->cached_subvol;

        if (local->file_count && local->dir_count) {
                gf_log (this->name, GF_LOG_ERROR,
                        "path %s exists as a file on one subvolume "
                        "and directory on another. "
                        "Please fix it manually",
                        local->loc.path);
                DHT_STACK_UNWIND (lookup, frame, -1, EIO, NULL, NULL, NULL,
                                  NULL);
                return 0;
        }

        if (local->dir_count) {
                dht_lookup_directory (frame, this, &local->loc);
                return 0;
        }

        if (!cached_subvol) {
                DHT_STACK_UNWIND (lookup, frame, -1, ENOENT, NULL, NULL, NULL,
                                  NULL);
                return 0;
        }

        if (local->need_lookup_everywhere) {
                if (uuid_compare (local->gfid, local->inode->gfid)) {
                        /* GFID different, return error */
                        DHT_STACK_UNWIND (lookup, frame, -1, ENOENT, NULL,
                                          NULL, NULL, NULL);
                        return 0;
                }
                local->op_ret = 0;
                local->op_errno = 0;
                layout = dht_layout_for_subvol (this, cached_subvol);
                if (!layout) {
                        gf_log (this->name, GF_LOG_INFO,
                                "%s: no pre-set layout for subvolume %s",
                                local->loc.path, (cached_subvol ?
                                                  cached_subvol->name :
                                                  "<nil>"));
                }

                ret = dht_layout_set (this, local->inode, layout);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_INFO,
                                "%s: failed to set layout for subvol %s",
                                local->loc.path, (cached_subvol ?
                                                  cached_subvol->name :
                                                  "<nil>"));
                }

                if (local->loc.parent) {
                        dht_inode_ctx_time_update (local->loc.parent, this,
                                                   &local->postparent, 1);
                }

                DHT_STRIP_PHASE1_FLAGS (&local->stbuf);
                DHT_STACK_UNWIND (lookup, frame, local->op_ret,
                                  local->op_errno, local->inode,
                                  &local->stbuf, local->xattr,
                                  &local->postparent);
                return 0;
        }

        if (!hashed_subvol) {
                gf_log (this->name, GF_LOG_INFO,
                        "cannot create linkfile file for %s on %s: "
                        "hashed subvolume cannot be found.",
                        local->loc.path, cached_subvol->name);

                local->op_ret = 0;
                local->op_errno = 0;

                ret = dht_layout_preset (frame->this, cached_subvol,
                                         local->inode);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_INFO,
                                "failed to set layout for subvol %s",
                                cached_subvol ? cached_subvol->name :
                                "<nil>");
                        local->op_ret = -1;
                        local->op_errno = EINVAL;
                }

                if (local->loc.parent) {
                        dht_inode_ctx_time_update (local->loc.parent, this,
                                                   &local->postparent, 1);
                }

                DHT_STRIP_PHASE1_FLAGS (&local->stbuf);
                DHT_STACK_UNWIND (lookup, frame, local->op_ret,
                                  local->op_errno, local->inode,
                                  &local->stbuf, local->xattr,
                                  &local->postparent);
                return 0;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "linking file %s existing on %s to %s (hash)",
                local->loc.path, cached_subvol->name,
                hashed_subvol->name);

        ret = dht_linkfile_create (frame,
                                   dht_lookup_linkfile_create_cbk, this,
                                   cached_subvol, hashed_subvol, &local->loc);

        return ret;
}


int
dht_lookup_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno,
                       struct iatt *preparent, struct iatt *postparent,
                       dict_t *xdata)
{
        int  this_call_cnt = 0;

        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
                dht_lookup_everywhere_done (frame, this);
        }

        return 0;
}


int
dht_lookup_everywhere_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno,
                           inode_t *inode, struct iatt *buf, dict_t *xattr,
                           struct iatt *postparent)
{
        dht_local_t  *local         = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev          = NULL;
        int           is_linkfile   = 0;
        int           is_dir        = 0;
        xlator_t     *subvol        = NULL;
        loc_t        *loc           = NULL;
        xlator_t     *link_subvol   = NULL;
        int           ret           = -1;
        int32_t       fd_count      = 0;
        dht_conf_t   *conf          = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, out);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);
        GF_VALIDATE_OR_GOTO ("dht", this->private, out);

        local  = frame->local;
        loc    = &local->loc;
        conf   = this->private;

        prev   = cookie;
        subvol = prev->this;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        if (op_errno != ENOENT)
                                local->op_errno = op_errno;
                        goto unlock;
                }

                if (uuid_is_null (local->gfid))
                        uuid_copy (local->gfid, buf->ia_gfid);

                if (uuid_compare (local->gfid, buf->ia_gfid)) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: gfid differs on subvolume %s",
                                loc->path, prev->this->name);
                }

                is_linkfile = check_is_linkfile (inode, buf, xattr,
                                                 conf->link_xattr_name);
                is_dir = check_is_dir (inode, buf, xattr);

                if (is_linkfile) {
                        link_subvol = dht_linkfile_subvol (this, inode, buf,
                                                           xattr);
                        gf_log (this->name, GF_LOG_DEBUG,
                                "found on %s linkfile %s (-> %s)",
                                subvol->name, loc->path,
                                link_subvol ? link_subvol->name : "''");
                        goto unlock;
                }

                /* non linkfile GFID takes precedence */
                uuid_copy (local->gfid, buf->ia_gfid);

                if (is_dir) {
                        local->dir_count++;

                        gf_log (this->name, GF_LOG_DEBUG,
                                "found on %s directory %s",
                                subvol->name, loc->path);
                } else {
                        local->file_count++;

                        if (!local->cached_subvol) {
                                /* found one file */
                                dht_iatt_merge (this, &local->stbuf, buf,
                                                subvol);
                                local->xattr = dict_ref (xattr);
                                local->cached_subvol = subvol;
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "found on %s file %s",
                                        subvol->name, loc->path);

                                dht_iatt_merge (this, &local->postparent,
                                                postparent, subvol);
                        } else {
                                /* This is where we need 'rename' both entries logic */
                                gf_log (this->name, GF_LOG_WARNING,
                                        "multiple subvolumes (%s and %s) have "
                                        "file %s (preferably rename the file "
                                        "in the backend, and do a fresh lookup)",
                                        local->cached_subvol->name,
                                        subvol->name, local->loc.path);
                        }
                }
        }
unlock:
        UNLOCK (&frame->lock);

        if (is_linkfile) {
                ret = dict_get_int32 (xattr, GLUSTERFS_OPEN_FD_COUNT, &fd_count);
                /* Delete the linkfile only if there are no open fds on it.
                   if there is a open-fd, it may be in migration */
                if (!ret && (fd_count == 0)) {
                        gf_log (this->name, GF_LOG_INFO,
                                "deleting stale linkfile %s on %s",
                                loc->path, subvol->name);
                        STACK_WIND (frame, dht_lookup_unlink_cbk,
                                    subvol, subvol->fops->unlink, loc, 0, NULL);
                        return 0;
                }
        }

        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
                dht_lookup_everywhere_done (frame, this);
        }

out:
        return ret;
}


int
dht_lookup_everywhere (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        dht_conf_t     *conf = NULL;
        dht_local_t    *local = NULL;
        int             i = 0;
        int             call_cnt = 0;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", this->private, out);
        GF_VALIDATE_OR_GOTO ("dht", loc, out);

        conf = this->private;
        local = frame->local;

        call_cnt = conf->subvolume_cnt;
        local->call_cnt = call_cnt;

        if (!local->inode)
                local->inode = inode_ref (loc->inode);

        for (i = 0; i < call_cnt; i++) {
                STACK_WIND (frame, dht_lookup_everywhere_cbk,
                            conf->subvolumes[i],
                            conf->subvolumes[i]->fops->lookup,
                            loc, local->xattr_req);
        }

        return 0;
out:
        DHT_STACK_UNWIND (lookup, frame, -1, EINVAL, NULL, NULL, NULL, NULL);
err:
        return -1;
}


int
dht_lookup_linkfile_cbk (call_frame_t *frame, void *cookie,
                         xlator_t *this, int op_ret, int op_errno,
                         inode_t *inode, struct iatt *stbuf, dict_t *xattr,
                         struct iatt *postparent)
{
        call_frame_t *prev          = NULL;
        dht_local_t  *local         = NULL;
        xlator_t     *subvol        = NULL;
        loc_t        *loc           = NULL;
        dht_conf_t   *conf          = NULL;
        int           ret           = 0;

        GF_VALIDATE_OR_GOTO ("dht", frame, out);
        GF_VALIDATE_OR_GOTO ("dht", this, unwind);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, unwind);
        GF_VALIDATE_OR_GOTO ("dht", this->private, unwind);
        GF_VALIDATE_OR_GOTO ("dht", cookie, unwind);

        prev   = cookie;
        subvol = prev->this;
        conf   = this->private;
        local  = frame->local;
        loc    = &local->loc;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_INFO,
                        "lookup of %s on %s (following linkfile) failed (%s)",
                        local->loc.path, subvol->name, strerror (op_errno));

                /* If cached subvol returned ENOTCONN, do not do
                lookup_everywhere. We need to make sure linkfile does not get
                removed, which can take away the namespace, and subvol is
                anyways down. */

                if (op_errno != ENOTCONN)
                        goto err;
                else
                        goto unwind;
        }

        if (check_is_dir (inode, stbuf, xattr)) {
                gf_log (this->name, GF_LOG_INFO,
                        "lookup of %s on %s (following linkfile) reached dir",
                        local->loc.path, subvol->name);
                goto err;
        }

        if (check_is_linkfile (inode, stbuf, xattr, conf->link_xattr_name)) {
                gf_log (this->name, GF_LOG_INFO,
                        "lookup of %s on %s (following linkfile) reached link",
                        local->loc.path, subvol->name);
                goto err;
        }

        if (uuid_compare (local->gfid, stbuf->ia_gfid)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: gfid different on data file on %s",
                        local->loc.path, subvol->name);
                goto err;
        }

        if ((stbuf->ia_nlink == 1)
            && (conf && conf->unhashed_sticky_bit)) {
                stbuf->ia_prot.sticky = 1;
        }

        ret = dht_layout_preset (this, prev->this, inode);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "failed to set layout for subvolume %s",
                        prev->this->name);
                op_ret   = -1;
                op_errno = EINVAL;
        }

        if (local->loc.parent) {
                dht_inode_ctx_time_update (local->loc.parent, this,
                                           postparent, 1);
        }

unwind:
        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (lookup, frame, op_ret, op_errno, inode, stbuf, xattr,
                          postparent);

        return 0;

err:
        dht_lookup_everywhere (frame, this, loc);
out:
        return 0;
}


int
dht_lookup_directory (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        int           call_cnt = 0;
        int           i = 0;
        dht_conf_t   *conf = NULL;
        dht_local_t  *local = NULL;
        int           ret = 0;

        GF_VALIDATE_OR_GOTO ("dht", frame, out);
        GF_VALIDATE_OR_GOTO ("dht", this, unwind);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, unwind);
        GF_VALIDATE_OR_GOTO ("dht", this->private, unwind);
        GF_VALIDATE_OR_GOTO ("dht", loc, unwind);

        conf = this->private;
        local = frame->local;

        call_cnt        = conf->subvolume_cnt;
        local->call_cnt = call_cnt;

        local->layout = dht_layout_new (this, conf->subvolume_cnt);
        if (!local->layout) {
                goto unwind;
        }

        if (local->xattr != NULL) {
                dict_unref (local->xattr);
                local->xattr = NULL;
        }

        if (!uuid_is_null (local->gfid)) {
                ret = dict_set_static_bin (local->xattr_req, "gfid-req",
                                           local->gfid, 16);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: failed to set gfid", local->loc.path);
        }

        for (i = 0; i < call_cnt; i++) {
                STACK_WIND (frame, dht_lookup_dir_cbk,
                            conf->subvolumes[i],
                            conf->subvolumes[i]->fops->lookup,
                            &local->loc, local->xattr_req);
        }
        return 0;
unwind:
        DHT_STACK_UNWIND (lookup, frame, -1, ENOMEM, NULL, NULL, NULL, NULL);
out:
        return 0;

}


int
dht_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno,
                inode_t *inode, struct iatt *stbuf, dict_t *xattr,
                struct iatt *postparent)
{
        char          is_linkfile   = 0;
        char          is_dir        = 0;
        xlator_t     *subvol        = NULL;
        dht_conf_t   *conf          = NULL;
        dht_local_t  *local         = NULL;
        loc_t        *loc           = NULL;
        call_frame_t *prev          = NULL;
        int           ret           = 0;
        dht_layout_t *parent_layout = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);
        GF_VALIDATE_OR_GOTO ("dht", this->private, out);

        conf  = this->private;

        prev  = cookie;
        local = frame->local;
        loc   = &local->loc;

        /* This is required for handling stale linkfile deletion,
         * or any more call which happens from this 'loc'.
         */
        if (!op_ret && uuid_is_null (local->gfid))
                memcpy (local->gfid, stbuf->ia_gfid, 16);

        if (ENTRY_MISSING (op_ret, op_errno)) {
                gf_log (this->name, GF_LOG_TRACE, "Entry %s missing on subvol"
                        " %s", loc->path, prev->this->name);
                if (conf->search_unhashed == GF_DHT_LOOKUP_UNHASHED_ON) {
                        local->op_errno = ENOENT;
                        dht_lookup_everywhere (frame, this, loc);
                        return 0;
                }
                if ((conf->search_unhashed == GF_DHT_LOOKUP_UNHASHED_AUTO) &&
                    (loc->parent)) {
                        ret = dht_inode_ctx_layout_get (loc->parent, this,
                                                        &parent_layout);
                        if (ret || !parent_layout)
                                goto out;
                        if (parent_layout->search_unhashed) {
                                local->op_errno = ENOENT;
                                dht_lookup_everywhere (frame, this, loc);
                                return 0;
                        }
                }
        }

        if (op_ret == 0) {
                is_dir      = check_is_dir (inode, stbuf, xattr);
                if (is_dir) {
                        local->inode = inode_ref (inode);
                        local->xattr = dict_ref (xattr);
                }
        }

        if (is_dir || (op_ret == -1 && op_errno == ENOTCONN)) {
                dht_lookup_directory (frame, this, &local->loc);
                return 0;
        }

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "Lookup of %s for subvolume"
                       " %s failed with error %s", loc->path, prev->this->name,
                       strerror (op_errno));
                goto out;
        }

        is_linkfile = check_is_linkfile (inode, stbuf, xattr,
                                         conf->link_xattr_name);

        if (!is_linkfile) {
                /* non-directory and not a linkfile */

                ret = dht_layout_preset (this, prev->this, inode);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_INFO,
                                "could not set pre-set layout for subvolume %s",
                                prev->this->name);
                        op_ret   = -1;
                        op_errno = EINVAL;
                        goto out;
                }
                goto out;
        }

        subvol = dht_linkfile_subvol (this, inode, stbuf, xattr);
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "linkfile not having link subvolume. path=%s",
                        loc->path);
                dht_lookup_everywhere (frame, this, loc);
                return 0;
        }

        STACK_WIND (frame, dht_lookup_linkfile_cbk,
                    subvol, subvol->fops->lookup,
                    &local->loc, local->xattr_req);

        return 0;

out:
        /*
         * FIXME: postparent->ia_size and postparent->st_blocks do not have
         * correct values. since, postparent corresponds to a directory these
         * two members should have values equal to sum of corresponding values
         * from each of the subvolume. See dht_iatt_merge for reference.
         */

        if (!op_ret && local->loc.parent) {
                dht_inode_ctx_time_update (local->loc.parent, this,
                                           postparent, 1);
        }

        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (lookup, frame, op_ret, op_errno, inode, stbuf, xattr,
                          postparent);
err:
        return 0;
}

/* For directories, check if acl xattrs have been requested (by the acl xlator),
 * if not, request for them. These xattrs are needed for dht dir self-heal to
 * perform proper self-healing of dirs
 */
void
dht_check_and_set_acl_xattr_req (inode_t *inode, dict_t *xattr_req)
{
        int     ret = 0;

        GF_ASSERT (inode);
        GF_ASSERT (xattr_req);

        if (inode->ia_type != IA_IFDIR)
                return;

        if (!dict_get (xattr_req, POSIX_ACL_ACCESS_XATTR)) {
                ret = dict_set_int8 (xattr_req, POSIX_ACL_ACCESS_XATTR, 0);
                if (ret)
                        gf_log (THIS->name, GF_LOG_WARNING,
                                "failed to set key %s",
                                POSIX_ACL_ACCESS_XATTR);
        }

        if (!dict_get (xattr_req, POSIX_ACL_DEFAULT_XATTR)) {
                ret = dict_set_int8 (xattr_req, POSIX_ACL_DEFAULT_XATTR, 0);
                if (ret)
                        gf_log (THIS->name, GF_LOG_WARNING,
                                "failed to set key %s",
                                POSIX_ACL_DEFAULT_XATTR);
        }

        return;
}

int
dht_lookup (call_frame_t *frame, xlator_t *this,
            loc_t *loc, dict_t *xattr_req)
{
        xlator_t     *subvol = NULL;
        xlator_t     *hashed_subvol = NULL;
        dht_local_t  *local  = NULL;
        dht_conf_t   *conf = NULL;
        int           ret    = -1;
        int           op_errno = -1;
        dht_layout_t *layout = NULL;
        int           i = 0;
        int           call_cnt = 0;
        loc_t         new_loc = {0,};

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        conf = this->private;
        if (!conf)
                goto err;

        local = dht_local_init (frame, loc, NULL, GF_FOP_LOOKUP);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        ret = dht_filter_loc_subvol_key (this, loc, &new_loc,
                                         &hashed_subvol);
        if (ret) {
                loc_wipe (&local->loc);
                ret = loc_dup (&new_loc, &local->loc);

                /* we no more need 'new_loc' entries */
                loc_wipe (&new_loc);

                /* check if loc_dup() is successful */
                if (ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "copying location failed for path=%s",
                                loc->path);
                        goto err;
                }
        }

        if (xattr_req) {
                local->xattr_req = dict_ref (xattr_req);
        } else {
                local->xattr_req = dict_new ();
        }

        if (uuid_is_null (loc->pargfid) && !uuid_is_null (loc->gfid) &&
            !__is_root_gfid (loc->inode->gfid)) {
                local->cached_subvol = NULL;
                dht_discover (frame, this, loc);
                return 0;
        }

        if (!hashed_subvol)
                hashed_subvol = dht_subvol_get_hashed (this, loc);
        local->hashed_subvol = hashed_subvol;

        if (is_revalidate (loc)) {
                layout = local->layout;
                if (!layout) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "revalidate without cache. path=%s",
                                loc->path);
                        op_errno = EINVAL;
                        goto err;
                }

                if (layout->gen && (layout->gen < conf->gen)) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "incomplete layout failure for path=%s",
                                loc->path);

                        dht_layout_unref (this, local->layout);
                        local->layout = NULL;
                        local->cached_subvol = NULL;
                        goto do_fresh_lookup;
                }

                local->inode = inode_ref (loc->inode);

                /* NOTE: we don't require 'trusted.glusterfs.dht.linkto' attribute,
                 *       revalidates directly go to the cached-subvolume.
                 */
                ret = dict_set_uint32 (local->xattr_req,
                                       conf->xattr_name, 4 * 4);

                if (IA_ISDIR (local->inode->ia_type)) {
                        local->call_cnt = call_cnt = conf->subvolume_cnt;
                        for (i = 0; i < call_cnt; i++) {
                                STACK_WIND (frame, dht_revalidate_cbk,
                                            conf->subvolumes[i],
                                            conf->subvolumes[i]->fops->lookup,
                                            loc, local->xattr_req);
                        }
                        return 0;
                }

                call_cnt = local->call_cnt = layout->cnt;

                /* need it for self-healing linkfiles which is
                   'in-migration' state */
                ret = dict_set_uint32 (local->xattr_req,
                                       GLUSTERFS_OPEN_FD_COUNT, 4);

                /* need it for dir self-heal */
                dht_check_and_set_acl_xattr_req (loc->inode, local->xattr_req);

		for (i = 0; i < call_cnt; i++) {
			subvol = layout->list[i].xlator;

			STACK_WIND (frame, dht_revalidate_cbk,
				    subvol, subvol->fops->lookup,
				    &local->loc, local->xattr_req);

		}
        } else {
        do_fresh_lookup:
                /* TODO: remove the hard-coding */
                ret = dict_set_uint32 (local->xattr_req,
                                       conf->xattr_name, 4 * 4);

                ret = dict_set_uint32 (local->xattr_req,
                                       conf->link_xattr_name, 256);

                /* need it for self-healing linkfiles which is
                   'in-migration' state */
                ret = dict_set_uint32 (local->xattr_req,
                                       GLUSTERFS_OPEN_FD_COUNT, 4);

                /* need it for dir self-heal */
                dht_check_and_set_acl_xattr_req (loc->inode, local->xattr_req);

                if (!hashed_subvol) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "no subvolume in layout for path=%s, "
                                "checking on all the subvols to see if "
                                "it is a directory", loc->path);
                        call_cnt        = conf->subvolume_cnt;
                        local->call_cnt = call_cnt;

                        local->layout = dht_layout_new (this,
                                                        conf->subvolume_cnt);
                        if (!local->layout) {
                                op_errno = ENOMEM;
                                goto err;
                        }

                        for (i = 0; i < call_cnt; i++) {
                                STACK_WIND (frame, dht_lookup_dir_cbk,
                                            conf->subvolumes[i],
                                            conf->subvolumes[i]->fops->lookup,
                                            &local->loc, local->xattr_req);
                        }
                        return 0;
                }

                STACK_WIND (frame, dht_lookup_cbk,
                            hashed_subvol, hashed_subvol->fops->lookup,
                            loc, local->xattr_req);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (lookup, frame, -1, op_errno, NULL, NULL, NULL,
                          NULL);
        return 0;
}


int
dht_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev  = NULL;

        local = frame->local;
        prev  = cookie;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_ret   = -1;
                        local->op_errno = op_errno;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "subvolume %s returned -1 (%s)",
                                prev->this->name, strerror (op_errno));
                        goto unlock;
                }

                local->op_ret = 0;

                local->postparent = *postparent;
                local->preparent = *preparent;

                if (local->loc.parent) {
                        dht_inode_ctx_time_update (local->loc.parent, this,
                                                   &local->preparent, 0);
                        dht_inode_ctx_time_update (local->loc.parent, this,
                                                   &local->postparent, 1);
                }
        }
unlock:
        UNLOCK (&frame->lock);

        DHT_STACK_UNWIND (unlink, frame, local->op_ret, local->op_errno,
                          &local->preparent, &local->postparent, NULL);

        return 0;
}


int
dht_unlink_linkfile_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, struct iatt *preparent,
                         struct iatt *postparent, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;

        xlator_t *cached_subvol = NULL;

        local = frame->local;
        prev  = cookie;

        LOCK (&frame->lock);
        {
                if ((op_ret == -1) && !((op_errno == ENOENT) ||
                                        (op_errno == ENOTCONN))) {
                        local->op_errno = op_errno;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "subvolume %s returned -1 (%s)",
                                prev->this->name, strerror (op_errno));
                        goto unlock;
                }

                local->op_ret = 0;
        }
unlock:
        UNLOCK (&frame->lock);

        if (local->op_ret == -1)
                goto err;

        cached_subvol = dht_subvol_get_cached (this, local->loc.inode);
        if (!cached_subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for path=%s",
                        local->loc.path);
                local->op_errno = EINVAL;
                goto err;
        }

        STACK_WIND (frame, dht_unlink_cbk,
                    cached_subvol, cached_subvol->fops->unlink,
                    &local->loc, local->flags, NULL);

        return 0;

err:
        DHT_STACK_UNWIND (unlink, frame, -1, local->op_errno,
                          NULL, NULL, NULL);
        return 0;
}

int
dht_err_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int op_ret, int op_errno, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev = NULL;

        local = frame->local;
        prev = cookie;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "subvolume %s returned -1 (%s)",
                                prev->this->name, strerror (op_errno));
                        goto unlock;
                }

                local->op_ret = 0;
        }
unlock:
        UNLOCK (&frame->lock);

        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
                DHT_STACK_UNWIND (setxattr, frame, local->op_ret,
                                  local->op_errno, NULL);
        }

        return 0;
}

static void
fill_layout_info (dht_layout_t *layout, char *buf)
{
        int i = 0;
        char tmp_buf[128] = {0,};

        for (i = 0; i < layout->cnt; i++) {
                snprintf (tmp_buf, 128, "(%s %u %u)",
                          layout->list[i].xlator->name,
                          layout->list[i].start,
                          layout->list[i].stop);
                if (i)
                        strcat (buf, " ");
                strcat (buf, tmp_buf);
        }
}

void
dht_fill_pathinfo_xattr (xlator_t *this, dht_local_t *local,
                         char *xattr_buf, int32_t alloc_len,
                         int flag, char *layout_buf)
{
        if (flag && local->xattr_val)
                snprintf (xattr_buf, alloc_len,
                          "((<"DHT_PATHINFO_HEADER"%s> %s) (%s-layout %s))",
                          this->name, local->xattr_val, this->name,
                          layout_buf);
        else if (local->xattr_val)
                snprintf (xattr_buf, alloc_len,
                          "(<"DHT_PATHINFO_HEADER"%s> %s)",
                          this->name, local->xattr_val);
        else if (flag)
                snprintf (xattr_buf, alloc_len, "(%s-layout %s)",
                          this->name, layout_buf);
}

int
dht_vgetxattr_alloc_and_fill (dht_local_t *local, dict_t *xattr, xlator_t *this,
                              int op_errno)
{
        int      ret       = -1;
        char    *value     = NULL;
        int32_t  plen      = 0;

        ret = dict_get_str (xattr, local->xsel, &value);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Subvolume %s returned -1 (%s)", this->name,
                        strerror (op_errno));
                local->op_ret = -1;
                local->op_errno = op_errno;
                goto out;
        }

        local->alloc_len += strlen(value);

        if (!local->xattr_val) {
                local->alloc_len += (strlen (DHT_PATHINFO_HEADER) + 10);
                local->xattr_val = GF_CALLOC (local->alloc_len, sizeof (char),
                                              gf_common_mt_char);
                if (!local->xattr_val) {
                        ret = -1;
                        goto out;
                }
        }

        if (local->xattr_val) {
                plen = strlen (local->xattr_val);
                if (plen) {
                        /* extra byte(s) for \0 to be safe */
                        local->alloc_len += (plen + 2);
                        local->xattr_val = GF_REALLOC (local->xattr_val,
                                                       local->alloc_len);
                        if (!local->xattr_val) {
                                ret = -1;
                                goto out;
                        }
                }

                (void) strcat (local->xattr_val, value);
                (void) strcat (local->xattr_val, " ");
                local->op_ret = 0;
        }

        ret = 0;

 out:
        return ret;
}

int
dht_vgetxattr_fill_and_set (dht_local_t *local, dict_t **dict, xlator_t *this,
                            gf_boolean_t flag)
{
        int   ret             = -1;
        char *xattr_buf       = NULL;
        char layout_buf[8192] = {0,};

        if (flag)
                fill_layout_info (local->layout, layout_buf);

        *dict = dict_new ();
        if (!*dict)
                goto out;

        local->xattr_val[strlen (local->xattr_val) - 1] = '\0';

        /* we would need max this many bytes to create xattr string
         * extra 40 bytes is just an estimated amount of additional
         * space required as we include translator name and some
         * spaces, brackets etc. when forming the pathinfo string.
         *
         * For node-uuid we just don't have all the pretty formatting,
         * but since this is a generic routine for pathinfo & node-uuid
         * we dont have conditional space allocation and try to be
         * generic
         */
        local->alloc_len += (2 * strlen (this->name))
                + strlen (layout_buf)
                + 40;
        xattr_buf = GF_CALLOC (local->alloc_len, sizeof (char),
                               gf_common_mt_char);
        if (!xattr_buf)
                goto out;

        if (XATTR_IS_PATHINFO (local->xsel)) {
                (void) dht_fill_pathinfo_xattr (this, local, xattr_buf,
                                                local->alloc_len, flag,
                                                layout_buf);
        } else if (XATTR_IS_NODE_UUID (local->xsel)) {
                (void) snprintf (xattr_buf, local->alloc_len, "%s",
                                 local->xattr_val);
        } else {
                gf_log (this->name, GF_LOG_WARNING,
                        "Unknown local->xsel (%s)", local->xsel);
                GF_FREE (xattr_buf);
                goto out;
        }

        ret = dict_set_dynstr (*dict, local->xsel, xattr_buf);
        if (ret)
                GF_FREE (xattr_buf);
        GF_FREE (local->xattr_val);

 out:
        return ret;
}

int
dht_vgetxattr_dir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, dict_t *xattr, dict_t *xdata)
{
        int          ret           = 0;
        dht_local_t *local         = NULL;
        int          this_call_cnt = 0;
        dict_t      *dict          = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (frame->local, out);

        local = frame->local;

        LOCK (&frame->lock);
        {
                this_call_cnt = --local->call_cnt;
                if (op_ret < 0) {
                        if (op_errno != ENOTCONN) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "getxattr err (%s) for dir",
                                        strerror (op_errno));
				local->op_ret = -1;
				local->op_errno = op_errno;
                        }

                        goto unlock;
                }

                ret = dht_vgetxattr_alloc_and_fill (local, xattr, this,
                                                    op_errno);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR,
                                "alloc or fill failure");
        }
 unlock:
        UNLOCK (&frame->lock);

        if (!is_last_call (this_call_cnt))
                goto out;

        /* -- last call: do patch ups -- */

        if (local->op_ret == -1) {
                goto unwind;
        }

        ret = dht_vgetxattr_fill_and_set (local, &dict, this, _gf_true);
        if (ret)
                goto unwind;

        DHT_STACK_UNWIND (getxattr, frame, 0, 0, dict, xdata);
        goto cleanup;

 unwind:
        DHT_STACK_UNWIND (getxattr, frame, -1, local->op_errno, NULL, NULL);
 cleanup:
        if (dict)
                dict_unref (dict);
 out:
        return 0;
}

int
dht_vgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, dict_t *xattr, dict_t *xdata)
{
        dht_local_t  *local         = NULL;
        int           ret           = 0;
        dict_t       *dict          = NULL;
        call_frame_t *prev          = NULL;
        gf_boolean_t  flag          = _gf_true;

        local = frame->local;
        prev = cookie;

        if (op_ret < 0) {
                local->op_ret = -1;
                local->op_errno = op_errno;
                gf_log (this->name, GF_LOG_ERROR, "Subvolume %s returned -1 "
                        "(%s)", prev->this->name, strerror (op_errno));
                goto unwind;
        }

        ret = dht_vgetxattr_alloc_and_fill (local, xattr, this,
                                            op_errno);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "alloc or fill failure");
                goto unwind;
        }

        flag = (local->layout->cnt > 1) ? _gf_true : _gf_false;

        ret = dht_vgetxattr_fill_and_set (local, &dict, this, flag);
        if (ret)
                goto unwind;

        DHT_STACK_UNWIND (getxattr, frame, 0, 0, dict, xdata);
        goto cleanup;

 unwind:
        DHT_STACK_UNWIND (getxattr, frame, -1, local->op_errno,
                          NULL, NULL);
 cleanup:
        if (dict)
                dict_unref (dict);

        return 0;
}

int
dht_linkinfo_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int op_ret, int op_errno, dict_t *xattr,
                           dict_t *xdata)
{
        int   ret   = 0;
        char *value = NULL;

        if (op_ret != -1) {
                ret = dict_get_str (xattr, GF_XATTR_PATHINFO_KEY, &value);
                if (!ret) {
                        ret = dict_set_str (xattr, GF_XATTR_LINKINFO_KEY, value);
                        if (!ret)
                                gf_log (this->name, GF_LOG_TRACE,
                                        "failed to set linkinfo");
                }
        }

        DHT_STACK_UNWIND (getxattr, frame, op_ret, op_errno, xattr, xdata);

        return 0;
}

int
dht_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, dict_t *xattr, dict_t *xdata)
{
        int             this_call_cnt = 0;
        dht_local_t     *local = NULL;
        dht_conf_t      *conf = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (frame->local, out);
        VALIDATE_OR_GOTO (this->private, out);

        conf = this->private;
        local = frame->local;

        this_call_cnt = dht_frame_return (frame);

        if (!xattr || (op_ret == -1))
                goto out;

        if (dict_get (xattr, conf->xattr_name)) {
                dict_del (xattr, conf->xattr_name);
        }

        if (frame->root->pid >= 0 ) {
                GF_REMOVE_INTERNAL_XATTR("trusted.glusterfs.quota*", xattr);
                GF_REMOVE_INTERNAL_XATTR("trusted.pgfid*", xattr);
        }

        local->op_ret = 0;

        if (!local->xattr) {
                local->xattr = dict_copy_with_ref (xattr, NULL);
        } else {
                dht_aggregate_xattr (local->xattr, xattr);
        }
out:
        if (is_last_call (this_call_cnt)) {
                DHT_STACK_UNWIND (getxattr, frame, local->op_ret, op_errno,
                                  local->xattr, NULL);
        }
        return 0;
}

int32_t
dht_getxattr_unwind (call_frame_t *frame,
                     int op_ret, int op_errno, dict_t *dict, dict_t *xdata)
{
        DHT_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
dht_getxattr_get_real_filename_cbk (call_frame_t *frame, void *cookie,
				    xlator_t *this, int op_ret, int op_errno,
				    dict_t *xattr, dict_t *xdata)
{
        int             this_call_cnt = 0;
        dht_local_t     *local = NULL;


        local = frame->local;

	if (op_ret != -1) {
		if (local->xattr)
			dict_unref (local->xattr);
		local->xattr = dict_ref (xattr);

		if (local->xattr_req)
			dict_unref (local->xattr_req);
		local->xattr_req = dict_ref (xdata);
	}

	this_call_cnt = dht_frame_return (frame);
	if (is_last_call (this_call_cnt)) {
		DHT_STACK_UNWIND (getxattr, frame, local->op_ret, op_errno,
				  local->xattr, local->xattr_req);
	}

	return 0;
}


int
dht_getxattr_get_real_filename (call_frame_t *frame, xlator_t *this,
				loc_t *loc, const char *key, dict_t *xdata)
{
	dht_local_t     *local = NULL;
	int              i = 0;
	dht_layout_t    *layout = NULL;
	int              cnt = 0;
	xlator_t        *subvol = NULL;


	local = frame->local;
	layout = local->layout;

	cnt = local->call_cnt = layout->cnt;

	local->op_ret = -1;
	local->op_errno = ENODATA;

	for (i = 0; i < cnt; i++) {
		subvol = layout->list[i].xlator;
		STACK_WIND (frame, dht_getxattr_get_real_filename_cbk,
			    subvol, subvol->fops->getxattr,
			    loc, key, xdata);
	}

	return 0;
}


int
dht_getxattr (call_frame_t *frame, xlator_t *this,
              loc_t *loc, const char *key, dict_t *xdata)
#define DHT_IS_DIR(layout)  (layout->cnt > 1)
{

        xlator_t     *subvol        = NULL;
        xlator_t     *hashed_subvol = NULL;
        xlator_t     *cached_subvol = NULL;
        dht_conf_t   *conf          = NULL;
        dht_local_t  *local         = NULL;
        dht_layout_t *layout        = NULL;
        xlator_t     **sub_volumes  = NULL;
        int           op_errno      = -1;
        int           i             = 0;
        int           cnt           = 0;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (this->private, err);

        conf   = this->private;

        local = dht_local_init (frame, loc, NULL, GF_FOP_GETXATTR);
        if (!local) {
                op_errno = ENOMEM;

                goto err;
        }

        layout = local->layout;
        if (!layout) {
                gf_log (this->name, GF_LOG_ERROR,
                        "layout is NULL");
                op_errno = ENOENT;
                goto err;
        }

        if (key) {
                local->key = gf_strdup (key);
                if (!local->key) {
                        op_errno = ENOMEM;
                        goto err;
                }
        }

	if (key &&
	    (strncmp (key, GF_XATTR_GET_REAL_FILENAME_KEY,
		      strlen (GF_XATTR_GET_REAL_FILENAME_KEY)) == 0)
	    && DHT_IS_DIR(layout)) {
		dht_getxattr_get_real_filename (frame, this, loc, key, xdata);
		return 0;
	}

        /* for file use cached subvolume (obviously!): see if {}
         * below
         * for directory:
         *  wind to all subvolumes and exclude subvolumes which
         *  return ENOTCONN (in callback)
         *
         * NOTE: Don't trust inode here, as that may not be valid
         *       (until inode_link() happens)
         */
        if (key && DHT_IS_DIR(layout) &&
            (XATTR_IS_PATHINFO (key)
             || (strcmp (key, GF_XATTR_NODE_UUID_KEY) == 0))) {
                (void) strncpy (local->xsel, key, 256);
                cnt = local->call_cnt = layout->cnt;
                for (i = 0; i < cnt; i++) {
                        subvol = layout->list[i].xlator;
                        STACK_WIND (frame, dht_vgetxattr_dir_cbk,
                                    subvol, subvol->fops->getxattr,
                                    loc, key, NULL);
                }
                return 0;
        }

        /* node-uuid or pathinfo for files */
        if (key && ((strcmp (key, GF_XATTR_NODE_UUID_KEY) == 0)
                    || XATTR_IS_PATHINFO (key))) {
                cached_subvol = local->cached_subvol;
                (void) strncpy (local->xsel, key, 256);

                local->call_cnt = 1;
                STACK_WIND (frame, dht_vgetxattr_cbk, cached_subvol,
                            cached_subvol->fops->getxattr, loc, key, NULL);

                return 0;
        }

        if (key && (strcmp (key, GF_XATTR_LINKINFO_KEY) == 0)) {
                hashed_subvol = dht_subvol_get_hashed (this, loc);
                if (!hashed_subvol) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to get"
                                "hashed subvol for %s", loc->path);
                        op_errno = EINVAL;
                        goto err;
                }

                cached_subvol = dht_subvol_get_cached (this, loc->inode);
                if (!cached_subvol) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to get"
                                "cached subvol for %s", loc->path);
                        op_errno = EINVAL;
                        goto err;
                }

                if (hashed_subvol == cached_subvol) {
                        op_errno = ENODATA;
                        goto err;
                }
                if (hashed_subvol) {
                        STACK_WIND (frame, dht_linkinfo_getxattr_cbk, hashed_subvol,
                                    hashed_subvol->fops->getxattr, loc,
                                    GF_XATTR_PATHINFO_KEY, NULL);
                        return 0;
                }
                op_errno = ENODATA;
                goto err;
        }

        if (key && (!strcmp (GF_XATTR_MARKER_KEY, key))
            && (GF_CLIENT_PID_GSYNCD == frame->root->pid)) {
                if (DHT_IS_DIR(layout)) {
                        cnt = layout->cnt;
                } else {
                        cnt = 1;
                }

                sub_volumes = alloca ( cnt * sizeof (xlator_t *));
                for (i = 0; i < cnt; i++)
                        *(sub_volumes + i) = layout->list[i].xlator;

                if (cluster_getmarkerattr (frame, this, loc, key,
                                           local, dht_getxattr_unwind,
                                           sub_volumes, cnt,
                                           MARKER_UUID_TYPE, marker_uuid_default_gauge,
                                           conf->vol_uuid)) {
                        op_errno = EINVAL;
                        goto err;
                }

                return 0;
        }

        if (key && !strcmp (GF_XATTR_QUOTA_LIMIT_LIST, key)) {
                /* quota hardlimit and aggregated size of a directory is stored
                 * in inode contexts of each brick. Hence its good enough that
                 * we send getxattr for this key to any brick.
                 */
                local->call_cnt = 1;
                subvol = dht_first_up_subvol (this);
                STACK_WIND (frame, dht_getxattr_cbk, subvol,
                            subvol->fops->getxattr, loc, key, xdata);
                return 0;
        }

        if (key && *conf->vol_uuid) {
                if ((match_uuid_local (key, conf->vol_uuid) == 0) &&
                    (GF_CLIENT_PID_GSYNCD == frame->root->pid)) {
                        if (DHT_IS_DIR(layout)) {
                                cnt = layout->cnt;
                        } else {
                                cnt = 1;
                        }
                        sub_volumes = alloca ( cnt * sizeof (xlator_t *));
                        for (i = 0; i < cnt; i++)
                                sub_volumes[i] = layout->list[i].xlator;

                        if (cluster_getmarkerattr (frame, this, loc, key,
                                                   local, dht_getxattr_unwind,
                                                   sub_volumes, cnt,
                                                   MARKER_XTIME_TYPE,
                                                   marker_xtime_default_gauge,
                                                   conf->vol_uuid)) {
                                op_errno = EINVAL;
                                goto err;
                        }

                        return 0;
                }
        }

        if (DHT_IS_DIR(layout)) {
                cnt = local->call_cnt = layout->cnt;
        } else {
                cnt = local->call_cnt  = 1;
        }

        for (i = 0; i < cnt; i++) {
                subvol = layout->list[i].xlator;
                STACK_WIND (frame, dht_getxattr_cbk,
                            subvol, subvol->fops->getxattr,
                            loc, key, NULL);
        }
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (getxattr, frame, -1, op_errno, NULL, NULL);

        return 0;
}
#undef DHT_IS_DIR

int
dht_fgetxattr (call_frame_t *frame, xlator_t *this,
               fd_t *fd, const char *key, dict_t *xdata)
{
        xlator_t     *subvol        = NULL;
        dht_local_t  *local         = NULL;
        dht_layout_t *layout        = NULL;
        int           op_errno      = -1;
        int           i             = 0;
        int           cnt           = 0;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);
        VALIDATE_OR_GOTO (this->private, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_FGETXATTR);
        if (!local) {
                op_errno = ENOMEM;

                goto err;
        }

        layout = local->layout;
        if (!layout) {
                gf_log (this->name, GF_LOG_ERROR,
                        "layout is NULL");
                op_errno = ENOENT;
                goto err;
        }

        if (key) {
                local->key = gf_strdup (key);
                if (!local->key) {
                        op_errno = ENOMEM;
                        goto err;
                }
        }

        if ((fd->inode->ia_type == IA_IFDIR)
	    && key
            && (strncmp (key, GF_XATTR_LOCKINFO_KEY,
                         strlen (GF_XATTR_LOCKINFO_KEY) != 0))) {
                cnt = local->call_cnt = layout->cnt;
        } else {
                cnt = local->call_cnt  = 1;
        }

        for (i = 0; i < cnt; i++) {
                subvol = layout->list[i].xlator;
                STACK_WIND (frame, dht_getxattr_cbk,
                            subvol, subvol->fops->fgetxattr,
                            fd, key, NULL);
        }
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (fgetxattr, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int
dht_fsetxattr (call_frame_t *frame, xlator_t *this,
               fd_t *fd, dict_t *xattr, int flags, dict_t *xdata)
{
        xlator_t     *subvol   = NULL;
        dht_local_t  *local    = NULL;
        int           op_errno = EINVAL;
        dht_conf_t   *conf     = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);
        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;

        GF_IF_INTERNAL_XATTR_GOTO (conf->wild_xattr_name, xattr,
                                   op_errno, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_FSETXATTR);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        local->call_cnt = 1;

        STACK_WIND (frame, dht_err_cbk, subvol, subvol->fops->fsetxattr,
                    fd, xattr, flags, NULL);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (fsetxattr, frame, -1, op_errno, NULL);

        return 0;
}


static int
dht_common_setxattr_cbk (call_frame_t *frame, void *cookie,
                         xlator_t *this, int32_t op_ret, int32_t op_errno,
                         dict_t *xdata)
{
        DHT_STACK_UNWIND (setxattr, frame, op_ret, op_errno, xdata);

        return 0;
}

int
dht_checking_pathinfo_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int op_ret, int op_errno, dict_t *xattr,
                           dict_t *xdata)
{
        int           i     = -1;
        int           ret   = -1;
        char         *value = NULL;
        dht_local_t  *local = NULL;
        dht_conf_t   *conf  = NULL;
        call_frame_t *prev  = NULL;
        int           this_call_cnt = 0;

        local = frame->local;
        prev = cookie;
        conf = this->private;

        if (op_ret == -1)
                goto out;


        ret = dict_get_str (xattr, GF_XATTR_PATHINFO_KEY, &value);
        if (ret)
                goto out;

        if (!strcmp (value, local->key)) {
                for (i = 0; i < conf->subvolume_cnt; i++) {
                        if (conf->subvolumes[i] == prev->this)
                                conf->decommissioned_bricks[i] = prev->this;
                }
        }

out:
        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
                DHT_STACK_UNWIND (setxattr, frame, local->op_ret, ENOTSUP, NULL);
        }
        return 0;

}

int
dht_setxattr (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *xattr, int flags, dict_t *xdata)
{
        xlator_t     *subvol   = NULL;
        dht_local_t  *local    = NULL;
        dht_conf_t   *conf     = NULL;
        dht_layout_t *layout   = NULL;
        int           i        = 0;
        int           op_errno = EINVAL;
        int           ret      = -1;
        data_t       *tmp      = NULL;
        uint32_t      dir_spread = 0;
        char          value[4096] = {0,};
        gf_dht_migrate_data_type_t forced_rebalance = GF_DHT_MIGRATE_DATA;
        int           call_cnt = 0;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        conf   = this->private;

        GF_IF_INTERNAL_XATTR_GOTO (conf->wild_xattr_name, xattr,
                                   op_errno, err);

        local = dht_local_init (frame, loc, NULL, GF_FOP_SETXATTR);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        layout = local->layout;
        if (!layout) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no layout for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        local->call_cnt = call_cnt = layout->cnt;

        tmp = dict_get (xattr, "distribute.migrate-data");
        if (tmp) {
                if (IA_ISDIR (loc->inode->ia_type)) {
                        op_errno = ENOTSUP;
                        goto err;
                }

                /* TODO: need to interpret the 'value' for more meaning
                   (ie, 'target' subvolume given there, etc) */
                memcpy (value, tmp->data, tmp->len);
                if (strcmp (value, "force") == 0)
                       forced_rebalance =
                                GF_DHT_MIGRATE_DATA_EVEN_IF_LINK_EXISTS;

                if (conf->decommission_in_progress)
                        forced_rebalance = GF_DHT_MIGRATE_HARDLINK;

                local->rebalance.target_node = dht_subvol_get_hashed (this, loc);
                if (!local->rebalance.target_node) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to get "
                                "hashed subvol for %s", loc->path);
                        op_errno = EINVAL;
                        goto err;
                }

                local->rebalance.from_subvol = local->cached_subvol;

                if (local->rebalance.target_node == local->rebalance.from_subvol) {
                        op_errno = EEXIST;
                        goto err;
                }
                if (local->rebalance.target_node) {
                        local->flags = forced_rebalance;

                        ret = dht_start_rebalance_task (this, frame);
                        if (!ret)
                                return 0;

                        gf_log (this->name, GF_LOG_ERROR,
                                "%s: failed to create a new synctask",
                                loc->path);
                }
                op_errno = EINVAL;
                goto err;

        }

        tmp = dict_get (xattr, "decommission-brick");
        if (tmp) {
                /* This operation should happen only on '/' */
                if (!__is_root_gfid (loc->inode->gfid)) {
                        op_errno = ENOTSUP;
                        goto err;
                }

                memcpy (value, tmp->data, ((tmp->len < 4095) ? tmp->len : 4095));
                local->key = gf_strdup (value);
                local->call_cnt = conf->subvolume_cnt;

                for (i = 0 ; i < conf->subvolume_cnt; i++) {
                        /* Get the pathinfo, and then compare */
                        STACK_WIND (frame, dht_checking_pathinfo_cbk,
                                    conf->subvolumes[i],
                                    conf->subvolumes[i]->fops->getxattr,
                                    loc, GF_XATTR_PATHINFO_KEY, NULL);
                }
                return 0;
        }

        tmp = dict_get (xattr, GF_XATTR_FIX_LAYOUT_KEY);
        if (tmp) {
                gf_log (this->name, GF_LOG_INFO,
                        "fixing the layout of %s", loc->path);

                ret = dht_fix_directory_layout (frame, dht_common_setxattr_cbk,
                                                layout);
                if (ret) {
                        op_errno = ENOTCONN;
                        goto err;
                }
                return ret;
        }

        tmp = dict_get (xattr, "distribute.directory-spread-count");
        if (tmp) {
                /* Setxattr value is packed as 'binary', not string */
                memcpy (value, tmp->data, ((tmp->len < 4095)?tmp->len:4095));
                ret = gf_string2uint32 (value, &dir_spread);
                if (!ret && ((dir_spread <= conf->subvolume_cnt) &&
                             (dir_spread > 0))) {
                        layout->spread_cnt = dir_spread;

                        ret = dht_fix_directory_layout (frame,
                                                        dht_common_setxattr_cbk,
                                                        layout);
                        if (ret) {
                                op_errno = ENOTCONN;
                                goto err;
                        }
                        return ret;
                }
                gf_log (this->name, GF_LOG_ERROR,
                        "wrong 'directory-spread-count' value (%s)", value);
                op_errno = ENOTSUP;
                goto err;
        }

        for (i = 0; i < call_cnt; i++) {
                STACK_WIND (frame, dht_err_cbk,
                            layout->list[i].xlator,
                            layout->list[i].xlator->fops->setxattr,
                            loc, xattr, flags, xdata);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (setxattr, frame, -1, op_errno, NULL);

        return 0;
}


int
dht_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev = NULL;

        local = frame->local;
        prev = cookie;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "subvolume %s returned -1 (%s)",
                                prev->this->name, strerror (op_errno));
                        goto unlock;
                }

                local->op_ret = 0;
        }
unlock:
        UNLOCK (&frame->lock);

        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
                DHT_STACK_UNWIND (removexattr, frame, local->op_ret,
                                  local->op_errno, NULL);
        }

        return 0;
}


int
dht_removexattr (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, const char *key, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;
        dht_layout_t *layout = NULL;
        int           call_cnt = 0;
        dht_conf_t   *conf = NULL;

        int i;

        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;

        GF_IF_NATIVE_XATTR_GOTO (conf->wild_xattr_name, key, op_errno, err);

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        local = dht_local_init (frame, loc, NULL, GF_FOP_REMOVEXATTR);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        layout = local->layout;
        if (!local->layout) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no layout for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        local->call_cnt = call_cnt = layout->cnt;
        local->key = gf_strdup (key);

        for (i = 0; i < call_cnt; i++) {
                STACK_WIND (frame, dht_removexattr_cbk,
                            layout->list[i].xlator,
                            layout->list[i].xlator->fops->removexattr,
                            loc, key, NULL);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (removexattr, frame, -1, op_errno, NULL);

        return 0;
}

int
dht_fremovexattr (call_frame_t *frame, xlator_t *this,
                  fd_t *fd, const char *key, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;
        dht_layout_t *layout = NULL;
        int           call_cnt = 0;
        dht_conf_t   *conf = 0;

        int i;

        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;

        GF_IF_NATIVE_XATTR_GOTO (conf->wild_xattr_name, key, op_errno, err);

        VALIDATE_OR_GOTO (frame, err);

        local = dht_local_init (frame, NULL, fd, GF_FOP_FREMOVEXATTR);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for inode=%s",
                        uuid_utoa (fd->inode->gfid));
                op_errno = EINVAL;
                goto err;
        }

        layout = local->layout;
        if (!local->layout) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no layout for inode=%s", uuid_utoa (fd->inode->gfid));
                op_errno = EINVAL;
                goto err;
        }

        local->call_cnt = call_cnt = layout->cnt;
        local->key = gf_strdup (key);

        for (i = 0; i < call_cnt; i++) {
                STACK_WIND (frame, dht_removexattr_cbk,
                            layout->list[i].xlator,
                            layout->list[i].xlator->fops->fremovexattr,
                            fd, key, NULL);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (fremovexattr, frame, -1, op_errno, NULL);

        return 0;
}


int
dht_fd_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
            int op_ret, int op_errno, fd_t *fd, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev = NULL;

        local = frame->local;
        prev = cookie;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "subvolume %s returned -1 (%s)",
                                prev->this->name, strerror (op_errno));
                        goto unlock;
                }

                local->op_ret = 0;
        }
unlock:
        UNLOCK (&frame->lock);

        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt))
                DHT_STACK_UNWIND (open, frame, local->op_ret, local->op_errno,
                                  local->fd, NULL);

        return 0;
}

/*
 * dht_normalize_stats -
 */
static void
dht_normalize_stats (struct statvfs *buf, unsigned long bsize,
                     unsigned long frsize)
{
        double factor = 0;

        if (buf->f_bsize != bsize) {
                buf->f_bsize = bsize;
        }

        if (buf->f_frsize != frsize) {
                factor = ((double) buf->f_frsize) / frsize;
                buf->f_frsize = frsize;
                buf->f_blocks = (fsblkcnt_t) (factor * buf->f_blocks);
                buf->f_bfree  = (fsblkcnt_t) (factor * buf->f_bfree);
                buf->f_bavail = (fsblkcnt_t) (factor * buf->f_bavail);

        }
}

int
dht_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, struct statvfs *statvfs, dict_t *xdata)
{
        dht_local_t *local              = NULL;
        int          this_call_cnt      = 0;
        int          bsize              = 0;
        int          frsize             = 0;
        int8_t       quota_deem_statfs  = 0;
        GF_UNUSED int     ret           = 0;
        unsigned long     new_usage     = 0;
        unsigned long     cur_usage     = 0;

        ret = dict_get_int8 (xdata, "quota-deem-statfs", &quota_deem_statfs);

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        goto unlock;
                }
                if (!statvfs) {
                        op_errno = EINVAL;
                        local->op_ret = -1;
                        goto unlock;
                }
                local->op_ret = 0;

                if (quota_deem_statfs) {
                        new_usage = statvfs->f_blocks - statvfs->f_bfree;
                        cur_usage = local->statvfs.f_blocks - local->statvfs.f_bfree;
                        /* We take the maximux of the usage from the subvols */
                        if (new_usage >= cur_usage)
                                local->statvfs = *statvfs;
                        goto unlock;
                }

                if (local->statvfs.f_bsize != 0) {
                        bsize = max(local->statvfs.f_bsize, statvfs->f_bsize);
                        frsize = max(local->statvfs.f_frsize, statvfs->f_frsize);
                        dht_normalize_stats(&local->statvfs, bsize, frsize);
                        dht_normalize_stats(statvfs, bsize, frsize);
                } else {
                        local->statvfs.f_bsize    = statvfs->f_bsize;
                        local->statvfs.f_frsize   = statvfs->f_frsize;
                }

                local->statvfs.f_blocks  += statvfs->f_blocks;
                local->statvfs.f_bfree   += statvfs->f_bfree;
                local->statvfs.f_bavail  += statvfs->f_bavail;
                local->statvfs.f_files   += statvfs->f_files;
                local->statvfs.f_ffree   += statvfs->f_ffree;
                local->statvfs.f_favail  += statvfs->f_favail;
                local->statvfs.f_fsid     = statvfs->f_fsid;
                local->statvfs.f_flag     = statvfs->f_flag;
                local->statvfs.f_namemax  = statvfs->f_namemax;


        }
unlock:
        UNLOCK (&frame->lock);


        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt))
                DHT_STACK_UNWIND (statfs, frame, local->op_ret, local->op_errno,
                                  &local->statvfs, xdata);

        return 0;
}


int
dht_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        dht_local_t  *local  = NULL;
        dht_conf_t   *conf = NULL;
        int           op_errno = -1;
        int           i = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;

        local = dht_local_init (frame, NULL, NULL, GF_FOP_STATFS);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        if (IA_ISDIR (loc->inode->ia_type)) {
                local->call_cnt = conf->subvolume_cnt;

                for (i = 0; i < conf->subvolume_cnt; i++) {
                        STACK_WIND (frame, dht_statfs_cbk,
                                    conf->subvolumes[i],
                                    conf->subvolumes[i]->fops->statfs, loc,
                                    xdata);
                }
                return 0;
        }

        subvol = dht_subvol_get_cached (this, loc->inode);
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        local->call_cnt = 1;

        STACK_WIND (frame, dht_statfs_cbk,
                    subvol, subvol->fops->statfs, loc, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (statfs, frame, -1, op_errno, NULL, NULL);

        return 0;
}


int
dht_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
             dict_t *xdata)
{
        dht_local_t  *local  = NULL;
        dht_conf_t   *conf = NULL;
        int           op_errno = -1;
        int           i = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;

        local = dht_local_init (frame, loc, fd, GF_FOP_OPENDIR);
        if (!local) {
                op_errno = ENOMEM;

                goto err;
        }

        local->call_cnt = conf->subvolume_cnt;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                STACK_WIND (frame, dht_fd_cbk,
                            conf->subvolumes[i],
                            conf->subvolumes[i]->fops->opendir,
                            loc, fd, xdata);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (opendir, frame, -1, op_errno, NULL, NULL);

        return 0;
}


int
dht_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                  int op_errno, gf_dirent_t *orig_entries, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        gf_dirent_t   entries;
        gf_dirent_t  *orig_entry = NULL;
        gf_dirent_t  *entry = NULL;
        call_frame_t *prev = NULL;
        xlator_t     *next_subvol = NULL;
        off_t         next_offset = 0;
        int           count = 0;
        dht_layout_t *layout = 0;
        dht_conf_t   *conf   = NULL;
        xlator_t     *subvol = 0;
        int           ret    = 0;

        INIT_LIST_HEAD (&entries.list);
        prev = cookie;
        local = frame->local;
        conf  = this->private;

        if (op_ret < 0)
                goto done;

        if (!local->layout)
                local->layout = dht_layout_get (this, local->fd->inode);

        layout = local->layout;

        list_for_each_entry (orig_entry, (&orig_entries->list), list) {
                next_offset = orig_entry->d_off;
                if (check_is_dir (NULL, (&orig_entry->d_stat), NULL) &&
                    (prev->this != local->first_up_subvol)) {
                        continue;
                }
                if (check_is_linkfile (NULL, (&orig_entry->d_stat),
                                       orig_entry->dict,
                                       conf->link_xattr_name)) {
                        continue;
                }

                entry = gf_dirent_for_name (orig_entry->d_name);
                if (!entry) {

                        goto unwind;
                }

                /* Do this if conf->search_unhashed is set to "auto" */
                if (conf->search_unhashed == GF_DHT_LOOKUP_UNHASHED_AUTO) {
                        subvol = dht_layout_search (this, layout,
                                                    orig_entry->d_name);
                        if (!subvol || (subvol != prev->this)) {
                                /* TODO: Count the number of entries which need
                                   linkfile to prove its existence in fs */
                                layout->search_unhashed++;
                        }
                }

                dht_itransform (this, prev->this, orig_entry->d_off,
                                &entry->d_off);

                entry->d_stat = orig_entry->d_stat;
                entry->d_ino  = orig_entry->d_ino;
                entry->d_type = orig_entry->d_type;
                entry->d_len  = orig_entry->d_len;

                if (orig_entry->dict)
                        entry->dict = dict_ref (orig_entry->dict);

                /* making sure we set the inode ctx right with layout,
                   currently possible only for non-directories, so for
                   directories don't set entry inodes */
                if (!IA_ISDIR(entry->d_stat.ia_type)) {
                        ret = dht_layout_preset (this, prev->this,
                                                 orig_entry->inode);
                        if (ret)
                                gf_log (this->name, GF_LOG_WARNING,
                                        "failed to link the layout in inode");
                        entry->inode = inode_ref (orig_entry->inode);
                } else if (orig_entry->inode) {
                        dht_inode_ctx_time_update (orig_entry->inode, this,
                                                   &entry->d_stat, 1);
                }

                list_add_tail (&entry->list, &entries.list);
                count++;
        }
        op_ret = count;
        /* We need to ensure that only the last subvolume's end-of-directory
         * notification is respected so that directory reading does not stop
         * before all subvolumes have been read. That could happen because the
         * posix for each subvolume sends a ENOENT on end-of-directory but in
         * distribute we're not concerned only with a posix's view of the
         * directory but the aggregated namespace' view of the directory.
         */
        if (prev->this != dht_last_up_subvol (this))
                op_errno = 0;

done:
        if (count == 0) {
                /* non-zero next_offset means that
                   EOF is not yet hit on the current subvol
                */
                if (next_offset == 0) {
                        next_subvol = dht_subvol_next (this, prev->this);
                } else {
                        next_subvol = prev->this;
                }

                if (!next_subvol) {
                        goto unwind;
                }

		if (conf->readdir_optimize == _gf_true) {
                        if (next_subvol != local->first_up_subvol) {
                                ret = dict_set_int32 (local->xattr,
                                                      GF_READDIR_SKIP_DIRS, 1);
                                if (ret)
                                        gf_log (this->name, GF_LOG_ERROR,
					         "dict set failed");
		        } else {
                                 dict_del (local->xattr,
                                           GF_READDIR_SKIP_DIRS);
                        }
                }

                STACK_WIND (frame, dht_readdirp_cbk,
                            next_subvol, next_subvol->fops->readdirp,
                            local->fd, local->size, next_offset,
                            local->xattr);
                return 0;
        }

unwind:
        if (op_ret < 0)
                op_ret = 0;

        DHT_STACK_UNWIND (readdirp, frame, op_ret, op_errno, &entries, NULL);

        gf_dirent_free (&entries);

        return 0;
}



int
dht_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int op_ret, int op_errno, gf_dirent_t *orig_entries,
                 dict_t *xdata)
{
        dht_local_t  *local = NULL;
        gf_dirent_t   entries;
        gf_dirent_t  *orig_entry = NULL;
        gf_dirent_t  *entry = NULL;
        call_frame_t *prev = NULL;
        xlator_t     *next_subvol = NULL;
        off_t         next_offset = 0;
        int           count = 0;
        dht_layout_t *layout = 0;
        xlator_t     *subvol = 0;

        INIT_LIST_HEAD (&entries.list);
        prev = cookie;
        local = frame->local;

        if (op_ret < 0)
                goto done;

        if (!local->layout)
                local->layout = dht_layout_get (this, local->fd->inode);

        layout = local->layout;

        list_for_each_entry (orig_entry, (&orig_entries->list), list) {
                next_offset = orig_entry->d_off;

                subvol = dht_layout_search (this, layout, orig_entry->d_name);

                if (!subvol || (subvol == prev->this)) {
                        entry = gf_dirent_for_name (orig_entry->d_name);
                        if (!entry) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "memory allocation failed :(");
                                goto unwind;
                        }

                        dht_itransform (this, prev->this, orig_entry->d_off,
                                        &entry->d_off);

                        entry->d_ino  = orig_entry->d_ino;
                        entry->d_type = orig_entry->d_type;
                        entry->d_len  = orig_entry->d_len;

                        list_add_tail (&entry->list, &entries.list);
                        count++;
                }
        }
        op_ret = count;
        /* We need to ensure that only the last subvolume's end-of-directory
         * notification is respected so that directory reading does not stop
         * before all subvolumes have been read. That could happen because the
         * posix for each subvolume sends a ENOENT on end-of-directory but in
         * distribute we're not concerned only with a posix's view of the
         * directory but the aggregated namespace' view of the directory.
         */
        if (prev->this != dht_last_up_subvol (this))
                op_errno = 0;

done:
        if (count == 0) {
                /* non-zero next_offset means that
                   EOF is not yet hit on the current subvol
                */
                if (next_offset == 0) {
                        next_subvol = dht_subvol_next (this, prev->this);
                } else {
                        next_subvol = prev->this;
                }

                if (!next_subvol) {
                        goto unwind;
                }

                STACK_WIND (frame, dht_readdir_cbk,
                            next_subvol, next_subvol->fops->readdir,
                            local->fd, local->size, next_offset, NULL);
                return 0;
        }

unwind:
        if (op_ret < 0)
                op_ret = 0;

        DHT_STACK_UNWIND (readdir, frame, op_ret, op_errno, &entries, NULL);

        gf_dirent_free (&entries);

        return 0;
}


int
dht_do_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t yoff, int whichop, dict_t *dict)
{
        dht_local_t  *local  = NULL;
        int           op_errno = -1;
        xlator_t     *xvol = NULL;
        off_t         xoff = 0;
        int           ret = 0;
        dht_conf_t   *conf = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;

        local = dht_local_init (frame, NULL, NULL, whichop);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->fd = fd_ref (fd);
        local->size = size;
        local->xattr_req = (dict)? dict_ref (dict) : NULL;
        local->first_up_subvol = dht_first_up_subvol (this);

        dht_deitransform (this, yoff, &xvol, (uint64_t *)&xoff);

        /* TODO: do proper readdir */
        if (whichop == GF_FOP_READDIRP) {
                if (dict)
                        local->xattr = dict_ref (dict);
                else
                        local->xattr = dict_new ();

                if (local->xattr) {
                        ret = dict_set_uint32 (local->xattr,
                                               conf->link_xattr_name, 256);
                        if (ret)
                                gf_log (this->name, GF_LOG_WARNING,
                                        "failed to set '%s' key",
                                        conf->link_xattr_name);
			if (conf->readdir_optimize == _gf_true) {
                                if (xvol != local->first_up_subvol) {
				        ret = dict_set_int32 (local->xattr,
			                               GF_READDIR_SKIP_DIRS, 1);
				        if (ret)
					        gf_log (this->name,
                                                        GF_LOG_ERROR,
						        "Dict set failed");
                                } else {
                                        dict_del (local->xattr,
                                                  GF_READDIR_SKIP_DIRS);
                                }
			}
                }

                STACK_WIND (frame, dht_readdirp_cbk, xvol, xvol->fops->readdirp,
                            fd, size, xoff, local->xattr);
        } else {
                STACK_WIND (frame, dht_readdir_cbk, xvol, xvol->fops->readdir,
                            fd, size, xoff, local->xattr);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (readdir, frame, -1, op_errno, NULL, NULL);

        return 0;
}


int
dht_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t yoff, dict_t *xdata)
{
        int          op = GF_FOP_READDIR;
        dht_conf_t  *conf = NULL;
        int          i = 0;

        conf = this->private;
        if (!conf)
                goto out;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (!conf->subvolume_status[i]) {
                        op = GF_FOP_READDIRP;
                        break;
                }
        }

        if (conf->use_readdirp)
                op = GF_FOP_READDIRP;

out:
        dht_do_readdir (frame, this, fd, size, yoff, op, 0);
        return 0;
}

int
dht_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t yoff, dict_t *dict)
{
        dht_do_readdir (frame, this, fd, size, yoff, GF_FOP_READDIRP, dict);
        return 0;
}



int
dht_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = 0;


        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == -1)
                        local->op_errno = op_errno;

                if (op_ret == 0)
                        local->op_ret = 0;
        }
        UNLOCK (&frame->lock);

        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt))
                DHT_STACK_UNWIND (fsyncdir, frame, local->op_ret,
                                  local->op_errno, xdata);

        return 0;
}


int
dht_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
              int datasync, dict_t *xdata)
{
        dht_local_t  *local  = NULL;
        dht_conf_t   *conf = NULL;
        int           op_errno = -1;
        int           i = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;

        local = dht_local_init (frame, NULL, NULL, GF_FOP_FSYNCDIR);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->fd = fd_ref (fd);
        local->call_cnt = conf->subvolume_cnt;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                STACK_WIND (frame, dht_fsyncdir_cbk,
                            conf->subvolumes[i],
                            conf->subvolumes[i]->fops->fsyncdir,
                            fd, datasync, xdata);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (fsyncdir, frame, -1, op_errno, NULL);

        return 0;
}


int
dht_newfile_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int op_ret, int op_errno,
                 inode_t *inode, struct iatt *stbuf, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
        xlator_t     *prev = NULL;
        int           ret = -1;
        dht_local_t  *local = NULL;


        if (op_ret == -1)
                goto out;

        local = frame->local;
        if (!local) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        prev = cookie;

        if (local->loc.parent) {

                dht_inode_ctx_time_update (local->loc.parent, this,
                                           preparent, 0);
                dht_inode_ctx_time_update (local->loc.parent, this,
                                           postparent, 1);
        }

        ret = dht_layout_preset (this, prev, inode);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "could not set pre-set layout for subvolume %s",
                        prev? prev->name: NULL);
                op_ret   = -1;
                op_errno = EINVAL;
                goto out;
        }
        if (local->linked == _gf_true)
                dht_linkfile_attr_heal (frame, this);
out:
        /*
         * FIXME: ia_size and st_blocks of preparent and postparent do not have
         * correct values. since, preparent and postparent buffers correspond
         * to a directory these two members should have values equal to sum of
         * corresponding values from each of the subvolume.
         * See dht_iatt_merge for reference.
         */
        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (mknod, frame, op_ret, op_errno, inode, stbuf,
                          preparent, postparent, xdata);
        return 0;
}

int
dht_mknod_linkfile_create_cbk (call_frame_t *frame, void *cookie,
                               xlator_t *this,
                               int32_t op_ret, int32_t op_errno,
                               inode_t *inode, struct iatt *stbuf,
                               struct iatt *preparent, struct iatt *postparent,
                               dict_t *xdata)
{
        dht_local_t  *local = NULL;
        xlator_t     *cached_subvol = NULL;

        if (op_ret == -1)
                goto err;

        local = frame->local;
        if (!local || !local->cached_subvol) {
                op_errno = EINVAL;
                goto err;
        }

        cached_subvol = local->cached_subvol;

        STACK_WIND_COOKIE (frame, dht_newfile_cbk, (void *)cached_subvol,
                           cached_subvol, cached_subvol->fops->mknod,
                           &local->loc, local->mode, local->rdev, local->umask,
                           local->params);

        return 0;
err:
        DHT_STACK_UNWIND (mknod, frame, -1, op_errno, NULL, NULL, NULL, NULL,
                          NULL);
        return 0;
}

int
dht_mknod (call_frame_t *frame, xlator_t *this,
           loc_t *loc, mode_t mode, dev_t rdev, mode_t umask, dict_t *params)
{
        xlator_t    *subvol = NULL;
        int          op_errno = -1;
        xlator_t    *avail_subvol = NULL;
        dht_local_t *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);

        dht_get_du_info (frame, this, loc);

        local = dht_local_init (frame, loc, NULL, GF_FOP_MKNOD);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = dht_subvol_get_hashed (this, loc);
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no subvolume in layout for path=%s",
                        loc->path);
                op_errno = ENOENT;
                goto err;
        }

        if (!dht_is_subvol_filled (this, subvol)) {
                gf_log (this->name, GF_LOG_TRACE,
                        "creating %s on %s", loc->path, subvol->name);

                STACK_WIND_COOKIE (frame, dht_newfile_cbk, (void *)subvol,
                                   subvol, subvol->fops->mknod, loc, mode,
                                   rdev, umask, params);
        } else {

                avail_subvol = dht_free_disk_available_subvol (this, subvol,
                                                               local);
                if (avail_subvol != subvol) {
                        /* Choose the minimum filled volume, and create the
                           files there */

                        local->params = dict_ref (params);
                        local->cached_subvol = avail_subvol;
                        local->mode = mode;
                        local->rdev = rdev;
                        local->umask = umask;
                        dht_linkfile_create (frame,
                                             dht_mknod_linkfile_create_cbk,
                                             this, avail_subvol, subvol, loc);
                } else {
                        gf_log (this->name, GF_LOG_TRACE,
                                "creating %s on %s", loc->path, subvol->name);

                        STACK_WIND_COOKIE (frame, dht_newfile_cbk,
                                           (void *)subvol, subvol,
                                           subvol->fops->mknod, loc, mode,
                                           rdev, umask, params);
                }
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (mknod, frame, -1, op_errno,
                          NULL, NULL, NULL, NULL, NULL);

        return 0;
}


int
dht_symlink (call_frame_t *frame, xlator_t *this,
             const char *linkname, loc_t *loc, mode_t umask, dict_t *params)
{
        xlator_t    *subvol = NULL;
        int          op_errno = -1;
        dht_local_t *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);

        local = dht_local_init (frame, loc, NULL, GF_FOP_SYMLINK);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = dht_subvol_get_hashed (this, loc);
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no subvolume in layout for path=%s",
                        loc->path);
                op_errno = ENOENT;
                goto err;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "creating %s on %s", loc->path, subvol->name);

        STACK_WIND_COOKIE (frame, dht_newfile_cbk, (void *)subvol, subvol,
                           subvol->fops->symlink, linkname, loc, umask,
                           params);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (link, frame, -1, op_errno,
                          NULL, NULL, NULL, NULL, NULL);

        return 0;
}


int
dht_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
            dict_t *xdata)
{
        xlator_t    *cached_subvol = NULL;
        xlator_t    *hashed_subvol = NULL;
        int          op_errno = -1;
        dht_local_t *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);

        if (dht_filter_loc_subvol_key (this, loc, &local->loc,
                                       &cached_subvol)) {
                gf_log (this->name, GF_LOG_INFO,
                        "unlinking %s on %s (given path %s)",
                        local->loc.path, cached_subvol->name, loc->path);
                STACK_WIND (frame, dht_unlink_cbk,
                            cached_subvol, cached_subvol->fops->unlink,
                            &local->loc, xflag, xdata);
                goto done;
        }

        local = dht_local_init (frame, loc, NULL, GF_FOP_UNLINK);
        if (!local) {
                op_errno = ENOMEM;

                goto err;
        }

        hashed_subvol = dht_subvol_get_hashed (this, loc);
        if (!hashed_subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no subvolume in layout for path=%s",
                        loc->path);
                op_errno = EINVAL;
                goto err;
        }

        cached_subvol = local->cached_subvol;
        if (!cached_subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        local->flags = xflag;
        if (hashed_subvol != cached_subvol) {
                STACK_WIND (frame, dht_unlink_linkfile_cbk,
                            hashed_subvol, hashed_subvol->fops->unlink, loc,
                            xflag, xdata);
        } else {
                STACK_WIND (frame, dht_unlink_cbk,
                            cached_subvol, cached_subvol->fops->unlink, loc,
                            xflag, xdata);
        }
done:
        return 0;
err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}


int
dht_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int op_ret, int op_errno,
              inode_t *inode, struct iatt *stbuf, struct iatt *preparent,
              struct iatt *postparent, dict_t *xdata)
{
        call_frame_t *prev = NULL;
        dht_layout_t *layout = NULL;
        dht_local_t  *local = NULL;

        prev = cookie;

        local = frame->local;

        if (op_ret == -1)
                goto out;

        layout = dht_layout_for_subvol (this, prev->this);
        if (!layout) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no pre-set layout for subvolume %s",
                        prev->this->name);
                op_ret   = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (local->loc.parent) {
                dht_inode_ctx_time_update (local->loc.parent, this,
                                           preparent, 0);
                dht_inode_ctx_time_update (local->loc.parent, this,
                                           postparent, 1);
        }
        if (local->linked == _gf_true) {
                local->stbuf = *stbuf;
                dht_linkfile_attr_heal (frame, this);
        }
out:
        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (link, frame, op_ret, op_errno, inode, stbuf, preparent,
                          postparent, NULL);

        return 0;
}


int
dht_link_linkfile_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno,
                       inode_t *inode, struct iatt *stbuf,
                       struct iatt *preparent, struct iatt *postparent,
                       dict_t *xdata)
{
        dht_local_t  *local = NULL;
        xlator_t     *srcvol = NULL;

        if (op_ret == -1)
                goto err;

        local = frame->local;
        srcvol = local->linkfile.srcvol;

        STACK_WIND (frame, dht_link_cbk, srcvol, srcvol->fops->link,
                    &local->loc, &local->loc2, xdata);

        return 0;

err:
        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (link, frame, op_ret, op_errno, inode, stbuf, preparent,
                          postparent, NULL);

        return 0;
}


int
dht_link (call_frame_t *frame, xlator_t *this,
          loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        xlator_t    *cached_subvol = NULL;
        xlator_t    *hashed_subvol = NULL;
        int          op_errno = -1;
        int          ret = -1;
        dht_local_t *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (oldloc, err);
        VALIDATE_OR_GOTO (newloc, err);

        local = dht_local_init (frame, oldloc, NULL, GF_FOP_LINK);
        if (!local) {
                op_errno = ENOMEM;

                goto err;
        }

        cached_subvol = local->cached_subvol;
        if (!cached_subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for path=%s", oldloc->path);
                op_errno = EINVAL;
                goto err;
        }

        hashed_subvol = dht_subvol_get_hashed (this, newloc);
        if (!hashed_subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no subvolume in layout for path=%s",
                        newloc->path);
                op_errno = EINVAL;
                goto err;
        }

        ret = loc_copy (&local->loc2, newloc);
        if (ret == -1) {
                op_errno = ENOMEM;
                goto err;
        }

        if (hashed_subvol != cached_subvol) {
                uuid_copy (local->gfid, oldloc->inode->gfid);
                dht_linkfile_create (frame, dht_link_linkfile_cbk, this,
                                     cached_subvol, hashed_subvol, newloc);
        } else {
                STACK_WIND (frame, dht_link_cbk,
                            cached_subvol, cached_subvol->fops->link,
                            oldloc, newloc, xdata);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (link, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);

        return 0;
}


int
dht_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno,
                fd_t *fd, inode_t *inode, struct iatt *stbuf,
                struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        call_frame_t *prev = NULL;
        int           ret = -1;
        dht_local_t  *local = NULL;

        if (op_ret == -1)
                goto out;

        local = frame->local;
        if (!local) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        prev = cookie;

        if (local->loc.parent) {
                dht_inode_ctx_time_update (local->loc.parent, this,
                                           preparent, 0);

                dht_inode_ctx_time_update (local->loc.parent, this,
                                           postparent, 1);
        }

        ret = dht_layout_preset (this, prev->this, inode);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "could not set preset layout for subvol %s",
                        prev->this->name);
                op_ret   = -1;
                op_errno = EINVAL;
                goto out;
        }
        if (local->linked == _gf_true) {
                local->stbuf = *stbuf;
                dht_linkfile_attr_heal (frame, this);
        }
out:
        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (create, frame, op_ret, op_errno, fd, inode, stbuf, preparent,
                          postparent, NULL);
        return 0;
}


int
dht_create_linkfile_create_cbk (call_frame_t *frame, void *cookie,
                                xlator_t *this,
                                int32_t op_ret, int32_t op_errno,
                                inode_t *inode, struct iatt *stbuf,
                                struct iatt *preparent, struct iatt *postparent,
                                dict_t *xdata)
{
        dht_local_t  *local = NULL;
        xlator_t     *cached_subvol = NULL;

        if (op_ret == -1)
                goto err;

        local = frame->local;
        cached_subvol = local->cached_subvol;

        STACK_WIND (frame, dht_create_cbk,
                    cached_subvol, cached_subvol->fops->create,
                    &local->loc, local->flags, local->mode,
                    local->umask, local->fd, local->params);

        return 0;
err:
        DHT_STACK_UNWIND (create, frame, -1, op_errno, NULL, NULL, NULL,
                          NULL, NULL, NULL);
        return 0;
}

int
dht_create (call_frame_t *frame, xlator_t *this,
            loc_t *loc, int32_t flags, mode_t mode,
            mode_t umask, fd_t *fd, dict_t *params)
{
        int          op_errno = -1;
        xlator_t    *subvol = NULL;
        dht_local_t *local = NULL;
        xlator_t    *avail_subvol = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);

        dht_get_du_info (frame, this, loc);

        local = dht_local_init (frame, loc, fd, GF_FOP_CREATE);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        if (dht_filter_loc_subvol_key (this, loc, &local->loc,
                                       &subvol)) {
                gf_log (this->name, GF_LOG_INFO,
                        "creating %s on %s (got create on %s)",
                        local->loc.path, subvol->name, loc->path);
                STACK_WIND (frame, dht_create_cbk,
                            subvol, subvol->fops->create,
                            &local->loc, flags, mode, umask, fd, params);
                goto done;
        }

        subvol = dht_subvol_get_hashed (this, loc);
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no subvolume in layout for path=%s",
                        loc->path);
                op_errno = ENOENT;
                goto err;
        }

        if (!dht_is_subvol_filled (this, subvol)) {
                gf_log (this->name, GF_LOG_TRACE,
                        "creating %s on %s", loc->path, subvol->name);
                STACK_WIND (frame, dht_create_cbk,
                            subvol, subvol->fops->create,
                            loc, flags, mode, umask, fd, params);
                goto done;
        }
        /* Choose the minimum filled volume, and create the
           files there */
        avail_subvol = dht_free_disk_available_subvol (this, subvol, local);
        if (avail_subvol != subvol) {
                local->params = dict_ref (params);
                local->flags = flags;
                local->mode = mode;
                local->umask = umask;
                local->cached_subvol = avail_subvol;
                local->hashed_subvol = subvol;
                gf_log (this->name, GF_LOG_TRACE,
                        "creating %s on %s (link at %s)", loc->path,
                        avail_subvol->name, subvol->name);
                dht_linkfile_create (frame, dht_create_linkfile_create_cbk,
                                     this, avail_subvol, subvol, loc);
                goto done;
        }
        gf_log (this->name, GF_LOG_TRACE,
                "creating %s on %s", loc->path, subvol->name);
        STACK_WIND (frame, dht_create_cbk,
                    subvol, subvol->fops->create,
                    loc, flags, mode, umask, fd, params);
done:
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (create, frame, -1, op_errno, NULL, NULL, NULL,
                          NULL, NULL, NULL);

        return 0;
}


int
dht_mkdir_selfheal_cbk (call_frame_t *frame, void *cookie,
                        xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        dht_local_t   *local = NULL;
        dht_layout_t  *layout = NULL;

        local = frame->local;
        layout = local->selfheal.layout;

        if (op_ret == 0) {
                dht_layout_set (this, local->inode, layout);
                if (local->loc.parent) {
                        dht_inode_ctx_time_update (local->loc.parent, this,
                                                   &local->preparent, 0);

                        dht_inode_ctx_time_update (local->loc.parent, this,
                                                   &local->postparent, 1);
                }
        }

        DHT_STACK_UNWIND (mkdir, frame, op_ret, op_errno,
                          local->inode, &local->stbuf, &local->preparent,
                          &local->postparent, NULL);

        return 0;
}

int
dht_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int op_ret, int op_errno, inode_t *inode, struct iatt *stbuf,
               struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = 0;
        int           ret = -1;
        gf_boolean_t subvol_filled = _gf_false;
        call_frame_t *prev = NULL;
        dht_layout_t *layout = NULL;

        local = frame->local;
        prev  = cookie;
        layout = local->layout;

        subvol_filled = dht_is_subvol_filled (this, prev->this);

        LOCK (&frame->lock);
        {
                if (subvol_filled && (op_ret != -1)) {
                        ret = dht_layout_merge (this, layout, prev->this,
                                                -1, ENOSPC, NULL);
                } else {
			if (op_ret == -1 && op_errno == EEXIST)
				/* Very likely just a race between mkdir and
				   self-heal (from lookup of a concurrent mkdir
				   attempt).
				   Ignore error for now. layout setting will
				   anyways fail if this was a different (old)
				   pre-existing different directory.
				*/
				op_ret = 0;
                        ret = dht_layout_merge (this, layout, prev->this,
                                                op_ret, op_errno, NULL);
                }
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: failed to merge layouts", local->loc.path);

                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        goto unlock;
                }
                dht_iatt_merge (this, &local->stbuf, stbuf, prev->this);
                dht_iatt_merge (this, &local->preparent, preparent, prev->this);
                dht_iatt_merge (this, &local->postparent, postparent,
                                prev->this);
        }
unlock:
        UNLOCK (&frame->lock);

        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
                dht_selfheal_new_directory (frame, dht_mkdir_selfheal_cbk,
                                            layout);
        }

        return 0;
}

int
dht_mkdir_hashed_cbk (call_frame_t *frame, void *cookie,
                      xlator_t *this, int op_ret, int op_errno,
                      inode_t *inode, struct iatt *stbuf,
                      struct iatt *preparent, struct iatt *postparent,
                      dict_t *xdata)
{
        dht_local_t  *local = NULL;
        int           ret = -1;
        call_frame_t *prev = NULL;
        dht_layout_t *layout = NULL;
        dht_conf_t   *conf = NULL;
        int           i = 0;
        xlator_t     *hashed_subvol = NULL;

        VALIDATE_OR_GOTO (this->private, err);

        local = frame->local;
        prev  = cookie;
        layout = local->layout;
        conf = this->private;
        hashed_subvol = local->hashed_subvol;

        if (uuid_is_null (local->loc.gfid) && !op_ret)
                uuid_copy (local->loc.gfid, stbuf->ia_gfid);

        if (dht_is_subvol_filled (this, hashed_subvol))
                ret = dht_layout_merge (this, layout, prev->this,
                                        -1, ENOSPC, NULL);
        else
                ret = dht_layout_merge (this, layout, prev->this,
                                        op_ret, op_errno, NULL);

        /* TODO: we may have to return from the function
           if layout merge fails. For now, lets just log an error */
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to merge layouts", local->loc.path);

        if (op_ret == -1) {
                local->op_errno = op_errno;
                goto err;
        }
        local->op_ret = 0;

        dht_iatt_merge (this, &local->stbuf, stbuf, prev->this);
        dht_iatt_merge (this, &local->preparent, preparent, prev->this);
        dht_iatt_merge (this, &local->postparent, postparent, prev->this);

        local->call_cnt = conf->subvolume_cnt - 1;

        if (uuid_is_null (local->loc.gfid))
                uuid_copy (local->loc.gfid, stbuf->ia_gfid);
        if (local->call_cnt == 0) {
                dht_selfheal_directory (frame, dht_mkdir_selfheal_cbk,
                                        &local->loc, layout);
        }
        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (conf->subvolumes[i] == hashed_subvol)
                        continue;
                STACK_WIND (frame, dht_mkdir_cbk,
                            conf->subvolumes[i],
                            conf->subvolumes[i]->fops->mkdir, &local->loc,
                            local->mode, local->umask, local->params);
        }
        return 0;
err:
        DHT_STACK_UNWIND (mkdir, frame, -1, op_errno, NULL, NULL, NULL,
                          NULL, NULL);
        return 0;
}


 int
dht_mkdir (call_frame_t *frame, xlator_t *this,
           loc_t *loc, mode_t mode, mode_t umask, dict_t *params)
{
        dht_local_t  *local  = NULL;
        dht_conf_t   *conf = NULL;
        int           op_errno = -1;
        xlator_t     *hashed_subvol = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;

        dht_get_du_info (frame, this, loc);

        local = dht_local_init (frame, loc, NULL, GF_FOP_MKDIR);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        hashed_subvol = dht_subvol_get_hashed (this, loc);
        if (hashed_subvol == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "hashed subvol not found for %s",
                        loc->path);
                op_errno = EINVAL;
                goto err;
        }

        local->hashed_subvol = hashed_subvol;
        local->mode = mode;
        local->umask = umask;
        local->params = dict_ref (params);
        local->inode  = inode_ref (loc->inode);

        local->layout = dht_layout_new (this, conf->subvolume_cnt);
        if (!local->layout) {
                op_errno = ENOMEM;
                goto err;
        }

        STACK_WIND (frame, dht_mkdir_hashed_cbk,
                    hashed_subvol,
                    hashed_subvol->fops->mkdir,
                    loc, mode, umask, params);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (mkdir, frame, -1, op_errno, NULL, NULL, NULL,
                          NULL, NULL);

        return 0;
}


int
dht_rmdir_selfheal_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, dict_t *xdata)
{
        dht_local_t  *local = NULL;

        local = frame->local;

        DHT_STACK_UNWIND (rmdir, frame, local->op_ret, local->op_errno,
                          &local->preparent, &local->postparent, NULL);

        return 0;
}


int
dht_rmdir_hashed_subvol_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int op_ret, int op_errno, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev = NULL;

        local = frame->local;
        prev  = cookie;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        local->op_ret   = -1;
                        if (op_errno != ENOENT && op_errno != EACCES) {
                                local->need_selfheal = 1;
                        }


                        gf_log (this->name, GF_LOG_DEBUG,
                                "rmdir on %s for %s failed (%s)",
                                prev->this->name, local->loc.path,
                                strerror (op_errno));
                        goto unlock;
                }

                dht_iatt_merge (this, &local->preparent, preparent, prev->this);
                dht_iatt_merge (this, &local->postparent, postparent,
                                prev->this);

        }
unlock:
        UNLOCK (&frame->lock);

        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
               if (local->need_selfheal) {
                        local->layout =
                                dht_layout_get (this, local->loc.inode);

                        /* TODO: neater interface needed below */
                        local->stbuf.ia_type = local->loc.inode->ia_type;

                        uuid_copy (local->gfid, local->loc.inode->gfid);
                        dht_selfheal_restore (frame, dht_rmdir_selfheal_cbk,
                                              &local->loc, local->layout);
               } else {

                        if (local->loc.parent) {
                                dht_inode_ctx_time_update (local->loc.parent,
                                                           this,
                                                           &local->preparent,
                                                           0);

                                dht_inode_ctx_time_update (local->loc.parent,
                                                           this,
                                                           &local->postparent,
                                                           1);
                        }

                        DHT_STACK_UNWIND (rmdir, frame, local->op_ret,
                                          local->op_errno, &local->preparent,
                                          &local->postparent, NULL);
               }
        }

        return 0;
}


int
dht_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int op_ret, int op_errno, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev = NULL;
        int           done = 0;

        local = frame->local;
        prev  = cookie;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        local->op_ret   = -1;

                        if (op_errno != ENOENT && op_errno != EACCES) {
                                local->need_selfheal = 1;
                        }

                        gf_log (this->name, GF_LOG_DEBUG,
                                "rmdir on %s for %s failed (%s)",
                                prev->this->name, local->loc.path,
                                strerror (op_errno));
                        goto unlock;
                }

                /* Track if rmdir succeeded on atleast one subvol*/
                local->fop_succeeded = 1;
                dht_iatt_merge (this, &local->preparent, preparent, prev->this);
                dht_iatt_merge (this, &local->postparent, postparent,
                                prev->this);
        }
unlock:
        UNLOCK (&frame->lock);


        this_call_cnt = dht_frame_return (frame);

        /* if local->hashed_subvol, we are yet to wind to hashed_subvol. */
        if (local->hashed_subvol && (this_call_cnt == 1)) {
                done = 1;
        } else if (!local->hashed_subvol && !this_call_cnt) {
                done = 1;
        }


        if (done) {
                if (local->need_selfheal && local->fop_succeeded) {
                        local->layout =
                                dht_layout_get (this, local->loc.inode);

                        /* TODO: neater interface needed below */
                        local->stbuf.ia_type = local->loc.inode->ia_type;

                        uuid_copy (local->gfid, local->loc.inode->gfid);
                        dht_selfheal_restore (frame, dht_rmdir_selfheal_cbk,
                                              &local->loc, local->layout);
                } else if (this_call_cnt) {
                        /* If non-hashed subvol's have responded, proceed */

                        local->need_selfheal = 0;
                        STACK_WIND (frame, dht_rmdir_hashed_subvol_cbk,
                                    local->hashed_subvol,
                                    local->hashed_subvol->fops->rmdir,
                                    &local->loc, local->flags, NULL);
                } else if (!this_call_cnt) {
                        /* All subvol's have responded, proceed */

                        if (local->loc.parent) {

                                dht_inode_ctx_time_update (local->loc.parent,
                                                           this,
                                                           &local->preparent,
                                                           0);

                                dht_inode_ctx_time_update (local->loc.parent,
                                                           this,
                                                           &local->postparent,
                                                           1);

                        }

                        DHT_STACK_UNWIND (rmdir, frame, local->op_ret,
                                          local->op_errno, &local->preparent,
                                          &local->postparent, NULL);
                }
        }

        return 0;
}


int
dht_rmdir_do (call_frame_t *frame, xlator_t *this)
{
        dht_local_t  *local = NULL;
        dht_conf_t   *conf = NULL;
        int           i = 0;
        xlator_t     *hashed_subvol = NULL;

        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;
        local = frame->local;

        if (local->op_ret == -1)
                goto err;

        local->call_cnt = conf->subvolume_cnt;

        /* first remove from non-hashed_subvol */
        hashed_subvol = dht_subvol_get_hashed (this, &local->loc);

        if (!hashed_subvol) {
                gf_log (this->name, GF_LOG_WARNING, "failed to get hashed "
                        "subvol for %s",local->loc.path);
        } else {
                local->hashed_subvol = hashed_subvol;
        }

        /* When DHT has only 1 child */
        if (conf->subvolume_cnt == 1) {
                STACK_WIND (frame, dht_rmdir_hashed_subvol_cbk,
                            conf->subvolumes[0],
                            conf->subvolumes[0]->fops->rmdir,
                            &local->loc, local->flags, NULL);
                return 0;
        }

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (hashed_subvol &&
                    (hashed_subvol == conf->subvolumes[i]))
                        continue;

                STACK_WIND (frame, dht_rmdir_cbk,
                            conf->subvolumes[i],
                            conf->subvolumes[i]->fops->rmdir,
                            &local->loc, local->flags, NULL);
        }

        return 0;

err:
        DHT_STACK_UNWIND (rmdir, frame, local->op_ret, local->op_errno,
                          &local->preparent, &local->postparent, NULL);
        return 0;
}


int
dht_rmdir_linkfile_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                               int op_ret, int op_errno, struct iatt *preparent,
                               struct iatt *postparent, dict_t *xdata)
{
        dht_local_t    *local = NULL;
        call_frame_t   *prev = NULL;
        xlator_t       *src = NULL;
        call_frame_t   *main_frame = NULL;
        dht_local_t    *main_local = NULL;
        int             this_call_cnt = 0;

        local  = frame->local;
        prev   = cookie;
        src    = prev->this;

        main_frame = local->main_frame;
        main_local = main_frame->local;

        if (op_ret == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "unlinked linkfile %s on %s",
                        local->loc.path, src->name);
        } else {
                main_local->op_ret   = -1;
                main_local->op_errno = op_errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "unlink of %s on %s failed (%s)",
                        local->loc.path, src->name, strerror (op_errno));
        }

        this_call_cnt = dht_frame_return (main_frame);
        if (is_last_call (this_call_cnt))
                dht_rmdir_do (main_frame, this);

        DHT_STACK_DESTROY (frame);
        return 0;
}


int
dht_rmdir_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, inode_t *inode,
                      struct iatt *stbuf, dict_t *xattr, struct iatt *parent)
{
        dht_local_t    *local = NULL;
        call_frame_t   *prev = NULL;
        xlator_t       *src = NULL;
        call_frame_t   *main_frame = NULL;
        dht_local_t    *main_local = NULL;
        int             this_call_cnt = 0;
        dht_conf_t     *conf = this->private;

        local = frame->local;
        prev  = cookie;
        src   = prev->this;

        main_frame = local->main_frame;
        main_local = main_frame->local;

        if (op_ret != 0)
                goto err;

        if (!check_is_linkfile (inode, stbuf, xattr, conf->link_xattr_name)) {
                main_local->op_ret  = -1;
                main_local->op_errno = ENOTEMPTY;

                gf_log (this->name, GF_LOG_WARNING,
                        "%s on %s found to be not a linkfile (type=0%o)",
                        local->loc.path, src->name, stbuf->ia_type);
                goto err;
        }

        STACK_WIND (frame, dht_rmdir_linkfile_unlink_cbk,
                    src, src->fops->unlink, &local->loc, 0, NULL);
        return 0;
err:

        this_call_cnt = dht_frame_return (main_frame);
        if (is_last_call (this_call_cnt))
                dht_rmdir_do (main_frame, this);

        DHT_STACK_DESTROY (frame);
        return 0;
}


int
dht_rmdir_cached_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int op_ret, int op_errno, inode_t *inode,
                             struct iatt *stbuf, dict_t *xattr,
                             struct iatt *parent)
{
        dht_local_t    *local         = NULL;
        xlator_t       *src           = NULL;
        call_frame_t   *main_frame    = NULL;
        dht_local_t    *main_local    = NULL;
        int             this_call_cnt = 0;
        dht_conf_t     *conf          = this->private;
        dict_t         *xattrs        = NULL;
        int             ret           = 0;

        local = frame->local;
        src   = local->hashed_subvol;

        main_frame = local->main_frame;
        main_local = main_frame->local;

        if (op_ret == 0) {
                main_local->op_ret  = -1;
                main_local->op_errno = ENOTEMPTY;

                gf_log (this->name, GF_LOG_WARNING,
                        "%s found on cached subvol %s",
                        local->loc.path, src->name);
                goto err;
        } else if (op_errno != ENOENT) {
                main_local->op_ret  = -1;
                main_local->op_errno = op_errno;
                goto err;
        }

        xattrs = dict_new ();
        if (!xattrs) {
                gf_log (this->name, GF_LOG_ERROR, "dict_new failed");
                goto err;
        }

        ret = dict_set_uint32 (xattrs, conf->link_xattr_name, 256);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to set linkto key"
                        " in dict");
                if (xattrs)
                        dict_unref (xattrs);
                goto err;
        }

        STACK_WIND (frame, dht_rmdir_lookup_cbk,
                    src, src->fops->lookup, &local->loc, xattrs);
        if (xattrs)
                dict_unref (xattrs);

        return 0;
err:

        this_call_cnt = dht_frame_return (main_frame);
        if (is_last_call (this_call_cnt))
                dht_rmdir_do (main_frame, this);

        DHT_STACK_DESTROY (frame);
        return 0;
}


int
dht_rmdir_is_subvol_empty (call_frame_t *frame, xlator_t *this,
                           gf_dirent_t *entries, xlator_t *src)
{
        int                 ret          = 0;
        int                 build_ret    = 0;
        gf_dirent_t        *trav         = NULL;
        call_frame_t       *lookup_frame = NULL;
        dht_local_t        *lookup_local = NULL;
        dht_local_t        *local        = NULL;
        dict_t             *xattrs       = NULL;
        dht_conf_t         *conf         = this->private;
        xlator_t           *subvol       = NULL;

        local = frame->local;

        list_for_each_entry (trav, &entries->list, list) {
                if (strcmp (trav->d_name, ".") == 0)
                        continue;
                if (strcmp (trav->d_name, "..") == 0)
                        continue;
                if (check_is_linkfile (NULL, (&trav->d_stat), trav->dict,
                                              conf->link_xattr_name)) {
                        ret++;
                        continue;
                }

                /* this entry is either a directory which is neither "." nor "..",
                   or a non directory which is not a linkfile. the directory is to
                   be treated as non-empty
                */
                return 0;
        }

        xattrs = dict_new ();
        if (!xattrs) {
                gf_log (this->name, GF_LOG_ERROR, "dict_new failed");
                return -1;
        }

        ret = dict_set_uint32 (xattrs, conf->link_xattr_name, 256);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to set linkto key"
                        " in dict");
                if (xattrs)
                        dict_unref (xattrs);
                return -1;
        }

        list_for_each_entry (trav, &entries->list, list) {
                if (strcmp (trav->d_name, ".") == 0)
                        continue;
                if (strcmp (trav->d_name, "..") == 0)
                        continue;

                lookup_frame = NULL;
                lookup_local = NULL;

                lookup_frame = copy_frame (frame);
                if (!lookup_frame) {
                        /* out of memory, let the rmdir fail
                           (as non-empty, unfortunately) */
                        goto err;
                }

                lookup_local = mem_get0 (this->local_pool);
                if (!lookup_local) {
                        goto err;
                }

                lookup_frame->local = lookup_local;
                lookup_local->main_frame = frame;
                lookup_local->hashed_subvol = src;

                build_ret = dht_build_child_loc (this, &lookup_local->loc,
                                                 &local->loc, trav->d_name);
                if (build_ret != 0)
                        goto err;

                uuid_copy (lookup_local->loc.gfid, trav->d_stat.ia_gfid);

                gf_log (this->name, GF_LOG_TRACE,
                        "looking up %s on %s",
                        lookup_local->loc.path, src->name);

                LOCK (&frame->lock);
                {
                        local->call_cnt++;
                }
                UNLOCK (&frame->lock);

                subvol = dht_linkfile_subvol (this, NULL, &trav->d_stat,
                                              trav->dict);
                if (!subvol) {
                        gf_log (this->name, GF_LOG_INFO,
                                "linkfile not having link subvolume. path=%s",
                                lookup_local->loc.path);
                        STACK_WIND (lookup_frame, dht_rmdir_lookup_cbk,
                                    src, src->fops->lookup,
                                    &lookup_local->loc, xattrs);
                } else {
                        STACK_WIND (lookup_frame, dht_rmdir_cached_lookup_cbk,
                                    subvol, subvol->fops->lookup,
                                    &lookup_local->loc, xattrs);
                }
                ret++;
        }

        if (xattrs)
                dict_unref (xattrs);

        return ret;
err:
        if (xattrs)
                dict_unref (xattrs);

        DHT_STACK_DESTROY (lookup_frame);
        return 0;
}


int
dht_rmdir_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, gf_dirent_t *entries,
                        dict_t *xdata)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = -1;
        call_frame_t *prev = NULL;
        xlator_t     *src = NULL;
        int           ret = 0;

        local = frame->local;
        prev  = cookie;
        src   = prev->this;

        if (op_ret > 2) {
                ret = dht_rmdir_is_subvol_empty (frame, this, entries, src);

                switch (ret) {
                case 0: /* non linkfiles exist */
                        gf_log (this->name, GF_LOG_TRACE,
                                "readdir on %s for %s returned %d entries",
                                prev->this->name, local->loc.path, op_ret);
                        local->op_ret = -1;
                        local->op_errno = ENOTEMPTY;
                        break;
                default:
                        /* @ret number of linkfiles are getting unlinked */
                        gf_log (this->name, GF_LOG_TRACE,
                                "readdir on %s for %s found %d linkfiles",
                                prev->this->name, local->loc.path, ret);
                        break;
                }
        }

        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                dht_rmdir_do (frame, this);
        }

        return 0;
}


int
dht_rmdir_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, fd_t *fd, dict_t *xdata)
{
        dht_local_t  *local         = NULL;
        int           this_call_cnt = -1;
        call_frame_t *prev          = NULL;
        dict_t       *dict          = NULL;
        int           ret           = 0;
        dht_conf_t   *conf          = this->private;
        int           i             = 0;

        local = frame->local;
        prev  = cookie;

        this_call_cnt = dht_frame_return (frame);
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "opendir on %s for %s failed (%s)",
                        prev->this->name, local->loc.path,
                        strerror (op_errno));
                if (op_errno != ENOENT) {
                        local->op_ret = -1;
                        local->op_errno = op_errno;
                }
                goto err;
        }

        if (!is_last_call (this_call_cnt))
                return 0;

        if (local->op_ret == -1)
                goto err;

        dict = dict_new ();
        if (!dict) {
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                goto err;
        }

        ret = dict_set_uint32 (dict, conf->link_xattr_name, 256);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to set '%s' key",
                        local->loc.path, conf->link_xattr_name);

        local->call_cnt = conf->subvolume_cnt;
        for (i = 0; i < conf->subvolume_cnt; i++) {
                STACK_WIND (frame, dht_rmdir_readdirp_cbk,
                            conf->subvolumes[i],
                            conf->subvolumes[i]->fops->readdirp,
                            local->fd, 4096, 0, dict);
        }

        if (dict)
                dict_unref (dict);

        return 0;

err:
        if (is_last_call (this_call_cnt)) {
                dht_rmdir_do (frame, this);
        }

        return 0;
}


int
dht_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
           dict_t *xdata)
{
        dht_local_t  *local  = NULL;
        dht_conf_t   *conf = NULL;
        int           op_errno = -1;
        int           i = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;

        local = dht_local_init (frame, loc, NULL, GF_FOP_RMDIR);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->call_cnt = conf->subvolume_cnt;
        local->op_ret   = 0;
        local->fop_succeeded = 0;

        local->flags = flags;

        local->fd = fd_create (local->loc.inode, frame->root->pid);
        if (!local->fd) {

                op_errno = ENOMEM;
                goto err;
        }

        for (i = 0; i < conf->subvolume_cnt; i++) {
                STACK_WIND (frame, dht_rmdir_opendir_cbk,
                            conf->subvolumes[i],
                            conf->subvolumes[i]->fops->opendir,
                            loc, local->fd, NULL);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (rmdir, frame, -1, op_errno,
                          NULL, NULL, NULL);

        return 0;
}

int
dht_entrylk_cbk (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
        DHT_STACK_UNWIND (entrylk, frame, op_ret, op_errno, xdata);
        return 0;
}


int
dht_entrylk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc, const char *basename,
             entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        local = dht_local_init (frame, loc, NULL, GF_FOP_ENTRYLK);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        subvol = local->cached_subvol;
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        local->call_cnt = 1;

        STACK_WIND (frame, dht_entrylk_cbk,
                    subvol, subvol->fops->entrylk,
                    volume, loc, basename, cmd, type, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (entrylk, frame, -1, op_errno, NULL);

        return 0;
}


int
dht_fentrylk_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
        DHT_STACK_UNWIND (fentrylk, frame, op_ret, op_errno, NULL);
        return 0;
}


int
dht_fentrylk (call_frame_t *frame, xlator_t *this,
              const char *volume, fd_t *fd, const char *basename,
              entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        subvol = dht_subvol_get_cached (this, fd->inode);
        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for fd=%p", fd);
                op_errno = EINVAL;
                goto err;
        }

        STACK_WIND (frame, dht_fentrylk_cbk,
                    subvol, subvol->fops->fentrylk,
                    volume, fd, basename, cmd, type, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (fentrylk, frame, -1, op_errno, NULL);

        return 0;
}


int
dht_forget (xlator_t *this, inode_t *inode)
{
        uint64_t        ctx_int = 0;
        dht_inode_ctx_t *ctx    = NULL;
        dht_layout_t *layout = NULL;

        inode_ctx_del (inode, this, &ctx_int);

        if (!ctx_int)
                return 0;

        ctx = (dht_inode_ctx_t *) (long) ctx_int;

        layout = ctx->layout;
        ctx->layout = NULL;
        dht_layout_unref (this, layout);
        GF_FREE (ctx);

        return 0;
}


int
dht_notify (xlator_t *this, int event, void *data, ...)
{
        xlator_t                *subvol = NULL;
        int                      cnt    = -1;
        int                      i      = -1;
        dht_conf_t              *conf   = NULL;
        int                      ret    = -1;
        int                      propagate = 0;

        int                      had_heard_from_all = 0;
        int                      have_heard_from_all = 0;
        struct timeval           time = {0,};
        gf_defrag_info_t        *defrag = NULL;
        dict_t                  *dict   = NULL;
        gf_defrag_type           cmd    = 0;
        dict_t                  *output = NULL;
        va_list                  ap;


        conf = this->private;
        if (!conf)
                return ret;

        /* had all subvolumes reported status once till now? */
        had_heard_from_all = 1;
        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (!conf->last_event[i]) {
                        had_heard_from_all = 0;
                }
        }

        switch (event) {
        case GF_EVENT_CHILD_UP:
                subvol = data;

                conf->gen++;

                for (i = 0; i < conf->subvolume_cnt; i++) {
                        if (subvol == conf->subvolumes[i]) {
                                cnt = i;
                                break;
                        }
                }

                if (cnt == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "got GF_EVENT_CHILD_UP bad subvolume %s",
                                subvol->name);
                        break;
                }

                gettimeofday (&time, NULL);
                LOCK (&conf->subvolume_lock);
                {
                        conf->subvolume_status[cnt] = 1;
                        conf->last_event[cnt] = event;
                        conf->subvol_up_time[cnt] = time.tv_sec;
                }
                UNLOCK (&conf->subvolume_lock);

                /* one of the node came back up, do a stat update */
                dht_get_du_info_for_subvol (this, cnt);

                break;

        case GF_EVENT_CHILD_MODIFIED:
                subvol = data;

                conf->gen++;
                propagate = 1;

                break;

        case GF_EVENT_CHILD_DOWN:
                subvol = data;

                if (conf->assert_no_child_down) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Received CHILD_DOWN. Exiting");
                        if (conf->defrag) {
                                gf_defrag_stop (conf->defrag,
                                                GF_DEFRAG_STATUS_FAILED, NULL);
                        } else {
                                kill (getpid(), SIGTERM);
                        }
                }

                for (i = 0; i < conf->subvolume_cnt; i++) {
                        if (subvol == conf->subvolumes[i]) {
                                cnt = i;
                                break;
                        }
                }

                if (cnt == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "got GF_EVENT_CHILD_DOWN bad subvolume %s",
                                subvol->name);
                        break;
                }

                LOCK (&conf->subvolume_lock);
                {
                        conf->subvolume_status[cnt] = 0;
                        conf->last_event[cnt] = event;
                        conf->subvol_up_time[cnt] = 0;
                }
                UNLOCK (&conf->subvolume_lock);

                break;

        case GF_EVENT_CHILD_CONNECTING:
                subvol = data;

                for (i = 0; i < conf->subvolume_cnt; i++) {
                        if (subvol == conf->subvolumes[i]) {
                                cnt = i;
                                break;
                        }
                }

                if (cnt == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "got GF_EVENT_CHILD_CONNECTING bad subvolume %s",
                                subvol->name);
                        break;
                }

                LOCK (&conf->subvolume_lock);
                {
                        conf->last_event[cnt] = event;
                }
                UNLOCK (&conf->subvolume_lock);

                break;
        case GF_EVENT_VOLUME_DEFRAG:
        {
                if (!conf->defrag) {
                        return ret;
                }
                defrag = conf->defrag;

                dict = data;
                va_start (ap, data);
                output = va_arg (ap, dict_t*);

                ret = dict_get_int32 (dict, "rebalance-command",
                                      (int32_t*)&cmd);
                if (ret)
                        return ret;
                LOCK (&defrag->lock);
                {
                        if (defrag->is_exiting)
                                goto unlock;
                        if (cmd == GF_DEFRAG_CMD_STATUS)
                                gf_defrag_status_get (defrag, output);
                        else if (cmd == GF_DEFRAG_CMD_STOP)
                                gf_defrag_stop (defrag,
                                                GF_DEFRAG_STATUS_STOPPED, output);
                }
unlock:
                UNLOCK (&defrag->lock);
                return 0;
                break;
        }

        default:
                propagate = 1;
                break;
        }


        /* have all subvolumes reported status once by now? */
        have_heard_from_all = 1;
        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (!conf->last_event[i])
                        have_heard_from_all = 0;
        }

        /* if all subvols have reported status, no need to hide anything
           or wait for anything else. Just propagate blindly */
        if (have_heard_from_all) {
                propagate = 1;

        }


        if (!had_heard_from_all && have_heard_from_all) {
                /* This is the first event which completes aggregation
                   of events from all subvolumes. If at least one subvol
                   had come up, propagate CHILD_UP, but only this time
                */
                event = GF_EVENT_CHILD_DOWN;

                for (i = 0; i < conf->subvolume_cnt; i++) {
                        if (conf->last_event[i] == GF_EVENT_CHILD_UP) {
                                event = GF_EVENT_CHILD_UP;
                                break;
                        }

                        if (conf->last_event[i] == GF_EVENT_CHILD_CONNECTING) {
                                event = GF_EVENT_CHILD_CONNECTING;
                                /* continue to check other events for CHILD_UP */
                        }
                }

                /* rebalance is started with assert_no_child_down. So we do
                 * not need to handle CHILD_DOWN event here.
                 */
                if (conf->defrag) {
                        ret = gf_thread_create (&conf->defrag->th, NULL,
						gf_defrag_start, this);
                        if (ret) {
                                conf->defrag = NULL;
                                GF_FREE (conf->defrag);
                                kill (getpid(), SIGTERM);
                        }
                }
        }

        ret = 0;
        if (propagate)
                ret = default_notify (this, event, data);

        return ret;
}

int
dht_inode_ctx_layout_get (inode_t *inode, xlator_t *this, dht_layout_t **layout)
{
        dht_inode_ctx_t         *ctx            = NULL;
        int                      ret            = -1;

        ret = dht_inode_ctx_get (inode, this, &ctx);

        if (!ret && ctx) {
                if (ctx->layout) {
                        if (layout)
                                *layout = ctx->layout;
                        ret = 0;
                } else {
                        ret = -1;
                }
        }

        return ret;
}
