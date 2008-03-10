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

/* TODO: first iteration of testing:
 *      1. test lookup, opendir, readdir, closedir, mkdir calls.
 *      2. add namespace to database to directories.
 */

/* bdb based storage translator - named as 'bdb' translator
 * 
 * 
 * There can be only two modes for files existing on bdb translator:
 * 1. DIRECTORY - directories are stored by bdb as regular directories on background file-system. 
 *                directories also have an entry in the ns_db.db of their parent directory.
 * 2. REGULAR FILE - regular files are stored as records in the storage_db.db present in the directory.
 *                   regular files also have an entry in ns_db.db
 *
 * Internally bdb has a maximum of three different types of logical files associated with each directory:
 * 1. ns_db.db - nameserver database, used to serve the purpose of containing the namespace entries, 
 *              corresponsiding to the contents of a directory.
 * 2. storage_db.db - storage database, used to store the data corresponding to regular files in the
 *                   form of key/value pair. file-name is the 'key' and data is 'value'.
 * 3. directory (all subdirectories) - any subdirectory will have a regular directory entry.
 */
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#define __XOPEN_SOURCE 500

#include <stdint.h>
#include <sys/time.h>
#include <errno.h>
#include <ftw.h>
#include <libgen.h>

#include "glusterfs.h"
#include "dict.h"
#include "logging.h"
#include "bdb.h"
#include "xlator.h"
#include "lock.h"
#include "defaults.h"
#include "common-utils.h"

#define GLFS_BDB_NS         "/glusterfs_ns.db"
#define GLFS_BDB_STORAGE    "/glusterfs_storage.db"

#define MAKE_REAL_PATH(var, this, path) do {                              \
  int base_len = ((struct bdb_private *)this->private)->base_path_length; \
  var = alloca (strlen (path) + base_len + 2);                            \
  strcpy (var, ((struct bdb_private *)this->private)->base_path);         \
  strcpy (&var[base_len], path);                                          \
} while (0)


#define MAKE_REAL_PATH_TO_NS_DB(var, this, path) do {                     \
  int base_len = ((struct bdb_private *)this->private)->base_path_length; \
  var = alloca (strlen (path) + base_len + strlen (GLFS_BDB_NS));         \
  strcpy (var, ((struct bdb_private *)this->private)->base_path);         \
  strcpy (&var[base_len], path);                                          \
  strcat (var, GLFS_BDB_NS);                                              \
} while (0)


#define MAKE_REAL_PATH_TO_STORAGE_DB(var, this, path) do {                \
  int base_len = ((struct bdb_private *)this->private)->base_path_length; \
  var = alloca (strlen (path) + base_len + strlen (GLFS_BDB_STORAGE));    \
  strcpy (var, ((struct bdb_private *)this->private)->base_path);         \
  strcpy (&var[base_len], path);                                          \
  strcat (var, GLFS_BDB_STORAGE);                                         \
} while (0)

static void 
bdb_print_all_records (xlator_t *this, DB *dbp)
{
  DBC *cursorp = NULL;
  DBT value = {0,}, key = {0,};
  int32_t ret = 0;

  dbp->cursor (dbp, NULL, &cursorp, 0);
  
  while (ret == 0) {
    if (cursorp) {
      ret = cursorp->get (cursorp, &key, &value, DB_NEXT);
      if (!ret) {
	char *string = calloc (1, key.size);
	strncpy (string, (char *)key.data, key.size);
	gf_log (this->name,
		GF_LOG_DEBUG,
		"successfully read key: %s, size: %d", string, key.size);
      } else {
      gf_log (this->name,
	      GF_LOG_ERROR,
	      "cursorp->get failed with DB_SET");
    }
  }
  }

  if (cursorp)
    cursorp->close (cursorp);
}

static DB *
bdb_open_ns_db (xlator_t *this,
		const char *dir_name)
{
  char *ns_db_path = NULL;
  DB *ns_dbp = NULL;
  int32_t op_ret = -1, op_errno = ENOENT;
  struct bdb_private *private = this->private;

  MAKE_REAL_PATH_TO_NS_DB (ns_db_path, this, dir_name);  
  
  op_ret = db_create (&ns_dbp, private->dbenv, 0);
  if (op_ret != 0) {
    op_ret = -1;
    op_errno = ENOENT; /* DB failure, find appropriate errno */
    ns_dbp = NULL;
  } else {
    op_ret = ns_dbp->set_flags (ns_dbp, DB_RECNUM);
    op_ret = ns_dbp->open (ns_dbp,
			   NULL,
			   ns_db_path,
			   NULL,
			   DB_BTREE,
			   /*DB_AUTO_COMMIT |*/ DB_CREATE,
			   0);
    if (op_ret != 0 ) {
      ns_dbp->err (ns_dbp, op_ret, "failed to open ns-db");
      op_ret = -1;
      op_errno = ENOENT; /* DB failure, find appropriate errno */
      ns_dbp = NULL;
    } 
  }
  
  return ns_dbp;
}

static inline ino_t
bdb_inode_transform (ino_t parent,
		     db_recno_t recno)
{
  uint64_t only32 = 0x00000000ffffffff;
  return (only32 & ((parent << 16) | recno));
}

static struct bdb_ctx *
bdb_ctx_unref (struct bdb_ctx *ctx)
{
  /* TODO: bring locking */
  ctx->ref--;
  if (!ctx->ref) {
    /* time to close dbs */
    if (ctx->ns) {
      ctx->ns->close (ctx->ns, 0);
      ctx->ns = NULL;
    } else {
      /* do nothing */
    }

    if (ctx->storage) {
      ctx->storage->close (ctx->storage, 0);
      ctx->storage = NULL;
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
    
  return ctx;
}

static struct bdb_ctx *
bdb_ctx_ref (struct bdb_ctx *ctx)
{
  /* TODO: bring locking */
  ctx->ref++;
  return ctx;
}

static struct bdb_ctx *
bdb_get_bctx_from (xlator_t *this,
		   const char *path)
{
  char *pathname = strdup (path), *dir_name = NULL;
  dict_t *bctx_dict = ((struct bdb_private *)(this->private))->db_ctx;
  data_t *bctx_data = NULL;
  struct bdb_ctx *bctx = NULL;

  dir_name = dirname (pathname);

  if ((bctx_dict == NULL) || 
      ((bctx_data = dict_get (bctx_dict, dir_name)) == NULL) ||
      ((bctx = data_to_ptr (bctx_data)) == NULL)) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "failed to get bctx from private: %p for dirname: %s", this->private, dir_name);
    bctx = NULL;
  } else {
    /* do nothing */
  }
  
  return bctx;
}

static char *
stat_to_str (struct stat *stbuf)

{
  /* Convert the stat buf to string */
  char *tmp_buf = NULL;

  uint64_t dev = stbuf->st_dev;
  uint64_t ino = stbuf->st_ino;
  uint32_t mode = stbuf->st_mode;
  uint32_t nlink = stbuf->st_nlink;
  uint32_t uid = stbuf->st_uid;
  uint32_t gid = stbuf->st_gid;
  uint64_t rdev = stbuf->st_rdev;
  uint64_t size = stbuf->st_size;
  uint32_t blksize = stbuf->st_blksize;
  uint64_t blocks = stbuf->st_blocks;
  
  uint32_t atime = stbuf->st_atime;
  uint32_t mtime = stbuf->st_mtime;
  uint32_t ctime = stbuf->st_ctime;
  
#ifdef HAVE_TV_NSEC
  uint32_t atime_nsec = stbuf->st_atim.tv_nsec;
  uint32_t mtime_nsec = stbuf->st_mtim.tv_nsec;
  uint32_t ctime_nsec = stbuf->st_ctim.tv_nsec;
#else
  uint32_t atime_nsec = 0;
  uint32_t mtime_nsec = 0;
  uint32_t ctime_nsec = 0;
#endif
  
  asprintf (&tmp_buf,
	    GF_STAT_PRINT_FMT_STR,
	    dev,
	    ino,
	    mode,
	    nlink,
	    uid,
	    gid,
	    rdev,
	    size,
	    blksize,
	    blocks,
	    atime,
	    atime_nsec,
	    mtime,
	    mtime_nsec,
	    ctime,
	    ctime_nsec);
  
  return tmp_buf;
}

/* inode number transformation:
 *  very interesting note about using bdb and glusterfs together
 *      1. bdb xltor keeps directories as regular directories on the filesystem.
 *      2. regular files as key/value pair in database.
 *  problem here is to convert a given 'pathname' to 'inode'. to state the problem more formally:
 *   P={x | x is pathname}
 *   I={i | i is inode number}
 *   we need to find 'f' such that f(x) = i.
 *   i belongs to either D={d | d is st_ino obtained through struct stat from underlying fs} or 
 *                       R={r | r is recno of regular file entry in database of a directory}.
 *
 * well, currently we are using a work-around, to have a namespace database which houses all the regular files
 * as well as directories. BDB_MAKE_INO() uses the work-around method.
 *
 */
#define CHILD_INO_RANGE_BITS 32

#define BDB_MAKE_INO(ino, recno)  ((ino << CHILD_INO_RANGE_BITS) | recno)

int32_t 
bdb_mknod (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode,
	   dev_t dev)
{ /* TODO: sari illa idu, sari maaDbEku */
  DB *ns_dbp = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = ENOSYS;
  char *real_path = NULL;
  char *path_name = NULL, *dir_name;
  dict_t *bctx_dict = NULL;

  if (S_ISREG(mode)) {
    MAKE_REAL_PATH (real_path, this, loc->path);

    path_name = strdup (loc->path);
    dir_name = dirname (path_name);
    
    ns_dbp = bdb_open_ns_db (this, dir_name);
    
    if (!ns_dbp) {
      gf_log (this->name,
	      GF_LOG_ERROR,
	      "failed to open namespace db");
      op_ret = -1;
      op_errno = ENOENT;
    } else {
      /* direct lookup from database, so we can directly call ns_dbp->get() */
      DBT key, value;
      memset (&key, 0, sizeof (key));
      memset (&value, 0, sizeof (value));
      
      key.data = basename (path_name);
      key.size = strlen ((char *)key.data);
      
      //      bdb_print_all_records (this, ns_dbp);
      
      op_ret = ns_dbp->put (ns_dbp, NULL, &key, &value, 0/*DB_AUTO_COMMIT*/);
      
      if (!op_ret) {
	/* lookup successful */
	struct bdb_ctx *ctx = calloc (1, sizeof (*ctx));
	if (!ctx) {
	  op_ret = -1;
	  op_errno = ENOMEM;
	} else {
	  ctx->ns = ns_dbp;
	  dict_set (bctx_dict, path_name, data_from_static_ptr (ctx));
	} /* if (!ctx)...else */
      } /* if(op_ret != 0)...else */
    } /* if(ns_dbp)...else */
  } else {
    op_ret = -1;
    op_errno = ENOSYS;
  } /* if (S_ISREG(mode))...else */

  if (path_name)
    free (path_name);

  frame->root->rsp_refs = NULL;  
  STACK_UNWIND (frame, op_ret, op_errno, NULL, NULL);
  return 0;
}

