/*
   Copyright (c) 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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
 * unify-self-heal.c : 
 *   This file implements few functions which enables 'unify' translator 
 *  to be consistent in its behaviour when 
 *     > a node fails, 
 *     > a node gets added, 
 *     > a failed node comes back
 *     > a new namespace server is added (ie, an fresh namespace server).
 * 
 *  This functionality of 'unify' will enable glusterfs to support storage system 
 *  failure, and maintain consistancy. This works both ways, ie, when an entry 
 *  (either file or directory) is found on namespace server, and not on storage 
 *  nodes, its created in storage nodes and vica-versa.
 * 
 *  The two fops, where it can be implemented are 'getdents ()' and 'lookup ()'
 *
 */

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "unify.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"
#include "common-utils.h"


/**
 * unify_background_cbk - this is called by the background functions which 
 *   doesn't return any of inode, or buf. eg: rmdir, unlink, close, etc.
 *
 */
int32_t 
unify_background_cbk (call_frame_t *frame,
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
    
    /* Destroy the fd here */
    if (local->fd)
      fd_unref (local->fd);
    
    STACK_DESTROY (frame->root);
  }

  return 0;
}



int32_t 
unify_sh_setdents_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
  int32_t callcnt = -1;
  unify_private_t *priv = this->private;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    /* if local->call_count == 0, that means, setdents on storagenodes is 
     * still pending.
     */
    if (local->call_count)
      callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (!callcnt && local->flags) {
    local->call_count = priv->child_count + 1; /* +1 is for namespace */
    
    fd_unref (local->fd);
    
    STACK_UNWIND (frame, 
		  local->op_ret, 
		  local->op_errno, 
		  local->inode,
		  &local->stbuf, 
		  local->dict);

  }
  
  return 0;
}


int32_t
unify_sh_ns_getdents_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  dir_entry_t *entry,
			  int32_t count)
{
  unify_local_t *local = frame->local;
  unify_private_t *priv = this->private;
  long index = 0;
  unsigned long final = 0;

  if ((count < UNIFY_SELF_HEAL_GETDENTS_COUNT) || !entry) {
    final = 1;
  } else {
    /* count == size, that means, there are more entries to read from */
    //local->call_count = 0;
    local->offset_list[0] += UNIFY_SELF_HEAL_GETDENTS_COUNT;
    STACK_WIND (frame,
		unify_sh_ns_getdents_cbk,
		NS(this),
		NS(this)->fops->getdents,
		local->fd,
		UNIFY_SELF_HEAL_GETDENTS_COUNT,
		local->offset_list[0],
		GF_GET_DIR_ONLY);
  }

  LOCK (&frame->lock);
  {
    /* local->call_count will be '0' till now. make it 1 so, it can be 
     * UNWIND'ed for the last call. 
     */
    local->call_count = priv->child_count;
    if (final)
      local->flags = 1;
  }
  UNLOCK (&frame->lock);

  for (index = 0; index < priv->child_count; index++) 
    {
      STACK_WIND (frame,
		  unify_sh_setdents_cbk, 
		  priv->xl_array[index],
		  priv->xl_array[index]->fops->setdents,
		  local->fd, GF_SET_DIR_ONLY,
		  entry, count);
    }

  return 0;
}

int32_t 
unify_sh_ns_setdents_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno)
{
  /* Nothing needs to be done here, as we still need to do setdents of 
   * storage nodes 
   */
  return 0;
}


/**
 * unify_sh_getdents_cbk -
 */
int32_t
unify_sh_getdents_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       dir_entry_t *entry,
		       int32_t count)
{
  int32_t callcnt = -1;
  unify_local_t *local = frame->local;
  unify_private_t *priv = this->private;
  long index = (long)cookie;
  
  if (op_ret >= 0 && count > 0) {
    /* There is some dentry found, just send the dentry to NS */

    STACK_WIND (frame,
		unify_sh_ns_setdents_cbk,
		NS(this),
		NS(this)->fops->setdents,
		local->fd,
		GF_SET_IF_NOT_PRESENT,
		entry,
		count);
  }
  
  if (count < UNIFY_SELF_HEAL_GETDENTS_COUNT) {
    LOCK (&frame->lock);
    {
      callcnt = --local->call_count;
    }
    UNLOCK (&frame->lock);
  } else {
    /* count == size, that means, there are more entries to read from */
    local->offset_list[index] += UNIFY_SELF_HEAL_GETDENTS_COUNT;
    STACK_WIND_COOKIE (frame,
		       unify_sh_getdents_cbk,
		       cookie,
		       priv->xl_array[index],
		       priv->xl_array[index]->fops->getdents,
		       local->fd,
		       UNIFY_SELF_HEAL_GETDENTS_COUNT,
		       local->offset_list[index],
		       GF_GET_ALL);
    
    gf_log (this->name, GF_LOG_DEBUG, "readdir on (%s) with offset %"PRId64"", 
	    priv->xl_array[index]->name, 
	    local->offset_list[index]);
  }

  if (!callcnt) {
    /* All storage nodes have done unified setdents on NS node.
     * Now, do getdents from NS and do setdents on storage nodes.
     */
    
    /* offset_list is no longer required for storage nodes now */
    local->offset_list[0] = 0; /* reset */

    STACK_WIND (frame,
		unify_sh_ns_getdents_cbk,
		NS(this),
		NS(this)->fops->getdents,
		local->fd,
		UNIFY_SELF_HEAL_GETDENTS_COUNT,
		0, /* In this call, do send '0' as offset */
		GF_GET_DIR_ONLY);
  }

  return 0;
}

