/*
   Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _POSIX_METADATA_H
#define _POSIX_METADATA_H

#include "posix-metadata-disk.h"

/* In memory representation posix metadata xattr */
typedef struct {
    /* flags indicates valid fields in the structure */
    uint64_t flags;
    struct timespec ctime;
    struct timespec mtime;
    struct timespec atime;
    /* version of structure, bumped up if any new member is added */
    uint8_t version;

    char _pad[7]; /* manual padding */
} posix_mdata_t;

typedef struct {
    unsigned short ctime : 1;
    unsigned short mtime : 1;
    unsigned short atime : 1;
} posix_mdata_flag_t;

/* With inode lock*/
int
posix_get_mdata_xattr(xlator_t *this, const char *real_path, int _fd,
                      inode_t *inode, struct iatt *stbuf);
/* With out inode lock*/
int
__posix_get_mdata_xattr(xlator_t *this, const char *real_path, int _fd,
                        inode_t *inode, struct iatt *stbuf);
void
posix_update_utime_in_mdata(xlator_t *this, const char *real_path, int fd,
                            inode_t *inode, struct timespec *ctime,
                            struct iatt *stbuf, int valid);
void
posix_update_ctime_in_mdata(xlator_t *this, const char *real_path, int fd,
                            inode_t *inode, struct timespec *ctime,
                            struct iatt *stbuf, int valid);
void
posix_set_ctime(call_frame_t *frame, xlator_t *this, const char *real_path,
                int fd, inode_t *inode, struct iatt *stbuf);
void
posix_set_parent_ctime(call_frame_t *frame, xlator_t *this,
                       const char *real_path, int fd, inode_t *inode,
                       struct iatt *stbuf);
void
posix_set_ctime_cfr(call_frame_t *frame, xlator_t *this,
                    const char *real_path_in, int fd_in, inode_t *inode_in,
                    struct iatt *stbuf_in, const char *read_path_put,
                    int fd_out, inode_t *inode_out, struct iatt *stbuf_out);
int
posix_set_mdata_xattr_legacy_files(xlator_t *this, inode_t *inode,
                                   const char *realpath,
                                   struct mdata_iatt *mdata_iatt,
                                   int *op_errno);
void
posix_mdata_iatt_from_disk(struct mdata_iatt *out, posix_mdata_disk_t *in);

#endif /* _POSIX_METADATA_H */
