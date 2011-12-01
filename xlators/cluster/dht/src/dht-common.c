/*
  Copyright (c) 2009-2011 Gluster, Inc. <http://www.gluster.com>
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

/* TODO: add NS locking */

#include "glusterfs.h"
#include "xlator.h"
#include "libxlator.h"
#include "dht-common.h"
#include "defaults.h"
#include "byte-order.h"

#include <sys/time.h>
#include <libgen.h>

void
dht_aggregate (dict_t *this, char *key, data_t *value, void *data)
{
        dict_t  *dst  = NULL;
        int64_t *ptr  = 0, *size = NULL;
        int32_t  ret  = -1;
        data_pair_t  *data_pair = NULL;

        dst = data;

        if (strcmp (key, GF_XATTR_QUOTA_SIZE_KEY) == 0) {
                ret = dict_get_bin (dst, key, (void **)&size);
                if (ret < 0) {
                        size = GF_CALLOC (1, sizeof (int64_t),
                                          gf_common_mt_char);
                        if (size == NULL) {
                                gf_log ("dht", GF_LOG_WARNING,
                                        "memory allocation failed");
                                return;
                        }
                        ret = dict_set_bin (dst, key, size, sizeof (int64_t));
                        if (ret < 0) {
                                gf_log ("dht", GF_LOG_WARNING,
                                        "dht aggregate dict set failed");
                                GF_FREE (size);
                                return;
                        }
                }

                ptr = data_to_bin (value);
                if (ptr == NULL) {
                        gf_log ("dht", GF_LOG_WARNING, "data to bin failed");
                        return;
                }

                *size = hton64 (ntoh64 (*size) + ntoh64 (*ptr));
        } else {
                /* compare user xattrs only */
                if (!strncmp (key, "user.", strlen ("user."))) {
                        ret = dict_lookup (dst, key, &data_pair); 
                        if (!ret && data) {
                                ret = is_data_equal (data_pair->value, value);
                                if (!ret)
                                        gf_log ("dht", GF_LOG_WARNING,
                                                "xattr mismatch for %s", key);
                        }
                }
                ret = dict_set (dst, key, value);
                if (ret)
                        gf_log ("dht", GF_LOG_WARNING, "xattr dict set failed");
        }

        return;
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
                         int op_ret, int op_errno)
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

        WIPE (&local->postparent);

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

        local = discover_frame->local;
        layout = local->layout;

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

                if (ret != 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "normalizing failed on %s",
                                local->loc.path);
                        op_errno = EINVAL;
                        goto out;
                }

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

        GF_VALIDATE_OR_GOTO ("dht", frame, out);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", this->private, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);

        local = frame->local;
        prev  = cookie;

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
                        local->op_errno = ENOENT;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "lookup of %s on %s returned error (%s)",
                                local->loc.path, prev->this->name,
                                strerror (op_errno));

                        goto unlock;
                }

                is_linkfile = check_is_linkfile (inode, stbuf, xattr);
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

        ret = dict_set_uint32 (local->xattr_req,
                               "trusted.glusterfs.dht", 4 * 4);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to set 'trusted.glusterfs.dht' key",
                        loc->path);

        ret = dict_set_uint32 (local->xattr_req,
                               "trusted.glusterfs.dht.linkto", 256);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to set 'trusted.glusterfs.dht.linkto' key",
                        loc->path);

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
        DHT_STACK_UNWIND (lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);

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
                        local->op_errno = ENOENT;
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
                is_linkfile = check_is_linkfile (inode, stbuf, xattr);

                if (is_linkfile) {
                        gf_log (this->name, GF_LOG_INFO,
                                "linkfile found in revalidate for %s",
                                local->loc.path);
                        local->return_estale = 1;

                        goto unlock;
                }

                if (is_dir) {
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

                WIPE (&local->postparent);

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
                                struct iatt *preparent, struct iatt *postparent)
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

        ret = dht_layout_preset (this, local->cached_subvol, inode);
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

unwind:
        WIPE (&local->postparent);

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

                WIPE (&local->postparent);

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

                WIPE (&local->postparent);

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
                                   dht_lookup_linkfile_create_cbk,
                                   cached_subvol, hashed_subvol, &local->loc);

        return ret;
}


