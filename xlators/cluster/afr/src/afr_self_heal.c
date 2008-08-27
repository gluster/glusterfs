/*
   Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>

#include "compat.h"
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
#include "compat.h"

extern int32_t
afr_lookup_unlock_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno);

extern loc_t* afr_loc_dup(loc_t *loc);

dict_t *afr_dict_new (xlator_t **children, char *fdsuccess, int child_count);

int32_t afr_lock_servers (call_frame_t *frame);

void afr_lookup_directory_selfheal (call_frame_t*);
int32_t afr_open (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  int32_t flags,
		  fd_t *fd);

int32_t
afr_unlock_servers (call_frame_t *frame);


int32_t
afr_lds_closedir_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
  int32_t callcnt;
  afr_local_t *local = frame->local;
  afr_selfheal_private_t *asp = local->asp;

  if (op_ret == -1) {
    asp->error = 1;
    GF_ERROR (this, "op_ret=-1 op_errno=%d", op_errno);
  }
  LOCK(&frame->lock);
  callcnt = --local->call_count;
  UNLOCK(&frame->lock);

  if (callcnt == 0)
    afr_lookup_directory_selfheal (frame);
  return 0;
}

int32_t
afr_lds_setxattr_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
  int32_t callcnt;
  afr_local_t *local = frame->local;
  afr_selfheal_private_t *asp = local->asp;

  if (op_ret == -1) {
    asp->error = 1;
    GF_ERROR (this, "op_ret=-1 op_errno=%d", op_errno);
  }

  LOCK(&frame->lock);
  callcnt = --local->call_count;
  UNLOCK(&frame->lock);

  if (callcnt == 0)
    afr_lookup_directory_selfheal (frame);
  return 0;
}

int32_t
afr_lds_setdents_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
  int32_t callcnt;
  afr_local_t *local = frame->local;
  afr_selfheal_private_t *asp = local->asp;

  if (op_ret == -1) {
    asp->error = 1;
    GF_ERROR (this, "op_ret=-1 op_errno=%d", op_errno);
  }

  LOCK(&frame->lock);
  callcnt = --local->call_count;
  UNLOCK(&frame->lock);

  if (callcnt == 0)
    afr_lookup_directory_selfheal (frame);
  return 0;
}

int32_t
afr_lds_rmelem_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  int32_t callcnt;
  afr_local_t *local = frame->local;
  afr_selfheal_private_t *asp = local->asp;

  if (op_ret == -1) {
    asp->error = 1;
    GF_ERROR (this, "op_ret=-1 op_errno=%d", op_errno);
  }
  LOCK(&frame->lock);
  callcnt = --local->call_count;
  UNLOCK(&frame->lock);

  if (callcnt == 0) {
    dir_entry_t *entry;
    for (entry = asp->entries; entry;) {
      dir_entry_t *temp;
      temp = entry;
      entry = temp->next;
      if (temp->name)
	FREE (temp->name);
      FREE (temp);
    }
    afr_lookup_directory_selfheal (frame);
  }
  return 0;
}

int32_t
afr_lds_lookup_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf,
		    dict_t *xattr)
{
  int32_t callcnt;
  afr_local_t *local = frame->local;
  afr_selfheal_private_t *asp = local->asp;
  dir_entry_t *entry = asp->entries;

  if (inode != NULL)
    inode_unref (inode);
  else
    GF_ERROR (this, "inode is NULL");
  if (op_ret == -1 && op_errno != ENOENT) {
    asp->error = 1;
    GF_ERROR (this, "op_ret=-1 op_errno=%d", op_errno);
  }
  LOCK(&frame->lock);
  if (op_ret == 0) {
    if (entry == cookie) {
      FREE (entry->name);
      entry->name = NULL;
    } else {
      while (entry) {
	if (entry->next == cookie) {
	  FREE (entry->next->name);
	  entry->next->name = NULL;
	  break;
	}
	entry = entry->next;
      }
    }
  }
  callcnt = --local->call_count;
  UNLOCK(&frame->lock);
  if (callcnt == 0)
    afr_lookup_directory_selfheal (frame);
  return 0;
}

int32_t
afr_lds_getdents_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      dir_entry_t *entry,
		      int32_t count)
{
  afr_local_t *local = frame->local;
  afr_selfheal_private_t *asp = local->asp;

  if (op_ret == -1) {
    asp->error = 1;
    GF_ERROR (this, "op_ret=-1 op_errno=%d", op_errno);
  } else
    asp->dents_count = count;
  if (count > 0) {
    asp->entries = entry->next;
    entry->next = NULL;
  }
  afr_lookup_directory_selfheal(frame);
  return 0;
}

int32_t
afr_lds_opendir_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     fd_t *fd)
{
  int32_t callcnt;
  afr_local_t *local = frame->local;
  afr_selfheal_private_t *asp = local->asp;

  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (op_ret == -1) {
    asp->error = 1;
    GF_ERROR (this, "op_ret=-1 op_errno=%d", op_errno);
  }

  if (callcnt == 0) {
    afr_lookup_directory_selfheal (frame);
  }
  return 0;
}

void afr_lookup_directory_selfheal(call_frame_t *frame)
{
  afr_private_t *pvt = frame->this->private;
  afr_local_t *local = frame->local;
  afr_selfheal_private_t *asp = local->asp;
  int32_t child_count = pvt->child_count, i;
  afr_selfheal_t *ashptr = local->ashptr;
  xlator_t **children = pvt->children;
  xlator_t *this = frame->this;
  dir_entry_t *entry;
  int32_t latest = local->latest;
  int32_t cnt = 0, len = 0;
  char DEBUG1[BUFSIZ] = {0,}, DEBUG2[BUFSIZ] = {0,};

  local = frame->local;
  asp = local->asp;

  if (asp == NULL)
    goto AFR_SELFHEAL_1_GOTO;

  i = asp->i;

  switch (asp->label) {
  case AFR_SELFHEAL_2:
    goto AFR_SELFHEAL_2_GOTO;
  case AFR_SELFHEAL_3:
    goto AFR_SELFHEAL_3_GOTO;
  case AFR_SELFHEAL_4:
    goto AFR_SELFHEAL_4_GOTO;
  case AFR_SELFHEAL_5:
    goto AFR_SELFHEAL_5_GOTO;
  case AFR_SELFHEAL_6:
    goto AFR_SELFHEAL_6_GOTO;
  case AFR_SELFHEAL_7:
    goto AFR_SELFHEAL_7_GOTO;
  case AFR_SELFHEAL_8:
    goto AFR_SELFHEAL_8_GOTO;
  case AFR_SELFHEAL_9:
    goto AFR_SELFHEAL_9_GOTO;
  default:
    goto AFR_ERROR;
  }

 AFR_SELFHEAL_1_GOTO:
  GF_TRACE (this, "at LABEL1");
  /* FIXME: free asp */
  asp = calloc (1, sizeof(*asp));
  ERR_ABORT (asp);
  asp->loc = calloc (1, sizeof (loc_t));
  ERR_ABORT (asp->loc);

  local->asp = asp;
  DEBUG2[0] = '\0';
  for (i = 0; i < child_count; i++) {
    if (ashptr[i].repair) {
      strcat (DEBUG2, " ");
      strcat (DEBUG2, children[i]->name);
    }

    if (i == local->latest || ashptr[i].repair)
      local->call_count++;
  }

  GF_TRACE (this, "Latest on %s, repair on %s, call_count %d", children[local->latest]->name, DEBUG2, local->call_count);
  asp->label = AFR_SELFHEAL_2;
  for (i = 0; i < child_count; i++) {
    if (i == local->latest || ashptr[i].repair) {
      STACK_WIND (frame,
		  afr_lds_opendir_cbk,
		  children[i],
		  children[i]->fops->opendir,
		  local->loc,
		  local->fd);
    }
  }
  return;
 AFR_SELFHEAL_2_GOTO:
  GF_TRACE (this, "at LABEL_2");
  if (asp->error)
    goto AFR_ERROR;
  for (i = 0; i < child_count; i++) {
    local->offset = 0;
    if (ashptr[i].repair) {
      while (1) {
	asp->i = i;
	asp->label = AFR_SELFHEAL_3;
	STACK_WIND (frame,
		    afr_lds_getdents_cbk,
		    children[i],
		    children[i]->fops->getdents,
		    local->fd,
		    100,
		    0,
		    0);
	return;
 AFR_SELFHEAL_3_GOTO:
	GF_TRACE (this, "at LABEL_3");
	if (asp->error)
	  goto AFR_ERROR;
	/* all the contents read */
	if (asp->dents_count == 0) {
	  GF_TRACE (this, "dents_count = 0, all conents read from %s, breaking.", children[i]->name);
	  break;
	}
	DEBUG1[0] = '\0';
	len = 0;
	asp->label = AFR_SELFHEAL_4;
	for (entry = asp->entries; entry; entry = entry->next) {
	  local->call_count++;

	  /* for debug info only */
	  len += strlen (entry->name) + 1;
	  if (len + 1 >= BUFSIZ) {
	    GF_TRACE (this, "getdents contents: %s", DEBUG1);
	    len = 0;
	    DEBUG1[0] = '\0';
	  }
	  strcat (DEBUG1, entry->name);
	  strcat (DEBUG1, " ");
	}
	cnt = local->call_count;
	if (len) {
	  GF_TRACE (this, "getdents contents: %s", DEBUG1);
	}
	GF_TRACE (this, "call_count = %d", cnt);
	for (entry = asp->entries; entry; entry = entry->next) {
	  char path[PATH_MAX];
	  strcpy (path, local->loc->path);
	  strcat (path, "/");
	  strcat (path, entry->name);
	  asp->loc->path = path;
	  asp->loc->inode = inode_new (local->loc->inode->table);
	  /* inode structure will be freed in the call back */
	  STACK_WIND_COOKIE (frame,
			     afr_lds_lookup_cbk,
			     entry,
			     children[local->latest],
			     children[local->latest]->fops->lookup,
			     asp->loc,
			     0);
	  if (--cnt == 0)
	    break;
	}
	return;
 AFR_SELFHEAL_4_GOTO:
	GF_TRACE (this, "at LABEL_4");
	if (asp->error)
	  goto AFR_ERROR;
	DEBUG1[0] = '\0';
	len = 0;
	asp->label = AFR_SELFHEAL_5;
	for (entry = asp->entries; entry; entry = entry->next) {
	  if (entry->name == NULL)
	    continue;
	  local->call_count++;

	  /* for debug info only */
	  len += strlen (entry->name) + 1;
	  if (len + 1 >= BUFSIZ) {
	    GF_TRACE (this, "rmelem on %s: %s", children[i]->name, DEBUG1);
	    len = 0;
	    DEBUG1[0] = '\0';
	  }
	  strcat (DEBUG1, entry->name);
	  strcat (DEBUG1, " ");
	}
	if (local->call_count == 0) {
	  GF_TRACE (this, "nothing to rmelem in this loop");
	  continue;
	}

	cnt = local->call_count;
	if (len) {
	  GF_TRACE (this, "rmelem contents: %s", DEBUG1);
	}
	GF_TRACE (this, "call_count = %d", cnt);
	for (entry = asp->entries; entry; entry = entry->next) {
	  char path[PATH_MAX];
	  if (entry->name == NULL)
	    continue;
	  strcpy (path, local->loc->path);
	  strcat (path, "/");
	  strcat (path, entry->name);
	  STACK_WIND (frame,
		      afr_lds_rmelem_cbk,
		      children[i],
		      children[i]->fops->rmelem,
		      path);
	  if (--cnt == 0)
	    break;
	}
	return;
 AFR_SELFHEAL_5_GOTO:
	GF_TRACE (this, "at LABEL_5");
	if (asp->error)
	  goto AFR_ERROR;
      }
    }
  }

  while (1) {
    asp->label = AFR_SELFHEAL_6;
    STACK_WIND (frame,
		afr_lds_getdents_cbk,
		children[local->latest],
		children[local->latest]->fops->getdents,
		local->fd,
		100,
		0,
		0);
    return;
 AFR_SELFHEAL_6_GOTO:
    GF_TRACE (this, "at LABEL_6");
    if (asp->error)
      goto  AFR_ERROR;
    if (asp->dents_count == 0)
      break;
    asp->label = AFR_SELFHEAL_7;
    for (i = 0; i < child_count; i++) {
      if (ashptr[i].repair)
	local->call_count++;
    }
    int32_t count = 0, cnt;
    for (entry = asp->entries; entry; entry = entry->next)
      count++;
    cnt = local->call_count;
    GF_TRACE (this, "calling setdents(), number of entries is %d", count);
    dir_entry_t *tmpdir = calloc (1, sizeof (*tmpdir));
    ERR_ABORT (tmpdir);
    tmpdir->next = asp->entries;
    asp->entries = tmpdir;
    for (i = 0; i < child_count; i++) {
      if (ashptr[i].repair) {
	STACK_WIND (frame,
		    afr_lds_setdents_cbk,
		    children[i],
		    children[i]->fops->setdents,
		    local->fd,
		    GF_SET_IF_NOT_PRESENT | GF_SET_EPOCH_TIME,
		    asp->entries,
		    count);
	if (--cnt == 0)
	  break;
      }
    }
    for (entry = tmpdir->next; entry;) {
      dir_entry_t *temp;
      temp = entry;
      entry = temp->next;
      FREE (temp->name);
      FREE (temp);
    }
    FREE (tmpdir);

    return;
 AFR_SELFHEAL_7_GOTO:
    GF_TRACE (this, "at LABEL_7");
    if (asp->error)
      goto AFR_ERROR;
  }

  {
    dict_t *latest_xattr;
    char *version_str, *ctime_str;

    latest_xattr = get_new_dict();
    asprintf (&version_str, "%u", ashptr[latest].version);
    asprintf (&ctime_str, "%u", ashptr[latest].ctime);
    dict_set (latest_xattr, GLUSTERFS_VERSION, data_from_dynptr (version_str, strlen(version_str)));
    dict_set (latest_xattr, GLUSTERFS_CREATETIME, data_from_dynptr (ctime_str, strlen(ctime_str)));

    GF_TRACE (this, "latest ctime/version = %s/%s", ctime_str, version_str);

    for (i = 0; i < child_count; i++) {
      if (ashptr[i].repair)
	local->call_count++;
    }
    cnt = local->call_count;
    asp->label = AFR_SELFHEAL_8;
    for (i = 0; i < child_count; i++) {
      if (ashptr[i].repair) {
	STACK_WIND (frame,
		    afr_lds_setxattr_cbk,
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
    return;
  }

 AFR_SELFHEAL_8_GOTO:
  GF_TRACE (this, "at LABEL_8");
  if (asp->error)
    goto AFR_ERROR;

  for (i = 0; i < child_count; i++) {
    if (ashptr[i].repair || i == latest)
      local->call_count++;
  }
  asp->label = AFR_SELFHEAL_9;
  cnt = local->call_count;
  for (i = 0; i < child_count; i++) {
    if (ashptr[i].repair || i == latest) {
      STACK_WIND (frame,
		  afr_lds_closedir_cbk,
		  children[i],
		  children[i]->fops->closedir,
		  local->fd);
      if (--cnt == 0)
	break;
    }
  }
  return;
 AFR_SELFHEAL_9_GOTO:
  GF_TRACE (this, "at LABEL_8");
  if (asp->error)
    goto AFR_ERROR;

  goto AFR_SUCCESS;
 AFR_ERROR:
  GF_TRACE (this, "at AFR_ERROR");
  local->rmelem_status = 1;
 AFR_SUCCESS:
  GF_TRACE (this, "at AFR_SUCCESS");
  if (asp) {
    FREE (asp->loc);
    FREE (asp);
  }
  char *lock_path;
  asprintf (&lock_path, "/%s%s", local->lock_node->name, local->loc->path);
  STACK_WIND (frame,
	      afr_lookup_unlock_cbk,
	      local->lock_node,
	      local->lock_node->mops->unlock,
	      lock_path);
  return;
}

int32_t
afr_open_xattrop_get_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  dict_t *xattr)
{
  afr_lock_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i, callcnt;
  call_frame_t *prev_frame = cookie;

  local = frame->local;

  GF_TRACE (this, "(child=%s) (op_ret=%d op_errno=%d)", prev_frame->this->name,
	    op_ret, op_errno);

  LOCK (&frame->lock);
  callcnt = --local->call_count;
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) (op_ret=-1, op_errno=%d(%s))", prev_frame->this->name, op_errno, strerror(op_errno));
    local->error = 1;
  } else {
      data_pair_t *trav = xattr->members_list;
      while (trav) {
	int ver = data_to_int32 (trav->value);
	char *child = trav->key;
	child = child + strlen ("trusted.glusterfs.afr.");
	for (i = 0; i < child_count; i++) {
	  if (strcmp (child, children[i]->name) == 0)
	    break;
	}
	GF_TRACE (this, "child/ver=%s/%d", child, ver);
	local->writecnt[i] += ver;
	trav = trav->next;
      }
  }
  UNLOCK (&frame->lock);
  if (callcnt == 0)
    afr_open (frame, this, local->loc, local->flags, local->fd);
  return 0;
}

