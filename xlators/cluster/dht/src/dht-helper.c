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


#include "glusterfs.h"
#include "xlator.h"
#include "dht-common.h"

static inline int
dht_inode_ctx_set1 (xlator_t *this, inode_t *inode, xlator_t *subvol)
{
        uint64_t tmp_subvol = 0;

        tmp_subvol = (long)subvol;
        return inode_ctx_set1 (inode, this, &tmp_subvol);
}

int
dht_inode_ctx_get1 (xlator_t *this, inode_t *inode, xlator_t **subvol)
{
        int ret = -1;
        uint64_t tmp_subvol = 0;

        ret =  inode_ctx_get1 (inode, this, &tmp_subvol);
        if (tmp_subvol && subvol)
                *subvol = (xlator_t *)tmp_subvol;

        return ret;
}


int
dht_frame_return (call_frame_t *frame)
{
        dht_local_t *local = NULL;
        int          this_call_cnt = -1;

        if (!frame)
                return -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                this_call_cnt = --local->call_cnt;
        }
        UNLOCK (&frame->lock);

        return this_call_cnt;
}


static uint64_t
dht_bits_for (uint64_t num)
{
	uint64_t bits = 0, ctrl = 1;

	while (ctrl < num) {
		ctrl *= 2;
		bits ++;
	}

	return bits;
}

/*
 * A slightly "updated" version of the algorithm described in the commit log
 * is used here.
 *
 * The only enhancement is that:
 *
 * - The number of bits used by the backend filesystem for HUGE d_off which
 *   is described as 63, and
 * - The number of bits used by the d_off presented by the transformation
 *   upwards which is described as 64, are both made "configurable."
 */


#define BACKEND_D_OFF_BITS 63
#define PRESENT_D_OFF_BITS 63

#define ONE 1ULL
#define MASK (~0ULL)
#define PRESENT_MASK (MASK >> (64 - PRESENT_D_OFF_BITS))
#define BACKEND_MASK (MASK >> (64 - BACKEND_D_OFF_BITS))

#define TOP_BIT (ONE << (PRESENT_D_OFF_BITS - 1))
#define SHIFT_BITS (max (0, (BACKEND_D_OFF_BITS - PRESENT_D_OFF_BITS + 1)))

int
dht_itransform (xlator_t *this, xlator_t *subvol, uint64_t x, uint64_t *y_p)
{
        dht_conf_t *conf = NULL;
        int         cnt = 0;
        int         max = 0;
        uint64_t    y = 0;
        uint64_t    hi_mask = 0;
        uint64_t    off_mask = 0;
        int         max_bits = 0;

        if (x == ((uint64_t) -1)) {
                y = (uint64_t) -1;
                goto out;
        }

        conf = this->private;
        if (!conf)
                goto out;

        max = conf->subvolume_cnt;
        cnt = dht_subvol_cnt (this, subvol);

	if (max == 1) {
		y = x;
		goto out;
	}

        max_bits = dht_bits_for (max);

        hi_mask = ~(PRESENT_MASK >> (max_bits + 1));

        if (x & hi_mask) {
                /* HUGE d_off */
                off_mask = MASK << max_bits;
                y = TOP_BIT | ((x >> SHIFT_BITS) & off_mask) | cnt;
        } else {
                /* small d_off */
                y = ((x * max) + cnt);
        }

out:
        if (y_p)
                *y_p = y;

        return 0;
}

