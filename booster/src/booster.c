/*
   Copyright (c) 2007-2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <dlfcn.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <inttypes.h>
#include <libglusterfsclient.h>
#include <list.h>
#include <pthread.h>
#include <sys/xattr.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <logging.h>
#include <utime.h>
#include <dirent.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include "booster-fd.h"

#ifndef GF_UNIT_KB
#define GF_UNIT_KB 1024
#endif

static pthread_mutex_t cwdlock   = PTHREAD_MUTEX_INITIALIZER;

/* attr constructor registers this function with libc's
 * _init function as a function that must be called before
 * the main() of the program.
 */
static void booster_lib_init (void) __attribute__((constructor));

extern fd_t *
fd_ref (fd_t *fd);

extern void
fd_unref (fd_t *fd);

extern int pipe (int filedes[2]);
/* We define these flags so that we can remove fcntl.h from the include path.
 * fcntl.h has certain defines and other lines of code that redirect the
 * application's open and open64 calls to the syscalls defined by
 * libc, for us, thats not a Good Thing (TM).
 */
#ifndef GF_O_CREAT
#define GF_O_CREAT      0x40
#endif

#ifndef GF_O_TRUNC
#define GF_O_TRUNC      0x200
#endif

#ifndef GF_O_RDWR
#define GF_O_RDWR       0x2
#endif

#ifndef GF_O_WRONLY
#define GF_O_WRONLY     0x1
#endif

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

typedef enum {
        BOOSTER_OPEN,
        BOOSTER_CREAT
} booster_op_t;

struct _inode;
struct _dict;

ssize_t
write (int fd, const void *buf, size_t count);

/* open, open64, creat */
static int (*real_open) (const char *pathname, int flags, ...);
static int (*real_open64) (const char *pathname, int flags, ...);
static int (*real_creat) (const char *pathname, mode_t mode);
static int (*real_creat64) (const char *pathname, mode_t mode);

/* read, readv, pread, pread64 */
static ssize_t (*real_read) (int fd, void *buf, size_t count);
static ssize_t (*real_readv) (int fd, const struct iovec *vector, int count);
static ssize_t (*real_pread) (int fd, void *buf, size_t count,
                              unsigned long offset);
static ssize_t (*real_pread64) (int fd, void *buf, size_t count,
                                uint64_t offset);

/* write, writev, pwrite, pwrite64 */
static ssize_t (*real_write) (int fd, const void *buf, size_t count);
static ssize_t (*real_writev) (int fd, const struct iovec *vector, int count);
static ssize_t (*real_pwrite) (int fd, const void *buf, size_t count,
                               unsigned long offset);
static ssize_t (*real_pwrite64) (int fd, const void *buf, size_t count,
                                 uint64_t offset);

/* lseek, llseek, lseek64 */
static off_t (*real_lseek) (int fildes, unsigned long offset, int whence);
static off_t (*real_lseek64) (int fildes, uint64_t offset, int whence);

/* close */
static int (*real_close) (int fd);

/* dup dup2 */
static int (*real_dup) (int fd);
static int (*real_dup2) (int oldfd, int newfd);

static pid_t (*real_fork) (void);
static int (*real_mkdir) (const char *pathname, mode_t mode);
static int (*real_rmdir) (const char *pathname);
static int (*real_chmod) (const char *pathname, mode_t mode);
static int (*real_chown) (const char *pathname, uid_t owner, gid_t group);
static int (*real_fchmod) (int fd, mode_t mode);
static int (*real_fchown) (int fd, uid_t, gid_t gid);
static int (*real_fsync) (int fd);
static int (*real_ftruncate) (int fd, off_t length);
static int (*real_ftruncate64) (int fd, loff_t length);
static int (*real_link) (const char *oldpath, const char *newname);
static int (*real_rename) (const char *oldpath, const char *newpath);
static int (*real_utimes) (const char *path, const struct timeval times[2]);
static int (*real_utime) (const char *path, const struct utimbuf *buf);
static int (*real_mknod) (const char *path, mode_t mode, dev_t dev);
static int (*real_mkfifo) (const char *path, mode_t mode);
static int (*real_unlink) (const char *path);
static int (*real_symlink) (const char *oldpath, const char *newpath);
static int (*real_readlink) (const char *path, char *buf, size_t bufsize);
static char * (*real_realpath) (const char *path, char *resolved);
static DIR * (*real_opendir) (const char *path);
static struct dirent * (*real_readdir) (DIR *dir);
static struct dirent64 * (*real_readdir64) (DIR *dir);
static int (*real_readdir_r) (DIR *dir, struct dirent *entry,
                              struct dirent **result);
static int (*real_readdir64_r) (DIR *dir, struct dirent64 *entry,
                                struct dirent64 **result);
static int (*real_closedir) (DIR *dh);
static int (*real___xstat) (int ver, const char *path, struct stat *buf);
static int (*real___xstat64) (int ver, const char *path, struct stat64 *buf);
static int (*real_stat) (const char *path, struct stat *buf);
static int (*real_stat64) (const char *path, struct stat64 *buf);
static int (*real___fxstat) (int ver, int fd, struct stat *buf);
static int (*real___fxstat64) (int ver, int fd, struct stat64 *buf);
static int (*real_fstat) (int fd, struct stat *buf);
static int (*real_fstat64) (int fd , struct stat64 *buf);
static int (*real___lxstat) (int ver, const char *path, struct stat *buf);
static int (*real___lxstat64) (int ver, const char *path, struct stat64 *buf);
static int (*real_lstat) (const char *path, struct stat *buf);
static int (*real_lstat64) (const char *path, struct stat64 *buf);
static int (*real_statfs) (const char *path, struct statfs *buf);
static int (*real_statfs64) (const char *path, struct statfs64 *buf);
static int (*real_statvfs) (const char *path, struct statvfs *buf);
static int (*real_statvfs64) (const char *path, struct statvfs64 *buf);
static ssize_t (*real_getxattr) (const char *path, const char *name,
                                 void *value, size_t size);
static ssize_t (*real_lgetxattr) (const char *path, const char *name,
                                  void *value, size_t size);
static int (*real_remove) (const char* path);
static int (*real_lchown) (const char *path, uid_t owner, gid_t group);
static void (*real_rewinddir) (DIR *dirp);
static void (*real_seekdir) (DIR *dirp, off_t offset);
static off_t (*real_telldir) (DIR *dirp);

static ssize_t (*real_sendfile) (int out_fd, int in_fd, off_t *offset,
                                 size_t count);
static ssize_t (*real_sendfile64) (int out_fd, int in_fd, off_t *offset,
                                   size_t count);
static int (*real_fcntl) (int fd, int cmd, ...);
static int (*real_chdir) (const char *path);
static int (*real_fchdir) (int fd);  
static char * (*real_getcwd) (char *buf, size_t size);
static int (*real_truncate) (const char *path, off_t length);
static int (*real_truncate64) (const char *path, loff_t length);


#define RESOLVE(sym) do {                                       \
                if (!real_##sym)                                \
                        real_##sym = dlsym (RTLD_NEXT, #sym);   \
        } while (0)

/*TODO: set proper value */
#define MOUNT_HASH_SIZE 256

struct booster_mount {
        dev_t st_dev;
        glusterfs_handle_t handle;
        struct list_head device_list;
};
typedef struct booster_mount booster_mount_t;

static booster_fdtable_t *booster_fdtable = NULL;

extern int booster_configure (char *confpath);
/* This is dup'ed every time VMP open/creat wants a new fd.
 * This is needed so we occupy an entry in the process' file
 * table.
 */
int process_piped_fd = -1;

static int
booster_get_process_fd ()
{
        return real_dup (process_piped_fd);
}

/* The following two define which file contains
 * the FSTAB configuration for VMP-based usage.
 */
#define DEFAULT_BOOSTER_CONF    CONFDIR"/booster.conf"
#define BOOSTER_CONF_ENV_VAR    "GLUSTERFS_BOOSTER_FSTAB"


/* The following define which log file is used when
 * using the old mount point bypass approach.
 */
#define BOOSTER_DEFAULT_LOG     CONFDIR"/booster.log"
#define BOOSTER_LOG_ENV_VAR     "GLUSTERFS_BOOSTER_LOG"

