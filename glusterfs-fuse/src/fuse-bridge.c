/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
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

#include "glusterfs.h"
#include "logging.h"
#include "xlator.h"
#include "glusterfs.h"
#include "transport.h"

#include <fuse.h>
#include <fuse/fuse_lowlevel.h>

#include "fuse-internals.h"

struct fuse_private {
  int fd;
  struct fuse *fuse;
  struct fuse_session *se;
  struct fuse_chan *ch;
  char *buf;
  size_t bufsize;
  char *mountpoint;
};

static int32_t
fuse_transport_flush (transport_t *this)
{

  return 0;
}

static int32_t
fuse_transport_recieve (transport_t *this,
			char *buf,
			int32_t len)
{
  return 0;
}

static int32_t
fuse_transport_submit (transport_t *this,
		       char *buf,
		       int32_t len)
{
  return 0;
}

static int32_t
fuse_transport_except (transport_t *this)
{
  struct fuse_private *priv = this->private;
  fuse_session_exit (priv->se);

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
  int argc = 7;
  char *argv[] = { "glusterfs",
                   "-o",
                   "nonempty",
                   "-o",
                   "allow_other",
                   "-o",
                   "default_permissions",
                   NULL };
  struct fuse_args args = FUSE_ARGS_INIT(argc,
					 argv);
  int fd;
  struct fuse *fuse;
  struct fuse_private *priv = calloc (1, sizeof (*priv));
  int32_t res;

  this->notify = notify;
  this->private = (void *)priv;

  fd = fuse_mount(mountpoint, &args);
  if (fd == -1) {
    fuse_opt_free_args(&args);
    goto err_free;
  }

  fuse = glusterfs_fuse_new_common(fd, &args);
  fuse_opt_free_args(&args);

  if (fuse == NULL)
    goto err_unmount;

  res = fuse_set_signal_handlers(fuse->se);
  if (res == -1)
    goto err_destroy;


  priv->fd = fd;
  priv->fuse = (void *)fuse;
  priv->se = fuse->se;
  priv->ch = fuse_session_next_chan(fuse->se, NULL);
  priv->bufsize = fuse_chan_bufsize(priv->ch);
  priv->buf = (char *) malloc(priv->bufsize);
  priv->mountpoint = mountpoint;
  fuse->user_data = this->xl;

  if (!priv->buf) {
    fprintf(stderr, "fuse: failed to allocate read buffer\n");
    goto err_destroy;
  }

  register_transport (this, fd);

  return 0;

 err_destroy:
    fuse_destroy(fuse);
 err_unmount:
    fuse_unmount(mountpoint);
 err_free:
    free(mountpoint);
  return -1;
}

static void
fuse_transport_fini (transport_t *this)
{
  struct fuse_private *priv = this->private;

  free(priv->buf);
  fuse_session_reset(priv->se);
  fuse_teardown(priv->fuse,
		priv->fd,
		priv->mountpoint);
  free (priv);

  return;
}


static int32_t
fuse_transport_notify (xlator_t *xl,
		       transport_t *trans,
		       int32_t event)
{
  struct fuse_private *priv = trans->private;
  int32_t res = 0;

  if (!((event & POLLIN) || (event & POLLPRI)))
    return 0;

  if (!fuse_session_exited(priv->se)) {
    if (priv->fuse->conf.debug)
      printf ("ACTIVITY /dev/fuse\n");
    res = fuse_chan_receive(priv->ch,
			    priv->buf,
			    priv->bufsize);
    if (res == -1) {
      transport_destroy (trans);
    } else if (res) {
      fuse_session_process (priv->se,
			    priv->buf,
			    res,
			    priv->ch);
    }
  } 

  if (fuse_session_exited (priv->se)) {
    transport_destroy (trans);
    res = -1;
  }

  return res;
}

static struct transport_ops fuse_transport_ops = {
  .flush = fuse_transport_flush,
  .recieve = fuse_transport_recieve,
  .submit = fuse_transport_submit,
  .except = fuse_transport_except
};

static transport_t fuse_transport = {
  .ops = &fuse_transport_ops,
  .private = NULL,
  .xl = NULL,
  .init = fuse_transport_init,
  .fini = fuse_transport_fini,
  .notify = fuse_transport_notify
};

static xlator_t *
fuse_graph (xlator_t *graph)
{
  xlator_t *top = calloc (1, sizeof (*top));

  top->first_child = graph;
  graph->parent = top;

  return top;
}


int32_t
glusterfs_mount (xlator_t *graph,
		 const char *mount_point)
{
  dict_t *options = get_new_dict ();
  transport_t *new_fuse = calloc (1, sizeof (*new_fuse));

  memcpy (new_fuse, &fuse_transport, sizeof (*new_fuse));
  new_fuse->ops = &fuse_transport_ops;

  dict_set (options,
	    "mountpoint", 
	    str_to_data ((char *)mount_point));

  new_fuse->xl = fuse_graph (graph);

  return new_fuse->init (new_fuse,
			 options,
			 fuse_transport_notify);
}

