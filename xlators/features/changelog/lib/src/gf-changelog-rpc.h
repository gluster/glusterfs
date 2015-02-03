/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __GF_CHANGELOG_RPC_H
#define __GF_CHANGELOG_RPC_H

#include "xlator.h"

#include "gf-changelog-helpers.h"
#include "changelog-rpc-common.h"

struct rpc_clnt *gf_changelog_rpc_init (xlator_t *, gf_changelog_t *);

int gf_changelog_invoke_rpc (xlator_t *, gf_changelog_t *, int);

rpcsvc_t *
gf_changelog_reborp_init_rpc_listner (xlator_t *, char *, char *, void *);

#endif
