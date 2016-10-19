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

#if defined(HAVE_SYS_ACL_H) || (defined(USE_POSIX_ACLS) && USE_POSIX_ACLS)
#include <sys/acl.h>
#else
typedef void *acl_t;
typedef int acl_type_t;
#endif

/* Portability non glibc c++ build systems */
#ifndef __THROW
# if defined __cplusplus
#  define __THROW       throw ()
# else
#  define __THROW
# endif
#endif

#ifndef GF_DARWIN_HOST_OS
#define GFAPI_PUBLIC(sym, ver) /**/
#define GFAPI_PRIVATE(sym, ver) /**/
#else
#define GFAPI_PUBLIC(sym, ver) __asm("_" __STRING(sym) "$GFAPI_" __STRING(ver))
#define GFAPI_PRIVATE(sym, ver) __asm("_" __STRING(sym) "$GFAPI_PRIVATE_" __STRING(ver))
#endif

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

glfs_t *glfs_new (const char *volname) __THROW
        GFAPI_PUBLIC(glfs_new, 3.4.0);


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

int glfs_set_volfile (glfs_t *fs, const char *volfile) __THROW
        GFAPI_PUBLIC(glfs_set_volfile, 3.4.0);


/*
  SYNOPSIS

  glfs_set_volfile_server: Specify the list of addresses for management server.

  DESCRIPTION

  This function specifies the list of addresses for the management server
  (glusterd) to connect, and establish the volume configuration. The @volname
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
              specification file (e.g "tcp", "rdma", "unix" etc.)

  @host:      String specifying the address where to find the management daemon.
              Socket path, while using Unix domain socket as transport type.
              This would either be
              - FQDN (e.g : "storage01.company.com") or
              - ASCII (e.g : "192.168.22.1") or
              - Socket path (e.g : "/var/run/glusterd.socket")

  NOTE: This API is special, multiple calls to this function with different
        volfile servers, port or transport-type would create a list of volfile
        servers which would be polled during `volfile_fetch_attempts()`

  @port: The TCP port number where gluster management daemon is listening.
         Specifying 0 uses the default port number GF_DEFAULT_BASE_PORT.
         This parameter is unused if you are using a UNIX domain socket.

  RETURN VALUES

   0 : Success.
  -1 : Failure. @errno will be set with the type of failure.

*/

int glfs_set_volfile_server (glfs_t *fs, const char *transport,
                             const char *host, int port) __THROW
        GFAPI_PUBLIC(glfs_set_volfile_server, 3.4.0);
int glfs_unset_volfile_server (glfs_t *fs, const char *transport,
                               const char *host, int port) __THROW
        GFAPI_PUBLIC(glfs_unset_volfile_server, 3.5.1);
/*
  SYNOPSIS

  glfs_set_logging: Specify logging parameters.

  DESCRIPTION

  This function specifies logging parameters for the virtual mount.
  Default log file is /dev/null.

  PARAMETERS

  @fs: The 'virtual mount' object to be configured with the logging parameters.

  @logfile: The logfile to be used for logging. Will be created if it does not
            already exist (provided system permissions allow). If NULL, a new
            logfile will be created in default log directory associated with
            the glusterfs installation.

  @loglevel: Numerical value specifying the degree of verbosity. Higher the
             value, more verbose the logging.

  RETURN VALUES

   0 : Success.
  -1 : Failure. @errno will be set with the type of failure.

*/

int glfs_set_logging (glfs_t *fs, const char *logfile, int loglevel) __THROW
        GFAPI_PUBLIC(glfs_set_logging, 3.4.0);


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

int glfs_init (glfs_t *fs) __THROW
        GFAPI_PUBLIC(glfs_init, 3.4.0);


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

int glfs_fini (glfs_t *fs) __THROW
        GFAPI_PUBLIC(glfs_fini, 3.4.0);

