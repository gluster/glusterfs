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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif /* _CONFIG_H */

#include "glusterfs.h"
#include "logging.h"
#include "xlator.h"
#include "glusterfs.h"
#include "defaults.h"
#include "common-utils.h"

#include <fuse/fuse_lowlevel.h>

#include "fuse-extra.h"
#include "list.h"
#include "dict.h"

#include "compat.h"
#include "compat-errno.h"

#include <sys/time.h>

/* TODO: when supporting posix acl, remove this definition */
#define DISABLE_POSIX_ACL

#define ZR_MOUNTPOINT_OPT       "mountpoint"
#define ZR_DIRECT_IO_OPT        "direct-io-mode"
#define ZR_STRICT_VOLFILE_CHECK "strict-volfile-check"
#define MAX_FUSE_PROC_DELAY 1

struct fuse_private {
        int                  fd;
        struct fuse         *fuse;
        struct fuse_session *se;
        struct fuse_chan    *ch;
        char                *volfile;
        size_t               volfile_size;
        char                *mount_point;
        struct iobuf        *iobuf;
        pthread_t            fuse_thread;
        char                 fuse_thread_started;
        uint32_t             direct_io_mode;
        double               entry_timeout;
        double               attribute_timeout;
        pthread_cond_t       first_call_cond;
        pthread_mutex_t      first_call_mutex;
        char                 first_call;
        gf_boolean_t         strict_volfile_check;
        pthread_cond_t       child_up_cond;
        pthread_mutex_t      child_up_mutex;
        char                 child_up_value;
};
typedef struct fuse_private fuse_private_t;

#define _FI_TO_FD(fi) ((fd_t *)((long)fi->fh))

#define FI_TO_FD(fi) ((_FI_TO_FD (fi))?(fd_ref (_FI_TO_FD(fi))):((fd_t *) 0))

#define FUSE_FOP(state, ret, op_num, fop, args ...)                     \
        do {                                                            \
                call_frame_t *frame = get_call_frame_for_req (state, 1); \
                xlator_t *xl = frame->this->children ?                  \
                        frame->this->children->xlator : NULL;           \
                frame->root->state = state;                             \
                frame->root->op   = op_num;                                \
                STACK_WIND (frame, ret, xl, xl->fops->fop, args);       \
        } while (0)

#define GF_SELECT_LOG_LEVEL(_errno)                     \
        (((_errno == ENOENT) || (_errno == ESTALE))?    \
         GF_LOG_DEBUG)

typedef struct {
        void          *pool;
        xlator_t      *this;
        inode_table_t *itable;
        loc_t          loc;
        loc_t          loc2;
        fuse_req_t     req;
        int32_t        flags;
        off_t          off;
        size_t         size;
        unsigned long  nlookup;
        fd_t          *fd;
        dict_t        *dict;
        char          *name;
        char           is_revalidate;
} fuse_state_t;

int fuse_chan_receive (struct fuse_chan *ch, char *buf, int32_t size);


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
#ifdef DEBUG
        memset (state, 0x90, sizeof (*state));
#endif
        FREE (state);
        state = NULL;
}


fuse_state_t *
state_from_req (fuse_req_t req)
{
        fuse_state_t *state = NULL;
        xlator_t     *this = NULL;

        this = fuse_req_userdata (req);

        state = (void *)calloc (1, sizeof (*state));
        if (!state)
                return NULL;
        state->pool = this->ctx->pool;
        state->itable = this->itable;
        state->req = req;
        state->this = this;

        return state;
}


static pid_t
get_pid_from_req (fuse_req_t req)
{
        const struct fuse_ctx *ctx = NULL;

        ctx = fuse_req_ctx (req);
        return ctx->pid;
}


static call_frame_t *
get_call_frame_for_req (fuse_state_t *state, char d)
{
        call_pool_t           *pool = NULL;
        fuse_req_t             req = NULL;
        const struct fuse_ctx *ctx = NULL;
        call_frame_t          *frame = NULL;
        xlator_t              *this = NULL;
        fuse_private_t        *priv = NULL;

        pool = state->pool;
        req  = state->req;

        if (req) {
                this = fuse_req_userdata (req);
        } else {
                this = state->this;
        }
        priv = this->private;

        frame = create_frame (this, pool);

        if (req) {
                ctx = fuse_req_ctx (req);

                frame->root->uid    = ctx->uid;
                frame->root->gid    = ctx->gid;
                frame->root->pid    = ctx->pid;
                frame->root->unique = req_callid (req);
        }

        frame->root->type = GF_OP_TYPE_FOP_REQUEST;

        return frame;
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

        if (!inode) {
                if (ino)
                        inode = inode_search (state->itable, ino, NULL);
                if (par && name)
                        inode = inode_search (state->itable, par, name);

                loc->inode = inode;
                if (inode)
                        loc->ino = inode->ino;
        }

        parent = loc->parent;
        if (!parent) {
                if (inode)
                        parent = inode_parent (inode, par, name);
                else
                        parent = inode_search (state->itable, par, NULL);
                loc->parent = parent;
        }

        if (name && parent) {
                ret = inode_path (parent, name, &path);
                if (ret <= 0) {
                        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                                "inode_path failed for %"PRId64"/%s",
                                parent->ino, name);
                        goto fail;
                } else {
                        loc->path = path;
                }
        } else         if (inode) {
                ret = inode_path (inode, NULL, &path);
                if (ret <= 0) {
                        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                                "inode_path failed for %"PRId64,
                                inode->ino);
                        goto fail;
                } else {
                        loc->path = path;
                }
        }
        if (loc->path) {
                loc->name = strrchr (loc->path, '/');
                if (loc->name)
                        loc->name++;
                else loc->name = "";
        }

        if ((ino != 1) &&
            (parent == NULL)) {
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


static int
need_fresh_lookup (int32_t op_ret, int32_t op_errno,
                   loc_t *loc, struct stat *buf)
{
        if (op_ret == -1) {
                gf_log ("fuse-bridge", GF_LOG_DEBUG,
                        "revalidate of %s failed (%s)",
                        loc->path, strerror (op_errno));
                return 1;
        }

        if (loc->inode->ino != buf->st_ino) {
                gf_log ("fuse-bridge", GF_LOG_DEBUG,
                        "inode num of %s changed %"PRId64" -> %"PRId64,
                        loc->path, loc->inode->ino, buf->st_ino);
                return 1;
        }

        if ((loc->inode->st_mode & S_IFMT) ^ (buf->st_mode & S_IFMT)) {
                gf_log ("fuse-bridge", GF_LOG_DEBUG,
                        "inode mode of %s changed 0%o -> 0%o",
                        loc->path, loc->inode->st_mode, buf->st_mode);
                return 1;
        }

        return 0;
}


static int
fuse_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 inode_t *inode, struct stat *stat, dict_t *dict);

