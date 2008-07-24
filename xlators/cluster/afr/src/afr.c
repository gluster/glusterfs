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

/*
 * TODO:
 * 1) Check the FIXMEs
 * 2) There are no known mem leaks, check once again
 */

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "afr.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"
#include "list.h"
#include "call-stub.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "compat.h"

#define BUF_SIZE 512

#define AFR_DEBUG_FMT(xl, format, args...) if(((afr_private_t*)(xl)->private)->debug) gf_log ((xl)->name, GF_LOG_DEBUG, "AFRDEBUG:" format, ##args);
#define AFR_DEBUG(xl) if(((afr_private_t*)xl->private)->debug) gf_log (xl->name, GF_LOG_DEBUG, "AFRDEBUG:");

#define AFR_ERRNO_DUP(child_errno, afr_errno, child_count) do {\
    child_errno = alloca(child_count);			       \
    ERR_ABORT (child_errno);				       \
    memcpy (child_errno, afr_errno, child_count);	       \
} while(0);

extern void afr_lookup_directory_selfheal (call_frame_t *);

loc_t*
afr_loc_dup(loc_t *loc)
{
  loc_t *loctmp;
  GF_BUG_ON (!loc);
  loctmp = calloc(1, sizeof(loc_t));
  ERR_ABORT (loctmp);
  loctmp->inode = loc->inode;
  loctmp->path = strdup (loc->path);
  return loctmp;
}

void
afr_loc_free(loc_t *loc)
{
  GF_BUG_ON (!loc);
  FREE (loc->path);
  FREE(loc);
}

inline void 
afr_free_ashptr (afr_selfheal_t *ashptr, int32_t child_count, int32_t latest)
{
  FREE (ashptr);
}

int32_t
afr_sync_ownership_permission_cbk(call_frame_t *frame,
				  void *cookie,
				  xlator_t *this,
				  int32_t op_ret,
				  int32_t op_errno,
				  struct stat *stbuf)
{
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;
  int32_t  callcnt, i, first = -1, latest = -1;
  struct stat *statptr = local->statptr;
  afr_selfheal_t *ashptr = local->ashptr;
  char *child_errno = NULL;
  inode_t *inoptr = local->loc->inode;
  dict_t *xattr;
  AFR_DEBUG (this);
  child_errno = data_to_ptr (dict_get(local->loc->inode->ctx, this->name));

  for (i = 0; i < child_count; i++)
    if (prev_frame->this == children[i])
      break;

  if (op_ret == 0) {
    GF_BUG_ON (!stbuf);
    statptr[i] = *stbuf;
  } else {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)",
	      local->loc->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }
  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    for (i = 0; i < child_count; i++) {
      if (child_errno[i] == 0) {
	if (first == -1) {
	  first = i;
	  latest = i;
	  continue;
	}
	if ((ashptr[i].ctime > ashptr[latest].ctime) ||
	    (ashptr[i].ctime == ashptr[latest].ctime && ashptr[i].version > ashptr[latest].version))
	  latest = i;
      }
    }
    if (first == -1) {
      GF_WARNING (this, "first == -1");
      first = latest = 0;
    }

    afr_loc_free(local->loc);
    afr_free_ashptr (local->ashptr, child_count, local->latest);
    if (local->ino)
      statptr[latest].st_ino = local->ino;
    else
      statptr[latest].st_ino = statptr[first].st_ino;
    xattr = local->latest_xattr;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inoptr,
		  &statptr[latest],
		  xattr);
    if (xattr)
      dict_unref (xattr);
    FREE (statptr);
  }
  return 0;
}

/*
 * afr_sync_ownership_permission - sync ownership and permission attributes
 * 
 * @frame: we are doing syncing in frame's context
 */
int32_t
afr_sync_ownership_permission (call_frame_t *frame)
{
  char *child_errno = NULL;
  afr_local_t *local = frame->local;
  inode_t *inode = local->loc->inode;
  afr_private_t *pvt = frame->this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;
  int32_t i, first = -1;
  int32_t latest = -1;   /* to keep track of the the child node, which contains the most recent entry */
  struct stat *statptr = local->statptr;
  afr_selfheal_t *ashptr = local->ashptr;
  dict_t *xattr;
  child_errno = data_to_ptr (dict_get(local->loc->inode->ctx, frame->this->name));

  /* krishna claims child_errno can't be null, but we are paranoid */
  GF_BUG_ON (!child_errno);

  /* we get the stat info with the latest ctime
   * ctime indicates the time when there was any modification to the
   * inode like permission, mode etc
   */
  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      if (latest == -1) {
	latest = i;
	continue;
      }
      if (statptr[i].st_ctime > statptr[latest].st_ctime)
	latest = i;
    }
  }

  AFR_DEBUG_FMT (frame->this, "latest %s uid %u gid %u %d", 
		 children[latest]->name, statptr[latest].st_uid, 
		 statptr[latest].st_gid, statptr[latest].st_mode);

  /* find out if there are any stat whose uid/gid/mode mismatch */
  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      if (statptr[latest].st_uid != statptr[i].st_uid || 
	  statptr[latest].st_gid != statptr[i].st_gid) {
	local->call_count++;
      }
      if (statptr[latest].st_mode != statptr[i].st_mode) {
	local->call_count++;
      }
    }
  }

  AFR_DEBUG_FMT (frame->this, "local->call_count %d", local->call_count);

  if (local->call_count) {
    local->stbuf = statptr[latest];
    /* in case there was any uid/gid/mode mismatch, we rectify it as root */
    for (i = 0; i < child_count; i++) {
      if (child_errno[i] == 0) {
	if (i == latest)
	  continue;
	if (statptr[latest].st_uid != statptr[i].st_uid || 
	    statptr[latest].st_gid != statptr[i].st_gid) {
	  GF_DEBUG (frame->this, "uid/gid mismatch, latest on %s, calling chown(%s, %u, %u) on %s",
		    children[latest]->name, local->loc->path, statptr[latest].st_uid, 
		    statptr[latest].st_gid, children[i]->name);

	  STACK_WIND (frame,
		      afr_sync_ownership_permission_cbk,
		      children[i],
		      children[i]->fops->chown,
		      local->loc,
		      statptr[latest].st_uid,
		      statptr[latest].st_gid);
	}

	if (statptr[latest].st_mode != statptr[i].st_mode) {
	  GF_DEBUG (frame->this, "mode mismatch, latest on %s, calling chmod(%s, 0%o) on %s", 
		    children[latest]->name, local->loc->path, statptr[latest].st_mode, 
		    children[i]->name);

	  STACK_WIND (frame,
		      afr_sync_ownership_permission_cbk,
		      children[i],
		      children[i]->fops->chmod,
		      local->loc,
		      statptr[latest].st_mode);
	}
      }
    }
    return 0;
  }
  /* we reach here means no self-heal is needed */

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      if (first == -1) {
	first = i;
	latest = i;
	continue;
      }
      if ((ashptr[i].ctime > ashptr[latest].ctime) ||
	  (ashptr[i].ctime == ashptr[latest].ctime && ashptr[i].version > ashptr[latest].version))
	latest = i;
    }
  }
  if (first == -1) {
    GF_WARNING (frame->this, "first == -1");
    first = latest = 0;
  }

  if (local->ino)
    statptr[latest].st_ino = local->ino;
  else
    statptr[latest].st_ino = statptr[first].st_ino;
  afr_loc_free(local->loc);
  afr_free_ashptr (local->ashptr, child_count, local->latest);
  xattr = local->latest_xattr;
  STACK_UNWIND (frame,
		local->op_ret,
		local->op_errno,
		inode,
		&statptr[latest],
		xattr);
  if (xattr)
    dict_unref (xattr);
  FREE (statptr);
  return 0;
}

int32_t
afr_lookup_unlock_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG_FMT (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
		 prev_frame->this->name, op_ret, op_errno, strerror(op_errno));

  if (local->rmelem_status) {
    loc_t *loc = local->loc;
    afr_selfheal_t *ashptr = local->ashptr;
    struct stat *statptr = local->statptr;
    afr_private_t *pvt = this->private;

    afr_loc_free (loc);
    afr_free_ashptr (ashptr, pvt->child_count, local->latest);
    FREE (statptr);
    if (local->latest_xattr)
      dict_unref (local->latest_xattr);
    STACK_UNWIND (frame, -1, EIO, local->loc->inode, NULL, NULL);
    return 0;
  }

  afr_sync_ownership_permission (frame);
  return 0;
}

int32_t
afr_lookup_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		inode_t *inode,
		struct stat *buf,
		dict_t *xattr);

int32_t
afr_lookup_lock_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
  afr_local_t *local = frame->local;
  afr_private_t *pvt = this->private;
  int32_t child_count = pvt->child_count;
  int32_t i;
  xlator_t **children = pvt->children;

  AFR_DEBUG_FMT (this, "op_ret=%d op_errno=%d", op_ret, op_errno);
  local->call_count = child_count;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;

  for (i = 0; i < child_count; i++) {
    STACK_WIND (frame,
		afr_lookup_cbk,
		children[i],
		children[i]->fops->lookup,
		local->loc,
		1);
  }
  return 0;
}

/*
 * afr_check_ctime_version
 *
 * @frame: call frame, this is the context in which we will try to complete the directory self-heal
 *
 */
void
afr_check_ctime_version (call_frame_t *frame)
{
  /*
   * if not a directory, call sync perm/ownership function
   * if it is a directory, compare the ctime/versions
   * if they are same, call sync perm/owenership function
   * if they differ, lock the path
   * in lock_cbk, get dirents from the latest and the outdated children
   * note down all the elements (files/dirs/links) that need to be deleted from the outdated children
   * call remove_elem on the elements that need to be removed.
   * in the cbk, update the ctime/version on the outdated children
   * in the cbk call sync perm/ownership function.
   */
  /* we need to increment the 'version' count whenever there is change in contents
   * of a directory, which can happen during the fops mentioned on next line:
   * create(), unlink(), rmdir(), mkdir(), symlink(), link(), rename(), mknod()
   */
  char *child_errno = NULL;
  int32_t latest = 0, differ = 0, first = 0, i;
  afr_local_t *local = frame->local;
  afr_private_t *pvt = frame->this->private;
  int32_t child_count = pvt->child_count;
  struct stat *statptr = local->statptr;
  afr_selfheal_t *ashptr = local->ashptr;
  xlator_t **children = pvt->children;
  char *state = pvt->state;

  AFR_DEBUG (frame->this);

  /* child_errno cant be NULL */
  child_errno = data_to_ptr (dict_get(local->loc->inode->ctx, frame->this->name)); 
  
  GF_BUG_ON (!child_errno);

  /* 'i' will be the index to the first child node which returned the fop with complete success */
  for (i = 0; i < child_count; i++)
    if (child_errno[i] == 0)
      break;
  
  latest = first = i; /* this is valid else we wouldnt have got called */

  if (S_ISDIR(statptr[i].st_mode) == 0) {
    /* in case this is not directory, we shouldn't call directory selfheal code
     */
    afr_sync_ownership_permission (frame);
    return;
  }

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      if (ashptr[i].ctime != ashptr[latest].ctime || 
	  ashptr[i].version != ashptr[latest].version) {
	differ = 1;
      }
      if (ashptr[i].ctime > ashptr[latest].ctime) {
	latest = i;
      } else if (ashptr[i].ctime == ashptr[latest].ctime && 
		 ashptr[i].version > ashptr[latest].version) {
	latest = i;
      }
    }
  }

  if (differ == 0) {
    if (local->lock_node) {
      char *lock_path = NULL;
      asprintf (&lock_path, "/%s%s", local->lock_node->name, local->loc->path);
      STACK_WIND (frame,
		  afr_lookup_unlock_cbk,
		  local->lock_node,
		  local->lock_node->mops->unlock,
		  lock_path);
      FREE (lock_path);
    } else
      afr_sync_ownership_permission (frame);
    return;
  }
  for (i = 0; i < child_count; i++) {
    if (pvt->state[i])
      break;
  }
  if (i == child_count) {
    if (local->lock_node) {
      char *lock_path = NULL;
      asprintf (&lock_path, "/%s%s", local->lock_node->name, local->loc->path);
      STACK_WIND (frame,
		  afr_lookup_unlock_cbk,
		  local->lock_node,
		  local->lock_node->mops->unlock,
		  lock_path);
      FREE (lock_path);
    } else
      afr_sync_ownership_permission (frame);
    return;
  }

  if (local->lock_node) {
    local->fd = fd_create (local->loc->inode);

    for (i = 0; i < child_count; i++) {
      if (child_errno[i] != 0)
	continue;
      if (i == latest) {
	continue;
      }
      if (ashptr[latest].ctime > ashptr[i].ctime) {
	ashptr[i].repair = 1;
	continue;
      }
      if (ashptr[latest].ctime == ashptr[i].ctime && ashptr[latest].version > ashptr[i].version) {
	ashptr[i].repair = 1;
      }
    }
    local->latest = latest;
    afr_lookup_directory_selfheal (frame);

  } else {
    char *lock_path = NULL;
    for (i = 0; i < child_count; i++) {
      if (state[i])
	break;
    }
    if (i == child_count) {
      GF_ERROR (frame->this, "(path=%s) no child up for locking, returning EIO", local->loc->path);
      afr_loc_free(local->loc);
      afr_free_ashptr (local->ashptr, child_count, local->latest);
      FREE (statptr);

      STACK_UNWIND (frame,
		    -1,
		    EIO,
		    NULL,
		    NULL,
		    NULL);
      return;
    }

    local->lock_node = children[i];

    asprintf (&lock_path, "/%s%s", local->lock_node->name, local->loc->path);
    AFR_DEBUG_FMT (frame->this, "locking (%s on %s)", lock_path, local->lock_node->name);
    /* lets lock the first alive node */
    STACK_WIND (frame,
		afr_lookup_lock_cbk,
		children[i],
		children[i]->mops->lock,
		lock_path);
    FREE (lock_path);
  }
  return;
}

