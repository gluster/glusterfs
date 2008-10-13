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
#endif
#include <inttypes.h>


#include "glusterfs.h"
#include "client-protocol.h"
#include "compat.h"
#include "dict.h"
#include "protocol.h"
#include "transport.h"
#include "xlator.h"
#include "logging.h"
#include "timer.h"
#include "defaults.h"
#include "compat.h"
#include "compat-errno.h"

#include <sys/resource.h>
#include <inttypes.h>

int protocol_client_cleanup (transport_t *trans);
int protocol_client_interpret (xlator_t *this, transport_t *trans,
                               char *hdr_p, size_t hdrlen,
                               char *buf_p, size_t buflen);

#define CLIENT_PRIVATE(frame) (((transport_t *)(frame->this->private))->xl_private)
#define CLIENT_PRIV(this) (((transport_t *)(this->private))->xl_private)

typedef int32_t (*gf_op_t) (call_frame_t *frame,
                            gf_hdr_common_t *hdr, size_t hdrlen,
                            char *buf, size_t buflen);
static gf_op_t gf_fops[];
static gf_op_t gf_mops[];

static ino_t
this_ino_get (inode_t *inode, xlator_t *this)
{
  ino_t ino = 0;
  data_t *ino_data = NULL;

  if (!inode || !this)
    return 0;

  ino_data = dict_get (inode->ctx, this->name);
  if (ino_data)
    {
      ino = data_to_uint64 (ino_data);
    }
  return ino;
}

static void
this_ino_set (inode_t *inode, xlator_t *this, ino_t ino)
{
  data_t *old_ino_data = NULL;
  data_t *new_ino_data = NULL;
  ino_t old_ino = 0;

  old_ino_data = dict_get (inode->ctx, this->name);
  if (old_ino_data)
    {
      old_ino = data_to_uint64 (old_ino_data);
    }

  if (old_ino != ino)
    {
      new_ino_data = data_from_uint64 (ino);

      dict_set (inode->ctx, this->name, new_ino_data);
    }
}

static int
this_fd_get (fd_t *file, xlator_t *this, uint64_t *remote_fd)
{
  int ret = -1;
  uint64_t fd = 0;
  data_t *fd_data = NULL;

  fd_data = dict_get (file->ctx, this->name);
  if (fd_data)
    {
      fd = data_to_uint64 (fd_data);
      if (remote_fd)
        *remote_fd = fd;
      ret = 0;
    }
  return ret;
}


static void
this_fd_set (fd_t *file, xlator_t *this, uint64_t fd)
{
  data_t *old_fd_data = NULL;
  data_t *new_fd_data = NULL;
  uint64_t old_fd = 0;

  old_fd_data = dict_get (file->ctx, this->name);
  if (old_fd_data)
    {
      old_fd = data_to_uint64 (old_fd_data);
    }

    {
      new_fd_data = data_from_uint64 (fd);

      dict_set (file->ctx, this->name, new_fd_data);
    }
}


/*
 * lookup_frame - lookup call frame corresponding to a given callid
 * @trans: transport object
 * @callid: call id of the frame
 *
 * not for external reference
 */

static call_frame_t *
lookup_frame (transport_t *trans, int64_t callid)
{
  char buf[64];
  call_frame_t *frame = NULL;
  client_proto_priv_t *priv = NULL;

  if (!trans) {
    gf_log ("", GF_LOG_ERROR, "!trans");
    return NULL;
  }

  snprintf (buf, 64, "%"PRId64, callid);
  priv = trans->xl_private;

  pthread_mutex_lock (&priv->lock);
  {
    frame = data_to_bin (dict_get (priv->saved_frames, buf));
    dict_del (priv->saved_frames, buf);
  }
  pthread_mutex_unlock (&priv->lock);

  return frame;
}


static void
call_bail (void *trans)
{
  client_proto_priv_t *priv = NULL;
  struct timeval current;
  int32_t bail_out = 0;

  if (!trans) {
    gf_log ("", GF_LOG_ERROR, "!trans");
    return;
  }

  priv = ((transport_t *)trans)->xl_private;

  gettimeofday (&current, NULL);
  pthread_mutex_lock (&priv->lock);
  {
    /* Chaining to get call-always functionality from call-once timer */
    if (priv->timer) {
      struct timeval timeout = {0,};
      timeout.tv_sec = 10;
      timeout.tv_usec = 0;
      gf_timer_cbk_t timer_cbk = priv->timer->cbk;
      gf_timer_call_cancel (((transport_t *) trans)->xl->ctx, priv->timer);
      priv->timer = gf_timer_call_after (((transport_t *) trans)->xl->ctx,
                                         timeout,
                                         timer_cbk,
                                         trans);
      if (!priv->timer) {
        gf_log (((transport_t *)trans)->xl->name, GF_LOG_DEBUG,
                "Cannot create timer");
      }
    }

    if ((priv->saved_frames->count > 0)
        && (((unsigned long long)priv->last_received.tv_sec + priv->transport_timeout) < current.tv_sec)
        && (((unsigned long long)priv->last_sent.tv_sec + priv->transport_timeout ) < current.tv_sec)) {
      struct tm last_sent_tm, last_received_tm;
      char last_sent[32], last_received[32];

      bail_out = 1;
      localtime_r (&priv->last_sent.tv_sec, &last_sent_tm);
      localtime_r (&priv->last_received.tv_sec, &last_received_tm);
      strftime (last_sent, 32, "%Y-%m-%d %H:%M:%S", &last_sent_tm);
      strftime (last_received, 32, "%Y-%m-%d %H:%M:%S", &last_received_tm);
      gf_log (((transport_t *)trans)->xl->name, GF_LOG_ERROR,
              "activating bail-out. pending frames = %d. last sent = %s. last received = %s. transport-timeout = %d",
              priv->saved_frames->count, last_sent, last_received,
              priv->transport_timeout);
    }
  }
  pthread_mutex_unlock (&priv->lock);

  if (bail_out) {
    gf_log (((transport_t *)trans)->xl->name, GF_LOG_CRITICAL,
          "bailing transport");
    transport_disconnect (trans);
  }
}


void
__protocol_client_frame_save (xlator_t *this, call_frame_t *frame,
                              uint64_t callid)
{
  client_proto_priv_t *priv = NULL;
  transport_t *trans = NULL;
  char callid_str[32];
  struct timeval timeout = {0, };

  trans = this->private;
  priv = trans->xl_private;

  snprintf (callid_str, 32, "%"PRId64, callid);

  dict_set (priv->saved_frames, callid_str, data_from_static_ptr (frame));

  if (!priv->timer)
    {
      timeout.tv_sec  = 10;
      timeout.tv_usec = 0;
      priv->timer     = gf_timer_call_after (trans->xl->ctx, timeout,
                                             call_bail, (void *)trans);
    }
}


int
protocol_client_xfer (call_frame_t *frame,
                      xlator_t *this,
                      int type, int op,
                      gf_hdr_common_t *hdr, size_t hdrlen,
                      struct iovec *vector, int count,
                      dict_t *refs)
{
  transport_t *trans = NULL;
  client_proto_priv_t *priv = NULL;
  uint64_t callid = 0;
  int ret = -1;
  gf_hdr_common_t rsphdr = {0, };

  trans = this->private;
  priv = trans->xl_private;

  pthread_mutex_lock (&priv->lock);
  {
    callid = ++priv->callid;

    hdr->callid = hton64 (callid);
    hdr->op     = hton32 (op);
    hdr->type   = hton32 (type);

    if (frame)
      {
        hdr->req.uid = hton32 (frame->root->uid);
        hdr->req.gid = hton32 (frame->root->gid);
        hdr->req.pid = hton32 (frame->root->pid);

        frame->op    = op;
        frame->type  = type;
      }

    if (!priv->connected)
      transport_connect (trans);

    if (priv->connected || (type == GF_OP_TYPE_MOP_REQUEST && op == GF_MOP_SETVOLUME)) {
      ret = transport_submit (trans, (char *)hdr, hdrlen,
			      vector, count, refs);
    }

    if (ret >= 0 && frame)
      {
	gettimeofday (&priv->last_sent, NULL);
        __protocol_client_frame_save (this, frame, callid);
      }
  }
  pthread_mutex_unlock (&priv->lock);

  if (frame && ret < 0)
    {
      rsphdr.op = op;
      rsphdr.rsp.op_ret   = hton32 (-1);
      rsphdr.rsp.op_errno = hton32 (ENOTCONN);

      if (frame->type == GF_OP_TYPE_FOP_REQUEST)
        {
          rsphdr.type = GF_OP_TYPE_FOP_REPLY;
          gf_fops[op] (frame, &rsphdr, sizeof (rsphdr), NULL, 0);
        }
      else
        {
          rsphdr.type = GF_OP_TYPE_MOP_REPLY;
          gf_mops[op] (frame, &rsphdr, sizeof (rsphdr), NULL, 0);
        }
    }

  return ret;
}



/**
 * client_create - create function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @path: complete path to file
 * @flags: create flags
 * @mode: create mode
 *
 * external reference through client_protocol_xlator->fops->create
 */

int32_t
client_create (call_frame_t *frame,
               xlator_t *this,
               loc_t *loc,
               int32_t flags,
               mode_t mode,
               fd_t *fd)
{
  int ret = -1;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_create_req_t *req = NULL;
  size_t hdrlen = 0;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->flags   = hton32 (flags);
  req->mode    = hton32 (mode);
  strcpy (req->path, loc->path);

  frame->local = fd;

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_CREATE,
                              hdr, hdrlen, NULL, 0, NULL);
  return ret;
}

/**
 * client_open - open function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location of file
 * @flags: open flags
 * @mode: open modes
 *
 * external reference through client_protocol_xlator->fops->open
 */

int32_t
client_open (call_frame_t *frame,
             xlator_t *this,
             loc_t *loc,
             int32_t flags,
             fd_t *fd)
{
  int ret = -1;
  gf_hdr_common_t *hdr = NULL;
  size_t hdrlen = 0;
  gf_fop_open_req_t *req = NULL;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->ino   = hton64 (this_ino_get (loc->inode, this));
  req->flags = hton32 (flags);
  strcpy (req->path, loc->path);

  frame->local = fd;

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_OPEN,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/**
 * client_stat - stat function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 *
 * external reference through client_protocol_xlator->fops->stat
 */

int32_t
client_stat (call_frame_t *frame,
             xlator_t *this,
             loc_t *loc)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_stat_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->ino  = hton64 (this_ino_get (loc->inode, this));
  strcpy (req->path, loc->path);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_STAT,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/**
 * client_readlink - readlink function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 * @size:
 *
 * external reference through client_protocol_xlator->fops->readlink
 */


int32_t
client_readlink (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *loc,
                 size_t size)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_readlink_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->ino  = hton64 (this_ino_get (loc->inode, this));
  req->size = hton32 (size);
  strcpy (req->path, loc->path);


  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_READLINK,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/**
 * client_mknod - mknod function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @path: pathname of node
 * @mode:
 * @dev:
 *
 * external reference through client_protocol_xlator->fops->mknod
 */

int32_t
client_mknod (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              mode_t mode,
              dev_t dev)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_mknod_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->mode = hton32 (mode);
  req->dev  = hton64 (dev);
  strcpy (req->path, loc->path);

  frame->local = loc->inode;

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_MKNOD,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/**
 * client_mkdir - mkdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @path: pathname of directory
 * @mode:
 *
 * external reference through client_protocol_xlator->fops->mkdir
 */


int32_t
client_mkdir (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              mode_t mode)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_mkdir_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->mode = hton32 (mode);
  strcpy (req->path, loc->path);

  frame->local = loc->inode;

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_MKDIR,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}



/**
 * client_unlink - unlink function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location of file
 *
 * external reference through client_protocol_xlator->fops->unlink
 */

int32_t
client_unlink (call_frame_t *frame,
               xlator_t *this,
               loc_t *loc)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_unlink_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->ino  = hton64 (this_ino_get (loc->inode, this));
  strcpy (req->path, loc->path);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_UNLINK,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}

int32_t
client_rmelem (call_frame_t *frame,
               xlator_t *this,
               const char *path)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_rmelem_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (path) + 1);
  hdr    = gf_hdr_new (req, strlen (path) + 1);
  req    = gf_param (hdr);

  strcpy (req->path, path);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_RMELEM,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}

/**
 * client_rmdir - rmdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 *
 * external reference through client_protocol_xlator->fops->rmdir
 */

int32_t
client_rmdir (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_rmdir_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->ino  = hton64 (this_ino_get (loc->inode, this));
  strcpy (req->path, loc->path);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_RMDIR,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}



