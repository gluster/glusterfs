/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "thin-arbiter.h"
#include "thin-arbiter-messages.h"
#include "thin-arbiter-mem-types.h"
#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "byte-order.h"
#include "common-utils.h"

int
ta_set_incoming_values (dict_t *dict, char *key,
                        data_t *value, void *data)
{
    int32_t     ret = 0;
    ta_fop_t    *fop = (ta_fop_t *)data;
    int32_t     *pending = NULL;

    pending = GF_CALLOC (1, value->len, gf_ta_mt_char);
    if (!pending) {
        ret = -ENOMEM;
        goto out;
    }
    ret = dict_set_bin (fop->brick_xattr, key, pending, value->len);
out:
    return ret;
}

int
ta_get_incoming_and_brick_values (dict_t *dict, char *key,
                                  data_t *value, void *data)
{
    ta_fop_t    *fop    = data;
    char        *source = NULL;
    char        *in_coming = NULL;
    int32_t     len = 0, ret = 0;

    source = GF_CALLOC (1, value->len, gf_ta_mt_char);
    if (!source) {
        ret = -ENOMEM;
        goto out;
    }

    ret = dict_get_ptr_and_len (fop->dict, key,
                                (void **)&in_coming, &len);

    if (!in_coming || value->len != len) {
        ret = -EINVAL;
        goto out;
    }

    if (!memcmp(value->data, source, value->len) &&
        (!memcmp(in_coming, source, len))) {
        fop->on_disk[fop->idx] = 0;
    } else {
        fop->on_disk[fop->idx] = 1;
    }

    fop->idx++;
out:
    GF_FREE (source);
    return ret;
}

void
ta_release_fop (ta_fop_t *fop)
{
    if (!fop) {
        return;
    }
    if (fop->fd) {
        fd_unref(fop->fd);
    }
    loc_wipe (&fop->loc);
    if (fop->dict) {
        dict_unref (fop->dict);
    }
    if (fop->brick_xattr) {
        dict_unref (fop->brick_xattr);
    }

    GF_FREE (fop);
    return;
}

int32_t
ta_set_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *dict,
                    dict_t *xdata)
{
    TA_STACK_UNWIND(xattrop, frame, op_ret, op_errno, dict, xdata);
    return 0;
}

/*
case 1 - If brick value is 0 and incoming value is also 0, fine
case 2 - If brick value is 0 and incoming value is non 0, fine
case 3 - If brick value is non 0 and incoming value is also 0, fine
case 4 - If brick value is non 0 and incoming value is non 0, fine
case 5 - If incoming value is non zero on both brick, it is wrong
case 6 - If incoming value is non zero but brick value for other
brick is also non zero, wrong
*/

int32_t
ta_verify_on_disk_source (ta_fop_t *fop, dict_t *dict)
{
    int ret = 0;

    if (!fop) {
        return -EINVAL;
    }

    ret = dict_foreach (dict, ta_get_incoming_and_brick_values,
                  (void *) fop);
    if (ret < 0) {
        return ret;
    }
    if (fop->on_disk[0] && fop->on_disk[1]) {
        return -EINVAL;
    }
    return 0;
}

int32_t
ta_get_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *dict,
                    dict_t *xdata)
{
    ta_fop_t    *fop    = NULL;
    int         ret     = 0;

    fop = frame->local;
    if (op_ret) {
        goto unwind;
    }

    ret = ta_verify_on_disk_source (fop, dict);
    if (ret < 0) {
        op_errno = -ret;
        goto unwind;
    }

    if (fop->fd) {
        STACK_WIND (frame, ta_set_xattrop_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->fxattrop, fop->fd,
                    fop->xattrop_flags, fop->dict, NULL);
    } else {
        STACK_WIND (frame, ta_set_xattrop_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->xattrop, &fop->loc,
                    fop->xattrop_flags, fop->dict, NULL);
    }
    return 0;

unwind:

    TA_STACK_UNWIND(xattrop, frame, -1, op_errno, NULL, NULL);
    return -1;
}

