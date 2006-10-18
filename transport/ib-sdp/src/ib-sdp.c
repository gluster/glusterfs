/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
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

#include "sdp_inet.h"

#include "glusterfs.h"
#include "transport.h"
#include "logging.h"
#include "ib-sdp.h"

static int32_t
ibsdp_connect (struct transport *this, 
	       dict_t *address)
{
  struct ibsdp_private *priv = this->private;
  struct sockaddr_in sin;
  struct sockaddr_in sin_src;
  int32_t ret = 0;
  int32_t try_port = CLIENT_PORT_CIELING;

  if (priv->sock == -1)
    priv->sock = socket (AF_INET_SDP, SOCK_STREAM, 0);

  if (priv->sock == -1) {
    gf_log ("transport: ib-sdp: ", GF_LOG_ERROR, "try_connect: error: %s", strerror (errno));
    return -errno;
  }

  while (try_port){ 
    sin_src.sin_family = PF_INET;
    sin_src.sin_port = htons (try_port); //FIXME: have it a #define or configurable
    sin_src.sin_addr.s_addr = INADDR_ANY;
    
    if ((ret = bind (priv->sock, (struct sockaddr *)&sin_src, sizeof (sin_src))) == 0) {
      break;
    }
    
    try_port--;
  }
  
  if (ret != 0){
      gf_log ("transport: ib-sdp: ", GF_LOG_ERROR, "try_connect: error: %s", strerror (errno));
      close (priv->sock);
      return -errno;
  }

  sin.sin_family = AF_INET;
  sin.sin_port = priv->port;
  if (inet_aton (data_to_str (dict_get (address, "ip")), &sin.sin_addr) == 0) {
    gf_log ("transport: ib-sdp: ", GF_LOG_ERROR, "invalid address %s", dict_get (address, "ip"));
    exit (1);
  }

  if (connect (priv->sock, (struct sockaddr *)&sin, sizeof (sin)) != 0) {
    gf_log ("transport/ib-sdp", GF_LOG_ERROR, "try_connect: error: %s", strerror (errno));
    close (priv->sock);
    priv->sock = -1;
    return -errno;
  }

  priv->connected = 1;

  return ret;
}

/*
  TODO: Reconnection logic and queueing of requests
*/

static int32_t
ibsdp_send (struct transport *this,
	  int8_t *buf, 
	  int32_t len)
{
  struct ibsdp_private *priv = this->private;
  int ret;
  pthread_mutex_lock (&priv->mutex);
  ret = write (priv->sock, buf, len);
  pthread_mutex_unlock (&priv->mutex);
  return ret;
}

static int32_t
ibsdp_recieve (struct transport *this,
	     int8_t *buf, 
	     int32_t len)
{
  struct ibsdp_private *priv = this->private;
  int ret;
  pthread_mutex_lock (&priv->mutex);
  ret = read (priv->sock, buf, len);
  pthread_mutex_unlock (&priv->mutex);
  return ret;
}

struct transport_ops transport_ops = {
  .connect = ibsdp_connect,
  .send = ibsdp_send,
  .recieve = ibsdp_recieve
};

int 
init (struct transport *this)
{
  this->private = calloc (1, sizeof (struct transport));
  pthread_mutex_init (&((struct ibsdp_private *)this->private)->mutex, NULL);

  return 0;
}

int 
fini (struct transport *this)
{
  struct ibsdp_private *priv = this->private;
  close (priv->sock);
  free (priv);
  return 0;
}
