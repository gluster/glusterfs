/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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
 *    This translator maintains statistics of all filesystem activity
 *    happening through it. The kind of statistics include:
 *
 *  a) total read data - since process start, last interval and per fd
 *  b) total write data - since process start, last interval and per fd
 *  c) counts of read IO block size - since process start, last interval and per fd
 *  d) counts of write IO block size - since process start, last interval and per fd
 *  e) counts of all FOP types passing through it
 *
 *  Usage: setfattr -n io-stats-dump /tmp/filename /mnt/gluster
 *
 */

#include <fnmatch.h>
#include <errno.h>
#include "glusterfs.h"
#include "xlator.h"
#include "io-stats-mem-types.h"


struct ios_global_stats {
        uint64_t        data_written;
        uint64_t        data_read;
        uint64_t        block_count_write[32];
        uint64_t        block_count_read[32];
        uint64_t        fop_hits[GF_FOP_MAXVALUE];
        struct timeval  started_at;
};


struct ios_conf {
        gf_lock_t                 lock;
        struct ios_global_stats   cumulative;
        uint64_t                  increment;
        struct ios_global_stats   incremental;
        gf_boolean_t              dump_fd_stats;
};


struct ios_fd {
        char           *filename;
        uint64_t        data_written;
        uint64_t        data_read;
        uint64_t        block_count_write[32];
        uint64_t        block_count_read[32];
        struct timeval  opened_at;
};


struct ios_local {
        struct timeval  wind_at;
        struct timeval  unwind_at;
};


