/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __COMPAT_H__
#define __COMPAT_H__

#include <stdint.h>

#ifndef LLONG_MAX
#define LLONG_MAX __LONG_LONG_MAX__ /* compat with old gcc */
#endif /* LLONG_MAX */


#ifdef GF_LINUX_HOST_OS

#define UNIX_PATH_MAX 108

#include <sys/un.h>
#include <linux/limits.h>
#include <sys/xattr.h>
#include <linux/xattr.h>
#include <endian.h>
#ifdef HAVE_LINUX_FALLOC_H
#include <linux/falloc.h>
#endif

#ifdef HAVE_ENDIAN_H
#include <endian.h>
#endif

#ifndef _PATH_UMOUNT
#define _PATH_UMOUNT "/bin/umount"
#endif
#define GF_XATTR_NAME_MAX       XATTR_NAME_MAX
#endif /* GF_LINUX_HOST_OS */

#ifdef HAVE_XATTR_H
#include <sys/xattr.h>
#endif

/*
 * Define the fallocate flags in case we do not have the header. This also
 * accounts for older systems that do not define FALLOC_FL_PUNCH_HOLE.
 */

#ifndef FALLOC_FL_KEEP_SIZE
#define FALLOC_FL_KEEP_SIZE     0x01 /* default is extend size */
#endif
#ifndef FALLOC_FL_PUNCH_HOLE
#define FALLOC_FL_PUNCH_HOLE    0x02 /* de-allocates range */
#endif
#ifndef FALLOC_FL_ZERO_RANGE
#define FALLOC_FL_ZERO_RANGE    0x10 /* zeroes out range */
#endif

#ifndef HAVE_LLISTXATTR

/* This part is valid only incase of old glibc which doesn't support
 * 'llistxattr()' system calls.
 */

#define lremovexattr(path,key) removexattr(path,key)
#define llistxattr(path,key,size)  listxattr(path,key,size)
#define lgetxattr(path, key, value, size) getxattr(path,key,value,size)
#define lsetxattr(path,key,value,size,flags) setxattr(path,key,value,size,flags)

#endif /* HAVE_LLISTXATTR */


#ifdef GF_DARWIN_HOST_OS
#include <machine/endian.h>
#include <libkern/OSByteOrder.h>
#include <sys/xattr.h>

#define htobe16(x) OSSwapHostToBigInt16(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)

#define htobe32(x) OSSwapHostToBigInt32(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)

#define htobe64(x) OSSwapHostToBigInt64(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)

#endif


#ifdef GF_BSD_HOST_OS
/* In case of FreeBSD and NetBSD */

#define UNIX_PATH_MAX 104
#include <sys/types.h>

#include <sys/un.h>
#include <sys/endian.h>
#include <sys/extattr.h>
#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif /* HAVE_SYS_XATTR_H */
#include <limits.h>

#include <libgen.h>

#ifndef XATTR_CREATE
enum {
        ATTR_CREATE = 1,
#define XATTR_CREATE ATTR_CREATE
        ATTR_REPLACE = 2
#define XATTR_REPLACE ATTR_REPLACE
};
#endif /* XATTR_CREATE */


#ifndef sighandler_t
#define sighandler_t sig_t
#endif

#ifdef __FreeBSD__
#undef ino_t
#define ino_t uint64_t
#include <sys/types.h>
#include <sys/extattr.h>
/* Using NAME_MAX since EXTATTR_MAXNAMELEN is inside a preprocessor conditional
 * for the kernel
 */
#define GF_XATTR_NAME_MAX       NAME_MAX
#endif /* __FreeBSD__ */

#ifdef __NetBSD__
#define GF_XATTR_NAME_MAX       XATTR_NAME_MAX
#endif

#ifndef ino64_t
#define ino64_t ino_t
#endif

#ifndef EUCLEAN
#define EUCLEAN 0
#endif

#include <netinet/in.h>
#ifndef s6_addr16
#define s6_addr16 __u6_addr.__u6_addr16
#endif
#ifndef s6_addr32
#define s6_addr32 __u6_addr.__u6_addr32
#endif

#ifndef LOGIN_NAME_MAX
#define LOGIN_NAME_MAX 256
#endif

/* Posix dictates NAME_MAX to be used */
# ifndef NAME_MAX
#  ifdef  MAXNAMLEN
#   define NAME_MAX MAXNAMLEN
#  else
#   define NAME_MAX 255
#  endif
# endif

