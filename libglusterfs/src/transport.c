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

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/poll.h>

#include "logging.h"
#include "transport.h"


transport_t *
transport_load (dict_t *options,
		xlator_t *xl,
		int32_t (*notify) (xlator_t *xl,
				   transport_t *trans,
				   int32_t event))
{
  struct transport *trans = calloc (1, sizeof (struct transport));
  data_t *type_data;
  char *name = NULL;
  void *handle = NULL;
  char *type = "ERROR";

  if (!options) {
    free (trans);
    gf_log ("libglusterfs/transport",
	    GF_LOG_ERROR,
	    "transport_load: options is NULL");
    return NULL;
  }

  type_data = dict_get (options, "transport-type"); // transport type, e.g., "tcp"
  if (!xl) {
    free (trans);
    gf_log ("libglusterfs/transport",
	    GF_LOG_ERROR,
	    "transport_load: xl is NULL");
    return NULL;
  }
  trans->xl = xl;

  if (!notify) {
    free (trans);
    gf_log ("libglusterfs/transport",
	    GF_LOG_ERROR,
	    "transport_load: notify is NULL");
    return NULL;
  }
  trans->notify = notify;


  if (type_data) {
    type = data_to_str (type_data);
  } else {
    free (trans);
    gf_log ("libglusterfs/transport",
	    GF_LOG_ERROR,
	    "transport_load: 'option transport-type <value>' missing in specification");
    return NULL;
  }

  gf_log ("libglusterfs/transport",
	  GF_LOG_DEBUG,
	  "transport_load: attempt to load type %s",
	  type);
  asprintf (&name, "%s/%s.so", TRANSPORTDIR, type);
  gf_log ("libglusterfs/transport",
	  GF_LOG_DEBUG,
	  "transport_load: attempt to load file %s",
	  name);

  handle = dlopen (name, RTLD_LAZY);

  if (!handle) {
    gf_log ("libglusterfs/transport",
	    GF_LOG_ERROR,
	    "transport_load: dlopen (%s): %s",
	    name,
	    dlerror ());
    free (name);
    free (trans);
    return NULL;
  };
  free (name);

  if (!(trans->ops = dlsym (handle, "transport_ops"))) {
    gf_log ("libglusterfs/transport",
	    GF_LOG_ERROR,
	    "transport_load: dlsym (transport_ops) on %s",
	    dlerror ());
    free (trans);
    return NULL;
  }

  if (!(trans->init = dlsym (handle, "init"))) {
    gf_log ("libglusterfs/transport",
	    GF_LOG_ERROR,
	    "transport_load: dlsym (init) on %s",
	    dlerror ());
    free (trans);
    return NULL;
  }

  if (!(trans->fini = dlsym (handle, "fini"))) {
    gf_log ("libglusterfs/transport",
	    GF_LOG_ERROR,
	    "transport_load: dlsym (fini) on %s",
	    dlerror ());
    free (trans);
    return NULL;
  }

  if (trans->init (trans, options, notify) != 0) {
    gf_log ("libglusterfs/transport",
	    GF_LOG_ERROR,
	    "transport_load: '%s' initialization failed",
	    type);
    free (trans);
    return NULL;
  }

  return trans;
}

int32_t 
transport_notify (transport_t *this, int32_t event)
{
  return this->notify (this->xl, this, event);
}

int32_t 
transport_submit (transport_t *this, char *buf, int32_t len)
{
  return this->ops->submit (this, buf, len);
}

/*
int32_t
transport_flush (transport_t *this)
{
  return this->ops->flush (this);
}
*/

int32_t
transport_except (transport_t *this)
{
  return this->ops->except (this);
}

int32_t 
transport_disconnect (transport_t *this)
{
  return this->ops->disconnect (this);
}

int32_t 
transport_destroy (transport_t *this)
{
  this->fini (this);
  free (this);

  return 0;
}


int32_t
transport_register (int fd, transport_t *trans)
{
  return epoll_register (fd, trans);
}

int32_t
transport_unregister (int fd)
{
  return epoll_unregister (fd);
}

int32_t
register_transport (transport_t *trans, int fd)
{
  int32_t ret;

  ret = epoll_register (fd, 
			(void *)trans);
#if 0
  ret = poll_register (fd,
		       (void *)trans);
#endif

  return ret;
}

int32_t
transport_poll ()
{
  return epoll_iteration ();
}
