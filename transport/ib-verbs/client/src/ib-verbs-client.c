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
#include "logging.h"
#include "xlator.h"
#include "protocol.h"

#include "ib-verbs.h"


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

    memcpy (priv->buf[0], blk_buf, blk_len);
    ret = ib_verbs_post_send (priv, blk_len, 0); //TODO

    free (blk_buf);
    free (dict_buf);
    free (blk);
  }

  if (ret == -1) { 
    struct sockaddr_in sin;
    sin.sin_addr.s_addr = priv->addr;
    
    gf_log ("transport: ib-verbs: ",
	    GF_LOG_ERROR,
	    "handshake with %s failed", 
	    inet_ntoa (sin.sin_addr));
    goto ret;
  }

  /*  char buf[4096] = {0,};
      ib_verbs_full_read (priv, buf, 4096);  //TODO */

  gf_block *reply_blk = gf_block_unserialize_transport (this);
  if (!reply_blk) {
    gf_log ("transport: ib-verbs: ",
	    GF_LOG_ERROR,
	    "gf_block_unserialize failed during handshake");
    ret = -1;
    goto reply_err;
  }
  

  if (!((reply_blk->type == GF_OP_TYPE_FOP_REPLY) || 
	(reply_blk->type == GF_OP_TYPE_MOP_REPLY))) {
    gf_log ("transport: ib-verbs: ",
	    GF_LOG_DEBUG,
	    "unexpected block type %d recieved during handshake",
	    reply_blk->type);
    ret = -1;
    goto reply_err;
  }

  dict_unserialize (reply_blk->data, reply_blk->size, &reply);
  
  if (reply == NULL) {
    gf_log ("transport: ib-verbs: ",
	    GF_LOG_ERROR,
	    "dict_unserialize failed");
    ret = -1;
    goto reply_err;
  }

  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
  if (ret < 0) {
    gf_log ("ib-verbs/client",
	    GF_LOG_ERROR,
	    "SETVOLUME on remote server failed (%s)",
	    strerror (errno));
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
ib_verbs_connect (struct transport *this, 
	     dict_t *options)
{
  GF_ERROR_IF_NULL (this);

  ib_verbs_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);
  
  if (!priv->options)
    priv->options = dict_copy (options);

  struct sockaddr_in sin;
  struct sockaddr_in sin_src;
  int32_t ret = 0;
  uint16_t try_port = CLIENT_PORT_CIELING;

  if (!priv->connected)
    priv->sock = socket (AF_INET, SOCK_STREAM, 0);

  gf_log ("transport: ib-verbs: ",
	  GF_LOG_DEBUG,
	  "try_connect: socket fd = %d", priv->sock);

  if (priv->sock == -1) {
    gf_log ("transport: ib-verbs: ",
	    GF_LOG_ERROR,
	    "try_connect: socket () - error: %s",
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
      gf_log ("transport: ib-verbs: ",
	      GF_LOG_DEBUG,
	      "try_connect: finalized on port `%d'",
	      try_port);
      break;
    }
    
    try_port--;
  }
  
  if (ret != 0) {
      gf_log ("transport: ib-verbs: ",
	      GF_LOG_ERROR,
	      "try_connect: bind loop failed - error: %s",
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
	    "try_connect: defaulting remote-port to %d", GF_DEFAULT_LISTEN_PORT);
    sin.sin_port = htons (GF_DEFAULT_LISTEN_PORT);
  }

  if (dict_get (options, "remote-host")) {
    sin.sin_addr.s_addr = resolve_ip (data_to_str (dict_get (options, "remote-host")));
  } else {
    gf_log ("ib-verbs/client",
	    GF_LOG_DEBUG,
	    "try_connect: error: missing 'option remote-host <hostname>'");
    close (priv->sock);
    return -errno;
  }

  if (connect (priv->sock, (struct sockaddr *)&sin, sizeof (sin)) != 0) {
    gf_log ("transport/ib-verbs",
	    GF_LOG_ERROR,
	    "try_connect: connect () - error: %s",
 	    strerror (errno));
    close (priv->sock);
    return -errno;
  }

  ib_verbs_create_qp (priv, 0);
  ib_verbs_create_qp (priv, 1);

  /* Keep the read requests in the queue */
  ib_verbs_post_recv (priv, CMD_BUF_SIZE, IBVERBS_CMD_QP);
  
  char msg[256] = {0,};

  sprintf (msg, "%04x:%06x:%06x:%04x:%06x:%06x", 
	   priv->local[0].lid, priv->local[0].qpn, priv->local[0].psn,
	   priv->local[1].lid, priv->local[1].qpn, priv->local[1].psn);
  write (priv->sock, msg, sizeof msg);
  
  read (priv->sock, msg, sizeof msg);
  sscanf (msg, "%04x:%06x:%06x:%04x:%06x:%06x", 
	  &priv->remote[0].lid, &priv->remote[0].qpn, &priv->remote[0].psn,
	  &priv->remote[1].lid, &priv->remote[1].qpn, &priv->remote[1].psn);
  gf_log ("ib-verbs/client", GF_LOG_DEBUG, "msg = %s", msg);

  ib_verbs_ibv_connect (priv, 1, IBV_MTU_1024);
  
  ret = do_handshake (this, options);

  if (ret != 0) {
    gf_log ("transport: ib-verbs: ", GF_LOG_ERROR, "handshake failed");
    close (priv->sock);
    return ret;
  }

  priv->connected = 1;

  return ret;
}

