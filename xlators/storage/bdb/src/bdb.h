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

#ifndef _BDB_H
#define _BDB_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>

#include <db.h>

#ifdef linux
#ifdef __GLIBC__
#include <sys/fsuid.h>
#else
#include <unistd.h>
#endif
#endif

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#ifdef HAVE_SYS_EXTATTR_H
#include <sys/extattr.h>
#endif

#include <pthread.h>
#include "xlator.h"
#include "inode.h"
#include "compat.h"

#define GLFS_BDB_STORAGE    "/glusterfs_storage.db"

/* numbers are not so reader-friendly, so lets have ON and OFF macros */
#define ON  1
#define OFF 0

#define BDB_DEFAULT_LRU_LIMIT 100
#define BDB_DEFAULT_HASH_SIZE 100

#define BDB_DEFAULT_CHECKPOINT_TIMEOUT 30
/* MAKE_REAL_PATH(var,this,path)
 * make the real path on the underlying file-system
 *
 * @var:  destination to hold the real path
 * @this: pointer to xlator_t corresponding to bdb xlator 
 * @path: path, as seen from mount-point 
 */
#define MAKE_REAL_PATH(var, this, path) do {                              \
  int base_len = ((struct bdb_private *)this->private)->export_path_length; \
  var = alloca (strlen (path) + base_len + 2);                            \
  strcpy (var, ((struct bdb_private *)this->private)->export_path);         \
  strcpy (&var[base_len], path);                                          \
} while (0)

/* MAKE_REAL_PATH_TO_STORAGE_DB(var,this,path)
 * make the real path to the storage-database file on file-system
 *
 * @var:  destination to hold the real path
 * @this: pointer to xlator_t corresponding to bdb xlator 
 * @path: path of the directory, as seen from mount-point 
 */
#define MAKE_REAL_PATH_TO_STORAGE_DB(var, this, path) do {                \
  int base_len = ((struct bdb_private *)this->private)->export_path_length; \
  var = alloca (strlen (path) + base_len + strlen (GLFS_BDB_STORAGE));    \
  strcpy (var, ((struct bdb_private *)this->private)->export_path);         \
  strcpy (&var[base_len], path);                                          \
  strcat (var, GLFS_BDB_STORAGE);                                         \
} while (0)

/* MAKE_KEY_FROM_PATH(key,path)
 * make a 'key', which we use as key in the underlying database by using the path
 *
 * @key:  destination to hold the key
 * @path: path to file as seen from mount-point 
 */
#define MAKE_KEY_FROM_PATH(key, path) do { \
  char *tmp = alloca (strlen (path));      \
  strcpy (tmp, path);                      \
  key = basename (tmp);                    \
}while (0);

/* BDB_DO_LSTAT(path,stbuf,dirent)
 * construct real-path to a dirent and do lstat on the real-path
 *
 * @path:   path to the directory whose readdir is currently in progress
 * @stbuf:  a 'struct stat *'
 * @dirent: a 'struct dirent *'
 */
#define BDB_DO_LSTAT(path, stbuf, dirent) do {   \
 char tmp_real_path[4096];                       \
 strcpy(tmp_real_path, path);                    \
 strcat (tmp_real_path, "/");                    \
 strcat(tmp_real_path, dirent->d_name);          \
 ret = lstat (tmp_real_path, stbuf);             \
} while(0);

/* IS_BDB_PRIVATE_FILE(name)
 * check if a given 'name' is bdb xlator's internal file name 
 *
 * @name: basename of a file.
 *
 * bdb xlator reserves file names 'glusterfs_storage.db', 
 * 'glusterfs_ns.db'(used by bdb xlator itself), 'log.*', '__db.*' (used by libdb)
 */
#define IS_BDB_PRIVATE_FILE(name) ((!strncmp(name, "__db.", 5)) || \
                                   (!strcmp(name, "glusterfs_storage.db")) || \
                                   (!strcmp(name, "glusterfs_ns.db")) || \
                                   (!strncmp(name, "log.0000", 8)))

/* check if 'name' is '.' or '..' entry */
#define IS_DOT_DOTDOT(name) ((!strncmp(name,".", 1)) || (!strncmp(name,"..", 2)))

/* BDB_SET_BCTX(this,inode,bctx)
 * put a stamp on inode. d00d, you are using bdb.. huhaha.
 * pointer to 'struct bdb_ctx' is stored in inode->ctx of all directories. 
 * this will happen either in lookup() or mkdir().
 *
 * @this:  pointer xlator_t of bdb xlator.
 * @inode: inode to whose ->ctx, 'struct bdb_ctx *' has to be stored.
 * @bctx:  a 'struct bdb_ctx *'
 */