int32_t
afr_lookup_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		inode_t *inode,
		struct stat *buf,
		dict_t *xattr)
{
  char *child_errno = NULL;
  data_t *errno_data = NULL;
  int32_t callcnt, i, latest = -1, first = -1;
  afr_local_t *local = frame->local;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;
  call_frame_t *prev_frame = cookie;
  struct stat *statptr = local->statptr;
  afr_selfheal_t *ashptr = local->ashptr;

  AFR_DEBUG_FMT(this, "op_ret = %d op_errno = %d, inode = %p, returned from %s", 
		op_ret, op_errno, inode, prev_frame->this->name);

  if (op_ret != 0 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  /* 'i' will be the index indicating us, which child node has returned to us */
  for (i = 0; i < child_count; i++)
    if (children[i] == prev_frame->this)
      break;

  /* child_errno is an array of one bytes, each byte corresponding to the errno returned by a child node
   * child_errno array is initialized during the first succesful return call from a child.
   */
  errno_data = dict_get (local->loc->inode->ctx, this->name);
  if (errno_data)
    child_errno = data_to_ptr (errno_data);
  if (child_errno == NULL) {
    /* first time lookup and success */
    child_errno = calloc (child_count, sizeof (char));	
    ERR_ABORT (child_errno);
    dict_set (local->loc->inode->ctx, this->name, data_from_dynptr (child_errno, child_count));
  }

  /* child_errno[i] is either 0 indicating success or op_errno indicating failure */
  if (op_ret == 0) {
    data_t *ctime_data, *version_data;
    local->op_ret = 0;
    if (inode && list_empty (&inode->fds)) {
      child_errno[i] = 0;
    }

    GF_BUG_ON (!inode);
    GF_BUG_ON (!buf);

    statptr[i] = *buf;
    if (pvt->self_heal && xattr) {
      /* self heal is 'on' and we also recieved the xattr that we requested from our children.
       * store ctime and version returned by each child */
      ctime_data = dict_get (xattr, GLUSTERFS_CREATETIME);
      if (ctime_data) {
	ashptr[i].ctime = data_to_uint32 (ctime_data);
      }

      version_data = dict_get (xattr, GLUSTERFS_VERSION);
      if (version_data) {
	ashptr[i].version = data_to_uint32 (version_data);
      }

      if (ashptr[i].ctime > local->latest_ctime || 
	  (ashptr[i].ctime == local->latest_ctime && ashptr[i].version > local->latest_version)) {
	local->latest_ctime = ashptr[i].ctime;
	local->latest_version = ashptr[i].version;
	if (local->latest_xattr)
	  dict_unref (local->latest_xattr);
	local->latest_xattr = dict_ref (xattr);
      }

      AFR_DEBUG_FMT (this, "child %s ctime %d version %d", 
		     prev_frame->this->name, ashptr[i].ctime, ashptr[i].version);
    }
  } else if (inode && list_empty (&inode->fds)) {
    /* either self-heal is turned 'off' or we didn't recieve xattr, which we requested for */
    child_errno[i] = op_errno;
  }

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    if (local->op_ret == 0) {
      if (pvt->self_heal) {
	for (i = 0; i < child_count; i++) {
	  if (child_errno[i] == 0)
	    break;
	}
	if (i < child_count) {
	  afr_check_ctime_version (frame);
	  return 0;
	}
      }
    }
    /* child_errno will be freed when dict is destroyed */

    if (local->op_ret == 0) {
      for (i = 0; i < child_count; i++) {
	if (child_errno[i] == 0) {
	  if (latest == -1) {
	    /* first will be the first valid stat, latest will be the stat with latest mtime */
	    first = i;
	    latest = i;
	    continue;
	  }
	  if (ashptr[i].ctime > ashptr[latest].ctime ||
	      (ashptr[i].ctime == ashptr[latest].ctime && ashptr[i].version > ashptr[latest].version))
	    latest = i;
	}
      }
    }
    if (first == -1) {
      first = latest = 0;
    } else {
      /* FIXME: we preserve the ino num (whatever that was got during the initial lookup(?) */
      if (local->ino)
	statptr[latest].st_ino = local->ino;
      else
	statptr[latest].st_ino = statptr[first].st_ino;
    }
    afr_loc_free(local->loc);
    afr_free_ashptr (local->ashptr, child_count, local->latest);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inode,
		  &statptr[latest],
		  xattr);
    FREE (statptr);
  }
  return 0;
}

int32_t
afr_lookup (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t need_xattr)
{
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  local = calloc (1, sizeof (*local));
  ERR_ABORT (local);
  
  AFR_DEBUG_FMT (this, "loc->path = %s loc->inode = %p", loc->path, loc->inode);

  frame->root->uid = 0; /* selfheal happens as root */
  frame->root->gid = 0;

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->loc = afr_loc_dup (loc);

  /* statptr[] array is used for selfheal */
  local->statptr = calloc (child_count, sizeof (struct stat));
  ERR_ABORT (local->statptr);
  local->ashptr  = calloc (child_count, sizeof (afr_selfheal_t));
  ERR_ABORT (local->ashptr);
  local->call_count = child_count;
  local->ino = loc->ino;

  for (i = 0; i < child_count; i ++) {
    /* request for extended attributes if self heal is 'on' */
    int32_t need_xattr = pvt->self_heal;
    STACK_WIND (frame,
		afr_lookup_cbk,
		children[i],
		children[i]->fops->lookup,
		loc,
		need_xattr);
  }
  return 0;
}

int32_t
afr_incver_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;

  if (op_ret > local->op_ret)
    local->op_ret = op_ret;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

int32_t
afr_incver (call_frame_t *frame,
	    xlator_t *this,
	    const char *path,
	    fd_t *fd)
{
  afr_local_t *local = NULL;
  afr_private_t *pvt = frame->this->private;
  int32_t child_count = pvt->child_count, i;
  xlator_t **children = pvt->children;
  char *state = pvt->state;

  local = calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);

  frame->local = local;
  local->op_ret = -1;
  for (i = 0; i < child_count; i++) {
    if (state[i])
      local->call_count++;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "(fd=%x path=%s) all children are down, returning ENOTCONN", fd, path ? path : "");
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (state[i]) {
      STACK_WIND (frame,
		  afr_incver_cbk,
		  children[i],
		  children[i]->fops->incver,
		  path,
		  fd);
    }
  }
  return 0;
}

int32_t
afr_close_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno);

int32_t
afr_incver_close (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd)
{
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i, cnt;
  afr_local_t *local = frame->local;
  afrfd_t *afrfdp = data_to_ptr (dict_get (fd->ctx, this->name));

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdsuccess[i])
      local->call_count++;
  }

  cnt = local->call_count;

  if (cnt == 0) {
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdsuccess[i]) {
      STACK_WIND (frame,
		  afr_close_cbk,
		  children[i],
		  children[i]->fops->close,
		  fd);
      if (--cnt == 0)
	break;
    }
  }

  return 0;
}

int32_t
afr_incver_internal_unlock_cbk (call_frame_t *frame,
				void *cookie,
				xlator_t *this,
				int32_t op_ret,
				int32_t op_errno)
{
  afr_local_t *local = frame->local;
  afr_local_t *orig_local = NULL;

  /* will not be set in case of afr_incver_internal_dir */
  if (local->orig_frame)
    orig_local = local->orig_frame->local;

  /* in case it was an incver from close() call */
  if (orig_local && orig_local->close) {
    afr_incver_close (local->orig_frame, this, local->fd);
    local->orig_frame = NULL;
  }

  /* will be set in afr_incver_internal_[fd|inode] */
  if (local->orig_frame)
    STACK_UNWIND (local->orig_frame, orig_local->op_ret, orig_local->op_errno, &orig_local->stbuf);

  FREE (local->path);
  STACK_DESTROY (frame->root);
  return 0;
}

int32_t
afr_incver_internal_incver_cbk (call_frame_t *frame,
				void *cookie,
				xlator_t *this,
				int32_t op_ret,
				int32_t op_errno)
{
  afr_local_t *local = frame->local;
  int32_t callcnt;

  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (op_ret == -1) {
    /* FIXME */
  }

  if (callcnt == 0) {
    char *lock_path = NULL;
    asprintf (&lock_path, "/%s%s", local->lock_node->name, local->path);
    STACK_WIND (frame,
		afr_incver_internal_unlock_cbk,
		local->lock_node,
		local->lock_node->mops->unlock,
		lock_path);
    FREE (lock_path);
  }
  return 0;
}

int32_t
afr_incver_internal_lock_cbk (call_frame_t *frame,
			      void *cookie,
			      xlator_t *this,
			      int32_t op_ret,
			      int32_t op_errno)
{
  afr_local_t *local = frame->local;
  afr_private_t *pvt = frame->this->private;
  int32_t child_count = pvt->child_count, i;
  xlator_t **children = pvt->children;
  char *state = pvt->state;
  afrfd_t *afrfdp;

  if (op_ret == -1) {
    afr_incver_internal_unlock_cbk (frame, NULL, this, -1, op_errno);
    return 0;
  }

  if (local->fd) {
    afrfdp = data_to_ptr (dict_get (local->fd->ctx, this->name));
    if (afrfdp == NULL) {
      char *lock_path = NULL;
      call_frame_t *orig_frame = local->orig_frame;
      afr_local_t *orig_local  = orig_frame->local;
      orig_local->op_ret = -1;
      orig_local->op_ret = EIO;
      asprintf (&lock_path, "/%s%s", local->lock_node->name, local->path);
      GF_ERROR (this, "afrfdp is NULL, for %s", local->path);
      STACK_WIND (frame,
		  afr_incver_internal_unlock_cbk,
		  local->lock_node,
		  local->lock_node->mops->unlock,
		  lock_path);
      FREE (lock_path);
      return 0;
    }
    state = afrfdp->fdstate;
  }

  for (i = 0; i < child_count; i++) {
    if (state[i])
      local->call_count++;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "(path=%s) none of the subvols are up for locking", local->path);
    char *lock_path = NULL;
    asprintf (&lock_path, "/%s%s", local->lock_node->name, local->path);
    GF_ERROR (this, "afrfdp is NULL, for %s", local->path);
    STACK_WIND (frame,
		afr_incver_internal_unlock_cbk,
		local->lock_node,
		local->lock_node->mops->unlock,
		lock_path);
    FREE (lock_path);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (state[i]) {
      STACK_WIND (frame,
		  afr_incver_internal_incver_cbk,
		  children[i],
		  children[i]->fops->incver,
		  local->path,
		  local->fd);
    }
  }
  return 0;
}

int32_t
afr_incver_internal_fd (call_frame_t *frame,
			xlator_t *this,
			fd_t *fd)
{
  call_frame_t *incver_frame;
  afr_local_t *local;
  afr_private_t *pvt = frame->this->private;
  int32_t child_count = pvt->child_count, i;
  xlator_t **children = pvt->children;
  char *state = pvt->state;
  char *lock_path = NULL;
  afrfd_t *afrfdp = data_to_ptr (dict_get (fd->ctx, this->name));

  if (pvt->self_heal == 0)
    return 0;

  for (i = 0; i < child_count; i++) {
    if (state[i])
      break;
  }
  if (i == child_count) {
    GF_ERROR (this, "none of the subvols are up for locking");
    local = frame->local;
    /* FIXME in case this is done on close() need to free afrfdp */
    if (local->orig_frame)
      STACK_UNWIND (local->orig_frame, -1, EIO, NULL);
    return 0;
  }

  local = calloc (1, sizeof (afr_local_t));
  incver_frame = copy_frame (frame);
  incver_frame->local = local;

  local->lock_node = children[i];
  local->path = strdup (afrfdp->path);
  local->fd = fd; /* will be NULL for incver of dir */
  local->orig_frame = frame;

  asprintf (&lock_path, "/%s%s", local->lock_node->name, local->path);
  STACK_WIND (incver_frame,
	      afr_incver_internal_lock_cbk,
	      local->lock_node,
	      local->lock_node->mops->lock,
	      lock_path);
  FREE (lock_path);

  return 0;
}

int32_t
afr_incver_internal_inode (call_frame_t *frame,
			   xlator_t *this,
			   inode_t *inode,
			   char *path)
{
  call_frame_t *incver_frame;
  afr_local_t *local;
  afr_private_t *pvt = frame->this->private;
  int32_t child_count = pvt->child_count, i;
  xlator_t **children = pvt->children;
  char *state = pvt->state;
  char *lock_path = NULL;

  if (pvt->self_heal == 0)
    return 0;

  for (i = 0; i < child_count; i++) {
    if (state[i])
      break;
  }

  if (i == child_count) {
    GF_ERROR (this, "none of the subvols are up for locking");
    local = frame->local;
    if (local->orig_frame)
      STACK_UNWIND (local->orig_frame, -1, EIO, NULL);
    return 0;
  }

  local = calloc (1, sizeof (afr_local_t));
  incver_frame = copy_frame (frame);
  incver_frame->local = local;

  local->lock_node = children[i];
  local->path = strdup (path);
  local->orig_frame = frame;

  asprintf (&lock_path, "/%s%s", local->lock_node->name, local->path);
  STACK_WIND (incver_frame,
	      afr_incver_internal_lock_cbk,
	      local->lock_node,
	      local->lock_node->mops->lock,
	      lock_path);
  FREE (lock_path);

  return 0;
}

int32_t
afr_incver_internal_dir (call_frame_t *frame,
			 xlator_t *this,
			 const char *path)
{
  call_frame_t *incver_frame;
  afr_local_t *local;
  afr_private_t *pvt = frame->this->private;
  int32_t child_count = pvt->child_count, i, call_count = 0;
  xlator_t **children = pvt->children;
  char *state = pvt->state;
  char *lock_path = NULL;

  if (pvt->self_heal == 0)
    return 0;

  for (i = 0; i < child_count; i++) {
    if (state[i])
      call_count++;
  }
  /* we wont incver if all children are down or if all children are up */
  if (call_count == 0 || call_count == child_count) {
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (state[i])
      break;
  }
  if (i == child_count) {
    GF_ERROR (this, "none of the subvols are up for locking");
    return 0;
  }

  local = calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);
  incver_frame = copy_frame (frame);
  incver_frame->local = local;

  local->lock_node = children[i];
  local->path = dirname (strdup(path));

  asprintf (&lock_path, "/%s%s", local->lock_node->name, local->path);
  STACK_WIND (incver_frame,
	      afr_incver_internal_lock_cbk,
	      local->lock_node,
	      local->lock_node->mops->lock,
	      lock_path);
  FREE (lock_path);

  return 0;
}

/* no need to do anything in forget, as the mem will be just free'd in dict_destroy(inode->ctx) */

int32_t
afr_forget (call_frame_t *frame,
	    xlator_t *this,
	    inode_t *inode)
{
  return 0;
}

int32_t
afr_setxattr_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  afr_local_t *local = (afr_local_t *) frame->local;
  call_frame_t *prev_frame = cookie;
  int32_t callcnt;

  AFR_DEBUG(this);

  if (op_ret != 0  && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  if (op_ret == 0) {
    local->op_ret = op_ret;
  } else {
    if (op_errno != ENOTSUP)
      GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
		local->loc->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    if (local->loc)
      afr_loc_free (local->loc);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

int32_t
afr_setxattr (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      dict_t *dict,
	      int32_t flags)
{
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  char *afr_errno = NULL;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  char *child_errno = NULL;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);

  AFR_DEBUG_FMT (this, "loc->path = %s", loc->path);

  if (loc->inode && loc->inode->ctx)
    {
      afr_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));
      AFR_ERRNO_DUP(child_errno, afr_errno, child_count);
      local->loc = afr_loc_dup (loc);
    }
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  
  if (afr_errno)
    {
      for (i = 0; i < child_count; i++) {
	if (child_errno[i] == 0)
	  ++local->call_count;
      }
      
      if (local->call_count == 0) {
	GF_ERROR (this, "child_errno[] is not 0, returning ENOTCONN");
	STACK_UNWIND (frame, -1, ENOTCONN);
	return 0;
      }
      
      for (i = 0; i < child_count; i++) {
	if (child_errno[i] == 0)
	  STACK_WIND(frame,
		     afr_setxattr_cbk,
		     children[i],
		     children[i]->fops->setxattr,
		     loc,
		     dict,
		     flags);
      }
    }
  else
    {
      local->call_count = child_count;
      for (i = 0; i < child_count; i++) 
	{
	  STACK_WIND(frame, afr_setxattr_cbk,
		     children[i], children[i]->fops->setxattr,
		     loc, dict, flags);
	}
    }
  return 0;
}

