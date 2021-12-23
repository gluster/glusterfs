/*
  Copyright (c) 2012-2018 Red Hat, Inc. <http://www.redhat.com>
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

/* Values for valid flags to be used when using XXXsetattr, to set multiple
 attribute values passed via the related stat structure.
 */

#define GFAPI_SET_ATTR_MODE 0x1
#define GFAPI_SET_ATTR_UID 0x2
#define GFAPI_SET_ATTR_GID 0x4
#define GFAPI_SET_ATTR_SIZE 0x8
#define GFAPI_SET_ATTR_ATIME 0x10
#define GFAPI_SET_ATTR_MTIME 0x20

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
#include <stdint.h>
#include <sys/time.h>

/*
 * For off64_t to be defined, we need both
 * __USE_LARGEFILE64 to be true and __off64_t_defnined to be
 * false. But, making __USE_LARGEFILE64 true causes other issues
 * such as redinition of stat and fstat to stat64 and fstat64
 * respectively which again causes compilation issues.
 * Without off64_t being defined, this will not compile as
 * copy_file_range uses off64_t. Hence define it here. First
 * check whether __off64_t_defined is true or not. <unistd.h>
 * sets that flag when it defines off64_t. If __off64_t_defined
 * is false and __USE_FILE_OFFSET64 is true, then go on to define
 * off64_t using __off64_t.
 */
#ifndef GF_BSD_HOST_OS
#if defined(__USE_FILE_OFFSET64) && !defined(__off64_t_defined)
typedef __off64_t off64_t;
#endif /* defined(__USE_FILE_OFFSET64) && !defined(__off64_t_defined) */
#else
#include <stdio.h>
#ifndef _OFF64_T_DECLARED
/*
 * Including <stdio.h> (done above) should actually define
 * _OFF64_T_DECLARED with off64_t data type being available
 * for consumption. But, off64_t data type is not recognizable
 * for FreeBSD versions less than 11. Hence, int64_t is typedefed
 * to off64_t.
 */
#define _OFF64_T_DECLARED
typedef int64_t off64_t;
#endif /* _OFF64_T_DECLARED */
#endif /* GF_BSD_HOST_OS */

#if defined(HAVE_SYS_ACL_H) || (defined(USE_POSIX_ACLS) && USE_POSIX_ACLS)
#include <sys/acl.h>
#else
typedef void *acl_t;
typedef int acl_type_t;
#endif

/* Portability non glibc c++ build systems */
#ifndef __THROW
#if defined __cplusplus
#define __THROW throw()
#else
#define __THROW
#endif
#endif

#ifndef GF_DARWIN_HOST_OS
#define GFAPI_PUBLIC(sym, ver)  /**/
#define GFAPI_PRIVATE(sym, ver) /**/
#else
#define GFAPI_PUBLIC(sym, ver) __asm("_" __STRING(sym) "$GFAPI_" __STRING(ver))
#define GFAPI_PRIVATE(sym, ver)                                                \
    __asm("_" __STRING(sym) "$GFAPI_PRIVATE_" __STRING(ver))
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

glfs_t *
glfs_new(const char *volname) __THROW GFAPI_PUBLIC(glfs_new, 3.4.0);

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

int
glfs_set_volfile(glfs_t *fs, const char *volfile) __THROW
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
              management daemon. Specifying NULL will result in the
              usage of the default (tcp) transport type. Permitted
              values are "tcp" or "unix".

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

int
glfs_set_volfile_server(glfs_t *fs, const char *transport, const char *host,
                        int port) __THROW
    GFAPI_PUBLIC(glfs_set_volfile_server, 3.4.0);

int
glfs_unset_volfile_server(glfs_t *fs, const char *transport, const char *host,
                          int port) __THROW
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

int
glfs_set_logging(glfs_t *fs, const char *logfile, int loglevel) __THROW
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

int
glfs_init(glfs_t *fs) __THROW GFAPI_PUBLIC(glfs_init, 3.4.0);

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

int
glfs_fini(glfs_t *fs) __THROW GFAPI_PUBLIC(glfs_fini, 3.4.0);

/*
  SYNOPSIS

      glfs_get_volfile: Get the volfile associated with a 'virtual mount'

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

ssize_t
glfs_get_volfile(glfs_t *fs, void *buf, size_t len) __THROW
    GFAPI_PUBLIC(glfs_get_volfile, 3.6.0);

/*
  SYNOPSIS

       glfs_get_volumeid: Copy the Volume UUID stored in the glfs object fs.

  DESCRIPTION

       This function when invoked for the first time sends RPC call to the
       the management server (glusterd) to fetch volume uuid and stores it
       in the glusterfs_context linked to the glfs object fs which can be used
       in the subsequent calls. Later it parses that UUID to convert it from
       canonical string format into an opaque byte array and copy it into
       the volid array. In case if either of the input parameters, volid or
  size, is NULL, number of bytes required to copy the volume UUID is returned.

  PARAMETERS

       @fs: The 'virtual mount' object to be used to retrieve and store
            volume's UUID.
       @volid: Pointer to a place for the volume UUID to be stored
       @size: Length of @volid

  RETURN VALUES

       -1 : Failure. @errno will be set with the type of failure.
        Others : length of the volume UUID stored.
*/

int
glfs_get_volumeid(glfs_t *fs, char *volid, size_t size) __THROW
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
 * Mask for request/result items in the struct glfs_stat.
 *
 * Query request/result mask for glfs_stat() (family of functions) and
 * struct glfs_stat::glfs_st_mask.
 *
 * These bits should be set in the mask argument of glfs_stat() (family of
 * functions) to request particular items when calling glfs_stat().
 *
 * NOTE: Lower order 32 bits are used to reflect statx(2) bits. For Gluster
 * specific attrs/extensions, use higher order 32 bits.
 *
 */