int
dht_filter_loc_subvol_key (xlator_t *this, loc_t *loc, loc_t *new_loc,
                           xlator_t **subvol)
{
        char          *new_name  = NULL;
        char          *new_path  = NULL;
        xlator_list_t *trav      = NULL;
        char           key[1024] = {0,};
        int            ret       = 0; /* not found */

        /* Why do other tasks if first required 'char' itself is not there */
        if (!new_loc || !loc || !loc->name || !strchr (loc->name, '@'))
                goto out;

        trav = this->children;
        while (trav) {
                snprintf (key, 1024, "*@%s:%s", this->name, trav->xlator->name);
                if (fnmatch (key, loc->name, FNM_NOESCAPE) == 0) {
                        new_name = GF_CALLOC(strlen (loc->name),
                                             sizeof (char),
                                             gf_common_mt_char);
                        if (!new_name)
                                goto out;
                        if (fnmatch (key, loc->path, FNM_NOESCAPE) == 0) {
                                new_path = GF_CALLOC(strlen (loc->path),
                                                     sizeof (char),
                                                     gf_common_mt_char);
                                if (!new_path)
                                        goto out;
                                strncpy (new_path, loc->path, (strlen (loc->path) -
                                                               strlen (key) + 1));
                        }
                        strncpy (new_name, loc->name, (strlen (loc->name) -
                                                       strlen (key) + 1));

                        if (new_loc) {
                                new_loc->path   = ((new_path) ? new_path:
                                                   gf_strdup (loc->path));
                                new_loc->name   = new_name;
                                new_loc->inode  = inode_ref (loc->inode);
                                new_loc->parent = inode_ref (loc->parent);
                        }
                        *subvol         = trav->xlator;
                        ret = 1;  /* success */
                        goto out;
                }
                trav = trav->next;
        }
out:
        if (!ret) {
                /* !success */
                GF_FREE (new_path);
                GF_FREE (new_name);
        }
        return ret;
}

int
dht_deitransform (xlator_t *this, uint64_t y, xlator_t **subvol_p,
                  uint64_t *x_p)
{
        dht_conf_t *conf = NULL;
        int         cnt = 0;
        int         max = 0;
        uint64_t    x = 0;
        xlator_t   *subvol = 0;
        int         max_bits = 0;
        uint64_t    off_mask = 0;
        uint64_t    host_mask = 0;

        if (!this->private)
                return -1;

        conf = this->private;
        max = conf->subvolume_cnt;

	if (max == 1) {
		x = y;
		cnt = 0;
		goto out;
	}

        if (y & TOP_BIT) {
                /* HUGE d_off */
                max_bits = dht_bits_for (max);
                off_mask = (MASK << max_bits);
                host_mask = ~(off_mask);

                x = ((y & ~TOP_BIT) & off_mask) << SHIFT_BITS;

                cnt = y & host_mask;
	} else {
                /* small d_off */
                cnt = y % max;
                x = y / max;
        }

out:
        subvol = conf->subvolumes[cnt];

        if (subvol_p)
                *subvol_p = subvol;

        if (x_p)
                *x_p = x;

        return 0;
}


void
dht_local_wipe (xlator_t *this, dht_local_t *local)
{
        if (!local)
                return;

        loc_wipe (&local->loc);
        loc_wipe (&local->loc2);

        if (local->xattr)
                dict_unref (local->xattr);

        if (local->inode)
                inode_unref (local->inode);

        if (local->layout) {
                dht_layout_unref (this, local->layout);
                local->layout = NULL;
        }

        loc_wipe (&local->linkfile.loc);

        if (local->linkfile.xattr)
                dict_unref (local->linkfile.xattr);

        if (local->linkfile.inode)
                inode_unref (local->linkfile.inode);

        if (local->fd) {
                fd_unref (local->fd);
                local->fd = NULL;
        }

        if (local->params) {
                dict_unref (local->params);
                local->params = NULL;
        }

        if (local->xattr_req)
                dict_unref (local->xattr_req);

        if (local->selfheal.layout) {
                dht_layout_unref (this, local->selfheal.layout);
                local->selfheal.layout = NULL;
        }

        GF_FREE (local->newpath);

        GF_FREE (local->key);

        GF_FREE (local->rebalance.vector);

        if (local->rebalance.iobref)
                iobref_unref (local->rebalance.iobref);

        mem_put (local);
}


dht_local_t *
dht_local_init (call_frame_t *frame, loc_t *loc, fd_t *fd, glusterfs_fop_t fop)
{
        dht_local_t *local = NULL;
        inode_t     *inode = NULL;
        int          ret   = 0;

        local = mem_get0 (THIS->local_pool);
        if (!local)
                goto out;

        if (loc) {
                ret = loc_copy (&local->loc, loc);
                if (ret)
                        goto out;

                inode = loc->inode;
        }

        if (fd) {
                local->fd = fd_ref (fd);
                if (!inode)
                        inode = fd->inode;
        }

        local->op_ret   = -1;
        local->op_errno = EUCLEAN;
        local->fop      = fop;

        if (inode) {
                local->layout   = dht_layout_get (frame->this, inode);
                local->cached_subvol = dht_subvol_get_cached (frame->this,
                                                              inode);
        }

        frame->local = local;

out:
        if (ret) {
                if (local)
                        mem_put (local);
                local = NULL;
        }
        return local;
}