int32_t 
bdb_rename (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *oldloc,
	    loc_t *newloc)
{
  /* TODO: hold a global lock, do bdb->del() followed by bdb->put () */
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, ENOSYS, NULL);
  return 0;
}

int32_t 
bdb_link (call_frame_t *frame, 
	  xlator_t *this,
	  loc_t *oldloc,
	  const char *newpath)
{
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, ENOSYS, NULL, NULL);
  return 0;
}

db_recno_t
bdb_get_recno (xlator_t *this,
	       DB *ns_dbp,
	       DBT *key)
{
  DBC *cursorp = NULL;
  DBT value = {0,};
  db_recno_t recno = 0;
  int32_t ret = 0;

  ns_dbp->cursor (ns_dbp, NULL, &cursorp, 0);
  
  if (cursorp) {
    key->flags = DB_DBT_USERMEM;
    value.flags = DB_DBT_MALLOC;
    ret = cursorp->get (cursorp, key, &value, DB_SET);
    
    if (!ret) {
      if (value.data)
	free (value.data);
      
      value.data = &recno;
      value.ulen = sizeof (recno);
      value.flags = DB_DBT_USERMEM;
      
      ret = cursorp->get (cursorp, key, &value, DB_GET_RECNO);
      
      if (!ret) {
	gf_log (this->name,
		GF_LOG_DEBUG,
		"successfully read record number is %lu", recno);
      } else {
	gf_log (this->name,
		GF_LOG_ERROR,
		"cursorp->get failed with DB_GET_RECNO");
      }
    } else {
      gf_log (this->name,
	      GF_LOG_ERROR,
	      "cursorp->get failed with DB_SET");
    }
    cursorp->close (cursorp);
  } else {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "failed to create cursor");
  }
  
  return recno;
}

static DB *
bdb_open_storage_db (xlator_t *this,
		     struct bdb_ctx *ctx)
{
  char *dir_name = ctx->directory;
  char *storage_db_path = NULL;
  DB *storage_dbp = NULL;
  int32_t op_ret = -1, op_errno = ENOENT;
  struct bdb_private *private = this->private;

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
      storage_dbp->err (storage_dbp, op_ret, "failed to open ns-db");
      op_ret = -1;
      op_errno = ENOENT; /* DB failure, find appropriate errno */
      storage_dbp = NULL;
    } 
  }
  return storage_dbp;
}


#define MAKE_KEY_FROM_PATH(key, path) do { \
  char *tmp = alloca (strlen (path));      \
  strcpy (tmp, path);                      \
  key = basename (tmp);                    \
}while (0);


int32_t 
bdb_create (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t flags,
	    mode_t mode,
	    fd_t *fd)
{
  int32_t op_ret = -1;
  int32_t op_errno = ENOSYS;
  DB *ns_dbp = NULL;
  char *pathname = strdup (loc->path);
  char *dir_name = NULL;
  char *ns_db_path = NULL;
  struct stat stbuf = {0,};
  struct bdb_ctx *bctx = NULL;

  dir_name = dirname (pathname);
  MAKE_REAL_PATH_TO_NS_DB (ns_db_path, this, dir_name);

  if (((bctx = bdb_get_bctx_from (this, loc->path)) != NULL) &&
      (bctx->ns != NULL)) {
    ns_dbp = bctx->ns;

    DBT key = {0,}, value = {0,};
    char *key_string = NULL;
    
    lstat (ns_db_path, &stbuf);
    MAKE_KEY_FROM_PATH (key_string, loc->path);
    
    key.data = key_string;
    key.size = strlen (key_string);
    key.flags = DB_DBT_USERMEM;

    value.data = stat_to_str (&stbuf);
    value.size = strlen ((char *)value.data);
    value.flags = DB_DBT_USERMEM;

    op_ret = ns_dbp->put (ns_dbp, NULL, &key, &value, 0/*DB_AUTO_COMMIT*/);
    if (!op_ret) {
      /* create successful */
      db_recno_t recno = 1;
      struct bdb_fd *bfd = calloc (1, sizeof (*bfd));
      char *key_string = NULL;
      
      bfd->ctx = bctx; /* TODO: make ref(ctx) */
      
      MAKE_KEY_FROM_PATH (key_string, loc->path);
      bfd->key = key_string;
      
      bctx->ns = ns_dbp;
      if (!bctx->directory)
	bctx->directory = strdup (dir_name);
      /* we are ready to go now, wat do we do??? do nothing... just place the same ctx which is there in 
       * inode->ctx to fd->ctx... ashTe ashTe anta open storage_db for this directory and place that 
       * pointer too, in ctx, check if someone else has opened the same storage db already. */
      if (!bctx->storage){
	/* NOTE: first time a file is being opened in this directory */
	bctx->storage = bdb_open_storage_db (this, bctx);
      }
      
      if (!bctx->storage) {
	gf_log (this->name,
		GF_LOG_ERROR,
		"failed to open storage database for fd:%p", fd);
	op_ret = -1;
	op_errno = EBADFD; /* TODO: find a more meaningful errno */
      } else {
	/* successfully opened storage db, dict_set should ideally sit here */
	bfd = calloc (1, sizeof (*bfd));
	if (!bfd) {
	  op_ret = -1;
	  op_errno = ENOMEM;
	} else {
	  char *key_string = NULL;
	  
	  bfd->ctx = bdb_ctx_ref (bctx); /* TODO: we need to ref bctx before using it */
	  
	  MAKE_KEY_FROM_PATH (key_string, loc->path);
	  bfd->key = strdup (key_string);
	  
	  op_ret = dict_set (fd->ctx, this->name, data_from_static_ptr (bfd));
	  if (op_ret == -1) {
	    gf_log (this->name,
		    GF_LOG_ERROR,
		    "failed to do dict_set() on fd->ctx for fd:%p && fd->ctx:%p", fd, fd->ctx);
	    op_errno = EBADFD;
	  } else {
	    op_ret = 0;
	    op_errno = 0;
	  }/* if(op_ret == -1)...else */
	}/* if(!bfd)...else */
      } /* if(!bctx->storage)...else */
      
      lstat (ns_db_path, &stbuf);
      recno = bdb_get_recno (this, ns_dbp, &key);
      stbuf.st_ino = bdb_inode_transform (stbuf.st_ino, recno);
      stbuf.st_mode  = mode;
    } /* if (!op_ret)...else */
  } else {
    op_ret = -1;
    op_errno = ENOENT;
  }/* if(bctx_data...)...else */


  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, fd, loc->inode, &stbuf);

  return 0;

}


/* bdb_open
 *
 * as input parameters bdb_open gets the file name, i.e key. bdb_open should effectively 
 * do: store key, open storage db, store storage-db pointer. And everything should be accessible 
 * by doing dict_get (fd->ctx, this->name).
 *
 */
int32_t 
bdb_open (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  int32_t flags,
	  fd_t *fd)
{
  int32_t op_ret = -1;
  int32_t op_errno = ENOSYS;
  struct bdb_ctx *bctx = NULL;
  struct bdb_fd  *bfd = NULL;
  char *path_name = NULL;

  path_name = strdup (loc->path);

  if (((bctx = bdb_get_bctx_from (this, loc->path)) == NULL)) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "failed to extract %s specific data", this->name);
    op_ret = -1;
    op_errno = EBADFD;
  } else {
    char *key_string = NULL;
    /* we are ready to go now, wat do we do??? do nothing... just place the same ctx which is there in 
     * inode->ctx to fd->ctx... ashTe ashTe anta open storage_db for this directory and place that pointer too,
     * in ctx, check if someone else has opened the same storage db already. */
    if (!bctx->storage) {
      bctx->storage = bdb_open_storage_db (this, bctx);
      if (!bctx->storage) {
	gf_log (this->name,
		GF_LOG_ERROR,
		"failed to open storage database for fd:%p", fd);
	op_ret = -1;
	op_errno = EBADFD; /* TODO: find a more meaningful errno */
      } else {
	/* successfully opened storage db, dict_set should ideally sit here */
	bfd = calloc (1, sizeof (*bfd));
	if (!bfd) {
	  op_ret = -1;
	  op_errno = ENOMEM;
	} else {
	  bfd->ctx = bdb_ctx_ref (bctx);
	  
	  MAKE_KEY_FROM_PATH (key_string, loc->path);
	  bfd->key = strdup (key_string);
	  
	  op_ret = dict_set (fd->ctx, this->name, data_from_static_ptr (bfd));
	  if (op_ret == -1) {
	    gf_log (this->name,
		    GF_LOG_ERROR,
		    "failed to do dict_set() on fd->ctx for fd:%p && fd->ctx:%p", fd, fd->ctx);
	    op_errno = EBADFD;
	  } else {
	    op_ret = 0;
	    op_errno = 0;
	  }/* if(op_ret == -1)...else */
	}/* if(!bfd)...else */
      } /* if(!bctx->storage)...else */
    } else {
      /* someone else has already opened storage db, do nothing
       * TODO: place dict_set ideally, so that code is not copied at two different locations. :( */
      bfd = calloc (1, sizeof (*bfd));
      bfd->ctx = bdb_ctx_ref (bctx);

      MAKE_KEY_FROM_PATH (key_string, loc->path);
      bfd->key = strdup (key_string);

      op_ret = dict_set (fd->ctx, this->name, data_from_static_ptr (bfd));
      if (op_ret == -1) {
	gf_log (this->name,
		GF_LOG_ERROR,
		"failed to do dict_set() on fd->ctx for fd:%p && fd->ctx:%p", fd, fd->ctx);
	op_errno = EBADFD;
      } else {
	op_ret = 0;
	op_errno = 0;
      }/* if(op_ret == -1)...else */
    }/* if(!bctx->storage)...else */
  } /* if((inode->ctx == NULL)...)...else */

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, fd);

  return 0;
}