/*
  SYNOPSIS

      glfs_getvol: Get the volfile associated with a 'virtual mount'

  DESCRIPTION

      Sometimes it's useful e.g. for scripts to see the volfile, so that they
      can parse it and find subvolumes to do things like split-brain resolution
      or custom layouts.  The API here was specifically intended to make access
      e.g. from Python as simple as possible.

      Note that the volume must be started (not necessarily mounted) for this
      to work.

  PARAMETERS

      @fs:  The 'virtual mount' object for which a volfile is desired
      @buf: Pointer to a place for the volfile length to be stored
      @len: Length of @buf

  RETURN VALUES

      >0: filled N bytes of buffer
       0: no volfile available
      <0: volfile length exceeds @len by N bytes (@buf unchanged)
*/

ssize_t glfs_get_volfile (glfs_t *fs, void *buf, size_t len) __THROW
        GFAPI_PUBLIC(glfs_get_volfile, 3.6.0);


/*
  SYNOPSIS

       glfs_get_volumeid: Copy the Volume UUID stored in the glfs object fs.

  DESCRIPTION

       This function when invoked for the first time sends RPC call to the
       the management server (glusterd) to fetch volume uuid and stores it
       in the glusterfs_context linked to the glfs object fs which can be used
       in the subsequent calls. Later it parses that UUID to convert it from
       cannonical string format into an opaque byte array and copy it into
       the volid array. Incase if either of the input parameters, volid or size,
       is NULL, number of bytes required to copy the volume UUID is returned.

  PARAMETERS

       @fs: The 'virtual mount' object to be used to retrieve and store
            volume's UUID.
       @volid: Pointer to a place for the volume UUID to be stored
       @size: Length of @volid

  RETURN VALUES

       -1 : Failure. @errno will be set with the type of failure.
        Others : length of the volume UUID stored.
*/

int glfs_get_volumeid (struct glfs *fs, char *volid, size_t size) __THROW
        GFAPI_PUBLIC(glfs_get_volumeid, 3.5.0);


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
 * PER THREAD IDENTITY MODIFIERS
 *
 * The following operations enable to set a per thread identity context
 * for the glfs APIs to perform operations as. The calls here are kept as close
 * to POSIX equivalents as possible.
 *
 * NOTES:
 *
 *  - setgroups is a per thread setting, hence this is named as fsgroups to be
 *    close in naming to the fs(u/g)id APIs
 *  - Typical mode of operation is to set the IDs as required, with the
 *    supplementary groups being optionally set, make the glfs call and post the
 *    glfs operation set them back to eu/gid or uid/gid as appropriate to the
 *    caller
 *  - The groups once set, need to be unset by setting the size to 0 (in which
 *    case the list argument is a do not care)
 *  - Once a process for a thread of operation choses to set the IDs, all glfs
 *    calls made from that thread would default to the IDs set for the thread.
 *    As a result use these APIs with care and ensure that the set IDs are
 *    reverted to global process defaults as required.
 *
 */
int glfs_setfsuid (uid_t fsuid) __THROW
        GFAPI_PUBLIC(glfs_setfsuid, 3.4.2);
int glfs_setfsgid (gid_t fsgid) __THROW
        GFAPI_PUBLIC(glfs_setfsgid, 3.4.2);
int glfs_setfsgroups (size_t size, const gid_t *list) __THROW
        GFAPI_PUBLIC(glfs_setfsgroups, 3.4.2);

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

glfs_fd_t *glfs_open (glfs_t *fs, const char *path, int flags) __THROW
        GFAPI_PUBLIC(glfs_open, 3.4.0);


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
		       mode_t mode) __THROW
        GFAPI_PUBLIC(glfs_creat, 3.4.0);

int glfs_close (glfs_fd_t *fd) __THROW
        GFAPI_PUBLIC(glfs_close, 3.4.0);

glfs_t *glfs_from_glfd (glfs_fd_t *fd) __THROW
        GFAPI_PUBLIC(glfs_from_glfd, 3.4.0);

int glfs_set_xlator_option (glfs_t *fs, const char *xlator, const char *key,
			    const char *value) __THROW
        GFAPI_PUBLIC(glfs_set_xlator_options, 3.4.0);

