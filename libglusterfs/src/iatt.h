/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef _IATT_H
#define _IATT_H

#if defined(GF_LINUX_HOST_OS)
#include <sys/sysmacros.h> /* for makedev(3), major(3), minor(3) */
#endif
#include <sys/types.h>
#include <sys/stat.h> /* for iatt <--> stat conversions */
#include <unistd.h>

#include "compat.h"
#include "compat-uuid.h"

typedef enum {
        IA_INVAL = 0,
        IA_IFREG,
        IA_IFDIR,
        IA_IFLNK,
        IA_IFBLK,
        IA_IFCHR,
        IA_IFIFO,
        IA_IFSOCK
} ia_type_t;


typedef struct {
        uint8_t    suid:1;
        uint8_t    sgid:1;
        uint8_t    sticky:1;
        struct {
                uint8_t    read:1;
                uint8_t    write:1;
                uint8_t    exec:1;
        } owner, group, other;
} ia_prot_t;


struct iatt {
        uint64_t     ia_ino;        /* inode number */
        uuid_t       ia_gfid;
        uint64_t     ia_dev;        /* backing device ID */
        ia_type_t    ia_type;       /* type of file */
        ia_prot_t    ia_prot;       /* protection */
        uint32_t     ia_nlink;      /* Link count */
        uint32_t     ia_uid;        /* user ID of owner */
        uint32_t     ia_gid;        /* group ID of owner */
        uint64_t     ia_rdev;       /* device ID (if special file) */
        uint64_t     ia_size;       /* file size in bytes */
        uint32_t     ia_blksize;    /* blocksize for filesystem I/O */
        uint64_t     ia_blocks;     /* number of 512B blocks allocated */
        uint32_t     ia_atime;      /* last access time */
        uint32_t     ia_atime_nsec;
        uint32_t     ia_mtime;      /* last modification time */
        uint32_t     ia_mtime_nsec;
        uint32_t     ia_ctime;      /* last status change time */
        uint32_t     ia_ctime_nsec;
};


#define IA_ISREG(t) (t == IA_IFREG)
#define IA_ISDIR(t) (t == IA_IFDIR)
#define IA_ISLNK(t) (t == IA_IFLNK)
#define IA_ISBLK(t) (t == IA_IFBLK)
#define IA_ISCHR(t) (t == IA_IFCHR)
#define IA_ISFIFO(t) (t == IA_IFIFO)
#define IA_ISSOCK(t) (t == IA_IFSOCK)
#define IA_ISINVAL(t) (t == IA_INVAL)

#define IA_PROT_RUSR(prot)      ((prot).owner.read == 1)
#define IA_PROT_WUSR(prot)      ((prot).owner.write == 1)
#define IA_PROT_XUSR(prot)      ((prot).owner.exec == 1)

#define IA_PROT_RGRP(prot)      ((prot).group.read == 1)
#define IA_PROT_WGRP(prot)      ((prot).group.write == 1)
#define IA_PROT_XGRP(prot)      ((prot).group.exec == 1)

#define IA_PROT_ROTH(prot)      ((prot).other.read == 1)
#define IA_PROT_WOTH(prot)      ((prot).other.write == 1)
#define IA_PROT_XOTH(prot)      ((prot).other.exec == 1)

#define IA_PROT_SUID(prot)      ((prot).suid == 1)
#define IA_PROT_SGID(prot)      ((prot).sgid == 1)
#define IA_PROT_STCKY(prot)     ((prot).sticky == 1)

#define IA_FILE_OR_DIR(t)       (IA_ISREG(t) || IA_ISDIR(t))

static inline uint32_t
ia_major (uint64_t ia_dev)
{
        return (uint32_t) (ia_dev >> 32);
}


static inline uint32_t
ia_minor (uint64_t ia_dev)
{
        return (uint32_t) (ia_dev & 0xffffffff);
}


static inline uint64_t
ia_makedev (uint32_t ia_maj, uint32_t ia_min)
{
        return ((((uint64_t) ia_maj) << 32) | ia_min);
}


static inline ia_prot_t
ia_prot_from_st_mode (mode_t mode)
{
        ia_prot_t ia_prot = {0, };

        if (mode & S_ISUID)
                ia_prot.suid = 1;
        if (mode & S_ISGID)
                ia_prot.sgid = 1;
        if (mode & S_ISVTX)
                ia_prot.sticky = 1;

        if (mode & S_IRUSR)
                ia_prot.owner.read = 1;
        if (mode & S_IWUSR)
                ia_prot.owner.write = 1;
        if (mode & S_IXUSR)
                ia_prot.owner.exec = 1;

        if (mode & S_IRGRP)
                ia_prot.group.read = 1;
        if (mode & S_IWGRP)
                ia_prot.group.write = 1;
        if (mode & S_IXGRP)
                ia_prot.group.exec = 1;

        if (mode & S_IROTH)
                ia_prot.other.read = 1;
        if (mode & S_IWOTH)
                ia_prot.other.write = 1;
        if (mode & S_IXOTH)
                ia_prot.other.exec = 1;

        return ia_prot;
}


