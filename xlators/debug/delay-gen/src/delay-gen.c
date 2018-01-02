/*
 *  Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
 *  This file is part of GlusterFS.
 *
 *  This file is licensed to you under your choice of the GNU Lesser
 *  General Public License, version 3 or any later version (LGPLv3 or
 *  later), or the GNU General Public License, version 2 (GPLv2), in all
 *  cases as published by the Free Software Foundation.
 */


#include "delay-gen.h"

#define DELAY_GRANULARITY     (1 << 20)

#define DG_FOP(fop, name, frame, this, args...)                                \
        do {                                                                   \
                delay_gen (this, fop);                                         \
                default_##name (frame, this, args);                            \
        } while (0)

int
delay_gen (xlator_t *this, int fop)
{
        dg_t             *dg = this->private;

        if (!dg->enable[fop] || !dg->delay_ppm)
                return 0;

        if ((rand () % DELAY_GRANULARITY) < dg->delay_ppm)
                usleep (dg->delay_duration);

        return 0;
}

int32_t
dg_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
           dict_t *xdata)
{
        DG_FOP (GF_FOP_RENAME, rename, frame, this,  oldloc,  newloc, xdata);
        return 0;
}


int32_t
dg_ipc (call_frame_t *frame, xlator_t *this, int32_t op, dict_t *xdata)
{
        DG_FOP (GF_FOP_IPC, ipc, frame, this, op, xdata);
        return 0;
}

int32_t
dg_setactivelk (call_frame_t *frame, xlator_t *this, loc_t *loc,
                lock_migration_info_t *locklist, dict_t *xdata)
{
        DG_FOP (GF_FOP_SETACTIVELK, setactivelk, frame, this, loc,
                locklist, xdata);
        return 0;
}

int32_t
dg_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        DG_FOP (GF_FOP_FLUSH, flush, frame, this, fd, xdata);
        return 0;
}

int32_t
dg_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
            off_t off, dict_t *xdata)
{
        DG_FOP (GF_FOP_READDIR, readdir, frame, this,  fd, size, off, xdata);
        return 0;
}

int32_t
dg_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
             int32_t flags, dict_t *xdata)
{
        DG_FOP (GF_FOP_SETXATTR, setxattr, frame, this, loc, dict, flags,
                xdata);
        return 0;
}

int32_t
dg_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dev_t rdev, mode_t umask, dict_t *xdata)
{
        DG_FOP (GF_FOP_MKNOD, mknod, frame, this, loc, mode, rdev, umask,
                xdata);
        return 0;
}

int32_t
dg_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
              int32_t flags, dict_t *xdata)
{
        DG_FOP (GF_FOP_FSETXATTR, fsetxattr, frame, this, fd, dict, flags,
                xdata);
        return 0;
}

int32_t
dg_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset, uint32_t flags, dict_t *xdata)
{
        DG_FOP (GF_FOP_READ, readv, frame, this, fd, size, offset, flags,
                xdata);
        return 0;
}

int32_t
dg_inodelk (call_frame_t *frame, xlator_t *this, const char *volume,
            loc_t *loc, int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        DG_FOP (GF_FOP_INODELK, inodelk, frame, this, volume, loc, cmd, lock,
                xdata);
        return 0;
}

int32_t
dg_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 const char *name, dict_t *xdata)
{
        DG_FOP (GF_FOP_FREMOVEXATTR, fremovexattr, frame, this, fd, name,
                xdata);
        return 0;
}

int32_t
dg_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, dict_t *xdata)
{
        DG_FOP (GF_FOP_OPEN, open, frame, this, loc, flags, fd, xdata);
        return 0;
}

int32_t
dg_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
            gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        DG_FOP (GF_FOP_XATTROP, xattrop, frame, this, loc, flags, dict, xdata);
        return 0;
}

int32_t
dg_entrylk (call_frame_t *frame, xlator_t *this, const char *volume,
            loc_t *loc, const char *basename, entrylk_cmd cmd,
            entrylk_type type, dict_t *xdata)
{
        DG_FOP (GF_FOP_ENTRYLK, entrylk, frame, this, volume,
                loc, basename, cmd, type, xdata);
        return 0;
}

int32_t
dg_getactivelk (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        DG_FOP (GF_FOP_GETACTIVELK, getactivelk, frame, this, loc, xdata);
        return 0;
}

