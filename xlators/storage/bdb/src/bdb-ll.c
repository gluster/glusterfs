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
ino_t
bdb_inode_transform (ino_t parent,
		     bctx_t *bctx)
{
#if 0
  ino_t ino = 0;
  uint32_t shift = 18;

  shift = ((struct bdb_private *)bctx->table->this->private)->inode_bit_shift;
  LOCK (&bctx->lock);
  ino = ((parent << shift) | bctx->iseed);
  bctx->iseed++;
  UNLOCK (&bctx->lock);
  return ino & 0x00000000ffffffff;
#endif

  struct bdb_private *priv = NULL;
  ino_t ino = 0;

  priv = bctx->table->this->private;

  LOCK (&priv->ino_lock);
  ino = ++priv->next_ino;
  UNLOCK (&priv->ino_lock);

  return ino;
}


/***********************************************************
 *
 *  bdb storage database utilities
 *
 **********************************************************/

/*
 * bdb_open_storage_db - opens a storage db.
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
bdb_open_storage_db (bctx_t *bctx)
{
  DB *storage_dbp = NULL;
  int32_t op_ret = -1;
  bctx_table_t *table = bctx->table;

  /* we have to do the following, we can't deny someone of db_open ;) */
  op_ret = db_create (&storage_dbp, table->dbenv, 0);
  if (op_ret != 0) {
    op_ret = -1;
    storage_dbp = NULL;
  } else {
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
	      "failed to open storage-db %s: %s", bctx->db_path, db_strerror (op_ret));
      op_ret = -1;
      storage_dbp = NULL;
    } else {
      /* do nothing */
    }
  }

  return storage_dbp;
}



int32_t
bdb_close_db_cursor (bctx_t *ctx,
		     DBC *cursorp)
{
  int32_t ret = 0;

  LOCK (&ctx->lock);
#ifdef HAVE_BDB_CURSOR_GET
  ret = cursorp->close (cursorp);
#else
  ret = cursorp->c_close (cursorp);
#endif
  UNLOCK (&ctx->lock);
 
 return ret;
}


int32_t
bdb_open_db_cursor (bctx_t *bctx,
		    DBC **cursorpp)
{
  int32_t ret = -1;
  
  LOCK (&bctx->lock);
  if (bctx->dbp) {
    /* do nothing, just continue */
    ret = 0;
  } else {
    bctx->dbp = bdb_open_storage_db (bctx);
    if (!bctx->dbp) {
      gf_log ("bdb-ll",
	      GF_LOG_ERROR,
	      "failed to open storage db for %s", bctx->directory);
      ret = -1;
    } else {
      ret = 0;
    }
  }
  
  if (ret == 0) {
    /* all set, lets open cursor */
    ret = bctx->dbp->cursor (bctx->dbp, NULL, cursorpp, 0);
  }
  UNLOCK (&bctx->lock);

  return ret;
}


/* cache related */
static bdb_cache_t *
bdb_lookup_cache (bctx_t *bctx,
		  char *path)
{
  bdb_cache_t *bcache = NULL, *trav = NULL;
  char *key = NULL;

  MAKE_KEY_FROM_PATH (key, path);

  LOCK (&bctx->lock);

  list_for_each_entry (trav, &bctx->c_list, c_list) {
    if (!strcmp (trav->key, key)){
      bcache = trav;
      break;
    }
  }
  UNLOCK (&bctx->lock);

  
  return bcache;
}

static int32_t
bdb_insert_to_cache (bctx_t *bctx, 
		     DBT *key, 
		     DBT *data)
{
  bdb_cache_t *bcache = NULL;

  LOCK (&bctx->lock);

  if (bctx->c_count > 5) {
    /* most of the times, we enter here */
    /* FIXME: ugly, not supposed to disect any of the 'struct list_head' directly */
    if (!list_empty (&bctx->c_list)) {
      bcache = list_entry (bctx->c_list.prev, bdb_cache_t, c_list);
      list_del_init (&bcache->c_list);
    }
    if (bcache->key) {
      free (bcache->key);
      bcache->key = strdup ((char *)key->data);
    } else {
      /* should never come here */
      gf_log ("bdb-ll",
	      GF_LOG_CRITICAL,
	      "bcache->key (null)");
    } /* if(bcache->key)...else */
    if (bcache->data) {
      free (bcache->data);
      bcache->data = memdup (data->data, data->size);
      bcache->size = data->size;
    } else {
      /* should never come here */
      gf_log ("bdb-ll",
	      GF_LOG_CRITICAL,
	      "bcache->data (null)");
    } /* if(bcache->data)...else */
    list_add (&bcache->c_list, &bctx->c_list);
  } else {
    /* we will be entering here very rarely */
    bcache = calloc (1, sizeof (*bcache));    
    ERR_ABORT (bcache);
    bcache->key = strdup ((char *)(key->data));
    bcache->data = memdup (data->data, data->size);
    bcache->size = data->size;
    list_add (&bcache->c_list, &bctx->c_list);
    bctx->c_count++;
  } /* if(private->c_count < 5)...else */
  UNLOCK (&bctx->lock);

  return 0;
}

