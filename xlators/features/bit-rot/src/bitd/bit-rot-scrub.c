/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
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

#include <ctype.h>
#include <sys/uio.h>

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"

#include "bit-rot.h"
#include "bit-rot-scrub.h"
#include <pthread.h>

static inline int32_t
bitd_fetch_signature (xlator_t *this,
                      br_child_t *child, fd_t *fd, br_isignature_out_t *sign)
{
        int32_t ret = -1;
        dict_t *xattr = NULL;
        br_isignature_out_t *sigptr = NULL;

        ret = syncop_fgetxattr (child->xl, fd, &xattr,
                               GLUSTERFS_GET_OBJECT_SIGNATURE, NULL);
        if (ret < 0) {
                br_log_object (this, "getxattr", fd->inode->gfid, -ret);
                goto out;
        }

        ret = dict_get_ptr (xattr, GLUSTERFS_GET_OBJECT_SIGNATURE,
                            (void **)&sigptr);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to extract signature info [GFID: %s]",
                        uuid_utoa (fd->inode->gfid));
                goto unref_dict;
        }

        ret = 0;
        (void) memcpy (sign, sigptr, sizeof (br_isignature_out_t));

 unref_dict:
        dict_unref (xattr);
 out:
        return ret;

}

static inline int32_t
bitd_scrub_post_compute_check (xlator_t *this,
                               br_child_t *child,
                               br_isignature_out_t *sign, fd_t *fd)
{
        int32_t ret = 0;

        ret = bitd_fetch_signature (this, child, fd, sign);
        if (ret)
                goto out;
        if (sign->stale)
                ret = -1;

 out:
        return ret;

}

static inline int32_t
bitd_scrub_pre_compute_check (xlator_t *this, br_child_t *child, fd_t *fd)
{
        int32_t ret = -1;
        br_isignature_out_t sign = {0,};

        /* if the object is already marked bad, don't bother checking */
        if (bitd_is_bad_file (this, child, NULL, fd))
                goto out;

        /* else, check for signature staleness */
        ret = bitd_fetch_signature (this, child, fd, &sign);
        if (ret)
                goto out;
        if (sign.stale) {
                ret = -1;
                goto out;
        }

        ret = 0;

 out:
        return ret;
}

static inline int
bitd_compare_ckum (xlator_t *this,
                   br_isignature_out_t *sign,
                   unsigned char *md, inode_t *linked_inode,
                   gf_dirent_t *entry, fd_t *fd, br_child_t *child)
{
        int   ret = -1;
        dict_t xattr = {0,};

        GF_VALIDATE_OR_GOTO ("bit-rot", this, out);
        GF_VALIDATE_OR_GOTO (this->name, sign, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, child, out);
        GF_VALIDATE_OR_GOTO (this->name, linked_inode, out);
        GF_VALIDATE_OR_GOTO (this->name, md, out);
        GF_VALIDATE_OR_GOTO (this->name, entry, out);

        if (strncmp (sign->signature, (char *)md, strlen (sign->signature))) {
                gf_log (this->name, GF_LOG_WARNING, "checksums does not match "
                        "for the entry %s (gfid: %s)", entry->d_name,
                        uuid_utoa (linked_inode->gfid));
                ret = dict_set_int32 (&xattr, "trusted.glusterfs.bad-file",
                                      _gf_true);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "dict-set for "
                                "bad-file (entry: %s, gfid: %s) failed",
                                entry->d_name, uuid_utoa (linked_inode->gfid));
                        goto out;
                }

                ret = syncop_fsetxattr (child->xl, fd, &xattr, 0);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "setxattr to mark "
                                "the file %s (gfid: %s) as bad failed",
                                entry->d_name, uuid_utoa (linked_inode->gfid));
                        goto out;
                }
        }

out:
        return ret;
}

/**
 * This is the scrubber. As of now there's a mix of fd and inode
 * operations. Better to move them to fd based to be clean and
 * avoid code cluttering.
 */
