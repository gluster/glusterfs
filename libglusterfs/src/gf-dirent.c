/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/



#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "compat.h"
#include "xlator.h"
#include "syncop.h"

#define ONE 1ULL
#define PRESENT_D_OFF_BITS 63
#define BACKEND_D_OFF_BITS 63
#define TOP_BIT (ONE << (PRESENT_D_OFF_BITS - 1))
#define MASK (~0ULL)
#define SHIFT_BITS (max (0, (BACKEND_D_OFF_BITS - PRESENT_D_OFF_BITS + 1)))
#define PRESENT_MASK (MASK >> (64 - PRESENT_D_OFF_BITS))

static uint64_t
bits_for (uint64_t num)
{
	uint64_t bits = 0, ctrl = 1;

	while (ctrl < num) {
		ctrl *= 2;
		bits++;
	}

	return bits;
}

int
gf_deitransform(xlator_t *this,
                uint64_t offset)
{
        int         cnt = 0;
        int         max = 0;
        int         max_bits = 0;
        uint64_t    off_mask = 0;
        uint64_t    host_mask = 0;

        max = glusterfs_get_leaf_count(this->graph);

	if (max == 1) {
		cnt = 0;
		goto out;
	}

        if (offset & TOP_BIT) {
                /* HUGE d_off */
                max_bits = bits_for (max);
                off_mask = (MASK << max_bits);
                host_mask = ~(off_mask);

                cnt = offset & host_mask;
	} else {
                /* small d_off */
                cnt = offset % max;
        }
out:
        return cnt;
}

uint64_t
gf_dirent_orig_offset(xlator_t *this,
                      uint64_t offset)
{
        int         max = 0;
        int         max_bits = 0;
        uint64_t    off_mask = 0;
        uint64_t    orig_offset;

        max = glusterfs_get_leaf_count(this->graph);

	if (max == 1) {
                orig_offset = offset;
		goto out;
	}

        if (offset & TOP_BIT) {
                /* HUGE d_off */
                max_bits = bits_for (max);
                off_mask = (MASK << max_bits);
                orig_offset = ((offset & ~TOP_BIT) & off_mask) << SHIFT_BITS;
	} else {
                /* small d_off */
                orig_offset = offset / max;
        }
out:
        return orig_offset;
}

int
gf_itransform (xlator_t *this, uint64_t x, uint64_t *y_p, int client_id)
{
        int         max = 0;
        uint64_t    y = 0;
        uint64_t    hi_mask = 0;
        uint64_t    off_mask = 0;
        int         max_bits = 0;

        if (x == ((uint64_t) -1)) {
                y = (uint64_t) -1;
                goto out;
        }

        if (!x) {
                y = 0;
                goto out;
        }

        max = glusterfs_get_leaf_count(this->graph);

	if (max == 1) {
		y = x;
		goto out;
	}

        max_bits = bits_for (max);

        hi_mask = ~(PRESENT_MASK >> (max_bits + 1));

        if (x & hi_mask) {
                /* HUGE d_off */
                off_mask = MASK << max_bits;
                y = TOP_BIT | ((x >> SHIFT_BITS) & off_mask) | client_id;
        } else {
                /* small d_off */
                y = ((x * max) + client_id);
        }

out:
        if (y_p)
                *y_p = y;

        return 0;
}

gf_dirent_t *
gf_dirent_for_name (const char *name)
{
        gf_dirent_t *gf_dirent = NULL;

        /* TODO: use mem-pool */
        gf_dirent = GF_CALLOC (gf_dirent_size (name), 1,
                               gf_common_mt_gf_dirent_t);
        if (!gf_dirent)
                return NULL;

        INIT_LIST_HEAD (&gf_dirent->list);
        strcpy (gf_dirent->d_name, name);

        gf_dirent->d_off = 0;
        gf_dirent->d_ino = -1;
        gf_dirent->d_type = 0;
        gf_dirent->d_len = strlen (name);

        return gf_dirent;
}

void
gf_dirent_entry_free (gf_dirent_t *entry)
{
        if (!entry)
                return;

        if (entry->dict)
                dict_unref (entry->dict);
        if (entry->inode)
                inode_unref (entry->inode);

        list_del_init (&entry->list);
        GF_FREE (entry);
}

void
gf_dirent_free (gf_dirent_t *entries)
{
        gf_dirent_t *entry = NULL;
        gf_dirent_t *tmp = NULL;

        if (!entries)
                return;

        if (list_empty (&entries->list))
                return;

        list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                gf_dirent_entry_free (entry);
        }
}

gf_dirent_t *
entry_copy (gf_dirent_t *source)
{
        gf_dirent_t *sink = NULL;

        sink = gf_dirent_for_name (source->d_name);
        if (!sink)
                return NULL;

        sink->d_off = source->d_off;
        sink->d_ino = source->d_ino;
        sink->d_type = source->d_type;
        sink->d_stat = source->d_stat;
        sink->d_len  = source->d_len;

	if (source->inode)
		sink->inode = inode_ref (source->inode);

	if (source->dict)
		sink->dict = dict_ref (source->dict);
        return sink;
}

void
gf_link_inode_from_dirent (xlator_t *this, inode_t *parent, gf_dirent_t *entry)
{
        inode_t     *link_inode = NULL;
        inode_t     *tmp        = NULL;

        if (!entry->inode)
                return;
        link_inode = inode_link (entry->inode, parent,
                                 entry->d_name, &entry->d_stat);
        if (!link_inode)
                return;

        inode_lookup (link_inode);
        tmp = entry->inode;
        entry->inode = link_inode;
        inode_unref (tmp);
}

/* TODO: Currently, with this function, we will be breaking the
   policy of 1-1 mapping of kernel nlookup refs with our inode_t's
   nlookup count.
   Need more thoughts before finalizing this function
*/
int
gf_link_inodes_from_dirent (xlator_t *this, inode_t *parent,
                            gf_dirent_t *entries)
{
        gf_dirent_t *entry      = NULL;

        list_for_each_entry (entry, &entries->list, list) {
                gf_link_inode_from_dirent (this, parent, entry);
        }

        return 0;
}

int
gf_fill_iatt_for_dirent (gf_dirent_t *entry, inode_t *parent, xlator_t *subvol)
{
        loc_t                   loc             = {0, };
        int                     ret             = -1;
        char                   *path            = NULL;
        struct iatt             iatt            = {0,};

        loc.inode = inode_grep (parent->table, parent, entry->d_name);
        if (!loc.inode) {
                loc.inode = inode_new (parent->table);
                gf_uuid_copy (loc.inode->gfid, entry->d_stat.ia_gfid);
        }

        gf_uuid_copy (loc.pargfid, parent->gfid);
        loc.name = entry->d_name;
        loc.parent = inode_ref (parent);
        ret = inode_path (loc.inode, entry->d_name, &path);
        loc.path = path;
        if (ret < 0)
                goto out;

        ret = syncop_lookup (subvol, &loc, &iatt, NULL, NULL, NULL);
        if (ret)
                goto out;

        entry->d_stat = iatt;
        entry->inode = inode_ref (loc.inode);
        /* We don't need to link inode here, because as part of readdirp_cbk
         * we will link all dirents.
         *
         * Since we did a proper lookup, we don't need to set need_lookup
         * flag.
         */

        ret = 0;
out:
        loc_wipe (&loc);
        return ret;
}
