/*
   Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "afr.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"
#include "list.h"
#include "call-stub.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "compat.h"
#include "byte-order.h"

#include "transaction.h"
#include "self-heal.h"


int
afr_dir_self_heal_needed (dict_t *xattr)
{
	int32_t *pending = NULL;
	int ret = -1;
	int op_ret = 0;

	ret = dict_get_bin (xattr, AFR_ENTRY_PENDING, &pending);
	if (ret == 0) {
		op_ret = 1;
	}

	return op_ret;
}


int
afr_inode_self_heal_needed (dict_t *xattr)
{
	int32_t *pending = NULL;
	int ret = -1;
	int op_ret = 0;

	ret = dict_get_bin (xattr, AFR_METADATA_PENDING, &pending);
	if (ret == 0) {
		op_ret = 1;
	}

	ret = dict_get_bin (xattr, AFR_DATA_PENDING, &pending);
	if (ret == 0) {
		op_ret = 1;
	}

	return op_ret;
}


/**
 * Return true if attributes of any two children do not match
 */

static int
attr_mismatch_p ()
{
}


/**
 * Return index of the child which has the higest AFR_*_PENDING
 * number.
 */

static int
freshest_child ()
{
}