static int32_t
ib_verbs_client_submit (transport_t *this, char *buf, int32_t len)
{
  //TODO: Add logic to put in queue and all
  ib_verbs_private_t *priv = this->private;

  int32_t qp_idx = 0;

  if (len > CMD_BUF_SIZE) {
    qp_idx = IBVERBS_DATA_QP;
    /* Check if already registered memory is enough or not */
    if (len > priv->data_buf_size) {
      /* If not allocate a bigger memory and give it to qp */
      /* Unregister the old mr[1]. */
      priv->buf[IBVERBS_DATA_QP] = calloc (1, len + 1);
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

  if (!priv->connected) {
    int ret = ib_verbs_connect (this, priv->options);
    if (ret == 0) {
      return ib_verbs_post_send (priv, len, qp_idx);
    }
    else
      return -1;
  }

  return ib_verbs_post_send (priv, len, qp_idx);
}

static int32_t
ib_verbs_client_except (transport_t *this)
{
  // TODO : clear properly
  GF_ERROR_IF_NULL (this);

  ib_verbs_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);
  
  gf_log ("ib-verbs/client", GF_LOG_ERROR, "except");

  priv->connected = 0;
  int ret = ib_verbs_connect (this, priv->options);

  return ret;
}

struct transport_ops transport_ops = {
  //  .flush = ib_verbs_flush,
  .recieve = ib_verbs_recieve,

  .submit = ib_verbs_client_submit,

  .disconnect = ib_verbs_disconnect,
  .except = ib_verbs_client_except
};

int 
init (struct transport *this,
      dict_t *options,
      int32_t (*notify) (xlator_t *xl, transport_t *trans, int32_t event))
{
  ib_verbs_private_t *priv = calloc (1, sizeof (ib_verbs_private_t));
  this->private = priv;
  this->notify = ib_verbs_cq_notify;
  priv->notify = notify;

  ib_verbs_ibv_init (this->private);

  /* Register Channel fd for getting event notification on CQ */
  register_transport (this, priv->channel->fd);

  /* TODO: need to check out again : */
  int ret = ib_verbs_connect (this, options);
  if (ret != 0) {
    gf_log ("transport: ib-verbs: client: ", GF_LOG_ERROR, "init failed");
    return -1;
  }
  
  return 0;
}

int 
fini (struct transport *this)
{
  //TODO: proper cleaning
  ib_verbs_private_t *priv = this->private;
  //  this->ops->flush (this);

  dict_destroy (priv->options);
  close (priv->sock);
  free (priv);
  return 0;
}