int32_t
afr_open_readv_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct iovec *vector,
		    int32_t count,
		    struct stat *stat);

int32_t
afr_open_readv_writev_cbk (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno,
			   struct stat *stat)
{
  afr_lock_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t callcnt;
  call_frame_t *prev_frame = cookie;
  afrfd_t *afrfdp;

  GF_TRACE (this, "(child=%s) (op_ret=%d op_errno=%d)", prev_frame->this->name,
	    op_ret, op_errno);

  local = frame->local;

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) (op_ret=%d op_errno=%d(%s))", prev_frame->this->name,
	      op_ret, op_errno, strerror(op_errno));
    local->error = 1;
  }

  afrfdp = data_to_ptr (dict_get (local->shfd->ctx, this->name));

  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
    if (local->error) {
      afr_open (frame, this, local->loc, local->flags, local->fd);
      return 0;
    }
    STACK_WIND (frame,
		afr_open_readv_cbk,
		children[local->latest],
		children[local->latest]->fops->readv,
		local->shfd,
		local->size,
		local->offset);

  }
  return 0;
}

int32_t
afr_open_readv_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct iovec *vector,
		    int32_t count,
		    struct stat *stat)
{
  afr_lock_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i, cnt;
  call_frame_t *prev_frame = cookie;
  afrfd_t *afrfdp;
  off_t offset;

