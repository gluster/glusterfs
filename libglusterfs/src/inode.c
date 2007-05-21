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

#include "inode.h"
#include <pthread.h>
#include <sys/types.h>
#include <stdint.h>
#include "list.h"
#include <time.h>
#include <assert.h>

/* TODO: add debug logs */


/**
 * hash_name -hash function for filenames
 * @par: parent inode number
 * @name: basename of inode
 * @mod: number of buckets in the hashtable
 *
 * Not for external use.
 */

static uint32_t
hash_name (ino_t par,
	   const char *name,
	   int32_t mod)
{
  uint32_t hash = *name;

  if (hash) {
    for (name += 1; *name != '\0'; name++) {
      hash = (hash << 5) - hash + *name;
    }
  }

  return (hash + par) % mod;
}


/**
 * hash_inode -hash function for inode numbers
 * @ino: inode number to hash
 * @mod: number of buckets in the hashtable
 *
 * Not for external use.
 */

static uint32_t
hash_inode (ino_t ino,
	    int32_t mod)
{
  uint32_t hash = ino;

  return (hash) % mod;
}


/**
 * __unhash_name - unassociat an inode from its name hash. 
 * @inode: the inode to unhash
 *
 * Assumes locks are held. Not for external use.
 */

static void
__unhash_name (inode_t *inode)
{
  /* TODO: unref parent */

  list_del_init (&inode->name_hash);
  if (inode->name)
    free (inode->name);
  inode->name = NULL;
  inode->par = 0;
  inode->parent = NULL;
}


/**
 * __unhash_inode - unassociate an inode from its inode number  hash. 
 * @inode: the inode to unhash
 *
 * Assumes locks are held. Not for external use.
 */

static void
__unhash_inode (inode_t *inode)
{
  list_del_init (&inode->inode_hash);
}


/**
 * __hash_inode - associate an inode to its inode number hash. 
 * @inode: the inode to hash
 *
 * Assumes locks are held. Not for external use.
 */

static void
__hash_inode (inode_t *inode)
{
  inode_table_t *table = inode->table;
  uint32_t inode_hash = hash_inode (inode->ino, table->hashsize);

  list_del_init (&inode->inode_hash);
  list_add (&inode->inode_hash, &table->inode_hash[inode_hash]);
}


/**
 * __hash_name - associate an inode to its name hash. 
 * @inode: the inode to hash
 *
 * Assumes locks are held. Not for external use.
 */

static void
__hash_name (inode_t *inode)
{
  inode_table_t *table = inode->table;
  uint32_t name_hash = hash_name (inode->par, inode->name, table->hashsize);

  list_del_init (&inode->name_hash);
  list_add (&inode->name_hash, &table->name_hash[name_hash]);
}


/**
 * __search_inode - search for an inode.
 * @table: the table to search the inode in
 * @ino: the inode number of the inode
 *
 * Searches for an inode based on its inode number. 
 *
 * Assumes locks are held. Not for external use.
 * Use inode_search instead.
 */

static inode_t *
__search_inode (inode_table_t *table,
		ino_t ino)
{
  uint32_t hash = 0;
  inode_t *inode;

  hash = hash_inode (ino, table->hashsize);

  list_for_each_entry (inode, &table->inode_hash[hash], inode_hash) {
    if (inode->ino == ino)
      return inode;
  }

  return NULL;
}


/**
 * __search_name - search for an inode.
 * @table: the table to search the inode in
 * @par: parent inode number of the inode in search
 * @name: the basename of the inode in search
 *
 * Searches for an inode based on its basename.
 *
 * Assumes locks are held. Not for external use.
 * Use inode_search instead.
 *
 * Used by FUSE inode management ONLY.
 */

static inode_t *
__search_name (inode_table_t *table,
	       ino_t par,
	       const char *name)
{
  uint32_t hash = 0;
  inode_t *inode;

  hash = hash_name (par, name, table->hashsize);

  list_for_each_entry (inode, &table->name_hash[hash], name_hash) {
    if (inode->par == par && !strcmp (inode->name, name))
      return inode;
  }

  return NULL;
}


/**
 * __destroy_inode - destroy an inode
 * @inode: the inode to destroy
 *
 * Destroys an inode and all its members after unassociating it from
 * its table in all ways. Expected to have run the FORGET fop on this
 * inode so that translators would have cleaned up their components
 * in inode->ctx. 
 *
 * Assumes locks are held. Not for external use.
 */

