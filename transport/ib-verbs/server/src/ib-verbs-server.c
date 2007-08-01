/*
   Copyright (c) 2006, 2007 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include "dict.h"
#include "glusterfs.h"
#include "transport.h"
#include "protocol.h"
#include "logging.h"
#include "xlator.h"

#include "ib-verbs.h"

void gf_transport_fini (struct transport *this);  


static int32_t 
ib_verbs_server_writev (struct transport *this,
			const struct iovec *vector,
			int32_t count)
{
  ib_verbs_private_t *priv = this->private;

  if (!priv->connected) {
    gf_log ("ib-verbs/server",
	    GF_LOG_ERROR,
	    "attempt to write on non-connected transport %p",
	    this);
    return -ENOTCONN;
  }

  return ib_verbs_writev (this, vector, count);
}

struct transport_ops transport_ops = {
  //  .flush = ib_verbs_flush,
  .recieve = ib_verbs_receive,
  .disconnect = ib_verbs_disconnect,

  //  .submit = ib_verbs_server_submit,
  .except = ib_verbs_except,
  .writev = ib_verbs_server_writev
};


static int32_t
ib_verbs_server_notify (xlator_t *xl,
			int32_t event,
			void *data,
			...)
{
  transport_t *trans = data;
  int32_t main_sock;
  transport_t *this;
  ib_verbs_private_t *priv;
  ib_verbs_private_t *trans_priv = (ib_verbs_private_t *) trans->private;
  
  if (event != GF_EVENT_POLLIN)
    return 0;

  this = calloc (1, sizeof (transport_t));
  priv = calloc (1, sizeof (ib_verbs_private_t));
  this->private = priv;
  /* Copy all the ib_verbs related values in priv, from trans_priv as other than QP, 
     all the values remain same */
  priv->notify = trans_priv->notify;
  priv->device = trans_priv->device;
  priv->options = trans_priv->options;
  this->ops = trans->ops;
  this->xl = trans->xl;

  
  struct sockaddr_in sin;
  socklen_t addrlen = sizeof (sin);

  main_sock = (trans_priv)->sock;
  priv->sock = accept (main_sock, &sin, &addrlen);
  if (priv->sock == -1) {
    gf_log ("ib-verbs/server",
	    GF_LOG_ERROR,
	    "accept() failed: %s",
	    strerror (errno));
    free (this->private);
    free (this);
    return -1;
  }
  
  //  this->ops = &transport_ops;
  this->fini = (void *)gf_transport_fini;

  priv->connected = 1;
  priv->addr = sin.sin_addr.s_addr;
  priv->port = sin.sin_port;
  priv->peers[0].trans = this;
  priv->peers[1].trans = this;

  socklen_t sock_len = sizeof (struct sockaddr_in);
  getpeername (priv->sock,
	       &this->peerinfo.sockaddr,
	       &sock_len);
  
  if (ib_verbs_handshake (this)) {
    close (priv->sock);
    free (priv);
    free (this);
    return 0;
  }

  pthread_mutex_init (&priv->read_mutex, NULL);
  pthread_mutex_init (&priv->write_mutex, NULL);

  this->notify = ib_verbs_tcp_notify;

  poll_register (this->xl->ctx, priv->sock, transport_ref (this)); // for disconnect

  return 0;
}

/* Initialization function */
int32_t 
gf_transport_init (struct transport *this, 
		   dict_t *options,
		   event_notify_fn_t notify)
{
  data_t *bind_addr_data;
  data_t *listen_port_data;
  char *bind_addr;
  uint16_t listen_port;

  ib_verbs_private_t *priv = calloc (1, sizeof (ib_verbs_private_t));
  this->private = priv;
  priv->notify = notify;

  this->notify = ib_verbs_server_notify;

  /* Initialize the ib driver */
  if(ib_verbs_init (this)) {
    gf_log ("ib-verbs/server",
	    GF_LOG_ERROR,
	    "Failed to initialize IB Device");
    return -1;
  }

  struct sockaddr_in sin;
  priv->sock = socket (AF_INET, SOCK_STREAM, 0);
  if (priv->sock == -1) {
    gf_log ("ib-verbs/server",
	    GF_LOG_CRITICAL,
	    "init: failed to create socket, error: %s",
	    strerror (errno));
    free (this->private);
    return -1;
  }

  bind_addr_data = dict_get (options, "bind-address");
  if (bind_addr_data)
    bind_addr = data_to_str (bind_addr_data);
  else
    bind_addr = "0.0.0.0";

  listen_port_data = dict_get (options, "listen-port");
  if (listen_port_data)
    listen_port = htons (data_to_uint64 (listen_port_data));
  else
    listen_port = htons (GF_DEFAULT_LISTEN_PORT);

  sin.sin_family = AF_INET;
  sin.sin_port = listen_port;
  sin.sin_addr.s_addr = bind_addr ? inet_addr (bind_addr) : htonl (INADDR_ANY);

  int opt = 1;
  setsockopt (priv->sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt));
  if (bind (priv->sock,
	    (struct sockaddr *)&sin,
	    sizeof (sin)) != 0) {
    gf_log ("ib-verbs/server",
	    GF_LOG_CRITICAL,
	    "init: failed to bind to socket on port %d, error: %s",
	    sin.sin_port,
	    strerror (errno));
    free (this->private);
    return -1;
  }

  if (listen (priv->sock, 10) != 0) {
    gf_log ("ib-verbs/server",
	    GF_LOG_CRITICAL,
	    "init: listen () failed on socket, error: %s",
	    strerror (errno));
    free (this->private);
    return -1;
  }

  /* Register the main socket */
  poll_register (this->xl->ctx, priv->sock, transport_ref (this));

  return 0;
}

void  
gf_transport_fini (struct transport *this)
{
  /* TODO: verify this function does graceful finish */
  //  ib_verbs_private_t *priv = this->private;

  gf_log ("ib-verbs/server",
	  GF_LOG_CRITICAL,
	  "%s: called fini on transport: %p",
	  this->xl->name, this);
  return;
}

