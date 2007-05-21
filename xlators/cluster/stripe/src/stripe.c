/*
  (C) 2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
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

/**
 * xlators/cluster/stripe:
 *    Stripe translator, stripes the data accross its child nodes, 
 *    as per the options given in the spec file. The striping works 
 *    fairly simple. It writes files at different offset as per 
 *    calculation. So, 'ls -l' output at the real posix level will 
 *    show file size bigger than the actual size. But when one does 
 *    'df' or 'du <file>', real size of the file on the server is shown.
 *
 * WARNING:
 *    Stripe translator can't regenerate data if a child node gets disconnected.
 *    So, no 'self-heal' for stripe. Hence the advice, use stripe only when its 
 *    very much necessary, or else, use it in combination with AFR, to have a 
 *    backup copy. 
 */

#include "xlator.h"
#include "logging.h"
#include <fnmatch.h>


#define LOCK_INIT(x)    pthread_mutex_init (x, NULL);
#define LOCK(x)         pthread_mutex_lock (x);
#define UNLOCK(x)       pthread_mutex_unlock (x);
#define LOCK_DESTROY(x) pthread_mutex_destroy (x);

#define stripe_map_inode(itable, inode)  (inode)  /* TODO: temp */

struct stripe_local;

/**
 * struct stripe_options : This keeps the pattern and the block-size 
 * information, which is used for striping on a file.
 */
struct stripe_options {
  struct stripe_options *next;
  char path_pattern[256];
  int32_t block_size;
};

/**
 * Private structure for stripe translator 
 */
struct stripe_private {
  struct stripe_options *pattern;
  int32_t child_count;
};

/**
 * Used to keep info about the replies received from fops->readv calls 
 */
struct readv_replies {
  struct iovec *vector;
  int32_t count; //count of vector
  int32_t op_ret;   //op_ret of readv
  int32_t op_errno;
};

/**
 * Local structure to be passed with all the frames in case of STACK_WIND
 */
struct stripe_local {
  struct stripe_local *next;
  call_frame_t *orig_frame; //

  /* Used by _cbk functions */
  int32_t call_count;
  int32_t wind_count; // used instead of child_cound in case of read and write */
  int32_t op_ret;
  int32_t op_errno; 
  int32_t count;
  int32_t unwind;
  struct stat stbuf;
  struct flock lock;
  struct readv_replies *replies;
  struct statvfs statvfs_buf;
  dir_entry_t *entry;
  struct xlator_stats stats;
  inode_t *inode;

  /* For File I/O fops */
  dict_t *ctx;

  /* General usage */
  off_t offset;
  off_t stripe_size;
  int32_t node_index;

  int32_t failed;
};

typedef struct stripe_local   stripe_local_t;
typedef struct stripe_private stripe_private_t;


/**
 * stripe_get_matching_bs - Get the matching block size for the given path
 */
static int32_t 
stripe_get_matching_bs (const char *path, struct stripe_options *opts) 
{
  struct stripe_options *trav = opts;
  char *filename = strdup (path);
  char *file = basename (filename);
  while (trav) {
    if (fnmatch (trav->path_pattern, file, FNM_PATHNAME) == 0) {
      free (filename);
      return trav->block_size;
    }
    trav = trav->next;
  }
  free (filename);
  return 0;
}


/**
 * stripe_stack_unwind_cbk -  This function is used for all the _cbk without 
 *     any extra arguments (other than the minimum given)
 */
