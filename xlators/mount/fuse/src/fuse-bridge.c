/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

/* TODO: when supporting posix acl, remove this definition */
#define DISABLE_POSIX_ACL

#define ZR_MOUNTPOINT_OPT   "mount-point"
#define ZR_DIRECT_IO_OPT    "direct-io-mode"

#define BIG_FUSE_CHANNEL_SIZE 1048576

struct fuse_private {
        int                  fd;
        struct fuse         *fuse;
        struct fuse_session *se;
        struct fuse_chan    *ch;
        char                *volfile;
        size_t               volfile_size;
        char                *mount_point;
        data_t              *buf;
        pthread_t            fuse_thread;
        char                 fuse_thread_started;
        uint32_t             direct_io_mode;
        uint32_t             entry_timeout;
        uint32_t             attribute_timeout;

};
typedef struct fuse_private fuse_private_t;

#define _FI_TO_FD(fi) ((fd_t *)((long)fi->fh))

#define FI_TO_FD(fi) ((_FI_TO_FD (fi))?(fd_ref (_FI_TO_FD(fi))):((fd_t *) 0))

#define FUSE_FOP(state, ret, op_num, fop, args ...)                     \
        do {                                                            \
                call_frame_t *frame = get_call_frame_for_req (state, 1); \
                xlator_t *xl = frame->this->children ?                  \
                        frame->this->children->xlator : NULL;           \
                dict_t *refs = frame->root->req_refs;                   \
                frame->root->state = state;                             \
                frame->op   = op_num;                                   \
                STACK_WIND (frame, ret, xl, xl->fops->fop, args);       \
                dict_unref (refs);                                      \
        } while (0)


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

int fuse_chan_receive (struct fuse_chan *ch,
                       char *buf,
                       int32_t size);


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
        fuse_state_t *state;
        xlator_t *this = NULL;

        this = fuse_req_userdata (req);

        state = (void *)calloc (1, sizeof (*state));
        ERR_ABORT (state);
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
        ctx = fuse_req_ctx(req);
        return ctx->pid;
}

static call_frame_t *
get_call_frame_for_req (fuse_state_t *state, char d)
{
        call_pool_t *pool = state->pool;
        fuse_req_t req = state->req;
        const struct fuse_ctx *ctx = NULL;
	call_frame_t *frame = NULL;
        xlator_t *this = NULL;
        fuse_private_t *priv = NULL;


	if (req) {
		this = fuse_req_userdata (req);
	} else {
		this = state->this;
	}
	priv = this->private;

	frame = create_frame (this, pool);

        if (req) {
                ctx = fuse_req_ctx(req);

                frame->root->uid    = ctx->uid;
                frame->root->gid    = ctx->gid;
                frame->root->pid    = ctx->pid;
                frame->root->unique = req_callid (req);
        }

        if (d) {
                frame->root->req_refs = dict_ref (get_new_dict ());
                dict_set (frame->root->req_refs, NULL, priv->buf);
        }

        frame->type = GF_OP_TYPE_FOP_REQUEST;

        return frame;
}


GF_MUST_CHECK static int32_t
fuse_loc_fill (loc_t *loc,
               fuse_state_t *state,
               ino_t ino,
               ino_t par,
               const char *name)
{
        inode_t *inode = NULL, *parent = NULL;
	int32_t ret = -1;
	char *path = NULL;

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
			gf_log ("glusterfs-fuse", GF_LOG_ERROR,
				"inode_path failed for %"PRId64"/%s",
				parent->ino, name);
			goto fail;
		} else {
			loc->path = path;
		}
        } else 	if (inode) {
		ret = inode_path (inode, NULL, &path);
		if (ret <= 0) {
			gf_log ("glusterfs-fuse", GF_LOG_ERROR,
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
		gf_log ("fuse-bridge", GF_LOG_ERROR,
			"failed to search parent for %"PRId64" (%s)",
			(ino_t)ino, name);
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
		gf_log ("fuse-bridge",
			(op_errno == ENOENT)? GF_LOG_DEBUG: GF_LOG_WARNING,
			"revalidate of %s failed (%s)",
			loc->path, strerror (op_errno));
                return 1;
	}

        if (loc->inode->ino != buf->st_ino) {
		gf_log ("fuse-bridge", GF_LOG_WARNING,
			"inode num of %s changed %"PRId64" -> %"PRId64,
			loc->path, loc->inode->ino, buf->st_ino);
                return 1;
        }

	if ((loc->inode->st_mode & S_IFMT) ^ (buf->st_mode & S_IFMT)) {
		gf_log ("fuse-bridge", GF_LOG_WARNING,
			"inode mode of %s changed 0%o -> 0%o",
			loc->path, loc->inode->st_mode, buf->st_mode);
		return 1;
	}

        return 0;
}


static int
fuse_lookup_cbk (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno,
                 inode_t *inode,
                 struct stat *stat,
                 dict_t *dict);

static int
fuse_entry_cbk (call_frame_t *frame,
                void *cookie,
                xlator_t *this,
                int32_t op_ret,
                int32_t op_errno,
                inode_t *inode,
                struct stat *buf)
{
        fuse_state_t *state;
        fuse_req_t req;
        struct fuse_entry_param e = {0, };
        fuse_private_t *priv = this->private;

        state = frame->root->state;
        req = state->req;

        if (!op_ret && inode->ino == 1) {
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
                            &state->loc, 0);

                return 0;
        }

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": %s() %s => %"PRId64" (%"PRId64")",
			frame->root->unique, gf_fop_list[frame->op],
			state->loc.path, buf->st_ino, state->loc.ino);

		if (state->is_revalidate != 1)
			inode_link (inode, state->loc.parent,
				    state->loc.name, buf);

		inode_lookup (inode);

                /* TODO: make these timeouts configurable (via meta?) */
                e.ino = inode->ino;

#ifdef GF_DARWIN_HOST_OS
                e.generation = 0;
#else
                e.generation = buf->st_ctime;
#endif

                e.entry_timeout = priv->entry_timeout;
                e.attr_timeout  = priv->attribute_timeout;
                e.attr = *buf;
                e.attr.st_blksize = BIG_FUSE_CHANNEL_SIZE; 
  
		if (!e.ino || !buf->st_ino) {
			gf_log ("glusterfs-fuse", GF_LOG_ERROR,
				"%"PRId64": %s() %s returning inode 0",
				frame->root->unique,
				gf_fop_list[frame->op], state->loc.path);
		}

                if (state->loc.parent)
                        fuse_reply_entry (req, &e);
                else
                        fuse_reply_attr (req, buf, priv->attribute_timeout);
        } else {
                gf_log ("glusterfs-fuse",
			(op_errno == ENOENT ? GF_LOG_DEBUG : GF_LOG_ERROR),
                        "%"PRId64": %s() %s => -1 (%s)", frame->root->unique,
                        gf_fop_list[frame->op], state->loc.path,
			strerror (op_errno));
                fuse_reply_err (req, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);
        return 0;
}


static int
fuse_lookup_cbk (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno,
                 inode_t *inode,
                 struct stat *stat,
                 dict_t *dict)
{
        fuse_entry_cbk (frame, cookie, this, op_ret, op_errno, inode, stat);
        return 0;
}


