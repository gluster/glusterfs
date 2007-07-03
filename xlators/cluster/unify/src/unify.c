/*
  (C) 2006, 2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
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
 * xlators/cluster/unify 
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

/* TODO: */
#define CHILDDOWN ENOTCONN

/**
 * unify_local_wipe - free all the extra allocation of local->* here.
 */
static void 
unify_local_wipe (unify_local_t *local)
{
  /* Free the strdup'd variables in the local structure */
  if (local->path) {
    free (local->path);
    local->path = NULL;
  }
  if (local->name) {
    free (local->name);
    local->name = NULL;
  }
}

/**
 * unify_bg_cbk - this is called by the background functions which 
 *   doesn't return any of inode, or buf. eg: rmdir, unlink, close, etc.
 *
 */
static int32_t 
unify_bg_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
    if (op_ret == 0)
      local->op_ret = 0;
  }
  UNLOCK (&frame->mutex);

  if (!callcnt) {
    unify_local_wipe (local);
    LOCK_DESTROY (&frame->mutex);
    STACK_DESTROY (frame->root);
  }

  return 0;
}

/**
 * unify_bg_buf_cbk - Used as _cbk in background frame, which returns buf.
 *
 */
static int32_t
unify_bg_buf_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->mutex);

  if (!callcnt) {
    unify_local_wipe (local);
    LOCK_DESTROY (&frame->mutex);
    STACK_DESTROY (frame->root);
  }
  return 0;
}

/**
 * unify_buf_cbk - 
 */
static int32_t
unify_buf_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *buf)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
    
    if (local->op_ret == -1 && op_errno != CHILDDOWN)
      local->op_errno = op_errno;

    if (op_ret == 0) {
      local->op_ret = 0;
      /* If file, then replace size of file in stat info. */
      if (!S_ISDIR (buf->st_mode)) {
	local->stbuf.st_size = buf->st_size;
	local->stbuf.st_blocks = buf->st_blocks;
      }
    }
  }
  UNLOCK (&frame->mutex);
    
  if (!callcnt) {
    unify_local_wipe (local);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

/**
 * unify_lookup_cbk - 
 */
static int32_t 
unify_lookup_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
		  struct stat *buf)
{
  int32_t callcnt = 0;
  unify_inode_list_t *ino_list = NULL;
  unify_private_t *priv = this->private;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
 
    if (op_ret == -1) {
      local->op_errno = op_errno;
      local->failed = 1;
      if (op_errno == CHILDDOWN) {
	/* This is used to know, whether there is inconsistancy with 
	 * namespace and storage nodes, or entry is really missing 
	 */
	local->flags = 1;  
      }
    }
  }
  UNLOCK (&frame->mutex);

  if (op_ret == 0) {
    if (local->create_inode) {
      ino_list = calloc (1, sizeof (unify_inode_list_t));
      ino_list->xl = (xlator_t *)cookie;
      ino_list->inode = inode_ref (inode);
      
      LOCK (&frame->mutex);
      {
	if (!local->list) {
	  local->list = calloc (1, sizeof (struct list_head));
	  INIT_LIST_HEAD (local->list);
	}
	/* This is to be used as hint from the inode and also mapping */
	list_add (&ino_list->list_head, local->list);
	
	/* Replace most of the variables from NameSpace */
	if (NS(this) == (xlator_t *)cookie) {
	  local->inode = inode_update (this->itable, NULL, NULL, buf);
	  local->inode->isdir = S_ISDIR(buf->st_mode);
	  if (local->inode->isdir)
	    local->inode->generation = 0; /*it will be reset after self-heal */
	}
      }
      UNLOCK (&frame->mutex);
    }

    LOCK (&frame->mutex);
    {
      local->entry_count++;
      local->op_ret = 0; 
      /* Replace most of the variables from NameSpace */
      if (NS(this) == (xlator_t *)cookie) {
	local->stbuf = *buf;
	if (local->revalidate) {
	  /* Revalidate */
	  /*
	  if (local->inode->ino != buf->st_ino) {
	    gf_log (this->name,
		    GF_LOG_WARNING,
		    "inode number changed for the entry %s (%lld -> %ld)",
		    local->path, local->inode->ino, buf->st_ino);
	  } 
	  */
	  local->inode = inode_update (this->itable, NULL, NULL, buf);
	}
      } else {
	if (!S_ISDIR (buf->st_mode)) {
	  /* If file, then replace size of file in stat info */
	  local->st_size = buf->st_size;
	  local->st_blocks = buf->st_blocks;
	}
	if (local->st_nlink < buf->st_nlink)
	  local->st_nlink = buf->st_nlink;
	if (local->revalidate) {
	  /* TODO: at end of it, make sure, list is proper, no stale entries */
	  /* Revalidate. Update the inode of clients */
	  int32_t flag = 0;
	  struct list_head *list = NULL;
	  list = local->list;
	  list_for_each_entry (ino_list, list, list_head) {
	    if (ino_list->xl == (xlator_t *)cookie) {
	      ino_list->inode = inode_ref (inode);
	      flag = 1;
	      break;
	    }
	  }
	  if (!flag) {
	    /* If no entry is found for this node, it means the first lookup
	     * was made when the node was down. so update the map.
	     */
	    ino_list = calloc (1, sizeof (unify_inode_list_t));
	    ino_list->xl = (xlator_t *)cookie;
	    ino_list->inode = inode_ref (inode);
	    list_add (&ino_list->list_head, local->list);
	  }
	}
      }
    }
    UNLOCK (&frame->mutex);
  }

  if (!callcnt) {
    inode_t *loc_inode = local->inode;
    
    if (local->inode) {
      /* There is a proper inode entry (ie, namespace entry present) */
      if (!loc_inode->private)
	loc_inode->private = local->list;

      if (local->inode->isdir) {
	if (local->failed)
	  local->inode->generation = 0;/*means, self-heal required for inode*/
      } else {
	/* If the lookup was done for file */
	if (local->entry_count != 2) {
	  /* If there are wrong number of entries */
	  if ((local->entry_count == 1) && !local->flags) {
	    gf_log (this->name,
		    GF_LOG_WARNING,
		    "%s: missing file in the storage node",
		    local->path);
	    /* send unlink to namespace entry */
	    {
	      call_frame_t *bg_frame = copy_frame (frame);
	      unify_local_t *bg_local = calloc (1, sizeof (unify_local_t));
	      bg_local->call_count = 1;
	      bg_frame->local = bg_local;
	      LOCK_INIT (&bg_frame->mutex);
	      ino_list = (unify_inode_list_t *)local->list->next;
	      {	      
		loc_t tmp_loc = {
		  .inode = ino_list->inode,
		  .path = local->path,
		  .ino = ino_list->inode->ino
		};
		STACK_WIND (bg_frame,
			    unify_bg_cbk,
			    NS(this),
			    NS(this)->fops->unlink,
			    &tmp_loc);
	      }
	    }
	  } else {
	    /* If more entries found */
	    gf_log (this->name,
		    GF_LOG_WARNING,
		    "%s: present in more than one storage node",
		    local->path);
	  }
	  local->op_ret = -1;
	  local->op_errno = ENOENT;
	}
	local->stbuf.st_size = local->st_size;
	local->stbuf.st_blocks = local->st_blocks;
      }
      local->stbuf.st_nlink = local->st_nlink;
    } else {
      local->op_ret = -1;
    }
    if ((priv->self_heal) && 
	((local->op_ret == 0) && local->inode->isdir)) {
      /* Let the self heal be done here */
      gf_unify_self_heal (frame, this, local);
    } else {
      /* either no self heal, or failure */
      unify_local_wipe (local);
      LOCK_DESTROY (&frame->mutex);
      STACK_UNWIND (frame, 
		    local->op_ret, 
		    local->op_errno, 
		    loc_inode, &local->stbuf);
      if (loc_inode)
	inode_unref (loc_inode);
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
  unify_inode_list_t *ino_list = NULL;
  xlator_list_t *trav = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->path = strdup (loc->path);

  if (loc->inode) {
    local->revalidate = 1;
    /* local->inode = loc->inode; */
    local->list = loc->inode->private;
    list = local->list;

    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;

    list_for_each_entry (ino_list, list, list_head) {
      loc_t tmp_loc = {
	.path = loc->path, 
	.ino = ino_list->inode->ino, 
	.inode = ino_list->inode
	};
      _STACK_WIND (frame,
		   unify_lookup_cbk,
		   ino_list->xl,
		   ino_list->xl,
		   ino_list->xl->fops->lookup,
		   &tmp_loc);
      
    }
  } else {
    /* First time lookup: right now there is no idea where the 
     * file present, send the request to all the client nodes 
     */
    
    /* No inode present for the path, create inode in _cbk */
    local->create_inode = 1;
    /* call count should be all child + 1 namespace */
    local->call_count = priv->child_count + 1;

    _STACK_WIND (frame,
		 unify_lookup_cbk,
		 NS(this),
		 NS(this),
		 NS(this)->fops->lookup,
		 loc);

    trav = this->children;
    while (trav) {
      _STACK_WIND (frame,
		   unify_lookup_cbk,
		   trav->xlator,
		   trav->xlator,
		   trav->xlator->fops->lookup,
		   loc);
      trav = trav->next;
    }
  }

  return 0;
}

/**
 * unify_forget_cbk -
 */
static int32_t 
unify_forget_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
    
    if (op_ret == 0)
      local->op_ret = 0;
  }
  UNLOCK (&frame->mutex);

  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, op_ret, op_errno);
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
  unify_local_t *local = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_inode_list_t *ino_list_prev = NULL;
  struct list_head *list = inode->private;

  /* Initialization */
  INIT_LOCAL (frame, local);
  list_for_each_entry (ino_list, list, list_head)
    local->call_count++;
  
  list_for_each_entry (ino_list, list, list_head) {
    STACK_WIND (frame,
                unify_forget_cbk,
                ino_list->xl,
                ino_list->xl->fops->forget,
                ino_list->inode);
    inode_unref (ino_list->inode);
  }

  /* Unref and free the inode->private list */
  ino_list_prev = NULL;
  list_for_each_entry_safe (ino_list, ino_list_prev, list, list_head) {
    list_del (&ino_list->list_head);
    free (ino_list);
  }
  free (list);
  inode->private = (void *)0xcafebabe; //debug
  /* Forget the 'inode' from the itables */

  inode_forget (inode, 0);

  return 0;
}