int
dht_lookup_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno,
                       struct iatt *preparent, struct iatt *postparent)
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
        int           ret = -1;
        int32_t       fd_count = 0;

        GF_VALIDATE_OR_GOTO ("dht", frame, out);
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);
        GF_VALIDATE_OR_GOTO ("dht", cookie, out);
        GF_VALIDATE_OR_GOTO ("dht", this->private, out);

        local  = frame->local;
        loc    = &local->loc;

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

                is_linkfile = check_is_linkfile (inode, buf, xattr);
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
                                    subvol, subvol->fops->unlink, loc);
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
                goto err;
        }

        if (check_is_dir (inode, stbuf, xattr)) {
                gf_log (this->name, GF_LOG_INFO,
                        "lookup of %s on %s (following linkfile) reached dir",
                        local->loc.path, subvol->name);
                goto err;
        }

        if (check_is_linkfile (inode, stbuf, xattr)) {
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

unwind:
        WIPE (postparent);

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
        uint64_t      tmp_layout    = 0;
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
                        ret = inode_ctx_get (loc->parent, this, &tmp_layout);
                        parent_layout = (dht_layout_t *)(long)tmp_layout;
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

        is_linkfile = check_is_linkfile (inode, stbuf, xattr);

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

        WIPE (postparent);

        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (lookup, frame, op_ret, op_errno, inode, stbuf, xattr,
                          postparent);
err:
        return 0;
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
        VALIDATE_OR_GOTO (loc->path, err);

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
                                       "trusted.glusterfs.dht", 4 * 4);

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
                                       "trusted.glusterfs.dht", 4 * 4);

                ret = dict_set_uint32 (local->xattr_req,
                                       DHT_LINKFILE_KEY, 256);

                /* need it for self-healing linkfiles which is
                   'in-migration' state */
                ret = dict_set_uint32 (local->xattr_req,
                                       GLUSTERFS_OPEN_FD_COUNT, 4);

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
        DHT_STACK_UNWIND (lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);
        return 0;
}


int
dht_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, struct iatt *preparent,
                struct iatt *postparent)
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

                WIPE (&local->postparent);
                WIPE (&local->preparent);
        }
unlock:
        UNLOCK (&frame->lock);

        DHT_STACK_UNWIND (unlink, frame, local->op_ret, local->op_errno,
                          &local->preparent, &local->postparent);

        return 0;
}


int
dht_unlink_linkfile_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, struct iatt *preparent,
                         struct iatt *postparent)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;

        xlator_t *cached_subvol = NULL;

        local = frame->local;
        prev  = cookie;

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

        if (op_ret == -1)
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
                    &local->loc);

        return 0;

err:
        DHT_STACK_UNWIND (unlink, frame, -1, local->op_errno,
                          NULL, NULL);
        return 0;
}

static int
dht_ufo_xattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev = NULL;

        local = frame->local;
        prev = cookie;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_ret = -1;
                        local->op_errno = op_errno;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "subvolume %s returned -1 (%s)",
                                prev->this->name, strerror (op_errno));
                        goto unlock;
                }
        }
unlock:
        UNLOCK (&frame->lock);

        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
                DHT_STACK_UNWIND (setxattr, frame, local->op_ret, local->op_errno);
        }

        return 0;
}


int
dht_err_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int op_ret, int op_errno)
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
                DHT_STACK_UNWIND (setxattr, frame, local->op_ret, local->op_errno);
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

int
dht_pathinfo_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int op_ret, int op_errno, dict_t *xattr)
{
        dht_local_t *local         = NULL;
        int          ret           = 0;
        int          flag          = 0;
        int          this_call_cnt = 0;
        char        *value_got     = NULL;
        char  layout_buf[8192]     = {0,};
        char        *xattr_buf     = NULL;
        dict_t      *dict          = NULL;
        int32_t      alloc_len     = 0;
        int32_t      plen          = 0;

        local = frame->local;

        if (op_ret != -1) {
                ret = dict_get_str (xattr, GF_XATTR_PATHINFO_KEY, &value_got);
                if (!ret) {
                        alloc_len = strlen (value_got);

                        /**
                         * allocate the buffer:- we allocate 10 bytes extra in case we need to
                         * append ' Link: ' in the buffer for another STACK_WIND
                         */
                        if (!local->pathinfo) {
                                alloc_len += (strlen (DHT_PATHINFO_HEADER) + 10);
                                local->pathinfo = GF_CALLOC (alloc_len, sizeof (char), gf_common_mt_char);
                        }

                        if (local->pathinfo) {
                                plen = strlen (local->pathinfo);
                                if (plen) {
                                        /* extra byte(s) for \0 to be safe */
                                        alloc_len += (plen + 2);
                                        local->pathinfo = GF_REALLOC (local->pathinfo,
                                                                      alloc_len);
                                        if (!local->pathinfo)
                                                goto out;
                                }

                                strcat (local->pathinfo, value_got);
                        }
                }
        }

 out:
        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
                if (local->layout->cnt > 1) {
                        /* Set it for directory */
                        fill_layout_info (local->layout, layout_buf);
                        flag = 1;
                }

                dict = dict_new ();

                /* we would need max-to-max this many bytes to create pathinfo string */
                alloc_len += (2 * strlen (this->name)) + strlen (layout_buf) + 40;
                xattr_buf = GF_CALLOC (alloc_len, sizeof (char), gf_common_mt_char);

                if (flag && local->pathinfo)
                        snprintf (xattr_buf, alloc_len, "((<"DHT_PATHINFO_HEADER"%s> %s) (%s-layout %s))",
                                  this->name, local->pathinfo, this->name,
                                  layout_buf);
                else if (local->pathinfo)
                        snprintf (xattr_buf, alloc_len, "(<"DHT_PATHINFO_HEADER"%s> %s)",
                                  this->name, local->pathinfo);
                else if (flag)
                        snprintf (xattr_buf, alloc_len, "(%s-layout %s)",
                                  this->name, layout_buf);

                ret = dict_set_dynstr (dict, GF_XATTR_PATHINFO_KEY,
                                       xattr_buf);

                if (local->pathinfo)
                        GF_FREE (local->pathinfo);

                DHT_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict);

                if (dict)
                        dict_unref (dict);

                return 0;
        }

        if (local->pathinfo)
                strcat (local->pathinfo, " Link: ");
        if (local->hashed_subvol) {
                /* This will happen if there pending */
                STACK_WIND (frame, dht_pathinfo_getxattr_cbk, local->hashed_subvol,
                            local->hashed_subvol->fops->getxattr,
                            &local->loc, local->key);

                return 0;
        }

        gf_log ("this->name", GF_LOG_ERROR, "Unable to find hashed_subvol for path"
                " %s", local->pathinfo);

        DHT_STACK_UNWIND (getxattr, frame, -1, op_errno, dict);
        return 0;
}

