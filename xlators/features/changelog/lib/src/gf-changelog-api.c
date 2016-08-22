/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "compat-uuid.h"
#include "globals.h"
#include "glusterfs.h"
#include "syscall.h"

#include "gf-changelog-helpers.h"
#include "gf-changelog-journal.h"
#include "changelog-mem-types.h"
#include "changelog-lib-messages.h"

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
        gf_msg_debug (this->name, 0,
                      "moving %s to processed directory", file);
        ret = sys_rename (buffer, to_path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_LIB_MSG_RENAME_FAILED,
                        "cannot move %s to %s",
                        file, to_path);
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
        int             tracker_fd  = 0;
        size_t          off         = 0;
        xlator_t       *this        = NULL;
        size_t          nr_entries  = 0;
        gf_changelog_journal_t *jnl = NULL;
        struct dirent  *entry       = NULL;
        struct dirent   scratch[2]  = {{0,},};
        char            buffer[PATH_MAX] = {0,};

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

        rewinddir (jnl->jnl_dir);

        for (;;) {
                errno = 0;
                entry = sys_readdir (jnl->jnl_dir, scratch);
                if (!entry || errno != 0)
                        break;

                if (!strcmp (basename (entry->d_name), ".")
                     || !strcmp (basename (entry->d_name), ".."))
                        continue;

                nr_entries++;

                GF_CHANGELOG_FILL_BUFFER (jnl->jnl_processing_dir,
                                          buffer, off,
                                          strlen (jnl->jnl_processing_dir));
                GF_CHANGELOG_FILL_BUFFER (entry->d_name, buffer,
                                          off, strlen (entry->d_name));
                GF_CHANGELOG_FILL_BUFFER ("\n", buffer, off, 1);

                if (gf_changelog_write (tracker_fd, buffer, off) != off) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                CHANGELOG_LIB_MSG_WRITE_FAILED,
                                "error writing changelog filename"
                                " to tracker file");
                        break;
                }
                off = 0;
        }

        if (!entry) {
                if (gf_lseek (tracker_fd, 0, SEEK_SET) != -1)
                        return nr_entries;
        }
 out:
        return -1;
}
