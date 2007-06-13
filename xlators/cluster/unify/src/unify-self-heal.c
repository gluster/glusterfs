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
 *  The two fops, where it can be implemented are 'readdir ()' and 'lookup ()'
 *
 */

#include "glusterfs.h"
#include "unify.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"

/**
 * unify_sh_closedir_cbk -
 */
static int32_t
unify_sh_closedir_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
  int32_t callcnt;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->mutex);

  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
    STACK_DESTROY (frame->root);
  }
  return 0;
}

/**
 * unify_sh_readdir_cbk -
 */
static int32_t
unify_sh_readdir_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      dir_entry_t *entry,
		      int32_t count)
{
  int32_t callcnt, tmp_count;
  dir_entry_t *trav, *prev, *tmp, *unify_entry;
  unify_local_t *local = frame->local;
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;

  LOCK (&frame->mutex);
  {
    if (op_ret >= 0) {
      if ((xlator_t *)cookie != NS(this)) {
	/* For all the successful calls, come inside this block */
	local->op_ret = op_ret;
	trav = entry->next;
	prev = entry;
	if (local->entry == NULL) {
	  /* local->entry is NULL only for the first successful call. So, 
	   * take all the entries from that node. 
	   */
	  unify_entry = calloc (1, sizeof (dir_entry_t));
	  unify_entry->next = trav;
	  
	  while (trav->next)
	    trav = trav->next;
	  local->entry = unify_entry;
	  local->last = trav;
	  local->count = count;
	} else {
	  /* This block is true for all the call other than first successful call.
	   * So, take only file names from these entries, as directory entries are 
	   * already taken.
	   */
	  tmp_count = count;
	  while (trav) {
	    tmp = trav;
	    if (S_ISDIR (tmp->buf.st_mode)) {
	      /* If directory, check this entry is present in other nodes, 
	       * if yes, free the entry, otherwise, keep this entry.
	       */
	      int32_t flag = 0;
	      dir_entry_t *sh_trav = local->entry->next;
	      while (sh_trav) {
		if (strcmp (sh_trav->name, tmp->name) == 0) {
		  /* Found the directory name in earlier entries. */
		  flag = 1;
		  break;
		}
		sh_trav = sh_trav->next;
	      }
	      if (flag) {
		/* if its set, it means entry is already present, so remove entries 
		 * from current list.
		 */ 
		prev->next = tmp->next;
		trav = tmp->next;
		free (tmp->name);
		free (tmp);
		tmp_count--;
	      }
	      continue;
	    }
	    prev = trav;
	    trav = trav->next;
	  }
	  /* Append the 'entries' from this call at the end of the previously stored entry */
	  local->last->next = entry->next;
	  local->count += tmp_count;
	  while (local->last->next)
	    local->last = local->last->next;
	}
      } else {
	/* If its a _cbk from namespace, keep its entries seperate */
	local->ns_entry = entry->next;
	local->ns_count = count;
      }
      /* This makes child nodes to free only head, and all dir_entry_t structures 
       * are kept reference at this level.
       */
      entry->next = NULL;
    } 
    
    /* If there is an error, other than ENOTCONN, its failure */
    if ((op_ret == -1 && op_errno != ENOTCONN)) {
      local->op_ret = -1;
      local->op_errno = op_errno;
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->mutex);

  if (!callcnt) {
    /* Do basic level of self heal here */
    unify_readdir_self_heal (frame, this, local->fd, local);
    
    /* free the local->* */
    {
      /* Now free the entries stored at this level */
      prev = local->entry;
      if (prev) {
	trav = prev->next;
	while (trav) {
	  prev->next = trav->next;
	  free (trav->name);
	  free (trav);
	  trav = prev->next;
	}
	free (prev);
      }
      /* Now remove the entries of namespace */
      prev = local->ns_entry;
      if (prev) {
	trav = prev->next;
	while (trav) {
	  prev->next = trav->next;
	  free (trav->name);
	  free (trav);
	  trav = prev->next;
	}
	free (prev);
      }
    }
    {
      data_t *child_fd_data = NULL;
      list = local->inode->private;
      list_for_each_entry (ino_list, list, list_head)
	local->call_count++;
      
      list_for_each_entry (ino_list, list, list_head) {
	child_fd_data = dict_get (local->fd->ctx, ino_list->xl->name);
	if (child_fd_data) {
	  STACK_WIND (frame,
		      unify_sh_closedir_cbk,
		      ino_list->xl,
		      ino_list->xl->fops->closedir,
		      data_to_ptr (child_fd_data));
	}
      }
    }
  }
  return 0;
}

/**
 * unify_sh_opendir_cbk -
 *
 * @cookie: 
 */
static int32_t 
unify_sh_opendir_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      fd_t *fd)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  data_t *child_fd_data;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
    
    if (op_ret >= 0) {
      local->op_ret = op_ret;
      if (!local->fd) {
	local->fd = calloc (1, sizeof (fd_t));
	local->fd->ctx = get_new_dict ();
	local->fd->inode = inode_ref (local->inode);
	list_add (&local->fd->inode_list, &local->inode->fds);
      }
      dict_set (local->fd->ctx, (char *)cookie, data_from_static_ptr (fd));
    }
    if (op_ret == -1)
      local->failed = 1;
  }
  UNLOCK (&frame->mutex);
  
  /* Opendir done on all nodes, do readdir and write dir now */
  if (!callcnt) {
    if (local->failed) {
      local->inode->s_h_required = 1;
    } else {
      list = local->inode->private;
      list_for_each_entry (ino_list, list, list_head)
	local->call_count++;
      
      list_for_each_entry (ino_list, list, list_head) {
	child_fd_data = dict_get (local->fd->ctx, ino_list->xl->name);
	if (child_fd_data) {
	  _STACK_WIND (frame,
		       unify_sh_readdir_cbk,
		       ino_list->xl, //cookie
		       ino_list->xl,
		       ino_list->xl->fops->readdir,
		       0,
		       0,
		       data_to_ptr (child_fd_data));
	}
      }
    }
  }
  return 0;
}


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
		    const char *path,
		    inode_t *inode)
{
  call_frame_t *sh_frame = NULL;
  struct list_head *list = NULL;
  unify_local_t *local = NULL;
  unify_inode_list_t *ino_list = NULL;

  if (!((unify_private_t *)this->private)->self_heal)
    return 0;

  if (inode && inode->s_h_required) {
    /* Any self heal will be done at the directory level */
    sh_frame = copy_frame (frame);
    
    INIT_LOCAL (sh_frame, local);
  
    local->inode = inode;
    
    list = inode->private;
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;
    
    list_for_each_entry (ino_list, list, list_head) {
      loc_t tmp_loc = {
	.inode = ino_list->inode,
	.path = path,
	.ino = ino_list->inode->ino,
      };
      _STACK_WIND (sh_frame,
		   unify_sh_opendir_cbk,
		   ino_list->xl->name,
		   ino_list->xl,
		   ino_list->xl->fops->opendir,
		   &tmp_loc);
    }
    inode->s_h_required = 0;
  }
  return 0;
}