#define GLFS_STAT_TYPE 0x0000000000000001U   /* Want/got stx_mode & S_IFMT */
#define GLFS_STAT_MODE 0x0000000000000002U   /* Want/got stx_mode & ~S_IFMT */
#define GLFS_STAT_NLINK 0x0000000000000004U  /* Want/got stx_nlink */
#define GLFS_STAT_UID 0x0000000000000008U    /* Want/got stx_uid */
#define GLFS_STAT_GID 0x0000000000000010U    /* Want/got stx_gid */
#define GLFS_STAT_ATIME 0x0000000000000020U  /* Want/got stx_atime */
#define GLFS_STAT_MTIME 0x0000000000000040U  /* Want/got stx_mtime */
#define GLFS_STAT_CTIME 0x0000000000000080U  /* Want/got stx_ctime */
#define GLFS_STAT_INO 0x0000000000000100U    /* Want/got stx_ino */
#define GLFS_STAT_SIZE 0x0000000000000200U   /* Want/got stx_size */
#define GLFS_STAT_BLOCKS 0x0000000000000400U /* Want/got stx_blocks */
#define GLFS_STAT_BASIC_STATS                                                  \
    0x00000000000007ffU /* Items in the normal stat struct */
#define GLFS_STAT_BTIME 0x0000000000000800U /* Want/got stx_btime */
#define GLFS_STAT_ALL 0x0000000000000fffU   /* All currently supported flags */
#define GLFS_STAT_RESERVED                                                     \
    0x8000000000000000U /* Reserved to denote future expansion */

/* Macros for checking validity of struct glfs_stat members.*/
#define GLFS_STAT_TYPE_VALID(stmask) (stmask & GLFS_STAT_TYPE)
#define GLFS_STAT_MODE_VALID(stmask) (stmask & GLFS_STAT_MODE)
#define GLFS_STAT_NLINK_VALID(stmask) (stmask & GLFS_STAT_NLINK)
#define GLFS_STAT_UID_VALID(stmask) (stmask & GLFS_STAT_UID)
#define GLFS_STAT_GID_VALID(stmask) (stmask & GLFS_STAT_GID)
#define GLFS_STAT_ATIME_VALID(stmask) (stmask & GLFS_STAT_ATIME)
#define GLFS_STAT_MTIME_VALID(stmask) (stmask & GLFS_STAT_MTIME)
#define GLFS_STAT_CTIME_VALID(stmask) (stmask & GLFS_STAT_CTIME)
#define GLFS_STAT_INO_VALID(stmask) (stmask & GLFS_STAT_INO)
#define GLFS_STAT_SIZE_VALID(stmask) (stmask & GLFS_STAT_SIZE)
#define GLFS_STAT_BLOCKS_VALID(stmask) (stmask & GLFS_STAT_BLOCKS)
#define GLFS_STAT_BTIME_VALID(stmask) (stmask & GLFS_STAT_BTIME)
#define GLFS_STAT_GFID_VALID(stmask) (stmask & GLFS_STAT_GFID)

/*
 * Attributes to be found in glfs_st_attributes and masked in
 * glfs_st_attributes_mask.
 *
 * These give information about the features or the state of a file that might
 * be of use to programs.
 *
 * NOTE: Lower order 32 bits are used to reflect statx(2) attribute bits. For
 * Gluster specific attrs, use higher order 32 bits.
 *
 * NOTE: We do not support any file attributes or state as yet!
 */
#define GLFS_STAT_ATTR_RESERVED                                                \
    0x8000000000000000U /* Reserved to denote future expansion */

/* Extended file attribute structure.
 *
 * The caller passes a mask of what they're specifically interested in as a
 * parameter to glfs_stat().  What glfs_stat() actually got will be indicated
 * in glfs_st_mask upon return.
 *
 * For each bit in the mask argument:
 *
 * - if the datum is not supported:
 *
 *   - the bit will be cleared, and
 *
 *   - the datum value is undefined
 *
 * - otherwise, if explicitly requested:
 *
 *   - the field will be filled in and the bit will be set;
 *
 * - otherwise, if not requested, but available in, it will be filled in
 * anyway, and the bit will be set upon return;
 *
 * - otherwise the field and the bit will be cleared before returning.
 *
 */

struct glfs_stat {
    uint64_t glfs_st_mask;       /* What results were written [uncond] */
    uint64_t glfs_st_attributes; /* Flags conveying information about the file
                                    [uncond] */
    uint64_t glfs_st_attributes_mask; /* Mask to show what's supported in
                                         st_attributes [ucond] */
    struct timespec glfs_st_atime;    /* Last access time */
    struct timespec glfs_st_btime;    /* File creation time */
    struct timespec glfs_st_ctime;    /* Last attribute change time */
    struct timespec glfs_st_mtime;    /* Last data modification time */
    ino_t glfs_st_ino;                /* Inode number */
    off_t glfs_st_size;               /* File size */
    blkcnt_t glfs_st_blocks;          /* Number of 512-byte blocks allocated */
    uint32_t glfs_st_rdev_major; /* Device ID of special file [if bdev/cdev] */
    uint32_t glfs_st_rdev_minor;
    uint32_t glfs_st_dev_major; /* ID of device containing file [uncond] */
    uint32_t glfs_st_dev_minor;
    blksize_t glfs_st_blksize; /* Preferred general I/O size [uncond] */
    nlink_t glfs_st_nlink;     /* Number of hard links */
    uid_t glfs_st_uid;         /* User ID of owner */
    gid_t glfs_st_gid;         /* Group ID of owner */
    mode_t glfs_st_mode;       /* File mode */
};