/** 
 * unify_stat_cbk -
 *
 * @cookie - default: this will be set to prev_frame.
 */
static int32_t
unify_stat_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *buf)
{
  int32_t callcnt = 0;
  unify_private_t *priv = this->private;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
    
    if (op_ret == -1 && op_errno != CHILDDOWN) {
      local->op_errno = op_errno;
      local->failed = 1;
    }
    if (op_ret == 0) {
      local->op_ret = 0;
      local->entry_count++;
      /* Replace most of the variables from NameSpace */
      if (NS(this) == ((call_frame_t *)cookie)->this) {
	local->stbuf = *buf;
      } else {
	if (!S_ISDIR (buf->st_mode)) {
	  /* If file, then get the size of file from storage node */
	  local->st_size = buf->st_size;
	  local->st_blocks = buf->st_blocks;
	}
      }
      if (buf->st_nlink > local->st_nlink)
	local->st_nlink = buf->st_nlink;
    }
  }
  UNLOCK (&frame->mutex);
    
  if (!callcnt) {
    if (!local->failed && local->stbuf.st_blksize) {
      /* If file, update the size and blocks in 'stbuf' to be returned */
      if (!S_ISDIR(local->stbuf.st_mode)) {
	local->stbuf.st_size = local->st_size;
	local->stbuf.st_blocks = local->st_blocks;
      } 
      local->stbuf.st_nlink = local->st_nlink;
    } else {
      local->inode->generation = 0; /* self-heal required */
      local->op_ret = -1;
    }
    if (!local->inode->isdir) {
      /* If the lookup was done for file */
      if (local->entry_count != 2) {
	/* If there are wrong number of entries */
	if (local->entry_count == 1) {
	  gf_log (this->name,
		  GF_LOG_WARNING,
		  "%s: missing file in the storage node",
		  local->path);
	} else {
	  /* If more entries found */
	  gf_log (this->name,
		  GF_LOG_WARNING,
		  "%s: present in more than one storage node",
		  local->path);
	}
	local->op_ret = -1;
	local->op_errno = ENOENT;
      }
    }
    if ((priv->self_heal) && 
	((local->op_ret == 0) && local->inode->isdir)) {
      gf_unify_self_heal (frame, this, local);
    } else {
      unify_local_wipe (local);
      LOCK_DESTROY (&frame->mutex);
      STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
    }
  }
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
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = NULL;
  struct list_head *list = NULL;

  /* Initialization */
  INIT_LOCAL (frame, local);
  local->inode = loc->inode;
  local->path = strdup (loc->path);
  
  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head)
    local->call_count++;
  
  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {
      .path = loc->path, 
      .inode = ino_list->inode,
      .ino = ino_list->inode->ino, 
    };
    STACK_WIND (frame,
		unify_stat_cbk,
		ino_list->xl,
		ino_list->xl->fops->stat,
		&tmp_loc);
  }

  return 0;
}

/**
 * unify_access_cbk -
 */
static int32_t
unify_access_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  if (op_errno == CHILDDOWN)
    op_errno = EIO;
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
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      loc_t tmp_loc = {
	.path = loc->path, 
	.ino = ino_list->inode->ino, 
	.inode = ino_list->inode
      };
      STACK_WIND (frame,
		  unify_access_cbk,
		  NS(this),
		  NS(this)->fops->access,
		  &tmp_loc,
		  mask);
    }
  }

  return 0;
}

static int32_t
unify_mkdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *buf)
{
  int32_t callcnt = 0;
  inode_t *loc_inode = NULL;
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->mutex);

  if (op_ret == 0) {

    ino_list = calloc (1, sizeof (unify_inode_list_t));
    ino_list->xl = ((call_frame_t *)cookie)->this;
    ino_list->inode = inode_ref (inode);
    
    LOCK (&frame->mutex);
    {
      local->op_ret = 0;
      /* This is to be used as hint from the inode and also mapping */
      list = local->inode->private;
      list_add (&ino_list->list_head, list);
    }
    UNLOCK (&frame->mutex);
  }
  
  if (!callcnt) {
    loc_inode = local->inode;
    unify_local_wipe (local);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->inode, &local->stbuf);
    if (loc_inode)
      inode_unref (loc_inode);
  }

  return 0;
}

/**
 * unify_ns_mkdir_cbk -
 */
static int32_t
unify_ns_mkdir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf)
{
  xlator_list_t *trav = NULL;
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_private_t *priv = this->private;
  unify_local_t *local = frame->local;
  
  if (op_ret == -1) {
    /* No need to send mkdir request to other servers, 
     * as namespace action failed 
     */
    if (op_errno == CHILDDOWN)
      op_errno = EIO;
    unify_local_wipe (local);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  NULL,
		  NULL);
    return 0;
  }
  
  /* Create one inode for this entry */
  local->op_ret = 0;
  local->stbuf = *buf;
  local->inode = inode_update (this->itable, NULL, NULL, buf);
  local->inode->isdir = 1;

  list = calloc (1, sizeof (struct list_head));
  
  /* Start the mapping list */
  INIT_LIST_HEAD (list);
  local->inode->private = (void *)list;

  ino_list = calloc (1, sizeof (unify_inode_list_t));
  ino_list->xl = NS (this);
  ino_list->inode = inode_ref (inode);

  /* Add entry to NameSpace's inode */
  list_add (&ino_list->list_head, list);
  local->call_count = priv->child_count;

  /* Send mkdir request to all the nodes now */
  trav = this->children;
  while (trav) {
    STACK_WIND (frame,
		unify_mkdir_cbk,
		trav->xlator,
		trav->xlator->fops->mkdir,
		local->name,
		local->mode);
    trav = trav->next;
  }
  
  return 0;
}


/**
 * unify_mkdir -
 */
int32_t
unify_mkdir (call_frame_t *frame,
	     xlator_t *this,
	     const char *name,
	     mode_t mode)
{
  unify_local_t *local = NULL;

  /* Initialization */
  INIT_LOCAL (frame, local);
  local->name = strdup (name);
  local->mode = mode;

  STACK_WIND (frame,
	      unify_ns_mkdir_cbk,
	      NS(this),
	      NS(this)->fops->mkdir,
	      name,
	      mode);
  return 0;
}

/**
 * unify_rmdir_cbk -
 */
static int32_t
unify_rmdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
    if (op_ret == 0)
      local->op_ret = 0;
  }
  UNLOCK (&frame->mutex);

  if (!callcnt) {
    unify_local_wipe (local);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}

/**
 * unify_ns_rmdir_cbk -
 */
static int32_t
unify_ns_rmdir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;
  
  if (op_ret == -1) {
    /* No need to send rmdir request to other servers, 
     * as namespace action failed 
     */
    if (op_errno == CHILDDOWN)
      op_errno = EIO;
    unify_local_wipe (local);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno);
    return 0;
  }
  
  list = local->inode->private;
  list_for_each_entry (ino_list, list, list_head)
    local->call_count++;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {
      .path = local->path, 
      .ino = ino_list->inode->ino, 
      .inode = ino_list->inode
    };
    STACK_WIND (frame,
		unify_rmdir_cbk,
		ino_list->xl,
		ino_list->xl->fops->rmdir,
		&tmp_loc);
  }

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
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->path = strdup (loc->path);
  local->inode = loc->inode;

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      loc_t tmp_loc = {
	.path = loc->path, 
	.ino = ino_list->inode->ino, 
	.inode = ino_list->inode
      };
      STACK_WIND (frame,
		  unify_ns_rmdir_cbk,
		  NS(this),
		  NS(this)->fops->rmdir,
		  &tmp_loc);
    }
  }

  return 0;
}


/**
 * unify_open_cbk -
 */
