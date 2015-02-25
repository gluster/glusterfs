/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <ctype.h>
#include <sys/uio.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"

#include "bit-rot.h"
#include "bit-rot-mem-types.h"

int32_t
mem_acct_init (xlator_t *this)
{
        int32_t     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_br_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_WARNING, "Memory accounting"
                        " init failed");
                return ret;
        }

        return ret;
}

int32_t
init (xlator_t *this)
{
	br_private_t *priv = NULL;
        int32_t   ret = -1;

	if (!this->children) {
		gf_log (this->name, GF_LOG_ERROR,
			"FATAL: no children");
		goto out;
	}

        priv = GF_CALLOC (1, sizeof (*priv), gf_br_mt_br_private_t);
        if (!priv)
                goto out;

	this->private = priv;

        ret = 0;

out:
        gf_log (this->name, GF_LOG_DEBUG, "bit-rot xlator loaded");
	return ret;
}

void
fini (xlator_t *this)
{
	br_private_t *priv = this->private;

        if (!priv)
                return;
        this->private = NULL;
	GF_FREE (priv);

	return;
}

struct xlator_fops fops;

struct xlator_cbks cbks;

struct volume_options options[] = {
	{ .key  = {NULL} },
};