int
dht_linkinfo_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int op_ret, int op_errno, dict_t *xattr)
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

        DHT_STACK_UNWIND (getxattr, frame, op_ret, op_errno, xattr);

        return 0;
}

int
dht_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, dict_t *xattr)
{
        int             this_call_cnt = 0;
        dht_local_t     *local = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (frame->local, out);

        local = frame->local;

        this_call_cnt = dht_frame_return (frame);

        if (!xattr || (op_ret == -1))
                goto out;

        if (dict_get (xattr, "trusted.glusterfs.dht")) {
                dict_del (xattr, "trusted.glusterfs.dht");
        }
        local->op_ret = 0;

        if (!local->xattr) {
                local->xattr = dict_copy_with_ref (xattr, NULL);
        } else {
                /* first aggregate everything into xattr and then copy into
                 * local->xattr. This is required as we want to have
                 * 'local->xattr' as the proper final dictionary passed above
                 * distribute xlator.
                 */
                dht_aggregate_xattr (xattr, local->xattr);
                local->xattr = dict_copy (xattr, local->xattr);
        }
out:
        if (is_last_call (this_call_cnt)) {
                DHT_STACK_UNWIND (getxattr, frame, local->op_ret, op_errno, local->xattr);
        }
        return 0;
}

int32_t
dht_getxattr_unwind (call_frame_t *frame,
                     int op_ret, int op_errno, dict_t *dict)
{
        DHT_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict);
        return 0;
}


int
dht_getxattr (call_frame_t *frame, xlator_t *this,
              loc_t *loc, const char *key)
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
        VALIDATE_OR_GOTO (loc->path, err);
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

        if (key && (strcmp (key, GF_XATTR_PATHINFO_KEY) == 0)) {
                hashed_subvol = dht_subvol_get_hashed (this, loc);
                cached_subvol = local->cached_subvol;

                local->call_cnt = 1;
                if (hashed_subvol != cached_subvol) {
                        local->call_cnt = 2;
                        local->hashed_subvol = hashed_subvol;
                }

                STACK_WIND (frame, dht_pathinfo_getxattr_cbk, cached_subvol,
                            cached_subvol->fops->getxattr, loc, key);

                return 0;
        }
        if (key && (strcmp (key, GF_XATTR_LINKINFO_KEY) == 0)) {
                hashed_subvol = dht_subvol_get_hashed (this, loc);
                cached_subvol = dht_subvol_get_cached (this, loc->inode);
                if (hashed_subvol == cached_subvol) {
                        op_errno = ENODATA;
                        goto err;
                }
                if (hashed_subvol) {
                        STACK_WIND (frame, dht_linkinfo_getxattr_cbk, hashed_subvol,
                                    hashed_subvol->fops->getxattr, loc,
                                    GF_XATTR_PATHINFO_KEY);
                        return 0;
                }
                op_errno = ENODATA;
                goto err;
        }

        if (key && (!strcmp (GF_XATTR_MARKER_KEY, key))
            && (-1 == frame->root->pid)) {

                if (loc->inode-> ia_type == IA_IFDIR) {
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
                                           MARKER_UUID_TYPE, conf->vol_uuid)) {
                        op_errno = EINVAL;
                        goto err;
                }

                return 0;
        }

        if (key && *conf->vol_uuid) {
                if ((match_uuid_local (key, conf->vol_uuid) == 0) &&
                    (-1 == frame->root->pid)) {
                        if (loc->inode-> ia_type == IA_IFDIR) {
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
                                                   conf->vol_uuid)) {
                                op_errno = EINVAL;
                                goto err;
                        }

                        return 0;
                }
        }

        if (loc->inode-> ia_type == IA_IFDIR) {
                cnt = local->call_cnt = layout->cnt;
        } else {
                cnt = local->call_cnt  = 1;
        }

        for (i = 0; i < cnt; i++) {
                subvol = layout->list[i].xlator;
                STACK_WIND (frame, dht_getxattr_cbk,
                            subvol, subvol->fops->getxattr,
                            loc, key);
        }
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (getxattr, frame, -1, op_errno, NULL);

        return 0;
}

