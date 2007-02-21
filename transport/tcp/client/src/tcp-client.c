/*
  (C) 2006, 2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
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
#include "tcp.h"

static int32_t 
do_handshake (transport_t *this, dict_t *options)
{
  GF_ERROR_IF_NULL (this);
  tcp_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);

  dict_t *request = get_new_dict ();
  dict_t *reply = get_new_dict ();
  data_t *rem_err = NULL;
  char *remote_subvolume = NULL;
  char *remote_error = NULL;
  int32_t ret;
  int32_t remote_errno;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  
  remote_subvolume = data_to_str (dict_get (options,
					    "remote-subvolume"));
  dict_set (request, 
	    "remote-subvolume",
	    data_from_dynstr (strdup (remote_subvolume)));
  //	    dict_get (options, "remote-subvolume"));

  {
    int32_t dict_len = dict_serialized_length (request);
    gf_log ("transport/tcp-client",
	    GF_LOG_DEBUG,
	    "dictionary length = %d", dict_len);
    char *dict_buf = calloc (dict_len, 1);
    dict_serialize (request, dict_buf);

    gf_block_t *blk = gf_block_new (424242); /* "random" number */
    blk->type = GF_OP_TYPE_MOP_REQUEST;
    blk->op = GF_MOP_SETVOLUME;
    blk->size = dict_len;
    blk->data = dict_buf;

    int32_t blk_len = gf_block_serialized_length (blk);
    char *blk_buf = malloc (blk_len);
    gf_block_serialize (blk, blk_buf);

    ret = full_write (priv->sock, blk_buf, blk_len);

    free (blk_buf);
    free (dict_buf);
    free (blk);
  }

  if (ret == -1) { 
    struct sockaddr_in sin;
    sin.sin_addr.s_addr = priv->addr;
    
    gf_log ("transport: tcp: ",
	    GF_LOG_ERROR,
	    "handshake with %s failed", 
	    inet_ntoa (sin.sin_addr));
    goto ret;
  }

  gf_block_t *reply_blk = gf_block_unserialize (priv->sock);
  if (!reply_blk) {
    gf_log ("transport: tcp: ",
	    GF_LOG_ERROR,
	    "gf_block_unserialize failed during handshake");
    ret = -1;
    goto reply_err;
  }

  if (!((reply_blk->type == GF_OP_TYPE_MOP_REPLY) &&
	(reply_blk->op == GF_MOP_SETVOLUME))) {
    gf_log ("transport: tcp: ",
	    GF_LOG_DEBUG,
	    "unexpected block type %d recieved during handshake",
	    reply_blk->type);
    ret = -1;
    goto reply_err;
  }

  //  dict_unserialize (reply_blk->data, reply_blk->size, &reply);
  reply = reply_blk->dict;
  
  ret = data_to_int (dict_get (reply, "RET"));
  remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  rem_err = dict_get (reply, "ERROR"); /* note that its not ERRNO */
  if (rem_err)
    remote_error = data_to_str (rem_err);

    
  if (ret < 0) {
    gf_log ("tcp/client",
	    GF_LOG_ERROR,
	    "SETVOLUME on remote server failed (%s)",
	    remote_error? remote_error : "Server not updated to newer version");
    errno = remote_errno;
    goto reply_err;
  }

 reply_err:
  if (reply_blk) {
    if (reply_blk->dict)
      dict_destroy (reply_blk->dict);
    free (reply_blk);
  }

 ret:
  dict_destroy (request);
  //  dict_destroy (reply);
  return ret;
}

