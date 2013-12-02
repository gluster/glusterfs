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
#include "glusterfs-acl.h"

#define DHT_SET_LAYOUT_RANGE(layout,i,srt,chunk,cnt,path)    do {       \
                layout->list[i].start = srt;                            \
                layout->list[i].stop  = srt + chunk - 1;                \
                                                                        \
                gf_log (this->name, GF_LOG_TRACE,                       \
                        "gave fix: %u - %u on %s for %s",               \
                        layout->list[i].start, layout->list[i].stop,    \
                        layout->list[i].xlator->name, path);            \
        } while (0)

#define DHT_RESET_LAYOUT_RANGE(layout)    do {                          \
                int cnt = 0;                                            \
                for (cnt = 0; cnt < layout->cnt; cnt++ ) {              \
                        layout->list[cnt].start = 0;                    \
                        layout->list[cnt].stop  = 0;                    \
                }                                                       \
        } while (0)

static uint32_t
dht_overlap_calc (dht_layout_t *old, int o, dht_layout_t *new, int n)
{
	if (o >= old->cnt || n >= new->cnt)
		return 0;

	if (old->list[o].err > 0 || new->list[n].err > 0)
		return 0;

        if (old->list[o].start == old->list[o].stop) {
                return 0;
        }

        if (new->list[n].start == new->list[n].stop) {
                return 0;
        }

	if ((old->list[o].start > new->list[n].stop) ||
	    (old->list[o].stop < new->list[n].start))
		return 0;

	return min (old->list[o].stop, new->list[n].stop) -
	        max (old->list[o].start, new->list[n].start) + 1;
}


int
dht_selfheal_dir_finish (call_frame_t *frame, xlator_t *this, int ret)
{
        dht_local_t  *local = NULL;

        local = frame->local;
        local->selfheal.dir_cbk (frame, NULL, frame->this, ret,
                                 local->op_errno, NULL);

        return 0;
}


int
dht_selfheal_dir_xattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int op_ret, int op_errno, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;
        xlator_t     *subvol = NULL;
        int           i = 0;
        dht_layout_t *layout = NULL;
        int           err = 0;
        int           this_call_cnt = 0;

        local = frame->local;
        layout = local->selfheal.layout;
        prev = cookie;
        subvol = prev->this;

        if (op_ret == 0)
                err = 0;
        else
                err = op_errno;

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].xlator == subvol) {
                        layout->list[i].err = err;
                        break;
                }
        }

        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                dht_selfheal_dir_finish (frame, this, 0);
        }

        return 0;
}


int
dht_selfheal_dir_xattr_persubvol (call_frame_t *frame, loc_t *loc,
                                  dht_layout_t *layout, int i,
                                  xlator_t *req_subvol)
{
        xlator_t          *subvol = NULL;
        dict_t            *xattr = NULL;
        int                ret = 0;
        xlator_t          *this = NULL;
        int32_t           *disk_layout = NULL;
        dht_local_t       *local = NULL;
        dht_conf_t        *conf = NULL;
        data_t            *data = NULL;

        local = frame->local;
        if (req_subvol)
                subvol = req_subvol;
        else
                subvol = layout->list[i].xlator;
        this = frame->this;

        GF_VALIDATE_OR_GOTO ("", this, err);
        GF_VALIDATE_OR_GOTO (this->name, layout, err);
        GF_VALIDATE_OR_GOTO (this->name, local, err);
        GF_VALIDATE_OR_GOTO (this->name, subvol, err);
        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;

        xattr = get_new_dict ();
        if (!xattr) {
                goto err;
        }

        ret = dht_disk_layout_extract (this, layout, i, &disk_layout);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: (subvol %s) failed to extract disk layout",
                        loc->path, subvol->name);
                goto err;
        }

        ret = dict_set_bin (xattr, conf->xattr_name, disk_layout, 4 * 4);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: (subvol %s) failed to set xattr dictionary",
                        loc->path, subvol->name);
                goto err;
        }
        disk_layout = NULL;

        gf_log (this->name, GF_LOG_TRACE,
                "setting hash range %u - %u (type %d) on subvolume %s for %s",
                layout->list[i].start, layout->list[i].stop,
                layout->type, subvol->name, loc->path);

        dict_ref (xattr);
        if (local->xattr) {
                data = dict_get (local->xattr, QUOTA_LIMIT_KEY);
                if (data) {
                        ret = dict_add (xattr, QUOTA_LIMIT_KEY, data);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                        "set quota limit key on %s",loc->path);
                        }
                }
        }
        if (!uuid_is_null (local->gfid))
                uuid_copy (loc->gfid, local->gfid);

        STACK_WIND (frame, dht_selfheal_dir_xattr_cbk,
                    subvol, subvol->fops->setxattr,
                    loc, xattr, 0, NULL);

        dict_unref (xattr);

        return 0;

