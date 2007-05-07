/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
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


#include "glusterfs.h"
#include "stat-prefetch.h"
#include "dict.h"
#include "xlator.h"
#include <sys/time.h>

struct sp_cache {
  struct sp_cache *next;
  struct sp_cache *prev;
  pid_t pid;
  long long tv_time;
  char *dirname;
  dir_entry_t entries;
  int32_t count;
  pthread_mutex_t lock;
};

static void
stat_prefetch_cache_flush (struct sp_cache *cache, int32_t force)
{
  struct sp_cache *trav;
  struct timeval tv;
  long long tv_time;

  gettimeofday (&tv, NULL);
  tv_time = (tv.tv_usec + (tv.tv_sec * 1000000));

  pthread_mutex_lock (&cache->lock);

  trav = cache->next;
  while (trav != cache) {
    struct sp_cache *next = trav->next;
    {
      if (tv_time > trav->tv_time || force) {
	gf_log ("stat-prefetch",
		GF_LOG_DEBUG,
		"flush on: %s",
		trav->dirname);
	dir_entry_t *entries;

	trav->prev->next = trav->next;
	trav->next->prev = trav->prev;

	entries = trav->entries.next;

	while (entries) {
	  dir_entry_t *nextentry = entries->next;
	  {
	    free (entries->name);
	    free (entries);
	  }
	  entries = nextentry;
	}
	free (trav->dirname);
	free (trav);
      }
    }
    trav = next;
  }

  pthread_mutex_unlock (&cache->lock);
}

static int32_t
stat_prefetch_cache_fill (struct sp_cache *cache,
			  pid_t pid,
			  char *dirname,
			  dir_entry_t *entries)
{
  struct sp_cache *trav;
  struct timeval tv;

  pthread_mutex_unlock (&cache->lock);
  trav = cache->next;
  while (trav != cache) {
    //    if (trav->pid == pid && !strcmp (trav->dirname, dirname)) {
    if (!strcmp (trav->dirname, dirname)) {
      break;
    }
    trav = trav->next;
  }

  if (trav == cache) {
    trav = calloc (1, sizeof (*trav));
    trav->pid = pid;
    trav->dirname = dirname;

    trav->prev = cache->prev;
    trav->next = cache;
    trav->next->prev = trav;
    trav->prev->next = trav;
  } else {
    free (dirname);
  }

  while (trav->entries.next) {
    dir_entry_t *tmp = trav->entries.next;

    trav->entries.next = trav->entries.next->next;
    free (tmp->name);
    free (tmp);
  }
  trav->entries.next = entries->next;
  entries->next = NULL;

  gettimeofday (&tv, NULL);
  trav->tv_time = (tv.tv_usec + (tv.tv_sec * 1000000)) + cache->tv_time;

  pthread_mutex_unlock (&cache->lock);
  return 0;
}

static int32_t
stat_prefetch_cache_lookup (struct sp_cache *cache,
			    pid_t pid,
			    const char *path,
			    struct stat *buf)
{
  struct sp_cache *trav;
  char *dirname = strdup (path);
  char *filename = strrchr (dirname, '/');
  dir_entry_t *entries;
  dir_entry_t *prev = NULL;

  *filename = '\0';
  filename ++;

  pthread_mutex_lock (&cache->lock);
  trav = cache->next;
  while (trav != cache) {
    //    if ((trav->pid == pid) && !strcmp (dirname, trav->dirname))
    if (!strcmp (dirname, trav->dirname))
      break;
    trav = trav->next;
  }
  if (trav == cache) {
    free (dirname);
    pthread_mutex_unlock (&cache->lock);
    return -1;
  }

  entries = trav->entries.next;
  prev = &trav->entries;
  while (entries) {
    if (!strcmp (entries->name, filename))
      break;
    prev = entries;
    entries = entries->next;
  }
  if (!entries) {
    free (dirname);
    pthread_mutex_unlock (&cache->lock);
    return -1;
  }

  *buf = entries->buf;
  prev->next = entries->next;
  free (entries->name);
  free (entries);
  free (dirname);

  pthread_mutex_unlock (&cache->lock);

  return 0;
}

			    
static int32_t
stat_prefetch_readdir_cbk (call_frame_t *frame,
			   void *cooky,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno,
			   dir_entry_t *entries,
			   int32_t count)
{
  char *path = frame->local;
  pid_t pid = frame->root->pid;
  frame->local = NULL;

  STACK_UNWIND (frame, op_ret, op_errno, entries, count);

  if (op_ret == 0)
    stat_prefetch_cache_fill (this->private,
			      pid,
			      path,
			      entries);
  else
    free (path);

  return 0;
}

static int32_t
stat_prefetch_readdir (call_frame_t *frame,
		       xlator_t *this,
		       const char *path)
{
  stat_prefetch_cache_flush (this->private, 0);

  frame->local = strdup (path);
  STACK_WIND (frame,
	      stat_prefetch_readdir_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->readdir,
	      path);
  return 0;
}


