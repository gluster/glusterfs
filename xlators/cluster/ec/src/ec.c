/*
  Copyright (c) 2012 DataLab, s.l. <http://www.datalab.es>

  This file is part of the cluster/ec translator for GlusterFS.

  The cluster/ec translator for GlusterFS is free software: you can
  redistribute it and/or modify it under the terms of the GNU General
  Public License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  The cluster/ec translator for GlusterFS is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE. See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with the cluster/ec translator for GlusterFS. If not, see
  <http://www.gnu.org/licenses/>.
*/

#include "defaults.h"
#include "statedump.h"

#include "ec-mem-types.h"
#include "ec-helpers.h"
#include "ec-common.h"
#include "ec-fops.h"
#include "ec-method.h"
#include "ec.h"

#define EC_MAX_FRAGMENTS EC_METHOD_MAX_FRAGMENTS
/* The maximum number of nodes is derived from the maximum allowed fragments
 * using the rule that redundancy cannot be equal or greater than the number
 * of fragments.
 */
#define EC_MAX_NODES     (EC_MAX_FRAGMENTS + ((EC_MAX_FRAGMENTS - 1) / 2))

int32_t ec_parse_options(xlator_t * this)
{
    ec_t * ec = this->private;
    int32_t error = EINVAL;
    uintptr_t mask;

    GF_OPTION_INIT("redundancy", ec->redundancy, int32, out);
    ec->fragments = ec->nodes - ec->redundancy;
    if ((ec->redundancy < 1) || (ec->redundancy >= ec->fragments) ||
        (ec->fragments > EC_MAX_FRAGMENTS))
    {
        gf_log(this->name, GF_LOG_ERROR, "Invalid redundancy (must be between "
                                         "1 and %d)", (ec->nodes - 1) / 2);

        goto out;
    }

    ec->bits_for_nodes = 1;
    mask = 2;
    while (ec->nodes > mask)
    {
        ec->bits_for_nodes++;
        mask <<= 1;
    }
    ec->node_mask = (1ULL << ec->nodes) - 1ULL;
    ec->fragment_size = EC_METHOD_CHUNK_SIZE;
    ec->stripe_size = ec->fragment_size * ec->fragments;

    gf_log("ec", GF_LOG_DEBUG, "Initialized with: nodes=%u, fragments=%u, "
                               "stripe_size=%u, node_mask=%lX",
           ec->nodes, ec->fragments, ec->stripe_size, ec->node_mask);

    error = 0;

out:
    return error;
}

int32_t ec_prepare_childs(xlator_t * this)
{
    ec_t * ec = this->private;
    xlator_list_t * child = NULL;
    int32_t count = 0;

    for (child = this->children; child != NULL; child = child->next)
    {
        count++;
    }
    if (count > EC_MAX_NODES)
    {
        gf_log(this->name, GF_LOG_ERROR, "Too many subvolumes");

        return EINVAL;
    }
    ec->nodes = count;

    ec->xl_list = GF_CALLOC(count, sizeof(ec->xl_list[0]), ec_mt_xlator_t);
    if (ec->xl_list == NULL)
    {
        gf_log(this->name, GF_LOG_ERROR, "Allocation of xlator list failed");

        return ENOMEM;
    }
    ec->xl_up = 0;
    ec->xl_up_count = 0;

    count = 0;
    for (child = this->children; child != NULL; child = child->next)
    {
        ec->xl_list[count++] = child->xlator;
    }

    return 0;
}

void __ec_destroy_private(xlator_t * this)
{
    ec_t * ec = this->private;

    if (ec != NULL)
    {
        LOCK(&ec->lock);

        if (ec->timer != NULL)
        {
            gf_timer_call_cancel(this->ctx, ec->timer);
            ec->timer = NULL;
        }

        UNLOCK(&ec->lock);

        /* There is a race with timer because there is no way to know if
         * timer callback has really been cancelled or it has been scheduled
         * for execution. If it has been scheduled, it will crash if we
         * destroy ec too fast.
         *
         * Not sure how this can be solved without using global variables or
         * having support from gf_timer_call_cancel()
         */
        sleep(2);

        this->private = NULL;
        if (ec->xl_list != NULL)
        {
            GF_FREE(ec->xl_list);
            ec->xl_list = NULL;
        }

        if (ec->fop_pool != NULL)
        {
            mem_pool_destroy(ec->fop_pool);
        }

        if (ec->cbk_pool != NULL)
        {
            mem_pool_destroy(ec->cbk_pool);
        }

        if (ec->lock_pool != NULL)
        {
            mem_pool_destroy(ec->lock_pool);
        }

        LOCK_DESTROY(&ec->lock);

        GF_FREE(ec);
    }
}