err:
        if (xattr)
                dict_destroy (xattr);

        GF_FREE (disk_layout);

        dht_selfheal_dir_xattr_cbk (frame, subvol, frame->this,
                                    -1, ENOMEM, NULL);
        return 0;
}

int
dht_fix_dir_xattr (call_frame_t *frame, loc_t *loc, dht_layout_t *layout)
{
        dht_local_t *local = NULL;
        int          i = 0;
        int          count = 0;
        xlator_t    *this = NULL;
        dht_conf_t  *conf = NULL;
        dht_layout_t *dummy = NULL;

        local = frame->local;
        this = frame->this;
        conf = this->private;

        gf_log (this->name, GF_LOG_DEBUG,
                "writing the new range for all subvolumes");

        local->call_cnt = count = conf->subvolume_cnt;

        for (i = 0; i < layout->cnt; i++) {
                dht_selfheal_dir_xattr_persubvol (frame, loc, layout, i, NULL);

                if (--count == 0)
                        goto out;
        }
        /* if we are here, subvolcount > layout_count. subvols-per-directory
         * option might be set here. We need to clear out layout from the
         * non-participating subvolumes, else it will result in overlaps */
        dummy = dht_layout_new (this, 1);
        if (!dummy)
                goto out;
        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (_gf_false ==
                    dht_is_subvol_in_layout (layout, conf->subvolumes[i])) {
                        dht_selfheal_dir_xattr_persubvol (frame, loc, dummy, 0,
                                                          conf->subvolumes[i]);
                        if (--count == 0)
                                break;
                }
        }

        dht_layout_unref (this, dummy);
out:
        return 0;
}

int
dht_selfheal_dir_xattr (call_frame_t *frame, loc_t *loc, dht_layout_t *layout)
{
        dht_local_t *local = NULL;
        int          missing_xattr = 0;
        int          i = 0;
        xlator_t    *this = NULL;
        dht_conf_t   *conf = NULL;
        dht_layout_t *dummy = NULL;

        local = frame->local;
        this = frame->this;
        conf = this->private;

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err != -1 || !layout->list[i].stop) {
                        /* err != -1 would mean xattr present on the directory
                         * or the directory is non existent.
                         * !layout->list[i].stop would mean layout absent
                         */

                        continue;
                }
                missing_xattr++;
        }
        /* Also account for subvolumes with no-layout. Used for zero'ing out
         * the layouts and for setting quota key's if present */
        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (_gf_false ==
                    dht_is_subvol_in_layout (layout, conf->subvolumes[i])) {
                        missing_xattr++;
                }
        }
        gf_log (this->name, GF_LOG_TRACE,
                "%d subvolumes missing xattr for %s",
                missing_xattr, loc->path);

        if (missing_xattr == 0) {
                dht_selfheal_dir_finish (frame, this, 0);
                return 0;
        }

        local->call_cnt = missing_xattr;
        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err != -1 || !layout->list[i].stop)
                        continue;

                dht_selfheal_dir_xattr_persubvol (frame, loc, layout, i, NULL);

                if (--missing_xattr == 0)
                        break;
        }
        dummy = dht_layout_new (this, 1);
        if (!dummy)
                goto out;
        for (i = 0; i < conf->subvolume_cnt && missing_xattr; i++) {
                if (_gf_false ==
                    dht_is_subvol_in_layout (layout, conf->subvolumes[i])) {
                        dht_selfheal_dir_xattr_persubvol (frame, loc, dummy, 0,
                                                          conf->subvolumes[i]);
                        missing_xattr--;
                }
        }

        dht_layout_unref (this, dummy);
