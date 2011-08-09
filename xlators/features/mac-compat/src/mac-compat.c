/*
  Copyright (c) 2008-2011 Gluster, Inc. <http://www.gluster.com>
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

#include "xlator.h"
#include "defaults.h"
#include "compat-errno.h"


enum apple_xattr {
        GF_FINDER_INFO_XATTR,
        GF_RESOURCE_FORK_XATTR,
        GF_XATTR_ALL,
        GF_XATTR_NONE
};

static char *apple_xattr_name[] = {
        [GF_FINDER_INFO_XATTR]   = "com.apple.FinderInfo",
        [GF_RESOURCE_FORK_XATTR] = "com.apple.ResourceFork"
};

static const char *apple_xattr_value[] = {
        [GF_FINDER_INFO_XATTR]   =
        /* 1 2 3 4 5 6 7 8 */
         "\0\0\0\0\0\0\0\0"
         "\0\0\0\0\0\0\0\0"
         "\0\0\0\0\0\0\0\0"
         "\0\0\0\0\0\0\0\0",
        [GF_RESOURCE_FORK_XATTR] = ""
};

static int32_t apple_xattr_len[] = {
        [GF_FINDER_INFO_XATTR]   = 32,
        [GF_RESOURCE_FORK_XATTR] = 1
};


int32_t
maccomp_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        intptr_t ax = (intptr_t)this->private;
        int i = 0;

        if ((ax == GF_XATTR_ALL && op_ret >= 0) || ax != GF_XATTR_NONE) {
                op_ret = op_errno = 0;

                for (i = 0; i < GF_XATTR_ALL; i++) {
                        if (dict_get (dict, apple_xattr_name[i]))
                                continue;

                        if (dict_set (dict, apple_xattr_name[i],
                                      bin_to_data ((void *)apple_xattr_value[i],
                                                   apple_xattr_len[i])) == -1) {
                                op_ret = -1;
                                op_errno = ENOMEM;

                                break;
                        }
                }
         }

        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict);

        return 0;
}


int32_t
maccomp_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                  const char *name)
{
        intptr_t ax = GF_XATTR_NONE;
        int i = 0;

        if (name) {
                for (i = 0; i < GF_XATTR_ALL; i++) {
                        if (strcmp (apple_xattr_name[i], name) == 0) {
                                ax = i;

                                break;
                        }
                }
        } else
                ax = GF_XATTR_ALL;

        this->private = (void *)ax;

        STACK_WIND (frame, maccomp_getxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr,
                    loc, name);
        return 0;
}


int32_t
maccomp_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   const char *name)
{
        intptr_t ax = GF_XATTR_NONE;
        int i = 0;

        if (name) {
                for (i = 0; i < GF_XATTR_ALL; i++) {
                        if (strcmp (apple_xattr_name[i], name) == 0) {
                                ax = i;

                                break;
                        }
                }
        } else
                ax = GF_XATTR_ALL;

        this->private = (void *)ax;

        STACK_WIND (frame, maccomp_getxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fgetxattr,
                    fd, name);
        return 0;
}


int32_t
maccomp_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno)
{
        intptr_t ax = (intptr_t)this->private;

        if (op_ret == -1 && ax != GF_XATTR_NONE)
                op_ret = op_errno = 0;

        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno);

        return 0;
}


int32_t
maccomp_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
                  int32_t flags)
{
        intptr_t ax = GF_XATTR_NONE;
        int i = 0;

        for (i = 0; i < GF_XATTR_ALL; i++) {
                if (dict_get (dict, apple_xattr_name[i])) {
                        ax = i;

                        break;
                }
        }

        this->private = (void *)ax;

        STACK_WIND (frame, maccomp_setxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    loc, dict, flags);
        return 0;
}


int32_t
maccomp_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
                   int32_t flags)
{
        intptr_t ax = GF_XATTR_NONE;
        int i = 0;

        for (i = 0; i < GF_XATTR_ALL; i++) {
                if (dict_get (dict, apple_xattr_name[i])) {
                        ax = i;

                        break;
                }
        }

        this->private = (void *)ax;

        STACK_WIND (frame, maccomp_setxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetxattr,
                    fd, dict, flags);
        return 0;
}


int32_t
init (xlator_t *this)
{
        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "translator not configured with exactly one child");
                return -1;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
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
        .getxattr    = maccomp_getxattr,
        .fgetxattr   = maccomp_fgetxattr,
        .setxattr    = maccomp_setxattr,
        .fsetxattr   = maccomp_fsetxattr,
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key = {NULL} },
};