int32_t mem_acct_init(xlator_t * this)
{
    if (xlator_mem_acct_init(this, ec_mt_end + 1) != 0)
    {
        gf_log(this->name, GF_LOG_ERROR, "Memory accounting initialization "
                                         "failed.");

        return -1;
    }

    return 0;
}

int32_t reconfigure(xlator_t * this, dict_t * options)
{
    gf_log(this->name, GF_LOG_ERROR, "Online volume reconfiguration is not "
                                     "supported.");

    return -1;
}

void ec_up(xlator_t * this, ec_t * ec)
{
    if (ec->timer != NULL)
    {
        gf_timer_call_cancel(this->ctx, ec->timer);
        ec->timer = NULL;
    }

    if (!ec->up && (ec->xl_up_count >= ec->fragments))
    {
        if (ec->xl_up_count < ec->nodes)
        {
            gf_log("ec", GF_LOG_WARNING, "Starting volume with only %d bricks",
                   ec->xl_up_count);
        }

        ec->up = 1;
        gf_log(this->name, GF_LOG_INFO, "Going UP");

        default_notify(this, GF_EVENT_CHILD_UP, NULL);
    }
}

void ec_down(xlator_t * this, ec_t * ec)
{
    if (ec->timer != NULL)
    {
        gf_timer_call_cancel(this->ctx, ec->timer);
        ec->timer = NULL;
    }

    if (ec->up)
    {
        ec->up = 0;
        gf_log(this->name, GF_LOG_INFO, "Going DOWN");

        default_notify(this, GF_EVENT_CHILD_DOWN, NULL);
    }
}

void ec_notify_up_cbk(void * data)
{
    ec_t * ec = data;

    LOCK(&ec->lock);

    if (ec->timer != NULL)
    {
        ec_up(ec->xl, ec);
    }

    UNLOCK(&ec->lock);
}

int32_t ec_notify_up(xlator_t * this, ec_t * ec, int32_t idx)
{
    struct timespec delay = {0, };

    if (((ec->xl_up >> idx) & 1) == 0)
    {
        ec->xl_up |= 1ULL << idx;
        ec->xl_up_count++;

        gf_log("ec", GF_LOG_DEBUG, "Child %d is UP (%lX, %u)", idx, ec->xl_up,
               ec->xl_up_count);

        if (ec->xl_up_count == ec->fragments)
        {
            gf_log("ec", GF_LOG_DEBUG, "Initiating up timer");

            delay.tv_sec = 5;
            delay.tv_nsec = 0;
            ec->timer = gf_timer_call_after(this->ctx, delay, ec_notify_up_cbk,
                                            ec);
            if (ec->timer == NULL)
            {
                gf_log(this->name, GF_LOG_ERROR, "Cannot create timer for "
                                                 "delayed initialization");

                return ENOMEM;
            }
        }
        else if (ec->xl_up_count == ec->nodes)
        {
            ec_up(this, ec);
        }
    }

    return EAGAIN;
}

int32_t ec_notify_down(xlator_t * this, ec_t * ec, int32_t idx)
{
    if (((ec->xl_up >> idx) & 1) != 0)
    {
        gf_log("ec", GF_LOG_DEBUG, "Child %d is DOWN", idx);

        ec->xl_up ^= 1ULL << idx;
        if (ec->xl_up_count-- == ec->fragments)
        {
            ec_down(this, ec);
        }
    }

    return EAGAIN;
}