/**
 * client_symlink - symlink function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @oldpath: pathname of target
 * @newpath: pathname of symlink
 *
 * external reference through client_protocol_xlator->fops->symlink
 */

int32_t
client_symlink (call_frame_t *frame,
                xlator_t *this,
                const char *linkname,
                loc_t *loc)
{
  int ret = -1;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_symlink_req_t *req = NULL;
  size_t hdrlen = 0;
  size_t oldlen = 0;
  size_t newlen = 0;

  frame->local = loc->inode;

  oldlen = strlen (loc->path);
  newlen = strlen (linkname);

  hdrlen = gf_hdr_len (req, oldlen + 1 + newlen + 1);
  hdr    = gf_hdr_new (req, oldlen + 1 + newlen + 1);
  req    = gf_param (hdr);

  strcpy (req->oldpath, loc->path);
  strcpy (req->newpath + oldlen + 1, linkname);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_SYMLINK,
                              hdr, hdrlen, NULL, 0, NULL);
  return ret;
}


/**
 * client_rename - rename function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @oldloc: location of old pathname
 * @newloc: location of new pathname
 *
 * external reference through client_protocol_xlator->fops->rename
 */

int32_t
client_rename (call_frame_t *frame,
               xlator_t *this,
               loc_t *oldloc,
               loc_t *newloc)
{
  int ret = -1;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_rename_req_t *req = NULL;
  size_t hdrlen = 0;
  size_t oldlen = 0;
  size_t newlen = 0;

  oldlen = strlen (oldloc->path);
  newlen = strlen (newloc->path);

  hdrlen = gf_hdr_len (req, oldlen + 1 + newlen + 1);
  hdr    = gf_hdr_new (req, oldlen + 1 + newlen + 1);
  req    = gf_param (hdr);

  req->oldino = hton64 (this_ino_get (oldloc->inode, this));
  if (newloc->ino)
    req->newino = hton64 (this_ino_get (newloc->inode, this));

  strcpy (req->oldpath, oldloc->path);
  strcpy (req->newpath + oldlen + 1, newloc->path);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_RENAME,
                              hdr, hdrlen, NULL, 0, NULL);
  return ret;

}



/**
 * client_link - link function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @oldloc: location of old pathname
 * @newpath: new pathname
 *
 * external reference through client_protocol_xlator->fops->link
 */

int32_t
client_link (call_frame_t *frame,
             xlator_t *this,
             loc_t *oldloc,
             loc_t *newloc)
{
  int ret = -1;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_link_req_t *req = NULL;
  size_t hdrlen = 0;
  size_t oldlen = 0;
  size_t newlen = 0;

  oldlen = strlen (oldloc->path);
  newlen = strlen (newloc->path);

  hdrlen = gf_hdr_len (req, oldlen + 1 + newlen + 1);
  hdr    = gf_hdr_new (req, oldlen + 1 + newlen + 1);
  req    = gf_param (hdr);

  strcpy (req->oldpath, oldloc->path);
  strcpy (req->newpath + oldlen + 1, newloc->path);

  req->oldino = hton64 (this_ino_get (oldloc->inode, this));

  frame->local = oldloc->inode;

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_LINK,
                              hdr, hdrlen, NULL, 0, NULL);
  return ret;
}



/**
 * client_chmod - chmod function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 * @mode:
 *
 * external reference through client_protocol_xlator->fops->chmod
 */

int32_t
client_chmod (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              mode_t mode)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_chmod_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->oldino  = hton64 (this_ino_get (loc->inode, this));
  req->mode    = hton32 (mode);
  strcpy (req->path, loc->path);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_CHMOD,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/**
 * client_chown - chown function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 * @uid: uid of new owner
 * @gid: gid of new owner group
 *
 * external reference through client_protocol_xlator->fops->chown
 */

int32_t
client_chown (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              uid_t uid,
              gid_t gid)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_chown_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->ino = hton64 (this_ino_get (loc->inode, this));
  req->uid = hton32 (uid);
  req->gid = hton32 (gid);
  strcpy (req->path, loc->path);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_CHOWN,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}

/**
 * client_truncate - truncate function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 * @offset:
 *
 * external reference through client_protocol_xlator->fops->truncate
 */

int32_t
client_truncate (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *loc,
                 off_t offset)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_truncate_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->ino    = hton64 (this_ino_get (loc->inode, this));
  req->offset = hton64 (offset);
  strcpy (req->path, loc->path);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_TRUNCATE,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}



/**
 * client_utimes - utimes function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 * @tvp:
 *
 * external reference through client_protocol_xlator->fops->utimes
 */

int32_t
client_utimens (call_frame_t *frame,
                xlator_t *this,
                loc_t *loc,
                struct timespec *tvp)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_utimens_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->ino = hton64 (this_ino_get (loc->inode, this));
  gf_timespec_from_timespec (req->tv, tvp);
  strcpy (req->path, loc->path);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_UTIMENS,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}



/**
 * client_readv - readv function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @size:
 * @offset:
 *
 * external reference through client_protocol_xlator->fops->readv
 */

int32_t
client_readv (call_frame_t *frame,
              xlator_t *this,
              fd_t *fd,
              size_t size,
              off_t offset)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_read_req_t *req = NULL;
  size_t hdrlen = 0;
  uint64_t remote_fd = 0;
  int ret = -1;

  if (this_fd_get (fd, this, &remote_fd) == -1)
    {
      STACK_UNWIND (frame, -1, EBADFD, NULL, 0, NULL);
      return 0;
    }

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);
  req    = gf_param (hdr);

  req->fd     = hton64 (remote_fd);
  req->size   = hton32 (size);
  req->offset = hton64 (offset);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_READ,
                              hdr, hdrlen, NULL, 0, NULL);

  return 0;
}


/**
 * client_writev - writev function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @vector:
 * @count:
 * @offset:
 *
 * external reference through client_protocol_xlator->fops->writev
 */

int32_t
client_writev (call_frame_t *frame,
               xlator_t *this,
               fd_t *fd,
               struct iovec *vector,
               int32_t count,
               off_t offset)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_write_req_t *req = NULL;
  size_t hdrlen = 0;
  uint64_t remote_fd = -1;
  int ret = -1;

  if (this_fd_get (fd, this, &remote_fd) == -1)
    {
      STACK_UNWIND (frame, -1, EBADFD, NULL);
      return -1;
    }

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);
  req    = gf_param (hdr);

  req->fd     = hton64 (remote_fd);
  req->size   = hton32 (iov_length (vector, count));
  req->offset = hton64 (offset);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_WRITE,
                              hdr, hdrlen, vector, count,
                              frame->root->req_refs);

  return ret;
}


/**
 * client_statfs - statfs function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 *
 * external reference through client_protocol_xlator->fops->statfs
 */

int32_t
client_statfs (call_frame_t *frame,
               xlator_t *this,
               loc_t *loc)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_statfs_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->ino = hton64 (this_ino_get (loc->inode, this));
  strcpy (req->path, loc->path);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_STATFS,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/**
 * client_flush - flush function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 *
 * external reference through client_protocol_xlator->fops->flush
 */

int32_t
client_flush (call_frame_t *frame,
              xlator_t *this,
              fd_t *fd)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_flush_req_t *req = NULL;
  size_t hdrlen = 0;
  uint64_t remote_fd = 0;
  int ret = -1;

  if (this_fd_get (fd, this, &remote_fd) == -1)
    {
      STACK_UNWIND (frame, -1, EBADFD);
      return 0;
    }

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);
  req    = gf_param (hdr);

  req->fd = hton64 (remote_fd);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_FLUSH,
                              hdr, hdrlen, NULL, 0, NULL);

  return 0;
}


/**
 * client_close - close function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 *
 * external reference through client_protocol_xlator->fops->close
 *
 * TODO: fd_t is top-down now... no need to do anything destructive. Also need to look into
 *      cleanup().
 */

int32_t
client_close (xlator_t *this,
              fd_t *fd)
{
  call_frame_t *fr = NULL;
  int32_t ret = -1;
  uint64_t remote_fd = 0;
  char key[32] = {0,};
  client_proto_priv_t *priv = NULL;
  gf_hdr_common_t *hdr = NULL;
  size_t hdrlen = 0;
  gf_fop_close_req_t *req = NULL;

  if (this_fd_get (fd, this, &remote_fd) == -1)
    {
      return 0;
    }

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);
  req    = gf_param (hdr);

  req->fd = hton64 (remote_fd);

  {
    priv = CLIENT_PRIV (this);
    sprintf (key, "%p", fd);
    pthread_mutex_lock (&priv->lock);
    {
      dict_del (priv->saved_fds, key);
    }
    pthread_mutex_unlock (&priv->lock);
  }

  fr = create_frame (this, this->ctx->pool);

  ret = protocol_client_xfer (fr, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_CLOSE,
                              hdr, hdrlen, NULL, 0, NULL);
  return ret;
}


/**
 * client_fsync - fsync function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @flags:
 *
 * external reference through client_protocol_xlator->fops->fsync
 */

int32_t
client_fsync (call_frame_t *frame,
              xlator_t *this,
              fd_t *fd,
              int32_t flags)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_fsync_req_t *req = NULL;
  size_t hdrlen = 0;
  uint64_t remote_fd = 0;
  int32_t ret = -1;

  if (this_fd_get (fd, this, &remote_fd) == -1)
    {
      STACK_UNWIND (frame, -1, EBADFD);
      return 0;
    }

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);
  req    = gf_param (hdr);

  req->fd   = hton64 (remote_fd);
  req->data = hton32 (flags);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_FSYNC,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


int32_t
client_incver (call_frame_t *frame,
               xlator_t *this,
               const char *path,
               fd_t *fd)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_incver_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;
  uint64_t remote_fd = 0;

  if (fd && this_fd_get (fd, this, &remote_fd) == -1)
    {
      STACK_UNWIND (frame, -1, EBADFD, NULL);
      return 0;
    }

  hdrlen      = gf_hdr_len (req, strlen (path) + 1);
  hdr         = gf_hdr_new (req, strlen (path) + 1);
  req         = gf_param (hdr);
  req->fd     = hton64 (remote_fd);

  strcpy (req->path, path);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_INCVER,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}

int32_t
client_xattrop (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		const char *path,
		int32_t flags,
		dict_t *dict)
{
  int ret = -1;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_xattrop_req_t *req = NULL;
  size_t hdrlen = 0;
  size_t dict_len = 0;
  uint64_t remote_fd = 0;

  if (dict)
    dict_len = dict_serialized_length (dict);

  hdrlen = gf_hdr_len (req, dict_len + strlen (path) + 1);
  hdr    = gf_hdr_new (req, dict_len + strlen (path) + 1);
  req    = gf_param (hdr);

  if (fd && this_fd_get (fd, this, &remote_fd) == -1) {
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }

  req->flags = hton32 (flags);
  req->dict_len = hton32 (dict_len);
  if (dict)
    dict_serialize (dict, req->dict);
  req->fd     = hton64 (remote_fd);

  /* NOTE: (req->dict + dict_len) will be the memory location which houses loc->path,
   * in the protocol data.
   */
  strcpy (req->dict + dict_len, path);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_XATTROP,
                              hdr, hdrlen, NULL, 0, NULL);
  return ret;
}

int32_t
client_xattrop_cbk (call_frame_t *frame,
                     gf_hdr_common_t *hdr, size_t hdrlen,
                     char *buf, size_t buflen)
{
  gf_fop_xattrop_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;
  int dict_len = 0;
  dict_t *dict = NULL;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret >= 0)
    {
      dict_len = ntoh32 (rsp->dict_len);

      if (dict_len > 0)
        {
          char *dictbuf = memdup (rsp->dict, dict_len);
          dict = get_new_dict();
          dict_unserialize (dictbuf, dict_len, &dict);
          dict->extra_free = dictbuf;
          dict_del (dict, "__@@protocol_client@@__key");
        }
    }

  if (dict)
    dict_ref (dict);

  STACK_UNWIND (frame, op_ret, op_errno, dict);

  if (dict)
    dict_unref (dict);

  return 0;
}

/**
 * client_setxattr - setxattr function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 * @dict: dictionary which contains key:value to be set.
 * @flags:
 *
 * external reference through client_protocol_xlator->fops->setxattr
 */

int32_t
client_setxattr (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *loc,
                 dict_t *dict,
                 int32_t flags)
{
  int ret = -1;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_setxattr_req_t *req = NULL;
  size_t hdrlen = 0;
  size_t dict_len = 0;

  dict_len = dict_serialized_length (dict);

  hdrlen = gf_hdr_len (req, dict_len + strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, dict_len + strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->ino   = hton64 (this_ino_get (loc->inode, this));
  req->flags = hton32 (flags);
  req->dict_len = hton32 (dict_len);
  dict_serialize (dict, req->dict);
  /* NOTE: (req->dict + dict_len) will be the memory location which houses loc->path,
   * in the protocol data.
   */
  strcpy (req->dict + dict_len, loc->path);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_SETXATTR,
                              hdr, hdrlen, NULL, 0, NULL);
  return ret;
}

