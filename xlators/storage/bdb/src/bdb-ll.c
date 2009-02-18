/*
  Copyright (c) 2008, 2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include <libgen.h>
#include "bdb.h"
#include <list.h>
/* 
 * implement the procedures to interact with bdb */

/****************************************************************
 *
 * General wrappers and utility procedures for bdb xlator
 *
 ****************************************************************/
#define BDB_LL_PAGE_SIZE_DEFAULT    4096
#define BDB_LL_PAGE_SIZE_MIN    4096
#define BDB_LL_PAGE_SIZE_MAX    65536

ino_t
bdb_inode_transform (ino_t parent,
                     bctx_t *bctx)
{
	struct bdb_private *private = NULL;
	ino_t               ino = -1;
	
	GF_VALIDATE_OR_GOTO ("bdb-ll", bctx, out);

	private = bctx->table->this->private;

	LOCK (&private->ino_lock);
	ino = ++private->next_ino;
	UNLOCK (&private->ino_lock);
out:
	return ino;
}


/***********************************************************
 *
 *  bdb storage database utilities
 *
 **********************************************************/

/*
 * bdb_db_open - opens a storage db.
 *
 * @ctx: context specific to the directory for which we are supposed to open db
 *
 * see, if we have empty slots to open a db.
 *      if (no-empty-slots), then prune open dbs and close as many as possible
 *      if (empty-slot-available), tika muchkonDu db open maaDu
 *
 * NOTE: illi baro munche lock hiDkobEku
 */
static DB *
bdb_db_open (bctx_t *bctx)
{
	DB *storage_dbp = NULL;
	int32_t op_ret = -1;
	bctx_table_t *table = NULL;
	
	GF_VALIDATE_OR_GOTO ("bdb-ll", bctx, out);

	table = bctx->table;
	GF_VALIDATE_OR_GOTO ("bdb-ll", table, out);

	/* we have to do the following, we can't deny someone of db_open ;) */
	op_ret = db_create (&storage_dbp, table->dbenv, 0);
	if (op_ret != 0) {
		gf_log ("bdb-ll", GF_LOG_ERROR,
			"failed to do db_create for directory %s (%s)",
			bctx->directory, db_strerror (op_ret));
		storage_dbp = NULL;
		goto out;
	} 

	if (table->page_size) {
		op_ret = storage_dbp->set_pagesize (storage_dbp, 
						    table->page_size);
		if (op_ret != 0) {
			gf_log ("bdb-ll", GF_LOG_ERROR, 
				"failed to set the page_size (%"PRIu64") for directory %s (%s)", 
				table->page_size, bctx->directory, db_strerror (op_ret));
		} else {
			gf_log ("bdb-ll", GF_LOG_DEBUG,
				"page-size (%"PRIu64") set on DB", 
				table->page_size);
		}
	}
     
	op_ret = storage_dbp->open (storage_dbp,
				    NULL,
				    bctx->db_path,
				    NULL,
				    table->access_mode,
				    table->dbflags,
				    0);
	if (op_ret != 0 ) {
		gf_log ("bdb-ll",
			GF_LOG_ERROR,
			"failed to open storage-db for directory %s (%s)", 
			bctx->db_path, db_strerror (op_ret));
		storage_dbp = NULL;
	} 

out:
	return storage_dbp;
}



int32_t
bdb_cursor_close (bctx_t *bctx,
		  DBC *cursorp)
{
	int32_t ret = -1;
	
	GF_VALIDATE_OR_GOTO ("bdb-ll", bctx, out);
	GF_VALIDATE_OR_GOTO ("bdb-ll", cursorp, out);

	LOCK (&bctx->lock);
	{
#ifdef HAVE_BDB_CURSOR_GET
		ret = cursorp->close (cursorp);
#else
		ret = cursorp->c_close (cursorp);
#endif
		if ((ret != 0)) {
			gf_log ("bdb-ll",
				GF_LOG_ERROR,
				"failed to close db cursor for directory %s (%s)",
				bctx->directory, db_strerror (ret));
		}
	}
	UNLOCK (&bctx->lock);

out: 
	return ret;
}


int32_t
bdb_cursor_open (bctx_t *bctx,
		 DBC **cursorpp)
{
	int32_t ret = -1;
	
	GF_VALIDATE_OR_GOTO ("bdb-ll", bctx, out);
	GF_VALIDATE_OR_GOTO ("bdb-ll", cursorpp, out);

	LOCK (&bctx->lock);
	{
		if (bctx->dbp) {
			/* do nothing, just continue */
			ret = 0;
		} else {
			bctx->dbp = bdb_db_open (bctx);
			if (!bctx->dbp) {
				gf_log ("bdb-ll",
					GF_LOG_ERROR,
					"failed to open storage db for %s", 
					bctx->directory);
				ret = -1;
			} else {
				ret = 0;
			}
		}
  
		if (ret == 0) {
			/* all set, lets open cursor */
			ret = bctx->dbp->cursor (bctx->dbp, NULL, cursorpp, 0);
			if (ret != 0) {
				gf_log ("bdb-ll",
					GF_LOG_ERROR,
					"failed to create a cursor for %s (%s)", 
					bctx->directory, db_strerror (ret));
			}
		}
	}
	UNLOCK (&bctx->lock);

out:
	return ret;
}


/* cache related */
static bdb_cache_t *
bdb_cache_lookup (bctx_t *bctx,
                  char *path)
{
	bdb_cache_t *bcache = NULL; 
	bdb_cache_t *trav   = NULL;
	char        *key    = NULL;
	
	GF_VALIDATE_OR_GOTO ("bdb-ll", bctx, out);
	GF_VALIDATE_OR_GOTO ("bdb-ll", path, out);

	MAKE_KEY_FROM_PATH (key, path);

	LOCK (&bctx->lock);
	{
		list_for_each_entry (trav, &bctx->c_list, c_list) {
			if (!strcmp (trav->key, key)){
				bcache = trav;
				break;
			}
		}
	}
	UNLOCK (&bctx->lock);

out:
	return bcache;
}

