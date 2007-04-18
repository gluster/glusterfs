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
  int32_t i, len = 0;
  ib_verbs_private_t *priv = this->private;
  const struct iovec *trav = vector;

  if (!priv->connected) {
    return -ENOTCONN;
  }
  for (i = 0; i< count; i++) {
    len += trav[i].iov_len;
  }

  /* See if the buffer (memory region) is free, then send it */
  int32_t qp_idx = 0;
  ib_verbs_post_t *post;
  if (len <= priv->peers[0].send_size + 2048) {
    qp_idx = IBVERBS_CMD_QP;
    while (1) {
      pthread_mutex_lock (&priv->write_mutex);
      post = priv->peers[0].send_list;
      if (post)
	priv->peers[0].send_list = post->next;
      pthread_mutex_unlock (&priv->write_mutex);
      if (!post) {
	ib_verbs_send_cq_notify (this->xl, this, POLLIN);
      } else {
	break;
      }
    }
  } else {
    qp_idx = IBVERBS_MISC_QP;
    while (1) {
      pthread_mutex_lock (&priv->write_mutex);
      post = priv->peers[1].send_list;
      if (post)
	priv->peers[1].send_list = post->next;
      pthread_mutex_unlock (&priv->write_mutex);
      if (!post) {
	ib_verbs_send_cq_notify1 (this->xl, this, POLLIN);
      } else {
	break;
      }
    }

    if (post->buf_size < len) {
      /* Already allocated data buffer is not enough, allocate bigger chunk */
      if (post->buf) {
	free (post->buf);
	ibv_dereg_mr (post->mr);
      }

      post->buf = valloc (len + 2048);
      post->buf_size = len + 2048;
      memset (post->buf, 0, len + 2048);

      post->mr = ibv_reg_mr (priv->pd, 
			     post->buf, 
			     len + 2048,
			     IBV_ACCESS_LOCAL_WRITE);
      if (!post->mr) {
	gf_log ("transport/ib-verbs", 
		GF_LOG_CRITICAL, 
		"Couldn't allocate MR\n");
	return -1;
      }
    }

    pthread_mutex_lock (&priv->write_mutex);
    ib_verbs_post_t *temp_mr = priv->peers[0].send_list;
    priv->peers[0].send_list = temp_mr->next;
    pthread_mutex_unlock (&priv->write_mutex);

    sprintf (temp_mr->buf, 
	     "NeedDataMR:%d\n", len + 4);
    if (ib_verbs_post_send (this, &priv->peers[0], temp_mr, 20) < 0) {
      gf_log ("ib-verbs-writev", 
	      GF_LOG_CRITICAL, 
	      "Failed to send meta buffer");
      return -EINTR;
    }
  }  
  
  len = 0;
  for (i = 0; i< count; i++) {
    memcpy (post->buf + len, trav[i].iov_base, trav[i].iov_len);
    len += trav[i].iov_len;
  }

  if (ib_verbs_post_send (this, &priv->peers[qp_idx], post, len) < 0) {
    gf_log ("ib-verbs-writev", 
	    GF_LOG_CRITICAL, 
	    "Failed to send buffer");
    return -EINTR;
  }

  return 0;
}

static int32_t 
ib_verbs_server_disconnect (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;

  /* Free anything thats allocated per connection, registered */
  /* dereg_mr */
  ib_verbs_post_t *temp, *trav = priv->peers[0].recv_list;
  while (trav) {
    temp = trav;
    if (trav->buf) free (trav->buf);
    if (trav->mr) ibv_dereg_mr (trav->mr);
    trav = trav->next;
    free (temp);
  }
  trav = priv->peers[1].recv_list;
  while (trav) {
    temp = trav;
    if (trav->buf) free (trav->buf);
    if (trav->mr) ibv_dereg_mr (trav->mr);
    trav = trav->next;
    free (temp);
  }
  trav = priv->peers[0].send_list;
  while (trav) {
    temp = trav;
    if (trav->buf) free (trav->buf);
    if (trav->mr) ibv_dereg_mr (trav->mr);
    trav = trav->next;
    free (temp);
  }
  trav = priv->peers[1].send_list;
  while (trav) {
    temp = trav;
    if (trav->buf) free (trav->buf);
    if (trav->mr) ibv_dereg_mr (trav->mr);
    trav = trav->next;
    free (temp);
  }
  /* destroy_qp */
  if (priv->peers[0].qp) ibv_destroy_qp (priv->peers[0].qp);
  if (priv->peers[1].qp) ibv_destroy_qp (priv->peers[1].qp);

  priv->connected = 0;

  free (this);
  return 0;
}