/**
 * unify_sh_opendir_cbk -
 *
 * @cookie: 
 */
int32_t 
unify_sh_opendir_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      fd_t *fd)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;
  unify_private_t *priv = this->private;
  int16_t index = 0;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    
    if (op_ret >= 0) {
      local->op_ret = op_ret;
    } else {
      local->failed = 1;
    }
  }
  UNLOCK (&frame->lock);
  
  if (!callcnt) {
    local->call_count = priv->child_count + 1;
    
    if (!local->failed) {
      /* send getdents() namespace after finishing storage nodes */
      local->call_count--; 
      
      fd_bind (fd);

      if (local->call_count) {
	/* Used as the offset index. This list keeps track of offset
	 * sent to each node during STACK_WIND.
	 */
	local->offset_list = calloc (priv->child_count, sizeof (off_t));
	ERR_ABORT (local->offset_list);
	
	/* Send getdents on all the fds */
	for (index = 0; index < priv->child_count; index++) {
	  STACK_WIND_COOKIE (frame,
			     unify_sh_getdents_cbk,
			     (void *)(long)index,
			     priv->xl_array[index],
			     priv->xl_array[index]->fops->getdents,
			     local->fd,
			     UNIFY_SELF_HEAL_GETDENTS_COUNT,
			     0, /* In this call, do send '0' as offset */
			     GF_GET_ALL);
	}
	/* did a stack wind, so no need to unwind here */
	return 0;
      } /* (local->call_count) */
    } /* (!local->failed) */

    {
      /* Opendir failed on one node.
       */
      fd_unref (local->fd);

      FREE (local->path);
      FREE (local->sh_struct);

      /* Only 'self-heal' did not succeed, lookup() was successful. */
      local->op_ret = 0;
      
      /* This is lookup_cbk ()'s UNWIND. */
      STACK_UNWIND (frame, local->op_ret, local->op_errno, local->inode,
		    &local->stbuf, local->dict);
      
    }
  }

  return 0;
}

/**
 * gf_sh_checksum_cbk - 
 * 
 * @frame: frame used in lookup. get a copy of it, and use that copy.
 * @this: pointer to unify xlator.
 * @inode: pointer to inode, for which the consistency check is required.
 *
 */