#define GLFS_LEASE_ID_SIZE 16 /* 128bits */
typedef char glfs_leaseid_t[GLFS_LEASE_ID_SIZE];

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
 *  - In case of leases feature enables, setfsleaseid is used to set and reset
 *    leaseid before and after every I/O operation.
 *  - Once a process for a thread of operation choses to set the IDs, all glfs
 *    calls made from that thread would default to the IDs set for the thread.
 *    As a result use these APIs with care and ensure that the set IDs are
 *    reverted to global process defaults as required.
 *
 */
int
glfs_setfsuid(uid_t fsuid) __THROW GFAPI_PUBLIC(glfs_setfsuid, 3.4.2);

int
glfs_setfsgid(gid_t fsgid) __THROW GFAPI_PUBLIC(glfs_setfsgid, 3.4.2);

int
glfs_setfsgroups(size_t size, const gid_t *list) __THROW
    GFAPI_PUBLIC(glfs_setfsgroups, 3.4.2);

int
glfs_setfsleaseid(glfs_leaseid_t leaseid) __THROW
    GFAPI_PUBLIC(glfs_setfsleaseid, 4.0.0);

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

glfs_fd_t *
glfs_open(glfs_t *fs, const char *path, int flags) __THROW
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

glfs_fd_t *
glfs_creat(glfs_t *fs, const char *path, int flags, mode_t mode) __THROW
    GFAPI_PUBLIC(glfs_creat, 3.4.0);

int
glfs_close(glfs_fd_t *fd) __THROW GFAPI_PUBLIC(glfs_close, 3.4.0);

glfs_t *
glfs_from_glfd(glfs_fd_t *fd) __THROW GFAPI_PUBLIC(glfs_from_glfd, 3.4.0);

int
glfs_set_xlator_option(glfs_t *fs, const char *xlator, const char *key,
                       const char *value) __THROW
    GFAPI_PUBLIC(glfs_set_xlator_option, 3.4.0);

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

  @prestat and @poststat are allocated on the stack, that are auto destroyed
  post the callback function returns.
*/

typedef void (*glfs_io_cbk)(glfs_fd_t *fd, ssize_t ret,
                            struct glfs_stat *prestat,
                            struct glfs_stat *poststat, void *data);

// glfs_{read,write}[_async]

ssize_t
glfs_read(glfs_fd_t *fd, void *buf, size_t count, int flags) __THROW
    GFAPI_PUBLIC(glfs_read, 3.4.0);

ssize_t
glfs_write(glfs_fd_t *fd, const void *buf, size_t count, int flags) __THROW
    GFAPI_PUBLIC(glfs_write, 3.4.0);

int
glfs_read_async(glfs_fd_t *fd, void *buf, size_t count, int flags,
                glfs_io_cbk fn, void *data) __THROW
    GFAPI_PUBLIC(glfs_read_async, 6.0);

int
glfs_write_async(glfs_fd_t *fd, const void *buf, size_t count, int flags,
                 glfs_io_cbk fn, void *data) __THROW
    GFAPI_PUBLIC(glfs_write_async, 6.0);

// glfs_{read,write}v[_async]

ssize_t
glfs_readv(glfs_fd_t *fd, const struct iovec *iov, int iovcnt,
           int flags) __THROW GFAPI_PUBLIC(glfs_readv, 3.4.0);

ssize_t
glfs_writev(glfs_fd_t *fd, const struct iovec *iov, int iovcnt,
            int flags) __THROW GFAPI_PUBLIC(glfs_writev, 3.4.0);

int
glfs_readv_async(glfs_fd_t *fd, const struct iovec *iov, int count, int flags,
                 glfs_io_cbk fn, void *data) __THROW
    GFAPI_PUBLIC(glfs_readv_async, 6.0);

int
glfs_writev_async(glfs_fd_t *fd, const struct iovec *iov, int count, int flags,
                  glfs_io_cbk fn, void *data) __THROW
    GFAPI_PUBLIC(glfs_writev_async, 6.0);

// glfs_p{read,write}[_async]

ssize_t
glfs_pread(glfs_fd_t *fd, void *buf, size_t count, off_t offset, int flags,
           struct glfs_stat *poststat) __THROW GFAPI_PUBLIC(glfs_pread, 6.0);

ssize_t
glfs_pwrite(glfs_fd_t *fd, const void *buf, size_t count, off_t offset,
            int flags, struct glfs_stat *prestat,
            struct glfs_stat *poststat) __THROW GFAPI_PUBLIC(glfs_pwrite, 6.0);

int
glfs_pread_async(glfs_fd_t *fd, void *buf, size_t count, off_t offset,
                 int flags, glfs_io_cbk fn, void *data) __THROW
    GFAPI_PUBLIC(glfs_pread_async, 6.0);

int
glfs_pwrite_async(glfs_fd_t *fd, const void *buf, int count, off_t offset,
                  int flags, glfs_io_cbk fn, void *data) __THROW
    GFAPI_PUBLIC(glfs_pwrite_async, 6.0);

// glfs_p{read,write}v[_async]

ssize_t
glfs_preadv(glfs_fd_t *fd, const struct iovec *iov, int iovcnt, off_t offset,
            int flags) __THROW GFAPI_PUBLIC(glfs_preadv, 3.4.0);

ssize_t
glfs_pwritev(glfs_fd_t *fd, const struct iovec *iov, int iovcnt, off_t offset,
             int flags) __THROW GFAPI_PUBLIC(glfs_pwritev, 3.4.0);

