/*
  Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <fcntl.h>

#include "byte-order.h"


struct gf_stat {
	uint64_t ino;
	uint64_t size;
	uint64_t blocks;
	uint32_t dev;
	uint32_t rdev;
	uint32_t mode;
	uint32_t nlink;
	uint32_t uid;
	uint32_t gid;
	uint32_t blksize;
	uint32_t atime;
	uint32_t atime_nsec;
	uint32_t mtime;
	uint32_t mtime_nsec;
	uint32_t ctime;
	uint32_t ctime_nsec;
} __attribute__((packed));


static inline void
gf_stat_to_stat (struct gf_stat *gf_stat, struct stat *stat)
{
	stat->st_dev          = ntoh32 (gf_stat->dev);
	stat->st_ino          = ntoh64 (gf_stat->ino);
	stat->st_mode         = ntoh32 (gf_stat->mode);
	stat->st_nlink        = ntoh32 (gf_stat->nlink);
	stat->st_uid          = ntoh32 (gf_stat->uid);
	stat->st_gid          = ntoh32 (gf_stat->gid);
	stat->st_rdev         = ntoh32 (gf_stat->rdev);
	stat->st_size         = ntoh64 (gf_stat->size);
	stat->st_blksize      = ntoh32 (gf_stat->blksize);
	stat->st_blocks       = ntoh64 (gf_stat->blocks);
	stat->st_atime        = ntoh32 (gf_stat->atime);
	stat->st_mtime        = ntoh32 (gf_stat->mtime);
	stat->st_ctime        = ntoh32 (gf_stat->ctime);
	/* TODO: handle nsec */
}


static inline void
gf_stat_from_stat (struct gf_stat *gf_stat, struct stat *stat)
{
	gf_stat->dev         = hton32 (stat->st_dev);
	gf_stat->ino         = hton64 (stat->st_ino);
	gf_stat->mode        = hton32 (stat->st_mode);
	gf_stat->nlink       = hton32 (stat->st_nlink);
	gf_stat->uid         = hton32 (stat->st_uid);
	gf_stat->gid         = hton32 (stat->st_gid);
	gf_stat->rdev        = hton32 (stat->st_rdev);
	gf_stat->size        = hton64 (stat->st_size);
	gf_stat->blksize     = hton32 (stat->st_blksize);
	gf_stat->blocks      = hton64 (stat->st_blocks);
	gf_stat->atime       = hton32 (stat->st_atime);
	gf_stat->mtime       = hton32 (stat->st_mtime);
	gf_stat->ctime       = hton32 (stat->st_ctime);
	/* TODO: handle nsec */
}


struct gf_statfs {
	uint64_t bsize;
	uint64_t frsize;
	uint64_t blocks;
	uint64_t bfree;
	uint64_t bavail;
	uint64_t files;
	uint64_t ffree;
	uint64_t favail;
	uint64_t fsid;
	uint64_t flag;
	uint64_t namemax;
} __attribute__((packed));


static inline void
gf_statfs_to_statfs (struct gf_statfs *gf_stat, struct statvfs *stat)
{
	stat->f_bsize   = ntoh64 (gf_stat->bsize);
	stat->f_frsize  = ntoh64 (gf_stat->frsize);
	stat->f_blocks  = ntoh64 (gf_stat->blocks);
	stat->f_bfree   = ntoh64 (gf_stat->bfree);
	stat->f_bavail  = ntoh64 (gf_stat->bavail);
	stat->f_files   = ntoh64 (gf_stat->files);
	stat->f_ffree   = ntoh64 (gf_stat->ffree);
	stat->f_favail  = ntoh64 (gf_stat->favail);
	stat->f_fsid    = ntoh64 (gf_stat->fsid);
	stat->f_flag    = ntoh64 (gf_stat->flag);
	stat->f_namemax = ntoh64 (gf_stat->namemax);
}