/**
 * client_getxattr - getxattr function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location structure
 *
 * external reference through client_protocol_xlator->fops->getxattr
 */

int32_t
client_getxattr (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *loc,
                 const char *name)
{
  int ret = -1;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_getxattr_req_t *req = NULL;
  size_t hdrlen = 0;
  size_t name_len = 0;

  if (name)
    name_len = strlen (name);

  hdrlen = gf_hdr_len (req, name_len + 1 + strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, name_len + 1 + strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->ino   = hton64 (this_ino_get (loc->inode, this));
  req->name_len = hton32 (name_len);
  strcpy (req->path, loc->path);
  if (name)
    strcpy (req->name + strlen (loc->path) + 1, name);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_GETXATTR,
                              hdr, hdrlen, NULL, 0, NULL);
  return ret;
}

/**
 * client_removexattr - removexattr function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location structure
 * @name:
 *
 * external reference through client_protocol_xlator->fops->removexattr
 */

int32_t
client_removexattr (call_frame_t *frame,
                    xlator_t *this,
                    loc_t *loc,
                    const char *name)
{
  int ret = -1;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_removexattr_req_t *req = NULL;
  size_t hdrlen = 0;
  size_t name_len = 0;

  name_len = strlen (name);

  hdrlen = gf_hdr_len (req, name_len + 1 + strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, name_len + 1 + strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->ino   = hton64 (this_ino_get (loc->inode, this));
  strcpy (req->path, loc->path);
  strcpy (req->name + strlen (loc->path) + 1, name);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_REMOVEXATTR,
                              hdr, hdrlen, NULL, 0, NULL);
  return ret;
}


/**
 * client_opendir - opendir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location structure
 *
 * external reference through client_protocol_xlator->fops->opendir
 */

int32_t
client_opendir (call_frame_t *frame,
                xlator_t *this,
                loc_t *loc,
                fd_t *fd)
{
  int ret = -1;
  gf_hdr_common_t *hdr = NULL;
  size_t hdrlen = 0;
  gf_fop_opendir_req_t *req = NULL;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->ino = hton64 (this_ino_get (loc->inode, this));
  strcpy (req->path, loc->path);

  frame->local = fd;

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_OPENDIR,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/**
 * client_readdir - readdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 *
 * external reference through client_protocol_xlator->fops->readdir
 */

int32_t
client_getdents (call_frame_t *frame,
                 xlator_t *this,
                 fd_t *fd,
                 size_t size,
                 off_t offset,
                 int32_t flag)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_getdents_req_t *req = NULL;
  size_t hdrlen = 0;
  uint64_t remote_fd = 0;
  int ret = -1;

  if (this_fd_get (fd, this, &remote_fd) == -1)
    {
      STACK_UNWIND (frame, -1, EBADFD, NULL);
      return 0;
    }

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);
  req    = gf_param (hdr);

  req->fd     = hton64 (remote_fd);
  req->size   = hton32 (size);
  req->offset = hton64 (offset);
  req->flags  = hton32 (flag);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_GETDENTS,
                              hdr, hdrlen, NULL, 0, NULL);

  return 0;
}

/**
 * client_readdir - readdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 *
 * external reference through client_protocol_xlator->fops->readdir
 */

int32_t
client_readdir (call_frame_t *frame,
                 xlator_t *this,
                 fd_t *fd,
                 size_t size,
                 off_t offset)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_readdir_req_t *req = NULL;
  size_t hdrlen = 0;
  uint64_t remote_fd = 0;
  int ret = -1;

  if (this_fd_get (fd, this, &remote_fd) == -1)
    {
      STACK_UNWIND (frame, -1, EBADFD, NULL);
      return 0;
    }

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);
  req    = gf_param (hdr);

  req->fd     = hton64 (remote_fd);
  req->size   = hton32 (size);
  req->offset = hton64 (offset);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_READDIR,
                              hdr, hdrlen, NULL, 0, NULL);

  return 0;
}


/**
 * client_closedir - closedir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 *
 * external reference through client_protocol_xlator->fops->closedir
 */

int32_t
client_closedir (xlator_t *this,
                 fd_t *fd)
{
  call_frame_t *fr = NULL;
  int32_t ret = -1;
  uint64_t remote_fd = 0;
  char key[32];
  client_proto_priv_t *priv = NULL;
  gf_hdr_common_t *hdr = NULL;
  size_t hdrlen = 0;
  gf_fop_closedir_req_t *req = NULL;


  if (this_fd_get (fd, this, &remote_fd) == -1)
    {
      return 0;
    }

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);
  req    = gf_param (hdr);

  req->fd = hton64 (remote_fd);

  {
    priv = CLIENT_PRIV (this);
    sprintf (key, "%p", fd);
    pthread_mutex_lock (&priv->lock);
    {
      dict_del (priv->saved_fds, key);
    }
    pthread_mutex_unlock (&priv->lock);
  }

  fr = create_frame (this, this->ctx->pool);

  ret = protocol_client_xfer (fr, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_CLOSEDIR,
                              hdr, hdrlen, NULL, 0, NULL);
  return ret;
}


/**
 * client_fsyncdir - fsyncdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @flags:
 *
 * external reference through client_protocol_xlator->fops->fsyncdir
 */

int32_t
client_fsyncdir (call_frame_t *frame,
                 xlator_t *this,
                 fd_t *fd,
                 int32_t flags)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_fsyncdir_req_t *req = NULL;
  size_t hdrlen = 0;
  uint64_t remote_fd = 0;
  int32_t ret = -1;

  if (this_fd_get (fd, this, &remote_fd) == -1)
    {
      STACK_UNWIND (frame, -1, EBADFD);
      return 0;
    }

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);
  req    = gf_param (hdr);

  req->data = hton32 (flags);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_FSYNCDIR,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/**
 * client_access - access function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location structure
 * @mode:
 *
 * external reference through client_protocol_xlator->fops->access
 */

int32_t
client_access (call_frame_t *frame,
               xlator_t *this,
               loc_t *loc,
               int32_t mask)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_access_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->ino  = hton64 (this_ino_get (loc->inode, this));
  req->mask = hton32 (mask);
  strcpy (req->path, loc->path);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_ACCESS,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/**
 * client_ftrucate - ftruncate function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @offset: offset to truncate to
 *
 * external reference through client_protocol_xlator->fops->ftruncate
 */

int32_t
client_ftruncate (call_frame_t *frame,
                  xlator_t *this,
                  fd_t *fd,
                  off_t offset)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_ftruncate_req_t *req = NULL;
  uint64_t remote_fd = 0;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);
  req    = gf_param (hdr);

  if (this_fd_get (fd, this, &remote_fd) == -1)
    {
      STACK_UNWIND (frame, -1, EBADFD, NULL);
      return 0;
    }

  req->fd     = hton64 (remote_fd);
  req->offset = hton64 (offset);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_FTRUNCATE,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/**
 * client_fstat - fstat function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 *
 * external reference through client_protocol_xlator->fops->fstat
 */

int32_t
client_fstat (call_frame_t *frame,
              xlator_t *this,
              fd_t *fd)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_fstat_req_t *req = NULL;
  uint64_t remote_fd = 0;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);
  req    = gf_param (hdr);

  if (this_fd_get (fd, this, &remote_fd) == -1)
    {
      STACK_UNWIND (frame, -1, EBADFD, NULL);
      return 0;
    }

  req->fd = hton64 (remote_fd);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_FSTAT,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/**
 * client_lk - lk function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 * @cmd: lock command
 * @lock:
 *
 * external reference through client_protocol_xlator->fops->lk
 */

int32_t
client_lk (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd,
           int32_t cmd,
           struct flock *flock)
{
	int ret = -1;
	gf_hdr_common_t *hdr = NULL;
	gf_fop_lk_req_t *req = NULL;
	size_t hdrlen = 0;
	uint64_t remote_fd = 0;
	int32_t gf_cmd = 0;
	int32_t gf_type = 0;

	if (this_fd_get (fd, this, &remote_fd) == -1)
	{
		STACK_UNWIND (frame, -1, EBADFD, NULL);
		return 0;
	}

	if (cmd == F_GETLK || cmd == F_GETLK64)
		gf_cmd = GF_LK_GETLK;
	else if (cmd == F_SETLK || cmd == F_SETLK64)
		gf_cmd = GF_LK_SETLK;
	else if (cmd == F_SETLKW || cmd == F_SETLKW64)
		gf_cmd = GF_LK_SETLKW;
	else
		gf_log (this->name, GF_LOG_ERROR, "Unknown cmd (%d)!", gf_cmd);

	switch (flock->l_type)
	{
	case F_RDLCK: gf_type = GF_LK_F_RDLCK; break;
	case F_WRLCK: gf_type = GF_LK_F_WRLCK; break;
	case F_UNLCK: gf_type = GF_LK_F_UNLCK; break;
	}

	hdrlen = gf_hdr_len (req, 0);
	hdr    = gf_hdr_new (req, 0);
	req    = gf_param (hdr);

	req->fd   = hton64 (remote_fd);
	req->cmd  = hton32 (gf_cmd);
	req->type = hton32 (gf_type);
	gf_flock_from_flock (&req->flock, flock);

	ret = protocol_client_xfer (frame, this,
				    GF_OP_TYPE_FOP_REQUEST,
				    GF_FOP_LK,
				    hdr, hdrlen, NULL, 0, NULL);
	return ret;
}


/**
 * client_gf_file_lk - gf_file_lk function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @inode: inode structure
 * @cmd: lock command
 * @lock: flock struct
 *
 * external reference through client_protocol_xlator->fops->gf_file_lk
 */

int32_t
client_gf_file_lk (call_frame_t *frame,
		   xlator_t *this,
		   loc_t *loc, fd_t *fd,
		   int32_t cmd,
		   struct flock *flock)
{
  int ret = -1;
  gf_hdr_common_t *hdr = NULL;
  gf_fop_gf_file_lk_req_t *req = NULL;
  size_t hdrlen = 0;
  int32_t gf_cmd = 0;
  int32_t gf_type = 0;
  int64_t remote_fd = -1;

  size_t pathlen = 0;

  if (cmd == F_GETLK || cmd == F_GETLK64)
    gf_cmd = GF_LK_GETLK;
  else if (cmd == F_SETLK || cmd == F_SETLK64)
    gf_cmd = GF_LK_SETLK;
  else if (cmd == F_SETLKW || cmd == F_SETLKW64)
    gf_cmd = GF_LK_SETLKW;
  else
    gf_log (this->name, GF_LOG_ERROR, "Unknown cmd (%d)!", gf_cmd);

  switch (flock->l_type)
    {
    case F_RDLCK: gf_type = GF_LK_F_RDLCK; break;
    case F_WRLCK: gf_type = GF_LK_F_WRLCK; break;
    case F_UNLCK: gf_type = GF_LK_F_UNLCK; break;
    }

  if (loc && loc->path)
	  pathlen = strlen (loc->path) + 1;

  hdrlen = gf_hdr_len (req, pathlen);
  hdr    = gf_hdr_new (req, pathlen);
  req    = gf_param (hdr);

  if (fd) {
	  if (this_fd_get (fd, this, &remote_fd) == -1) {
		  STACK_UNWIND (frame, -1, EBADFD);
	  }
  }

  req->fd   = hton64 (remote_fd);

  if (loc && loc->path)
	  strcpy (req->path, loc->path);

  req->ino  = hton64 (this_ino_get (loc->inode, this));

  req->cmd  = hton32 (gf_cmd);
  req->type = hton32 (gf_type);
  gf_flock_from_flock (&req->flock, flock);


  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_GF_FILE_LK,
                              hdr, hdrlen, NULL, 0, NULL);
  return ret;
}

int32_t
client_gf_dir_lk (call_frame_t *frame, xlator_t *this,
		  loc_t *loc, const char *basename,
		  gf_dir_lk_cmd cmd, gf_dir_lk_type type)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_gf_dir_lk_req_t *req = NULL;

  size_t pathlen = 0;
  size_t baselen = 0;

  size_t hdrlen = -1;
  int ret = -1;
  
  pathlen = strlen (loc->path) + 1;
  baselen = strlen (basename) + 1;

  hdrlen = gf_hdr_len (req, pathlen + baselen);
  hdr    = gf_hdr_new (req, pathlen + baselen);
  req    = gf_param (hdr);

  req->ino  = hton64 (this_ino_get (loc->inode, this));
  strcpy (req->path, loc->path);

  strcpy (req->path + pathlen, basename);

  req->cmd  = hton32 (cmd);
  req->type = hton32 (type);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_GF_DIR_LK,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/*
 * client_lookup - lookup function for client protocol
 * @frame: call frame
 * @this:
 * @loc: location
 *
 * not for external reference
 */
