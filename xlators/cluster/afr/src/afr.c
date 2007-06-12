/*
 * (C) 2007 Z RESEARCH Inc. <http://www.zresearch.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

/*
 * TODO:
 * 1) writev assumes that the calls on the children will write entire
 *    buffer. We need to see how we can handle the case where
 *    one of the children writes less than the buffer.
 * 2) Check the FIXMEs
 *
 */

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>

#include "glusterfs.h"
#include "afr.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"
#include "list.h"
#include "call-stub.h"

#define LOCK_INIT(x)    pthread_mutex_init (x, NULL);
#define LOCK(x)         pthread_mutex_lock (x);
#define UNLOCK(x)       pthread_mutex_unlock (x);
#define LOCK_DESTROY(x) pthread_mutex_destroy (x);

#define AFR_DEBUG_FMT(xl, format, args...) if(((afr_private_t*)(xl)->private)->debug) gf_log ((xl)->name, GF_LOG_DEBUG, "AFRDEBUG:" format, ##args);
#define AFR_DEBUG(xl) if(((afr_private_t*)xl->private)->debug) gf_log (xl->name, GF_LOG_DEBUG, "AFRDEBUG:");

static int32_t
afr_get_num_copies (const char *path, xlator_t *xl)
{
  pattern_info_t *tmp = ((afr_private_t *)xl->private)->pattern_info_list;
  int32_t pil_num = ((afr_private_t *)xl->private)->pil_num;
  int32_t count = 0;

  for (count = 0; count < pil_num; count++) {
    gf_log ("afr", GF_LOG_DEBUG, "file : %s pattern : %s copies : %d", path, tmp->pattern, tmp->copies);
    if (fnmatch (tmp->pattern, path, 0) == 0) {
      gf_log ("afr", GF_LOG_DEBUG, "copies : %d", tmp->copies);
      return tmp->copies;
    }
    tmp++;
  }
  return 1;
}

static int32_t
afr_lookup_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		inode_t *inode,
		struct stat *buf)
{
  afr_local_t *local = frame->local;
  struct list_head *list;
  gf_inode_child_t *gic;
  call_frame_t *prev_frame = cookie;
  inode_t *linode = NULL;
  int32_t callcnt;
  AFR_DEBUG_FMT(this, "op_ret = %d op_errno = %d inode = %p, returned from %s", op_ret, op_errno, inode, prev_frame->this->name);

  if (op_ret != 0 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  list = local->list;
  list_for_each_entry (gic, list, clist) {
    if (gic->xl == prev_frame->this) {
      break;
    }
  }
  gic->repair = 0;
  if (op_ret == 0) {
    local->op_ret = 0;
    gic->inode = inode_ref (inode);
    gic->stat = *buf;
    gic->op_errno = 0;
  } else {
    gic->op_errno = op_errno;
    gic->inode = NULL;
  }

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  /* FIXME: when all children fail, free the private list */
  if (callcnt == 0){
    if (local->op_ret == 0) {
      list_for_each_entry (gic, list, clist) {
	if (gic->inode)
	  break;
      }
      /* we will preserve the inode number even if the first child goes down */
      ino_t ino;
      if (local->inode) {
	ino = local->inode->ino;
      } else {
	ino = gic->inode->ino;
      }
      linode = inode_update (this->itable, NULL, NULL, ino);
      if (local->inode && (linode != local->inode))
	inode_forget (local->inode, 0);
      linode->private = list;
    }
    struct stat *statptr = local->op_ret == 0 ? &gic->stat : NULL;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  linode,
		  statptr);
  }
  return 0;
}

static int32_t
afr_lookup (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  AFR_DEBUG(this);
  afr_local_t *local = calloc (1, sizeof (*local));
  xlator_list_t *trav = this->children;
  gf_inode_child_t *gic;
  struct list_head *list;
  loc_t temploc;
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  temploc.inode = NULL;
  temploc.path = loc->path;

  if(loc->inode == NULL) {
    list = calloc (1, sizeof(*list));
    INIT_LIST_HEAD (list);
    local->list = list;
    while (trav) {
      ++local->call_count;
      gic = calloc (1, sizeof (*gic));
      gic->xl = trav->xlator;
      list_add_tail (&gic->clist, list);
      trav = trav->next;
    }
    trav = this->children;
    while (trav) {
      STACK_WIND (frame,
		  afr_lookup_cbk,
		  trav->xlator,
		  trav->xlator->fops->lookup,
		  &temploc);
      trav = trav->next;
    }
  } else {
    local->inode = loc->inode;
    list = loc->inode->private;
    local->list = list;
    list_for_each_entry (gic, list, clist) {
      ++local->call_count;
    }
    list_for_each_entry (gic, list, clist) {
      if (gic->inode) {
	temploc.inode = gic->inode;
      } else {
	temploc.inode = NULL;
      }
      STACK_WIND (frame,
		  afr_lookup_cbk,
		  gic->xl,
		  gic->xl->fops->lookup,
		  &temploc);
    }
  }
  return 0;
}

static int32_t
afr_forget_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  if (op_ret == 0) {
    local->op_ret = 0;
  }

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);    
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_forget (call_frame_t *frame,
	    xlator_t *this,
	    inode_t *inode)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic, *gictemp;
  struct list_head *list = inode->private;
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->inode = inode;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (gic->inode){
      inode_unref (gic->inode);
      STACK_WIND(frame,
		 afr_forget_cbk,
		 gic->xl,
		 gic->xl->fops->forget,
		 gic->inode);
    }
  }
  list_for_each_entry_safe (gic, gictemp, list, clist) {
    list_del (& gic->clist);
    free (gic);
  }
  free (list);
  inode_forget (local->inode, 0);

  return 0;
}

static int32_t
afr_setxattr_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  AFR_DEBUG(this);
  afr_local_t *local = (afr_local_t *) frame->local;
  int32_t callcnt;

  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_setxattr (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      const char *name,
	      const char *value,
	      size_t size,
	      int32_t flags)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = loc->inode->private;
  loc_t temploc;
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  temploc.path = loc->path;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (gic->inode){
      temploc.inode = gic->inode;
      STACK_WIND(frame,
		 afr_setxattr_cbk,
		 gic->xl,
		 gic->xl->fops->setxattr,
		 &temploc,
		 name,
		 value,
		 size,
		 flags);
    }
  }
  return 0;
}

static int32_t
afr_getxattr_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  void *value)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, op_ret, op_errno, value);
  return 0;
}

static int32_t
afr_getxattr (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      const char *name,
	      size_t size)
{
  AFR_DEBUG(this);
  struct list_head *list = loc->inode->private;
  gf_inode_child_t *gic;
  loc_t temploc;
  temploc.path = loc->path;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      break;
  }
  temploc.inode = gic->inode;
  STACK_WIND (frame,
	      afr_getxattr_cbk,
	      gic->xl,
	      gic->xl->fops->getxattr,
	      &temploc,
	      name,
	      size);
  return 0;
}