int
glfs_preadv_async(glfs_fd_t *fd, const struct iovec *iov, int count,
                  off_t offset, int flags, glfs_io_cbk fn, void *data) __THROW
    GFAPI_PUBLIC(glfs_preadv_async, 6.0);

int
glfs_pwritev_async(glfs_fd_t *fd, const struct iovec *iov, int count,
                   off_t offset, int flags, glfs_io_cbk fn, void *data) __THROW
    GFAPI_PUBLIC(glfs_pwritev_async, 6.0);

off_t
glfs_lseek(glfs_fd_t *fd, off_t offset, int whence) __THROW
    GFAPI_PUBLIC(glfs_lseek, 3.4.0);

ssize_t
glfs_copy_file_range(struct glfs_fd *glfd_in, off64_t *off_in,
                     struct glfs_fd *glfd_out, off64_t *off_out, size_t len,
                     unsigned int flags, struct glfs_stat *statbuf,
                     struct glfs_stat *prestat,
                     struct glfs_stat *poststat) __THROW
    GFAPI_PUBLIC(glfs_copy_file_range, 6.0);

int
glfs_truncate(glfs_t *fs, const char *path, off_t length) __THROW
    GFAPI_PUBLIC(glfs_truncate, 3.7.15);

int
glfs_ftruncate(glfs_fd_t *fd, off_t length, struct glfs_stat *prestat,
               struct glfs_stat *poststat) __THROW
    GFAPI_PUBLIC(glfs_ftruncate, 6.0);

int
glfs_ftruncate_async(glfs_fd_t *fd, off_t length, glfs_io_cbk fn,
                     void *data) __THROW
    GFAPI_PUBLIC(glfs_ftruncate_async, 6.0);

int
glfs_lstat(glfs_t *fs, const char *path, struct stat *buf) __THROW
    GFAPI_PUBLIC(glfs_lstat, 3.4.0);

int
glfs_stat(glfs_t *fs, const char *path, struct stat *buf) __THROW
    GFAPI_PUBLIC(glfs_stat, 3.4.0);

int
glfs_fstat(glfs_fd_t *fd, struct stat *buf) __THROW
    GFAPI_PUBLIC(glfs_fstat, 3.4.0);

int
glfs_fsync(glfs_fd_t *fd, struct glfs_stat *prestat,
           struct glfs_stat *poststat) __THROW GFAPI_PUBLIC(glfs_fsync, 6.0);

int
glfs_fsync_async(glfs_fd_t *fd, glfs_io_cbk fn, void *data) __THROW
    GFAPI_PUBLIC(glfs_fsync_async, 6.0);

int
glfs_fdatasync(glfs_fd_t *fd, struct glfs_stat *prestat,
               struct glfs_stat *poststat) __THROW
    GFAPI_PUBLIC(glfs_fdatasync, 6.0);

int
glfs_fdatasync_async(glfs_fd_t *fd, glfs_io_cbk fn, void *data) __THROW
    GFAPI_PUBLIC(glfs_fdatasync_async, 6.0);

int
glfs_access(glfs_t *fs, const char *path, int mode) __THROW
    GFAPI_PUBLIC(glfs_access, 3.4.0);

int
glfs_symlink(glfs_t *fs, const char *oldpath, const char *newpath) __THROW
    GFAPI_PUBLIC(glfs_symlink, 3.4.0);

int
glfs_readlink(glfs_t *fs, const char *path, char *buf, size_t bufsiz) __THROW
    GFAPI_PUBLIC(glfs_readlink, 3.4.0);

int
glfs_mknod(glfs_t *fs, const char *path, mode_t mode, dev_t dev) __THROW
    GFAPI_PUBLIC(glfs_mknod, 3.4.0);

int
glfs_mkdir(glfs_t *fs, const char *path, mode_t mode) __THROW
    GFAPI_PUBLIC(glfs_mkdir, 3.4.0);

int
glfs_unlink(glfs_t *fs, const char *path) __THROW
    GFAPI_PUBLIC(glfs_unlink, 3.4.0);

int
glfs_rmdir(glfs_t *fs, const char *path) __THROW
    GFAPI_PUBLIC(glfs_rmdir, 3.4.0);

int
glfs_rename(glfs_t *fs, const char *oldpath, const char *newpath) __THROW
    GFAPI_PUBLIC(glfs_rename, 3.4.0);

int
glfs_link(glfs_t *fs, const char *oldpath, const char *newpath) __THROW
    GFAPI_PUBLIC(glfs_link, 3.4.0);

glfs_fd_t *
glfs_opendir(glfs_t *fs, const char *path) __THROW
    GFAPI_PUBLIC(glfs_opendir, 3.4.0);

/*
 * @glfs_readdir_r and @glfs_readdirplus_r ARE thread safe AND re-entrant,
 * but the interface has ambiguity about the size of @dirent to be allocated
 * before calling the APIs. 512 byte buffer (for @dirent) is sufficient for
 * all known systems which are tested againt glusterfs/gfapi, but may be
 * insufficient in the future.
 */

int
glfs_readdir_r(glfs_fd_t *fd, struct dirent *dirent,
               struct dirent **result) __THROW
    GFAPI_PUBLIC(glfs_readdir_r, 3.4.0);

int
glfs_readdirplus_r(glfs_fd_t *fd, struct stat *stat, struct dirent *dirent,
                   struct dirent **result) __THROW
    GFAPI_PUBLIC(glfs_readdirplus_r, 3.4.0);

/*
 * @glfs_readdir and @glfs_readdirplus are NEITHER thread safe NOR re-entrant
 * when called on the same directory handle. However they ARE thread safe
 * AND re-entrant when called on different directory handles (which may be
 * referring to the same directory too.)
 */