static int32_t 
stripe_stack_unwind_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
  int32_t callcnt = 0;
  stripe_local_t *local = frame->local;

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == -1) {
    if (op_errno == ENOTCONN) {
      local->failed = 1;
    } else {
      local->op_ret = -1;
      local->op_errno = op_errno;
    }
  }

  if (!callcnt) {
    if (local->failed) {
      local->op_ret = -1;
      local->op_errno = EIO; /* TODO: Or should it be ENOENT? */
    }
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

/**
 * stripe_stack_unwind_buf_cbk -  This function is used for all the _cbk with 
 *    'struct stat *buf' as extra argument (other than minimum)
 *
 * @cookie - this argument should be always 'xlator_t *' of child node 
 */
static int32_t 
stripe_stack_unwind_buf_cbk (call_frame_t *frame,
			     void *cookie,
			     xlator_t *this,
			     int32_t op_ret,
			     int32_t op_errno,
			     struct stat *buf)
{
  int32_t callcnt = 0;
  stripe_local_t *local = frame->local;
  xlator_t *first_child = this->children->xlator;

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == -1) {
    if (op_errno == ENOTCONN) {
      local->failed = 1;
    } else {
      local->op_ret = -1;
      local->op_errno = op_errno;
    }
  }

  if (op_ret == 0) {
    if (local->stbuf.st_blocks == 0)
      local->stbuf = *buf;
    if (first_child == (xlator_t *)cookie) {
      /* Always, pass the inode number of first child to the above layer */
      local->stbuf.st_ino = buf->st_ino;
    }
    LOCK (&frame->mutex);
    if (local->stbuf.st_size < buf->st_size)
      local->stbuf.st_size = buf->st_size;
    local->stbuf.st_blocks += buf->st_blocks;
    if (local->stbuf.st_blksize != buf->st_blksize) {
      /* TODO: add to blocks in terms of original block size */
    }
    UNLOCK (&frame->mutex);
  }

  if (!callcnt) {
    if (local->failed) {
      local->op_ret = -1;
      local->op_errno = EIO; /* TODO: Or should it be ENOENT? */
    }
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

/**
 * stripe_lookup_cbk - 
 */
static int32_t 
stripe_lookup_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
		   struct stat *buf)
{
  int32_t callcnt = 0;
  stripe_local_t *local = frame->local;
  xlator_t *first_child = this->children->xlator;

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == -1) {
    if (op_errno == ENOTCONN) {
      local->failed = 1;
    } else {
      local->op_ret = -1;
      local->op_errno = op_errno;
    }
  }
 
  if (op_ret == 0) {
    /* Get the stat buf right */
    local->inode = stripe_map_inode (this->itable, inode);

    if (local->stbuf.st_blocks == 0)
      local->stbuf = *buf;

    if (first_child == (xlator_t *)cookie) {
      /* Always, pass the inode number of first child to the above layer */
      local->stbuf.st_ino = buf->st_ino;
    }

    LOCK (&frame->mutex);
    if (local->stbuf.st_size < buf->st_size)
      local->stbuf.st_size = buf->st_size;
    local->stbuf.st_blocks += buf->st_blocks;
    if (local->stbuf.st_blksize != buf->st_blksize) {
      /* TODO: add to blocks in terms of original block size */
    }
    UNLOCK (&frame->mutex);
  }

  if (!callcnt) {
    if (local->failed) {
      local->op_ret = -1;
      local->op_errno = EIO; /* TODO: Or should it be ENOENT? */
    }
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->inode, &local->stbuf);
  }
  return 0;
}

int32_t 
stripe_lookup (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
  stripe_local_t *local = NULL;
  xlator_list_t *trav = NULL;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  /* Everytime in stripe lookup, all child nodes should be looked up */
  trav = this->children;
  while (trav) {
    _STACK_WIND (frame,
		 stripe_lookup_cbk,
		 trav->xlator,  /* cookie */
		 trav->xlator,
		 trav->xlator->fops->lookup,
		 loc);
    trav = trav->next;
  }
  
  return 0;
}

int32_t 
stripe_forget (call_frame_t *frame,
	       xlator_t *this,
	       inode_t *inode)
{
  STACK_WIND (frame,
	      stripe_stack_unwind_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->forget,
	      inode);
  return 0;
}

int32_t
stripe_stat (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
  _STACK_WIND (frame,
	       stripe_stack_unwind_buf_cbk,
	       FIRST_CHILD(this),
	       FIRST_CHILD(this),
	       FIRST_CHILD(this)->fops->stat,
	       loc);
  return 0;
}

