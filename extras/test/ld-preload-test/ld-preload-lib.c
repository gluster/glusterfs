/*
   Copyright (c) 2007-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

/* LD PRELOAD'able library
 * A very simple library that intercepts booster supported system calls
 * and prints a log message to stdout.
 *
 * Combined with the ld-preload-test, we cam determine whether all system calls
 * are getting redirected into this library when LD_PRELOAD'ed. This helps us
 * conduct a basic test to ensure that the required syscalls actually will
 * be intercepted by the booster library.
 */

#include <dlfcn.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <utime.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <attr/xattr.h>
#include <sys/sendfile.h>

/* Err number that is assigned to errno so that test application can
 * verify that the function was intercepted correctly.
 */
#define PRELOAD_ERRNO_VERF      6449
#define set_errno()             (errno = PRELOAD_ERRNO_VERF)

void
intercept (char *call, int tabs)
{
        while (tabs > 0) {
                fprintf (stdout, "\t");
                --tabs;
        }

        fprintf (stdout, "Intercepted by %s", call);
}

int
creat64 (const char *pathname, mode_t mode)
{
        intercept ("creat64", 2);
        set_errno ();
        return -1;
}

int
creat (const char *pathname, mode_t mode)
{
        intercept ("creat", 2);
        set_errno ();
        return -1;
}


int
close (int fd)
{
        intercept ("close", 2);
        set_errno ();
        return -1;
}

int
open64 (const char *pathname, int flags, ...)
{
        intercept ("open64", 2);
        set_errno ();
        return -1;
}


int
open (const char *pathname, int flags, ...)
{
        intercept ("open", 2);
        set_errno ();
        return -1;
}

ssize_t
read (int fd, void *buf, size_t count)
{
        intercept ("read", 2);
        set_errno ();
        return -1;
}

ssize_t
readv (int fd, const struct iovec *vector, int count)
{
        intercept ("readv", 2);
        set_errno ();
        return -1;
}

ssize_t
pread (int fd, void *buf, size_t count, unsigned long offset)
{
        intercept ("pread", 2);
        set_errno ();
        return -1;
}


ssize_t
pread64 (int fd, void *buf, size_t count, uint64_t offset)
{
        intercept ("pread64", 2);
        set_errno ();
        return -1;
}

ssize_t
write (int fd, const void *buf, size_t count)
{
        intercept ("write", 2);
        set_errno ();
        return -1;
}

ssize_t
writev (int fd, const struct iovec *vector, int count)
{
        intercept ("writev", 2);
        set_errno ();
        return -1;
}

ssize_t
pwrite (int fd, const void *buf, size_t count, unsigned long offset)
{
        intercept ("pwrite", 2);
        set_errno ();
        return -1;
}

ssize_t
pwrite64 (int fd, const void *buf, size_t count, uint64_t offset)
{
        intercept ("pwrite64", 2);
        set_errno ();
        return -1;
}


off_t
lseek (int fildes, unsigned long offset, int whence)
{
        intercept ("lseek", 2);
        set_errno ();
        return -1;
}

off_t
lseek64 (int fildes, uint64_t offset, int whence)
{
        intercept ("lseek64", 2);
        set_errno ();
        return -1;
}


int
dup (int fd)
{
        intercept ("dup", 2);
        set_errno ();
        return -1;
}

int
dup2 (int oldfd, int newfd)
{
        intercept ("dup2", 2);
        set_errno ();
        return -1;
}

int
mkdir (const char *pathname, mode_t mode)
{
        intercept ("mkdir", 2);
        set_errno ();
        return -1;
}

int
rmdir (const char *pathname)
{
        intercept ("rmdir", 2);
        set_errno ();
        return -1;
}

int
chmod (const char *pathname, mode_t mode)
{
        intercept ("chmod", 2);
        set_errno ();
        return -1;
}

int
chown (const char *pathname, uid_t owner, gid_t group)
{
        intercept ("chown", 2);
        set_errno ();
        return -1;
}

int
fchmod (int fd, mode_t mode)
{
        intercept ("fchmod", 2);
        set_errno ();
        return -1;
}

int
fchown (int fd, uid_t uid, gid_t gid)
{
        intercept ("fchown", 2);
        set_errno ();
        return -1;
}

int fsync (int fd)
{
        intercept ("fsync", 2);
        set_errno ();
        return -1;
}


int
ftruncate (int fd, off_t length)
{
        intercept ("ftruncate", 1);
        set_errno ();
        return -1;
}


int
ftruncate64 (int fd, off_t length)
{
        intercept ("ftruncate64", 1);
        set_errno ();
        return -1;
}

int
link (const char *oldpath, const char *newname)
{
        intercept ("link", 2);
        set_errno ();
        return -1;
}

int
rename (const char *oldpath, const char *newpath)
{
        intercept ("rename", 2);
        set_errno ();
        return -1;
}

int
utimes (const char *path, const struct timeval times[2])
{
        intercept ("utimes", 2);
        set_errno ();
        return -1;
}

int
futimes (int fd, const struct timeval times[2])
{
        intercept ("futimes", 2);
        set_errno ();
        return -1;
}

int
utime (const char *path, const struct utimbuf *buf)
{
        intercept ("utime", 2);
        set_errno ();
        return -1;
}


int
mknod (const char *path, mode_t mode, dev_t dev)
{
        intercept ("mknod", 2);
        set_errno ();
        return -1;
}

