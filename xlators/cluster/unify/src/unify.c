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

#define INIT_LOCK(x)    pthread_mutex_init (x, NULL);
#define LOCK(x)         pthread_mutex_lock (x);
#define UNLOCK(x)       pthread_mutex_unlock (x);
#define LOCK_DESTROY(x) pthread_mutex_destroy (x);
#define NAMESPACE(xl)   (((unify_private_t *)xl->private)->namespace)

#define get_location(table,loc) (1)

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
 * unify_lookup_cbk - one of the tricky function of whole GlusterFS. :D
 */
static int32_t 
unify_lookup_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  loc_t *loc,
		  struct stat *buf)
{
#if 0
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == -1 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  if (op_ret == 0 && local->op_ret == -1) {
    /* This is for only one time */
    struct list_head *list = calloc (1, sizeof (struct list_head));

    local->op_ret = 0;
    local->stbuf = *buf; /* memcpy (local->stbuf, buf, sizeof (struct stat)); */

    /* Create one inode for this entry */
    //    local->inode = inode_update (this->itable, local->, local->name, 0);
    inode_lookup (local->inode);

    /* Start the mapping list */
    INIT_LIST_HEAD (list);
    local->inode->private = (void *)list;
  }
  
  if (op_ret == 0) {
    struct list_head *list = local->inode->private;
    unify_inode_list_t *ilist = calloc (1, sizeof (unify_inode_list_t));
    
    ilist->xl = (xlator_t *)cookie;
    ilist->inode = loc;

    list_add (&ilist->list_head, list);

    /* For all the successful returns.. compare the values, and set it to max */
    if (local->stbuf.st_mtime < buf->st_mtime)
      local->stbuf.st_mtime = buf->st_mtime;
    if (local->stbuf.st_ctime < buf->st_ctime)
      local->stbuf.st_ctime = buf->st_ctime;
    if (local->stbuf.st_atime < buf->st_atime)
      local->stbuf.st_atime = buf->st_atime;

    if (local->stbuf.st_size < buf->st_size)
      local->stbuf.st_size = buf->st_size;
  }
  
  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->inode, &local->stbuf);
  }
#endif
  return 0;
}

/**
 * unify_lookup - Need to send request to namespace and then to all other nodes.
 */
int32_t 
unify_lookup (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
#if 0
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = NULL;
  loc_t *loc = NULL;

  local = calloc (1, sizeof (unify_local_t));
  local->op_ret = -1;
  local->op_errno = ENOENT;
  INIT_LOCK (&frame->mutex);
  frame->local = local;

  inode = inode_search (this->itable, parent->ino, name);
  gf_log (this->name, 1, "inode %p", inode);
  /* If an entry is found, send the lookup call to only that node. 
   * If not, send call to all the nodes.
   */
  if (inode) {
    if (S_ISDIR (inode->buf.st_mode)) {
      list = parent->private;
      list_for_each_entry (ino_list, list, list_head) 
	local->call_count++;
    } else {
      /* Its a file */
      local->call_count = 1;
    }
      
    list = parent->private;
    list_for_each_entry (ino_list, list, list_head) {
      _STACK_WIND (frame,
		   unify_lookup_cbk,
		   ino_list->xl,
		   ino_list->xl,
		   ino_list->xl->fops->lookup,
		   ino_list->inode);
    }
  } else {
    inode_t *pinode = NULL;
    xlator_list_t *trav = NULL;
    /*TODO: if parent inode mapping is not there for all the nodes, how to handle it? */

    /* The inode is not there yet. Forward the request to all the nodes
     */
    
    /* Get the parent inode, which will e a directory, so, will be present 
     * on all the nodes 
     */
    pinode = inode_search (this->itable, parent->ino, NULL);
    list = pinode->private;
    list_for_each_entry (ino_list, list, list_head) 
      local->call_count++;
    
    gf_log (this->name, 1, "pinode %p, call_count %d", pinode, local->call_count);
    trav = this->children;
    while (trav) {
      list = pinode->private;
      list_for_each_entry (ino_list, list, list_head) {
	gf_log (this->name, 1, "xl %p, trav %p", ino_list->xl, trav);
	
	if (trav->xlator == ino_list->xl) {
	  gf_log (this->name, 1, "---");
	  _STACK_WIND (frame,
		       unify_lookup_cbk,
		       ino_list->xl,
		       ino_list->xl,
		       ino_list->xl->fops->lookup,
		       ino_list->inode);
	}
      }
      trav = trav->next;
    }
  }
#endif
  return 0;
}