static void
__destroy_inode (inode_t *inode)
{
  list_del (&inode->list);
  list_del (&inode->name_hash);
  list_del (&inode->inode_hash);

  dict_destroy (inode->ctx);

  if (inode->name)
    free (inode->name);

  gf_log (inode->table->name,
	  GF_LOG_DEBUG,
	  "destroy inode(%ld)", inode->ino);

  free (inode);
}


/**
 * inode_destroy - destory an inode
 * @inode: inode to destroy
 *
 * NOT to be used by FUSE inode management.
 *
 * Expects @inode to be a pruned entry (using inode_table_prune). Thus
 * locks not held.
 */

void
inode_destroy (inode_t *inode)
{
  inode_table_t *table = inode->table;

  __destroy_inode (inode);
}


/**
 * __active_inode: activate an inode
 * @inode: inode to activate
 *
 * To be called when an inode is referred which was previously not
 * referred and was in the table's lru list.
 *
 * Assumes locks are held. Not for external use.
 */

static inode_t *
__active_inode (inode_t *inode)
{
  list_move (&inode->list, &inode->table->active);

  return inode;
}


/**
 * __passive_inode - passivate an inode
 * @inode: the inode to passivate
 *
 * To be called when all references are removed on an inode. 
 *
 * If lru_limit is not available (FUSE inode management usage) then
 * destroys the inode immediately.
 *
 * If lru_limit is availble (non FUSE inode management usage) then
 * moves the inode from the table's active list to the lru list. 
 *
 * Assumes locks are held. Not for external use.
 */

static inode_t *
__passive_inode (inode_t *inode)
{
  if (inode->table->lru_limit) {
    list_move_tail (&inode->list, &inode->table->lru);
    inode->table->lru_size ++;
  } else {
    __destroy_inode (inode);
    inode = NULL;
  }

  return inode;
}


/**
 * __inode_unref - unrefer an inode
 * @inode: the inode to unref
 *
 * When all references are gone, passivate the inode.
 *
 * Assumes locks are held. Not for external use.
 * Use inode_unref instead.
 */

static inode_t *
__inode_unref (inode_t *inode)
{
  if (inode->ino == 1)
    return inode;

  assert (inode->ref);

  --inode->ref;

  if (!inode->ref)
    inode = __passive_inode (inode);

  return inode;
}


/**
 * __inode_ref - refer an inode
 * @inode: the inode to refer
 *
 * If this is the first reference, activate the inode.
 *
 * Assumes locks are held. Not for external use.
 * Use inode_ref instead.
 */

static inode_t *
__inode_ref (inode_t *inode)
{
  if (!inode->ref)
    __active_inode (inode);

  inode->ref++;

  return inode;
}


/**
 * inode_unref - unrefer an inode
 * @inode: the inode to unrefer
 *
 * See __inode_unref for details.
 */

inode_t *
inode_unref (inode_t *inode)
{
  inode_table_t *table = inode->table;

  pthread_mutex_lock (&table->lock);

  inode = __inode_unref (inode);

  pthread_mutex_unlock (&table->lock);

  return inode;
}


/**
 * inode_ref - refer an inode
 * @inode: the inode to refer
 *
 * See __inode_ref for details.
 */

inode_t *
inode_ref (inode_t *inode)
{
  inode_table_t *table = inode->table;

  pthread_mutex_lock (&table->lock);

  inode = __inode_ref (inode);

  pthread_mutex_unlock (&table->lock);

  return inode;
}


/**
 * __create_inode - create a new inode
 * @table: the table to create the inode in
 * @parent: parent inode of the new inode
 * @name: basename of the new inode
 * @ino: inode number of the new inode
 *
 * If @ino is 0 an arbitrary inode number is assigned.
 *
 * if @parent is NULL then the 'name' association is not hashed.
 *
 * Should be called only if no inode exists with both
 * a) inode number as @ino
 * b) inode has parent inode number as @parent->ino and name as @name
 *
 * Assumes locks are held. Not for external use.
 */

