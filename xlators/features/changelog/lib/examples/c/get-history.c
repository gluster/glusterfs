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
#include <errno.h>

#include "changelog.h"

#define handle_error(fn)                                \
        printf ("%s (reason: %s)\n", fn, strerror (errno))

int
main (int argc, char ** argv)
{
        int     i            = 0;
        int     ret          = 0;
        ssize_t nr_changes   = 0;
        ssize_t changes      = 0;
        char fbuf[PATH_MAX]  = {0,};
        unsigned long end_ts = 0;

        ret = gf_changelog_register ("/export/z1/zwoop",
                                     "/tmp/scratch_v1", "/tmp/changes.log",
                                     9, 5);
        if (ret) {
                handle_error ("register failed");
                goto out;
        }

        int a, b;
        printf ("give the two numbers start and end\t");
        scanf ("%d%d", &a, &b);
        ret = gf_history_changelog ("/export/z1/zwoop/.glusterfs/changelogs",
                                    a, b, 3, &end_ts);
        if (ret == -1) {
                printf ("history failed");
                goto out;
        }

        printf ("end time till when changelog available : %d , ret(%d) \t", end_ts, ret);
        fflush(stdout);

        while (1) {
                nr_changes = gf_history_changelog_scan ();
                printf ("scanned, nr_changes : %d\n",nr_changes);
                if (nr_changes < 0) {
                        handle_error ("scan(): ");
                        break;
                }

                if (nr_changes == 0) {
                        printf ("done scanning \n");
                        goto out;
                }

                printf ("Got %ld changelog files\n", nr_changes);

                while ( (changes =
                         gf_history_changelog_next_change (fbuf, PATH_MAX)) > 0) {
                        printf ("changelog file [%d]: %s\n", ++i, fbuf);

                        /* process changelog */
                        /* ... */
                        /* ... */
                        /* ... */
                        /* done processing */

                        ret = gf_history_changelog_done (fbuf);
                        if (ret)
                                handle_error ("gf_changelog_done");
                }
                /*
                if (changes == -1)
                        handle_error ("gf_changelog_next_change");
                if (nr_changes ==1){
                        printf("continue scanning\n");
                }

                if(nr_changes == 0){
                        printf("done scanning \n");
                        goto out;
                }
                */
        }


out:
        return ret;
}
