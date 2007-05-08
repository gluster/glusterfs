/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>

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

#ifndef __META_H__
#define __META_H__

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
