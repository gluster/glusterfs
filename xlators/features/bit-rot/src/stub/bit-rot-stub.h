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

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "defaults.h"
#include "call-stub.h"
#include "bit-rot-stub-mem-types.h"

#include "bit-rot-common.h"
#include "bit-rot-stub-messages.h"

typedef int (br_stub_version_cbk) (call_frame_t *, void *,
                                   xlator_t *, int32_t, int32_t, dict_t *);

typedef struct br_stub_inode_ctx {
        int need_writeback;                     /* does the inode need
                                                      a writeback to disk? */
        unsigned long currentversion;           /* ongoing version */

        int            info_sign;
        struct list_head fd_list; /* list of open fds or fds participating in
                                     write operations */
        gf_boolean_t bad_object;
} br_stub_inode_ctx_t;

typedef struct br_stub_fd {
        fd_t *fd;
        struct list_head list;
} br_stub_fd_t;

#define I_DIRTY  (1<<0)        /* inode needs writeback */
#define I_MODIFIED (1<<1)
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
                } context;
        } u;
} br_stub_local_t;

#define BR_STUB_NO_VERSIONING (1 << 0)
#define BR_STUB_INCREMENTAL_VERSIONING (1 << 1)

typedef struct br_stub_private {
        gf_boolean_t go;

        uint32_t boot[2];
        char export[PATH_MAX];

        pthread_mutex_t lock;
        pthread_cond_t  cond;

        struct list_head squeue;      /* ordered signing queue */
        pthread_t signth;

        struct mem_pool *local_pool;
} br_stub_private_t;

static inline gf_boolean_t
__br_stub_is_bad_object (br_stub_inode_ctx_t *ctx)
{
        return ctx->bad_object;
}

static inline void
__br_stub_mark_object_bad (br_stub_inode_ctx_t *ctx)
{
        ctx->bad_object = _gf_true;
}

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

/* inode mofification markers */
static inline void
__br_stub_set_inode_modified (br_stub_inode_ctx_t *ctx)
{
        ctx->need_writeback |= I_MODIFIED;
}

static inline void
__br_stub_unset_inode_modified (br_stub_inode_ctx_t *ctx)
{
        ctx->need_writeback &= ~I_MODIFIED;
}

static inline int
__br_stub_is_inode_modified (br_stub_inode_ctx_t *ctx)
{
        return (ctx->need_writeback & I_MODIFIED);
}

br_stub_fd_t *
br_stub_fd_new (void)
{
        br_stub_fd_t    *br_stub_fd = NULL;

        br_stub_fd = GF_CALLOC (1, sizeof (*br_stub_fd),
                                gf_br_stub_mt_br_stub_fd_t);

        return br_stub_fd;
}

int
__br_stub_fd_ctx_set (xlator_t *this, fd_t *fd, br_stub_fd_t *br_stub_fd)
{
        uint64_t    value = 0;
        int         ret   = -1;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, br_stub_fd, out);

        value = (uint64_t)(long) br_stub_fd;

        ret = __fd_ctx_set (fd, this, value);

out:
        return ret;
}

br_stub_fd_t *
__br_stub_fd_ctx_get (xlator_t *this, fd_t *fd)
{
        br_stub_fd_t *br_stub_fd = NULL;
        uint64_t  value  = 0;
        int       ret    = -1;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        ret = __fd_ctx_get (fd, this, &value);
        if (ret)
                return NULL;

        br_stub_fd = (br_stub_fd_t *) ((long) value);

out:
        return br_stub_fd;
}

br_stub_fd_t *
br_stub_fd_ctx_get (xlator_t *this, fd_t *fd)
{
        br_stub_fd_t *br_stub_fd = NULL;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        LOCK (&fd->lock);
        {
                br_stub_fd = __br_stub_fd_ctx_get (this, fd);
        }
        UNLOCK (&fd->lock);

out:
        return br_stub_fd;
}

int32_t
br_stub_fd_ctx_set (xlator_t *this, fd_t *fd, br_stub_fd_t *br_stub_fd)
{
        int32_t    ret = -1;

        GF_VALIDATE_OR_GOTO ("bit-rot-stub", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, br_stub_fd, out);

        LOCK (&fd->lock);
        {
                ret = __br_stub_fd_ctx_set (this, fd, br_stub_fd);
        }
        UNLOCK (&fd->lock);

out:
        return ret;
}

