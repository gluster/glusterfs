/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __TREE_H__
#define __TREE_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

meta_dirent_t *
insert_meta_entry (meta_dirent_t *root, const char *path,
		   int type, struct stat *stbuf, struct xlator_fops *fops);
meta_dirent_t *
lookup_meta_entry (meta_dirent_t *root, const char *path, 
		   char **remain);

#endif /* __TREE_H__ */
