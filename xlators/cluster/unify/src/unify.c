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
 */
#include "glusterfs.h"
#include "unify.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"

#define LOCK_INIT(x)    pthread_mutex_init (x, NULL);
#define LOCK(x)         pthread_mutex_lock (x);
#define UNLOCK(x)       pthread_mutex_unlock (x);
#define LOCK_DESTROY(x) pthread_mutex_destroy (x);

#define NS(xl)          (((unify_private_t *)xl->private)->namespace)

#define UNIFY_INODE_COUNT 100

/**
 * gcd_path - function used for adding two strings, on which a namespace lock is taken
 * @path1 - 
 * @path2 - 
 */
static char *
gcd_path (const char *path1, const char *path2)
{
  char *s1 = (char *)path1;
  char *s2 = (char *)path2;
  int32_t diff = -1;

  while (*s1 && *s2 && (*s1 == *s2)) {
    if (*s1 == '/')
      diff = s1 - path1;
    s1++;
    s2++;
  }

  return (diff == -1) ? NULL : strndup (path1, diff + 1);
}


/**
 * unify_bg_cbk - this is called by the background functions which 
 * doesn't return any of inode, or buf. eg: rmdir, unlink, close, etc.
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
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == 0) {
    local->op_ret = 0;
  }
  
  if (!callcnt) {
    /* Free the strdup'd variables in the local structure */
    if (local->path)
      free (local->path);
    if (local->name)
      free (local->name);

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
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == 0) {
    local->op_ret = 0;

    LOCK (&frame->mutex);
    if (!S_ISDIR (buf->st_mode)) {
      /* If file, then add size from each file */
      local->stbuf.st_size += buf->st_size;
      local->stbuf.st_blocks += buf->st_blocks;
    }
    UNLOCK (&frame->mutex);
  }

  if (!callcnt) {
    /* Free the strdup'd variables of local before getting destroyed */
    if (local->path)
      free (local->path);
    if (local->name)
      free (local->name);

    LOCK_DESTROY (&frame->mutex);
    STACK_DESTROY (frame->root);
  }
  return 0;
}


/**
 * unify_bg_inode_cbk - This function is called by the fops, 
 * which return inode in their _cbk (). eg: mkdir (), mknod (), link (), symlink ()
 *
 * @cookie - ptr to the child xlator, from which the call is returning.
 *
 */
static int32_t
unify_bg_inode_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf)
{
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;
  unify_inode_list_t *ino_list = NULL;

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == 0) {
    local->op_ret = 0;

    ino_list = calloc (1, sizeof (unify_inode_list_t));
    ino_list->xl = (xlator_t *)cookie;
    ino_list->inode = inode;

    LOCK (&frame->mutex);
    /* This is to be used as hint from the inode and also mapping */
    list_add (&ino_list->list_head, local->list);
    if (!S_ISDIR (buf->st_mode)) {
      /* If file, then add size from each file */
      local->stbuf.st_size += buf->st_size;
      local->stbuf.st_blocks += buf->st_blocks;
    }
    UNLOCK (&frame->mutex);
  }
  
  if (!callcnt) {
    /* Free strdup'd variables */
    if (local->path)
      free (local->path);
    if (local->name)
      free (local->name);

    LOCK_DESTROY (&frame->mutex);
    STACK_DESTROY (frame->root);
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
  unify_local_t *local = frame->local;
  unify_inode_list_t *ino_list = NULL;

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  
  if (op_ret == -1)
    local->op_errno = op_errno;

  if (op_ret == 0) {
    local->op_ret = 0;
    ino_list = calloc (1, sizeof (unify_inode_list_t));
    ino_list->xl = (xlator_t *)cookie;
    ino_list->inode = inode;

    LOCK (&frame->mutex);
    if (!local->list) {
      local->list = calloc (1, sizeof (struct list_head));
      INIT_LIST_HEAD (local->list);
    }
    /* This is to be used as hint from the inode and also mapping */
    list_add (&ino_list->list_head, local->list);

    /* Replace most of the variables from NameSpace */
    if (NS(this) == (xlator_t *)cookie) {
      local->stbuf = *buf;
      local->inode = inode_update (this->itable, NULL, NULL, buf->st_ino);
    } else {
      if (!S_ISDIR (buf->st_mode)) {
	/* If file, then add size from each file */
	local->stbuf.st_size += buf->st_size;
	local->stbuf.st_blocks += buf->st_blocks;
      }
    }
    UNLOCK (&frame->mutex);
  }
  if (!callcnt) {
    if (local->path)
      free (local->path);
    local->inode->private = local->list;
    local->list = NULL;
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->inode, &local->stbuf);
  }

  return 0;
}

