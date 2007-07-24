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
 * 3) in cbks the thing about ENOENT and ENOTCONN
 * 4) mem optimization
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

#define AFR_DEBUG_FMT(xl, format, args...) if(((afr_private_t*)(xl)->private)->debug) gf_log ((xl)->name, GF_LOG_DEBUG, "AFRDEBUG:" format, ##args);
#define AFR_DEBUG(xl) if(((afr_private_t*)xl->private)->debug) gf_log (xl->name, GF_LOG_DEBUG, "AFRDEBUG:");

static int32_t
afr_get_num_copies (const char *path, xlator_t *this)
{
  pattern_info_t *tmp = ((afr_private_t *)this->private)->pattern_info_list;
  int32_t pil_num = ((afr_private_t *)this->private)->pil_num;
  int32_t count = 0;

  for (count = 0; count < pil_num; count++) {
    if (fnmatch (tmp->pattern, path, 0) == 0) {
      return tmp->copies;
    }
    tmp++;
  }
  GF_WARNING (this, "pattern for %s did not match with any options, defaulting to 1", path);
  return 1;
}

static struct list_head *
afr_inode_to_giclist (xlator_t *this, inode_t *inode)
{
  afr_inode_private_t *aip;
  GF_BUG_ON (!inode);
  data_t *aip_data = dict_get (inode->ctx, this->name);
  if (aip_data) {
    aip = data_to_ptr (aip_data);
    return aip->giclist;
  }
  GF_DEBUG (this, "returning NULL");
  return NULL;
}

static loc_t*
afr_loc_dup(loc_t *loc)
{
  loc_t *loctmp;
  GF_BUG_ON (!loc);
  loctmp = calloc(1, sizeof(loc_t));
  loctmp->inode = loc->inode;
  loctmp->path = strdup (loc->path);
  return loctmp;
}

static void
afr_loc_free(loc_t *loc)
{
  GF_BUG_ON (!loc);
  freee (loc->path);
  freee(loc);
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
  gf_inode_child_t *gic;
  struct list_head *list = afr_inode_to_giclist(this, local->loc->inode);
  call_frame_t *prev_frame = cookie;
  AFR_DEBUG_FMT (this, "op_ret = %d op_errno = %d from client %s", op_ret, op_errno, prev_frame->this->name);
  if (op_ret == 0) {
    GF_BUG_ON (!inode);
    GF_BUG_ON (!buf);
    GF_BUG_ON (local->loc->inode != inode);
    list_for_each_entry (gic, list, clist) {
      if (prev_frame->this == gic->xl)
	break;
    }
    gic->inode = inode;
    gic->stat = *buf;
    gic->op_errno = 0;
  } else {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
  }
  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
    frame->root->uid = local->uid;
    frame->root->gid = local->gid;
    afr_loc_free(local->loc);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  local->loc->inode,
		  &local->stbuf);
  }
  return 0;
}

static int32_t
afr_sync_ownership_permission_cbk(call_frame_t *frame,
				  void *cookie,
				  xlator_t *this,
				  int32_t op_ret,
				  int32_t op_errno,
				  struct stat *stbuf)
{
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  gf_inode_child_t *gic;
  struct list_head *list = afr_inode_to_giclist (this, local->loc->inode);
  int32_t  callcnt;
  AFR_DEBUG (this);

  list_for_each_entry (gic, list, clist)
    if (prev_frame->this == gic->xl)
      break;
  if (op_ret == 0) {
    GF_BUG_ON (!stbuf);
    gic->stat = *stbuf;
  } else {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
  }
  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (callcnt == 0){
    frame->root->uid = local->uid;
    frame->root->gid = local->gid;
    /* mkdir missing entries in case of dirs */
    if (S_ISDIR (local->stbuf.st_mode)) {
      list_for_each_entry (gic, list, clist) {
	if (gic->op_errno == ENOENT)
	  local->call_count++;
      }
      if (local->call_count) {
	list_for_each_entry (gic, list, clist) {
	  if(gic->op_errno == ENOENT) {
	    GF_DEBUG (this, "calling mkdir(%s) on child %s", local->loc->path, gic->xl->name);
	    local->uid = frame->root->uid;
	    local->gid = frame->root->gid;
	    frame->root->uid = local->stbuf.st_uid;
	    frame->root->gid = local->stbuf.st_gid;
	    STACK_WIND (frame,
			afr_lookup_mkdir_cbk,
			gic->xl,
			gic->xl->fops->mkdir,
			local->loc,
			local->stbuf.st_mode);
	  }
	}
	return 0;
      }
    }
    afr_loc_free(local->loc);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  local->loc->inode,
		  &local->stbuf);
  }
  return 0;
}

static int32_t
afr_sync_ownership_permission (call_frame_t *frame)
{
  afr_local_t *local = frame->local;
  struct list_head *list;
  gf_inode_child_t *gic, *latestgic = NULL;
  inode_t *inode = local->loc->inode;
  list = afr_inode_to_giclist (frame->this, inode);
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      if (latestgic == NULL)
	latestgic = gic;
      if (gic->stat.st_ctime > latestgic->stat.st_ctime)
	latestgic = gic;
    }
  }
  local->stbuf.st_uid = latestgic->stat.st_uid;
  local->stbuf.st_gid = latestgic->stat.st_gid;
  local->stbuf.st_mode = latestgic->stat.st_mode;
  AFR_DEBUG_FMT (frame->this, "latest %s uid %u gid %u %d", latestgic->xl->name, latestgic->stat.st_uid, latestgic->stat.st_gid, latestgic->stat.st_mode);
  /* latestgic will not be NULL as local->op_ret is 0 */
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      if (gic == latestgic)
	continue;
      if ((latestgic->stat.st_uid != gic->stat.st_uid) || (latestgic->stat.st_gid != gic->stat.st_gid))
	local->call_count++;
      if (latestgic->stat.st_mode != gic->stat.st_mode)
	local->call_count++;
    }
  }
  AFR_DEBUG_FMT (frame->this, "local->call_count %d", local->call_count);
  /* FIXME see if there is a race condition here i.e before completing
   * the foll loop, if any of the cbks update the gic->stat and if there
   * can be more STACK_WINDS than local->call_count
   */
  if (local->call_count) {
    local->uid = frame->root->uid;
    local->gid = frame->root->gid;
    frame->root->uid = 0;
    frame->root->gid = 0;

    list_for_each_entry (gic, list, clist) {
      if (gic->inode) {
	if (gic == latestgic)
	  continue;
	if ((latestgic->stat.st_uid != gic->stat.st_uid) || (latestgic->stat.st_gid != gic->stat.st_gid)) {
	  GF_DEBUG (frame->this, "uid/gid mismatch, latest on %s, calling chown(%s, %u, %u) on %s", latestgic->xl->name, local->loc->path, latestgic->stat.st_uid, latestgic->stat.st_gid, gic->xl->name);
	  STACK_WIND (frame,
		      afr_sync_ownership_permission_cbk,
		      gic->xl,
		      gic->xl->fops->chown,
		      local->loc,
		      latestgic->stat.st_uid,
		      latestgic->stat.st_gid);
	}
	if (latestgic->stat.st_mode != gic->stat.st_mode) {
	  GF_DEBUG (frame->this, "mode mismatch, latest on %s, calling chmod(%s, 0%o) on %s", latestgic->xl->name, local->loc->path, latestgic->stat.st_mode, gic->xl->name);
	  STACK_WIND (frame,
		      afr_sync_ownership_permission_cbk,
		      gic->xl,
		      gic->xl->fops->chmod,
		      local->loc,
		      latestgic->stat.st_mode);
	}
      }
    }
  } else {
    /* mkdir missing entries in case of dirs */
    if (S_ISDIR (local->stbuf.st_mode)) {
      list_for_each_entry (gic, list, clist) {
	if (gic->op_errno == ENOENT)
	  local->call_count++;
      }
      if (local->call_count) {
	list_for_each_entry (gic, list, clist) {
	  if(gic->op_errno == ENOENT) {
	    AFR_DEBUG_FMT (frame->this, "calling mkdir(%s) on %s", local->loc->path, gic->xl->name);
	    local->uid = frame->root->uid;
	    local->gid = frame->root->gid;
	    frame->root->uid = local->stbuf.st_uid;
	    frame->root->gid = local->stbuf.st_gid;
	    STACK_WIND (frame,
			afr_lookup_mkdir_cbk,
			gic->xl,
			gic->xl->fops->mkdir,
			local->loc,
			local->stbuf.st_mode);
	  }
	}
	return 0;
      }
    }
    afr_loc_free(local->loc);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inode,
		  &local->stbuf);
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
    GF_BUG_ON (!inode);
    GF_BUG_ON (!buf);

    gic->inode = inode;

    gic->stat = *buf;
    gic->op_errno = 0;
  } else {
    gic->op_errno = op_errno;
    gic->inode = NULL;
  }

  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (callcnt == 0){
    if (local->op_ret == 0) {
      /* FIXME preserve the inode number even if the first child goes down */
      list_for_each_entry (gic, list, clist)
	if (gic->inode)
	  break;
      local->stbuf = gic->stat;
      if (afr_inode_to_giclist(this, local->loc->inode) == NULL) {
	/* first time lookup and success */
	afr_inode_private_t *aip = calloc (1, sizeof (*aip));
	aip->giclist = list;
	dict_set (inode->ctx, this->name, data_from_static_ptr(aip));
      }
      if (((afr_private_t *)this->private)->self_heal) {
	afr_sync_ownership_permission (frame);
	return 0;
      }
    } else if (local->inode == NULL) {
      /* all lookup_cbks failed and its not a revalidate.
       * we should not do this if its a revalidate because it will
       * segfault during next revalidate
       */
      list_for_each_entry_safe (gic, gictemp, list, clist) {
	list_del (&gic->clist);
	freee (gic);
      }
      freee (list);
    }
    afr_loc_free(local->loc);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inode,
		  &local->stbuf);
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
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->loc = afr_loc_dup (loc);
  list = afr_inode_to_giclist (this, loc->inode);
  if(list == NULL) {
    /* its the first time lookup, not revalidation */
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
		  loc);
      trav = trav->next;
    }
  } else {
    /* revalidation */
    int32_t cnt;
    local->inode = loc->inode; /* FIXME using this to delete list when first time lookup fails in all cbks */
    local->list = list;
    list_for_each_entry (gic, list, clist) {
      ++local->call_count;
    }
    cnt = local->call_count;
    list_for_each_entry (gic, list, clist) {
      STACK_WIND (frame,
		  afr_lookup_cbk,
		  gic->xl,
		  gic->xl->fops->lookup,
		  loc);
      /* we use cnt to break because we had a situation when io-threads was loaded
       * during the last iteration, it would STACK_UNWIND, later dbench would unlink file,
       * and then forget (on other threads) which will delete inode->private.
       * and when this thread resumes for next iteration where it would have break'ed,
       * it will segfault as list will not be valid
       */
      if (--cnt == 0)
	break;
    }
  }
  return 0;
}


