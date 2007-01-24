/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
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

#include "dict.h"
#include "glusterfs.h"
#include "transport.h"
#include "protocol.h"
#include "logging.h"
#include "xlator.h"
#include "ib-verbs.h"

int32_t fini (struct transport *this);  

static int32_t
ib_verbs_server_submit (transport_t *this, char *buf, int32_t len)
{
  ib_verbs_private_t *priv = this->private;

  if (!priv->connected)
    return -1;

  return ib_verbs_full_write (priv, buf, len);//TODO
}

static int32_t
ib_verbs_server_except (transport_t *this)
{
  GF_ERROR_IF_NULL (this);

  ib_verbs_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);

  priv->connected = 0;

  fini (this);

  return 0;
}

struct transport_ops transport_ops = {
  //  .flush = ib_verbs_flush,
  .recieve = ib_verbs_recieve,
  .disconnect = fini,

  .submit = ib_verbs_server_submit,
  .except = ib_verbs_server_except
};

int32_t 
ib_verbs_server_cq_notify (xlator_t *xl,
			   transport_t *trans,
			   int32_t event)
{
  gf_log ("ib-verbs/server", GF_LOG_DEBUG, "Received activity in CQ");
  
  ib_verbs_private_t *priv = (ib_verbs_private_t *)trans->private;

  struct ibv_wc wc[2];
  
  ibv_poll_cq (priv->cq, 2, wc);

  ibv_ack_cq_events (priv->cq, 1);

  gf_log ("ib-verbs/server", GF_LOG_DEBUG, "priv->buf is \"%s\"", priv->buf);
  
  return 0;
}

int32_t
ib_verbs_server_notify (xlator_t *xl, 
			transport_t *trans,
			int32_t event)
{
  int32_t main_sock;
  transport_t *this = calloc (1, sizeof (transport_t));
  ib_verbs_private_t *priv = calloc (1, sizeof (ib_verbs_private_t));
  ib_verbs_private_t * trans_priv = (ib_verbs_private_t *) trans->private;
  this->private = priv;

  /* Copy all the ib_verbs related values in priv, from trans_priv as other than QP, 
     all the values remain same */
  memcpy (priv, trans_priv, sizeof (ib_verbs_private_t));

  //  pthread_mutex_init (&((ib_verbs_private_t *)this->private)->read_mutex, NULL);
  //pthread_mutex_init (&((ib_verbs_private_t *)this->private)->write_mutex, NULL);
  //  pthread_mutex_init (&((ib_verbs_private_t *)this->private)->queue_mutex, NULL);
  
  GF_ERROR_IF_NULL (xl);
  
  trans->xl = xl;
  this->xl = xl;
  
  GF_ERROR_IF_NULL (priv);
  
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
    return -1;
  }

  
  this->ops = &transport_ops;
  this->fini = (void *)fini;
  this->notify = ib_verbs_server_cq_notify;

  // copy all the required data */
  priv->connected = 1;
  priv->addr = sin.sin_addr.s_addr;
  priv->port = sin.sin_port;
  
  priv->options = get_new_dict ();
  dict_set (priv->options, "remote-host", 
	    data_from_dynstr (strdup (inet_ntoa (sin.sin_addr))));
  dict_set (priv->options, "remote-port", 
	    int_to_data (ntohs (sin.sin_port)));
  
  /* get (lid, psn, qpn) from client, also send local node info */
  char buf[256] = {0,};

  read (priv->sock, buf, 256);
  sscanf (buf, "%04x:%06x:%06x", &priv->remote.lid, &priv->remote.qpn, &priv->remote.psn);

  gf_log ("ib-verbs/server", GF_LOG_DEBUG, "%s", buf);
  
  // open a qp here and get the qpn and all.
  if (ib_verbs_create_qp (priv) < 0) {
    gf_log ("ib-verbs/server", 
	    GF_LOG_CRITICAL,
	    "failed to create QP");
    return -1;
  }

  sprintf (buf, "%04x:%06x:%06x", priv->local.lid, priv->local.qpn, priv->local.psn);
  
  gf_log ("ib-verbs/server", GF_LOG_DEBUG, "%s", buf);
  
  write (priv->sock, buf, sizeof buf);

  ib_verbs_post_recv (priv, 4096); //TODO

  ib_verbs_ibv_connect (priv, 1, priv->local.psn, IBV_MTU_1024);

  gf_log ("ib-verbs/server",
	  GF_LOG_DEBUG,
	  "Registering socket (%d) for new transport object of %s",
	  priv->sock,
	  data_to_str (dict_get (priv->options, "remote-host")));
  

  close (priv->sock); // no use keeping this socket open.

  /* Replace the socket fd with the channel->fd of ibv */
  priv->sock = priv->channel->fd;

  register_transport (this, priv->channel->fd);
  
  return 0;
}


int 
init (struct transport *this, 
      dict_t *options,
      int32_t (*notify) (xlator_t *xl, transport_t *trans, int32_t))
{
  data_t *bind_addr_data;
  data_t *listen_port_data;
  char *bind_addr;
  uint16_t listen_port;

  struct ib_verbs_private *priv = calloc (1, sizeof (ib_verbs_private_t));
  this->private = priv;
  priv->notify = notify;
  this->notify = ib_verbs_server_notify;

  /* Initialize the ib driver */
  ib_verbs_ibv_init (priv);

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
    listen_port = htons (data_to_int (listen_port_data));
  else
    /* TODO: move this default port to a macro definition */
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


  register_transport (this, priv->sock);

  //pthread_mutex_init (&((ib_verbs_private_t *)this->private)->read_mutex, NULL);
  //pthread_mutex_init (&((ib_verbs_private_t *)this->private)->write_mutex, NULL);
  //  pthread_mutex_init (&((ib_verbs_private_t *)this->private)->queue_mutex, NULL);

  return 0;
}

int 
fini (struct transport *this)
{
  ib_verbs_private_t *priv = this->private;
  //  this->ops->flush (this);

  if (priv->options)
    gf_log ("ib-verbs/server",
	    GF_LOG_DEBUG,
	    "destroying transport object for %s:%s (fd=%d)",
	    data_to_str (dict_get (priv->options, "remote-host")),
	    data_to_str (dict_get (priv->options, "remote-port")),
	    priv->sock);

  //pthread_mutex_destroy (&((ib_verbs_private_t *)this->private)->read_mutex);
  //pthread_mutex_destroy (&((ib_verbs_private_t *)this->private)->write_mutex);

  if (priv->options)
    dict_destroy (priv->options);
  if (priv->connected)
    close (priv->sock);
  free (priv);
  free (this);
  return 0;
}
