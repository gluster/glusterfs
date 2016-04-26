/*
 * Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
 * This file is part of GlusterFS.
 *
 * This file is licensed to you under your choice of the GNU Lesser
 * General Public License, version 3 or any later version (LGPLv3 or
 * later), or the GNU General Public License, version 2 (GPLv2), in all
 * cases as published by the Free Software Foundation.
 */

#ifdef RPC_XDR
%#include "rpc-pragmas.h"
#endif
%#include "compat.h"

/*
 * This defines the maximum length of the string
 * identifying the caller.
 */
const SM_MAXSTRLEN = 1024;

struct sm_name {
    string mon_name<SM_MAXSTRLEN>;
};

enum res {
    STAT_SUCC = 0,   /*  NSM agrees to monitor.  */
    STAT_FAIL = 1    /*  NSM cannot monitor.  */
};

struct sm_stat_res {
    res    res_stat;
    int    state;
};

struct sm_stat {
    int    state;    /*  state number of NSM  */
};

struct my_id {
    string my_name<SM_MAXSTRLEN>;  /*  hostname  */
    int    my_prog;                /*  RPC program number  */
    int    my_vers;                /*  program version number  */
    int    my_proc;                /*  procedure number  */
};

struct mon_id {
    string mon_name<SM_MAXSTRLEN>; /* name of the host to be monitored */
    struct my_id my_id;
};

struct mon {
    struct mon_id mon_id;
    opaque    priv[16];        /*  private information  */
};

struct nsm_callback_status {
    string mon_name<SM_MAXSTRLEN>;
    int    state;
    opaque priv[16];        /*  for private information  */
};

