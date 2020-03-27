#include <errno.h>
#include <dirent.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include <glusterfs/globals.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/logging.h>
#include <glusterfs/syscall.h>

#include "gf-changelog-helpers.h"
#include "gf-changelog-journal.h"

/* from the changelog translator */
#include "changelog-misc.h"
#include "changelog-lib-messages.h"
#include "changelog-mem-types.h"

/**
 * @API
 * gf_history_changelog_done:
 *    Move processed history changelog file from .processing
 *    to .processed
 *
 * ARGUMENTS:
 *    file(IN): path to processed history changelog file in
 *    .processing directory.
 *
 * RETURN VALUE:
 *    0: On success.
 *   -1: On error.
 */
int
gf_history_changelog_done(char *file)
{
    int ret = -1;
    char *buffer = NULL;
    xlator_t *this = NULL;
    gf_changelog_journal_t *jnl = NULL;
    gf_changelog_journal_t *hist_jnl = NULL;
    char to_path[PATH_MAX] = {
        0,
    };

    errno = EINVAL;

    this = THIS;
    if (!this)
        goto out;

    jnl = (gf_changelog_journal_t *)GF_CHANGELOG_GET_API_PTR(this);
    if (!jnl)
        goto out;

    hist_jnl = jnl->hist_jnl;
    if (!hist_jnl)
        goto out;

    if (!file || !strlen(file))
        goto out;

    /* make sure 'file' is inside ->jnl_working_dir */
    buffer = realpath(file, NULL);
    if (!buffer)
        goto out;

    if (strncmp(hist_jnl->jnl_working_dir, buffer,
                strlen(hist_jnl->jnl_working_dir)))
        goto out;

    (void)snprintf(to_path, PATH_MAX, "%s%s", hist_jnl->jnl_processed_dir,
                   basename(buffer));
    gf_msg_debug(this->name, 0, "moving %s to processed directory", file);
    ret = sys_rename(buffer, to_path);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno,
                CHANGELOG_LIB_MSG_RENAME_FAILED, "from=%s", file, "to=%s",
                to_path, NULL);
        goto out;
    }

    ret = 0;

out:
    if (buffer)
        free(buffer); /* allocated by realpath() */
    return ret;
}

/**
 * @API
 *  gf_history_changelog_start_fresh:
 *     For a set of changelogs, start from the beginning.
 *     It will truncates the history tracker fd.
 *
 *  RETURN VALUES:
 *     0: On success.
 *    -1: On error.
 */
int
gf_history_changelog_start_fresh()
{
    xlator_t *this = NULL;
    gf_changelog_journal_t *jnl = NULL;
    gf_changelog_journal_t *hist_jnl = NULL;

    this = THIS;
    if (!this)
        goto out;

    errno = EINVAL;

    jnl = (gf_changelog_journal_t *)GF_CHANGELOG_GET_API_PTR(this);
    if (!jnl)
        goto out;

    hist_jnl = jnl->hist_jnl;
    if (!hist_jnl)
        goto out;

    if (gf_ftruncate(hist_jnl->jnl_fd, 0))
        goto out;

    return 0;

out:
    return -1;
}

/**
 * @API
 *  gf_history_changelog_next_change:
 *     Return the next history changelog file entry. Zero means all
 *     history chanelogs are consumed.
 *
 *  ARGUMENTS:
 *     bufptr(OUT): Path to unprocessed history changelog file
 *                  from tracker file.
 *     maxlen(IN): Usually PATH_MAX.
 *
 *  RETURN VALUES:
 *     size: On success.
 *     -1  : On error.
 */
ssize_t
gf_history_changelog_next_change(char *bufptr, size_t maxlen)
{
    ssize_t size = -1;
    int tracker_fd = 0;
    xlator_t *this = NULL;
    gf_changelog_journal_t *jnl = NULL;
    gf_changelog_journal_t *hist_jnl = NULL;
    char buffer[PATH_MAX] = {
        0,
    };

    if (maxlen > PATH_MAX) {
        errno = ENAMETOOLONG;
        goto out;
    }

    errno = EINVAL;

    this = THIS;
    if (!this)
        goto out;

    jnl = (gf_changelog_journal_t *)GF_CHANGELOG_GET_API_PTR(this);
    if (!jnl)
        goto out;

    hist_jnl = jnl->hist_jnl;
    if (!hist_jnl)
        goto out;

    tracker_fd = hist_jnl->jnl_fd;

    size = gf_readline(tracker_fd, buffer, maxlen);
    if (size < 0) {
        size = -1;
        goto out;
    }

    if (size == 0)
        goto out;

    memcpy(bufptr, buffer, size - 1);
    bufptr[size - 1] = '\0';

out:
    return size;
}

