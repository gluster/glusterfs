/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef _GLFS_H
#define _GLFS_H

/*
  Enforce the following flags as libgfapi is built
  with them, and we want programs linking against them to also
  be built with these flags. This is necessary as it affects
  some of the structures defined in libc headers (like struct stat)
  and those definitions need to be consistently compiled in
  both the library and the application.
*/

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#ifndef __USE_FILE_OFFSET64
#define __USE_FILE_OFFSET64
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/cdefs.h>
#include <dirent.h>
#include <sys/statvfs.h>

__BEGIN_DECLS

/* The filesystem object. One object per 'virtual mount' */
struct glfs;
typedef struct glfs glfs_t;


/*
  SYNOPSIS

  glfs_new: Create a new 'virtual mount' object.

  DESCRIPTION

  This is most likely the very first function you will use. This function
  will create a new glfs_t (virtual mount) object in memory.

  On this newly created glfs_t, you need to be either set a volfile path
  (glfs_set_volfile) or a volfile server (glfs_set_volfile_server).

  The glfs_t object needs to be initialized with glfs_init() before you
  can start issuing file operations on it.

  PARAMETERS

  @volname: Name of the volume. This identifies the server-side volume and
            the fetched volfile (equivalent of --volfile-id command line
	    parameter to glusterfsd). When used with glfs_set_volfile() the
	    @volname has no effect (except for appearing in log messages).

  RETURN VALUES

  NULL   : Out of memory condition.
  Others : Pointer to the newly created glfs_t virtual mount object.

*/

glfs_t *glfs_new (const char *volname);


/*
  SYNOPSIS

  glfs_set_volfile: Specify the path to the volume specification file.

  DESCRIPTION

  If you are using a static volume specification file (without dynamic
  volume management abilities from the CLI), then specify the path to
  the volume specification file.

  This is incompatible with glfs_set_volfile_server().

  PARAMETERS

  @fs: The 'virtual mount' object to be configured with the volume
       specification file.

  @volfile: Path to the locally available volume specification file.

  RETURN VALUES

   0 : Success.
  -1 : Failure. @errno will be set with the type of failure.

*/

int glfs_set_volfile (glfs_t *fs, const char *volfile);


/*
  SYNOPSIS

  glfs_set_volfile_server: Specify the address of management server.

  DESCRIPTION

  This function specifies the address of the management server (glusterd)
  to connect, and establish the volume configuration. The @volname
  parameter passed to glfs_new() is the volume which will be virtually
  mounted as the glfs_t object. All operations performed by the CLI at
  the management server will automatically be reflected in the 'virtual
  mount' object as it maintains a connection to glusterd and polls on
  configuration change notifications.

  This is incompatible with glfs_set_volfile().

  PARAMETERS

  @fs: The 'virtual mount' object to be configured with the volume
       specification file.

  @transport: String specifying the transport used to connect to the
              management daemon. Specifying NULL will result in the usage
	      of the default (tcp) transport type. Permitted values
	      are those what you specify as transport-type in a volume
	      specification file (e.g "tcp", "rdma", "unix".)

  @host: String specifying the address of where to find the management
         daemon. Depending on the transport type this would either be
	 an FQDN (e.g: "storage01.company.com"), ASCII encoded IP
	 address "192.168.22.1", or a UNIX domain socket path (e.g
	 "/tmp/glusterd.socket".)

  @port: The TCP port number where gluster management daemon is listening.
         Specifying 0 uses the default port number GF_DEFAULT_BASE_PORT.
	 This parameter is unused if you are using a UNIX domain socket.

  RETURN VALUES

   0 : Success.
  -1 : Failure. @errno will be set with the type of failure.

*/

int glfs_set_volfile_server (glfs_t *fs, const char *transport,
			     const char *host, int port);


/*
  SYNOPSIS

  glfs_set_logging: Specify logging parameters.

  DESCRIPTION

  This function specifies logging parameters for the virtual mount.
  Default log file is /dev/null.

  PARAMETERS

  @fs: The 'virtual mount' object to be configured with the logging parameters.

  @logfile: The logfile to be used for logging. Will be created if it does not
            already exist (provided system permissions allow.)

  @loglevel: Numerical value specifying the degree of verbosity. Higher the
             value, more verbose the logging.

  RETURN VALUES

   0 : Success.
  -1 : Failure. @errno will be set with the type of failure.

*/

int glfs_set_logging (glfs_t *fs, const char *logfile, int loglevel);


/*
  SYNOPSIS

  glfs_init: Initialize the 'virtual mount'

  DESCRIPTION

  This function initializes the glfs_t object. This consists of many steps:
  - Spawn a poll-loop thread.
  - Establish connection to management daemon and receive volume specification.
  - Construct translator graph and initialize graph.
  - Wait for initialization (connecting to all bricks) to complete.

  PARAMETERS

  @fs: The 'virtual mount' object to be initialized.

  RETURN VALUES

   0 : Success.
  -1 : Failure. @errno will be set with the type of failure.

*/

int glfs_init (glfs_t *fs);