ta_fop_t *
ta_prepare_fop (call_frame_t *frame, xlator_t *this,
                loc_t *loc, fd_t *fd, gf_xattrop_flags_t flags,
                dict_t *dict, dict_t *xdata)
{
    ta_fop_t    *fop   = NULL;
    int         ret    = 0;

    fop = GF_CALLOC (1, sizeof(*fop), gf_ta_mt_local_t);
    if (!fop) {
        goto out;
    }

    if (loc) {
        loc_copy(&fop->loc, loc);
    }

    if (fd) {
        fop->fd = fd_ref (fd);
    }

    fop->xattrop_flags = flags;
    fop->idx = 0;

    if (dict != NULL) {
        fop->dict = dict_ref (dict);
    }
    fop->brick_xattr = dict_new();
    if (fop->brick_xattr == NULL) {
            goto out;
    }
    ret = dict_foreach (dict, ta_set_incoming_values,
                       (void *) fop);
    if (ret < 0) {
        goto out;
    }
    frame->local = fop;
    return fop;

out:
    ta_release_fop (fop);
    return NULL;
}

int32_t
ta_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
             gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
    int      ret  = 0;
    ta_fop_t *fop = NULL;

    fop = ta_prepare_fop (frame, this, NULL, fd, flags, dict, xdata);
    if (!fop) {
        ret = -ENOMEM;
        goto unwind;
    }

    STACK_WIND (frame, ta_get_xattrop_cbk, FIRST_CHILD (this),
                FIRST_CHILD(this)->fops->fxattrop, fd,
                flags, fop->brick_xattr, xdata);
    return 0;

unwind:

    TA_STACK_UNWIND(xattrop, frame, -1, -ret, NULL, NULL);
    return 0;
}

int32_t
ta_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
            gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
    int         ret    = 0;
    ta_fop_t    *fop   = NULL;

    fop = ta_prepare_fop (frame, this, loc, NULL, flags,
                          dict, xdata);
    if (!fop) {
        ret = -ENOMEM;
        goto unwind;
    }

    STACK_WIND (frame, ta_get_xattrop_cbk, FIRST_CHILD (this),
                FIRST_CHILD(this)->fops->xattrop, loc,
                flags, fop->brick_xattr, xdata);
    return 0;

unwind:

    TA_STACK_UNWIND(xattrop, frame, -1, -ret, NULL, NULL);
    return 0;
}

int32_t
ta_writev (call_frame_t *frame, xlator_t *this,
           fd_t *fd, struct iovec *vector, int32_t count,
           off_t off, uint32_t flags, struct iobref *iobref,
           dict_t *xdata)
{
    TA_FAILED_FOP(writev, frame, EINVAL);
    return 0;
}

int32_t
ta_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
              dict_t *dict, int32_t flags, dict_t *xdata)
{
    TA_FAILED_FOP(fsetxattr, frame, EINVAL);
    return 0;
}

int32_t
ta_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             dict_t *dict, int32_t flags,	dict_t *xdata)
{
    TA_FAILED_FOP(setxattr, frame, EINVAL);
    return 0;
}

int32_t
ta_fallocate (call_frame_t *frame,	xlator_t *this,	fd_t *fd,
              int32_t keep_size, off_t offset,	size_t len,
              dict_t *xdata)
{
    TA_FAILED_FOP(fallocate, frame, EINVAL);
    return 0;
}

int32_t ta_access(call_frame_t *frame, xlator_t *this, loc_t *loc,
                  int32_t mask, dict_t *xdata)
{
    TA_FAILED_FOP(access, frame, EINVAL);
    return 0;
}

int32_t ta_discard(call_frame_t *frame, xlator_t *this, fd_t *fd,
                   off_t offset, size_t len, dict_t *xdata)
{
    TA_FAILED_FOP(discard, frame, EINVAL);
    return 0;
}

int32_t ta_entrylk(call_frame_t *frame, xlator_t *this,
                   const char *volume, loc_t *loc, const char *basename,
                   entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
    TA_FAILED_FOP(entrylk, frame, EINVAL);
    return 0;
}

int32_t ta_fentrylk(call_frame_t *frame, xlator_t *this,
                    const char *volume, fd_t *fd, const char *basename,
                    entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
    TA_FAILED_FOP(fentrylk, frame, EINVAL);
    return 0;
}

int32_t ta_flush(call_frame_t *frame, xlator_t *this, fd_t *fd,
                 dict_t *xdata)
{
    TA_FAILED_FOP(flush, frame, EINVAL);
    return 0;
}