static inline void
gf_statfs_from_statfs (struct gf_statfs *gf_stat, struct statvfs *stat)
{
	gf_stat->bsize   = hton64 (stat->f_bsize);
	gf_stat->frsize  = hton64 (stat->f_frsize);
	gf_stat->blocks  = hton64 (stat->f_blocks);
	gf_stat->bfree   = hton64 (stat->f_bfree);
	gf_stat->bavail  = hton64 (stat->f_bavail);
	gf_stat->files   = hton64 (stat->f_files);
	gf_stat->ffree   = hton64 (stat->f_ffree);
	gf_stat->favail  = hton64 (stat->f_favail);
	gf_stat->fsid    = hton64 (stat->f_fsid);
	gf_stat->flag    = hton64 (stat->f_flag);
	gf_stat->namemax = hton64 (stat->f_namemax);
}


struct gf_flock {
	uint16_t type;
	uint16_t whence;
	uint64_t start;
	uint64_t len;
	uint32_t pid;
} __attribute__((packed));


static inline void
gf_flock_to_flock (struct gf_flock *gf_flock, struct flock *flock)
{
	flock->l_type   = ntoh16 (gf_flock->type);
	flock->l_whence = ntoh16 (gf_flock->whence);
	flock->l_start  = ntoh64 (gf_flock->start);
	flock->l_len    = ntoh64 (gf_flock->len);
	flock->l_pid    = ntoh32 (gf_flock->pid);
}


static inline void
gf_flock_from_flock (struct gf_flock *gf_flock, struct flock *flock)
{
	gf_flock->type   = hton16 (flock->l_type);
	gf_flock->whence = hton16 (flock->l_whence);
	gf_flock->start  = hton64 (flock->l_start);
	gf_flock->len    = hton64 (flock->l_len);
	gf_flock->pid    = hton32 (flock->l_pid);
}


struct gf_timespec {
	uint32_t tv_sec;
	uint32_t tv_nsec;
} __attribute__((packed));


static inline void
gf_timespec_to_timespec (struct gf_timespec *gf_ts, struct timespec *ts)
{

	ts[0].tv_sec  = ntoh32 (gf_ts[0].tv_sec);
	ts[0].tv_nsec = ntoh32 (gf_ts[0].tv_nsec);
	ts[1].tv_sec  = ntoh32 (gf_ts[1].tv_sec);
	ts[1].tv_nsec = ntoh32 (gf_ts[1].tv_nsec);
}


static inline void
gf_timespec_from_timespec (struct gf_timespec *gf_ts, struct timespec *ts)
{
	gf_ts[0].tv_sec  = hton32 (ts[0].tv_sec);
	gf_ts[0].tv_nsec = hton32 (ts[0].tv_nsec);
	gf_ts[1].tv_sec  = hton32 (ts[1].tv_sec);
	gf_ts[1].tv_nsec = hton32 (ts[1].tv_nsec);
}


typedef struct {
	uint64_t ino;
	char     path[0]; /* NULL terminated */
} __attribute__((packed)) gf_fop_stat_req_t;;
typedef struct {
	struct gf_stat stat;
} __attribute__((packed)) gf_fop_stat_rsp_t;


typedef struct {
	uint64_t ino;
	uint32_t size;
	char     path[0]; /* NULL terminated */
} __attribute__((packed)) gf_fop_readlink_req_t;
typedef struct {
	char     path[0]; /* NULL terminated */
} __attribute__((packed)) gf_fop_readlink_rsp_t;


typedef struct {
	uint64_t dev;
	uint32_t mode;
	char     path[0]; /* NULL terminated */
} __attribute__((packed)) gf_fop_mknod_req_t;
typedef struct {
	struct gf_stat stat;
} __attribute__((packed)) gf_fop_mknod_rsp_t;


typedef struct {
	uint32_t mode;
	char     path[0]; /* NULL terminated */
} __attribute__((packed)) gf_fop_mkdir_req_t;
typedef struct {
	struct gf_stat stat;
} __attribute__((packed)) gf_fop_mkdir_rsp_t;


typedef struct {
	uint64_t ino;
	char     path[0];
} __attribute__((packed)) gf_fop_unlink_req_t;
typedef struct {
} __attribute__((packed)) gf_fop_unlink_rsp_t;


typedef struct {
	uint64_t ino;
	char     path[0];
} __attribute__((packed)) gf_fop_rmdir_req_t;
typedef struct {
} __attribute__((packed)) gf_fop_rmdir_rsp_t;


