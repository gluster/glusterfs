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

/* bdb based storage translator - named as 'bdb' translator
 *
 *
 * There can be only two modes for files existing on bdb translator:
 * 1. DIRECTORY - directories are stored by bdb as regular directories on
 * back-end file-system. directories also have an entry in the ns_db.db of
 * their parent directory.
 * 2. REGULAR FILE - regular files are stored as records in the storage_db.db
 * present in the directory. regular files also have an entry in ns_db.db
 *
 * Internally bdb has a maximum of three different types of logical files
 * associated with each directory:
 * 1. storage_db.db - storage database, used to store the data corresponding to
 *    regular files in the form of key/value pair. file-name is the 'key' and
 *    data is 'value'.
 * 2. directory (all subdirectories) - any subdirectory will have a regular
 *    directory entry.
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
#include "defaults.h"
#include "common-utils.h"

/* to be used only by fops, nobody else */
#define BDB_ENV(this) ((((struct bdb_private *)this->private)->b_table)->dbenv)
#define B_TABLE(this) (((struct bdb_private *)this->private)->b_table)


int32_t
bdb_mknod (call_frame_t *frame,
           xlator_t *this,
           loc_t *loc,
           mode_t mode,
           dev_t dev)
{
        int32_t     op_ret     = -1;
        int32_t     op_errno   = EINVAL;
        char       *key_string = NULL; /* after translating path to DB key */
        char       *db_path    = NULL;
        bctx_t     *bctx       = NULL;
        struct stat stbuf      = {0,};


        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);

        if (!S_ISREG(mode)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "MKNOD %"PRId64"/%s (%s): EPERM"
                        "(mknod supported only for regular files. "
                        "file mode '%o' not supported)",
                        loc->parent->ino, loc->name, loc->path, mode);
                op_ret = -1;
                op_errno = EPERM;
                goto out;
        } /* if(!S_ISREG(mode)) */

        bctx = bctx_parent (B_TABLE(this), loc->path);
        if (bctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "MKNOD %"PRId64"/%s (%s): ENOMEM"
                        "(failed to lookup database handle)",
                        loc->parent->ino, loc->name, loc->path);
                op_ret   = -1;
                op_errno = ENOMEM;
                goto out;
        }

        MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bctx->directory);

        op_ret = lstat (db_path, &stbuf);
        if (op_ret != 0) {
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_DEBUG,
                        "MKNOD %"PRId64"/%s (%s): EINVAL"
                        "(failed to lookup database handle)",
                        loc->parent->ino, loc->name, loc->path);
                goto out;
        }

        MAKE_KEY_FROM_PATH (key_string, loc->path);
        op_ret = bdb_db_icreate (bctx, key_string);
        if (op_ret > 0) {
                /* create successful */
                stbuf.st_ino = bdb_inode_transform (loc->parent->ino,
                                                    key_string,
                                                    strlen (key_string));
                stbuf.st_mode  = mode;
                stbuf.st_size = 0;
                stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size, \
                                                    stbuf.st_blksize);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "MKNOD %"PRId64"/%s (%s): ENOMEM"
                        "(failed to create database entry)",
                        loc->parent->ino, loc->name, loc->path);
                op_ret   = -1;
                op_errno = EINVAL; /* TODO: errno sari illa */
                goto out;
        }/* if (!op_ret)...else */

out:
        if (bctx) {
                /* NOTE: bctx_unref always returns success,
                 * see description of bctx_unref for more details */
                bctx_unref (bctx);
        }

        STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf);
        return 0;
}

static inline int32_t
is_dir_empty (xlator_t *this,
              loc_t *loc)
{
        int32_t        ret       = 1;
        bctx_t        *bctx      = NULL;
        DIR           *dir       = NULL;
        char          *real_path = NULL;
        void          *dbstat    = NULL;
        struct dirent *entry     = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);

        bctx = bctx_lookup (B_TABLE(this), loc->path);
        if (bctx == NULL) {
                ret = -ENOMEM;
                goto out;
        }

        dbstat = bdb_db_stat (bctx, NULL, 0);
        if (dbstat) {
                switch (bctx->table->access_mode)
                {
                case DB_HASH:
                        ret = (((DB_HASH_STAT *)dbstat)->hash_nkeys == 0);
                        break;
                case DB_BTREE:
                case DB_RECNO:
                        ret = (((DB_BTREE_STAT *)dbstat)->bt_nkeys == 0);
                        break;
                case DB_QUEUE:
                        ret = (((DB_QUEUE_STAT *)dbstat)->qs_nkeys == 0);
                        break;
                case DB_UNKNOWN:
                        gf_log (this->name, GF_LOG_CRITICAL,
                                "unknown access-mode set for database");
                        ret = 0;
                }
        } else {
                ret = -EBUSY;
                goto out;
        }

        MAKE_REAL_PATH (real_path, this, loc->path);
        dir = opendir (real_path);
        if (dir == NULL) {
                ret = -errno;
                goto out;
        }

        while ((entry = readdir (dir))) {
                if ((!IS_BDB_PRIVATE_FILE(entry->d_name)) &&
                    (!IS_DOT_DOTDOT(entry->d_name))) {
                        ret = 0;
                        break;
                }/* if(!IS_BDB_PRIVATE_FILE()) */
        } /* while(true) */
        closedir (dir);
out:
        if (bctx) {
                /* NOTE: bctx_unref always returns success,
                 * see description of bctx_unref for more details */
                bctx_unref (bctx);
        }

        return ret;
}

int32_t
bdb_rename (call_frame_t *frame,
            xlator_t *this,
            loc_t *oldloc,
            loc_t *newloc)
{
        STACK_UNWIND (frame, -1, EXDEV, NULL);
        return 0;
}

int32_t
bdb_link (call_frame_t *frame,
          xlator_t *this,
          loc_t *oldloc,
          loc_t *newloc)
{
        STACK_UNWIND (frame, -1, EXDEV, NULL, NULL);
        return 0;
}

int32_t
is_space_left (xlator_t *this,
               size_t size)
{
        struct bdb_private *private = this->private;
        struct statvfs stbuf = {0,};
        int32_t ret = -1;
        fsblkcnt_t req_blocks = 0;
        fsblkcnt_t usable_blocks = 0;

        ret = statvfs (private->export_path, &stbuf);
        if (ret != 0) {
                ret = 0;
        } else {
                req_blocks = (size / stbuf.f_frsize) + 1;

                usable_blocks = (stbuf.f_bfree - BDB_ENOSPC_THRESHOLD);

                if (req_blocks < usable_blocks)
                        ret = 1;
                else
                        ret = 0;
        }

        return ret;
}

int32_t
bdb_create (call_frame_t *frame,
            xlator_t *this,
            loc_t *loc,
            int32_t flags,
            mode_t mode,
            fd_t *fd)
{
        int32_t             op_ret     = -1;
        int32_t             op_errno   = EPERM;
        char               *db_path    = NULL;
        struct stat         stbuf      = {0,};
        bctx_t             *bctx       = NULL;
        struct bdb_private *private    = NULL;
        char               *key_string = NULL;
        struct bdb_fd      *bfd        = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        private = this->private;

        bctx = bctx_parent (B_TABLE(this), loc->path);
        if (bctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "CREATE %"PRId64"/%s (%s): ENOMEM"
                        "(failed to lookup database handle)",
                        loc->parent->ino, loc->name, loc->path);
                op_ret   = -1;
                op_errno = ENOMEM;
                goto out;
        }

        MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bctx->directory);
        op_ret = lstat (db_path, &stbuf);
        if (op_ret != 0) {
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_DEBUG,
                        "CREATE %"PRId64"/%s (%s): EINVAL"
                        "(database file missing)",
                        loc->parent->ino, loc->name, loc->path);
                goto out;
        }

        MAKE_KEY_FROM_PATH (key_string, loc->path);
        op_ret = bdb_db_icreate (bctx, key_string);
        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "CREATE %"PRId64"/%s (%s): ENOMEM"
                        "(failed to create database entry)",
                        loc->parent->ino, loc->name, loc->path);
                op_errno = EINVAL; /* TODO: errno sari illa */
                goto out;
        }

        /* create successful */
        bfd = CALLOC (1, sizeof (*bfd));
        if (bfd == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "CREATE %"PRId64"/%s (%s): ENOMEM"
                        "(failed to allocate memory for internal fd context)",
                        loc->parent->ino, loc->name, loc->path);
                op_ret   = -1;
                op_errno = ENOMEM;
                goto out;
        }

        /* NOTE: bdb_get_bctx_from () returns bctx with a ref */
        bfd->ctx = bctx;
        bfd->key = strdup (key_string);
        if (bfd->key == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "CREATE %"PRId64" (%s): ENOMEM"
                        "(failed to allocate memory for internal fd->key)",
                        loc->ino, loc->path);
                op_ret   = -1;
                op_errno = ENOMEM;
                goto out;
        }

        BDB_FCTX_SET (fd, this, bfd);

        stbuf.st_ino = bdb_inode_transform (loc->parent->ino,
                                            key_string,
                                            strlen (key_string));
        stbuf.st_mode = private->file_mode;
        stbuf.st_size = 0;
        stbuf.st_nlink = 1;
        stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size, stbuf.st_blksize);
        op_ret = 0;
        op_errno = 0;
out:
        STACK_UNWIND (frame, op_ret, op_errno, fd, loc->inode, &stbuf);

        return 0;
}


/* bdb_open
 *
 * as input parameters bdb_open gets the file name, i.e key. bdb_open should
 * effectively
 * do: store key, open storage db, store storage-db pointer.
 *
 */
int32_t
bdb_open (call_frame_t *frame,
          xlator_t *this,
          loc_t *loc,
          int32_t flags,
          fd_t *fd,
          int32_t wbflags)
{
        int32_t         op_ret     = -1;
        int32_t         op_errno   = EINVAL;
        bctx_t         *bctx       = NULL;
        char           *key_string = NULL;
        struct bdb_fd  *bfd        = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        bctx = bctx_parent (B_TABLE(this), loc->path);
        if (bctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "OPEN %"PRId64" (%s): ENOMEM"
                        "(failed to lookup database handle)",
                        loc->ino, loc->path);
                op_ret   = -1;
                op_errno = ENOMEM;
                goto out;
        }

        bfd = CALLOC (1, sizeof (*bfd));
        if (bfd == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "OPEN %"PRId64" (%s): ENOMEM"
                        "(failed to allocate memory for internal fd context)",
                        loc->ino, loc->path);
                op_ret   = -1;
                op_errno = ENOMEM;
                goto out;
        }

        /* NOTE: bctx_parent () returns bctx with a ref */
        bfd->ctx = bctx;

        MAKE_KEY_FROM_PATH (key_string, loc->path);
        bfd->key = strdup (key_string);
        if (bfd->key == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "OPEN %"PRId64" (%s): ENOMEM"
                        "(failed to allocate memory for internal fd->key)",
                        loc->ino, loc->path);
                op_ret   = -1;
                op_errno = ENOMEM;
                goto out;
        }

        BDB_FCTX_SET (fd, this, bfd);
        op_ret = 0;