static int32_t
unify_open_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		fd_t *fd)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    if (op_ret == 0) {
      if (!local->fd) {
	local->op_ret = 0;
	local->fd = fd_create (local->inode);
      }
      dict_set (local->fd->ctx, 
		((xlator_t *)cookie)->name, 
		data_from_static_ptr (fd));
      if (((xlator_t *)cookie) != NS(this)) {
	/* Store child node's ptr, used in all the f*** / FileIO calls */
	dict_set (local->fd->ctx, 
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
  UNLOCK (&frame->mutex);
  
  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
    if (local->failed == 1 && local->fd) {
      data_t *child_fd_data = NULL;
      unify_local_t *bg_local = NULL;
      unify_inode_list_t *ino_list = NULL;
      struct list_head *list = NULL;
      call_frame_t *bg_frame = copy_frame (frame);

      INIT_LOCAL (bg_frame, bg_local);
      bg_local->call_count = 1;
      list = local->inode->private;
      list_for_each_entry (ino_list, list, list_head) {
	child_fd_data = dict_get (local->fd->ctx, ino_list->xl->name);
	if (child_fd_data) {
	  STACK_WIND (bg_frame,
		      unify_bg_cbk,
		      ino_list->xl,
		      ino_list->xl->fops->close,
		      (fd_t *)data_to_ptr (child_fd_data));
	}
      }
      /* return -1 to user */
      local->op_ret = -1;
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
	    int32_t flags)
{
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = NULL;

  /* Init */
  INIT_LOCAL (frame, local);
  local->inode = loc->inode;

  if (!loc->inode) {
    STACK_UNWIND (frame, -1, ENOENT, NULL);
    return 0;
  }
  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head)
    local->call_count++;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {
      .inode = ino_list->inode,
      .path = loc->path,
      .ino = ino_list->inode->ino
    };
    _STACK_WIND (frame,
		 unify_open_cbk,
		 ino_list->xl, //cookie
		 ino_list->xl,
		 ino_list->xl->fops->open,
		 &tmp_loc,
		 flags);
  }

  return 0;
}

/**
 * unify_create_open_cbk -
 */
static int32_t
unify_create_open_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       fd_t *fd)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    if (op_ret == 0) {
      if (!local->fd) {
	local->op_ret = 0;
	local->fd = fd_create (local->inode);
      }
      dict_set (local->fd->ctx, 
		((xlator_t *)cookie)->name, 
		data_from_static_ptr (fd));
      if (((xlator_t *)cookie) != NS(this)) {
	/* Store child node's ptr, used in all the f*** / FileIO calls */
	dict_set (local->fd->ctx, 
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
  UNLOCK (&frame->mutex);
  
  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
    if (local->failed == 1) {
      data_t *child_fd_data = NULL;
      unify_local_t *bg_local = NULL;
      unify_inode_list_t *ino_list = NULL;
      struct list_head *list = NULL;
      call_frame_t *bg_frame = copy_frame (frame);

      INIT_LOCAL (bg_frame, bg_local);
      bg_local->call_count = 1;
      list = local->inode->private;
      list_for_each_entry (ino_list, list, list_head) {
	child_fd_data = dict_get (local->fd->ctx, ino_list->xl->name);
	if (child_fd_data) {
	  STACK_WIND (bg_frame,
		      unify_bg_cbk,
		      ino_list->xl,
		      ino_list->xl->fops->close,
		      (fd_t *)data_to_ptr (child_fd_data));
	}
      }
      /* return -1 to user */
      local->op_ret = -1;
    }
    STACK_UNWIND (frame, 
		  local->op_ret, 
		  local->op_errno, 
		  local->fd,
		  local->inode,
		  &local->stbuf);
  }
  return 0;
}

/**
 * unify_create_lookup_cbk - 
 */
static int32_t 
unify_create_lookup_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 inode_t *inode,
			 struct stat *buf)
{
  int32_t callcnt = 0;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
    if (op_ret == -1) {
      local->op_errno = op_errno;
      local->failed = 1;
    }
  }
  UNLOCK (&frame->mutex);

  if (op_ret == 0) {
    if (local->create_inode) {
      ino_list = calloc (1, sizeof (unify_inode_list_t));
      ino_list->xl = (xlator_t *)cookie;
      ino_list->inode = inode_ref (inode);
      
      LOCK (&frame->mutex);
      {
	if (!local->list) {
	  local->list = calloc (1, sizeof (struct list_head));
	  INIT_LIST_HEAD (local->list);
	}
	/* This is to be used as hint from the inode and also mapping */
	list_add (&ino_list->list_head, local->list);
	
	/* Replace most of the variables from NameSpace */
	if (NS(this) == (xlator_t *)cookie) {
	  local->inode = inode_update (this->itable, NULL, NULL, buf);
	  local->inode->isdir = S_ISDIR(buf->st_mode);
	}
      }
      UNLOCK (&frame->mutex);
    }

    LOCK (&frame->mutex);
    {
      local->entry_count++;
      local->op_ret = 0; 
      /* Replace most of the variables from NameSpace */
      if (NS(this) == (xlator_t *)cookie) {
	local->stbuf = *buf;
      } else {
	/* If file, then replace size of file in stat info */
	local->st_size = buf->st_size;
	local->st_blocks = buf->st_blocks;
      }
    }
    UNLOCK (&frame->mutex);
  }

  if (!callcnt) {
    inode_t *loc_inode = local->inode;

    if ((local->entry_count == 2) && local->inode) {
      struct list_head *list = NULL;

      if (!loc_inode->private)
	loc_inode->private = local->list;

      local->op_ret = -1;
      list = local->inode->private;
      list_for_each_entry (ino_list, list, list_head)
	local->call_count++;
      
      list_for_each_entry (ino_list, list, list_head) {
	loc_t tmp_loc = {
	  .inode = ino_list->inode,
	  .path = local->name,
	  .ino = ino_list->inode->ino
	};
	_STACK_WIND (frame,
		     unify_create_open_cbk,
		     ino_list->xl, //cookie
		     ino_list->xl,
		     ino_list->xl->fops->open,
		     &tmp_loc,
		     local->flags);
      }

    } else {
      /* Lookup failed, can't do open */
      if (local->entry_count == 1) {
	gf_log (this->name,
		GF_LOG_WARNING,
		"%s: missing file in the storage node",
		local->name);
      } else {
	/* If more entries found */
	gf_log (this->name,
		GF_LOG_WARNING,
		"%s: present in more than one storage node",
		local->name);
      }
      local->op_ret = -1;    
      local->op_errno = ENOENT;
      unify_local_wipe (local);
      LOCK_DESTROY (&frame->mutex);
      STACK_UNWIND (frame, 
		    local->op_ret, 
		    local->op_errno, 
		    NULL,
		    NULL,
		    NULL);
    }

    if (loc_inode)
      inode_unref (loc_inode);
  }

  return 0;
}

/**
 * unify_create_cbk -
 */
static int32_t
unify_create_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  fd_t *fd,
		  inode_t *inode,
		  struct stat *buf)
{
  int32_t callcnt = 0;
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;

  if (op_ret == 0) {
    local->op_ret = 0;
    list = local->inode->private;
    
    ino_list = calloc (1, sizeof (unify_inode_list_t));
    ino_list->xl = (xlator_t *)cookie;
    ino_list->inode = inode_ref (inode);

    /* Add entry to NameSpace's inode */
    list_add (&ino_list->list_head, list);

    dict_set (local->fd->ctx, 
	      ((xlator_t *)cookie)->name, 
	      data_from_static_ptr (fd));
    
    /* Store the child node's ptr, will be used in all the f*** / FileIO calls. */
    dict_set (local->fd->ctx, this->name, data_from_static_ptr (cookie));
  }

  LOCK (&frame->mutex);
  {
    callcnt == --local->call_count;
    if (op_ret == -1 && op_errno != ENOENT) {
      local->op_errno = op_errno;
    }
  }
  UNLOCK (&frame->mutex);

  if (!callcnt) {
    if (local->op_ret == -1) {
      /* send close () on Namespace */
      unify_local_t *bg_local = NULL;
      data_t *child_fd_data = NULL;
      call_frame_t *bg_frame = copy_frame (frame);

      INIT_LOCAL (bg_frame, bg_local);
      bg_local->call_count = 1;
      child_fd_data = dict_get (local->fd->ctx, NS(this)->name);
      if (child_fd_data) {
	STACK_WIND (bg_frame,
		    unify_bg_cbk,
		    NS(this),
		    NS(this)->fops->close,
		    (fd_t *)data_to_ptr (child_fd_data));
      }
      fd_destroy (local->fd);

      bg_local = NULL;
      bg_frame = NULL;
      bg_frame = copy_frame (frame);
      
      INIT_LOCAL (bg_frame, bg_local);
      bg_local->call_count = 1;
      {
	/* Create failed in storage node, but it was success in 
	 * namespace node, so after closing fd, need to unlink the file
	 */
	list = local->inode->private;
	list_for_each_entry (ino_list, list, list_head) {
	  loc_t tmp_loc = {
	    .inode = ino_list->inode,
	    .ino = ino_list->inode->ino,
	    .path = local->name
	  };
	  STACK_WIND (bg_frame,
		      unify_bg_cbk,
		      NS(this),
		      NS(this)->fops->unlink,
		      &tmp_loc);
	}
      }
    }
    
    unify_local_wipe (local);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, 
		  local->op_ret, 
		  local->op_errno, 
		  local->fd, 
		  local->inode, 
		  &local->stbuf);
  }

  return 0;
}

/**
 * unify_ns_create_cbk -
 * 
 */
static int32_t
unify_ns_create_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     fd_t *fd,
		     inode_t *inode,
		     struct stat *buf)
{
  struct list_head *list = NULL;
  struct sched_ops *sched_ops = NULL;
  xlator_t *sched_xl = NULL;
  xlator_list_t *trav = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;
  
  if (op_ret == -1) {
    /* No need to send create request to other servers, 
     * as namespace action failed 
     */
    if ((op_errno != EEXIST) || 
	((op_errno == EEXIST) && ((local->flags & O_EXCL) == O_EXCL))) {
      unify_local_wipe (local);
      LOCK_DESTROY (&frame->mutex);
      if (op_errno == CHILDDOWN)
	op_errno = EIO;
      STACK_UNWIND (frame,
		    op_ret,
		    op_errno,
		    NULL,
		    NULL,
		    buf);
      return 0;
    }
  }
  