static inode_t *
__create_inode (inode_table_t *table,
		inode_t *parent,
		const char *name,
		ino_t ino)
{
  inode_t *new = NULL;

  new = (void *)calloc (1, sizeof (*new));
  if (!new)
    return NULL;

  /* TODO: ref on parent inode */

  if (parent)
    new->par = parent->ino;
  new->parent = parent;

  if (name)
    new->name = strdup (name);

  if (ino)
    new->ino = ino;
  else
    new->ino = (unsigned long) new;

  new->table = table;

  INIT_LIST_HEAD (&new->fds);
  INIT_LIST_HEAD (&new->list);
  INIT_LIST_HEAD (&new->name_hash);
  INIT_LIST_HEAD (&new->inode_hash);

  if (parent)
    __hash_name (new);

  __hash_inode (new);

  list_add (&new->list, &table->lru);

  new->ctx = get_new_dict ();

  gf_log (table->name,
	  GF_LOG_DEBUG,
	  "create inode(%ld)", new->ino);
  return new;
}


/**
 * __inode_lookup - lookup an inode once
 * @inode: inode to lookup
 *
 * If this is the first lookup refer it as well. Thus, any
 * looked up inode has ONE reference from the kernel.
 *
 * Used by FUSE inode management ONLY.
 *
 * Assumes locks are held. Not for external use.
 * Use inode_lookup instead.
 */

static inode_t *
__inode_lookup (inode_t *inode)
{
  if (!inode->nlookup)
    __inode_ref (inode);

  inode->nlookup++;

  return inode;
}


/**
 * __inode_forget - forget an inode @nlookup times
 * @inode: the inode to forget
 * @nlookup: the number of times to forget
 *
 * When completely forgotten, unref the inode once. Indicates
 * the kernel is no more referring to this @inode.
 *
 * Used by FUSE inode management ONLY.
 *
 * Assumes locks are held. Not for external use.
 * Use inode_forget instead.
 */

static inode_t *
__inode_forget (inode_t *inode, uint64_t nlookup)
{
  assert (inode->nlookup >= nlookup);
  inode->nlookup -= nlookup;

  if (!inode->nlookup) {
    __unhash_inode (inode);
    __unhash_name (inode);
    inode = __inode_unref (inode);
  }

  return inode;
}


/**
 * inode_search - search for an inode
 * @table: the table to search the inode in
 * @ino: inode number
 * @name: basename
 *
 * If @name is NULL, search for an inode which has inode number @ino.
 *
 * If @name is not NULL, search for an inode has its parent inode number @ino
 * and basename as @name (This convention used only by FUSE inode management.)
 *
 * NOTE: inode is returned with a reference in any case.
 */

inode_t *
inode_search (inode_table_t *table,
	      ino_t ino,
	      const char *name)
{
  inode_t *inode = NULL;

  pthread_mutex_lock (&table->lock);

  if (!name) {
    inode = __search_inode (table, ino);
  } else {
    inode = __search_name (table, ino, name);
  }

  if (inode)
    __inode_ref (inode);

  pthread_mutex_unlock (&table->lock);

  return inode;
}


/**
 * __inode_update - update inode relations
 * @table: inode table to update the relation in
 * @parent: parent inode
 * @name: basename of the inode
 * @ino: inode number of the inode
 *
 * if @parent and @name already has with a differnt inode number,
 *   then orphan the old inode
 *
 * if @ino already exists with different parent/name,
 *   then update its relation to the new @parent and @name
 * 
 * if @ino does not exist,
 *   create new inode for the given relation
 *
 * NOTE: inode is returned with a reference in any case.
 *
 * Assumes locks are held. Not for external use.
 * Use inode_update instead.
 */

static inode_t *
__inode_update (inode_table_t *table,
		inode_t *parent,
		const char *name,
		ino_t ino)
{
  inode_t *old_inode = NULL;
  inode_t *old_name = NULL;
  inode_t *inode = NULL;

  old_inode = __search_inode (table, ino);

  if (parent)
    old_name = __search_name (table, parent->ino, name);

  if (old_name) {
    /*
      @old_inode was deleted or renamed by another client and currently
      @name under @par is the inode_t with @ino as its inode number

      unhash the current @inode from namespace and when
      - freshly looked up with its new name (if renamed) or
      - when an FOP on this inode returns ENOENT or
      - enough forget()s and unref()s happen it will be deleted

      (do not __unhash_inode() to allow fops to still happen)
    */
    if (old_name->ino != ino)
      __unhash_name (old_name);
    /* TODO: unref @inode */
  }

  if (old_inode && parent) {
    if (old_inode->par != parent->ino || strcmp (old_inode->name, name)) {
      /*
	@ino belongs to:
	- @old_inode itself which was renamed by another client?
	- hard link of @old_inode?
	- race condition where @old_inode->par and @old_inode->name are
	  stale entries of a deleted file who's inode number is reused?
	  
	anyways, update table to this freshest relation
	if it was
	- hardlink, no issues, relation change makes no difference
	- if it was renamed, until the attr_timeout expires
	  operations will be performed on the new related path
	  (think of it as a race between SETATTR+RENAME, where
	  SETATTR happened just before the rename)
	- a race condition and this is a new file reusing the same
	inode number, tough luck :(
      */

      __unhash_name (old_inode);

      old_inode->par = parent->ino;
      old_inode->parent = parent; // TODO: ref?
      old_inode->name = strdup (name);

      __hash_name (old_inode);
    }
  }

  if (old_inode)
    inode = old_inode;
  else
    inode = __create_inode (table, parent, name, ino);

  __inode_ref (inode);

  return inode;
}