/**
 * Lookup will be first done on namespace, if an entry exists there, then 
 * its sent to other storage nodes.
 */
int32_t 
unify_lookup (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  unify_local_t *local = calloc (1, sizeof (unify_local_t));
  unify_private_t *priv = this->private;
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  xlator_t *xl = NULL;
  
  /* Initialization */
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = 0;
  local->path = strdup (loc->path);
  local->inode = loc->inode;
  frame->local = local;

  if (loc->inode) {
    list = loc->inode->private;
    if (S_ISDIR (loc->inode->buf.st_mode)) {
      local->call_count = 1;
      list_for_each_entry (ino_list, list, list_head) {
	if (ino_list->xl == NS(this)) {
	  loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
	  _STACK_WIND (frame,
		       unify_lookup_cbk,
		       NS(this),
		       NS(this),
		       NS(this)->fops->lookup,
		       &tmp_loc);
	}
      }
    } else {
      local->call_count = 2; /* 1 for NameSpace, 1 for where the file is */
      local->inode = loc->inode;
      local->path = strdup (loc->path);

      list_for_each_entry (ino_list, list, list_head) {
	if (ino_list->xl == NS(this)) {
	  loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
	  _STACK_WIND (frame,
		       unify_lookup_cbk,
		       ino_list->xl,
		       ino_list->xl,
		       ino_list->xl->fops->lookup,
		       &tmp_loc);
	}
      }
    }
  } else {
    /* Can't help. send it across */
    int32_t index = 0;
    local->call_count = priv->child_count + 1;
    _STACK_WIND (frame,
		 unify_lookup_cbk,
		 NS(this),
		 NS(this),
		 NS(this)->fops->lookup,
		 loc);
    for (index = 0; index < priv->child_count; index++) {
      _STACK_WIND (frame,
		   unify_lookup_cbk,
		   priv->array[index],
		   priv->array[index],		     
		   priv->array[index]->fops->lookup,
		   loc);
    }
  }

  return 0;
}

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
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == 0) {
    local->op_ret = 0;
  }
  
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
  struct list_head *list = inode->private;

  /* Initialization */
  local = calloc (1, sizeof (unify_local_t));
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  frame->local = local;
  list_for_each_entry (ino_list, list, list_head)
    local->call_count++;
  
  list_for_each_entry (ino_list, list, list_head) {
    STACK_WIND (frame,
                unify_forget_cbk,
                ino_list->xl,
                ino_list->xl->fops->forget,
                ino_list->inode);
  }
  inode_forget (inode, 0);
  
  return 0;
}

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

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  
  if (op_ret == -1)
    local->op_errno = op_errno;

  if (op_ret == 0) {
    local->op_ret = 0;
    LOCK (&frame->mutex);
    /* Replace most of the variables from NameSpace */
    if (NS(this) == (xlator_t *)cookie) {
      local->stbuf = *buf;
    } else {
      if (!S_ISDIR (buf->st_mode)) {
	/* If file, then add size from each file */
	local->stbuf.st_size += buf->st_size;
	local->stbuf.st_blocks += buf->st_blocks;
      }
    }
    UNLOCK (&frame->mutex);
  }
  if (!callcnt) {
    if (local->path)
      free (local->path);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

/**
 * unify_stat - if directory, get the stat directly from NameSpace child.
 *     if file, check for a hint and send it only there (also to NS).
 *     if its a fresh stat, then do it on all the nodes.
 * NOTE: for all the call, sending cookie as xlator pointer, which will be used in cbk.
 */
int32_t
unify_stat (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  unify_inode_list_t *ino_list = NULL;
  unify_private_t *priv = (unify_private_t *)this->private;
  unify_local_t *local = calloc (1, sizeof (unify_local_t));
  struct list_head *list = NULL;
  xlator_t *xl = NULL;
  int32_t index = 0;

  /* Initialization */
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  frame->local = local;

  if (loc->inode) {
    list = loc->inode->private;
    if (S_ISDIR (loc->inode->buf.st_mode)) {
      local->call_count = 1;
      list_for_each_entry (ino_list, list, list_head) {
	if (ino_list->xl == NS(this)) {
	  loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
	  _STACK_WIND (frame,
		       unify_stat_cbk,
		       NS(this),
		       NS(this),
		       NS(this)->fops->stat,
		       &tmp_loc);
	}
      }
    } else {
      local->call_count = 2; /* 1 for NameSpace, 1 for where the file is */
      local->inode = loc->inode;
      local->path = strdup (loc->path);
      
      list_for_each_entry (ino_list, list, list_head) {
	if (ino_list->xl == NS(this)) {
	  loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
	  _STACK_WIND (frame,
		       unify_stat_cbk,
		       ino_list->xl,
		       ino_list->xl,
		       ino_list->xl->fops->stat,
		       &tmp_loc);
	}
      }
    }
  } else {
    /* If everything goes fine, this case should not happen */
    local->call_count = priv->child_count + 1;
    _STACK_WIND (frame,
		 unify_stat_cbk,
		 NS(this),
		 NS(this),
		 NS(this)->fops->stat,
		 loc);
    for (index = 0; index < priv->child_count; index++) {
      _STACK_WIND (frame,
		   unify_stat_cbk,
		   priv->array[index],
		   priv->array[index],		     
		   priv->array[index]->fops->stat,
		   loc);
    }
  }
  return 0;
}

static int32_t
unify_access_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

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
      loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
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
  struct list_head *list = NULL;
  xlator_list_t *trav = NULL;
  call_frame_t *bg_frame = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;
  
  if (op_ret == -1) {
    /* No need to send mkdir request to other servers, 
     * as namespace action failed 
     */
    free (local->name);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  NULL,
		  buf);
    return 0;
  }
  
  /* Get a copy of the current frame, and set the current local to bg_frame's local */
  bg_frame = copy_frame (frame);
  frame->local = NULL;
  bg_frame->local = local;

  /* Create one inode for this entry */
  local->op_ret = 0;
  local->stbuf = *buf;
  local->inode = inode_update (this->itable, NULL, NULL, buf->st_ino);
  inode_lookup (local->inode); //TODO:
  
  /* Unwind this frame, and continue with bg_frame */
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		local->inode,
		buf);
  
  list = calloc (1, sizeof (struct list_head));
  
  /* Start the mapping list */
  INIT_LIST_HEAD (list);
  local->inode->private = (void *)list;

  ino_list = calloc (1, sizeof (unify_inode_list_t));
  ino_list->xl = NS (this);
  ino_list->inode = inode;
  /* Add entry to NameSpace's inode */
  list_add (&ino_list->list_head, list);
  
  /* Send mkdir request to all the nodes now */
  trav = this->children;
  while (trav) {
    _STACK_WIND (bg_frame,
		 unify_bg_inode_cbk,
		 trav->xlator,
		 trav->xlator,
		 trav->xlator->fops->mkdir,
		 local->name,
		 local->mode);
    trav = trav->next;
  }
  
  return 0;
}

