/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "xlator.h"
#include "defaults.h"
#include "logging.h"
#include "iobuf.h"
#include "syscall.h"

#include "changelog-helpers.h"
#include "changelog-encoders.h"
#include "changelog-mem-types.h"
#include "changelog-messages.h"

#include "changelog-encoders.h"
#include "changelog-rpc-common.h"
#include <pthread.h>

static void
changelog_cleanup_free_mutex (void *arg_mutex)
{
    pthread_mutex_t *p_mutex = (pthread_mutex_t*) arg_mutex;

    if (p_mutex)
            pthread_mutex_unlock(p_mutex);
}

int
changelog_thread_cleanup (xlator_t *this, pthread_t thr_id)
{
        int   ret    = 0;
        void *retval = NULL;

        /* send a cancel request to the thread */
        ret = pthread_cancel (thr_id);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_PTHREAD_CANCEL_FAILED,
                        "could not cancel thread");
                goto out;
        }

        ret = pthread_join (thr_id, &retval);
        if ((ret != 0) || (retval != PTHREAD_CANCELED)) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_PTHREAD_CANCEL_FAILED,
                        "cancel request not adhered as expected");
        }

 out:
        return ret;
}

void *
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

static int
changelog_selector_index (unsigned int selector)
{
        return (ffs (selector) - 1);
}

int
changelog_ev_selected (xlator_t *this,
                       changelog_ev_selector_t *selection,
                       unsigned int selector)
{
        int idx = 0;

        idx = changelog_selector_index (selector);
        gf_msg_debug (this->name, 0,
                      "selector ref count for %d (idx: %d): %d",
                      selector, idx, selection->ref[idx]);
        /* this can be lockless */
        return (idx < CHANGELOG_EV_SELECTION_RANGE
                 && (selection->ref[idx] > 0));
}

void
changelog_select_event (xlator_t *this,
                        changelog_ev_selector_t *selection,
                        unsigned int selector)
{
        int idx = 0;

        LOCK (&selection->reflock);
        {
                while (selector) {
                        idx = changelog_selector_index (selector);
                        if (idx < CHANGELOG_EV_SELECTION_RANGE) {
                                selection->ref[idx]++;
                                gf_msg_debug (this->name, 0,
                                              "selecting event %d", idx);
                        }
                        selector &= ~(1 << idx);
                }
        }
        UNLOCK (&selection->reflock);
}

void
changelog_deselect_event (xlator_t *this,
                          changelog_ev_selector_t *selection,
                          unsigned int selector)
{
        int idx = 0;

        LOCK (&selection->reflock);
        {
                while (selector) {
                        idx = changelog_selector_index (selector);
                        if (idx < CHANGELOG_EV_SELECTION_RANGE) {
                                selection->ref[idx]--;
                                gf_msg_debug (this->name, 0,
                                              "de-selecting event %d", idx);
                        }
                        selector &= ~(1 << idx);
                }
        }
        UNLOCK (&selection->reflock);
}

int
changelog_init_event_selection (xlator_t *this,
                                changelog_ev_selector_t *selection)
{
        int ret = 0;
        int j = CHANGELOG_EV_SELECTION_RANGE;

        ret = LOCK_INIT (&selection->reflock);
        if (ret != 0)
                return -1;

        LOCK (&selection->reflock);
        {
                while (j--) {
                        selection->ref[j] = 0;
                }
        }
        UNLOCK (&selection->reflock);

        return 0;
}

int
changelog_cleanup_event_selection (xlator_t *this,
                                   changelog_ev_selector_t *selection)
{
        int j = CHANGELOG_EV_SELECTION_RANGE;

        LOCK (&selection->reflock);
        {
                while (j--) {
                        if (selection->ref[j] > 0)
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        CHANGELOG_MSG_CLEANUP_ON_ACTIVE_REF,
                                        "changelog event selection cleaning up "
                                        " on active references");
                }
        }
        UNLOCK (&selection->reflock);

        return LOCK_DESTROY (&selection->reflock);
}

static void
changelog_perform_dispatch (xlator_t *this,
                            changelog_priv_t *priv, void *mem, size_t size)
{
        char *buf    = NULL;
        void *opaque = NULL;

        buf = rbuf_reserve_write_area (priv->rbuf, size, &opaque);
        if (!buf) {
                gf_msg_callingfn (this->name,
                                  GF_LOG_WARNING, 0,
                                  CHANGELOG_MSG_DISPATCH_EVENT_FAILED,
                                  "failed to dispatch event");
                return;
        }

        memcpy (buf, mem, size);
        rbuf_write_complete (opaque);
}

