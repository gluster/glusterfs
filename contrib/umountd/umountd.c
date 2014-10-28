/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>

#include "glusterfs.h"
#include "globals.h"
#include "logging.h"
#include "syscall.h"
#include "mem-types.h"

static void
usage (void)
{
        fprintf (stderr, "Usage: umountd [-d dev] [-t timeout] [-r] path\n");
        exit (EXIT_FAILURE);
}


static int
sanity_check (char *path, dev_t *devp)
{
        struct stat st;
        struct stat parent_st;
        int ret;
        char pathtmp[PATH_MAX];
        char *parent;

        if (path == NULL)
                usage ();

        if ((ret = stat (path, &st)) != 0) {
                switch (errno) {
                case ENOTCONN:
                        /* volume is stopped */
                        break;
                default:
                        gf_log ("umountd", GF_LOG_ERROR,
                                "Cannot access %s\n", path, strerror (errno));
                        goto out;
                }
        }

        /* If dev was not specified, get it from path */
        if (*devp == -1 && ret == 0)
                *devp = st.st_dev;

        strncpy (pathtmp, path, PATH_MAX);
        parent = dirname (pathtmp);

        if (stat (parent, &parent_st) != 0) {
                gf_log ("umountd", GF_LOG_ERROR,
                        "Cannot access %s\n", parent, strerror (errno));
                goto out;
        }

        if (st.st_dev == parent_st.st_dev) {
                gf_log ("umountd", GF_LOG_ERROR,
                        "No filesystem mounted on %s\n", path);
                goto out;
        }

        ret = 0;

out:
        return ret;
}

static void
log_rotate (int signum)
{
        gf_log_logrotate (1);

        if (signal (SIGHUP, *log_rotate) == SIG_ERR) {
                gf_log ("umountd", GF_LOG_ERROR, "signal () failed");
                exit (EXIT_FAILURE);
        }

        return;
}

static int
logging_init (void)
{
        glusterfs_ctx_t *ctx;
        char log_file[PATH_MAX];
        int ret = -1;

        ctx = glusterfs_ctx_new ();
        if (!ctx) {
                fprintf (stderr, "glusterfs_ctx_new failed\n");
                goto out;
        }

        ret = glusterfs_globals_init (ctx);
        if (ret) {
                fprintf (stderr, "glusterfs_globals_init failed\n");
                goto out;
        }

        THIS->ctx = ctx;
        xlator_mem_acct_init (THIS, gf_common_mt_end);

        snprintf (log_file, PATH_MAX,
                  "%s/umountd.log", DEFAULT_LOG_FILE_DIRECTORY);

        ret = gf_log_init (ctx, log_file, "umountd");
        if (ret) {
                fprintf (stderr, "gf_log_init failed\n");
                goto out;
        }

        if (signal (SIGHUP, *log_rotate) == SIG_ERR) {
                gf_log ("umountd", GF_LOG_ERROR, "signal () failed");
                goto out;
        }

        ret = 0;
out:
        return ret;
}

static int
umountd_async (char *path, dev_t dev, int frmdir, int timeout)
{
        int                   ret        = -1;
        struct stat           stbuf      = {0, };
        int                       unmount_ret = 0;

        do {
                unmount_ret = unmount (path, 0);
                if (unmount_ret == 0)
                        gf_log ("umountd", GF_LOG_INFO, "Unmounted %s", path);

                if (unmount_ret != 0 && errno != EBUSY) {
                               gf_log ("umountd", GF_LOG_WARNING,
                                      "umount %s failed: %s",
                                path, strerror (errno));
                }

                ret = sys_lstat (path, &stbuf);
                if (ret != 0) {
                        gf_log ("umountd", GF_LOG_WARNING,
                                      "Cannot stat device from %s",
                                path, strerror (errno));
                        break;
                }

                if (stbuf.st_dev != dev) {
                        if (unmount_ret != 0)
                                gf_log ("umountd", GF_LOG_INFO,
                                        "device mismatch "
                                        "(expect %lld, found %lld), "
                                        "someone else unmounted %s",
                                        dev, stbuf.st_dev, path);
                        ret = 0;
                        break;
                }

                sleep (timeout);
        } while (1/*CONSTCOND*/);

        if (ret) {
                gf_log ("umountd", GF_LOG_ERROR,
                        "Asynchronous unmount of %s failed: %s",
                        path, strerror (errno));
        } else {
                if (frmdir) {
                        ret = rmdir (path);
                        if (ret)
                                gf_log ("umountd", GF_LOG_WARNING,
                                         "rmdir %s failed: %s",
                                         path, strerror (errno));
                        else
                                gf_log ("umountd", GF_LOG_INFO,
                                         "Removed %s", path);
                }
        }

        return ret;
}

int
main (int argc, char **argv)
{
        char *path = NULL;
        dev_t dev = -1;
        int frmdir = 0;
        int timeout = 30;
        int f;

        while ((f = getopt (argc, argv, "d:rt:")) != -1) {
                switch (f) {
                case 'p':
                        path = optarg;
                        break;
                case 'd':
                        dev = strtoll (optarg, NULL, 10);
                        break;
                case 't':
                        timeout = atoi (optarg);
                        break;
                case 'r':
                        frmdir = 1;
                        break;
                default:
                        usage ();
                        break;
                }
        }

        argc -= optind;
        argv += optind;

        if (argc != 1)
                usage ();

        path = argv[0];

        if (logging_init () != 0)
                exit (EXIT_FAILURE);

        if (sanity_check (path, &dev) != 0)
                exit (EXIT_FAILURE);

        if (daemon (0, 0) != 0)
                exit (EXIT_FAILURE);

        if (umountd_async (path, dev, frmdir, timeout) != 0)
                exit (EXIT_FAILURE);

        return EXIT_SUCCESS;
}
