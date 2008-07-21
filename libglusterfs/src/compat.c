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

#ifdef GF_DARWIN_HOST_OS
#include <libgen.h>
#endif

#ifdef GF_BSD_HOST_OS
#include <libgen.h>
#endif 

#ifdef GF_SOLARIS_HOST_OS
#include "logging.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#endif /* GF_SOLARIS_HOST_OS */

#include "compat.h"
#include "common-utils.h"

/*
 * Produce output for --help
 */

extern const char *argp_program_bug_address;
extern const char *argp_program_version;

void
argp_help_ (const struct argp *__argp, char **__argv)
{
  if (!__argp || !__argv[0])
    return;

  const struct argp_option *options = __argp->options;

  fprintf (stderr, "Usage: %s %s\n", basename (__argv[0]), __argp->args_doc);
  fprintf (stderr, "%s", __argp->doc);
  
  while (options->doc) {
    if (options->name != NULL && options->key != 0) {
      if (options->flags == 0) {
	fprintf (stderr, "  -%c, --%s%s%s\n\t\t%s\n", 
		 options->key, options->name, 
		 options->arg ? "=" : "", 
		 options->arg ? options->arg : "", options->doc);
      }
    } else {
      fprintf (stderr,"\n%s\n", options->doc);
    }
    options++;
  }
  
  fprintf (stderr, "\nMandatory or optional arguments to long options are also mandatory \nor optional for any corresponding short options.\n");
  fprintf (stderr, "\nReport bugs to %s.\n", argp_program_bug_address);
  exit (0);
}

void
argp_usage_ (const struct argp *__argp, char **__argv)
{
  if (!__argp || !__argv[0])
    return;

  const struct argp_option *options = __argp->options;

  fprintf (stderr, "Usage: %s", basename (__argv[0]));

  while (options->doc) {
    if (options->name != NULL && options->key != 0) {
      if (options->flags == 0) {
	fprintf (stderr, " [-%c %s] [--%s%s%s] \n", 
		 options->key, options->arg ? options->arg : "", 
		 options->name, options->arg ? "=" : "", 
		 options->arg ? options->arg : "");
      }
    }
    options++;
  }

  exit (0);
}

void
argp_version_ (const char *version)
{
  fprintf (stderr, "%s\n", version);
  exit(0);
}

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

  while (options->doc) {
    if (options->name != NULL && options->key != 0) {
      num_opts++;
    }
    options++;
  }

  getopt_long_options = (struct option *) calloc (num_opts+1, sizeof (*getopt_long_options));
  ERR_ABORT (getopt_long_options);
  getopt_short_options = (char *) calloc (num_opts+1, 2 * sizeof (char));
  ERR_ABORT (getopt_short_options);

  options = __argp->options;

  while (options->doc) {
    if (options->name != NULL && options->key != 0) {
      getopt_short_options[short_idx++] = options->key;
      getopt_long_options[long_idx].name = options->name;
      getopt_long_options[long_idx].val = options->key;
      if (options->arg != NULL) {
	getopt_short_options[short_idx++] = ':';
	getopt_long_options[long_idx].has_arg = 1;
      }
      long_idx++;
    }
    options++;
  }

  int option_index = 1;

  while (1) {

    c = getopt_long (__argc, __argv, getopt_short_options,
		     getopt_long_options, NULL);
    
    if (c == -1)
      break;
    
    if (c == 'h')
      argp_help_ (__argp, __argv);
    
    if (c == 'u')
      argp_usage_ (__argp, __argv);
    
    if (c == 'V')
      argp_version_ (argp_program_version);
    
    if (c == '?')
      fprintf (stderr, "Try `%s --help` or `%s --usage` for more information\n",
	       basename (__argv[0]), basename (__argv[0]));
    
    __argp->parser (c, optarg, &state);
    option_index += 2;
  }

  while (option_index < __argc) {
    __argp->parser (ARGP_KEY_ARG, __argv[option_index], &state);
    option_index++;
  }

  return 0;
}

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

  attrfd = openat (fd, key, flags|O_CREAT|O_WRONLY, 0777);
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

  attrfd = openat (fd, key, O_RDONLY, 0);
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

