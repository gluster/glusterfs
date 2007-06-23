/*
  (C) 2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
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

#include "io-cache.h"


/*
 * str_to_ptr - convert a string to pointer
 * @string: string
 *
 */
void *
str_to_ptr (char *string)
{
  void *ptr = (void *)strtoul (string, NULL, 16);
  return ptr;
}


/*
 * ptr_to_str - convert a pointer to string
 * @ptr: pointer
 *
 */
char *
ptr_to_str (void *ptr)
{
  char *str;
  asprintf (&str, "%p", ptr);
  return str;
}

void
ioc_inode_wakeup (call_frame_t *frame,
		  ioc_inode_t *ioc_inode, 
		  struct stat *stbuf)
{
  ioc_waitq_t *waiter = NULL, *waited = NULL;
  char cache_still_valid = 1;
  ioc_local_t *local = frame->local;

  ioc_inode_lock (ioc_inode);
  waiter = ioc_inode->waitq;
  ioc_inode->waitq = NULL;
  ioc_inode_unlock (ioc_inode);

  if (!stbuf || (stbuf->st_mtime != ioc_inode->stbuf.st_mtime) || 
      (stbuf->st_mtim.tv_nsec != ioc_inode->stbuf.st_mtim.tv_nsec))
    cache_still_valid = 0;

  gf_log ("io-cache", GF_LOG_DEBUG,
	  "cache_still_valid = %d for frame = %p", cache_still_valid, frame);

  if (!waiter) {
    gf_log ("io-cache", GF_LOG_DEBUG,
	    "cache validate called without any page waiting to be validated");
  }

  while (waiter) {
    ioc_page_t *waiter_page = waiter->data;
    
    if (cache_still_valid) {
      /* cache valid, wake up page */
      gf_log ("io-cache", GF_LOG_DEBUG,
	      "validate frame(%p) is waking up page = %p", frame, waiter_page);
      ioc_inode_lock (ioc_inode);
      ioc_page_wakeup (waiter_page);
      ioc_inode_unlock (ioc_inode);
    } else {
      /* cache invalid, generate page fault and set page->ready = 0, to avoid double faults  */
      if (waiter_page->ready) {
	gf_log ("io-cache", GF_LOG_DEBUG,
		"validate frame(%p) is faultin page = %p", frame, waiter_page);
	ioc_page_fault (ioc_inode, frame, local->fd, waiter_page->offset);
	waiter_page->ready = 0;
      } else {
	gf_log ("io-cache", GF_LOG_DEBUG,
		"validate frame(%p) is waiting for in-transit page = %p", frame, waiter_page);
      }
    }

    waited = waiter;
    waiter = waiter->next;
    
    waited->data = NULL;
    free (waited);
  }
}

/* 
 * ioc_inode_update - create a new ioc_inode_t structure and add it to 
 *                    the table table. fill in the fields which are derived 
 *                    from inode_t corresponding to the file
 * 
 * @table: io-table structure
 * @inode: inode structure
 *
 * not for external reference
 */
ioc_inode_t *
ioc_inode_update (ioc_table_t *table, 
		  inode_t *inode)
{
  ioc_inode_t *ioc_inode = calloc (1, sizeof (ioc_inode_t));
  
  
  ioc_inode->size = inode->buf.st_size;
  ioc_inode->table = table;
  ioc_inode->inode = inode;

  /* initialize the list for pages */
  INIT_LIST_HEAD (&ioc_inode->pages);
  INIT_LIST_HEAD (&ioc_inode->page_lru);

  ioc_table_lock (table);
  list_add (&ioc_inode->inode_list, &table->inodes);
  list_add_tail (&ioc_inode->inode_lru, &table->inode_lru);
  ioc_table_unlock (table);

  pthread_mutex_init (&ioc_inode->inode_lock, NULL);
  
  return ioc_inode;
}

/*
 * ioc_inode_search - search for a ioc_inode in the table.
 *
 * @table: io-table structure
 * @inode: inode_t structure
 *
 * not for external reference
 */
ioc_inode_t *
ioc_inode_search (ioc_table_t *table,
		  inode_t *inode)
{
  ioc_inode_t *ioc_inode = NULL;
  
  ioc_table_lock (table);
  list_for_each_entry (ioc_inode, &table->inodes, inode_list){
    if (ioc_inode->stbuf.st_ino == inode->ino)
      break;
  }
  ioc_table_unlock (table);
  
  if (ioc_inode->stbuf.st_ino == inode->ino)
    return ioc_inode;
  else 
    return NULL;
}

/* 
 * ioc_inode_destroy - destroy an ioc_inode_t object.
 *
 * @inode: inode to destroy
 *
 * to be called only from ioc_forget. 
 */
void
ioc_inode_destroy (ioc_inode_t *ioc_inode)
{
  ioc_table_t *table = ioc_inode->table;

  ioc_table_lock (table);
  list_del (&ioc_inode->inode_list);
  list_del (&ioc_inode->inode_lru);
  ioc_table_unlock (table);
  
  ioc_inode_flush (ioc_inode);
  pthread_mutex_destroy (&ioc_inode->inode_lock);
  free (ioc_inode);
}

