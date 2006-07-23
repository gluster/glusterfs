#ifndef _GLUSTERFS_H
#define _GLUSTERFS_H

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/poll.h>
#include <pthread.h>

#include "dict.h"

#define gprintf printf

#define FUNCTION_CALLED \
do {                    \
   gprintf ("%s called\n", __FUNCTION__); \
} while (0)

#define RELATIVE(path) (((char *)path)[1] == '\0' ? "." : path + 1)

typedef enum {
  OP_GETATTR = 1,
  OP_READLINK,
  OP_GETDIR,
  OP_MKNOD,
  OP_MKDIR,
  OP_UNLINK,
  OP_RMDIR,
  OP_SYMLINK,
  OP_RENAME,
  OP_LINK,
  OP_CHMOD,
  OP_CHOWN,
  OP_TRUNCATE,
  OP_UTIME,
  OP_OPEN,
  OP_READ,
  OP_WRITE,
  OP_STATFS,
  OP_FLUSH,
  OP_RELEASE,
  OP_FSYNC,
  OP_SETXATTR,
  OP_GETXATTR,
  OP_REMOVEXATTR,
  OP_OPENDIR,
  OP_READDIR,
  OP_RELEASEDIR,
  OP_FSYNCDIR,
  OP_INIT,
  OP_DESTROY,
  OP_ACCESS,
  OP_CREATE,
  OP_FTRUNCATE,
  OP_FGETATTR
} glusterfs_op_t;

/* Keys used in the http style header */

static const struct data_t _op           = { 3, "OP", 1, 1};
static const struct data_t _path         = { 5, "PATH", 1, 1};
static const struct data_t _offset       = { 7, "OFFSET", 1, 1}; 
static const struct data_t _fd           = { 3, "FD", 1, 1};
static const struct data_t _buf          = { 4, "BUF", 1, 1};
static const struct data_t _count        = { 6, "COUNT", 1, 1};
static const struct data_t _flags        = { 6, "FLAGS", 1, 1};
static const struct data_t _errno        = { 6, "ERRNO", 1, 1};
static const struct data_t _ret          = { 4, "RET", 1, 1};
static const struct data_t _mode         = { 5, "MODE", 1, 1};
static const struct data_t _dev          = { 4, "DEV", 1, 1};
static const struct data_t _uid          = { 4, "UID", 1, 1};
static const struct data_t _gid          = { 4, "GID", 1, 1};
static const struct data_t _actime       = { 7, "ACTIME", 1, 1};
static const struct data_t _modtime      = { 8, "MODTIME", 1, 1};

const data_t * DATA_OP      = &_op;
const data_t * DATA_PATH    = &_path;
const data_t * DATA_OFFSET  = &_offset;
const data_t * DATA_FD      = &_fd;
const data_t * DATA_BUF     = &_buf;
const data_t * DATA_COUNT   = &_count;
const data_t * DATA_FLAGS   = &_flags;
const data_t * DATA_ERRNO   = &_errno;
const data_t * DATA_RET     = &_ret;
const data_t * DATA_MODE    = &_mode;
const data_t * DATA_DEV     = &_dev;
const data_t * DATA_UID     = &_uid;
const data_t * DATA_GID     = &_gid;
const data_t * DATA_ACTIME  = &_actime;
const data_t * DATA_MODTIME = &_modtime;

struct wait_queue {
  struct wait_queue *next;
  pthread_mutex_t mutex;
};

struct glusterfs_private {
  int sock;
  FILE *sock_fp;
  unsigned char connected;
  in_addr_t addr;
  unsigned short port;
  pthread_mutex_t mutex; /* mutex to fall in line in *queue */
  struct wait_queue *queue;
};


int full_write (struct glusterfs_private *priv,
		const void *data,
		size_t size);
int full_read (struct glusterfs_private *priv,
	       void *data,
	       size_t size);


#endif /* _GLUSTERFS_H */
