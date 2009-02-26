/*
   Copyright (c) 2008-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef __LIBGLUSTERFSCLIENT_H
#define __LIBGLUSTERFSCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/stat.h>
        //#include <unistd.h>
#include <dirent.h>
#include <errno.h>
/* #include <logging.h> */

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
        struct {
                char *specfile;
                FILE *specfp;
        }; 
        char          *volume_name;
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

/* added for log related initialization for fork implementation in booster */
void 
glusterfs_reset (void);

void
glusterfs_log_lock (void);

void
glusterfs_log_unlock (void);

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
glusterfs_llistxattr (libglusterfs_handle_t handle, 
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

ssize_t 
glusterfs_readv (unsigned long fd, 
                 const struct iovec *vec, 
                 int count);

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

ssize_t 
glusterfs_writev (unsigned long fd, 
                  const struct iovec *vector,
                  size_t count);

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
                   unsigned int count);

int
glusterfs_getdents (unsigned long fd,
		    struct dirent *dirp,
		    unsigned int count);

ssize_t 
glusterfs_pread (unsigned long fd, 
                 void *buf, 
                 size_t count, 
                 off_t offset);

ssize_t 
glusterfs_pwrite (unsigned long fd, 
                  const void *buf, 
                  size_t count, 
                  off_t offset);

off_t
glusterfs_lseek (unsigned long fd, off_t offset, int whence);

#ifdef __cplusplus
}
#endif

#endif