static int32_t
afr_forget (call_frame_t *frame,
	    xlator_t *this,
	    inode_t *inode)
{
  AFR_DEBUG_FMT(this, "inode = %u", inode->ino);
  gf_inode_child_t *gic, *gictemp;
  afr_inode_private_t *aip = NULL;
  data_t *aip_data;
  struct list_head *list;
  aip_data = dict_get (inode->ctx, this->name);
  GF_BUG_ON (!aip_data);
  aip = data_to_ptr (aip_data);
  dict_del (inode->ctx, this->name);
  list = aip->giclist;

  list_for_each_entry_safe (gic, gictemp, list, clist) {
    list_del (& gic->clist);
    freee (gic);
  }
  freee (list);
  freee (aip);
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
  call_frame_t *prev_frame = cookie;
  int32_t callcnt;

  if (op_ret != 0  && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  if (op_ret == 0) {
    local->op_ret = op_ret;
  } else {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
  }
  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    afr_loc_free (local->loc);
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
  struct list_head *list = afr_inode_to_giclist (this, loc->inode);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->loc = afr_loc_dup (loc);
  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      STACK_WIND(frame,
		 afr_setxattr_cbk,
		 gic->xl,
		 gic->xl->fops->setxattr,
		 loc,
		 dict,
		 flags);
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
  call_frame_t *prev_frame = cookie;
  if (op_ret == 0) {
    GF_BUG_ON (!dict);
  } else {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", frame->local, prev_frame->this->name, op_ret, op_errno);
  }
  frame->local = NULL; /* so that STACK_DESTROY does not free that memory */
  STACK_UNWIND (frame, op_ret, op_errno, dict);
  return 0;
}

static int32_t
afr_getxattr (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  AFR_DEBUG_FMT (this, "loc->path = %s", loc->path);
  struct list_head *list = afr_inode_to_giclist (this, loc->inode);
  gf_inode_child_t *gic;

  frame->local = (void*)loc->path;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      break;
  }
  STACK_WIND (frame,
	      afr_getxattr_cbk,
	      gic->xl,
	      gic->xl->fops->getxattr,
	      loc);
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
  call_frame_t *prev_frame = cookie;
  int32_t callcnt;

  if (op_ret != 0 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  if (op_ret == 0) {
    local->op_ret = op_ret;
  } else {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
  }
  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    afr_loc_free (local->loc);
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
  gf_inode_child_t *gic;
  struct list_head *list = afr_inode_to_giclist (this, loc->inode);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->loc = afr_loc_dup (loc);
  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      STACK_WIND (frame,
		  afr_removexattr_cbk,
		  gic->xl,
		  gic->xl->fops->removexattr,
		  loc,
		  name);
  }
  return 0;
}

static int32_t
afr_open_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      fd_t *fd)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  call_frame_t *prev_frame = cookie;

  if (op_ret == -1 && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  if (op_ret >= 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
  }
  if (op_ret == -1) {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->path, prev_frame->this->name, op_ret, op_errno);
  }
  LOCK (&frame->lock);
  if (op_ret >= 0) {
    GF_BUG_ON (!fd);
    dict_t *afrctx;
    data_t *afrctx_data;
    afrctx_data = dict_get (fd->ctx, this->name);
    if (afrctx_data == NULL) {
      afrctx = get_new_dict ();
      dict_set (fd->ctx, this->name, data_from_static_ptr (afrctx));
      /* we use the path here just for debugging */
      dict_set (afrctx, "path", data_from_ptr (strdup(local->path))); /* strduped mem will be freed on dict_destroy */
      if (local->flags & O_TRUNC)
	dict_set (afrctx, "afr-write", data_from_uint32(1)); /* so that we increment version count */
    } else 
      afrctx = data_to_ptr (afrctx_data);

    dict_set (afrctx, prev_frame->this->name, data_from_uint32(1)); /* indicates open is success in that child */
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    freee (local->path);
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
  call_frame_t *prev_frame = cookie;
  struct list_head *list = local->list;
  if (op_ret == -1) {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
    call_frame_t *open_frame = local->orig_frame;
    afr_local_t *open_local = open_frame->local;
    open_local->sh_return_error = 1;
  }
  AFR_DEBUG_FMT (this, "call_resume()");
  call_resume (local->stub);
  freee (local->loc->path);
  freee (local->loc);
  if (local->fd) {
    dict_t *afrctx = data_to_ptr (dict_get(local->fd->ctx, this->name));
    dict_destroy(afrctx);
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

static int32_t
afr_selfheal_nosync_close_cbk (call_frame_t *frame,
			       void *cookie,
			       xlator_t *this,
			       int32_t op_ret,
			       int32_t op_errno);

static int32_t
afr_error_during_sync (call_frame_t *frame)
{
  afr_local_t *local = frame->local;
  struct list_head *list = local->list;
  afr_selfheal_t *ash;
  int32_t cnt;
  dict_t *afrctx = data_to_ptr(dict_get(local->fd->ctx, frame->this->name));
  GF_ERROR (frame->this, "error during self-heal");
  call_frame_t *open_frame = local->orig_frame;
  afr_local_t *open_local = open_frame->local;
  open_local->sh_return_error = 1;

  local->call_count = 0;
  list_for_each_entry (ash, list, clist) {
    if(dict_get (afrctx, ash->xl->name))
      local->call_count++;
  }
  /* local->call_count cant be 0 because we'll have atleast source's fd in dict */
  GF_BUG_ON (!local->call_count);
  cnt = local->call_count;
  list_for_each_entry (ash, list, clist) {
    if(dict_get (afrctx, ash->xl->name))
      STACK_WIND (frame,
		  afr_selfheal_nosync_close_cbk,
		  ash->xl,
		  ash->xl->fops->close,
		  local->fd);
    /* in case this is the last WIND, list will be free'd before next iteration
     * causing segfault. so we use a cnt counter
     */
    if (--cnt == 0)
      break;
  }
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
  if (op_ret == -1) {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
    /* since we would have already called close, we wont use afr_error_during_sync */
    call_frame_t *open_frame = local->orig_frame;
    afr_local_t *open_local = open_frame->local;
    open_local->sh_return_error = 1;
  }

  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
    AFR_DEBUG_FMT (this, "calling unlock on local->loc->path %s", local->loc->path);
    STACK_WIND (frame,
		afr_selfheal_unlock_cbk,
		local->lock_node,
		local->lock_node->mops->unlock,
		local->loc->path);
  }
  return 0;
}

static int32_t
afr_selfheal_utimens_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  struct stat *stat)
{
  afr_local_t *local = frame->local;
  int32_t callcnt;
  call_frame_t *prev_frame = cookie;
  if (op_ret == -1) {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
    /* since we would have already called close, we wont use afr_error_during_sync */
    call_frame_t *open_frame = local->orig_frame;
    afr_local_t *open_local = open_frame->local;
    open_local->sh_return_error = 1;
  }

  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
    AFR_DEBUG_FMT (this, "calling unlock on local->loc->path %s", local->loc->path);
    STACK_WIND (frame,
		afr_selfheal_unlock_cbk,
		local->lock_node,
		local->lock_node->mops->unlock,
		local->loc->path);
  }
  return 0;
}