/*
  SYNOPSIS

  glfs_fini: Cleanup and destroy the 'virtual mount'

  DESCRIPTION

  This function attempts to gracefully destroy glfs_t object. An attempt is
  made to wait for all background processing to complete before returning.

  glfs_fini() must be called after all operations on glfs_t is finished.

  IMPORTANT

  IT IS NECESSARY TO CALL glfs_fini() ON ALL THE INITIALIZED glfs_t
  OBJECTS BEFORE TERMINATING THE PROGRAM. THERE MAY BE CACHED AND
  UNWRITTEN / INCOMPLETE OPERATIONS STILL IN PROGRESS EVEN THOUGH THE
  API CALLS HAVE RETURNED. glfs_fini() WILL WAIT FOR BACKGROUND OPERATIONS
  TO COMPLETE BEFORE RETURNING, THEREBY MAKING IT SAFE FOR THE PROGRAM TO
  EXIT.

  PARAMETERS

  @fs: The 'virtual mount' object to be destroyed.

  RETURN VALUES

   0 : Success.
*/

int glfs_fini (glfs_t *fs);

/*
 * FILE OPERATION
 *
 * What follows are filesystem operations performed on the
 * 'virtual mount'. The calls here are kept as close to
 * the POSIX system calls as possible.
 *
 * Notes:
 *
 * - All paths specified, even if absolute, are relative to the
 *   root of the virtual mount and not the system root (/).
 *
 */

/* The file descriptor object. One per open file/directory. */

struct glfs_fd;
typedef struct glfs_fd glfs_fd_t;


/*
  SYNOPSIS

  glfs_open: Open a file.

  DESCRIPTION

  This function opens a file on a virtual mount.

  PARAMETERS

  @fs: The 'virtual mount' object to be initialized.

  @path: Path of the file within the virtual mount.

  @flags: Open flags. See open(2). O_CREAT is not supported.
          Use glfs_creat() for creating files.

  RETURN VALUES

  NULL   : Failure. @errno will be set with the type of failure.
  Others : Pointer to the opened glfs_fd_t.

 */

glfs_fd_t *glfs_open (glfs_t *fs, const char *path, int flags);


/*
  SYNOPSIS

  glfs_creat: Create a file.

  DESCRIPTION

  This function opens a file on a virtual mount.

  PARAMETERS

  @fs: The 'virtual mount' object to be initialized.

  @path: Path of the file within the virtual mount.

  @mode: Permission of the file to be created.

  @flags: Create flags. See open(2). O_EXCL is supported.

  RETURN VALUES

  NULL   : Failure. @errno will be set with the type of failure.
  Others : Pointer to the opened glfs_fd_t.

 */

glfs_fd_t *glfs_creat (glfs_t *fs, const char *path, int flags,
		       mode_t mode);

int glfs_close (glfs_fd_t *fd);

glfs_t *glfs_from_glfd (glfs_fd_t *fd);

int glfs_set_xlator_option (glfs_t *fs, const char *xlator, const char *key,
			    const char *value);

typedef void (*glfs_io_cbk) (glfs_fd_t *fd, ssize_t ret, void *data);

// glfs_{read,write}[_async]

ssize_t glfs_read (glfs_fd_t *fd, void *buf, size_t count, int flags);
ssize_t glfs_write (glfs_fd_t *fd, const void *buf, size_t count, int flags);
int glfs_read_async (glfs_fd_t *fd, void *buf, size_t count, int flags,
		     glfs_io_cbk fn, void *data);
int glfs_write_async (glfs_fd_t *fd, const void *buf, size_t count, int flags,
		      glfs_io_cbk fn, void *data);

// glfs_{read,write}v[_async]

ssize_t glfs_readv (glfs_fd_t *fd, const struct iovec *iov, int iovcnt,
		    int flags);
ssize_t glfs_writev (glfs_fd_t *fd, const struct iovec *iov, int iovcnt,
		     int flags);
int glfs_readv_async (glfs_fd_t *fd, const struct iovec *iov, int count,
		      int flags, glfs_io_cbk fn, void *data);
int glfs_writev_async (glfs_fd_t *fd, const struct iovec *iov, int count,
		       int flags, glfs_io_cbk fn, void *data);

// glfs_p{read,write}[_async]

ssize_t glfs_pread (glfs_fd_t *fd, void *buf, size_t count, off_t offset,
		    int flags);
ssize_t glfs_pwrite (glfs_fd_t *fd, const void *buf, size_t count,
		     off_t offset, int flags);
int glfs_pread_async (glfs_fd_t *fd, void *buf, size_t count, off_t offset,
		      int flags, glfs_io_cbk fn, void *data);
int glfs_pwrite_async (glfs_fd_t *fd, const void *buf, int count, off_t offset,
		       int flags, glfs_io_cbk fn, void *data);

// glfs_p{read,write}v[_async]

ssize_t glfs_preadv (glfs_fd_t *fd, const struct iovec *iov, int iovcnt,
		     off_t offset, int flags);
ssize_t glfs_pwritev (glfs_fd_t *fd, const struct iovec *iov, int iovcnt,
		      off_t offset, int flags);