int32_t
client_lookup (call_frame_t *frame,
               xlator_t *this,
               loc_t *loc,
               int32_t need_xattr)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_lookup_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->ino   = hton64 (this_ino_get (loc->inode, this));
  req->flags = hton32 (need_xattr);
  strcpy (req->path, loc->path);

  frame->local = loc->inode;

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_LOOKUP,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}

/*
 * client_forget - forget function for client protocol
 * @frame: call frame
 * @this:
 * @inode:
 *
 * not for external reference
 */
int32_t
client_forget (call_frame_t *frame,
               xlator_t *this,
               inode_t *inode)
{
  ino_t ino = 0;
  call_frame_t *fr = NULL;
  gf_hdr_common_t *hdr = NULL;
  size_t hdrlen = 0;
  gf_fop_forget_req_t *req = NULL;
  int ret = -1;

  ino = this_ino_get (inode, this);

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);
  req    = gf_param (hdr);

  req->ino = hton64 (ino);

  fr = create_frame (this, this->ctx->pool);

  ret = protocol_client_xfer (fr, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_FORGET,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/*
 * client_fchmod
 *
 */
int32_t
client_fchmod (call_frame_t *frame,
               xlator_t *this,
               fd_t *fd,
               mode_t mode)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_fchmod_req_t *req = NULL;
  uint64_t remote_fd = 0;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);
  req    = gf_param (hdr);

  if (this_fd_get (fd, this, &remote_fd) == -1)
    {
      STACK_UNWIND (frame, -1, EBADFD, NULL);
      return 0;
    }

  req->fd   = hton64 (remote_fd);
  req->mode = hton32 (mode);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_FCHMOD,
                              hdr, hdrlen, NULL, 0, NULL);

  return 0;
}


/*
 * client_fchown -
 *
 * @frame:
 * @this:
 * @fd:
 * @uid:
 * @gid:
 *
 */
int32_t
client_fchown (call_frame_t *frame,
               xlator_t *this,
               fd_t *fd,
               uid_t uid,
               gid_t gid)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_fchown_req_t *req = NULL;
  uint64_t remote_fd = 0;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);
  req    = gf_param (hdr);

  if (this_fd_get (fd, this, &remote_fd) == -1)
    {
      STACK_UNWIND (frame, -1, EBADFD, NULL);
      return 0;
    }

  req->fd  = hton64 (remote_fd);
  req->uid = hton32 (uid);
  req->gid = hton32 (gid);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_FCHOWN,
                              hdr, hdrlen, NULL, 0, NULL);

  return 0;

}

/**
 * client_setdents -
 */
int32_t
client_setdents (call_frame_t *frame,
                 xlator_t *this,
                 fd_t *fd,
                 int32_t flags,
                 dir_entry_t *entries,
                 int32_t count)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_setdents_req_t *req = NULL;
  uint64_t remote_fd = 0;
  char *buffer = NULL;
  dir_entry_t *trav = NULL;
  uint32_t len = 0;
  char *ptr = NULL;
  int32_t buf_len = 0;
  int32_t ret = -1;
  int32_t vec_count = 0;
  size_t hdrlen = -1;
  struct iovec vector[1];
  
  if (this_fd_get (fd, this, &remote_fd) == -1)
    {
      STACK_UNWIND (frame, -1, EBADFD, NULL);
      return 0;
    }

  if (!entries || !count)
    {
      /* If there is no data to be transmitted, just say invalid argument */
      STACK_UNWIND (frame, -1, EINVAL);
      return 0;
    }

  trav = entries->next;
  while (trav) {
    len += strlen (trav->name);
    len += 1;
    len += strlen (trav->link);
    len += 1;
    len += 256; // max possible for statbuf;
    trav = trav->next;
  }
  buffer = calloc (1, len);
  ERR_ABORT (buffer);

  ptr = buffer;

  trav = entries->next;
  while (trav) {
    int32_t this_len = 0;
    char *tmp_buf = NULL;
    struct stat *stbuf = &trav->buf;
    {
      /* Convert the stat buf to string */
      uint64_t dev = stbuf->st_dev;
      uint64_t ino = stbuf->st_ino;
      uint32_t mode = stbuf->st_mode;
      uint32_t nlink = stbuf->st_nlink;
      uint32_t uid = stbuf->st_uid;
      uint32_t gid = stbuf->st_gid;
      uint64_t rdev = stbuf->st_rdev;
      uint64_t size = stbuf->st_size;
      uint32_t blksize = stbuf->st_blksize;
      uint64_t blocks = stbuf->st_blocks;

      uint32_t atime = stbuf->st_atime;
      uint32_t mtime = stbuf->st_mtime;
      uint32_t ctime = stbuf->st_ctime;

#ifdef HAVE_TV_NSEC
      uint32_t atime_nsec = stbuf->st_atim.tv_nsec;
      uint32_t mtime_nsec = stbuf->st_mtim.tv_nsec;
      uint32_t ctime_nsec = stbuf->st_ctim.tv_nsec;
#else
      uint32_t atime_nsec = 0;
      uint32_t mtime_nsec = 0;
      uint32_t ctime_nsec = 0;
#endif

      asprintf (&tmp_buf,
                GF_STAT_PRINT_FMT_STR,
                dev,
                ino,
                mode,
                nlink,
                uid,
                gid,
                rdev,
                size,
                blksize,
                blocks,
                atime,
                atime_nsec,
                mtime,
                mtime_nsec,
                ctime,
                ctime_nsec);
    }
    this_len = sprintf (ptr, "%s/%s%s\n",
                        trav->name,
                        tmp_buf,
                        trav->link);

    FREE (tmp_buf);
    trav = trav->next;
    ptr += this_len;
  }
  buf_len = strlen (buffer);

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);
  req    = gf_param (hdr);

  req->fd    = hton64 (remote_fd);
  req->flags = hton32 (flags);
  req->count = hton32 (count);

  {
    data_t *buf_data = get_new_data ();
    dict_t *reply_dict = get_new_dict ();
    
    reply_dict->is_locked = 1;
    buf_data->is_locked = 1;
    buf_data->data = buffer;
    buf_data->len = buf_len;
    
    dict_set (reply_dict, NULL, buf_data);
    frame->root->rsp_refs = dict_ref (reply_dict);
    vector[0].iov_base = buffer;
    vector[0].iov_len = buf_len;
    vec_count = 1;
  }
  
  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_SETDENTS,
                              hdr, hdrlen, vector, vec_count, frame->root->rsp_refs);

  return ret;
}

/*
 * MGMT_OPS
 */

/**
 * client_stats - stats function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @flags:
 *
 * external reference through client_protocol_xlator->mops->stats
 */

int32_t
client_stats (call_frame_t *frame,
              xlator_t *this,
              int32_t flags)
{
  gf_hdr_common_t *hdr = NULL;
  gf_mop_stats_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);
  req    = gf_param (hdr);

  req->flags = hton32 (flags);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_MOP_REQUEST, GF_MOP_STATS,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/**
 * client_fsck - fsck (file system check) function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @flags:
 *
 * external reference through client_protocol_xlator->mops->fsck
 */

int32_t
client_fsck (call_frame_t *frame,
             xlator_t *this,
             int32_t flags)
{
  gf_log (this->name, GF_LOG_ERROR, "Function not implemented");
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}


/**
 * client_lock - lock function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @name: pathname of file to lock
 *
 * external reference through client_protocol_xlator->fops->lock
 */

int32_t
client_lock (call_frame_t *frame,
             xlator_t *this,
             const char *name)
{
  gf_hdr_common_t *hdr = NULL;
  gf_mop_lock_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (name) + 1);
  hdr    = gf_hdr_new (req, strlen (name) + 1);
  req    = gf_param (hdr);

  strcpy (req->name, name);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_MOP_REQUEST, GF_MOP_LOCK,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/**
 * client_unlock - management function of client protocol to unlock
 * @frame: call frame
 * @this: this translator structure
 * @name: pathname of the file whose lock has to be released
 *
 * external reference through client_protocol_xlator->mops->unlock
 */

int32_t
client_unlock (call_frame_t *frame,
               xlator_t *this,
               const char *name)
{
  gf_hdr_common_t *hdr = NULL;
  gf_mop_unlock_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (name) + 1);
  hdr    = gf_hdr_new (req, strlen (name) + 1);
  req    = gf_param (hdr);

  strcpy (req->name, name);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_MOP_REQUEST, GF_MOP_UNLOCK,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/**
 * client_listlocks - management function of client protocol to list locks
 * @frame: call frame
 * @this: this translator structure
 * @pattern:
 *
 * external reference through client_protocol_xlator->mops->listlocks
 */

int32_t
client_listlocks (call_frame_t *frame,
                  xlator_t *this,
                  const char *pattern)
{
  gf_hdr_common_t *hdr = NULL;
  gf_mop_listlocks_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (pattern) + 1);
  hdr    = gf_hdr_new (req, strlen (pattern) + 1);
  req    = gf_param (hdr);

  strcpy (req->pattern, pattern);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_MOP_REQUEST, GF_MOP_LISTLOCKS,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}

/* Callbacks */

/*
 * client_chown_cbk -
 *
 * @frame:
 * @args:
 *
 * not for external reference
 */
int32_t
client_fchown_cbk (call_frame_t *frame,
                   gf_hdr_common_t *hdr, size_t hdrlen,
                   char *buf, size_t buflen)
{
  struct stat stbuf = {0, };
  gf_fop_fchown_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret == 0)
    {
      gf_stat_to_stat (&rsp->stat, &stbuf);
    }

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


/*
 * client_fchmod_cbk
 *
 * @frame:
 * @args:
 *
 * not for external reference
 */
int32_t
client_fchmod_cbk (call_frame_t *frame,
                   gf_hdr_common_t *hdr, size_t hdrlen,
                   char *buf, size_t buflen)
{
  struct stat stbuf = {0, };
  gf_fop_fchmod_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret == 0)
    {
      gf_stat_to_stat (&rsp->stat, &stbuf);
    }

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


/*
 * client_create_cbk - create callback function for client protocol
 * @frame: call frame
 * @args: arguments in dictionary
 *
 * not for external reference
 */

int32_t
client_create_cbk (call_frame_t *frame,
                   gf_hdr_common_t *hdr, size_t hdrlen,
                   char *buf, size_t buflen)
{
  gf_fop_create_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;
  fd_t *fd = NULL;
  inode_t *inode = NULL;
  struct stat stbuf = {0, };
  uint64_t remote_fd = 0;
  client_proto_priv_t *priv = NULL;
  char key[32];

  fd = frame->local;
  frame->local = NULL;
  inode = fd->inode;

  rsp = gf_param (hdr);

  op_ret    = ntoh32 (hdr->rsp.op_ret);
  op_errno  = ntoh32 (hdr->rsp.op_errno);

  if (op_ret >= 0)
    {
      remote_fd = ntoh64 (rsp->fd);
      gf_stat_to_stat (&rsp->stat, &stbuf);
    }

  if (op_ret >= 0)
    {
      priv = CLIENT_PRIVATE (frame);

      this_ino_set (inode, frame->this, stbuf.st_ino);
      this_fd_set (fd, frame->this, remote_fd);

      sprintf (key, "%p", fd);
      dict_set (priv->saved_fds, key, str_to_data (""));
    }

  STACK_UNWIND (frame, op_ret, op_errno, fd, inode, &stbuf);

  return 0;
}


/*
 * client_open_cbk - open callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_open_cbk (call_frame_t *frame,
                 gf_hdr_common_t *hdr, size_t hdrlen,
                 char *buf, size_t buflen)
{
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  fd_t *fd = NULL;
  uint64_t remote_fd = 0;
  gf_fop_open_rsp_t *rsp = NULL;
  client_proto_priv_t *priv = NULL;
  char key[32];

  fd = frame->local;
  frame->local = NULL;

  rsp = gf_param (hdr);

  op_ret    = ntoh32 (hdr->rsp.op_ret);
  op_errno  = ntoh32 (hdr->rsp.op_errno);

  if (op_ret >= 0)
    {
      remote_fd = ntoh64 (rsp->fd);
    }

  if (op_ret >= 0)
    {
      priv = CLIENT_PRIVATE(frame);

      this_fd_set (fd, frame->this, remote_fd);

      sprintf (key, "%p", fd);

      pthread_mutex_lock (&priv->lock);
      {
        dict_set (priv->saved_fds, key, str_to_data (""));
      }
      pthread_mutex_unlock (&priv->lock);

    }

  STACK_UNWIND (frame, op_ret, op_errno, fd);

  return 0;
}

/*
 * client_stat_cbk - stat callback for client protocol
 * @frame: call frame
 * @args: arguments dictionary
 *
 * not for external reference
 */