void
do_open (int fd, const char *pathname, int flags, mode_t mode, booster_op_t op)
{
        char                   *specfile = NULL;
        char                   *mount_point = NULL; 
        int32_t                 size = 0;
        int32_t                 ret = -1;
        FILE                   *specfp = NULL;
        glusterfs_file_t        fh = NULL;
        char                   *logfile = NULL;
        glusterfs_init_params_t iparams = {
                .loglevel = "error",
                .lookup_timeout = 600,
                .stat_timeout = 600,
        };
      
        gf_log ("booster", GF_LOG_DEBUG, "Opening using MPB: %s", pathname);
        size = fgetxattr (fd, "user.glusterfs-booster-volfile", NULL, 0);
        if (size == -1) {
                gf_log ("booster", GF_LOG_ERROR, "Xattr "
                        "user.glusterfs-booster-volfile not found: %s",
                        strerror (errno));
                goto out;
        }
		
        specfile = calloc (1, size);
        if (!specfile) {
                gf_log ("booster", GF_LOG_ERROR, "Memory allocation failed");
                goto out;
        }

        ret = fgetxattr (fd, "user.glusterfs-booster-volfile", specfile,
                         size);
        if (ret == -1) {
                gf_log ("booster", GF_LOG_ERROR, "Xattr "
                        "user.glusterfs-booster-volfile not found: %s",
                        strerror (errno));
                goto out;
        }
    
        specfp = tmpfile ();
        if (!specfp) {
                gf_log ("booster", GF_LOG_ERROR, "Temp file creation failed"
                        ": %s", strerror (errno));
                goto out;
        }

        ret = fwrite (specfile, size, 1, specfp);
        if (ret != 1) {
                gf_log ("booster", GF_LOG_ERROR, "Failed to write volfile: %s",
                        strerror (errno));
                goto out;
        }
		
        fseek (specfp, 0L, SEEK_SET);

        size = fgetxattr (fd, "user.glusterfs-booster-mount", NULL, 0);
        if (size == -1) {
                gf_log ("booster", GF_LOG_ERROR, "Xattr "
                        "user.glusterfs-booster-mount not found: %s",
                        strerror (errno));
                goto out;
        }
        
        mount_point = calloc (size, sizeof (char));
        if (!mount_point) {
                gf_log ("booster", GF_LOG_ERROR, "Memory allocation failed");
                goto out;
        }
	
        ret = fgetxattr (fd, "user.glusterfs-booster-mount", mount_point, size);
        if (ret == -1) {
                gf_log ("booster", GF_LOG_ERROR, "Xattr "
                        "user.glusterfs-booster-mount not found: %s",
                        strerror (errno));
                goto out;
        }

        logfile = getenv (BOOSTER_LOG_ENV_VAR);
        if (logfile) {
                if (strlen (logfile) > 0)
                        iparams.logfile = strdup (logfile);
                else
                        iparams.logfile = strdup (BOOSTER_DEFAULT_LOG);
        } else {
                iparams.logfile = strdup (BOOSTER_DEFAULT_LOG);
        }

        gf_log ("booster", GF_LOG_TRACE, "Using log-file: %s", iparams.logfile);
        iparams.specfp = specfp;

        ret = glusterfs_mount (mount_point, &iparams);
        if (ret == -1) {
                if (errno != EEXIST) {
                        gf_log ("booster", GF_LOG_ERROR, "Mount failed over"
                                " glusterfs");
                        goto out;
                } else
                        gf_log ("booster", GF_LOG_ERROR, "Already mounted");
        }

        switch (op) {
        case BOOSTER_OPEN:
                gf_log ("booster", GF_LOG_TRACE, "Booster open call");
                fh = glusterfs_open (pathname, flags, mode);
                break;

        case BOOSTER_CREAT:
                gf_log ("booster", GF_LOG_TRACE, "Booster create call");
                fh = glusterfs_creat (pathname, mode);
                break;
        }

        if (!fh) {
                gf_log ("booster", GF_LOG_ERROR, "Error performing operation");
                goto out;
        }

        if (booster_fd_unused_get (booster_fdtable, fh, fd) == -1) {
                gf_log ("booster", GF_LOG_ERROR, "Failed to get unused FD");
                goto out;
        }
        fh = NULL;

out:
        if (specfile) {
                free (specfile);
        }

        if (specfp) {
                fclose (specfp);
        }

        if (mount_point) {
                free (mount_point);
        }

        if (fh) {
                glusterfs_close (fh);
        }

        return;
}

int
vmp_open (const char *pathname, int flags, ...)
{
        mode_t                  mode = 0;
        int                     fd = -1;
        glusterfs_file_t        fh = NULL;
        va_list                 ap;

        if (flags & GF_O_CREAT) {
                va_start (ap, flags);
                mode = va_arg (ap, mode_t);
                va_end (ap);

                fh = glusterfs_open (pathname, flags, mode);
        }
        else
                fh = glusterfs_open (pathname, flags);

        if (!fh) {
                gf_log ("booster", GF_LOG_ERROR, "VMP open failed");
                goto out;
        }

        fd = booster_get_process_fd ();
        if (fd == -1) {
                gf_log ("booster", GF_LOG_ERROR, "Failed to create open fd");
                goto fh_close_out;
        }

        if (booster_fd_unused_get (booster_fdtable, fh, fd) == -1) {
                gf_log ("booster", GF_LOG_ERROR, "Failed to map fd into table");
                goto realfd_close_out;
        }

        return fd;

realfd_close_out:
        real_close (fd);
        fd = -1;

fh_close_out:
        glusterfs_close (fh);

out:
        return fd;
}

#define BOOSTER_USE_OPEN64          1
#define BOOSTER_DONT_USE_OPEN64     0

int
booster_open (const char *pathname, int use64, int flags, ...)
{
        int     ret = -1;
        mode_t  mode = 0;
        va_list ap;
        int     (*my_open) (const char *pathname, int flags, ...);

        if (!pathname) {
                errno = EINVAL;
                goto out;
        }

        gf_log ("booster", GF_LOG_TRACE, "Open: %s", pathname);
        /* First try opening through the virtual mount point.
         * The difference lies in the fact that:
         * 1. We depend on libglusterfsclient library to perform
         * the translation from the path to handle.
         * 2. We do not go to the file system for the fd, instead
         * we use booster_get_process_fd (), which returns a dup'ed
         * fd of a pipe created in booster_init.
         */
        if (flags & GF_O_CREAT) {
                va_start (ap, flags);
                mode = va_arg (ap, mode_t);
                va_end (ap);
                ret = vmp_open (pathname, flags, mode);
        }
        else
                ret = vmp_open (pathname, flags);

        /* We receive an ENODEV if the VMP does not exist. If we
         * receive an error other than ENODEV, it means, there
         * actually was an error performing vmp_open. This must
         * be returned to the user.
         */
        if ((ret < 0) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "Error in opening file over "
                        " VMP: %s", strerror (errno));
                goto out;
        }

        if (ret > 0) {
                gf_log ("booster", GF_LOG_TRACE, "File opened");
                goto out;
        }

        if (use64) {
                gf_log ("booster", GF_LOG_TRACE, "Using 64-bit open");
		my_open = real_open64;
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Using 32-bit open");
		my_open = real_open;
        }

        /* It is possible the RESOLVE macro is not able
         * to resolve the symbol of a function, in that case
         * we dont want to seg-fault on calling a NULL functor.
         */
        if (my_open == NULL) {
                gf_log ("booster", GF_LOG_ERROR, "open not resolved");
                ret = -1;
                errno = ENOSYS;
                goto out;
        }

	if (flags & GF_O_CREAT) {
		va_start (ap, flags);
		mode = va_arg (ap, mode_t);
		va_end (ap);

                ret = my_open (pathname, flags, mode);
	} else
                ret = my_open (pathname, flags);

        if (ret != -1) {
                do_open (ret, pathname, flags, mode, BOOSTER_OPEN);
        }

out:
        return ret;
}

/* This is done to over-write existing definitions of open and open64 inside
 * libc with our own copies. __REDIRECT is provided by libc.
 *
 * XXX: This will not work anywhere other than libc based systems.
 */
int __REDIRECT (booster_false_open, (__const char *__file, int __oflag, ...),
                open) __nonnull ((1));
int __REDIRECT (booster_false_open64, (__const char *__file, int __oflag, ...),
                open64) __nonnull ((1));
int
booster_false_open (const char *pathname, int flags, ...)
{
        int     ret;
        mode_t  mode = 0;
        va_list ap;

        if (flags & GF_O_CREAT) {
                va_start (ap, flags);
                mode = va_arg (ap, mode_t);
                va_end (ap);

                ret = booster_open (pathname, BOOSTER_DONT_USE_OPEN64, flags,
                                    mode);
        }
        else
                ret = booster_open (pathname, BOOSTER_DONT_USE_OPEN64, flags);

        return ret;
}

int
booster_false_open64 (const char *pathname, int flags, ...)
{
        int     ret;
        mode_t  mode = 0;
        va_list ap;

        if (flags & GF_O_CREAT) {
                va_start (ap, flags);
                mode = va_arg (ap, mode_t);
                va_end (ap);

                ret = booster_open (pathname, BOOSTER_USE_OPEN64, flags, mode);
        }
        else
                ret = booster_open (pathname, BOOSTER_USE_OPEN64, flags);

        return ret;
}

int
vmp_creat (const char *pathname, mode_t mode)
{
        int                     fd = -1;
        glusterfs_file_t        fh = NULL;

        fh = glusterfs_creat (pathname, mode);
        if (!fh) {
                gf_log ("booster", GF_LOG_ERROR, "Create failed: %s: %s",
                        pathname, strerror (errno));
                goto out;
        }

        fd = booster_get_process_fd ();
        if (fd == -1) {
                gf_log ("booster", GF_LOG_ERROR, "Failed to create fd");
                goto close_out;
        }

        if ((booster_fd_unused_get (booster_fdtable, fh, fd)) == -1) {
                gf_log ("booster", GF_LOG_ERROR, "Failed to map unused fd");
                goto real_close_out;
        }

        return fd;

real_close_out:
        real_close (fd);
        fd = -1;

close_out:
        glusterfs_close (fh);

out:
        return -1;
}

