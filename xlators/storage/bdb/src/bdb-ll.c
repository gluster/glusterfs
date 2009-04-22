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

#include <libgen.h>
#include "bdb.h"
#include <list.h>
#include "hashfn.h"
/*
 * implement the procedures to interact with bdb */

/****************************************************************
 *
 * General wrappers and utility procedures for bdb xlator
 *
 ****************************************************************/

ino_t
bdb_inode_transform (ino_t parent,
                     const char *name,
                     size_t namelen)
{
        ino_t               ino = -1;
        uint64_t            hash = 0;

        hash = gf_dm_hashfn (name, namelen);

        ino = (((parent << 32) | 0x00000000ffffffffULL)
               & (hash | 0xffffffff00000000ULL));

        return ino;
}

static int
bdb_generate_secondary_hash (DB *secondary,
                             const DBT *pkey,
                             const DBT *data,
                             DBT *skey)
{
        char *primary = NULL;
        uint32_t *hash = NULL;

        primary = pkey->data;

        hash = calloc (1, sizeof (uint32_t));

        *hash = gf_dm_hashfn (primary, pkey->size);

        skey->data = hash;
        skey->size = sizeof (hash);
        skey->flags = DB_DBT_APPMALLOC;

        return 0;
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
 */
static int
bdb_db_open (bctx_t *bctx)
{
        DB *primary   = NULL;
        DB *secondary = NULL;
        int32_t ret = -1;
        bctx_table_t *table = NULL;

        GF_VALIDATE_OR_GOTO ("bdb-ll", bctx, out);

        table = bctx->table;
        GF_VALIDATE_OR_GOTO ("bdb-ll", table, out);

        /* we have to do the following, we can't deny someone of db_open ;) */
        ret = db_create (&primary, table->dbenv, 0);
        if (ret < 0) {
                gf_log ("bdb-ll", GF_LOG_DEBUG,
                        "_BDB_DB_OPEN %s: %s (failed to create database object"
                        " for primary database)",
                        bctx->directory, db_strerror (ret));
                ret = -ENOMEM;
                goto out;
        }

        if (table->page_size) {
                ret = primary->set_pagesize (primary,
                                             table->page_size);
                if (ret < 0) {
                        gf_log ("bdb-ll", GF_LOG_DEBUG,
                                "_BDB_DB_OPEN %s: %s (failed to set page-size "
                                "to %"PRIu64")",
                                bctx->directory, db_strerror (ret),
                                table->page_size);
                } else {
                        gf_log ("bdb-ll", GF_LOG_DEBUG,
                                "_BDB_DB_OPEN %s: page-size set to %"PRIu64,
                                bctx->directory, table->page_size);
                }
        }

        ret = primary->open (primary, NULL, bctx->db_path, "primary",
                             table->access_mode, table->dbflags, 0);
        if (ret < 0) {
                gf_log ("bdb-ll", GF_LOG_ERROR,
                        "_BDB_DB_OPEN %s: %s "
                        "(failed to open primary database)",
                        bctx->directory, db_strerror (ret));
                ret = -1;
                goto cleanup;
        }

        ret = db_create (&secondary, table->dbenv, 0);
        if (ret < 0) {
                gf_log ("bdb-ll", GF_LOG_DEBUG,
                        "_BDB_DB_OPEN %s: %s (failed to create database object"
                        " for secondary database)",
                        bctx->directory, db_strerror (ret));
                ret = -ENOMEM;
                goto cleanup;
        }

        ret = secondary->open (secondary, NULL, bctx->db_path, "secondary",
                               table->access_mode, table->dbflags, 0);
        if (ret != 0 ) {
                gf_log ("bdb-ll", GF_LOG_ERROR,
                        "_BDB_DB_OPEN %s: %s "
                        "(failed to open secondary database)",
                        bctx->directory, db_strerror (ret));
                ret = -1;
                goto cleanup;
        }

        ret = primary->associate (primary, NULL, secondary,
                                  bdb_generate_secondary_hash,
#ifdef DB_IMMUTABLE_KEY
                                  DB_IMMUTABLE_KEY);
#else
                                  0);
#endif
        if (ret != 0 ) {
                gf_log ("bdb-ll", GF_LOG_ERROR,
                        "_BDB_DB_OPEN %s: %s "
                        "(failed to associate primary database with "
                        "secondary database)",
                        bctx->directory, db_strerror (ret));
                ret = -1;
                goto cleanup;
        }

out:
        bctx->primary = primary;
        bctx->secondary = secondary;

        return ret;
cleanup:
        if (primary)
                primary->close (primary, 0);
        if (secondary)
                secondary->close (secondary, 0);

        return ret;
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
                if (ret < 0) {
                        gf_log ("bdb-ll", GF_LOG_DEBUG,
                                "_BDB_CURSOR_CLOSE %s: %s "
                                "(failed to close database cursor)",
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
                if (bctx->secondary) {
                        /* do nothing, just continue */
                        ret = 0;
                } else {
                        ret = bdb_db_open (bctx);
                        if (ret < 0) {
                                gf_log ("bdb-ll", GF_LOG_DEBUG,
                                        "_BDB_CURSOR_OPEN %s: ENOMEM "
                                        "(failed to open secondary database)",
                                        bctx->directory);
                                ret = -ENOMEM;
                        } else {
                                ret = 0;
                        }
                }

                if (ret == 0) {
                        /* all set, open cursor */
                        ret = bctx->secondary->cursor (bctx->secondary,
                                                       NULL, cursorpp, 0);
                        if (ret < 0) {
                                gf_log ("bdb-ll", GF_LOG_DEBUG,
                                        "_BDB_CURSOR_OPEN %s: %s "
                                        "(failed to open a cursor to database)",
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
                                bcache = list_entry (bctx->c_list.prev,
                                                     bdb_cache_t, c_list);
                                list_del_init (&bcache->c_list);
                        }
                        if (bcache->key) {
                                free (bcache->key);
                                bcache->key = calloc (key->size + 1,
                                                      sizeof (char));
                                GF_VALIDATE_OR_GOTO ("bdb-ll",
                                                     bcache->key, unlock);
                                memcpy (bcache->key, (char *)key->data,
                                        key->size);
                        } else {
                                /* should never come here */
                                gf_log ("bdb-ll", GF_LOG_DEBUG,
                                        "_BDB_CACHE_INSERT %s (%s) "
                                        "(found a cache entry with empty key)",
                                        bctx->directory, (char *)key->data);
                        } /* if(bcache->key)...else */
                        if (bcache->data) {
                                free (bcache->data);
                                bcache->data = memdup (data->data, data->size);
                                GF_VALIDATE_OR_GOTO ("bdb-ll", bcache->data,
                                                     unlock);
                                bcache->size = data->size;
                        } else {
                                /* should never come here */
                                gf_log ("bdb-ll", GF_LOG_CRITICAL,
                                        "_BDB_CACHE_INSERT %s (%s) "
                                        "(found a cache entry with no data)",
                                        bctx->directory, (char *)key->data);
                        } /* if(bcache->data)...else */
                        list_add (&bcache->c_list, &bctx->c_list);
                        ret = 0;
                } else {
                        /* we will be entering here very rarely */
                        bcache = CALLOC (1, sizeof (*bcache));
                        GF_VALIDATE_OR_GOTO ("bdb-ll", bcache, unlock);

                        bcache->key = calloc (key->size + 1, sizeof (char));
                        GF_VALIDATE_OR_GOTO ("bdb-ll", bcache->key, unlock);
                        memcpy (bcache->key, key->data, key->size);

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
                  const char *key)
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
                if (bctx->primary == NULL) {
                        ret = bdb_db_open (bctx);
                        storage = bctx->primary;
                } else {
                        /* we are just fine, lets continue */
                        storage = bctx->primary;
                } /* if(bctx->dbp==NULL)...else */
        }
        UNLOCK (&bctx->lock);

        GF_VALIDATE_OR_GOTO ("bdb-ll", storage, out);

        ret = storage->stat (storage, txnid, &stat, flags);

        if (ret < 0) {
                gf_log ("bdb-ll", GF_LOG_DEBUG,
                        "_BDB_DB_STAT %s: %s "
                        "(failed to do stat database)",
                        bctx->directory, db_strerror (ret));
        }
out:
        return stat;

}

/* bdb_storage_get - retrieve a key/value pair corresponding to @path from the
 *  corresponding db file.
 *
 * @bctx: bctx_t * corresponding to the parent directory of @path. (should
 *  always be a valid bctx).  bdb_storage_get should never be called if
 *  @bctx = NULL.
 * @txnid: NULL if bdb_storage_get is not embedded in an explicit transaction
 *  or a valid DB_TXN *, when embedded in an explicit transaction.
 * @path: path of the file to read from (translated to a database key using
 *  MAKE_KEY_FROM_PATH)
 * @buf: char ** - pointer to a pointer to char. a read buffer is created in
 *  this procedure and pointer to the buffer is passed through @buf to the
 *  caller.
 * @size: size of the file content to be read.
 * @offset: offset from which the file content to be read.
 *
 * NOTE: bdb_storage_get tries to open DB, if @bctx->dbp == NULL
 *  (@bctx->dbp == NULL, nobody has opened DB till now or DB was closed by
 *  bdb_table_prune()).
 *
 * NOTE: if private->cache is set (bdb xlator's internal caching enabled), then
 *  bdb_storage_get first looks up the cache for key/value pair. if
 *  bdb_lookup_cache fails, then only DB->get() is called. also,  inserts a
 *  newly read key/value pair to cache through bdb_insert_to_cache.
 *
 * return: 'number of bytes read' on success or -1 on error.
 *
 * also see: bdb_lookup_cache, bdb_insert_to_cache for details about bdb
 *  xlator's internal cache.
 */
static int32_t
bdb_db_get (bctx_t *bctx,
            DB_TXN *txnid,
            const char *path,
            char *buf,
            size_t size,
            off_t offset)
{
        DB          *storage    = NULL;
        DBT          key        = {0,};
        DBT          value      = {0,};
        int32_t      ret        = -1;
        size_t       copy_size  = 0;
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
                        copy_size = ((bcache->size - offset) < size)?
                                (bcache->size - offset) : size;

                        memcpy (buf, (bcache->data + offset), copy_size);
                        ret = copy_size;
                } else {
                        ret = bcache->size;
                }
                
                goto out;
        } 

        LOCK (&bctx->lock);
        {
                if (bctx->primary == NULL) {
                        ret = bdb_db_open (bctx);
                        storage = bctx->primary;
                } else {
                        /* we are just fine, lets continue */
                        storage = bctx->primary;
                } /* if(bctx->dbp==NULL)...else */
        }
        UNLOCK (&bctx->lock);

        GF_VALIDATE_OR_GOTO ("bdb-ll", storage, out);

        key.data = (char *)key_string;
        key.size = strlen (key_string);
        key.flags = DB_DBT_USERMEM;

        if (bctx->cache){
                value.flags = DB_DBT_MALLOC;
        } else {
                if (size) {
                        value.data  = buf;
                        value.ulen  = size;
                        value.flags = DB_DBT_USERMEM | DB_DBT_PARTIAL;
                } else {
                        value.flags = DB_DBT_MALLOC;
                }
                value.dlen = size;
                value.doff = offset;
        }

        do {
                /* TODO: we prefer to give our own buffer to value.data
                 * and ask bdb to fill in it */
                ret = storage->get (storage, txnid, &key, &value,
                                    db_flags);

                if (ret == DB_NOTFOUND) {
                        gf_log ("bdb-ll", GF_LOG_DEBUG,
                                "_BDB_DB_GET %s - %s: ENOENT"
                                "(specified key not found in database)",
                                bctx->directory, key_string);
                        ret = -1;
                        need_break = 1;
                } else if (ret == DB_LOCK_DEADLOCK) {
                        retries++;
                        gf_log ("bdb-ll", GF_LOG_DEBUG,
                                "_BDB_DB_GET %s - %s"
                                "(deadlock detected, retrying for %d "
                                "time)",
                                bctx->directory, key_string, retries);
                } else if (ret == 0) {
                        /* successfully read data, lets set everything
                         * in place and return */
                        if (bctx->cache) {
                                if (buf) {
                                        copy_size = ((value.size - offset) < size) ?
                                                (value.size - offset) : size;

                                        memcpy (buf, (value.data + offset),
                                                copy_size);
                                        ret = copy_size;
                                }

                                bdb_cache_insert (bctx, &key, &value);
                        } else {
                                ret = value.size;
                        }

                        if (size == 0)
                                free (value.data);

                        need_break = 1;
                } else {
                        gf_log ("bdb-ll", GF_LOG_DEBUG,
                                "_BDB_DB_GET %s - %s: %s"
                                "(failed to retrieve specified key from"
                                " database)",
                                bctx->directory, key_string,
                                db_strerror (ret));
                        ret = -1;
                        need_break = 1;
                }
        } while (!need_break);

out:
        return ret;
}/* bdb_db_get */

