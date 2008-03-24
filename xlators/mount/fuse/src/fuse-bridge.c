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

#define BIG_FUSE_CHANNEL_SIZE 1048576

struct fuse_private {
  int fd;
  struct fuse *fuse;
  struct fuse_session *se;
  struct fuse_chan *ch;
  char *mount_point;
  data_t *buf;
  pthread_t fuse_thread;

  uint32_t direct_io_mode;
  uint32_t entry_timeout;
  uint32_t attr_timeout;

};
typedef struct fuse_private fuse_private_t;

#define FI_TO_FD(fi) ((fd_t *)((long)fi->fh))

#define FUSE_FOP(state, ret, op_num, fop, args ...)             \
do {                                                            \
  call_frame_t *frame = get_call_frame_for_req (state, 1);      \
  xlator_t *xl = frame->this->children ?                        \
                        frame->this->children->xlator : NULL;   \
  dict_t *refs = frame->root->req_refs;                         \
  frame->root->state = state;                                   \
  frame->op   = op_num;                                         \
  STACK_WIND (frame, ret, xl, xl->fops->fop, args);             \
  dict_unref (refs);                                            \
} while (0)

#define FUSE_FOP_NOREPLY(state, op_num, fop, args ...)           \
do {                                                             \
  call_frame_t *_frame = get_call_frame_for_req (state, 0);      \
  xlator_t *xl = _frame->this->children->xlator;                 \
  _frame->root->req_refs = NULL;                                 \
  _frame->op   = op_num;                                         \
  STACK_WIND (_frame, fuse_nop_cbk, xl, xl->fops->fop, args);    \
} while (0)

typedef struct {
  loc_t loc;
  inode_t *parent;
  inode_t *inode;
  char *name;
} fuse_loc_t;

typedef struct {
  void *pool;
  xlator_t *this;
  inode_table_t *itable;
  fuse_loc_t fuse_loc;
  fuse_loc_t fuse_loc2;
  fuse_req_t req;

  int32_t flags;
  off_t off;
  size_t size;
  unsigned long nlookup;
  fd_t *fd;
  dict_t *dict;
  char *name;
  char is_revalidate;
} fuse_state_t;


static void
loc_wipe (loc_t *loc)
{
  if (loc->inode) {
    inode_unref (loc->inode);
    loc->inode = NULL;
  }
  if (loc->path) {
    freee (loc->path);
    loc->path = NULL;
  }
  
  if (loc->parent) {
    inode_unref (loc->parent);
    loc->parent = NULL;
  }
}


static inode_t *
dummy_inode (inode_table_t *table)
{
  inode_t *dummy;

  dummy = calloc (1, sizeof (*dummy));

  dummy->table = table;

  INIT_LIST_HEAD (&dummy->list);
  INIT_LIST_HEAD (&dummy->inode_hash);
  INIT_LIST_HEAD (&dummy->fds);
  INIT_LIST_HEAD (&dummy->dentry.name_hash);
  INIT_LIST_HEAD (&dummy->dentry.inode_list);

  dummy->ref = 1;
  dummy->ctx = get_new_dict ();

  LOCK_INIT (&dummy->lock);
  return dummy;
}

static void
fuse_loc_wipe (fuse_loc_t *fuse_loc)
{
  loc_wipe (&fuse_loc->loc);
  if (fuse_loc->name) {
    freee (fuse_loc->name);
    fuse_loc->name = NULL;
  }
  if (fuse_loc->inode) {
    inode_unref (fuse_loc->inode);
    fuse_loc->inode = NULL;
  }
  if (fuse_loc->parent) {
    inode_unref (fuse_loc->parent);
    fuse_loc->parent = NULL;
  }
}


static void
free_state (fuse_state_t *state)
{
  fuse_loc_wipe (&state->fuse_loc);

  fuse_loc_wipe (&state->fuse_loc2);

  if (state->dict) {
    dict_unref (state->dict);
    state->dict = (void *)0xaaaaeeee;
  }
  if (state->name) {
    freee (state->name);
    state->name = NULL;
  }
#ifdef DEBUG
  memset (state, 0x90, sizeof (*state));
#endif
  freee (state);
  state = NULL;
}


static int32_t
fuse_nop_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno)
{
  if (frame->root->state)
    free_state (frame->root->state);

  frame->root->state = EEEEKS;
  STACK_DESTROY (frame->root);
  return 0;
}

fuse_state_t *
state_from_req (fuse_req_t req)
{
  fuse_state_t *state;
  xlator_t *this = fuse_req_userdata (req);

  state = (void *)calloc (1, sizeof (*state));
  state->pool = this->ctx->pool;
  state->itable = this->itable;
  state->req = req;
  state->this = this;

  return state;
}


static call_frame_t *
get_call_frame_for_req (fuse_state_t *state, char d)
{
  call_pool_t *pool = state->pool;
  fuse_req_t req = state->req;
  const struct fuse_ctx *ctx = NULL;
  call_ctx_t *cctx = NULL;
  xlator_t *this = NULL;
  fuse_private_t *priv = NULL;

  cctx = calloc (1, sizeof (*cctx));
  cctx->frames.root = cctx;

  if (req) {
    ctx = fuse_req_ctx(req);

    cctx->uid = ctx->uid;
    cctx->gid = ctx->gid;
    cctx->pid = ctx->pid;
    cctx->unique = req_callid (req);
  }

  if (req) {
    this = fuse_req_userdata (req);
    cctx->frames.this = this;
    priv = this->private;
  } else {
    cctx->frames.this = state->this;
  }

  if (d) {
    cctx->req_refs = dict_ref (get_new_dict ());
    dict_set (cctx->req_refs, NULL, priv->buf);
    cctx->req_refs->is_locked = 1;
  }

  cctx->pool = pool;
  LOCK (&pool->lock);
  list_add (&cctx->all_frames, &pool->all_frames);
  UNLOCK (&pool->lock);

  cctx->frames.type = GF_OP_TYPE_FOP_REQUEST;

  return &cctx->frames;
}


static void
fuse_loc_fill (fuse_loc_t *fuse_loc,
	       fuse_state_t *state,
	       ino_t ino,
	       const char *name)
{
  size_t n;
  inode_t *inode, *parent = NULL;

  /* resistance against multiple invocation of loc_fill not to get
     reference leaks via inode_search() */
  inode = fuse_loc->inode;
  if (!inode) {
    inode = inode_search (state->itable, ino, name);
  }
  fuse_loc->inode = inode;

  if (name) {
    if (!fuse_loc->name)
      fuse_loc->name = strdup (name);

    parent = fuse_loc->parent;
    if (!parent) {
      if (inode)
	parent = inode_parent (inode, ino);
      else
	parent = inode_search (state->itable, ino, NULL);
    }
  }

  fuse_loc->parent = parent;

  if (inode) {
    fuse_loc->loc.inode = inode_ref (inode);
    fuse_loc->loc.ino = inode->ino;
  }
  
  if (name) {
    n = inode_path (parent, name, NULL, 0) + 1;
    fuse_loc->loc.path = calloc (1, n);
    inode_path (parent, name, (char *)fuse_loc->loc.path, n);
  } else {
    n = inode_path (inode, NULL, NULL, 0) + 1;
    fuse_loc->loc.path = calloc (1, n);
    inode_path (inode, NULL, (char *)fuse_loc->loc.path, n);
  }
  
}