int32_t
stripe_chmod (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode)
{
  _STACK_WIND (frame,
	       stripe_stack_unwind_buf_cbk,
	       FIRST_CHILD(this),
	       FIRST_CHILD(this),
	       FIRST_CHILD(this)->fops->chmod,
	       loc,
	       mode);
  return 0;
}


int32_t 
stripe_fchmod (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       mode_t mode)
{
  _STACK_WIND (frame,
	       stripe_stack_unwind_buf_cbk,
	       FIRST_CHILD(this),
	       FIRST_CHILD(this),
	       FIRST_CHILD(this)->fops->fchmod,
	       fd,
	       mode);
  return 0;
}


int32_t
stripe_chown (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      uid_t uid,
	      gid_t gid)
{
  _STACK_WIND (frame,	      
	       stripe_stack_unwind_buf_cbk,
	       FIRST_CHILD(this),
	       FIRST_CHILD(this),
	       FIRST_CHILD(this)->fops->chown,
	       loc,
	       uid,
	       gid);
  return 0;
}

int32_t 
stripe_fchown (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       uid_t uid,
	       gid_t gid)
{
  _STACK_WIND (frame,	      
	       stripe_stack_unwind_buf_cbk,
	       FIRST_CHILD(this),
	       FIRST_CHILD(this),
	       FIRST_CHILD(this)->fops->fchown,
	       fd,
	       uid,
	       gid);
  return 0;
}

int32_t
stripe_truncate (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 off_t offset)
{
  _STACK_WIND (frame,
	       stripe_stack_unwind_buf_cbk,
	       FIRST_CHILD(this),
	       FIRST_CHILD(this),
	       FIRST_CHILD(this)->fops->truncate,
	       loc,
	       offset);
  return 0;
}

int32_t
stripe_ftruncate (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  off_t offset)
{
  _STACK_WIND (frame,
	       stripe_stack_unwind_buf_cbk,
	       FIRST_CHILD(this),
	       FIRST_CHILD(this),
	       FIRST_CHILD(this)->fops->ftruncate,
	       fd,
	       offset);
  return 0;
}


int32_t 
stripe_utimens (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		struct timespec tv[2])
{
  _STACK_WIND (frame,
	       stripe_stack_unwind_buf_cbk,
	       FIRST_CHILD(this),
	       FIRST_CHILD(this),
	       FIRST_CHILD(this)->fops->utimens,
	       loc,
	       tv);
  return 0;
}

int32_t
stripe_access (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       int32_t mask)
{
  STACK_WIND (frame,
	      stripe_stack_unwind_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->access,
	      loc,
	      mask);
  return 0;
}

int32_t
stripe_readlink (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 size_t size)
{
  _STACK_WIND (frame,
	       stripe_stack_unwind_buf_cbk,
	       FIRST_CHILD(this),
	       FIRST_CHILD(this),
	       FIRST_CHILD(this)->fops->readlink,
	       loc,
	       size);
  return 0;
}


static int32_t
stripe_mknod_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
		  struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		inode,
		buf);
  return 0;
}

int32_t
stripe_mknod (call_frame_t *frame,
	      xlator_t *this,
	      const char *name,
	      mode_t mode,
	      dev_t rdev)
{
  STACK_WIND (frame,
	      stripe_mknod_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->mknod,
	      name,
	      mode,
	      rdev);
  return 0;
}

static int32_t
stripe_mkdir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
		  struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		inode,
		buf);
  return 0;
}

int32_t
stripe_mkdir (call_frame_t *frame,
	      xlator_t *this,
	      const char *name,
	      mode_t mode)
{
  STACK_WIND (frame,
	      stripe_mkdir_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->mkdir,
	      name,
	      mode);
  return 0;
}