static void
fuse_lookup (fuse_req_t req,
             fuse_ino_t par,
             const char *name)
{
        fuse_state_t *state;
	int32_t ret = -1;

        state = state_from_req (req);

        ret = fuse_loc_fill (&state->loc, state, 0, par, name);

	if (ret < 0) {
		gf_log ("glusterfs-fuse", GF_LOG_ERROR, 
			"%"PRId64": LOOKUP %"PRId64"/%s (fuse_loc_fill() failed)", 
			req_callid (req), (ino_t)par, name);
		free_state (state);
		fuse_reply_err (req, EINVAL);
		return;
	}

        if (!state->loc.inode) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": LOOKUP %s", req_callid (req),
                        state->loc.path);

                state->loc.inode = inode_new (state->itable);
                /* to differntiate in entry_cbk what kind of call it is */
                state->is_revalidate = -1;
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": LOOKUP %s(%"PRId64")", req_callid (req),
                        state->loc.path, state->loc.inode->ino);
                state->is_revalidate = 1;
        }

        FUSE_FOP (state, fuse_lookup_cbk, GF_FOP_LOOKUP,
                  lookup, &state->loc, 0);
}


static void
fuse_forget (fuse_req_t req,
             fuse_ino_t ino,
             unsigned long nlookup)
{
        inode_t *fuse_inode;
        fuse_state_t *state;

        if (ino == 1) {
                fuse_reply_none (req);
                return;
        }

        state = state_from_req (req);
        fuse_inode = inode_search (state->itable, ino, NULL);
	if (fuse_inode) {
		gf_log ("glusterfs-fuse", GF_LOG_DEBUG, 
			"got forget on inode (%lu)", ino);
		inode_forget (fuse_inode, nlookup);
		inode_unref (fuse_inode);
	} else {
		gf_log ("glusterfs-fuse", GF_LOG_ERROR, 
			"got forget, but inode (%lu) not found", ino);
	}

        free_state (state);
        fuse_reply_none (req);
}


static int
fuse_attr_cbk (call_frame_t *frame,
               void *cookie,
               xlator_t *this,
               int32_t op_ret,
               int32_t op_errno,
               struct stat *buf)
{
        fuse_state_t *state;
        fuse_req_t req;
        fuse_private_t *priv = this->private;

        state = frame->root->state;
        req = state->req;

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse",
			(buf->st_ino ? GF_LOG_DEBUG : GF_LOG_ERROR),
                        "%"PRId64": %s() %s => %"PRId64, frame->root->unique, 
                        gf_fop_list[frame->op],
			state->loc.path ? state->loc.path : "ERR",
                        buf->st_ino);

                /* TODO: make these timeouts configurable via meta */
                /* TODO: what if the inode number has changed by now */ 
                buf->st_blksize = BIG_FUSE_CHANNEL_SIZE;

                fuse_reply_attr (req, buf, priv->attribute_timeout);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": %s() %s => -1 (%s)", frame->root->unique, 
                        gf_fop_list[frame->op],
			state->loc.path ? state->loc.path : "ERR", 
                        strerror (op_errno));

                fuse_reply_err (req, op_errno);
        }
        
        free_state (state);
        STACK_DESTROY (frame->root);
        return 0;
}


static void
fuse_getattr (fuse_req_t req,
              fuse_ino_t ino,
              struct fuse_file_info *fi)
{
        fuse_state_t *state;
        fd_t         *fd = NULL;
	int32_t       ret = -1;

        state = state_from_req (req);

        if (ino == 1) {
                ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
		if (ret < 0) {
			gf_log ("glusterfs-fuse", GF_LOG_ERROR, 
				"%"PRId64": GETATTR %"PRId64" (fuse_loc_fill() failed)",
				req_callid(req), (ino_t)ino);
			fuse_reply_err (req, EINVAL);
			free_state (state);
			return;
		}

                if (state->loc.inode)
                        state->is_revalidate = 1;
                else
                        state->is_revalidate = -1;
                FUSE_FOP (state, fuse_lookup_cbk, GF_FOP_LOOKUP,
                          lookup, &state->loc, 0);
                return;
        }

        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
	if (ret < 0) {
		gf_log ("glusterfs-fuse", GF_LOG_ERROR, 
			"%"PRId64": GETATTR %"PRId64" (fuse_loc_fill() failed)",
			req_callid(req), (ino_t)ino);
		fuse_reply_err (req, EINVAL);
		free_state (state);
		return;
	}


        if (!state->loc.inode) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": GETATTR %"PRId64" (%s) (fuse_loc_fill() returned NULL inode)", 
                        req_callid (req), (int64_t)ino, state->loc.path);
                fuse_reply_err (req, EINVAL);
                return;
        }
        
        fd = fd_lookup (state->loc.inode, get_pid_from_req (req));
        state->fd = fd;
        if ((fd == NULL) || 
            S_ISDIR (state->loc.inode->st_mode)) {

                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": GETATTR %"PRId64" (%s)",
                        req_callid (req), (int64_t)ino, state->loc.path);
    
                FUSE_FOP (state, fuse_attr_cbk, GF_FOP_STAT,
                          stat, &state->loc);
        } else {

                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": FGETATTR %"PRId64" (%s/%p)",
                        req_callid (req), (int64_t)ino, state->loc.path, fd);

                FUSE_FOP (state,fuse_attr_cbk, GF_FOP_FSTAT,
                          fstat, fd);
        }
}


static int
fuse_fd_cbk (call_frame_t *frame,
             void *cookie,
             xlator_t *this,
             int32_t op_ret,
             int32_t op_errno,
             fd_t *fd)
{
        fuse_state_t *state;
        fuse_req_t req;
        fuse_private_t *priv = this->private;

        state = frame->root->state;
        req = state->req;

        if (op_ret >= 0) {
                struct fuse_file_info fi = {0, };
                
                fi.fh = (unsigned long) fd;
                fi.flags = state->flags;

                if (!S_ISDIR (fd->inode->st_mode)) {
                        if ((fi.flags & 3) && priv->direct_io_mode)
                                fi.direct_io = 1;
                }

                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": %s() %s => %p", frame->root->unique,
			gf_fop_list[frame->op], state->loc.path, fd);

		fd_ref (fd);
                if (fuse_reply_open (req, &fi) == -ENOENT) {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
				"open() got EINTR");
			fd_unref (fd);
				goto out;
                }
		
		fd_bind (fd);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": %s() %s => -1 (%s)", frame->root->unique,
                        gf_fop_list[frame->op], state->loc.path,
			strerror (op_errno));

                fuse_reply_err (req, op_errno);
        }
out:
        free_state (state);
        STACK_DESTROY (frame->root);
        return 0;
}



static void
do_chmod (fuse_req_t req,
          fuse_ino_t ino,
          struct stat *attr,
          struct fuse_file_info *fi)
{
        fuse_state_t *state = state_from_req (req);
        fd_t *fd = NULL;
	int32_t ret = -1;

        if (fi) {
                fd = FI_TO_FD (fi);
                state->fd = fd;
        }

        if (fd) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": FCHMOD %p", req_callid (req), fd);

                FUSE_FOP (state, fuse_attr_cbk, GF_FOP_FCHMOD,
                          fchmod, fd, attr->st_mode);
        } else {
                ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);

                if ((state->loc.inode == NULL) ||
		    (ret < 0)) {
                        gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                "%"PRId64": CHMOD %"PRId64" (%s) (fuse_loc_fill() failed)", 
                                req_callid (req), (int64_t)ino,
				state->loc.path);
                        fuse_reply_err (req, EINVAL);
			free_state (state);
                        return;
                }


                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": CHMOD %s", req_callid (req),
                        state->loc.path);

                FUSE_FOP (state, fuse_attr_cbk, GF_FOP_CHMOD,
                          chmod, &state->loc, attr->st_mode);
        }
}


static void
do_chown (fuse_req_t req,
          fuse_ino_t ino,
          struct stat *attr,
          int valid,
          struct fuse_file_info *fi)
{
        fuse_state_t *state;
        fd_t *fd = NULL;
	int32_t ret = -1;
        uid_t uid = (valid & FUSE_SET_ATTR_UID) ? attr->st_uid : (uid_t) -1;
        gid_t gid = (valid & FUSE_SET_ATTR_GID) ? attr->st_gid : (gid_t) -1;