/* TODO: handle errors here and log. propogate only the errno to caller */
int32_t
bdb_db_fread (struct bdb_fd *bfd, char *buf, size_t size, off_t offset)
{
        return bdb_db_get (bfd->ctx, NULL, bfd->key, buf, size, offset);
}

int32_t
bdb_db_iread (struct bdb_ctx *bctx, const char *key, char **bufp)
{
        char *buf = NULL;
        size_t size = 0;
        int64_t ret = 0;

        ret = bdb_db_get (bctx, NULL, key, NULL, 0, 0);
        size = ret;

        if (bufp) {
                buf = calloc (size, sizeof (char));
                *bufp = buf;
                ret = bdb_db_get (bctx, NULL, key, buf, size, 0);
        }

        return ret; 
}

/* bdb_storage_put - insert a key/value specified to the corresponding DB.
 *
 * @bctx: bctx_t * corresponding to the parent directory of @path.
 *        (should always be a valid bctx). bdb_storage_put should never be
 *         called if @bctx = NULL.
 * @txnid: NULL if bdb_storage_put is not embedded in an explicit transaction
 *         or a valid DB_TXN *, when embedded in an explicit transaction.
 * @key_string: key of the database entry.
 * @buf: pointer to the buffer data to be written as data for @key_string.
 * @size: size of @buf.
 * @offset: offset in the key's data to be modified with provided data.
 * @flags: valid flags are BDB_TRUNCATE_RECORD (to reduce the data of
 *         @key_string to 0 size).
 *
 * NOTE: bdb_storage_put tries to open DB, if @bctx->dbp == NULL
 *      (@bctx->dbp == NULL, nobody has opened DB till now or DB was closed by
 *       bdb_table_prune()).
 *
 * NOTE: bdb_storage_put deletes the key/value from bdb xlator's internal cache.
 *
 * return: 0 on success or -1 on error.
 *
 * also see: bdb_cache_delete for details on how a cached key/value pair is
 * removed.
 */
