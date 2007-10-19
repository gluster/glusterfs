#ifndef _POSIX_H
#define _POSIX_H

#include <stdio.h>
#include <dirent.h>
#include <unistd.h>

#ifdef linux
#ifdef __GLIBC__
#include <sys/fsuid.h>
#else
#include <unistd.h>
#endif
#endif

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#ifdef HAVE_SYS_EXTATTR_H
#include <sys/extattr.h>
#endif

#include "xlator.h"
#include "inode.h"
#include "compat.h"

struct posix_private {
  inode_table_t *itable;
  int32_t temp;
  char is_stateless;
  char *base_path;
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