static int32_t
bdb_delete_from_cache (bctx_t *bctx,
		       char *key)
{
  bdb_cache_t *bcache = NULL, *trav = NULL;

  LOCK (&bctx->lock);

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
  UNLOCK (&bctx->lock);

  return 0;
}



int32_t
bdb_storage_get (bctx_t *bctx,
		 DB_TXN *txnid,
		 const char *path,
		 char **buf,
		 size_t size,
		 off_t offset)
{
  DB *storage = NULL;
  DBT key = {0,}, value = {0,};
  int32_t ret = -1;
  char *key_string = NULL;
  bdb_cache_t *bcache = NULL;
  int32_t db_flags = 0;

  MAKE_KEY_FROM_PATH (key_string, path);
  
  if (bctx->cache && 
      ((bcache = bdb_lookup_cache(bctx, key_string)) != NULL)) {
    if (buf) {
      *buf = calloc (1, bcache->size);
      ERR_ABORT (buf);
      memcpy (*buf, (bcache->data + offset), bcache->size);
    }
    ret = bcache->size;
  } else {
    LOCK (&bctx->lock);
    {
      if (bctx->dbp == NULL) {
	bctx->dbp = bdb_open_storage_db (bctx);
	storage = bctx->dbp;
      } else {
	/* we are just fine, lets continue */
	storage = bctx->dbp;
      } /* if(bctx->dbp==NULL)...else */
    }
    UNLOCK (&bctx->lock);
    if (storage) {
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
      
      if (!txnid && bctx->table->transaction) 
	db_flags = DB_AUTO_COMMIT;
      else 
	db_flags = 0;

      /* TODO: we prefer to give our own buffer to value.data and ask bdb to fill in it */
      ret = storage->get (storage, txnid, &key, &value, db_flags);

      if (ret == DB_NOTFOUND) {
	gf_log ("bdb-ll",
		GF_LOG_DEBUG,
		"failed to do storage->get() for key: %s. key not found in storage DB", key_string);
	ret = -1;
      } else if (ret == 0) {
	/* successfully read data, lets set everything in place and return */
	if (buf) {
	  *buf = calloc (1, value.size);
	  ERR_ABORT (*buf);
	  memcpy (*buf, value.data, value.size);
	}
	ret = value.size;
	if (bctx->cache)
	  bdb_insert_to_cache (bctx, &key, &value);
	free (value.data);
      } else {
	gf_log ("bdb-ll",
		GF_LOG_ERROR,
		"failed to do storage->get() for key %s: %s", key_string, db_strerror (ret));
	ret = -1;
      }
    } else {
      gf_log ("bdb-ll",
	      GF_LOG_DEBUG,
	      "failed to open storage db");
      ret = -1;
    }
  }
  
  return ret;
}/* bdb_storage_get */


int32_t
bdb_storage_put (bctx_t *bctx,
		 DB_TXN *txnid,
		 const char *key_string,
		 const char *buf,
		 size_t size,
		 off_t offset,
		 int32_t flags)
{
  DB *storage = NULL;
  DBT key = {0,}, value = {0,};
  int32_t ret = -1;
  int32_t db_flags = 0;

  LOCK (&bctx->lock);
  {
    if (bctx->dbp == NULL) {
      bctx->dbp = bdb_open_storage_db (bctx);
      storage = bctx->dbp;
    } else {
      /* we are just fine, lets continue */
      storage = bctx->dbp;
    }
  }
  UNLOCK (&bctx->lock);
  
  if (storage) {
    if (bctx->cache)
      bdb_delete_from_cache (bctx, (char *)key_string);

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
    
    if (!txnid && bctx->table->transaction)
      db_flags = DB_AUTO_COMMIT;
    else 
      db_flags = 0;
    ret = storage->put (storage, txnid, &key, &value, db_flags);
    if (ret) {
      /* write failed */
      gf_log ("bdb-ll",
	      GF_LOG_ERROR,
	      "failed to do storage->put() for key %s: %s", key_string, db_strerror (ret));
      ret = -1;
    } else {
      /* successfully wrote */
      ret = 0;
    }
  } else {
    gf_log ("bdb-ll",
	    GF_LOG_ERROR,
	    "failed to open storage db");
    ret = -1;
  }

  return ret;
}/* bdb_cache_storage_put */


