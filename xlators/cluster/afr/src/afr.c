/*
   Copyright (c) 2007 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifdef STATIC
#undef STATIC
#endif
#define STATIC /* static */

#define BUF_SIZE 512


#define AFR_DEBUG_FMT(xl, format, args...) if(((afr_private_t*)(xl)->private)->debug) gf_log ((xl)->name, GF_LOG_DEBUG, "AFRDEBUG:" format, ##args);
#define AFR_DEBUG(xl) if(((afr_private_t*)xl->private)->debug) gf_log (xl->name, GF_LOG_DEBUG, "AFRDEBUG:");

#define AFR_ERRNO_DUP(child_errno, afr_errno, child_count) do {\
   child_errno = alloca(child_count);\
   memcpy (child_errno, afr_errno, child_count);\
} while(0);


STATIC loc_t*
afr_loc_dup(loc_t *loc)
{
  loc_t *loctmp;
  GF_BUG_ON (!loc);
  loctmp = calloc(1, sizeof(loc_t));
  loctmp->inode = loc->inode;
  loctmp->path = strdup (loc->path);
  return loctmp;
}

STATIC void
afr_loc_free(loc_t *loc)
{
  GF_BUG_ON (!loc);
  freee (loc->path);
  freee(loc);
}

STATIC void 
afr_free_ashptr (afr_selfheal_t * ashptr, int32_t child_count, int32_t latest)
{
  int32_t i;
  for (i = 0; i < child_count; i++) {
    if (ashptr[i].entry && (ashptr[i].repair || i == latest)) {
      dir_entry_t *element = ashptr[i].entry->next;
      while (element) {
	dir_entry_t *tmp;
	tmp = element;
	element = element->next;
	freee (tmp->name);
	freee (tmp);
      }
      freee (ashptr[i].entry);
    }
  }
  freee (ashptr);
}

STATIC int32_t
afr_lookup_mkdir_chown_cbk (call_frame_t *frame,
			    void *cookie,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno,
			    struct stat *stbuf)
{
  int32_t callcnt = 0;
  int32_t first = -1, latest = -1, i;
  char *child_errno = NULL;
  afr_local_t *local = frame->local;
  afr_private_t *pvt = this->private;
  int32_t child_count = pvt->child_count;
  xlator_t **children = pvt->children;
  call_frame_t *prev_frame = cookie;
  struct stat *statptr = local->statptr;
  inode_t *inoptr = local->loc->inode;
  
  child_errno = data_to_ptr (dict_get(local->loc->inode->ctx, this->name));

  if (op_ret == 0) {
    for (i = 0; i < child_count; i++) {
      if (children[i] == prev_frame->this)
	break;
    }
    statptr[i] = *stbuf;
  }

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    for (i = 0; i < child_count; i++) {
      if (child_errno[i] == 0) {
	if (first == -1) {
	  first = i;
	  latest = i;
	  continue;
	}
	if (statptr[i].st_mtime > statptr[latest].st_mtime)
	  latest = i;
      }
    }
    if (local->ino)
      statptr[latest].st_ino = local->ino;
    else
      statptr[latest].st_ino = statptr[first].st_ino;
    afr_loc_free(local->loc);
    afr_free_ashptr (local->ashptr, child_count, local->latest);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inoptr,
		  &statptr[latest],
		  NULL);
    freee (statptr);
  }
  return 0;
}

STATIC int32_t
afr_lookup_mkdir_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      inode_t *inode,
		      struct stat *buf)
{
  int32_t  i;
  char *child_errno = NULL;
  afr_local_t *local = frame->local;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;
  call_frame_t *prev_frame = cookie;
  struct stat *statptr = local->statptr;

  child_errno = data_to_ptr (dict_get(local->loc->inode->ctx, this->name));

  AFR_DEBUG_FMT (this, "op_ret = %d op_errno = %d from client %s", 
		 op_ret, op_errno, prev_frame->this->name);

  if (op_ret == 0) {
    GF_BUG_ON (!inode);
    GF_BUG_ON (!buf);
    GF_BUG_ON (local->loc->inode != inode);
    for (i = 0; i < child_count; i++) {
      if (children[i] == prev_frame->this)
	break;
    }
    child_errno[i] = 0;
    statptr[i] = *buf;
    STACK_WIND (frame,
		afr_lookup_mkdir_chown_cbk,
		children[i],
		children[i]->fops->chown,
		local->loc,
		local->stbuf.st_uid,
		local->stbuf.st_gid);
  } else {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);

    afr_lookup_mkdir_chown_cbk (frame, prev_frame, this, -1, op_errno, NULL);
  }
  return 0;
}

STATIC int32_t
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
  char *child_errno = NULL;
  inode_t *inoptr = local->loc->inode;

  AFR_DEBUG (this);
  child_errno = data_to_ptr (dict_get(local->loc->inode->ctx, this->name));

  for (i = 0; i < child_count; i++)
    if (prev_frame->this == children[i])
      break;

  if (op_ret == 0) {
    GF_BUG_ON (!stbuf);
    statptr[i] = *stbuf;
  } else {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);
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
	if (statptr[i].st_mtime > statptr[latest].st_mtime)
	  latest = i;
      }
    }
    /* mkdir missing entries in case of dirs */
    if (S_ISDIR (statptr[latest].st_mode)) {
      for (i = 0; i < child_count; i++) {
	if (child_errno[i] == ENOENT)
	  local->call_count++;
      }
      if (local->call_count) {
	/* frame uid/gid is already 0/0 */
	for (i = 0; i < child_count; i++) {
	  if (child_errno[i] == ENOENT) {
	    GF_DEBUG (this, "calling mkdir(%s) on child %s", local->loc->path, children[i]->name);
	    STACK_WIND (frame,
			afr_lookup_mkdir_cbk,
			children[i],
			children[i]->fops->mkdir,
			local->loc,
			local->stbuf.st_mode);
	  }
	}
	return 0;
      }
    }
    afr_loc_free(local->loc);
    afr_free_ashptr (local->ashptr, child_count, local->latest);
    if (local->ino)
      statptr[latest].st_ino = local->ino;
    else
      statptr[latest].st_ino = statptr[first].st_ino;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inoptr,
		  &statptr[latest],
		  NULL);
    freee (statptr);
  }
  return 0;
}

STATIC int32_t
afr_sync_ownership_permission (call_frame_t *frame)
{
  char *child_errno = NULL;
  afr_local_t *local = frame->local;
  inode_t *inode = local->loc->inode;
  afr_private_t *pvt = frame->this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;
  int32_t i, first = -1, latest = -1;
  struct stat *statptr = local->statptr;
  
  child_errno = data_to_ptr (dict_get(local->loc->inode->ctx, frame->this->name));

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
  } else {
    /* mkdir missing directories */
    if (S_ISDIR (statptr[latest].st_mode)) {
      for (i = 0; i < child_count; i++) {
	if (child_errno[i] == ENOENT)
	  local->call_count++;
      }
      if (local->call_count) {
	/* directories are indeed missing on certain children */
	local->stbuf = statptr[latest];
	for (i = 0; i < child_count; i++) {
	  if (child_errno[i] == ENOENT) {
	    AFR_DEBUG_FMT (frame->this, "calling mkdir(%s) on %s", 
			   local->loc->path, children[i]->name);

	    STACK_WIND (frame,
			afr_lookup_mkdir_cbk,
			children[i],
			children[i]->fops->mkdir,
			local->loc,
			statptr[latest].st_mode);
	  }
	}
	return 0;
      }
    }
    /* we reach here means no self-heal is needed */

    for (i = 0; i < child_count; i++) {
      if (child_errno[i] == 0) {
	if (first == -1) {
	  first = i;
	  latest = i;
	  continue;
	}
	if (statptr[i].st_mtime > statptr[latest].st_mtime)
	  latest = i;
      }
    }
    if (local->ino)
      statptr[latest].st_ino = local->ino;
    else
      statptr[latest].st_ino = statptr[first].st_ino;
    afr_loc_free(local->loc);
    afr_free_ashptr (local->ashptr, child_count, local->latest);

    /* latest can not be -1 as local->op_ret is 0 */
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inode,
		  &statptr[latest],
		  NULL);    /* FIXME passing NULL here means afr on afr wont work */
    freee (statptr);
  }
  return 0;
}

STATIC int32_t
afr_lookup_unlock_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG_FMT (this, "(child=%s) op_ret=%d op_errno=%d", 
		 prev_frame->this->name, op_ret, op_errno);

  if (local->rmelem_status) {
    loc_t *loc = local->loc;
    afr_selfheal_t *ashptr = local->ashptr;
    struct stat *statptr = local->statptr;
    afr_private_t *pvt = this->private;

    afr_loc_free (loc);
    afr_free_ashptr (ashptr, pvt->child_count, local->latest);
    freee (statptr);
    STACK_UNWIND (frame, -1, EIO, local->loc->inode, NULL, NULL);
    return 0;
  }

  afr_sync_ownership_permission (frame);
  return 0;
}