/**
 * @API
 *  gf_history_changelog_scan:
 *     Scan and generate a list of change entries.
 *     Calling this api multiple times (without calling gf_changlog_done())
 *     would result new changelogs(s) being refreshed in the tracker file.
 *     This call also acts as a cancellation point for the consumer.
 *
 *  RETURN VALUES:
 *      +ve integer : success and keep scanning.(count of changelogs)
 *      0           : success and done scanning.
 *     -1           : error.
 *
 *  NOTE: After first 0 return call_get_next change for once more time
 *        to empty the tracker
 *
 */
ssize_t
gf_history_changelog_scan()
{
    int tracker_fd = 0;
    size_t off = 0;
    xlator_t *this = NULL;
    size_t nr_entries = 0;
    gf_changelog_journal_t *jnl = NULL;
    gf_changelog_journal_t *hist_jnl = NULL;
    struct dirent *entry = NULL;
    struct dirent scratch[2] = {
        {
            0,
        },
    };
    char buffer[PATH_MAX] = {
        0,
    };
    static int is_last_scan;

    this = THIS;
    if (!this)
        goto out;

    jnl = (gf_changelog_journal_t *)GF_CHANGELOG_GET_API_PTR(this);
    if (!jnl)
        goto out;
    if (JNL_IS_API_DISCONNECTED(jnl)) {
        errno = ENOTCONN;
        goto out;
    }

    hist_jnl = jnl->hist_jnl;
    if (!hist_jnl)
        goto out;

retry:
    if (is_last_scan == 1)
        return 0;
    if (hist_jnl->hist_done == 0)
        is_last_scan = 1;

    errno = EINVAL;
    if (hist_jnl->hist_done == -1)
        goto out;

    tracker_fd = hist_jnl->jnl_fd;

    if (gf_ftruncate(tracker_fd, 0))
        goto out;

    rewinddir(hist_jnl->jnl_dir);

    for (;;) {
        errno = 0;
        entry = sys_readdir(hist_jnl->jnl_dir, scratch);
        if (!entry || errno != 0)
            break;

        if (strcmp(basename(entry->d_name), ".") == 0 ||
            strcmp(basename(entry->d_name), "..") == 0)
            continue;

        nr_entries++;

        GF_CHANGELOG_FILL_BUFFER(hist_jnl->jnl_processing_dir, buffer, off,
                                 strlen(hist_jnl->jnl_processing_dir));
        GF_CHANGELOG_FILL_BUFFER(entry->d_name, buffer, off,
                                 strlen(entry->d_name));
        GF_CHANGELOG_FILL_BUFFER("\n", buffer, off, 1);

        if (gf_changelog_write(tracker_fd, buffer, off) != off) {
            gf_msg(this->name, GF_LOG_ERROR, 0, CHANGELOG_LIB_MSG_WRITE_FAILED,
                   "error writing changelog filename"
                   " to tracker file");
            break;
        }
        off = 0;
    }

    gf_msg_debug(this->name, 0, "hist_done %d, is_last_scan: %d",
                 hist_jnl->hist_done, is_last_scan);

    if (!entry) {
        if (gf_lseek(tracker_fd, 0, SEEK_SET) != -1) {
            if (nr_entries > 0)
                return nr_entries;
            else {
                sleep(1);
                goto retry;
            }
        }
    }
out:
    return -1;
}

/**
 * "gf_history_consume" is a worker function for history.
 * parses and moves changelogs files from index "from"
 * to index "to" in open htime file whose fd is "fd".
 */

#define MAX_PARALLELS 10

