/*
  Copyright (c) 2012 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
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

