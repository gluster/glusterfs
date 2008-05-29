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

#include "mem-pool.h"

#define GF_MEM_POOL_PAD_BOUNDRY    16

struct mem_pool *
mem_pool_new_fn (unsigned long sizeof_type,
		 unsigned long count)
{
  struct mem_pool *mem_pool = NULL;
  int pad = 0;
  unsigned long padded_sizeof_type = 0;
  void *pool = NULL;
  int i = 0;
  struct list_head *list = NULL;
  
  if (sizeof_type == 0 && count == 0)
    {
      gf_log ("mem-pool", GF_LOG_ERROR, "invalid argument");
      return NULL;
    }
  
  pad = GF_MEM_POOL_PAD_BOUNDRY - (sizeof_type % GF_MEM_POOL_PAD_BOUNDRY);
  padded_sizeof_type = sizeof_type + pad;
  
  mem_pool = calloc (1, sizeof (*mem_pool));
  ERR_ABORT (mem_pool);

  LOCK_INIT (&mem_pool->lock);
  INIT_LIST_HEAD (&mem_pool->list);
  mem_pool->padded_sizeof_type = padded_sizeof_type;
  mem_pool->cold_count = count;

  pool = calloc (count, sizeof_type + pad);
  if (!pool)
    return NULL;

  for (i=0; i<count; i++)
    {
      list = pool + (i * (sizeof_type + pad));
      INIT_LIST_HEAD (list);
      list_add_tail (list, &mem_pool->list);
    }

  mem_pool->pool = pool;
  mem_pool->pool_end = pool + (count * (sizeof_type + pad));

  return mem_pool;
}


void *
mem_get (struct mem_pool *mem_pool)
{
  struct list_head *list = NULL;
  void *ptr = NULL;
  
  if (mem_pool == NULL)
    {
      gf_log ("mem-pool", GF_LOG_ERROR, "invalid argument");
      return NULL;
    }

  LOCK (&mem_pool->lock);
  {
    if (mem_pool->cold_count)
      {
	list = mem_pool->list.next;
	list_del (list);

	mem_pool->hot_count++;
	mem_pool->cold_count--;

	ptr = list;
      }
  }
  UNLOCK (&mem_pool->lock);

  if (!ptr)
    {
      /* TODO: debug log about excess memory usage */
      ptr = malloc (mem_pool->padded_sizeof_type);

      if (!ptr)
	{
	  return NULL;
	}

      LOCK (&mem_pool->lock);
      {
	mem_pool->hot_count ++;
      }
      UNLOCK (&mem_pool->lock);
    }

  return ptr;
}


static int
__is_member (struct mem_pool *pool, void *ptr)
{
  if (pool == NULL || ptr == NULL)
    {
      gf_log ("mem-pool", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  if (ptr < pool->pool || ptr >= pool->pool_end)
    return 0;

  if ((ptr - pool->pool) % pool->padded_sizeof_type)
    return -1;

  return 1;
}


void
mem_put (struct mem_pool *pool, void *ptr)
{
  struct list_head *list = NULL;
  
  if (pool == NULL || ptr == NULL)
    {
      gf_log ("mem-pool", GF_LOG_ERROR, "invalid argument");
      return;
    }
  
  list = ptr;
  
  LOCK (&pool->lock);
  {
    pool->hot_count--;

    switch (__is_member (pool, ptr))
      {
      case 1:
	pool->cold_count++;
	list_add (list, &pool->list);
	break;
      case -1:
	/* log error */
	abort ();
	break;
      case 0:
	free (ptr);
	break;
      default:
	/* log error */
	break;
      }
  }
  UNLOCK (&pool->lock);

  if (ptr)
    free (ptr);
}