int32_t
client_stat_cbk (call_frame_t *frame,
                 gf_hdr_common_t *hdr, size_t hdrlen,
                 char *buf, size_t buflen)
{
  struct stat stbuf = {0, };
  gf_fop_stat_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret == 0)
    {
      gf_stat_to_stat (&rsp->stat, &stbuf);
    }

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}

/*
 * client_utimens_cbk - utimens callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t
client_utimens_cbk (call_frame_t *frame,
                    gf_hdr_common_t *hdr, size_t hdrlen,
                    char *buf, size_t buflen)
{
  struct stat stbuf = {0, };
  gf_fop_utimens_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret == 0)
    {
      gf_stat_to_stat (&rsp->stat, &stbuf);
    }

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}

/*
 * client_chmod_cbk - chmod for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_chmod_cbk (call_frame_t *frame,
                  gf_hdr_common_t *hdr, size_t hdrlen,
                  char *buf, size_t buflen)
{
  struct stat stbuf = {0, };
  gf_fop_chmod_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret == 0)
    {
      gf_stat_to_stat (&rsp->stat, &stbuf);
    }

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}

/*
 * client_chown_cbk - chown for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_chown_cbk (call_frame_t *frame,
                  gf_hdr_common_t *hdr, size_t hdrlen,
                  char *buf, size_t buflen)
{
  struct stat stbuf = {0, };
  gf_fop_chown_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret == 0)
    {
      gf_stat_to_stat (&rsp->stat, &stbuf);
    }

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}

/*
 * client_mknod_cbk - mknod callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_mknod_cbk (call_frame_t *frame,
                  gf_hdr_common_t *hdr, size_t hdrlen,
                  char *buf, size_t buflen)
{
  gf_fop_mknod_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;
  struct stat stbuf = {0, };
  inode_t *inode = NULL;

  inode = frame->local;
  frame->local = NULL;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret >= 0)
    {
      gf_stat_to_stat (&rsp->stat, &stbuf);
      this_ino_set (inode, frame->this, stbuf.st_ino);
    }

  STACK_UNWIND (frame, op_ret, op_errno, inode, &stbuf);

  return 0;
}

/*
 * client_symlink_cbk - symlink callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_symlink_cbk (call_frame_t *frame,
                    gf_hdr_common_t *hdr, size_t hdrlen,
                    char *buf, size_t buflen)
{
  gf_fop_symlink_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;
  struct stat stbuf = {0, };
  inode_t *inode = NULL;

  inode = frame->local;
  frame->local = NULL;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret >= 0)
    {
      gf_stat_to_stat (&rsp->stat, &stbuf);
      this_ino_set (inode, frame->this, stbuf.st_ino);
    }

  STACK_UNWIND (frame, op_ret, op_errno, inode, &stbuf);

  return 0;
}

/*
 * client_link_cbk - link callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_link_cbk (call_frame_t *frame,
                 gf_hdr_common_t *hdr, size_t hdrlen,
                 char *buf, size_t buflen)
{
  gf_fop_link_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;
  struct stat stbuf = {0, };
  inode_t *inode = NULL;

  inode = frame->local;
  frame->local = NULL;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret >= 0)
    {
      gf_stat_to_stat (&rsp->stat, &stbuf);
    }

  STACK_UNWIND (frame, op_ret, op_errno, inode, &stbuf);

  return 0;
}

/*
 * client_truncate_cbk - truncate callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t
client_truncate_cbk (call_frame_t *frame,
                     gf_hdr_common_t *hdr, size_t hdrlen,
                     char *buf, size_t buflen)
{
  struct stat stbuf = {0, };
  gf_fop_truncate_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret == 0)
    {
      gf_stat_to_stat (&rsp->stat, &stbuf);
    }

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}

/* client_fstat_cbk - fstat callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t
client_fstat_cbk (call_frame_t *frame,
                  gf_hdr_common_t *hdr, size_t hdrlen,
                  char *buf, size_t buflen)
{
  struct stat stbuf = {0, };
  gf_fop_fstat_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret == 0)
    {
      gf_stat_to_stat (&rsp->stat, &stbuf);
    }

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}

/*
 * client_ftruncate_cbk - ftruncate callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_ftruncate_cbk (call_frame_t *frame,
                      gf_hdr_common_t *hdr, size_t hdrlen,
                      char *buf, size_t buflen)
{
  struct stat stbuf = {0, };
  gf_fop_ftruncate_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret == 0)
    {
      gf_stat_to_stat (&rsp->stat, &stbuf);
    }

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}

/* client_readv_cbk - readv callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external referece
 */

int32_t
client_readv_cbk (call_frame_t *frame,
                  gf_hdr_common_t *hdr, size_t hdrlen,
                  char *buf, size_t buflen)
{
  gf_fop_read_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;
  struct iovec vector = {0, };
  struct stat stbuf = {0, };
  dict_t *refs = NULL;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret != -1)
    {
      gf_stat_to_stat (&rsp->stat, &stbuf);
      vector.iov_base = buf;
      vector.iov_len  = buflen;

      refs = get_new_dict ();
      dict_set (refs, NULL, data_from_dynptr (buf, 0));
      frame->root->rsp_refs = dict_ref (refs);
    }

  STACK_UNWIND (frame, op_ret, op_errno, &vector, 1, &stbuf);

  if (refs)
    dict_unref (refs);

  return 0;
}

/*
 * client_write_cbk - write callback for client protocol
 * @frame: cal frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t
client_write_cbk (call_frame_t *frame,
                  gf_hdr_common_t *hdr, size_t hdrlen,
                  char *buf, size_t buflen)
{
  gf_fop_write_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;
  struct stat stbuf = {0, };

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret >= 0)
    gf_stat_to_stat (&rsp->stat, &stbuf);

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


int32_t
client_readdir_cbk (call_frame_t *frame,
                    gf_hdr_common_t *hdr, size_t hdrlen,
                    char *buf, size_t buflen)
{
  gf_fop_readdir_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;
  uint32_t buf_size = 0;
  gf_dirent_t entries;

  rsp = gf_param (hdr);

  op_ret    = ntoh32 (hdr->rsp.op_ret);
  op_errno  = ntoh32 (hdr->rsp.op_errno);

  INIT_LIST_HEAD (&entries.list);
  if (op_ret > 0) {
    buf_size = ntoh32 (rsp->size);
    gf_dirent_unserialize (&entries, rsp->buf, buf_size);
  }

  STACK_UNWIND (frame, op_ret, op_errno, &entries);

  gf_dirent_free (&entries);

  return 0;
}

/*
 * client_fsync_cbk - fsync callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_fsync_cbk (call_frame_t *frame,
                  gf_hdr_common_t *hdr, size_t hdrlen,
                  char *buf, size_t buflen)
{
  struct stat stbuf = {0, };
  gf_fop_fsync_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


/*
 * client_unlink_cbk - unlink callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_unlink_cbk (call_frame_t *frame,
                   gf_hdr_common_t *hdr, size_t hdrlen,
                   char *buf, size_t buflen)
{
  gf_fop_unlink_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

int32_t
client_rmelem_cbk (call_frame_t *frame,
                   gf_hdr_common_t *hdr, size_t hdrlen,
                   char *buf, size_t buflen)
{
  gf_fop_rmelem_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}


/*
 * client_rename_cbk - rename callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_rename_cbk (call_frame_t *frame,
                   gf_hdr_common_t *hdr, size_t hdrlen,
                   char *buf, size_t buflen)
{
  struct stat stbuf = {0, };
  gf_fop_rename_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret == 0)
    {
      gf_stat_to_stat (&rsp->stat, &stbuf);
    }

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


/*
 * client_readlink_cbk - readlink callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_readlink_cbk (call_frame_t *frame,
                     gf_hdr_common_t *hdr, size_t hdrlen,
                     char *buf, size_t buflen)
{
  gf_fop_readlink_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;
  char *link = NULL;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret > 0)
    {
      link = rsp->path;
    }

  STACK_UNWIND (frame, op_ret, op_errno, link);
  return 0;
}

/*
 * client_mkdir_cbk - mkdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_mkdir_cbk (call_frame_t *frame,
                  gf_hdr_common_t *hdr, size_t hdrlen,
                  char *buf, size_t buflen)
{
  gf_fop_mkdir_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;
  struct stat stbuf = {0, };
  inode_t *inode = NULL;

  inode = frame->local;
  frame->local = NULL;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret >= 0)
    {
      gf_stat_to_stat (&rsp->stat, &stbuf);
      this_ino_set (inode, frame->this, stbuf.st_ino);
    }

  STACK_UNWIND (frame, op_ret, op_errno, inode, &stbuf);

  return 0;
}

/*
 * client_flush_cbk - flush callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t
client_flush_cbk (call_frame_t *frame,
                  gf_hdr_common_t *hdr, size_t hdrlen,
                  char *buf, size_t buflen)
{
  int op_ret = 0;
  int op_errno = 0;

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

/*
 * client_close_cbk - close callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_close_cbk (call_frame_t *frame,
                  gf_hdr_common_t *hdr, size_t hdrlen,
                  char *buf, size_t buflen)
{
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * client_opendir_cbk - opendir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_opendir_cbk (call_frame_t *frame,
                    gf_hdr_common_t *hdr, size_t hdrlen,
                    char *buf, size_t buflen)
{
  int32_t op_ret = -1;
  int32_t op_errno = ENOTCONN;
  fd_t *fd = NULL;
  uint64_t remote_fd = 0;
  gf_fop_opendir_rsp_t *rsp = NULL;
  client_proto_priv_t *priv = NULL;
  char key[32];

  fd = frame->local;
  frame->local = NULL;

  rsp = gf_param (hdr);

  op_ret    = ntoh32 (hdr->rsp.op_ret);
  op_errno  = ntoh32 (hdr->rsp.op_errno);

  if (op_ret >= 0)
    {
      remote_fd = ntoh64 (rsp->fd);
    }

  if (op_ret >= 0)
    {
      priv = CLIENT_PRIVATE(frame);

      this_fd_set (fd, frame->this, remote_fd);

      sprintf (key, "%p", fd);

      pthread_mutex_lock (&priv->lock);
      {
        dict_set (priv->saved_fds, key, str_to_data (""));
      }
      pthread_mutex_unlock (&priv->lock);

    }

  STACK_UNWIND (frame, op_ret, op_errno, fd);

  return 0;
}

/*
 * client_closedir_cbk - closedir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_closedir_cbk (call_frame_t *frame,
                     gf_hdr_common_t *hdr, size_t hdrlen,
                     char *buf, size_t buflen)
{
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * client_rmdir_cbk - rmdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t
client_rmdir_cbk (call_frame_t *frame,
                  gf_hdr_common_t *hdr, size_t hdrlen,
                  char *buf, size_t buflen)
{
  gf_fop_rmdir_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

/*
 * client_access_cbk - access callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_access_cbk (call_frame_t *frame,
                   gf_hdr_common_t *hdr, size_t hdrlen,
                   char *buf, size_t buflen)
{
  gf_fop_access_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

int32_t
client_incver_cbk (call_frame_t *frame,
                   gf_hdr_common_t *hdr, size_t hdrlen,
                   char *buf, size_t buflen)
{
  gf_fop_incver_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}


int
client_forget_cbk (call_frame_t *frame,
                   gf_hdr_common_t *hdr, size_t hdrlen,
                   char *buf, size_t buflen)
{
  STACK_DESTROY (frame->root);
  return 0;
}

/*
 * client_lookup_cbk - lookup callback for client protocol
 *
 * @frame: call frame
 * @args: arguments dictionary
 *
 * not for external reference
 */
int32_t
client_lookup_cbk (call_frame_t *frame,
                   gf_hdr_common_t *hdr, size_t hdrlen,
                   char *buf, size_t buflen)
{
  struct stat stbuf = {0, };
  inode_t *inode = NULL;
  dict_t *xattr = NULL;
  gf_fop_lookup_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;
  size_t dict_len = 0;

  inode = frame->local;
  frame->local = NULL;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret == 0)
    {
      gf_stat_to_stat (&rsp->stat, &stbuf);
      this_ino_set (inode, frame->this, stbuf.st_ino);

      dict_len = ntoh32 (rsp->dict_len);

      if (dict_len > 0)
        {
          char *dictbuf = memdup (rsp->dict, dict_len);
          xattr = get_new_dict();
          dict_unserialize (dictbuf, dict_len, &xattr);
          xattr->extra_free = dictbuf;
        }
    }

  if (xattr)
    dict_ref (xattr);

  STACK_UNWIND (frame, op_ret, op_errno, inode, &stbuf, xattr);

  if (xattr)
    dict_unref (xattr);

  return 0;
}


