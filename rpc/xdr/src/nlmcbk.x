/*
  Copyright (c) 2007-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

const LM_MAXSTRLEN = 1024;

struct nlm_sm_status {
        string mon_name<LM_MAXSTRLEN>; /* name of host */
        int state;                      /* new state */
        opaque priv[16];                /* private data */
};

program NLMCBK_PROGRAM {
	version NLMCBK_V0 {
		void NLMCBK_SM_NOTIFY(struct nlm_sm_status) = 1;
	} = 0;
} = 1238477;

