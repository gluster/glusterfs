/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "defaults.h"
#include "compat-errno.h"
#include "syscall.h"
#include "mem-pool.h"
#include "mac-compat.h"

static int
dict_key_remove_namespace(dict_t *dict, char *key, data_t *value, void *data)
{
  /*
    char buffer[3*value->len+1];
    int index = 0;
    for (index = 0; index < value->len; index++)
    sprintf(buffer+3*index, " %02x", value->data[index]);
  */
        xlator_t *this = (xlator_t *) data;
        if (strncmp(key, "user.", 5) == 0) {
                dict_set (dict, key + 5, value);
                gf_log (this->name, GF_LOG_DEBUG,
                        "remove_namespace_dict: %s -> %s ", key, key + 5);
                dict_del (dict, key);
        }
        return 0;
}

int32_t
maccomp_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *dict,
                      dict_t *xdata)
{
        intptr_t ax = (intptr_t)this->private;
        int i = 0;

        gf_log (this->name, GF_LOG_DEBUG,
                "getxattr_cbk: dict %p private: %p xdata %p ", dict,
                this->private, xdata);

        if (dict) {
                dict_foreach(dict, dict_key_remove_namespace, this);
        }
        else {
                // TODO: we expect dict to exist here, don't know why this
                // this is needed
                dict = dict_new();
        }
        gf_log (this->name, GF_LOG_DEBUG,
                "getxattr_cbk: dict %p ax: %ld op_ret %d op_err %d  ", dict, ax,
                op_ret, op_errno);
        if ((ax == GF_XATTR_ALL && op_ret >= 0) || ax != GF_XATTR_NONE) {
                op_ret = op_errno = 0;
                for (i = 0; i < GF_XATTR_ALL; i++) {
                        if (dict_get (dict, apple_xattr_name[i]))
                                continue;
                        /* set dummy data */
                        gf_log (this->name, GF_LOG_DEBUG,
                                "getxattr_cbk: setting dummy data %p, %s", dict,
                                apple_xattr_name[i]);
                        if (dict_set (dict, apple_xattr_name[i],
                                      bin_to_data ((void *)apple_xattr_value[i],
                                                   apple_xattr_len[i])) == -1) {
                                op_ret = -1;
                                op_errno = ENOATTR;

                                break;
                        }
                }
         }
        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


static
int prepend_xattr_user_namespace(dict_t *dict, char *key, data_t *value, void *obj)
{
        xlator_t *this = (xlator_t *) obj;
        dict_t *newdict = (dict_t *) this->private;
        char *newkey = NULL;
        gf_add_prefix(XATTR_USER_PREFIX, key, &newkey);
        key = newkey;
        dict_set(newdict, (char *)key, value);
        if (newkey)
                GF_FREE(newkey);
        return 0;
}

intptr_t
check_name(const char *name, char **newkey)
{
        intptr_t ax = GF_XATTR_NONE;
        if (name) {
                int i = 0;
                for (i = 0; i < GF_XATTR_ALL; i++) {
                        if (strcmp (apple_xattr_name[i], name) == 0) {
                                ax = i;
                                break;
                        }
                }
                gf_add_prefix("user.", name, newkey);
        } else
                ax = GF_XATTR_ALL;
        return ax;
}

int32_t
maccomp_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                  const char *name, dict_t *xdata)
{
        char *newkey = NULL;
        this->private = (void *) check_name(name, &newkey);

        gf_log (this->name, GF_LOG_DEBUG,
                "getxattr: name %s private: %p xdata %p ", name,
                this->private, xdata);
        STACK_WIND (frame, maccomp_getxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr,
                    loc, newkey, xdata);
        return 0;
}


int32_t
maccomp_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   const char *name, dict_t *xdata)
{
        char *newkey = NULL;
        this->private = (void *) check_name(name, &newkey);

        STACK_WIND (frame, maccomp_getxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fgetxattr,
                    fd, newkey, xdata);
        GF_FREE(newkey);
        return 0;
}

int32_t
maccomp_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        intptr_t ax = (intptr_t)this->private;

        if (op_ret == -1 && ax != GF_XATTR_NONE)
                op_ret = op_errno = 0;
        gf_log (this->name, GF_LOG_DEBUG,
                "setxattr_cbk op_ret %d op_errno %d private: %p xdata %p ",
                op_ret, op_errno, this->private, xdata);
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int32_t
maccomp_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *iatt1,
                     struct iatt *iattr2, dict_t *xdata)
{
        gf_log (this->name, GF_LOG_DEBUG,
                "setattr_cbk op_ret %d op_errno %d private: %p xdata %p ",
                op_ret, op_errno, this->private, xdata);
        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno,
                             iatt1, iattr2, xdata);
        return 0;
}