STATIC int32_t
afr_lookup_setxattr_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG_FMT (this, "(child=%s) op_ret=%d op_errno=%d", 
		 prev_frame->this->name, op_ret, op_errno);

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d", 
	      prev_frame->this->name, op_ret, op_errno);
    local->rmelem_status = 1;
  }
  if (callcnt == 0) {
    AFR_DEBUG_FMT (this, "unlocking on %s", local->loc->path);
    STACK_WIND (frame,
		afr_lookup_unlock_cbk,
		local->lock_node,
		local->lock_node->mops->unlock,
		local->loc->path);
  }
  return 0;
}

STATIC int32_t
afr_lookup_rmelem_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  afr_private_t *pvt = this->private;
  int32_t child_count = pvt->child_count;
  xlator_t **children = pvt->children;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG_FMT (this, "(child=%s) op_ret=%d op_errno=%d", 
		 prev_frame->this->name, op_ret, op_errno);

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (op_ret == -1)
    local->rmelem_status = 1;

  if (callcnt == 0) {
    if (local->rmelem_status) {
      AFR_DEBUG_FMT (this, "unlocking on %s", local->loc->path);
      STACK_WIND (frame,
		  afr_lookup_unlock_cbk,
		  local->lock_node,
		  local->lock_node->mops->unlock,
		  local->loc->path);
    } else {
      dict_t *latest_xattr;
      int32_t latest = local->latest, i, cnt;
      char *version_str, *ctime_str;
      afr_selfheal_t *ashptr = local->ashptr;
      
      latest_xattr = get_new_dict();
      asprintf (&version_str, "%u", ashptr[latest].version);
      asprintf (&ctime_str, "%u", ashptr[latest].ctime);
      dict_set (latest_xattr, GLUSTERFS_VERSION, data_from_dynptr (version_str, strlen(version_str)));
      dict_set (latest_xattr, GLUSTERFS_CREATETIME, data_from_dynptr (ctime_str, strlen(ctime_str)));

      for (i = 0; i < child_count; i++) {
	if (ashptr[i].repair)
	  local->call_count++;
      }
      cnt = local->call_count;
      for (i = 0; i < child_count; i++) {
	if (ashptr[i].repair) {
	  STACK_WIND (frame,
		      afr_lookup_setxattr_cbk,
		      children[i],
		      children[i]->fops->setxattr,
		      local->loc,
		      latest_xattr,
		      0);
	  if (--cnt == 0)
	    break;
	}
      }
      dict_destroy (latest_xattr);
    }
  }
  return 0;
}

STATIC int32_t
afr_lookup_closedir_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
  int32_t callcnt, i;
  afr_local_t *local = frame->local;
  afr_selfheal_t *ashptr = local->ashptr;
  afr_private_t *pvt = this->private;
  int32_t child_count = pvt->child_count;
  xlator_t **children = pvt->children;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG_FMT (this, "(child=%s) op_ret=%d op_errno=%d", 
		 prev_frame->this->name, op_ret, op_errno);

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    fd_destroy (local->fd);
    for (i = 0; i < child_count; i++) {
      if (ashptr[i].repair && ashptr[i].entry && ashptr[local->latest].entry) {
	/* delete ashptr[i].next */
	dir_entry_t *element = ashptr[i].entry->next;
	while (element) {
	  char path[BUF_SIZE];
	  strcpy (path, local->loc->path);
	  strcat (path, "/");
	  strcat (path, element->name);
	  local->call_count++;
	  AFR_DEBUG_FMT (this, "%s file %s to be deleted", children[i]->name, path);
	  element = element->next;
	}
      }
    }
    if (ashptr[local->latest].entry == NULL)
      local->rmelem_status = 1;
    if (local->call_count == 0) {
      local->call_count++; /* it will be decremented in the cbk function
			    * case where versions differ, but contents are same
			    */
      GF_DEBUG (this, "versions differ, but contents are same");
      afr_lookup_rmelem_cbk (frame, cookie, this, 0, 0);
    } else {
      for (i = 0; i < child_count; i++) {
	if (ashptr[i].repair && ashptr[i].entry && ashptr[local->latest].entry) {
	  /* delete ashptr[i].next */
	  dir_entry_t *element = ashptr[i].entry->next;
	  while (element) {
	    char path[BUF_SIZE];
	    strcpy (path, local->loc->path);
	    strcat (path, "/");
	    strcat (path, element->name);
	    STACK_WIND (frame,
			afr_lookup_rmelem_cbk,
			children[i],
			children[i]->fops->rmelem,
			path);
	    element = element->next;
	  }
	}
      }
    }
  }
  return 0;
}

STATIC int32_t
afr_lookup_readdir_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			dir_entry_t *entry,
			int32_t count)
{
  int32_t callcnt, i;
  afr_local_t *local = frame->local;
  afr_selfheal_t *ashptr = local->ashptr;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;
  int32_t latest = local->latest;

  AFR_DEBUG_FMT (this, "(child=%s) op_ret=%d op_errno=%d", 
		 prev_frame->this->name, op_ret, op_errno);

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (op_ret != -1) {
    for (i = 0; i < child_count; i++) {
      if (children[i] == prev_frame->this)
      break;
    }
    ashptr[i].entry = calloc (1, sizeof (dir_entry_t));
    ashptr[i].entry->next = entry->next;
    entry->next = NULL;
  }

  if (callcnt == 0) {
    for (i = 0; i < child_count; i++) {
      if (ashptr[i].repair || i == local->latest) {
	local->call_count++;
      }
      if (i == latest)
	continue;

      /* from the outdated directories, we keep the entries which have been deleted
       * from the latest directory. Later call rmelem on these entries to remove them
       */
      if (ashptr[i].repair && ashptr[i].entry && ashptr[latest].entry) {
	dir_entry_t *latest_entry, *now;
	now = ashptr[i].entry;
	while (now->next != NULL) {
	  latest_entry = ashptr[latest].entry->next;
	  while (latest_entry) {
	    if (strcmp (latest_entry->name, now->next->name) == 0) {
	      dir_entry_t *tmp = now->next;
	      now->next = tmp->next;
	      freee (tmp->name);
	      freee (tmp);
	      if (now->next == NULL) {
		break;
	      }
	      latest_entry = ashptr[latest].entry->next;
	      continue;
	    }
	    latest_entry = latest_entry->next;
	  }
	  if (now->next)
	    now = now->next;
	}
      }
    }

    for (i = 0; i < child_count; i++) {
      if (ashptr[i].repair || i == local->latest) {
	AFR_DEBUG_FMT (this, "closedir on %s", children[i]->name);
	STACK_WIND (frame,
		    afr_lookup_closedir_cbk,
		    children[i],
		    children[i]->fops->closedir,
		    local->fd);
      }
    }
  }
  return 0;
}

STATIC int32_t
afr_lookup_opendir_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			fd_t *fd)
{
  int32_t callcnt, i;
  afr_local_t *local = frame->local;
  afr_selfheal_t *ashptr = local->ashptr;
  afr_private_t *pvt = this->private;
  int32_t child_count = pvt->child_count;
  xlator_t **children = pvt->children;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG_FMT (this, "(child=%s) op_ret=%d op_errno=%d", 
		 prev_frame->this->name, op_ret, op_errno);

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    for (i = 0; i < child_count; i++)
      if (ashptr[i].repair || i == local->latest)
	local->call_count++;
    for (i = 0; i < child_count; i++) {
      if (ashptr[i].repair || i == local->latest) {
	AFR_DEBUG_FMT (this, "readdir on %s", children[i]->name);
	STACK_WIND (frame,
		    afr_lookup_readdir_cbk,
		    children[i],
		    children[i]->fops->readdir,
		    0,
		    0,
		    local->fd);
      }
    }
  }
  return 0;
}

STATIC int32_t
afr_lookup_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		inode_t *inode,
		struct stat *buf,
		dict_t *xattr);

STATIC int32_t
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

  local->call_count = child_count;

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

