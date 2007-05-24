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


#define LOCK_INIT(x)    pthread_mutex_init (x, NULL)
#define LOCK(x)         pthread_mutex_lock (x)
#define UNLOCK(x)       pthread_mutex_unlock (x)
#define LOCK_DESTROY(x) pthread_mutex_destroy (x)

#define FIRST_NODE(xl)  (xl->children->xlator)

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
  struct list_head *list;
  struct flock *lock;
  fd_t *fd;
  void *value;
};

struct stripe_inode_list {
  struct list_head list_head;
  xlator_t *xl;
  inode_t *inode;
};


typedef struct stripe_local   stripe_local_t;
typedef struct stripe_private stripe_private_t;
typedef struct stripe_inode_list stripe_inode_list_t;

/**
 * stripe_get_matching_bs - Get the matching block size for the given path.
 */
static int32_t 
stripe_get_matching_bs (const char *path, struct stripe_options *opts) 
{
  struct stripe_options *trav = opts;
  char *pathname = strdup (path);
  while (trav) {
    if (fnmatch (trav->path_pattern, pathname, FNM_PATHNAME) == 0) {
      free (pathname);
      return trav->block_size;
    }
    trav = trav->next;
  }
  free (pathname);
  return 0;
}


/**
 * stripe_stack_unwind_cbk -  This function is used for all the _cbk without 
 *     any extra arguments (other than the minimum given)
 * This is called from functions like forget,fsync,unlink,rmdir,close,closedir etc.
 *
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
      local->op_errno = op_errno;
    }
  }
  if (op_ret == 0) 
    local->op_ret = 0;

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
 * This is called from functions like, chmod,fchmod,chown,fchown,truncate,ftruncate,
 *   utimens etc.
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
      local->op_errno = op_errno;
    }
  }

  if (op_ret == 0) {
    local->op_ret = 0;
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
 * stripe_stack_unwind_inode_cbk - This is called by the function like, 
 *                   lookup (), link (), symlink (), mkdir (), mknod () 
 * This creates a inode for new inode. It keeps a list of all the inodes received
 * from the child nodes. It is used while forwarding any fops to child nodes.
 *
 */
static int32_t 
stripe_stack_unwind_inode_cbk (call_frame_t *frame,
			       void *cookie,
			       xlator_t *this,
			       int32_t op_ret,
			       int32_t op_errno,
			       inode_t *inode,
			       struct stat *buf)
{
  int32_t callcnt = 0;
  stripe_inode_list_t *ino_list = NULL;
  stripe_local_t *local = frame->local;
  xlator_t *first_child = this->children->xlator;

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == -1) {
    if (op_errno == ENOTCONN) {
      local->failed = 1;
    } else {
      local->op_errno = op_errno;
    }
  }
 
  if (op_ret == 0) {
    local->op_ret = 0;
    /* Get the mapping in inode private */
    ino_list = calloc (1, sizeof (stripe_inode_list_t));
    ino_list->xl = (xlator_t *)cookie;
    ino_list->inode = inode;

    LOCK (&frame->mutex);
    /* Get the stat buf right */
    if (local->stbuf.st_blocks == 0) {
      local->stbuf = *buf;
      local->list = calloc (1, sizeof (struct list_head));
      INIT_LIST_HEAD (local->list);
    }

    list_add (&ino_list->list_head, local->list);

    if (first_child == (xlator_t *)cookie) {
      /* Always, pass the inode number of first child to the above layer */
      local->inode = inode_update (this->itable, NULL, NULL, buf->st_ino);
      local->stbuf.st_ino = buf->st_ino;
    }

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
    if (local->op_ret == 0) {
      local->inode->private = local->list;
    }
    local->list = NULL;
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
		 stripe_stack_unwind_inode_cbk,
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
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = inode->private;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  list_for_each_entry (ino_list, list, list_head) {
    STACK_WIND (frame,
		stripe_stack_unwind_cbk,
		ino_list->xl,
		ino_list->xl->fops->forget,
		ino_list->inode);
  }
  inode_forget (inode, 0);
  return 0;
}