static int32_t
fuse_lookup_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *stat,
		 dict_t *dict);

static int32_t
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

  if (!op_ret) {
    if (inode->ino == 1)
      buf->st_ino = 1;
  }

  if (!op_ret && inode && inode->ino && buf && inode->ino != buf->st_ino) {
    /* temporary workaround to handle AFR returning differnt inode number */
    gf_log ("glusterfs-fuse", GF_LOG_WARNING,
	    "%"PRId64": (%d) %s => inode number changed %"PRId64" -> %"PRId64,
	    frame->root->unique, frame->op, state->fuse_loc.loc.path,
	    inode->ino, buf->st_ino);
    inode_unref (state->fuse_loc.loc.inode);
    state->fuse_loc.loc.inode = dummy_inode (state->itable);
    state->is_revalidate = 2;

    STACK_WIND (frame, fuse_lookup_cbk,
		FIRST_CHILD (this), FIRST_CHILD (this)->fops->lookup,
		&state->fuse_loc.loc, 
		0);

    return 0;
  }

  if (op_ret == 0) {
    ino_t ino = buf->st_ino;
    inode_t *fuse_inode;

    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": (%d) %s => %"PRId64, frame->root->unique,
	    frame->op, state->fuse_loc.loc.path, ino);

  try_again:
    fuse_inode = inode_update (state->itable, state->fuse_loc.parent,
			       state->fuse_loc.name, buf);

    if (fuse_inode->ctx) {
      /* if the inode was already in the hash, checks to flush out
	 old name hashes */
      if ((fuse_inode->st_mode ^ buf->st_mode) & S_IFMT) {
	gf_log ("glusterfs-fuse", GF_LOG_WARNING,
		"%"PRId64": (%d) %s => %"PRId64" Rehashing %x/%x",
		frame->root->unique, frame->op,
		state->fuse_loc.loc.path, ino, (S_IFMT & buf->st_ino),
		(S_IFMT & fuse_inode->st_mode));

	fuse_inode->st_mode = buf->st_mode;
	inode_unhash_name (state->itable, fuse_inode);
	inode_unref (fuse_inode);
	goto try_again;
      }
      if (buf->st_nlink == 1 || S_ISDIR (buf->st_mode)) {
	/* no other name hashes should exist */
	if (!list_empty (&fuse_inode->dentry.inode_list)) {
	  gf_log ("glusterfs-fuse", GF_LOG_WARNING,
		  "%"PRId64": (%d) %s => %"PRId64" Rehashing because st_nlink less than dentry maps",
		  frame->root->unique, frame->op,
		  state->fuse_loc.loc.path, ino);
	  inode_unhash_name (state->itable, fuse_inode);
	  inode_unref (fuse_inode);
	  goto try_again;
	}
	if ((state->fuse_loc.parent != fuse_inode->dentry.parent) || 
	    ( state->fuse_loc.name && fuse_inode->dentry.name && 
	    strcmp (state->fuse_loc.name, fuse_inode->dentry.name))) {
	  gf_log ("glusterfs-fuse", GF_LOG_WARNING,
		  "%"PRId64": (%d) %s => %"PRId64" Rehashing because single st_nlink does not match dentry map",
		  frame->root->unique, frame->op,
		  state->fuse_loc.loc.path, ino);
	  inode_unhash_name (state->itable, fuse_inode);
	  inode_unref (fuse_inode);
	  goto try_again;
	}
      }
    }

    if ((fuse_inode->ctx != inode->ctx) &&
	list_empty (&fuse_inode->fds)) {
      dict_t *swap = inode->ctx;
      inode->ctx = fuse_inode->ctx;
      fuse_inode->ctx = swap;
      fuse_inode->generation = inode->generation;
      fuse_inode->st_mode = buf->st_mode;
    }

    inode_lookup (fuse_inode);

    inode_unref (fuse_inode);

    /* TODO: make these timeouts configurable (via meta?) */
    e.ino = fuse_inode->ino;

#ifdef GF_DARWIN_HOST_OS
    e.generation = 0;
#else
    e.generation = buf->st_ctime;
#endif

    e.entry_timeout = priv->entry_timeout;
    e.attr_timeout = priv->attr_timeout;
    e.attr = *buf;
    e.attr.st_blksize = BIG_FUSE_CHANNEL_SIZE; 
    if (state->fuse_loc.parent)
      fuse_reply_entry (req, &e);
    else
      fuse_reply_attr (req, buf, priv->attr_timeout);
  } else {
    if (state->is_revalidate == -1 && op_errno == ENOENT) {
      gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	      "%"PRId64": (%d) %s => -1 (%d)", frame->root->unique,
	      frame->op, state->fuse_loc.loc.path, op_errno);
    } else {
      gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	      "%"PRId64": (%d) %s => -1 (%d)", frame->root->unique,
	      frame->op, state->fuse_loc.loc.path, op_errno);
    }

    if (state->is_revalidate == 1) {
      inode_unref (state->fuse_loc.loc.inode);
      state->fuse_loc.loc.inode = dummy_inode (state->itable);
      state->is_revalidate = 2;

      STACK_WIND (frame, fuse_lookup_cbk,
		  FIRST_CHILD (this), FIRST_CHILD (this)->fops->lookup,
		  &state->fuse_loc.loc, 0);

      return 0;
    }

    fuse_reply_err (req, op_errno);
  }

  free_state (state);
  STACK_DESTROY (frame->root);
  return 0;
}


static int32_t
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

  state = state_from_req (req);

  fuse_loc_fill (&state->fuse_loc, state, par, name);

  if (!state->fuse_loc.loc.inode) {
    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": LOOKUP %s", req_callid (req),
	    state->fuse_loc.loc.path);

    state->fuse_loc.loc.inode = dummy_inode (state->itable);
    /* to differntiate in entry_cbk what kind of call it is */
    state->is_revalidate = -1;
  } else {
    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": LOOKUP %s(%"PRId64")", req_callid (req),
	    state->fuse_loc.loc.path, state->fuse_loc.loc.inode->ino);
    state->is_revalidate = 1;
  }

  FUSE_FOP (state, fuse_lookup_cbk, GF_FOP_LOOKUP,
	    lookup, &state->fuse_loc.loc, 0);
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
  inode_forget (fuse_inode, nlookup);
  inode_unref (fuse_inode);

  free_state (state);
  fuse_reply_none (req);
}


