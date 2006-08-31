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
  struct xlator_stats stats;
  
  long long read_value;
  long long write_value;
};

#endif /* _POSIX_H */