int32_t ta_fsync(call_frame_t *frame, xlator_t *this, fd_t *fd,
                 int32_t datasync, dict_t *xdata)
{
    TA_FAILED_FOP(fsync, frame, EINVAL);
    return 0;
}
int32_t ta_fsyncdir(call_frame_t *frame, xlator_t *this, fd_t *fd,
                    int32_t datasync, dict_t *xdata)
{
    TA_FAILED_FOP(fsyncdir, frame, EINVAL);
    return 0;
}

int32_t
ta_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             const char *name, dict_t *xdata)
{
    TA_FAILED_FOP(getxattr, frame, EINVAL);
    return 0;
}

int32_t
ta_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
              const char *name, dict_t *xdata)
{
    TA_FAILED_FOP(fgetxattr, frame, EINVAL);
    return 0;
}

int32_t ta_link(call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                loc_t *newloc, dict_t *xdata)
{
    TA_FAILED_FOP(link, frame, EINVAL);
    return 0;
}

int32_t ta_lk(call_frame_t *frame, xlator_t *this, fd_t *fd,
              int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
    TA_FAILED_FOP(lk, frame, EINVAL);
    return 0;
}

int32_t ta_mkdir(call_frame_t *frame, xlator_t *this, loc_t *loc,
                 mode_t mode, mode_t umask, dict_t *xdata)
{
    TA_FAILED_FOP(mkdir, frame, EINVAL);
    return 0;
}

int32_t ta_mknod(call_frame_t *frame, xlator_t *this, loc_t *loc,
                 mode_t mode, dev_t rdev, mode_t umask, dict_t *xdata)
{
    TA_FAILED_FOP(mknod, frame, EINVAL);
    return 0;
}

int32_t ta_open(call_frame_t *frame, xlator_t *this, loc_t *loc,
                int32_t flags, fd_t *fd, dict_t *xdata)
{
    TA_FAILED_FOP(open, frame, EINVAL);
    return 0;
}

int32_t ta_opendir(call_frame_t *frame, xlator_t *this, loc_t *loc,
                   fd_t *fd, dict_t *xdata)
{
    TA_FAILED_FOP(opendir, frame, EINVAL);
    return 0;
}

int32_t ta_readdir(call_frame_t *frame, xlator_t *this, fd_t *fd,
                   size_t size, off_t offset, dict_t *xdata)
{
    TA_FAILED_FOP(readdir, frame, EINVAL);
    return 0;
}

int32_t ta_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd,
                    size_t size, off_t offset, dict_t *xdata)
{
    TA_FAILED_FOP(readdirp, frame, EINVAL);
    return 0;
}

int32_t ta_readlink(call_frame_t *frame, xlator_t *this, loc_t *loc,
                    size_t size, dict_t *xdata)
{
    TA_FAILED_FOP(readlink, frame, EINVAL);
    return 0;
}

int32_t ta_readv(call_frame_t *frame, xlator_t *this, fd_t *fd,
                 size_t size, off_t offset, uint32_t flags, dict_t *xdata)
{
    TA_FAILED_FOP(readv, frame, EINVAL);
    return 0;
}

int32_t
ta_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata)
{
    TA_FAILED_FOP(removexattr, frame, EINVAL);
    return 0;
}

int32_t
ta_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 const char *name, dict_t *xdata)
{
    TA_FAILED_FOP(fremovexattr, frame, EINVAL);
    return 0;
}

int32_t ta_rename(call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                  loc_t *newloc, dict_t *xdata)
{
    TA_FAILED_FOP(rename, frame, EINVAL);
    return 0;
}

int32_t ta_rmdir(call_frame_t *frame, xlator_t *this, loc_t *loc,
                 int xflags, dict_t *xdata)
{
    TA_FAILED_FOP(rmdir, frame, EINVAL);
    return 0;
}

int32_t ta_setattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                   struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
    TA_FAILED_FOP(setattr, frame, EINVAL);
    return 0;
}

int32_t ta_fsetattr(call_frame_t *frame, xlator_t *this, fd_t *fd,
                    struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
    TA_FAILED_FOP(fsetattr, frame, EINVAL);
    return 0;
}