/**
 * unify_sh_writedir_cbk -
 */
static int32_t
unify_sh_writedir_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    --local->call_count;
  }
  UNLOCK (&frame->mutex);
  
  if (!local->call_count) {
    LOCK_DESTROY (&frame->mutex);
    STACK_DESTROY (frame->root);
  }
  return 0;
}

/**
 * unify_readdir_self_heal - 
 */
int32_t 
unify_readdir_self_heal (call_frame_t *frame,
			 xlator_t *this,
			 fd_t *fd,
			 unify_local_t *local)
{
  xlator_list_t *trav = this->children;
  unify_local_t *sh_local = NULL;
  call_frame_t *sh_frame = NULL;
  data_t *child_fd_data = NULL;

  if (!((unify_private_t *)this->private)->self_heal)
    return 0;

  sh_frame = copy_frame (frame);
  sh_local = calloc (1, sizeof (unify_local_t));

  /* Init */
  LOCK_INIT (&frame->mutex);
  sh_frame->local = sh_local;
  /* Rightnow let it be like this */
  sh_local->call_count = ((unify_private_t *)this->private)->child_count * 2 + 1;
  
  /* Send the namespace's entry to all the storage nodes */
  while (trav) {
    child_fd_data = dict_get (fd->ctx, trav->xlator->name);
    if (child_fd_data) {
      STACK_WIND (sh_frame,
		  unify_sh_writedir_cbk,
		  trav->xlator,
		  trav->xlator->fops->writedir,
		  data_to_ptr (child_fd_data),
		  GF_CREATE_ONLY_DIR,
		  local->ns_entry,
		  local->ns_count);

      STACK_WIND (sh_frame,
		  unify_sh_writedir_cbk,
		  trav->xlator,
		  trav->xlator->fops->writedir,
		  data_to_ptr (child_fd_data),
		  GF_CREATE_ONLY_DIR,
		  local->entry->next,
		  local->count);
    } else {
      local->call_count -= 2;
      gf_log (this->name, 1, "error: fd not found for %s", trav->xlator->name);
    }
    trav = trav->next;
  }
  
  /* Send the other unified readdir entries to namespace */
  child_fd_data = dict_get (fd->ctx, NS(this)->name);
  if (child_fd_data) {
    STACK_WIND (sh_frame,
		unify_sh_writedir_cbk,
		NS(this),
		NS(this)->fops->writedir,
		data_to_ptr (child_fd_data),
		GF_CREATE_MISSING_FILE,
		local->entry->next,
		local->count);
  } else {
    --local->call_count;
    gf_log (this->name, 1, "error: fd not found for Namespace %s", NS(this)->name);
  }

  return 0;
}