        state = state_from_req (req);

        if (fi) {
                fd = FI_TO_FD (fi);
                state->fd = fd;
        }

        if (fd) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": FCHOWN %p", req_callid (req), fd);

                FUSE_FOP (state, fuse_attr_cbk, GF_FOP_FCHOWN,
                          fchown, fd, uid, gid);
        } else {
                ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
                if ((state->loc.inode == NULL) ||
		    (ret < 0)) {
                        gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                "%"PRId64": CHOWN %"PRId64" (%s) (fuse_loc_fill() failed)", 
                                req_callid (req), (int64_t)ino,
				state->loc.path);
                        fuse_reply_err (req, EINVAL);
			free_state (state);
                        return;
                }

                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": CHOWN %s", req_callid (req),
                        state->loc.path);

                FUSE_FOP (state, fuse_attr_cbk, GF_FOP_CHOWN,
                          chown, &state->loc, uid, gid);
        }
}


static void 
do_truncate (fuse_req_t req,
             fuse_ino_t ino,
             struct stat *attr,
             struct fuse_file_info *fi)
{
        fuse_state_t *state;
        fd_t *fd = NULL;
	int32_t ret = -1;

        state = state_from_req (req);
        
        if (fi) {
                fd = FI_TO_FD (fi);
                state->fd = fd;
        }
        if (fd) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": FTRUNCATE %p/%"PRId64, req_callid (req),
                        fd, attr->st_size);

                FUSE_FOP (state, fuse_attr_cbk, GF_FOP_FTRUNCATE,
                          ftruncate, fd, attr->st_size);
        } else {
                ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
                if ((state->loc.inode == NULL) || 
		    (ret < 0)) {
                        gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                "%"PRId64": TRUNCATE %s/%"PRId64" (fuse_loc_fill() failed)", 
                                req_callid (req), state->loc.path,
				attr->st_size);
                        fuse_reply_err (req, EINVAL);
			free_state (state);
                        return;
                }

                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": TRUNCATE %s/%"PRId64"(%lu)",
			req_callid (req),
                        state->loc.path, attr->st_size, ino);

                FUSE_FOP (state, fuse_attr_cbk, GF_FOP_TRUNCATE,
                          truncate, &state->loc, attr->st_size);
        }

        return;
}


static void 
do_utimes (fuse_req_t req,
           fuse_ino_t ino,
           struct stat *attr)
{
        fuse_state_t *state;

        struct timespec tv[2];
	int32_t ret = -1;

#ifdef FUSE_STAT_HAS_NANOSEC
        tv[0] = ST_ATIM(attr);
        tv[1] = ST_MTIM(attr);
#else
        tv[0].tv_sec = attr->st_atime;
        tv[0].tv_nsec = 0;
        tv[1].tv_sec = attr->st_mtime;
        tv[1].tv_nsec = 0;
#endif

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
        if ((state->loc.inode == NULL) ||
	    (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": UTIMENS %s (fuse_loc_fill() failed)", 
                        req_callid (req), state->loc.path);
                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": UTIMENS (%lu)%s", req_callid (req),
                ino, state->loc.path);

        FUSE_FOP (state, fuse_attr_cbk, GF_FOP_UTIMENS,
                  utimens, &state->loc, tv);
}


static void
fuse_setattr (fuse_req_t req,
              fuse_ino_t ino,
              struct stat *attr,
              int valid,
              struct fuse_file_info *fi)
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
fuse_err_cbk (call_frame_t *frame,
              void *cookie,
              xlator_t *this,
              int32_t op_ret,
              int32_t op_errno)
{
        fuse_state_t *state = frame->root->state;
        fuse_req_t req = state->req;

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": %s() %s => 0", frame->root->unique,
			gf_fop_list[frame->op], 
                        state->loc.path ? state->loc.path : "ERR");

                fuse_reply_err (req, 0);
        } else {
                if (frame->op == GF_FOP_SETXATTR) {
			op_ret = gf_compat_setxattr (state->dict);
			if (op_ret == 0)
				op_errno = 0;
			if (op_errno == ENOTSUP) {
				gf_fuse_xattr_enotsup_log++;
				if (!(gf_fuse_xattr_enotsup_log % GF_UNIVERSAL_ANSWER))
					gf_log ("glusterfs-fuse", GF_LOG_CRITICAL,
						"[ ERROR ] Extended attribute not supported by the backend storage");
			}
                } else {
                        gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                "%"PRId64": %s() %s => -1 (%s)",
				frame->root->unique, gf_fop_list[frame->op],
				state->loc.path ? state->loc.path : "ERR",
                                strerror (op_errno));
                }
                fuse_reply_err (req, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}



static int
fuse_unlink_cbk (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno)
{
        fuse_state_t *state = frame->root->state;
        fuse_req_t req = state->req;

        if (op_ret == 0)
                inode_unlink (state->loc.inode, state->loc.parent,
			      state->loc.name);

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": %s() %s => 0", frame->root->unique,
                        gf_fop_list[frame->op], state->loc.path);

                fuse_reply_err (req, 0);
        } else {
                gf_log ("glusterfs-fuse", 
                        (op_errno != ENOTEMPTY ? GF_LOG_ERROR : GF_LOG_DEBUG),
                        "%"PRId64": %s() %s => -1 (%s)", frame->root->unique,
                        gf_fop_list[frame->op], state->loc.path,
			strerror (op_errno));

                fuse_reply_err (req, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_access (fuse_req_t req,
             fuse_ino_t ino,
             int mask)
{
        fuse_state_t *state;
	int32_t ret = -1;

        state = state_from_req (req);

        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
        if ((state->loc.inode == NULL) || 
	    (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": ACCESS %"PRId64" (%s) (fuse_loc_fill() failed)", 
                        req_callid (req), (int64_t)ino, state->loc.path);
                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64" ACCESS %s/%lu mask=%d", req_callid (req),
                state->loc.path, ino, mask);

        FUSE_FOP (state, fuse_err_cbk,
                  GF_FOP_ACCESS, access,
		  &state->loc, mask);

        return;
}



static int
fuse_readlink_cbk (call_frame_t *frame,
                   void *cookie,
                   xlator_t *this,
                   int32_t op_ret,
                   int32_t op_errno,
                   const char *linkname)
{
        fuse_state_t *state = frame->root->state;
        fuse_req_t req = state->req;

        if (op_ret > 0) {
                ((char *)linkname)[op_ret] = '\0';

                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": %s => %s", frame->root->unique,
                        state->loc.path, linkname);

                fuse_reply_readlink(req, linkname);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": %s => -1 (%s)", frame->root->unique,
                        state->loc.path, strerror(op_errno));

                fuse_reply_err(req, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_readlink (fuse_req_t req,
               fuse_ino_t ino)
{
        fuse_state_t *state;
	int32_t ret = -1;

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
        if ((state->loc.inode == NULL) ||
	    (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64" READLINK %s/%"PRId64" (fuse_loc_fill() returned NULL inode)", 
                        req_callid (req), state->loc.path,
			state->loc.inode->ino);
                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
        }
  
        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64" READLINK %s/%"PRId64, req_callid (req),
                state->loc.path, state->loc.inode->ino);

        FUSE_FOP (state, fuse_readlink_cbk, GF_FOP_READLINK,
		  readlink, &state->loc, 4096);

        return;
}


static void
fuse_mknod (fuse_req_t req,
            fuse_ino_t par,
            const char *name,
            mode_t mode,
            dev_t rdev)
{
        fuse_state_t *state;
	int32_t ret = -1;

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, 0, par, name);
	if (ret < 0) {
		gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64" MKNOD %s (fuse_loc_fill() failed)", 
                        req_callid (req), state->loc.path);
                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
	}

        state->loc.inode = inode_new (state->itable);

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": MKNOD %s", req_callid (req),
                state->loc.path);

        FUSE_FOP (state, fuse_entry_cbk, GF_FOP_MKNOD,
		  mknod, &state->loc, mode, rdev);

        return;
}