int32_t 
bdb_readv (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   size_t size,
	   off_t offset)
{
  int32_t op_ret = -1;
  int32_t op_errno = ENOSYS;
  struct iovec vec = {0,};
  struct stat stbuf = {0,};
  data_t *bfd_data = NULL;
  struct bdb_fd *bfd = NULL;  
  dict_t *reply_dict = NULL;
  char *buf = NULL;

  if ((fd->ctx == NULL) ||
      ((bfd_data = dict_get (fd->ctx, this->name)) == NULL) ||
      ((bfd = data_to_ptr (bfd_data)) == NULL) ||
      (bfd->ctx->storage == NULL)){
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "failed to extract %s specific information from fd:%p", this->name, fd);
    op_ret = -1;
    op_errno = EBADFD;
  } else {
    if (offset != 0) {
      gf_log (this->name,
	      GF_LOG_ERROR,
	      "offset != 0");
      op_ret = -1;
      op_errno = EPERM;
    } else {
      /* we are ready to go */
      DBT key = {0,}, value = {0,};
      buf = calloc (1, size);
      if (!buf) {
	gf_log (this->name,
		GF_LOG_ERROR,
		"failed to allocate memory for buf");
	op_ret = -1;
	op_errno = ENOMEM;
      } else {
	key.data = bfd->key;
	key.size = strlen (bfd->key);
	key.flags = DB_DBT_USERMEM;
	
	/* NOTE: bdb lets us expand the file, suppose value.size > value.len, then value.len bytes
	 *      from value.doff offset and value.size bytes will be written from value.doff and 
	 *      data from value.doff + value.dlen will be pushed value.doff + value.size
	 */
	//	value.data = buf;
	//value.size = size;
	value.doff = offset;
	value.dlen = size;
	value.flags = DB_DBT_MALLOC | DB_DBT_PARTIAL;
	/* TODO: we prefer to give our own buffer to value.data and ask bdb to fill in it */
	op_ret = bfd->ctx->storage->get (bfd->ctx->storage, NULL, &key, &value, 0);
	
	if (op_ret == DB_NOTFOUND) {
	  op_ret = 0;
	  op_errno = 0; /* TODO: find an appropriate errno */
	} else if (op_ret == 0) {
	  /* NOTE: next two calls are redundant, _have_ to remove */
	  data_t *buf_data = get_new_data ();
	  char *ns_db_path = NULL;
	  memcpy (buf, (char *)value.data, value.size);
	  if (value.data)
	    free (value.data);

	  reply_dict = get_new_dict ();
	  
	  reply_dict->is_locked = 1;
	  buf_data->is_locked = 1;
	  buf_data->data      = buf;
	  buf_data->len       = value.size;
	  
	  dict_set (reply_dict, NULL, buf_data);
	  
	  frame->root->rsp_refs = dict_ref (reply_dict);
	  vec.iov_base = buf;
	  vec.iov_len = value.size;
	  
	  MAKE_REAL_PATH_TO_NS_DB (ns_db_path, this, bfd->ctx->directory);
	  lstat (ns_db_path, &stbuf);
	  op_ret = value.size;
	} else {
	  bfd->ctx->storage->err (bfd->ctx->storage, op_ret, "failed to do storage->get()");
	  gf_log (this->name,
		  GF_LOG_ERROR,
		  "failed to do dbp->get()");
	  op_ret = -1;
	  op_errno = EBADFD; /* TODO: this is DB error */
	}
      } /* if(op_ret)...else */
    }/* if(offset != 0)...else */
  }/* if((fd->ctx == NULL)...)...else */
    
  STACK_UNWIND (frame, op_ret, op_errno, &vec, 1, &stbuf);

  if (reply_dict)
    dict_unref (reply_dict);

  return 0;
}


int32_t 
bdb_writev (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    struct iovec *vector,
	    int32_t count,
	    off_t offset)
{
  int32_t op_ret = -1;
  int32_t op_errno = ENOSYS;
  struct stat stbuf = {0,};
  data_t *bfd_data = NULL;
  struct bdb_fd *bfd = NULL;
  
  if ((fd->ctx == NULL) ||
      ((bfd_data = dict_get (fd->ctx, this->name)) == NULL) ||
      ((bfd = data_to_ptr (bfd_data)) == NULL) ||
      (bfd->ctx->storage == NULL)){
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "failed to extract %s specific information from fd:%p", this->name, fd);
    op_ret = -1;
    op_errno = EBADFD;
  } else {
    /* we are ready to go */
    {
      DBT key = {0,}, value = {0,};
      int32_t idx = 0;
      int32_t buf_size = 0;
      char *buf = NULL, *buf_i = NULL;
    
      /* we are not doing writev, we are exposing writev to other, but doing a write using single buffer
       * internally */
      
      /* calculate the size of buffer we would require */
      for (idx = 0; idx < count; idx++) {
	buf_size += vector[idx].iov_len;
      } /* for(idx=0;...)... */
      
      buf = calloc (1, buf_size);
      buf_i = buf;
      
      /* copy to the buffer */
      for (idx = 0; idx < count; idx++) {
	/* page aligned buffer */
	memcpy (buf_i, vector[idx].iov_base, vector[idx].iov_len);
	
	buf_i += vector[idx].iov_len;
	op_ret += vector[idx].iov_len;
      } /* for(idx=0;...)... */
      /* we are ready to do bdb->put */
      key.data = bfd->key;
      key.size = strlen (bfd->key);
      
      value.data = buf;
      value.size = buf_size;
      value.dlen = buf_size;
      value.doff = offset;
      value.flags = DB_DBT_PARTIAL;
      op_ret = bfd->ctx->storage->put (bfd->ctx->storage, NULL, &key, &value, 0 /*DB_AUTO_COMMIT*/);
      if (op_ret) {
	/* write failed */
	gf_log (this->name,
		GF_LOG_ERROR,
		"failed to do storage->put()");
	bfd->ctx->storage->err (bfd->ctx->storage, op_ret, "failed to do storage->put()");
	op_ret = -1;
	op_errno = EBADFD; /* TODO: search for a more meaningful errno */
      } else {
	/* write successful, return now.. :) */
	char *ns_db_path = NULL;
	MAKE_REAL_PATH_TO_NS_DB (ns_db_path, this, bfd->ctx->directory);
	op_ret = buf_size;
	op_errno = 0;
	lstat (ns_db_path, &stbuf);
      }/* if(op_ret)...else */
      /* cleanup */
      if (buf)
	free (buf);
    }/* if(offset != 0)...else */
  }/* if((fd->ctx == NULL)...)...else */
  
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);
  return 0;
}

int32_t 
bdb_flush (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  int32_t op_ret = -1;
  int32_t op_errno = ENOSYS;
  data_t *bfd_data = NULL;
  struct bdb_fd *bfd = NULL;

  if ((fd == NULL) ||
      (fd->ctx == NULL) || 
      ((bfd_data = dict_get (fd->ctx, this->name)) == NULL) ||
      ((bfd = data_to_ptr (bfd_data)) == NULL) ||
      (bfd->key == NULL)) {
    gf_log (this->name, 
	    GF_LOG_ERROR, 
	    "failed to extract fd data from fd=%p", fd);
    op_ret = -1;
    op_errno = EBADF;
  } else {
    /* do nothing, as posix says */
    op_ret = 0;
    op_errno = 0;
  } /* if((fd == NULL)...)...else */
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t 
bdb_close (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  int32_t op_ret = -1;
  int32_t op_errno = EBADFD;
  data_t *bfd_data = NULL;
  struct bdb_fd *bfd = NULL;
  
  if ((fd->ctx == NULL) ||
      ((bfd_data = dict_get (fd->ctx, this->name)) == NULL) ||
      ((bfd = data_to_ptr (bfd_data)) == NULL) ||
      (bfd->ctx->storage == NULL)){
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "failed to extract %s specific information from fd:%p", this->name, fd);
    op_ret = -1;
    op_errno = EBADFD;
  } else {
    dict_del (fd->ctx, this->name);
    
    bdb_ctx_unref (bfd->ctx);
    bfd->ctx = NULL; /* TODO: ideally we should do unref(bfd->ctx) before unlinking */
    
    if (bfd->key)
      free (bfd->key); /* we did strdup() in bdb_open() */
    op_ret = 0;
    op_errno = 0;
  } /* if((fd->ctx == NULL)...)...else */

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}/* bdb_close */


int32_t 
bdb_fsync (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   int32_t datasync)
{
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, 0, 0);
  return 0;
}/* bdb_fsync */

int32_t 
bdb_lk (call_frame_t *frame,
	xlator_t *this,
	fd_t *fd,
	int32_t cmd,
	struct flock *lock)
{
  struct flock nullock = {0, };

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, EPERM, &nullock);
  return 0;
}/* bdb_lk */

int32_t
bdb_forget (call_frame_t *frame,
	    xlator_t *this,
	    inode_t *inode)
{
  data_t *bctx_data = NULL;
  struct bdb_ctx *bctx = NULL;

  if (((bctx_data = dict_get (inode->ctx, this->name)) == NULL) ||
      ((bctx = data_to_ptr (bctx_data)) == NULL)){
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "forgeting a file, do nothing");
  } else {
    gf_log (this->name,
	    GF_LOG_DEBUG,
	    "forget called for directory %s", bctx->directory);
#if 0 /* TODO: do somethig, now doing nothing */
    /* NOTE: the whole point of storing the bctx based on per directory basis stands on
    *        the assumption that directory will not be sent forget before all the files under it. */
    dict_del (inode->ctx, this->name);

    /* also do dict_del from the this->private->db_ctx */
    dict_del (bctx_dict, bctx->directory);

    bdb_ctx_unref (bctx);

    if (bctx->ns && (bctx->ref == 0)) {
      /* ideally this should happen in bdb_ctx_unref itself */
      bctx->ns->close (bctx->ns, 0);
    }
    if (bctx->storage && (bctx->ref == 0)){
      /* ideally this should happen in bdb_ctx_unref itself */
      bctx->storage->close (bctx->storage, 0);
    }
    if (bctx->directory)
      free (bctx->directory);
    
    free (bctx);
#endif
  }
  return 0;
}/* bdb_forget */


/* bdb_lookup
 *
 * bdb_lookup looks up for a pathname in ns_db.db and returns the struct stat as read from ns_db.db,
 * if required
 */