out:
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
        int32_t        op_ret     = -1;
        int32_t        op_errno   = EINVAL;
        struct iovec   vec        = {0,};
        struct stat    stbuf      = {0,};
        struct bdb_fd *bfd        = NULL;
        char          *db_path    = NULL;
        int32_t        read_size  = 0;
        struct iobref *iobref     = NULL;
        struct iobuf  *iobuf      = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        BDB_FCTX_GET (fd, this, &bfd);
        if (bfd == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "READV %"PRId64" - %"GF_PRI_SIZET",%"PRId64": EBADFD"
                        "(internal fd not found through fd)",
                        fd->inode->ino, size, offset);
                op_errno = EBADFD;
                op_ret = -1;
                goto out;
        }

        MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bfd->ctx->directory);
        op_ret = lstat (db_path, &stbuf);
        if (op_ret != 0) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "READV %"PRId64" - %"GF_PRI_SIZET",%"PRId64": EINVAL"
                        "(database file missing)",
                        fd->inode->ino, size, offset);
                goto out;
        }

        iobuf = iobuf_get (this->ctx->iobuf_pool);
        if (!iobuf) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory :(");
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        /* we are ready to go */
        op_ret = bdb_db_fread (bfd, iobuf->ptr, size, offset);
        read_size = op_ret;
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "READV %"PRId64" - %"GF_PRI_SIZET",%"PRId64": EBADFD"
                        "(failed to find entry in database)",
                        fd->inode->ino, size, offset);
                op_ret   = -1;
                op_errno = ENOENT;
                goto out;
        } else if (op_ret == 0) {
                goto out;
        }

        iobref = iobref_new ();
        if (iobref == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory :(");
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        if (size < read_size) {
                op_ret = size;
                read_size = size;
        }

        iobref_add (iobref, iobuf);

        vec.iov_base = iobuf->ptr;
        vec.iov_len = read_size;

        stbuf.st_ino = fd->inode->ino;
        stbuf.st_size = bdb_db_fread (bfd, NULL, 0, 0);
        stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size, stbuf.st_blksize);
        op_ret = size;
out:
        STACK_UNWIND (frame, op_ret, op_errno, &vec, 1, &stbuf, iobuf);

        if (iobref)
                iobref_unref (iobref);

        if (iobuf)
                iobuf_unref (iobuf);

        return 0;
}


int32_t
bdb_writev (call_frame_t *frame,
            xlator_t *this,
            fd_t *fd,
            struct iovec *vector,
            int32_t count,
            off_t offset,
            struct iobref *iobref)
{
        int32_t        op_ret   = -1;
        int32_t        op_errno = EINVAL;
        struct stat    stbuf    = {0,};
        struct bdb_fd *bfd      = NULL;
        int32_t        idx      = 0;
        off_t          c_off    = offset;
        int32_t        c_ret    = -1;
        char          *db_path  = NULL;
        size_t         total_size = 0;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, vector, out);

        BDB_FCTX_GET (fd, this, &bfd);
        if (bfd == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "WRITEV %"PRId64" - %"PRId32",%"PRId64": EBADFD"
                        "(internal fd not found through fd)",
                        fd->inode->ino, count, offset);
                op_ret = -1;
                op_errno = EBADFD;
                goto out;
        }

        MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bfd->ctx->directory);
        op_ret = lstat (db_path, &stbuf);
        if (op_ret != 0) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "WRITEV %"PRId64" - %"PRId32",%"PRId64": EINVAL"
                        "(database file missing)",
                        fd->inode->ino, count, offset);
                goto out;
        }

        for (idx = 0; idx < count; idx++)
                total_size += vector[idx].iov_len;

        if (!is_space_left (this, total_size)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "WRITEV %"PRId64" - %"PRId32" (%"GF_PRI_SIZET"),%"
                        PRId64": ENOSPC "
                        "(not enough space after internal measurement)",
                        fd->inode->ino, count, total_size, offset);
                op_ret = -1;
                op_errno = ENOSPC;
                goto out;
        }

        /* we are ready to go */
        for (idx = 0; idx < count; idx++) {
                c_ret = bdb_db_fwrite (bfd, vector[idx].iov_base,
                                       vector[idx].iov_len, c_off);
                if (c_ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "WRITEV %"PRId64" - %"PRId32",%"PRId64": EINVAL"
                                "(database write at %"PRId64" failed)",
                                fd->inode->ino, count, offset, c_off);
                        break;
                } else {
                        c_off += vector[idx].iov_len;
                }
                op_ret += vector[idx].iov_len;
        } /* for(idx=0;...)... */

        if (c_ret) {
                /* write failed after a point, not an error */
                stbuf.st_size   = bdb_db_fread (bfd, NULL, 0, 0);
                stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size,
                                                    stbuf.st_blksize);
                goto out;
        }

        /* NOTE: we want to increment stbuf->st_size, as stored in db */
        stbuf.st_size   = op_ret;
        stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size, stbuf.st_blksize);
        op_errno = 0;

out:
        STACK_UNWIND (frame, op_ret, op_errno, &stbuf);
        return 0;
}

int32_t
bdb_flush (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd)
{
        int32_t        op_ret   = -1;
        int32_t        op_errno = EPERM;
        struct bdb_fd *bfd      = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        BDB_FCTX_GET (fd, this, &bfd);
        if (bfd == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "FLUSH %"PRId64": EBADFD"
                        "(internal fd not found through fd)",
                        fd->inode->ino);
                op_ret = -1;
                op_errno = EBADFD;
                goto out;
        }

        /* do nothing */
        op_ret = 0;
        op_errno = 0;

out:
        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}

int32_t
bdb_release (xlator_t *this,
             fd_t *fd)
{
        int32_t op_ret = -1;
        int32_t op_errno = EBADFD;
        struct bdb_fd *bfd = NULL;

        BDB_FCTX_GET (fd, this, &bfd);
        if (bfd == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "RELEASE %"PRId64": EBADFD"
                        "(internal fd not found through fd)",
                        fd->inode->ino);
                op_ret = -1;
                op_errno = EBADFD;
                goto out;
        }

        bctx_unref (bfd->ctx);
        bfd->ctx = NULL;

        if (bfd->key)
                FREE (bfd->key); /* we did strdup() in bdb_open() */
        FREE (bfd);
        op_ret = 0;
        op_errno = 0;

out:
        return 0;
}/* bdb_release */


int32_t
bdb_fsync (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd,
           int32_t datasync)
{
        STACK_UNWIND (frame, 0, 0);
        return 0;
}/* bdb_fsync */

static int gf_bdb_lk_log;

int32_t
bdb_lk (call_frame_t *frame,
        xlator_t *this,
        fd_t *fd,
        int32_t cmd,
        struct flock *lock)
{
        struct flock nullock = {0, };

        if (BDB_TIMED_LOG (ENOTSUP, gf_bdb_lk_log)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "LK %"PRId64": ENOTSUP "
                        "(load \"features/locks\" translator to enable "
                        "lock support)",
                        fd->inode->ino);
        }

        STACK_UNWIND (frame, -1, ENOTSUP, &nullock);
        return 0;
}/* bdb_lk */

/* bdb_lookup
 *
 * there are four possibilities for a file being looked up:
 *  1. file exists and is a directory.
 *  2. file exists and is a symlink.
 *  3. file exists and is a regular file.
 *  4. file does not exist.
 * case 1 and 2 are handled by doing lstat() on the @loc. if the file is a
 * directory or symlink, lstat() succeeds. lookup continues to check if the
 * @loc belongs to case-3 only if lstat() fails.
 * to check for case 3, bdb_lookup does a bdb_db_iread() for the given @loc.
 * (see description of bdb_db_iread() for more details on how @loc is transformed
 * into db handle and key). if check for case 1, 2 and 3 fail, we proceed to
 * conclude that file doesn't exist (case 4).
 *
 * @frame:      call frame.
 * @this:       xlator_t of this instance of bdb xlator.
 * @loc:        loc_t specifying the file to operate upon.
 * @need_xattr: if need_xattr != 0, we are asked to return all the extended
 *   attributed of @loc, if any exist, in a dictionary. if @loc is a regular
 *   file and need_xattr is set, then we look for value of need_xattr. if
 *   need_xattr > sizo-of-the-file @loc, then the file content of @loc is
 *   returned in dictionary of xattr with 'glusterfs.content' as dictionary key.
 *
 * NOTE: bdb currently supports only directories, symlinks and regular files.
 *
 * NOTE: bdb_lookup returns the 'struct stat' of underlying file itself, in
 *  case of directory and symlink (st_ino is modified as bdb allocates its own
 *  set of inodes of all files). for regular files, bdb uses 'struct stat' of
 *  the database file in which the @loc is stored as templete and modifies
 *  st_ino (see bdb_inode_transform for more details), st_mode (can be set in
 *  volfile 'option file-mode <mode>'), st_size (exact size of the @loc
 *  contents), st_blocks (block count on the underlying filesystem to
 *  accomodate st_size, see BDB_COUNT_BLOCKS in bdb.h for more details).
 */
int32_t
bdb_lookup (call_frame_t *frame,
            xlator_t *this,
            loc_t *loc,
            dict_t *xattr_req)
{
        struct stat stbuf           = {0, };
        int32_t op_ret              = -1;
        int32_t op_errno            = ENOENT;
        dict_t *xattr               = NULL;
        char *pathname              = NULL;
        char *directory             = NULL;
        char *real_path             = NULL;
        bctx_t *bctx                = NULL;
        char *db_path               = NULL;
        struct bdb_private *private = NULL;
        char *key_string            = NULL;
        int32_t entry_size          = 0;
        char *file_content          = NULL;
        uint64_t   need_xattr       = 0;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);

        private = this->private;

        MAKE_REAL_PATH (real_path, this, loc->path);

        pathname = strdup (loc->path);
        GF_VALIDATE_OR_GOTO (this->name, pathname, out);

        directory = dirname (pathname);
        GF_VALIDATE_OR_GOTO (this->name, directory, out);

        if (!strcmp (directory, loc->path)) {
                /* SPECIAL CASE: looking up root */
                op_ret = lstat (real_path, &stbuf);
                if (op_ret != 0) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "LOOKUP %"PRId64" (%s): %s",
                                loc->ino, loc->path, strerror (op_errno));
                        goto out;
                }

                /* bctx_lookup() returns NULL only when its time to wind up,
                 * we should shutdown functioning */
                bctx = bctx_lookup (B_TABLE(this), (char *)loc->path);
                if (bctx == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "LOOKUP %"PRId64" (%s): ENOMEM"
                                "(failed to lookup database handle)",
                                loc->ino, loc->path);
                        op_ret   = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

                stbuf.st_ino = 1;
                stbuf.st_mode = private->dir_mode;

                op_ret = 0;
                goto out;
        }

        MAKE_KEY_FROM_PATH (key_string, loc->path);
        op_ret = lstat (real_path, &stbuf);
        if ((op_ret == 0) && (S_ISDIR (stbuf.st_mode))){
                bctx = bctx_lookup (B_TABLE(this), (char *)loc->path);
                if (bctx == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "LOOKUP %"PRId64"/%s (%s): ENOMEM"
                                "(failed to lookup database handle)",
                                loc->parent->ino, loc->name, loc->path);
                        op_ret   = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

                if (loc->ino) {
                        /* revalidating directory inode */
                        stbuf.st_ino = loc->ino;
                } else {
                        stbuf.st_ino = bdb_inode_transform (loc->parent->ino,
                                                            key_string,
                                                            strlen (key_string));
                }
                stbuf.st_mode = private->dir_mode;

                op_ret = 0;
                goto out;

        } else if (op_ret == 0) {
                /* a symlink */
                bctx = bctx_parent (B_TABLE(this), loc->path);
                if (bctx == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "LOOKUP %"PRId64"/%s (%s): ENOMEM"
                                "(failed to lookup database handle)",
                                loc->parent->ino, loc->name, loc->path);
                        op_ret   = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

                if (loc->ino) {
                        stbuf.st_ino = loc->ino;
                } else {
                        stbuf.st_ino = bdb_inode_transform (loc->parent->ino,
                                                            key_string,
                                                            strlen (key_string));
                }

                stbuf.st_mode = private->symlink_mode;

                op_ret = 0;
                goto out;

        }

        /* for regular files */
        bctx = bctx_parent (B_TABLE(this), loc->path);
        if (bctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "LOOKUP %"PRId64"/%s (%s): ENOMEM"
                        "(failed to lookup database handle for parent)",
                        loc->parent->ino, loc->name, loc->path);
                op_ret   = -1;
                op_errno = ENOMEM;
                goto out;
        }

        if (GF_FILE_CONTENT_REQUESTED(xattr_req, &need_xattr)) {
                entry_size = bdb_db_iread (bctx, key_string, &file_content);
        } else {
                entry_size = bdb_db_iread (bctx, key_string, NULL);
        }

        op_ret = entry_size;
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "LOOKUP %"PRId64"/%s (%s): ENOENT"
                        "(database entry not found)",
                        loc->parent->ino, loc->name, loc->path);
                op_errno = ENOENT;
                goto out;
        }

        MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bctx->directory);
        op_ret = lstat (db_path, &stbuf);
        if (op_ret != 0) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "LOOKUP %"PRId64"/%s (%s): %s",
                        loc->parent->ino, loc->name, loc->path,
                        strerror (op_errno));
                goto out;
        }

        if (entry_size
            && (need_xattr >= entry_size)
            && (file_content)) {
                xattr = dict_new ();
                op_ret = dict_set_dynptr (xattr, "glusterfs.content",
                                          file_content, entry_size);
                if (op_ret < 0) {
                        /* continue without giving file contents */
                        FREE (file_content);
                }
        } else {
                if (file_content)
                        FREE (file_content);
        }

        if (loc->ino) {
                /* revalidate */
                stbuf.st_ino = loc->ino;
                stbuf.st_size = entry_size;
                stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size,
                                                    stbuf.st_blksize);
        } else {
                /* fresh lookup, create an inode number */
                stbuf.st_ino = bdb_inode_transform (loc->parent->ino,
                                                    key_string,
                                                    strlen (key_string));
                stbuf.st_size = entry_size;
                stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size,
                                                    stbuf.st_blksize);
        }/* if(inode->ino)...else */
        stbuf.st_nlink = 1;
        stbuf.st_mode = private->file_mode;

        op_ret = 0;