struct dirent *
glfs_readdir(glfs_fd_t *fd) __THROW GFAPI_PUBLIC(glfs_readdir, 3.5.0);

struct dirent *
glfs_readdirplus(glfs_fd_t *fd, struct stat *stat) __THROW
    GFAPI_PUBLIC(glfs_readdirplus, 3.5.0);

long
glfs_telldir(glfs_fd_t *fd) __THROW GFAPI_PUBLIC(glfs_telldir, 3.4.0);

void
glfs_seekdir(glfs_fd_t *fd, long offset) __THROW
    GFAPI_PUBLIC(glfs_seekdir, 3.4.0);

int
glfs_closedir(glfs_fd_t *fd) __THROW GFAPI_PUBLIC(glfs_closedir, 3.4.0);

int
glfs_statvfs(glfs_t *fs, const char *path, struct statvfs *buf) __THROW
    GFAPI_PUBLIC(glfs_statvfs, 3.4.0);

int
glfs_chmod(glfs_t *fs, const char *path, mode_t mode) __THROW
    GFAPI_PUBLIC(glfs_chmod, 3.4.0);

int
glfs_fchmod(glfs_fd_t *fd, mode_t mode) __THROW
    GFAPI_PUBLIC(glfs_fchmod, 3.4.0);

int
glfs_chown(glfs_t *fs, const char *path, uid_t uid, gid_t gid) __THROW
    GFAPI_PUBLIC(glfs_chown, 3.4.0);

int
glfs_lchown(glfs_t *fs, const char *path, uid_t uid, gid_t gid) __THROW
    GFAPI_PUBLIC(glfs_lchown, 3.4.0);

int
glfs_fchown(glfs_fd_t *fd, uid_t uid, gid_t gid) __THROW
    GFAPI_PUBLIC(glfs_fchown, 3.4.0);

int
glfs_utimens(glfs_t *fs, const char *path,
             const struct timespec times[2]) __THROW
    GFAPI_PUBLIC(glfs_utimens, 3.4.0);

int
glfs_lutimens(glfs_t *fs, const char *path,
              const struct timespec times[2]) __THROW
    GFAPI_PUBLIC(glfs_lutimens, 3.4.0);

int
glfs_futimens(glfs_fd_t *fd, const struct timespec times[2]) __THROW
    GFAPI_PUBLIC(glfs_futimens, 3.4.0);

ssize_t
glfs_getxattr(glfs_t *fs, const char *path, const char *name, void *value,
              size_t size) __THROW GFAPI_PUBLIC(glfs_getxattr, 3.4.0);

ssize_t
glfs_lgetxattr(glfs_t *fs, const char *path, const char *name, void *value,
               size_t size) __THROW GFAPI_PUBLIC(glfs_lgetxattr, 3.4.0);

ssize_t
glfs_fgetxattr(glfs_fd_t *fd, const char *name, void *value,
               size_t size) __THROW GFAPI_PUBLIC(glfs_fgetxattr, 3.4.0);

ssize_t
glfs_listxattr(glfs_t *fs, const char *path, void *value, size_t size) __THROW
    GFAPI_PUBLIC(glfs_listxattr, 3.4.0);

ssize_t
glfs_llistxattr(glfs_t *fs, const char *path, void *value, size_t size) __THROW
    GFAPI_PUBLIC(glfs_llistxattr, 3.4.0);

ssize_t
glfs_flistxattr(glfs_fd_t *fd, void *value, size_t size) __THROW
    GFAPI_PUBLIC(glfs_flistxattr, 3.4.0);

int
glfs_setxattr(glfs_t *fs, const char *path, const char *name, const void *value,
              size_t size, int flags) __THROW
    GFAPI_PUBLIC(glfs_setxattr, 3.4.0);

int
glfs_lsetxattr(glfs_t *fs, const char *path, const char *name,
               const void *value, size_t size, int flags) __THROW
    GFAPI_PUBLIC(glfs_lsetxattr, 3.4.0);

int
glfs_fsetxattr(glfs_fd_t *fd, const char *name, const void *value, size_t size,
               int flags) __THROW GFAPI_PUBLIC(glfs_fsetxattr, 3.4.0);

int
glfs_removexattr(glfs_t *fs, const char *path, const char *name) __THROW
    GFAPI_PUBLIC(glfs_removexattr, 3.4.0);

int
glfs_lremovexattr(glfs_t *fs, const char *path, const char *name) __THROW
    GFAPI_PUBLIC(glfs_lremovexattr, 3.4.0);

int
glfs_fremovexattr(glfs_fd_t *fd, const char *name) __THROW
    GFAPI_PUBLIC(glfs_fremovexattr, 3.4.0);

int
glfs_fallocate(glfs_fd_t *fd, int keep_size, off_t offset, size_t len) __THROW
    GFAPI_PUBLIC(glfs_fallocate, 3.5.0);

int
glfs_discard(glfs_fd_t *fd, off_t offset, size_t len) __THROW
    GFAPI_PUBLIC(glfs_discard, 3.5.0);

int
glfs_discard_async(glfs_fd_t *fd, off_t length, size_t lent, glfs_io_cbk fn,
                   void *data) __THROW GFAPI_PUBLIC(glfs_discard_async, 6.0);

int
glfs_zerofill(glfs_fd_t *fd, off_t offset, off_t len) __THROW
    GFAPI_PUBLIC(glfs_zerofill, 3.5.0);

int
glfs_zerofill_async(glfs_fd_t *fd, off_t length, off_t len, glfs_io_cbk fn,
                    void *data) __THROW GFAPI_PUBLIC(glfs_zerofill_async, 6.0);