int32_t
stripe_stat (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
  struct list_head *list = NULL;
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list = NULL;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  if (loc->inode) {
    list = loc->inode->private;
    list_for_each_entry (ino_list, list, list_head) {
      loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
      _STACK_WIND (frame,
		   stripe_stack_unwind_buf_cbk,
		   ino_list->xl,
		   ino_list->xl,
		   ino_list->xl->fops->stat,
		   &tmp_loc);
    }
  } else {
    xlator_list_t *trav = this->children;
    while (trav) {
      _STACK_WIND (frame,
		   stripe_stack_unwind_buf_cbk,
		   trav->xlator,
		   trav->xlator,
		   trav->xlator->fops->stat,
		   loc);
      trav = trav->next;
    }

  }
  return 0;
}

int32_t
stripe_chmod (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode)
{
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = loc->inode->private;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
    _STACK_WIND (frame,
		 stripe_stack_unwind_buf_cbk,
		 ino_list->xl,
		 ino_list->xl,
		 ino_list->xl->fops->chmod,
		 &tmp_loc,
		 mode);
  }
  return 0;
}

int32_t
stripe_chown (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      uid_t uid,
	      gid_t gid)
{
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = loc->inode->private;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
    _STACK_WIND (frame,
		 stripe_stack_unwind_buf_cbk,
		 ino_list->xl,
		 ino_list->xl,
		 ino_list->xl->fops->chown,
		 &tmp_loc,
		 uid,
		 gid);
  }

  return 0;
}


static int32_t
stripe_statfs_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct statvfs *stbuf)
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
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = loc->inode->private;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
    STACK_WIND (frame,
		stripe_statfs_cbk,
		ino_list->xl,
		ino_list->xl->fops->statfs,
		&tmp_loc);
  }

  return 0;
}

int32_t
stripe_truncate (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 off_t offset)
{
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = loc->inode->private;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
    _STACK_WIND (frame,
		 stripe_stack_unwind_buf_cbk,
		 ino_list->xl,
		 ino_list->xl,
		 ino_list->xl->fops->truncate,
		 &tmp_loc,
		 offset);
  }

  return 0;
}

int32_t 
stripe_utimens (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		struct timespec tv[2])
{
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = loc->inode->private;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
    _STACK_WIND (frame,
		 stripe_stack_unwind_buf_cbk,
		 ino_list->xl,
		 ino_list->xl,
		 ino_list->xl->fops->utimens,
		 &tmp_loc,
		 tv);
  }

  return 0;
}


int32_t
stripe_rename (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *oldloc,
	       loc_t *newloc)
{
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list1 = NULL;
  stripe_inode_list_t *ino_list2 = NULL;
  struct list_head *list1 = NULL;
  struct list_head *list2 = NULL;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;
  
  list1 = oldloc->inode->private;
  if (!newloc->inode) {
    list_for_each_entry (ino_list1, list1, list_head) {
      loc_t tmp_oldloc = {oldloc->path, ino_list1->inode->ino, ino_list1->inode};
      _STACK_WIND (frame,
		   stripe_stack_unwind_buf_cbk,
		   ino_list1->xl,
		   ino_list1->xl,
		   ino_list1->xl->fops->rename,
		   &tmp_oldloc,
		   newloc);
    }
  } else {
    list2 = newloc->inode->private;
    list_for_each_entry (ino_list1, list1, list_head) {
      list_for_each_entry (ino_list2, list2, list_head) {
	if (ino_list1->xl != ino_list2->xl)
	  continue;
	loc_t tmp_oldloc = {oldloc->path, 
			    ino_list1->inode->ino, 
			    ino_list1->inode};
	loc_t tmp_newloc = {newloc->path,
			    ino_list2->inode->ino,
			    ino_list2->inode};
	_STACK_WIND (frame,
		     stripe_stack_unwind_buf_cbk,
		     ino_list1->xl,
		     ino_list1->xl,
		     ino_list1->xl->fops->rename,
		     &tmp_oldloc,
		     &tmp_newloc);
      }
    }
  }
  return 0;
}

int32_t
stripe_access (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       int32_t mask)
{
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = loc->inode->private;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
    STACK_WIND (frame,
		stripe_stack_unwind_cbk,
		ino_list->xl,
		ino_list->xl->fops->access,
		&tmp_loc,
		mask);
  }

  return 0;
}

static int32_t 
stripe_readlink_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     const char *path)
{
  STACK_UNWIND (frame, op_ret, op_errno, path);

  return 0;
}

