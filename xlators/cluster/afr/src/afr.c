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
 * 1) Check the FIXMEs
 * 2) There are no known mem leaks, check once again
 * 3) some places loc->inode->private is used without doing inode_ref
 * 4) add code comments
 *
 */

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <malloc.h>


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
    if (fnmatch (tmp->pattern, path, 0) == 0) {
      return tmp->copies;
    }
    tmp++;
  }
  return 1;
}

static int32_t
afr_lookup_mkdir_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      inode_t *inode,
		      struct stat *buf)
{
  afr_local_t *local = frame->local;
  int callcnt;
  inode_t *linode = local->inode;
  gf_inode_child_t *gic;
  struct list_head *list = linode->private;
  call_frame_t *prev_frame = cookie;
  AFR_DEBUG_FMT (this, "op_ret = %d op_errno = %d from client %s", op_ret, op_errno, prev_frame->this->name);
  /* FIXME what if rmdir was done and inode got forget() */
  if (op_ret == 0) {
    list_for_each_entry (gic, list, clist) {
      if (prev_frame->this == gic->xl)
	break;
    }
    gic->inode = inode_ref (inode);
    gic->stat = *buf;
    gic->op_errno = 0;
  }
  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  linode,
		  &local->stbuf);
    inode_unref (linode);
  }
  return 0;
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
  gf_inode_child_t *gic, *gictemp;
  call_frame_t *prev_frame = cookie;
  inode_t *linode = NULL;
  int32_t callcnt;
  AFR_DEBUG_FMT(this, "op_ret = %d op_errno = %d, inode = %p, returned from %s", op_ret, op_errno, inode, prev_frame->this->name);

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
    if (gic->inode == NULL)
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
      /* we will preserve the inode number even if the first child goes down */
      ino_t ino;
      list_for_each_entry (gic, list, clist) {
	if (gic->inode)
	  break;
      }
      if (local->inode) {
	ino = local->inode->ino;
      } else {
	ino = gic->inode->ino;
      }
      local->stbuf = gic->stat;
      local->stbuf.st_ino = ino;
      linode = inode_update (this->itable, NULL, NULL, &local->stbuf);
      if (local->inode && (linode != local->inode))
	inode_forget (local->inode, 0);
      linode->private = list;
      if (((afr_private_t *)this->private)->self_heal   && S_ISDIR (local->stbuf.st_mode)) {
	list_for_each_entry (gic, list, clist) {
	  if (gic->op_errno == ENOENT)
	    local->call_count++;
	}
	if (local->call_count) {
	  char *path = local->path;
	  local->inode = linode;  /* already refed in inode_update, will be unrefed in cbk */

	  list_for_each_entry (gic, list, clist) {
	    if(gic->op_errno == ENOENT) {
	      AFR_DEBUG_FMT (this, "calling mkdir(%s) on %s", path, gic->xl->name);
	      STACK_WIND (frame,
			  afr_lookup_mkdir_cbk,
			  gic->xl,
			  gic->xl->fops->mkdir,
			  path,
			  local->stbuf.st_mode);
	    }
	  }
	  free (path);
	  return 0;
	}
      }
    } else if (local->inode == NULL) {
      list_for_each_entry_safe (gic, gictemp, list, clist) {
	list_del (&gic->clist);
	free (gic);
      }
      free (list);
    }
    free (local->path);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  linode,
		  &local->stbuf);
    if (linode) {
      inode_unref (linode);
    }
  }
  return 0;
}

static int32_t
afr_lookup (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  AFR_DEBUG_FMT (this, "loc->path = %s loc->inode = %p", loc->path, loc->inode);
  afr_local_t *local = calloc (1, sizeof (*local));
  xlator_list_t *trav = this->children;
  gf_inode_child_t *gic;
  struct list_head *list;
  loc_t temploc;
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->loc = loc;
  temploc.inode = NULL;
  temploc.path = loc->path;
  local->path = strdup (loc->path); /* see if we can avoid strdup */
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
    int32_t cnt = local->call_count;
    list_for_each_entry (gic, list, clist) {
      if (gic->inode) {
	temploc.inode = inode_ref (gic->inode);
      } else {
	temploc.inode = NULL;
      }
      STACK_WIND (frame,
		  afr_lookup_cbk,
		  gic->xl,
		  gic->xl->fops->lookup,
		  &temploc);
      if(temploc.inode) {
	inode_unref (temploc.inode); /* we unref here as it will be again refed at cbk (?) */
      }
      if (--cnt == 0)
	break;
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
  AFR_DEBUG_FMT(this, "op_ret = %d", op_ret);

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
  AFR_DEBUG_FMT(this, "inode = %u", inode->ino);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic, *gictemp;
  struct list_head *list = inode->private;
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  if (inode->fds.next != &inode->fds) {
    AFR_DEBUG_FMT(this, "FORGET_ERROR inode->fds.next %p &inode->fds %p", inode->fds.next, &inode->fds);
  }
  inode->private = (void*) 0xFFFFFFFF; /* if any other thread tries to access we'll know */

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (gic->inode){
      STACK_WIND(frame,
		 afr_forget_cbk,
		 gic->xl,
		 gic->xl->fops->forget,
		 gic->inode);
      inode_unref (gic->inode);
    }
  }
  list_for_each_entry_safe (gic, gictemp, list, clist) {
    list_del (& gic->clist);
    free (gic);
  }
  free (list);
  inode_forget (inode, 0);

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
	      dict_t *dict,
	      int32_t flags)
{
  AFR_DEBUG_FMT (this, "loc->path = %s", loc->path);
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
      temploc.inode = inode_ref (gic->inode);
      STACK_WIND(frame,
		 afr_setxattr_cbk,
		 gic->xl,
		 gic->xl->fops->setxattr,
		 &temploc,
		 dict,
		 flags);
      inode_unref (temploc.inode);
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
		  dict_t *dict)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, op_ret, op_errno, dict);
  return 0;
}

static int32_t
afr_getxattr (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  AFR_DEBUG_FMT (this, "loc->path = %s", loc->path);
  struct list_head *list = loc->inode->private;
  gf_inode_child_t *gic;
  loc_t temploc;
  temploc.path = loc->path;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      break;
  }
  temploc.inode = inode_ref (gic->inode);
  STACK_WIND (frame,
	      afr_getxattr_cbk,
	      gic->xl,
	      gic->xl->fops->getxattr,
	      &temploc);
  inode_unref (temploc.inode);
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
      temploc.inode = inode_ref (gic->inode);
      STACK_WIND (frame,
		  afr_removexattr_cbk,
		  gic->xl,
		  gic->xl->fops->removexattr,
		  &temploc,
		  name);
      inode_unref (temploc.inode);
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
    if (fd == NULL) {
      fd = fd_create (local->inode);
      local->fd = fd;
      dict_set (fd->ctx, this->name, data_from_ptr (local->path));
    }
    dict_set (fd->ctx, prev_frame->this->name, data_from_static_ptr (fdchild));
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (callcnt == 0) {
    /* FIXME: free fd and ctx when all the opens fail */
    //    if (local->op_ret == 0)
    //      list_add_tail (&fd->inode_list, &local->inode->fds);
    inode_unref (local->inode);
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, fd);
  }
  return 0;
}