STATIC void
afr_check_ctime_version (call_frame_t *frame)
{
  /*
   * if not a directory call sync perm/ownership function
   * if it is a directory, compare the ctime/versions
   * if they are same call sync perm/owenership function
   * if they differ, lock the path
   * in lock_cbk, get dirents from the latest and the outdated children
   * note down all the elements (files/dirs/links) that need to be deleted from the outdated children
   * call remove_elem on the elements that need to be removed.
   * in the cbk, update the ctime/version on the outdated children
   * in the cbk call sync perm/ownership function.
   */
  /* we need to inc version count whenever there is change in contents
   * of a directory:
   * create
   * unlink
   * rmdir
   * mkdir
   * symlink
   * link
   * rename
   * mknod
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

  child_errno = data_to_ptr (dict_get(local->loc->inode->ctx, frame->this->name)); /* child_errno cant be NULL */

  for (i = 0; i < child_count; i++)
    if (child_errno[i] == 0)
      break;
  latest = first = i; /* this is valid else we wouldnt have got called */

  if (S_ISDIR(statptr[i].st_mode) == 0) {
    /* in case this is not directory */
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
    afr_sync_ownership_permission (frame);
    return;
  }
  for (i = 0; i < child_count; i++) {
    if (pvt->state[i])
      break;
  }
  if (i == child_count) {
    afr_sync_ownership_permission (frame);
    return;
  }

  if (local->lock_node) {
    local->fd = fd_create (local->loc->inode);

    for (i = 0; i < child_count; i++) {
      if (child_errno[i] != 0)
	continue;
      if (i == latest) {
	local->call_count++;
	continue;
      }
      if (ashptr[latest].ctime > ashptr[i].ctime) {
	local->call_count++;
	ashptr[i].repair = 1;
	continue;
      }
      if (ashptr[latest].ctime == ashptr[i].ctime && ashptr[latest].version > ashptr[i].version) {
	local->call_count++;
	ashptr[i].repair = 1;
      }
    }
    local->latest = latest;

    for (i = 0; i < child_count; i++) {
      if (i == latest || ashptr[i].repair) {
	STACK_WIND (frame,
		    afr_lookup_opendir_cbk,
		    children[i],
		    children[i]->fops->opendir,
		    local->loc,
		    local->fd);
      }
    }
  } else {
    for (i = 0; i < child_count; i++) {
      if (state[i])
	break;
    }
    /* FIXME handle i == child_count */
    local->lock_node = children[i];

    /* lets lock the first alive node */
    STACK_WIND (frame,
		afr_lookup_lock_cbk,
		children[i],
		children[i]->mops->lock,
		local->loc->path);
  }
  return;
}

STATIC int32_t
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

  for (i = 0; i < child_count; i++)
    if (children[i] == prev_frame->this)
      break;

  errno_data = dict_get (local->loc->inode->ctx, this->name);
  if (errno_data)
    child_errno = data_to_ptr (errno_data);
  if (child_errno == NULL) {
    /* first time lookup and success */
    child_errno = calloc (child_count, sizeof (char));	
    dict_set (local->loc->inode->ctx, this->name, data_from_dynptr (child_errno, child_count));
  }

  /* child_errno[i] is either 0 indicating success or op_errno indicating failure */
  if (op_ret == 0) {
    data_t *ctime_data, *version_data;
    local->op_ret = 0;
    child_errno[i] = 0;
    GF_BUG_ON (!inode);
    GF_BUG_ON (!buf);
    statptr[i] = *buf;
    if (pvt->self_heal && xattr) {
      ctime_data = dict_get (xattr, GLUSTERFS_CREATETIME);
      if (ctime_data) {
	ashptr[i].ctime = data_to_uint32 (ctime_data);
      }
      version_data = dict_get (xattr, GLUSTERFS_VERSION);
      if (version_data) {
	ashptr[i].version = data_to_uint32 (version_data);
      }

      AFR_DEBUG_FMT (this, "child %s ctime %d version %d", 
		     prev_frame->this->name, ashptr[i].ctime, ashptr[i].version);
    }
  } else {
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
	afr_check_ctime_version (frame);
	return 0;
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
	  if (statptr[i].st_mtime > statptr[latest].st_mtime)
	    latest = i;
	}
      }
    }
    if (latest == -1) {
      /* so that STACK_UNWIND does not access statptr[-1] */
      latest = 0;
    } else {
      /* we preserve the ino num (whatever that was got during the initial lookup */
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
		  xattr); /* FIXME is this correct? */
    freee (statptr);
  }
  return 0;
}

STATIC int32_t
afr_lookup (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t need_xattr)
{
  afr_local_t *local = calloc (1, sizeof (*local));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  AFR_DEBUG_FMT (this, "loc->path = %s loc->inode = %p", loc->path, loc->inode);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->loc = afr_loc_dup (loc);

  /* statptr[] array is used for selfheal */
  local->statptr = calloc (child_count, sizeof (struct stat));
  local->ashptr  = calloc (child_count, sizeof (afr_selfheal_t));
  local->call_count = child_count;
  local->ino = loc->ino;

  for (i = 0; i < child_count; i ++) {
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

STATIC int32_t
afr_incver (call_frame_t *frame,
		xlator_t *this,
		const char *path)
{
  afr_local_t *local = calloc (1, sizeof (afr_local_t));;
  afr_private_t *pvt = frame->this->private;
  int32_t child_count = pvt->child_count, i;
  xlator_t **children = pvt->children;
  char *state = pvt->state;

  frame->local = local;
  local->op_ret = -1;
  for (i = 0; i < child_count; i++) {
    if (state[i])
      local->call_count++;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "all children are down, returning ENOTCONN");
    STACK_UNWIND (frame, -1, ENOTCONN);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (state[i]) {
      STACK_WIND (frame,
		  afr_incver_cbk,
		  children[i],
		  children[i]->fops->incver,
		  path);
    }
  }
  return 0;
}


STATIC int32_t
afr_incver_internal_unlock_cbk (call_frame_t *frame,
				void *cookie,
				xlator_t *this,
				int32_t op_ret,
				int32_t op_errno)
{
  afr_local_t *local = frame->local;

  freee (local->path);
  STACK_DESTROY (frame->root);
  return 0;
}

STATIC int32_t
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

  if (callcnt == 0) {
    STACK_WIND (frame,
		afr_incver_internal_unlock_cbk,
		local->lock_node,
		local->lock_node->mops->unlock,
		local->path);
  }
  return 0;
}

STATIC int32_t
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

  for (i = 0; i < child_count; i++) {
    if (state[i])
      local->call_count++;
  }

  /* handle call_count == 0 */
  for (i = 0; i < child_count; i++) {
    if (state[i]) {
      STACK_WIND (frame,
		  afr_incver_internal_incver_cbk,
		  children[i],
		  children[i]->fops->incver,
		  local->path);
    }
  }
  return 0;
}

STATIC int32_t
afr_incver_internal (call_frame_t *frame,
		     xlator_t *this,
		     const char *path)
{
  call_frame_t *incver_frame;
  afr_local_t *local;
  afr_private_t *pvt = frame->this->private;
  int32_t child_count = pvt->child_count, i, call_count = 0;
  xlator_t **children = pvt->children;
  char *state = pvt->state;

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

  local = calloc (1, sizeof (afr_local_t));
  incver_frame = copy_frame (frame);
  incver_frame->local = local;

  local->lock_node = children[i];
  local->path = dirname (strdup(path));
  STACK_WIND (incver_frame,
	      afr_incver_internal_lock_cbk,
	      local->lock_node,
	      local->lock_node->mops->lock,
	      local->path);
  return 0;
}


/* no need to do anything in forget, as the mem will be just free'd in dict_destroy(inode->ctx) */

STATIC int32_t
afr_forget (call_frame_t *frame,
	    xlator_t *this,
	    inode_t *inode)
{
  return 0;
}

STATIC int32_t
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
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);
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

STATIC int32_t
afr_setxattr (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      dict_t *dict,
	      int32_t flags)
{
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  char *afr_errno = NULL;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  char *child_errno = NULL;

  AFR_DEBUG_FMT (this, "loc->path = %s", loc->path);

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
  return 0;
}

STATIC int32_t
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
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      frame->local, prev_frame->this->name, op_ret, op_errno);
  }

  STACK_UNWIND (frame, op_ret, op_errno, dict);
  return 0;
}

STATIC int32_t
afr_getxattr (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
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
	      loc);
  return 0;
}

STATIC int32_t
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
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);
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

STATIC int32_t
afr_removexattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 const char *name)
{
  char *afr_errno = NULL;
  char *child_errno = NULL;
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

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

STATIC int32_t
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
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);
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
	afrfdp->fdstate = calloc (child_count, sizeof (char));
	afrfdp->fdsuccess = calloc (child_count, sizeof (char));
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

STATIC int32_t
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
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);
    call_frame_t *open_frame = local->orig_frame;
    afr_local_t *open_local = open_frame->local;
    open_local->sh_return_error = 1;
  }

  AFR_DEBUG_FMT (this, "call_resume()");
  call_resume (local->stub);
  /* clean up after resume */
  freee (local->loc->path);
  freee (local->loc);

  if (local->fd) {
    afrfd_t *afrfdp = data_to_ptr (dict_get(local->fd->ctx, this->name));
    freee (afrfdp->fdstate);
    /* afrfdp->path is not allocated */
    freee (afrfdp);
    dict_destroy (local->fd->ctx);
    freee (local->fd);
  }
  list_for_each_entry_safe (ash, ashtemp, list, clist) {
    list_del (&ash->clist);
    if (ash->dict)
      dict_unref (ash->dict);
    freee (ash);
  }
  freee (list);
  STACK_DESTROY (frame->root);
  return 0;
}