int32_t
afr_getxattr_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  dict_t *dict)
{
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG(this);

  if (op_ret >= 0) {
    GF_BUG_ON (!dict);
  } else if (op_errno != ENODATA) {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      frame->local, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  STACK_UNWIND (frame, op_ret, op_errno, dict);
  return 0;
}

int32_t
afr_getxattr (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      const char *name)
{
  afr_private_t *pvt = this->private;
  char *afr_errno = NULL;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  char *child_errno = NULL;

  AFR_DEBUG_FMT (this, "loc->path = %s", loc->path);

  afr_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  AFR_ERRNO_DUP(child_errno, afr_errno, child_count);

  frame->local = strdup (loc->path);
  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0)
      break;
  }

  if (i == child_count) {
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }
  /* send getxattr command to the first child where the file is available */
  STACK_WIND (frame,
	      afr_getxattr_cbk,
	      children[i],
	      children[i]->fops->getxattr,
	      loc,
	      name);
  return 0;
}

int32_t
afr_removexattr_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG(this);
  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  if (op_ret == 0) {
    local->op_ret = op_ret;
  } else {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    afr_loc_free (local->loc);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

int32_t
afr_removexattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 const char *name)
{
  char *afr_errno = NULL;
  char *child_errno = NULL;
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  
  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);
  


  AFR_DEBUG(this);

  afr_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  AFR_ERRNO_DUP(child_errno, afr_errno, child_count);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->loc = afr_loc_dup (loc);

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0)
      ++local->call_count;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "child_errno[] is not 0, returning ENOTCONN");
    STACK_UNWIND (frame,
		  -1,
		  ENOTCONN);
    return 0;
  }
  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0)
      STACK_WIND (frame,
		  afr_removexattr_cbk,
		  children[i],
		  children[i]->fops->removexattr,
		  loc,
		  name);
  }
  return 0;
}

int32_t
afr_selfheal_unlock_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
  afr_local_t *local = frame->local;
  afr_selfheal_t *ash, *ashtemp;
  call_frame_t *prev_frame = cookie;
  struct list_head *list = local->list;

  if (op_ret == -1) {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
    call_frame_t *open_frame = local->orig_frame;
    afr_local_t *open_local = open_frame->local;
    open_local->sh_return_error = 1;
  }

  AFR_DEBUG_FMT (this, "call_resume()");
  call_resume (local->stub);
  /* clean up after resume */
  FREE (local->loc->path);
  FREE (local->loc);

  if (local->fd) {
    afrfd_t *afrfdp = data_to_ptr (dict_get(local->fd->ctx, this->name));
    FREE (afrfdp->fdstate);
    /* afrfdp->path is not allocated */
    FREE (afrfdp);
    dict_destroy (local->fd->ctx);
    FREE (local->fd);
  }
  list_for_each_entry_safe (ash, ashtemp, list, clist) {
    list_del (&ash->clist);
    if (ash->dict)
      dict_unref (ash->dict);
    FREE (ash);
  }
  FREE (list);
  STACK_DESTROY (frame->root);
  return 0;
}

int32_t
afr_selfheal_nosync_close_cbk (call_frame_t *frame,
			       void *cookie,
			       xlator_t *this,
			       int32_t op_ret,
			       int32_t op_errno);

/* we call afr_error_during_sync if there was any error during read/write during syncing. */

int32_t
afr_error_during_sync (call_frame_t *frame)
{
  int32_t cnt;
  afr_local_t *local = frame->local;
  afr_private_t *pvt = frame->this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afrfd_t *afrfdp = NULL;
  call_frame_t *open_frame = local->orig_frame;
  afr_local_t *open_local = open_frame->local;

  GF_ERROR (frame->this, "error during self-heal");

  afrfdp = data_to_ptr(dict_get(local->fd->ctx, frame->this->name));
  open_local->sh_return_error = 1;

  local->call_count = 0;
  for (i = 0; i < child_count; i++) {
    if(afrfdp->fdstate[i])
      local->call_count++;
  }
  /* local->call_count cant be 0 because we'll have atleast source's fd in dict */
  GF_BUG_ON (!local->call_count);
  cnt = local->call_count;
  for (i = 0; i < child_count; i++) {
    if(afrfdp->fdstate[i]){
      STACK_WIND (frame,
		  afr_selfheal_nosync_close_cbk,
		  children[i],
		  children[i]->fops->close,
		  local->fd);
      /* in case this is the last WIND, list will be free'd before next iteration
       * causing segfault. so we use a cnt counter
       */
      if (--cnt == 0)
	break;
    }
  }

  return 0;
}


int32_t
afr_selfheal_setxattr_cbk (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG_FMT (this, "op_ret = %d from client %s", op_ret, prev_frame->this->name);

  if (op_ret == -1) {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
    /* since we would have already called close, we wont use afr_error_during_sync */
    call_frame_t *open_frame = local->orig_frame;
    afr_local_t *open_local = open_frame->local;
    open_local->sh_return_error = 1;
  }

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    char *lock_path = NULL;
    asprintf (&lock_path, "/%s%s", local->lock_node->name, local->loc->path);
    STACK_WIND (frame,
		afr_selfheal_unlock_cbk,
		local->lock_node,
		local->lock_node->mops->unlock,
		lock_path);
    FREE (lock_path);
  }
  return 0;
}

int32_t
afr_selfheal_utimens_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  struct stat *stat)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;

  if (op_ret == -1) {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
    /* since we would have already called close, we wont use afr_error_during_sync */
    call_frame_t *open_frame = local->orig_frame;
    afr_local_t *open_local = open_frame->local;
    open_local->sh_return_error = 1;
  }

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    char *lock_path = NULL;
    asprintf (&lock_path, "/%s%s", local->lock_node->name, local->loc->path);
    STACK_WIND (frame,
		afr_selfheal_unlock_cbk,
		local->lock_node,
		local->lock_node->mops->unlock,
		lock_path);
    FREE (lock_path);
  }
  return 0;
}

/* FIXME handle the situation when one of the close fails */

int32_t
afr_selfheal_close_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
  int32_t cnt;
  int32_t callcnt;
  struct list_head *list;
  afr_selfheal_t *ash;
  afr_local_t *local = frame->local;

  AFR_DEBUG (this);

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    if (local->source->ctime == 0) { /* if source didnt have any ctime xattr */
      struct timeval tv;             /* if it was copied in the backend */
      int32_t ctime;
      char dict_ctime[100];
      gettimeofday (&tv, NULL);
      ctime = tv.tv_sec;
      sprintf (dict_ctime, "%u", ctime);
      dict_set (local->source->dict, GLUSTERFS_CREATETIME, 
		bin_to_data (dict_ctime, strlen (dict_ctime)));
    }
    list = local->list;
    list_for_each_entry (ash, list, clist) {
      if (ash->inode && (ash->repair || ash->version == 1)) {
	/* version 1 means there are no attrs, possibly it was copied
	 * direcly in the backend
	 */
	local->call_count++; /* for setxattr */
	local->call_count++; /* for utimens */
      }
    }
    cnt = local->call_count;

    list_for_each_entry (ash, list, clist) {
      struct timespec ts[2];
      ts[0].tv_sec = local->source->stat.st_atime;
      ts[0].tv_nsec = 0;
      ts[1].tv_sec = local->source->stat.st_mtime;
      ts[1].tv_nsec = 0;
      if (ash->inode && (ash->repair || ash->version == 1)) {
	AFR_DEBUG_FMT (this, "setxattr() on %s version %u ctime %u", 
		       ash->xl->name, local->source->version, local->source->ctime);
	STACK_WIND (frame,
		    afr_selfheal_setxattr_cbk,
		    ash->xl,
		    ash->xl->fops->setxattr,
		    local->loc,
		    local->source->dict,
		    0);
	STACK_WIND (frame,
		    afr_selfheal_utimens_cbk,
		    ash->xl,
		    ash->xl->fops->utimens,
		    local->loc,
		    ts);
	--cnt;              /* for setxattr */
	if (--cnt == 0)     /* for utimens */
	  break;
      }
    }
  }
  return 0;
}

int32_t
afr_selfheal_sync_file (call_frame_t *frame,
			xlator_t *this);

int32_t
afr_selfheal_sync_file_writev_cbk (call_frame_t *frame,
				   void *cookie,
				   xlator_t *this,
				   int32_t op_ret,
				   int32_t op_errno,
				   struct stat *stat)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG_FMT (this, "op_ret = %d", op_ret);

  if (op_ret == -1 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  if (op_ret >= 0)
    local->op_ret = op_ret;

  if (op_ret == -1) {
    /* even if one write fails, we will return open with error, need to see if
     * we can improve on this behaviour.
     * We should wait for all cbks before calling afr_error_during_sync()
     */
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
    local->sh_return_error = 1;
  }

  LOCK(&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK(&frame->lock);

  if (callcnt == 0) {
    if (local->sh_return_error) {
      /* if there was an error during one of the writes */
      afr_error_during_sync (frame);
    } else {
      local->offset = local->offset + op_ret;
      afr_selfheal_sync_file (frame, this);
    }
  }
  return 0;
}

int32_t
afr_selfheal_sync_file_readv_cbk (call_frame_t *frame,
				  void *cookie,
				  xlator_t *this,
				  int32_t op_ret,
				  int32_t op_errno,
				  struct iovec *vector,
				  int32_t count,
				  struct stat *stat)
{
  int32_t cnt;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afrfd_t *afrfdp = NULL; 

  AFR_DEBUG_FMT (this, "op_ret = %d", op_ret);

  afrfdp = data_to_ptr (dict_get (local->fd->ctx, this->name));

  for (i = 0; i < child_count; i++){
    if (afrfdp->fdstate[i])
      local->call_count++;
  }

  if (op_ret == 0) {
    /* EOF reached */
    AFR_DEBUG_FMT (this, "EOF reached");
    cnt = local->call_count;
    for (i = 0; i < child_count; i++){
      if (afrfdp->fdstate[i]) {
	STACK_WIND (frame,
		    afr_selfheal_close_cbk,
		    children[i],
		    children[i]->fops->close,
		    local->fd);
	if (--cnt == 0)
	  break;
      }
    }
  } else if (op_ret > 0) {
    local->call_count--; /* we dont write on source */
    local->op_ret = -1;
    local->op_errno = ENOTCONN;
    cnt = local->call_count;
    for (i = 0; i < child_count; i++) {
      if (children[i] == local->source->xl)
	continue;
      if (afrfdp->fdstate[i]) {
	AFR_DEBUG_FMT (this, "write call on %s", children[i]->name);
	STACK_WIND (frame,
		    afr_selfheal_sync_file_writev_cbk,
		    children[i],
		    children[i]->fops->writev,
		    local->fd,
		    vector,
		    count,
		    local->offset);
	if (--cnt == 0)
	  break;
      }
    }
  } else {
    /* error during read */
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
    afr_error_during_sync(frame);
  }
  return 0;
}

int32_t
afr_selfheal_sync_file (call_frame_t *frame,
			xlator_t *this)
{
  afr_local_t *local = frame->local;
  size_t readbytes = 128*1024;

  AFR_DEBUG_FMT (this, "reading from offset %u", local->offset);

  STACK_WIND (frame,
	      afr_selfheal_sync_file_readv_cbk,
	      local->source->xl,
	      local->source->xl->fops->readv,
	      local->fd,
	      readbytes,
	      local->offset);

  return 0;
}

int32_t
afr_selfheal_nosync_close_cbk (call_frame_t *frame,
			       void *cookie,
			       xlator_t *this,
			       int32_t op_ret,
			       int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;

  AFR_DEBUG(this);

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    char *lock_path = NULL;
    AFR_DEBUG_FMT(this, "calling unlock on local->loc->path %s", local->loc->path);
    asprintf (&lock_path, "/%s%s", local->lock_node->name, local->loc->path);
    STACK_WIND (frame,
		afr_selfheal_unlock_cbk,
		local->lock_node,
		local->lock_node->mops->unlock,
		lock_path);
    FREE (lock_path);
  }
  return 0;
}

int32_t
afr_selfheal_open_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       fd_t *fd)
{
  int32_t callcnt = 0;
  afrfd_t *afrfdp = NULL;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  AFR_DEBUG_FMT (this, "op_ret = %d from %s", op_ret, prev_frame->this->name);
  
  afrfdp = data_to_ptr (dict_get (local->fd->ctx, this->name));

  if (op_ret >= 0) {
    GF_BUG_ON (!local->fd);
    for (i = 0; i < child_count; i++)
      if (prev_frame->this == children[i])
	break;
    afrfdp->fdstate[i] = 1;
  } else {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    int32_t src_open = 0, sync_file_cnt = 0;
    for (i = 0; i < child_count; i++) {
      if (afrfdp->fdstate[i]) {
	sync_file_cnt++;
	if (children[i] == local->source->xl)
	  src_open = 1;
      }
    }
    if (src_open  && (sync_file_cnt >= 2)) {
      /* source open success + atleast a file to sync */
      afr_selfheal_sync_file (frame, this);
    } else {
      local->call_count = sync_file_cnt;
      for (i = 0; i < child_count; i++) {
	if (afrfdp->fdstate[i])
	  STACK_WIND (frame,
		      afr_selfheal_nosync_close_cbk,
		      children[i],
		      children[i]->fops->close,
		      local->fd);
      }
    }
  }
  return 0;
}

int32_t
afr_selfheal_stat_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct stat *stat)
{
  int32_t cnt = 0;
  afr_local_t *local = frame->local;
  struct list_head *list = local->list;
  afr_selfheal_t *ash, *source = local->source;

  if (op_ret == 0) {
    local->source->stat = *stat;
  } else {
    char *lock_path = NULL;
    call_frame_t *open_frame = local->orig_frame;
    afr_local_t *open_local = open_frame->local;

    open_local->sh_return_error = 1;

    asprintf (&lock_path, "/%s%s", local->lock_node->name, local->loc->path);
    GF_ERROR (this, "stat() on latest file failed (errno=%d), calling unlock on %s", 
	      op_errno, lock_path);

    STACK_WIND (frame,
		afr_selfheal_unlock_cbk,
		local->lock_node,
		local->lock_node->mops->unlock,
		lock_path);
    FREE (lock_path);
    return 0;
  }

  cnt = local->call_count;
  list_for_each_entry (ash, list, clist) {
    int32_t flags;
    if (ash == source || ash->repair) {
      if (ash == source)
	flags = O_RDONLY;
      else
	flags = O_RDWR | O_TRUNC;
      AFR_DEBUG_FMT (this, "open() on %s", ash->xl->name);
      STACK_WIND (frame,
		  afr_selfheal_open_cbk,
		  ash->xl,
		  ash->xl->fops->open,
		  local->loc,
		  flags,
		  local->fd);
      if (--cnt == 0)
	break;
      continue;
    }
  }
  return 0;
}


/* TODO: crappy code, clean this function */

