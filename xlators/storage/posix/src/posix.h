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

#ifndef _POSIX_H
#define _POSIX_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>

#ifdef linux
#ifdef __GLIBC__
#include <sys/fsuid.h>
#else
#include <unistd.h>
#endif
#endif

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#ifdef HAVE_SYS_EXTATTR_H
#include <sys/extattr.h>
#endif

#include "xlator.h"
#include "inode.h"
#include "compat.h"

struct posix_fd {
  int32_t fd;
  int32_t flags;
  char *path;
  DIR *dir;
};

struct posix_private {
  inode_table_t *itable;
  int32_t temp;
  char is_stateless;
  char *base_path;
  int32_t base_path_length;

  struct xlator_stats stats; /* Statastics, provides activity of the server */
  
  struct timeval prev_fetch_time;
  struct timeval init_time;
  int32_t max_read;            /* */
  int32_t max_write;           /* */
  int64_t interval_read;      /* Used to calculate the max_read value */
  int64_t interval_write;     /* Used to calculate the max_write value */
  int64_t read_value;    /* Total read, from init */
  int64_t write_value;   /* Total write, from init */
  char export_statfs;    /* Some times, there may be an extra export from the same partition, in that case 
			    statvfs output of which may not be desired on client, hence don't send statvfs 
			    info to client, so the output of df over GlusterFS will be sane. */
  char o_direct;         /* If set, always open the files with O_DIRECT flag set */
};

#endif /* _POSIX_H */
