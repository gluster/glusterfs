#include <errno.h>
#include <dirent.h>
#include <stddef.h>
#include <sys/types.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <string.h>

#include "globals.h"
#include "glusterfs.h"
#include "logging.h"
#include "syscall.h"

#include "gf-changelog-helpers.h"

/* from the changelog translator */
#include "changelog-misc.h"
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
gf_history_changelog_done (char *file)
{
        int                     ret                     = -1;
        char                    *buffer                 = NULL;
        xlator_t                *this                   = NULL;
        gf_changelog_t          *gfc                    = NULL;
        gf_changelog_t          *hist_gfc               = NULL;
        char                    to_path[PATH_MAX]       = {0,};

        errno = EINVAL;

        this = THIS;
        if (!this)
                goto out;

        gfc = (gf_changelog_t *) this->private;
        if (!gfc)
                goto out;

        hist_gfc = gfc->hist_gfc;
        if (!hist_gfc)
                goto out;

        if (!file || !strlen (file))
                goto out;

        /* make sure 'file' is inside ->gfc_working_dir */
        buffer = realpath (file, NULL);
        if (!buffer)
                goto out;

        if (strncmp (hist_gfc->gfc_working_dir,
                     buffer, strlen (hist_gfc->gfc_working_dir)))
                goto out;

        (void) snprintf (to_path, PATH_MAX, "%s%s",
                         hist_gfc->gfc_processed_dir, basename (buffer));
        gf_log (this->name, GF_LOG_DEBUG,
                "moving %s to processed directory", file);
        ret = rename (buffer, to_path);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot move %s to %s (reason: %s)",
                        file, to_path, strerror (errno));
                goto out;
        }

        ret = 0;

 out:
        if (buffer)
                free (buffer); /* allocated by realpath() */
        return ret;
}

/**
 * @API
 *  gf_history_changelog_start_fresh:
 *     For a set of changelogs, start from the begining.
 *     It will truncates the history tracker fd.
 *
 *  RETURN VALUES:
 *     0: On success.
 *    -1: On error.
 */