/*

  glfs_io_cbk

  The following is the function type definition of the callback
  function pointer which has to be provided by the caller to the
  *_async() versions of the IO calls.

  The callback function is called on completion of the requested
  IO, and the appropriate return value is returned in @ret.

  In case of an error in completing the IO, @ret will be -1 and
  @errno will be set with the appropriate error.

  @ret will be same as the return value of the non _async() variant
  of the particular call

  @data is the same context pointer provided by the caller at the
  time of issuing the async IO call. This can be used by the
  caller to differentiate different instances of the async requests
  in a common callback function.
*/

typedef void (*glfs_io_cbk) (glfs_fd_t *fd, ssize_t ret, void *data);

// glfs_{read,write}[_async]

ssize_t glfs_read (glfs_fd_t *fd, void *buf,
                   size_t count, int flags) __THROW
        GFAPI_PUBLIC(glfs_read, 3.4.0);
ssize_t glfs_write (glfs_fd_t *fd, const void *buf,
                    size_t count, int flags) __THROW
        GFAPI_PUBLIC(glfs_write, 3.4.0);
int glfs_read_async (glfs_fd_t *fd, void *buf, size_t count, int flags,
		     glfs_io_cbk fn, void *data) __THROW
        GFAPI_PUBLIC(glfs_read_async, 3.4.0);
int glfs_write_async (glfs_fd_t *fd, const void *buf, size_t count, int flags,
		      glfs_io_cbk fn, void *data) __THROW
        GFAPI_PUBLIC(glfs_write_async, 3.4.0);

// glfs_{read,write}v[_async]

ssize_t glfs_readv (glfs_fd_t *fd, const struct iovec *iov, int iovcnt,
		    int flags) __THROW
        GFAPI_PUBLIC(glfs_readv, 3.4.0);
ssize_t glfs_writev (glfs_fd_t *fd, const struct iovec *iov, int iovcnt,
		     int flags) __THROW
        GFAPI_PUBLIC(glfs_writev, 3.4.0);
int glfs_readv_async (glfs_fd_t *fd, const struct iovec *iov, int count,
		      int flags, glfs_io_cbk fn, void *data) __THROW
        GFAPI_PUBLIC(glfs_readv_async, 3.4.0);
int glfs_writev_async (glfs_fd_t *fd, const struct iovec *iov, int count,
		       int flags, glfs_io_cbk fn, void *data) __THROW
        GFAPI_PUBLIC(glfs_writev_async, 3.4.0);

// glfs_p{read,write}[_async]

ssize_t glfs_pread (glfs_fd_t *fd, void *buf, size_t count, off_t offset,
		    int flags) __THROW
        GFAPI_PUBLIC(glfs_pread, 3.4.0);
ssize_t glfs_pwrite (glfs_fd_t *fd, const void *buf, size_t count,
		     off_t offset, int flags) __THROW
        GFAPI_PUBLIC(glfs_pwrite, 3.4.0);
int glfs_pread_async (glfs_fd_t *fd, void *buf, size_t count, off_t offset,
		      int flags, glfs_io_cbk fn, void *data) __THROW
        GFAPI_PUBLIC(glfs_pread_async, 3.4.0);
int glfs_pwrite_async (glfs_fd_t *fd, const void *buf, int count, off_t offset,
		       int flags, glfs_io_cbk fn, void *data) __THROW
        GFAPI_PUBLIC(glfs_pwrite_async, 3.4.0);

// glfs_p{read,write}v[_async]

ssize_t glfs_preadv (glfs_fd_t *fd, const struct iovec *iov, int iovcnt,
		     off_t offset, int flags) __THROW
        GFAPI_PUBLIC(glfs_preadv, 3.4.0);
ssize_t glfs_pwritev (glfs_fd_t *fd, const struct iovec *iov, int iovcnt,
		      off_t offset, int flags) __THROW
        GFAPI_PUBLIC(glfs_pwritev, 3.4.0);
int glfs_preadv_async (glfs_fd_t *fd, const struct iovec *iov,
                       int count, off_t offset, int flags,
                       glfs_io_cbk fn, void *data) __THROW
        GFAPI_PUBLIC(glfs_preadv_async, 3.4.0);