int32_t
stripe_readlink (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 size_t size)
{
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = loc->inode->private;

  list_for_each_entry (ino_list, list, list_head) {
    if (FIRST_NODE (this) == ino_list->xl) {
      loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
      STACK_WIND (frame,
		  stripe_readlink_cbk,
		  ino_list->xl,
		  ino_list->xl->fops->readlink,
		  &tmp_loc,
		  size);
    }
  }

  return 0;
}



int32_t
stripe_unlink (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = loc->inode->private;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
    STACK_WIND (frame,
		stripe_stack_unwind_cbk,
		ino_list->xl,
		ino_list->xl->fops->unlink,
		&tmp_loc);
  }

  return 0;
}


int32_t
stripe_rmdir (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = loc->inode->private;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
    STACK_WIND (frame,
		stripe_stack_unwind_cbk,
		ino_list->xl,
		ino_list->xl->fops->rmdir,
		&tmp_loc);
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
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = loc->inode->private;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
    STACK_WIND (frame,
		stripe_stack_unwind_cbk,
		ino_list->xl,
		ino_list->xl->fops->setxattr,
		&tmp_loc,
		name,
		value,
		size,
		flags);
  }

  return 0;
}

int32_t
stripe_mknod (call_frame_t *frame,
	      xlator_t *this,
	      const char *name,
	      mode_t mode,
	      dev_t rdev)
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
		 stripe_stack_unwind_inode_cbk,
		 trav->xlator,  /* cookie */
		 trav->xlator,
		 trav->xlator->fops->mknod,
		 name,
		 mode,
		 rdev);
    trav = trav->next;
  }

  return 0;
}

int32_t
stripe_mkdir (call_frame_t *frame,
	      xlator_t *this,
	      const char *name,
	      mode_t mode)
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
		 stripe_stack_unwind_inode_cbk,
		 trav->xlator,  /* cookie */
		 trav->xlator,
		 trav->xlator->fops->mkdir,
		 name,
		 mode);
    trav = trav->next;
  }

  return 0;
}

int32_t
stripe_symlink (call_frame_t *frame,
		xlator_t *this,
		const char *linkpath,
		const char *name)
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
		 stripe_stack_unwind_inode_cbk,
		 trav->xlator,  /* cookie */
		 trav->xlator,
		 trav->xlator->fops->symlink,
		 linkpath,
		 name);
    trav = trav->next;
  }

  return 0;
}


int32_t
stripe_link (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     const char *newname)
{
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = loc->inode->private;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  /* Everytime in stripe lookup, all child nodes should be looked up */
  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
    _STACK_WIND (frame,
		 stripe_stack_unwind_inode_cbk,
		 ino_list->xl,
		 ino_list->xl,
		 ino_list->xl->fops->link,
		 &tmp_loc,
		 newname);
  }

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
  int32_t callcnt = 0;
  stripe_inode_list_t *ino_list = NULL;
  stripe_local_t *local = frame->local;
  xlator_t *first_child = this->children->xlator;

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == -1) {
    if (op_errno == ENOTCONN) {
      local->failed = 1;
    } else {
      local->op_errno = op_errno;
    }
  }
 
  if (op_ret == 0) {
    local->op_ret = 0;
    /* Get the mapping in inode private */
    ino_list = calloc (1, sizeof (stripe_inode_list_t));
    ino_list->xl = (xlator_t *)cookie;
    ino_list->inode = inode;

    LOCK (&frame->mutex);
    /* Get the stat buf right */
    if (local->stbuf.st_blocks == 0) {
      local->stbuf = *buf;
      local->list = calloc (1, sizeof (struct list_head));
      local->fd = calloc (1, sizeof (fd_t));
      local->fd->ctx = get_new_dict ();
      INIT_LIST_HEAD (local->list);
    }

    list_add (&ino_list->list_head, local->list);
    dict_set (local->fd->ctx, (char *)cookie, int_to_data ((long)fd));

    if (first_child == (xlator_t *)cookie) {
      /* Always, pass the inode number of first child to the above layer */
      local->inode = inode_update (this->itable, NULL, NULL, buf->st_ino);
      local->stbuf.st_ino = buf->st_ino;
      local->fd->inode = local->inode;
      list_add (&local->fd->inode_list, &local->inode->fds);
    }

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
    if (local->op_ret == 0) {
      local->inode->private = local->list;
    }
    local->list = NULL;
    dict_set (local->fd->ctx, frame->this->name, data_from_int64 (local->stripe_size));
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->fd, local->inode, &local->stbuf);
  }

  return 0;
}

