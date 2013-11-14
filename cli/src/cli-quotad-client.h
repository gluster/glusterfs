/*
   Copyright (c) 2010-2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "cli.h"
#include "compat-errno.h"
#include "compat.h"
#include "cli-cmd.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "protocol-common.h"
#include "cli-mem-types.h"


int
cli_quotad_submit_request (void *req, call_frame_t *frame,
                           rpc_clnt_prog_t *prog,
                           int procnum, struct iobref *iobref,
                           xlator_t *this, fop_cbk_fn_t cbkfn,
                           xdrproc_t xdrproc);

struct rpc_clnt *
cli_quotad_clnt_init (xlator_t *this, dict_t *options);

int
cli_quotad_notify (struct rpc_clnt *rpc, void *mydata,
                   rpc_clnt_event_t event, void *data);

