/*
  Copyright (c) 2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "dht-common.h"

#define GF_DISK_SECTOR_SIZE             512
#define DHT_REBALANCE_PID               4242 /* Change it if required */
#define DHT_REBALANCE_BLKSIZE           (128 * 1024)
#define DHT_MIGRATE_EVEN_IF_LINK_EXISTS 1

static int
dht_write_with_holes (xlator_t *to, fd_t *fd, struct iovec *vec, int count,
                      int32_t size, off_t offset, struct iobref *iobref)
{
        int i            = 0;
        int ret          = -1;
        int start_idx    = 0;
        int tmp_offset   = 0;
        int write_needed = 0;
        int buf_len      = 0;
        int size_pending = 0;
        char *buf        = NULL;

        /* loop through each vector */
        for (i = 0; i < count; i++) {
                buf = vec[i].iov_base;
                buf_len = vec[i].iov_len;

                for (start_idx = 0; (start_idx + GF_DISK_SECTOR_SIZE) <= buf_len;
                     start_idx += GF_DISK_SECTOR_SIZE) {

                        if (mem_0filled (buf + start_idx, GF_DISK_SECTOR_SIZE) != 0) {
                                write_needed = 1;
                                continue;
                        }

                        if (write_needed) {
                                ret = syncop_write (to, fd, (buf + tmp_offset),
                                                    (start_idx - tmp_offset),
                                                    (offset + tmp_offset),
                                                    iobref);
                                if (ret < 0)
                                        goto out;

                                write_needed = 0;
                        }
                        tmp_offset = start_idx + GF_DISK_SECTOR_SIZE;
                }

                if ((start_idx < buf_len) || write_needed) {
                        /* This means, last chunk is not yet written.. write it */
                        ret = syncop_write (to, fd, (buf + tmp_offset),
                                            (buf_len - tmp_offset),
                                            (offset + tmp_offset), iobref);
                        if (ret < 0)
                                goto out;
                }

                size_pending = (size - buf_len);
                if (!size_pending)
                        break;
        }

        /* do it regardless of all the above cases as we had to 'write' the
           given number of bytes */
        ret = syncop_ftruncate (to, fd, offset + size);
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to perform truncate on %s", to->name);
                goto out;
        }

        ret = size;
out:
        return ret;

}

static inline int
__is_file_migratable (xlator_t *this, loc_t *loc, dict_t *rsp_dict,
                      struct iatt *stbuf)
{
        int ret           = -1;
        int open_fd_count = 0;

        if (!IA_ISREG (stbuf->ia_type)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: migrate-file called on non-regular entry (0%o)",
                        loc->path, stbuf->ia_type);
                ret = -1;
                goto out;
        }

        if (stbuf->ia_nlink > 1) {
                /* TODO : support migrating hardlinks */
                gf_log (this->name, GF_LOG_WARNING, "%s: file has hardlinks",
                        loc->path);
                ret = -ENOTSUP;
                goto out;
        }

        ret = dict_get_int32 (rsp_dict, GLUSTERFS_OPEN_FD_COUNT, &open_fd_count);
        if (!ret && (open_fd_count > 0)) {
                /* TODO: support migration of files with open fds */
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: file has open fds, not attempting migration",
                        loc->path);
                goto out;
        }
        ret = 0;

out:
        return ret;
}

static inline int
__dht_rebalance_create_dst_file (xlator_t *to, loc_t *loc, struct iatt *stbuf,
                                 dict_t *dict, fd_t **dst_fd, int *need_unlink)
{
        xlator_t *this = NULL;
        int       ret  = -1;
        mode_t    mode = 0;
        fd_t     *fd   = NULL;
        struct iatt new_stbuf = {0,};

        this = THIS;

        ret = dict_set_static_bin (dict, "gfid-req", stbuf->ia_gfid, 16);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: failed to set gfid in dict for create", loc->path);
                goto out;
        }

        fd = fd_create (loc->inode, DHT_REBALANCE_PID);
        if (!fd) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: fd create failed (destination)", loc->path);
                ret = -1;
                goto out;
        }

        ret = syncop_lookup (to, loc, NULL, &new_stbuf, NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "failed to lookup %s on %s",
                        loc->path, to->name);

                mode = st_mode_from_ia (stbuf->ia_prot, stbuf->ia_type);
                ret = syncop_create (to, loc, O_WRONLY, mode, fd, dict);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to create %s on %s", loc->path, to->name);
                        goto out;
                }

                *need_unlink = 1;
                goto done;
        }

        /* File exits in the destination, just do the open if gfid matches */
        if (uuid_compare (stbuf->ia_gfid, new_stbuf.ia_gfid) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "file %s exits in %s with different gfid",
                        loc->path, to->name);
                fd_unref (fd);
                goto out;
        }

        ret = syncop_open (to, loc, O_WRONLY, fd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to open file %s on %s",
                        loc->path, to->name);
                fd_unref (fd);
                goto out;
        }
