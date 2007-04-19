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
#include "logging.h"
#include "xlator.h"
#include "protocol.h"

#include "ib-verbs.h"


static int32_t 
ib_verbs_client_disconnect (transport_t *this)
{
  ib_verbs_private_t *priv = this->private;

  /* Free everything allocated, registered */
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
  if (priv->peers[0].qp) {
    ibv_destroy_qp (priv->peers[0].qp);
    priv->peers[0].qp = NULL;
  }
  if (priv->peers[1].qp) {
    ibv_destroy_qp (priv->peers[1].qp);
    priv->peers[1].qp = NULL;
  }
  if (priv->pd) {
    ibv_dealloc_pd (priv->pd);
    priv->pd = NULL;
  }
  /* ibv_destroy_cq (priv->ibv.sendcq[0]);
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
  priv->connected = 0;
  return 0;
}

static int32_t 
ib_verbs_client_notify (xlator_t *xl, 
			transport_t *trans, 
			int32_t event) 
{
  ib_verbs_private_t *priv = trans->private;
  priv->connected = 0;
  poll_unregister (xl->ctx, priv->sock);
  return 0;
}


static int32_t  
do_handshake (transport_t *this, dict_t *options)
{
  GF_ERROR_IF_NULL (this);
  ib_verbs_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);
  
  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  int32_t ret;
  int32_t remote_errno;
  char *remote_subvolume = NULL;
  char *remote_error;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  
  remote_subvolume = data_to_str (dict_get (options,
					    "remote-subvolume"));
  dict_set (request, 
	    "remote-subvolume",
	    data_from_dynstr (strdup (remote_subvolume)));
  
  {
    int32_t dict_len = dict_serialized_length (request);
    char *dict_buf = malloc (dict_len);
    dict_serialize (request, dict_buf);

    gf_block *blk = gf_block_new (424242); /* "random" number */
    blk->type = GF_OP_TYPE_MOP_REQUEST;
    blk->op = GF_MOP_SETVOLUME;
    blk->size = dict_len;
    blk->data = dict_buf;

    int32_t blk_len = gf_block_serialized_length (blk);
    char *blk_buf = malloc (blk_len);
    gf_block_serialize (blk, blk_buf);

    ib_verbs_post_t *post = priv->peers[0].send_list;
    priv->peers[0].send_list = post->next;
    memcpy (post->buf, blk_buf, blk_len);
    
    {
      ret = ib_verbs_post_send (this, &priv->peers[0], post, blk_len); 
      struct ibv_wc wc;
      struct ibv_cq *ev_cq;
      void *ev_ctx;

      ibv_get_cq_event (priv->ctx->send_chan[0], &ev_cq, &ev_ctx);
      ibv_poll_cq (priv->ctx->send_cq[0], 1, &wc);
      ibv_req_notify_cq (priv->ctx->send_cq[0], 0);
      
      ib_verbs_comp_t *comp = (ib_verbs_comp_t *)(long)wc.wr_id;
      ib_verbs_peer_t *peer = comp->peer;
      ib_verbs_post_t *post = comp->post;
      
      if (comp->type != 0) {
	/* error :O */
	gf_log ("ib-verbs/client", 
		GF_LOG_ERROR, 
		"%s: Error in sending handshake message",
		this->xl->name);
	free (comp);
	return -1;
      }
      free (comp);
      post->next = peer->send_list;
      peer->send_list = post;
    }

    free (blk_buf);
    free (dict_buf);
    free (blk);
  }

  if (ret == -1) { 
    struct sockaddr_in sin;
    sin.sin_addr.s_addr = priv->addr;
    
    gf_log ("ib-verbs/client",
	    GF_LOG_ERROR,
	    "%s: handshake with %s failed",
	    this->xl->name,
	    inet_ntoa (sin.sin_addr));
    goto ret;
  }

  gf_block *reply_blk = gf_block_unserialize_transport (this);
  if (!reply_blk) {
    gf_log ("ib-verbs/client",
	    GF_LOG_ERROR,
	    "%s: gf_block_unserialize failed during handshake",
	    this->xl->name);
    ret = -1;
    goto reply_err;
  }

  if (reply_blk->data)
    dict_unserialize (reply_blk->data, reply_blk->size, &reply);
  else
    goto reply_err;

  if (reply == NULL) {
    gf_log ("ib-verbs/client",
	    GF_LOG_ERROR,
	    "%s: dict_unserialize failed", this->xl->name);
    ret = -1;
    goto reply_err;
  }

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  remote_error = data_to_str (dict_get (reply, "ERROR")); /* note that its not 'errno' */
  
  if (ret < 0) {
    gf_log ("ib-verbs/client",
	    GF_LOG_ERROR,
	    "%s: SETVOLUME on remote server failed (%s)",
	    this->xl->name,
	    remote_error);
    errno = remote_errno;
    goto reply_err;
  }

 reply_err:
  if (reply_blk) {
    if (reply_blk->data)
      free (reply_blk->data);
    free (reply_blk);
  }
		   
 ret:
  dict_destroy (request);
  dict_destroy (reply);
  return ret;
}