int32_t notify(xlator_t * this, int32_t event, void * data, ...)
{
    ec_t * ec = this->private;
    int32_t idx = 0;
    int32_t error = 0;

    LOCK(&ec->lock);

    for (idx = 0; idx < ec->nodes; idx++)
    {
        if (ec->xl_list[idx] == data)
        {
            break;
        }
    }

    gf_log("ec", GF_LOG_TRACE, "NOTIFY(%d): %p, %d", event, data, idx);

    if (idx < ec->nodes)
    {
        if (event == GF_EVENT_CHILD_UP)
        {
            error = ec_notify_up(this, ec, idx);
        }
        else if (event == GF_EVENT_CHILD_DOWN)
        {
            error = ec_notify_down(this, ec, idx);
        }
    }

    UNLOCK(&ec->lock);

    if (error == 0)
    {
        return default_notify(this, event, data);
    }

    return 0;
}

int32_t init(xlator_t * this)
{
    ec_t * ec;

    if (this->parents == NULL)
    {
        gf_log(this->name, GF_LOG_WARNING, "Volume does not have parents.");
    }

    ec = GF_MALLOC(sizeof(*ec), ec_mt_ec_t);
    if (ec == NULL)
    {
        gf_log(this->name, GF_LOG_ERROR, "Failed to allocate private memory.");

        return -1;
    }
    memset(ec, 0, sizeof(*ec));

    this->private = ec;

    ec->xl = this;
    LOCK_INIT(&ec->lock);

    ec->fop_pool = mem_pool_new(ec_fop_data_t, 1024);
    ec->cbk_pool = mem_pool_new(ec_cbk_data_t, 4096);
    ec->lock_pool = mem_pool_new(ec_lock_t, 1024);
    if ((ec->fop_pool == NULL) || (ec->cbk_pool == NULL) ||
        (ec->lock_pool == NULL))
    {
        gf_log(this->name, GF_LOG_ERROR, "Failed to create memory pools.");

        goto failed;
    }

    if (ec_prepare_childs(this) != 0)
    {
        gf_log(this->name, GF_LOG_ERROR, "Failed to initialize xlator");

        goto failed;
    }

    if (ec_parse_options(this) != 0)
    {
        gf_log(this->name, GF_LOG_ERROR, "Failed to parse xlator options");

        goto failed;
    }

    ec_method_initialize();

    gf_log(this->name, GF_LOG_DEBUG, "Disperse translator initialized.");

    return 0;

failed:
    __ec_destroy_private(this);

    return -1;
}

void fini(xlator_t * this)
{
    __ec_destroy_private(this);
}

int32_t ec_gf_access(call_frame_t * frame, xlator_t * this, loc_t * loc,
                     int32_t mask, dict_t * xdata)
{
    ec_access(frame, this, -1, EC_MINIMUM_ONE, default_access_cbk, NULL, loc,
              mask, xdata);

    return 0;
}

int32_t ec_gf_create(call_frame_t * frame, xlator_t * this, loc_t * loc,
                     int32_t flags, mode_t mode, mode_t umask, fd_t * fd,
                     dict_t * xdata)
{
    ec_create(frame, this, -1, EC_MINIMUM_MIN, default_create_cbk, NULL, loc,
              flags, mode, umask, fd, xdata);

    return 0;
}

int32_t ec_gf_discard(call_frame_t * frame, xlator_t * this, fd_t * fd,
                      off_t offset, size_t len, dict_t * xdata)
{
    default_discard_failure_cbk(frame, ENOTSUP);

    return 0;
}

int32_t ec_gf_entrylk(call_frame_t * frame, xlator_t * this,
                      const char * volume, loc_t * loc, const char * basename,
                      entrylk_cmd cmd, entrylk_type type, dict_t * xdata)
{
    ec_entrylk(frame, this, -1, EC_MINIMUM_ALL, default_entrylk_cbk, NULL,
               volume, loc, basename, cmd, type, xdata);

    return 0;
}

int32_t ec_gf_fentrylk(call_frame_t * frame, xlator_t * this,
                       const char * volume, fd_t * fd, const char * basename,
                       entrylk_cmd cmd, entrylk_type type, dict_t * xdata)
{
    ec_fentrylk(frame, this, -1, EC_MINIMUM_ALL, default_fentrylk_cbk, NULL,
                volume, fd, basename, cmd, type, xdata);

    return 0;
}

int32_t ec_gf_fallocate(call_frame_t * frame, xlator_t * this, fd_t * fd,
                        int32_t keep_size, off_t offset, size_t len,
                        dict_t * xdata)
{
    default_fallocate_failure_cbk(frame, ENOTSUP);

    return 0;
}

