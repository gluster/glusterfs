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


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "dict.h"
#include "glusterfs.h"
#include "transport.h"
#include "protocol.h"
#include "logging.h"
#include "xlator.h"
#include "unix.h"

int gf_transport_fini (struct transport *this);  

static int32_t
unix_server_submit (transport_t *this, char *buf, int32_t len)
{
  unix_private_t *priv = this->private;
  int32_t ret;

  if (!priv->connected)
    return -1;

  pthread_mutex_lock (&priv->write_mutex);
  ret = gf_full_write (priv->sock, buf, len);
  pthread_mutex_unlock (&priv->write_mutex);

  return ret;
}

static int32_t
unix_server_writev (transport_t *this,
		   const struct iovec *vector,
		   int32_t count)
{
  unix_private_t *priv = this->private;
  int32_t ret;

  if (!priv->connected)
    return -1;

  pthread_mutex_lock (&priv->write_mutex);
  ret = gf_full_writev (priv->sock, vector, count);
  pthread_mutex_unlock (&priv->write_mutex);

  return ret;
}

struct transport_ops transport_ops = {
  //  .flush = unix_flush,
  .recieve = unix_recieve,
  .disconnect = unix_disconnect,

  .submit = unix_server_submit,
  .except = unix_except,

  .readv = unix_readv,
  .writev = unix_server_writev
};

static int32_t
unix_server_notify (xlator_t *xl,
		   int32_t event,
		   void *data,
		   ...)
{
  transport_t *trans = data;
  int32_t main_sock;

  if (event == GF_EVENT_CHILD_UP)
    return 0;

  transport_t *this = calloc (1, sizeof (transport_t));
  ERR_ABORT (this);
  this->private = calloc (1, sizeof (unix_private_t));
  ERR_ABORT (this->private);

  
  pthread_mutex_init (&((unix_private_t *)this->private)->read_mutex, NULL);
  pthread_mutex_init (&((unix_private_t *)this->private)->write_mutex, NULL);
  //  pthread_mutex_init (&((unix_private_t *)this->private)->queue_mutex, NULL);

  GF_ERROR_IF_NULL (xl);

  trans->xl = xl;
  this->xl = xl;

  unix_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);

  struct sockaddr_un sun;
  socklen_t addrlen = sizeof (sun);

  main_sock = ((unix_private_t *) trans->private)->sock;
  priv->sock = accept (main_sock, &sun, &addrlen);
  if (priv->sock == -1) {
    gf_log (this->xl->name, GF_LOG_ERROR,
	    "accept() failed: %s", strerror (errno));
    free (this->private);
    free (this);
    return -1;
  }

  this->ops = trans->ops;
  this->fini = (void *)gf_transport_fini;
  this->notify = ((unix_private_t *)trans->private)->notify;
  priv->connected = 1;

  priv->options = get_new_dict ();

  socklen_t sock_len = sizeof (struct sockaddr_un);
  getpeername (priv->sock, &this->peerinfo.sockaddr, &sock_len);
  
  gf_log (this->xl->name, GF_LOG_DEBUG,
	  "Registering socket (%d) for new transport object of %s",
	  priv->sock, "<unix>");

  poll_register (this->xl->ctx, priv->sock, transport_ref (this));
  return 0;
}


int 
gf_transport_init (struct transport *this, 
		   dict_t *options,
		   event_notify_fn_t notify)
{
  data_t *listen_path_data;
  char *listen_path;

  this->private = calloc (1, sizeof (unix_private_t));
  ERR_ABORT (this->private);
  ((unix_private_t *)this->private)->notify = notify;

  this->notify = unix_server_notify;
  struct unix_private *priv = this->private;

  struct sockaddr_un sun;
  priv->sock = socket (AF_UNIX, SOCK_STREAM, 0);
  if (priv->sock == -1) {
    gf_log (this->xl->name, GF_LOG_CRITICAL,
	    "failed to create socket, error: %s",
	    strerror (errno));
    free (this->private);
    return -1;
  }

  listen_path_data = dict_get (options, "listen-path");
  if (!listen_path_data) {
    gf_log (this->xl->name, GF_LOG_ERROR,
	    "missing option listen-path");
    return -1;
  }

  listen_path = data_to_str (listen_path_data);

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

  if (strlen (listen_path) > UNIX_PATH_MAX) {
    gf_log (this->xl->name, GF_LOG_ERROR,
	    "option listen-path has value length %d > %d",
	    strlen (listen_path), UNIX_PATH_MAX);
    return -1;
  }

  sun.sun_family = AF_UNIX;
  strcpy (sun.sun_path, listen_path);

  int opt = 1;
  setsockopt (priv->sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt));

 again:
  if (bind (priv->sock, (struct sockaddr *)&sun,
	    sizeof (sun)) != 0) {
    int32_t saved_errno = errno;

    gf_log (this->xl->name, GF_LOG_CRITICAL,
	    "failed to bind to socket on path %s, error: %s",
	    sun.sun_path, strerror (errno));

    if (saved_errno == EADDRINUSE) {
      gf_log (this->xl->name, GF_LOG_WARNING,
	      "attempting to unlink(%s) and retry", sun.sun_path);
      if (unlink (sun.sun_path) == 0) {
	gf_log (this->xl->name, GF_LOG_DEBUG,
		"unlink successful, retrying bind() again");
	goto again;
      }
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "unlink (%s) failed - %s", sun.sun_path, strerror (errno));
    }
    free (this->private);
    return -1;
  }

  if (listen (priv->sock, 10) != 0) {
    gf_log (this->xl->name, GF_LOG_CRITICAL,
	    "listen () failed on socket, error: %s",
	    strerror (errno));
    free (this->private);
    return -1;
  }

  poll_register (this->xl->ctx, priv->sock, transport_ref (this));

  pthread_mutex_init (&((unix_private_t *)this->private)->read_mutex, NULL);
  pthread_mutex_init (&((unix_private_t *)this->private)->write_mutex, NULL);
  //  pthread_mutex_init (&((unix_private_t *)this->private)->queue_mutex, NULL);

  return 0;
}

int32_t
gf_transport_fini (struct transport *this)
{
  unix_private_t *priv = this->private;
  //  this->ops->flush (this);

  if (priv->options)
    gf_log (this->xl->name, GF_LOG_DEBUG,
	    "destroying transport object for (fd=%d)", priv->sock);

  pthread_mutex_destroy (&priv->read_mutex);
  pthread_mutex_destroy (&priv->write_mutex);

  if (priv->options)
    dict_destroy (priv->options);
  if (priv->connected)
    close (priv->sock);
  free (priv);
  return 0;
}