static void 
fuse_mkdir (fuse_req_t req,
            fuse_ino_t par,
            const char *name,
            mode_t mode)
{
        fuse_state_t *state;
	int32_t ret = -1;

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, 0, par, name);
	if (ret < 0) {
		gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64" MKDIR %s (fuse_loc_fill() failed)", 
                        req_callid (req), state->loc.path);
                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
	}

        state->loc.inode = inode_new (state->itable);

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": MKDIR %s", req_callid (req),
                state->loc.path);

        FUSE_FOP (state, fuse_entry_cbk, GF_FOP_MKDIR,
		  mkdir, &state->loc, mode);

        return;
}


static void 
fuse_unlink (fuse_req_t req,
             fuse_ino_t par,
             const char *name)
{
        fuse_state_t *state;
	int32_t ret = -1;

        state = state_from_req (req);

        ret = fuse_loc_fill (&state->loc, state, 0, par, name);

        if ((state->loc.inode == NULL) ||
	    (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": UNLINK %s (fuse_loc_fill() returned NULL inode)",
                        req_callid (req), state->loc.path);
                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": UNLINK %s", req_callid (req),
                state->loc.path);

        FUSE_FOP (state, fuse_unlink_cbk, GF_FOP_UNLINK,
		  unlink, &state->loc);

        return;
}


static void 
fuse_rmdir (fuse_req_t req,
            fuse_ino_t par,
            const char *name)
{
        fuse_state_t *state;
	int32_t ret = -1;

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, 0, par, name);
        if ((state->loc.inode == NULL) ||
	    (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": RMDIR %s (fuse_loc_fill() failed)",
                        req_callid (req), state->loc.path);
                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": RMDIR %s", req_callid (req),
                state->loc.path);

        FUSE_FOP (state, fuse_unlink_cbk, GF_FOP_RMDIR,
		  rmdir, &state->loc);

        return;
}


static void
fuse_symlink (fuse_req_t req,
              const char *linkname,
              fuse_ino_t par,
              const char *name)
{
        fuse_state_t *state;
	int32_t ret = -1;

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, 0, par, name);
	if (ret < 0) {
		gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64" SYMLINK %s -> %s (fuse_loc_fill() failed)", 
                        req_callid (req), state->loc.path, linkname);
                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
	}

        state->loc.inode = inode_new (state->itable);

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": SYMLINK %s -> %s", req_callid (req),
                state->loc.path, linkname);

        FUSE_FOP (state, fuse_entry_cbk, GF_FOP_SYMLINK,
                  symlink, linkname, &state->loc);

        return;
}


int 
fuse_rename_cbk (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno,
                 struct stat *buf)
{
        fuse_state_t *state = frame->root->state;
        fuse_req_t req = state->req;

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
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
                inode_rename (state->itable,
                              state->loc.parent, state->loc.name,
                              state->loc2.parent, state->loc2.name,
                              state->loc.inode, buf);

                fuse_reply_err (req, 0);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
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
fuse_rename (fuse_req_t req,
             fuse_ino_t oldpar,
             const char *oldname,
             fuse_ino_t newpar,
             const char *newname)
{
        fuse_state_t *state;
	int32_t ret = -1;

        state = state_from_req (req);

        ret = fuse_loc_fill (&state->loc, state, 0, oldpar, oldname);
        if ((state->loc.inode == NULL) ||
	    (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "for %s %"PRId64": RENAME `%s' -> `%s' (fuse_loc_fill() failed)",
                        state->loc.path, req_callid (req), state->loc.path,
                        state->loc2.path);
    
                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
        }

        ret = fuse_loc_fill (&state->loc2, state, 0, newpar, newname);
	if (ret < 0) {
		gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "for %s %"PRId64": RENAME `%s' -> `%s' (fuse_loc_fill() failed)",
                        state->loc.path, req_callid (req), state->loc.path,
                        state->loc2.path);
    
                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
       	}

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": RENAME `%s (%"PRId64")' -> `%s (%"PRId64")'",
                req_callid (req), state->loc.path, state->loc.ino,
                state->loc2.path, state->loc2.ino);

        FUSE_FOP (state, fuse_rename_cbk, GF_FOP_RENAME,
                  rename, &state->loc, &state->loc2);

        return;
}


static void
fuse_link (fuse_req_t req,
           fuse_ino_t ino,
           fuse_ino_t par,
           const char *name)
{
        fuse_state_t *state;
	int32_t ret = -1;

        state = state_from_req (req);

        ret = fuse_loc_fill (&state->loc, state, 0, par, name);
        ret = fuse_loc_fill (&state->loc2, state, ino, 0, NULL);

        if ((state->loc2.inode == NULL) ||
	    (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "fuse_loc_fill() failed for %s %"PRId64": LINK %s %s", 
                        state->loc2.path, req_callid (req), 
                        state->loc2.path, state->loc.path);
                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
        }

        state->loc.inode = inode_ref (state->loc2.inode);
        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": LINK() %s (%"PRId64") -> %s (%"PRId64")",
		req_callid (req), state->loc2.path, state->loc2.ino,
		state->loc.path, state->loc.ino);

        FUSE_FOP (state, fuse_entry_cbk, GF_FOP_LINK,
                  link, &state->loc2, &state->loc);

        return;
}


static int
fuse_create_cbk (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno,
                 fd_t *fd,
                 inode_t *inode,
                 struct stat *buf)
{
        fuse_state_t *state = frame->root->state;
        fuse_req_t req = state->req;
        fuse_private_t *priv = this->private;

        struct fuse_file_info fi = {0, };
        struct fuse_entry_param e = {0, };

        fi.flags = state->flags;
        if (op_ret >= 0) {
                fi.fh = (unsigned long) fd;

                if ((fi.flags & 3) && priv->direct_io_mode)
                        fi.direct_io = 1;

                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": %s() %s => %p (ino=%"PRId64")",
			frame->root->unique, gf_fop_list[frame->op],
			state->loc.path, fd, buf->st_ino);

                e.ino = buf->st_ino;

#ifdef GF_DARWIN_HOST_OS
                e.generation = 0;
#else
                e.generation = buf->st_ctime;
#endif

                e.entry_timeout = priv->entry_timeout;
                e.attr_timeout = priv->attribute_timeout;
                e.attr = *buf;
                e.attr.st_blksize = BIG_FUSE_CHANNEL_SIZE;

                fi.keep_cache = 0;

		inode_link (inode, state->loc.parent,
			    state->loc.name, buf);

		fd_ref (fd);
                if (fuse_reply_create (req, &e, &fi) == -ENOENT) {
                        gf_log ("glusterfs-fuse", GF_LOG_WARNING,
				"create() got EINTR");
			fd_unref (fd);
			goto out;
                } 

		inode_lookup (inode);
		fd_bind (fd);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
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
fuse_create (fuse_req_t req,
             fuse_ino_t par,
             const char *name,
             mode_t mode,
             struct fuse_file_info *fi)
{
        fuse_state_t *state;
        fd_t *fd;
	int32_t ret = -1;

        state = state_from_req (req);
        state->flags = fi->flags;

        ret = fuse_loc_fill (&state->loc, state, 0, par, name);
	if (ret < 0) {
		gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64" CREATE %s (fuse_loc_fill() failed)", 
                        req_callid (req), state->loc.path);
                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
	}

        state->loc.inode = inode_new (state->itable);