int32_t
dg_finodelk (call_frame_t *frame, xlator_t *this, const char *volume,
             fd_t *fd, int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        DG_FOP (GF_FOP_FINODELK, finodelk, frame, this, volume, fd, cmd, lock,
                xdata);
        return 0;
}

int32_t
dg_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        DG_FOP (GF_FOP_CREATE, create, frame, this, loc, flags, mode, umask, fd,
                xdata);
        return 0;
}

int32_t
dg_discard (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            size_t len, dict_t *xdata)
{
        DG_FOP (GF_FOP_DISCARD, discard, frame, this, fd, offset, len, xdata);
        return 0;
}

int32_t
dg_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          mode_t umask, dict_t *xdata)
{
        DG_FOP (GF_FOP_MKDIR, mkdir, frame, this, loc, mode, umask, xdata);
        return 0;
}

int32_t
dg_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
       struct gf_flock *lock, dict_t *xdata)
{
        DG_FOP (GF_FOP_LK, lk, frame, this, fd, cmd, lock, xdata);
        return 0;
}

int32_t
dg_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
           struct iovec *vector, int32_t count, off_t off, uint32_t flags,
           struct iobref *iobref, dict_t *xdata)
{
        DG_FOP (GF_FOP_WRITE, writev, frame, this, fd,
                vector, count, off, flags, iobref, xdata);
        return 0;
}

int32_t
dg_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
           dict_t *xdata)
{
        DG_FOP (GF_FOP_ACCESS, access, frame, this, loc, mask, xdata);
        return 0;
}

int32_t
dg_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        DG_FOP (GF_FOP_LOOKUP, lookup, frame, this, loc, xdata);
        return 0;
}



int32_t
dg_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc,
          int32_t flags, dict_t *xdata)
{
        DG_FOP (GF_FOP_RMDIR, rmdir, frame, this, loc, flags, xdata);
        return 0;
}



int32_t
dg_fallocate (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t keep_size,
              off_t offset, size_t len, dict_t *xdata)
{
        DG_FOP (GF_FOP_FALLOCATE, fallocate, frame, this, fd, keep_size, offset,
                len, xdata);
        return 0;
}



int32_t
dg_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        DG_FOP (GF_FOP_FSTAT, fstat, frame, this, fd, xdata);
        return 0;
}



int32_t
dg_lease (call_frame_t *frame, xlator_t *this, loc_t *loc,
          struct gf_lease *lease, dict_t *xdata)
{
        DG_FOP (GF_FOP_LEASE, lease, frame, this, loc, lease, xdata);
        return 0;
}



int32_t
dg_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        DG_FOP (GF_FOP_STAT, stat, frame, this, loc, xdata);
        return 0;
}



int32_t
dg_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
             dict_t *xdata)
{
        DG_FOP (GF_FOP_TRUNCATE, truncate, frame, this, loc, offset, xdata);
        return 0;
}



int32_t
dg_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             const char *name, dict_t *xdata)
{
        DG_FOP (GF_FOP_GETXATTR, getxattr, frame, this, loc, name, xdata);
        return 0;
}



int32_t
dg_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
            loc_t *loc, mode_t umask, dict_t *xdata)
{
        DG_FOP (GF_FOP_SYMLINK, symlink, frame, this, linkpath, loc, umask,
                xdata);
        return 0;
}



int32_t
dg_zerofill (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             off_t len, dict_t *xdata)
{
        DG_FOP (GF_FOP_ZEROFILL, zerofill, frame, this, fd, offset, len, xdata);
        return 0;
}



int32_t
dg_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
             dict_t *xdata)
{
        DG_FOP (GF_FOP_FSYNCDIR, fsyncdir, frame, this, fd, flags, xdata);
        return 0;
}



int32_t
dg_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
              dict_t *xdata)
{
        DG_FOP (GF_FOP_FGETXATTR, fgetxattr, frame, this, fd, name, xdata);
        return 0;
}



int32_t
dg_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t off, dict_t *xdata)
{
        DG_FOP (GF_FOP_READDIRP, readdirp, frame, this, fd, size, off,  xdata);
        return 0;
}



int32_t
dg_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
         dict_t *xdata)
{
        DG_FOP (GF_FOP_LINK, link, frame, this, oldloc, newloc, xdata);
        return 0;
}



int32_t
dg_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
             gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        DG_FOP (GF_FOP_FXATTROP, fxattrop, frame, this, fd, flags, dict, xdata);
        return 0;
}



int32_t
dg_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              dict_t *xdata)
{
        DG_FOP (GF_FOP_FTRUNCATE, ftruncate, frame, this, fd, offset,  xdata);
        return 0;
}