int
dht_fsetxattr (call_frame_t *frame, xlator_t *this,
               fd_t *fd, dict_t *xattr, int flags)
{
        xlator_t     *subvol   = NULL;
        dht_local_t  *local    = NULL;
        int           op_errno = EINVAL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

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
                    fd, xattr, flags);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (fsetxattr, frame, -1, op_errno);

        return 0;
}


static int
dht_common_setxattr_cbk (call_frame_t *frame, void *cookie,
                         xlator_t *this, int32_t op_ret, int32_t op_errno)
{
        DHT_STACK_UNWIND (setxattr, frame, op_ret, op_errno);

        return 0;
}

int
dht_checking_pathinfo_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int op_ret, int op_errno, dict_t *xattr)
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
                DHT_STACK_UNWIND (setxattr, frame, local->op_ret, ENOTSUP);
        }
        return 0;

}

int
dht_setxattr (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *xattr, int flags)
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
        int           forced_rebalance = 0;
        int           call_cnt = 0;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

        conf   = this->private;
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

        /*  This key is sent by Unified File and Object storage
         *  to test xattr support in backend.
         */
        tmp = dict_get (xattr, "user.ufo-test");
        if (tmp) {
                if (IA_ISREG (loc->inode->ia_type)) {
                        op_errno = ENOTSUP;
                        goto err;
                }
                local->op_ret = 0;
                for (i = 0; i < call_cnt; i++) {
                        STACK_WIND (frame, dht_ufo_xattr_cbk,
                                    layout->list[i].xlator,
                                    layout->list[i].xlator->fops->setxattr,
                                    loc, xattr, flags);
                }
                return 0;
        }

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
                        forced_rebalance = 1;

                local->rebalance.target_node = dht_subvol_get_hashed (this, loc);
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
                                    loc, GF_XATTR_PATHINFO_KEY);
                }
                return 0;
        }

        tmp = dict_get (xattr, GF_XATTR_FIX_LAYOUT_KEY);
        if (tmp) {
                gf_log (this->name, GF_LOG_INFO,
                        "fixing the layout of %s", loc->path);

                dht_fix_directory_layout (frame, dht_common_setxattr_cbk,
                                          layout);
                return 0;
        }

        tmp = dict_get (xattr, "distribute.directory-spread-count");
        if (tmp) {
                /* Setxattr value is packed as 'binary', not string */
                memcpy (value, tmp->data, ((tmp->len < 4095)?tmp->len:4095));
                ret = gf_string2uint32 (value, &dir_spread);
                if (!ret && ((dir_spread <= conf->subvolume_cnt) &&
                             (dir_spread > 0))) {
                        layout->spread_cnt = dir_spread;

                        dht_fix_directory_layout (frame,
                                                  dht_common_setxattr_cbk,
                                                  layout);
                        return 0;
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
                            loc, xattr, flags);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (setxattr, frame, -1, op_errno);

        return 0;
}


int
dht_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno)
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
                DHT_STACK_UNWIND (removexattr, frame, local->op_ret, local->op_errno);
        }

        return 0;
}


int
dht_removexattr (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, const char *key)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;
        dht_layout_t *layout = NULL;
        int           call_cnt = 0;

        int i;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

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
                            loc, key);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (removexattr, frame, -1, op_errno);

        return 0;
}


int
dht_fd_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
            int op_ret, int op_errno, fd_t *fd)
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
                                  local->fd);

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
                int op_ret, int op_errno, struct statvfs *statvfs)
{
        dht_local_t *local         = NULL;
        int          this_call_cnt = 0;
        int          bsize         = 0;
        int          frsize        = 0;


        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        goto unlock;
                }
                local->op_ret = 0;

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
                                  &local->statvfs);

        return 0;
}


