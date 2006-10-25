#ifndef _POSIX_H
#define _POSIX_H

#include <stdio.h>
#include <dirent.h>
#include <sys/xattr.h>
#include "xlator.h"

// FIXME: possible portability issue if we ever run on other POSIX systems
#include <linux/limits.h> 
//#include <any_other_required_header>

#define WITH_DIR_PREPENDED(path, var, code) do { \
  int8_t var[PATH_MAX]; \
  memset (var, 0, PATH_MAX);\
  strcpy (var, ((struct posix_private *)this->private)->base_path); \
  strcpy (var+((struct posix_private *)this->private)->base_path_length, path); \
  code ; \
} while (0);

#define GET_DIR_PREPENDED(path, var) do { \
  int8_t var[PATH_MAX]; \
  strcpy (var, ((struct posix_private *)this->private)->base_path); \
  strcpy (var+((struct posix_private *)this->private)->base_path_length, path); \
} while (0);

struct posix_private {
  int32_t temp;
  int8_t is_stateless;
  int8_t is_debug;
  int8_t base_path[PATH_MAX];
  int32_t base_path_length;

  struct xlator_stats stats; /* Statastics, provides activity of the server */
  
  struct timeval prev_fetch_time;
  struct timeval init_time;
  int32_t max_read;            /* */
  int32_t max_write;           /* */
  int64_t interval_read;      /* Used to calculate the max_read value */
  int64_t interval_write;     /* Used to calculate the max_write value */
  int64_t read_value;    /* Total read, from init */
  int64_t write_value;   /* Total write, from init */
};

#endif /* _POSIX_H */
