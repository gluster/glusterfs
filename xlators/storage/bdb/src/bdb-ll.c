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
		     struct bdb_ctx *bctx)
{
  ino_t ino = 0;
  uint64_t only32 = 0x00000000ffffffff;
  LOCK (&bctx->lock);
  ino = only32 & ((parent << 8) | bctx->iseed);
  bctx->iseed++;
  UNLOCK (&bctx->lock);
  return ino;
}

int32_t
bdb_ctx_deactivate (xlator_t *this,
		    struct bdb_ctx *ctx)
{
  struct bdb_private *private = this->private;

  LOCK(&ctx->lock);
  {
    list_del_init (&ctx->b_hash);
    list_add_tail (&ctx->lru, &private->b_lru);
  }
  UNLOCK(&ctx->lock);
  
  return 0;
}

struct bdb_ctx *
bdb_ctx_unref (struct bdb_ctx *ctx)
{
  LOCK (&ctx->lock);
  ctx->ref--;
  if (!ctx->ref) {
    list_del_init (&ctx->b_hash);
    list_del_init (&ctx->lru);

    /* time to close dbs */
    if (ctx->dbp) {
      ctx->dbp->close (ctx->dbp, 0);
      ctx->dbp = NULL;
    } else {
      /* do nothing */
    }
    
    if (ctx->directory) {
      free (ctx->directory);
      ctx->directory = NULL;
    } else {
      /* do nothing */
    }
  } else {
    /* do nothing */
  }

  UNLOCK (&ctx->lock);

  return ctx;
}

struct bdb_ctx *
bdb_ctx_ref (struct bdb_ctx *ctx)
{
  LOCK (&ctx->lock);
  ctx->ref++;
  UNLOCK (&ctx->lock);
  return ctx;
}

/* struct bdb_ctx related */
static uint32_t
bdb_key_hash (char *key)
{
  uint32_t hash = *key;
  
  if (hash) {
    for (key += 1; *key != '\0'; key++) {
      hash = (hash << 5) - hash + *key;
    }
  }
  
  return (hash + *key) % BDB_HASH_SIZE;
}

static int32_t
bdb_add_ctx (xlator_t *this, 
	     struct bdb_ctx *bctx)
{
  struct bdb_private *private = this->private;
  char *key = NULL;
  uint32_t key_hash = 0;
  
  MAKE_KEY_FROM_PATH (key, bctx->directory);
  key_hash = bdb_key_hash (key);
  INIT_LIST_HEAD (&bctx->lru);
  INIT_LIST_HEAD (&bctx->b_hash);
  
  if (list_empty(&bctx->b_hash)) {
    LOCK (&bctx->lock);
    list_add (&bctx->b_hash, &private->b_hash[key_hash]);
    UNLOCK (&bctx->lock);
  }

  return 0;
}

static inline struct bdb_ctx *
bdb_get_new_ctx (xlator_t *this,
		 const char *path)
{
  struct bdb_ctx *bctx = NULL;
  
  bctx = calloc (1, sizeof (*bctx));
  ERR_ABORT (bctx);
  if (bctx) {
    bctx->directory = strdup (path);
    bctx->iseed = 1;
    INIT_LIST_HEAD (&bctx->c_list);
    LOCK_INIT (&bctx->lock);
    bdb_ctx_ref (bctx);
    bdb_add_ctx (this, bctx);
  }
  
  return bctx;
}

/* 
 * @path is always a directory
 */
struct bdb_ctx *
bdb_lookup_ctx (xlator_t *this, 
		char *path)
{
  struct bdb_private *private = this->private;
  char *key = NULL;
  uint32_t key_hash = 0;
  struct bdb_ctx *trav = NULL, *bctx = NULL;

  MAKE_KEY_FROM_PATH (key, path);
  key_hash = bdb_key_hash (key);
  if (!list_empty (&private->b_hash[key_hash])) {
    list_for_each_entry (trav, &private->b_hash[key_hash], b_hash) {
      if (!strcmp(trav->directory, path)) {
	bctx = trav;
	break;
      } 
    }
  }
  
  if (!bctx) {
    bctx = bdb_get_new_ctx (this, path);
  }

  return bctx;
}