int32_t
dg_rchecksum (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              int32_t len, dict_t *xdata)
{
        DG_FOP (GF_FOP_RCHECKSUM, rchecksum, frame, this, fd, offset, len,
                xdata);
        return 0;
}



int32_t
dg_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           dict_t *xdata)
{
        DG_FOP (GF_FOP_UNLINK, unlink, frame, this, loc, flags, xdata);
        return 0;
}



int32_t
dg_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume,
             fd_t *fd, const char *basename, entrylk_cmd cmd,
             entrylk_type type, dict_t *xdata)
{
        DG_FOP (GF_FOP_FENTRYLK, fentrylk, frame, this, volume,  fd, basename,
                cmd, type, xdata);
        return 0;
}



int32_t
dg_getspec (call_frame_t *frame, xlator_t *this, const char *key,
            int32_t flags)
{
        DG_FOP (GF_FOP_GETSPEC, getspec, frame, this, key, flags);
        return 0;
}



int32_t
dg_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
            struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        DG_FOP (GF_FOP_SETATTR, setattr, frame, this, loc, stbuf, valid, xdata);
        return 0;
}



int32_t
dg_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
          dict_t *xdata)
{
        DG_FOP (GF_FOP_FSYNC, fsync, frame, this,  fd, flags, xdata);
        return 0;
}



int32_t
dg_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        DG_FOP (GF_FOP_STATFS, statfs, frame, this, loc, xdata);
        return 0;
}



int32_t
dg_seek (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
         gf_seek_what_t what, dict_t *xdata)
{
        DG_FOP (GF_FOP_SEEK, seek, frame, this, fd, offset, what, xdata);
        return 0;
}



int32_t
dg_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
             struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        DG_FOP (GF_FOP_FSETATTR, fsetattr, frame, this, fd,
                stbuf, valid, xdata);
        return 0;
}



int32_t
dg_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
            dict_t *xdata)
{
        DG_FOP (GF_FOP_OPENDIR, opendir, frame, this, loc, fd, xdata);
        return 0;
}



int32_t
dg_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
             dict_t *xdata)
{
        DG_FOP (GF_FOP_READLINK, readlink, frame, this, loc, size, xdata);
        return 0;
}



int32_t
dg_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata)
{
        DG_FOP (GF_FOP_REMOVEXATTR, removexattr, frame, this, loc, name, xdata);
        return 0;
}

int32_t
dg_forget (xlator_t *this, inode_t *inode)
{
        return 0;
}

int32_t
dg_release (xlator_t *this, fd_t *fd)
{
        return 0;
}

int32_t
dg_releasedir (xlator_t *this, fd_t *fd)
{
        return 0;
}

static int
delay_gen_parse_fill_fops (dg_t *dg, char *enable_fops)
{
        char            *op_no_str = NULL;
        int              op_no = -1;
        int              i = 0;
        int              ret = 0;
        xlator_t        *this = THIS;
        char            *saveptr = NULL;
        char            *dup_enable_fops = NULL;

        if (strlen (enable_fops) == 0) {
                for (i = GF_FOP_NULL + 1; i < GF_FOP_MAXVALUE; i++)
                        dg->enable[i] = 1;
        } else {
                dup_enable_fops = gf_strdup (enable_fops);
                if (!dup_enable_fops) {
                        ret = -1;
                        goto out;
                }
                op_no_str = strtok_r (dup_enable_fops, ",", &saveptr);
                while (op_no_str) {
                        op_no = gf_fop_int (op_no_str);
                        if (op_no == -1) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Wrong option value %s", op_no_str);
                                ret = -1;
                                goto out;
                        } else {
                                dg->enable[op_no] = 1;
                        }

                        op_no_str = strtok_r (NULL, ",", &saveptr);
                }
        }
out:
        GF_FREE (dup_enable_fops);
        return ret;
}

void
delay_gen_set_delay_ppm (dg_t *dg, double percent)
{
        double ppm;

        ppm = (percent / 100.0) * (double) DELAY_GRANULARITY;
        dg->delay_ppm = ppm;
}