static int32_t
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
                if (bctx->primary == NULL) {
                        ret = bdb_db_open (bctx);
                        storage = bctx->primary;
                } else {
                        /* we are just fine, lets continue */
                        storage = bctx->primary;
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

        /* NOTE: bdb lets us expand the file, suppose value.size > value.len,
         * then value.len bytes from value.doff offset and value.size bytes
         * will be written from value.doff and data from
         * value.doff + value.dlen will be pushed value.doff + value.size
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
                        gf_log ("bdb-ll", GF_LOG_DEBUG,
                                "_BDB_DB_PUT %s - %s"
                                "(deadlock detected, retying for %d time)",
                                bctx->directory, key_string, retries);
                } else if (ret) {
                        /* write failed */
                        gf_log ("bdb-ll", GF_LOG_DEBUG,
                                "_BDB_DB_PUT %s - %s: %s"
                                "(failed to put specified entry into database)",
                                bctx->directory, key_string, db_strerror (ret));
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

int32_t
bdb_db_icreate (struct bdb_ctx *bctx, const char *key)
{
        return bdb_db_put (bctx, NULL, key, NULL, 0, 0, 0);
}

/* TODO: handle errors here and log. propogate only the errno to caller */
int32_t
bdb_db_fwrite (struct bdb_fd *bfd, char *buf, size_t size, off_t offset)
{
        return bdb_db_put (bfd->ctx, NULL, bfd->key, buf, size, offset, 0);
}

/* TODO: handle errors here and log. propogate only the errno to caller */
int32_t
bdb_db_iwrite (struct bdb_ctx *bctx, const char *key, char *buf, size_t size)
{
        return bdb_db_put (bctx, NULL, key, buf, size, 0, 0);
}

int32_t
bdb_db_itruncate (struct bdb_ctx *bctx, const char *key)
{
        return bdb_db_put (bctx, NULL, key, NULL, 0, 1, 0);
}

/* bdb_storage_del - delete a key/value pair corresponding to @path from
 *  corresponding db file.
 *
 * @bctx: bctx_t * corresponding to the parent directory of @path.
 *       (should always be a valid bctx). bdb_storage_del should never be called
 *       if @bctx = NULL.
 * @txnid: NULL if bdb_storage_del is not embedded in an explicit transaction
 *   or a valid DB_TXN *, when embedded in an explicit transaction.
 * @path: path to the file, whose key/value pair has to be deleted.
 *
 * NOTE: bdb_storage_del tries to open DB, if @bctx->dbp == NULL
 *  (@bctx->dbp == NULL, nobody has opened DB till now or DB was closed by
 *  bdb_table_prune()).
 *
 * return: 0 on success or -1 on error.
 */
static int32_t
bdb_db_del (bctx_t *bctx,
            DB_TXN *txnid,
            const char *key_string)
{
        DB     *storage    = NULL;
        DBT     key        = {0,};
        int32_t ret        = -1;
        int32_t db_flags   = 0;
        uint8_t need_break = 0;
        int32_t retries    = 1;

        LOCK (&bctx->lock);
        {
                if (bctx->primary == NULL) {
                        ret = bdb_db_open (bctx);
                        storage = bctx->primary;
                } else {
                        /* we are just fine, lets continue */
                        storage = bctx->primary;
                }
        }
        UNLOCK (&bctx->lock);

        GF_VALIDATE_OR_GOTO ("bdb-ll", storage, out);

        ret = bdb_cache_delete (bctx, key_string);
        GF_VALIDATE_OR_GOTO ("bdb-ll", (ret == 0), out);

        key.data = (char *)key_string;
        key.size = strlen (key_string);
        key.flags = DB_DBT_USERMEM;

        do {
                ret = storage->del (storage, txnid, &key, db_flags);

                if (ret == DB_NOTFOUND) {
                        gf_log ("bdb-ll", GF_LOG_DEBUG,
                                "_BDB_DB_DEL %s - %s: ENOENT"
                                "(failed to delete entry, could not be "
                                "found in the database)",
                                bctx->directory, key_string);
                        need_break = 1;
                } else if (ret == DB_LOCK_DEADLOCK) {
                        retries++;
                        gf_log ("bdb-ll", GF_LOG_DEBUG,
                                "_BDB_DB_DEL %s - %s"
                                "(deadlock detected, retying for %d time)",
                                bctx->directory, key_string, retries);
                } else if (ret == 0) {
                        /* successfully deleted the entry */
                        gf_log ("bdb-ll", GF_LOG_DEBUG,
                                "_BDB_DB_DEL %s - %s"
                                "(successfully deleted entry from database)",
                                bctx->directory, key_string);
                        ret = 0;
                        need_break = 1;
                } else {
                        gf_log ("bdb-ll", GF_LOG_DEBUG,
                                "_BDB_DB_DEL %s - %s: %s"
                                "(failed to delete entry from database)",
                                bctx->directory, key_string, db_strerror (ret));
                        ret = -1;
                        need_break = 1;
                }
        } while (!need_break);
out:
        return ret;
}

int32_t
bdb_db_iremove (bctx_t *bctx,
                const char *key)
{
        return bdb_db_del (bctx, NULL, key);
}

/* NOTE: bdb version compatibility wrapper */
int32_t
bdb_cursor_get (DBC *cursorp,
                DBT *sec, DBT *pri,
                DBT *val,
                int32_t flags)
{
        int32_t ret = -1;

        GF_VALIDATE_OR_GOTO ("bdb-ll", cursorp, out);

#ifdef HAVE_BDB_CURSOR_GET
        ret = cursorp->pget (cursorp, sec, pri, val, flags);
#else
        ret = cursorp->c_pget (cursorp, sec, pri, val, flags);
#endif
        if ((ret != 0)  && (ret != DB_NOTFOUND)) {
                gf_log ("bdb-ll", GF_LOG_DEBUG,
                        "_BDB_CURSOR_GET: %s"
                        "(failed to retrieve entry from database cursor)",
                        db_strerror (ret));
        }

out:
        return ret;
}/* bdb_cursor_get */

int32_t
bdb_dirent_size (DBT *key)
{
        return ALIGN (24 /* FIX MEEEE!!! */ + key->size);
}



/* bdb_dbenv_init - initialize DB_ENV
 *
 *  initialization includes:
 *   1. opening DB_ENV (db_env_create(), DB_ENV->open()).
 *      NOTE: see private->envflags for flags used.
 *   2. DB_ENV->set_lg_dir - set log directory to be used for storing log files
 *     (log files are the files in which transaction logs are written by db).
 *   3. DB_ENV->set_flags (DB_LOG_AUTOREMOVE) - set DB_ENV to automatically
 *      clear the unwanted log files (flushed at each checkpoint).
 *   4. DB_ENV->set_errfile - set errfile to be used by db to report detailed
 *      error logs. used only for debbuging purpose.
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

        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (directory, err);

        private = this->private;
        VALIDATE_OR_GOTO (private, err);

        ret = db_env_create (&dbenv, 0);
        VALIDATE_OR_GOTO ((ret == 0), err);

        /* NOTE: set_errpfx returns 'void' */
        dbenv->set_errpfx(dbenv, this->name);

        ret = dbenv->set_lk_detect (dbenv, DB_LOCK_DEFAULT);
        VALIDATE_OR_GOTO ((ret == 0), err);

        ret = dbenv->open(dbenv, directory,
                          private->envflags,
                          S_IRUSR | S_IWUSR);
        if ((ret != 0) && (ret != DB_RUNRECOVERY)) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "failed to join Berkeley DB environment at %s: %s."
                        "please run manual recovery and retry running "
                        "glusterfs",
                        directory, db_strerror (ret));
                dbenv = NULL;
                goto err;
        } else if (ret == DB_RUNRECOVERY) {
                fatal_flags = ((private->envflags & (~DB_RECOVER))
                               | DB_RECOVER_FATAL);
                ret = dbenv->open(dbenv, directory, fatal_flags,
                                  S_IRUSR | S_IWUSR);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_CRITICAL,
                                "failed to join Berkeley DB environment in "
                                "recovery mode at %s: %s. please run manual "
                                "recovery and retry running glusterfs",
                                directory, db_strerror (ret));
                        dbenv = NULL;
                        goto err;
                }
        }

        ret = 0;
