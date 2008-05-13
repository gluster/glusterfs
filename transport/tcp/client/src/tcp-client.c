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
#include "tcp.h"


static int32_t
tcp_connect (struct transport *this)
{
  tcp_private_t *priv = this->private;
  dict_t *options = priv->options;
  char non_blocking = 1;

  if (!priv->options) {
    priv->options = dict_copy (this->xl->options, NULL);
    options = priv->options;
  }

  if (dict_get (options, "non-blocking-connect")) {
    char *nb_connect =data_to_str (dict_get (options,
					     "non-blocking-connect"));
    if ((!strcasecmp (nb_connect, "off")) ||
	(!strcasecmp (nb_connect, "no")))
      non_blocking = 0;
  }

  struct sockaddr_in sin;
  struct sockaddr_in sin_src;
  int32_t ret = 0;
  uint16_t try_port = CLIENT_PORT_CIELING;

  struct pollfd poll_s;
  int    nfds;
  int    timeout = 0;
  int optval_s;
  unsigned int optvall_s = sizeof(int);
  int window_size = 640 * 1024;

  // Create the socket if no connection ?
  if (priv->connected)
    return 0;

  if (!priv->connection_in_progress)
  {
    timeout = 100; /* If first attempt of reconnection, wait sometime */
    priv->sock = socket (AF_INET, SOCK_STREAM, 0);
    
    gf_log (this->xl->name, GF_LOG_DEBUG,
	    "socket fd = %d", priv->sock);
  
    if (priv->sock == -1) {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "socket () - error: %s", strerror (errno));
      return -errno;
    }

    if (dict_get (this->xl->options, "window-size")) {
      window_size = data_to_uint32 (dict_get (this->xl->options,
					      "window-size"));
      gf_log (this->xl->name, GF_LOG_DEBUG,
	      "setting tcp window size %d", window_size);

      setsockopt (priv->sock, SOL_SOCKET, SO_SNDBUF, (char *)&window_size,
		  sizeof (window_size));
      setsockopt (priv->sock, SOL_SOCKET, SO_RCVBUF, (char *)&window_size,
		  sizeof (window_size));
    }

    // Find a local port avaiable for use
    while (try_port) { 
      sin_src.sin_family = PF_INET;
      sin_src.sin_port = htons (try_port); //FIXME: have it a #define or configurable
      sin_src.sin_addr.s_addr = INADDR_ANY;
      
      if ((ret = bind (priv->sock,
		       (struct sockaddr *)&sin_src,
		       sizeof (sin_src))) == 0) {
	gf_log (this->xl->name, GF_LOG_DEBUG,
		"finalized on port `%d'", try_port);
	break;
      }
	
      try_port--;
    }
	
    if (ret != 0) {
      gf_log (this->xl->name, GF_LOG_WARNING,
	      "bind failed %s. attempting to use default port during connect()",
	      strerror (errno));
    }
	
    sin.sin_family = AF_INET;
	
    if (dict_get (options, "remote-port")) {
      sin.sin_port = htons (data_to_uint64 (dict_get (options,
						   "remote-port")));
    } else {
      gf_log (this->xl->name, GF_LOG_DEBUG,
	      "defaulting remote-port to %d", GF_DEFAULT_LISTEN_PORT);
      sin.sin_port = htons (GF_DEFAULT_LISTEN_PORT);
    }
	
    if (dict_get (options, "remote-host")) {
      sin.sin_addr.s_addr = gf_resolve_ip (data_to_str (dict_get (options,
							       "remote-host")),
					   &this->dnscache);
    } else {
      gf_log (this->xl->name, GF_LOG_DEBUG,
	      "error: missing 'option remote-host <hostname>'");
      close (priv->sock);
      return -errno;
    }

    if (non_blocking) {
    // TODO, others ioctl, ioctlsocket, IoctlSocket, or if dont support
      fcntl (priv->sock, F_SETFL, O_NONBLOCK);
    }
    // Try to connect
    errno = 0;
    ret = connect (priv->sock, (struct sockaddr *)&sin, sizeof (sin));
    
    if (ret == -1) {
      if (errno && errno != EINPROGRESS) {
	gf_log (this->xl->name, GF_LOG_ERROR,
		"error: not in progress - trace: %s",
		strerror (errno));
	close (priv->sock);
	return -errno;
      }
    }

    gf_log (this->xl->name, GF_LOG_DEBUG,
	    "connect on %d in progress (non-blocking)", priv->sock);
    priv->connection_in_progress = 1;
    priv->connected = 0;
  }

  if (non_blocking) {
    nfds = 1;
    memset (&poll_s, 0, sizeof(poll_s));
    poll_s.fd = priv->sock;
    poll_s.events = POLLOUT;
    ret = poll (&poll_s, nfds, timeout); 

    if (ret) {
      /* success or not, connection is no more in progress */
      priv->connection_in_progress = 0;

      ret = getsockopt (priv->sock,
			SOL_SOCKET,
			SO_ERROR,
			(void *)&optval_s,
			&optvall_s);
      if (ret) {
	gf_log (this->xl->name, GF_LOG_ERROR, "%s: SOCKET ERROR");
	close (priv->sock);
	return -1;
      }
      if (optval_s) {
	gf_log (this->xl->name, GF_LOG_ERROR,
		"non-blocking connect() returned: %d (%s)",
		optval_s, strerror (optval_s));
	close (priv->sock);
	return -1;
      }
    } else {
      /* connection is still in progress */
      gf_log (this->xl->name, GF_LOG_DEBUG,
	      "connection on %d still in progress - try later",
	       priv->sock);
      return -1;
    }
  }

  /* connection was successful */
  gf_log (this->xl->name, GF_LOG_DEBUG,
	  "connection on %d success", priv->sock);

  if (non_blocking) {
    int flags = fcntl (priv->sock, F_GETFL, 0);
    fcntl (priv->sock, F_SETFL, flags & (~O_NONBLOCK));
  }

  socklen_t sock_len = sizeof (struct sockaddr_in);
  getpeername (priv->sock, &this->peerinfo.sockaddr, &sock_len);

  priv->connected = 1;
  priv->connection_in_progress = 0;

  poll_register (this->xl->ctx, priv->sock, transport_ref (this));

  return 0;
}