/* FIXME handle the situation when one of the close fails */

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
  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
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
    int cnt = local->call_count;

    list_for_each_entry (ash, list, clist) {
      struct timespec ts[2];
      ts[0].tv_sec = local->source->stat.st_atime;
      ts[1].tv_sec = local->source->stat.st_mtime;
      if (ash->inode && (ash->repair || ash->version == 1)) {
	AFR_DEBUG_FMT (this, "setxattr() on %s version %u ctime %u", ash->xl->name, local->source->version, local->source->ctime);
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
  call_frame_t *prev_frame = cookie;

  if (op_ret >= 0)
    local->op_ret = op_ret;
  if (op_ret == -1) {
    /* even if one write fails, we will return open with error, need to see if
     * we can improve on this behaviour.
     * We should wait for all cbks before calling afr_error_during_sync()
     */
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
    local->sh_return_error = 1;
  }
  LOCK(&frame->lock);
  callcnt = --local->call_count;
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
  call_frame_t *prev_frame = cookie;
  afr_selfheal_t *ash;
  dict_t *afrctx = data_to_ptr (dict_get (local->fd->ctx, this->name));
  int32_t cnt;
  list_for_each_entry (ash, list, clist) {
    if (dict_get(afrctx, ash->xl->name))
      local->call_count++;
  }

  if (op_ret == 0) {
    AFR_DEBUG_FMT (this, "EOF reached");
    cnt = local->call_count;
    list_for_each_entry (ash, list, clist) {
      if (dict_get(afrctx, ash->xl->name)) {
	STACK_WIND (frame,
		    afr_selfheal_close_cbk,
		    ash->xl,
		    ash->xl->fops->close,
		    local->fd);
	if (--cnt == 0)
	  break;
      }
    }
  } else if (op_ret > 0) {
    local->call_count--; /* we dont write on source */
    local->op_ret = -1;
    local->op_errno = ENOENT;
    cnt = local->call_count;
    list_for_each_entry (ash, list, clist) {
      if (ash == local->source)
	continue;
      if (dict_get(afrctx, ash->xl->name)) {
	AFR_DEBUG_FMT (this, "write call on %s", ash->xl->name);
	STACK_WIND (frame,
		    afr_selfheal_sync_file_writev_cbk,
		    ash->xl,
		    ash->xl->fops->writev,
		    local->fd,
		    vector,
		    count,
		    local->offset);
	if (--cnt == 0)
	  break;
      }
    }
  } else {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
    afr_error_during_sync(frame);
  }
  return 0;
}

static int32_t
afr_selfheal_sync_file (call_frame_t *frame,
			xlator_t *this)
{
  AFR_DEBUG (this);
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

static int32_t
afr_selfheal_nosync_close_cbk (call_frame_t *frame,
			       void *cookie,
			       xlator_t *this,
			       int32_t op_ret,
			       int32_t op_errno)
{
  AFR_DEBUG(this);
  afr_local_t *local = frame->local;
  int32_t callcnt;

  LOCK (&frame->lock);
  callcnt = --local->call_count;
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

static int32_t
afr_selfheal_create_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 fd_t *fd,
			 inode_t *inode,
			 struct stat *stat)
{
  AFR_DEBUG (this);
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  struct list_head *list = afr_inode_to_giclist (this, local->loc->inode);
  gf_inode_child_t *gic;
  afr_selfheal_t *ash;
  int32_t callcnt;
  dict_t *afrctx = data_to_ptr (dict_get (fd->ctx, this->name));
  AFR_DEBUG_FMT (this, "op_ret = %d from %s", op_ret, prev_frame->this->name);
  frame->root->uid = local->uid;
  frame->root->gid = local->gid;
  if (op_ret >= 0) {
    GF_BUG_ON (!fd);
    GF_BUG_ON (!inode);
    GF_BUG_ON (!stat);
    LOCK (&frame->lock);
    dict_set (afrctx, prev_frame->this->name, data_from_uint32 (1));
    UNLOCK (&frame->lock);
    list_for_each_entry (gic, list, clist) {
      if (gic->xl == prev_frame->this)
	break;
    }
    gic->inode = inode;
    gic->stat = *stat;
    list = local->list;
    list_for_each_entry (ash, list, clist) {
      if (ash->xl == prev_frame->this)
	break;
    }
    ash->inode = inode;
  } else {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
  }
  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
    int32_t src_open = 0, sync_file_cnt = 0;
    afr_selfheal_t *ash;
    struct list_head *list = local->list;
    list_for_each_entry (ash, list, clist) {
      if (dict_get(afrctx, ash->xl->name)) {
	sync_file_cnt++;
	if (ash->xl == local->source->xl)
	  src_open = 1;
      }
    }
    if (src_open  && (sync_file_cnt >= 2)) /* source open success + atleast a file to sync */
      afr_selfheal_sync_file (frame, this);
    else {
      local->call_count = sync_file_cnt;
      if (dict_get(afrctx, ash->xl->name))
	STACK_WIND (frame,
		    afr_selfheal_nosync_close_cbk,
		    ash->xl,
		    ash->xl->fops->close,
		    local->fd);
    }
  }
  return 0;
}

static int32_t
afr_selfheal_open_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       fd_t *fd)
{
  afr_local_t *local = frame->local;
  call_frame_t *prev_frame = cookie;
  int32_t callcnt;
  dict_t *afrctx = data_to_ptr (dict_get (fd->ctx, this->name));
  AFR_DEBUG_FMT (this, "op_ret = %d from %s", op_ret, prev_frame->this->name);
  if (op_ret >= 0) {
    GF_BUG_ON (!fd);
    LOCK (&frame->lock);
    dict_set (afrctx, prev_frame->this->name, data_from_uint32 (1));
    UNLOCK (&frame->lock);
  } else {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d ", local->loc->path, prev_frame->this->name, op_ret, op_errno);
  }
  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
    afr_selfheal_t *ash;
    int32_t src_open = 0, sync_file_cnt = 0;
    struct list_head *list = local->list;
    list_for_each_entry (ash, list, clist) {
      if (dict_get(afrctx, ash->xl->name)) {
	sync_file_cnt++;
	if (ash->xl == local->source->xl)
	  src_open = 1;
      }
    }
    if (src_open  && (sync_file_cnt >= 2)) /* source open success + atleast a file to sync */
      afr_selfheal_sync_file (frame, this);
    else {
      local->call_count = sync_file_cnt;
      list_for_each_entry (ash, list, clist) {
      if (dict_get(afrctx, ash->xl->name))
	STACK_WIND (frame,
		    afr_selfheal_nosync_close_cbk,
		    ash->xl,
		    ash->xl->fops->close,
		    local->fd);
      }
    }
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
  dict_t *afrctx;

  list_for_each_entry (ash, list, clist) {
    if (prev_frame->this == ash->xl)
      break;
  }
  if (op_ret >= 0) {
    if (dict){
      ash->dict = dict_ref (dict);
      data_t *version_data = dict_get (dict, "trusted.afr.version");
      if (version_data) 
	ash->version = data_to_uint32 (version_data); /* version_data->data is NULL terminated bin data*/
      else {
	AFR_DEBUG_FMT (this, "version attribute was not found on %s, defaulting to 1", prev_frame->this->name)
	ash->version = 1;
	dict_set(ash->dict, "trusted.afr.version", bin_to_data("1", 1));
      }
      data_t *ctime_data = dict_get (dict, "trusted.afr.createtime");
      if (ctime_data)
	ash->ctime = data_to_uint32 (ctime_data);     /* ctime_data->data is NULL terminated bin data */
      else {
	ash->ctime = 0;
	dict_set (ash->dict, "trusted.afr.createtime", bin_to_data("0", 1));
      }
      AFR_DEBUG_FMT (this, "op_ret = %d version = %u ctime = %u from %s", op_ret, ash->version, ash->ctime, prev_frame->this->name);
      ash->op_errno = 0;
    }
  } else {
    AFR_DEBUG_FMT (this, "op_ret = %d from %s", op_ret, prev_frame->this->name);
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
    ash->op_errno = op_errno;
    if (op_errno == ENODATA) {
      ash->dict = dict_ref (dict);
      ash->version = 1;
      dict_set(ash->dict, "trusted.afr.version", bin_to_data("1", 1));
      ash->ctime = 0;
      dict_set (ash->dict, "trusted.afr.createtime", bin_to_data("0", 1));
    }
  }

  LOCK(&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
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
	  AFR_DEBUG_FMT (this, "%s version %d outdated, latest=%d, %d", ash->xl->name, ash->version, latest, local->call_count);
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
    GF_DEBUG (this, "self-heal needed (path=%s source=%s)", source->xl->name, local->loc->path);
    local->source = source;
    local->fd = calloc (1, sizeof(fd_t));
    local->fd->ctx = get_new_dict();
    afrctx = get_new_dict();
    dict_set (local->fd->ctx, this->name, data_from_static_ptr (afrctx));
    local->fd->inode = local->loc->inode;
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
      if (ash->repair == 0) { /* case where op_errno might be ENOTCONN */
	AFR_DEBUG_FMT (this, "repair not needed on %s", ash->xl->name);
	continue;             /* also when we are not supposed to replicate here */
      }

      if (ash->op_errno == ENOENT) {
	AFR_DEBUG_FMT (this, "create() on %s", ash->xl->name);

	local->uid = frame->root->uid;
	local->gid = frame->root->gid;
	frame->root->uid = source->stat.st_uid;
	frame->root->gid = source->stat.st_gid;

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
  afr_local_t *local = frame->local;
  afr_selfheal_t *ash, *ashtemp;
  struct list_head *list = local->list;
  call_frame_t *prev_frame = cookie;
  if (op_ret == -1) {
    AFR_DEBUG_FMT (this, "locking failed!");
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
    call_frame_t *open_frame = local->orig_frame;
    afr_local_t *open_local = open_frame->local;
    open_local->sh_return_error = 1;
    call_resume(local->stub);
    freee (local->loc->path);
    freee (local->loc);
    if (local->fd) {
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

  int32_t totcnt = local->call_count;
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
  shlocal->loc->inode = loc->inode;
  shlocal->orig_frame = frame;
  shlocal->stub = stub;
  ((afr_local_t*)frame->local)->shcalled = 1;
  struct list_head *inodepvt = afr_inode_to_giclist(this, loc->inode);
  list_for_each_entry (gic, inodepvt, clist) {
    ash = calloc (1, sizeof (*ash));
    ash->xl = gic->xl;
    ash->stat = gic->stat;
    if (gic->inode)
      ash->inode = gic->inode;
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
	  int32_t flags,
	  fd_t *fd)
{
  AFR_DEBUG_FMT (this, "loc->path = %s loc->inode = %p", loc->path, loc->inode);
  afr_local_t *local; 
  gf_inode_child_t *gic;
  struct list_head *list = afr_inode_to_giclist (this, loc->inode);


  if (frame->local == NULL) {
    frame->local = (void *) calloc (1, sizeof (afr_local_t));
  }
  local = frame->local;

  if (((afr_private_t *) this->private)->self_heal) {
    AFR_DEBUG_FMT (this, "self heal enabled");
    if (local->sh_return_error) {
      AFR_DEBUG_FMT (this, "self heal failed, open will return EIO");
      GF_ERROR (this, "self heal failed, returning EIO");
      STACK_UNWIND (frame, -1, EIO, fd);
      return 0;
    }
    if (local->shcalled == 0) {
      AFR_DEBUG_FMT (this, "self heal checking...");
      call_stub_t *stub = fop_open_stub (frame, afr_open, loc, flags, fd);
      afr_selfheal (frame, this, stub, loc);
      return 0;
    }
    AFR_DEBUG_FMT (this, "self heal already called");
  } else {
    AFR_DEBUG_FMT (this, "self heal disabled");
  }


  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->path = strdup(loc->path);
  local->flags = flags;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if(gic->inode) {
      STACK_WIND (frame,
		  afr_open_cbk,
		  gic->xl,
		  gic->xl->fops->open,
		  loc,
		  flags,
		  fd);
    }
  }
  return 0;
}

/* FIXME if one read fails, we need to fail over */

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
  if (op_ret == -1) {
    char *path;
    call_frame_t *prev_frame = cookie;
    path = data_to_str(dict_get(frame->local, "path"));
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", path, prev_frame->this->name, op_ret, op_errno);
  }
  frame->local = NULL; /* so that STACK_DESTROY does not free it */
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
  struct list_head *list= afr_inode_to_giclist (this, fd->inode);
  dict_t *ctx;
  ctx = data_to_ptr (dict_get (fd->ctx, this->name));
  frame->local = ctx;
  list_for_each_entry (gic, list, clist) {
    if (dict_get (ctx, gic->xl->name))
      break;
  }

  STACK_WIND (frame,
	      afr_readv_cbk,
	      gic->xl,
	      gic->xl->fops->readv,
	      fd,
	      size,
	      offset);
  return 0;
}

/* FIXME if one write fails, we should not increment version on it */

static int32_t
afr_writev_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct stat *stat)
{
  AFR_DEBUG_FMT(this, "op_ret %d op_errno %d", op_ret, op_errno);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  call_frame_t *prev_frame = cookie;

  if (op_ret == -1 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }

  if (op_ret == -1) {
    dict_t *afrctx = data_to_ptr (dict_get(local->fd->ctx, this->name));
    char *path = data_to_str (dict_get (afrctx, "path"));
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", path, prev_frame->this->name, op_ret, op_errno);
  }
  LOCK (&frame->lock);
  if (op_ret != -1 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
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
  struct list_head *list = afr_inode_to_giclist (this, fd->inode);
  dict_t *afrctx = data_to_ptr (dict_get(fd->ctx, this->name));

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->fd = fd;
  dict_set (afrctx, "afr-write", data_from_uint32(1));
  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name))    
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name)) {
      STACK_WIND (frame,
		  afr_writev_cbk,
		  gic->xl,
		  gic->xl->fops->writev,
		  fd,
		  vector,
		  count,
		  offset);
    }
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
  call_frame_t *prev_frame = cookie;
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  if (op_ret == -1) {
    dict_t *afrctx = data_to_ptr (dict_get(local->fd->ctx, this->name));
    char *path = data_to_str (dict_get (afrctx, "path"));
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", path, prev_frame->this->name, op_ret, op_errno);
  }

  LOCK (&frame->lock);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    local->stbuf = *stbuf;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
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
  struct list_head *list = afr_inode_to_giclist (this, fd->inode);
  dict_t *afrctx = data_to_ptr (dict_get(fd->ctx, this->name));

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->fd = fd;
  dict_set (afrctx, "afr-write", data_from_uint32(1));
  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name))    
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name)) {
      STACK_WIND(frame,
		 afr_ftruncate_cbk,
		 gic->xl,
		 gic->xl->fops->ftruncate,
		 fd,
		 offset);
    }
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
  if (op_ret == -1) {
    char *path;
    call_frame_t *prev_frame = cookie;
    path = data_to_str(dict_get(frame->local, "path"));
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", path, prev_frame->this->name, op_ret, op_errno);
  }
  frame->local = NULL; /* so that STACK_UNWIND does not try to free */
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
  struct list_head *list= afr_inode_to_giclist (this, fd->inode);
  dict_t *ctx;
  ctx = data_to_ptr (dict_get (fd->ctx, this->name));
  frame->local = ctx;
  list_for_each_entry (gic, list, clist) {
    if (dict_get (ctx, gic->xl->name))
      break;
  }
  STACK_WIND (frame,
	      afr_fstat_cbk,
	      gic->xl,
	      gic->xl->fops->fstat,
	      fd);
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
  call_frame_t *prev_frame = cookie;
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  if (op_ret == -1) {
    dict_t *afrctx = data_to_ptr (dict_get(local->fd->ctx, this->name));
    char *path = data_to_str (dict_get (afrctx, "path"));
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", path, prev_frame->this->name, op_ret, op_errno);
  }

  LOCK (&frame->lock);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
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
  struct list_head *list = afr_inode_to_giclist (this, fd->inode);
  dict_t *afrctx = data_to_ptr (dict_get(fd->ctx, this->name));

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->fd = fd;
  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name))    
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name)) {
      STACK_WIND(frame,
		 afr_flush_cbk,
		 gic->xl,
		 gic->xl->fops->flush,
		 fd);
    }
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
  if (op_ret == -1) {
    call_frame_t *prev_frame = cookie;
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
  }
  LOCK (&frame->lock);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
    afr_loc_free (local->loc);
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
  fd_t *fd;
  dict_t *afrctx = data_to_ptr (dict_get(local->fd->ctx, this->name));
  if (op_ret == -1) {
    call_frame_t *prev_frame = cookie;
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
  }
  fd = local->fd;
  list_for_each_entry (ash, list, clist) {
    if (dict_get(afrctx, ash->xl->name))
      local->call_count++;
  }
  list_for_each_entry (ash, list, clist) {
    if (dict_get(afrctx, ash->xl->name)) {
      STACK_WIND (frame,
		  afr_close_cbk,
		  ash->xl,
		  ash->xl->fops->close,
		  fd);
    }
  }

  list_for_each_entry_safe (ash, ashtemp, list, clist) {
    list_del (&ash->clist);
    freee (ash);
  }
  free(list);
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
  if (op_ret == -1) {
    call_frame_t *prev_frame = cookie;
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
  }

  LOCK (&frame->lock);
  callcnt = --local->call_count;
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
  if (op_ret>=0 && dict) {
    data_t *version_data = dict_get (dict, "trusted.afr.version");
    if (version_data) {
      ash->version = data_to_uint32 (version_data);
      AFR_DEBUG_FMT (this, "version %d returned from %s", ash->version, prev_frame->this->name);
    } else {
      AFR_DEBUG_FMT (this, "version attribute missing on %s, putting it to 1", prev_frame->this->name);
      ash->version = 0; /* no version found, we'll increment and put it as 1 */
    }
  } else {
    ash->version = 0;
    AFR_DEBUG_FMT (this, "version attribute missing on %s, putting it to 1", prev_frame->this->name);
  }
  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if(callcnt == 0) {
    dict_t *attr;
    dict_t *afrctx = data_to_ptr (dict_get(local->fd->ctx, this->name));
    attr = get_new_dict();
    list_for_each_entry (ash, list, clist) {
      if (dict_get(afrctx, ash->xl->name))
	local->call_count++;
    }
    int32_t cnt = local->call_count;
    list_for_each_entry (ash, list, clist) {
      if (dict_get(afrctx, ash->xl->name)) {
	char dict_version[100];
	sprintf (dict_version, "%u", ash->version+1);
	dict_set (attr, "trusted.afr.version", bin_to_data(dict_version, strlen(dict_version)));
	STACK_WIND (frame,
		    afr_close_setxattr_cbk,
		    ash->xl,
		    ash->xl->fops->setxattr,
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
  dict_t *afrctx = data_to_ptr (dict_get(fd->ctx, this->name));
  list = afr_inode_to_giclist (this, fd->inode);
  struct list_head *ashlist = calloc(1, sizeof (*ashlist));
  if (op_ret == -1) {
    call_frame_t *prev_frame = cookie;
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
  }
  INIT_LIST_HEAD (ashlist);  
  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name)) {
      local->call_count++;
      ash = calloc (1, sizeof (*ash));
      ash->xl = gic->xl;
      ash->inode = gic->inode;
      list_add_tail (&ash->clist, ashlist);
    }
  }
  local->list = ashlist;
  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name)) {
      STACK_WIND (frame,
		  afr_close_getxattr_cbk,
		  gic->xl,
		  gic->xl->fops->getxattr,
		  local->loc);
    }
  }
  return 0;
}