        fd = fd_create (state->loc.inode, get_pid_from_req (req));
        state->fd = fd;
	fd->flags = state->flags;

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": CREATE %s", req_callid (req),
                state->loc.path);

        FUSE_FOP (state, fuse_create_cbk, GF_FOP_CREATE,
                  create, &state->loc, state->flags, mode, fd);

        return;
}


static void
fuse_open (fuse_req_t req,
           fuse_ino_t ino,
           struct fuse_file_info *fi)
{
        fuse_state_t *state;
        fd_t *fd;
	int32_t ret = -1;

        state = state_from_req (req);
        state->flags = fi->flags;

        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
        if ((state->loc.inode == NULL) ||
	    (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": OPEN %s (fuse_loc_fill() failed)",
                        req_callid (req), state->loc.path);
  
                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
        }


        fd = fd_create (state->loc.inode, get_pid_from_req (req));
        state->fd = fd;
	fd->flags = fi->flags;

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": OPEN %s", req_callid (req),
                state->loc.path);

        FUSE_FOP (state, fuse_fd_cbk, GF_FOP_OPEN,
                  open, &state->loc, fi->flags, fd);

        return;
}


static int
fuse_readv_cbk (call_frame_t *frame,
                void *cookie,
                xlator_t *this,
                int32_t op_ret,
                int32_t op_errno,
                struct iovec *vector,
                int32_t count,
                struct stat *stbuf)
{
        fuse_state_t *state = frame->root->state;
        fuse_req_t req = state->req;

        if (op_ret >= 0) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": READ => %d/%"GF_PRI_SIZET",%"PRId64"/%"PRId64,
			frame->root->unique,
                        op_ret, state->size, state->off, stbuf->st_size);

                fuse_reply_vec (req, vector, count);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": READ => %d (%s)", frame->root->unique, 
                        op_ret, strerror (op_errno));

                fuse_reply_err (req, op_errno);
        }
        
        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}

static void
fuse_readv (fuse_req_t req,
            fuse_ino_t ino,
            size_t size,
            off_t off,
            struct fuse_file_info *fi)
{
        fuse_state_t *state;
        fd_t *fd = NULL;
        state = state_from_req (req);
        state->size = size;
        state->off = off;
        
        fd = FI_TO_FD (fi);
        state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": READ (%p, size=%"GF_PRI_SIZET", offset=%"PRId64")",
                req_callid (req), fd, size, off);

        FUSE_FOP (state, fuse_readv_cbk, GF_FOP_READ,
                  readv, fd, size, off);

}


static int
fuse_writev_cbk (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno,
                 struct stat *stbuf)
{
        fuse_state_t *state = frame->root->state;
        fuse_req_t req = state->req;

        if (op_ret >= 0) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": WRITE => %d/%"GF_PRI_SIZET",%"PRId64"/%"PRId64,
			frame->root->unique,
                        op_ret, state->size, state->off, stbuf->st_size);

                fuse_reply_write (req, op_ret);
        } else {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": WRITE => -1 (%s)", frame->root->unique, 
                        strerror(op_errno));

                fuse_reply_err (req, op_errno);
        }
        
        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_write (fuse_req_t req,
            fuse_ino_t ino,
            const char *buf,
            size_t size,
            off_t off,
            struct fuse_file_info *fi)
{
        fuse_state_t *state;
        struct iovec vector;
	fd_t *fd = NULL;

        state = state_from_req (req);
        state->size = size;
        state->off = off;
	fd = FI_TO_FD (fi);
	state->fd = fd;
        vector.iov_base = (void *)buf;
        vector.iov_len = size;

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": WRITE (%p, size=%"GF_PRI_SIZET", offset=%"PRId64")",
                req_callid (req), fd, size, off);

        FUSE_FOP (state, fuse_writev_cbk, GF_FOP_WRITE,
                  writev, fd, &vector, 1, off);
        return;
}


static void
fuse_flush (fuse_req_t req,
            fuse_ino_t ino,
            struct fuse_file_info *fi)
{
        fuse_state_t *state;
	fd_t *fd = NULL;

        state = state_from_req (req);
	fd = FI_TO_FD (fi);
	state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": FLUSH %p", req_callid (req), fd);

        FUSE_FOP (state, fuse_err_cbk, GF_FOP_FLUSH,
                  flush, fd);

        return;
}


static void 
fuse_release (fuse_req_t req,
	      fuse_ino_t ino,
	      struct fuse_file_info *fi)
{
        fuse_state_t *state;

        state = state_from_req (req);
        state->fd = FI_TO_FD (fi);

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": RELEASE %p", req_callid (req), state->fd);

        fd_unref (state->fd);
        
        fuse_reply_err (req, 0);
        
        free_state (state);
        return;
}


static void 
fuse_fsync (fuse_req_t req,
            fuse_ino_t ino,
            int datasync,
            struct fuse_file_info *fi)
{
        fuse_state_t *state;
        fd_t *fd = NULL;

        state = state_from_req (req);
        fd = FI_TO_FD (fi);
        state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": FSYNC %p", req_callid (req), fd);

        FUSE_FOP (state, fuse_err_cbk, GF_FOP_FSYNC,
                  fsync, fd, datasync);

        return;
}


static void
fuse_opendir (fuse_req_t req,
              fuse_ino_t ino,
              struct fuse_file_info *fi)
{
        fuse_state_t *state;
        fd_t *fd;
	int32_t ret = -1;

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
        if ((state->loc.inode == NULL) ||
	    (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": OPENDIR %s (fuse_loc_fill() failed)",
                        req_callid (req), state->loc.path);
  
                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
        }

        fd = fd_create (state->loc.inode, get_pid_from_req (req));
        state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": OPENDIR %s", req_callid (req),
                state->loc.path);

        FUSE_FOP (state, fuse_fd_cbk, GF_FOP_OPENDIR,
                  opendir, &state->loc, fd);
}

static int
fuse_readdir_cbk (call_frame_t *frame,
                  void *cookie,
                  xlator_t *this,
                  int32_t op_ret,
                  int32_t op_errno,
                  gf_dirent_t *entries)
{
        fuse_state_t *state = frame->root->state;
        fuse_req_t    req = state->req;
	int           size = 0;
	int           entry_size = 0;
	char         *buf = NULL;
	gf_dirent_t  *entry = NULL;
	struct stat   stbuf = {0, };

        if (op_ret < 0) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": READDIR => -1 (%s)", frame->root->unique, 
                        strerror (op_errno));

                fuse_reply_err (req, op_errno);
		goto out;
	}

	gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
		"%"PRId64": READDIR => %d/%"GF_PRI_SIZET",%"PRId64,
		frame->root->unique, op_ret, state->size, state->off);

	list_for_each_entry (entry, &entries->list, list) {
		size += fuse_dirent_size (strlen (entry->d_name));
	}

	buf = calloc (1, size);
	if (!buf) {
		gf_log ("glusterfs-fuse", GF_LOG_ERROR,
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
fuse_readdir (fuse_req_t req,
              fuse_ino_t ino,
              size_t size,
              off_t off,
              struct fuse_file_info *fi)
{
        fuse_state_t *state;
	fd_t *fd = NULL;

        state = state_from_req (req);
        state->size = size;
        state->off = off;
	fd = FI_TO_FD (fi);
	state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": READDIR (%p, size=%"GF_PRI_SIZET", offset=%"PRId64")",
                req_callid (req), fd, size, off);

        FUSE_FOP (state, fuse_readdir_cbk, GF_FOP_READDIR,
                  readdir, fd, size, off);
}


static void 
fuse_releasedir (fuse_req_t req,
		 fuse_ino_t ino,
		 struct fuse_file_info *fi)
{
        fuse_state_t *state;

        state = state_from_req (req);
        state->fd = FI_TO_FD (fi);

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": RELEASEDIR %p", req_callid (req), state->fd);
	
	fd_unref (state->fd);

	fuse_reply_err (req, 0);
	
	free_state (state);

        return;
}


