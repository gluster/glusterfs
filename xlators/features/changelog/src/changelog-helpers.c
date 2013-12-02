/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
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

#include "xlator.h"
#include "defaults.h"
#include "logging.h"
#include "iobuf.h"

#include "changelog-helpers.h"
#include "changelog-mem-types.h"

#include "changelog-encoders.h"
#include <pthread.h>

void
changelog_thread_cleanup (xlator_t *this, pthread_t thr_id)
{
        int   ret    = 0;
        void *retval = NULL;

        /* send a cancel request to the thread */
        ret = pthread_cancel (thr_id);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "could not cancel thread (reason: %s)",
                        strerror (errno));
                goto out;
        }

        ret = pthread_join (thr_id, &retval);
        if (ret || (retval != PTHREAD_CANCELED)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cancel request not adhered as expected"
                        " (reason: %s)", strerror (errno));
        }

 out:
        return;
}

inline void *
changelog_get_usable_buffer (changelog_local_t *local)
{
        changelog_log_data_t *cld = NULL;

        if (!local)
                return NULL;

        cld = &local->cld;
        if (!cld->cld_iobuf)
                return NULL;

        return cld->cld_iobuf->ptr;
}

inline void
changelog_set_usable_record_and_length (changelog_local_t *local,
                                        size_t len, int xr)
{
        changelog_log_data_t *cld = NULL;

        cld = &local->cld;

        cld->cld_ptr_len = len;
        cld->cld_xtra_records = xr;
}

void
changelog_local_cleanup (xlator_t *xl, changelog_local_t *local)
{
        int                   i   = 0;
        changelog_opt_t      *co  = NULL;
        changelog_log_data_t *cld = NULL;

        if (!local)
                return;

        cld = &local->cld;

        /* cleanup dynamic allocation for extra records */
        if (cld->cld_xtra_records) {
                co = (changelog_opt_t *) cld->cld_ptr;
                for (; i < cld->cld_xtra_records; i++, co++)
                        if (co->co_free)
                                co->co_free (co);
        }

        CHANGELOG_IOBUF_UNREF (cld->cld_iobuf);

        if (local->inode)
                inode_unref (local->inode);

        mem_put (local);
}

inline int
changelog_write (int fd, char *buffer, size_t len)
{
        ssize_t size = 0;
        size_t writen = 0;

        while (writen < len) {
                size = write (fd,
                              buffer + writen, len - writen);
                if (size <= 0)
                        break;

                writen += size;
        }

        return (writen != len);
}

static int
changelog_rollover_changelog (xlator_t *this,
                              changelog_priv_t *priv, unsigned long ts)
{
        int   ret            = -1;
        int   notify         = 0;
        char *bname          = NULL;
        char ofile[PATH_MAX] = {0,};
        char nfile[PATH_MAX] = {0,};

        if (priv->changelog_fd != -1) {
                close (priv->changelog_fd);
                priv->changelog_fd = -1;
        }

        (void) snprintf (ofile, PATH_MAX,
                         "%s/"CHANGELOG_FILE_NAME, priv->changelog_dir);
        (void) snprintf (nfile, PATH_MAX,
                         "%s/"CHANGELOG_FILE_NAME".%lu",
                         priv->changelog_dir, ts);

        ret = rename (ofile, nfile);
        if (!ret)
                notify = 1;

        if (ret && (errno == ENOENT)) {
                ret = 0;
        }

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "error renaming %s -> %s (reason %s)",
                        ofile, nfile, strerror (errno));
        }

        if (notify) {
                bname = basename (nfile);
                gf_log (this->name, GF_LOG_DEBUG, "notifying: %s", bname);
                ret = changelog_write (priv->wfd, bname, strlen (bname) + 1);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to send file name to notify thread"
                                " (reason: %s)", strerror (errno));
                }
        }

        return ret;
}

