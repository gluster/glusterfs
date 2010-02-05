/*
   Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

#define GF_FINDER_INFO_XATTR   "com.apple.FinderInfo"
#define GF_RESOURCE_FORK_XATTR "com.apple.ResourceFork"
#define GF_FINDER_INFO_SIZE    32

static const char gf_finder_info_content[GF_FINDER_INFO_SIZE] = {
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
};


int32_t 
gf_darwin_compat_listxattr (int len, dict_t *dict, int size)
{
	data_t *data = NULL;
	if (len == -1)
		len = 0;

	data = dict_get (dict, GF_FINDER_INFO_XATTR);
	if (!data) {
		dict_set (dict, GF_FINDER_INFO_XATTR, 
			  bin_to_data ((void *)gf_finder_info_content,
				       GF_FINDER_INFO_SIZE));
		len += strlen (GF_FINDER_INFO_XATTR);
	}

	data = dict_get (dict, GF_RESOURCE_FORK_XATTR);
	if (!data) {
		dict_set (dict, GF_RESOURCE_FORK_XATTR, str_to_data (""));
		len += strlen (GF_RESOURCE_FORK_XATTR);
	}

	return len;
}

int32_t 
gf_darwin_compat_getxattr (const char *key, dict_t *dict)
{
	data_t *data = NULL;

	if (strcmp(key, GF_FINDER_INFO_XATTR) == 0) {
		data = dict_get (dict, GF_FINDER_INFO_XATTR);
		if (!data) {
			dict_set (dict, GF_FINDER_INFO_XATTR, 
				  bin_to_data ((void *)gf_finder_info_content,
					       GF_FINDER_INFO_SIZE));
			return GF_FINDER_INFO_SIZE;
		}
		return 0;
	}
	
	if (strcmp(key, GF_RESOURCE_FORK_XATTR) == 0) {
		data = dict_get (dict, GF_RESOURCE_FORK_XATTR);
		if (!data) {
			/* Always null */
			dict_set (dict, GF_RESOURCE_FORK_XATTR,
				  str_to_data (""));
			return 0;
		}
		return 0;
	}
	return -1;
}


int32_t 
gf_darwin_compat_setxattr (dict_t *dict)
{
	data_t *data = NULL;

	data = dict_get (dict, GF_FINDER_INFO_XATTR);
	if (data)
		return 0;
	data = dict_get (dict, GF_RESOURCE_FORK_XATTR);
	if (data)
		return 0;

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
		if (errno == ENOENT)
			errno = ENODATA;
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
solaris_flistxattr(int fd,
                   char *list, 
                   size_t size)
{
	int attrdirfd = -1;
	ssize_t len = 0;
	DIR *dirptr = NULL;
	struct dirent *dent = NULL;
	int newfd = -1;
	
	attrdirfd = openat (fd, ".", O_RDONLY, 0);
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
	} else {
		if (errno == ENOENT)
			errno = ENODATA;
		return -1;
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
		if (errno == ENOENT)
			errno = ENODATA;
		if (errno != ENOENT)
			gf_log ("libglusterfs", GF_LOG_DEBUG, 
				"Couldn't read extended attribute for the file %s (%d)", 
				path, errno);
		return -1;
	}
	return ret;
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

/* Code comes from libiberty */

int
vasprintf (char **result, const char *format, va_list args)
{
  const char *p = format;
  /* Add one to make sure that it is never zero, which might cause malloc
     to return NULL.  */
  int total_width = strlen (format) + 1;
  va_list ap;

  /* this is not really portable but works under Windows */
  memcpy ( &ap, &args, sizeof (va_list));

  while (*p != '\0')
    {
      if (*p++ == '%')
	{
	  while (strchr ("-+ #0", *p))
	    ++p;
	  if (*p == '*')
	    {
	      ++p;
	      total_width += abs (va_arg (ap, int));
	    }
	  else
            {
              char *endp;  
              total_width += strtoul (p, &endp, 10);
              p = endp;
            }
	  if (*p == '.')
	    {
	      ++p;
	      if (*p == '*')
		{
		  ++p;
		  total_width += abs (va_arg (ap, int));
		}
	      else
                {
                  char *endp;
                  total_width += strtoul (p, &endp, 10);
                  p = endp;
                }
	    }
	  while (strchr ("hlL", *p))
	    ++p;
	  /* Should be big enough for any format specifier except %s
             and floats.  */
	  total_width += 30;
	  switch (*p)
	    {
	    case 'd':
	    case 'i':
	    case 'o':
	    case 'u':
	    case 'x':
	    case 'X':
	    case 'c':
	      (void) va_arg (ap, int);
	      break;
	    case 'f':
	    case 'e':
	    case 'E':
	    case 'g':
	    case 'G':
	      (void) va_arg (ap, double);
	      /* Since an ieee double can have an exponent of 307, we'll
		 make the buffer wide enough to cover the gross case. */
	      total_width += 307;
	    
	    case 's':
	      total_width += strlen (va_arg (ap, char *));
	      break;
	    case 'p':
	    case 'n':
	      (void) va_arg (ap, char *);
	      break;
	    }
	}
    }
  *result = malloc (total_width);
  if (*result != NULL)
    return vsprintf (*result, format, args);
  else
    return 0;
}

int
asprintf (char **buf, const char *fmt, ...)
{
  int status;
  va_list ap;

  va_start (ap, fmt);
  status = vasprintf (buf, fmt, ap);
  va_end (ap);
  return status;  
}

#endif /* GF_SOLARIS_HOST_OS */

#ifndef HAVE_STRNLEN
size_t 
strnlen(const char *string, size_t maxlen)                   
{
	int len = 0;
	while ((len < maxlen) && string[len])
		len++;
	return len;
}
#endif /* STRNLEN */