#define BUMP_FOP(op)                                                    \
        do {                                                            \
                struct ios_conf  *conf = NULL;                          \
                                                                        \
                conf = this->private;                                   \
                LOCK (&conf->lock);                                     \
                {                                                       \
                        conf->cumulative.fop_hits[GF_FOP_##op]++;       \
                        conf->incremental.fop_hits[GF_FOP_##op]++;      \
                }                                                       \
                UNLOCK (&conf->lock);                                   \
        } while (0)


#define BUMP_READ(fd, len)                                              \
        do {                                                            \
                struct ios_conf  *conf = NULL;                          \
                struct ios_fd    *iosfd = NULL;                         \
                int               lb2 = 0;                              \
                                                                        \
                conf = this->private;                                   \
                lb2 = log_base2 (len);                                  \
                ios_fd_ctx_get (fd, this, &iosfd);                      \
                                                                        \
                LOCK (&conf->lock);                                     \
                {                                                       \
                        conf->cumulative.data_read += len;              \
                        conf->incremental.data_read += len;             \
                        conf->cumulative.block_count_read[lb2]++;       \
                        conf->incremental.block_count_read[lb2]++;      \
                                                                        \
                        if (iosfd) {                                    \
                                iosfd->data_read += len;                \
                                iosfd->block_count_read[lb2]++;         \
                        }                                               \
                }                                                       \
                UNLOCK (&conf->lock);                                   \
        } while (0)


#define BUMP_WRITE(fd, len)                                             \
        do {                                                            \
                struct ios_conf  *conf = NULL;                          \
                struct ios_fd    *iosfd = NULL;                         \
                int               lb2 = 0;                              \
                                                                        \
                conf = this->private;                                   \
                lb2 = log_base2 (len);                                  \
                ios_fd_ctx_get (fd, this, &iosfd);                      \
                                                                        \
                LOCK (&conf->lock);                                     \
                {                                                       \
                        conf->cumulative.data_written += len;           \
                        conf->incremental.data_written += len;          \
                        conf->cumulative.block_count_write[lb2]++;      \
                        conf->incremental.block_count_write[lb2]++;     \
                                                                        \
                        if (iosfd) {                                    \
                                iosfd->data_written += len;             \
                                iosfd->block_count_write[lb2]++;        \
                        }                                               \
                }                                                       \
                UNLOCK (&conf->lock);                                   \
        } while (0)


int
ios_fd_ctx_get (fd_t *fd, xlator_t *this, struct ios_fd **iosfd)
{
        uint64_t      iosfd64 = 0;
        unsigned long iosfdlong = 0;
        int           ret = 0;

        ret = fd_ctx_get (fd, this, &iosfd64);
        iosfdlong = iosfd64;
        if (ret != -1)
                *iosfd = (void *) iosfdlong;

        return ret;
}



int
ios_fd_ctx_set (fd_t *fd, xlator_t *this, struct ios_fd *iosfd)
{
        uint64_t   iosfd64 = 0;
        int        ret = 0;

        iosfd64 = (unsigned long) iosfd;
        ret = fd_ctx_set (fd, this, iosfd64);

        return ret;
}


#define ios_log(this, logfp, fmt ...)                           \
        do {                                                    \
                if (logfp) {                                    \
                        fprintf (logfp, fmt);                   \
                        fprintf (logfp, "\n");                  \
                }                                               \
                gf_log (this->name, GF_LOG_NORMAL, fmt);        \
        } while (0)


int
io_stats_dump_global (xlator_t *this, struct ios_global_stats *stats,
                      struct timeval *now, int interval, FILE *logfp)
{
        int    i = 0;

        if (interval == -1)
                ios_log (this, logfp, "=== Cumulative stats ===");
        else
                ios_log (this, logfp, "=== Interval %d stats ===",
                         interval);
        ios_log (this, logfp, "      Duration : %"PRId64"secs",
                 (uint64_t) (now->tv_sec - stats->started_at.tv_sec));
        ios_log (this, logfp, "     BytesRead : %"PRId64,
                 stats->data_read);
        ios_log (this, logfp, "  BytesWritten : %"PRId64,
                 stats->data_written);

        for (i = 0; i < 32; i++) {
                if (stats->block_count_read[i])
                        ios_log (this, logfp, " Read %06db+ : %"PRId64,
                                 (1 << i), stats->block_count_read[i]);
        }

        for (i = 0; i < 32; i++) {
                if (stats->block_count_write[i])
                        ios_log (this, logfp, "Write %06db+ : %"PRId64,
                                 (1 << i), stats->block_count_write[i]);
        }

        for (i = 0; i < GF_FOP_MAXVALUE; i++)
                if (stats->fop_hits[i])
                        ios_log (this, logfp, "%14s : %"PRId64,
                                 gf_fop_list[i], stats->fop_hits[i]);


        return 0;
}


int
io_stats_dump (xlator_t *this, char *filename, inode_t *inode,
               const char *path)
{
        struct ios_conf         *conf = NULL;
        struct ios_global_stats  cumulative = {0, };
        struct ios_global_stats  incremental = {0, };
        int                      increment = 0;
        struct timeval           now;
        FILE                    *logfp = NULL;

        conf = this->private;

        gettimeofday (&now, NULL);
        LOCK (&conf->lock);
        {
                cumulative  = conf->cumulative;
                incremental = conf->incremental;

                increment = conf->increment++;

                memset (&conf->incremental, 0, sizeof (conf->incremental));
                conf->incremental.started_at = now;
        }
        UNLOCK (&conf->lock);

        logfp = fopen (filename, "w+");
        io_stats_dump_global (this, &cumulative, &now, -1, logfp);
        io_stats_dump_global (this, &incremental, &now, increment, logfp);

        if (logfp)
                fclose (logfp);
        return 0;
}


int
io_stats_dump_fd (xlator_t *this, struct ios_fd *iosfd)
{
        struct ios_conf         *conf = NULL;
        struct timeval           now;
        uint64_t                 sec = 0;
        uint64_t                 usec = 0;
        int                      i = 0;

        conf = this->private;

        if (!conf->dump_fd_stats)
                return 0;

        if (!iosfd)
                return 0;

        gettimeofday (&now, NULL);

        if (iosfd->opened_at.tv_usec > now.tv_usec) {
                now.tv_usec += 1000000;
                now.tv_usec--;
        }

        sec = now.tv_sec - iosfd->opened_at.tv_sec;
        usec = now.tv_usec - iosfd->opened_at.tv_usec;

        gf_log (this->name, GF_LOG_NORMAL,
                "--- fd stats ---");

        if (iosfd->filename)
                gf_log (this->name, GF_LOG_NORMAL,
                        "      Filename : %s",
                        iosfd->filename);

        if (sec)
                gf_log (this->name, GF_LOG_NORMAL,
                        "      Lifetime : %"PRId64"secs, %"PRId64"usecs",
                        sec, usec);

        if (iosfd->data_read)
                gf_log (this->name, GF_LOG_NORMAL,
                        "     BytesRead : %"PRId64" bytes",
                        iosfd->data_read);

        if (iosfd->data_written)
                gf_log (this->name, GF_LOG_NORMAL,
                        "  BytesWritten : %"PRId64" bytes",
                        iosfd->data_written);

        for (i = 0; i < 32; i++) {
                if (iosfd->block_count_read[i])
                        gf_log (this->name, GF_LOG_NORMAL,
                                " Read %06db+ : %"PRId64,
                                (1 << i), iosfd->block_count_read[i]);
        }
        for (i = 0; i < 32; i++) {
                if (iosfd->block_count_write[i])
                        gf_log (this->name, GF_LOG_NORMAL,
                                "Write %06db+ : %"PRId64,
                                (1 << i), iosfd->block_count_write[i]);
        }
        return 0;
}


int
io_stats_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, fd_t *fd,
                     inode_t *inode, struct iatt *buf,
                     struct iatt *preparent, struct iatt *postparent)
{
        struct ios_fd *iosfd = NULL;
        char          *path = NULL;

        path = frame->local;
        frame->local = NULL;

        if (!path)
                goto unwind;

        if (op_ret < 0) {
                GF_FREE (path);
                goto unwind;
        }

        iosfd = GF_CALLOC (1, sizeof (*iosfd), gf_io_stats_mt_ios_fd);
        if (!iosfd) {
                GF_FREE (path);
                goto unwind;
        }

        iosfd->filename = path;
        gettimeofday (&iosfd->opened_at, NULL);

        ios_fd_ctx_set (fd, this, iosfd);

unwind:
        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent);
        return 0;
}


