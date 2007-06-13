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

#include "glusterfs.h"
#include "client-protocol.h"
#include "dict.h"
#include "protocol.h"
#include "transport.h"
#include "xlator.h"
#include "logging.h"
//#include "layout.h"
#include "timer.h"

#include <inttypes.h>

#if __WORDSIZE == 64
# define F_L64 "%l"
#else
# define F_L64 "%ll"
#endif

static int32_t client_protocol_notify (xlator_t *this, transport_t *trans, int32_t event);
static int32_t client_protocol_interpret (transport_t *trans, gf_block_t *blk);
static int32_t client_protocol_cleanup (transport_t *trans);

/*
 * str_to_ptr - convert a string to pointer
 * @string: string
 *
 */
static void *
str_to_ptr (char *string)
{
  void *ptr = (void *)strtoul (string, NULL, 16);
  return ptr;
}

#if 0
/*
 * ptr_to_str - convert a pointer to string
 * @ptr: pointer
 *
 */
static char *
ptr_to_str (void *ptr)
{
  char *str;
  asprintf (&str, "%p", ptr);
  return str;
}
#endif

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
  client_proto_priv_t *priv = trans->xl_private;
  char buf[64];
  call_frame_t *frame = NULL;
  snprintf (buf, 64, "%"PRId64, callid);

  pthread_mutex_lock (&priv->lock);
  frame = data_to_bin (dict_get (priv->saved_frames, buf));
  dict_del (priv->saved_frames, buf);
  pthread_mutex_unlock (&priv->lock);
  return frame;
}

/*
 * str_to_stat - convert a ASCII string to a struct stat
 * @buf: string
 *
 * not for external reference
 */
static struct stat *
str_to_stat (char *buf)
{
  struct stat *stbuf = calloc (1, sizeof (*stbuf));

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

  sscanf (buf, GF_STAT_PRINT_FMT_STR,
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

  stbuf->st_dev = dev;
  stbuf->st_ino = ino;
  stbuf->st_mode = mode;
  stbuf->st_nlink = nlink;
  stbuf->st_uid = uid;
  stbuf->st_gid = gid;
  stbuf->st_rdev = rdev;
  stbuf->st_size = size;
  stbuf->st_blksize = blksize;
  stbuf->st_blocks = blocks;
  stbuf->st_atime = atime;
  stbuf->st_atim.tv_nsec = atime_nsec;
  stbuf->st_mtime = mtime;
  stbuf->st_mtim.tv_nsec = mtime_nsec;
  stbuf->st_ctime = ctime;
  stbuf->st_ctim.tv_nsec = ctime_nsec;

  return stbuf;
}

/* 
 * client_protocol_xfer - client protocol transfer routine. called to send 
 *                        request packet to server
 * @frame: call frame
 * @this:
 * @type: operation type
 * @op: operation
 * @request: request data
 *
 * not for external reference
 */
static int32_t
client_protocol_xfer (call_frame_t *frame,
		      xlator_t *this,
		      glusterfs_op_type_t type,
		      int32_t op,
		      dict_t *request)
{
  int32_t ret;
  transport_t *trans;
  client_proto_priv_t *proto_priv;

  if (!this) {
    gf_log ("protocol/client",
	    GF_LOG_ERROR,
	    "'this' is NULL");
    return -1;
  }

  if (!request) {
    gf_log ("protocol/client",
	    GF_LOG_ERROR,
	    "request is NULL");
    return -1;
  }

  trans = this->private;
  if (!trans) {
    gf_log ("protocol/client",
	    GF_LOG_ERROR,
	    "this->private is NULL");
    return -1;
  }
  

  proto_priv = trans->xl_private;
  if (!proto_priv) {
    gf_log ("protocol/client",
	    GF_LOG_ERROR,
	    "trans->xl_private is NULL");
    return -1;
  }

  dict_set (request, "CALLER_UID", data_from_uint64 (frame->root->uid));
  dict_set (request, "CALLER_GID", data_from_uint64 (frame->root->gid));
  dict_set (request, "CALLER_PID", data_from_uint64 (frame->root->pid));

  {
    int64_t callid;
    gf_block_t *blk;
    struct iovec *vector = NULL;
    int32_t count = 0;
    int32_t i;
    char buf[64];

    pthread_mutex_lock (&proto_priv->lock);
    callid = proto_priv->callid++;
    snprintf (buf, 64, "%"PRId64, callid);
    dict_set (proto_priv->saved_frames,
	      buf,
	      bin_to_data (frame, sizeof (frame)));
    pthread_mutex_unlock (&proto_priv->lock);

    blk = gf_block_new (callid);
    blk->type = type;
    blk->op = op;
    blk->size = 0;    // obselete
    blk->data = NULL; // obselete
    blk->dict = request;

    count = gf_block_iovec_len (blk);
    vector = alloca (count * sizeof (*vector));
    memset (vector, 0, count * sizeof (*vector));

    gf_block_to_iovec (blk, vector, count);
    for (i=0; i<count; i++)
      if (!vector[i].iov_base)
	vector[i].iov_base = alloca (vector[i].iov_len);
    gf_block_to_iovec (blk, vector, count);

    ret = trans->ops->writev (trans, vector, count);

    //  transport_flush (trans);

    free (blk);

    if (ret != 0) {
      gf_log ("protocol/client",
	      GF_LOG_ERROR,
	      "transport_submit failed");
      transport_except (trans);
      client_protocol_cleanup (trans);
      return -1;
    }
  }
  return ret;
}

static void
call_bail (void *trans)
{
  gf_log ("client/protocol",
	  GF_LOG_CRITICAL,
	  "bailing transport");
  transport_bail (trans);
}

#define BAIL(frame, sec) do {                                     \
    struct timeval tv;                                            \
    tv.tv_sec = sec;                                              \
    tv.tv_usec = 0;                                               \
    client_local_t *_bail_local = frame->local;                   \
    _bail_local->timer = gf_timer_call_after (frame->this->ctx,   \
					tv,		          \
					call_bail,	          \
					frame->this->private);    \
} while (0)

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
 
static int32_t 
client_create (call_frame_t *frame,
	       xlator_t *this,
	       const char *path,
	       int32_t flags,
	       mode_t mode)
{
  dict_t *request = get_new_dict ();
  int32_t ret;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  
  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "FLAGS", data_from_int64 (flags));
  dict_set (request, "MODE", data_from_int64 (mode));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_CREATE,
			      request);
  dict_destroy (request);
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

static int32_t 
client_open (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     int32_t flags)
{
  client_local_t *local = calloc (1, sizeof (client_local_t));
  dict_t *request = get_new_dict ();
  int32_t ret;
  const char *path = loc->path;
  ino_t ino = loc->inode->ino;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "FLAGS", data_from_int64 (flags));

  local->inode = loc->inode;
  frame->local = local;
  
  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_OPEN,
			      request);

  dict_destroy (request);
  //  free (local);
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

static int32_t 
client_stat (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
  dict_t *request = get_new_dict ();
  int32_t ret;

  const char *path = loc->path;
  ino_t ino = loc->inode->ino;
  client_local_t *local = calloc (1, sizeof (client_local_t));

  frame->local = local;
  local->inode = loc->inode;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);
  
  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_STAT,
			      request);

  dict_destroy (request);
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


static int32_t 
client_readlink (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 size_t size)
{
  dict_t *request = get_new_dict ();
  int32_t ret;
  
  const char *path = loc->path;
  inode_t *inode = loc->inode;
  ino_t ino = inode->ino;
  client_local_t *local = calloc (1, sizeof (client_local_t));

  frame->local = local;
  local->inode = inode;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "LEN", data_from_int64 (size));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_READLINK,
			      request);

  dict_destroy (request);
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

static int32_t 
client_mknod (call_frame_t *frame,
	      xlator_t *this,
	      const char *path,
	      mode_t mode,
	      dev_t dev)
{
  dict_t *request = get_new_dict ();
  int32_t ret;
  client_local_t *local = calloc (1, sizeof (client_local_t));

  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "MODE", data_from_int64 (mode));
  dict_set (request, "DEV", data_from_int64 (dev));
  dict_set (request, "CALLER_UID", data_from_uint64 (frame->root->uid));
  dict_set (request, "CALLER_GID", data_from_uint64 (frame->root->gid));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_MKNOD,
			      request);

  dict_destroy (request);
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