STATIC int32_t
afr_selfheal_nosync_close_cbk (call_frame_t *frame,
			       void *cookie,
			       xlator_t *this,
			       int32_t op_ret,
			       int32_t op_errno);

/* we call afr_error_during_sync if there was any error during read/write during syncing. */

STATIC int32_t
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


STATIC int32_t
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
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);
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
    STACK_WIND (frame,
		afr_selfheal_unlock_cbk,
		local->lock_node,
		local->lock_node->mops->unlock,
		local->loc->path);
  }
  return 0;
}

STATIC int32_t
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
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);
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
    STACK_WIND (frame,
		afr_selfheal_unlock_cbk,
		local->lock_node,
		local->lock_node->mops->unlock,
		local->loc->path);
  }
  return 0;
}

/* FIXME handle the situation when one of the close fails */

STATIC int32_t
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

STATIC int32_t
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
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);
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

STATIC int32_t
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
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);
    afr_error_during_sync(frame);
  }
  return 0;
}

STATIC int32_t
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

STATIC int32_t
afr_selfheal_chown_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			struct stat *stat)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    afr_selfheal_sync_file (frame, frame->this);
  }

  return 0;
}


STATIC int32_t
afr_selfheal_chown_file (call_frame_t *frame,
			 xlator_t *this)
{
  int32_t cnt = 0;
  afr_selfheal_t *ash = NULL;
  afr_local_t *local = frame->local;
  struct list_head *list = local->list;

  list_for_each_entry (ash, list, clist) {
    if (ash->repair && ash->op_errno == ENOENT)
      local->call_count++;
  }

  cnt = local->call_count;
  if (local->call_count) {
    list_for_each_entry (ash, list, clist) {
      if (ash->repair && ash->op_errno == ENOENT) {
	STACK_WIND (frame,
		    afr_selfheal_chown_cbk,
		    ash->xl,
		    ash->xl->fops->chown,
		    local->loc,
		    local->source->stat.st_uid,
		    local->source->stat.st_gid);
	if (--cnt == 0)
	  break;
      }
    }
  } else {
    afr_selfheal_sync_file (frame, this);
  }

  return 0;
}

STATIC int32_t
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
    AFR_DEBUG_FMT(this, "calling unlock on local->loc->path %s", local->loc->path);
    STACK_WIND (frame,
		afr_selfheal_unlock_cbk,
		local->lock_node,
		local->lock_node->mops->unlock,
		local->loc->path);
  }
  return 0;
}

STATIC int32_t
afr_selfheal_create_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 fd_t *fd,
			 inode_t *inode,
			 struct stat *stat)
{
  int32_t callcnt = 0;
  char *child_errno = NULL;
  afrfd_t *afrfdp = NULL;
  afr_selfheal_t *ash = NULL;
  struct list_head *list = NULL;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  AFR_DEBUG_FMT (this, "op_ret = %d from %s", op_ret, prev_frame->this->name);

  child_errno = data_to_ptr (dict_get (fd->inode->ctx, this->name));
  afrfdp = data_to_ptr (dict_get (fd->ctx, this->name));

  if (op_ret >= 0) {
    GF_BUG_ON (!fd);
    GF_BUG_ON (!inode);
    GF_BUG_ON (!stat);
    for (i = 0; i < child_count; i++) {
      if (children[i] == prev_frame->this)
	break;
    }
    child_errno[i] = 0;
    afrfdp->fdstate[i] = 1;
    list = local->list;
    list_for_each_entry (ash, list, clist) {
      if (ash->xl == prev_frame->this)
	break;
    }
    ash->inode = inode;
  } else {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);
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
      afr_selfheal_chown_file (frame, this);
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


STATIC int32_t
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
  
  afrfdp = data_to_ptr (dict_get (fd->ctx, this->name));

  if (op_ret >= 0) {
    GF_BUG_ON (!fd);
    for (i = 0; i < child_count; i++)
      if (prev_frame->this == children[i])
	break;
    afrfdp->fdstate[i] = 1;
  } else {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d ", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);
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
      afr_selfheal_chown_file (frame, this);
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

STATIC int32_t
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

  /* FIXME handle failure here! */
  if (op_ret == 0) {
    local->source->stat = *stat;
  }

  cnt = local->call_count;
  list_for_each_entry (ash, list, clist) {
    if (ash == source) {
      AFR_DEBUG_FMT (this, "open() on %s", ash->xl->name);
      STACK_WIND (frame,
		  afr_selfheal_open_cbk,
		  ash->xl,
		  ash->xl->fops->open,
		  local->loc,
		  O_RDONLY,
		  local->fd);
      if (--cnt == 0)
	break;
      continue;
    }
    if (ash->repair == 0) { 
      /* case where op_errno might be ENOTCONN */
      AFR_DEBUG_FMT (this, "repair not needed on %s", ash->xl->name);
      continue;             
      /* also when we are not supposed to replicate here */
    }

    if (ash->op_errno == ENOENT) {
      AFR_DEBUG_FMT (this, "create() on %s", ash->xl->name);
      STACK_WIND (frame,
		  afr_selfheal_create_cbk,
		  ash->xl,
		  ash->xl->fops->create,
		  local->loc,
		  0,
		  source->stat.st_mode,
		  local->fd);
      if (--cnt == 0)
	break;
      /* We restore the frame->root->uid/gid in create_cbk because we should not
       * set it here. It will fail if this is the last STACK_WIND, in which
       * case we might STACK_UNWIND and frame might get destroyed after which
       * we should not be accessing frame->*
       */
      continue;
    }
    
    AFR_DEBUG_FMT (this, "open() on %s", ash->xl->name);
    STACK_WIND (frame,
		afr_selfheal_open_cbk,
		ash->xl,
		ash->xl->fops->open,
		local->loc,
		O_RDWR | O_TRUNC,
		local->fd);
    if (--cnt == 0)
      break;
  }
  return 0;
}


/* TODO: crappy code, clean this function */

STATIC int32_t
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
      GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
		local->loc->path, prev_frame->this->name, op_ret, op_errno);
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
	if (ash->op_errno == ENOENT) {
	  AFR_DEBUG_FMT (this, "file missing on %s", ash->xl->name);
	  ash->repair = 1;
	  local->call_count++;
	  continue;
	}
	if (ash->inode == NULL) {
	  AFR_DEBUG_FMT (this, "something wrong with %s errno = %d", 
			 ash->xl->name, ash->op_errno);
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
	  local->call_count++;
	  AFR_DEBUG_FMT (this, "%s has ENOENT , %d", ash->xl->name, local->call_count);
	  ash->repair = 1;
	  continue;
	}
	/* we will not repair any other errors like ENOTCONN */
	/* if op_ret is 0, we make op_errno 0 in the code above */
	if (ash->op_errno != 0) 
	  continue;             

	/* Can we reach this condition at all? because previous continue will take care of this */
	if (ash->inode == NULL)
	  continue;

	if (latest > ash->version) {
	  ash->repair = 1;
	  local->call_count++;
	  AFR_DEBUG_FMT (this, "%s version %d outdated, latest=%d, %d", 
			 ash->xl->name, ash->version, latest, local->call_count);
	}
      }

      if (local->call_count == 1) {
	/* call_count would have got incremented for source */
	AFR_DEBUG_FMT (this, "self heal NOT needed");
	STACK_WIND (frame,
		    afr_selfheal_unlock_cbk,
		    local->lock_node,
		    local->lock_node->mops->unlock,
		    local->loc->path);
	return 0;
      }
    }

    AFR_DEBUG_FMT (this, "self heal needed, source is %s", source->xl->name);
    GF_DEBUG (this, "self-heal needed (path=%s source=%s)", 
	      source->xl->name, local->loc->path);

    local->source = source;
    local->fd = calloc (1, sizeof(fd_t));
    local->fd->ctx = get_new_dict();
    afrfdp = calloc (1, sizeof (*afrfdp));
    afrfdp->fdstate = calloc (child_count, sizeof (char));
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

