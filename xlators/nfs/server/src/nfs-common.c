/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

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
#include "nfs-messages.h"

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
        char            *volname = NULL;
        char            *volptr = NULL;
        size_t           pathlen;
        xlator_t        *targetxl = NULL;

        if ((!cl) || (!path))
                return NULL;

        volname = strdupa (path);
        pathlen = strlen (volname);
        gf_msg_trace (GF_NFS, 0, "Subvolume search: %s", path);
        if (volname[0] == '/')
                volptr = &volname[1];
        else
                volptr = &volname[0];

        if (pathlen && volname[pathlen - 1] == '/')
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


void
nfs_loc_wipe (loc_t *loc)
{
        loc_wipe (loc);
}


int
nfs_loc_copy (loc_t *dst, loc_t *src)
{
        int  ret = -1;

        ret = loc_copy (dst, src);

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
                if (!gf_uuid_is_null (inode->gfid))
                        gf_uuid_copy (loc->gfid, inode->gfid);
        }

        if (parent)
                loc->parent = inode_ref (parent);

        if (path) {
                loc->path = gf_strdup (path);
                if (!loc->path) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, ENOMEM,
                                NFS_MSG_NO_MEMORY, "strdup failed");
                        goto loc_wipe;
                }
                loc->name = strrchr (loc->path, '/');
                if (loc->name)
                        loc->name++;
        }

        ret = 0;
loc_wipe:
        if (ret < 0)
                nfs_loc_wipe (loc);

        return ret;
}


int
nfs_inode_loc_fill (inode_t *inode, loc_t *loc, int how)
{
        char            *resolvedpath = NULL;
        inode_t         *parent = NULL;
        int             ret = -EFAULT;

        if ((!inode) || (!loc))
                return ret;

        /* If gfid is not null, then the inode is already linked to
         * the inode table, and not a newly created one. For newly
         * created inode, inode_path returns null gfid as the path.
         */
        if (!gf_uuid_is_null (inode->gfid)) {
                ret = inode_path (inode, NULL, &resolvedpath);
                if (ret < 0) {
                        gf_msg (GF_NFS, GF_LOG_ERROR, 0,
                                NFS_MSG_PATH_RESOLVE_FAIL, "path resolution "
                                "failed %s", resolvedpath);
                        goto err;
                }
        }

        if (resolvedpath == NULL) {
                char tmp_path[GFID_STR_PFX_LEN + 1] = {0,};
                snprintf (tmp_path, sizeof (tmp_path), "<gfid:%s>",
                          uuid_utoa (loc->gfid));
                resolvedpath = gf_strdup (tmp_path);
        } else {
                parent = inode_parent (inode, loc->pargfid, NULL);
        }

        ret = nfs_loc_fill (loc, inode, parent, resolvedpath);
        if (ret < 0) {
                gf_msg (GF_NFS, GF_LOG_ERROR, -ret,
                        NFS_MSG_LOC_FILL_RESOLVE_FAIL,
                        "loc fill resolution failed %s", resolvedpath);
                goto err;
        }

        ret = 0;
err:
        if (parent)
                inode_unref (parent);

        GF_FREE (resolvedpath);

        return ret;
}