int32_t 
unify_sh_checksum_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       uint8_t *file_checksum,
		       uint8_t *dir_checksum)
{
  unify_local_t *local = frame->local;
  unify_private_t *priv = this->private;
  int16_t index = 0;
  int32_t callcnt = 0;
  
  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    if (op_ret >= 0) {
      if (NS(this) == (xlator_t *)cookie) {
	memcpy (local->sh_struct->ns_file_checksum, file_checksum, GF_FILENAME_MAX);
	memcpy (local->sh_struct->ns_dir_checksum, dir_checksum, GF_FILENAME_MAX);
      } else {
	if (local->entry_count == 0) {
	  /* Initialize the dir_checksum to be used for comparision 
	   * with other storage nodes. Should be done for the first 
	   * successful call *only*. 
	   */
	  local->entry_count = 1; /* Using 'entry_count' as a flag */
	  memcpy (local->sh_struct->dir_checksum, dir_checksum, GF_FILENAME_MAX);
	}

	/* Reply from the storage nodes */
	for (index = 0; index < GF_FILENAME_MAX; index++) {
	  /* Files should be present in only one node */
	  local->sh_struct->file_checksum[index] ^= file_checksum[index];
	  
	  /* directory structure should be same accross */
	  if (local->sh_struct->dir_checksum[index] != dir_checksum[index])
	    local->failed = 1;
	}
      }
    } 
  }
  UNLOCK (&frame->lock);

  if (!callcnt) {
    for (index = 0; index < GF_FILENAME_MAX ; index++) {
      if (local->sh_struct->file_checksum[index] != local->sh_struct->ns_file_checksum[index]) {
	local->failed = 1;
	break;
      }
      if (local->sh_struct->dir_checksum[index] != local->sh_struct->ns_dir_checksum[index]) {
	local->failed = 1;
	break;
      }
    }
	
    if (local->failed) {
      /* Log it, it should be a rare event */
      gf_log (this->name, GF_LOG_WARNING, "Self-heal triggered on directory %s", local->path);

      /* Any self heal will be done at the directory level */
      local->call_count = 0;
      local->op_ret = -1;
      local->failed = 0;
      
      local->fd = fd_create (local->inode, frame->root->pid);

      local->call_count = priv->child_count + 1;
	
      for (index = 0; index < (priv->child_count + 1); index++) {
	loc_t tmp_loc = {
	  .inode = local->inode,
	  .path = local->path,
	};
	STACK_WIND_COOKIE (frame,
			   unify_sh_opendir_cbk,
			   priv->xl_array[index]->name,
			   priv->xl_array[index],
			   priv->xl_array[index]->fops->opendir,
			   &tmp_loc,
			   local->fd);
      }
      /* opendir can be done on the directory */
      return 0;
    }

    /* no mismatch */
    FREE (local->path);
    FREE (local->sh_struct);

    /* This is lookup_cbk ()'s UNWIND. */
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  local->inode,
		  &local->stbuf,
		  local->dict);
  }

  return 0;
}

/* Foreground self-heal part over */

/* Background self-heal part */



int32_t 
unify_bgsh_setdents_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
  int32_t callcnt = -1;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    /* if local->call_count == 0, that means, setdents on storagenodes is 
     * still pending.
     */
    if (local->call_count)
      callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (!callcnt && local->flags) {
    fd_unref (local->fd);
  }
  
  return 0;
}


int32_t
unify_bgsh_ns_getdents_cbk (call_frame_t *frame,
			    void *cookie,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno,
			    dir_entry_t *entry,
			    int32_t count)
{
  unify_local_t *local = frame->local;
  unify_private_t *priv = this->private;
  long index = 0;
  unsigned long final = 0;

  if ((count < UNIFY_SELF_HEAL_GETDENTS_COUNT) || !entry) {
    final = 1;
  } else {
    /* count == size, that means, there are more entries to read from */
    local->offset_list[0] += UNIFY_SELF_HEAL_GETDENTS_COUNT;
    STACK_WIND (frame,
		unify_bgsh_ns_getdents_cbk,
		NS(this),
		NS(this)->fops->getdents,
		local->fd,
		UNIFY_SELF_HEAL_GETDENTS_COUNT,
		local->offset_list[0],
		GF_GET_DIR_ONLY);
  }

  LOCK (&frame->lock);
  {
    /* local->call_count will be '0' till now. make it 1 so, it can be 
     * UNWIND'ed for the last call. 
     */
    local->call_count = priv->child_count;
    if (final)
      local->flags = 1;
  }
  UNLOCK (&frame->lock);

  for (index = 0; index < priv->child_count; index++) 
    {
      STACK_WIND (frame,
		  unify_bgsh_setdents_cbk, 
		  priv->xl_array[index],
		  priv->xl_array[index]->fops->setdents,
		  local->fd, GF_SET_DIR_ONLY,
		  entry, count);
    }

  return 0;
}

int32_t 
unify_bgsh_ns_setdents_cbk (call_frame_t *frame,
			    void *cookie,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno)
{
  /* Nothing needs to be done here, as we still need to do setdents of 
   * storage nodes 
   */
  return 0;
}


/**
 * unify_sh_getdents_cbk -
 */
