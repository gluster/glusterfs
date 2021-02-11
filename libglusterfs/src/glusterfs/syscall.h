/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include <dirent.h>
#include <sys/uio.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <stdio.h>

/* GF follows the Linux XATTR definition, which differs in Darwin. */
#define GF_XATTR_CREATE 0x1  /* set value, fail if attr already exists */
#define GF_XATTR_REPLACE 0x2 /* set value, fail if attr does not exist */

/* Linux kernel version 2.6.x don't have these defined
   define if not defined */

#ifndef XATTR_SECURITY_PREFIX
#define XATTR_SECURITY_PREFIX "security."
#define XATTR_SECURITY_PREFIX_LEN (sizeof(XATTR_SECURITY_PREFIX) - 1)
#endif

#ifndef XATTR_SYSTEM_PREFIX
#define XATTR_SYSTEM_PREFIX "system."
#define XATTR_SYSTEM_PREFIX_LEN (sizeof(XATTR_SYSTEM_PREFIX) - 1)
#endif

#ifndef XATTR_TRUSTED_PREFIX
#define XATTR_TRUSTED_PREFIX "trusted."
#define XATTR_TRUSTED_PREFIX_LEN (sizeof(XATTR_TRUSTED_PREFIX) - 1)
#endif

#ifndef XATTR_USER_PREFIX
#define XATTR_USER_PREFIX "user."
#define XATTR_USER_PREFIX_LEN (sizeof(XATTR_USER_PREFIX) - 1)
#endif

#if defined(GF_DARWIN_HOST_OS)
#include <sys/xattr.h>
#define XATTR_DARWIN_NOSECURITY XATTR_NOSECURITY
#define XATTR_DARWIN_NODEFAULT XATTR_NODEFAULT
#define XATTR_DARWIN_SHOWCOMPRESSION XATTR_SHOWCOMPRESSION
#endif

int
sys_lstat(const char *path, struct stat *buf);

int
sys_stat(const char *path, struct stat *buf);

int
sys_fstat(int fd, struct stat *buf);

int
sys_fstatat(int dirfd, const char *pathname, struct stat *buf, int flags);

int
sys_open(const char *pathname, int flags, int mode);

int
sys_openat(int dirfd, const char *pathname, int flags, int mode);

DIR *
sys_opendir(const char *name);

struct dirent *
sys_readdir(DIR *dir, struct dirent *de);

ssize_t
sys_readlink(const char *path, char *buf, size_t bufsiz);

int
sys_closedir(DIR *dir);

int
sys_mknod(const char *pathname, mode_t mode, dev_t dev);

int
sys_mkdir(const char *pathname, mode_t mode);

int
sys_mkdirat(int dirfd, const char *pathname, mode_t mode);

int
sys_unlink(const char *pathname);

int
sys_unlinkat(int dfd, const char *pathname);

int
sys_rmdir(const char *pathname);

int
sys_symlink(const char *oldpath, const char *newpath);

int
sys_symlinkat(const char *oldpath, int dirfd, const char *newpath);

int
sys_rename(const char *oldpath, const char *newpath);

int
sys_link(const char *oldpath, const char *newpath);

int
sys_linkat(int oldfd, const char *oldpath, int newfd, const char *newpath);

int
sys_chmod(const char *path, mode_t mode);

int
sys_fchmod(int fd, mode_t mode);

int
sys_lchmod(const char *path, mode_t mode);

int
sys_chown(const char *path, uid_t owner, gid_t group);

int
sys_fchown(int fd, uid_t owner, gid_t group);

int
sys_lchown(const char *path, uid_t owner, gid_t group);

int
sys_truncate(const char *path, off_t length);

int
sys_ftruncate(int fd, off_t length);

int
sys_utimes(const char *filename, const struct timeval times[2]);

#if defined(HAVE_UTIMENSAT)
int
sys_utimensat(int dirfd, const char *filename, const struct timespec times[2],
              int flags);
#endif

int
sys_futimes(int fd, const struct timeval times[2]);

int
sys_creat(const char *pathname, mode_t mode);

ssize_t
sys_readv(int fd, const struct iovec *iov, int iovcnt);

ssize_t
sys_writev(int fd, const struct iovec *iov, int iovcnt);

ssize_t
sys_read(int fd, void *buf, size_t count);

ssize_t
sys_write(int fd, const void *buf, size_t count);

off_t
sys_lseek(int fd, off_t offset, int whence);

int
sys_statvfs(const char *path, struct statvfs *buf);

int
sys_fstatvfs(int fd, struct statvfs *buf);

int
sys_close(int fd);

int
sys_fsync(int fd);

int
sys_fdatasync(int fd);

void
gf_add_prefix(const char *ns, const char *key, char **newkey);

void
gf_remove_prefix(const char *ns, const char *key, char **newkey);

int
sys_lsetxattr(const char *path, const char *name, const void *value,
              size_t size, int flags);

ssize_t
sys_llistxattr(const char *path, char *list, size_t size);

ssize_t
sys_lgetxattr(const char *path, const char *name, void *value, size_t size);

ssize_t
sys_fgetxattr(int filedes, const char *name, void *value, size_t size);

int
sys_fsetxattr(int filedes, const char *name, const void *value, size_t size,
              int flags);

ssize_t
sys_flistxattr(int filedes, char *list, size_t size);

int
sys_lremovexattr(const char *path, const char *name);

int
sys_fremovexattr(int filedes, const char *name);

int
sys_access(const char *pathname, int mode);

int
sys_fallocate(int fd, int mode, off_t offset, off_t len);

ssize_t
sys_preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset);

ssize_t
sys_pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset);

ssize_t
sys_pread(int fd, void *buf, size_t count, off_t offset);

ssize_t
sys_pwrite(int fd, const void *buf, size_t count, off_t offset);

int
sys_socket(int domain, int type, int protocol);

int
sys_accept(int sock, struct sockaddr *sockaddr, socklen_t *socklen, int flags);

#ifdef GF_BSD_HOST_OS
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

/*
 * According to the man page of copy_file_range, both off_in and off_out are
 * pointers to the data type loff_t (i.e. loff_t *). But, freebsd does not
 * have (and recognize) loff_t. Since loff_t is 64 bits, use off64_t
 * instead.  Since it's a pointer type it should be okay. It just needs
 * to be a pointer-to-64-bit pointer for both 32- and 64-bit platforms.
 * off64_t is recognized by freebsd.
 * TODO: In future, when freebsd can recognize loff_t, probably revisit this
 *       and change the off_in and off_out to (loff_t *).
 */
ssize_t
sys_copy_file_range(int fd_in, off64_t *off_in, int fd_out, off64_t *off_out,
                    size_t len, unsigned int flags);

int
sys_kill(pid_t pid, int sig);

#ifdef __FreeBSD__
int
sys_sysctl(const int *name, u_int namelen, void *oldp, size_t *oldlenp,
           const void *newp, size_t newlen);
#endif

#endif /* __SYSCALL_H__ */