int
io_stats_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        struct ios_fd *iosfd = NULL;
        char          *path = NULL;

        path = frame->local;
        frame->local = NULL;

        if (!path)
                goto unwind;

        if (op_ret < 0) {
                GF_FREE (path);
                goto unwind;
        }

        iosfd = GF_CALLOC (1, sizeof (*iosfd), gf_io_stats_mt_ios_fd);
        if (!iosfd) {
                GF_FREE (path);
                goto unwind;
        }

        iosfd->filename = path;
        gettimeofday (&iosfd->opened_at, NULL);

        ios_fd_ctx_set (fd, this, iosfd);

unwind:
        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd);
        return 0;
}


int
io_stats_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, buf);
        return 0;
}


int
io_stats_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct iovec *vector, int32_t count,
                    struct iatt *buf, struct iobref *iobref)
{
        struct ios_conf *conf = NULL;
        int              len = 0;
        fd_t            *fd = NULL;

        conf = this->private;

        fd = frame->local;
        frame->local = NULL;

        if (op_ret > 0) {
                len = iov_length (vector, count);
                BUMP_READ (fd, len);
        }

        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno,
                             vector, count, buf, iobref);
        return 0;
}


int
io_stats_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *prebuf, struct iatt *postbuf)
{
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}




int
io_stats_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, gf_dirent_t *buf)
{
        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, buf);
        return 0;
}


int
io_stats_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, gf_dirent_t *buf)
{
        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, buf);
        return 0;
}


int
io_stats_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *prebuf, struct iatt *postbuf)
{
        STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf);
        return 0;
}


int
io_stats_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      struct iatt *preop, struct iatt *postop)
{
        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, preop, postop);
        return 0;
}


int
io_stats_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *preparent, struct iatt *postparent)
{
        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno,
                             preparent, postparent);
        return 0;
}


int
io_stats_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *buf,
                     struct iatt *preoldparent, struct iatt *postoldparent,
                     struct iatt *prenewparent, struct iatt *postnewparent)
{
        STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, buf,
                             preoldparent, postoldparent,
                             prenewparent, postnewparent);
        return 0;
}


int
io_stats_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, const char *buf,
                       struct iatt *sbuf)
{
        STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, buf, sbuf);
        return 0;
}