static int32_t
afr_selfheal_unlock_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
  afr_local_t *local = frame->local;
  afr_selfheal_t *ash, *ashtemp;
  struct list_head *list = local->list;
  AFR_DEBUG_FMT (this, "call_resume()");
  call_resume (local->stub);
  free ((char*)local->loc->path);
  inode_unref (local->loc->inode);
  free (local->loc);
  if (local->fd) {
    dict_destroy (local->fd->ctx);
    free (local->fd);
  }
  list_for_each_entry_safe (ash, ashtemp, list, clist) {
    list_del (&ash->clist);
    if(ash->inode)
      inode_unref (ash->inode);
    if (ash->dict)
      dict_destroy (ash->dict);
    free (ash);
  }
  free (list);
  LOCK_DESTROY (&frame->mutex);
  STACK_DESTROY (frame->root);
  return 0;
}

static int32_t
afr_selfheal_setxattr_cbk (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno)
{
  afr_local_t *local = frame->local;
  int32_t callcnt;
  call_frame_t *prev_frame = cookie;
  AFR_DEBUG_FMT (this, "op_ret = %d from client %s", op_ret, prev_frame->this->name);
  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (callcnt == 0) {
    STACK_WIND (frame,
		afr_selfheal_unlock_cbk,
		local->lock_node,
		local->lock_node->mops->unlock,
		local->loc->path);
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
  AFR_DEBUG (this);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  struct list_head *list;
  afr_selfheal_t *ash;
  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (callcnt == 0) {
    list = local->list;
    list_for_each_entry (ash, list, clist) {
      if (ash->repair || ash->version == 1) 
	/* version 1 means there are no attrs, possibly it was copied
	 *  direcly in the backend
	 */
	local->call_count++;
    }
    loc_t temploc;
    temploc.path = local->loc->path;
    list_for_each_entry (ash, list, clist) {
      if (ash->repair || ash->version == 1) {
	temploc.inode = inode_ref (ash->inode);
	AFR_DEBUG_FMT (this, "setxattr() on %s version %u ctime %u", ash->xl->name, local->source->version, local->source->ctime);
	STACK_WIND (frame,
		    afr_selfheal_setxattr_cbk,
		    ash->xl,
		    ash->xl->fops->setxattr,
		    &temploc,
		    local->source->dict,
		    0);
	inode_unref (temploc.inode);
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
				   int32_t op_errno,
				   struct stat *stat)
{
  AFR_DEBUG_FMT (this, "op_ret = %d", op_ret);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  LOCK(&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK(&frame->mutex);
  if (callcnt == 0) {
    local->offset = local->offset + op_ret;
    afr_selfheal_sync_file (frame, this);
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
				  int32_t count,
				  struct stat *stat)
{
  AFR_DEBUG_FMT (this, "op_ret = %d", op_ret);
  afr_local_t *local = frame->local;
  struct list_head *list=local->list;
  afr_selfheal_t *ash;
  data_t *fdchild_data;
  fd_t *fdchild;

  list_for_each_entry (ash, list, clist) {
    if (dict_get(local->fd->ctx, ash->xl->name))
      local->call_count++;
  }

  if (op_ret == 0) {
    AFR_DEBUG_FMT (this, "EOF reached");
    list_for_each_entry (ash, list, clist) {
      if ((fdchild_data = dict_get (local->fd->ctx, ash->xl->name)) != NULL) {
	fdchild = data_to_ptr (fdchild_data);
	STACK_WIND (frame,
		    afr_selfheal_close_cbk,
		    ash->xl,
		    ash->xl->fops->close,
		    fdchild);
      }
    }
  } else {
    local->call_count--; /* we dont write on source */
    list_for_each_entry (ash, list, clist) {
      if (ash == local->source)
	continue;
      if ((fdchild_data = dict_get (local->fd->ctx, ash->xl->name)) != NULL) {
	fdchild = data_to_ptr (fdchild_data);
	STACK_WIND (frame,
		    afr_selfheal_sync_file_writev_cbk,
		    ash->xl,
		    ash->xl->fops->writev,
		    fdchild,
		    vector,
		    count,
		    local->offset);
      }
    }
  }
  return 0;
}

static int32_t
afr_selfheal_sync_file (call_frame_t *frame,
			xlator_t *this)
{
  AFR_DEBUG (this);
  afr_local_t *local = frame->local;
  data_t *fdchild_data = dict_get (local->fd->ctx, local->source->xl->name);
  fd_t *fdchild = data_to_ptr (fdchild_data);
  size_t readbytes = 128*1024;
  AFR_DEBUG_FMT (this, "reading from offset %u", local->offset);
  STACK_WIND (frame,
	      afr_selfheal_sync_file_readv_cbk,
	      local->source->xl,
	      local->source->xl->fops->readv,
	      fdchild,
	      readbytes,
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
			 struct stat *stat)
{
  AFR_DEBUG (this);
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  fd_t *fd = local->fd;
  struct list_head *list = local->loc->inode->private;
  gf_inode_child_t *gic;
  afr_selfheal_t *ash;
  int32_t callcnt;
  AFR_DEBUG_FMT (this, "op_ret = %d from %s", op_ret, prev_frame->this->name);
  if (op_ret >= 0) {
    LOCK (&frame->mutex);
    dict_set (fd->ctx, prev_frame->this->name, data_from_static_ptr (fdchild));
    UNLOCK (&frame->mutex);
    list_for_each_entry (gic, list, clist) {
      if (gic->xl == prev_frame->this)
	break;
    }
    gic->inode = inode_ref (inode);
    gic->stat = *stat;
    list = local->list;
    list_for_each_entry (ash, list, clist) {
      if (ash->xl == prev_frame->this)
	break;
    }
    ash->inode = inode_ref (inode);
  }
  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
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
  fd_t *fd = local->fd;
  int32_t callcnt;
  AFR_DEBUG_FMT (this, "op_ret = %d from %s", op_ret, prev_frame->this->name);
  if (op_ret >= 0) {
    LOCK (&frame->mutex);
    dict_set (fd->ctx, prev_frame->this->name, data_from_static_ptr (fdchild));
    UNLOCK (&frame->mutex);
  }
  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    afr_selfheal_sync_file (frame, this);
  }
  return 0;
}

/* TODO: crappy code, clean this function */

static int32_t
afr_selfheal_getxattr_cbk (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno,
			   dict_t *dict)
{
  afr_local_t *local = frame->local;
  struct list_head *list = local->list;
  afr_selfheal_t *ash;
  int32_t callcnt;
  call_frame_t *prev_frame = cookie;

  list_for_each_entry (ash, list, clist) {
    if (prev_frame->this == ash->xl)
      break;
  }
  if (op_ret >= 0) {
    if (dict){ 
      data_t *version_data = dict_get (dict, "trusted.afr.version");
      if (version_data) 
	ash->version = data_to_uint32 (version_data);
      else {
	AFR_DEBUG_FMT (this, "version attribute was not found on %s, defaulting to 1", prev_frame->this->name)
	ash->version = 1;
      }
      data_t *ctime_data = dict_get (dict, "trusted.afr.createtime");
      if (ctime_data)
	ash->ctime = data_to_uint32 (ctime_data);
      else
	ash->ctime = 0;
      AFR_DEBUG_FMT (this, "op_ret = %d version = %u ctime = %u from %s", op_ret, ash->version, ash->ctime, prev_frame->this->name);
      ash->op_errno = 0;
      ash->dict = dict_copy(dict, NULL);
    }
  } else {
    AFR_DEBUG_FMT (this, "op_ret = %d from %s", op_ret, prev_frame->this->name);
    ash->op_errno = op_errno;
  }

  LOCK(&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    uint32_t latest = 0;
    int32_t copies = afr_get_num_copies (local->loc->path, this);
    int32_t cnt = 0;
    afr_selfheal_t *source = NULL;
    int32_t ctime_repair = 0;
    list_for_each_entry (ash, list, clist) {
      if (ash->inode == NULL)
	continue;
      if (source == NULL) {
	source = ash;
	continue;
      }
      if (source->ctime != ash->ctime) {
	ctime_repair = 1;
	if (source->ctime < ash->ctime)
	  source = ash;
	continue;
      }
      if (source->ctime == ash->ctime && source->version < ash->version) {
	source = ash;
      }
    }

    if (ctime_repair) {
      AFR_DEBUG_FMT(this, "create time difference! latest is %s", source->xl->name);
      cnt = 0;
      list_for_each_entry (ash, list, clist) {
	cnt++;
	if (ash == source) {
	  AFR_DEBUG_FMT (this, "%s is the source", ash->xl->name);
	  local->call_count++;
	  continue;
	}
	if (ash->op_errno == ENOENT) {
	  if (cnt > copies) {
	    AFR_DEBUG_FMT (this, "cnt > copies for %s", ash->xl->name);
	    continue;
	  }
	  AFR_DEBUG_FMT (this, "file missing on %s", ash->xl->name);
	  ash->repair = 1;
	  local->call_count++;
	  continue;
	}
	if (ash->inode == NULL) {
	  AFR_DEBUG_FMT (this, "something wrong with %s errno = %d", ash->xl->name, ash->op_errno);
	  continue;
	}
	if (source->ctime > ash->ctime) {
	  AFR_DEBUG_FMT (this, "%s ctime is outdated", ash->xl->name);
	  ash->repair = 1;
	  local->call_count++;
	  continue;
	}
	if (source->ctime == ash->ctime && source->version > ash->version) {
	  AFR_DEBUG_FMT (this, "%s version is outdated", ash->xl->name);
	  ash->repair = 1;
	  local->call_count++;
	}
	AFR_DEBUG_FMT (this, "%s does not need repair", ash->xl->name);
      }
    } else {
      source = NULL;
      list_for_each_entry (ash, list, clist) {
	AFR_DEBUG_FMT (this, "latest %d ash->version %d", latest, ash->version);
	if (ash->inode) {
	  if (latest == 0) {
	    latest = ash->version;
	    continue;
	  }
	  if (ash->version > latest)
	    latest = ash->version;
	}
      }
      if (latest == 0) {
	AFR_DEBUG_FMT (this, "latest version is 0? or the file does not have verion attribute?");
	STACK_WIND (frame,
		    afr_selfheal_unlock_cbk,
		    local->lock_node,
		    local->lock_node->mops->unlock,
		    local->loc->path);
	return 0;
      }
      AFR_DEBUG_FMT (this, "latest version is %u", latest);
      cnt = 0;
      list_for_each_entry (ash, list, clist) {
	cnt++;
	if (latest == ash->version && source == NULL) {
	  local->call_count++;
	  AFR_DEBUG_FMT (this, "%s is latest, %d", ash->xl->name, local->call_count);
	  source = ash;
	  continue;
	}
	if (ash->op_errno == ENOENT) {
	  if (cnt > copies) {
	    AFR_DEBUG_FMT (this, "cnt > copies for %s", ash->xl->name);
	    continue;
	  }
	  local->call_count++;
	  AFR_DEBUG_FMT (this, "%s has ENOENT , %d", ash->xl->name, local->call_count);
	  ash->repair = 1;
	  continue;
	}
	if (ash->op_errno != 0) /* we will not repair any other errors like ENOTCONN */
	  continue;
	 /* NULL and not ENOENT means, the file got 
	  * and number of copies is less than number of children
	  */
	if (ash->inode == NULL)
	  continue;

	if (latest > ash->version) {
	  ash->repair = 1;
	  local->call_count++;
	  AFR_DEBUG_FMT (this, "%s version %d outdated, latest=%d, %d", ash->xl->name, ash->version, latest, local->call_count);
	}
      }
      if (local->call_count == 1) {
	AFR_DEBUG_FMT (this, "self heal NOT needed");
	STACK_WIND (frame,
		    afr_selfheal_unlock_cbk,
		    local->lock_node,
		    local->lock_node->mops->unlock,
		    local->loc->path);
	return 0;
	/*FIXME I think everything gets cleaned in the cbk function, check */
      }
    }
 
    AFR_DEBUG_FMT (this, "self heal needed, source is %s", source->xl->name);
    local->source = source;
    local->fd = calloc (1, sizeof(fd_t));
    local->fd->ctx = get_new_dict();
    loc_t temploc;
    temploc.path = local->loc->path;
    list_for_each_entry (ash, list, clist) {
      if (ash == source) {
	temploc.inode = inode_ref (ash->inode);
	AFR_DEBUG_FMT (this, "open() on %s", ash->xl->name);
	STACK_WIND (frame,
		    afr_selfheal_open_cbk,
		    ash->xl,
		    ash->xl->fops->open,
		    &temploc,
		    O_RDONLY);
	inode_unref (temploc.inode);
	continue;
      }
      if (ash->repair == 0) { /* case where op_errno might be ENOTCONN */
	AFR_DEBUG_FMT (this, "repair not needed on %s", ash->xl->name);
	continue;             /* also when we are not supposed to replicate here */
      }

      if (ash->op_errno == ENOENT) {
	AFR_DEBUG_FMT (this, "create() on %s", ash->xl->name);
	STACK_WIND (frame,
		    afr_selfheal_create_cbk,
		    ash->xl,
		    ash->xl->fops->create,
		    local->loc->path,
		    0,
		    0500);
	continue;
      }
      temploc.inode = inode_ref (ash->inode);
      AFR_DEBUG_FMT (this, "open() on %s", ash->xl->name);
      STACK_WIND (frame,
		  afr_selfheal_open_cbk,
		  ash->xl,
		  ash->xl->fops->open,
		  &temploc,
		  O_RDWR | O_TRUNC);
      inode_unref (temploc.inode);
    }
  }
  return 0;
}

static int32_t
afr_selfheal_lock_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
  AFR_DEBUG_FMT(this, "op_ret = %d", op_ret, op_errno);
  LOCK_INIT (&frame->mutex);
  afr_local_t *local = frame->local;
  afr_selfheal_t *ash;
  struct list_head *list = local->list;
  if (op_ret == -1) {
    AFR_DEBUG_FMT (this, "locking failed!");
    call_frame_t *open_frame = local->orig_frame;
    afr_local_t *open_local = open_frame->local;
    open_local->sh_return_error = 1;
    call_resume(local->stub);
    /* FIXME: do other cleanups like freeing the ash list */
    return 0;
  }
  list_for_each_entry (ash, list, clist) {
    if(ash->inode)
      local->call_count++;
  }
  int32_t totcnt = local->call_count;
  int32_t cnt = 0;
  loc_t temploc;
  temploc.path = local->loc->path;
  list_for_each_entry (ash, list, clist) {
    if (ash->inode) {
      temploc.inode = inode_ref (ash->inode);
      AFR_DEBUG_FMT (this, "calling getxattr on %s", ash->xl->name);
      STACK_WIND (frame,
		  afr_selfheal_getxattr_cbk,
		  ash->xl,
		  ash->xl->fops->getxattr,
		  &temploc);
      inode_unref (temploc.inode);
      if (++cnt == totcnt)
	break;
    }
  }
  return 0;
}

/* FIXME there is no use of using another frame, just use the open's frame itself */

static int32_t
afr_selfheal (call_frame_t *frame,
	      xlator_t *this,
	      call_stub_t *stub,
	      loc_t *loc)
{
  AFR_DEBUG(this);
  call_frame_t *shframe = copy_frame (frame);
  afr_local_t *shlocal = calloc (1, sizeof (afr_local_t));
  struct list_head *list = calloc (1, sizeof (*list));
  afr_selfheal_t *ash;
  gf_inode_child_t *gic;
  INIT_LIST_HEAD (list);
  shframe->local = shlocal;
  shlocal->list = list;
  shlocal->loc = calloc (1, sizeof (loc_t));
  shlocal->loc->path = strdup (loc->path);
  shlocal->loc->inode = inode_ref (loc->inode);
  shlocal->orig_frame = frame;
  shlocal->stub = stub;
  ((afr_local_t*)frame->local)->shcalled = 1;
  struct list_head *inodepvt = loc->inode->private;
  list_for_each_entry (gic, inodepvt, clist) {
    ash = calloc (1, sizeof (*ash));
    ash->xl = gic->xl;
    if (gic->inode)
      ash->inode = inode_ref (gic->inode);
    ash->op_errno = gic->op_errno;
    list_add_tail (&ash->clist, list);
  }

  list_for_each_entry (ash, list, clist) {
    if (ash->inode) 
      break;
  }
  if (&ash->clist == list) { /* FIXME is this ok */
    STACK_UNWIND (frame, -1, EIO, NULL);
    return 0;
    /* FIXME clean up the mess, check if can reach this point */
  }
  AFR_DEBUG_FMT (this, "locking the node %s", ash->xl->name);
  shlocal->lock_node = ash->xl;
  
  STACK_WIND (shframe,
	      afr_selfheal_lock_cbk,
	      ash->xl,
	      ash->xl->mops->lock,
	      loc->path);

  return 0;
}


static int32_t
afr_open (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  int32_t flags)
{
  AFR_DEBUG_FMT (this, "loc->path = %s loc->inode = %p", loc->path, loc->inode);
  afr_local_t *local; 
  gf_inode_child_t *gic;
  struct list_head *list = loc->inode->private;
  loc_t temploc;
  int32_t ret;

  if (frame->local == NULL) {
    frame->local = (void *) calloc (1, sizeof (afr_local_t));
  }
  local = frame->local;

  if (((afr_private_t *) this->private)->self_heal) {
    AFR_DEBUG_FMT (this, "self heal enabled");
    if (local->sh_return_error) {
      AFR_DEBUG_FMT (this, "self heal failed, open will return EIO");
      STACK_UNWIND (frame, -1, EIO, NULL);
    }
    if (local->shcalled == 0) {
      AFR_DEBUG_FMT (this, "self heal checking...");
      call_stub_t *stub = fop_open_stub (frame, afr_open, loc, flags);
      ret = afr_selfheal (frame, this, stub, loc);
      return 0;
    }
    AFR_DEBUG_FMT (this, "self heal already called");
  } else {
    AFR_DEBUG_FMT (this, "self heal disabled");
  }

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->inode = inode_ref (loc->inode);
  local->path = strdup(loc->path);
  temploc.path = loc->path;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if(gic->inode) {
      temploc.inode = inode_ref (gic->inode);
      STACK_WIND (frame,
		  afr_open_cbk,
		  gic->xl,
		  gic->xl->fops->open,
		  &temploc,
		  flags);
      inode_unref (temploc.inode);
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
	       int32_t count,
	       struct stat *stat)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, op_ret, op_errno, vector, count, stat);
  return 0;
}

static int32_t
afr_readv (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   size_t size,
	   off_t offset)
{
  AFR_DEBUG_FMT(this, "fd %p", fd);
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
		int32_t op_errno,
		struct stat *stat)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;

  if (op_ret == -1 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->mutex);
  if (op_ret != -1 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, stat);
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
  AFR_DEBUG_FMT(this, "fd %p", fd);
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
  AFR_DEBUG_FMT(this, "fd %p", fd);
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
    return 0;
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
  AFR_DEBUG_FMT(this, "fd %p", fd);
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
    LOCK_DESTROY (&frame->mutex);
    AFR_DEBUG_FMT (this, "close() stack_unwinding");
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}


static int32_t
afr_close_unlock_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
  AFR_DEBUG (this);
  afr_local_t *local = frame->local;
  struct list_head *list = local->list;
  afr_selfheal_t *ash, *ashtemp;
  data_t *fdchild_data;
  fd_t *fdchild, *fd;
  loc_t *loc = local->loc;
  fd = local->fd;
  list_for_each_entry (ash, list, clist) {
    if (dict_get(local->fd->ctx, ash->xl->name))
      local->call_count++;
  }
  list_for_each_entry (ash, list, clist) {
    if ((fdchild_data = dict_get(local->fd->ctx, ash->xl->name)) != NULL) {
      fdchild = data_to_ptr (fdchild_data);
      STACK_WIND (frame,
		  afr_close_cbk,
		  ash->xl,
		  ash->xl->fops->close,
		  fdchild);
    }
  }

  list_for_each_entry_safe (ash, ashtemp, list, clist) {
    list_del (&ash->clist);
    if (ash->inode)
      inode_unref (ash->inode);
    free (ash);
  }
  free(list);
  fd_destroy (fd);
  free (loc);
  return 0;
}

static int32_t
afr_close_setxattr_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
  AFR_DEBUG (this);
  afr_local_t *local = frame->local;
  int32_t callcnt;

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (callcnt == 0) {
    STACK_WIND (frame,
		afr_close_unlock_cbk,
		local->lock_node,
		local->lock_node->mops->unlock,
		local->loc->path);
  }
  return 0;
}

static int32_t
afr_close_getxattr_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			dict_t *dict)
{
  AFR_DEBUG (this);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  struct list_head *list = local->list;
  afr_selfheal_t *ash;
  call_frame_t *prev_frame = cookie;
  list_for_each_entry (ash, list, clist) {
    if (prev_frame->this == ash->xl)
      break;
  }
  if (dict) {
    ash->version = data_to_uint32 (dict_get(dict, "trusted.afr.version"));
    AFR_DEBUG_FMT (this, "version %d returned from %s", ash->version, prev_frame->this->name);
  } else 
    AFR_DEBUG_FMT (this, "version attribute missing on %s", prev_frame->this->name);
  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if(callcnt == 0) {
    dict_t *attr;
    loc_t temploc;
    temploc.path = local->loc->path;
    attr = get_new_dict();
    list_for_each_entry (ash, list, clist) {
      if (dict_get(local->fd->ctx, ash->xl->name))
	local->call_count++;
    }
    int32_t totcnt = local->call_count;
    int32_t cnt = 0;
    list_for_each_entry (ash, list, clist) {
      if (dict_get(local->fd->ctx, ash->xl->name)) {
	temploc.inode = inode_ref (ash->inode);
	dict_set (attr, "trusted.afr.version", data_from_uint32(ash->version+1));
	STACK_WIND (frame,
		    afr_close_setxattr_cbk,
		    ash->xl,
		    ash->xl->fops->setxattr,
		    &temploc,
		    attr,
		    0);
	inode_unref (temploc.inode);
	if (++cnt == totcnt)  /* in case posix was loaded as child */
	  break;
      }
    }
    dict_destroy (attr);
  }
  return 0;
}