int
__xmknod (int ver, const char *path, mode_t mode, dev_t *dev)
{
        intercept ("__xmknod", 2);
        set_errno ();
        return -1;
}

int
mkfifo (const char *path, mode_t mode)
{
        intercept ("mkfifo", 2);
        set_errno ();
        return -1;
}

int
unlink (const char *path)
{
        intercept ("unlink", 2);
        set_errno ();
        return -1;
}


int
symlink (const char *oldpath, const char *newpath)
{
        intercept ("symlink", 2);
        set_errno ();
        return -1;
}

int
readlink (const char *path, char *buf, size_t bufsize)
{
        intercept ("readlink", 1);
        set_errno ();
        return -1;
}


char *
realpath (const char *path, char *resolved)
{
        intercept ("realpath", 1);
        set_errno ();
        return NULL;
}


DIR *
opendir (const char *path)
{
        intercept ("opendir", 2);
        set_errno ();
        return NULL;
}


struct dirent *
readdir (DIR *dir)
{
        intercept ("readdir\t", 2);
        set_errno ();
        return NULL;
}

struct dirent *
readdir64 (DIR *dir)
{
        intercept ("readdir64", 2);
        set_errno ();
        return NULL;
}


int
readdir_r (DIR *dir, struct dirent *entry, struct dirent **result)
{
        intercept ("readdir_r", 1);
        set_errno ();
        return -1;
}

int
readdir64_r (DIR *dir, struct dirent *entry, struct dirent **result)
{
        intercept ("readdir64_r", 1);
        set_errno ();
        return -1;
}


int
closedir (DIR *dh)
{
        intercept ("closedir", 1);
        set_errno ();
        return -1;
}

int
__xstat (int ver, const char *path, struct stat *buf)
{
        intercept ("__xstat\t", 2);
        set_errno ();
        return -1;
}


int
__xstat64 (int ver, const char *path, struct stat *buf)
{
        intercept ("__xstat64", 2);
        set_errno ();
        return -1;
}

int
stat (const char *path, struct stat *buf)
{
        intercept ("stat", 2);
        set_errno ();
        return -1;
}

int
stat64 (const char *path, struct stat *buf)
{
        intercept ("stat64", 2);
        set_errno ();
        return -1;
}

int
__fxstat (int ver, int fd, struct stat *buf)
{
        intercept ("__fxstat\t", 2);
        set_errno ();
        return -1;
}


int
__fxstat64 (int ver, int fd, struct stat *buf)
{
        intercept ("__fxstat64", 2);
        set_errno ();
        return -1;
}

int
fstat (int fd, struct stat *buf)
{
        intercept ("fstat", 2);
        set_errno ();
        return -1;
}

int
fstat64 (int fd , struct stat *buf)
{
        intercept ("fstat64", 2);
        set_errno ();
        return -1;
}

int
__lxstat (int ver, const char *path, struct stat *buf)
{
        intercept ("__lxstat\t", 2);
        set_errno ();
        return -1;
}

int
__lxstat64 (int ver, const char *path, struct stat *buf)
{
        intercept ("__lxstat64", 2);
        set_errno ();
        return -1;
}

int
lstat (const char *path, struct stat *buf)
{
        intercept ("lstat", 2);
        set_errno ();
        return -1;
}

int
lstat64 (const char *path, struct stat *buf)
{
        intercept ("lstat64", 2);
        set_errno ();
        return -1;
}

int
statfs (const char *path, struct statfs *buf)
{
        intercept ("statfs", 2);
        set_errno ();
        return -1;
}


int
statfs64 (const char *path, struct statfs *buf)
{
        intercept ("statfs64", 2);
        set_errno ();
        return -1;
}

int
statvfs (const char *path, struct statvfs *buf)
{
        intercept ("statvfs\t", 2);
        set_errno ();
        return -1;
}


int
statvfs64 (const char *path, struct statvfs *buf)
{
        intercept ("statvfs64", 2);
        set_errno ();
        return -1;
}

ssize_t
getxattr (const char *path, const char *name, void *value, size_t size)
{
        intercept ("getxattr", 1);
        set_errno ();
        return -1;
}

ssize_t
lgetxattr (const char *path, const char *name, void *value, size_t size)
{
        intercept ("lgetxattr", 1);
        set_errno ();
        return -1;
}


int
remove (const char* path)
{
        intercept ("remove", 2);
        set_errno ();
        return -1;
}

int
lchown (const char *path, uid_t owner, gid_t group)
{
        intercept ("lchown", 2);
        set_errno ();
        return -1;
}

void
rewinddir (DIR *dirp)
{
        intercept ("rewinddir", 1);
        set_errno ();
        return;
}

void
seekdir (DIR *dirp, off_t offset)
{
        intercept ("seekdir", 2);
        set_errno ();
        return;
}

off_t
telldir (DIR *dirp)
{
        intercept ("telldir", 2);
        set_errno ();
        return -1;
}

ssize_t
sendfile (int out_fd, int in_fd, off_t *offset, size_t count)
{
        intercept ("sendfile\t", 1);
        set_errno ();
        return -1;
}

ssize_t
sendfile64 (int out_fd, int in_fd, off_t *offset, size_t count)
{
        intercept ("sendfile64", 1);
        set_errno ();
        return -1;
}


int
fcntl (int fd, int cmd, ...)
{
        intercept ("fcntl", 2);
        set_errno ();
        return -1;
}

