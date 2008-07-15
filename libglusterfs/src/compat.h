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

#ifndef HAVE_ARGP
#include <stdio.h>
#include <errno.h>

#define ARGP_ERR_UNKNOWN E2BIG

#define ARGP_KEY_ARG 		0
#define ARGP_KEY_NO_ARGS        0x1000002 /* ??? */
#define ARGP_HELP_USAGE         0x01 /* a Usage: message. (???) */

#define OPTION_HIDDEN           0x2
#define OPTION_ALIAS            0x4

typedef int error_t;

struct argp;
struct argp_state;
struct argp_child;

struct argp_option {
  const char *name;
  int key;
  const char *arg;
  int flags;
  const char *doc;
  int group;
};

typedef int (*argp_parser_t) (int key, char *arg,
			      struct argp_state *state);

struct argp_child
{
  /* The child parser.  */
  __const struct argp *argp;

  /* Flags for this child.  */
  int flags;

  /* If non-zero, an optional header to be printed in help output before the
     child options.  As a side-effect, a non-zero value forces the child
     options to be grouped together; to achieve this effect without actually
     printing a header string, use a value of "".  */
  __const char *header;

  /* Where to group the child options relative to the other (`consolidated')
     options in the parent argp; the values are the same as the GROUP field
     in argp_option structs, but all child-groupings follow parent options at
     a particular group level.  If both this field and HEADER are zero, then
     they aren't grouped at all, but rather merged with the parent options
     (merging the child's grouping levels with the parents).  */
  int group;
};

struct argp_state
{
  const struct argp *root_argp;

  int argc;
  char **argv;
  int next;
  unsigned flags;
  unsigned arg_num;
  int quoted;

  void *input;
  void **child_inputs;
  void *hook;
  char *name;

  FILE *err_stream;
  FILE *out_stream;

  void *pstate;
};

struct argp
{
  const struct argp_option *options;
  argp_parser_t parser;
  const char *args_doc;
  const char *doc;
  const struct argp_child *children;
  char *(*help_filter) (int __key, __const char *__text, void *__input);
  const char *argp_domain;
};

#define argp_parse argp_parse_
int argp_parse_ (const struct argp * __argp,
		 int __argc, char **  __argv,
		 unsigned __flags, int * __arg_index,
		 void * __input);

void argp_help_ (const struct argp *__argp, char ** __argv);

void argp_usage_ (const struct argp *__argp, char ** __argv);
 
void argp_version_ (const char *version);

#else
#include <argp.h>
#endif /* HAVE_ARGP */

#ifndef strdupa
#define strdupa(s)                                     \
  (__extension__                                       \
    ({                                                 \
      __const char *__old = (s);                       \
      size_t __len = strlen (__old) + 1;               \
      char *__new = (char *) __builtin_alloca (__len); \
      (char *) memcpy (__new, __old, __len);	       \
    }))
#endif 

#define ALIGN(x) (((x) + sizeof (uint64_t) - 1) & ~(sizeof (uint64_t) - 1))

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


#ifdef GF_LINUX_HOST_OS

#include <linux/limits.h>
#include <sys/xattr.h>
#include <endian.h>

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

#include <sys/endian.h>
#include <sys/extattr.h>
#include <limits.h>

enum {
  ATTR_CREATE = 1,
#define XATTR_CREATE ATTR_CREATE
  ATTR_REPLACE = 2
#define XATTR_REPLACE ATTR_REPLACE
};

#ifndef sighandler_t
#define sighandler_t sig_t
#endif

	
#define lremovexattr(path,key)               extattr_delete_link(path, EXTATTR_NAMESPACE_USER, key)
#define llistxattr(path,key,size)            extattr_list_link(path, EXTATTR_NAMESPACE_USER, key, size)
#define lgetxattr(path, key, value, size)    extattr_get_link(path, EXTATTR_NAMESPACE_USER, key, value, size)
#define lsetxattr(path,key,value,size,flags) extattr_set_link(path, EXTATTR_NAMESPACE_USER, key, value, size)
#define fgetxattr(fd,key,value,size)         extattr_get_fd(fd, EXTATTR_NAMESPACE_USER, key, value, size)
#define fsetxattr(fd,key,value,size,flag)    extattr_set_fd(fd, EXTATTR_NAMESPACE_USER, key, value, size)


#define F_GETLK64	F_GETLK
#define F_SETLK64	F_SETLK
#define F_SETLKW64	F_SETLKW

#endif /* GF_BSD_HOST_OS */

#ifdef GF_DARWIN_HOST_OS

#include <machine/endian.h>
#include <sys/xattr.h>
#include <limits.h>

#ifndef sighandler_t
#define sighandler_t sig_t
#endif

#define llistxattr(path,key,size)               listxattr(path,key,size,XATTR_NOFOLLOW)
#define lgetxattr(path,key,value,size)          getxattr(path,key,value,size,0,XATTR_NOFOLLOW)
#define lsetxattr(path,key,value,size,flags)    setxattr(path,key,value,size,flags,XATTR_NOFOLLOW)
#define lremovexattr(path,key)                  removexattr(path,key,XATTR_NOFOLLOW)
#define fgetxattr(path,key,value,size)          fgetxattr(path,key,value,size,0,0)
#define fsetxattr(path,key,value,size,flag)     fsetxattr(path,key,value,size,0,flag)

#define F_GETLK64	F_GETLK
#define F_SETLK64	F_SETLK
#define F_SETLKW64	F_SETLKW

#endif /* GF_DARWIN_HOST_OS */

#ifdef GF_SOLARIS_HOST_OS

#include <endian.h>
#include <limits.h>

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

#define lremovexattr(path,key)               solaris_removexattr(path,key)
#define llistxattr(path,key,size)            solaris_listxattr(path,key,size)
#define lgetxattr(path,key,value,size)       solaris_getxattr(path,key,value,size)
#define lsetxattr(path,key,value,size,flags) solaris_setxattr(path,key,value,size,flags)
#define fgetxattr(fd,key,value,size)         solaris_fgetxattr(path,key,value,size)
#define fsetxattr(fd,key,value,size,flags)   solaris_fsetxattr(path,key,value,size,flags)
#define lutimes(filename,times)              utimes(filename,times)

int asprintf(char **string_ptr, const char *format, ...); 
int solaris_listxattr(const char *path, char *list, size_t size);
int solaris_removexattr(const char *path, const char* key);
int solaris_getxattr(const char *path, const char* key, 
		     char *value, size_t size);
int solaris_setxattr(const char *path, const char* key, const char *value, 
		     size_t size, int flags);
int solaris_fgetxattr(const char *path, const char* key,
		      char *value, size_t size);
int solaris_fsetxattr(const char *path, const char* key, const char *value, 
		      size_t size, int flags);

#endif /* GF_SOLARIS_HOST_OS */

#endif /* __COMPAT_H__ */
