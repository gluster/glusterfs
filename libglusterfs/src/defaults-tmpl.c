/*
  Copyright (c) 2008-2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* libglusterfs/src/defaults.c:
   This file contains functions, which are used to fill the 'fops', 'cbk'
   structures in the xlator structures, if they are not written. Here, all the
   function calls are plainly forwared to the first child of the xlator, and
   all the *_cbk function does plain STACK_UNWIND of the frame, and returns.

   This function also implements *_resume () functions, which does same
   operation as a fop().

   All the functions are plain enough to understand.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "defaults.h"

#pragma generate

struct xlator_fops _default_fops = {
        .create = default_create,
        .open = default_open,
        .stat = default_stat,
        .readlink = default_readlink,
        .mknod = default_mknod,
        .mkdir = default_mkdir,
        .unlink = default_unlink,
        .rmdir = default_rmdir,
        .symlink = default_symlink,
        .rename = default_rename,
        .link = default_link,
        .truncate = default_truncate,
        .readv = default_readv,
        .writev = default_writev,
        .statfs = default_statfs,
        .flush = default_flush,
        .fsync = default_fsync,
        .setxattr = default_setxattr,
        .getxattr = default_getxattr,
        .fsetxattr = default_fsetxattr,
        .fgetxattr = default_fgetxattr,
        .removexattr = default_removexattr,
        .fremovexattr = default_fremovexattr,
        .opendir = default_opendir,
        .readdir = default_readdir,
        .readdirp = default_readdirp,
        .fsyncdir = default_fsyncdir,
        .access = default_access,
        .ftruncate = default_ftruncate,
        .fstat = default_fstat,
        .lk = default_lk,
        .inodelk = default_inodelk,
        .finodelk = default_finodelk,
        .entrylk = default_entrylk,
        .fentrylk = default_fentrylk,
        .lookup = default_lookup,
        .rchecksum = default_rchecksum,
        .xattrop = default_xattrop,
        .fxattrop = default_fxattrop,
        .setattr = default_setattr,
        .fsetattr = default_fsetattr,
	.fallocate = default_fallocate,
	.discard = default_discard,
        .zerofill = default_zerofill,
        .ipc = default_ipc,
        .seek = default_seek,

        .getspec = default_getspec,
        .getactivelk = default_getactivelk,
        .setactivelk = default_setactivelk,
};
struct xlator_fops *default_fops = &_default_fops;


/*
 * Remaining functions don't follow the fop calling conventions, so they're
 * not generated.
 */

int32_t
default_forget (xlator_t *this, inode_t *inode)
{
        gf_log_callingfn (this->name, GF_LOG_DEBUG, "xlator does not "
                          "implement forget_cbk");
        return 0;
}


int32_t
default_releasedir (xlator_t *this, fd_t *fd)
{
        gf_log_callingfn (this->name, GF_LOG_DEBUG, "xlator does not "
                          "implement releasedir_cbk");
        return 0;
}

int32_t
default_release (xlator_t *this, fd_t *fd)
{
        gf_log_callingfn (this->name, GF_LOG_DEBUG, "xlator does not "
                          "implement release_cbk");
        return 0;
}

/* notify */
int
default_notify (xlator_t *this, int32_t event, void *data, ...)
{
        switch (event) {
        case GF_EVENT_PARENT_UP:
        case GF_EVENT_PARENT_DOWN:
        {
                xlator_list_t *list = this->children;

                while (list) {
                        xlator_notify (list->xlator, event, this);
                        list = list->next;
                }
        }
        break;
        case GF_EVENT_CHILD_CONNECTING:
        case GF_EVENT_CHILD_DOWN:
        case GF_EVENT_CHILD_UP:
        case GF_EVENT_AUTH_FAILED:
        {
                xlator_list_t *parent = this->parents;

                /*
                 * Handle case of CHILD_* & AUTH_FAILED event specially, send
                 * it to fuse.
                 */
                if (!parent && this->ctx && this->ctx->master) {
                        xlator_notify (this->ctx->master, event, this->graph,
                                       NULL);
                }

                while (parent) {
                        if (parent->xlator->init_succeeded)
                                xlator_notify (parent->xlator, event,
                                               this, NULL);
                        parent = parent->next;
                }
        }
        break;
        case GF_EVENT_UPCALL:
        {
                xlator_list_t *parent = this->parents;

                if (!parent && this->ctx && this->ctx->master)
                        xlator_notify (this->ctx->master, event, data, NULL);

                while (parent) {
                        if (parent->xlator->init_succeeded)
                                xlator_notify (parent->xlator, event,
                                               data, NULL);
                        parent = parent->next;
                }
        }
        break;
        default:
        {
                xlator_list_t *parent = this->parents;

                while (parent) {
                        if (parent->xlator->init_succeeded)
                                xlator_notify (parent->xlator, event,
                                               this, NULL);
                        parent = parent->next;
                }
        }
        /*
         * Apparently our picky-about-everything else coding standard allows
         * adjacent same-indendation-level close braces.  Clearly it has
         * nothing to do with readability.
         */
        }

        return 0;
}

int32_t
default_mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        ret = xlator_mem_acct_init (this, gf_common_mt_end);

        return ret;
}