static int32_t
tcp_client_submit (transport_t *this, char *buf, int32_t len)
{
  tcp_private_t *priv = this->private;
  int32_t ret = 0;

  pthread_mutex_lock (&priv->write_mutex);
  if (!priv->connected) {
    /*
    ret = tcp_connect (this, priv->options);
    if (ret == 0) {
      poll_register (this->xl->ctx, priv->sock, transport_ref (this));
      ret = gf_full_write (priv->sock, buf, len);
    } else {
      ret = -1;
    }
    */
    ret = -1;
  } else {
    ret = gf_full_write (priv->sock, buf, len);
  }
  pthread_mutex_unlock (&priv->write_mutex);

  return ret;
}


static int32_t
tcp_client_writev (transport_t *this,
		   const struct iovec *vector,
		   int32_t count)
{
  tcp_private_t *priv = this->private;
  int32_t ret = 0;
  
  pthread_mutex_lock (&priv->write_mutex);
  if (!priv->connected) {
    /*
    ret = tcp_connect (this, priv->options);
    if (ret == 0) {
      poll_register (this->xl->ctx, priv->sock, transport_ref (this));
      ret = gf_full_writev (priv->sock, vector, count);
    } else {
      ret = -1;
    }
    */
    ret = -1;
  } else {
    ret = gf_full_writev (priv->sock, vector, count);
  }
  pthread_mutex_unlock (&priv->write_mutex);

  return ret;
}

struct transport_ops transport_ops = {
  //  .flush = tcp_flush,
  .recieve = tcp_recieve,

  .submit = tcp_client_submit,

  .connect = tcp_connect,
  .disconnect = tcp_disconnect,
  .except = tcp_except,

  .readv = tcp_readv,
  .writev = tcp_client_writev,

  .bail = tcp_bail,
};

int 
gf_transport_init (struct transport *this,
		   dict_t *options,
		   event_notify_fn_t notify)
{
  tcp_private_t *priv;

  priv = calloc (1, sizeof (tcp_private_t));
  this->private = priv;
  this->notify = notify;

  pthread_mutex_init (&priv->read_mutex, NULL);
  pthread_mutex_init (&priv->write_mutex, NULL);
  
  priv->connection_in_progress = 0;

  /*
  ret = tcp_connect (this, options);
  if (!ret) {
    poll_register (this->xl->ctx, priv->sock, transport_ref (this));
  }

  if (ret) {
    retry_data = dict_get (options, "background-retry");
    if (retry_data) {
      if (strcasecmp (data_to_str (retry_data), "off") == 0)
	return -1;
    }
  }
  */
  return 0;
}

int 
gf_transport_fini (struct transport *this)
{
  tcp_private_t *priv = this->private;
  //  this->ops->flush (this);

  dict_destroy (priv->options);
  close (priv->sock);
  free (priv);
  return 0;
}
