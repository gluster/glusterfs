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
	struct list_head *next = NULL;
	bctx_t *entry = NULL;
	bctx_t *del = NULL, *tmp = NULL;
  
	if (!table)
		return 0;
  
	INIT_LIST_HEAD (&purge);
  
	LOCK (&table->lock);
	{
		if ((table->lru_limit) &&
		    (table->lru_size > table->lru_limit)) {
			while (table->lru_size > table->lru_limit) {
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
		list_for_each_entry_safe (del, tmp, &purge, list) {
			list_del_init (&del->list);
			if (del->dbp) {
				ret = del->dbp->close (del->dbp, 0);
				if (ret != 0) {
					gf_log (table->this->name, GF_LOG_ERROR,
						"failed to close db on path (%s): %s", 
						del->directory, db_strerror (ret));
				} else {
					gf_log (table->this->name, GF_LOG_WARNING,
						"close db for path %s; table->lru_count = %d", 
						del->directory, table->lru_size);
				}
			}
			__destroy_bctx (del);
		}
	}

	return ret;
}


/* struct bdb_ctx related */
static inline uint32_t
bdb_key_hash (char *key, uint32_t hash_size)
{
	uint32_t hash = 0;
  
	hash = *key;
  
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
	bctx_table_t *table = NULL;
	char *key = NULL;

	table = bctx->table;

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
	bctx_table_t *table = NULL;

	if (!bctx && !bctx->table)
		return NULL;
  
	table = bctx->table;

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
	char *db_path = NULL;

	bctx = CALLOC (1, sizeof (*bctx));
	GF_VALIDATE_OR_GOTO ("bctx", bctx, out);

	bctx->table = table;
	bctx->directory = strdup (path);
	GF_VALIDATE_OR_GOTO ("bctx", bctx->directory, out);

	MAKE_REAL_PATH_TO_STORAGE_DB (db_path, BDB_THIS (table), path);

	bctx->db_path = strdup (db_path);
	GF_VALIDATE_OR_GOTO ("bctx", bctx->directory, out);

	INIT_LIST_HEAD (&bctx->c_list);
	INIT_LIST_HEAD (&bctx->list);
	INIT_LIST_HEAD (&bctx->b_hash);

	LOCK_INIT (&bctx->lock);

	__hash_bctx (bctx);

	list_add (&bctx->list, &table->b_lru);
	table->lru_size++;

out:
	return bctx;
}

/* bctx_lookup - lookup bctx_t for the directory @directory. (see description of bctx_t in bdb.h)
 *
 * @table:     bctx_table_t for this instance of bdb.
 * @directory: directory for which bctx_t is being looked up. 
 */
bctx_t *
bctx_lookup (bctx_table_t *table, 
	     const char *directory)
{
	char    *key = NULL;
	uint32_t key_hash = 0;
	bctx_t  *trav = NULL, *bctx = NULL, *tmp = NULL;
	int32_t  need_break = 0;
	
	GF_VALIDATE_OR_GOTO ("bctx", table, out);
	GF_VALIDATE_OR_GOTO ("bctx", directory, out);

	MAKE_KEY_FROM_PATH (key, directory);
	key_hash = bdb_key_hash (key, table->hash_size);

	LOCK (&table->lock);
	{
		if (!list_empty (&table->b_hash[key_hash])) {
			list_for_each_entry_safe (trav, tmp, &table->b_hash[key_hash], b_hash) {
				LOCK(&trav->lock);
				if (!strcmp(trav->directory, directory)) {
					bctx = __bctx_ref (trav);
					need_break = 1;
				} 
				UNLOCK(&trav->lock);
				if (need_break)
					break;
			}
		}
    
		if (!bctx) {
			bctx = __create_bctx (table, directory);
			bctx = __bctx_ref (bctx);
		} 
	}
	UNLOCK (&table->lock);
out:
	return bctx;
}


bctx_t *
bctx_parent (bctx_table_t *table,
	     const char *path)
{
	char   *pathname = NULL, *directory = NULL;
	bctx_t *bctx = NULL;

	GF_VALIDATE_OR_GOTO ("bctx", table, out);
	GF_VALIDATE_OR_GOTO ("bctx", path, out);
  
	pathname = strdup (path);
	GF_VALIDATE_OR_GOTO ("bctx", pathname, out);
	directory = dirname (pathname);

	bctx = bctx_lookup (table, directory);
	GF_VALIDATE_OR_GOTO ("bctx", bctx, out);
  
out:
	if (pathname)
		free (pathname);
	return bctx;
}

inline int32_t
bdb_db_rename (bctx_table_t *table, 
	       const char *oldpath, 
	       const char *newpath)
{
	DB_ENV *dbenv = NULL;
	int32_t ret = -1;
	
	GF_VALIDATE_OR_GOTO ("bctx", table, out);
	GF_VALIDATE_OR_GOTO ("bctx", oldpath, out);
	GF_VALIDATE_OR_GOTO ("bctx", newpath, out);

	dbenv = table->dbenv;
	GF_VALIDATE_OR_GOTO ("bctx", dbenv, out);

	LOCK (&table->lock);
	{
		ret = dbenv->dbrename (dbenv, NULL, oldpath, NULL, newpath, 0);
    
		if (ret != 0) {
			gf_log ("bctx",
				GF_LOG_ERROR,
				"failed to rename %s to %s: %s", 
				oldpath, newpath, db_strerror (ret));
		} else {
			gf_log ("bctx",
				GF_LOG_DEBUG,
				"successfully renamed %s to %s: %s",
				oldpath, newpath, db_strerror (ret));
		}
	}
	UNLOCK (&table->lock);

out:
	return ret;
}

bctx_t *
bctx_rename (bctx_t *bctx, 
	     const char *db_newpath)
{
	bctx_table_t *table = NULL;
	int32_t ret = -1;

	table = bctx->table;

	LOCK (&table->lock);
	{
		__unhash_bctx (bctx);
		list_del_init (&bctx->list);
		if (bctx->dbp) {
			ret = bctx->dbp->close (bctx->dbp, 0);
			if (ret != 0) {
				gf_log ("bdb-ll",
					GF_LOG_ERROR,
					"failed to close db for directory %s (%s)",
					bctx->directory, db_strerror (ret));
			}
			bctx->dbp = NULL;
		}
	}
	UNLOCK (&table->lock);
  
	ret = bdb_db_rename (table, bctx->db_path, db_newpath);
	
	if (ret != 0) {
		gf_log ("bctx",
			GF_LOG_ERROR,
			"bdb_db_rename failed for directory %s",
			bctx->directory);
		bctx = NULL;
	} 

	return bctx;
}
