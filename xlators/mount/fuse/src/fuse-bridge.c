/*
   Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

/*
 * TODO:
 * Need to free_state() when fuse_reply_err() + return.
 * Check loc->path for "" after fuse_loc_fill in all fops
 * (now being done in getattr, lookup) or better - make
 * fuse_loc_fill() and inode_path() return success/failure.
 */

#include <stdint.h>
#include <signal.h>
#include <pthread.h>
#include <stddef.h>
#include <dirent.h>
#include <sys/mount.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif /* _CONFIG_H */

#include "glusterfs.h"
#include "logging.h"
#include "xlator.h"
#include "defaults.h"
#include "common-utils.h"
#include "statedump.h"

#include "fuse_kernel.h"
#include "fuse-misc.h"
#include "fuse-mount.h"

#include "list.h"
#include "dict.h"

#include "compat.h"
#include "compat-errno.h"

/* TODO: when supporting posix acl, remove this definition */
#define DISABLE_POSIX_ACL

#define ZR_MOUNTPOINT_OPT       "mountpoint"
#define ZR_DIRECT_IO_OPT        "direct-io-mode"
#define ZR_STRICT_VOLFILE_CHECK "strict-volfile-check"

#define FUSE_712_OP_HIGH (FUSE_POLL + 1)
#define GLUSTERFS_XATTR_LEN_MAX  65536

typedef struct fuse_in_header fuse_in_header_t;
typedef void (fuse_handler_t) (xlator_t *this, fuse_in_header_t *finh,
                               void *msg);

struct fuse_private {
        int                  fd;
        uint32_t             proto_minor;
        char                *volfile;
        size_t               volfile_size;
        char                *mount_point;
        struct iobuf        *iobuf;
        pthread_t            fuse_thread;
        char                 fuse_thread_started;
        uint32_t             direct_io_mode;
        size_t              *msg0_len_p;
        double               entry_timeout;
        double               attribute_timeout;
        pthread_cond_t       first_call_cond;
        pthread_mutex_t      first_call_mutex;
        char                 first_call;
        gf_boolean_t         strict_volfile_check;
};
typedef struct fuse_private fuse_private_t;

#define _FH_TO_FD(fh) ((fd_t *)(uintptr_t)(fh))

#define FH_TO_FD(fh) ((_FH_TO_FD (fh))?(fd_ref (_FH_TO_FD (fh))):((fd_t *) 0))

#define FUSE_FOP(state, ret, op_num, fop, args ...)                     \
        do {                                                            \
                call_frame_t *frame = NULL;                             \
                xlator_t *xl = NULL;                                    \
                                                                        \
                frame = get_call_frame_for_req (state);                 \
                if (!frame) {                                           \
                         /* This is not completely clean, as some       \
                          * earlier allocations might remain unfreed    \
                          * if we return at this point, but still       \
                          * better than trying to go on with a NULL     \
                          * frame ...                                   \
                          */                                            \
                        gf_log ("glusterfs-fuse",                       \
                                GF_LOG_ERROR,                           \
                                "FUSE message"                          \
                                " unique %"PRIu64" opcode %d:"          \
                                " frame allocation failed",             \
                                state->finh->unique,                    \
                                state->finh->opcode);                   \
                        free_state (state);                             \
                        return;                                         \
                }                                                       \
                xl = frame->this->children ?                            \
                        frame->this->children->xlator : NULL;           \
                frame->root->state = state;                             \
                frame->root->op    = op_num;                            \
                STACK_WIND (frame, ret, xl, xl->fops->fop, args);       \
        } while (0)

#define GF_SELECT_LOG_LEVEL(_errno)                     \
        (((_errno == ENOENT) || (_errno == ESTALE))?    \
         GF_LOG_DEBUG)

#define GET_STATE(this, finh, state)                                       \
        do {                                                               \
                state = get_state (this, finh);                            \
                if (!state) {                                              \
                        gf_log ("glusterfs-fuse",                          \
                                GF_LOG_ERROR,                              \
                                "FUSE message unique %"PRIu64" opcode %d:" \
                                " state allocation failed",                \
                                finh->unique, finh->opcode);               \
                                                                           \
                        send_fuse_err (this, finh, ENOMEM);                \
                        FREE (finh);                                       \
                                                                           \
                        return;                                            \
                }                                                          \
        } while (0)


typedef struct {
        void          *pool;
        xlator_t      *this;
        inode_table_t *itable;
        loc_t          loc;
        loc_t          loc2;
        fuse_in_header_t *finh;
        int32_t        flags;
        off_t          off;
        size_t         size;
        unsigned long  nlookup;
        fd_t          *fd;
        dict_t        *dict;
        char          *name;
        char           is_revalidate;
        int32_t        callcount;
        gf_lock_t      lock;
} fuse_state_t;


static void
free_state (fuse_state_t *state)
{
        loc_wipe (&state->loc);

        loc_wipe (&state->loc2);

        if (state->dict) {
                dict_unref (state->dict);
                state->dict = (void *)0xaaaaeeee;
        }
        if (state->name) {
                FREE (state->name);
                state->name = NULL;
        }
        if (state->fd) {
                fd_unref (state->fd);
                state->fd = (void *)0xfdfdfdfd;
        }
        if (state->finh) {
                FREE (state->finh);
                state->finh = NULL;
        }
#ifdef DEBUG
        memset (state, 0x90, sizeof (*state));
#endif
        FREE (state);
        state = NULL;
}


static int
__can_fuse_return (fuse_state_t *state,
                   char success)
{
        int ret = 0;

        if (success) {
                if ((state->callcount == 0)
                    || (state->callcount == 1))
                        ret = 1;
                else
                        ret = 0;
        } else {
                if (state->callcount != -1)
                        ret = 1;
                else
                        ret = 0;
        }

        return ret;
}


static void
__fuse_mark_return (fuse_state_t *state,
                    char success)
{
        if (success) {
                if (state->callcount == 2)
                        state->callcount--;
                else
                        state->callcount = 0;
        } else {
                if (state->callcount == 2)
                        state->callcount = -1;
                else
                        state->callcount = 0;
        }

        return;
}


static int
can_fuse_return (fuse_state_t *state,
                 char success)
{
        int ret = 0;

        LOCK(&state->lock);
        {
                ret = __can_fuse_return (state, success);

                __fuse_mark_return (state, success);
        }
        UNLOCK(&state->lock);

        return ret;
}


fuse_state_t *
get_state (xlator_t *this, fuse_in_header_t *finh)
{
        fuse_state_t *state = NULL;

        state = (void *)calloc (1, sizeof (*state));
        if (!state)
                return NULL;
        state->pool = this->ctx->pool;
        state->itable = this->itable;
        state->finh = finh;
        state->this = this;

        LOCK_INIT (&state->lock);

        return state;
}


static call_frame_t *
get_call_frame_for_req (fuse_state_t *state)
{
        call_pool_t           *pool = NULL;
        fuse_in_header_t      *finh = NULL;
        call_frame_t          *frame = NULL;
        xlator_t              *this = NULL;
        fuse_private_t        *priv = NULL;

        pool = state->pool;
        finh = state->finh;
        this = state->this;
        priv = this->private;

        frame = create_frame (this, pool);
        if (!frame)
                return NULL;

        if (finh) {
                frame->root->uid    = finh->uid;
                frame->root->gid    = finh->gid;
                frame->root->pid    = finh->pid;
                frame->root->unique = finh->unique;
        }

        frame->root->type = GF_OP_TYPE_FOP_REQUEST;

        return frame;
}


/*
 * iov_out should contain a fuse_out_header at zeroth position.
 * The error value of this header is sent to kernel.
 */
static int
send_fuse_iov (xlator_t *this, fuse_in_header_t *finh, struct iovec *iov_out,
               int count)
{
        fuse_private_t *priv = NULL;
        struct fuse_out_header *fouh = NULL;
        int res, i;

        priv = this->private;

        fouh = iov_out[0].iov_base;
        iov_out[0].iov_len = sizeof (*fouh);
        fouh->len = 0;
        for (i = 0; i < count; i++)
                fouh->len += iov_out[i].iov_len;
        fouh->unique = finh->unique;

        res = writev (priv->fd, iov_out, count);

        if (res == -1)
                return errno;
        if (res != fouh->len)
                return EINVAL;
        return 0;
}

static int
send_fuse_data (xlator_t *this, fuse_in_header_t *finh, void *data, size_t size)
{
        struct fuse_out_header fouh = {0, };
        struct iovec iov_out[2];

        fouh.error = 0;
        iov_out[0].iov_base = &fouh;
        iov_out[1].iov_base = data;
        iov_out[1].iov_len = size;

        return send_fuse_iov (this, finh, iov_out, 2);
}

#define send_fuse_obj(this, finh, obj) \
        send_fuse_data (this, finh, obj, sizeof (*(obj)))

static int
send_fuse_err (xlator_t *this, fuse_in_header_t *finh, int error)
{
        struct fuse_out_header fouh = {0, };
        struct iovec iov_out;

        fouh.error = -error;
        iov_out.iov_base = &fouh;

        return send_fuse_iov (this, finh, &iov_out, 1);
}

static inode_t *
fuse_ino_to_inode (uint64_t ino, inode_table_t *table)
{
        inode_t *inode = NULL;

        if (ino == 1) {
                inode = table->root;
        } else {
                inode = (inode_t *) (unsigned long) ino;
                inode_ref (inode);
        }

        return inode;
}

static uint64_t
inode_to_nodeid (inode_t *inode)
{
        if (!inode || inode->ino == 1)
                return 1;

        return (unsigned long) inode;
}


GF_MUST_CHECK static int32_t
fuse_loc_fill (loc_t *loc, fuse_state_t *state, ino_t ino,
               ino_t par, const char *name)
{
        inode_t  *inode = NULL;
        inode_t  *parent = NULL;
        int32_t   ret = -1;
        char     *path = NULL;

        /* resistance against multiple invocation of loc_fill not to get
           reference leaks via inode_search() */

        inode = loc->inode;

        if (name) {
                parent = loc->parent;
                if (!parent) {
                        parent = fuse_ino_to_inode (par, state->itable);
                        loc->parent = parent;
                }

                inode = loc->inode;
                if (!inode) {
                        inode = inode_grep (state->itable, parent, name);
                        loc->inode = inode;
                }

                ret = inode_path (parent, name, &path);
                if (ret <= 0) {
                        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                                "inode_path failed for %"PRId64"/%s",
                                parent->ino, name);
                        goto fail;
                }
                loc->path = path;
        } else {
                inode = loc->inode;
                if (!inode) {
                        inode = fuse_ino_to_inode (ino, state->itable);
                        loc->inode = inode;
                }

                parent = loc->parent;
                if (!parent) {
                        parent = inode_parent (inode, par, name);
                        loc->parent = parent;
                }

                ret = inode_path (inode, NULL, &path);
                if (ret <= 0) {
                        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                                "inode_path failed for %"PRId64,
                                inode->ino);
                        goto fail;
                }
                loc->path = path;
        }

        if (inode)
                loc->ino = inode->ino;

        if (loc->path) {
                loc->name = strrchr (loc->path, '/');
                if (loc->name)
                        loc->name++;
                else
                        loc->name = "";
        }

        if ((ino != 1) && (parent == NULL)) {
                gf_log ("fuse-bridge", GF_LOG_DEBUG,
                        "failed to search parent for %"PRId64"/%s (%"PRId64")",
                        (ino_t)par, name, (ino_t)ino);
                ret = -1;
                goto fail;
        }
        ret = 0;