/*
 * client_getdents_cbk - readdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_getdents_cbk (call_frame_t *frame,
                     gf_hdr_common_t *hdr, size_t hdrlen,
                     char *buf, size_t buflen)
{
  gf_fop_getdents_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;
  int32_t nr_count = 0;
  dir_entry_t *entry = NULL;
  dir_entry_t *trav = NULL, *prev = NULL;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  entry = calloc (1, sizeof (dir_entry_t));
  ERR_ABORT (entry);

  if (op_ret >= 0)
    {
      int32_t count, i, bread;
      char *ender = NULL, *buffer_ptr = NULL;
      char tmp_buf[512] = {0,};

      nr_count = ntoh32 (rsp->count);
      buffer_ptr = buf;
      prev = entry;

      for (i = 0; i < nr_count ; i++) {
        bread = 0;
        trav = calloc (1, sizeof (dir_entry_t));
        ERR_ABORT (trav);

        ender = strchr (buffer_ptr, '/');
        if (!ender)
          break;
        count = ender - buffer_ptr;
        trav->name = calloc (1, count + 2);
        ERR_ABORT (trav->name);

        strncpy (trav->name, buffer_ptr, count);
        bread = count + 1;
        buffer_ptr += bread;

        ender = strchr (buffer_ptr, '\n');
        if (!ender)
          break;
        count = ender - buffer_ptr;
        strncpy (tmp_buf, buffer_ptr, count);
        bread = count + 1;
        buffer_ptr += bread;

        {
          uint64_t dev;
          uint64_t ino;
          uint32_t mode;
          uint32_t nlink;
          uint32_t uid;
          uint32_t gid;
          uint64_t rdev;
          uint64_t size;
          uint32_t blksize;
          uint64_t blocks;
          uint32_t atime;
          uint32_t atime_nsec;
          uint32_t mtime;
          uint32_t mtime_nsec;
          uint32_t ctime;
          uint32_t ctime_nsec;

          sscanf (tmp_buf, GF_STAT_PRINT_FMT_STR,
                  &dev,
                  &ino,
                  &mode,
                  &nlink,
                  &uid,
                  &gid,
                  &rdev,
                  &size,
                  &blksize,
                  &blocks,
                  &atime,
                  &atime_nsec,
                  &mtime,
                  &mtime_nsec,
                  &ctime,
                  &ctime_nsec);

          trav->buf.st_dev = dev;
          trav->buf.st_ino = ino;
          trav->buf.st_mode = mode;
          trav->buf.st_nlink = nlink;
          trav->buf.st_uid = uid;
          trav->buf.st_gid = gid;
          trav->buf.st_rdev = rdev;
          trav->buf.st_size = size;
          trav->buf.st_blksize = blksize;
          trav->buf.st_blocks = blocks;

          trav->buf.st_atime = atime;
          trav->buf.st_mtime = mtime;
          trav->buf.st_ctime = ctime;

#ifdef HAVE_TV_NSEC
          trav->buf.st_atim.tv_nsec = atime_nsec;
          trav->buf.st_mtim.tv_nsec = mtime_nsec;
          trav->buf.st_ctim.tv_nsec = ctime_nsec;
#endif

        }

        ender = strchr (buffer_ptr, '\n');
        if (!ender)
          break;
        count = ender - buffer_ptr;
        *ender = '\0';
        if (S_ISLNK (trav->buf.st_mode))
          trav->link = strdup (buffer_ptr);
        else
          trav->link = "";

        bread = count + 1;
        buffer_ptr += bread;

        prev->next = trav;
        prev = trav;
      }
    }

  STACK_UNWIND (frame, op_ret, op_errno, entry, nr_count);

  if (op_ret >= 0)
    {
      prev = entry;
      if (!entry)
        return 0;
      trav = entry->next;
      while (trav) {
        prev->next = trav->next;
        FREE (trav->name);
        if (S_ISLNK (trav->buf.st_mode))
          FREE (trav->link);
        FREE (trav);
        trav = prev->next;
      }
      FREE (entry);
      
      /* Free the buffer */
      FREE (buf);
    }


  return 0;
}

/*
 * client_statfs_cbk - statfs callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_statfs_cbk (call_frame_t *frame,
                   gf_hdr_common_t *hdr, size_t hdrlen,
                   char *buf, size_t buflen)
{
  struct statvfs stbuf = {0, };
  gf_fop_statfs_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret == 0)
    {
      gf_statfs_to_statfs (&rsp->statfs, &stbuf);
    }

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}

/*
 * client_fsyncdir_cbk - fsyncdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_fsyncdir_cbk (call_frame_t *frame,
                     gf_hdr_common_t *hdr, size_t hdrlen,
                     char *buf, size_t buflen)
{
  int op_ret = 0;
  int op_errno = 0;

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

/*
 * client_setxattr_cbk - setxattr callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_setxattr_cbk (call_frame_t *frame,
                     gf_hdr_common_t *hdr, size_t hdrlen,
                     char *buf, size_t buflen)
{
  gf_fop_setxattr_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

/*
 * client_getxattr_cbk - getxattr callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_getxattr_cbk (call_frame_t *frame,
                     gf_hdr_common_t *hdr, size_t hdrlen,
                     char *buf, size_t buflen)
{
  gf_fop_getxattr_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;
  int dict_len = 0;
  dict_t *dict = NULL;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret >= 0)
    {
      dict_len = ntoh32 (rsp->dict_len);

      if (dict_len > 0)
        {
          char *dictbuf = memdup (rsp->dict, dict_len);
          dict = get_new_dict();
          dict_unserialize (dictbuf, dict_len, &dict);
          dict->extra_free = dictbuf;
          dict_del (dict, "__@@protocol_client@@__key");
        }
    }

  if (dict)
    dict_ref (dict);

  STACK_UNWIND (frame, op_ret, op_errno, dict);

  if (dict)
    dict_unref (dict);

  return 0;
}

/*
 * client_removexattr_cbk - removexattr callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_removexattr_cbk (call_frame_t *frame,
                        gf_hdr_common_t *hdr, size_t hdrlen,
                        char *buf, size_t buflen)
{
  int op_ret = 0;
  int op_errno = 0;

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

/*
 * client_lk_cbk - lk callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_lk_common_cbk (call_frame_t *frame,
               gf_hdr_common_t *hdr, size_t hdrlen,
               char *buf, size_t buflen)
{
  struct flock lock = {0,};
  gf_fop_lk_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret >= 0)
    {
      gf_flock_to_flock (&rsp->flock, &lock);
    }

  STACK_UNWIND (frame, op_ret, op_errno, &lock);
  return 0;
}


/*
 * client_gf_file_lk_cbk - gf_file_lk callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_gf_file_lk_cbk (call_frame_t *frame,
		       gf_hdr_common_t *hdr, size_t hdrlen,
		       char *buf, size_t buflen)
{
  gf_fop_gf_file_lk_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}


/*
 * client_gf_dir_lk_cbk - gf_dir_lk callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_gf_dir_lk_cbk (call_frame_t *frame,
		      gf_hdr_common_t *hdr, size_t hdrlen,
		      char *buf, size_t buflen)
{
  gf_fop_gf_dir_lk_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}


/**
 * client_writedir_cbk -
 *
 * @frame:
 * @args:
 *
 * not for external reference
 */
int32_t
client_setdents_cbk (call_frame_t *frame,
                     gf_hdr_common_t *hdr, size_t hdrlen,
                     char *buf, size_t buflen)
{
  int op_ret = 0;
  int op_errno = 0;

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}


/*
 * client_lock_cbk - lock callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_lock_cbk (call_frame_t *frame,
                 gf_hdr_common_t *hdr, size_t hdrlen,
                 char *buf, size_t buflen)
{
  int op_ret = 0;
  int op_errno = 0;

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

/*
 * client_unlock_cbk - unlock callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_unlock_cbk (call_frame_t *frame,
                   gf_hdr_common_t *hdr, size_t hdrlen,
                   char *buf, size_t buflen)
{
  int op_ret = 0;
  int op_errno = 0;

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

/*
 * client_listlocks_cbk - listlocks callback for client protocol
 *
 * @frame: call frame
 * @args: arguments dictionary
 *
 * not for external reference
 */

int32_t
client_listlocks_cbk (call_frame_t *frame,
                      gf_hdr_common_t *hdr, size_t hdrlen,
                      char *buf, size_t buflen)
{
  /*TODO*/
  gf_mop_listlocks_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno, "");

  return 0;
}

/*
 * client_stats_cbk - stats callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t
client_stats_cbk (call_frame_t *frame,
                  gf_hdr_common_t *hdr, size_t hdrlen,
                  char *buf, size_t buflen)
{
  struct xlator_stats stats = {0,};
  gf_mop_stats_rsp_t *rsp = NULL;
  char *buffer = NULL;
  int op_ret = 0;
  int op_errno = 0;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret >= 0)
    {
      buffer = rsp->buf;

      sscanf (buffer, "%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64"\n",
              &stats.nr_files,
              &stats.disk_usage,
              &stats.free_disk,
              &stats.total_disk_size,
              &stats.read_usage,
              &stats.write_usage,
              &stats.disk_speed,
              &stats.nr_clients);
    }

  STACK_UNWIND (frame, op_ret, op_errno, &stats);
  return 0;
}

/*
 * client_getspec - getspec function for client protocol
 * @frame: call frame
 * @this: client protocol xlator structure
 * @flag:
 *
 * external reference through client_protocol_xlator->fops->getspec
 */
int32_t
client_getspec (call_frame_t *frame,
                xlator_t *this,
                int32_t flag)
{
  gf_hdr_common_t *hdr = NULL;
  gf_mop_getspec_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, 0);
  hdr    = gf_hdr_new (req, 0);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_MOP_REQUEST, GF_MOP_GETSPEC,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


/*
 * client_getspec_cbk - getspec callback for client protocol
 *
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t
client_getspec_cbk (call_frame_t *frame,
                    gf_hdr_common_t *hdr, size_t hdrlen,
                    char *buf, size_t buflen)
{
  gf_mop_getspec_rsp_t *rsp = NULL;
  char *spec_data = NULL;
  int op_ret = 0;
  int op_errno = 0;

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));
  rsp = gf_param (hdr);

  if (op_ret >= 0)
    {
      spec_data = rsp->spec;
    }

  STACK_UNWIND (frame, op_ret, op_errno, spec_data);
  return 0;
}

int32_t
client_checksum (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *loc,
                 int32_t flag)
{
  gf_hdr_common_t *hdr = NULL;
  gf_fop_checksum_req_t *req = NULL;
  size_t hdrlen = -1;
  int ret = -1;

  hdrlen = gf_hdr_len (req, strlen (loc->path) + 1);
  hdr    = gf_hdr_new (req, strlen (loc->path) + 1);
  req    = gf_param (hdr);

  req->ino  = hton64 (this_ino_get (loc->inode, this));
  req->flag = hton32 (flag);
  strcpy (req->path, loc->path);

  ret = protocol_client_xfer (frame, this,
                              GF_OP_TYPE_FOP_REQUEST, GF_FOP_CHECKSUM,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}

int32_t
client_checksum_cbk (call_frame_t *frame,
                     gf_hdr_common_t *hdr, size_t hdrlen,
                     char *buf, size_t buflen)
{
  gf_fop_checksum_rsp_t *rsp = NULL;
  int op_ret = 0;
  int op_errno = 0;
  unsigned char *fchecksum = NULL;
  unsigned char *dchecksum = NULL;

  rsp = gf_param (hdr);

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  if (op_ret >= 0)
    {
      fchecksum = rsp->fchecksum;
      dchecksum = rsp->dchecksum + GF_FILENAME_MAX;
    }

  STACK_UNWIND (frame, op_ret, op_errno, fchecksum, dchecksum);
  return 0;
}


/*
 * client_setspec_cbk - setspec callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

int32_t
client_setspec_cbk (call_frame_t *frame,
                    gf_hdr_common_t *hdr, size_t hdrlen,
                    char *buf, size_t buflen)
{
  int op_ret = 0;
  int op_errno = 0;

  op_ret   = ntoh32 (hdr->rsp.op_ret);
  op_errno = gf_error_to_errno (ntoh32 (hdr->rsp.op_errno));

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

/*
 * client_setvolume_cbk - setvolume callback for client protocol
 * @frame:  call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
int32_t
client_setvolume_cbk (call_frame_t *frame,
                      gf_hdr_common_t *hdr, size_t hdrlen,
                      char *buf, size_t buflen)
{
  xlator_t *this = NULL;
  transport_t *trans = NULL;
  dict_t *reply = NULL;
  gf_mop_setvolume_rsp_t *rsp = NULL;
  client_proto_priv_t *priv = NULL;
  char *remote_error = NULL;
  int32_t remote_errno = ENOTCONN;
  int32_t ret = -1;
  xlator_list_t *parent = NULL;

  this  = frame->this;
  trans = this->private;
  priv  = trans->xl_private;

  rsp = gf_param (hdr);

  reply = get_new_dict ();
  dict_unserialize (rsp->buf, ntoh32 (hdr->size), &reply);

  if (dict_get (reply, "RET"))
    ret = data_to_int32 (dict_get (reply, "RET"));
  else
    ret = -2;

  if (dict_get (reply, "ERRNO"))
    remote_errno = gf_error_to_errno (data_to_int32 (dict_get (reply, "ERRNO")));
  
  if (dict_get (reply, "ERROR"))
    remote_error = data_to_str (dict_get (reply, "ERROR"));

  if (ret < 0) {
    gf_log (trans->xl->name, GF_LOG_ERROR,
            "SETVOLUME on remote-host failed: ret=%d error=%s", ret,  
	    remote_error ? remote_error : strerror (remote_errno));
    errno = remote_errno;
  } else {
    gf_log (trans->xl->name, GF_LOG_DEBUG,
            "SETVOLUME on remote-host succeeded");
  }

  if (reply)
    dict_destroy (reply);

  if (!ret) {
    pthread_mutex_lock (&(priv->lock));
    {
      priv->connected = 1;
    }
    pthread_mutex_unlock (&(priv->lock));

    parent = trans->xl->parents;
    while (parent) {
      parent->xlator->notify (parent->xlator,
                              GF_EVENT_CHILD_UP,
                              trans->xl);
      parent = parent->next;
    }
  }

  STACK_DESTROY (frame->root);
  return ret;
}

/*
 * client_enosys_cbk -
 * @frame: call frame
 *
 * not for external reference
 */