out:
        return 0;
}

int
dht_selfheal_dir_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                              int op_ret, int op_errno, struct iatt *statpre,
                              struct iatt *statpost, dict_t *xdata)
{
        dht_local_t   *local = NULL;
        dht_layout_t  *layout = NULL;
        int            this_call_cnt = 0;

        local  = frame->local;
        layout = local->selfheal.layout;

        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                dht_selfheal_dir_xattr (frame, &local->loc, layout);
        }

        return 0;
}


int
dht_selfheal_dir_setattr (call_frame_t *frame, loc_t *loc, struct iatt *stbuf,
                          int32_t valid, dht_layout_t *layout)
{
        int           missing_attr = 0;
        int           i     = 0;
        dht_local_t  *local = NULL;
        xlator_t     *this = NULL;

        local = frame->local;
        this = frame->this;

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err == -1)
                        missing_attr++;
        }

        if (missing_attr == 0) {
                dht_selfheal_dir_xattr (frame, loc, layout);
                return 0;
        }

        if (!uuid_is_null (local->gfid))
                uuid_copy (loc->gfid, local->gfid);

        local->call_cnt = missing_attr;
        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err == -1) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "setattr for %s on subvol %s",
                                loc->path, layout->list[i].xlator->name);

                        STACK_WIND (frame, dht_selfheal_dir_setattr_cbk,
                                    layout->list[i].xlator,
                                    layout->list[i].xlator->fops->setattr,
                                    loc, stbuf, valid, NULL);
                }
        }

        return 0;
}

int
dht_selfheal_dir_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int op_ret, int op_errno,
                            inode_t *inode, struct iatt *stbuf,
                            struct iatt *preparent, struct iatt *postparent,
                            dict_t *xdata)
{
        dht_local_t   *local = NULL;
        dht_layout_t  *layout = NULL;
        call_frame_t  *prev = NULL;
        xlator_t      *subvol = NULL;
        int            i = 0;
        int            this_call_cnt = 0;


        local  = frame->local;
        layout = local->selfheal.layout;
        prev   = cookie;
        subvol = prev->this;

        if ((op_ret == 0) || ((op_ret == -1) && (op_errno == EEXIST))) {
                for (i = 0; i < layout->cnt; i++) {
                        if (layout->list[i].xlator == subvol) {
                                layout->list[i].err = -1;
                                break;
                        }
                }
        }

        if (op_ret) {
                gf_log (this->name, ((op_errno == EEXIST) ? GF_LOG_DEBUG :
                                     GF_LOG_WARNING),
                        "selfhealing directory %s failed: %s",
                        local->loc.path, strerror (op_errno));
                goto out;
        }

        dht_iatt_merge (this, &local->stbuf, stbuf, prev->this);
        dht_iatt_merge (this, &local->preparent, preparent, prev->this);
        dht_iatt_merge (this, &local->postparent, postparent, prev->this);

out:
        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                dht_selfheal_dir_setattr (frame, &local->loc, &local->stbuf, 0xffffff, layout);
        }

        return 0;
}

void
dht_selfheal_dir_mkdir_setacl (dict_t *xattr, dict_t *dict)
{
        data_t          *acl_default = NULL;
        data_t          *acl_access = NULL;
        xlator_t        *this = NULL;
        int     ret = -1;

        GF_ASSERT (xattr);
        GF_ASSERT (dict);

        this = THIS;
        GF_ASSERT (this);

        acl_default = dict_get (xattr, POSIX_ACL_DEFAULT_XATTR);

        if (!acl_default) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "ACL_DEFAULT xattr not present");
                goto cont;
        }
        ret = dict_set (dict, POSIX_ACL_DEFAULT_XATTR, acl_default);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "Could not set ACL_DEFAULT xattr");
cont:
        acl_access = dict_get (xattr, POSIX_ACL_ACCESS_XATTR);
        if (!acl_access) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "ACL_ACCESS xattr not present");
                goto out;
        }
        ret = dict_set (dict, POSIX_ACL_ACCESS_XATTR, acl_access);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "Could not set ACL_ACCESS xattr");

out:
        return;
}