int32_t
afr_selfheal_getxattr_cbk (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno,
			   dict_t *dict)
{
  int32_t callcnt = 0;
  afrfd_t *afrfdp = NULL;
  afr_selfheal_t *ash = NULL;
  afr_local_t *local = frame->local;
  struct list_head *list = local->list;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  int32_t child_count = pvt->child_count;

  list_for_each_entry (ash, list, clist) {
    if (prev_frame->this == ash->xl)
      break;
  }

  if (op_ret >= 0) {
    if (dict){
      ash->dict = dict_ref (dict);
      data_t *version_data = dict_get (dict, GLUSTERFS_VERSION);
      if (version_data) {
	/* version_data->data is NULL terminated bin data*/
	ash->version = data_to_uint32 (version_data); 
      } else {
	AFR_DEBUG_FMT (this, "version attribute was not found on %s, defaulting to 1", 
		       prev_frame->this->name)
	ash->version = 1;
	dict_set(ash->dict, GLUSTERFS_VERSION, bin_to_data("1", 1));
      }
      data_t *ctime_data = dict_get (dict, GLUSTERFS_CREATETIME);
      if (ctime_data) {
	/* ctime_data->data is NULL terminated bin data */
	ash->ctime = data_to_uint32 (ctime_data);
      } else {
	ash->ctime = 0;
      }

      AFR_DEBUG_FMT (this, "op_ret = %d version = %u ctime = %u from %s", 
		     op_ret, ash->version, ash->ctime, prev_frame->this->name);

      ash->op_errno = 0;
    }
  } else {
    AFR_DEBUG_FMT (this, "op_ret = %d from %s", op_ret, prev_frame->this->name);

    if (op_errno != ENODATA) {
      GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
		local->loc->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
    }

    ash->op_errno = op_errno;
    if (op_errno == ENODATA) {
      ash->dict = dict_ref (dict);
      ash->version = 1;
      dict_set(ash->dict, GLUSTERFS_VERSION, bin_to_data("1", 1));
      ash->ctime = 0;
    }
  }

  LOCK(&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    uint32_t latest = 0;
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
	if (ash->op_errno != 0) {
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
	char *lock_path = NULL;
	asprintf (&lock_path, "/%s%s", local->lock_node->name, local->loc->path);
	AFR_DEBUG_FMT (this, "latest version is 0? or the file does not have verion attribute?");
	STACK_WIND (frame,
		    afr_selfheal_unlock_cbk,
		    local->lock_node,
		    local->lock_node->mops->unlock,
		    lock_path);
	FREE (lock_path);
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

	if (ash->op_errno != 0) 
	  continue;             

	if (latest > ash->version) {
	  ash->repair = 1;
	  local->call_count++;
	  AFR_DEBUG_FMT (this, "%s version %d outdated, latest=%d, %d", 
			 ash->xl->name, ash->version, latest, local->call_count);
	}
      }

      if (local->call_count == 1) {
	char *lock_path = NULL;
	asprintf (&lock_path, "/%s%s", local->lock_node->name, local->loc->path);
	/* call_count would have got incremented for source */
	AFR_DEBUG_FMT (this, "self heal NOT needed");
	STACK_WIND (frame,
		    afr_selfheal_unlock_cbk,
		    local->lock_node,
		    local->lock_node->mops->unlock,
		    lock_path);
	FREE (lock_path);
	return 0;
      }
    }

    GF_DEBUG (this, "self-heal needed (path=%s source=%s)", 
	      local->loc->path, source->xl->name);

    local->source = source;
    local->fd = calloc (1, sizeof(fd_t));
    ERR_ABORT (local->fd);
    local->fd->ctx = get_new_dict();
    afrfdp = calloc (1, sizeof (*afrfdp));
    ERR_ABORT (afrfdp);
    afrfdp->fdstate = calloc (child_count, sizeof (char));
    ERR_ABORT (afrfdp->fdstate);
    dict_set (local->fd->ctx, this->name, data_from_static_ptr (afrfdp));
    local->fd->inode = local->loc->inode;
    cnt = local->call_count;

    STACK_WIND (frame,
		afr_selfheal_stat_cbk,
		source->xl,
		source->xl->fops->stat,
		local->loc);
  }

  return 0;
}

int32_t
afr_selfheal_lock_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
  int32_t totcnt = 0;
  afr_selfheal_t *ash, *ashtemp;
  afr_local_t *local = frame->local;
  struct list_head *list = local->list;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG_FMT(this, "op_ret = %d", op_ret, op_errno);

  if (op_ret == -1) {
    AFR_DEBUG_FMT (this, "locking failed!");
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));

    call_frame_t *open_frame = local->orig_frame;
    afr_local_t *open_local = open_frame->local;
    open_local->sh_return_error = 1;
    call_resume(local->stub);
    FREE (local->loc->path);
    FREE (local->loc);
    if (local->fd) {
      afrfd_t *afrfdp;
      afrfdp = data_to_ptr (dict_get(local->fd->ctx, this->name));
      FREE(afrfdp->fdstate);
      /* afrfdp is freed in dict_destroy */
      dict_destroy (local->fd->ctx);
      FREE (local->fd);
    }
    list_for_each_entry_safe (ash, ashtemp, list, clist) {
      list_del (&ash->clist);
      if (ash->dict)
	dict_unref (ash->dict);
      FREE (ash);
    }
    FREE (list);
    STACK_DESTROY (frame->root);
    return 0;
  }

  list_for_each_entry (ash, list, clist) {
    if(ash->inode)
      local->call_count++;
  }

  totcnt = local->call_count;
  list_for_each_entry (ash, list, clist) {
    if (ash->inode) {
      AFR_DEBUG_FMT (this, "calling getxattr on %s", ash->xl->name);
      STACK_WIND (frame,
		  afr_selfheal_getxattr_cbk,
		  ash->xl,
		  ash->xl->fops->getxattr,
		  local->loc,
		  NULL);
      if (--totcnt == 0)
	break;
    }
  }

  return 0;
}


int32_t
afr_selfheal (call_frame_t *frame,
	      xlator_t *this,
	      call_stub_t *stub,
	      loc_t *loc)
{
  int32_t i, lock_node = 0;
  char *child_errno = NULL;
  char *lock_path = NULL;
  afr_selfheal_t *ash;
  call_frame_t *shframe = copy_frame (frame);
  afr_local_t *shlocal = NULL;
  struct list_head *list = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  shlocal = calloc (1, sizeof (afr_local_t));
  ERR_ABORT (shlocal);
  list = calloc (1, sizeof (*list));
  ERR_ABORT (list);



  AFR_DEBUG(this);

  child_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  for (i = 0; i < child_count; i++) {
    if (pvt->state[i])
      break;
  }
  if (i == child_count) {
    GF_ERROR (this, "none of the children are up for locking, returning EIO");

    FREE (list);
    FREE (shlocal);
    STACK_DESTROY (shframe->root); /* copy_frame() allocates some memory, free it */
    STACK_UNWIND (frame, -1, EIO, NULL);
    return 0;
  }

  lock_node = i;
  INIT_LIST_HEAD (list);
  shframe->local = shlocal;
  shlocal->list = list;
  shlocal->loc = calloc (1, sizeof (loc_t));
  ERR_ABORT (shlocal->loc);
  shlocal->loc->path = strdup (loc->path);
  shlocal->loc->inode = loc->inode;
  shlocal->orig_frame = frame;
  shlocal->stub = stub;

  ((afr_local_t*)frame->local)->shcalled = 1;

  shframe->root->uid = 0;
  shframe->root->gid = 0;
  for (i = 0; i < child_count; i++) {
    ash = calloc (1, sizeof (*ash));
    ERR_ABORT (ash);
    ash->xl = children[i];
    if (child_errno[i] == 0)
      ash->inode = (void*)1;
    ash->op_errno = child_errno[i];
    list_add_tail (&ash->clist, list);
  }

  AFR_DEBUG_FMT (this, "locking the node %s", children[lock_node]->name);

  shlocal->lock_node = children[lock_node];
  asprintf (&lock_path, "/%s%s", children[lock_node]->name, loc->path);
  STACK_WIND (shframe,
	      afr_selfheal_lock_cbk,
	      children[lock_node],
	      children[lock_node]->mops->lock,
	      lock_path);
  FREE (lock_path);

  return 0;
}


int32_t
afr_open_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      fd_t *fd)
{
  int32_t callcnt, i;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  AFR_DEBUG(this);

  if (op_ret == -1 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  if (op_ret >= 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
  }
  if (op_ret == -1) {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  LOCK (&frame->lock);
  {
    if (op_ret >= 0) {
      GF_BUG_ON (!fd);
      afrfd_t *afrfdp;
      data_t *afrfdp_data;
      afrfdp_data = dict_get (fd->ctx, this->name);
      if (afrfdp_data == NULL) {
	/* first successful open_cbk */
	afrfdp = calloc (1, sizeof (afrfd_t));
	ERR_ABORT (afrfdp);
	afrfdp->fdstate = calloc (child_count, sizeof (char));
	ERR_ABORT (afrfdp->fdstate);
	afrfdp->fdsuccess = calloc (child_count, sizeof (char));
	ERR_ABORT (afrfdp->fdsuccess);
	/* path will be used during close to increment version */
	afrfdp->path = strdup (local->loc->path);
	dict_set (fd->ctx, this->name, data_from_static_ptr (afrfdp));
	/* we use the path here just for debugging */
	if (local->flags & O_TRUNC)
	  afrfdp->write = 1;
      } else {
	afrfdp = data_to_ptr (afrfdp_data);
      }

      for (i = 0; i < child_count; i++) {
	if (children[i] == prev_frame->this)
	  break;
      }

      /* 1 indicates open success, 0 indicates failure */
      afrfdp->fdstate[i] = 1;
      afrfdp->fdsuccess[i] = 1;
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    afrfd_t *afrfdp = data_to_ptr (dict_get(local->fd->ctx, this->name));
    if (local->op_ret != -1) {
      if (pvt->read_node == -1 || afrfdp->fdstate[pvt->read_node] == 0) {
	int32_t rchild = 0, alive_children = 0;
	for (i = 0; i < child_count; i++) {
	  if (afrfdp->fdstate[i]) {
	    /* op_ret != -1 implies atleast one increment */
	    alive_children++;
	  }
	}
	rchild = local->loc->inode->ino % alive_children;
	/* read schedule among alive children */
	for (i = 0; i < child_count; i++) {
	  if (afrfdp->fdstate[i] == 1) {
	    if (rchild == 0)
	      break;
	    rchild--;
	  }
	}
	afrfdp->rchild = i;
      } else {
	afrfdp->rchild = pvt->read_node;
      }
    }

    afr_loc_free (local->loc);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, fd);
  }
  return 0;
}

int32_t
afr_open (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  int32_t flags,
	  fd_t *fd)
{
  int32_t i = 0;
  afr_local_t *local; 
  char *afr_errno = NULL;
  char *child_errno = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  AFR_DEBUG_FMT (this, "loc->path = %s loc->inode = %p", loc->path, loc->inode);

  afr_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  if (afr_errno == NULL) {
    if (loc->inode->ctx == NULL) {
      GF_ERROR (this, "inode->ctx is NULL, returning EIO");
    }
    GF_ERROR (this, "afr_errno is NULL, returning EIO");
    STACK_UNWIND (frame, -1, EIO, fd);
    return 0;
  }

  AFR_ERRNO_DUP(child_errno, afr_errno, child_count);

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0)
      break;
  }

  if (i == child_count) {
    GF_ERROR (this, "%s: child_errno[] is not 0, returning ENOTCONN", loc->path);
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }

  if (frame->local == NULL) {
    frame->local = (void *) calloc (1, sizeof (afr_local_t));
    ERR_ABORT (frame->local);
  }
  local = frame->local;
  
  if (((afr_private_t *) this->private)->self_heal) {
    AFR_DEBUG_FMT (this, "self heal enabled");
    if (local->sh_return_error) {
      GF_ERROR (this, "(path=%s) self heal failed, returning EIO", loc->path);
      STACK_UNWIND (frame, -1, EIO, fd);
      return 0;
    }
    if (local->shcalled == 0) {
      call_stub_t *stub = fop_open_stub (frame, afr_open, loc, flags, fd);
      AFR_DEBUG_FMT (this, "self heal checking...");
      afr_selfheal (frame, this, stub, loc);
      return 0;
    }
    AFR_DEBUG_FMT (this, "self heal already called");
  } else {
    AFR_DEBUG_FMT (this, "self heal disabled");
  }

  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->flags = flags;
  local->loc = afr_loc_dup (loc);
  local->fd = fd;
  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0)
      ++local->call_count;
  }

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0)
      STACK_WIND (frame,
		  afr_open_cbk,
		  children[i],
		  children[i]->fops->open,
		  loc,
		  flags,
		  fd);
  }

  return 0;
}

int32_t
afr_readv_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct iovec *vector,
	       int32_t count,
	       struct stat *stat)
{
  afr_local_t *local = (afr_local_t *)frame->local;

  AFR_DEBUG(this);

  if (op_ret == -1) {
    call_frame_t *prev_frame = cookie;
    afrfd_t *afrfdp = local->afrfdp;
    if (op_errno == ENOTCONN || op_errno == EBADFD) {
      int i=0;
      afr_private_t *pvt = this->private;
      xlator_t **children = pvt->children;

      for (i = 0; i < pvt->child_count; i++)
	if (((call_frame_t *)cookie)->this == children[i])
	  break;

      afrfdp->fdstate[i] = 0;
      afrfdp->rchild = -1;
      for (i = 0; i < pvt->child_count; i++) {
	if (afrfdp->fdstate[i])
	  break;
      }
      GF_DEBUG (this, "reading from child %d", i);
      if (i < pvt->child_count) {
      	STACK_WIND (frame,
		    afr_readv_cbk,
		    children[i],
		    children[i]->fops->readv,
		    local->fd,
		    local->size,
		    local->offset);
	return 0;
      }
    }

    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      afrfdp->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  STACK_UNWIND (frame, op_ret, op_errno, vector, count, stat);
  return 0;
}


int32_t
afr_readv (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   size_t size,
	   off_t offset)
{
  int32_t i = 0;
  afrfd_t *afrfdp = NULL;
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  afrfdp = data_to_ptr (dict_get (fd->ctx, this->name));

  AFR_DEBUG_FMT(this, "fd %p", fd);

  if (afrfdp == NULL) {
    GF_ERROR (this, "afrfdp is NULL, returning EBADFD");
    STACK_UNWIND (frame, -1, EBADFD, NULL, 0, NULL);
    return 0;
  }

  local = frame->local = calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);
  local->afrfdp = afrfdp;
  local->offset = offset;
  local->size = size;
  local->fd = fd;

  i = afrfdp->rchild;
  if (i == -1 || afrfdp->fdstate[i] == 0) {
    for (i = 0; i < child_count; i++) {
      if (afrfdp->fdstate[i] && pvt->state[i])
	break;
    }
  }
  if (i == child_count) {
    STACK_UNWIND (frame, -1, ENOTCONN, NULL, 0, NULL);
  } else {
    STACK_WIND (frame,
		afr_readv_cbk,
		children[i],
		children[i]->fops->readv,
		fd,
		size,
		offset);
  }

  return 0;
}

/* FIXME if one write fails, we should not increment version on 
 * the other subvols
 */

