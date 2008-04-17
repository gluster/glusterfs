#ifndef __LIBGLUSTERFS_CLIENT_H
#define __LIBGLUSTERFS_CLIENT_H

#include <sys/types.h>
#include <unistd.h>

//typedef struct libglusterfs_ctx libglusterfs_ctx_t;
//struct libglusterfs_ctx;

typedef struct {
  int op_ret;
  int op_errno;
  struct iovec *vector;
  int32_t count;
  void *ref;
}glusterfs_read_buf_t;

typedef struct {
  char *logfile;
  char *loglevel;
  char *specfile;
  uint32_t lookup_timeout;
  uint32_t stat_timeout;
}glusterfs_init_ctx_t;

typedef struct libglusterfs_client_ctx *libglusterfs_handle_t;

typedef int32_t (*glusterfs_readv_cbk_t)(glusterfs_read_buf_t *buf,
					 void *cbk_data);

typedef int32_t (*glusterfs_writev_cbk_t) (int32_t op_ret, int32_t op_errno, void *cbk_data);

void 
glusterfs_free (glusterfs_read_buf_t *buf);

libglusterfs_handle_t 
glusterfs_init (glusterfs_init_ctx_t *ctx);

int
glusterfs_fini (libglusterfs_handle_t handle);

long 
glusterfs_open (libglusterfs_handle_t handle, const char *path, int flags, mode_t mode);

long 
glusterfs_creat (libglusterfs_handle_t handle, const char *path, mode_t mode);

int 
glusterfs_close(long fd);

int32_t  
glusterfs_stat (libglusterfs_handle_t handle, 
		const char *path, 
		struct stat *buf);

int32_t 
glusterfs_fstat (long fd, struct stat *buf) ;

int 
glusterfs_setxattr (libglusterfs_handle_t handle, 
		    const char *path, 
		    const char *name,
		    const void *value, 
		    size_t size, 
		    int flags);

int 
glusterfs_lsetxattr (libglusterfs_handle_t handle,
		     const char *path, 
		     const char *name,
		     const void *value, 
		     size_t size, 
		     int flags);

int 
glusterfs_fsetxattr (libglusterfs_handle_t handle, 
		     int filedes, 
		     const char *name,
		     const void *value, 
		     size_t size, 
		     int flags);

ssize_t 
glusterfs_getxattr (libglusterfs_handle_t handle, 
		    const char *path, 
		    const char *name,
		    void *value, 
		    size_t size);

ssize_t 
glusterfs_lgetxattr (libglusterfs_handle_t handle, 
		     const char *path, 
		     const char *name,
		     void *value, 
		     size_t size);

ssize_t 
glusterfs_fgetxattr (libglusterfs_handle_t handle, 
		     int filedes, 
		     const char *name,
		     void *value, 
		     size_t size);

ssize_t 
glusterfs_listxattr (libglusterfs_handle_t handle, 
		     const char *path, 
		     char *list,
		     size_t size);

ssize_t 
gf_llistxattr (libglusterfs_handle_t handle, 
	       const char *path, 
	       char *list,
	       size_t size);

ssize_t 
glusterfs_flistxattr (libglusterfs_handle_t handle, 
		      int filedes, 
		      char *list,
		      size_t size);

int 
glusterfs_removexattr (libglusterfs_handle_t handle, 
		       const char *path, 
		       const char *name);

int 
glusterfs_lremovexattr (libglusterfs_handle_t handle, 
			const char *path, 
			const char *name);

int 
glusterfs_fremovexattr (libglusterfs_handle_t handle, 
			int filedes, 
			const char *name);

ssize_t 
glusterfs_read (long fd, void *buf, size_t nbytes);

int 
glusterfs_read_async (long fd, 
		      size_t nbytes, 
		      off_t offset,
		      glusterfs_readv_cbk_t readv_cbk,
		      void *cbk_data);

ssize_t 
glusterfs_write (long fd, const void *buf, size_t n);

int
glusterfs_write_async (long fd, 
		       void *buf, 
		       size_t nbytes, 
		       off_t offset,
		       glusterfs_writev_cbk_t writev_cbk,
		       void *cbk_data);


int
glusterfs_readdir (long fd, struct dirent *dirp, int count);

#endif
