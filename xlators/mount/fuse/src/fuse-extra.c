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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif /* _CONFIG_H */

#include "fuse-extra.h"
#include "common-utils.h"
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "common-utils.h"

struct fuse_req;
struct fuse_ll;

struct fuse_req {
    struct fuse_ll *f;
    uint64_t unique;
    int ctr;
    pthread_mutex_t lock;
    struct fuse_ctx ctx;
    struct fuse_chan *ch;
    int interrupted;
    union {
        struct {
            uint64_t unique;
        } i;
        struct {
            fuse_interrupt_func_t func;
            void *data;
        } ni;
    } u;
    struct fuse_req *next;
    struct fuse_req *prev;
};

struct fuse_ll {
    int debug;
    int allow_root;
    struct fuse_lowlevel_ops op;
    int got_init;
    void *userdata;
    uid_t owner;
    struct fuse_conn_info conn;
    struct fuse_req list;
    struct fuse_req interrupts;
    pthread_mutex_t lock;
    int got_destroy;
};

struct fuse_out_header {
  uint32_t   len;
  int32_t    error;
  uint64_t   unique;
};

uint64_t req_callid (fuse_req_t req)
{
  return req->unique;
}

static void destroy_req(fuse_req_t req)
{
    pthread_mutex_destroy (&req->lock);
    FREE (req);
}

static void list_del_req(struct fuse_req *req)
{
    struct fuse_req *prev = req->prev;
    struct fuse_req *next = req->next;
    prev->next = next;
    next->prev = prev;
}

static void
free_req (fuse_req_t req)
{
  int ctr;
  struct fuse_ll *f = req->f;
  
  pthread_mutex_lock(&req->lock);
  req->u.ni.func = NULL;
  req->u.ni.data = NULL;
  pthread_mutex_unlock(&req->lock);

  pthread_mutex_lock(&f->lock);
  list_del_req(req);
  ctr = --req->ctr;
  pthread_mutex_unlock(&f->lock);
  if (!ctr)
    destroy_req(req);
}

int32_t
fuse_reply_vec (fuse_req_t req,
		struct iovec *vector,
		int32_t count)
{
  int32_t error = 0;
  struct fuse_out_header out;
  struct iovec *iov;
  int res;

  iov = alloca ((count + 1) * sizeof (*vector));
  out.unique = req->unique;
  out.error = error;
  iov[0].iov_base = &out;
  iov[0].iov_len = sizeof(struct fuse_out_header);
  memcpy (&iov[1], vector, count * sizeof (*vector));
  count++;
  out.len = iov_length(iov, count);
  res = fuse_chan_send(req->ch, iov, count);
  free_req(req);

  return res;
}
