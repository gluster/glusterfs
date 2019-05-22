/*
   Copyright (c) 2019 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

/**
 * get set of new changes every 5 seconds (just print the file names)
 *
 * Compile it using:
 *  gcc -o getchanges `pkg-config --cflags libgfchangelog` get-changes.c \
 *  `pkg-config --libs libgfchangelog`
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/un.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>

#include "changelog.h"

int
main(int argc, char **argv)
{
    int i = 0;
    int ret = 0;
    ssize_t nr_changes = 0;
    ssize_t changes = 0;
    char fbuf[PATH_MAX] = {
        0,
    };

    ret = gf_changelog_init(NULL);
    if (ret) {
        printf("-1");
        fflush(stdout);
        return -1;
    }

    /* get changes for brick "/d/backends/patchy0" */
    ret = gf_changelog_register("/d/backends/patchy0", "/tmp/scratch_v1",
                                "/var/log/glusterfs/changes.log", 9, 5);
    if (ret) {
        printf("-2");
        fflush(stdout);
        return -1;
    }

    while (1) {
        i = 0;
        nr_changes = gf_changelog_scan();
        if (nr_changes < 0) {
            printf("-4");
            fflush(stdout);
            return -1;
        }

        if (nr_changes == 0)
            goto next;

        while ((changes = gf_changelog_next_change(fbuf, PATH_MAX)) > 0) {
            /* process changelog */
            /* ... */
            /* ... */
            /* ... */
            /* done processing */

            ret = gf_changelog_done(fbuf);
            if (ret) {
                printf("-5");
                fflush(stdout);
                return -1;
            }
        }

        if (changes == -1) {
            printf("-6");
            fflush(stdout);
            return -1;
        }

    next:
        sleep(2);
    }

out:
    printf("0");
    fflush(stdout);
    return ret;
}