static int32_t
afr_listxattr_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   void *value)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, op_ret, op_errno, value);
  return 0;
}

static int32_t
afr_listxattr (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       size_t size)
{
  AFR_DEBUG(this);
  struct list_head *list = loc->inode->private;
  gf_inode_child_t *gic;
  loc_t temploc;
  temploc.path = loc->path;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      break;
  }
  temploc.inode = gic->inode;
  STACK_WIND (frame,
	      afr_listxattr_cbk,
	      gic->xl,
	      gic->xl->fops->listxattr,
	      &temploc,
	      size);
  return 0;
}

static int32_t
afr_removexattr_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;

  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  LOCK (&frame->mutex);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_removexattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 const char *name)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  loc_t temploc;
  gf_inode_child_t *gic;
  struct list_head *list = loc->inode->private;
  temploc.path = loc->path;
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      temploc.inode = gic->inode;
      STACK_WIND (frame,
		  afr_removexattr_cbk,
		  gic->xl,
		  gic->xl->fops->removexattr,
		  &temploc,
		  name);
    }
  }
  return 0;
}

static int32_t
afr_open_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      fd_t *fdchild)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  fd_t *fd = local->fd;
  int32_t callcnt;
  call_frame_t *prev_frame = cookie;

  if (op_ret == -1 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->mutex); /* FIXME see if this is ok */
  if (op_ret >= 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (op_ret >= 0) {
    dict_set (fd->ctx, prev_frame->this->name, data_from_static_ptr (fdchild));
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (callcnt == 0) {
    /* FIXME: free fd and ctx when all the opens fail */
    if (local->op_ret == 0)
      list_add_tail (&fd->inode_list, &local->inode->fds);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, fd);
  }
  return 0;
}

#define SH_TIME_DELTA 10

static int32_t
afr_selfheal_check (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc)
{
  AFR_DEBUG (this);
  gf_inode_child_t *gic, *compare=NULL;
  struct list_head *list = loc->inode->private;
  int32_t copies = afr_get_num_copies (loc->path, this);
  int32_t cnt = 0;
  int32_t timediff; /* FIXME: this should not be of int32_t? */

  list_for_each_entry (gic, list, clist) {
    cnt++;
    if (cnt > copies)
      break;
    if (gic->op_errno == ENOENT) {
      AFR_DEBUG_FMT (this, "self heal needed op_errno is ENOENT");
      return -1;
    }
    if (gic->op_errno != 0) {
      AFR_DEBUG_FMT (this, "self heal needed op_errno is not 0");
      continue;
    }
    if (compare == NULL) {
      compare = gic;
      continue;
    }
    timediff = gic->stat.st_mtime - compare->stat.st_mtime;
    if (timediff < 0) 
      timediff = timediff * -1;
    if (timediff > SH_TIME_DELTA) {
      AFR_DEBUG_FMT (this, "self heal needed mtime difference is there");
      return -1;
    }
    if (gic->stat.st_size != compare->stat.st_size) {
      AFR_DEBUG_FMT (this, "self heal needed as size not different");
      return -1;
    }
  }
  return 0;
}

static int32_t
afr_selfheal_utimens_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *stbuf)
{
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  AFR_DEBUG_FMT (this, "op_ret = %d op_errno = %d from child %s", op_ret, op_errno, prev_frame->this->name);
  int32_t callcnt;
  gf_inode_child_t *gic;
  struct list_head *list;
  list = local->inode->private;

  if (op_ret == 0){
    list_for_each_entry (gic, list, clist) {
      gic->repair = 0;
      gic->op_errno = 0;
      if (gic->xl == prev_frame->this)
	gic->stat = *stbuf;
    }
  }
  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    call_resume (local->stub);
    STACK_DESTROY (local->orig_frame->root);
    frame->local = NULL;
    STACK_DESTROY (frame->root);
  }
  return 0;
}

static int32_t
afr_selfheal_close_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
  AFR_DEBUG_FMT (this, "op_ret = %d op_errno = %d", op_ret, op_errno);
  int32_t callcnt;
  afr_local_t *local = frame->local;
  gf_inode_child_t *gic;
  struct list_head *list;
  inode_t *inode;
  call_stub_t *stub = local->stub;
  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (callcnt == 0) {
    
    inode = local->inode;
    list = inode->private;
    list_for_each_entry (gic, list, clist) {
      if (gic->inode)
	++local->call_count;
    }
    loc_t temploc;
    temploc.path = stub->args.open.loc.path;
    struct timespec tv[2];
    tv[0].tv_sec = (unsigned long long)local->latest->stat.st_atime;
    tv[1].tv_sec = (unsigned long long)local->latest->stat.st_mtime;
    list_for_each_entry (gic, list, clist) {
      if (gic->inode) {
	temploc.inode = gic->inode;
	STACK_WIND (frame,
		    afr_selfheal_utimens_cbk,
		    gic->xl,
		    gic->xl->fops->utimens,
		    &temploc,
		    tv);
      }
    }
  }
  return 0;
}

static int32_t
afr_selfheal_sync_file (call_frame_t *frame,
			xlator_t *this);