int32_t
bdb_lookup (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t need_xattr)
{
  struct bdb_private *private = this->private;
  struct stat stbuf = {0, };
  int32_t op_ret = -1;
  int32_t op_errno = ENOENT;
  dict_t *xattr = NULL;
  char *pathname = NULL, *dir_name = NULL, *ns_db_path = NULL, *real_path = NULL;
  DB *ns_dbp = NULL;
  dict_t *bctx_dict = NULL;
  data_t *bctx_data = NULL;
  struct bdb_ctx *bctx = NULL;

  MAKE_REAL_PATH (real_path, this, loc->path);

  pathname = strdup (loc->path);
  dir_name = dirname (pathname);

  MAKE_REAL_PATH_TO_NS_DB (ns_db_path, this, dir_name);  
  
  bctx_dict = private->db_ctx;
  if (!loc->parent) {
    /* this is valid only when we are looking up for root */
    /* SPECIAL CASE: looking up root, do exactly same as posix does */
    op_ret = lstat (real_path, &stbuf);
    op_errno = errno;
    
    if ((bctx = bdb_get_bctx_from (this, loc->path)) != NULL){
      /* revalidate */
      if (!bctx->ns) {
	ns_dbp = bdb_open_ns_db (this, dir_name);
	bctx->ns = ns_dbp;
      } else {
	/* do nothing */
      }
    } else {
      struct bdb_ctx *ctx = calloc (1, sizeof (*ctx));
      ns_dbp = bdb_open_ns_db (this, dir_name);
      
      if (ns_dbp) {
	stbuf.st_ino = 1;
	ctx->ns = ns_dbp;
	ctx->directory = strdup (dir_name);
	bdb_ctx_ref (ctx);
	dict_set (bctx_dict, dir_name, data_from_static_ptr (ctx));
      } else {
	gf_log (this->name,
		GF_LOG_ERROR,
		"failed to open ns db for \"/\"");
	op_ret = -1;
	op_errno = ENOENT; /* TODO: find better errno */
      }
    }/* if ((loc->inode->ctx...)...)...else */
  } else {
    DBT key = {0,}, value = {0,};
    char *key_string = NULL;
    /* see if the parent has dict_set the db information, if not error out */
    if ((bctx = bdb_get_bctx_from (this, loc->path)) != NULL){

      MAKE_KEY_FROM_PATH (key_string, loc->path);
      key.data = key_string;
      key.size = strlen (key_string);
      key.flags = DB_DBT_USERMEM;
      value.flags = DB_DBT_MALLOC;

      op_ret = bctx->ns->get (bctx->ns, NULL, &key, &value, 0);
      if (op_ret) {
	bctx->ns->err (bctx->ns, op_ret, "failed to do ns->get()");
	gf_log (this->name,
		GF_LOG_DEBUG,
		"lookup failed on %s", loc->path);
	op_ret = -1;
	op_errno = ENOENT;
      } else {
	/* lookup successful */
	if (value.data)
	  free (value.data);

	op_ret = lstat (real_path, &stbuf);
	if (op_ret == 0){
	  /* directory, we do have additional work to do */
	  if ((bctx_data = dict_get (bctx_dict, (char *)loc->path)) != NULL) {
	    /* revalidating directory inode */
	    gf_log (this->name,
		    GF_LOG_DEBUG,
		    "revaluating directory %s", (char *)loc->path);
	  } else {
	    /* fresh lookup for a directory, lot of work :O */
	    DB *child_ns_dbp = NULL;
	    struct bdb_ctx *child_bctx = NULL;

	    /* open only ns_db, storage_db will be opened on demand */
	    child_ns_dbp = bdb_open_ns_db (this, loc->path);
	    if (child_ns_dbp) {
	      child_bctx = calloc (1, sizeof (*child_bctx));
	      child_bctx->ns = child_ns_dbp;
	      child_bctx->directory = strdup (loc->path);
	      
	      bdb_ctx_ref (child_bctx);
	      dict_set (bctx_dict, (char *)loc->path, data_from_static_ptr (child_bctx));
	      dict_set (loc->inode->ctx, this->name, data_from_static_ptr (child_bctx));
	    } else {
	      /* this will break all the operations on the children of this directory,
	       * lets be on the safer side, return error */
	      gf_log (this->name,
		      GF_LOG_ERROR,
		      "failed to namespace data-base under %s directory", loc->path);
	      op_ret = -1;
	      op_errno = ENOENT; /* TODO: this is database error */
	    }/* if(child_ns_dbp)...else */
	  }/* if((bctx_data = ...)...)...else */
	  {
	    db_recno_t recno = 1;
	    recno = bdb_get_recno (this, bctx->ns, &key);
	    stbuf.st_ino = bdb_inode_transform (stbuf.st_ino, recno);
	  }
	} else {
	  /* get the stat of namespace db file and return */
	  op_ret = lstat (ns_db_path, &stbuf);
	  op_errno = errno;
	  {
	    db_recno_t recno = 1;
	    recno = bdb_get_recno (this, bctx->ns, &key);
	    stbuf.st_ino = bdb_inode_transform (stbuf.st_ino, recno);
	  }
	}
      }/* if(op_ret)...else */
    } else {
      gf_log (this->name,
	      GF_LOG_CRITICAL,
	      "loc->parent->dict is empty and looking up for file, this is impossible");
      op_ret = -1;
      op_errno = ENOENT;
    }/* if(bctx_data=...)...else */
  }/* if(loc->parent)...else */

  frame->root->rsp_refs = NULL;

  if (xattr)
    dict_ref (xattr);
  
  /* NOTE: ns_database of database which houses this entry is kept open */
  STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf, xattr);
  
  if (xattr)
    dict_unref (xattr);
  
  return 0;

}/* bdb_lookup */

int32_t
bdb_stat (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc)
{
  /* TODO: change st_ino to MAKE_INO (st_ino, recno) */
  struct stat buf;
  char *real_path;
  int32_t op_ret;
  int32_t op_errno;

  MAKE_REAL_PATH (real_path, this, loc->path);

  op_ret = lstat (real_path, &buf);
  op_errno = errno;

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &buf);

  return 0;
}/* bdb_stat */



/* bdb_opendir - in the world of bdb, open/opendir is all about opening correspondind databases.
 *               opendir in particular, opens the namespace database for the directory which is
 *               to be opened. after opening the database, a cursor to the database is also created.
 *               cursor helps us get the dentries one after the other, and cursor maintains the state
 *               about current positions in directory. pack 'pointer to namespace db', 'pointer to the
 *               cursor' into struct bdb_dir and store it in fd->ctx, we get from our parent xlator.
 *
 * @frame: call frame
 * @this:  our information, as we filled during init()
 * @loc:   location information
 * @fd:    file descriptor structure (glusterfs internal)
 *
 * return value - immaterial, async call.
 *
 */
int32_t 
bdb_opendir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc, 
	     fd_t *fd)
{
  char *real_path;
  int32_t op_ret;
  int32_t op_errno;
  dict_t *bctx_dict = ((struct bdb_private *)(this->private))->db_ctx;
  data_t *bctx_data = NULL;
  struct bdb_ctx *bctx = NULL;
  char *path_name = NULL;

  MAKE_REAL_PATH (real_path, this, loc->path);
  path_name = strdup (loc->path);

  if ((bctx_dict == NULL ) ||
      ((bctx_data = dict_get (bctx_dict, path_name)) == NULL) ||
      ((bctx = data_to_ptr (bctx_data)) == NULL)) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "failed to extract %s specific data from private data", this->name);
    op_ret = -1;
    op_errno = EBADFD;
  } else {
    struct bdb_dir *dir_fd = calloc (1, sizeof (*dir_fd));
    
    if (!dir_fd) {
      op_ret = -1;
      op_errno = ENOMEM;
    } else {
      DB *ns_dbp = bctx->ns;
      DBC *cursorp = NULL;

      dir_fd->ctx = bctx; /* TODO: ref(bctx) */
      if (ns_dbp) {
	op_ret = ns_dbp->cursor (ns_dbp, NULL, &cursorp, 0);
	dir_fd->path = strdup (real_path);
	dir_fd->key  = NULL;
	bdb_ctx_ref (bctx);
	dir_fd->ctx  = bctx;
	dir_fd->nsc = cursorp;
	dict_set (fd->ctx, this->name, data_from_static_ptr (dir_fd));
      } else {
	op_ret = -1;
	op_errno = ENOENT; /* TODO: find a better errno */
      }
    }
  }
  
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, fd);

  return 0;
}/* bdb_opendir */

/*
 * str_to_stat - convert a ASCII string to a struct stat
 * @buf: string
 *
 * not for external reference
 */
static struct stat *
str_to_stat (char *buf)
{
  struct stat *stbuf = calloc (1, sizeof (*stbuf));

  uint64_t dev;
  uint64_t ino;
  uint32_t mode;
  uint32_t nlink;
  uint32_t uid;
  uint32_t gid;
  uint64_t rdev;
  uint64_t size;
  uint32_t blksize;
  uint64_t blocks;
  uint32_t atime;
  uint32_t atime_nsec;
  uint32_t mtime;
  uint32_t mtime_nsec;
  uint32_t ctime;
  uint32_t ctime_nsec;

  sscanf (buf, GF_STAT_PRINT_FMT_STR,
	  &dev,
	  &ino,
	  &mode,
	  &nlink,
	  &uid,
	  &gid,
	  &rdev,
	  &size,
	  &blksize,
	  &blocks,
	  &atime,
	  &atime_nsec,
	  &mtime,
	  &mtime_nsec,
	  &ctime,
	  &ctime_nsec);

  stbuf->st_dev = dev;
  stbuf->st_ino = ino;
  stbuf->st_mode = mode;
  stbuf->st_nlink = nlink;
  stbuf->st_uid = uid;
  stbuf->st_gid = gid;
  stbuf->st_rdev = rdev;
  stbuf->st_size = size;
  stbuf->st_blksize = blksize;
  stbuf->st_blocks = blocks;

  stbuf->st_atime = atime;
  stbuf->st_mtime = mtime;
  stbuf->st_ctime = ctime;

#if HAVE_TV_NSEC
  stbuf->st_atim.tv_nsec = atime_nsec;
  stbuf->st_mtim.tv_nsec = mtime_nsec;
  stbuf->st_ctim.tv_nsec = ctime_nsec;
#endif

  return stbuf;
}/* str_to_stat */