typedef struct {
	char     oldpath[0];
	char     newpath[0];
} __attribute__((packed)) gf_fop_symlink_req_t;
typedef struct {
	struct gf_stat stat;
}__attribute__((packed)) gf_fop_symlink_rsp_t;


typedef struct {
	uint64_t   oldino;
	uint64_t   newino;
	char       oldpath[0];
	char       newpath[0];
} __attribute__((packed)) gf_fop_rename_req_t;
typedef struct {
	struct gf_stat stat;
} __attribute__((packed)) gf_fop_rename_rsp_t;


typedef struct {
	uint64_t   oldino;
	char       oldpath[0];
	char       newpath[0];
}__attribute__((packed)) gf_fop_link_req_t;
typedef struct {
	struct gf_stat stat;
} __attribute__((packed)) gf_fop_link_rsp_t;


typedef struct {
	uint64_t  oldino;
	uint32_t  mode;
	char      path[0];
} __attribute__((packed)) gf_fop_chmod_req_t;
typedef struct {
	struct gf_stat stat;
} __attribute__((packed)) gf_fop_chmod_rsp_t;


typedef struct {
	uint64_t ino;
	uint32_t uid;
	uint32_t gid;
	char     path[0];
} __attribute__((packed)) gf_fop_chown_req_t;
typedef struct {
	struct gf_stat stat;
} __attribute__((packed)) gf_fop_chown_rsp_t;


typedef struct {
	uint64_t ino;
	uint64_t offset;
	char     path[0];
} __attribute__((packed)) gf_fop_truncate_req_t;
typedef struct {
	struct gf_stat stat;
} __attribute__((packed)) gf_fop_truncate_rsp_t;


typedef struct {
	uint64_t ino;
	uint32_t flags;
	char     path[0];
} __attribute__((packed)) gf_fop_open_req_t;
typedef struct {
	uint64_t fd;
} __attribute__((packed)) gf_fop_open_rsp_t;


typedef struct {
	uint64_t fd;
	uint64_t offset;
	uint32_t size;
} __attribute__((packed)) gf_fop_read_req_t;
typedef struct {
	struct gf_stat stat;
	char buf[0];
} __attribute__((packed)) gf_fop_read_rsp_t;


typedef struct {
	uint64_t fd;
	uint64_t offset;
	uint32_t size;
} __attribute__((packed)) gf_fop_write_req_t;
typedef struct {
	struct gf_stat stat;
} __attribute__((packed)) gf_fop_write_rsp_t;


typedef struct {
	uint64_t ino;
	char     path[0];
} __attribute__((packed)) gf_fop_statfs_req_t;
typedef struct {
	struct gf_statfs statfs;
} __attribute__((packed)) gf_fop_statfs_rsp_t;


typedef struct {
	uint64_t fd;
} __attribute__((packed)) gf_fop_flush_req_t;
typedef struct { } __attribute__((packed)) gf_fop_flush_rsp_t;


typedef struct {
	uint64_t fd;
} __attribute__((packed)) gf_fop_close_req_t;
typedef struct {
} __attribute__((packed)) gf_fop_close_rsp_t;


typedef struct fsync_req {
	uint64_t fd;
	uint32_t data;
} __attribute__((packed)) gf_fop_fsync_req_t;
typedef struct {
} __attribute__((packed)) gf_fop_fsync_rsp_t;


typedef struct {
	uint64_t ino;
	uint32_t flags;
	uint32_t dict_len;
	char     dict[0];
	char     path[0];
} __attribute__((packed)) gf_fop_setxattr_req_t;
typedef struct { } __attribute__((packed)) gf_fop_setxattr_rsp_t;

typedef struct {
	uint64_t fd;
	uint32_t flags;
	uint32_t dict_len;
	char     dict[0];
	char     path[0];
} __attribute__((packed)) gf_fop_xattrop_req_t;

typedef struct {
	uint32_t dict_len;
	char  dict[0];
} __attribute__((packed)) gf_fop_xattrop_rsp_t;


typedef struct {
	uint64_t ino;
	uint32_t name_len;
	char     path[0];
	char     name[0];
} __attribute__((packed)) gf_fop_getxattr_req_t;
typedef struct {
	uint32_t dict_len;
	char  dict[0];
} __attribute__((packed)) gf_fop_getxattr_rsp_t;