int
bitd_start_scrub (xlator_t *subvol,
                  gf_dirent_t *entry, loc_t *parent, void *data)
{
        int32_t              ret          = -1;
        fd_t                *fd           = NULL;
        loc_t                loc          = {0, };
        struct iatt          iatt         = {0, };
        struct iatt          parent_buf   = {0, };
        pid_t                pid          = 0;
        br_child_t          *child        = NULL;
        xlator_t            *this         = NULL;
        unsigned char       *md           = NULL;
        inode_t             *linked_inode = NULL;
        br_isignature_out_t  sign         = {0,};

        GF_VALIDATE_OR_GOTO ("bit-rot", subvol, out);
        GF_VALIDATE_OR_GOTO ("bit-rot", data, out);

        child = data;
        this = child->this;

        pid = GF_CLIENT_PID_SCRUB;

        ret = br_prepare_loc (this, child, parent, entry, &loc);
        if (!ret)
                goto out;

        syncopctx_setfspid (&pid);

        ret = syncop_lookup (child->xl, &loc, NULL, &iatt, NULL, &parent_buf);
        if (ret) {
                br_log_object_path (this, "lookup", loc.path, -ret);
                goto out;
        }

        linked_inode = inode_link (loc.inode, parent->inode, loc.name, &iatt);
        if (linked_inode)
                inode_lookup (linked_inode);

        if (iatt.ia_type != IA_IFREG) {
                gf_log (this->name, GF_LOG_DEBUG, "%s is not a regular "
                        "file", entry->d_name);
                ret = 0;
                goto unref_inode;
        }

        /**
         * open() an fd for subsequent opertaions
         */
        fd = fd_create (linked_inode, 0);
        if (!fd) {
                gf_log (this->name, GF_LOG_ERROR, "failed to create fd for "
                        "inode %s", uuid_utoa (linked_inode->gfid));
                goto unref_inode;
        }

        ret = syncop_open (child->xl, &loc, O_RDWR, fd);
        if (ret) {
                br_log_object (this, "open", linked_inode->gfid, -ret);
                ret = -1;
                goto unrefd;
        }

        fd_bind (fd);

        /**
         * perform pre compute checks before initiating checksum
         * computation
         *  - presence of bad object
         *  - signature staleness
         */
        ret = bitd_scrub_pre_compute_check (this, child, fd);
        if (ret)
                goto unrefd; /* skip this object */

        /* if all's good, proceed to calculate the hash */
        md = GF_CALLOC (SHA256_DIGEST_LENGTH, sizeof (*md),
                        gf_common_mt_char);
        if (!md)
                goto unrefd;

        ret = br_calculate_obj_checksum (md, child, fd, &iatt);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "error calculating hash "
                        "for object [GFID: %s]", uuid_utoa (fd->inode->gfid));
                ret = -1;
                goto free_md;
        }

        /**
         * perform post compute checks as an object's signature may have
         * become stale while scrubber calculated checksum.
         */
        ret = bitd_scrub_post_compute_check (this, child, &sign, fd);
        if (ret)
                goto free_md;

        ret = bitd_compare_ckum (this, &sign, md,
                                 linked_inode, entry, fd, child);

        /** fd_unref() takes care of closing fd.. like syncop_close() */

 free_md:
        GF_FREE (md);
 unrefd:
        fd_unref (fd);
 unref_inode:
        inode_unref (linked_inode);
 out:
        loc_wipe (&loc);
        return ret;
}

#define BR_SCRUB_THROTTLE_COUNT 10
#define BR_SCRUB_THROTTLE_ZZZ   100
void *
br_scrubber (void *arg)
{
        loc_t       loc   = {0,};
        xlator_t   *this  = NULL;
        br_child_t *child = NULL;

        child = arg;
        this = child->this;

        THIS = this;

        loc.inode = child->table->root;
        while (1) {
                (void) syncop_ftw_throttle
                           (child->xl, &loc,
                            GF_CLIENT_PID_SCRUB, child, bitd_start_scrub,
                            BR_SCRUB_THROTTLE_COUNT, BR_SCRUB_THROTTLE_ZZZ);

                sleep (BR_SCRUB_THROTTLE_ZZZ * BR_SCRUB_THROTTLE_COUNT);
        }

        return NULL;
}