int32_t
bdb_storage_del (bctx_t *bctx,
		 DB_TXN *txnid,
		 const char *path)
{
  DB *storage = NULL;
  DBT key = {0,};
  char *key_string = NULL;
  int32_t ret = -1;
  int32_t db_flags = 0;

  MAKE_KEY_FROM_PATH (key_string, path);

  LOCK (&bctx->lock);

  {
    if (bctx->dbp == NULL) {
      bctx->dbp = bdb_open_storage_db (bctx);
      storage = bctx->dbp;
    } else {
      /* we are just fine, lets continue */
      storage = bctx->dbp;
    }
  }

  UNLOCK (&bctx->lock);  
    
  if (storage) {
    bdb_delete_from_cache (bctx, key_string);
    key.data = key_string;
    key.size = strlen (key_string);
    key.flags = DB_DBT_USERMEM;
    
    if (!txnid && bctx->table->transaction)
      db_flags = DB_AUTO_COMMIT;
    else 
      db_flags = 0;

    ret = storage->del (storage, txnid, &key, db_flags);

  
    if (ret == DB_NOTFOUND) {
      gf_log ("bdb-ll",
	      GF_LOG_DEBUG,
	      "failed to delete %s from storage db, doesn't exist in storage DB", path);
    } else if (ret == 0) {
      /* successfully deleted the entry */
      gf_log ("bdb-ll",
	      GF_LOG_DEBUG,
	      "deleted %s from storage db", path);
      ret = 0;
    } else {
      gf_log ("bdb-ll",
	      GF_LOG_ERROR,
	      "failed to delete %s from storage db: %s", path, db_strerror (ret));
      ret = -1;	    
    }
  } else {
    gf_log ("bdb-ll",
	    GF_LOG_CRITICAL,
	    "failed to open db for path: %s", path);
    ret = -1;
  }
	    
  return ret;
}

/* NOTE: bdb version compatibility wrapper */
int32_t
bdb_cursor_get (DBC *cursorp,
		DBT *key,
		DBT *value,
		int32_t flags)
{
#ifdef HAVE_BDB_CURSOR_GET
  return cursorp->get (cursorp, key, value, flags);
#else
  return cursorp->c_get (cursorp, key, value, flags);
#endif
}/* bdb_cursor_get */


int32_t
bdb_dirent_size (DBT *key)
{
  return ALIGN (24 /* FIX MEEEE!!! */ + key->size);
}

int32_t
dirent_size (struct dirent *entry)
{
  return ALIGN (24 /* FIX MEEEE!!! */ + entry->d_reclen);
}

inline void *
bdb_extract_bfd (fd_t *fd,
		 char *name)
{
  data_t *bfd_data = NULL;
  void *bfd = NULL;

  bfd_data = dict_get (fd->ctx, name);
  bfd = data_to_ptr (bfd_data);

  return bfd;
}