int
dht_selfheal_dir_mkdir (call_frame_t *frame, loc_t *loc,
                        dht_layout_t *layout, int force)
{
        int           missing_dirs = 0;
        int           i     = 0;
        int           ret   = -1;
        dht_local_t  *local = NULL;
        xlator_t     *this = NULL;
        dict_t       *dict = NULL;

        local = frame->local;
        this = frame->this;

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err == ENOENT || force)
                        missing_dirs++;
        }

        if (missing_dirs == 0) {
                dht_selfheal_dir_setattr (frame, loc, &local->stbuf, 0xffffffff, layout);
                return 0;
        }

        local->call_cnt = missing_dirs;
        if (!uuid_is_null (local->gfid)) {
                dict = dict_new ();
                if (!dict)
                        return -1;

                ret = dict_set_static_bin (dict, "gfid-req", local->gfid, 16);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: failed to set gfid in dict", loc->path);
        } else if (local->params) {
                /* Send the dictionary from higher layers directly */
                dict = dict_ref (local->params);
        }
        /* Set acls */
        if (local->xattr && dict)
                dht_selfheal_dir_mkdir_setacl (local->xattr, dict);

        if (!dict)
                gf_log (this->name, GF_LOG_WARNING,
                        "dict is NULL, need to make sure gfids are same");

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err == ENOENT || force) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "creating directory %s on subvol %s",
                                loc->path, layout->list[i].xlator->name);

                        STACK_WIND (frame, dht_selfheal_dir_mkdir_cbk,
                                    layout->list[i].xlator,
                                    layout->list[i].xlator->fops->mkdir,
                                    loc,
                                    st_mode_from_ia (local->stbuf.ia_prot,
                                                     local->stbuf.ia_type),
                                    0, dict);
                }
        }

        if (dict)
                dict_unref (dict);

        return 0;
}


int
dht_selfheal_layout_alloc_start (xlator_t *this, loc_t *loc,
                                 dht_layout_t *layout)
{
        int           start = 0;
        uint32_t      hashval = 0;
        int           ret = 0;

        ret = dht_hash_compute (this, layout->type, loc->path, &hashval);
        if (ret == 0) {
                start = (hashval % layout->cnt);
        }

        return start;
}

static inline int
dht_get_layout_count (xlator_t *this, dht_layout_t *layout, int new_layout)
{
        int i = 0;
        int j = 0;
        int err = 0;
        int count = 0;
        dht_conf_t *conf = NULL;

        /* Gets in use only for replace-brick, remove-brick */
        conf = this->private;
        for (i = 0; i < layout->cnt; i++) {
                for (j = 0; j < conf->subvolume_cnt; j++) {
                        if (conf->decommissioned_bricks[j] &&
                            conf->decommissioned_bricks[j] == layout->list[i].xlator) {
                                layout->list[i].err = EINVAL;
                                break;
                        }
                }
        }

        for (i = 0; i < layout->cnt; i++) {
                err = layout->list[i].err;
                if (err == -1 || err == 0 || err == ENOENT) {
			/* Setting list[i].err = -1 is an indication for
			   dht_selfheal_layout_new_directory() to assign
			   a range. We set it to -1 based on any one of
			   the three criteria:

			   - err == -1 already, which means directory
			     existed but layout was not set on it.

			   - err == 0, which means directory exists and
			     has an old layout piece which will be
			     overwritten now.

			   - err == ENOENT, which means directory does
			     not exist (possibly racing with mkdir or
			     finishing half done mkdir). The missing
			     directory will be attempted to be recreated.

			     It is important to note that it is safe
			     to race with mkdir() as self-heal and
			     mkdir are idempotent operations. Both will
			     strive to set the directory and layouts to
			     the same final state.
			*/
                        count++;
			if (!err)
				layout->list[i].err = -1;
                }
        }

        /* no subvolume has enough space, but can't stop directory creation */
        if (!count || !new_layout) {
                for (i = 0; i < layout->cnt; i++) {
                        err = layout->list[i].err;
                        if (err == ENOSPC) {
                                layout->list[i].err = -1;
                                count++;
                        }
                }
        }

        /* if layout->spread_cnt is set, check if it is <= available
         * subvolumes (down brick and decommissioned bricks are considered
         * un-availbale). Else return count (available up bricks) */
        count = ((layout->spread_cnt &&
                 (layout->spread_cnt <= count)) ?
                 layout->spread_cnt : ((count) ? count : 1));

        return count;
}