static int32_t
afr_selfheal_sync_file_writev_cbk (call_frame_t *frame,
				   void *cookie,
				   xlator_t *this,
				   int32_t op_ret,
				   int32_t op_errno)
{
  AFR_DEBUG_FMT (this, "op_ret = %d op_errno = %d", op_ret, op_errno);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  call_frame_t *orig_frame = local->orig_frame;
  LOCK(&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK(&frame->mutex);
  if (op_ret >= 0)
    local->op_ret = 0;
  if (callcnt == 0) {
    local->offset = local->offset + op_ret;
    afr_selfheal_sync_file (orig_frame, this);
    //    STACK_DESTROY (frame->root);
  }
  return 0;
}

static int32_t
afr_selfheal_sync_file_readv_cbk (call_frame_t *frame,
				  void *cookie,
				  xlator_t *this,
				  int32_t op_ret,
				  int32_t op_errno,
				  struct iovec *vector,
				  int32_t count)
{
  AFR_DEBUG_FMT (this, "op_ret = %d op_errno = %d ", op_ret, op_errno);
  afr_local_t *local = frame->local;
  gf_inode_child_t *gic;
  struct list_head *list = local->inode->private;

  if (op_ret == 0) {
    AFR_DEBUG_FMT (this, "EOF reached");
    list_for_each_entry (gic, list, clist) {
      if (gic->repair == 0)
	continue;
      if (dict_get (local->fd->ctx, gic->xl->name))
	local->call_count++;
    }
    list_for_each_entry (gic, list, clist) {
      if (gic->repair == 0)
	continue;
      data_t *fdchild_data;
      fd_t *fdchild;
      fdchild_data = dict_get (local->fd->ctx, gic->xl->name);
      if (fdchild_data == NULL)
	continue;
      fdchild = data_to_ptr (fdchild_data);
      STACK_WIND (frame,
		  afr_selfheal_close_cbk,
		  gic->xl,
		  gic->xl->fops->close,
		  fdchild);
    }

  } else {
    list_for_each_entry (gic, list, clist) {
      if (gic->repair == 0 || gic == local->latest)
	continue;
      if (dict_get (local->fd->ctx, gic->xl->name))
	local->call_count++;
    }
    list_for_each_entry (gic, list, clist) {
      if (gic->repair == 0 || gic == local->latest)
	continue;
      data_t *fdchild_data;
      fd_t *fdchild;
      fdchild_data = dict_get (local->fd->ctx, gic->xl->name);
      if (fdchild_data == NULL)
	continue;
      fdchild = data_to_ptr (fdchild_data);
      STACK_WIND (frame,
		  afr_selfheal_sync_file_writev_cbk,
		  gic->xl,
		  gic->xl->fops->writev,
		  fdchild,
		  vector,
		  count,
		  local->offset);
    }
  }
  return 0;
}

static int32_t
afr_selfheal_sync_file_readv (call_frame_t *frame,
			      xlator_t *this,
			      fd_t *fd,
			      size_t size,
			      off_t offset)
{
  AFR_DEBUG_FMT (this, "offset %d", offset);
  afr_local_t *local = frame->local;
  LOCK_INIT (&frame->mutex);
  data_t *fdchild_data = dict_get (fd->ctx, local->latest->xl->name);
  fd_t *fdchild = data_to_ptr (fdchild_data);
  STACK_WIND (frame,
	      afr_selfheal_sync_file_readv_cbk,
	      local->latest->xl,
	      local->latest->xl->fops->readv,
	      fdchild,
	      size,
	      offset);
  return 0;
}

static int32_t
afr_selfheal_sync_file (call_frame_t *frame,
			xlator_t *this)
{
  AFR_DEBUG (this);
  afr_local_t *local = frame->local;
  call_frame_t *sh_frame = copy_frame (frame);
  sh_frame->local = frame->local;
  local->orig_frame = frame;
  afr_selfheal_sync_file_readv (sh_frame,
				this,
				local->fd,
				4096,
				local->offset);
  return 0;
}

static int32_t
afr_selfheal_create_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 fd_t *fdchild,
			 inode_t *inode,
			 struct stat *stbuf)
{
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  inode_t *linode = local->inode;
  fd_t *fd = local->fd;
  struct list_head *list = linode->private;
  gf_inode_child_t *gic;
  int32_t callcnt;
  AFR_DEBUG_FMT (this, "child %s returned, op_ret = %d, fdchild = %p", prev_frame->this->name, op_ret, fdchild);
  if (op_ret >= 0){
    LOCK (&frame->mutex);
    dict_set (fd->ctx, prev_frame->this->name, data_from_static_ptr (fdchild));
    UNLOCK (&frame->mutex);
  }
  list_for_each_entry (gic, list, clist) {
    if (gic->xl == prev_frame->this)
      break;
  }
  if (op_ret >= 0) {
    gic->inode = inode_ref (inode);
    gic->stat = *stbuf;
  } else {
    gic->inode = NULL;
  }
  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (callcnt == 0) {
    fd->inode = linode;
    afr_selfheal_sync_file (frame, this);
  }
  return 0;
}

static int32_t
afr_selfheal_open_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       fd_t *fdchild)
{
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  inode_t *linode = local->inode;
  fd_t *fd = local->fd;
  int32_t callcnt;

  AFR_DEBUG_FMT (this, "child %s returned, op_ret = %d, fdchild = %p", prev_frame->this->name, op_ret, fdchild);
  if (op_ret >= 0) {
    LOCK (&frame->mutex);
    dict_set (fd->ctx, prev_frame->this->name, data_from_static_ptr (fdchild));
    UNLOCK (&frame->mutex);
  }

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (callcnt == 0) {
    fd->inode = linode;
    afr_selfheal_sync_file (frame, this);
  }
  return 0;
}

static int32_t
afr_selfheal (xlator_t *this,
	      call_stub_t *stub,
	      loc_t *loc)
{
  AFR_DEBUG(this);
  call_frame_t *sh_frame = copy_frame (stub->frame);
  afr_local_t *sh_local = calloc (1, sizeof (afr_local_t));
  struct list_head *list = loc->inode->private;
  gf_inode_child_t *gic, *latest = NULL;
  int32_t copies = afr_get_num_copies (loc->path, this);
  int32_t cnt = 0;
  int32_t timediff;
  LOCK_INIT (&sh_frame->mutex);
  sh_frame->local = sh_local;
  sh_local->stub = stub;
  sh_local->fd = calloc (1, sizeof(fd_t));
  sh_local->fd->ctx = get_new_dict();
  sh_local->inode = loc->inode;
  list_for_each_entry (gic, list, clist) {
    cnt++;
    if (cnt > copies)
      break;
    if (gic->inode == NULL)
      continue;
    if (latest == NULL) {
      latest = gic;
      continue;
    }
    if (gic->stat.st_mtime > latest->stat.st_mtime) {
      latest = gic;
    }
  }
  sh_local->latest = latest;
  AFR_DEBUG_FMT(this, "latest child is %s", latest->xl->name);
  cnt = 0;
  list_for_each_entry (gic, list, clist) {
    cnt++;
    if (cnt > copies)
      break;
    if (gic == latest) {
      sh_local->call_count++;
      continue;
    }
    if (gic->op_errno == ENOENT) {
      AFR_DEBUG_FMT (this, "ENOENT at %s", gic->xl->name);
      gic->repair = 1;
      sh_local->call_count++;
      continue;
    }
    if (gic->inode == NULL) /* for now we are not handling any other errnos */
      continue;
    timediff = gic->stat.st_mtime - latest->stat.st_mtime;
    if (timediff < 0)
      timediff = timediff * -1;
    if (timediff > SH_TIME_DELTA) {
      AFR_DEBUG_FMT (this, "mtime difference for %s", gic->xl->name);
      gic->repair = 1;
      sh_local->call_count++;
      continue;
    }
    if (latest->stat.st_size != gic->stat.st_size) {
      AFR_DEBUG_FMT (this, "size different for %s", gic->xl->name);
      gic->repair = 1;
      sh_local->call_count++;
    }
  }

  cnt = 0;
  list_for_each_entry (gic, list, clist) {
    cnt++;
    if (cnt > copies)
      break;
    if (gic->repair == 1 && gic != latest) {
      AFR_DEBUG_FMT (this, "needs repair %s", gic->xl->name);
    }
  }

  afr_local_t *origlocal = stub->frame->local;
  origlocal->shcalled = 1;
  if (sh_local->call_count == 0 || sh_local->call_count == 1) {
    AFR_DEBUG_FMT (this, "self heal not needed? selfheal_check said its needed");
    call_resume (stub);
    return 0;
    /*FIXME : free the calloced mem etc */
  } else {
    AFR_DEBUG_FMT (this, "call_count is %d", sh_local->call_count);
  }
  cnt = 0;
  int32_t mode = latest->stat.st_mode;
  loc_t temploc;
  temploc.path = loc->path;
  list_for_each_entry (gic, list, clist) {
    int32_t flags = O_RDWR | O_LARGEFILE;
    cnt++;
    if (cnt > copies)
      break;
    if (gic->repair == 0 && gic != latest)
      continue;
    if (gic->op_errno == ENOENT) {
      AFR_DEBUG_FMT (this, "caling create() on %s", gic->xl->name);
      STACK_WIND (sh_frame,
		  afr_selfheal_create_cbk,
		  gic->xl,
		  gic->xl->fops->create,
		  loc->path,
		  0,       /* in posix xlator, this will be O_CREAT|O_RDWR|O_LARGEFILE|O_EXCL */
		  mode);
      continue;
    }
    if (gic != latest)
      flags = flags | O_TRUNC;
    AFR_DEBUG_FMT (this, "calling open() on %s", gic->xl->name);
    temploc.inode = gic->inode;
    STACK_WIND (sh_frame,
		afr_selfheal_open_cbk,
		gic->xl,
		gic->xl->fops->open,
		&temploc,
		flags);
  }
  return 0;
}


