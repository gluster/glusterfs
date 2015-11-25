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
#include "syscall.h"
#include "bit-rot-common.h"
#include "bit-rot-stub-messages.h"
#include "glusterfs3-xdr.h"

#define BAD_OBJECT_THREAD_STACK_SIZE   ((size_t)(1024*1024))

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
        struct bad_object_dir {
                DIR *dir;
                off_t dir_eof;
        } bad_object;
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
        struct bad_objects_container {
                pthread_t thread;
                pthread_mutex_t bad_lock;
                pthread_cond_t  bad_cond;
                struct list_head bad_queue;
        } container;
        struct mem_pool *local_pool;

        char stub_basepath[PATH_MAX];

        uuid_t bad_object_dir_gfid;
} br_stub_private_t;

br_stub_fd_t *
br_stub_fd_new (void);


int
__br_stub_fd_ctx_set (xlator_t *this, fd_t *fd, br_stub_fd_t *br_stub_fd);

br_stub_fd_t *
__br_stub_fd_ctx_get (xlator_t *this, fd_t *fd);

br_stub_fd_t *
br_stub_fd_ctx_get (xlator_t *this, fd_t *fd);

int32_t
br_stub_fd_ctx_set (xlator_t *this, fd_t *fd, br_stub_fd_t *br_stub_fd);

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

int
br_stub_dir_create (xlator_t *this, br_stub_private_t *priv);

int
br_stub_add (xlator_t *this, uuid_t gfid);

int32_t
br_stub_create_stub_gfid (xlator_t *this, char *stub_gfid_path, uuid_t gfid);

int
br_stub_dir_create (xlator_t *this, br_stub_private_t *priv);

call_stub_t *
__br_stub_dequeue (struct list_head *callstubs);

void
__br_stub_enqueue (struct list_head *callstubs, call_stub_t *stub);

void
br_stub_worker_enqueue (xlator_t *this, call_stub_t *stub);

void *
br_stub_worker (void *data);

int32_t
br_stub_lookup_wrapper (call_frame_t *frame, xlator_t *this,
                        loc_t *loc, dict_t *xattr_req);

int32_t
br_stub_readdir_wrapper (call_frame_t *frame, xlator_t *this,
                         fd_t *fd, size_t size, off_t off, dict_t *xdata);

int
br_stub_del (xlator_t *this, uuid_t gfid);

#endif /* __BIT_ROT_STUB_H__ */
