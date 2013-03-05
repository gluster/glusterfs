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
#include "syncop.h"
#include "qemu-block-memory-types.h"

#include "block/aio.h"

void
qemu_bh_schedule (QEMUBH *bh)
{
	return;
}

void
qemu_bh_cancel (QEMUBH *bh)
{
	return;
}

void
qemu_bh_delete (QEMUBH *bh)
{

}

QEMUBH *
qemu_bh_new (QEMUBHFunc *cb, void *opaque)
{
	return NULL;
}