out:
        if (bctx) {
                /* NOTE: bctx_unref always returns success,
                 * see description of bctx_unref for more details */
                bctx_unref (bctx);
        }

        if (pathname)
                free (pathname);

        if (xattr)
                dict_ref (xattr);

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

        struct stat stbuf           = {0,};
        char *real_path             = NULL;
        int32_t op_ret              = -1;
        int32_t op_errno            = EINVAL;
        struct bdb_private *private = NULL;
        char *db_path               = NULL;
        bctx_t *bctx                = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);

        private = this->private;
        GF_VALIDATE_OR_GOTO (this->name, private, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = lstat (real_path, &stbuf);
        op_errno = errno;
        if (op_ret == 0) {
                /* directory or symlink */
                stbuf.st_ino = loc->inode->ino;
                if (S_ISDIR(stbuf.st_mode))
                        stbuf.st_mode = private->dir_mode;
                else
                        stbuf.st_mode = private->symlink_mode;
                /* we are done, lets unwind the stack */
                goto out;
        }

        bctx = bctx_parent (B_TABLE(this), loc->path);
        if (bctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "STAT %"PRId64" (%s): ENOMEM"
                        "(no database handle for parent)",
                        loc->ino, loc->path);
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bctx->directory);
        op_ret = lstat (db_path, &stbuf);
        if (op_ret < 0) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "STAT %"PRId64" (%s): %s"
                        "(failed to stat on database file)",
                        loc->ino, loc->path, strerror (op_errno));
                goto out;
        }

        stbuf.st_size = bdb_db_iread (bctx, loc->path, NULL);
        stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size, stbuf.st_blksize);
        stbuf.st_ino = loc->inode->ino;

out:
        if (bctx) {
                /* NOTE: bctx_unref always returns success,
                 * see description of bctx_unref for more details */
                bctx_unref (bctx);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

        return 0;
}/* bdb_stat */



/* bdb_opendir - in the world of bdb, open/opendir is all about opening
 *   correspondind databases. opendir in particular, opens the database for the
 *   directory which is to be opened. after opening the database, a cursor to
 *   the database is also created. cursor helps us get the dentries one after
 *   the other, and cursor maintains the state about current positions in
 *   directory. pack 'pointer to db', 'pointer to the cursor' into
 *   struct bdb_dir and store it in fd->ctx, we get from our parent xlator.
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
        char           *real_path = NULL;
        int32_t         op_ret    = -1;
        int32_t         op_errno  = EINVAL;
        bctx_t         *bctx      = NULL;
        struct bdb_dir *bfd       = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        bctx = bctx_lookup (B_TABLE(this), (char *)loc->path);
        if (bctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "OPENDIR %"PRId64" (%s): ENOMEM"
                        "(no database handle for directory)",
                        loc->ino, loc->path);
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        bfd = CALLOC (1, sizeof (*bfd));
        if (bfd == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "OPENDIR %"PRId64" (%s): ENOMEM"
                        "(failed to allocate memory for internal fd)",
                        loc->ino, loc->path);
                op_ret = -1;
                op_errno = ENOMEM;
                goto err;
        }

        bfd->dir = opendir (real_path);
        if (bfd->dir == NULL) {
                op_ret   = -1;
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "OPENDIR %"PRId64" (%s): %s",
                        loc->ino, loc->path, strerror (op_errno));
                goto err;
        }

        /* NOTE: bctx_lookup() return bctx with ref */
        bfd->ctx = bctx;

        bfd->path = strdup (real_path);
        if (bfd == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "OPENDIR %"PRId64" (%s): ENOMEM"
                        "(failed to allocate memory for internal fd->path)",
                        loc->ino, loc->path);
                op_ret = -1;
                op_errno = ENOMEM;
                goto err;
        }

        BDB_FCTX_SET (fd, this, bfd);
        op_ret = 0;
out:
        STACK_UNWIND (frame, op_ret, op_errno, fd);
        return 0;
err:
        if (bctx)
                bctx_unref (bctx);
        if (bfd) {
                if (bfd->dir)
                        closedir (bfd->dir);

                FREE (bfd);
        }

        return 0;
}/* bdb_opendir */

int32_t
bdb_getdents (call_frame_t *frame,
              xlator_t     *this,
              fd_t         *fd,
              size_t        size,
              off_t         off,
              int32_t       flag)
{
        struct bdb_dir *bfd        = NULL;
        int32_t         op_ret     = -1;
        int32_t         op_errno   = EINVAL;
        size_t          filled     = 0;
        dir_entry_t     entries    = {0, };
        dir_entry_t    *this_entry = NULL;
        char           *entry_path     = NULL;
        struct dirent  *dirent         = NULL;
        off_t           in_case    = 0;
        int32_t         this_size  = 0;
        DBC            *cursorp    = NULL;
        int32_t         ret            = -1;
        int32_t         real_path_len  = 0;
        int32_t         entry_path_len = 0;
        int32_t         count          = 0;
        off_t   offset = 0;
        size_t          tmp_name_len   = 0;
        struct stat     db_stbuf       = {0,};
        struct stat     buf            = {0,};

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        BDB_FCTX_GET (fd, this, &bfd);
        if (bfd == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "GETDENTS %"PRId64" - %"GF_PRI_SIZET",%"PRId64
                        " %o: EBADFD "
                        "(failed to find internal context in fd)",
                        fd->inode->ino, size, off, flag);
                op_errno = EBADFD;
                op_ret   = -1;
                goto out;
        }

        op_ret = bdb_cursor_open (bfd->ctx, &cursorp);
        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "GETDENTS %"PRId64" - %"GF_PRI_SIZET",%"PRId64
                        ": EBADFD "
                        "(failed to open cursor to database handle)",
                        fd->inode->ino, size, off);
                op_errno = EBADFD;
                goto out;
        }

        if (off) {
                DBT sec = {0,}, pri = {0,}, val = {0,};
                sec.data = &(off);
                sec.size = sizeof (off);
                sec.flags = DB_DBT_USERMEM;
                val.dlen = 0;
                val.doff = 0;
                val.flags = DB_DBT_PARTIAL;

                op_ret = bdb_cursor_get (cursorp, &sec, &pri, &val, DB_SET);
                if (op_ret == DB_NOTFOUND) {
                        offset = off;
                        goto dir_read;
                }
        }

        while (filled <= size) {
                DBT sec = {0,}, pri = {0,}, val = {0,};

                this_entry = NULL;

                sec.flags = DB_DBT_MALLOC;
                pri.flags = DB_DBT_MALLOC;
                val.dlen = 0;
                val.doff = 0;
                val.flags = DB_DBT_PARTIAL;
                op_ret = bdb_cursor_get (cursorp, &sec, &pri, &val, DB_NEXT);

                if (op_ret == DB_NOTFOUND) {
                        /* we reached end of the directory */
                        op_ret = 0;
                        op_errno = 0;
                        break;
                } else if (op_ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "GETDENTS %"PRId64" - %"GF_PRI_SIZET
                                ",%"PRId64":"
                                "(failed to read the next entry from database)",
                                fd->inode->ino, size, off);
                        op_errno = ENOENT;
                        break;
                } /* if (op_ret == DB_NOTFOUND)...else if...else */

                if (pri.data == NULL) {
                        /* NOTE: currently ignore when we get key.data == NULL.
                         * FIXME: we should not get key.data = NULL */
                        gf_log (this->name, GF_LOG_DEBUG,
                                "GETDENTS %"PRId64" - %"GF_PRI_SIZET
                                ",%"PRId64":"
                                "(null key read for entry from database)",
                                fd->inode->ino, size, off);
                        continue;
                }/* if(key.data)...else */

                this_entry = CALLOC (1, sizeof (*this_entry));
                if (this_entry == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "GETDENTS %"PRId64" - %"GF_PRI_SIZET",%"PRId64
                                " - %s:"
                                "(failed to allocate memory for an entry)",
                                fd->inode->ino, size, off, strerror (errno));
                        op_errno = ENOMEM;
                        op_ret   = -1;
                        goto out;
                }

                this_entry->name = CALLOC (pri.size + 1, sizeof (char));
                if (this_entry->name == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "GETDENTS %"PRId64" - %"GF_PRI_SIZET",%"PRId64
                                " - %s:"
                                "(failed to allocate memory for an "
                                "entry->name)",
                                fd->inode->ino, size, off, strerror (errno));
                        op_errno = ENOMEM;
                        op_ret   = -1;
                        goto out;
                }

                memcpy (this_entry->name, pri.data, pri.size);
                this_entry->buf = db_stbuf;
                this_entry->buf.st_size = bdb_db_iread (bfd->ctx,
                                                        this_entry->name, NULL);
                this_entry->buf.st_blocks = BDB_COUNT_BLOCKS (
                        this_entry->buf.st_size,
                        this_entry->buf.st_blksize);

                this_entry->buf.st_ino = bdb_inode_transform (fd->inode->ino,
                                                              pri.data,
                                                              pri.size);
                count++;

                this_entry->next = entries.next;
                this_entry->link = "";
                entries.next = this_entry;
                /* if size is 0, count can never be = size,
                 * so entire dir is read */
                if (sec.data)
                        FREE (sec.data);

                if (pri.data)
                        FREE (pri.data);

                if (count == size)
                        break;
        }/* while */
        bdb_cursor_close (bfd->ctx, cursorp);
        op_ret = count;
        op_errno = 0;
        if (count >= size)
                goto out;
