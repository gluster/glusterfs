/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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
#include <fnmatch.h>
#include "quick-read-mem-types.h"

struct qr_fd_ctx {
        char              opened;
        char              disabled;
        char              open_in_transit;
        char             *path;
        int               flags;
        int               wbflags;
        struct list_head  waiting_ops;
        gf_lock_t         lock;
        struct list_head  inode_list;
        fd_t             *fd;
        dict_t           *xdata;
};
typedef struct qr_fd_ctx qr_fd_ctx_t;

struct qr_local {
        char              is_open;
        char             *path;
        char              just_validated;
        fd_t             *fd;
        int               open_flags;
        int32_t           op_ret;
        int32_t           op_errno;
        uint32_t          open_count;
        call_stub_t      *stub;
        struct list_head  list;
        gf_lock_t         lock;
};
typedef struct qr_local qr_local_t;

struct qr_inode {
        dict_t           *xattr;
        inode_t          *inode;
        int               priority;
        struct iatt       stbuf;
        struct timeval    tv;
        struct list_head  lru;
        struct list_head  fd_list;
        struct list_head  unlinked_dentries;
};
typedef struct qr_inode qr_inode_t;

struct qr_priority {
        char             *pattern;
        int32_t           priority;
        struct list_head  list;
};
typedef struct qr_priority qr_priority_t;

struct qr_conf {
        uint64_t         max_file_size;
        int32_t          cache_timeout;
        uint64_t         cache_size;
        int              max_pri;
        struct list_head priority_list;
};
typedef struct qr_conf qr_conf_t;

struct qr_inode_table {
        uint64_t          cache_used;
        struct list_head *lru;
        gf_lock_t         lock;
};
typedef struct qr_inode_table qr_inode_table_t;

struct qr_private {
        qr_conf_t         conf;
        qr_inode_table_t  table;
};
typedef struct qr_private qr_private_t;

struct qr_unlink_ctx {
        struct list_head  list;
        qr_fd_ctx_t      *fdctx;
        char              need_open;
};
typedef struct qr_unlink_ctx qr_unlink_ctx_t;

void qr_local_free (qr_local_t *local);

#define QR_STACK_UNWIND(op, frame, params ...) do {             \
                qr_local_t *__local = frame->local;             \
                frame->local = NULL;                            \
                STACK_UNWIND_STRICT (op, frame, params);        \
                qr_local_free (__local);                        \
        } while (0)

#endif /* #ifndef __QUICK_READ_H */