static int32_t
afr_close (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  struct list_head *list = afr_inode_to_giclist(this, fd->inode);
  gf_inode_child_t *gic;
  afr_local_t *local = calloc (1, sizeof(*local));
  dict_t *afrctx = data_to_ptr (dict_get (fd->ctx, this->name));
  char *path = data_to_ptr (dict_get(afrctx, "path"));
  AFR_DEBUG_FMT (this, "close on %s fd %p", path, fd);
  frame->local = local;
  local->fd = fd;
  local->loc = calloc (1, sizeof (loc_t));
  local->loc->path = strdup(path);
  local->loc->inode = fd->inode;

  if (((afr_private_t*) this->private)->self_heal && dict_get (afrctx, "afr-write")) {
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
    return 0;
  }

  AFR_DEBUG_FMT (this, "self heal disabled or write was not done");
  list_for_each_entry (gic, list, clist) {
    if (dict_get(afrctx, gic->xl->name))
      local->call_count++;
  }
  int cnt = local->call_count;
  list_for_each_entry (gic, list, clist) {
    if (dict_get(afrctx, gic->xl->name)) {
      STACK_WIND (frame,
		  afr_close_cbk,
		  gic->xl,
		  gic->xl->fops->close,
		  fd);
      if (--cnt == 0)
	break;
    }
  }
  dict_destroy (afrctx);
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
  call_frame_t *prev_frame = cookie;
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->lock);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
  }

  if (op_ret == -1) {
    dict_t *afrctx = data_to_ptr (dict_get(local->fd->ctx, this->name));
    char *path = data_to_str (dict_get (afrctx, "path"));
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", path, prev_frame->this->name, op_ret, op_errno);
  }

  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
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
  struct list_head *list = afr_inode_to_giclist (this, fd->inode);
  dict_t *afrctx = data_to_ptr (dict_get(fd->ctx, this->name));

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->fd = fd;
  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name))    
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name)) {
      STACK_WIND(frame,
		 afr_fsync_cbk,
		 gic->xl,
		 gic->xl->fops->fsync,
		 fd,
		 datasync);
    }
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
  call_frame_t *prev_frame = cookie;
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  if (op_ret == -1) {
    dict_t *afrctx = data_to_ptr (dict_get(local->fd->ctx, this->name));
    char *path = data_to_str (dict_get (afrctx, "path"));
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", path, prev_frame->this->name, op_ret, op_errno);
  }

  LOCK (&frame->lock);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    local->lock = *lock;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
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
  struct list_head *list = afr_inode_to_giclist (this, fd->inode);
  dict_t *afrctx = data_to_ptr (dict_get(fd->ctx, this->name));

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->fd = fd;
  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name))    
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name)) {
      STACK_WIND(frame,
		 afr_lk_cbk,
		 gic->xl,
		 gic->xl->fops->lk,
		 fd,
		 cmd,
		 lock);
    }
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
  call_frame_t *prev_frame = cookie;
  struct list_head *list = afr_inode_to_giclist (this, local->loc->inode);
  gf_inode_child_t *gic;
  list_for_each_entry (gic, list, clist) {
    if (gic->xl == prev_frame->this)
      break;
  }
  if (op_ret == 0) {
    gic->stat = *stbuf;
    local->op_ret = 0;
  } else 
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);

  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
    list_for_each_entry (gic, list, clist) {
      if (gic->inode)
	break;
    }
    afr_loc_free (local->loc);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &gic->stat);
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
  frame->local = local;
  local->loc = afr_loc_dup(loc);
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list  = afr_inode_to_giclist(this, loc->inode);
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      local->call_count++;
    }
  }
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      STACK_WIND (frame,
		  afr_stat_cbk,
		  gic->xl,
		  gic->xl->fops->stat,
		  loc);
    }
  }
  return 0;
}