int32_t
stripe_unlink (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
  STACK_WIND (frame,
	      stripe_stack_unwind_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->unlink,
	      loc);
  return 0;
}


int32_t
stripe_rmdir (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  STACK_WIND (frame,
	      stripe_stack_unwind_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->rmdir,
	      loc);
  return 0;
}

static int32_t
stripe_symlink_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		inode,
		buf);
  return 0;
}

int32_t
stripe_symlink (call_frame_t *frame,
		xlator_t *this,
		const char *linkpath,
		const char *name)
{
  STACK_WIND (frame,
	      stripe_symlink_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->symlink,
	      linkpath,
	      name);
  return 0;
}

int32_t
stripe_rename (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *oldloc,
	       loc_t *newloc)
{
  _STACK_WIND (frame,
	       stripe_stack_unwind_buf_cbk,
	       FIRST_CHILD(this),
	       FIRST_CHILD(this),
	       FIRST_CHILD(this)->fops->rename,
	       oldloc,
	       newloc);
  return 0;
}


static int32_t
stripe_link_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		buf);
  return 0;
}

int32_t
stripe_link (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     const char *newname)
{
  STACK_WIND (frame,
	      stripe_link_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->link,
	      loc,
	      newname);
  return 0;
}


static int32_t
stripe_create_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   fd_t *fd,
		   inode_t *inode,
		   struct stat *buf)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		fd,
		inode,
		buf);
  return 0;
}

int32_t
stripe_create (call_frame_t *frame,
	       xlator_t *this,
	       const char *name,
	       int32_t flags,
	       mode_t mode)
{
  STACK_WIND (frame,
	      stripe_create_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->create,
	      name,
	      flags,
	      mode);
  return 0;
}

static int32_t
stripe_open_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 fd_t *fd)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		fd);
  return 0;
}

int32_t
stripe_open (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     int32_t flags)
{
  STACK_WIND (frame,
	      stripe_open_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->open,
	      loc,
	      flags);
  return 0;
}

static int32_t
stripe_readv_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct iovec *vector,
		  int32_t count)
{
  stripe_local_t *local = frame->local;
  call_frame_t *main_frame = local->orig_frame;
  stripe_local_t *main_local = main_frame->local;
  int32_t callcnt;
  int32_t index = local->node_index;


  LOCK (&main_frame->mutex);

  main_local->replies[index].op_ret = op_ret;
  main_local->replies[index].op_errno = op_errno;
  if (op_ret > 0) {
    main_local->replies[index].count  = count;
    main_local->replies[index].vector = iov_dup (vector, count);
    dict_copy (frame->root->rsp_refs, main_frame->root->rsp_refs);
  }
  callcnt = ++main_local->call_count;

  UNLOCK(&main_frame->mutex);

  if (callcnt == main_local->wind_count) {
    dict_t *refs = main_frame->root->rsp_refs;
    int32_t index = 0;
    int32_t final_count = 0;
    struct iovec *final_vec = NULL;

    op_ret = 0;
    for (index=0; index < main_local->wind_count; index++) {
      /* TODO: check whether each stripe returned 'expected'
       * number of bytes 
       */
      if (main_local->replies[index].op_ret == -1) {
        op_ret = -1;
        op_errno = main_local->replies[index].op_errno;
        break;
      }
      op_ret += main_local->replies[index].op_ret;
      final_count += main_local->replies[index].count;
    }

    if (op_ret != -1) {
      final_vec = calloc (final_count, sizeof (struct iovec));
      final_count = 0;

      for (index=0; index < main_local->wind_count; index++) {
        memcpy (final_vec + final_count,
                main_local->replies[index].vector,
                main_local->replies[index].count * sizeof (struct iovec));
        final_count += main_local->replies[index].count;

        free (main_local->replies[index].vector);
      }
    } else {
      final_vec = NULL;
      final_count = 0;
    }

    refs = main_frame->root->rsp_refs;
    LOCK_DESTROY (&main_frame->mutex);
    STACK_UNWIND (main_frame, op_ret, op_errno, final_vec, final_count);

    dict_unref (refs);
    free (final_vec);
  }

  STACK_DESTROY (frame->root);
  return 0;
}