static int32_t
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
    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": (%d) %s => %"PRId64, frame->root->unique, frame->op,
	    state->fuse_loc.loc.path ? state->fuse_loc.loc.path : "ERR",
	    buf->st_ino);
    /* TODO: make these timeouts configurable via meta */
    /* TODO: what if the inode number has changed by now */ 
    buf->st_blksize = BIG_FUSE_CHANNEL_SIZE;
    fuse_reply_attr (req, buf, priv->attr_timeout);
  } else {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": (%d) %s => -1 (%d)", frame->root->unique, frame->op,
	    state->fuse_loc.loc.path ? state->fuse_loc.loc.path : "ERR",
	    op_errno);
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

  state = state_from_req (req);

  if (ino == 1) {
    fuse_loc_fill (&state->fuse_loc, state, ino, NULL);
    if (state->fuse_loc.loc.inode)
      state->is_revalidate = 1;
    else
      state->is_revalidate = -1;
    FUSE_FOP (state, fuse_lookup_cbk, GF_FOP_LOOKUP,
	      lookup, &state->fuse_loc.loc, 0);
    return;
  }

  fuse_loc_fill (&state->fuse_loc, state, ino, NULL);
  if (!state->fuse_loc.loc.inode) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": GETATTR %"PRId64" (%s) (fuse_loc_fill() returned NULL inode)", 
	    req_callid (req), (int64_t)ino, state->fuse_loc.loc.path);
    fuse_reply_err (req, EINVAL);
    return;
  }

  if (list_empty (&state->fuse_loc.loc.inode->fds) || 
      S_ISDIR (state->fuse_loc.loc.inode->st_mode)) {

    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": GETATTR %"PRId64" (%s)",
	    req_callid (req), (int64_t)ino, state->fuse_loc.loc.path);
    
    FUSE_FOP (state,
	      fuse_attr_cbk,
	      GF_FOP_STAT,
	      stat,
	      &state->fuse_loc.loc);
  } else {
    fd_t *fd  = list_entry (state->fuse_loc.loc.inode->fds.next,
			    fd_t, inode_list);

    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": FGETATTR %"PRId64" (%s/%p)",
	    req_callid (req), (int64_t)ino, state->fuse_loc.loc.path, fd);

    FUSE_FOP (state,fuse_attr_cbk, GF_FOP_FSTAT,
	      fstat, fd);
  }
}


static int32_t
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
  fd = state->fd;

  if (op_ret >= 0) {
    struct fuse_file_info fi = {0, };

    LOCK (&fd->inode->lock);
    list_add (&fd->inode_list, &fd->inode->fds);
    UNLOCK (&fd->inode->lock);

    fi.fh = (unsigned long) fd;
    fi.flags = state->flags;

    if (!S_ISDIR (fd->inode->st_mode)) {
      if ((fi.flags & 3) && priv->direct_io_mode)
	  fi.direct_io = 1;
    }

    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": (%d) %s => %p", frame->root->unique, frame->op,
	    state->fuse_loc.loc.path, fd);

    if (fuse_reply_open (req, &fi) == -ENOENT) {
      gf_log ("glusterfs-fuse", GF_LOG_WARNING, "open() got EINTR");
      state->req = 0;

      if (S_ISDIR (fd->inode->st_mode))
	FUSE_FOP_NOREPLY (state, GF_FOP_CLOSEDIR, closedir, fd);
      else
	FUSE_FOP_NOREPLY (state, GF_FOP_CLOSE, close, fd);
    }
  } else {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": (%d) %s => -1 (%d)", frame->root->unique,
	    frame->op, state->fuse_loc.loc.path, op_errno);
    fuse_reply_err (req, op_errno);
    fd_destroy (fd);
  }

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

  if (fi) {
    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": FCHMOD %p", req_callid (req), FI_TO_FD (fi));

    FUSE_FOP (state,
	      fuse_attr_cbk,
	      GF_FOP_FCHMOD,
	      fchmod,
	      FI_TO_FD (fi),
	      attr->st_mode);
  } else {
    fuse_loc_fill (&state->fuse_loc, state, ino, NULL);
    if (!state->fuse_loc.loc.inode) {
      gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	      "%"PRId64": CHMOD %"PRId64" (%s) (fuse_loc_fill() returned NULL inode)", 
	      req_callid (req), (int64_t)ino, state->fuse_loc.loc.path);
      fuse_reply_err (req, EINVAL);
      return;
    }


    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": CHMOD %s", req_callid (req),
	    state->fuse_loc.loc.path);

    FUSE_FOP (state,
	      fuse_attr_cbk,
	      GF_FOP_CHMOD,
	      chmod,
	      &state->fuse_loc.loc,
	      attr->st_mode);
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

  uid_t uid = (valid & FUSE_SET_ATTR_UID) ? attr->st_uid : (uid_t) -1;
  gid_t gid = (valid & FUSE_SET_ATTR_GID) ? attr->st_gid : (gid_t) -1;

  state = state_from_req (req);

  if (fi) {
    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": FCHOWN %p", req_callid (req), FI_TO_FD (fi));

    FUSE_FOP (state,
	      fuse_attr_cbk,
	      GF_FOP_FCHOWN,
	      fchown,
	      FI_TO_FD (fi),
	      uid,
	      gid);
  } else {
    fuse_loc_fill (&state->fuse_loc, state, ino, NULL);
    if (!state->fuse_loc.loc.inode) {
      gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	      "%"PRId64": CHOWN %"PRId64" (%s) (fuse_loc_fill() returned NULL inode)", 
	      req_callid (req), (int64_t)ino, state->fuse_loc.loc.path);
      fuse_reply_err (req, EINVAL);
      return;
    }

    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": CHOWN %s", req_callid (req),
	    state->fuse_loc.loc.path);

    FUSE_FOP (state,
	      fuse_attr_cbk,
	      GF_FOP_CHOWN,
	      chown,
	      &state->fuse_loc.loc,
	      uid,
	      gid);
  }
}

static void 
do_truncate (fuse_req_t req,
	     fuse_ino_t ino,
	     struct stat *attr,
	     struct fuse_file_info *fi)
{
  fuse_state_t *state;

  state = state_from_req (req);

  if (fi) {
    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": FTRUNCATE %p/%"PRId64, req_callid (req),
	    FI_TO_FD (fi), attr->st_size);