static int
fuse_entry_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                inode_t *inode, struct stat *buf)
{
        fuse_state_t            *state = NULL;
        fuse_req_t               req = NULL;
        struct fuse_entry_param  e = {0, };
        fuse_private_t          *priv = NULL;

        priv = this->private;
        state = frame->root->state;
        req = state->req;

        if (!op_ret && state->loc.ino == 1) {
                buf->st_ino = 1;
        }

        if (state->is_revalidate == 1
            && need_fresh_lookup (op_ret, op_errno, &state->loc, buf)) {
                inode_unref (state->loc.inode);
                state->loc.inode = inode_new (state->itable);
                state->is_revalidate = 2;

                STACK_WIND (frame, fuse_lookup_cbk,
                            FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->lookup,
                            &state->loc, state->dict);

                return 0;
        }

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": %s() %s => %"PRId64" (%"PRId64")",
                        frame->root->unique, gf_fop_list[frame->root->op],
                        state->loc.path, buf->st_ino, state->loc.ino);

                inode_link (inode, state->loc.parent, state->loc.name, buf);

                inode_lookup (inode);

                /* TODO: make these timeouts configurable (via meta?) */
                e.ino = inode->ino;

#ifdef GF_DARWIN_HOST_OS
                e.generation = 0;
#else
                e.generation = buf->st_ctime;
#endif

                buf->st_blksize = this->ctx->page_size;
                e.entry_timeout = priv->entry_timeout;
                e.attr_timeout  = priv->attribute_timeout;
                e.attr = *buf;

                if (!e.ino || !buf->st_ino) {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                "%"PRId64": %s() %s returning inode 0",
                                frame->root->unique,
                                gf_fop_list[frame->root->op], state->loc.path);
                }

                if (state->loc.parent)
                        fuse_reply_entry (req, &e);
                else
                        fuse_reply_attr (req, buf, priv->attribute_timeout);
        } else {
                gf_log ("glusterfs-fuse",
                        (op_errno == ENOENT ? GF_LOG_TRACE : GF_LOG_WARNING),
                        "%"PRId64": %s() %s => -1 (%s)", frame->root->unique,
                        gf_fop_list[frame->root->op], state->loc.path,
                        strerror (op_errno));
                fuse_reply_err (req, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);
        return 0;
}


static int
fuse_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 inode_t *inode, struct stat *stat, dict_t *dict)
{
        fuse_entry_cbk (frame, cookie, this, op_ret, op_errno, inode, stat);
        return 0;
}


static void
fuse_lookup (fuse_req_t req, fuse_ino_t par, const char *name)
{
        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);

        ret = fuse_loc_fill (&state->loc, state, 0, par, name);

        if (ret < 0) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": LOOKUP %"PRId64"/%s (fuse_loc_fill() failed)",
                        req_callid (req), (ino_t)par, name);
                free_state (state);
                fuse_reply_err (req, ENOENT);
                return;
        }

        if (!state->loc.inode) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": LOOKUP %s", req_callid (req),
                        state->loc.path);

                state->loc.inode = inode_new (state->itable);
                /* to differntiate in entry_cbk what kind of call it is */
                state->is_revalidate = -1;
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": LOOKUP %s(%"PRId64")", req_callid (req),
                        state->loc.path, state->loc.inode->ino);
                state->is_revalidate = 1;
        }

        state->dict = dict_new ();

        FUSE_FOP (state, fuse_lookup_cbk, GF_FOP_LOOKUP,
                  lookup, &state->loc, state->dict);
}


static void
fuse_forget (fuse_req_t req, fuse_ino_t ino, unsigned long nlookup)
{
        inode_t      *fuse_inode;
        fuse_state_t *state;

        if (ino == 1) {
                fuse_reply_none (req);
                return;
        }

        state = state_from_req (req);
        fuse_inode = inode_search (state->itable, ino, NULL);
        if (fuse_inode) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "got forget on inode (%lu)", ino);
                inode_forget (fuse_inode, nlookup);
                inode_unref (fuse_inode);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "got forget, but inode (%lu) not found", ino);
        }

        free_state (state);
        fuse_reply_none (req);
}


static int
fuse_attr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct stat *buf)
{
        fuse_state_t   *state;
        fuse_req_t      req;
        fuse_private_t *priv = NULL;

        priv  = this->private;
        state = frame->root->state;
        req   = state->req;

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": %s() %s => %"PRId64, frame->root->unique,
                        gf_fop_list[frame->root->op],
                        state->loc.path ? state->loc.path : "ERR",
                        buf->st_ino);

                /* TODO: make these timeouts configurable via meta */
                /* TODO: what if the inode number has changed by now */
                buf->st_blksize = this->ctx->page_size;

                fuse_reply_attr (req, buf, priv->attribute_timeout);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": %s() %s => -1 (%s)", frame->root->unique,
                        gf_fop_list[frame->root->op],
                        state->loc.path ? state->loc.path : "ERR",
                        strerror (op_errno));

                fuse_reply_err (req, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);
        return 0;
}


static void
fuse_getattr (fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
        fuse_state_t *state;
        fd_t         *fd = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);

        if (ino == 1) {
                ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
                if (ret < 0) {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                "%"PRId64": GETATTR %"PRId64" (fuse_loc_fill() failed)",
                                req_callid (req), (ino_t)ino);
                        fuse_reply_err (req, ENOENT);
                        free_state (state);
                        return;
                }

                if (state->loc.inode)
                        state->is_revalidate = 1;
                else
                        state->is_revalidate = -1;

                state->dict = dict_new ();

                FUSE_FOP (state, fuse_lookup_cbk, GF_FOP_LOOKUP,
                          lookup, &state->loc, state->dict);
                return;
        }

        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);

        if (!state->loc.inode) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": GETATTR %"PRId64" (%s) (fuse_loc_fill() returned NULL inode)",
                        req_callid (req), (int64_t)ino, state->loc.path);
                fuse_reply_err (req, ENOENT);
                return;
        }

        fd = fd_lookup (state->loc.inode, get_pid_from_req (req));
        state->fd = fd;
        if (!fd || S_ISDIR (state->loc.inode->st_mode)) {
                /* this is the @ret of fuse_loc_fill, checked here
                   to permit fstat() to happen even when fuse_loc_fill fails
                */
                if (ret < 0) {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                "%"PRId64": GETATTR %"PRId64" (fuse_loc_fill() failed)",
                                req_callid (req), (ino_t)ino);
                        fuse_reply_err (req, ENOENT);
                        free_state (state);
                        return;
                }

                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": GETATTR %"PRId64" (%s)",
                        req_callid (req), (int64_t)ino, state->loc.path);


                FUSE_FOP (state, fuse_attr_cbk, GF_FOP_STAT,
                          stat, &state->loc);
        } else {

                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": FGETATTR %"PRId64" (%s/%p)",
                        req_callid (req), (int64_t)ino, state->loc.path, fd);

                FUSE_FOP (state,fuse_attr_cbk, GF_FOP_FSTAT,
                          fstat, fd);
        }
}


static int
fuse_fd_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        fuse_state_t          *state;
        fuse_req_t             req;
        fuse_private_t        *priv = NULL;
        struct fuse_file_info  fi = {0, };

        priv = this->private;
        state = frame->root->state;
        req = state->req;

        if (op_ret >= 0) {
                fi.fh = (unsigned long) fd;
                fi.flags = state->flags;

                if (!S_ISDIR (fd->inode->st_mode)) {
                        if ((fi.flags & 3) && priv->direct_io_mode)
                                fi.direct_io = 1;
                }

                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": %s() %s => %p", frame->root->unique,
                        gf_fop_list[frame->root->op], state->loc.path, fd);

                fd_ref (fd);
                if (fuse_reply_open (req, &fi) == -ENOENT) {
                        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                                "open(%s) got EINTR", state->loc.path);
                        fd_unref (fd);
                                goto out;
                }

                fd_bind (fd);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": %s() %s => -1 (%s)", frame->root->unique,
                        gf_fop_list[frame->root->op], state->loc.path,
                        strerror (op_errno));

                fuse_reply_err (req, op_errno);
        }
