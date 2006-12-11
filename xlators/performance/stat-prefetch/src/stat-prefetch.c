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
};

static void
stat_prefetch_cache_flush (struct sp_cache *cache)
{
  struct sp_cache *trav = cache->next;
  struct timeval tv;
  long long tv_time;

  gettimeofday (&tv, NULL);
  tv_time = (tv.tv_usec + (tv.tv_sec * 1000000));

  while (trav != cache) {
    struct sp_cache *next = trav->next;
    {
      if (tv_time > trav->tv_time) {
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
}

static int32_t
stat_prefetch_cache_fill (struct sp_cache *cache,
			  pid_t pid,
			  char *dirname,
			  dir_entry_t *entries)
{
  struct sp_cache *trav = cache->next;
  struct timeval tv;

  while (trav != cache) {
    if (trav->pid == pid && !strcmp (trav->dirname, dirname))
      break;
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
  }

  trav->entries.next = entries->next;
  entries->next = NULL;

  gettimeofday (&tv, NULL);
  trav->tv_time = (tv.tv_usec + (tv.tv_sec * 1000000)) + cache->tv_time;
  return 0;
}

static int32_t
stat_prefetch_cache_lookup (struct sp_cache *cache,
			    pid_t pid,
			    const char *path,
			    struct stat **buf)
{
  struct sp_cache *trav = cache->next;
  char *dirname = strdup (path);
  char *filename = strrchr (dirname, '/');
  dir_entry_t *entries;

  *filename = '\0';
  filename ++;

  while (trav != cache) {
    //    if ((trav->pid == pid) && !strcmp (dirname, trav->dirname))
    if (!strcmp (dirname, trav->dirname))
      break;
    trav = trav->next;
  }
  if (trav == cache)
    return -1;

  entries = trav->entries.next;
  while (entries) {
    if (!strcmp (entries->name, filename))
      break;
    entries = entries->next;
  }
  if (!entries)
    return -1;
  *buf = &entries->buf;
  return 0;
}

			    
static int32_t
stat_prefetch_readdir_cbk (call_frame_t *frame,
			   call_frame_t *prev_frame,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno,
			   dir_entry_t *entries,
			   int32_t count)
{
  char *path = frame->local;

  frame->local = NULL;

  STACK_UNWIND (frame, op_ret, op_errno, entries, count);

  if (op_ret == 0)
    stat_prefetch_cache_fill (this->private,
			      frame->root->pid,
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
  frame->local = strdup (path);
  STACK_WIND (frame,
	      stat_prefetch_readdir_cbk,
	      this->first_child,
	      this->first_child->fops->readdir,
	      path);
  stat_prefetch_cache_flush (this->private);
  return 0;
}


static int32_t
stat_prefetch_getattr_cbk (call_frame_t *frame,
			   call_frame_t *prev_frame,
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
  struct stat *buf;

  if (stat_prefetch_cache_lookup (this->private,
				  frame->root->pid,
				  path,
				  &buf) == 0) {
    STACK_UNWIND (frame, 0, 0, buf);
    return 0;
  }

  STACK_WIND (frame,
	      stat_prefetch_getattr_cbk,
	      this->first_child,
	      this->first_child->fops->getattr,
	      path);
  stat_prefetch_cache_flush (this->private);
  return 0;
}

int32_t 
init (struct xlator *this)
{
  struct sp_cache *cache;
  dict_t *options = this->options;

  if (!this->first_child || this->first_child->next_sibling) {
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
};

struct xlator_mops mops = {
};
