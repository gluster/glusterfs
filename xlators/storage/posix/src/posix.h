/*
   Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
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
#include <time.h>

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
#include "timer.h"
#include "posix-mem-types.h"

/**
 * posix_fd - internal structure common to file and directory fd's
 */

struct posix_fd {
	int     fd;      /* fd returned by the kernel */
	int32_t flags;   /* flags for open/creat      */
	char *  path;    /* used by setdents/getdents */
	DIR *   dir;     /* handle returned by the kernel */
        int     flushwrites;
        struct list_head list; /* to add to the janitor list */
};


struct posix_private {
	char   *base_path;
	int32_t base_path_length;

        gf_lock_t lock;

        char   *hostname;
        /* Statistics, provides activity of the server */

	struct timeval prev_fetch_time;
	struct timeval init_time;

        time_t last_landfill_check;
        int32_t janitor_sleep_duration;
        struct list_head janitor_fds;
        pthread_cond_t janitor_cond;
        pthread_mutex_t janitor_lock;

	int64_t read_value;    /* Total read, from init */
	int64_t write_value;   /* Total write, from init */
        int64_t nr_files;
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


/* 
   decide whether posix_unlink does open (file), unlink (file), close (fd)
   instead of just unlink (file). with the former approach there is no lockout
   of access to parent directory during removal of very large files for the
   entire duration of freeing of data blocks.
*/ 
        gf_boolean_t    background_unlink;

/* janitor thread which cleans up /.trash (created by replicate) */
        pthread_t       janitor;
        gf_boolean_t    janitor_present;
        char *          trash_path;
};

#define POSIX_BASE_PATH(this) (((struct posix_private *)this->private)->base_path)

#define POSIX_BASE_PATH_LEN(this) (((struct posix_private *)this->private)->base_path_length)

#define MAKE_REAL_PATH(var, this, path) do {                            \
		var = alloca (strlen (path) + POSIX_BASE_PATH_LEN(this) + 2); \
                strcpy (var, POSIX_BASE_PATH(this));			\
                strcpy (&var[POSIX_BASE_PATH_LEN(this)], path);		\
        } while (0)

#endif /* _POSIX_H */