static int32_t
bdb_cache_insert (bctx_t *bctx, 
		  DBT *key, 
		  DBT *data)
{
	bdb_cache_t *bcache = NULL;
	int32_t ret = -1;

	GF_VALIDATE_OR_GOTO ("bdb-ll", bctx, out);
	GF_VALIDATE_OR_GOTO ("bdb-ll", key, out);
	GF_VALIDATE_OR_GOTO ("bdb-ll", data, out);

	LOCK (&bctx->lock);
	{
		if (bctx->c_count > 5) {
			/* most of the times, we enter here */
			/* FIXME: ugly, not supposed to disect any of the
			 * 'struct list_head' directly */
			if (!list_empty (&bctx->c_list)) {
				bcache = list_entry (bctx->c_list.prev, bdb_cache_t, c_list);
				list_del_init (&bcache->c_list);
			}
			if (bcache->key) {
				free (bcache->key);
				bcache->key = strdup ((char *)key->data);
				GF_VALIDATE_OR_GOTO ("bdb-ll", bcache->key, unlock);
			} else {
				/* should never come here */
				gf_log ("bdb-ll",
					GF_LOG_CRITICAL,
					"bcache->key (null)");
			} /* if(bcache->key)...else */
			if (bcache->data) {
				free (bcache->data);
				bcache->data = memdup (data->data, data->size);
				GF_VALIDATE_OR_GOTO ("bdb-ll", bcache->data, unlock);
				bcache->size = data->size;
			} else {
				/* should never come here */
				gf_log ("bdb-ll",
					GF_LOG_CRITICAL,
					"bcache->data (null)");
			} /* if(bcache->data)...else */
			list_add (&bcache->c_list, &bctx->c_list);
			ret = 0;
		} else {
			/* we will be entering here very rarely */
			bcache = CALLOC (1, sizeof (*bcache));    
			GF_VALIDATE_OR_GOTO ("bdb-ll", bcache, unlock);
			bcache->key = strdup ((char *)(key->data));
			GF_VALIDATE_OR_GOTO ("bdb-ll", bcache->key, unlock);
			bcache->data = memdup (data->data, data->size);
			GF_VALIDATE_OR_GOTO ("bdb-ll", bcache->data, unlock);
			bcache->size = data->size;
			list_add (&bcache->c_list, &bctx->c_list);
			bctx->c_count++;
			ret = 0;
		} /* if(private->c_count < 5)...else */
	}
unlock:
	UNLOCK (&bctx->lock);
out:
	return ret;
}

static int32_t
bdb_cache_delete (bctx_t *bctx,
		  char *key)
{
	bdb_cache_t *bcache = NULL; 
	bdb_cache_t *trav   = NULL;
	
	GF_VALIDATE_OR_GOTO ("bdb-ll", bctx, out);
	GF_VALIDATE_OR_GOTO ("bdb-ll", key, out);

	LOCK (&bctx->lock);
	{
		list_for_each_entry (trav, &bctx->c_list, c_list) {
			if (!strcmp (trav->key, key)){
				bctx->c_count--;
				bcache = trav;
				break;
			}
		}
  
		if (bcache) {
			list_del_init (&bcache->c_list);
			free (bcache->key);
			free (bcache->data);
			free (bcache);
		}
	}
	UNLOCK (&bctx->lock);

out:
	return 0;
}

void *
bdb_db_stat (bctx_t *bctx, 
	     DB_TXN *txnid,
	     uint32_t flags)
{
	DB     *storage = NULL;
	void   *stat    = NULL;
	int32_t ret     = -1;

	LOCK (&bctx->lock);
	{
		if (bctx->dbp == NULL) {
			bctx->dbp = bdb_db_open (bctx);
			storage = bctx->dbp;
		} else {
			/* we are just fine, lets continue */
			storage = bctx->dbp;
		} /* if(bctx->dbp==NULL)...else */
	}
	UNLOCK (&bctx->lock);

	GF_VALIDATE_OR_GOTO ("bdb-ll", storage, out);

	ret = storage->stat (storage, txnid, &stat, flags);
    
	if (ret != 0) {
		gf_log ("bdb-ll",
			GF_LOG_ERROR,
			"failed to do DB->stat() on db file %s: %s", 
			bctx->db_path, db_strerror (ret));
	} else {
		gf_log ("bdb-ll",
			GF_LOG_DEBUG,
			"successfully called DB->stat() on db file %s", 
			bctx->db_path);
	}
out:
	return stat;
  
}

/* bdb_storage_get - retrieve a key/value pair corresponding to @path from the corresponding
 *                   db file.
 *
 * @bctx: bctx_t * corresponding to the parent directory of @path. (should always be a valid
 *        bctx).  bdb_storage_get should never be called if @bctx = NULL.
 * @txnid: NULL if bdb_storage_get is not embedded in an explicit transaction or a valid
 *         DB_TXN *, when embedded in an explicit transaction.        
 * @path: path of the file to read from (translated to a database key using MAKE_KEY_FROM_PATH)
 * @buf: char ** - pointer to a pointer to char. a read buffer is created in this procedure
 *       and pointer to the buffer is passed through @buf to the caller.
 * @size: size of the file content to be read.
 * @offset: offset from which the file content to be read.
 *
 * NOTE: bdb_storage_get tries to open DB, if @bctx->dbp == NULL (@bctx->dbp == NULL, 
 *      nobody has opened DB till now or DB was closed by bdb_table_prune()).
 *
 * NOTE: if private->cache is set (bdb xlator's internal caching enabled), then bdb_storage_get
 *      first looks up the cache for key/value pair. if bdb_lookup_cache fails, then only 
 *      DB->get() is called. also,  inserts a newly read key/value pair to cache through
 *      bdb_insert_to_cache.
 *
 * return: 'number of bytes read' on success or -1 on error.
 *
 * also see: bdb_lookup_cache, bdb_insert_to_cache for details about bdb xlator's internal cache.
 */