done:
        if (dst_fd)
                *dst_fd = fd;

        /* success */
        ret = 0;

out:
        return ret;
}

static inline int
__dht_check_free_space (xlator_t *to, xlator_t *from, loc_t *loc,
                        struct iatt *stbuf)
{
        struct statvfs  src_statfs = {0,};
        struct statvfs  dst_statfs = {0,};
        int             ret        = -1;
        xlator_t       *this       = NULL;

        this = THIS;

        ret = syncop_statfs (from, loc, &src_statfs);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to get statfs of %s on %s",
                        loc->path, from->name);
                goto out;
        }

        ret = syncop_statfs (to, loc, &dst_statfs);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to get statfs of %s on %s",
                        loc->path, to->name);
                goto out;
        }
        if (((dst_statfs.f_bavail *
              dst_statfs.f_bsize) / GF_DISK_SECTOR_SIZE) >
            (((src_statfs.f_bavail * src_statfs.f_bsize) /
              GF_DISK_SECTOR_SIZE) - stbuf->ia_blocks)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "data movement attempted from node (%s) with"
                        " higher disk space to a node (%s) with "
                        "lesser disk space (%s)", from->name,
                        to->name, loc->path);

                ret = -1;
                goto out;
        }

        ret = 0;
out:
        return ret;
}

static inline int
__dht_rebalane_migrate_data (xlator_t *from, xlator_t *to, fd_t *src, fd_t *dst,
                             int hole_exists)
{
        int            ret    = -1;
        int            count  = 0;
        off_t          offset = 0;
        struct iovec  *vector = NULL;
        struct iobref *iobref = NULL;

        while (1) {
                ret = syncop_readv (from, src, DHT_REBALANCE_BLKSIZE,
                                    offset, &vector, &count, &iobref);
                if (!ret || (ret < 0)) {
                        break;
                }

                if (hole_exists)
                        ret = dht_write_with_holes (to, dst, vector, count,
                                                    ret, offset, iobref);
                else
                        ret = syncop_writev (to, dst, vector, count,
                                             offset, iobref);
                if (ret < 0) {
                        break;
                }
                offset += ret;

                if (vector)
                        GF_FREE (vector);
                if (iobref)
                        iobref_unref (iobref);
                iobref = NULL;
                vector = NULL;
        }
        if (iobref)
                iobref_unref (iobref);
        if (vector)
                GF_FREE (vector);

        if (ret >= 0)
                ret = 0;

        return ret;
}