char *
glfs_getcwd(glfs_t *fs, char *buf, size_t size) __THROW
    GFAPI_PUBLIC(glfs_getcwd, 3.4.0);

int
glfs_chdir(glfs_t *fs, const char *path) __THROW
    GFAPI_PUBLIC(glfs_chdir, 3.4.0);

int
glfs_fchdir(glfs_fd_t *fd) __THROW GFAPI_PUBLIC(glfs_fchdir, 3.4.0);

char *
glfs_realpath(glfs_t *fs, const char *path, char *resolved_path) __THROW
    GFAPI_PUBLIC(glfs_realpath, 3.7.17);

/*
 * @cmd and @flock are as specified in man fcntl(2).
 */
int
glfs_posix_lock(glfs_fd_t *fd, int cmd, struct flock *flock) __THROW
    GFAPI_PUBLIC(glfs_posix_lock, 3.4.0);

/*
  SYNOPSIS

  glfs_file_lock: Request extended byte range lock on a file

  DESCRIPTION

  This function is capable of requesting either advisory or mandatory type
  byte range locks on a file.

  Note: To set a unique owner key for locks based on a particular file
  descriptor, make use of glfs_fd_set_lkowner() api to do so before
  requesting lock via this api. This owner key will be further consumed
  by other incoming data modifying file operations via the same file
  descriptor.

  PARAMETERS

  @fd: File descriptor

  @cmd: As specified in man fcntl(2).

  @flock: As specified in man fcntl(2).

  @lk_mode: Required lock type from options available with the
            enum glfs_lock_mode_t defined below.

  RETURN VALUES

  0   : Success. Lock has been granted.
  -1  : Failure. @errno will be set indicating the type of failure.

 */

/* Lock modes used by glfs_file_lock() */
enum glfs_lock_mode { GLFS_LK_ADVISORY = 0, GLFS_LK_MANDATORY };
typedef enum glfs_lock_mode glfs_lock_mode_t;

int
glfs_file_lock(glfs_fd_t *fd, int cmd, struct flock *flock,
               glfs_lock_mode_t lk_mode) __THROW
    GFAPI_PUBLIC(glfs_file_lock, 3.13.0);

glfs_fd_t *
glfs_dup(glfs_fd_t *fd) __THROW GFAPI_PUBLIC(glfs_dup, 3.4.0);

void
glfs_free(void *ptr) __THROW GFAPI_PUBLIC(glfs_free, 3.7.16);

/*
 * glfs_sysrq: send a system request to the @fs instance
 *
 * Different commands for @sysrq are possible, the defines for these are listed
 * below the function definition.
 *
 * This function always returns success if the @sysrq is recognized. The return
 * value does not way anythin about the result of the @sysrq execution. Not all
 * @sysrq command will be able to return a success/failure status.
 */
int
glfs_sysrq(glfs_t *fs, char sysrq) __THROW GFAPI_PUBLIC(glfs_sysrq, 3.10.0);

#define GLFS_SYSRQ_HELP 'h' /* log a message with supported sysrq commands */
#define GLFS_SYSRQ_STATEDUMP 's' /* create a statedump */

/*
 * Structure returned as part of xreaddirplus
 */
struct glfs_xreaddirp_stat;
typedef struct glfs_xreaddirp_stat glfs_xreaddirp_stat_t;

/* Request flags to be used in XREADDIRP operation */
#define GFAPI_XREADDIRP_NULL                                                   \
    0x00000000 /* by default, no stat will be fetched */
#define GFAPI_XREADDIRP_STAT 0x00000001   /* Get stat */
#define GFAPI_XREADDIRP_HANDLE 0x00000002 /* Get object handle */

/*
 * This stat structure returned gets freed as part of glfs_free(xstat)
 */
struct stat *
glfs_xreaddirplus_get_stat(glfs_xreaddirp_stat_t *xstat) __THROW
    GFAPI_PUBLIC(glfs_xreaddirplus_get_stat, 3.11.0);

/*
 * SYNOPSIS
 *
 * glfs_xreaddirplus_r: Extended Readirplus operation
 *
 * DESCRIPTION
 *
 * This API does readdirplus operation, but along with stat it can fetch other
 * extra information like object handles etc for each of the dirents returned
 * based on requested flags. On success it returns the set of flags successfully
 * processed.
 *
 * Note that there are chances that some of the requested information may not be
 * available or returned (for example if reached EOD). Ensure to validate the
 * returned value to determine what flags have been successfully processed
 * & set.
 *
 * PARAMETERS
 *
 * INPUT:
 * @glfd: GFAPI file descriptor of the directory
 * @flags: Flags determining xreaddirp_stat requested
 *         Current available values are:
 *              GFAPI_XREADDIRP_NULL
 *              GFAPI_XREADDIRP_STAT
 *              GFAPI_XREADDIRP_HANDLE
 * @ext: Dirent struture to copy the values to
 *       (though optional recommended to be allocated by application
 *        esp., in multi-threaded environment)
 *
 * OUTPUT:
 * @res: to store the next dirent value. If NULL and return value is '0',
 *       it means it reached end of the directory.
 * @xstat_p: Pointer to contain all the requested data returned
 *           for that dirent. Application should make use of glfs_free() API
 *           to free this pointer and the variables returned by
 *           glfs_xreaddirplus_get_*() APIs.
 *
 * RETURN VALUE:
 * >=0: SUCCESS (value contains the flags successfully processed)
 *  -1: FAILURE
 */
int
glfs_xreaddirplus_r(glfs_fd_t *glfd, uint32_t flags,
                    glfs_xreaddirp_stat_t **xstat_p, struct dirent *ext,
                    struct dirent **res) __THROW
    GFAPI_PUBLIC(glfs_xreaddirplus_r, 3.11.0);

