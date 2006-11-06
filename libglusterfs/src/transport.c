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
  char *type = "ERROR";

  if (!options) {
    gf_log ("libglusterfs: transport_new: ",
	    GF_LOG_ERROR,
	    "options is NULL");
    return NULL;
  }
  
  if (!xl) {
    gf_log ("libglusterfs: transport_new: ", GF_LOG_ERROR, "xl is NULL");
    return NULL;
  }
  trans->xl = xl;

  if (!notify) {
    gf_log ("libglusterfs: transport_new: ", GF_LOG_ERROR, "notify is NULL");
    return NULL;
  }
  trans->notify = notify;

  data_t *type_data = dict_get (options, "transport-type"); // transport type, e.g., "tcp"

  if (type_data) {
    type = data_to_str (type_data);
  } else {
    gf_log ("libglusterfs",
	    GF_LOG_ERROR,
	    "transport.c: transport_new: 'option transport-type <value>' missing in specification");
    return NULL;
  }

  char *name = NULL;
  void *handle = NULL;

  gf_log ("libglusterfs",
	  GF_LOG_DEBUG,
	  "transport.c: transport_new: attempt to load type %s",
	  type);
  asprintf (&name, "%s/%s.so", TRANSPORTDIR, type);
  gf_log ("libglusterfs",
	  GF_LOG_DEBUG,
	  "transport.c: transport_new: attempt to load file %s",
	  name);

  handle = dlopen (name, RTLD_LAZY);

  if (!handle) {
    gf_log ("libglusterfs",
	    GF_LOG_ERROR,
	    "transport.c: transport_new: dlopen (%s): %s",
	    name,
	    dlerror ());
    return NULL;
  };

  if (!(trans->ops = dlsym (handle, "transport_ops"))) {
    gf_log ("libglusterfs",
	    GF_LOG_ERROR,
	    "dlsym (transport_ops) on %s",
	    dlerror ());
    return NULL;
  }

  if (!(trans->init = dlsym (handle, "init"))) {
    gf_log ("libglusterfs",
	    GF_LOG_ERROR,
	    "dlsym (init) on %s",
	    dlerror ());
    return NULL;
  }

  if (!(trans->fini = dlsym (handle, "fini"))) {
    gf_log ("libglusterfs",
	    GF_LOG_ERROR,
	    "dlsym (fini) on %s",
	    dlerror ());
    return NULL;
  }

  if (trans->init (trans, options, notify) != 0) {
    gf_log ("libglusterfs",
	    GF_LOG_ERROR,
	    "transport '%s' initialization failed",
	    type);
    return NULL;
  }

  free (name);
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

static int32_t (*user_transport_register)(int fd,
					  int32_t (*fn)(int32_t rfd,
							int32_t event,
							void *data),
					  void *data);


static int32_t
internal_transport_register (int32_t fd,
			     transport_event_notify_t transport_notify,
			     void *data)
{
  return 0;
}

static int32_t
transport_event_handler (int32_t fd,
			 int32_t event,
			 void *data)
{
  int32_t ret = 0;
  transport_t *trans = (transport_t *)data;

  ret = transport_notify (trans, event);
  if (ret || (event & (POLLERR|POLLHUP)))
    /* connected on demand on the next transaction */
    transport_disconnect (trans);

  return ret;
}
			 

int32_t
register_transport (transport_t *trans, int fd)
{
  int32_t ret;

  if (user_transport_register)
    ret = user_transport_register (fd, 
				   transport_event_handler,
				   (void *)trans);
  else
    ret = internal_transport_register (fd,
				       transport_event_handler,
				       (void *)trans);

  return ret;
}

void
set_transport_register_cbk (int32_t (*fn)(int32_t fd,
					  int32_t (*hnd)(int32_t fd, 
							 int32_t event, 
							 void *data),
					  void *data))
{
  user_transport_register = fn;
}
