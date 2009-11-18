/*
  Copyright (c) 2008, 2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef __BEGIN_DECLS
#ifdef __cplusplus
#define __BEGIN_DECLS extern "C" {
#else
#define __BEGIN_DECLS
#endif
#endif

#ifndef __END_DECLS
#ifdef __cplusplus
#define __END_DECLS }
#else
#define __END_DECLS
#endif
#endif


__BEGIN_DECLS

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <utime.h>
#include <sys/time.h>
#include <stdint.h>

typedef struct {
        struct iovec *vector;
        int           count;
        void         *iobref;
        void         *dictref;
} glusterfs_iobuf_t;


typedef
int (*glusterfs_readv_cbk_t) (int op_ret, int op_errno, glusterfs_iobuf_t *buf,
			      void *cbk_data);

typedef
int (*glusterfs_write_cbk_t) (int op_ret, int op_errno, void *cbk_data);

typedef
int (*glusterfs_get_cbk_t) (int op_ret, int op_errno, glusterfs_iobuf_t *buf,
			    struct stat *stbuf, void *cbk_data);


/* Data Interface
 * The first section describes the data structures required for
 * using libglusterfsclient.
 */

/* This structure needs to be filled up and
 * passed to te glusterfs_init function which uses
 * the params passed herein to initialize a glusterfs
 * client context and then connect to a glusterfs server.
 */
typedef struct {
        char          *logfile;         /* Path to the file which will store
                                           the log.
                                           */
        char          *loglevel;        /* The log level required for
                                           reporting various events within
                                           libglusterfsclient.
                                           */
        struct {
                char  *specfile;        /* Users can either open a volume or
                                           specfile and assign the pointer to
                                           specfp, or just refer to the volume
                                           /spec file path in specfile.
                                           */
                FILE  *specfp;
        };
        char          *volume_name;     /* The volume file could describe many
                                           volumes but the specific volume
                                           within that file is chosen by
                                           specifying the volume name here.
                                           */
        unsigned long  lookup_timeout;  /* libglusterclient provides the inode
                                           numbers to be cached by the library.
                                           The duration for which these are
                                           cached are defined by lookup_timeout
                                           . In Seconds.
                                           */
        unsigned long  stat_timeout;    /* The file attributes received from
                                           a stat syscall can also be cached
                                           for the duration specified in this
                                           member. In Seconds.
                                           */
} glusterfs_init_params_t;



/* This is the handle returned by glusterfs_init
 * once the initialization is complete.
 * Users should treat this as an opaque handle.
 */
typedef void * glusterfs_handle_t;



/* These identifiers are used as handles for files and dirs.
 * Users of libglusterfsclient should not in anyway try to interpret
 * the actual structures these will point to.
 */
typedef void * glusterfs_file_t;
typedef void * glusterfs_dir_t;


/* Function Call Interface */
/* libglusterfsclient initialization function.
 * @ctx : the structure described above filled with required values.
 * @fakefsid: User generated fsid to be used to identify this
 * volume.
 *
 * Returns NULL on failure and the non-NULL pointer on success.
 * On failure, the error description might be present in the logfile
 * depending on the log level.
 */
glusterfs_handle_t
glusterfs_init (glusterfs_init_params_t *ctx, uint32_t fakefsid);



/* Used to destroy a glusterfs client context and the
 * connection to the glusterfs server.
 *
 * @handle      : The glusterfs handle returned by glusterfs_init.
 */
int
glusterfs_fini (glusterfs_handle_t handle);



/* libglusterfs client provides two interfaces.
 * 1. handle-based interface
 * Functions that comprise the handle-based interface accept the
 * glusterfs_handle_t as the first argument. It specifies the
 * glusterfs client context over which to perform the operation.
 *
 * 2. Virtual Mount Point based interface:
 * Functions that do not require a handle to be given in order to
 * identify which client context to operate on. This interface
 * internally determines the corresponding client context for the
 * given path. The down-side is that a virtual mount point (VMP) needs to be
 * registered with the library. A VMP is just a string that maps to a
 * glusterfs_handle_t. The advantage of a VMP based interface is that
 * a user program using multiple client contexts does not need to
 * maintain its own mapping between paths and the corresponding
 * handles.
 */



