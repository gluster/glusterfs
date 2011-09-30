/*
  Copyright (c) 2011 Gluster, Inc. <http://www.gluster.com>
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

        ret = size;
out:
        return ret;

}

static inline int
__is_file_migratable (xlator_t *this, loc_t *loc, dict_t *rsp_dict,
                      struct iatt *stbuf)
{
        int ret = -1;

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

        ret = 0;

out:
        return ret;
}

static inline int
__dht_rebalance_create_dst_file (xlator_t *to, xlator_t *from, loc_t *loc, struct iatt *stbuf,
                                 dict_t *dict, fd_t **dst_fd)
{
        xlator_t *this = NULL;
        int       ret  = -1;
        fd_t     *fd   = NULL;
        struct iatt new_stbuf = {0,};

        this = THIS;

        ret = dict_set_static_bin (dict, "gfid-req", stbuf->ia_gfid, 16);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: failed to set gfid in dict for create", loc->path);
                goto out;
        }

        ret = dict_set_str (dict, DHT_LINKFILE_KEY, from->name);
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
        if (!ret) {
                /* File exits in the destination, check if gfid matches */
                if (uuid_compare (stbuf->ia_gfid, new_stbuf.ia_gfid) != 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "file %s exits in %s with different gfid",
                                loc->path, to->name);
                        fd_unref (fd);
                        goto out;
                }
        }

        /* Create the destination with LINKFILE mode, and linkto xattr,
           if the linkfile already exists, it will just open the file */
        ret = syncop_create (to, loc, O_RDWR, DHT_LINKFILE_MODE, fd,
                             dict);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create %s on %s", loc->path, to->name);
                goto out;
        }

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

                /* this is not a 'failure', but we don't want to
                   consider this as 'success' too :-/ */
                ret = 1;
                goto out;
        }

        ret = 0;
out:
        return ret;
}

static inline int
__dht_rebalane_migrate_data (xlator_t *from, xlator_t *to, fd_t *src, fd_t *dst,
                             uint64_t ia_size, int hole_exists)
{
        int            ret    = 0;
        int            count  = 0;
        off_t          offset = 0;
        struct iovec  *vector = NULL;
        struct iobref *iobref = NULL;
        uint64_t       total  = 0;
        size_t         read_size = 0;

        /* if file size is '0', no need to enter this loop */
        while (total < ia_size) {
                read_size = (((ia_size - total) > DHT_REBALANCE_BLKSIZE) ?
                             DHT_REBALANCE_BLKSIZE : (ia_size - total));
                ret = syncop_readv (from, src, read_size,
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
                total += ret;

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


static inline int
__dht_rebalance_open_src_file (xlator_t *from, xlator_t *to, loc_t *loc,
                               struct iatt *stbuf, fd_t **src_fd)
{
        int          ret  = 0;
        fd_t        *fd   = NULL;
        dict_t      *dict = NULL;
        xlator_t    *this = NULL;
        struct iatt  iatt = {0,};

        this = THIS;

        fd = fd_create (loc->inode, DHT_REBALANCE_PID);
        if (!fd) {
                gf_log (this->name, GF_LOG_ERROR,
                        "%s: fd create failed (source)", loc->path);
                ret = -1;
                goto out;
        }

        ret = syncop_open (from, loc, O_RDWR, fd);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to open file %s on %s",
                        loc->path, from->name);
                goto out;
        }

        ret = -1;
        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_str (dict, DHT_LINKFILE_KEY, to->name);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to set xattr in dict for %s (linkto:%s)",
                        loc->path, to->name);
                goto out;
        }

        /* Once the migration starts, the source should have 'linkto' key set
           to show which is the target, so other clients can work around it */
        ret = syncop_setxattr (from, loc, dict, 0);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to set xattr on %s in %s",
                        loc->path, from->name);
                goto out;
        }

        /* mode should be (+S+T) to indicate migration is in progress */
        iatt.ia_prot = stbuf->ia_prot;
        iatt.ia_type = stbuf->ia_type;
        iatt.ia_prot.sticky = 1;
        iatt.ia_prot.sgid = 1;

        ret = syncop_setattr (from, loc, &iatt, GF_SET_ATTR_MODE, NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to set mode on %s in %s",
                        loc->path, from->name);
                goto out;
        }

        if (src_fd)
                *src_fd = fd;

        /* success */
        ret = 0;