fail:
        return ret;
}


/* courtesy of folly */
static void
stat2attr (struct stat *st, struct fuse_attr *fa)
{
        fa->ino        = st->st_ino;
        fa->size       = st->st_size;
        fa->blocks     = st->st_blocks;
        fa->atime      = st->st_atime;
        fa->mtime      = st->st_mtime;
        fa->ctime      = st->st_ctime;
        fa->atimensec = ST_ATIM_NSEC (st);
        fa->mtimensec = ST_MTIM_NSEC (st);
        fa->ctimensec = ST_CTIM_NSEC (st);
        fa->mode       = st->st_mode;
        fa->nlink      = st->st_nlink;
        fa->uid        = st->st_uid;
        fa->gid        = st->st_gid;
        fa->rdev       = st->st_rdev;
        fa->blksize    = st->st_blksize;
}


static int
fuse_entry_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                inode_t *inode, struct stat *buf)
{
        fuse_state_t            *state = NULL;
        fuse_in_header_t        *finh = NULL;
        struct fuse_entry_out    feo = {0, };
        fuse_private_t          *priv = NULL;
        inode_t                 *linked_inode = NULL;

        priv = this->private;
        state = frame->root->state;
        finh = state->finh;

        if (!op_ret && state->loc.ino == 1) {
                buf->st_ino = 1;
        }

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": %s() %s => %"PRId64" (%"PRId64")",
                        frame->root->unique, gf_fop_list[frame->root->op],
                        state->loc.path, buf->st_ino, state->loc.ino);

                buf->st_blksize = this->ctx->page_size;
                stat2attr (buf, &feo.attr);

                if (!buf->st_ino) {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                "%"PRIu64": %s() %s returning inode 0",
                                frame->root->unique,
                                gf_fop_list[frame->root->op], state->loc.path);
                }

                linked_inode = inode_link (inode, state->loc.parent,
                                           state->loc.name, buf);

                inode_lookup (linked_inode);

                /* TODO: make these timeouts configurable (via meta?) */
                feo.nodeid = inode_to_nodeid (linked_inode);

                feo.generation = linked_inode->generation;

                inode_unref (linked_inode);

                feo.entry_valid =
                        calc_timeout_sec (priv->entry_timeout);
                feo.entry_valid_nsec =
                        calc_timeout_nsec (priv->entry_timeout);
                feo.attr_valid =
                        calc_timeout_sec (priv->attribute_timeout);
                feo.attr_valid_nsec =
                        calc_timeout_nsec (priv->attribute_timeout);

                priv->proto_minor >= 9 ?
                        send_fuse_obj (this, finh, &feo) :
                        send_fuse_data (this, finh, &feo,
                                        FUSE_COMPAT_ENTRY_OUT_SIZE);
        } else {
                gf_log ("glusterfs-fuse",
                        (op_errno == ENOENT ? GF_LOG_TRACE : GF_LOG_WARNING),
                        "%"PRIu64": %s() %s => -1 (%s)", frame->root->unique,
                        gf_fop_list[frame->root->op], state->loc.path,
                        strerror (op_errno));
                send_fuse_err (this, state->finh, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);
        return 0;
}


static int
fuse_newentry_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   inode_t *inode, struct stat *buf, struct stat *preparent,
                   struct stat *postparent)
{
        fuse_entry_cbk (frame, cookie, this, op_ret, op_errno, inode, buf);
        return 0;
}


static int
fuse_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 inode_t *inode, struct stat *stat, dict_t *dict,
                 struct stat *postparent)
{
        fuse_state_t            *state = NULL;
        call_frame_t            *prev = NULL;

        state = frame->root->state;
        prev  = cookie;

        if (op_ret == -1 && state->is_revalidate == 1) {
                inode_unref (state->loc.inode);
                state->loc.inode = inode_new (state->itable);
                state->is_revalidate = 2;

                STACK_WIND (frame, fuse_lookup_cbk,
                            prev->this, prev->this->fops->lookup,
                            &state->loc, state->dict);
                return 0;
        }

        fuse_entry_cbk (frame, cookie, this, op_ret, op_errno, inode, stat);
        return 0;
}


static void
fuse_lookup (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        char *name = msg;

        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        GET_STATE (this, finh, state);

        ret = fuse_loc_fill (&state->loc, state, 0, finh->nodeid, name);

        if (ret < 0) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": LOOKUP %"PRIu64"/%s (fuse_loc_fill() failed)",
                        finh->unique, finh->nodeid, name);
                free_state (state);
                send_fuse_err (this, finh, ENOENT);
                return;
        }

        if (!state->loc.inode) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": LOOKUP %s", finh->unique,
                        state->loc.path);

                state->loc.inode = inode_new (state->itable);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": LOOKUP %s(%"PRId64")", finh->unique,
                        state->loc.path, state->loc.inode->ino);
                state->is_revalidate = 1;
        }

        state->dict = dict_new ();

        FUSE_FOP (state, fuse_lookup_cbk, GF_FOP_LOOKUP,
                  lookup, &state->loc, state->dict);
}


static void
fuse_forget (xlator_t *this, fuse_in_header_t *finh, void *msg)

{
        struct fuse_forget_in *ffi = msg;

        inode_t      *fuse_inode;

        if (finh->nodeid == 1) {
                FREE (finh);
                return;
        }

        fuse_inode = fuse_ino_to_inode (finh->nodeid, this->itable);

        inode_forget (fuse_inode, ffi->nlookup);
        inode_unref (fuse_inode);

        FREE (finh);
}


static int
fuse_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                   struct stat *postbuf)
{
        fuse_state_t     *state;
        fuse_in_header_t *finh;
        fuse_private_t   *priv = NULL;
        struct fuse_attr_out fao;

        priv  = this->private;
        state = frame->root->state;
        finh  = state->finh;

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": %s() %s => %"PRId64, frame->root->unique,
                        gf_fop_list[frame->root->op],
                        state->loc.path ? state->loc.path : "ERR",
                        prebuf->st_ino);

                /* TODO: make these timeouts configurable via meta */
                /* TODO: what if the inode number has changed by now */
                postbuf->st_blksize = this->ctx->page_size;
                stat2attr (postbuf, &fao.attr);

                fao.attr_valid = calc_timeout_sec (priv->attribute_timeout);
                fao.attr_valid_nsec =
                  calc_timeout_nsec (priv->attribute_timeout);

                priv->proto_minor >= 9 ?
                send_fuse_obj (this, finh, &fao) :
                send_fuse_data (this, finh, &fao,
                                FUSE_COMPAT_ATTR_OUT_SIZE);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": %s() %s => -1 (%s)", frame->root->unique,
                        gf_fop_list[frame->root->op],
                        state->loc.path ? state->loc.path : "ERR",
                        strerror (op_errno));

                if (can_fuse_return (state, 0))
                        send_fuse_err (this, finh, op_errno);
        }

        if (state->callcount == 0) {
                free_state (state);
                STACK_DESTROY (frame->root);
        }

        return 0;
}


static int
fuse_attr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct stat *buf)
{
        fuse_state_t     *state;
        fuse_in_header_t *finh;
        fuse_private_t   *priv = NULL;
        struct fuse_attr_out fao;

        priv  = this->private;
        state = frame->root->state;
        finh  = state->finh;

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": %s() %s => %"PRId64, frame->root->unique,
                        gf_fop_list[frame->root->op],
                        state->loc.path ? state->loc.path : "ERR",
                        buf->st_ino);

                /* TODO: make these timeouts configurable via meta */
                /* TODO: what if the inode number has changed by now */
                buf->st_blksize = this->ctx->page_size;
                stat2attr (buf, &fao.attr);

                fao.attr_valid = calc_timeout_sec (priv->attribute_timeout);
                fao.attr_valid_nsec =
                  calc_timeout_nsec (priv->attribute_timeout);

                priv->proto_minor >= 9 ?
                send_fuse_obj (this, finh, &fao) :
                send_fuse_data (this, finh, &fao,
                                FUSE_COMPAT_ATTR_OUT_SIZE);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": %s() %s => -1 (%s)", frame->root->unique,
                        gf_fop_list[frame->root->op],
                        state->loc.path ? state->loc.path : "ERR",
                        strerror (op_errno));

                if (can_fuse_return (state, 0))
                        send_fuse_err (this, finh, op_errno);
        }

        if (state->callcount == 0) {
                free_state (state);
                STACK_DESTROY (frame->root);
        }

        return 0;
}


static int
fuse_root_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      inode_t *inode, struct stat *stat, dict_t *dict,
                      struct stat *postparent)
{
        fuse_attr_cbk (frame, cookie, this, op_ret, op_errno, stat);

        return 0;
}


static void
fuse_getattr (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        fuse_state_t *state;
        fd_t         *fd = NULL;
        int32_t       ret = -1;

        GET_STATE (this, finh, state);

        if (finh->nodeid == 1) {
                ret = fuse_loc_fill (&state->loc, state, finh->nodeid, 0, NULL);
                if (ret < 0) {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                "%"PRIu64": GETATTR %"PRIu64" (fuse_loc_fill() failed)",
                                finh->unique, finh->nodeid);
                        send_fuse_err (this, finh, ENOENT);
                        free_state (state);
                        return;
                }

                state->dict = dict_new ();

                FUSE_FOP (state, fuse_root_lookup_cbk, GF_FOP_LOOKUP,
                          lookup, &state->loc, state->dict);
                return;
        }

        ret = fuse_loc_fill (&state->loc, state, finh->nodeid, 0, NULL);

        if (!state->loc.inode) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": GETATTR %"PRIu64" (%s) (fuse_loc_fill() returned NULL inode)",
                        finh->unique, finh->nodeid, state->loc.path);
                send_fuse_err (this, finh, ENOENT);
                return;
        }

        fd = fd_lookup (state->loc.inode, finh->pid);
        state->fd = fd;
        if (!fd || S_ISDIR (state->loc.inode->st_mode)) {
                /* this is the @ret of fuse_loc_fill, checked here
                   to permit fstat() to happen even when fuse_loc_fill fails
                */
                if (ret < 0) {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                "%"PRIu64": GETATTR %"PRIu64" (fuse_loc_fill() failed)",
                                finh->unique, finh->nodeid);
                        send_fuse_err (this, finh, ENOENT);
                        free_state (state);
                        return;
                }

                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": GETATTR %"PRIu64" (%s)",
                        finh->unique, finh->nodeid, state->loc.path);


                FUSE_FOP (state, fuse_attr_cbk, GF_FOP_STAT,
                          stat, &state->loc);
        } else {

                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": FGETATTR %"PRIu64" (%s/%p)",
                        finh->unique, finh->nodeid, state->loc.path, fd);

                FUSE_FOP (state,fuse_attr_cbk, GF_FOP_FSTAT,
                          fstat, fd);
        }
}