static int32_t
ib_verbs_connect (transport_t *this, 
		  dict_t *options)
{
  GF_ERROR_IF_NULL (this);
  
  ib_verbs_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);
  
  struct sockaddr_in sin;
  struct sockaddr_in sin_src;
  int32_t ret = 0;
  uint16_t try_port = CLIENT_PORT_CIELING;

  if (!priv->connected)
    priv->sock = socket (AF_INET, SOCK_STREAM, 0);

  gf_log ("ib-verbs/client",
	  GF_LOG_DEBUG,
	  "%s: socket fd = %d", this->xl->name, priv->sock);

  if (priv->sock == -1) {
    gf_log ("ib-verbs/client",
	    GF_LOG_ERROR,
	    "%s: socket () - error: %s",
	    this->xl->name,
	    strerror (errno));
    return -errno;
  }

  while (try_port) { 
    sin_src.sin_family = AF_INET;
    sin_src.sin_port = htons (try_port); //FIXME: have it a #define or configurable
    sin_src.sin_addr.s_addr = INADDR_ANY;
    
    if ((ret = bind (priv->sock,
		     (struct sockaddr *)&sin_src,
		     sizeof (sin_src))) == 0) {
      gf_log ("ib-verbs/client",
	      GF_LOG_DEBUG,
	      "%s: finalized on port `%d'",
	      this->xl->name,
	      try_port);
      break;
    }
    
    try_port--;
  }
  
  if (ret != 0) {
      gf_log ("ib-verbs/client",
	      GF_LOG_ERROR,
	      "%s: bind loop failed - error: %s",
	      this->xl->name,
	      strerror (errno));
      close (priv->sock);
      return -errno;
  }

  sin.sin_family = AF_INET;

  if (dict_get (options, "remote-port")) {
    sin.sin_port = htons (data_to_int (dict_get (options,
						 "remote-port")));
  } else {
    gf_log ("ib-verbs/client",
	    GF_LOG_DEBUG,
	    "%s: defaulting remote-port to %d",
	    this->xl->name,
	    GF_DEFAULT_LISTEN_PORT);
    sin.sin_port = htons (GF_DEFAULT_LISTEN_PORT);
  }

  if (dict_get (options, "remote-host")) {
    sin.sin_addr.s_addr = gf_resolve_ip (data_to_str (dict_get (options, 
								"remote-host")));
  } else {
    gf_log ("ib-verbs/client",
	    GF_LOG_ERROR,
	    "missing 'option remote-host <hostname>'");
    close (priv->sock);
    return -errno;
  }

  if (connect (priv->sock, (struct sockaddr *)&sin, sizeof (sin)) != 0) {
    gf_log ("ib-verbs/client",
	    GF_LOG_ERROR,
	    "%s: connect () - error: %s",
	    this->xl->name,
 	    strerror (errno));
    close (priv->sock);
    return -errno;
  }

  ret = ib_verbs_conn_setup (this);
  if (ret != 0) {
    gf_log ("ib-verbs/client",
	    GF_LOG_ERROR,
	    "%s: IB connection setup failed",
	    this->xl->name);
    close (priv->sock);
    return -1;
  }
  
  ret = do_handshake (this, options);

  if (ret != 0) {
    gf_log ("ib-verbs/client", 
	    GF_LOG_ERROR, "%s: handshake failed",
	    this->xl->name);

    close (priv->sock);
    /* TODO:
       free buf list
       disconnect qp
       destroy qp
       undo post recv's
    */
    return ret;
  }

  this->notify = ib_verbs_client_notify; //for server disconnect
  poll_register (this->xl->ctx, priv->sock, this);

  priv->connected = 1;

  return ret;
}


static int32_t
ib_verbs_client_except (transport_t *this)
{
  /* TODO : Check whether this is enough */
  /* Need to free few of the pointers already allocated */
  GF_ERROR_IF_NULL (this);

  ib_verbs_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);
  
  gf_log ("ib-verbs/client", GF_LOG_ERROR, "except");

  priv->connected = 0;
  int32_t ret = ib_verbs_connect (this, this->xl->options);

  return ret;
}

static int32_t 
ib_verbs_client_writev (struct transport *this,
			const struct iovec *vector,
			int32_t count)
{
  ib_verbs_private_t *priv = this->private;

  if (!priv->connected) {
    int32_t ret = ib_verbs_connect (this, this->xl->options);
    if (ret)
      return -ENOTCONN;
  }

  return ib_verbs_writev (this, vector, count);
}

struct transport_ops transport_ops = {
  .recieve = ib_verbs_recieve,
  //  .submit = ib_verbs_client_submit,
  .writev = ib_verbs_client_writev,

  .disconnect = ib_verbs_client_disconnect,
  .except = ib_verbs_client_except,

  .bail = ib_verbs_bail
};

int32_t 
gf_transport_init (struct transport *this,
		   dict_t *options,
		   int32_t (*notify) (xlator_t *xl,
				      transport_t *trans,
				      int32_t event))
{
  ib_verbs_private_t *priv = calloc (1, sizeof (ib_verbs_private_t));
  this->private = priv;
  priv->notify = notify;

  /* Initialize the driver specific parameters */
  if (ib_verbs_init (this)) {
    gf_log ("ib-verbs/client", 
	    GF_LOG_ERROR, 
	    "%s: failed to initialize IB device",
	    this->xl->name);
    return -1;
  }

  pthread_mutex_init (&priv->read_mutex, NULL);
  pthread_mutex_init (&priv->write_mutex, NULL);

  int ret = ib_verbs_connect (this, options);
  if (ret != 0) {
    gf_log ("ib-verbs/client", 
	    GF_LOG_ERROR, "%s: init failed",
	    this->xl->name);
    //    return -1;
  }
  
  return 0;
}

void  
gf_transport_fini (struct transport *this)
{
  /* TODO: proper cleaning */
  ib_verbs_private_t *priv = this->private;

  /* This cleans up all the ib-verbs related pointers */
  ib_verbs_client_disconnect (this);

  //  dict_destroy (priv->options);
  close (priv->sock);
  free (priv);
  return ;
}