/* glusterfs_mount is the function that allows users to register a VMP
 * along with the parameters, which will be used to initialize a
 * context. Applications calling glusterfs_mount do not need to
 * initialized a context using the glusterfs_init interface.
 *
 * @vmp         : The virtual mount point.
 * @ipars       : Initialization parameters populated as described
 *              earlier.
 *
 * Returns 0 on success, and -1 on failure.
 */
int
glusterfs_mount (char *vmp, glusterfs_init_params_t *ipars);



/* glusterfs_umount is the VMP equivalent of glusterfs_fini.
 *
 * @vmp         : The VMP which was initialized using glusterfs_mount.
 *
 * Returns 0 on sucess, and -1 on failure.
 */
int
glusterfs_umount (char *vmp);


/* glusterfs_umount_all unmounts all the mounts */
int
glusterfs_umount_all (void);


/* For smaller files, application can use just
 * glusterfs_get/glusterfs_get_async
 * to read the whole content. Limit of the file-sizes to be read in
 * glusterfs_get/glusterfs_get_async is passed in the size argument
 */

/* glusterfs_glh_get:
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



/* Opens a file. Corresponds to the open syscall.
 *
 * @handle      : Handle returned from glusterfs_init
 * @path        : Path to the file or directory on the glusterfs
 *              export. Must be absolute to the export on the server.
 * @flags       : flags to control open behaviour.
 * @...         : The mode_t argument that defines the mode for a new
 *              file, in case a new file is being created using the
 *              O_CREAT flag in @flags.
 *
 * Returns a non-NULL handle on success. NULL on failure and sets
 * errno accordingly.
 */
glusterfs_file_t
glusterfs_glh_open (glusterfs_handle_t handle, const char *path, int flags,
                        ...);


/* Opens a file without having to specify a handle.
 *
 * @path        : Path to the file to open in the glusterfs export.
 *              The path to the file in glusterfs export must be
 *              pre-fixed with the VMP string registered with
 *              glusterfs_mount.
 * @flags       : flags to control open behaviour.
 * @...         : The mode_t argument that defines the mode for a new
 *              file, in case a new file is being created using the
 *              O_CREAT flag in @flags.
 *
 * Returns 0 on success, -1 on failure with errno set accordingly.
 */
glusterfs_file_t
glusterfs_open (const char *path, int flags, ...);



/* Creates a file. Corresponds to the creat syscall.
 *
 * @handle      : Handle returned from glusterfs_init
 * @path        : Path to the file that needs to be created in the
 *              glusterfs export.
 * @mode        : File creation mode.
 *
 * Returns the file handle on success. NULL on error with errno set as
 * required.
 */
glusterfs_file_t
glusterfs_glh_creat (glusterfs_handle_t handle, const char *path, mode_t mode);



/* VMP-based creat.
 * @path        : Path to the file to be created. Must be
 *              pre-prepended with the VMP string registered with
 *              glusterfs_mount.
 * @mode        : File creation mode.
 *
 * Returns file handle on success. NULL handle on error with errno set
 * accordingly.
 */
glusterfs_file_t
glusterfs_creat (const char *path, mode_t mode);



/* Close the file identified by the handle.
 *
 * @fd          : Closes the file.
 *
 * Returns 0 on success, -1 on error with errno set accordingly.
 */
int
glusterfs_close (glusterfs_file_t fd);



/* Get struct stat for the file in path.
 *
 * @handle      : The handle that identifies a glusterfs client
 *              context.
 * @path        : The file for which we need to get struct stat.
 * @stbuf       : The buffer into which the file's stat is copied.
 *
 * Returns 0 on success and -1 on error with errno set accordingly.
 */
int
glusterfs_glh_stat (glusterfs_handle_t handle, const char *path,
                        struct stat *stbuf);


