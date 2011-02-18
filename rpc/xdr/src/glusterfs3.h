/*
  Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#ifndef _GLUSTERFS3_H
#define _GLUSTERFS3_H

#include <sys/uio.h>

#include "glusterfs3-xdr.h"
#include "iatt.h"

#define xdr_decoded_remaining_addr(xdr)        ((&xdr)->x_private)
#define xdr_decoded_remaining_len(xdr)         ((&xdr)->x_handy)
#define xdr_encoded_length(xdr) (((size_t)(&xdr)->x_private) - ((size_t)(&xdr)->x_base))
#define xdr_decoded_length(xdr) (((size_t)(&xdr)->x_private) - ((size_t)(&xdr)->x_base))


#define GF_O_ACCMODE           003
#define GF_O_RDONLY             00
#define GF_O_WRONLY             01
#define GF_O_RDWR               02
#define GF_O_CREAT            0100
#define GF_O_EXCL             0200
#define GF_O_NOCTTY           0400
#define GF_O_TRUNC           01000
#define GF_O_APPEND          02000
#define GF_O_NONBLOCK        04000
#define GF_O_SYNC           010000
#define GF_O_ASYNC          020000

#define GF_O_DIRECT         040000
#define GF_O_DIRECTORY     0200000
#define GF_O_NOFOLLOW      0400000
#define GF_O_NOATIME      01000000
#define GF_O_CLOEXEC      02000000

#define GF_O_LARGEFILE     0100000

#define XLATE_BIT(from, to, bit)    do {                \
                if (from & bit)                         \
                        to = to | GF_##bit;             \
        } while (0)

#define UNXLATE_BIT(from, to, bit)  do {                \
                if (from & GF_##bit)                    \
                        to = to | bit;                  \
        } while (0)

#define XLATE_ACCESSMODE(from, to) do {                 \
                switch (from & O_ACCMODE) {             \
                case O_RDONLY: to |= GF_O_RDONLY;       \
                        break;                          \
                case O_WRONLY: to |= GF_O_WRONLY;       \
                        break;                          \
                case O_RDWR: to |= GF_O_RDWR;           \
                        break;                          \
                }                                       \
        } while (0)

#define UNXLATE_ACCESSMODE(from, to) do {               \
                switch (from & GF_O_ACCMODE) {          \
                case GF_O_RDONLY: to |= O_RDONLY;       \
                        break;                          \
                case GF_O_WRONLY: to |= O_WRONLY;       \
                        break;                          \
                case GF_O_RDWR: to |= O_RDWR;           \
                        break;                          \
                }                                       \
        } while (0)

static inline uint32_t
gf_flags_from_flags (uint32_t flags)
{
        uint32_t gf_flags = 0;

        XLATE_ACCESSMODE (flags, gf_flags);

        XLATE_BIT (flags, gf_flags, O_CREAT);
        XLATE_BIT (flags, gf_flags, O_EXCL);
        XLATE_BIT (flags, gf_flags, O_NOCTTY);
        XLATE_BIT (flags, gf_flags, O_TRUNC);
        XLATE_BIT (flags, gf_flags, O_APPEND);
        XLATE_BIT (flags, gf_flags, O_NONBLOCK);
        XLATE_BIT (flags, gf_flags, O_SYNC);
        XLATE_BIT (flags, gf_flags, O_ASYNC);

        XLATE_BIT (flags, gf_flags, O_DIRECT);
        XLATE_BIT (flags, gf_flags, O_DIRECTORY);
        XLATE_BIT (flags, gf_flags, O_NOFOLLOW);
#ifdef O_NOATIME
        XLATE_BIT (flags, gf_flags, O_NOATIME);
#endif
#ifdef O_CLOEXEC
        XLATE_BIT (flags, gf_flags, O_CLOEXEC);
#endif
        XLATE_BIT (flags, gf_flags, O_LARGEFILE);

        return gf_flags;
}

static inline uint32_t
gf_flags_to_flags (uint32_t gf_flags)
{
        uint32_t flags = 0;

        UNXLATE_ACCESSMODE (gf_flags, flags);

        UNXLATE_BIT (gf_flags, flags, O_CREAT);
        UNXLATE_BIT (gf_flags, flags, O_EXCL);
        UNXLATE_BIT (gf_flags, flags, O_NOCTTY);
        UNXLATE_BIT (gf_flags, flags, O_TRUNC);
        UNXLATE_BIT (gf_flags, flags, O_APPEND);
        UNXLATE_BIT (gf_flags, flags, O_NONBLOCK);
        UNXLATE_BIT (gf_flags, flags, O_SYNC);
        UNXLATE_BIT (gf_flags, flags, O_ASYNC);

        UNXLATE_BIT (gf_flags, flags, O_DIRECT);
        UNXLATE_BIT (gf_flags, flags, O_DIRECTORY);
        UNXLATE_BIT (gf_flags, flags, O_NOFOLLOW);
#ifdef O_NOATIME
        UNXLATE_BIT (gf_flags, flags, O_NOATIME);
#endif
#ifdef O_CLOEXEC
        UNXLATE_BIT (gf_flags, flags, O_CLOEXEC);
#endif
        UNXLATE_BIT (gf_flags, flags, O_LARGEFILE);

        return flags;
}


static inline void
gf_statfs_to_statfs (struct gf_statfs *gf_stat, struct statvfs *stat)
{
        if (!stat || !gf_stat)
                return;

	stat->f_bsize   =  (gf_stat->bsize);
	stat->f_frsize  =  (gf_stat->frsize);
	stat->f_blocks  =  (gf_stat->blocks);
	stat->f_bfree   =  (gf_stat->bfree);
	stat->f_bavail  =  (gf_stat->bavail);
	stat->f_files   =  (gf_stat->files);
	stat->f_ffree   =  (gf_stat->ffree);
	stat->f_favail  =  (gf_stat->favail);
	stat->f_fsid    =  (gf_stat->fsid);
	stat->f_flag    =  (gf_stat->flag);
	stat->f_namemax =  (gf_stat->namemax);
}


static inline void
gf_statfs_from_statfs (struct gf_statfs *gf_stat, struct statvfs *stat)
{
        if (!stat || !gf_stat)
                return;

	gf_stat->bsize   = stat->f_bsize;
	gf_stat->frsize  = stat->f_frsize;
	gf_stat->blocks  = stat->f_blocks;
	gf_stat->bfree   = stat->f_bfree;
	gf_stat->bavail  = stat->f_bavail;
	gf_stat->files   = stat->f_files;
	gf_stat->ffree   = stat->f_ffree;
	gf_stat->favail  = stat->f_favail;
	gf_stat->fsid    = stat->f_fsid;
	gf_stat->flag    = stat->f_flag;
	gf_stat->namemax = stat->f_namemax;
}

static inline void
gf_proto_flock_to_flock (struct gf_proto_flock *gf_proto_flock, struct gf_flock *gf_flock)
{
        if (!gf_flock || !gf_proto_flock)
                return;

	gf_flock->l_type     = gf_proto_flock->type;
	gf_flock->l_whence   = gf_proto_flock->whence;
	gf_flock->l_start    = gf_proto_flock->start;
	gf_flock->l_len      = gf_proto_flock->len;
	gf_flock->l_pid      = gf_proto_flock->pid;
	gf_flock->l_owner    = gf_proto_flock->owner;
}


static inline void
gf_proto_flock_from_flock (struct gf_proto_flock *gf_proto_flock, struct gf_flock *gf_flock)
{
        if (!gf_flock || !gf_proto_flock)
                return;

	gf_proto_flock->type     =  (gf_flock->l_type);
	gf_proto_flock->whence   =  (gf_flock->l_whence);
	gf_proto_flock->start    =  (gf_flock->l_start);
	gf_proto_flock->len      =  (gf_flock->l_len);
	gf_proto_flock->pid      =  (gf_flock->l_pid);
	gf_proto_flock->owner    =  (gf_flock->l_owner);
}

static inline void
gf_stat_to_iatt (struct gf_iatt *gf_stat, struct iatt *iatt)
{
        if (!iatt || !gf_stat)
                return;

        memcpy (iatt->ia_gfid, gf_stat->ia_gfid, 16);
	iatt->ia_ino = gf_stat->ia_ino ;
	iatt->ia_dev = gf_stat->ia_dev ;
	iatt->ia_type = ia_type_from_st_mode (gf_stat->mode) ;
	iatt->ia_prot = ia_prot_from_st_mode (gf_stat->mode) ;
	iatt->ia_nlink = gf_stat->ia_nlink ;
	iatt->ia_uid = gf_stat->ia_uid ;
	iatt->ia_gid = gf_stat->ia_gid ;
	iatt->ia_rdev = gf_stat->ia_rdev ;
	iatt->ia_size = gf_stat->ia_size ;
	iatt->ia_blksize = gf_stat->ia_blksize ;
	iatt->ia_blocks = gf_stat->ia_blocks ;
	iatt->ia_atime = gf_stat->ia_atime ;
	iatt->ia_atime_nsec = gf_stat->ia_atime_nsec ;
	iatt->ia_mtime = gf_stat->ia_mtime ;
	iatt->ia_mtime_nsec = gf_stat->ia_mtime_nsec ;
	iatt->ia_ctime = gf_stat->ia_ctime ;
	iatt->ia_ctime_nsec = gf_stat->ia_ctime_nsec ;
}


static inline void
gf_stat_from_iatt (struct gf_iatt *gf_stat, struct iatt *iatt)
{
        if (!iatt || !gf_stat)
                return;

        memcpy (gf_stat->ia_gfid, iatt->ia_gfid, 16);
	gf_stat->ia_ino = iatt->ia_ino ;
	gf_stat->ia_dev = iatt->ia_dev ;
	gf_stat->mode   = st_mode_from_ia (iatt->ia_prot, iatt->ia_type);
	gf_stat->ia_nlink = iatt->ia_nlink ;
	gf_stat->ia_uid = iatt->ia_uid ;
	gf_stat->ia_gid = iatt->ia_gid ;
	gf_stat->ia_rdev = iatt->ia_rdev ;
	gf_stat->ia_size = iatt->ia_size ;
	gf_stat->ia_blksize = iatt->ia_blksize ;
	gf_stat->ia_blocks = iatt->ia_blocks ;
	gf_stat->ia_atime = iatt->ia_atime ;
	gf_stat->ia_atime_nsec = iatt->ia_atime_nsec ;
	gf_stat->ia_mtime = iatt->ia_mtime ;
	gf_stat->ia_mtime_nsec = iatt->ia_mtime_nsec ;
	gf_stat->ia_ctime = iatt->ia_ctime ;
	gf_stat->ia_ctime_nsec = iatt->ia_ctime_nsec ;
}

/* FOPS */
ssize_t
xdr_serialize_lookup_rsp (struct iovec outmsg, void *resp);

