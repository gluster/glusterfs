/*
   Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "tcp.h"
#include "dict.h"
#include "transport.h"
#include "logging.h"
#include "xlator.h"
#include "byte-order.h"
#include "common-utils.h"

#include <fcntl.h>
#include <errno.h>


static int tcp_init (transport_t *this);

/*
 * return value:
 *   0 = success (completed)
 *  -1 = error
 * > 0 = incomplete
 */

static int
__tcp_rwv (transport_t *this, struct iovec *vector, int count,
	   struct iovec **pending_vector, int *pending_count,
	   int write)
{
  tcp_private_t *priv = NULL;
  int sock = -1;
  int ret = -1;
  struct iovec *opvector = vector;
  int opcount = count;
  int moved = 0;

  priv = this->private;
  sock = priv->sock;

  while (opcount)
    {
      if (write)
	{
	  ret = writev (sock, opvector, opcount);

	  if (ret == 0 || (ret == -1 && errno == EAGAIN))
	    {
	      /* done for now */
	      break;
	    }
	  total_bytes_xferd += ret;
	}
      else
	{
	  ret = readv (sock, opvector, opcount);

	  if (ret == -1 && errno == EAGAIN)
	    {
	      /* done for now */
	      break;
	    }
	  total_bytes_rcvd += ret;
	}

      if (ret == 0)
	{
	  gf_log (this->xl->name, GF_LOG_ERROR, "EOF from peer");
	  opcount = -1;
	  errno = ENOTCONN;
	  break;
	}

      if (ret == -1)
	{
	  if (errno == EINTR)
	    continue;

	  gf_log (this->xl->name, GF_LOG_ERROR,
		  "%s failed (%s)", write ? "writev" : "readv",
		  strerror (errno));
	  opcount = -1;
	  break;
	}

      moved = 0;

      while (moved < ret)
	{
	  if ((ret - moved) >= opvector[0].iov_len)
	    {
	      moved += opvector[0].iov_len;
	      opvector++;
	      opcount--;
	    }
	  else
	    {
	      opvector[0].iov_len -= (ret - moved);
	      opvector[0].iov_base += (ret - moved);
	      moved += (ret - moved);
	    }
	  while (opcount && !opvector[0].iov_len)
	    {
	      opvector++;
	      opcount--;
	    }
	}
    }

  if (pending_vector)
    *pending_vector = opvector;

  if (pending_count)
    *pending_count = opcount;

  return opcount;
}


static int
__tcp_readv (transport_t *this, struct iovec *vector, int count,
	     struct iovec **pending_vector, int *pending_count)
{
  int ret = -1;

  ret = __tcp_rwv (this, vector, count, pending_vector, pending_count, 0);

  return ret;
}


static int
__tcp_writev (transport_t *this, struct iovec *vector, int count,
	     struct iovec **pending_vector, int *pending_count)
{
  int ret = -1;

  ret = __tcp_rwv (this, vector, count, pending_vector, pending_count, 1);

  return ret;
}


static int
__tcp_disconnect (transport_t *this)
{
  tcp_private_t *priv = NULL;
  int ret = -1;

  priv = this->private;

  if (priv->sock != -1)
    {
      ret = shutdown (priv->sock, SHUT_RDWR);
      priv->connected = -1;
    }

  return ret;
}


static int
__tcp_bind_port_lt_1024 (int fd)
{
  int ret = -1;
  struct sockaddr_in sin = {0, };
  uint16_t port = 1023;

  sin.sin_family = PF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;

  while (port)
    {
      sin.sin_port = htons (port);

      ret = bind (fd, (struct sockaddr *)&sin, sizeof (sin));

      if (ret == 0)
	break;

      if (ret == -1 && errno == EACCES)
	break;

      port--;
    }

  return ret;
}


