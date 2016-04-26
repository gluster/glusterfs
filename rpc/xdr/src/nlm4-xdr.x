/*
  Copyright (c) 2007-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifdef RPC_XDR
%#include "rpc-pragmas.h"
#endif
%#include "compat.h"

/* .x file defined as according to the RFC */

%#include "xdr-common.h"

const MAXNETOBJ_SZ = 1024;
const LM_MAXSTRLEN = 1024;
const MAXNAMELEN = 1025;

typedef opaque nlm4_netobj<MAXNETOBJ_SZ>;

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
	nlm4_netobj oh;
	u_int64_t l_offset;
	u_int64_t l_len;
};

struct nlm4_lock {
	string caller_name<LM_MAXSTRLEN>;
	nlm4_netobj fh;
	nlm4_netobj oh;
	u_int32_t svid;
	u_int64_t l_offset;
	u_int64_t l_len;
};

struct nlm4_share {
	string caller_name<LM_MAXSTRLEN>;
	nlm4_netobj fh;
	nlm4_netobj oh;
	fsh_mode mode;
	fsh_access access;
};

union nlm4_testrply switch (nlm4_stats stat) {
	case nlm4_denied:
		struct nlm4_holder holder;
	default:
		void;
};

struct nlm4_testres {
	nlm4_netobj cookie;
	nlm4_testrply stat;
};

struct nlm4_testargs {
	nlm4_netobj cookie;
	bool exclusive;
	struct nlm4_lock alock;
};

struct nlm4_res {
	nlm4_netobj cookie;
	nlm4_stat stat;
};

struct nlm4_lockargs {
	nlm4_netobj cookie;
	bool block;
	bool exclusive;
	struct nlm4_lock alock;
	bool reclaim;		/* used for recovering locks */
	int32_t state;		/* specify local status monitor state */
};

struct nlm4_cancargs {
	nlm4_netobj cookie;
	bool block;
	bool exclusive;
	struct nlm4_lock alock;
};

struct nlm4_unlockargs {
	nlm4_netobj cookie;
	struct nlm4_lock alock;
};

struct	nlm4_shareargs {
	nlm4_netobj	cookie;
	nlm4_share	share;
	bool	reclaim;
};

struct	nlm4_shareres {
	nlm4_netobj	cookie;
	nlm4_stats	stat;
	int32_t	sequence;
};

struct  nlm4_freeallargs {
        string       name<LM_MAXSTRLEN>;   /* client hostname */
        uint32_t     state;                /* unused */
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

program NLMCBK_PROGRAM {
        version NLMCBK_V1 {
                void NLMCBK_SM_NOTIFY(struct nlm_sm_status) = 16;
        } = 1;
} = 100021;