static int32_t
afr_close_lock_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  AFR_DEBUG (this);
  afr_local_t *local = frame->local;
  gf_inode_child_t *gic;
  struct list_head *list;
  afr_selfheal_t *ash;
  fd_t *fd = local->fd;
  list = fd->inode->private;
  struct list_head *ashlist = calloc(1, sizeof (*ashlist));
  INIT_LIST_HEAD (ashlist);  
  list_for_each_entry (gic, list, clist) {
    if (dict_get (fd->ctx, gic->xl->name)) {
      local->call_count++;
      ash = calloc (1, sizeof (*ash));
      ash->xl = gic->xl;
      ash->inode = inode_ref(gic->inode);
      list_add_tail (&ash->clist, ashlist);
    }
  }
  loc_t temploc;
  temploc.path = local->loc->path;
  local->list = ashlist;
  list_for_each_entry (gic, list, clist) {
    if (dict_get (fd->ctx, gic->xl->name)) {
      temploc.inode = inode_ref (gic->inode);
      STACK_WIND (frame,
		  afr_close_getxattr_cbk,
		  gic->xl,
		  gic->xl->fops->getxattr,
		  &temploc);
      inode_unref (temploc.inode);
    }
  }
  return 0;
}

static int32_t
afr_close (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  inode_t *inode = fd->inode;
  struct list_head *list = inode->private;
  gf_inode_child_t *gic;
  afr_local_t *local = calloc (1, sizeof(*local));
  data_t *path_data = dict_get (fd->ctx, this->name);
  char *path = data_to_ptr (path_data);
  AFR_DEBUG_FMT (this, "close on %s fd %p", path, fd);
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->fd = fd;
  
  if (((afr_private_t*) this->private)->self_heal == 1) {
    local->loc = calloc (1, sizeof (loc_t));
    local->loc->path = path;
    local->loc->inode = fd->inode;
    AFR_DEBUG_FMT (this, "self heal enabled, increasing the version count");
    list_for_each_entry (gic, list, clist) {
      if (gic->inode)
	break;
    }
    local->lock_node = gic->xl;
    STACK_WIND (frame,
		afr_close_lock_cbk,
		gic->xl,
		gic->xl->mops->lock,
		path);
  } else {
    AFR_DEBUG_FMT (this, "self heal disabled");
    list_for_each_entry (gic, list, clist) {
      if (dict_get(local->fd->ctx, gic->xl->name))
	local->call_count++;
    }
    list_for_each_entry (gic, list, clist) {
      data_t *fdchild_data;
      fd_t *fdchild;
      if ((fdchild_data = dict_get(local->fd->ctx, gic->xl->name)) != NULL) {
	fdchild = data_to_ptr (fdchild_data);
	STACK_WIND (frame,
		    afr_close_cbk,
		    gic->xl,
		    gic->xl->fops->close,
		    fdchild);
      }
    }
    fd_destroy (fd);
  }
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
  AFR_DEBUG_FMT(this, "fd %p", fd);
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
  AFR_DEBUG_FMT(this, "fd %p", fd);
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
  AFR_DEBUG_FMT(this, "frame %p op_ret %d", frame, op_ret);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  //  if (op_ret == 0 && (stbuf->st_mtime > local->stbuf.st_mtime)) 
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = 0;
    local->stbuf = *stbuf;
  }
  UNLOCK (&frame->mutex);

  if (callcnt == 0) {
    local->stbuf.st_ino = local->inode->ino;
    inode_unref (local->inode);
    STACK_UNWIND (frame, op_ret, op_errno, &local->stbuf);
  }
  return 0;
}