void *
gf_history_consume(void *data)
{
    xlator_t *this = NULL;
    gf_changelog_journal_t *jnl = NULL;
    gf_changelog_journal_t *hist_jnl = NULL;
    int ret = 0;
    int iter = 0;
    int n_parallel = 0;
    int n_envoked = 0;
    gf_boolean_t publish = _gf_true;
    pthread_t th_id[MAX_PARALLELS] = {
        0,
    };
    gf_changelog_history_data_t *hist_data = NULL;
    gf_changelog_consume_data_t ccd[MAX_PARALLELS] = {
        {{0}},
    };
    gf_changelog_consume_data_t *curr = NULL;

    hist_data = (gf_changelog_history_data_t *)data;
    if (hist_data == NULL) {
        ret = -1;
        goto out;
    }

    n_parallel = hist_data->n_parallel;

    THIS = hist_data->this;
    this = hist_data->this;
    if (!this) {
        ret = -1;
        goto out;
    }

    jnl = (gf_changelog_journal_t *)GF_CHANGELOG_GET_API_PTR(this);
    if (!jnl) {
        ret = -1;
        goto out;
    }

    hist_jnl = jnl->hist_jnl;
    if (!hist_jnl) {
        ret = -1;
        goto out;
    }

    n_envoked = 0;

    for (iter = 0; (iter < n_parallel); iter++) {
        curr = &ccd[iter];

        curr->this = this;
        curr->jnl = hist_jnl;
        strcpy(curr->changelog_path, hist_data->changelog_path);
        curr->no_publish = _gf_true;

        curr->retval = 0;
        memset(curr->changelog, '\0', PATH_MAX);

        ret = gf_thread_create(&th_id[iter], NULL, gf_changelog_consume, curr,
                               "clogc%03hx", (iter + 1) & 0x3ff);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, ret,
                   CHANGELOG_LIB_MSG_THREAD_CREATION_FAILED,
                   "could not create consume-thread");
            goto sync;
        } else
            n_envoked++;
    }

sync:
    for (iter = 0; iter < n_envoked; iter++) {
        ret = pthread_join(th_id[iter], NULL);
        if (ret) {
            publish = _gf_false;
            gf_msg(this->name, GF_LOG_ERROR, ret,
                   CHANGELOG_LIB_MSG_PTHREAD_JOIN_FAILED,
                   "pthread_join() error");
            /* try to join the rest */
            continue;
        }

        if (publish == _gf_false)
            continue;

        curr = &ccd[iter];
        if (ccd->retval) {
            publish = _gf_false;
            gf_smsg(this->name, GF_LOG_ERROR, 0,
                    CHANGELOG_LIB_MSG_PARSE_ERROR_CEASED, NULL);
            continue;
        }

        ret = gf_changelog_publish(curr->this, curr->jnl, curr->changelog);
        if (ret) {
            publish = _gf_false;
            gf_msg(this->name, GF_LOG_ERROR, 0, CHANGELOG_LIB_MSG_PUBLISH_ERROR,
                   "publish error, ceased publishing...");
        }
    }

    /* informing "parsing done". */
    hist_jnl->hist_done = (publish == _gf_true) ? 0 : -1;

out:
    GF_FREE(hist_data);
    return NULL;
}

/**
 * @API
 * gf_history_changelog() : Get/parse historical changelogs and get them ready
 * for consumption.
 *
 * Arguments:
 * @changelog_dir : Directory location from where history changelogs are
 * supposed to be consumed.
 * @start: Unix timestamp FROM where changelogs should be consumed.
 * @end: Unix timestamp TO where changelogsshould be consumed.
 * @n_parallel : degree of parallelism while changelog parsing.
 * @actual_end : the end time till where changelogs are available.
 *
 * Return:
 * Returns <timestamp> on success, the last time till where changelogs are
 *      available.
 * Returns -1 on failure(error).
 */

/* gf_history_changelog returns actual_end and spawns threads to
 * parse historical changelogs. The return values are as follows.
 *     0 : On success
 *     1 : Successful, but partial historical changelogs available,
 *         end time falls into different htime file or future time
 *    -2 : Error, requested historical changelog not available, not
 *         even partial
 *    -1 : On any error
 */