static int32_t
tcp_connect (struct transport *this, 
	     dict_t *options)
{
  GF_ERROR_IF_NULL (this);
  
  tcp_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);
  
  if (!priv->options)
    priv->options = dict_copy (options, NULL);

  struct sockaddr_in sin;
  struct sockaddr_in sin_src;
  int32_t ret = 0;
  uint16_t try_port = CLIENT_PORT_CIELING;

  struct pollfd poll_s;
  int    nfds;
  int    timeout;
  int optval_s;
  unsigned int optvall_s = sizeof(int);

  // Create the socket if no connection ?
  if (priv->connected)
    return 0;

  if (!priv->connection_in_progress)
  {
    priv->sock = socket (AF_INET, SOCK_STREAM, 0);
    
    gf_log ("transport: tcp: ",
	    GF_LOG_DEBUG,
	    "try_connect: socket fd = %d", priv->sock);
  
    if (priv->sock == -1) {
      gf_log ("transport: tcp: ",
	      GF_LOG_ERROR,
	      "try_connect: socket () - error: %s",
	      strerror (errno));
      return -errno;
    }
	
    // Find a local port avaiable for use
    while (try_port) { 
      sin_src.sin_family = PF_INET;
      sin_src.sin_port = htons (try_port); //FIXME: have it a #define or configurable
      sin_src.sin_addr.s_addr = INADDR_ANY;
      
      if ((ret = bind (priv->sock,
		       (struct sockaddr *)&sin_src,
		       sizeof (sin_src))) == 0) {
	gf_log ("transport: tcp: ",
		GF_LOG_DEBUG,
		"try_connect: finalized on port `%d'",
		try_port);
	break;
      }
	
      try_port--;
    }
	
    if (ret != 0) {
      gf_log ("transport: tcp: ",
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
      gf_log ("tcp/client",
	      GF_LOG_DEBUG,
	      "try_connect: defaulting remote-port to %d", GF_DEFAULT_LISTEN_PORT);
      sin.sin_port = htons (GF_DEFAULT_LISTEN_PORT);
    }
	
    if (dict_get (options, "remote-host")) {
      sin.sin_addr.s_addr = resolve_ip (data_to_str (dict_get (options,
							       "remote-host")));
    } else {
      gf_log ("tcp/client",
	      GF_LOG_DEBUG,
	      "try_connect: error: missing 'option remote-host <hostname>'");
      close (priv->sock);
      return -errno;
    }

    // TODO, others ioctl, ioctlsocket, IoctlSocket, or if dont support
    fcntl (priv->sock, F_SETFL, O_NONBLOCK);

    // Try to connect
    ret = connect (priv->sock, (struct sockaddr *)&sin, sizeof (sin));
    
    if (ret == -1) {
      if (errno != EINPROGRESS)	{
	gf_log ("tcp/client",
		GF_LOG_ERROR,
		"try_connect: error: not in progress - trace: %s",
		strerror (errno));
	close (priv->sock);
	return -errno;
      }
    }

    gf_log ("tcp/client",
	    GF_LOG_DEBUG,
	    "connect on %d in progress (non-blocking)",
	    priv->sock);
    priv->connection_in_progress = 1;
    priv->connected = 0;
  }
  
  nfds = 1;
  memset (&poll_s, 0, sizeof(poll_s));
  poll_s.fd = priv->sock;
  poll_s.events = POLLOUT;
  timeout = 0; // Setup 50ms later, nonblock
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
      gf_log ("tcp/client", GF_LOG_ERROR, "SOCKET ERROR");
      close (priv->sock);
      return -1;
    }
    if (optval_s) {
      gf_log ("tcp/client", GF_LOG_ERROR,
	      "non-blocking connect() returned: %d (%s)",
	      optval_s, strerror (optval_s));
      close (priv->sock);
      return -1;
    }
  } else {
    /* connection is still in progress */
    gf_log ("tcp/client",
	    GF_LOG_DEBUG,
	    "connection on %d still in progress - try later",
	    priv->sock);
    return -1;
  }

  /* connection was successful */
  gf_log ("tcp/client",
	  GF_LOG_DEBUG,
	  "connection on %d success, attempting to handshake",
	  priv->sock);

  int flags = fcntl (priv->sock, F_GETFL, 0);
  fcntl (priv->sock, F_SETFL, flags & (~O_NONBLOCK));

  ret = do_handshake (this, options);
  if (ret != 0) {
	  gf_log ("tcp/client",
		  GF_LOG_ERROR,
		  "handshake: failed");
	  close (priv->sock);
	  return ret;
  }

  priv->connected = 1;
  priv->connection_in_progress = 0;

  return ret;
}

static int32_t
tcp_client_submit (transport_t *this, char *buf, int32_t len)
{
  tcp_private_t *priv = this->private;
  int32_t ret = 0;

  pthread_mutex_lock (&priv->write_mutex);
  if (!priv->connected) {
    ret = tcp_connect (this, priv->options);
    if (ret == 0) {
      transport_register (priv->sock, this);
      ret = full_write (priv->sock, buf, len);
    } else {
      ret = -1;
    }
  } else {
    ret = full_write (priv->sock, buf, len);
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
    ret = tcp_connect (this, priv->options);
    if (ret == 0) {
      transport_register (priv->sock, this);
      ret = full_writev (priv->sock, vector, count);
    } else {
      ret = -1;
    }
  } else {
    ret = full_writev (priv->sock, vector, count);
  }
  pthread_mutex_unlock (&priv->write_mutex);

  return ret;
}

static int32_t
tcp_client_except (transport_t *this)
{
  GF_ERROR_IF_NULL (this);

  tcp_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);

  priv->connected = 0;
  priv->connection_in_progress = 0;
  int ret = tcp_connect (this, priv->options);

  return ret;
}

struct transport_ops transport_ops = {
  //  .flush = tcp_flush,
  .recieve = tcp_recieve,

  .submit = tcp_client_submit,

  .disconnect = tcp_disconnect,
  .except = tcp_client_except,

  .readv = tcp_readv,
  .writev = tcp_client_writev
};

int 
init (struct transport *this,
      dict_t *options,
      int32_t (*notify) (xlator_t *xl, transport_t *trans, int32_t event))
{
  int32_t ret;
  data_t *retry_data;
  tcp_private_t *priv;

  priv = calloc (1, sizeof (tcp_private_t));
  this->private = priv;
  this->notify = notify;

  pthread_mutex_init (&priv->read_mutex, NULL);
  pthread_mutex_init (&priv->write_mutex, NULL);
  
  priv->connection_in_progress = 0;

  ret = tcp_connect (this, options);
  if (!ret) {
    transport_register (priv->sock, this);
  }

  if (ret) {
    retry_data = dict_get (options, "background-retry");
    if (retry_data) {
      if (strcasecmp (data_to_str (retry_data), "off") == 0)
	return -1;
    }
  }
  return 0;
}

int 
fini (struct transport *this)
{
  tcp_private_t *priv = this->private;
  //  this->ops->flush (this);

  dict_destroy (priv->options);
  close (priv->sock);
  free (priv);
  return 0;
}
