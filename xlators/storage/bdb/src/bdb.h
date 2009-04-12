/*
  Copyright (c) 2008-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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
#include "compat-errno.h"
#include "fd.h"
#include "syscall.h"

#define BDB_STORAGE    "/glusterfs_storage.db"

/* numbers are not so reader-friendly, so lets have ON and OFF macros */
#define ON  1
#define OFF 0

#define BDB_DEFAULT_LRU_LIMIT 100
#define BDB_DEFAULT_HASH_SIZE 100

#define BDB_ENOSPC_THRESHOLD 25600

#define BDB_DEFAULT_CHECKPOINT_INTERVAL 30

#define BCTX_ENV(bctx) (bctx->table->dbenv)

#define BDB_EXPORT_PATH_LEN(_private) \
        (((struct bdb_private *)_private)->export_path_length)

#define BDB_KEY_FROM_FREQUEST_KEY(_key) (&(key[15]))

#define BDB_EXPORT_PATH(_private) \
        (((struct bdb_private *)_private)->export_path)
/* MAKE_REAL_PATH(var,this,path)
 * make the real path on the underlying file-system
 *
 * @var:  destination to hold the real path
 * @this: pointer to xlator_t corresponding to bdb xlator
 * @path: path, as seen from mount-point
 */
#define MAKE_REAL_PATH(var, this, path) do {                            \
                int base_len = BDB_EXPORT_PATH_LEN(this->private);      \
                var = alloca (strlen (path) + base_len + 2);            \
                strcpy (var, BDB_EXPORT_PATH(this->private));           \
                strcpy (&var[base_len], path);                          \
        } while (0)


#define BDB_TIMED_LOG(_errno,_counter)  \
        ((_errno == ENOTSUP) && (((++_counter) % GF_UNIVERSAL_ANSWER) == 1))

#define GF_FILE_CONTENT_REQUEST ZR_FILE_CONTENT_REQUEST

/* MAKE_REAL_PATH_TO_STORAGE_DB(var,this,path)
 * make the real path to the storage-database file on file-system
 *
 * @var:  destination to hold the real path
 * @this: pointer to xlator_t corresponding to bdb xlator
 * @path: path of the directory, as seen from mount-point
 */
#define MAKE_REAL_PATH_TO_STORAGE_DB(var, this, path) do {              \
                int base_len = BDB_EXPORT_PATH_LEN(this->private);      \
                var = alloca (strlen (path) +                           \
                              base_len +                                \
                              strlen (BDB_STORAGE));                    \
                strcpy (var, BDB_EXPORT_PATH(this->private));           \
                strcpy (&var[base_len], path);                          \
                strcat (var, BDB_STORAGE);                              \
        } while (0)

/* MAKE_KEY_FROM_PATH(key,path)
 * make a 'key', which we use as key in the underlying database by using
 * the path
 *
 * @key:  destination to hold the key
 * @path: path to file as seen from mount-point
 */
#define MAKE_KEY_FROM_PATH(key, path) do {              \
                char *tmp = alloca (strlen (path));     \
                strcpy (tmp, path);                     \
                key = basename (tmp);                   \
        }while (0);

/* IS_BDB_PRIVATE_FILE(name)
 * check if a given 'name' is bdb xlator's internal file name
 *
 * @name: basename of a file.
 *
 * bdb xlator reserves file names 'glusterfs_storage.db',
 * 'glusterfs_ns.db'(used by bdb xlator itself), 'log.*', '__db.*'
 * (used by libdb)
 */
#define IS_BDB_PRIVATE_FILE(name) ((!strncmp(name, "__db.", 5)) ||      \
                                   (!strcmp(name, "glusterfs_storage.db")) || \
                                   (!strcmp(name, "glusterfs_ns.db")) || \
                                   (!strncmp(name, "log.0000", 8)))

/* check if 'name' is '.' or '..' entry */
#define IS_DOT_DOTDOT(name) \
        ((!strncmp(name,".", 1)) || (!strncmp(name,"..", 2)))

/* BDB_ICTX_SET(this,inode,bctx)
 * pointer to 'struct bdb_ctx' is stored in inode's ctx of all directories.
 * this will happen either in lookup() or mkdir().
 *
 * @this:  pointer xlator_t of bdb xlator.
 * @inode: inode where 'struct bdb_ctx *' has to be stored.
 * @bctx:  a 'struct bdb_ctx *'
 */
#define BDB_ICTX_SET(_inode,_this,_bctx) do{                            \
                inode_ctx_put(_inode, _this, (uint64_t)(long)_bctx);    \
        }while (0);

