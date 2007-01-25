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

  /*TODO: See if the buffer (memory region) is free, then send it */
  int32_t qp_idx = 0;
  if (len > CMD_BUF_SIZE) {
    qp_idx = IBVERBS_DATA_QP;
    if (priv->data_buf_size < len) {
      /* Already allocated data buffer is not enough, allocate bigger chunk */
      if (priv->buf[1])
	free (priv->buf[1]);
      priv->buf[1] = calloc (1, len + 1);
      priv->data_buf_size = len;
      priv->mr[1] = ibv_reg_mr(priv->pd, priv->buf[1], len, IBV_ACCESS_LOCAL_WRITE);
      if (!priv->mr[1]) {
	gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't allocate MR[0]\n");
	return -1;
      }
    }
    sprintf (priv->buf[0], "NeedDataMR with BufLen = %d\n", len - (len % 4) + 4);
    ib_verbs_post_send (priv, 40, IBVERBS_CMD_QP);
  } else
    qp_idx = IBVERBS_CMD_QP;
  
  memcpy (priv->buf[qp_idx], buf, len);
  if (ib_verbs_post_send (priv, len, qp_idx) < 0) {
    return -EINTR;
  }
  return len;
}

static int32_t
ib_verbs_server_except (transport_t *this)
{
  GF_ERROR_IF_NULL (this);

  ib_verbs_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);

  priv->connected = 0;

  // TODO: Free lot of stuff intialized 

  fini (this);

  return 0;
}

struct transport_ops transport_ops = {
  //  .flush = ib_verbs_flush,
  .recieve = ib_verbs_recieve,
  .disconnect = fini,

  .submit = ib_verbs_server_submit,
  .except = ib_verbs_server_except,

  .readv = ib_verbs_readv,
  .writev = ib_verbs_writev
};

//TODO

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

  /* 'this' transport will register channel->fd */
  this->notify = ib_verbs_cq_notify;

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
  sscanf (buf, "%04x:%06x:%06x:%04x:%06x:%06x", 
	  &priv->remote[0].lid, &priv->remote[0].qpn, 
	  &priv->remote[0].psn, &priv->remote[1].lid,
	  &priv->remote[1].qpn, &priv->remote[1].psn);

  gf_log ("ib-verbs/server", GF_LOG_DEBUG, "%s", buf);
  
  // open a qp here and get the qpn and all.
  if (ib_verbs_create_qp (priv, 0) < 0) {
    gf_log ("ib-verbs/server", 
	    GF_LOG_CRITICAL,
	    "failed to create QP [0]");
    return -1;
  }

  if (ib_verbs_create_qp (priv, 1) < 0) {
    gf_log ("ib-verbs/server", 
	    GF_LOG_CRITICAL,
	    "failed to create QP [1]");
    return -1;
  }

  sprintf (buf, "%04x:%06x:%06x:%04x:%06x:%06x", 
	   priv->local[0].lid, priv->local[0].qpn, priv->local[0].psn,
	   priv->local[1].lid, priv->local[1].qpn, priv->local[1].psn);
  
  gf_log ("ib-verbs/server", GF_LOG_DEBUG, "%s", buf);
  
  write (priv->sock, buf, sizeof buf);

  ib_verbs_post_recv (priv, CMD_BUF_SIZE, 0); //TODO

  ib_verbs_ibv_connect (priv, 1, IBV_MTU_1024);

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

  if (priv->options)
    dict_destroy (priv->options);
  if (priv->connected)
    close (priv->sock);
  free (priv);
  free (this);
  return 0;
}