int32_t
bdb_db_get (bctx_t *bctx,
	    DB_TXN *txnid,
	    const char *path,
	    char **buf,
	    size_t size,
	    off_t offset)
{
	DB          *storage    = NULL;
	DBT          key        = {0,};
	DBT          value      = {0,};
	int32_t      ret        = -1;
	char        *key_string = NULL;
	bdb_cache_t *bcache     = NULL;
	int32_t      db_flags   = 0;
	uint8_t      need_break = 0;
	int32_t      retries    = 1;
	
	GF_VALIDATE_OR_GOTO ("bdb-ll", bctx, out);
	GF_VALIDATE_OR_GOTO ("bdb-ll", path, out);

	MAKE_KEY_FROM_PATH (key_string, path);
	
	if (bctx->cache && 
	    ((bcache = bdb_cache_lookup (bctx, key_string)) != NULL)) {
		if (buf) {
			*buf = CALLOC (1, bcache->size);
			GF_VALIDATE_OR_GOTO ("bdb-ll", buf, out);
			memcpy (*buf, (bcache->data + offset), bcache->size);
		}
		ret = bcache->size;
	} else {
		LOCK (&bctx->lock);
		{
			if (bctx->dbp == NULL) {
				bctx->dbp = bdb_db_open (bctx);
				storage = bctx->dbp;
			} else {
				/* we are just fine, lets continue */
				storage = bctx->dbp;
			} /* if(bctx->dbp==NULL)...else */
		}
		UNLOCK (&bctx->lock);
		
		GF_VALIDATE_OR_GOTO ("bdb-ll", storage, out);

		key.data = (char *)key_string;
		key.size = strlen (key_string);
		key.flags = DB_DBT_USERMEM;
      
		if (bctx->cache){
			/* we are called to return the size of the file */
			value.flags = DB_DBT_MALLOC;
		} else {
			if (size) {
				value.flags = DB_DBT_MALLOC | DB_DBT_PARTIAL;
			} else {
				value.flags = DB_DBT_MALLOC;
			}
			value.dlen = size;
			value.doff = offset;
		}
      
		do {
			/* TODO: we prefer to give our own buffer to value.data
			 * and ask bdb to fill in it */
			ret = storage->get (storage, txnid, &key, &value, db_flags);
        
			if (ret == DB_NOTFOUND) {
				gf_log ("bdb-ll",
					GF_LOG_DEBUG,
					"failed to do DB->get() for key: %s."
					" key not found in storage DB", key_string);
				ret = -1;
				need_break = 1;
			} else if (ret == DB_LOCK_DEADLOCK) {
				retries++;
				gf_log ("bdb-ll",
					GF_LOG_ERROR,
					"deadlock detected in DB->put. retrying DB->put (%d)", 
					retries);
			}else if (ret == 0) {
				/* successfully read data, lets set everything in place
				 * and return */
				if (buf) {
					*buf = CALLOC (1, value.size);
					ERR_ABORT (*buf);
					memcpy (*buf, value.data, value.size);
				}
				ret = value.size;
				if (bctx->cache)
					bdb_cache_insert (bctx, &key, &value);
				free (value.data);
				need_break = 1;
			} else {
				gf_log ("bdb-ll",
					GF_LOG_ERROR,
					"failed to do DB->get() for key %s: %s", 
					key_string, db_strerror (ret));
				ret = -1;
				need_break = 1;
			}
		} while (!need_break);
	}
out:  
	return ret;
}/* bdb_db_get */

/* bdb_storage_put - insert a key/value specified to the corresponding DB.
 *
 * @bctx: bctx_t * corresponding to the parent directory of @path. 
 *        (should always be a valid bctx). bdb_storage_put should never be called if @bctx = NULL.
 * @txnid: NULL if bdb_storage_put is not embedded in an explicit transaction or a valid
 *         DB_TXN *, when embedded in an explicit transaction. 
 * @key_string: key of the database entry.
 * @buf: pointer to the buffer data to be written as data for @key_string.
 * @size: size of @buf.
 * @offset: offset in the key's data to be modified with provided data.
 * @flags: valid flags are BDB_TRUNCATE_RECORD (to reduce the data of @key_string to 0 size).
 *
 * NOTE: bdb_storage_put tries to open DB, if @bctx->dbp == NULL (@bctx->dbp == NULL, 
 *      nobody has opened DB till now or DB was closed by bdb_table_prune()).
 *
 * NOTE: bdb_storage_put deletes the key/value from bdb xlator's internal cache.
 *
 * return: 0 on success or -1 on error.
 *
 * also see: bdb_cache_delete for details on how a cached key/value pair is removed.
 */