int32_t
unify_bgsh_getdents_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 dir_entry_t *entry,
			 int32_t count)
{
  int32_t callcnt = -1;
  unify_local_t *local = frame->local;
  unify_private_t *priv = this->private;
  long index = (long)cookie;
  
  if (op_ret >= 0 && count > 0) {
    /* There is some dentry found, just send the dentry to NS */

    STACK_WIND (frame,
		unify_bgsh_ns_setdents_cbk,
		NS(this),
		NS(this)->fops->setdents,
		local->fd,
		GF_SET_IF_NOT_PRESENT,
		entry,
		count);
  }
  
  if (count < UNIFY_SELF_HEAL_GETDENTS_COUNT) {
    LOCK (&frame->lock);
    {
      callcnt = --local->call_count;
    }
    UNLOCK (&frame->lock);
  } else {
    /* count == size, that means, there are more entries to read from */
    local->offset_list[index] += UNIFY_SELF_HEAL_GETDENTS_COUNT;
    STACK_WIND_COOKIE (frame,
		       unify_bgsh_getdents_cbk,
		       cookie,
		       priv->xl_array[index],
		       priv->xl_array[index]->fops->getdents,
		       local->fd,
		       UNIFY_SELF_HEAL_GETDENTS_COUNT,
		       local->offset_list[index],
		       GF_GET_ALL);
    
    gf_log (this->name, GF_LOG_DEBUG, "readdir on (%s) with offset %"PRId64"", 
	    priv->xl_array[index]->name, 
	    local->offset_list[index]);
    
  }

  if (!callcnt) {
    /* All storage nodes have done unified setdents on NS node.
     * Now, do getdents from NS and do setdents on storage nodes.
     */
    
    /* offset_list is no longer required for storage nodes now */
    local->offset_list[0] = 0; /* reset */

    STACK_WIND (frame,
		unify_bgsh_ns_getdents_cbk,
		NS(this),
		NS(this)->fops->getdents,
		local->fd,
		UNIFY_SELF_HEAL_GETDENTS_COUNT,
		0, /* In this call, do send '0' as offset */
		GF_GET_DIR_ONLY);
  }

  return 0;
}

/**
 * unify_sh_opendir_cbk -
 *
 * @cookie: 
 */
int32_t 
unify_bgsh_opendir_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			fd_t *fd)
{
  unify_local_t *local = frame->local;
  unify_private_t *priv = this->private;
  int32_t callcnt = 0;
  int16_t index = 0;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    
    if (op_ret >= 0) {
      local->op_ret = op_ret;
    } else {
      local->failed = 1;
    }
  }
  UNLOCK (&frame->lock);
  
  if (!callcnt) {
    local->call_count = priv->child_count + 1;
    
    if (!local->failed) {
      /* send getdents() namespace after finishing storage nodes */
      local->call_count--; 
      callcnt = local->call_count;
      
      fd_bind (fd);

      if (local->call_count) {
	/* Used as the offset index. This list keeps track of offset
	 * sent to each node during STACK_WIND.
	 */
	local->offset_list = calloc (priv->child_count, sizeof (off_t));
	ERR_ABORT (local->offset_list);
	
	/* Send getdents on all the fds */
	for (index = 0; index < priv->child_count; index++) {
	  STACK_WIND_COOKIE (frame,
			     unify_bgsh_getdents_cbk,
			     (void *)(long)index,
			     priv->xl_array[index],
			     priv->xl_array[index]->fops->getdents,
			     local->fd,
			     UNIFY_SELF_HEAL_GETDENTS_COUNT,
			     0, /* In this call, do send '0' as offset */
			     GF_GET_ALL);
	}
	/* did a stack wind, so no need to unwind here */
	return 0;
      } /* (local->call_count) */
    } /* (!local->failed) */


    {
      /* Opendir failed on one node. 
       */
      fd_unref (local->fd);
      
      FREE (local->path);
      FREE (local->sh_struct);

      STACK_DESTROY (frame->root);
    }

  }

  return 0;
}

/**
 * gf_sh_checksum_cbk - 
 * 
 * @frame: frame used in lookup. get a copy of it, and use that copy.
 * @this: pointer to unify xlator.
 * @inode: pointer to inode, for which the consistency check is required.
 *
 */