int
dht_migrate_file (xlator_t *this, loc_t *loc, xlator_t *from, xlator_t *to,
                  int flag)
{
        int             ret            = -1;
        struct iatt     new_stbuf      = {0,};
        struct iatt     stbuf          = {0,};
        fd_t           *src_fd         = NULL;
        fd_t           *dst_fd         = NULL;
        dict_t         *dict           = NULL;
        dict_t         *xattr          = NULL;
        dict_t         *rsp_dict       = NULL;
        int             file_has_holes = 0;
        int             need_unlink    = 0;

        gf_log (this->name, GF_LOG_INFO, "%s: attempting to move from %s to %s",
                loc->path, from->name, to->name);

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_int32 (dict, GLUSTERFS_OPEN_FD_COUNT, 4);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to set fd-count key in dict, may attempt "
                        "migration of file which has open fds", loc->path);

        ret = syncop_lookup (from, loc, dict, &stbuf, &rsp_dict, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to lookup %s on %s",
                        loc->path, from->name);
                goto out;
        }

        /* Check if file can be migrated */
        ret = __is_file_migratable (this, loc, rsp_dict, &stbuf);
        if (ret)
                goto out;

        /* create the destination */
        ret = __dht_rebalance_create_dst_file (to, loc, &stbuf, dict, &dst_fd,
                                               &need_unlink);
        if (ret)
                goto out;

        /* Should happen on all files when 'force' option is not given */
        if (flag != DHT_MIGRATE_EVEN_IF_LINK_EXISTS) {
                ret = __dht_check_free_space (to, from, loc, &stbuf);
                if (ret)
                        goto out;
        }

        /* Try to preserve 'holes' while migrating data */
        if (stbuf.ia_size > (stbuf.ia_blocks * GF_DISK_SECTOR_SIZE))
                file_has_holes = 1;

        src_fd = fd_create (loc->inode, DHT_REBALANCE_PID);
        if (!src_fd) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: fd create failed (source)", loc->path);
                ret = -1;
                goto out;
        }

        ret = syncop_open (from, loc, O_RDONLY, src_fd);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to open file %s on %s",
                        loc->path, from->name);
                goto out;
        }

        /* All I/O happens in this function */
        ret = __dht_rebalane_migrate_data (from, to, src_fd, dst_fd,
                                           file_has_holes);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "%s: failed to migrate data",
                        loc->path);
                goto out;
        }

        ret = syncop_lookup (from, loc, NULL, &new_stbuf, NULL, NULL);
        if (ret < 0) {
                /* Failed to get the stat info */
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to lookup file %s on %s",
                        loc->path, from->name);
                need_unlink = 0;
                goto out;
        }

        /* No need to rebalance, if there is some
           activity on source file */
        if (new_stbuf.ia_mtime != stbuf.ia_mtime) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: ignoring destination file as source has "
                        "undergone some changes while migration was happening",
                        loc->path);
                ret = -1;
                goto out;
        }

        ret = syncop_setattr (to, loc, &new_stbuf,
                              (GF_SET_ATTR_UID | GF_SET_ATTR_GID |
                               GF_SET_ATTR_MODE | GF_SET_ATTR_ATIME |
                               GF_SET_ATTR_MTIME), NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to perform setattr on %s",
                        loc->path, to->name);
        }

        ret = syncop_listxattr (from, loc, &xattr);
        if (ret == -1)
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to get xattr from %s", loc->path, from->name);

        ret = syncop_setxattr (to, loc, xattr, 0);
        if (ret == -1)
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to set xattr on %s", loc->path, to->name);

        /* rebalance complete */
        syncop_close (dst_fd);
        syncop_close (src_fd);
        syncop_unlink (from, loc);
        need_unlink = 0;

        gf_log (this->name, GF_LOG_INFO,
                "completed migration of %s from subvolume %s to %s",
                loc->path, from->name, to->name);

        ret = 0;
out:
        if (dict)
                dict_unref (dict);

        if (ret) {
                if (dst_fd)
                        syncop_close (dst_fd);
                if (src_fd)
                        syncop_close (src_fd);
                if (need_unlink)
                        syncop_unlink (to, loc);
        }

        return ret;
}

static int
rebalance_task (void *data)
{
        int           ret   = -1;
        xlator_t     *this  = NULL;
        dht_local_t  *local = NULL;
        call_frame_t *frame = NULL;

        frame = data;
        this = THIS;

        local = frame->local;

        /* This function is 'synchrounous', hence if it returns,
           we are done with the task */
        ret = dht_migrate_file (THIS, &local->loc, local->from_subvol,
                                local->to_subvol, local->flags);

        return ret;
}

static int
rebalance_task_completion (int op_ret, call_frame_t *sync_frame, void *data)
{
        int           ret        = -1;
        uint64_t      layout_int = 0;
        dht_layout_t *layout     = 0;
        xlator_t     *this       = NULL;
        dht_local_t  *local      = NULL;

        this = THIS;
        local = sync_frame->local;

        if (!op_ret) {
                /* Make sure we have valid 'layout' in inode ctx
                   after the operation */
                ret = inode_ctx_del (local->loc.inode, this, &layout_int);
                if (!ret && layout_int) {
                        layout = (dht_layout_t *)(long)layout_int;
                        dht_layout_unref (this, layout);
                }

                ret = dht_layout_preset (this, local->to_subvol,
                                         local->loc.inode);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: failed to set inode ctx", local->loc.path);
        }

        /* if success, errno is not checked,
           if ret is -1, then let errno be 'ENOTSUP' */
        DHT_STACK_UNWIND (setxattr, sync_frame, op_ret, ENOTSUP);
        return 0;
}

int
dht_start_rebalance_task (xlator_t *this, call_frame_t *frame)
{
        int         ret     = -1;
        dht_conf_t *conf    = NULL;

        conf = this->private;

        ret = synctask_new (conf->env, rebalance_task,
                            rebalance_task_completion,
                            frame, frame);
        return ret;
}