static int32_t
afr_open (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  int32_t flags)
{
  AFR_DEBUG(this);
  afr_local_t *local; 
  gf_inode_child_t *gic;
  struct list_head *list = loc->inode->private;
  loc_t temploc;
  int32_t ret;

  if (frame->local == NULL) {
    frame->local = (void *) calloc (1, sizeof (afr_local_t));
  }
  local = frame->local;
  ret = afr_selfheal_check (frame, this, loc);
  if (ret == -1) {
    call_stub_t *stub = fop_open_stub (frame, afr_open, loc, flags);
    afr_selfheal (this, stub, loc);
    return 0;
  }
  if (local->shcalled == 0) {
    AFR_DEBUG_FMT (this, "self heal not required");
  } else {
    AFR_DEBUG_FMT (this, "self heal done");
  }
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->fd = calloc (1, sizeof (fd_t));
  local->fd->ctx = get_new_dict ();
  local->inode = loc->inode;
  local->fd->inode = inode_ref (loc->inode);
  temploc.path = loc->path;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if(gic->inode) {
      temploc.inode = gic->inode;
      STACK_WIND (frame,
		  afr_open_cbk,
		  gic->xl,
		  gic->xl->fops->open,
		  &temploc,
		  flags);
    }
  }
  return 0;
}

static int32_t
afr_readv_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct iovec *vector,
	       int32_t count)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, op_ret, op_errno, vector, count);
  return 0;
}

static int32_t
afr_readv (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   size_t size,
	   off_t offset)
{
  AFR_DEBUG(this);
  gf_inode_child_t *gic;
  data_t *fdchild_data = NULL;
  fd_t *fdchild = NULL;
  struct list_head *list= fd->inode->private;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode){
      if ((fdchild_data = dict_get (fd->ctx, gic->xl->name)) != NULL)
	break;
    }
  }

  if (fdchild_data == NULL) {
    trap();
    return 0;
  }
  fdchild = data_to_ptr (fdchild_data);
  STACK_WIND (frame, 
	      afr_readv_cbk,
	      gic->xl,
	      gic->xl->fops->readv,
	      fdchild,
	      size,
	      offset);
  return 0;
}

static int32_t
afr_writev_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;

  if (op_ret == -1 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->mutex);
  if (op_ret != -1 && local->op_ret == -1) { /* FIXME check this logic once more */
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_writev (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    struct iovec *vector,
	    int32_t count,
	    off_t offset)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = fd->inode->private;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode && dict_get (fd->ctx, gic->xl->name))    
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    data_t *fdchild_data;
    fd_t *fdchild;
    if (gic->inode == NULL)
      continue;
    fdchild_data = dict_get (fd->ctx, gic->xl->name);
    if (fdchild_data == NULL)
      continue;
    fdchild = data_to_ptr (fdchild_data);
    STACK_WIND(frame,
	       afr_writev_cbk,
	       gic->xl,
	       gic->xl->fops->writev,
	       fdchild,
	       vector,
	       count,
	       offset);
  }
  return 0;
}

static int32_t
afr_ftruncate_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *stbuf)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;

  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->mutex);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    local->stbuf = *stbuf;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

static int32_t
afr_ftruncate (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       off_t offset)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = fd->inode->private;
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode && dict_get (fd->ctx, gic->xl->name))    
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    data_t *fdchild_data;
    fd_t *fdchild;
    if (gic->inode == NULL)
      continue;
    fdchild_data = dict_get (fd->ctx, gic->xl->name);
    if (fdchild_data == NULL)
      continue;
    fdchild = data_to_ptr (fdchild_data);
    STACK_WIND(frame,
	       afr_ftruncate_cbk,
	       gic->xl,
	       gic->xl->fops->ftruncate,
	       fdchild,
	       offset);
  }
  return 0;
}

static int32_t
afr_fstat_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

static int32_t
afr_fstat (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  AFR_DEBUG(this);
  gf_inode_child_t *gic;
  data_t *fdchild_data = NULL;
  fd_t *fdchild = NULL;
  struct list_head *list= fd->inode->private;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode && (fdchild_data = dict_get (fd->ctx, gic->xl->name)))
      break;
  }

  if (fdchild_data == NULL)
    trap();
  fdchild = data_to_ptr (fdchild_data);
  STACK_WIND (frame,
	      afr_fstat_cbk,
	      gic->xl,
	      gic->xl->fops->fstat,
	      fdchild);
  return 0;
}

static int32_t
afr_flush_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;

  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->mutex);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_flush (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = fd->inode->private;
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode && dict_get (fd->ctx, gic->xl->name))    
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    data_t *fdchild_data;
    fd_t *fdchild;
    if (gic->inode == NULL)
      continue;
    fdchild_data = dict_get (fd->ctx, gic->xl->name);
    if (fdchild_data == NULL)
      continue;
    fdchild = data_to_ptr (fdchild_data);
    STACK_WIND(frame,
	       afr_flush_cbk,
	       gic->xl,
	       gic->xl->fops->flush,
	       fdchild);
  }
  return 0;
}