    FUSE_FOP (state,
	      fuse_attr_cbk,
	      GF_FOP_FTRUNCATE,
	      ftruncate,
	      FI_TO_FD (fi),
	      attr->st_size);
  } else {
    fuse_loc_fill (&state->fuse_loc, state, ino, NULL);
    if (!state->fuse_loc.loc.inode) {
      gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	      "%"PRId64": TRUNCATE %s/%"PRId64" (fuse_loc_fill() returned NULL inode)", 
	      req_callid (req), state->fuse_loc.loc.path, attr->st_size);
      fuse_reply_err (req, EINVAL);
      return;
    }

    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": TRUNCATE %s/%"PRId64, req_callid (req),
	    state->fuse_loc.loc.path, attr->st_size);

    FUSE_FOP (state,
	      fuse_attr_cbk,
	      GF_FOP_TRUNCATE,
	      truncate,
	      &state->fuse_loc.loc,
	      attr->st_size);
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
  fuse_loc_fill (&state->fuse_loc, state, ino, NULL);
  if (!state->fuse_loc.loc.inode) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": UTIMENS %s (fuse_loc_fill() returned NULL inode)", 
	    req_callid (req), state->fuse_loc.loc.path);
    fuse_reply_err (req, EINVAL);
    return;
  }

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": UTIMENS %s", req_callid (req),
	  state->fuse_loc.loc.path);

  FUSE_FOP (state,
	    fuse_attr_cbk,
	    GF_FOP_UTIMENS,
	    utimens,
	    &state->fuse_loc.loc,
	    tv);
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
  else if ((valid & (FUSE_SET_ATTR_ATIME | FUSE_SET_ATTR_MTIME)) == (FUSE_SET_ATTR_ATIME | FUSE_SET_ATTR_MTIME))
    do_utimes (req, ino, attr);

  if (!valid)
    fuse_getattr (req, ino, fi);
}


static int32_t
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
	    "%"PRId64": (%d) %s => 0", frame->root->unique, frame->op, 
	    state->fuse_loc.loc.path ? state->fuse_loc.loc.path : "ERR");
    fuse_reply_err (req, 0);
  } else {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": (%d) %s => -1 (%d)", frame->root->unique, frame->op,
	    state->fuse_loc.loc.path ? state->fuse_loc.loc.path : "ERR",
	    op_errno);
    fuse_reply_err (req, op_errno);
  }

  if (state->fd)
    fd_destroy (state->fd);

  free_state (state);
  STACK_DESTROY (frame->root);

  return 0;
}



static int32_t
fuse_unlink_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  fuse_state_t *state = frame->root->state;
  fuse_req_t req = state->req;

  if (op_ret == 0)
    inode_unlink (state->itable, state->fuse_loc.parent, state->fuse_loc.name);

  if (op_ret == 0) {
    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": (%d) %s => 0", frame->root->unique,
	    frame->op, state->fuse_loc.loc.path);

    fuse_reply_err (req, 0);
  } else {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": (%d) %s => -1 (%d)", frame->root->unique,
	    frame->op, state->fuse_loc.loc.path, op_errno);

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

  state = state_from_req (req);

  fuse_loc_fill (&state->fuse_loc, state, ino, NULL);
  if (!state->fuse_loc.loc.inode) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": ACCESS %"PRId64" (%s) (fuse_loc_fill() returned NULL inode)", 
	    req_callid (req), (int64_t)ino, state->fuse_loc.loc.path);
    fuse_reply_err (req, EINVAL);
    return;
  }

  FUSE_FOP (state,
	    fuse_err_cbk,
	    GF_FOP_ACCESS,
	    access,
	    &state->fuse_loc.loc,
	    mask);

  return;
}



static int32_t
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
	    state->fuse_loc.loc.path, linkname);

      fuse_reply_readlink(req, linkname);
  } else {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": %s => -1 (%d)", frame->root->unique,
	    state->fuse_loc.loc.path, op_errno);
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

  state = state_from_req (req);
  fuse_loc_fill (&state->fuse_loc, state, ino, NULL);
  if (!state->fuse_loc.loc.inode) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64" READLINK %s/%"PRId64" (fuse_loc_fill() returned NULL inode)", 
	    req_callid (req), state->fuse_loc.loc.path, state->fuse_loc.loc.inode->ino);
    fuse_reply_err (req, EINVAL);
    return;
  }
  
  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64" READLINK %s/%"PRId64, req_callid (req),
	  state->fuse_loc.loc.path, state->fuse_loc.loc.inode->ino);

  FUSE_FOP (state,
	    fuse_readlink_cbk,
	    GF_FOP_READLINK,
	    readlink,
	    &state->fuse_loc.loc,
	    4096);

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

  state = state_from_req (req);
  fuse_loc_fill (&state->fuse_loc, state, par, name);

  state->fuse_loc.loc.inode = dummy_inode (state->itable);

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": MKNOD %s", req_callid (req),
	  state->fuse_loc.loc.path);

  FUSE_FOP (state,
	    fuse_entry_cbk,
	    GF_FOP_MKNOD,
	    mknod,
	    &state->fuse_loc.loc,
	    mode,
	    rdev);

  return;
}


static void 
fuse_mkdir (fuse_req_t req,
	    fuse_ino_t par,
	    const char *name,
	    mode_t mode)
{
  fuse_state_t *state;

  state = state_from_req (req);
  fuse_loc_fill (&state->fuse_loc, state, par, name);

  state->fuse_loc.loc.inode = dummy_inode (state->itable);

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": MKDIR %s", req_callid (req),
	  state->fuse_loc.loc.path);

  FUSE_FOP (state,
	    fuse_entry_cbk,
	    GF_FOP_MKDIR,
	    mkdir,
	    &state->fuse_loc.loc,
	    mode);

  return;
}


static void 
fuse_unlink (fuse_req_t req,
	     fuse_ino_t par,
	     const char *name)
{
  fuse_state_t *state;

  state = state_from_req (req);

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": UNLINK %s", req_callid (req),
	  state->fuse_loc.loc.path);

  fuse_loc_fill (&state->fuse_loc, state, par, name);
  if (!state->fuse_loc.loc.inode) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": UNLINK %s (fuse_loc_fill() returned NULL inode)", req_callid (req),
	    state->fuse_loc.loc.path);
    fuse_reply_err (req, EINVAL);
    return;
  }

  FUSE_FOP (state,
	    fuse_unlink_cbk,
	    GF_FOP_UNLINK,
	    unlink,
	    &state->fuse_loc.loc);

  return;
}


static void 
fuse_rmdir (fuse_req_t req,
	    fuse_ino_t par,
	    const char *name)
{
  fuse_state_t *state;

  state = state_from_req (req);
  fuse_loc_fill (&state->fuse_loc, state, par, name);
  if (!state->fuse_loc.loc.inode) {
    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": RMDIR %s (fuse_loc_fill() returned NULL inode)", req_callid (req),
	    state->fuse_loc.loc.path);
    fuse_reply_err (req, EINVAL);
    return;
  }

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": RMDIR %s", req_callid (req),
	  state->fuse_loc.loc.path);

  FUSE_FOP (state,
	    fuse_unlink_cbk,
	    GF_FOP_RMDIR,
	    rmdir,
	    &state->fuse_loc.loc);

  return;
}


static void
fuse_symlink (fuse_req_t req,
	      const char *linkname,
	      fuse_ino_t par,
	      const char *name)
{
  fuse_state_t *state;

  state = state_from_req (req);
  fuse_loc_fill (&state->fuse_loc, state, par, name);

  state->fuse_loc.loc.inode = dummy_inode (state->itable);

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": SYMLINK %s -> %s", req_callid (req),
	  state->fuse_loc.loc.path, linkname);

  FUSE_FOP (state,
	    fuse_entry_cbk,
	    GF_FOP_SYMLINK,
	    symlink,
	    linkname,
	    &state->fuse_loc.loc);
  return;
}


