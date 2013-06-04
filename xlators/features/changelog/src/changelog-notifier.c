/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "changelog-notifier.h"

#include <pthread.h>

inline static void
changelog_notify_clear_fd (changelog_notify_t *cn, int i)
{
        cn->client_fd[i] = -1;
}

inline static void
changelog_notify_save_fd (changelog_notify_t *cn, int i, int fd)
{
        cn->client_fd[i] = fd;
}

static int
changelog_notify_insert_fd (xlator_t *this, changelog_notify_t *cn, int fd)
{
        int i   = 0;
        int ret = 0;

        for (; i < CHANGELOG_MAX_CLIENTS; i++) {
                if (cn->client_fd[i] == -1)
                        break;
        }

        if (i == CHANGELOG_MAX_CLIENTS) {
                /**
                 * this case should not be hit as listen() would limit
                 * the number of completely established connections.
                 */
                gf_log (this->name, GF_LOG_WARNING,
                        "hit max client limit (%d)", CHANGELOG_MAX_CLIENTS);
                ret = -1;
        }
        else
                changelog_notify_save_fd (cn, i, fd);

        return ret;
}

static void
changelog_notify_fill_rset (changelog_notify_t *cn, fd_set *rset, int *maxfd)
{
        int i = 0;

        FD_ZERO (rset);

        FD_SET (cn->socket_fd, rset);
        *maxfd = cn->socket_fd;

        FD_SET (cn->rfd, rset);
        *maxfd = max (*maxfd, cn->rfd);

        for (; i < CHANGELOG_MAX_CLIENTS; i++) {
                if (cn->client_fd[i] != -1) {
                        FD_SET (cn->client_fd[i], rset);
                        *maxfd = max (*maxfd, cn->client_fd[i]);
                }
        }

        *maxfd = *maxfd + 1;
}

static int
changelog_notify_client (changelog_notify_t *cn, char *path, ssize_t len)
{
        int i = 0;
        int ret = 0;

        for (; i < CHANGELOG_MAX_CLIENTS; i++) {
                if (cn->client_fd[i] == -1)
                        continue;

                if (changelog_write (cn->client_fd[i],
                                     path, len)) {
                        ret = -1;

                        close (cn->client_fd[i]);
                        changelog_notify_clear_fd (cn, i);
                }
        }

        return ret;
}

static void
changelog_notifier_init (changelog_notify_t *cn)
{
        int i = 0;

        cn->socket_fd = -1;

        for (; i < CHANGELOG_MAX_CLIENTS; i++) {
                changelog_notify_clear_fd (cn, i);
        }
}

static void
changelog_close_client_conn (changelog_notify_t *cn)
{
        int i = 0;

        for (; i < CHANGELOG_MAX_CLIENTS; i++) {
                if (cn->client_fd[i] == -1)
                        continue;

                close (cn->client_fd[i]);
                changelog_notify_clear_fd (cn, i);
        }
}

static void
changelog_notifier_cleanup (void *arg)
{
        changelog_notify_t *cn = NULL;

        cn = (changelog_notify_t *) arg;

        changelog_close_client_conn (cn);

        if (cn->socket_fd != -1)
                close (cn->socket_fd);

        if (cn->rfd)
                close (cn->rfd);

        if (unlink (cn->sockpath))
                gf_log ("", GF_LOG_WARNING,
                        "could not unlink changelog socket file"
                        " %s (reason: %s", cn->sockpath, strerror (errno));
}