int map_flags(int flags)
{
        /* DARWIN has different defines on XATTR_ flags.
           There do not seem to be a POSIX standard
           Parse any other flags over.
           NOFOLLOW is always true on Linux and Darwin
        */
        int linux_flags = flags & ~(GF_XATTR_CREATE | GF_XATTR_REPLACE | XATTR_REPLACE);
        if (XATTR_CREATE & flags)
                linux_flags |= GF_XATTR_CREATE;
        if (XATTR_REPLACE & flags)
                linux_flags |= GF_XATTR_REPLACE;
        return linux_flags;
}

int32_t
maccomp_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   const char *name, dict_t *xdata)
{
        char *newkey = NULL;

        this->private = (void *) check_name(name, &newkey);

        STACK_WIND (frame, default_fremovexattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fremovexattr,
                    fd, newkey, xdata);
        GF_FREE(newkey);
        return 0;
}

int32_t
maccomp_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
                  int32_t flags, dict_t *xdata)
{
        intptr_t ax = GF_XATTR_NONE;
        int i = 0;

        for (i = 0; i < GF_XATTR_ALL; i++) {
                if (dict_get (dict, apple_xattr_name[i])) {
                        ax = i;

                        break;
                }
        }
        dict_t *newdict = dict_new();
        this->private = (void *) newdict;
        dict_foreach(dict, prepend_xattr_user_namespace, this);

        this->private = (void *)ax;
        int linux_flags = map_flags(flags);
        gf_log (this->name, GF_LOG_DEBUG,
                "setxattr flags: %d -> %d dict %p private: %p xdata %p ",
                flags, linux_flags, dict, this->private, xdata);
        STACK_WIND (frame, maccomp_setxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    loc, newdict, linux_flags, xdata);
        dict_unref(newdict);
        return 0;
}

int32_t
maccomp_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc, struct iatt *iattr,
                  int32_t flags, dict_t *xdata)
{
        gf_log (this->name, GF_LOG_DEBUG,
                "setattr iattr %p private: %p xdata %p ",
                iattr, this->private, xdata);
        STACK_WIND (frame, maccomp_setattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setattr,
                    loc, iattr, flags, xdata);
        return 0;
}

int32_t
maccomp_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                     const char *name, dict_t *xdata)
{
        char *newkey = NULL;
        this->private = (void *) check_name(name, &newkey);

        STACK_WIND (frame, default_removexattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr,
                    loc, newkey, xdata);

        gf_log (this->name, GF_LOG_TRACE,
                "removeattr name %p private: %p xdata %p ",
                name, this->private, xdata);
        GF_FREE(newkey);
        return 0;

}

int32_t
maccomp_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
                   int32_t flags, dict_t *xdata)
{
        intptr_t ax = GF_XATTR_NONE;
        int i = 0;

        for (i = 0; i < GF_XATTR_ALL; i++) {
                if (dict_get (dict, apple_xattr_name[i])) {
                        ax = i;

                        break;
                }
        }

        dict_t *newdict = dict_new();
        this->private = (void *) newdict;
        dict_foreach(dict, prepend_xattr_user_namespace, this);

        this->private = (void *)ax;
        int linux_flags = map_flags(flags);
        gf_log (this->name, GF_LOG_DEBUG,
                "fsetxattr flags: %d -> %d dict %p private: %p xdata %p ",
                flags, linux_flags, dict, this->private, xdata);
        STACK_WIND (frame, maccomp_setxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetxattr,
                    fd, newdict, linux_flags, xdata);
        dict_unref(newdict);
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
        .getxattr       = maccomp_getxattr,
        .fgetxattr      = maccomp_fgetxattr,
        .setxattr       = maccomp_setxattr,
        .setattr        = maccomp_setattr,
        .fsetxattr      = maccomp_fsetxattr,
        .removexattr    = maccomp_removexattr,
        .fremovexattr   = maccomp_fremovexattr,
};

struct xlator_cbks cbks;

struct volume_options options[] = {
        { .key = {NULL} },
};