dir_read:
        /* hungry kyaa? */
        if (!offset) {
                rewinddir (bfd->dir);
        } else {
                seekdir (bfd->dir, offset);
        }

        while (filled <= size) {
                this_entry = NULL;
                this_size  = 0;

                in_case = telldir (bfd->dir);
                dirent = readdir (bfd->dir);
                if (!dirent)
                        break;

                if (IS_BDB_PRIVATE_FILE(dirent->d_name))
                        continue;

                tmp_name_len = strlen (dirent->d_name);
                if (entry_path_len < (real_path_len + 1 + (tmp_name_len) + 1)) {
                        entry_path_len = real_path_len + tmp_name_len + 1024;
                        entry_path = realloc (entry_path, entry_path_len);
                        if (entry_path == NULL) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "GETDENTS %"PRId64" - %"GF_PRI_SIZET","
                                        "%"PRId64" - %s: (failed to allocate "
                                        "memory for an entry_path)",
                                        fd->inode->ino, size, off,
                                        strerror (errno));
                                op_errno = ENOMEM;
                                op_ret   = -1;
                                goto out;
                        }
                }

                strncpy (&entry_path[real_path_len+1], dirent->d_name,
                         tmp_name_len);
                op_ret = stat (entry_path, &buf);
                if (op_ret < 0) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "GETDENTS %"PRId64" - %"GF_PRI_SIZET",%"PRId64
                                " - %s:"
                                " (failed to stat on an entry '%s')",
                                fd->inode->ino, size, off,
                                strerror (errno), entry_path);
                        goto out; /* FIXME: shouldn't we continue here */
                }

                if ((flag == GF_GET_DIR_ONLY) &&
                    ((ret != -1) && (!S_ISDIR(buf.st_mode)))) {
                        continue;
                }

                this_entry = CALLOC (1, sizeof (*this_entry));
                if (this_entry == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "GETDENTS %"PRId64" - %"GF_PRI_SIZET",%"PRId64
                                " - %s:"
                                "(failed to allocate memory for an entry)",
                                fd->inode->ino, size, off, strerror (errno));
                        op_errno = ENOMEM;
                        op_ret   = -1;
                        goto out;
                }

                this_entry->name = strdup (dirent->d_name);
                if (this_entry->name == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "GETDENTS %"PRId64" - %"GF_PRI_SIZET",%"PRId64
                                " - %s:"
                                "(failed to allocate memory for an "
                                "entry->name)",
                                fd->inode->ino, size, off, strerror (errno));
                        op_errno = ENOMEM;
                        op_ret   = -1;
                        goto out;
                }

                this_entry->buf = buf;

                this_entry->buf.st_ino = -1;
                if (S_ISLNK(this_entry->buf.st_mode)) {
                        char linkpath[ZR_PATH_MAX] = {0,};
                        ret = readlink (entry_path, linkpath, ZR_PATH_MAX);
                        if (ret != -1) {
                                linkpath[ret] = '\0';
                                this_entry->link = strdup (linkpath);
                        }
                } else {
                        this_entry->link = "";
                }

                count++;

                this_entry->next = entries.next;
                entries.next = this_entry;

                /* if size is 0, count can never be = size,
                 * so entire dir is read */
                if (count == size)
                        break;
        }
        op_ret = filled;
        op_errno = 0;

out:
        gf_log (this->name, GF_LOG_DEBUG,
                "GETDENTS %"PRId64" - %"GF_PRI_SIZET" (%"PRId32")"
                "/%"GF_PRI_SIZET",%"PRId64":"
                "(failed to read the next entry from database)",
                fd->inode->ino, filled, count, size, off);

        STACK_UNWIND (frame, count, op_errno, &entries);

        while (entries.next) {
                this_entry = entries.next;
                entries.next = entries.next->next;
                FREE (this_entry->name);
                FREE (this_entry);
        }

        return 0;
}/* bdb_getdents */


int32_t
bdb_releasedir (xlator_t *this,
                fd_t *fd)
{
        int32_t op_ret = 0;
        int32_t op_errno = 0;
        struct bdb_dir *bfd = NULL;

        BDB_FCTX_GET (fd, this, &bfd);
        if (bfd == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "RELEASEDIR %"PRId64": EBADFD",
                        fd->inode->ino);
                op_errno = EBADFD;
                op_ret   = -1;
                goto out;
        }

        if (bfd->path) {
                free (bfd->path);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "RELEASEDIR %"PRId64": (bfd->path is NULL)",
                        fd->inode->ino);
        }

        if (bfd->dir) {
                closedir (bfd->dir);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "RELEASEDIR %"PRId64": (bfd->dir is NULL)",
                        fd->inode->ino);
        }

        if (bfd->ctx) {
                bctx_unref (bfd->ctx);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "RELEASEDIR %"PRId64": (bfd->ctx is NULL)",
                        fd->inode->ino);
        }

        free (bfd);

out:
        return 0;
}/* bdb_releasedir */


int32_t
bdb_readlink (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              size_t size)
{
        char   *dest      = NULL;
        int32_t op_ret    = -1;
        int32_t op_errno  = EPERM;
        char   *real_path = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);

        dest = alloca (size + 1);
        GF_VALIDATE_OR_GOTO (this->name, dest, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = readlink (real_path, dest, size);

        if (op_ret > 0)
                dest[op_ret] = 0;

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "READLINK %"PRId64" (%s): %s",
                        loc->ino, loc->path, strerror (op_errno));
        }
out:
        STACK_UNWIND (frame, op_ret, op_errno, dest);

        return 0;
}/* bdb_readlink */


int32_t
bdb_mkdir (call_frame_t *frame,
           xlator_t *this,
           loc_t *loc,
           mode_t mode)
{
        int32_t op_ret = -1;
        int32_t ret = -1;
        int32_t op_errno = EINVAL;
        char *real_path = NULL;
        struct stat stbuf = {0, };
        bctx_t *bctx = NULL;
        char *key_string = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);

        MAKE_KEY_FROM_PATH (key_string, loc->path);
        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = mkdir (real_path, mode);
        if (op_ret < 0) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "MKDIR %"PRId64" (%s): %s",
                        loc->ino, loc->path, strerror (op_errno));
                goto out;
        }

        op_ret = chown (real_path, frame->root->uid, frame->root->gid);
        if (op_ret < 0) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "MKDIR %"PRId64" (%s): %s "
                        "(failed to do chmod)",
                        loc->ino, loc->path, strerror (op_errno));
                goto err;
        }

        op_ret = lstat (real_path, &stbuf);
        if (op_ret < 0) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "MKDIR %"PRId64" (%s): %s "
                        "(failed to do lstat)",
                        loc->ino, loc->path, strerror (op_errno));
                goto err;
        }

        bctx = bctx_lookup (B_TABLE(this), (char *)loc->path);
        if (bctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "MKDIR %"PRId64" (%s): ENOMEM"
                        "(no database handle for parent)",
                        loc->ino, loc->path);
                op_ret = -1;
                op_errno = ENOMEM;
                goto err;
        }

        stbuf.st_ino = bdb_inode_transform (loc->parent->ino, key_string,
                                            strlen (key_string));

        goto out;

err:
        ret = rmdir (real_path);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "MKDIR %"PRId64" (%s): %s"
                        "(failed to do rmdir)",
                        loc->ino, loc->path, strerror (errno));
        }

out:
        if (bctx) {
                /* NOTE: bctx_unref always returns success,
                 * see description of bctx_unref for more details */
                bctx_unref (bctx);
        }

        STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf);

        return 0;
}/* bdb_mkdir */


int32_t
bdb_unlink (call_frame_t *frame,
            xlator_t *this,
            loc_t *loc)
{
        int32_t op_ret    = -1;
        int32_t op_errno  = EINVAL;
        bctx_t *bctx      = NULL;
        char   *real_path = NULL;
        char   *key_string = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);

        bctx = bctx_parent (B_TABLE(this), loc->path);
        if (bctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "UNLINK %"PRId64" (%s): ENOMEM"
                        "(no database handle for parent)",
                        loc->ino, loc->path);
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        MAKE_KEY_FROM_PATH (key_string, loc->path);
        op_ret = bdb_db_iremove (bctx, key_string);
        if (op_ret == DB_NOTFOUND) {
                MAKE_REAL_PATH (real_path, this, loc->path);
                op_ret = unlink (real_path);
                if (op_ret != 0) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "UNLINK %"PRId64" (%s): %s"
                                "(symlink unlink failed)",
                                loc->ino, loc->path, strerror (op_errno));
                        goto out;
                }
        } else if (op_ret == 0) {
                op_errno = 0;
        }
out:
        if (bctx) {
                /* NOTE: bctx_unref always returns success,
                 * see description of bctx_unref for more details */
                bctx_unref (bctx);
        }

        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}/* bdb_unlink */



static int32_t
bdb_do_rmdir (xlator_t *this,
              loc_t *loc)
{
        char   *real_path = NULL;
        int32_t ret       = -1;
        bctx_t *bctx      = NULL;
        DB_ENV *dbenv     = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);

        dbenv = BDB_ENV(this);
        GF_VALIDATE_OR_GOTO (this->name, dbenv, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        bctx = bctx_lookup (B_TABLE(this), loc->path);
        if (bctx == NULL) {
                ret = -ENOMEM;
                goto out;
        }

        LOCK(&bctx->lock);
        {
                if ((bctx->primary == NULL)
                    || (bctx->secondary == NULL)) {
                        goto unlock;
                }

                ret = bctx->primary->close (bctx->primary, 0);
                if (ret < 0) {
                        ret = -EINVAL;
                }

                ret = bctx->secondary->close (bctx->secondary, 0);
                if (ret < 0) {
                        ret = -EINVAL;
                }

                ret = dbenv->dbremove (dbenv, NULL, bctx->db_path,
                                       "primary", 0);
                if (ret < 0) {
                        ret = -EBUSY;
                }

                ret = dbenv->dbremove (dbenv, NULL, bctx->db_path,
                                       "secondary", 0);
                if (ret != 0) {
                        ret = -EBUSY;
                }
        }
unlock:
        UNLOCK(&bctx->lock);

        if (ret) {
                goto out;
        }
        ret = rmdir (real_path);

out:
        if (bctx) {
                /* NOTE: bctx_unref always returns success,
                 * see description of bctx_unref for more details */
                bctx_unref (bctx);
        }

        return ret;
}

int32_t
bdb_rmdir (call_frame_t *frame,
           xlator_t *this,
           loc_t *loc)
{
        int32_t op_ret   = -1;
        int32_t op_errno = 0;

        op_ret = is_dir_empty (this, loc);
        if (op_ret < 0) {
                op_errno = -op_ret;
                gf_log (this->name, GF_LOG_DEBUG,
                        "RMDIR %"PRId64" (%s): %s"
                        "(internal rmdir routine returned error)",
                        loc->ino, loc->path, strerror (op_errno));
        } else if (op_ret == 0) {
                op_ret   = -1;
                op_errno = ENOTEMPTY;
                gf_log (this->name, GF_LOG_DEBUG,
                        "RMDIR %"PRId64" (%s): ENOTEMPTY",
                        loc->ino, loc->path);
                goto out;
        }

        op_ret = bdb_do_rmdir (this, loc);
        if (op_ret < 0) {
                op_errno = -op_ret;
                gf_log (this->name, GF_LOG_DEBUG,
                        "RMDIR %"PRId64" (%s): %s"
                        "(internal rmdir routine returned error)",
                        loc->ino, loc->path, strerror (op_errno));
                goto out;
        }

out:
        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
} /* bdb_rmdir */

int32_t
bdb_symlink (call_frame_t *frame,
             xlator_t *this,
             const char *linkname,
             loc_t *loc)
{
        int32_t             op_ret    = -1;
        int32_t             op_errno  = EINVAL;
        char               *real_path = NULL;
        struct stat         stbuf     = {0,};
        struct bdb_private *private   = NULL;
        bctx_t             *bctx      = NULL;
        char               *key_string = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, linkname, out);

        private = this->private;
        GF_VALIDATE_OR_GOTO (this->name, private, out);

        MAKE_KEY_FROM_PATH (key_string, loc->path);

        MAKE_REAL_PATH (real_path, this, loc->path);
        op_ret = symlink (linkname, real_path);
        op_errno = errno;
        if (op_ret == 0) {
                op_ret = lstat (real_path, &stbuf);
                if (op_ret != 0) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "SYMLINK %"PRId64" (%s): %s",
                                loc->ino, loc->path, strerror (op_errno));
                        goto err;
                }

                bctx = bctx_parent (B_TABLE(this), loc->path);
                if (bctx == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "SYMLINK %"PRId64" (%s): ENOMEM"
                                "(no database handle for parent)",
                                loc->ino, loc->path);
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto err;
                }

                stbuf.st_ino = bdb_inode_transform (loc->parent->ino,
                                                    key_string,
                                                    strlen (key_string));
                stbuf.st_mode = private->symlink_mode;

                goto out;
        }