  if (op_ret == 0) {
    /* Create/update inode for this entry */
    local->inode = inode_update (this->itable, NULL, NULL, buf);
  
    /* link fd and inode */
    local->fd = fd_create (local->inode);
    dict_set (local->fd->ctx, NS(this)->name, data_from_static_ptr (fd));
    
    local->op_ret = 0;
    local->stbuf = *buf;
  
    /* This is the case where create is really happening */
    local->create_inode = 1;

    /* Start the mapping list */
    list = calloc (1, sizeof (struct list_head));
    INIT_LIST_HEAD (list);
    local->inode->private = (void *)list;
    
    ino_list = calloc (1, sizeof (unify_inode_list_t));
    ino_list->xl = NS (this);
    ino_list->inode = inode_ref (inode);
    /* Add entry to NameSpace's inode */
    list_add (&ino_list->list_head, list);
    
    /* This means, file doesn't exist anywhere in the Filesystem */
    sched_ops = ((unify_private_t *)this->private)->sched_ops;
    local->op_ret = -1;
    local->call_count = 1;
    /* Send create request to the scheduled node now */
    sched_xl = sched_ops->schedule (this, 0); 
    _STACK_WIND (frame,
		 unify_create_cbk,
		 sched_xl,
		 sched_xl,
		 sched_xl->fops->create,
		 local->name,
		 local->flags,
		 local->mode);
    inode_unref (local->inode);
  } else {
    /* File already exists, and there is no O_EXCL flag */
    local->call_count = ((unify_private_t *)this->private)->child_count + 1;
    local->op_ret = -1;
    if (local->inode)
      inode_unref (local->inode);

    {
      /* Send the lookup to all the nodes including namespace */
      loc_t tmp_loc = {
	.path = local->name,
	.inode = 0,
	.ino = 0
      };
      STACK_WIND (frame,
		  unify_create_lookup_cbk,
		  NS(this),
		  NS(this)->fops->lookup,
		  &tmp_loc);
    }
    trav = this->children;
    while (trav) {
      loc_t tmp_loc = {
	.path = local->name,
	.inode = 0,
	.ino = 0
      };
      STACK_WIND (frame,
		  unify_create_lookup_cbk,
		  trav->xlator,
		  trav->xlator->fops->lookup,
		  &tmp_loc);
      trav = trav->next;
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
	      const char *name,
	      int32_t flags,
	      mode_t mode)
{
  unify_local_t *local = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->name = strdup (name);
  local->mode = mode;
  local->flags = flags;

  STACK_WIND (frame,
	      unify_ns_create_cbk,
	      NS(this),
	      NS(this)->fops->create,
	      name,
	      flags | O_EXCL,
	      mode);
  
  return 0;
}


/**
 * unify_opendir_cbk - 
 */
static int32_t
unify_opendir_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   fd_t *fd)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

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
    if (op_ret == -1 && op_errno != CHILDDOWN) {
      local->op_errno = op_errno;
      local->failed = 1;
    }
  }
  UNLOCK (&frame->mutex);

  if (!callcnt) {
    if (local->failed == 1) {
      data_t *child_fd_data = NULL;
      unify_local_t *bg_local = NULL;
      unify_inode_list_t *ino_list = NULL;
      struct list_head *list = NULL;
      call_frame_t *bg_frame = copy_frame (frame);
      
      INIT_LOCAL (bg_frame, bg_local);
      list = local->inode->private;
      list_for_each_entry (ino_list, list, list_head) {
	child_fd_data = dict_get (local->fd->ctx, ino_list->xl->name);
	if (child_fd_data)
	  bg_local->call_count++;
      }
      list = local->inode->private;
      list_for_each_entry (ino_list, list, list_head) {
	child_fd_data = dict_get (local->fd->ctx, ino_list->xl->name);
	if (child_fd_data) {
	  STACK_WIND (bg_frame,
		      unify_bg_cbk,
		      ino_list->xl,
		      ino_list->xl->fops->closedir,
		      (fd_t *)data_to_ptr (child_fd_data));
	}
      }
      /* return -1 to user */
      local->op_ret = -1;
    }
    LOCK_DESTROY (&frame->mutex);
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
	       loc_t *loc)
{
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = NULL;

  INIT_LOCAL (frame, local);
  local->inode = loc->inode;

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head)
    local->call_count++;

  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {
      .inode = ino_list->inode,
      .path = loc->path,
      .ino = ino_list->inode->ino,
    };
    _STACK_WIND (frame,
		 unify_opendir_cbk,
		 ino_list->xl->name,
		 ino_list->xl,
		 ino_list->xl->fops->opendir,
		 &tmp_loc);
  }

  return 0;
}

/**
 * unify_statfs_cbk -
 */
static int32_t
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

  LOCK (&frame->mutex);
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
  UNLOCK (&frame->mutex);

  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
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
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = NULL;

  INIT_LOCAL (frame, local);
  local->call_count = ((unify_private_t *)this->private)->child_count;

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl != NS(this)) {
      loc_t tmp_loc = {
	.path = loc->path,
	.inode = ino_list->inode,
	.ino = ino_list->inode->ino
      };
      STACK_WIND (frame,
		  unify_statfs_cbk,
		  ino_list->xl,		     
		  ino_list->xl->fops->statfs,
		  &tmp_loc);
    }
  }

  return 0;
}

/**
 * unify_chmod_cbk - 
 */
static int32_t
unify_chmod_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  struct list_head *list = NULL;
  call_frame_t *bg_frame = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;

  if (op_ret == -1) {
    /* No need to send chmod request to other servers, 
     * as namespace action failed 
     */
    if (op_errno == CHILDDOWN)
      op_errno = EIO;

    unify_local_wipe (local);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = 0;
  local->stbuf = *buf;
  list = local->inode->private;
    
  if (S_ISDIR (buf->st_mode)) {
    /* If directory, get a copy of the current frame, and set 
     * the current local to bg_frame's local 
     */
    bg_frame = copy_frame (frame);
    frame->local = NULL;
    bg_frame->local = local;
    LOCK_INIT (&bg_frame->mutex);

    /* Unwind this frame, and continue with bg_frame */
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    
    /* Send chmod request to all the nodes now */
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;
    local->call_count--; /* Reduce 1 for namespace entry */
    
    list_for_each_entry (ino_list, list, list_head) {
      if (ino_list->xl != NS(this)) {
	loc_t tmp_loc = {
	  .inode = ino_list->inode,
	  .path = local->path,
	  .ino = ino_list->inode->ino,
	};
	STACK_WIND (bg_frame,
		    unify_bg_buf_cbk,
		    ino_list->xl,
		    ino_list->xl->fops->chmod,
		    &tmp_loc,
		    local->mode);
      }
    }
  } else {
    /* Its not a directory, so copy will be present only on one storage node */
    local->call_count = 1;
    list_for_each_entry (ino_list, list, list_head) {
      if (ino_list->xl != NS(this)) {
	loc_t tmp_loc = {
	  .path = local->path, 
	  .ino = ino_list->inode->ino, 
	  .inode = ino_list->inode
	};
	STACK_WIND (frame,
		    unify_buf_cbk,
		    ino_list->xl,
		    ino_list->xl->fops->chmod,
		    &tmp_loc,
		    local->mode);
	break;
      }
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
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->path = strdup (loc->path);
  local->inode = loc->inode;
  local->mode = mode;

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      loc_t tmp_loc = {
	.path = loc->path, 
	.ino = ino_list->inode->ino, 
	.inode = ino_list->inode
      };
      STACK_WIND (frame,
		  unify_chmod_cbk,
		  NS(this),
		  NS(this)->fops->chmod,
		  &tmp_loc,
		  mode);
    }
  }
  return 0;
}

/**
 * unify_chown_cbk - 
 */
static int32_t
unify_chown_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  struct list_head *list = NULL;
  call_frame_t *bg_frame = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;

  if (op_ret == -1) {
    /* No need to send chown request to other servers, as namespace action failed */
    if (op_errno == CHILDDOWN)
      op_errno = EIO;
    unify_local_wipe (local);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = 0;
  local->stbuf = *buf;
  list = local->inode->private;
    
  if (S_ISDIR (buf->st_mode)) {
    /* If directory, get a copy of the current frame, and set 
     * the current local to bg_frame's local 
     */
    bg_frame = copy_frame (frame);
    frame->local = NULL;
    bg_frame->local = local;
    LOCK_INIT (&bg_frame->mutex);
    
    /* Unwind this frame, and continue with bg_frame */
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);

    local->call_count = 0;
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;
    local->call_count--; /* Reduce 1 for namespace entry */

    /* Send chown request to all the nodes now */
    list_for_each_entry (ino_list, list, list_head) {
      if (ino_list->xl != NS(this)) {
	loc_t tmp_loc = {
	  .inode = ino_list->inode,
	  .path = local->path,
	  .ino = ino_list->inode->ino,
	};
	STACK_WIND (bg_frame,
		    unify_bg_buf_cbk,
		    ino_list->xl,
		    ino_list->xl->fops->chown,
		    &tmp_loc,
		    local->uid,
		    local->gid);
      }
    }
  } else {
    /* Its not a directory */
    local->call_count = 1;
    list_for_each_entry (ino_list, list, list_head) {
      if (ino_list->xl != NS(this)) {
	loc_t tmp_loc = {
	  .path = local->path, 
	  .ino = ino_list->inode->ino, 
	  .inode = ino_list->inode
	};
	STACK_WIND (frame,
		    unify_buf_cbk,
		    ino_list->xl,
		    ino_list->xl->fops->chown,
		    &tmp_loc,
		    local->uid,
		    local->gid);
	break;
      }
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
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->path = strdup (loc->path);
  local->inode = loc->inode;
  local->uid = uid;
  local->gid = gid;

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      loc_t tmp_loc = {
	.path = loc->path, 
	.ino = ino_list->inode->ino, 
	.inode = ino_list->inode
      };
      STACK_WIND (frame,
		  unify_chown_cbk,
		  NS(this),
		  NS(this)->fops->chown,
		  &tmp_loc,
		  uid,
		  gid);
    }
  }
  return 0;
}

/**
 * unify_truncate_cbk - 
 */
static int32_t
unify_truncate_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
  struct list_head *list = NULL;
  call_frame_t *bg_frame = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;

  if (op_ret == -1) {
    /* No need to send truncate request to other servers, 
     * as namespace action failed 
     */
    if (op_errno == CHILDDOWN)
      op_errno = EIO;
    unify_local_wipe (local);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = 0;
  local->stbuf = *buf;
  list = local->inode->private;
    
  /* If directory, get a copy of the current frame, and set 
   * the current local to bg_frame's local 
   */
  if (S_ISDIR (buf->st_mode)) {
    bg_frame = copy_frame (frame);
    frame->local = NULL;
    bg_frame->local = local;
    LOCK_INIT (&bg_frame->mutex);
    
    /* Unwind this frame, and continue with bg_frame */
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    
    /* Send chmod request to all the nodes now */
    local->call_count = 0;
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;
    local->call_count--; /* Reduce 1 for namespace entry */

    list_for_each_entry (ino_list, list, list_head) {
      if (ino_list->xl != NS(this)) {
	loc_t tmp_loc = {
	  .inode = ino_list->inode,
	  .path = local->path,
	  .ino = ino_list->inode->ino,
	};
	STACK_WIND (bg_frame,
		    unify_bg_buf_cbk,
		    ino_list->xl,
		    ino_list->xl->fops->truncate,
		    &tmp_loc,
		    local->offset);
      }
    }
  } else {
    /* Its not a directory */
    local->call_count = 1;
    list_for_each_entry (ino_list, list, list_head) {
      if (ino_list->xl != NS(this)) {
	loc_t tmp_loc = {
	  .path = local->path, 
	  .ino = ino_list->inode->ino, 
	  .inode = ino_list->inode
	};
	STACK_WIND (frame,
		    unify_buf_cbk,
		    ino_list->xl,
		    ino_list->xl->fops->truncate,
		    &tmp_loc,
		    local->offset);
      }
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
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->path = strdup (loc->path);
  local->inode = loc->inode;
  local->offset = offset;
  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      loc_t tmp_loc = {
	.path = loc->path, 
	.ino = ino_list->inode->ino, 
	.inode = ino_list->inode
      };
      STACK_WIND (frame,
		  unify_truncate_cbk,
		  NS(this),
		  NS(this)->fops->truncate,
		  &tmp_loc,
		  offset);
    }
  }

  return 0;
}