int32_t ec_gf_flush(call_frame_t * frame, xlator_t * this, fd_t * fd,
                    dict_t * xdata)
{
    ec_flush(frame, this, -1, EC_MINIMUM_MIN, default_flush_cbk, NULL, fd,
             xdata);

    return 0;
}

int32_t ec_gf_fsync(call_frame_t * frame, xlator_t * this, fd_t * fd,
                    int32_t datasync, dict_t * xdata)
{
    ec_fsync(frame, this, -1, EC_MINIMUM_MIN, default_fsync_cbk, NULL, fd,
             datasync, xdata);

    return 0;
}

int32_t ec_gf_fsyncdir(call_frame_t * frame, xlator_t * this, fd_t * fd,
                       int32_t datasync, dict_t * xdata)
{
    ec_fsyncdir(frame, this, -1, EC_MINIMUM_MIN, default_fsyncdir_cbk, NULL,
                fd, datasync, xdata);

    return 0;
}

int32_t ec_gf_getxattr(call_frame_t * frame, xlator_t * this, loc_t * loc,
                       const char * name, dict_t * xdata)
{
    ec_getxattr(frame, this, -1, EC_MINIMUM_MIN, default_getxattr_cbk, NULL,
                loc, name, xdata);

    return 0;
}

int32_t ec_gf_fgetxattr(call_frame_t * frame, xlator_t * this, fd_t * fd,
                        const char * name, dict_t * xdata)
{
    ec_fgetxattr(frame, this, -1, EC_MINIMUM_MIN, default_fgetxattr_cbk, NULL,
                 fd, name, xdata);

    return 0;
}

int32_t ec_gf_inodelk(call_frame_t * frame, xlator_t * this,
                      const char * volume, loc_t * loc, int32_t cmd,
                      struct gf_flock * flock, dict_t * xdata)
{
    ec_inodelk(frame, this, -1, EC_MINIMUM_ALL, default_inodelk_cbk, NULL,
               volume, loc, cmd, flock, xdata);

    return 0;
}

int32_t ec_gf_finodelk(call_frame_t * frame, xlator_t * this,
                       const char * volume, fd_t * fd, int32_t cmd,
                       struct gf_flock * flock, dict_t * xdata)
{
    ec_finodelk(frame, this, -1, EC_MINIMUM_ALL, default_finodelk_cbk, NULL,
                volume, fd, cmd, flock, xdata);

    return 0;
}

int32_t ec_gf_link(call_frame_t * frame, xlator_t * this, loc_t * oldloc,
                   loc_t * newloc, dict_t * xdata)
{
    ec_link(frame, this, -1, EC_MINIMUM_MIN, default_link_cbk, NULL, oldloc,
            newloc, xdata);

    return 0;
}

int32_t ec_gf_lk(call_frame_t * frame, xlator_t * this, fd_t * fd,
                 int32_t cmd, struct gf_flock * flock, dict_t * xdata)
{
    ec_lk(frame, this, -1, EC_MINIMUM_ALL, default_lk_cbk, NULL, fd, cmd,
          flock, xdata);

    return 0;
}

int32_t ec_gf_lookup(call_frame_t * frame, xlator_t * this, loc_t * loc,
                     dict_t * xdata)
{
    ec_lookup(frame, this, -1, EC_MINIMUM_MIN, default_lookup_cbk, NULL, loc,
              xdata);

    return 0;
}

int32_t ec_gf_mkdir(call_frame_t * frame, xlator_t * this, loc_t * loc,
                    mode_t mode, mode_t umask, dict_t * xdata)
{
    ec_mkdir(frame, this, -1, EC_MINIMUM_MIN, default_mkdir_cbk, NULL, loc,
             mode, umask, xdata);

    return 0;
}

int32_t ec_gf_mknod(call_frame_t * frame, xlator_t * this, loc_t * loc,
                    mode_t mode, dev_t rdev, mode_t umask, dict_t * xdata)
{
    ec_mknod(frame, this, -1, EC_MINIMUM_MIN, default_mknod_cbk, NULL, loc,
             mode, rdev, umask, xdata);

    return 0;
}

int32_t ec_gf_open(call_frame_t * frame, xlator_t * this, loc_t * loc,
                   int32_t flags, fd_t * fd, dict_t * xdata)
{
    ec_open(frame, this, -1, EC_MINIMUM_MIN, default_open_cbk, NULL, loc,
            flags, fd, xdata);

    return 0;
}