static int32_t
afr_close_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;

  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->mutex);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    /* FIXME anything else to be done? */
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_close (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = fd->inode->private;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->fd = fd;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode && dict_get (fd->ctx, gic->xl->name))    
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    data_t *fdchild_data;
    fd_t *fdchild;
    if (gic->inode == NULL)
      continue;
    fdchild_data = dict_get (fd->ctx, gic->xl->name);
    if (fdchild_data == NULL)
      continue;
    fdchild = data_to_ptr (fdchild_data);
    STACK_WIND(frame,
	       afr_close_cbk,
	       gic->xl,
	       gic->xl->fops->close,
	       fdchild);
  }
  list_del (&fd->inode_list);
  /* FIXME: free the following, its crashing */
  dict_destroy (fd->ctx);
  free (fd);
  return 0;
}

static int32_t
afr_fsync_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->mutex);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }

  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_fsync (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   int32_t datasync)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = fd->inode->private;
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;


  list_for_each_entry (gic, list, clist) {
    if (gic->inode && dict_get (fd->ctx, gic->xl->name))    
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    data_t *fdchild_data;
    fd_t *fdchild;
    if (gic->inode == NULL)
      continue;
    fdchild_data = dict_get (fd->ctx, gic->xl->name);
    if (fdchild_data == NULL)
      continue;
    fdchild = data_to_ptr (fdchild_data);
    STACK_WIND(frame,
	       afr_fsync_cbk,
	       gic->xl,
	       gic->xl->fops->fsync,
	       fdchild,
	       datasync);
  }
  return 0;
}

static int32_t
afr_lk_cbk (call_frame_t *frame,
	    void *cookie,
	    xlator_t *this,
	    int32_t op_ret,
	    int32_t op_errno,
	    struct flock *lock)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->mutex);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    local->lock = *lock;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->lock);
  }
  return 0;
}

static int32_t
afr_lk (call_frame_t *frame,
	xlator_t *this,
	fd_t *fd,
	int32_t cmd,
	struct flock *lock)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = fd->inode->private;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode && dict_get (fd->ctx, gic->xl->name))    
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    data_t *fdchild_data;
    fd_t *fdchild;
    if (gic->inode == NULL)
      continue;
    fdchild_data = dict_get (fd->ctx, gic->xl->name);
    if (fdchild_data == NULL)
      continue;
    fdchild = data_to_ptr (fdchild_data);
    STACK_WIND(frame,
	       afr_lk_cbk,
	       gic->xl,
	       gic->xl->fops->lk,
	       fdchild,
	       cmd,
	       lock);
  }
  return 0;
}

static int32_t
afr_stat_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct stat *stbuf)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

static int32_t
afr_stat (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc)
{
  AFR_DEBUG(this);
  struct list_head *list;
  gf_inode_child_t *gic;
  loc_t temploc;

  temploc.path = loc->path;

  if (loc->inode->ino == 1) {
    xlator_list_t *trav = this->children;
    list = calloc (1, sizeof (*list));
    INIT_LIST_HEAD (list);
    while (trav) {
      gic = calloc (1, sizeof (*gic));
      gic->xl = trav->xlator;
      gic->inode = inode_search (gic->xl->itable, 1, NULL);
      if (!gic->inode) {
	gf_log (this->name, GF_LOG_DEBUG, "inode_search returned NULL");
	continue;
      }
      list_add_tail (&gic->clist, list);
      trav = trav->next;
    }
    loc->inode->private = list;
  }
  list  = loc->inode->private;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      break;
  }
  temploc.inode = gic->inode;

  STACK_WIND (frame,
	      afr_stat_cbk,
	      gic->xl,
	      gic->xl->fops->stat,
	      &temploc);
  return 0;
}

static int32_t
afr_statfs_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct statvfs *stbuf)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

static int32_t
afr_statfs (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  AFR_DEBUG(this);
  struct list_head *list = loc->inode->private;
  gf_inode_child_t *gic;
  loc_t temploc;

  temploc.path = loc->path;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      break;
  }
  temploc.inode = gic->inode;
  STACK_WIND (frame,
	      afr_statfs_cbk,
	      gic->xl,
	      gic->xl->fops->statfs,
	      &temploc);
  return 0;
}

static int32_t
afr_truncate_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->mutex);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    local->stbuf = *stbuf;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

static int32_t
afr_truncate (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      off_t offset)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = loc->inode->private;
  loc_t temploc;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }
  temploc.path = loc->path;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      temploc.inode = gic->inode;
      STACK_WIND(frame,
		 afr_truncate_cbk,
		 gic->xl,
		 gic->xl->fops->truncate,
		 &temploc,
		 offset);
    }
  }
  return 0;
}

static int32_t
afr_utimens_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *stbuf)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->mutex);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    local->stbuf = *stbuf;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

int32_t
afr_utimens (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     struct timespec tv[2])
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = loc->inode->private;
  loc_t temploc;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }
  temploc.path = loc->path;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      temploc.inode = gic->inode;
      STACK_WIND(frame,
		 afr_utimens_cbk,
		 gic->xl,
		 gic->xl->fops->utimens,
		 &temploc,
		 tv);
    }
  }
  return 0;
}

static int32_t
afr_opendir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 fd_t *fdchild)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  fd_t *fd = local->fd;
  int32_t callcnt;
  call_frame_t *prev_frame = cookie;
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->mutex);
  if (op_ret >= 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (op_ret >= 0) {
    dict_set (fd->ctx, prev_frame->this->name, data_from_static_ptr (fdchild));
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    // FIXME: should i do list_add (&fd->inode_list, &fd->inode->fds);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, fd);
  }
  return 0;
}

static int32_t
afr_opendir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = loc->inode->private;
  loc_t temploc;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->fd = calloc (1, sizeof (fd_t));
  local->fd->ctx = get_new_dict ();
  local->fd->inode = inode_ref (loc->inode);
  temploc.path = loc->path;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      temploc.inode = gic->inode;
      STACK_WIND (frame,
		  afr_opendir_cbk,
		  gic->xl,
		  gic->xl->fops->opendir,
		  &temploc);
    }
  }
  return 0;
}

static int32_t
afr_readlink_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  const char *buf)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t
afr_readlink (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      size_t size)
{
  AFR_DEBUG(this);
  struct list_head *list = loc->inode->private;
  gf_inode_child_t *gic;

  loc_t temploc;
  temploc.path = loc->path;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      break;
  }
  temploc.inode = gic->inode;

  STACK_WIND (frame,
              afr_readlink_cbk,
              gic->xl,
              gic->xl->fops->readlink,
              &temploc,
              size);
  return 0;
}

static int32_t
afr_readdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 dir_entry_t *entry,
		 int32_t count)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, op_ret, op_errno, entry, count);
  return 0;
}