static int
fuse_fd_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        fuse_state_t          *state;
        fuse_in_header_t      *finh;
        fuse_private_t        *priv = NULL;
        struct fuse_open_out   foo = {0, };

        priv = this->private;
        state = frame->root->state;
        finh = state->finh;

        if (op_ret >= 0) {
                foo.fh = (uintptr_t) fd;
                foo.open_flags = 0;

                if (!S_ISDIR (fd->inode->st_mode)) {
                        if (((state->flags & O_ACCMODE) != O_RDONLY) &&
                            priv->direct_io_mode)
                                foo.open_flags |= FOPEN_DIRECT_IO;
                }

                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": %s() %s => %p", frame->root->unique,
                        gf_fop_list[frame->root->op], state->loc.path, fd);

                fd_ref (fd);
                if (send_fuse_obj (this, finh, &foo) == ENOENT) {
                        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                                "open(%s) got EINTR", state->loc.path);
                        fd_unref (fd);
                                goto out;
                }

                fd_bind (fd);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": %s() %s => -1 (%s)", frame->root->unique,
                        gf_fop_list[frame->root->op], state->loc.path,
                        strerror (op_errno));

                send_fuse_err (this, finh, op_errno);
        }
out:
        free_state (state);
        STACK_DESTROY (frame->root);
        return 0;
}


static int
fuse_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct stat *statpre, struct stat *statpost)
{
        fuse_state_t     *state;
        fuse_in_header_t *finh;
        fuse_private_t   *priv = NULL;
        struct fuse_attr_out fao;

        priv  = this->private;
        state = frame->root->state;
        finh  = state->finh;

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": %s() %s => %"PRId64, frame->root->unique,
                        gf_fop_list[frame->root->op],
                        state->loc.path ? state->loc.path : "ERR",
                        statpost->st_ino);

                /* TODO: make these timeouts configurable via meta */
                /* TODO: what if the inode number has changed by now */

                statpost->st_blksize = this->ctx->page_size;

                stat2attr (statpost, &fao.attr);

                fao.attr_valid = calc_timeout_sec (priv->attribute_timeout);
                fao.attr_valid_nsec =
                  calc_timeout_nsec (priv->attribute_timeout);

                if (can_fuse_return (state, 1)) {
                        priv->proto_minor >= 9 ?
                                send_fuse_obj (this, finh, &fao) :
                                send_fuse_data (this, finh, &fao,
                                                FUSE_COMPAT_ATTR_OUT_SIZE);
                }
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": %s() %s => -1 (%s)", frame->root->unique,
                        gf_fop_list[frame->root->op],
                        state->loc.path ? state->loc.path : "ERR",
                        strerror (op_errno));

                if (can_fuse_return (state, 0))
                        send_fuse_err (this, finh, op_errno);
        }

        if (state->callcount == 0) {
                free_state (state);
                STACK_DESTROY (frame->root);
        }

        return 0;
}


static void
fuse_do_truncate (fuse_state_t *state, struct fuse_setattr_in *fsi)
{
        if (state->fd) {
                FUSE_FOP (state, fuse_truncate_cbk, GF_FOP_FTRUNCATE,
                          ftruncate, state->fd, fsi->size);
        } else {
                FUSE_FOP (state, fuse_truncate_cbk, GF_FOP_TRUNCATE,
                          truncate, &state->loc, fsi->size);
        }

        return;
}


static int32_t
fattr_to_gf_set_attr (int32_t valid)
{
        int32_t gf_valid = 0;

        if (valid & FATTR_MODE)
                gf_valid |= GF_SET_ATTR_MODE;

        if (valid & FATTR_UID)
                gf_valid |= GF_SET_ATTR_UID;

        if (valid & FATTR_GID)
                gf_valid |= GF_SET_ATTR_GID;

        if (valid & FATTR_ATIME)
                gf_valid |= GF_SET_ATTR_ATIME;

        if (valid & FATTR_MTIME)
                gf_valid |= GF_SET_ATTR_MTIME;

        if (valid & FATTR_SIZE)
                gf_valid |= GF_SET_ATTR_SIZE;

        return gf_valid;
}


#define FATTR_MASK   (FATTR_SIZE                        \
                      | FATTR_UID | FATTR_GID           \
                      | FATTR_ATIME | FATTR_MTIME       \
                      | FATTR_MODE)


static void
fuse_setattr (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_setattr_in *fsi = msg;

        struct stat attr = {0, };

        fuse_state_t *state = NULL;
        int32_t       ret   = -1;
        int32_t       valid = 0;

        GET_STATE (this, finh, state);

        ret = fuse_loc_fill (&state->loc, state, finh->nodeid, 0, NULL);

        if ((state->loc.inode == NULL) ||
            (ret < 0)) {

                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": SETATTR %s (fuse_loc_fill() failed)",
                        finh->unique, state->loc.path);

                send_fuse_err (this, finh, ENOENT);
                free_state (state);

                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": SETATTR (%"PRIu64")%s", finh->unique,
                finh->nodeid, state->loc.path);

        valid = fsi->valid;

        if ((fsi->valid & FATTR_SIZE)
            && ((fsi->valid & (FATTR_MASK)) != FATTR_SIZE)) {
                state->callcount = 2;
        }

        if (fsi->valid & FATTR_FH) {
                state->fd = FH_TO_FD (fsi->fh);
        }

        if (fsi->valid & FATTR_SIZE) {
                fuse_do_truncate (state, fsi);
        }

        if ((valid & (FATTR_MASK)) != FATTR_SIZE) {
                attr.st_size  = fsi->size;
                attr.st_atime = fsi->atime;
                attr.st_mtime = fsi->mtime;
                ST_ATIM_NSEC_SET (&attr, fsi->atimensec);
                ST_MTIM_NSEC_SET (&attr, fsi->mtimensec);

                attr.st_mode = fsi->mode;
                attr.st_uid  = fsi->uid;
                attr.st_gid  = fsi->gid;

                if (state->fd &&
                    !((fsi->valid & FATTR_ATIME) || (fsi->valid & FATTR_MTIME))) {

                         /*
                            there is no "futimes" call, so don't send
                            fsetattr if ATIME or MTIME is set
                         */

                        FUSE_FOP (state, fuse_setattr_cbk, GF_FOP_FSETATTR,
                                  fsetattr, state->fd, &attr,
                                  fattr_to_gf_set_attr (fsi->valid));
                } else {
                        FUSE_FOP (state, fuse_setattr_cbk, GF_FOP_SETATTR,
                                  setattr, &state->loc, &attr,
                                  fattr_to_gf_set_attr (fsi->valid));
                }
        }
}


static int gf_fuse_xattr_enotsup_log;
static int
fuse_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                struct stat *postbuf)
{
        fuse_state_t *state = frame->root->state;
        fuse_in_header_t *finh = state->finh;

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": %s() %s => 0", frame->root->unique,
                        gf_fop_list[frame->root->op],
                        state->loc.path ? state->loc.path : "ERR");

                send_fuse_err (this, finh, 0);
        } else {
                if (frame->root->op == GF_FOP_SETXATTR) {
                        op_ret = gf_compat_setxattr (state->dict);
                        if (op_ret == 0)
                                op_errno = 0;
                        if (op_errno == ENOTSUP) {
                                gf_fuse_xattr_enotsup_log++;
                                if (!(gf_fuse_xattr_enotsup_log % GF_UNIVERSAL_ANSWER))
                                        gf_log ("glusterfs-fuse",
                                                GF_LOG_CRITICAL,
                                                "extended attribute not "
                                                "supported by the backend "
                                                "storage");
                        }
                } else {
                        if ((frame->root->op == GF_FOP_REMOVEXATTR)
                            && (op_errno == ENOATTR)) {
                                goto nolog;
                        }
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                "%"PRIu64": %s() %s => -1 (%s)",
                                frame->root->unique,
                                gf_fop_list[frame->root->op],
                                state->loc.path ? state->loc.path : "ERR",
                                strerror (op_errno));
                }
        nolog:

                send_fuse_err (this, finh, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static int
fuse_err_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno)
{
        fuse_state_t *state = frame->root->state;
        fuse_in_header_t *finh = state->finh;

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": %s() %s => 0", frame->root->unique,
                        gf_fop_list[frame->root->op],
                        state->loc.path ? state->loc.path : "ERR");

                send_fuse_err (this, finh, 0);
        } else {
                if (frame->root->op == GF_FOP_SETXATTR) {
                        op_ret = gf_compat_setxattr (state->dict);
                        if (op_ret == 0)
                                op_errno = 0;
                        if (op_errno == ENOTSUP) {
                                gf_fuse_xattr_enotsup_log++;
                                if (!(gf_fuse_xattr_enotsup_log % GF_UNIVERSAL_ANSWER))
                                        gf_log ("glusterfs-fuse",
                                                GF_LOG_CRITICAL,
                                                "extended attribute not "
                                                "supported by the backend "
                                                "storage");
                        }
                } else {
                        if ((frame->root->op == GF_FOP_REMOVEXATTR)
                            && (op_errno == ENOATTR)) {
                                goto nolog;
                        }
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                "%"PRIu64": %s() %s => -1 (%s)",
                                frame->root->unique,
                                gf_fop_list[frame->root->op],
                                state->loc.path ? state->loc.path : "ERR",
                                strerror (op_errno));
                }
        nolog:

                send_fuse_err (this, finh, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static int
fuse_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct stat *preparent,
                 struct stat *postparent)
{
        fuse_state_t     *state = NULL;
        fuse_in_header_t *finh = NULL;

        state = frame->root->state;
        finh = state->finh;

        if (op_ret == 0)
                inode_unlink (state->loc.inode, state->loc.parent,
                              state->loc.name);

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": %s() %s => 0", frame->root->unique,
                        gf_fop_list[frame->root->op], state->loc.path);

                send_fuse_err (this, finh, 0);
        } else {
                gf_log ("glusterfs-fuse",
                        op_errno == ENOTEMPTY ? GF_LOG_DEBUG : GF_LOG_WARNING,
                        "%"PRIu64": %s() %s => -1 (%s)", frame->root->unique,
                        gf_fop_list[frame->root->op], state->loc.path,
                        strerror (op_errno));

                send_fuse_err (this, finh, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_access (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_access_in *fai = msg;

        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        GET_STATE (this, finh, state);

        ret = fuse_loc_fill (&state->loc, state, finh->nodeid, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": ACCESS %"PRIu64" (%s) (fuse_loc_fill() failed)",
                        finh->unique, finh->nodeid, state->loc.path);
                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64" ACCESS %s/%"PRIu64" mask=%d", finh->unique,
                state->loc.path, finh->nodeid, fai->mask);

        FUSE_FOP (state, fuse_err_cbk,
                  GF_FOP_ACCESS, access,
                  &state->loc, fai->mask);

        return;
}


static int
fuse_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, const char *linkname,
                   struct stat *buf)
{
        fuse_state_t     *state = NULL;
        fuse_in_header_t *finh = NULL;

        state = frame->root->state;
        finh = state->finh;

        if (op_ret > 0) {
                ((char *)linkname)[op_ret] = '\0';

                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": %s => %s", frame->root->unique,
                        state->loc.path, linkname);

                send_fuse_data (this, finh, (void *)linkname, op_ret + 1);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": %s => -1 (%s)", frame->root->unique,
                        state->loc.path, strerror (op_errno));

                send_fuse_err (this, finh, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_readlink (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        GET_STATE (this, finh, state);
        ret = fuse_loc_fill (&state->loc, state, finh->nodeid, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64" READLINK %s/%"PRId64" (fuse_loc_fill() returned NULL inode)",
                        finh->unique, state->loc.path,
                        state->loc.inode->ino);
                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64" READLINK %s/%"PRId64, finh->unique,
                state->loc.path, state->loc.inode->ino);

        FUSE_FOP (state, fuse_readlink_cbk, GF_FOP_READLINK,
                  readlink, &state->loc, 4096);

        return;
}