int __REDIRECT (booster_false_creat, (const char *pathname, mode_t mode),
                creat) __nonnull ((1));
int __REDIRECT (booster_false_creat64, (const char *pathname, mode_t mode),
                creat64) __nonnull ((1));

int
booster_false_creat (const char *pathname, mode_t mode)
{
        int     ret = -1;
        if (!pathname) {
                errno = EINVAL;
                goto out;
        }

        gf_log ("booster", GF_LOG_TRACE, "Create: %s", pathname);
        ret = vmp_creat (pathname, mode);

        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "VMP create failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret > 0) {
                gf_log ("booster", GF_LOG_TRACE, "File created");
                goto out;
        }

        if (real_creat == NULL) {
                errno = ENOSYS;
                ret = -1;
                goto out;
        }

        ret = real_creat (pathname, mode);

        if (ret != -1) {
                do_open (ret, pathname, GF_O_WRONLY | GF_O_TRUNC, mode,
                         BOOSTER_CREAT);
        } else
                gf_log ("booster", GF_LOG_ERROR, "real create failed: %s",
                        strerror (errno));

out:
        return ret;
}


int
booster_false_creat64 (const char *pathname, mode_t mode)
{
        int     ret = -1;
        if (!pathname) {
                errno = EINVAL;
                goto out;
        }

        gf_log ("booster", GF_LOG_TRACE, "Create: %s", pathname);
        ret = vmp_creat (pathname, mode);

        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "VMP create failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret > 0) {
                gf_log ("booster", GF_LOG_TRACE, "File created");
                goto out;
        }

        if (real_creat64 == NULL) {
                errno = ENOSYS;
                ret = -1;
                goto out;
        }

        ret = real_creat64 (pathname, mode);

        if (ret != -1) {
                do_open (ret, pathname, GF_O_WRONLY | GF_O_TRUNC, mode,
                         BOOSTER_CREAT);
        } else
                gf_log ("booster", GF_LOG_ERROR, "real create failed: %s",
                        strerror (errno));

out:
        return ret;
}


/* pread */

ssize_t
pread (int fd, void *buf, size_t count, unsigned long offset)
{
        ssize_t ret;
        glusterfs_file_t glfs_fd = 0;

        gf_log ("booster", GF_LOG_TRACE, "pread: fd %d, count %lu, offset %lu"
                ,fd, (long unsigned)count, offset);
        glfs_fd = booster_fdptr_get (booster_fdtable, fd);
        if (!glfs_fd) { 
                gf_log ("booster", GF_LOG_TRACE, "Not booster fd");
                if (real_pread == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_pread (fd, buf, count, offset);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_pread (glfs_fd, buf, count, offset);
                booster_fdptr_put (glfs_fd);
        }

        return ret;
}


ssize_t
pread64 (int fd, void *buf, size_t count, uint64_t offset)
{
        ssize_t ret;
        glusterfs_file_t glfs_fd = 0;

        gf_log ("booster", GF_LOG_TRACE, "pread64: fd %d, count %lu, offset %"
                PRIu64, fd, (long unsigned)count, offset);
        glfs_fd = booster_fdptr_get (booster_fdtable, fd);
        if (!glfs_fd) { 
                gf_log ("booster", GF_LOG_TRACE, "Not booster fd");
                if (real_pread64 == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_pread64 (fd, buf, count, offset);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_pread (glfs_fd, buf, count, offset);
                booster_fdptr_put (glfs_fd);
        }

        return ret;
}


ssize_t
read (int fd, void *buf, size_t count)
{
        int ret;
        glusterfs_file_t glfs_fd;

        gf_log ("booster", GF_LOG_TRACE, "read: fd %d, count %lu", fd,
                (long unsigned)count);
        glfs_fd = booster_fdptr_get (booster_fdtable, fd);
        if (!glfs_fd) {
                gf_log ("booster", GF_LOG_TRACE, "Not booster fd");
                if (real_read == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_read (fd, buf, count);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_read (glfs_fd, buf, count);
                booster_fdptr_put (glfs_fd);
        }

        return ret;
}


ssize_t
readv (int fd, const struct iovec *vector, int count)
{
        int ret;
        glusterfs_file_t glfs_fd = 0;

        gf_log ("booster", GF_LOG_TRACE, "readv: fd %d, iovecs %d", fd, count);
        glfs_fd = booster_fdptr_get (booster_fdtable, fd);
        if (!glfs_fd) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_readv == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_readv (fd, vector, count);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
		ret = glusterfs_readv (glfs_fd, vector, count);
                booster_fdptr_put (glfs_fd);
        }

        return ret;
}


ssize_t
write (int fd, const void *buf, size_t count)
{
        int ret;
        glusterfs_file_t glfs_fd = 0;

        gf_log ("booster", GF_LOG_TRACE, "write: fd %d, count %d", fd, count);

        glfs_fd = booster_fdptr_get (booster_fdtable, fd);

        if (!glfs_fd) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_write == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_write (fd, buf, count);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_write (glfs_fd, buf, count);
                booster_fdptr_put (glfs_fd);
        }
 
        return ret;
}

ssize_t
writev (int fd, const struct iovec *vector, int count)
{
        int ret = 0;
        glusterfs_file_t glfs_fd = 0; 

        gf_log ("booster", GF_LOG_TRACE, "writev: fd %d, iovecs %d", fd, count);
        glfs_fd = booster_fdptr_get (booster_fdtable, fd);

        if (!glfs_fd) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_writev == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_writev (fd, vector, count);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_writev (glfs_fd, vector, count);
                booster_fdptr_put (glfs_fd);
        }

        return ret;
}


ssize_t
pwrite (int fd, const void *buf, size_t count, unsigned long offset)
{
        int ret;
        glusterfs_file_t glfs_fd = 0;

        gf_log ("booster", GF_LOG_TRACE, "pwrite: fd %d, count %d, offset %lu",
                fd, count, offset);
        glfs_fd = booster_fdptr_get (booster_fdtable, fd);

        if (!glfs_fd) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_pwrite == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_pwrite (fd, buf, count, offset);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_pwrite (glfs_fd, buf, count, offset);
                booster_fdptr_put (glfs_fd);
        }

        return ret;
}


ssize_t
pwrite64 (int fd, const void *buf, size_t count, uint64_t offset)
{
        int ret;
        glusterfs_file_t glfs_fd = 0;

        gf_log ("booster", GF_LOG_TRACE, "pwrite64: fd %d, count %d, offset %"
                PRIu64, fd, count, offset);
        glfs_fd = booster_fdptr_get (booster_fdtable, fd);
  
        if (!glfs_fd) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_pwrite64 == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_pwrite64 (fd, buf, count, offset);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_pwrite (glfs_fd, buf, count, offset);
                booster_fdptr_put (glfs_fd);
        }

        return ret;
}


int
close (int fd)
{
        int ret = -1;
        glusterfs_file_t glfs_fd = 0;

        gf_log ("booster", GF_LOG_TRACE, "close: fd %d", fd);
	glfs_fd = booster_fdptr_get (booster_fdtable, fd);
    
	if (glfs_fd) {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
		booster_fd_put (booster_fdtable, fd);
		ret = glusterfs_close (glfs_fd);
		booster_fdptr_put (glfs_fd);
	}

        ret = real_close (fd);

        return ret;
}

#ifndef _LSEEK_DECLARED
#define _LSEEK_DECLARED
off_t
lseek (int filedes, unsigned long offset, int whence)
{
        int ret;
        glusterfs_file_t glfs_fd = 0;

        gf_log ("booster", GF_LOG_TRACE, "lseek: fd %d, offset %ld",
                filedes, offset);

        glfs_fd = booster_fdptr_get (booster_fdtable, filedes);
        if (glfs_fd) {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_lseek (glfs_fd, offset, whence);
                booster_fdptr_put (glfs_fd);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_lseek == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_lseek (filedes, offset, whence);
        }

        return ret;
}
#endif

off_t
lseek64 (int filedes, uint64_t offset, int whence)
{
        int ret;
        glusterfs_file_t glfs_fd = 0;


        gf_log ("booster", GF_LOG_TRACE, "lseek: fd %d, offset %"PRIu64,
                filedes, offset);
        glfs_fd = booster_fdptr_get (booster_fdtable, filedes);
        if (glfs_fd) {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_lseek (glfs_fd, offset, whence);
                booster_fdptr_put (glfs_fd);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_lseek64 == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_lseek64 (filedes, offset, whence);
        }

        return ret;
}