static DB_ENV *
bdb_init_db_env (xlator_t *this,
		 char *directory)
{
  /* Create a DB environment */
  DB_ENV *dbenv = NULL;
  int32_t ret = 0;
  bdb_private_t *private = this->private;

  if ((ret = db_env_create (&dbenv, 0)) != 0) {
    gf_log (this->name, GF_LOG_ERROR, 
	    "Failed to create DB environment (%d)", ret);
    dbenv = NULL;
  } else {
    dbenv->set_errpfx(dbenv, this->name);
    
    if ((ret = dbenv->set_lk_detect(dbenv, DB_LOCK_DEFAULT)) != 0) { 
      gf_log (this->name, GF_LOG_ERROR, 
	      "Failed to set Deadlock detection (%s)", db_strerror (ret));
      dbenv = NULL;
    } else {
      if (((ret = dbenv->open(dbenv, directory, private->envflags, 
			      S_IRUSR | S_IWUSR)) != 0) && (ret != DB_RUNRECOVERY)) {
	gf_log (this->name, GF_LOG_ERROR, 
		"Failed to open DB Environment (%s)", db_strerror (ret));
	dbenv = NULL;
      } else if (ret == DB_RUNRECOVERY) {
	int32_t fatal_flags = ((private->envflags & (~DB_RECOVER)) | DB_RECOVER_FATAL);
	if (((ret = dbenv->open(dbenv, directory, fatal_flags, 
				S_IRUSR | S_IWUSR)) != 0)) {
	  gf_log (this->name, GF_LOG_ERROR,
		  "failed to open DB Environment (%s) with DB_REOVER_FATAL",
		  db_strerror (ret));
	  dbenv = NULL;
	} else {
	  gf_log (this->name, GF_LOG_DEBUG,
		  "opened DB Environment after DB_RECOVER_FATAL");
	}
      }

      if (dbenv) {
	ret = dbenv->set_lg_dir (dbenv, private->logdir);
	if (ret != 0) {
	  gf_log ("bctx",
		  GF_LOG_ERROR,
		  "failed to set log directory for dbenv");
	} else {
	  gf_log ("bctx",
		  GF_LOG_DEBUG,
		  "set dbenv log dir to %s", private->logdir);
	}

	ret = dbenv->set_flags (dbenv, DB_LOG_AUTOREMOVE, 1);
	if (ret != 0) {
	  gf_log ("bctx",
		  GF_LOG_ERROR,
		  "failed to set DB_LOG_AUTOREMOVE on dbenv");
	} else {
	  gf_log ("bctx",
		  GF_LOG_DEBUG,
		  "DB_LOG_AUTOREMOVE set on dbenv");
	}
      }
    }
  }
  return dbenv;
}

#define BDB_ENV(this) ((((struct bdb_private *)this->private)->b_table)->dbenv)

static void *
bdb_checkpoint (void *data)
{
  xlator_t *this = data;
  struct bdb_private *private = this->private;
  DB_ENV *dbenv = NULL;

  dbenv = BDB_ENV(this);
  for (;;sleep (private->checkpoint_timeout)) {
    int32_t ret = 0;
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
  }

  return NULL;
}



