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

int32_t fini (struct transport *this);  

static int32_t
ib_verbs_server_submit (transport_t *this, char *buf, int32_t len)
{
  ib_verbs_private_t *priv = this->private;

  if (!priv->connected)
    return -1;

  /* See if the buffer (memory region) is free, then send it */
  int32_t qp_idx = 0;
  if (len <= priv->ibv.qp[0].send_wr_size + 2048) {
    qp_idx = IBVERBS_CMD_QP;
  } else {
    qp_idx = IBVERBS_MISC_QP;

    if (!priv->ibv.qp[1].send_wr_list)
      priv->ibv.qp[1].send_wr_list = calloc (1, sizeof (ib_mr_struct_t));

    if (priv->ibv.qp[1].send_wr_list->buf_size < len) {
      /* Already allocated data buffer is not enough, allocate bigger chunk */
      if (priv->ibv.qp[1].send_wr_list->buf)
	free (priv->ibv.qp[1].send_wr_list->buf);
      priv->ibv.qp[1].send_wr_list->buf = valloc (len + 2048);
      priv->ibv.qp[1].send_wr_list->buf_size = len + 2048;
      memset (priv->ibv.qp[1].send_wr_list->buf, 0, len + 2048);
      priv->ibv.qp[1].send_wr_list->mr = ibv_reg_mr(priv->ibv.pd, 
						    priv->ibv.qp[1].send_wr_list->buf, 
						    len + 2048,
						    IBV_ACCESS_LOCAL_WRITE);
      if (!priv->ibv.qp[1].send_wr_list->mr) {
	gf_log ("transport/ib-verbs", GF_LOG_CRITICAL, "Couldn't allocate MR[0]\n");
	return -1;
      }
    }
    sprintf (priv->ibv.qp[0].send_wr_list->buf, 
	     "NeedDataMR:%d\n", len + 4);
    ib_verbs_post_send (this, &priv->ibv.qp[0], 20);
  }
  
  memcpy (priv->ibv.qp[qp_idx].send_wr_list->buf, buf, len);
  if (ib_verbs_post_send (this, &priv->ibv.qp[qp_idx], len) < 0) {
    return -EINTR;
  }
  return len;
}


