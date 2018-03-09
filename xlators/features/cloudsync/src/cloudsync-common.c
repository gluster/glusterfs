/*
  Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "cloudsync-common.h"

void
cs_local_wipe (xlator_t *this, cs_local_t *local)
{
        if (!local)
                return;

        loc_wipe (&local->loc);

        if (local->fd) {
                fd_unref (local->fd);
                local->fd = NULL;
        }

        if (local->stub) {
                call_stub_destroy (local->stub);
                local->stub = NULL;
        }

        if (local->xattr_req)
                dict_unref (local->xattr_req);

        if (local->xattr_rsp)
                dict_unref (local->xattr_rsp);

        if (local->dlfd)
                fd_unref (local->dlfd);

        if (local->remotepath)
                GF_FREE (local->remotepath);

        mem_put (local);
}
