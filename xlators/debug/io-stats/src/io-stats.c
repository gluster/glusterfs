/*
  Copyright (c) 2006-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

/**
 * xlators/debug/io_stats :
 *    This translator maintains a counter for each fop (that is,
 *    a counter which stores the number of invocations for the certain
 *    fop), and makes these counters accessible as extended attributes.
 */

#include <errno.h>
#include "glusterfs.h"
#include "xlator.h"

#define BUMP_HIT(op)                                            \
do {                                                            \
        LOCK (&((io_stats_private_t *)this->private)->lock);    \
        {                                                       \
                ((io_stats_private_t *)this->private)           \
                  ->fop_records[GF_FOP_##op].hits++;            \
        }                                                       \
        UNLOCK (&((io_stats_private_t *)this->private)->lock);  \
 } while (0)

struct io_stats_io_count {
        size_t  size;
        int64_t hits;        
};
typedef enum {
        GF_IO_STAT_BLK_SIZE_1K,
        GF_IO_STAT_BLK_SIZE_2K,
        GF_IO_STAT_BLK_SIZE_4K,
        GF_IO_STAT_BLK_SIZE_8K,
        GF_IO_STAT_BLK_SIZE_16K,
        GF_IO_STAT_BLK_SIZE_32K,
        GF_IO_STAT_BLK_SIZE_64K,
        GF_IO_STAT_BLK_SIZE_128K,
        GF_IO_STAT_BLK_SIZE_MAX,
} gf_io_stat_blk_t;

struct io_stats_private {
        gf_lock_t lock;
        struct {
                char *name;
                int enabled;
                uint32_t hits;
        } fop_records[GF_FOP_MAXVALUE];
        struct io_stats_io_count read[GF_IO_STAT_BLK_SIZE_MAX + 1];
        struct io_stats_io_count write[GF_IO_STAT_BLK_SIZE_MAX + 1];        
};
typedef struct io_stats_private io_stats_private_t;

int32_t
io_stats_create_cbk (call_frame_t *frame,
                     void *cookie,
                     xlator_t *this,
                     int32_t op_ret,
                     int32_t op_errno,
                     fd_t *fd,
                     inode_t *inode,
                     struct stat *buf,
                     struct stat *preparent,
                     struct stat *postparent)
{
        STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf,
                      preparent, postparent);
        return 0;
}

int32_t
io_stats_open_cbk (call_frame_t *frame,
                   void *cookie,
                   xlator_t *this,
                   int32_t op_ret,
                   int32_t op_errno,
                   fd_t *fd)
{
        STACK_UNWIND (frame, op_ret, op_errno, fd);
        return 0;
}

int32_t
io_stats_stat_cbk (call_frame_t *frame,
                   void *cookie,
                   xlator_t *this,
                   int32_t op_ret,
                   int32_t op_errno,
                   struct stat *buf)
{
        STACK_UNWIND (frame, op_ret, op_errno, buf);
        return 0;
}

int32_t
io_stats_readv_cbk (call_frame_t *frame,
                    void *cookie,
                    xlator_t *this,
                    int32_t op_ret,
                    int32_t op_errno,
                    struct iovec *vector,
                    int32_t count,
                    struct stat *buf,
                    struct iobref *iobref)
{
        int i = 0;
        io_stats_private_t *priv = NULL;

        priv = this->private;

        if (op_ret > 0) {
                for (i=0; i < GF_IO_STAT_BLK_SIZE_MAX; i++) {
                        if (priv->read[i].size > iov_length (vector, count)) {
                                break;
                        }
                }
                priv->read[i].hits++;
        }

        STACK_UNWIND (frame, op_ret, op_errno, vector, count, buf, iobref);
        return 0;
}