#define F_GETLK64       F_GETLK
#define F_SETLK64       F_SETLK
#define F_SETLKW64      F_SETLKW
#define FALLOC_FL_KEEP_SIZE     0x01 /* default is extend size */
#define FALLOC_FL_PUNCH_HOLE    0x02 /* de-allocates range */
#define FALLOC_FL_ZERO_RANGE    0x10 /* zeroes out range */

#ifndef _PATH_UMOUNT
  #define _PATH_UMOUNT "/sbin/umount"
#endif
#endif /* GF_BSD_HOST_OS */

#ifdef GF_DARWIN_HOST_OS
#include <machine/endian.h>
#include <libkern/OSByteOrder.h>

#define htobe16(x) OSSwapHostToBigInt16(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)

#define htobe32(x) OSSwapHostToBigInt32(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)

#define htobe64(x) OSSwapHostToBigInt64(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)

#define UNIX_PATH_MAX 104
/* OSX Yosemite now has this defined */
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif
#include <sys/types.h>

#include <sys/un.h>
#include <sys/xattr.h>
#include <limits.h>

#include <libgen.h>


#if __DARWIN_64_BIT_INO_T == 0
#    error '64 bit ino_t is must for GlusterFS to work, Compile with "CFLAGS=-D__DARWIN_64_BIT_INO_T"'
#endif /* __DARWIN_64_BIT_INO_T */


#if __DARWIN_64_BIT_INO_T == 0
#    error '64 bit ino_t is must for GlusterFS to work, Compile with "CFLAGS=-D__DARWIN_64_BIT_INO_T"'
#endif /* __DARWIN_64_BIT_INO_T */

#ifndef sighandler_t
#define sighandler_t sig_t
#endif

#ifndef EUCLEAN
#define EUCLEAN 0
#endif

#include <netinet/in.h>
#ifndef s6_addr16
#define s6_addr16 __u6_addr.__u6_addr16
#endif
#ifndef s6_addr32
#define s6_addr32 __u6_addr.__u6_addr32
#endif

/* Posix dictates NAME_MAX to be used */
# ifndef NAME_MAX
#  ifdef  MAXNAMLEN
#   define NAME_MAX MAXNAMLEN
#  else
#   define NAME_MAX 255
#  endif
# endif

#define F_GETLK64       F_GETLK
#define F_SETLK64       F_SETLK
#define F_SETLKW64      F_SETLKW

#ifndef FTW_CONTINUE
  #define FTW_CONTINUE 0
#endif

#ifndef _PATH_UMOUNT
  #define _PATH_UMOUNT "/sbin/umount"
#endif
#endif /* GF_DARWIN_HOST_OS */

#ifdef GF_SOLARIS_HOST_OS

#define UNIX_PATH_MAX 108
#define EUCLEAN 117

#include <sys/un.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <libgen.h>
#include <sys/mkdev.h>

#ifndef lchmod
#define lchmod chmod
#endif

#define lgetxattr(path, key, value, size) solaris_getxattr(path,key,value,size)
enum {
        ATTR_CREATE = 1,
#define XATTR_CREATE ATTR_CREATE
        ATTR_REPLACE = 2
#define XATTR_REPLACE ATTR_REPLACE
};

/* This patch is not present in Solaris 10 and before */
#ifndef dirfd
#define dirfd(dirp)   ((dirp)->dd_fd)
#endif

/* Posix dictates NAME_MAX to be used */
# ifndef NAME_MAX
#  ifdef  MAXNAMLEN
#   define NAME_MAX MAXNAMLEN
#  else
#   define NAME_MAX 255
#  endif
# endif

#include <netinet/in.h>
#ifndef s6_addr16
#define S6_ADDR16(x)    ((uint16_t*) ((char*)&(x).s6_addr))
#endif
#ifndef s6_addr32
#define s6_addr32       _S6_un._S6_u32
#endif

#define lutimes(filename,times)              utimes(filename,times)

#ifndef SEEK_SET
#define SEEK_SET 0
#endif