int32_t
unify_mkdir (call_frame_t *frame,
	     xlator_t *this,
	     const char *name,
	     mode_t mode)
{
  unify_local_t *local = calloc (1, sizeof (unify_local_t));
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;

  /* Initialization */
  frame->local = local;
  local->name = strdup (name);
  local->mode = mode;

  STACK_WIND (frame,
	      unify_mkdir_cbk,
	      NS(this),
	      NS(this)->fops->mkdir,
	      name,
	      mode);
  return 0;
}


static int32_t
unify_rmdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  struct list_head *list = NULL;
  call_frame_t *bg_frame = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;
  
  if (op_ret == -1) {
    /* No need to send rmdir request to other servers, 
     * as namespace action failed 
     */
    free (local->path);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno);
    return 0;
  }
  
  /* Get a copy of the current frame, and set the current local to bg_frame's local */
  bg_frame = copy_frame (frame);
  frame->local = NULL;
  bg_frame->local = local;
  
  /* Unwind this frame, and continue with bg_frame */
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  
  list = local->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {local->path, ino_list->inode->ino, ino_list->inode};
    STACK_WIND (bg_frame,
		unify_bg_cbk,
		ino_list->xl,
		ino_list->xl->fops->rmdir,
		&tmp_loc);
  }

  return 0;
}

