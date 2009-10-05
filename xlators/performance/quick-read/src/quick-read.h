/*
  Copyright (c) 2009-2010 Gluster, Inc. <http://www.gluster.com>
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

#ifndef __QUICK_READ_H
#define __QUICK_READ_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "list.h"
#include "compat.h"
#include "compat-errno.h"
#include "common-utils.h"
#include "call-stub.h"
#include "defaults.h"
#include <libgen.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define GLUSTERFS_CONTENT_KEY "glusterfs.content"

struct qr_fd_ctx {
        char              opened;
        char              open_in_transit;
        char             *path;
        int               flags;
        struct list_head  waiting_ops;
        gf_lock_t         lock;
};
typedef struct qr_fd_ctx qr_fd_ctx_t;

struct qr_local {
        char         is_open;
        fd_t        *fd;
        int          open_flags;
        int32_t      op_ret;
        int32_t      op_errno;
        call_stub_t *stub;
};
typedef struct qr_local qr_local_t;

struct qr_file {
        dict_t           *xattr;
        struct stat       stbuf;
        struct timeval    tv;
        gf_lock_t         lock;
};
typedef struct qr_file qr_file_t;

struct qr_conf {
        uint64_t  max_file_size;
        int32_t   cache_timeout;
};
typedef struct qr_conf qr_conf_t;

#endif /* #ifndef __QUICK_READ_H */
