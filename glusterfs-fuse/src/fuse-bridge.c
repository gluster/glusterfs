/*
  (C) 2006,2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 


#include <stdint.h>
#include <signal.h>
#include <pthread.h>

#include "glusterfs.h"
#include "logging.h"
#include "xlator.h"
#include "glusterfs.h"
#include "transport.h"

#include <fuse/fuse_lowlevel.h>

#include "fuse-extra.h"

#define FI_TO_FD(fi) ((fd_t *)((long)fi->fh))

#define FUSE_FOP(state, ret, op, args ...)                      \
do {                                                            \
  call_frame_t *frame = get_call_frame_for_req (state, 1);      \
  xlator_t *xl = frame->this->children ?                        \
                        frame->this->children->xlator : NULL;   \
  dict_t *refs = frame->root->req_refs;                         \
  frame->root->state = state;                                   \
  STACK_WIND (frame, ret, xl, xl->fops->op, args);              \
  dict_unref (refs);                                            \
} while (0)

#define FUSE_FOP_NOREPLY(state, op, args ...)                    \
do {                                                             \
  call_frame_t *_frame = get_call_frame_for_req (state, 0);      \
  xlator_t *xl = _frame->this->children->xlator;                 \
  _frame->root->req_refs = NULL;                                 \
  STACK_WIND (_frame, fuse_nop_cbk, xl, xl->fops->op, args);     \
} while (0)

typedef struct {
  loc_t loc;
  inode_t *parent;
  inode_t *inode;
  char *name;
} fuse_loc_t;

typedef struct {
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

} fuse_state_t;

/* TODO: ensure inode->private is unref'd whenever needed */

static void
loc_wipe (loc_t *loc)
{
  if (loc->inode) {
    inode_unref (loc->inode);
    loc->inode = NULL;
  }
  if (loc->path) {
    free ((char *)loc->path);
    loc->path = NULL;
  }
}