int32_t
stripe_readv (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      size_t size,
	      off_t offset)
{
  stripe_local_t *local = calloc (1, sizeof (stripe_local_t));

  off_t stripe_size = data_to_int64 (dict_get (fd->ctx,
					       frame->this->name));
  off_t rounded_start = floor (offset, stripe_size);
  off_t rounded_end = roof (offset+size, stripe_size);
  int32_t num_stripe = (rounded_end - rounded_start) / stripe_size;
  xlator_list_t *trav = this->children;
  int32_t index = 0;
  off_t frame_offset = offset;
  size_t frame_size;

  LOCK_INIT (&frame->mutex);
  local->wind_count = num_stripe;
  frame->local = local;
  frame->root->rsp_refs = dict_ref (get_new_dict ());

  /* This is where all the vectors should be copied. */
  local->replies = calloc (1, num_stripe * sizeof (struct readv_replies));

  for (index = 0; index < num_stripe; index++) {
    call_frame_t *rframe = copy_frame (frame);
    stripe_local_t *rlocal = calloc (1, sizeof (stripe_local_t));
    data_t *ctx_data = dict_get (fd->ctx, trav->xlator->name);
    fd_t *ctx = (void *)data_to_ptr(ctx_data);

    frame_size = min (roof (frame_offset+1, stripe_size),
                      (offset + size)) - frame_offset;

    rlocal->node_index = index;
    rlocal->orig_frame = frame;
    rframe->local = rlocal;

    STACK_WIND (rframe,
                stripe_readv_cbk,
                trav->xlator,
                trav->xlator->fops->readv,
                ctx,
                frame_size,
                frame_offset);

    frame_offset += frame_size;
    trav = trav->next ? trav->next : this->children;
  }

  return 0;
}


static int32_t
stripe_writev_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
  int32_t callcnt = 0;
  stripe_local_t *local = frame->local;

  LOCK(&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == -1) {
    local->op_errno = op_errno;
    local->op_ret = -1;
  }
  if (op_ret >= 0) {
    LOCK (&frame->mutex);
    local->op_ret += op_ret;
    UNLOCK (&frame->mutex);
  }

  if ((callcnt == local->wind_count) && local->unwind) {
    LOCK_DESTROY(&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

int32_t
stripe_writev (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       struct iovec *vector,
	       int32_t count,
	       off_t offset)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  int32_t total_size = 0;
  int32_t i = 0;
  int32_t tmp_count = count;
  struct iovec *tmp_vec = vector;
  stripe_private_t *priv = this->private;

  local->stripe_size = data_to_int64 (dict_get (fd->ctx, frame->this->name));

  for (i = 0; i< count; i++) {
    total_size += tmp_vec[i].iov_len;
  }
  frame->local = local;

  LOCK_INIT (&frame->mutex);

  int32_t offset_offset = 0;
  int32_t remaining_size = total_size;
  int32_t fill_size = 0;
  while (1) {
    xlator_list_t *trav = this->children;

    if (local->stripe_size) {
      int32_t idx = ((offset + offset_offset) / local->stripe_size) % priv->child_count;
      while (idx) {
        trav = trav->next;
        idx--;
      }
      fill_size = local->stripe_size - (offset % local->stripe_size);
      if (fill_size > remaining_size)
        fill_size = remaining_size;
      remaining_size -= fill_size;
    } else {
      fill_size = total_size;
      remaining_size = 0;
    }
    tmp_count = iov_subset (vector, count, offset_offset,
                            offset_offset + fill_size, NULL);
    tmp_vec = calloc (tmp_count, sizeof (struct iovec));
    tmp_count = iov_subset (vector, count, offset_offset,
			    offset_offset + fill_size, tmp_vec);

    data_t *ctx_data = dict_get (fd->ctx, trav->xlator->name);
    fd_t *ctx = (void *)data_to_ptr (ctx_data);
    local->wind_count++;
    if (remaining_size == 0)
      local->unwind = 1;
    STACK_WIND(frame,
               stripe_writev_cbk,
               trav->xlator,
               trav->xlator->fops->writev,
               ctx,
               tmp_vec,
               tmp_count,
               offset + offset_offset);
    offset_offset += fill_size;
    if (remaining_size == 0)
      break;
  }

  return 0;
}


int32_t
stripe_flush (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  STACK_WIND (frame,
	      stripe_stack_unwind_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->flush,
	      fd);
  return 0;
}


int32_t
stripe_close (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  STACK_WIND (frame,
	      stripe_stack_unwind_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->close,
	      fd);
  return 0;
}

int32_t
stripe_fsync (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t flags)
{
  STACK_WIND (frame,
	      stripe_stack_unwind_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->fsync,
	      fd,
	      flags);
  return 0;
}

int32_t
stripe_fstat (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  _STACK_WIND (frame,
	       stripe_stack_unwind_buf_cbk,
	       FIRST_CHILD(this),
	       FIRST_CHILD(this),
	       FIRST_CHILD(this)->fops->fstat,
	       fd);
  return 0;
}

static int32_t
stripe_opendir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    fd_t *fd)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		fd);
  return 0;
}