int 
dup (int oldfd)
{
        int ret = -1, new_fd = -1;
        glusterfs_file_t glfs_fd = 0;

        gf_log ("booster", GF_LOG_TRACE, "dup: fd %d", oldfd);
        glfs_fd = booster_fdptr_get (booster_fdtable, oldfd);
        new_fd = real_dup (oldfd);

        if (new_fd >=0 && glfs_fd) {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = booster_fd_unused_get (booster_fdtable, glfs_fd,
                                             new_fd);
                fd_ref ((fd_t *)glfs_fd);
                if (ret == -1) {
                        gf_log ("booster", GF_LOG_ERROR,"Failed to map new fd");
                        real_close (new_fd);
                } 
        }

        if (glfs_fd) {
                booster_fdptr_put (glfs_fd);
        }

        return new_fd;
}


int 
dup2 (int oldfd, int newfd)
{
        int ret = -1;
        glusterfs_file_t old_glfs_fd = NULL, new_glfs_fd = NULL;

        if (oldfd == newfd) {
                return newfd;
        }

        old_glfs_fd = booster_fdptr_get (booster_fdtable, oldfd);
        new_glfs_fd = booster_fdptr_get (booster_fdtable, newfd);
 
        ret = real_dup2 (oldfd, newfd); 
        if (ret >= 0) {
                if (new_glfs_fd) {
                        glusterfs_close (new_glfs_fd);
                        booster_fdptr_put (new_glfs_fd);
                        booster_fd_put (booster_fdtable, newfd);
                        new_glfs_fd = 0;
                }

                if (old_glfs_fd) {
                        ret = booster_fd_unused_get (booster_fdtable,
                                                     old_glfs_fd, newfd);
                        fd_ref ((fd_t *)old_glfs_fd);
                        if (ret == -1) {
                                real_close (newfd);
                        }
                }
        } 

        if (old_glfs_fd) {
                booster_fdptr_put (old_glfs_fd);
        }

        if (new_glfs_fd) {
                booster_fdptr_put (new_glfs_fd);
        }

        return ret;
}

int
mkdir (const char *pathname, mode_t mode)
{
        int     ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "mkdir: path %s", pathname);
        ret = glusterfs_mkdir (pathname, mode);

        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "mkdir failed: %s",
                        strerror (errno));
                return ret;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "directory created");
                return ret;
        }

        if (real_mkdir == NULL) {
                ret = -1;
                errno = ENOSYS;
        } else
                ret = real_mkdir (pathname, mode);

        return ret;
}

int
rmdir (const char *pathname)
{
        int     ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "rmdir: path %s", pathname);
        ret = glusterfs_rmdir (pathname);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "rmdir failed: %s",
                        strerror (errno));
                return ret;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "directory removed");
                return ret;
        }

        if (real_rmdir == NULL) {
                errno = ENOSYS;
                ret = -1;
        } else
                ret = real_rmdir (pathname);

        return ret;
}

int
chmod (const char *pathname, mode_t mode)
{
        int     ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "chmod: path %s", pathname);
        ret = glusterfs_chmod (pathname, mode);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "chmod failed: %s",
                        strerror (errno));
                return ret;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "chmod succeeded");
                return ret;
        }

        if (real_chmod == NULL) {
                errno = ENOSYS;
                ret = -1;
        } else
                ret = real_chmod (pathname, mode);

        return ret;
}

int
chown (const char *pathname, uid_t owner, gid_t group)
{
        int     ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "chown: path: %s", pathname);
        ret = glusterfs_chown (pathname, owner, group);

        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "chown failed: %s\n",
                        strerror (errno));
                return ret;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "chown succeeded");
                return ret;
        }

        if (real_chown == NULL) {
                errno = ENOSYS;
                ret = -1;
        } else
                ret = real_chown (pathname, owner, group);

        return ret;
}

int
fchown (int fd, uid_t owner, gid_t group)
{
        int                     ret = -1;
        glusterfs_file_t        fh = NULL;

        gf_log ("booster", GF_LOG_TRACE, "fchown: fd %d, uid %d, gid %d", fd,
                owner, group);
        fh = booster_fdptr_get (booster_fdtable, fd);
        if (!fh) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_fchown == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_fchown (fd, owner, group);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_fchown (fh, owner, group);
                booster_fdptr_put (fh);
        }

        return ret;
}


#define MOUNT_TABLE_HASH_SIZE 256


static void booster_cleanup (void);
static int 
booster_init (void)
{
        char    *booster_conf_path = NULL;
        int     ret = -1;
        int     pipefd[2];

        booster_fdtable = booster_fdtable_alloc ();
        if (!booster_fdtable) {
                fprintf (stderr, "cannot allocate fdtable: %s\n",
                         strerror (errno));
		goto err;
        }
 
        if (pipe (pipefd) == -1) {
                gf_log ("booster-fstab", GF_LOG_ERROR, "Pipe creation failed:%s"
                        , strerror (errno));
                goto err;
        }

        process_piped_fd = pipefd[0];
        real_close (pipefd[1]);
        /* libglusterfsclient based VMPs should be inited only
         * after the file tables are inited so that if the socket
         * calls use the fd based syscalls, the fd tables are
         * correctly initialized to return a NULL handle, on which the
         * socket calls will fall-back to the real API.
         */
        booster_conf_path = getenv (BOOSTER_CONF_ENV_VAR);
        if (booster_conf_path != NULL) {
                if (strlen (booster_conf_path) > 0)
                        ret = booster_configure (booster_conf_path);
                else {
                        gf_log ("booster", GF_LOG_ERROR, "%s not defined, "
                                "using default path: %s", BOOSTER_CONF_ENV_VAR,
                                DEFAULT_BOOSTER_CONF);
                        ret = booster_configure (DEFAULT_BOOSTER_CONF);
                }
        } else {
                gf_log ("booster", GF_LOG_ERROR, "%s not defined, using default"
                        " path: %s", BOOSTER_CONF_ENV_VAR,DEFAULT_BOOSTER_CONF);
                ret = booster_configure (DEFAULT_BOOSTER_CONF);
        }

        atexit (booster_cleanup);
        if (ret == 0)
                gf_log ("booster", GF_LOG_DEBUG, "booster is inited");
	return 0;

err:
        /* Sure we return an error value here
         * but who cares about booster.
         */
	return -1; 
}


static void
booster_cleanup (void)
{
        /* Ideally, we should be de-initing the fd-table
         * here but the problem is that I've seen file accesses through booster
         * continuing while the atexit registered function is called. That means
         * , we cannot dealloc the fd-table since then there could be a crash
         * while trying to determine whether a given fd is for libc or for
         * libglusterfsclient.
         * We should be satisfied with having cleaned up glusterfs contexts.
         */
        glusterfs_umount_all ();
	glusterfs_reset ();
}

int
fchmod (int fd, mode_t mode)
{
        int                     ret = -1;
        glusterfs_file_t        fh = NULL;

        gf_log ("booster", GF_LOG_TRACE, "fchmod: fd %d, mode: 0x%x", fd, mode);
        fh = booster_fdptr_get (booster_fdtable, fd);
        if (!fh) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_fchmod == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_fchmod (fd, mode);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_fchmod (fh, mode);
                booster_fdptr_put (fh);
        }

        return ret;
}

int
fsync (int fd)
{
        int                     ret = -1;
        glusterfs_file_t        fh = NULL;

        gf_log ("booster", GF_LOG_TRACE, "fsync: fd %d", fd);
        fh = booster_fdptr_get (booster_fdtable, fd);
        if (!fh) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_fsync == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_fsync (fd);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_fsync (fh);
                booster_fdptr_put (fh);
        }

        return ret;
}

int __REDIRECT (booster_false_ftruncate, (int fd, off_t length),
                ftruncate);
int __REDIRECT (booster_false_ftruncate64, (int fd, loff_t length),
                ftruncate64);

int
booster_false_ftruncate (int fd, off_t length)
{
        int                     ret = -1;
        glusterfs_file_t        fh = NULL;

        gf_log ("booster", GF_LOG_TRACE, "ftruncate: fd %d, length: %"PRIu64,fd
                , length);
        fh = booster_fdptr_get (booster_fdtable, fd);
        if (!fh) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_ftruncate == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_ftruncate (fd, length);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_ftruncate (fh, length);
                booster_fdptr_put (fh);
        }

        return ret;
}

int
booster_false_ftruncate64 (int fd, loff_t length)
{
        int                     ret = -1;
        glusterfs_file_t        fh = NULL;

        gf_log ("booster", GF_LOG_TRACE, "ftruncate: fd %d, length: %"PRIu64,fd
                , length);
        fh = booster_fdptr_get (booster_fdtable, fd);
        if (!fh) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_ftruncate == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_ftruncate64 (fd, length);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_ftruncate (fh, length);
                booster_fdptr_put (fh);
        }

        return ret;
}

int
link (const char *old, const char *new)
{
        int             ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "link: old: %s, new: %s", old, new);
        ret = glusterfs_link (old, new);

        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "Link failed: %s",
                        strerror (errno));
                return ret;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "link call succeeded");
                return ret;
        }

        if (real_link == NULL) {
                errno = ENOSYS;
                ret = -1;
        } else
                ret = real_link (old, new);

        return ret;
}

