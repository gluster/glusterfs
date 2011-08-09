/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _RPCSVC_COMMON_H
#define _RPCSVC_COMMON_H

#include <pthread.h>
#include "list.h"
#include "compat.h"
#include "glusterfs.h"
#include "dict.h"

typedef enum {
        RPCSVC_EVENT_ACCEPT,
        RPCSVC_EVENT_DISCONNECT,
        RPCSVC_EVENT_TRANSPORT_DESTROY,
        RPCSVC_EVENT_LISTENER_DEAD,
} rpcsvc_event_t;


struct rpcsvc_state;

typedef int (*rpcsvc_notify_t) (struct rpcsvc_state *, void *mydata,
                                rpcsvc_event_t, void *data);


/* Contains global state required for all the RPC services.
 */
typedef struct rpcsvc_state {

        /* Contains list of (program, version) handlers.
         * other options.
         */

        pthread_mutex_t         rpclock;

        unsigned int            memfactor;

        /* List of the authentication schemes available. */
        struct list_head        authschemes;

        /* Reference to the options */
        dict_t                  *options;

        /* Allow insecure ports. */
        int                     allow_insecure;
        gf_boolean_t            register_portmap;
        glusterfs_ctx_t         *ctx;

        /* list of connections which will listen for incoming connections */
        struct list_head         listeners;

        /* list of programs registered with rpcsvc */
        struct list_head         programs;

        /* list of notification callbacks */
        struct list_head         notify;
        int                      notify_count;

        void                    *mydata; /* This is xlator */
        rpcsvc_notify_t          notifyfn;
        struct mem_pool         *rxpool;
} rpcsvc_t;


#endif /* #ifndef _RPCSVC_COMMON_H */