int glfs_pwritev_async (glfs_fd_t *fd, const struct iovec *iov,
                        int count, off_t offset, int flags,
                        glfs_io_cbk fn, void *data) __THROW
        GFAPI_PUBLIC(glfs_pwritev_async, 3.4.0);


off_t glfs_lseek (glfs_fd_t *fd, off_t offset, int whence) __THROW
        GFAPI_PUBLIC(glfs_lseek, 3.4.0);

int glfs_truncate (glfs_t *fs, const char *path, off_t length) __THROW
        GFAPI_PUBLIC(glfs_truncate, 3.7.15);

int glfs_ftruncate (glfs_fd_t *fd, off_t length) __THROW
        GFAPI_PUBLIC(glfs_ftruncate, 3.4.0);
int glfs_ftruncate_async (glfs_fd_t *fd, off_t length, glfs_io_cbk fn,
			  void *data) __THROW
        GFAPI_PUBLIC(glfs_ftruncate_async, 3.4.0);

int glfs_lstat (glfs_t *fs, const char *path, struct stat *buf) __THROW
        GFAPI_PUBLIC(glfs_lstat, 3.4.0);
int glfs_stat (glfs_t *fs, const char *path, struct stat *buf) __THROW
        GFAPI_PUBLIC(glfs_stat, 3.4.0);
int glfs_fstat (glfs_fd_t *fd, struct stat *buf) __THROW
        GFAPI_PUBLIC(glfs_fstat, 3.4.0);

int glfs_fsync (glfs_fd_t *fd) __THROW
        GFAPI_PUBLIC(glfs_fsync, 3.4.0);
int glfs_fsync_async (glfs_fd_t *fd, glfs_io_cbk fn, void *data) __THROW
        GFAPI_PUBLIC(glfs_fsync_async, 3.4.0);

int glfs_fdatasync (glfs_fd_t *fd) __THROW
        GFAPI_PUBLIC(glfs_fdatasync, 3.4.0);
int glfs_fdatasync_async (glfs_fd_t *fd, glfs_io_cbk fn, void *data) __THROW
        GFAPI_PUBLIC(glfs_fdatasync_async, 3.4.0);

int glfs_access (glfs_t *fs, const char *path, int mode) __THROW
        GFAPI_PUBLIC(glfs_access, 3.4.0);

int glfs_symlink (glfs_t *fs, const char *oldpath, const char *newpath) __THROW
        GFAPI_PUBLIC(glfs_symlink, 3.4.0);

int glfs_readlink (glfs_t *fs, const char *path,
                   char *buf, size_t bufsiz) __THROW
        GFAPI_PUBLIC(glfs_readlink, 3.4.0);

int glfs_mknod (glfs_t *fs, const char *path, mode_t mode, dev_t dev) __THROW
        GFAPI_PUBLIC(glfs_mknod, 3.4.0);

int glfs_mkdir (glfs_t *fs, const char *path, mode_t mode) __THROW
        GFAPI_PUBLIC(glfs_mkdir, 3.4.0);

int glfs_unlink (glfs_t *fs, const char *path) __THROW
        GFAPI_PUBLIC(glfs_unlink, 3.4.0);

int glfs_rmdir (glfs_t *fs, const char *path) __THROW
        GFAPI_PUBLIC(glfs_rmdir, 3.4.0);

int glfs_rename (glfs_t *fs, const char *oldpath, const char *newpath) __THROW
        GFAPI_PUBLIC(glfs_rename, 3.4.0);

int glfs_link (glfs_t *fs, const char *oldpath, const char *newpath) __THROW
        GFAPI_PUBLIC(glfs_link, 3.4.0);

glfs_fd_t *glfs_opendir (glfs_t *fs, const char *path) __THROW
        GFAPI_PUBLIC(glfs_opendir, 3.4.0);

/*
 * @glfs_readdir_r and @glfs_readdirplus_r ARE thread safe AND re-entrant,
 * but the interface has ambiguity about the size of @dirent to be allocated
 * before calling the APIs. 512 byte buffer (for @dirent) is sufficient for
 * all known systems which are tested againt glusterfs/gfapi, but may be
 * insufficient in the future.
 */