/* Get struct stat for file in path.
 *
 * @path        : The file for which struct stat is required.
 * @sbuf        : The buffer into which the stat structure is copied.
 *
 * Returns 0 on success and -1 on error with errno set accordingly.
 */
int
glusterfs_stat (const char *path, struct stat *buf);



/* Gets stat struct for the file.
 *
 * @handle      : The handle identifying a glusterfs client context.
 * @path        : Path to the file for which stat structure is
 *              required. If path is a symlink, the symlink is
 *              interpreted and the stat structure returned for the
 *              target of the link.
 * @buf         : The buffer into which the stat structure is copied.
 *
 * Returns 0 on success and -1 on error with errno set accordingly.
 */
int
glusterfs_glh_lstat (glusterfs_handle_t handle, const char *path,
                        struct stat *buf);



/* Gets stat struct for a file.
 *
 * @path        : The file to get the struct stat for.
 * @buf         : The receiving struct stat buffer.
 *
 * Returns 0 on success and -1 on error with errno set accordingly.
 */
int
glusterfs_lstat (const char *path, struct stat *buf);



/* Get stat structure for a file.
 *
 * @fd          : The file handle identifying a file on the glusterfs
 *              server.
 * @stbuf       : The buffer into which the stat data is copied.
 *
 * Returns 0 on success and -1 on error with errno set accordingly.
 */
int
glusterfs_fstat (glusterfs_file_t fd, struct stat *stbuf);

int
glusterfs_glh_setxattr (glusterfs_handle_t handle, const char *path,
                                const char *name, const void *value,
                                size_t size, int flags);

int
glusterfs_glh_lsetxattr (glusterfs_handle_t handle, const char *path,
                         const char *name, const void *value, size_t size,
                         int flags);

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
glusterfs_glh_lgetxattr (glusterfs_handle_t handle, const char *path,
		         const char *name, void *value, size_t size);

ssize_t
glusterfs_getxattr (const char *path, const char *name, void *value,
                        size_t size);

ssize_t
glusterfs_lgetxattr (const char *path, const char *name, void *value,
                     size_t size);

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



/* Read data from a file.
 * @fd          : Handle returned by glusterfs_open or
 *              glusterfs_glh_open.
 * @buf         : Buffer to read the data into.
 * @nbytes      : Number of bytes to read.
 *
 * Returns number of bytes actually read on success or -1 on error
 * with errno set to the appropriate error number.
 */
ssize_t
glusterfs_read (glusterfs_file_t fd, void *buf, size_t nbytes);



/* Read data into an array of buffers.
 *
 * @fd          : File handle returned by glusterfs_open or
 *              glusterfs_glh_open.
 * @vec         : Array of buffers into which the data is read.
 * @count       : Number of iovecs referred to by vec.
 *
 * Returns number of bytes read on success or -1 on error with errno
 * set appropriately.
 */
ssize_t
glusterfs_readv (glusterfs_file_t fd, const struct iovec *vec, int count);

int
glusterfs_read_async (glusterfs_file_t fd, size_t nbytes, off_t offset,
                      glusterfs_readv_cbk_t readv_cbk, void *cbk_data);



/* Write data into a file.
 *
 * @fd          : File handle returned from glusterfs_open or
 *              glusterfs_glh_open.
 * @buf         : Buffer which is written to the file.
 * @nbytes      : Number bytes of the @buf written to the file.
 *
 * On success, returns number of bytes written. On error, returns -1
 * with errno set appropriately.
 */
ssize_t
glusterfs_write (glusterfs_file_t fd, const void *buf, size_t nbytes);



/* Writes an array of buffers into a file.
 *
 * @fd          : The file handle returned from glusterfs_open or
 *              glusterfs_glh_open.
 * @vector      : Array of buffers to be written to the file.
 * @count       : Number of separate buffers in the @vector array.
 *
 * Returns number of bytes written on success or -1 on error with
 * errno set approriately.
 */
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