  GF_TRACE (this, "(child=%s) (op_ret=%d op_errno=%d)", prev_frame->this->name, op_ret, op_errno);

  local = frame->local;
  afrfdp = data_to_ptr (dict_get (local->shfd->ctx, this->name));

  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) (op_ret=%d op_errno=%d(%s))", prev_frame->this->name, op_ret, op_errno, strerror(op_errno));
    local->error = 1;
    afr_open (frame, this, local->loc, local->flags, local->fd);
    return 0;
  }

  if (op_ret == 0) {
    GF_TRACE (this, "sync done, readv returning");
    local->stbuf = *stat;
    afr_open (frame, this, local->loc, local->flags, local->fd);
    return 0;
  }

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i] && local->writecnt[i]) {
      local->call_count++;
    }
  }
  cnt = local->call_count;
  if (cnt == 0) {
    GF_ERROR (this, "nowhere to write!");
    local->error = 1;
  }
  offset = local->offset;
  local->offset = offset + op_ret;
  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i] && local->writecnt[i]) {
      STACK_WIND (frame,
		  afr_open_readv_writev_cbk,
		  children[i],
		  children[i]->fops->writev,
		  local->shfd,
		  vector,
		  count,
		  offset);
      if (--cnt == 0)
	break;
    }
  }
  return 0;
}