int32_t
afr_writev_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *stat)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  int32_t child_count = pvt->child_count, i;
  xlator_t **children = pvt->children;

  AFR_DEBUG_FMT(this, "op_ret %d op_errno %d", op_ret, op_errno);

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    if (op_ret == -1 && op_errno != ENOTCONN) {
      local->op_errno = op_errno;
    }

    if (op_ret != -1) {
      local->op_ret = op_ret;
      for (i = 0; i < child_count; i++) {
	if (children[i] == prev_frame->this) {
	  if (i < local->stat_child) {
	    local->stbuf = *stat;
	    local->stat_child = i;
	  }
	}
      }
    }
  }
  UNLOCK (&frame->lock);


  if (op_ret == -1) {
    afrfd_t *afrfdp = NULL;

    afrfdp = data_to_ptr (dict_get (local->fd->ctx, this->name));
    for (i = 0; i < child_count; i++)
      if (prev_frame->this == children[i])
	break;

    afrfdp->fdstate[i] = 0;

    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      afrfdp->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  } 

  if (callcnt == 0) {
    if (local->op_ret != -1) {
      local->stbuf.st_ino = local->ino;
    }
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

int32_t
afr_writev (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    struct iovec *vector,
	    int32_t count,
	    off_t offset)
{
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afrfd_t *afrfdp = NULL;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);


  AFR_DEBUG_FMT(this, "fd %p", fd);

  afrfdp = data_to_ptr (dict_get (fd->ctx, this->name));
  if (afrfdp == NULL) {
    FREE (local);
    GF_ERROR (this, "afrfdp is NULL, returning EBADFD");
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i])
      ++local->call_count;
  }

  if (local->call_count == 0) {
    FREE (local);
    GF_ERROR (this, "afrfdp->fdstate[] is 0, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }

  frame->local = local;
 
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->fd = fd;
  local->ino = fd->inode->ino;
  local->stat_child = child_count;

  afrfdp->write = 1;

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i]) {
      STACK_WIND (frame,
		  afr_writev_cbk,
		  children[i],
		  children[i]->fops->writev,
		  fd,
		  vector,
		  count,
		  offset);
    }
  }

  return 0;
}


int32_t
afr_ftruncate_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *stbuf)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  if (op_ret == -1) {
    afrfd_t *afrfdp = data_to_ptr (dict_get (local->fd->ctx, this->name));
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      afrfdp->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }
  if (op_ret == 0)
    GF_BUG_ON (!stbuf);

  LOCK (&frame->lock);
  {
    if (op_ret == 0 && local->op_ret == -1) {
      local->op_ret = op_ret;
      local->stbuf = *stbuf;
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    if (local->op_ret == 0) {
      afr_incver_internal_fd (frame,
			      this,
			      local->fd);
    } else
      STACK_UNWIND (frame, local->op_ret, local->op_errno, NULL);
  }
  return 0;
}


int32_t
afr_ftruncate (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       off_t offset)
{
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afrfd_t *afrfdp = NULL;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);

  AFR_DEBUG_FMT(this, "fd %p", fd);

  afrfdp = data_to_ptr (dict_get (fd->ctx, this->name));
  if (afrfdp == NULL) {
    free (local);
    GF_ERROR (this, "afrfdp is NULL, returning EBADFD");
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->fd = fd;
  afrfdp->write = 1;
  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i])
      ++local->call_count;
  }
  if (local->call_count == 0) {
    GF_ERROR (this, "afrfdp->fdstate[] is 0, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }

  for ( i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i]) {
      STACK_WIND(frame,
		 afr_ftruncate_cbk,
		 children[i],
		 children[i]->fops->ftruncate,
		 fd,
		 offset);
    }
  }

  return 0;
}

int32_t
afr_fstat_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *stbuf)
{

  AFR_DEBUG(this);

  if (op_ret == -1) {
    afrfd_t *afrfdp = frame->local;
    call_frame_t *prev_frame = cookie;

    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      afrfdp->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  frame->local = NULL; /* so that STACK_UNWIND does not try to free */
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

int32_t
afr_fstat (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afrfd_t *afrfdp = NULL;

  AFR_DEBUG(this);

  afrfdp = data_to_ptr (dict_get (fd->ctx, this->name));
  if (afrfdp == NULL) {
    GF_ERROR (this, "afrfdp is NULL, returning EBADFD");
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }

  frame->local = afrfdp;
  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i])
      break;
  }

  if (i == child_count) {
    GF_ERROR (this, "afrfdp->fdstate[i] is 0, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }

  STACK_WIND (frame,
	      afr_fstat_cbk,
	      children[i],
	      children[i]->fops->fstat,
	      fd);

  return 0;
}

int32_t
afr_flush_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  if (op_ret == -1) {
    afrfd_t *afrfdp = data_to_ptr (dict_get (local->fd->ctx, this->name));
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      afrfdp->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  if (op_ret == 0) {
    local->op_ret = op_ret;
  }

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);
  
  if (callcnt == 0) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}

int32_t
afr_flush (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afrfd_t *afrfdp = NULL;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);
  
  AFR_DEBUG_FMT(this, "fd %p", fd);

  afrfdp = data_to_ptr (dict_get (fd->ctx, this->name));
  if (afrfdp == NULL) {
    FREE (local);
    GF_ERROR (this, "afrfdp is NULL, returning EBADFD");
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->fd = fd;

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i])
      ++local->call_count;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "afrfdp->fdstate[] is 0, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i]) {
      STACK_WIND(frame,
		 afr_flush_cbk,
		 children[i],
		 children[i]->fops->flush,
		 fd);
    }
  }

  return 0;
}

int32_t
afr_close_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;

  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  if (op_ret == -1) {
    call_frame_t *prev_frame = cookie;
    afrfd_t *afrfdp = data_to_ptr (dict_get(local->fd->ctx, this->name));
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      afrfdp->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  LOCK (&frame->lock);
  {
    if (op_ret == 0 && local->op_ret == -1) {
      local->op_ret = op_ret;
      local->op_errno = op_errno;
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    afrfd_t *afrfdp = data_to_ptr (dict_get(local->fd->ctx, this->name));
    FREE (afrfdp->fdstate);
    FREE (afrfdp->fdsuccess);
    FREE (afrfdp->path);
    FREE (afrfdp);

    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}

int32_t
afr_close (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  int32_t cnt = 0;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afr_local_t *local = NULL;
  afrfd_t *afrfdp = data_to_ptr (dict_get (fd->ctx, this->name));
  
  local = calloc (1, sizeof(*local));
  ERR_ABORT (local);


  if (afrfdp == NULL) {
    free (local);
    GF_ERROR (this, "afrfdp is NULL, returning EBADFD");
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  AFR_DEBUG_FMT (this, "close on %s fd %p", afrfdp->path, fd);

  frame->local = local;
  local->fd = fd;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;

  if (((afr_private_t*) this->private)->self_heal && 
      (afrfdp->write || afrfdp->create)) {
    AFR_DEBUG_FMT (this, "self heal enabled, increasing the version count");
    local->close = 1;
    afr_incver_internal_fd (frame, this, fd);
    return 0;
  }

  AFR_DEBUG_FMT (this, "self heal disabled or write was not done or fdstate[] is 0");
  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdsuccess[i])
      local->call_count++;
  }

  cnt = local->call_count;

  if (cnt == 0) {
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdsuccess[i]) {
      STACK_WIND (frame,
		  afr_close_cbk,
		  children[i],
		  children[i]->fops->close,
		  fd);
      if (--cnt == 0)
	break;
    }
  }

  return 0;
}

int32_t
afr_fsync_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  LOCK (&frame->lock);
  {
    if (op_ret == 0 && local->op_ret == -1) {
      local->op_ret = op_ret;
    }

    if (op_ret == -1) {
      afrfd_t *afrfdp = data_to_ptr (dict_get(local->fd->ctx, this->name));
      GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
		afrfdp->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
    }
    
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}

int32_t
afr_fsync (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   int32_t datasync)
{
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afrfd_t *afrfdp = data_to_ptr (dict_get(fd->ctx, this->name));
  
  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);

  AFR_DEBUG_FMT(this, "fd %p", fd);

  if (afrfdp == NULL) {
    free (local);
    GF_ERROR (this, "afrfdp is NULL, returning EBADFD");
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->fd = fd;

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i])
      ++local->call_count;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "afrfdp->fdstate[] is 0, returning ENOTCONN");
    STACK_UNWIND (frame,
		  -1,
		  ENOTCONN);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i]) {
      STACK_WIND(frame,
		 afr_fsync_cbk,
		 children[i],
		 children[i]->fops->fsync,
		 fd,
		 datasync);
    }
  }

  return 0;
}

int32_t
afr_lk_cbk (call_frame_t *frame,
	    void *cookie,
	    xlator_t *this,
	    int32_t op_ret,
	    int32_t op_errno,
	    struct flock *lock)
{
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  if (op_ret == -1 && op_errno != ENOSYS) {
    afrfd_t *afrfdp = data_to_ptr (dict_get(local->fd->ctx, this->name));
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      afrfdp->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  if (op_ret == 0 && local->op_ret == -1) {
    local->lock = *lock;
    local->op_ret = 0;
  }

  i = local->child + 1;
  for (; i < child_count; i++) {
    afrfd_t *afrfdp = data_to_ptr (dict_get(local->fd->ctx, this->name));
    if (afrfdp->fdstate[i])
      break;
  }
  local->child = i;

  if ((local->child == child_count) || (op_ret == -1 && op_errno != ENOTCONN && local->op_ret == -1)) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->lock);
  } else {
    STACK_WIND (frame,
		afr_lk_cbk,
		children[local->child],
		children[local->child]->fops->lk,
		local->fd,
		local->flags,
		&local->lockp);
  }

  return 0;
}


int32_t
afr_lk (call_frame_t *frame,
	xlator_t *this,
	fd_t *fd,
	int32_t cmd,
	struct flock *lock)
{
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afrfd_t *afrfdp = data_to_ptr (dict_get(fd->ctx, this->name));
  
  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);


  AFR_DEBUG_FMT(this, "fd %p", fd);

  if (afrfdp == NULL) {
    free (local);
    GF_ERROR (this, "afrfdp is NULL, returning EBADFD");
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->fd = fd;
  local->flags = cmd; /* use flags just to save memory */
  local->lockp = *lock;

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i])
      break;
  }

  if (i == child_count) {
    GF_ERROR (this, "afrfdp->fdstate[] is 0, returning ENOTCONN");
    STACK_UNWIND (frame,
		  -1,
		  ENOTCONN,
		  NULL);
    return 0;
  }

  local->child = i;
  STACK_WIND(frame,
	     afr_lk_cbk,
	     children[i],
	     children[i]->fops->lk,
	     fd,
	     cmd,
	     lock);

  return 0;
}

int32_t
afr_stat_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct stat *stat)
{
  call_frame_t *prev_frame = cookie;
  int32_t callcnt;
  afr_local_t *local = frame->local;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  AFR_DEBUG_FMT(this, "frame %p op_ret %d", frame, op_ret);

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  if (op_ret == 0) {
    local->op_ret = op_ret;
    /* we will return stat info from the first successful child */
    for (i = 0; i < child_count; i++) {
      if (children[i] == prev_frame->this) {
	if (i < local->stat_child) {
	  local->stbuf = *stat;
	  local->stat_child = i;
	}
      }
    }
  }

  if (local->ino)
    local->stbuf.st_ino = local->ino;

  if (callcnt == 0)
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);

  return 0;
}

int32_t
afr_stat (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc)
{
  char *child_errno = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afr_local_t *local = calloc (1, sizeof (*local));
  ERR_ABORT (local);

  AFR_DEBUG_FMT(this, "frame %p loc->inode %p", frame, loc->inode);

  child_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->ino = loc->ino;
  local->stat_child = child_count;

  for(i = 0; i < child_count; i++) {
    if (child_errno[i] == 0)
      local->call_count++;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "child_errno[] is not 0, returning ENOTCONN");
    STACK_UNWIND (frame,
		  -1,
		  ENOTCONN,
		  NULL);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0)
      STACK_WIND (frame,
		  afr_stat_cbk,
		  children[i],
		  children[i]->fops->stat,
		  loc);
  }

  return 0;
}

int32_t
afr_statfs_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct statvfs *statvfs)
{
  afr_statfs_local_t *local = frame->local;
  int32_t callcnt = 0;
  call_frame_t *prev_frame = cookie;

  if (op_ret == -1 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  LOCK (&frame->lock);
  {
    if (op_ret == 0) {
      local->op_ret = op_ret;
      /* we will return stat info from the first successful child */
      if (!local->statvfs.f_bfree)
	local->statvfs = *statvfs;
      else if (local->statvfs.f_bfree > statvfs->f_bfree) 
	local->statvfs = *statvfs;
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0)
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->statvfs);

  return 0;
}

int32_t
afr_statfs (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i = 0;
  afr_statfs_local_t *local;

  local = calloc(1, sizeof(*local));
  ERR_ABORT (local);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->call_count = child_count;
  local->stat_child = child_count;
  for (i=0; i < child_count; i++) {
    STACK_WIND (frame,
		afr_statfs_cbk,
		children[i],
		children[i]->fops->statfs,
		loc);
  }

  return 0;
}


int32_t
afr_truncate_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *stbuf)
{
  int32_t i, callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  LOCK (&frame->lock);
  {
    if (op_ret == 0) {
      local->op_ret = op_ret;
      /* we will return stat info from the first successful child */
      for (i = 0; i < child_count; i++) {
	if (children[i] == prev_frame->this) {
	  if (i < local->stat_child) {
	    local->stbuf = *stbuf;
	    local->stat_child = i;
	  }
	}
      }
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    char *path = local->path;
    if (local->op_ret == 0)
      afr_incver_internal_inode (frame, this, local->inode, (char *) local->path);
    else
      STACK_UNWIND (frame, local->op_ret, local->op_errno, NULL);
    FREE (path);
  }

  return 0;
}

/* FIXME increase the version count */

int32_t
afr_truncate (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      off_t offset)
{
  int32_t i = 0;
  char *child_errno = NULL;
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);

  AFR_DEBUG_FMT(this, "loc->path %s", loc->path);

  child_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->stat_child = child_count;
  local->inode = loc->inode;
  local->path = strdup (loc->path);

  for (i = 0; i  < child_count; i++) {
    if (child_errno[i] == 0) {
      ++local->call_count;
    }
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "child_errno[] is not 0, returning ENOTCONN");
    STACK_UNWIND (frame,
		  -1,
		  ENOTCONN,
		  NULL);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      STACK_WIND(frame,
		 afr_truncate_cbk,
		 children[i],
		 children[i]->fops->truncate,
		 loc,
		 offset);
    }
  }

  return 0;
}

int32_t
afr_utimens_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *stbuf)
{
  int32_t i, callcnt = 0;
  afr_local_t *local = frame->local;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  LOCK (&frame->lock);
  {
    if (op_ret == 0) {
      local->op_ret = op_ret;
      /* we will return stat info from the first successful child */
      for (i = 0; i < child_count; i++) {
	if (children[i] == prev_frame->this) {
	  if (i < local->stat_child) {
	  local->stbuf = *stbuf;
	  local->stat_child = i;
	  }
	}
      }
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
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
  char *child_errno = NULL;
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  
  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);


  AFR_DEBUG_FMT (this, "loc->path %s", loc->path);

  child_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->stat_child = child_count;

  for (i = 0; i  < child_count; i++) {
    if (child_errno[i] == 0)
      ++local->call_count;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "child_errno[] is not 0, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      STACK_WIND(frame,
		 afr_utimens_cbk,
		 children[i],
		 children[i]->fops->utimens,
		 loc,
		 tv);
    }
  }

  return 0;
}

int32_t
afr_opendir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 fd_t *fd)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  AFR_DEBUG_FMT(this, "op_ret = %d fd = %p, local %p", op_ret, fd, local);

  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  LOCK (&frame->lock);
  {
    if (op_ret >= 0 && local->op_ret == -1) {
      local->op_ret = op_ret;
    }

    if (op_ret >= 0) {
      afrfd_t *afrfdp;
      data_t *afrfdp_data;

      afrfdp_data = dict_get (fd->ctx, this->name);
      if (afrfdp_data == NULL) {
	afrfdp = calloc (1, sizeof (afrfd_t));
	ERR_ABORT (afrfdp);
	afrfdp->fdstate = calloc (child_count, sizeof (char));
	ERR_ABORT (afrfdp->fdstate);
	afrfdp->path = strdup (local->loc->path);
	dict_set (fd->ctx, this->name, data_from_static_ptr (afrfdp));
      } else {
	afrfdp = data_to_ptr (afrfdp_data);
      }
      
      for (i = 0; i < child_count; i++) {
	if (children[i] == prev_frame->this)
	  break;
      }
      afrfdp->fdstate[i] = 1;
    }
    
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    afr_loc_free (local->loc);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, fd);
  }

  return 0;
}