int32_t 
unify_bgsh_checksum_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 uint8_t *file_checksum,
			 uint8_t *dir_checksum)
{
  unify_local_t *local = frame->local;
  unify_private_t *priv = this->private;
  int16_t index = 0;
  int32_t callcnt = 0;
  
  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    if (op_ret >= 0) {
      if (NS(this) == (xlator_t *)cookie) {
	memcpy (local->sh_struct->ns_file_checksum, file_checksum, GF_FILENAME_MAX);
	memcpy (local->sh_struct->ns_dir_checksum, dir_checksum, GF_FILENAME_MAX);
      } else {
	if (local->entry_count == 0) {
	  /* Initialize the dir_checksum to be used for comparision 
	   * with other storage nodes. Should be done for the first 
	   * successful call *only*. 
	   */
	  local->entry_count = 1; /* Using 'entry_count' as a flag */
	  memcpy (local->sh_struct->dir_checksum, dir_checksum, GF_FILENAME_MAX);
	}

	/* Reply from the storage nodes */
	for (index = 0; index < GF_FILENAME_MAX; index++) {
	  /* Files should be present in only one node */
	  local->sh_struct->file_checksum[index] ^= file_checksum[index];
	  
	  /* directory structure should be same accross */
	  if (local->sh_struct->dir_checksum[index] != dir_checksum[index])
	    local->failed = 1;
	}
      }
    } 
  }
  UNLOCK (&frame->lock);

  if (!callcnt) {
    for (index = 0; index < GF_FILENAME_MAX ; index++) {
      if (local->sh_struct->file_checksum[index] != local->sh_struct->ns_file_checksum[index]) {
	local->failed = 1;
	break;
      }
      if (local->sh_struct->dir_checksum[index] != local->sh_struct->ns_dir_checksum[index]) {
	local->failed = 1;
	break;
      }
    }
	
    if (local->failed) {
      /* Log it, it should be a rare event */
      gf_log (this->name, GF_LOG_WARNING, "Self-heal triggered on directory %s", local->path);

      /* Any self heal will be done at the directory level */
      local->op_ret = -1;
      local->failed = 0;
      
      local->fd = fd_create (local->inode, frame->root->pid);
      local->call_count = priv->child_count + 1;
	
      for (index = 0; index < (priv->child_count + 1); index++) {
	loc_t tmp_loc = {
	  .inode = local->inode,
	  .path = local->path,
	};
	STACK_WIND_COOKIE (frame,
			   unify_bgsh_opendir_cbk,
			   priv->xl_array[index]->name,
			   priv->xl_array[index],
			   priv->xl_array[index]->fops->opendir,
			   &tmp_loc,
			   local->fd);
      }
      
      /* opendir can be done on the directory */
      return 0;
    }

    /* no mismatch */
    FREE (local->path);
    FREE (local->sh_struct);

    STACK_DESTROY (frame->root);
  }

  return 0;
}

/* Background self-heal part over */




/**
 * gf_unify_self_heal - 
 * 
 * @frame: frame used in lookup. get a copy of it, and use that copy.
 * @this: pointer to unify xlator.
 * @inode: pointer to inode, for which the consistency check is required.
 *
 */
int32_t 
gf_unify_self_heal (call_frame_t *frame,
		    xlator_t *this,
		    unify_local_t *local)
{
  unify_private_t *priv = this->private;
  call_frame_t *bg_frame = NULL;
  unify_local_t *bg_local = NULL;
  int16_t index = 0;
  
  if (local->inode_generation < priv->inode_generation) {
    /* Any self heal will be done at the directory level */
    /* Update the inode's generation to the current generation value. */
    local->inode_generation = priv->inode_generation;
    dict_set (local->inode->ctx, this->name, data_from_int64 (local->inode_generation));

    if (priv->self_heal == GF_UNIFY_FG_SELF_HEAL) {
      local->op_ret = 0;
      local->failed = 0;
      local->call_count = priv->child_count + 1;
      local->sh_struct = calloc (1, sizeof (struct unify_self_heal_struct));
      
      /* +1 is for NS */
      for (index = 0; index < (priv->child_count + 1); index++) {
	loc_t tmp_loc = {
	  .inode = local->inode,
	  .path = local->path,
	};
	STACK_WIND_COOKIE (frame,
			   unify_sh_checksum_cbk,
			   priv->xl_array[index],
			   priv->xl_array[index],
			   priv->xl_array[index]->fops->checksum,
			   &tmp_loc,
			   0);
      }

      /* Self-heal in foreground, hence no need to UNWIND here */
      return 0;
    }

    /* Self Heal done in background */
    bg_frame = copy_frame (frame);
    INIT_LOCAL (bg_frame, bg_local);
    bg_local->inode = inode_ref (local->inode);
    bg_local->path = strdup (local->path);

    bg_local->op_ret = 0;
    bg_local->failed = 0;
    bg_local->call_count = priv->child_count + 1;
    bg_local->sh_struct = calloc (1, sizeof (struct unify_self_heal_struct));
    
    /* +1 is for NS */
    for (index = 0; index < (priv->child_count + 1); index++) {
      loc_t tmp_loc = {
	.inode = bg_local->inode,
	.path = bg_local->path,
      };
      STACK_WIND_COOKIE (bg_frame,
			 unify_bgsh_checksum_cbk,
			 priv->xl_array[index],
			 priv->xl_array[index],
			 priv->xl_array[index]->fops->checksum,
			 &tmp_loc,
			 0);
    }
  }

  FREE (local->path);
  
  /* generation number matches, self heal already done or self heal done in background:
   * just do STACK_UNWIND 
   */

  /* This is lookup_cbk ()'s UNWIND. */
  STACK_UNWIND (frame,
		local->op_ret,
		local->op_errno,
		local->inode,
		&local->stbuf,
		local->dict);
  
  return 0;
}