static int
__tcp_server_bind (transport_t *this)
{
  data_t *listen_port_data = NULL;
  data_t *listen_host_data = NULL;
  char *listen_host = NULL;
  uint16_t listen_port = GF_DEFAULT_LISTEN_PORT;
  in_addr_t listen_addr = INADDR_ANY;
  struct sockaddr_in sin = {0, };
  dict_t *options = NULL;
  int ret = -1;
  tcp_private_t *priv = NULL;
  int opt = 1;

  options = this->xl->options;
  priv = this->private;

  listen_port_data = dict_get (options, "listen-port");
  listen_host_data = dict_get (options, "listen-host");

  if (listen_port_data)
    {
      listen_port = data_to_uint16 (listen_port_data);

      if (listen_port == (uint16_t) -1)
	listen_port = GF_DEFAULT_LISTEN_PORT;
    }

  if (listen_host_data)
    {
      listen_host = data_to_str (listen_host_data);
      listen_addr = gf_resolve_ip (listen_host, &this->dnscache);

      if (listen_addr == INADDR_NONE)
	listen_addr = INADDR_ANY;
    }

  sin.sin_family = PF_INET;
  sin.sin_addr.s_addr = listen_addr;
  sin.sin_port = htons (listen_port);

  ret = setsockopt (priv->sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt));

  if (ret == -1)
    {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "setsockopt() for SO_REUSEADDR failed (%s)",
	      strerror (errno));
    }

  ret = bind (priv->sock, (struct sockaddr *)&sin, sizeof (sin));

  if (ret == -1)
    {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "binding to port %d failed (%s)",
	      listen_port, strerror (errno));
    }

  return ret;
}


static int
__tcp_nonblock (int fd)
{
  int flags = 0;
  int ret = -1;

  flags = fcntl (fd, F_GETFL);

  if (flags != -1)
    ret = fcntl (fd, F_SETFL, flags | O_NONBLOCK);

  return ret;
}


static int32_t
__tcp_connect_finish (int fd)
{
  int ret = -1;
  int optval = 0;
  socklen_t optlen = sizeof (int);

  ret = getsockopt (fd, SOL_SOCKET, SO_ERROR,
		    (void *)&optval, &optlen);

  if (ret == 0 && optval)
    {
      errno = optval;
      ret = -1;
    }

  return ret;
}


static void
__tcp_reset (transport_t *this)
{
  tcp_private_t *priv = NULL;

  priv = this->private;

  /* TODO: use mem-pool on incoming data */

  if (priv->incoming.hdr_p)
    free (priv->incoming.hdr_p);

  if (priv->incoming.buf_p)
    free (priv->incoming.buf_p);

  memset (&priv->incoming, 0, sizeof (priv->incoming));

  event_unregister (this->xl->ctx->event_pool, priv->sock, priv->idx);
  close (priv->sock);
  priv->sock = -1;
  priv->idx = -1;
  priv->connected = -1;
}


static struct ioq *
__tcp_ioq_new (transport_t *this, char *buf, int len,
	       struct iovec *vector, int count, dict_t *refs)
{
  struct ioq *entry = NULL;
  tcp_private_t *priv = NULL;

  priv = this->private;

  /* TODO: use mem-pool */
  entry = calloc (1, sizeof (*entry));

  assert (count <= (MAX_IOVEC-2));

  entry->header.colonO[0] = ':';
  entry->header.colonO[1] = 'O';
  entry->header.colonO[2] = '\0';
  entry->header.version   = 42;
  entry->header.size1     = hton32 (len);
  entry->header.size2     = hton32 (iov_length (vector, count));

  entry->vector[0].iov_base = &entry->header;
  entry->vector[0].iov_len  = sizeof (entry->header);
  entry->count++;

  entry->vector[1].iov_base = buf;
  entry->vector[1].iov_len  = len;
  entry->count++;

  if (vector && count)
    {
      memcpy (&entry->vector[2], vector, sizeof (*vector) * count);
      entry->count += count;
    }

  entry->pending_vector = entry->vector;
  entry->pending_count  = entry->count;

  if (refs)
    entry->refs = dict_ref (refs);

  entry->buf = buf;

  INIT_LIST_HEAD (&entry->list);

  return entry;
}


static void
__tcp_ioq_entry_free (struct ioq *entry)
{
  list_del_init (&entry->list);
  if (entry->refs)
    dict_unref (entry->refs);

  /* TODO: use mem-pool */
  free (entry->buf);

  /* TODO: use mem-pool */
  free (entry);
}


static void
__tcp_ioq_flush (transport_t *this)
{
  tcp_private_t *priv = NULL;
  struct ioq *entry = NULL;

  priv = this->private;

  while (!list_empty (&priv->ioq))
    {
      entry = priv->ioq_next;
      __tcp_ioq_entry_free (entry);
    }

  return;
}


static int
__tcp_ioq_churn_entry (transport_t *this,
		       struct ioq *entry)
{
  int ret = -1;

  ret = __tcp_writev (this, entry->pending_vector, entry->pending_count,
		      &entry->pending_vector, &entry->pending_count);

