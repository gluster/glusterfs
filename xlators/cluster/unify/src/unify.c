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
    freee (local->path);
  }
  if (local->name) {
    freee (local->name);
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

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    if (op_ret == 0)
      local->op_ret = 0;
  }
  UNLOCK (&frame->lock);

  if (!callcnt) {
    unify_local_wipe (local);
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

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    
    if (local->op_ret == -1 && op_errno != CHILDDOWN)
      local->op_errno = op_errno;

    if (op_ret >= 0) {
      local->op_ret = op_ret;

      if (NS (this) == (xlator_t *)cookie)
	local->stbuf = *buf;

      /* If file, then replace size of file in stat info. */
      if ((!S_ISDIR (buf->st_mode)) && (NS (this) != (xlator_t *)cookie)) {
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

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
 
    if (op_ret == -1) {
      local->op_errno = op_errno;
      local->failed = 1;
    }
  }
  UNLOCK (&frame->lock);

  if (op_ret == 0) {
    if (!local->revalidate) {
      ino_list = calloc (1, sizeof (unify_inode_list_t));
      ino_list->xl = (xlator_t *)cookie;
      
      LOCK (&frame->lock);
      {
	if (!local->list) {
	  local->list = calloc (1, sizeof (struct list_head));
	  INIT_LIST_HEAD (local->list);
	}
	/* This is to be used as hint from the inode and also mapping */
	list_add (&ino_list->list_head, local->list);
      }
      UNLOCK (&frame->lock);
    }

    LOCK (&frame->lock);
    {
      local->op_ret = 0; 
      /* Replace most of the variables from NameSpace */
      if (NS(this) == (xlator_t *)cookie) {
	local->stbuf = *buf;
	inode->st_mode = buf->st_mode;
      } else {
	if (!S_ISDIR (buf->st_mode)) {
	  /* If file, then replace size of file in stat info */
	  local->st_size = buf->st_size;
	  local->st_blocks = buf->st_blocks;
	  ///local->st_mtime = buf->st_mtime;
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
	    list_add (&ino_list->list_head, local->list);
	  }
	}
      }
    }
    UNLOCK (&frame->lock);
  }

  if (!callcnt) {
    if (!local->stbuf.st_blksize) {
      /* Inode not present */
      local->op_ret = -1;
    } else {
      if (!local->revalidate) { 
	dict_set (inode->ctx, this->name, data_from_static_ptr (local->list));
      }
      if (S_ISDIR(inode->st_mode)) {
	/* lookup is done for directory */
	if (local->failed) {
	  inode->generation = 0; /*means, self-heal required for inode*/
	  priv->inode_generation++;
	}
      } else {
	local->stbuf.st_size = local->st_size;
	local->stbuf.st_blocks = local->st_blocks;
	///local->stbuf.st_mtime = local->st_mtime;
      }

      local->stbuf.st_nlink = local->st_nlink;
    }

    if ((priv->self_heal) && 
	((local->op_ret == 0) && S_ISDIR(inode->st_mode))) {
      /* Let the self heal be done here */
      local->inode = inode;
      gf_unify_self_heal (frame, this, local);
    } else {
      /* either no self heal, or failure */
      unify_local_wipe (local);
      STACK_UNWIND (frame, 
		    local->op_ret, 
		    local->op_errno, 
		    inode, 
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
  unify_inode_list_t *ino_list = NULL;
  xlator_list_t *trav = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->path = strdup (loc->path);
  
  if (dict_get (loc->inode->ctx, this->name)) 
    local->list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  if (local->list) {
    list = local->list;
    local->revalidate = 1;
    if (!S_ISDIR (loc->inode->st_mode)) {
      list_for_each_entry (ino_list, list, list_head)
	local->call_count++;
      
      list_for_each_entry (ino_list, list, list_head) {
	_STACK_WIND (frame,
		     unify_lookup_cbk,
		     ino_list->xl,
		     ino_list->xl,
		     ino_list->xl->fops->lookup,
		     loc);
      }
    } else {
      /* This is first call, there is no list */
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
  } else {
    /* This is first call, there is no list */
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
 * unify_forget - call inode_forget which removes it from cache 
 */
int32_t 
unify_forget (call_frame_t *frame,
	      xlator_t *this,
	      inode_t *inode)
{
  unify_inode_list_t *ino_list = NULL;
  unify_inode_list_t *ino_list_prev = NULL;
  struct list_head *list = data_to_ptr (dict_get (inode->ctx, this->name));

  /* Unref and free the inode->private list */
  ino_list_prev = NULL;
  list_for_each_entry_safe (ino_list, ino_list_prev, list, list_head) {
    list_del (&ino_list->list_head);
    freee (ino_list);
  }
  freee (list);
  
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
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    
    if (op_ret == -1 && op_errno != CHILDDOWN) {
      local->op_errno = op_errno;
      local->failed = 1;
    }
    if (op_ret == 0) {
      local->op_ret = 0;
      /* Replace most of the variables from NameSpace */
      if (NS(this) == ((call_frame_t *)cookie)->this) {
	local->stbuf = *buf;
      } else {
	if (!S_ISDIR (buf->st_mode)) {
	  /* If file, then get the size of file from storage node */
	  local->st_size = buf->st_size;
	  local->st_blocks = buf->st_blocks;
	  ///local->st_mtime = buf->st_mtime;
	}
      }
      if (buf->st_nlink > local->st_nlink)
	local->st_nlink = buf->st_nlink;
    }
  }
  UNLOCK (&frame->lock);
    
  if (!callcnt) {
    if (local->stbuf.st_blksize) {
      /* If file, update the size and blocks in 'stbuf' to be returned */
      if (!S_ISDIR(local->stbuf.st_mode)) {
	local->stbuf.st_size = local->st_size;
	local->stbuf.st_blocks = local->st_blocks;
	///local->stbuf.st_mtime = local->st_mtime;
      } 
      local->stbuf.st_nlink = local->st_nlink;
    } else {
      local->inode->generation = 0; /* self-heal required */
      local->op_ret = -1;
    }

    unify_local_wipe (local);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
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
  
  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  list_for_each_entry (ino_list, list, list_head)
    local->call_count++;
  
  list_for_each_entry (ino_list, list, list_head) {
    STACK_WIND (frame,
		unify_stat_cbk,
		ino_list->xl,
		ino_list->xl->fops->stat,
		loc);
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

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      STACK_WIND (frame,
		  unify_access_cbk,
		  NS(this),
		  NS(this)->fops->access,
		  loc,
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
  unify_private_t *priv = this->private;
  int32_t callcnt = 0;
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);
  if (op_ret == -1) {
    local->failed = 1;
  }
  if (op_ret >= 0) {
    ino_list = calloc (1, sizeof (unify_inode_list_t));
    ino_list->xl = ((call_frame_t *)cookie)->this;
    
    LOCK (&frame->lock);
    {
      local->op_ret = 0;
      /* This is to be used as hint from the inode and also mapping */
      list = data_to_ptr (dict_get (inode->ctx, this->name));
      list_add (&ino_list->list_head, list);
    }
    UNLOCK (&frame->lock);
  }
  
  if (!callcnt) {
    unify_local_wipe (local);
    if (!local->failed)
      inode->generation = priv->inode_generation;
    STACK_UNWIND (frame, 
		  local->op_ret, 
		  local->op_errno, 
		  inode, 
		  &local->stbuf);
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

  list = calloc (1, sizeof (struct list_head));
  
  /* Start the mapping list */
  INIT_LIST_HEAD (list);
  dict_set (inode->ctx, this->name, data_from_static_ptr (list));

  ino_list = calloc (1, sizeof (unify_inode_list_t));
  ino_list->xl = NS (this);

  /* Add entry to NameSpace's inode */
  list_add (&ino_list->list_head, list);
  local->call_count = priv->child_count;

  /* Send mkdir request to all the nodes now */
  trav = this->children;
  while (trav) {
    loc_t tmp_loc = {
      .inode = inode,
      .path = local->name
    };
    STACK_WIND (frame,
		unify_mkdir_cbk,
		trav->xlator,
		trav->xlator->fops->mkdir,
		&tmp_loc,
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
	     loc_t *loc,
	     mode_t mode)
{
  unify_local_t *local = NULL;

  /* Initialization */
  INIT_LOCAL (frame, local);
  local->name = strdup (loc->path);
  local->mode = mode;
  local->inode = loc->inode;

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
static int32_t
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
  int32_t call_count = 0;
  
  if (op_ret == -1) {
     /* No need to send rmdir request to other servers, 
     * as namespace action failed 
     */
    if (op_errno == CHILDDOWN)
      op_errno = EIO;
    unify_local_wipe (local);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno);
    return 0;
  }
  
  list = local->list;
  list_for_each_entry (ino_list, list, list_head)
    local->call_count++;
  local->call_count--;

  call_count = local->call_count;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl != NS(this)) {
      loc_t tmp_loc = {
	.path = local->path, 
	.inode = local->inode
      };
      STACK_WIND (frame,
		  unify_rmdir_cbk,
		  ino_list->xl,
		  ino_list->xl->fops->rmdir,
		  &tmp_loc);
      call_count--;
      if (call_count == 0)
	break;
    }
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
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->path = strdup (loc->path);
  local->inode = loc->inode;
  local->list = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  list = local->list;

  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      STACK_WIND (frame,
		  unify_ns_rmdir_cbk,
		  NS(this),
		  NS(this)->fops->rmdir,
		  loc);
      	break;
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
  unify_openfd_t *openfd = NULL;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    if (op_ret >= 0) {
      local->op_ret = op_ret;
      openfd = calloc (1, sizeof (*openfd));
      openfd->xl = cookie;
      if (!local->openfd) {
	local->openfd = openfd;
      } else {
	openfd->next = local->openfd->next;
	local->openfd->next = openfd;
      }
    }
    if (op_ret == -1 && op_errno != CHILDDOWN) {
      local->op_errno = op_errno;
      trap ();
      local->failed = 1;
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);
  
  if (!callcnt) {
    if (local->failed == 1 && local->openfd) {
      unify_local_t *bg_local = NULL;
      unify_openfd_t *trav_openfd = local->openfd;
      call_frame_t *bg_frame = copy_frame (frame);

      INIT_LOCAL (bg_frame, bg_local);

      while (trav_openfd) {
	bg_local->call_count++;
	trav_openfd = trav_openfd->next;
      }

      trav_openfd = local->openfd;
      while (trav_openfd) {
	unify_openfd_t *tmpfd = trav_openfd;
	STACK_WIND (bg_frame,
		    unify_bg_cbk,
		    trav_openfd->xl,
		    trav_openfd->xl->fops->close,
		    local->fd);
	trav_openfd = trav_openfd->next;
	free (tmpfd);
      }
      /* return -1 to user */
      local->op_ret = -1;
    }
    if (local->op_ret >= 0) {
      /* Store child node's ptr, used in all the f*** / FileIO calls */
      dict_set (fd->ctx,
		this->name,
		data_from_static_ptr (local->openfd));
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
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = NULL;

  /* Init */
  INIT_LOCAL (frame, local);
  local->inode = loc->inode;
  local->fd = fd;

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  list_for_each_entry (ino_list, list, list_head)
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

  list_for_each_entry (ino_list, list, list_head) {
    _STACK_WIND (frame,
		 unify_open_cbk,
		 ino_list->xl, //cookie
		 ino_list->xl,
		 ino_list->xl->fops->open,
		 loc,
		 flags,
		 fd);
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
  unify_openfd_t *openfd = NULL;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    if (op_ret == 0) {
      local->op_ret = 0;
      openfd = calloc (1, sizeof (*openfd));
      openfd->xl = cookie;
      if (!local->openfd) {
	local->openfd = openfd;
      } else {
	openfd->next = local->openfd->next;
	local->openfd->next = openfd;
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
    if (local->failed == 1) {
      unify_local_t *bg_local = NULL;
      unify_openfd_t *trav_openfd = local->openfd;
      call_frame_t *bg_frame = copy_frame (frame);

      INIT_LOCAL (bg_frame, bg_local);
      while (trav_openfd) {
	bg_local->call_count++;
	trav_openfd = trav_openfd->next;
      }
      trav_openfd = local->openfd;
      while (trav_openfd) {
	unify_openfd_t *tmp_openfd = trav_openfd;
	STACK_WIND (bg_frame,
		    unify_bg_cbk,
		    trav_openfd->xl,
		    trav_openfd->xl->fops->close,
		    fd);
	trav_openfd = trav_openfd->next;
	free (tmp_openfd);
      }
      /* return -1 to user */
      local->op_ret = -1;
    }
    if (local->op_ret >= 0) {
      /* Store child node's ptr, used in all the f*** / FileIO calls */
      dict_set (fd->ctx, 
		this->name, 
		data_from_static_ptr (local->openfd));
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

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    if (op_ret == -1) {
      local->op_errno = op_errno;
      local->failed = 1;
    }
  }
  UNLOCK (&frame->lock);

  if (op_ret >= 0) {
    ino_list = calloc (1, sizeof (unify_inode_list_t));
    ino_list->xl = (xlator_t *)cookie;
    
    LOCK (&frame->lock);
    {
      if (!local->list) {
	local->list = calloc (1, sizeof (struct list_head));
	INIT_LIST_HEAD (local->list);
      }
      /* This is to be used as hint from the inode and also mapping */
      list_add (&ino_list->list_head, local->list);

      local->op_ret = op_ret; 
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
    UNLOCK (&frame->lock);
  }

  if (!callcnt) {
    dict_set (local->inode->ctx, 
	      this->name, 
	      data_from_static_ptr (local->list));

    if (local->entry_count == 2) {
      /* Everything is perfect :) */
      struct list_head *list = NULL;

      local->op_ret = -1;
      list = local->list;
      list_for_each_entry (ino_list, list, list_head)
	local->call_count++;

      list_for_each_entry (ino_list, list, list_head) {
	loc_t tmp_loc = {
	  .inode = inode,
	  .path = local->name,
	};
	_STACK_WIND (frame,
		     unify_create_open_cbk,
		     ino_list->xl, //cookie
		     ino_list->xl,
		     ino_list->xl->fops->open,
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
		    NULL,
		    NULL,
		    NULL);
    }
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
  unify_openfd_t *openfd = NULL;
  unify_local_t *local = frame->local;

  if (op_ret == 0) {
    local->op_ret = 0;
    list = data_to_ptr (dict_get (inode->ctx, this->name));

    openfd = calloc (1, sizeof (*openfd));
    openfd->xl = cookie;
    LOCK (&frame->lock);
    {
      openfd->next = local->openfd->next;
      local->openfd->next = openfd;
    }
    UNLOCK (&frame->lock);
    
    ino_list = calloc (1, sizeof (unify_inode_list_t));
    ino_list->xl = (xlator_t *)cookie;

    /* Add entry to NameSpace's inode */
    list_add (&ino_list->list_head, list);
  }

  LOCK (&frame->lock);
  {
    callcnt == --local->call_count;
    if (op_ret == -1 && op_errno != ENOENT) {
      local->op_errno = op_errno;
    }
  }
  UNLOCK (&frame->lock);

  if (!callcnt) {
    if (local->op_ret == -1) {
      /* send close () on Namespace */
      unify_local_t *bg_local = NULL;
      call_frame_t *bg_frame = copy_frame (frame);

      INIT_LOCAL (bg_frame, bg_local);
      bg_local->call_count = 1;
      STACK_WIND (bg_frame,
		  unify_bg_cbk,
		  NS(this),
		  NS(this)->fops->close,
		  fd);
      bg_local = NULL;
      bg_frame = NULL;
      bg_frame = copy_frame (frame);
      INIT_LOCAL (bg_frame, bg_local);
      bg_local->call_count = 1;
      {
	/* Create failed in storage node, but it was success in 
	 * namespace node, so after closing fd, need to unlink the file
	 */
	list = data_to_ptr (dict_get (inode->ctx, this->name));
	list_for_each_entry (ino_list, list, list_head) {
	  loc_t tmp_loc = {
	    .inode = local->inode,
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
    if (local->op_ret >= 0) {
      dict_set (fd->ctx, 
		this->name, 
		data_from_static_ptr (local->openfd));
    }

    unify_local_wipe (local);
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
  unify_openfd_t *openfd = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;
  
  if (op_ret == -1) {
    /* No need to send create request to other servers, as namespace 
     * action failed. Handle exclusive create here.
     */
    if ((op_errno != EEXIST) || 
	((op_errno == EEXIST) && ((local->flags & O_EXCL) == O_EXCL))) {
      /* If its just a create call without O_EXCL, don't do this */
      if (op_errno == CHILDDOWN)
	op_errno = EIO;
      unify_local_wipe (local);
      STACK_UNWIND (frame,
		    op_ret,
		    op_errno,
		    NULL,
		    NULL,
		    buf);
      return 0;
    }
  }
  
  if (op_ret >= 0) {
    /* Create/update inode for this entry */
  
    /* link fd and inode */
    local->stbuf = *buf;
    openfd = calloc (1, sizeof (*openfd));
    openfd->xl = NS (this);
    local->openfd = openfd;
  
    /* Start the mapping list */
    list = calloc (1, sizeof (struct list_head));
    INIT_LIST_HEAD (list);
    dict_set (inode->ctx, this->name, data_from_static_ptr (list));
    
    ino_list = calloc (1, sizeof (unify_inode_list_t));
    ino_list->xl = NS (this);
    /* Add entry to NameSpace's inode */
    list_add (&ino_list->list_head, list);
    
    /* This means, file doesn't exist anywhere in the Filesystem */
    sched_ops = ((unify_private_t *)this->private)->sched_ops;
    local->op_ret = -1;
    local->call_count = 1;
    /* Send create request to the scheduled node now */
    sched_xl = sched_ops->schedule (this, 0); 
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
    local->call_count = ((unify_private_t *)this->private)->child_count + 1;
    local->op_ret = -1;
    {
      /* Send the lookup to all the nodes including namespace */
      loc_t tmp_loc = {
	.path = local->name,
	.inode = inode,
      };
      _STACK_WIND (frame,
		   unify_create_lookup_cbk,
		   NS(this),
		   NS(this),
		   NS(this)->fops->lookup,
		   &tmp_loc);
    }
    trav = this->children;
    while (trav) {
      loc_t tmp_loc = {
	.path = local->name,
	.inode = inode,
      };
      _STACK_WIND (frame,
		   unify_create_lookup_cbk,
		   trav->xlator,
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
	      loc_t *loc,
	      int32_t flags,
	      mode_t mode,
	      fd_t *fd)
{
  unify_local_t *local = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->name = strdup (loc->path);
  local->mode = mode;
  local->flags = flags;
  local->inode = loc->inode;
  local->fd = fd;

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
  unify_openfd_t *openfd = NULL;
  unify_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    
    if (op_ret >= 0) {
      local->op_ret = op_ret;
      openfd = calloc (1, sizeof (*openfd));
      openfd->xl = cookie;
      if (!local->openfd) {
	local->openfd = openfd;
      } else {
	openfd->next = local->openfd->next;
	local->openfd->next = openfd;
      }
    }
    if (op_ret == -1 && op_errno != CHILDDOWN) {
      local->op_errno = op_errno;
      local->failed = 1;
    }
  }
  UNLOCK (&frame->lock);

  if (!callcnt) {
    if (local->failed == 1) {
      unify_local_t *bg_local = NULL;
      unify_openfd_t *trav_openfd = local->openfd;
      call_frame_t *bg_frame = copy_frame (frame);

      INIT_LOCAL (bg_frame, bg_local);
      while(trav_openfd) {
	bg_local->call_count++;
	trav_openfd = trav_openfd->next;
      }

      trav_openfd = local->openfd;
      while (trav_openfd) {
	unify_openfd_t *tmp_openfd = trav_openfd;
	STACK_WIND (bg_frame,
		    unify_bg_cbk,
		    trav_openfd->xl,
		    trav_openfd->xl->fops->closedir,
		    fd);
	trav_openfd = trav_openfd->next;
	free (tmp_openfd);
      }
      /* return -1 to user */
      local->op_ret = -1;
    }
    if (local->op_ret >= 0) {
      /* Store child node's ptr, used in all the f*** / FileIO calls */
      dict_set (fd->ctx, 
		this->name, 
		data_from_static_ptr (local->openfd));
    }

    STACK_UNWIND (frame, local->op_ret, local->op_errno, fd);
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
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = NULL;

  INIT_LOCAL (frame, local);
  local->inode = loc->inode;

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  list_for_each_entry (ino_list, list, list_head)
    local->call_count++;

  list_for_each_entry (ino_list, list, list_head) {
    _STACK_WIND (frame,
		 unify_opendir_cbk,
		 ino_list->xl,
		 ino_list->xl,
		 ino_list->xl->fops->opendir,
		 loc,
		 fd);
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
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = op_ret;
  local->stbuf = *buf;
  list = data_to_ptr (dict_get (local->inode->ctx, this->name));
    
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
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;
    local->call_count--; /* Reduce 1 for namespace entry */
    
    if (local->call_count) {
      list_for_each_entry (ino_list, list, list_head) {
	if (ino_list->xl != NS(this)) {
	  loc_t tmp_loc = {
	    .inode = local->inode,
	    .path = local->path,
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
      unify_local_wipe (local);
      STACK_DESTROY (bg_frame->root);
    }
  } else {
    /* Its not a directory, so copy will be present only on one storage node */
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;
    local->call_count--; //for namespace
    if (local->call_count) {
      list_for_each_entry (ino_list, list, list_head) {
	if (ino_list->xl != NS(this)) {
	  loc_t tmp_loc = {
	    .path = local->path, 
	    .inode = local->inode
	  };
	  STACK_WIND (frame,
		      unify_buf_cbk,
		      ino_list->xl,
		      ino_list->xl->fops->chmod,
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
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->path = strdup (loc->path);
  local->inode = loc->inode;
  local->mode = mode;

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      STACK_WIND (frame,
		  unify_chmod_cbk,
		  NS(this),
		  NS(this)->fops->chmod,
		  loc,
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
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = op_ret;
  local->stbuf = *buf;
  list = data_to_ptr (dict_get (local->inode->ctx, this->name));

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

    local->call_count = 0;
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;
    local->call_count--; /* Reduce 1 for namespace entry */

    if (local->call_count) {
      /* Send chown request to all the nodes now */
      list_for_each_entry (ino_list, list, list_head) {
	if (ino_list->xl != NS(this)) {
	  loc_t tmp_loc = {
	    .inode = local->inode,
	    .path = local->path,
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
      unify_local_wipe (local);
      STACK_DESTROY (bg_frame->root);
    }
  } else {
    /* Its not a directory, so copy will be present only on one storage node */
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;
    local->call_count--; //for namespace
    if (local->call_count) {
      list_for_each_entry (ino_list, list, list_head) {
	if (ino_list->xl != NS(this)) {
	  loc_t tmp_loc = {
	    .path = local->path, 
	    .inode = local->inode
	  };
	  STACK_WIND (frame,
		      unify_buf_cbk,
		      ino_list->xl,
		      ino_list->xl->fops->chown,
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
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->path = strdup (loc->path);
  local->inode = loc->inode;
  local->uid = uid;
  local->gid = gid;

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      STACK_WIND (frame,
		  unify_chown_cbk,
		  NS(this),
		  NS(this)->fops->chown,
		  loc,
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
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = op_ret;
  local->stbuf = *buf;
  list = data_to_ptr (dict_get (local->inode->ctx, this->name));

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
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;
    local->call_count--; /* Reduce 1 for namespace entry */
    
    if (local->call_count) {
      list_for_each_entry (ino_list, list, list_head) {
	if (ino_list->xl != NS(this)) {
	  loc_t tmp_loc = {
	    .inode = local->inode,
	    .path = local->path,
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
      unify_local_wipe (local);
      STACK_DESTROY (bg_frame->root);
    }
  } else {
    /* Its not a directory, so copy will be present only on one storage node */
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;
    local->call_count--; //for namespace

    if (local->call_count) {
      list_for_each_entry (ino_list, list, list_head) {
	if (ino_list->xl != NS(this)) {
	  loc_t tmp_loc = {
	    .path = local->path, 
	    .inode = local->inode
	  };
	  STACK_WIND (frame,
		      unify_buf_cbk,
		      ino_list->xl,
		      ino_list->xl->fops->truncate,
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
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->path = strdup (loc->path);
  local->inode = loc->inode;
  local->offset = offset;
  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      STACK_WIND (frame,
		  unify_truncate_cbk,
		  NS(this),
		  NS(this)->fops->truncate,
		  loc,
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
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = 0;
  local->stbuf = *buf;
  list = data_to_ptr (dict_get (local->inode->ctx, this->name));
    
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
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;
    local->call_count--; /* Reduce 1 for namespace entry */

    if (local->call_count) {
      list_for_each_entry (ino_list, list, list_head) {
	if (ino_list->xl != NS(this)) {
	  loc_t tmp_loc = {
	    .inode = local->inode,
	    .path = local->path,
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
      unify_local_wipe (local);
      STACK_DESTROY (bg_frame->root);
    }
  } else {
    /* Its not a directory */
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;
    local->call_count--;

    if (local->call_count) {
      list_for_each_entry (ino_list, list, list_head) {
	if (ino_list->xl != NS(this)) {
	  loc_t tmp_loc = {
	    .path = local->path, 
	    .inode = local->inode
	  };
	  STACK_WIND (frame,
		      unify_buf_cbk,
		      ino_list->xl,
		      ino_list->xl->fops->utimens,
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
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->inode = loc->inode;
  local->path = strdup (loc->path);
  memcpy (local->tv, tv, 2 * sizeof (struct timespec));

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      STACK_WIND (frame,
		  unify_utimens_cbk,
		  NS(this),
		  NS(this)->fops->utimens,
		  loc,
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
  int32_t entry_count = 0;

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  list_for_each_entry (ino_list, list, list_head)
    entry_count++;

  if (entry_count == 2) {
    list_for_each_entry (ino_list, list, list_head) {
      if (ino_list->xl != NS(this)) {
	STACK_WIND (frame,
		    unify_readlink_cbk,
		    ino_list->xl,
		    ino_list->xl->fops->readlink,
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
static int32_t
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
  unify_local_t *local = NULL;
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  int32_t counter = 0;

  /* Initialization */
  INIT_LOCAL (frame, local);
  local->path = strdup (loc->path);
  local->inode = loc->inode;

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  list_for_each_entry (ino_list, list, list_head) {
    local->call_count++;
    counter++;
  }

  list_for_each_entry (ino_list, list, list_head) {
    STACK_WIND (frame,
		unify_unlink_cbk,
		ino_list->xl,
		ino_list->xl->fops->unlink,
		loc);
    counter--;
    if (!counter)
      break;
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
  unify_openfd_t *openfd = NULL;

  openfd = data_to_ptr (dict_get (fd->ctx, this->name));
  while (openfd) {
    if (openfd->xl != NS(this)) {
      STACK_WIND (frame,
		  unify_readv_cbk,
		  openfd->xl,
		  openfd->xl->fops->readv,
		  fd,
		  size,
		  offset);
      break;
    }
    openfd = openfd->next;
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
  unify_openfd_t *openfd = NULL;

  openfd = data_to_ptr (dict_get (fd->ctx, this->name));
  while (openfd) {
    if (openfd->xl != NS(this)) {
      STACK_WIND (frame,
		  unify_writev_cbk,
		  openfd->xl,
		  openfd->xl->fops->writev,
		  fd,
		  vector,
		  count,
		  off);
      break;
    }
    openfd = openfd->next;
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
  unify_local_t *local = NULL;
  unify_openfd_t *openfd = NULL;
  unify_openfd_t *trav = NULL;


  /* Initialization */
  INIT_LOCAL (frame, local);

  openfd = data_to_ptr (dict_get (fd->ctx, this->name));
  trav = openfd;
  while (trav) {
    local->call_count++;
    trav = trav->next;
  }
  
  trav = openfd;
  while (trav) {
    _STACK_WIND (frame,
		 unify_buf_cbk,
		 trav->xl,
		 trav->xl,
		 trav->xl->fops->ftruncate,
		 fd,
		 offset);
    trav = trav->next;
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
  unify_openfd_t *openfd = NULL;
  unify_openfd_t *trav = NULL;


  /* Initialization */
  INIT_LOCAL (frame, local);

  openfd = data_to_ptr (dict_get (fd->ctx, this->name));
  trav = openfd;
  while (trav) {
    local->call_count++;
    trav = trav->next;
  }

  while (openfd) {
    _STACK_WIND (frame,
		 unify_buf_cbk,
		 openfd->xl,
		 openfd->xl,
		 openfd->xl->fops->fchmod,
		 fd,
		 mode);
    openfd = openfd->next;
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
  unify_openfd_t *openfd = NULL;
  unify_openfd_t *trav = NULL;

  /* Initialization */
  INIT_LOCAL (frame, local);

  openfd = data_to_ptr (dict_get (fd->ctx, this->name));
  trav = openfd;
  while (trav) {
    local->call_count++;
    trav = trav->next;
  }

  while (openfd) {
    _STACK_WIND (frame,
		 unify_buf_cbk,
		 openfd->xl,
		 openfd->xl,
		 openfd->xl->fops->fchown,
		 fd,
		 uid,
		 gid);
    openfd = openfd->next;
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
  unify_openfd_t *openfd = NULL;

  openfd = data_to_ptr (dict_get (fd->ctx, this->name));
  while (openfd) {
    if (openfd->xl != NS(this)) {
      STACK_WIND (frame,
		  unify_flush_cbk,
		  openfd->xl,
		  openfd->xl->fops->flush,
		  fd);
      break;
    }
    openfd = openfd->next;
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

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (op_ret >= 0) 
    local->op_ret = op_ret;
  
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
  unify_openfd_t *openfd = NULL;
  unify_openfd_t *trav = NULL;

  /* Init */
  INIT_LOCAL (frame, local);
  local->inode = fd->inode;
  local->fd = fd;

  openfd = data_to_ptr (dict_get (fd->ctx, this->name));
  dict_del (fd->ctx, this->name);
  trav = openfd;
  while (trav) {
    local->call_count++;
    trav = trav->next;
  }

  while (openfd) {
    STACK_WIND (frame,
		unify_close_cbk,
		openfd->xl,
		openfd->xl->fops->close,
		fd);
    trav = openfd;
    openfd = openfd->next;
    free (trav);
  }

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
  unify_openfd_t *openfd = NULL;

  openfd = data_to_ptr (dict_get (fd->ctx, this->name));
  while (openfd) {
    if (openfd->xl != NS(this)) {
      STACK_WIND (frame,
		  unify_fsync_cbk,
		  openfd->xl,
		  openfd->xl->fops->fsync,
		  fd,
		  flags);
      break;
    }
    openfd = openfd->next;
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
  unify_local_t *local = NULL;
  unify_openfd_t *openfd = NULL;
  unify_openfd_t *trav = NULL;

  INIT_LOCAL (frame, local);

  openfd = data_to_ptr (dict_get (fd->ctx, this->name));
  trav = openfd;
  while (trav) {
    local->call_count++;
    trav = trav->next;
  }

  while (openfd) {
    _STACK_WIND (frame,
		 unify_buf_cbk,
		 openfd->xl, //cookie
		 openfd->xl,
		 openfd->xl->fops->fstat,
		 fd);
    openfd = openfd->next;
  }

  return 0;
}

/**
 * unify_readdir_cbk - 
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
  unify_openfd_t *openfd = NULL;

  openfd = data_to_ptr (dict_get (fd->ctx, this->name));
  while (openfd) {
    if (openfd->xl == NS(this)) {
      _STACK_WIND (frame,
		   unify_readdir_cbk,
		   openfd->xl, //cookie
		   openfd->xl,
		   openfd->xl->fops->readdir,
		   size,
		   offset,
		   fd);
      break;
    }
    openfd = openfd->next;
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
  unify_local_t *local = NULL;
  unify_openfd_t *openfd = NULL;
  unify_openfd_t *trav = NULL;

  INIT_LOCAL (frame, local);

  openfd = data_to_ptr (dict_get (fd->ctx, this->name));
  dict_del (fd->ctx, this->name);

  trav = openfd;
  while (trav) {
    local->call_count++;
    trav = trav->next;
  }

  while (openfd) {
    STACK_WIND (frame,
		unify_closedir_cbk,
		openfd->xl,
		openfd->xl->fops->closedir,
		fd);
    trav = openfd;
    openfd = openfd->next;
    free (trav);
  }

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
  unify_local_t *local = NULL;
  unify_openfd_t *openfd = NULL;
  unify_openfd_t *trav = NULL;
  
  INIT_LOCAL (frame, local);

  openfd = data_to_ptr (dict_get (fd->ctx, this->name));

  trav = openfd;
  while (trav) {
    local->call_count++;
    trav = trav->next;
  }

  while (openfd) {
    STACK_WIND (frame,
		unify_fsyncdir_cbk,
		openfd->xl,
		openfd->xl->fops->fsyncdir,
		fd,
		flags);
    openfd = openfd->next;
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
  unify_openfd_t *openfd = NULL;

  openfd = data_to_ptr (dict_get (fd->ctx, this->name));
  while (openfd) {
    if (openfd->xl != NS(this)) {
      STACK_WIND (frame,
		  unify_lk_cbk,
		  openfd->xl,
		  openfd->xl->fops->lk,
		  fd,
		  cmd,
		  lock);
      break;
    }
    openfd = openfd->next;
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
  unify_local_t *local = NULL;
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  list_for_each_entry (ino_list, list, list_head) 
    local->call_count++;
  local->call_count--; //don't do it on namespace
 
  if (local->call_count) {
    list_for_each_entry (ino_list, list, list_head) {
      if (ino_list->xl != NS(this)) {
	STACK_WIND (frame,
		    unify_setxattr_cbk,
		    ino_list->xl,
		    ino_list->xl->fops->setxattr,
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
  int32_t count = 0;

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  list_for_each_entry (ino_list, list, list_head)
    count++;
  count--; //done for namespace entry

  if (count) {
    list_for_each_entry (ino_list, list, list_head) {
      if (ino_list->xl != NS(this)) {
	STACK_WIND (frame,
		    unify_getxattr_cbk,
		    ino_list->xl,
		    ino_list->xl->fops->getxattr,
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
static int32_t
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
  unify_local_t *local = NULL;
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  INIT_LOCAL (frame, local);

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  list_for_each_entry (ino_list, list, list_head) 
    local->call_count++;
  local->call_count--; /* on NS its not done */
  if (local->call_count) {
    list_for_each_entry (ino_list, list, list_head) {
      if (ino_list->xl != NS(this)) {
	STACK_WIND (frame,
		    unify_removexattr_cbk,
		    ino_list->xl,
		    ino_list->xl->fops->removexattr,
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

  if (op_ret >= 0) {
    list = data_to_ptr (dict_get (inode->ctx, this->name));

    ino_list = calloc (1, sizeof (unify_inode_list_t));
    ino_list->xl = (xlator_t *)cookie;

    /* Add entry to NameSpace's inode */
    list_add (&ino_list->list_head, list);
  }
  unify_local_wipe (local);
  STACK_UNWIND (frame, op_ret, op_errno, inode, &local->stbuf);

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
  
  /* Start the mapping list */
  list = calloc (1, sizeof (struct list_head));
  INIT_LIST_HEAD (list);
  dict_set (local->inode->ctx, this->name, data_from_static_ptr (list));
  
  ino_list = calloc (1, sizeof (unify_inode_list_t));
  ino_list->xl = NS (this);

  /* Add entry to NameSpace's inode */
  list_add (&ino_list->list_head, list);
  
  sched_ops = ((unify_private_t *)this->private)->sched_ops;

  /* Send mknod request to scheduled node now */
  sched_xl = sched_ops->schedule (this, 0); 
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
  local->name = strdup (loc->path);
  local->mode = mode;
  local->dev = rdev;
  local->inode = loc->inode;

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
  
  if (op_ret >= 0) {
    list = data_to_ptr (dict_get (inode->ctx, this->name));
    
    ino_list = calloc (1, sizeof (unify_inode_list_t));
    ino_list->xl = (xlator_t *)cookie;
    /* Add entry to NameSpace's inode */
    list_add (&ino_list->list_head, list);
    
  }

  unify_local_wipe (local);
  STACK_UNWIND (frame, op_ret, op_errno, inode, &local->stbuf);

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
  
  /* Start the mapping list */
  list = calloc (1, sizeof (struct list_head));
  INIT_LIST_HEAD (list);
  dict_set (local->inode->ctx, 
	    this->name, 
	    data_from_static_ptr (list));

  ino_list = calloc (1, sizeof (unify_inode_list_t));
  ino_list->xl = NS (this);

  /* Add entry to NameSpace's inode */
  list_add (&ino_list->list_head, list);
  
  sched_ops = ((unify_private_t *)this->private)->sched_ops;

  /* Send symlink request to all the nodes now */
  sched_xl = sched_ops->schedule (this, 0); 
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
  local->path = strdup (linkpath);
  local->name = strdup (loc->path);
  local->inode = loc->inode;

  STACK_WIND (frame,
	      unify_ns_symlink_cbk,
	      NS(this),
	      NS(this)->fops->symlink,
	      linkpath,
	      loc);

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
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  buf);
    return 0;
  }
  
  local->op_ret = 0;
  local->stbuf = *buf;
  
  if (local->new_inode && !S_ISDIR(local->new_inode->st_mode)) {
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

    list = data_to_ptr (dict_get (local->new_inode->ctx, this->name));
    list_for_each_entry (ino_list, list, list_head) {
      if (NS(this) != ino_list->xl) {
	loc_t tmp_loc = {
	  .path = local->name,
	  .inode = local->new_inode,
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
  list = data_to_ptr (dict_get (local->inode->ctx, this->name));

  local->call_count = 0;
  list_for_each_entry (ino_list, list, list_head)
    local->call_count++;
  local->call_count--; // minus one entry for namespace deletion which just happend

  list_for_each_entry (ino_list, list, list_head) {
    if (NS(this) != ino_list->xl) {
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
  
  /* Initialization */
  INIT_LOCAL (frame, local);
  local->path = strdup (oldloc->path);
  local->name = strdup (newloc->path);
  local->inode = oldloc->inode;

  list = data_to_ptr (dict_get (oldloc->inode->ctx, this->name));
  /* if 'newloc->inode' is true, that means there is a file existing 
   * in that path. Anyways, send the rename request to the Namespace 
   * first with corresponding 'loc_t' values 
   */
  local->new_inode = newloc->inode;
  
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      STACK_WIND (frame,
		  unify_ns_rename_cbk,
		  NS(this),
		  NS(this)->fops->rename,
		  oldloc,
		  newloc);
      break;
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
  unify_local_t *local = frame->local;

  unify_local_wipe (local);
  STACK_UNWIND (frame, op_ret, op_errno, inode, &local->stbuf);

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
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  NULL,
		  buf);
    return 0;
  }

  

  /* Update inode for this entry */
  local->op_ret = 0;
  local->stbuf = *buf;

  list = data_to_ptr (dict_get (inode->ctx, this->name));

  /* Send link request to the node now */
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl != NS (this)) {
      loc_t tmp_loc = {
	.inode = local->inode,
	.path = local->path,
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

  list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      STACK_WIND (frame,
		  unify_ns_link_cbk,
		  NS(this),
		  NS(this)->fops->link,
		  loc,
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