#define BDB_SET_BCTX(this,inode,bctx) do{\
   dict_set(inode->ctx, this->name, data_from_static_ptr (bctx));\
}while (0);

/* MAKE_BCTX_FROM_INODE(this,bctx,inode)
 * extract bdb xlator's 'struct bdb_ctx *' from an inode->ctx.
 * valid only if done for directory inodes, otherwise bctx = NULL.
 *
 * @this:  pointer xlator_t of bdb xlator.
 * @bctx:  a 'struct bdb_ctx *'
 * @inode: inode from whose ->ctx, 'struct bdb_ctx *' has to be extracted. 
 */
#define MAKE_BCTX_FROM_INODE(this,bctx,inode) do{\
   data_t *data = dict_get (inode->ctx, this->name);\
   bctx = data_to_ptr (data); \
}while (0);

#define BDB_SET_BFD(this,fd,bfd) do{\
   dict_set(fd->ctx, this->name, data_from_static_ptr (bfd));\
}while (0);

/* maximum number of open dbs that bdb xlator will ever have */
#define BDB_MAX_OPEN_DBS 100

/* convert file size to block-count */
#define BDB_COUNT_BLOCKS(size,blksize) (((size + blksize - 1)/blksize) - 1)

/* file permissions, again macros are more readable */
#define RWXRWXRWX         0777
#define DEFAULT_FILE_MODE 0644
#define DEFAULT_DIR_MODE  0755

/* see, if have a valid file permissions specification in @mode */
#define IS_VALID_FILE_MODE(mode) (!(mode & (~RWXRWXRWX)))
#define IS_VALID_DIR_MODE(mode)  (!(mode & (~(RWXRWXRWX)))

/* maximum retries for a failed transactional operation */		  
#define BDB_MAX_RETRIES 10

#define ALIGN(x) (((x) + sizeof (uint64_t) - 1) & ~(sizeof (uint64_t) - 1))

typedef struct bctx_table bctx_table_t;
typedef struct bdb_ctx    bctx_t;
typedef struct bdb_cache  bdb_cache_t;
typedef struct bdb_private bdb_private_t;
			 
struct bctx_table {
  uint64_t            dbflags;               /* flags to be used for opening each database */
  uint64_t            cache;                 /* cache: can be either ON or OFF */
  gf_lock_t           lock;                  /* lock */
  gf_lock_t           checkpoint_lock;       /* lock for checkpointing */
  struct list_head   *b_hash;                /* hash table of 'struct bdb_ctx' */
  struct list_head    active;
  struct list_head    b_lru;                 /* lru list of 'struct bdb_ctx' */
  struct list_head    purge;
  uint32_t            lru_limit;
  uint32_t            lru_size;
  uint32_t            hash_size;
  DBTYPE              access_mode;           /* access mode for accessing the databases, 
					      * can be DB_HASH, DB_BTREE */
  DB_ENV             *dbenv;                 /* DB_ENV under which every db operation is carried over */
  int32_t             transaction;
  xlator_t           *this;
};

struct bdb_ctx {
  /* controller members */
  struct list_head list;            /* directory 'name' hashed list of 'struct bdb_ctx's */
                                    /* lru list of 'struct bdb_ctx's, 
				     * a bdb_ctx can exist in one of b_hash or lru lists */
  struct list_head b_hash;

  struct bctx_table *table;
  int32_t          ref;             /* reference count */
  gf_lock_t        lock;            /* lock */

  char            *directory;       /* directory path */
  DB              *dbp;             /* pointer to open database, that resides inside this directory */
  uint64_t         iseed;           /* current inode number seed, starts with 1. 
				     * see bdb_inode_transform() for usage of iseed */
  uint32_t         cache;           /* cache ON or OFF */
  /* per directory cache, bdb xlator's internal cache */
  struct list_head c_list;          /* linked list of cached records */
  int32_t          c_count;         /* number of cached records */
  int32_t          key_hash;
  char            *db_path;
};

struct bdb_fd {
  struct bdb_ctx *ctx;              /* pointer to bdb_ctx of the parent directory */
  char           *key;              /* name of the file. NOTE: basename, not the complete path */
  int32_t         flags;            /* open flags */
};

struct bdb_dir {
  struct bdb_ctx *ctx;              /* pointer to bdb_ctx of this directory */
  DIR            *dir;              /* open directory pointer, as returned by opendir() */
  char            offset[NAME_MAX]; /* FIXME: readdir offset, too crude. must go  */
  char           *path;             /* path to this directory */
};

