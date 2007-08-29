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

/**
 * xlators/cluster/unify:
 *     - This xlator is one of the main translator in GlusterFS, which
 *   actually does the clustering work of the file system. One need to 
 *   understand that, unify assumes file to be existing in only one of 
 *   the child node, and directories to be present on all the nodes. 
 *
 * NOTE:
 *   Now, unify has support for global namespace, which is used to keep a 
 * global view of fs's namespace tree. The stat for directories are taken
 * just from the namespace, where as for files, just 'st_size' and 'st_blocks' 
 * fields are taken from the actual file. Namespace also helps to provide 
 * consistant inode for files across glusterfs mounts.
 *
 */
#include "glusterfs.h"
#include "unify.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"
#include "defaults.h"
#include "common-utils.h"

#define CHILDDOWN ENOTCONN

#ifdef STATIC
#undef STATIC
#endif
#define STATIC   /*static*/

#define UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR(_loc) do { \
  if (!(_loc && _loc->inode && _loc->inode->ctx &&         \
	dict_get (_loc->inode->ctx, this->name))) {        \
    TRAP_ON (!(_loc && _loc->inode && _loc->inode->ctx &&  \
               dict_get (_loc->inode->ctx, this->name)));  \
    STACK_UNWIND (frame, -1, EINVAL, NULL, NULL, NULL);    \
    return 0;                                              \
  }                                                        \
} while(0)


#define UNIFY_CHECK_FD_CTX_AND_UNWIND_ON_ERR(_fd) do { \
  if (!(_fd && _fd->ctx &&                             \
	dict_get (_fd->ctx, this->name))) {            \
    TRAP_ON (!(_fd && _fd->ctx &&                      \
	          dict_get (_fd->ctx, this->name)));   \
    STACK_UNWIND (frame, -1, EBADFD, NULL, NULL);      \
    return 0;                                          \
  }                                                    \
} while(0)

#define UNIFY_CHECK_FD_AND_UNWIND_ON_ERR(_fd) do { \
  if (!(_fd && _fd->ctx)) {                        \
    TRAP_ON (!(_fd && _fd->ctx));                  \
    STACK_UNWIND (frame, -1, EBADFD, NULL, NULL);  \
    return 0;                                      \
  }                                                \
} while(0)

/**
 * unify_local_wipe - free all the extra allocation of local->* here.
 */
static void 
unify_local_wipe (unify_local_t *local)
{
  /* Free the strdup'd variables in the local structure */
  if (local->path) {
    freee (local->path);
  }
  if (local->name) {
    freee (local->name);
  }
}

/**
 * unify_bg_buf_cbk - Used as _cbk in background frame, which returns buf.
 *
 */
STATIC int32_t
unify_bg_buf_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (!callcnt) {
    unify_local_wipe (local);
    STACK_DESTROY (frame->root);
  }
  return 0;
}

/**
 * unify_buf_cbk - 
 */
STATIC int32_t
unify_buf_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *buf)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    
    if (local->op_ret == -1 && op_errno != CHILDDOWN)
      local->op_errno = op_errno;

    if (op_ret >= 0) {
      local->op_ret = op_ret;

      if (NS (this) == ((call_frame_t *)cookie)->this)
	local->stbuf = *buf;

      /* If file, then replace size of file in stat info. */
      if ((!S_ISDIR (buf->st_mode)) && 
	  (NS (this) != ((call_frame_t *)cookie)->this)) {
	local->st_size = buf->st_size;
	local->st_blocks = buf->st_blocks;
	///local->stbuf.st_mtime = buf->st_mtime;
      }
    }
  }
  UNLOCK (&frame->lock);
    
  if (!callcnt) {
    unify_local_wipe (local);
    local->stbuf.st_size = local->st_size;
    local->stbuf.st_blocks = local->st_blocks;
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}


/**
 * unify_lookup_cbk - 
 */
STATIC int32_t 
unify_lookup_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
		  struct stat *buf)
{
  int32_t callcnt = 0;
  unify_private_t *priv = this->private;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
 
    if (op_ret == -1) {
      local->op_errno = op_errno;
      local->failed = 1;
    }

    if (op_ret == 0) {
      local->op_ret = 0; 
      if (!local->revalidate) {
	/* This is the first time lookup */
	if (!local->list) {
	  /* list is not allocated, allocate  the max possible range */
	  local->list = calloc (1, sizeof (int16_t) * (priv->child_count + 2));
	  if (!local->list) {
	    gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
	    STACK_UNWIND (frame, -1, ENOMEM, local->inode, NULL);
	    return 0;
	  }
	}
	/* update the index of the list */
	local->list [local->index++] = (int16_t)(long)cookie;
      }
      /* Replace most of the variables from NameSpace */
      if (priv->child_count == (int16_t)(long)cookie) {
	local->stbuf = *buf;
	local->inode = inode;
	inode->st_mode = buf->st_mode;
      } else if (!S_ISDIR (buf->st_mode)) {
	  /* If file, then replace size of file in stat info */
	  local->st_size = buf->st_size;
	  local->st_blocks = buf->st_blocks;
	  ///local->st_mtime = buf->st_mtime;
      }
      if (local->st_nlink < buf->st_nlink)
	local->st_nlink = buf->st_nlink;
    }
  }
  UNLOCK (&frame->lock);

  if (!callcnt) {
    if (!local->stbuf.st_blksize) {
      /* Inode not present */
      local->op_ret = -1;
    } else {
      if (!local->revalidate) { 
	int16_t *list = NULL;
	if (!S_ISDIR (local->inode->st_mode)) {
	  /* If its a file, big array is useless, allocate the smaller one */
	  list = calloc (1, sizeof (int16_t) * (local->index + 1));
	  memcpy (list, local->list, sizeof (int16_t) * local->index);
	  /* Make the end of the list as -1 */
	  freee (local->list);
	  local->list = list;
	}
	local->list [local->index] = -1;
	/* Update the inode->ctx with the proper array */
	dict_set (local->inode->ctx, this->name, data_from_ptr (local->list));
      }
      if (S_ISDIR(local->inode->st_mode)) {
	/* lookup is done for directory */
	if (local->failed) {
	  local->inode->generation = 0; /*means, self-heal required for inode*/
	  priv->inode_generation++;
	}
      } else {
	local->stbuf.st_size = local->st_size;
	local->stbuf.st_blocks = local->st_blocks;
	///local->stbuf.st_mtime = local->st_mtime;
      }

      local->stbuf.st_nlink = local->st_nlink;
    }
    if (local->op_ret == -1) {
      if (!local->revalidate && local->list)
	freee (local->list);
    }

    if ((local->op_ret >= 0) && local->failed && local->revalidate) {
      /* Done revalidate, but it failed */
      local->op_ret = -1;
      local->op_errno = ENOENT;
    }
    if ((priv->self_heal) && 
	((local->op_ret == 0) && S_ISDIR(local->inode->st_mode))) {
      /* Let the self heal be done here */
      gf_unify_self_heal (frame, this, local);
    } else {
      /* either no self heal, or failure */
      unify_local_wipe (local);
      STACK_UNWIND (frame, 
		    local->op_ret, 
		    local->op_errno, 
		    local->inode, 
		    &local->stbuf);
    }
  }

  return 0;
}

/**
 * unify_lookup - 
 */
int32_t 
unify_lookup (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  unify_local_t *local = NULL;
  unify_private_t *priv = this->private;
  int16_t *list = NULL;
  int16_t index = 0;

  if (!(loc && loc->inode && loc->inode->ctx)) {
    gf_log (this->name, GF_LOG_ERROR, 
	    "%s: Argument not right", loc?loc->path:"(null)");
    STACK_UNWIND (frame, -1, EINVAL, NULL, NULL);
    return 0;
  }

  /* Initialization */
  INIT_LOCAL (frame, local);
  local->inode = loc->inode;
  local->path = strdup (loc->path);
  if (!local->path) {
    gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
    STACK_UNWIND (frame, -1, ENOMEM, loc->inode, NULL);
    return 0;
  }

  if (dict_get (loc->inode->ctx, this->name)) 
    local->list = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  
  if (local->list) {
    list = local->list;
    local->revalidate = 1;

    for (index = 0; list[index] != -1; index++)
      local->call_count++;
    
    for (index = 0; list[index] != -1; index++) {
      _STACK_WIND (frame,
		   unify_lookup_cbk,
		   (void *)(long)list [index], //cookie
		   priv->xl_array [list [index]],
		   priv->xl_array [list [index]]->fops->lookup,
		   loc);
    }
  } else {
    /* This is first call, there is no list */
    /* call count should be all child + 1 namespace */
    local->call_count = priv->child_count + 1;

    for (index = 0; index <= priv->child_count; index++) {
      _STACK_WIND (frame,
		   unify_lookup_cbk,
		   (void *)(long)index, //cookie
		   priv->xl_array[index],
		   priv->xl_array[index]->fops->lookup,
		   loc);
    }
  }

  return 0;
}


/**
 * unify_forget - call inode_forget which removes it from cache 
 */
int32_t 
unify_forget (call_frame_t *frame,
	      xlator_t *this,
	      inode_t *inode)
{
  /* in dictionary list is stored as pointer, so will be freed, when dictionary
   * is destroyed 
   */

  return 0;
}

/**
 * unify_stat - if directory, get the stat directly from NameSpace child.
 *     if file, check for a hint and send it only there (also to NS).
 *     if its a fresh stat, then do it on all the nodes.
 *
 * NOTE: for all the call, sending cookie as xlator pointer, which will be 
 *       used in cbk.
 */
int32_t
unify_stat (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  unify_local_t *local = NULL;
  unify_private_t *priv = this->private;
  int16_t index = 0;
  int16_t *list = NULL;

  UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

  /* Initialization */
  INIT_LOCAL (frame, local);
  local->inode = loc->inode;
  local->path = strdup (loc->path);
  if (!local->path) {
    gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }
  
  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  for (index = 0; list[index] != -1; index++)
    local->call_count++;
  
  for (index = 0; list[index] != -1; index++) {
    STACK_WIND (frame,
		unify_buf_cbk,
		priv->xl_array[list[index]],
		priv->xl_array[list[index]]->fops->stat,
		loc);
  }

  return 0;
}