int
gf_history_changelog_start_fresh ()
{
        xlator_t                *this                   = NULL;
        gf_changelog_t          *gfc                    = NULL;
        gf_changelog_t          *hist_gfc               = NULL;

        this = THIS;
        if (!this)
                goto out;

        errno = EINVAL;

        gfc = (gf_changelog_t *) this->private;
        if (!gfc)
                goto out;

        hist_gfc = gfc->hist_gfc;
        if (!hist_gfc)
                goto out;

        if (gf_ftruncate (hist_gfc->gfc_fd, 0))
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
gf_history_changelog_next_change (char *bufptr, size_t maxlen)
{
        ssize_t                 size                    = -1;
        int                     tracker_fd              = 0;
        xlator_t                *this                   = NULL;
        gf_changelog_t          *gfc                    = NULL;
        gf_changelog_t          *hist_gfc               = NULL;
        char                    buffer[PATH_MAX]        = {0,};

        errno = EINVAL;

        this = THIS;
        if (!this)
                goto out;

        gfc = (gf_changelog_t *) this->private;
        if (!gfc)
                goto out;

        hist_gfc = gfc->hist_gfc;
        if (!hist_gfc)
                goto out;

        tracker_fd = hist_gfc->gfc_fd;

        size = gf_readline (tracker_fd, buffer, maxlen);
        if (size < 0) {
                size = -1;
                goto out;
        }

        if (size == 0)
                goto out;

        memcpy (bufptr, buffer, size - 1);
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
gf_history_changelog_scan ()
{
        int             ret        = 0;
        int             tracker_fd = 0;
        size_t          len        = 0;
        size_t          off        = 0;
        xlator_t       *this       = NULL;
        size_t          nr_entries = 0;
        gf_changelog_t *gfc        = NULL;
        gf_changelog_t *hist_gfc   = NULL;
        struct dirent  *entryp     = NULL;
        struct dirent  *result     = NULL;
        char buffer[PATH_MAX]      = {0,};
        static int    is_last_scan = 0;

        this = THIS;
        if (!this)
                goto out;

        gfc = (gf_changelog_t *) this->private;
        if (!gfc)
                goto out;

        hist_gfc = gfc->hist_gfc;
        if (!hist_gfc)
                goto out;

 retry:
        if (is_last_scan == 1)
                return 0;
        if (hist_gfc->hist_done == 0)
                is_last_scan = 1;

        errno = EINVAL;
        if (hist_gfc->hist_done == -1)
                goto out;

        tracker_fd = hist_gfc->gfc_fd;

        if (gf_ftruncate (tracker_fd, 0))
                goto out;

        len = offsetof (struct dirent, d_name)
                + pathconf (hist_gfc->gfc_processing_dir, _PC_NAME_MAX) + 1;
        entryp = GF_CALLOC (1, len,
                            gf_changelog_mt_libgfchangelog_dirent_t);
        if (!entryp)
                goto out;

        rewinddir (hist_gfc->gfc_dir);
        while (1) {
                ret = readdir_r (hist_gfc->gfc_dir, entryp, &result);
                if (ret || !result)
                        break;

                if ( !strcmp (basename (entryp->d_name), ".")
                     || !strcmp (basename (entryp->d_name), "..") )
                        continue;

                nr_entries++;

                GF_CHANGELOG_FILL_BUFFER (hist_gfc->gfc_processing_dir,
                                          buffer, off,
                                          strlen (hist_gfc->gfc_processing_dir));
                GF_CHANGELOG_FILL_BUFFER (entryp->d_name, buffer,
                                          off, strlen (entryp->d_name));
                GF_CHANGELOG_FILL_BUFFER ("\n", buffer, off, 1);

                if (gf_changelog_write (tracker_fd, buffer, off) != off) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "error writing changelog filename"
                                " to tracker file");
                        break;
                }
                off = 0;
        }

        GF_FREE (entryp);

        gf_log (this->name, GF_LOG_DEBUG,
                "hist_done %d, is_last_scan: %d", hist_gfc->hist_done, is_last_scan);

        if (!result) {
                if (gf_lseek (tracker_fd, 0, SEEK_SET) != -1) {
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

/*
 * Gets timestamp value at the changelog path at index.
 * Returns 0 on success(updates given time-stamp), -1 on failure.
 */
int
gf_history_get_timestamp (int fd, int index, int len,
                          unsigned long *ts)
{
        xlator_t        *this             = NULL;
        int             n_read            = -1;
        char            path_buf[PATH_MAX]= {0,};
        char            *iter             = path_buf;
        size_t          offset            = index * (len+1);
        unsigned long   value             = 0;
        int             ret               = 0;

        this = THIS;
        if (!this) {
                return -1;
        }

        n_read = pread (fd, path_buf, len, offset);
        if (n_read < 0 ) {
                ret = -1;
                gf_log ( this->name, GF_LOG_ERROR,
                         "could not read from htime file");
                goto out;
        }
        iter+= len - TIMESTAMP_LENGTH;
        sscanf (iter, "%lu",&value);
out:
        if(ret == 0)
                *ts = value;
        return ret;
}

/*
 * Function to ensure correctness of search
 * Checks whether @value is there next to @target_index or not
 */
int
gf_history_check ( int fd, int target_index, unsigned long value, int len)
{
        int             ret = 0;
        unsigned long   ts1 = 0;
        unsigned long   ts2 = 0;

        if (target_index == 0) {
                ret = gf_history_get_timestamp (fd, target_index, len, &ts1);
                if (ret == -1)
                        goto out;
                if (value <= ts1)
                        goto out;
                else {
                        ret = -1;
                        goto out;
                }
        }

        ret = gf_history_get_timestamp (fd, target_index, len, &ts1);
        if (ret ==-1)
                goto out;
        ret = gf_history_get_timestamp (fd, target_index -1, len, &ts2);
        if (ret ==-1)
                goto out;

        if ( (value <= ts1) && (value > ts2) ) {
                goto out;
        }
        else
                ret = -1;
out:
        return ret;
}

/*
 * This is a "binary search" based search function which checks neighbours
 * for in-range availability of the value to be searched and provides the
 * index at which the changelog file nearest to the requested timestamp(value)
 * can be read from.
 *
 * Actual offset can be calculated as (index* (len+1) ).
 * "1" is because the changelog paths are null terminated.
 *
 * @path        : Htime file to search in
 * @value       : time stamp to search
 * @from        : start index to search
 * @to          : end index to search
 * @len         : length of fixes length strings seperated by null
 */

int
gf_history_b_search (int fd, unsigned long value,
                     unsigned long from, unsigned long to, int len)
{
        int             m_index   = -1;
        unsigned long   cur_value = 0;
        unsigned long   ts1       = 0;
        int             ret       = 0;

        m_index = (from + to)/2;

        if ( (to - from) <=1 ) {
                /* either one or 2 changelogs left */
                if ( to != from ) {
                        /* check if value is less or greater than to
                         * return accordingly
                         */
                        ret = gf_history_get_timestamp (fd, from, len, &ts1);
                        if (ret ==-1)
                                goto out;
                        if ( ts1 >= value) {
                                /* actually compatision should be
                                 * exactly == but considering
                                 *
                                 * case of only 2 changelogs in htime file
                                 */
                                return from;
                        }
                        else
                                return to;
                }
                else
                        return to;
        }

        ret = gf_history_get_timestamp (fd, m_index, len, &cur_value);
        if (ret == -1)
                goto out;
        if (cur_value == value) {
                return m_index;
        }
        else if (value > cur_value) {
                ret = gf_history_get_timestamp (fd, m_index+1, len, &cur_value);
                if (ret == -1)
                        goto out;
                if (value < cur_value)
                        return m_index + 1;
                else
                        return gf_history_b_search (fd, value,
                                                    m_index+1, to, len);
        }
        else {
                if (m_index ==0) {
                       /*  we are sure that values exists
                        *  in this htime file
                        */
                        return 0;
                }
                else {
                        ret = gf_history_get_timestamp (fd, m_index-1, len,
                                                        &cur_value);
                        if (ret == -1)
                                goto out;
                        if (value > cur_value) {
                                return m_index;
                        }
                        else
                                return gf_history_b_search (fd, value, from,
                                                            m_index-1, len);
                }
        }
out:
        return -1;
}

void *
gf_changelog_consume_wrap (void* data)
{
        int                          ret   = -1;
        ssize_t                      nread = 0;
        xlator_t                    *this  = NULL;
        gf_changelog_consume_data_t *ccd   = NULL;

        ccd = (gf_changelog_consume_data_t *) data;
        this = ccd->this;

        ccd->retval = -1;

        nread = pread (ccd->fd, ccd->changelog, PATH_MAX, ccd->offset);
        if (nread < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot read from history metadata file (reason %s)",
                        strerror (errno));
                goto out;
        }

        /* TODO: handle short reads and EOF. */

        ret = gf_changelog_consume (ccd->this,
                                    ccd->gfc, ccd->changelog, _gf_true);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "could not parse changelog: %s", ccd->changelog);
                goto out;
        }

        ccd->retval = 0;

 out:
        return NULL;
}