out:
        free_state (state);
        STACK_DESTROY (frame->root);
        return 0;
}


static void
do_chmod (fuse_req_t req, fuse_ino_t ino, struct stat *attr,
          struct fuse_file_info *fi)
{
        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);
        if (fi) {
                fd = FI_TO_FD (fi);
                state->fd = fd;
        }

        if (fd) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": FCHMOD %p", req_callid (req), fd);

                FUSE_FOP (state, fuse_attr_cbk, GF_FOP_FCHMOD,
                          fchmod, fd, attr->st_mode);
        } else {
                ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);

                if ((state->loc.inode == NULL) ||
                    (ret < 0)) {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                "%"PRId64": CHMOD %"PRId64" (%s) (fuse_loc_fill() failed)",
                                req_callid (req), (int64_t)ino,
                                state->loc.path);
                        fuse_reply_err (req, ENOENT);
                        free_state (state);
                        return;
                }


                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": CHMOD %s", req_callid (req),
                        state->loc.path);

                FUSE_FOP (state, fuse_attr_cbk, GF_FOP_CHMOD,
                          chmod, &state->loc, attr->st_mode);
        }
}


static void
do_chown (fuse_req_t req, fuse_ino_t ino, struct stat *attr,
          int valid, struct fuse_file_info *fi)
{
        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;
        int32_t       ret = -1;
        uid_t         uid = 0;
        gid_t         gid = 0;

        uid = (valid & FUSE_SET_ATTR_UID) ? attr->st_uid : (uid_t) -1;
        gid = (valid & FUSE_SET_ATTR_GID) ? attr->st_gid : (gid_t) -1;
        state = state_from_req (req);

        if (fi) {
                fd = FI_TO_FD (fi);
                state->fd = fd;
        }

        if (fd) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": FCHOWN %p", req_callid (req), fd);

                FUSE_FOP (state, fuse_attr_cbk, GF_FOP_FCHOWN,
                          fchown, fd, uid, gid);
        } else {
                ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
                if ((state->loc.inode == NULL) ||
                    (ret < 0)) {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                "%"PRId64": CHOWN %"PRId64" (%s) (fuse_loc_fill() failed)",
                                req_callid (req), (int64_t)ino,
                                state->loc.path);
                        fuse_reply_err (req, ENOENT);
                        free_state (state);
                        return;
                }

                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": CHOWN %s", req_callid (req),
                        state->loc.path);

                FUSE_FOP (state, fuse_attr_cbk, GF_FOP_CHOWN,
                          chown, &state->loc, uid, gid);
        }
}


static void
do_truncate (fuse_req_t req, fuse_ino_t ino, struct stat *attr,
             struct fuse_file_info *fi)
{
        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);

        if (fi) {
                fd = FI_TO_FD (fi);
                state->fd = fd;
        }

        if (fd) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": FTRUNCATE %p/%"PRId64, req_callid (req),
                        fd, attr->st_size);

                FUSE_FOP (state, fuse_attr_cbk, GF_FOP_FTRUNCATE,
                          ftruncate, fd, attr->st_size);
        } else {
                ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
                if ((state->loc.inode == NULL) ||
                    (ret < 0)) {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                "%"PRId64": TRUNCATE %s/%"PRId64" (fuse_loc_fill() failed)",
                                req_callid (req), state->loc.path,
                                attr->st_size);
                        fuse_reply_err (req, ENOENT);
                        free_state (state);
                        return;
                }

                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": TRUNCATE %s/%"PRId64"(%lu)",
                        req_callid (req),
                        state->loc.path, attr->st_size, ino);

                FUSE_FOP (state, fuse_attr_cbk, GF_FOP_TRUNCATE,
                          truncate, &state->loc, attr->st_size);
        }

        return;
}


static void
do_utimes (fuse_req_t req, fuse_ino_t ino, struct stat *attr)
{
        fuse_state_t    *state = NULL;
        struct timespec  tv[2];
        int32_t          ret = -1;

        tv[0].tv_sec  = attr->st_atime;
        tv[0].tv_nsec = ST_ATIM_NSEC (attr);
        tv[1].tv_sec  = attr->st_mtime;
        tv[1].tv_nsec = ST_ATIM_NSEC (attr);

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": UTIMENS %s (fuse_loc_fill() failed)",
                        req_callid (req), state->loc.path);
                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": UTIMENS (%lu)%s", req_callid (req),
                ino, state->loc.path);

        FUSE_FOP (state, fuse_attr_cbk, GF_FOP_UTIMENS,
                  utimens, &state->loc, tv);
}


static void
fuse_setattr (fuse_req_t req, fuse_ino_t ino, struct stat *attr,
              int valid, struct fuse_file_info *fi)
{

        if (valid & FUSE_SET_ATTR_MODE)
                do_chmod (req, ino, attr, fi);
        else if (valid & (FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID))
                do_chown (req, ino, attr, valid, fi);
        else if (valid & FUSE_SET_ATTR_SIZE)
                do_truncate (req, ino, attr, fi);
        else if (valid & (FUSE_SET_ATTR_ATIME | FUSE_SET_ATTR_MTIME))
                do_utimes (req, ino, attr);
        else
                fuse_getattr (req, ino, fi);
}


static int gf_fuse_xattr_enotsup_log;

static int
fuse_err_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno)
{
        fuse_state_t *state = frame->root->state;
        fuse_req_t req = state->req;

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": %s() %s => 0", frame->root->unique,
                        gf_fop_list[frame->root->op],
                        state->loc.path ? state->loc.path : "ERR");

                fuse_reply_err (req, 0);
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
                                "%"PRId64": %s() %s => -1 (%s)",
                                frame->root->unique,
                                gf_fop_list[frame->root->op],
                                state->loc.path ? state->loc.path : "ERR",
                                strerror (op_errno));
                }
        nolog:

                fuse_reply_err (req, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static int
fuse_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno)
{
        fuse_state_t *state = NULL;
        fuse_req_t    req = NULL;

        state = frame->root->state;
        req = state->req;

        if (op_ret == 0)
                inode_unlink (state->loc.inode, state->loc.parent,
                              state->loc.name);

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": %s() %s => 0", frame->root->unique,
                        gf_fop_list[frame->root->op], state->loc.path);

                fuse_reply_err (req, 0);
        } else {
                gf_log ("glusterfs-fuse",
                        op_errno == ENOTEMPTY ? GF_LOG_DEBUG : GF_LOG_WARNING,
                        "%"PRId64": %s() %s => -1 (%s)", frame->root->unique,
                        gf_fop_list[frame->root->op], state->loc.path,
                        strerror (op_errno));

                fuse_reply_err (req, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_access (fuse_req_t req, fuse_ino_t ino, int mask)
{
        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);

        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": ACCESS %"PRId64" (%s) (fuse_loc_fill() failed)",
                        req_callid (req), (int64_t)ino, state->loc.path);
                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64" ACCESS %s/%lu mask=%d", req_callid (req),
                state->loc.path, ino, mask);

        FUSE_FOP (state, fuse_err_cbk,
                  GF_FOP_ACCESS, access,
                  &state->loc, mask);

        return;
}