int32_t
bdb_getdents (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      size_t size,
	      off_t off,
	      int32_t flag)
{
  /* TODO: 
   *     1. do readdir.
   *     2. get all 'in-use' entries from database
   *     3. merge the list and give to user
   */
  int32_t op_ret;
  int32_t op_errno;
  dir_entry_t entries = {0, };
  int count = 0;
#if 0
  char *real_path;
  dir_entry_t *tmp;
  DIR *dir;
  //  struct dirent *dirent;
  int real_path_len;
  int entry_path_len;
  char *entry_path;
  data_t *bfd_data = NULL;
  struct bdb_dir *bfd;
  DB *ns_dbp = NULL;
#endif
#if 0
  if (fd && fd->ctx) {
    bfd_data = dict_get (fd->ctx, this->name);
    if (!bfd_data) {
      frame->root->rsp_refs = NULL;
      gf_log (this->name, GF_LOG_ERROR, "fd %p does not have context in %s",
	      fd, this->name);
      STACK_UNWIND (frame, -1, EBADFD, &entries, 0);
      return 0;
    }
  } else {
    gf_log (this->name, GF_LOG_ERROR, "fd or fd->ctx is NULL (fd=%p)", fd);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD, &entries, 0);
    return 0;
  }

  bfd = data_to_ptr (bfd_data);

  if (!bfd) {
    gf_log (this->name, GF_LOG_ERROR, "bfd from fd->ctx for %s is NULL", fd);
    STACK_UNWIND (frame, -1, EBADFD, NULL, 0);
    return 0;
  }

  if (!bfd->path) {
    gf_log (this->name, GF_LOG_ERROR,
	    "bfd does not have path set (possibly file fd, fd=%p)", fd);
    STACK_UNWIND (frame, -1, EBADFD, NULL, 0);
    return 0;
  }

  real_path = bfd->path;
  real_path_len = strlen (real_path);
  entry_path_len = real_path_len + 1024;
  entry_path = calloc (1, entry_path_len);
  strcpy (entry_path, real_path);
  entry_path[real_path_len] = '/';
  
  dir = bfd->dir;
  ns_dbp = bfd->ns;

  if (!dir) {
    gf_log (this->name, GF_LOG_ERROR, 
	    "bfd does not have dir set (possibly file fd, fd=%p, path=`%s'",
	    fd, real_path);


    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, errno, &entries, 0);
    freee (entry_path);
    return 0;
  } else {
    op_ret = 0;
    op_errno = 0;
  }

  if (ns_dbp) {
    DBC *cursorp = NULL;
    op_ret = ns_dbp->cursor (ns_dbp, NULL, &cursorp, 0);
    
    if (op_ret) {
      /* failed to get cursor */
      op_ret = -1;
      op_ret = EBADFD; /* TODO: find appropriate errno */
    } else {
      DBT key = {0,}, value = {0,};
      char *retkey = NULL, *retdata = NULL;
      size_t retklen = 0, retdlen = 0;
      void *p = NULL;
      struct stat *stbuf = NULL;

#define READDIR_BUF_LEN   1024

      value.data = calloc (1, READDIR_BUF_LEN);
      value.size  = READDIR_BUF_LEN;
      value.flags = DB_DBT_USERMEM;
      
      while ((op_ret = cursorp->get (cursorp, &key, &value, DB_MULTIPLE_KEY | DB_NEXT)) == 0) {
	
	for (DB_MULTIPLE_INIT(p, &value);;) {
	  DB_MULTIPLE_KEY_NEXT(p, &value, retkey, retklen, retdata, retdlen);
	  
	  if (p == NULL)
	    break;
	  
	  /* TODO: work with retkey, retklen, retdata, retdlen */
	  tmp = calloc (1, sizeof (*tmp));
	  tmp->name = strdup (retkey);
	  stbuf = str_to_stat (retdata);
	  tmp->buf = *stbuf;
	  count++;
	  tmp->next = entries.next;
	  entries.next = tmp;
	  
	  if (count == size)
	    break;
	}
      }
    }
  } else {
    op_ret = -1;
    op_errno = EBADFD;
  }
  //  seekdir (dir, 0);
  freee (entry_path);
#endif /* #if 0 */
  
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &entries, count);
#if 0
  while (entries.next) {
    tmp = entries.next;
    entries.next = entries.next->next;
    freee (tmp->name);
    freee (tmp);
  }
#endif
  return 0;
}/* bdb_getdents */


int32_t 
bdb_closedir (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  /* TODO: regular closedir as in posix */
  int32_t op_ret;
  int32_t op_errno;
  data_t *dir_data;
  struct bdb_dir *dir_fd;

  op_ret = 0;
  op_errno = errno;

  frame->root->rsp_refs = NULL;
  
  if ((fd == NULL) ||
      (fd->ctx == NULL) || 
      ((dir_data = dict_get (fd->ctx, this->name)) == NULL) ||
      ((dir_fd = data_to_ptr (dir_data)) == NULL)) {
    gf_log (this->name, 
	    GF_LOG_ERROR, 
	    "failed to extract fd data from fd=%p", fd);
    op_ret = -1;
    op_errno = EBADF;
  } else {
    dict_del (fd->ctx, this->name);
	
    if (dir_fd->path) {
      free (dir_fd->path);
    } else {
      gf_log (this->name, GF_LOG_ERROR, "dir_fd->path was NULL. fd=%p bfd=%p",
	      fd, dir_fd);
    }
    
    if (dir_fd->nsc) {
      dir_fd->nsc->close (dir_fd->nsc);
      dir_fd->nsc = NULL;
      bdb_ctx_unref (dir_fd->ctx);
      dir_fd->ctx = NULL; 
    } else {
      gf_log (this->name, GF_LOG_ERROR, "dir_fd->ns is NULL");
    }
    free (dir_fd);
  }

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}/* bdb_closedir */


int32_t 
bdb_readlink (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      size_t size)
{
  /* TODO: ENOSYS */
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, ENOSYS, NULL);

  return 0;
}/* bdb_readlink */


int32_t 
bdb_mkdir (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode)
{
  /* TODO: use database environment to guarantee the consistancy between different db operations */
  int32_t op_ret = -1;
  int32_t op_errno = EEXIST;
  char *real_path = NULL;
  struct stat stbuf = {0, };
  dict_t *bctx_dict = ((struct bdb_private *)(this->private))->db_ctx;
  struct bdb_ctx *bctx = NULL;
  DB *ns_dbp = NULL;

  MAKE_REAL_PATH (real_path, this, loc->path);
  
  op_ret = mkdir (real_path, mode);
  op_errno = errno;
  
  if (op_ret == 0) {
    chown (real_path, frame->root->uid, frame->root->gid);
    lstat (real_path, &stbuf);
    
    if (((bctx = bdb_get_bctx_from (this, loc->path)) != NULL) &&
	(bctx->ns != NULL)) {
      ns_dbp = bctx->ns;

      /* mkdir successful, we need to put an entry in the parent dir ns_db */
      DBT key = {0,}, value = {0,};

      key.data = basename (real_path);
      key.size = strlen ((char *)key.data);
      
      value.data = stat_to_str (&stbuf);
      value.size = strlen ((char *)value.data);
      
      op_ret = ns_dbp->put (ns_dbp, NULL, &key, &value, 0/*DB_AUTO_COMMIT*/);
      
      if (!op_ret) {
	struct bdb_ctx *child_bctx = calloc (1, sizeof (*child_bctx));
	
	if (!child_bctx) {
	  op_ret = -1;
	  op_errno = ENOMEM;
	} else {
	  DB *child_ns_dbp = NULL;
	  
	  /* open only ns_db, storage_db will be opened on demand */
	  child_ns_dbp = bdb_open_ns_db (this, loc->path);
	  if (child_ns_dbp) {
	    child_bctx->ns = child_ns_dbp;
	    child_bctx->directory = strdup (loc->path);
	    
	    bdb_ctx_ref (child_bctx);
	    dict_set (bctx_dict, (char *)loc->path, data_from_static_ptr (child_bctx));
	  } else {
	    /* this will break all the operations on the children of this directory,
	     * lets be on the safer side, return error */
	    gf_log (this->name,
		    GF_LOG_ERROR,
		    "failed to namespace data-base under %s directory", real_path);
	    op_ret = -1;
	    op_errno = ENOENT; /* TODO: this is database error */
	  }/* if(child_ns_dbp)...else */
	
	  db_recno_t recno = 1;
	  recno = bdb_get_recno (this, ns_dbp, &key);
	  stbuf.st_ino = bdb_inode_transform (stbuf.st_ino, recno);
	} /* if (!ctx)...else */
      } else {
	op_ret = -1;
	op_errno = ENOENT;
      } /* if (!ret)....else */
    }
  } else {
    /* TODO: remove the directory we just created */
    op_ret = -1;
    op_errno = ENOENT;
  }
  
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf);

  return 0;
}/* bdb_mkdir */


int32_t 
bdb_unlink (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  int32_t op_ret = -1;
  int32_t op_errno = EPERM;
  
  /* TODO: storage->del () and ns->put () with data as null, to ensure that inode numbers of other
   *       files don't change in current session.
   */
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}/* bdb_unlink */

int32_t 
bdb_remove (const char *path, 
	    const struct stat *stat, 
	    int32_t typeflag, 
	    struct FTW *ftw)
{
  /* TODO: regular remove won't work here, when path is a regular file */
  return remove (path);
} /* bdb_remove */

int32_t
bdb_rmelem (call_frame_t *frame,
	    xlator_t *this,
	    const char *path)
{
  int32_t op_ret, op_errno;
  char *real_path;

  MAKE_REAL_PATH (real_path, this, path);
  op_ret = nftw (real_path, bdb_remove, 20, FTW_DEPTH|FTW_PHYS);
  op_errno = errno;
  /* FTW_DEPTH = traverse subdirs first before calling bdb_remove
   * on real_path
   * FTW_PHYS = do not follow symlinks
   */
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
} /* bdb_rmelm */

int32_t 
bdb_rmdir (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc)
{
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, EPERM);

  return 0;
} /* bdb_rmdir */

int32_t 
bdb_symlink (call_frame_t *frame,
	     xlator_t *this,
	     const char *linkname,
	     loc_t *loc)
{
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, EPERM, loc->inode, NULL);
  return 0;
} /* bdb_symlink */

int32_t 
bdb_chmod (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode)
{
  int32_t op_ret = -1;
  int32_t op_errno = ENOSYS;
  char *real_path;
  struct stat stbuf = {0,};

  MAKE_REAL_PATH (real_path, this, loc->path);
  
  op_ret = lstat (real_path, &stbuf);
  
  if (op_ret == 0) {
    /* directory */
    op_ret = chmod (real_path, mode);
    op_errno = errno;
  } else {
    /* file, ENOSYS */
    op_ret = -1;
    op_errno = EPERM;
  }/* if(op_ret == 0)...else */
    
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}/* bdb_chmod */


int32_t 
bdb_chown (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   uid_t uid,
	   gid_t gid)
{
  int32_t op_ret = -1;
  int32_t op_errno = ENOSYS;
  char *real_path;
  struct stat stbuf = {0,};

  MAKE_REAL_PATH (real_path, this, loc->path);
  
  op_ret = lstat (real_path, &stbuf);
  if (op_ret == 0) {
    /* directory */
    op_ret = lchown (real_path, uid, gid);
    op_errno = errno;
  } else {
    /* not a directory, ENOSYS */
    op_ret = -1;
    op_errno = EPERM;
  }/* if(op_ret == 0)...else */
    
  frame->root->rsp_refs = NULL;  
  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}/* bdb_chown */