int
bdb_init_db (xlator_t *this,
	     dict_t *options)
{
  /* create a db entry for root */
  int32_t op_ret = 0;
  bdb_private_t *private = this->private;

  
  {
    data_t *cache = dict_get (options, "cache");
    
    if (cache && !strcmp (cache->data, "on")) {
      gf_log (this->name,
	      GF_LOG_DEBUG,
	      "bdb cache turned on");
      private->cache = ON;
    } else {
      private->cache = OFF;
    }
  }
  {
    data_t *access_mode = dict_get (options, "access-mode");
    
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
  {
    data_t *transaction = dict_get (options, "transaction");
    
    if (transaction && !strcmp (transaction->data, "off")) {
      gf_log (this->name,
	      GF_LOG_DEBUG,
	      "transaction turned off");
      private->envflags = DB_CREATE | DB_INIT_LOG | 
	DB_INIT_MPOOL | DB_THREAD;
      private->dbflags = DB_CREATE | DB_THREAD;
      private->transaction = OFF;
    } else {
      gf_log (this->name,
	      GF_LOG_DEBUG,
	      "transaction turned on");
      private->transaction = ON;
      private->envflags = DB_CREATE | DB_INIT_LOCK | DB_INIT_LOG | 
	DB_INIT_MPOOL | DB_INIT_TXN | DB_RECOVER | DB_THREAD;
      private->dbflags = DB_CREATE | DB_AUTO_COMMIT | DB_THREAD;
    }
  }
  {
    data_t *inode_bit_shift = dict_get (options, "inode-bit-shift");
    
    if (inode_bit_shift) {
      private->inode_bit_shift = strtol (inode_bit_shift->data, NULL, 0);
      
      if (private->inode_bit_shift > 48 || private->inode_bit_shift < 8) {
	gf_log (this->name,
		GF_LOG_DEBUG,
		"inode-bit-shift %d, out of range. setting to default 18",
		private->inode_bit_shift);
	private->inode_bit_shift = 18;
      } else {
	gf_log (this->name,
		GF_LOG_DEBUG,
		"setting inode bit shift to %d", private->inode_bit_shift);
      }
    } else {
      private->inode_bit_shift = 18;
    }
  }

  {
    LOCK_INIT (&private->ino_lock);
    private->next_ino = 2;
  }

  {
    data_t *checkpoint_timeout = dict_get (options, "checkpoint-timeout");
    
    private->checkpoint_timeout = BDB_DEFAULT_CHECKPOINT_TIMEOUT;

    if (checkpoint_timeout) {
      private->checkpoint_timeout = strtol (checkpoint_timeout->data, NULL, 0);
      
      if (private->checkpoint_timeout < 5 || private->checkpoint_timeout > 60) {
	gf_log (this->name,
		GF_LOG_WARNING,
		"checkpoint-timeout %d seconds too %s", private->checkpoint_timeout, 
		(private->checkpoint_timeout < 5)?"low":"high");
      } else {
	gf_log (this->name,
		GF_LOG_DEBUG,
		"setting checkpoint-timeout to %d seconds", private->checkpoint_timeout);
      }
    } else {
      gf_log (this->name,
	      GF_LOG_DEBUG,
	      "setting checkpoint-timeout to default: %d seconds", private->checkpoint_timeout);
    }
  }

  {
    data_t *file_mode = dict_get (options, "file-mode");
    char *endptr = NULL;

    if (file_mode) {
      private->file_mode = strtol (file_mode->data, &endptr, 8);

      if ((*endptr) || 
	  (!IS_VALID_FILE_MODE(private->file_mode))) {
	gf_log (this->name,
		GF_LOG_DEBUG,
		"invalid file-mode %o. setting to default %o", private->file_mode, 
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

  {
    data_t *dir_mode = dict_get (options, "dir-mode");
    char *endptr = NULL;
    if (dir_mode) {
      private->dir_mode = strtol (dir_mode->data, &endptr, 8);
      if ((*endptr) || 
	  (!IS_VALID_FILE_MODE(private->dir_mode))) {
	gf_log (this->name,
		GF_LOG_DEBUG,
		"invalid dir-mode %o. setting to default %o", private->dir_mode, 
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

  {
    bctx_table_t *table = calloc (1, sizeof (*table));
    int32_t i = 0;
    
    if (table) {
      INIT_LIST_HEAD(&(table->b_lru));
      INIT_LIST_HEAD(&(table->active));
      INIT_LIST_HEAD(&(table->purge));

      LOCK_INIT (&table->lock);
      LOCK_INIT (&table->checkpoint_lock);
      
      table->transaction = private->transaction;
      table->access_mode = private->access_mode;
      table->dbflags = private->dbflags;
      table->this  = this;
      
      {
	data_t *lru_limit = dict_get (options, "lru-limit");
	
	if (lru_limit) {
	  table->lru_limit = strtol (lru_limit->data, NULL, 0);
	  gf_log (this->name,
		  GF_LOG_DEBUG,
		  "setting bctx lru limit to %d", table->lru_limit);
	} else {
	  table->lru_limit = BDB_DEFAULT_LRU_LIMIT;
	}
      }

      table->hash_size = BDB_DEFAULT_HASH_SIZE;
      table->b_hash = calloc (BDB_DEFAULT_HASH_SIZE, sizeof (struct list_head));

      for (i = 0; i < table->hash_size; i++)
	INIT_LIST_HEAD(&(table->b_hash[i]));

      private->b_table = table;
    } else {
      gf_log (this->name,
	      GF_LOG_CRITICAL,
	      "failed to allocate bctx table: out of memory");
      return -1;
    }
  }

  {
    data_t *directory = dict_get (options, "directory");
    
    if (directory) {
      {
	data_t *logdir = dict_get (options, "logdir");
	
	if (logdir) {
	  private->logdir = strdup (logdir->data);
	  gf_log (this->name,
		  GF_LOG_DEBUG,
		  "using logdir: %s", private->logdir);
	} else {
	  gf_log (this->name,
		  GF_LOG_DEBUG,
		  "using default logdir as database home");
	  private->logdir = strdup (directory->data);
	}
      }

      private->b_table->dbenv = bdb_init_db_env (this, directory->data);
      
      if (!private->b_table->dbenv) {
	gf_log (this->name, GF_LOG_ERROR,
		"failed to initialize db environment");
	FREE (private);
	op_ret = -1;
      } else {
	if (private->transaction) {
	  /* all well, start the checkpointing thread */
	  pthread_create (&private->checkpoint_thread, NULL,
			  bdb_checkpoint, this);
	}
      }
    } else {
      /* this point will never be reached */
    }
  }

  
  return op_ret;
}