ssize_t
xdr_serialize_getspec_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_common_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_setvolume_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_open_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_create_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_mknod_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_mkdir_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_symlink_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_link_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_rename_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_writev_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_readv_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_readdir_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_readdirp_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_opendir_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_setattr_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_fsetattr_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_truncate_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_ftruncate_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_statfs_rsp (struct iovec outmsg, void *rsp);


ssize_t
xdr_serialize_lk_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_xattrop_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_fxattrop_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_getxattr_rsp (struct iovec outmsg, void *rsp);


ssize_t
xdr_serialize_fgetxattr_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_unlink_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_rmdir_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_rchecksum_rsp (struct iovec outmsg, void *rsp);


ssize_t
xdr_serialize_fstat_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_fsync_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_readlink_rsp (struct iovec outmsg, void *rsp);

ssize_t
xdr_serialize_stat_rsp (struct iovec outmsg, void *rsp);


ssize_t
xdr_to_lookup_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_getspec_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_setvolume_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_statfs_req (struct iovec inmsg, void *args);


ssize_t
xdr_to_stat_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_getattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fstat_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_setattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fsetattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_readv_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_writev_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fsetattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_readlink_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_create_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_open_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_release_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_xattrop_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fxattrop_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_setxattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fsetxattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_flush_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_unlink_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fsync_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_ftruncate_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_truncate_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_getxattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fgetxattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_removexattr_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_entrylk_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fentrylk_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_inodelk_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_finodelk_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_lk_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_access_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_opendir_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_readdirp_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_readdir_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_fsyncdir_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_mknod_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_mkdir_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_symlink_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_rmdir_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_rchecksum_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_rename_req (struct iovec inmsg, void *args);