int32_t
stripe_opendir (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc)
{
  STACK_WIND (frame,
	      stripe_opendir_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->opendir,
	      loc);
  return 0;
}


static int32_t
stripe_readdir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    dir_entry_t *entries,
		    int32_t count)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		entries,
		count);
  return 0;
}

int32_t
stripe_readdir (call_frame_t *frame,
		xlator_t *this,
		size_t size,
		off_t offset,
		fd_t *fd)
{
  STACK_WIND (frame,
	      stripe_readdir_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->readdir,
	      size,
	      offset,
	      fd);
  return 0;
}

int32_t
stripe_closedir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd)
{
  STACK_WIND (frame,
	      stripe_stack_unwind_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->closedir,
	      fd);
  return 0;
}

int32_t
stripe_fsyncdir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 int32_t flags)
{
  STACK_WIND (frame,
	      stripe_stack_unwind_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->fsyncdir,
	      fd,
	      flags);
  return 0;
}


static int32_t
stripe_statfs_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct statvfs *buf)
{
  stripe_local_t *local = (stripe_local_t *)frame->local;
  int32_t callcnt;
  LOCK(&frame->mutex);
  callcnt = ++local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  if (op_ret == 0) {
    LOCK(&frame->mutex);
    struct statvfs *dict_buf = &local->statvfs_buf;
    dict_buf->f_bsize   = stbuf->f_bsize;
    dict_buf->f_frsize  = stbuf->f_frsize;
    dict_buf->f_blocks += stbuf->f_blocks;
    dict_buf->f_bfree  += stbuf->f_bfree;
    dict_buf->f_bavail += stbuf->f_bavail;
    dict_buf->f_files  += stbuf->f_files;
    dict_buf->f_ffree  += stbuf->f_ffree;
    dict_buf->f_favail += stbuf->f_favail;
    dict_buf->f_fsid    = stbuf->f_fsid;
    dict_buf->f_flag    = stbuf->f_flag;
    dict_buf->f_namemax = stbuf->f_namemax;
    local->op_ret = 0;
    UNLOCK (&frame->mutex);
  }
  if (callcnt == ((stripe_private_t *)this->private)->child_count) {
    LOCK_DESTROY(&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->statvfs_buf);
  }

  return 0;
}

int32_t
stripe_statfs (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
  stripe_local_t *local = calloc (1, sizeof (stripe_local_t)); 
  xlator_list_t *trav = this->children;

  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  LOCK_INIT (&frame->mutex);
  frame->local = local;

  while (trav) {
    STACK_WIND (frame,
                stripe_statfs_cbk,
                trav->xlator,
                trav->xlator->fops->statfs,
                path);
    trav = trav->next;
  }

  return 0;
}

