/*
    This file defines the kernel interface of FUSE
    Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.

    This -- and only this -- header file may also be distributed under
    the terms of the BSD Licence as follows:

    Copyright (C) 2001-2007 Miklos Szeredi. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
    OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
    OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
    SUCH DAMAGE.
*/

#ifndef linux
#include <sys/types.h>
#define __u64 uint64_t
#define __u32 uint32_t
#define __s32 int32_t
#else
#include <asm/types.h>
#include <linux/major.h>
#endif

/** Version number of this interface */
#define FUSE_KERNEL_VERSION 7

/** Minor version number of this interface */
#define FUSE_KERNEL_MINOR_VERSION 8

/** The node ID of the root inode */
#define FUSE_ROOT_ID 1

/** The major number of the fuse character device */
#define FUSE_MAJOR MISC_MAJOR

/** The minor number of the fuse character device */
#define FUSE_MINOR 229

/* Make sure all structures are padded to 64bit boundary, so 32bit
   userspace works under 64bit kernels */

struct fuse_attr {
    __u64 ino;
    __u64 size;
    __u64 blocks;
    __u64 atime;
    __u64 mtime;
    __u64 ctime;
    __u64 crtime;
    __u32 atimensec;
    __u32 mtimensec;
    __u32 ctimensec;
    __u32 crtimensec;
    __u32 mode;
    __u32 nlink;
    __u32 uid;
    __u32 gid;
    __u32 rdev;
    __u32 flags; /* file flags; see chflags(2) */
};

struct fuse_kstatfs {
    __u64 blocks;
    __u64 bfree;
    __u64 bavail;
    __u64 files;
    __u64 ffree;
    __u32 bsize;
    __u32 namelen;
    __u32 frsize;
    __u32 padding;
    __u32 spare[6];
};

struct fuse_file_lock {
    __u64 start;
    __u64 end;
    __u32 type;
    __u32 pid; /* tgid */
};

/**
 * Bitmasks for fuse_setattr_in.valid
 */
#define FATTR_MODE (1 << 0)
#define FATTR_UID (1 << 1)
#define FATTR_GID (1 << 2)
#define FATTR_SIZE (1 << 3)
#define FATTR_ATIME (1 << 4)
#define FATTR_MTIME (1 << 5)
#define FATTR_FH (1 << 6)
#define FATTR_CRTIME (1 << 28)
#define FATTR_CHGTIME (1 << 29)
#define FATTR_BKUPTIME (1 << 30)
#define FATTR_FLAGS (1 << 31)

/**
 * Flags returned by the OPEN request
 *
 * FOPEN_DIRECT_IO: bypass page cache for this open file
 * FOPEN_KEEP_CACHE: don't invalidate the data cache on open
 */
#define FOPEN_DIRECT_IO (1 << 0)
#define FOPEN_KEEP_CACHE (1 << 1)
#define FOPEN_PURGE_ATTR (1 << 30)
#define FOPEN_PURGE_UBC (1 << 31)

/**
 * INIT request/reply flags
 */
#define FUSE_ASYNC_READ (1 << 0)
#define FUSE_POSIX_LOCKS (1 << 1)
#define FUSE_CASE_INSENSITIVE (1 << 29)
#define FUSE_VOL_RENAME (1 << 30)
#define FUSE_XTIMES (1 << 31)

/**
 * Release flags
 */
#define FUSE_RELEASE_FLUSH (1 << 0)

enum fuse_opcode {
    FUSE_LOOKUP = 1,
    FUSE_FORGET = 2, /* no reply */
    FUSE_GETATTR = 3,
    FUSE_SETATTR = 4,
    FUSE_READLINK = 5,
    FUSE_SYMLINK = 6,
    FUSE_MKNOD = 8,
    FUSE_MKDIR = 9,
    FUSE_UNLINK = 10,
    FUSE_RMDIR = 11,
    FUSE_RENAME = 12,
    FUSE_LINK = 13,
    FUSE_OPEN = 14,
    FUSE_READ = 15,
    FUSE_WRITE = 16,
    FUSE_STATFS = 17,
    FUSE_RELEASE = 18,
    FUSE_FSYNC = 20,
    FUSE_SETXATTR = 21,
    FUSE_GETXATTR = 22,
    FUSE_LISTXATTR = 23,
    FUSE_REMOVEXATTR = 24,
    FUSE_FLUSH = 25,
    FUSE_INIT = 26,
    FUSE_OPENDIR = 27,
    FUSE_READDIR = 28,
    FUSE_RELEASEDIR = 29,
    FUSE_FSYNCDIR = 30,
    FUSE_GETLK = 31,
    FUSE_SETLK = 32,
    FUSE_SETLKW = 33,
    FUSE_ACCESS = 34,
    FUSE_CREATE = 35,
    FUSE_INTERRUPT = 36,
    FUSE_BMAP = 37,
    FUSE_DESTROY = 38,
    /*
    FUSE_IOCTL         = 39,
    FUSE_POLL          = 40,
    FUSE_NOTIFY_REPLY  = 41,
    FUSE_BATCH_FORGET  = 42,
    FUSE_FALLOCATE     = 43,
    FUSE_READDIRPLUS   = 44,
    */