int
changelog_open (xlator_t *this,
                changelog_priv_t *priv)
{
        int fd                        = 0;
        int ret                       = -1;
        int flags                     = 0;
        char buffer[1024]             = {0,};
        char changelog_path[PATH_MAX] = {0,};

        (void) snprintf (changelog_path, PATH_MAX,
                         "%s/"CHANGELOG_FILE_NAME,
                         priv->changelog_dir);

        flags |= (O_CREAT | O_RDWR);
        if (priv->fsync_interval == 0)
                flags |= O_SYNC;

        fd = open (changelog_path, flags,
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "unable to open/create changelog file %s"
                        " (reason: %s). change-logging will be"
                        " inactive", changelog_path, strerror (errno));
                goto out;
        }

        priv->changelog_fd = fd;

        (void) snprintf (buffer, 1024, CHANGELOG_HEADER,
                         CHANGELOG_VERSION_MAJOR,
                         CHANGELOG_VERSION_MINOR,
                         priv->ce->encoder);
        ret = changelog_write_change (priv, buffer, strlen (buffer));
        if (ret) {
                close (priv->changelog_fd);
                priv->changelog_fd = -1;
                goto out;
        }

        ret = 0;

 out:
        return ret;
}

int
changelog_start_next_change (xlator_t *this,
                             changelog_priv_t *priv,
                             unsigned long ts, gf_boolean_t finale)
{
        int ret = -1;

        ret = changelog_rollover_changelog (this, priv, ts);

        if (!ret && !finale)
                ret = changelog_open (this, priv);

        return ret;
}

/**
 * return the length of entry
 */
inline size_t
changelog_entry_length ()
{
        return sizeof (changelog_log_data_t);
}

int
changelog_fill_rollover_data (changelog_log_data_t *cld, gf_boolean_t is_last)
{
        struct timeval tv = {0,};

        cld->cld_type = CHANGELOG_TYPE_ROLLOVER;

        if (gettimeofday (&tv, NULL))
                return -1;

        cld->cld_roll_time = (unsigned long) tv.tv_sec;
        cld->cld_finale = is_last;
        return 0;
}

int
changelog_write_change (changelog_priv_t *priv, char *buffer, size_t len)
{
        return changelog_write (priv->changelog_fd, buffer, len);
}

inline int
changelog_handle_change (xlator_t *this,
                         changelog_priv_t *priv, changelog_log_data_t *cld)
{
        int ret = 0;

        if (CHANGELOG_TYPE_IS_ROLLOVER (cld->cld_type)) {
                changelog_encode_change(priv);
                ret = changelog_start_next_change (this, priv,
                                                   cld->cld_roll_time,
                                                   cld->cld_finale);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR,
                                "Problem rolling over changelog(s)");
                goto out;
        }

        /**
         * case when there is reconfigure done (disabling changelog) and there
         * are still fops that have updates in prgress.
         */
        if (priv->changelog_fd == -1)
                return 0;

        if (CHANGELOG_TYPE_IS_FSYNC (cld->cld_type)) {
                ret = fsync (priv->changelog_fd);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "fsync failed (reason: %s)",
                                strerror (errno));
                }
                goto out;
        }

        ret = priv->ce->encode (this, cld);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "error writing changelog to disk");
        }

 out:
        return ret;
}

changelog_local_t *
changelog_local_init (xlator_t *this, inode_t *inode,
                      uuid_t gfid, int xtra_records,
                      gf_boolean_t update_flag)
{
        changelog_local_t *local = NULL;
        struct iobuf      *iobuf = NULL;

        /**
         * We relax the presence of inode if @update_flag is true.
         * The caller (implmentation of the fop) needs to be careful to
         * not blindly use local->inode.
         */
        if (!update_flag && !inode) {
                gf_log_callingfn (this->name, GF_LOG_WARNING,
                                  "inode needed for version checking !!!");
                goto out;
        }

        if (xtra_records) {
                iobuf = iobuf_get2 (this->ctx->iobuf_pool,
                                    xtra_records * CHANGELOG_OPT_RECORD_LEN);
                if (!iobuf)
                        goto out;
        }

        local = mem_get0 (this->local_pool);
        if (!local) {
                CHANGELOG_IOBUF_UNREF (iobuf);
                goto out;
        }

        local->update_no_check = update_flag;

        uuid_copy (local->cld.cld_gfid, gfid);

        local->cld.cld_iobuf = iobuf;
        local->cld.cld_xtra_records = 0; /* set by the caller */

        if (inode)
                local->inode = inode_ref (inode);

 out:
        return local;
}

