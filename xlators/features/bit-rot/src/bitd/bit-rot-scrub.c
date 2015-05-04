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

#include <math.h>
#include <ctype.h>
#include <sys/uio.h>

#include "glusterfs.h"
#include "logging.h"
#include "common-utils.h"

#include "bit-rot-scrub.h"
#include <pthread.h>

struct br_scrubbers {
        pthread_t scrubthread;

        struct list_head list;
};

struct br_fsscan_entry {
        void *data;

        loc_t parent;

        gf_dirent_t *entry;

        struct br_scanfs *fsscan;  /* backpointer to subvolume scanner */

        struct list_head list;
};

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
br_scrubber_scrub_begin (xlator_t *this, struct br_fsscan_entry *fsentry)
{
        int32_t              ret           = -1;
        fd_t                *fd            = NULL;
        loc_t                loc           = {0, };
        struct iatt          iatt          = {0, };
        struct iatt          parent_buf    = {0, };
        pid_t                pid           = 0;
        br_child_t          *child         = NULL;
        unsigned char       *md            = NULL;
        inode_t             *linked_inode  = NULL;
        br_isignature_out_t *sign          = NULL;
        unsigned long        signedversion = 0;
        gf_dirent_t         *entry         = NULL;
        loc_t               *parent        = NULL;

        GF_VALIDATE_OR_GOTO ("bit-rot", fsentry, out);

        entry = fsentry->entry;
        parent = &fsentry->parent;
        child = fsentry->data;

        GF_VALIDATE_OR_GOTO ("bit-rot", entry, out);
        GF_VALIDATE_OR_GOTO ("bit-rot", parent, out);
        GF_VALIDATE_OR_GOTO ("bit-rot", child, out);

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

static void
wait_for_scrubbing (xlator_t *this, struct br_scanfs *fsscan)
{
        br_private_t *priv = NULL;
        struct br_scrubber *fsscrub = NULL;

        priv = this->private;
        fsscrub = &priv->fsscrub;

        pthread_mutex_lock (&fsscan->waitlock);
        {
                pthread_mutex_lock (&fsscrub->mutex);
                {
                        list_replace_init (&fsscan->queued, &fsscan->ready);

                        /* wake up scrubbers */
                        pthread_cond_broadcast (&fsscrub->cond);
                }
                pthread_mutex_unlock (&fsscrub->mutex);

                while (fsscan->entries != 0)
                        pthread_cond_wait
                                    (&fsscan->waitcond, &fsscan->waitlock);
        }
        pthread_mutex_unlock (&fsscan->waitlock);
}

static inline void
_br_fsscan_inc_entry_count (struct br_scanfs *fsscan)
{
        fsscan->entries++;
}

static inline void
_br_fsscan_dec_entry_count (struct br_scanfs *fsscan)
{
        if (--fsscan->entries == 0) {
                pthread_mutex_lock (&fsscan->waitlock);
                {
                        pthread_cond_signal (&fsscan->waitcond);
                }
                pthread_mutex_unlock (&fsscan->waitlock);
        }
}

static void
_br_fsscan_collect_entry (struct br_scanfs *fsscan,
                           struct br_fsscan_entry *fsentry)
{
        list_add_tail (&fsentry->list, &fsscan->queued);
        _br_fsscan_inc_entry_count (fsscan);
}

#define NR_ENTRIES (1<<7) /* ..bulk scrubbing */

int
br_fsscanner_handle_entry (xlator_t *subvol,
                           gf_dirent_t *entry, loc_t *parent, void *data)
{
        int32_t                 ret     = -1;
        int                     scrub   = 0;
        br_child_t             *child   = NULL;
        xlator_t               *this    = NULL;
        struct br_scanfs       *fsscan  = NULL;
        struct br_fsscan_entry *fsentry = NULL;

        GF_VALIDATE_OR_GOTO ("bit-rot", subvol, error_return);
        GF_VALIDATE_OR_GOTO ("bit-rot", data, error_return);

        child = data;
        this = child->this;
        fsscan = &child->fsscan;

        fsentry = GF_CALLOC (1, sizeof (*fsentry), gf_br_mt_br_fsscan_entry_t);
        if (!fsentry)
                goto error_return;

        {
                fsentry->data = data;
                fsentry->fsscan = &child->fsscan;

                /* copy parent loc */
                ret = loc_copy (&fsentry->parent, parent);
                if (ret)
                        goto dealloc;

                /* copy child entry */
                fsentry->entry = entry_copy (entry);
                if (!fsentry->entry)
                        goto locwipe;

                INIT_LIST_HEAD (&fsentry->list);
        }

        LOCK (&fsscan->entrylock);
        {
                _br_fsscan_collect_entry (fsscan, fsentry);

                /**
                 * need not be a equality check as entries may be pushed
                 * back onto the scanned queue when thread(s) are cleaned.
                 */
                if (fsscan->entries >= NR_ENTRIES)
                        scrub = 1;
        }
        UNLOCK (&fsscan->entrylock);

        if (scrub)
                wait_for_scrubbing (this, fsscan);

        return 0;

 locwipe:
        loc_wipe (&fsentry->parent);
 dealloc:
        GF_FREE (fsentry);
 error_return:
        return -1;
}

void *
br_fsscanner (void *arg)
{
        loc_t             loc    = {0,};
        xlator_t         *this   = NULL;
        br_child_t       *child  = NULL;
        struct br_scanfs *fsscan = NULL;

        child = arg;
        this = child->this;
        fsscan = &child->fsscan;

        THIS = this;

        loc.inode = child->table->root;
        while (1) {
                (void) syncop_ftw (child->xl, &loc,
                                   GF_CLIENT_PID_SCRUB,
                                   child, br_fsscanner_handle_entry);
                if (!list_empty (&fsscan->queued))
                        wait_for_scrubbing (this, fsscan);
        }

        return NULL;
}

#define BR_SCRUB_THREAD_SCALE_LAZY       0
#define BR_SCRUB_THREAD_SCALE_NORMAL     0.4
#define BR_SCRUB_THREAD_SCALE_AGGRESSIVE 1.0

#ifndef M_E
#define M_E 2.718
#endif

/**
 * This is just a simple exponential scale to a fixed value selected
 * per throttle config. We probably need to be more smart and select
 * the scale based on the number of processor cores too.
 */
static unsigned int
br_scrubber_calc_scale (xlator_t *this,
                        br_private_t *priv, scrub_throttle_t throttle)
{
        unsigned int scale = 0;

        switch (throttle) {
        case BR_SCRUB_THROTTLE_VOID:
        case BR_SCRUB_THROTTLE_STALLED:
                scale = 0;
                break;
        case BR_SCRUB_THROTTLE_LAZY:
                scale = priv->child_count *
                              pow (M_E, BR_SCRUB_THREAD_SCALE_LAZY);
                break;
        case BR_SCRUB_THROTTLE_NORMAL:
                scale = priv->child_count *
                              pow (M_E, BR_SCRUB_THREAD_SCALE_NORMAL);
                break;
        case BR_SCRUB_THROTTLE_AGGRESSIVE:
                scale = priv->child_count *
                              pow (M_E, BR_SCRUB_THREAD_SCALE_AGGRESSIVE);
                break;
        default:
                gf_log (this->name, GF_LOG_ERROR,
                        "Unknown throttle %d", throttle);
        }

        return scale;

}

static void
br_scrubber_cleanup_handler (void *arg)
{
        struct br_scrubber *fsscrub = arg;
        pthread_mutex_unlock (&fsscrub->mutex);
}

static inline br_child_t *
_br_scrubber_get_next_child (struct br_scrubber *fsscrub)
{
        br_child_t *child = NULL;

        child = list_first_entry (&fsscrub->scrublist, br_child_t, list);
        list_rotate_left (&fsscrub->scrublist);

        return child;
}

static inline void
_br_scrubber_get_entry (br_child_t *child, struct br_fsscan_entry **fsentry)
{
        struct br_scanfs *fsscan = &child->fsscan;

        if (list_empty (&fsscan->ready))
                return;
        *fsentry = list_first_entry
                            (&fsscan->ready, struct br_fsscan_entry, list);
        list_del_init (&(*fsentry)->list);
}

static inline void
_br_scrubber_find_scrubbable_entry (struct br_scrubber *fsscrub,
                                     struct br_fsscan_entry **fsentry)
{
        br_child_t *child = NULL;
        br_child_t *firstchild = NULL;

        while (1) {
                if (list_empty (&fsscrub->scrublist))
                        pthread_cond_wait (&fsscrub->cond, &fsscrub->mutex);

                firstchild = NULL;
                for (child = _br_scrubber_get_next_child (fsscrub);
                     child != firstchild;
                     child = _br_scrubber_get_next_child (fsscrub)) {

                        if (!firstchild)
                                firstchild = child;

                        _br_scrubber_get_entry (child, fsentry);
                        if (*fsentry)
                                break;
                }

                if (*fsentry)
                        break;

                /* nothing to work on.. wait till available */
                pthread_cond_wait (&fsscrub->cond, &fsscrub->mutex);
        }
}

static void
br_scrubber_pick_entry (struct br_scrubber *fsscrub,
                        struct br_fsscan_entry **fsentry)
{
        pthread_cleanup_push (br_scrubber_cleanup_handler, fsscrub);

        pthread_mutex_lock (&fsscrub->mutex);
        {
                *fsentry = NULL;
                _br_scrubber_find_scrubbable_entry (fsscrub, fsentry);
        }
        pthread_mutex_unlock (&fsscrub->mutex);

        pthread_cleanup_pop (0);
}

struct br_scrub_entry {
        gf_boolean_t scrubbed;
        struct br_fsscan_entry *fsentry;
};

/**
 * We need to be a bit careful here. These thread(s) are prone to cancellations
 * when threads are scaled down (depending on the thottling value configured)
 * and pausing scrub. A thread can get cancelled while it's waiting for entries
 * in the ->pending queue or when an object is undergoing scrubbing.
 */
static void
br_scrubber_entry_handle (void *arg)
{
        struct br_scanfs       *fsscan  = NULL;
        struct br_scrub_entry  *sentry  = NULL;
        struct br_fsscan_entry *fsentry = NULL;

        sentry = arg;

        fsentry = sentry->fsentry;
        fsscan  = fsentry->fsscan;

        LOCK (&fsscan->entrylock);
        {
                if (sentry->scrubbed) {
                        _br_fsscan_dec_entry_count (fsscan);

                        /* cleanup ->entry */
                        fsentry->data   = NULL;
                        fsentry->fsscan = NULL;
                        loc_wipe (&fsentry->parent);
                        gf_dirent_entry_free (fsentry->entry);

                        GF_FREE (sentry->fsentry);
                } else {
                        /* (re)queue the entry again for scrub */
                        _br_fsscan_collect_entry (fsscan, sentry->fsentry);
                }
        }
        UNLOCK (&fsscan->entrylock);
}

static void
br_scrubber_scrub_entry (xlator_t *this, struct br_fsscan_entry *fsentry)
{
        struct br_scrub_entry sentry = {0, };

        sentry.scrubbed = 0;
        sentry.fsentry = fsentry;

        pthread_cleanup_push (br_scrubber_entry_handle, &sentry);
        {
                (void) br_scrubber_scrub_begin (this, fsentry);
                sentry.scrubbed = 1;
        }
        pthread_cleanup_pop (1);
}

void *br_scrubber_proc (void *arg)
{
        xlator_t *this = NULL;
        struct br_scrubber *fsscrub = NULL;
        struct br_fsscan_entry *fsentry = NULL;

        fsscrub = arg;
        THIS = this = fsscrub->this;

        while (1) {
                br_scrubber_pick_entry (fsscrub, &fsentry);
                br_scrubber_scrub_entry (this, fsentry);
                sleep (1);
        }

        return NULL;
}

static int32_t
br_scrubber_scale_up (xlator_t *this,
                      struct br_scrubber *fsscrub,
                      unsigned int v1, unsigned int v2)
{
        int i = 0;
        int32_t ret = -1;
        int diff = 0;
        struct br_scrubbers *scrub = NULL;

        diff = (int)(v2 - v1);

        gf_log (this->name, GF_LOG_INFO,
                "Scaling up scrubbers [%d => %d]", v1, v2);

        for (i = 0; i < diff; i++) {
                scrub = GF_CALLOC (diff, sizeof (*scrub),
                                   gf_br_mt_br_scrubber_t);
                if (!scrub)
                        break;

                INIT_LIST_HEAD (&scrub->list);
                ret = gf_thread_create (&scrub->scrubthread,
                                        NULL, br_scrubber_proc, fsscrub);
                if (ret)
                        break;

                fsscrub->nr_scrubbers++;
                list_add_tail (&scrub->list, &fsscrub->scrubbers);
        }

        if ((i != diff) && !scrub)
                goto error_return;

        if (i != diff) /* degraded scaling.. */
                gf_log (this->name, GF_LOG_WARNING,
                        "Could not fully scale up to %d scrubber(s). Spawned "
                        "%d/%d [total scrubber(s): %d]", v2, i, diff, (v1 + i));

        return 0;

 error_return:
        return -1;
}

static int32_t
br_scrubber_scale_down (xlator_t *this,
                        struct br_scrubber *fsscrub,
                        unsigned int v1, unsigned int v2)
{
        int i = 0;
        int diff = 0;
        int32_t ret = -1;
        struct br_scrubbers *scrub = NULL;

        diff = (int)(v1 - v2);

        gf_log (this->name, GF_LOG_INFO,
                "Scaling down scrubbers [%d => %d]", v1, v2);

        for (i = 0 ; i < diff; i++) {
                scrub = list_first_entry
                            (&fsscrub->scrubbers, struct br_scrubbers, list);

                list_del_init (&scrub->list);
                ret = gf_thread_cleanup_xint (scrub->scrubthread);
                if (ret)
                        break;
                GF_FREE (scrub);

                fsscrub->nr_scrubbers--;
        }

        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Could not fully scale down to %d scrubber(s). "
                        "Terminated %d/%d [total scrubber(s): %d]",
                        v1, i, diff, (v2 - i));
                ret = 0;
        }

        return ret;
}