int32_t
afr_opendir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     fd_t *fd)
{
  char *child_errno = NULL;
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);


  AFR_DEBUG_FMT(this, "loc->path = %s inode = %p, local %p", 
		loc->path, loc->inode, local);

  child_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->loc = afr_loc_dup(loc);

  for(i = 0; i < child_count; i++) {
    if (child_errno[i] == 0)
      ++local->call_count;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "child_errno[] is not 0, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN, fd);
    return 0;
  }

  for(i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      STACK_WIND (frame,
		  afr_opendir_cbk,
		  children[i],
		  children[i]->fops->opendir,
		  loc,
		  fd);
    }
  }

  return 0;
}

int32_t
afr_readlink_symlink_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  inode_t *inode,
			  struct stat *stbuf)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG (this);

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  if (callcnt == 0) {
    char *name = local->name;
    int len = strlen (name);
    afr_loc_free(local->loc);
    STACK_UNWIND (frame,
		  len,
		  0,
		  name);
    FREE (name);
  }

  return 0;
}

int32_t
afr_readlink_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  const char *buf)
{
  char *child_errno = NULL;
  afr_local_t *local = frame->local;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG(this);

  child_errno = data_to_ptr (dict_get (local->loc->inode->ctx, this->name));

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == ENOENT)
      local->call_count++;
  }

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  AFR_DEBUG_FMT (this, "op_ret %d buf %s local->call_count %d", 
		 op_ret, buf, local->call_count);

  if ( op_ret >= 0 && 
       (((afr_private_t*)this->private)->self_heal) && 
       local->call_count ) {
    /* readlink was successful, self heal enabled, symlink missing in atleast one node */
    local->name = strdup (buf);
    for (i = 0; i < child_count; i++) {
      if (child_errno[i] == ENOENT) {
	STACK_WIND (frame,
		    afr_readlink_symlink_cbk,
		    children[i],
		    children[i]->fops->symlink,
		    buf,
		    local->loc);
      }
    }
    return 0;
  }

  afr_loc_free(local->loc);
  STACK_UNWIND (frame, op_ret, op_errno, buf);

  return 0;
}


int32_t
afr_readlink (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      size_t size)
{
  char *child_errno = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afr_local_t *local = calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);

  AFR_DEBUG_FMT(this, "loc->path %s loc->inode %p size %d", 
		loc->path, loc->inode, size);

  child_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  frame->local = local;
  local->loc = afr_loc_dup(loc);

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0)
      break;
  }

  if (i == child_count) {
    GF_DEBUG (this, "child_errno[] is not 0, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }

  STACK_WIND (frame,
              afr_readlink_cbk,
              children[i],
              children[i]->fops->readlink,
              loc,
              size);

  return 0;
}

int32_t
afr_getdents_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  dir_entry_t *entry,
		  int32_t count)
{
  afrfd_t *afrfdp = NULL;
  int32_t tmp_count, i;
  dir_entry_t *trav, *prev, *tmp, *afr_entry;
  afr_local_t *local = frame->local;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  AFR_DEBUG_FMT (this, "op_ret = %d", op_ret);

  afrfdp = data_to_ptr(dict_get (local->fd->ctx, this->name));
  if (op_ret >= 0 && entry->next) {
    /* For all the successful calls, come inside this block */
    local->op_ret = op_ret;
    trav = entry->next;
    prev = entry;
    if (local->entry == NULL) {
      /* local->entry is NULL only for the first successful call. So, 
       * take all the entries from that node. 
       */
      afr_entry = calloc (1, sizeof (dir_entry_t));
      ERR_ABORT (afr_entry);
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
	    FREE (tmp->name);
	    FREE (tmp);
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

  if (op_ret == -1 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  for (i = local->call_count; i < child_count; i++) {
    if (afrfdp->fdstate[i]) {
      local->call_count = i + 1;
      STACK_WIND (frame, 
		  afr_getdents_cbk,
		  children[i],
		  children[i]->fops->getdents,
		  local->fd,
		  local->size,
		  local->offset,
		  local->flags);
      return 0;
    }
  }

  /* unwind the current frame with proper entries */
  frame->local = NULL;
  STACK_UNWIND (frame, local->op_ret, local->op_errno, local->entry, local->count);
  
  /* free the local->* */
  {
    /* Now free the entries stored at this level */
    prev = local->entry;
    if (prev) {
      trav = prev->next;
      while (trav) {
	prev->next = trav->next;
	FREE (trav->name);
	FREE (trav);
	trav = prev->next;
	}
      FREE (prev);
    }
  }
  FREE (local);

  return 0;
}


int32_t
afr_getdents (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      size_t size,
	      off_t offset,
	      int32_t flag)
{
  afrfd_t *afrfdp = NULL;
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  local = calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);


  AFR_DEBUG_FMT(this, "fd = %d", fd);

  afrfdp = data_to_ptr(dict_get (fd->ctx, this->name));
  if (afrfdp == NULL) {
    free (local);
    GF_ERROR (this, "afrfdp is NULL, returning EBADFD");
    STACK_UNWIND (frame, -1, EBADFD, NULL, 0);
    return 0;
  }

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->fd = fd;
  local->size = size;
  local->offset = offset;
  local->flags = flag;

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i]) {
      local->call_count = i + 1;
      STACK_WIND (frame, 
		  afr_getdents_cbk,
		  children[i],
		  children[i]->fops->getdents,
		  fd,
		  size,
		  offset,
		  flag);
      return 0;
    }
  }

  STACK_UNWIND (frame, -1, ENOTCONN, NULL, 0);

  return 0;
}


int32_t
afr_readdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 gf_dirent_t *buf)
{
  afr_local_t *local = (afr_local_t *)frame->local;

  AFR_DEBUG(this);

  if (op_ret == -1) {
    call_frame_t *prev_frame = cookie;
    afrfd_t *afrfdp = local->afrfdp;
    if (op_errno == ENOTCONN || op_errno == EBADFD) {
      int i=0;
      afr_private_t *pvt = this->private;
      xlator_t **children = pvt->children;

      for (i = 0; i < pvt->child_count; i++)
	if (((call_frame_t *)cookie)->this == children[i])
	  break;

      afrfdp->fdstate[i] = 0;
      afrfdp->rchild = -1;
      for (i = 0; i < pvt->child_count; i++) {
	if (afrfdp->fdstate[i])
	  break;
      }
      GF_DEBUG (this, "reading from child %d", i);
      if (i < pvt->child_count) {
      	STACK_WIND (frame,
		    afr_readdir_cbk,
		    children[i],
		    children[i]->fops->readdir,
		    local->fd,
		    local->size,
		    local->offset);
	return 0;
      }
    }
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}


int32_t
afr_readdir (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     size_t size,
	     off_t offset)
{
  int32_t i = 0;
  afrfd_t *afrfdp = NULL;
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  afrfdp = data_to_ptr (dict_get (fd->ctx, this->name));

  AFR_DEBUG_FMT(this, "fd %p", fd);

  if (afrfdp == NULL) {
    GF_ERROR (this, "afrfdp is NULL, returning EBADFD");
    STACK_UNWIND (frame, -1, EBADFD, NULL);
    return 0;
  }

  local = frame->local = calloc (1, sizeof (afr_local_t));
  ERR_ABORT (frame->local);
  local->afrfdp = afrfdp;
  local->offset = offset;
  local->size = size;
  local->fd = fd;

  i = afrfdp->rchild;
  if (i == -1 || afrfdp->fdstate[i] == 0) {
    for (i = 0; i < child_count; i++) {
      if (afrfdp->fdstate[i] && pvt->state[i])
	break;
    }
  }
  GF_DEBUG (this, "getdenting from child %d", i);
  if (i == child_count) {
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
  } else {
    STACK_WIND (frame,
		afr_readdir_cbk,
		children[i],
		children[i]->fops->readdir,
		fd,
		size,
		offset);
  }

  return 0;
}


int32_t
afr_setdents_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  afr_local_t *local = frame->local;
  int32_t callcnt;
  call_frame_t *prev_frame = cookie;
  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  if (op_ret == -1 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  if (op_ret == 0)
    local->op_ret = 0;

  if (callcnt == 0) {
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno);
  }

  return 0;
}


int32_t
afr_setdents (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t flags,
	      dir_entry_t *entries,
	      int32_t count)
{
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;
  int32_t i;

  local = calloc (1, sizeof (*local));
  ERR_ABORT (local);


  afrfd_t *afrfdp = data_to_ptr(dict_get (fd->ctx, this->name));

  if (afrfdp == NULL) {
    FREE (local);
    GF_ERROR (this, "afrfdp is NULL, returning EBADFD");
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  frame->local = local;

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i])
      local->call_count++;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "afrfdp->fdstate[] is 0, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i]) {
      STACK_WIND (frame,
		  afr_setdents_cbk,
		  children[i],
		  children[i]->fops->setdents,
		  fd,
		  flags,
		  entries,
		  count);
    }
  }

  return 0;
}

int32_t
afr_bg_setxattr_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  if (callcnt == 0) {
    afr_loc_free (local->loc);
    STACK_DESTROY (frame->root);
  }

  return 0;
}

int32_t
afr_bg_setxattr (call_frame_t *frame, loc_t *loc, dict_t *dict)
{
  call_frame_t *setxattr_frame;
  afr_local_t *local = NULL;
  afr_private_t *pvt = frame->this->private;
  char *state = pvt->state;
  int32_t child_count = pvt->child_count, i;
  xlator_t **children = pvt->children;

  local = calloc (1, sizeof (*local));
  ERR_ABORT (local);


  for (i = 0; i < child_count; i++) {
    if (state[i])
      local->call_count++;
  }

  if (local->call_count == 0) {
    FREE (local);
    return 0;
  }

  setxattr_frame = copy_frame (frame);
  setxattr_frame->local = local;
  setxattr_frame->root->uid = 0;
  setxattr_frame->root->gid = 0;

  local->loc = afr_loc_dup (loc);
  for (i = 0; i < child_count; i++) {
    if (state[i]) {
      STACK_WIND (setxattr_frame,
		  afr_bg_setxattr_cbk,
		  children[i],
		  children[i]->fops->setxattr,
		  local->loc,
		  dict,
		  0);
    }
  }

  return 0;
}

int32_t
afr_mkdir_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       inode_t *inode,
	       struct stat *buf)
{
  int32_t i;
  char *child_errno = NULL;
  data_t *errno_data = NULL;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  inode_t *inoptr = local->loc->inode;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  if (op_ret == 0 && local->op_ret == -1) {
    local->stbuf = *buf;
    local->op_ret = 0;
  }

  errno_data = dict_get (local->loc->inode->ctx, this->name);

  if (errno_data)
    child_errno = data_to_ptr (errno_data);

  if (child_errno == NULL) {
    child_errno = calloc (child_count, sizeof(char));
    ERR_ABORT (child_errno);
    memset (child_errno, ENOTCONN, child_count);
    dict_set (local->loc->inode->ctx, this->name, data_from_dynptr(child_errno, child_count));
  }
    
  /* we will return stat info from the first successful child */
  for (i = 0; i < child_count; i++) {
    if (children[i] == prev_frame->this) {
      break;
    }
  }
  if (inode && list_empty (&inode->fds)) {
    if (op_ret == 0)
      child_errno[i] = 0;
    else 
      child_errno[i] = op_errno;
  }

  local->child++;

  if ((local->child == child_count) || (op_ret == -1 && op_errno != ENOTCONN && local->op_ret == -1)) {
    if (local->op_ret == 0) {
      struct timeval tv;
      int32_t ctime;
      char dict_ctime[100];
      char *dict_version = "1";
      dict_t *dict = get_new_dict();
   
      if (pvt->self_heal) {
	gettimeofday (&tv, NULL);
	ctime = tv.tv_sec;
	sprintf (dict_ctime, "%u", ctime);
	dict_set (dict, GLUSTERFS_VERSION, bin_to_data (dict_version, strlen(dict_version)));
	dict_set (dict, GLUSTERFS_CREATETIME, bin_to_data (dict_ctime, strlen (dict_ctime)));
	dict_ref (dict);
	afr_bg_setxattr (frame, local->loc, dict);
	dict_unref (dict);
      }
      afr_incver_internal_dir (frame, this, (char *)local->loc->path);
    }
    afr_loc_free(local->loc);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inoptr,
		  &local->stbuf);
  } else {
    STACK_WIND (frame,
		afr_mkdir_cbk,
		children[local->child],
		children[local->child]->fops->mkdir,
		local->loc,
		local->mode);
  }
  return 0;
}

int32_t
afr_mkdir (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode)
{
  afr_local_t *local = NULL;
  xlator_list_t *trav = this->children;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);

  AFR_DEBUG_FMT(this, "path %s", loc->path);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->loc = afr_loc_dup(loc);
  local->mode = mode;

  STACK_WIND (frame,
	      afr_mkdir_cbk,
	      trav->xlator,
	      trav->xlator->fops->mkdir,
	      loc,
	      mode);

  return 0;
}

int32_t
afr_unlink_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  AFR_DEBUG_FMT(this, "op_ret = %d", op_ret);
  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  LOCK (&frame->lock);
  {
    if (op_ret == 0 && local->op_ret == -1) {
      local->op_ret = op_ret;
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    if (local->op_ret == 0)
      afr_incver_internal_dir (frame, this, (char *)local->loc->path);
    afr_loc_free (local->loc);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}


int32_t
afr_unlink (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  char *child_errno = NULL;
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);


  AFR_DEBUG_FMT(this, "loc->path = %s loc->inode = %u",loc->path, loc->inode->ino);

  child_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->loc = afr_loc_dup (loc);

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0)
      ++local->call_count;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "child_errno[] is not 0, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0){
      STACK_WIND(frame,
		 afr_unlink_cbk,
		 children[i],
		 children[i]->fops->unlink,
		 loc);
    }
  }

  return 0;
}

int32_t
afr_rmdir_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  if (op_ret == 0) {
    local->op_ret = op_ret;
  }

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    if (local->op_ret == 0)
      afr_incver_internal_dir (frame, this, (char *)local->loc->path);
    afr_loc_free (local->loc);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}


int32_t
afr_rmdir (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc)
{
  char *child_errno = NULL;
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);

  AFR_DEBUG(this);

  child_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->loc = afr_loc_dup (loc);

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0)
      ++local->call_count;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "child_errno[] is not 0, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0){
      STACK_WIND(frame,
		 afr_rmdir_cbk,
		 children[i],
		 children[i]->fops->rmdir,
		 loc);
    }
  }

  return 0;
}


int32_t
afr_create_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		fd_t *fd,
		inode_t *inode,
		struct stat *stbuf)
{
  int32_t i;
  char *child_errno = NULL;
  data_t *errno_data = NULL;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  inode_t *inoptr = local->loc->inode;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  if (op_ret != -1 &&local->op_ret == -1) {
    local->stbuf = *stbuf;
    local->op_ret = op_ret;
  }

