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
ib_verbs_client_connect (struct transport *this)
{
  GF_ERROR_IF_NULL (this);
  dict_t *options = this->xl->options;
  
  ib_verbs_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);
  
  char non_blocking = 1;

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
  int    timeout;
  int optval_s;
  unsigned int optvall_s = sizeof(int);

  // Create the socket if no connection ?
  if (priv->connected)
    return 0;

  if (!priv->connection_in_progress)
  {
    priv->sock = socket (AF_INET, SOCK_STREAM, 0);
    
    gf_log (this->xl->name, GF_LOG_DEBUG,
	    "socket fd = %d", priv->sock);
  
    if (priv->sock == -1) {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "socket () - error: %s", strerror (errno));
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
	gf_log (this->xl->name, GF_LOG_DEBUG,
		"finalized on port `%d'", try_port);
	break;
      }
	
      try_port--;
    }
	
    if (ret != 0) {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "bind loop failed - error: %s", strerror (errno));
      close (priv->sock);
      return -errno;
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
							       "remote-host")));
    } else {
      gf_log (this->xl->name,
	      GF_LOG_DEBUG,
	      "error: missing 'option remote-host <hostname>'");
      close (priv->sock);
      return -errno;
    }

    if (non_blocking) {
    // TODO, others ioctl, ioctlsocket, IoctlSocket, or if dont support
      fcntl (priv->sock, F_SETFL, O_NONBLOCK);
    }
    // Try to connect
    ret = connect (priv->sock, (struct sockaddr *)&sin, sizeof (sin));
    
    if (ret == -1) {
      if (errno != EINPROGRESS)	{
	gf_log (this->xl->name, GF_LOG_ERROR,
		"connection not in progress - trace: %s", strerror (errno));
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
	gf_log (this->xl->name, GF_LOG_ERROR, "SOCKET ERROR");
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
	  "connection on %d success, attempting to handshake",
	  priv->sock);

  if (non_blocking) {
    int flags = fcntl (priv->sock, F_GETFL, 0);
    fcntl (priv->sock, F_SETFL, flags & (~O_NONBLOCK));
  }

  if (ib_verbs_handshake (this) != 0) {
    gf_log (this->xl->name, GF_LOG_ERROR,
	    "ib_verbs_handshake failed");
    close (priv->sock);
    return -1;
  }

  socklen_t sock_len = sizeof (struct sockaddr_in);
  getpeername (priv->sock, &this->peerinfo.sockaddr, &sock_len);

  priv->connected = 1;
  priv->connection_in_progress = 0;

  poll_register (this->xl->ctx, priv->sock, transport_ref (this));

  return ret;
}


static int32_t
ib_verbs_client_writev (struct transport *this,
			const struct iovec *vector,
			int32_t count)
{
  ib_verbs_private_t *priv = this->private;
  int32_t ret = 0;

  pthread_mutex_lock (&priv->write_mutex);
  if (!priv->connected) {
    ret = -1; //ib_verbs_client_connect (this, this->xl->options);
  }
  if (ret == 0)
    ret = ib_verbs_writev (this, vector, count);
  pthread_mutex_unlock (&priv->write_mutex);

  return ret;
}


struct transport_ops transport_ops = {
  .recieve = ib_verbs_receive,
  //  .submit = ib_verbs_client_submit,
  .writev = ib_verbs_client_writev,

  .connect = ib_verbs_client_connect,
  .disconnect = ib_verbs_disconnect,
  .except = ib_verbs_except,

  .bail = ib_verbs_bail
};

int32_t 
gf_transport_init (transport_t *this,
		   dict_t *options,
		   event_notify_fn_t notify)
{
  ib_verbs_private_t *priv;

  priv = calloc (1, sizeof (ib_verbs_private_t));
  this->private = priv;
  this->notify = notify; //ib_verbs_tcp_notify;
  priv->notify = notify;

  /* Initialize the driver specific parameters */
  if (ib_verbs_init (this)) {
    gf_log (this->xl->name, GF_LOG_ERROR, 
	    "%s: failed to initialize IB device",
	    this->xl->name);
    return -1;
  }

  return 0;
}

void  
gf_transport_fini (struct transport *this)
{
  /* TODO: proper cleaning */
  return ;
}
