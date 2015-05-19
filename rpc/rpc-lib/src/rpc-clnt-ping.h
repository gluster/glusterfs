/*
  Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#define RPC_DEFAULT_PING_TIMEOUT 30
void
rpc_clnt_check_and_start_ping (struct rpc_clnt *rpc_ptr);
int
rpc_clnt_remove_ping_timer_locked (struct rpc_clnt *rpc);