/**
 * unify_utimens_cbk -
 */
int32_t 
unify_utimens_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *buf)
{
  struct list_head *list = NULL;
  call_frame_t *bg_frame = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;

  if (op_ret == -1) {
    /* No need to send chmod request to other servers, 
     * as namespace action failed 
     */
    if (op_errno == CHILDDOWN)
      op_errno = EIO;
    unify_local_wipe (local);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = 0;
  local->stbuf = *buf;
  list = local->inode->private;
    
  /* If directory, get a copy of the current frame, and set 
   * the current local to bg_frame's local 
   */
  if (S_ISDIR (buf->st_mode)) {
    bg_frame = copy_frame (frame);
    frame->local = NULL;
    bg_frame->local = local;
    LOCK_INIT (&bg_frame->mutex);
    
    /* Unwind this frame, and continue with bg_frame */
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    
    /* Send chmod request to all the nodes now */
    local->call_count = 0;
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;
    local->call_count--; /* Reduce 1 for namespace entry */

    list_for_each_entry (ino_list, list, list_head) {
      if (ino_list->xl != NS(this)) {
	loc_t tmp_loc = {
	  .inode = ino_list->inode,
	  .path = local->path,
	  .ino = ino_list->inode->ino,
	};
	STACK_WIND (bg_frame,
		    unify_bg_buf_cbk,
		    ino_list->xl,
		    ino_list->xl->fops->utimens,
		    &tmp_loc,
		    local->tv);
      }
    }
  } else {
    /* Its not a directory */
    local->call_count = 1;
    list_for_each_entry (ino_list, list, list_head) {
      if (ino_list->xl != NS(this)) {
	loc_t tmp_loc = {
	  .path = local->path, 
	  .ino = ino_list->inode->ino, 
	  .inode = ino_list->inode
	};
	STACK_WIND (frame,
		    unify_buf_cbk,
		    ino_list->xl,
		    ino_list->xl->fops->utimens,
		    &tmp_loc,
		    local->tv);
      }
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
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->inode = loc->inode;
  local->path = strdup (loc->path);
  memcpy (local->tv, tv, 2 * sizeof (struct timespec));

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      loc_t tmp_loc = {
	.path = loc->path, 
	.ino = ino_list->inode->ino, 
	.inode = ino_list->inode
      };
      STACK_WIND (frame,
		  unify_utimens_cbk,
		  NS(this),
		  NS(this)->fops->utimens,
		  &tmp_loc,
		  tv);
    }
  }
  
  return 0;
}

/**
 * unify_readlink_cbk - 
 */
static int32_t
unify_readlink_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    const char *path)
{
  if (op_errno == CHILDDOWN)
    op_errno = EIO;
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
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl != NS(this)) {
      loc_t tmp_loc = {
	.path = loc->path, 
	.ino = ino_list->inode->ino, 
	.inode = ino_list->inode
      };
      STACK_WIND (frame,
		  unify_readlink_cbk,
		  ino_list->xl,
		  ino_list->xl->fops->readlink,
		  &tmp_loc,
		  size);
      break;
    }
  }

  return 0;
}


/**
 * unify_unlink_cbk - 
 */
static int32_t
unify_unlink_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
    if (op_ret == 0)
      local->op_ret = 0;
  }
  UNLOCK (&frame->mutex);

  if (!callcnt) {
    unify_local_wipe (local);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}

/**
 * unify_ns_unlink_cbk - 
 */
static int32_t
unify_ns_unlink_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;

  if (op_ret == -1) {
    /* No need to send unlink request to other servers, 
     * as namespace action failed 
     */
    if (op_errno == CHILDDOWN)
      op_errno = EIO;
    unify_local_wipe (local);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno);
    return 0;
  }

  local->op_ret = 0;
  list = local->inode->private;
  local->call_count = 1;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl != NS(this)) {
      loc_t tmp_loc = {
	.path = local->path, 
	.ino = ino_list->inode->ino, 
	.inode = ino_list->inode
      };
      STACK_WIND (frame,
		  unify_unlink_cbk,
		  ino_list->xl,
		  ino_list->xl->fops->unlink,
		  &tmp_loc);
    }
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
  
  unify_local_t *local = NULL;
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->path = strdup (loc->path);
  local->inode = loc->inode;

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      loc_t tmp_loc = {
	.path = loc->path, 
	.ino = ino_list->inode->ino, 
	.inode = ino_list->inode
      };
      STACK_WIND (frame,
		  unify_ns_unlink_cbk,
		  NS(this),
		  NS(this)->fops->unlink,
		  &tmp_loc);
    }
  }

  return 0;
}


/**
 * unify_readv_cbk - 
 */
static int32_t
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
  data_t *child_fd_data = NULL;
  data_t *fd_data = dict_get (fd->ctx, this->name);

  if (!fd_data) {
    STACK_UNWIND (frame, -1, EBADFD, "");
    return 0;
  }
  child = data_to_ptr (fd_data);
  
  child_fd_data = dict_get (fd->ctx, child->name);
  if (child_fd_data) {
      STACK_WIND (frame,
		  unify_readv_cbk,
		  child,
		  child->fops->readv,
		  (fd_t *)data_to_ptr (child_fd_data),
		  size,
		  offset);
  }
  return 0;
}

/**
 * unify_writev_cbk - 
 */
static int32_t
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
  data_t *child_fd_data = NULL;
  data_t *fd_data = dict_get (fd->ctx, this->name);

  if (!fd_data) {
    STACK_UNWIND (frame, -1, EBADFD, "");
    return 0;
  }
  child = data_to_ptr (fd_data);
  
  child_fd_data = dict_get (fd->ctx, child->name);
  if (child_fd_data) {
    STACK_WIND (frame,
		unify_writev_cbk,
		child,
		child->fops->writev,
		(fd_t *)data_to_ptr (child_fd_data),
		vector,
		count,
		off);
  }

  return 0;
}


/**
 * unify_ftruncate_cbk -
 */
static int32_t
unify_ftruncate_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
    
    if (op_ret == -1)
      local->op_errno = op_errno;
    
    if (op_ret == 0) {
      local->op_ret = 0;
      
      /* Get most of the variables of struct stat from NameSpace */
      if (NS(this) == (xlator_t *)cookie) {
	local->stbuf = *buf;
      } else if (!S_ISDIR (buf->st_mode)) {
	  /* If file, then get the size of file from storage node */
	  local->stbuf.st_size = buf->st_size;
	  local->stbuf.st_blocks = buf->st_blocks;
      }
    }
  }
  UNLOCK (&frame->mutex);

  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }

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
  data_t *child_fd_data = NULL;
  unify_local_t *local = NULL;
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;

  /* Init */
  INIT_LOCAL (frame, local);
  local->inode = fd->inode;

  list = local->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    child_fd_data = dict_get (fd->ctx, ino_list->xl->name);
    if (child_fd_data)
    local->call_count++;
  }
  list_for_each_entry (ino_list, list, list_head) {
    child_fd_data = dict_get (fd->ctx, ino_list->xl->name);
    if (child_fd_data) {
      _STACK_WIND (frame,
		   unify_ftruncate_cbk,
		   ino_list->xl,
		   ino_list->xl,
		   ino_list->xl->fops->ftruncate,
		   (fd_t *)data_to_ptr (child_fd_data),
		   offset);
    }
  }

  return 0;
}


/**
 * unify_fchmod_cbk -
 */
static int32_t
unify_fchmod_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
  struct list_head *list = NULL;
  data_t *child_fd_data = NULL;
  call_frame_t *bg_frame = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;

  if (op_ret == -1) {
    /* No need to send fchmod request to storage server, 
     * as namespace action failed 
     */
    if (op_errno == CHILDDOWN)
      op_errno = EIO;
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = 0;
  local->stbuf = *buf;
  list = local->fd->inode->private;
    
  if (S_ISDIR (buf->st_mode)) {
    /* If directory, get a copy of the current frame, and set 
     * the current local to bg_frame's local 
     */
    bg_frame = copy_frame (frame);
    frame->local = NULL;
    bg_frame->local = local;
    LOCK_INIT (&bg_frame->mutex);

    /* Unwind this frame, and continue with bg_frame */
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    
    /* Send fchmod request to all the nodes now */
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;
    local->call_count--; /* Reduce 1 for namespace entry */
    
    list_for_each_entry (ino_list, list, list_head) {
      if (ino_list->xl != NS(this)) {
	child_fd_data = dict_get (local->fd->ctx, ino_list->xl->name);
	if (child_fd_data) {
	  STACK_WIND (bg_frame,
		      unify_bg_buf_cbk,
		      ino_list->xl,
		      ino_list->xl->fops->fchmod,
		      (fd_t *)data_to_ptr (child_fd_data),
		      local->mode);
	}
      }
    }
  } else {
    /* Its not a directory */
    xlator_t *child = NULL;
    data_t *fd_data = dict_get (local->fd->ctx, this->name);
    
    if (!fd_data) {
      STACK_UNWIND (frame, -1, EBADFD, "");
      return 0;
    }
    child = data_to_ptr (fd_data);
    local->call_count = 1;
  
    child_fd_data = dict_get (local->fd->ctx, child->name);
    if (child_fd_data) {
      STACK_WIND (frame,
		  unify_buf_cbk,
		  child,
		  child->fops->fchmod,
		  (fd_t *)data_to_ptr (child_fd_data),
		  local->mode);
    }
  }
  
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
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  data_t *child_fd_data = NULL;

  /* Initialization */
  INIT_LOCAL (frame, local);
  local->fd = fd;
  local->mode = mode;

  list = fd->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      child_fd_data = dict_get (fd->ctx, ino_list->xl->name);
      if (child_fd_data) {
	STACK_WIND (frame,
		    unify_fchmod_cbk,
		    NS(this),
		    NS(this)->fops->fchmod,
		    (fd_t *)data_to_ptr (child_fd_data),
		    mode);
	break;
      }
    }
  }

  return 0;
}