static void
fuse_mknod (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_mknod_in *fmi = msg;
        char         *name = (char *)(fmi + 1);

        fuse_state_t   *state = NULL;
        fuse_private_t *priv = NULL;
        int32_t         ret = -1;

        priv = this->private;
        if (priv->proto_minor < 12)
                name = (char *)msg + FUSE_COMPAT_MKNOD_IN_SIZE;

        GET_STATE (this, finh, state);
        ret = fuse_loc_fill (&state->loc, state, 0, finh->nodeid, name);
        if (ret < 0) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64" MKNOD %s (fuse_loc_fill() failed)",
                        finh->unique, state->loc.path);
                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        state->loc.inode = inode_new (state->itable);

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": MKNOD %s", finh->unique,
                state->loc.path);

        FUSE_FOP (state, fuse_newentry_cbk, GF_FOP_MKNOD,
                  mknod, &state->loc, fmi->mode, fmi->rdev);

        return;
}


static void
fuse_mkdir (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_mkdir_in *fmi = msg;
        char *name = (char *)(fmi + 1);

        fuse_state_t *state;
        int32_t ret = -1;

        GET_STATE (this, finh, state);
        ret = fuse_loc_fill (&state->loc, state, 0, finh->nodeid, name);
        if (ret < 0) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64" MKDIR %s (fuse_loc_fill() failed)",
                        finh->unique, state->loc.path);
                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        state->loc.inode = inode_new (state->itable);

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": MKDIR %s", finh->unique,
                state->loc.path);

        FUSE_FOP (state, fuse_newentry_cbk, GF_FOP_MKDIR,
                  mkdir, &state->loc, fmi->mode);

        return;
}


static void
fuse_unlink (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        char         *name = msg;

        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        GET_STATE (this, finh, state);

        ret = fuse_loc_fill (&state->loc, state, 0, finh->nodeid, name);

        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": UNLINK %s (fuse_loc_fill() returned NULL inode)",
                        finh->unique, state->loc.path);
                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": UNLINK %s", finh->unique,
                state->loc.path);

        FUSE_FOP (state, fuse_unlink_cbk, GF_FOP_UNLINK,
                  unlink, &state->loc);

        return;
}


static void
fuse_rmdir (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        char         *name = msg;

        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        GET_STATE (this, finh, state);
        ret = fuse_loc_fill (&state->loc, state, 0, finh->nodeid, name);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": RMDIR %s (fuse_loc_fill() failed)",
                        finh->unique, state->loc.path);
                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": RMDIR %s", finh->unique,
                state->loc.path);

        FUSE_FOP (state, fuse_unlink_cbk, GF_FOP_RMDIR,
                  rmdir, &state->loc);

        return;
}


static void
fuse_symlink (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        char *name = msg;
        char *linkname = name + strlen (name) + 1;

        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        GET_STATE (this, finh, state);
        ret = fuse_loc_fill (&state->loc, state, 0, finh->nodeid, name);
        if (ret < 0) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64" SYMLINK %s -> %s (fuse_loc_fill() failed)",
                        finh->unique, state->loc.path, linkname);
                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        state->loc.inode = inode_new (state->itable);

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": SYMLINK %s -> %s", finh->unique,
                state->loc.path, linkname);

        FUSE_FOP (state, fuse_newentry_cbk, GF_FOP_SYMLINK,
                  symlink, linkname, &state->loc);

        return;
}


int
fuse_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct stat *buf,
                 struct stat *preoldparent, struct stat *postoldparent,
                 struct stat *prenewparent, struct stat *postnewparent)
{
        fuse_state_t     *state = NULL;
        fuse_in_header_t *finh = NULL;

        state = frame->root->state;
        finh  = state->finh;

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": %s -> %s => 0 (buf->st_ino=%"PRId64" , loc->ino=%"PRId64")",
                        frame->root->unique, state->loc.path, state->loc2.path,
                        buf->st_ino, state->loc.ino);

                {
                        /* ugly ugly - to stay blind to situation where
                           rename happens on a new inode
                        */
                        buf->st_ino = state->loc.ino;
                        buf->st_mode = state->loc.inode->st_mode;
                }
                buf->st_blksize = this->ctx->page_size;

                inode_rename (state->itable,
                              state->loc.parent, state->loc.name,
                              state->loc2.parent, state->loc2.name,
                              state->loc.inode, buf);

                send_fuse_err (this, finh, 0);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": %s -> %s => -1 (%s)", frame->root->unique,
                        state->loc.path, state->loc2.path,
                        strerror (op_errno));
                send_fuse_err (this, finh, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);
        return 0;
}


static void
fuse_rename (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_rename_in  *fri = msg;
        char *oldname = (char *)(fri + 1);
        char *newname = oldname + strlen (oldname) + 1;

        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        GET_STATE (this, finh, state);

        ret = fuse_loc_fill (&state->loc, state, 0, finh->nodeid, oldname);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "for %s %"PRIu64": RENAME `%s' -> `%s' (fuse_loc_fill() failed)",
                        state->loc.path, finh->unique, state->loc.path,
                        state->loc2.path);

                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        ret = fuse_loc_fill (&state->loc2, state, 0, fri->newdir, newname);
        if (ret < 0) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "for %s %"PRIu64": RENAME `%s' -> `%s' (fuse_loc_fill() failed)",
                        state->loc.path, finh->unique, state->loc.path,
                        state->loc2.path);

                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
               }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": RENAME `%s (%"PRId64")' -> `%s (%"PRId64")'",
                finh->unique, state->loc.path, state->loc.ino,
                state->loc2.path, state->loc2.ino);

        FUSE_FOP (state, fuse_rename_cbk, GF_FOP_RENAME,
                  rename, &state->loc, &state->loc2);

        return;
}


static void
fuse_link (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_link_in *fli = msg;
        char         *name = (char *)(fli + 1);

        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        GET_STATE (this, finh, state);

        ret = fuse_loc_fill (&state->loc, state, 0, finh->nodeid, name);
        if (ret == 0)
                ret = fuse_loc_fill (&state->loc2, state, fli->oldnodeid, 0,
                                     NULL);

        if ((state->loc2.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "fuse_loc_fill() failed for %s %"PRIu64": LINK %s %s",
                        state->loc2.path, finh->unique,
                        state->loc2.path, state->loc.path);
                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        state->loc.inode = inode_ref (state->loc2.inode);
        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": LINK() %s (%"PRId64") -> %s (%"PRId64")",
                finh->unique, state->loc2.path, state->loc2.ino,
                state->loc.path, state->loc.ino);

        FUSE_FOP (state, fuse_newentry_cbk, GF_FOP_LINK,
                  link, &state->loc2, &state->loc);

        return;
}


static int
fuse_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 fd_t *fd, inode_t *inode, struct stat *buf,
                 struct stat *preparent, struct stat *postparent)
{
        fuse_state_t            *state = NULL;
        fuse_in_header_t        *finh = NULL;
        fuse_private_t          *priv = NULL;
        struct fuse_out_header   fouh = {0, };
        struct fuse_entry_out    feo = {0, };
        struct fuse_open_out     foo = {0, };
        struct iovec             iov_out[3];
        inode_t                 *linked_inode = NULL;


        state    = frame->root->state;
        priv     = this->private;
        finh     = state->finh;
        foo.open_flags = 0;

        if (op_ret >= 0) {
                foo.fh = (uintptr_t) fd;

                if (((state->flags & O_ACCMODE) != O_RDONLY) &&
                    priv->direct_io_mode)
                        foo.open_flags |= FOPEN_DIRECT_IO;

                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": %s() %s => %p (ino=%"PRId64")",
                        frame->root->unique, gf_fop_list[frame->root->op],
                        state->loc.path, fd, buf->st_ino);

                buf->st_blksize = this->ctx->page_size;
                stat2attr (buf, &feo.attr);

                linked_inode = inode_link (inode, state->loc.parent,
                                           state->loc.name, buf);

                if (linked_inode != inode) {
                        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                                "create(%s) inode (ptr=%p, ino=%"PRId64", "
                                "gen=%"PRId64") found conflict (ptr=%p, "
                                "ino=%"PRId64", gen=%"PRId64")",
                                state->loc.path, inode, inode->ino,
                                inode->generation, linked_inode,
                                linked_inode->ino, linked_inode->generation);

                        /*
                           VERY racy code (if used anywhere else)
                           -- don't do this without understanding
                        */
                        inode_unref (fd->inode);
                        fd->inode = inode_ref (linked_inode);
                }

                inode_lookup (linked_inode);

                inode_unref (linked_inode);

                fd_ref (fd);

                feo.nodeid = inode_to_nodeid (inode);

                feo.generation = inode->generation;

                feo.entry_valid = calc_timeout_sec (priv->entry_timeout);
                feo.entry_valid_nsec = calc_timeout_nsec (priv->entry_timeout);
                feo.attr_valid = calc_timeout_sec (priv->attribute_timeout);
                feo.attr_valid_nsec =
                  calc_timeout_nsec (priv->attribute_timeout);

                fouh.error = 0;
                iov_out[0].iov_base = &fouh;
                iov_out[1].iov_base = &feo;
                iov_out[1].iov_len = priv->proto_minor >= 9 ?
                                     sizeof (feo) :
                                     FUSE_COMPAT_ENTRY_OUT_SIZE;
                iov_out[2].iov_base = &foo;
                iov_out[2].iov_len = sizeof (foo);
                if (send_fuse_iov (this, finh, iov_out, 3) == ENOENT) {
                        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                                "create(%s) got EINTR", state->loc.path);
                        inode_forget (inode, 1);
                        fd_unref (fd);
                        goto out;
                }

                fd_bind (fd);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": %s => -1 (%s)", finh->unique,
                        state->loc.path, strerror (op_errno));
                send_fuse_err (this, finh, op_errno);
        }
out:
        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_create (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_create_in *fci = msg;
        char         *name = (char *)(fci + 1);

        fuse_private_t        *priv = NULL;
        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;
        int32_t       ret = -1;

        priv = this->private;
        if (priv->proto_minor < 12)
                name = (char *)((struct fuse_open_in *)msg + 1);

        GET_STATE (this, finh, state);
        state->flags = fci->flags;

        ret = fuse_loc_fill (&state->loc, state, 0, finh->nodeid, name);
        if (ret < 0) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64" CREATE %s (fuse_loc_fill() failed)",
                        finh->unique, state->loc.path);
                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        state->loc.inode = inode_new (state->itable);

        fd = fd_create (state->loc.inode, finh->pid);
        state->fd = fd;
        fd->flags = state->flags;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": CREATE %s", finh->unique,
                state->loc.path);

        FUSE_FOP (state, fuse_create_cbk, GF_FOP_CREATE,
                  create, &state->loc, state->flags, fci->mode, fd);

        return;
}


static void
fuse_open (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_open_in *foi = msg;

        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;
        int32_t       ret = -1;

        GET_STATE (this, finh, state);
        state->flags = foi->flags;

        ret = fuse_loc_fill (&state->loc, state, finh->nodeid, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": OPEN %s (fuse_loc_fill() failed)",
                        finh->unique, state->loc.path);

                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        fd = fd_create (state->loc.inode, finh->pid);
        state->fd = fd;
        fd->flags = foi->flags;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": OPEN %s", finh->unique,
                state->loc.path);

        FUSE_FOP (state, fuse_fd_cbk, GF_FOP_OPEN,
                  open, &state->loc, foi->flags, fd, 0);

        return;
}