/**
 * unify_access_cbk -
 */
STATIC int32_t
unify_access_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}


/**
 * unify_access - Send request to only namespace, which has all the 
 *      attributes set for the file.
 */
int32_t
unify_access (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t mask)
{
  UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

  STACK_WIND (frame,
	      unify_access_cbk,
	      NS(this),
	      NS(this)->fops->access,
	      loc,
	      mask);

  return 0;
}

STATIC int32_t
unify_mkdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *buf)
{
  int32_t callcnt = 0;
  unify_private_t *priv = this->private;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  
    if (op_ret == -1) {
      local->failed = 1;
    }
  
    if (op_ret >= 0) {
      local->op_ret = 0;
      /* This is to be used as hint from the inode and also mapping */
      local->list[local->index++] = (int16_t)(long)cookie;
    }
  }
  UNLOCK (&frame->lock);
  
  if (!callcnt) {
    unify_local_wipe (local);
    if (!local->failed)
      local->inode->generation = priv->inode_generation;
    if (local->op_ret >= 0) {
      local->list[local->index] = -1;
    }
    STACK_UNWIND (frame, 
		  local->op_ret, 
		  local->op_errno, 
		  local->inode, 
		  &local->stbuf);
  }

  return 0;
}

/**
 * unify_ns_mkdir_cbk -
 */
STATIC int32_t
unify_ns_mkdir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf)
{
  unify_private_t *priv = this->private;
  unify_local_t *local = frame->local;
  int16_t index = 0;

  if (op_ret == -1) {
    /* No need to send mkdir request to other servers, 
     * as namespace action failed 
     */
    unify_local_wipe (local);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  inode,
		  NULL);
    return 0;
  }
  
  /* Create one inode for this entry */
  local->op_ret = 0;
  local->stbuf = *buf;

  local->inode = inode;

  local->list = calloc (1, sizeof (int16_t) * (priv->child_count + 2));
  if (!local->list) {
    gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
    unify_local_wipe (local);
    STACK_UNWIND (frame, -1, ENOMEM, inode, NULL);
    return 0;
  }
  dict_set (inode->ctx, this->name, data_from_ptr (local->list));

  local->list[0] = priv->child_count; //index of namespace in xl_array
  local->index = 1;
  local->call_count = priv->child_count;

  /* Send mkdir request to all the nodes now */
  for (index = 0; index < priv->child_count; index++) {
    loc_t tmp_loc = {
      .inode = inode,
      .path = local->name
    };
    _STACK_WIND (frame,
		 unify_mkdir_cbk,
		 (void *)(long)index, //cookie
		 priv->xl_array[index],
		 priv->xl_array[index]->fops->mkdir,
		 &tmp_loc,
		 local->mode);
  }
  
  return 0;
}


/**
 * unify_mkdir -
 */
int32_t
unify_mkdir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode)
{
  unify_local_t *local = NULL;

  /* Initialization */
  INIT_LOCAL (frame, local);
  local->mode = mode;
  local->inode = loc->inode;
  local->name = strdup (loc->path);
  if (!local->name) {
    gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
    STACK_UNWIND (frame, -1, ENOMEM, loc->inode, NULL);
    return 0;
  }

  STACK_WIND (frame,
	      unify_ns_mkdir_cbk,
	      NS(this),
	      NS(this)->fops->mkdir,
	      loc,
	      mode);
  return 0;
}

/**
 * unify_rmdir_cbk -
 */
STATIC int32_t
unify_rmdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    if (op_ret == 0)
      local->op_ret = 0;
  }
  UNLOCK (&frame->lock);

  if (!callcnt) {
    unify_local_wipe (local);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}

/**
 * unify_ns_rmdir_cbk -
 */
STATIC int32_t
unify_ns_rmdir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  int16_t index = 0;
  unify_private_t *priv = this->private;
  unify_local_t *local = frame->local;
  
  if (op_ret == -1) {
     /* No need to send rmdir request to other servers, 
     * as namespace action failed 
     */
    unify_local_wipe (local);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno);
    return 0;
  }
  
  for (index = 0; local->list[index] != -1; index++)
    local->call_count++;
  local->call_count--; // for namespace

  if (local->call_count) {
    for (index = 0; local->list[index] != -1; index++) {
      if (priv->xl_array[local->list[index]] != NS(this)) {
	loc_t tmp_loc = {
	  .path = local->path, 
	  .inode = local->inode
	};
	STACK_WIND (frame,
		    unify_rmdir_cbk,
		    priv->xl_array[local->list[index]],
		    priv->xl_array[local->list[index]]->fops->rmdir,
		    &tmp_loc);
      }
    }
  } else {
    unify_local_wipe (local);
    STACK_UNWIND (frame, -1, ENOENT);
  }

  /* */
  return 0;
}

/**
 * unify_rmdir -
 */
int32_t
unify_rmdir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
  unify_local_t *local = NULL;

  UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

  /* Initialization */
  INIT_LOCAL (frame, local);
  local->inode = loc->inode;
  local->list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  local->path = strdup (loc->path);
  if (!local->path) {
    gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  STACK_WIND (frame,
	      unify_ns_rmdir_cbk,
	      NS(this),
	      NS(this)->fops->rmdir,
	      loc);

  return 0;
}

STATIC int32_t 
unify_open_close_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
  unify_local_t *local = frame->local;

  STACK_UNWIND (frame, 
		local->op_ret, 
		local->op_errno, 
		local->fd);
  
  return 0;
}

/**
 * unify_open_cbk -
 */
STATIC int32_t
unify_open_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		fd_t *fd)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    if (op_ret >= 0) {
      local->op_ret = op_ret;
      if (NS(this) != (xlator_t *)cookie) {
	/* Store child node's ptr, used in all the f*** / FileIO calls */
	dict_set (fd->ctx,
		  this->name,
		  data_from_static_ptr (cookie));
      }
    }
    if (op_ret == -1 && op_errno != CHILDDOWN) {
      local->op_errno = op_errno;
      local->failed = 1;
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);
  
  if (!callcnt) {
    if (local->failed == 1 && (local->op_ret >= 0)) {
      local->call_count = 1;
      /* return -1 to user */
      local->op_ret = -1;
      local->op_errno = EIO; 
      
      if (dict_get (local->fd->ctx, this->name)) {
	xlator_t *child = data_to_ptr (dict_get (local->fd->ctx, this->name));
	gf_log (this->name, GF_LOG_ERROR, 
		"Open success on child node, failed on namespace");
	STACK_WIND (frame,
		    unify_open_close_cbk,
		    child,
		    child->fops->close,
		    local->fd);
      } else {
	gf_log (this->name, GF_LOG_ERROR, 
		"Open success on namespace, failed on child node");
	STACK_WIND (frame,
		    unify_open_close_cbk,
		    NS(this),
		    NS(this)->fops->close,
		    local->fd);
      }
      return 0;
    }

    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->fd);
  }

  return 0;
}

/**
 * unify_open - 
 */
int32_t
unify_open (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t flags,
	    fd_t *fd)
{
  unify_private_t *priv = this->private;
  unify_local_t *local = NULL;
  int16_t *list = NULL;
  int16_t index = 0;

  UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

  /* Init */
  INIT_LOCAL (frame, local);
  local->inode = loc->inode;
  local->fd = fd;
  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  local->list = list;
  for (index = 0; local->list[index] != -1; index++)
    local->call_count++;

  if (local->call_count != 2) {
    /* If the lookup was done for file */
    gf_log (this->name, 
	    GF_LOG_ERROR,
	    "%s: entry_count is %d",
	    loc->path, local->call_count);
    STACK_UNWIND (frame, -1, EIO, fd);
    return 0;
  }

  for (index = 0; list[index] != -1; index++) {
    _STACK_WIND (frame,
		 unify_open_cbk,
		 priv->xl_array[list[index]], //cookie
		 priv->xl_array[list[index]],
		 priv->xl_array[list[index]]->fops->open,
		 loc,
		 flags,
		 fd);
  }

  return 0;
}


STATIC int32_t 
unify_create_close_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
  unify_local_t *local = frame->local;
  
  STACK_UNWIND (frame, 
		local->op_ret, 
		local->op_errno, 
		local->fd,
		local->inode,
		&local->stbuf);
  
  return 0;
}


STATIC int32_t 
unify_create_fail_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
  unify_local_t *local = frame->local;
  
  /* Create failed in storage node, but it was success in 
   * namespace node, so after closing fd, need to unlink the file
   */
  loc_t tmp_loc = {
    .inode = local->inode,
    .path = local->name
  };
  local->call_count = 1;

  STACK_WIND (frame,
	      unify_create_close_cbk,
	      NS(this),
	      NS(this)->fops->unlink,
	      &tmp_loc);

  return 0;
}

/**
 * unify_create_open_cbk -
 */
STATIC int32_t
unify_create_open_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       fd_t *fd)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    if (op_ret >= 0) {
      local->op_ret = op_ret;
      if (NS(this) != (xlator_t *)cookie) {
	/* Store child node's ptr, used in all the f*** / FileIO calls */
	dict_set (fd->ctx, 
		  this->name, 
		  data_from_static_ptr (cookie));
      }
    }
    if (op_ret == -1 && op_errno != CHILDDOWN) {
      local->op_errno = op_errno;
      local->failed = 1;
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);
  
  if (!callcnt) {
    if (local->failed == 1 && (local->op_ret >= 0)) {
      local->call_count = 1;
      /* return -1 to user */
      local->op_ret = -1;
      local->op_errno = EIO;
      local->fd = fd;
      if (dict_get (local->fd->ctx, this->name)) {
	xlator_t *child = data_to_ptr (dict_get (local->fd->ctx, this->name));
	gf_log (this->name, GF_LOG_ERROR, 
		"Open success on child node, failed on namespace");
	STACK_WIND (frame,
		    unify_create_close_cbk,
		    child,
		    child->fops->close,
		    local->fd);
      } else {
	gf_log (this->name, GF_LOG_ERROR, 
		"Open success on namespace, failed on child node");
	STACK_WIND (frame,
		    unify_create_close_cbk,
		    NS(this),
		    NS(this)->fops->close,
		    local->fd);
      }
      return 0;
    }

    STACK_UNWIND (frame, 
		  local->op_ret, 
		  local->op_errno, 
		  fd,
		  local->inode,
		  &local->stbuf);
  }
  return 0;
}

