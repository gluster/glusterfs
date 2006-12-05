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
#include "vapi.h"


int32_t 
vapi_full_read (vapi_private_t *priv, char *buf, int32_t len)
{
  return 0;
}

int32_t
vapi_recieve (struct transport *this,
	      char *buf, 
	      int32_t len)
{
  GF_ERROR_IF_NULL (this);

  vapi_private_t *priv = this->private;
  int ret = 0;

  GF_ERROR_IF_NULL (priv);
  GF_ERROR_IF_NULL (buf);
  GF_ERROR_IF (len < 0);

  if (!priv->connected)
    return -1;
  
  //  pthread_mutex_lock (&priv->read_mutex);
  // ret = full_read (priv->sock, buf, len);
  ret = vapi_full_read (priv, buf, len);
  //  pthread_mutex_unlock (&priv->read_mutex);
  return ret;
}

int32_t 
vapi_disconnect (transport_t *this)
{
  vapi_private_t *priv = this->private;

  if (close (priv->sock) != 0) {
    gf_log ("transport/vapi",
	    GF_LOG_ERROR,
	    "vapi_disconnect: close () - error: %s",
	    strerror (errno));
    return -errno;
  }

  priv->connected = 0;
  return 0;
}