err:
        op_ret = unlink (real_path);
        op_errno = errno;
        if (op_ret != 0) {
               gf_log (this->name, GF_LOG_DEBUG,
                       "SYMLINK %"PRId64" (%s): %s"
                       "(failed to unlink the created symlink)",
                       loc->ino, loc->path, strerror (op_errno));
        }
        op_ret = -1;
        op_errno = ENOENT;
out:
        if (bctx) {
                /* NOTE: bctx_unref always returns success,
                 * see description of bctx_unref for more details */
                bctx_unref (bctx);
        }

        STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf);

        return 0;
} /* bdb_symlink */

static int
bdb_do_chmod (xlator_t *this,
              const char *path,
              struct stat *stbuf)
{
        int32_t ret = -1;

        ret = lchmod (path, stbuf->st_mode);
        if ((ret == -1) && (errno == ENOSYS)) {
                ret = chmod (path, stbuf->st_mode);
        }

        return ret;
}

static int
bdb_do_chown (xlator_t *this,
              const char *path,
              struct stat *stbuf,
              int32_t valid)
{
        int32_t ret = -1;
        uid_t uid = -1;
        gid_t gid = -1;

        if (valid & GF_SET_ATTR_UID)
                uid = stbuf->st_uid;

        if (valid & GF_SET_ATTR_GID)
                gid = stbuf->st_gid;

        ret = lchown (path, uid, gid);

        return ret;
}

static int
bdb_do_utimes (xlator_t *this,
               const char *path,
               struct stat *stbuf)
{
        int32_t ret = -1;
        struct timeval tv[2]     = {{0,},{0,}};

        tv[0].tv_sec  = stbuf->st_atime;
        tv[0].tv_usec = ST_ATIM_NSEC (stbuf) / 1000;
        tv[1].tv_sec  = stbuf->st_mtime;
        tv[1].tv_usec = ST_ATIM_NSEC (stbuf) / 1000;

        ret = lutimes (path, tv);

        return ret;
}

int32_t
bdb_setattr (call_frame_t *frame,
             xlator_t *this,
             loc_t *loc,
             struct stat *stbuf,
             int32_t valid)
{
        int32_t     op_ret    = -1;
        int32_t     op_errno  = EINVAL;
        char       *real_path = NULL;
        struct stat preop     = {0,};
        struct stat postop    = {0,};

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);

        MAKE_REAL_PATH (real_path, this, loc->path);
        op_ret = lstat (real_path, &preop);
        op_errno = errno;
        if (op_ret != 0) {
                if (op_errno == ENOENT) {
                        op_errno = EPERM;
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "CHMOD %"PRId64" (%s): %s"
                                "(pre-op lstat failed)",
                                loc->ino, loc->path, strerror (op_errno));
                }
                goto out;
        }

        /* directory or symlink */
        if (valid & GF_SET_ATTR_MODE) {
                op_ret = bdb_do_chmod (this, real_path, stbuf);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "setattr (chmod) on %s failed: %s", loc->path,
                                strerror (op_errno));
                        goto out;
                }
        }

        if (valid & (GF_SET_ATTR_UID | GF_SET_ATTR_GID)){
                op_ret = bdb_do_chown (this, real_path, stbuf, valid);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "setattr (chown) on %s failed: %s", loc->path,
                                strerror (op_errno));
                        goto out;
                }
        }

        if (valid & (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME)) {
                op_ret = bdb_do_utimes (this, real_path, stbuf);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "setattr (utimes) on %s failed: %s", loc->path,
                                strerror (op_errno));
                        goto out;
                }
        }

        op_ret = lstat (real_path, &postop);
        op_errno = errno;
        if (op_ret != 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "CHMOD %"PRId64" (%s): %s"
                                "(post-op lstat failed)",
                                loc->ino, loc->path, strerror (op_errno));
        }

out:
        STACK_UNWIND (frame, op_ret, op_errno, &preop, &postop);

        return 0;
}/* bdb_setattr */

int32_t
bdb_fsetattr (call_frame_t *frame,
              xlator_t *this,
              fd_t *fd,
              struct stat *stbuf,
              int32_t valid)
{
        int32_t     op_ret    = -1;
        int32_t     op_errno  = EPERM;
        struct stat preop     = {0,};
        struct stat postop    = {0,};

        STACK_UNWIND (frame, op_ret, op_errno, &preop, &postop);

        return 0;
}/* bdb_fsetattr */


int32_t
bdb_truncate (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              off_t offset)
{
        int32_t     op_ret     = -1;
        int32_t     op_errno   = EINVAL;
        char       *real_path  = NULL;
        struct stat stbuf      = {0,};
        char       *db_path    = NULL;
        bctx_t     *bctx       = NULL;
        char       *key_string = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);

        bctx = bctx_parent (B_TABLE(this), loc->path);
        if (bctx == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "TRUNCATE %"PRId64" (%s): ENOMEM"
                        "(no database handle for parent)",
                        loc->ino, loc->path);
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        MAKE_REAL_PATH (real_path, this, loc->path);
        MAKE_KEY_FROM_PATH (key_string, loc->path);

        /* now truncate */
        MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bctx->directory);
        op_ret = lstat (db_path, &stbuf);
        if (op_ret != 0) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "TRUNCATE %"PRId64" (%s): %s"
                        "(lstat on database file failed)",
                        loc->ino, loc->path, strerror (op_errno));
                goto out;
        }

        if (loc->inode->ino) {
                stbuf.st_ino = loc->inode->ino;
        }else {
                stbuf.st_ino = bdb_inode_transform (loc->parent->ino,
                                                    key_string,
                                                    strlen (key_string));
        }

        op_ret = bdb_db_itruncate (bctx, key_string);
        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "TRUNCATE %"PRId64" (%s): EINVAL"
                        "(truncating entry in  database failed - %s)",
                        loc->ino, loc->path, db_strerror (op_ret));
                op_errno = EINVAL; /* TODO: better errno */
        }

out:
        if (bctx) {
                /* NOTE: bctx_unref always returns success,
                 * see description of bctx_unref for more details */
                bctx_unref (bctx);
        }

        STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

        return 0;
}/* bdb_truncate */


int32_t
bdb_statfs (call_frame_t *frame,
            xlator_t *this,
            loc_t *loc)

{
        int32_t        op_ret    = -1;
        int32_t        op_errno  = EINVAL;
        char          *real_path = NULL;
        struct statvfs buf       = {0, };

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = statvfs (real_path, &buf);
        op_errno = errno;
out:
        STACK_UNWIND (frame, op_ret, op_errno, &buf);
        return 0;
}/* bdb_statfs */

static int gf_bdb_xattr_log;

/* bdb_setxattr - set extended attributes.
 *
 * bdb allows setxattr operation only on directories.
 *    bdb reservers 'glusterfs.file.<attribute-name>' to operate on the content
 *  of the files under the specified directory.
 * 'glusterfs.file.<attribute-name>' transforms to contents of file of name
 * '<attribute-name>' under specified directory.
 *
 * @frame: call frame.
 * @this:  xlator_t of this instance of bdb xlator.
 * @loc:   loc_t specifying the file to operate upon.
 * @dict:  list of extended attributes to set on @loc.
 * @flags: can be XATTR_REPLACE (replace an existing extended attribute only if
 *         it exists) or XATTR_CREATE (create an extended attribute only if it
 *         doesn't already exist).
 *
 *
 */
int32_t
bdb_setxattr (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              dict_t *dict,
              int flags)
{
        int32_t      op_ret = -1;
        int32_t      op_errno = EINVAL;
        data_pair_t *trav = dict->members_list;
        bctx_t      *bctx = NULL;
        char        *real_path = NULL;
        char        *key = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, dict, out);

        MAKE_REAL_PATH (real_path, this, loc->path);
        if (!S_ISDIR (loc->inode->st_mode)) {
                op_ret   = -1;
                op_errno = ENOATTR;
                goto out;
        }

        while (trav) {
                if (GF_FILE_CONTENT_REQUEST(trav->key) ) {
                        key = BDB_KEY_FROM_FREQUEST_KEY(trav->key);

                        bctx = bctx_lookup (B_TABLE(this), loc->path);
                        if (bctx == NULL) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "SETXATTR %"PRId64" (%s) - %s: ENOMEM"
                                        "(no database handle for directory)",
                                        loc->ino, loc->path, key);
                                op_ret = -1;
                                op_errno = ENOMEM;
                                goto out;
                        }

                        if (flags & XATTR_REPLACE) {
                                op_ret = bdb_db_itruncate (bctx, key);
                                if (op_ret == -1) {
                                        /* key doesn't exist in database */
                                        gf_log (this->name, GF_LOG_DEBUG,
                                                "SETXATTR %"PRId64" (%s) - %s:"
                                                " (entry not present in "
                                                "database)",
                                                loc->ino, loc->path, key);
                                        op_ret = -1;
                                        op_errno = ENOATTR;
                                        break;
                                }
                                op_ret = bdb_db_iwrite (bctx, key,
                                                        trav->value->data,
                                                        trav->value->len);
                                if (op_ret != 0) {
                                        op_ret   = -1;
                                        op_errno = ENOATTR;
                                        break;
                                }
                        } else {
                                /* fresh create */
                                op_ret = bdb_db_iwrite (bctx, key,
                                                        trav->value->data,
                                                        trav->value->len);
                                if (op_ret != 0) {
                                        op_ret   = -1;
                                        op_errno = EEXIST;
                                        break;
                                } else {
                                        op_ret = 0;
                                        op_errno = 0;
                                } /* if(op_ret!=0)...else */
                        } /* if(flags&XATTR_REPLACE)...else */
                        if (bctx) {
                                /* NOTE: bctx_unref always returns success, see
                                 * description of bctx_unref for more details */
                                bctx_unref (bctx);
                        }
                } else {
                        /* do plain setxattr */
                        op_ret = lsetxattr (real_path,
                                            trav->key, trav->value->data,
                                            trav->value->len,
                                            flags);
                        op_errno = errno;

                        if ((op_errno == ENOATTR) || (op_errno == EEXIST)) {
                                /* don't log, normal behaviour */
                                ;
                        } else if (BDB_TIMED_LOG (op_errno, gf_bdb_xattr_log)) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "SETXATTR %"PRId64" (%s) - %s: %s",
                                        loc->ino, loc->path, trav->key,
                                        strerror (op_errno));
                                /* do not continue, break out */
                                break;
                        } else {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "SETXATTR %"PRId64" (%s) - %s: %s",
                                        loc->ino, loc->path, trav->key,
                                        strerror (op_errno));
                        }
                } /* if(ZR_FILE_CONTENT_REQUEST())...else */
                trav = trav->next;
        }/* while(trav) */
out:
        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}/* bdb_setxattr */


/* bdb_gettxattr - get extended attributes.
 *
 * bdb allows getxattr operation only on directories.
 * bdb_getxattr retrieves the whole content of the file, when
 * glusterfs.file.<attribute-name> is specified.
 *
 * @frame: call frame.
 * @this:  xlator_t of this instance of bdb xlator.
 * @loc:   loc_t specifying the file to operate upon.
 * @name:  name of extended attributes to get for @loc.
 *
 * NOTE: see description of bdb_setxattr for details on how
 *     'glusterfs.file.<attribute-name>' is handles by bdb.
 */