int32_t
init (xlator_t *this)
{
        dg_t            *dg = NULL;
        int32_t          ret = 0;
        double          delay_percent = 0;
        char            *delay_enable_fops = NULL;

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "delay-gen not configured with one subvolume");
                ret = -1;
                goto out;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile ");
        }

        dg = GF_CALLOC (1, sizeof (*dg), gf_delay_gen_mt_dg_t);

        if (!dg) {
                ret = -1;
                goto out;
        }

        ret = -1;

        GF_OPTION_INIT ("delay-percentage", delay_percent, percent, out);
        GF_OPTION_INIT ("enable", delay_enable_fops, str, out);
        GF_OPTION_INIT ("delay-duration", dg->delay_duration, int32, out);

        delay_gen_set_delay_ppm (dg, delay_percent);

        ret = delay_gen_parse_fill_fops (dg, delay_enable_fops);
        if (ret)
                goto out;

        this->private = dg;

        ret = 0;
out:
        if (ret)
                GF_FREE (dg);
        return ret;
}

void
fini (xlator_t *this)
{
        GF_FREE (this->private);
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_delay_gen_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        " failed");
                return ret;
        }

        return ret;
}

int32_t
reconfigure (xlator_t *this, dict_t *dict)
{
        /*At the moment I don't see any need to implement this. In future
         *if this is needed we can add code here.
         */
        return 0;
}

int
notify (xlator_t *this, int event, void *data, ...)
{
        return default_notify (this, event, data);
}


struct xlator_fops fops = {
        .rename               = dg_rename,
        .ipc                  = dg_ipc,
        .setactivelk          = dg_setactivelk,
        .flush                = dg_flush,
        .readdir              = dg_readdir,
        .setxattr             = dg_setxattr,
        .mknod                = dg_mknod,
        .fsetxattr            = dg_fsetxattr,
        .readv                = dg_readv,
        .inodelk              = dg_inodelk,
        .fremovexattr         = dg_fremovexattr,
        .open                 = dg_open,
        .xattrop              = dg_xattrop,
        .entrylk              = dg_entrylk,
        .getactivelk          = dg_getactivelk,
        .finodelk             = dg_finodelk,
        .create               = dg_create,
        .discard              = dg_discard,
        .mkdir                = dg_mkdir,
        .lk                   = dg_lk,
        .writev               = dg_writev,
        .access               = dg_access,
        .lookup               = dg_lookup,
        .rmdir                = dg_rmdir,
        .fallocate            = dg_fallocate,
        .fstat                = dg_fstat,
        .lease                = dg_lease,
        .stat                 = dg_stat,
        .truncate             = dg_truncate,
        .getxattr             = dg_getxattr,
        .symlink              = dg_symlink,
        .zerofill             = dg_zerofill,
        .fsyncdir             = dg_fsyncdir,
        .fgetxattr            = dg_fgetxattr,
        .readdirp             = dg_readdirp,
        .link                 = dg_link,
        .fxattrop             = dg_fxattrop,
        .ftruncate            = dg_ftruncate,
        .rchecksum            = dg_rchecksum,
        .unlink               = dg_unlink,
        .fentrylk             = dg_fentrylk,
        .getspec              = dg_getspec,
        .setattr              = dg_setattr,
        .fsync                = dg_fsync,
        .statfs               = dg_statfs,
        .seek                 = dg_seek,
        .fsetattr             = dg_fsetattr,
        .opendir              = dg_opendir,
        .readlink             = dg_readlink,
        .removexattr          = dg_removexattr,
};

struct xlator_cbks cbks = {
        .forget               = dg_forget,
        .release              = dg_release,
        .releasedir           = dg_releasedir,
};

struct volume_options options[] = {
        { .key  = {"delay-percentage"},
          .type = GF_OPTION_TYPE_PERCENT,
          .default_value = "10%",
          .description = "Percentage delay of operations when enabled.",
          .op_version  = {GD_OP_VERSION_3_13_0},
          .flags       = OPT_FLAG_SETTABLE,
          .tags        = {"delay-gen"},
        },

        { .key  = {"delay-duration"},
          .type = GF_OPTION_TYPE_INT,
          .description = "Delay duration in micro seconds",
          .default_value = "100000",
          .op_version = {GD_OP_VERSION_3_13_0},
          .flags = OPT_FLAG_SETTABLE,
          .tags  = {"delay-gen"},
        },

        { .key  = {"enable"},
          .type = GF_OPTION_TYPE_STR,
          .description = "Accepts a string which takes ',' separated fop "
                         "strings to denote which fops are enabled for delay",
          .op_version = {GD_OP_VERSION_3_13_0},
          .flags = OPT_FLAG_SETTABLE,
          .tags  = {"delay-gen"},
          .default_value = "",
        },

        { .key  = {NULL} }
};