int32_t
stripe_create (call_frame_t *frame,
	       xlator_t *this,
	       const char *name,
	       int32_t flags,
	       mode_t mode)
{
  stripe_private_t *priv = this->private;
  stripe_local_t *local = NULL;
  xlator_list_t *trav = NULL;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;
  local->stripe_size = stripe_get_matching_bs (name, priv->pattern);

  /* Everytime in stripe lookup, all child nodes should be looked up */
  trav = this->children;
  while (trav) {
    _STACK_WIND (frame,
		 stripe_create_cbk,
		 trav->xlator->name,  /* cookie */
		 trav->xlator,
		 trav->xlator->fops->create,
		 name,
		 flags,
		 mode);
    trav = trav->next;
  }

       
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
    /* Get the mapping in inode private */
    LOCK (&frame->mutex);
    /* Get the stat buf right */
    if (local->fd == 0) {
      local->fd = calloc (1, sizeof (fd_t));
      local->fd->ctx = get_new_dict ();
      local->fd->inode = local->inode;
      list_add (&local->fd->inode_list, &local->inode->fds); 
    }
    dict_set (local->fd->ctx, (char *)cookie, int_to_data ((long)fd));
    UNLOCK (&frame->mutex);
  }

  if (!callcnt) {
    if (local->failed) {
      local->op_ret = -1;
      local->op_errno = EIO; /* TODO: Or should it be ENOENT? */
    }
    if (local->op_ret == 0) {
      local->inode->private = local->list;
    }
    local->list = NULL;
    dict_set (local->fd->ctx, frame->this->name, data_from_int64 (local->stripe_size));
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->fd);
  }

  return 0;
}

int32_t
stripe_open (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     int32_t flags)
{
  stripe_local_t *local = NULL;
  stripe_private_t *priv = this->private;
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = loc->inode->private;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->inode = loc->inode;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  local->stripe_size = stripe_get_matching_bs (loc->path, priv->pattern);
  frame->local = local;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
    _STACK_WIND (frame,
		 stripe_open_cbk,
		 ino_list->xl->name,
		 ino_list->xl,
		 ino_list->xl->fops->open,
		 &tmp_loc,
		 flags);
  }

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
    /* Get the mapping in inode private */
    LOCK (&frame->mutex);
    /* Get the stat buf right */
    if (local->fd == 0) {
      local->fd = calloc (1, sizeof (fd_t));
      local->fd->ctx = get_new_dict ();
      local->fd->inode = local->inode;
      list_add (&local->fd->inode_list, &local->inode->fds); 
    }
    dict_set (local->fd->ctx, (char *)cookie, int_to_data ((long)fd));
    UNLOCK (&frame->mutex);
  }

  if (!callcnt) {
    if (local->failed) {
      local->op_ret = -1;
      local->op_errno = EIO; /* TODO: Or should it be ENOENT? */
    }
    if (local->op_ret == 0) {
      local->inode->private = local->list;
    }
    local->list = NULL;
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->fd);
  }

  return 0;
}

int32_t
stripe_opendir (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc)
{
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = loc->inode->private;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
    _STACK_WIND (frame,
		 stripe_opendir_cbk,
		 ino_list->xl->name, //cookie
		 ino_list->xl,
		 ino_list->xl->fops->opendir,
		 &tmp_loc);
  }
  
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
  int32_t callcnt = 0;
  stripe_local_t *local = frame->local;

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == -1) {
    if (op_errno == ENOTCONN) {
      local->failed = 1;
    } else {
      local->op_errno = op_errno;
    }
  }

  if (op_ret == 0) {
    local->op_ret = 0;
    local->value = value;
  }
  if (!callcnt) {
    if (local->failed) {
      local->op_ret = -1;
      local->op_errno = EIO; /* TODO: Or should it be ENOENT? */
    }
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->value);
  }
  return 0;
}