int
rename (const char *old, const char *new)
{
        int             ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "link: old: %s, new: %s", old, new);
        ret = glusterfs_rename (old, new);

        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "Rename failed: %s",
                        strerror (errno));
                return ret;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "Rename succeeded");
                return ret;
        }

        if (real_rename == NULL) {
                errno = ENOSYS;
                ret = -1;
        } else
                ret = real_rename (old, new);

        return ret;
}

int
utimes (const char *path, const struct timeval times[2])
{
        int             ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "utimes: path %s", path);
        ret = glusterfs_utimes (path, times);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "utimes failed: %s",
                        strerror (errno));
                return ret;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "utimes succeeded");
                return ret;
        }

        if (real_utimes == NULL) {
                errno = ENOSYS;
                ret = -1;
        } else
                ret = real_utimes (path, times);

        return ret;
}

int
utime (const char *path, const struct utimbuf *buf)
{
        int     ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "utime: path %s", path);
        ret = glusterfs_utime (path, buf);

        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "utime failed: %s",
                        strerror (errno));
                return ret;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "utime succeeded");
                return ret;
        }

        if (real_utime == NULL) {
                errno = ENOSYS;
                ret = -1;
        } else
                ret = real_utime (path, buf);

        return ret;
}

int
mknod (const char *path, mode_t mode, dev_t dev)
{
        int     ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "mknod: path %s", path);
        ret = glusterfs_mknod (path, mode, dev);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "mknod failed: %s",
                        strerror (errno));
                return ret;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "mknod succeeded");
                return ret;
        }

        if (real_mknod) {
                errno = ENOSYS;
                ret = -1;
        } else
                ret = real_mknod (path, mode, dev);

        return ret;
}

int
mkfifo (const  char *path, mode_t mode)
{
        int     ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "mkfifo: path %s", path);
        ret = glusterfs_mkfifo (path, mode);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "mkfifo failed: %s",
                        strerror (errno));
                return ret;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "mkfifo succeeded");
                return ret;
        }

        if (real_mkfifo == NULL) {
                errno = ENOSYS;
                ret = -1;
        } else
                ret = real_mkfifo (path, mode);

        return ret;
}

int
unlink (const char *path)
{
        int     ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "unlink: path %s", path);
        ret = glusterfs_unlink (path);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "unlink failed: %s",
                        strerror (errno));
                return ret;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "unlink succeeded");
                return ret;
        }

        if (real_unlink == NULL) {
                errno = ENOSYS;
                ret = -1;
        } else
                ret = real_unlink (path);

        return ret;
}

int
symlink (const char *oldpath, const char *newpath)
{
        int     ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "symlink: old: %s, new: %s",
                oldpath, newpath);
        ret = glusterfs_symlink (oldpath, newpath);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "symlink failed: %s",
                        strerror (errno));
                return ret;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "symlink succeeded");
                return ret;
        }

        if (real_symlink == NULL) {
                errno = ENOSYS;
                ret = -1;
        } else
                ret = real_symlink (oldpath, newpath);

        return ret;
}

int
readlink (const char *path, char *buf, size_t bufsize)
{
        int     ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "readlink: path %s", path);
        ret = glusterfs_readlink (path, buf, bufsize);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "readlink failed: %s",
                        strerror (errno));
                return ret;
        }

        if (ret > 0) {
                gf_log ("booster", GF_LOG_TRACE, "readlink succeeded");
                return ret;
        }

        if (real_readlink == NULL) {
                errno = ENOSYS;
                ret = -1;
        } else
                ret = real_readlink (path, buf, bufsize);

        return ret;
}

char *
realpath (const char *path, char *resolved_path)
{
        char    *res = NULL;

        gf_log ("booster", GF_LOG_TRACE, "realpath: path %s", path);
        res = glusterfs_realpath (path, resolved_path);
        if ((res == NULL) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "realpath failed: %s",
                        strerror (errno));
                return res;
        }

        if (res != NULL) {
                gf_log ("booster", GF_LOG_TRACE, "realpath succeeded");
                return res;
        }

        if (real_realpath == NULL) {
                errno = ENOSYS;
                res = NULL;
        } else
                res = real_realpath (path, resolved_path);

        return res;
}

#define BOOSTER_GL_DIR          1
#define BOOSTER_POSIX_DIR       2

struct booster_dir_handle {
        int type;
        void *dirh;
};

DIR *
opendir (const char *path)
{
        glusterfs_dir_t                 gdir = NULL;
        struct booster_dir_handle       *bh = NULL;
        DIR                             *pdir = NULL;

        gf_log ("booster", GF_LOG_TRACE, "opendir: path: %s", path);
        bh = calloc (1, sizeof (struct booster_dir_handle));
        if (!bh) {
                gf_log ("booster", GF_LOG_ERROR, "memory allocation failed");
                errno = ENOMEM;
                goto out;
        }

        gdir = glusterfs_opendir (path);
        if (gdir) {
                gf_log ("booster", GF_LOG_TRACE, "Gluster dir opened");
                bh->type = BOOSTER_GL_DIR;
                bh->dirh = (void *)gdir;
                goto out;
        } else if ((!gdir) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "Opendir failed");
                goto free_out;
        }

        if (real_opendir == NULL) {
                errno = ENOSYS;
                goto free_out;
        }

        pdir = real_opendir (path);

        if (pdir) {
                bh->type = BOOSTER_POSIX_DIR;
                bh->dirh = (void *)pdir;
                goto out;
        }

free_out:
        if (bh) {
                free (bh);
                bh = NULL;
        }
out:
        return (DIR *)bh;
}

int __REDIRECT (booster_false_readdir_r, (DIR *dir, struct dirent *entry,
                struct dirent **result), readdir_r) __nonnull ((1));
int __REDIRECT (booster_false_readdir64_r, (DIR *dir, struct dirent64 *entry,
                struct dirent64 **result), readdir64_r) __nonnull ((1));

int
booster_false_readdir_r (DIR *dir, struct dirent *entry, struct dirent **result)
{
        struct booster_dir_handle       *bh = (struct booster_dir_handle *)dir;
        int                              ret = 0;  

        if (!bh) {
                ret = errno = EFAULT;
                goto out;
        }

        if (bh->type == BOOSTER_GL_DIR) {
                gf_log ("booster", GF_LOG_TRACE, "readdir_r on gluster");
                ret = glusterfs_readdir_r ((glusterfs_dir_t)bh->dirh, entry,
                                           result);
                
        } else if (bh->type == BOOSTER_POSIX_DIR) {
                gf_log ("booster", GF_LOG_TRACE, "readdir_r on posix");
                if (real_readdir_r == NULL) {
                        ret = errno = ENOSYS;
                        goto out;
                }

                ret = real_readdir_r ((DIR *)bh->dirh, entry, result);
        } else {
                ret = errno = EINVAL;
        }

out:
        return  ret;
}

int
booster_false_readdir64_r (DIR *dir, struct dirent64 *entry,
                           struct dirent64 **result)
{
        struct booster_dir_handle       *bh = (struct booster_dir_handle *)dir;
        int                              ret = 0;  

        if (!bh) {
                ret = errno = EFAULT;
                goto out;
        }

        if (bh->type == BOOSTER_GL_DIR) {
                gf_log ("booster", GF_LOG_TRACE, "readdir_r on gluster");
                ret = glusterfs_readdir_r ((glusterfs_dir_t)bh->dirh,
                                           (struct dirent *)entry,
                                           (struct dirent **)result);
        } else if (bh->type == BOOSTER_POSIX_DIR) {
                gf_log ("booster", GF_LOG_TRACE, "readdir_r on posix");
                if (real_readdir64_r == NULL) {
                        ret = errno = ENOSYS;
                        goto out;
                }

                ret = real_readdir64_r ((DIR *)bh->dirh, entry, result);
        } else {
                ret = errno = EINVAL;
        }

out:
        return  ret;
}

struct dirent *
__REDIRECT (booster_false_readdir, (DIR *dir), readdir) __nonnull ((1));

struct dirent64 *
__REDIRECT (booster_false_readdir64, (DIR *dir), readdir64) __nonnull ((1));

struct dirent *
booster_false_readdir (DIR *dir)
{
        struct booster_dir_handle       *bh = (struct booster_dir_handle *)dir;
        struct dirent                   *dirp = NULL;

        if (!bh) {
                errno = EFAULT;
                goto out;
        }

        if (bh->type == BOOSTER_GL_DIR) {
                gf_log ("booster", GF_LOG_TRACE, "readdir on gluster");
                dirp = glusterfs_readdir ((glusterfs_dir_t)bh->dirh);
        } else if (bh->type == BOOSTER_POSIX_DIR) {
                gf_log ("booster", GF_LOG_TRACE, "readdir on posix");
                if (real_readdir == NULL) {
                        errno = ENOSYS;
                        dirp = NULL;
                        goto out;
                }

                dirp = real_readdir ((DIR *)bh->dirh);
        } else {
                dirp = NULL;
                errno = EINVAL;
        }

out:
        return  dirp;
}