static int32_t
afr_readdir (call_frame_t *frame,
	     xlator_t *this,
	     size_t size,
	     off_t offset,
	     fd_t *fd)
{
  AFR_DEBUG(this);
  gf_inode_child_t *gic;
  data_t *fdchild_data;
  fd_t *fdchild;
  struct list_head *list= fd->inode->private;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode && dict_get (fd->ctx, gic->xl->name))
      break;
  }

  fdchild_data = dict_get (fd->ctx, gic->xl->name);
  if (fdchild_data == NULL)
    trap();
  fdchild = data_to_ptr (fdchild_data);
  STACK_WIND (frame, 
	      afr_readdir_cbk,
	      gic->xl,
	      gic->xl->fops->readdir,
	      size,
	      offset,
	      fdchild);
  return 0;
}

static int32_t
afr_mkdir_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       inode_t *inode,
	       struct stat *buf)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  struct list_head *list;
  gf_inode_child_t *gic;
  call_frame_t *prev_frame = cookie;
  inode_t *linode=NULL;
  int32_t callcnt;

  if (op_ret != 0 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  list = local->list;
  list_for_each_entry (gic, list, clist) {
    if (gic->xl == prev_frame->this) {
      break;
    }
  }

  if (op_ret == 0) {
    local->op_ret = 0;
    gic->inode = inode_ref (inode);
    gic->stat = *buf;
  }

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  /* FIXME: when all children fail, free the private list */
  if (callcnt == 0){
    if (local->op_ret == 0) {
      list_for_each_entry (gic, list, clist) {
	if (gic->inode)
	  break;
      }
      linode = inode_update (this->itable, NULL, NULL, gic->inode->ino);
      linode->private = list;
    }
    struct stat *statptr = local->op_ret == 0 ? &gic->stat : NULL;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  linode,
		  statptr);
  }
  return 0;
}

static int32_t
afr_mkdir (call_frame_t *frame,
	   xlator_t *this,
	   const char *path,
	   mode_t mode)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list;
  xlator_list_t *trav = this->children;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list = calloc (1, sizeof(*list));
  INIT_LIST_HEAD (list);
  local->list = list;
  while (trav) {
    ++local->call_count;
    gic = calloc (1, sizeof (*gic));
    gic->xl = trav->xlator;
    list_add_tail (&gic->clist, list);
    trav = trav->next;
  }
  trav = this->children;
  while (trav) {
    STACK_WIND (frame,
		afr_mkdir_cbk,
		trav->xlator,
		trav->xlator->fops->mkdir,
		path,
		mode);
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_unlink_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  LOCK (&frame->mutex);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_unlink (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = loc->inode->private;
  loc_t temploc;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }
  temploc.path = loc->path;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode){
      temploc.inode = gic->inode;
      STACK_WIND(frame,
		 afr_unlink_cbk,
		 gic->xl,
		 gic->xl->fops->unlink,
		 &temploc);

    }
  }
  return 0;
}

static int32_t
afr_rmdir_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  if (op_ret == 0) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_rmdir (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = loc->inode->private;
  loc_t temploc;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }
  temploc.path = loc->path;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode){
      temploc.inode = gic->inode;
      STACK_WIND(frame,
		 afr_rmdir_cbk,
		 gic->xl,
		 gic->xl->fops->rmdir,
		 &temploc);
    }
  }
  return 0;
}

static int32_t
afr_create_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		fd_t *fdchild,
		inode_t *inode,
		struct stat *stbuf)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  struct list_head *list;
  gf_inode_child_t *gic;
  call_frame_t *prev_frame = cookie;
  inode_t *linode;
  int32_t callcnt;
  fd_t *fd;

  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  linode = local->inode;
  fd = local->fd;
  if (op_ret == 0) {
    LOCK (&frame->mutex);
    if (fd == NULL){
      fd = calloc (1, sizeof (*fd));
      fd->ctx = get_new_dict();
      local->fd = fd;
      local->op_ret = 0;
    }
    dict_set (fd->ctx, ((call_frame_t *)cookie)->this->name, data_from_static_ptr (fdchild));
    UNLOCK (&frame->mutex);
  }
  list = local->list;
  list_for_each_entry (gic, list, clist) {
    if (gic->xl == prev_frame->this) {
      break;
    }
  }

  if (op_ret == 0) {
    gic->inode = inode_ref (inode);
    gic->stat = *stbuf;
  } else {
    gic->inode = NULL;
  }
  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  /* FIXME when all children fail, free the inode */
  if (callcnt == 0){
    if (local->op_ret == 0) {
      list_for_each_entry (gic, list, clist) {
	if (gic->inode)
	  break;
      }
      linode = inode_update (this->itable, NULL, NULL, gic->inode->ino);
      fd->inode = linode;
      list_add (&fd->inode_list, &linode->fds);
      linode->private = list;
    }
    
    struct stat *statptr = local->op_ret == 0 ? &gic->stat : NULL;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  fd,
		  linode,
		  statptr);
  }
  return 0;
}

static int32_t
afr_create (call_frame_t *frame,
	    xlator_t *this,
	    const char *path,
	    int32_t flags,
	    mode_t mode)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  int32_t num_copies = afr_get_num_copies (path, this);
  gf_inode_child_t *gic;
  struct list_head *list;
  int32_t child_count = ((afr_private_t *)this->private)->child_count;
  xlator_list_t *trav = this->children;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  if (num_copies == 0)
    num_copies = 1;

  list = calloc (1, sizeof(*list));
  INIT_LIST_HEAD (list);
  local->list = list;
  while (trav) {
    gic = calloc (1, sizeof (*gic));
    gic->xl = trav->xlator;
    list_add_tail (&gic->clist, list);
    trav = trav->next;
  }
  local->call_count = (num_copies <= child_count) ? num_copies : child_count;
  trav = this->children;
  while (trav) {
    STACK_WIND (frame,
		afr_create_cbk,
		trav->xlator,
		trav->xlator->fops->create,
		path,
		flags,
		mode);
    trav = trav->next;
    num_copies--;
    if (num_copies == 0)
      break;
  }
  return 0;
}

static int32_t
afr_mknod_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       inode_t *inode,
	       struct stat *stbuf)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  struct list_head *list;
  gf_inode_child_t *gic;
  call_frame_t *prev_frame = cookie;
  inode_t *linode=NULL;
  int32_t callcnt;

  if (op_ret != 0 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  list = local->list;
  list_for_each_entry (gic, list, clist) {
    if (gic->xl == prev_frame->this) {
      break;
    }
  }
  if (op_ret == 0) {
    local->op_ret = 0;
    gic->inode = inode_ref (inode);
    gic->stat = *stbuf;
  }

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  /* FIXME: when all children fail, free the private list */
  if (callcnt == 0){
    if (local->op_ret == 0) {
      list_for_each_entry (gic, list, clist) {
	if (gic->inode)
	  break;
      }
      linode = inode_update (this->itable, NULL, NULL, gic->inode->ino);
      linode->private = list;
    }
    struct stat *statptr = local->op_ret == 0 ? &gic->stat : NULL;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  linode,
		  statptr);
  }
  return 0;
}