static int
fuse_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                struct iovec *vector, int32_t count,
                struct stat *stbuf, struct iobref *iobref)
{
        fuse_state_t *state = NULL;
        fuse_in_header_t *finh = NULL;
        struct fuse_out_header fouh = {0, };
        struct iovec *iov_out = NULL;

        state = frame->root->state;
        finh = state->finh;

        if (op_ret >= 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": READ => %d/%"GF_PRI_SIZET",%"PRId64"/%"PRId64,
                        frame->root->unique,
                        op_ret, state->size, state->off, stbuf->st_size);

                iov_out = CALLOC (count + 1, sizeof (*iov_out));
                if (iov_out) {
                        fouh.error = 0;
                        iov_out[0].iov_base = &fouh;
                        memcpy (iov_out + 1, vector, count * sizeof (*iov_out));
                        send_fuse_iov (this, finh, iov_out, count + 1);
                        FREE (iov_out);
                } else
                        send_fuse_err (this, finh, ENOMEM);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": READ => %d (%s)", frame->root->unique,
                        op_ret, strerror (op_errno));

                send_fuse_err (this, finh, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_readv (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_read_in *fri = msg;

        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;

        GET_STATE (this, finh, state);
        state->size = fri->size;
        state->off = fri->offset;

        fd = FH_TO_FD (fri->fh);
        state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": READ (%p, size=%"PRIu32", offset=%"PRIu64")",
                finh->unique, fd, fri->size, fri->offset);

        FUSE_FOP (state, fuse_readv_cbk, GF_FOP_READ,
                  readv, fd, fri->size, fri->offset);

}


static int
fuse_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct stat *stbuf, struct stat *postbuf)
{
        fuse_state_t *state = NULL;
        fuse_in_header_t *finh = NULL;
        struct fuse_write_out fwo = {0, };

        state = frame->root->state;
        finh = state->finh;

        if (op_ret >= 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": WRITE => %d/%"GF_PRI_SIZET",%"PRId64"/%"PRId64,
                        frame->root->unique,
                        op_ret, state->size, state->off, stbuf->st_size);

                fwo.size = op_ret;
                send_fuse_obj (this, finh, &fwo);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": WRITE => -1 (%s)", frame->root->unique,
                        strerror (op_errno));

                send_fuse_err (this, finh, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_write (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        /* WRITE is special, metadata is attached to in_header,
         * and msg is the payload as-is.
         */
        struct fuse_write_in *fwi = (struct fuse_write_in *)
                                      (finh + 1);

        fuse_private_t  *priv = NULL;
        fuse_state_t    *state = NULL;
        struct iovec     vector;
        fd_t            *fd = NULL;
        struct iobref   *iobref = NULL;
        struct iobuf    *iobuf = NULL;

        priv = this->private;

        GET_STATE (this, finh, state);
        state->size = fwi->size;
        state->off  = fwi->offset;
        fd          = FH_TO_FD (fwi->fh);
        state->fd   = fd;
        vector.iov_base = msg;
        vector.iov_len  = fwi->size;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": WRITE (%p, size=%"PRIu32", offset=%"PRId64")",
                finh->unique, fd, fwi->size, fwi->offset);

        iobref = iobref_new ();
        if (!iobref) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRIu64": WRITE iobref allocation failed",
                        finh->unique);

                free_state (state);
                return;
        }
        iobuf = ((fuse_private_t *) (state->this->private))->iobuf;
        iobref_add (iobref, iobuf);

        FUSE_FOP (state, fuse_writev_cbk, GF_FOP_WRITE,
                  writev, fd, &vector, 1, fwi->offset, iobref);

        iobref_unref (iobref);
        return;
}


static void
fuse_flush (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_flush_in *ffi = msg;

        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;

        GET_STATE (this, finh, state);
        fd = FH_TO_FD (ffi->fh);
        state->fd = fd;
        if (fd)
                fd->flush_unique = finh->unique;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": FLUSH %p", finh->unique, fd);

        FUSE_FOP (state, fuse_err_cbk, GF_FOP_FLUSH,
                  flush, fd);

        return;
}


static void
fuse_release (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_release_in *fri = msg;

        fd_t     *fd = NULL;
        int do_flush = 0;

        fuse_state_t *state = NULL;

        GET_STATE (this, finh, state);
        fd = FH_TO_FD (fri->fh);
        state->fd = fd;
#ifdef  GF_LINUX_HOST_OS
        /* This is an ugly Linux specific hack, relying on subtle
         * implementation details.
         *
         * The self-heal algorithm of replicate relies on being
         * notified by means of a flush fop whenever a consumer
         * of a file is done with that file. If this happens
         * from userspace by means of close(2) or process termination,
         * the kernel sends us a FLUSH message which we can handle with
         * the flush fop (nb. this mechanism itself is Linux specific!!).
         *
         * However, if it happens from a kernel context, we get no FLUSH,
         * just the final RELEASE when all references to the file are gone.
         * We try to guess that this is the case by checking if the last FLUSH
         * on the file was just the previous message. If not, we conjecture
         * that this release is from a kernel context and call the flush fop
         * here.
         *
         * Note #1: we check the above condition by means of looking at
         * the "unique" values of the FUSE messages, relying on which is
         * a big fat NO NO NO in any sane code.
         *
         * Note #2: there is no guarantee against false positives (in theory
         * it's possible that the scheduler arranges an unrelated FUSE message
         * in between FLUSH and RELEASE, although it seems to be unlikely), but
         * extra flushes are not a problem.
         *
         * Note #3: cf. Bug #223.
         */

        if (fd && fd->flush_unique + 1 != finh->unique)
                do_flush = 1;
#endif

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": RELEASE %p%s", finh->unique, fd,
                do_flush ? " (FLUSH implied)" : "");

        if (do_flush) {
                FUSE_FOP (state, fuse_err_cbk, GF_FOP_FLUSH, flush, fd);
                fd_unref (fd);
        } else {
                fd_unref (fd);

                send_fuse_err (this, finh, 0);

                free_state (state);
        }

        return;
}


static void
fuse_fsync (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_fsync_in *fsi = msg;

        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;

        GET_STATE (this, finh, state);
        fd = FH_TO_FD (fsi->fh);
        state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": FSYNC %p", finh->unique, fd);

        /* fsync_flags: 1 means "datasync" (no defines for this) */
        FUSE_FOP (state, fuse_fsync_cbk, GF_FOP_FSYNC,
                  fsync, fd, fsi->fsync_flags & 1);

        return;
}


static void
fuse_opendir (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        /*
        struct fuse_open_in *foi = msg;
         */

        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;
        int32_t       ret = -1;

        GET_STATE (this, finh, state);
        ret = fuse_loc_fill (&state->loc, state, finh->nodeid, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": OPENDIR %s (fuse_loc_fill() failed)",
                        finh->unique, state->loc.path);

                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        fd = fd_create (state->loc.inode, finh->pid);
        state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": OPENDIR %s", finh->unique,
                state->loc.path);

        FUSE_FOP (state, fuse_fd_cbk, GF_FOP_OPENDIR,
                  opendir, &state->loc, fd);
}


unsigned char
d_type_from_stat (struct stat *buf)
{
        unsigned char d_type;

        if (buf->st_mode & S_IFREG) {
                d_type = DT_REG;

        } else if (buf->st_mode & S_IFDIR) {
                d_type = DT_DIR;

        } else if (buf->st_mode & S_IFIFO) {
                d_type = DT_FIFO;

        } else if (buf->st_mode & S_IFSOCK) {
                d_type = DT_SOCK;

        } else if (buf->st_mode & S_IFCHR) {
                d_type = DT_CHR;

        } else if (buf->st_mode & S_IFBLK) {
                d_type = DT_BLK;

        } else {
                d_type = DT_UNKNOWN;
        }

        return d_type;
}


static int
fuse_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
        fuse_state_t *state = NULL;
        fuse_in_header_t *finh = NULL;
        int           size = 0;
        char         *buf = NULL;
        gf_dirent_t  *entry = NULL;
        struct fuse_dirent *fde = NULL;

        state = frame->root->state;
        finh  = state->finh;

        if (op_ret < 0) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": READDIR => -1 (%s)", frame->root->unique,
                        strerror (op_errno));

                send_fuse_err (this, finh, op_errno);
                goto out;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": READDIR => %d/%"GF_PRI_SIZET",%"PRId64,
                frame->root->unique, op_ret, state->size, state->off);

        list_for_each_entry (entry, &entries->list, list) {
                size += FUSE_DIRENT_ALIGN (FUSE_NAME_OFFSET +
                                           strlen (entry->d_name));
        }

        buf = CALLOC (1, size);
        if (!buf) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRIu64": READDIR => -1 (%s)", frame->root->unique,
                        strerror (ENOMEM));
                send_fuse_err (this, finh, ENOMEM);
                goto out;
        }

        size = 0;
        list_for_each_entry (entry, &entries->list, list) {
                fde = (struct fuse_dirent *)(buf + size);
                fde->ino = entry->d_ino;
                fde->off = entry->d_off;
                fde->type = d_type_from_stat (&entry->d_stat);
                fde->namelen = strlen (entry->d_name);
                strncpy (fde->name, entry->d_name, fde->namelen);
                size += FUSE_DIRENT_SIZE (fde);
        }

        send_fuse_data (this, finh, buf, size);

out:
        free_state (state);
        STACK_DESTROY (frame->root);
        if (buf)
                FREE (buf);
        return 0;

}


static void
fuse_readdir (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_read_in *fri = msg;

        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;

        GET_STATE (this, finh, state);
        state->size = fri->size;
        state->off = fri->offset;
        fd = FH_TO_FD (fri->fh);
        state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": READDIR (%p, size=%"PRIu32", offset=%"PRId64")",
                finh->unique, fd, fri->size, fri->offset);

        FUSE_FOP (state, fuse_readdir_cbk, GF_FOP_READDIR,
                  readdir, fd, fri->size, fri->offset);
}


static void
fuse_releasedir (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_release_in *fri = msg;

        fuse_state_t *state = NULL;

        GET_STATE (this, finh, state);
        state->fd = FH_TO_FD (fri->fh);

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": RELEASEDIR %p", finh->unique, state->fd);

        fd_unref (state->fd);

        send_fuse_err (this, finh, 0);

        free_state (state);

        return;
}


static void
fuse_fsyncdir (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_fsync_in *fsi = msg;

        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;

        fd = FH_TO_FD (fsi->fh);

        GET_STATE (this, finh, state);
        state->fd = fd;

        FUSE_FOP (state, fuse_err_cbk, GF_FOP_FSYNCDIR,
                  fsyncdir, fd, fsi->fsync_flags & 1);

        return;
}