static void
fuse_loc_wipe (fuse_loc_t *fuse_loc)
{
  loc_wipe (&fuse_loc->loc);
  if (fuse_loc->name) {
    free (fuse_loc->name);
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

  free (state);
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

  STACK_DESTROY (frame->root);
  return 0;
}

fuse_state_t *
state_from_req (fuse_req_t req)
{
  fuse_state_t *state;
  transport_t *trans = fuse_req_userdata (req);

  state = (void *)calloc (1, sizeof (*state));
  state->itable = trans->xl->itable;
  state->req = req;
  state->this = trans->xl;

  return state;
}


static call_frame_t *
get_call_frame_for_req (fuse_state_t *state, char d)
{
  fuse_req_t req = state->req;
  const struct fuse_ctx *ctx = NULL;
  call_ctx_t *cctx = NULL;
  transport_t *trans = NULL;

  cctx = calloc (1, sizeof (*cctx));
  cctx->frames.root = cctx;

  if (req) {
    ctx = fuse_req_ctx(req);

    cctx->uid = ctx->uid;
    cctx->gid = ctx->gid;
    cctx->pid = ctx->pid;
  }

  if (req) {
    trans = fuse_req_userdata (req);
    cctx->frames.this = trans->xl;
  } else {
    cctx->frames.this = state->this;
  }

  if (d) {
    cctx->req_refs = dict_ref (get_new_dict ());
    cctx->req_refs->lock = calloc (1, sizeof (pthread_mutex_t));
    pthread_mutex_init (cctx->req_refs->lock, NULL);
    dict_set (cctx->req_refs, NULL, trans->buf);
  }

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
  if (!inode)
    inode = inode_search (state->itable, ino, name);
  fuse_loc->inode = inode;

  if (name) {
    if (!fuse_loc->name)
      fuse_loc->name = strdup (name);

    parent = fuse_loc->parent;
    if (!parent)
      parent = inode_search (state->itable, ino, NULL);
  }
  fuse_loc->parent = parent;

  if (inode) {
    fuse_loc->loc.inode = inode_ref (inode->private);
    fuse_loc->loc.ino = inode->ino;
  }

  if (parent) {
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

  state = frame->root->state;
  req = state->req;

  if (op_ret == 0) {
    inode_t *fuse_inode;

    gf_log ("glusterfs-fuse",
	    GF_LOG_DEBUG,
	    "ENTRY => %ld", inode->ino);
    fuse_inode = inode_update (state->itable,
			       state->fuse_loc.parent,
			       state->fuse_loc.name,
			       inode->ino);

    /* TODO: what if fuse_inode->private already exists and != inode */
    if (!fuse_inode->private)
      fuse_inode->private = inode_ref (inode);
    inode_lookup (fuse_inode);
    inode_unref (fuse_inode);

    /* TODO: make these timeouts configurable (via meta?) */
    e.ino = fuse_inode->ino;
    e.entry_timeout = 1.0;
    e.attr_timeout = 1.0;
    e.attr = *buf;
    fuse_reply_entry (req, &e);
  } else {
    gf_log ("glusterfs-fuse",
	    GF_LOG_DEBUG,
	    "ERR => -1 (%d)", op_errno);
    fuse_reply_err (req, op_errno);
  }

  free_state (state);
  STACK_DESTROY (frame->root);
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

  gf_log ("glusterfs-fuse",
	  GF_LOG_DEBUG,
	  "LOOKUP %ld/%s (%s)", par, name, state->fuse_loc.loc.path);

  FUSE_FOP (state,
	    fuse_entry_cbk,
	    lookup,
	    &state->fuse_loc.loc);
}


static void
fuse_forget (fuse_req_t req,
	     fuse_ino_t ino,
	     unsigned long nlookup)
{
  fuse_state_t *state;
  inode_t *fuse_inode, *inode;
  char last_forget = 0;

  if (ino == 1) {
    fuse_reply_none (req);
    return;
  }

  state = state_from_req (req);
  fuse_inode = inode_search (state->itable, ino, NULL);
  inode = fuse_inode->private;
  inode_forget (fuse_inode, nlookup);
  last_forget = (fuse_inode->nlookup == 0);
  inode_unref (fuse_inode);

  if (last_forget) {
    inode_unref (inode);
    FUSE_FOP_NOREPLY (state,
		      forget,
		      inode);
  }

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

  state = frame->root->state;
  req = state->req;

  if (op_ret == 0) {
    /* TODO: make these timeouts configurable via meta */
    /* TODO: what if the inode number has changed by now */ 
    fuse_reply_attr (req, buf, 0.1);
  } else {
    fuse_reply_err (req, ENOENT);
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

  if (!fi) {
    fuse_loc_fill (&state->fuse_loc, state, ino, NULL);

    gf_log ("glusterfs-fuse",
	    GF_LOG_DEBUG,
	    "GETATTR %ld (%s)", ino, state->fuse_loc.loc.path);

    FUSE_FOP (state,
	      fuse_attr_cbk,
	      stat,
	      &state->fuse_loc.loc);
  } else {
    FUSE_FOP (state,
	      fuse_attr_cbk,
	      fstat,
	      FI_TO_FD (fi));
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

  state = frame->root->state;
  req = state->req;

  if (op_ret >= 0) {
    struct fuse_file_info fi = {0, };
    fi.fh = (unsigned long) fd;
    if (fuse_reply_open (req, &fi) == -ENOENT) {
      /* TODO: this should be releasedir if call was for opendir */
      state->req = 0;
      FUSE_FOP_NOREPLY (state, close, fd);
    }
  } else {
    fuse_reply_err (req, op_errno);
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
    FUSE_FOP (state,
	      fuse_attr_cbk,
	      fchmod,
	      FI_TO_FD (fi),
	      attr->st_mode);
  } else {
    fuse_loc_fill (&state->fuse_loc, state, ino, NULL);

    FUSE_FOP (state,
	      fuse_attr_cbk,
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
    FUSE_FOP (state,
	      fuse_attr_cbk,
	      fchown,
	      FI_TO_FD (fi),
	      uid,
	      gid);
  } else {
    fuse_loc_fill (&state->fuse_loc, state, ino, NULL);

    FUSE_FOP (state,
	      fuse_attr_cbk,
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
    FUSE_FOP (state,
	      fuse_attr_cbk,
	      ftruncate,
	      FI_TO_FD (fi),
	      attr->st_size);
  } else {
    fuse_loc_fill (&state->fuse_loc, state, ino, NULL);

    FUSE_FOP (state,
	      fuse_attr_cbk,
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

  FUSE_FOP (state,
	    fuse_attr_cbk,
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

  if (op_ret == 0)
    fuse_reply_err (req, 0);
  else
    fuse_reply_err (req, op_errno);

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

  FUSE_FOP (state,
	    fuse_err_cbk,
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
    fuse_reply_readlink(req, linkname);
  } else {
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

  FUSE_FOP (state,
	    fuse_readlink_cbk,
	    readlink,
	    &state->fuse_loc.loc,
	    PATH_MAX);

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

  FUSE_FOP (state,
	    fuse_entry_cbk,
	    mknod,
	    state->fuse_loc.loc.path,
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

  FUSE_FOP (state,
	    fuse_entry_cbk,
	    mkdir,
	    state->fuse_loc.loc.path,
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

  gf_log ("glusterfs-fuse",
	  GF_LOG_DEBUG,
	  "UNLINK %ld/%s", par, name);

  fuse_loc_fill (&state->fuse_loc, state, par, name);

  FUSE_FOP (state,
	    fuse_err_cbk,
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

  FUSE_FOP (state,
	    fuse_err_cbk,
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

  FUSE_FOP (state,
	    fuse_entry_cbk,
	    symlink,
	    linkname,
	    state->fuse_loc.loc.path);
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
    /* TODO: call inode_rename (); */
    inode_rename (state->itable,
		  state->fuse_loc.parent,
		  state->fuse_loc.name,
		  state->fuse_loc2.parent,
		  state->fuse_loc2.name,
		  buf->st_ino);

    fuse_reply_err (req, 0);
  } else {
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
  fuse_loc_fill (&state->fuse_loc2, state, newpar, newname);

  gf_log ("glusterfs-fuse",
	  GF_LOG_DEBUG,
	  "RENAME `%s' -> `%s'",
	  state->fuse_loc.loc.path, state->fuse_loc2.loc.path);

  FUSE_FOP (state,
	    fuse_rename_cbk,
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

  fuse_loc_fill (&state->fuse_loc, state, ino, NULL);
  fuse_loc_fill (&state->fuse_loc2, state, par, name);

  gf_log ("glusterfs-fuse",
	  GF_LOG_DEBUG,
	  "LINK %s %s", state->fuse_loc.loc.path, state->fuse_loc2.loc.path);

  FUSE_FOP (state,
	    fuse_entry_cbk,
	    link,
	    &state->fuse_loc.loc,
	    state->fuse_loc2.loc.path);

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

  struct fuse_file_info fi = {0, };
  struct fuse_entry_param e;

  fi.flags = state->flags;
  if (op_ret >= 0) {
    inode_t *fuse_inode;
    fi.fh = (long) fd;

    fuse_inode = inode_update (state->itable,
			       state->fuse_loc.parent,
			       state->fuse_loc.name,
			       buf->st_ino);
    fuse_inode->private = inode_ref (inode);
    inode_lookup (fuse_inode);
    inode_unref (fuse_inode);

    e.ino = inode->ino;
    e.entry_timeout = 1.0;
    e.attr_timeout = 1.0;
    e.attr = *buf;

    fi.keep_cache = 0;

    if (fi.flags & 1)
      fi.direct_io = 1;

    if (fuse_reply_create (req, &e, &fi) == -ENOENT) {
      /* TODO: forget this node too */
      state->req = 0;
      FUSE_FOP_NOREPLY (state, close, fd);
    }
  } else {
    fuse_reply_err (req, op_errno);
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

  state = state_from_req (req);
  state->flags = fi->flags;
  fuse_loc_fill (&state->fuse_loc, state, par, name);
  
  FUSE_FOP (state,
	    fuse_create_cbk,
	    create,
	    state->fuse_loc.loc.path,
	    state->flags,
	    mode);

  return;
}


static void
fuse_open (fuse_req_t req,
	   fuse_ino_t ino,
	   struct fuse_file_info *fi)
{
  fuse_state_t *state;

  state = state_from_req (req);
  state->flags = fi->flags;
  fuse_loc_fill (&state->fuse_loc, state, ino, NULL);

  FUSE_FOP (state,
	    fuse_fd_cbk,
	    open,
	    &state->fuse_loc.loc,
	    fi->flags);

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
    if ((size_t) op_ret > state->size)
      fprintf (stderr, "fuse: read too many bytes");

    fuse_reply_vec (req, vector, count);
  } else {
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

  FUSE_FOP (state,
	    fuse_readv_cbk,
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
		 int32_t op_errno)
{
  fuse_state_t *state = frame->root->state;
  fuse_req_t req = state->req;

  if (op_ret >= 0) {
    if ((size_t) op_ret > state->size)
      fprintf(stderr, "fuse: wrote too many bytes");

    fuse_reply_write (req, op_ret);
  } else {
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

  FUSE_FOP (state,
	    fuse_writev_cbk,
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

  FUSE_FOP (state,
	    fuse_err_cbk,
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

  FUSE_FOP_NOREPLY (state, close, FI_TO_FD (fi));

  fuse_reply_err(req, 0);
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

  state = state_from_req (req);

  FUSE_FOP (state,
	    fuse_err_cbk,
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

  state = state_from_req (req);
  fuse_loc_fill (&state->fuse_loc, state, ino, NULL);

  FUSE_FOP (state,
	    fuse_fd_cbk,
	    opendir,
	    &state->fuse_loc.loc);
}

void
fuse_dir_reply (fuse_req_t req,
		size_t size,
		off_t off,
		fd_t *fd)
{
  char *buf;
  size_t size_limited;
  data_t *buf_data;

  buf_data = dict_get (fd->ctx, "__fuse__readdir__internal__@@!!");
  buf = buf_data->data;
  size_limited = size;

  if (size_limited > (buf_data->len - off))
    size_limited = (buf_data->len - off);

  fuse_reply_buf (req, buf + off, size_limited);
}


static int32_t
fuse_readdir_cbk (call_frame_t *frame,
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
    fuse_reply_err (state->req, op_errno);
  } else {
    dir_entry_t *trav;
    size_t size = 0;
    char *buf;
    data_t *buf_data;

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
	      "__fuse__readdir__internal__@@!!",
	      buf_data);

    fuse_dir_reply (state->req, state->size, state->off, state->fd);
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
  fd_t *fd = FI_TO_FD (fi);

  if (dict_get (fd->ctx, "__fuse__readdir__internal__@@!!")) {
    fuse_dir_reply (req, size, off, fd);
    return;
  }

  state = state_from_req (req);

  state->size = size;
  state->off = off;
  state->fd = fd;

  FUSE_FOP (state,
	    fuse_readdir_cbk,
	    readdir,
	    size,
	    off,
	    fd);
}


static void
fuse_releasedir (fuse_req_t req,
		 fuse_ino_t ino,
		 struct fuse_file_info *fi)
{
  fuse_state_t *state;

  state = state_from_req (req);
  state->fd = FI_TO_FD (fi);

  FUSE_FOP_NOREPLY (state, closedir, state->fd);

  fuse_reply_err (req, 0);
  free_state (state);
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

  if (op_ret == 0)
    fuse_reply_statfs (req, buf);
  else
    fuse_reply_err (req, op_errno);

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

  FUSE_FOP (state,
	    fuse_statfs_cbk,
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

  FUSE_FOP (state,
	    fuse_err_cbk,
	    setxattr,
	    &state->fuse_loc.loc,
	    name,
	    value,
	    size,
	    flags);

  return;
}


static int32_t
fuse_xattr_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		void *value)
{
  fuse_state_t *state = frame->root->state;
  fuse_req_t req = state->req;

  if (state->size) {
    /* TODO: check for op_ret > state->size */
    if (op_ret > 0)
      fuse_reply_buf (req, value, op_ret);
    else 
      fuse_reply_err (req, op_errno);
  } else {
    if (op_ret >= 0)
      fuse_reply_xattr (req, op_ret);
    else
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
  fuse_loc_fill (&state->fuse_loc, state, ino, NULL);

  FUSE_FOP (state,
	    fuse_xattr_cbk,
	    getxattr,
	    &state->fuse_loc.loc,
	    name,
	    size);

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

  FUSE_FOP (state,
	    fuse_xattr_cbk,
	    listxattr,
	    &state->fuse_loc.loc,
	    size);

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

  FUSE_FOP (state,
	    fuse_err_cbk,
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

  if (op_ret == 0)
    fuse_reply_lock (state->req, lock);
  else
    fuse_reply_err (state->req, op_errno);

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

  FUSE_FOP (state,
	    fuse_getlk_cbk,
	    lk,
	    FI_TO_FD(fi),
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

  if (op_ret == 0)
    fuse_reply_err (state->req, 0);
  else
    fuse_reply_err (state->req, op_errno);

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

  FUSE_FOP (state,
	    fuse_setlk_cbk,
	    lk,
	    FI_TO_FD(fi),
	    (sleep ? F_SETLKW : F_SETLK),
	    lock);

  return;
}


static void
fuse_init (void *data, struct fuse_conn_info *conn)
{
  transport_t *trans = data;
  xlator_t *xl = trans->xl;
  int32_t ret;

  xl->itable = inode_table_new (0, "fuse");
  ret = xlator_tree_init (xl);
  if (ret == 0) {
    xl->itable->root->private = xl->children->xlator->itable->root;
  }
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


struct fuse_private {
  int fd;
  struct fuse *fuse;
  struct fuse_session *se;
  struct fuse_chan *ch;
  char *mountpoint;
};

static int32_t
fuse_transport_disconnect (transport_t *this)
{
  struct fuse_private *priv = this->private;

  gf_log ("glusterfs-fuse",
	  GF_LOG_DEBUG,
	  "cleaning up fuse transport in disconnect handler");

  fuse_session_remove_chan (priv->ch);
  fuse_session_destroy (priv->se);
  fuse_unmount (priv->mountpoint, priv->ch);

  free (priv);
  priv = NULL;
  this->private = NULL;

  /* TODO: need graceful exit. every xlator should be ->fini()'ed
     and come out of main poll loop cleanly
  */
  exit (0);

  return -1;
}


static int32_t
fuse_transport_init (transport_t *this,
		     dict_t *options,
		     int32_t (*notify) (xlator_t *xl,
					transport_t *trans,
					int32_t event))
{
  char *mountpoint = strdup (data_to_str (dict_get (options, 
						    "mountpoint")));
  char *source;
  asprintf (&source, "fsname=glusterfs");
  char *argv[] = { "glusterfs",
                   "-o", "nonempty",
                   "-o", "allow_other",
                   "-o", "default_permissions",
		   "-o", source,
		   "-o", "max_readahead=1048576",
		   "-o", "max_read=1048576",
		   "-o", "max_write=1048576",
                   NULL };
  int argc = 15;

  struct fuse_args args = FUSE_ARGS_INIT(argc,
					 argv);
  struct fuse_private *priv = calloc (1, sizeof (*priv));
  int32_t res;

  this->notify = notify;
  this->private = (void *)priv;

  priv->ch = fuse_mount (mountpoint, &args);
  if (!priv->ch) {
    gf_log ("glusterfs-fuse",
	    GF_LOG_ERROR, "fuse_mount failed (%s)\n", strerror (errno));
    fuse_opt_free_args(&args);
    goto err_free;
  }

  priv->se = fuse_lowlevel_new (&args, &fuse_ops, sizeof (fuse_ops), this);
  fuse_opt_free_args(&args);

  res = fuse_set_signal_handlers (priv->se);
  if (res == -1) {
    gf_log ("glusterfs-fuse", GF_LOG_ERROR, "fuse_set_signal_handlers failed");
    goto err;
  }

  fuse_session_add_chan (priv->se, priv->ch);

  priv->fd = fuse_chan_fd (priv->ch);
  this->buf = data_ref (data_from_dynptr (NULL, 0));
  this->buf->lock = calloc (1, sizeof (pthread_mutex_t));
  pthread_mutex_init (this->buf->lock, NULL);

  priv->mountpoint = mountpoint;

  poll_register (this->xl_private, priv->fd, this);

  return 0;

 err: 
    fuse_unmount (mountpoint, priv->ch);
 err_free:
    free (mountpoint);
    mountpoint = NULL;
  return -1;
}


static void *
fuse_thread_proc (void *data)
{
  transport_t *trans = data;
  struct fuse_private *priv = trans->private;
  int32_t res = 0;
  data_t *buf = trans->buf;
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
      transport_disconnect (trans);
    }

    if (res && res != -1) {
      buf = trans->buf;

      if (buf->len < (res)) {
	if (buf->data) {
	  free (buf->data);
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

    pthread_mutex_lock (buf->lock);
    ref = buf->refcount;
    pthread_mutex_unlock (buf->lock);
    if (ref > 1) {
      data_unref (buf);

      trans->buf = data_ref (data_from_dynptr (NULL, 0));

      trans->buf->lock = calloc (1, sizeof (pthread_mutex_t));
      pthread_mutex_init (trans->buf->lock, NULL);
    }
  } 
  return NULL;
}


static int32_t
fuse_transport_notify (xlator_t *xl,
		       transport_t *trans,
		       int32_t event)
{
  struct fuse_private *priv = trans->private;
  int32_t res = 0;
  data_t *buf;
  int32_t ref = 0;

  if (!((event & POLLIN) || (event & POLLPRI)))
    return 0;

  if (!fuse_session_exited(priv->se)) {
    static char *recvbuf = NULL;
    static size_t chan_size = 0;

    int32_t fuse_chan_receive (struct fuse_chan * ch,
			       char *buf,
			       int32_t size);
    if (!chan_size)
      chan_size = fuse_chan_bufsize (priv->ch);

    if (!recvbuf)
      recvbuf = calloc (1, chan_size);

    buf = trans->buf;
    res = fuse_chan_receive (priv->ch,
			     recvbuf,
			     chan_size);
    /*    if (res == -1) {
      transport_destroy (trans);
    */
    if (res && res != -1) {
      if (buf->len < (res)) {
	if (buf->data) {
	  free (buf->data);
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

    pthread_mutex_lock (buf->lock);
    ref = buf->refcount;
    pthread_mutex_unlock (buf->lock);
    /* TODO do the check with a lock */
    if (ref > 1) {
      data_unref (buf);

      //      trans->buf = data_ref (data_from_dynptr (malloc (fuse_chan_bufsize (priv->ch)),
      trans->buf = data_ref (data_from_dynptr (NULL, 0));

      trans->buf->lock = calloc (1, sizeof (pthread_mutex_t));
      pthread_mutex_init (trans->buf->lock, NULL);
    }
  } 

  /*
  if (fuse_session_exited (priv->se)) {
    transport_destroy (trans);
    res = -1;
    }*/

  return res >= 0 ? 0 : res;
}

static void
fuse_transport_fini (transport_t *this)
{

}

static struct transport_ops fuse_transport_ops = {
  .disconnect = fuse_transport_disconnect,
};

static transport_t fuse_transport = {
  .ops = &fuse_transport_ops,
  .private = NULL,
  .xl = NULL,
  .init = fuse_transport_init,
  .fini = fuse_transport_fini,
  .notify = fuse_transport_notify
};


transport_t *
glusterfs_mount (glusterfs_ctx_t *ctx,
		 const char *mount_point)
{
  dict_t *options = get_new_dict ();
  transport_t *new_fuse = calloc (1, sizeof (*new_fuse));

  memcpy (new_fuse, &fuse_transport, sizeof (*new_fuse));
  new_fuse->ops = &fuse_transport_ops;
  new_fuse->xl_private = ctx;

  dict_set (options,
	    "mountpoint", 
	    str_to_data ((char *)mount_point));

  return (new_fuse->init (new_fuse,
			  options,
			  fuse_transport_notify) == 0 ? new_fuse : NULL);
}

int32_t
fuse_thread (pthread_t *thread, void *data)
{
  return pthread_create (thread, NULL, fuse_thread_proc, data);
}