int32_t
io_stats_writev_cbk (call_frame_t *frame,
                     void *cookie,
                     xlator_t *this,
                     int32_t op_ret,
                     int32_t op_errno,
                     struct stat *prebuf,
                     struct stat *postbuf)
{
        STACK_UNWIND (frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}

int32_t
io_stats_getdents_cbk (call_frame_t *frame,
                       void *cookie,
                       xlator_t *this,
                       int32_t op_ret,
                       int32_t op_errno,
                       dir_entry_t *entries,
                       int32_t count)
{
        STACK_UNWIND (frame, op_ret, op_errno, entries, count);
        return 0;
}

int32_t
io_stats_readdir_cbk (call_frame_t *frame,
                      void *cookie,
                      xlator_t *this,
                      int32_t op_ret,
                      int32_t op_errno,
                      gf_dirent_t *buf)
{
        STACK_UNWIND (frame, op_ret, op_errno, buf);
        return 0;
}

int32_t
io_stats_fsync_cbk (call_frame_t *frame,
                    void *cookie,
                    xlator_t *this,
                    int32_t op_ret,
                    int32_t op_errno,
                    struct stat *prebuf,
                    struct stat *postbuf)
{
        STACK_UNWIND (frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}

int32_t
io_stats_setattr_cbk (call_frame_t *frame,
                      void *cookie,
                      xlator_t *this,
                      int32_t op_ret,
                      int32_t op_errno,
                      struct stat *preop,
                      struct stat *postop)
{
        STACK_UNWIND (frame, op_ret, op_errno, preop, postop);
        return 0;
}

int32_t
io_stats_unlink_cbk (call_frame_t *frame,
                     void *cookie,
                     xlator_t *this,
                     int32_t op_ret,
                     int32_t op_errno,
                     struct stat *preparent,
                     struct stat *postparent)
{
        STACK_UNWIND (frame, op_ret, op_errno, preparent, postparent);
        return 0;
}

int32_t
io_stats_rename_cbk (call_frame_t *frame,
                     void *cookie,
                     xlator_t *this,
                     int32_t op_ret,
                     int32_t op_errno,
                     struct stat *buf,
                     struct stat *preoldparent,
                     struct stat *postoldparent,
                     struct stat *prenewparent,
                     struct stat *postnewparent)
{
        STACK_UNWIND (frame, op_ret, op_errno, buf,
                      preoldparent, postoldparent,
                      prenewparent, postnewparent);
        return 0;
}

int32_t
io_stats_readlink_cbk (call_frame_t *frame,
                       void *cookie,
                       xlator_t *this,
                       int32_t op_ret,
                       int32_t op_errno,
                       const char *buf,
                       struct stat *sbuf)
{
        STACK_UNWIND (frame, op_ret, op_errno, buf, sbuf);
        return 0;
}

int32_t
io_stats_lookup_cbk (call_frame_t *frame,
                     void *cookie,
                     xlator_t *this,
                     int32_t op_ret,
                     int32_t op_errno,
                     inode_t *inode,
                     struct stat *buf,
                     dict_t *xattr,
                     struct stat *postparent)
{
        STACK_UNWIND (frame, op_ret, op_errno, inode, buf, xattr,
                      postparent);
        return 0;
}

int32_t
io_stats_symlink_cbk (call_frame_t *frame,
                      void *cookie,
                      xlator_t *this,
                      int32_t op_ret,
                      int32_t op_errno,
                      inode_t *inode,
                      struct stat *buf,
                      struct stat *preparent,
                      struct stat *postparent)
{
        STACK_UNWIND (frame, op_ret, op_errno, inode, buf,
                      preparent, postparent);
        return 0;
}

int32_t
io_stats_mknod_cbk (call_frame_t *frame,
                    void *cookie,
                    xlator_t *this,
                    int32_t op_ret,
                    int32_t op_errno,
                    inode_t *inode,
                    struct stat *buf,
                    struct stat *preparent,
                    struct stat *postparent)
{
        STACK_UNWIND (frame, op_ret, op_errno, inode, buf,
                      preparent, postparent);
        return 0;
}


int32_t
io_stats_mkdir_cbk (call_frame_t *frame,
                    void *cookie,
                    xlator_t *this,
                    int32_t op_ret,
                    int32_t op_errno,
                    inode_t *inode,
                    struct stat *buf,
                    struct stat *preparent,
                    struct stat *postparent)
{
        STACK_UNWIND (frame, op_ret, op_errno, inode, buf,
                      preparent, postparent);
        return 0;
}

int32_t
io_stats_link_cbk (call_frame_t *frame,
                   void *cookie,
                   xlator_t *this,
                   int32_t op_ret,
                   int32_t op_errno,
                   inode_t *inode,
                   struct stat *buf,
                   struct stat *preparent,
                   struct stat *postparent)
{
        STACK_UNWIND (frame, op_ret, op_errno, inode, buf,
                      preparent, postparent);
        return 0;
}

int32_t
io_stats_flush_cbk (call_frame_t *frame,
                    void *cookie,
                    xlator_t *this,
                    int32_t op_ret,
                    int32_t op_errno)
{
        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}


int32_t
io_stats_opendir_cbk (call_frame_t *frame,
                      void *cookie,
                      xlator_t *this,
                      int32_t op_ret,
                      int32_t op_errno,
                      fd_t *fd)
{
        STACK_UNWIND (frame, op_ret, op_errno, fd);
        return 0;
}

int32_t
io_stats_rmdir_cbk (call_frame_t *frame,
                    void *cookie,
                    xlator_t *this,
                    int32_t op_ret,
                    int32_t op_errno,
                    struct stat *preparent,
                    struct stat *postparent)
{
        STACK_UNWIND (frame, op_ret, op_errno, preparent, postparent);
        return 0;
}

int32_t
io_stats_truncate_cbk (call_frame_t *frame,
                       void *cookie,
                       xlator_t *this,
                       int32_t op_ret,
                       int32_t op_errno,
                       struct stat *prebuf,
                       struct stat *postbuf)
{
        STACK_UNWIND (frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}

int32_t
io_stats_utimens_cbk (call_frame_t *frame,
                      void *cookie,
                      xlator_t *this,
                      int32_t op_ret,
                      int32_t op_errno,
                      struct stat *buf)
{
        STACK_UNWIND (frame, op_ret, op_errno, buf);
        return 0;
}

int32_t
io_stats_statfs_cbk (call_frame_t *frame,
                     void *cookie,
                     xlator_t *this,
                     int32_t op_ret,
                     int32_t op_errno,
                     struct statvfs *buf)
{
        STACK_UNWIND (frame, op_ret, op_errno, buf);
        return 0;
}

int32_t
io_stats_setxattr_cbk (call_frame_t *frame,
                       void *cookie,
                       xlator_t *this,
                       int32_t op_ret,
                       int32_t op_errno)
{
        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}

int32_t
io_stats_getxattr_cbk (call_frame_t *frame,
                       void *cookie,
                       xlator_t *this,
                       int32_t op_ret,
                       int32_t op_errno,
                       dict_t *dict)
{
        io_stats_private_t *priv = this->private;
        int i = 0;
        char keycont[] = "trusted.glusterfs.hits." /* 23 chars */
                         "0123456789"
                         "0123456789"
                         "0123456789"
                         "0123456789";
        int ret = -1;

        memset (keycont + 23, '\0', 40);

        for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                if (!(priv->fop_records[i].enabled &&
                      priv->fop_records[i].name))
                        continue;

                strncpy (keycont + 23, priv->fop_records[i].name, 40);
                LOCK(&priv->lock);
                {
                        ret = dict_set_uint32 (dict, keycont,
                                               priv->fop_records[i].hits);
                }
                UNLOCK(&priv->lock);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "dict set failed for xattr %s",
                                keycont);
                        break;
                }
        }