static inline int
br_stub_require_release_call (xlator_t *this, fd_t *fd, br_stub_fd_t **fd_ctx)
{
        int32_t ret = 0;
        br_stub_fd_t *br_stub_fd = NULL;

        br_stub_fd = br_stub_fd_new ();
        if (!br_stub_fd)
                return -1;

        br_stub_fd->fd = fd;
        INIT_LIST_HEAD (&br_stub_fd->list);

        ret = br_stub_fd_ctx_set (this, fd, br_stub_fd);
        if (ret)
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        BRS_MSG_SET_CONTEXT_FAILED,
                        "could not set fd context (for release callback");
        else
                *fd_ctx = br_stub_fd;

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
        int ret = -1;

        LOCK (&inode->lock);
        {
                ret = __br_stub_get_inode_ctx (this, inode, ctx);
        }
        UNLOCK (&inode->lock);

        return ret;
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
        if (ctx->currentversion < version)
                ctx->currentversion = version;
        else
                gf_msg ("bit-rot-stub", GF_LOG_WARNING, 0,
                        BRS_MSG_CHANGE_VERSION_FAILED, "current version: %lu"
                        "new version: %lu", ctx->currentversion, version);
}

static inline int
__br_stub_can_trigger_release (inode_t *inode,
                               br_stub_inode_ctx_t *ctx, unsigned long *version)
{
        /**
         * If the inode is modified, then it has to be dirty. An inode is
         * marked dirty once version is increased. Its marked as modified
         * when the modification call (write/truncate) which triggered
         * the versioning is successful.
         */
        if (__br_stub_is_inode_modified (ctx)
            && list_empty (&ctx->fd_list)
            && (ctx->info_sign != BR_SIGN_REOPEN_WAIT)) {

                GF_ASSERT (__br_stub_is_inode_dirty (ctx) == 0);

                if (version)
                        *version = htonl (ctx->currentversion);
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
                dict_del (xattr, BITROT_OBJECT_BAD_KEY);
                dict_del (xattr, BITROT_CURRENT_VERSION_KEY);
                dict_del (xattr, BITROT_SIGNING_VERSION_KEY);
                dict_del (xattr, BITROT_SIGNING_XATTR_SIZE_KEY);
        }
}

/**
 * This function returns the below values for different situations
 * 0  => as per the inode context object is not bad
 * -1 => Failed to get the inode context itself
 * -2 => As per the inode context object is bad
 * Both -ve values means the fop which called this function is failed
 * and error is returned upwards.
 * In future if needed or more errors have to be handled, then those
 * errors can be made into enums.
 */
static inline int
br_stub_is_bad_object (xlator_t *this, inode_t *inode)
{
        int                  bad_object = 0;
        gf_boolean_t         tmp        = _gf_false;
        uint64_t             ctx_addr   = 0;
        br_stub_inode_ctx_t *ctx        = NULL;
        int32_t              ret        = -1;

        ret = br_stub_get_inode_ctx (this, inode, &ctx_addr);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRS_MSG_GET_INODE_CONTEXT_FAILED,
                        "failed to get the inode context for the inode %s",
                        uuid_utoa (inode->gfid));
                bad_object = -1;
                goto out;
        }

        ctx = (br_stub_inode_ctx_t *)(long)ctx_addr;

        LOCK (&inode->lock);
        {
                tmp = __br_stub_is_bad_object (ctx);
                if (tmp)
                        bad_object = -2;
        }
        UNLOCK (&inode->lock);

out:
        return bad_object;
}

static inline int32_t
br_stub_mark_object_bad (xlator_t *this, inode_t *inode)
{
        int32_t  ret = -1;
        uint64_t ctx_addr = 0;
        br_stub_inode_ctx_t *ctx = NULL;

        ret = br_stub_get_inode_ctx (this, inode, &ctx_addr);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        BRS_MSG_GET_INODE_CONTEXT_FAILED, "failed to get the "
                        "inode context for the inode %s",
                        uuid_utoa (inode->gfid));
               goto out;
        }

        ctx = (br_stub_inode_ctx_t *)(long)ctx_addr;

        LOCK (&inode->lock);
        {
                __br_stub_mark_object_bad (ctx);
        }
        UNLOCK (&inode->lock);

out:
        return ret;
}

/**
 * There is a possibility that dict_set might fail. The o/p of dict_set is
 * given to the caller and the caller has to decide what to do.
 */
static inline int32_t
br_stub_mark_xdata_bad_object (xlator_t *this, inode_t *inode, dict_t *xdata)
{
        int32_t    ret = 0;

        if (br_stub_is_bad_object (this, inode) == -2)
                ret = dict_set_int32 (xdata, GLUSTERFS_BAD_INODE, 1);

        return ret;
}

int32_t
br_stub_add_fd_to_inode (xlator_t *this, fd_t *fd, br_stub_inode_ctx_t *ctx);

br_sign_state_t
__br_stub_inode_sign_state (br_stub_inode_ctx_t *ctx, glusterfs_fop_t fop,
                            fd_t *fd);
#endif /* __BIT_ROT_STUB_H__ */
