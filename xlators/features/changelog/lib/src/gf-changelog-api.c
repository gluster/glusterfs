/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "uuid.h"
#include "globals.h"
#include "glusterfs.h"

#include "gf-changelog-helpers.h"
#include "gf-changelog-journal.h"
#include "changelog-mem-types.h"

int
gf_changelog_done (char *file)
{
        int                     ret    = -1;
        char                   *buffer = NULL;
        xlator_t               *this   = NULL;
        gf_changelog_journal_t *jnl    = NULL;
        char to_path[PATH_MAX]         = {0,};

        errno = EINVAL;

        this = THIS;
        if (!this)
                goto out;

        jnl = (gf_changelog_journal_t *) GF_CHANGELOG_GET_API_PTR (this);
        if (!jnl)
                goto out;

        if (!file || !strlen (file))
                goto out;

        /* make sure 'file' is inside ->jnl_working_dir */
        buffer = realpath (file, NULL);
        if (!buffer)
                goto out;

        if (strncmp (jnl->jnl_working_dir,
                     buffer, strlen (jnl->jnl_working_dir)))
                goto out;

        (void) snprintf (to_path, PATH_MAX, "%s%s",
                         jnl->jnl_processed_dir, basename (buffer));
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
 *  for a set of changelogs, start from the beginning
 */
int
gf_changelog_start_fresh ()
{
        xlator_t *this = NULL;
        gf_changelog_journal_t *jnl = NULL;

        this = THIS;
        if (!this)
                goto out;

        errno = EINVAL;

        jnl = (gf_changelog_journal_t *) GF_CHANGELOG_GET_API_PTR (this);
        if (!jnl)
                goto out;

        if (gf_ftruncate (jnl->jnl_fd, 0))
                goto out;

        return 0;

 out:
        return -1;
}

/**
 * @API
 * return the next changelog file entry. zero means all chanelogs
 * consumed.
 */
ssize_t
gf_changelog_next_change (char *bufptr, size_t maxlen)
{
        ssize_t         size       = -1;
        int             tracker_fd = 0;
        xlator_t       *this       = NULL;
        gf_changelog_journal_t *jnl = NULL;
        char buffer[PATH_MAX]      = {0,};

        errno = EINVAL;

        this = THIS;
        if (!this)
                goto out;

        jnl = (gf_changelog_journal_t *) GF_CHANGELOG_GET_API_PTR (this);
        if (!jnl)
                goto out;

        tracker_fd = jnl->jnl_fd;

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
 *  gf_changelog_scan() - scan and generate a list of change entries
 *
 * calling this api multiple times (without calling gf_changlog_done())
 * would result new changelogs(s) being refreshed in the tracker file.
 * This call also acts as a cancellation point for the consumer.
 */
ssize_t
gf_changelog_scan ()
{
        int             ret        = 0;
        int             tracker_fd = 0;
        size_t          len        = 0;
        size_t          off        = 0;
        xlator_t       *this       = NULL;
        size_t          nr_entries = 0;
        gf_changelog_journal_t *jnl = NULL;
        struct dirent  *entryp     = NULL;
        struct dirent  *result     = NULL;
        char buffer[PATH_MAX]      = {0,};

        this = THIS;
        if (!this)
                goto out;

        jnl = (gf_changelog_journal_t *) GF_CHANGELOG_GET_API_PTR (this);
        if (!jnl)
                goto out;
        if (JNL_IS_API_DISCONNECTED (jnl)) {
                errno = ENOTCONN;
                goto out;
        }

        errno = EINVAL;

        tracker_fd = jnl->jnl_fd;
        if (gf_ftruncate (tracker_fd, 0))
                goto out;

        len = offsetof(struct dirent, d_name)
                + pathconf(jnl->jnl_processing_dir, _PC_NAME_MAX) + 1;
        entryp = GF_CALLOC (1, len,
                            gf_changelog_mt_libgfchangelog_dirent_t);
        if (!entryp)
                goto out;

        rewinddir (jnl->jnl_dir);
        while (1) {
                ret = readdir_r (jnl->jnl_dir, entryp, &result);
                if (ret || !result)
                        break;

                if ( !strcmp (basename (entryp->d_name), ".")
                     || !strcmp (basename (entryp->d_name), "..") )
                        continue;

                nr_entries++;

                GF_CHANGELOG_FILL_BUFFER (jnl->jnl_processing_dir,
                                          buffer, off,
                                          strlen (jnl->jnl_processing_dir));
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
