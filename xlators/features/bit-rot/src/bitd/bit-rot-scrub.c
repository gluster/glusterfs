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

/**
 * fetch signature extended attribute from an object's fd.
 * NOTE: On success @xattr is not unref'd as @sign points
 * to the dictionary value.
 */
static inline int32_t
bitd_fetch_signature (xlator_t *this, br_child_t *child,
                      fd_t *fd, dict_t **xattr, br_isignature_out_t **sign)
{
       int32_t ret = -1;

        ret = syncop_fgetxattr (child->xl, fd, xattr,
                               GLUSTERFS_GET_OBJECT_SIGNATURE, NULL, NULL);
        if (ret < 0) {
                br_log_object (this, "fgetxattr", fd->inode->gfid, -ret);
                goto out;
        }

        ret = dict_get_ptr
                (*xattr, GLUSTERFS_GET_OBJECT_SIGNATURE, (void **) sign);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to extract signature info [GFID: %s]",
                        uuid_utoa (fd->inode->gfid));
                goto unref_dict;
        }

        return 0;

 unref_dict:
        dict_unref (*xattr);
 out:
        return -1;

}

/**
 * POST COMPUTE CHECK
 *
 * Checks to be performed before verifying calculated signature
 * Object is skipped if:
 *  - has stale signature
 *  - mismatches versions caches in pre-compute check
 */

int32_t
bitd_scrub_post_compute_check (xlator_t *this,
                               br_child_t *child,
                               fd_t *fd, unsigned long version,
                               br_isignature_out_t **signature)
{
        int32_t              ret     = 0;
        size_t               signlen = 0;
        dict_t              *xattr   = NULL;
        br_isignature_out_t *signptr = NULL;

        ret = bitd_fetch_signature (this, child, fd, &xattr, &signptr);
        if (ret < 0)
                goto out;

        /**
         * Either the object got dirtied during the time the signature was
         * calculated OR the version we saved during pre-compute check does
         * not match now, implying that the object got dirtied and signed in
         * between scrubs pre & post compute checks (checksum window).
         *
         * The log entry looks pretty ugly, but helps in debugging..
         */
        if (signptr->stale || (signptr->version != version)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "<STAGE: POST> Object [GFID: %s] either has a stale "
                        "signature OR underwent signing during checksumming "
                        "{Stale: %d | Version: %lu,%lu}",
                        uuid_utoa (fd->inode->gfid), (signptr->stale) ? 1 : 0,
                        version, signptr->version);
                ret = -1;
                goto unref_dict;
        }

        signlen = signptr->signaturelen;
        *signature = GF_CALLOC (1, sizeof (br_isignature_out_t) + signlen,
                                gf_common_mt_char);

        (void) memcpy (*signature, signptr,
                       sizeof (br_isignature_out_t) + signlen);

 unref_dict:
        dict_unref (xattr);
 out:
        return ret;

}

static inline int32_t
bitd_signature_staleness (xlator_t *this,
                          br_child_t *child, fd_t *fd,
                          int *stale, unsigned long *version)
{
        int32_t ret = -1;
        dict_t *xattr = NULL;
        br_isignature_out_t *signptr = NULL;

        ret = bitd_fetch_signature (this, child, fd, &xattr, &signptr);
        if (ret < 0)
                goto out;

        /**
         * save verison for validation in post compute stage
         * c.f. bitd_scrub_post_compute_check()
         */
        *stale = signptr->stale ? 1 : 0;
        *version = signptr->version;

        dict_unref (xattr);

 out:
        return ret;
}

/**
 * PRE COMPUTE CHECK
 *
 * Checks to be performed before initiating object signature calculation.
 * An object is skipped if:
 *  - it's already marked corrupted
 *  - has stale signature
 */
int32_t
bitd_scrub_pre_compute_check (xlator_t *this, br_child_t *child,
                              fd_t *fd, unsigned long *version)
{
        int     stale = 0;
        int32_t ret   = -1;

        if (bitd_is_bad_file (this, child, NULL, fd)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Object [GFID: %s] is marked corrupted, skipping..",
                        uuid_utoa (fd->inode->gfid));
                goto out;
        }

        ret = bitd_signature_staleness (this, child, fd, &stale, version);
        if (!ret && stale) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "<STAGE: PRE> Object [GFID: %s] has stale signature",
                        uuid_utoa (fd->inode->gfid));
                ret = -1;
        }

 out:
        return ret;
}