static void 
fuse_fsyncdir (fuse_req_t req,
               fuse_ino_t ino,
               int datasync,
               struct fuse_file_info *fi)
{
        fuse_state_t *state;
	fd_t *fd = NULL;
	
	fd = FI_TO_FD (fi);

        state = state_from_req (req);
	state->fd = fd;

        FUSE_FOP (state, fuse_err_cbk, GF_FOP_FSYNCDIR,
                  fsyncdir, fd, datasync);

        return;
}


static int
fuse_statfs_cbk (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno,
                 struct statvfs *buf)
{
        fuse_state_t *state = frame->root->state;
        fuse_req_t req = state->req;

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
                buf->f_blocks /= BIG_FUSE_CHANNEL_SIZE;

                buf->f_bavail *= buf->f_frsize;
                buf->f_bavail /= BIG_FUSE_CHANNEL_SIZE;

                buf->f_bfree *= buf->f_frsize;
                buf->f_bfree /= BIG_FUSE_CHANNEL_SIZE;

                buf->f_frsize = buf->f_bsize = BIG_FUSE_CHANNEL_SIZE;
#endif /* GF_DARWIN_HOST_OS */
                fuse_reply_statfs (req, buf);

        } else {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": ERR => -1 (%s)", frame->root->unique, 
                        strerror(op_errno));
                fuse_reply_err (req, op_errno);
        }

        free_state (state);
        STACK_DESTROY (frame->root);

        return 0;
}


static void
fuse_statfs (fuse_req_t req,
             fuse_ino_t ino)
{
        fuse_state_t *state;
	int32_t ret = -1;

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, 1, 0, NULL);
        if ((state->loc.inode == NULL) ||
	    (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": STATFS (fuse_loc_fill() fail)",
			req_callid (req));
    
                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": STATFS", req_callid (req));

        FUSE_FOP (state, fuse_statfs_cbk, GF_FOP_STATFS,
                  statfs, &state->loc);
}


static void
fuse_setxattr (fuse_req_t req,
               fuse_ino_t ino,
               const char *name,
               const char *value,
               size_t size,
               int flags)
{
        fuse_state_t *state;
	char *dict_value = NULL;
	int32_t ret = -1;

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
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": SETXATTR %s/%"PRId64" (%s) (fuse_loc_fill() failed)", 
                        req_callid (req),
                        state->loc.path, (int64_t)ino, name);

                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
        }

        state->dict = get_new_dict ();

	dict_value = memdup (value, size);
        dict_set (state->dict, (char *)name,
                  data_from_dynptr ((void *)dict_value, size));
        dict_ref (state->dict);

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": SETXATTR %s/%"PRId64" (%s)", req_callid (req),
                state->loc.path, (int64_t)ino, name);

        FUSE_FOP (state, fuse_err_cbk, GF_FOP_SETXATTR,
                  setxattr, &state->loc, state->dict, flags);

        return;
}


static int
fuse_xattr_cbk (call_frame_t *frame,
                void *cookie,
                xlator_t *this,
                int32_t op_ret,
                int32_t op_errno,
                dict_t *dict)
{
	int need_to_free_dict = 0;
        int32_t ret = op_ret;
        char *value = "";
        fuse_state_t *state = frame->root->state;
        fuse_req_t req = state->req;

#ifdef GF_DARWIN_HOST_OS
	/* This is needed in MacFuse, where MacOSX Finder needs some specific 
	 * keys to be supported from FS
	 */
	int32_t dummy_ret = 0;
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
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": %s() %s => %d", frame->root->unique,
                        gf_fop_list[frame->op], state->loc.path, op_ret);

                /* if successful */
                if (state->name) {
                        /* if callback for getxattr */
                        data_t *value_data = dict_get (dict, state->name);
                        if (value_data) {
                                ret = value_data->len; /* Don't return the value for '\0' */
                                value = value_data->data;
        
                                /* linux kernel limits the size of xattr value to 64k */
                                if (ret > GLUSTERFS_XATTR_LEN_MAX) {
                                        fuse_reply_err (req, E2BIG);
                                } else if (state->size) {
                                        /* if callback for getxattr and asks for value */
                                        fuse_reply_buf (req, value, ret);
                                } else {
                                        /* if callback for getxattr and asks for value length only */
                                        fuse_reply_xattr (req, ret);
                                } /* if(ret >...)...else if...else */
                        }  else if (!strcmp (state->name, "user.glusterfs-booster-volfile")) {
				fuse_private_t *priv = this->private;
 
				if (!priv->volfile) {
					glusterfs_ctx_t *ctx = NULL;
					int32_t fd = -1, ret = -1;
					struct stat st;
					char *file = NULL;
					
					memset (&st, 0, sizeof (st));
                                         ctx = get_global_ctx_ptr ();
                                         fd = fileno (ctx->specfp);
                                         ret = fstat (fd, &st);
                                         if (ret != 0) {
                                                 gf_log (this->name,
                                                         GF_LOG_ERROR,
                                                         "fstat on fd (%d) failed (%s)", fd, strerror (errno));
                                                 fuse_reply_err (req, ENODATA);
                                         }
					 
                                         priv->volfile_size = st.st_size;
                                         file = priv->volfile = calloc (1, priv->volfile_size);
                                         ret = lseek (fd, 0, SEEK_SET);
                                         while ((ret = read (fd, file, GF_UNIT_KB)) > 0) {
                                                 file += ret;
                                         }
				}
 
				if (priv->volfile_size > GLUSTERFS_XATTR_LEN_MAX) {
					fuse_reply_err (req, E2BIG);
				} else if (state->size) {
					/* if callback for getxattr and asks for value */
					fuse_reply_buf (req, priv->volfile, priv->volfile_size);
				} else {
					/* if callback for getxattr and asks for value length only */
					fuse_reply_xattr (req, priv->volfile_size);
				} /* if(ret >...)...else if...else */
			} else if (!strcmp (state->name, "user.glusterfs-booster-path")) {
				if (state->size) {
					fuse_reply_buf (req, state->loc.path, strlen (state->loc.path) + 1);
				} else {
					fuse_reply_xattr (req, strlen (state->loc.path) + 1);
				}
			} else {
				fuse_reply_err (req, ENODATA);
			} /* if(value_data)...else */
		} else {
				/* if callback for listxattr */
                        int32_t len = 0;
                        data_pair_t *trav = dict->members_list;
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
                                value[len + strlen(trav->key)] = '\0';
                                len += strlen (trav->key) + 1;
                                trav = trav->next;
                        } /* while(trav) */
                        if (state->size) {
                                /* if callback for listxattr and asks for list of keys */
                                fuse_reply_buf (req, value, len);
                        } else {
                                /* if callback for listxattr and asks for length of keys only */
                                fuse_reply_xattr (req, len);
                        } /* if(state->size)...else */
		} /* if(state->name)...else */
        } else {
                /* if failure - no need to check if listxattr or getxattr */
                if (op_errno != ENODATA) {
                        if (op_errno == ENOTSUP) 
                        {
                                gf_fuse_xattr_enotsup_log++;
                                if (!(gf_fuse_xattr_enotsup_log % GF_UNIVERSAL_ANSWER))
                                        gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                                "[ ERROR ] Extended attribute not supported by the backend storage");
                        } 
                        else 
                        {
                                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                        "%"PRId64": %s() %s => -1 (%s)", frame->root->unique,
                                        gf_fop_list[frame->op], state->loc.path, strerror(op_errno));
                        }
                } else {
                        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                                "%"PRId64": %s() %s => -1 (%s)", frame->root->unique,
                                gf_fop_list[frame->op], state->loc.path, strerror(op_errno));
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
fuse_getxattr (fuse_req_t req,
               fuse_ino_t ino,
               const char *name,
               size_t size)
{
        fuse_state_t *state;
	int32_t ret = -1;

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
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": GETXATTR %s/%"PRId64" (%s) (fuse_loc_fill() failed)", 
                        req_callid (req), state->loc.path, (int64_t)ino, name);

                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": GETXATTR %s/%"PRId64" (%s)", req_callid (req),
                state->loc.path, (int64_t)ino, name);

        FUSE_FOP (state, fuse_xattr_cbk, GF_FOP_GETXATTR,
                  getxattr, &state->loc, name);

        return;
}