int32_t
client_enosys_cbk (call_frame_t *frame,
                   gf_hdr_common_t *hdr, size_t hdrlen,
                   char *buf, size_t buflen)
{
  STACK_DESTROY (frame->root);
  return 0;
}

void
client_protocol_reconnect (void *trans_ptr)
{
  transport_t *trans = trans_ptr;
  client_proto_priv_t *priv = trans->xl_private;
  struct timeval tv = {0, 0};

  pthread_mutex_lock (&priv->lock);
  {
    if (priv->reconnect)
      gf_timer_call_cancel (trans->xl->ctx, priv->reconnect);
    priv->reconnect = 0;

    if (!priv->connected) {
      uint32_t n_plus_1 = priv->n_minus_1 + priv->n;

      priv->n_minus_1 = priv->n;
      priv->n = n_plus_1;
      tv.tv_sec = n_plus_1;

      gf_log (trans->xl->name, GF_LOG_DEBUG, "attempting reconnect");
      transport_connect (trans);

      priv->reconnect = gf_timer_call_after (trans->xl->ctx, tv,
                                             client_protocol_reconnect, trans);
    } else {
      gf_log (trans->xl->name, GF_LOG_DEBUG, "breaking reconnect chain");
      priv->n_minus_1 = 0;
      priv->n = 1;
    }
  }
  pthread_mutex_unlock (&priv->lock);
}

/*
 * client_protocol_cleanup - cleanup function
 * @trans: transport object
 *
 */
int
protocol_client_cleanup (transport_t *trans)
{
  client_proto_priv_t *priv = trans->xl_private;
  //  glusterfs_ctx_t *ctx = trans->xl->ctx;
  dict_t *saved_frames = NULL;

  gf_log (trans->xl->name, GF_LOG_DEBUG,
          "cleaning up state in transport object %p", trans);

  pthread_mutex_lock (&priv->lock);
  {
    saved_frames = priv->saved_frames;
    priv->saved_frames = get_new_dict_full (1024);
    data_pair_t *trav = priv->saved_fds->members_list;
    xlator_t *this = trans->xl;

    while (trav) {
      fd_t *tmp = (fd_t *)(long) strtoul (trav->key, NULL, 0);
      if (tmp->ctx)
        dict_del (tmp->ctx, this->name);
      trav = trav->next;
    }

    dict_destroy (priv->saved_fds);

    priv->saved_fds = get_new_dict_full (64);


    /* bailout logic cleanup */
    memset (&(priv->last_sent), 0, sizeof (priv->last_sent));
    memset (&(priv->last_received), 0, sizeof (priv->last_received));

    if (!priv->timer) {
      gf_log (trans->xl->name, GF_LOG_DEBUG, "priv->timer is NULL!!!!");
    } else {
      gf_timer_call_cancel (trans->xl->ctx, priv->timer);
      priv->timer = NULL;
    }

    if (!priv->reconnect) {
      /* :O This part is empty.. any thing missing? */
    }
  }
  pthread_mutex_unlock (&priv->lock);

  {
    data_pair_t *trav = saved_frames->members_list;
    dict_t *reply = dict_ref (get_new_dict ());
    reply->is_locked = 1;
    while (trav && trav->next)
      trav = trav->next;
    while (trav) {
      gf_hdr_common_t hdr = {0, };

      call_frame_t *tmp = (call_frame_t *) (trav->value->data);

      if ((tmp->type == GF_OP_TYPE_FOP_REQUEST) || (tmp->type == GF_OP_TYPE_FOP_REPLY))
	      gf_log (trans->xl->name, GF_LOG_ERROR,
		      "forced unwinding frame type(%d) op(%s) reply=@%p",
		      tmp->type, gf_fop_list[tmp->op], reply);
      else if ((tmp->type == GF_OP_TYPE_MOP_REQUEST) || (tmp->type == GF_OP_TYPE_MOP_REPLY))
	      gf_log (trans->xl->name, GF_LOG_ERROR,
		      "forced unwinding frame type(%d) op(%s) reply=@%p",
		      tmp->type, gf_mop_list[tmp->op], reply);

      tmp->root->rsp_refs = dict_ref (reply);

      hdr.type = hton32 (tmp->type);
      hdr.op   = hton32 (tmp->op);
      hdr.rsp.op_ret   = hton32 (-1);
      hdr.rsp.op_errno = hton32 (ENOTCONN);

      if (tmp->type == GF_OP_TYPE_FOP_REQUEST)
        gf_fops[tmp->op] (tmp, &hdr, sizeof (hdr), NULL, 0);
      else
        gf_mops[tmp->op] (tmp, &hdr, sizeof (hdr), NULL, 0);

      dict_unref (reply);
      trav = trav->prev;
    }
    dict_unref (reply);

    dict_destroy (saved_frames);
  }

  return 0;
}

static gf_op_t gf_fops[] = {
  [GF_FOP_STAT]           =  client_stat_cbk,
  [GF_FOP_READLINK]       =  client_readlink_cbk,
  [GF_FOP_MKNOD]          =  client_mknod_cbk,
  [GF_FOP_MKDIR]          =  client_mkdir_cbk,
  [GF_FOP_UNLINK]         =  client_unlink_cbk,
  [GF_FOP_RMDIR]          =  client_rmdir_cbk,
  [GF_FOP_SYMLINK]        =  client_symlink_cbk,
  [GF_FOP_RENAME]         =  client_rename_cbk,
  [GF_FOP_LINK]           =  client_link_cbk,
  [GF_FOP_CHMOD]          =  client_chmod_cbk,
  [GF_FOP_CHOWN]          =  client_chown_cbk,
  [GF_FOP_TRUNCATE]       =  client_truncate_cbk,
  [GF_FOP_OPEN]           =  client_open_cbk,
  [GF_FOP_READ]           =  client_readv_cbk,
  [GF_FOP_WRITE]          =  client_write_cbk,
  [GF_FOP_STATFS]         =  client_statfs_cbk,
  [GF_FOP_FLUSH]          =  client_flush_cbk,
  [GF_FOP_CLOSE]          =  client_close_cbk,
  [GF_FOP_FSYNC]          =  client_fsync_cbk,
  [GF_FOP_SETXATTR]       =  client_setxattr_cbk,
  [GF_FOP_GETXATTR]       =  client_getxattr_cbk,
  [GF_FOP_REMOVEXATTR]    =  client_removexattr_cbk,
  [GF_FOP_OPENDIR]        =  client_opendir_cbk,
  [GF_FOP_GETDENTS]       =  client_getdents_cbk,
  [GF_FOP_CLOSEDIR]       =  client_closedir_cbk,
  [GF_FOP_FSYNCDIR]       =  client_fsyncdir_cbk,
  [GF_FOP_ACCESS]         =  client_access_cbk,
  [GF_FOP_CREATE]         =  client_create_cbk,
  [GF_FOP_FTRUNCATE]      =  client_ftruncate_cbk,
  [GF_FOP_FSTAT]          =  client_fstat_cbk,
  [GF_FOP_LK]             =  client_lk_common_cbk,
  [GF_FOP_UTIMENS]        =  client_utimens_cbk,
  [GF_FOP_FCHMOD]         =  client_fchmod_cbk,
  [GF_FOP_FCHOWN]         =  client_fchown_cbk,
  [GF_FOP_LOOKUP]         =  client_lookup_cbk,
  [GF_FOP_FORGET]         =  client_enosys_cbk,
  [GF_FOP_SETDENTS]       =  client_setdents_cbk,
  [GF_FOP_RMELEM]         =  client_rmelem_cbk,
  [GF_FOP_INCVER]         =  client_incver_cbk,
  [GF_FOP_READDIR]        =  client_readdir_cbk,
  [GF_FOP_GF_FILE_LK]     =  client_gf_file_lk_cbk,
  [GF_FOP_GF_DIR_LK]      =  client_gf_dir_lk_cbk,
  [GF_FOP_CHECKSUM]       =  client_checksum_cbk,
  [GF_FOP_XATTROP]        =  client_xattrop_cbk,
};

static gf_op_t gf_mops[] = {
  [GF_MOP_SETVOLUME]        =  client_setvolume_cbk,
  [GF_MOP_GETVOLUME]        =  client_enosys_cbk,
  [GF_MOP_STATS]            =  client_stats_cbk,
  [GF_MOP_SETSPEC]          =  client_setspec_cbk,
  [GF_MOP_GETSPEC]          =  client_getspec_cbk,
  [GF_MOP_LOCK]             =  client_lock_cbk,
  [GF_MOP_UNLOCK]           =  client_unlock_cbk,
  [GF_MOP_LISTLOCKS]        =  client_listlocks_cbk,
  [GF_MOP_FSCK]             =  client_enosys_cbk,
};

/*
 * client_protocol_interpret - protocol interpreter
 * @trans: transport object
 * @blk: data block
 *
 */
int
protocol_client_interpret (xlator_t *this, transport_t *trans,
                           char *hdr_p, size_t hdrlen,
                           char *buf_p, size_t buflen)
{
  int ret = -1;
  call_frame_t *frame = NULL;
  gf_hdr_common_t *hdr = NULL;
  uint64_t callid = 0;
  int type = -1, op = -1;

  hdr  = (gf_hdr_common_t *)hdr_p;
  type = ntoh32 (hdr->type);
  op   = ntoh32 (hdr->op);

  callid = ntoh64 (hdr->callid);
  frame = lookup_frame (trans, callid);

  if (!frame)
    {
      gf_log (this->name, GF_LOG_ERROR,
              "could not find frame for callid=%"PRId64, callid);
      return ret;
    }

  switch (type)
    {
    case GF_OP_TYPE_FOP_REPLY:
      if (op > GF_FOP_MAXVALUE || op < 0) {
        gf_log (trans->xl->name, GF_LOG_WARNING,
                "invalid fop '%d'", op);
        break;
      }

      ret = gf_fops[op] (frame, hdr, hdrlen, buf_p, buflen);
      break;
    case GF_OP_TYPE_MOP_REPLY:
      if (op > GF_MOP_MAXVALUE || op < 0) {
        gf_log (trans->xl->name, GF_LOG_WARNING,
                "invalid fop '%d'", op);
        break;
      }

      ret = gf_mops[op] (frame, hdr, hdrlen, buf_p, buflen);
      break;
    default:
      gf_log (trans->xl->name, GF_LOG_ERROR,
              "invalid packet type: %d", type);
      break;
    }

  return ret;
}

/*
 * init - initiliazation function. called during loading of client protocol
 * @this:
 *
 */