void dht_selfheal_layout_new_directory (call_frame_t *frame, loc_t *loc,
					dht_layout_t *new_layout);

void dht_layout_entry_swap (dht_layout_t *layout, int i, int j);
void dht_layout_range_swap (dht_layout_t *layout, int i, int j);

/*
 * It's a bit icky using local variables in a macro, but it makes the rest
 * of the code a lot clearer.
 */
#define OV_ENTRY(x,y)      table[x*new->cnt+y]

void
dht_selfheal_layout_maximize_overlap (call_frame_t *frame, loc_t *loc,
				      dht_layout_t *new, dht_layout_t *old)
{
	int           i            = 0;
	int           j            = 0;
	uint32_t      curr_overlap = 0;
	uint32_t      max_overlap  = 0;
	int           max_overlap_idx = -1;
	uint32_t      overlap      = 0;
        uint32_t     *table = NULL;

	dht_layout_sort_volname (old);
	/* Now both old_layout->list[] and new_layout->list[]
	   are match the same xlators/subvolumes. i.e,
	   old_layout->[i] and new_layout->[i] are referring
	   to the same subvolumes
	*/

        /* Build a table of overlaps between new[i] and old[j]. */
        table = alloca(sizeof(overlap)*old->cnt*new->cnt);
        if (!table) {
                return;
        }
        memset(table,0,sizeof(overlap)*old->cnt*new->cnt);
        for (i = 0; i < new->cnt; ++i) {
                for (j = 0; j < old->cnt; ++j) {
                        OV_ENTRY(i,j) = dht_overlap_calc(old,j,new,i);
                }
        }

	for (i = 0; i < new->cnt; i++) {
		if (new->list[i].err > 0) {
			/* Subvol might be marked for decommission
			   with EINVAL, or some other serious error
			   marked with positive errno.
			*/
			continue;
                }

                max_overlap = 0;
                max_overlap_idx = i;
                for (j = (i + 1); j < new->cnt; ++j) {
                        if (new->list[j].err > 0) {
			        /* Subvol might be marked for decommission
			        with EINVAL, or some other serious error
			        marked with positive errno.
			        */
			        continue;
                        }
                        /* Calculate the overlap now. */
                        curr_overlap = OV_ENTRY(i,i) + OV_ENTRY(j,j);
                        /* Calculate the overlap after the proposed swap. */
                        overlap = OV_ENTRY(i,j) + OV_ENTRY(j,i);
                        /* Are we better than status quo? */
                        if (overlap > curr_overlap) {
                                overlap -= curr_overlap;
                                /* Are we better than the previous choice? */
                                if (overlap > max_overlap) {
                                        max_overlap = overlap;
                                        max_overlap_idx = j;
                                }
                        }
                }

		if (max_overlap_idx != i) {
 			dht_layout_range_swap (new, i, max_overlap_idx);
                        /* Need to swap the table values too. */
                        for (j = 0; j < old->cnt; ++j) {
                                overlap = OV_ENTRY(i,j);
                                OV_ENTRY(i,j) = OV_ENTRY(max_overlap_idx,j);
                                OV_ENTRY(max_overlap_idx,j) = overlap;
                        }
                }
	}
}