  if (ret == 0)
    {
      /* current entry was completely written */
      assert (entry->pending_count == 0);
      __tcp_ioq_entry_free (entry);
    }

  return ret;
}


static int
__tcp_ioq_churn (transport_t *this)
{
  tcp_private_t *priv = NULL;
  int ret = 0;
  struct ioq *entry = NULL;

  priv = this->private;

  while (!list_empty (&priv->ioq))
    {
      /* pick next entry */
      entry = priv->ioq_next;

      ret = __tcp_ioq_churn_entry (this, entry);

      if (ret != 0)
	break;
    }

  if (list_empty (&priv->ioq))
    {
      /* all pending writes done, not interested in POLLOUT */
      priv->idx = event_select_on (this->xl->ctx->event_pool, priv->sock,
				   priv->idx, -1, 0);
    }

  return ret;
}


static int
tcp_event_poll_err (transport_t *this)
{
  tcp_private_t *priv = NULL;
  int ret = -1;

  priv = this->private;

  pthread_mutex_lock (&priv->lock);
  {
    __tcp_ioq_flush (this);
    __tcp_reset (this);
  }
  pthread_mutex_unlock (&priv->lock);

  this->xl->notify (this->xl, GF_EVENT_POLLERR, this);

  return ret;
}


static int
tcp_event_poll_out (transport_t *this)
{
  tcp_private_t *priv = NULL;
  int ret = -1;
  int event = GF_EVENT_POLLOUT;

  priv = this->private;

  pthread_mutex_lock (&priv->lock);
  {
    if (priv->connected == 1)
      {
	ret = __tcp_ioq_churn (this);

	if (ret == -1)
	  {
	    __tcp_disconnect (this);
	    event = GF_EVENT_POLLERR;
	  }
      }
  }
  pthread_mutex_unlock (&priv->lock);

  this->xl->notify (this->xl, event, this);

  return 0;
}


static int
__tcp_proto_validate_header (transport_t *this, struct tcp_header *header,
			     size_t *size1_p, size_t *size2_p)
{
  size_t size1 = 0, size2 = 0;

  if (strcmp (header->colonO, ":O"))
    {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "tcp header signature does not match :O (%x.%x.%x)",
	      header->colonO[0], header->colonO[1], header->colonO[2]);
      return -1;
    }

  if (header->version != 42)
    {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "tcp header version does not match 42 != %d", header->version);
      return -1;
    }

  size1 = ntoh32 (header->size1);
  size2 = ntoh32 (header->size2);

  if (size1 <= 0 || size1 > 1048576)
    {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "tcp header has incorrect size1=%d", size1);
      return -1;
    }

  if (size2 > (1048576 * 4))
    {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "tcp header has incorrect size2=%d", size2);
      return -1;
    }

  if (size1_p)
    *size1_p = size1;

  if (size2_p)
    *size2_p = size2;

  return 0;
}


/* tcp protocol state machine */