int32_t ec_gf_opendir(call_frame_t * frame, xlator_t * this, loc_t * loc,
                      fd_t * fd, dict_t * xdata)
{
    ec_opendir(frame, this, -1, EC_MINIMUM_MIN, default_opendir_cbk, NULL, loc,
               fd, xdata);

    return 0;
}

int32_t ec_gf_readdir(call_frame_t * frame, xlator_t * this, fd_t * fd,
                      size_t size, off_t offset, dict_t * xdata)
{
    ec_readdir(frame, this, -1, EC_MINIMUM_ONE, default_readdir_cbk, NULL, fd,
               size, offset, xdata);

    return 0;
}

int32_t ec_gf_readdirp(call_frame_t * frame, xlator_t * this, fd_t * fd,
                       size_t size, off_t offset, dict_t * xdata)
{
    ec_readdirp(frame, this, -1, EC_MINIMUM_ONE, default_readdirp_cbk, NULL,
                fd, size, offset, xdata);

    return 0;
}

int32_t ec_gf_readlink(call_frame_t * frame, xlator_t * this, loc_t * loc,
                       size_t size, dict_t * xdata)
{
    ec_readlink(frame, this, -1, EC_MINIMUM_ONE, default_readlink_cbk, NULL,
                loc, size, xdata);

    return 0;
}

int32_t ec_gf_readv(call_frame_t * frame, xlator_t * this, fd_t * fd,
                    size_t size, off_t offset, uint32_t flags, dict_t * xdata)
{
    ec_readv(frame, this, -1, EC_MINIMUM_MIN, default_readv_cbk, NULL, fd,
             size, offset, flags, xdata);

    return 0;
}

int32_t ec_gf_removexattr(call_frame_t * frame, xlator_t * this, loc_t * loc,
                          const char * name, dict_t * xdata)
{
    ec_removexattr(frame, this, -1, EC_MINIMUM_MIN, default_removexattr_cbk,
                   NULL, loc, name, xdata);

    return 0;
}

int32_t ec_gf_fremovexattr(call_frame_t * frame, xlator_t * this, fd_t * fd,
                           const char * name, dict_t * xdata)
{
    ec_fremovexattr(frame, this, -1, EC_MINIMUM_MIN, default_fremovexattr_cbk,
                    NULL, fd, name, xdata);

    return 0;
}

int32_t ec_gf_rename(call_frame_t * frame, xlator_t * this, loc_t * oldloc,
                     loc_t * newloc, dict_t * xdata)
{
    ec_rename(frame, this, -1, EC_MINIMUM_MIN, default_rename_cbk, NULL,
              oldloc, newloc, xdata);

    return 0;
}

int32_t ec_gf_rmdir(call_frame_t * frame, xlator_t * this, loc_t * loc,
                    int xflags, dict_t * xdata)
{
    ec_rmdir(frame, this, -1, EC_MINIMUM_MIN, default_rmdir_cbk, NULL, loc,
             xflags, xdata);

    return 0;
}

int32_t ec_gf_setattr(call_frame_t * frame, xlator_t * this, loc_t * loc,
                      struct iatt * stbuf, int32_t valid, dict_t * xdata)
{
    ec_setattr(frame, this, -1, EC_MINIMUM_MIN, default_setattr_cbk, NULL, loc,
               stbuf, valid, xdata);

    return 0;
}

int32_t ec_gf_fsetattr(call_frame_t * frame, xlator_t * this, fd_t * fd,
                       struct iatt * stbuf, int32_t valid, dict_t * xdata)
{
    ec_fsetattr(frame, this, -1, EC_MINIMUM_MIN, default_fsetattr_cbk, NULL,
                fd, stbuf, valid, xdata);

    return 0;
}

int32_t ec_gf_setxattr(call_frame_t * frame, xlator_t * this, loc_t * loc,
                       dict_t * dict, int32_t flags, dict_t * xdata)
{
    ec_setxattr(frame, this, -1, EC_MINIMUM_MIN, default_setxattr_cbk, NULL,
                loc, dict, flags, xdata);

    return 0;
}