/**
 * unify_create_lookup_cbk - 
 */
STATIC int32_t 
unify_create_lookup_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 inode_t *inode,
			 struct stat *buf)
{
  int32_t callcnt = 0;
  int16_t index = 0;
  unify_private_t *priv = this->private;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    if (op_ret == -1) {
      local->op_errno = op_errno;
      local->failed = 1;
    }

    if (op_ret >= 0) {
      local->op_ret = op_ret; 
      local->list[local->index++] = (int16_t)(long)cookie;
      /* Replace most of the variables from NameSpace */
      if (NS(this) == (xlator_t *)cookie) {
	local->stbuf = *buf;
      } else {
	/* If file, then replace size of file in stat info */
	local->st_size = buf->st_size;
	local->st_blocks = buf->st_blocks;
	///local->st_mtime = buf->st_mtime;
      }
    }
  }
  UNLOCK (&frame->lock);

  if (!callcnt) {
    local->list [local->index] = -1;
    dict_set (local->inode->ctx, 
	      this->name, 
	      data_from_static_ptr (local->list));

    if (local->entry_count == 2) {
      /* Everything is perfect :) */
      int16_t *list = local->list;

      local->op_ret = -1;

      for (index = 0; list[index] != -1; index++)
	local->call_count++;
      
      for (index = 0; list[index] != -1; index++) {
	loc_t tmp_loc = {
	  .inode = inode,
	  .path = local->name,
	};
	_STACK_WIND (frame,
		     unify_create_open_cbk,
		     priv->xl_array[list[index]], //cookie
		     priv->xl_array[list[index]],
		     priv->xl_array[list[index]]->fops->open,
		     &tmp_loc,
		     local->flags,
		     local->fd);
      }
    } else {
      /* Lookup failed, can't do open */
      gf_log (this->name, GF_LOG_ERROR,
	      "%s: entry_count is %d",
	      local->path, local->entry_count);
      local->op_ret = -1;
      local->op_errno = ENOENT;
      unify_local_wipe (local);
      STACK_UNWIND (frame, 
		    local->op_ret, 
		    local->op_errno, 
		    local->fd,
		    local->inode,
		    NULL);
    }
  }

  return 0;
}


/**
 * unify_create_cbk -
 */
STATIC int32_t
unify_create_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  fd_t *fd,
		  inode_t *inode,
		  struct stat *buf)
{
  unify_local_t *local = frame->local;

  if (op_ret == -1 && op_errno != ENOENT) {
    /* send close () on Namespace */
    local->op_errno = op_errno;
    local->op_ret = -1;
    local->call_count = 1;
    STACK_WIND (frame,
		unify_create_fail_cbk,
		NS(this),
		NS(this)->fops->close,
		fd);

    return 0;
  }

  if (op_ret >= 0) {
    local->op_ret = op_ret;

    dict_set (fd->ctx, 
	      this->name, 
	      data_from_static_ptr (cookie));
  }
  
  unify_local_wipe (local);
  STACK_UNWIND (frame, 
		local->op_ret, 
		local->op_errno, 
		local->fd, 
		local->inode, 
		&local->stbuf);

  return 0;
}

/**
 * unify_ns_create_cbk -
 * 
 */
STATIC int32_t
unify_ns_create_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     fd_t *fd,
		     inode_t *inode,
		     struct stat *buf)
{
  struct sched_ops *sched_ops = NULL;
  xlator_t *sched_xl = NULL;
  unify_local_t *local = frame->local;
  unify_private_t *priv = this->private;
  int16_t *list = NULL;
  int16_t index = 0;

  if (op_ret == -1) {
    /* No need to send create request to other servers, as namespace 
     * action failed. Handle exclusive create here.
     */
    if ((op_errno != EEXIST) || 
	((op_errno == EEXIST) && ((local->flags & O_EXCL) == O_EXCL))) {
      /* If its just a create call without O_EXCL, don't do this */
      unify_local_wipe (local);
      STACK_UNWIND (frame,
		    op_ret,
		    op_errno,
		    fd,
		    inode,
		    buf);
      return 0;
    }
  }
  
  if (op_ret >= 0) {
    /* Create/update inode for this entry */
  
    /* link fd and inode */
    local->stbuf = *buf;
  
    local->op_ret = -1;

    /* Start the mapping list */
    list = calloc (1, sizeof (int16_t) * 3);
    dict_set (inode->ctx, this->name, data_from_ptr (list));
    list[0] = priv->child_count;
    list[2] = -1;

    /* This means, file doesn't exist anywhere in the Filesystem */
    sched_ops = priv->sched_ops;

    /* Send create request to the scheduled node now */
    sched_xl = sched_ops->schedule (this, 0); 
    for (index = 0; index < priv->child_count; index++)
      if (sched_xl == priv->xl_array[index])
	break;
    list[1] = index;
    local->inode = inode;

    {
      loc_t tmp_loc = {
	.inode = inode,
	.path = local->name
      };
      _STACK_WIND (frame,
		   unify_create_cbk,
		   sched_xl,
		   sched_xl,
		   sched_xl->fops->create,
		   &tmp_loc,
		   local->flags,
		   local->mode,
		   fd);
    }
  } else {
    /* File already exists, and there is no O_EXCL flag */
    local->list = calloc (1, sizeof (int16_t) * 3);
    local->call_count = priv->child_count + 1;
    local->op_ret = -1;
    for (index = 0; index <= priv->child_count; index++) {
      /* Send the lookup to all the nodes including namespace */
      loc_t tmp_loc = {
	.path = local->name,
	.inode = inode,
      };
      _STACK_WIND (frame,
		   unify_create_lookup_cbk,
		   (void *)(long)index,
		   priv->xl_array[index],
		   priv->xl_array[index]->fops->lookup,
		   &tmp_loc);
    }
  }
  return 0;
}

/**
 * unify_create - create a file in global namespace first, so other 
 *    clients can see them. Create the file in storage nodes in background.
 */
int32_t
unify_create (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t flags,
	      mode_t mode,
	      fd_t *fd)
{
  unify_local_t *local = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->mode = mode;
  local->flags = flags;
  local->inode = loc->inode;
  local->fd = fd;

  local->name = strdup (loc->path);
  if (!local->name) {
    gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
    STACK_UNWIND (frame, -1, ENOMEM, fd, loc->inode, NULL);
    return 0;
  }

  STACK_WIND (frame,
	      unify_ns_create_cbk,
	      NS(this),
	      NS(this)->fops->create,
	      loc,
	      flags | O_EXCL,
	      mode,
	      fd);
  
  return 0;
}

STATIC int32_t 
unify_opendir_fail_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (!callcnt) {
    unify_local_wipe (local);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->fd);
  }

  return 0;
}

/**
 * unify_opendir_cbk - 
 */
STATIC int32_t
unify_opendir_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   fd_t *fd)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;
  unify_private_t *priv = this->private;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    
    if (op_ret >= 0) {
      local->op_ret = op_ret;
    }
    if (op_ret == -1 && op_errno != CHILDDOWN) {
      local->op_errno = op_errno;
      local->failed = 1;
    }
  }
  UNLOCK (&frame->lock);

  if (!callcnt) {
    if (local->failed == 1 && dict_get (local->fd->inode->ctx, this->name)) {
      int16_t *list = NULL;
      int16_t index = 0;

      list = data_to_ptr (dict_get (local->fd->inode->ctx, this->name));
      /* return -1 to user */
      local->op_ret = -1;
      local->call_count =0;
      for (index = 0; list[index] != -1; index++)
	local->call_count++;
      
      for (index = 0; list[index] != -1; index++) {
	STACK_WIND (frame,
		    unify_opendir_fail_cbk,
		    priv->xl_array[list[index]],
		    priv->xl_array[list[index]]->fops->closedir,
		    local->fd);
      }
      return 0;
    }

    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->fd);
  }
  return 0;
}

/** 
 * unify_opendir -
 */
int32_t
unify_opendir (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       fd_t *fd)
{
  int16_t *list = NULL;
  int16_t index = 0;
  unify_local_t *local = NULL;
  unify_private_t *priv = this->private;

  UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

  INIT_LOCAL (frame, local);
  local->inode = loc->inode;
  local->fd = fd;
  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  for (index = 0; list[index] != -1; index++)
    local->call_count++;

  for (index = 0; list[index] != -1; index++) {
    _STACK_WIND (frame,
		 unify_opendir_cbk,
		 priv->xl_array[list[index]],
		 priv->xl_array[list[index]],
		 priv->xl_array[list[index]]->fops->opendir,
		 loc,
		 fd);
  }

  return 0;
}

/**
 * unify_statfs_cbk -
 */
STATIC int32_t
unify_statfs_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct statvfs *stbuf)
{
  int32_t callcnt = 0;
  struct statvfs *dict_buf = NULL;
  unify_local_t *local = (unify_local_t *)frame->local;

  LOCK (&frame->lock);
  {
    if (op_ret == -1 && op_errno != CHILDDOWN) {
      /* fop on a storage node has failed due to some error, other than 
       * ENOTCONN
       */
      local->op_errno = op_errno;
    }
    if (op_ret == 0) {
      /* when a call is successfull, add it to local->dict */
      dict_buf = &local->statvfs_buf;
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
    }
    
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (!callcnt) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->statvfs_buf);
  }

  return 0;
}

/**
 * unify_statfs -
 */