int
changelog_forget (xlator_t *this, inode_t *inode)
{
        uint64_t ctx_addr = 0;
        changelog_inode_ctx_t *ctx = NULL;

        inode_ctx_del (inode, this, &ctx_addr);
        if (!ctx_addr)
                return 0;

        ctx = (changelog_inode_ctx_t *) (long) ctx_addr;
        GF_FREE (ctx);

        return 0;
}

int
changelog_inject_single_event (xlator_t *this,
                               changelog_priv_t *priv,
                               changelog_log_data_t *cld)
{
        return priv->cd.dispatchfn (this, priv, priv->cd.cd_data, cld, NULL);
}

/**
 * TODO: these threads have many thing in common (wake up after
 * a certain time etc..). move them into separate routine.
 */
void *
changelog_rollover (void *data)
{
        int                     ret   = 0;
        xlator_t               *this  = NULL;
        struct timeval          tv    = {0,};
        changelog_log_data_t    cld   = {0,};
        changelog_time_slice_t *slice = NULL;
        changelog_priv_t       *priv  = data;

        this = priv->cr.this;
        slice = &priv->slice;

        while (1) {
                tv.tv_sec  = priv->rollover_time;
                tv.tv_usec = 0;

                ret = select (0, NULL, NULL, NULL, &tv);
                if (ret)
                        continue;

                ret = changelog_fill_rollover_data (&cld, _gf_false);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to fill rollover data");
                        continue;
                }

                LOCK (&priv->lock);
                {
                        ret = changelog_inject_single_event (this, priv, &cld);
                        if (!ret)
                                SLICE_VERSION_UPDATE (slice);
                }
                UNLOCK (&priv->lock);
        }

        return NULL;
}

void *
changelog_fsync_thread (void *data)
{
        int                   ret  = 0;
        xlator_t             *this = NULL;
        struct timeval        tv   = {0,};
        changelog_log_data_t  cld  = {0,};
        changelog_priv_t     *priv = data;

        this = priv->cf.this;
        cld.cld_type = CHANGELOG_TYPE_FSYNC;

        while (1) {
                tv.tv_sec  = priv->fsync_interval;
                tv.tv_usec = 0;

                ret = select (0, NULL, NULL, NULL, &tv);
                if (ret)
                        continue;

                ret = changelog_inject_single_event (this, priv, &cld);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to inject fsync event");
        }

        return NULL;
}

/* macros for inode/changelog version checks */

#define INODE_VERSION_UPDATE(priv, inode, iver, slice, type) do {       \
                LOCK (&inode->lock);                                    \
                {                                                       \
                        LOCK (&priv->lock);                             \
                        {                                               \
                                *iver = slice->changelog_version[type]; \
                        }                                               \
                        UNLOCK (&priv->lock);                           \
                }                                                       \
                UNLOCK (&inode->lock);                                  \
        } while (0)

#define INODE_VERSION_EQUALS_SLICE(priv, ver, slice, type, upd) do {    \
                LOCK (&priv->lock);                                     \
                {                                                       \
                        upd = (ver == slice->changelog_version[type])   \
                                ? _gf_false : _gf_true;                 \
                }                                                       \
                UNLOCK (&priv->lock);                                   \
        } while (0)

