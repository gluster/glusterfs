/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <errno.h>
#include <dirent.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

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

int byebye = 0;

static void
gf_changelog_cleanup (gf_changelog_t *gfc)
{
        /* socket */
        if (gfc->gfc_sockfd != -1)
                close (gfc->gfc_sockfd);
        /* tracker fd */
        if (gfc->gfc_fd != -1)
                close (gfc->gfc_fd);
        /* processing dir */
        if (gfc->gfc_dir)
                closedir (gfc->gfc_dir);

        if (gfc->gfc_working_dir)
                free (gfc->gfc_working_dir); /* allocated by realpath */
}

void
__attribute__ ((constructor)) gf_changelog_ctor (void)
{
        glusterfs_ctx_t *ctx = NULL;

        ctx = glusterfs_ctx_new ();
        if (!ctx)
                return;

        if (glusterfs_globals_init (ctx)) {
                free (ctx);
                ctx = NULL;
                return;
        }

        THIS->ctx = ctx;
}

void
__attribute__ ((destructor)) gf_changelog_dtor (void)
{
        xlator_t        *this = NULL;
        glusterfs_ctx_t *ctx  = NULL;
        gf_changelog_t  *gfc  = NULL;

        this = THIS;
        if (!this)
                return;

        ctx = this->ctx;
        gfc = this->private;

        if (gfc) {
                gf_changelog_cleanup (gfc);
                GF_FREE (gfc);
        }

        if (ctx) {
                pthread_mutex_destroy (&ctx->lock);
                free (ctx);
                ctx = NULL;
        }
}