static int32_t 
unify_forget_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
#if 0
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == -1 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  if (op_ret == 0)
    local->op_ret = 0;
  
  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
#endif  
  return 0;
}

int32_t 
unify_forget (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
#if 0
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = NULL;

  /* Initialization */
  local = calloc (1, sizeof (unify_local_t));
  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  frame->local = local;

  /* Initialize call_count - which will be >1 for directories only */
  list = inode->private;
  list_for_each_entry (ino_list, list, list_head)
    local->call_count++;
  
  /* wind the stack to all the mapped entries */
  list= inode->private;
  list_for_each_entry (ino_list, list, list_head) {
    STACK_WIND (frame,
		unify_forget_cbk,
		ino_list->xl,
		ino_list->xl->fops->forget,
		ino_list->inode,
		nlookup);
  }

  /* forget the entry in this table also */
  inode_forget (inode, nlookup);
#endif
  return 0;
}

/**
 * unify_stat_cbk - This function will be called many times in the 
 *   same frame, only if stat call is made on directory. When a stat 
 *   entry for directory returns, unify should see latest mtime,atime 
 *   and ctime of all the directories of children, and send the latest 
 *   to the above layer. Its done for the 'st_size' entry also to give 
 *   consistant size accross stat calls.
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

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == -1 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  if (op_ret == 0 && local->op_ret == -1) {
    /* This is for only one time */
    local->op_ret = 0;
    local->stbuf = *buf; /* memcpy (local->stbuf, buf, sizeof (struct stat)); */
  }
  
  if (op_ret == 0) {
    /* For all the successful returns.. compare the values, and set it to max */
    if (local->stbuf.st_mtime < buf->st_mtime)
      local->stbuf.st_mtime = buf->st_mtime;
    if (local->stbuf.st_ctime < buf->st_ctime)
      local->stbuf.st_ctime = buf->st_ctime;
    if (local->stbuf.st_atime < buf->st_atime)
      local->stbuf.st_atime = buf->st_atime;

    if (local->stbuf.st_size < buf->st_size)
      local->stbuf.st_size = buf->st_size;
  }
  
  if (!callcnt) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }

  return 0;
}

int32_t
unify_stat (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  unify_local_t *local = NULL;
  unify_private_t *priv = NULL;
  xlator_t *loc_xl = NULL;

  /* Initialization */
  local = calloc (1, sizeof (unify_local_t));
  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  frame->local = local;

  priv = (unify_private_t *)this->private;

  loc_xl = get_location (priv->location, loc);

  if (!loc_xl) {
    /* found location */

  } else {
    /* send it to all */
    
  }

  return 0;
}

static int32_t 
unify_chmod_unlock_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
#if 0
  unify_local_t *local = frame->local;
  free (local->path);
  STACK_DESTROY (frame->root);