int32_t
afr_open_xattrop_reset_cbk (call_frame_t *frame,
			    void *cookie,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno,
			    dict_t *xattr)
{
  afr_lock_local_t *local = NULL;
  int32_t  callcnt;
  call_frame_t *prev_frame = cookie;

  local = frame->local;

  GF_TRACE (this, "(child=%s) (op_ret=%d op_errno=%d)", prev_frame->this->name,
	    op_ret, op_errno);
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) (op_ret=%d op_errno=%d(%s))", prev_frame->this->name,
	      op_ret, op_errno, strerror(op_errno));
    local->error = 1;
  }
  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
    afr_open (frame, this, local->loc, local->flags, local->fd);
  }
  return 0;
}

int32_t
afr_open_utimens_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  struct stat *stat)
{
  afr_open_xattrop_reset_cbk (frame, cookie, this, op_ret, op_errno, NULL);
  return 0;
}

int32_t
afr_open_ftruncate_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			struct stat *stat)
{
  afr_lock_local_t *local = NULL;
  int32_t  callcnt;
  call_frame_t *prev_frame = cookie;

  local = frame->local;

  GF_TRACE (this, "(child=%s) (op_ret=%d op_errno=%d)", prev_frame->this->name,
	    op_ret, op_errno);
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) (op_ret=%d op_errno=%d(%s))", prev_frame->this->name,
	      op_ret, op_errno, strerror(op_errno));
    local->error = 1;
  }
  LOCK (&frame->lock);
  callcnt = -- local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
    afr_open (frame, this, local->loc, local->flags, local->fd);
  }
  return 0;
}