xlator_t *
dht_first_up_subvol (xlator_t *this)
{
        dht_conf_t *conf = NULL;
        xlator_t   *child = NULL;
        int         i = 0;
        time_t      time = 0;

        conf = this->private;
        if (!conf)
                goto out;

        LOCK (&conf->subvolume_lock);
        {
                for (i = 0; i < conf->subvolume_cnt; i++) {
                        if (conf->subvol_up_time[i]) {
                                if (!time) {
                                        time = conf->subvol_up_time[i];
                                        child = conf->subvolumes[i];
                                } else if (time > conf->subvol_up_time[i]) {
                                        time  = conf->subvol_up_time[i];
                                        child = conf->subvolumes[i];
                                }
                        }
                }
        }
        UNLOCK (&conf->subvolume_lock);

out:
        return child;
}

xlator_t *
dht_last_up_subvol (xlator_t *this)
{
        dht_conf_t *conf = NULL;
        xlator_t   *child = NULL;
        int         i = 0;

        conf = this->private;
        if (!conf)
                goto out;

        LOCK (&conf->subvolume_lock);
        {
                for (i = conf->subvolume_cnt-1; i >= 0; i--) {
                        if (conf->subvolume_status[i]) {
                                child = conf->subvolumes[i];
                                break;
                        }
                }
        }
        UNLOCK (&conf->subvolume_lock);

out:
        return child;
}

xlator_t *
dht_subvol_get_hashed (xlator_t *this, loc_t *loc)
{
        dht_layout_t *layout = NULL;
        xlator_t     *subvol = NULL;

        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);

        if (__is_root_gfid (loc->gfid)) {
                subvol = dht_first_up_subvol (this);
                goto out;
        }

        GF_VALIDATE_OR_GOTO (this->name, loc->parent, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, out);

        layout = dht_layout_get (this, loc->parent);

        if (!layout) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "layout missing path=%s parent=%s",
                        loc->path, uuid_utoa (loc->parent->gfid));
                goto out;
        }

        subvol = dht_layout_search (this, layout, loc->name);

        if (!subvol) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "could not find subvolume for path=%s",
                        loc->path);
                goto out;
        }

out:
        if (layout) {
                dht_layout_unref (this, layout);
        }

        return subvol;
}


xlator_t *
dht_subvol_get_cached (xlator_t *this, inode_t *inode)
{
        dht_layout_t *layout = NULL;
        xlator_t     *subvol = NULL;

        GF_VALIDATE_OR_GOTO (this->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        layout = dht_layout_get (this, inode);

        if (!layout) {
                goto out;
        }

        subvol = layout->list[0].xlator;

out:
        if (layout) {
                dht_layout_unref (this, layout);
        }

        return subvol;
}


xlator_t *
dht_subvol_next (xlator_t *this, xlator_t *prev)
{
        dht_conf_t *conf = NULL;
        int         i = 0;
        xlator_t   *next = NULL;

        conf = this->private;
        if (!conf)
                goto out;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (conf->subvolumes[i] == prev) {
                        if ((i + 1) < conf->subvolume_cnt)
                                next = conf->subvolumes[i + 1];
                        break;
                }
        }

out:
        return next;
}

/* This func wraps around, if prev is actually the last subvol.
 */
xlator_t *
dht_subvol_next_available (xlator_t *this, xlator_t *prev)
{
        dht_conf_t *conf = NULL;
        int         i = 0;
        xlator_t   *next = NULL;

        conf = this->private;
        if (!conf)
                goto out;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (conf->subvolumes[i] == prev) {
                        /* if prev is last in conf->subvolumes, then wrap
                         * around.
                         */
                        if ((i + 1) < conf->subvolume_cnt) {
                                next = conf->subvolumes[i + 1];
                        } else {
                                next = conf->subvolumes[0];
                        }
                        break;
                }
        }

