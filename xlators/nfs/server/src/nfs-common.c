/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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

#include "rpcsvc.h"
#include "dict.h"
#include "xlator.h"
#include "xdr-nfs3.h"
#include "msg-nfs3.h"
#include "iobuf.h"
#include "nfs-common.h"
#include "nfs-fops.h"
#include "nfs-mem-types.h"
#include "rpcsvc.h"
#include "iatt.h"

#include <libgen.h>

xlator_t *
nfs_xlid_to_xlator (xlator_list_t *cl, uint8_t xlid)
{
        xlator_t        *xl = NULL;
        uint8_t         id = 0;

        while (id <= xlid) {
                if (!cl) {
                        xl = NULL;
                        break;
                }

                xl = cl->xlator;
                cl = cl->next;
                id++;
        }

        return xl;
}


xlator_t *
nfs_path_to_xlator (xlator_list_t *cl, char *path)
{
        return NULL;
}


uint16_t
nfs_xlator_to_xlid (xlator_list_t *cl, xlator_t *xl)
{
        uint16_t        xlid = 0;

        if ((!cl) || (!xl))
                return 0;

        while (cl) {
                if (xl == cl->xlator)
                        break;
                cl = cl->next;
                ++xlid;
        }

        return xlid;
}


xlator_t *
nfs_mntpath_to_xlator (xlator_list_t *cl, char *path)
{
        char            volname[MNTPATHLEN];
        char            *volptr = NULL;
        int             pathlen = 0;
        xlator_t        *targetxl = NULL;

        if ((!cl) || (!path))
                return NULL;

        strcpy (volname, path);
        pathlen = strlen (volname);
        gf_log (GF_NFS, GF_LOG_TRACE, "Subvolume search: %s", path);
        if (volname[0] == '/')
                volptr = &volname[1];
        else
                volptr = &volname[0];

        if (volname[pathlen - 1] == '/')
                volname[pathlen - 1] = '\0';

        while (cl) {
                if (strcmp (volptr, cl->xlator->name) == 0) {
                        targetxl = cl->xlator;
                        break;
                }

                cl = cl->next;
        }

        return targetxl;

}


/* Returns 1 if the stat seems to be filled with zeroes. */
int
nfs_zero_filled_stat (struct iatt *buf)
{
        if (!buf)
                return 1;

        /* Do not use st_dev because it is transformed to store the xlator id
         * in place of the device number. Do not use st_ino because by this time
         * we've already mapped the root ino to 1 so it is not guaranteed to be
         * 0.
         */
        if ((buf->ia_nlink == 0) && (buf->ia_type == 0))
                return 1;

        return 0;
}


void
nfs_loc_wipe (loc_t *loc)
{
        if (!loc)
                return;

        if (loc->path) {
                GF_FREE ((char *)loc->path);
                loc->path = NULL;
        }

        if (loc->parent) {
                inode_unref (loc->parent);
                loc->parent = NULL;
	}

        if (loc->inode) {
                inode_unref (loc->inode);
                loc->inode = NULL;
        }

        loc->ino = 0;
}


int
nfs_loc_copy (loc_t *dst, loc_t *src)
{
	int ret = -1;

	dst->ino = src->ino;

	if (src->inode)
		dst->inode = inode_ref (src->inode);

	if (src->parent)
		dst->parent = inode_ref (src->parent);

	dst->path = gf_strdup (src->path);

	if (!dst->path) {
                gf_log (GF_NFS, GF_LOG_ERROR, "strdup failed");
		goto out;
        }

	dst->name = strrchr (dst->path, '/');
	if (dst->name)
		dst->name++;

	ret = 0;
out:
        if (ret == -1) {
                if (dst->inode)
                        inode_unref (dst->inode);

                if (dst->parent)
                        inode_unref (dst->parent);
        }

	return ret;
}


int
nfs_loc_fill (loc_t *loc, inode_t *inode, inode_t *parent, char *path)
{
        int     ret = -EFAULT;

        if (!loc)
                return ret;

        if (inode) {
                loc->inode = inode_ref (inode);
                loc->ino = inode->ino;
        }

        if (parent)
                loc->parent = inode_ref (parent);

        loc->path = gf_strdup (path);
        if (!loc->path) {
                gf_log (GF_NFS, GF_LOG_ERROR, "strdup failed");
                goto loc_wipe;
        }

        loc->name = strrchr (loc->path, '/');
        if (loc->name)
                loc->name++;
        else {
                gf_log (GF_NFS, GF_LOG_ERROR, "No / in path %s", loc->path);
                goto loc_wipe;
        }

        ret = 0;
loc_wipe:
        if (ret < 0)
                nfs_loc_wipe (loc);

        return ret;
}