/* Read from a file starting at a given offset.
 *
 * @fd          : File handle returned from glusterfs_open or
 *              glusterfs_glh_open.
 * @buf         : Buffer to read the data into.
 * @nbytes      : Number of bytes to read.
 * @offset      : The offset to start reading @nbytes from.
 *
 * Returns number of bytes read on success or -1 on error with errno
 * set appropriately.
 */
ssize_t
glusterfs_pread (glusterfs_file_t fd, void *buf, size_t nbytes, off_t offset);



/* Write to a file starting at a given offset.
 *
 * @fd          : Flie handle returned from glusterfs_open or
 *              glusterfs_glh_open.
 * @buf         : Buffer that will be written to the file.
 * @nbytes      : Number of bytes to write from @buf.
 * @offset      : The starting offset from where @nbytes will be
 *              written.
 *
 * Returns number of bytes written on success and -1 on error with
 * errno set appropriately.
 */
ssize_t
glusterfs_pwrite (glusterfs_file_t fd, const void *buf, size_t nbytes,
		  off_t offset);



/* Seek to an offset in the file.
 *
 * @fd          : File handle in which to seek to. File handle
 *              returned by glusterfs_open or glusterfs_glh_open.
 * @offset      : Offset to seek to in the given file.
 * @whence      : Determines how the offset is interpreted by this
 *              syscall. The behaviour is similar to the options
 *              provided by the POSIX lseek system call. See man lseek
 *              for more details.
 *
 * On success, returns the resulting absolute offset in the file after the seek
 * operation is performed. ON error, returns -1 with errno set
 * appropriately.
 */
off_t
glusterfs_lseek (glusterfs_file_t fd, off_t offset, int whence);



/* Create a directory.
 *
 * @handle      : The handle of the glusterfs context in which the
 *              directory needs to be created.
 * @path        : The absolute path within the glusterfs context where
 *              the directory needs to be created.
 * @mode        : The mode bits for the newly created directory.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_glh_mkdir (glusterfs_handle_t handle, const char *path, mode_t mode);



/* Create a directory.
 *
 * @path        : Path to the directory that needs to be created. This
 *              path must be prefixed with the VMP of the particular glusterfs
 *              context.
 * @mode        : Mode flags for the newly created directory.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_mkdir (const char *path, mode_t mode);



/* Remove a directory.
 *
 * @handle      : Handle of the glusterfs context from which to remove
 *              the directory.
 * @path        : The path of the directory to be removed in the glusterfs
 *              context.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_glh_rmdir (glusterfs_handle_t handle, const char *path);



/* Remove a directory.
 *
 * @path        : The absolute path to the directory to be removed.
 *              This path must be pre-fixed with the VMP of the
 *              particular glusterfs context in which this directory
 *              resides.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_rmdir (const char *path);



/* Read directory entries.
 *
 * @fd          : The handle of the directory to be read. This handle
 *              is the one returned by opendir.
 *
 * Returns the directory entry on success and NULL pointer on error
 * with errno set appropriately.
 */
void *
glusterfs_readdir (glusterfs_dir_t dirfd);



/* re-entrant version of glusterfs_readdir.
 *
 * @dirfd       : The handle of directory to be read. This handle is the one
 *                returned by opendir.
 * @entry       : Pointer to storage to store a directory entry. The storage
 *                pointed to by entry shall be large enough for a dirent with 
 *                an array of char d_name members containing at least
 *                {NAME_MAX}+1 elements.
 * @result      : Upon successful return, the pointer returned at *result shall
 *                have the same value as the argument entry. Upon reaching the
 *                end of the directory stream, this pointer shall have the
 *                value NULL.
 */
int
glusterfs_readdir_r (glusterfs_dir_t dirfd, struct dirent *entry,
                     struct dirent **result);

/* Close a directory handle.
 *
 * @fd          : The directory handle to be closed.
 *
 * Returns 0 on success and -1 on error with errno set to 0.
 */
int
glusterfs_closedir (glusterfs_dir_t dirfd);
/* FIXME: remove getdents */
int
glusterfs_getdents (glusterfs_dir_t fd, struct dirent *dirp,
		    unsigned int count);



