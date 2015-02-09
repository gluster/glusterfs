 /*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __BIT_ROT_STUB_H__
#define __BIT_ROT_STUB_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "defaults.h"
#include "call-stub.h"

#include "bit-rot-common.h"

typedef int (br_stub_version_cbk) (call_frame_t *, void *,
                                   xlator_t *, int32_t, int32_t, dict_t *);

typedef struct br_stub_inode_ctx {
        int need_writeback;
        unsigned long currentversion;
        unsigned long transientversion;
} br_stub_inode_ctx_t;

#define I_DIRTY  (1<<0)        /* inode needs writeback */
#define WRITEBACK_DURABLE 1    /* writeback is durable */

/**
 * This could just have been a plain struct without unions and all,
 * but we may need additional things in the future.
 */
typedef struct br_stub_local {
        call_stub_t *fopstub;   /* stub for original fop */

        int versioningtype; /* not much used atm */

        union {
                struct br_stub_ctx {
                        uuid_t         gfid;
                        inode_t       *inode;
                        unsigned long  version;
                        gf_boolean_t   markdirty;
                } context;
        } u;
} br_stub_local_t;

#define BR_STUB_FULL_VERSIONING 1<<0
#define BR_STUB_INCREMENTAL_VERSIONING 1<<1

typedef struct br_stub_private {
        gf_boolean_t go;

        uint32_t boot[2];
        char export[PATH_MAX];

        struct mem_pool *local_pool;
} br_stub_private_t;

/* inode writeback helpers */
static inline void
__br_stub_mark_inode_dirty (br_stub_inode_ctx_t *ctx)
{
        ctx->need_writeback |= I_DIRTY;
}

static inline void
__br_stub_mark_inode_synced (br_stub_inode_ctx_t *ctx)
{
        ctx->need_writeback &= ~I_DIRTY;
}

static inline int
__br_stub_is_inode_dirty (br_stub_inode_ctx_t *ctx)
{
        return (ctx->need_writeback & I_DIRTY);
}

/* get/set inode context helpers */

static inline int
br_stub_get_inode_ctx (xlator_t *this,
                       inode_t *inode, uint64_t *ctx)
{
        return inode_ctx_get (inode, this, ctx);
}

static inline int
br_stub_set_inode_ctx (xlator_t *this,
                       inode_t *inode, br_stub_inode_ctx_t *ctx)
{
        uint64_t ctx_addr = (uint64_t) ctx;
        return inode_ctx_set (inode, this, &ctx_addr);
}

static inline unsigned long
br_stub_get_current_version (br_version_t *obuf, br_signature_t *sbuf)
{
        if (obuf->ongoingversion > sbuf->currentversion)
                return obuf->ongoingversion;
        return sbuf->currentversion;
}

/* filter for xattr fetch */
static inline int
br_stub_is_internal_xattr (const char *name)
{
        if ( name
             && ( (strncmp (name, BITROT_CURRENT_VERSION_KEY,
                            strlen (BITROT_CURRENT_VERSION_KEY)) == 0)
                  || (strncmp (name, BITROT_SIGNING_VERSION_KEY,
                               strlen (BITROT_SIGNING_VERSION_KEY)) == 0) ) )
                return 1;
        return 0;
}

#endif /* __BIT_ROT_STUB_H__ */