/**
 * "gf_history_consume" is a worker function for history.
 * parses and moves changelogs files from index "from"
 * to index "to" in open htime file whose fd is "fd".
 */

#define MAX_PARALLELS  10

void *
gf_history_consume (void * data)
{
        xlator_t                    *this              = NULL;
        gf_changelog_t              *gfc               = NULL;
        gf_changelog_t              *hist_gfc          = NULL;
        int                          ret               = 0;
        int                          iter              = 0;
        int                          fd                = -1;
        int                          from              = -1;
        int                          to                = -1;
        int                          len               = -1;
        int                          n_parallel        = 0;
        int                          n_envoked         = 0;
        gf_boolean_t                 publish           = _gf_true;
        pthread_t th_id[MAX_PARALLELS]                 = {0,};
        gf_changelog_history_data_t *hist_data         = NULL;
        gf_changelog_consume_data_t ccd[MAX_PARALLELS] = {{0},};
        gf_changelog_consume_data_t *curr              = NULL;

        hist_data = (gf_changelog_history_data_t *) data;
        if (hist_data == NULL) {
                ret = -1;
                goto out;
        }

        fd         = hist_data->htime_fd;
        from       = hist_data->from;
        to         = hist_data->to;
        len        = hist_data->len;
        n_parallel = hist_data->n_parallel;

        this = THIS;
        if (!this) {
                ret = -1;
                goto out;
        }

        gfc = (gf_changelog_t *) this->private;
        if (!gfc) {
                ret = -1;
                goto out;
        }

        hist_gfc = gfc->hist_gfc;
        if (!hist_gfc) {
                ret = -1;
                goto out;
        }

        while (from <= to) {
                n_envoked = 0;

                for (iter = 0 ; (iter < n_parallel) && (from <= to); iter++) {
                        curr = &ccd[iter];

                        curr->this   = this;
                        curr->gfc    = hist_gfc;
                        curr->fd     = fd;
                        curr->offset = from * (len + 1);

                        curr->retval = 0;
                        memset (curr->changelog, '\0', PATH_MAX);

                        ret = pthread_create (&th_id[iter], NULL,
                                              gf_changelog_consume_wrap, curr);
                        if (ret) {
                                gf_log ( this->name, GF_LOG_ERROR,
                                        "could not create consume-thread"
                                        " reason (%s)", strerror (ret));
                                ret = -1;
                                goto sync;
                        } else
                                n_envoked++;

                        from++;
                }

        sync:
                for (iter = 0; iter < n_envoked; iter++) {
                        ret = pthread_join (th_id[iter], NULL);
                        if (ret) {
                                publish = _gf_false;
                                gf_log (this->name, GF_LOG_ERROR,
                                        "pthread_join() error %s",
                                        strerror (ret));
                                /* try to join the rest */
                                continue;
                        }

                        if (publish == _gf_false)
                                continue;

                        curr = &ccd[iter];
                        if (ccd->retval) {
                                publish = _gf_false;
                                gf_log (this->name, GF_LOG_ERROR,
                                        "parsing error, ceased publishing...");
                                continue;
                        }

                        ret = gf_changelog_publish (curr->this,
                                                    curr->gfc, curr->changelog);
                        if (ret) {
                                publish = _gf_false;
                                gf_log (this->name, GF_LOG_ERROR,
                                        "publish error, ceased publishing...");
                        }
                }
        }

       /* informing "parsing done". */
        hist_gfc->hist_done = (publish == _gf_true) ? 0 : -1;

out:
        if (fd != -1)
                close (fd);
        GF_FREE (hist_data);
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

#define MAKE_HTIME_FILE_PATH(htime_file, htime_dir, htime_bname) do {   \
                strcpy (htime_file, htime_dir);                         \
                strcat (htime_file, "/");                               \
                strcat (htime_file, htime_bname);                       \
        } while (0)