#if 0
/* FIXME: currently because the statfs call is passed to just first child, 
 * we are using default_statfs (). When we decide to get some other method, 
 * uncomment it.
 */
static int32_t
afr_statfs_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		struct statvfs *stbuf)
{
  STACK_UNWIND (frame, op_ret, op_errno, stbuf);
  return 0;
}

static int32_t
afr_statfs (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  STACK_WIND (frame,
	      afr_statfs_cbk,
	      FIRST_CHILD (this),
	      FIRST_CHILD (this)->fops->statfs,
	      loc);
  return 0;
}
#endif

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
  LOCK (&frame->lock);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    local->stbuf = *stbuf;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
  }
  return 0;
}

/* FIXME increase the version count */

static int32_t
afr_truncate (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      off_t offset)
{
  AFR_DEBUG_FMT(this, "loc->path %s", loc->path);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = afr_inode_to_giclist (this, loc->inode);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      STACK_WIND(frame,
		 afr_truncate_cbk,
		 gic->xl,
		 gic->xl->fops->truncate,
		 loc,
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
  LOCK (&frame->lock);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    local->stbuf = *stbuf;
  }
  callcnt = --local->call_count;
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
  AFR_DEBUG_FMT (this, "loc->path %s", loc->path);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = afr_inode_to_giclist(this, loc->inode);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      STACK_WIND(frame,
		 afr_utimens_cbk,
		 gic->xl,
		 gic->xl->fops->utimens,
		 loc,
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
		 fd_t *fd)
{
  AFR_DEBUG_FMT(this, "op_ret = %d fd = %p", op_ret, fd);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  call_frame_t *prev_frame = cookie;
  AFR_DEBUG_FMT (this, "local %p", local);
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->lock);
  if (op_ret >= 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  if (op_ret >= 0) {
    dict_t *afrctx;
    data_t *afrctx_data;
    afrctx_data = dict_get (fd->ctx, this->name);
    if (afrctx_data == NULL) {
      afrctx = get_new_dict ();
      dict_set (fd->ctx, this->name, data_from_static_ptr (afrctx));
      /* we use the path here just for debugging */
      dict_set (afrctx, "path", data_from_ptr (strdup(local->loc->path))); /* strduped mem will be freed on dict_destroy */
    } else 
      afrctx = data_to_ptr (afrctx_data);
    dict_set (afrctx, prev_frame->this->name, data_from_uint32(1)); /* indicates open is success in that child */
  }

  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
    afr_loc_free(local->loc);
    STACK_UNWIND (frame, local->op_ret, local->op_errno, fd);
  }
  return 0;
}