int32_t
unify_rmdir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
  unify_local_t *local = calloc (1, sizeof (unify_local_t));
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  frame->local = local;
  local->path = strdup (loc->path);
  local->inode = loc->inode;

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
      STACK_WIND (frame,
		  unify_rmdir_cbk,
		  NS(this),
		  NS(this)->fops->rmdir,
		  &tmp_loc);
    }
  }

  return 0;
}


static int32_t
unify_bg_create_cbk (call_frame_t *frame,
  void *cookie,
  xlator_t *this,
  int32_t op_ret,
  int32_t op_errno,
  fd_t *fd,
  inode_t *inode,
  struct stat *buf)
{
}

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
  struct list_head *list = NULL;
  struct sched_ops *sched_ops = NULL;
  xlator_t *sched_xl = NULL;
  call_frame_t *bg_frame = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;
  
  if (op_ret == -1) {
    /* No need to send create request to other servers, 
     * as namespace action failed 
     */
    free (local->name);
    STACK_UNWIND (frame,
		  op_ret,
		  op_errno,
		  NULL,
		  NULL,
		  buf);
    return 0;
  }
  
  /* Get a copy of the current frame, and set the current local to bg_frame's local */
  bg_frame = copy_frame (frame);
  frame->local = NULL;
  bg_frame->local = local;

  /* Create one inode for this entry */
  local->inode = inode_update (this->itable, NULL, NULL, buf->st_ino);
  inode_lookup (local->inode); 
  
  /* link fd and inode */
  local->fd = calloc (1, sizeof (fd_t));
  local->fd->inode = local->inode;
  local->fd->ctx = get_new_dict ();
  dict_set (local->fd->ctx, (char *)NS(this)->name, int_to_data ((long)fd));
  list_add (&local->fd->inode_list, &local->inode->fds);
  
  /* Unwind this frame, and continue with bg_frame */
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		local->inode,
		local->fd,
		buf);
  
  list = calloc (1, sizeof (struct list_head));
  local->op_ret = 0;
  
  /* Start the mapping list */
  INIT_LIST_HEAD (list);
  local->inode->private = (void *)list;

  ino_list = calloc (1, sizeof (unify_inode_list_t));
  ino_list->xl = NS (this);
  ino_list->inode = inode;
  /* Add entry to NameSpace's inode */
  list_add (&ino_list->list_head, list);
  
  sched_ops = ((unify_private_t *)this->private)->sched_ops;

  /* Send create request to the scheduled node now */
  sched_xl = sched_ops->schedule (this, 0);
  _STACK_WIND (bg_frame,
	       unify_bg_create_cbk,
	       sched_xl,
	       sched_xl,
	       sched_xl->fops->create,
	       local->name,
	       local->flags,
	       local->mode);
    
  return 0;
}

int32_t
unify_create (call_frame_t *frame,
	      xlator_t *this,
	      const char *name,
	      int32_t flags,
	      mode_t mode)
{
  unify_local_t *local = calloc (1, sizeof (unify_local_t));
  
  /* Initialization */
  frame->local = local;
  local->name = strdup (name);
  local->mode = mode;
  local->flags = flags;

  STACK_WIND (frame,
	      unify_create_cbk,
	      NS(this),
	      NS(this)->fops->create,
	      name,
	      flags,
	      mode);
  
  return 0;
}