static int32_t 
client_mkdir (call_frame_t *frame,
	      xlator_t *this,
	      const char *path,
	      mode_t mode)
{
  dict_t *request = get_new_dict ();
  int32_t ret;
  client_local_t *local = calloc (1, sizeof (client_local_t));

  frame->local = local;
  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "MODE", data_from_int64 (mode));
  dict_set (request, "CALLER_UID", data_from_uint64 (frame->root->uid));
  dict_set (request, "CALLER_GID", data_from_uint64 (frame->root->gid));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_MKDIR,
			      request);

  dict_destroy (request);
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

static int32_t 
client_unlink (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  const char *path = loc->path;
  inode_t *inode = loc->inode;
  ino_t ino = 0;
  client_local_t *local = calloc (1, sizeof (client_local_t));

  if (inode) {
    ino = inode->ino;
    local->inode = inode;
  } else {
    ino = 0;
  }
  

  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_UNLINK,
			      request);

  dict_destroy (request);
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

static int32_t 
client_rmdir (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  dict_t *request = get_new_dict ();
  int32_t ret;
  const  char *path = loc->path;
  inode_t *inode = loc->inode;
  ino_t ino = 0;
  client_local_t *local = calloc (1, sizeof (client_local_t));

  if (inode){
    ino = inode->ino;
    local->inode = inode;
  }

  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_RMDIR,
			      request);

  dict_destroy (request);
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

static int32_t 
client_symlink (call_frame_t *frame,
		xlator_t *this,
		const char *oldpath,
		const char *newpath)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  client_local_t *local = calloc (1, sizeof (client_local_t));

  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)oldpath));
  dict_set (request, "SYMLINK", str_to_data ((char *)newpath));
  dict_set (request, "CALLER_UID", data_from_uint64 (frame->root->uid));
  dict_set (request, "CALLER_GID", data_from_uint64 (frame->root->gid));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_SYMLINK,
			      request);

  dict_destroy (request);
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

static int32_t 
client_rename (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *oldloc,
	       loc_t *newloc)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  client_local_t *local = calloc (1, sizeof (client_local_t));

  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)oldloc->path));
  dict_set (request, "INODE", data_from_uint64 (oldloc->ino));
  dict_set (request, "NEWPATH", str_to_data ((char *)newloc->path));
  dict_set (request, "NEWINODE", data_from_uint64 (newloc->ino));
  dict_set (request, "CALLER_UID", data_from_uint64 (frame->root->uid));
  dict_set (request, "CALLER_GID", data_from_uint64 (frame->root->gid));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_RENAME,
			      request);

  dict_destroy (request);
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

static int32_t 
client_link (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *oldloc,
	     const char *newpath)
{
  dict_t *request = get_new_dict ();
  int32_t ret;
  
  const char *oldpath = oldloc->path;
  inode_t *oldinode = oldloc->inode;
  ino_t oldino = oldinode->ino;
  client_local_t *local = calloc (1, sizeof (client_local_t));

  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)oldpath));
  dict_set (request, "INODE", data_from_uint64 (oldino));
  dict_set (request, "LINK", str_to_data ((char *)newpath));
  dict_set (request, "CALLER_UID", data_from_uint64 (frame->root->uid));
  dict_set (request, "CALLER_GID", data_from_uint64 (frame->root->gid));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_LINK,
			      request);

  dict_destroy (request);
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

static int32_t 
client_chmod (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  const char *path = loc->path;
  inode_t *inode = loc->inode;
  ino_t ino = inode->ino;
  client_local_t *local = calloc (1, sizeof (client_local_t));

  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "MODE", data_from_int64 (mode));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_CHMOD,
			      request);

  dict_destroy (request);
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

static int32_t 
client_chown (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      uid_t uid,
	      gid_t gid)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  const char *path = loc->path;
  inode_t *inode = loc->inode;
  ino_t ino = inode->ino;
  
  client_local_t *local = calloc (1, sizeof (client_local_t));
  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "CALLER_UID", data_from_uint64 (frame->root->uid));
  dict_set (request, "CALLER_GID", data_from_uint64 (frame->root->gid));
  dict_set (request, "UID", data_from_uint64 (uid));
  dict_set (request, "GID", data_from_uint64 (gid));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_CHOWN,
			      request);

  dict_destroy (request);
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

static int32_t 
client_truncate (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 off_t offset,
     struct timespec tv[2])
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  const char *path = loc->path;
  inode_t *inode = loc->inode;
  ino_t ino = inode->ino;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "OFFSET", data_from_int64 (offset));
  dict_set (request, "ACTIME_SEC", data_from_int64 (tv[0].tv_sec));
  dict_set (request, "ACTIME_NSEC", data_from_int64 (tv[0].tv_nsec));
  dict_set (request, "MODTIME_SEC", data_from_int64 (tv[1].tv_sec));
  dict_set (request, "MODTIME_NSEC", data_from_int64 (tv[1].tv_nsec));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_TRUNCATE,
			      request);

  dict_destroy (request);
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

static int32_t 
client_utimens (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       struct timespec *tvp)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;

  const char *path = loc->path;
  inode_t *inode = loc->inode;
  ino_t ino = inode->ino;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "ACTIME_SEC", data_from_int64 (tvp[0].tv_sec));
  dict_set (request, "ACTIME_NSEC", data_from_int64 (tvp[0].tv_nsec));
  dict_set (request, "MODTIME_SEC", data_from_int64 (tvp[1].tv_sec));
  dict_set (request, "MODTIME_NSEC", data_from_int64 (tvp[1].tv_nsec));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_UTIMENS,
			      request);

  dict_destroy (request);
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

static int32_t 
client_readv (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      size_t size,
	      off_t offset)
{
  dict_t *request = get_new_dict ();
  dict_t *ctx = fd->ctx;
  data_t *ctx_data = dict_get (ctx, this->name);
  int32_t ret;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  frame->local = local;

  if (!ctx_data) {
    struct iovec vec;
    vec.iov_base = "";
    vec.iov_len = 0;
    dict_destroy (request);
    STACK_UNWIND (frame, -1, EBADFD, &vec);
    return 0;
  }

  dict_set (request, "FD", str_to_data (data_to_str (ctx_data)));
  dict_set (request, "OFFSET", data_from_int64 (offset));
  dict_set (request, "LEN", data_from_int64 (size));

  //BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_READ,
			      request);

  dict_destroy (request);
  return ret;
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

static int32_t 
client_writev (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       struct iovec *vector,
	       int32_t count,
	       off_t offset,
         struct timespec tv[2])
{
  dict_t *request = get_new_dict ();
  dict_t *ctx = fd->ctx;
  data_t *ctx_data = dict_get (ctx, this->name);
  size_t size = 0, i;
  int32_t ret = -1;
  char *fd_str = NULL;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  frame->local = local;

  if (!ctx_data) {
    dict_destroy (request);
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }
 
  for (i = 0; i<count; i++)
    size += vector[i].iov_len;

  fd_str = strdup (data_to_str (ctx_data));

  dict_set (request, "FD", str_to_data (fd_str));
  dict_set (request, "OFFSET", data_from_int64 (offset));
  dict_set (request, "BUF", data_from_iovec (vector, count));
  dict_set (request, "LEN", data_from_int64 (size));
  dict_set (request, "ACTIME_SEC", data_from_int64 (tv[0].tv_sec));
  dict_set (request, "ACTIME_NSEC", data_from_int64 (tv[0].tv_nsec));
  dict_set (request, "MODTIME_SEC", data_from_int64 (tv[1].tv_sec));
  dict_set (request, "MODTIME_NSEC", data_from_int64 (tv[1].tv_nsec));
 
  //    BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_WRITE,
			      request);

  dict_destroy (request);
  free (fd_str);
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