static int
fuse_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, const char *linkname)
{
        fuse_state_t *state = NULL;
        fuse_req_t    req = NULL;

        state = frame->root->state;
        req = state->req;

        if (op_ret > 0) {
                ((char *)linkname)[op_ret] = '\0';

                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": %s => %s", frame->root->unique,
                        state->loc.path, linkname);

                fuse_reply_readlink (req, linkname);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": %s => -1 (%s)", frame->root->unique,
                        state->loc.path, strerror (op_errno));

                fuse_reply_err (req, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_readlink (fuse_req_t req, fuse_ino_t ino)
{
        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64" READLINK %s/%"PRId64" (fuse_loc_fill() returned NULL inode)",
                        req_callid (req), state->loc.path,
                        state->loc.inode->ino);
                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64" READLINK %s/%"PRId64, req_callid (req),
                state->loc.path, state->loc.inode->ino);

        FUSE_FOP (state, fuse_readlink_cbk, GF_FOP_READLINK,
                  readlink, &state->loc, 4096);

        return;
}


static void
fuse_mknod (fuse_req_t req, fuse_ino_t par, const char *name,
            mode_t mode, dev_t rdev)
{
        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, 0, par, name);
        if (ret < 0) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64" MKNOD %s (fuse_loc_fill() failed)",
                        req_callid (req), state->loc.path);
                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        state->loc.inode = inode_new (state->itable);

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": MKNOD %s", req_callid (req),
                state->loc.path);

        FUSE_FOP (state, fuse_entry_cbk, GF_FOP_MKNOD,
                  mknod, &state->loc, mode, rdev);

        return;
}


static void
fuse_mkdir (fuse_req_t req, fuse_ino_t par, const char *name, mode_t mode)
{
        fuse_state_t *state;
        int32_t ret = -1;

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, 0, par, name);
        if (ret < 0) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64" MKDIR %s (fuse_loc_fill() failed)",
                        req_callid (req), state->loc.path);
                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        state->loc.inode = inode_new (state->itable);

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": MKDIR %s", req_callid (req),
                state->loc.path);

        FUSE_FOP (state, fuse_entry_cbk, GF_FOP_MKDIR,
                  mkdir, &state->loc, mode);

        return;
}


static void
fuse_unlink (fuse_req_t req, fuse_ino_t par, const char *name)
{
        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);

        ret = fuse_loc_fill (&state->loc, state, 0, par, name);

        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": UNLINK %s (fuse_loc_fill() returned NULL inode)",
                        req_callid (req), state->loc.path);
                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": UNLINK %s", req_callid (req),
                state->loc.path);

        FUSE_FOP (state, fuse_unlink_cbk, GF_FOP_UNLINK,
                  unlink, &state->loc);

        return;
}


static void
fuse_rmdir (fuse_req_t req, fuse_ino_t par, const char *name)
{
        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, 0, par, name);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": RMDIR %s (fuse_loc_fill() failed)",
                        req_callid (req), state->loc.path);
                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": RMDIR %s", req_callid (req),
                state->loc.path);

        FUSE_FOP (state, fuse_unlink_cbk, GF_FOP_RMDIR,
                  rmdir, &state->loc);

        return;
}


static void
fuse_symlink (fuse_req_t req, const char *linkname, fuse_ino_t par,
              const char *name)
{
        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, 0, par, name);
        if (ret < 0) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64" SYMLINK %s -> %s (fuse_loc_fill() failed)",
                        req_callid (req), state->loc.path, linkname);
                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        state->loc.inode = inode_new (state->itable);

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": SYMLINK %s -> %s", req_callid (req),
                state->loc.path, linkname);

        FUSE_FOP (state, fuse_entry_cbk, GF_FOP_SYMLINK,
                  symlink, linkname, &state->loc);

        return;
}


int
fuse_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct stat *buf)
{
        fuse_state_t *state = NULL;
        fuse_req_t    req = NULL;

        state = frame->root->state;
        req   = state->req;

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": %s -> %s => 0 (buf->st_ino=%"PRId64" , loc->ino=%"PRId64")",
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

                fuse_reply_err (req, 0);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": %s -> %s => -1 (%s)", frame->root->unique,
                        state->loc.path, state->loc2.path,
                        strerror (op_errno));
                fuse_reply_err (req, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);
        return 0;
}


static void
fuse_rename (fuse_req_t req, fuse_ino_t oldpar, const char *oldname,
             fuse_ino_t newpar, const char *newname)
{
        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);

        ret = fuse_loc_fill (&state->loc, state, 0, oldpar, oldname);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "for %s %"PRId64": RENAME `%s' -> `%s' (fuse_loc_fill() failed)",
                        state->loc.path, req_callid (req), state->loc.path,
                        state->loc2.path);

                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        ret = fuse_loc_fill (&state->loc2, state, 0, newpar, newname);
        if (ret < 0) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "for %s %"PRId64": RENAME `%s' -> `%s' (fuse_loc_fill() failed)",
                        state->loc.path, req_callid (req), state->loc.path,
                        state->loc2.path);

                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
               }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": RENAME `%s (%"PRId64")' -> `%s (%"PRId64")'",
                req_callid (req), state->loc.path, state->loc.ino,
                state->loc2.path, state->loc2.ino);

        FUSE_FOP (state, fuse_rename_cbk, GF_FOP_RENAME,
                  rename, &state->loc, &state->loc2);

        return;
}


static void
fuse_link (fuse_req_t req, fuse_ino_t ino, fuse_ino_t par, const char *name)
{
        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);

        ret = fuse_loc_fill (&state->loc, state, 0, par, name);
        ret = fuse_loc_fill (&state->loc2, state, ino, 0, NULL);

        if ((state->loc2.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "fuse_loc_fill() failed for %s %"PRId64": LINK %s %s",
                        state->loc2.path, req_callid (req),
                        state->loc2.path, state->loc.path);
                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        state->loc.inode = inode_ref (state->loc2.inode);
        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": LINK() %s (%"PRId64") -> %s (%"PRId64")",
                req_callid (req), state->loc2.path, state->loc2.ino,
                state->loc.path, state->loc.ino);

        FUSE_FOP (state, fuse_entry_cbk, GF_FOP_LINK,
                  link, &state->loc2, &state->loc);

        return;
}


static int
fuse_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 fd_t *fd, inode_t *inode, struct stat *buf)
{
        fuse_state_t            *state = NULL;
        fuse_req_t               req = NULL;
        fuse_private_t          *priv = NULL;
        struct fuse_file_info    fi = {0, };
        struct fuse_entry_param  e = {0, };

        state    = frame->root->state;
        priv     = this->private;
        req      = state->req;
        fi.flags = state->flags;

        if (op_ret >= 0) {
                fi.fh = (unsigned long) fd;

                if ((fi.flags & 3) && priv->direct_io_mode)
                        fi.direct_io = 1;

                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": %s() %s => %p (ino=%"PRId64")",
                        frame->root->unique, gf_fop_list[frame->root->op],
                        state->loc.path, fd, buf->st_ino);

                e.ino = buf->st_ino;

#ifdef GF_DARWIN_HOST_OS
                e.generation = 0;
#else
                e.generation = buf->st_ctime;
#endif

                buf->st_blksize = this->ctx->page_size;
                e.entry_timeout = priv->entry_timeout;
                e.attr_timeout = priv->attribute_timeout;
                e.attr = *buf;

                fi.keep_cache = 0;

                inode_link (inode, state->loc.parent,
                            state->loc.name, buf);

                inode_lookup (inode);

                fd_ref (fd);
                if (fuse_reply_create (req, &e, &fi) == -ENOENT) {
                        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                                "create(%s) got EINTR", state->loc.path);
                        inode_forget (inode, 1);
                        fd_unref (fd);
                        goto out;
                }

                fd_bind (fd);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": %s => -1 (%s)", req_callid (req),
                        state->loc.path, strerror (op_errno));
                fuse_reply_err (req, op_errno);
        }