void *
changelog_notifier (void *data)
{
        int                 i         = 0;
        int                 fd        = 0;
        int                 max_fd    = 0;
        int                 len       = 0;
        ssize_t             readlen   = 0;
        xlator_t           *this      = NULL;
        changelog_priv_t   *priv      = NULL;
        changelog_notify_t *cn        = NULL;
        struct sockaddr_un  local     = {0,};
        char path[PATH_MAX]           = {0,};
        char abspath[PATH_MAX]        = {0,};

        char buffer;
        fd_set rset;

        priv = (changelog_priv_t *) data;

        cn = &priv->cn;
        this = cn->this;

        pthread_cleanup_push (changelog_notifier_cleanup, cn);

        changelog_notifier_init (cn);

        cn->socket_fd = socket (AF_UNIX, SOCK_STREAM, 0);
        if (cn->socket_fd < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "changelog socket error (reason: %s)",
                        strerror (errno));
                goto out;
        }

        CHANGELOG_MAKE_SOCKET_PATH (priv->changelog_brick,
                                    cn->sockpath, PATH_MAX);
        if (unlink (cn->sockpath) < 0) {
                if (errno != ENOENT) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Could not unlink changelog socket file (%s)"
                                " (reason: %s)",
                                CHANGELOG_UNIX_SOCK, strerror (errno));
                        goto cleanup;
                }
        }

        local.sun_family = AF_UNIX;
        strcpy (local.sun_path, cn->sockpath);

        len = strlen (local.sun_path) + sizeof (local.sun_family);

        /* bind to the unix domain socket */
        if (bind (cn->socket_fd, (struct sockaddr *) &local, len) < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not bind to changelog socket (reason: %s)",
                        strerror (errno));
                goto cleanup;
        }

        /* listen for incoming connections */
        if (listen (cn->socket_fd, CHANGELOG_MAX_CLIENTS) < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "listen() error on changelog socket (reason: %s)",
                        strerror (errno));
                goto cleanup;
        }

        /**
         * simple select() on all to-be-read file descriptors. This method
         * though old school works pretty well when you have a handfull of
         * fd's to be watched (clients).
         *
         * Future TODO: move this to epoll based notification facility if
         *              number of clients increase.
         */
        for (;;) {
                changelog_notify_fill_rset (cn, &rset, &max_fd);

                if (select (max_fd, &rset, NULL, NULL, NULL) < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "select() returned -1 (reason: %s)",
                                strerror (errno));
                        sleep (2);
                        continue;
                }

                if (FD_ISSET (cn->socket_fd, &rset)) {
                        fd = accept (cn->socket_fd, NULL, NULL);
                        if (fd < 0) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "accept error on changelog socket"
                                        " (reason: %s)", strerror (errno));
                        } else if (changelog_notify_insert_fd (this, cn, fd)) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "hit max client limit");
                        }
                }

                if (FD_ISSET (cn->rfd, &rset)) {
                        /**
                         * read changelog filename and notify all connected
                         * clients.
                         */
                        readlen = 0;
                        while (readlen < PATH_MAX) {
                                len = read (cn->rfd, &path[readlen++], 1);
                                if (len == -1) {
                                        break;
                                }

                                if (len == 0) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "rollover thread sent EOF"
                                                " on pipe - possibly a crash.");
                                        /* be blunt and close all connections */
                                        pthread_exit(NULL);
                                }

                                if (path[readlen - 1] == '\0')
                                        break;
                        }

                        /* should we close all client connections here too? */
                        if (len < 0 || readlen == PATH_MAX) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Could not get pathname from rollover"
                                        " thread or pathname too long");
                                goto process_rest;
                        }

                        (void) snprintf (abspath, PATH_MAX,
                                         "%s/%s", priv->changelog_dir, path);
                        if (changelog_notify_client (cn, abspath,
                                                     strlen (abspath) + 1))
                                gf_log (this->name, GF_LOG_ERROR,
                                        "could not notify some clients with new"
                                        " changelogs");
                }

        process_rest:
                for (i = 0; i < CHANGELOG_MAX_CLIENTS; i++) {
                        if ( (fd = cn->client_fd[i]) == -1 )
                                continue;

                        if (FD_ISSET (fd, &rset)) {
                                /**
                                 * the only data we accept from the client is a
                                 * disconnect. Anything else is treated as bogus
                                 * and is silently discarded (also warned!!!).
                                 */
                                if ( (readlen = read (fd, &buffer, 1)) <= 0 ) {
                                        close (fd);
                                        changelog_notify_clear_fd (cn, i);
                                } else {
                                        /* silently discard data and log */
                                        gf_log (this->name, GF_LOG_WARNING,
                                                "misbehaving changelog client");
                                }
                        }
                }

        }

 cleanup:;
        pthread_cleanup_pop (1);

 out:
        return NULL;
}