/* Create device node.
 *
 * @handle      : glusterfs context in which to create the device
 *              node.
 * @pathname    : The absolute path of the device to be created in the
 *              given glusterfs context.
 *
 * @mode        : Mode flags to apply to the newly created node.
 * @dev         : Device numbers that will apply to the node.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_glh_mknod(glusterfs_handle_t handle, const char *pathname,
                        mode_t mode, dev_t dev);



/* Create a device node.
 *
 * @pathname    : The full path of the node to be created. This path
 *              should be pre-pended with the VMP of the glusterfs
 *              context in which this node is to be created.
 * @mode        : Mode flags that will be applied to the newly created
 *              device file.
 * @dev         : The device numbers that will be associated with the
 *              device node.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_mknod(const char *pathname, mode_t mode, dev_t dev);



/* Returns the real absolute path of the given path.
 *
 * @handle              : The glusterfs context in which the path resides in.
 * @path                : The path to be resolved.
 * @resolved_path       : The resolved path is stored in this buffer
 *                      provided by the caller.
 *
 * Returns a pointer to resolved_path on success and NULL on error
 * with errno set appropriately.
 *
 * See man realpath for details.
 */
char *
glusterfs_glh_realpath (glusterfs_handle_t handle, const char *path,
                        char *resolved_path);


/* Returns the real absolute path of the given path.
 *
 * @path                : The path to be resolved. This path must be
 *                      pre-fixed with the VMP of the glusterfs
 *                      context in which the file resides.
 *
 * @resolved_path       : The resolved path is stored in this user
 *                      provided buffer.
 *
 * Returns a pointer to resolved_path on success, and NULL on error
 * with errno set appropriately.
 */
char *
glusterfs_realpath (const char *path, char *resolved_path);



/* Change mode flags on a path.
 *
 * @handle      : Handle of the glusterfs instance in which the path
 *              resides.
 * @path        : The path whose mode bits need to be changed.
 * @mode        : The new mode bits.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_glh_chmod (glusterfs_handle_t handle, const char *path, mode_t mode);



/* Change mode flags on a path.
 *
 * @path        : The path whose mode bits need to be changed. The
 *              path should be pre-fixed with the VMP that identifies the
 *              glusterfs context within which the path resides.
 * @mode        : The new mode bits.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_chmod (const char *path, mode_t mode);



/* Change the owner of a path.
 * If @path is a symlink, it is dereferenced and the ownership change
 * happens on the target.
 *
 * @handle      : Handle of the glusterfs context in which the path
 *              resides.
 * @path        : The path whose owner needs to be changed.
 * @owner       : ID of the new owner.
 * @group       : ID of the new group.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_glh_chown (glusterfs_handle_t handle, const char *path, uid_t owner,
                        gid_t group);



/* Change the owner of a path.
 *
 * If @path is a symlink, it is dereferenced and the ownership change
 * happens on the target.
 * @path        : The path whose owner needs to be changed. Path must
 *              be pre-fixed with the VMP that identifies the
 *              glusterfs context in which the path resides.
 * @owner       : ID of the new owner.
 * @group       : ID of the new group.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_chown (const char *path, uid_t owner, gid_t group);



/* Change the owner of the file.
 *
 * @fd          : Handle of the file whose owner needs to be changed.
 * @owner       : ID of the new owner.
 * @group       : ID of the new group.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_fchown (glusterfs_file_t fd, uid_t owner, gid_t group);



/* Open a directory.
 *
 * @handle      : Handle that identifies a glusterfs context.
 * @path        : Path to the directory in the glusterfs context.
 *
 * Returns a non-NULL handle on success and NULL on failure with errno
 * set appropriately.
 */
glusterfs_dir_t
glusterfs_glh_opendir (glusterfs_handle_t handle, const char *path);



/* Open a directory.
 *
 * @path        : Path to the directory. The path must be prepended
 *              with the VMP in order to identify the glusterfs
 *              context in which path resides.
 *
 * Returns a non-NULL handle on success and NULL on failure with errno
 * set appropriately.
 */
glusterfs_dir_t
glusterfs_opendir (const char *path);