int32_t
stripe_setxattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 const char *name,
		 const char *value,
		 size_t size,
		 int32_t flags)
{
  STACK_WIND (frame,
	      stripe_stack_unwind_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->setxattr,
	      loc,
	      name,
	      value,
	      size,
	      flags);
  return 0;
}

static int32_t
stripe_getxattr_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      void *value)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		value);
  return 0;
}

int32_t
stripe_getxattr (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  const char *name,
		  size_t size)
{
  STACK_WIND (frame,
	      stripe_getxattr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->getxattr,
	      loc,
	      name,
	      size);
  return 0;
}

static int32_t
stripe_listxattr_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       void *value)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		value);
  return 0;
}

int32_t
stripe_listxattr (call_frame_t *frame,
		   xlator_t *this,
		   loc_t *loc,
		   size_t size)
{
  STACK_WIND (frame,
	      stripe_listxattr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->listxattr,
	      loc,
	      size);
  return 0;
}

int32_t
stripe_removexattr (call_frame_t *frame,
		     xlator_t *this,
		     loc_t *loc,
		     const char *name)
{
  STACK_WIND (frame,
	      stripe_stack_unwind_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->removexattr,
	      loc,
	      name);
  return 0;
}

static int32_t
stripe_lk_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct flock *lock)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		lock);
  return 0;
}

int32_t
stripe_lk (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    int32_t cmd,
	    struct flock *lock)
{
  STACK_WIND (frame,
	      stripe_lk_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->lk,
	      fd,
	      cmd,
	      lock);
  return 0;
}


/* Management operations */

static int32_t
stripe_stats_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct xlator_stats *stats)
{
  int32_t callcnt = 0;
  stripe_local_t *local = frame->local;

  LOCK(&frame->mutex);
  callcnt = ++local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == -1) {
    local->op_ret = -1;
    local->op_errno = op_errno;
  }
  if (op_ret == 0) {
    LOCK (&frame->mutex);
    if (local->op_ret == -2) {
      /* This is to make sure this is the frist time */
      local->stats = *stats;
      local->op_ret = 0;
    } else {
      local->stats.nr_files += stats->nr_files;
      local->stats.free_disk += stats->free_disk;
      local->stats.disk_usage += stats->disk_usage;
      local->stats.nr_clients += stats->nr_clients;
    }
    UNLOCK (&frame->mutex);
  }

  if (callcnt == ((stripe_private_t*)this->private)->child_count) {
    LOCK_DESTROY(&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stats);
  }

  return 0;
}

int32_t
stripe_stats (call_frame_t *frame,
	       xlator_t *this,
	       int32_t flags)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = this->children;
  frame->local = local;
  local->op_ret = -2; /* ugly */
  LOCK_INIT (&frame->mutex);

  while (trav) {
    STACK_WIND (frame,
                stripe_stats_cbk,
                trav->xlator,
                trav->xlator->mops->stats,
                flags);
    trav = trav->next;
  }
  return 0;
}


int32_t
stripe_fsck (call_frame_t *frame,
	     xlator_t *this,
	     int32_t flags)
{
  return 0;
}

/**
 * init - This function is called when xlator-graph gets initialized. 
 *     The option given in spec files are parsed here.
 * @this - 
 */