static int32_t 
ib_verbs_server_disconnect (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;

  /* Free anything thats allocated per connection, registered */
  /* dereg_mr */
  ib_mr_struct_t *temp, *trav = priv->ibv.qp[0].recv_wr_list;
  while (trav) {
    temp = trav;
    free (trav->buf);
    ibv_dereg_mr (trav->mr);
    trav = trav->next;
    free (temp);
  }
  trav = priv->ibv.qp[1].recv_wr_list;
  while (trav) {
    temp = trav;
    free (trav->buf);
    ibv_dereg_mr (trav->mr);
    trav = trav->next;
    free (temp);
  }
  trav = priv->ibv.qp[0].send_wr_list;
  while (trav) {
    temp = trav;
    free (trav->buf);
    ibv_dereg_mr (trav->mr);
    trav = trav->next;
    free (temp);
  }
  trav = priv->ibv.qp[1].send_wr_list;
  while (trav) {
    temp = trav;
    free (trav->buf);
    ibv_dereg_mr (trav->mr);
    trav = trav->next;
    free (temp);
  }
  /* destroy_qp */
  ibv_destroy_qp (priv->ibv.qp[0].qp);
  ibv_destroy_qp (priv->ibv.qp[1].qp);

  priv->connected = 0;
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

  .submit = ib_verbs_server_submit,
  .except = ib_verbs_server_except,

  .readv = ib_verbs_readv,
  .writev = ib_verbs_writev
};

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
  this->notify = ib_verbs_send_cq_notify;

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
  int32_t recv_buf_size[2], send_buf_size[2]; //change 2->3 later
  read (priv->sock, buf, 256);

  /* Get the ibv options from xl->options */
  priv->ibv.qp[0].send_wr_count = 4;
  priv->ibv.qp[0].recv_wr_count = 4;
  priv->ibv.qp[0].send_wr_size = 131072; //65536; //128kB
  priv->ibv.qp[0].recv_wr_size = 131072; //65536;
  priv->ibv.qp[1].send_wr_count = 2;
  priv->ibv.qp[1].recv_wr_count = 2;

  data_t *temp =NULL;
  temp = dict_get (this->xl->options, "ibv-send-wr-count");
  if (temp)
    priv->ibv.qp[0].send_wr_count = data_to_int (temp);

  temp = dict_get (this->xl->options, "ibv-recv-wr-count");
  if (temp)
    priv->ibv.qp[0].recv_wr_count = data_to_int (temp);

  temp = dict_get (this->xl->options, "ibv-send-wr-size");
  if (temp)
    priv->ibv.qp[0].send_wr_size = data_to_int (temp);
  temp = dict_get (this->xl->options, "ibv-recv-wr-size");
  if (temp)
    priv->ibv.qp[0].recv_wr_size = data_to_int (temp);

  sscanf (buf, "QP1:LID=%04x:QPN=%06x:PSN=%06x:RECV_BLKSIZE=%08x:SEND_BLKSIZE=%08x\n"
	  "QP2:LID=%04x:QPN=%06x:PSN=%06x:RECV_BLKSIZE=%08x:SEND_BLKSIZE=%08x\n",
	  &priv->ibv.qp[0].remote_lid,
	  &priv->ibv.qp[0].remote_qpn,
	  &priv->ibv.qp[0].remote_psn,
 	  &send_buf_size[0],
	  &recv_buf_size[0],
	  &priv->ibv.qp[1].remote_lid,
	  &priv->ibv.qp[1].remote_qpn,
	  &priv->ibv.qp[1].remote_psn,
	  &send_buf_size[1],
	  &recv_buf_size[1]);
  
  if (recv_buf_size[0] < priv->ibv.qp[0].recv_wr_size)
    priv->ibv.qp[0].recv_wr_size = recv_buf_size[0];
  if (recv_buf_size[1] < priv->ibv.qp[1].recv_wr_size)
    priv->ibv.qp[1].recv_wr_size = recv_buf_size[1];
  if (send_buf_size[0] < priv->ibv.qp[0].send_wr_size)
    priv->ibv.qp[0].send_wr_size = send_buf_size[0];
  if (send_buf_size[1] < priv->ibv.qp[1].send_wr_size)
    priv->ibv.qp[1].send_wr_size = send_buf_size[1];

  // open a qp here and get the qpn and all.
  if (ib_verbs_create_qp (priv) < 0) {
    gf_log ("ib-verbs/server", GF_LOG_CRITICAL, "failed to create QP");
    return -1;
  }

  sprintf (buf, "QP1:LID=%04x:QPN=%06x:PSN=%06x:RECV_BLKSIZE=%08x:SEND_BLKSIZE=%08x\n"
	   "QP2:LID=%04x:QPN=%06x:PSN=%06x:RECV_BLKSIZE=%08x:SEND_BLKSIZE=%08x\n",
	   priv->ibv.qp[0].local_lid,
	   priv->ibv.qp[0].local_qpn,
	   priv->ibv.qp[0].local_psn,
	   priv->ibv.qp[0].recv_wr_size,
	   priv->ibv.qp[0].send_wr_size,
	   priv->ibv.qp[1].local_lid,
	   priv->ibv.qp[1].local_qpn,
	   priv->ibv.qp[1].local_psn,
	   priv->ibv.qp[1].recv_wr_size,
	   priv->ibv.qp[1].send_wr_size);

  write (priv->sock, buf, sizeof buf);
  
  // Create memory buffer (buf, mr etc)
  if (ib_verbs_create_buf_list (&priv->ibv) < 0) {
    gf_log ("ib-verbs/server", GF_LOG_CRITICAL, "Failed to create buffers for QP");
    return -1;
  }

  /* Keep recv requests always in the receive queue */
  int32_t i = 0;
  transport_t *this_recv = calloc (1, sizeof (transport_t));
  transport_t *this_send = calloc (1, sizeof (transport_t));

  for (i = 0; i < priv->ibv.qp[0].recv_wr_count; i++)
    ib_verbs_post_recv (this, &priv->ibv.qp[0]);

  if (ib_verbs_ibv_connect (priv, 1, IBV_MTU_1024)) {
    gf_log ("ib-verbs/server", 
	    GF_LOG_CRITICAL, 
	    "Failed to connect with remote qp");
  }

  int sock_len = sizeof (struct sockaddr_in);
  getpeername (priv->sock,
	       &this->peerinfo.sockaddr,
	       &sock_len);

  gf_log ("ib-verbs/server",
	  GF_LOG_DEBUG,
	  "Registering transport object of %s",
	  data_to_str (dict_get (priv->options, "remote-host")));
  
  close (priv->sock); // no use keeping this socket open.

  pthread_mutex_init (&priv->read_mutex, NULL);
  pthread_mutex_init (&priv->write_mutex, NULL);

  /* Replace the socket fd with the channel->fd of ibv */
  memcpy (this_recv, this, sizeof (transport_t));
  memcpy (this_send, this, sizeof (transport_t));

  priv->sock = priv->ibv.send_channel[0]->fd;

  this_recv->notify = ib_verbs_recv_cq_notify;
  this_send->notify = ib_verbs_send_cq_notify1;
  if (!trans_priv->registered) {
    /* This is to make sure that not more than one fd is registered per CQ */
    register_transport (this, priv->ibv.send_channel[0]->fd);
    register_transport (this_recv, priv->ibv.recv_channel[0]->fd);
    register_transport (this_send, priv->ibv.send_channel[1]->fd);
    trans_priv->registered = 1;
  }
  
  return 0;
}

/* Initialization function */
int32_t 
init (struct transport *this, 
      dict_t *options,
      int32_t (*notify) (xlator_t *xl, transport_t *trans, int32_t))
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
  if(ib_verbs_ibv_init (&priv->ibv)) {
    gf_log ("ib-verbs/server", GF_LOG_ERROR, "Failed to initialize IB Device");
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

  /* Register the main socket */
  register_transport (this, priv->sock);

  return 0;
}

int 
fini (struct transport *this)
{
  //TODO: verify this function does graceful finish 

  ib_verbs_private_t *priv = this->private;
  //  this->ops->flush (this);

  if (priv->options)
    gf_log ("ib-verbs/server",
	    GF_LOG_DEBUG,
	    "destroying transport object for %s:%s (fd=%d)",
	    data_to_str (dict_get (priv->options, "remote-host")),
	    data_to_str (dict_get (priv->options, "remote-port")),
	    priv->sock);

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

  if (priv->options)
    dict_destroy (priv->options);
  if (priv->connected)
    close (priv->sock);
  free (priv);
  free (this);
  return 0;
}