int
io_stats_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     inode_t *inode, struct iatt *buf,
                     dict_t *xattr, struct iatt *postparent)
{
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf, xattr,
                             postparent);
        return 0;
}


int
io_stats_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      inode_t *inode, struct iatt *buf,
                      struct iatt *preparent, struct iatt *postparent)
{
        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
io_stats_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    inode_t *inode, struct iatt *buf,
                    struct iatt *preparent, struct iatt *postparent)
{
        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
io_stats_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    inode_t *inode, struct iatt *buf,
                    struct iatt *preparent, struct iatt *postparent)
{
        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
io_stats_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   inode_t *inode, struct iatt *buf,
                   struct iatt *preparent, struct iatt *postparent)
{
        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
io_stats_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno);
        return 0;
}


int
io_stats_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        if (op_ret >= 0)
                ios_fd_ctx_set (fd, this, 0);

        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd);
        return 0;
}


int
io_stats_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *preparent, struct iatt *postparent)
{
        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno,
                             preparent, postparent);
        return 0;
}


int
io_stats_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *prebuf, struct iatt *postbuf)
{
        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno,
                             prebuf, postbuf);
        return 0;
}


int
io_stats_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct statvfs *buf)
{
        STACK_UNWIND_STRICT (statfs, frame, op_ret, op_errno, buf);
        return 0;
}


int
io_stats_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno);
        return 0;
}


int
io_stats_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        STACK_UNWIND (frame, op_ret, op_errno, dict);
        return 0;
}


int
io_stats_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno);
        return 0;
}


int
io_stats_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (fsyncdir, frame, op_ret, op_errno);
        return 0;
}


int
io_stats_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (access, frame, op_ret, op_errno);
        return 0;
}


int
io_stats_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno,
                        struct iatt *prebuf, struct iatt *postbuf)
{
        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno,
                             prebuf, postbuf);
        return 0;
}


int
io_stats_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, buf);
        return 0;
}


int
io_stats_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct flock *lock)
{
        STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, lock);
        return 0;
}


int
io_stats_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (entrylk, frame, op_ret, op_errno);
        return 0;
}


int
io_stats_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        STACK_UNWIND_STRICT (xattrop, frame, op_ret, op_errno, dict);
        return 0;
}


int
io_stats_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        STACK_UNWIND_STRICT (fxattrop, frame, op_ret, op_errno, dict);
        return 0;
}


int
io_stats_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (inodelk, frame, op_ret, op_errno);
        return 0;
}


int
io_stats_entrylk (call_frame_t *frame, xlator_t *this,
                  const char *volume, loc_t *loc, const char *basename,
                  entrylk_cmd cmd, entrylk_type type)
{
        BUMP_FOP (ENTRYLK);

        STACK_WIND (frame, io_stats_entrylk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->entrylk,
                    volume, loc, basename, cmd, type);
        return 0;
}


int
io_stats_inodelk (call_frame_t *frame, xlator_t *this,
                  const char *volume, loc_t *loc, int32_t cmd, struct flock *flock)
{

        BUMP_FOP (INODELK);

        STACK_WIND (frame, io_stats_inodelk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->inodelk,
                    volume, loc, cmd, flock);
        return 0;
}


int
io_stats_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno)
{

        STACK_UNWIND_STRICT (finodelk, frame, op_ret, op_errno);
        return 0;
}


int
io_stats_finodelk (call_frame_t *frame, xlator_t *this,
                   const char *volume, fd_t *fd, int32_t cmd, struct flock *flock)
{
        BUMP_FOP (FINODELK);

        STACK_WIND (frame, io_stats_finodelk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->finodelk,
                    volume, fd, cmd, flock);
        return 0;
}


int
io_stats_xattrop (call_frame_t *frame, xlator_t *this,
                  loc_t *loc, gf_xattrop_flags_t flags, dict_t *dict)
{
        BUMP_FOP (XATTROP);

        STACK_WIND (frame, io_stats_xattrop_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop,
                    loc, flags, dict);

        return 0;
}


int
io_stats_fxattrop (call_frame_t *frame, xlator_t *this,
                   fd_t *fd, gf_xattrop_flags_t flags, dict_t *dict)
{
        BUMP_FOP (FXATTROP);

        STACK_WIND (frame, io_stats_fxattrop_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fxattrop,
                    fd, flags, dict);

        return 0;
}