out:
        if (dict)
                dict_unref (dict);

        return ret;
}


/*
  return values:

   -1 : failure
    0 : successfully migrated data
    1 : not a failure, but we can't migrate data as of now
*/
int
dht_migrate_file (xlator_t *this, loc_t *loc, xlator_t *from, xlator_t *to,
                  int flag)
{
        int             ret            = -1;
        struct iatt     new_stbuf      = {0,};
        struct iatt     stbuf          = {0,};
        struct iatt     empty_iatt     = {0,};
        ia_prot_t       src_ia_prot    = {0,};
        fd_t           *src_fd         = NULL;
        fd_t           *dst_fd         = NULL;
        dict_t         *dict           = NULL;
        dict_t         *xattr          = NULL;
        dict_t         *rsp_dict       = NULL;
        int             file_has_holes = 0;

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

        /* Phase 1 - Data migration is in progress from now on */
        ret = syncop_lookup (from, loc, dict, &stbuf, &rsp_dict, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to lookup %s on %s",
                        loc->path, from->name);
                goto out;
        }

        /* preserve source mode, so set the same to the destination */
        src_ia_prot = stbuf.ia_prot;

        /* Check if file can be migrated */
        ret = __is_file_migratable (this, loc, rsp_dict, &stbuf);
        if (ret)
                goto out;

        /* create the destination, with required modes/xattr */
        ret = __dht_rebalance_create_dst_file (to, from, loc, &stbuf,
                                               dict, &dst_fd);
        if (ret)
                goto out;

        /* Should happen on all files when 'force' option is not given */
        if (flag != DHT_MIGRATE_EVEN_IF_LINK_EXISTS) {
                ret = __dht_check_free_space (to, from, loc, &stbuf);
                if (ret) {
                        goto out;
                }
        }

        /* Open the source, and also update mode/xattr */
        ret = __dht_rebalance_open_src_file (from, to, loc, &stbuf, &src_fd);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to open %s on %s",
                        loc->path, from->name);
                goto out;
        }

        ret = syncop_fstat (from, src_fd, &stbuf);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to lookup %s on %s",
                        loc->path, from->name);
                goto out;
        }

        /* Try to preserve 'holes' while migrating data */
        if (stbuf.ia_size > (stbuf.ia_blocks * GF_DISK_SECTOR_SIZE))
                file_has_holes = 1;

        /* All I/O happens in this function */
        ret = __dht_rebalane_migrate_data (from, to, src_fd, dst_fd,
                                           stbuf.ia_size, file_has_holes);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "%s: failed to migrate data",
                        loc->path);
                /* reset the destination back to 0 */
                ret = syncop_ftruncate (to, dst_fd, 0);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s: failed to reset the target size back to 0",
                                loc->path);
                }

                ret = -1;
                goto out;
        }

        /* TODO: move all xattr related operations to fd based operations */
        ret = syncop_listxattr (from, loc, &xattr);
        if (ret == -1)
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to get xattr from %s", loc->path, from->name);

        ret = syncop_setxattr (to, loc, xattr, 0);
        if (ret == -1)
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to set xattr on %s", loc->path, to->name);

        /* TODO: Sync the locks */

        ret = syncop_fsync (to, dst_fd);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to fsync on %s", loc->path, to->name);


        /* Phase 2 - Data-Migration Complete, Housekeeping updates pending */

        ret = syncop_fstat (from, src_fd, &new_stbuf);
        if (ret < 0) {
                /* Failed to get the stat info */
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to fstat file %s on %s",
                        loc->path, from->name);
                goto out;
        }

        /* source would have both sticky bit and sgid bit set, reset it to 0,
           and set the source permission on destination, if it was not set
           prior to setting rebalance-modes in source  */
        if (!src_ia_prot.sticky)
                new_stbuf.ia_prot.sticky = 0;

        if (!src_ia_prot.sgid)
                new_stbuf.ia_prot.sgid = 0;

        /* TODO: if the source actually had sticky bit, or sgid bit set,
           we are not handling it */

        ret = syncop_fsetattr (to, dst_fd, &new_stbuf,
                               (GF_SET_ATTR_UID | GF_SET_ATTR_GID |
                                GF_SET_ATTR_MODE), NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to perform setattr on %s",
                        loc->path, to->name);
        }

        /* Because 'futimes' is not portable */
        ret = syncop_setattr (to, loc, &new_stbuf,
                              (GF_SET_ATTR_MTIME | GF_SET_ATTR_ATIME),
                              NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to perform setattr on %s",
                        loc->path, to->name);
        }

        /* Make the source as a linkfile first before deleting it */
        empty_iatt.ia_prot.sticky = 1;
        ret = syncop_fsetattr (from, src_fd, &empty_iatt,
                               GF_SET_ATTR_MODE, NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,             \
                        "%s: failed to perform setattr on %s",
                        loc->path, from->name);
        }

        /* Do a stat and check the gfid before unlink */
        ret = syncop_stat (from, loc, &empty_iatt);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to do a stat on %s",
                        loc->path, from->name);
        }

        if (uuid_compare (empty_iatt.ia_gfid, loc->inode->gfid) == 0) {
                /* take out the source from namespace */
                ret = syncop_unlink (from, loc);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: failed to perform unlink on %s",
                                loc->path, from->name);
                }
        }

        /* Free up the data blocks on the source node, as the whole
           file is migrated */
        ret = syncop_ftruncate (from, src_fd, 0);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to perform truncate on %s",
                        loc->path, from->name);
        }

        /* remove the 'linkto' xattr from the destination */
        ret = syncop_removexattr (to, loc, DHT_LINKFILE_KEY);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to perform removexattr on %s",
                        loc->path, to->name);
        }

        ret = syncop_lookup (this, loc, NULL, NULL, NULL, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to lookup the file on subvolumes",
                        loc->path);
        }

        gf_log (this->name, GF_LOG_INFO,
                "completed migration of %s from subvolume %s to %s",
                loc->path, from->name, to->name);

        ret = 0;