/**
 * unify_fchown_cbk - 
 */
static int32_t
unify_fchown_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
  struct list_head *list = NULL;
  data_t *child_fd_data = NULL;
  call_frame_t *bg_frame = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;

  if (op_ret == -1) {
    /* No need to send fchown request to other storage nodes, 
     * as namespace action failed 
     */
    if (op_errno == CHILDDOWN)
      op_errno = EIO;
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = 0;
  local->stbuf = *buf;
  list = local->fd->inode->private;
    
  if (S_ISDIR (buf->st_mode)) {
    /* If directory, get a copy of the current frame, and set 
     * the current local to bg_frame's local 
     */
    bg_frame = copy_frame (frame);
    frame->local = NULL;
    bg_frame->local = local;
    LOCK_INIT (&bg_frame->mutex);
    
    /* Unwind this frame, and continue with bg_frame */
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    
    /* Send chmod request to all the nodes now */
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;
    local->call_count--; /* Reduce 1 for namespace entry */
    
    list_for_each_entry (ino_list, list, list_head) {
      if (ino_list->xl != NS(this)) {
	child_fd_data = dict_get (local->fd->ctx, ino_list->xl->name);
	if (child_fd_data) {
	  STACK_WIND (bg_frame,
		      unify_bg_buf_cbk,
		      ino_list->xl,
		      ino_list->xl->fops->fchown,
		      (fd_t *)data_to_ptr (child_fd_data),
		      local->uid,
		      local->gid);
	}
      }
    }
  } else {
    /* Its not a directory */
    xlator_t *child = NULL;
    data_t *fd_data = dict_get (local->fd->ctx, this->name);
    
    if (!fd_data) {
      STACK_UNWIND (frame, -1, EBADFD, "");
      return 0;
    }
    child = data_to_ptr (fd_data);
    local->call_count = 1;

    child_fd_data = dict_get (local->fd->ctx, child->name);
    if (child_fd_data) {
      STACK_WIND (frame,
		  unify_buf_cbk,
		  child,
		  child->fops->fchown,
		  data_to_ptr (child_fd_data),
		  local->uid,
		  local->gid);
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
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  data_t *child_fd_data = NULL;

  /* Initialization */
  INIT_LOCAL(frame, local);
  local->fd = fd;
  local->uid = uid;
  local->gid = gid;

  list = fd->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      child_fd_data = dict_get (fd->ctx, ino_list->xl->name);
      if (child_fd_data) {
	STACK_WIND (frame,
		    unify_fchown_cbk,
		    NS(this),
		    NS(this)->fops->fchown,
		    (fd_t *)data_to_ptr (child_fd_data),
		    uid,
		    gid);
	break;
      }
    }
  }

  return 0;
}

/**
 * unify_flush_cbk - 
 */
static int32_t
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
  data_t *child_fd_data = NULL;
  data_t *fd_data = dict_get (fd->ctx, this->name);

  if (!fd_data) {
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }
  child = data_to_ptr (fd_data);
  
  child_fd_data = dict_get (fd->ctx, child->name);
  if (child_fd_data) {
    STACK_WIND (frame,
		unify_flush_cbk,
		child,
		child->fops->flush,
		(fd_t *)data_to_ptr (child_fd_data));
  }

  return 0;
}

/**
 * unify_close_cbk -
 */
static int32_t
unify_close_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->mutex);

  if (op_ret == 0) 
    local->op_ret = 0;
  
  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
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
  data_t *child_fd_data = NULL;
  unify_local_t *local = NULL;
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;

  /* Init */
  INIT_LOCAL (frame, local);
  local->inode = fd->inode;
  local->fd = fd;

  list = local->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    child_fd_data = dict_get (fd->ctx, ino_list->xl->name);
    if (child_fd_data) 
      local->call_count++;
  }

  list_for_each_entry (ino_list, list, list_head) {
    child_fd_data = dict_get (fd->ctx, ino_list->xl->name);
    if (child_fd_data) {
      STACK_WIND (frame,
		  unify_close_cbk,
		  ino_list->xl,
		  ino_list->xl->fops->close,
		  (fd_t *)data_to_ptr (child_fd_data));
    }
  }

  fd_destroy (fd);

  return 0;
}

/**
 * unify_fsync_cbk - 
 */
static int32_t
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
  data_t *child_fd_data = NULL;
  data_t *fd_data = dict_get (fd->ctx, this->name);

  if (!fd_data) {
    STACK_UNWIND (frame, -1, EBADFD, "");
    return 0;
  }
  child = data_to_ptr (fd_data);

  child_fd_data = dict_get (fd->ctx, child->name);
  if (child_fd_data) {
    STACK_WIND (frame,
		unify_fsync_cbk,
		child,
		child->fops->fsync,
		(fd_t *)data_to_ptr (child_fd_data),
		flags);
  }

  return 0;
}

/**
 * unify_fstat_cbk - 
 */
static int32_t
unify_fstat_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
    
    if (op_ret == -1 && op_errno != CHILDDOWN)
      local->op_errno = op_errno;
    
    if (op_ret == 0) {
      local->op_ret = 0;
      /* Get most of the variables of struct stat from NameSpace */
      if (NS(this) == (xlator_t *)cookie) {
	local->stbuf = *buf;
      } else if (!S_ISDIR (buf->st_mode)) {
	/* If file, then add size from each file */
	local->stbuf.st_size += buf->st_size;
	local->stbuf.st_blocks += buf->st_blocks;
      }
    }
  }
  UNLOCK (&frame->mutex);

  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }

  return 0;
}

/**
 * unify_fstat - Send fstat FOP to Namespace only if its directory, and to both 
 *     namespace and the storage node if its a file.
 */
int32_t
unify_fstat (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd)
{
  data_t *child_fd_data = NULL;
  unify_local_t *local = NULL;
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;

  /* Init */
  INIT_LOCAL (frame, local);
  local->inode = fd->inode;
  list = local->inode->private;

  if (fd->inode->isdir) {
    /* Directory */
    local->call_count = 1;
  
    list_for_each_entry (ino_list, list, list_head) {
      if (ino_list->xl == NS(this)) {
	child_fd_data = dict_get (fd->ctx, ino_list->xl->name);
	if (child_fd_data) {
	  _STACK_WIND (frame,
		       unify_fstat_cbk,
		       NS(this),
		       NS(this),
		       NS(this)->fops->fstat,
		       (fd_t *)data_to_ptr (child_fd_data));
	  break;
	}
      }
    }
  } else {
    /* Entry is a file */
    local->call_count = 2;

    list_for_each_entry (ino_list, list, list_head) {
      child_fd_data = dict_get (fd->ctx, ino_list->xl->name);
      if (child_fd_data) {
	_STACK_WIND (frame,
		     unify_fstat_cbk,
		     ino_list->xl, //cookie
		     ino_list->xl,
		     ino_list->xl->fops->fstat,
		     (fd_t *)data_to_ptr (child_fd_data));
      }
    }
  }
  return 0;
}

/**
 * unify_readdir_cbk - this function gets entries from all the nodes (both 
 *        storage nodes and namespace). Here the directory entry is taken only
 *        from only namespace, (if it exists there) and the files are taken 
 *        from storage nodes.
 */
static int32_t
unify_readdir_cbk (call_frame_t *frame,
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
	      dir_entry_t *local_trav = local->entry->next;
	      while (local_trav) {
		if (strcmp (local_trav->name, tmp->name) == 0) {
		  /* Found the directory name in earlier entries. */
		  flag = 1;
		  /* Update the nlink field */
		  if (local_trav->buf.st_nlink < tmp->buf.st_nlink)
		    local_trav->buf.st_nlink = tmp->buf.st_nlink;

		  if ((local_trav->buf.st_mode != tmp->buf.st_mode) ||
		      (local_trav->buf.st_uid != tmp->buf.st_uid) ||
		      (local_trav->buf.st_gid != tmp->buf.st_gid)) {
		    /* There is some inconsistency in storagenodes. Do a 
		     * self heal 
		     */
		    gf_log (this->name,
			    GF_LOG_WARNING,
			    "found mismatch in mode/uid/gid for %s",
			    tmp->name);
		    local->failed = 1;
		  }
		  break;
		}
		local_trav = local_trav->next;
	      }
	      if (flag) {
		/* if its set, it means entry is already present, so remove 
		 * entries from current list.
		 */ 
		prev->next = tmp->next;
		trav = tmp->next;
		free (tmp->name);
		free (tmp);
		tmp_count--;
		continue;
	      } else {
		/* This means, there is miss match in storage nodes itself. */
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
	local->ns_entry = unify_entry;
	unify_entry->next = entry->next;
	local->ns_count = count;
      }
      /* This makes child nodes to free only head, and all dir_entry_t 
       * structures are kept reference at this level.
       */
      entry->next = NULL;
    } 

    /* If there is an error, other than ENOTCONN, its failure */
    if ((op_ret == -1 && op_errno != CHILDDOWN)) {
      local->op_ret = -1;
      local->op_errno = op_errno;
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->mutex);

  if (!callcnt) {
    /* unwind the current frame with proper entries */
    frame->local = NULL;
    if ((local->op_ret >= 0) && local->ns_entry && local->entry) {
      /* put the stat buf's 'st_ino' in the buf of readdir entries */
      {
	dir_entry_t *ns_trav = local->ns_entry->next;
	while (ns_trav) {
	  /* If its a file in the namespace, then replace its size from
	   * actual storage nodes 
	   */
	  prev = local->entry;
	  trav = local->entry->next;
	  while (trav) {
	    if (strcmp (ns_trav->name, trav->name) == 0) {
	      /* Update the namespace entries->buf */
	      if (!S_ISDIR(ns_trav->buf.st_mode)) {
		ns_trav->buf.st_size = trav->buf.st_size;
		ns_trav->buf.st_blocks = trav->buf.st_blocks;
	      }
	      if ((ns_trav->buf.st_mode != trav->buf.st_mode) ||
		  (ns_trav->buf.st_uid != trav->buf.st_uid) ||
		  (ns_trav->buf.st_gid != trav->buf.st_gid)) {
		/* There is some inconsistency in storagenodes. Do a 
		 * self heal 
		 */
		gf_log (this->name,
			GF_LOG_WARNING,
			"found mismatch in mode/uid/gid for %s",
			trav->name);
		local->failed = 1;
	      }
	      /* Free this entry, as its been added to namespace */
	      tmp = trav;
	      prev->next = tmp->next;
	      trav = tmp->next;
	      free (tmp->name);
	      free (tmp);
	      local->count--;
	      break;
	    }
	    prev = trav;
	    trav = trav->next;
	  }
	  ns_trav = ns_trav->next;
	}
      }
      
      /* Do basic level of self heal here, if directory is not empty */
      unify_readdir_self_heal (frame, this, local->fd, local);
    }

    if (!local->ns_entry) {
      local->op_ret = -1;
      local->op_errno = ENOENT;
    }
    STACK_UNWIND (frame, 
		  local->op_ret, 
		  local->op_errno, 
		  local->ns_entry, 
		  local->ns_count);

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
      free (local);
    }
  }
  
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
  data_t *child_fd_data = NULL;
  unify_local_t *local = NULL;
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;

  /* Init */
  INIT_LOCAL (frame, local);
  local->inode = fd->inode;
  local->fd = fd;

  list = local->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    child_fd_data = dict_get (fd->ctx, ino_list->xl->name);
    if (child_fd_data)
      local->call_count++;
  }

  list_for_each_entry (ino_list, list, list_head) {
    child_fd_data = dict_get (fd->ctx, ino_list->xl->name);
    if (child_fd_data) {
      _STACK_WIND (frame,
		   unify_readdir_cbk,
		   ino_list->xl, //cookie
		   ino_list->xl,
		   ino_list->xl->fops->readdir,
		   size,
		   offset,
		   data_to_ptr (child_fd_data));
    }
  }

  return 0;
}