int32_t
stripe_getxattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 const char *name,
		 size_t size)
{
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = loc->inode->private;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
    STACK_WIND (frame,
		stripe_getxattr_cbk,
		ino_list->xl,
		ino_list->xl->fops->getxattr,
		&tmp_loc,
		name,
		size);
  }

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
  int32_t callcnt = 0;
  stripe_local_t *local = frame->local;

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == -1) {
    if (op_errno == ENOTCONN) {
      local->failed = 1;
    } else {
      local->op_errno = op_errno;
    }
  }
  
  if (op_ret == 0) {
    local->op_ret = 0;
    local->value = value;
  }
    
  if (!callcnt) {
    if (local->failed) {
      local->op_ret = -1;
      local->op_errno = EIO; /* TODO: Or should it be ENOENT? */
    }
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->value);
  }
  return 0;
}

int32_t
stripe_listxattr (call_frame_t *frame,
		   xlator_t *this,
		   loc_t *loc,
		   size_t size)
{
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = loc->inode->private;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
    STACK_WIND (frame,
		stripe_listxattr_cbk,
		ino_list->xl,
		ino_list->xl->fops->listxattr,
		&tmp_loc,
		size);
  }
  return 0;
}

int32_t
stripe_removexattr (call_frame_t *frame,
		     xlator_t *this,
		     loc_t *loc,
		     const char *name)
{
  stripe_local_t *local = NULL;
  stripe_inode_list_t *ino_list = NULL;
  struct list_head *list = loc->inode->private;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
    STACK_WIND (frame,
		stripe_stack_unwind_cbk,
		ino_list->xl,
		ino_list->xl->fops->removexattr,
		&tmp_loc,
		name);
  }

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
  int32_t callcnt = 0;
  stripe_local_t *local = frame->local;

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == -1) {
    if (op_errno == ENOTCONN) {
      local->failed = 1;
    } else {
      local->op_errno = op_errno;
    }
  }

  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = 0;
    local->lock = lock;
  }

  if (!callcnt) {
    if (local->failed) {
      local->op_ret = -1;
      local->op_errno = EIO; /* TODO: Or should it be ENOENT? */
    }
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->lock);
  }
  return 0;
}

int32_t
stripe_lk (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    int32_t cmd,
	    struct flock *lock)
{
  xlator_list_t *trav = NULL;
  stripe_local_t *local = NULL;
  data_t *fd_data = NULL;
  fd_t *child_fd = NULL;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;
  
  trav = this->children;
  while (trav) {
    fd_data = dict_get (fd->ctx, trav->xlator->name);
    if (fd_data) {
      child_fd = data_to_ptr (fd_data);
      
      _STACK_WIND (frame,	      
		   stripe_lk_cbk,
		   trav->xlator,
		   trav->xlator,
		   trav->xlator->fops->lk,
		   child_fd,
		   cmd,
		   lock);
    }
  }
  return 0;
}


int32_t
stripe_flush (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  xlator_list_t *trav = NULL;
  stripe_local_t *local = NULL;
  data_t *fd_data = NULL;
  fd_t *child_fd = NULL;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;
  
  trav = this->children;
  while (trav) {
    fd_data = dict_get (fd->ctx, trav->xlator->name);
    if (fd_data) {
      child_fd = data_to_ptr (fd_data);
      
      STACK_WIND (frame,	      
		  stripe_stack_unwind_cbk,
		  trav->xlator,
		  trav->xlator->fops->flush,
		  child_fd);
    }
  }

  return 0;
}


int32_t
stripe_close (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  xlator_list_t *trav = NULL;
  stripe_local_t *local = NULL;
  data_t *fd_data = NULL;
  fd_t *child_fd = NULL;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;
  
  trav = this->children;
  while (trav) {
    fd_data = dict_get (fd->ctx, trav->xlator->name);
    if (fd_data) {
      child_fd = data_to_ptr (fd_data);
      
      STACK_WIND (frame,	      
		  stripe_stack_unwind_cbk,
		  trav->xlator,
		  trav->xlator->fops->close,
		  child_fd);
    }
  }
  
  dict_destroy (fd->ctx);
  return 0;
}

