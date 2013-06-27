/*
  BD translator - Exports Block devices on server side as regular
  files to client

  Copyright IBM, Corp. 2012

  This file is part of GlusterFS.

  Author:
  M. Mohan Kumar <mohan@in.ibm.com>

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _BD_MAP_H
#define _BD_MAP_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "mem-types.h"

#define BD_XLATOR "block device mapper xlator"

#define BACKEND_VG "vg"

/* lvm2-2.02.79 added this in lvm2app.h, but it is available for linking in
 * older versions already */
#if NEED_LVM_LV_FROM_NAME_DECL
lv_t lvm_lv_from_name(vg_t vg, const char *name);
#endif

enum gf_bd_mem_types_ {
        gf_bd_fd = gf_common_mt_end + 1,
        gf_bd_private,
        gf_bd_entry,
        gf_bd_attr,
        gf_bd_mt_end
};

/*
 * Each BD/LV is represented by this data structure
 * Usually root entry will have only children and there is no sibling for that
 * All other entries may have children and/or sibling entries
 * If an entry is a Volume Group it will have child (. & .. and Logical
 * Volumes) and also other Volume groups will be a sibling for this
 */
typedef struct bd_entry {
        struct list_head child; /* List to child */
        struct list_head sibling; /* List of siblings */
        struct bd_entry  *parent;/* Parent of this node */
        struct bd_entry  *link; /* Link to actual entry, if its . or .. */
        char             name[NAME_MAX];
        struct iatt      *attr;
        int              refcnt;
        uint64_t         size;
        pthread_rwlock_t lock;
} bd_entry_t;

/**
 * bd_fd - internal structure common to file and directory fd's
 */
typedef struct bd_fd {
        bd_entry_t      *entry;
        bd_entry_t      *p_entry; /* Parent entry */
        int             fd;
        int32_t         flag;
} bd_fd_t;

typedef struct bd_priv {
        lvm_t             handle;
        pthread_rwlock_t  lock;
        char              *vg;
} bd_priv_t;

#endif