enum {
        DT_UNKNOWN = 0,
# define DT_UNKNOWN	DT_UNKNOWN
        DT_FIFO = 1,
# define DT_FIFO	DT_FIFO
        DT_CHR = 2,
# define DT_CHR		DT_CHR
        DT_DIR = 4,
# define DT_DIR		DT_DIR
        DT_BLK = 6,
# define DT_BLK		DT_BLK
        DT_REG = 8,
# define DT_REG		DT_REG
        DT_LNK = 10,
# define DT_LNK		DT_LNK
        DT_SOCK = 12,
# define DT_SOCK	DT_SOCK
        DT_WHT = 14
# define DT_WHT		DT_WHT
};

#ifndef _PATH_MOUNTED
 #define _PATH_MOUNTED "/etc/mtab"
#endif
#ifndef _PATH_UMOUNT
 #define _PATH_UMOUNT "/sbin/umount"
#endif

#ifndef O_ASYNC
  #ifdef FASYNC
    #define O_ASYNC FASYNC
  #else
    #define O_ASYNC 0
  #endif
#endif

#ifndef FTW_CONTINUE
  #define FTW_CONTINUE 0
#endif

int asprintf(char **string_ptr, const char *format, ...);

int vasprintf (char **result, const char *format, va_list args);
char* strsep(char** str, const char* delims);
int solaris_listxattr(const char *path, char *list, size_t size);
int solaris_removexattr(const char *path, const char* key);
int solaris_getxattr(const char *path, const char* key,
                     char *value, size_t size);
int solaris_setxattr(const char *path, const char* key, const char *value,
                     size_t size, int flags);
int solaris_fgetxattr(int fd, const char* key,
                      char *value, size_t size);
int solaris_fsetxattr(int fd, const char* key, const char *value,
                      size_t size, int flags);
int solaris_flistxattr(int fd, char *list, size_t size);

int solaris_rename (const char *oldpath, const char *newpath);

int solaris_unlink (const char *pathname);

char *mkdtemp (char *temp);

#define GF_SOLARIS_XATTR_DIR ".glusterfs_xattr_inode"

int solaris_xattr_resolve_path (const char *real_path, char **path);

#endif /* GF_SOLARIS_HOST_OS */

#ifndef HAVE_ARGP
#include "argp.h"
#else
#include <argp.h>
#endif /* HAVE_ARGP */

#ifndef HAVE_STRNLEN
size_t strnlen(const char *string, size_t maxlen);
#endif /* STRNLEN */

#ifndef strdupa
#define strdupa(s)                                                      \
        (__extension__                                                  \
         ({                                                             \
                 __const char *__old = (s);                             \
                 size_t __len = strlen (__old) + 1;                     \
                 char *__new = (char *) __builtin_alloca (__len);       \
                 (char *) memcpy (__new, __old, __len);                 \
         }))
#endif

#define GF_DIR_ALIGN(x) (((x) + sizeof (uint64_t) - 1) & ~(sizeof (uint64_t) - 1))

#include <sys/types.h>
#include <dirent.h>

static inline int32_t
dirent_size (struct dirent *entry)
{
        int32_t size = -1;

#ifdef GF_BSD_HOST_OS
        size = GF_DIR_ALIGN (24 /* FIX MEEEE!!! */ + entry->d_namlen);
#endif
#ifdef GF_DARWIN_HOST_OS
        size = GF_DIR_ALIGN (24 /* FIX MEEEE!!! */ + entry->d_namlen);
#endif
#ifdef GF_LINUX_HOST_OS
        size = GF_DIR_ALIGN (24 /* FIX MEEEE!!! */ + entry->d_reclen);
#endif
#ifdef GF_SOLARIS_HOST_OS
        size = GF_DIR_ALIGN (24 /* FIX MEEEE!!! */ + entry->d_reclen);
#endif
        return size;
}

#ifdef THREAD_UNSAFE_BASENAME
char *basename_r(const char *);
#define basename(path) basename_r(path)
#endif /* THREAD_UNSAFE_BASENAME */

#ifdef THREAD_UNSAFE_DIRNAME
char *dirname_r(char *path);
#define dirname(path) dirname_r(path)
#endif /* THREAD_UNSAFE_DIRNAME */

int gf_mkostemp (char *tmpl, int suffixlen, int flags);

