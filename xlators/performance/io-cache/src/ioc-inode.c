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
 * TODO: waiting on in-transit stat 
 */

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
ioc_inode_wakeup (ioc_inode_t *ioc_inode, struct stat *stbuf)
{
  ioc_waitq_t *waiter = ioc_inode->waitq, *waited = NULL;

  ioc_inode_lock (ioc_inode);
  ioc_inode->waitq = NULL;
  ioc_inode_unlock (ioc_inode);

  while (waiter) {
    call_stub_t *waiter_stub = waiter->data;
    ioc_local_t *local = waiter_stub->frame->local;
    if (stbuf->st_mtime != ioc_inode->stbuf.st_mtime) {
      /* file has been modified since we cached it */
      ioc_inode->stbuf = *stbuf;
      local->op_ret = -1;
    } else {
      ioc_inode->stbuf = *stbuf;
      local->op_ret = 0;
    }
 
    call_resume (waiter_stub);
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

  list_add (&ioc_inode->inode_list, &table->inodes);
  list_add_tail (&ioc_inode->inode_lru, &table->inode_lru);

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
  
  list_for_each_entry (ioc_inode, &table->inodes, inode_list){
    if (ioc_inode->inode->ino == inode->ino)
      return ioc_inode;
  }

  return NULL;
}



/*
 * ioc_inode_ref - io cache inode ref
 * @inode:
 *
 */
ioc_inode_t *
ioc_inode_ref (ioc_inode_t *inode)
{
  inode->refcount++;
  return inode;
}



/*
 * ioc_inode_ref_locked - io cache inode ref
 * @inode:
 *
 */
ioc_inode_t *
ioc_inode_ref_locked (ioc_inode_t *inode)
{
  ioc_inode_lock (inode);
  inode->refcount++;
  ioc_inode_unlock (inode);
  return inode;
}

/* 
 * ioc_inode_destroy -
 * @inode:
 *
 */
void
ioc_inode_destroy (ioc_inode_t *ioc_inode)
{
  ioc_table_t *table = ioc_inode->table;

  if (ioc_inode->refcount)
    return;

  ioc_table_lock (table);
  list_del (&ioc_inode->inode_list);
  list_del (&ioc_inode->inode_lru);
  ioc_table_unlock (table);
  
  ioc_inode_flush (ioc_inode);
  pthread_mutex_destroy (&ioc_inode->inode_lock);
  free (ioc_inode);
}

/*
 * ioc_inode_unref_locked -
 * @inode:
 *
 */
void
ioc_inode_unref_locked (ioc_inode_t *inode)
{
  int32_t refcount;

  refcount = --inode->refcount;

  if (refcount)
    return;

}

/*
 * ioc_inode_unref - unref a inode
 * @inode:
 *
 */
void
ioc_inode_unref (ioc_inode_t *inode)
{
  int32_t refcount;

  ioc_inode_lock (inode);
  refcount = --inode->refcount;
  ioc_inode_unlock (inode);

  if (refcount)
    return;

}