int glfs_readdir_r (glfs_fd_t *fd, struct dirent *dirent,
		    struct dirent **result) __THROW
        GFAPI_PUBLIC(glfs_readdir_r, 3.4.0);

int glfs_readdirplus_r (glfs_fd_t *fd, struct stat *stat, struct dirent *dirent,
			struct dirent **result) __THROW
        GFAPI_PUBLIC(glfs_readdirplus_r, 3.4.0);

/*
 * @glfs_readdir and @glfs_readdirplus are NEITHER thread safe NOR re-entrant
 * when called on the same directory handle. However they ARE thread safe
 * AND re-entrant when called on different directory handles (which may be
 * referring to the same directory too.)
 */

struct dirent *glfs_readdir (glfs_fd_t *fd) __THROW
        GFAPI_PUBLIC(glfs_readdir, 3.5.0);

struct dirent *glfs_readdirplus (glfs_fd_t *fd, struct stat *stat) __THROW
        GFAPI_PUBLIC(glfs_readdirplus, 3.5.0);

long glfs_telldir (glfs_fd_t *fd) __THROW
        GFAPI_PUBLIC(glfs_telldir, 3.4.0);

void glfs_seekdir (glfs_fd_t *fd, long offset) __THROW
        GFAPI_PUBLIC(glfs_seekdir, 3.4.0);

int glfs_closedir (glfs_fd_t *fd) __THROW
        GFAPI_PUBLIC(glfs_closedir, 3.4.0);

int glfs_statvfs (glfs_t *fs, const char *path, struct statvfs *buf) __THROW
        GFAPI_PUBLIC(glfs_statvfs, 3.4.0);

int glfs_chmod (glfs_t *fs, const char *path, mode_t mode) __THROW
        GFAPI_PUBLIC(glfs_chmod, 3.4.0);

int glfs_fchmod (glfs_fd_t *fd, mode_t mode) __THROW
        GFAPI_PUBLIC(glfs_fchmod, 3.4.0);

int glfs_chown (glfs_t *fs, const char *path, uid_t uid, gid_t gid) __THROW
        GFAPI_PUBLIC(glfs_chown, 3.4.0);

int glfs_lchown (glfs_t *fs, const char *path, uid_t uid, gid_t gid) __THROW
        GFAPI_PUBLIC(glfs_lchown, 3.4.0);

int glfs_fchown (glfs_fd_t *fd, uid_t uid, gid_t gid) __THROW
        GFAPI_PUBLIC(glfs_fchown, 3.4.0);

int glfs_utimens (glfs_t *fs, const char *path,
                  const struct timespec times[2]) __THROW
        GFAPI_PUBLIC(glfs_utimens, 3.4.0);

int glfs_lutimens (glfs_t *fs, const char *path,
                   const struct timespec times[2]) __THROW
        GFAPI_PUBLIC(glfs_lutimens, 3.4.0);

int glfs_futimens (glfs_fd_t *fd, const struct timespec times[2]) __THROW
        GFAPI_PUBLIC(glfs_futimens, 3.4.0);

ssize_t glfs_getxattr (glfs_t *fs, const char *path, const char *name,
		       void *value, size_t size) __THROW
        GFAPI_PUBLIC(glfs_getxattr, 3.4.0);

ssize_t glfs_lgetxattr (glfs_t *fs, const char *path, const char *name,
			void *value, size_t size) __THROW
        GFAPI_PUBLIC(glfs_lgetxattr, 3.4.0);

ssize_t glfs_fgetxattr (glfs_fd_t *fd, const char *name,
			void *value, size_t size) __THROW
        GFAPI_PUBLIC(glfs_fgetxattr, 3.4.0);

ssize_t glfs_listxattr (glfs_t *fs, const char *path,
                        void *value, size_t size) __THROW
        GFAPI_PUBLIC(glfs_listxattr, 3.4.0);

