/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "defaults.h"

int32_t
init (xlator_t *this)
{
	if (!this->children || this->children->next) {
		gf_log (this->name, GF_LOG_ERROR,
			"translator not configured with exactly one child");
		return -1;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

	return 0;
}


void
fini (xlator_t *this)
{
	return;
}


struct xlator_fops fops = {
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
	{ .key = {NULL} },
};