int
nfs_gfid_loc_fill (inode_table_t *itable, uuid_t gfid, loc_t *loc, int how)
{
        int             ret = -EFAULT;
        inode_t         *inode = NULL;

        if (!loc)
                return ret;

        inode = inode_find (itable, gfid);
        if (!inode) {
		gf_msg_trace (GF_NFS, 0, "Inode not found in itable, will "
                              "try to create one.");
                if (how == NFS_RESOLVE_CREATE) {
			gf_msg_trace (GF_NFS, 0, "Inode needs to be created.");
                        inode = inode_new (itable);
                        if (!inode) {
                                gf_msg (GF_NFS, GF_LOG_ERROR, ENOMEM,
                                        NFS_MSG_NO_MEMORY, "Failed to "
                                        "allocate memory");
                                ret = -ENOMEM;
                                goto err;
                        }

                } else {
			gf_msg (GF_NFS, GF_LOG_ERROR, ENOENT,
                                NFS_MSG_INODE_NOT_FOUND, "Inode not found in "
                                "itable and no creation was requested.");
                        ret = -ENOENT;
                        goto err;
                }
        } else {
		gf_msg_trace (GF_NFS, 0, "Inode was found in the itable.");
	}

        gf_uuid_copy (loc->gfid, gfid);

        ret = nfs_inode_loc_fill (inode, loc, how);
	if (ret < 0) {
		gf_msg (GF_NFS, GF_LOG_ERROR, -ret,
                        NFS_MSG_INODE_LOC_FILL_ERROR,
                        "Inode loc filling failed.: %s", strerror (-ret));
		goto err;
	}

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
        return nfs_gfid_loc_fill (itable, rootgfid, loc, NFS_RESOLVE_EXIST);
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
                gf_msg (GF_NFS, GF_LOG_ERROR, -ret, NFS_MSG_PATH_RESOLVE_FAIL,
                        "path resolution failed %s", path);
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
nfs_entry_loc_fill (xlator_t *this, inode_table_t *itable, uuid_t pargfid,
                    char *entry, loc_t *loc, int how, gf_boolean_t *freshlookup)
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
        if (!parent || inode_ctx_get (parent, this, NULL))
                goto err;

        gf_uuid_copy (loc->pargfid, pargfid);

        ret = -2;
        entryinode = inode_grep (itable, parent, entry);
        if (!entryinode || inode_ctx_get (entryinode, this, NULL)) {
                if (how == NFS_RESOLVE_CREATE) {
                        /* Even though we'll create the inode and the loc for
                         * a missing inode, we still need to return -2 so
                         * that the caller can use the filled loc to call
                         * lookup.
                         */
                        if (!entryinode) {
                                entryinode = inode_new (itable);
                                if (freshlookup)
                                        *freshlookup = _gf_true;
                        }
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
                gf_msg (GF_NFS, GF_LOG_ERROR, -ret, NFS_MSG_PATH_RESOLVE_FAIL,
                        "path resolution failed %s", resolvedpath);
                ret = -3;
                goto err;
        }

        ret = nfs_loc_fill (loc, entryinode, parent, resolvedpath);
        if (ret < 0) {
                gf_msg (GF_NFS, GF_LOG_ERROR, 0, NFS_MSG_INODE_LOC_FILL_ERROR,
                        "loc_fill failed %s", resolvedpath);
                ret = -3;
        }

err:
        if (parent)
                inode_unref (parent);

        if (entryinode)
                inode_unref (entryinode);

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

        if (__is_root_gfid (gfid))
                return 0x1;

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


void
nfs_fix_generation (xlator_t *this, inode_t *inode)
{
        uint64_t                 raw_ctx        = 0;
        struct nfs_inode_ctx    *ictx           = NULL;
        struct nfs_state        *priv           = NULL;
        int                      ret            = -1;

        if (!inode) {
                return;
        }
        priv = this->private;

        if (inode_ctx_get(inode,this,&raw_ctx) == 0) {
                ictx = (struct nfs_inode_ctx *)raw_ctx;
                ictx->generation = priv->generation;
        }
        else {
                ictx = GF_CALLOC (1, sizeof (struct nfs_inode_ctx),
                                  gf_nfs_mt_inode_ctx);
                if (!ictx) {
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                NFS_MSG_NO_MEMORY,
                                "could not allocate nfs inode ctx");
                        return;
                }
                INIT_LIST_HEAD(&ictx->shares);
                ictx->generation = priv->generation;
                ret = inode_ctx_put (inode, this, (uint64_t)ictx);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                NFS_MSG_INODE_CTX_STORE_FAIL,
                                "could not store nfs inode ctx");
                        return;
                }
        }
}