struct dirent64 *
booster_false_readdir64 (DIR *dir)
{
        struct booster_dir_handle       *bh = (struct booster_dir_handle *)dir;
        struct dirent64                 *dirp = NULL;

        if (!bh) {
                errno = EFAULT;
                goto out;
        }

        if (bh->type == BOOSTER_GL_DIR) {
                gf_log ("booster", GF_LOG_TRACE, "readdir on gluster");
                dirp = glusterfs_readdir ((glusterfs_dir_t)bh->dirh);
        } else if (bh->type == BOOSTER_POSIX_DIR) {
                gf_log ("booster", GF_LOG_TRACE, "readdir on posix");
                if (real_readdir == NULL) {
                        errno = ENOSYS;
                        dirp = NULL;
                        goto out;
                }

                dirp = real_readdir64 ((DIR *)bh->dirh);
        } else {
                dirp = NULL;
                errno = EINVAL;
        }

out:
        return  dirp;
}

int
closedir (DIR *dh)
{
        struct booster_dir_handle       *bh = (struct booster_dir_handle *)dh;
        int                             ret = -1;

        if (!bh) {
                errno = EFAULT;
                goto out;
        }

        if (bh->type == BOOSTER_GL_DIR) {
                gf_log ("booster", GF_LOG_TRACE, "closedir on gluster");
                ret = glusterfs_closedir ((glusterfs_dir_t)bh->dirh);
        } else if (bh->type == BOOSTER_POSIX_DIR) {
                gf_log ("booster", GF_LOG_TRACE, "closedir on posix");
                if (real_closedir == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else
                        ret = real_closedir ((DIR *)bh->dirh);
        } else {
                errno = EBADF;
        }

        if (ret == 0) {
                free (bh);
                bh = NULL;
        }
out:
        return ret;
}

/* The real stat functions reside in booster_stat.c to
 * prevent clash with the statX prototype and functions
 * declared from sys/stat.h
 */
int
booster_xstat (int ver, const char *path, void *buf)
{
        struct stat     *sbuf = (struct stat *)buf;
        int             ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "xstat: path: %s", path);
        ret = glusterfs_stat (path, sbuf);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "xstat failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "xstat succeeded");
                goto out;
        }

        if (real___xstat == NULL) {
                ret = -1;
                errno = ENOSYS;
                goto out;
        }

        ret = real___xstat (ver, path, sbuf);
out:
        return ret;
}

int
booster_xstat64 (int ver, const char *path, void *buf)
{
        int             ret = -1;
        struct stat64   *sbuf = (struct stat64 *)buf;

        gf_log ("booster", GF_LOG_TRACE, "xstat64: path: %s", path);
        ret = glusterfs_stat (path, (struct stat *)sbuf);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "xstat64 failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "xstat64 succeeded");
                goto out;
        }

        if (real___xstat64 == NULL) {
                errno = ENOSYS;
                ret = -1;
                goto out;
        }

        ret = real___xstat64 (ver, path, sbuf);
out:
        return ret;
}

int
booster_stat (const char *path, void *buf)
{
        struct stat     *sbuf = (struct stat *)buf;
        int             ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "stat: path: %s", path);
        ret = glusterfs_stat (path, sbuf);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "stat failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "stat succeeded");
                goto out;
        }

        if (real_stat != NULL)
                ret = real_stat (path, sbuf);
        else if (real___xstat != NULL)
                ret = real___xstat (0, path, sbuf);
        else {
                errno = ENOSYS;
                ret = -1;
                goto out;
        }


out:
        return ret;
}

int
booster_stat64 (const char *path, void *buf)
{
        int             ret = -1;
        struct stat64   *sbuf = (struct stat64 *)buf;

        gf_log ("booster", GF_LOG_TRACE, "stat64: %s", path);
        ret = glusterfs_stat (path, (struct stat *)sbuf);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "stat64 failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "stat64 succeeded");
                goto out;
        }

        if (real_stat64 != NULL)
                ret = real_stat64 (path, sbuf);
        else if (real___xstat64 != NULL)
                ret = real___xstat64 (0, path, sbuf);
        else {
                errno = ENOSYS;
                ret = -1;
                goto out;
        }

out:
        return ret;
}

int
booster_fxstat (int ver, int fd, void *buf)
{
        struct stat             *sbuf = (struct stat *)buf;
        int                     ret = -1;
        glusterfs_file_t        fh = NULL;

        gf_log ("booster", GF_LOG_TRACE, "fxstat: fd %d", fd);
        fh = booster_fdptr_get (booster_fdtable, fd);
        if (!fh) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real___fxstat == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                        goto out;
                }

                ret = real___fxstat (ver, fd, sbuf);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_fstat (fh, sbuf);
                booster_fdptr_put (fh);
        }

out:
        return ret;
}

int
booster_fxstat64 (int ver, int fd, void *buf)
{
        int                     ret = -1;
        struct stat64           *sbuf = (struct stat64 *)buf;
        glusterfs_file_t        fh = NULL;

        gf_log ("booster", GF_LOG_TRACE, "fxstat64: fd %d", fd);
        fh = booster_fdptr_get (booster_fdtable, fd);
        if (!fh) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real___fxstat64 == NULL) {
                        ret = -1;
                        errno = ENOSYS;
                        goto out;
                }
                ret = real___fxstat64 (ver, fd, sbuf);
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_fstat (fh, (struct stat *)sbuf);
                booster_fdptr_put (fh);
        }

out:
        return ret;
}

int
booster_fstat (int fd, void *buf)
{
        struct stat             *sbuf = (struct stat *)buf;
        int                     ret = -1;
        glusterfs_file_t        fh = NULL;

        gf_log ("booster", GF_LOG_TRACE, "fstat: fd %d", fd);
        fh = booster_fdptr_get (booster_fdtable, fd);
        if (!fh) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_fstat != NULL)
                        ret = real_fstat (fd, sbuf);
                else if (real___fxstat != NULL)
                        ret = real___fxstat (0, fd, sbuf);
                else {
                        ret = -1;
                        errno = ENOSYS;
                        goto out;
                }
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_fstat (fh, sbuf);
                booster_fdptr_put (fh);
        }

out:
        return ret;
}

int
booster_fstat64 (int fd, void *buf)
{
        int                     ret = -1;
        struct stat64           *sbuf = (struct stat64 *)buf;
        glusterfs_file_t        fh = NULL;

        gf_log ("booster", GF_LOG_TRACE, "fstat64: fd %d", fd);
        fh = booster_fdptr_get (booster_fdtable, fd);
        if (!fh) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_fstat64 != NULL)
                        ret = real_fstat64 (fd, sbuf);
                else if (real___fxstat64 != NULL)
                        /* Not sure how portable the use of 0 for
                         * version number is but it works over glibc.
                         * We need this because, I've
                         * observed that all the above real* functors can be
                         * NULL. In that case, this is our last and only option.
                         */
                        ret = real___fxstat64 (0, fd, sbuf);
                else {
                        ret = -1;
                        errno = ENOSYS;
                        goto out;
                }
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_fstat (fh, (struct stat *)sbuf);
                booster_fdptr_put (fh);
        }

out:
        return ret;
}

int
booster_lxstat (int ver, const char *path, void *buf)
{
        struct stat     *sbuf = (struct stat *)buf;
        int             ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "lxstat: path %s", path);
        ret = glusterfs_lstat (path, sbuf);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "lxstat failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "lxstat succeeded");
                goto out;
        }

        if (real___lxstat == NULL) {
                ret = -1;
                errno = ENOSYS;
                goto out;
        }

        ret = real___lxstat (ver, path, sbuf);
out:
        return ret;
}

int
booster_lxstat64 (int ver, const char *path, void *buf)
{
        int             ret = -1;
        struct stat64   *sbuf = (struct stat64 *)buf;

        gf_log ("booster", GF_LOG_TRACE, "lxstat64: path %s", path);
        ret = glusterfs_lstat (path, (struct stat *)sbuf);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "lxstat64 failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "lxstat64 succeeded");
                goto out;
        }

        if (real___lxstat64 == NULL) {
                errno = ENOSYS;
                ret = -1;
                goto out;
        }

        ret = real___lxstat64 (ver, path, sbuf);
out:
        return ret;
}

int
booster_lstat (const char *path, void *buf)
{
        struct stat     *sbuf = (struct stat *)buf;
        int             ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "lstat: path %s", path);
        ret = glusterfs_lstat (path, sbuf);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "lstat failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "lstat succeeded");
                goto out;
        }

        if (real_lstat != NULL)
                ret = real_lstat (path, sbuf);
        else if (real___lxstat != NULL)
                ret = real___lxstat (0, path, sbuf);
        else {
                errno = ENOSYS;
                ret = -1;
                goto out;
        }


out:
        return ret;
}