/* Change the mode bits on an open file.
 *
 * @fd          : The file whose mode bits need to be changed.
 * @mode        : The new mode bits.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_fchmod (glusterfs_file_t fd, mode_t mode);



/* Sync the file contents to storage.
 *
 * @fd          : The file whose contents need to be sync'ed to
 *              storage.
 *
 * Return 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_fsync (glusterfs_file_t *fd);



/* Truncate an open file.
 *
 * @fd          : The file to truncate.
 * @length      : The length to truncate to.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_ftruncate (glusterfs_file_t fd, off_t length);



/* Create a hard link between two paths.
 *
 * @handle      : glusterfs context in which both paths should reside.
 * @oldpath     : The existing path to link to.
 * @newpath     : The new path which will be linked to @oldpath.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_glh_link (glusterfs_handle_t handle, const char *oldpath,
                        const char *newpath);



/* Create a hard link between two paths.
 *
 * @oldpath     : The existing path to link to.
 * @newpath     : The new path which will be linked to @oldpath.
 *
 * Both paths should exist on the same glusterfs context and should be
 * prefixed with the same VMP.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_link (const char *oldpath, const char *newpath);



/* Get stats about the underlying file system.
 *
 * @handle      : Identifies the glusterfs context in which resides
 *              the given path.
 * @path        : stats are returned for the file system on which file
 *              is located.
 * @buf         : The buffer into which the stats are copied.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_glh_statfs (glusterfs_handle_t handle, const char *path,
                        struct statfs *buf);



/* Get stats about the underlying file system.
 *
 * @path        : stats are returned for the file system on which file
 *              is located. @path must start with the VMP of the
 *              glusterfs context on which the file reside.
 * @buf         : The buffer into which the stats are copied.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_statfs (const char *path, struct statfs *buf);



/* Get stats about the underlying file system.
 *
 * @handle      : Identifies the glusterfs context in which resides
 *              the given path.
 * @path        : stats are returned for the file system on which file
 *              is located.
 * @buf         : The buffer into which the stats are copied.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_glh_statvfs (glusterfs_handle_t handle, const char *path,
                   struct statvfs *buf);



/* Get stats about the underlying file system.
 *
 * @path        : stats are returned for the file system on which file
 *              is located. @path must start with the VMP of the
 *              glusterfs context on which the file reside.
 * @buf         : The buffer into which the stats are copied.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_statvfs (const char *path, struct statvfs *buf);



/* Set the atime and mtime values for a given path.
 *
 * @handle      : The handle identifying the glusterfs context.
 * @path        : The path for which the times need to be changed.
 * @times       : The array containing new time stamps for the file.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_glh_utimes (glusterfs_handle_t handle, const char *path,
                        const struct timeval times[2]);



/* Set the atime and mtime values for a given path.
 *
 * @path        : The path for which the times need to be changed.
 * @times       : The array containing new time stamps for the file.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_utimes (const char *path, const struct timeval times[2]);



/* Set the atime and mtime values for a given path.
 *
 * @handle      : The handle identifying the glusterfs context.
 * @path        : The path for which the times need to be changed.
 * @buf         : The structure containing new time stamps for the file.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_glh_utime (glusterfs_handle_t handle, const char *path,
                        const struct utimbuf *buf);



/* Set the atime and mtime values for a given path.
 *
 * @path        : The path for which the times need to be changed.
 * @buf         : The structure containing new time stamps for the file.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_utime (const char *path, const struct utimbuf *buf);



/*  Create  FIFO at the given path.
 *
 *  @handle     : The glusterfs context in which to create that FIFO.
 *  @path       : The path within the context where the FIFO is to be
 *              created.
 * @mode        : The mode bits for the newly create FIFO.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_glh_mkfifo (glusterfs_handle_t handle, const char *path,
                        mode_t mode);



/*  Create  FIFO at the given path.
 *
 *  @path       : The path within the context where the FIFO is to be
 *              created. @path should begin with the VMP of the
 *              glusterfs context in which the FIFO needs to be
 *              created.
 * @mode        : The mode bits for the newly create FIFO.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_mkfifo (const char *path, mode_t mode);



/* Unlink a file.
 *
 * @handle      : Handle that identifies a glusterfs instance.
 * @path        : Path in the glusterfs instance that needs to be
 *              unlinked.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_glh_unlink (glusterfs_handle_t handle, const char *path);



/* Unlink a file.
 *
 * @path        : Path in the glusterfs instance that needs to be
 *              unlinked.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_unlink (const char *path);



/* Create a symbolic link.
 *
 * @handle      : The handle identifying the glusterfs context.
 * @oldpath     : The existing path to which a symlink needs to be
 *              created.
 * @newpath     : The new path which will be symlinked to the
 *              @oldpath.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_glh_symlink (glusterfs_handle_t handle, const char *oldpath,
                                const char *newpath);



/* Create a symbolic link.
 *
 * @oldpath     : The existing path to which a symlink needs to be
 *              created.
 * @newpath     : The new path which will be symlinked to the
 *              @oldpath.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_symlink (const char *oldpath, const char *newpath);



/* Read a symbolic link.
 *
 * @handle      : Handle identifying the glusterfs context.
 * @path        : The symlink that needs to be read.
 * @buf         : The buffer into which the target of @path will be
 *              stored.
 * @bufsize     : Size of the buffer allocated to @buf.
 *
 * Returns number of bytes copied into @buf and -1 on error with errno
 * set appropriately.
 */