typedef struct {
	uint64_t ino;
	char     path[0];
	char     name[0];
} __attribute__((packed)) gf_fop_removexattr_req_t;
typedef struct { } __attribute__((packed)) gf_fop_removexattr_rsp_t;


typedef struct {
	uint64_t ino;
	char     path[0];
} __attribute__((packed)) gf_fop_opendir_req_t;
typedef struct {
	uint64_t fd;
} __attribute__((packed)) gf_fop_opendir_rsp_t;


typedef struct fsyncdir_req {
	uint64_t fd;
	int32_t  data;
	char     path[0];
} __attribute__((packed)) gf_fop_fsyncdir_req_t;
typedef struct {
} __attribute__((packed)) gf_fop_fsyncdir_rsp_t;


typedef struct {
	uint64_t fd;
	uint64_t offset;
	uint32_t size;
} __attribute__((packed)) gf_fop_readdir_req_t;
typedef struct {
	uint32_t size;
	char     buf[0];
} __attribute__((packed)) gf_fop_readdir_rsp_t;


typedef struct {
	uint64_t fd;
} __attribute__((packed)) gf_fop_closedir_req_t;
typedef struct {
} __attribute__((packed)) gf_fop_closedir_rsp_t;


typedef struct  {
	uint64_t ino;
	uint32_t mask;
	char     path[0];
} __attribute__((packed)) gf_fop_access_req_t;
typedef struct {
} __attribute__((packed)) gf_fop_access_rsp_t;


typedef struct {
	uint32_t flags;
	uint32_t mode;
	char     path[0];
} __attribute__((packed)) gf_fop_create_req_t;
typedef struct {
	struct gf_stat stat;
	uint64_t       fd;
} __attribute__((packed)) gf_fop_create_rsp_t;



typedef struct {
	uint64_t fd;
	uint64_t offset;
} __attribute__((packed)) gf_fop_ftruncate_req_t;
typedef struct {
	struct gf_stat stat;
} __attribute__((packed)) gf_fop_ftruncate_rsp_t;


typedef struct {
	uint64_t fd;
} __attribute__((packed)) gf_fop_fstat_req_t;
typedef struct {
	struct gf_stat stat;
} __attribute__((packed)) gf_fop_fstat_rsp_t;


typedef struct {
	uint64_t fd;
	uint32_t cmd;
	uint32_t type;
	struct gf_flock flock;
} __attribute__((packed)) gf_fop_lk_req_t;
typedef struct {
	struct gf_flock flock;
} __attribute__((packed)) gf_fop_lk_rsp_t;

typedef struct {
	uint64_t ino;
	uint64_t fd;
	uint32_t cmd;
	uint32_t type;
	char     path[0];
	struct gf_flock flock;
} __attribute__((packed)) gf_fop_gf_file_lk_req_t;
typedef struct {
} __attribute__((packed)) gf_fop_gf_file_lk_rsp_t;


typedef struct {
	uint64_t  ino;
	uint32_t  cmd;
	uint32_t  type;
	char      path[0];
	char      basename[0];
} __attribute__((packed)) gf_fop_gf_dir_lk_req_t;
typedef struct {
} __attribute__((packed)) gf_fop_gf_dir_lk_rsp_t;

typedef struct {
	uint64_t           ino;
	struct gf_timespec tv[2];
	char               path[0];
} __attribute__((packed)) gf_fop_utimens_req_t;
typedef struct {
	struct gf_stat stat;
} __attribute__((packed)) gf_fop_utimens_rsp_t;


typedef struct {
	uint64_t  fd;
	uint32_t  mode;
} __attribute__((packed)) gf_fop_fchmod_req_t;
typedef struct {
	struct gf_stat stat;
} __attribute__((packed)) gf_fop_fchmod_rsp_t;


typedef struct {
	uint64_t fd;
	uint32_t uid;
	uint32_t gid;
} __attribute__((packed)) gf_fop_fchown_req_t;
typedef struct {
	struct gf_stat stat;
} __attribute__((packed)) gf_fop_fchown_rsp_t;