#ifdef HAVE_STRUCT_STAT_ST_ATIM_TV_NSEC
/* Linux, Solaris, Cygwin */
#define ST_ATIM_SEC(stbuf) ((stbuf)->st_atim.tv_sec)
#define ST_CTIM_SEC(stbuf) ((stbuf)->st_ctim.tv_sec)
#define ST_MTIM_SEC(stbuf) ((stbuf)->st_mtim.tv_sec)
#define ST_ATIM_SEC_SET(stbuf, val) ((stbuf)->st_atim.tv_sec = (val))
#define ST_MTIM_SEC_SET(stbuf, val) ((stbuf)->st_mtim.tv_sec = (val))
#define ST_CTIM_SEC_SET(stbuf, val) ((stbuf)->st_ctim.tv_sec = (val))
#define ST_ATIM_NSEC(stbuf) ((stbuf)->st_atim.tv_nsec)
#define ST_CTIM_NSEC(stbuf) ((stbuf)->st_ctim.tv_nsec)
#define ST_MTIM_NSEC(stbuf) ((stbuf)->st_mtim.tv_nsec)
#define ST_ATIM_NSEC_SET(stbuf, val) ((stbuf)->st_atim.tv_nsec = (val))
#define ST_MTIM_NSEC_SET(stbuf, val) ((stbuf)->st_mtim.tv_nsec = (val))
#define ST_CTIM_NSEC_SET(stbuf, val) ((stbuf)->st_ctim.tv_nsec = (val))
#elif defined(HAVE_STRUCT_STAT_ST_ATIMESPEC_TV_NSEC)
/* FreeBSD, NetBSD */
#define ST_ATIM_SEC(stbuf) ((stbuf)->st_atimespec.tv_sec)
#define ST_CTIM_SEC(stbuf) ((stbuf)->st_ctimespec.tv_sec)
#define ST_MTIM_SEC(stbuf) ((stbuf)->st_mtimespec.tv_sec)
#define ST_ATIM_SEC_SET(stbuf, val) ((stbuf)->st_atimespec.tv_sec = (val))
#define ST_MTIM_SEC_SET(stbuf, val) ((stbuf)->st_mtimespec.tv_sec = (val))
#define ST_CTIM_SEC_SET(stbuf, val) ((stbuf)->st_ctimespec.tv_sec = (val))
#define ST_ATIM_NSEC(stbuf) ((stbuf)->st_atimespec.tv_nsec)
#define ST_CTIM_NSEC(stbuf) ((stbuf)->st_ctimespec.tv_nsec)
#define ST_MTIM_NSEC(stbuf) ((stbuf)->st_mtimespec.tv_nsec)
#define ST_ATIM_NSEC_SET(stbuf, val) ((stbuf)->st_atimespec.tv_nsec = (val))
#define ST_MTIM_NSEC_SET(stbuf, val) ((stbuf)->st_mtimespec.tv_nsec = (val))
#define ST_CTIM_NSEC_SET(stbuf, val) ((stbuf)->st_ctimespec.tv_nsec = (val))
#else
#define ST_ATIM_NSEC(stbuf) (0)
#define ST_CTIM_NSEC(stbuf) (0)
#define ST_MTIM_NSEC(stbuf) (0)
#define ST_ATIM_NSEC_SET(stbuf, val) do { } while (0);
#define ST_MTIM_NSEC_SET(stbuf, val) do { } while (0);
#define ST_CTIM_NSEC_SET(stbuf, val) do { } while (0);
#endif

#ifndef IXDR_GET_LONG
#define IXDR_GET_LONG(buf) ((long)IXDR_GET_U_INT32(buf))
#endif

#ifndef IXDR_PUT_LONG
#define IXDR_PUT_LONG(buf, v) ((long)IXDR_PUT_INT32(buf, (long)(v)))
#endif

#ifndef IXDR_GET_U_LONG
#define IXDR_GET_U_LONG(buf)          ((u_long)IXDR_GET_LONG(buf))
#endif

#ifndef IXDR_PUT_U_LONG
#define IXDR_PUT_U_LONG(buf, v)       IXDR_PUT_LONG(buf, (long)(v))
#endif

#if defined(__GNUC__) && !defined(RELAX_POISONING)
/* Use run API, see run.h */
#include <stdlib.h> /* system(), mkostemp() */
#include <stdio.h> /* popen() */
#pragma GCC poison system mkostemp popen
#endif

int gf_umount_lazy(char *xlname, char *path, int rmdir);

#ifndef GF_XATTR_NAME_MAX
#error 'Please define GF_XATTR_NAME_MAX for your OS distribution.'
#endif

#endif /* __COMPAT_H__ */