/**
 * Extract timestamp range from a historical metadata file
 * Returns:
 *    0 : Success ({min,max}_ts with the appropriate values)
 *   -1 : Failure
 *   -2 : Ignore this metadata file and process next
 */
int
gf_changelog_extract_min_max (const char *dname, const char *htime_dir,
                              int *fd, unsigned long *total,
                              unsigned long *min_ts, unsigned long *max_ts)
{
        int          ret          = -1;
        xlator_t    *this         = NULL;
        char htime_file[PATH_MAX] = {0,};
        struct stat  stbuf        = {0,};
        char        *iter         = NULL;
        char x_value[30]          = {0,};

        this = THIS;

        MAKE_HTIME_FILE_PATH (htime_file, htime_dir, dname);

        iter = (htime_file + strlen (htime_file) - TIMESTAMP_LENGTH);
        sscanf (iter ,"%lu",min_ts);

        ret = stat (htime_file, &stbuf);
        if (ret) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "stat() failed on htime file %s (reason %s)",
                        htime_file, strerror (errno));
                goto out;
        }

        /* ignore everything except regular files */
        if (!S_ISREG (stbuf.st_mode)) {
                ret = -2;
                goto out;
        }

        *fd = open (htime_file, O_RDONLY);
        if (*fd < 0) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "open() failed for htime %s (reasong %s)",
                        htime_file, strerror (errno));
                goto out;
        }

        /* Looks good, extract max timestamp */
        ret = sys_fgetxattr (*fd, HTIME_KEY, x_value, sizeof (x_value));
        if (ret < 0) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "error extracting max timstamp from htime file"
                        " %s (reason %s)", htime_file, strerror (errno));
                goto out;
        }

        sscanf (x_value, "%lu:%lu", max_ts, total);
        gf_log (this->name, GF_LOG_INFO,
                "MIN: %lu, MAX: %lu, TOTAL CHANGELOGS: %lu",
                *min_ts, *max_ts, *total);

        ret = 0;

 out:
        return ret;
}