        STACK_UNWIND (frame, op_ret, op_errno, dict);
        return 0;
}

int32_t
io_stats_removexattr_cbk (call_frame_t *frame,
                          void *cookie,
                          xlator_t *this,
                          int32_t op_ret,
                          int32_t op_errno)
{
        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}


int32_t
io_stats_fsyncdir_cbk (call_frame_t *frame,
                       void *cookie,
                       xlator_t *this,
                       int32_t op_ret,
                       int32_t op_errno)
{
        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}

int32_t
io_stats_access_cbk (call_frame_t *frame,
                     void *cookie,
                     xlator_t *this,
                     int32_t op_ret,
                     int32_t op_errno)
{
        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}

int32_t
io_stats_ftruncate_cbk (call_frame_t *frame,
                        void *cookie,
                        xlator_t *this,
                        int32_t op_ret,
                        int32_t op_errno,
                        struct stat *prebuf,
                        struct stat *postbuf)
{
        STACK_UNWIND (frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}

int32_t
io_stats_fstat_cbk (call_frame_t *frame,
                    void *cookie,
                    xlator_t *this,
                    int32_t op_ret,
                    int32_t op_errno,
                    struct stat *buf)
{
        STACK_UNWIND (frame, op_ret, op_errno, buf);
        return 0;
}

int32_t
io_stats_lk_cbk (call_frame_t *frame,
                 void *cookie,
                 xlator_t *this,
                 int32_t op_ret,
                 int32_t op_errno,
                 struct flock *lock)
{
        STACK_UNWIND (frame, op_ret, op_errno, lock);
        return 0;
}


int32_t
io_stats_setdents_cbk (call_frame_t *frame,
                       void *cookie,
                       xlator_t *this,
                       int32_t op_ret,
                       int32_t op_errno)
{
        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}

int32_t
io_stats_entrylk_cbk (call_frame_t *frame,
                      void *cookie,
                      xlator_t *this,
                      int32_t op_ret,
                      int32_t op_errno)
{
        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}


int32_t
io_stats_xattrop_cbk (call_frame_t *frame,
                      void *cookie,
                      xlator_t *this,
                      int32_t op_ret,
                      int32_t op_errno,
                      dict_t *dict)
{
        STACK_UNWIND (frame, op_ret, op_errno, dict);
        return 0;
}

int32_t
io_stats_fxattrop_cbk (call_frame_t *frame,
                       void *cookie,
                       xlator_t *this,
                       int32_t op_ret,
                       int32_t op_errno,
                       dict_t *dict)
{
        STACK_UNWIND (frame, op_ret, op_errno, dict);
        return 0;
}

int32_t
io_stats_inodelk_cbk (call_frame_t *frame,
                      void *cookie,
                      xlator_t *this,
                      int32_t op_ret,
                      int32_t op_errno)
{
        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}


int32_t
io_stats_entrylk (call_frame_t *frame, xlator_t *this,
                  const char *volume, loc_t *loc, const char *basename,
                  entrylk_cmd cmd, entrylk_type type)
{
        BUMP_HIT(ENTRYLK);

        STACK_WIND (frame,
                    io_stats_entrylk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->entrylk,
                    volume, loc, basename, cmd, type);
        return 0;
}

int32_t
io_stats_inodelk (call_frame_t *frame,
                  xlator_t *this,
                  const char *volume, loc_t *loc, int32_t cmd, struct flock *flock)
{

        BUMP_HIT(INODELK);

        STACK_WIND (frame,
                    io_stats_inodelk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->inodelk,
                    volume, loc, cmd, flock);
        return 0;
}


int32_t
io_stats_finodelk_cbk (call_frame_t *frame,
                       void *cookie,
                       xlator_t *this,
                       int32_t op_ret,
                       int32_t op_errno)
{

        STACK_UNWIND (frame, op_ret, op_errno);
        return 0;
}

int32_t
io_stats_finodelk (call_frame_t *frame,
                   xlator_t *this,
                   const char *volume, fd_t *fd, int32_t cmd, struct flock *flock)
{
        BUMP_HIT(FINODELK);

        STACK_WIND (frame,
                    io_stats_finodelk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->finodelk,
                    volume, fd, cmd, flock);
        return 0;
}


int32_t
io_stats_xattrop (call_frame_t *frame,
                  xlator_t *this,
                  loc_t *loc,
                  gf_xattrop_flags_t flags,
                  dict_t *dict)
{
        BUMP_HIT(XATTROP);

        STACK_WIND (frame, io_stats_xattrop_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop,
                    loc, flags, dict);

        return 0;
}

int32_t
io_stats_fxattrop (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd,
                   gf_xattrop_flags_t flags,
                   dict_t *dict)
{
        BUMP_HIT(FXATTROP);

        STACK_WIND (frame, io_stats_fxattrop_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fxattrop,
                    fd, flags, dict);

        return 0;
}

int32_t
io_stats_lookup (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *loc,
                 dict_t *xattr_req)
{
        BUMP_HIT(LOOKUP);

        STACK_WIND (frame, io_stats_lookup_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup,
                    loc, xattr_req);

        return 0;
}

int32_t
io_stats_stat (call_frame_t *frame,
               xlator_t *this,
               loc_t *loc)
{
        BUMP_HIT(STAT);

        STACK_WIND (frame,
                    io_stats_stat_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat,
                    loc);

        return 0;
}

int32_t
io_stats_readlink (call_frame_t *frame,
                   xlator_t *this,
                   loc_t *loc,
                   size_t size)
{
        BUMP_HIT(READLINK);

        STACK_WIND (frame,
                    io_stats_readlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readlink,
                    loc,
                    size);

        return 0;
}

int32_t
io_stats_mknod (call_frame_t *frame,
                xlator_t *this,
                loc_t *loc,
                mode_t mode,
                dev_t dev)
{
        BUMP_HIT(MKNOD);

        STACK_WIND (frame,
                    io_stats_mknod_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mknod,
                    loc,
                    mode,
                    dev);

        return 0;
}

int32_t
io_stats_mkdir (call_frame_t *frame,
                xlator_t *this,
                loc_t *loc,
                mode_t mode)
{
        BUMP_HIT(MKDIR);

        STACK_WIND (frame,
                    io_stats_mkdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mkdir,
                    loc,
                    mode);
        return 0;
}

int32_t
io_stats_unlink (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *loc)
{
        BUMP_HIT(UNLINK);

        STACK_WIND (frame,
                    io_stats_unlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink,
                    loc);
        return 0;
}

int32_t
io_stats_rmdir (call_frame_t *frame,
                xlator_t *this,
                loc_t *loc)
{
        BUMP_HIT(RMDIR);

        STACK_WIND (frame,
                    io_stats_rmdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rmdir,
                    loc);

        return 0;
}

int32_t
io_stats_symlink (call_frame_t *frame,
                  xlator_t *this,
                  const char *linkpath,
                  loc_t *loc)
{
        BUMP_HIT(SYMLINK);

        STACK_WIND (frame,
                    io_stats_symlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->symlink,
                    linkpath,
                    loc);

        return 0;
}

int32_t
io_stats_rename (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *oldloc,
                 loc_t *newloc)
{
        BUMP_HIT(RENAME);

        STACK_WIND (frame,
                    io_stats_rename_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename,
                    oldloc,
                    newloc);

        return 0;
}

int32_t
io_stats_link (call_frame_t *frame,
               xlator_t *this,
               loc_t *oldloc,
               loc_t *newloc)
{
        BUMP_HIT(LINK);

        STACK_WIND (frame,
                    io_stats_link_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->link,
                    oldloc,
                    newloc);
        return 0;
}

int32_t
io_stats_setattr (call_frame_t *frame,
                  xlator_t *this,
                  loc_t *loc,
                  struct stat *stbuf,
                  int32_t valid)
{
        BUMP_HIT(SETATTR);

        STACK_WIND (frame,
                    io_stats_setattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setattr,
                    loc,
                    stbuf, valid);

        return 0;
}

int32_t
io_stats_truncate (call_frame_t *frame,
                   xlator_t *this,
                   loc_t *loc,
                   off_t offset)
{
        BUMP_HIT(TRUNCATE);

        STACK_WIND (frame,
                    io_stats_truncate_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate,
                    loc,
                    offset);

        return 0;
}

int32_t
io_stats_open (call_frame_t *frame,
               xlator_t *this,
               loc_t *loc,
               int32_t flags,
               fd_t *fd, int32_t wbflags)
{
        BUMP_HIT(OPEN);

        STACK_WIND (frame,
                    io_stats_open_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open,
                    loc,
                    flags,
                    fd, wbflags);
        return 0;
}

int32_t
io_stats_create (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *loc,
                 int32_t flags,
                 mode_t mode,
                 fd_t *fd)
{
        BUMP_HIT(CREATE);

        STACK_WIND (frame,
                    io_stats_create_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create,
                    loc,
                    flags,
                    mode,
                    fd);
        return 0;
}

int32_t
io_stats_readv (call_frame_t *frame,
                xlator_t *this,
                fd_t *fd,
                size_t size,
                off_t offset)
{
        BUMP_HIT(READ);

        STACK_WIND (frame,
                    io_stats_readv_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readv,
                    fd,
                    size,
                    offset);
        return 0;
}

int32_t
io_stats_writev (call_frame_t *frame,
                 xlator_t *this,
                 fd_t *fd,
                 struct iovec *vector,
                 int32_t count,
                 off_t offset,
                 struct iobref *iobref)
{
        int i = 0;
        io_stats_private_t *priv = NULL;