void
changelog_dispatch_event (xlator_t *this,
                          changelog_priv_t *priv, changelog_event_t *ev)
{
        changelog_ev_selector_t *selection = NULL;

        selection = &priv->ev_selection;
        if (changelog_ev_selected (this, selection, ev->ev_type)) {
                changelog_perform_dispatch (this, priv, ev, CHANGELOG_EV_SIZE);
        }
}

void
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

int
changelog_write (int fd, char *buffer, size_t len)
{
        ssize_t size = 0;
        size_t written = 0;

        while (written < len) {
                size = sys_write (fd,
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_HTIME_ERROR,
                        "Htime fd not available for updation");
                ret = -1;
                goto out;
        }
        strncpy (changelog_path, buffer, PATH_MAX);
        len = strlen (changelog_path);
        changelog_path[len] = '\0'; /* redundant */

        if (changelog_write (priv->htime_fd, (void*) changelog_path, len+1 ) < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_HTIME_ERROR,
                        "Htime file content write failed");
                ret =-1;
                goto out;
        }

        snprintf (x_value, sizeof x_value, "%lu:%d",
                 ts, priv->rollover_count);

        if (sys_fsetxattr (priv->htime_fd, HTIME_KEY, x_value,
                           strlen (x_value), XATTR_REPLACE)) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_HTIME_ERROR,
                        "Htime xattr updation failed with XATTR_REPLACE "
                        "Changelog: %s", changelog_path);

                if (sys_fsetxattr (priv->htime_fd, HTIME_KEY, x_value,
                                   strlen (x_value), 0)) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                CHANGELOG_MSG_HTIME_ERROR,
                                "Htime xattr updation failed "
                                "Changelog: %s", changelog_path);
                        ret = -1;
                        goto out;
                }
        }

        priv->rollover_count +=1;

out:
        return ret;
}

/*
 * Description: Check if the changelog to rollover is empty or not.
 * It is assumed that fd passed is already verified.
 *
 * Returns:
 * 1 : If found empty, changed path from "CHANGELOG.<TS>" to "changelog.<TS>"
 * 0 : If NOT empty, proceed usual.
 */
int
cl_is_empty (xlator_t *this, int fd)
{
        int             ret             = -1;
        size_t          elen            = 0;
        int             encoding        = -1;
        char            buffer[1024]    = {0,};
        struct stat     stbuf           = {0,};
        int             major_version   = -1;
        int             minor_version   = -1;

        ret = sys_fstat (fd, &stbuf);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_FSTAT_OP_FAILED,
                        "Could not stat (CHANGELOG)");
                goto out;
        }

        ret = sys_lseek (fd, 0, SEEK_SET);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_LSEEK_OP_FAILED,
                        "Could not lseek (CHANGELOG)");
                goto out;
        }

        CHANGELOG_GET_HEADER_INFO (fd, buffer, 1024, encoding,
                                   major_version, minor_version, elen);

        if (elen == stbuf.st_size) {
                ret = 1;
        } else {
                ret = 0;
        }

out:
        return ret;
}

/*
 * Description: Updates "CHANGELOG" to "changelog" for writing changelog path
 * to htime file.
 *
 * Returns:
 * 0  : Success
 * -1 : Error
 */
int
update_path (xlator_t *this, char *cl_path)
{
        char low_cl[]     = "changelog";
        char up_cl[]      = "CHANGELOG";
        char *found       = NULL;
        int  ret          = -1;

        found = strstr(cl_path, up_cl);

        if (found == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_LSEEK_OP_FAILED,
                        "Could not find CHANGELOG in changelog path");
                goto out;
        } else {
                strncpy(found, low_cl, strlen(low_cl));
        }

        ret = 0;
out:
        return ret;
}