  if (op_ret == -1) {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d(%s)", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  errno_data = dict_get (local->loc->inode->ctx, this->name);
  if (errno_data)
    child_errno = data_to_ptr (errno_data);

  if (child_errno == NULL) {
    child_errno = calloc (child_count, sizeof(char));
    ERR_ABORT (child_errno);
    memset (child_errno, ENOTCONN, child_count);
    dict_set (inoptr->ctx, this->name, data_from_dynptr(child_errno, child_count));
  }
  if (op_ret >= 0) {
    afrfd_t *afrfdp;
    data_t *afrfdp_data;

    afrfdp_data = dict_get (fd->ctx, this->name);
    if (afrfdp_data == NULL) {
      afrfdp = calloc (1, sizeof (afrfd_t));
      ERR_ABORT (afrfdp);
      afrfdp->fdstate = calloc (child_count, sizeof (char));
      ERR_ABORT (afrfdp->fdstate);
      afrfdp->fdsuccess = calloc (child_count, sizeof (char));
      ERR_ABORT (afrfdp->fdsuccess);
      afrfdp->create = 1;
      afrfdp->path = strdup (local->loc->path); /* used just for debugging */
      dict_set (fd->ctx, this->name, data_from_static_ptr (afrfdp));
    } else {
      afrfdp = data_to_ptr (afrfdp_data);
    }
    
    for (i = 0; i < child_count; i++) {
      if (children[i] == prev_frame->this)
	break;
    }
    afrfdp->fdstate[i] = 1;
    afrfdp->fdsuccess[i] = 1;
    local->op_ret = op_ret;
  }
    
  /* we will return stat info from the first successful child */
  for (i = 0; i < child_count; i++) {
    if (children[i] == prev_frame->this) {
	break;
    }
  }

  if (inode && list_empty (&inode->fds)) {
    if (op_ret != -1)
      child_errno[i] = 0;
    else
      child_errno[i] = op_errno;
  }

  local->child++;

  if ((local->child == child_count) || (op_ret == -1 && op_errno != ENOTCONN && local->op_ret == -1)) {
    if (local->op_ret != -1) {
      afrfd_t *afrfdp = data_to_ptr (dict_get (local->fd->ctx, this->name));
      if (pvt->read_node == -1 || afrfdp->fdstate[pvt->read_node] == 0) {
	int32_t rchild = 0, alive_children = 0;
	for (i = 0; i < child_count; i++) {
	  if (afrfdp->fdstate[i]) {
	    /* op_ret != -1 implies atleast one increment */
	    alive_children++;
	  }
	}
	rchild = local->stbuf.st_ino % alive_children;
	/* read schedule among alive children */
	for (i = 0; i < child_count; i++) {
	  if (afrfdp->fdstate[i] == 1) {
	    if (rchild == 0)
	      break;
	    rchild--;
	  }
	}
	afrfdp->rchild = i;
      } else {
	afrfdp->rchild = pvt->read_node;
      }

      afr_incver_internal_dir (frame, this, (char *)local->loc->path);
    }
    afr_loc_free(local->loc);
    AFR_DEBUG_FMT (this, "INO IS %d", local->stbuf.st_ino);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  local->fd,
		  inoptr,
		  &local->stbuf);
  } else {
    STACK_WIND (frame,
		afr_create_cbk,
		children[local->child],
		children[local->child]->fops->create,
		local->loc,
		local->flags,
		local->mode,
		local->fd);
  }
  return 0;
}

int32_t
afr_create (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t flags,
	    mode_t mode,
	    fd_t *fd)
{
  afr_local_t *local = NULL;
  afr_private_t *pvt = (afr_private_t *) this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);

  AFR_DEBUG_FMT (this, "path = %s", loc->path);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->stat_child = child_count;
  local->fd = fd;
  local->loc = afr_loc_dup(loc);
  local->flags = flags;
  local->mode = mode;

  STACK_WIND (frame,
	      afr_create_cbk,
	      children[local->child],
	      children[local->child]->fops->create,
	      loc,
	      flags,
	      mode,
	      fd);
  return 0;
}

/*

if (op_ret == -1 && op_errno != ENOTCONN)
  local->op_ret = op_errno;

if (op_ret == 0)
  local->op_ret = 0;

local->child++;

if ((local->child == child_count) || (op_ret == -1 && op_errno != ENOTCONN && local->op_ret == -1)) {
  STACK_UNWIND()
}



*/


int32_t
afr_mknod_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       inode_t *inode,
	       struct stat *stbuf)
{
  int32_t i;
  char *child_errno = NULL;
  data_t *errno_data = NULL;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  inode_t *inoptr = local->loc->inode;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  AFR_DEBUG(this);

  if (op_ret == -1 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  if (op_ret == 0 && local->op_ret == -1) {
    local->stbuf = *stbuf;
    local->op_ret = 0;
  }
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  errno_data = dict_get (local->loc->inode->ctx, this->name);
  if (errno_data)
    child_errno = data_to_ptr (errno_data);

  if (child_errno == NULL) {
    child_errno = calloc (child_count, sizeof(char));
    ERR_ABORT (child_errno);
    memset (child_errno, ENOTCONN, child_count);
    dict_set (inoptr->ctx, this->name, data_from_dynptr(child_errno, child_count));
  }

  for (i = 0; i < child_count; i++) {
    if (children[i] == prev_frame->this) {
      break;
    }
  }

  if (inode && list_empty (&inode->fds)) {
    if (op_ret == 0)
      child_errno[i] = 0;
    else
      child_errno[i] = op_errno;
  }

  local->child++;
  if ((local->child == child_count) || (op_ret == -1 && op_errno != ENOTCONN && local->op_ret == -1)) {
    afr_incver_internal_dir (frame, this, (char *) local->loc->path);
    afr_loc_free(local->loc);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inoptr,
		  &local->stbuf);
    return 0;
  }

  STACK_WIND (frame,
	      afr_mknod_cbk,
	      children[local->child],
	      children[local->child]->fops->mknod,
	      local->loc,
	      local->mode,
	      local->dev);
  return 0;
}

int32_t
afr_mknod (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode,
	   dev_t dev)
{
  afr_local_t *local = NULL;
  xlator_list_t *trav = this->children;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);


  AFR_DEBUG(this);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->loc = afr_loc_dup(loc);
  local->mode = mode;
  local->dev = dev;
  local->child = 0;

  STACK_WIND (frame,
	      afr_mknod_cbk,
	      trav->xlator,
	      trav->xlator->fops->mknod,
	      loc,
	      mode,
	      dev);
  return 0;
}

int32_t
afr_symlink_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *stbuf)
{
  int32_t i;
  char *child_errno = NULL;
  data_t *errno_data = NULL;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  inode_t *inoptr = local->loc->inode;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  if (op_ret == 0 && local->op_ret == -1) {
    local->stbuf = *stbuf;
    local->op_ret = 0;
  }
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  errno_data = dict_get (local->loc->inode->ctx, this->name);
  if (errno_data)
    child_errno = data_to_ptr (errno_data);

  if (child_errno == NULL) {
    child_errno = calloc (child_count, sizeof(char));
    ERR_ABORT (child_errno);
    memset (child_errno, ENOTCONN, child_count);
    dict_set (inoptr->ctx, this->name, data_from_dynptr(child_errno, child_count));
  }
    
  for (i = 0; i < child_count; i++) {
    if (children[i] == prev_frame->this) {
      break;
    }
  }

  if (inode && list_empty (&inode->fds)) {
    if (op_ret == 0)
      child_errno[i] = 0;
    else
      child_errno[i] = op_errno;
  }

  local->child++;

  if ((local->child == child_count) || (op_ret == -1 && op_errno != ENOTCONN && local->op_ret == -1)) {
    if (local->op_ret == 0) {
      afr_incver_internal_dir (frame, this, (char *)local->loc->path);
    }
    afr_loc_free(local->loc);
    FREE (local->path);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inoptr,
		  &local->stbuf);
  } else {
    STACK_WIND (frame,
		afr_symlink_cbk,
		children[local->child],
		children[local->child]->fops->symlink,
		local->path,
		local->loc);
  }
  return 0;
}

int32_t
afr_symlink (call_frame_t *frame,
	     xlator_t *this,
	     const char *linkname,
	     loc_t *loc)
{
  afr_local_t *local = NULL;
  xlator_list_t *trav = this->children;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);


  AFR_DEBUG_FMT(this, "linkname %s loc->path %s", linkname, loc->path);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->loc = afr_loc_dup(loc);
  local->path = strdup (linkname);

  STACK_WIND (frame,
	      afr_symlink_cbk,
	      trav->xlator,
	      trav->xlator->fops->symlink,
	      linkname,
	      loc);
  return 0;
}

int32_t
afr_rename_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *buf)
{
  int32_t callcnt, i;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  AFR_DEBUG(this);

  if (op_ret == -1 && op_errno != ENOTCONN)
    local->op_errno = op_errno;
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  if (op_ret == 0)
    local->op_ret = 0;

  LOCK (&frame->lock);
  {
    if (op_ret == 0) {
      local->op_ret = 0;
      /* we will return stat info from the first successful child */
      for (i = 0; i < child_count; i++) {
	if (children[i] == prev_frame->this) {
	  if (i < local->stat_child) {
	    local->stbuf = *buf;
	    local->stat_child = i;
	  }
	}
      }
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0){
    afr_incver_internal_dir (frame, this, (char *) local->loc->path);
    afr_incver_internal_dir (frame, this, (char *) local->loc2->path);
    afr_loc_free (local->loc);
    afr_loc_free (local->loc2);

    /* This is required as the inode number sent back from 
     * successful rename is always same 
     */
    local->stbuf.st_ino = local->ino; 

    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  &local->stbuf);
  }

  return 0;
}


int32_t
afr_rename (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *oldloc,
	    loc_t *newloc)
{
  char *child_errno = NULL;
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);


  AFR_DEBUG_FMT(this, "oldloc->path %s newloc->path %s", oldloc->path, newloc->path);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->stat_child = pvt->child_count;

  /* Keep track of the inode number of 'oldloc->inode', as we have 
   * to return the same to the parent in case of success 
   */
  local->ino = oldloc->inode->ino;

  child_errno = data_to_ptr (dict_get (oldloc->inode->ctx, this->name));
  for(i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      local->call_count++;
    }
  }

  if (local->call_count > 0)
    {
      for(i = 0; i < child_count; i++) {
	if (child_errno[i] == 0) {
	  STACK_WIND (frame,
		      afr_rename_cbk,
		      children[i],
		      children[i]->fops->rename,
		      oldloc,
		      newloc);
	}
      }
    }
  else
    {
      gf_log (this->name, GF_LOG_WARNING, "returning ENOTCONN");
      STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    }

  return 0;
}

int32_t
afr_link_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      inode_t *inode,
	      struct stat *stbuf)
{
  afr_local_t *local = frame->local;
  inode_t *inoptr = local->loc->inode;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;
  call_frame_t *prev_frame = cookie;
  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno != ENOTCONN)
    local->op_errno = op_errno;
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  if (op_ret == 0 && local->op_ret == -1) {
    local->stbuf = *stbuf;
    local->op_ret = 0;
  }

  local->child++;
  
  if ((local->child == child_count) || (op_ret == -1 && op_errno != ENOTCONN && local->op_ret == -1)) {

    if (local->op_ret == 0) {
      afr_incver_internal_dir (frame, this, (char *) local->path);
    }
    FREE (local->path);
    afr_loc_free (local->loc);    
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inoptr,
		  &local->stbuf);
  } else {
    STACK_WIND (frame,
		afr_link_cbk,
		children[local->child],
		children[local->child]->fops->link,
		local->loc,
		local->path);
  }

  return 0;
}


int32_t
afr_link (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *oldloc,
	  const char *newpath)
{
  char *child_errno = NULL;
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);

  AFR_DEBUG_FMT(this, "oldloc->path %s newpath %s", oldloc->path, newpath);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->loc = afr_loc_dup(oldloc);
  local->path = strdup(newpath);

  child_errno = data_to_ptr (dict_get (oldloc->inode->ctx, this->name));
  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0)
      break;
  }
  if (i == child_count) {
    GF_ERROR (this, "child_errno[] is not 0, returning ENOTCONN");
    STACK_UNWIND (frame,
		  -1,
		  ENOTCONN,
		  NULL,
		  NULL);
    return 0;
  }
  local->child = i;
  STACK_WIND (frame,
	      afr_link_cbk,
	      children[i],
	      children[i]->fops->link,
	      oldloc,
	      newpath);

  return 0;
}

int32_t
afr_chmod_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *stbuf)
{
  int32_t callcnt;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno != ENOTCONN)
    local->op_errno = op_errno;
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  if (op_ret == 0)
    local->op_ret = 0;

  LOCK (&frame->lock);
  {
    if (op_ret == 0) {
      /* we will return stat info from the first successful child */
      for (i = 0; i < child_count; i++) {
	if (children[i] == prev_frame->this) {
	  if (i < local->stat_child) {
	    local->stbuf = *stbuf;
	    local->stat_child = i;
	  }
	}
      }
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0){
    afr_loc_free (local->loc);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  &local->stbuf);
  }

  return 0;
}

int32_t
afr_chmod (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode)
{
  char *child_errno = NULL;
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);


  AFR_DEBUG(this);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->stat_child = pvt->child_count;
  local->loc = afr_loc_dup(loc);

  child_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  for(i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      local->call_count++;
    }
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "child_errno[] is not 0, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }

  local->loc = afr_loc_dup(oldloc);
  local->loc2 = afr_loc_dup (newloc);

  for(i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      STACK_WIND (frame,
		  afr_chmod_cbk,
		  children[i],
		  children[i]->fops->chmod,
		  loc,
		  mode);
    }
  }

  return 0;
}

int32_t
afr_chown_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *stbuf)
{
  int32_t callcnt, i;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno != ENOTCONN)
    local->op_errno = op_errno;
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  if (op_ret == 0)
    local->op_ret = 0;

  LOCK (&frame->lock);
  {
    if (op_ret == 0) {
      /* we will return stat info from the first successful child */
      for (i = 0; i < child_count; i++) {
	if (children[i] == prev_frame->this) {
	  if (i < local->stat_child) {
	    local->stbuf = *stbuf;
	    local->stat_child = i;
	  }
	}
      }
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0){
    afr_loc_free (local->loc);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  &local->stbuf);
  }

  return 0;
}

int32_t
afr_chown (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   uid_t uid,
	   gid_t gid)
{
  char *child_errno = NULL;
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);

  AFR_DEBUG(this);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->stat_child = pvt->child_count;
  local->loc = afr_loc_dup(loc);

  child_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  for(i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      local->call_count++;
    }
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "child_errno[] not 0, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }

  for(i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      STACK_WIND (frame,
		  afr_chown_cbk,
		  children[i],
		  children[i]->fops->chown,
		  loc,
		  uid,
		  gid);
    }
  }

  return 0;
}

int32_t
afr_closedir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  AFR_DEBUG_FMT(this, "op_ret = %d", op_ret);

  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  LOCK (&frame->lock);
  {
    if (op_ret == 0 && local->op_ret == -1) {
      local->op_ret = op_ret;
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}


/* releasedir */
int32_t
afr_closedir (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  afrfd_t *afrfdp = NULL;
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);


  AFR_DEBUG_FMT(this, "fd = %p", fd);

  afrfdp = data_to_ptr(dict_get (fd->ctx, this->name));
  if (afrfdp == NULL) {
    FREE (local);
    GF_ERROR (this, "afrfdp is NULL, returning EBADFD");
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i])
      ++local->call_count;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "child_errno[] not 0, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i]){
      STACK_WIND (frame,
		  afr_closedir_cbk,
		  children[i],
		  children[i]->fops->closedir,
		  fd);
    }
  }
  FREE (afrfdp->fdstate);
  FREE (afrfdp->path);
  FREE (afrfdp);

  return 0;
}