static int
fuse_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct statvfs *buf)
{
        fuse_state_t *state = NULL;
        fuse_in_header_t *finh = NULL;
        fuse_private_t   *priv = NULL;
        struct fuse_statfs_out fso = {{0, }, };

        state = frame->root->state;
        priv  = this->private;
        finh  = state->finh;
        /*
          Filesystems (like ZFS on solaris) reports
          different ->f_frsize and ->f_bsize. Old coreutils
          df tools use statfs() and do not see ->f_frsize.
          the ->f_blocks, ->f_bavail and ->f_bfree are
          w.r.t ->f_frsize and not ->f_bsize which makes the
          df tools report wrong values.

          Scale the block counts to match ->f_bsize.
        */
        /* TODO: with old coreutils, f_bsize is taken from stat()'s st_blksize
         * so the df with old coreutils this wont work :(
         */

        if (op_ret == 0) {
#ifndef GF_DARWIN_HOST_OS
                /* MacFUSE doesn't respect anyof these tweaks */
                buf->f_blocks *= buf->f_frsize;
                buf->f_blocks /= this->ctx->page_size;

                buf->f_bavail *= buf->f_frsize;
                buf->f_bavail /= this->ctx->page_size;

                buf->f_bfree *= buf->f_frsize;
                buf->f_bfree /= this->ctx->page_size;

                buf->f_frsize = buf->f_bsize =this->ctx->page_size;
#endif /* GF_DARWIN_HOST_OS */
                fso.st.bsize   = buf->f_bsize;
                fso.st.frsize  = buf->f_frsize;
                fso.st.blocks  = buf->f_blocks;
                fso.st.bfree   = buf->f_bfree;
                fso.st.bavail  = buf->f_bavail;
                fso.st.files   = buf->f_files;
                fso.st.ffree   = buf->f_ffree;
                fso.st.namelen = buf->f_namemax;

                priv->proto_minor >= 4 ?
                send_fuse_obj (this, finh, &fso) :
                send_fuse_data (this, finh, &fso, FUSE_COMPAT_STATFS_SIZE);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": ERR => -1 (%s)", frame->root->unique,
                        strerror (op_errno));
                send_fuse_err (this, finh, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_statfs (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        GET_STATE (this, finh, state);
        ret = fuse_loc_fill (&state->loc, state, 1, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": STATFS (fuse_loc_fill() fail)",
                        finh->unique);

                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": STATFS", finh->unique);

        FUSE_FOP (state, fuse_statfs_cbk, GF_FOP_STATFS,
                  statfs, &state->loc);
}


static void
fuse_setxattr (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_setxattr_in *fsi = msg;
        char         *name = (char *)(fsi + 1);
        char         *value = name + strlen (name) + 1;

        fuse_state_t *state = NULL;
        char         *dict_value = NULL;
        int32_t       ret = -1;

#ifdef DISABLE_POSIX_ACL
        if (!strncmp (name, "system.", 7)) {
                send_fuse_err (this, finh, EOPNOTSUPP);
                return;
        }
#endif

        GET_STATE (this, finh, state);
        state->size = fsi->size;
        ret = fuse_loc_fill (&state->loc, state, finh->nodeid, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": SETXATTR %s/%"PRIu64" (%s) (fuse_loc_fill() failed)",
                        finh->unique,
                        state->loc.path, finh->nodeid, name);

                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        state->dict = get_new_dict ();
        if (!state->dict) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRIu64": SETXATTR dict allocation failed",
                        finh->unique);

                free_state (state);
                return;
        }

        dict_value = memdup (value, fsi->size);
        dict_set (state->dict, (char *)name,
                  data_from_dynptr ((void *)dict_value, fsi->size));
        dict_ref (state->dict);

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": SETXATTR %s/%"PRIu64" (%s)", finh->unique,
                state->loc.path, finh->nodeid, name);

        FUSE_FOP (state, fuse_err_cbk, GF_FOP_SETXATTR,
                  setxattr, &state->loc, state->dict, fsi->flags);

        return;
}

static void
send_fuse_xattr (xlator_t *this, fuse_in_header_t *finh, const char *value,
                 size_t size, size_t expected)
{
        struct fuse_getxattr_out fgxo;

        /* linux kernel limits the size of xattr value to 64k */
        if (size > GLUSTERFS_XATTR_LEN_MAX)
                send_fuse_err (this, finh, E2BIG);
        else if (expected) {
                /* if callback for getxattr and asks for value */
                if (size > expected)
                        /* reply would be bigger than
                         * what was asked by kernel */
                        send_fuse_err (this, finh, ERANGE);
                else
                        send_fuse_data (this, finh, (void *)value, size);
        } else {
                fgxo.size = size;
                send_fuse_obj (this, finh, &fgxo);
        }
}

static int
fuse_xattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        int             need_to_free_dict = 0;
        char           *value = "";
        fuse_state_t   *state = NULL;
        fuse_in_header_t *finh = NULL;
        int32_t         dummy_ret = 0;
        data_t         *value_data = NULL;
        fuse_private_t *priv = NULL;
        struct stat     st;
        char           *file = NULL;
        int32_t         fd = -1;
        int             ret = -1;
        int32_t         len = 0;
        data_pair_t    *trav = NULL;

        priv  = this->private;
        ret   = op_ret;
        state = frame->root->state;
        finh  = state->finh;
        dummy_ret = 0;

#ifdef GF_DARWIN_HOST_OS
        /* This is needed in MacFuse, where MacOSX Finder needs some specific
         * keys to be supported from FS
         */

        if (state->name) {
                if (!dict) {
                        dict = get_new_dict ();
                        need_to_free_dict = 1;
                }
                dummy_ret = gf_compat_getxattr (state->name, dict);
                if (dummy_ret != -1)
                        ret = dummy_ret;
        } else {
                if (!dict) {
                        dict = get_new_dict ();
                        need_to_free_dict = 1;
                }
                dummy_ret = gf_compat_listxattr (ret, dict, state->size);
                if (dummy_ret != -1)
                        ret = dummy_ret;
        }
#endif /* DARWIN */

        if (ret >= 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": %s() %s => %d", frame->root->unique,
                        gf_fop_list[frame->root->op], state->loc.path, op_ret);

                /* if successful */
                if (state->name) {
                        /* if callback for getxattr */
                        value_data = dict_get (dict, state->name);
                        if (value_data) {
                                ret = value_data->len; /* Don't return the value for '\0' */
                                value = value_data->data;

                                send_fuse_xattr (this, finh, value, ret, state->size);
                                /* if(ret >...)...else if...else */
                        }  else if (!strcmp (state->name, "user.glusterfs-booster-volfile")) {
                                if (!priv->volfile) {
                                        memset (&st, 0, sizeof (st));
                                        fd = fileno (this->ctx->specfp);
                                        ret = fstat (fd, &st);
                                        if (ret != 0) {
                                                gf_log (this->name,
                                                        GF_LOG_ERROR,
                                                        "fstat on fd (%d) failed (%s)", fd, strerror (errno));
                                                send_fuse_err (this, finh, ENODATA);
                                        }

                                        priv->volfile_size = st.st_size;
                                        file = priv->volfile = CALLOC (1, priv->volfile_size);
                                        ret = lseek (fd, 0, SEEK_SET);
                                        while ((ret = read (fd, file, GF_UNIT_KB)) > 0) {
                                                file += ret;
                                        }
                                }

                                send_fuse_xattr (this, finh, priv->volfile,
                                                 priv->volfile_size, state->size);
                                /* if(ret >...)...else if...else */
                        } else if (!strcmp (state->name, "user.glusterfs-booster-path")) {
                                send_fuse_xattr (this, finh, state->loc.path,
                                                 strlen (state->loc.path) + 1, state->size);
                        } else if (!strcmp (state->name, "user.glusterfs-booster-mount")) {
                                send_fuse_xattr (this, finh, priv->mount_point,
                                                 strlen (priv->mount_point) + 1, state->size);
                        } else {
                                send_fuse_err (this, finh, ENODATA);
                        } /* if(value_data)...else */
                } else {
                        /* if callback for listxattr */
                        trav = dict->members_list;
                        while (trav) {
                                len += strlen (trav->key) + 1;
                                trav = trav->next;
                        } /* while(trav) */
                        value = alloca (len + 1);
                        ERR_ABORT (value);
                        len = 0;
                        trav = dict->members_list;
                        while (trav) {
                                strcpy (value + len, trav->key);
                                value[len + strlen (trav->key)] = '\0';
                                len += strlen (trav->key) + 1;
                                trav = trav->next;
                        } /* while(trav) */
                        send_fuse_xattr (this, finh, value, len, state->size);
                } /* if(state->name)...else */
        } else {
                /* if failure - no need to check if listxattr or getxattr */
                if (op_errno != ENODATA) {
                        if (op_errno == ENOTSUP)
                        {
                                gf_fuse_xattr_enotsup_log++;
                                if (!(gf_fuse_xattr_enotsup_log % GF_UNIVERSAL_ANSWER))
                                        gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                                "extended attribute not "
                                                "supported by the backend "
                                                "storage");
                        }
                        else
                        {
                                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                        "%"PRIu64": %s() %s => -1 (%s)",
                                        frame->root->unique,
                                        gf_fop_list[frame->root->op],
                                        state->loc.path, strerror (op_errno));
                        }
                } else {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                "%"PRIu64": %s() %s => -1 (%s)",
                                frame->root->unique,
                                gf_fop_list[frame->root->op], state->loc.path,
                                strerror (op_errno));
                } /* if(op_errno!= ENODATA)...else */

                send_fuse_err (this, finh, op_errno);
        } /* if(op_ret>=0)...else */

        if (need_to_free_dict)
                dict_unref (dict);

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_getxattr (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_getxattr_in *fgxi = msg;
        char         *name = (char *)(fgxi + 1);

        fuse_state_t *state = NULL;
        int32_t       ret = -1;

#ifdef DISABLE_POSIX_ACL
        if (!strncmp (name, "system.", 7)) {
                send_fuse_err (this, finh, ENODATA);
                return;
        }
#endif

        GET_STATE (this, finh, state);
        state->size = fgxi->size;
        state->name = strdup (name);

        ret = fuse_loc_fill (&state->loc, state, finh->nodeid, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": GETXATTR %s/%"PRIu64" (%s) (fuse_loc_fill() failed)",
                        finh->unique, state->loc.path, finh->nodeid, name);

                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": GETXATTR %s/%"PRIu64" (%s)", finh->unique,
                state->loc.path, finh->nodeid, name);

        FUSE_FOP (state, fuse_xattr_cbk, GF_FOP_GETXATTR,
                  getxattr, &state->loc, name);

        return;
}


static void
fuse_listxattr (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_getxattr_in *fgxi = msg;

        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        GET_STATE (this, finh, state);
        state->size = fgxi->size;
        ret = fuse_loc_fill (&state->loc, state, finh->nodeid, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRIu64": LISTXATTR %s/%"PRIu64" (fuse_loc_fill() failed)",
                        finh->unique, state->loc.path, finh->nodeid);

                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": LISTXATTR %s/%"PRIu64, finh->unique,
                state->loc.path, finh->nodeid);

        FUSE_FOP (state, fuse_xattr_cbk, GF_FOP_GETXATTR,
                  getxattr, &state->loc, NULL);

        return;
}


static void
fuse_removexattr (xlator_t *this, fuse_in_header_t *finh, void *msg)

{
        char *name = msg;

        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        GET_STATE (this, finh, state);
        ret = fuse_loc_fill (&state->loc, state, finh->nodeid, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRIu64": REMOVEXATTR %s/%"PRIu64" (%s) (fuse_loc_fill() failed)",
                        finh->unique, state->loc.path, finh->nodeid, name);

                send_fuse_err (this, finh, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": REMOVEXATTR %s/%"PRIu64" (%s)", finh->unique,
                state->loc.path, finh->nodeid, name);

        FUSE_FOP (state, fuse_err_cbk, GF_FOP_REMOVEXATTR,
                  removexattr, &state->loc, name);

        return;
}


