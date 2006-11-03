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

#ifndef _LOCK_H
#define _LOCK_H

#include "xlator.h"

#include "hashfn.h"
#include "ns.h"

#define LOCK_HASH 1024

typedef struct _lock_inner {
  struct _lock_inner *next;
  struct _lock_inner *prev;
  const char *path;
  void *who;
} lock_inner_t;

int32_t
mop_lock_impl (call_frame_t *frame,
	       xlator_t *this,
	       const char *path);

int32_t
mop_unlock_impl (call_frame_t *frame,
		 xlator_t *this,
		 const char *path);

#endif /* _LOCK_H */
