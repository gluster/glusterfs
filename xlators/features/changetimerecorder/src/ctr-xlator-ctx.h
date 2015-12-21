/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __CTR_XLATOR_CTX_H
#define __CTR_XLATOR_CTX_H

#include "xlator.h"
#include "ctr_mem_types.h"
#include "iatt.h"
#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "locking.h"
#include "common-utils.h"
#include <time.h>
#include <sys/time.h>

typedef struct ctr_hard_link {
        uuid_t                  pgfid;
        char                    *base_name;
        /* Hardlink expiry : Defines the expiry period after which a
         * database heal is attempted. */
        uint64_t                  hardlink_heal_period;
        struct list_head        list;
} ctr_hard_link_t;

typedef struct ctr_xlator_ctx {
        /* This represents the looked up hardlinks
         * NOTE: This doesn't represent all physical hardlinks of the inode*/
        struct list_head        hardlink_list;
        uint64_t                inode_heal_period;
        gf_lock_t               lock;
} ctr_xlator_ctx_t;


ctr_hard_link_t *
ctr_search_hard_link_ctx (xlator_t                  *this,
                          ctr_xlator_ctx_t        *ctr_xlator_ctx,
                          uuid_t                  pgfid,
                          const char              *base_name);


int
ctr_add_hard_link (xlator_t          *this,
               ctr_xlator_ctx_t         *ctr_xlator_ctx,
               uuid_t                   pgfid,
               const char               *base_name);



int
ctr_delete_hard_link (xlator_t                *this,
                  ctr_xlator_ctx_t      *ctr_xlator_ctx,
                  uuid_t                pgfid,
                  const char            *base_name);


int
ctr_update_hard_link (xlator_t                *this,
                  ctr_xlator_ctx_t      *ctr_xlator_ctx,
                  uuid_t                pgfid,
                  const char            *base_name,
                  uuid_t                old_pgfid,
                  const char            *old_base_name);


ctr_xlator_ctx_t *
get_ctr_xlator_ctx (xlator_t *this,
                    inode_t *inode);




ctr_xlator_ctx_t *
init_ctr_xlator_ctx (xlator_t *this,
                     inode_t *inode);


void
fini_ctr_xlator_ctx (xlator_t *this,
                     inode_t *inode);

#endif