out:
        return next;
}
int
dht_subvol_cnt (xlator_t *this, xlator_t *subvol)
{
        int i = 0;
        int ret = -1;
        dht_conf_t *conf = NULL;

        conf = this->private;
        if (!conf)
                goto out;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (subvol == conf->subvolumes[i]) {
                        ret = i;
                        break;
                }
        }

out:
        return ret;
}


#define set_if_greater(a, b) do {               \
                if ((a) < (b))                  \
                        (a) = (b);              \
        } while (0)


#define set_if_greater_time(a, an, b, bn) do {                          \
                if (((a) < (b)) || (((a) == (b)) && ((an) < (bn)))){    \
                        (a) = (b);                                      \
                        (an) = (bn);                                    \
                }                                                       \
        } while (0)                                                     \


int
dht_iatt_merge (xlator_t *this, struct iatt *to,
                struct iatt *from, xlator_t *subvol)
{
        if (!from || !to)
                return 0;

        to->ia_dev      = from->ia_dev;

        uuid_copy (to->ia_gfid, from->ia_gfid);

        to->ia_ino      = from->ia_ino;
        to->ia_prot     = from->ia_prot;
        to->ia_type     = from->ia_type;
        to->ia_nlink    = from->ia_nlink;
        to->ia_rdev     = from->ia_rdev;
        to->ia_size    += from->ia_size;
        to->ia_blksize  = from->ia_blksize;
        to->ia_blocks  += from->ia_blocks;

        set_if_greater (to->ia_uid, from->ia_uid);
        set_if_greater (to->ia_gid, from->ia_gid);

        set_if_greater_time(to->ia_atime, to->ia_atime_nsec,
                            from->ia_atime, from->ia_atime_nsec);
        set_if_greater_time (to->ia_mtime, to->ia_mtime_nsec,
                             from->ia_mtime, from->ia_mtime_nsec);
        set_if_greater_time (to->ia_ctime, to->ia_ctime_nsec,
                             from->ia_ctime, from->ia_ctime_nsec);

        return 0;
}

int
dht_build_child_loc (xlator_t *this, loc_t *child, loc_t *parent, char *name)
{
        if (!child) {
                goto err;
        }

        if (strcmp (parent->path, "/") == 0)
                gf_asprintf ((char **)&child->path, "/%s", name);
        else
                gf_asprintf ((char **)&child->path, "%s/%s", parent->path, name);

        if (!child->path) {
                goto err;
        }

        child->name = strrchr (child->path, '/');
        if (child->name)
                child->name++;

        child->parent = inode_ref (parent->inode);
        child->inode = inode_new (parent->inode->table);

        if (!child->inode) {
                goto err;
        }

        return 0;
err:
        loc_wipe (child);
        return -1;
}



int
dht_init_subvolumes (xlator_t *this, dht_conf_t *conf)
{
        xlator_list_t *subvols = NULL;
        int            cnt = 0;

        if (!conf)
                return -1;

        for (subvols = this->children; subvols; subvols = subvols->next)
                cnt++;

        conf->subvolumes = GF_CALLOC (cnt, sizeof (xlator_t *),
                                      gf_dht_mt_xlator_t);
        if (!conf->subvolumes) {
                return -1;
        }
        conf->subvolume_cnt = cnt;

        cnt = 0;
        for (subvols = this->children; subvols; subvols = subvols->next)
                conf->subvolumes[cnt++] = subvols->xlator;

        conf->subvolume_status = GF_CALLOC (cnt, sizeof (char),
                                            gf_dht_mt_char);
        if (!conf->subvolume_status) {
                return -1;
        }

        conf->last_event = GF_CALLOC (cnt, sizeof (int),
                                      gf_dht_mt_char);
        if (!conf->last_event) {
                return -1;
        }

        conf->subvol_up_time = GF_CALLOC (cnt, sizeof (time_t),
                                          gf_dht_mt_subvol_time);
        if (!conf->subvol_up_time) {
                return -1;
        }

        conf->du_stats = GF_CALLOC (conf->subvolume_cnt, sizeof (dht_du_t),
                                    gf_dht_mt_dht_du_t);
        if (!conf->du_stats) {
                return -1;
        }

        conf->decommissioned_bricks = GF_CALLOC (cnt, sizeof (xlator_t *),
                                                 gf_dht_mt_xlator_t);
        if (!conf->decommissioned_bricks) {
                return -1;
        }

        return 0;
}