static int
tcp_proto_state_machine (transport_t *this)
{
  int ret = -1;
  tcp_private_t *priv = NULL;
  size_t size1 = 0, size2 = 0;
  int previous_state = -1;

  priv = this->private;

  pthread_mutex_lock (&priv->lock);
  {
    while (priv->incoming.state != TCP_PROTO_STATE_COMPLETE)
      {
	/* debug check against infinite loops */
	if (previous_state == priv->incoming.state)
	  {
	    gf_log (this->xl->name, GF_LOG_ERROR,
		    "state did not change! (%d) Forcing break",
		    previous_state);
	    ret = -1;
	    goto unlock;
	  }
	previous_state = priv->incoming.state;

	switch (priv->incoming.state)
	  {

	  case TCP_PROTO_STATE_NADA:

	    priv->incoming.pending_vector = priv->incoming.vector;
	    priv->incoming.pending_vector->iov_base = &priv->incoming.header;
	    priv->incoming.pending_vector->iov_len  =
	      sizeof (struct tcp_header);

	    priv->incoming.state = TCP_PROTO_STATE_HEADER_COMING;
	    break;

	  case TCP_PROTO_STATE_HEADER_COMING:

	    ret = __tcp_readv (this, priv->incoming.pending_vector, 1,
			       &priv->incoming.pending_vector, NULL);
	    if (ret == 0)
	      {
		priv->incoming.state = TCP_PROTO_STATE_HEADER_CAME;
		break;
	      }

	    if (ret == -1)
	      {
		gf_log (this->xl->name, GF_LOG_ERROR,
			"socket read failed (%s) in state %d",
			strerror (errno), TCP_PROTO_STATE_HEADER_COMING);
		goto unlock;
	      }

	    if (ret > 0)
	      {
		gf_log (this->xl->name, GF_LOG_DEBUG,
			"partial header read on NB socket. continue later");
		goto unlock;
	      }
	    break;

	  case TCP_PROTO_STATE_HEADER_CAME:

	    ret = __tcp_proto_validate_header (this, &priv->incoming.header,
					       &size1, &size2);

	    if (ret == -1)
	      {
		gf_log (this->xl->name, GF_LOG_ERROR,
			"tcp header validation failed");
		goto unlock;
	      }

	    priv->incoming.hdrlen = size1;
	    priv->incoming.buflen = size2;

	    /* TODO: use mem-pool */
	    priv->incoming.hdr_p  = malloc (size1);
	    if (size2)
	      priv->incoming.buf_p = malloc (size2);

	    priv->incoming.vector[0].iov_base = priv->incoming.hdr_p;
	    priv->incoming.vector[0].iov_len  = size1;
	    priv->incoming.vector[1].iov_base = priv->incoming.buf_p;
	    priv->incoming.vector[1].iov_len  = size2;
	    priv->incoming.count              = size2 ? 2 : 1;

	    priv->incoming.pending_vector = priv->incoming.vector;
	    priv->incoming.pending_count  = priv->incoming.count;

	    priv->incoming.state = TCP_PROTO_STATE_DATA_COMING;
	    break;

	  case TCP_PROTO_STATE_DATA_COMING:

	    ret = __tcp_readv (this, priv->incoming.pending_vector,
			       priv->incoming.pending_count,
			       &priv->incoming.pending_vector,
			       &priv->incoming.pending_count);
	    if (ret == 0)
	      {
		priv->incoming.state = TCP_PROTO_STATE_DATA_CAME;
		break;
	      }

	    if (ret == -1)
	      {
		gf_log (this->xl->name, GF_LOG_ERROR,
			"socket read failed (%s) in state %d",
			strerror (errno), TCP_PROTO_STATE_DATA_COMING);
		goto unlock;
	      }

	    if (ret > 0)
	      {
		gf_log (this->xl->name, GF_LOG_DEBUG,
			"partial header read on NB socket. continue later");
		goto unlock;
	      }
	    break;

	  case TCP_PROTO_STATE_DATA_CAME:

	    memset (&priv->incoming.vector, 0, sizeof (priv->incoming.vector));
	    priv->incoming.pending_vector = NULL;
	    priv->incoming.pending_count  = 0;
	    priv->incoming.state = TCP_PROTO_STATE_COMPLETE;
	    break;

	  case TCP_PROTO_STATE_COMPLETE:
	    /* not reached */
	    break;

	  default:
	    gf_log (this->xl->name, GF_LOG_ERROR,
		    "undefined state reached: %d", priv->incoming.state);
	    goto unlock;
	  }
      }
  }
 unlock:
  pthread_mutex_unlock (&priv->lock);

  return ret;
}


static int
tcp_event_poll_in (transport_t *this)
{
  int ret = -1;

  ret = tcp_proto_state_machine (this);

  /* call POLLIN on xlator even if complete block is not received,
     just to keep the last_received timestamp ticking */

  if (ret == 0)
    ret = this->xl->notify (this->xl, GF_EVENT_POLLIN, this);

  if (ret == -1)
    transport_disconnect (this);

  return 0;
}


static int
tcp_connect_finish (transport_t *this)
{
  int ret = -1;
  tcp_private_t *priv = NULL;
  int event = -1;
  char notify_xlator = 0;

  priv = this->private;

  pthread_mutex_lock (&priv->lock);
  {
    if (!priv->connected)
      {
	ret = __tcp_connect_finish (priv->sock);

	if (ret == -1 && errno != EINPROGRESS)
	  {
	    gf_log (this->xl->name, GF_LOG_ERROR,
		    "connection failed (%s)", strerror (errno));
	    __tcp_disconnect (this);
	    notify_xlator = 1;
	    event = GF_EVENT_POLLERR;
	    goto unlock;
	  }

	if (ret == 0)
	  {
	    priv->connected = 1;
	    notify_xlator = 1;
	    event = GF_EVENT_CHILD_UP;
	  }
      }
  }
 unlock:
  pthread_mutex_unlock (&priv->lock);

  if (notify_xlator)
    this->xl->notify (this->xl, event, this);

  return 0;
}