out:
        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_create (fuse_req_t req, fuse_ino_t par, const char *name,
             mode_t mode, struct fuse_file_info *fi)
{
        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);
        state->flags = fi->flags;

        ret = fuse_loc_fill (&state->loc, state, 0, par, name);
        if (ret < 0) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64" CREATE %s (fuse_loc_fill() failed)",
                        req_callid (req), state->loc.path);
                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        state->loc.inode = inode_new (state->itable);

        fd = fd_create (state->loc.inode, get_pid_from_req (req));
        state->fd = fd;
        fd->flags = state->flags;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": CREATE %s", req_callid (req),
                state->loc.path);

        FUSE_FOP (state, fuse_create_cbk, GF_FOP_CREATE,
                  create, &state->loc, state->flags, mode, fd);

        return;
}


static void
fuse_open (fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);
        state->flags = fi->flags;

        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": OPEN %s (fuse_loc_fill() failed)",
                        req_callid (req), state->loc.path);

                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        fd = fd_create (state->loc.inode, get_pid_from_req (req));
        state->fd = fd;
        fd->flags = fi->flags;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": OPEN %s", req_callid (req),
                state->loc.path);

        FUSE_FOP (state, fuse_fd_cbk, GF_FOP_OPEN,
                  open, &state->loc, fi->flags, fd);

        return;
}


static int
fuse_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                struct iovec *vector, int32_t count,
                struct stat *stbuf, struct iobref *iobref)
{
        fuse_state_t *state = NULL;
        fuse_req_t    req = NULL;

        state = frame->root->state;
        req = state->req;

        if (op_ret >= 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": READ => %d/%"GF_PRI_SIZET",%"PRId64"/%"PRId64,
                        frame->root->unique,
                        op_ret, state->size, state->off, stbuf->st_size);

#ifdef HAVE_FUSE_REPLY_IOV
                fuse_reply_iov (req, vector, count);
#else
                fuse_reply_vec (req, vector, count);
#endif
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": READ => %d (%s)", frame->root->unique,
                        op_ret, strerror (op_errno));

                fuse_reply_err (req, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_readv (fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
            struct fuse_file_info *fi)
{
        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;

        state = state_from_req (req);
        state->size = size;
        state->off = off;

        fd = FI_TO_FD (fi);
        state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": READ (%p, size=%"GF_PRI_SIZET", offset=%"PRId64")",
                req_callid (req), fd, size, off);

        FUSE_FOP (state, fuse_readv_cbk, GF_FOP_READ,
                  readv, fd, size, off);

}


static int
fuse_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct stat *stbuf)
{
        fuse_state_t *state = NULL;
        fuse_req_t    req = NULL;

        state = frame->root->state;
        req = state->req;

        if (op_ret >= 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": WRITE => %d/%"GF_PRI_SIZET",%"PRId64"/%"PRId64,
                        frame->root->unique,
                        op_ret, state->size, state->off, stbuf->st_size);

                fuse_reply_write (req, op_ret);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": WRITE => -1 (%s)", frame->root->unique,
                        strerror (op_errno));

                fuse_reply_err (req, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_write (fuse_req_t req, fuse_ino_t ino, const char *buf,
            size_t size, off_t off,
            struct fuse_file_info *fi)
{
        fuse_state_t    *state = NULL;
        struct iovec     vector;
        fd_t            *fd = NULL;
        struct iobref   *iobref = NULL;
        struct iobuf    *iobuf = NULL;

        state       = state_from_req (req);
        state->size = size;
        state->off  = off;
        fd          = FI_TO_FD (fi);
        state->fd   = fd;
        vector.iov_base = (void *)buf;
        vector.iov_len  = size;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": WRITE (%p, size=%"GF_PRI_SIZET", offset=%"PRId64")",
                req_callid (req), fd, size, off);

        iobref = iobref_new ();
        iobuf = ((fuse_private_t *) (state->this->private))->iobuf;
        iobref_add (iobref, iobuf);

        FUSE_FOP (state, fuse_writev_cbk, GF_FOP_WRITE,
                  writev, fd, &vector, 1, off, iobref);

        iobref_unref (iobref);
        return;
}


static void
fuse_flush (fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;

        state = state_from_req (req);
        fd = FI_TO_FD (fi);
        state->fd = fd;
        if (fd)
                fd->flush_unique = req_callid (req);

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": FLUSH %p", req_callid (req), fd);

        FUSE_FOP (state, fuse_err_cbk, GF_FOP_FLUSH,
                  flush, fd);

        return;
}


static void
fuse_release (fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
        fuse_state_t *state = NULL;
        fd_t            *fd = NULL;
        int        do_flush = 0;

        state = state_from_req (req);
        fd = FI_TO_FD (fi);
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

        if (fd && fd->flush_unique + 1 != req_callid (req))
                do_flush = 1;
#endif

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": RELEASE %p%s", req_callid (req), fd,
                do_flush ? " (FLUSH implied)" : "");

        if (do_flush) {
                FUSE_FOP (state, fuse_err_cbk, GF_FOP_FLUSH, flush, fd);
                fd_unref (fd);
        } else {
                fd_unref (fd);

                fuse_reply_err (req, 0);

                free_state (state);
        }

        return;
}


static void
fuse_fsync (fuse_req_t req, fuse_ino_t ino, int datasync,
            struct fuse_file_info *fi)
{
        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;

        state = state_from_req (req);
        fd = FI_TO_FD (fi);
        state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": FSYNC %p", req_callid (req), fd);

        FUSE_FOP (state, fuse_err_cbk, GF_FOP_FSYNC,
                  fsync, fd, datasync);

        return;
}


static void
fuse_opendir (fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": OPENDIR %s (fuse_loc_fill() failed)",
                        req_callid (req), state->loc.path);

                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        fd = fd_create (state->loc.inode, get_pid_from_req (req));
        state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": OPENDIR %s", req_callid (req),
                state->loc.path);

        FUSE_FOP (state, fuse_fd_cbk, GF_FOP_OPENDIR,
                  opendir, &state->loc, fd);
}


static int
fuse_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
        fuse_state_t *state = NULL;
        fuse_req_t    req = NULL;
        int           size = 0;
        int           entry_size = 0;
        char         *buf = NULL;
        gf_dirent_t  *entry = NULL;
        struct stat   stbuf = {0, };

        state = frame->root->state;
        req   = state->req;

        if (op_ret < 0) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": READDIR => -1 (%s)", frame->root->unique,
                        strerror (op_errno));

                fuse_reply_err (req, op_errno);
                goto out;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": READDIR => %d/%"GF_PRI_SIZET",%"PRId64,
                frame->root->unique, op_ret, state->size, state->off);

        list_for_each_entry (entry, &entries->list, list) {
                size += fuse_dirent_size (strlen (entry->d_name));
        }

        buf = CALLOC (1, size);
        if (!buf) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": READDIR => -1 (%s)", frame->root->unique,
                        strerror (ENOMEM));
                fuse_reply_err (req, -ENOMEM);
                goto out;
        }

        size = 0;
        list_for_each_entry (entry, &entries->list, list) {
                stbuf.st_ino = entry->d_ino;
                entry_size = fuse_dirent_size (strlen (entry->d_name));
                fuse_add_direntry (req, buf + size, entry_size,
                                   entry->d_name, &stbuf,
                                   entry->d_off);
                size += entry_size;
        }

        fuse_reply_buf (req, (void *)buf, size);