int32_t
bdb_db_put (bctx_t *bctx,
	    DB_TXN *txnid,
	    const char *key_string,
	    const char *buf,
	    size_t size,
	    off_t offset,
	    int32_t flags)
{
	DB     *storage = NULL;
	DBT     key = {0,}, value = {0,};
	int32_t ret = -1;
	int32_t db_flags = DB_AUTO_COMMIT;
	uint8_t need_break = 0;
	int32_t retries = 1;

	LOCK (&bctx->lock);
	{
		if (bctx->dbp == NULL) {
			bctx->dbp = bdb_db_open (bctx);
			storage = bctx->dbp;
		} else {
			/* we are just fine, lets continue */
			storage = bctx->dbp;
		}
	}
	UNLOCK (&bctx->lock);
	
	GF_VALIDATE_OR_GOTO ("bdb-ll", storage, out);

	if (bctx->cache) {
		ret = bdb_cache_delete (bctx, (char *)key_string);
		GF_VALIDATE_OR_GOTO ("bdb-ll", (ret == 0), out);
	}

	key.data = (void *)key_string;
	key.size = strlen (key_string);
    
	/* NOTE: bdb lets us expand the file, suppose value.size > value.len, then value.len bytes
	 *      from value.doff offset and value.size bytes will be written from value.doff and 
	 *      data from value.doff + value.dlen will be pushed value.doff + value.size
	 */
	value.data = (void *)buf;

	if (flags & BDB_TRUNCATE_RECORD) {
		value.size = size;
		value.doff = 0;
		value.dlen = offset;
	} else {
		value.size = size;
		value.dlen = size;
		value.doff = offset;
	}
	value.flags = DB_DBT_PARTIAL;
	if (buf == NULL && size == 0) 
		/* truncate called us */
		value.flags = 0;
    
	do {
		ret = storage->put (storage, txnid, &key, &value, db_flags);
		if (ret == DB_LOCK_DEADLOCK) {
			retries++;
			gf_log ("bdb-ll",
				GF_LOG_ERROR,
				"deadlock detected in DB->put. retrying DB->put (%d)", 
				retries);
		} else if (ret) {
			/* write failed */
			gf_log ("bdb-ll",
				GF_LOG_ERROR,
				"failed to do DB->put() for key %s: %s", 
				key_string, db_strerror (ret));
			need_break = 1;
		} else {
			/* successfully wrote */
			ret = 0;
			need_break = 1;
		}
	} while (!need_break);
out:
	return ret;
}/* bdb_db_put */


/* bdb_storage_del - delete a key/value pair corresponding to @path from corresponding db file.
 *
 * @bctx: bctx_t * corresponding to the parent directory of @path. 
 *       (should always be a valid bctx). bdb_storage_del should never be called
 *       if @bctx = NULL.
 * @txnid: NULL if bdb_storage_del is not embedded in an explicit transaction or a
 *         valid DB_TXN *, when embedded in an explicit transaction. 
 * @path: path to the file, whose key/value pair has to be deleted.
 *
 * NOTE: bdb_storage_del tries to open DB, if @bctx->dbp == NULL (@bctx->dbp == NULL, 
 *      nobody has opened DB till now or DB was closed by bdb_table_prune()).
 *
 * return: 0 on success or -1 on error.
 */
int32_t
bdb_db_del (bctx_t *bctx,
	    DB_TXN *txnid,
	    const char *path)
{
	DB     *storage    = NULL;
	DBT     key        = {0,};
	char   *key_string = NULL;
	int32_t ret        = -1;
	int32_t db_flags   = 0;
	uint8_t need_break = 0;
	int32_t retries    = 1;

	MAKE_KEY_FROM_PATH (key_string, path);

	LOCK (&bctx->lock);
	{
		if (bctx->dbp == NULL) {
			bctx->dbp = bdb_db_open (bctx);
			storage = bctx->dbp;
		} else {
			/* we are just fine, lets continue */
			storage = bctx->dbp;
		}
	}
	UNLOCK (&bctx->lock);  
	
	GF_VALIDATE_OR_GOTO ("bdb-ll", storage, out);

	ret = bdb_cache_delete (bctx, key_string);
	GF_VALIDATE_OR_GOTO ("bdb-ll", (ret == 0), out);

	key.data = key_string;
	key.size = strlen (key_string);
	key.flags = DB_DBT_USERMEM;
    
	do {
		ret = storage->del (storage, txnid, &key, db_flags);
      
		if (ret == DB_NOTFOUND) {
			gf_log ("bdb-ll",
				GF_LOG_DEBUG,
				"failed to delete %s from storage db, doesn't exist in storage DB", 
				path);
			need_break = 1;
		} else if (ret == DB_LOCK_DEADLOCK) {
			retries++;
			gf_log ("bdb-ll",
				GF_LOG_ERROR,
				"deadlock detected in DB->put. retrying DB->put (%d)", 
				retries);
		}else if (ret == 0) {
			/* successfully deleted the entry */
			gf_log ("bdb-ll",
				GF_LOG_DEBUG,
				"deleted %s from storage db", path);
			ret = 0;
			need_break = 1;
		} else {
			gf_log ("bdb-ll",
				GF_LOG_ERROR,
				"failed to delete %s from storage db: %s", 
				path, db_strerror (ret));
			ret = -1;
			need_break = 1;    
		}
	} while (!need_break);
out:
	return ret;
}

/* NOTE: bdb version compatibility wrapper */
int32_t
bdb_cursor_get (DBC *cursorp,
                DBT *key,
                DBT *value,
                int32_t flags)
{
	int32_t ret = -1;
	
	GF_VALIDATE_OR_GOTO ("bdb-ll", cursorp, out);

#ifdef HAVE_BDB_CURSOR_GET
	ret = cursorp->get (cursorp, key, value, flags);
#else
	ret = cursorp->c_get (cursorp, key, value, flags);
#endif
	if ((ret != 0)  && (ret != DB_NOTFOUND)) {
		gf_log ("bdb-ll",
			GF_LOG_ERROR,
			"failed to CURSOR->get() for key %s (%s)",
			(char *)key->data, db_strerror (ret));
	}

out:
	return ret;
}/* bdb_cursor_get */


int32_t
bdb_dirent_size (DBT *key)
{
	return ALIGN (24 /* FIX MEEEE!!! */ + key->size);
}


/* bdb_extract_bfd - translate a fd_t to a bfd (either a 'struct bdb_bfd' or 'struct bdb_dir')
 *
 * @fd->ctx is with bdb specific file handle during a successful bdb_open (also bdb_create)
 *  or bdb_opendir.
 *
 * return: 'struct bdb_bfd *' or 'struct bdb_dir *' on success, or NULL on failure.
 */