int32_t ec_gf_fsetxattr(call_frame_t * frame, xlator_t * this, fd_t * fd,
                        dict_t * dict, int32_t flags, dict_t * xdata)
{
    ec_fsetxattr(frame, this, -1, EC_MINIMUM_MIN, default_fsetxattr_cbk, NULL,
                 fd, dict, flags, xdata);

    return 0;
}

int32_t ec_gf_stat(call_frame_t * frame, xlator_t * this, loc_t * loc,
                   dict_t * xdata)
{
    ec_stat(frame, this, -1, EC_MINIMUM_MIN, default_stat_cbk, NULL, loc,
            xdata);

    return 0;
}

int32_t ec_gf_fstat(call_frame_t * frame, xlator_t * this, fd_t * fd,
                    dict_t * xdata)
{
    ec_fstat(frame, this, -1, EC_MINIMUM_MIN, default_fstat_cbk, NULL, fd,
             xdata);

    return 0;
}

int32_t ec_gf_statfs(call_frame_t * frame, xlator_t * this, loc_t * loc,
                     dict_t * xdata)
{
    ec_statfs(frame, this, -1, EC_MINIMUM_MIN, default_statfs_cbk, NULL, loc,
              xdata);

    return 0;
}

int32_t ec_gf_symlink(call_frame_t * frame, xlator_t * this,
                      const char * linkname, loc_t * loc, mode_t umask,
                      dict_t * xdata)
{
    ec_symlink(frame, this, -1, EC_MINIMUM_MIN, default_symlink_cbk, NULL,
               linkname, loc, umask, xdata);

    return 0;
}

int32_t ec_gf_truncate(call_frame_t * frame, xlator_t * this, loc_t * loc,
                       off_t offset, dict_t * xdata)
{
    ec_truncate(frame, this, -1, EC_MINIMUM_MIN, default_truncate_cbk, NULL,
                loc, offset, xdata);

    return 0;
}

int32_t ec_gf_ftruncate(call_frame_t * frame, xlator_t * this, fd_t * fd,
                        off_t offset, dict_t * xdata)
{
    ec_ftruncate(frame, this, -1, EC_MINIMUM_MIN, default_ftruncate_cbk, NULL,
                 fd, offset, xdata);

    return 0;
}

int32_t ec_gf_unlink(call_frame_t * frame, xlator_t * this, loc_t * loc,
                     int xflags, dict_t * xdata)
{
    ec_unlink(frame, this, -1, EC_MINIMUM_MIN, default_unlink_cbk, NULL, loc,
              xflags, xdata);

    return 0;
}

int32_t ec_gf_writev(call_frame_t * frame, xlator_t * this, fd_t * fd,
                     struct iovec * vector, int32_t count, off_t offset,
                     uint32_t flags, struct iobref * iobref, dict_t * xdata)
{
    ec_writev(frame, this, -1, EC_MINIMUM_MIN, default_writev_cbk, NULL, fd,
              vector, count, offset, flags, iobref, xdata);

    return 0;
}

int32_t ec_gf_xattrop(call_frame_t * frame, xlator_t * this, loc_t * loc,
                      gf_xattrop_flags_t optype, dict_t * xattr,
                      dict_t * xdata)
{
    ec_xattrop(frame, this, -1, EC_MINIMUM_MIN, default_xattrop_cbk, NULL, loc,
               optype, xattr, xdata);

    return 0;
}

int32_t ec_gf_fxattrop(call_frame_t * frame, xlator_t * this, fd_t * fd,
                       gf_xattrop_flags_t optype, dict_t * xattr,
                       dict_t * xdata)
{
    ec_fxattrop(frame, this, -1, EC_MINIMUM_MIN, default_fxattrop_cbk, NULL,
                fd, optype, xattr, xdata);

    return 0;
}

int32_t ec_gf_zerofill(call_frame_t * frame, xlator_t * this, fd_t * fd,
                       off_t offset, off_t len, dict_t * xdata)
{
    default_zerofill_failure_cbk(frame, ENOTSUP);

    return 0;
}

int32_t ec_gf_forget(xlator_t * this, inode_t * inode)
{
    uint64_t value = 0;
    ec_inode_t * ctx = NULL;

    if ((inode_ctx_del(inode, this, &value) == 0) && (value != 0))
    {
        ctx = (ec_inode_t *)(uintptr_t)value;
        GF_FREE(ctx);
    }

    return 0;
}