int
booster_lstat64 (const char *path, void *buf)
{
        int             ret = -1;
        struct stat64   *sbuf = (struct stat64 *)buf;

        gf_log ("booster", GF_LOG_TRACE, "lstat64: path %s", path);
        ret = glusterfs_lstat (path, (struct stat *)sbuf);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "lstat64 failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "lstat64 succeeded");
                goto out;
        }

        if (real_lstat64 != NULL)
                ret = real_lstat64 (path, sbuf);
        else if (real___lxstat64 != NULL)
                ret = real___lxstat64 (0, path, sbuf);
        else {
                errno = ENOSYS;
                ret = -1;
                goto out;
        }

out:
        return ret;
}

int
booster_statfs (const char *pathname, struct statfs *buf)
{
        int             ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "statfs: path %s", pathname);
        ret = glusterfs_statfs (pathname, buf);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "statfs failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "statfs succeeded");
                goto out;
        }

        if (real_statfs == NULL) {
                ret = -1;
                errno = ENOSYS;
                goto out;
        }

        ret = real_statfs (pathname, buf);

out:
        return ret;
}

int
booster_statfs64 (const char *pathname, struct statfs64 *buf)
{
        int             ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "stat64: path %s", pathname);
        ret = glusterfs_statfs (pathname, (struct statfs *)buf);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "statfs64 failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "statfs64 succeeded");
                goto out;
        }

        if (real_statfs64 == NULL) {
                ret = -1;
                errno = ENOSYS;
                goto out;
        }

        ret = real_statfs64 (pathname, buf);

out:
        return ret;
}

int
booster_statvfs (const char *pathname, struct statvfs *buf)
{
        int             ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "statvfs: path %s", pathname);
        ret = glusterfs_statvfs (pathname, buf);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "statvfs failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "statvfs succeeded");
                goto out;
        }

        if (real_statvfs == NULL) {
                ret = -1;
                errno = ENOSYS;
                goto out;
        }

        ret = real_statvfs (pathname, buf);

out:
        return ret;
}

int
booster_statvfs64 (const char *pathname, struct statvfs64 *buf)
{
        int             ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "statvfs64: path %s", pathname);
        ret = glusterfs_statvfs (pathname, (struct statvfs *)buf);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "statvfs64 failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "statvfs64 succeeded");
                goto out;
        }

        if (real_statvfs64 == NULL) {
                ret = -1;
                errno = ENOSYS;
                goto out;
        }

        ret = real_statvfs64 (pathname, buf);

out:
        return ret;
}

ssize_t
getxattr (const char *path, const char *name, void *value, size_t size)
{
        int     ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "getxattr: path %s, name %s", path,
                name);
        ret = glusterfs_getxattr (path, name, value, size);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "getxattr failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret > 0) {
                gf_log ("booster", GF_LOG_TRACE, "getxattr succeeded");
                return ret;
        }

        if (real_getxattr == NULL) {
                ret = -1;
                errno = ENOSYS;
                goto out;
        }

        ret = real_getxattr (path, name, value, size);
out:
        return ret;
}


ssize_t
lgetxattr (const char *path, const char *name, void *value, size_t size)
{
        int     ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "lgetxattr: path %s, name %s", path,
                name);
        ret = glusterfs_lgetxattr (path, name, value, size);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "lgetxattr failed: %s",
                        strerror (errno));

                goto out;
        }

        if (ret > 0) {
                gf_log ("booster", GF_LOG_TRACE, "lgetxattr succeeded");
                return ret;
        }

        if (real_lgetxattr == NULL) {
                ret = -1;
                errno = ENOSYS;
                goto out;
        }

        ret = real_lgetxattr (path, name, value, size);
out:
        return ret;
}

int
remove (const char *path)
{
        int     ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "remove: %s", path);
        ret = glusterfs_remove (path);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "remove failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "remove succeeded");
                goto out;
        }

        if (real_remove == NULL) {
                errno = ENOSYS;
                ret = -1;
                goto out;
        }

        ret = real_remove (path);

out:
        return ret;
}

int
lchown (const char *path, uid_t owner, gid_t group)
{
        int     ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "lchown: path %s", path);
        ret = glusterfs_lchown (path, owner, group);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "lchown failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_ERROR, "lchown succeeded");
                goto out;
        }

        if (real_lchown == NULL) {
                errno = ENOSYS;
                ret = -1;
                goto out;
        }

        ret = real_lchown (path, owner, group);

out:
        return ret;
}

void
booster_rewinddir (DIR *dir)
{
        struct booster_dir_handle       *bh = (struct booster_dir_handle *)dir;

        if (!bh) {
                errno = EFAULT;
                goto out;
        }

        if (bh->type == BOOSTER_GL_DIR) {
                gf_log ("booster", GF_LOG_TRACE, "rewinddir on glusterfs");
                glusterfs_rewinddir ((glusterfs_dir_t)bh->dirh);
        } else if (bh->type == BOOSTER_POSIX_DIR) {
                if (real_rewinddir == NULL) {
                        errno = ENOSYS;
                        goto out;
                }
                gf_log ("booster", GF_LOG_TRACE, "rewinddir on posix");
                real_rewinddir ((DIR *)bh->dirh);
        } else
                errno = EINVAL;
out:
        return;
}

void
booster_seekdir (DIR *dir, off_t offset)
{
        struct booster_dir_handle       *bh = (struct booster_dir_handle *)dir;

        if (!bh) {
                errno = EFAULT;
                goto out;
        }

        if (bh->type == BOOSTER_GL_DIR) {
                gf_log ("booster", GF_LOG_TRACE, "seekdir on glusterfs");
                glusterfs_seekdir ((glusterfs_dir_t)bh->dirh, offset);
         } else if (bh->type == BOOSTER_POSIX_DIR) {
                if (real_seekdir == NULL) {
                        errno = ENOSYS;
                        goto out;
                }

                gf_log ("booster", GF_LOG_TRACE, "seekdir on posix");
                real_seekdir ((DIR *)bh->dirh, offset);
        } else
                errno = EINVAL;
out:
        return;
}

off_t
booster_telldir (DIR *dir)
{
        struct booster_dir_handle       *bh = (struct booster_dir_handle *)dir;
        off_t	offset = -1;

        if (!bh) {
                errno = EFAULT;
                goto out;
        }

        if (bh->type == BOOSTER_GL_DIR) {
                gf_log ("booster", GF_LOG_TRACE, "telldir on glusterfs");
                offset = glusterfs_telldir ((glusterfs_dir_t)bh->dirh);
        } else if (bh->type == BOOSTER_POSIX_DIR) {
                if (real_telldir == NULL) {
                        errno = ENOSYS;
                        goto out;
                }

                gf_log ("booster", GF_LOG_TRACE, "telldir on posix");
                offset = real_telldir ((DIR *)bh->dirh);
        } else
                errno = EINVAL;
out:
        return offset;
}


pid_t 
fork (void)
{
	pid_t pid = 0;
	char child = 0;

	glusterfs_log_lock ();
	{
		pid = real_fork ();
	}
	glusterfs_log_unlock ();

	child = (pid == 0);
	if (child) {
		booster_cleanup ();
		booster_init ();
	}

	return pid;
}

ssize_t
sendfile (int out_fd, int in_fd, off_t *offset, size_t count)
{
        glusterfs_file_t            in_fh = NULL;
        ssize_t                     ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "sendfile: in fd %d, out fd %d, offset"
                " %"PRIu64", count %d", in_fd, out_fd, *offset, count);
        /*
         * handle sendfile in booster only if in_fd corresponds to a glusterfs
         * file handle 
         */
        in_fh = booster_fdptr_get (booster_fdtable, in_fd);
        if (!in_fh) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_sendfile == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else {
                        ret = real_sendfile (out_fd, in_fd, offset, count);
                }
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_sendfile (out_fd, in_fh, offset, count);
                booster_fdptr_put (in_fh);
        }
        
        return ret;
}

ssize_t
sendfile64 (int out_fd, int in_fd, off_t *offset, size_t count)
{
        glusterfs_file_t            in_fh = NULL;
        ssize_t                     ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "sendfile64: in fd %d, out fd %d,"
                " offset %"PRIu64", count %d", in_fd, out_fd, *offset, count);
        /*
         * handle sendfile in booster only if in_fd corresponds to a glusterfs
         * file handle 
         */
        in_fh = booster_fdptr_get (booster_fdtable, in_fd);
        if (!in_fh) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_sendfile64 == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else {
                        ret = real_sendfile64 (out_fd, in_fd, offset, count);
                }
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_sendfile (out_fd, in_fh, offset, count);
                booster_fdptr_put (in_fh);
        }
        
        return ret;
}


