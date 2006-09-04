/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 


#ifndef _POSIX_H
#define _POSIX_H

#include <stdio.h>
#include <dirent.h>
#include <sys/xattr.h>
#include "xlator.h"

// FIXME: possible portability issue if we ever run on other POSIX systems
#include <linux/limits.h> 
//#include <any_other_required_header>

/* Note: This assumes that you have "xl" declared as the xlator struct */
#define WITH_DIR_PREPENDED(path, var, code) do { \
  char var[PATH_MAX]; \
  memset (var, 0, PATH_MAX);\
  strcpy (var, ((struct posix_private *)xl->private)->base_path); \
  strcpy (var+((struct posix_private *)xl->private)->base_path_length, path); \
  code ; \
} while (0);

#define GET_DIR_PREPENDED(path, var) do { \
  char var[PATH_MAX]; \
  strcpy (var, ((struct posix_private *)xl->private)->base_path); \
  strcpy (var+((struct posix_private *)xl->private)->base_path_length, path); \
} while (0);

struct posix_private {
  int temp;
  char is_stateless;
  char is_debug;
  char base_path[PATH_MAX];
  int base_path_length;

  struct xlator_stats stats; /* Statastics, provides activity of the server */
  
  struct timeval prev_fetch_time;
  struct timeval init_time;
  int max_read;            /* */
  int max_write;           /* */
  long interval_read;      /* Used to calculate the max_read value */
  long interval_write;     /* Used to calculate the max_write value */
  long long read_value;    /* Total read, from init */
  long long write_value;   /* Total write, from init */
};

#endif /* _POSIX_H */