int32_t 
bdb_truncate (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      off_t offset)
{
  /* TODO: do bdb->put () with a data of size = 1 */
  int32_t op_ret = -1;
  int32_t op_errno = ENOSYS;
  char *real_path;
  struct stat stbuf = {0,};
  DB *storage_dbp = NULL;
  char *ns_db_path = NULL;
  inode_t *inode = loc->inode;
  struct bdb_ctx *bctx = NULL;

  if ((bctx = bdb_get_bctx_from (this, loc->path)) == NULL) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "failed to extract %s specific data from inode: %p", this->name, inode);
    op_ret = -1;
    op_errno = EBADFD;
  } else {

    MAKE_REAL_PATH (real_path, this, loc->path);
    MAKE_REAL_PATH_TO_NS_DB (ns_db_path, this, bctx->directory);
    
    if (!bctx->storage) {
      storage_dbp = bdb_open_storage_db (this, bctx);
      bctx->storage = storage_dbp;
    } else {
      storage_dbp = bctx->storage;
    }
    
    if (!storage_dbp) {
      gf_log (this->name,
	      GF_LOG_ERROR,
	      "failed to open storage db for directory %s", bctx->directory);
      op_ret = -1;
      op_errno = ENOENT; /* TODO: find appropriate errno */
    } else {
      /* now truncate */
      DBT key = {0,}, value = {0,};
      /* some checks:
       *  1. see if we already have this file on namespace, if not create on namespace too
       */
      {
	DB *ns_dbp = NULL;
	char *key_string = NULL;
	ns_dbp = bctx->ns;
	
	MAKE_KEY_FROM_PATH (key_string, loc->path);
	key.data = key_string;
	key.size = strlen (key_string);
	value.flags = DB_DBT_MALLOC;
	op_ret = ns_dbp->get (ns_dbp, NULL, &key, &value, 0);
	lstat (ns_db_path, &stbuf);
	if (!op_ret) {
	  /* namespace entry exists, file has been created. do nothing */
	  stbuf.st_ino = loc->inode->ino;
	} else {
	  MAKE_KEY_FROM_PATH (key_string, loc->path);
      
	  op_ret = ns_dbp->put (ns_dbp, NULL, &key, &value, 0/*DB_AUTO_COMMIT*/);
	  if (op_ret) {
	    gf_log (this->name,
		    GF_LOG_ERROR,
		    "failed to create namespace db entry for %s", loc->path);
	    op_ret = -1;
	    op_errno = ENOENT; /* TODO: find appropriate errno */
	  } else {
	    db_recno_t recno = 0;
	    recno = bdb_get_recno (this, ns_dbp, &key);
	    stbuf.st_ino = bdb_inode_transform (stbuf.st_ino, recno);
	  }
	} /* if(!op_ret)...else */
	{
	  /* now we create a entry in storage db for the loc->path */
	  DBT key = {0,}, value = {0,};
	  
	  key.data = key_string;
	  key.size = strlen (key_string);
	  value.flags = DB_DBT_MALLOC;
	  op_ret = storage_dbp->get (storage_dbp, NULL, &key, &value, 0 /*DB_AUTO_COMMIT*/);
	  
	  if (op_ret == DB_NOTFOUND) {
	    /* storage db entry doesn't exist, don't do anything */
	    op_ret = 0;
	    op_errno = 0;
	  } else if (op_ret == 0){
	    /* storage db entry exist, truncate it */
	    op_ret = storage_dbp->put (storage_dbp, NULL, &key, &value, 0 /*DB_AUTO_COMMIT*/);
	    if (op_ret) {
	      op_ret = -1;
	      op_errno = ENOENT; /* TODO: find appropriate errno */
	    } else {
	      /* do nothing */
	    }/* if(op_ret)...else */
	  } else {
	    storage_dbp->err (storage_dbp, op_ret, "failed to do dbp->put()");
	    op_ret = -1;
	    op_ret = ENOSYS;
	  }
	}
      }
    }/* if(!storage_dbp)...else */
  }/* if((inode->ctx == NULL)...)...else */
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);
  
  return 0;
}/* bdb_truncate */


int32_t 
bdb_utimens (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     struct timespec ts[2])
{
  int32_t op_ret = 0;
  int32_t op_errno = 0;
  struct stat stbuf = {0, };
  struct timeval tv[2];
  char *ns_db_path = NULL, *dir_name = NULL;
  
  dir_name = strdup (loc->path);
  dir_name = dirname (dir_name);

  MAKE_REAL_PATH_TO_NS_DB (ns_db_path, this, dir_name);
  tv[0].tv_sec = ts[0].tv_sec;
  tv[0].tv_usec = ts[0].tv_nsec / 1000;
  tv[1].tv_sec = ts[1].tv_sec;
  tv[1].tv_usec = ts[1].tv_nsec / 1000;
  
  op_ret = lutimes (ns_db_path, tv);
  if (op_ret == -1 && errno == ENOSYS) {
    op_ret = utimes (ns_db_path, tv);
  }
  op_errno = errno;

  lstat (ns_db_path, &stbuf);
  stbuf.st_ino = loc->inode->ino;
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}/* bdb_utimens */

int32_t 
bdb_statfs (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)

{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct statvfs buf = {0, };

  MAKE_REAL_PATH (real_path, this, loc->path);

  op_ret = statvfs (real_path, &buf);
  op_errno = errno;

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &buf);
  return 0;
}/* bdb_statfs */

int32_t
bdb_incver (call_frame_t *frame,
	      xlator_t *this,
	      const char *path)
{
  /* TODO: version exists for directory, version is consistent for every entry in the directory */
  char *real_path;
  char version[50];
  int32_t size = 0;
  int32_t ver = 0;

  MAKE_REAL_PATH (real_path, this, path);

  size = lgetxattr (real_path, GLUSTERFS_VERSION, version, 50);
  if ((size == -1) && (errno != ENODATA)) {
    STACK_UNWIND (frame, -1, errno);
    return 0;
  } else {
    version[size] = '\0';
    ver = strtoll (version, NULL, 10);
  }
  ver++;
  sprintf (version, "%u", ver);
  lsetxattr (real_path, GLUSTERFS_VERSION, version, strlen (version), 0);
  STACK_UNWIND (frame, ver, 0);

  return 0;
}/* bdb_incver */

int32_t 
bdb_setxattr (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      dict_t *dict,
	      int flags)
{
  /* TODO:
   *       1. equivalent to write, if done on directory
   *       2. ENOSYS for regular file
   */
  int32_t ret = -1;
  int32_t op_ret = -1;
  int32_t op_errno = ENOENT;
  data_pair_t *trav = dict->members_list;
  DB *dbp = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  DBT key  = {0,};
  DBT data = {0,};

  if (S_ISDIR (loc->inode->st_mode)) {
    if (dbp) {
      while (trav) {
	key.data = trav->key;
	key.size = strlen (trav->key)+1;
	data.data = trav->value->data;
	data.size = trav->value->len+1;
	
	if ((ret = dbp->put (dbp, NULL, &key, &data, 0/*DB_AUTO_COMMIT*/)) != 0) {
	  op_ret   = -1;
	  op_errno = ret;
	  break;
	}
	trav = trav->next;
      }
    }
    frame->root->rsp_refs = NULL;
  } else {
    frame->root->rsp_refs = NULL;
    op_ret   = -1;
    op_errno = ENOSYS;
  }

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;  
}/* bdb_setxattr */

int32_t 
bdb_getxattr (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  /* TODO: equivalent to read, if done on a directory.
   *       ENOSYS for regular files
   */
  int32_t ret = -1;
  dict_t *dict = get_new_dict ();
  DB *dbp = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  DBT key = {0,};
  DBT data = {0,};

  if (dbp) {
    key.data = (char *)loc->path;
    key.size = strlen (loc->path)+1;
    
    if ((ret = dbp->get (dbp, NULL, &key, &data, 0)) != 0) {
      STACK_UNWIND (frame, -1, ret, dict);
      if (dict)
	dict_destroy (dict);
      return 0;
    }
    
    dict_set (dict, (char *)loc->path, bin_to_data (data.data, data.size));
  }

  STACK_UNWIND (frame, 0, 0, dict);

  if (dict)
    dict_destroy (dict);
  
  return 0;
}/* bdb_getxattr */

/* send the DB->del in the call */
int32_t 
bdb_removexattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
                 const char *name)
{
  /* TODO: equivalent to unlink, if done on directory
   *       ENOSYS for regular files
   */
  int32_t ret;

  DB *dbp = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  if (dbp) {
    DBT key = {
      .data = (char *)name,
      .size = strlen (name),
    };
    
    if ((ret = dbp->del(dbp, NULL, &key, 0)) != 0) {
      frame->root->rsp_refs = NULL;  
      STACK_UNWIND (frame, -1, ret);
      return 0;
    }
  }
  
  frame->root->rsp_refs = NULL;  
  STACK_UNWIND (frame, 0, 0);
  return 0;
}/* bdb_removexattr */