static int
__changelog_inode_ctx_set (xlator_t *this,
                           inode_t *inode, changelog_inode_ctx_t *ctx)
{
        uint64_t ctx_addr = (uint64_t) ctx;
        return __inode_ctx_set (inode, this, &ctx_addr);
}

/**
 * one shot routine to get the address and the value of a inode version
 * for a particular type.
 */
static changelog_inode_ctx_t *
__changelog_inode_ctx_get (xlator_t *this,
                           inode_t *inode, unsigned long **iver,
                           unsigned long *version, changelog_log_type type)
{
        int                    ret      = 0;
        uint64_t               ctx_addr = 0;
        changelog_inode_ctx_t *ctx      = NULL;

        ret = __inode_ctx_get (inode, this, &ctx_addr);
        if (ret < 0)
                ctx_addr = 0;
        if (ctx_addr != 0) {
                ctx = (changelog_inode_ctx_t *) (long)ctx_addr;
                goto out;
        }

        ctx = GF_CALLOC (1, sizeof (*ctx), gf_changelog_mt_inode_ctx_t);
        if (!ctx)
                goto out;

        ret = __changelog_inode_ctx_set (this, inode, ctx);
        if (ret) {
                GF_FREE (ctx);
                ctx = NULL;
        }

 out:
        if (ctx && iver && version) {
                *iver = CHANGELOG_INODE_VERSION_TYPE (ctx, type);
                *version = **iver;
        }

        return ctx;
}

static changelog_inode_ctx_t *
changelog_inode_ctx_get (xlator_t *this,
                         inode_t *inode, unsigned long **iver,
                         unsigned long *version, changelog_log_type type)
{
        changelog_inode_ctx_t *ctx = NULL;

        LOCK (&inode->lock);
        {
                ctx = __changelog_inode_ctx_get (this,
                                                 inode, iver, version, type);
        }
        UNLOCK (&inode->lock);

        return ctx;
}