ssize_t
glusterfs_glh_readlink (glusterfs_handle_t handle, const char *path, char *buf,
                                size_t bufsize);



/* Read a symbolic link.
 *
 * @path        : The symlink that needs to be read.
 * @buf         : The buffer into which the target of @path will be
 *              stored.
 * @bufsize     : Size of the buffer allocated to @buf.
 *
 * Returns number of bytes copied into @buf and -1 on error with errno
 * set appropriately.
 */
ssize_t
glusterfs_readlink (const char *path, char *buf, size_t bufsize);



/* Rename a file or directory.
 *
 * @handle      : The identifier of a glusterfs context.
 * @oldpath     : The path to be renamed.
 * @newpath     : The new name for the @oldpath.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_glh_rename (glusterfs_handle_t handle, const char *oldpath,
                      const char *newpath);



/* Rename a file or directory.
 * @oldpath     : The path to be renamed.
 * @newpath     : The new name for the @oldpath.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_rename (const char *oldpath, const char *newpath);



/* Remove a file or directory in the given glusterfs context.
 *
 * @handle      : Handle identifying the glusterfs context.
 * @path        : Path of the file or directory to be removed.
 *
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_glh_remove (glusterfs_handle_t handle, const char *path);



/* Remove a file or directory.
 *
 * @path        : Path of the file or directory to be removed. The
 *              path must be pre-fixed with the VMP.
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_remove (const char *path);



/* Change the owner of the given path.
 *
 * If @path is a symlink, the ownership change happens on the symlink.
 *
 * @handle      : Handle identifying the glusterfs client context.
 * @path        : Path whose owner needs to be changed.
 * @owner       : New owner ID
 * @group       : New Group ID
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */
int
glusterfs_glh_lchown (glusterfs_handle_t handle, const char *path, uid_t owner,
                      gid_t group);



/* Change the owner of the given path.
 *
 * If @path is a symlink, the ownership change happens on the symlink.
 *
 * @path        : Path whose owner needs to be changed.
 * @owner       : New owner ID
 * @group       : New Group ID
 *
 * Returns 0 on success and -1 on error with errno set appropriately.
 */

int
glusterfs_lchown (const char *path, uid_t owner, gid_t group);



/* Rewind directory stream pointer to beginning of the directory.
 *
 * @dirfd       : Directory handle returned by glusterfs_open on
 *              glusterfs_opendir.
 *
 * Returns no value.
 */
void
glusterfs_rewinddir (glusterfs_dir_t dirfd);