static int32_t
stat_prefetch_getattr_cbk (call_frame_t *frame,
			   void *cooky,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno,
			   struct stat *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t
stat_prefetch_getattr (call_frame_t *frame,
		       struct xlator *this,
		       const char *path)
{
  struct stat buf;
  pid_t pid = frame->root->pid;
  stat_prefetch_cache_flush (this->private, 0);

  if (stat_prefetch_cache_lookup (this->private,
				  pid,
				  path,
				  &buf) == 0) {
    STACK_UNWIND (frame, 0, 0, &buf);
    return 0;
  }

  STACK_WIND (frame,
	      stat_prefetch_getattr_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->fops->getattr,
	      path);

  return 0;
}


static int32_t
stat_prefetch_unlink_cbk (call_frame_t *frame,
                          void *cooky,
                          xlator_t *this,
                          int32_t op_ret,
                          int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
stat_prefetch_unlink (call_frame_t *frame,
                      struct xlator *this,
                      const char *path)
{
  stat_prefetch_cache_flush (this->private, 1);

  STACK_WIND (frame,
              stat_prefetch_unlink_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->unlink,
              path);

  return 0;
}


static int32_t
stat_prefetch_chmod_cbk (call_frame_t *frame,
			 void *cooky,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t
stat_prefetch_chmod (call_frame_t *frame,
		     struct xlator *this,
		     const char *path,
		     mode_t mode)
{
  stat_prefetch_cache_flush (this->private, 1);

  STACK_WIND (frame,
              stat_prefetch_chmod_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->chmod,
              path,
	      mode);

  return 0;
}


static int32_t
stat_prefetch_chown_cbk (call_frame_t *frame,
			 void *cooky,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t
stat_prefetch_chown (call_frame_t *frame,
		     struct xlator *this,
		     const char *path,
		     uid_t uid,
		     gid_t gid)
{
  stat_prefetch_cache_flush (this->private, 1);

  STACK_WIND (frame,
              stat_prefetch_chown_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->chown,
              path,
	      uid,
	      gid);

  return 0;
}


static int32_t
stat_prefetch_utimes_cbk (call_frame_t *frame,
                          void *cooky,
                          xlator_t *this,
                          int32_t op_ret,
                          int32_t op_errno,
			  struct stat *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t
stat_prefetch_utimes (call_frame_t *frame,
		      struct xlator *this,
		      const char *path,
		      struct timespec *tvp)
{
  stat_prefetch_cache_flush (this->private, 1);

  STACK_WIND (frame,
              stat_prefetch_utimes_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->utimes,
              path,
	      tvp);

  return 0;
}


static int32_t
stat_prefetch_truncate_cbk (call_frame_t *frame,
			    void *cooky,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno,
			    struct stat *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

static int32_t
stat_prefetch_truncate (call_frame_t *frame,
			struct xlator *this,
			const char *path,
			off_t offset)
{
  stat_prefetch_cache_flush (this->private, 1);

  STACK_WIND (frame,
              stat_prefetch_truncate_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->truncate,
              path,
	      offset);

  return 0;
}


static int32_t
stat_prefetch_rename_cbk (call_frame_t *frame,
                          void *cooky,
                          xlator_t *this,
                          int32_t op_ret,
                          int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t
stat_prefetch_rename (call_frame_t *frame,
                      struct xlator *this,
                      const char *oldpath,
		      const char *newpath)
{
  stat_prefetch_cache_flush (this->private, 1);

  STACK_WIND (frame,
              stat_prefetch_rename_cbk,
              FIRST_CHILD(this),
              FIRST_CHILD(this)->fops->rename,
              oldpath,
	      newpath);

  return 0;
}

int32_t 
init (struct xlator *this)
{
  struct sp_cache *cache;
  dict_t *options = this->options;

  if (!this->children || this->children->next) {
    gf_log ("stat-prefetch",
	    GF_LOG_ERROR,
	    "FATAL: translator %s does not have exactly one child node",
	    this->name);
    return -1;
  }

  cache = (void *) calloc (1, sizeof (*cache));
  cache->next = cache->prev = cache;

  cache->tv_time = 1 * 1000000;

  if (dict_get (options, "cache-seconds")) {
    cache->tv_time = (data_to_int (dict_get (options, "cache-seconds")) *
		      1000000);
  }

  pthread_mutex_init (&cache->lock, NULL);

  this->private = cache;
  return 0;
}

void
fini (struct xlator *this)
{
  return;
}


struct xlator_fops fops = {
  .getattr     = stat_prefetch_getattr,
  .readdir     = stat_prefetch_readdir,
  .unlink      = stat_prefetch_unlink,
  .chmod       = stat_prefetch_chmod,
  .chown       = stat_prefetch_chown,
  .rename      = stat_prefetch_rename,
  .utimes      = stat_prefetch_utimes,
  .truncate    = stat_prefetch_truncate,
};

struct xlator_mops mops = {
};