int
fcntl (int fd, int cmd, ...)
{
        va_list           ap;
        int               ret = -1;
        long              arg = 0;
        struct flock     *lock = NULL;
        glusterfs_file_t  glfs_fd = 0; 

        glfs_fd = booster_fdptr_get (booster_fdtable, fd);

        gf_log ("booster", GF_LOG_TRACE, "fcntl: fd %d, cmd %d", fd, cmd);
	switch (cmd) {
	case F_DUPFD:
                ret = dup (fd);
                break;
                /* 
                 * FIXME: Consider this case when implementing F_DUPFD, F_GETFD
                 *        etc flags in libglusterfsclient. Commenting it out for
                 *        timebeing since it is defined only in linux kernel 
                 *        versions >= 2.6.24.
                 */
                /* case F_DUPFD_CLOEXEC: */
	case F_GETFD:
	case F_GETFL:
	case F_GETOWN:
	case F_GETSIG:
	case F_GETLEASE:
                if (glfs_fd) {
                        gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                        ret = glusterfs_fcntl (glfs_fd, cmd);
                } else {
                        if (!real_fcntl) {
                                errno = ENOSYS;
                                goto out;
                        }

                        gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                        ret = real_fcntl (fd, cmd);
                }
		break;

	case F_SETFD:
	case F_SETFL:
	case F_SETOWN:
	case F_SETSIG:
	case F_SETLEASE:
	case F_NOTIFY:
                va_start (ap, cmd);
                arg = va_arg (ap, long);
                va_end (ap);

                if (glfs_fd) {
                        gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                        ret = glusterfs_fcntl (glfs_fd, cmd, arg);
                } else {
                        if (!real_fcntl) {
                                errno = ENOSYS;
                                goto out;
                        }

                        gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                        ret = real_fcntl (fd, cmd, arg);
                }
		break;

	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
#if F_GETLK != F_GETLK64 
        case F_GETLK64:
#endif
#if F_SETLK != F_SETLK64
        case F_SETLK64:
#endif
#if F_SETLKW != F_SETLKW64
        case F_SETLKW64:
#endif
                va_start (ap, cmd);
                lock = va_arg (ap, struct flock *);
                va_end (ap);

                if (lock == NULL) {
                        errno = EINVAL;
                        goto out;
                }

                if (glfs_fd) {
                        gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                        ret = glusterfs_fcntl (glfs_fd, cmd, lock);
                } else {
                        if (!real_fcntl) {
                                errno = ENOSYS;
                                goto out;
                        }

                        gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                        ret = real_fcntl (fd, cmd, lock);
                }
		break;

	default:
                errno = EINVAL;
		break;
	}

out:
        if (glfs_fd) {
                booster_fdptr_put (glfs_fd);
        }

        return ret;
}


int
chdir (const char *path)
{
        int     ret = -1;
        char    cwd[PATH_MAX];
        char   *res = NULL;

        gf_log ("booster", GF_LOG_TRACE, "chdir: path %s", path);

        pthread_mutex_lock (&cwdlock);
        {
                res = glusterfs_getcwd (cwd, PATH_MAX);
                if (res == NULL) {
                        gf_log ("booster", GF_LOG_ERROR, "getcwd failed: %s",
                                strerror (errno));
                        goto unlock;
                }

                ret = glusterfs_chdir (path);
                if ((ret == -1) && (errno != ENODEV)) {
                        gf_log ("booster", GF_LOG_ERROR, "chdir failed: %s",
                                strerror (errno));
                        goto unlock;
                }

                if (ret == 0) {
                        gf_log ("booster", GF_LOG_TRACE, "chdir succeeded");
                        goto unlock;
                }

                if (real_chdir == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                        goto unlock;
                }

                ret = real_chdir (path);
                if (ret == -1) {
                        glusterfs_chdir (cwd);
                }
        }
unlock:
        pthread_mutex_unlock (&cwdlock);

        return ret;
}


int
fchdir (int fd)
{
        int              ret     = -1;
        glusterfs_file_t glfs_fd = 0;
        char             cwd[PATH_MAX]; 
        char            *res     = NULL;

        glfs_fd = booster_fdptr_get (booster_fdtable, fd);

        if (!glfs_fd) {
                gf_log ("booster", GF_LOG_TRACE, "Not a booster fd");
                if (real_write == NULL) {
                        errno = ENOSYS;
                        ret = -1;
                } else {
                        ret = real_fchdir (fd);
                        if (ret == 0) {
                                res = real_getcwd (cwd, PATH_MAX);
                                if (res == NULL) {
                                        gf_log ("booster", GF_LOG_ERROR,
                                                "getcwd failed (%s)",
                                                strerror (errno));
                                        ret = -1;
                                } else {
                                        glusterfs_chdir (cwd);
                                }
                        }
                }
        } else {
                gf_log ("booster", GF_LOG_TRACE, "Is a booster fd");
                ret = glusterfs_fchdir (glfs_fd);
                booster_fdptr_put (glfs_fd);
        }
 
        return ret;
}


char *
getcwd (char *buf, size_t size)
{
        char *res = NULL;

        res = glusterfs_getcwd (buf, size);
        if ((res == NULL) && (errno == ENODEV)) {
                res = real_getcwd (buf, size);
        }

        return res;
}


int __REDIRECT (booster_false_truncate, (const char *path, off_t length),
                truncate) __nonnull ((1));
int __REDIRECT (booster_false_truncate64, (const char *path, loff_t length),
                truncate64) __nonnull ((1));;

int
booster_false_truncate (const char *path, off_t length)
{
        int             ret = -1;

        gf_log ("booster", GF_LOG_TRACE, "truncate: path (%s) length (%"PRIu64
                ")", path, length);

        ret = glusterfs_truncate (path, length);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "truncate failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "truncate succeeded");
                goto out;
        }

        if (real_truncate != NULL)
                ret = real_truncate (path, length);
        else {
                errno = ENOSYS;
                ret = -1;
                goto out;
        }

out:
        return ret;
}


int
booster_false_truncate64 (const char *path, loff_t length)
{
        int             ret = -1;
  
        gf_log ("booster", GF_LOG_TRACE, "truncate64: path (%s) length "
                "(%"PRIu64")", path, length);

        ret = glusterfs_truncate (path, length);
        if ((ret == -1) && (errno != ENODEV)) {
                gf_log ("booster", GF_LOG_ERROR, "truncate64 failed: %s",
                        strerror (errno));
                goto out;
        }

        if (ret == 0) {
                gf_log ("booster", GF_LOG_TRACE, "truncate64 succeeded");
                goto out;
        }

        if (real_truncate64 != NULL)
                ret = real_truncate64 (path, length);
        else {
                errno = ENOSYS;
                ret = -1;
                goto out;
        }

out:
        return ret;
}


void
booster_lib_init (void)
{

        RESOLVE (open);
        RESOLVE (open64);
        RESOLVE (creat);
        RESOLVE (creat64);

        RESOLVE (read);
        RESOLVE (readv);
        RESOLVE (pread);
        RESOLVE (pread64);

        RESOLVE (write);
        RESOLVE (writev);
        RESOLVE (pwrite);
        RESOLVE (pwrite64);

        RESOLVE (lseek);
        RESOLVE (lseek64);

        RESOLVE (close);

        RESOLVE (dup);
        RESOLVE (dup2);

	RESOLVE (fork); 
        RESOLVE (mkdir);
        RESOLVE (rmdir);
        RESOLVE (chmod);
        RESOLVE (chown);
        RESOLVE (fchmod);
        RESOLVE (fchown);
        RESOLVE (fsync);
        RESOLVE (ftruncate);
        RESOLVE (ftruncate64);
        RESOLVE (link);
        RESOLVE (rename);
        RESOLVE (utimes);
        RESOLVE (utime);
        RESOLVE (mknod);
        RESOLVE (mkfifo);
        RESOLVE (unlink);
        RESOLVE (symlink);
        RESOLVE (readlink);
        RESOLVE (realpath);
        RESOLVE (opendir);
        RESOLVE (readdir);
        RESOLVE (readdir64);
        RESOLVE (closedir);
        RESOLVE (__xstat);
        RESOLVE (__xstat64);
        RESOLVE (stat);
        RESOLVE (stat64);
        RESOLVE (__fxstat);
        RESOLVE (__fxstat64);
        RESOLVE (fstat);
        RESOLVE (fstat64);
        RESOLVE (__lxstat);
        RESOLVE (__lxstat64);
        RESOLVE (lstat);
        RESOLVE (lstat64);
        RESOLVE (statfs);
        RESOLVE (statfs64);
        RESOLVE (statvfs);
        RESOLVE (statvfs64);
        RESOLVE (getxattr);
        RESOLVE (lgetxattr);
        RESOLVE (remove);
        RESOLVE (lchown);
	RESOLVE (rewinddir);
	RESOLVE (seekdir);
	RESOLVE (telldir);
        RESOLVE (sendfile);
        RESOLVE (sendfile64);
        RESOLVE (readdir_r);
        RESOLVE (readdir64_r);
        RESOLVE (fcntl);
        RESOLVE (chdir);
        RESOLVE (fchdir);
        RESOLVE (getcwd);
        RESOLVE (truncate);
        RESOLVE (truncate64);

        /* This must be called after resolving real functions
         * above so that the socket based IO calls in libglusterfsclient
         * can fall back to a non-NULL real_XXX function pointer.
         * Calling booster_init before resolving the names above
         * results in seg-faults because the function symbols above are NULL.
         */
	booster_init ();
}

