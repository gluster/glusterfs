/*
  Copyright (c) 2008, 2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _LIBGLUSTERFSCLIENT_H
#define _LIBGLUSTERFSCLIENT_H

#include <sys/cdefs.h>

__BEGIN_DECLS

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

typedef struct {
        struct iovec *vector;
        int           count;
        void         *iobref;
        void         *dictref;
} glusterfs_iobuf_t;

typedef struct {
        char          *logfile;
        char          *loglevel;
        struct {
                char  *specfile;
                FILE  *specfp;
        }; 
        char          *volume_name;
        unsigned long  lookup_timeout;
        unsigned long  stat_timeout;
} glusterfs_init_params_t;

typedef void * glusterfs_handle_t;

/* FIXME: how is glusterfs_file_t different from glusterfs_dir_t? */
typedef void * glusterfs_file_t;
typedef void * glusterfs_dir_t;

typedef
int (*glusterfs_readv_cbk_t) (int op_ret, int op_errno, glusterfs_iobuf_t *buf,
			      void *cbk_data);

typedef
int (*glusterfs_write_cbk_t) (int op_ret, int op_errno, void *cbk_data);

typedef
int (*glusterfs_get_cbk_t) (int op_ret, int op_errno, glusterfs_iobuf_t *buf,
			    struct stat *stbuf, void *cbk_data);

/* Used to free the glusterfs_read_buf passed to the application from
   glusterfs_read_async_cbk
*/
void
glusterfs_free (glusterfs_iobuf_t *buf);

/* libglusterfsclient initialization function  */
glusterfs_handle_t
glusterfs_init (glusterfs_init_params_t *ctx);

int
glusterfs_fini (glusterfs_handle_t handle);

/* For smaller files, application can use just glusterfs_get/glusterfs_get_async
 * to read the whole content. Limit of the file-sizes to be read in
 * glusterfs_get/glusterfs_get_async is passed in the size argument
 */

/* glusterfs_get:
 * @handle : glusterfs handle
 * @path   : path to be looked upon
 * @size   : upper limit of file-sizes to be read in lookup
 * @stbuf  : attribute buffer
 */

int
glusterfs_glh_get (glusterfs_handle_t handle, const char *path, void *buf,
	       size_t size, struct stat *stbuf);

int
glusterfs_get (const char *path, void *buf, size_t size, struct stat *stbuf);

int
glusterfs_get_async (glusterfs_handle_t handle, const char *path, size_t size,
		     glusterfs_get_cbk_t cbk, void *cbk_data);

glusterfs_file_t
glusterfs_glh_open (glusterfs_handle_t handle, const char *path, int flags,
                        ...);

glusterfs_file_t
glusterfs_open (const char *path, int flags, ...);

glusterfs_file_t
glusterfs_glh_creat (glusterfs_handle_t handle, const char *path, mode_t mode);

glusterfs_file_t
glusterfs_creat (const char *path, mode_t mode);

int
glusterfs_mkdir (glusterfs_handle_t handle, const char *path, mode_t mode);

int
glusterfs_rmdir (glusterfs_handle_t handle, const char *path);

int
glusterfs_close (glusterfs_file_t fd);

int
glusterfs_glh_stat (glusterfs_handle_t handle, const char *path,
                        struct stat *stbuf);

int
glusterfs_stat (const char *path, struct stat *buf);

int
glusterfs_fstat (glusterfs_file_t fd, struct stat *stbuf);

int
glusterfs_glh_setxattr (glusterfs_handle_t handle, const char *path,
                                const char *name, const void *value,
                                size_t size, int flags);

int
glusterfs_setxattr (const char *path, const char *name, const void *value,
                        size_t size, int flags);

int
glusterfs_lsetxattr (glusterfs_handle_t handle, const char *path,
                     const char *name, const void *value, size_t size,
                     int flags);

int
glusterfs_fsetxattr (glusterfs_file_t fd, const char *name, const void *value,
                     size_t size, int flags);

ssize_t
glusterfs_glh_getxattr (glusterfs_handle_t handle, const char *path,
		                const char *name, void *value, size_t size);

ssize_t
glusterfs_getxattr (const char *path, const char *name, void *value,
                        size_t size);

ssize_t
glusterfs_lgetxattr (glusterfs_handle_t handle, const char *path,
                     const char *name, void *value, size_t size);

ssize_t
glusterfs_fgetxattr (glusterfs_file_t fd, const char *name, void *value,
		     size_t size);

ssize_t
glusterfs_listxattr (glusterfs_handle_t handle, const char *path, char *list,
                     size_t size);

ssize_t
glusterfs_llistxattr (glusterfs_handle_t handle, const char *path, char *list,
                      size_t size);

ssize_t
glusterfs_flistxattr (glusterfs_file_t fd, char *list, size_t size);

int
glusterfs_removexattr (glusterfs_handle_t handle, const char *path,
		       const char *name);

int
glusterfs_lremovexattr (glusterfs_handle_t handle, const char *path,
			const char *name);

int
glusterfs_fremovexattr (glusterfs_file_t fd, const char *name);

ssize_t
glusterfs_read (glusterfs_file_t fd, void *buf, size_t nbytes);

ssize_t
glusterfs_readv (glusterfs_file_t fd, const struct iovec *vec, int count);

int
glusterfs_read_async (glusterfs_file_t fd, size_t nbytes, off_t offset,
                      glusterfs_readv_cbk_t readv_cbk, void *cbk_data);

ssize_t
glusterfs_write (glusterfs_file_t fd, const void *buf, size_t nbytes);

ssize_t
glusterfs_writev (glusterfs_file_t fd, const struct iovec *vector, int count);

int
glusterfs_write_async (glusterfs_file_t fd, const void *buf, size_t nbytes,
                       off_t offset, glusterfs_write_cbk_t write_cbk,
		       void *cbk_data);

int
glusterfs_writev_async (glusterfs_file_t fd, const struct iovec *vector,
			int count, off_t offset,
			glusterfs_write_cbk_t write_cbk, void *cbk_data);

ssize_t
glusterfs_pread (glusterfs_file_t fd, void *buf, size_t nbytes, off_t offset);

ssize_t
glusterfs_pwrite (glusterfs_file_t fd, const void *buf, size_t nbytes,
		  off_t offset);

off_t
glusterfs_lseek (glusterfs_file_t fd, off_t offset, int whence);

int
glusterfs_mkdir (glusterfs_handle_t handle, const char *path, mode_t mode);

int
glusterfs_rmdir (glusterfs_handle_t handle, const char *path);

/* FIXME: implement man 3 readdir semantics */
int
glusterfs_readdir (glusterfs_dir_t fd, struct dirent *dirp,
		   unsigned int count);

/* FIXME: remove getdents */
int
glusterfs_getdents (glusterfs_dir_t fd, struct dirent *dirp,
		    unsigned int count);

int
glusterfs_mknod(glusterfs_handle_t handle, const char *pathname, mode_t mode,
                dev_t dev);

char *
glusterfs_realpath (glusterfs_handle_t handle, const char *path,
                        char *resolved_path);

int
glusterfs_mount (char *vmp, glusterfs_init_params_t *ipars);

/* FIXME: review the need for these apis */
/* added for log related initialization in booster fork implementation */
void
glusterfs_reset (void);

void
glusterfs_log_lock (void);

void
glusterfs_log_unlock (void);

__END_DECLS

#endif /* !_LIBGLUSTERFSCLIENT_H */
