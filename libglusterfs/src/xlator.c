/*
   Copyright (c) 2006, 2007 Z RESEARCH, Inc. <http://www.zresearch.com>
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


#include "xlator.h"
#include <dlfcn.h>
#include <netdb.h>
#include "defaults.h"

#define SET_DEFAULT_FOP(fn) do {    \
    if (!xl->fops->fn)              \
       xl->fops->fn = default_##fn; \
} while (0)

#define SET_DEFAULT_MOP(fn) do {        \
    if (!xl->mops->fn)                  \
       xl->mops->fn = default_##fn;     \
} while (0)

static void
fill_defaults (xlator_t *xl)
{
  SET_DEFAULT_FOP (create);
  SET_DEFAULT_FOP (open);
  SET_DEFAULT_FOP (stat);
  SET_DEFAULT_FOP (readlink);
  SET_DEFAULT_FOP (mknod);
  SET_DEFAULT_FOP (mkdir);
  SET_DEFAULT_FOP (unlink);
  SET_DEFAULT_FOP (rmdir);
  SET_DEFAULT_FOP (rmelem);
  SET_DEFAULT_FOP (incver);
  SET_DEFAULT_FOP (symlink);
  SET_DEFAULT_FOP (rename);
  SET_DEFAULT_FOP (link);
  SET_DEFAULT_FOP (chmod);
  SET_DEFAULT_FOP (chown);
  SET_DEFAULT_FOP (truncate);
  SET_DEFAULT_FOP (utimens);
  SET_DEFAULT_FOP (readv);
  SET_DEFAULT_FOP (writev);
  SET_DEFAULT_FOP (statfs);
  SET_DEFAULT_FOP (flush);
  SET_DEFAULT_FOP (close);
  SET_DEFAULT_FOP (fsync);
  SET_DEFAULT_FOP (setxattr);
  SET_DEFAULT_FOP (getxattr);
  SET_DEFAULT_FOP (removexattr);
  SET_DEFAULT_FOP (opendir);
  SET_DEFAULT_FOP (readdir);
  SET_DEFAULT_FOP (closedir);
  SET_DEFAULT_FOP (fsyncdir);
  SET_DEFAULT_FOP (access);
  SET_DEFAULT_FOP (ftruncate);
  SET_DEFAULT_FOP (fstat);
  SET_DEFAULT_FOP (lk);
  SET_DEFAULT_FOP (lookup);
  SET_DEFAULT_FOP (forget);
  SET_DEFAULT_FOP (fchown);
  SET_DEFAULT_FOP (fchmod);
  SET_DEFAULT_FOP (setdents);
  SET_DEFAULT_FOP (getdents);

  SET_DEFAULT_MOP (stats);
  SET_DEFAULT_MOP (lock);
  SET_DEFAULT_MOP (unlock);
  SET_DEFAULT_MOP (listlocks);
  SET_DEFAULT_MOP (checksum);

  if (!xl->notify)
    xl->notify = default_notify;

  return;
}

void
xlator_set_type (xlator_t *xl, 
		 const char *type)
{
  char *name = NULL;
  void *handle = NULL;

  asprintf (&xl->type, "%s\n", type);

  gf_log ("libglusterfs/xlator",
	  GF_LOG_DEBUG,
	  "attempt to load type %s",
	  type);

  asprintf (&name, "%s/%s.so", XLATORDIR, type);

  gf_log ("libglusterfs/xlator",
	  GF_LOG_DEBUG,
	  "attempt to load file %s",
	  name);

  handle = dlopen (name, RTLD_NOW|RTLD_GLOBAL);

  if (!handle) {
    gf_log ("libglusterfs/xlator",
	    GF_LOG_ERROR,
	    "dlopen(%s): %s", 
	    name, dlerror ());
    exit (1);
  }

  if (!(xl->fops = dlsym (handle, "fops"))) {
    gf_log ("libglusterfs/xlator",
	    GF_LOG_ERROR,
	    "dlsym(fops) on %s",
	    dlerror ());
    exit (1);
  }
  if (!(xl->mops = dlsym (handle, "mops"))) {
    gf_log ("libglusterfs/xlator",
	    GF_LOG_ERROR,
	    "dlsym(mops) on %s",
	    dlerror ());
    exit (1);
  }

  if (!(xl->init = dlsym (handle, "init"))) {
    gf_log ("libglusterfs/xlator",
	    GF_LOG_ERROR,
	    "dlsym(init) on %s",
	    dlerror ());
    exit (1);
  }

  if (!(xl->fini = dlsym (handle, "fini"))) {
    gf_log ("libglusterfs/xlator",
	    GF_LOG_ERROR,
	    "dlsym(fini) on %s",
	    dlerror ());
    exit (1);
  }

  if (!(xl->notify = dlsym (handle, "notify"))) {
    gf_log ("libglusterfs/xlator",
	    GF_LOG_DEBUG,
	    "dlsym(notify) on %s -- neglecting",
	    dlerror ());
  }

  fill_defaults (xl);

  freee (name);
  return ;
}

static void
_foreach_dfs (xlator_t *this,
	      void (*fn)(xlator_t *each,
			 void *data),
	      void *data)
{
  xlator_list_t *child = this->children;

  while (child) {
    _foreach_dfs (child->xlator, fn, data);
    child = child->next;
  }

  fn (this, data);
}

void
xlator_foreach (xlator_t *this,
		void (*fn)(xlator_t *each,
			   void *data),
		void *data)
{
  xlator_t *first;

  first = this;

  while (first->prev)
    first = first->prev;

  while (first) {
    fn (first, data);
    first = first->next;
  }
}


xlator_t *
xlator_search_by_name (xlator_t *any, const char *name)
{
  xlator_t *search = NULL;

  search = any;

  while (search->prev)
    search = search->prev;

  while (search) {
    if (!strcmp (search->name, name))
      break;
    search = search->next;
  }

  return search;
}


static int32_t
xlator_init_rec (xlator_t *xl)
{
  int32_t ret = 0;
  xlator_list_t *trav = xl->children;

  while (trav) {
    ret = xlator_init_rec (trav->xlator);
    if (ret != 0)
      break;
    trav = trav->next;
  }

  if (!ret && !xl->ready) {
    if (xl->init)
      ret = xl->init (xl);
    xl->ready = 1;
  }

  return ret;
}


int32_t
xlator_tree_init (xlator_t *xl)
{
  xlator_t *top;
  int32_t ret;

  top = xl;

  while (top->parent)
    top = top->parent;

  ret = xlator_init_rec (top);

  if (ret == 0) {
    top->notify (top, GF_EVENT_PARENT_UP, top->parent);
  }

  return ret;
}

fd_t *
fd_create (inode_t *inode)
{
  fd_t *fd = calloc (1, sizeof (*fd));

  fd->ctx = get_new_dict ();
  fd->ctx->is_locked = 1;
  fd->inode = inode_ref (inode);

  LOCK (&inode->lock);
  {
    list_add (&fd->inode_list, &inode->fds);
  }
  UNLOCK (&inode->lock);

  return fd;
}

void
fd_destroy (fd_t *fd)
{
  LOCK (&fd->inode->lock);
  {
    list_del (&fd->inode_list);
  }
  UNLOCK (&fd->inode->lock);

  inode_unref (fd->inode);
  fd->inode = (inode_t *)0xaaaaaaaa;
  dict_destroy (fd->ctx);
  freee (fd);
}
