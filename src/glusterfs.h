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

#define gprintf printf

#define FUNCTION_CALLED \
do {                    \
   gprintf ("%s called\n", __FUNCTION__); \
} while (0)

#define RELATIVE(path) (((char *)path)[1] == '\0' ? "." : path + 1)

#define GLUSTER_OPENFD_MIN 1024
#define GLUSTERFSD_MAX_CONTEXT 64 * 1024


typedef enum {
  OP_GETATTR,
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
  OP_LISTXATTR,
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

struct xfer_header {
  off_t offset;
  int fd;
  int flags;
  size_t size;
  int len;
  int remote_errno;
  int remote_ret;
  glusterfs_op_t op;
  dev_t dev;
  mode_t mode;
  uid_t uid;
  gid_t gid;
  time_t actime;
  time_t modtime;
} __attribute__ ((packed));

struct wait_queue {
  struct wait_queue *next;
  pthread_mutex_t mutex;
};

struct glusterfs_private {
  int sock;
  unsigned char connected;
  in_addr_t addr;
  unsigned short port;
  pthread_mutex_t mutex; /* mutex to fall in line in *queue */
  struct wait_queue *queue;
};

struct write_queue {
  struct write_queue *next;
  int buf_len;
  void *buffer;
};

struct gfsd_context {
  struct write_queue *gfsd_write_queue;
  /* Lot of more thing can come here */
};

struct gfsd_context *glusterfsd_context[GLUSTERFSD_MAX_CONTEXT];

int full_write (struct glusterfs_private *priv,
		const void *data,
		size_t size);
int full_read (struct glusterfs_private *priv,
	       void *data,
	       size_t size);


#endif /* _GLUSTERFS_H */