int
dht_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc)
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
        VALIDATE_OR_GOTO (loc->path, err);
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
                                    conf->subvolumes[i]->fops->statfs, loc);
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
                    subvol, subvol->fops->statfs, loc);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (statfs, frame, -1, op_errno, NULL);

        return 0;
}


int
dht_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
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
                            loc, fd);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (opendir, frame, -1, op_errno, NULL);

        return 0;
}


int
dht_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                  int op_errno, gf_dirent_t *orig_entries)
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

                if (check_is_linkfile_wo_dict (NULL, (&orig_entry->d_stat))
                    || (check_is_dir (NULL, (&orig_entry->d_stat), NULL)
                        && (prev->this != dht_first_up_subvol (this)))) {
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

                STACK_WIND (frame, dht_readdirp_cbk,
                            next_subvol, next_subvol->fops->readdirp,
                            local->fd, local->size, next_offset);
                return 0;
        }

unwind:
        if (op_ret < 0)
                op_ret = 0;

        DHT_STACK_UNWIND (readdirp, frame, op_ret, op_errno, &entries);

        gf_dirent_free (&entries);

        return 0;
}



int
dht_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int op_ret, int op_errno, gf_dirent_t *orig_entries)
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
                            local->fd, local->size, next_offset);
                return 0;
        }

unwind:
        if (op_ret < 0)
                op_ret = 0;

        DHT_STACK_UNWIND (readdir, frame, op_ret, op_errno, &entries);

        gf_dirent_free (&entries);

        return 0;
}


int
dht_do_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t yoff, int whichop)
{
        dht_local_t  *local  = NULL;
        int           op_errno = -1;
        xlator_t     *xvol = NULL;
        off_t         xoff = 0;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        local = dht_local_init (frame, NULL, NULL, whichop);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->fd = fd_ref (fd);
        local->size = size;

        dht_deitransform (this, yoff, &xvol, (uint64_t *)&xoff);

        /* TODO: do proper readdir */
        if (whichop == GF_FOP_READDIR)
                STACK_WIND (frame, dht_readdir_cbk, xvol, xvol->fops->readdir,
                            fd, size, xoff);
        else
                STACK_WIND (frame, dht_readdirp_cbk, xvol, xvol->fops->readdirp,
                            fd, size, xoff);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (readdir, frame, -1, op_errno, NULL);

        return 0;
}


int
dht_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t yoff)
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
        dht_do_readdir (frame, this, fd, size, yoff, op);
        return 0;
}

int
dht_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t yoff)
{
        dht_do_readdir (frame, this, fd, size, yoff, GF_FOP_READDIRP);
        return 0;
}



int
dht_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno)
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
                DHT_STACK_UNWIND (fsyncdir, frame, local->op_ret, local->op_errno);

        return 0;
}


int
dht_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync)
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
                            fd, datasync);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (fsyncdir, frame, -1, op_errno);

        return 0;
}


int
dht_newfile_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int op_ret, int op_errno,
                 inode_t *inode, struct iatt *stbuf, struct iatt *preparent,
                 struct iatt *postparent)
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
                WIPE (preparent);
                WIPE (postparent);
        }

        ret = dht_layout_preset (this, prev->this, inode);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "could not set pre-set layout for subvolume %s",
                        prev->this->name);
                op_ret   = -1;
                op_errno = EINVAL;
                goto out;
        }
out:
        /*
         * FIXME: ia_size and st_blocks of preparent and postparent do not have
         * correct values. since, preparent and postparent buffers correspond
         * to a directory these two members should have values equal to sum of
         * corresponding values from each of the subvolume.
         * See dht_iatt_merge for reference.
         */

        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (mknod, frame, op_ret, op_errno, inode, stbuf, preparent,
                          postparent);
        return 0;
}

int
dht_mknod_linkfile_create_cbk (call_frame_t *frame, void *cookie,
                               xlator_t *this,
                               int32_t op_ret, int32_t op_errno,
                               inode_t *inode, struct iatt *stbuf,
                               struct iatt *preparent, struct iatt *postparent)
{
        dht_local_t  *local = NULL;
        xlator_t     *cached_subvol = NULL;

        if (op_ret == -1)
                goto err;

        local = frame->local;
        cached_subvol = local->cached_subvol;

        STACK_WIND (frame, dht_newfile_cbk,
                    cached_subvol, cached_subvol->fops->mknod,
                    &local->loc, local->mode, local->rdev,
                    local->params);

        return 0;
err:
        DHT_STACK_UNWIND (mknod, frame, -1, op_errno, NULL, NULL, NULL, NULL);
        return 0;
}

