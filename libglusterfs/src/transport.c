/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/poll.h>

#include <stdint.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "logging.h"
#include "transport.h"
#include "glusterfs.h"

transport_t *
transport_load (dict_t *options,
		xlator_t *xl)
{
  struct transport *trans = NULL;
  data_t *type_data = NULL;
  data_t *addr_family = NULL;
  char *name = NULL;
  void *handle = NULL;
  char *type = NULL;
  char str[] = "ERROR";
  
  if (options == NULL || xl == NULL)
    {
      gf_log ("transport", GF_LOG_ERROR, "invalid argument");
      return NULL;
    }
  
  trans = calloc (1, sizeof (struct transport));
  trans->xl = xl;
  type = str;

  /* Backword compatibility */
  type_data = dict_get (options, "transport-type");
  addr_family = dict_get (options, "address-family");
  if (!type_data)
    {
      dict_set (options, "transport-type", str_to_data ("socket"));
      if (!addr_family)
	dict_set (options, "address-family", str_to_data ("inet/inet6"));
      gf_log ("transport", GF_LOG_DEBUG,
	      "missing 'option transport-type'. defaulting to \"socket\" (%s)",
	      addr_family?addr_family->data:"inet/inet6");
    }
  else
    {
      if ((strncmp (type_data->data, "tcp", 3) == 0) ||
	  (strncmp (type_data->data, "unix", 4) == 0) ||
	  (strncmp (type_data->data, "ib-sdp", 6) == 0))
	{
	  if ((strncmp (type_data->data, "tcp", 3) == 0))
	    dict_set (options, "address-family", str_to_data ("inet/inet6"));
	  if ((strncmp (type_data->data, "unix", 4) == 0))
	    dict_set (options, "address-family", str_to_data ("unix"));
	  if ((strncmp (type_data->data, "ib-sdp", 6) == 0))
	    dict_set (options, "address-family", str_to_data ("ib-sdp"));
	  dict_set (options, "transport-type", str_to_data ("socket"));
	}
    }
  type_data = dict_get (options, "transport-type");
  if (type_data) {
    type = data_to_str (type_data);
  } else {
    FREE (trans);
    gf_log ("transport", GF_LOG_ERROR,
	    "'option transport-type <xxxx>' missing in specification");
    return NULL;
  }
  {
    char *tmp = strchr (type, '/');
    if (tmp)
      *tmp = '\0';
  }

  asprintf (&name, "%s/%s.so", TRANSPORTDIR, type);
  gf_log ("transport", GF_LOG_DEBUG,
	  "attempt to load file %s", name);

  handle = dlopen (name, RTLD_NOW|RTLD_GLOBAL);

  if (!handle) {
    gf_log ("transport", GF_LOG_ERROR,
	    "dlopen (%s): %s", name, dlerror ());
    FREE (name);
    FREE (trans);
    return NULL;
  };
  FREE (name);

  if (!(trans->ops = dlsym (handle, "tops"))) {
    gf_log ("transport", GF_LOG_ERROR,
	    "dlsym (transport_ops) on %s", dlerror ());
    FREE (trans);
    return NULL;
  }

  if (!(trans->init = dlsym (handle, "init"))) {
    gf_log ("transport", GF_LOG_ERROR,
	    "dlsym (gf_transport_init) on %s", dlerror ());
    FREE (trans);
    return NULL;
  }

  if (!(trans->fini = dlsym (handle, "fini"))) {
    gf_log ("transport", GF_LOG_ERROR,
	    "dlsym (gf_transport_fini) on %s", dlerror ());
    FREE (trans);
    return NULL;
  }

  if (trans->init (trans) != 0) {
    gf_log ("transport", GF_LOG_ERROR,
	    "'%s' initialization failed", type);
    FREE (trans);
    return NULL;
  }

  pthread_mutex_init (&trans->lock, NULL);

  return trans;
}


int32_t 
transport_submit (transport_t *this, char *buf, int32_t len,
		  struct iovec *vector, int count, dict_t *refs)
{
  if (this == NULL)
    {
      gf_log ("transport", GF_LOG_ERROR, "invalid argument");
      return 0; /* bala: isn't it an error condition ?! */
    }
  
  if (this->ops == NULL)
    {
      gf_log ("transport", GF_LOG_ERROR, "this->ops is NULL");
      return 0; /* bala: isn't it an error condition ?! */
    }
  
  return this->ops->submit (this, buf, len, vector, count, refs);
}


int32_t 
transport_connect (transport_t *this)
{
  int ret = -1;
  
  if (this == NULL)
    {
      gf_log ("transport", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  ret = this->ops->connect (this);

  return ret;
}


int32_t
transport_listen (transport_t *this)
{
  int ret = -1;

  if (this == NULL)
    {
      gf_log ("transport", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  ret = this->ops->listen (this);

  return ret;
}


int32_t 
transport_disconnect (transport_t *this)
{
  int ret = -1;

  if (this == NULL)
    {
      gf_log ("transport", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  ret = this->ops->disconnect (this);

  return ret;
}


int32_t 
transport_destroy (transport_t *this)
{
  if (this == NULL)
    {
      gf_log ("transport", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  if (this->fini)
    this->fini (this);
  pthread_mutex_destroy (&this->lock);
  FREE (this);

  return 0;
}


transport_t *
transport_ref (transport_t *this)
{
  if (this == NULL)
    {
      gf_log ("transport", GF_LOG_ERROR, "invalid argument");
      return NULL;
    }
  
  pthread_mutex_lock (&this->lock);
  this->refcount ++;
  pthread_mutex_unlock (&this->lock);

  return this;
}


int
transport_receive (transport_t *this, char **hdr_p, size_t *hdrlen_p,
		   char **buf_p, size_t *buflen_p)
{
  int ret = -1;

  if (this == NULL)
    {
      gf_log ("transport", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  ret = this->ops->receive (this, hdr_p, hdrlen_p, buf_p, buflen_p);

  return ret;
}


void
transport_unref (transport_t *this)
{
  int32_t refcount;

  if (this == NULL)
    {
      gf_log ("transport", GF_LOG_ERROR, "invalid argument");
      return;
    }
  
  pthread_mutex_lock (&this->lock);
  refcount = --this->refcount;
  pthread_mutex_unlock (&this->lock);

  if (!refcount) {
    this->xl->notify (this->xl, GF_EVENT_TRANSPORT_CLEANUP, this);
    transport_destroy (this);
  }
}

