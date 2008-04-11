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
  ino = (only32 & ((parent << 16) | bctx->iseed));
  bctx->iseed++;
  UNLOCK (&bctx->lock);
  return ino;
}

struct bdb_ctx *
bdb_ctx_unref (struct bdb_ctx *ctx)
{
  LOCK (&ctx->lock);
  ctx->ref--;
  if (!ctx->ref) {
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
    op_ret = storage_dbp->set_flags (storage_dbp, DB_RECNUM);
    op_ret = storage_dbp->open (storage_dbp,
				NULL,
				storage_db_path,
				NULL,
				DB_BTREE,
				/*DB_AUTO_COMMIT | */DB_CREATE,
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
  list_for_each_entry (trav, &private->b_hash[key_hash], b_hash) {
    if (!strcmp(trav->directory, path)) {
      bctx = trav;
      break;
    }
  }

  return bctx;
}

int32_t
bdb_add_ctx (xlator_t *this, 
	     struct bdb_ctx *bctx)
{
  struct bdb_private *private = this->private;
  char *key = NULL;
  uint32_t key_hash = 0;
  
  MAKE_KEY_FROM_PATH (key, bctx->directory);
  key_hash = bdb_key_hash (key);
  INIT_LIST_HEAD (&bctx->lru);
  
  LOCK (&bctx->lock);
  list_add (&bctx->b_hash, &private->b_hash[key_hash]);
  UNLOCK (&bctx->lock);

  return 0;
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
  struct bdb_private *private = this->private;
  
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
    /* push the bctx to end of lru, so that pruning doesn't chop it off */
    list_move_tail (&bctx->lru, &private->b_lru);
    list_del_init (&bctx->lru);
    /* all set, lets open cursor */
    ret = bctx->dbp->cursor (bctx->dbp, NULL, cursorpp, 0);
  }
  UNLOCK (&bctx->lock);

  return ret;
}
int32_t
bdb_remove_ctx (xlator_t *this, 
		struct bdb_ctx *bctx)
{
  list_del (&bctx->b_hash);

  return 0;
}

/* cache related */
struct bdb_cache *
bdb_lookup_cache (xlator_t *this, 
		  char *path)
{
  struct bdb_private *private = this->private;
  struct bdb_cache *bcache = NULL, *trav = NULL;
  char *key = NULL;

  MAKE_KEY_FROM_PATH (key, path);

  list_for_each_entry (trav, &private->c_list, c_list) {
    if (!strcmp (trav->key, key)){
      bcache = trav;
      break;
    }
  }
  
  return bcache;
}

static int32_t
bdb_insert_to_cache (xlator_t *this, 
		     DBT *key, 
		     DBT *data)
{
  struct bdb_private *private = this->private;
  struct bdb_cache *bcache = NULL;

  if (private->cache_full == 5) {
    /* most of the times, we enter here */

    /* FIXME: ugly, not supposed to disect any of the 'struct list_head' directly */
    bcache = list_entry (private->c_list.prev, struct bdb_cache, c_list);
    list_del_init (&bcache->c_list);
    if (bcache->key) {
      free (bcache->key);
      bcache->key = strdup ((char *)key->data);
    }
    if (bcache->data) {
      free (bcache->data);
      bcache->data = (char *)data->data;
      bcache->size = data->size;
    }
    list_add (&bcache->c_list, &private->c_list);
  } else {
    /* we will be entering here very rarely */
    bcache = calloc (1, sizeof (*bcache));    
    bcache->key = strdup ((char *)(key->data));
    bcache->data = (char *)(data->data);
    bcache->size = data->size;
    list_add (&bcache->c_list, &private->c_list);
    private->cache_full++;
  }
  return 0;
}

/* int bdb_storage_get (DB *storage, const char *key, char **buf, off_t offset)*/
int32_t
bdb_storage_get (xlator_t *this,
		 struct bdb_ctx *bctx,
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

  MAKE_KEY_FROM_PATH (key_string, path);
  
  LOCK (&bctx->lock);
  if ((bcache = bdb_lookup_cache(this, key_string)) != NULL) {
    if (buf)
      *buf = bcache->data;
    ret = bcache->size;
  } else {
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
      key.data = (char *)key_string;
      key.size = strlen (key_string);
      key.flags = DB_DBT_USERMEM;
      
      /* we are called to return the size of the file */
      value.flags = DB_DBT_MALLOC;
      /* TODO: we prefer to give our own buffer to value.data and ask bdb to fill in it */
      ret = storage->get (storage, NULL, &key, &value, 0);
      if (ret == DB_NOTFOUND) {
	gf_log (this->name,
		GF_LOG_DEBUG,
		"failed to do storage->get() for key: %s. key not found in storage DB", key_string);
	ret = -1;
      } else if (ret == 0) {
	/* successfully read data, lets set everything in place and return */
	if (buf)
	  *buf = value.data;
	ret = value.size;
	bdb_insert_to_cache (this, &key, &value);
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

/* bdb_storage_put (DB *storage, const char *path, const char *buf, size_t size, off_t offset, int32_t flags) */
int32_t
bdb_storage_put (xlator_t *this,
		 struct bdb_ctx *bctx,
		 const char *key_string,
		 const char *buf,
		 size_t size,
		 off_t offset,
		 int32_t flags)
{
  DB *storage = NULL;
  DBT key = {0,}, value = {0,};
  int32_t ret = -1;

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
    
    ret = storage->put (storage, NULL, &key, &value, 0);
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
}/* bdb_storage_put */

int32_t
bdb_storage_del (xlator_t *this,
		 struct bdb_ctx *bctx,
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

  key.data = key_string;
  key.size = strlen (key_string);
  key.flags = DB_DBT_USERMEM;

  ret = storage->del (storage, NULL, &key, 0);
  
  if (ret == DB_NOTFOUND) {
    /* key not found in database. this may happen when a file is created and not written to. */
    /* do nothing */
    gf_log (this->name,
	    GF_LOG_DEBUG,
	    "failed to delete %s from storage db, doesn't exist in storage DB", path);
    ret = 0;
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

int
bdb_init_db (xlator_t *this,
	     char *directory)
{
  /* create a db entry for root */
  int32_t op_ret = 0;
  struct bdb_private *private = this->private;

  {
    int32_t i = 0;
    for (i = 0; i < BDB_HASH_SIZE; i++)
      INIT_LIST_HEAD(&(private->b_hash[i]));
    
    INIT_LIST_HEAD(&(private->b_lru));
  }
  
  return op_ret;
}

DB_ENV *
bdb_init_db_env (xlator_t *this,
		 char *directory)
{
  /* Create a DB environment */
  DB_ENV *dbenv = NULL;
  int32_t ret = 0;

  if ((ret = db_env_create (&dbenv, 0)) != 0) {
    gf_log (this->name, GF_LOG_ERROR, 
	    "Failed to create DB environment (%d)", ret);
    dbenv = NULL;
  } else {
    dbenv->set_errpfx(dbenv, this->name);
    
    if ((ret = dbenv->set_lk_detect(dbenv, DB_LOCK_DEFAULT)) != 0) { 
      gf_log (this->name, GF_LOG_ERROR, 
	      "Failed to set Deadlock detection (%d)", ret);
      dbenv = NULL;
    } else {
      if ((ret = dbenv->open(dbenv, directory, DB_CREATE | 
			     DB_INIT_LOG | DB_INIT_MPOOL | 
			     DB_THREAD, 
			     S_IRUSR | S_IWUSR)) != 0) {
	gf_log (this->name, GF_LOG_ERROR, 
		"Failed to open DB Environment (%d)", ret);
	dbenv = NULL;
      }
    }
  }
  
  return dbenv;
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