static int
changelog_rollover_changelog (xlator_t *this,
                              changelog_priv_t *priv, unsigned long ts)
{
        int   ret             = -1;
        int   notify          = 0;
        int   cl_empty_flag   = 0;
        char  ofile[PATH_MAX] = {0,};
        char  nfile[PATH_MAX] = {0,};
        changelog_event_t ev  = {0,};

        if (priv->changelog_fd != -1) {
                ret = sys_fsync (priv->changelog_fd);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                CHANGELOG_MSG_FSYNC_OP_FAILED,
                                "fsync failed");
                }
                ret = cl_is_empty (this, priv->changelog_fd);
                if (ret == 1) {
                        cl_empty_flag = 1;
                } else if (ret == -1) {
                        /* Log error but proceed as usual */
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                CHANGELOG_MSG_DETECT_EMPTY_CHANGELOG_FAILED,
                                "Error detecting empty changelog");
                }
                sys_close (priv->changelog_fd);
                priv->changelog_fd = -1;
        }

        (void) snprintf (ofile, PATH_MAX,
                         "%s/"CHANGELOG_FILE_NAME, priv->changelog_dir);
        (void) snprintf (nfile, PATH_MAX,
                         "%s/"CHANGELOG_FILE_NAME".%lu",
                         priv->changelog_dir, ts);

        if (cl_empty_flag == 1) {
                ret = sys_unlink (ofile);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                CHANGELOG_MSG_UNLINK_OP_FAILED,
                                "error unlinking(empty cl) %s)",
                                ofile);
                        ret = 0;  /* Error in unlinking empty changelog should
                                     not break further changelog operation, so
                                     reset return value to 0*/
                }
        } else {
                ret = sys_rename (ofile, nfile);

                if (ret && (errno == ENOENT)) {
                        ret = 0;
                        goto out;
                }
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                CHANGELOG_MSG_RENAME_ERROR,
                                "error renaming %s -> %s",
                                ofile, nfile);
                }
        }

        if (!ret && (cl_empty_flag == 0)) {
                        notify = 1;
        }

        if (!ret) {
                if (cl_empty_flag) {
                        update_path (this, nfile);
                }
                ret = htime_update (this, priv, ts, nfile);
                if (ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR,
                                0, CHANGELOG_MSG_HTIME_ERROR,
                                "could not update htime file");
                        goto out;
                }
        }

        if (notify) {
                ev.ev_type = CHANGELOG_OP_TYPE_JOURNAL;
                memcpy (ev.u.journal.path, nfile, strlen (nfile) + 1);
                changelog_dispatch_event (this, priv, &ev);
        }
 out:
        /* If this is explicit rollover initiated by snapshot,
         * wakeup reconfigure thread waiting for changelog to
         * rollover. This should happen even in failure cases as
         * well otherwise snapshot will timeout and fail. Hence
         * moved under out.
         */
        if (priv->explicit_rollover) {
                priv->explicit_rollover = _gf_false;

                pthread_mutex_lock (&priv->bn.bnotify_mutex);
                {
                        if (ret) {
                                priv->bn.bnotify_error = _gf_true;
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        CHANGELOG_MSG_EXPLICIT_ROLLOVER_FAILED,
                                        "Fail snapshot because of "
                                        "previous errors");
                        } else {
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        CHANGELOG_MSG_BNOTIFY_INFO, "Explicit "
                                        "rollover changelog: %s signaling "
                                        "bnotify", nfile);
                        }
                        priv->bn.bnotify = _gf_false;
                        pthread_cond_signal (&priv->bn.bnotify_cond);
                }
                pthread_mutex_unlock (&priv->bn.bnotify_mutex);
        }
        return ret;
}

int
filter_cur_par_dirs (const struct dirent *entry)
{
        if (entry == NULL)
                return 0;

        if ((strcmp(entry->d_name, ".") == 0) ||
            (strcmp(entry->d_name, "..") == 0))
                return 0;
        else
                return 1;
}

/*
 * find_current_htime:
 *       It finds the latest htime file and sets the HTIME_CURRENT
 *       xattr.
 *       RETURN VALUE:
 *           -1 : Error
 *           ret: Number of directory entries;
 */