static int32_t
afr_stat (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc)
{
  AFR_DEBUG_FMT(this, "frame %p loc->inode %p", frame, loc->inode);
  afr_local_t *local = calloc (1, sizeof (afr_local_t));
  struct list_head *list;
  gf_inode_child_t *gic;
  loc_t temploc;
  frame->local = local;
  LOCK_INIT (&frame->mutex);
  temploc.path = loc->path;
  local->inode = inode_ref (loc->inode);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  if (local->inode == NULL) 
    AFR_DEBUG_FMT (this, "inode is NULL :O");

  list  = local->inode->private;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      local->call_count++;
    }
  }
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      temploc.inode = inode_ref (gic->inode);
      STACK_WIND (frame,
		  afr_stat_cbk,
		  gic->xl,
		  gic->xl->fops->stat,
		  &temploc);
      inode_unref (temploc.inode);
    }
  }
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
  AFR_DEBUG_FMT(this, "loc->path %s", loc->path);
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
  AFR_DEBUG_FMT(this, "loc->path %s", loc->path);
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
      temploc.inode = inode_ref (gic->inode);
      STACK_WIND(frame,
		 afr_truncate_cbk,
		 gic->xl,
		 gic->xl->fops->truncate,
		 &temploc,
		 offset);
      inode_unref (temploc.inode);
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
  AFR_DEBUG_FMT (this, "loc->path %s", loc->path);
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
      temploc.inode = inode_ref (gic->inode);
      STACK_WIND(frame,
		 afr_utimens_cbk,
		 gic->xl,
		 gic->xl->fops->utimens,
		 &temploc,
		 tv);
      inode_unref (temploc.inode);
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
  AFR_DEBUG_FMT(this, "op_ret = %d fdchild = %p", op_ret, fdchild);
  afr_local_t *local = frame->local;
  fd_t *fd = local->fd;
  int32_t callcnt;
  call_frame_t *prev_frame = cookie;
  AFR_DEBUG_FMT (this, "local %p", local);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->mutex);
  if (op_ret >= 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (op_ret >= 0) {
    if (fd == NULL) {
      fd = fd_create (local->inode);
      local->fd = fd;
      inode_unref (local->inode);
    }
    dict_set (fd->ctx, prev_frame->this->name, data_from_static_ptr (fdchild));
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);
  if (callcnt == 0) {
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
  AFR_DEBUG_FMT(this, "loc->path = %s inode = %p", loc->path, loc->inode);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = loc->inode->private;
  loc_t temploc;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  AFR_DEBUG_FMT (this, "local %p", local);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->inode = inode_ref (loc->inode);
  temploc.path = loc->path;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      temploc.inode = inode_ref (gic->inode);
      STACK_WIND (frame,
		  afr_opendir_cbk,
		  gic->xl,
		  gic->xl->fops->opendir,
		  &temploc);
      inode_unref (temploc.inode);
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
  AFR_DEBUG_FMT(this, "loc->path %s loc->inode %p size %d", loc->path, loc->inode, size);
  struct list_head *list = loc->inode->private;
  gf_inode_child_t *gic;

  loc_t temploc;
  temploc.path = loc->path;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      break;
  }
  temploc.inode = inode_ref (gic->inode);

  STACK_WIND (frame,
              afr_readlink_cbk,
              gic->xl,
              gic->xl->fops->readlink,
              &temploc,
              size);
  inode_unref (temploc.inode);
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
  AFR_DEBUG_FMT (this, "op_ret = %d", op_ret);
  int32_t callcnt, tmp_count;
  dir_entry_t *trav, *prev, *tmp, *afr_entry;
  afr_local_t *local = frame->local;

  LOCK (&frame->mutex);
  {
    if (op_ret >= 0) {
      /* For all the successful calls, come inside this block */
      local->op_ret = op_ret;
      trav = entry->next;
      prev = entry;
      if (local->entry == NULL) {
	/* local->entry is NULL only for the first successful call. So, 
	 * take all the entries from that node. 
	 */
	afr_entry = calloc (1, sizeof (dir_entry_t));
	afr_entry->next = trav;
	
	while (trav->next) {
	  trav = trav->next;
	}
	local->entry = afr_entry;
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
	  {
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
	      continue;
	    }
	  }
	  prev = trav;
	  if (trav) trav = trav->next;
	}
	/* Append the 'entries' from this call at the end of the previously stored entry */
	local->last->next = entry->next;
	local->count += tmp_count;
	while (local->last->next)
	  local->last = local->last->next;
      }
      /* This makes child nodes to free only head, and all dir_entry_t structures are
       * kept reference at this level.
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

  if (callcnt == 0) {
    /* unwind the current frame with proper entries */
    frame->local = NULL;
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, local->entry, local->count);

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
    }
  }
  return 0;
}