int
io_stats_lookup (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, dict_t *xattr_req)
{
        BUMP_FOP (LOOKUP);

        STACK_WIND (frame, io_stats_lookup_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup,
                    loc, xattr_req);

        return 0;
}


int
io_stats_stat (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        BUMP_FOP (STAT);

        STACK_WIND (frame, io_stats_stat_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat,
                    loc);

        return 0;
}


int
io_stats_readlink (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, size_t size)
{
        BUMP_FOP (READLINK);

        STACK_WIND (frame, io_stats_readlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readlink,
                    loc, size);

        return 0;
}


int
io_stats_mknod (call_frame_t *frame, xlator_t *this,
                loc_t *loc, mode_t mode, dev_t dev)
{
        BUMP_FOP (MKNOD);

        STACK_WIND (frame, io_stats_mknod_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mknod,
                    loc, mode, dev);

        return 0;
}


int
io_stats_mkdir (call_frame_t *frame, xlator_t *this,
                loc_t *loc, mode_t mode)
{
        BUMP_FOP (MKDIR);

        STACK_WIND (frame, io_stats_mkdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mkdir,
                    loc, mode);
        return 0;
}


int
io_stats_unlink (call_frame_t *frame, xlator_t *this,
                 loc_t *loc)
{
        BUMP_FOP (UNLINK);

        STACK_WIND (frame, io_stats_unlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink,
                    loc);
        return 0;
}


int
io_stats_rmdir (call_frame_t *frame, xlator_t *this,
                loc_t *loc)
{
        BUMP_FOP (RMDIR);

        STACK_WIND (frame, io_stats_rmdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rmdir,
                    loc);

        return 0;
}


int
io_stats_symlink (call_frame_t *frame, xlator_t *this,
                  const char *linkpath, loc_t *loc)
{
        BUMP_FOP (SYMLINK);

        STACK_WIND (frame, io_stats_symlink_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->symlink,
                    linkpath, loc);

        return 0;
}


int
io_stats_rename (call_frame_t *frame, xlator_t *this,
                 loc_t *oldloc, loc_t *newloc)
{
        BUMP_FOP (RENAME);

        STACK_WIND (frame, io_stats_rename_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename,
                    oldloc, newloc);

        return 0;
}


int
io_stats_link (call_frame_t *frame, xlator_t *this,
               loc_t *oldloc, loc_t *newloc)
{
        BUMP_FOP (LINK);

        STACK_WIND (frame, io_stats_link_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->link,
                    oldloc, newloc);
        return 0;
}


int
io_stats_setattr (call_frame_t *frame, xlator_t *this,
                  loc_t *loc, struct iatt *stbuf, int32_t valid)
{
        BUMP_FOP (SETATTR);

        STACK_WIND (frame, io_stats_setattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setattr,
                    loc, stbuf, valid);

        return 0;
}


int
io_stats_truncate (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, off_t offset)
{
        BUMP_FOP (TRUNCATE);

        STACK_WIND (frame, io_stats_truncate_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate,
                    loc, offset);

        return 0;
}


int
io_stats_open (call_frame_t *frame, xlator_t *this,
               loc_t *loc, int32_t flags, fd_t *fd, int32_t wbflags)
{
        BUMP_FOP (OPEN);

        frame->local = gf_strdup (loc->path);

        STACK_WIND (frame, io_stats_open_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open,
                    loc, flags, fd, wbflags);
        return 0;
}


int
io_stats_create (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, int32_t flags, mode_t mode, fd_t *fd)
{
        BUMP_FOP (CREATE);

        frame->local = gf_strdup (loc->path);

        STACK_WIND (frame, io_stats_create_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create,
                    loc, flags, mode, fd);
        return 0;
}


int
io_stats_readv (call_frame_t *frame, xlator_t *this,
                fd_t *fd, size_t size, off_t offset)
{
        BUMP_FOP (READ);

        frame->local = fd;

        STACK_WIND (frame, io_stats_readv_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readv,
                    fd, size, offset);
        return 0;
}


