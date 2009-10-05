/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef __TRANSPORT_H__
#define __TRANSPORT_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>

struct transport_ops;
typedef struct transport transport_t;

#include "xlator.h"
#include "dict.h"
#include "compat.h"

typedef struct peer_info {
	struct sockaddr_storage sockaddr;
	socklen_t sockaddr_len;
	char identifier[UNIX_PATH_MAX];
}peer_info_t;

struct transport_msg {
        struct list_head  list;
        char             *hdr;
        int               hdrlen;
        struct iobuf     *iobuf;
};

struct transport {
	struct transport_ops  *ops;
	void                  *private;
	void                  *xl_private;
	pthread_mutex_t        lock;
	int32_t                refcount;

	xlator_t              *xl;
	void                  *dnscache;
	data_t                *buf;
	int32_t              (*init)   (transport_t *this);
	void                 (*fini)   (transport_t *this);
	/*  int                  (*notify) (transport_t *this, int event, void *data); */
	peer_info_t     peerinfo;
	peer_info_t     myinfo;

        transport_t    *peer_trans;
        struct {
                pthread_mutex_t       mutex;
                pthread_cond_t        cond;
                pthread_t             thread;
                struct list_head      msgs;
                struct transport_msg *msg;
        } handover;
                
};

struct transport_ops {
	int32_t (*receive)    (transport_t *this, char **hdr_p, size_t *hdrlen_p,
                               struct iobuf **iobuf_p);
	int32_t (*submit)     (transport_t *this, char *buf, int len,
                               struct iovec *vector, int count,
                               struct iobref *iobref);
	int32_t (*connect)    (transport_t *this);
	int32_t (*listen)     (transport_t *this);
	int32_t (*disconnect) (transport_t *this);
};


int32_t transport_listen     (transport_t *this);
int32_t transport_connect    (transport_t *this);
int32_t transport_disconnect (transport_t *this);
int32_t transport_notify     (transport_t *this, int event);
int32_t transport_submit     (transport_t *this, char *buf, int len,
                              struct iovec *vector, int count,
                              struct iobref *iobref);
int32_t transport_receive    (transport_t *this, char **hdr_p, size_t *hdrlen_p,
                              struct iobuf **iobuf_p);
int32_t transport_destroy    (transport_t *this);

transport_t *transport_load  (dict_t *options, xlator_t *xl);
transport_t *transport_ref   (transport_t *trans);
int32_t      transport_unref (transport_t *trans);

int transport_setpeer (transport_t *trans, transport_t *trans_peer);

#endif /* __TRANSPORT_H__ */