int
find_current_htime (int ht_dir_fd, const char *ht_dir_path, char *ht_file_bname)
{
        struct dirent       **namelist = NULL;
        int                   ret      = 0;
        int                   cnt      = 0;
        int                   i        = 0;
        xlator_t             *this     = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (ht_dir_path);

        cnt = scandir (ht_dir_path, &namelist, filter_cur_par_dirs, alphasort);
        if (cnt < 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_SCAN_DIR_FAILED,
                        "scandir failed");
        } else if (cnt > 0) {
                strncpy (ht_file_bname, namelist[cnt - 1]->d_name, NAME_MAX);
                ht_file_bname[NAME_MAX - 1] = 0;

                if (sys_fsetxattr (ht_dir_fd, HTIME_CURRENT, ht_file_bname,
                    strlen (ht_file_bname), 0)) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                CHANGELOG_MSG_FSETXATTR_FAILED,
                                "fsetxattr failed: HTIME_CURRENT");
                        ret = -1;
                        goto out;
                }

                if (sys_fsync (ht_dir_fd) < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                CHANGELOG_MSG_FSYNC_OP_FAILED,
                                "fsync failed");
                        ret = -1;
                        goto out;
                }
        }

 out:
        for (i = 0; i < cnt; i++)
                free (namelist[i]);
        free (namelist);

        if (ret)
                cnt = ret;

        return cnt;
}

/* Returns 0 on successful open of htime file
 * returns -1 on failure or error
 */
int
htime_open (xlator_t *this,
              changelog_priv_t *priv, unsigned long ts)
{
        int ht_file_fd                  = -1;
        int ht_dir_fd                   = -1;
        int ret                         = 0;
        int cnt                         = 0;
        char ht_dir_path[PATH_MAX]      = {0,};
        char ht_file_path[PATH_MAX]     = {0,};
        char ht_file_bname[NAME_MAX]    = {0,};
        char x_value[NAME_MAX]          = {0,};
        int flags                       = 0;
        unsigned long min_ts            = 0;
        unsigned long max_ts            = 0;
        unsigned long total             = 0;
        unsigned long total1            = 0;
        ssize_t size                    = 0;
        struct stat stat_buf            = {0,};
        unsigned long record_len        = 0;

        CHANGELOG_FILL_HTIME_DIR(priv->changelog_dir, ht_dir_path);

        /* Open htime directory to get HTIME_CURRENT */
        ht_dir_fd = open (ht_dir_path, O_RDONLY);
        if (ht_dir_fd == -1) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_OPEN_FAILED, "open failed: %s",
                        ht_dir_path);
                ret = -1;
                goto out;
        }

        size = sys_fgetxattr (ht_dir_fd, HTIME_CURRENT, ht_file_bname,
                             sizeof (ht_file_bname));
        if (size < 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_FGETXATTR_FAILED, "Error extracting"
                        " HTIME_CURRENT.");

                /* If upgrade scenario, find the latest HTIME.TSTAMP file
                 * and use the same. If error, create a new HTIME.TSTAMP
                 * file.
                 */
                cnt = find_current_htime (ht_dir_fd, ht_dir_path,
                                           ht_file_bname);
                if (cnt <= 0) {
                        gf_msg (this->name, GF_LOG_INFO, errno,
                                CHANGELOG_MSG_HTIME_INFO,
                                "HTIME_CURRENT not found. Changelog enabled"
                                " before init");
                        return htime_create (this, priv, ts);
                }

                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_HTIME_ERROR, "Error extracting"
                        " HTIME_CURRENT.");
        }

        gf_msg (this->name, GF_LOG_INFO, 0, CHANGELOG_MSG_HTIME_INFO,
                "HTIME_CURRENT: %s", ht_file_bname);
        (void) snprintf (ht_file_path, PATH_MAX, "%s/%s",
                         ht_dir_path, ht_file_bname);

        /* Open in append mode as existing htime file is used */
        flags |= (O_RDWR | O_SYNC | O_APPEND);
        ht_file_fd = open (ht_file_path, flags,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (ht_file_fd < 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_OPEN_FAILED,
                        "unable to open htime file: %s",
                        ht_file_path);
                ret = -1;
                goto out;
        }

        /* save this htime_fd in priv->htime_fd */
        priv->htime_fd = ht_file_fd;

        ret = sys_fstat (ht_file_fd, &stat_buf);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_HTIME_ERROR,
                        "unable to stat htime file: %s",
                        ht_file_path);
                ret = -1;
                goto out;
        }

        /* Initialize rollover-number in priv to current number */
        size = sys_fgetxattr (ht_file_fd, HTIME_KEY, x_value, sizeof (x_value));
        if (size < 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_FGETXATTR_FAILED, "error extracting max"
                        " timstamp from htime file %s",
                        ht_file_path);
                ret = -1;
                goto out;
        }

        sscanf (x_value, "%lu:%lu", &max_ts, &total);

        /* 22 = 1(/) + 20(CHANGELOG.TIMESTAMP) + 1(\x00) */
        record_len = strlen(priv->changelog_dir) + 22;
        total1 = stat_buf.st_size/record_len;
        if (total != total1) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        CHANGELOG_MSG_TOTAL_LOG_INFO,
                        "Mismatch of changelog count. "
                        "INIT CASE: XATTR TOTAL: %lu, SIZE TOTAL: %lu",
                        total, total1);
        }

        gf_msg (this->name, GF_LOG_INFO, 0,
                CHANGELOG_MSG_TOTAL_LOG_INFO,
                "INIT CASE: MIN: %lu, MAX: %lu,"
                " TOTAL CHANGELOGS: %lu", min_ts, max_ts, total);

        if (total < total1)
                priv->rollover_count = total1 + 1;
        else
                priv->rollover_count = total + 1;

