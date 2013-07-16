/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GLFS_FUSE_BRIDGE_H
#define _GLFS_FUSE_BRIDGE_H

#define UUID_CANONICAL_FORM_LEN 36

#define GF_FUSE_AUX_GFID_NEWFILE "glusterfs.gfid.newfile"
#define GF_FUSE_AUX_GFID_HEAL    "glusterfs.gfid.heal"

struct auxfuse_symlink_in {
        char     *linkpath;
} __attribute__ ((__packed__));

struct auxfuse_mknod_in {
	unsigned int mode;
	unsigned int rdev;
	unsigned int umask;
} __attribute__ ((__packed__));

struct auxfuse_mkdir_in {
	unsigned int mode;
	unsigned int umask;
} __attribute__ ((__packed__));

typedef struct {
        unsigned int  uid;
        unsigned int  gid;
        char          gfid[UUID_CANONICAL_FORM_LEN + 1];
        unsigned int  st_mode;
        char         *bname;

        union {
                struct auxfuse_mkdir_in   mkdir;
                struct auxfuse_mknod_in   mknod;
                struct auxfuse_symlink_in symlink;
        } __attribute__ ((__packed__)) args;
} __attribute__((__packed__)) fuse_auxgfid_newfile_args_t;

typedef struct {
        char      gfid[UUID_CANONICAL_FORM_LEN + 1];
        char     *bname; /* a null terminated basename */
} __attribute__((__packed__)) fuse_auxgfid_heal_args_t;

#endif