/**
 * inode_update - update inode relations
 * @table: inode table to update the relation in
 * @parent: parent inode
 * @name: basename of the inode
 * @ino: inode number of the inode
 *
 * See __inode_update for details.
 */

inode_t *
inode_update (inode_table_t *table,
	      inode_t *parent,
	      const char *name,
	      ino_t ino)
{
  inode_t *inode = NULL;

  pthread_mutex_lock (&table->lock);

  inode = __inode_update (table, parent, name, ino);

  pthread_mutex_unlock (&table->lock);

  return inode;
}


/**
 * inode_lookup - lookup an inode once
 * @inode: inode to lookup
 *
 * See __inode_lookup for details.
 */

inode_t *
inode_lookup (inode_t *inode)
{
  inode_table_t *table = inode->table;

  pthread_mutex_lock (&table->lock);

  inode = __inode_lookup (inode);

  pthread_mutex_unlock (&table->lock);

  return inode;
}


/**
 * inode_forget - forget an inode @nlookup times
 * @inode: inode to forget
 * @nlookup: number of times to forget
 *
 * See __inode_forget for details.
 */

inode_t *
inode_forget (inode_t *inode, uint64_t nlookup)
{
  inode_table_t *table = inode->table;

  pthread_mutex_lock (&table->lock);

  inode = __inode_forget (inode, nlookup);

  pthread_mutex_unlock (&table->lock);

  return inode;
}


/**
 * __inode_unlink - remove parent/name relation for an inode
 * @table: inode table to perform the operation in
 * @parent: parent of the inode
 * @name: basename of the inode
 *
 * To be called when an entry is removed from the namespace (rmdir/unlink)
 *
 * Assumes locks are held. Not for external use.
 * Use inode_unlink instead.
 *
 * Used by FUSE inode management ONLY.
 */

static void
__inode_unlink (inode_table_t *table,
		inode_t *parent,
		const char *name)
{
  inode_t *inode = NULL;

  inode = __search_name (table, parent->ino, name);

  if (inode)
    __unhash_name (inode);
}


/**
 * inode_unlink - remove parent/name relation for an inode
 * @table: inode table to perform operation in
 * @parent: parent of the inode
 * @name: basename of the inode
 *
 * See __inode_unlink for details.
 */

void
inode_unlink (inode_table_t *table,
              inode_t *parent,
              const char *name)
{
  pthread_mutex_lock (&table->lock);

  __inode_unlink (table, parent, name);

  pthread_mutex_unlock (&table->lock);
}


/**
 * inode_rename - re-associate name hashes because of rename
 * @table: inode table to perform operation in
 * @olddir: parent of inode to move
 * @oldname: basename of inode to move
 * @newdir: parent of new location of inode
 * @newname: new basename of inode
 *
 * Resistant against @olddir/@oldname being recreated by external agent.
 *
 * Used by FUSE inode management ONLY.
 */

inode_t *
inode_rename (inode_table_t *table,
              inode_t *olddir,
	      const char *oldname,
              inode_t *newdir,
              const char *newname,
	      ino_t newino)
{
  inode_t *inode;

  pthread_mutex_lock (&table->lock);

  __inode_unlink (table, olddir, oldname);
  __inode_unlink (table, newdir, newname);
  inode = __inode_update (table, newdir, newname, newino);

  pthread_mutex_unlock (&table->lock);

  return inode;
}


