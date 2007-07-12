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
#include "common-utils.h"

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
    freee (local->path);
    LOCK_DESTROY (&frame->mutex);
    if (local->revalidate)
      inode_unref (local->revalidate);
    if (local->create_inode || local->revalidate) {
      /* This is _cbk() of lookup (). */
      inode_t *loc_inode = local->inode;
      STACK_UNWIND (frame,
		    local->op_ret,
		    local->op_errno,
		    local->inode,
		    &local->stbuf);
      if (loc_inode)
	inode_unref (loc_inode);
    } else {
      /* This should be _cbk() of stat(), as its not lookup_cbk () :p */
      STACK_UNWIND (frame,
		    local->op_ret,
		    local->op_errno,
		    &local->stbuf);
    }
  }

  return 0;
}

/**
 * unify_sh_readdir_cbk - sort of copy paste from unify.c:unify_readdir_cbk(), 
 *       duplicated the code as no STACK_UNWIND is done here.
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
	  /* This block is true for all the call other than first successful 
	   * call. So, take only file names from these entries, as directory 
	   * entries are already taken.
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
		  if ((sh_trav->buf.st_mode != tmp->buf.st_mode) ||
		      (sh_trav->buf.st_uid != tmp->buf.st_uid) ||
		      (sh_trav->buf.st_gid != tmp->buf.st_gid)) {
		    /* There is some inconsistency in storagenodes. Do a 
		     * self heal 
		     */
		    gf_log (this->name,
			    GF_LOG_WARNING,
			    "found mismatch in mode/uid/gid for %s",
			    tmp->name);
		    local->failed = 1;
		  }
		  flag = 1;
		  break;
		}
		sh_trav = sh_trav->next;
	      }
	      if (flag) {
		/* if its set, it means entry is already present, so remove 
		 * entries from current list.
		 */ 
		prev->next = tmp->next;
		trav = tmp->next;
		freee (tmp->name);
		freee (tmp);
		tmp_count--;
		continue;
	      } else {
		local->failed = 1;
	      }
	    }
	    prev = trav;
	    trav = trav->next;
	  }
	  /* Append the 'entries' from this call at the end of the previously 
	   * stored entry 
	   */
	  local->last->next = entry->next;
	  local->count += tmp_count;
	  while (local->last->next)
	    local->last = local->last->next;
	}
      } else {
	/* If its a _cbk from namespace, keep its entries seperate */
	unify_entry = calloc (1, sizeof (dir_entry_t));
	unify_entry->next = entry->next;
	local->ns_entry = unify_entry;
	local->ns_count = count;
      }
      /* This makes child nodes to free only head, and all dir_entry_t 
       * structures are kept reference at this level.
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
    if (local->ns_entry && local->entry) {
      dir_entry_t *ns_trav = local->ns_entry->next;
      while (ns_trav) {
	/* If its a file in the namespace, then replace its size from
	 * actual storage nodes 
	 */
	prev = local->entry;
	trav = local->entry->next;
	while (trav) {
	  if (strcmp (ns_trav->name, trav->name) == 0) {
	    if ((ns_trav->buf.st_mode != trav->buf.st_mode) ||
		(ns_trav->buf.st_uid != trav->buf.st_uid) ||
		(ns_trav->buf.st_gid != trav->buf.st_gid)) {
	      /* There is some inconsistency in storagenodes. Do a self heal */
	      gf_log (this->name,
		      GF_LOG_WARNING,
		      "found mismatch in mode/uid/gid for %s",
		      trav->name);
	      local->failed = 1;
	    }
	    /* Free this entry, as its already present in namespace */
	    tmp = trav;
	    prev->next = tmp->next;
	    trav = tmp->next;
	    freee (tmp->name);
	    freee (tmp);
	    local->count--;
	    break;
	  }
	  prev = trav;
	  trav = trav->next;
	}
	ns_trav = ns_trav->next;
      }
    }
    
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
	  freee (trav->name);
	  freee (trav);
	  trav = prev->next;
	}
	freee (prev);
      }
      /* Now remove the entries of namespace */
      prev = local->ns_entry;
      if (prev) {
	trav = prev->next;
	while (trav) {
	  prev->next = trav->next;
	  freee (trav->name);
	  freee (trav);
	  trav = prev->next;
	}
	freee (prev);
      }
    }
    {
      /* Send the closedir to all the nodes, whereever opendir has been sent */
      data_t *child_fd_data = NULL;
      fd_t *fd = local->fd;
      list = local->inode->private;
      local->call_count = 0;
      list_for_each_entry (ino_list, list, list_head) {
	child_fd_data = dict_get (fd->ctx, ino_list->xl->name);
	if (child_fd_data) 
	  local->call_count++;
      }      
      list_for_each_entry (ino_list, list, list_head) {
	child_fd_data = dict_get (fd->ctx, ino_list->xl->name);
	if (child_fd_data) {
	  STACK_WIND (frame,
		      unify_sh_closedir_cbk,
		      ino_list->xl,
		      ino_list->xl->fops->closedir,
		      data_to_ptr (child_fd_data));
	}
      }
      fd_destroy (fd);
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
	local->fd = fd_create (local->inode);
      }
      dict_set (local->fd->ctx, (char *)cookie, data_from_static_ptr (fd));
    }
    if (op_ret == -1)
      local->failed = 1;
  }
  UNLOCK (&frame->mutex);
  
  if (!callcnt) {
    /* opendir returned from all nodes, do readdir and write dir now */
    if (!local->failed) {
      /* Send readdir on all the fds */
      int32_t unwind = 0;
      local->failed = 0;
      list = local->inode->private;
      list_for_each_entry (ino_list, list, list_head) {
	child_fd_data = dict_get (local->fd->ctx, ino_list->xl->name);
	if (child_fd_data)
	  local->call_count++;
      }
      if (!local->call_count) {
	/* :O WTF? i need to UNWIND here then */
	unwind = 1;
      }
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
      if (!unwind)
	return 0;
    }
    local->inode->generation = 0;
    /* no inode, or everything is fine, just do STACK_UNWIND */
    if (local->fd)
      fd_destroy (local->fd);
    freee (local->path);
    LOCK_DESTROY (&frame->mutex);
    if (local->revalidate)
      inode_unref (local->revalidate);
    if (local->create_inode || local->revalidate) {
      /* This is lookup_cbk ()'s UNWIND. */
      inode_t *loc_inode = local->inode;
      STACK_UNWIND (frame,
		    local->op_ret,
		    local->op_errno,
		    local->inode,
		    &local->stbuf);
      /* unref the inode as ref is taken in lookup_cbk() itself. */
      if (loc_inode)
	inode_unref (loc_inode);
    } else {
      /* This should be stat_cbk() as its not lookup_cbk () */
      STACK_UNWIND (frame,
		    local->op_ret,
		    local->op_errno,
		    &local->stbuf);
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
		    unify_local_t *local)
{
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_private_t *priv = this->private;

  if (local->inode && 
      (local->inode->generation < priv->inode_generation)) {
    /* Any self heal will be done at the directory level */
    local->call_count = 0;
    list = local->inode->private;
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;

    list_for_each_entry (ino_list, list, list_head) {
      loc_t tmp_loc = {
	.inode = ino_list->inode,
	.path = local->path,
	.ino = ino_list->inode->ino,
      };
      _STACK_WIND (frame,
		   unify_sh_opendir_cbk,
		   ino_list->xl->name,
		   ino_list->xl,
		   ino_list->xl->fops->opendir,
		   &tmp_loc);
    }
  } else {
    /* no inode, or everything is fine, just do STACK_UNWIND */
    freee (local->path);
    LOCK_DESTROY (&frame->mutex);
    if (local->revalidate)
      inode_unref (local->revalidate);
    if (local->create_inode || local->revalidate) {
      /* This is lookup_cbk ()'s UNWIND. */
      inode_t *loc_inode = local->inode;
      STACK_UNWIND (frame,
		    local->op_ret,
		    local->op_errno,
		    local->inode,
		    &local->stbuf);
      /* unref the inode as ref is taken in lookup_cbk() itself. */
      if (loc_inode)
	inode_unref (loc_inode);
    } else {
      /* This should be stat_cbk() as its not lookup_cbk () */
      STACK_UNWIND (frame,
		    local->op_ret,
		    local->op_errno,
		    &local->stbuf);
    }
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
  unify_private_t *priv = this->private;
  xlator_list_t *trav = this->children;
  unify_local_t *sh_local = NULL;
  call_frame_t *sh_frame = NULL;
  data_t *child_fd_data = NULL;

  if (!priv->self_heal)
    return 0;

  if (local->ns_entry && local->ns_entry->next) {
    /* There is an reference to heal. There exists namespace */

    if (local->entry && local->entry->next) {
      /* This means, there are some directories missing in storage nodes.
       * So, send the writedir request to all the nodes + namespace node.
       */
      sh_frame = copy_frame (frame);
      sh_local = calloc (1, sizeof (unify_local_t));

      /* Init */
      LOCK_INIT (&sh_frame->mutex);
      sh_frame->local = sh_local;

      /* Rightnow let it be like this */
      sh_local->call_count = priv->child_count * 2 + 1;
    
      /* Send the other unified readdir entries to namespace */
      child_fd_data = dict_get (fd->ctx, NS(this)->name);
      if (child_fd_data) {
	STACK_WIND (sh_frame,
		    unify_sh_writedir_cbk,
		    NS(this),
		    NS(this)->fops->writedir,
		    data_to_ptr (child_fd_data),
		    GF_CREATE_MISSING_FILE,
		    local->entry,
		    local->count);
      } else {
	--local->call_count;
	gf_log (this->name, 
		GF_LOG_WARNING, 
		"fd not found for Namespace %s", 
		NS(this)->name);
      }
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
		      local->entry,
		      local->count);
	} else {
	  local->call_count -= 2;
	  gf_log (this->name, 
		  GF_LOG_WARNING, 
		  "fd not found for %s", 
		  trav->xlator->name);
	}
	trav = trav->next;
      }
    } else if (local->failed) {
      /* No missing directories in storagenodes. No need for writedir in
       * namespace. Send writedir to only storagenodes, for keeping the 
       * consistancy in permision and mode.
       */
      sh_frame = copy_frame (frame);
      sh_local = calloc (1, sizeof (unify_local_t));

      /* Init */
      LOCK_INIT (&sh_frame->mutex);
      sh_frame->local = sh_local;

      sh_local->call_count = priv->child_count;

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
	} else {
	  local->call_count--;
	  gf_log (this->name, 
		  GF_LOG_WARNING, 
		  "fd not found for %s", 
		  trav->xlator->name);
	}
	trav = trav->next;
      }
    }
  }

  /* Update the inode's generation to the current generation value. */
  fd->inode->generation = priv->inode_generation;
  return 0;
}