static int32_t
unify_open_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		fd_t *fd)
{
  
  fd_t *new_fd = NULL;

  /* Decide on cookie here */
  if (op_ret == 0) {
    new_fd = calloc (1, sizeof (fd_t));
    new_fd->ctx = get_new_dict ();
    new_fd->inode = (inode_t*)cookie;
    list_add (&new_fd->inode_list, &new_fd->inode->fds);
    dict_set (new_fd->ctx, (char *)cookie, int_to_data ((long)fd));
  }
  STACK_UNWIND (frame, op_ret, op_errno, new_fd);
  return 0;
}

/* TODO */
int32_t
unify_open (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t flags)
{
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  inode_t *inode = loc->inode;
  //  if (!inode)
  //    return ENOENT;
  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {
      .inode = ino_list->inode,
      .path = loc->path,
      .ino = ino_list->inode->ino
    };
    if (NS(this) == ino_list->xl)
      continue;
    _STACK_WIND (frame,
		 unify_open_cbk,
		 inode, //cookie
		 ino_list->xl,
		 ino_list->xl->fops->open,
		 &tmp_loc,
		 flags);
  }
}


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
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  
  /* Decide on cookie here */
  if (op_ret == 0) {
    if (!local->fd) {
      local->fd = calloc (1, sizeof (fd_t));
      local->fd->ctx = get_new_dict ();
      local->fd->inode = local->inode;
    }
    list_add (&local->fd->inode_list, &local->inode->fds);
    dict_set (local->fd->ctx, (char *)cookie, int_to_data ((long)fd));
  }

  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, op_ret, op_errno, local->fd);
  }
  return 0;
}

int32_t
unify_opendir (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
  inode_t *inode = loc->inode;
  struct list_head *list = inode->private;
  unify_local_t *local = calloc (1, sizeof (unify_local_t *));
  unify_inode_list_t *ino_list = NULL;

  frame->local = local;
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->inode = loc->inode;
  local->call_count = ((unify_private_t *)this->private)->child_count;
  
  list_for_each_entry (ino_list, list, list_head) {
    loc_t tmp_loc = {
      .inode = ino_list->inode,
      .path = loc->path,
      .ino = ino_list->inode->ino
    };
    if (NS(this) == ino_list->xl)
      continue;
    _STACK_WIND (frame,
		 unify_opendir_cbk,
		 ino_list->xl,
		 ino_list->xl,
		 ino_list->xl->fops->opendir,
		 &tmp_loc);
  }
  return 0;
}


static int32_t
unify_statfs_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct statvfs *stbuf)
{
  int32_t callcnt = 0;
  unify_local_t *local = (unify_local_t *)frame->local;
  
  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  if (op_ret == 0) {
    LOCK (&frame->mutex);
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

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->statvfs_buf);
  }

  return 0;
}

int32_t
unify_statfs (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  int32_t index = 0;
  unify_local_t *local = calloc (1, sizeof (unify_local_t *));
  unify_private_t *priv = this->private;

  frame->local = local;
  LOCK_INIT (&frame->mutex);
  local->op_ret = -1;
  local->call_count = ((unify_private_t *)this->private)->child_count;

  for (index = 0; index < priv->child_count; index++) {
    _STACK_WIND (frame,
		 unify_statfs_cbk,
		 priv->array[index],
		 priv->array[index],		     
		 priv->array[index]->fops->statfs,
		 loc);
  }

  return 0;
}

static int32_t
unify_chmod_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  return 0;
}

int32_t
unify_chmod (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode)
{

  unify_local_t *local = calloc (1, sizeof (unify_local_t));
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  frame->local = local;
  local->path = strdup (loc->path);
  local->inode = loc->inode;

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
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


static int32_t
unify_chown_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  return 0;
}

int32_t
unify_chown (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     uid_t uid,
	     gid_t gid)
{
  unify_local_t *local = calloc (1, sizeof (unify_local_t));
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  frame->local = local;
  local->path = strdup (loc->path);
  local->inode = loc->inode;

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
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


static int32_t
unify_truncate_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
  return 0;
}

int32_t
unify_truncate (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		off_t offset)
{
  /* TODO: Do I need to truncate entry @ namespace ? */
  unify_local_t *local = calloc (1, sizeof (unify_local_t));
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  frame->local = local;
  local->path = strdup (loc->path);
  local->inode = loc->inode;

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
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


int32_t 
unify_utimens_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *buf)
{
}