        priv = this->private;

        BUMP_HIT(WRITE);

        for (i=0; i < GF_IO_STAT_BLK_SIZE_MAX; i++) {
                if (priv->write[i].size > iov_length (vector, count)) {
                        break;
                }
        }
        priv->write[i].hits++;

        STACK_WIND (frame,
                    io_stats_writev_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev,
                    fd,
                    vector,
                    count,
                    offset,
                    iobref);
        return 0;
}

int32_t
io_stats_statfs (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *loc)
{
        BUMP_HIT(STATFS);

        STACK_WIND (frame,
                    io_stats_statfs_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->statfs,
                    loc);
        return 0;
}

int32_t
io_stats_flush (call_frame_t *frame,
                xlator_t *this,
                fd_t *fd)
{
        BUMP_HIT(FLUSH);

        STACK_WIND (frame,
                    io_stats_flush_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->flush,
                    fd);
        return 0;
}


int32_t
io_stats_fsync (call_frame_t *frame,
                xlator_t *this,
                fd_t *fd,
                int32_t flags)
{
        BUMP_HIT(FSYNC);

        STACK_WIND (frame,
                    io_stats_fsync_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsync,
                    fd,
                    flags);
        return 0;
}

int32_t
io_stats_setxattr (call_frame_t *frame,
                   xlator_t *this,
                   loc_t *loc,
                   dict_t *dict,
                   int32_t flags)
{
        BUMP_HIT(SETXATTR);

        STACK_WIND (frame,
                    io_stats_setxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    loc,
                    dict,
                    flags);
        return 0;
}

int32_t
io_stats_getxattr (call_frame_t *frame,
                   xlator_t *this,
                   loc_t *loc,
                   const char *name)
{
        BUMP_HIT(GETXATTR);

        STACK_WIND (frame,
                    io_stats_getxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr,
                    loc,
                    name);
        return 0;
}

int32_t
io_stats_removexattr (call_frame_t *frame,
                      xlator_t *this,
                      loc_t *loc,
                      const char *name)
{
        BUMP_HIT(REMOVEXATTR);

        STACK_WIND (frame,
                    io_stats_removexattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr,
                    loc,
                    name);

        return 0;
}

int32_t
io_stats_opendir (call_frame_t *frame,
                  xlator_t *this,
                  loc_t *loc,
                  fd_t *fd)
{
        BUMP_HIT(OPENDIR);

        STACK_WIND (frame,
                    io_stats_opendir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->opendir,
                    loc,
                    fd);
        return 0;
}

int32_t
io_stats_getdents (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd,
                   size_t size,
                   off_t offset,
                   int32_t flag)
{
        BUMP_HIT(GETDENTS);

        STACK_WIND (frame,
                    io_stats_getdents_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getdents,
                    fd,
                    size,
                    offset,
                    flag);
        return 0;
}


int32_t
io_stats_readdir (call_frame_t *frame,
                  xlator_t *this,
                  fd_t *fd,
                  size_t size,
                  off_t offset)
{
        BUMP_HIT(READDIR);

        STACK_WIND (frame,
                    io_stats_readdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdir,
                    fd,
                    size,
                    offset);

        return 0;
}


int32_t
io_stats_fsyncdir (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd,
                   int32_t datasync)
{
        BUMP_HIT(FSYNCDIR);

        STACK_WIND (frame,
                    io_stats_fsyncdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsyncdir,
                    fd,
                    datasync);
        return 0;
}

int32_t
io_stats_access (call_frame_t *frame,
                 xlator_t *this,
                 loc_t *loc,
                 int32_t mask)
{
        BUMP_HIT(ACCESS);

        STACK_WIND (frame,
                    io_stats_access_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->access,
                    loc,
                    mask);
        return 0;
}

int32_t
io_stats_ftruncate (call_frame_t *frame,
                    xlator_t *this,
                    fd_t *fd,
                    off_t offset)
{
        BUMP_HIT(FTRUNCATE);

        STACK_WIND (frame,
                    io_stats_ftruncate_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate,
                    fd,
                    offset);

        return 0;
}

int32_t
io_stats_fsetattr (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd,
                   struct stat *stbuf,
                   int32_t valid)
{
        BUMP_HIT(FSETATTR);

        STACK_WIND (frame,
                    io_stats_setattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetattr,
                    fd,
                    stbuf, valid);
        return 0;
}

int32_t
io_stats_fstat (call_frame_t *frame,
                xlator_t *this,
                fd_t *fd)
{
        BUMP_HIT(FSTAT);

        STACK_WIND (frame,
                    io_stats_fstat_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat,
                    fd);
        return 0;
}

int32_t
io_stats_lk (call_frame_t *frame,
             xlator_t *this,
             fd_t *fd,
             int32_t cmd,
             struct flock *lock)
{
        BUMP_HIT(LK);

