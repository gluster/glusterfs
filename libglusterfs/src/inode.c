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
#include <assert.h>

/* TODO: add debug logs */

/* TODO: if ino has changed for an existing inode_t rehash it */

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


static uint32_t
hash_inode (ino_t ino,
	    int32_t mod)
{
  uint32_t hash = ino;

  return (hash) % mod;
}



static void
__unhash_name (inode_t *inode)
{
  /* TODO: unref parent */

  list_del_init (&inode->name_hash);
  free (inode->name);
  inode->name = NULL;
  inode->par = 0;
  inode->parent = NULL;
}


static void
__unhash_inode (inode_t *inode)
{
  list_del_init (&inode->inode_hash);
}


static void
__hash_inode (inode_t *inode)
{
  inode_table_t *table = inode->table;
  uint32_t inode_hash = hash_inode (inode->ino, table->hashsize);

  list_del_init (&inode->inode_hash);
  list_add (&inode->inode_hash, &table->inode_hash[inode_hash]);
}


static void
__hash_name (inode_t *inode)
{
  inode_table_t *table = inode->table;
  uint32_t name_hash = hash_name (inode->par, inode->name, table->hashsize);

  list_del_init (&inode->name_hash);
  list_add (&inode->name_hash, &table->name_hash[name_hash]);
}


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


static void
__destroy_inode (inode_t *inode)
{
  list_del (&inode->all);
  list_del (&inode->name_hash);
  list_del (&inode->inode_hash);

  dict_destroy (inode->ctx);

  if (inode->name)
    free (inode->name);

  gf_log (inode->table->name,
	  GF_LOG_DEBUG,
	  "destroy: %p", inode);
  inode->table->d++;
  free (inode);
}


static inode_t *
__inode_unref (inode_t *inode)
{
  int32_t ref;

  if (inode->ino == 1)
    return inode;

  assert (inode->ref);

  ref = --inode->ref;
  if (!ref) {
    __destroy_inode (inode);
    return NULL;
  }

  return inode;
}


static inode_t *
__inode_ref (inode_t *inode)
{
  inode->ref++;

  return inode;
}


inode_t *
inode_unref (inode_t *inode)
{
  inode_table_t *table = inode->table;

  pthread_mutex_lock (&table->lock);

  inode = __inode_unref (inode);

  pthread_mutex_unlock (&table->lock);

  return inode;
}


inode_t *
inode_ref (inode_t *inode)
{
  inode_table_t *table = inode->table;

  pthread_mutex_lock (&table->lock);

  inode = __inode_ref (inode);

  pthread_mutex_unlock (&table->lock);

  return inode;
}


static inode_t *
__inode_lookup (inode_t *inode)
{

  if (!inode->nlookup) {
    __inode_ref (inode);
  }
  inode->nlookup++;

  return inode;
}


static inode_t *
__inode_forget (inode_t *inode, uint64_t nlookup)
{
  assert (inode->nlookup >= nlookup);
  inode->nlookup -= nlookup;
  if (!inode->nlookup) {
    __unhash_inode (inode);
    __inode_unref (inode);
  }
  return inode;
}


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
  table->c++;
  if (parent)
    new->par = parent->ino;
  else
    new->par = 1;
  new->parent = parent;
  new->name = strdup (name);
  if (ino)
    new->ino = ino;
  else
    new->ino = (unsigned long) new;

  new->table = table;

  INIT_LIST_HEAD (&new->fds);
  INIT_LIST_HEAD (&new->all);
  INIT_LIST_HEAD (&new->name_hash);
  INIT_LIST_HEAD (&new->inode_hash);

  __hash_name (new);
  __hash_inode (new);

  list_add (&new->all, &table->all);

  new->ctx = get_new_dict ();

  gf_log (table->name,
	  GF_LOG_DEBUG,
	  "create: %p", new);
  return new;
}


/*
 * if @name is null, search for inode which has inode number as @ino
 * if @name is not null, search of inode which has name @name and parent @ino
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



static inode_t *
__inode_update (inode_table_t *table,
		inode_t *parent,
		const char *name,
		ino_t ino)
{
  inode_t *old_inode = NULL;
  inode_t *old_name = NULL;
  inode_t *inode = NULL;

  old_name = __search_name (table, parent->ino, name);

  if (old_name) {
    /*
      @old_inode was deleted or renamed by another client and currently
      @name under @par is the inode_t with @ino as its inode number

      unhash the current @inode from namespace and when
      - freshly looked up with its new name (if renamed) or
      - when an FOP on this inode returns ENOENT or
      - enough forget()s and unref()s happen

      it will be deleted

      (do not __unhash_inode() to allow fops to still happen)
    */
    if (old_name->ino != ino)
      __unhash_name (old_name);
      /* TODO: unref @inode */
  }

  if (ino)
    old_inode = __search_inode (table, ino);

  if (old_inode) {
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
    inode = old_inode;
  } else {
    inode = __create_inode (table, parent, name, ino);
  }

  __inode_ref (inode);

  return inode;
}



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



inode_t *
inode_lookup (inode_t *inode)
{
  inode_table_t *table = inode->table;

  pthread_mutex_lock (&table->lock);

  inode = __inode_lookup (inode);

  pthread_mutex_unlock (&table->lock);

  return inode;
}


inode_t *
inode_forget (inode_t *inode, uint64_t nlookup)
{
  inode_table_t *table = inode->table;

  pthread_mutex_lock (&table->lock);

  inode = __inode_forget (inode, nlookup);

  pthread_mutex_unlock (&table->lock);

  return inode;
}


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

void
inode_unlink (inode_table_t *table,
              inode_t *parent,
              const char *name)
{
  pthread_mutex_lock (&table->lock);

  __inode_unlink (table, parent, name);

  pthread_mutex_unlock (&table->lock);
}


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


inode_table_t *
inode_table_new (size_t hashsize, const char *name)
{
  inode_table_t *new;
  int32_t i;

  /* 
     TODO: hardcode inode=1 to be "/" of view and handle explicitly 
     in all API
  */

  new = (void *)calloc (1, sizeof (*new));
  if (!new)
    return NULL;

  if (!hashsize)
    hashsize = 14057;

  new->hashsize = hashsize;
  new->inode_hash = (void *)calloc (new->hashsize,
				    sizeof (struct list_head));
  if (!new->inode_hash) {
    free (new);
    return NULL;
  }

  for (i=0; i<hashsize; i++) {
    INIT_LIST_HEAD (&new->inode_hash[i]);
  }

  new->name_hash = (void *)calloc (new->hashsize,
				   sizeof (struct list_head));
  if (!new->name_hash) {
    free (new->inode_hash);
    free (new);
    return NULL;
  }

  for (i=0; i<hashsize; i++) {
    INIT_LIST_HEAD (&new->name_hash[i]);
  }

  INIT_LIST_HEAD (&new->all);

  asprintf (&new->name, "%s/inode", name);

  new->root = __create_inode (new, NULL, "/", 1);

  pthread_mutex_init (&new->lock, NULL);

  return new;
}