dht_layout_t *
dht_fix_layout_of_directory (call_frame_t *frame, loc_t *loc,
                             dht_layout_t *layout)
{
        int           i            = 0;
        xlator_t     *this         = NULL;
        dht_layout_t *new_layout   = NULL;
        dht_conf_t   *priv         = NULL;
        dht_local_t  *local        = NULL;
        uint32_t      subvol_down  = 0;
        int           ret          = 0;

        this  = frame->this;
        priv  = this->private;
        local = frame->local;

        if (layout->type == DHT_HASH_TYPE_DM_USER) {
                gf_log (THIS->name, GF_LOG_DEBUG, "leaving %s alone",
                        loc->path);
                goto done;
        }

        new_layout = dht_layout_new (this, priv->subvolume_cnt);
        if (!new_layout)
                goto done;

        /* If a subvolume is down, do not re-write the layout. */
        ret = dht_layout_anomalies (this, loc, layout, NULL, NULL, NULL,
                                    &subvol_down, NULL, NULL);

        if (subvol_down || (ret == -1)) {
                gf_log (this->name, GF_LOG_WARNING, "%u subvolume(s) are down"
                        ". Skipping fix layout.", subvol_down);
                GF_FREE (new_layout);
                return NULL;
        }

        for (i = 0; i < new_layout->cnt; i++) {
		if (layout->list[i].err != ENOSPC)
			new_layout->list[i].err = layout->list[i].err;
		else
			new_layout->list[i].err = -1;

		new_layout->list[i].xlator = layout->list[i].xlator;
        }

	/* First give it a layout as though it is a new directory. This
	   ensures rotation to kick in */
        dht_layout_sort_volname (new_layout);
	dht_selfheal_layout_new_directory (frame, loc, new_layout);

	/* Now selectively re-assign ranges only when it helps */
	dht_selfheal_layout_maximize_overlap (frame, loc, new_layout, layout);

done:
        if (new_layout) {
                /* Now that the new layout has all the proper layout, change the
                   inode context */
                dht_layout_set (this, loc->inode, new_layout);

                /* Make sure the extra 'ref' for existing layout is removed */
                dht_layout_unref (this, local->layout);

                local->layout = new_layout;
        }

        return local->layout;
}


void
dht_selfheal_layout_new_directory (call_frame_t *frame, loc_t *loc,
                                   dht_layout_t *layout)
{
        xlator_t    *this = NULL;
        uint32_t     chunk = 0;
        int          i = 0;
        uint32_t     start = 0;
        int          cnt = 0;
        int          err = 0;
        int          start_subvol = 0;

        this = frame->this;

        cnt = dht_get_layout_count (this, layout, 1);

        chunk = ((unsigned long) 0xffffffff) / ((cnt) ? cnt : 1);

        start_subvol = dht_selfheal_layout_alloc_start (this, loc, layout);

        /* clear out the range, as we are re-computing here */
        DHT_RESET_LAYOUT_RANGE (layout);
        for (i = start_subvol; i < layout->cnt; i++) {
                err = layout->list[i].err;
                if (err == -1 || err == ENOENT) {
                        DHT_SET_LAYOUT_RANGE(layout, i, start, chunk,
                                             cnt, loc->path);
                        if (--cnt == 0) {
                                layout->list[i].stop = 0xffffffff;
                                goto done;
                        }
                        start += chunk;
                }
        }

        for (i = 0; i < start_subvol; i++) {
                err = layout->list[i].err;
                if (err == -1 || err == ENOENT) {
                        DHT_SET_LAYOUT_RANGE(layout, i, start, chunk,
                                             cnt, loc->path);
                        if (--cnt == 0) {
                                layout->list[i].stop = 0xffffffff;
                                goto done;
                        }
                        start += chunk;
                }
        }

done:
        return;
}

int
dht_selfheal_dir_getafix (call_frame_t *frame, loc_t *loc,
                          dht_layout_t *layout)
{
        dht_local_t *local = NULL;
        uint32_t     holes = 0;
        int          ret = -1;
        int          i = -1;
        uint32_t     overlaps = 0;

        local = frame->local;

        holes = local->selfheal.hole_cnt;
        overlaps = local->selfheal.overlaps_cnt;

        if (holes || overlaps) {
                dht_selfheal_layout_new_directory (frame, loc, layout);
                ret = 0;
        }

        for (i = 0; i < layout->cnt; i++) {
                /* directory not present */
                if (layout->list[i].err == ENOENT) {
                        ret = 0;
                        break;
                }
        }

        /* TODO: give a fix to these non-virgins */

        return ret;
}

int
dht_selfheal_new_directory (call_frame_t *frame,
                            dht_selfheal_dir_cbk_t dir_cbk,
                            dht_layout_t *layout)
{
        dht_local_t *local = NULL;

        local = frame->local;

        local->selfheal.dir_cbk = dir_cbk;
        local->selfheal.layout = dht_layout_ref (frame->this, layout);

        dht_layout_sort_volname (layout);
        dht_selfheal_layout_new_directory (frame, &local->loc, layout);
        dht_selfheal_dir_xattr (frame, &local->loc, layout);
        return 0;
}