out:
        if (ht_dir_fd != -1)
                sys_close (ht_dir_fd);
        return ret;
}

/* Returns 0 on successful creation of htime file
 * returns -1 on failure or error
 */
int
htime_create (xlator_t *this,
              changelog_priv_t *priv, unsigned long ts)
{
        int ht_file_fd                      = -1;
        int ht_dir_fd                       = -1;
        int ret                             = 0;
        char ht_dir_path[PATH_MAX]          = {0,};
        char ht_file_path[PATH_MAX]         = {0,};
        char ht_file_bname[NAME_MAX + 1]    = {0,};
        int flags                           = 0;

        gf_msg (this->name, GF_LOG_INFO, 0,
                CHANGELOG_MSG_HTIME_INFO, "Changelog enable: Creating new "
                "HTIME.%lu file", ts);

        CHANGELOG_FILL_HTIME_DIR(priv->changelog_dir, ht_dir_path);

        /* get the htime file name in ht_file_path */
        (void) snprintf (ht_file_path,PATH_MAX,"%s/%s.%lu",ht_dir_path,
                        HTIME_FILE_NAME, ts);

        flags |= (O_CREAT | O_RDWR | O_SYNC);
        ht_file_fd = open (ht_file_path, flags,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (ht_file_fd < 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_OPEN_FAILED,
                        "unable to create htime file: %s",
                        ht_file_path);
                ret = -1;
                goto out;
        }

        if (sys_fsetxattr (ht_file_fd, HTIME_KEY, HTIME_INITIAL_VALUE,
                       sizeof (HTIME_INITIAL_VALUE)-1,  0)) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_FSETXATTR_FAILED,
                        "Htime xattr initialization failed");
                ret = -1;
                goto out;
        }

        ret = sys_fsync (ht_file_fd);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_FSYNC_OP_FAILED,
                        "fsync failed");
                goto out;
        }

        /* Set xattr HTIME_CURRENT on htime directory to htime filename */
        ht_dir_fd = open (ht_dir_path, O_RDONLY);
        if (ht_dir_fd == -1) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_OPEN_FAILED, "open of %s failed",
                        ht_dir_path);
                ret = -1;
                goto out;
        }

        (void) snprintf (ht_file_bname, sizeof (ht_file_bname), "%s.%lu",
                         HTIME_FILE_NAME, ts);
        if (sys_fsetxattr (ht_dir_fd, HTIME_CURRENT, ht_file_bname,
            strlen (ht_file_bname), 0)) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_FSETXATTR_FAILED, "fsetxattr failed:"
                        " HTIME_CURRENT");
                ret = -1;
                goto out;
        }

        ret = sys_fsync (ht_dir_fd);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_FSYNC_OP_FAILED,
                        "fsync failed");
                goto out;
        }

        /* save this htime_fd in priv->htime_fd */
        priv->htime_fd = ht_file_fd;
        /* initialize rollover-number in priv to 1 */
        priv->rollover_count = 1;

out:
        if (ht_dir_fd != -1)
                sys_close (ht_dir_fd);
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
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_OPEN_FAILED, "unable to open %s file ",
                        c_snap_path);
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
                sys_close (priv->c_snap_fd);
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
        gf_msg (this->name, GF_LOG_INFO, 0,
                        CHANGELOG_MSG_SNAP_INFO,
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

        sys_close (priv->c_snap_fd);
        priv->c_snap_fd = -1;

        gf_msg (this->name, GF_LOG_INFO, 0,
                CHANGELOG_MSG_SNAP_INFO,
                "Stopped to log in call path");

        return ret;
}