int32_t
unify_statfs (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  unify_local_t *local = NULL;
  xlator_list_t *trav = this->children;

  INIT_LOCAL (frame, local);
  local->call_count = ((unify_private_t *)this->private)->child_count;

  while(trav) {
    STACK_WIND (frame,
		unify_statfs_cbk,
		trav->xlator,
		trav->xlator->fops->statfs,
		loc);
    trav = trav->next;
  }

  return 0;
}

/**
 * unify_ns_chmod_cbk - 
 */
STATIC int32_t
unify_ns_chmod_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
  call_frame_t *bg_frame = NULL;
  unify_local_t *local = frame->local;
  unify_private_t *priv = this->private;
  int16_t *list = local->list;
  int16_t index = 0;

  if (op_ret == -1) {
    /* No need to send chmod request to other servers, 
     * as namespace action failed 
     */
    unify_local_wipe (local);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = op_ret;
  local->stbuf = *buf;
    
  if (S_ISDIR (buf->st_mode)) {
    /* If directory, get a copy of the current frame, and set 
     * the current local to bg_frame's local 
     */
    bg_frame = copy_frame (frame);
    frame->local = NULL;
    bg_frame->local = local;
    LOCK_INIT (&bg_frame->lock);

    /* Unwind this frame, and continue with bg_frame */
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    
    /* Send chmod request to all the nodes now */
    for (index = 0; list[index] != -1; index++)
      local->call_count++;
    local->call_count--; /* Reduce 1 for namespace entry */
    
    if (local->call_count) {
      for (index = 0; list[index] != -1; index++) {
	if (priv->xl_array[list[index]] != NS(this)) {
	  loc_t tmp_loc = {
	    .inode = local->inode,
	    .path = local->path,
	  };
	  STACK_WIND (bg_frame,
		      unify_bg_buf_cbk,
		      priv->xl_array[list[index]],
		      priv->xl_array[list[index]]->fops->chmod,
		      &tmp_loc,
		      local->mode);
	}
      }
    } else {
      unify_local_wipe (local);
      STACK_DESTROY (bg_frame->root);
    }
  } else {
    /* Its not a directory, so copy will be present only on one storage node */
    for (index = 0; list[index] != -1; index++)
      local->call_count++;
    local->call_count--; //for namespace

    if (local->call_count) {
      for (index = 0; list[index] != -1; index++) {
	if (priv->xl_array[list[index]] != NS(this)) {
	  loc_t tmp_loc = {
	    .path = local->path, 
	    .inode = local->inode
	  };
	  STACK_WIND (frame,
		      unify_buf_cbk,
		      priv->xl_array[list[index]],
		      priv->xl_array[list[index]]->fops->chmod,
		      &tmp_loc,
		      local->mode);
	}
      }
    } else {
      STACK_UNWIND (frame, 0, 0, &local->stbuf);
    }
  }

  return 0;
}

/**
 * unify_chmod - 
 */
int32_t
unify_chmod (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode)
{
  unify_local_t *local = NULL;

  UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

  /* Initialization */
  INIT_LOCAL (frame, local);
  local->inode = loc->inode;
  local->mode = mode;
  local->path = strdup (loc->path);
  if (!local->path) {
    gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  local->list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  STACK_WIND (frame,
	      unify_ns_chmod_cbk,
	      NS(this),
	      NS(this)->fops->chmod,
	      loc,
	      mode);

  return 0;
}

/**
 * unify_ns_chown_cbk - 
 */
STATIC int32_t
unify_ns_chown_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
  call_frame_t *bg_frame = NULL;
  unify_local_t *local = frame->local;
  unify_private_t *priv = this->private;
  int16_t *list = local->list;
  int16_t index = 0;

  if (op_ret == -1) {
    /* No need to send chown request to other servers, as namespace action 
     * failed 
     */
    unify_local_wipe (local);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = op_ret;
  local->stbuf = *buf;

  if (S_ISDIR (buf->st_mode)) {
    /* If directory, get a copy of the current frame, and set 
     * the current local to bg_frame's local 
     */
    bg_frame = copy_frame (frame);
    frame->local = NULL;
    bg_frame->local = local;
    LOCK_INIT (&bg_frame->lock);
    
    /* Unwind this frame, and continue with bg_frame */
    STACK_UNWIND (frame, op_ret, op_errno, buf);

    local->call_count = 0;
    for (index = 0; list[index] != -1; index++)
      local->call_count++;
    local->call_count--; /* Reduce 1 for namespace entry */

    if (local->call_count) {
      /* Send chown request to all the nodes now */
      for (index = 0; list[index] != -1; index++) {
	if (priv->xl_array[list[index]] != NS(this)) {
	  loc_t tmp_loc = {
	    .inode = local->inode,
	    .path = local->path,
	  };
	  STACK_WIND (bg_frame,
		      unify_bg_buf_cbk,
		      priv->xl_array[list[index]],
		      priv->xl_array[list[index]]->fops->chown,
		      &tmp_loc,
		      local->uid,
		      local->gid);
	}
      }
    } else {
      unify_local_wipe (local);
      STACK_DESTROY (bg_frame->root);
    }
  } else {
    /* Its not a directory, so copy will be present only on one storage node */
    for (index = 0; list[index] != -1; index++)
      local->call_count++;
    local->call_count--; //for namespace

    if (local->call_count) {
      for (index = 0; list[index] != -1; index++) {
	if (priv->xl_array[list[index]] != NS(this)) {
	  loc_t tmp_loc = {
	    .path = local->path, 
	    .inode = local->inode
	  };
	  STACK_WIND (frame,
		      unify_buf_cbk,
		      priv->xl_array[list[index]],
		      priv->xl_array[list[index]]->fops->chown,
		      &tmp_loc,
		      local->uid,
		      local->gid);
	}
      }
    } else {
      STACK_UNWIND (frame, 0, 0, &local->stbuf);
    }
  }

  return 0;
}

/**
 * unify_chown - 
 */
int32_t
unify_chown (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     uid_t uid,
	     gid_t gid)
{
  unify_local_t *local = NULL;
  
  UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

  /* Initialization */
  INIT_LOCAL (frame, local);
  local->inode = loc->inode;
  local->uid = uid;
  local->gid = gid;
  local->path = strdup (loc->path);
  if (!local->path) {
    gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  local->list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  STACK_WIND (frame,
	      unify_ns_chown_cbk,
	      NS(this),
	      NS(this)->fops->chown,
	      loc,
	      uid,
	      gid);

  return 0;
}

/**
 * unify_ns_truncate_cbk - 
 */
STATIC int32_t
unify_ns_truncate_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct stat *buf)
{
  call_frame_t *bg_frame = NULL;
  unify_local_t *local = frame->local;
  unify_private_t *priv = this->private;
  int16_t *list = NULL;
  int16_t index = 0;

  if (op_ret == -1) {
    /* No need to send truncate request to other servers, 
     * as namespace action failed 
     */
    unify_local_wipe (local);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = op_ret;
  local->stbuf = *buf;

  list = local->list;

  /* If directory, get a copy of the current frame, and set 
   * the current local to bg_frame's local 
   */
  if (S_ISDIR (buf->st_mode)) {
    bg_frame = copy_frame (frame);
    frame->local = NULL;
    bg_frame->local = local;
    LOCK_INIT (&bg_frame->lock);
    
    /* Unwind this frame, and continue with bg_frame */
    STACK_UNWIND (frame, op_ret, op_errno, buf);
    
    /* Send chmod request to all the nodes now */
    local->call_count = 0;
    for (index = 0; list[index] != -1; index++)
      local->call_count++;
    local->call_count--; /* Reduce 1 for namespace entry */
    
    if (local->call_count) {
      for (index = 0; list[index] != -1; index++) {
	if (priv->xl_array[list[index]] != NS(this)) {
	  loc_t tmp_loc = {
	    .inode = local->inode,
	    .path = local->path,
	  };
	  STACK_WIND (bg_frame,
		      unify_bg_buf_cbk,
		      priv->xl_array[list[index]],
		      priv->xl_array[list[index]]->fops->truncate,
		      &tmp_loc,
		      local->offset);
	}
      }
    } else {
      unify_local_wipe (local);
      STACK_DESTROY (bg_frame->root);
    }
  } else {
    /* Its not a directory, so copy will be present only on one storage node */
    for (index = 0; list[index] != -1; index++)
      local->call_count++;
    local->call_count--; //for namespace

    if (local->call_count) {
      for (index = 0; list[index] != -1; index++) {
	if (priv->xl_array[list[index]] != NS(this)) {
	  loc_t tmp_loc = {
	    .path = local->path, 
	    .inode = local->inode
	  };
	  STACK_WIND (frame,
		      unify_buf_cbk,
		      priv->xl_array[list[index]],
		      priv->xl_array[list[index]]->fops->truncate,
		      &tmp_loc,
		      local->offset);
	}
      }
    } else {
      STACK_UNWIND (frame, 0, 0, &local->stbuf);
    }
  }

  return 0;
}

/**
 * unify_truncate - 
 */
int32_t
unify_truncate (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		off_t offset)
{
  unify_local_t *local = NULL;
  
  UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

  /* Initialization */
  INIT_LOCAL (frame, local);
  local->inode = loc->inode;
  local->offset = offset;
  local->path = strdup (loc->path);
  if (!local->path) {
    gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  local->list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  STACK_WIND (frame,
	      unify_ns_truncate_cbk,
	      NS(this),
	      NS(this)->fops->truncate,
	      loc,
	      offset);

  return 0;
}

/**
 * unify_ns_utimens_cbk -
 */
int32_t 
unify_ns_utimens_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *buf)
{
  call_frame_t *bg_frame = NULL;
  unify_local_t *local = frame->local;
  unify_private_t *priv = this->private;
  int16_t *list = NULL;
  int16_t index = 0;

  if (op_ret == -1) {
    /* No need to send chmod request to other servers, 
     * as namespace action failed 
     */
    unify_local_wipe (local);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = 0;
  local->stbuf = *buf;

  list = local->list;

  /* If directory, get a copy of the current frame, and set 
   * the current local to bg_frame's local 
   */
  if (S_ISDIR (buf->st_mode)) {
    bg_frame = copy_frame (frame);
    frame->local = NULL;
    bg_frame->local = local;
    LOCK_INIT (&bg_frame->lock);
    
    /* Unwind this frame, and continue with bg_frame */
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    
    /* Send chmod request to all the nodes now */
    local->call_count = 0;
    for (index = 0; list[index] != -1; index++)
      local->call_count++;
    local->call_count--; /* Reduce 1 for namespace entry */

    if (local->call_count) {
      for (index = 0; list[index] != -1; index++) {
	if (priv->xl_array[list[index]] != NS(this)) {
	  loc_t tmp_loc = {
	    .inode = local->inode,
	    .path = local->path,
	  };
	  STACK_WIND (bg_frame,
		      unify_bg_buf_cbk,
		      priv->xl_array[list[index]],
		      priv->xl_array[list[index]]->fops->utimens,
		      &tmp_loc,
		      local->tv);
	}
      }
    } else {
      unify_local_wipe (local);
      STACK_DESTROY (bg_frame->root);
    }
  } else {
    /* Its not a directory */
    for (index = 0; list[index] != -1; index++)
      local->call_count++;
    local->call_count--;

    if (local->call_count) {
      for (index = 0; list[index] != -1; index++) {
	if (priv->xl_array[list[index]] != NS(this)) {
	  loc_t tmp_loc = {
	    .path = local->path, 
	    .inode = local->inode
	  };
	  STACK_WIND (frame,
		      unify_buf_cbk,
		      priv->xl_array[list[index]],
		      priv->xl_array[list[index]]->fops->utimens,
		      &tmp_loc,
		      local->tv);
	}
      }
    } else {
      STACK_UNWIND (frame, 0, 0, &local->stbuf);
    }
  }
  
  return 0;
}

/**
 * unify_utimens - 
 */
int32_t 
unify_utimens (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       struct timespec tv[2])
{
  unify_local_t *local = NULL;
  
  UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

  /* Initialization */
  INIT_LOCAL (frame, local);
  local->inode = loc->inode;
  memcpy (local->tv, tv, 2 * sizeof (struct timespec));
  local->path = strdup (loc->path);
  if (!local->path) {
    gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }

  local->list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  STACK_WIND (frame,
	      unify_ns_utimens_cbk,
	      NS(this),
	      NS(this)->fops->utimens,
	      loc,
	      tv);
  
  return 0;
}

/**
 * unify_readlink_cbk - 
 */
STATIC int32_t
unify_readlink_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    const char *path)
{
  STACK_UNWIND (frame, op_ret, op_errno, path);
  return 0;
}

/**
 * unify_readlink - Read the link only from the storage node.
 */
int32_t
unify_readlink (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		size_t size)
{
  unify_private_t *priv = this->private;
  int32_t entry_count = 0;
  int16_t *list = NULL;
  int16_t index = 0;
  
  UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  for (index = 0; list[index] != -1; index++)
    entry_count++;

  if (entry_count == 2) {
    for (index = 0; list[index] != -1; index++) {
      if (priv->xl_array[list[index]] != NS(this)) {
	STACK_WIND (frame,
		    unify_readlink_cbk,
		    priv->xl_array[list[index]],
		    priv->xl_array[list[index]]->fops->readlink,
		    loc,
		    size);
	break;
      }
    }
  } else {
    STACK_UNWIND (frame, -1, ENOENT, NULL);
  }
  return 0;
}


/**
 * unify_unlink_cbk - 
 */
STATIC int32_t
unify_unlink_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    if (op_ret == 0)
      local->op_ret = 0;
  }
  UNLOCK (&frame->lock);

  if (!callcnt) {
    unify_local_wipe (local);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}


/**
 * unify_unlink - 
 */
int32_t
unify_unlink (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  unify_private_t *priv = this->private;
  unify_local_t *local = NULL;
  int16_t *list = NULL;
  int16_t index = 0;

  UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

  /* Initialization */
  INIT_LOCAL (frame, local);
  local->inode = loc->inode;

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  for (index = 0; list[index] != -1; index++)
    local->call_count++;

  for (index = 0; list[index] != -1; index++) {
    STACK_WIND (frame,
		unify_unlink_cbk,
		priv->xl_array[list[index]],
		priv->xl_array[list[index]]->fops->unlink,
		loc);
  }

  return 0;
}


/**
 * unify_readv_cbk - 
 */
STATIC int32_t
unify_readv_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct iovec *vector,
		 int32_t count,
		 struct stat *stbuf)
{
  STACK_UNWIND (frame, op_ret, op_errno, vector, count, stbuf);
  return 0;
}