int
dht_mknod (call_frame_t *frame, xlator_t *this,
           loc_t *loc, mode_t mode, dev_t rdev, dict_t *params)
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

                STACK_WIND (frame, dht_newfile_cbk,
                            subvol, subvol->fops->mknod,
                            loc, mode, rdev, params);
        } else {
                avail_subvol = dht_free_disk_available_subvol (this, subvol);
                if (avail_subvol != subvol) {
                        /* Choose the minimum filled volume, and create the
                           files there */

                        local->params = dict_ref (params);
                        local->cached_subvol = avail_subvol;
                        local->mode = mode;
                        local->rdev = rdev;

                        dht_linkfile_create (frame,
                                             dht_mknod_linkfile_create_cbk,
                                             avail_subvol, subvol, loc);
                } else {
                        gf_log (this->name, GF_LOG_TRACE,
                                "creating %s on %s", loc->path, subvol->name);

                        STACK_WIND (frame, dht_newfile_cbk,
                                    subvol, subvol->fops->mknod,
                                    loc, mode, rdev, params);
                }
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (mknod, frame, -1, op_errno,
                          NULL, NULL, NULL, NULL);

        return 0;
}


int
dht_symlink (call_frame_t *frame, xlator_t *this,
             const char *linkname, loc_t *loc, dict_t *params)
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

        STACK_WIND (frame, dht_newfile_cbk,
                    subvol, subvol->fops->symlink,
                    linkname, loc, params);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (link, frame, -1, op_errno,
                          NULL, NULL, NULL, NULL);

        return 0;
}


int
dht_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
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
                            &local->loc);
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

        if (hashed_subvol != cached_subvol) {
                STACK_WIND (frame, dht_unlink_linkfile_cbk,
                            hashed_subvol, hashed_subvol->fops->unlink, loc);
        } else {
                STACK_WIND (frame, dht_unlink_cbk,
                            cached_subvol, cached_subvol->fops->unlink, loc);
        }
done:
        return 0;
err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL);

        return 0;
}


int
dht_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int op_ret, int op_errno,
              inode_t *inode, struct iatt *stbuf, struct iatt *preparent,
              struct iatt *postparent)
{
        call_frame_t *prev = NULL;
        dht_layout_t *layout = NULL;

        prev = cookie;

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

        WIPE (preparent);
        WIPE (postparent);

out:
        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (link, frame, op_ret, op_errno, inode, stbuf, preparent,
                          postparent);

        return 0;
}


int
dht_link_linkfile_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno,
                       inode_t *inode, struct iatt *stbuf,
                       struct iatt *preparent, struct iatt *postparent)
{
        dht_local_t  *local = NULL;
        xlator_t     *srcvol = NULL;

        if (op_ret == -1)
                goto err;

        local = frame->local;
        srcvol = local->linkfile.srcvol;

        STACK_WIND (frame, dht_link_cbk, srcvol, srcvol->fops->link,
                    &local->loc, &local->loc2);

        return 0;

err:
        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (link, frame, op_ret, op_errno, inode, stbuf, preparent,
                          postparent);

        return 0;
}


int
dht_link (call_frame_t *frame, xlator_t *this,
          loc_t *oldloc, loc_t *newloc)
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
                dht_linkfile_create (frame, dht_link_linkfile_cbk,
                                     cached_subvol, hashed_subvol, newloc);
        } else {
                STACK_WIND (frame, dht_link_cbk,
                            cached_subvol, cached_subvol->fops->link,
                            oldloc, newloc);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (link, frame, -1, op_errno, NULL, NULL, NULL, NULL);

        return 0;
}


int
dht_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno,
                fd_t *fd, inode_t *inode, struct iatt *stbuf,
                struct iatt *preparent, struct iatt *postparent)
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
                WIPE (preparent);
                WIPE (postparent);
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

out:
        DHT_STRIP_PHASE1_FLAGS (stbuf);
        DHT_STACK_UNWIND (create, frame, op_ret, op_errno, fd, inode, stbuf, preparent,
                          postparent);
        return 0;
}


int
dht_create_linkfile_create_cbk (call_frame_t *frame, void *cookie,
                                xlator_t *this,
                                int32_t op_ret, int32_t op_errno,
                                inode_t *inode, struct iatt *stbuf,
                                struct iatt *preparent, struct iatt *postparent)
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
                    local->fd, local->params);

        return 0;
err:
        DHT_STACK_UNWIND (create, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);
        return 0;
}