#endif
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
#if 0
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == -1 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  if (op_ret == 0 && local->op_ret == -1) {
    /* This is for only one time */
    local->op_ret = 0;
    local->stbuf = *buf; /* memcpy (local->stbuf, buf, sizeof (struct stat)); */
  }
  
  if (op_ret == 0) {
    /* For all the successful returns.. compare the values, and set it to max */
    if (local->stbuf.st_mtime < buf->st_mtime)
      local->stbuf.st_mtime = buf->st_mtime;
    if (local->stbuf.st_ctime < buf->st_ctime)
      local->stbuf.st_ctime = buf->st_ctime;
    if (local->stbuf.st_atime < buf->st_atime)
      local->stbuf.st_atime = buf->st_atime;

    if (local->stbuf.st_size < buf->st_size)
      local->stbuf.st_size = buf->st_size;
  }
  
  if (!callcnt) {
    call_frame_t *unlock_frame = NULL;
    if (local->lock_taken) {
      /* If lock is taken, then send mops->unlock after unwinding the current frame */
      unlock_frame = copy_frame (frame);
      unlock_frame->local = local;
    }

    frame->local = NULL;
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);

    if (local->lock_taken) {
      STACK_WIND (unlock_frame,
		  unify_chmod_unlock_cbk,
		  LOCK_NODE (this),
		  LOCK_NODE (this)->mops->unlock,
		  local->path);
    } else {
      free (local);
    }
  }
#endif
  return 0;
}

static int32_t 
unify_chmod_lock_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
#if 0
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = frame->local;

  if (op_ret == 0) {
    /* wind the stack to all the mapped entries */
    list = local->inode->private;
    list_for_each_entry (ino_list, list, list_head) {
      STACK_WIND (frame,
		  unify_chmod_cbk,
		  ino_list->xl,
		  ino_list->xl->fops->chmod,
		  ino_list->inode,
		  local->mode);
    }
  } else {
    free (local->path);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, -1, ENOENT, NULL);
  }
#endif
  return 0;
}

int32_t
unify_chmod (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode)
{
#if 0
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = NULL;

  /* Initialization */
  local = calloc (1, sizeof (unify_local_t));
  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  frame->local = local;

  if (S_ISDIR(inode->buf.st_mode)) {
    /* Initialize call_count - which will be >1 for directories only */
    char lock_path[4096] = {0,};

    list = inode->private;
    list_for_each_entry (ino_list, list, list_head)
      local->call_count++;
  
    local->inode = inode;
    local->mode = mode;
    local->lock_taken = 1;

    /* get the lock on directory name */
    inode_path (inode, NULL, lock_path, 4096);
    local->path = strdup (lock_path);

    STACK_WIND (frame,
		unify_chmod_lock_cbk,
		LOCK_NODE (this),
		LOCK_NODE (this)->mops->lock,
		local->path);
  } else {
    /* if its a file, no lock required */
    local->call_count = 1;
    list= inode->private;
    list_for_each_entry (ino_list, list, list_head) {
      STACK_WIND (frame,
		  unify_chmod_cbk,
		  ino_list->xl,
		  ino_list->xl->fops->chmod,
		  ino_list->inode,
		  mode);
    }
  }
#endif
  return 0;
}

static int32_t
unify_fchmod_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
}

int32_t 
unify_fchmod (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      mode_t mode)
{
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
}

static int32_t
unify_ftruncate_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
  return 0;
}

int32_t
unify_ftruncate (call_frame_t *frame,
		   xlator_t *this,
		   fd_t *fd,
		   off_t offset)
{
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
  return 0;
}


int32_t 
unify_utimens (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       struct timespec tv[2])
{
  return 0;
}

int32_t 
unify_futimens_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
  return 0;
}

int32_t 
unify_futimens (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		struct timespec tv[2])
{
}

static int32_t
unify_access_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  return 0;
}

int32_t
unify_access (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t mask)
{
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
		 loc_t *loc,
		 struct stat *buf)
{
  return 0;
}

int32_t
unify_mknod (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode,
	     dev_t rdev)
{
  return 0;
}

static int32_t 
unify_mkdir_unlock_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
#if 0
  unify_local_t *local = frame->local;
  free (local->path);
  STACK_DESTROY (frame->root);
#endif
  return 0;
}