/* static inline int */
int
bitd_compare_ckum (xlator_t *this,
                   br_isignature_out_t *sign,
                   unsigned char *md, inode_t *linked_inode,
                   gf_dirent_t *entry, fd_t *fd, br_child_t *child, loc_t *loc)
{
        int   ret = -1;
        dict_t *xattr = NULL;

        GF_VALIDATE_OR_GOTO ("bit-rot", this, out);
        GF_VALIDATE_OR_GOTO (this->name, sign, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, child, out);
        GF_VALIDATE_OR_GOTO (this->name, linked_inode, out);
        GF_VALIDATE_OR_GOTO (this->name, md, out);
        GF_VALIDATE_OR_GOTO (this->name, entry, out);

        if (strncmp
            (sign->signature, (char *) md, strlen (sign->signature)) == 0) {
                gf_log (this->name, GF_LOG_DEBUG, "%s [GFID: %s | Brick: %s] "
                        "matches calculated checksum", loc->path,
                        uuid_utoa (linked_inode->gfid), child->brick_path);
                return 0;
        }

        gf_log (this->name, GF_LOG_ALERT,
                "Object checksum mismatch: %s [GFID: %s | Brick: %s]",
                loc->path, uuid_utoa (linked_inode->gfid), child->brick_path);

        /* Perform bad-file marking */
        xattr = dict_new ();
        if (!xattr) {
                ret = -1;
                goto out;
        }

        ret = dict_set_int32 (xattr, BITROT_OBJECT_BAD_KEY, _gf_true);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error setting bad-file marker for %s [GFID: %s | "
                        "Brick: %s]", loc->path, uuid_utoa (linked_inode->gfid),
                        child->brick_path);
                goto dictfree;
        }

        gf_log (this->name, GF_LOG_INFO, "Marking %s [GFID: %s | Brick: %s] "
                "as corrupted..", loc->path, uuid_utoa (linked_inode->gfid),
                child->brick_path);
        ret = syncop_fsetxattr (child->xl, fd, xattr, 0, NULL, NULL);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR,
                        "Error marking object %s [GFID: %s] as corrupted",
                        loc->path, uuid_utoa (linked_inode->gfid));

 dictfree:
        dict_unref (xattr);
 out:
        return ret;
}

/**
 * "The Scrubber"
 *
 * Perform signature validation for a given object with the assumption
 * that the signature is SHA256 (because signer as of now _always_
 * signs with SHA256).
 */
int
bitd_start_scrub (xlator_t *subvol,
                  gf_dirent_t *entry, loc_t *parent, void *data)
{
        int32_t              ret           = -1;
        fd_t                *fd            = NULL;
        loc_t                loc           = {0, };
        struct iatt          iatt          = {0, };
        struct iatt          parent_buf    = {0, };
        pid_t                pid           = 0;
        br_child_t          *child         = NULL;
        xlator_t            *this          = NULL;
        unsigned char       *md            = NULL;
        inode_t             *linked_inode  = NULL;
        br_isignature_out_t *sign          = NULL;
        unsigned long        signedversion = 0;

        GF_VALIDATE_OR_GOTO ("bit-rot", subvol, out);
        GF_VALIDATE_OR_GOTO ("bit-rot", data, out);

        child = data;
        this = child->this;

        pid = GF_CLIENT_PID_SCRUB;

        ret = br_prepare_loc (this, child, parent, entry, &loc);
        if (!ret)
                goto out;

        syncopctx_setfspid (&pid);

        ret = syncop_lookup (child->xl, &loc, &iatt, &parent_buf, NULL, NULL);
        if (ret) {
                br_log_object_path (this, "lookup", loc.path, -ret);
                goto out;
        }

        linked_inode = inode_link (loc.inode, parent->inode, loc.name, &iatt);
        if (linked_inode)
                inode_lookup (linked_inode);

        gf_log (this->name, GF_LOG_DEBUG, "Scrubbing object %s [GFID: %s]",
                entry->d_name, uuid_utoa (linked_inode->gfid));

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

        ret = syncop_open (child->xl, &loc, O_RDWR, fd, NULL, NULL);
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
        ret = bitd_scrub_pre_compute_check (this, child, fd, &signedversion);
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
        ret = bitd_scrub_post_compute_check (this, child,
                                             fd, signedversion, &sign);
        if (ret)
                goto free_md;

        ret = bitd_compare_ckum (this, sign, md,
                                 linked_inode, entry, fd, child, &loc);

        GF_FREE (sign); /* alloced on post-compute */

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

#define BR_SCRUB_THROTTLE_COUNT 30
#define BR_SCRUB_THROTTLE_ZZZ   60
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

                sleep (BR_SCRUB_THROTTLE_ZZZ);
        }

        return NULL;
}