int
gf_history_changelog(char *changelog_dir, unsigned long start,
                     unsigned long end, int n_parallel,
                     unsigned long *actual_end)
{
    int ret = 0;
    char from_year[40];
    char from_month[40];
    char from_day[40];
    xlator_t *this = NULL;
    gf_changelog_journal_t *jnl = NULL;
    gf_changelog_journal_t *hist_jnl = NULL;
    gf_changelog_history_data_t *hist_data = NULL;
    DIR *entry_od = NULL;
    DIR *year_entry_od = NULL;
    DIR *month_entry_od = NULL;
    DIR *day_entry_od = NULL;

    struct dirent *entry = NULL;
    struct dirent *year_entry = NULL;
    struct dirent *month_entry = NULL;
    struct dirent *day_entry = NULL;
    struct dirent *changelogs = NULL;
    struct tm *gmtime_val;
    pthread_t consume_th = 0;
    gf_boolean_t partial_history = _gf_false;
    time_t val_t;
    char *buffer = NULL;

    pthread_attr_t attr;

    this = THIS;
    if (!this) {
        ret = -1;
        goto out;
    }

    ret = pthread_attr_init(&attr);
    if (ret != 0) {
        gf_msg(this->name, GF_LOG_ERROR, errno, CHANGELOG_LIB_MSG_PTHREAD_ERROR,
               "Pthread init failed");
        return -1;
    }

    jnl = (gf_changelog_journal_t *)GF_CHANGELOG_GET_API_PTR(this);
    if (!jnl) {
        ret = -1;
        goto out;
    }

    hist_jnl = (gf_changelog_journal_t *)jnl->hist_jnl;
    if (!hist_jnl) {
        ret = -1;
        goto out;
    }

    gf_smsg(this->name, GF_LOG_INFO, 0, CHANGELOG_LIB_MSG_REQUESTING_INFO,
            "start=%lu", start, "end=%lu", end, NULL);

    /* basic sanity check */
    if (start > end || n_parallel <= 0) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, CHANGELOG_LIB_MSG_HIST_FAILED,
                "start=%lu", start, "end=%lu", end, "thread_count=%d",
                n_parallel, NULL);
        ret = -1;
        goto out;
    }

    /* cap parallelism count */
    if (n_parallel > MAX_PARALLELS)
        n_parallel = MAX_PARALLELS;

    entry_od = sys_opendir(changelog_dir);
    ret = getxattr(entry->d_name, "CHANGELOG-ENABLE-TIME", &val_t, sizeof(val_t));
    if (ret <= 0) {
        ret = -1;
        gf_smsg(this->name, GF_LOG_ERROR, errno,
                CHANGELOG_LIB_MSG_GET_XATTR_FAILED, "path=%s", entry->d_name,
                NULL);
    }

    gmtime_val = gmtime(&val_t);
    strftime(from_year, sizeof(from_year), "%Y", gmtime_val);
    strftime(from_month, sizeof(from_month), "%m", gmtime_val);
    strftime(from_day, sizeof(from_day), "%d", gmtime_val);

    while (((year_entry = readdir(entry_od)) != 0) &&
           (atoi(from_year) <= atoi(entry->d_name))) {
        year_entry_od = sys_opendir(entry->d_name);
        while (((month_entry = readdir(year_entry_od)) != 0) &&
               (atoi(from_month) <= atoi(year_entry->d_name))) {
            month_entry_od = sys_opendir(year_entry->d_name);
            while (((day_entry = readdir(month_entry_od)) != 0) &&
                   (atoi(from_day) <= atoi(day_entry->d_name))) {
                day_entry_od = sys_opendir(day_entry->d_name);
                while ((changelogs = readdir(day_entry_od)) != 0) {
                    realpath(day_entry->d_name, changelog_dir);
                    if (!changelog_dir) {
                        goto out;
                    }

                    hist_data = GF_CALLOC(1,
                                          sizeof(gf_changelog_history_data_t),
                                          gf_changelog_mt_history_data_t);

                    hist_data->n_parallel = n_parallel;
                    hist_data->this = this;
                    strcpy(hist_data->changelog_path, changelog_dir);

                    ret = pthread_attr_setdetachstate(&attr,
                                                      PTHREAD_CREATE_DETACHED);
                    if (ret != 0) {
                        gf_msg(this->name, GF_LOG_ERROR, ret,
                               CHANGELOG_LIB_MSG_PTHREAD_ERROR,
                               "unable to sets the detach"
                               " state attribute");
                        ret = -1;
                        goto out;
                    }

                    /* spawn a thread for background parsing & publishing */
                    ret = gf_thread_create(&consume_th, &attr,
                                           gf_history_consume, hist_data,
                                           "cloghcon");
                    if (ret) {
                        gf_msg(this->name, GF_LOG_ERROR, ret,
                               CHANGELOG_LIB_MSG_THREAD_CREATION_FAILED,
                               "creation of consume parent-thread"
                               " failed.");
                        ret = -1;
                        goto out;
                    }

                    snprintf(buffer, 32, "CHANGELOG.%ld", end);
                    if (strcmp(day_entry->d_name, buffer) == 0)
                        goto out;
                }
            }
        }
    }

out:
    if (ret < 0) {
        GF_FREE(hist_data);
        (void)pthread_attr_destroy(&attr);

        return ret;
    }

    hist_jnl->hist_done = 1;

    if (partial_history) {
        ret = 1;
    }

    return ret;
}