int32_t
afr_open_close_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  afr_lock_local_t *local = NULL;
  int32_t  callcnt;
  call_frame_t *prev_frame = cookie;

  local = frame->local;

  GF_TRACE (this, "(child=%s) (op_ret=%d op_errno=%d)", prev_frame->this->name,
	    op_ret, op_errno);
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) (op_ret=%d op_errno=%d(%s))", prev_frame->this->name,
	      op_ret, op_errno, strerror(op_errno));
    local->error = 1;
  }
  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
    afr_open (frame, this, local->loc, local->flags, local->fd);
  }
  return 0;
}

int32_t
afr_open_open_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   fd_t *fd)
{
  afr_lock_local_t *local = NULL;
  fd_t *usefd;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i, callcnt;
  call_frame_t *prev_frame = cookie;
  afrfd_t *afrfdp;

  GF_TRACE (this, "(child=%s) op_ret=%d op_errno=%d", prev_frame->this->name, op_ret, op_errno);
  local = frame->local;
  if (op_ret == -1) {
    GF_ERROR (this, "(child=%s) op_ret=%d op_errno=%d", prev_frame->this->name, op_ret, op_errno);
    if (local->sh)
      local->error = 1;
  }
  if (local->sh)
    usefd = local->shfd;
  else
    usefd = local->fd;
  afrfdp = data_to_ptr (dict_get (usefd->ctx, this->name));

  for (i = 0; i < child_count; i++) {
    if (prev_frame->this == children[i])
      break;
  }
  afrfdp->fdstate[i] = afrfdp->fdsuccess[i] = 1;
  LOCK (&frame->lock);
  callcnt = --local->call_count;
  if (op_ret != -1) {
    local->op_ret = 0;
  }
  if (op_ret == -1 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    local->sh = 0;
    afr_open (frame, this, local->loc, local->flags, local->fd);
  }
  return 0;
}

/*

selfheal algorithm: each label indicates a logical "step"

AFR_OPEN_1:
  open() on all the subvols
AFR_OPEN_2:
  gf_lk() from 0 to MAX on the file
AFR_OPEN_3:
  xattrop() - get xattrs from the subvols on the file
AFR_OPEN_4:
  3 possibilities - splitbrain, needsheal, allfine
  if (splitbrain)
    goto AFR_OPEN_ERROR and let sysadmin set things right
  if (allfine)
    goto AFR_OPEN_8
  needsheal:
    call open(O_TRUNC) on outdated subvols and open(RD_ONLY) on latest;
AFR_OPEN_5:
    sync() the outdated files
AFR_OPEN_6:
    reset the xattrs on the outdated files and set mtime of changed files
AFR_OPEN_7:
    close() the fds opened for selfheal.
AFR_OPEN_8:
    gf_unlk()
AFR_OPEN_9:
    free() the malloc'ed memory
    STACK_UNWIND (success)
AFR_OPEN_ERROR:
    (error handling/cleanup)
    gf_unlk() if not yet unlocked
AFR_OPEN_10:
    close() on fd opened for selfheal as well as the fd opened by application.
AFR_OPEN_11:
    free() the malloc'ed memory
    STACK_UNWIND (failure)
*/