inline void *
bdb_extract_bfd (fd_t *fd,
                 xlator_t *this)
{
	uint64_t tmp_bfd = 0;
	void    *bfd     = NULL;
 	
	GF_VALIDATE_OR_GOTO ("bdb-ll", fd, out);
	GF_VALIDATE_OR_GOTO ("bdb-ll", this, out);

	fd_ctx_get (fd, this, &tmp_bfd);
	bfd = (void *)(long)bfd;

out:
	return bfd;
}

/* bdb_dbenv_init - initialize DB_ENV
 *
 *  initialization includes:
 *   1. opening DB_ENV (db_env_create(), DB_ENV->open()). 
 *      NOTE: see private->envflags for flags used.
 *   2. DB_ENV->set_lg_dir - set log directory to be used for storing log files 
 *     (log files are the files in which transaction logs are written by db).
 *   3. DB_ENV->set_flags (DB_LOG_AUTOREMOVE) - set DB_ENV to automatically clear
 *      the unwanted log files (flushed at each checkpoint).
 *   4. DB_ENV->set_errfile - set errfile to be used by db to report detailed error logs. 
 *     used only for debbuging purpose.
 *
 * return: returns a valid DB_ENV * on success or NULL on error.
 *
 */
static DB_ENV *
bdb_dbenv_init (xlator_t *this,
		char *directory)
{
	/* Create a DB environment */
	DB_ENV        *dbenv       = NULL;
	int32_t        ret         = 0;
	bdb_private_t *private     = NULL;
	int32_t        fatal_flags = 0;
	
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (directory, out);

	private = this->private;
	VALIDATE_OR_GOTO (private, out);

	ret = db_env_create (&dbenv, 0);
	VALIDATE_OR_GOTO ((ret == 0), out);
	
	/* NOTE: set_errpfx returns 'void' */
	dbenv->set_errpfx(dbenv, this->name);
	
	ret = dbenv->set_lk_detect (dbenv, DB_LOCK_DEFAULT);
	VALIDATE_OR_GOTO ((ret == 0), out);

	ret = dbenv->open(dbenv, directory, 
			  private->envflags, 
			  S_IRUSR | S_IWUSR);
	if ((ret != 0) && (ret != DB_RUNRECOVERY)) {
		gf_log (this->name, 
			GF_LOG_CRITICAL, 
			"failed to open DB environment (%s)", 
			db_strerror (ret));
		dbenv = NULL;
		goto out;
	} else if (ret == DB_RUNRECOVERY) {
		fatal_flags = ((private->envflags & (~DB_RECOVER)) | DB_RECOVER_FATAL);
		ret = dbenv->open(dbenv, directory, 
				  fatal_flags, 
				  S_IRUSR | S_IWUSR);
		if (ret != 0) {
			gf_log (this->name, 
				GF_LOG_ERROR,
				"failed to open DB environment (%s) with DB_REOVER_FATAL",
				db_strerror (ret));
			dbenv = NULL;
			goto out;
		} else {
			gf_log (this->name, 
				GF_LOG_WARNING,
				"opened DB environment after DB_RECOVER_FATAL: %s", 
				db_strerror (ret));
		}
	} else {
		gf_log (this->name, 
			GF_LOG_DEBUG,
			"DB environment successfull opened: %s", 
			db_strerror (ret));
	}

        

#if (DB_VERSION_MAJOR == 4 &&			\
     DB_VERSION_MINOR == 7)
	if (private->log_auto_remove) {
		ret = dbenv->log_set_config (dbenv, DB_LOG_AUTO_REMOVE, 1);
	} else {
		ret = dbenv->log_set_config (dbenv, DB_LOG_AUTO_REMOVE, 0);
	}
#else
	if (private->log_auto_remove) {
		ret = dbenv->set_flags (dbenv, DB_LOG_AUTOREMOVE, 1);
	} else {
		ret = dbenv->set_flags (dbenv, DB_LOG_AUTOREMOVE, 0);
	}         
#endif
	if (ret != 0) {
		gf_log ("bctx",
			GF_LOG_ERROR,
			"failed to set DB_LOG_AUTOREMOVE on dbenv: %s", db_strerror (ret));
	} else {
		gf_log ("bctx",
			GF_LOG_DEBUG,
			"DB_LOG_AUTOREMOVE set on dbenv");
	}

	if (private->transaction) {
		ret = dbenv->set_flags(dbenv, DB_AUTO_COMMIT, 1);
          
		if (ret != 0) {
			gf_log ("bctx",
				GF_LOG_ERROR,
				"failed to set DB_AUTO_COMMIT on dbenv: %s", 
				db_strerror (ret));
		} else {
			gf_log ("bctx",
				GF_LOG_DEBUG,
				"DB_AUTO_COMMIT set on dbenv");
		}
          
		if (private->txn_timeout) {
			ret = dbenv->set_timeout (dbenv, 
						  private->txn_timeout, 
						  DB_SET_TXN_TIMEOUT);
			if (ret != 0) {
				gf_log ("bctx",
					GF_LOG_ERROR,
					"failed to set TXN_TIMEOUT to %d milliseconds "
					"on dbenv: %s", 					
					private->txn_timeout, db_strerror (ret));
			} else {
				gf_log ("bctx",
					GF_LOG_DEBUG,
					"TXN_TIMEOUT set to %d milliseconds", 
					private->txn_timeout);
			}
		}

		if (private->lock_timeout) {
			ret = dbenv->set_timeout(dbenv, 
						 private->txn_timeout, 
						 DB_SET_LOCK_TIMEOUT);
            
			if (ret != 0) {
				gf_log ("bctx",
					GF_LOG_ERROR,
					"failed to set LOCK_TIMEOUT to %d milliseconds "
					"on dbenv: %s", 
					private->lock_timeout, db_strerror (ret));
			} else {
				gf_log ("bctx",
					GF_LOG_DEBUG,
					"LOCK_TIMEOUT set to %d milliseconds", 
					private->lock_timeout);
			}
		}

		ret = dbenv->set_lg_dir (dbenv, private->logdir);
		
		if (ret != 0) {
			gf_log ("bctx",
				GF_LOG_ERROR,
				"failed to set log directory for dbenv: %s", db_strerror (ret));
		} else {
			gf_log ("bctx",
				GF_LOG_DEBUG,
				"set dbenv log dir to %s", private->logdir);
		}
		
	}
        
	if (private->errfile) {
		private->errfp = fopen (private->errfile, "a+");
		if (private->errfp) {
			dbenv->set_errfile (dbenv, private->errfp);
		} else {
			gf_log ("bctx",
				GF_LOG_ERROR,
				"failed to open errfile: %s", strerror (errno));
		}
	}