static void
fuse_listxattr (fuse_req_t req,
                fuse_ino_t ino,
                size_t size)
{
        fuse_state_t *state;
	int32_t ret = -1;

        state = state_from_req (req);
        state->size = size;
        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
        if ((state->loc.inode == NULL) ||
	    (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": LISTXATTR %s/%"PRId64" (fuse_loc_fill() failed)", 
                        req_callid (req), state->loc.path, (int64_t)ino);

                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": LISTXATTR %s/%"PRId64, req_callid (req),
                state->loc.path, (int64_t)ino);

        FUSE_FOP (state, fuse_xattr_cbk, GF_FOP_GETXATTR,
                  getxattr, &state->loc, NULL);

        return;
}


static void
fuse_removexattr (fuse_req_t req,
                  fuse_ino_t ino,
                  const char *name)

{
        fuse_state_t *state;
	int32_t ret = -1;

        state = state_from_req (req);
        ret = fuse_loc_fill (&state->loc, state, ino, 0, NULL);
        if ((state->loc.inode == NULL) ||
	    (ret < 0)) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "%"PRId64": REMOVEXATTR %s/%"PRId64" (%s) (fuse_loc_fill() failed)",
                        req_callid (req), state->loc.path, (int64_t)ino, name);

                fuse_reply_err (req, EINVAL);
		free_state (state);
                return;
        }

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": REMOVEXATTR %s/%"PRId64" (%s)", req_callid (req),
                state->loc.path, (int64_t)ino, name);

        FUSE_FOP (state, fuse_err_cbk, GF_FOP_REMOVEXATTR,
                  removexattr, &state->loc, name);

        return;
}


static int gf_fuse_lk_enosys_log;