static int32_t 
client_statfs (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  const char *path = loc->path;
  inode_t *inode = loc->inode;
  ino_t ino = inode->ino;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));


  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_STATFS,
			      request);

  dict_destroy (request);
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

static int32_t 
client_flush (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  dict_t *request = get_new_dict ();
  dict_t *ctx = fd->ctx;
  data_t *ctx_data = dict_get (ctx, this->name);
  int32_t ret = -1;
  char *fd_str = NULL;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  frame->local = local;

  if (!ctx_data) {
    dict_destroy (request);
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }
  fd_str = strdup (data_to_str (ctx_data));
  dict_set (request, "FD", str_to_data (fd_str));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FLUSH,
			      request);

  free (fd_str);
  dict_destroy (request);
  return ret;
}


/**
 * client_close - close function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 *
 * external reference through client_protocol_xlator->fops->close
 */

static int32_t 
client_close (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  dict_t *request = get_new_dict ();
  dict_t *ctx = fd->ctx;
  data_t *ctx_data = dict_get (ctx, this->name);
  transport_t *trans = NULL;
  client_proto_priv_t *priv = NULL;
  int32_t ret = -1;
  char *key = NULL;
  char *fd_str = NULL;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  frame->local = local;

  if (!ctx_data) {
    STACK_UNWIND (frame, -1, EBADFD);
    dict_destroy (ctx);
    return 0;
  }
  
  fd_str = strdup (data_to_str (ctx_data));
  dict_set (request, "FD", str_to_data (fd_str));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  trans = frame->this->private;

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_CLOSE,
			      request);

  priv = trans->xl_private;
  
  asprintf (&key, "%p", ctx);

  pthread_mutex_lock (&priv->lock);
  dict_del (priv->saved_fds, key); 
  pthread_mutex_unlock (&priv->lock);
  
  free (fd_str);
  free (key);
  free (data_to_str (ctx_data));
  dict_destroy (ctx);
  dict_destroy (request);

  if (fd->inode) {
    inode_unref (fd->inode);
    gf_log ("protocol/client",
	    GF_LOG_DEBUG,
	    "inode->ref is %d", fd->inode->ref);
    fd->inode = NULL;
  }

  free (fd);

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

static int32_t 
client_fsync (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t flags)
{
  dict_t *request = get_new_dict ();
  dict_t *ctx = fd->ctx;
  data_t *ctx_data = dict_get (ctx, this->name);
  int32_t ret = -1;
  char *fd_str = NULL;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  frame->local = local;

  if (!ctx_data) {
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  dict_set (request, "FLAGS", data_from_int64 (flags));
  
  fd_str = strdup (data_to_str (ctx_data));
  dict_set (request, "FD", str_to_data (fd_str));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FSYNC,
			      request);

  free (fd_str);
  dict_destroy (request);
  return ret;
}


/**
 * client_setxattr - setxattr function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 * @name:
 * @value:
 * @size:
 * @flags:
 *
 * external reference through client_protocol_xlator->fops->setxattr
 */

static int32_t 
client_setxattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 const char *name,
		 const char *value,
		 size_t size,
		 int32_t flags)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;  
  const char *path = loc->path;
  inode_t *inode = loc->inode;
  ino_t ino = inode->ino;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "FLAGS", data_from_int64 (flags));
  dict_set (request, "COUNT", data_from_int64 (size));
  dict_set (request, "NAME", str_to_data ((char *)name));
  dict_set (request, "VALUE", str_to_data ((char *)value));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_SETXATTR,
			      request);

  dict_destroy (request);
  return ret;
}


/**
 * client_getxattr - getxattr function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location structure
 * @name:
 * @size:
 *
 * external reference through client_protocol_xlator->fops->getxattr
 */

static int32_t 
client_getxattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 const char *name,
		 size_t size)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;  
  const char *path = loc->path;
  inode_t *inode = loc->inode;
  ino_t ino = inode->ino;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "NAME", str_to_data ((char *)name));
  dict_set (request, "COUNT", data_from_int64 (size));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_GETXATTR,
			      request);

  dict_destroy (request);
  return ret;
}


/**
 * client_listxattr - listxattr function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @loc: location
 * @size:
 *
 * external reference through client_protocol_xlator->fops->listxattr
 */

static int32_t 
client_listxattr (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  size_t size)
{
  dict_t *request = get_new_dict ();
  int32_t ret;
  const char *path = loc->path;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "COUNT", data_from_int64 (size));
  dict_set (request, "INODE", data_from_uint64 (loc->ino));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_LISTXATTR,
			      request);

  dict_destroy (request);
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
	     
static int32_t 
client_removexattr (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    const char *name)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  const char *path = loc->path;
  inode_t *inode = loc->inode;
  ino_t ino = inode->ino;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "NAME", str_to_data ((char *)name));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_REMOVEXATTR,
			      request);

  dict_destroy (request);
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

static int32_t 
client_opendir (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  const char *path = loc->path;
  inode_t *inode = loc->inode;
  ino_t ino = inode->ino;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  
  local->inode = loc->inode;
  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_OPENDIR,
			      request);

  dict_destroy (request);
  return ret;
}


/**
 * client_readdir - readdir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 *
 * external reference through client_protocol_xlator->fops->readdir
 */

static int32_t 
client_readdir (call_frame_t *frame,
		xlator_t *this,
		size_t size,
		off_t offset,
		fd_t *fd)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  data_t *fd_data = dict_get (fd->ctx, this->name);
  char *fd_str = NULL;

  frame->local = local;
  fd_str = strdup (data_to_str (fd_data));
  dict_set (request, "FD", str_to_data (fd_str));
  dict_set (request, "OFFSET", data_from_uint64 (offset));
  dict_set (request, "SIZE", data_from_uint64 (size));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_READDIR,
			      request);
  
  free (fd_str);
  dict_destroy (request);
  return ret;
}


/**
 * client_closedir - closedir function for client protocol
 * @frame: call frame
 * @this: this translator structure
 * @fd: file descriptor structure
 *
 * external reference through client_protocol_xlator->fops->closedir
 */

static int32_t 
client_closedir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd)
{
  dict_t *request = get_new_dict ();
  data_t *fd_data = dict_get (fd->ctx, this->name);
  transport_t *trans = NULL;
  client_proto_priv_t *priv = NULL;
  int32_t ret = -1;
  char *key = NULL;
  char *fd_str = NULL;
  client_local_t *local = calloc (1, sizeof (client_local_t));

  frame->local = local;

  if (!fd_data) {
    STACK_UNWIND (frame, -1, EBADFD);
    dict_destroy (fd->ctx);
    return 0;
  }
  
  fd_str = strdup (data_to_str (fd_data));
  dict_set (request, "FD", str_to_data (fd_str));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  trans = frame->this->private;

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_CLOSEDIR, 
			      request);

  priv = trans->xl_private;
  
  asprintf (&key, "%p", fd->ctx);

  pthread_mutex_lock (&priv->lock);
  dict_del (priv->saved_fds, key); 
  pthread_mutex_unlock (&priv->lock);

  free (key);
  free (fd_str);
  dict_destroy (fd->ctx);
  if (fd->inode){
    inode_unref (fd->inode);
    gf_log ("protocol/client",
	    GF_LOG_DEBUG,
	    "inode->ref is %d", fd->inode->ref);
  }

  dict_destroy (request);

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

static int32_t 
client_fsyncdir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 int32_t flags)
{
  int32_t ret = -1;
  dict_t *ctx = fd->ctx;
  data_t *ctx_data = dict_get (ctx, this->name);
  client_local_t *local = calloc (1, sizeof (client_local_t));

  frame->local = local;
  if (!ctx_data) {
    STACK_UNWIND (frame, -1, EBADFD);
    return -1;
  }

  /*  int32_t remote_errno = 0;
      struct brick_private *priv = this->private;
      dict_t *request = get_new_dict ();
      dict_t *reply = get_new_dict ();

      {
      dict_set (request, "PATH", str_to_data ((char *)path));
      dict_set (request, "FLAGS", int_to_data (datasync));
      }

      ret = fops_xfer (priv, OP_FSYNCDIR, request, reply);
      dict_destroy (request);

      if (ret != 0)
      goto ret;

      ret = data_to_int (dict_get (reply, "RET"));
      remote_errno = data_to_int (dict_get (reply, "ERRNO"));
  
      if (ret < 0) {
      errno = remote_errno;
      goto ret;      }

      ret:
      dict_destroy (reply); */
  
  STACK_UNWIND (frame, -1, ENOSYS);
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

static int32_t 
client_access (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       int32_t mask)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  const char *path = loc->path;
  inode_t *inode = loc->inode;
  ino_t ino = inode->ino;
  client_local_t *local = calloc (1, sizeof (client_local_t));

  frame->local = local;

  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (ino));
  dict_set (request, "MASK", data_from_int64 (mask));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_ACCESS,
			      request);

  dict_destroy (request);
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