int32_t
afr_fchmod_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *stbuf)
{
  afr_local_t *local = frame->local;
  int32_t callcnt;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  int32_t child_count = pvt->child_count, i;
  xlator_t **children = pvt->children;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    if (op_ret == 0) {
      /* we will return stat info from the first successful child */
      for (i = 0; i < child_count; i++) {
	if (children[i] == prev_frame->this) {
	  if (i < local->stat_child) {
	    local->stbuf = *stbuf;
	    local->stat_child = i;
	  }
	}
      }
    }
  }
  UNLOCK (&frame->lock);

  if (op_ret == -1 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  if (op_ret == 0)
    local->op_ret = 0;

  if (callcnt == 0) {
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  &local->stbuf);
  }

  return 0;
}


int32_t
afr_fchmod (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    mode_t mode)
{
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  afrfd_t *afrfdp = data_to_ptr(dict_get (fd->ctx, this->name));

  local = calloc (1, sizeof (*local));
  ERR_ABORT (local);


  if (afrfdp == NULL) {
    FREE (local);
    GF_ERROR (this, "afrfdp is NULL, returning EBADFD");
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  frame->local = local;
  local->stat_child = child_count;
  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i])
      local->call_count++;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "afrfdp->fdstate[] is 0, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i]) {
      STACK_WIND (frame,
		  afr_fchmod_cbk,
		  children[i],
		  children[i]->fops->fchmod,
		  fd,
		  mode);
    }
  }

  return 0;
}


int32_t
afr_fchown_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *stbuf)
{
  afr_local_t *local = frame->local;
  int32_t callcnt;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  int32_t child_count = pvt->child_count, i;
  xlator_t **children = pvt->children;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
    if (op_ret == 0) {
      /* we will return stat info from the first successful child */
      for (i = 0; i < child_count; i++) {
	if (children[i] == prev_frame->this) {
	  if (i < local->stat_child) {
	    local->stbuf = *stbuf;
	    local->stat_child = i;
	  }
	}
      }
    }
  }
  UNLOCK (&frame->lock);

  if (op_ret == -1 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  if (op_ret == 0)
    local->op_ret = 0;

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  if (callcnt == 0) {
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  &local->stbuf);
  }
  return 0;
}


int32_t
afr_fchown (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    uid_t uid,
	    gid_t gid)
{
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  afrfd_t *afrfdp = data_to_ptr(dict_get (fd->ctx, this->name));

  local = calloc (1, sizeof (*local));
  ERR_ABORT (local);

  if (afrfdp == NULL) {
    FREE (local);
    GF_ERROR (this, "afrfdp is NULL, returning EBADFD");
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  frame->local = local;
  local->stat_child = child_count;
  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i])
      local->call_count++;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "afrfdp->fdstate[] is 0, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i]) {
      STACK_WIND (frame,
		  afr_fchown_cbk,
		  children[i],
		  children[i]->fops->fchown,
		  fd,
		  uid,
		  gid);
    }
  }

  return 0;
}

/* fsyncdir */
int32_t
afr_fsyncdir (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t flags)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}

int32_t
afr_access_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  int32_t callcnt;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  if (op_ret == 0)
    local->op_ret = 0;

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0){
    afr_loc_free (local->loc);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  &local->stbuf);
  }

  return 0;
}

/* access */
int32_t
afr_access (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t mask)
{
  char *child_errno = NULL;
  afr_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);


  AFR_DEBUG(this);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->stat_child = pvt->child_count;
  local->loc = afr_loc_dup(loc);

  child_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  for(i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      local->call_count++;
    }
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "child_errno[] not 0, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN, NULL);
    return 0;
  }

  for(i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      STACK_WIND (frame,
		  afr_access_cbk,
		  children[i],
		  children[i]->fops->access,
		  loc,
		  mask);
    }
  }

  return 0;
}

int32_t
afr_lock_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno)
{
  call_frame_t *prev_frame = cookie;
  AFR_DEBUG(this);
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/*
 * FIXME:
 * the following implementation of lock/unlock is not OK, because
 * we will have to remember on which node we locked and unlock
 * on that node while unlocking. However this is fine as NS lock
 * will not be used by any upper xlators (presently being used
 * only by afr.
 */

int32_t
afr_lock (call_frame_t *frame,
	  xlator_t *this,
	  const char *path)
{
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  char *lock_path = NULL;

  AFR_DEBUG(this);

  for (i = 0; i < child_count; i++) {
    if (pvt->state[i])
      break;
  }

  asprintf (&lock_path, "/%s%s", children[i]->name, path);
  STACK_WIND (frame,
	      afr_lock_cbk,
	      children[i],
	      children[i]->mops->lock,
	      lock_path);
  FREE (lock_path);

  return 0;
}

int32_t
afr_unlock_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  call_frame_t *prev_frame = cookie;
  AFR_DEBUG(this);

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t
afr_unlock (call_frame_t *frame,
	    xlator_t *this,
	    const char *path)
{
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  char *lock_path = NULL;

  AFR_DEBUG(this);

  for (i = 0; i < child_count; i++) {
    if (pvt->state[i])
      break;
  }

  asprintf (&lock_path, "/%s%s", children[i]->name, path);
  STACK_WIND (frame,
	      afr_unlock_cbk,
	      children[i],
	      children[i]->mops->unlock,
	      lock_path);
  FREE (lock_path);

  return 0;
}


int32_t
afr_stats_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct xlator_stats *stats)
{
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  AFR_DEBUG(this);

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  if (op_ret != 0 && op_errno == ENOTCONN && local->xlnodeptr->next) {

    LOCK (&frame->lock);
    {
      local->xlnodeptr = local->xlnodeptr->next;
    }
    UNLOCK (&frame->lock);

    STACK_WIND (frame,
		afr_stats_cbk,
		local->xlnodeptr->xlator,
		local->xlnodeptr->xlator->mops->stats,
		local->flags);
    return 0;
  }

  STACK_UNWIND (frame, op_ret, op_errno, stats);
  return 0;
}


int32_t
afr_stats (call_frame_t *frame,
	   xlator_t *this,
	   int32_t flags)
{
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);

  AFR_DEBUG(this);

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
afr_checksum_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  uint8_t *fchecksum,
		  uint8_t *dchecksum)
{
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  AFR_DEBUG(this);
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d(%s)", 
	      prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
  }

  if (op_ret != 0 && op_errno == ENOTCONN && local->xlnodeptr->next) {

    LOCK (&frame->lock);
    {
      local->xlnodeptr = local->xlnodeptr->next;
    }
    UNLOCK (&frame->lock);

    STACK_WIND (frame,
		afr_checksum_cbk,
		local->xlnodeptr->xlator,
		local->xlnodeptr->xlator->fops->checksum,
		local->loc,
		local->flags);

    return 0;
  }

  afr_loc_free (local->loc);
  STACK_UNWIND (frame, op_ret, op_errno, fchecksum, dchecksum);
  return 0;
}


int32_t
afr_checksum (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t flags)
{
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  ERR_ABORT (local);

  AFR_DEBUG(this);

  frame->local = local;
  local->xlnodeptr = this->children;
  local->flags = flags;
  local->loc = afr_loc_dup(loc);

  STACK_WIND (frame,
	      afr_checksum_cbk,
	      local->xlnodeptr->xlator,
	      local->xlnodeptr->xlator->fops->checksum,
	      loc,
	      flags);

  return 0;
}

static int32_t
afr_check_xattr_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_CRITICAL, 
	    "[CRITICAL]: '%s' doesn't support Extended attribute: %s", 
	    (char *)cookie, strerror (op_errno));
    raise (SIGTERM);
  } else {
    gf_log (this->name, GF_LOG_DEBUG, 
	    "'%s' supports Extended attribute", (char *)cookie);
  }

  STACK_DESTROY (frame->root);
  return 0;
}

static void 
afr_check_xattr (xlator_t *this, 
		 xlator_t *child)
{
  call_ctx_t *cctx = NULL;
  call_pool_t *pool = this->ctx->pool;
  cctx = calloc (1, sizeof (*cctx));
  ERR_ABORT (cctx);
  cctx->frames.root  = cctx;
  cctx->frames.this  = this;    
  cctx->uid = 100; /* Make it some user */
  cctx->pool = pool;
  LOCK (&pool->lock);
  {
    list_add (&cctx->all_frames, &pool->all_frames);
  }
  UNLOCK (&pool->lock);
  
  {
    dict_t *dict = get_new_dict();
    loc_t tmp_loc = {
      .inode = NULL,
      .path = "/",
    };
    dict_set (dict, "trusted.glusterfs-afr-test", 
	      bin_to_data("testing", 7));

    STACK_WIND_COOKIE ((&cctx->frames), 
		       afr_check_xattr_cbk,
		       child->name,
		       child,
		       child->fops->setxattr,
		       &tmp_loc,
		       dict,
		       0);
  }

  return;
}

int32_t
notify (xlator_t *this,
	int32_t event,
	void *data,
	...)
{
  afr_private_t *pvt = this->private;
  int32_t upclients = 0, i = 0;

  if (!pvt)
    return 0;

  AFR_DEBUG_FMT (this, "EVENT %d", event);

  switch (event) {
  case GF_EVENT_CHILD_UP:
    {
      for (i = 0; i < pvt->child_count; i++) {
	if (data == pvt->children[i])
	  break;
      }

      GF_DEBUG (this, "GF_EVENT_CHILD_UP from %s", pvt->children[i]->name);
      pvt->state[i] = 1;

      if (!pvt->xattr_check[i]) {
	afr_check_xattr(this, data);
	pvt->xattr_check[i] = 1;
      }

      /* if all the children were down, and one child came up, send notify to parent */
      for (i = 0; i < pvt->child_count; i++)
	if (pvt->state[i])
	  upclients++;
      
      if (upclients == 1)
	default_notify (this, event, data);
    }
    break;
  case GF_EVENT_CHILD_DOWN:
    {
      for (i = 0; i < pvt->child_count; i++) {
	if (data == pvt->children[i])
	  break;
      }

      GF_DEBUG (this, "GF_EVENT_CHILD_DOWN from %s", pvt->children[i]->name);
      pvt->state[i] = 0;

      for (i = 0; i < pvt->child_count; i++) {
	if (pvt->state[i])
	  upclients++;
      }

      /* if all children are down, and this was the last to go down, send notify to parent */
      if (upclients == 0)
	default_notify (this, event, data);
    }
    break;
  case GF_EVENT_PARENT_UP:
    break;

  default:
    {
      default_notify (this, event, data);
    }
  }

  return 0;
}

int32_t 
init (xlator_t *this)
{
  int32_t i = 0;
  int32_t count = 0;
  afr_private_t *pvt = NULL;
  data_t *lock_node = dict_get (this->options, "lock-node");
  data_t *replicate = dict_get (this->options, "replicate");
  data_t *selfheal = dict_get (this->options, "self-heal");
  data_t *debug = dict_get (this->options, "debug");
  data_t *read_node = dict_get (this->options, "read-subvolume");
  /* change read_node to read_subvolume */
  data_t *read_schedule = dict_get (this->options, "read-schedule");
  xlator_list_t *trav = this->children;

  pvt = calloc (1, sizeof (afr_private_t));
  ERR_ABORT (pvt);


  trav = this->children;
  while (trav) {
    count++;
    trav = trav->next;
  }

  pvt->child_count = count;
  if (debug && strcmp(data_to_str(debug), "on") == 0) {
    /* by default debugging is off */
    GF_DEBUG (this, "debug logs enabled");
    pvt->debug = 1;
    this->trace = 1;
  }

  /* by default self-heal is on */
  pvt->self_heal = 1;
  if (selfheal && strcmp(data_to_str(selfheal), "off") == 0) {
    GF_WARNING (this, "self-heal is disabled");
    pvt->self_heal = 0;
  } else {
    GF_DEBUG (this, "self-heal is enabled (default)");
  }
  /* by default read-schedule is on */
  pvt->read_node = -1;

  if (read_node) {
    if (strcmp(data_to_str(read_node), "*") == 0) {
      GF_DEBUG (this, "config: reads will be scheduled between the children");
      pvt->read_node = -1;
    } else {
      char *rnode = data_to_str (read_node);
      i = 0;
      trav = this->children;
      while (trav) {
	if (strcmp(trav->xlator->name, rnode) == 0)
	  break;
	i++;
	trav = trav->next;
      }
      if (trav == NULL) {
	GF_ERROR (this, "read-subvolume should be * or one among the sobvols");
	FREE (pvt);
	return -1;
      }
      GF_DEBUG (this, "config: reads will be done on %s", trav->xlator->name);
      pvt->read_node = i;
    }
  } else {
    GF_DEBUG (this, "(default) reads will be scheduled between the children");
  }

  if (lock_node) {
    GF_ERROR (this, "lock node will be used from subvolumes list, should not bespecified as a separate option, Exiting.");
    FREE (pvt);
    return -1;
  }
  if (read_schedule) {
    GF_ERROR (this, "please use \"option read-subvolume\"");
    FREE (pvt);
    return -1;
  }
  if(replicate) {
    GF_ERROR (this, "\"option replicate\" is deprecated, it is no more supported. For more information please check http://www.mail-archive.com/gluster-devel@nongnu.org/msg02201.html (This message will be removed in future patches). Exiting!");
    FREE (pvt);
    return -1;
  }

  /* pvt->children will have list of children which maintains its state (up/down) */
  pvt->children = calloc(pvt->child_count, sizeof(xlator_t*));
  ERR_ABORT (pvt->children);
  pvt->state = calloc (pvt->child_count, sizeof(char));
  ERR_ABORT (pvt->state);
  pvt->xattr_check = calloc (pvt->child_count, sizeof(char));
  ERR_ABORT (pvt->xattr_check);
  i = 0;
  trav = this->children;
  while (trav) {
    pvt->children[i++] = trav->xlator;
    trav = trav->next;
  }
  this->private = pvt;

  trav = this->children;
  while (trav) {
    trav->xlator->notify (trav->xlator, GF_EVENT_PARENT_UP, this);
    trav = trav->next;
  }

  return 0;
}

void
fini (xlator_t *this)
{
  afr_private_t *priv = this->private;
  FREE (priv);
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
  .incver      = afr_incver,
  .setxattr    = afr_setxattr,
  .getxattr    = afr_getxattr,
  .removexattr = afr_removexattr,
  .opendir     = afr_opendir,
  .readdir     = afr_readdir,
  .getdents    = afr_getdents,
  .closedir    = afr_closedir,
  .fsyncdir    = afr_fsyncdir,
  .access      = afr_access,
  .ftruncate   = afr_ftruncate,
  .fstat       = afr_fstat,
  .lk          = afr_lk,
  .fchmod      = afr_fchmod,
  .fchown      = afr_fchown,
  .setdents    = afr_setdents,
  .lookup_cbk  = afr_lookup_cbk,
  .checksum    = afr_checksum,
};

struct xlator_mops mops = {
  .stats = afr_stats,
  .lock = afr_lock,
  .unlock = afr_unlock,
};