static int
dht_migration_complete_check_done (int op_ret, call_frame_t *frame, void *data)
{
        dht_local_t *local = NULL;

        local = frame->local;

        local->rebalance.target_op_fn (THIS, frame, op_ret);

        return 0;
}


int
dht_migration_complete_check_task (void *data)
{
        int           ret      = -1;
        xlator_t     *src_node = NULL;
        xlator_t     *dst_node = NULL;
        dht_local_t  *local    = NULL;
        dict_t       *dict     = NULL;
        dht_layout_t *layout   = NULL;
        struct iatt   stbuf    = {0,};
        xlator_t     *this     = NULL;
        call_frame_t *frame    = NULL;
        loc_t         tmp_loc  = {0,};
        char         *path     = NULL;
        dht_conf_t   *conf     = NULL;
        inode_t      *inode    = NULL;
        fd_t         *iter_fd  = NULL;
        uint64_t      tmp_subvol = 0;
        int           open_failed = 0;

        this  = THIS;
        frame = data;
        local = frame->local;
        conf = this->private;

        src_node = local->cached_subvol;

        if (!local->loc.inode && !local->fd) {
                local->op_errno = EINVAL;
                goto out;
        }

        inode = (!local->fd) ? local->loc.inode : local->fd->inode;

        /* getxattr on cached_subvol for 'linkto' value. Do path based getxattr
         * as root:root. If a fd is already open, access check wont be done*/

        if (!local->loc.inode) {
                ret = syncop_fgetxattr (src_node, local->fd, &dict,
                                        conf->link_xattr_name);
        } else {
                SYNCTASK_SETID (0, 0);
                ret = syncop_getxattr (src_node, &local->loc, &dict,
                                       conf->link_xattr_name);
                SYNCTASK_SETID (frame->root->uid, frame->root->gid);
        }

        if (!ret)
                dst_node = dht_linkfile_subvol (this, NULL, NULL, dict);

        if (ret) {
                if (!dht_inode_missing(-ret) || (!local->loc.inode)) {
                        local->op_errno = -ret;
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s: failed to get the 'linkto' xattr %s",
                                local->loc.path, strerror (-ret));
                        ret = -1;
                        goto out;
                }

                /* Need to do lookup on hashed subvol, then get the file */
                ret = syncop_lookup (this, &local->loc, NULL, &stbuf, NULL,
                                     NULL);
                if (ret) {
                        local->op_errno = -ret;
                        ret = -1;
                        goto out;
                }

                dst_node = dht_subvol_get_cached (this, local->loc.inode);
        }

        if (!dst_node) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: failed to get the destination node",
                        local->loc.path);
                ret = -1;
                local->op_errno = EINVAL;
                goto out;
        }

        /* lookup on dst */
        if (local->loc.inode) {
                ret = syncop_lookup (dst_node, &local->loc, NULL, &stbuf, NULL,
                                     NULL);

                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s: failed to lookup the file on %s",
                                local->loc.path, dst_node->name);
                        local->op_errno = -ret;
                        ret = -1;
                        goto out;
                }

                if (uuid_compare (stbuf.ia_gfid, local->loc.inode->gfid)) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s: gfid different on the target file on %s",
                                local->loc.path, dst_node->name);
                        ret = -1;
                        local->op_errno = EIO;
                        goto out;
                }
        }

        /* update inode ctx (the layout) */
        dht_layout_unref (this, local->layout);

        ret = dht_layout_preset (this, dst_node, inode);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s: could not set preset layout for subvol %s",
                        local->loc.path, dst_node->name);
                ret   = -1;
                local->op_errno = EINVAL;
                goto out;
        }

        layout = dht_layout_for_subvol (this, dst_node);
        if (!layout) {
                gf_log (this->name, GF_LOG_INFO,
                        "%s: no pre-set layout for subvolume %s",
                        local->loc.path, dst_node ? dst_node->name : "<nil>");
                ret = -1;
                local->op_errno = EINVAL;
                goto out;
        }

        ret = dht_layout_set (this, inode, layout);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: failed to set the new layout",
                        local->loc.path);
                local->op_errno = EINVAL;
                goto out;
        }

        local->cached_subvol = dst_node;
        ret = 0;

        /* once we detect the migration complete, the inode-ctx2 is no more
           required.. delete the ctx and also, it means, open() already
           done on all the fd of inode */
        ret = inode_ctx_reset1 (inode, this, &tmp_subvol);
        if (tmp_subvol)
                goto out;

        if (list_empty (&inode->fd_list))
                goto out;

        /* perform open as root:root. There is window between linkfile
         * creation(root:root) and setattr with the correct uid/gid
         */
        SYNCTASK_SETID(0, 0);

        /* perform 'open()' on all the fd's present on the inode */
        tmp_loc.inode = inode;
        inode_path (inode, NULL, &path);
        if (path)
                tmp_loc.path = path;
        list_for_each_entry (iter_fd, &inode->fd_list, inode_list) {
                if (fd_is_anonymous (iter_fd))
                        continue;

                ret = syncop_open (dst_node, &tmp_loc,
                                   iter_fd->flags, iter_fd);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to open "
                                "the fd (%p, flags=0%o) on file %s @ %s",
                                iter_fd, iter_fd->flags, path, dst_node->name);
                        open_failed = 1;
                        local->op_errno = -ret;
                        ret = -1;
                }
        }
        GF_FREE (path);

        SYNCTASK_SETID (frame->root->uid, frame->root->gid);

        if (open_failed) {
                ret = -1;
                goto out;
        }
        ret = 0;
