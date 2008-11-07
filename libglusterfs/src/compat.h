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

#ifndef __COMPAT_H__
#define __COMPAT_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include "dict.h"

#ifndef LLONG_MAX
#define LLONG_MAX LONG_LONG_MAX /* compat with old gcc */
#endif /* LLONG_MAX */


#ifdef GF_LINUX_HOST_OS

#define UNIX_PATH_MAX 108

#include <sys/un.h>
#include <linux/limits.h>
#include <sys/xattr.h>
#include <endian.h>
#include <sys/sysmacros.h>

#define MAKEDEV(maj, min) makedev(maj, min)
#ifndef HAVE_LLISTXATTR

/* This part is valid only incase of old glibc which doesn't support 
 * 'llistxattr()' system calls.
 */

#define lremovexattr(path,key) removexattr(path,key)
#define llistxattr(path,key,size)  listxattr(path,key,size)
#define lgetxattr(path, key, value, size) getxattr(path,key,value,size)
#define lsetxattr(path,key,value,size,flags) setxattr(path,key,value,size,flags)

#endif /* HAVE_LLISTXATTR */
#endif /* GF_LINUX_HOST_OS */

#ifdef GF_BSD_HOST_OS 
/* In case of FreeBSD */

#define UNIX_PATH_MAX 104
#include <sys/types.h>

#include <sys/un.h>
#include <sys/endian.h>
#include <sys/extattr.h>
#include <limits.h>

#include <libgen.h>

enum {
        ATTR_CREATE = 1,
#define XATTR_CREATE ATTR_CREATE
        ATTR_REPLACE = 2
#define XATTR_REPLACE ATTR_REPLACE
};

#define MAKEDEV(maj, min) makedev(maj, min)

#define MAKEDEV(maj, min) makedev(maj, min)

#ifndef sighandler_t
#define sighandler_t sig_t
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
        
#define lremovexattr(path,key)               extattr_delete_link(path, EXTATTR_NAMESPACE_USER, key)
#define llistxattr(path,key,size)            extattr_list_link(path, EXTATTR_NAMESPACE_USER, key, size)
#define lgetxattr(path, key, value, size)    extattr_get_link(path, EXTATTR_NAMESPACE_USER, key, value, size)
#define lsetxattr(path,key,value,size,flags) extattr_set_link(path, EXTATTR_NAMESPACE_USER, key, value, size)
#define fgetxattr(fd,key,value,size)         extattr_get_fd(fd, EXTATTR_NAMESPACE_USER, key, value, size)
#define fsetxattr(fd,key,value,size,flag)    extattr_set_fd(fd, EXTATTR_NAMESPACE_USER, key, value, size)


#define F_GETLK64       F_GETLK
#define F_SETLK64       F_SETLK
#define F_SETLKW64      F_SETLKW

#endif /* GF_BSD_HOST_OS */

#ifdef GF_DARWIN_HOST_OS

#define UNIX_PATH_MAX 104
#include <sys/types.h>

#include <sys/un.h>
#include <machine/endian.h>
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

#define MAKEDEV(maj, min) makedev(maj, min)

#define MAKEDEV(maj, min) makedev(maj, min)

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

#define llistxattr(path,key,size)               listxattr(path,key,size,XATTR_NOFOLLOW)
#define lgetxattr(path,key,value,size)          getxattr(path,key,value,size,0,XATTR_NOFOLLOW)
#define lsetxattr(path,key,value,size,flags)    setxattr(path,key,value,size,flags,XATTR_NOFOLLOW)
#define lremovexattr(path,key)                  removexattr(path,key,XATTR_NOFOLLOW)
#define fgetxattr(path,key,value,size)          fgetxattr(path,key,value,size,0,0)
#define fsetxattr(path,key,value,size,flag)     fsetxattr(path,key,value,size,0,flag)

#define F_GETLK64       F_GETLK
#define F_SETLK64       F_SETLK
#define F_SETLKW64      F_SETLKW

int32_t gf_darwin_compat_listxattr (int len, dict_t *dict, int size);
int32_t gf_darwin_compat_getxattr (const char *key, dict_t *dict);
int32_t gf_darwin_compat_setxattr (dict_t *dict);

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
#include <sys/sysmacros.h>

#define MAKEDEV(maj, min) makedevice(maj, min)
#ifndef lchmod
#define lchmod chmod
#endif 

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

#define lremovexattr(path,key)               solaris_removexattr(path,key)
#define llistxattr(path,key,size)            solaris_listxattr(path,key,size)
#define lgetxattr(path,key,value,size)       solaris_getxattr(path,key,value,size)
#define lsetxattr(path,key,value,size,flags) solaris_setxattr(path,key,value,size,flags)
#define fgetxattr(fd,key,value,size)         solaris_fgetxattr(fd,key,value,size)
#define fsetxattr(fd,key,value,size,flags)   solaris_fsetxattr(fd,key,value,size,flags)
#define lutimes(filename,times)              utimes(filename,times)

int asprintf(char **string_ptr, const char *format, ...); 
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

#endif /* GF_SOLARIS_HOST_OS */

#ifndef HAVE_ARGP
#include "argp.h"
#else
#include <argp.h>
#endif /* HAVE_ARGP */

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

#ifndef HAVE_STRNLEN
static inline size_t 
strnlen(const char *string, size_t maxlen)                   
{
	int len = 0;
	while ((len < maxlen) && string[len])
		len++;
	return len;
}
#endif


#define ALIGN(x) (((x) + sizeof (uint64_t) - 1) & ~(sizeof (uint64_t) - 1))

#include <sys/types.h>
#include <dirent.h>

static inline int32_t
dirent_size (struct dirent *entry)
{
#ifdef GF_BSD_HOST_OS
        return ALIGN (24 /* FIX MEEEE!!! */ + entry->d_namlen);
#endif
#ifdef GF_DARWIN_HOST_OS
        return ALIGN (24 /* FIX MEEEE!!! */ + entry->d_namlen);
#endif
#ifdef GF_LINUX_HOST_OS
        return ALIGN (24 /* FIX MEEEE!!! */ + entry->d_reclen);
#endif
#ifdef GF_SOLARIS_HOST_OS
        return ALIGN (24 /* FIX MEEEE!!! */ + entry->d_reclen);
#endif
}


static inline int32_t
gf_compat_getxattr (const char *key, dict_t *dict)
{
#ifdef GF_DARWIN_HOST_OS
  return gf_darwin_compat_getxattr (key, dict);
#endif
  return -1;
}


static inline int32_t
gf_compat_setxattr (dict_t *dict)
{
#ifdef GF_DARWIN_HOST_OS
  return gf_darwin_compat_setxattr (dict);
#endif
  return -1;
}


static inline int32_t
gf_compat_listxattr (int len, dict_t *dict, int size)
{
#ifdef GF_DARWIN_HOST_OS
  return gf_darwin_compat_listxattr (len, dict, size);
#endif
  return len;
}


#endif /* __COMPAT_H__ */