/**
 * unify_readv - 
 */
int32_t
unify_readv (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     size_t size,
	     off_t offset)
{
  xlator_t *child = NULL;

  UNIFY_CHECK_FD_CTX_AND_UNWIND_ON_ERR (fd);

  child = data_to_ptr (dict_get (fd->ctx, this->name));

  STACK_WIND (frame,
	      unify_readv_cbk,
	      child,
	      child->fops->readv,
	      fd,
	      size,
	      offset);


  return 0;
}

/**
 * unify_writev_cbk - 
 */
STATIC int32_t
unify_writev_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

/**
 * unify_writev - 
 */
int32_t
unify_writev (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      struct iovec *vector,
	      int32_t count,
	      off_t off)
{
  xlator_t *child = NULL;

  UNIFY_CHECK_FD_CTX_AND_UNWIND_ON_ERR (fd);

  child = data_to_ptr (dict_get (fd->ctx, this->name));

  STACK_WIND (frame,
	      unify_writev_cbk,
	      child,
	      child->fops->writev,
	      fd,
	      vector,
	      count,
	      off);

  return 0;
}

/**
 * unify_ftruncate -
 */
int32_t
unify_ftruncate (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 off_t offset)
{
  xlator_t *child = NULL;
  unify_local_t *local = NULL;

  UNIFY_CHECK_FD_CTX_AND_UNWIND_ON_ERR(fd);

  /* Initialization */
  INIT_LOCAL (frame, local);

  child = data_to_ptr (dict_get (fd->ctx, this->name));
  local->call_count = 2;
  
  STACK_WIND (frame, unify_buf_cbk, 
	      child, child->fops->ftruncate,
	      fd, offset);
  
  STACK_WIND (frame, unify_buf_cbk, 
	      NS(this), NS(this)->fops->ftruncate,
	      fd, offset);
  
  return 0;
}


/**
 * unify_fchmod - 
 */
int32_t 
unify_fchmod (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      mode_t mode)
{
  unify_local_t *local = NULL;
  unify_private_t *priv = this->private;

  UNIFY_CHECK_FD_AND_UNWIND_ON_ERR(fd);

  /* Initialization */
  INIT_LOCAL (frame, local);

  if (dict_get (fd->ctx, this->name)) {
    /* If its set, then its file */
    xlator_t *child = NULL;

    child = data_to_ptr (dict_get (fd->ctx, this->name));
    local->call_count = 2;

    STACK_WIND (frame,
		unify_buf_cbk,
		child,
		child->fops->fchmod,
		fd,
		mode);

    STACK_WIND (frame,
		unify_buf_cbk,
		NS(this),
		NS(this)->fops->fchmod,
		fd,
		mode);
  } else {
    /* this is an directory */
    int16_t *list = NULL;
    int16_t index = 0;

    if (dict_get (fd->inode->ctx, this->name)) {
      list = data_to_ptr (dict_get (fd->inode->ctx, this->name));
    } else {
      STACK_UNWIND (frame, -1, EINVAL, NULL);
      return 0;
    }
    for (index = 0; list[index] != -1; index++)
      local->call_count++;
    
    for (index = 0; list[index] != -1; index++) {
      STACK_WIND (frame,
		  unify_buf_cbk,
		  priv->xl_array[list[index]],
		  priv->xl_array[list[index]]->fops->fchmod,
		  fd,
		  mode);
    }
  }

  return 0;
}

/**
 * unify_fchown - 
 */
int32_t 
unify_fchown (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      uid_t uid,
	      gid_t gid)
{
  unify_local_t *local = NULL;
  unify_private_t *priv = this->private;

  UNIFY_CHECK_FD_AND_UNWIND_ON_ERR(fd);

  /* Initialization */
  INIT_LOCAL (frame, local);

  if (dict_get (fd->ctx, this->name)) {
    /* If its set, then its file */
    xlator_t *child = NULL;

    child = data_to_ptr (dict_get (fd->ctx, this->name));
    local->call_count = 2;

    STACK_WIND (frame,
		unify_buf_cbk,
		child,
		child->fops->fchown,
		fd,
		uid,
		gid);

    STACK_WIND (frame,
		unify_buf_cbk,
		NS(this),
		NS(this)->fops->fchown,
		fd,
		uid,
		gid);
  } else {
    /* this is an directory */
    int16_t *list = NULL;
    int16_t index = 0;

    if (dict_get (fd->inode->ctx, this->name)) {
      list = data_to_ptr (dict_get (fd->inode->ctx, this->name));
    } else {
      STACK_UNWIND (frame, -1, EINVAL, NULL);
      return 0;
    }
    for (index = 0; list[index] != -1; index++)
      local->call_count++;
    
    for (index = 0; list[index] != -1; index++) {
      STACK_WIND (frame,
		  unify_buf_cbk,
		  priv->xl_array[list[index]],
		  priv->xl_array[list[index]]->fops->fchown,
		  fd,
		  uid,
		  gid);
    }
  }
  
  return 0;
}

/**
 * unify_flush_cbk - 
 */
STATIC int32_t
unify_flush_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/**
 * unify_flush -
 */
int32_t
unify_flush (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd)
{
  xlator_t *child = NULL;

  UNIFY_CHECK_FD_CTX_AND_UNWIND_ON_ERR(fd);

  child = data_to_ptr (dict_get (fd->ctx, this->name));

  STACK_WIND (frame,
	      unify_flush_cbk,
	      child,
	      child->fops->flush,
	      fd);

  return 0;
}

