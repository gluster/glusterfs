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
%#include "xdr-nfs3.h"

struct aclentry {
	int type;
	int uid;
	int perm;
};

struct getaclargs {
	netobj fh;
	int	mask;
};

struct getaclreply {
	int status;
	int attr_follows;
	struct fattr3 attr;
	int	mask;
	int aclcount;
	struct aclentry aclentry<>;
	int daclcount;
	struct aclentry daclentry<>;
};

struct setaclargs {
	netobj fh;
	int mask;
	int aclcount;
	struct aclentry aclentry<>;
	int daclcount;
	struct aclentry daclentry<>;
};

struct setaclreply {
	int status;
	int attr_follows;
	struct fattr3 attr;
};