static int32_t
afr_opendir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     fd_t *fd)
{
  AFR_DEBUG_FMT(this, "loc->path = %s inode = %p", loc->path, loc->inode);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = afr_inode_to_giclist(this, loc->inode);

  frame->local = local;
  AFR_DEBUG_FMT (this, "local %p", local);
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->loc = afr_loc_dup(loc);
  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      STACK_WIND (frame,
		  afr_opendir_cbk,
		  gic->xl,
		  gic->xl->fops->opendir,
		  loc,
		  fd);
    }
  }
  return 0;
}

static int32_t
afr_readlink_symlink_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  inode_t *inode,
			  struct stat *stbuf)
{
  AFR_DEBUG (this);
  afr_local_t *local = frame->local;
  int32_t callcnt;
  gf_inode_child_t *gic;
  struct list_head *list;
  call_frame_t *prev_frame = cookie;
  list = afr_inode_to_giclist (this, local->loc->inode);
  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  list_for_each_entry (gic, list, clist) {
    if (gic->xl == prev_frame->this)
      break;
  }
  if (op_ret == 0) {
    gic->inode = inode;
    gic->stat = *stbuf;
  }
  if (callcnt == 0) {
    int len = strlen (local->name);
    char *name = local->name;
    afr_loc_free(local->loc);
    STACK_UNWIND (frame,
		  len,
		  0,
		  name);
    freee (name);
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
  afr_local_t *local = frame->local;
  gf_inode_child_t *gic;
  struct list_head *list = afr_inode_to_giclist (this, local->loc->inode);
  list_for_each_entry (gic, list, clist) {
    if (gic->inode == NULL && gic->op_errno == ENOENT)
      local->call_count++;
  }

  AFR_DEBUG_FMT (this, "op_ret %d buf %s local->call_count %d", op_ret, buf, local->call_count);
  if ( op_ret >= 0 && (((afr_private_t*)this->private)->self_heal) && local->call_count ) {
    /* readlink was successful, self heal enabled, symlink missing in atleast one node */
    local->name = strdup (buf);
    list_for_each_entry (gic, list, clist) {
      if (gic->inode == NULL && gic->op_errno == ENOENT) {
	STACK_WIND (frame,
		    afr_readlink_symlink_cbk,
		    gic->xl,
		    gic->xl->fops->symlink,
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

static int32_t
afr_readlink (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      size_t size)
{
  AFR_DEBUG_FMT(this, "loc->path %s loc->inode %p size %d", loc->path, loc->inode, size);
  struct list_head *list = afr_inode_to_giclist(this, loc->inode);
  gf_inode_child_t *gic;
  afr_local_t *local = calloc (1, sizeof (afr_local_t));
  frame->local = local;
  local->loc = afr_loc_dup(loc);

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      break;
  }
  STACK_WIND (frame,
              afr_readlink_cbk,
              gic->xl,
              gic->xl->fops->readlink,
              loc,
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
  AFR_DEBUG_FMT (this, "op_ret = %d", op_ret);
  int32_t callcnt, tmp_count;
  dir_entry_t *trav, *prev, *tmp, *afr_entry;
  afr_local_t *local = frame->local;

  LOCK (&frame->lock);
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
  
    /* If there is an error, other than ENOTCONN, its failure */
    if ((op_ret == -1 && op_errno != ENOTCONN)) {
      local->op_ret = -1;
      local->op_errno = op_errno;
    }
    callcnt = --local->call_count;
  }
  UNLOCK (&frame->lock);

  if (callcnt == 0) {
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
  struct list_head *list = afr_inode_to_giclist (this, fd->inode);
  afr_local_t *local = calloc (1, sizeof (afr_local_t));
  dict_t *afrctx;
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  afrctx = data_to_ptr(dict_get (fd->ctx, this->name));
  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name))
      local->call_count++;
  }
  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name)) {
      STACK_WIND (frame, 
		  afr_readdir_cbk,
		  gic->xl,
		  gic->xl->fops->readdir,
		  size,
		  offset,
		  fd);
    }
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

  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (op_ret == 0)
    local->op_ret = 0;

  if (callcnt == 0) {
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
  frame->local = local;
  dict_t *afrctx = data_to_ptr(dict_get (fd->ctx, this->name));
  list = afr_inode_to_giclist(this, fd->inode);
  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name))
      local->call_count++;
  }

  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name)) {
      STACK_WIND (frame,
		  afr_writedir_cbk,
		  gic->xl,
		  gic->xl->fops->writedir,
		  fd,
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
  gf_inode_child_t *gic, *gictemp;
  call_frame_t *prev_frame = cookie;
  int32_t callcnt;
  inode_t *inoptr = local->loc->inode;
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
    gic->inode = inode;
    gic->stat = *buf;
  }

  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (callcnt == 0){
    if (local->op_ret == 0) {
      list_for_each_entry (gic, list, clist) {
	if (gic->inode)
	  break;
      }
      afr_inode_private_t *aip = calloc (1, sizeof (*aip));
      aip->giclist = list;
      dict_set (inoptr->ctx, this->name, data_from_static_ptr(aip));
    } else {
      list_for_each_entry_safe (gic, gictemp, list, clist) {
	list_del (&gic->clist);
	freee (gic);
      }
      freee (list);
    }
    struct stat *statptr = local->op_ret == 0 ? &gic->stat : NULL;
    afr_loc_free(local->loc);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inoptr,
		  statptr);
  }
  return 0;
}