int32_t
stripe_fsync (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t flags)
{
  xlator_list_t *trav = NULL;
  stripe_local_t *local = NULL;
  data_t *fd_data = NULL;
  fd_t *child_fd = NULL;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;
  
  trav = this->children;
  while (trav) {
    fd_data = dict_get (fd->ctx, trav->xlator->name);
    if (fd_data) {
      child_fd = data_to_ptr (fd_data);
      
      STACK_WIND (frame,	      
		  stripe_stack_unwind_cbk,
		  trav->xlator,
		  trav->xlator->fops->fsync,
		  child_fd,
		  flags);
    }
  }

  return 0;
}

int32_t
stripe_fstat (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  xlator_list_t *trav = NULL;
  stripe_local_t *local = NULL;
  data_t *fd_data = NULL;
  fd_t *child_fd = NULL;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;
  
  trav = this->children;
  while (trav) {
    fd_data = dict_get (fd->ctx, trav->xlator->name);
    if (fd_data) {
      child_fd = data_to_ptr (fd_data);
      
      _STACK_WIND (frame,	      
		   stripe_stack_unwind_buf_cbk,
		   trav->xlator,
		   trav->xlator,
		   trav->xlator->fops->fstat,
		   child_fd);
    }
  }

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
  /* TODO: Currently the assumption is the direntry_t structure will be
   * having file info in same order in all the nodes. If its not, then 
   * this wont work.
   */
  stripe_local_t *local = frame->local;
  int32_t callcnt;
  LOCK(&frame->mutex);
  callcnt = ++local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == -1 && op_errno != ENOTCONN && op_errno != ENOENT) {
    local->op_errno = op_errno;
  }
  if (op_ret == 0) {
    LOCK (&frame->mutex);
    local->op_ret = op_ret;
    if (!local->entry) {
      dir_entry_t *trav = entries->next;
      local->entry = calloc (1, sizeof (dir_entry_t));
      entries->next = NULL;
      local->entry->next = trav;
      local->count = count;
    } else {
      /* update stat of all the entries */
      dir_entry_t *trav_local = local->entry->next;
      dir_entry_t *trav = entries->next;
      while (trav && trav_local) {
        trav_local->buf.st_size += trav->buf.st_size;
        trav_local->buf.st_blocks += trav->buf.st_blocks;
        trav_local->buf.st_blksize += trav->buf.st_blksize;

        trav = trav->next;
        trav_local = trav_local->next;
      }
    }
    UNLOCK (&frame->mutex);
  }
  if (callcnt == ((stripe_private_t *)this->private)->child_count) {
    dir_entry_t *prev = local->entry;
    LOCK_DESTROY(&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->entry, local->count);
    dir_entry_t *trav = prev->next;
    while (trav) {
      prev->next = trav->next;
      free (trav->name);
      free (trav);
      trav = prev->next;
    }
    free (prev);
  }

  return 0;
}

int32_t
stripe_readdir (call_frame_t *frame,
		xlator_t *this,
		size_t size,
		off_t offset,
		fd_t *fd)
{
  xlator_list_t *trav = NULL;
  stripe_local_t *local = NULL;
  data_t *fd_data = NULL;
  fd_t *child_fd = NULL;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;
  
  trav = this->children;
  while (trav) {
    fd_data = dict_get (fd->ctx, trav->xlator->name);
    if (fd_data) {
      child_fd = data_to_ptr (fd_data);
      
      _STACK_WIND (frame,	      
		   stripe_readdir_cbk,
		   trav->xlator,
		   trav->xlator,
		   trav->xlator->fops->readdir,
		   size,
		   offset,
		   child_fd);
    }
  }

  return 0;
}

int32_t 
stripe_fchmod (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       mode_t mode)
{
  xlator_list_t *trav = NULL;
  stripe_local_t *local = NULL;
  data_t *fd_data = NULL;
  fd_t *child_fd = NULL;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;
  
  trav = this->children;
  while (trav) {
    fd_data = dict_get (fd->ctx, trav->xlator->name);
    if (fd_data) {
      child_fd = data_to_ptr (fd_data);
      
      _STACK_WIND (frame,	      
		   stripe_stack_unwind_buf_cbk,
		   trav->xlator,
		   trav->xlator,
		   trav->xlator->fops->fchmod,
		   child_fd,
		   mode);
    }
  }

  return 0;
}