out:
        free_state (state);
        STACK_DESTROY (frame->root);
        if (buf)
                FREE (buf);
        return 0;

}


static void
fuse_readdir (fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
              struct fuse_file_info *fi)
{
        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;

        state = state_from_req (req);
        state->size = size;
        state->off = off;
        fd = FI_TO_FD (fi);
        state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": READDIR (%p, size=%"GF_PRI_SIZET", offset=%"PRId64")",
                req_callid (req), fd, size, off);

        FUSE_FOP (state, fuse_readdir_cbk, GF_FOP_READDIR,
                  readdir, fd, size, off);
}


static void
fuse_releasedir (fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
        fuse_state_t *state = NULL;

        state = state_from_req (req);
        state->fd = FI_TO_FD (fi);

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": RELEASEDIR %p", req_callid (req), state->fd);

        fd_unref (state->fd);

        fuse_reply_err (req, 0);

        free_state (state);

        return;
}


static void
fuse_fsyncdir (fuse_req_t req, fuse_ino_t ino, int datasync,
               struct fuse_file_info *fi)
{
        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;

        fd = FI_TO_FD (fi);

        state = state_from_req (req);
        state->fd = fd;

        FUSE_FOP (state, fuse_err_cbk, GF_FOP_FSYNCDIR,
                  fsyncdir, fd, datasync);

        return;
}


static int
fuse_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct statvfs *buf)
{
        fuse_state_t *state = NULL;
        fuse_req_t    req = NULL;

        state = frame->root->state;
        req   = state->req;
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
                fuse_reply_statfs (req, buf);

        } else {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": ERR => -1 (%s)", frame->root->unique,
                        strerror (op_errno));
                fuse_reply_err (req, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_statfs (fuse_req_t req, fuse_ino_t ino)
{
        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, 1, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": STATFS (fuse_loc_fill() fail)",
                        req_callid (req));

                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": STATFS", req_callid (req));

        FUSE_FOP (state, fuse_statfs_cbk, GF_FOP_STATFS,
                  statfs, &state->loc);
}


static void
fuse_setxattr (fuse_req_t req, fuse_ino_t ino, const char *name,
               const char *value, size_t size, int flags)
{
        fuse_state_t *state = NULL;
        char         *dict_value = NULL;
        int32_t       ret = -1;

#ifdef DISABLE_POSIX_ACL
        if (!strncmp (name, "system.", 7)) {
                fuse_reply_err (req, EOPNOTSUPP);
                return;
        }
#endif

        state = state_from_req (req);
        state->size = size;
        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": SETXATTR %s/%"PRId64" (%s) (fuse_loc_fill() failed)",
                        req_callid (req),
                        state->loc.path, (int64_t)ino, name);

                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        state->dict = get_new_dict ();

        dict_value = memdup (value, size);
        dict_set (state->dict, (char *)name,
                  data_from_dynptr ((void *)dict_value, size));
        dict_ref (state->dict);

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": SETXATTR %s/%"PRId64" (%s)", req_callid (req),
                state->loc.path, (int64_t)ino, name);

        FUSE_FOP (state, fuse_err_cbk, GF_FOP_SETXATTR,
                  setxattr, &state->loc, state->dict, flags);

        return;
}

static void
fuse_reply_xattr_buf (fuse_state_t *state, fuse_req_t req, const char *value,
                     size_t ret)
{
        /* linux kernel limits the size of xattr value to 64k */
        if (ret > GLUSTERFS_XATTR_LEN_MAX)
                fuse_reply_err (req, E2BIG);
        else if (state->size) {
                /* if callback for getxattr and asks for value */
                if (ret > state->size)
                        /* reply would be bigger than
                         * what was asked by kernel */
                        fuse_reply_err (req, ERANGE);
                else
                        fuse_reply_buf (req, value, ret);
        } else
                fuse_reply_xattr (req, ret);
}

static int
fuse_xattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        int             need_to_free_dict = 0;
        char           *value = "";
        fuse_state_t   *state = NULL;
        fuse_req_t      req = NULL;
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
        req   = state->req;
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
                        "%"PRId64": %s() %s => %d", frame->root->unique,
                        gf_fop_list[frame->root->op], state->loc.path, op_ret);

                /* if successful */
                if (state->name) {
                        /* if callback for getxattr */
                        value_data = dict_get (dict, state->name);
                        if (value_data) {
                                ret = value_data->len; /* Don't return the value for '\0' */
                                value = value_data->data;

                                fuse_reply_xattr_buf (state, req, value, ret);
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
                                                fuse_reply_err (req, ENODATA);
                                        }

                                        priv->volfile_size = st.st_size;
                                        file = priv->volfile = CALLOC (1, priv->volfile_size);
                                        ret = lseek (fd, 0, SEEK_SET);
                                        while ((ret = read (fd, file, GF_UNIT_KB)) > 0) {
                                                file += ret;
                                        }
                                }

                                fuse_reply_xattr_buf (state, req, priv->volfile, priv->volfile_size);
                                /* if(ret >...)...else if...else */
                        } else if (!strcmp (state->name, "user.glusterfs-booster-path")) {
                                fuse_reply_xattr_buf (state, req, state->loc.path,
                                                      strlen (state->loc.path) + 1);
                        } else if (!strcmp (state->name, "user.glusterfs-booster-mount")) {
                                fuse_reply_xattr_buf (state, req, priv->mount_point,
                                                      strlen(priv->mount_point) + 1);
                        } else {
                                fuse_reply_err (req, ENODATA);
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
                        fuse_reply_xattr_buf (state, req, value, len);
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
                                        "%"PRId64": %s() %s => -1 (%s)",
                                        frame->root->unique,
                                        gf_fop_list[frame->root->op],
                                        state->loc.path, strerror (op_errno));
                        }
                } else {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                "%"PRId64": %s() %s => -1 (%s)",
                                frame->root->unique,
                                gf_fop_list[frame->root->op], state->loc.path,
                                strerror (op_errno));
                } /* if(op_errno!= ENODATA)...else */

                fuse_reply_err (req, op_errno);
        } /* if(op_ret>=0)...else */

        if (need_to_free_dict)
                dict_unref (dict);

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_getxattr (fuse_req_t req, fuse_ino_t ino, const char *name, size_t size)
{
        fuse_state_t *state = NULL;
        int32_t       ret = -1;

#ifdef DISABLE_POSIX_ACL
        if (!strncmp (name, "system.", 7)) {
                fuse_reply_err (req, ENODATA);
                return;
        }
#endif

        state = state_from_req (req);
        state->size = size;
        state->name = strdup (name);

        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": GETXATTR %s/%"PRId64" (%s) (fuse_loc_fill() failed)",
                        req_callid (req), state->loc.path, (int64_t)ino, name);

                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": GETXATTR %s/%"PRId64" (%s)", req_callid (req),
                state->loc.path, (int64_t)ino, name);

        FUSE_FOP (state, fuse_xattr_cbk, GF_FOP_GETXATTR,
                  getxattr, &state->loc, name);

        return;
}