int
nfs_inode_loc_fill (inode_t *inode, loc_t *loc)
{
        char            *resolvedpath = NULL;
        inode_t         *parent = NULL;
        int             ret = -EFAULT;

        if ((!inode) || (!loc))
                return ret;

        if ((inode) && (inode->ino == 1))
                goto ignore_parent;

        parent = inode_parent (inode, 0, NULL);
        if (!parent)
                goto err;

ignore_parent:
        ret = inode_path (inode, NULL, &resolvedpath);
        if (ret < 0) {
                gf_log (GF_NFS, GF_LOG_ERROR, "path resolution failed %s",
                                resolvedpath);
                goto err;
        }

        ret = nfs_loc_fill (loc, inode, parent, resolvedpath);
        if (ret < 0) {
                gf_log (GF_NFS, GF_LOG_ERROR, "loc fill resolution failed %s",
                                resolvedpath);
                goto err;
        }

err:
        if (parent)
                inode_unref (parent);

        if (resolvedpath)
                GF_FREE (resolvedpath);

        return ret;
}

int
nfs_gfid_loc_fill (inode_table_t *itable, uuid_t gfid, loc_t *loc)
{
        int             ret = -EFAULT;
        inode_t         *inode = NULL;

        if (!loc)
                return ret;

        inode = inode_find (itable, gfid);
        if (!inode) {
                ret = -ENOENT;
                goto err;
        }

        ret = nfs_inode_loc_fill (inode, loc);

err:
        if (inode)
                inode_unref (inode);
        return ret;
}


int
nfs_root_loc_fill (inode_table_t *itable, loc_t *loc)
{
        uuid_t  rootgfid = {0, };

        rootgfid[15] = 1;
        return nfs_gfid_loc_fill (itable, rootgfid, loc);
}



int
nfs_parent_inode_loc_fill (inode_t *parent, inode_t *entryinode, char *entry,
                           loc_t *loc)
{
        int             ret = -EFAULT;
        char            *path = NULL;

        if ((!parent) || (!entry) || (!loc) || (!entryinode))
                return ret;

        ret = inode_path (parent, entry, &path);
        if (ret < 0) {
                gf_log (GF_NFS, GF_LOG_ERROR, "path resolution failed %s",
                                path);
                goto err;
        }

        ret = nfs_loc_fill (loc, entryinode, parent, path);
        GF_FREE (path);
err:
        return ret;
}


/* Returns -1 if parent is not available, return -2 if the entry is not
 * available. In case the return is going to be -2, and how = NFS_RESOLVE_CREATE
 * it does however fill in the loc so that it can be used to perform a lookup
 * fop for the entry.
 * On other errors, return -3. 0 on success.
 */
int
nfs_entry_loc_fill (inode_table_t *itable, uuid_t pargfid, char *entry,
                    loc_t *loc, int how)
{
        inode_t         *parent = NULL;
        inode_t         *entryinode = NULL;
        int             ret = -3;
        char            *resolvedpath = NULL;
        int             pret = -3;

        if ((!itable) || (!entry) || (!loc))
                return ret;

        parent = inode_find (itable, pargfid);

        ret = -1;
        /* Will need hard resolution now */
        if (!parent)
                goto err;

        ret = -2;
        entryinode = inode_grep (itable, parent, entry);
        if (!entryinode) {
                if (how == NFS_RESOLVE_CREATE) {
                        /* Even though we'll create the inode and the loc for
                         * a missing inode, we still need to return -2 so
                         * that the caller can use the filled loc to call
                         * lookup.
                         */
                        entryinode = inode_new (itable);
                        /* Cannot change ret because that must
                         * continue to have -2.
                         */
                        pret = nfs_parent_inode_loc_fill (parent, entryinode,
                                                          entry, loc);
                        /* Only if parent loc fill fails, should we notify error
                         * through ret, otherwise, we still need to force a
                         * lookup by returning -2.
                         */
                        if (pret < 0)
			         ret = -3;
                }
                goto err;
        }

        ret = inode_path (parent, entry, &resolvedpath);
        if (ret < 0) {
                gf_log (GF_NFS, GF_LOG_ERROR, "path resolution failed %s",
                                resolvedpath);
                ret = -3;
                goto err;
        }

        ret = nfs_loc_fill (loc, entryinode, parent, resolvedpath);
        if (ret < 0) {
                gf_log (GF_NFS, GF_LOG_ERROR, "loc_fill failed %s",
                                resolvedpath);
                ret = -3;
        }

err:
        if (parent)
                inode_unref (parent);

        if (entryinode)
                inode_unref (entryinode);

        if (resolvedpath)
                GF_FREE (resolvedpath);

        return ret;
}


uint32_t
nfs_hash_gfid (uuid_t gfid)
{
        uint32_t                hash = 0;
        uint64_t                msb64 = 0;
        uint64_t                lsb64 = 0;
        uint32_t                a1 = 0;
        uint32_t                a2 = 0;
        uint32_t                a3 = 0;
        uint32_t                a4 = 0;
        uint32_t                b1 = 0;
        uint32_t                b2 = 0;

        memcpy (&msb64, &gfid[8], 8);
        memcpy (&lsb64, &gfid[0], 8);

        a1 = (msb64 << 32);
        a2 = (msb64 >> 32);
        a3 = (lsb64 << 32);
        a4 = (lsb64 >> 32);

        b1 = a1 ^ a4;
        b2 = a2 ^ a3;

        hash = b1 ^ b2;

        return hash;
}


