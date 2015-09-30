/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __TRASH_H__
#define __TRASH_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "defaults.h"
#include "inode.c"
#include "fnmatch.h"

#include <libgen.h>

#ifndef GF_BLOCK_READV_SIZE
#define GF_BLOCK_READV_SIZE      (128 * GF_UNIT_KB)
#endif

#ifndef GF_DEFAULT_MAX_FILE_SIZE
#define GF_DEFAULT_MAX_FILE_SIZE (200 * GF_UNIT_MB)
#endif

#ifndef GF_ALLOWED_MAX_FILE_SIZE
#define GF_ALLOWED_MAX_FILE_SIZE (1 * GF_UNIT_GB)
#endif


struct trash_struct {
        fd_t    *fd;         /* for the fd of existing file */
        fd_t    *newfd;      /* for the newly created file */
        loc_t    loc;        /* to store the location of the existing file */
        loc_t    newloc;     /* to store the location for the new file */
        size_t   fsize;      /* for keeping the size of existing file */
        off_t    cur_offset; /* current offset for read and write ops */
        off_t    fop_offset;
        char     origpath[PATH_MAX];
        char     newpath[PATH_MAX];
        int32_t  loop_count;
        struct iatt preparent;
        struct iatt postparent;
};
typedef struct trash_struct trash_local_t;

struct _trash_elim_pattern;
typedef struct _trash_elim_pattern {
        struct _trash_elim_pattern *next;
        char                       *pattern;
} trash_elim_pattern_t;

struct trash_priv {
        char                 *trash_dir;
        trash_elim_pattern_t *eliminate;
        size_t                max_trash_file_size;
};
typedef struct trash_priv trash_private_t;

#define TRASH_STACK_UNWIND(op, frame, params ...) do {     \
		trash_local_t *__local = NULL;         \
		__local = frame->local;                \
		frame->local = NULL;		       \
		STACK_UNWIND_STRICT (op, frame, params);          \
		trash_local_wipe (__local);	       \
	} while (0)


#endif /* __TRASH_H__ */