int32_t 
bdb_fsyncdir (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		int datasync)
{
  int32_t op_ret;
  int32_t op_errno;
  data_t *bfd_data = dict_get (fd->ctx, this->name);
  struct bdb_fd *bfd;
  int32_t _fd;

  frame->root->rsp_refs = NULL;

  if (bfd_data == NULL) {
    gf_log (this->name, GF_LOG_ERROR,
	    "bfd data is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  bfd = data_to_ptr (bfd_data);
  if (!bfd) {
    gf_log (this->name, GF_LOG_ERROR,
	    "bfd is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  _fd = 0;/* TODO: bfd->fd; fd doesn't make any sense to bdb */

  op_ret = 0;
  op_errno = errno;

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}/* bdb_fsycndir */


int32_t 
bdb_access (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t mask)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;

  MAKE_REAL_PATH (real_path, this, loc->path);

  op_ret = access (real_path, mask);
  op_errno = errno;

  frame->root->rsp_refs = NULL;  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}/* bdb_access */


int32_t 
bdb_ftruncate (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 off_t offset)
{
  int32_t op_ret = -1;
  int32_t op_errno = ENOSYS;
  int32_t _fd = 0;
  struct stat buf = {0,};
  data_t *bfd_data = dict_get (fd->ctx, this->name);
  struct bdb_fd *bfd;

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &buf);

  return 0;

  frame->root->rsp_refs = NULL;

  if (bfd_data == NULL) {
    gf_log (this->name, GF_LOG_ERROR,
	    "bfd_data is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  bfd = data_to_ptr (bfd_data);
  if (!bfd) {
    gf_log (this->name, GF_LOG_ERROR,
	    "bfd is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  _fd = 0; /* TODO: this bfd->fd; doesn't make any sense to bdb */


  op_ret = ftruncate (_fd, offset);
  op_errno = errno;
    
  fstat (_fd, &buf);
  

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &buf);

  return 0;
}

int32_t 
bdb_fchown (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      uid_t uid,
	      gid_t gid)
{
  int32_t op_ret = -1;
  int32_t op_errno = ENOSYS;
  int32_t _fd;
  struct stat buf;
  data_t *bfd_data = dict_get (fd->ctx, this->name);
  struct bdb_fd *bfd;

  STACK_UNWIND (frame, op_ret, op_errno, &buf);

  return 0;

  frame->root->rsp_refs = NULL;

  if (bfd_data == NULL) {
    gf_log (this->name, GF_LOG_ERROR,
	    "bfd_data is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  bfd = data_to_ptr (bfd_data);

  if (!bfd) {
    gf_log (this->name, GF_LOG_ERROR,
	    "bfd is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  _fd = 0; /* TODO: bfd->fd; doesn't make any sense to bdb */


  op_ret = fchown (_fd, uid, gid);
  op_errno = errno;

  fstat (_fd, &buf);


  STACK_UNWIND (frame, op_ret, op_errno, &buf);

  return 0;
}


int32_t 
bdb_fchmod (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      mode_t mode)
{
  int32_t op_ret = -1;
  int32_t op_errno = ENOSYS;
  int32_t _fd;
  struct stat buf = {0,};
  data_t *bfd_data = dict_get (fd->ctx, this->name);
  struct bdb_fd *bfd;

  frame->root->rsp_refs = NULL;  
  STACK_UNWIND (frame, op_ret, op_errno, &buf);

  return 0;

  if (bfd_data == NULL) {
    gf_log (this->name, GF_LOG_ERROR,
	    "bfd_data is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  bfd = data_to_ptr (bfd_data);

  if (!bfd) {
    gf_log (this->name, GF_LOG_ERROR,
	    "bfd is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  _fd = 0; /* TODO: bfd->fd; doesn't make any sense to bdb */


  op_ret = fchmod (_fd, mode);
  op_errno = errno;
  
  fstat (_fd, &buf);


  frame->root->rsp_refs = NULL;  
  STACK_UNWIND (frame, op_ret, op_errno, &buf);

  return 0;
}

int32_t 
bdb_setdents (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		int32_t flags,
		dir_entry_t *entries,
		int32_t count)
{
  char *real_path;
  char *entry_path;
  int32_t real_path_len;
  int32_t entry_path_len;
  int32_t ret = 0;
  struct bdb_fd *bfd;
  data_t *bfd_data = NULL;

  frame->root->rsp_refs = NULL;

  bfd_data = dict_get (fd->ctx, this->name);
  if (!bfd_data) {
    gf_log (this->name, GF_LOG_ERROR, "fd->ctx not found on fd=%p for %s",
	    fd, this->name);
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }


  bfd = data_to_ptr (bfd_data);

  if (!bfd) {
    gf_log (this->name, GF_LOG_ERROR, "bfd is NULL on fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  real_path = bfd->key; /* TODO: fucked up, it was bfd->path earlier. we need to rebuild real_path
			 * using bfd->ctx->directory and bfd->key */

  if (!real_path) {
    gf_log (this->name, GF_LOG_ERROR,
	    "path is NULL on bfd=%p fd=%p", bfd, fd);
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  real_path_len = strlen (real_path);
  entry_path_len = real_path_len + 256;
  entry_path = calloc (1, entry_path_len);

  if (!entry_path) {
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  strcpy (entry_path, real_path);
  entry_path[real_path_len] = '/';

  /* fd exists, and everything looks fine */
  {
    /**
     * create an entry for each one present in '@entries' 
     *  - if flag is set (ie, if its namespace), create both directories and 
     *    files 
     *  - if not set, create only directories.
     *
     *  after the entry is created, change the mode and ownership of the entry
     *  according to the stat present in entries->buf.  
     */
    dir_entry_t *trav = entries->next;
    while (trav) {
      char pathname[4096] = {0,};
      strcpy (pathname, entry_path);
      strcat (pathname, trav->name);

      if (S_ISDIR(trav->buf.st_mode)) {
	/* If the entry is directory, create it by calling 'mkdir'. If 
	 * directory is not present, it will be created, if its present, 
	 * no worries even if it fails.
	 */
	ret = mkdir (pathname, trav->buf.st_mode);
	if (!ret) {
	  gf_log (this->name, 
		  GF_LOG_DEBUG, 
		  "Creating directory %s with mode (0%o)", 
		  pathname,
		  trav->buf.st_mode);
	}
      } else if (flags == GF_SET_IF_NOT_PRESENT || flags != GF_SET_DIR_ONLY) {
	/* Create a 0byte file here */
	if (S_ISREG (trav->buf.st_mode)) {
	  ret = open (pathname, O_CREAT|O_EXCL, trav->buf.st_mode);
	  if (ret > 0) {
	    gf_log (this->name,
		    GF_LOG_DEBUG,
		    "Creating file %s with mode (0%o)",
		    pathname, 
		    trav->buf.st_mode);
	    close (ret);
	  }
	} else if (S_ISLNK(trav->buf.st_mode)) {
	  ret = symlink (trav->name, pathname);
	  if (!ret) {
	    gf_log (this->name,
		    GF_LOG_DEBUG,
		    "Creating symlink %s",
		    pathname);
	  }
	} else if (S_ISBLK (trav->buf.st_mode) || 
		   S_ISCHR (trav->buf.st_mode) || 
		   S_ISFIFO (trav->buf.st_mode)) {
	  ret = mknod (pathname, trav->buf.st_mode, trav->buf.st_dev);
	  if (!ret) {
	    gf_log (this->name,
		    GF_LOG_DEBUG,
		    "Creating device file %s",
		    pathname);
	  }
	}
      }
      /* TODO: handle another flag, GF_SET_OVERWRITE */

      /* Change the mode */
      chmod (pathname, trav->buf.st_mode);
      /* change the ownership */
      chown (pathname, trav->buf.st_uid, trav->buf.st_gid);

      /* consider the next entry */
      trav = trav->next;
    }
  }
  //  op_errno = errno;
  
  /* Return success all the time */
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, 0, 0);
  
  freee (entry_path);
  return 0;
}

int32_t 
bdb_fstat (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  int32_t _fd;
  int32_t op_ret = -1;
  int32_t op_errno = ENOSYS;
  struct stat buf;
  data_t *bfd_data = dict_get (fd->ctx, this->name);
  struct bdb_fd *bfd;

  frame->root->rsp_refs = NULL;

  STACK_UNWIND (frame, op_ret, op_errno, &buf);
  return 0;

  
  if (bfd_data == NULL) {
    gf_log (this->name, GF_LOG_ERROR,
	    "fd=%p has no context", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  bfd = data_to_ptr (bfd_data);

  if (!bfd) {
    gf_log (this->name, GF_LOG_ERROR,
	    "bfd is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;  }

  _fd = 0; /* TODO: bfd->fd; doesn't make any sense to bdb */

  op_ret = fstat (_fd, &buf);
  op_errno = errno;

  STACK_UNWIND (frame, op_ret, op_errno, &buf);
  return 0;
}


#define ALIGN(x) (((x) + sizeof (uint64_t) - 1) & ~(sizeof (uint64_t) - 1))


static int32_t
bdb_dirent_size (DBT *key)
{
  return ALIGN (24 /* FIX MEEEE!!! */ + key->size);
}

/* TODO: handle both dir and file in the directory */

int32_t
bdb_readdir (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     size_t size,
	     off_t off)
{
  data_t *bfd_data = NULL;
  struct bdb_dir *bfd = NULL;
  int32_t this_size = 0;
  int32_t op_ret = -1, op_errno = ENOSYS;
  char *buf = NULL;
  size_t filled = 0;
  DB *ns_dbp = NULL;
  struct stat stbuf = {0,};


  if (((bfd_data = dict_get (fd->ctx, this->name)) == NULL) || 
      ((bfd = data_to_ptr (bfd_data)) == NULL) ||
      ((ns_dbp = bfd->ctx->ns)  == NULL)) {
    gf_log (this->name, GF_LOG_ERROR,
	    "failed to extract %s specific fd information from fd=%p", this->name, fd);
    op_ret = -1;
    op_errno = EBADFD;
  } else {
    buf = calloc (size, 1); /* readdir buffer needs 0 padding */
    
    if (!buf) {
      gf_log (this->name, GF_LOG_ERROR,
	      "malloc (%d) returned NULL", size);
      STACK_UNWIND (frame, -1, ENOMEM, NULL);
      return 0;
    }

    lstat (bfd->path, &stbuf);
    db_recno_t recno = 1;
    DBC *cursorp = bfd->nsc;

    while (filled <= size) {
      gf_dirent_t *this_entry = NULL;
      DBT key = {0,}, value = {0,};

      /*      key.data = &recno;
	      key.size = sizeof (recno);*/
      key.flags = DB_DBT_MALLOC;
      value.flags = DB_DBT_MALLOC;
      op_ret = cursorp->get (cursorp, &key, &value, DB_NEXT);
      
      if (op_ret == DB_NOTFOUND) {
	/* we reached end of the directory */
	break;
      } else if (op_ret == 0){
	/* TODO: 
	 *      1. convert value to stbuf and struct dirent
	 */
	if (key.data) {
	  this_size = bdb_dirent_size (&key);
	  if (this_size + filled > size)
	    break;
	  /* TODO - consider endianness here */
	  this_entry = (void *)(buf + filled);
	  this_entry->d_ino = bdb_inode_transform (stbuf.st_ino, recno);
	  this_entry->d_off = 0;
	  this_entry->d_type = 0;
	  this_entry->d_len = key.size;
	  strncpy (this_entry->d_name, (char *)key.data, key.size);
	  if (key.data)
	    free (key.data);
	  if (value.data)
	    free (value.data);

	  filled += this_size;
	  recno++;
	} else {
	  /* NOTE: currently ignore when we get key.data == NULL, TODO: we should not get key.data = NULL */
	  gf_log (this->name,
		  GF_LOG_DEBUG,
		  "null key read from db");
	}/* if(key.data)...else */
      } else {
	gf_log (this->name,
		GF_LOG_DEBUG,
		"database error during readdir");
	op_ret = -1;
	op_errno = ENOMEM;
	break;
      } /* if (op_ret == DB_NOTFOUND)...else if...else */
    }
  } /* while */

  frame->root->rsp_refs = NULL;
  gf_log (this->name,
	  GF_LOG_DEBUG,
	  "read %d bytes", filled);
  STACK_UNWIND (frame, filled, op_errno, buf);

  free (buf);
    
  return 0;
}


int32_t 
bdb_stats (call_frame_t *frame,
	     xlator_t *this,
	     int32_t flags)

{
  int32_t op_ret = 0;
  int32_t op_errno = 0;

  struct xlator_stats xlstats = {0, }, *stats = &xlstats;
  struct statvfs buf;
  struct timeval tv;
  struct bdb_private *priv = (struct bdb_private *)this->private;
  int64_t avg_read = 0;
  int64_t avg_write = 0;
  int64_t _time_ms = 0; 

    
  op_ret = statvfs (priv->base_path, &buf);
  op_errno = errno;
    
  
  stats->nr_files = priv->stats.nr_files;
  stats->nr_clients = priv->stats.nr_clients; /* client info is maintained at FSd */
  stats->free_disk = buf.f_bfree * buf.f_bsize; /* Number of Free block in the filesystem. */
  stats->total_disk_size = buf.f_blocks * buf.f_bsize; /* */
  stats->disk_usage = (buf.f_blocks - buf.f_bavail) * buf.f_bsize;

  /* Calculate read and write usage */
  gettimeofday (&tv, NULL);
  
  /* Read */
  _time_ms = (tv.tv_sec - priv->init_time.tv_sec) * 1000 +
             ((tv.tv_usec - priv->init_time.tv_usec) / 1000);

  avg_read = (_time_ms) ? (priv->read_value / _time_ms) : 0; /* KBps */
  avg_write = (_time_ms) ? (priv->write_value / _time_ms) : 0; /* KBps */
  
  _time_ms = (tv.tv_sec - priv->prev_fetch_time.tv_sec) * 1000 +
             ((tv.tv_usec - priv->prev_fetch_time.tv_usec) / 1000);
  if (_time_ms && ((priv->interval_read / _time_ms) > priv->max_read)) {
    priv->max_read = (priv->interval_read / _time_ms);
  }
  if (_time_ms && ((priv->interval_write / _time_ms) > priv->max_write)) {
    priv->max_write = priv->interval_write / _time_ms;
  }

  stats->read_usage = avg_read / priv->max_read;
  stats->write_usage = avg_write / priv->max_write;

  gettimeofday (&(priv->prev_fetch_time), NULL);
  priv->interval_read = 0;
  priv->interval_write = 0;

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, stats);
  return 0;
}

int32_t 
bdb_checksum (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t flag)
{
  char *real_path;
  DIR *dir;
  struct dirent *dirent;
  uint8_t file_checksum[4096] = {0,};
  uint8_t dir_checksum[4096] = {0,};
  int32_t op_ret = -1;
  int32_t op_errno = 2;
  int32_t i, length = 0;
  MAKE_REAL_PATH (real_path, this, loc->path);

  dir = opendir (real_path);
  
  if (!dir){
    gf_log (this->name, GF_LOG_DEBUG, 
	    "checksum: opendir() failed for `%s'", real_path);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, errno, NULL, NULL);
    return 0;
  } else {
    op_ret = 0;
    op_errno = 0;
  }

  /* TODO: read the filename in db file and send the checksum */
  
  while ((dirent = readdir (dir))) {
    //struct stat buf;

    if (!dirent)
      break;

    length = strlen (dirent->d_name);

    if (strcmp (dirent->d_name, "glusterfs.db") == 0) 
      continue;

    //lstat (dirent->d_name, &buf);
    
    for (i = 0; i < length; i++)
      dir_checksum[i] ^= dirent->d_name[i];
    {
      /* TODO: */
      /* There will be one file 'glusterfs.db' */
      /* retrive keys from it and send it accross */
    }
  }
  closedir (dir);

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, file_checksum, dir_checksum);

  return 0;
}

/**
 * notify - when parent sends PARENT_UP, send CHILD_UP event from here
 */
int32_t
notify (xlator_t *this,
        int32_t event,
        void *data,
        ...)
{
  switch (event)
    {
    case GF_EVENT_PARENT_UP:
      {
	/* Tell the parent that bdb xlator is up */
	default_notify (this, GF_EVENT_CHILD_UP, data);
      }
      break;
    default:
      /* */
      break;
    }
  return 0;
}

static inline int
bdb_init_ns_db (xlator_t *this,
		char *directory)
{
  /* create a namespace db entry for root */
  DB *ns_dbp = NULL;
  int32_t op_ret = -1, op_errno = ENOENT;
  char *ns_db_path = NULL;
  struct bdb_private *private = this->private;

  MAKE_REAL_PATH_TO_NS_DB (ns_db_path, this, "");  
  
  op_ret = db_create (&ns_dbp, private->dbenv, 0);
  if (op_ret != 0) {
    op_ret = -1;
    op_errno = ENOENT; /* DB failure, find appropriate errno */
  } else {
    op_ret = ns_dbp->set_flags (ns_dbp, DB_RECNUM);
    op_ret = ns_dbp->open (ns_dbp,
			   NULL,
			   ns_db_path,
			   NULL,
			   DB_BTREE,
			   /*DB_AUTO_COMMIT | */DB_CREATE,
			   0);
    if (op_ret != 0 ) {
      op_ret = -1;
      op_errno = ENOENT; /* DB failure, find appropriate errno */
    } else {
      /* direct put to database, so we can directly call ns_dbp->get() */
      DBT key, value;
      char path_name[] = "";
      //      DB_TXN *tid = NULL;
      memset (&key, 0, sizeof (key));
      memset (&value, 0, sizeof (value));
      
      key.data = basename (path_name);
      key.size = strlen ((char *)key.data);

      op_ret = ns_dbp->put (ns_dbp, NULL, &key, &value, 0/*DB_AUTO_COMMIT*/);
      if (!op_ret) {
	/* create successful */
	struct bdb_ctx *ctx = calloc (1, sizeof (*ctx));
	if (!ctx) {
	  op_ret = -1;
	  op_errno = ENOMEM;
	} else {
	  ctx->ns = ns_dbp;
	} /* if (!ctx)...else */
      } else {
	op_ret = -1;
	op_errno = ENOENT;
      } /* if (!ret)....else */
    } /* if(op_ret != 0)...else */
  } /* if(op_ret != 0)...else */
  ns_dbp->close (ns_dbp, 0);
  
  {
    /* create a dictionary, for keeping bdb_ctx corresponding to each of the directories */
    dict_t *bctx_dict = NULL;
    
    bctx_dict = get_new_dict ();
    if (!bctx_dict) {
      gf_log (this->name,
	      GF_LOG_ERROR,
	      "failed to get_new_dict() for bdb_ctx dictionary");
      op_ret = -1;
    } else {
      private->db_ctx = bctx_dict;
    }
  }
  
  return op_ret;
}


static inline DB_ENV *
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
      /* TODO: make DB_INIT_TXN to be configured through spec file */
      if ((ret = dbenv->open(dbenv, directory, DB_CREATE /*| DB_INIT_LOCK*/ | 
			     DB_INIT_LOG /*|DB_INIT_TXN*/ | DB_INIT_MPOOL | 
			     /*DB_RECOVER |*/ DB_THREAD, 
			     S_IRUSR | S_IWUSR)) != 0) {
	gf_log (this->name, GF_LOG_ERROR, 
		"Failed to open DB Environment (%d)", ret);
	dbenv = NULL;
      }
    }
  }
  
  return dbenv;
}

/**
 * init - 
 */
int32_t 
init (xlator_t *this)
{
  int32_t ret;
  struct stat buf;
  struct bdb_private *_private = calloc (1, sizeof (*_private));
  data_t *directory = dict_get (this->options, "directory");

  if (this->children) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "FATAL: storage/bdb cannot have subvolumes");
    freee (_private);
    return -1;
  }

  if (!directory) {
    gf_log (this->name, GF_LOG_ERROR,
	    "export directory not specified in spec file");
    freee (_private);
    return -1;
  }
  umask (000); // umask `masking' is done at the client side
  if (mkdir (directory->data, 0777) == 0) {
    gf_log (this->name, GF_LOG_WARNING,
	    "directory specified not exists, created");
  }
  /* Check whether the specified directory exists, if not create it. */
  ret = stat (directory->data, &buf);
  if (ret != 0 && !S_ISDIR (buf.st_mode)) {
    gf_log (this->name, GF_LOG_ERROR, 
	    "Specified directory doesn't exists, Exiting");
    freee (_private);
    return -1;
  }

  {
    _private->dbenv = bdb_init_db_env (this, directory->data);
    
    if (!_private->dbenv) {
      gf_log (this->name, GF_LOG_ERROR,
	      "failed to initialize db environment");
      freee (_private);
      return -1;
    }
  }

  _private->base_path = strdup (directory->data);
  _private->base_path_length = strlen (_private->base_path);


  {
    /* Stats related variables */
    gettimeofday (&_private->init_time, NULL);
    gettimeofday (&_private->prev_fetch_time, NULL);
    _private->max_read = 1;
    _private->max_write = 1;
  }

  this->private = (void *)_private;
  
  {
    ret = bdb_init_ns_db (this, directory->data);
    
    if (ret == -1){
      gf_log (this->name,
	      GF_LOG_DEBUG,
	      "failed to initialize namespace db");
      return -1;
    }
  }

  return 0;
}

static void
bdb_close_dbs_from_dict (dict_t *this,
			 char *key,
			 data_t *value,
			 void *data)
{
  struct bdb_ctx *bctx = data_to_ptr (value);
  
  if (bctx) {
    /* cleanup, by closing all the ns and storage dbs */
    if (bctx->ns) {
      bctx->ns->sync (bctx->ns, 0);
      bctx->ns->close (bctx->ns, 0);
    } else {
      /* do nothing */
    }
    if (bctx->storage) {
      bctx->storage->sync (bctx->storage, 0);
      bctx->storage->close (bctx->storage, 0);
    } else {
      /* do nothing */
    }
  } else {
    gf_log ("bdb",
	    GF_LOG_ERROR,
	    "unwanted entry in dictionary");
  }
}

void
fini (xlator_t *this)
{
  struct bdb_private *private = this->private;
  if (private->dbenv) {
    /* pick each of the 'struct bdb_ctx' from private->db_ctx and close all the databases that are open */
    dict_foreach (private->db_ctx, bdb_close_dbs_from_dict, NULL);
  } else {
    /* impossible to reach here */
  }

  freee (private);
  return;
}

struct xlator_mops mops = {
  .stats    = bdb_stats,
  .lock     = mop_lock_impl,
  .unlock   = mop_unlock_impl,
  .checksum = bdb_checksum,
};

struct xlator_fops fops = {
  .lookup      = bdb_lookup,
  .forget      = bdb_forget,
  .stat        = bdb_stat,
  .opendir     = bdb_opendir,
  .readdir     = bdb_readdir,
  .closedir    = bdb_closedir,
  .readlink    = bdb_readlink,
  .mknod       = bdb_mknod,
  .mkdir       = bdb_mkdir,
  .unlink      = bdb_unlink,
  .rmelem      = bdb_rmelem,
  .rmdir       = bdb_rmdir,
  .symlink     = bdb_symlink,
  .rename      = bdb_rename,
  .link        = bdb_link,
  .chmod       = bdb_chmod,
  .chown       = bdb_chown,
  .truncate    = bdb_truncate,
  .utimens     = bdb_utimens,
  .create      = bdb_create,
  .open        = bdb_open,
  .readv       = bdb_readv,
  .writev      = bdb_writev,
  .statfs      = bdb_statfs,
  .flush       = bdb_flush,
  .close       = bdb_close,
  .fsync       = bdb_fsync,
  .incver      = bdb_incver,
  .setxattr    = bdb_setxattr,
  .getxattr    = bdb_getxattr,
  .removexattr = bdb_removexattr,
  .fsyncdir    = bdb_fsyncdir,
  .access      = bdb_access,
  .ftruncate   = bdb_ftruncate,
  .fstat       = bdb_fstat,
  .lk          = bdb_lk,
  .fchown      = bdb_fchown,
  .fchmod      = bdb_fchmod,
  .setdents    = bdb_setdents,
  .getdents    = bdb_getdents,
};
