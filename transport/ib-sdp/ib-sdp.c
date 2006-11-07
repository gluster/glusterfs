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

#include "transport.h"
#include "ib-sdp.h"

int32_t
ib_sdp_flush (transport_t *this)
{
  GF_ERROR_IF_NULL (this);

  ib_sdp_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);

  int ret = 0;

  if (!priv->connected) 
    return -1;

  pthread_mutex_lock (&priv->write_mutex);
  pthread_mutex_lock (&priv->queue_mutex);
  struct wait_queue *w = priv->queue;
  while (w) {
    ret = full_write (priv->sock, w->buf, w->len);
    if (ret < 0) {
      goto err;
    }
    struct wait_queue *prev = w;
    w = w->next;

    free (prev->buf);
    priv->queue = prev->next;
    free (prev);
  }

 err:
  pthread_mutex_unlock (&priv->queue_mutex);
  pthread_mutex_unlock (&priv->write_mutex);
  return ret;
}

int32_t
ib_sdp_recieve (transport_t *this,
		char *buf, 
		int32_t len)
{
  GF_ERROR_IF_NULL (this);

  ib_sdp_private_t *priv = this->private;

  GF_ERROR_IF_NULL (priv);
  GF_ERROR_IF_NULL (buf);
  GF_ERROR_IF (len < 0);

  int ret = 0;

  if (!priv->connected)
    return -1;

  pthread_mutex_lock (&priv->read_mutex);
  ret = full_read (priv->sock, buf, len);
  pthread_mutex_unlock (&priv->read_mutex);
  return ret;
}

int32_t
ib_sdp_submit (transport_t *this, 
	       char *buf, 
	       int32_t len)
{
  GF_ERROR_IF_NULL (this);

  ib_sdp_private_t *priv = this->private;
  GF_ERROR_IF_NULL (priv);
  
  struct wait_queue *w = calloc (1, sizeof (struct wait_queue));
  w->buf = calloc (len, 1);
  memcpy (w->buf, buf, len);
  w->len = len;

  pthread_mutex_lock (&priv->queue_mutex);
  w->next = priv->queue;
  priv->queue = w;
  pthread_mutex_unlock (&priv->queue_mutex);

  return len;
}
