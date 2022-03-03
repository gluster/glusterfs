/*
   Copyright (c) 2010-2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "cli.h"
#include <glusterfs/compat-errno.h>
#include <glusterfs/compat.h>
#include "cli-cmd.h"
#include "cli-mem-types.h"

struct rpc_clnt *
cli_quotad_clnt_init(xlator_t *this, dict_t *options);

int
cli_quotad_notify(struct rpc_clnt *rpc, void *mydata, rpc_clnt_event_t event,
                  void *data);