        STACK_WIND (frame,
                    io_stats_lk_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lk,
                    fd,
                    cmd,
                    lock);
        return 0;
}

int32_t
io_stats_setdents (call_frame_t *frame,
                   xlator_t *this,
                   fd_t *fd,
                   int32_t flags,
                   dir_entry_t *entries,
                   int32_t count)
{
        BUMP_HIT(SETDENTS);

        STACK_WIND (frame,
                    io_stats_setdents_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setdents,
                    fd,
                    flags,
                    entries,
                    count);
        return 0;
}


int32_t
io_stats_checksum_cbk (call_frame_t *frame,
                       void *cookie,
                       xlator_t *this,
                       int32_t op_ret,
                       int32_t op_errno,
                       uint8_t *fchecksum,
                       uint8_t *dchecksum)
{
        STACK_UNWIND (frame, op_ret, op_errno, fchecksum, dchecksum);

        return 0;
}

int32_t
io_stats_checksum (call_frame_t *frame,
                   xlator_t *this,
                   loc_t *loc,
                   int32_t flag)
{
        STACK_WIND (frame,
                    io_stats_checksum_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->checksum,
                    loc,
                    flag);

        return 0;
}


int32_t
io_stats_stats_cbk (call_frame_t *frame,
                    void *cookie,
                    xlator_t *this,
                    int32_t op_ret,
                    int32_t op_errno,
                    struct xlator_stats *stats)
{
        STACK_UNWIND (frame, op_ret, op_errno, stats);
        return 0;
}

int32_t
io_stats_stats (call_frame_t *frame,
                xlator_t *this,
                int32_t flags)
{
        STACK_WIND (frame,
                    io_stats_stats_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->mops->stats,
                    flags);

        return 0;
}

void
enable_all_calls (io_stats_private_t *priv, int enabled)
{
        int i;
        for (i = 0; i < GF_FOP_MAXVALUE; i++)
                priv->fop_records[i].enabled = enabled;
}

void
enable_call (io_stats_private_t *priv, const char *name, int enabled)
{
        int i;
        for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                if (!priv->fop_records[i].name)
                        continue;
                if (!strcasecmp(priv->fop_records[i].name, name))
                        priv->fop_records[i].enabled = enabled;
        }
}


