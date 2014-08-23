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
#include "syscall.h"

#include "changelog-helpers.h"
#include "changelog-mem-types.h"

#include "changelog-encoders.h"
#include <pthread.h>

static inline void
__mask_cancellation (xlator_t *this)
{
        int ret = 0;

        ret = pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to disable thread cancellation");
}

static inline void
__unmask_cancellation (xlator_t *this)
{
        int ret = 0;

        ret = pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to enable thread cancellation");
}

static void
changelog_cleanup_free_mutex (void *arg_mutex)
{
    pthread_mutex_t *p_mutex = (pthread_mutex_t*) arg_mutex;

    if (p_mutex)
            pthread_mutex_unlock(p_mutex);
}

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
        size_t written = 0;

        while (written < len) {
                size = write (fd,
                              buffer + written, len - written);
                if (size <= 0)
                        break;

                written += size;
        }

        return (written != len);
}

int
htime_update (xlator_t *this,
              changelog_priv_t *priv, unsigned long ts,
              char * buffer)
{
        char changelog_path[PATH_MAX+1]   = {0,};
        int len                           = -1;
        char x_value[25]                  = {0,};
        /* time stamp(10) + : (1) + rolltime (12 ) + buffer (2) */
        int ret                           = 0;

        if (priv->htime_fd ==-1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Htime fd not available for updation");
                ret = -1;
                goto out;
        }
        strcpy (changelog_path, buffer);
        len = strlen (changelog_path);
        changelog_path[len] = '\0'; /* redundant */

        if (changelog_write (priv->htime_fd, (void*) changelog_path, len+1 ) < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Htime file content write failed");
                ret =-1;
                goto out;
        }

        sprintf (x_value,"%lu:%d",ts, priv->rollover_count);

        if (sys_fsetxattr (priv->htime_fd, HTIME_KEY, x_value,
                       strlen (x_value),  XATTR_REPLACE)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Htime xattr updation failed, "
                        "reason (%s)",strerror (errno));
                goto out;
        }

        priv->rollover_count +=1;

out:
        return ret;
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
                ret = fsync (priv->changelog_fd);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "fsync failed (reason: %s)",
                                strerror (errno));
                }
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
                goto out;
        }

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "error renaming %s -> %s (reason %s)",
                        ofile, nfile, strerror (errno));
        }

        if (!ret) {
                ret = htime_update (this, priv, ts, nfile);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "could not update htime file");
                        goto out;
                }
        }

        if (notify) {
                bname = basename (nfile);
                gf_log (this->name, GF_LOG_DEBUG, "notifying: %s", bname);
                ret = changelog_write (priv->wfd, bname, strlen (bname) + 1);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Failed to send file name to notify thread"
                                " (reason: %s)", strerror (errno));
                } else {
                        /* If this is explicit rollover initiated by snapshot,
                         * wakeup reconfigure thread waiting for changelog to
                         * rollover
                         */
                        if (priv->explicit_rollover) {
                                priv->explicit_rollover = _gf_false;
                                ret = pthread_mutex_lock (
                                                   &priv->bn.bnotify_mutex);
                                CHANGELOG_PTHREAD_ERROR_HANDLE_0 (ret, out);
                                {
                                         priv->bn.bnotify = _gf_false;
                                         ret = pthread_cond_signal (
                                                        &priv->bn.bnotify_cond);
                                         CHANGELOG_PTHREAD_ERROR_HANDLE_0 (ret,
                                                                           out);
                                         gf_log (this->name, GF_LOG_INFO,
                                                 "Changelog published: %s and"
                                                 " signalled bnotify", bname);
                                }
                                ret = pthread_mutex_unlock (
                                                       &priv->bn.bnotify_mutex);
                                CHANGELOG_PTHREAD_ERROR_HANDLE_0 (ret, out);
                        }
                }
        }

 out:
        return ret;
}

/* Returns 0 on successful creation of htime file
 * returns -1 on failure or error
 */