static int32_t
afr_mknod (call_frame_t *frame,
	   xlator_t *this,
	   const char *path,
	   mode_t mode,
	   dev_t dev)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list;
  xlator_list_t *trav = this->children;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list = calloc (1, sizeof(*list));
  INIT_LIST_HEAD (list);
  local->list = list;
  while (trav) {
    ++local->call_count;
    gic = calloc (1, sizeof (*gic));
    gic->xl = trav->xlator;
    list_add_tail (&gic->clist, list);
    trav = trav->next;
  }
  trav = this->children;
  while (trav) {
    STACK_WIND (frame,
		afr_mknod_cbk,
		trav->xlator,
		trav->xlator->fops->mknod,
		path,
		mode,
		dev);
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_symlink_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *stbuf)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  struct list_head *list;
  gf_inode_child_t *gic;
  call_frame_t *prev_frame = cookie;
  inode_t *linode=NULL;
  int32_t callcnt;

  if (op_ret != 0 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  list = local->list;
  list_for_each_entry (gic, list, clist) {
    if (gic->xl == prev_frame->this) {
      break;
    }
  }

  if (op_ret == 0) {
    local->op_ret = 0;
    gic->inode = inode_ref (inode);
    gic->stat = *stbuf;
  }

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  /* FIXME: when all children fail, free the private list */
  if (callcnt == 0){
    if (local->op_ret == 0) {
      list_for_each_entry (gic, list, clist) {
	if (gic->inode)
	  break;
      }
      linode = inode_update (this->itable, NULL, NULL, gic->inode->ino);
      linode->private = list;
    }
    struct stat *statptr = local->op_ret == 0 ? &gic->stat : NULL;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  linode,
		  statptr);
  }
  return 0;
}

static int32_t
afr_symlink (call_frame_t *frame,
	     xlator_t *this,
	     const char *linkname,
	     const char *newpath)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list;
  xlator_list_t *trav = this->children;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list = calloc (1, sizeof(*list));
  INIT_LIST_HEAD (list);
  local->list = list;
  while (trav) {
    ++local->call_count;
    gic = calloc (1, sizeof (*gic));
    gic->xl = trav->xlator;
    list_add_tail (&gic->clist, list);
    trav = trav->next;
  }
  trav = this->children;
  while (trav) {
    STACK_WIND (frame,
		afr_symlink_cbk,
		trav->xlator,
		trav->xlator->fops->symlink,
		linkname,
		newpath);
    trav = trav->next;
  }
  return 0;
}

static int32_t
afr_rename_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *buf)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  struct list_head *list;
  gf_inode_child_t *gic;
  call_frame_t *prev_frame = cookie;

  if (op_ret != 0 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  list = local->list;
  list_for_each_entry (gic, list, clist) {
    if (gic->xl == prev_frame->this) {
      break;
    }
  }
  if (op_ret == 0) {
    local->op_ret = 0;
    gic->stat = *buf;
  }

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (callcnt == 0){
    if (local->op_ret == 0) {
      list_for_each_entry (gic, list, clist) {
	if (gic->inode)
	  break;
      }
    }
    struct stat *statptr = local->op_ret == 0 ? &gic->stat : NULL;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  statptr);
  }
  return 0;
}

static int32_t
afr_rename (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *oldloc,
	    loc_t *newloc)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list;
  loc_t temploc;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  temploc.path = oldloc->path;
  local->list = list = oldloc->inode->private;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      local->call_count++;
    }
  }
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      temploc.inode = gic->inode;
      STACK_WIND (frame,
		  afr_rename_cbk,
		  gic->xl,
		  gic->xl->fops->rename,
		  &temploc,
		  newloc);
    }
  }
  return 0;
}

/* FIXME: check FIXME of symlink call */
static int32_t
afr_link_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      inode_t *inode,
	      struct stat *stbuf)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  struct list_head *list;
  gf_inode_child_t *gic;
  call_frame_t *prev_frame = cookie;
  inode_t *linode=NULL;
  int32_t callcnt;

  if (op_ret != 0 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  list = local->list;
  list_for_each_entry (gic, list, clist) {
    if (gic->xl == prev_frame->this) {
      break;
    }
  }

  if (op_ret == 0) {
    local->op_ret = 0;
    gic->inode = inode_ref (inode);
    gic->stat = *stbuf;
  }

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  /* FIXME: when all children fail, free the private list */
  if (callcnt == 0){
    if (local->op_ret == 0) {
      list_for_each_entry (gic, list, clist) {
	if (gic->inode)
	  break;
      }
      linode = inode_update (this->itable, NULL, NULL, gic->inode->ino);
      linode->private = list;
    }
    struct stat *statptr = local->op_ret == 0 ? &gic->stat : NULL;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  linode,
		  statptr);
  }
  return 0;
}

static int32_t
afr_link (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *oldloc,
	  const char *newpath)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list;
  loc_t temploc;
  temploc.path = oldloc->path;
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list = oldloc->inode->private;
  local->list = list;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      temploc.inode = gic->inode;
      STACK_WIND (frame,
		  afr_link_cbk,
		  gic->xl,
		  gic->xl->fops->link,
		  &temploc,
		  newpath);
    }
  }
  return 0;
}

static int32_t
afr_chmod_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *stbuf)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->mutex);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    local->stbuf = *stbuf;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

static int32_t
afr_chmod (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = loc->inode->private;
  loc_t temploc;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if(gic->inode)
      ++local->call_count;
  }

  temploc.path = loc->path;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode){
      temploc.inode = gic->inode;
      STACK_WIND(frame,
		 afr_chmod_cbk,
		 gic->xl,
		 gic->xl->fops->chmod,
		 &temploc,
		 mode);
    }
  }
  return 0;
}

static int32_t
afr_chown_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *stbuf)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->mutex);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    local->stbuf = *stbuf;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

static int32_t
afr_chown (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   uid_t uid,
	   gid_t gid)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = loc->inode->private;
  loc_t temploc;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }

  temploc.path = loc->path;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      temploc.inode = gic->inode;
      STACK_WIND(frame,
		 afr_chown_cbk,
		 gic->xl,
		 gic->xl->fops->chown,
		 &temploc,
		 uid,
		 gid);
    }
  }
  return 0;
}

static int32_t
afr_closedir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;

  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->mutex);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