int32_t
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
	    "%"PRId64": %s -> %s => 0", frame->root->unique,
	    state->fuse_loc.loc.path,
	    state->fuse_loc2.loc.path);

    inode_t *inode;
    {
      /* ugly ugly - to stay blind to situation where
	 rename happens on a new inode
      */
      buf->st_ino = state->fuse_loc.loc.ino;
    }
    inode = inode_rename (state->itable,
			  state->fuse_loc.parent,
			  state->fuse_loc.name,
			  state->fuse_loc2.parent,
			  state->fuse_loc2.name,
			  buf);

    inode_unref (inode);
    fuse_reply_err (req, 0);
  } else {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": %s -> %s => -1 (%d)", frame->root->unique,
	    state->fuse_loc.loc.path,
	    state->fuse_loc2.loc.path, op_errno);
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

  state = state_from_req (req);

  fuse_loc_fill (&state->fuse_loc, state, oldpar, oldname);
  if (!state->fuse_loc.loc.inode) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "for %s %"PRId64": RENAME `%s' -> `%s' (fuse_loc_fill() returned NULL inode)",
	    state->fuse_loc.loc.path, req_callid (req), state->fuse_loc.loc.path,
	    state->fuse_loc2.loc.path);
    
    fuse_reply_err (req, EINVAL);
    return;
  }

  fuse_loc_fill (&state->fuse_loc2, state, newpar, newname);

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": RENAME `%s' -> `%s'",
	  req_callid (req), state->fuse_loc.loc.path,
	  state->fuse_loc2.loc.path);

  FUSE_FOP (state,
	    fuse_rename_cbk,
	    GF_FOP_RENAME,
	    rename,
	    &state->fuse_loc.loc,
	    &state->fuse_loc2.loc);

  return;
}


static void
fuse_link (fuse_req_t req,
	   fuse_ino_t ino,
	   fuse_ino_t par,
	   const char *name)
{
  fuse_state_t *state;

  state = state_from_req (req);

  fuse_loc_fill (&state->fuse_loc, state, par, name);
  fuse_loc_fill (&state->fuse_loc2, state, ino, NULL);
  if (!state->fuse_loc2.loc.inode) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "fuse_loc_fill() returned NULL inode for %s %"PRId64": LINK %s %s", 
	    state->fuse_loc2.loc.path, req_callid (req), 
	    state->fuse_loc2.loc.path, state->fuse_loc.loc.path);
    fuse_reply_err (req, EINVAL);
    return;
  }

  state->fuse_loc.loc.inode = inode_ref (state->fuse_loc2.loc.inode);
  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": LINK %s %s", req_callid (req),
	  state->fuse_loc2.loc.path, state->fuse_loc.loc.path);

  FUSE_FOP (state,
	    fuse_entry_cbk,
	    GF_FOP_LINK,
	    link,
	    &state->fuse_loc2.loc,
	    state->fuse_loc.loc.path);

  return;
}


static int32_t
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

  fd = state->fd;

  fi.flags = state->flags;
  if (op_ret >= 0) {
    inode_t *fuse_inode;
    fi.fh = (unsigned long) fd;

    if ((fi.flags & 3) && priv->direct_io_mode)
      fi.direct_io = 1;

    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": (%d) %s => %p", frame->root->unique, frame->op,
	    state->fuse_loc.loc.path, fd);

    fuse_inode = inode_update (state->itable,
			       state->fuse_loc.parent,
			       state->fuse_loc.name,
			       buf);
    if (fuse_inode->ctx) {
      inode_unhash_name (state->itable, fuse_inode);
      inode_unref (fuse_inode);

      fuse_inode = inode_update (state->itable,
                                 state->fuse_loc.parent,
                                 state->fuse_loc.name,
                                 buf);
    }


    {
      if (fuse_inode->ctx != inode->ctx) {
	dict_t *swap = inode->ctx;
	inode->ctx = fuse_inode->ctx;
	fuse_inode->ctx = swap;
	fuse_inode->generation = inode->generation;
	fuse_inode->st_mode = buf->st_mode;
      }

      inode_lookup (fuse_inode);

      /*      list_del (&fd->inode_list); */

      LOCK (&fuse_inode->lock);
      list_add (&fd->inode_list, &fuse_inode->fds);
      inode_unref (fd->inode);
      fd->inode = inode_ref (fuse_inode);
      UNLOCK (&fuse_inode->lock);

      //      inode_destroy (inode);
    }

    inode_unref (fuse_inode);

    e.ino = fuse_inode->ino;

#ifdef GF_DARWIN_HOST_OS
    e.generation = 0;
#else
    e.generation = buf->st_ctime;
#endif

    e.entry_timeout = priv->entry_timeout;
    e.attr_timeout = priv->attr_timeout;
    e.attr = *buf;
    e.attr.st_blksize = BIG_FUSE_CHANNEL_SIZE;

    fi.keep_cache = 0;

    //    if (fi.flags & 1)
    //      fi.direct_io = 1;

    if (fuse_reply_create (req, &e, &fi) == -ENOENT) {
      gf_log ("glusterfs-fuse", GF_LOG_WARNING, "create() got EINTR");
      /* TODO: forget this node too */
      state->req = 0;
      FUSE_FOP_NOREPLY (state, GF_FOP_CLOSE, close, fd);
    }
  } else {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": %s => -1 (%d)", req_callid (req),
	    state->fuse_loc.loc.path, op_errno);
    fuse_reply_err (req, op_errno);
    fd_destroy (fd);
  }

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

  state = state_from_req (req);
  state->flags = fi->flags;

  fuse_loc_fill (&state->fuse_loc, state, par, name);
  state->fuse_loc.loc.inode = dummy_inode (state->itable);

  fd = fd_create (state->fuse_loc.loc.inode);
  state->fd = fd;


  LOCK (&fd->inode->lock);
  list_del_init (&fd->inode_list);
  UNLOCK (&fd->inode->lock);

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": CREATE %s", req_callid (req),
	  state->fuse_loc.loc.path);

  FUSE_FOP (state,
	    fuse_create_cbk,
	    GF_FOP_CREATE,
	    create,
	    &state->fuse_loc.loc,
	    state->flags,
	    mode, fd);

  return;
}


static void
fuse_open (fuse_req_t req,
	   fuse_ino_t ino,
	   struct fuse_file_info *fi)
{
  fuse_state_t *state;
  fd_t *fd;

  state = state_from_req (req);
  state->flags = fi->flags;

  fuse_loc_fill (&state->fuse_loc, state, ino, NULL);
  if (!state->fuse_loc.loc.inode) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": OPEN %s (fuse_loc_fill() returned NULL inode)", req_callid (req),
	    state->fuse_loc.loc.path);
  
    fuse_reply_err (req, EINVAL);
    return;
  }


  fd = fd_create (state->fuse_loc.loc.inode);
  state->fd = fd;

  LOCK (&fd->inode->lock);
  list_del_init (&fd->inode_list);
  UNLOCK (&fd->inode->lock);

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": OPEN %s", req_callid (req),
	  state->fuse_loc.loc.path);

  FUSE_FOP (state,
	    fuse_fd_cbk,
	    GF_FOP_OPEN,
	    open,
	    &state->fuse_loc.loc,
	    fi->flags, fd);

  return;
}


