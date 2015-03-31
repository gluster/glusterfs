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
        int need_writeback;                     /* does the inode need
                                                      a writeback to disk? */
        unsigned long currentversion;           /* ongoing version */

        struct release {
                int32_t ordflags;
                unsigned long opencount;        /* number of open()s before
                                                   final release() */
                unsigned long releasecount;     /* number of release()s */
        } releasectx;
#define BR_STUB_REQUIRE_RELEASE_CBK 0x0E0EA0E
} br_stub_inode_ctx_t;


#define I_DIRTY  (1<<0)        /* inode needs writeback */
#define WRITEBACK_DURABLE 1    /* writeback is durable */

/**
 * This could just have been a plain struct without unions and all,
 * but we may need additional things in the future.
 */
typedef struct br_stub_local {
        call_stub_t *fopstub;   /* stub for original fop */

        int versioningtype;     /* not much used atm */

        union {
                struct br_stub_ctx {
                        fd_t          *fd;
                        uuid_t         gfid;
                        inode_t       *inode;
                        unsigned long  version;
                        gf_boolean_t   markdirty;
                } context;
        } u;
} br_stub_local_t;

#define BR_STUB_FULL_VERSIONING (1<<0)
#define BR_STUB_INCREMENTAL_VERSIONING (1<<1)

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

static inline int
br_stub_require_release_call (xlator_t *this, fd_t *fd)
{
        int32_t ret = 0;

        ret = fd_ctx_set (fd, this,
                          (uint64_t)(long)BR_STUB_REQUIRE_RELEASE_CBK);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "could not set fd context (for release callback");
        return ret;
}

/* get/set inode context helpers */

static inline int
__br_stub_get_inode_ctx (xlator_t *this,
                         inode_t *inode, uint64_t *ctx)
{
        return __inode_ctx_get (inode, this, ctx);
}

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

/* version get/set helpers */

static inline unsigned long
__br_stub_writeback_version (br_stub_inode_ctx_t *ctx)
{
        return (ctx->currentversion + 1);
}

static inline void
__br_stub_set_ongoing_version (br_stub_inode_ctx_t *ctx, unsigned long version)
{
        ctx->currentversion = version;
}

static inline void
__br_stub_reset_release_counters (br_stub_inode_ctx_t *ctx)
{
        ctx->releasectx.ordflags = 0;
        ctx->releasectx.opencount = 0;
        ctx->releasectx.releasecount = 0;
}

static inline void
__br_stub_track_release (br_stub_inode_ctx_t *ctx)
{
        ++ctx->releasectx.releasecount;
}

static inline void
___br_stub_track_open (br_stub_inode_ctx_t *ctx)
{
        ++ctx->releasectx.opencount;
}

static inline void
___br_stub_track_open_flags (fd_t *fd, br_stub_inode_ctx_t *ctx)
{
        ctx->releasectx.ordflags |= fd->flags;
}

static inline void
__br_stub_track_openfd (fd_t *fd, br_stub_inode_ctx_t *ctx)
{
        ___br_stub_track_open (ctx);
        ___br_stub_track_open_flags (fd, ctx);
}

static inline int
__br_stub_can_trigger_release (inode_t *inode,
                               br_stub_inode_ctx_t *ctx,
                               unsigned long *version, int32_t *flags)
{
        if (list_empty (&inode->fd_list)
            && (ctx->releasectx.releasecount == ctx->releasectx.opencount)) {
                if (flags)
                        *flags = htonl (ctx->releasectx.ordflags);
                if (version)
                        *version = htonl (ctx->currentversion);

                __br_stub_reset_release_counters (ctx);
                return 1;
        }

        return 0;
}

static inline int32_t
br_stub_get_ongoing_version (xlator_t *this,
                             inode_t *inode, unsigned long *version)
{
        int32_t ret = 0;
        uint64_t ctx_addr = 0;
        br_stub_inode_ctx_t *ctx = NULL;

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &ctx_addr);
                if (ret < 0)
                        goto unblock;
                ctx = (br_stub_inode_ctx_t *) (long) ctx_addr;
                *version = ctx->currentversion;
        }
 unblock:
        UNLOCK (&inode->lock);

        return ret;
}

/**
 * fetch the current version from inode and return the context.
 * inode->lock should be held before invoking this as context
 * *needs* to be valid in the caller.
 */
static inline br_stub_inode_ctx_t *
__br_stub_get_ongoing_version_ctx (xlator_t *this,
                                   inode_t *inode, unsigned long *version)
{
        int32_t ret = 0;
        uint64_t ctx_addr = 0;
        br_stub_inode_ctx_t *ctx = NULL;

        ret = __inode_ctx_get (inode, this, &ctx_addr);
        if (ret < 0)
                return NULL;
        ctx = (br_stub_inode_ctx_t *) (long) ctx_addr;
        if (version)
                *version = ctx->currentversion;

        return ctx;
}

/* filter for xattr fetch */
static inline int
br_stub_is_internal_xattr (const char *name)
{
        if (name
            && ((strncmp (name, BITROT_CURRENT_VERSION_KEY,
                          strlen (BITROT_CURRENT_VERSION_KEY)) == 0)
                || (strncmp (name, BITROT_SIGNING_VERSION_KEY,
                             strlen (BITROT_SIGNING_VERSION_KEY)) == 0)))
                return 1;
        return 0;
}

static inline void
br_stub_remove_vxattrs (dict_t *xattr)
{
        if (xattr) {
                dict_del (xattr, BITROT_CURRENT_VERSION_KEY);
                dict_del (xattr, BITROT_SIGNING_VERSION_KEY);
                dict_del (xattr, BITROT_SIGNING_XATTR_SIZE_KEY);
        }
}

#endif /* __BIT_ROT_STUB_H__ */
