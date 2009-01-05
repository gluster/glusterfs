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

/**
 * posix_fd - internal structure common to file and directory fd's
 */

struct posix_fd {
	int     fd;      /* fd returned by the kernel */
	int32_t flags;   /* flags for open/creat      */
	char *  path;    /* used by setdents/getdents */
	DIR *   dir;     /* handle returned by the kernel */
};

struct posix_private {
	char   *base_path;
	int32_t base_path_length;
	dev_t   base_stdev;
        /* Statistics, provides activity of the server */
	struct xlator_stats stats; 
  
	struct timeval prev_fetch_time;
	struct timeval init_time;

	int32_t max_read;            /* */
	int32_t max_write;           /* */
	int64_t interval_read;      /* Used to calculate the max_read value */
	int64_t interval_write;     /* Used to calculate the max_write value */
	int64_t read_value;    /* Total read, from init */
	int64_t write_value;   /* Total write, from init */

/*
   In some cases, two exported volumes may reside on the same
   partition on the server. Sending statvfs info for both
   the volumes will lead to erroneous df output at the client,
   since free space on the partition will be counted twice.

   In such cases, user can disable exporting statvfs info
   on one of the volumes by setting this option.
*/
	gf_boolean_t    export_statfs;

	gf_boolean_t    o_direct;     /* always open files in O_DIRECT mode */
};

#define POSIX_BASE_PATH(this) (((struct posix_private *)this->private)->base_path)

#define POSIX_BASE_PATH_LEN(this) (((struct posix_private *)this->private)->base_path_length)

#endif /* _POSIX_H */