static int32_t
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
	    "%"PRId64": READ => %d/%d,%"PRId64"/%"PRId64, frame->root->unique,
	    op_ret, state->size, state->off, stbuf->st_size);

    fuse_reply_vec (req, vector, count);
  } else {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": READ => -1 (%d)", frame->root->unique, op_errno);

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

  state = state_from_req (req);
  state->size = size;
  state->off = off;

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": READ (%p, size=%d, offset=%"PRId64")",
	  req_callid (req), FI_TO_FD (fi), size, off);

  FUSE_FOP (state,
	    fuse_readv_cbk,
	    GF_FOP_READ,
	    readv,
	    FI_TO_FD (fi),
	    size,
	    off);

}


static int32_t
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
	    "%"PRId64": WRITE => %d/%d,%"PRId64"/%"PRId64, frame->root->unique,
	    op_ret, state->size, state->off, stbuf->st_size);

    fuse_reply_write (req, op_ret);
  } else {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": WRITE => -1 (%d)", frame->root->unique, op_errno);

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

  state = state_from_req (req);
  state->size = size;
  state->off = off;

  vector.iov_base = (void *)buf;
  vector.iov_len = size;

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": WRITE (%p, size=%d, offset=%"PRId64")",
	  req_callid (req), FI_TO_FD (fi), size, off);

  FUSE_FOP (state,
	    fuse_writev_cbk,
	    GF_FOP_WRITE,
	    writev,
	    FI_TO_FD (fi),
	    &vector,
	    1,
	    off);
  return;
}


static void
fuse_flush (fuse_req_t req,
	    fuse_ino_t ino,
	    struct fuse_file_info *fi)
{
  fuse_state_t *state;

  state = state_from_req (req);

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": FLUSH %p", req_callid (req), FI_TO_FD (fi));

  FUSE_FOP (state,
	    fuse_err_cbk,
	    GF_FOP_FLUSH,
	    flush,
	    FI_TO_FD (fi));

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

  LOCK (&state->fd->inode->lock);
  list_del_init (&state->fd->inode_list);
  UNLOCK (&state->fd->inode->lock);

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": CLOSE %p", req_callid (req), FI_TO_FD (fi));

  FUSE_FOP (state, fuse_err_cbk, GF_FOP_CLOSE, close, state->fd);
  return;
}


static void 
fuse_fsync (fuse_req_t req,
	    fuse_ino_t ino,
	    int datasync,
	    struct fuse_file_info *fi)
{
  fuse_state_t *state;

  state = state_from_req (req);

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": FSYNC %p", req_callid (req), FI_TO_FD (fi));

  FUSE_FOP (state,
	    fuse_err_cbk,
	    GF_FOP_FSYNC,
	    fsync,
	    FI_TO_FD (fi),
	    datasync);

  return;
}

static void
fuse_opendir (fuse_req_t req,
	      fuse_ino_t ino,
	      struct fuse_file_info *fi)
{
  fuse_state_t *state;
  fd_t *fd;

  state = state_from_req (req);
  fuse_loc_fill (&state->fuse_loc, state, ino, NULL);
  if (!state->fuse_loc.loc.inode) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": OPEN %s (fuse_loc_fill() returned NULL inode)", req_callid (req),
	    state->fuse_loc.loc.path);
  
    fuse_reply_err (req, EINVAL);
    return;
  }


  fd = fd_create (state->fuse_loc.loc.inode);
  state->fd = fd;

  LOCK (&fd->inode->lock);
  list_del_init (&fd->inode_list);
  UNLOCK (&fd->inode->lock);

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": OPEN %s", req_callid (req),
	  state->fuse_loc.loc.path);

  FUSE_FOP (state,
	    fuse_fd_cbk,
	    GF_FOP_OPENDIR,
	    opendir,
	    &state->fuse_loc.loc, fd);
}

#if 0

void
fuse_dir_reply (fuse_req_t req,
		size_t size,
		off_t off,
		fd_t *fd)
{
  char *buf;
  size_t size_limited;
  data_t *buf_data;

  buf_data = dict_get (fd->ctx, "__fuse__getdents__internal__@@!!");
  buf = buf_data->data;
  size_limited = size;

  if (size_limited > (buf_data->len - off))
    size_limited = (buf_data->len - off);

  if (off > buf_data->len) {
    size_limited = 0;
    off = 0;
  }

  fuse_reply_buf (req, buf + off, size_limited);
}


static int32_t
fuse_getdents_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   dir_entry_t *entries,
		   int32_t count)
{
  fuse_state_t *state = frame->root->state;
  fuse_req_t req = state->req;

  if (op_ret < 0) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": READDIR => -1 (%d)",
	    frame->root->unique, op_errno);

    fuse_reply_err (state->req, op_errno);
  } else {
    dir_entry_t *trav;
    size_t size = 0;
    char *buf;
    data_t *buf_data;

    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": READDIR => %d entries",
	    frame->root->unique, count);

    for (trav = entries->next; trav; trav = trav->next) {
      size += fuse_add_direntry (req, NULL, 0, trav->name, NULL, 0);
    }

    buf = calloc (1, size);
    buf_data = data_from_dynptr (buf, size);
    size = 0;

    for (trav = entries->next; trav; trav = trav->next) {
      size_t entry_size;
      entry_size = fuse_add_direntry (req, NULL, 0, trav->name, NULL, 0);
      fuse_add_direntry (req, buf + size, entry_size, trav->name,
			 &trav->buf, entry_size + size);
      size += entry_size;
    }

    dict_set (state->fd->ctx,
	      "__fuse__getdents__internal__@@!!",
	      buf_data);

    fuse_dir_reply (state->req, state->size, state->off, state->fd);
  }

  free_state (state);
  STACK_DESTROY (frame->root);

  return 0;
}

static void
fuse_getdents (fuse_req_t req,
	       fuse_ino_t ino,
	       struct fuse_file_info *fi,
	       size_t size,
	       off_t off,
	       int32_t flag)
{
  fuse_state_t *state;
  fd_t *fd = FI_TO_FD (fi);

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": GETDENTS %p", req_callid (req), FI_TO_FD (fi));

  if (!off)
    dict_del (fd->ctx, "__fuse__getdents__internal__@@!!");

  if (dict_get (fd->ctx, "__fuse__getdents__internal__@@!!")) {
    fuse_dir_reply (req, size, off, fd);
    return;
  }

  state = state_from_req (req);

  state->size = size;
  state->off = off;
  state->fd = fd;

  FUSE_FOP (state,
	    fuse_getdents_cbk,
	    GF_FOP_GETDENTS,
	    getdents,
	    fd,
	    size,
	    off,
	    0);
}