int
dht_create (call_frame_t *frame, xlator_t *this,
            loc_t *loc, int32_t flags, mode_t mode,
            fd_t *fd, dict_t *params)
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
                            &local->loc, flags, mode, fd, params);
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
                            loc, flags, mode, fd, params);
                goto done;
        }
        /* Choose the minimum filled volume, and create the
           files there */
        avail_subvol = dht_free_disk_available_subvol (this, subvol);
        if (avail_subvol != subvol) {
                local->params = dict_ref (params);
                local->flags = flags;
                local->mode = mode;

                local->cached_subvol = avail_subvol;
                local->hashed_subvol = subvol;
                gf_log (this->name, GF_LOG_TRACE,
                        "creating %s on %s (link at %s)", loc->path,
                        avail_subvol->name, subvol->name);
                dht_linkfile_create (frame,
                                     dht_create_linkfile_create_cbk,
                                     avail_subvol, subvol, loc);
                goto done;
        }
        gf_log (this->name, GF_LOG_TRACE,
                "creating %s on %s", loc->path, subvol->name);
        STACK_WIND (frame, dht_create_cbk,
                    subvol, subvol->fops->create,
                    loc, flags, mode, fd, params);
done:
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (create, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);

        return 0;
}


int
dht_mkdir_selfheal_cbk (call_frame_t *frame, void *cookie,
                        xlator_t *this,
                        int32_t op_ret, int32_t op_errno)
{
        dht_local_t   *local = NULL;
        dht_layout_t  *layout = NULL;

        local = frame->local;
        layout = local->selfheal.layout;

        if (op_ret == 0) {
                dht_layout_set (this, local->inode, layout);
                if (local->loc.parent) {
                        WIPE (&local->preparent);
                        WIPE (&local->postparent);
                }
        }

        DHT_STACK_UNWIND (mkdir, frame, op_ret, op_errno,
                          local->inode, &local->stbuf, &local->preparent,
                          &local->postparent);

        return 0;
}

int
dht_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int op_ret, int op_errno, inode_t *inode, struct iatt *stbuf,
               struct iatt *preparent, struct iatt *postparent)
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
                      struct iatt *preparent, struct iatt *postparent)
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
                            conf->subvolumes[i]->fops->mkdir,
                            &local->loc, local->mode, local->params);
        }
        return 0;
err:
        DHT_STACK_UNWIND (mkdir, frame, -1, op_errno, NULL, NULL, NULL, NULL);
        return 0;
}


int
dht_mkdir (call_frame_t *frame, xlator_t *this,
           loc_t *loc, mode_t mode, dict_t *params)
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
                    loc, mode, params);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (mkdir, frame, -1, op_errno, NULL, NULL, NULL, NULL);

        return 0;
}


int
dht_rmdir_selfheal_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno)
{
        dht_local_t  *local = NULL;

        local = frame->local;

        DHT_STACK_UNWIND (rmdir, frame, local->op_ret, local->op_errno,
                          &local->preparent, &local->postparent);

        return 0;
}


int
dht_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int op_ret, int op_errno, struct iatt *preparent,
               struct iatt *postparent)
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

                /* Track if rmdir succeeded on atleast one subvol*/
                local->fop_succeeded = 1;
                dht_iatt_merge (this, &local->preparent, preparent, prev->this);
                dht_iatt_merge (this, &local->postparent, postparent,
                                prev->this);
        }
unlock:
        UNLOCK (&frame->lock);


        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
                if (local->need_selfheal && local->fop_succeeded) {
                        local->layout =
                                dht_layout_get (this, local->loc.inode);

                        /* TODO: neater interface needed below */
                        local->stbuf.ia_type = local->loc.inode->ia_type;

                        uuid_copy (local->gfid, local->loc.inode->gfid);
                        dht_selfheal_restore (frame, dht_rmdir_selfheal_cbk,
                                              &local->loc, local->layout);
                } else {
                        if (local->loc.parent) {
                                WIPE (&local->preparent);
                                WIPE (&local->postparent);
                        }

                        DHT_STACK_UNWIND (rmdir, frame, local->op_ret,
                                          local->op_errno, &local->preparent,
                                          &local->postparent);
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

        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;
        local = frame->local;

        if (local->op_ret == -1)
                goto err;

        local->call_cnt = conf->subvolume_cnt;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                STACK_WIND (frame, dht_rmdir_cbk,
                            conf->subvolumes[i],
                            conf->subvolumes[i]->fops->rmdir,
                            &local->loc, local->flags);
        }

        return 0;

err:
        DHT_STACK_UNWIND (rmdir, frame, local->op_ret, local->op_errno,
                          &local->preparent, &local->postparent);
        return 0;
}