int32_t 
stripe_fchown (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       uid_t uid,
	       gid_t gid)
{
  xlator_list_t *trav = NULL;
  stripe_local_t *local = NULL;
  data_t *fd_data = NULL;
  fd_t *child_fd = NULL;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;
  
  trav = this->children;
  while (trav) {
    fd_data = dict_get (fd->ctx, trav->xlator->name);
    if (fd_data) {
      child_fd = data_to_ptr (fd_data);
      
      _STACK_WIND (frame,	      
		   stripe_stack_unwind_buf_cbk,
		   trav->xlator,
		   trav->xlator,
		   trav->xlator->fops->fchown,
		   child_fd,
		   uid,
		   gid);
    }
  }

  return 0;
}


int32_t
stripe_ftruncate (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  off_t offset)
{
  xlator_list_t *trav = NULL;
  stripe_local_t *local = NULL;
  data_t *fd_data = NULL;
  fd_t *child_fd = NULL;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;
  
  trav = this->children;
  while (trav) {
    fd_data = dict_get (fd->ctx, trav->xlator->name);
    if (fd_data) {
      child_fd = data_to_ptr (fd_data);
      
      _STACK_WIND (frame,	      
		   stripe_stack_unwind_buf_cbk,
		   trav->xlator,
		   trav->xlator,
		   trav->xlator->fops->ftruncate,
		   child_fd,
		   offset);
    }
  }

  return 0;
}


int32_t
stripe_closedir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd)
{
  xlator_list_t *trav = NULL;
  stripe_local_t *local = NULL;
  data_t *fd_data = NULL;
  fd_t *child_fd = NULL;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;
  
  trav = this->children;
  while (trav) {
    fd_data = dict_get (fd->ctx, trav->xlator->name);
    if (fd_data) {
      child_fd = data_to_ptr (fd_data);
      STACK_WIND (frame,	      
		  stripe_stack_unwind_cbk,
		  trav->xlator,
		  trav->xlator->fops->closedir,
		  child_fd);
    }
  }
  dict_destroy (fd->ctx);

  return 0;
}

int32_t
stripe_fsyncdir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 int32_t flags)
{
  xlator_list_t *trav = NULL;
  stripe_local_t *local = NULL;
  data_t *fd_data = NULL;
  fd_t *child_fd = NULL;

  /* Initialization */
  local = calloc (1, sizeof (stripe_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((stripe_private_t *)this->private)->child_count;
  frame->local = local;
  
  trav = this->children;
  while (trav) {
    fd_data = dict_get (fd->ctx, trav->xlator->name);
    if (fd_data) {
      child_fd = data_to_ptr (fd_data);
      
      STACK_WIND (frame,	      
		  stripe_stack_unwind_cbk,
		  trav->xlator,
		  trav->xlator->fops->fsyncdir,
		  child_fd,
		  flags);
    }
  }

  return 0;
}



/**
 * TODO: Below 4 functions needs a review for coding guidelines
 */
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
  int32_t callcnt = 0;
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
  callcnt = ++local->call_count;
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

/**
 * stripe_write - follow the coding guidelines
 */
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



/* Management operations */

/**
 * stripe_stats_cbk - Add all the fields received from different clients. 
 *    Once all the clients return, send stats to above layer.
 * 
 */
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

/**
 * stripe_stats - 
 */
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

/**
 * stripe_fsck_cbk  - pass on the to the above layer once all the child returns 
 */
static int32_t 
stripe_fsck_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  int32_t callcnt = 0;
  stripe_local_t *local = frame->local;

  LOCK(&frame->mutex);
  callcnt = ++local->call_count;
  UNLOCK (&frame->mutex);

  local->op_ret = op_ret;
  local->op_errno = op_errno;

  if (callcnt == ((stripe_private_t*)this->private)->child_count) {
    LOCK_DESTROY(&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
} 

/**
 * stripe_fsck - At this level there is nothing to heal. Pass it on to child nodes.
 */
int32_t
stripe_fsck (call_frame_t *frame,
	     xlator_t *this,
	     int32_t flags)
{
  stripe_local_t *local = (stripe_local_t *) calloc (1, sizeof (stripe_local_t));
  xlator_list_t *trav = this->children;
 
  frame->local = local;
  LOCK_INIT (&frame->mutex);
  
  while (trav) {
    STACK_WIND (frame,
                stripe_fsck_cbk,
                trav->xlator,
                trav->xlator->mops->fsck,
                flags);
    trav = trav->next;
  }

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