static int32_t
tcp_event_handler (int fd, int idx, void *data,
		   int poll_in, int poll_out, int poll_err)
{
  transport_t *this = NULL;
  tcp_private_t *priv = NULL;
  int ret = 0;

  this = data;
  priv = this->private;

  pthread_mutex_lock (&priv->lock);
  {
    priv->idx = idx;
  }
  pthread_mutex_unlock (&priv->lock);

  if (!priv->connected)
    {
      ret = tcp_connect_finish (this);
    }

  if (!ret && poll_err)
    {
      ret = tcp_event_poll_err (this);
    }

  if (!ret && poll_out)
    {
      ret = tcp_event_poll_out (this);
    }

  if (!ret && poll_in)
    {
      ret = tcp_event_poll_in (this);
    }

  if (ret < 0)
    transport_unref (this);

  return 0;
}


static int
tcp_server_event_handler (int fd, int idx, void *data,
			  int poll_in, int poll_out, int poll_err)
{
  transport_t *this = NULL;
  tcp_private_t *priv = NULL;
  int ret = 0;
  int new_sock = -1;
  transport_t *new_trans = NULL;
  struct sockaddr_in new_sin = {0, };
  socklen_t addrlen = sizeof (new_sin);
  tcp_private_t *new_priv = NULL;

  this = data;
  priv = this->private;

  pthread_mutex_lock (&priv->lock);
  {
    priv->idx = idx;

    if (poll_in)
      {
	new_sock = accept (priv->sock, (struct sockaddr *)&new_sin,
			   (socklen_t *)&addrlen);

	if (new_sock == -1)
	  goto unlock;

	ret = __tcp_nonblock (new_sock);

	if (ret == -1)
	  {
	    gf_log (this->xl->name, GF_LOG_ERROR,
		    "could not set socket %d to non blocking mode (%s)",
		    new_sock, strerror (errno));
	    close (new_sock);
	    goto unlock;
	  }

	new_trans = calloc (1, sizeof (*new_trans));
	new_trans->xl = this->xl;
	new_trans->fini = this->fini;
	new_trans->peerinfo.sockaddr = new_sin;
	tcp_init (new_trans);
	new_priv = new_trans->private;

	pthread_mutex_lock (&new_priv->lock);
	{
	  new_priv->sock = new_sock;
	  new_priv->connected = 1;

	  transport_ref (new_trans);
	  new_priv->idx = event_register (this->xl->ctx->event_pool, new_sock,
					  tcp_event_handler, new_trans, 1, 0);
	}
	pthread_mutex_unlock (&new_priv->lock);
      }
  }
 unlock:
  pthread_mutex_unlock (&priv->lock);

  return ret;
}


static int
__tcp_dns_resolve (transport_t *this, struct sockaddr_in *sin)
{
  dict_t *options = this->xl->options;
  data_t *remote_host_data = NULL;
  data_t *remote_port_data = NULL;
  char *remote_host = NULL;
  uint16_t remote_port = 0;
  in_addr_t addr = INADDR_NONE;

  remote_host_data = dict_get (options, "remote-host");
  if (remote_host_data == NULL)
    {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "option remote-host missing in volume %s", this->xl->name);
      return -1;
    }

  remote_host = data_to_str (remote_host_data);
  if (remote_host == NULL)
    {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "option remote-host has data NULL in volume %s", this->xl->name);
      return -1;
    }

  remote_port_data = dict_get (options, "remote-port");
  if (remote_port_data == NULL)
    {
      gf_log (this->xl->name, GF_LOG_DEBUG,
	      "option remote-port missing in volume %s. Defaulting to 6996",
	      this->xl->name);

      remote_port = GF_DEFAULT_LISTEN_PORT;
    }
  else
    {
      remote_port = data_to_uint16 (remote_port_data);
    }

  if (remote_port == (uint16_t)-1)
    {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "option remote-port has invalid port in volume %s",
	      this->xl->name);
      return -1;
    }

  /* TODO: gf_resolve is a blocking call. kick in some
     non blocking dns techniques */
  addr = gf_resolve_ip (remote_host, &this->dnscache);

  if (addr == INADDR_NONE)
    {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "DNS resolution failed on host %s", remote_host);
      return -1;
    }

  memset (sin, 0, sizeof (*sin));

  sin->sin_family = PF_INET;
  sin->sin_addr.s_addr = addr;
  sin->sin_port = htons (remote_port);

  return 0;
}


