/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"

struct sched_ops {
  int32_t (*init) (xlator_t *this);
  void (*fini) (xlator_t *this);
  void (*update) (xlator_t *this);
  xlator_t *(*schedule) (xlator_t *this, const void *path);
  void (*notify) (xlator_t *xl, int32_t event, void *data);
  int32_t (*mem_acct_init) (xlator_t *this);
};

extern struct sched_ops *get_scheduler (xlator_t *xl, const char *name);

#endif /* _SCHEDULER_H */