static int32_t 
client_ftruncate (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  off_t offset,
      struct timespec tv[2])
{
  dict_t *request = get_new_dict ();
  dict_t *ctx = fd->ctx;
  data_t *ctx_data = dict_get (ctx, this->name);
  int32_t ret = -1;
  char *fd_str = NULL;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  frame->local = local;

  if (!ctx_data) {
    dict_destroy (request);
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }
  
  fd_str = strdup (data_to_str (ctx_data));
  
  dict_set (request, "FD", str_to_data (fd_str));
  dict_set (request, "OFFSET", data_from_int64 (offset));
  dict_set (request, "ACTIME_SEC", data_from_int64 (tv[0].tv_sec));
  dict_set (request, "MODTIME_SEC", data_from_int64 (tv[1].tv_sec));
  dict_set (request, "ACTIME_NSEC", data_from_int64 (tv[0].tv_nsec));
  dict_set (request, "MODTIME_NSEC", data_from_int64 (tv[1].tv_nsec));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FTRUNCATE,
			      request);
  
  free (fd_str);
  dict_destroy (request);
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

static int32_t 
client_fstat (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  dict_t *request = get_new_dict ();
  data_t *fd_data = dict_get (fd->ctx, this->name);
  int32_t ret = -1;
  char *fd_str = NULL;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  frame->local = local;

  if (!fd_data) {
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }
  
  fd_str = strdup (data_to_str (fd_data));
  dict_set (request, "FD", str_to_data (fd_str));

  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FSTAT,
			      request);
  
  free (fd_str);
  dict_destroy (request);
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

static int32_t 
client_lk (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   int32_t cmd,
	   struct flock *lock)
{
  dict_t *request = get_new_dict ();
  dict_t *ctx = fd->ctx;
  data_t *ctx_data = dict_get (ctx, this->name);
  int32_t ret = -1;
  char *fd_str = NULL;
  client_local_t *local = calloc (1, sizeof (client_local_t));
  frame->local = local;

  if (!ctx_data) {
    dict_destroy (request);
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }

  fd_str = strdup (data_to_str (ctx_data));
  dict_set (request, "FD", str_to_data (fd_str));
  dict_set (request, "CMD", data_from_int32 (cmd));
  dict_set (request, "TYPE", data_from_int16 (lock->l_type));
  dict_set (request, "WHENCE", data_from_int16 (lock->l_whence));
  dict_set (request, "START", data_from_int64 (lock->l_start));
  dict_set (request, "LEN", data_from_int64 (lock->l_len));
  dict_set (request, "PID", data_from_uint64 (lock->l_pid));
  dict_set (request, "CLIENT_PID", data_from_uint64 (getpid ()));

  //  BAIL (frame, ((client_proto_priv_t *)(((transport_t *)this->private)->xl_private))->transport_timeout);

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_LK,
			      request);

  free (fd_str);
  dict_destroy (request);
  return ret;
}

/** 
 * client_writedir - 
 */
static int32_t
client_writedir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 int32_t flags,
		 dir_entry_t *entries,
		 int32_t count)
{
  int32_t ret = -1;
  char *buffer = NULL;
  dict_t *request = get_new_dict ();
  data_t *fd_data = dict_get (fd->ctx, this->name);
  char *fd_str = NULL;

  frame->local = calloc (1, sizeof (client_local_t));

  fd_str = strdup (data_to_str (fd_data));
  dict_set (request, "FD", str_to_data (fd_str));
  dict_set (request, "FLAGS", data_from_int32 (flags));
  dict_set (request, "NR_ENTRIES", data_from_int32 (count));

  {   
    dir_entry_t *trav = entries;
    uint32_t len = 0;
    char *ptr = NULL;
    while (trav) {
      len += strlen (trav->name);
      len += 1;
      len += 256; // max possible for statbuf;
      trav = trav->next;
    }
    buffer = calloc (1, len);
    ptr = buffer;
    trav = entries;
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
	uint32_t atime_nsec = stbuf->st_atim.tv_nsec;
	uint32_t mtime = stbuf->st_mtime;
	uint32_t mtime_nsec = stbuf->st_mtim.tv_nsec;
	uint32_t ctime = stbuf->st_ctime;
	uint32_t ctime_nsec = stbuf->st_ctim.tv_nsec;

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
      this_len = sprintf (ptr, "%s/%s", 
			  trav->name,
			  tmp_buf);
      
      free (tmp_buf);
      trav = trav->next;
      ptr += this_len;
    }
    dict_set (request, "DENTRIES", str_to_data (buffer));
  }
  
  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_WRITEDIR,
			      request);

  free (fd_str);
  dict_destroy (request);

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
static int32_t 
client_lookup (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
  dict_t *request = get_new_dict ();
  const char *path = loc->path;
  int32_t ret = -1;
  client_local_t *local = calloc (1, sizeof (client_local_t));

  frame->local = local;
  
  dict_set (request, "PATH", str_to_data ((char *)path));
  dict_set (request, "INODE", data_from_uint64 (loc->ino));

  ret = client_protocol_xfer (frame, 
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_LOOKUP,
			      request);
  
  dict_destroy (request);
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
static int32_t
client_forget (call_frame_t *frame,
	       xlator_t *this,
	       inode_t *inode)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;
  client_local_t *local = calloc (1, sizeof (client_local_t));

  frame->local = local;

  dict_set (request, "INODE", data_from_uint64 (inode->ino));

  inode_forget (inode, 0);

  ret = client_protocol_xfer (frame, 
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FORGET,
			      request);

  dict_destroy (request);
  return ret;
}


/*
 * client_fchmod
 *
 */
static int32_t
client_fchmod (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       mode_t mode)
{
  dict_t *request = NULL;
  data_t *remote_fd_data = NULL;
  char *remote_fd = NULL;
  int32_t ret = -1;

  request = get_new_dict ();

  remote_fd_data = dict_get (fd->ctx, this->name);
  remote_fd      = strdup (data_to_str (remote_fd_data));

  dict_set (request, "FD", str_to_data (remote_fd));
  dict_set (request, "MODE", data_from_uint64 (mode));

  ret = client_protocol_xfer (frame, 
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FCHMOD,
			      request);

  free (remote_fd);
  dict_destroy (request);
  return 0;
}


static int32_t
client_fchmod_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = NULL, *errno_data = NULL, *stat_data = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  char *stat_str = NULL;
  struct stat *stbuf = NULL;

  ret_data = dict_get (args, "RET");
  errno_data = dict_get (args, "ERRNO");
  stat_data = dict_get (args, "STAT");
  
  if (!ret_data || !errno_data || !stat_data) {
    STACK_UNWIND (frame, op_ret, op_errno, stbuf);
    return -1;
  }
  
  op_ret = data_to_uint64 (ret_data);
  op_errno = data_to_uint64 (errno_data);
  

  stat_str = strdup (data_to_str (stat_data));
  stbuf = str_to_stat (stat_str);

  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  
  free (stat_str);
  free (stbuf);
  return 0;
}

/*
 * client_fchown
 *
 */