void ec_gf_release_fd(xlator_t * this, fd_t * fd)
{
    uint64_t value = 0;
    ec_fd_t * ctx = NULL;

    if ((fd_ctx_del(fd, this, &value) == 0) && (value != 0))
    {
        ctx = (ec_fd_t *)(uintptr_t)value;
        loc_wipe(&ctx->loc);
        GF_FREE(ctx);
    }
}

int32_t ec_gf_release(xlator_t * this, fd_t * fd)
{
    ec_gf_release_fd(this, fd);

    return 0;
}

int32_t ec_gf_releasedir(xlator_t * this, fd_t * fd)
{
    ec_gf_release_fd(this, fd);

    return 0;
}

int32_t ec_dump_private(xlator_t *this)
{
    ec_t *ec = NULL;
    char  key_prefix[GF_DUMP_MAX_BUF_LEN];
    char  tmp[65];

    GF_ASSERT(this);

    ec = this->private;
    GF_ASSERT(ec);

    snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type, this->name);
    gf_proc_dump_add_section(key_prefix);
    gf_proc_dump_write("nodes", "%u", ec->nodes);
    gf_proc_dump_write("redundancy", "%u", ec->redundancy);
    gf_proc_dump_write("fragment_size", "%u", ec->fragment_size);
    gf_proc_dump_write("stripe_size", "%u", ec->stripe_size);
    gf_proc_dump_write("childs_up", "%u", ec->xl_up_count);
    gf_proc_dump_write("childs_up_mask", "%s",
                       ec_bin(tmp, sizeof(tmp), ec->xl_up, ec->nodes));

    return 0;
}

struct xlator_fops fops =
{
    .lookup       = ec_gf_lookup,
    .stat         = ec_gf_stat,
    .fstat        = ec_gf_fstat,
    .truncate     = ec_gf_truncate,
    .ftruncate    = ec_gf_ftruncate,
    .access       = ec_gf_access,
    .readlink     = ec_gf_readlink,
    .mknod        = ec_gf_mknod,
    .mkdir        = ec_gf_mkdir,
    .unlink       = ec_gf_unlink,
    .rmdir        = ec_gf_rmdir,
    .symlink      = ec_gf_symlink,
    .rename       = ec_gf_rename,
    .link         = ec_gf_link,
    .create       = ec_gf_create,
    .open         = ec_gf_open,
    .readv        = ec_gf_readv,
    .writev       = ec_gf_writev,
    .flush        = ec_gf_flush,
    .fsync        = ec_gf_fsync,
    .opendir      = ec_gf_opendir,
    .readdir      = ec_gf_readdir,
    .readdirp     = ec_gf_readdirp,
    .fsyncdir     = ec_gf_fsyncdir,
    .statfs       = ec_gf_statfs,
    .setxattr     = ec_gf_setxattr,
    .getxattr     = ec_gf_getxattr,
    .fsetxattr    = ec_gf_fsetxattr,
    .fgetxattr    = ec_gf_fgetxattr,
    .removexattr  = ec_gf_removexattr,
    .fremovexattr = ec_gf_fremovexattr,
    .lk           = ec_gf_lk,
    .inodelk      = ec_gf_inodelk,
    .finodelk     = ec_gf_finodelk,
    .entrylk      = ec_gf_entrylk,
    .fentrylk     = ec_gf_fentrylk,
    .xattrop      = ec_gf_xattrop,
    .fxattrop     = ec_gf_fxattrop,
    .setattr      = ec_gf_setattr,
    .fsetattr     = ec_gf_fsetattr,
    .fallocate    = ec_gf_fallocate,
    .discard      = ec_gf_discard,
    .zerofill     = ec_gf_zerofill
};

struct xlator_cbks cbks =
{
    .forget            = ec_gf_forget,
    .release           = ec_gf_release,
    .releasedir        = ec_gf_releasedir
};

struct xlator_dumpops dumpops = {
    .priv = ec_dump_private
};

struct volume_options options[] =
{
    {
        .key = { "redundancy" },
        .type = GF_OPTION_TYPE_INT,
        .description = "Maximum number of bricks that can fail "
                       "simultaneously without losing data."
    },
    { }
};