int32_t
afr_open (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  int32_t flags,
	  fd_t *fd)
{
  afr_lock_local_t *local = NULL;
  afr_private_t *pvt = this->private;
  xlator_t **children = pvt->children;
  int32_t child_count = pvt->child_count, i, cnt, latest;
  afrfd_t *afrfdp = NULL, *shafrfdp = NULL;
  char *child_errno;
  dict_t *xattr;
  struct timespec ts[2];

  GF_TRACE (this, "-----");

  local = frame->local;
  child_errno = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  afrfdp = data_to_ptr (dict_get (fd->ctx, this->name));

  if (local && local->shfd) {
    shafrfdp = data_to_ptr (dict_get (local->shfd->ctx, this->name));
  }

  if (local == NULL) {
    frame->local = local = calloc (1, sizeof (*local));
    local->loc = afr_loc_dup (loc);
    local->flags = flags;
    local->writecnt = calloc (child_count, sizeof (char));
    local->op_ret = -1;
    local->op_errno = ENOTCONN;
    local->offset = 0;
    local->length = LLONG_MAX;
    local->flags = flags;
    local->fd = fd;
    local->uid = frame->root->uid;
    local->gid = frame->root->gid;
    afrfdp = calloc (1, sizeof (*afrfdp));
    afrfdp->fdstate = calloc (child_count, sizeof (char));
    afrfdp->fdsuccess = calloc (child_count, sizeof (char));
    afrfdp->path = strdup (loc->path);
    dict_set (fd->ctx, this->name, data_from_static_ptr (afrfdp));    
    local->label = AFR_OPEN_1;
  }

  switch (local->label) {
  case AFR_OPEN_1:
    goto AFR_OPEN_1_GOTO;
  case AFR_OPEN_2:
    goto AFR_OPEN_2_GOTO;
  case AFR_OPEN_3:
    goto AFR_OPEN_3_GOTO;
  case AFR_OPEN_4:
    goto AFR_OPEN_4_GOTO;
  case AFR_OPEN_5:
    goto AFR_OPEN_5_GOTO;
  case AFR_OPEN_6:
    goto AFR_OPEN_6_GOTO;
  case AFR_OPEN_7:
    goto AFR_OPEN_7_GOTO;
  case AFR_OPEN_8:
    goto AFR_OPEN_8_GOTO;
  case AFR_OPEN_9:
    goto AFR_OPEN_9_GOTO;
  case AFR_OPEN_10:
    goto AFR_OPEN_10_GOTO;
  case AFR_OPEN_11:
    goto AFR_OPEN_11_GOTO;
  default:
    GF_ERROR (this, "default case should not be reached");
  }

 AFR_OPEN_1_GOTO:
  GF_TRACE(this, "AFR_OPEN_1_GOTO");

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0)
      local->call_count++;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "local->call_count is 0");
    goto AFR_OPEN_ERROR;
  }
  cnt = local->call_count;
  local->label = AFR_OPEN_2;

  for (i = 0; i < child_count; i++) {
    if (child_errno[i] == 0) {
      STACK_WIND (frame,
		  afr_open_open_cbk,
		  children[i],
		  children[i]->fops->open,
		  local->loc,
		  local->flags,
		  local->fd);
      if (--cnt == 0)
	break;
    }
  }
  return 0;

 AFR_OPEN_2_GOTO:
  GF_TRACE(this, "AFR_OPEN_2_GOTO");
  if (local->error) {
    GF_ERROR (this, "error during opening");
    goto AFR_OPEN_ERROR;
  }
  /* if one child up, optimize here */

  local->label = AFR_OPEN_3;
  local->returnfn = afr_open;
  local->locked = calloc (1, child_count);
  afr_lock_servers (frame);
  return 0;
 AFR_OPEN_3_GOTO:
  GF_TRACE (this, "AFR_OPEN_3_GOTO");
  cnt = 0;
  if (local->error) {
    for (i = 0; i < child_count; i++) {
      if (local->locked[i] == ENOTCONN)
	cnt++;
    }
    for (i = 0; i < child_count; i++) {
      if (afrfdp->fdstate[i])
	cnt--;
    }
    if (cnt == 0) {
      local->op_errno = ENOTCONN;
      GF_ERROR (this, "error during locking");
      goto AFR_OPEN_ERROR;
    }
    GF_TRACE (this, "locking failed, retrying");
    local->error = 0;
    memset (local->locked, 0, child_count);
    local->unlock = 0;
    afr_lock_servers (frame);
    return 0;
  }

  local->lock = 1;
  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i])
      local->call_count++;
  }

  if (local->call_count == 0) {
    GF_ERROR (this, "local->call_count is 0, none of the open succeeded?");
    goto AFR_OPEN_ERROR;
  }

  cnt = local->call_count;
  local->label = AFR_OPEN_4;

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i]) {
      afrfdp->fdstate[i] = 0;
      xattr = afr_dict_new (children, afrfdp->fdstate, child_count);
      afrfdp->fdstate[i] = 1;
      STACK_WIND (frame,
		  afr_open_xattrop_get_cbk,
		  children[i],
		  children[i]->fops->xattrop,
		  local->fd,
		  "",
		  GF_XATTROP_GET,
		  xattr);
      dict_destroy (xattr);
      if (--cnt == 0)
	break;
    }
  }
  return 0;
 AFR_OPEN_4_GOTO:
  GF_TRACE (this, "AFR_OPEN_4_GOTO");
  if (local->error) {
    GF_ERROR (this, "error during xattrop(GET)");
    goto AFR_OPEN_ERROR;
  }
  latest = -1;
  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i] && local->writecnt[i] == 0) {
      latest = i;
      break;
    }
  }

  if (latest == -1) {
    GF_ERROR (this, "must be split brain case, not healing!");
    goto AFR_OPEN_ERROR;
  }

  local->latest = latest;

  cnt = 0;
  for (i = 0; i < child_count; i++) {
    if (local->writecnt[i])
      cnt++;
  }
  if (cnt == 0) {
    GF_DEBUG (this, "none of the subvols need healing.");
    goto AFR_OPEN_8_GOTO;
  }

  /* selfheal */
  GF_TRACE (this, "needs selfheal");
  GF_TRACE (this, "latest = %s", children[local->latest]->name);
  for (i = 0; i < child_count; i++) {
    if (local->writecnt[i])
      GF_TRACE (this, "needs selfheal = %s", children[i]->name);
  }

  local->shfd = calloc (1, sizeof (fd_t));
  local->shfd->ctx = get_new_dict();
  shafrfdp = calloc (1, sizeof (*afrfdp));
  shafrfdp->fdstate = calloc (child_count, sizeof (char));
  shafrfdp->fdsuccess = calloc (child_count, sizeof (char));
  shafrfdp->path = strdup (local->loc->path);
  dict_set (local->shfd->ctx, this->name, data_from_static_ptr (shafrfdp));
  local->shfd->inode = local->loc->inode;

  for (i = 0; i < child_count; i++) {
    if (i == local->latest || local->writecnt[i])
      local->call_count++;
  }
  cnt = local->call_count;
  local->label = AFR_OPEN_5;
  local->sh = 1;
  local->shclose = 1;
  frame->root->uid = frame->root->gid = 0;
  for (i = 0; i < child_count; i++) {
    if (i == local->latest || local->writecnt[i]) {
      if (i == local->latest) {
	GF_TRACE (this, "reopening on latest=%s for readv", children[i]->name);
	flags = O_RDONLY;
      } else {
	GF_TRACE (this, "reopening outdated=%s for writev", children[i]->name);
	flags = O_TRUNC | O_RDWR;
      }
      STACK_WIND (frame,
		  afr_open_open_cbk,
		  children[i],
		  children[i]->fops->open,
		  local->loc,
		  flags,
		  local->shfd);
      if (--cnt == 0)
	break;
    }
  }
  return 0;

 AFR_OPEN_5_GOTO:
  GF_TRACE (this, "AFR_OPEN_5_GOTO");
  local->label = AFR_OPEN_6;
  local->size = 128 * 1024;
  local->offset = 0;
  /* do the sync */
  GF_TRACE (this, "starting selfheal");
  STACK_WIND (frame,
	      afr_open_readv_cbk,
	      children[local->latest],
	      children[local->latest]->fops->readv,
	      local->shfd,
	      local->size,
	      local->offset);
  return 0;

 AFR_OPEN_6_GOTO:
  GF_TRACE (this, "AFR_OPEN_6_GOTO");
  if (local->error) {
    GF_ERROR (this, "error occured during sync()ing");
    goto AFR_OPEN_ERROR;
  }

  local->label = AFR_OPEN_7;
  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i])
      local->call_count += 2;
  }
  cnt = local->call_count;
  if (local->call_count == 0) {
    GF_ERROR (this, "local->call_count is 0");
    goto AFR_OPEN_ERROR;
  }

  xattr = afr_dict_new (children, local->writecnt, child_count);
  ts[0].tv_sec = local->stbuf.st_atime;
  ts[0].tv_nsec = 0;
  ts[1].tv_sec = local->stbuf.st_mtime;
  ts[1].tv_nsec = 0;

  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdstate[i]) {
      STACK_WIND (frame,
		  afr_open_xattrop_reset_cbk,
		  children[i],
		  children[i]->fops->xattrop,
		  local->fd,
		  "",
		  GF_XATTROP_RESET,
		  xattr);
      STACK_WIND (frame,
		  afr_open_utimens_cbk,
		  children[i],
		  children[i]->fops->utimens,
		  local->loc,
		  ts);
      if (--cnt == 0)
	    break;
    }
  }
  dict_destroy (xattr);
  return 0;

 AFR_OPEN_7_GOTO:
  GF_TRACE (this, "AFR_OPEN_7_GOTO");
  if (local->error) {
    GF_ERROR (this, "error during xattrop/RESET or utimens");
    goto AFR_OPEN_ERROR;
  }
  cnt = 0;
  for (i = 0; i < child_count; i++) {
    if (shafrfdp->fdsuccess[i])
      cnt++;
  }

  if (cnt == 0) {
    GF_ERROR (this, "none of subvols are up");
    goto AFR_OPEN_ERROR;
  }
  local->call_count = cnt;
  local->label = AFR_OPEN_8;
  for (i = 0; i < child_count; i++) {
    if (shafrfdp->fdsuccess[i]) {
      STACK_WIND (frame,
		  afr_open_close_cbk,
		  children[i],
		  children[i]->fops->close,
		  local->shfd);
      if (--cnt == 0)
	break;
    }
  }
  return 0;

 AFR_OPEN_8_GOTO:
  GF_TRACE (this, "AFR_OPEN_8_GOTO");
  local->shclose = 0; /* so that we dont try to close shfd if unlock fails */
  if (local->error) {
    GF_ERROR (this, "error during close of selfheal fds");
    goto AFR_OPEN_ERROR;
  }

  local->label = AFR_OPEN_9;
  /* so that we dont try to unlock in AFR_OPEN_ERROR */
  local->lock = 0; 
  local->offset = 0;
  local->length = LLONG_MAX;
  if (local->uid)
    frame->root->uid = local->uid;
  if (local->gid)
    frame->root->gid = local->gid;
  for (i = 0; i < child_count; i++) {
    if (local->locked[i] == 0 && afrfdp->fdstate[i])
      break;
  }
  if (i < child_count) {
    afr_unlock_servers (frame);
    return 0;
  }

 AFR_OPEN_9_GOTO:
  GF_TRACE (this, "AFR_OPEN_9_GOTO");
  if (local->error) {
    GF_ERROR (this, "there was an error on unlocking");
    goto AFR_OPEN_ERROR;
  }

  FREE (local->writecnt);
  FREE (local->loc->path);
  FREE (local->loc);
  if (local->shfd) {
    FREE (shafrfdp->path);
    FREE (shafrfdp->fdsuccess);
    FREE (shafrfdp->fdstate);
    FREE (shafrfdp);
    dict_destroy (local->shfd->ctx);
    FREE (local->shfd);
  }
  if (local->locked)
    FREE (local->locked);
  STACK_UNWIND (frame, 0, 0, local->fd);
  return 0;

 AFR_OPEN_ERROR:
  GF_ERROR (this, "executing AFR_OPEN_ERROR");
  if (local->lock == 0) {
    goto AFR_OPEN_10_GOTO;
  }
  local->lock = 0;
  GF_TRACE (this, "unlocking..");
  local->label = AFR_OPEN_10;
  local->offset = 0;
  local->length = LLONG_MAX;
  if (local->uid)
    frame->root->uid = local->uid;
  if (local->gid)
    frame->root->gid = local->gid;
  afr_unlock_servers (frame);
  return 0;

 AFR_OPEN_10_GOTO:
  GF_TRACE (this, "AFR_OPEN_10_GOTO");
  GF_TRACE (this, "closing the opened fds");
  cnt = 0;
  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdsuccess[i])
      cnt++;
  }
  if (local->shclose) {
    for (i = 0; i < child_count; i++) {
      if (shafrfdp->fdsuccess[i])
	cnt++;
    }
  }
  local->call_count = cnt;
  local->label = AFR_OPEN_11;
  if (local->shclose) {
    for (i = 0; i < child_count; i++) {
      if (shafrfdp->fdsuccess[i]) {
	STACK_WIND (frame,
		    afr_open_close_cbk,
		    children[i],
		    children[i]->fops->close,
		    local->shfd);
	if (--cnt == 0)
	  break;
      }
    }
  }
  for (i = 0; i < child_count; i++) {
    if (afrfdp->fdsuccess[i]) {
      STACK_WIND (frame,
		  afr_open_close_cbk,
		  children[i],
		  children[i]->fops->close,
		  local->fd);
      if (--cnt == 0)
	break;
    }
  }
  return 0;

 AFR_OPEN_11_GOTO:
  GF_TRACE (this, "AFR_OPEN_11_GOTO");
  local->op_ret = -1;
  local->op_errno = EIO;
  FREE (afrfdp->path);
  FREE (afrfdp->fdsuccess);
  FREE (afrfdp->fdstate);
  FREE (afrfdp);

  FREE (local->writecnt);
  FREE (local->loc->path);
  FREE (local->loc);
  if (local->shfd) {
    FREE (shafrfdp->path);
    FREE (shafrfdp->fdsuccess);
    FREE (shafrfdp->fdstate);
    FREE (shafrfdp);
    dict_destroy (local->shfd->ctx);
    FREE (local->shfd);
  }
  if (local->locked)
    FREE (local->locked);
  STACK_UNWIND (frame, -1, EIO, local->fd);
  return 0;
}