static int32_t
client_fchown (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       uid_t uid,
	       gid_t gid)
{
  dict_t *request = NULL;
  data_t *remote_fd_data = NULL;
  char *remote_fd = NULL;
  int32_t ret = -1;

  request = get_new_dict ();

  remote_fd_data = dict_get (fd->ctx, this->name);
  remote_fd      = strdup (data_to_str (remote_fd_data));

  dict_set (request, "FD", str_to_data (remote_fd));
  dict_set (request, "UID", data_from_uint64 (uid));
  dict_set (request, "GID", data_from_uint64 (gid));

  ret = client_protocol_xfer (frame, 
			      this,
			      GF_OP_TYPE_FOP_REQUEST,
			      GF_FOP_FCHMOD,
			      request);

  free (remote_fd);
  dict_destroy (request);
  return 0;

}

static int32_t
client_fchown_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = NULL, *errno_data = NULL, *stat_data = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  char *stat_str = NULL;
  struct stat *stbuf = NULL;

  ret_data = dict_get (args, "RET");
  errno_data = dict_get (args, "ERRNO");
  stat_data = dict_get (args, "STAT");
  
  if (!ret_data || !errno_data || !stat_data ) {
    STACK_UNWIND (frame, op_ret, op_errno, stbuf);
    return -1;
  }
  
  op_ret = data_to_uint64 (ret_data);
  op_errno = data_to_uint64 (errno_data);
  stat_str = strdup (data_to_str (stat_data));
  stbuf = str_to_stat (stat_str);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  
  free (stat_str);
  free (stbuf);
  return 0;
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

static int32_t 
client_stats (call_frame_t *frame,
	      xlator_t *this, 
	      int32_t flags)
{
  dict_t *request = get_new_dict ();
  int32_t ret;
  client_local_t *local = calloc (1, sizeof (client_local_t));

  frame->local = local;

  /* without this dummy key the server crashes */
  dict_set (request, "FLAGS", data_from_int64 (0)); 
  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_STATS,
			      request);

  dict_destroy (request);

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

//TODO: make it static (currently !static because of the warning)
int32_t 
client_fsck (call_frame_t *frame,
	     xlator_t *this,
	     int32_t flags)
{
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

static int32_t 
client_lock (call_frame_t *frame,
	     xlator_t *this,
	     const char *name)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;

  dict_set (request, "PATH", str_to_data ((char *)name));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_LOCK,
			      request);

  dict_destroy (request);

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

static int32_t 
client_unlock (call_frame_t *frame,
	       xlator_t *this,
	       const char *name)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;

  dict_set (request, "PATH", str_to_data ((char *)name));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_UNLOCK, request);

  dict_destroy (request);

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

static int32_t 
client_listlocks (call_frame_t *frame,
		  xlator_t *this,
		  const char *pattern)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;

  dict_set (request, "OP", data_from_uint64 (0xcafebabe));
  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_LISTLOCKS,
			      request);

  dict_destroy (request);

  return ret;
}



/* Callbacks */

/*
 * client_create_cbk - create callback function for client protocol
 * @frame: call frame
 * @args: arguments in dictionary
 *
 * not for external reference 
 */

static int32_t 
client_create_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *fd_data = dict_get (args, "FD");
  transport_t *trans = NULL;
  client_proto_priv_t *priv = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL; 
  char *buf = NULL;
  struct stat *stbuf = NULL;
  fd_t *fd = NULL;
  dict_t *file_ctx = NULL;
  inode_t *inode = NULL;
  
  if (!buf_data || !ret_data || !err_data || !fd_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);
  buf = data_to_str (buf_data);
  stbuf = str_to_stat (buf);
  fd = calloc (1, sizeof (fd_t));
  file_ctx = NULL;
  inode = NULL;

  if (op_ret >= 0) {
    /* handle fd */
    char *remote_fd = strdup (data_to_str (fd_data));
    char *key;

    trans = frame->this->private;
    priv = trans->xl_private;
    
    /* add newly created file's inode to client protocol inode table */
    inode = inode_update (priv->table, NULL, NULL, stbuf->st_ino);

    fd->ctx = get_new_dict ();
    fd->inode = inode;
    file_ctx = fd->ctx;

    dict_set (file_ctx,
	      (frame->this)->name,
	      str_to_data(remote_fd));

    asprintf (&key, "%p", file_ctx);

    pthread_mutex_lock (&priv->lock);
    dict_set (priv->saved_fds, key, str_to_data ("")); 
    pthread_mutex_unlock (&priv->lock);

    free (key);
  }

  STACK_UNWIND (frame, op_ret, op_errno, fd, inode, stbuf);

  free (stbuf);
  return 0;
}

/*
 * client_open_cbk - open callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_open_cbk (call_frame_t *frame,
		 dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *fd_data = dict_get (args, "FD");
  transport_t *trans = NULL;
  client_proto_priv_t *priv = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = 0;
  fd_t *fd = NULL; 
  dict_t *file_ctx = NULL;
  inode_t *inode = NULL;
  client_local_t *local = frame->local;

  if (!ret_data || !err_data || !fd_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);
  
  /*  char *buf = data_to_str (buf_data);
  struct stat *stbuf = str_to_stat (buf);
  */
  fd = calloc (1, sizeof (fd_t));
  file_ctx = NULL;

  if (op_ret >= 0) {
    /* handle fd */
    char *remote_fd = strdup (data_to_str (fd_data));
    char *key;

    trans = frame->this->private;
    priv = trans->xl_private;

    /* add opened file's inode to client protocol inode table */
    inode = local->inode;

    if (inode) 
      inode = inode_ref (inode);
  
    fd->ctx = get_new_dict ();
    fd->inode = inode;
    file_ctx = fd->ctx;

    dict_set (fd->ctx,
	      (frame->this)->name,
	      str_to_data(remote_fd));
    
    asprintf (&key, "%p", fd->ctx);

    pthread_mutex_lock (&priv->lock);
    dict_set (priv->saved_fds, key, str_to_data (""));
    pthread_mutex_unlock (&priv->lock);

    free (key);
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
static int32_t 
client_stat_cbk (call_frame_t *frame,
		 dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1; 
  int32_t op_errno = 0; 
  char *buf = NULL; 
  struct stat *stbuf = NULL;
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  buf = data_to_str (buf_data);
  stbuf = str_to_stat (buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

/* 
 * client_utimens_cbk - utimens callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
 
static int32_t 
client_utimens_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1; 
  int32_t op_errno = 0;
  char *buf = NULL;
  struct stat *stbuf = NULL;
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  buf = data_to_str (buf_data);
  stbuf = str_to_stat (buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

/*
 * client_chmod_cbk - chmod for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_chmod_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1; 
  int32_t op_errno = EINVAL; 
  char *buf = NULL;
  struct stat *stbuf = NULL;
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  buf = data_to_str (buf_data);
  stbuf = str_to_stat (buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

/*
 * client_chown_cbk - chown for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference 
 */
static int32_t 
client_chown_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1; 
  int32_t op_errno = EINVAL; 
  char *buf = NULL;
  struct stat *stbuf = NULL;
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  buf = data_to_str (buf_data);
  stbuf = str_to_stat (buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

/* 
 * client_mknod_cbk - mknod callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_mknod_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  char *buf = NULL;
  struct stat *stbuf = NULL;
  inode_t *inode = NULL;

  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }

  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  buf = data_to_str (buf_data);
  stbuf = str_to_stat (buf);
  inode = NULL;
  
  
  if (op_ret >= 0){
    /* handle inode */
    transport_t *trans = frame->this->private;
    client_proto_priv_t *priv = trans->xl_private;
    inode = inode_update (priv->table, NULL, NULL, stbuf->st_ino);
  }
  
  STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);
  free (stbuf);
  return 0;
}

/*
 * client_symlink_cbk - symlink callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_symlink_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *stat_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  char *stat_str = NULL;
  struct stat *stbuf = NULL;
  inode_t *inode = NULL;
  
  if (!stat_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  stat_str = data_to_str (stat_data);
  stbuf = str_to_stat (stat_str);
  inode = NULL;

  if (op_ret >= 0){
    /* handle inode */
    transport_t *trans = frame->this->private;
    client_proto_priv_t *priv = trans->xl_private;
    inode = inode_update (priv->table, NULL, NULL, stbuf->st_ino);
  }
  
  STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);
  
  if (inode){
    inode_unref (inode);
    gf_log ("protocol/client",
	    GF_LOG_DEBUG,
	    "inode->ref is %d", inode->ref);
  }

  free (stbuf);
  return 0;
}