/*
   include = 1 for "include-ops"
           = 0 for "exclude-ops"
*/
void
process_call_list (io_stats_private_t *priv, const char *list, int include)
{
        enable_all_calls (priv, include ? 0 : 1);

        char *call = strsep ((char **)&list, ",");
        while (call) {
                enable_call (priv, call, include);
                call = strsep ((char **)&list, ",");
        }
}


int32_t
init (xlator_t *this)
{
        dict_t *options = this->options;
        char *includes = NULL, *excludes = NULL;
        io_stats_private_t *priv = NULL;
        size_t size = 0;
        int i = 0;

        if (!this)
                return -1;

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "io_stats translator requires one subvolume");
                return -1;
        }
        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile ");
        }

        priv = CALLOC (1, sizeof(*priv));
        ERR_ABORT (priv);

        includes = data_to_str (dict_get (options, "include-ops"));
        excludes = data_to_str (dict_get (options, "exclude-ops"));

        for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                priv->fop_records[i].name = gf_fop_list[i];
                priv->fop_records[i].enabled = 1;
        }

        if (includes && excludes) {
                gf_log (this->name, GF_LOG_ERROR,
                        "must specify only one of 'include-ops' and "
                        "'exclude-ops'");
                return -1;
        }
        if (includes)
                process_call_list (priv, includes, 1);
        if (excludes)
                process_call_list (priv, excludes, 0);

        LOCK_INIT (&priv->lock);

        /* Set this translator's inode table pointer to child node's pointer. */
        size = GF_UNIT_KB;
        for (i=0; i < GF_IO_STAT_BLK_SIZE_MAX; i++) {
                priv->read[i].size = size;
                priv->write[i].size = size;
                size *= 2;
        }

        this->itable = FIRST_CHILD (this)->itable;
        this->private = priv;

        return 0;
}