int
io_stats_writev (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, struct iovec *vector,
                 int32_t count, off_t offset,
                 struct iobref *iobref)
{
        struct ios_conf    *conf = NULL;
        int                 len = 0;

        conf = this->private;

        len = iov_length (vector, count);

        BUMP_FOP (WRITE);
        BUMP_WRITE (fd, len);

        STACK_WIND (frame, io_stats_writev_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev,
                    fd, vector, count, offset, iobref);
        return 0;
}


int
io_stats_statfs (call_frame_t *frame, xlator_t *this,
                 loc_t *loc)
{
        BUMP_FOP (STATFS);

        STACK_WIND (frame, io_stats_statfs_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->statfs,
                    loc);
        return 0;
}


int
io_stats_flush (call_frame_t *frame, xlator_t *this,
                fd_t *fd)
{
        BUMP_FOP (FLUSH);

        STACK_WIND (frame, io_stats_flush_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->flush,
                    fd);
        return 0;
}


int
io_stats_fsync (call_frame_t *frame, xlator_t *this,
                fd_t *fd, int32_t flags)
{
        BUMP_FOP (FSYNC);

        STACK_WIND (frame, io_stats_fsync_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsync,
                    fd, flags);
        return 0;
}


void
conditional_dump (dict_t *dict, char *key, data_t *value, void *data)
{
        struct {
                xlator_t       *this;
                inode_t        *inode;
                const char     *path;
        } *stub;
        xlator_t   *this = NULL;
        inode_t    *inode = NULL;
        const char *path = NULL;
        char       *filename = NULL;

        stub  = data;
        this  = stub->this;
        inode = stub->inode;
        path  = stub->path;

        filename = alloca (value->len + 1);
        memset (filename, 0, value->len + 1);
        memcpy (filename, data_to_str (value), value->len);

        if (fnmatch ("*io*stat*dump", key, 0) == 0) {
                io_stats_dump (this, filename, inode, path);
        }
}


int
io_stats_setxattr (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, dict_t *dict,
                   int32_t flags)
{
        struct {
                xlator_t     *this;
                inode_t      *inode;
                const char   *path;
        } stub;

        BUMP_FOP (SETXATTR);

        stub.this  = this;
        stub.inode = loc->inode;
        stub.path  = loc->path;

        dict_foreach (dict, conditional_dump, &stub);

        STACK_WIND (frame, io_stats_setxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    loc, dict, flags);
        return 0;
}


int
io_stats_getxattr (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, const char *name)
{
        BUMP_FOP (GETXATTR);

        STACK_WIND (frame, io_stats_getxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr,
                    loc, name);
        return 0;
}


int
io_stats_removexattr (call_frame_t *frame, xlator_t *this,
                      loc_t *loc, const char *name)
{
        BUMP_FOP (REMOVEXATTR);

        STACK_WIND (frame, io_stats_removexattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr,
                    loc, name);

        return 0;
}


int
io_stats_opendir (call_frame_t *frame, xlator_t *this,
                  loc_t *loc, fd_t *fd)
{
        BUMP_FOP (OPENDIR);

        STACK_WIND (frame, io_stats_opendir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->opendir,
                    loc, fd);
        return 0;
}

int
io_stats_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                   off_t offset)
{
        BUMP_FOP (READDIRP);

        STACK_WIND (frame, io_stats_readdirp_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdirp,
                    fd, size, offset);

        return 0;
}


int
io_stats_readdir (call_frame_t *frame, xlator_t *this,
                  fd_t *fd, size_t size, off_t offset)
{
        BUMP_FOP (READDIR);

        STACK_WIND (frame, io_stats_readdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdir,
                    fd, size, offset);

        return 0;
}


int
io_stats_fsyncdir (call_frame_t *frame, xlator_t *this,
                   fd_t *fd, int32_t datasync)
{
        BUMP_FOP (FSYNCDIR);

        STACK_WIND (frame, io_stats_fsyncdir_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsyncdir,
                    fd, datasync);
        return 0;
}


int
io_stats_access (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, int32_t mask)
{
        BUMP_FOP (ACCESS);

        STACK_WIND (frame, io_stats_access_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->access,
                    loc, mask);
        return 0;
}


