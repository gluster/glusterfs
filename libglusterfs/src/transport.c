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
	struct transport *trans = NULL, *return_trans = NULL;
	char *addr_family = NULL;
	char *name = NULL;
	void *handle = NULL;
	char *type = NULL;
	char str[] = "ERROR";
	int32_t ret = -1;
	int8_t is_tcp = 0, is_unix = 0, is_ibsdp = 0;

	GF_VALIDATE_OR_GOTO("transport", options, fail);
	GF_VALIDATE_OR_GOTO("transport", xl, fail);
  
	trans = CALLOC (1, sizeof (struct transport));
	GF_VALIDATE_OR_GOTO("transport", trans, fail);

	trans->xl = xl;
	type = str;

	/* Backward compatibility */
	ret = dict_get_str (options, "transport-type", &type);
	if (ret < 0) {
		ret = dict_set_str (options, "transport-type", "socket");
		if (ret < 0)
			gf_log ("dict", GF_LOG_DEBUG,
				"setting transport-type failed");			
		ret = dict_get_str (options, "address-family", &addr_family);
		if (ret < 0) {
			ret = dict_set_str (options, "address-family", "inet");
			if (ret < 0)
				gf_log ("dict", GF_LOG_DEBUG,
					"setting address-family failed");
		}

		gf_log ("transport", GF_LOG_WARNING,
			"missing 'option transport-type'. defaulting to \"socket\" (%s)",
			addr_family?addr_family:"inet");
	} else {
		{
			char *tmp = strchr (type, '/');
			if (tmp)
				*tmp = '\0';
		}
		
		is_tcp = strcmp (type, "tcp");
		is_unix = strcmp (type, "unix");
		is_ibsdp = strcmp (type, "ib-sdp");
		if ((is_tcp == 0) ||
		    (is_unix == 0) ||
		    (is_ibsdp == 0)) {
			if (is_tcp == 0)
				ret = dict_set_str (options, "address-family", "inet");
			if (is_unix == 0)
				ret = dict_set_str (options, "address-family", "unix");
			if (is_ibsdp == 0)
				ret = dict_set_str (options, "address-family", "inet-sdp");

			if (ret < 0)
				gf_log ("dict", GF_LOG_DEBUG,
					"setting address-family failed");

			ret = dict_set_str (options, "transport-type", "socket");
			if (ret < 0)
				gf_log ("dict", GF_LOG_DEBUG,
					"setting transport-type failed");
		}
	}

	ret = dict_get_str (options, "transport-type", &type);
	if (ret < 0) {
		FREE (trans);
		gf_log ("transport", GF_LOG_ERROR,
			"'option transport-type <xxxx>' missing in volume '%s'",
			xl->name);
		goto fail;
	}

	asprintf (&name, "%s/%s.so", TRANSPORTDIR, type);
	gf_log ("transport", GF_LOG_DEBUG,
		"attempt to load file %s", name);

	handle = dlopen (name, RTLD_NOW|RTLD_GLOBAL);
	if (handle == NULL) {
		gf_log ("transport", GF_LOG_ERROR, "%s", dlerror ());
		gf_log ("transport", GF_LOG_ERROR,
			"volume '%s': transport-type '%s' is not valid or "
			"not found on this machine", 
			xl->name, type);
		FREE (name);
		FREE (trans);
		goto fail;
	}
	FREE (name);
	
	trans->ops = dlsym (handle, "tops");
	if (trans->ops == NULL) {
		gf_log ("transport", GF_LOG_ERROR,
			"dlsym (transport_ops) on %s", dlerror ());
		FREE (trans);
		goto fail;
	}

	trans->init = dlsym (handle, "init");
	if (trans->init == NULL) {
		gf_log ("transport", GF_LOG_ERROR,
			"dlsym (gf_transport_init) on %s", dlerror ());
		FREE (trans);
		goto fail;
	}

	trans->fini = dlsym (handle, "fini");
	if (trans->fini == NULL) {
		gf_log ("transport", GF_LOG_ERROR,
			"dlsym (gf_transport_fini) on %s", dlerror ());
		FREE (trans);
		goto fail;
	}
	
	ret = trans->init (trans);
	if (ret != 0) {
		gf_log ("transport", GF_LOG_ERROR,
			"'%s' initialization failed", type);
		FREE (trans);
		goto fail;
	}

	pthread_mutex_init (&trans->lock, NULL);
	return_trans = trans;
fail:
	return return_trans;
}


int32_t 
transport_submit (transport_t *this, char *buf, int32_t len,
		  struct iovec *vector, int count, dict_t *refs)
{
	int32_t ret = -1;

	GF_VALIDATE_OR_GOTO("transport", this, fail);
	GF_VALIDATE_OR_GOTO("transport", this->ops, fail);
	
	ret = this->ops->submit (this, buf, len, vector, count, refs);
fail:
	return ret;
}


int32_t 
transport_connect (transport_t *this)
{
	int ret = -1;
	
	GF_VALIDATE_OR_GOTO("transport", this, fail);
  
	ret = this->ops->connect (this);
fail:
	return ret;
}


int32_t
transport_listen (transport_t *this)
{
	int ret = -1;
	
	GF_VALIDATE_OR_GOTO("transport", this, fail);
  
	ret = this->ops->listen (this);
fail:
	return ret;
}


int32_t 
transport_disconnect (transport_t *this)
{
	int32_t ret = -1;
	
	GF_VALIDATE_OR_GOTO("transport", this, fail);
  
	ret = this->ops->disconnect (this);
fail:
	return ret;
}


int32_t 
transport_destroy (transport_t *this)
{
	int32_t ret = -1;

	GF_VALIDATE_OR_GOTO("transport", this, fail);
  
	if (this->fini)
		this->fini (this);

	pthread_mutex_destroy (&this->lock);
	FREE (this);
fail:
	return ret;
}


transport_t *
transport_ref (transport_t *this)
{
	transport_t *return_this = NULL;

	GF_VALIDATE_OR_GOTO("transport", this, fail);
	
	pthread_mutex_lock (&this->lock);
	{
		this->refcount ++;
	}
	pthread_mutex_unlock (&this->lock);
	
	return_this = this;
fail:
	return return_this;
}


int32_t
transport_receive (transport_t *this, char **hdr_p, size_t *hdrlen_p,
		   char **buf_p, size_t *buflen_p)
{
	int32_t ret = -1;

	GF_VALIDATE_OR_GOTO("transport", this, fail);
  
	ret = this->ops->receive (this, hdr_p, hdrlen_p, buf_p, buflen_p);
fail:
	return ret;
}


int32_t
transport_unref (transport_t *this)
{
	int32_t refcount = 0;
	int32_t ret = -1;

	GF_VALIDATE_OR_GOTO("transport", this, fail);
  
	pthread_mutex_lock (&this->lock);
	{
		refcount = --this->refcount;
	}
	pthread_mutex_unlock (&this->lock);

	if (refcount == 0) {
		this->xl->notify (this->xl, GF_EVENT_TRANSPORT_CLEANUP, this);
		transport_destroy (this);
	}
	
	ret = 0;
fail:
	return ret;
}

