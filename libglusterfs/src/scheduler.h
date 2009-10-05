/*
   Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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
};

extern struct sched_ops *get_scheduler (xlator_t *xl, const char *name);

#endif /* _SCHEDULER_H */