static int32_t
unify_mkdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 loc_t *loc,
		 struct stat *buf)
{
#if 0
  int32_t callcnt = 0;
  unify_local_t *local = frame->local;

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == -1 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  if (op_ret == 0 && local->op_ret == -1) {
    /* This is for only one time */
    struct list_head *list = calloc (1, sizeof (struct list_head));

    local->op_ret = 0;
    local->stbuf = *buf; /* memcpy (local->stbuf, buf, sizeof (struct stat)); */

    /* Create one inode for this entry */
    local->inode = inode_update (this->itable, local->parent, local->name, 0);
    inode_lookup (local->inode);

    /* Start the mapping list */
    INIT_LIST_HEAD (list);
    local->inode->private = (void *)list;
  }
  
  if (op_ret == 0) {
    struct list_head *list = local->inode->private;
    unify_inode_list_t *ilist = calloc (1, sizeof (unify_inode_list_t));
    
    ilist->xl = (xlator_t *)cookie;
    ilist->inode = inode;

    list_add (&ilist->list_head, list);

    /* For all the successful returns.. compare the values, and set it to max */
    if (local->stbuf.st_mtime < buf->st_mtime)
      local->stbuf.st_mtime = buf->st_mtime;
    if (local->stbuf.st_ctime < buf->st_ctime)
      local->stbuf.st_ctime = buf->st_ctime;
    if (local->stbuf.st_atime < buf->st_atime)
      local->stbuf.st_atime = buf->st_atime;

    if (local->stbuf.st_size < buf->st_size)
      local->stbuf.st_size = buf->st_size;
  }
  
  if (!callcnt) {
    call_frame_t *unlock_frame = NULL;
    
    unlock_frame = copy_frame (frame);
    unlock_frame->local = local;

    frame->local = NULL;
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->inode, &local->stbuf);

    STACK_WIND (unlock_frame,
		unify_chmod_unlock_cbk,
		LOCK_NODE (this),
		LOCK_NODE (this)->mops->unlock,
		local->path);
  }
#endif
}

static int32_t
unify_mkdir_lock_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
#if 0
  unify_local_t *local = frame->local;
  if (op_ret == 0) {
    /* wind the stack to all the mapped entries */
    struct list_head *list = NULL;
    unify_inode_list_t *ino_list = NULL;

    list= local->parent->private;
    list_for_each_entry (ino_list, list, list_head) {
      _STACK_WIND (frame,
		   unify_mkdir_cbk,
		   ino_list->xl,
		   ino_list->xl,
		   ino_list->xl->fops->mkdir,
		   ino_list->inode,
		   local->name,
		   local->mode);
    }
  } else {
    free (local->path);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, -1, local->op_errno, NULL, NULL);
  }
#endif
  return 0;
}

int32_t
unify_mkdir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode)
{
#if 0
  struct list_head *list = NULL;
  unify_inode_list_t *ino_list = NULL;
  unify_local_t *local = NULL;
  char lock_path[4096] = {0,};

  /* Initialization */
  local = calloc (1, sizeof (unify_local_t));
  INIT_LOCK (&frame->mutex);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  frame->local = local;

  /* Initialize call_count - which will be >1 for directories only */
  list = parent->private;
  list_for_each_entry (ino_list, list, list_head)
    local->call_count++;

  local->parent = parent;
  local->name = name;
  local->mode = mode;
  local->lock_taken = 1;

  /* get the lock on directory name */
  inode_path (parent, name, lock_path, 4096);
  local->path = strdup (lock_path);

  list= parent->private;
  list_for_each_entry (ino_list, list, list_head) {
    if (ino_list->xl == LOCK_NODE(this)) {
      STACK_WIND (frame,
		  unify_mkdir_lock_cbk,
		  ino_list->xl,
		  ino_list->xl->mops->lock,
		  local->path);
    }
  }
#endif
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
  return 0;
}