int glfs_preadv_async (glfs_fd_t *fd, const struct iovec *iov, int count,
		       off_t offset, int flags, glfs_io_cbk fn, void *data);
int glfs_pwritev_async (glfs_fd_t *fd, const struct iovec *iov, int count,
			off_t offset, int flags, glfs_io_cbk fn, void *data);


off_t glfs_lseek (glfs_fd_t *fd, off_t offset, int whence);

int glfs_truncate (glfs_t *fs, const char *path, off_t length);

int glfs_ftruncate (glfs_fd_t *fd, off_t length);
int glfs_ftruncate_async (glfs_fd_t *fd, off_t length, glfs_io_cbk fn,
			  void *data);

int glfs_lstat (glfs_t *fs, const char *path, struct stat *buf);
int glfs_stat (glfs_t *fs, const char *path, struct stat *buf);
int glfs_fstat (glfs_fd_t *fd, struct stat *buf);

int glfs_fsync (glfs_fd_t *fd);
int glfs_fsync_async (glfs_fd_t *fd, glfs_io_cbk fn, void *data);

int glfs_fdatasync (glfs_fd_t *fd);
int glfs_fdatasync_async (glfs_fd_t *fd, glfs_io_cbk fn, void *data);

int glfs_access (glfs_t *fs, const char *path, int mode);

int glfs_symlink (glfs_t *fs, const char *oldpath, const char *newpath);

int glfs_readlink (glfs_t *fs, const char *path, char *buf, size_t bufsiz);

int glfs_mknod (glfs_t *fs, const char *path, mode_t mode, dev_t dev);

int glfs_mkdir (glfs_t *fs, const char *path, mode_t mode);

int glfs_unlink (glfs_t *fs, const char *path);

int glfs_rmdir (glfs_t *fs, const char *path);

int glfs_rename (glfs_t *fs, const char *oldpath, const char *newpath);

int glfs_link (glfs_t *fs, const char *oldpath, const char *newpath);

glfs_fd_t *glfs_opendir (glfs_t *fs, const char *path);

int glfs_readdir_r (glfs_fd_t *fd, struct dirent *dirent,
		    struct dirent **result);

int glfs_readdirplus_r (glfs_fd_t *fd, struct stat *stat, struct dirent *dirent,
			struct dirent **result);

long glfs_telldir (glfs_fd_t *fd);

void glfs_seekdir (glfs_fd_t *fd, long offset);

int glfs_closedir (glfs_fd_t *fd);

int glfs_statvfs (glfs_t *fs, const char *path, struct statvfs *buf);

int glfs_chmod (glfs_t *fs, const char *path, mode_t mode);

int glfs_fchmod (glfs_fd_t *fd, mode_t mode);

int glfs_chown (glfs_t *fs, const char *path, uid_t uid, gid_t gid);

int glfs_lchown (glfs_t *fs, const char *path, uid_t uid, gid_t gid);

int glfs_fchown (glfs_fd_t *fd, uid_t uid, gid_t gid);

int glfs_utimens (glfs_t *fs, const char *path, struct timespec times[2]);

int glfs_lutimens (glfs_t *fs, const char *path, struct timespec times[2]);

int glfs_futimens (glfs_fd_t *fd, struct timespec times[2]);

ssize_t glfs_getxattr (glfs_t *fs, const char *path, const char *name,
		       void *value, size_t size);

ssize_t glfs_lgetxattr (glfs_t *fs, const char *path, const char *name,
			void *value, size_t size);

ssize_t glfs_fgetxattr (glfs_fd_t *fd, const char *name,
			void *value, size_t size);

ssize_t glfs_listxattr (glfs_t *fs, const char *path, void *value, size_t size);

ssize_t glfs_llistxattr (glfs_t *fs, const char *path, void *value,
			 size_t size);

ssize_t glfs_flistxattr (glfs_fd_t *fd, void *value, size_t size);

int glfs_setxattr (glfs_t *fs, const char *path, const char *name,
		   const void *value, size_t size, int flags);

int glfs_lsetxattr (glfs_t *fs, const char *path, const char *name,
		    const void *value, size_t size, int flags);

int glfs_fsetxattr (glfs_fd_t *fd, const char *name,
		    const void *value, size_t size, int flags);

int glfs_removexattr (glfs_t *fs, const char *path, const char *name);

int glfs_lremovexattr (glfs_t *fs, const char *path, const char *name);

int glfs_fremovexattr (glfs_fd_t *fd, const char *name);

char *glfs_getcwd (glfs_t *fs, char *buf, size_t size);

int glfs_chdir (glfs_t *fs, const char *path);

int glfs_fchdir (glfs_fd_t *fd);

char *glfs_realpath (glfs_t *fs, const char *path, char *resolved_path);

/*
 * @cmd and @flock are as specified in man fcntl(2).
 */
int glfs_posix_lock (glfs_fd_t *fd, int cmd, struct flock *flock);

glfs_fd_t *glfs_dup (glfs_fd_t *fd);

__END_DECLS

#endif /* !_GLFS_H */