static void
fuse_listxattr (fuse_req_t req, fuse_ino_t ino, size_t size)
{
        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);
        state->size = size;
        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                        "%"PRId64": LISTXATTR %s/%"PRId64" (fuse_loc_fill() failed)",
                        req_callid (req), state->loc.path, (int64_t)ino);

                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": LISTXATTR %s/%"PRId64, req_callid (req),
                state->loc.path, (int64_t)ino);

        FUSE_FOP (state, fuse_xattr_cbk, GF_FOP_GETXATTR,
                  getxattr, &state->loc, NULL);

        return;
}


static void
fuse_removexattr (fuse_req_t req, fuse_ino_t ino, const char *name)

{
        fuse_state_t *state = NULL;
        int32_t       ret = -1;

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
        if ((state->loc.inode == NULL) ||
            (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": REMOVEXATTR %s/%"PRId64" (%s) (fuse_loc_fill() failed)",
                        req_callid (req), state->loc.path, (int64_t)ino, name);

                fuse_reply_err (req, ENOENT);
                free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": REMOVEXATTR %s/%"PRId64" (%s)", req_callid (req),
                state->loc.path, (int64_t)ino, name);

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

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                        "%"PRId64": ERR => 0", frame->root->unique);
                fuse_reply_lock (state->req, lock);
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
                                "%"PRId64": ERR => -1 (%s)",
                                frame->root->unique, strerror (op_errno));
                }
                fuse_reply_err (state->req, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_getlk (fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi,
            struct flock *lock)
{
        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;

        fd = FI_TO_FD (fi);
        state = state_from_req (req);
        state->req = req;
        state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": GETLK %p", req_callid (req), fd);

        FUSE_FOP (state, fuse_getlk_cbk, GF_FOP_LK,
                  lk, fd, F_GETLK, lock);

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
                        "%"PRId64": ERR => 0", frame->root->unique);
                fuse_reply_err (state->req, 0);
        } else {
                if (op_errno == ENOSYS) {
                        gf_fuse_lk_enosys_log++;
                        if (!(gf_fuse_lk_enosys_log % GF_UNIVERSAL_ANSWER)) {
                                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                        "SETLK not supported. loading "
                                        "'features/posix-locks' on server side "
                                        "will add SETLK support.");
                        }
                } else  if (op_errno != EAGAIN) {
                        gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                "%"PRId64": ERR => -1 (%s)",
                                frame->root->unique, strerror (op_errno));
                }

                fuse_reply_err (state->req, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_setlk (fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi,
            struct flock *lock, int sleep)
{
        fuse_state_t *state = NULL;
        fd_t         *fd = NULL;

        fd = FI_TO_FD (fi);
        state = state_from_req (req);
        state->req = req;
        state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_TRACE,
                "%"PRId64": SETLK %p (sleep=%d)", req_callid (req), fd,
                sleep);

        FUSE_FOP (state, fuse_setlk_cbk, GF_FOP_LK,
                  lk, fd, (sleep ? F_SETLKW : F_SETLK), lock);

        return;
}


static void
fuse_init (void *data, struct fuse_conn_info *conn)
{
        return;
}

static void
fuse_destroy (void *data)
{

}

static struct fuse_lowlevel_ops fuse_ops = {
        .init         = fuse_init,
        .destroy      = fuse_destroy,
        .lookup       = fuse_lookup,
        .forget       = fuse_forget,
        .getattr      = fuse_getattr,
        .setattr      = fuse_setattr,
        .opendir      = fuse_opendir,
        .readdir      = fuse_readdir,
        .releasedir   = fuse_releasedir,
        .access       = fuse_access,
        .readlink     = fuse_readlink,
        .mknod        = fuse_mknod,
        .mkdir        = fuse_mkdir,
        .unlink       = fuse_unlink,
        .rmdir        = fuse_rmdir,
        .symlink      = fuse_symlink,
        .rename       = fuse_rename,
        .link         = fuse_link,
        .create       = fuse_create,
        .open         = fuse_open,
        .read         = fuse_readv,
        .write        = fuse_write,
        .flush        = fuse_flush,
        .release      = fuse_release,
        .fsync        = fuse_fsync,
        .fsyncdir     = fuse_fsyncdir,
        .statfs       = fuse_statfs,
        .setxattr     = fuse_setxattr,
        .getxattr     = fuse_getxattr,
        .listxattr    = fuse_listxattr,
        .removexattr  = fuse_removexattr,
        .getlk        = fuse_getlk,
        .setlk        = fuse_setlk
};


int
fuse_root_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      inode_t *inode, struct stat *buf, dict_t *xattr)
{
        fuse_private_t *priv = NULL;

        priv = this->private;

        if (op_ret == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "first lookup on root succeeded.");
                inode_lookup (inode);
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
fuse_root_lookup (xlator_t *this)
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
        loc.inode = inode_search (this->itable, 1, NULL);
        loc.parent = NULL;

        dict = dict_new ();
        frame = create_frame (this, this->ctx->pool);
        frame->root->type = GF_OP_TYPE_FOP_REQUEST;
        xl = this->children->xlator;

        STACK_WIND (frame, fuse_root_lookup_cbk, xl, xl->fops->lookup,
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
        int32_t         res = 0;
        struct iobuf   *iobuf = NULL;
        size_t          chan_size = 0;
        int             ret  = -1;

        struct timeval  now;
        struct timespec timeout;


        this = data;
        priv = this->private;
        chan_size = fuse_chan_bufsize (priv->ch);

        pthread_mutex_lock (&priv->child_up_mutex);
        {
                gettimeofday (&now, NULL);
                timeout.tv_sec = now.tv_sec + MAX_FUSE_PROC_DELAY;
                timeout.tv_nsec = now.tv_usec * 1000;

                while (priv->child_up_value) {
                        ret = pthread_cond_timedwait (&priv->child_up_cond,
                                                &priv->child_up_mutex,
                                                &timeout);
                        if (ret != 0)
                                break;

                }
        }
        pthread_mutex_unlock (&priv->child_up_mutex);

        gf_log (this->name, GF_LOG_DEBUG,
                " pthread_cond_timedout returned non zero value"
                " ret: %d errno: %d", ret, errno);

        while (!fuse_session_exited (priv->se)) {
                iobuf = iobuf_get (this->ctx->iobuf_pool);

                if (!iobuf) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Out of memory");
                        sleep (10);
                        continue;
                }

                res = fuse_chan_receive (priv->ch, iobuf->ptr, chan_size);

                if (priv->first_call) {
                        if (priv->first_call > 1) {
                                priv->first_call--;
                        } else {
                                fuse_root_lookup (this);
                        }
                }

                if (res == -1) {
                        if (errno != EINTR) {
                                gf_log ("glusterfs-fuse", GF_LOG_WARNING,
                                        "fuse_chan_receive() returned -1 (%d)", errno);
                        }
                        if (errno == ENODEV)
                                break;
                        continue;
                }

                priv->iobuf = iobuf;

                if (res && res != -1) {
                        fuse_session_process (priv->se, iobuf->ptr,
                                              res, priv->ch);
                }

                iobuf_unref (iobuf);
        }

        if (dict_get (this->options, ZR_MOUNTPOINT_OPT))
                mount_point = data_to_str (dict_get (this->options,
                                                     ZR_MOUNTPOINT_OPT));
        if (mount_point) {
                gf_log (this->name, GF_LOG_NORMAL,
                        "unmounting %s", mount_point);
                dict_del (this->options, ZR_MOUNTPOINT_OPT);
        }

        fuse_session_remove_chan (priv->ch);
        fuse_session_destroy (priv->se);
        //  fuse_unmount (priv->mount_point, priv->ch);

        raise (SIGTERM);

        return NULL;
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
                pthread_mutex_lock (&private->child_up_mutex);
                {
                        private->child_up_value = 0;
                        pthread_cond_broadcast (&private->child_up_cond);
                }
                pthread_mutex_unlock (&private->child_up_mutex);
                break;
        }

        case GF_EVENT_PARENT_UP:
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

