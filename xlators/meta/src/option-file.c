/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <glusterfs/xlator.h>
#include <glusterfs/defaults.h>

#include "meta-mem-types.h"
#include "meta.h"
#include "meta-hooks.h"

/*
 * Generic, read-only variant
 *
 * Exports xl->options dict to userspace (keys as filenames, values as file
 * content), applicable to any xlator.
 *
 */

static int
option_file_fill(xlator_t *this, inode_t *inode, strfd_t *strfd)
{
    data_t *data = NULL;

    data = meta_ctx_get(inode, this);

    strprintf(strfd, "%s\n", data_to_str(data));

    return strfd->size;
}

static struct meta_ops option_file_ops = {.file_fill = option_file_fill};

int
meta_option_file_hook(call_frame_t *frame, xlator_t *this, loc_t *loc,
                      dict_t *xdata)
{
    xlator_t *xl = NULL;

    xl = meta_ctx_get(loc->parent, this);

    meta_ctx_set(loc->inode, this, dict_get(xl->options, (char *)loc->name));

    meta_ops_set(loc->inode, this, &option_file_ops);

    return 0;
}

/*
 * Adjustable variant
 *
 * Exports xl->voiume_options to userspace, relying on the fetch and
 * update methods of the volume_option_t instance. The xlator has to
 * implement fetch for those options which it wants to make accessible
 * for reading through this interface, and also update for the ones
 * which it wants to make adjustable (typically consulting the appropriate
 * member(s) of xl->private).
 */

static int
adjustable_option_file_fill(xlator_t *this, inode_t *inode, strfd_t *strfd)
{
    volume_option_t *vol_opt = NULL;
    char *optval = NULL;
    xlator_t *xl = NULL;
    dentry_t *dentry = NULL;

    xl = meta_ctx_get(inode, this);
    dentry = list_first_entry(&inode->dentry_list, dentry_t, inode_list);
    if (!dentry)
        return -EIO;
    vol_opt = xlator_volume_option_get(xl, dentry->name);
    optval = vol_opt->fetch(vol_opt, xl);
    if (!optval)
        return -ENOMEM;

    strprintf(strfd, "%s\n", optval);
    GF_FREE(optval);

    return strfd->size;
}

static int
adjustable_option_file_write_str(xlator_t *this, fd_t *fd, char *str)
{
    volume_option_t *vol_opt = NULL;
    xlator_t *xl = NULL;
    dentry_t *dentry = NULL;
    int ret = 0;
    size_t len = 0;

    len = strlen(str);
    /*
     * For convenience, we chomp str (some conversions would not tolerate a
     * bogus trailing newline). (If user meant to feed newline-trailed input,
     * they'd need to add an extra one.)
     */
    if (len > 0 && str[len - 1] == '\n')
        str[len - 1] = 0;

    xl = meta_ctx_get(fd->inode, this);
    dentry = list_first_entry(&fd->inode->dentry_list, dentry_t, inode_list);
    if (!dentry)
        return -EIO;
    vol_opt = xlator_volume_option_get(xl, dentry->name);
    ret = vol_opt->update(vol_opt, xl, str);

    return (ret < 0) ? ret : len;
}

static int
adjustable_option_file_write(xlator_t *this, fd_t *fd, struct iovec *iov,
                             int count)
{
    /*
     * By using file_write_wrapper, only string data accepted (no interim
     * zeros).
     */
    return file_write_wrapper(this, fd, iov, count,
                              adjustable_option_file_write_str);
}

static struct meta_ops adjustable_option_file_ops = {
    .file_fill = adjustable_option_file_fill};

static struct meta_ops adjustable_option_file_rw_ops = {
    .file_fill = adjustable_option_file_fill,
    .file_write = adjustable_option_file_write};

int
meta_adjustable_option_file_hook(call_frame_t *frame, xlator_t *this,
                                 loc_t *loc, dict_t *xdata)
{
    xlator_t *xl = NULL;
    volume_option_t *vol_opt = NULL;

    xl = meta_ctx_get(loc->parent, this);
    meta_ctx_set(loc->inode, this, xl);

    vol_opt = xlator_volume_option_get(xl, (char *)loc->name);
    meta_ops_set(loc->inode, this,
                 vol_opt->update ? &adjustable_option_file_rw_ops
                                 : &adjustable_option_file_ops);

    return 0;
}

static int
adjustable_option_link_fill(xlator_t *this, inode_t *inode, strfd_t *strfd)
{
    volume_option_t *vol_opt = NULL;
    xlator_t *xl = NULL;
    dentry_t *dentry = NULL;

    xl = meta_ctx_get(inode, this);
    dentry = list_first_entry(&inode->dentry_list, dentry_t, inode_list);
    if (!dentry)
        return -EIO;
    vol_opt = xlator_volume_option_get(xl, dentry->name);

    strprintf(strfd, "%s", vol_opt->key[0]);

    return 0;
}

static struct meta_ops adjustable_option_link_ops = {
    .link_fill = adjustable_option_link_fill};

int
meta_adjustable_option_link_hook(call_frame_t *frame, xlator_t *this,
                                 loc_t *loc, dict_t *xdata)
{
    meta_ctx_set(loc->inode, this, meta_ctx_get(loc->parent, this));

    meta_ops_set(loc->inode, this, &adjustable_option_link_ops);

    return 0;
}
