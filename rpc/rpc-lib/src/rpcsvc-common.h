/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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
        gf_boolean_t            root_squash;
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