static int
fuse_getlk_cbk (call_frame_t *frame,
                void *cookie,
                xlator_t *this,
                int32_t op_ret,
                int32_t op_errno,
                struct flock *lock)
{
        fuse_state_t *state = frame->root->state;

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": ERR => 0", frame->root->unique);
                fuse_reply_lock (state->req, lock);
        } else {
                if (op_errno == ENOSYS) {
                        gf_fuse_lk_enosys_log++;
                        if (!(gf_fuse_lk_enosys_log % GF_UNIVERSAL_ANSWER)) {
				gf_log ("glusterfs-fuse", GF_LOG_ERROR,
					"[ ERROR ] loading 'features/posix-locks' on server side may help your application");
                        }
                } else {
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
fuse_getlk (fuse_req_t req,
            fuse_ino_t ino,
            struct fuse_file_info *fi,
            struct flock *lock)
{
        fuse_state_t *state;
	fd_t *fd = NULL;
	
	fd = FI_TO_FD (fi);
        state = state_from_req (req);
        state->req = req;
	state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": GETLK %p", req_callid (req), fd);

        FUSE_FOP (state, fuse_getlk_cbk, GF_FOP_LK,
                  lk, fd, F_GETLK, lock);

        return;
}


static int
fuse_setlk_cbk (call_frame_t *frame,
                void *cookie,
                xlator_t *this,
                int32_t op_ret,
                int32_t op_errno,
                struct flock *lock)
{
        fuse_state_t *state = frame->root->state;

        if (op_ret == 0) {
                gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                        "%"PRId64": ERR => 0", frame->root->unique);
                fuse_reply_err (state->req, 0);
        } else {
                if (op_errno == ENOSYS) {
                        gf_fuse_lk_enosys_log++;
                        if (!(gf_fuse_lk_enosys_log % GF_UNIVERSAL_ANSWER)) {
				gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                        "[ ERROR ] loading 'features/posix-locks' on server side may help your application");
                        }
                } else  {
                        gf_log ("glusterfs-fuse",
				(op_errno == EAGAIN) ? GF_LOG_DEBUG : GF_LOG_ERROR,
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
fuse_setlk (fuse_req_t req,
            fuse_ino_t ino,
            struct fuse_file_info *fi,
            struct flock *lock,
            int sleep)
{
        fuse_state_t *state;
        fd_t *fd = NULL;
        
        fd = FI_TO_FD (fi);
        state = state_from_req (req);
        state->req = req;
        state->fd = fd;

        gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
                "%"PRId64": SETLK %p (sleep=%d)", req_callid (req), fd,
                sleep);

        FUSE_FOP (state, fuse_setlk_cbk, GF_FOP_LK,
                  lk, fd, (sleep ? F_SETLKW : F_SETLK), lock);

        return;
}


static void 
fuse_init (void *data, struct fuse_conn_info *conn)
{
        xlator_t *this_xl = NULL;
        
        if (data == NULL) {
                return ;
        }
        
        this_xl = data;
        
        this_xl->itable = inode_table_new (0, this_xl);
        
        return ;
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


static void *
fuse_thread_proc (void *data)
{
	char *mount_point = NULL;
        xlator_t *this = data;
        fuse_private_t *priv = this->private;
        int32_t res = 0;
        data_t *buf = priv->buf;
        int32_t ref = 0;
        size_t chan_size = fuse_chan_bufsize (priv->ch);
        char *recvbuf = calloc (1, chan_size);
        ERR_ABORT (recvbuf);

        while (!fuse_session_exited (priv->se)) {


                res = fuse_chan_receive (priv->ch,
                                         recvbuf,
                                         chan_size);

                if (res == -1) {
                        if (errno != EINTR) {
                                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                        "fuse_chan_receive() returned -1 (%d)", errno);
                        }
                        if (errno == ENODEV)
                                break;
                        continue;
                }

                buf = priv->buf;

                if (res && res != -1) {
                        if (buf->len < (res)) {
                                if (buf->data) {
                                        FREE (buf->data);
                                        buf->data = NULL;
                                }
                                buf->data = calloc (1, res);
                                ERR_ABORT (buf->data);
                                buf->len = res;
                        }
                        memcpy (buf->data, recvbuf, res); // evil evil

                        fuse_session_process (priv->se,
                                              buf->data,
                                              res,
                                              priv->ch);
                }

                LOCK (&buf->lock);
                ref = buf->refcount;
                UNLOCK (&buf->lock);
                if (1) {
                        data_unref (buf);

                        priv->buf = data_ref (data_from_dynptr (NULL, 0));
                }
        }
	if (dict_get (this->options, ZR_MOUNTPOINT_OPT))
		mount_point = data_to_str (dict_get (this->options, 
						     ZR_MOUNTPOINT_OPT));
	if (mount_point) {
		gf_log (this->name, GF_LOG_WARNING, 
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
notify (xlator_t *this, int32_t event,
        void *data, ...)
{
  
        switch (event)
        {
        case GF_EVENT_CHILD_UP:

#ifndef GF_DARWIN_HOST_OS
                /* This is because macfuse sends statfs() once the fuse thread
                   gets activated, and by that time if the client is not connected,
                   it give 'Device not configured' error. Hence, create thread only when 
                   client sends CHILD_UP (ie, client is connected).
                */

                /* TODO: somehow, try to get the mountpoint active as soon as init()
                   is complete, so that the hang effect when the server is not not
                   started is removed. */
        case GF_EVENT_CHILD_CONNECTING: 
#endif /* DARWIN */

        {
                fuse_private_t *private = this->private;
                int32_t ret = 0;

                if (!private->fuse_thread_started)
                {
                        private->fuse_thread_started = 1;

                        ret = pthread_create (&private->fuse_thread, NULL,
                                              fuse_thread_proc, this);

                        if (ret != 0)
                                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                        "pthread_create() failed (%s)", strerror (errno));
                        assert (ret == 0);
                }
                break;
        }
        case GF_EVENT_PARENT_UP:
        {
                default_notify (this, GF_EVENT_PARENT_UP, data);
        }
        default:
                break;
        }
        return 0;
}

int 
init (xlator_t *this_xl)
{
	int ret = 0;
	dict_t *options = NULL;
	char *value_string = NULL;
	fuse_private_t *priv = NULL;
	struct stat stbuf = {0,};

#ifdef GF_DARWIN_HOST_OS
        int fuse_argc = 9;
	char *fuse_argv[] = {"glusterfs",
			     "-o", "allow_other",
			     "-o", "default_permissions",
			     "-o", "fsname=glusterfs",
			     "-o", "local",
			     NULL};

#elif GF_LINUX_HOST_OS /* ! DARWIN_OS */
        int fuse_argc = 19;
	
	char *fuse_argv[] = {"glusterfs",
			     "-o", "nonempty",
			     "-o", "max_readahead=1048576",
			     "-o", "max_read=1048576",
			     "-o", "max_write=1048576",
			     "-o", "allow_other",
			     "-o", "default_permissions",
			     "-o", "fsname=glusterfs",
			     "-o", "dev",
			     "-o", "suid",
			     NULL};

#else /* BSD || SOLARIS */
	/* BSD fuse doesn't support '-o dev' option */
        int fuse_argc = 17;
	
	char *fuse_argv[] = {"glusterfs",
			     "-o", "nonempty",
			     "-o", "max_readahead=1048576",
			     "-o", "max_read=1048576",
			     "-o", "max_write=1048576",
			     "-o", "allow_other",
			     "-o", "default_permissions",
			     "-o", "fsname=glusterfs",
			     "-o", "suid",
			     NULL};

#endif /* ! DARWIN_OS || ! LINUX */
        struct fuse_args args = FUSE_ARGS_INIT (fuse_argc, fuse_argv);
	
	if (this_xl == NULL)
		return -1;
	
	if (this_xl->options == NULL)
		return -1;
	
	options = this_xl->options;
	
	if (this_xl->name == NULL)
		this_xl->name = strdup ("fuse");
	
        priv = calloc (1, sizeof (*priv));
        ERR_ABORT (priv);
        this_xl->private = (void *) priv;


#ifdef GF_DARWIN_HOST_OS
	if (dict_get (options, "non-local")) {
		/* This way, GlusterFS will be detected as 'servers' instead
		 *  of 'devices'. This method is useful if you want to do 
		 * 'umount <mount_point>' over network,  instead of 'eject'ing
		 * it from desktop. Works better for servers 
		 */
		/* Make the '-o local' in argv as NULL, so that its not 
		   in effect */
		fuse_argv[--args.argc] = NULL;
		fuse_argv[--args.argc] = NULL;
	}
#endif /* ! DARWIN */

	/* get options from option dictionary */
	ret = dict_get_str (options, ZR_MOUNTPOINT_OPT, &value_string);
	if (value_string == NULL) {
                gf_log ("fuse", GF_LOG_ERROR, 
			"mandatory option mount-point is not specified");
		return -1;
	}

	if (stat (value_string, &stbuf) != 0) {
		if (errno == ENOENT) {
			gf_log (this_xl->name, GF_LOG_ERROR ,
				"%s %s does not exist",
				ZR_MOUNTPOINT_OPT, value_string);
		} else if (errno == ENOTCONN) {
			gf_log (this_xl->name, GF_LOG_ERROR ,
				"mount-point %s seems to have a stale "
				"mount, run 'umount %s' and try again",
				value_string, value_string);
		} else {
			gf_log (this_xl->name, GF_LOG_ERROR ,
				"%s %s : stat returned %s",
				ZR_MOUNTPOINT_OPT,
				value_string, strerror (errno));
		}
		return -1;
	}
	
	if (S_ISDIR (stbuf.st_mode) == 0) {
		gf_log (this_xl->name, GF_LOG_ERROR ,
			"%s %s is not a directory",
			ZR_MOUNTPOINT_OPT, value_string);
		return -1;
	}
	priv->mount_point = strdup (value_string);
	
	ret = dict_get_uint32 (options, "attribute-timeout", 
			      &priv->attribute_timeout);
	if (!priv->attribute_timeout)
		priv->attribute_timeout = 1; /* default */
	
	ret = dict_get_uint32 (options, "entry-timeout", 
			       &priv->entry_timeout);
	if (!priv->entry_timeout)
		priv->entry_timeout = 1; /* default */
	

	priv->direct_io_mode = 1;
	ret = dict_get_str (options, ZR_DIRECT_IO_OPT, &value_string);
	if (value_string) {
		ret = gf_string2boolean (value_string, &priv->direct_io_mode);
	}

        priv->ch = fuse_mount (priv->mount_point, &args);
        if (priv->ch == NULL) {
                if (errno == ENOTCONN) {
                        gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                                "A stale mount present on %s. "
				"run 'umount %s' and try again",
                                priv->mount_point, 
				priv->mount_point);
                } else {
                        if (errno == ENOENT) {
                                gf_log ("glusterfs-fuse", GF_LOG_ERROR, 
                                        "unable to mount on %s. run "
					"'modprobe fuse' and try again", 
                                        priv->mount_point);
                        } else {
                                gf_log ("glusterfs-fuse", GF_LOG_ERROR, 
                                        "fuse_mount() failed with error %s "
					"on mount point %s", 
                                        strerror (errno), 
					priv->mount_point);
                        }
                }
                
                goto cleanup_exit;
        }
        
        priv->se = fuse_lowlevel_new (&args, &fuse_ops, 
				      sizeof (fuse_ops), this_xl);
        if (priv->se == NULL) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "fuse_lowlevel_new() failed with error %s on "
			"mount point %s", 
                        strerror (errno), priv->mount_point);
                goto umount_exit;
        }
        
        ret = fuse_set_signal_handlers (priv->se);
        if (ret == -1) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR, 
                        "fuse_set_signal_handlers() failed on mount point %s", 
                        priv->mount_point);
                goto umount_exit;
        }
        
        fuse_opt_free_args (&args);
        
        fuse_session_add_chan (priv->se, priv->ch);
        
        priv->fd = fuse_chan_fd (priv->ch);
        priv->buf = data_ref (data_from_dynptr (NULL, 0));
        
        return 0;
        
umount_exit: 
        fuse_unmount (priv->mount_point, priv->ch);
cleanup_exit:
        fuse_opt_free_args (&args);
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
		gf_log (this_xl->name, GF_LOG_WARNING, 
			"unmounting '%s'", mount_point);
		
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

struct xlator_options options[] = {
	{ "direct-io-mode", GF_OPTION_TYPE_BOOL, 0, 0, 0 },
	{ "non-local", GF_OPTION_TYPE_BOOL, 0, 0, 0 },
	{ "icon-name", GF_OPTION_TYPE_ANY, 0, 0, 0 },
	{ "mount-point", GF_OPTION_TYPE_PATH, 0, 0, 0 },
	{ "attribute-timeout", GF_OPTION_TYPE_INT, 0, 0, 3600, },
	{ "entry-timeout", GF_OPTION_TYPE_PATH, 0, 0, 3600, },
	{ NULL, 0, 0, 0 },
};