out:
	return dbenv;
}

#define BDB_ENV(this) ((((struct bdb_private *)this->private)->b_table)->dbenv)

/* bdb_checkpoint - during transactional usage, db does not directly write the data to db
 *                  files, instead db writes a 'log' (similar to a journal entry) into a
 *                  log file. db normally clears the log files during opening of an
 *                  environment. since we expect a filesystem server to run for a pretty
 *                  long duration and flushing 'log's during dbenv->open would prove very 
 *                  costly, if we accumulate the log entries for one complete run of 
 *                  glusterfs server. to flush the logs frequently, db provides a mechanism
 *                  called 'checkpointing'. when we do a checkpoint, db flushes the logs to
 *                  disk (writes changes to db files) and we can also clear the accumulated 
 *                  log files after checkpointing. NOTE: removing unwanted log files is not
 *                  part of dbenv->txn_checkpoint() call. 
 *
 * @data: xlator_t of the current instance of bdb xlator.
 *
 *  bdb_checkpoint is called in a different thread from the main glusterfs thread. bdb 
 *  xlator creates the checkpoint thread after successfully opening the db environment. 
 *  NOTE: bdb_checkpoint thread shares the DB_ENV handle with the filesystem thread.
 *
 *  db environment checkpointing frequency is controlled by 
 *  'option checkpoint-timeout <time-in-seconds>' in volfile.
 *
 * NOTE: checkpointing thread is started only if 'option transaction on' specified in
 *      volfile. checkpointing is not valid for non-transactional environments.
 *
 */
static void *
bdb_checkpoint (void *data)
{
	xlator_t *this = NULL;
	struct bdb_private *private = NULL;
	DB_ENV *dbenv = NULL;
	int32_t ret = 0;
	uint32_t active = 0;

	this = (xlator_t *) data;
	dbenv = BDB_ENV(this);
	private = this->private;

	for (;;sleep (private->checkpoint_timeout)) {
		LOCK (&private->active_lock);
		active = private->active;
		UNLOCK (&private->active_lock);
 
		if (active) {
			ret = dbenv->txn_checkpoint (dbenv, 1024, 0, 0);
			if (ret) {
				gf_log ("bctx",
					GF_LOG_ERROR,
					"failed to checkpoint environment: %s", db_strerror (ret));
			} else {
				gf_log ("bctx",
					GF_LOG_DEBUG,
					"checkpointing successful");
			} 
		} else {
			ret = dbenv->txn_checkpoint (dbenv, 1024, 0, 0);
			if (ret) {
				gf_log ("bctx",
					GF_LOG_ERROR,
					"failed to do final checkpoint environment: %s", 
					db_strerror (ret));
			} else {
				gf_log ("bctx",
					GF_LOG_DEBUG,
					"final checkpointing successful");
			}
			break;
		}
	}

	return NULL;
}

static inline void
BDB_CACHE_INIT (xlator_t *this,
		dict_t *options, 
		struct bdb_private *private)
{
	/* cache is always on */
	private->cache = ON;
}

static inline void
BDB_LOG_REMOVE_INIT(xlator_t *this,
		    dict_t *options,
		    struct bdb_private *private)
{
	private->log_auto_remove = 1;
	gf_log (this->name,
		GF_LOG_DEBUG,
		"DB_ENV will use DB_LOG_AUTO_REMOVE");
}

static inline void
BDB_ERRFILE_INIT (xlator_t *this,
		  dict_t *options,
		  struct bdb_private *private)
{
	data_t *errfile = NULL;

	errfile = dict_get (options, "errfile");
	if (errfile) {
		private->errfile = strdup (errfile->data);
		gf_log (this->name,
			GF_LOG_DEBUG,
			"using errfile: %s", private->errfile);
	} 
}

static inline void
BDB_TABLE_INIT (xlator_t *this,
		dict_t *options,
		struct bdb_private *private)
{
	bctx_table_t *table = NULL;
	int32_t        idx                = 0;
  
	data_t        *lru_limit          = NULL;
	data_t        *page_size          = NULL;
	