/*
 * client_link_cbk - link callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference 
 */
static int32_t 
client_link_cbk (call_frame_t *frame,
		 dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1; 
  int32_t op_errno = EINVAL;
  char *buf = NULL;
  struct stat *stbuf = NULL;
  inode_t *inode = NULL;
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  buf = data_to_str (buf_data);
  stbuf = str_to_stat (buf);
    
  if (op_ret >= 0){
    /* handle inode */
    transport_t *trans = frame->this->private;
    client_proto_priv_t *priv = trans->xl_private;
    inode = inode_update (priv->table, NULL, NULL, stbuf->st_ino);
  }

  
  STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);

  if (inode){
    inode_unref (inode);
    gf_log ("protocol/client",
	    GF_LOG_DEBUG,
	    "inode->ref is %d", inode->ref);
  }

  free (stbuf);
  return 0;
}

/* 
 * client_truncate_cbk - truncate callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference 
 */

static int32_t 
client_truncate_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1; 
  int32_t op_errno = EINVAL;
  char *buf = NULL;
  struct stat *stbuf = NULL;
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  buf = data_to_str (buf_data);
  stbuf = str_to_stat (buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

/* client_fstat_cbk - fstat callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

static int32_t 
client_fstat_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *stat_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1; 
  int32_t op_errno = EINVAL;
  char *stat_buf = NULL;
  struct stat *stbuf = NULL;
  
  if (!stat_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  stat_buf = data_to_str (stat_data);
  stbuf = str_to_stat (stat_buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

/* 
 * client_ftruncate_cbk - ftruncate callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */ 
static int32_t 
client_ftruncate_cbk (call_frame_t *frame,
		      dict_t *args)
{
  data_t *buf_data = dict_get (args, "STAT");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1; 
  int32_t op_errno = EINVAL; 
  char *buf = NULL;
  struct stat *stbuf = NULL;
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  buf = data_to_str (buf_data);
  stbuf = str_to_stat (buf);
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  free (stbuf);
  return 0;
}

/* client_readv_cbk - readv callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external referece
 */

static int32_t 
client_readv_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  char *buf = NULL;
  struct iovec vec = {0,};
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  buf = data_to_bin (buf_data);
  
  vec.iov_base = buf;
  vec.iov_len = op_ret;
  STACK_UNWIND (frame, op_ret, op_errno, &vec, 1);

  return 0;
}

/* 
 * client_write_cbk - write callback for client protocol
 * @frame: cal frame
 * @args: argument dictionary
 *
 * not for external reference
 */

static int32_t 
client_write_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * client_readdir_cbk - readdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_readdir_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *buf_data = dict_get (args, "DENTRIES");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *cnt_data = dict_get (args, "NR_ENTRIES");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  int32_t nr_count = 0;
  char *buf = NULL;
  
  dir_entry_t *entry = NULL;
  dir_entry_t *trav = NULL, *prev = NULL;
  int32_t count, i, bread;
  char *ender = NULL, *buffer_ptr = NULL;
  char tmp_buf[512] = {0,};
  
  if (!buf_data || !ret_data || !err_data || !cnt_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL, 0);
    return 0;
  }
  
  op_ret   = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  nr_count = data_to_int32 (cnt_data);
  buf      = data_to_str (buf_data);
  
  entry = calloc (1, sizeof (dir_entry_t));
  prev = entry;
  buffer_ptr = buf;
  
  for (i = 0; i < nr_count ; i++) {
    bread = 0;
    trav = calloc (1, sizeof (dir_entry_t));
    ender = strchr (buffer_ptr, '/');
    count = ender - buffer_ptr;
    trav->name = calloc (1, count + 2);
    strncpy (trav->name, buffer_ptr, count);
    bread = count + 1;
    buffer_ptr += bread;
    
    ender = strchr (buffer_ptr, '\n');
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
      trav->buf.st_atim.tv_nsec = atime_nsec;
      trav->buf.st_mtime = mtime;
      trav->buf.st_mtim.tv_nsec = mtime_nsec;
      trav->buf.st_ctime = ctime;
      trav->buf.st_ctim.tv_nsec = ctime_nsec;
    }    
    prev->next = trav;
    prev = trav;
  }
  
  STACK_UNWIND (frame, op_ret, op_errno, entry, nr_count);

  prev = entry;
  trav = entry->next;
  while (trav) {
    prev->next = trav->next;
    free (trav->name);
    free (trav);
    trav = prev->next;
  }
  free (entry);

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
static int32_t 
client_fsync_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/* 
 * client_unlink_cbk - unlink callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_unlink_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_rename_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *stat_data = dict_get (args, "STAT");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  char *buf = NULL;
  struct stat *stbuf = NULL;

  if (!ret_data || !err_data || !stat_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret   = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  buf      = data_to_str (stat_data);
  stbuf    = str_to_stat (buf);

  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  free (stbuf);
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
static int32_t 
client_readlink_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *buf_data = dict_get (args, "LINK");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  char *buf = NULL;
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  op_ret   = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  buf      = data_to_str (buf_data);
  
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

/*
 * client_mkdir_cbk - mkdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_mkdir_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *buf_data = dict_get (args, "STAT");
  char *stat_str = NULL;
  struct stat *stbuf = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  inode_t *inode = NULL;
  
  if (!ret_data || !err_data || !buf_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  stat_str = strdup (data_to_str (buf_data));
  stbuf = str_to_stat (stat_str);
  
  inode = inode_update (frame->this->itable, NULL, NULL, stbuf->st_ino);

  STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);

  if (inode){
    inode_unref (inode);
    gf_log ("protocol/client",
	    GF_LOG_DEBUG,
	    "inode->ref is %d", inode->ref);
  }
  free (stat_str);
  free (stbuf);
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

static int32_t 
client_flush_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_close_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;

  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  
  return 0;
}

/*
 * client_opendir_cbk - opendir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_opendir_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *fd_data = dict_get (args, "FD");
  transport_t *trans = NULL;
  client_proto_priv_t *priv = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  fd_t *fd = NULL;
  client_local_t *local = frame->local;
  
  if (!ret_data || !err_data || !fd_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);
  fd = calloc (1, sizeof (fd_t));

  if (op_ret >= 0) {
    /* handle fd */
    char *key = NULL;
    char *remote_fd_str = strdup (data_to_str (fd_data));
    fd_t *remote_fd = str_to_ptr (remote_fd_str);
    
    trans = frame->this->private;
    priv = trans->xl_private;
    
    fd->ctx = get_new_dict ();
    
    /* TODO: get inode from client_opendir */
    /*    fd->inode = inode_update (priv->table, NULL, NULL, ino);*/
    if (local->inode)
      inode_ref (local->inode);

    fd->inode = local->inode;
    
    
    dict_set (fd->ctx,
	      (frame->this)->name,
	      str_to_data (remote_fd_str));
    
    asprintf (&key, "%p", remote_fd);

    pthread_mutex_lock (&priv->lock);
    dict_set (priv->saved_fds, key, str_to_data (""));
    pthread_mutex_unlock (&priv->lock);

    free (key);
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
static int32_t
client_closedir_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * client_rmdir_cbk - rmdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

static int32_t 
client_rmdir_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * client_statfs_cbk - statfs callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_statfs_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *buf_data = dict_get (args, "BUF");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  char *buf = NULL;
  struct statvfs *stbuf = NULL;
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  buf = data_to_str (buf_data);
  stbuf = calloc (1, sizeof (struct statvfs));
  
  {
    uint32_t bsize;
    uint32_t frsize;
    uint64_t blocks;
    uint64_t bfree;
    uint64_t bavail;
    uint64_t files;
    uint64_t ffree;
    uint64_t favail;
    uint32_t fsid;
    uint32_t flag;
    uint32_t namemax;
    
    sscanf (buf, GF_STATFS_SCAN_FMT_STR,
	    &bsize,
	    &frsize,
	    &blocks,
	    &bfree,
	    &bavail,
	    &files,
	    &ffree,
	    &favail,
	    &fsid,
	    &flag,
	    &namemax);
    
    stbuf->f_bsize = bsize;
    stbuf->f_frsize = frsize;
    stbuf->f_blocks = blocks;
    stbuf->f_bfree = bfree;
    stbuf->f_bavail = bavail;
    stbuf->f_files = files;
    stbuf->f_ffree = ffree;
    stbuf->f_favail = favail;
    stbuf->f_fsid = fsid;
    stbuf->f_flag = flag;
    stbuf->f_namemax = namemax;
    
  }
  
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);

  free (stbuf);
  return 0;
}