int32_t 
unify_utimens (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       struct timespec tv[2])
{
  unify_local_t *local = calloc (1, sizeof (unify_local_t));
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  frame->local = local;
  local->path = strdup (loc->path);
  local->inode = loc->inode;

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
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

static int32_t
unify_readlink_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    const char *path)
{

  return 0;
}

int32_t
unify_readlink (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		size_t size)
{

  return 0;
}


static int32_t
unify_mknod_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *buf)
{
  return 0;
}

int32_t
unify_mknod (call_frame_t *frame,
	     xlator_t *this,
	     const char *name,
	     mode_t mode,
	     dev_t rdev)
{
  unify_local_t *local = calloc (1, sizeof (unify_local_t));
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  frame->local = local;
  local->name = strdup (name);
  local->mode = mode;
  local->dev = rdev;
  
  STACK_WIND (frame,
	      unify_mknod_cbk,
	      NS(this),
	      NS(this)->fops->mknod,
	      name,
	      mode,
	      rdev);

  return 0;
}

static int32_t
unify_unlink_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  return 0;
}

int32_t
unify_unlink (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  
  unify_local_t *local = calloc (1, sizeof (unify_local_t));
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  frame->local = local;
  local->path = strdup (loc->path);
  local->inode = loc->inode;

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
      STACK_WIND (frame,
		  unify_unlink_cbk,
		  ino_list->xl,
		  ino_list->xl->fops->unlink,
		  &tmp_loc);
    }
  }

  return 0;
}

static int32_t
unify_symlink_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     inode_t *inode,
		     struct stat *buf)
{
}

int32_t
unify_symlink (call_frame_t *frame,
	       xlator_t *this,
	       const char *linkpath,
	       const char *name)
{
  unify_local_t *local = calloc (1, sizeof (unify_local_t));
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  frame->local = local;
  local->path = strdup (linkpath);
  local->name = strdup (name);
  
  STACK_WIND (frame,
	      unify_symlink_cbk,
	      NS(this),
	      NS(this)->fops->symlink,
	      linkpath,
	      name);

  return 0;
}


static int32_t
unify_rename_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{

  return 0;
}

int32_t
unify_rename (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *oldloc,
	      loc_t *newloc)
{
}


static int32_t
unify_link_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		inode_t *inode,
		struct stat *buf)
{
}

int32_t
unify_link (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    const char *newname)
{
  unify_local_t *local = calloc (1, sizeof (unify_local_t));
  unify_inode_list_t *ino_list = NULL;
  struct list_head *list = NULL;
  
  /* Initialization */
  frame->local = local;
  local->path = strdup (loc->path);
  local->inode = loc->inode;
  local->name = strdup (newname);

  list = loc->inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == NS(this)) {
      loc_t tmp_loc = {loc->path, ino_list->inode->ino, ino_list->inode};
      STACK_WIND (frame,
		  unify_link_cbk,
		  ino_list->xl,
		  ino_list->xl->fops->link,
		  &tmp_loc,
		  newname);
    }
  }

  return 0;
}


static int32_t
unify_readv_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct iovec *vector,
		 int32_t count)
{
  STACK_UNWIND (frame, op_ret, op_errno, vector, count);
  return 0;
}

int32_t
unify_readv (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     size_t size,
	     off_t offset)
{
  int32_t index = 0;
  data_t *child_fd_data = NULL;
  unify_private_t *priv = this->private;

  for (index = 0; index < priv->child_count; index++) {
    child_fd_data = dict_get (fd->ctx, priv->array[index]->name);
    if (child_fd_data) {
      STACK_WIND (frame,
		  unify_readv_cbk,
		  priv->array[index],
		  priv->array[index]->fops->readv,
		  data_to_ptr (child_fd_data),
		  size,
		  offset);
      break;
    }
  }
  return 0;
}