static int32_t
afr_readdir (call_frame_t *frame,
	     xlator_t *this,
	     size_t size,
	     off_t offset,
	     fd_t *fd)
{
  AFR_DEBUG_FMT(this, "fd = %d", fd);
  gf_inode_child_t *gic;
  data_t *fdchild_data;
  fd_t *fdchild;
  struct list_head *list= fd->inode->private;
  afr_local_t *local = calloc (1, sizeof (afr_local_t));
  frame->local = local;
  local->op_ret = -1;
  local->op_ret = ENOENT;
  LOCK_INIT (&frame->mutex);
  list_for_each_entry (gic, list, clist) {
    if (gic->inode && dict_get (fd->ctx, gic->xl->name))
      local->call_count++;
  }
  list_for_each_entry (gic, list, clist) {
    if (gic->inode == NULL)
      continue;
    fdchild_data = dict_get (fd->ctx, gic->xl->name);
    if (fdchild_data == NULL)
      continue;
    fdchild = data_to_ptr (fdchild_data);
    STACK_WIND (frame, 
		afr_readdir_cbk,
		gic->xl,
		gic->xl->fops->readdir,
		size,
		offset,
		fdchild);
  }
  return 0;
}

static int32_t
afr_writedir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  afr_local_t *local = frame->local;
  int32_t callcnt;

  LOCK (&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (op_ret == 0)
    local->op_ret = 0;

  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno);
  }
  return 0;
}