static int32_t
unify_rmdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  return 0;
}

int32_t
unify_rmdir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
  return 0;
}

static int32_t
unify_symlink_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   loc_t *loc,
		   struct stat *buf)
{
  return 0;
}

int32_t
unify_symlink (call_frame_t *frame,
	       xlator_t *this,
	       const char *linkname,
	       loc_t *loc)
{
  return 0;
}


static int32_t
unify_rename_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  loc_t *loc,
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
  return 0;
}


static int32_t
unify_link_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		loc_t *loc,
		struct stat *buf)
{
  return 0;
}

int32_t
unify_link (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    loc_t *newloc)
{
  return 0;
}


static int32_t
unify_create_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  fd_t *fd,
		  loc_t *loc,
		  struct stat *buf)
{
  return 0;
}

int32_t
unify_create (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t flags,
	      mode_t mode)
{
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
  return 0;
}

int32_t
unify_open (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t flags)
{
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
  return 0;
}

int32_t
unify_readv (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     size_t size,
	     off_t offset)
{
  return 0;
}


static int32_t
unify_writev_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
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
  return 0;
}

static int32_t
unify_flush_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  return 0;
}

int32_t
unify_flush (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd)
{
  return 0;
}

static int32_t
unify_close_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  return 0;
}

int32_t
unify_close (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd)
{
  return 0;
}


static int32_t
unify_fsync_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  return 0;
}

int32_t
unify_fsync (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     int32_t flags)
{
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
  return 0;
}

static int32_t
unify_opendir_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   fd_t *fd)
{
  return 0;
}

int32_t
unify_opendir (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
  return 0;
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
unify_statfs_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct statvfs *buf)
{
  return 0;
}

int32_t
unify_statfs (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
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
  return 0;
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
		   loc_t *loc)
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
  return 0;
}

int32_t
unify_lk (call_frame_t *frame,
	  xlator_t *this,
	  fd_t *fd,
	  int32_t cmd,
	  struct flock *lock)
{
  return 0;
}


/* Management operations */

static int32_t
unify_stats_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct xlator_stats *stats)
{
  return 0;
}

int32_t
unify_stats (call_frame_t *frame,
	     xlator_t *this,
	     int32_t flags)
{
  
  return 0;
}


static int32_t
unify_fsck_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  return 0;
}

int32_t
unify_fsck (call_frame_t *frame,
	    xlator_t *this,
	    int32_t flags)
{
  return 0;
}


static int32_t
unify_lock_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  return 0;
}

int32_t
unify_lock (call_frame_t *frame,
	    xlator_t *this,
	    const char *path)
{
  return 0;
}

static int32_t
unify_unlock_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno);
  return 0;
}

int32_t
unify_unlock (call_frame_t *frame,
	      xlator_t *this,
	      const char *path)
{
  STACK_WIND (frame,
	      unify_unlock_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->mops->unlock,
	      path);
  return 0;
}


static int32_t
unify_listlocks_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       char *locks)
{
  STACK_UNWIND (frame,
		op_ret,
		op_errno,
		locks);
  return 0;
}

int32_t
unify_listlocks (call_frame_t *frame,
		   xlator_t *this,
		   const char *pattern)
{
  STACK_WIND (frame,
	      unify_listlocks_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->mops->listlocks,
	      pattern);
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
    
    count = 0;
    trav = this->children;
    /* Update the child array */
    while (trav) {
      _private->array[count++] = trav->xlator;
      trav = trav->next;
    }
  
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

    this->private = (void *)_private;
  }

  /* Get the inode table of the child nodes */
  {
    xlator_list_t *trav = NULL;
    unify_inode_list_t *ilist = NULL;
    struct list_head *list = NULL;

    /* Create a inode table for this level */
    this->itable = inode_table_new (0, this->name);
    
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
 * fini - Free all the allocated memory 
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
  .stats = unify_stats
};