static int32_t
unify_writev_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t
unify_writev (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      struct iovec *vector,
	      int32_t count,
	      off_t off)
{
  int32_t index = 0;
  data_t *child_fd_data = NULL;
  unify_private_t *priv = this->private;

  for (index = 0; index < priv->child_count; index++) {
    child_fd_data = dict_get (fd->ctx, priv->array[index]->name);
    if (child_fd_data) {
      STACK_WIND (frame,
		  unify_writev_cbk,
		  priv->array[index],
		  priv->array[index]->fops->writev,
		  data_to_ptr (child_fd_data),
		  vector,
		  count,
		  off);
      break;
    }
  }
}


static int32_t
unify_fchmod_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
  return 0;
}

int32_t 
unify_fchmod (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      mode_t mode)
{
  return 0;
}


static int32_t
unify_fchown_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
  return 0;
}

int32_t 
unify_fchown (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      uid_t uid,
	      gid_t gid)
{
  return 0;
}


static int32_t
unify_ftruncate_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, NULL); //TODO:
  return 0;
}

int32_t
unify_ftruncate (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 off_t offset)
{
  int32_t index = 0;
  data_t *child_fd_data = NULL;
  unify_private_t *priv = this->private;

  for (index = 0; index < priv->child_count; index++) {
    child_fd_data = dict_get (fd->ctx, priv->array[index]->name);
    if (child_fd_data) {
      STACK_WIND (frame,
		  unify_ftruncate_cbk,
		  priv->array[index],
		  priv->array[index]->fops->ftruncate,
		  data_to_ptr (child_fd_data),
		  offset);
      break;
    }
  }
}

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

int32_t
unify_flush (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd)
{
  int32_t index = 0;
  data_t *child_fd_data = NULL;
  unify_private_t *priv = this->private;

  for (index = 0; index < priv->child_count; index++) {
    child_fd_data = dict_get (fd->ctx, priv->array[index]->name);
    if (child_fd_data) {
      STACK_WIND (frame,
		  unify_flush_cbk,
		  priv->array[index],
		  priv->array[index]->fops->flush,
		  data_to_ptr (child_fd_data));
      break;
    }
  }
}

static int32_t
unify_close_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t
unify_close (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd)
{
  int32_t index = 0;
  data_t *child_fd_data = NULL;
  unify_private_t *priv = this->private;

  for (index = 0; index < priv->child_count; index++) {
    child_fd_data = dict_get (fd->ctx, priv->array[index]->name);
    if (child_fd_data) {
      STACK_WIND (frame,
		  unify_close_cbk,
		  priv->array[index],
		  priv->array[index]->fops->close,
		  data_to_ptr (child_fd_data));
      break;
    }
  }
  
  return 0;
}


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

int32_t
unify_fsync (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     int32_t flags)
{
  int32_t index = 0;
  data_t *child_fd_data = NULL;
  unify_private_t *priv = this->private;

  for (index = 0; index < priv->child_count; index++) {
    child_fd_data = dict_get (fd->ctx, priv->array[index]->name);
    if (child_fd_data) {
      STACK_WIND (frame,
		  unify_fsync_cbk,
		  priv->array[index],
		  priv->array[index]->fops->fsync,
		  data_to_ptr (child_fd_data),
		  flags);
      break;
    }
  }

  return 0;
}

static int32_t
unify_fstat_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  return 0;
}

int32_t
unify_fstat (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd)
{
}


static int32_t
unify_readdir_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   dir_entry_t *entries,
		   int32_t count)
{
  return 0;
}

int32_t
unify_readdir (call_frame_t *frame,
	       xlator_t *this,
	       size_t size,
	       off_t offset,
	       fd_t *fd)
{
  return 0;
}


static int32_t
unify_closedir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  return 0;
}

int32_t
unify_closedir (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd)
{
  return 0;
}

static int32_t
unify_fsyncdir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  return 0;
}

int32_t
unify_fsyncdir (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		int32_t flags)
{
  return 0;
}


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

int32_t
unify_lk (call_frame_t *frame,
	  xlator_t *this,
	  fd_t *fd,
	  int32_t cmd,
	  struct flock *lock)
{
  int32_t index = 0;
  data_t *child_fd_data = NULL;
  unify_private_t *priv = this->private;