static int gf_fuse_lk_enosys_log;

static int
fuse_getlk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct flock *lock)
{
        fuse_state_t *state = NULL;

        state = frame->root->state;
        struct fuse_lk_out flo = {{0, }, };

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": ERR => 0", frame->root->unique);
                flo.lk.type = lock->l_type;
                flo.lk.pid  = lock->l_pid;
                if (lock->l_type == F_UNLCK)
                        flo.lk.start = flo.lk.end = 0;
                else {
                        flo.lk.start = lock->l_start;
                        flo.lk.end = lock->l_len ?
                                     (lock->l_start + lock->l_len - 1) :
                                     OFFSET_MAX;
                }
                send_fuse_obj (this, state->finh, &flo);
        } else {
                if (op_errno == ENOSYS) {
                        gf_fuse_lk_enosys_log++;
                        if (!(gf_fuse_lk_enosys_log % GF_UNIVERSAL_ANSWER)) {
                                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                        "GETLK not supported. loading "
                                        "'features/posix-locks' on server side "
                                        "will add GETLK support.");
                        }
                } else {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                "%"PRIu64": ERR => -1 (%s)",
                                frame->root->unique, strerror (op_errno));
                }
                send_fuse_err (this, state->finh, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_getlk (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_lk_in *fli = msg;

        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;
        struct flock  lock = {0, };

        fd = FH_TO_FD (fli->fh);
        GET_STATE (this, finh, state);
        state->fd = fd;
        convert_fuse_file_lock (&fli->lk, &lock);

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": GETLK %p", finh->unique, fd);

        FUSE_FOP (state, fuse_getlk_cbk, GF_FOP_LK,
                  lk, fd, F_GETLK, &lock);

        return;
}


static int
fuse_setlk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct flock *lock)
{
        fuse_state_t *state = NULL;

        state = frame->root->state;

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRIu64": ERR => 0", frame->root->unique);
                send_fuse_err (this, state->finh, 0);
        } else {
                if (op_errno == ENOSYS) {
                        gf_fuse_lk_enosys_log++;
                        if (!(gf_fuse_lk_enosys_log % GF_UNIVERSAL_ANSWER)) {
                                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                        "SETLK not supported. loading "
                                        "'features/posix-locks' on server side "
                                        "will add SETLK support.");
                        }
                } else  {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                "%"PRIu64": ERR => -1 (%s)",
                                frame->root->unique, strerror (op_errno));
                }

                send_fuse_err (this, state->finh, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_setlk (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_lk_in *fli = msg;

        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;
        struct flock  lock = {0, };

        fd = FH_TO_FD (fli->fh);
        GET_STATE (this, finh, state);
        state->finh = finh;
        state->fd = fd;
        convert_fuse_file_lock (&fli->lk, &lock);

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRIu64": SETLK%s %p", finh->unique,
                finh->opcode == FUSE_SETLK ? "" : "W", fd);

        FUSE_FOP (state, fuse_setlk_cbk, GF_FOP_LK,
                  lk, fd, finh->opcode == FUSE_SETLK ? F_SETLK : F_SETLKW,
                  &lock);

        return;
}


static void
fuse_init (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        struct fuse_init_in *fini = msg;

        struct fuse_init_out fino;
        fuse_private_t *priv = NULL;
        int ret;

        priv = this->private;

        if (!priv->first_call) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "got INIT after first message");

                close (priv->fd);
                goto out;
        }

        if (fini->major != FUSE_KERNEL_VERSION) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "unsupported FUSE protocol version %d.%d",
                        fini->major, fini->minor);

                close (priv->fd);
                goto out;
        }
        priv->proto_minor = fini->minor;

        fino.major = FUSE_KERNEL_VERSION;
        fino.minor = FUSE_KERNEL_MINOR_VERSION;
        fino.max_readahead = 1 << 17;
        fino.max_write = 1 << 17;
        fino.flags = FUSE_ASYNC_READ | FUSE_POSIX_LOCKS;
        if (fini->minor >= 6 /* fuse_init_in has flags */ &&
            fini->flags & FUSE_BIG_WRITES) {
                /* no need for direct I/O mode if big writes are supported */
                priv->direct_io_mode = 0;
                fino.flags |= FUSE_BIG_WRITES;
        }
        if (fini->minor < 9)
                *priv->msg0_len_p = sizeof(*finh) + FUSE_COMPAT_WRITE_IN_SIZE;

        ret = send_fuse_obj (this, finh, &fino);
        if (ret == 0)
                gf_log ("glusterfs-fuse", GF_LOG_INFO,
                        "FUSE inited with protocol versions:"
                        " glusterfs %d.%d kernel %d.%d",
                        FUSE_KERNEL_VERSION, FUSE_KERNEL_MINOR_VERSION,
                        fini->major, fini->minor);
        else {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "FUSE init failed (%s)", strerror (ret));

                close (priv->fd);
        }

 out:
        FREE (finh);
}


static void
fuse_enosys (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        send_fuse_err (this, finh, ENOSYS);

        FREE (finh);
}


static void
fuse_discard (xlator_t *this, fuse_in_header_t *finh, void *msg)
{
        FREE (finh);
}

static fuse_handler_t *fuse_ops[FUSE_712_OP_HIGH];

int
fuse_first_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       inode_t *inode, struct stat *buf, dict_t *xattr,
                       struct stat *postparent)
{
        fuse_private_t *priv = NULL;

        priv = this->private;

        if (op_ret == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "first lookup on root succeeded.");
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "first lookup on root failed.");
        }
        STACK_DESTROY (frame->root);
        pthread_mutex_lock (&priv->first_call_mutex);
        {
                priv->first_call = 0;
                pthread_cond_broadcast (&priv->first_call_cond);
        }
        pthread_mutex_unlock (&priv->first_call_mutex);
        return 0;
}


int
fuse_first_lookup (xlator_t *this)
{
        fuse_private_t *priv = NULL;
        loc_t           loc;
        call_frame_t   *frame = NULL;
        xlator_t       *xl = NULL;
        dict_t         *dict = NULL;

        priv = this->private;

        pthread_cond_init (&priv->first_call_cond, NULL);
        pthread_mutex_init (&priv->first_call_mutex, NULL);

        loc.path = "/";
        loc.name = "";
        loc.ino = 1;
        loc.inode = fuse_ino_to_inode (1, this->itable);
        loc.parent = NULL;

        dict = dict_new ();
        frame = create_frame (this, this->ctx->pool);
        frame->root->type = GF_OP_TYPE_FOP_REQUEST;
        xl = this->children->xlator;

        STACK_WIND (frame, fuse_first_lookup_cbk, xl, xl->fops->lookup,
                    &loc, dict);
        dict_unref (dict);

        pthread_mutex_lock (&priv->first_call_mutex);
        {
                while (priv->first_call) {
                        pthread_cond_wait (&priv->first_call_cond,
                                           &priv->first_call_mutex);
                }
        }
        pthread_mutex_unlock (&priv->first_call_mutex);

        return 0;
}



static void *
fuse_thread_proc (void *data)
{
        char           *mount_point = NULL;
        xlator_t       *this = NULL;
        fuse_private_t *priv = NULL;
        ssize_t         res = 0;
        struct iobuf   *iobuf = NULL;
        fuse_in_header_t *finh;
        struct iovec iov_in[2];
        void *msg = NULL;
        const size_t msg0_size = sizeof (*finh) + 128;

        this = data;
        priv = this->private;

        THIS = this;

        iov_in[0].iov_len = sizeof (*finh) + sizeof (struct fuse_write_in);
        iov_in[1].iov_len = ((struct iobuf_pool *)this->ctx->iobuf_pool)
                              ->page_size;
        priv->msg0_len_p = &iov_in[0].iov_len;

        for (;;) {
                iobuf = iobuf_get (this->ctx->iobuf_pool);
                /* Add extra 128 byte to the first iov so that it can
                 * accomodate "ordinary" non-write requests. It's not
                 * guaranteed to be big enough, as SETXATTR and namespace
                 * operations with very long names may grow behind it,
                 * but it's good enough in most cases (and we can handle
                 * rest via realloc).
                 */
                iov_in[0].iov_base = CALLOC (1, msg0_size);

                if (!iobuf || !iov_in[0].iov_base) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Out of memory");
                        if (iobuf)
                                iobuf_unref (iobuf);
                        FREE (iov_in[0].iov_base);
                        sleep (10);
                        continue;
                }

                iov_in[1].iov_base = iobuf->ptr;

                res = readv (priv->fd, iov_in, 2);

                if (priv->first_call) {
                        if (priv->first_call > 1) {
                                priv->first_call--;
                        } else {
                                fuse_first_lookup (this);
                        }
                }

                if (res == -1) {
                        if (errno == ENODEV || errno == EBADF) {
                                gf_log ("glusterfs-fuse", GF_LOG_NORMAL,
                                        "terminating upon getting %s when "
                                        "reading /dev/fuse",
                                        errno == ENODEV ? "ENODEV" : "EBADF");

                                break;
                        }
                        if (errno != EINTR) {
                                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                        "read from /dev/fuse returned -1 (%s)",
                                        strerror (errno));
                        }

                        goto cont_err;
                }
                if (res < sizeof (finh)) {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING, "short read on /dev/fuse");
                        break;
                }

                finh = (fuse_in_header_t *)iov_in[0].iov_base;
                if (res != finh->len) {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING, "inconsistent read on /dev/fuse");
                        break;
                }

                priv->iobuf = iobuf;

                if (finh->opcode == FUSE_WRITE)
                        msg = iov_in[1].iov_base;
                else {
                        if (res > msg0_size) {
                                iov_in[0].iov_base =
                                  realloc (iov_in[0].iov_base, res);
                                if (iov_in[0].iov_base)
                                        finh = (fuse_in_header_t *)
                                                 iov_in[0].iov_base;
                                else {
                                        gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                                "Out of memory");
                                        send_fuse_err (this, finh, ENOMEM);

                                        goto cont_err;
                                }
                        }

                        if (res > iov_in[0].iov_len)
                                memcpy (iov_in[0].iov_base + iov_in[0].iov_len,
                                        iov_in[1].iov_base,
                                        res - iov_in[0].iov_len);

                        msg = finh + 1;
                }
                fuse_ops[finh->opcode] (this, finh, msg);

                iobuf_unref (iobuf);
                continue;

 cont_err:
                iobuf_unref (iobuf);
                FREE (iov_in[0].iov_base);
        }

        iobuf_unref (iobuf);
        FREE (iov_in[0].iov_base);

        if (dict_get (this->options, ZR_MOUNTPOINT_OPT))
                mount_point = data_to_str (dict_get (this->options,
                                                     ZR_MOUNTPOINT_OPT));
        if (mount_point) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "unmounting %s", mount_point);
                dict_del (this->options, ZR_MOUNTPOINT_OPT);
        }

        raise (SIGTERM);

        return NULL;
}

int32_t
fuse_itable_dump (xlator_t  *this)
{
        if (!this)
                 return -1;

        gf_proc_dump_add_section("xlator.mount.fuse.itable");
        inode_table_dump(this->itable, "xlator.mount.fuse.itable");

	return 0;
}