static int32_t
afr_writedir (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t flags,
	      dir_entry_t *entries,
	      int32_t count)
{
  afr_local_t *local = calloc (1, sizeof (*local));
  gf_inode_child_t *gic;
  struct list_head *list;
  data_t *fdchild_data;
  fd_t *fdchild;
  frame->local = local;
  list = fd->inode->private;
  LOCK_INIT (&frame->mutex);
  list_for_each_entry (gic, list, clist) {
    if (dict_get (fd->ctx, gic->xl->name))
      local->call_count++;
  }

  list_for_each_entry (gic, list, clist) {
    if ((fdchild_data = dict_get (fd->ctx, gic->xl->name)) != NULL ) {
      fdchild = data_to_ptr (fdchild_data);
      STACK_WIND (frame,
		  afr_writedir_cbk,
		  gic->xl,
		  gic->xl->fops->writedir,
		  fdchild,
		  flags,
		  entries,
		  count);
    }
  }

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
      linode = inode_update (this->itable, NULL, NULL, &gic->stat);
      linode->private = list;
    }
    struct stat *statptr = local->op_ret == 0 ? &gic->stat : NULL;
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  linode,
		  statptr);
    if (linode)
      inode_unref (linode);
  }
  return 0;
}

static int32_t
afr_mkdir (call_frame_t *frame,
	   xlator_t *this,
	   const char *path,
	   mode_t mode)
{
  AFR_DEBUG_FMT(this, "path %s", path);
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
  AFR_DEBUG_FMT(this, "op_ret = %d", op_ret);
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
  AFR_DEBUG_FMT(this, "loc->path = %s loc->inode = %u",loc->path, loc->inode->ino );
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
      temploc.inode = inode_ref (gic->inode);
      STACK_WIND(frame,
		 afr_unlink_cbk,
		 gic->xl,
		 gic->xl->fops->unlink,
		 &temploc);
      inode_unref (temploc.inode);
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
      temploc.inode = inode_ref(gic->inode);
      STACK_WIND(frame,
		 afr_rmdir_cbk,
		 gic->xl,
		 gic->xl->fops->rmdir,
		 &temploc);
      inode_unref (temploc.inode);
    }
  }
  return 0;
}

