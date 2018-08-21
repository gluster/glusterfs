/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

/**
 * get set of new changes every 10 seconds (just print the file names)
 *
 * Compile it using:
 *  gcc -o gethistory `pkg-config --cflags libgfchangelog` get-history.c \
 *  `pkg-config --libs libgfchangelog`
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/un.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "changelog.h"

int
main (int argc, char **argv)
{
        int     ret          = 0;
        unsigned long end_ts = 0;
        int start  = 0;
        int end    = 0;

        ret = gf_changelog_init (NULL);
        if (ret) {
                printf ("-1");
                fflush(stdout);
                return -1;
        }

        ret = gf_changelog_register ("/d/backends/patchy0",
                                     "/tmp/scratch_v1",
                                     "/var/log/glusterfs/changes.log",
                                     9, 5);
        if (ret) {
                printf ("-2");
                fflush(stdout);
                return -1;
        }

        start = atoi(argv[1]);
        end = atoi(argv[2]);

        ret = gf_history_changelog ("/d/backends/patchy0/.glusterfs/changelogs",
                                    start, end, 3, &end_ts);
        if (ret < 0) {
                printf ("-3");
                fflush(stdout);
                return -1;
        } else if (ret == 1) {
                printf ("1");
                fflush(stdout);
                return 0;
        }

out:
        printf ("0");
        fflush(stdout);
        return 0;
}