/**
 * unify_close_cbk -
 */
STATIC int32_t
unify_close_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    if (op_ret >= 0) 
      local->op_ret = op_ret;
  }
  UNLOCK (&frame->lock);
  
  if (!callcnt) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}

/**
 * unify_close - send 'close' fop to all the nodes where 'fd' is open.
 */
int32_t
unify_close (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd)
{
  unify_local_t *local = NULL;
  xlator_t *child = NULL;

  UNIFY_CHECK_FD_CTX_AND_UNWIND_ON_ERR(fd);

  /* Init */
  INIT_LOCAL (frame, local);
  local->inode = fd->inode;
  local->fd = fd;

  child = data_to_ptr (dict_get (fd->ctx, this->name));
  dict_del (fd->ctx, this->name);

  local->call_count = 2;

  /* to storage node */
  STACK_WIND (frame,
	      unify_close_cbk,
	      child,
	      child->fops->close,
	      fd);
  /* to namespace */
  STACK_WIND (frame,
	      unify_close_cbk,
	      NS(this),
	      NS(this)->fops->close,
	      fd);

  return 0;
}

/**
 * unify_fsync_cbk - 
 */
STATIC int32_t
unify_fsync_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/**
 * unify_fsync - 
 */
int32_t
unify_fsync (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     int32_t flags)
{
  xlator_t *child = NULL;

  UNIFY_CHECK_FD_CTX_AND_UNWIND_ON_ERR(fd);

  child = data_to_ptr (dict_get (fd->ctx, this->name));

  STACK_WIND (frame,
	      unify_fsync_cbk,
	      child,
	      child->fops->fsync,
	      fd,
	      flags);

  return 0;
}

/**
 * unify_fstat - Send fstat FOP to Namespace only if its directory, and to 
 *     both namespace and the storage node if its a file.
 */
int32_t
unify_fstat (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd)
{
  unify_local_t *local = NULL;
  unify_private_t *priv = this->private;

  UNIFY_CHECK_FD_AND_UNWIND_ON_ERR(fd);

  INIT_LOCAL (frame, local);
  if (dict_get (fd->ctx, this->name)) {
    /* If its set, then its file */
    xlator_t *child = NULL;

    child = data_to_ptr (dict_get (fd->ctx, this->name));
    local->call_count = 2;

    STACK_WIND (frame,
		unify_buf_cbk,
		child,
		child->fops->fstat,
		fd);

    STACK_WIND (frame,
		unify_buf_cbk,
		NS(this),
		NS(this)->fops->fstat,
		fd);
  } else {
    /* this is an directory */
    int16_t *list = NULL;
    int16_t index = 0;

    if (dict_get (fd->inode->ctx, this->name)) {
      list = data_to_ptr (dict_get (fd->inode->ctx, this->name));
    } else {
      STACK_UNWIND (frame, -1, EINVAL, NULL);
      return 0;
    }
    for (index = 0; list[index] != -1; index++)
      local->call_count++;
    
    for (index = 0; list[index] != -1; index++) {
      STACK_WIND (frame,
		  unify_buf_cbk,
		  priv->xl_array[list[index]],
		  priv->xl_array[list[index]]->fops->fstat,
		  fd);
    }
  }

  return 0;
}

/**
 * unify_readdir_cbk - 
 */
STATIC int32_t
unify_readdir_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   dir_entry_t *entry,
		   int32_t count)
{
  STACK_UNWIND (frame, op_ret, op_errno, entry, count);
  return 0;
}

/**
 * unify_readdir - send the FOP request to all the nodes.
 */
int32_t
unify_readdir (call_frame_t *frame,
	       xlator_t *this,
	       size_t size,
	       off_t offset,
	       fd_t *fd)
{
  UNIFY_CHECK_FD_AND_UNWIND_ON_ERR (fd);

  STACK_WIND (frame,
	      unify_readdir_cbk,
	      NS(this),
	      NS(this)->fops->readdir,
	      size,
	      offset,
	      fd);

  return 0;
}

/**
 * unify_closedir_cbk - 
 */
STATIC int32_t
unify_closedir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    if (op_ret == 0)
      local->op_ret = 0;
  }
  UNLOCK (&frame->lock);
  
  if (!callcnt) {
    STACK_UNWIND (frame, op_ret, op_errno);
  }
  
  return 0;
}

/**
 * unify_closedir - send 'closedir' fop to all the nodes, where 'fd' is open. 
 */
int32_t
unify_closedir (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd)
{
  int16_t *list = NULL;
  int16_t index = 0;
  unify_local_t *local = NULL;
  unify_private_t *priv = this->private;

  UNIFY_CHECK_FD_AND_UNWIND_ON_ERR (fd);

  INIT_LOCAL (frame, local);
  
  if (dict_get (fd->inode->ctx, this->name)) {
    list = data_to_ptr (dict_get (fd->inode->ctx, this->name));
  } else {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  for (index = 0; list[index] != -1; index++)
    local->call_count++;
  
  for (index = 0; list[index] != -1; index++) {
    STACK_WIND (frame,
		unify_closedir_cbk,
		priv->xl_array[list[index]],
		priv->xl_array[list[index]]->fops->closedir,
		fd);
  }

  return 0;
}

/**
 * unify_fsyncdir_cbk - 
 */
STATIC int32_t
unify_fsyncdir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    
    if (op_ret == -1 && op_errno != CHILDDOWN)
      local->op_errno = op_errno;
    
    if (op_ret == 0) 
      local->op_ret = 0;
  }
  UNLOCK (&frame->lock);
  
  if (!callcnt) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

/**
 * unify_fsyncdir -
 */
int32_t
unify_fsyncdir (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		int32_t flags)
{
  int16_t *list = NULL;
  int16_t index = 0;
  unify_local_t *local = NULL;
  unify_private_t *priv = this->private;

  UNIFY_CHECK_FD_AND_UNWIND_ON_ERR (fd);

  INIT_LOCAL (frame, local);
  
  if (dict_get (fd->inode->ctx, this->name)) {
    list = data_to_ptr (dict_get (fd->inode->ctx, this->name));
  } else {
    STACK_UNWIND (frame, -1, EINVAL, NULL);
    return 0;
  }
  for (index = 0; list[index] != -1; index++)
    local->call_count++;
  
  for (index = 0; list[index] != -1; index++) {
    STACK_WIND (frame,
		unify_fsyncdir_cbk,
		priv->xl_array[list[index]],
		priv->xl_array[list[index]]->fops->fsyncdir,
		fd,
		flags);
  }

  return 0;
}

/**
 * unify_lk_cbk - UNWIND frame with the proper return arguments.
 */
STATIC int32_t
unify_lk_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct flock *lock)
{
  STACK_UNWIND (frame, op_ret, op_errno, lock);
  return 0;
}

/**
 * unify_lk - Send it to all the storage nodes, (should be 1) which has file.
 */
int32_t
unify_lk (call_frame_t *frame,
	  xlator_t *this,
	  fd_t *fd,
	  int32_t cmd,
	  struct flock *lock)
{
  xlator_t *child = NULL;

  UNIFY_CHECK_FD_CTX_AND_UNWIND_ON_ERR (fd);

  child = data_to_ptr (dict_get (fd->ctx, this->name));

  STACK_WIND (frame,
	      unify_lk_cbk,
	      child,
	      child->fops->lk,
	      fd,
	      cmd,
	      lock);

  return 0;
}

/**
 * unify_setxattr_cbk - When all the child nodes return, UNWIND frame.
 */
STATIC int32_t
unify_setxattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    
    if (op_ret == -1 && op_errno != CHILDDOWN)
      local->op_errno = op_errno;
    
    if (op_ret == 0)
      local->op_ret = 0;
  }
  UNLOCK (&frame->lock);
  
  if (!callcnt) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}

/**
 * unify_sexattr - This function should be sent to all the storage nodes, which 
 *    contains the file, (excluding namespace).
 */
int32_t
unify_setxattr (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		dict_t *dict,
		int32_t flags)
{
  unify_private_t *priv = this->private;
  unify_local_t *local = NULL;
  int16_t *list = NULL;
  int16_t index = 0;

  UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

  /* Initialization */
  INIT_LOCAL (frame, local);

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  for (index = 0; list[index] != -1; index++)
    local->call_count++;
  local->call_count--; //don't do it on namespace
 
  if (local->call_count) {
    for (index = 0; list[index] != -1; index++) {
      if (priv->xl_array[list[index]] != NS(this)) {
	STACK_WIND (frame,
		    unify_setxattr_cbk,
		    priv->xl_array[list[index]],
		    priv->xl_array[list[index]]->fops->setxattr,
		    loc,
		    dict,
		    flags);
      }
    }
  } else {
    /* No entry in storage nodes */
    STACK_UNWIND (frame, -1, ENOENT);
  }

  return 0;
}


/**
 * unify_getxattr_cbk - This function is called from only one child, so, no
 *     need of any lock or anything else, just send it to above layer 
 */
STATIC int32_t
unify_getxattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    dict_t *value)
{
  STACK_UNWIND (frame, op_ret, op_errno, value);
  return 0;
}


/** 
 * unify_getxattr - This FOP is sent to only the storage node.
 */
int32_t
unify_getxattr (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc)
{
  unify_private_t *priv = this->private;
  int16_t *list = NULL;
  int16_t index = 0;
  int16_t count = 0;

  UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  for (index = 0; list[index] != -1; index++)
    count++;
  count--; //done for namespace entry

  if (count) {
    for (index = 0; list[index] != -1; index++) {
      if (priv->xl_array[list[index]] != NS(this)) {
	STACK_WIND (frame,
		    unify_getxattr_cbk,
		    priv->xl_array[list[index]],
		    priv->xl_array[list[index]]->fops->getxattr,
		    loc);
	break;
      }
    }
  } else {
    dict_t *tmp_dict = get_new_dict ();
    STACK_UNWIND (frame, 0, 0, tmp_dict);
    dict_destroy (tmp_dict);
  }

  return 0;
}

