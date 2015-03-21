/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

/**
 * Compile it using:
 *  gcc -o getchanges-multi `pkg-config --cflags libgfchangelog` \
 *  get-changes-multi.c `pkg-config --libs libgfchangelog`
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

void *brick_init (void *xl, struct gf_brick_spec *brick)
{
        return brick;
}

void brick_fini (void *xl, char *brick, void *data)
{
        return;
}

void brick_callback (void *xl, char *brick,
                    void *data, changelog_event_t *ev)
{
        printf ("->callback: (brick,type) [%s:%d]\n", brick, ev->ev_type);
}

void fill_brick_spec (struct gf_brick_spec *brick, char *path)
{
        brick->brick_path = strdup (path);
        brick->filter = CHANGELOG_OP_TYPE_BR_RELEASE;

        brick->init         = brick_init;
        brick->fini         = brick_fini;
        brick->callback     = brick_callback;
        brick->connected    = NULL;
        brick->disconnected = NULL;
}

int
main (int argc, char **argv)
{
        int ret = 0;
        void *bricks = NULL;
        struct gf_brick_spec *brick = NULL;

        bricks = calloc (2, sizeof (struct gf_brick_spec));
        if (!bricks)
                goto error_return;

        brick = (struct gf_brick_spec *)bricks;
        fill_brick_spec (brick, "/export/z1/zwoop");

        brick++;
        fill_brick_spec (brick, "/export/z2/zwoop");

        ret = gf_changelog_init (NULL);
        if (ret)
                goto error_return;

        ret = gf_changelog_register_generic ((struct gf_brick_spec *)bricks, 2,
                                             0, "/tmp/multi-changes.log", 9,
                                             NULL);
        if (ret)
                goto error_return;

        /* let callbacks do the job */
        select (0, NULL, NULL, NULL, NULL);

 error_return:
        return -1;
}