/**
 * inode_path - calculate the path from an inode
 * @inode: inode who's path is to be calculated
 * @name: basename to suffix for the calculated path
 * @buf: where to build the path string
 * @size: allocated size of @buf
 *
 * Function returns the complete size of the path including @name appended.
 *
 * If @size was smaller than the calculated size, @buf will remain untouched.
 * Complete size is returned in any case.
 *
 * Typical usage case is to either pass the inode as @inode and NULL as @name
 * or the parent inode as @inode and basename as @name.
 *
 * Used by FUSE inode management ONLY.
 */

size_t
inode_path (inode_t *inode, const char *name, char *buf, size_t size)
{
  inode_table_t *table = inode->table;
  inode_t *trav = inode;
  size_t i = 0;
  size_t ret = 0;

  pthread_mutex_lock (&table->lock);

  i++; /* '\0' */

  while (trav->parent) {
    i ++; /* "/" */
    i += strlen (trav->name);
    trav = trav->parent;
  }

  if (name) {
    i++;
    i += strlen (name);
  }

  ret = i;

  if (buf && size > i) {
    int32_t len;

    buf[i] = 0;
    i--;
    buf[i] = 0;

    if (name) {
      len = strlen (name);
      strncpy (buf + (i - len), name, len);
      buf[i-len-1] = '/';
      i -= (len + 1);
    }

    trav = inode;
    while (trav->parent) {
      len = strlen (trav->name);
      strncpy (buf + (i - len), trav->name, len);
      buf[i-len-1] = '/';
      i -= (len + 1);

      trav = trav->parent;
    }
  }

  pthread_mutex_unlock (&table->lock);

  return ret;
}


/**
 * inode_table_prune - prune an inode table
 * @table: inode table to prune
 * @head: empty list head to fill pruned entries
 *
 * NOT used by FUSE inode management.
 *
 * If @table has lru_limit (i.e. non FUSE inode management) and there
 * are more passive entries than lru_limit then the oldest
 * (@table->lru_size - @table->lru_limit) entries are unlinked from the
 * lru list and moved into @head.
 *
 * Pruned entries have all relations with @table removed.
 *
 * Returns the number of entries moved to @head.
 *
 * Moved entries into @head are expected to be passed to FORGET fop
 * and then destroyed with inode_destroy.
 */

int32_t
inode_table_prune (inode_table_t *table,
		   struct list_head *head)
{
  int32_t ret = 0;

  pthread_mutex_lock (&table->lock);

  while (table->lru_size > table->lru_limit) {
    struct list_head *next;
    inode_t *entry;

    next = table->lru.next;
    entry = list_entry (next, inode_t, list);

    list_del_init (next);
    __unhash_inode (entry);
    __unhash_name (entry);

    entry->table = NULL;

    list_add (next, head);

    table->lru_size--;
    ret++;
  }

  pthread_mutex_unlock (&table->lock);

  return ret;
}


/**
 * inode_table_new - create new inode table.
 * @lru_limit: limit of lru entries before pruning
 * @name: table name (used for logging to indicate which inode table)
 *
 * NOTE: FUSE inode management should pass @lru_limit as 0. Other inode
 *       management should pass a suitable number (~100 to ~1000)
 *
 * root inode with inode number 1 is hardcoded and activated by default in
 * the created table
 */

inode_table_t *
inode_table_new (size_t lru_limit, const char *name)
{
  inode_table_t *new;
  int32_t i;

  new = (void *)calloc (1, sizeof (*new));
  if (!new)
    return NULL;

  new->lru_limit = lru_limit;

  if (!lru_limit)
    new->hashsize = 14057;
  else
    new->hashsize = (lru_limit / 2);

  new->inode_hash = (void *)calloc (new->hashsize,
				    sizeof (struct list_head));
  if (!new->inode_hash) {
    free (new);
    return NULL;
  }

  for (i=0; i<new->hashsize; i++) {
    INIT_LIST_HEAD (&new->inode_hash[i]);
  }

  new->name_hash = (void *)calloc (new->hashsize,
				   sizeof (struct list_head));
  if (!new->name_hash) {
    free (new->inode_hash);
    free (new);
    return NULL;
  }

  for (i=0; i<new->hashsize; i++) {
    INIT_LIST_HEAD (&new->name_hash[i]);
  }

  INIT_LIST_HEAD (&new->active);
  INIT_LIST_HEAD (&new->lru);

  asprintf (&new->name, "%s/inode", name);

  new->root = __create_inode (new, NULL, "/", 1);

  __inode_ref (new->root); /* always in active list */

  pthread_mutex_init (&new->lock, NULL);

  return new;
}