/* Seek to the given offset in the directory handle.
 *
 * @dirfd       : Directory handle returned by glusterfs_open on
 *              glusterfs_opendir.
 * @offset      : The offset to seek to.
 *
 * Returns no value.
 */
void
glusterfs_seekdir (glusterfs_dir_t dirfd, off_t offset);



/* Return the current offset in a directory stream.
 *
 * @dirfd       : Directory handle returned by glusterfs_open on
 *              glusterfs_opendir.
 *
 * Returns the offset in the directory or -1 on error with errno set
 * appropriately.
 */
off_t
glusterfs_telldir (glusterfs_dir_t dirfd);


/* Write count bytes from in_fd to out_fd, starting at *offset.
 * glusterfs_sendfile aims at eliminating memory copy at the end of
 * each read from in_fd, copying the file directly to out_fd from the buffer 
 * provided by glusterfs.
 *
 * @out_fd: file descriptor opened for writing
 *
 * @in_fd: glusterfs file handle to the file to be read from.
 *
 * @offset: If offset is not NULL, then it points to a variable holding the file
 *          offset  from  which  glusterfs_sendfile()  will  start reading data
 *          from in_fd.  When glusterfs_sendfile() returns, this variable will 
 *          be set to the offset of the byte following the last byte that was 
 *          read.  If offset is  not  NULL, then glusterfs_sendfile()  does  not
 *          modify the current file offset of in_fd; otherwise the current file
 *          offset is adjusted to reflect the number of bytes read from in_fd.
 *
 * @count:  number of bytes to copy between the file descriptors.
 */

ssize_t
glusterfs_sendfile (int out_fd, glusterfs_file_t in_fd, off_t *offset,
                    size_t count);

/* manipulate file descriptor
 * This api can have 3 forms similar to fcntl(2).
 *
 * int
 * glusterfs_fcntl (glusterfs_file_t fd, int cmd)
 *
 * int
 * glusterfs_fcntl (glusterfs_file_t fd, int cmd, long arg)
 *
 * int
 * glusterfs_fcntl (glusterfs_file_t fd, int cmd, struct flock *lock)
 *
 * @fd   : file handle returned by glusterfs_open or glusterfs_create.
 * @cmd  : Though the aim is to implement all possible commands supported by
 *         fcntl(2), currently following commands are supported.
 *         F_SETLK, F_SETLKW, F_GETLK -  used to acquire, release, and test for
 *                                       the existence of record locks (also 
 *                                       known as file-segment or file-region
 *                                       locks). More detailed explanation is
 *                                       found in 'man 2 fcntl'
 */
   
int
glusterfs_fcntl (glusterfs_file_t fd, int cmd, ...);

/*
 * Change the current working directory to @path
 * 
 * @path  : path to change the current working directory to.
 *
 * Returns 0 on success and -1 on failure with errno set appropriately.
 */
int
glusterfs_chdir (const char *path);

/*
 * Change the current working directory to the path @fd is opened on.
 *
 * @fd   : current working directory will be changed to path @fd is opened on.
 *
 * Returns 0 on success and -1 on  with errno set appropriately.
 */
int
glusterfs_fchdir (glusterfs_file_t fd);

/* copies the current working directory into @buf if it is big enough
 *
 * @buf: buffer to copy into it. If @buf is NULL, a buffer will be allocated.
 *       The size of the buffer will be @size if it is not zero, otherwise the
 *       size will be big enough to hold the current working directory.
 * @size: size of the buffer.
 *
 * Returns the pointer to buffer holding current working directory on success
 * and NULL on failure.
 */

char *
glusterfs_getcwd (char *buf, size_t size);

/* FIXME: review the need for these apis */
/* added for log related initialization in booster fork implementation */
void
glusterfs_reset (void);

void
glusterfs_log_lock (void);

void
glusterfs_log_unlock (void);
/* Used to free the glusterfs_read_buf passed to the application from
   glusterfs_read_async_cbk
*/
void
glusterfs_free (glusterfs_iobuf_t *buf);

__END_DECLS

#endif /* !_LIBGLUSTERFSCLIENT_H */