static int32_t
br_scrubber_configure (xlator_t *this, br_private_t *priv,
                       struct br_scrubber *fsscrub, scrub_throttle_t nthrottle)
{
        int32_t ret = 0;
        unsigned int v1 = 0;
        unsigned int v2 = 0;

        v1 = fsscrub->nr_scrubbers;
        v2 = br_scrubber_calc_scale (this, priv, nthrottle);

        if (v1 == v2)
                return 0;

        if (v1 > v2)
                ret = br_scrubber_scale_down (this, fsscrub, v1, v2);
        else
                ret = br_scrubber_scale_up (this, fsscrub, v1, v2);

        return ret;
}

static inline int32_t
br_scrubber_fetch_option (xlator_t *this,
                          char *opt, dict_t *options, char **value)
{
        if (options)
                GF_OPTION_RECONF (opt, *value, options, str, error_return);
        else
                GF_OPTION_INIT (opt, *value, str, error_return);

        return 0;

 error_return:
        return -1;
}

/* internal "throttle" override */
#define BR_SCRUB_STALLED  "STALLED"

/* TODO: token buket spec */
static int32_t
br_scrubber_handle_throttle (xlator_t *this, br_private_t *priv,
                             dict_t *options, gf_boolean_t scrubstall)
{
        int32_t ret = 0;
        char *tmp = NULL;
        struct br_scrubber *fsscrub = NULL;
        scrub_throttle_t nthrottle = BR_SCRUB_THROTTLE_VOID;

        fsscrub = &priv->fsscrub;

        ret = br_scrubber_fetch_option (this, "scrub-throttle", options, &tmp);
        if (ret)
                goto error_return;

        if (scrubstall)
                tmp = BR_SCRUB_STALLED;

        if (strcasecmp (tmp, "lazy") == 0)
                nthrottle = BR_SCRUB_THROTTLE_LAZY;
        else if (strcasecmp (tmp, "normal") == 0)
                nthrottle = BR_SCRUB_THROTTLE_NORMAL;
        else if (strcasecmp (tmp, "aggressive") == 0)
                nthrottle = BR_SCRUB_THROTTLE_AGGRESSIVE;
        else if (strcasecmp (tmp, BR_SCRUB_STALLED) == 0)
                nthrottle = BR_SCRUB_THROTTLE_STALLED;
        else
                goto error_return;

        /* on failure old throttling value is preserved */
        ret = br_scrubber_configure (this, priv, fsscrub, nthrottle);
        if (ret)
                goto error_return;

        fsscrub->throttle = nthrottle;
        return 0;

 error_return:
        return -1;
}

