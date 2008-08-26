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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <sys/types.h>
#include <dirent.h>

#ifdef GF_SOLARIS_HOST_OS
#include "logging.h"
#endif /* GF_SOLARIS_HOST_OS */

#include "compat.h"
#include "common-utils.h"

#ifdef GF_DARWIN_HOST_OS

static const char finder_info_content[32] = {
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
};

static const char finder_info_xattr[] = "com.apple.FinderInfo";

static const char resource_fork_xattr[] = "com.apple.ResourceFork";

int32_t 
gf_darwin_compat_listxattr (int len, dict_t *dict, int size)
{
  if (len == -1)
    len = 0;
  if (size >= 0)
    {
      dict_set (dict, (char *)finder_info_xattr, str_to_data (""));
      dict_set (dict, (char *)resource_fork_xattr, str_to_data (""));
    }
  
  return (len + sizeof(finder_info_xattr) + sizeof (resource_fork_xattr));
}

int32_t 
gf_darwin_compat_getxattr (char *key, char **value, int size)
{
  if (strcmp(key, finder_info_xattr) == 0) {
    if (*value != 0 && size >= sizeof (finder_info_content)) {
      memcpy(*value, finder_info_content, sizeof (finder_info_content));
      return sizeof (finder_info_content);
    }
  }
  if (strcmp(key, resource_fork_xattr) == 0) {
    /* Always null */
    return 0;
  }
  
  return -1;
}

#endif /* DARWIN */


#ifdef GF_SOLARIS_HOST_OS

int 
solaris_fsetxattr(int fd, 
		  const char* key, 
		  const char *value, 
		  size_t size, 
		  int flags)
{
  int attrfd = -1;
  int ret = 0;

  attrfd = openat (fd, key, flags|O_CREAT|O_WRONLY|O_XATTR, 0777);
  if (attrfd >= 0) {
    ftruncate (attrfd, 0);
    ret = write (attrfd, value, size);
    close (attrfd);
  } else {
    if (errno != ENOENT)
      gf_log ("libglusterfs", GF_LOG_ERROR, 
	      "Couldn't set extended attribute for %d (%d)", 
	      fd, errno);
    return -1;
  }

  return 0;
}


int 
solaris_fgetxattr(int fd, 
		  const char* key, 
		  char *value, 
		  size_t size)
{
  int attrfd = -1;
  int ret = 0;

  attrfd = openat (fd, key, O_RDONLY|O_XATTR);
  if (attrfd >= 0) {
    if (size == 0) {
      struct stat buf;
      fstat (attrfd, &buf);
      ret = buf.st_size;
    } else {
      ret = read (attrfd, value, size);
    }
    close (attrfd);
  } else {
    if (errno != ENOENT)
      gf_log ("libglusterfs", GF_LOG_DEBUG, 
	      "Couldn't read extended attribute for the file %d (%d)", 
	      fd, errno);
    return -1;
  }

  return ret;
}


int 
solaris_setxattr(const char *path, 
		 const char* key, 
		 const char *value, 
		 size_t size, 
		 int flags)
{
  int attrfd = -1;
  int ret = 0;

  attrfd = attropen (path, key, flags|O_CREAT|O_WRONLY, 0777);
  if (attrfd >= 0) {
    ftruncate (attrfd, 0);
    ret = write (attrfd, value, size);
    close (attrfd);
  } else {
    if (errno != ENOENT)
      gf_log ("libglusterfs", GF_LOG_ERROR, 
	      "Couldn't set extended attribute for %s (%d)", 
	      path, errno);
    return -1;
  }

  return 0;
}


int
solaris_listxattr(const char *path, 
		  char *list, 
		  size_t size)
{
  int attrdirfd = -1;
  ssize_t len = 0;
  DIR *dirptr = NULL;
  struct dirent *dent = NULL;
  int newfd = -1;

  attrdirfd = attropen (path, ".", O_RDONLY, 0);
  if (attrdirfd >= 0) {
    newfd = dup(attrdirfd);
    dirptr = fdopendir(newfd);
    if (dirptr) {
      while ((dent = readdir(dirptr))) {
	size_t listlen = strlen(dent->d_name);
	if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, "..")) {
	  /* we don't want "." and ".." here */
	  continue;
	}
	if (size == 0) {
	  /* return the current size of the list of extended attribute names*/
	  len += listlen + 1;
	} else {
	  /* check size and copy entrie + nul into list. */
	  if ((len + listlen + 1) > size) {
	    errno = ERANGE;
	    len = -1;
	    break;
	  } else {
	    strncpy(list + len, dent->d_name, listlen);
	    len += listlen;
	    list[len] = '\0';
	    ++len;
	  }
	}
      }
      
      if (closedir(dirptr) == -1) {
	close (attrdirfd);
	return -1;
      }
    } else {
      close (attrdirfd);
      return -1;
    }
    close (attrdirfd);
  }

  return len;
}

int 
solaris_removexattr(const char *path, 
		    const char* key)
{
  int ret = -1;
  int attrfd = attropen (path, ".", O_RDONLY, 0);
  if (attrfd >= 0) {
    ret = unlinkat (attrfd, key, 0);
    close (attrfd);
  }

  return ret;
}

int 
solaris_getxattr(const char *path, 
		 const char* key, 
		 char *value, 
		 size_t size)
{
  int attrfd = -1;
  int ret = 0;

  attrfd = attropen (path, key, O_RDONLY, 0);
  if (attrfd >= 0) {
    if (size == 0) {
      struct stat buf;
      fstat (attrfd, &buf);
      ret = buf.st_size;
    } else {
      ret = read (attrfd, value, size);
    }
    close (attrfd);
  } else {
    if (errno != ENOENT)
      gf_log ("libglusterfs", GF_LOG_DEBUG, 
	      "Couldn't read extended attribute for the file %s (%d)", 
	      path, errno);
    return -1;
  }
  return ret;
}


int
asprintf(char **string_ptr, const char *format, ...)
{ 
  va_list arg;
  char *str;
  int size;
  int rv;
  
  if (!string_ptr || !format)
    return -1;
   
  va_start(arg, format);
  size = vsnprintf(NULL, 0, format, arg);
  size++;
  va_start(arg, format);
  str = malloc(size);
  if (str == NULL) {
    va_end(arg);
    /*
     * Strictly speaking, GNU asprintf doesn't do this,
     * but the caller isn't checking the return value.
     */
    gf_log ("libglusterfs", GF_LOG_CRITICAL, "failed to allocate memory\n");
    return -1;
  }
  rv = vsnprintf(str, size, format, arg);
  va_end(arg);
   
  *string_ptr = str;
  return (rv);
}  

char* strsep(char** str, const char* delims)
{
  char* token;

  if (*str==NULL) {
    /* No more tokens */
    return NULL;
  }

  token=*str;
  while (**str!='\0') {
    if (strchr(delims,**str)!=NULL) {
      **str='\0';
      (*str)++;
      return token;
    }
    (*str)++;
  }
  /* There is no other token */
  *str=NULL;
  return token;
}

#endif /* GF_SOLARIS_HOST_OS */

