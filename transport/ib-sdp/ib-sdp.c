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
#include "ib-sdp.h"
#include <signal.h>
#include <fcntl.h>

int32_t
ib_sdp_recieve (struct transport *this,
		char *buf, 
		int32_t len)
{
  GF_ERROR_IF_NULL (this);

  ib_sdp_private_t *priv = this->private;
  int ret = 0;

  GF_ERROR_IF_NULL (priv);
  GF_ERROR_IF_NULL (buf);
  GF_ERROR_IF (len < 0);

  if (!priv->connected)
    return -1;
  
  pthread_mutex_lock (&priv->read_mutex);
  ret = gf_full_read (priv->sock, buf, len);
  pthread_mutex_unlock (&priv->read_mutex);
  return ret;
}

int32_t
ib_sdp_readv (struct transport *this,
	      const struct iovec *vector,
	      int32_t count)
{
  ib_sdp_private_t *priv = this->private;
  int ret = 0;

  if (!priv->connected)
    return -1;
  
  pthread_mutex_lock (&priv->read_mutex);
  ret = gf_full_readv (priv->sock, vector, count);
  pthread_mutex_unlock (&priv->read_mutex);
  return ret;
}

int32_t 
ib_sdp_disconnect (transport_t *this)
{
  ib_sdp_private_t *priv = this->private;
  int32_t ret = 0;

  pthread_mutex_lock (&priv->write_mutex);
  if (priv->connected) {
    if (close (priv->sock) != 0) {
      gf_log ("transport/ib-sdp",
	      GF_LOG_ERROR,
	      "close () - error: %s",
	      strerror (errno));
      ret = -errno;
    }
    priv->connected = 0;
  }
  pthread_mutex_unlock (&priv->write_mutex);

  return ret;
}

int32_t 
ib_sdp_except (transport_t *this)
{
  ib_sdp_private_t *priv = this->private;
  int32_t ret = 0;

  //  pthread_mutex_lock (&priv->write_mutex);
  if (priv->connected) {
    fcntl (priv->sock, F_SETFL, O_NONBLOCK);
    if (shutdown (priv->sock, SHUT_RDWR) != 0) {
      gf_log ("transport/ib-sdp",
	      GF_LOG_ERROR,
	      "shutdown () - error: %s",
	      strerror (errno));
      ret = -errno;
    }
  }
  //pthread_mutex_unlock (&priv->write_mutex);

  return ret;
}

static void
cont_hand (int32_t sig)
{
  gf_log ("ib-sdp",
          GF_LOG_DEBUG,
          "forcing poll/read/write to break on blocked socket (if any)");
}

int32_t 
ib_sdp_bail (transport_t *this)
{
  /*
  ib_sdp_private_t *priv = this->private;
  fcntl (priv->sock, F_SETFL, O_NONBLOCK);
  shutdown (priv->sock, SHUT_RDWR);
  */
  ib_sdp_except (this);
  signal (SIGCONT, cont_hand);
  raise (SIGCONT);

  return 0;
}

