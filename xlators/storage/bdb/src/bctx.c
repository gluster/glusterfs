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

#include <list.h>
#include <bdb.h>
#include <libgen.h> /* for dirname */

static void
__destroy_bctx (bctx_t *bctx)
{
  if (bctx->directory)
    FREE (bctx->directory);
  
  if (bctx->db_path)
    FREE (bctx->db_path);
  
  FREE (bctx);
}

static void
__unhash_bctx (bctx_t *bctx)
{
  list_del_init (&bctx->b_hash);
}

static int32_t
bctx_table_prune (bctx_table_t *table)
{
  int32_t ret = 0;
  struct list_head purge = {0,};

  if (!table)
    return 0;
  
  INIT_LIST_HEAD (&purge);
  
  LOCK (&table->lock);
  {
    if ((table->lru_limit) &&
	(table->lru_size > table->lru_limit)) {
      while (table->lru_size > table->lru_limit) {
	struct list_head *next;
	bctx_t *entry;
	
	next = table->b_lru.next;
	entry = list_entry (next, bctx_t, list);
	
	list_move_tail (next, &table->purge);
	__unhash_bctx (entry);
	
	table->lru_size--;
	ret++;
      }
    }
    list_move_tail (&purge, &table->purge);
    list_del_init (&table->purge);
  }
  UNLOCK (&table->lock);
  
  {
    bctx_t *del = NULL, *tmp = NULL;
    
    list_for_each_entry_safe (del, tmp, &purge, list) {
      list_del_init (&del->list);
      if (del->dbp)
	del->dbp->close (del->dbp, 0);
      __destroy_bctx (del);
    }
  }

  return ret;
}


/* struct bdb_ctx related */
static inline uint32_t
bdb_key_hash (char *key, uint32_t hash_size)
{
  uint32_t hash = *key;
  
  if (hash) {
    for (key += 1; *key != '\0'; key++) {
      hash = (hash << 5) - hash + *key;
    }
  }
  
  return (hash + *key) % hash_size;
}

static void
__hash_bctx (bctx_t *bctx)
{
  bctx_table_t *table = bctx->table;
  char *key = NULL;

  MAKE_KEY_FROM_PATH (key, bctx->directory);
  bctx->key_hash = bdb_key_hash (key, table->hash_size);

  list_del_init (&bctx->b_hash);
  list_add (&bctx->b_hash, &table->b_hash[bctx->key_hash]);
}

static inline bctx_t *
__bctx_passivate (bctx_t *bctx)
{
  if (bctx->dbp) {
    list_move_tail (&bctx->list, &(bctx->table->b_lru));
    bctx->table->lru_size++;
  } else {
    list_move_tail (&bctx->list, &bctx->table->purge);
    __unhash_bctx (bctx);
  }
  return bctx;
}

static inline bctx_t *
__bctx_activate (bctx_t *bctx)
{
  list_move (&bctx->list, &bctx->table->active);
  bctx->table->lru_size--;
  
  return bctx;
}

static bctx_t *
__bdb_ctx_unref (bctx_t *bctx)
{
  assert (bctx->ref);
  
  --bctx->ref;
  
  if (!bctx->ref)
    bctx = __bctx_passivate (bctx);
  
  return bctx;
}


bctx_t *
bctx_unref (bctx_t *bctx)
{
  if (!bctx && !bctx->table)
    return NULL;
  
  bctx_table_t *table = bctx->table;

  LOCK (&table->lock);    
  {
    bctx = __bdb_ctx_unref (bctx);
  }
  UNLOCK (&table->lock);
  
  bctx_table_prune (table);

  return bctx;
}

/*
 * NOTE: __bdb_ctx_ref() is called only after holding table->lock and bctx->lock, in that order
 */
static inline bctx_t *
__bctx_ref (bctx_t *bctx)
{
  if (!bctx->ref)
    __bctx_activate (bctx);

  bctx->ref++;

  return bctx;
}

bctx_t *
bctx_ref (bctx_t *bctx)
{
  LOCK (&(bctx->table->lock));
  {
    __bctx_ref (bctx);
  }
  UNLOCK (&(bctx->table->lock));

  return bctx;
}


#define BDB_THIS(table) (table->this)

static inline bctx_t *
__create_bctx (bctx_table_t *table,
	       const char *path)
{
  bctx_t *bctx = NULL;

  bctx = calloc (1, sizeof (*bctx));
  ERR_ABORT (bctx);
  if (bctx) {
    char *db_path = NULL;
    bctx->table = table;
    bctx->directory = strdup (path);
    bctx->iseed = 1;

    MAKE_REAL_PATH_TO_STORAGE_DB (db_path, BDB_THIS (table), path);

    bctx->db_path = strdup (db_path);
    INIT_LIST_HEAD (&bctx->c_list);
    INIT_LIST_HEAD (&bctx->list);
    INIT_LIST_HEAD (&bctx->b_hash);

    LOCK_INIT (&bctx->lock);

    __hash_bctx (bctx);

    list_add (&bctx->list, &table->b_lru);
    table->lru_size++;
  }
  
  return bctx;
}

/* 
 * @path is always a directory
 */
bctx_t *
bctx_lookup (bctx_table_t *table, 
	     const char *path)
{
  char *key = NULL;
  uint32_t key_hash = 0;
  bctx_t *trav = NULL, *bctx = NULL, *tmp = NULL;
  int32_t need_break = 0;

  MAKE_KEY_FROM_PATH (key, path);
  key_hash = bdb_key_hash (key, table->hash_size);

  LOCK (&table->lock);
  {
    if (!list_empty (&table->b_hash[key_hash])) {
      list_for_each_entry_safe (trav, tmp, &table->b_hash[key_hash], b_hash) {
	LOCK(&trav->lock);
	if (!strcmp(trav->directory, path)) {
	  bctx = __bctx_ref (trav);
	  need_break = 1;
	} 
	UNLOCK(&trav->lock);
	if (need_break)
	  break;
      }
    }
    
    if (!bctx) {
      bctx = __create_bctx (table, path);
      bctx = __bctx_ref (bctx);
    } 
  }
  UNLOCK (&table->lock);
  return bctx;
}


bctx_t *
bctx_parent (bctx_table_t *table,
	     const char *path)
{
  char *pathname = strdup (path), *directory = NULL;
  bctx_t *bctx = NULL;
  directory = dirname (pathname);

  if ((bctx = bctx_lookup (table, directory)) == NULL) {
    gf_log ("bdb-ll",
	    GF_LOG_ERROR,
	    "failed to get bctx from table: %p for directory: %s", table, directory);
    bctx = NULL;
  } else {
    /* do nothing */
  }
  
  free (pathname);
  return bctx;
}
