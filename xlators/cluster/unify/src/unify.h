/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#ifndef _UNIFY_H
#define _UNIFY_H

#include "scheduler.h"

#define MAX_DIR_ENTRY_STRING     (32 * 1024)

struct cement_private {
  /* Update this structure depending on requirement */
  void *scheduler; /* THIS SHOULD BE THE FIRST VARIABLE, if xlator is using scheduler */
  struct sched_ops *sched_ops; /* Scheduler options */
  struct xlator **array; /* Child node array */
  int child_count;
  int is_debug;
};

#endif /* _UNIFY_H */