ssize_t
xdr_to_link_req (struct iovec inmsg, void *args);

ssize_t
xdr_from_lookup_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_getspec_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_stat_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_access_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_truncate_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_ftruncate_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_readlink_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_writev_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_readv_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_flush_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_fstat_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_fsync_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_open_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_unlink_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_rmdir_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_fsyncdir_req (struct iovec outmsg, void *args);


ssize_t
xdr_from_fsetxattr_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_setxattr_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_getxattr_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_fgetxattr_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_statfs_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_opendir_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_lk_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_inodelk_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_finodelk_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_entrylk_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_fentrylk_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_removexattr_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_xattrop_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_fxattrop_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_rchecksum_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_readdir_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_readdirp_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_setattr_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_fsetattr_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_symlink_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_rename_req (struct iovec outmsg, void *args);


ssize_t
xdr_from_link_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_rename_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_create_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_mkdir_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_mknod_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_releasedir_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_release_req (struct iovec outmsg, void *args);

ssize_t
xdr_from_setvolume_req (struct iovec outmsg, void *args);

ssize_t
xdr_to_setvolume_rsp (struct iovec inmsg, void *args);



ssize_t
xdr_to_statfs_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_stat_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_fstat_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_rename_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_readlink_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_link_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_access_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_truncate_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_ftruncate_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_unlink_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_rmdir_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_open_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_create_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_mkdir_rsp (struct iovec inmsg, void *args);


ssize_t
xdr_to_mknod_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_setattr_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_fsetattr_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_common_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_getxattr_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_fxattrop_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_xattrop_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_symlink_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_fgetxattr_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_rchecksum_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_lk_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_readdirp_rsp (struct iovec inmsg, void *args);

ssize_t
xdr_to_readdir_rsp (struct iovec inmsg, void *args);
ssize_t
xdr_to_opendir_rsp (struct iovec inmsg, void *args);
ssize_t
xdr_to_lookup_rsp (struct iovec inmsg, void *args);
ssize_t
xdr_to_readv_rsp (struct iovec inmsg, void *args);
ssize_t
xdr_to_getspec_rsp (struct iovec inmsg, void *args);

#endif /* !_GLUSTERFS3_H */