static int32_t
afr_mkdir (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode)
{
  AFR_DEBUG_FMT(this, "path %s", loc->path);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list;
  xlator_list_t *trav = this->children;

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->loc = afr_loc_dup(loc);
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
		loc,
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

  LOCK (&frame->lock);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
    STACK_UNWIND (frame, local->op_ret, local->op_errno);
  }
  return 0;
}

static int32_t
afr_unlink (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  AFR_DEBUG_FMT(this, "loc->path = %s loc->inode = %u",loc->path, loc->inode->ino);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list = afr_inode_to_giclist(this, loc->inode);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }
  list_for_each_entry (gic, list, clist) {
    if (gic->inode){
      STACK_WIND(frame,
		 afr_unlink_cbk,
		 gic->xl,
		 gic->xl->fops->unlink,
		 loc);
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

  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
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
  struct list_head *list = afr_inode_to_giclist (this, loc->inode);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }
  list_for_each_entry (gic, list, clist) {
    if (gic->inode){
      STACK_WIND(frame,
		 afr_rmdir_cbk,
		 gic->xl,
		 gic->xl->fops->rmdir,
		 loc);
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
  call_frame_t *prev_frame = cookie;
  LOCK(&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (op_ret == -1)
    GF_ERROR (this, "(path=%s child=%d) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
  if (callcnt == 0) {
    afr_loc_free(local->loc);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  local->fd,
		  local->inode,
		  &local->stbuf);
  }
  return 0;
}

static int32_t
afr_create_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		fd_t *fd,
		inode_t *inode,
		struct stat *stbuf)
{
  afr_local_t *local = frame->local;
  struct list_head *list;
  gf_inode_child_t *gic, *gictemp;
  call_frame_t *prev_frame = cookie;
  int32_t callcnt;
  inode_t *inoptr = local->loc->inode;
  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  if (op_ret == -1) {
    GF_ERROR (this, "(path=%s child=%s) op_ret=%d op_errno=%d", local->loc->path, prev_frame->this->name, op_ret, op_errno);
  }
  if (op_ret >= 0) {
    LOCK (&frame->lock);
    dict_t *afrctx;
    data_t *afrctx_data;
    afrctx_data = dict_get (fd->ctx, this->name);
    if (afrctx_data == NULL) {
      afrctx = get_new_dict ();
      dict_set (fd->ctx, this->name, data_from_static_ptr (afrctx));
      /* we use the path here just for debugging */
      dict_set (afrctx, "path", data_from_ptr (strdup(local->loc->path))); /* strduped mem will be freed on dict_destroy */
    } else
      afrctx = data_to_ptr (afrctx_data);
    dict_set (afrctx, prev_frame->this->name, data_from_uint32(1)); /* indicates open is success in that child */
    UNLOCK (&frame->lock);
  }
  list = local->list;
  list_for_each_entry (gic, list, clist) {
    if (gic->xl == prev_frame->this) {
      break;
    }
  }
  if (local->op_ret == -1 && op_ret >= 0)
    local->op_ret = op_ret;

  if (op_ret >= 0) {
    gic->inode = inode;
    gic->stat = *stbuf;
  } else {
    gic->inode = NULL;
  }
  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (callcnt == 0){
    if (local->op_ret >= 0) {
      list_for_each_entry (gic, list, clist) {
	if (gic->inode)
	  break;
      }
      afr_inode_private_t *aip = calloc (1, sizeof (*aip));
      aip->giclist = list;
      dict_set (local->loc->inode->ctx, this->name, data_from_static_ptr(aip));

      if (((afr_private_t*)this->private)->self_heal) {
	local->inode = local->loc->inode;
	local->fd = fd;
	local->stbuf = gic->stat;
	list_for_each_entry (gic, list, clist) {
	  if (gic->inode)
	    local->call_count++;
	}
	dict_t *dict = get_new_dict();
	if (dict) {
	  dict->lock = calloc (1, sizeof (pthread_mutex_t));
	  pthread_mutex_init (dict->lock, NULL);
	  dict_ref (dict);
	}

	struct timeval tv;
	gettimeofday (&tv, NULL);
	uint32_t ctime = tv.tv_sec;
	char dict_ctime[100], dict_version[100];
	sprintf (dict_ctime, "%u", ctime);
	sprintf (dict_version, "%u", 0);
	dict_set (dict, "trusted.afr.createtime", bin_to_data (dict_ctime, strlen (dict_ctime)));
	dict_set (dict, "trusted.afr.version", bin_to_data(dict_version, strlen (dict_version)));
	GF_DEBUG (this, "createtime = %s", dict_ctime);
	GF_DEBUG (this, "version = %s len = %d", dict_version, strlen(dict_version));
	/* FIXME iterate over fdlist */
	list_for_each_entry (gic, list, clist) {
	  if (gic->inode) {
	    STACK_WIND (frame,
			afr_create_setxattr_cbk,
			gic->xl,
			gic->xl->fops->setxattr,
			local->loc,
			dict,
			0);
	  }
	}
	dict_unref (dict);

	return 0;
      }
    } else {
      /* free the list as it is not used */
      list_for_each_entry_safe (gic, gictemp, list, clist) {
	list_del (&gic->clist);
	freee (gic);
      }
      freee (list);
    }

    struct stat *statptr = local->op_ret >= 0 ? &gic->stat : NULL;
    afr_loc_free(local->loc);
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  fd,
		  inoptr,
		  statptr);
  }
  return 0;
}

static int32_t
afr_create (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t flags,
	    mode_t mode,
	    fd_t *fd)
{
  AFR_DEBUG_FMT (this, "path = %s", loc->path);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  int32_t num_copies = afr_get_num_copies (loc->path, this);
  gf_inode_child_t *gic;
  struct list_head *list;
  xlator_list_t *trav = this->children;
  
  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  if (num_copies == 0)
    num_copies = 1;
  local->loc = afr_loc_dup(loc);
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
		loc,
		flags,
		mode,
		fd);
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
  gf_inode_child_t *gic, *gictemp;
  call_frame_t *prev_frame = cookie;
  int32_t callcnt;
  inode_t *inoptr = local->loc->inode;
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
    gic->inode = inode;
    gic->stat = *stbuf;
  }

  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (callcnt == 0){
    if (local->op_ret == 0) {
      list_for_each_entry (gic, list, clist) {
	if (gic->inode)
	  break;
      }
      afr_inode_private_t *aip = calloc (1, sizeof (*aip));
      aip->giclist = list;
      dict_set (inoptr->ctx, this->name, data_from_static_ptr(aip));
    } else {
      list_for_each_entry_safe (gic, gictemp, list, clist) {
	list_del (&gic->clist);
	freee (gic);
      }
      freee (list);
    }
    afr_loc_free(local->loc);
    struct stat *statptr = local->op_ret == 0 ? &gic->stat : NULL;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inoptr,
		  statptr);
  }
  return 0;
}

static int32_t
afr_mknod (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode,
	   dev_t dev)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list;
  xlator_list_t *trav = this->children;

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->loc = afr_loc_dup(loc);
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
		loc,
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
  gf_inode_child_t *gic, *gictemp;
  call_frame_t *prev_frame = cookie;
  int32_t callcnt;
  inode_t *inoptr = local->loc->inode;
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
    gic->inode = inode;
    gic->stat = *stbuf;
  }

  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (callcnt == 0){
    if (local->op_ret == 0) {
      list_for_each_entry (gic, list, clist) {
	if (gic->inode)
	  break;
      }
      afr_inode_private_t *aip = calloc (1, sizeof (*aip));
      aip->giclist = list;
      dict_set (inoptr->ctx, this->name, data_from_static_ptr(aip));
    } else {
      list_for_each_entry_safe (gic, gictemp, list, clist) {
	list_del (&gic->clist);
	freee (gic);
      }
      freee (list);
    }
    afr_loc_free(local->loc);
    struct stat *statptr = local->op_ret == 0 ? &gic->stat : NULL;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inoptr,
		  statptr);
  }
  return 0;
}

