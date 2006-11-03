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

#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include "xlator.h"

struct sched_ops {
  int32_t (*init) (struct xlator *this);
  void (*fini) (struct xlator *this);
  struct xlator *(*schedule) (struct xlator *this, int32_t size);
};

extern struct sched_ops *get_scheduler (const char *name);

#endif /* _SCHEDULER_H */