static int32_t
tcp_disconnect (transport_t *this)
{
  tcp_private_t *priv = NULL;
  int ret = -1;

  priv = this->private;

  pthread_mutex_lock (&priv->lock);
  {
    ret = __tcp_disconnect (this);
  }
  pthread_mutex_unlock (&priv->lock);

  return ret;
}


static int32_t
tcp_connect (transport_t *this)
{
  int ret = -1;
  tcp_private_t *priv = NULL;
  struct sockaddr_in sin = {0, };

  priv = this->private;

  if (!priv)
    {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "connect() called on uninitialized transport");
      goto err;
    }

  pthread_mutex_lock (&priv->lock);
  {
    if (priv->sock != -1)
      {
	gf_log (this->xl->name, GF_LOG_ERROR,
		"connect() called on transport already connected");
	goto unlock;
      }

    priv->sock = socket (PF_INET, SOCK_STREAM, 0);

    if (priv->sock == -1)
      {
	gf_log (this->xl->name, GF_LOG_ERROR,
		"socket creation failed (%s)", strerror (errno));
	goto unlock;
      }

    ret = __tcp_nonblock (priv->sock);

    if (ret == -1)
      {
	gf_log (this->xl->name, GF_LOG_ERROR,
		"could not set socket %d to non blocking mode (%s)",
		priv->sock, strerror (errno));
	close (priv->sock);
	priv->sock = -1;
	goto unlock;
      }

    ret = __tcp_bind_port_lt_1024 (priv->sock);

    if (ret == -1)
      {
	gf_log (this->xl->name, GF_LOG_WARNING,
		"could not bind to port < 1024 (%s)", strerror (errno));
      }

    ret = __tcp_dns_resolve (this, &sin);

    if (ret == -1)
      {
	/* logged inside __tcp_dns_resolve() */
	close (priv->sock);
	priv->sock = -1;
	goto unlock;
      }

    ret = connect (priv->sock, (struct sockaddr *)&sin, sizeof (sin));

    if (ret == -1 && errno != EINPROGRESS)
      {
	gf_log (this->xl->name, GF_LOG_ERROR,
		"connection attempt failed (%s)", strerror (errno));
	close (priv->sock);
	priv->sock = -1;
	goto unlock;
      }

    priv->connected = 0; /* 0 = connecting */
    transport_ref (this);

    priv->idx = event_register (this->xl->ctx->event_pool, priv->sock,
				tcp_event_handler, this, 1, 1);
  }
 unlock:
  pthread_mutex_unlock (&priv->lock);

 err:
  return ret;
}


static int32_t
tcp_listen (transport_t *this)
{
  tcp_private_t *priv = NULL;
  int ret = -1;

  priv = this->private;

  pthread_mutex_lock (&priv->lock);
  {
    if (priv->sock != -1)
      {
	gf_log (this->xl->name, GF_LOG_ERROR,
		"tcp_listen() called on already listening transport");
	goto unlock;
      }

    priv->sock = socket (PF_INET, SOCK_STREAM, 0);

    if (priv->sock == -1)
      {
	gf_log (this->xl->name, GF_LOG_ERROR,
		"socket creation failed (%s)", strerror (errno));
	goto unlock;
      }

    ret = __tcp_nonblock (priv->sock);

    if (ret == -1)
      {
	gf_log (this->xl->name, GF_LOG_ERROR,
		"could not set socket %d to non blocking mode (%s)",
		priv->sock, strerror (errno));
	close (priv->sock);
	priv->sock = -1;
	goto unlock;
      }

    ret = __tcp_server_bind (this);

    if (ret == -1)
      {
	/* logged inside __tcp_server_bind() */
	close (priv->sock);
	priv->sock = -1;
	goto unlock;
      }

    ret = listen (priv->sock, 10);

    if (ret == -1)
      {
	gf_log (this->xl->name, GF_LOG_ERROR,
		"could not set socket %d to listen mode (%s)", priv->sock,
		strerror (errno));
	close (priv->sock);
	priv->sock = -1;
	goto unlock;
      }

    transport_ref (this);

    priv->idx = event_register (this->xl->ctx->event_pool, priv->sock,
				tcp_server_event_handler, this, 1, 0);

    if (priv->idx == -1)
      {
	gf_log (this->xl->name, GF_LOG_ERROR,
		"could not register socket %d with events", priv->sock);
	ret = -1;
	close (priv->sock);
	priv->sock = -1;
	goto unlock;
      }
  }
 unlock:
  pthread_mutex_unlock (&priv->lock);

  return ret;
}


