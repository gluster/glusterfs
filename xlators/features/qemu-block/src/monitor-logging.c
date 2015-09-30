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

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "qemu-block-memory-types.h"

#include "block/block_int.h"

Monitor *cur_mon;

int
monitor_cur_is_qmp()
{
	/* No QMP support here */
	return 0;
}

void
monitor_set_error (Monitor *mon, QError *qerror)
{
	/* NOP here */
	return;
}


void
monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
{
	char buf[4096];

	vsnprintf(buf, sizeof(buf), fmt, ap);

	gf_log (THIS->name, GF_LOG_ERROR, "%s", buf);
}