/**
 * unify_closedir_cbk - 
 */
static int32_t
unify_closedir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
    if (op_ret == 0)
      local->op_ret = 0;
  }
  UNLOCK (&frame->mutex);
  
  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
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
  data_t *child_fd_data = NULL;
  unify_local_t *local = NULL;
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;

  /* Init */
  INIT_LOCAL (frame, local);
  local->inode = fd->inode;
  local->fd = fd;

  list = local->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    child_fd_data = dict_get (fd->ctx, ino_list->xl->name);
    if (child_fd_data)
      local->call_count++;
  }

  list_for_each_entry (ino_list, list, list_head) {
    child_fd_data = dict_get (fd->ctx, ino_list->xl->name);
    if (child_fd_data) {
      STACK_WIND (frame,
		  unify_closedir_cbk,
		  ino_list->xl,
		  ino_list->xl->fops->closedir,
		  (fd_t *)data_to_ptr (child_fd_data));
    }
  }

  fd_destroy (fd);

  return 0;
}

/**
 * unify_fsyncdir_cbk - 
 */
static int32_t
unify_fsyncdir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
    
    if (op_ret == -1 && op_errno != CHILDDOWN)
      local->op_errno = op_errno;
    
    if (op_ret == 0) 
      local->op_ret = 0;
  }
  UNLOCK (&frame->mutex);
  
  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
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
  data_t *child_fd_data = NULL;
  unify_local_t *local = NULL;
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;

  /* Init */
  INIT_LOCAL (frame, local);
  local->inode = fd->inode;
  list = local->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    child_fd_data = dict_get (fd->ctx, ino_list->xl->name);
    if (child_fd_data)
      local->call_count++;
  }

  list_for_each_entry (ino_list, list, list_head) {
    child_fd_data = dict_get (fd->ctx, ino_list->xl->name);
    if (child_fd_data) {
      STACK_WIND (frame,
		  unify_fsyncdir_cbk,
		  ino_list->xl,
		  ino_list->xl->fops->fsyncdir,
		  (fd_t *)data_to_ptr (child_fd_data),
		  flags);
    }
  }
  return 0;
}

/**
 * unify_lk_cbk - UNWIND frame with the proper return arguments.
 */
static int32_t
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
  data_t *child_fd_data = NULL;
  data_t *fd_data = dict_get (fd->ctx, this->name);

  if (!fd_data) {
    STACK_UNWIND (frame, -1, EBADFD, "");
    return 0;
  }
  child = data_to_ptr (fd_data);
  child_fd_data = dict_get (fd->ctx, child->name);
  if (child_fd_data) {
    STACK_WIND (frame,
		unify_lk_cbk,
		child,
		child->fops->lk,
		(fd_t *)data_to_ptr (child_fd_data),
		cmd,
		lock);
  }

  return 0;
}

/**
 * unify_setxattr_cbk - When all the child nodes return, UNWIND frame.
 */
static int32_t
unify_setxattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    callcnt = --local->call_count;
    
    if (op_ret == -1 && op_errno != CHILDDOWN)
      local->op_errno = op_errno;
    
    if (op_ret == 0)
      local->op_ret = 0;
  }
  UNLOCK (&frame->mutex);
  
  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
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
  unify_local_t *local = NULL;
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) 
    local->call_count++;
  local->call_count--;

  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl != NS(this)) {
      loc_t tmp_loc = {
	.path = loc->path, 
	.ino = ino_list->inode->ino, 
	.inode = ino_list->inode
      };
      STACK_WIND (frame,
		  unify_setxattr_cbk,
		  ino_list->xl,
		  ino_list->xl->fops->setxattr,
		  &tmp_loc,
		  dict,
		  flags);
    }
  }

  return 0;
}


/**
 * unify_getxattr_cbk - This function is called from only one child, so, no
 *     need of any lock or anything else, just send it to above layer 
 */
static int32_t
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
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl != NS(this)) {
      loc_t tmp_loc = {
	.path = loc->path, 
	.ino = ino_list->inode->ino, 
	.inode = ino_list->inode
      };
      STACK_WIND (frame,
		  unify_getxattr_cbk,
		  ino_list->xl,
		  ino_list->xl->fops->getxattr,
		  &tmp_loc);
      break;
    }
  }

  return 0;
}

/**
 * unify_removexattr_cbk - Wait till all the child node returns the call and then
 *    UNWIND to above layer.
 */
static int32_t
unify_removexattr_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  { 
    callcnt = --local->call_count;
    if (op_ret == -1)
      local->op_errno = op_errno;
    if (op_ret == 0)
      local->op_ret = 0;
  }
  UNLOCK (&frame->mutex);  

  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
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
  unify_local_t *local = NULL;
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) 
    local->call_count++;
  local->call_count--; /* on NS its not done */
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl != NS(this)) {
      loc_t tmp_loc = {
	.path = loc->path, 
	.ino = ino_list->inode->ino, 
	.inode = ino_list->inode
      };
      STACK_WIND (frame,
		  unify_removexattr_cbk,
		  ino_list->xl,
		  ino_list->xl->fops->removexattr,
		  &tmp_loc,
		  name);
    }
  }

  return 0;
}


/**
 * unify_mknod_cbk - 
 */
static int32_t
unify_mknod_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *buf)
{
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;
  inode_t *loc_inode = NULL;

  if (op_ret >= 0) {
    list = local->inode->private;

    ino_list = calloc (1, sizeof (unify_inode_list_t));
    ino_list->xl = (xlator_t *)cookie;
    ino_list->inode = inode_ref (inode);

    /* Add entry to NameSpace's inode */
    list_add (&ino_list->list_head, list);
  }
  loc_inode = local->inode;
  unify_local_wipe (local);
  STACK_UNWIND (frame, op_ret, op_errno, loc_inode, &local->stbuf);

  if (loc_inode)
    inode_unref (loc_inode);

  return 0;
}

/**
 * unify_ns_mknod_cbk - 
 */
static int32_t
unify_ns_mknod_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf)
{
  struct list_head *list = NULL;
  struct sched_ops *sched_ops = NULL;
  xlator_t *sched_xl = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;
  
  if (op_ret == -1) {
    /* No need to send mknod request to other servers, 
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
  local->inode = inode_update (this->itable, NULL, NULL, buf);
  
  /* Start the mapping list */
  list = calloc (1, sizeof (struct list_head));
  INIT_LIST_HEAD (list);
  local->inode->private = (void *)list;
  
  ino_list = calloc (1, sizeof (unify_inode_list_t));
  ino_list->xl = NS (this);
  ino_list->inode = inode_ref (inode);
  /* Add entry to NameSpace's inode */
  list_add (&ino_list->list_head, list);
  
  sched_ops = ((unify_private_t *)this->private)->sched_ops;

  /* Send mknod request to scheduled node now */
  sched_xl = sched_ops->schedule (this, 0); 
  _STACK_WIND (frame,
	       unify_mknod_cbk,
	       sched_xl,
	       sched_xl,
	       sched_xl->fops->mknod,
	       local->name,
	       local->mode,
	       local->dev);

  return 0;
}

/**
 * unify_mknod - Create a device on namespace first, and later create on 
 *       the storage node.
 */
int32_t
unify_mknod (call_frame_t *frame,
	     xlator_t *this,
	     const char *name,
	     mode_t mode,
	     dev_t rdev)
{
  unify_local_t *local = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->name = strdup (name);
  local->mode = mode;
  local->dev = rdev;
  
  STACK_WIND (frame,
	      unify_ns_mknod_cbk,
	      NS(this),
	      NS(this)->fops->mknod,
	      name,
	      mode,
	      rdev);

  return 0;
}

/**
 * unify_symlink_cbk - 
 */
static int32_t
unify_symlink_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
		   struct stat *buf)
{
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;
  inode_t *loc_inode = NULL;
  
  if (op_ret >= 0) {
    list = local->inode->private;
    
    ino_list = calloc (1, sizeof (unify_inode_list_t));
    ino_list->xl = (xlator_t *)cookie;
    ino_list->inode = inode_ref (inode);
    /* Add entry to NameSpace's inode */
    list_add (&ino_list->list_head, list);
    
  }
  loc_inode = local->inode;
  unify_local_wipe (local);
  STACK_UNWIND (frame, op_ret, op_errno, loc_inode, &local->stbuf);