/**
 * This is the main update routine. Locking has been made granular so as to
 * maximize parallelism of fops - I'll try to explain it below using execution
 * timelines.
 *
 * Basically, the contention is between multiple execution threads of this
 * routine and the roll-over thread. So, instead of having a big lock, we hold
 * granular locks: inode->lock and priv->lock. Now I'll explain what happens
 * when there is an update and a roll-over at just about the same time.
 * NOTE:
 *  - the dispatcher itself synchronizes updates via it's own lock
 *  - the slice version in incremented by the roll-over thread
 *
 * Case 1: When the rollover thread wins before the inode version can be
 * compared with the slice version.
 *
 *          [updater]                 |             [rollover]
 *                                    |
 *                                    |           <SLICE: 1, 1, 1>
 * <changelog_update>                 |
 *   <changelog_inode_ctx_get>        |
 *      <CTX: 1, 1, 1>                |
 *                                    |         <dispatch-rollover-event>
 *                                    |         LOCK (&priv->lock)
 *                                    |            <SLICE_VERSION_UPDATE>
 *                                    |              <SLICE: 2, 2, 2>
 *                                    |         UNLOCK (&priv->lock)
 *                                    |
 * LOCK (&priv->lock)                 |
 *   <INODE_VERSION_EQUALS_SLICE>     |
 *    I: 1 <-> S: 2                   |
 *    update: true                    |
 * UNLOCK (&priv->lock)               |
 *                                    |
 * <if update == true>                |
 *  <dispath-update-event>            |
 *  <INODE_VERSION_UPDATE>            |
 *   LOCK (&inode->lock)              |
 *    LOCK (&priv->lock)              |
 *     <CTX: 2, 1, 1>                 |
 *    UNLOCK (&priv->lock)            |
 *   UNLOCK (&inode->lock)            |
 *
 * Therefore, the change gets recorded in the next change (no lost change). If
 * the slice version was ahead of the inode version (say I:1, S: 2), then
 * anyway the comparison would result in a update (I: 1, S: 3).
 *
 * If the rollover time is too less, then there is another contention when the
 * updater tries to bring up inode version to the slice version (this is also
 * the case when the roll-over thread wakes up during INODE_VERSION_UPDATE.
 *
 *   <CTX: 1, 1, 1>                   |       <SLICE: 2, 2, 2>
 *                                    |
 *                                    |
 * <dispath-update-event>             |
 * <INODE_VERSION_UPDATE>             |
 *  LOCK (&inode->lock)               |
 *   LOCK (&priv->lock)               |
 *    <CTX: 2, 1, 1>                  |
 *   UNLOCK (&priv->lock)             |
 *  UNLOCK (&inode->lock)             |
 *                                    |         <dispatch-rollover-event>
 *                                    |         LOCK (&priv->lock)
 *                                    |            <SLICE_VERSION_UPDATE>
 *                                    |              <SLICE: 3, 3, 3>
 *                                    |         UNLOCK (&priv->lock)
 *
 *
 * Case 2: When the fop thread wins
 *
 *          [updater]                 |             [rollover]
 *                                    |
 *                                    |           <SLICE: 1, 1, 1>
 * <changelog_update>                 |
 *   <changelog_inode_ctx_get>        |
 *      <CTX: 0, 0, 0>                |
 *                                    |
 * LOCK (&priv->lock)                 |
 *   <INODE_VERSION_EQUALS_SLICE>     |
 *    I: 0 <-> S: 1                   |
 *    update: true                    |
 * UNLOCK (&priv->lock)               |
 *                                    |         <dispatch-rollover-event>
 *                                    |         LOCK (&priv->lock)
 *                                    |            <SLICE_VERSION_UPDATE>
 *                                    |              <SLICE: 2, 2, 2>
 *                                    |         UNLOCK (&priv->lock)
 * <if update == true>                |
 *  <dispath-update-event>            |
 *  <INODE_VERSION_UPDATE>            |
 *   LOCK (&inode->lock)              |
 *    LOCK (&priv->lock)              |
 *     <CTX: 2, 0, 0>                 |
 *    UNLOCK (&priv->lock)            |
 *   UNLOCK (&inode->lock)            |
 *
 * Here again, if the inode version was equal to the slice version (I: 1, S: 1)
 * then there is no need to record an update (as the equality of the two version
 * signifies an update was recorded in the current time slice).
 */
inline void
changelog_update (xlator_t *this, changelog_priv_t *priv,
                  changelog_local_t *local, changelog_log_type type)
{
        int                     ret        = 0;
        unsigned long          *iver       = NULL;
        unsigned long           version    = 0;
        inode_t                *inode      = NULL;
        changelog_time_slice_t *slice      = NULL;
        changelog_inode_ctx_t  *ctx        = NULL;
        changelog_log_data_t   *cld_0      = NULL;
        changelog_log_data_t   *cld_1      = NULL;
        changelog_local_t      *next_local = NULL;
        gf_boolean_t            need_upd   = _gf_true;

        slice = &priv->slice;

        /**
         * for fops that do not require inode version checking
         */
        if (local->update_no_check)
                goto update;

        inode = local->inode;

        ctx = changelog_inode_ctx_get (this,
                                       inode, &iver, &version, type);
        if (!ctx)
                goto update;

        INODE_VERSION_EQUALS_SLICE (priv, version, slice, type, need_upd);

 update:
        if (need_upd) {
                cld_0 = &local->cld;
                cld_0->cld_type = type;

                if ( (next_local = local->prev_entry) != NULL ) {
                        cld_1 = &next_local->cld;
                        cld_1->cld_type = type;
                }

                ret = priv->cd.dispatchfn (this, priv,
                                           priv->cd.cd_data, cld_0, cld_1);

                /**
                 * update after the dispatcher has successfully done
                 * it's job.
                 */
                if (!local->update_no_check && iver && !ret)
                        INODE_VERSION_UPDATE (priv, inode, iver, slice, type);
        }

        return;
}