STATIC int32_t
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
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);

    call_frame_t *open_frame = local->orig_frame;
    afr_local_t *open_local = open_frame->local;
    open_local->sh_return_error = 1;
    call_resume(local->stub);
    freee (local->loc->path);
    freee (local->loc);
    if (local->fd) {
      afrfd_t *afrfdp;
      afrfdp = data_to_ptr (dict_get(local->fd->ctx, this->name));
      freee(afrfdp->fdstate);
      /* afrfdp is freed in dict_destroy */
      dict_destroy (local->fd->ctx);
      freee (local->fd);
    }
    list_for_each_entry_safe (ash, ashtemp, list, clist) {
      list_del (&ash->clist);
      if (ash->dict)
	dict_unref (ash->dict);
      freee (ash);
    }
    freee (list);
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
		  local->loc);
      if (--totcnt == 0)
	break;
    }
  }

  return 0;
}


STATIC int32_t
afr_selfheal (call_frame_t *frame,
	      xlator_t *this,
	      call_stub_t *stub,
	      loc_t *loc)
{
  int32_t i, lock_node = 0;
  char *child_errno = NULL;
  afr_selfheal_t *ash;
  call_frame_t *shframe = copy_frame (frame);
  afr_local_t *shlocal = calloc (1, sizeof (afr_local_t));
  struct list_head *list = calloc (1, sizeof (*list));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  AFR_DEBUG(this);

  child_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  for (i = 0; i < child_count; i++) {
    if (pvt->state[i])
      break;
  }
  if (i == child_count) {
    GF_ERROR (this, "none of the children are up for locking, returning EIO");

    freee (list);
    freee (shlocal);
    //STACK_DESTROY (shframe->root); /* copy_frame() allocates some memory, free it */

    STACK_UNWIND (frame, -1, EIO, NULL);
    return 0;
    /* FIXME clean up the mess, check if can reach this point */
  }

  lock_node = i;
  INIT_LIST_HEAD (list);
  shframe->local = shlocal;
  shlocal->list = list;
  shlocal->loc = calloc (1, sizeof (loc_t));
  shlocal->loc->path = strdup (loc->path);
  shlocal->loc->inode = loc->inode;
  shlocal->orig_frame = frame;
  shlocal->stub = stub;

  ((afr_local_t*)frame->local)->shcalled = 1;

  shframe->root->uid = 0;
  shframe->root->gid = 0;
  for (i = 0; i < child_count; i++) {
    ash = calloc (1, sizeof (*ash));
    ash->xl = children[i];
    if (child_errno[i] == 0)
      ash->inode = (void*)1;
    ash->op_errno = child_errno[i];
    list_add_tail (&ash->clist, list);
  }

  AFR_DEBUG_FMT (this, "locking the node %s", children[lock_node]->name);

  shlocal->lock_node = children[lock_node];

  STACK_WIND (shframe,
	      afr_selfheal_lock_cbk,
	      children[lock_node],
	      children[lock_node]->mops->lock,
	      loc->path);

  return 0;
}

STATIC int32_t
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
  }
  local = frame->local;
  
  if (((afr_private_t *) this->private)->self_heal) {
    AFR_DEBUG_FMT (this, "self heal enabled");
    if (local->sh_return_error) {
      GF_ERROR (this, "self heal failed, returning EIO");
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

/* FIXME if one read fails, we need to fail over */

STATIC int32_t
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

    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      afrfdp->path, prev_frame->this->name, op_ret, op_errno);
  }

  STACK_UNWIND (frame, op_ret, op_errno, vector, count, stat);
  return 0;
}


STATIC int32_t
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
  GF_DEBUG (this, "reading from child %d", i);
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

/* FIXME if one write fails, we should not increment version on it */

STATIC int32_t
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

  AFR_DEBUG_FMT(this, "op_ret %d op_errno %d", op_ret, op_errno);

  if (op_ret == -1 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  if (op_ret == -1) {
    afr_private_t *pvt = this->private;
    int32_t child_count = pvt->child_count, i;
    xlator_t **children = pvt->children;
    afrfd_t *afrfdp = NULL;

    afrfdp = data_to_ptr (dict_get (local->fd->ctx, this->name));
    for (i = 0; i < child_count; i++)
      if (prev_frame->this == children[i])
	break;

    afrfdp->fdstate[i] = 0;

    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      afrfdp->path, prev_frame->this->name, op_ret, op_errno);
  }

  if (op_ret >= 0) {
    local->op_ret = op_ret;
    local->stbuf = *stat;
  }

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }

  return 0;
}

STATIC int32_t
afr_writev (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    struct iovec *vector,
	    int32_t count,
	    off_t offset)
{
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afrfd_t *afrfdp = NULL;
  
  AFR_DEBUG_FMT(this, "fd %p", fd);

  afrfdp = data_to_ptr (dict_get (fd->ctx, this->name));
  if (afrfdp == NULL) {
    freee (local);
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


STATIC int32_t
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
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      afrfdp->path, prev_frame->this->name, op_ret, op_errno);
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
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}


STATIC int32_t
afr_ftruncate (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       off_t offset)
{
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afrfd_t *afrfdp = NULL;

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

STATIC int32_t
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

    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      afrfdp->path, prev_frame->this->name, op_ret, op_errno);
  }

  frame->local = NULL; /* so that STACK_UNWIND does not try to free */
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

STATIC int32_t
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

STATIC int32_t
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
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      afrfdp->path, prev_frame->this->name, op_ret, op_errno);
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

STATIC int32_t
afr_flush (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afrfd_t *afrfdp = NULL;

  AFR_DEBUG_FMT(this, "fd %p", fd);

  afrfdp = data_to_ptr (dict_get (fd->ctx, this->name));
  if (afrfdp == NULL) {
    freee (local);
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


STATIC int32_t
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
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);
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
    freee (afrfdp->fdstate);
    freee (afrfdp->fdsuccess);
    freee (afrfdp->path);
    freee (afrfdp);
    afr_loc_free (local->loc);
    if (local->ashptr)
      free(local->ashptr);

    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}


STATIC int32_t
afr_close_unlock_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
  fd_t *fd;
  int32_t i, cnt;
  afr_local_t *local = frame->local;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;
  afrfd_t *afrfdp = NULL;

  AFR_DEBUG (this);

  if (op_ret == -1) {
    call_frame_t *prev_frame = cookie;
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);
  }

  fd = local->fd;
  afrfdp = data_to_ptr (dict_get(local->fd->ctx, this->name));
  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdsuccess[i])
      local->call_count++;
  }

  cnt = local->call_count;
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


STATIC int32_t
afr_close_setxattr_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;

  AFR_DEBUG (this);

  if (op_ret == -1 && op_errno != ENOENT) {
    call_frame_t *prev_frame = cookie;
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);
  }

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    STACK_WIND (frame,
		afr_close_unlock_cbk,
		local->lock_node,
		local->lock_node->mops->unlock,
		local->loc->path);
  }
  return 0;
}

STATIC int32_t
afr_close_getxattr_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			dict_t *dict)
{
  int32_t callcnt, i;
  afr_selfheal_t *ashptr;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, ctime_exist = 0;

  AFR_DEBUG (this);

  ashptr = local->ashptr;
  for (i = 0; i < child_count; i++)
    if (children[i] == prev_frame->this)
      break;

  if (op_ret>=0 && dict) {
    data_t *version_data = dict_get (dict, GLUSTERFS_VERSION);
    data_t *ctime_data = dict_get (dict, GLUSTERFS_CREATETIME);
    if (version_data) {
      ashptr[i].version = data_to_uint32 (version_data);
      AFR_DEBUG_FMT (this, "version %d returned from %s", 
		     ashptr[i].version, prev_frame->this->name);
    } else {
      AFR_DEBUG_FMT (this, "version attribute missing on %s, putting it to 2", 
		     prev_frame->this->name);
      ashptr[i].version = 1; /* no version found, we'll increment and put it as 2 */
    }
    if (ctime_data) {
      ashptr[i].ctime = data_to_uint32 (ctime_data);
    }
  } else {
    ashptr[i].version = 1; /* will be incremented to 2 */
    AFR_DEBUG_FMT (this, "version attribute missing on %s, putting it to 2", 
		   prev_frame->this->name);
  }

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if(callcnt == 0) {
    dict_t *attr;
    int32_t i, cnt;
    struct timeval tv;
    int32_t ctime;
    char dict_ctime[100];
    afrfd_t *afrfdp = data_to_ptr (dict_get(local->fd->ctx, this->name));

    attr = get_new_dict();
    for (i = 0; i < child_count; i++) {
      if (afrfdp->fdstate[i])
	local->call_count++;
      if (ashptr[i].ctime != 0)
	ctime_exist = 1;
    }

    cnt = local->call_count;
    if (afrfdp->create || ctime_exist == 0) {
      gettimeofday (&tv, NULL);
      ctime = tv.tv_sec;
      sprintf (dict_ctime, "%u", ctime);
      dict_set (attr, GLUSTERFS_CREATETIME, bin_to_data (dict_ctime, strlen (dict_ctime)));
    }

    for (i = 0; i < child_count; i++) {
      if (afrfdp->fdstate[i]) {
	char dict_version[100];
	sprintf (dict_version, "%u", ashptr[i].version+1);
	dict_set (attr, GLUSTERFS_VERSION, bin_to_data(dict_version, strlen(dict_version)));
	STACK_WIND (frame,
		    afr_close_setxattr_cbk,
		    children[i],
		    children[i]->fops->setxattr,
		    local->loc,
		    attr,
		    0);
	if (--cnt == 0)  /* in case posix was loaded as child */
	  break;
      }
    }
    dict_destroy (attr);
  }

  return 0;
}