struct bdb_ctx *
bdb_get_bctx_from (xlator_t *this,
		   const char *path)
{
  char *pathname = strdup (path), *dir_name = NULL;
  struct bdb_ctx *bctx = NULL;

  dir_name = dirname (pathname);

  if ((bctx = bdb_lookup_ctx (this, dir_name)) == NULL) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "failed to get bctx from private: %p for dirname: %s", this->private, dir_name);
    bctx = NULL;
  } else {
    /* do nothing */
  }
  
  free (pathname);
  return bctx;
}


/***********************************************************
 *
 *  bdb storage database utilities
 *
 **********************************************************/
static int32_t
bdb_db_slots_free (xlator_t *this)
{
  struct bdb_private *private = this->private;

  return (private->open_dbs == BDB_MAX_OPEN_DBS)? 0 : 1;
}

static int32_t
bdb_db_slots_prune (xlator_t *this)
{
  struct bdb_private *private = this->private;
  struct bdb_ctx *trav = NULL, *tmp = NULL;
  DB *storage = NULL;

  list_for_each_entry_safe (trav, tmp, &private->b_lru, lru) {
    
    LOCK (&trav->lock);
    storage = trav->dbp;
    trav->dbp = NULL;
    list_del_init (&trav->lru);
    UNLOCK (&trav->lock);

    if (storage) {
      storage->close (storage, 0);
      storage = NULL;
      private->open_dbs--;
    }
    
    if (private->open_dbs == (BDB_MAX_OPEN_DBS/4))
      break;
  }
  
  return 0;
}


/*
 * bdb_open_storage_db - opens a storage db.
 *
 * @this: xlator_t 
 * @ctx: context specific to the directory for which we are supposed to open db
 *
 * see, if we have empty slots to open a db.
 *      if (no-empty-slots), then prune open dbs and close as many as possible
 *      if (empty-slot-available), tika muchkonDu db open maaDu
 *
 * NOTE: illli baro munche lock hiDkobEku
 */
static DB *
bdb_open_storage_db (xlator_t *this,
		     struct bdb_ctx *ctx)
{
  char *dir_name = ctx->directory;
  char *storage_db_path = NULL;
  DB *storage_dbp = NULL;
  int32_t op_ret = -1, op_errno = ENOENT;
  struct bdb_private *private = this->private;

  if (bdb_db_slots_free (this)) {
    /* we are free to go, do nothing */
  } else {
    /* prune as much as possible */
    bdb_db_slots_prune (this);
  }

  /* we have to do the following, we can't deny someone of db_open ;) */
  MAKE_REAL_PATH_TO_STORAGE_DB (storage_db_path, this, dir_name);  
  
  op_ret = db_create (&storage_dbp, private->dbenv, 0);
  if (op_ret != 0) {
    op_ret = -1;
    op_errno = ENOENT; /* DB failure, find appropriate errno */
    storage_dbp = NULL;
  } else {
    op_ret = storage_dbp->open (storage_dbp,
				NULL,
				storage_db_path,
				NULL,
				private->access_mode,
				private->dbflags,
				0);
    if (op_ret != 0 ) {
      gf_log (this->name,
	      GF_LOG_ERROR,
	      "failed to open storage-db %s: %s", storage_db_path, db_strerror (op_ret));
      op_ret = -1;
      op_errno = ENOENT; /* DB failure, find appropriate errno */
      storage_dbp = NULL;
    } else {
      list_add_tail (&ctx->lru, &private->b_lru);
      private->open_dbs++;
    }
  }

  return storage_dbp;
}