ssize_t glfs_llistxattr (glfs_t *fs, const char *path, void *value,
			 size_t size) __THROW
        GFAPI_PUBLIC(glfs_llistxattr, 3.4.0);

ssize_t glfs_flistxattr (glfs_fd_t *fd, void *value, size_t size) __THROW
        GFAPI_PUBLIC(glfs_flistxattr, 3.4.0);

int glfs_setxattr (glfs_t *fs, const char *path, const char *name,
		   const void *value, size_t size, int flags) __THROW
        GFAPI_PUBLIC(glfs_setxattr, 3.4.0);

int glfs_lsetxattr (glfs_t *fs, const char *path, const char *name,
		    const void *value, size_t size, int flags) __THROW
        GFAPI_PUBLIC(glfs_lsetxattr, 3.4.0);

int glfs_fsetxattr (glfs_fd_t *fd, const char *name,
		    const void *value, size_t size, int flags) __THROW
        GFAPI_PUBLIC(glfs_fsetxattr, 3.4.0);

int glfs_removexattr (glfs_t *fs, const char *path, const char *name) __THROW
        GFAPI_PUBLIC(glfs_removexattr, 3.4.0);

int glfs_lremovexattr (glfs_t *fs, const char *path, const char *name) __THROW
        GFAPI_PUBLIC(glfs_lremovexattr, 3.4.0);

int glfs_fremovexattr (glfs_fd_t *fd, const char *name) __THROW
        GFAPI_PUBLIC(glfs_fremovexattr, 3.4.0);

int glfs_fallocate(glfs_fd_t *fd, int keep_size,
                   off_t offset, size_t len) __THROW
        GFAPI_PUBLIC(glfs_fallocate, 3.5.0);

int glfs_discard(glfs_fd_t *fd, off_t offset, size_t len) __THROW
        GFAPI_PUBLIC(glfs_discard, 3.5.0);


int glfs_discard_async (glfs_fd_t *fd, off_t length, size_t lent,
			glfs_io_cbk fn, void *data) __THROW
        GFAPI_PUBLIC(glfs_discard_async, 3.5.0);

int glfs_zerofill(glfs_fd_t *fd, off_t offset, off_t len) __THROW
        GFAPI_PUBLIC(glfs_zerofill, 3.5.0);

int glfs_zerofill_async (glfs_fd_t *fd, off_t length, off_t len,
                        glfs_io_cbk fn, void *data) __THROW
        GFAPI_PUBLIC(glfs_zerofill_async, 3.5.0);

char *glfs_getcwd (glfs_t *fs, char *buf, size_t size) __THROW
        GFAPI_PUBLIC(glfs_getcwd, 3.4.0);

int glfs_chdir (glfs_t *fs, const char *path) __THROW
        GFAPI_PUBLIC(glfs_chdir, 3.4.0);

int glfs_fchdir (glfs_fd_t *fd) __THROW
        GFAPI_PUBLIC(glfs_fchdir, 3.4.0);

char *glfs_realpath (glfs_t *fs, const char *path, char *resolved_path) __THROW
        GFAPI_PUBLIC(glfs_realpath, 3.4.0);

/*
 * @cmd and @flock are as specified in man fcntl(2).
 */
int glfs_posix_lock (glfs_fd_t *fd, int cmd, struct flock *flock) __THROW
        GFAPI_PUBLIC(glfs_posix_lock, 3.4.0);

glfs_fd_t *glfs_dup (glfs_fd_t *fd) __THROW
        GFAPI_PUBLIC(glfs_dup, 3.4.0);

void glfs_free (void *ptr) __THROW
        GFAPI_PUBLIC(glfs_free, 3.7.16);

/*
 * No xdata support for now.  Nobody needs this call at all yet except for the
 * test script, and that doesn't need xdata.  Adding dict_t support and a new
 * header-file requirement doesn't seem worth it until the need is greater.
 */
int glfs_ipc (glfs_fd_t *fd, int cmd) __THROW
        GFAPI_PUBLIC(glfs_ipc, 3.7.0);

__END_DECLS

#endif /* !_GLFS_H */