  if (loc_inode)
    inode_unref (loc_inode);

  return 0;
}

/**
 * unify_ns_symlink_cbk - 
 */
static int32_t
unify_ns_symlink_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      inode_t *inode,
		      struct stat *buf)
{
  struct list_head *list = NULL;
  struct sched_ops *sched_ops = NULL;
  xlator_t *sched_xl = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;
  
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
  local->inode = inode_update (this->itable, NULL, NULL, buf);
  
  /* Start the mapping list */
  list = calloc (1, sizeof (struct list_head));
  INIT_LIST_HEAD (list);
  local->inode->private = (void *)list;

  ino_list = calloc (1, sizeof (unify_inode_list_t));
  ino_list->xl = NS (this);
  ino_list->inode = inode_ref (inode);
  /* Add entry to NameSpace's inode */
  list_add (&ino_list->list_head, list);
  
  sched_ops = ((unify_private_t *)this->private)->sched_ops;

  /* Send symlink request to all the nodes now */
  sched_xl = sched_ops->schedule (this, 0); 
  _STACK_WIND (frame,
	       unify_symlink_cbk,
	       sched_xl,
	       sched_xl,
	       sched_xl->fops->symlink,
	       local->path,
	       local->name);

  return 0;
}

/**
 * unify_symlink - 
 */
int32_t
unify_symlink (call_frame_t *frame,
	       xlator_t *this,
	       const char *linkpath,
	       const char *name)
{
  unify_local_t *local = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->path = strdup (linkpath);
  local->name = strdup (name);
  
  STACK_WIND (frame,
	      unify_ns_symlink_cbk,
	      NS(this),
	      NS(this)->fops->symlink,
	      linkpath,
	      name);

  return 0;
}

/**
 * unify_ns_rename_cbk - Namespace rename callback. 
 */
static int32_t
unify_ns_rename_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;
  
  if (op_ret == -1) {
    /* No need to send rename request to other servers, 
     * as namespace action failed 
     */
    unify_local_wipe (local);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = 0;
  local->stbuf = *buf;
  
  if (local->new_inode && !local->new_inode->isdir) {
    /* if the target path exists, and if its not directory, send unlink  
     * to the target (to the node where it resides). Check for ! directory 
     * is added, because, rename on namespace could have successed only if 
     * its an empty directory and it exists on all nodes. So, anyways, 'fops->rename' 
     * call will handle it.
     */
    call_frame_t *bg_frame = NULL;
    unify_local_t *bg_local = NULL;

    bg_frame = copy_frame (frame);

    INIT_LOCAL (bg_frame, bg_local);

    bg_local->call_count = 1;

    list = local->new_inode->private;
    list_for_each_entry (ino_list, list, list_head) {
      if (NS(this) != ino_list->xl) {
	loc_t tmp_loc = {
	  .path = local->name,
	  .ino = ino_list->inode->ino,
	  .inode = ino_list->inode,
	};
	STACK_WIND (bg_frame,
		    unify_bg_cbk,
		    ino_list->xl,
		    ino_list->xl->fops->unlink,
		    &tmp_loc);
      }
    }
  } 

  /* Send 'fops->rename' request to all the nodes where 'oldloc->path' exists. 
   * The case of 'newloc' being existing is handled already.
   */
  list = local->inode->private;
  local->call_count = 0;
  list_for_each_entry (ino_list, list, list_head)
    local->call_count++;
  local->call_count--;

  list_for_each_entry (ino_list, list, list_head) {
    if (NS(this) != ino_list->xl) {
      loc_t tmp_loc = {
	.path = local->path,
	.inode = ino_list->inode,
	.ino = ino_list->inode->ino
      };
      loc_t tmp_newloc = {
	.path = local->name,
	.inode = NULL,
	.ino = 0
      };
      STACK_WIND (frame,
		  unify_buf_cbk,
		  ino_list->xl,
		  ino_list->xl->fops->rename,
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
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list2 = NULL;
  struct list_head *list2 = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->path = strdup (oldloc->path);
  local->name = strdup (newloc->path);
  local->inode = oldloc->inode;

  list = oldloc->inode->private;
  if (newloc->inode) {
    /* if 'newloc->inode' is true, that means there is a file existing 
     * in that path. Anyways, send the rename request to the Namespace 
     * first with corresponding 'loc_t' values 
     */
    local->new_inode = newloc->inode;
    list2 = newloc->inode->private;

    list_for_each_entry (ino_list, list, list_head) {
      list_for_each_entry (ino_list2, list2, list_head) {
	if (ino_list->xl == NS(this) && ino_list2->xl == NS(this)) {
	  loc_t tmp_loc = {
	    .path = oldloc->path, 
	    .ino = ino_list->inode->ino, 
	    .inode = ino_list->inode
	  };
	  loc_t tmp_loc2 = {
	    .path = newloc->path, 
	    .ino = ino_list2->inode->ino, 
	    .inode = ino_list2->inode
	  };
	  STACK_WIND (frame,
		      unify_ns_rename_cbk,
		      NS(this),
		      NS(this)->fops->rename,
		      &tmp_loc,
		      &tmp_loc2);
	  break;
	}
      }
    }
  } else {
    /* 'newloc->inode' is false. this means, the target path in case of rename 
     * is not present in the filesystem. So, send corresponding 'loc_t' for 
     * oldpath, to namespace first.
     */
    list_for_each_entry (ino_list, list, list_head) {
      if (ino_list->xl == NS(this)) {
	loc_t tmp_loc = {
	  .path = oldloc->path, 
	  .ino = ino_list->inode->ino, 
	  .inode = ino_list->inode
	};
	STACK_WIND (frame,
		    unify_ns_rename_cbk,
		    NS(this),
		    NS(this)->fops->rename,
		    &tmp_loc,
		    newloc);
	break;
      }
    }
  }

  return 0;
}

/**
 * unify_link_cbk -
 */
static int32_t
unify_link_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		inode_t *inode,
		struct stat *buf)
{
  inode_t *loc_inode = NULL;
  unify_local_t *local = frame->local;

  loc_inode = local->inode;
  unify_local_wipe (local);
  LOCK_DESTROY (&frame->mutex);
  STACK_UNWIND (frame, op_ret, op_errno, local->inode, &local->stbuf);
  if (loc_inode)
    inode_unref (loc_inode);

  return 0;
}

/**
 * unify_ns_link_cbk - 
 */
static int32_t
unify_ns_link_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
		   struct stat *buf)
{
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;

  if (op_ret == -1) {
    /* No need to send link request to other servers, 
     * as namespace action failed 
     */
    unify_local_wipe (local);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  NULL,
		  buf);
    return 0;
  }

  list = local->inode->private;

  /* Update inode for this entry */
  local->op_ret = 0;
  local->stbuf = *buf;
  local->inode = inode_update (this->itable, NULL, NULL, buf);

  /* Send link request to the node now */
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl != NS (this)) {
      loc_t tmp_loc = {
	.inode = ino_list->inode,
	.path = local->path,
	.ino = ino_list->inode->ino,
      };
      STACK_WIND (frame,
		  unify_link_cbk,
		  ino_list->xl,
		  ino_list->xl->fops->link,
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
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->path = strdup (loc->path);
  local->name = strdup (newname);
  local->inode = loc->inode;

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      loc_t tmp_loc = {
	.path = loc->path, 
	.ino = ino_list->inode->ino, 
	.inode = ino_list->inode
      };
      STACK_WIND (frame,
		  unify_ns_link_cbk,
		  NS(this),
		  NS(this)->fops->link,
		  &tmp_loc,
		  newname);
    }
  }

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
	sched->update (this);
	
	LOCK (&priv->mutex);
	{
	  /* Increment the inode's generation, which is used for self_heal */
	  ++priv->inode_generation;
	}
	UNLOCK (&priv->mutex);
      }
      break;
    case GF_EVENT_CHILD_DOWN:
      {
	/* Call scheduler's update () to disable the child node 
	 * for scheduling
	 */
	sched->update (this);
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
    
    _private->self_heal = 1;
    self_heal = dict_get (this->options, "self-heal");
    if (self_heal) {
      if (strcmp (self_heal->data, "off") == 0) {
	_private->self_heal = 0;
      }
    }
    
    /* self-heal part, start with generation '1' */
    LOCK_INIT (&_private->mutex);
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
      free (_private);
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
      free (_private);
      return -1;
    }
  }

  /* Get the inode table of the child nodes */
  {
    unify_inode_list_t *ilist = NULL;
    struct list_head *list = NULL;
    int32_t lru_limit = 1000;
    data_t *lru_data = NULL;

    lru_data = dict_get (this->options, "inode-lru-limit");
    if (!lru_data){
      gf_log (this->name, 
	      GF_LOG_DEBUG,
	      "missing 'inode-lru-limit'. defaulting to 1000");
      dict_set (this->options,
		"inode-lru-limit",
		data_from_uint64 (lru_limit));
    } else {
      lru_limit = data_to_uint64 (lru_data);
    }

    /* Create a inode table for this xlator */
    this->itable = inode_table_new (lru_limit, this->name);
    
    /* Create a mapping list */
    list = calloc (1, sizeof (struct list_head));
    INIT_LIST_HEAD (list);

    /* Storage nodes */
    trav = this->children;
    while (trav) {
      ilist = calloc (1, sizeof (unify_inode_list_t));
      ilist->xl = trav->xlator;
      ilist->inode = inode_ref (trav->xlator->itable->root);
      list_add (&ilist->list_head, list);
      trav = trav->next;
    }
    /* Namespace node */
    ilist = calloc (1, sizeof (unify_inode_list_t));
    ilist->xl = ns_xl;
    ilist->inode = inode_ref (ns_xl->itable->root);
    list_add (&ilist->list_head, list);
    
    this->itable->root->isdir = 1;        /* always '/' is directory */
    this->itable->root->private = (void *)list;
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
  LOCK_DESTROY (&priv->mutex);
  free (priv);
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