  for (index = 0; index < priv->child_count; index++) {
    child_fd_data = dict_get (fd->ctx, priv->array[index]->name);
    if (child_fd_data) {
      STACK_WIND (frame,
		  unify_lk_cbk,
		  priv->array[index],
		  priv->array[index]->fops->lk,
		  data_to_ptr (child_fd_data),
		  cmd,
		  lock);
      break;
    }
  }

  return 0;
}


static int32_t
unify_setxattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  return 0;
}

int32_t
unify_setxattr (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		const char *name,
		const char *value,
		size_t size,
		int32_t flags)
{

  return 0;
}

static int32_t
unify_getxattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    void *value)
{

  return 0;
}

int32_t
unify_getxattr (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		const char *name,
		size_t size)
{
  return 0;
}

static int32_t
unify_listxattr_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     void *value)
{
}

int32_t
unify_listxattr (call_frame_t *frame,
		   xlator_t *this,
		   loc_t *loc,
		   size_t size)
{
  return 0;
}

static int32_t
unify_removexattr_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
  return 0;
}

int32_t
unify_removexattr (call_frame_t *frame,
		     xlator_t *this,
		     loc_t *loc,
		     const char *name)
{
}



/* Management operations */

/* ===** missing **=== */

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
  data_t *scheduler = NULL;
  data_t *namespace = NULL;
 
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

  _private = calloc (1, sizeof (*_private));
  _private->sched_ops = get_scheduler (scheduler->data);

  /* update _private structure */
  {
    trav = this->children;
    /* Get the number of child count */
    while (trav) {
      count++;
      trav = trav->next;
    }
    /* There will be one namespace child, which is not regular storage child */
    count--; 
    if (!count) {
      gf_log (this->name,
	      GF_LOG_ERROR,
	      "No storage child nodes. "
	      "check \"subvolumes \" and \"option namespace\" in spec file");
      free (_private);
      return -1;
    }
    _private->child_count = count;   
    gf_log (this->name, 
	    GF_LOG_DEBUG, 
	    "Child node count is %d", count);
    _private->array = (xlator_t **)calloc (1, sizeof (xlator_t *) * count);
    
  
    namespace = dict_get (this->options, "namespace");
    if(namespace) {
      gf_log (this->name, 
	      GF_LOG_DEBUG, 
	      "namespace client specified as %s", namespace->data);
      
      trav = this->children;
      while (trav) {
	if(strcmp (trav->xlator->name, namespace->data) == 0)
	break;
	trav = trav->next;
      }
      if (trav == NULL) {
	gf_log (this->name, 
		GF_LOG_ERROR, 
		"namespace entry not found among the child nodes");
	free (_private);
	return -1;
      }
      _private->namespace = trav->xlator;
    } else {
      gf_log (this->name, 
	      GF_LOG_DEBUG, 
	      "namespace option not specified, defaulting to %s", 
	      this->children->xlator->name);
      _private->namespace = this->children->xlator;
    }

    count = 0;
    trav = this->children;
    /* Update the child array without the namespace xlator */
    while (trav) {
      if (trav->xlator != _private->namespace)
	_private->array[count++] = trav->xlator;
      trav = trav->next;
    }

    this->private = (void *)_private;
  }

  /* Get the inode table of the child nodes */
  {
    xlator_list_t *trav = NULL;
    unify_inode_list_t *ilist = NULL;
    struct list_head *list = calloc (1, sizeof (struct list_head));

    /* Create a inode table for this level */
    this->itable = inode_table_new (UNIFY_INODE_COUNT, this->name);
    
    /* Create a mapping list */
    list = calloc (1, sizeof (struct list_head));
    INIT_LIST_HEAD (list);

    trav = this->children;
    while (trav) {
      ilist = calloc (1, sizeof (unify_inode_list_t));
      ilist->xl = trav->xlator;
      ilist->inode = trav->xlator->itable->root;
      list_add (&ilist->list_head, list);
      trav = trav->next;
    }
    this->itable->root->private = (void *)list;
  }

  /* Initialize the scheduler, if everything else is successful */
  _private->sched_ops->init (this); 
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
  .listxattr   = unify_listxattr,
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