int
changelog_open_journal (xlator_t *this,
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
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_OPEN_FAILED,
                        "unable to open/create changelog file %s."
                        " change-logging will be"
                        " inactive", changelog_path);
                goto out;
        }

        priv->changelog_fd = fd;

        (void) snprintf (buffer, 1024, CHANGELOG_HEADER,
                         CHANGELOG_VERSION_MAJOR,
                         CHANGELOG_VERSION_MINOR,
                         priv->ce->encoder);
        ret = changelog_write_change (priv, buffer, strlen (buffer));
        if (ret) {
                sys_close (priv->changelog_fd);
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
                ret = changelog_open_journal (this, priv);

        return ret;
}

/**
 * return the length of entry
 */
size_t
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_WRITE_FAILED,
                        "error writing csnap to disk");
        }
        gf_msg (this->name, GF_LOG_INFO, 0, CHANGELOG_MSG_SNAP_INFO,
                        "Successfully wrote to csnap");
        ret = 0;
out:
        return ret;
}

int
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                CHANGELOG_MSG_GET_TIME_OP_FAILED,
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
                ret = sys_fsync (priv->changelog_fd);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                CHANGELOG_MSG_FSYNC_OP_FAILED,
                                "fsync failed");
                }
                goto out;
        }

        ret = priv->ce->encode (this, cld);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_WRITE_FAILED,
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
                gf_msg_callingfn (this->name, GF_LOG_WARNING, 0,
                                  CHANGELOG_MSG_INODE_NOT_FOUND,
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

        gf_uuid_copy (local->cld.cld_gfid, gfid);

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
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_PTHREAD_ERROR, "pthread error:"
                        " Error:%d", ret);
        while (priv->dm.black_fop_cnt > 0) {
                gf_msg_debug (this->name, 0,
                              "Condtional wait on black fops: %ld",
                              priv->dm.black_fop_cnt);
                priv->dm.drain_wait_black = _gf_true;
                ret = pthread_cond_wait (&priv->dm.drain_black_cond,
                                         &priv->dm.drain_black_mutex);
                if (ret)
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                CHANGELOG_MSG_PTHREAD_COND_WAIT_FAILED,
                                "pthread cond wait failed: Error:%d",
                                ret);
        }
        priv->dm.drain_wait_black = _gf_false;
        ret = pthread_mutex_unlock (&priv->dm.drain_black_mutex);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_PTHREAD_ERROR, "pthread error:"
                        " Error:%d", ret);
        pthread_cleanup_pop (0);
        gf_msg_debug (this->name, 0,
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
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_PTHREAD_ERROR, "pthread error:"
                        " Error:%d", ret);
        while (priv->dm.white_fop_cnt > 0) {
                gf_msg_debug (this->name, 0,
                              "Condtional wait on white fops : %ld",
                        priv->dm.white_fop_cnt);
                priv->dm.drain_wait_white = _gf_true;
                ret = pthread_cond_wait (&priv->dm.drain_white_cond,
                                         &priv->dm.drain_white_mutex);
                if (ret)
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                CHANGELOG_MSG_PTHREAD_COND_WAIT_FAILED,
                                "pthread cond wait failed: Error:%d",
                                ret);
        }
        priv->dm.drain_wait_white = _gf_false;
        ret = pthread_mutex_unlock (&priv->dm.drain_white_mutex);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_PTHREAD_ERROR, "pthread error:"
                        " Error:%d", ret);
        pthread_cleanup_pop (0);
        gf_msg_debug (this->name, 0,
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
        struct timespec         tv              = {0,};
        changelog_log_data_t    cld             = {0,};
        changelog_time_slice_t *slice           = NULL;
        changelog_priv_t       *priv            = data;

        this = priv->cr.this;
        slice = &priv->slice;

        while (1) {
                (void) pthread_testcancel();

                tv.tv_sec  = time (NULL) + priv->rollover_time;
                tv.tv_nsec = 0;
                ret = 0; /* Reset ret to zero */

               /* The race between actual rollover and explicit rollover is
                * handled. If actual rollover is being done and the
                * explicit rollover event comes, the event is not missed.
                * Since explicit rollover sets 'cr.notify' to true, this
                * thread doesn't wait on 'pthread_cond_timedwait'.
                */
                pthread_cleanup_push (changelog_cleanup_free_mutex,
                                      &priv->cr.lock);
                pthread_mutex_lock (&priv->cr.lock);
                {
                        while (ret == 0 && !priv->cr.notify)
                                ret = pthread_cond_timedwait (&priv->cr.cond,
                                                              &priv->cr.lock,
                                                              &tv);
                        if (ret == 0)
                                priv->cr.notify = _gf_false;
                }
                pthread_mutex_unlock (&priv->cr.lock);
                pthread_cleanup_pop (0);

                if (ret == 0) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                CHANGELOG_MSG_BARRIER_INFO,
                                "Explicit wakeup on barrier notify");
                        priv->explicit_rollover = _gf_true;
                } else if (ret && ret != ETIMEDOUT) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                CHANGELOG_MSG_SELECT_FAILED,
                                "pthread_cond_timedwait failed");
                        continue;
                } else if (ret && ret == ETIMEDOUT) {
                        gf_msg_debug (this->name, 0, "Wokeup on timeout");
                }

               /* Reading curent_color without lock is fine here
                * as it is only modified here and is next to reading.
                */
                if (priv->current_color == FOP_COLOR_BLACK) {
                        LOCK(&priv->lock);
                                priv->current_color = FOP_COLOR_WHITE;
                        UNLOCK(&priv->lock);
                        gf_msg_debug (this->name, 0, "Black fops"
                                      " to be drained:%ld",
                                      priv->dm.black_fop_cnt);
                        changelog_drain_black_fops (this, priv);
                } else {
                        LOCK(&priv->lock);
                                priv->current_color = FOP_COLOR_BLACK;
                        UNLOCK(&priv->lock);
                        gf_msg_debug (this->name, 0, "White fops"
                                      " to be drained:%ld",
                                      priv->dm.white_fop_cnt);
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                CHANGELOG_MSG_GET_TIME_OP_FAILED,
                                "failed to fill rollover data");
                        continue;
                }

                _mask_cancellation ();

                LOCK (&priv->lock);
                {
                        ret = changelog_inject_single_event (this, priv, &cld);
                        if (!ret)
                                SLICE_VERSION_UPDATE (slice);
                }
                UNLOCK (&priv->lock);

                _unmask_cancellation ();
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

                _mask_cancellation ();

                ret = changelog_inject_single_event (this, priv, &cld);
                if (ret)
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                CHANGELOG_MSG_INJECT_FSYNC_FAILED,
                                "failed to inject fsync event");

                _unmask_cancellation ();
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
changelog_inode_ctx_t *
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
void
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