out:

        return ret;
}

int
dht_rebalance_complete_check (xlator_t *this, call_frame_t *frame)
{
        int         ret     = -1;

        ret = synctask_new (this->ctx->env, dht_migration_complete_check_task,
                            dht_migration_complete_check_done,
                            frame, frame);
        return ret;
}

/* During 'in-progress' state, both nodes should have the file */
static int
dht_inprogress_check_done (int op_ret, call_frame_t *sync_frame, void *data)
{
        dht_local_t *local = NULL;

        local = sync_frame->local;

        local->rebalance.target_op_fn (THIS, sync_frame, op_ret);

        return 0;
}

static int
dht_rebalance_inprogress_task (void *data)
{
        int           ret      = -1;
        xlator_t     *src_node = NULL;
        xlator_t     *dst_node = NULL;
        dht_local_t  *local    = NULL;
        dict_t       *dict     = NULL;
        call_frame_t *frame    = NULL;
        xlator_t     *this     = NULL;
        char         *path     = NULL;
        struct iatt   stbuf    = {0,};
        loc_t         tmp_loc  = {0,};
        dht_conf_t   *conf     = NULL;
        inode_t      *inode    = NULL;
        fd_t         *iter_fd  = NULL;
        int           open_failed = 0;

        this  = THIS;
        frame = data;
        local = frame->local;
        conf = this->private;

        src_node = local->cached_subvol;

        if (!local->loc.inode && !local->fd)
                goto out;

        inode = (!local->fd) ? local->loc.inode : local->fd->inode;

        /* getxattr on cached_subvol for 'linkto' value. Do path based getxattr
         * as root:root. If a fd is already open, access check wont be done*/
        if (local->loc.inode) {
                SYNCTASK_SETID (0, 0);
                ret = syncop_getxattr (src_node, &local->loc, &dict,
                                       conf->link_xattr_name);
                SYNCTASK_SETID (frame->root->uid, frame->root->gid);
        } else {
                ret = syncop_fgetxattr (src_node, local->fd, &dict,
                                        conf->link_xattr_name);
        }

        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: failed to get the 'linkto' xattr %s",
                        local->loc.path, strerror (-ret));
                ret = -1;
                goto out;
        }

        dst_node = dht_linkfile_subvol (this, NULL, NULL, dict);
        if (!dst_node) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: failed to get the 'linkto' xattr from dict",
                        local->loc.path);
                ret = -1;
                goto out;
        }

        local->rebalance.target_node = dst_node;

        if (local->loc.inode) {
                /* lookup on dst */
                ret = syncop_lookup (dst_node, &local->loc, NULL,
                                     &stbuf, NULL, NULL);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s: failed to lookup the file on %s",
                                local->loc.path, dst_node->name);
                        ret = -1;
                        goto out;
                }

                if (uuid_compare (stbuf.ia_gfid, local->loc.inode->gfid)) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s: gfid different on the target file on %s",
                                local->loc.path, dst_node->name);
                        ret = -1;
                        goto out;
                }
        }

        ret = 0;

        if (list_empty (&inode->fd_list))
                goto done;

        /* perform open as root:root. There is window between linkfile
         * creation(root:root) and setattr with the correct uid/gid
         */
        SYNCTASK_SETID (0, 0);

        tmp_loc.inode = inode;
        inode_path (inode, NULL, &path);
        if (path)
                tmp_loc.path = path;

        list_for_each_entry (iter_fd, &inode->fd_list, inode_list) {
                if (fd_is_anonymous (iter_fd))
                        continue;

                ret = syncop_open (dst_node, &tmp_loc,
                                   iter_fd->flags, iter_fd);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to send open "
                                "the fd (%p, flags=0%o) on file %s @ %s",
                                iter_fd, iter_fd->flags, path, dst_node->name);
                        ret = -1;
                        open_failed = 1;
                }
        }
        GF_FREE (path);

        SYNCTASK_SETID (frame->root->uid, frame->root->gid);

        if (open_failed) {
                ret = -1;
                goto out;
        }