typedef struct {
	uint64_t ino;
	uint32_t flags;
	char     path[0];
} __attribute__((packed)) gf_fop_lookup_req_t;
typedef struct {
	struct gf_stat stat;
	uint32_t       dict_len;
	char           dict[0];
} __attribute__((packed)) gf_fop_lookup_rsp_t;


typedef struct {
	uint64_t ino;
} __attribute__((packed)) gf_fop_forget_req_t;
typedef struct { } __attribute__((packed)) gf_fop_forget_rsp_t;


typedef struct {
	uint64_t fd;
	uint32_t flags;
	uint32_t count;
	char     buf[0];
} __attribute__((packed)) gf_fop_setdents_req_t;
typedef struct { } __attribute__((packed)) gf_fop_setdents_rsp_t;


typedef struct {
	char path[0];
} __attribute__((packed)) gf_fop_rmelem_req_t;
typedef struct {
} __attribute__((packed)) gf_fop_rmelem_rsp_t;


typedef struct {
	uint64_t fd;
	char path[0];
} __attribute__((packed)) gf_fop_incver_req_t;
typedef struct {
} __attribute__((packed)) gf_fop_incver_rsp_t;


typedef struct {
	uint64_t fd;
	uint64_t offset;
	uint32_t size;
	uint32_t flags;
} __attribute__((packed)) gf_fop_getdents_req_t;
typedef struct {
	uint32_t count;
	char buf[0];
} __attribute__((packed)) gf_fop_getdents_rsp_t;


typedef struct {
	uint64_t  ino;
	uint32_t  flag;
	char      path[0];
} __attribute__((packed)) gf_fop_checksum_req_t;
typedef struct {
	unsigned char fchecksum[0];
	unsigned char dchecksum[0];
} __attribute__((packed)) gf_fop_checksum_rsp_t;


typedef struct {
	char name[0];
} __attribute__((packed)) gf_mop_lock_req_t;
typedef struct {} __attribute__((packed)) gf_mop_lock_rsp_t;

typedef struct {
	char name[0];
} __attribute__((packed)) gf_mop_unlock_req_t;
typedef struct {} __attribute__((packed)) gf_mop_unlock_rsp_t;

typedef struct {
	char pattern[0];
} __attribute__((packed)) gf_mop_listlocks_req_t;
typedef struct {} __attribute__((packed)) gf_mop_listlocks_rsp_t;

typedef struct {
	uint32_t  flags;
} __attribute__((packed)) gf_mop_stats_req_t;
typedef struct {
	char buf[0];
} __attribute__((packed)) gf_mop_stats_rsp_t;

typedef struct {
} __attribute__((packed)) gf_mop_getspec_req_t;
typedef struct {
	char spec[0];
} __attribute__((packed)) gf_mop_getspec_rsp_t;


typedef struct {
	char buf[0];
} __attribute__((packed)) gf_mop_setvolume_req_t;
typedef struct {
	char buf[0];
} __attribute__((packed)) gf_mop_setvolume_rsp_t;



typedef struct {
	uint32_t pid;
	uint32_t uid;
	uint32_t gid;
} __attribute__ ((packed)) gf_hdr_req_t;


typedef struct {
	uint32_t op_ret;
	uint32_t op_errno;
} __attribute__ ((packed)) gf_hdr_rsp_t;


typedef struct {
	uint64_t callid;
	uint32_t type;
	uint32_t op;
	uint32_t size;
	union {
		gf_hdr_req_t req;
		gf_hdr_rsp_t rsp;
	} __attribute__ ((packed));
} __attribute__ ((packed)) gf_hdr_common_t;


static inline gf_hdr_common_t *
__gf_hdr_new (int size)
{
	gf_hdr_common_t *hdr = NULL;

	/* TODO: use mem-pool */
	hdr = calloc (sizeof (gf_hdr_common_t) + size, 1);

	if (!hdr) {
		return NULL;
	}

	hdr->size = hton32 (size);

	return hdr;
}


#define gf_hdr_len(type, x) (sizeof (gf_hdr_common_t) + sizeof (*type) + x)
#define gf_hdr_new(type, x) __gf_hdr_new (sizeof (*type) + x)


static inline void *
gf_param (gf_hdr_common_t *hdr)
{
	return ((void *)hdr) + sizeof (*hdr);
}

#endif