int
htime_open (xlator_t *this,
            changelog_priv_t * priv, unsigned long ts)
{
        int fd                          = -1;
        int ret                         = 0;
        char ht_dir_path[PATH_MAX]      = {0,};
        char ht_file_path[PATH_MAX]     = {0,};
        int flags                       = 0;

        CHANGELOG_FILL_HTIME_DIR(priv->changelog_dir, ht_dir_path);

        /* get the htime file name in ht_file_path */
        (void) snprintf (ht_file_path,PATH_MAX,"%s/%s.%lu",ht_dir_path,
                        HTIME_FILE_NAME, ts);

        flags |= (O_CREAT | O_RDWR | O_SYNC);
        fd = open (ht_file_path, flags,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "unable to open/create htime file: %s"
                        "(reason: %s)", ht_file_path, strerror (errno));
                ret = -1;
                goto out;

        }

        if (sys_fsetxattr (fd, HTIME_KEY, HTIME_INITIAL_VALUE,
                       sizeof (HTIME_INITIAL_VALUE)-1,  0)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Htime xattr initialization failed");
                ret = -1;
                goto out;
        }

        /* save this htime_fd in priv->htime_fd */
        priv->htime_fd = fd;
        /* initialize rollover-number in priv to 1 */
        priv->rollover_count = 1;

out:
        return ret;
}

/* Description:
 *      Opens the snap changelog to log call path fops in it.
 *      This changelos name is "CHANGELOG.SNAP", stored in
 *      path ".glusterfs/changelogs/csnap".
 * Returns:
 *       0  : On success.
 *      -1  : On failure.
 */
int
changelog_snap_open (xlator_t *this,
                         changelog_priv_t *priv)
{
        int fd                        = -1;
        int ret                       = 0;
        int flags                     = 0;
        char buffer[1024]             = {0,};
        char c_snap_path[PATH_MAX]    = {0,};
        char csnap_dir_path[PATH_MAX] = {0,};

        CHANGELOG_FILL_CSNAP_DIR(priv->changelog_dir, csnap_dir_path);

        (void) snprintf (c_snap_path, PATH_MAX,
                        "%s/"CSNAP_FILE_NAME,
                        csnap_dir_path);

        flags |= (O_CREAT | O_RDWR | O_TRUNC);

        fd = open (c_snap_path, flags,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                                "unable to open %s file "
                                "reason:(%s)", c_snap_path, strerror (errno));
                ret = -1;
                goto out;
        }
        priv->c_snap_fd = fd;

        (void) snprintf (buffer, 1024, CHANGELOG_HEADER,
                        CHANGELOG_VERSION_MAJOR,
                        CHANGELOG_VERSION_MINOR,
                        priv->ce->encoder);
        ret = changelog_snap_write_change (priv, buffer, strlen (buffer));
        if (ret < 0) {
                close (priv->c_snap_fd);
                priv->c_snap_fd = -1;
                goto out;
        }

out:
        return ret;
}

/*
 * Description:
 *      Starts logging fop details in CSNAP journal.
 * Returns:
 *       0 : On success.
 *      -1 : On Failure.
 */
int
changelog_snap_logging_start (xlator_t *this,
                                  changelog_priv_t *priv)
{
        int ret = 0;

        ret = changelog_snap_open (this, priv);
        gf_log (this->name, GF_LOG_INFO,
                        "Now starting to log in call path");

        return ret;
}

/*
 * Description:
 *      Stops logging fop details in CSNAP journal.
 * Returns:
 *       0 : On success.
 *      -1 : On Failure.
 */
int
changelog_snap_logging_stop (xlator_t *this,
                changelog_priv_t *priv)
{
        int ret         = 0;

        close (priv->c_snap_fd);
        priv->c_snap_fd = -1;

        gf_log (this->name, GF_LOG_INFO,
                        "Stopped to log in call path");

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
changelog_snap_write_change (changelog_priv_t *priv, char *buffer, size_t len)
{
        return changelog_write (priv->c_snap_fd, buffer, len);
}

int
changelog_write_change (changelog_priv_t *priv, char *buffer, size_t len)
{
        return changelog_write (priv->changelog_fd, buffer, len);
}

/*
 * Descriptions:
 *      Writes fop details in ascii format to CSNAP.
 * Issues:
 *      Not Encoding agnostic.
 * Returns:
 *      0 : On Success.
 *     -1 : On Failure.
 */
int
changelog_snap_handle_ascii_change (xlator_t *this,
                                    changelog_log_data_t *cld)
{
        size_t            off      = 0;
        size_t            gfid_len = 0;
        char             *gfid_str = NULL;
        char             *buffer   = NULL;
        changelog_priv_t *priv     = NULL;
        int               ret      = 0;

        if (this == NULL) {
                ret = -1;
                goto out;
        }

        priv = this->private;

        if (priv == NULL) {
                ret = -1;
                goto out;
        }

        gfid_str = uuid_utoa (cld->cld_gfid);
        gfid_len = strlen (gfid_str);

        /*  extra bytes for decorations */
        buffer = alloca (gfid_len + cld->cld_ptr_len + 10);
        CHANGELOG_STORE_ASCII (priv, buffer,
                        off, gfid_str, gfid_len, cld);

        CHANGELOG_FILL_BUFFER (buffer, off, "\0", 1);

        ret = changelog_snap_write_change (priv, buffer, off);

        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                                "error writing csnap to disk");
        }
        gf_log (this->name, GF_LOG_INFO,
                        "Successfully wrote to csnap");
        ret = 0;