/**
 * unify_removexattr_cbk - Wait till all the child node returns the call and then
 *    UNWIND to above layer.
 */
STATIC int32_t
unify_removexattr_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  { 
    callcnt = --local->call_count;
    if (op_ret == -1)
      local->op_errno = op_errno;
    if (op_ret == 0)
      local->op_ret = 0;
  }
  UNLOCK (&frame->lock);  

  if (!callcnt) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}

/**
 * unify_removexattr - Send it to all the child nodes which has the files.
 */
int32_t
unify_removexattr (call_frame_t *frame,
		   xlator_t *this,
		   loc_t *loc,
		   const char *name)
{
  unify_private_t *priv = this->private;
  unify_local_t *local = NULL;
  int16_t *list = NULL;
  int16_t index = 0;

  UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

  /* Initialization */
  INIT_LOCAL (frame, local);

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  for (index = 0; list[index] != -1; index++)
    local->call_count++;
  local->call_count--; /* on NS its not done */

  if (local->call_count) {
    for (index = 0; list[index] != -1; index++) {
      if (priv->xl_array[list[index]] != NS(this)) {
	STACK_WIND (frame,
		    unify_removexattr_cbk,
		    priv->xl_array[list[index]],
		    priv->xl_array[list[index]]->fops->removexattr,
		    loc,
		    name);
      }
    }
  } else {
    STACK_UNWIND (frame, -1, ENOENT);
  }

  return 0;
}


/**
 * unify_mknod_cbk - 
 */
STATIC int32_t
unify_mknod_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *buf)
{
  unify_local_t *local = frame->local;

  if (op_ret >= 0) {
  }
  unify_local_wipe (local);
  STACK_UNWIND (frame, op_ret, op_errno, inode, &local->stbuf);

  return 0;
}

/**
 * unify_ns_mknod_cbk - 
 */
STATIC int32_t
unify_ns_mknod_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf)
{
  struct sched_ops *sched_ops = NULL;
  xlator_t *sched_xl = NULL;
  unify_local_t *local = frame->local;
  unify_private_t *priv = this->private;
  int16_t *list = NULL;
  int16_t index = 0;

  if (op_ret == -1) {
    /* No need to send mknod request to other servers, 
     * as namespace action failed 
     */
    unify_local_wipe (local);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  inode,
		  buf);
    return 0;
  }
  
  /* Create one inode for this entry */
  local->op_ret = 0;
  local->stbuf = *buf;
  
  list = calloc (1, sizeof (int16_t) * 3);
  list[0] = priv->child_count;
  list[2] = -1;
  dict_set (inode->ctx, this->name, data_from_ptr (list));

  sched_ops = priv->sched_ops;

  /* Send mknod request to scheduled node now */
  sched_xl = sched_ops->schedule (this, 0); 
  for (index = 0; index < priv->child_count; index++)
    if (sched_xl == priv->xl_array[index])
      break;
  list[1] = index;
  
  {
    loc_t tmp_loc = {
      .inode = inode,
      .path = local->name
    };
    _STACK_WIND (frame,
		 unify_mknod_cbk,
		 sched_xl,
		 sched_xl,
		 sched_xl->fops->mknod,
		 &tmp_loc,
		 local->mode,
		 local->dev);
  }

  return 0;
}

/**
 * unify_mknod - Create a device on namespace first, and later create on 
 *       the storage node.
 */
int32_t
unify_mknod (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode,
	     dev_t rdev)
{
  unify_local_t *local = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->mode = mode;
  local->dev = rdev;
  local->inode = loc->inode;
  local->name = strdup (loc->path);
  if (!local->name) {
    gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
    STACK_UNWIND (frame, -1, ENOMEM, loc->inode, NULL);
    return 0;
  }

  STACK_WIND (frame,
	      unify_ns_mknod_cbk,
	      NS(this),
	      NS(this)->fops->mknod,
	      loc,
	      mode,
	      rdev);

  return 0;
}

/**
 * unify_symlink_cbk - 
 */
STATIC int32_t
unify_symlink_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
		   struct stat *buf)
{
  unify_local_t *local = frame->local;
  
  unify_local_wipe (local);
  STACK_UNWIND (frame, op_ret, op_errno, inode, &local->stbuf);

  return 0;
}

/**
 * unify_ns_symlink_cbk - 
 */
STATIC int32_t
unify_ns_symlink_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      inode_t *inode,
		      struct stat *buf)
{

  struct sched_ops *sched_ops = NULL;
  xlator_t *sched_xl = NULL;
  int16_t *list = NULL;
  unify_local_t *local = frame->local;
  unify_private_t *priv = this->private;
  int16_t index = 0;

  if (op_ret == -1) {
    /* No need to send symlink request to other servers, 
     * as namespace action failed 
     */
    unify_local_wipe (local);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  NULL,
		  buf);
    return 0;
  }
  
  /* Create one inode for this entry */
  local->op_ret = 0;
  local->stbuf = *buf;
  
  /* Start the mapping list */

  list = calloc (1, sizeof (int16_t) * 3);
  list[0] = priv->child_count; //namespace's index
  list[2] = -1;
  dict_set (inode->ctx, this->name, data_from_ptr (list));

  sched_ops = priv->sched_ops;

  /* Send symlink request to all the nodes now */
  sched_xl = sched_ops->schedule (this, 0); 
  for (index = 0; index < priv->child_count; index++)
    if (sched_xl == priv->xl_array[index])
      break;
  list[1] = index;

  {
    loc_t tmp_loc = {
      .inode = inode,
      .path = local->name
    };
    _STACK_WIND (frame,
		 unify_symlink_cbk,
		 sched_xl,
		 sched_xl,
		 sched_xl->fops->symlink,
		 local->path,
		 &tmp_loc);
  }

  return 0;
}

/**
 * unify_symlink - 
 */
int32_t
unify_symlink (call_frame_t *frame,
	       xlator_t *this,
	       const char *linkpath,
	       loc_t *loc)
{
  unify_local_t *local = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->inode = loc->inode;
  local->path = strdup (linkpath);
  local->name = strdup (loc->path);
  if (!local->path || !local->name) {
    gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
    STACK_UNWIND (frame, -1, ENOMEM, loc->inode, NULL);
    return 0;
  }

  STACK_WIND (frame,
	      unify_ns_symlink_cbk,
	      NS(this),
	      NS(this)->fops->symlink,
	      linkpath,
	      loc);

  return 0;
}

/* unify_rename_unlink_cbk () */
STATIC int32_t 
unify_rename_unlink_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
  unify_private_t *priv = this->private;
  unify_local_t *local = frame->local;
  int16_t *list = local->list;
  int16_t index = 0;

  /* Send 'fops->rename' request to all the nodes where 'oldloc->path' exists. 
   * The case of 'newloc' being existing is handled already.
   */
  list = local->list;
  local->call_count = 0;
  for (index = 0; list[index] != -1; index++)
    local->call_count++;
  local->call_count--; // minus one entry for namespace deletion which just happend

  for (index = 0; list[index] != -1; index++) {
    if (NS(this) != priv->xl_array[list[index]]) {
      loc_t tmp_loc = {
	.path = local->path,
	.inode = local->inode,
      };
      loc_t tmp_newloc = {
	.path = local->name,
	.inode = NULL,
      };
      STACK_WIND (frame,
		  unify_buf_cbk,
		  priv->xl_array[list[index]],
		  priv->xl_array[list[index]]->fops->rename,
		  &tmp_loc,
		  &tmp_newloc);
    }
  }
  return 0;
}

/**
 * unify_ns_rename_cbk - Namespace rename callback. 
 */
STATIC int32_t
unify_ns_rename_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
  unify_private_t *priv = this->private;
  unify_local_t *local = frame->local;
  int16_t *list = local->list;
  int16_t index = 0;

  if (op_ret == -1) {
    /* No need to send rename request to other servers, 
     * as namespace action failed 
     */
    unify_local_wipe (local);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = 0;
  local->stbuf = *buf;
  
  if (local->new_inode && !S_ISDIR(local->new_inode->st_mode)) {
    if (local->new_inode->ctx && 
	dict_get (local->new_inode->ctx, this->name)) {
      /* if the target path exists, and if its not directory, send unlink  
       * to the target (to the node where it resides). Check for ! directory 
       * is added, because, rename on namespace could have successed only if 
       * its an empty directory and it exists on all nodes. So, anyways, 
       * 'fops->rename' call will handle it.
       */
      local->call_count = 0;
      for (index = 0; list[index] != -1; index++)
	local->call_count++;
      local->call_count--; /* for namespace */

      list = data_to_ptr (dict_get (local->new_inode->ctx, this->name));

      for (index = 0; list[index] != -1; index++) {
	if (NS(this) != priv->xl_array[list[index]]) {
	  loc_t tmp_loc = {
	    .path = local->name,
	    .inode = local->new_inode,
	  };
	  STACK_WIND (frame,
		      unify_rename_unlink_cbk,
		      priv->xl_array[list[index]],
		      priv->xl_array[list[index]]->fops->unlink,
		      &tmp_loc);
	}
      }
      return 0;
    }
  }

  /* Send 'fops->rename' request to all the nodes where 'oldloc->path' exists. 
   * The case of 'newloc' being existing is handled already.
   */
  list = local->list;
  local->call_count = 0;
  for (index = 0; list[index] != -1; index++)
    local->call_count++;
  local->call_count--; // minus one entry for namespace deletion which just happend

  for (index = 0; list[index] != -1; index++) {
    if (NS(this) != priv->xl_array[list[index]]) {
      loc_t tmp_loc = {
	.path = local->path,
	.inode = local->inode,
      };
      loc_t tmp_newloc = {
	.path = local->name,
	.inode = NULL,
      };
      STACK_WIND (frame,
		  unify_buf_cbk,
		  priv->xl_array[list[index]],
		  priv->xl_array[list[index]]->fops->rename,
		  &tmp_loc,
		  &tmp_newloc);
    }
  }

  return 0;
}


