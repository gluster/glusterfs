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

#include "gf-changelog-helpers.h"

/* from the changelog translator */
#include "changelog-misc.h"
#include "changelog-mem-types.h"

/*@API
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

/*
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
        ssize_t                 size                    = 0;
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
        if (size < 0)
                goto out;
        if (size == 0)
                return 0;

        memcpy (bufptr, buffer, size - 1);
        *(buffer + size) = '\0';

        return size;

 out:
        return -1;
}

/*
 * @API
 *  gf_history_changelog_scan:
 *     Scan and generate a list of change entries.
 *     Calling this api multiple times (without calling gf_changlog_done())
 *     would result new changelogs(s) being refreshed in the tracker file.
 *     This call also acts as a cancellation point for the consumer.
 *
 *  RETURN VALUES:
 *     nr_entries: On success.
 *     -1        : On error.
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
        gf_changelog_t *hist_gfc        = NULL;
        struct dirent  *entryp     = NULL;
        struct dirent  *result     = NULL;
        char buffer[PATH_MAX]      = {0,};

        this = THIS;
        if (!this)
                goto out;

        gfc = (gf_changelog_t *) this->private;
        if (!gfc)
                goto out;

        hist_gfc = gfc->hist_gfc;
        if (!hist_gfc)
                goto out;

        errno = EINVAL;

        tracker_fd = hist_gfc->gfc_fd;

        if (gf_ftruncate (tracker_fd, 0))
                goto out;

        len = offsetof(struct dirent, d_name)
                + pathconf(hist_gfc->gfc_processing_dir, _PC_NAME_MAX) + 1;
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

        if (!result) {
                if (gf_lseek (tracker_fd, 0, SEEK_SET) != -1)
                        return nr_entries;
        }
 out:
        return -1;
}
