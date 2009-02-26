/*
   Copyright (c) 2006-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef __META_H__
#define __META_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

struct _meta_dirent {
  const char *name;
  int type;
  struct _meta_dirent *children;
  struct _meta_dirent *parent;
  struct _meta_dirent *next;
  struct stat *stbuf;
  xlator_t *view_xlator;
  struct xlator_fops *fops;
};
typedef struct _meta_dirent meta_dirent_t;

typedef struct {
  const char *directory;
  meta_dirent_t *tree;
} meta_private_t;

#include "tree.h"
#include "misc.h"

#endif /* __META_H__ */