#if (DB_VERSION_MAJOR == 4 &&                   \
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
        if (ret < 0) {
                gf_log ("bdb-ll", GF_LOG_ERROR,
                        "autoremoval of transactional log files could not be "
                        "configured (%s). you may have to do a manual "
                        "monitoring of transactional log files and remove "
                        "periodically.",
                        db_strerror (ret));
                goto err;
        }

        if (private->transaction) {
                ret = dbenv->set_flags(dbenv, DB_AUTO_COMMIT, 1);

                if (ret != 0) {
                        gf_log ("bdb-ll", GF_LOG_DEBUG,
                                "configuration of auto-commit failed for "
                                "database environment at %s. none of the "
                                "operations will be embedded in transaction "
                                "unless explicitly done so.",
                                db_strerror (ret));
                        goto err;
                }

                if (private->txn_timeout) {
                        ret = dbenv->set_timeout (dbenv, private->txn_timeout,
                                                  DB_SET_TXN_TIMEOUT);
                        if (ret != 0) {
                                gf_log ("bdb-ll", GF_LOG_ERROR,
                                        "could not configure Berkeley DB "
                                        "transaction timeout to %d (%s). please"
                                        " review 'option transaction-timeout %d"
                                        "' option.",
                                        private->txn_timeout,
                                        db_strerror (ret),
                                        private->txn_timeout);
                                goto err;
                        }
                }

                if (private->lock_timeout) {
                        ret = dbenv->set_timeout(dbenv,
                                                 private->txn_timeout,
                                                 DB_SET_LOCK_TIMEOUT);
                        if (ret < 0) {
                                gf_log ("bdb-ll", GF_LOG_ERROR,
                                        "could not configure Berkeley DB "
                                        "lock timeout to %d (%s). please"
                                        " review 'option lock-timeout %d"
                                        "' option.",
                                        private->lock_timeout,
                                        db_strerror (ret),
                                        private->lock_timeout);
                                goto err;
                        }
                }

                ret = dbenv->set_lg_dir (dbenv, private->logdir);
                if (ret < 0) {
                        gf_log ("bdb-ll", GF_LOG_ERROR,
                                "failed to configure libdb transaction log "
                                "directory at %s. please review the "
                                "'option logdir %s' option.",
                                db_strerror (ret), private->logdir);
                        goto err;
                }
        }

        if (private->errfile) {
                private->errfp = fopen (private->errfile, "a+");
                if (private->errfp) {
                        dbenv->set_errfile (dbenv, private->errfp);
                } else {
                        gf_log ("bdb-ll", GF_LOG_ERROR,
                                "failed to open error logging file for "
                                "libdb (Berkeley DB) internal logging (%s)."
                                "please review the 'option errfile %s' option.",
                                strerror (errno), private->errfile);
                        goto err;
                }
        }

        return dbenv;