int
dht_fix_directory_layout (call_frame_t *frame,
                          dht_selfheal_dir_cbk_t dir_cbk,
                          dht_layout_t *layout)
{
        dht_local_t  *local = NULL;
        dht_layout_t *tmp_layout = NULL;

        local = frame->local;

        local->selfheal.dir_cbk = dir_cbk;
        local->selfheal.layout = dht_layout_ref (frame->this, layout);

        /* No layout sorting required here */
        tmp_layout = dht_fix_layout_of_directory (frame, &local->loc, layout);
        if (!tmp_layout) {
                return -1;
        }
        dht_fix_dir_xattr (frame, &local->loc, tmp_layout);

        return 0;
}


int
dht_selfheal_directory (call_frame_t *frame, dht_selfheal_dir_cbk_t dir_cbk,
                        loc_t *loc, dht_layout_t *layout)
{
        dht_local_t *local    = NULL;
        uint32_t     down     = 0;
        uint32_t     misc     = 0;
        int          ret      = 0;
        xlator_t    *this     = NULL;

        local = frame->local;
        this = frame->this;

        dht_layout_anomalies (this, loc, layout,
                              &local->selfheal.hole_cnt,
                              &local->selfheal.overlaps_cnt,
                              NULL, &local->selfheal.down,
                              &local->selfheal.misc, NULL);

        down     = local->selfheal.down;
        misc     = local->selfheal.misc;

        local->selfheal.dir_cbk = dir_cbk;
        local->selfheal.layout = dht_layout_ref (this, layout);

        if (down) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%d subvolumes down -- not fixing", down);
                ret = 0;
                goto sorry_no_fix;
        }

        if (misc) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%d subvolumes have unrecoverable errors", misc);
                ret = 0;
                goto sorry_no_fix;
        }

        dht_layout_sort_volname (layout);
        ret = dht_selfheal_dir_getafix (frame, loc, layout);

        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "not able to form layout for the directory");
                goto sorry_no_fix;
        }

        dht_selfheal_dir_mkdir (frame, loc, layout, 0);

        return 0;

sorry_no_fix:
        /* TODO: need to put appropriate local->op_errno */
        dht_selfheal_dir_finish (frame, this, ret);

        return 0;
}


int
dht_selfheal_restore (call_frame_t *frame, dht_selfheal_dir_cbk_t dir_cbk,
                      loc_t *loc, dht_layout_t *layout)
{
        int          ret = 0;
        dht_local_t *local    = NULL;

        local = frame->local;

        local->selfheal.dir_cbk = dir_cbk;
        local->selfheal.layout = dht_layout_ref (frame->this, layout);

        ret = dht_selfheal_dir_mkdir (frame, loc, layout, 1);

        return ret;
}

int
dht_dir_attr_heal (void *data)
{
        call_frame_t    *frame = NULL;
        dht_local_t     *local = NULL;
        xlator_t        *subvol = NULL;
        xlator_t        *this  = NULL;
        dht_conf_t      *conf  = NULL;
        int              call_cnt = 0;
        int              ret   = -1;
        int              i     = 0;

        GF_VALIDATE_OR_GOTO ("dht", data, out);

        frame = data;
        local = frame->local;
        this = frame->this;
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", local, out);
        conf = this->private;
        GF_VALIDATE_OR_GOTO ("dht", conf, out);

        call_cnt = conf->subvolume_cnt;

        for (i = 0; i < call_cnt; i++) {
                subvol = conf->subvolumes[i];
                if (!subvol || (subvol == dht_first_up_subvol (this)))
                        continue;
                ret = syncop_setattr (subvol, &local->loc, &local->stbuf,
                                      (GF_SET_ATTR_UID | GF_SET_ATTR_GID),
                                      NULL, NULL);
                if (ret) {
                        gf_log ("dht", GF_LOG_ERROR, "Failed to set uid/gid on"
                                " %s on %s subvol (%s)", local->loc.path,
                                subvol->name, strerror (-ret));
                }
        }
out:
        return 0;
}

int
dht_dir_attr_heal_done (int ret, call_frame_t *sync_frame, void *data)
{
        DHT_STACK_DESTROY (sync_frame);
        return 0;
}
