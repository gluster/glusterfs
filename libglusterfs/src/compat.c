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

#include "compat.h"
#include <sys/types.h>
#include <dirent.h>

#ifdef GF_SOLARIS_HOST_OS
#include "logging.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#endif /* GF_SOLARIS_HOST_OS */

int 
argp_parse_ (const struct argp * __argp,
	     int __argc, char **  __argv,
	     unsigned __flags, int * __arg_index,
	     void * __input)
{
  const struct argp_option * options = __argp->options;
  struct argp_state state;

  state.input = __input;
  
  int num_opts = 0;
  struct option * getopt_long_options;
  char *getopt_short_options;
  int c;
  int short_idx = 0;
  int long_idx = 0;

  while (options->name) {
    num_opts++;
    options++;
  }

  getopt_long_options = (struct option *) calloc (num_opts+1, sizeof (*getopt_long_options));
  getopt_short_options = (char *) calloc (num_opts+1, 2 * sizeof (char));

  options = __argp->options;

  while (options->name) {
    getopt_short_options[short_idx++] = options->key;
    getopt_long_options[long_idx].name = options->name;
    getopt_long_options[long_idx].val = options->key;

    if (options->arg != NULL) {
      getopt_short_options[short_idx++] = ':';
      getopt_long_options[long_idx].has_arg = 1;
    }
    options++;
    long_idx++;
  }

  while (1) {
    int option_index = 0;

    c = getopt_long (__argc, __argv, getopt_short_options,
		     getopt_long_options, &option_index);
    
    if (c == -1)
      break;

    __argp->parser (c, optarg, &state);
  }
  return 0;
}

void 
argp_help (const struct argp *argp, FILE *stream, unsigned flags, char *name)
{
  fprintf (stream, "This is a help message");
}


#ifdef GF_SOLARIS_HOST_OS

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
    fprintf(stderr, "failed to allocate memory\\n");
    exit(1);
  }
  rv = vsnprintf(str, size, format, arg);
  va_end(arg);
   
  *string_ptr = str;
  return (rv);
}  

#endif /* GF_SOLARIS_HOST_OS */