out:
        return ret;
}

inline int
changelog_handle_change (xlator_t *this,
                         changelog_priv_t *priv, changelog_log_data_t *cld)
{
        int ret = 0;

        if (CHANGELOG_TYPE_IS_ROLLOVER (cld->cld_type)) {
                changelog_encode_change (priv);
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

/* Wait till all the black fops are drained */
void
changelog_drain_black_fops (xlator_t *this, changelog_priv_t *priv)
{
        int ret = 0;

        /* clean up framework of pthread_mutex is required here as
         * 'reconfigure' terminates the changelog_rollover thread
         * on graph change.
         */
        pthread_cleanup_push (changelog_cleanup_free_mutex,
                                        &priv->dm.drain_black_mutex);
        ret = pthread_mutex_lock (&priv->dm.drain_black_mutex);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "pthread error:"
                        " Error:%d", ret);
        while (priv->dm.black_fop_cnt > 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Condtional wait on black fops: %ld",
                        priv->dm.black_fop_cnt);
                priv->dm.drain_wait_black = _gf_true;
                ret = pthread_cond_wait (&priv->dm.drain_black_cond,
                                         &priv->dm.drain_black_mutex);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR, "pthread"
                                " cond wait failed: Error:%d", ret);
        }
        priv->dm.drain_wait_black = _gf_false;
        ret = pthread_mutex_unlock (&priv->dm.drain_black_mutex);
        pthread_cleanup_pop (0);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "pthread error:"
                        " Error:%d", ret);
        gf_log (this->name, GF_LOG_DEBUG,
                "Woke up: Conditional wait on black fops");
}

/* Wait till all the white  fops are drained */
void
changelog_drain_white_fops (xlator_t *this, changelog_priv_t *priv)
{
        int ret = 0;

        /* clean up framework of pthread_mutex is required here as
         * 'reconfigure' terminates the changelog_rollover thread
         * on graph change.
         */
        pthread_cleanup_push (changelog_cleanup_free_mutex,
                                        &priv->dm.drain_white_mutex);
        ret = pthread_mutex_lock (&priv->dm.drain_white_mutex);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "pthread error:"
                        " Error:%d", ret);
        while (priv->dm.white_fop_cnt > 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Condtional wait on white fops : %ld",
                        priv->dm.white_fop_cnt);
                priv->dm.drain_wait_white = _gf_true;
                ret = pthread_cond_wait (&priv->dm.drain_white_cond,
                                         &priv->dm.drain_white_mutex);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR, "pthread"
                                " cond wait failed: Error:%d", ret);
        }
        priv->dm.drain_wait_white = _gf_false;
        ret = pthread_mutex_unlock (&priv->dm.drain_white_mutex);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "pthread error:"
                        " Error:%d", ret);
        pthread_cleanup_pop (0);
        gf_log (this->name, GF_LOG_DEBUG,
                "Woke up: Conditional wait on white fops");
}

/**
 * TODO: these threads have many thing in common (wake up after
 * a certain time etc..). move them into separate routine.
 */