int32_t
bdb_close_db_cursor (xlator_t *this, 
		     struct bdb_ctx *ctx,
		     DBC *cursorp)
{
  struct bdb_private *private = this->private;
  int32_t ret = 0;

  LOCK (&ctx->lock);
#ifdef HAVE_BDB_CURSOR_GET
  ret = cursorp->close (cursorp);
#else
  ret = cursorp->c_close (cursorp);
#endif
  list_add_tail (&ctx->lru, &private->b_lru);
  UNLOCK (&ctx->lock);
 
 return ret;
}


int32_t
bdb_open_db_cursor (xlator_t *this,
		    struct bdb_ctx *bctx,
		    DBC **cursorpp)
{
  int32_t ret = -1;
  
  LOCK (&bctx->lock);
  if (bctx->dbp) {
    /* do nothing, just continue */
    ret = 0;
  } else {
    bctx->dbp = bdb_open_storage_db (this, bctx);
    if (!bctx->dbp) {
      gf_log (this->name,
	      GF_LOG_ERROR,
	      "failed to open storage db for %s", bctx->directory);
      ret = -1;
    } else {
      ret = 0;
    }
  }
  
  if (ret == 0) {
    list_del_init (&bctx->lru);
    /* all set, lets open cursor */
    ret = bctx->dbp->cursor (bctx->dbp, NULL, cursorpp, 0);
  }
  UNLOCK (&bctx->lock);

  return ret;
}


/* cache related */
struct bdb_cache *
bdb_lookup_cache (xlator_t *this, 
		  struct bdb_ctx *bctx,
		  char *path)
{
  struct bdb_cache *bcache = NULL, *trav = NULL;
  char *key = NULL;

  MAKE_KEY_FROM_PATH (key, path);

  list_for_each_entry (trav, &bctx->c_list, c_list) {
    if (!strcmp (trav->key, key)){
      bcache = trav;
      break;
    }
  }
  
  return bcache;
}

