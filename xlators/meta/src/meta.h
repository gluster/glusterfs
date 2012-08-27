/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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