STATIC int32_t
afr_close_lock_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  afr_local_t *local = frame->local;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i, cnt;
  fd_t *fd = local->fd;
  afrfd_t *afrfdp = data_to_ptr (dict_get(fd->ctx, this->name));

  AFR_DEBUG (this);

  if (op_ret == -1) {
    call_frame_t *prev_frame = cookie;
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);
  }

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i]) {
      local->call_count++;
    }
  }

  cnt = local->call_count;
  local->ashptr = calloc (child_count, sizeof (afr_selfheal_t));
  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i]) {
      STACK_WIND (frame,
		  afr_close_getxattr_cbk,
		  children[i],
		  children[i]->fops->getxattr,
		  local->loc);
      if (--cnt == 0)
	break;
    }
  }

  return 0;
}

STATIC int32_t
afr_close (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  int32_t cnt = 0;
  char *path = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afr_local_t *local = calloc (1, sizeof(*local));
  afrfd_t *afrfdp = data_to_ptr (dict_get (fd->ctx, this->name));

  if (afrfdp == NULL) {
    free (local);
    GF_ERROR (this, "afrfdp is NULL, returning EBADFD");
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  AFR_DEBUG_FMT (this, "close on %s fd %p", path, fd);

  path = afrfdp->path;
  frame->local = local;
  local->fd = fd;
  local->loc = calloc (1, sizeof (loc_t));
  local->loc->path = strdup(path);
  local->loc->inode = fd->inode;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;

  if (((afr_private_t*) this->private)->self_heal && 
      (afrfdp->write || afrfdp->create)) {

    AFR_DEBUG_FMT (this, "self heal enabled, increasing the version count");

    for (i = 0; i < child_count; i++)
      if (afrfdp->fdstate[i])
	break;

    if (i < child_count) {
      for (i = 0; i < child_count; i++) {
	if (pvt->state[i])
	  break;
      }
      if (i < child_count) {
	local->lock_node = children[i];
	STACK_WIND (frame,
		    afr_close_lock_cbk,
		    children[i],
		    children[i]->mops->lock,
		    path);
	return 0;
      }
    }
  }

  AFR_DEBUG_FMT (this, "self heal disabled or write was not done or fdstate[] is 0");
  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdsuccess[i])
      local->call_count++;
  }

  cnt = local->call_count;
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


STATIC int32_t
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
      GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
		afrfdp->path, prev_frame->this->name, op_ret, op_errno);
    }
    
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}

STATIC int32_t
afr_fsync (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   int32_t datasync)
{
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afrfd_t *afrfdp = data_to_ptr (dict_get(fd->ctx, this->name));

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

STATIC int32_t
afr_lk_cbk (call_frame_t *frame,
	    void *cookie,
	    xlator_t *this,
	    int32_t op_ret,
	    int32_t op_errno,
	    struct flock *lock)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;

  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  if (op_ret == -1) {
    afrfd_t *afrfdp = data_to_ptr (dict_get(local->fd->ctx, this->name));
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      afrfdp->path, prev_frame->this->name, op_ret, op_errno);
  }

  LOCK (&frame->lock);
  {
    if (op_ret == 0 && local->op_ret == -1) {
      local->op_ret = op_ret;
      local->lock = *lock;
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->lock);
  }

  return 0;
}


STATIC int32_t
afr_lk (call_frame_t *frame,
	xlator_t *this,
	fd_t *fd,
	int32_t cmd,
	struct flock *lock)
{
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afrfd_t *afrfdp = data_to_ptr (dict_get(fd->ctx, this->name));

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

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i])
      ++local->call_count;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "afrfdp->fdstate[] is 0, returning ENOTCONN");
    STACK_UNWIND (frame,
		  -1,
		  ENOTCONN,
		  NULL);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i]) {
      STACK_WIND(frame,
		 afr_lk_cbk,
		 children[i],
		 children[i]->fops->lk,
		 fd,
		 cmd,
		 lock);
    }
  }

  return 0;
}

STATIC int32_t
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
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d", 
	      prev_frame->this->name, op_ret, op_errno);
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

STATIC int32_t
afr_stat (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc)
{
  char *child_errno = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;
  afr_local_t *local = calloc (1, sizeof (*local));

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

STATIC int32_t
afr_statfs_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct statvfs *statvfs)
{
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i = 0, callcnt;
  afr_statfs_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;

  if (op_ret == -1 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

  LOCK (&frame->lock);
  {
    if (op_ret == 0) {
      local->op_ret = op_ret;
      /* we will return stat info from the first successful child */
      for (i = 0; i < child_count; i++) {
	if (children[i] == prev_frame->this) {
	  if (i < local->stat_child) {
	    local->statvfs = *statvfs;
	    local->stat_child = i;
	    break;
	  }
	}
      }
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0)
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->statvfs);

  return 0;
}

STATIC int32_t
afr_statfs (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i = 0;
  afr_statfs_local_t *local;

  local = calloc(1, sizeof(*local));
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


STATIC int32_t
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

/* FIXME increase the version count */

STATIC int32_t
afr_truncate (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      off_t offset)
{
  int32_t i = 0;
  char *child_errno = NULL;
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  AFR_DEBUG_FMT(this, "loc->path %s", loc->path);

  child_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->stat_child = child_count;

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

STATIC int32_t
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


STATIC int32_t
afr_utimens (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     struct timespec tv[2])
{
  char *child_errno = NULL;
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

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

STATIC int32_t
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
	afrfdp->fdstate = calloc (child_count, sizeof (char));
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

STATIC int32_t
afr_opendir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     fd_t *fd)
{
  char *child_errno = NULL;
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

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

STATIC int32_t
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

  AFR_DEBUG (this);

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    char *name = local->name;
    int len = strlen (name);
    afr_loc_free(local->loc);
    STACK_UNWIND (frame,
		  len,
		  0,
		  name);
    freee (name);
  }

  return 0;
}

STATIC int32_t
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

  AFR_DEBUG(this);

  child_errno = data_to_ptr (dict_get (local->loc->inode->ctx, this->name));

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == ENOENT)
      local->call_count++;
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


STATIC int32_t
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

STATIC int32_t
afr_readdir_cbk (call_frame_t *frame,
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
	    freee (tmp->name);
	    freee (tmp);
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
		  afr_readdir_cbk,
		  children[i],
		  children[i]->fops->readdir,
		  local->size,
		  local->offset,
		  local->fd);
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
	freee (trav->name);
	freee (trav);
	trav = prev->next;
	}
      freee (prev);
    }
  }
  freee (local);

  return 0;
}


STATIC int32_t
afr_readdir (call_frame_t *frame,
	     xlator_t *this,
	     size_t size,
	     off_t offset,
	     fd_t *fd)
{
  afrfd_t *afrfdp = NULL;
  afr_local_t *local = calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

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

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i]) {
      local->call_count = i + 1;
      STACK_WIND (frame, 
		  afr_readdir_cbk,
		  children[i],
		  children[i]->fops->readdir,
		  size,
		  offset,
		  fd);
      return 0;
    }
  }

  STACK_UNWIND (frame, -1, ENOTCONN, NULL, 0);

  return 0;
}

STATIC int32_t
afr_writedir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  afr_local_t *local = frame->local;
  int32_t callcnt;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

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


STATIC int32_t
afr_writedir (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t flags,
	      dir_entry_t *entries,
	      int32_t count)
{
  afr_local_t *local = calloc (1, sizeof (*local));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  afrfd_t *afrfdp = data_to_ptr(dict_get (fd->ctx, this->name));

  if (afrfdp == NULL) {
    freee (local);
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
		  afr_writedir_cbk,
		  children[i],
		  children[i]->fops->writedir,
		  fd,
		  flags,
		  entries,
		  count);
    }
  }

  return 0;
}

STATIC int32_t
afr_bg_setxattr_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;

  LOCK (&frame->lock);
  {
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    afr_loc_free (local->loc);
    STACK_DESTROY (frame->root);
  }

  return 0;
}