void *
changelog_rollover (void *data)
{
        int                     ret             = 0;
        xlator_t               *this            = NULL;
        struct timeval          tv              = {0,};
        changelog_log_data_t    cld             = {0,};
        changelog_time_slice_t *slice           = NULL;
        changelog_priv_t       *priv            = data;
        int                     max_fd          = 0;
        char                    buf[1]          = {0};
        int                     len             = 0;

        fd_set                  rset;

        this = priv->cr.this;
        slice = &priv->slice;

        while (1) {
                (void) pthread_testcancel();

                tv.tv_sec  = priv->rollover_time;
                tv.tv_usec = 0;
                FD_ZERO(&rset);
                FD_SET(priv->cr.rfd, &rset);
                max_fd = priv->cr.rfd;
                max_fd = max_fd + 1;

               /* It seems there is a race between actual rollover and explicit
                * rollover. But it is handled. If actual rollover is being
                * done and the explicit rollover event comes, the event is
                * not missed. The next select will immediately wakeup to
                * handle explicit wakeup.
                */

                ret = select (max_fd, &rset, NULL, NULL, &tv);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "select failed: %s", strerror(errno));
                        continue;
                } else if (ret && FD_ISSET(priv->cr.rfd, &rset)) {
                        gf_log (this->name, GF_LOG_INFO,
                                "Explicit wakeup of select on barrier notify");
                        len = read(priv->cr.rfd, buf, 1);
                        if (len == 0) {
                                gf_log (this->name, GF_LOG_ERROR, "BUG: Got EOF"
                                        " from reconfigure notification pipe");
                                continue;
                        }
                        if (len < 0) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to read wakeup data");
                                continue;
                        }
                        /* Lock is not required as same thread is modifying.*/
                        priv->explicit_rollover = _gf_true;
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "select wokeup on timeout");
                }

               /* Reading curent_color without lock is fine here
                * as it is only modified here and is next to reading.
                */
                if (priv->current_color == FOP_COLOR_BLACK) {
                        LOCK(&priv->lock);
                                priv->current_color = FOP_COLOR_WHITE;
                        UNLOCK(&priv->lock);
                        gf_log (this->name, GF_LOG_DEBUG, "Black fops"
                                " to be drained:%ld",priv->dm.black_fop_cnt);
                        changelog_drain_black_fops (this, priv);
                } else {
                        LOCK(&priv->lock);
                                priv->current_color = FOP_COLOR_BLACK;
                        UNLOCK(&priv->lock);
                        gf_log (this->name, GF_LOG_DEBUG, "White fops"
                                " to be drained:%ld",priv->dm.white_fop_cnt);
                        changelog_drain_white_fops (this, priv);
                }

                /* Adding delay of 1 second only during explicit rollover:
                 *
                 * Changelog rollover can happen either due to actual
                 * or the explict rollover during snapshot. Actual
                 * rollover is controlled by tuneable called 'rollover-time'.
                 * The minimum granularity for rollover-time is 1 second.
                 * Explicit rollover is asynchronous in nature and happens
                 * during snapshot.
                 *
                 * Basically, rollover renames the current CHANGELOG file
                 * to CHANGELOG.TIMESTAMP. Let's assume, at time 't1',
                 * actual and explicit rollover raced against  each
                 * other and actual rollover won the race renaming the
                 * CHANGELOG file to CHANGELOG.t1 and opens a new
                 * CHANGELOG file. There is high chance that, an immediate
                 * explicit rollover at time 't1' can happen with in the same
                 * second to rename CHANGELOG file to CHANGELOG.t1 resulting in
                 * purging the earlier CHANGELOG.t1 file created by actual
                 * rollover. So adding a delay of 1 second guarantees unique
                 * CHANGELOG.TIMESTAMP during  explicit rollover.
                 */
                if (priv->explicit_rollover == _gf_true)
                        sleep (1);

                ret = changelog_fill_rollover_data (&cld, _gf_false);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to fill rollover data");
                        continue;
                }

                __mask_cancellation (this);

                LOCK (&priv->lock);
                {
                        ret = changelog_inject_single_event (this, priv, &cld);
                        if (!ret)
                                SLICE_VERSION_UPDATE (slice);
                }
                UNLOCK (&priv->lock);

                __unmask_cancellation (this);
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
                (void) pthread_testcancel();

                tv.tv_sec  = priv->fsync_interval;
                tv.tv_usec = 0;

                ret = select (0, NULL, NULL, NULL, &tv);
                if (ret)
                        continue;

                __mask_cancellation (this);

                ret = changelog_inject_single_event (this, priv, &cld);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to inject fsync event");

                __unmask_cancellation (this);
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

/* Begin: Geo-rep snapshot dependency changes */

/* changelog_color_fop_and_inc_cnt: Assign color and inc fop cnt.
 *
 * Assigning color and increment of corresponding fop count should happen
 * in a lock (i.e., there should be no window between them). If it does not,
 * we might miss draining those fops which are colored but not yet incremented
 * the count. Let's assume black fops are draining. If the black fop count
 * reaches zero, we say draining is completed but we miss black fops which are
 * not incremented fop count but color is assigned black.
 */

inline void
changelog_color_fop_and_inc_cnt (xlator_t *this, changelog_priv_t *priv,
                                                 changelog_local_t *local)
{
        if (!priv || !local)
                return;

        LOCK (&priv->lock);
        {
                local->color = priv->current_color;
                changelog_inc_fop_cnt (this, priv, local);
        }
        UNLOCK (&priv->lock);
}