int
dht_rmdir_linkfile_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                               int op_ret, int op_errno, struct iatt *preparent,
                               struct iatt *postparent)
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

        local = frame->local;
        prev  = cookie;
        src   = prev->this;

        main_frame = local->main_frame;
        main_local = main_frame->local;

        if (op_ret != 0)
                goto err;

        if (check_is_linkfile (inode, stbuf, xattr) == 0) {
                main_local->op_ret  = -1;
                main_local->op_errno = ENOTEMPTY;

                gf_log (this->name, GF_LOG_WARNING,
                        "%s on %s found to be not a linkfile (type=0%o)",
                        local->loc.path, src->name, stbuf->ia_type);
                goto err;
        }

        STACK_WIND (frame, dht_rmdir_linkfile_unlink_cbk,
                    src, src->fops->unlink, &local->loc);
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
        int                 ret = 0;
        int                 build_ret = 0;
        gf_dirent_t        *trav = NULL;
        call_frame_t       *lookup_frame = NULL;
        dht_local_t        *lookup_local = NULL;
        dht_local_t        *local = NULL;

        local = frame->local;

        list_for_each_entry (trav, &entries->list, list) {
                if (strcmp (trav->d_name, ".") == 0)
                        continue;
                if (strcmp (trav->d_name, "..") == 0)
                        continue;
                if (check_is_linkfile (NULL, (&trav->d_stat), NULL) == 1) {
                        ret++;
                        continue;
                }

                /* this entry is either a directory which is neither "." nor "..",
                   or a non directory which is not a linkfile. the directory is to
                   be treated as non-empty
                */
                return 0;
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

                lookup_local = GF_CALLOC (sizeof (*local), 1,
                                          gf_dht_mt_dht_local_t);
                if (!lookup_local) {
                        goto err;
                }

                lookup_frame->local = lookup_local;
                lookup_local->main_frame = frame;

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

                STACK_WIND (lookup_frame, dht_rmdir_lookup_cbk,
                            src, src->fops->lookup,
                            &lookup_local->loc, NULL);
                ret++;
        }

        return ret;
err:
        DHT_STACK_DESTROY (lookup_frame);
        return 0;
}


int
dht_rmdir_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, gf_dirent_t *entries)
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
                       int op_ret, int op_errno, fd_t *fd)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = -1;
        call_frame_t *prev = NULL;


        local = frame->local;
        prev  = cookie;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "opendir on %s for %s failed (%s)",
                        prev->this->name, local->loc.path,
                        strerror (op_errno));
                local->op_ret = -1;
                local->op_errno = op_errno;
                goto err;
        }

        STACK_WIND (frame, dht_rmdir_readdirp_cbk,
                    prev->this, prev->this->fops->readdirp,
                    local->fd, 4096, 0);

        return 0;

err:
        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                dht_rmdir_do (frame, this);
        }

        return 0;
}


int
dht_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags)
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
                            loc, local->fd);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (rmdir, frame, -1, op_errno,
                          NULL, NULL);

        return 0;
}

int
dht_entrylk_cbk (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno)

{
        DHT_STACK_UNWIND (entrylk, frame, op_ret, op_errno);
        return 0;
}


int
dht_entrylk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc, const char *basename,
             entrylk_cmd cmd, entrylk_type type)
{
        xlator_t     *subvol = NULL;
        int           op_errno = -1;
        dht_local_t  *local = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);
        VALIDATE_OR_GOTO (loc->path, err);

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
                    volume, loc, basename, cmd, type);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (entrylk, frame, -1, op_errno);

        return 0;
}


int
dht_fentrylk_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno)

{
        DHT_STACK_UNWIND (fentrylk, frame, op_ret, op_errno);
        return 0;
}


int
dht_fentrylk (call_frame_t *frame, xlator_t *this,
              const char *volume, fd_t *fd, const char *basename,
              entrylk_cmd cmd, entrylk_type type)
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
                    volume, fd, basename, cmd, type);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (fentrylk, frame, -1, op_errno);

        return 0;
}


int
dht_forget (xlator_t *this, inode_t *inode)
{
        uint64_t      tmp_layout = 0;
        dht_layout_t *layout = NULL;

        inode_ctx_del (inode, this, &tmp_layout);

        if (!tmp_layout)
                return 0;

        layout = (dht_layout_t *)(long)tmp_layout;
        dht_layout_unref (this, layout);

        return 0;
}


int
dht_notify (xlator_t *this, int event, void *data, ...)
{
        xlator_t   *subvol = NULL;
        int         cnt    = -1;
        int         i      = -1;
        dht_conf_t *conf   = NULL;
        int         ret    = -1;
        int         propagate = 0;

        int         had_heard_from_all = 0;
        int         have_heard_from_all = 0;
        struct timeval  time = {0,};

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
                        exit(0);
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
        if (have_heard_from_all)
                propagate = 1;

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
        }

        ret = 0;
        if (propagate)
                ret = default_notify (this, event, data);

        return ret;
}