#define GFAPI_MAX_LOCK_OWNER_LEN 255

/*
 *
 * DESCRIPTION
 *
 * This API allows application to set lk_owner on a fd.
 * A glfd can be associated with only single lk_owner. In case if there
 * is need to set another lk_owner, applications can make use of
 * 'glfs_dup' to get duplicate glfd and set new lk_owner on that second
 * glfd.
 *
 * Also its not recommended to override or clear lk_owner value as the
 * same shall be used to flush any outstanding locks while closing the fd.
 *
 * PARAMETERS
 *
 * INPUT:
 * @glfd: GFAPI file descriptor
 * @len: Size of lk_owner buffer. Max value can be GFAPI_MAX_LOCK_OWNER_LEN
 * @data: lk_owner data buffer.
 *
 * OUTPUT:
 * 0: SUCCESS
 * -1: FAILURE
 */
int
glfs_fd_set_lkowner(glfs_fd_t *glfd, void *data, int len) __THROW
    GFAPI_PUBLIC(glfs_fd_set_lkowner, 3.10.7);

/*
 * Applications (currently NFS-Ganesha) can make use of this
 * structure to read upcall notifications sent by server either
 * by polling or registering a callback function.
 *
 * On success, applications need to check for 'reason' to decide
 * if any upcall event is received.
 *
 * Currently supported upcall_events -
 *      GLFS_UPCALL_INODE_INVALIDATE -
 *              'event_arg' - glfs_upcall_inode
 *
 * After processing the event, applications need to free 'event_arg' with
 * glfs_free().
 *
 * Also similar to I/Os, the application should ideally stop polling
 * or unregister upcall_cbk function before calling glfs_fini(..).
 * Hence making an assumption that 'fs' & ctx structures cannot be
 * freed while in this routine.
 */
struct glfs_upcall;
typedef struct glfs_upcall glfs_upcall_t;

glfs_t *
glfs_upcall_get_fs(glfs_upcall_t *arg) __THROW
    GFAPI_PUBLIC(glfs_upcall_get_fs, 3.7.16);

enum glfs_upcall_reason {
    GLFS_UPCALL_EVENT_NULL = 0,
    GLFS_UPCALL_INODE_INVALIDATE, /* invalidate cache entry */
    GLFS_UPCALL_RECALL_LEASE,     /* recall lease */
};
typedef enum glfs_upcall_reason glfs_upcall_reason_t;

glfs_upcall_reason_t
glfs_upcall_get_reason(glfs_upcall_t *arg) __THROW
    GFAPI_PUBLIC(glfs_upcall_get_reason, 3.7.16);

/*
 * Applications first need to make use of above API i.e,
 * "glfs_upcall_get_reason" to determine which upcall event it has
 * received. Post that below API - "glfs_upcall_get_event" should
 * be used to get corresponding upcall event object.
 *
 * Below are the upcall_reason and corresponding upcall_event objects:
 *      ==========================================================
 *      glfs_upcall_reason           -    event_object
 *      ==========================================================
 *      GLFS_UPCALL_EVENT_NULL       -    NULL
 *      GLFS_UPCALL_INODE_INVALIDATE -    struct glfs_upcall_inode
 *      GLFS_UPCALL_RECALL_LEASE     -    struct glfs_upcall_lease
 *
 * After processing upcall event, glfs_free() should be called on the
 * glfs_upcall.
 */
void *
glfs_upcall_get_event(glfs_upcall_t *arg) __THROW
    GFAPI_PUBLIC(glfs_upcall_get_event, 3.7.16);

/*
 * SYNOPSIS
 *
 * glfs_upcall_cbk: Upcall callback definition
 *
 * This is function type definition of the callback function pointer
 * which has to be provided by the caller while registering for any
 * upcall events.
 *
 * This function is called whenever any upcall which the application
 * has registered for is received from the server.
 *
 * @up_arg: Upcall structure whose contents need to be interpreted by
 * making use of glfs_upcall_* helper routines.
 *
 * @data: The same context pointer provided by the caller at the time of
 * registering of upcall events. This may be used by the caller for any
 * of its internal use while processing upcalls.
 */
typedef void (*glfs_upcall_cbk)(glfs_upcall_t *up_arg, void *data);

/*
 * List of upcall events supported by gluster/gfapi
 */
#define GLFS_EVENT_INODE_INVALIDATE 0x00000001 /* invalidate cache entry */
#define GLFS_EVENT_RECALL_LEASE 0x00000002     /* Recall lease */
#define GLFS_EVENT_ANY 0xffffffff              /* for all the above events */

/*
 * SYNOPSIS
 *
 * glfs_upcall_register: Register for upcall events
 *
 * DESCRIPTION
 *
 * This function is used to register for various upcall events application
 * is interested in and the callback function to be invoked when such
 * events are triggered.
 *
 * Multiple calls of this routine shall override cbk function. That means
 * only one cbk function can be used for all the upcall events registered
 * and that shall be the one last updated.
 *
 * PARAMETERS:
 *
 * INPUT:
 * @fs: The 'virtual mount' object
 *
 * @event_list: List of upcall events to be registered.
 *              Current available values are:
 *               - GLFS_EVENT_INODE_INVALIDATE
 *               - GLFS_EVENT_RECALL_LEASE
 *
 * @cbk: The cbk routine to be invoked in case of any upcall received
 * @data: Any opaque pointer provided by caller which shall be using while
 * making cbk calls. This pointer may be used by caller for any of its
 * internal use while processing upcalls. Can be NULL.
 *
 * RETURN VALUE:
 * >0: SUCCESS (value contains the events successfully registered)
 * -1: FAILURE
 */