#endif

static int32_t
fuse_readdir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  gf_dirent_t *buf)
{
  fuse_state_t *state = frame->root->state;
  fuse_req_t req = state->req;

  if (op_ret >= 0) {
    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": READDIR => %d/%d,%"PRId64, frame->root->unique,
	    op_ret, state->size, state->off);

    fuse_reply_buf (req, (void *)buf, op_ret);
  } else {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": READDIR => -1 (%d)", frame->root->unique, op_errno);

    fuse_reply_err (req, op_errno);
  }

  free_state (state);
  STACK_DESTROY (frame->root);

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

  state = state_from_req (req);
  state->size = size;
  state->off = off;

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": READDIR (%p, size=%d, offset=%"PRId64")",
	  req_callid (req), FI_TO_FD (fi), size, off);

  FUSE_FOP (state,
	    fuse_readdir_cbk,
	    GF_FOP_READDIR,
	    readdir,
	    FI_TO_FD (fi),
	    size,
	    off);
}


static void
fuse_releasedir (fuse_req_t req,
		 fuse_ino_t ino,
		 struct fuse_file_info *fi)
{
  fuse_state_t *state;

  state = state_from_req (req);
  state->fd = FI_TO_FD (fi);

  LOCK (&state->fd->inode->lock);
  list_del_init (&state->fd->inode_list);
  UNLOCK (&state->fd->inode->lock);

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": CLOSEDIR %p", req_callid (req), FI_TO_FD (fi));

  FUSE_FOP (state, fuse_err_cbk, GF_FOP_CLOSEDIR, closedir, state->fd);
}


static void 
fuse_fsyncdir (fuse_req_t req,
	       fuse_ino_t ino,
	       int datasync,
	       struct fuse_file_info *fi)
{
  fuse_state_t *state;

  state = state_from_req (req);

  FUSE_FOP (state,
	    fuse_err_cbk,
	    GF_FOP_FSYNCDIR,
	    fsyncdir,
	    FI_TO_FD (fi),
	    datasync);

  return;
}


static int32_t
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
	    "%"PRId64": ERR => -1 (%d)", frame->root->unique, op_errno);
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

  state = state_from_req (req);
  fuse_loc_fill (&state->fuse_loc, state, 1, NULL);
  if (!state->fuse_loc.loc.inode) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": STATFS (fuse_loc_fill() returned NULL inode)", req_callid (req));
    
    fuse_reply_err (req, EINVAL);
    return;
  }

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": STATFS", req_callid (req));

  FUSE_FOP (state,
	    fuse_statfs_cbk,
	    GF_FOP_STATFS,
	    statfs,
	    &state->fuse_loc.loc);
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

  state = state_from_req (req);
  state->size = size;
  fuse_loc_fill (&state->fuse_loc, state, ino, NULL);
  if (!state->fuse_loc.loc.inode) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": SETXATTR %s/%"PRId64" (%s) (fuse_loc_fill() returned NULL inode)", 
	    req_callid (req),
	    state->fuse_loc.loc.path, (int64_t)ino, name);

    fuse_reply_err (req, EINVAL);
    return;
  }

  state->dict = get_new_dict ();

  dict_set (state->dict, (char *)name,
	    bin_to_data ((void *)value, size));
  dict_ref (state->dict);

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": SETXATTR %s/%"PRId64" (%s)", req_callid (req),
	  state->fuse_loc.loc.path, (int64_t)ino, name);

  FUSE_FOP (state,
	    fuse_err_cbk,
	    GF_FOP_SETXATTR,
	    setxattr,
	    &state->fuse_loc.loc,
	    state->dict,
	    flags);

  return;
}


static int32_t
fuse_xattr_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		dict_t *dict)
{
  int32_t ret = op_ret;
  char *value = "";
  fuse_state_t *state = frame->root->state;
  fuse_req_t req = state->req;

  if (ret >= 0) {
    gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	    "%"PRId64": (%d) %s => %d", frame->root->unique,
	    frame->op, state->fuse_loc.loc.path, op_ret);

    /* if successful */
    if (state->name) {
      /* if callback for getxattr */
      data_t *value_data = dict_get (dict, state->name);
      if (value_data) {
	ret = value_data->len; /* Don't return the value for '\0' */
	value = value_data->data;
	
	if (state->size) {
	  /* if callback for getxattr and asks for value */
	  fuse_reply_buf (req, value, ret);
	} else {
	  /* if callback for getxattr and asks for value length only */
	  fuse_reply_xattr (req, ret);
	}
      } else {
	fuse_reply_err (req, ENODATA);
      }
    } else {
      /* if callback for listxattr */
      int32_t len = 0;
      data_pair_t *trav = dict->members_list;
      while (trav) {
	len += strlen (trav->key) + 1;
	trav = trav->next;
      }
      value = alloca (len + 1);
      len = 0;
      trav = dict->members_list;
      while (trav) {
	strcpy (value + len, trav->key);
	value[len + strlen(trav->key)] = '\0';
	len += strlen (trav->key) + 1;
	trav = trav->next;
      }
      if (state->size) {
	/* if callback for listxattr and asks for list of keys */
	fuse_reply_buf (req, value, len);
      } else {
	/* if callback for listxattr and asks for length of keys only */
	fuse_reply_xattr (req, len);
      }
    }
  } else {
    /* if failure - no need to check if listxattr or getxattr */
    if (op_errno != ENODATA) {
      gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	      "%"PRId64": (%d) %s => -1 (%d)", frame->root->unique,
	      frame->op, state->fuse_loc.loc.path, op_errno);
    } else {
      gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	      "%"PRId64": (%d) %s => -1 (%d)", frame->root->unique,
	      frame->op, state->fuse_loc.loc.path, op_errno);
    }

    fuse_reply_err (req, op_errno);
  }

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

  state = state_from_req (req);
  state->size = size;
  state->name = strdup (name);
  fuse_loc_fill (&state->fuse_loc, state, ino, NULL);
  if (!state->fuse_loc.loc.inode) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": GETXATTR %s/%"PRId64" (%s) (fuse_loc_fill() returned NULL inode)", 
	    req_callid (req), state->fuse_loc.loc.path, (int64_t)ino, name);

    fuse_reply_err (req, EINVAL);
    return;
  }

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": GETXATTR %s/%"PRId64" (%s)", req_callid (req),
	  state->fuse_loc.loc.path, (int64_t)ino, name);

  FUSE_FOP (state,
	    fuse_xattr_cbk,
	    GF_FOP_GETXATTR,
	    getxattr,
	    &state->fuse_loc.loc);

  return;
}