int32_t
init (xlator_t *this)
{
  transport_t *trans = NULL;
  client_proto_priv_t *priv = NULL;
  data_t *timeout = NULL;
  int32_t transport_timeout = 0;
  data_t *max_block_size_data = NULL;

  if (this->children) {
    gf_log (this->name,
            GF_LOG_ERROR,
            "FATAL: client protocol translator cannot have subvolumes");
    return -1;
  }

  if (!dict_get (this->options, "remote-subvolume")) {
    gf_log (this->name, GF_LOG_ERROR,
            "missing 'option remote-subvolume'.");
    return -1;
  }

  timeout = dict_get (this->options, "transport-timeout");
  if (timeout) {
    transport_timeout = data_to_int32 (timeout);
    gf_log (this->name, GF_LOG_DEBUG,
            "setting transport-timeout to %d", transport_timeout);
  }
  else {
    gf_log (this->name, GF_LOG_DEBUG,
            "defaulting transport-timeout to 42");
    transport_timeout = 42;
  }

  trans = transport_load (this->options, this);

  if (!trans) {
    gf_log (this->name, GF_LOG_ERROR, "Failed to load transport");
    return -1;
  }

  this->private = transport_ref (trans);
  priv = calloc (1, sizeof (client_proto_priv_t));
  ERR_ABORT (priv);

  priv->saved_frames = get_new_dict_full (1024);
  priv->saved_fds = get_new_dict_full (64);
  priv->callid = 1;
  memset (&(priv->last_sent), 0, sizeof (priv->last_sent));
  memset (&(priv->last_received), 0, sizeof (priv->last_received));
  priv->transport_timeout = transport_timeout;
  pthread_mutex_init (&priv->lock, NULL);

  max_block_size_data = dict_get (this->options, "limits.transaction-size");

  if (max_block_size_data)
    {
      if (gf_string2bytesize (max_block_size_data->data, &priv->max_block_size) != 0)
      {
        gf_log ("client-protocol",
                GF_LOG_ERROR,
                "invalid number format \"%s\" of \"option limits.transaction-size\"",
                max_block_size_data->data);
        return -1;
      }
    }
  else
    {
      gf_log (this->name, GF_LOG_DEBUG,
              "defaulting limits.transaction-size to %d",
              DEFAULT_BLOCK_SIZE);
      priv->max_block_size = DEFAULT_BLOCK_SIZE;
    }

  trans->xl_private = priv;

#ifndef GF_DARWIN_HOST_OS
  {
    struct rlimit lim;

    lim.rlim_cur = 1048576;
    lim.rlim_max = 1048576;

    if (setrlimit (RLIMIT_NOFILE, &lim) == -1)
      {
        gf_log (this->name, GF_LOG_WARNING,
                "WARNING: Failed to set 'ulimit -n 1048576': %s",
                strerror(errno));
        lim.rlim_cur = 65536;
        lim.rlim_max = 65536;

        if (setrlimit (RLIMIT_NOFILE, &lim) == -1)
          gf_log (this->name, GF_LOG_ERROR,
                  "Failed to set max open fd to 64k: %s",
                  strerror(errno));
        else
          gf_log (this->name, GF_LOG_ERROR,
                  "max open fd set to 64k");

    }
  }
#endif

  return 0;
}

/*
 * fini - finish function called during unloading of client protocol
 * @this:
 *
 */
void
fini (xlator_t *this)
{
  /* TODO: Check if its enough.. how to call transport's fini () */
  client_proto_priv_t *priv = this->private;

  dict_destroy (priv->saved_frames);
  dict_destroy (priv->saved_fds);
  FREE (priv);
  return;
}


int
protocol_client_handshake (xlator_t *this,
                           transport_t *trans)
{
  gf_hdr_common_t *hdr = NULL;
  gf_mop_setvolume_req_t *req = NULL;
  dict_t *options = NULL;
  int ret = -1;
  int hdrlen = 0;
  int dict_len = 0;
  call_frame_t *fr = NULL;

  options = this->options;
  dict_set (options, "version", str_to_data (PACKAGE_VERSION));

  dict_len = dict_serialized_length (options);

  hdrlen = gf_hdr_len (req, dict_len);
  hdr    = gf_hdr_new (req, dict_len);
  req    = gf_param (hdr);

  dict_serialize (options, req->buf);

  fr  = create_frame (this, this->ctx->pool);
  ret = protocol_client_xfer (fr, this,
                              GF_OP_TYPE_MOP_REQUEST, GF_MOP_SETVOLUME,
                              hdr, hdrlen, NULL, 0, NULL);

  return ret;
}


int
protocol_client_pollout (xlator_t *this, transport_t *trans)
{
  client_proto_priv_t *priv = NULL;

  priv = trans->xl_private;

  pthread_mutex_lock (&priv->lock);
  {
    gettimeofday (&priv->last_sent, NULL);
  }
  pthread_mutex_unlock (&priv->lock);

  return 0;
}


int
protocol_client_pollin (xlator_t *this, transport_t *trans)
{
  client_proto_priv_t *priv = NULL;
  int ret = -1;
  char *buf = NULL;
  size_t buflen = 0;
  char *hdr = NULL;
  size_t hdrlen = 0;
  int connected = 0;

  priv = trans->xl_private;

  pthread_mutex_lock (&priv->lock);
  {
    gettimeofday (&priv->last_received, NULL);
    connected = priv->connected;
  }
  pthread_mutex_unlock (&priv->lock);

  ret = transport_receive (trans, &hdr, &hdrlen, &buf, &buflen);

  if (ret == 0)
    {
      ret = protocol_client_interpret (this, trans, hdr, hdrlen,
                                       buf, buflen);
    }

  /* TODO: use mem-pool */
  FREE (hdr);

  return ret;
}


/*
 * client_protocol_notify - notify function for client protocol
 * @this:
 * @trans: transport object
 * @event
 *
 */

int32_t
notify (xlator_t *this,
        int32_t event,
        void *data,
        ...)
{
  int ret = -1;
  transport_t *trans = NULL;

  switch (event)
    {
    case GF_EVENT_POLLOUT:
      {
        trans = data;

        ret = protocol_client_pollout (this, trans);

        break;
      }
    case GF_EVENT_POLLIN:
      {
        trans = data;

        ret = protocol_client_pollin (this, trans);

        break;
      }
      /* no break for ret check to happen below */
    case GF_EVENT_POLLERR:
      {
        trans = data;
        ret = -1;
        protocol_client_cleanup (trans);
      }
      client_proto_priv_t *priv = ((transport_t *)data)->xl_private;

      if (priv->connected) {
        transport_t *trans = data;
        struct timeval tv = {0, 0};
        client_proto_priv_t *priv = trans->xl_private;
        xlator_list_t *parent = NULL;

        parent = this->parents;
        while (parent) {
          parent->xlator->notify (parent->xlator,
                                  GF_EVENT_CHILD_DOWN,
                                  this);
          parent = parent->next;
        }

        priv->n_minus_1 = 0;
        priv->n = 1;

        pthread_mutex_lock (&priv->lock);
        {
          if (!priv->reconnect)
            priv->reconnect = gf_timer_call_after (trans->xl->ctx, tv,
                                                   client_protocol_reconnect,
                                                   trans);

          priv->connected = 0;
        }
        pthread_mutex_unlock (&priv->lock);

      }
      break;

    case GF_EVENT_PARENT_UP:
      {
        transport_t *trans = this->private;
        if (!trans) {
          gf_log (this->name,
                  GF_LOG_DEBUG,
                  "transport init failed");
          return -1;
        }
        client_proto_priv_t *priv = trans->xl_private;
        struct timeval tv = {0, 0};

        gf_log (this->name, GF_LOG_DEBUG,
                "got GF_EVENT_PARENT_UP, attempting connect on transport");

        //      ret = transport_connect (trans);

        priv->n_minus_1 = 0;
        priv->n = 1;

        priv->reconnect = gf_timer_call_after (trans->xl->ctx, tv,
                                               client_protocol_reconnect,
                                               trans);

        if (ret) {
          /* TODO: schedule reconnection with timer */
        }

	/* Let the connection/re-connection happen in background, for now, don't hang here,
	 * tell the parents that i am all ok..
	 */
	{
	  xlator_list_t *parent = NULL;
	  parent = trans->xl->parents;
	  while (parent) 
	    {
	      parent->xlator->notify (parent->xlator,
				      GF_EVENT_CHILD_CONNECTING,
				      trans->xl);
	      parent = parent->next;
	    }
	}
      }
      break;

    case GF_EVENT_CHILD_UP:
      {
        transport_t *trans = data;
        data_t *handshake = dict_get (this->options, "disable-handshake");

        gf_log (this->name, GF_LOG_DEBUG, "got GF_EVENT_CHILD_UP");
        if (!handshake ||
            (strcasecmp (data_to_str (handshake), "on"))) 
	  {
	    ret = protocol_client_handshake (this, trans);
	  } 
	else 
	  {
	    ((client_proto_priv_t *)trans->xl_private)->connected = 1;
	    ret = default_notify (this, event, trans);
	  }

        if (ret) 
          transport_disconnect (trans);
        
      }
      break;

    default:
      gf_log (this->name, GF_LOG_DEBUG,
              "got %d, calling default_notify ()", event);

      default_notify (this, event, data);
      break;
    }

  return ret;
}


struct xlator_fops fops = {
  .stat        = client_stat,
  .readlink    = client_readlink,
  .mknod       = client_mknod,
  .mkdir       = client_mkdir,
  .unlink      = client_unlink,
  .rmdir       = client_rmdir,
  .rmelem      = client_rmelem,
  .symlink     = client_symlink,
  .rename      = client_rename,
  .link        = client_link,
  .chmod       = client_chmod,
  .chown       = client_chown,
  .truncate    = client_truncate,
  .utimens     = client_utimens,
  .open        = client_open,
  .readv       = client_readv,
  .writev      = client_writev,
  .statfs      = client_statfs,
  .flush       = client_flush,
//  .close       = client_close,
  .fsync       = client_fsync,
  .incver      = client_incver,
  .setxattr    = client_setxattr,
  .getxattr    = client_getxattr,
  .removexattr = client_removexattr,
  .opendir     = client_opendir,
  .readdir     = client_readdir,
//  .closedir    = client_closedir,
  .fsyncdir    = client_fsyncdir,
  .access      = client_access,
  .ftruncate   = client_ftruncate,
  .fstat       = client_fstat,
  .create      = client_create,
  .lk          = client_lk,
  .gf_file_lk  = client_gf_file_lk,
  .gf_dir_lk   = client_gf_dir_lk,
  .lookup      = client_lookup,
  .forget      = client_forget,
  .fchmod      = client_fchmod,
  .fchown      = client_fchown,
  .setdents    = client_setdents,
  .getdents    = client_getdents,
  .checksum    = client_checksum,
  .xattrop     = client_xattrop,
};

struct xlator_mops mops = {
  .stats     = client_stats,
  .lock      = client_lock,
  .unlock    = client_unlock,
  .listlocks = client_listlocks,
  .getspec   = client_getspec,
};

struct xlator_cbks cbks = {
	.release = client_close,
	.releasedir = client_closedir
};


struct xlator_options options[] = {
	/* Authentication module */
 	{ "username", GF_OPTION_TYPE_STR, 0, },
 	{ "password", GF_OPTION_TYPE_STR, 0, }, 
  
  	/* Transport */
 	{ "ib-verbs-[work-request-send-size|...]", GF_OPTION_TYPE_ANY, 9, 0, 0 }, 
 	{ "remote-port", GF_OPTION_TYPE_INT, 0, 1025, 65534 }, 
 	{ "transport-type", GF_OPTION_TYPE_STR, 0, 0, 0, "tcp|socket|ib-verbs|unix|ib-sdp" }, 
 	{ "address-family", GF_OPTION_TYPE_STR, 0, 0, 0, "inet|inet6|inet/inet6|inet6/inet|unix|ib-sdp" }, 
 	{ "remote-host", GF_OPTION_TYPE_STR, 0, }, 
 	{ "connect-path", GF_OPTION_TYPE_STR, 0, },
 	{ "non-blocking-io", GF_OPTION_TYPE_BOOL, 0, }, 
  
  	/* Client protocol itself */
 	{ "limits.transaction-size", GF_OPTION_TYPE_SIZET, 0, 128 * GF_UNIT_KB, 8 * GF_UNIT_MB }, 
 	{ "remote-subvolume", GF_OPTION_TYPE_STR, 0, }, 
 	{ "transport-timeout", GF_OPTION_TYPE_TIME, 0, 1, 3600 }, /* More than 10mins? */

	{ NULL, 0, },
};
