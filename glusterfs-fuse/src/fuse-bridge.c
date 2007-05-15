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

#define FUSE_FOP_NOREPLY(f, op, args ...)                    \
do {                                                         \
  call_frame_t *frame = get_call_frame_for_req (NULL);       \
  transport_t *trans = f->user_data;                         \
  frame->this = trans->xl;                                   \
  xlator_t *xl = frame->this->children ?                     \
                        frame->this->children->xlator : NULL;\
  frame->root->state = NULL;                                 \
  STACK_WIND (frame, fuse_nop_cbk, xl, xl->fops->op, args);  \
} while (0)

struct fuse_call_state {
  fuse_req_t req;
  fuse_ino_t parent;
  fuse_ino_t ino;
  int32_t flags;
  char *name;
  char *path;
  off_t off;
  size_t size;
  fuse_ino_t olddir;
  fuse_ino_t newdir;
  char *oldname;
  char *newname;
  int32_t valid;
  struct fuse_dirhandle *dh;
};


static int32_t
fuse_nop_cbk (call_frame_t *frame,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      ...)
{
  if (frame->root->state)
    free (frame->root->state);

  STACK_DESTROY (frame->root);
  return 0;
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


static void
fuse_lookup (fuse_req_t req,
	     fuse_ino_t parent,
	     const char *name)
{

}

static void
fuse_forget (fuse_req_t req,
	     fuse_ino_t inode,
	     unsigned long nlookup)
{

}

static struct fuse_lowlevel_ops fuse_ops = {
  .lookup       = fuse_lookup,
  .forget       = fuse_forget
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
    /* TODO do the check with a lock */
    if (ref > 1) {
      data_unref (buf);

      //      trans->buf = data_ref (data_from_dynptr (malloc (fuse_chan_bufsize (priv->ch)),
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
    if (priv->fuse->conf.debug)
      printf ("ACTIVITY /dev/fuse\n");
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