int32_t
fuse_priv_dump (xlator_t  *this)
{
        fuse_private_t  *private = NULL;

        if (!this)
                return -1;

        private = this->private;

        if (!private)
                return -1;

        gf_proc_dump_add_section("xlator.mount.fuse.priv");

        gf_proc_dump_write("xlator.mount.fuse.priv.fd", "%d", private->fd);
        gf_proc_dump_write("xlator.mount.fuse.priv.proto_minor", "%u",
                            private->proto_minor);
        gf_proc_dump_write("xlator.mount.fuse.priv.volfile", "%s",
                            private->volfile?private->volfile:"None");
        gf_proc_dump_write("xlator.mount.fuse.volfile_size", "%d",
                            private->volfile_size);
        gf_proc_dump_write("xlator.mount.fuse.mount_point", "%s",
                            private->mount_point);
        gf_proc_dump_write("xlator.mount.fuse.iobuf", "%u",
                            private->iobuf);
        gf_proc_dump_write("xlator.mount.fuse.fuse_thread_started", "%d",
                            (int)private->fuse_thread_started);
        gf_proc_dump_write("xlator.mount.fuse.direct_io_mode", "%d",
                            private->direct_io_mode);
        gf_proc_dump_write("xlator.mount.fuse.entry_timeout", "%lf",
                            private->entry_timeout);
        gf_proc_dump_write("xlator.mount.fuse.entry_timeout", "%lf",
                            private->attribute_timeout);
        gf_proc_dump_write("xlator.mount.fuse.first_call", "%d",
                            (int)private->first_call);
        gf_proc_dump_write("xlator.mount.fuse.strict_volfile_check", "%d",
                            (int)private->strict_volfile_check);

        return 0;
}


int32_t
notify (xlator_t *this, int32_t event, void *data, ...)
{
        int32_t         ret     = 0;
        fuse_private_t *private = NULL;

        private = this->private;

        switch (event)
        {
        case GF_EVENT_CHILD_UP:
        case GF_EVENT_CHILD_CONNECTING:
        {
                if (!private->fuse_thread_started)
                {
                        private->fuse_thread_started = 1;

                        ret = pthread_create (&private->fuse_thread, NULL,
                                              fuse_thread_proc, this);

                        if (ret != 0) {
                                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                                        "pthread_create() failed (%s)",
                                        strerror (errno));

                                /* If fuse thread is not started, that means,
                                   its hung, we can't use this process. */
                                raise (SIGTERM);
                        }
                }
                break;
        }

        case GF_EVENT_PARENT_UP:
        {
                default_notify (this, GF_EVENT_PARENT_UP, data);
                break;
        }
        case GF_EVENT_VOLFILE_MODIFIED:
        {
                gf_log ("fuse", GF_LOG_CRITICAL,
                        "Remote volume file changed, try re-mounting.");
                if (private->strict_volfile_check) {
                        //fuse_session_remove_chan (private->ch);
                        //fuse_session_destroy (private->se);
                        //fuse_unmount (private->mount_point, private->ch);
                        /* TODO: Above code if works, will be a cleaner way,
                           but for now, lets just achieve what we want */
                        raise (SIGTERM);
                }
                break;
        }
        default:
                break;
        }
        return 0;
}


int
init (xlator_t *this_xl)
{
        int                ret = 0;
        dict_t            *options = NULL;
        char              *value_string = NULL;
        char              *fsname = NULL;
        fuse_private_t    *priv = NULL;
        struct stat        stbuf = {0,};
        int                i = 0;
        int                xl_name_allocated = 0;

        if (this_xl == NULL)
                return -1;

        if (this_xl->options == NULL)
                return -1;

        options = this_xl->options;

        if (this_xl->name == NULL) {
                this_xl->name = strdup ("fuse");
                if (!this_xl->name) {
                        gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                "Out of memory");

                        goto cleanup_exit;
                }
                xl_name_allocated = 1;
        }

        priv = CALLOC (1, sizeof (*priv));
        if (!priv) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "Out of memory");

                goto cleanup_exit;
        }
        this_xl->private = (void *) priv;
        priv->mount_point = NULL;
        priv->fd = -1;

        /* get options from option dictionary */
        ret = dict_get_str (options, ZR_MOUNTPOINT_OPT, &value_string);
        if (value_string == NULL) {
                gf_log ("fuse", GF_LOG_ERROR,
                        "Mandatory option 'mountpoint' is not specified.");
                goto cleanup_exit;
        }

        if (stat (value_string, &stbuf) != 0) {
                if (errno == ENOENT) {
                        gf_log (this_xl->name, GF_LOG_ERROR,
                                "%s %s does not exist",
                                ZR_MOUNTPOINT_OPT, value_string);
                } else if (errno == ENOTCONN) {
                        gf_log (this_xl->name, GF_LOG_ERROR,
                                "Mountpoint %s seems to have a stale "
                                "mount, run 'umount %s' and try again.",
                                value_string, value_string);
                } else {
                        gf_log (this_xl->name, GF_LOG_DEBUG,
                                "%s %s : stat returned %s",
                                ZR_MOUNTPOINT_OPT,
                                value_string, strerror (errno));
                }
                goto cleanup_exit;
        }

        if (S_ISDIR (stbuf.st_mode) == 0) {
                gf_log (this_xl->name, GF_LOG_ERROR,
                        "%s %s is not a directory",
                        ZR_MOUNTPOINT_OPT, value_string);
                goto cleanup_exit;
        }
        priv->mount_point = strdup (value_string);
        if (!priv->mount_point) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "Out of memory");

                goto cleanup_exit;
        }

        ret = dict_get_double (options, "attribute-timeout",
                               &priv->attribute_timeout);
        if (!priv->attribute_timeout)
                priv->attribute_timeout = 1.0; /* default */

        ret = dict_get_double (options, "entry-timeout",
                               &priv->entry_timeout);
        if (!priv->entry_timeout)
                priv->entry_timeout = 1.0; /* default */


        priv->direct_io_mode = 1;
        ret = dict_get_str (options, ZR_DIRECT_IO_OPT, &value_string);
        if (value_string) {
                ret = gf_string2boolean (value_string, &priv->direct_io_mode);
        }

        priv->strict_volfile_check = 0;
        ret = dict_get_str (options, ZR_STRICT_VOLFILE_CHECK, &value_string);
        if (value_string) {
                ret = gf_string2boolean (value_string,
                                         &priv->strict_volfile_check);
        }

        fsname = this_xl->ctx->cmd_args.volume_file;
        fsname = (fsname ? fsname : this_xl->ctx->cmd_args.volfile_server);
        fsname = (fsname ? fsname : "glusterfs");

        this_xl->itable = inode_table_new (0, this_xl);
        if (!this_xl->itable) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "Out of memory");

                goto cleanup_exit;
        }

        priv->fd = gf_fuse_mount (priv->mount_point, fsname,
                                  "allow_other,default_permissions,"
                                  "max_read=131072");
        if (priv->fd == -1)
                goto cleanup_exit;

        this_xl->ctx->top = this_xl;

        priv->first_call = 2;

        for (i = 0; i < FUSE_712_OP_HIGH; i++)
                fuse_ops[i] = fuse_enosys;
        fuse_ops[FUSE_INIT]        = fuse_init;
        fuse_ops[FUSE_DESTROY]     = fuse_discard;
        fuse_ops[FUSE_LOOKUP]      = fuse_lookup;
        fuse_ops[FUSE_FORGET]      = fuse_forget;
        fuse_ops[FUSE_GETATTR]     = fuse_getattr;
        fuse_ops[FUSE_SETATTR]     = fuse_setattr;
        fuse_ops[FUSE_OPENDIR]     = fuse_opendir;
        fuse_ops[FUSE_READDIR]     = fuse_readdir;
        fuse_ops[FUSE_RELEASEDIR]  = fuse_releasedir;
        fuse_ops[FUSE_ACCESS]      = fuse_access;
        fuse_ops[FUSE_READLINK]    = fuse_readlink;
        fuse_ops[FUSE_MKNOD]       = fuse_mknod;
        fuse_ops[FUSE_MKDIR]       = fuse_mkdir;
        fuse_ops[FUSE_UNLINK]      = fuse_unlink;
        fuse_ops[FUSE_RMDIR]       = fuse_rmdir;
        fuse_ops[FUSE_SYMLINK]     = fuse_symlink;
        fuse_ops[FUSE_RENAME]      = fuse_rename;
        fuse_ops[FUSE_LINK]        = fuse_link;
        fuse_ops[FUSE_CREATE]      = fuse_create;
        fuse_ops[FUSE_OPEN]        = fuse_open;
        fuse_ops[FUSE_READ]        = fuse_readv;
        fuse_ops[FUSE_WRITE]       = fuse_write;
        fuse_ops[FUSE_FLUSH]       = fuse_flush;
        fuse_ops[FUSE_RELEASE]     = fuse_release;
        fuse_ops[FUSE_FSYNC]       = fuse_fsync;
        fuse_ops[FUSE_FSYNCDIR]    = fuse_fsyncdir;
        fuse_ops[FUSE_STATFS]      = fuse_statfs;
        fuse_ops[FUSE_SETXATTR]    = fuse_setxattr;
        fuse_ops[FUSE_GETXATTR]    = fuse_getxattr;
        fuse_ops[FUSE_LISTXATTR]   = fuse_listxattr;
        fuse_ops[FUSE_REMOVEXATTR] = fuse_removexattr;
        fuse_ops[FUSE_GETLK]       = fuse_getlk;
        fuse_ops[FUSE_SETLK]       = fuse_setlk;
        fuse_ops[FUSE_SETLKW]      = fuse_setlk;

        return 0;

cleanup_exit:
        if (xl_name_allocated)
                FREE (this_xl->name);
        if (priv) {
                FREE (priv->mount_point);
                close (priv->fd);
        }
        FREE (priv);
        return -1;
}


void
fini (xlator_t *this_xl)
{
        fuse_private_t *priv = NULL;
        char *mount_point = NULL;

        if (this_xl == NULL)
                return;

        if ((priv = this_xl->private) == NULL)
                return;

        if (dict_get (this_xl->options, ZR_MOUNTPOINT_OPT))
                mount_point = data_to_str (dict_get (this_xl->options,
                                                     ZR_MOUNTPOINT_OPT));
        if (mount_point != NULL) {
                gf_log (this_xl->name, GF_LOG_NORMAL,
                        "Unmounting '%s'.", mount_point);

                dict_del (this_xl->options, ZR_MOUNTPOINT_OPT);
                gf_fuse_unmount (mount_point, priv->fd);
        }
}

struct xlator_fops fops = {
};

struct xlator_cbks cbks = {
};

struct xlator_mops mops = {
};

struct xlator_dumpops dumpops = {
        .priv  = fuse_priv_dump,
        .inode = fuse_itable_dump,
};

struct volume_options options[] = {
        { .key  = {"direct-io-mode"},
          .type = GF_OPTION_TYPE_BOOL
        },
        { .key  = {"macfuse-local"},
          .type = GF_OPTION_TYPE_BOOL
        },
        { .key  = {"mountpoint", "mount-point"},
          .type = GF_OPTION_TYPE_PATH
        },
        { .key  = {"attribute-timeout"},
          .type = GF_OPTION_TYPE_DOUBLE
        },
        { .key  = {"entry-timeout"},
          .type = GF_OPTION_TYPE_DOUBLE
        },
        { .key  = {"strict-volfile-check"},
          .type = GF_OPTION_TYPE_BOOL
        },
        { .key = {NULL} },
};