/*
 * client_fsyncdir_cbk - fsyncdir callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_fsyncdir_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_access_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_setxattr_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;

  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * client_listxattr_cbk - listxattr callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_listxattr_cbk (call_frame_t *frame,
		      dict_t *args)
{
  data_t *buf_data = dict_get (args, "VALUE");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  char *buf = NULL;
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  buf = data_to_str (buf_data);
  
  STACK_UNWIND (frame, op_ret, op_errno, buf);

  return 0;
}

/*
 * client_getxattr_cbk - getxattr callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_getxattr_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *buf_data = dict_get (args, "VALUE");
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  char *buf = NULL;
  
  if (!buf_data || !ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  buf = data_to_str (buf_data);
  
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

/*
 * client_removexattr_cbk - removexattr callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 * 
 * not for external reference
 */
static int32_t 
client_removexattr_cbk (call_frame_t *frame,
			dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_lk_cbk (call_frame_t *frame,
	       dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *type_data = dict_get (args, "TYPE");
  data_t *whence_data = dict_get (args, "WHENCE");
  data_t *start_data = dict_get (args, "START");
  data_t *len_data = dict_get (args, "LEN");
  data_t *pid_data = dict_get (args, "PID");
  struct flock lock = {0,};
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || 
      !err_data ||
      !type_data ||
      !whence_data ||
      !start_data ||
      !len_data ||
      !pid_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);

  lock.l_type =  data_to_int16 (type_data);
  lock.l_whence =  data_to_int16 (whence_data);
  lock.l_start =  data_to_int64 (start_data);
  lock.l_len =  data_to_int64 (len_data);
  lock.l_pid =  data_to_uint32 (pid_data);

  STACK_UNWIND (frame, op_ret, op_errno, &lock);
  return 0;
}

/**
 * client_writedir_cbk -
 */
static int32_t 
client_writedir_cbk (call_frame_t *frame,
		     dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_lock_cbk (call_frame_t *frame,
		 dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_unlock_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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

static int32_t 
client_listlocks_cbk (call_frame_t *frame,
		      dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno, "");
  return 0;
}

/*
 * client_fsck_cbk - fsck callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

static int32_t 
client_fsck_cbk (call_frame_t *frame,
		 dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
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

static int32_t 
client_stats_cbk (call_frame_t *frame,
		  dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *buf_data = dict_get (args, "BUF");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  char *buf = NULL;
  struct xlator_stats stats = {0,};

  if (!ret_data || !err_data || !buf_data) {
    struct xlator_stats stats = {0,};
    STACK_UNWIND (frame, -1, EINVAL, &stats);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  buf = data_to_bin (buf_data);

  sscanf (buf, "%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64",%"SCNx64"\n",
	  &stats.nr_files,
	  &stats.disk_usage,
	  &stats.free_disk,
	  &stats.total_disk_size,
	  &stats.read_usage,
	  &stats.write_usage,
	  &stats.disk_speed,
	  &stats.nr_clients);
  
  STACK_UNWIND (frame, op_ret, op_errno, &stats);
  return 0;
}

/* 
 * client_lookup_cbk - lookup callback for client protocol
 * @frame: call frame
 * @args: arguments dictionary
 * 
 * not for external reference
 */
static int32_t
client_lookup_cbk (call_frame_t *frame,
		   dict_t *args)
{
  transport_t *trans = NULL;
  client_proto_priv_t *priv = NULL;
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  data_t *stat_data = dict_get (args, "STAT");
  char *stat_buf = NULL;
  struct stat *stbuf = NULL;
  inode_t *inode = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;

  if (!ret_data || !err_data || !stat_data) {
    gf_log ("protocol/client",
	    GF_LOG_ERROR,
	    "client lookup failed");
    STACK_UNWIND (frame, -1, EINVAL, NULL, NULL);
    return 0;
  }
  
  stat_buf = data_to_str (stat_data);
  stbuf = str_to_stat (stat_buf);
  
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);

  if (op_ret == 0) {
    trans = frame->this->private;
    priv = trans->xl_private;
    inode = inode_update (priv->table, NULL, NULL, stbuf->st_ino);
  }

  STACK_UNWIND (frame, op_ret, op_errno, inode, stbuf);
  
  if (inode){
    /* TODO: inode->ref == 0, which should not happend unless fuse sends forget*/
    inode_unref (inode);
    gf_log ("protocol/client",
	    GF_LOG_DEBUG,
	    "inode->ref is %d", inode->ref);
  } else {
    gf_log ("protocol/client", GF_LOG_DEBUG, "lookup_cbk not successful");
  }
  free (stbuf);
  return 0;
}

/*
 * client_forget_cbk - forget callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 * 
 * not for external reference
 */
static int32_t
client_forget_cbk (call_frame_t *frame,
		   dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;

  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL, NULL, NULL);
    return 0;
  }

  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);

  STACK_UNWIND (frame, op_ret, op_errno);
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
static int32_t
client_getspec (call_frame_t *frame,
		xlator_t *this,
		int32_t flag)
{
  dict_t *request = get_new_dict ();
  int32_t ret = -1;

  dict_set (request, "foo", str_to_data ("bar"));

  ret = client_protocol_xfer (frame,
			      this,
			      GF_OP_TYPE_MOP_REQUEST,
			      GF_MOP_GETSPEC,
			      request);

  dict_destroy (request);

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

static int32_t 
client_getspec_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  data_t *spec_data = NULL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  spec_data = dict_get (args, "spec-file-data");
  
  STACK_UNWIND (frame, op_ret, op_errno, (spec_data?spec_data->data:""));
  return 0;
}

/*
 * client_setspec_cbk - setspec callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */

static int32_t 
client_setspec_cbk (call_frame_t *frame,
		    dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
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
static int32_t 
client_setvolume_cbk (call_frame_t *frame,
		      dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * client_getvolume_cbk - getvolume callback for client protocol
 * @frame: call frame
 * @args: argument dictionary
 *
 * not for external reference
 */
static int32_t 
client_getvolume_cbk (call_frame_t *frame,
		      dict_t *args)
{
  data_t *ret_data = dict_get (args, "RET");
  data_t *err_data = dict_get (args, "ERRNO");
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;
  
  if (!ret_data || !err_data) {
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }
  
  op_ret = data_to_int32 (ret_data);
  op_errno = data_to_int32 (err_data);  
  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * client_protocol_notify - notify function for client protocol
 * @this:
 * @trans: transport object
 * @event
 *
 */
static int32_t
client_protocol_notify (xlator_t *this,
			transport_t *trans,
			int32_t event)
{
  int ret = 0;
  //  client_proto_priv_t *priv = trans->xl_private;

  if (event & (POLLIN|POLLPRI)) {
    gf_block_t *blk;

    blk = gf_block_unserialize_transport (trans);
    if (!blk) {
      ret = -1;
    }

    if (!ret) {
      ret = client_protocol_interpret (trans, blk);

      //      free (blk->data);
      free (blk);
    }
  }

  if (ret || (event & (POLLERR|POLLHUP))) {
    client_protocol_cleanup (trans);
  }

  return ret;
}

/*
 * client_protocol_cleanup - cleanup function
 * @trans: transport object
 *
 */
static int32_t 
client_protocol_cleanup (transport_t *trans)
{
  client_proto_priv_t *priv = trans->xl_private;
  glusterfs_ctx_t *ctx = trans->xl->ctx;
  dict_t *saved_frames = NULL;

  gf_log ("protocol/client",
	  GF_LOG_DEBUG,
	  "cleaning up state in transport object %p",
	  trans);

  pthread_mutex_lock (&priv->lock);
  saved_frames = priv->saved_frames;
  priv->saved_frames = get_new_dict ();

  {
    data_pair_t *trav = (priv->saved_fds)->members_list;
    xlator_t *this = trans->xl;

    while (trav) {
      fd_t *tmp = (fd_t *)(long) strtoul (trav->key, NULL, 0);
      if (tmp->ctx)
	dict_del (tmp->ctx, this->name);
      trav = trav->next;
    }

    dict_destroy (priv->saved_fds);
    priv->saved_fds = get_new_dict ();
  }
  pthread_mutex_unlock (&priv->lock);

  {
    data_pair_t *trav = saved_frames->members_list;
    while (trav) {
      /* TODO: reply functions are different for different fops. */
      call_frame_t *tmp = (call_frame_t *) (trav->value->data);
      client_local_t *local = tmp->local;

      if (local->timer) {
	gf_timer_call_cancel (ctx, local->timer);
	local->timer = NULL;
      }

      STACK_UNWIND (tmp, -1, ENOTCONN, 0, 0);
      trav = trav->next;
    }

    dict_destroy (saved_frames);
  }

  return 0;
}

typedef int32_t (*gf_op_t) (call_frame_t *frame,
			    dict_t *args);

static gf_op_t gf_fops[] = {
  client_stat_cbk,
  client_readlink_cbk,
  client_mknod_cbk,
  client_mkdir_cbk,
  client_unlink_cbk,
  client_rmdir_cbk,
  client_symlink_cbk,
  client_rename_cbk,
  client_link_cbk,
  client_chmod_cbk,
  client_chown_cbk,
  client_truncate_cbk,
  client_open_cbk,
  client_readv_cbk,
  client_write_cbk,
  client_statfs_cbk,
  client_flush_cbk,
  client_close_cbk,
  client_fsync_cbk,
  client_setxattr_cbk,
  client_getxattr_cbk,
  client_listxattr_cbk,
  client_removexattr_cbk,
  client_opendir_cbk,
  client_readdir_cbk,
  client_closedir_cbk,
  client_fsyncdir_cbk,
  client_access_cbk,
  client_create_cbk,
  client_ftruncate_cbk,
  client_fstat_cbk,
  client_lk_cbk,
  client_utimens_cbk,
  client_fchmod_cbk,
  client_fchown_cbk,
  client_lookup_cbk,
  client_forget_cbk,
  client_writedir_cbk
};

static gf_op_t gf_mops[] = {
  client_setvolume_cbk,
  client_getvolume_cbk,
  client_stats_cbk,
  client_setspec_cbk,
  client_getspec_cbk,
  client_lock_cbk,
  client_unlock_cbk,
  client_listlocks_cbk,
  client_fsck_cbk
};

/*
 * client_protocol_interpret - protocol interpreter
 * @trans: transport object
 * @blk: data block
 *
 */
static int32_t
client_protocol_interpret (transport_t *trans,
			   gf_block_t *blk)
{
  int32_t ret = 0;
  dict_t *args = blk->dict;
  call_frame_t *frame = NULL;
  client_local_t *local = NULL;

  frame = lookup_frame (trans, blk->callid);
  if (!frame) {
    gf_log ("protocol/client",
	    GF_LOG_DEBUG,
	    "frame not found for blk with callid: %d",
	    blk->callid);
    return -1;
  }
  frame->root->rsp_refs = dict_ref (args);
  local = frame->local;
  dict_set (args, NULL, trans->buf);

  /* TODO: each fop needs to allocate client_local_t and set frame->local to point to it */

  if (local->timer) {
    gf_timer_call_cancel (trans->xl->ctx, local->timer);
    local->timer = NULL;
  }

  switch (blk->type) {
  case GF_OP_TYPE_FOP_REPLY:
    {
      if (blk->op > GF_FOP_MAXVALUE || blk->op < 0) {
	gf_log ("protocol/client",
		GF_LOG_DEBUG,
		"invalid opcode '%d'",
		blk->op);
	ret = -1;
	break;
      }
      
      gf_fops[blk->op] (frame, args);
      
      break;
    }
  case GF_OP_TYPE_MOP_REPLY:
    {
      if (blk->op > GF_MOP_MAXVALUE || blk->op < 0)
	return -1;
      
      gf_mops[blk->op] (frame, args);
     
      break;
    }
  default:
    gf_log ("protocol/client",
	    GF_LOG_DEBUG,
	    "invalid packet type: %d",
	    blk->type);
    ret = -1;
  }

  dict_unref (args);
  return 0;
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
  size_t lru_limit = 1000;
  data_t *timeout = NULL;
  int32_t transport_timeout = 0;
  data_t *lru_data = NULL;

  if (this->children) {
    gf_log ("protocol/client",
	    GF_LOG_ERROR,
	    "FATAL: client protocol translator cannot have subvolumes");
    return -1;
  }

  if (!dict_get (this->options, "transport-type")) {
    gf_log ("protocol/client",
	    GF_LOG_DEBUG,
	    "missing 'option transport-type'. defaulting to \"tcp/client\"");
    dict_set (this->options,
	      "transport-type",
	      str_to_data ("tcp/client"));
  }

  if (!dict_get (this->options, "remote-subvolume")) {
    gf_log ("protocol/client",
	    GF_LOG_ERROR,
	    "missing 'option remote-subvolume'.");
    return -1;
  }
  
  lru_data = dict_get (this->options, "inode-lru-limit");
  if (!lru_data){
    gf_log ("protocol/client",
	    GF_LOG_DEBUG,
	    "missing 'inode-lru-limit'. defaulting to 1000");
    dict_set (this->options,
	      "inode-lru-limit",
	      data_from_uint64 (lru_limit));
  } else {
    lru_limit = data_to_uint64 (lru_data);
  }
    

  timeout = dict_get (this->options, "transport-timeout");
  if (timeout) {
    transport_timeout = data_to_int32 (timeout);
    gf_log ("protocol/client",
	    GF_LOG_DEBUG,
	    "setting transport-timeout to %d", transport_timeout);
  }
  else {
    gf_log ("protocol/client",
	    GF_LOG_DEBUG,
	    "defaulting transport-timeout to 120");
    transport_timeout = 120;
  }

  trans = transport_load (this->options, 
			  this,
			  client_protocol_notify);
  if (!trans)
    return -1;

  this->private = transport_ref (trans);
  priv = calloc (1, sizeof (client_proto_priv_t));
  priv->saved_frames = get_new_dict ();
  priv->saved_fds = get_new_dict ();
  this->itable = inode_table_new (lru_limit, this->name);
  priv->table = this->itable;
  priv->callid = 1;
  priv->transport_timeout = transport_timeout;
  pthread_mutex_init (&priv->lock, NULL);
  trans->xl_private = priv;

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
  return;
}

struct xlator_fops fops = {
  .stat        = client_stat,
  .readlink    = client_readlink,
  .mknod       = client_mknod,
  .mkdir       = client_mkdir,
  .unlink      = client_unlink,
  .rmdir       = client_rmdir,
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
  .close       = client_close,
  .fsync       = client_fsync,
  .setxattr    = client_setxattr,
  .getxattr    = client_getxattr,
  .listxattr   = client_listxattr,
  .removexattr = client_removexattr,
  .opendir     = client_opendir,
  .readdir     = client_readdir,
  .closedir    = client_closedir,
  .fsyncdir    = client_fsyncdir,
  .access      = client_access,
  .ftruncate   = client_ftruncate,
  .fstat       = client_fstat,
  .create      = client_create,
  .lk          = client_lk,
  .lookup      = client_lookup,
  .forget      = client_forget,
  .fchmod      = client_fchmod,
  .fchown      = client_fchown,
  .writedir    = client_writedir,
};

struct xlator_mops mops = {
  .stats     = client_stats,
  .lock      = client_lock,
  .unlock    = client_unlock,
  .listlocks = client_listlocks,
  .getspec   = client_getspec
};