done:
        ret = dht_inode_ctx_set1 (this, inode, dst_node);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: failed to set inode-ctx target file at %s",
                        local->loc.path, dst_node->name);
                goto out;
        }

        ret = 0;
out:
        return ret;
}

int
dht_rebalance_in_progress_check (xlator_t *this, call_frame_t *frame)
{

        int         ret     = -1;

        ret = synctask_new (this->ctx->env, dht_rebalance_inprogress_task,
                            dht_inprogress_check_done,
                            frame, frame);
        return ret;
}

int
dht_inode_ctx_layout_set (inode_t *inode, xlator_t *this,
                          dht_layout_t *layout_int)
{
        dht_inode_ctx_t         *ctx            = NULL;
        int                      ret            = -1;

        ret = dht_inode_ctx_get (inode, this, &ctx);
        if (!ret && ctx) {
                ctx->layout = layout_int;
        } else {
                ctx = GF_CALLOC (1, sizeof (*ctx), gf_dht_mt_inode_ctx_t);
                if (!ctx)
                        return ret;
                ctx->layout = layout_int;
        }

        ret = dht_inode_ctx_set (inode, this, ctx);

        return ret;
}

int
dht_inode_ctx_time_update (inode_t *inode, xlator_t *this, struct iatt *stat,
                           int32_t post)
{
        dht_inode_ctx_t         *ctx            = NULL;
        dht_stat_time_t         *time           = 0;
        int                      ret            = -1;

        GF_VALIDATE_OR_GOTO (this->name, stat, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        ret = dht_inode_ctx_get (inode, this, &ctx);

        if (ret) {
                ctx = GF_CALLOC (1, sizeof (*ctx), gf_dht_mt_inode_ctx_t);
                if (!ctx)
                        return -1;
        }

        time = &ctx->time;

        DHT_UPDATE_TIME(time->mtime, time->mtime_nsec,
                        stat->ia_mtime, stat->ia_mtime_nsec, inode, post);
        DHT_UPDATE_TIME(time->ctime, time->ctime_nsec,
                        stat->ia_ctime, stat->ia_ctime_nsec, inode, post);
        DHT_UPDATE_TIME(time->atime, time->atime_nsec,
                        stat->ia_atime, stat->ia_atime_nsec, inode, post);

        ret = dht_inode_ctx_set (inode, this, ctx);
out:
        return 0;
}

int
dht_inode_ctx_get (inode_t *inode, xlator_t *this, dht_inode_ctx_t **ctx)
{
        int             ret     = -1;
        uint64_t        ctx_int = 0;

        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        ret = inode_ctx_get (inode, this, &ctx_int);

        if (ret)
                return ret;

        if (ctx)
                *ctx = (dht_inode_ctx_t *) ctx_int;
out:
        return ret;
}

int dht_inode_ctx_set (inode_t *inode, xlator_t *this, dht_inode_ctx_t *ctx)
{
        int             ret = -1;
        uint64_t        ctx_int = 0;

        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);
        GF_VALIDATE_OR_GOTO (this->name, ctx, out);

        ctx_int = (long)ctx;
        ret = inode_ctx_set (inode, this, &ctx_int);
out:
        return ret;
}