int
io_stats_ftruncate (call_frame_t *frame, xlator_t *this,
                    fd_t *fd, off_t offset)
{
        BUMP_FOP (FTRUNCATE);

        STACK_WIND (frame, io_stats_ftruncate_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate,
                    fd, offset);

        return 0;
}


int
io_stats_fsetattr (call_frame_t *frame, xlator_t *this,
                   fd_t *fd, struct iatt *stbuf, int32_t valid)
{
        BUMP_FOP (FSETATTR);

        STACK_WIND (frame, io_stats_setattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetattr,
                    fd, stbuf, valid);
        return 0;
}


int
io_stats_fstat (call_frame_t *frame, xlator_t *this,
                fd_t *fd)
{
        BUMP_FOP (FSTAT);

        STACK_WIND (frame, io_stats_fstat_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat,
                    fd);
        return 0;
}


int
io_stats_lk (call_frame_t *frame, xlator_t *this,
             fd_t *fd, int32_t cmd, struct flock *lock)
{
        BUMP_FOP (LK);

        STACK_WIND (frame, io_stats_lk_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lk,
                    fd, cmd, lock);
        return 0;
}


int
io_stats_checksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       uint8_t *fchecksum, uint8_t *dchecksum)
{
        STACK_UNWIND_STRICT (checksum, frame, op_ret, op_errno,
                             fchecksum, dchecksum);

        return 0;
}


int
io_stats_checksum (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, int32_t flag)
{
        STACK_WIND (frame, io_stats_checksum_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->checksum,
                    loc, flag);

        return 0;
}



int
io_stats_release (xlator_t *this, fd_t *fd)
{
        struct ios_fd  *iosfd = NULL;

        BUMP_FOP (RELEASE);

        ios_fd_ctx_get (fd, this, &iosfd);
        if (iosfd) {
                io_stats_dump_fd (this, iosfd);

                if (iosfd->filename)
                        GF_FREE (iosfd->filename);
                GF_FREE (iosfd);
        }

        return 0;
}


int
io_stats_releasedir (xlator_t *this, fd_t *fd)
{
        BUMP_FOP (RELEASEDIR);

        return 0;
}


int
io_stats_forget (xlator_t *this, inode_t *inode)
{
        BUMP_FOP (FORGET);

        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_io_stats_mt_end + 1);
        
        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        " failed");
                return ret;
        }

        return ret;
}

int
init (xlator_t *this)
{
        dict_t             *options = NULL;
        struct ios_conf    *conf = NULL;
        char               *str = NULL;
        int                 ret = 0;

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

        options = this->options;

        conf = GF_CALLOC (1, sizeof(*conf), gf_io_stats_mt_ios_conf);

        if (!conf) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                return -1;
        }

        LOCK_INIT (&conf->lock);

        gettimeofday (&conf->cumulative.started_at, NULL);
        gettimeofday (&conf->incremental.started_at, NULL);

        ret = dict_get_str (options, "dump-fd-stats", &str);
        if (ret == 0) {
                ret = gf_string2boolean (str, &conf->dump_fd_stats);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "'dump-fd-stats' takes only boolean arguments");
                        return -1;
                }

                if (conf->dump_fd_stats) {
			gf_log (this->name, GF_LOG_DEBUG,
				"enabling dump-fd-stats");
                }
        }

        this->private = conf;

        return 0;
}


void
fini (xlator_t *this)
{
        struct ios_conf *conf = NULL;

        if (!this)
                return;

        conf = this->private;

        GF_FREE(conf);

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
        .readdirp    = io_stats_readdirp,
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
        .checksum    = io_stats_checksum,
        .xattrop     = io_stats_xattrop,
        .fxattrop    = io_stats_fxattrop,
        .setattr     = io_stats_setattr,
        .fsetattr    = io_stats_fsetattr,
};

struct xlator_cbks cbks = {
        .release     = io_stats_release,
        .releasedir  = io_stats_releasedir,
        .forget      = io_stats_forget,
};

struct volume_options options[] = {
        { .key  = {"dump-fd-stats"},
          .type = GF_OPTION_TYPE_BOOL,
        },
        { .key  = {NULL} },
};