	table = CALLOC (1, sizeof (*table));
	if (table) {
		INIT_LIST_HEAD(&(table->b_lru));
		INIT_LIST_HEAD(&(table->active));
		INIT_LIST_HEAD(&(table->purge));

		LOCK_INIT (&table->lock);
		LOCK_INIT (&table->checkpoint_lock);
      
		table->transaction = private->transaction;
		table->access_mode = private->access_mode;
		table->dbflags = private->dbflags;
		table->this    = this;

		{
			lru_limit = dict_get (options, "lru-limit");
        
			/* TODO: set max lockers and max txns to accomodate 
			 * for more than lru_limit */
			if (lru_limit) {
				table->lru_limit = strtol (lru_limit->data, NULL, 0);
				gf_log ("bdb-ll",
					GF_LOG_DEBUG,
					"setting bctx lru limit to %d", table->lru_limit);
			} else {
				table->lru_limit = BDB_DEFAULT_LRU_LIMIT;
			}
		}

		{
			page_size = dict_get (options, "page-size");
        
			if (page_size)
			{
				if (gf_string2bytesize (page_size->data, 
							&table->page_size) != 0) {
					gf_log ("bdb-ll", 
						GF_LOG_ERROR, 
						"invalid number format \"%s\""
						" of \"option page-size\"", 
						page_size->data);
				}
            
				if (!(table->page_size >= BDB_LL_PAGE_SIZE_MIN && 
				      table->page_size <= BDB_LL_PAGE_SIZE_MAX)) {
					gf_log ("bdb-ll", 
						GF_LOG_ERROR, 
						"pagesize %s is out of range."
						"Allowed pagesize is between %d and %d", 
						page_size->data, 
						BDB_LL_PAGE_SIZE_MIN, 
						BDB_LL_PAGE_SIZE_MAX);
				}
			}
			else {
				table->page_size = BDB_LL_PAGE_SIZE_DEFAULT;
			}
			gf_log ("bdb-ll", 
				GF_LOG_DEBUG, "using page-size %"PRIu64, 
				table->page_size);
		}
      
		table->hash_size = BDB_DEFAULT_HASH_SIZE;
		table->b_hash = CALLOC (BDB_DEFAULT_HASH_SIZE, sizeof (struct list_head));

		for (idx = 0; idx < table->hash_size; idx++)
			INIT_LIST_HEAD(&(table->b_hash[idx]));

		private->b_table = table;
	} else {
		gf_log ("bdb-ll",
			GF_LOG_CRITICAL,
			"failed to allocate bctx table: out of memory");
	}
}

static inline void 
BDB_DIRECTORY_INIT (xlator_t *this, 
		    dict_t *options, 
		    struct bdb_private *private)
{
	data_t *directory = NULL;
	data_t *logdir = NULL;
	int32_t op_ret = -1;
	struct stat stbuf = {0};

	directory = dict_get (options, "directory");
    
	if (directory) {
		logdir = dict_get (options, "logdir");
        
		if (logdir == NULL) {
			gf_log ("bdb-ll",
				GF_LOG_DEBUG,
				"using default logdir as database home");
			private->logdir = strdup (directory->data);
					
		} else {
			private->logdir = strdup (logdir->data);
			gf_log ("bdb-ll",
				GF_LOG_DEBUG,
				"using logdir: %s", private->logdir);
			umask (000);
			if (mkdir (private->logdir, 0777) == 0) {
				gf_log ("bdb-ll", GF_LOG_WARNING,
					"logdir specified (%s) not exists, created", 
					private->logdir);
			}
            
			op_ret = stat (private->logdir, &stbuf);
			if ((op_ret != 0) || !S_ISDIR (stbuf.st_mode)) {
				gf_log ("bdb-ll", 
					GF_LOG_ERROR, 
					"specified logdir doesn't exist, "
					"using default (environment home directory: %s)", 
					directory->data);
				private->logdir = strdup (directory->data);
			}
		}

		private->b_table->dbenv = bdb_dbenv_init (this, directory->data);
      
		if (!private->b_table->dbenv) {
			gf_log ("bdb-ll", GF_LOG_ERROR,
				"failed to initialize db environment");
			FREE (private);
			op_ret = -1;
		} else {
			if (private->transaction) {
				/* all well, start the checkpointing thread */
				LOCK_INIT (&private->active_lock);
				
				LOCK (&private->active_lock);
				private->active = 1;
				UNLOCK (&private->active_lock);
				pthread_create (&private->checkpoint_thread, NULL,
						bdb_checkpoint, this);
			}
		}
	}
}

static inline void
BDB_DIR_MODE_INIT (xlator_t *this,
		   dict_t *options, 
		   struct bdb_private *private)
{
	data_t *dir_mode = NULL;
	char *endptr = NULL;

	dir_mode = dict_get (options, "dir-mode");

	if (dir_mode) {
		private->dir_mode = strtol (dir_mode->data, &endptr, 8);
		if ((*endptr) || 
		    (!IS_VALID_FILE_MODE(private->dir_mode))) {
			gf_log (this->name,
				GF_LOG_DEBUG,
				"invalid dir-mode %o. setting to default %o", 
				private->dir_mode, 
				DEFAULT_DIR_MODE);
			private->dir_mode = DEFAULT_DIR_MODE;
		} else {
			gf_log (this->name,
				GF_LOG_DEBUG,
				"setting dir-mode to %o", private->dir_mode);
			private->dir_mode = private->dir_mode;
		}
	} else {
		private->dir_mode = DEFAULT_DIR_MODE;
	}
    
	private->dir_mode = private->dir_mode | S_IFDIR;
}

static inline void
BDB_FILE_MODE_INIT (xlator_t *this,
		    dict_t *options, 
		    struct bdb_private *private)
{
	data_t *file_mode = NULL;
	char *endptr = NULL;

	file_mode = dict_get (options, "file-mode");

	if (file_mode) {
		private->file_mode = strtol (file_mode->data, &endptr, 8);

		if ((*endptr) || 
		    (!IS_VALID_FILE_MODE(private->file_mode))) {
			gf_log (this->name,
				GF_LOG_DEBUG,
				"invalid file-mode %o. setting to default %o", 
				private->file_mode, 
				DEFAULT_FILE_MODE);
			private->file_mode = DEFAULT_FILE_MODE;
		} else {
			gf_log (this->name,
				GF_LOG_DEBUG,
				"setting file-mode to %o", private->file_mode);
			private->file_mode = private->file_mode;
		}
	} else {
		private->file_mode = DEFAULT_FILE_MODE;
	}
    
	private->symlink_mode = private->file_mode | S_IFLNK;
	private->file_mode = private->file_mode | S_IFREG;
}

static inline void
BDB_CHECKPOINT_TIMEOUT_INIT (xlator_t *this,
			     dict_t *options, 
			     struct bdb_private *private)
{
	data_t        *checkpoint_timeout = NULL;