static int32_t
br_scrubber_handle_stall (xlator_t *this, br_private_t *priv,
                          dict_t *options, gf_boolean_t *scrubstall)
{
        int32_t ret = 0;
        char *tmp = NULL;

        ret = br_scrubber_fetch_option (this, "scrub-state", options, &tmp);
        if (ret)
                goto error_return;

        if (strcasecmp (tmp, "pause") == 0) /* anything else is active */
                *scrubstall = _gf_true;

        return 0;

 error_return:
        return -1;
}

/* TODO: frequency */
int32_t
br_scrubber_handle_options (xlator_t *this, br_private_t *priv, dict_t *options)
{
        int32_t ret = 0;
        gf_boolean_t scrubstall = _gf_false; /* not as dangerous as it sounds */

        ret = br_scrubber_handle_stall (this, priv, options, &scrubstall);
        if (ret)
                goto error_return;

        ret = br_scrubber_handle_throttle (this, priv, options, scrubstall);
        if (ret)
                goto error_return;

        return 0;

 error_return:
        return -1;
}

int32_t
br_scrubber_init (xlator_t *this, br_private_t *priv)
{
        struct br_scrubber *fsscrub = NULL;

        priv->tbf = br_tbf_init (NULL, 0);
        if (!priv->tbf)
                return -1;

        fsscrub = &priv->fsscrub;

        fsscrub->this = this;
        fsscrub->throttle = BR_SCRUB_THROTTLE_VOID;

        pthread_mutex_init (&fsscrub->mutex, NULL);
        pthread_cond_init (&fsscrub->cond, NULL);

        fsscrub->nr_scrubbers = 0;
        INIT_LIST_HEAD (&fsscrub->scrubbers);
        INIT_LIST_HEAD (&fsscrub->scrublist);

        return 0;
}