int
gf_history_changelog (char* changelog_dir, unsigned long start,
                      unsigned long end, int n_parallel,
                      unsigned long *actual_end)
{
        int                             ret                     = 0;
        int                             len                     = -1;
        int                             fd                      = -1;
        int                             n_read                  = -1;
        unsigned long                   min_ts                  = 0;
        unsigned long                   max_ts                  = 0;
        unsigned long                   end2                    = 0;
        unsigned long                   ts1                     = 0;
        unsigned long                   ts2                     = 0;
        unsigned long                   to                      = 0;
        unsigned long                   from                    = 0;
        unsigned long                   total_changelog         = 0;
        xlator_t                        *this                   = NULL;
        gf_changelog_t                  *gfc                    = NULL;
        gf_changelog_t                  *hist_gfc               = NULL;
        gf_changelog_history_data_t     *hist_data              = NULL;
        DIR                             *dirp                   = NULL;
        struct dirent                   *dp                     = NULL;
        pthread_t                       consume_th              = 0;
        char                            htime_dir[PATH_MAX]     = {0,};
        char                            buffer[PATH_MAX]        = {0,};

        pthread_attr_t attr;

        ret = pthread_attr_init (&attr);
        if (ret != 0) {
                return -1;
        }

        this = THIS;
        if (!this) {
                ret = -1;
                goto out;
        }

        gfc = (gf_changelog_t *) this->private;
        if (!gfc) {
                ret = -1;
                goto out;
        }

        hist_gfc = (gf_changelog_t *) gfc->hist_gfc;
        if (!hist_gfc) {
                ret = -1;
                goto out;
        }

        /* basic sanity check */
        if (start > end || n_parallel <= 0) {
                ret = -1;
                goto out;
        }

        /* cap parallelism count */
        if (n_parallel > MAX_PARALLELS)
                n_parallel = MAX_PARALLELS;

        CHANGELOG_FILL_HTIME_DIR (changelog_dir, htime_dir);

        dirp = opendir (htime_dir);
        if (dirp == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "open dir on htime failed : %s (reason: %s)",
                        htime_dir, strerror (errno));
                ret = -1;
                goto out;
        }

        while ((dp = readdir (dirp)) != NULL) {
                ret = gf_changelog_extract_min_max (dp->d_name, htime_dir,
                                                    &fd, &total_changelog,
                                                    &min_ts, &max_ts);
                if (ret) {
                        if (-2 == ret)
                                continue;
                        goto out;
                }

                if (start >= min_ts && start < max_ts) {
                        /**
                         * TODO: handle short reads later...
                         */
                        n_read = read (fd, buffer, PATH_MAX);
                        if (n_read < 0) {
                                ret = -1;
                                gf_log ( this->name, GF_LOG_ERROR,
                                        "unable to read htime file");
                                goto out;
                        }

                        len = strlen (buffer);

                        /**
                         * search @start in the htime file returning it's index
                         * (@from)
                         */
                        from = gf_history_b_search (fd, start, 0,
                                                   total_changelog - 1, len);

                        /* ensuring correctness of gf_b_search */
                        if (gf_history_check (fd, from, start, len) != 0) {
                                ret = -1;
                                gf_log (this->name, GF_LOG_ERROR,
                                        "wrong result for start: %lu idx: %lu",
                                        start, from);
                                goto out;
                        }

                        end2 = (end <= max_ts) ? end : max_ts;

                        /**
                         * search @end2 in htime file returning it's index (@to)
                         */
                        to = gf_history_b_search (fd, end2,
                                                  0, total_changelog - 1, len);

                        if (gf_history_check (fd, to, end2, len) != 0) {
                                ret = -1;
                                gf_log (this->name, GF_LOG_ERROR,
                                        "wrong result for start: %lu idx: %lu",
                                        end2, to);
                                goto out;
                        }

                        ret = gf_history_get_timestamp (fd, from, len, &ts1);
                        if (ret == -1)
                                goto out;

                        ret = gf_history_get_timestamp (fd, to, len, &ts2);
                        if (ret == -1)
                                goto out;

                        gf_log (this->name, GF_LOG_INFO,
                                "FINAL: from: %lu, to: %lu, changes: %lu",
                                ts1, ts2, (to - from + 1));

                        hist_data =  GF_CALLOC (1,
                                     sizeof (gf_changelog_history_data_t),
                                     gf_changelog_mt_history_data_t);

                        hist_data->htime_fd   = fd;
                        hist_data->from       = from;
                        hist_data->to         = to;
                        hist_data->len        = len;
                        hist_data->n_parallel = n_parallel;

                        ret = pthread_attr_setdetachstate
                                (&attr, PTHREAD_CREATE_DETACHED);
                        if (ret != 0) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "unable to sets the detach"
                                        " state attribute, reason(%s)",
                                        strerror (ret));
                                ret = -1;
                                goto out;
                        }

                        /* spawn a thread for background parsing & publishing */
                        ret = pthread_create (&consume_th, &attr,
                                              gf_history_consume, hist_data);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "creation of consume parent-thread"
                                        " failed. reason(%s)", strerror (ret));
                                ret = -1;
                                goto out;
                        }

                        goto out;

                } /* end of range check */

        } /* end of readdir() */

        if (!from || !to)
                ret = -1;

out:
        if (dirp != NULL)
                closedir (dirp);

        if (ret < 0) {
                if (fd != -1)
                        close (fd);
                GF_FREE (hist_data);
                (void) pthread_attr_destroy (&attr);

                return ret;
        }

        hist_gfc->hist_done = 1;
        *actual_end = ts2;

        return ret;
}