void
fini (xlator_t *this)
{
        io_stats_private_t *priv = NULL;

        if (!this)
                return;

        priv = this->private;
        if (priv) {
                LOCK_DESTROY (&priv->lock);
                FREE (priv);
        }

        gf_log (this->name, GF_LOG_NORMAL,
                "io-stats translator unloaded");
        return;
}

struct xlator_fops fops = {
        .stat        = io_stats_stat,
        .readlink    = io_stats_readlink,
        .mknod       = io_stats_mknod,
        .mkdir       = io_stats_mkdir,
        .unlink      = io_stats_unlink,
        .rmdir       = io_stats_rmdir,
        .symlink     = io_stats_symlink,
        .rename      = io_stats_rename,
        .link        = io_stats_link,
        .truncate    = io_stats_truncate,
        .open        = io_stats_open,
        .readv       = io_stats_readv,
        .writev      = io_stats_writev,
        .statfs      = io_stats_statfs,
        .flush       = io_stats_flush,
        .fsync       = io_stats_fsync,
        .setxattr    = io_stats_setxattr,
        .getxattr    = io_stats_getxattr,
        .removexattr = io_stats_removexattr,
        .opendir     = io_stats_opendir,
        .readdir     = io_stats_readdir,
        .fsyncdir    = io_stats_fsyncdir,
        .access      = io_stats_access,
        .ftruncate   = io_stats_ftruncate,
        .fstat       = io_stats_fstat,
        .create      = io_stats_create,
        .lk          = io_stats_lk,
        .inodelk     = io_stats_inodelk,
        .finodelk    = io_stats_finodelk,
        .entrylk     = io_stats_entrylk,
        .lookup      = io_stats_lookup,
        .setdents    = io_stats_setdents,
        .getdents    = io_stats_getdents,
        .checksum    = io_stats_checksum,
        .xattrop     = io_stats_xattrop,
        .fxattrop    = io_stats_fxattrop,
        .setattr     = io_stats_setattr,
        .fsetattr    = io_stats_fsetattr,
};

struct xlator_mops mops = {
        .stats    = io_stats_stats,
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key  = {"include-ops", "include"},
          .type = GF_OPTION_TYPE_STR,
          /*.value = { ""} */
        },
        { .key  = {"exclude-ops", "exclude"},
          .type = GF_OPTION_TYPE_STR
          /*.value = { ""} */
        },
        { .key  = {NULL} },
};