static inline ia_type_t
ia_type_from_st_mode (mode_t mode)
{
        ia_type_t type = IA_INVAL;

        if (S_ISREG (mode))
                type = IA_IFREG;
        if (S_ISDIR (mode))
                type = IA_IFDIR;
        if (S_ISLNK (mode))
                type = IA_IFLNK;
        if (S_ISBLK (mode))
                type = IA_IFBLK;
        if (S_ISCHR (mode))
                type = IA_IFCHR;
        if (S_ISFIFO (mode))
                type = IA_IFIFO;
        if (S_ISSOCK (mode))
                type = IA_IFSOCK;

        return type;
}


static inline mode_t
st_mode_from_ia (ia_prot_t prot, ia_type_t type)
{
        mode_t    st_mode = 0;
        uint32_t  type_bit = 0;
        uint32_t  prot_bit = 0;

        switch (type) {
        case IA_IFREG:
                type_bit = S_IFREG;
                break;
        case IA_IFDIR:
                type_bit = S_IFDIR;
                break;
        case IA_IFLNK:
                type_bit = S_IFLNK;
                break;
        case IA_IFBLK:
                type_bit = S_IFBLK;
                break;
        case IA_IFCHR:
                type_bit = S_IFCHR;
                break;
        case IA_IFIFO:
                type_bit = S_IFIFO;
                break;
        case IA_IFSOCK:
                type_bit = S_IFSOCK;
                break;
        case IA_INVAL:
                break;
        }

        if (prot.suid)
                prot_bit |= S_ISUID;
        if (prot.sgid)
                prot_bit |= S_ISGID;
        if (prot.sticky)
                prot_bit |= S_ISVTX;

        if (prot.owner.read)
                prot_bit |= S_IRUSR;
        if (prot.owner.write)
                prot_bit |= S_IWUSR;
        if (prot.owner.exec)
                prot_bit |= S_IXUSR;

        if (prot.group.read)
                prot_bit |= S_IRGRP;
        if (prot.group.write)
                prot_bit |= S_IWGRP;
        if (prot.group.exec)
                prot_bit |= S_IXGRP;

        if (prot.other.read)
                prot_bit |= S_IROTH;
        if (prot.other.write)
                prot_bit |= S_IWOTH;
        if (prot.other.exec)
                prot_bit |= S_IXOTH;

        st_mode = (type_bit | prot_bit);

        return st_mode;
}


static inline int
iatt_from_stat (struct iatt *iatt, struct stat *stat)
{
        iatt->ia_dev        = stat->st_dev;
        iatt->ia_ino        = stat->st_ino;

        iatt->ia_type       = ia_type_from_st_mode (stat->st_mode);
        iatt->ia_prot       = ia_prot_from_st_mode (stat->st_mode);

        iatt->ia_nlink      = stat->st_nlink;
        iatt->ia_uid        = stat->st_uid;
        iatt->ia_gid        = stat->st_gid;

        iatt->ia_rdev       = ia_makedev (major (stat->st_rdev),
                                          minor (stat->st_rdev));

        iatt->ia_size       = stat->st_size;
        iatt->ia_blksize    = stat->st_blksize;
        iatt->ia_blocks     = stat->st_blocks;

        /* There is a possibility that the backend FS (like XFS) can
           allocate blocks beyond EOF for better performance reasons, which
           results in 'st_blocks' with higher values than what is consumed by
           the file descriptor. This would break few logic inside GlusterFS,
           like quota behavior etc, thus we need the exact number of blocks
           which are consumed by the file to the higher layers inside GlusterFS.
           Currently, this logic won't work for sparse files (ie, file with
           holes)
        */
        {
                uint64_t maxblocks;

                maxblocks = (iatt->ia_size + 511) / 512;

                if (iatt->ia_blocks > maxblocks)
                        iatt->ia_blocks = maxblocks;
        }

        iatt->ia_atime      = stat->st_atime;
        iatt->ia_atime_nsec = ST_ATIM_NSEC (stat);

        iatt->ia_mtime      = stat->st_mtime;
        iatt->ia_mtime_nsec = ST_MTIM_NSEC (stat);

        iatt->ia_ctime      = stat->st_ctime;
        iatt->ia_ctime_nsec = ST_CTIM_NSEC (stat);

        return 0;
}


static inline int
iatt_to_stat (struct iatt *iatt, struct stat *stat)
{
        stat->st_dev        = iatt->ia_dev;
        stat->st_ino        = iatt->ia_ino;

        stat->st_mode       = st_mode_from_ia (iatt->ia_prot, iatt->ia_type);

        stat->st_nlink      = iatt->ia_nlink;
        stat->st_uid        = iatt->ia_uid;
        stat->st_gid        = iatt->ia_gid;

        stat->st_rdev       = makedev (ia_major (iatt->ia_rdev),
                                       ia_minor (iatt->ia_rdev));

        stat->st_size       = iatt->ia_size;
        stat->st_blksize    = iatt->ia_blksize;
        stat->st_blocks     = iatt->ia_blocks;

        stat->st_atime      = iatt->ia_atime;
        ST_ATIM_NSEC_SET (stat, iatt->ia_atime_nsec);

        stat->st_mtime      = iatt->ia_mtime;
        ST_MTIM_NSEC_SET (stat, iatt->ia_mtime_nsec);

        stat->st_ctime      = iatt->ia_ctime;
        ST_CTIM_NSEC_SET (stat, iatt->ia_ctime_nsec);

        return 0;
}


#endif /* _IATT_H */