int32_t
bdb_getxattr (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              const char *name)
{
        int32_t op_ret         = 0;
        int32_t op_errno       = 0;
        dict_t *dict           = NULL;
        bctx_t *bctx           = NULL;
        char   *buf            = NULL;
        char   *key_string     = NULL;
        int32_t list_offset    = 0;
        size_t  size           = 0;
        size_t  remaining_size = 0;
        char   *real_path      = NULL;
        char    key[1024]      = {0,};
        char   *value          = NULL;
        char   *list           = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, name, out);

        dict = dict_new ();
        GF_VALIDATE_OR_GOTO (this->name, dict, out);

        if (!S_ISDIR (loc->inode->st_mode)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "GETXATTR %"PRId64" (%s) - %s: ENOATTR "
                        "(not a directory)",
                        loc->ino, loc->path, name);
                op_ret = -1;
                op_errno = ENOATTR;
                goto out;
        }

        if (name && GF_FILE_CONTENT_REQUEST(name)) {
                bctx = bctx_lookup (B_TABLE(this), loc->path);
                if (bctx == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "GETXATTR %"PRId64" (%s) - %s: ENOMEM"
                                "(no database handle for directory)",
                                loc->ino, loc->path, name);
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

                key_string = BDB_KEY_FROM_FREQUEST_KEY(name);

                op_ret = bdb_db_iread (bctx, key_string, &buf);
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "GETXATTR %"PRId64" (%s) - %s: ENOATTR"
                                "(attribute not present in database)",
                                loc->ino, loc->path, name);
                        op_errno = ENOATTR;
                        goto out;
                }

                op_ret = dict_set_dynptr (dict, (char *)name, buf, op_ret);
                if (op_ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "GETXATTR %"PRId64" (%s) - %s: ENOATTR"
                                "(attribute present in database, "
                                "dict set failed)",
                                loc->ino, loc->path, name);
                        op_errno = ENODATA;
                }

                goto out;
        }

        MAKE_REAL_PATH (real_path, this, loc->path);
        size = sys_llistxattr (real_path, NULL, 0);
        op_errno = errno;
        if (size < 0) {
                if (BDB_TIMED_LOG (op_errno, gf_bdb_xattr_log)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "GETXATTR %"PRId64" (%s) - %s: %s",
                                loc->ino, loc->path, name, strerror (op_errno));
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "GETXATTR %"PRId64" (%s) - %s: %s",
                                loc->ino, loc->path, name, strerror (op_errno));
                }
                op_ret = -1;
                op_errno = ENOATTR;

                goto out;
        }

        if (size == 0)
                goto done;

        list = alloca (size + 1);
        if (list == NULL) {
                op_ret   = -1;
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "GETXATTR %"PRId64" (%s) - %s: %s",
                        loc->ino, loc->path, name, strerror (op_errno));
        }

        size = sys_llistxattr (real_path, list, size);
        op_ret   = size;
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "GETXATTR %"PRId64" (%s) - %s: %s",
                        loc->ino, loc->path, name, strerror (op_errno));
                goto out;
        }

        remaining_size = size;
        list_offset = 0;
        while (remaining_size > 0) {
                if(*(list+list_offset) == '\0')
                        break;

                strcpy (key, list + list_offset);

                op_ret = sys_lgetxattr (real_path, key, NULL, 0);
                if (op_ret == -1)
                        break;

                value = CALLOC (op_ret + 1, sizeof(char));
                GF_VALIDATE_OR_GOTO (this->name, value, out);

                op_ret = sys_lgetxattr (real_path, key, value,
                                        op_ret);
                if (op_ret == -1)
                        break;
                value [op_ret] = '\0';
                op_ret = dict_set_dynptr (dict, key,
                                          value, op_ret);
                if (op_ret < 0) {
                        FREE (value);
                        gf_log (this->name, GF_LOG_DEBUG,
                                "GETXATTR %"PRId64" (%s) - %s: "
                                "(skipping key %s)",
                                loc->ino, loc->path, name, key);
                        continue;
                }
                remaining_size -= strlen (key) + 1;
                list_offset += strlen (key) + 1;
        } /* while(remaining_size>0) */
done:
out:
        if(bctx) {
                /* NOTE: bctx_unref always returns success,
                 * see description of bctx_unref for more details */
                bctx_unref (bctx);
        }

        STACK_UNWIND (frame, op_ret, op_errno, dict);

        if (dict)
                dict_unref (dict);

        return 0;
}/* bdb_getxattr */


int32_t
bdb_removexattr (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *loc,
                 const char *name)
{
        int32_t op_ret    = -1;
        int32_t op_errno  = EINVAL;
        bctx_t *bctx      = NULL;
        char   *real_path = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, name, out);

        if (!S_ISDIR(loc->inode->st_mode)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "REMOVEXATTR %"PRId64" (%s) - %s: ENOATTR "
                        "(not a directory)",
                        loc->ino, loc->path, name);
                op_ret = -1;
                op_errno = ENOATTR;
                goto out;
        }

        if (GF_FILE_CONTENT_REQUEST(name)) {
                bctx = bctx_lookup (B_TABLE(this), loc->path);
                if (bctx == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "REMOVEXATTR %"PRId64" (%s) - %s: ENOATTR"
                                "(no database handle for directory)",
                                loc->ino, loc->path, name);
                        op_ret = -1;
                        op_errno = ENOATTR;
                        goto out;
                }

                op_ret = bdb_db_iremove (bctx, name);
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "REMOVEXATTR %"PRId64" (%s) - %s: ENOATTR"
                                "(no such attribute in database)",
                                loc->ino, loc->path, name);
                        op_errno = ENOATTR;
                }
                goto out;
        }

        MAKE_REAL_PATH(real_path, this, loc->path);
        op_ret = lremovexattr (real_path, name);
        op_errno = errno;
        if (op_ret == -1) {
                if (BDB_TIMED_LOG (op_errno, gf_bdb_xattr_log)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "REMOVEXATTR %"PRId64" (%s) - %s: %s",
                                loc->ino, loc->path, name, strerror (op_errno));
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "REMOVEXATTR %"PRId64" (%s) - %s: %s",
                                loc->ino, loc->path, name, strerror (op_errno));
                }
        } /* if(op_ret == -1) */
out:
        if (bctx) {
                /* NOTE: bctx_unref always returns success,
                 * see description of bctx_unref for more details */
                bctx_unref (bctx);
        }

        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}/* bdb_removexattr */


int32_t
bdb_fsyncdir (call_frame_t *frame,
              xlator_t *this,
              fd_t *fd,
              int datasync)
{
        int32_t op_ret = -1;
        int32_t op_errno = EINVAL;
        struct bdb_fd *bfd = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        BDB_FCTX_GET (fd, this, &bfd);
        if (bfd == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "FSYNCDIR %"PRId64": EBADFD"
                        "(failed to find internal context from fd)",
                        fd->inode->ino);
                op_errno = EBADFD;
                op_ret   = -1;
        }

out:
        STACK_UNWIND (frame, op_ret, op_errno);

        return 0;
}/* bdb_fsycndir */


int32_t
bdb_access (call_frame_t *frame,
            xlator_t *this,
            loc_t *loc,
            int32_t mask)
{
        int32_t op_ret = -1;
        int32_t op_errno = EINVAL;
        char *real_path = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        op_ret = access (real_path, mask);
        op_errno = errno;
        /* TODO: implement for db entries */
out:
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
        int32_t op_errno = EPERM;
        struct stat buf = {0,};

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        /* TODO: impelement */
out:
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
        int32_t op_ret = -1, op_errno = EINVAL;
        char *entry_path = NULL;
        int32_t real_path_len = 0;
        int32_t entry_path_len = 0;
        int32_t ret = 0;
        struct bdb_dir *bfd = NULL;
        dir_entry_t *trav = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, entries, out);

        BDB_FCTX_GET (fd, this, &bfd);
        if (bfd == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "SETDENTS %"PRId64": EBADFD",
                        fd->inode->ino);
                op_errno = EBADFD;
                op_ret   = -1;
                goto out;
        }

        real_path_len = strlen (bfd->path);
        entry_path_len = real_path_len + 256;
        entry_path = CALLOC (1, entry_path_len);
        GF_VALIDATE_OR_GOTO (this->name, entry_path, out);

        strcpy (entry_path, bfd->path);
        entry_path[real_path_len] = '/';

        trav = entries->next;
        while (trav) {
                char pathname[ZR_PATH_MAX] = {0,};
                strcpy (pathname, entry_path);
                strcat (pathname, trav->name);

                if (S_ISDIR(trav->buf.st_mode)) {
                        /* If the entry is directory, create it by calling
                         * 'mkdir'. If directory is not present, it will be
                         * created, if its present, no worries even if it fails.
                         */
                        ret = mkdir (pathname, trav->buf.st_mode);
                        if ((ret == -1) && (errno != EEXIST)) {
                                op_errno = errno;
                                op_ret   = ret;
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "SETDENTS %"PRId64" - %s: %s "
                                        "(mkdir failed)",
                                        fd->inode->ino, pathname,
                                        strerror (op_errno));
                                goto loop;
                        }

                        /* Change the mode
                         * NOTE: setdents tries its best to restore the state
                         *       of storage. if chmod and chown fail, they can
                         *       be ignored now */
                        ret = chmod (pathname, trav->buf.st_mode);
                        if (ret < 0) {
                                op_ret   = -1;
                                op_errno = errno;
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "SETDENTS %"PRId64" - %s: %s "
                                        "(chmod failed)",
                                        fd->inode->ino, pathname,
                                        strerror (op_errno));
                                goto loop;
                        }
                        /* change the ownership */
                        ret = chown (pathname, trav->buf.st_uid,
                                     trav->buf.st_gid);
                        if (ret != 0) {
                                op_ret   = -1;
                                op_errno = errno;
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "SETDENTS %"PRId64" - %s: %s "
                                        "(chown failed)",
                                        fd->inode->ino, pathname,
                                        strerror (op_errno));
                                goto loop;
                        }
                } else if ((flags == GF_SET_IF_NOT_PRESENT) ||
                           (flags != GF_SET_DIR_ONLY)) {
                        /* Create a 0 byte file here */
                        if (S_ISREG (trav->buf.st_mode)) {
                                op_ret = bdb_db_icreate (bfd->ctx,
                                                         trav->name);
                                if (op_ret < 0) {
                                        gf_log (this->name, GF_LOG_DEBUG,
                                                "SETDENTS %"PRId64" (%s) - %s: "
                                                "%s (database entry creation"
                                                " failed)",
                                                fd->inode->ino,
                                                bfd->ctx->directory, trav->name,
                                                strerror (op_errno));
                                }
                        } else if (S_ISLNK (trav->buf.st_mode)) {
                                /* TODO: impelement */;
                        } else {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "SETDENTS %"PRId64" (%s) - %s mode=%o: "
                                        "(unsupported file type)",
                                        fd->inode->ino,
                                        bfd->ctx->directory, trav->name,
                                        trav->buf.st_mode);
                        } /* if(S_ISREG())...else */
                } /* if(S_ISDIR())...else if */
        loop:
                /* consider the next entry */
                trav = trav->next;
        } /* while(trav) */

out:
        STACK_UNWIND (frame, op_ret, op_errno);

        FREE (entry_path);
        return 0;
}

int32_t
bdb_fstat (call_frame_t *frame,
           xlator_t *this,
           fd_t *fd)
{
        int32_t        op_ret   = -1;
        int32_t        op_errno = EINVAL;
        struct stat    stbuf    = {0,};
        struct bdb_fd *bfd      = NULL;
        bctx_t        *bctx     = NULL;
        char          *db_path  = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        BDB_FCTX_GET (fd, this, &bfd);
        if (bfd == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "FSTAT %"PRId64": EBADFD "
                        "(failed to find internal context in fd)",
                        fd->inode->ino);
                op_errno = EBADFD;
                op_ret   = -1;
                goto out;
        }

        bctx = bfd->ctx;

        MAKE_REAL_PATH_TO_STORAGE_DB (db_path, this, bctx->directory);
        op_ret = lstat (db_path, &stbuf);
        op_errno = errno;
        if (op_ret != 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "FSTAT %"PRId64": %s"
                        "(failed to stat database file %s)",
                        fd->inode->ino, strerror (op_errno), db_path);
                goto out;
        }

        stbuf.st_ino = fd->inode->ino;
        stbuf.st_size = bdb_db_fread (bfd, NULL, 0, 0);
        stbuf.st_blocks = BDB_COUNT_BLOCKS (stbuf.st_size, stbuf.st_blksize);