err:
        if (dbenv) {
                dbenv->close (dbenv, 0);
        }

        return NULL;
}

#define BDB_ENV(this) ((((struct bdb_private *)this->private)->b_table)->dbenv)

/* bdb_checkpoint - during transactional usage, db does not directly write the
 *  data to db files, instead db writes a 'log' (similar to a journal entry)
 *  into a log file. db normally clears the log files during opening of an
 *  environment. since we expect a filesystem server to run for a pretty long
 *  duration and flushing 'log's during dbenv->open would prove very costly, if
 *  we accumulate the log entries for one complete run of glusterfs server. to
 *  flush the logs frequently, db provides a mechanism called 'checkpointing'.
 *  when we do a checkpoint, db flushes the logs to disk (writes changes to db
 *  files) and we can also clear the accumulated log files after checkpointing.
 *  NOTE: removing unwanted log files is not part of dbenv->txn_checkpoint()
 *  call.
 *
 * @data: xlator_t of the current instance of bdb xlator.
 *
 *  bdb_checkpoint is called in a different thread from the main glusterfs
 *  thread. bdb xlator creates the checkpoint thread after successfully opening
 *  the db environment.
 *  NOTE: bdb_checkpoint thread shares the DB_ENV handle with the filesystem
 *  thread.
 *
 *  db environment checkpointing frequency is controlled by
 *  'option checkpoint-timeout <time-in-seconds>' in volfile.
 *
 * NOTE: checkpointing thread is started only if 'option transaction on'
 *      specified in volfile. checkpointing is not valid for non-transactional
 *      environments.
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

        for (;;sleep (private->checkpoint_interval)) {
                LOCK (&private->active_lock);
                active = private->active;
                UNLOCK (&private->active_lock);

                if (active) {
                        ret = dbenv->txn_checkpoint (dbenv, 1024, 0, 0);
                        if (ret) {
                                gf_log ("bdb-ll", GF_LOG_DEBUG,
                                        "_BDB_CHECKPOINT: %s"
                                        "(failed to checkpoint environment)",
                                        db_strerror (ret));
                        } else {
                                gf_log ("bdb-ll", GF_LOG_DEBUG,
                                        "_BDB_CHECKPOINT: successfully "
                                        "checkpointed");
                        }
                } else {
                        ret = dbenv->txn_checkpoint (dbenv, 1024, 0, 0);
                        if (ret) {
                                gf_log ("bdb-ll", GF_LOG_ERROR,
                                        "_BDB_CHECKPOINT: %s"
                                        "(final checkpointing failed. might "
                                        "need to run recovery tool manually on "
                                        "next usage of this database "
                                        "environment)",
                                        db_strerror (ret));
                        } else {
                                gf_log ("bdb-ll", GF_LOG_DEBUG,
                                        "_BDB_CHECKPOINT: final successfully "
                                        "checkpointed");
                        }
                        break;
                }
        }

        return NULL;
}


/* bdb_db_init - initialize bdb xlator
 *
 * reads the options from @options dictionary and sets appropriate values in
 * @this->private. also initializes DB_ENV.
 *
 * return: 0 on success or -1 on error
 * (with logging the error through gf_log()).
 */