out:
        if (dict)
                dict_unref (dict);

        if (dst_fd)
                syncop_close (dst_fd);
        if (src_fd)
                syncop_close (src_fd);

        return ret;
}

static int
rebalance_task (void *data)
{
        int           ret   = -1;
        dht_local_t  *local = NULL;
        call_frame_t *frame = NULL;

        frame = data;

        local = frame->local;

        /* This function is 'synchrounous', hence if it returns,
           we are done with the task */
        ret = dht_migrate_file (THIS, &local->loc, local->rebalance.from_subvol,
                                local->rebalance.target_node, local->flags);

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
        int32_t       op_errno   = EINVAL;

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

                ret = dht_layout_preset (this, local->rebalance.target_node,
                                         local->loc.inode);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: failed to set inode ctx", local->loc.path);
        }

        if (op_ret == -1) {
                /* Failure of migration process, mostly due to write process.
                   as we can't preserve the exact errno, lets say there was
                   no space to migrate-data
                */
                op_errno = ENOSPC;
        }

        if (op_ret == 1) {
                /* migration didn't happen, but is not a failure, let the user
                   understand that he doesn't have permission to migrate the
                   file.
                */
                op_ret = -1;
                op_errno = EPERM;
        }

        DHT_STACK_UNWIND (setxattr, sync_frame, op_ret, op_errno);
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
