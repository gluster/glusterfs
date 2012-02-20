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

/* .x file defined as according to the RFC */

const MAXNETOBJ_SZ = 1024;
const LM_MAXSTRLEN = 1024;
const MAXNAMELEN = 1025;

typedef opaque netobj<MAXNETOBJ_SZ>;

#ifdef RPC_HDR
%/*
% * The following enums are actually bit encoded for efficient
% * boolean algebra.... DON'T change them.....
% */
#endif
enum	fsh_mode {
	fsm_DN  = 0,	/* deny none */
	fsm_DR  = 1,	/* deny read */
	fsm_DW  = 2,	/* deny write */
	fsm_DRW = 3	/* deny read/write */
};

enum	fsh_access {
	fsa_NONE = 0,	/* for completeness */
	fsa_R    = 1,	/* read only */
	fsa_W    = 2,	/* write only */
	fsa_RW   = 3	/* read/write */
};

#ifdef RPC_HDR
%/* definitions for NLM version 4 */
#endif
enum nlm4_stats {
	nlm4_granted			= 0,
	nlm4_denied			= 1,
	nlm4_denied_nolock		= 2,
	nlm4_blocked			= 3,
	nlm4_denied_grace_period	= 4,
	nlm4_deadlck			= 5,
	nlm4_rofs			= 6,
	nlm4_stale_fh			= 7,
	nlm4_fbig			= 8,
	nlm4_failed			= 9
};

struct nlm4_stat {
	nlm4_stats stat;
};

struct nlm4_holder {
	bool exclusive;
	u_int32_t svid;
	netobj oh;
	u_int64_t l_offset;
	u_int64_t l_len;
};

struct nlm4_lock {
	string caller_name<MAXNAMELEN>;
	netobj fh;
	netobj oh;
	u_int32_t svid;
	u_int64_t l_offset;
	u_int64_t l_len;
};

struct nlm4_share {
	string caller_name<MAXNAMELEN>;
	netobj fh;
	netobj oh;
	fsh_mode mode;
	fsh_access access;
};

union nlm4_testrply switch (nlm4_stats stat) {
	case nlm_denied:
		struct nlm4_holder holder;
	default:
		void;
};

struct nlm4_testres {
	netobj cookie;
	nlm4_testrply stat;
};

struct nlm4_testargs {
	netobj cookie;
	bool exclusive;
	struct nlm4_lock alock;
};

struct nlm4_res {
	netobj cookie;
	nlm4_stat stat;
};

struct nlm4_lockargs {
	netobj cookie;
	bool block;
	bool exclusive;
	struct nlm4_lock alock;
	bool reclaim;		/* used for recovering locks */
	int state;		/* specify local status monitor state */
};

struct nlm4_cancargs {
	netobj cookie;
	bool block;
	bool exclusive;
	struct nlm4_lock alock;
};

struct nlm4_unlockargs {
	netobj cookie;
	struct nlm4_lock alock;
};

struct	nlm4_shareargs {
	netobj	cookie;
	nlm4_share	share;
	bool	reclaim;
};

struct	nlm4_shareres {
	netobj	cookie;
	nlm4_stats	stat;
	int	sequence;
};

/*
 * argument for the procedure called by rpc.statd when a monitored host
 * status change.
 * XXX assumes LM_MAXSTRLEN == SM_MAXSTRLEN
 */
struct nlm_sm_status {
	string mon_name<LM_MAXSTRLEN>; /* name of host */
	int state;			/* new state */
	opaque priv[16];		/* private data */
};