int
glfs_upcall_register(glfs_t *fs, uint32_t event_list, glfs_upcall_cbk cbk,
                     void *data) __THROW
    GFAPI_PUBLIC(glfs_upcall_register, 3.13.0);

/*
 * SYNOPSIS
 *
 * glfs_upcall_unregister: Unregister for upcall events
 *
 * DESCRIPTION
 *
 * This function is used to unregister the upcall events application
 * is not interested in. In case if the caller unregisters all the events
 * it has registered for, it shall no more receive any upcall event.
 *
 * PARAMETERS:
 *
 * INPUT:
 * @fs: The 'virtual mount' object
 *
 * @event_list: List of upcall events to be unregistered.
 *              Current available values are:
 *               - GLFS_EVENT_INODE_INVALIDATE
 *               - GLFS_EVENT_RECALL_LEASE
 * RETURN VALUE:
 * >0: SUCCESS (value contains the events successfully unregistered)
 * -1: FAILURE
 */
int
glfs_upcall_unregister(glfs_t *fs, uint32_t event_list) __THROW
    GFAPI_PUBLIC(glfs_upcall_unregister, 3.13.0);

/* Lease Types */
enum glfs_lease_types {
    GLFS_LEASE_NONE = 0,
    GLFS_RD_LEASE = 1,
    GLFS_RW_LEASE = 2,
};
typedef enum glfs_lease_types glfs_lease_types_t;

/* Lease cmds */
enum glfs_lease_cmds {
    GLFS_GET_LEASE = 1,
    GLFS_SET_LEASE = 2,
    GLFS_UNLK_LEASE = 3,
};
typedef enum glfs_lease_cmds glfs_lease_cmds_t;

struct glfs_lease {
    glfs_lease_cmds_t cmd;
    glfs_lease_types_t lease_type;
    glfs_leaseid_t lease_id;
    unsigned int lease_flags;
};
typedef struct glfs_lease glfs_lease_t;

typedef void (*glfs_recall_cbk)(glfs_lease_t lease, void *data);

/*
  SYNOPSIS

  glfs_lease: Takes a lease on a file.

  DESCRIPTION

  This function takes lease on an open file.

  PARAMETERS

  @glfd: The fd of the file on which lease should be taken,
   this fd is returned by glfs_open/glfs_create.

  @lease: Struct that defines the lease operation to be performed
   on the file.
      @lease.cmd - Can be one of the following values
         GF_GET_LEASE:  Get the lease type currently present on the file,
                        lease.lease_type will contain GF_RD_LEASE
                        or GF_RW_LEASE or 0 if no leases.
         GF_SET_LEASE:  Set the lease of given lease.lease_type on the file.
         GF_UNLK_LEASE: Unlock the lease present on the given fd.
                        Note that the every lease request should have
                        a corresponding unlk_lease.

      @lease.lease_type - Can be one of the following values
         GF_RD_LEASE:   Read lease on a file, shared lease.
         GF_RW_LEASE:   Read-Write lease on a file, exclusive lease.

      @lease.lease_id - A unique identification of lease, 128bits.

  @fn: This is the function that is invoked when the lease has to be recalled
  @data: It is a cookie, this pointer is returned as a part of recall

  fn and data field are stored as a part of glfs_fd, hence if there are multiple
  glfs_lease calls, each of them updates the fn and data fields. glfs_recall_cbk
  will be invoked with the last updated fn and data

  RETURN VALUES
  0:  Successful completion
  <0: Failure. @errno will be set with the type of failure
*/

int
glfs_lease(glfs_fd_t *glfd, glfs_lease_t *lease, glfs_recall_cbk fn,
           void *data) __THROW GFAPI_PUBLIC(glfs_lease, 4.0.0);

/*
  SYNOPSIS

  glfs_fsetattr: Function to set attributes.
  glfs_setattr: Function to set attributes

  DESCRIPTION

  The functions are used to set attributes on the file.

  PARAMETERS

  @glfs_fsetattr

     @glfd: The fd of the file for which the attributes are to be set,
            this fd is returned by glfs_open/glfs_create.

  @glfs_setattr

         @fs: File object.

         @path: The path of the file that is being operated on.

         @follow: Flag used to resolve symlink.


  @stat: Struct that has information about the file.

  @valid: This is the mask bit, that accepts GFAPI_SET_ATTR* masks.
          Refer glfs.h to see the mask definitions.

  Both functions are similar in functionality, just that the
  func setattr() uses file path whereas the func fsetattr()
  uses the fd.

  RETURN VALUES
  0:  Successful completion
  <0: Failure. @errno will be set with the type of failure

 */

int
glfs_fsetattr(struct glfs_fd *glfd, struct glfs_stat *stat) __THROW
    GFAPI_PUBLIC(glfs_fsetattr, 6.0);

int
glfs_setattr(struct glfs *fs, const char *path, struct glfs_stat *stat,
             int follow) __THROW GFAPI_PUBLIC(glfs_setattr, 6.0);

/*
  SYNOPSIS

  glfs_set_statedump_path: Function to set statedump path.

  DESCRIPTION

  This function is used to set statedump directory

  PARAMETERS

  @fs: The 'virtual mount' object to be configured with the volume
       specification file.

  @path: statedump path. Should be a directory. But the API won't fail if the
  directory doesn't exist yet, as one may create it later.

  RETURN VALUES

   0 : Success.
  -1 : Failure. @errno will be set with the type of failure.

 */

int
glfs_set_statedump_path(struct glfs *fs, const char *path) __THROW
    GFAPI_PUBLIC(glfs_set_statedump_path, 7.0);

__END_DECLS
#endif /* !_GLFS_H */
