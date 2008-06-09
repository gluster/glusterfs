#ifndef __LIBGLUSTERFSCLIENT_H
#define __LIBGLUSTERFSCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

typedef struct {
  int          op_ret;
  int          op_errno;
  struct iovec *vector;
  int          count;
  void         *ref;
}glusterfs_read_buf_t;

typedef struct {
  char          *logfile;
  char          *loglevel;
  char          *specfile;
  unsigned long lookup_timeout;
  unsigned long stat_timeout;
}glusterfs_init_ctx_t;

typedef struct libglusterfs_client_ctx *libglusterfs_handle_t;

typedef int (*glusterfs_readv_cbk_t) (glusterfs_read_buf_t *buf,
				      void *cbk_data);

typedef int (*glusterfs_writev_cbk_t) (int op_ret, 
				       int op_errno, 
				       void *cbk_data);

typedef int (*glusterfs_lookup_cbk_t) (int op_ret, 
				       int op_errno, 
				       void *buf, 
				       struct stat *st, 
				       void *cbk_data);

  /* Used to free the glusterfs_read_buf passed to the application from glusterfs_read_async_cbk */
void 
glusterfs_free (glusterfs_read_buf_t *buf);

  /* libglusterfsclient initialization function  */
libglusterfs_handle_t 
glusterfs_init (glusterfs_init_ctx_t *ctx);

int
glusterfs_fini (libglusterfs_handle_t handle);

  /* For smaller files, application can use just glusterfs_lookup/glusterfs_lookup_async to read 
   * the whole content. Limit of the file-sizes to be read in 
   * glusterfs_lookup/glusterfs_lookup_async is passed in the size argument */

  /* glusterfs_lookup:
   * @handle: glusterfs handle
   * @path: path to be looked upon
   * @buf: pointer to pre-allocated buf, in which the file content is returned for files with sizes    *       less than the size argument.
   * @size: upper limit of file-sizes to be read in lookup
   * @stbuf: stat buffer 
   */

int 
glusterfs_lookup (libglusterfs_handle_t handle, 
		  const char *path, 
		  void *buf, 
		  size_t size, 
		  struct stat *stbuf);

int
glusterfs_lookup_async (libglusterfs_handle_t handle,
			const char *path,
			void *buf,
			size_t size, 
			glusterfs_lookup_cbk_t cbk,
			void *cbk_data);

unsigned long 
glusterfs_open (libglusterfs_handle_t handle, 
		const char *path, 
		int flags, 
		mode_t mode);

unsigned long 
glusterfs_creat (libglusterfs_handle_t handle, 
		 const char *path, 
		 mode_t mode);

int 
glusterfs_close (unsigned long fd);

int  
glusterfs_stat (libglusterfs_handle_t handle, 
		const char *path, 
		struct stat *buf);

int 
glusterfs_fstat (unsigned long fd, 
		 struct stat *buf) ;

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
glusterfs_fsetxattr (unsigned long fd, 
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
glusterfs_fgetxattr (unsigned long fd,
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
glusterfs_flistxattr (unsigned long fd, 
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
glusterfs_fremovexattr (unsigned long fd, 
			const char *name);

ssize_t 
glusterfs_read (unsigned long fd, 
		void *buf, 
		size_t nbytes);

int 
glusterfs_read_async (unsigned long fd, 
		      size_t nbytes, 
		      off_t offset,
		      glusterfs_readv_cbk_t readv_cbk,
		      void *cbk_data);

ssize_t 
glusterfs_write (unsigned long fd, 
		 const void *buf, 
		 size_t n);

int
glusterfs_write_async (unsigned long fd, 
		       const void *buf, 
		       size_t nbytes, 
		       off_t offset,
		       glusterfs_writev_cbk_t writev_cbk,
		       void *cbk_data);

int
glusterfs_readdir (unsigned long fd, 
		   struct dirent *dirp, 
		   int count);

#ifdef __cplusplus
}
#endif

#endif