#define BDB_ICTX_GET(_inode,_this,_bctxp) do {                  \
                uint64_t tmp_bctx = 0;                          \
                inode_ctx_get (_inode, _this, &tmp_bctx);       \
                *_bctxp = tmp_bctx;                             \
        }while (0);

/* BDB_FCTX_SET(this,fd,bctx)
 * pointer to 'struct bdb_ctx' is stored in inode's ctx of all directories.
 * this will happen either in lookup() or mkdir().
 *
 * @this:  pointer xlator_t of bdb xlator.
 * @inode: inode where 'struct bdb_ctx *' has to be stored.
 * @bctx:  a 'struct bdb_ctx *'
 */
#define BDB_FCTX_SET(_fd,_this,_bfd) do{                        \
                fd_ctx_set(_fd, _this, (uint64_t)(long)_bfd);   \
        }while (0);

#define BDB_FCTX_GET(_fd,_this,_bfdp) do {              \
                uint64_t tmp_bfd = 0;                   \
                fd_ctx_get (_fd, _this, &tmp_bfd);      \
                *_bfdp = (void *)(long)tmp_bfd;         \
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

#define BDB_LL_PAGE_SIZE_DEFAULT    4096
#define BDB_LL_PAGE_SIZE_MIN        4096
#define BDB_LL_PAGE_SIZE_MAX        65536

#define PAGE_SIZE_IN_RANGE(_page_size)                  \
        ((_page_size >= BDB_LL_PAGE_SIZE_MIN)           \
         && (table->page_size <= BDB_LL_PAGE_SIZE_MAX))

typedef struct bctx_table bctx_table_t;
typedef struct bdb_ctx    bctx_t;
typedef struct bdb_cache  bdb_cache_t;
typedef struct bdb_private bdb_private_t;

struct bctx_table {
        /* flags to be used for opening each database */
        uint64_t            dbflags;

        /* cache: can be either ON or OFF */
        uint64_t            cache;

        /* used to lock the 'struct bctx_table *' */
        gf_lock_t           lock;

        /* lock for checkpointing */
        gf_lock_t           checkpoint_lock;

        /* hash table of 'struct bdb_ctx' */
        struct list_head   *b_hash;

        /* list of active 'struct bdb_ctx' */
        struct list_head    active;

        /* lru list of inactive 'struct bdb_ctx' */
        struct list_head    b_lru;
        struct list_head    purge;
        uint32_t            lru_limit;
        uint32_t            lru_size;
        uint32_t            hash_size;

        /* access mode for accessing the databases, can be DB_HASH, DB_BTREE */
        DBTYPE              access_mode;

        /* DB_ENV under which every db operation is carried over */
        DB_ENV             *dbenv;
        int32_t             transaction;
        xlator_t           *this;

        /* page-size of DB, DB->set_pagesize(), should be set before DB->open */
        uint64_t            page_size;
};

struct bdb_ctx {
        /* controller members */

        /* lru list of 'struct bdb_ctx's, a bdb_ctx can exist in one of
         * b_hash or lru lists */
        struct list_head   list;

        /* directory 'name' hashed list of 'struct bdb_ctx's */
        struct list_head   b_hash;

        struct bctx_table *table;
        int32_t            ref;         /* reference count */
        gf_lock_t          lock;        /* used to lock this 'struct bdb_ctx' */

        char              *directory;   /* directory path */

        /* pointer to open database, that resides inside this directory */
        DB                *primary;
        DB                *secondary;
        uint32_t           cache;       /* cache ON or OFF */

        /* per directory cache, bdb xlator's internal cache */
        struct list_head   c_list;      /* linked list of cached records */
        int32_t            c_count;     /* number of cached records */

        /* index to hash table list, to which this ctx belongs */
        int32_t            key_hash;
        char              *db_path;     /* absolute path to db file */
};

struct bdb_fd {
        /* pointer to bdb_ctx of the parent directory */
        struct bdb_ctx *ctx;

        /* name of the file. NOTE: basename, not the complete path */
        char           *key;
        int32_t         flags;          /* open flags */
};

struct bdb_dir {
        /* pointer to bdb_ctx of this directory */
        struct bdb_ctx *ctx;

        /* open directory pointer, as returned by opendir() */
        DIR            *dir;

        char           *path;             /* path to this directory */
};

/* cache */
struct bdb_cache {
        /* list of 'struct bdb_cache' under a 'struct bdb_ctx' */
        struct list_head c_list;

        /* name of the file this cache holds. NOTE: basename of file */
        char            *key;
        char            *data;            /* file content */

        /* size of the file content that this cache holds */
        size_t           size;
};


struct bdb_private {
        /* pointer to inode table that we use */
        inode_table_t      *itable;
        int32_t             temp;               /**/
        char                is_stateless;       /**/