/* releasedir */
static int32_t
afr_closedir (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = fd->inode->private;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if (dict_get (fd->ctx, gic->xl->name))    
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    data_t *fdchild_data;
    fd_t *fdchild;
    fdchild_data = dict_get (fd->ctx, gic->xl->name);
    if (fdchild_data == NULL)
      continue;
    fdchild = data_to_ptr (fdchild_data);
    STACK_WIND(frame,
	       afr_closedir_cbk,
	       gic->xl,
	       gic->xl->fops->closedir,
	       fdchild);
  }
  dict_destroy (fd->ctx);
  free (fd);
  return 0;
}

static int32_t
afr_fchmod (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    mode_t mode)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}

static int32_t
afr_fchown (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    uid_t uid,
	    gid_t gid)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}

/* fsyncdir */
static int32_t
afr_fsyncdir (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t flags)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}

/* access */
static int32_t
afr_access (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t mask)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}

static int32_t
afr_lock_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
afr_lock (call_frame_t *frame,
	  xlator_t *this,
	  const char *path)
{
  AFR_DEBUG(this);
  xlator_t *lock_node = ((afr_private_t *)this->private)->lock_node;

  STACK_WIND (frame,
	      afr_lock_cbk,
	      lock_node,
	      lock_node->mops->lock,
	      path);
  return 0;
}

static int32_t
afr_unlock_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
afr_unlock (call_frame_t *frame,
	    xlator_t *this,
	    const char *path)
{
  AFR_DEBUG(this);
  xlator_t *lock_node = ((afr_private_t*) this->private)->lock_node;
  STACK_WIND (frame,
	      afr_unlock_cbk,
	      lock_node,
	      lock_node->mops->unlock,
	      path);
  return 0;
}

static int32_t
afr_stats_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct xlator_stats *stats)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  if (op_ret != 0 && op_errno == ENOTCONN && local->xlnodeptr->next) {
    LOCK (&frame->mutex);
    local->xlnodeptr = local->xlnodeptr->next;
    UNLOCK (&frame->mutex);
    STACK_WIND (frame,
		afr_stats_cbk,
		local->xlnodeptr->xlator,
		local->xlnodeptr->xlator->mops->stats,
		local->flags);
    return 0;
  }
  LOCK_DESTROY (&frame->mutex);
  STACK_UNWIND (frame, op_ret, op_errno, stats);
  return 0;
}

static int32_t
afr_stats (call_frame_t *frame,
	   xlator_t *this,
	   int32_t flags)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->xlnodeptr = this->children;
  local->flags = flags;
  STACK_WIND (frame,
	      afr_stats_cbk,
	      local->xlnodeptr->xlator,
	      local->xlnodeptr->xlator->mops->stats,
	      flags);
  return 0;
}

void
afr_parse_replicate (char *data, xlator_t *xl)
{
  char *tok, *colon;
  int32_t num_tokens = 0;
  pattern_info_t *pattern_info_list;
  tok = data;
  while (*tok++){
    if(*tok == ',')
      num_tokens++;
  }
  num_tokens++; /* num_tokens is one more than number of ',' */
  tok = strtok (data, ",");
  if (!tok)
    return;
  pattern_info_list = calloc (num_tokens, sizeof (pattern_info_t));
  ((afr_private_t *)xl->private)->pattern_info_list = pattern_info_list;
  num_tokens = 0;
  do {
    colon = tok;
    while(*colon != ':')
      colon++;
    *colon = '\0';
    pattern_info_list[num_tokens].pattern = strdup (tok);
    pattern_info_list[num_tokens].copies = atoi (colon+1);
    num_tokens++;
    tok = strtok (NULL, ",");
  } while(tok);  
  ((afr_private_t*)xl->private)->pil_num = num_tokens;
}

int32_t 
init (xlator_t *xl)
{
  afr_private_t *pvt = calloc (1, sizeof (afr_private_t));
  data_t *lock_node = dict_get (xl->options, "lock-node");
  data_t *replicate = dict_get (xl->options, "replicate");
  int32_t count = 0;
  xlator_list_t *trav = xl->children;
  xl->itable = inode_table_new (100, xl->name);
  if (xl->itable == NULL)
    gf_log (xl->name, GF_LOG_DEBUG, "inode_table_new() failed");
  xl->private = pvt;
  while (trav) {
    gf_log ("afr", GF_LOG_DEBUG, "xlator name is %s", trav->xlator->name);
    count++;
    trav = trav->next;
  }
  gf_log ("afr", GF_LOG_DEBUG, "child count %d", count);
  pvt->child_count = count;
  pvt->debug = 1;
  if (lock_node) {
    trav = xl->children;
    while (trav) {
      if (strcmp (trav->xlator->name, lock_node->data) == 0)
	break;
      trav = trav->next;
    }
    if (trav == NULL) {
      gf_log ("afr", GF_LOG_ERROR, "afr->init: lock-node not found among the children");
      return -1;
    }
    gf_log ("afr", GF_LOG_DEBUG, "lock node is %s\n", trav->xlator->name);
    pvt->lock_node = trav->xlator;
  } else {
    gf_log ("afr", GF_LOG_DEBUG, "afr->init: lock node not specified, defaulting to %s", xl->children->xlator->name);
    pvt->lock_node = xl->children->xlator;
  }

  if(replicate)
    afr_parse_replicate (replicate->data, xl);
  return 0;
}

void
fini(xlator_t *xl)
{
  free (((afr_private_t *)xl->private)->pattern_info_list);
  free (xl->private);
  return;
}

struct xlator_fops fops = {
  .lookup      = afr_lookup,
  .forget      = afr_forget,
  .stat        = afr_stat,
  .readlink    = afr_readlink,
  .mknod       = afr_mknod,
  .mkdir       = afr_mkdir,
  .unlink      = afr_unlink,
  .rmdir       = afr_rmdir,
  .symlink     = afr_symlink,
  .rename      = afr_rename,
  .link        = afr_link,
  .chmod       = afr_chmod,
  .chown       = afr_chown,
  .truncate    = afr_truncate,
  .utimens     = afr_utimens,
  .create      = afr_create,
  .open        = afr_open,
  .readv       = afr_readv,
  .writev      = afr_writev,
  .statfs      = afr_statfs,
  .flush       = afr_flush,
  .close       = afr_close,
  .fsync       = afr_fsync,
  .setxattr    = afr_setxattr,
  .getxattr    = afr_getxattr,
  .listxattr   = afr_listxattr,
  .removexattr = afr_removexattr,
  .opendir     = afr_opendir,
  .readdir     = afr_readdir,
  .closedir    = afr_closedir,
  .fsyncdir    = afr_fsyncdir,
  .access      = afr_access,
  .ftruncate   = afr_ftruncate,
  .fstat       = afr_fstat,
  .lk          = afr_lk,
  .fchmod      = afr_fchmod,
  .fchown      = afr_fchown,

  .lookup_cbk  = afr_lookup_cbk
};

struct xlator_mops mops = {
  .stats = afr_stats,
  .lock = afr_lock,
  .unlock = afr_unlock,
};