int
bdb_db_init (xlator_t *this,
             dict_t *options)
{
        /* create a db entry for root */
        int32_t        op_ret  = 0;
        bdb_private_t *private = NULL;
        bctx_table_t  *table = NULL;

        char *checkpoint_interval_str = NULL;
        char *page_size_str           = NULL;
        char *lru_limit_str           = NULL;
        char *timeout_str             = NULL;
        char *access_mode             = NULL;
        char *endptr    = NULL;
        char *errfile   = NULL;
        char *directory = NULL;
        char *logdir    = NULL;
        char *mode      = NULL;
        char *mode_str  = NULL;
        int   ret = -1;
        int   idx = 0;
        struct stat stbuf = {0,};

        private = this->private;

        /* cache is always on */
        private->cache = ON;

        ret = dict_get_str (options, "access-mode", &access_mode);
        if ((ret == 0)
            && (!strcmp (access_mode, "btree"))) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "using BTREE access mode to access libdb "
                        "(Berkeley DB)");
                private->access_mode = DB_BTREE;
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "using HASH access mode to access libdb (Berkeley DB)");
                private->access_mode = DB_HASH;
        }

        ret = dict_get_str (options, "mode", &mode);
        if ((ret == 0)
            && (!strcmp (mode, "cache"))) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "cache data mode selected for 'storage/bdb'. filesystem"
                        " operations are not transactionally protected and "
                        "system crash does not guarantee recoverability of "
                        "data");
                private->envflags = DB_CREATE | DB_INIT_LOG |
                        DB_INIT_MPOOL | DB_THREAD;
                private->dbflags = DB_CREATE | DB_THREAD;
                private->transaction = OFF;
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "persistent data mode selected for 'storage/bdb'. each"
                        "filesystem operation is guaranteed to be Berkeley DB "
                        "transaction protected.");
                private->transaction = ON;
                private->envflags = DB_CREATE | DB_INIT_LOCK | DB_INIT_LOG |
                        DB_INIT_MPOOL | DB_INIT_TXN | DB_RECOVER | DB_THREAD;
                private->dbflags = DB_CREATE | DB_THREAD;


                ret = dict_get_str (options, "lock-timeout", &timeout_str);

                if (ret == 0) {
                        ret = gf_string2time (timeout_str,
                                              &private->lock_timeout);

                        if (private->lock_timeout > 4260000) {
                                /* db allows us to DB_SET_LOCK_TIMEOUT to be
                                 * set to a maximum of 71 mins
                                 * (4260000 milliseconds) */
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "Berkeley DB lock-timeout parameter "
                                        "(%d) is out of range. please specify"
                                        " a valid timeout value for "
                                        "lock-timeout and retry.",
                                        private->lock_timeout);
                                goto err;
                        }
                }
                ret = dict_get_str (options, "transaction-timeout",
                                    &timeout_str);
                if (ret == 0) {
                        ret = gf_string2time (timeout_str,
                                              &private->txn_timeout);

                        if (private->txn_timeout > 4260000) {
                                /* db allows us to DB_SET_TXN_TIMEOUT to be set
                                 * to a maximum of 71 mins
                                 * (4260000 milliseconds) */
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "Berkeley DB lock-timeout parameter "
                                        "(%d) is out of range. please specify"
                                        " a valid timeout value for "
                                        "lock-timeout and retry.",
                                        private->lock_timeout);
                                goto err;
                        }
                }

                private->checkpoint_interval = BDB_DEFAULT_CHECKPOINT_INTERVAL;
                ret = dict_get_str (options, "checkpoint-interval",
                                    &checkpoint_interval_str);
                if (ret == 0) {
                        ret = gf_string2time (checkpoint_interval_str,
                                              &private->checkpoint_interval);

                        if (ret < 0) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "'%"PRIu32"' is not a valid parameter "
                                        "for checkpoint-interval option. "
                                        "please specify a valid "
                                        "checkpoint-interval and retry",
                                        private->checkpoint_interval);
                                goto err;
                        }
                }
        }

        ret = dict_get_str (options, "file-mode", &mode_str);
        if (ret == 0) {
                private->file_mode = strtol (mode_str, &endptr, 8);

                if ((*endptr) ||
                    (!IS_VALID_FILE_MODE(private->file_mode))) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "'%o' is not a valid parameter for file-mode "
                                "option. please specify a valid parameter for "
                                "file-mode and retry.",
                                private->file_mode);
                        goto err;
                }
        } else {
                private->file_mode = DEFAULT_FILE_MODE;
        }
        private->symlink_mode = private->file_mode | S_IFLNK;
        private->file_mode = private->file_mode | S_IFREG;

        ret = dict_get_str (options, "dir-mode", &mode_str);
        if (ret == 0) {
                private->dir_mode = strtol (mode_str, &endptr, 8);
                if ((*endptr) ||
                    (!IS_VALID_FILE_MODE(private->dir_mode))) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "'%o' is not a valid parameter for dir-mode "
                                "option. please specify a valid parameter for "
                                "dir-mode and retry.",
                                private->dir_mode);
                        goto err;
                }
        } else {
                private->dir_mode = DEFAULT_DIR_MODE;
        }

        private->dir_mode = private->dir_mode | S_IFDIR;

        table = CALLOC (1, sizeof (*table));
        if (table == NULL) {
                gf_log ("bdb-ll", GF_LOG_CRITICAL,
                        "memory allocation for 'storage/bdb' internal "
                        "context table failed.");
                goto err;
        }

        INIT_LIST_HEAD(&(table->b_lru));
        INIT_LIST_HEAD(&(table->active));
        INIT_LIST_HEAD(&(table->purge));

        LOCK_INIT (&table->lock);
        LOCK_INIT (&table->checkpoint_lock);

        table->transaction = private->transaction;
        table->access_mode = private->access_mode;
        table->dbflags = private->dbflags;
        table->this    = this;

        ret = dict_get_str (options, "lru-limit",
                            &lru_limit_str);

        /* TODO: set max lockers and max txns to accomodate
         * for more than lru_limit */
        if (ret == 0) {
                ret = gf_string2uint32 (lru_limit_str,
                                        &table->lru_limit);
                gf_log ("bdb-ll", GF_LOG_DEBUG,
                        "setting lru limit of 'storage/bdb' internal context"
                        "table to %d. maximum of %d unused databases can be "
                        "open at any given point of time.",
                        table->lru_limit, table->lru_limit);
        } else {
                table->lru_limit = BDB_DEFAULT_LRU_LIMIT;
        }

        ret = dict_get_str (options, "page-size",
                            &page_size_str);

        if (ret == 0) {
                ret = gf_string2bytesize (page_size_str,
                                          &table->page_size);
                if (ret < 0) {
                        gf_log ("bdb-ll", GF_LOG_ERROR,
                                "\"%s\" is an invalid parameter to "
                                "\"option page-size\". please specify a valid "
                                "size and retry.",
                                page_size_str);
                        goto err;
                }

                if (!PAGE_SIZE_IN_RANGE(table->page_size)) {
                        gf_log ("bdb-ll", GF_LOG_ERROR,
                                "\"%s\" is out of range for Berkeley DB "
                                "page-size. allowed page-size range is %d to "
                                "%d. please specify a page-size value in the "
                                "range and retry.",
                                page_size_str, BDB_LL_PAGE_SIZE_MIN,
                                BDB_LL_PAGE_SIZE_MAX);
                        goto err;
                }
        } else {
                table->page_size = BDB_LL_PAGE_SIZE_DEFAULT;
        }

        table->hash_size = BDB_DEFAULT_HASH_SIZE;
        table->b_hash = CALLOC (BDB_DEFAULT_HASH_SIZE,
                                sizeof (struct list_head));

        for (idx = 0; idx < table->hash_size; idx++)
                INIT_LIST_HEAD(&(table->b_hash[idx]));

        private->b_table = table;

        ret = dict_get_str (options, "errfile", &errfile);
        if (ret == 0) {
                private->errfile = strdup (errfile);
                gf_log (this->name, GF_LOG_DEBUG,
                        "using %s as error logging file for libdb (Berkeley DB "
                        "library) internal logging.", private->errfile);
        }

        ret = dict_get_str (options, "directory", &directory);

        if (ret == 0) {
                ret = dict_get_str (options, "logdir", &logdir);

                if (ret < 0) {
                        gf_log ("bdb-ll", GF_LOG_DEBUG,
                                "using the database environment home "
                                "directory (%s) itself as transaction log "
                                "directory", directory);
                        private->logdir = strdup (directory);

                } else {
                        private->logdir = strdup (logdir);

                        op_ret = stat (private->logdir, &stbuf);
                        if ((op_ret != 0)
                            || (!S_ISDIR (stbuf.st_mode))) {
                                gf_log ("bdb-ll", GF_LOG_ERROR,
                                        "specified logdir %s does not exist. "
                                        "please provide a valid existing "
                                        "directory as parameter to 'option "
                                        "logdir'",
                                        private->logdir);
                                goto err;
                        }
                }

                private->b_table->dbenv = bdb_dbenv_init (this, directory);
                if (private->b_table->dbenv == NULL) {
                        gf_log ("bdb-ll", GF_LOG_ERROR,
                                "initialization of database environment "
                                "failed");
                        goto err;
                } else {
                        if (private->transaction) {
                                /* all well, start the checkpointing thread */
                                LOCK_INIT (&private->active_lock);

                                LOCK (&private->active_lock);
                                {
                                        private->active = 1;
                                }
                                UNLOCK (&private->active_lock);
                                pthread_create (&private->checkpoint_thread,
                                                NULL, bdb_checkpoint, this);
                        }
                }
        }

        return op_ret;
err:
        if (table) {
                FREE (table->b_hash);
                FREE (table);
        }
        if (private) {
                if (private->errfile)
                        FREE (private->errfile);

                if (private->logdir)
                        FREE (private->logdir);
        }

        return -1;
}