static struct fuse_opt subtype_workaround[] = {
        FUSE_OPT_KEY ("subtype=", 0),
        FUSE_OPT_KEY ("fssubtype=", 0),
        FUSE_OPT_END
};

static int
subtype_workaround_optproc (void *data, const char *arg, int key,
                           struct fuse_args *outargs)
{
        return key ? 1 : 0;
}

int
init (xlator_t *this_xl)
{
        int                ret = 0;
        dict_t            *options = NULL;
        char              *value_string = NULL;
        char              *fsname = NULL;
        char              *fsname_opt = NULL;
        fuse_private_t    *priv = NULL;
        struct stat        stbuf = {0,};
        struct fuse_args   args = FUSE_ARGS_INIT (0, NULL);

        if (this_xl == NULL)
                return -1;

        if (this_xl->options == NULL)
                return -1;

        options = this_xl->options;

        if (this_xl->name == NULL) {
                this_xl->name = strdup ("fuse");
                ERR_ABORT (this_xl->name);
        }

        fsname = this_xl->ctx->cmd_args.volume_file;
        fsname = (fsname ? fsname : this_xl->ctx->cmd_args.volfile_server);
        fsname = (fsname ? fsname : "glusterfs");
        ret = asprintf (&fsname_opt, "-ofsname=%s", fsname);

        if (ret != -1)
                ret = fuse_opt_add_arg (&args, "glusterfs");
        if (ret != -1)
                ret = fuse_opt_add_arg (&args, fsname_opt);
        if (ret != -1)
                ret = fuse_opt_add_arg (&args, "-oallow_other");
        if (ret != -1)
                ret = fuse_opt_add_arg (&args, "-odefault_permissions");
#ifdef GF_DARWIN_HOST_OS
        if (ret != -1)
                ret = fuse_opt_add_arg (&args, "-ofssubtype=glusterfs");
        if (ret != -1 && !dict_get (options, "macfuse-local"))
                /* This way, GlusterFS will be detected as 'servers' instead
                 *  of 'devices'. This method is useful if you want to do
                 * 'umount <mount_point>' over network,  instead of 'eject'ing
                 * it from desktop. Works better for servers
                 */
                ret = fuse_opt_add_arg (&args, "-olocal");
#else /* ! DARWIN_OS */
        if (ret != -1)
                ret = fuse_opt_add_arg (&args, "-osubtype=glusterfs");
        if (ret != -1)
                ret = fuse_opt_add_arg (&args, "-omax_readahead=131072");
        if (ret != -1)
                ret = fuse_opt_add_arg (&args, "-omax_read=131072");
        if (ret != -1)
                ret = fuse_opt_add_arg (&args, "-omax_write=131072");
        if (ret != -1)
                ret = fuse_opt_add_arg (&args, "-osuid");
#if GF_LINUX_HOST_OS /* LINUX */
        /* '-o dev', '-o nonempty' is supported only on Linux */
        if (ret != -1)
                ret = fuse_opt_add_arg (&args, "-ononempty");
        if (ret != -1)
                ret = fuse_opt_add_arg (&args, "-odev");
#ifdef HAVE_FUSE_VERSION_28
        if (ret != -1)
                ret = fuse_opt_add_arg (&args, "-obig_writes");
#endif /* FUSE 2.8 */

#endif /* LINUX */
#endif /* ! DARWIN_OS */

        if (ret == -1)
                ERR_ABORT (NULL);

        priv = CALLOC (1, sizeof (*priv));
        ERR_ABORT (priv);
        this_xl->private = (void *) priv;

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
        ERR_ABORT (priv->mount_point);

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

        priv->ch = fuse_mount (priv->mount_point, &args);
        if (priv->ch == NULL) {
                if (errno == ENOTCONN) {
                        gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                "A stale mount is present on %s. "
                                "Run 'umount %s' and try again",
                                priv->mount_point,
                                priv->mount_point);
                } else {
                        if (errno == ENOENT) {
                                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                        "Unable to mount on %s. Run "
                                        "'modprobe fuse' and try again",
                                        priv->mount_point);
                        } else {
                                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                                        "fuse_mount() failed%s%s "
                                        "on mount point %s",
                                        errno ? " with error " : "",
                                        errno ? strerror (errno) : "",
                                        priv->mount_point);
                        }
                }

                goto cleanup_exit;
        }

        errno = 0;

        priv->se = fuse_lowlevel_new (&args, &fuse_ops,
                                      sizeof (fuse_ops), this_xl);
        if (priv->se == NULL && !errno) {
                /*
                 * Option parsing misery. Can happen if libfuse is of
                 * FUSE < 2.7.0, as then the "-o subtype" option is not
                 * handled.
                 *
                 * Best we can do to is to handle it at runtime -- this is not
                 * a binary incompatibility issue (which should dealt with at
                 * compile time), but a behavioural incompatibility issue. Ie.
                 * we can't tell in advance whether the lib we use supports
                 * "-o subtype". So try to be clever now.
                 *
                 * Delete the subtype option, and try again.
                 */
                if (fuse_opt_parse (&args, NULL, subtype_workaround,
                                   subtype_workaround_optproc) == 0)
                        priv->se = fuse_lowlevel_new (&args, &fuse_ops,
                                                      sizeof (fuse_ops),
                                                      this_xl);
        }

        if (priv->se == NULL) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "fuse_lowlevel_new() failed with error %s on "
                        "mount point %s",
                        strerror (errno), priv->mount_point);
                goto umount_exit;
        }

        ret = fuse_set_signal_handlers (priv->se);
        if (ret == -1) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "fuse_set_signal_handlers() failed on mount point %s",
                        priv->mount_point);
                goto umount_exit;
        }

        fuse_opt_free_args (&args);
        FREE (fsname_opt);

        fuse_session_add_chan (priv->se, priv->ch);

        priv->fd = fuse_chan_fd (priv->ch);

        this_xl->ctx->top = this_xl;

        pthread_cond_init (&priv->child_up_cond, NULL);
        pthread_mutex_init (&priv->child_up_mutex, NULL);
        priv->child_up_value = 1;

        priv->first_call = 2;
        this_xl->itable = inode_table_new (0, this_xl);
        return 0;

umount_exit:
        fuse_unmount (priv->mount_point, priv->ch);
cleanup_exit:
        fuse_opt_free_args (&args);
        FREE (fsname_opt);
        if (priv)
                FREE (priv->mount_point);
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
                fuse_session_exit (priv->se);
                fuse_unmount (mount_point, priv->ch);
        }
}

struct xlator_fops fops = {
};

struct xlator_cbks cbks = {
};

struct xlator_mops mops = {
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