void
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
void
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
void
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
                                        gf_msg_debug (this->name, 0,
                                                      "Signalled "
                                                      "draining of black");
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
                                        gf_msg_debug (this->name, 0,
                                                      "Signalled "
                                                      "draining of white");
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
int
changelog_barrier_notify (changelog_priv_t *priv, char *buf)
{
        int ret = 0;

        pthread_mutex_lock (&priv->cr.lock);
        {
                ret = pthread_cond_signal (&priv->cr.cond);
                priv->cr.notify = _gf_true;
        }
        pthread_mutex_unlock (&priv->cr.lock);
        return ret;
}

/* Clean up flags set on barrier notification */
void
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

int32_t
changelog_fill_entry_buf (call_frame_t *frame, xlator_t *this,
                          loc_t *loc, changelog_local_t **local)
{
        changelog_opt_t  *co       = NULL;
        size_t            xtra_len = 0;
        char             *dup_path = NULL;
        char             *bname    = NULL;
        inode_t          *parent   = NULL;

        GF_ASSERT (this);

        parent = inode_parent (loc->inode, 0, 0);
        if (!parent) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_INODE_NOT_FOUND, "Parent inode not found"
                        " for gfid: %s", uuid_utoa (loc->inode->gfid));
                goto err;
        }

        CHANGELOG_INIT_NOCHECK (this, *local, loc->inode, loc->inode->gfid, 5);
        if (!(*local)) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_LOCAL_INIT_FAILED, "changelog local"
                        " initiatilization failed");
                goto err;
        }

        co = changelog_get_usable_buffer (*local);
        if (!co) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_NO_MEMORY,
                        "Failed to get buffer");
                goto err;
        }

        if (loc->inode->ia_type == IA_IFDIR) {
                CHANGLOG_FILL_FOP_NUMBER (co, GF_FOP_MKDIR, fop_fn, xtra_len);
                co++;
                CHANGELOG_FILL_UINT32 (co, S_IFDIR|0755, number_fn, xtra_len);
                co++;
        } else {
                CHANGLOG_FILL_FOP_NUMBER (co, GF_FOP_CREATE, fop_fn, xtra_len);
                co++;
                CHANGELOG_FILL_UINT32 (co, S_IFREG|0644, number_fn, xtra_len);
                co++;
        }

        CHANGELOG_FILL_UINT32 (co, frame->root->uid, number_fn, xtra_len);
        co++;

        CHANGELOG_FILL_UINT32 (co, frame->root->gid, number_fn, xtra_len);
        co++;

        dup_path = gf_strdup (loc->path);
        bname = basename (dup_path);

        CHANGELOG_FILL_ENTRY (co, parent->gfid, bname, entry_fn, entry_free_fn,
                              xtra_len, err);
        changelog_set_usable_record_and_length (*local, xtra_len, 5);

        if (dup_path)
                GF_FREE (dup_path);
        if (parent)
                inode_unref (parent);
        return 0;