int32_t
init (xlator_t *this)
{
  stripe_private_t *priv = NULL;
  xlator_list_t *trav = NULL;
  data_t *stripe_data = NULL;
  int32_t count = 0;
  
  trav = this->children;
  while (trav) {
    count++;
    trav = trav->next;
  }

  if (!count) {
    gf_log (this->name, 
	    GF_LOG_ERROR,
	    "stripe configured without \"subvolumes\" option. exiting");
    return -1;
  }
  priv = calloc (1, sizeof (stripe_private_t));
  priv->child_count = count;

  /* option stripe-pattern *avi:1GB,*pdf:4096 */
  stripe_data = dict_get (this->options, "block-size");
  if (!stripe_data) {
    gf_log (this->name, 
	    GF_LOG_WARNING,
	    "No stripe pattern specified. check \"option block-size <x>\" in spec file");
  } else {
    char *tmp_str = NULL;
    char *tmp_str1 = NULL;
    char *dup_str = NULL;
    char *stripe_str = NULL;
    char *pattern = NULL;
    char *num = NULL;
    struct stripe_options *temp_stripeopt = NULL;
    struct stripe_options *stripe_opt = NULL;    
    /* Get the pattern for striping. "option block-size *avi:10MB" etc */
    stripe_str = strtok_r (stripe_data->data, ",", &tmp_str);
    while (stripe_str) {
      dup_str = strdup (stripe_str);
      stripe_opt = calloc (1, sizeof (struct stripe_options));
      pattern = strtok_r (dup_str, ":", &tmp_str1);
      num = strtok_r (NULL, ":", &tmp_str1);
      memcpy (stripe_opt->path_pattern, pattern, strlen (pattern));
      if (num) 
	stripe_opt->block_size = gf_str_to_long_long (num);
      else {
	stripe_opt->block_size = gf_str_to_long_long ("128KB");
      }
      gf_log (this->name, 
	      GF_LOG_DEBUG, 
	      "stripe block size : pattern %s : size %d", 
	      stripe_opt->path_pattern, 
	      stripe_opt->block_size);
      if (!priv->pattern) {
	priv->pattern = stripe_opt;
      } else {
	temp_stripeopt = priv->pattern;
	while (temp_stripeopt->next)
	  temp_stripeopt = temp_stripeopt->next;
	temp_stripeopt->next = stripe_opt;
      }
      stripe_str = strtok_r (NULL, ",", &tmp_str);
    }
  }
  this->private = priv;
  
  return 0;
} 

/** 
 * fini -   Free all the private variables
 * @this - 
 */
void 
fini (xlator_t *this)
{
  struct stripe_options *prev = NULL;
  struct stripe_options *trav = ((stripe_private_t *)this->private)->pattern;
  while (trav) {
    prev = trav;
    trav = trav->next;
    free (prev);
  }
  free (this->private);
  return;
}


struct xlator_fops fops = {
  .stat        = stripe_stat,
  .unlink      = stripe_unlink,
  .symlink     = stripe_symlink,
  .rename      = stripe_rename,
  .link        = stripe_link,
  .chmod       = stripe_chmod,
  .chown       = stripe_chown,
  .truncate    = stripe_truncate,
  .utimens     = stripe_utimens,
  .create      = stripe_create,
  .open        = stripe_open,
  .readv       = stripe_readv,
  .writev      = stripe_writev,
  .statfs      = stripe_statfs,
  .flush       = stripe_flush,
  .close       = stripe_close,
  .fsync       = stripe_fsync,
  .setxattr    = stripe_setxattr,
  .getxattr    = stripe_getxattr,
  .listxattr   = stripe_listxattr,
  .removexattr = stripe_removexattr,
  .access      = stripe_access,
  .ftruncate   = stripe_ftruncate,
  .fstat       = stripe_fstat,
  .readdir     = stripe_readdir,
  .readlink    = stripe_readlink,
  .mkdir       = stripe_mkdir,
  .rmdir       = stripe_rmdir,
  .lk          = stripe_lk,
  .closedir    = stripe_closedir,
  .opendir     = stripe_opendir,
  .fsyncdir    = stripe_fsyncdir,
  .fchmod      = stripe_fchmod,
  .fchown      = stripe_fchown,
  .lookup      = stripe_lookup,
  .forget      = stripe_forget,
};

struct xlator_mops mops = {
  .stats  = stripe_stats,
  //.lock   = stripe_lock,
  //.unlock = stripe_unlock,
};