STATIC int32_t
afr_bg_setxattr (call_frame_t *frame, loc_t *loc, dict_t *dict)
{
  call_frame_t *setxattr_frame;
  afr_local_t *local = calloc (1, sizeof (*local));
  afr_private_t *pvt = frame->this->private;
  char *state = pvt->state;
  int32_t child_count = pvt->child_count, i;
  xlator_t **children = pvt->children;

  for (i = 0; i < child_count; i++) {
    if (state[i])
      local->call_count++;
  }

  if (local->call_count == 0) {
    freee (local);
    return 0;
  }

  setxattr_frame = copy_frame (frame);
  setxattr_frame->local = local;
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

STATIC int32_t
afr_mkdir_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       inode_t *inode,
	       struct stat *buf)
{
  int32_t callcnt, i;
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

  if (op_ret != -1)
    local->op_ret = op_ret;

  errno_data = dict_get (local->loc->inode->ctx, this->name);
  if (errno_data)
    child_errno = data_to_ptr (errno_data);

  LOCK (&frame->lock);
  {
    if (child_errno == NULL) {
      child_errno = calloc (child_count, sizeof(char));
      memset (child_errno, ENOTCONN, child_count);
      dict_set (local->loc->inode->ctx, this->name, data_from_dynptr(child_errno, child_count));
    }
    
    /* we will return stat info from the first successful child */
    for (i = 0; i < child_count; i++) {
      if (children[i] == prev_frame->this) {
	if (op_ret == 0) {
	  child_errno[i] = 0;
	  if (i < local->stat_child) {
	    local->stbuf = *buf;
	    local->stat_child = i;
	  }
	} else
	  child_errno[i] = op_errno;
      }
    }
    
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0){
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
      afr_incver_internal (frame, this, (char *)local->loc->path);
    }
    afr_loc_free(local->loc);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inoptr,
		  &local->stbuf);
  }

  return 0;
}

STATIC int32_t
afr_mkdir (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode)
{
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  xlator_list_t *trav = this->children;

  AFR_DEBUG_FMT(this, "path %s", loc->path);
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->loc = afr_loc_dup(loc);
  local->call_count = ((afr_private_t*)this->private)->child_count;
  local->stat_child = local->call_count;

  while (trav) {
    STACK_WIND (frame,
		afr_mkdir_cbk,
		trav->xlator,
		trav->xlator->fops->mkdir,
		loc,
		mode);
    trav = trav->next;
  }

  return 0;
}

STATIC int32_t
afr_unlink_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;

  AFR_DEBUG_FMT(this, "op_ret = %d", op_ret);
  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
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
      afr_incver_internal (frame, this, (char *) local->loc->path);
    afr_loc_free (local->loc);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}


STATIC int32_t
afr_unlink (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  char *child_errno = NULL;
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

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

STATIC int32_t
afr_rmdir_cbk (call_frame_t *frame,
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
      afr_incver_internal (frame, this, (char *)local->loc->path);
    afr_loc_free (local->loc);
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }

  return 0;
}


STATIC int32_t
afr_rmdir (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc)
{
  char *child_errno = NULL;
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

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


STATIC int32_t
afr_create_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		fd_t *fd,
		inode_t *inode,
		struct stat *stbuf)
{
  int32_t callcnt, i;
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

  if (op_ret == -1) {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", 
	      local->loc->path, prev_frame->this->name, op_ret, op_errno);
  }

  errno_data = dict_get (local->loc->inode->ctx, this->name);
  if (errno_data)
    child_errno = data_to_ptr (errno_data);

  LOCK (&frame->lock);
  {
    if (child_errno == NULL) {
      child_errno = calloc (child_count, sizeof(char));
      memset (child_errno, ENOTCONN, child_count);
      dict_set (inoptr->ctx, this->name, data_from_dynptr(child_errno, child_count));
    }
    if (op_ret >= 0) {
      afrfd_t *afrfdp;
      data_t *afrfdp_data;

      afrfdp_data = dict_get (fd->ctx, this->name);
      if (afrfdp_data == NULL) {
	afrfdp = calloc (1, sizeof (afrfd_t));
	afrfdp->fdstate = calloc (child_count, sizeof (char));
	afrfdp->fdsuccess = calloc (child_count, sizeof (char));
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
    callcnt = --local->call_count;
    
    /* we will return stat info from the first successful child */
    for (i = 0; i < child_count; i++) {
      if (children[i] == prev_frame->this) {
	if (op_ret >= 0) {
	  child_errno[i] = 0;
	  if (i < local->stat_child) {
	    local->stbuf = *stbuf;
	    local->stat_child = i;
	  }
	} else {
	  child_errno[i] = op_errno;
	}
	break;
      }
    }
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0){
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

      afr_incver_internal (frame, this, (char *)local->loc->path);
    }
    afr_loc_free(local->loc);
    AFR_DEBUG_FMT (this, "INO IS %d", local->stbuf.st_ino);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  fd,
		  inoptr,
		  &local->stbuf);
  }

  return 0;
}

STATIC int32_t
afr_create (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t flags,
	    mode_t mode,
	    fd_t *fd)
{
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = (afr_private_t *) this->private;
  xlator_t **children = pvt->children;
  int32_t i, child_count = pvt->child_count;

  AFR_DEBUG_FMT (this, "path = %s", loc->path);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->stat_child = child_count;
  local->fd = fd;
  local->loc = afr_loc_dup(loc);

  local->call_count = child_count;

  for (i = 0; i < child_count; i++) {
    STACK_WIND (frame,
		afr_create_cbk,
		children[i],
		children[i]->fops->create,
		loc,
		flags,
		mode,
		fd);
  }
  return 0;
}


STATIC int32_t
afr_mknod_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       inode_t *inode,
	       struct stat *stbuf)
{
  int32_t callcnt, i;
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

  if (op_ret == 0)
    local->op_ret = 0;

  errno_data = dict_get (local->loc->inode->ctx, this->name);
  if (errno_data)
    child_errno = data_to_ptr (errno_data);

  LOCK (&frame->lock);
  {
    if (child_errno == NULL) {
      child_errno = calloc (child_count, sizeof(char));
      memset (child_errno, ENOTCONN, child_count);
      dict_set (inoptr->ctx, this->name, data_from_dynptr(child_errno, child_count));
    }
    
    /* we will return stat info from the first successful child */
    for (i = 0; i < child_count; i++) {
      if (children[i] == prev_frame->this) {
	if (op_ret == 0) {
	  child_errno[i] = 0;
	  if (i < local->stat_child) {
	    local->stbuf = *stbuf;
	    local->stat_child = i;
	  }
	} else {
	  child_errno[i] = op_errno;
	}
      }
    }
    
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0){
    afr_incver_internal (frame, this, (char *) local->loc->path);
    afr_loc_free(local->loc);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inoptr,
		  &local->stbuf);
  }

  return 0;
}

STATIC int32_t
afr_mknod (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode,
	   dev_t dev)
{
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  xlator_list_t *trav = this->children;

  AFR_DEBUG(this);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->loc = afr_loc_dup(loc);
  local->call_count = ((afr_private_t*)this->private)->child_count;
  local->stat_child = local->call_count;
  trav = this->children;

  while (trav) {
    STACK_WIND (frame,
		afr_mknod_cbk,
		trav->xlator,
		trav->xlator->fops->mknod,
		loc,
		mode,
		dev);
    trav = trav->next;
  }

  return 0;
}


STATIC int32_t
afr_symlink_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *stbuf)
{
  int32_t callcnt, i;
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

  if (op_ret == 0)
    local->op_ret = 0;

  errno_data = dict_get (local->loc->inode->ctx, this->name);
  if (errno_data)
    child_errno = data_to_ptr (errno_data);

  LOCK (&frame->lock);
  {
    if (child_errno == NULL) {
      child_errno = calloc (child_count, sizeof(char));
      memset (child_errno, ENOTCONN, child_count);
      dict_set (inoptr->ctx, this->name, data_from_dynptr(child_errno, child_count));
    }
    
    /* we will return stat info from the first successful child */
    for (i = 0; i < child_count; i++) {
      if (children[i] == prev_frame->this) {
	if (op_ret == 0) {
	  child_errno[i] = 0;
	  if (i < local->stat_child) {
	    local->stbuf = *stbuf;
	    local->stat_child = i;
	  }
	} else {
	  child_errno[i] = op_errno;
	}
      }
    }
    
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0){
    if (local->op_ret == 0) {
      afr_incver_internal (frame, this, (char *)local->loc->path);
    }
    afr_loc_free(local->loc);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inoptr,
		  &local->stbuf);
  }

  return 0;
}

STATIC int32_t
afr_symlink (call_frame_t *frame,
	     xlator_t *this,
	     const char *linkname,
	     loc_t *loc)
{
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  xlator_list_t *trav = this->children;

  AFR_DEBUG_FMT(this, "linkname %s loc->path %s", linkname, loc->path);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->loc = afr_loc_dup(loc);
  local->call_count = ((afr_private_t*)this->private)->child_count;
  local->stat_child = local->call_count;
  trav = this->children;

  while (trav) {
    STACK_WIND (frame,
		afr_symlink_cbk,
		trav->xlator,
		trav->xlator->fops->symlink,
		linkname,
		loc);
    trav = trav->next;
  }

  return 0;
}