        /* path to the export directory
         * (option directory <export-path>) */
        char               *export_path;

        /* length of 'export_path' string */
        int32_t             export_path_length;

        /* statistics */
        /* Statistics, provides activity of the server */
        struct xlator_stats stats;

        struct timeval      prev_fetch_time;
        struct timeval      init_time;
        int32_t             max_read;           /* */
        int32_t             max_write;          /* */

        /* Used to calculate the max_read value */
        int64_t             interval_read;

        /* Used to calculate the max_write value */
        int64_t             interval_write;
        int64_t             read_value;         /* Total read, from init */
        int64_t             write_value;        /* Total write, from init */

        /* bdb xlator specific private data */

        /* flags used for opening DB_ENV for this xlator */
        uint64_t            envflags;

        /* flags to be used for opening each database */
        uint64_t            dbflags;

        /* cache: can be either ON or OFF */
        uint64_t            cache;

        /* transaction: can be either ON or OFF */
        uint32_t            transaction;
        uint32_t            active;
        gf_lock_t           active_lock;
        struct bctx_table  *b_table;

        /* access mode for accessing the databases, can be DB_HASH, DB_BTREE
         * (option access-mode <mode>) */
        DBTYPE              access_mode;

        /* mode for each and every file stored on bdb
         * (option file-mode <mode>) */
        mode_t              file_mode;

        /* mode for each and every directory stored on bdb
         * (option dir-mode <mode>) */
        mode_t              dir_mode;

        /* mode for each and every symlink stored on bdb */
        mode_t              symlink_mode;

        /* pthread_t object used for creating checkpoint thread */
        pthread_t           checkpoint_thread;

        /* time duration between two consecutive checkpoint operations.
         * (option checkpoint-interval <time-in-seconds>) */
        uint32_t             checkpoint_interval;

        /* environment log directory (option logdir <directory>) */
        char               *logdir;

        /* errfile path, used by environment to print detailed error log.
         * (option errfile <errfile-path>) */
        char               *errfile;

        /* DB_ENV->set_errfile() expects us to fopen
         * the errfile before doing DB_ENV->set_errfile() */
        FILE               *errfp;

       /* used by DB_ENV->set_timeout to set the timeout for
        * a transactionally encapsulated DB->operation() to
        * timeout before waiting for locks to be released.
        * (option transaction-timeout <time-in-milliseconds>)
        */
        uint32_t            txn_timeout;
        uint32_t            lock_timeout;

        /* DB_AUTO_LOG_REMOVE flag for DB_ENV*/
        uint32_t            log_auto_remove;
        uint32_t            log_region_max;
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

void *
bdb_db_stat (bctx_t *bctx,
             DB_TXN *txnid,
             uint32_t flags);

/*int32_t
bdb_db_get(struct bdb_ctx *bctx,
           DB_TXN *txnid,
           const char *key_string,
           char **buf,
           size_t size,
           off_t offset);
*/
int32_t
bdb_db_fread (struct bdb_fd *bfd, char *bufp, size_t size, off_t offset);

int32_t
bdb_db_iread (struct bdb_ctx *bctx, const char *key, char **bufp);

#define BDB_TRUNCATE_RECORD 0xcafebabe

/*int32_t
bdb_db_put (struct bdb_ctx *bctx,
            DB_TXN *txnid,
            const char *key_string,
            const char *buf,
            size_t size,
            off_t offset,
            int32_t flags);
*/
int32_t
bdb_db_icreate (struct bdb_ctx *bctx, const char *key);

int32_t
bdb_db_fwrite (struct bdb_fd *bfd, char *buf, size_t size, off_t offset);

int32_t
bdb_db_iwrite (struct bdb_ctx *bctx, const char *key, char *buf, size_t size);

int32_t
bdb_db_itruncate (struct bdb_ctx *bctx, const char *key);

int32_t
bdb_db_iremove (struct bdb_ctx *bctx,
                const char *key);

ino_t
bdb_inode_transform (ino_t parent,
                     const char *name,
                     size_t namelen);

int32_t
bdb_cursor_open (struct bdb_ctx *bctx,
                 DBC **cursorp);

int32_t
bdb_cursor_get (DBC *cursorp,
                DBT *sec, DBT *pri,
                DBT *value,
                int32_t flags);


int32_t
bdb_cursor_close (struct bdb_ctx *ctx,
                  DBC *cursorp);


int32_t
bdb_dirent_size (DBT *key);

int32_t
dirent_size (struct dirent *entry);

int
bdb_db_init (xlator_t *this,
             dict_t *options);

void
bdb_dbs_from_dict_close (dict_t *this,
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