	checkpoint_timeout = dict_get (options, "checkpoint-timeout");
    
	private->checkpoint_timeout = BDB_DEFAULT_CHECKPOINT_TIMEOUT;

	if (checkpoint_timeout) {
		private->checkpoint_timeout = strtol (checkpoint_timeout->data, NULL, 0);
      
		if (private->checkpoint_timeout < 5 || private->checkpoint_timeout > 60) {
			gf_log (this->name,
				GF_LOG_WARNING,
				"checkpoint-timeout %d seconds too %s", 
				private->checkpoint_timeout, 
				(private->checkpoint_timeout < 5)?"low":"high");
		} else {
			gf_log (this->name,
				GF_LOG_DEBUG,
				"setting checkpoint-timeout to %d seconds", 
				private->checkpoint_timeout);
		}
	} else {
		gf_log (this->name,
			GF_LOG_DEBUG,
			"setting checkpoint-timeout to default: %d seconds", 
			private->checkpoint_timeout);
	}
}

static inline void
BDB_LOCK_TIMEOUT_INIT (xlator_t *this,
		       dict_t *options, 
		       struct bdb_private *private)
{
	data_t        *lock_timeout       = NULL;

	lock_timeout = dict_get (options, "lock-timeout");
    
	if (lock_timeout) {
		private->lock_timeout = strtol (lock_timeout->data, NULL, 0);
      
		if (private->lock_timeout > 4260000) {
			/* db allows us to DB_SET_LOCK_TIMEOUT to be set to a
			 * maximum of 71 mins (4260000 milliseconds) */
			gf_log (this->name,
				GF_LOG_DEBUG,
				"lock-timeout %d, out of range",
				private->lock_timeout);
			private->lock_timeout = 0;
		} else {
			gf_log (this->name,
				GF_LOG_DEBUG,
				"setting lock-timeout to %d milliseconds", 
				private->lock_timeout);
		}
	}
}

static inline void
BDB_TRANSACTION_TIMEOUT_INIT (xlator_t *this,
			      dict_t *options, 
			      struct bdb_private *private)
{
	data_t *txn_timeout = NULL;
	txn_timeout = dict_get (options, "transaction-timeout");
    
	if (txn_timeout) {
		private->txn_timeout = strtol (txn_timeout->data, NULL, 0);
      
		if (private->txn_timeout > 4260000) {
			/* db allows us to DB_SET_TXN_TIMEOUT to be set to a maximum
			 * of 71 mins (4260000 milliseconds) */
			gf_log (this->name,
				GF_LOG_DEBUG,
				"transaction-timeout %d, out of range",
				private->txn_timeout);
			private->txn_timeout = 0;
		} else {
			gf_log (this->name,
				GF_LOG_DEBUG,
				"setting transaction-timeout to %d milliseconds", 
				private->txn_timeout);
		}
	}
}

static inline void
BDB_TRANSACTION_INIT (xlator_t *this,
		      dict_t *options, 
		      struct bdb_private *private)
{
	data_t *mode = NULL;

	mode = dict_get (options, "mode");
    
	if (mode && !strcmp (mode->data, "off")) {
		gf_log (this->name,
			GF_LOG_DEBUG,
			"cache mode selected");
		private->envflags = DB_CREATE | DB_INIT_LOG | 
			DB_INIT_MPOOL | DB_THREAD;
		private->dbflags = DB_CREATE | DB_THREAD;
		private->transaction = OFF;
	} else {
		gf_log (this->name,
			GF_LOG_DEBUG,
			"persistant mode selected");
		private->transaction = ON;
		private->envflags = DB_CREATE | DB_INIT_LOCK | DB_INIT_LOG | 
			DB_INIT_MPOOL | DB_INIT_TXN | DB_RECOVER | DB_THREAD;
		private->dbflags = DB_CREATE | DB_THREAD;
	}
}

static inline void
BDB_ACCESS_MODE_INIT (xlator_t *this,
		      dict_t *options, 
		      struct bdb_private *private)
{
	data_t *access_mode = NULL;
	
	access_mode = dict_get (options, "access-mode");
    
	if (access_mode && !strcmp (access_mode->data, "btree")) {
		gf_log (this->name,
			GF_LOG_DEBUG,
			"using access mode BTREE");
		private->access_mode = DB_BTREE;
	} else {
		gf_log (this->name,
			GF_LOG_DEBUG,
			"using access mode HASH");
		private->access_mode = DB_HASH;
	}
}


/* bdb_db_init - initialize bdb xlator
 * 
 * reads the options from @options dictionary and sets appropriate values in @this->private.
 * also initializes DB_ENV.
 *
 * return: 0 on success or -1 on error (with logging the error through gf_log()).
 */
int
bdb_db_init (xlator_t *this,
             dict_t *options)
{
	/* create a db entry for root */
	int32_t        op_ret             = 0;
	bdb_private_t *private            = NULL;
  
	private = this->private;

	BDB_CACHE_INIT (this, options, private);
	
	BDB_ACCESS_MODE_INIT (this, options, private);

	BDB_TRANSACTION_INIT (this, options, private);

	BDB_TRANSACTION_TIMEOUT_INIT (this, options, private);

	BDB_LOCK_TIMEOUT_INIT (this, options, private);

	{
		LOCK_INIT (&private->ino_lock);
		private->next_ino = 2;
	}
	
	BDB_CHECKPOINT_TIMEOUT_INIT (this, options, private);
	
	BDB_FILE_MODE_INIT (this, options, private);

	BDB_DIR_MODE_INIT (this, options, private);

	BDB_TABLE_INIT (this, options, private);

	BDB_ERRFILE_INIT (this, options, private);

	BDB_LOG_REMOVE_INIT (this, options, private);

	BDB_DIRECTORY_INIT (this, options, private);

	return op_ret;
}