static void
fuse_listxattr (fuse_req_t req,
		fuse_ino_t ino,
		size_t size)
{
  fuse_state_t *state;

  state = state_from_req (req);
  state->size = size;
  fuse_loc_fill (&state->fuse_loc, state, ino, NULL);
  if (!state->fuse_loc.loc.inode) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": LISTXATTR %s/%"PRId64" (fuse_loc_fill() returned NULL inode)", 
	    req_callid (req), state->fuse_loc.loc.path, (int64_t)ino);

    fuse_reply_err (req, EINVAL);
    return;
  }

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": LISTXATTR %s/%"PRId64, req_callid (req),
	  state->fuse_loc.loc.path, (int64_t)ino);

  FUSE_FOP (state,
	    fuse_xattr_cbk,
	    GF_FOP_GETXATTR,
	    getxattr,
	    &state->fuse_loc.loc);

  return;
}


static void
fuse_removexattr (fuse_req_t req,
		  fuse_ino_t ino,
		  const char *name)

{
  fuse_state_t *state;

  state = state_from_req (req);
  fuse_loc_fill (&state->fuse_loc, state, ino, NULL);
  if (!state->fuse_loc.loc.inode) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": REMOVEXATTR %s/%"PRId64" (%s) (fuse_loc_fill() returned NULL inode)",
	    req_callid (req), state->fuse_loc.loc.path, (int64_t)ino, name);

    fuse_reply_err (req, EINVAL);
    return;
  }

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": REMOVEXATTR %s/%"PRId64" (%s)", req_callid (req),
	  state->fuse_loc.loc.path, (int64_t)ino, name);

  FUSE_FOP (state,
	    fuse_err_cbk,
	    GF_FOP_REMOVEXATTR,
	    removexattr,
	    &state->fuse_loc.loc,
	    name);

  return;
}

static int32_t
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
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": ERR => -1 (%d)", frame->root->unique, op_errno);
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

  state = state_from_req (req);
  state->req = req;

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": GETLK %p", req_callid (req), FI_TO_FD (fi));

  FUSE_FOP (state,
	    fuse_getlk_cbk,
	    GF_FOP_LK,
	    lk,
	    FI_TO_FD (fi),
	    F_GETLK,
	    lock);

  return;
}

static int32_t
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
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "%"PRId64": ERR => -1 (%d)", frame->root->unique, op_errno);
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

  state = state_from_req (req);
  state->req = req;

  gf_log ("glusterfs-fuse", GF_LOG_DEBUG,
	  "%"PRId64": SETLK %p (sleep=%d)", req_callid (req), FI_TO_FD (fi),
	  sleep);

  FUSE_FOP (state,
	    fuse_setlk_cbk,
	    GF_FOP_LK,
	    lk,
	    FI_TO_FD(fi),
	    (sleep ? F_SETLKW : F_SETLK),
	    lock);

  return;
}



static void
fuse_init (void *data, struct fuse_conn_info *conn)
{
  xlator_t *this = data;
  struct fuse_private *priv = this->private;

  if (!this->name)
    this->name = "fuse";

  if (!priv->attr_timeout)
    priv->attr_timeout = 1.0;

  if (!priv->entry_timeout)
    priv->entry_timeout = 1.0;

  this->itable = inode_table_new (0, this);
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
  xlator_t *this = data;
  struct fuse_private *priv = this->private;
  int32_t res = 0;
  data_t *buf = priv->buf;
  int32_t ref = 0;
  size_t chan_size = fuse_chan_bufsize (priv->ch);
  char *recvbuf = calloc (1, chan_size);

  while (!fuse_session_exited (priv->se)) {
    int32_t fuse_chan_receive (struct fuse_chan * ch,
			       char *buf,
			       int32_t size);


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
	  freee (buf->data);
	  buf->data = NULL;
	}
	buf->data = calloc (1, res);
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
      priv->buf->is_locked = 1;
    }
  }

  fuse_session_remove_chan (priv->ch);
  fuse_session_destroy (priv->se);
  //  fuse_unmount (priv->mount_point, priv->ch);

  exit (0);

  return NULL;
}


int32_t
notify (xlator_t *this, int32_t event,
	void *data, ...)
{

  return 0;
}

int32_t
init (xlator_t *this)
{
  dict_t *options = NULL;
  char *mount_point = NULL;
  char *source;
  int argc;
  struct fuse_private *priv = NULL;
  int32_t res;

  options = this->options;
  asprintf (&source, "fsname=glusterfs");

  char *argv[] = { "glusterfs",

#ifndef GF_DARWIN_HOST_OS
		   "-o", "nonempty",
#else
		   "-o", "noexec",
#endif
		   "-o", "allow_other",
		   "-o", "default_permissions",
		   "-o", source,
		   "-o", "max_readahead=1048576",
		   "-o", "max_read=1048576",
		   "-o", "max_write=1048576",
		   NULL };

#ifdef GF_DARWIN_HOST_OS
  argc = 15;
#else
  argc = 15;
#endif

  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  priv = calloc (1, sizeof (*priv));

  this->private = (void *)priv;

  if (!data_to_str (dict_get (options, "mount-point"))) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "'option mount-point /directory' not specified");
    return -1;
  }

  mount_point = strdup (data_to_str (dict_get (options, 
					      "mount-point")));

  if (dict_get (options, "attr-timeout")) {
    priv->attr_timeout = data_to_uint32 (dict_get (options,
						   "attr-timeout"));
  }

  if (dict_get (options, "entry-timeout")) {
    priv->entry_timeout = data_to_uint32 (dict_get (options,
						    "entry-timeout"));
  }

  if (dict_get (options, "direct-io-mode")) {
    priv->direct_io_mode = data_to_uint32 (dict_get (options,
						     "direct-io-mode"));
  }

  priv->ch = fuse_mount (mount_point, &args);

  if (!priv->ch) {
    gf_log ("glusterfs-fuse",
	    GF_LOG_ERROR, "fuse_mount failed (%s)\n", strerror (errno));
    fuse_opt_free_args(&args);
    goto err_free;
  }

  priv->se = fuse_lowlevel_new (&args, &fuse_ops, sizeof (fuse_ops), this);

  if (!priv->se) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "fuse_lowlevel_new failed (%s)\n", strerror (errno));
    fuse_opt_free_args (&args);
    goto err_free;
  }

  fuse_opt_free_args(&args);

  res = fuse_set_signal_handlers (priv->se);
  if (res == -1) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR, "fuse_set_signal_handlers failed");
    goto err;
  }

  fuse_session_add_chan (priv->se, priv->ch);

  priv->fd = fuse_chan_fd (priv->ch);
  priv->buf = data_ref (data_from_dynptr (NULL, 0));
  priv->buf->is_locked = 1;

  priv->mount_point = mount_point;

  if (pthread_create (&priv->fuse_thread, NULL, fuse_thread_proc, this) != 0) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR,
	    "pthread_create() failed (%s)", strerror (errno));
    goto err;
  }

  (this->children->xlator)->notify (this->children->xlator, 
				    GF_EVENT_PARENT_UP, this);
  return 0;

 err: 
  fuse_unmount (mount_point, priv->ch);
 err_free:
  freee (mount_point);
  mount_point = NULL;

  return -1;
}

void
fini (xlator_t *this)
{


}

struct xlator_fops fops = {
};

struct xlator_mops mops = {
};