static int32_t
afr_symlink (call_frame_t *frame,
	     xlator_t *this,
	     const char *linkname,
	     loc_t *loc)
{
  AFR_DEBUG_FMT(this, "linkname %s loc->path %s", linkname, loc->path);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list;
  xlator_list_t *trav = this->children;

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->loc = afr_loc_dup(loc);
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
		loc);
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

  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (callcnt == 0){
    if (local->op_ret == 0) {
      list_for_each_entry (gic, list, clist) {
	if (gic->inode)
	  break;
      }
    }
    struct stat stat;
    /* FIXME preserve the inode number */
    if (local->op_ret == 0)
      stat = gic->stat;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  &stat);
  }
  return 0;
}

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

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->list = list = afr_inode_to_giclist (this, oldloc->inode);
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      local->call_count++;
    }
  }
  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      STACK_WIND (frame,
		  afr_rename_cbk,
		  gic->xl,
		  gic->xl->fops->rename,
		  oldloc,
		  newloc);
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
  gf_inode_child_t *gic, *gictemp;
  call_frame_t *prev_frame = cookie;
  int32_t callcnt;
  inode_t *inoptr = local->loc->inode;
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
      gic->inode = inode;
    gic->stat = *stbuf;
  } else {
    gic->inode = NULL;
  }

  LOCK (&frame->lock);
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);

  if (callcnt == 0){
    if (local->op_ret == 0) {
      list_for_each_entry (gic, list, clist) {
	if (gic->inode)
	  break;
      }
      afr_inode_private_t *aip = calloc (1, sizeof (*aip));
      aip->giclist = list;
      dict_set (inoptr->ctx, this->name, data_from_static_ptr(aip));
    } else {
      list_for_each_entry_safe (gic, gictemp, list, clist) {
	list_del (&gic->clist);
	freee (gic);
      }
      freee (list);
    }
    afr_loc_free(local->loc);
    struct stat *statptr = local->op_ret == 0 ? &gic->stat : NULL;
    STACK_UNWIND (frame,
		  local->op_ret,
		  local->op_errno,
		  inoptr,
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
  AFR_DEBUG_FMT(this, "oldloc->path %s newpath %s", oldloc->path, newpath);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));
  gf_inode_child_t *gic;
  struct list_head *list;

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;
  local->loc = afr_loc_dup(oldloc);
  list = afr_inode_to_giclist (this, oldloc->inode);
  local->list = list;
  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      STACK_WIND (frame,
		  afr_link_cbk,
		  gic->xl,
		  gic->xl->fops->link,
		  oldloc,
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
  LOCK (&frame->lock);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    local->stbuf = *stbuf;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
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
  struct list_head *list = afr_inode_to_giclist (this, loc->inode);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if(gic->inode)
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (gic->inode){
      STACK_WIND(frame,
		 afr_chmod_cbk,
		 gic->xl,
		 gic->xl->fops->chmod,
		 loc,
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
  LOCK (&frame->lock);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
    local->stbuf = *stbuf;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
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
  struct list_head *list = afr_inode_to_giclist (this, loc->inode);

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if (gic->inode)
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (gic->inode) {
      STACK_WIND(frame,
		 afr_chown_cbk,
		 gic->xl,
		 gic->xl->fops->chown,
		 loc,
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
  AFR_DEBUG_FMT(this, "op_ret = %d", op_ret);
  afr_local_t *local = frame->local;
  int32_t callcnt;

  if (op_ret != 0 && op_errno != ENOENT && op_errno != ENOTCONN) {
    local->op_errno = op_errno;
  }
  LOCK (&frame->lock);
  if (op_ret == 0 && local->op_ret == -1) {
    local->op_ret = op_ret;
    local->op_errno = op_errno;
  }
  callcnt = --local->call_count;
  UNLOCK (&frame->lock);
  if (callcnt == 0) {
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
  struct list_head *list = afr_inode_to_giclist (this, fd->inode);
  dict_t *afrctx = data_to_ptr(dict_get (fd->ctx, this->name));

  frame->local = local;
  local->op_ret = -1;
  local->op_errno = ENOENT;

  list_for_each_entry (gic, list, clist) {
    if (dict_get (afrctx, gic->xl->name))    
      ++local->call_count;
  }

  list_for_each_entry (gic, list, clist) {
    if (dict_get(afrctx, gic->xl->name)) {
      STACK_WIND (frame,
		  afr_closedir_cbk,
		  gic->xl,
		  gic->xl->fops->closedir,
		  fd);
    }
  }
  dict_destroy (afrctx);
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
    LOCK (&frame->lock);
    local->xlnodeptr = local->xlnodeptr->next;
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

static int32_t
afr_stats (call_frame_t *frame,
	   xlator_t *this,
	   int32_t flags)
{
  AFR_DEBUG(this);
  afr_local_t *local = (void *) calloc (1, sizeof (afr_local_t));

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
  int32_t upclients = 0;

  switch (event) {
  case GF_EVENT_CHILD_UP:
    list_for_each_entry (acs, list, clist) {
      if (data == acs->xl)
	break;
    }
    AFR_DEBUG_FMT (this, "CHILD_UP from %s", acs->xl->name);
    acs->state = 1;
    /* if all the children were down, and one child came up, send notify to parent */
    list_for_each_entry (acs, list, clist) {
      if (acs->state == 1)
	upclients++;
    }
    if (upclients == 1)
      default_notify (this, event, data);
    break;
  case GF_EVENT_CHILD_DOWN:
    list_for_each_entry (acs, list, clist) {
      if (data == acs->xl)
	break;
    }
    AFR_DEBUG_FMT (this, "CHILD_DOWN from %s", acs->xl->name);
    acs->state = 0;
    /* if the only child that was up went down, send notify to the parent */
    list_for_each_entry (acs, list, clist) {
      if (acs->state == 1)
	upclients++;
    }
    if (upclients == 0)
      default_notify (this, event, data);
    break;
  default:
    default_notify (this, event, data);
  }
  return 0;
}

void
afr_parse_replicate (char *data, xlator_t *xl)
{
  char *tok, *colon;
  int32_t num_tokens = 0, max_copies = 0;
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
    if (pattern_info_list[num_tokens].copies > ((afr_private_t *)xl->private)->child_count) {
      GF_ERROR (xl, "for %s pattern, number of copies (%d) > number of children of afr (%d)", tok, pattern_info_list[num_tokens].copies, ((afr_private_t *)xl->private)->child_count);
      exit (1);
    }
    if (pattern_info_list[num_tokens].copies > max_copies)
      max_copies = pattern_info_list[num_tokens].copies;
    num_tokens++;
    tok = strtok (NULL, ",");
  } while(tok);
  if (max_copies < ((afr_private_t*)xl->private)->child_count)
    GF_WARNING (xl, "maximum number of copies used = %d, child count = %d", max_copies, ((afr_private_t*)xl->private)->child_count);
  ((afr_private_t*)xl->private)->pil_num = num_tokens;
}

#if 0
#include <malloc.h> /* This does not work on FreeBSD */
static void *(*old_free_hook)(void *, const void *);

static void
afr_free_hook (void *ptr, const void *caller)
{
  __free_hook = old_free_hook;
  memset (ptr, 255, malloc_usable_size(ptr));
  freee (ptr);
  __free_hook = afr_free_hook;
  
}

static void
afr_init_hook (void)
{
  old_free_hook = __free_hook;
  __free_hook = afr_free_hook;
}

void (*__malloc_initialize_hook) (void) = afr_init_hook;
#endif

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

  trav = this->children;
  this->private = pvt;
  while (trav) {
    count++;
    trav = trav->next;
  }
  GF_DEBUG (this, "%s children count = %d", this->name, count);
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
  } else
    GF_DEBUG (this, "self-heal is enabled (default)");

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
  /* pvt->children will have list of children which maintains its state (up/down) */
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
fini (xlator_t *this)
{
  freee (((afr_private_t *)this->private)->pattern_info_list);
  freee (this->private);
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
  /*  .statfs      = afr_statfs, */ /* currently using default_statfs */
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