/* cache */
struct bdb_cache {
  struct list_head c_list;          /* list of 'struct bdb_cache' under a 'struct bdb_ctx' */
  char            *key;             /* name of the file this cache holds. NOTE: basename of file */
  char            *data;            /* file content */
  size_t           size;            /* size of the file content that this cache holds */
};


struct bdb_private {
  inode_table_t      *itable;            /* pointer to inode table that we use */
  int32_t             temp;              /**/
  char                is_stateless;      /**/
  char               *export_path;       /* path to the export directory */
  int32_t             export_path_length;/* length of 'export_path' string */

  /* statistics */
  struct xlator_stats stats;             /* Statistics, provides activity of the server */
  
  struct timeval      prev_fetch_time;
  struct timeval      init_time;
  int32_t             max_read;          /* */
  int32_t             max_write;         /* */
  int64_t             interval_read;     /* Used to calculate the max_read value */
  int64_t             interval_write;    /* Used to calculate the max_write value */
  int64_t             read_value;        /* Total read, from init */
  int64_t             write_value;       /* Total write, from init */
  
  /* bdb xlator specific private data */
  uint64_t            envflags;              /* flags used for opening DB_ENV for this xlator */ 
  uint64_t            dbflags;               /* flags to be used for opening each database */
  uint64_t            cache;                 /* cache: can be either ON or OFF */
  uint32_t            transaction;           /* transaction: can be either ON or OFF */
  uint32_t            inode_bit_shift;       /* number of bits to be left shifted. 
					      * see bdb_inode_transform() for details */
  struct bctx_table  *b_table;
  DBTYPE              access_mode;           /* access mode for accessing the databases, 
					      * can be DB_HASH, DB_BTREE */
  mode_t              file_mode;             /* mode for each and every file stored on bdb */
  mode_t              dir_mode;              /* mode for each and every directory stored on bdb */
  mode_t              symlink_mode;          /* mode for each and every symlink stored on bdb */
  pthread_t           checkpoint_thread;
  int32_t             checkpoint_timeout;
  ino_t               next_ino;
  gf_lock_t           ino_lock;
  char               *logdir;
};


static inline int32_t 
bdb_txn_begin (DB_ENV *dbenv,
	       DB_TXN **ptxnid)
{
  return dbenv->txn_begin (dbenv, NULL, ptxnid, 0);
}

static inline int32_t
bdb_txn_abort (DB_TXN *txnid)
{
 return txnid->abort (txnid);
}

static inline int32_t
bdb_txn_commit (DB_TXN *txnid)
{
  return txnid->commit (txnid, 0);
}

inline void *
bdb_extract_bfd (fd_t *fd,
		 char *name);


int32_t
bdb_storage_get(struct bdb_ctx *bctx,
		DB_TXN *txnid,
		const char *key_string,
		char **buf,
		size_t size,
		off_t offset);

#define BDB_TRUNCATE_RECORD 0xcafebabe

int32_t
bdb_storage_put (struct bdb_ctx *bctx,
		 DB_TXN *txnid,
		 const char *key_string,
		 const char *buf,
		 size_t size,
		 off_t offset,
		 int32_t flags);

int32_t
bdb_storage_del (struct bdb_ctx *bctx,
		 DB_TXN *txnid,
		 const char *path);

ino_t
bdb_inode_transform (ino_t parent,
		     struct bdb_ctx *bctx);


int32_t
bdb_open_db_cursor (struct bdb_ctx *bctx,
		    DBC **cursorp);

int32_t
bdb_cursor_get (DBC *cursorp,
		DBT *key,
		DBT *value,
		int32_t flags);


int32_t
bdb_close_db_cursor (struct bdb_ctx *ctx,
		     DBC *cursorp);


int32_t
bdb_dirent_size (DBT *key);

int32_t
dirent_size (struct dirent *entry);

int
bdb_init_db (xlator_t *this,
	     dict_t *options);

void
bdb_close_dbs_from_dict (dict_t *this,
			 char *key,
			 data_t *value,
			 void *data);

bctx_t *
bctx_lookup (struct bctx_table *table,
	     const char *path);

bctx_t *
bctx_parent
 (struct bctx_table *table,
	     const char *path);

bctx_t *
bctx_unref (bctx_t *ctx);

bctx_t *
bctx_ref (bctx_t *ctx);


#endif /* _BDB_H */