out:
        STACK_UNWIND (frame, op_ret, op_errno, &stbuf);
        return 0;
}

gf_dirent_t *
gf_dirent_for_namen (const char *name,
                     size_t len)
{
        char *tmp_name = NULL;

        tmp_name = alloca (len + 1);

        memcpy (tmp_name, name, len);

        tmp_name[len] = 0;

        return gf_dirent_for_name (tmp_name);
}

int32_t
bdb_readdir (call_frame_t *frame,
             xlator_t *this,
             fd_t *fd,
             size_t size,
             off_t off)
{
        struct bdb_dir *bfd        = NULL;
        int32_t         op_ret     = -1;
        int32_t         op_errno   = EINVAL;
        size_t          filled     = 0;
        gf_dirent_t    *this_entry = NULL;
        gf_dirent_t     entries;
        struct dirent  *entry      = NULL;
        off_t           in_case    = 0;
        int32_t         this_size  = 0;
        DBC            *cursorp    = NULL;
        int32_t count = 0;
        off_t   offset = 0;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        INIT_LIST_HEAD (&entries.list);

        BDB_FCTX_GET (fd, this, &bfd);
        if (bfd == NULL) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "READDIR %"PRId64" - %"GF_PRI_SIZET",%"PRId64": EBADFD "
                        "(failed to find internal context in fd)",
                        fd->inode->ino, size, off);
                op_errno = EBADFD;
                op_ret   = -1;
                goto out;
        }

        op_ret = bdb_cursor_open (bfd->ctx, &cursorp);
        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "READDIR %"PRId64" - %"GF_PRI_SIZET",%"PRId64": EBADFD "
                        "(failed to open cursor to database handle)",
                        fd->inode->ino, size, off);
                op_errno = EBADFD;
                goto out;
        }

        if (off) {
                DBT sec = {0,}, pri = {0,}, val = {0,};
                sec.data = &(off);
                sec.size = sizeof (off);
                sec.flags = DB_DBT_USERMEM;
                val.dlen = 0;
                val.doff = 0;
                val.flags = DB_DBT_PARTIAL;

                op_ret = bdb_cursor_get (cursorp, &sec, &pri, &val, DB_SET);
                if (op_ret == DB_NOTFOUND) {
                        offset = off;
                        goto dir_read;
                }
        }

        while (filled <= size) {
                DBT sec = {0,}, pri = {0,}, val = {0,};

                this_entry = NULL;

                sec.flags = DB_DBT_MALLOC;
                pri.flags = DB_DBT_MALLOC;
                val.dlen = 0;
                val.doff = 0;
                val.flags = DB_DBT_PARTIAL;
                op_ret = bdb_cursor_get (cursorp, &sec, &pri, &val, DB_NEXT);

                if (op_ret == DB_NOTFOUND) {
                        /* we reached end of the directory */
                        op_ret = 0;
                        op_errno = 0;
                        break;
                } else if (op_ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "READDIR %"PRId64" - %"GF_PRI_SIZET",%"PRId64":"
                                "(failed to read the next entry from database)",
                                fd->inode->ino, size, off);
                        op_errno = ENOENT;
                        break;
                } /* if (op_ret == DB_NOTFOUND)...else if...else */

                if (pri.data == NULL) {
                        /* NOTE: currently ignore when we get key.data == NULL.
                         * TODO: we should not get key.data = NULL */
                        gf_log (this->name, GF_LOG_DEBUG,
                                "READDIR %"PRId64" - %"GF_PRI_SIZET",%"PRId64":"
                                "(null key read for entry from database)",
                                fd->inode->ino, size, off);
                        continue;
                }/* if(key.data)...else */
                count++;
                this_size = bdb_dirent_size (&pri);
                if (this_size + filled > size)
                        break;
                /* TODO - consider endianness here */
                this_entry = gf_dirent_for_namen ((const char *)pri.data,
                                                  pri.size);

                this_entry->d_ino = bdb_inode_transform (fd->inode->ino,
                                                         pri.data,
                                                         pri.size);
                this_entry->d_off = *(uint32_t *)sec.data;
                this_entry->d_type = 0;
                this_entry->d_len = pri.size + 1;

                if (sec.data) {
                        FREE (sec.data);
                }

                if (pri.data)
                        FREE (pri.data);

                list_add_tail (&this_entry->list, &entries.list);

                filled += this_size;
        }/* while */
        bdb_cursor_close (bfd->ctx, cursorp);
        op_ret = filled;
        op_errno = 0;
        if (filled >= size) {
                goto out;
        }
dir_read:
        /* hungry kyaa? */
        if (!offset) {
                rewinddir (bfd->dir);
        } else {
                seekdir (bfd->dir, offset);
        }

        while (filled <= size) {
                this_entry = NULL;
                entry      = NULL;
                this_size  = 0;

                in_case = telldir (bfd->dir);
                entry = readdir (bfd->dir);
                if (!entry)
                        break;

                if (IS_BDB_PRIVATE_FILE(entry->d_name))
                        continue;

                this_size = dirent_size (entry);

                if (this_size + filled > size) {
                        seekdir (bfd->dir, in_case);
                        break;
                }

                count++;

                this_entry = gf_dirent_for_name (entry->d_name);
                this_entry->d_ino = entry->d_ino;

                this_entry->d_off = entry->d_off;

                this_entry->d_type = entry->d_type;
                this_entry->d_len = entry->d_reclen;


                list_add_tail (&this_entry->list, &entries.list);

                filled += this_size;
        }
        op_ret = filled;
        op_errno = 0;

out:
        gf_log (this->name, GF_LOG_DEBUG,
                "READDIR %"PRId64" - %"GF_PRI_SIZET" (%"PRId32")"
                "/%"GF_PRI_SIZET",%"PRId64":"
                "(failed to read the next entry from database)",
                fd->inode->ino, filled, count, size, off);

        STACK_UNWIND (frame, count, op_errno, &entries);

        gf_dirent_free (&entries);

        return 0;
}


int32_t
bdb_stats (call_frame_t *frame,
           xlator_t *this,
           int32_t flags)

{
        int32_t op_ret   = 0;
        int32_t op_errno = 0;

        struct xlator_stats xlstats = {0, }, *stats = NULL;
        struct statvfs buf = {0,};
        struct timeval tv;
        struct bdb_private *private = NULL;
        int64_t avg_read = 0;
        int64_t avg_write = 0;
        int64_t _time_ms = 0;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);

        private = (struct bdb_private *)(this->private);
        stats = &xlstats;

        op_ret = statvfs (private->export_path, &buf);
        if (op_ret != 0) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_DEBUG,
                        "STATS %s: %s",
                        private->export_path, strerror (op_errno));
                goto out;
        }

        stats->nr_files = private->stats.nr_files;

        /* client info is maintained at FSd */
        stats->nr_clients = private->stats.nr_clients;

        /* Number of Free block in the filesystem. */
        stats->free_disk       = buf.f_bfree * buf.f_bsize;
        stats->total_disk_size = buf.f_blocks * buf.f_bsize; /* */
        stats->disk_usage      = (buf.f_blocks - buf.f_bavail) * buf.f_bsize;

        /* Calculate read and write usage */
        gettimeofday (&tv, NULL);

        /* Read */
        _time_ms = (tv.tv_sec - private->init_time.tv_sec) * 1000 +
                ((tv.tv_usec - private->init_time.tv_usec) / 1000);

        avg_read  = (_time_ms) ? (private->read_value / _time_ms) : 0;/* KBps */
        avg_write = (_time_ms) ? (private->write_value / _time_ms) : 0;

        _time_ms = (tv.tv_sec - private->prev_fetch_time.tv_sec) * 1000 +
                ((tv.tv_usec - private->prev_fetch_time.tv_usec) / 1000);
        if (_time_ms
            && ((private->interval_read / _time_ms) > private->max_read)) {
                private->max_read = (private->interval_read / _time_ms);
        }
        if (_time_ms
            && ((private->interval_write / _time_ms) > private->max_write)) {
                private->max_write = private->interval_write / _time_ms;
        }

        stats->read_usage = avg_read / private->max_read;
        stats->write_usage = avg_write / private->max_write;

        gettimeofday (&(private->prev_fetch_time), NULL);
        private->interval_read = 0;
        private->interval_write = 0;

out:
        STACK_UNWIND (frame, op_ret, op_errno, stats);
        return 0;
}


int32_t
bdb_inodelk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc, int32_t cmd, struct flock *lock)
{
        gf_log (this->name, GF_LOG_ERROR,
                "glusterfs internal locking request. please load "
                "'features/locks' translator to enable glusterfs "
                "support");

        STACK_UNWIND (frame, -1, ENOSYS);
        return 0;
}


int32_t
bdb_finodelk (call_frame_t *frame, xlator_t *this,
              const char *volume, fd_t *fd, int32_t cmd, struct flock *lock)
{
        gf_log (this->name, GF_LOG_ERROR,
                "glusterfs internal locking request. please load "
                "'features/locks' translator to enable glusterfs "
                "support");

        STACK_UNWIND (frame, -1, ENOSYS);
        return 0;
}


int32_t
bdb_entrylk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc, const char *basename,
             entrylk_cmd cmd, entrylk_type type)
{
        gf_log (this->name, GF_LOG_ERROR,
                "glusterfs internal locking request. please load "
                "'features/locks' translator to enable glusterfs "
                "support");

        STACK_UNWIND (frame, -1, ENOSYS);
        return 0;
}


int32_t
bdb_fentrylk (call_frame_t *frame, xlator_t *this,
              const char *volume, fd_t *fd, const char *basename,
              entrylk_cmd cmd, entrylk_type type)
{
        gf_log (this->name, GF_LOG_ERROR,
                "glusterfs internal locking request. please load "
                "'features/locks' translator to enable glusterfs "
                "support");

        STACK_UNWIND (frame, -1, ENOSYS);
        return 0;
}

