/*
   Copyright (c) 2006, 2007 Z RESEARCH, Inc. <http://www.zresearch.com>
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


#include <stdint.h>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>

#include "glusterfs.h"
#include "logging.h"
#include "xlator.h"
#include "glusterfs.h"
#include "transport.h"
#include "defaults.h"
#include "common-utils.h"
#include "booster.h"


/* TODO:
   - add cond_wait in ib-verbs receive()
*/
static glusterfs_ctx_t ctx;

int32_t
glusterfs_booster_bridge_notify (xlator_t *this, int32_t event,
				 void *data, ...)
{
  switch (event) {
  case GF_EVENT_POLLERR:
    transport_disconnect (data);
    break;
  }
  return 0;
}

glusterfs_ctx_t *
glusterfs_booster_bridge_init ()
{
  ctx.logfile = "/dev/stderr";
  ctx.loglevel = GF_LOG_ERROR;
  ctx.poll_type = SYS_POLL_TYPE_MAX;

  gf_log_init ("/dev/stderr");
  gf_log_set_loglevel (GF_LOG_ERROR);

  return &ctx;
}

void *
glusterfs_booster_bridge_open (glusterfs_ctx_t *ctx, char *options, int size,
			       char *handle)
{
  xlator_t *xl;
  transport_t *trans;
  struct file *filep;
  int ret;

  xl = calloc (1, sizeof (xlator_t));
  xl->name = "booster";
  xl->type = "performance/booster\n";
  xl->next = xl->prev = xl;
  xl->ctx = ctx;
  xl->notify = glusterfs_booster_bridge_notify;

  xl->options = get_new_dict ();
  if (dict_unserialize (options, size, &xl->options) == NULL) {
    gf_log ("booster", GF_LOG_ERROR,
	    "could not unserialize dictionary");
    free (xl);
    return NULL;
  }

  if (dict_get (xl->options, "transport-type") == NULL) {
    gf_log ("booster", GF_LOG_ERROR,
	    "missing option transport-type");
    free (xl);
    return NULL;
  }

  trans = transport_load (xl->options, xl,
			  glusterfs_booster_bridge_notify);

  if (!trans) {
    gf_log ("booster", GF_LOG_ERROR,
	    "disabling booster for this fd");
    free (xl);
    return NULL;
  }

  ret = transport_connect (trans);

  if (ret != 0) {
    gf_log ("booster", GF_LOG_ERROR, "could not connect to translator");
    free (xl);
    free (trans);
    return NULL;
  }

  xl->private = transport_ref (trans);

  filep = calloc (1, sizeof (*filep));
  filep->transport = trans;
  memcpy (&filep->handle, handle, 20);

  return filep;
}


int
glusterfs_booster_bridge_preadv (struct file *filep, struct iovec *vector,
				 int count, off_t offset)
{
  int ret;
  struct glusterfs_booster_protocol_header hdr = {0, };
  transport_t *trans = filep->transport;
  struct iovec hdrvec;

  hdr.op = GF_FOP_READ;
  hdr.offset = offset;
  hdr.size = iov_length (vector, count);
  memcpy (&hdr.handle, filep->handle, 20);

  hdrvec.iov_base = &hdr;
  hdrvec.iov_len = sizeof (hdr);
  ret = trans->ops->writev (trans, &hdrvec, 1);
  if (ret != 0)
    return -1;

  ret = trans->ops->recieve (trans, (char *) &hdr, sizeof (hdr));
  if (ret != 0)
    return -1;

  if (hdr.op_ret <= 0) {
    errno = hdr.op_errno;
    return hdr.op_ret;
  }

  if (hdr.op_ret > iov_length (vector, count)) {
    errno = ERANGE;
    return -1;
  }

  {
    int i = 0;
    int op_ret = 0;
    ssize_t size_tot = hdr.op_ret, size_i;

    for (i=0; i<count && size_tot; i++) {
      size_i = min (size_tot, vector[i].iov_len);
      if (trans->ops->recieve (trans, vector[i].iov_base, size_i) != 0) {
	op_ret = -1;
	break;
      }
      size_tot -= size_i;
      op_ret += size_i;
    }

    return op_ret;
  }
  return 0;
}


int
glusterfs_booster_bridge_pwritev (struct file *filep, struct iovec *vector,
				  int count, off_t offset)
{
  int ret;
  struct glusterfs_booster_protocol_header hdr = {0, };
  transport_t *trans = filep->transport;
  struct iovec *hdrvec = alloca (sizeof (struct iovec) * (count + 1));

  hdr.op = GF_FOP_WRITE;
  hdr.offset = offset;
  hdr.size = iov_length (vector, count);
  memcpy (&hdr.handle, filep->handle, 20);

  hdrvec[0].iov_base = &hdr;
  hdrvec[0].iov_len = sizeof (hdr);
  memcpy (&hdrvec[1], vector, sizeof (struct iovec) * count);
  ret = trans->ops->writev (trans, hdrvec, count + 1);
  gf_log ("booster", GF_LOG_DEBUG,
	  "writev returned %d", ret);

  ret = trans->ops->recieve (trans, (char *) &hdr, sizeof (hdr));
  if (ret != 0)
    return -1;

  errno = hdr.op_errno;
  return hdr.op_ret;
}