static int
tcp_receive (transport_t *this, char **hdr_p, size_t *hdrlen_p,
	     char **buf_p, size_t *buflen_p)
{
  tcp_private_t *priv = NULL;
  int ret = -1;

  priv = this->private;

  pthread_mutex_lock (&priv->lock);
  {
    if (priv->connected != 1)
      {
	gf_log (this->xl->name, GF_LOG_ERROR,
		"socket not connected to receive");
	goto unlock;
      }

    if (!hdr_p || !hdrlen_p || !buf_p || !buflen_p)
      {
	gf_log (this->xl->name, GF_LOG_ERROR,
		"bad parameters hdr_p=%p hdrlen_p=%p buf_p=%p buflen_p=%p",
		hdr_p, hdrlen_p, buf_p, buflen_p);
	goto unlock;
      }

    if (priv->incoming.state == TCP_PROTO_STATE_COMPLETE)
      {
	*hdr_p    = priv->incoming.hdr_p;
	*hdrlen_p = priv->incoming.hdrlen;
	*buf_p    = priv->incoming.buf_p;
	*buflen_p = priv->incoming.buflen;

	memset (&priv->incoming, 0, sizeof (priv->incoming));
	priv->incoming.state = TCP_PROTO_STATE_NADA;

	ret = 0;
      }
  }
 unlock:
  pthread_mutex_unlock (&priv->lock);

  return ret;
}


/* TODO: implement per transfer limit */
static int
tcp_submit (transport_t *this, char *buf, int len,
	    struct iovec *vector, int count,
	    dict_t *refs)
{
  tcp_private_t *priv = NULL;
  int ret = -1;
  char need_poll_out = 0;
  char need_append = 1;
  struct ioq *entry = NULL;

  priv = this->private;

  pthread_mutex_lock (&priv->lock);
  {
    if (priv->connected != 1)
      {
	gf_log (this->xl->name, GF_LOG_ERROR,
		"transport not connected to submit");
	goto unlock;
      }

    entry = __tcp_ioq_new (this, buf, len, vector, count, refs);

    if (list_empty (&priv->ioq))
      {
	ret = __tcp_ioq_churn_entry (this, entry);

	if (ret == 0)
	  need_append = 0;

	if (ret > 0)
	  need_poll_out = 1;
      }

    if (need_append)
      {
	list_add_tail (&entry->list, &priv->ioq);
	ret = 0;
      }

    if (need_poll_out)
      {
	/* first entry to wait. continue writing on POLLOUT */
	priv->idx = event_select_on (this->xl->ctx->event_pool, priv->sock,
				     priv->idx, -1, 1);
      }
  }
 unlock:
  pthread_mutex_unlock (&priv->lock);

  return ret;
}


struct transport_ops tops = {
  .listen     = tcp_listen,
  .connect    = tcp_connect,
  .disconnect = tcp_disconnect,
  .submit     = tcp_submit,
  .receive    = tcp_receive
};


static int32_t
tcp_init (transport_t *this)
{
  tcp_private_t *priv = NULL;

  this->ops = &tops;

  if (this->private)
    {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "double init attempted");
      return -1;
    }

  priv = calloc (1, sizeof (*priv));
  if (!priv)
    {
      gf_log (this->xl->name, GF_LOG_ERROR,
	      "calloc (1, %d) returned NULL", sizeof (*priv));
      return -1;
    }

  pthread_mutex_init (&priv->lock, NULL);

  priv->sock = -1;
  priv->idx = -1;
  priv->connected = -1;

  INIT_LIST_HEAD (&priv->ioq);

  this->private = priv;

  return 0;
}


void
fini (transport_t *this)
{
  gf_log (this->xl->name, GF_LOG_ERROR,
	  "transport %p destroyed", this);
}


int32_t
init (transport_t *this)
{
  int ret = -1;

  ret = tcp_init (this);

  if (ret == -1)
    {
      gf_log (this->xl->name, GF_LOG_ERROR, "tcp_init() failed");
    }
  
  return ret;
}