STATIC int32_t
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
    afr_incver_internal (frame, this, (char *) local->loc->path);
    afr_incver_internal (frame, this, (char *) local->loc2->path);
    afr_loc_free (local->loc);
    afr_loc_free (local->loc2);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  &local->stbuf);
  }

  return 0;
}


STATIC int32_t
afr_rename (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *oldloc,
	    loc_t *newloc)
{
  char *child_errno = NULL;
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  AFR_DEBUG_FMT(this, "oldloc->path %s newloc->path %s", oldloc->path, newloc->path);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOTCONN;
  local->stat_child = pvt->child_count;
  local->loc = afr_loc_dup(oldloc);
  local->loc2 = afr_loc_dup (newloc);

  child_errno = data_to_ptr (dict_get (oldloc->inode->ctx, this->name));
  for(i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      local->call_count++;
    }
  }

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

  return 0;
}

/* FIXME: see if the implementation is correct */
STATIC int32_t
afr_link_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      inode_t *inode,
	      struct stat *stbuf)
{
  int32_t callcnt, i;
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  inode_t *inoptr = local->loc->inode;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count;

  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno != ENOTCONN)
    local->op_errno = op_errno;

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
      local->op_ret = 0;
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);
  
  if (callcnt == 0){
    if (local->op_ret == 0) {
      afr_incver_internal (frame, this, (char *) local->path);
    }
    freee (local->path);
    afr_loc_free (local->loc);    
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inoptr,
		  &local->stbuf);
  }

  return 0;
}


STATIC int32_t
afr_link (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *oldloc,
	  const char *newpath)
{
  char *child_errno = NULL;
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  AFR_DEBUG_FMT(this, "oldloc->path %s newpath %s", oldloc->path, newpath);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->loc = afr_loc_dup(oldloc);
  local->path = strdup(newpath);
  local->stat_child = child_count;

  child_errno = data_to_ptr (dict_get (oldloc->inode->ctx, this->name));
  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0)
      ++local->call_count;
  }

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      STACK_WIND (frame,
		  afr_link_cbk,
		  children[i],
		  children[i]->fops->link,
		  oldloc,
		  newpath);
    }
  }

  return 0;
}

STATIC int32_t
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

STATIC int32_t
afr_chmod (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode)
{
  char *child_errno = NULL;
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

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

STATIC int32_t
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

STATIC int32_t
afr_chown (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   uid_t uid,
	   gid_t gid)
{
  char *child_errno = NULL;
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

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

STATIC int32_t
afr_closedir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  int32_t callcnt = 0;
  afr_local_t *local = frame->local;

  AFR_DEBUG_FMT(this, "op_ret = %d", op_ret);

  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
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
STATIC int32_t
afr_closedir (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  afrfd_t *afrfdp = NULL;
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  afr_private_t *pvt = this->private;
  /* FIXME this should be done under lock */
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  AFR_DEBUG_FMT(this, "fd = %p", fd);

  afrfdp = data_to_ptr(dict_get (fd->ctx, this->name));
  if (afrfdp == NULL) {
    freee (local);
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

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i]){
      STACK_WIND (frame,
		  afr_closedir_cbk,
		  children[i],
		  children[i]->fops->closedir,
		  fd);
    }
  }
  freee (afrfdp->fdstate);
  freee (afrfdp->path);
  freee (afrfdp);

  return 0;
}

STATIC int32_t
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


STATIC int32_t
afr_fchmod (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    mode_t mode)
{
  afr_local_t *local = calloc (1, sizeof (*local));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  afrfd_t *afrfdp = data_to_ptr(dict_get (fd->ctx, this->name));

  if (afrfdp == NULL) {
    freee (local);
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


STATIC int32_t
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

  if (callcnt == 0) {
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  &local->stbuf);
  }
  return 0;
}


STATIC int32_t
afr_fchown (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    uid_t uid,
	    gid_t gid)
{
  afr_local_t *local = calloc (1, sizeof (*local));
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  afrfd_t *afrfdp = data_to_ptr(dict_get (fd->ctx, this->name));

  if (afrfdp == NULL) {
    freee (local);
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
STATIC int32_t
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
STATIC int32_t
afr_access (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t mask)
{
  AFR_DEBUG(this);
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}

STATIC int32_t
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

STATIC int32_t
afr_lock (call_frame_t *frame,
	  xlator_t *this,
	  const char *path)
{
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  AFR_DEBUG(this);

  for (i = 0; i < child_count; i++) {
    if (pvt->state[i])
      break;
  }

  STACK_WIND (frame,
	      afr_lock_cbk,
	      children[i],
	      children[i]->mops->lock,
	      path);

  return 0;
}

STATIC int32_t
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

STATIC int32_t
afr_unlock (call_frame_t *frame,
	    xlator_t *this,
	    const char *path)
{
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i;

  AFR_DEBUG(this);

  for (i = 0; i < child_count; i++) {
    if (pvt->state[i])
      break;
  }

  STACK_WIND (frame,
	      afr_unlock_cbk,
	      children[i],
	      children[i]->mops->unlock,
	      path);

  return 0;
}


STATIC int32_t
afr_stats_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct xlator_stats *stats)
{
  afr_local_t *local = frame->local;

  AFR_DEBUG(this);

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


STATIC int32_t
afr_stats (call_frame_t *frame,
	   xlator_t *this,
	   int32_t flags)
{
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));

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


STATIC int32_t
afr_checksum_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  uint8_t *fchecksum,
		  uint8_t *dchecksum)
{
  afr_local_t *local = frame->local;

  AFR_DEBUG(this);

  if (op_ret != 0 && op_errno == ENOTCONN && local->xlnodeptr->next) {

    LOCK (&frame->lock);
    {
      local->xlnodeptr = local->xlnodeptr->next;
    }
    UNLOCK (&frame->lock);

    STACK_WIND (frame,
		afr_checksum_cbk,
		local->xlnodeptr->xlator,
		local->xlnodeptr->xlator->mops->checksum,
		local->loc,
		local->flags);

    return 0;
  }

  afr_loc_free (local->loc);
  STACK_UNWIND (frame, op_ret, op_errno, fchecksum, dchecksum);
  return 0;
}


STATIC int32_t
afr_checksum (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t flags)
{
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));

  AFR_DEBUG(this);

  frame->local = local;
  local->xlnodeptr = this->children;
  local->flags = flags;
  local->loc = afr_loc_dup(loc);

  STACK_WIND (frame,
	      afr_checksum_cbk,
	      local->xlnodeptr->xlator,
	      local->xlnodeptr->xlator->mops->checksum,
	      loc,
	      flags);

  return 0;
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
  afr_private_t *pvt = calloc (1, sizeof (afr_private_t));
  data_t *lock_node = dict_get (this->options, "lock-node");
  data_t *replicate = dict_get (this->options, "replicate");
  data_t *selfheal = dict_get (this->options, "self-heal");
  data_t *debug = dict_get (this->options, "debug");
  data_t *read_node = dict_get (this->options, "read-subvolume");
  /* change read_node to read_subvolume */
  data_t *read_schedule = dict_get (this->options, "read-schedule");
  xlator_list_t *trav = this->children;

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
  }

  /* by default self-heal is on */
  pvt->self_heal = 1;
  if (selfheal && strcmp(data_to_str(selfheal), "off") == 0) {
    GF_DEBUG (this, "self-heal is disabled");
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
	GF_ERROR (this, "read-node should be * or one among the sobvols");
	freee (pvt);
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
    freee (pvt);
    return -1;
  }
  if (read_schedule) {
    GF_ERROR (this, "please use read-node");
    freee (pvt);
    return -1;
  }
  if(replicate) {
    GF_ERROR (this, "\"option replicate\" is deprecated, it is no more supported. For more information please check http://www.mail-archive.com/gluster-devel@nongnu.org/msg02201.html (This message will be removed in future patches). Exiting!");
    freee (pvt);
    return -1;
  }

  /* pvt->children will have list of children which maintains its state (up/down) */
  pvt->children = calloc(pvt->child_count, sizeof(xlator_t*));
  pvt->state = calloc (pvt->child_count, sizeof(char));
  i = 0;
  trav = this->children;
  while (trav) {
    pvt->children[i++] = trav->xlator;
    trav = trav->next;
  }
  this->private = pvt;

  return 0;
}

void
fini (xlator_t *this)
{
  afr_private_t *priv = this->private;
  freee (priv);
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
  .checksum = afr_checksum,
};