static int
gf_changelog_open_dirs (gf_changelog_t *gfc)
{
        int  ret                    = -1;
        DIR *dir                    = NULL;
        int  tracker_fd             = 0;
        char tracker_path[PATH_MAX] = {0,};

        (void) snprintf (gfc->gfc_current_dir, PATH_MAX,
                         "%s/"GF_CHANGELOG_CURRENT_DIR"/",
                         gfc->gfc_working_dir);
        ret = mkdir_p (gfc->gfc_current_dir, 0600, _gf_false);
        if (ret)
                goto out;

        (void) snprintf (gfc->gfc_processed_dir, PATH_MAX,
                         "%s/"GF_CHANGELOG_PROCESSED_DIR"/",
                         gfc->gfc_working_dir);
        ret = mkdir_p (gfc->gfc_processed_dir, 0600, _gf_false);
        if (ret)
                goto out;

        (void) snprintf (gfc->gfc_processing_dir, PATH_MAX,
                         "%s/"GF_CHANGELOG_PROCESSING_DIR"/",
                         gfc->gfc_working_dir);
        ret = mkdir_p (gfc->gfc_processing_dir, 0600, _gf_false);
        if (ret)
                goto out;

        dir = opendir (gfc->gfc_processing_dir);
        if (!dir) {
                gf_log ("", GF_LOG_ERROR,
                        "opendir() error [reason: %s]", strerror (errno));
                goto out;
        }

        gfc->gfc_dir = dir;

        (void) snprintf (tracker_path, PATH_MAX,
                         "%s/"GF_CHANGELOG_TRACKER, gfc->gfc_working_dir);

        tracker_fd = open (tracker_path, O_CREAT | O_APPEND | O_RDWR,
                           S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (tracker_fd < 0) {
                closedir (gfc->gfc_dir);
                ret = -1;
                goto out;
        }

        gfc->gfc_fd = tracker_fd;
        ret = 0;
 out:
        return ret;
}

int
gf_changelog_notification_init (xlator_t *this, gf_changelog_t *gfc)
{
        int                ret    = 0;
        int                len    = 0;
        int                tries  = 0;
        int                sockfd = 0;
        struct sockaddr_un remote;

        this = gfc->this;

        if (gfc->gfc_sockfd != -1) {
                gf_log (this->name, GF_LOG_INFO,
                        "Reconnecting...");
                close (gfc->gfc_sockfd);
        }

        sockfd = socket (AF_UNIX, SOCK_STREAM, 0);
        if (sockfd < 0) {
                ret = -1;
                goto out;
        }

        CHANGELOG_MAKE_SOCKET_PATH (gfc->gfc_brickpath,
                                    gfc->gfc_sockpath, PATH_MAX);
        gf_log (this->name, GF_LOG_INFO,
                "connecting to changelog socket: %s (brick: %s)",
                gfc->gfc_sockpath, gfc->gfc_brickpath);

        remote.sun_family = AF_UNIX;
        strcpy (remote.sun_path, gfc->gfc_sockpath);

        len = strlen (remote.sun_path) + sizeof (remote.sun_family);

        while (tries < gfc->gfc_connretries) {
                gf_log (this->name, GF_LOG_WARNING,
                        "connection attempt %d/%d...",
                        tries + 1, gfc->gfc_connretries);

                /* initiate a connect */
                if (connect (sockfd, (struct sockaddr *) &remote, len) == 0) {
                        gfc->gfc_sockfd = sockfd;
                        break;
                }

                tries++;
                sleep (2);
        }

        if (tries == gfc->gfc_connretries) {
                gf_log (this->name, GF_LOG_ERROR,
                        "could not connect to changelog socket!"
                        " bailing out...");
                ret = -1;
        } else
                gf_log (this->name, GF_LOG_INFO,
                        "connection successful");

 out:
        return ret;
}

int
gf_changelog_done (char *file)
{
        int             ret    = -1;
        char           *buffer = NULL;
        xlator_t       *this   = NULL;
        gf_changelog_t *gfc    = NULL;
        char to_path[PATH_MAX] = {0,};

        errno = EINVAL;

        this = THIS;
        if (!this)
                goto out;

        gfc = (gf_changelog_t *) this->private;
        if (!gfc)
                goto out;

        if (!file || !strlen (file))
                goto out;

        /* make sure 'file' is inside ->gfc_working_dir */
        buffer = realpath (file, NULL);
        if (!buffer)
                goto out;

        if (strncmp (gfc->gfc_working_dir,
                     buffer, strlen (gfc->gfc_working_dir)))
                goto out;

        (void) snprintf (to_path, PATH_MAX, "%s%s",
                         gfc->gfc_processed_dir, basename (buffer));
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
 *  for a set of changelogs, start from the begining
 */
int
gf_changelog_start_fresh ()
{
        xlator_t *this = NULL;
        gf_changelog_t *gfc = NULL;

        this = THIS;
        if (!this)
                goto out;

        errno = EINVAL;

        gfc = (gf_changelog_t *) this->private;
        if (!gfc)
                goto out;

        if (gf_ftruncate (gfc->gfc_fd, 0))
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
        ssize_t         size       = 0;
        int             tracker_fd = 0;
        xlator_t       *this       = NULL;
        gf_changelog_t *gfc        = NULL;
        char buffer[PATH_MAX]      = {0,};

        errno = EINVAL;

        this = THIS;
        if (!this)
                goto out;

        gfc = (gf_changelog_t *) this->private;
        if (!gfc)
                goto out;

        tracker_fd = gfc->gfc_fd;

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
        gf_changelog_t *gfc        = NULL;
        struct dirent  *entryp     = NULL;
        struct dirent  *result     = NULL;
        char buffer[PATH_MAX]      = {0,};

        this = THIS;
        if (!this)
                goto out;

        gfc = (gf_changelog_t *) this->private;
        if (!gfc)
                goto out;

        /**
         * do we need to protect 'byebye' with locks? worst, the
         * consumer would get notified during next scan().
         */
        if (byebye) {
                errno = ECONNREFUSED;
                goto out;
        }

        errno = EINVAL;

        tracker_fd = gfc->gfc_fd;

        if (gf_ftruncate (tracker_fd, 0))
                goto out;

        len = offsetof(struct dirent, d_name)
                + pathconf(gfc->gfc_processing_dir, _PC_NAME_MAX) + 1;
        entryp = GF_CALLOC (1, len,
                            gf_changelog_mt_libgfchangelog_dirent_t);
        if (!entryp)
                goto out;

        rewinddir (gfc->gfc_dir);
        while (1) {
                ret = readdir_r (gfc->gfc_dir, entryp, &result);
                if (ret || !result)
                        break;

                if ( !strcmp (basename (entryp->d_name), ".")
                     || !strcmp (basename (entryp->d_name), "..") )
                        continue;

                nr_entries++;

                GF_CHANGELOG_FILL_BUFFER (gfc->gfc_processing_dir,
                                          buffer, off,
                                          strlen (gfc->gfc_processing_dir));
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

/**
 * @API
 *  gf_changelog_register() - register a client for updates.
 */
int
gf_changelog_register (char *brick_path, char *scratch_dir,
                       char *log_file, int log_level, int max_reconnects)
{
        int             i    = 0;
        int             ret  = -1;
        int             errn = 0;
        xlator_t       *this = NULL;
        gf_changelog_t *gfc  = NULL;

        this = THIS;
        if (!this->ctx)
                goto out;

        errno = ENOMEM;

        gfc = GF_CALLOC (1, sizeof (*gfc),
                         gf_changelog_mt_libgfchangelog_t);
        if (!gfc)
                goto out;

        gfc->this = this;

        gfc->gfc_dir = NULL;
        gfc->gfc_fd = gfc->gfc_sockfd = -1;

        gfc->gfc_working_dir = realpath (scratch_dir, NULL);
        if (!gfc->gfc_working_dir) {
                errn = errno;
                goto cleanup;
        }

        ret = gf_changelog_open_dirs (gfc);
        if (ret) {
                errn = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "could not create entries in scratch dir");
                goto cleanup;
        }

        /* passing ident as NULL means to use default ident for syslog */
        if (gf_log_init (this->ctx, log_file, NULL))
                goto cleanup;

        gf_log_set_loglevel ((log_level == -1) ? GF_LOG_INFO :
                             log_level);

        gfc->gfc_connretries = (max_reconnects <= 0) ? 1 : max_reconnects;
        (void) strncpy (gfc->gfc_brickpath, brick_path, PATH_MAX);

        ret = gf_changelog_notification_init (this, gfc);
        if (ret) {
                errn = errno;
                goto cleanup;
        }

        ret = gf_thread_create (&gfc->gfc_changelog_processor,
				NULL, gf_changelog_process, gfc);
        if (ret) {
                errn = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "error creating changelog processor thread"
                        " new changes won't be recorded!!!");
                goto cleanup;
        }

        for (; i < 256; i++) {
                gfc->rfc3986[i] =
                        (isalnum(i) || i == '~' ||
                        i == '-' || i == '.' || i == '_') ? i : 0;
        }

        ret = 0;
        this->private = gfc;

        goto out;

 cleanup:
        gf_changelog_cleanup (gfc);
        GF_FREE (gfc);
        this->private = NULL;
        errno = errn;

 out:
        return ret;
}