err:
        if (dup_path)
                GF_FREE (dup_path);
        if (parent)
                inode_unref (parent);
        return -1;
}

/*
 * resolve_pargfid_to_path:
 *      It converts given pargfid to path by doing recursive readlinks at the
 * backend. If bname is given, it suffixes bname to pargfid to form the
 * complete path else it doesn't. It allocates memory for the path and is
 * caller's responsibility to free the same. If bname is NULL and pargfid
 * is ROOT, then it returns "."
 */

int
resolve_pargfid_to_path (xlator_t *this, const uuid_t pgfid,
                         char **path, char *bname)
{
        char             *linkname                  = NULL;
        char             *dir_handle                = NULL;
        char             *pgfidstr                  = NULL;
        char             *saveptr                   = NULL;
        ssize_t           len                       = 0;
        int               ret                       = 0;
        uuid_t            tmp_gfid                  = {0, };
        uuid_t            pargfid                   = {0, };
        changelog_priv_t *priv                      = NULL;
        char              gpath[PATH_MAX]           = {0,};
        char              result[PATH_MAX]          = {0,};
        char             *dir_name                  = NULL;
        char              pre_dir_name[PATH_MAX]    = {0,};

        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        gf_uuid_copy (pargfid, pgfid);
        if (!path || gf_uuid_is_null (pargfid)) {
                ret = -1;
                goto out;
        }

        if (__is_root_gfid (pargfid)) {
                if (bname)
                        *path = gf_strdup (bname);
                else
                        *path = gf_strdup (".");
                return ret;
        }

        dir_handle = alloca (PATH_MAX);
        linkname   = alloca (PATH_MAX);
        (void) snprintf (gpath, PATH_MAX, "%s/.glusterfs/",
                         priv->changelog_brick);

        while (!(__is_root_gfid (pargfid))) {
                snprintf (dir_handle, PATH_MAX, "%s/%02x/%02x/%s", gpath,
                          pargfid[0], pargfid[1], uuid_utoa (pargfid));

                len = sys_readlink (dir_handle, linkname, PATH_MAX);
                if (len < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                CHANGELOG_MSG_READLINK_OP_FAILED,
                                "could not read the "
                                "link from the gfid handle %s", dir_handle);
                        ret = -1;
                        goto out;
                }

                linkname[len] = '\0';

                pgfidstr = strtok_r (linkname + strlen("../../00/00/"), "/",
                                     &saveptr);
                dir_name = strtok_r (NULL, "/", &saveptr);

                snprintf (result, PATH_MAX, "%s/%s", dir_name, pre_dir_name);
                strncpy (pre_dir_name, result, sizeof(pre_dir_name));

                gf_uuid_parse (pgfidstr, tmp_gfid);
                gf_uuid_copy (pargfid, tmp_gfid);
        }

        if (bname)
                strncat (result, bname, strlen(bname) + 1);

        *path = gf_strdup (result);

out:
        return ret;
}