int32_t ta_stat(call_frame_t *frame, xlator_t *this, loc_t *loc,
                dict_t *xdata)
{
    TA_FAILED_FOP(stat, frame, EINVAL);
    return 0;
}

int32_t ta_fstat(call_frame_t *frame, xlator_t *this, fd_t *fd,
                 dict_t *xdata)
{
    TA_FAILED_FOP(fstat, frame, EINVAL);
    return 0;
}

int32_t ta_statfs(call_frame_t *frame, xlator_t *this, loc_t *loc,
                  dict_t *xdata)
{
    TA_FAILED_FOP(statfs, frame, EINVAL);
    return 0;
}

int32_t ta_symlink(call_frame_t *frame, xlator_t *this,
                   const char *linkname, loc_t *loc, mode_t umask,
                   dict_t *xdata)
{
    TA_FAILED_FOP(symlink, frame, EINVAL);
    return 0;
}

int32_t ta_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc,
                    off_t offset, dict_t *xdata)
{
    TA_FAILED_FOP(truncate, frame, EINVAL);
    return 0;
}

int32_t ta_ftruncate(call_frame_t *frame, xlator_t *this, fd_t *fd,
                     off_t offset, dict_t *xdata)
{
    TA_FAILED_FOP(ftruncate, frame, EINVAL);
    return 0;
}

int32_t ta_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc,
                  int xflags, dict_t *xdata)
{
    TA_FAILED_FOP(unlink, frame, EINVAL);
    return 0;
}

int32_t ta_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd,
                    off_t offset, off_t len, dict_t *xdata)
{
    TA_FAILED_FOP(zerofill, frame, EINVAL);
    return 0;
}

int32_t ta_seek(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                gf_seek_what_t what, dict_t *xdata)
{
    TA_FAILED_FOP(seek, frame, EINVAL);
    return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int ret = -1;

        ret = xlator_mem_acct_init (this, gf_ta_mt_end + 1);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting "
                        "initialization failed.");
        return ret;
}


int
reconfigure (xlator_t *this, dict_t *options)
{
    return 0;
}

int32_t
init (xlator_t *this)
{
    if (!this->children || this->children->next) {
        gf_log (this->name, GF_LOG_ERROR,
                "'thin_arbiter' not configured with exactly one child");
        return -1;
    }

    if (!this->parents) {
        gf_log (this->name, GF_LOG_ERROR,
                "dangling volume. check volfile ");
    }
    return 0;
}

void
fini (xlator_t *this)
{
    return;
}

struct xlator_fops fops = {
/*Passed fop*/
    .xattrop      = ta_xattrop,
    .fxattrop     = ta_fxattrop,
/*Failed fop*/
    .writev       = ta_writev,
    .stat         = ta_stat,
    .fstat        = ta_fstat,
    .truncate     = ta_truncate,
    .ftruncate    = ta_ftruncate,
    .access       = ta_access,
    .readlink     = ta_readlink,
    .mknod        = ta_mknod,
    .mkdir        = ta_mkdir,
    .unlink       = ta_unlink,
    .rmdir        = ta_rmdir,
    .symlink      = ta_symlink,
    .rename       = ta_rename,
    .link         = ta_link,
    .open         = ta_open,
    .readv        = ta_readv,
    .flush        = ta_flush,
    .fsync        = ta_fsync,
    .opendir      = ta_opendir,
    .readdir      = ta_readdir,
    .readdirp     = ta_readdirp,
    .fsyncdir     = ta_fsyncdir,
    .statfs       = ta_statfs,
    .setxattr     = ta_setxattr,
    .getxattr     = ta_getxattr,
    .fsetxattr    = ta_fsetxattr,
    .fgetxattr    = ta_fgetxattr,
    .removexattr  = ta_removexattr,
    .fremovexattr = ta_fremovexattr,
    .lk           = ta_lk,
    .entrylk      = ta_entrylk,
    .fentrylk     = ta_fentrylk,
    .setattr      = ta_setattr,
    .fsetattr     = ta_fsetattr,
    .fallocate    = ta_fallocate,
    .discard      = ta_discard,
    .zerofill     = ta_zerofill,
    .seek         = ta_seek,
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
    { .key  = {NULL} },
};
