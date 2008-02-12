/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _LOCK_H
#define _LOCK_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"

#include "hashfn.h"

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