static int32_t
bdb_insert_to_cache (xlator_t *this,
		     struct bdb_ctx *bctx, 
		     DBT *key, 
		     DBT *data)
{
  struct bdb_cache *bcache = NULL;

  if (bctx->c_count > 5) {
    /* most of the times, we enter here */
    /* FIXME: ugly, not supposed to disect any of the 'struct list_head' directly */
    if (!list_empty (&bctx->c_list)) {
      bcache = list_entry (bctx->c_list.prev, struct bdb_cache, c_list);
      list_del_init (&bcache->c_list);
    }
    if (bcache->key) {
      free (bcache->key);
      bcache->key = strdup ((char *)key->data);
    } else {
      /* should never come here */
      gf_log (this->name,
	      GF_LOG_CRITICAL,
	      "bcache->key (null)");
    } /* if(bcache->key)...else */
    if (bcache->data) {
      free (bcache->data);
      bcache->data = memdup (data->data, data->size);
      bcache->size = data->size;
    } else {
      /* should never come here */
      gf_log (this->name,
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
  return 0;
}

static int32_t
bdb_delete_from_cache (xlator_t *this,
		       struct bdb_ctx *bctx,
		       char *key)
{
  struct bdb_cache *bcache = NULL, *trav = NULL;

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
  return 0;
}



int32_t
bdb_storage_get (xlator_t *this,
		 struct bdb_ctx *bctx,
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
  struct bdb_cache *bcache = NULL;
  struct bdb_private *private = this->private;

  MAKE_KEY_FROM_PATH (key_string, path);
  
  LOCK (&bctx->lock);
  if (private->cache && 
      ((bcache = bdb_lookup_cache(this, bctx, key_string)) != NULL)) {
    if (buf) {
      *buf = calloc (1, bcache->size);
      ERR_ABORT (buf);
      memcpy (*buf, (bcache->data + offset), bcache->size);
    }
    ret = bcache->size;
  } else {
    {
      if (bctx->dbp == NULL) {
	bctx->dbp = bdb_open_storage_db (this, bctx);
	storage = bctx->dbp;
      } else {
	/* we are just fine, lets continue */
	storage = bctx->dbp;
      } /* if(bctx->dbp==NULL)...else */
    }
    
    if (storage) {
      key.data = (char *)key_string;
      key.size = strlen (key_string);
      key.flags = DB_DBT_USERMEM;
      
      if (private->cache){
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
      
      /* TODO: we prefer to give our own buffer to value.data and ask bdb to fill in it */
      ret = storage->get (storage, txnid, &key, &value, 0);

      if (ret == DB_NOTFOUND) {
	gf_log (this->name,
		GF_LOG_DEBUG,
		"failed to do storage->get() for key: %s. key not found in storage DB", key_string);
	ret = -1;
      } else if (ret == 0) {
	/* successfully read data, lets set everything in place and return */
	if (buf) {
	  *buf = calloc (1, value.size);
	  ERR_ABORT (buf);
	  memcpy (*buf, value.data, value.size);
	}
	ret = value.size;
	if (private->cache)
	  bdb_insert_to_cache (this, bctx, &key, &value);
	free (value.data);
      } else {
	gf_log (this->name,
		GF_LOG_ERROR,
		"failed to do storage->get() for key %s: %s", key_string, db_strerror (ret));
	ret = -1;
      }
    } else {
      gf_log (this->name,
	      GF_LOG_DEBUG,
	      "failed to open storage db");
      ret = -1;
    }
  }
  UNLOCK (&bctx->lock);
  
  return ret;
}/* bdb_storage_get */


int32_t
bdb_storage_put (xlator_t *this,
		 struct bdb_ctx *bctx,
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
  struct bdb_private *private = this->private;

  LOCK (&bctx->lock);
  {
    if (bctx->dbp == NULL) {
      bctx->dbp = bdb_open_storage_db (this, bctx);
      storage = bctx->dbp;
    } else {
      /* we are just fine, lets continue */
      storage = bctx->dbp;
    }
  }
  
  if (storage) {
    if (private->cache)
      bdb_delete_from_cache (this, bctx, (char *)key_string);

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
    if (buf == NULL && size == 0 && offset == 1) 
      /* truncate called us */
      value.flags = 0;
    ret = storage->put (storage, txnid, &key, &value, 0);
    if (ret) {
      /* write failed */
      gf_log (this->name,
	      GF_LOG_ERROR,
	      "failed to do storage->put() for key %s: %s", key_string, db_strerror (ret));
      ret = -1;
    } else {
      /* successfully wrote */
      ret = 0;
    }
  } else {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "failed to open storage db");
    ret = -1;
  }
  UNLOCK (&bctx->lock);

  return ret;
}/* bdb_cache_storage_put */


int32_t
bdb_storage_del (xlator_t *this,
		 struct bdb_ctx *bctx,
		 DB_TXN *txnid,
		 const char *path)
{
  DB *storage = NULL;
  DBT key = {0,};
  char *key_string = NULL;
  int32_t ret = -1;

  MAKE_KEY_FROM_PATH (key_string, path);

  LOCK (&bctx->lock);

  {
    if (bctx->dbp == NULL) {
      bctx->dbp = bdb_open_storage_db (this, bctx);
      storage = bctx->dbp;
    } else {
      /* we are just fine, lets continue */
      storage = bctx->dbp;
    }
  }
  
  if (storage) {
    bdb_delete_from_cache (this, bctx, key_string);
    key.data = key_string;
    key.size = strlen (key_string);
    key.flags = DB_DBT_USERMEM;
    
    ret = storage->del (storage, txnid, &key, 0);

  
    if (ret == DB_NOTFOUND) {
      gf_log (this->name,
	      GF_LOG_DEBUG,
	      "failed to delete %s from storage db, doesn't exist in storage DB", path);
    } else if (ret == 0) {
      /* successfully deleted the entry */
      gf_log (this->name,
	      GF_LOG_DEBUG,
	      "deleted %s from storage db", path);
      ret = 0;
    } else {
      gf_log (this->name,
	      GF_LOG_ERROR,
	      "failed to delete %s from storage db: %s", path, db_strerror (ret));
      ret = -1;	    
    }
  } else {
    gf_log (this->name,
	    GF_LOG_CRITICAL,
	    "failed to open db for path: %s", path);
    ret = -1;
  }
	    
  UNLOCK (&bctx->lock);  

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
bdb_extract_bfd (xlator_t *this,
		 fd_t *fd)
{
  data_t *bfd_data = NULL;
  void *bfd = NULL;

  bfd_data = dict_get (fd->ctx, this->name);
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
  struct bdb_private *private = this->private;

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
	gf_log (this->name, GF_LOG_ERROR,
		"failed to open DB Environment (%s) with recovery",
		db_strerror (ret));
      }
    }
  }
  return dbenv;
}


int
bdb_init_db (xlator_t *this,
	     dict_t *options)
{
  /* create a db entry for root */
  int32_t op_ret = 0;
  struct bdb_private *private = this->private;

  
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
    
    if (transaction && !strcmp (transaction->data, "on")) {
      gf_log (this->name,
	      GF_LOG_DEBUG,
	      "transaction turned on");
      private->transaction = ON;
      private->envflags = DB_CREATE | DB_INIT_LOCK | DB_INIT_LOG | 
	DB_INIT_MPOOL | DB_INIT_TXN | DB_RECOVER | DB_THREAD;
      private->dbflags = DB_CREATE | DB_AUTO_COMMIT | DB_THREAD;
    } else {
      private->envflags = DB_CREATE | DB_INIT_LOG | 
	DB_INIT_MPOOL | DB_THREAD;
      private->dbflags = DB_CREATE | DB_THREAD;
      private->transaction = OFF;
    }
  }
  {
    data_t *inode_bit_shift = dict_get (options, "inode-bit-shift");
    
    if (inode_bit_shift) {
      private->inode_bit_shift = strtol (inode_bit_shift->data, NULL, 0);
      
      if (private->inode_bit_shift > 48 || private->inode_bit_shift < 8) {
	gf_log (this->name,
		GF_LOG_DEBUG,
		"inode-bit-shift %d, out of range. setting to default 24", private->inode_bit_shift);
	private->inode_bit_shift = 24;
      } else {
	gf_log (this->name,
		GF_LOG_DEBUG,
		"setting inode bit shift to %d", private->inode_bit_shift);
      }
    } else {
      private->inode_bit_shift = 24;
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
    int32_t i = 0;
    for (i = 0; i < BDB_HASH_SIZE; i++)
      INIT_LIST_HEAD(&(private->b_hash[i]));
    
    INIT_LIST_HEAD(&(private->b_lru));
  }

  {
    data_t *directory = dict_get (options, "directory");
    
    if (directory) {
      private->dbenv = bdb_init_db_env (this, directory->data);
      
      if (!private->dbenv) {
	gf_log (this->name, GF_LOG_ERROR,
		"failed to initialize db environment");
	FREE (private);
	return -1;
      } else {
	/* all well */
      }
    } else {
      /* this point will never be reached */
    }
  }

  
  return op_ret;
}


void
bdb_close_dbs_from_dict (dict_t *this,
			 char *key,
			 data_t *value,
			 void *data)
{
  struct bdb_ctx *bctx = data_to_ptr (value);
  
  if (bctx) {
    LOCK (&bctx->lock);
    /* cleanup, by closing all the ns and storage dbs */
    if (bctx->dbp) {
      bctx->dbp->sync (bctx->dbp, 0);
      bctx->dbp->close (bctx->dbp, 0);
    } else {
      /* do nothing */
    }
    UNLOCK (&bctx->lock);
  } else {
    gf_log ("bdb",
	    GF_LOG_ERROR,
	    "unwanted entry in dictionary");
  }
}