    FUSE_SETVOLNAME = 61,
    FUSE_GETXTIMES = 62,
    FUSE_EXCHANGE = 63,
};

/* The read buffer is required to be at least 8k, but may be much larger */
#define FUSE_MIN_READ_BUFFER 8192

struct fuse_entry_out {
    __u64 nodeid;      /* Inode ID */
    __u64 generation;  /* Inode generation: nodeid:gen must
                          be unique for the fs's lifetime */
    __u64 entry_valid; /* Cache timeout for the name */
    __u64 attr_valid;  /* Cache timeout for the attributes */
    __u32 entry_valid_nsec;
    __u32 attr_valid_nsec;
    struct fuse_attr attr;
};

struct fuse_forget_in {
    __u64 nlookup;
};

struct fuse_attr_out {
    __u64 attr_valid; /* Cache timeout for the attributes */
    __u32 attr_valid_nsec;
    __u32 dummy;
    struct fuse_attr attr;
};

struct fuse_getxtimes_out {
    __u64 bkuptime;
    __u64 crtime;
    __u32 bkuptimensec;
    __u32 crtimensec;
};

struct fuse_mknod_in {
    __u32 mode;
    __u32 rdev;
};

struct fuse_mkdir_in {
    __u32 mode;
    __u32 padding;
};

struct fuse_rename_in {
    __u64 newdir;
};

struct fuse_exchange_in {
    __u64 olddir;
    __u64 newdir;
    __u64 options;
};

struct fuse_link_in {
    __u64 oldnodeid;
};

struct fuse_setattr_in {
    __u32 valid;
    __u32 padding;
    __u64 fh;
    __u64 size;
    __u64 unused1;
    __u64 atime;
    __u64 mtime;
    __u64 unused2;
    __u32 atimensec;
    __u32 mtimensec;
    __u32 unused3;
    __u32 mode;
    __u32 unused4;
    __u32 uid;
    __u32 gid;
    __u32 unused5;
    __u64 bkuptime;
    __u64 chgtime;
    __u64 crtime;
    __u32 bkuptimensec;
    __u32 chgtimensec;
    __u32 crtimensec;
    __u32 flags; /* file flags; see chflags(2) */
};

struct fuse_open_in {
    __u32 flags;
    __u32 mode;
};

struct fuse_open_out {
    __u64 fh;
    __u32 open_flags;
    __u32 padding;
};

struct fuse_release_in {
    __u64 fh;
    __u32 flags;
    __u32 release_flags;
    __u64 lock_owner;
};

struct fuse_flush_in {
    __u64 fh;
    __u32 unused;
    __u32 padding;
    __u64 lock_owner;
};

struct fuse_read_in {
    __u64 fh;
    __u64 offset;
    __u32 size;
    __u32 padding;
};

struct fuse_write_in {
    __u64 fh;
    __u64 offset;
    __u32 size;
    __u32 write_flags;
};

struct fuse_write_out {
    __u32 size;
    __u32 padding;
};

#define FUSE_COMPAT_STATFS_SIZE 48

struct fuse_statfs_out {
    struct fuse_kstatfs st;
};

struct fuse_fsync_in {
    __u64 fh;
    __u32 fsync_flags;
    __u32 padding;
};

struct fuse_setxattr_in {
    __u32 size;
    __u32 flags;
    __u32 position;
    __u32 padding;
};

struct fuse_getxattr_in {
    __u32 size;
    __u32 padding;
    __u32 position;
    __u32 padding2;
};

struct fuse_getxattr_out {
    __u32 size;
    __u32 padding;
};

struct fuse_lk_in {
    __u64 fh;
    __u64 owner;
    struct fuse_file_lock lk;
};

struct fuse_lk_out {
    struct fuse_file_lock lk;
};

struct fuse_access_in {
    __u32 mask;
    __u32 padding;
};

struct fuse_init_in {
    __u32 major;
    __u32 minor;
    __u32 max_readahead;
    __u32 flags;
};

struct fuse_init_out {
    __u32 major;
    __u32 minor;
    __u32 max_readahead;
    __u32 flags;
    __u32 unused;
    __u32 max_write;
};

struct fuse_interrupt_in {
    __u64 unique;
};

struct fuse_bmap_in {
    __u64 block;
    __u32 blocksize;
    __u32 padding;
};

struct fuse_bmap_out {
    __u64 block;
};

struct fuse_in_header {
    __u32 len;
    __u32 opcode;
    __u64 unique;
    __u64 nodeid;
    __u32 uid;
    __u32 gid;
    __u32 pid;
    __u32 padding;
};

struct fuse_out_header {
    __u32 len;
    __s32 error;
    __u64 unique;
};

struct fuse_dirent {
    __u64 ino;
    __u64 off;
    __u32 namelen;
    __u32 type;
    char name[0];
};

#define FUSE_NAME_OFFSET offsetof(struct fuse_dirent, name)
#define FUSE_DIRENT_ALIGN(x) (((x) + sizeof(__u64) - 1) & ~(sizeof(__u64) - 1))
#define FUSE_DIRENT_SIZE(d) FUSE_DIRENT_ALIGN(FUSE_NAME_OFFSET + (d)->namelen)
