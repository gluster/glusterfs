/*
  Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "utime.h"

int32_t
gf_utime_invalidate (xlator_t *this,
    inode_t *inode)
{
        return 0;
}


int32_t
gf_utime_forget (xlator_t *this,
    inode_t *inode)
{
        return 0;
}


int32_t
gf_utime_client_destroy (xlator_t *this,
    client_t *client)
{
        return 0;
}


void
gf_utime_ictxmerge (xlator_t *this,
    fd_t *fd,
    inode_t *inode,
    inode_t *linked_inode)
{
        return;
}


int32_t
gf_utime_release (xlator_t *this,
    fd_t *fd)
{
        return 0;
}


int32_t
gf_utime_releasedir (xlator_t *this,
    fd_t *fd)
{
        return 0;
}


int32_t
gf_utime_client_disconnect (xlator_t *this,
    client_t *client)
{
        return 0;
}


int32_t
gf_utime_fdctx_to_dict (xlator_t *this,
    fd_t *fd,
    dict_t *dict)
{
        return 0;
}


int32_t
gf_utime_inode (xlator_t *this)
{
        return 0;
}


int32_t
gf_utime_inode_to_dict (xlator_t *this,
    dict_t *dict)
{
        return 0;
}


int32_t
gf_utime_history (xlator_t *this)
{
        return 0;
}


int32_t
gf_utime_fd (xlator_t *this)
{
        return 0;
}


int32_t
gf_utime_fd_to_dict (xlator_t *this,
    dict_t *dict)
{
        return 0;
}


int32_t
gf_utime_fdctx (xlator_t *this,
    fd_t *fd)
{
        return 0;
}


int32_t
gf_utime_inodectx (xlator_t *this,
    inode_t *ino)
{
        return 0;
}


int32_t
gf_utime_inodectx_to_dict (xlator_t *this,
    inode_t *ino,
    dict_t *dict)
{
        return 0;
}


int32_t
gf_utime_priv_to_dict (xlator_t *this,
    dict_t *dict, char *brickname)
{
        return 0;
}


int32_t
gf_utime_priv (xlator_t *this)
{
        return 0;
}


int32_t
init (xlator_t *this)
{
        return 0;
}

void
fini (xlator_t *this)
{
        return;
}

int32_t
reconfigure (xlator_t *this, dict_t *dict)
{
        return 0;
}

int
notify (xlator_t *this, int event, void *data, ...)
{
        return default_notify (this, event, data);
}


struct xlator_fops fops = {
        /* TODO: Need to go through other fops and
         *       check if they modify time attributes
         */
        .rename               = gf_utime_rename,
        .mknod                = gf_utime_mknod,
        .readv                = gf_utime_readv,
        .fremovexattr         = gf_utime_fremovexattr,
        .open                 = gf_utime_open,
        .create               = gf_utime_create,
        .mkdir                = gf_utime_mkdir,
        .writev               = gf_utime_writev,
        .rmdir                = gf_utime_rmdir,
        .fallocate            = gf_utime_fallocate,
        .truncate             = gf_utime_truncate,
        .symlink              = gf_utime_symlink,
        .zerofill             = gf_utime_zerofill,
        .link                 = gf_utime_link,
        .ftruncate            = gf_utime_ftruncate,
        .unlink               = gf_utime_unlink,
        .setattr              = gf_utime_setattr,
        .fsetattr             = gf_utime_fsetattr,
        .opendir              = gf_utime_opendir,
        .removexattr          = gf_utime_removexattr,
};
struct xlator_cbks cbks = {
        .invalidate           = gf_utime_invalidate,
        .forget               = gf_utime_forget,
        .client_destroy       = gf_utime_client_destroy,
        .ictxmerge            = gf_utime_ictxmerge,
        .release              = gf_utime_release,
        .releasedir           = gf_utime_releasedir,
        .client_disconnect    = gf_utime_client_disconnect,
};
struct xlator_dumpops dumpops = {
        .fdctx_to_dict        = gf_utime_fdctx_to_dict,
        .inode                = gf_utime_inode,
        .inode_to_dict        = gf_utime_inode_to_dict,
        .history              = gf_utime_history,
        .fd                   = gf_utime_fd,
        .fd_to_dict           = gf_utime_fd_to_dict,
        .fdctx                = gf_utime_fdctx,
        .inodectx             = gf_utime_inodectx,
        .inodectx_to_dict     = gf_utime_inodectx_to_dict,
        .priv_to_dict         = gf_utime_priv_to_dict,
        .priv                 = gf_utime_priv,
};
