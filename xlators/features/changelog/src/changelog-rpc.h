/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __CHANGELOG_RPC_H
#define __CHANGELOG_RPC_H

#include "xlator.h"
#include "changelog-helpers.h"

/* one time */
#include "socket.h"
#include "changelog-rpc-common.h"

#define CHANGELOG_RPC_PROGNAME  "GlusterFS Changelog"

rpcsvc_t *
changelog_init_rpc_listener (xlator_t *, changelog_priv_t *, rbuf_t *, int);

void
changelog_destroy_rpc_listner (xlator_t *, changelog_priv_t *);

#endif