static int32_t
afr_create_setxattr_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  LOCK(&frame->mutex);
  callcnt = --local->call_count;
  UNLOCK (&frame->mutex);

  if (callcnt == 0) {
    LOCK_DESTROY (&frame->mutex);
    AFR_DEBUG_FMT (this, "unwinding(), fd = %p inode = %p inode->ino = %u path %s", local->fd, local->inode, local->inode->ino, local->path);
    STACK_UNWIND (frame,
		  0,
		  0,
		  local->fd,
		  local->inode,
		  &local->inode->buf);
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
      fd = calloc (1, sizeof (*fd)); /* FIXME Not using fd_create */
      fd->ctx = get_new_dict();
      local->fd = fd;
      local->op_ret = 0;
      dict_set (fd->ctx, this->name, data_from_ptr(strdup(local->path)));
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

  if (callcnt == 0){
    if (local->op_ret == 0) {
      list_for_each_entry (gic, list, clist) {
	if (gic->inode)
	  break;
      }
      linode = inode_update (this->itable, NULL, NULL, &gic->stat);
      fd->inode = linode; /* inode already refed */
      list_add (&fd->inode_list, &linode->fds);
      linode->private = list;

      if (((afr_private_t*)this->private)->self_heal == 1) {
	local->inode = linode;
	local->fd = fd;
	list_for_each_entry (gic, list, clist) {
	  if (gic->inode)
	    local->call_count++;
	}
	loc_t temploc;
	temploc.path = local->path;
	dict_t *dict = get_new_dict();
	struct timeval tv;
	gettimeofday (&tv, NULL);
	uint32_t ctime = tv.tv_sec;
	dict_set (dict, "trusted.afr.createtime", data_from_uint32 (ctime));
	dict_set (dict, "trusted.afr.version", data_from_uint32(0));
	list_for_each_entry (gic, list, clist) {
	  if (gic->inode) {
	    temploc.inode = inode_ref (gic->inode);
	    STACK_WIND (frame,
			afr_create_setxattr_cbk,
			gic->xl,
			gic->xl->fops->setxattr,
			&temploc,
			dict,
			0);
	    inode_unref (temploc.inode);
	  }
	}
	dict_destroy (dict);
	return 0;
      }
    } else {
      /*FIXME free the list */
    }
    
    struct stat *statptr = local->op_ret == 0 ? &gic->stat : NULL;
    LOCK_DESTROY (&frame->mutex);
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
  AFR_DEBUG_FMT (this, "path = %s", path);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  int32_t num_copies = afr_get_num_copies (path, this);
  gf_inode_child_t *gic;
  struct list_head *list;
  xlator_list_t *trav = this->children;
  
  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  if (num_copies == 0)
    num_copies = 1;
  local->path = (char *)path;
  list = calloc (1, sizeof(*list));
  INIT_LIST_HEAD (list);
  local->list = list;
  while (trav) {
    gic = calloc (1, sizeof (*gic));
    gic->xl = trav->xlator;
    list_add_tail (&gic->clist, list);
    trav = trav->next;
  }

  afr_child_state_t *acs;
  list = ((afr_private_t*)this->private)->children;
  list_for_each_entry (acs, list, clist) {
    if (acs->state)
      local->call_count++;
    if (local->call_count == num_copies)
      break;
  }
  trav = this->children;
  list_for_each_entry (acs, list, clist) {
    if (acs->state == 0)
      continue;

    STACK_WIND (frame,
		afr_create_cbk,
		acs->xl,
		acs->xl->fops->create,
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
      linode = inode_update (this->itable, NULL, NULL, &gic->stat);
      linode->private = list;
    }
    struct stat *statptr = local->op_ret == 0 ? &gic->stat : NULL;
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  linode,
		  statptr);
    if (linode)
      inode_unref (linode);
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
      linode = inode_update (this->itable, NULL, NULL, &gic->stat);
      linode->private = list;
    }
    struct stat *statptr = local->op_ret == 0 ? &gic->stat : NULL;
    LOCK_DESTROY (&frame->mutex);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  linode,
		  statptr);
    if (linode)
      inode_unref (linode);
  }
  return 0;
}

