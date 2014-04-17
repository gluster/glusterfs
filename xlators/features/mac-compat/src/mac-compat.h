/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __MAC_COMPAT_H__
#define __MAC_COMPAT_H__

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

#endif /* __MAC_COMPAT_H__ */