int32_t
bdb_checksum (call_frame_t *frame,
              xlator_t *this,
              loc_t *loc,
              int32_t flag)
{
        char          *real_path = NULL;
        DIR           *dir       = NULL;
        struct dirent *dirent    = NULL;
        uint8_t        file_checksum[NAME_MAX] = {0,};
        uint8_t        dir_checksum[NAME_MAX]  = {0,};
        int32_t        op_ret   = -1;
        int32_t        op_errno = EINVAL;
        int32_t        idx = 0, length = 0;
        bctx_t        *bctx    = NULL;
        DBC           *cursorp = NULL;
        char          *data    = NULL;
        uint8_t        no_break = 1;

        GF_VALIDATE_OR_GOTO ("bdb", frame, out);
        GF_VALIDATE_OR_GOTO ("bdb", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);

        MAKE_REAL_PATH (real_path, this, loc->path);

        {
                dir = opendir (real_path);
                op_errno = errno;
                GF_VALIDATE_OR_GOTO (this->name, dir, out);
                while ((dirent = readdir (dir))) {
                        if (!dirent)
                                break;

                        if (IS_BDB_PRIVATE_FILE(dirent->d_name))
                                continue;

                        length = strlen (dirent->d_name);
                        for (idx = 0; idx < length; idx++)
                                dir_checksum[idx] ^= dirent->d_name[idx];
                } /* while((dirent...)) */
                closedir (dir);
        }

        {
                bctx = bctx_lookup (B_TABLE(this), (char *)loc->path);
                if (bctx == NULL) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "CHECKSUM %"PRId64" (%s): ENOMEM"
                                "(failed to lookup database handle)",
                                loc->inode->ino, loc->path);
                        op_ret   = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

                op_ret = bdb_cursor_open (bctx, &cursorp);
                if (op_ret < 0) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "CHECKSUM %"PRId64" (%s): EBADFD"
                                "(failed to open cursor to database handle)",
                                loc->inode->ino, loc->path);
                        op_ret   = -1;
                        op_errno = EBADFD;
                        goto out;
                }


                do {
                        DBT key = {0,}, value = {0,}, sec = {0,};

                        key.flags = DB_DBT_MALLOC;
                        value.doff = 0;
                        value.dlen = 0;
                        op_ret = bdb_cursor_get (cursorp, &sec, &key,
                                                 &value, DB_NEXT);

                        if (op_ret == DB_NOTFOUND) {
                                op_ret = 0;
                                op_errno = 0;
                                no_break = 0;
                        } else if (op_ret == 0){
                                /* successfully read */
                                data = key.data;
                                length = key.size;
                                for (idx = 0; idx < length; idx++)
                                        file_checksum[idx] ^= data[idx];

                                FREE (key.data);
                        } else {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "CHECKSUM %"PRId64" (%s)",
                                        loc->inode->ino, loc->path);
                                op_ret = -1;
                                op_errno = ENOENT; /* TODO: watch errno */
                                no_break = 0;
                        }/* if(op_ret == DB_NOTFOUND)...else if...else */
                } while (no_break);
                bdb_cursor_close (bctx, cursorp);
        }
out:
        if (bctx) {
                /* NOTE: bctx_unref always returns success,
                 * see description of bctx_unref for more details */
                bctx_unref (bctx);
        }

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
                assert ((this->private != NULL) &&
                        (BDB_ENV(this) != NULL));
                default_notify (this, GF_EVENT_CHILD_UP, data);
        }
        break;
        default:
                /* */
                break;
        }
        return 0;
}



/**
 * init -
 */
int32_t
init (xlator_t *this)
{
        int32_t             ret = -1;
        struct stat         buf = {0,};
        struct bdb_private *_private = NULL;
        char               *directory = NULL;
        bctx_t             *bctx = NULL;

        GF_VALIDATE_OR_GOTO ("bdb", this, out);

        if (this->children) {
                gf_log (this->name, GF_LOG_ERROR,
                        "'storage/bdb' translator should be used as leaf node "
                        "in translator tree. please remove the subvolumes"
                        " specified and retry.");
                goto err;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_ERROR,
                        "'storage/bdb' translator needs at least one among "
                        "'protocol/server' or 'mount/fuse' translator as "
                        "parent. please add 'protocol/server' or 'mount/fuse' "
                        "as parent of 'storage/bdb' and retry. or you can also"
                        " try specifying mount-point on command-line.");
                goto err;
        }

        _private = CALLOC (1, sizeof (*_private));
        if (_private == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "could not allocate memory for 'storage/bdb' "
                        "configuration data-structure. cannot continue from "
                        "here");
                goto err;
        }


        ret = dict_get_str (this->options, "directory", &directory);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "'storage/bdb' needs at least "
                        "'option directory <path-to-export-directory>' as "
                        "minimal configuration option. please specify an "
                        "export directory using "
                        "'option directory <path-to-export-directory>' and "
                        "retry.");
                goto err;
        }

        umask (000); /* umask `masking' is done at the client side */

        /* Check whether the specified directory exists, if not create it. */
        ret = stat (directory, &buf);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "specified export path '%s' does not exist. "
                        "please create the export path '%s' and retry.",
                        directory, directory);
                goto err;
        } else if (!S_ISDIR (buf.st_mode)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "specified export path '%s' is not a directory. "
                        "please specify a valid and existing directory as "
                        "export directory and retry.",
                        directory);
                goto err;
        } else {
                ret = 0;
        }


        _private->export_path = strdup (directory);
        if (_private->export_path == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "could not allocate memory for 'storage/bdb' "
                        "configuration data-structure. cannot continue from "
                        "here");
                goto err;
        }

        _private->export_path_length = strlen (_private->export_path);

        {
                /* Stats related variables */
                gettimeofday (&_private->init_time, NULL);
                gettimeofday (&_private->prev_fetch_time, NULL);
                _private->max_read = 1;
                _private->max_write = 1;
        }

        this->private = (void *)_private;

        {
                ret = bdb_db_init (this, this->options);

                if (ret < 0){
                        gf_log (this->name, GF_LOG_ERROR,
                                "database environment initialisation failed. "
                                "manually run database recovery tool and "
                                "retry to run glusterfs");
                        goto err;
                } else {
                        bctx = bctx_lookup (_private->b_table, "/");
                        /* NOTE: we are not doing bctx_unref() for root bctx,
                         *      let it remain in active list forever */
                        if (bctx == NULL) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "could not allocate memory for "
                                        "'storage/bdb' configuration data-"
                                        "structure. cannot continue from "
                                        "here");
                                goto err;
                        } else {
                                ret = 0;
                                goto out;
                        }
                }
        }
err:
        if (_private) {
                if (_private->export_path)
                        FREE (_private->export_path);

                FREE (_private);
        }
out:
        return ret;
}

void
bctx_cleanup (struct list_head *head)
{
        bctx_t *trav    = NULL;
        bctx_t *tmp     = NULL;
        DB     *storage = NULL;
        DB     *secondary = NULL;

        list_for_each_entry_safe (trav, tmp, head, list) {
                LOCK (&trav->lock);
                {
                        storage = trav->primary;
                        trav->primary = NULL;

                        secondary = trav->secondary;
                        trav->secondary = NULL;

                        list_del_init (&trav->list);
                }
                UNLOCK (&trav->lock);

                if (storage) {
                        storage->close (storage, 0);
                        storage = NULL;
                }

                if (secondary) {
                        secondary->close (secondary, 0);
                        secondary = NULL;
                }
        }
        return;
}

void
fini (xlator_t *this)
{
        struct bdb_private *private = NULL;
        int32_t             ret     = 0;

        private = this->private;

        if (B_TABLE(this)) {
                /* close all the dbs from lru list */
                bctx_cleanup (&(B_TABLE(this)->b_lru));
                bctx_cleanup (&(B_TABLE(this)->active));

                if (BDB_ENV(this)) {
                        LOCK (&private->active_lock);
                        {
                                private->active = 0;
                        }
                        UNLOCK (&private->active_lock);

                        ret = pthread_join (private->checkpoint_thread, NULL);
                        if (ret != 0) {
                                gf_log (this->name, GF_LOG_CRITICAL,
                                        "could not complete checkpointing "
                                        "database environment. this might "
                                        "result in inconsistencies in few"
                                        " recent data and meta-data "
                                        "operations");
                        }

                        BDB_ENV(this)->close (BDB_ENV(this), 0);
                } else {
                        /* impossible to reach here */
                }

                FREE (B_TABLE(this));
        }
        FREE (private);
        return;
}

struct xlator_mops mops = {
        .stats    = bdb_stats,
};

struct xlator_fops fops = {
        .lookup      = bdb_lookup,
        .stat        = bdb_stat,
        .opendir     = bdb_opendir,
        .readdir     = bdb_readdir,
        .readlink    = bdb_readlink,
        .mknod       = bdb_mknod,
        .mkdir       = bdb_mkdir,
        .unlink      = bdb_unlink,
        .rmdir       = bdb_rmdir,
        .symlink     = bdb_symlink,
        .rename      = bdb_rename,
        .link        = bdb_link,
        .truncate    = bdb_truncate,
        .create      = bdb_create,
        .open        = bdb_open,
        .readv       = bdb_readv,
        .writev      = bdb_writev,
        .statfs      = bdb_statfs,
        .flush       = bdb_flush,
        .fsync       = bdb_fsync,
        .setxattr    = bdb_setxattr,
        .getxattr    = bdb_getxattr,
        .removexattr = bdb_removexattr,
        .fsyncdir    = bdb_fsyncdir,
        .access      = bdb_access,
        .ftruncate   = bdb_ftruncate,
        .fstat       = bdb_fstat,
        .lk          = bdb_lk,
        .inodelk     = bdb_inodelk,
        .finodelk    = bdb_finodelk,
        .entrylk     = bdb_entrylk,
        .fentrylk    = bdb_fentrylk,
        .setdents    = bdb_setdents,
        .getdents    = bdb_getdents,
        .checksum    = bdb_checksum,
        .setattr     = bdb_setattr,
        .fsetattr    = bdb_fsetattr,
};

struct xlator_cbks cbks = {
        .release    = bdb_release,
        .releasedir = bdb_releasedir
};


struct volume_options options[] = {
        { .key  = { "directory" },
          .type = GF_OPTION_TYPE_PATH,
          .description = "export directory"
        },
        { .key  = { "logdir" },
          .type = GF_OPTION_TYPE_PATH,
          .description = "directory to be used by libdb for writing"
                         "transaction logs. NOTE: in absence of 'logdir' "
                         "export directory itself will be used as 'logdir' also"
        },
        { .key  = { "errfile" },
          .type = GF_OPTION_TYPE_PATH,
          .description = "path to be used for libdb error logging. "
                         "NOTE: absence of 'errfile' will disable any "
                         "error logging by libdb."
        },
        { .key  = { "dir-mode" },
          .type = GF_OPTION_TYPE_ANY /* base 8 number */
        },
        { .key  = { "file-mode" },
          .type = GF_OPTION_TYPE_ANY,
          .description = "file mode for regular files. stat() on a regular file"
                         " returns the mode specified by this option. "
                         "NOTE: specify value in octal"
        },
        { .key  = { "page-size" },
          .type = GF_OPTION_TYPE_SIZET,
          .min  = 512,
          .max  = 16384,
          .description = "size of pages used to hold data by libdb. set it to "
                         "block size of exported filesystem for "
                         "optimal performance"
        },
        { .key  = { "open-db-lru-limit" },
          .type = GF_OPTION_TYPE_INT,
          .min  = 1,
          .max  = 2048,
          .description = "maximum number of per directory databases that can "
                         "be kept open. NOTE: for _advanced_ users only."
        },
        { .key  = { "lock-timeout" },
          .type = GF_OPTION_TYPE_TIME,
          .min  = 0,
          .max  = 4260000,
          .description = "define the maximum time a lock request can "
                         "be blocked by libdb. NOTE: only for _advanced_ users."
                         " do not specify this option when not sure."
        },
        { .key  = { "checkpoint-interval" },
          .type = GF_OPTION_TYPE_TIME,
          .min  = 1,
          .max  = 86400,
          .description = "define the time interval between two consecutive "
                         "libdb checpoints. setting to lower value will leave "
                         "bdb perform slowly, but guarantees that minimum data"
                         " will be lost in case of a crash. NOTE: this option "
                         "is valid only when "
                         "'option mode=\"persistent\"' is set."
        },
        { .key  = { "transaction-timeout" },
          .type = GF_OPTION_TYPE_TIME,
          .min  = 0,
          .max  = 4260000,
          .description = "maximum time for which a transaction can block "
                         "waiting for required resources."
        },
        { .key   = { "mode" },
          .type  = GF_OPTION_TYPE_BOOL,
          .value = { "cache", "persistent" },
          .description = "cache: data recovery is not guaranteed in case "
                         "of crash. persistent: data recovery is guaranteed, "
                         "since all operations are transaction protected."
        },
        { .key   = { "access-mode" },
          .type  = GF_OPTION_TYPE_STR,
          .value = {"btree", "hash" },
          .description = "chose the db access method. "
                         "NOTE: for _advanced_ users. leave the choice to "
                         "glusterfs when in doubt."
        },
        { .key = { NULL } }
};
