/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _LOCK_TABLE_H
#define _LOCK_TABLE_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"

struct _locker {
        struct list_head  lockers;
        char             *volume;
        loc_t             loc;
        fd_t             *fd;
        gf_lkowner_t      owner;
        pid_t             pid;
};

struct _lock_table {
        struct list_head  inodelk_lockers;
        struct list_head  entrylk_lockers;
        gf_lock_t         lock;
};

int32_t
gf_add_locker (struct _lock_table *table, const char *volume,
               loc_t *loc,
               fd_t *fd,
               pid_t pid,
               gf_lkowner_t *owner,
               glusterfs_fop_t type);

int32_t
gf_del_locker (struct _lock_table *table, const char *volume,
               loc_t *loc,
               fd_t *fd,
               gf_lkowner_t *owner,
               glusterfs_fop_t type);

struct _lock_table *
gf_lock_table_new (void);

#endif /* _LOCK_TABLE_H */
