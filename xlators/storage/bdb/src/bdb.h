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

#define MAKE_REAL_PATH(var, this, path) do {                              \
  int base_len = ((struct bdb_private *)this->private)->base_path_length; \
  var = alloca (strlen (path) + base_len + 2);                            \
  strcpy (var, ((struct bdb_private *)this->private)->base_path);         \
  strcpy (&var[base_len], path);                                          \
} while (0)

#define MAKE_REAL_PATH_TO_STORAGE_DB(var, this, path) do {                \
  int base_len = ((struct bdb_private *)this->private)->base_path_length; \
  var = alloca (strlen (path) + base_len + strlen (GLFS_BDB_STORAGE));    \
  strcpy (var, ((struct bdb_private *)this->private)->base_path);         \
  strcpy (&var[base_len], path);                                          \
  strcat (var, GLFS_BDB_STORAGE);                                         \
} while (0)

#define MAKE_KEY_FROM_PATH(key, path) do { \
  char *tmp = alloca (strlen (path));      \
  strcpy (tmp, path);                      \
  key = basename (tmp);                    \
}while (0);

#define BDB_DO_LSTAT(path, buf, dirent) do {   \
 char tmp_real_path[4096];                     \
 strcpy(tmp_real_path, path);                  \
 strcat (tmp_real_path, "/");                  \
 strcat(tmp_real_path, dirent->d_name);        \
 lstat (tmp_real_path, buf);                   \
} while(0);

#define IS_BDB_PRIVATE_FILE(name) ((!strcmp(name, "__db.001")) || \
                                   (!strcmp(name, "__db.002")) || \
                                   (!strcmp(name, "__db.003")) || \
                                   (!strcmp(name, "__db.004")) || \
                                   (!strcmp(name, "glusterfs_storage.db")) || \
                                   (!strcmp(name, "glusterfs_ns.db")) || \
                                   (!strcmp(name, "log.0000000001")))


#define BDB_SET_BCTX(this,inode,bctx) do{\
   dict_set(inode->ctx, this->name, data_from_static_ptr (bctx));\
}while (0);

#define MAKE_BCTX_FROM_INODE(this,bctx,inode) do{\
   data_t *data = dict_get (inode->ctx, this->name);\
   bctx = data_to_ptr (data); \
}while (0);

#define BDB_SET_BFD(this,fd,bfd) do{\
   dict_set(fd->ctx, this->name, data_from_static_ptr (bfd));\
}while (0);

#define CHILD_INO_RANGE_BITS 32

#define BDB_MAKE_INO(ino, recno)  ((ino << CHILD_INO_RANGE_BITS) | recno)

#define ALIGN(x) (((x) + sizeof (uint64_t) - 1) & ~(sizeof (uint64_t) - 1))

#define BDB_HASH_SIZE 20

#define BDB_MAX_OPEN_DBS 100

struct bdb_ctx {
  struct list_head b_hash;
  struct list_head lru;
  char *directory;
  DB *dbp;
  uint64_t iseed;
  int32_t ref;
  gf_lock_t lock;
  struct list_head c_list; /* linked list of cached records */
  int32_t cache_full;
};

struct bdb_fd {
  struct bdb_ctx *ctx;
  char *key;
  int32_t flags;
};

struct bdb_dir {
  DIR *dir;
  char *key;
  DBC *cursorp;
  char *path;
  struct bdb_ctx *ctx;
};

/* caching */
struct bdb_cache {
  struct list_head c_list;
  char *key;
  char *data;
  size_t size;
};


struct bdb_private {
  DB_ENV *dbenv;
  inode_table_t *itable;
  int32_t temp;
  char is_stateless;
  char *base_path;
  int32_t base_path_length;

  struct xlator_stats stats; /* Statistics, provides activity of the server */
  
  struct timeval prev_fetch_time;
  struct timeval init_time;
  int32_t max_read;            /* */
  int32_t max_write;           /* */
  int64_t interval_read;      /* Used to calculate the max_read value */
  int64_t interval_write;     /* Used to calculate the max_write value */
  int64_t read_value;    /* Total read, from init */
  int64_t write_value;   /* Total write, from init */

  dict_t *db_ctx;
  struct list_head b_hash[BDB_HASH_SIZE];
  struct list_head b_lru;
  int32_t open_dbs;
};

inline void *
bdb_extract_bfd (xlator_t *this,
		 fd_t *fd);

struct bdb_ctx *
bdb_ctx_unref (struct bdb_ctx *ctx);

struct bdb_ctx *
bdb_ctx_ref (struct bdb_ctx *ctx);

struct bdb_ctx *
bdb_get_bctx_from (xlator_t *this,
		   const char *path);

int32_t
bdb_storage_get (xlator_t *this,
		 struct bdb_ctx *bctx,
		 const char *key_string,
		 char **buf,
		 size_t size,
		 off_t offset);

#define BDB_TRUNCATE_RECORD 0xcafebabe

int32_t
bdb_storage_put (xlator_t *this,
		 struct bdb_ctx *bctx,
		 const char *key_string,
		 const char *buf,
		 size_t size,
		 off_t offset,
		 int32_t flags);

int32_t
bdb_storage_del (xlator_t *this,
		 struct bdb_ctx *bctx,
		 const char *path);

ino_t
bdb_inode_transform (ino_t parent,
		     struct bdb_ctx *bctx);


int32_t
bdb_open_db_cursor (xlator_t *this,
		    struct bdb_ctx *bctx,
		    DBC **cursorp);

int32_t
bdb_cursor_get (DBC *cursorp,
		DBT *key,
		DBT *value,
		int32_t flags);


int32_t
bdb_close_db_cursor (xlator_t *this,
		     struct bdb_ctx *ctx,
		     DBC *cursorp);


int32_t
bdb_dirent_size (DBT *key);

int32_t
dirent_size (struct dirent *entry);

int
bdb_init_db (xlator_t *this,
		char *directory);

DB_ENV *
bdb_init_db_env (xlator_t *this,
		 char *directory);

void
bdb_close_dbs_from_dict (dict_t *this,
			 char *key,
			 data_t *value,
			 void *data);

struct bdb_ctx *
bdb_lookup_ctx (xlator_t *this,
		char *path);

int32_t 
bdb_add_ctx (xlator_t *this,
	     struct bdb_ctx *bctx);

int32_t 
bdb_remove_ctx (xlator_t *this,
		struct bdb_ctx *bctx);

#endif /* _BDB_H */