/**
 * unify_rename - One of the tricky function. the 'oldloc' should have valid 
 * inode pointer. 'newloc' if exists, need to send an unlink to the node where
 * it exists (if its a file), otherwise, just rename is enough.
 */
int32_t
unify_rename (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *oldloc,
	      loc_t *newloc)
{
  unify_local_t *local = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->inode = oldloc->inode;

  /* if 'newloc->inode' is true, that means there is a file existing 
   * in that path. Anyways, send the rename request to the Namespace 
   * first with corresponding 'loc_t' values 
   */
  local->new_inode = newloc->inode;

  if (oldloc->inode->ctx && dict_get (oldloc->inode->ctx, this->name)) {
    local->list = data_to_ptr (dict_get (oldloc->inode->ctx, this->name));
  } else {
    STACK_UNWIND (frame, -1, ENOENT, NULL);
    return 0;
  }
   
  local->path = strdup (oldloc->path);
  local->name = strdup (newloc->path);
  if (!local->path || !local->name) {
    gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
    STACK_UNWIND (frame, -1, ENOMEM, NULL);
    return 0;
  }
  
  STACK_WIND (frame,
	      unify_ns_rename_cbk,
	      NS(this),
	      NS(this)->fops->rename,
	      oldloc,
	      newloc);

  return 0;
}

/**
 * unify_link_cbk -
 */
STATIC int32_t
unify_link_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		inode_t *inode,
		struct stat *buf)
{
  unify_local_t *local = frame->local;

  unify_local_wipe (local);
  STACK_UNWIND (frame, op_ret, op_errno, inode, &local->stbuf);

  return 0;
}

/**
 * unify_ns_link_cbk - 
 */
STATIC int32_t
unify_ns_link_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
		   struct stat *buf)
{
  unify_private_t *priv = this->private;
  unify_local_t *local = frame->local;
  int16_t *list = local->list;
  int16_t index = 0;

  if (op_ret == -1) {
    /* No need to send link request to other servers, 
     * as namespace action failed 
     */
    unify_local_wipe (local);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  inode,
		  buf);
    return 0;
  }

  /* Update inode for this entry */
  local->op_ret = 0;
  local->stbuf = *buf;

  /* Send link request to the node now */
  for (index = 0; list[index] != -1; index++) {
    if (priv->xl_array[list[index]] != NS (this)) {
      loc_t tmp_loc = {
	.inode = local->inode,
	.path = local->path,
      };
      STACK_WIND (frame,
		  unify_link_cbk,
		  priv->xl_array[list[index]],
		  priv->xl_array[list[index]]->fops->link,
		  &tmp_loc,
		  local->name);
    }
  }

  return 0;
}

/**
 * unify_link - 
 */
int32_t
unify_link (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    const char *newname)
{
  unify_local_t *local = NULL;

  UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

  /* Initialization */
  INIT_LOCAL (frame, local);
  local->inode = loc->inode;

  local->path = strdup (loc->path);
  local->name = strdup (newname);
  if (!local->path || !local->name) {
    gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
    STACK_UNWIND (frame, -1, ENOMEM, loc->inode, NULL);
    return 0;
  }

  local->list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  STACK_WIND (frame,
	      unify_ns_link_cbk,
	      NS(this),
	      NS(this)->fops->link,
	      loc,
	      newname);

  return 0;
}

/**
 * notify
 */
int32_t
notify (xlator_t *this,
        int32_t event,
        void *data,
        ...)
{
  unify_private_t *priv = this->private;
  struct sched_ops *sched = NULL;

  if (!priv) {
    default_notify (this, event, data);
    return 0;
  }

  sched = priv->sched_ops;    
  switch (event)
    {
    case GF_EVENT_CHILD_UP:
      {
	/* Call scheduler's update () to enable it for scheduling */
	sched->notify (this, event, data);
	
	LOCK (&priv->lock);
	{
	  /* Increment the inode's generation, which is used for self_heal */
	  ++priv->inode_generation;
	}
	UNLOCK (&priv->lock);
      }
      break;
    case GF_EVENT_CHILD_DOWN:
      {
	/* Call scheduler's update () to disable the child node 
	 * for scheduling
	 */
	sched->notify (this, event, data);
      }
      break;
    default:
      {
	default_notify (this, event, data);
      }
      break;
    }

  return 0;
}

/** 
 * init - This function is called first in the xlator, while initializing.
 *   All the config file options are checked and appropriate flags are set.
 *
 * @this - 
 */
int32_t 
init (xlator_t *this)
{
  int32_t count = 0;
  unify_private_t *_private = NULL; 
  xlator_list_t *trav = NULL;
  xlator_t *ns_xl = NULL;
  data_t *scheduler = NULL;
  data_t *namespace = NULL;
  data_t *self_heal = NULL;

  /* Check for number of child nodes, if there is no child nodes, exit */
  if (!this->children) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "No child nodes specified. check \"subvolumes \" option in spec file");
    return -1;
  }

  scheduler = dict_get (this->options, "scheduler");
  if (!scheduler) {
    gf_log (this->name, 
	    GF_LOG_ERROR, 
	    "\"option scheduler <x>\" is missing in spec file");
    return -1;
  }
  
  {
    /* Setting "option namespace <node>" */
    namespace = dict_get (this->options, "namespace");
    if(!namespace) {
      gf_log (this->name, 
	      GF_LOG_CRITICAL, 
	      "namespace option not specified, Exiting");
      return -1;
    }
    /* Search namespace in the child node, if found, exit */
    trav = this->children;
    while (trav) {
      if (strcmp (trav->xlator->name, namespace->data) == 0)
	break;
      trav = trav->next;
    }
    if (trav) {
      gf_log (this->name, 
	      GF_LOG_CRITICAL, 
	      "namespace node used as a subvolume, Exiting");
      return -1;
    }
      
    /* Search for the namespace node, if found, continue */
    ns_xl = this->next;
    while (ns_xl) {
      if (strcmp (ns_xl->name, namespace->data) == 0)
	break;
      ns_xl = ns_xl->next;
    }
    if (!ns_xl) {
      gf_log (this->name, 
	      GF_LOG_CRITICAL, 
	      "namespace node not found in spec file, Exiting");
      return -1;
    }
    
    gf_log (this->name, 
	    GF_LOG_DEBUG, 
	    "namespace node specified as %s", namespace->data);
  }  

  _private = calloc (1, sizeof (*_private));
  _private->sched_ops = get_scheduler (scheduler->data);
  _private->namespace = ns_xl;

  /* update _private structure */
  {
    trav = this->children;
    /* Get the number of child count */
    while (trav) {
      count++;
      trav = trav->next;
    }
    _private->child_count = count;   
    gf_log (this->name, 
	    GF_LOG_DEBUG, 
	    "Child node count is %d", count);

    _private->xl_array = calloc (1, sizeof (xlator_t) * (count + 1));

    count = 0;
    trav = this->children;
    while (trav) {
      _private->xl_array[count++] = trav->xlator;
      trav = trav->next;
    }
    _private->xl_array[count] = _private->namespace;

    _private->self_heal = 1;
    self_heal = dict_get (this->options, "self-heal");
    if (self_heal) {
      if (strcmp (self_heal->data, "off") == 0) {
	_private->self_heal = 0;
      }
    }
    
    /* self-heal part, start with generation '1' */
    LOCK_INIT (&_private->lock);
    _private->inode_generation = 1; 
  }

  this->private = (void *)_private;

  {
    int32_t ret;

    /* Initialize the scheduler, if everything else is successful */
    ret = _private->sched_ops->init (this); 
    if (ret == -1) {
      gf_log (this->name,
	      GF_LOG_CRITICAL,
	      "Initializing scheduler failed, Exiting");
      freee (_private);
      return -1;
    }
    
    ret = xlator_tree_init (ns_xl);
    if (!ret) {
      ns_xl->parent = this;
      ns_xl->notify (ns_xl, GF_EVENT_PARENT_UP, this);
    } else {
      gf_log (this->name, 
	      GF_LOG_CRITICAL, 
	      "initializing namespace node failed, Exiting");
      freee (_private);
      return -1;
    }
  }

  return 0;
}

/** 
 * fini  - Free all the allocated memory 
 */
void
fini (xlator_t *this)
{
  unify_private_t *priv = this->private;
  priv->sched_ops->fini (this);
  LOCK_DESTROY (&priv->lock);
  freee (priv->xl_array);
  freee (priv);
  return;
}


struct xlator_fops fops = {
  .stat        = unify_stat,
  .chmod       = unify_chmod,
  .readlink    = unify_readlink,
  .mknod       = unify_mknod,
  .mkdir       = unify_mkdir,
  .unlink      = unify_unlink,
  .rmdir       = unify_rmdir,
  .symlink     = unify_symlink,
  .rename      = unify_rename,
  .link        = unify_link,
  .chown       = unify_chown,
  .truncate    = unify_truncate,
  .create      = unify_create,
  .open        = unify_open,
  .readv       = unify_readv,
  .writev      = unify_writev,
  .statfs      = unify_statfs,
  .flush       = unify_flush,
  .close       = unify_close,
  .fsync       = unify_fsync,
  .setxattr    = unify_setxattr,
  .getxattr    = unify_getxattr,
  .removexattr = unify_removexattr,
  .opendir     = unify_opendir,
  .readdir     = unify_readdir,
  .closedir    = unify_closedir,
  .fsyncdir    = unify_fsyncdir,
  .access      = unify_access,
  .ftruncate   = unify_ftruncate,
  .fstat       = unify_fstat,
  .lk          = unify_lk,
  .fchown      = unify_fchown,
  .fchmod      = unify_fchmod,
  .utimens     = unify_utimens,
  .lookup      = unify_lookup,
  .forget      = unify_forget,
};

struct xlator_mops mops = {
  //  .stats = unify_stats
};