/* Increments the respective fop counter based on the fop color */
inline void
changelog_inc_fop_cnt (xlator_t *this, changelog_priv_t *priv,
                                       changelog_local_t *local)
{
        int ret = 0;

        if (local) {
                if (local->color == FOP_COLOR_BLACK) {
                        ret = pthread_mutex_lock (&priv->dm.drain_black_mutex);
                        CHANGELOG_PTHREAD_ERROR_HANDLE_0 (ret, out);
                        {
                                priv->dm.black_fop_cnt++;
                        }
                        ret = pthread_mutex_unlock(&priv->dm.drain_black_mutex);
                        CHANGELOG_PTHREAD_ERROR_HANDLE_0 (ret, out);
                } else {
                        ret = pthread_mutex_lock (&priv->dm.drain_white_mutex);
                        CHANGELOG_PTHREAD_ERROR_HANDLE_0 (ret, out);
                        {
                                priv->dm.white_fop_cnt++;
                        }
                        ret = pthread_mutex_unlock(&priv->dm.drain_white_mutex);
                        CHANGELOG_PTHREAD_ERROR_HANDLE_0 (ret, out);
                }
        }
 out:
        return;
}

/* Decrements the respective fop counter based on the fop color */
inline void
changelog_dec_fop_cnt (xlator_t *this, changelog_priv_t *priv,
                                       changelog_local_t *local)
{
        int ret = 0;

        if (local) {
                if (local->color == FOP_COLOR_BLACK) {
                        ret = pthread_mutex_lock (&priv->dm.drain_black_mutex);
                        CHANGELOG_PTHREAD_ERROR_HANDLE_0 (ret, out);
                        {
                                priv->dm.black_fop_cnt--;
                                if (priv->dm.black_fop_cnt == 0 &&
                                    priv->dm.drain_wait_black == _gf_true) {
                                        ret = pthread_cond_signal (
                                                    &priv->dm.drain_black_cond);
                                        CHANGELOG_PTHREAD_ERROR_HANDLE_0 (ret,
                                                                           out);
                                        gf_log (this->name, GF_LOG_DEBUG,
                                                "Signalled draining of black");
                                }
                        }
                        ret = pthread_mutex_unlock(&priv->dm.drain_black_mutex);
                        CHANGELOG_PTHREAD_ERROR_HANDLE_0 (ret, out);
                } else {
                        ret = pthread_mutex_lock (&priv->dm.drain_white_mutex);
                        CHANGELOG_PTHREAD_ERROR_HANDLE_0 (ret, out);
                        {
                                priv->dm.white_fop_cnt--;
                                if (priv->dm.white_fop_cnt == 0 &&
                                    priv->dm.drain_wait_white == _gf_true) {
                                        ret = pthread_cond_signal (
                                                    &priv->dm.drain_white_cond);
                                        CHANGELOG_PTHREAD_ERROR_HANDLE_0 (ret,
                                                                           out);
                                        gf_log (this->name, GF_LOG_DEBUG,
                                                "Signalled draining of white");
                                }
                        }
                        ret = pthread_mutex_unlock(&priv->dm.drain_white_mutex);
                        CHANGELOG_PTHREAD_ERROR_HANDLE_0 (ret, out);
                }
        }
 out:
        return;
}

/* Write to a pipe setup between changelog main thread and changelog
 * rollover thread to initiate explicit rollover of changelog journal.
 */
inline int
changelog_barrier_notify (changelog_priv_t *priv, char *buf)
{
        int ret = 0;

        LOCK(&priv->lock);
                ret = changelog_write (priv->cr_wfd, buf, 1);
        UNLOCK(&priv->lock);
        return ret;
}

/* Clean up flags set on barrier notification */
inline void
changelog_barrier_cleanup (xlator_t *this, changelog_priv_t *priv,
                                                struct list_head *queue)
{
        int ret = 0;

        LOCK (&priv->bflags.lock);
                priv->bflags.barrier_ext = _gf_false;
        UNLOCK (&priv->bflags.lock);

        ret = pthread_mutex_lock (&priv->bn.bnotify_mutex);
        CHANGELOG_PTHREAD_ERROR_HANDLE_0 (ret, out);
        {
                priv->bn.bnotify = _gf_false;
        }
        ret = pthread_mutex_unlock (&priv->bn.bnotify_mutex);
        CHANGELOG_PTHREAD_ERROR_HANDLE_0 (ret, out);

        /* Disable changelog barrier and dequeue fops */
        LOCK (&priv->lock);
        {
                if (priv->barrier_enabled == _gf_true)
                        __chlog_barrier_disable (this, queue);
                else
                        ret = -1;
        }
        UNLOCK (&priv->lock);
        if (ret == 0)
                chlog_barrier_dequeue_all(this, queue);

 out:
        return;
}
/* End: Geo-Rep snapshot dependency changes */
