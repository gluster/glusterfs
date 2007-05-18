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

#define FUSE_FOP(state, ret, op, args ...)                   \
do {                                                         \
  call_frame_t *frame = get_call_frame_for_req (state->req); \
  xlator_t *xl = frame->this->children ?                     \
                        frame->this->children->xlator : NULL;\
  dict_t *refs = frame->root->req_refs;                      \
  frame->root->state = state;                                \
  STACK_WIND (frame, ret, xl, xl->fops->op, args);           \
  dict_unref (refs);                                         \
} while (0)

#define FUSE_FOP_NOREPLY(state, op, args ...)                \
do {                                                         \
  call_frame_t *frame = get_call_frame_for_req (state->req); \
  xlator_t *xl = frame->this->children ?                     \
                        frame->this->children->xlator : NULL;\
  dict_unref (frame->root->req_refs);                        \
  frame->root->req_refs = NULL;                              \
  STACK_WIND (frame, fuse_nop_cbk, xl, xl->fops->op, args);  \
} while (0)


typedef struct {
  inode_table_t *table;
  fuse_req_t req;
  ino_t par;
  inode_t *parent;
  ino_t ino;
  inode_t *inode;
  int32_t flags;
  char *name;
  off_t off;
  size_t size;
  ino_t oldpar;
  inode_t *oldparent;
  ino_t newpar;
  inode_t *newparent;
  char *oldname;
  char *newname;
  unsigned long nlookup;
  fd_t *fd;
} fuse_state_t;



static void
free_state (fuse_state_t *state)
{
  if (state->parent)
    inode_unref (state->parent);
  if (state->inode)
    inode_unref (state->inode);
  if (state->oldparent)
    inode_unref (state->oldparent);
  if (state->newparent)
    inode_unref (state->newparent);

  if (state->name)
    free (state->name);
  if (state->oldname)
    free (state->oldname);
  if (state->newname)
    free (state->newname);

  free (state);
}


static int32_t
fuse_nop_cbk (call_frame_t *frame,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      ...)
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
  state->table = trans->xl->itable;
  state->req = req;

  return state;
}


static call_frame_t *
get_call_frame_for_req (fuse_req_t req)
{
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
  }

  cctx->req_refs = dict_ref (get_new_dict ());
  cctx->req_refs->lock = calloc (1, sizeof (pthread_mutex_t));
  pthread_mutex_init (cctx->req_refs->lock, NULL);
  dict_set (cctx->req_refs, NULL, trans->buf);

  return &cctx->frames;
}


static int32_t
fuse_lookup_cbk (call_frame_t *frame,
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
    /* TODO: make these timeouts configurable via meta */
    e.ino = inode->ino;
    e.entry_timeout = 0.1;
    e.attr_timeout = 0.1;
    e.attr = *buf;
    fuse_reply_entry (req, &e);
  } else {
    fuse_reply_err (req, ENOENT);
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
  inode_t *parent;
  fuse_state_t *state;

  state = state_from_req (req);
  parent = inode_search (state->table, par, NULL);

  state->par = par;
  state->name = strdup (name);
  state->parent = parent;

  FUSE_FOP (state,
	    fuse_lookup_cbk,
	    lookup,
	    parent,
	    name);
}


static int32_t
fuse_forget_cbk (call_frame_t *frame,
		 void *cookie,
		 int32_t op_ret,
		 int32_t op_errno)
{
  fuse_state_t *state;
  fuse_req_t req;

  state = frame->root->state;
  req = state->req;

  inode_forget (state->inode, state->nlookup);
  fuse_reply_none (req);

  free_state (state);
  STACK_DESTROY (frame->root);

  return 0;
}


static void
fuse_forget (fuse_req_t req,
	     fuse_ino_t ino,
	     unsigned long nlookup)
{
  fuse_state_t *state;
  inode_t *inode;

  if (ino == 1) {
    fuse_reply_none (req);
    return;
  }

  state = state_from_req (req);
  inode = inode_search (state->table, ino, NULL);

  state->ino = ino;
  state->inode = inode;

  FUSE_FOP (state,
	    fuse_forget_cbk,
	    forget,
	    inode,
	    nlookup);

}


static int32_t
fuse_getattr_cbk (call_frame_t *frame,
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
    buf->st_ino = state->ino;
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
  inode_t *inode;
  fuse_state_t *state;

  state = state_from_req (req);
  state->ino = ino;

  if (!fi) {
    inode = inode_search (state->table, ino, NULL);
    state->inode = inode;

    FUSE_FOP (state,
	      fuse_getattr_cbk,
	      getattr,
	      inode);
  } else {
    FUSE_FOP (state,
	      fuse_getattr_cbk,
	      fgetattr,
	      FI_TO_FD (fi));
  }
}


static int32_t
fuse_opendir_cbk (call_frame_t *frame,
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
      FUSE_FOP_NOREPLY (state, releasedir, fd);
    }
  } else {
    fuse_reply_err (req, ENOENT);
  }

  free_state (state);
  STACK_DESTROY (frame->root);
  return 0;
}


static void
fuse_opendir (fuse_req_t req,
	      fuse_ino_t ino,
	      struct fuse_file_info *fi)
{
  fuse_state_t *state;
  inode_t *inode;

  state = state_from_req (req);
  inode = inode_search (state->table, ino, NULL);

  state->ino = ino;
  state->inode = inode;

  FUSE_FOP (state,
	    fuse_opendir_cbk,
	    opendir,
	    inode);
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

    buf = malloc (size);
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
  inode_t *inode;
  fd_t *fd = FI_TO_FD (fi);

  if (dict_get (fd->ctx, "__fuse__readdir__internal__@@!!")) {
    fuse_dir_reply (req, size, off, fd);
    return;
  }

  state = state_from_req (req);
  inode = inode_search (state->table, ino, NULL);

  state->ino = ino;
  state->inode = inode;
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
  state->ino = ino;
  state->fd = FI_TO_FD (fi);

  FUSE_FOP_NOREPLY (state, releasedir, state->fd);

  fuse_reply_err (req, 0);
}

static void
fuse_init (void *data, struct fuse_conn_info *conn)
{
  transport_t *trans = data;
  xlator_t *xl = trans->xl;
  int32_t ret;

  ret = xlator_tree_init (xl);
}

static struct fuse_lowlevel_ops fuse_ops = {
  .init         = fuse_init,
  .lookup       = fuse_lookup,
  .forget       = fuse_forget,
  .getattr      = fuse_getattr,
  .opendir      = fuse_opendir,
  .readdir      = fuse_readdir,
  .releasedir   = fuse_releasedir
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
  char *recvbuf = malloc (chan_size);

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
	if (buf->data)
	  free (buf->data);
	buf->data = malloc (res);
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
      recvbuf = malloc (chan_size);

    buf = trans->buf;
    res = fuse_chan_receive (priv->ch,
			     recvbuf,
			     chan_size);
    /*    if (res == -1) {
      transport_destroy (trans);
    */
    if (res && res != -1) {
      if (buf->len < (res)) {
	if (buf->data)
	  free (buf->data);
	buf->data = malloc (res);
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