static int32_t
ib_verbs_server_except (transport_t *this)
{
  GF_ERROR_IF_NULL (this);

  ib_verbs_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);

  priv->connected = 0;

  ib_verbs_server_disconnect (this);

  return 0;
}


struct transport_ops transport_ops = {
  //  .flush = ib_verbs_flush,
  .recieve = ib_verbs_recieve,
  .disconnect = ib_verbs_server_disconnect,

  //  .submit = ib_verbs_server_submit,
  .except = ib_verbs_server_except,
  .writev = ib_verbs_server_writev
};

int32_t
ib_verbs_server_tcp_notify (xlator_t *xl, 
			    transport_t *trans,
			    int32_t event)
{
  ib_verbs_private_t *priv = (ib_verbs_private_t *) trans->private;
  poll_unregister (xl->ctx, priv->sock);
  ib_verbs_server_disconnect (trans);
  return 0;
}

static int32_t
ib_verbs_server_notify (xlator_t *xl, 
			transport_t *trans,
			int32_t event)
{
  int32_t main_sock;
  transport_t *this = calloc (1, sizeof (transport_t));
  ib_verbs_private_t *priv = calloc (1, sizeof (ib_verbs_private_t));
  ib_verbs_private_t *trans_priv = (ib_verbs_private_t *) trans->private;
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
    free (this);
    free (priv);
    return -1;
  }
  
  this->ops = &transport_ops;
  this->fini = (void *)gf_transport_fini;

  priv->connected = 1;
  priv->addr = sin.sin_addr.s_addr;
  priv->port = sin.sin_port;

  socklen_t sock_len = sizeof (struct sockaddr_in);
  getpeername (priv->sock,
	       &this->peerinfo.sockaddr,
	       &sock_len);
  
  pthread_mutex_init (&priv->read_mutex, NULL);
  pthread_mutex_init (&priv->write_mutex, NULL);

  ib_verbs_conn_setup (this);

  this->notify = ib_verbs_server_tcp_notify;

  poll_register (this->xl->ctx, priv->sock, this); // for disconnect

  return 0;
}

/* Initialization function */
int32_t 
gf_transport_init (struct transport *this, 
		   dict_t *options,
		   int32_t (*notify) (xlator_t *xl,
				      transport_t *trans,
				      int32_t))
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
    listen_port = htons (data_to_int (listen_port_data));
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
  poll_register (this->xl->ctx, priv->sock, this);

  return 0;
}

void  
gf_transport_fini (struct transport *this)
{
  /* TODO: verify this function does graceful finish */

  ib_verbs_private_t *priv = this->private;

  /*  if (priv->options)
    gf_log ("ib-verbs/server",
	    GF_LOG_DEBUG,
	    "destroying transport object for %s:%s (fd=%d)",
	    data_to_str (dict_get (priv->options, "remote-host")),
	    data_to_str (dict_get (priv->options, "remote-port")),
	    priv->sock);
  */
  /*
  ibv_destroy_cq (priv->ibv.sendcq[0]);
  ibv_destroy_cq (priv->ibv.sendcq[1]);
  ibv_destroy_cq (priv->ibv.recvcq[0]);
  ibv_destroy_cq (priv->ibv.recvcq[1]);

  ibv_destroy_comp_channel (priv->ibv.send_channel[0]);
  ibv_destroy_comp_channel (priv->ibv.send_channel[1]);
  ibv_destroy_comp_channel (priv->ibv.recv_channel[0]);
  ibv_destroy_comp_channel (priv->ibv.recv_channel[1]);

  ibv_dealloc_pd (priv->ibv.pd);
  ibv_close_device (priv->ibv.context);
  */
  /*  if (priv->options)
    dict_destroy (priv->options);
  */
  if (priv->connected)
    close (priv->sock);
  free (priv);
  free (this);
  return;
}