static int32_t
afr_symlink (call_frame_t *frame,
	     xlator_t *this,
	     const char *linkname,
	     const char *newpath)
{
  AFR_DEBUG_FMT(this, "linkname %s newpath %s", linkname, newpath);
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
    struct stat stat;
    if (local->op_ret == 0)
      stat = gic->stat;
    stat.st_ino = local->inode->buf.st_ino;
    inode_unref (local->inode);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  &stat);
  }
  return 0;
}
/* FIXME: newloc inode can be valid */
static int32_t
afr_rename (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *oldloc,
	    loc_t *newloc)
{
  AFR_DEBUG_FMT(this, "oldloc->path %s newloc->path %s", oldloc->path, newloc->path);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list;
  loc_t temploc;

  LOCK_INIT (&frame->mutex);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->inode = inode_ref (oldloc->inode);
  temploc.path = oldloc->path;
  local->list = list = oldloc->inode->private;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      local->call_count++;
    }
  }
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      temploc.inode = inode_ref (gic->inode);
      STACK_WIND (frame,
		  afr_rename_cbk,
		  gic->xl,
		  gic->xl->fops->rename,
		  &temploc,
		  newloc);
      inode_unref (temploc.inode);
    }
  }
  return 0;
}

/* FIXME: see if the implementation is correct */
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
  inode_t *linode = NULL;
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
    if (gic->inode == NULL)
      gic->inode = inode_ref (inode);
    gic->stat = *stbuf;
  } else {
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
      linode = inode_update (this->itable, NULL, NULL, &gic->stat);
      linode->private = list;
    }
    struct stat *statptr = local->op_ret == 0 ? &gic->stat : NULL;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  linode,
		  statptr);
    if (linode)
      inode_unref (linode);
  }
  return 0;
}

static int32_t
afr_link (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *oldloc,
	  const char *newpath)
{
  AFR_DEBUG_FMT(this, "oldloc->path %s newpath %s", oldloc->path, newpath);
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
      temploc.inode = inode_ref(gic->inode);
      STACK_WIND (frame,
		  afr_link_cbk,
		  gic->xl,
		  gic->xl->fops->link,
		  &temploc,
		  newpath);
      inode_unref (temploc.inode);
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
      temploc.inode = inode_ref (gic->inode);
      STACK_WIND(frame,
		 afr_chmod_cbk,
		 gic->xl,
		 gic->xl->fops->chmod,
		 &temploc,
		 mode);
      inode_unref (temploc.inode);
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
      temploc.inode = inode_ref(gic->inode);
      STACK_WIND(frame,
		 afr_chown_cbk,
		 gic->xl,
		 gic->xl->fops->chown,
		 &temploc,
		 uid,
		 gid);
      inode_unref (temploc.inode);
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
  AFR_DEBUG_FMT(this, "op_ret = %d", op_ret);
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
  AFR_DEBUG_FMT(this, "fd = %p", fd);
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
  fd_destroy (fd);
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

int32_t
notify (xlator_t *this,
	int32_t event,
	void *data,
	...)
{
  AFR_DEBUG_FMT (this, "EVENT %d", event);
  afr_child_state_t *acs;
  afr_private_t *pvt = this->private;
  struct list_head *list = pvt->children;

  list_for_each_entry (acs, list, clist) {
    if (data == acs->xl) /* FIXME data is not always child */
      break;
  }
  switch (event) {
  case GF_EVENT_CHILD_UP:
    AFR_DEBUG_FMT (this, "CHILD_UP from %s", acs->xl->name);
    acs->state = 1;
    break;
  case GF_EVENT_CHILD_DOWN:
    AFR_DEBUG_FMT (this, "CHILD_DOWN from %s", acs->xl->name);
    acs->state = 0;
    break;
  }
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

/*
static void *(*old_free_hook)(void *, const void *);

static void
afr_free_hook (void *ptr, const void *caller)
{
  __free_hook = old_free_hook;
  memset (ptr, 255, malloc_usable_size(ptr));
  free (ptr);
  __free_hook = afr_free_hook;
  
}

static void
afr_init_hook (void)
{
  old_free_hook = __free_hook;
  __free_hook = afr_free_hook;
}

void (*__malloc_initialize_hook) (void) = afr_init_hook;
*/

int32_t 
init (xlator_t *this)
{
  afr_private_t *pvt = calloc (1, sizeof (afr_private_t));
  data_t *lock_node = dict_get (this->options, "lock-node");
  data_t *replicate = dict_get (this->options, "replicate");
  data_t *selfheal = dict_get (this->options, "self-heal");
  data_t *debug = dict_get (this->options, "debug");
  int32_t count = 0;
  xlator_list_t *trav = this->children;
  struct list_head *list;
  gf_inode_child_t *gic;
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
  
  this->itable = inode_table_new (lru_limit, this->name);


  if (this->itable == NULL)
    gf_log (this->name, GF_LOG_DEBUG, "inode_table_new() failed");

  list = calloc (1, sizeof (*list));
  INIT_LIST_HEAD (list);
  while (trav) {
    gic = calloc (1, sizeof (*gic));
    gic->xl = trav->xlator;
    gic->inode = gic->xl->itable->root;
    list_add (&gic->clist, list);
    trav = trav->next;
  }
  this->itable->root->isdir = 1;
  this->itable->root->private = list;
  trav = this->children;
  this->private = pvt;
  while (trav) {
    gf_log ("afr", GF_LOG_DEBUG, "xlator name is %s", trav->xlator->name);
    count++;
    trav = trav->next;
  }
  gf_log ("afr", GF_LOG_DEBUG, "child count %d", count);
  pvt->child_count = count;
  if (debug && strcmp(data_to_str(debug), "on") == 0) {
    gf_log ("afr", GF_LOG_DEBUG, "debug logs enabled");
    pvt->debug = 1;
  }
  pvt->self_heal = 1;
  if (selfheal && strcmp(data_to_str(selfheal), "off") == 0) {
    gf_log ("afr", GF_LOG_DEBUG, "self heal disabled");
    pvt->self_heal = 0;
  }
  if (lock_node) {
    trav = this->children;
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
    gf_log ("afr", GF_LOG_DEBUG, "afr->init: lock node not specified, defaulting to %s", this->children->xlator->name);
    pvt->lock_node = this->children->xlator;
  }
  pvt->children = calloc (1, sizeof (struct list_head));
  INIT_LIST_HEAD (pvt->children);
  trav = this->children;
  while (trav) {
    afr_child_state_t *acs = calloc (1, sizeof(afr_child_state_t));
    acs->xl = trav->xlator;
    acs->state = 0;
    list_add_tail (&acs->clist, pvt->children);
    trav = trav->next;
  }
  if(replicate)
    afr_parse_replicate (replicate->data, this);
  return 0;
}

void
fini(xlator_t *this)
{
  free (((afr_private_t *)this->private)->pattern_info_list);
  free (this->private);
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
  .writedir    = afr_writedir,
  .lookup_cbk  = afr_lookup_cbk
};

struct xlator_mops mops = {
  .stats = afr_stats,
  .lock = afr_lock,
  .unlock = afr_unlock,
};

