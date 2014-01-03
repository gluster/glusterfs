/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "compat.h"
#include "syscall.h"

#include <sys/types.h>
#include <utime.h>
#include <sys/time.h>

int
sys_lstat (const char *path, struct stat *buf)
{
        return lstat (path, buf);
}


int
sys_stat (const char *path, struct stat *buf)
{
        return stat (path, buf);
}


int
sys_fstat (int fd, struct stat *buf)
{
        return fstat (fd, buf);
}


DIR *
sys_opendir (const char *name)
{
        return opendir (name);
}


struct dirent *
sys_readdir (DIR *dir)
{
        return readdir (dir);
}


ssize_t
sys_readlink (const char *path, char *buf, size_t bufsiz)
{
        return readlink (path, buf, bufsiz);
}


int
sys_closedir (DIR *dir)
{
        return closedir (dir);
}


int
sys_mknod (const char *pathname, mode_t mode, dev_t dev)
{
        return mknod (pathname, mode, dev);
}


int
sys_mkdir (const char *pathname, mode_t mode)
{
        return mkdir (pathname, mode);
}


int
sys_unlink (const char *pathname)
{
#ifdef GF_SOLARIS_HOST_OS
        return solaris_unlink (pathname);
#endif
        return unlink (pathname);
}


int
sys_rmdir (const char *pathname)
{
        return rmdir (pathname);
}


int
sys_symlink (const char *oldpath, const char *newpath)
{
        return symlink (oldpath, newpath);
}


int
sys_rename (const char *oldpath, const char *newpath)
{
#ifdef GF_SOLARIS_HOST_OS
        return solaris_rename (oldpath, newpath);
#endif
        return rename (oldpath, newpath);
}


int
sys_link (const char *oldpath, const char *newpath)
{
#ifdef HAVE_LINKAT
	/*
	 * On most systems (Linux being the notable exception), link(2)
	 * first resolves symlinks. If the target is a directory or
	 * is nonexistent, it will fail. linkat(2) operates on the
	 * symlink instead of its target when the AT_SYMLINK_FOLLOW
	 * flag is not supplied.
	 */
        return linkat (AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
#else
        return link (oldpath, newpath);
#endif
}


int
sys_chmod (const char *path, mode_t mode)
{
        return chmod (path, mode);
}


int
sys_fchmod (int fd, mode_t mode)
{
        return fchmod (fd, mode);
}


int
sys_chown (const char *path, uid_t owner, gid_t group)
{
        return chown (path, owner, group);
}


int
sys_fchown (int fd, uid_t owner, gid_t group)
{
        return fchown (fd, owner, group);
}


int
sys_lchown (const char *path, uid_t owner, gid_t group)
{
        return lchown (path, owner, group);
}


int
sys_truncate (const char *path, off_t length)
{
        return truncate (path, length);
}


int
sys_ftruncate (int fd, off_t length)
{
        return ftruncate (fd, length);
}


int
sys_utimes (const char *filename, const struct timeval times[2])
{
        return utimes (filename, times);
}


int
sys_creat (const char *pathname, mode_t mode)
{
        return creat (pathname, mode);
}


ssize_t
sys_readv (int fd, const struct iovec *iov, int iovcnt)
{
        return readv (fd, iov, iovcnt);
}


ssize_t
sys_writev (int fd, const struct iovec *iov, int iovcnt)
{
        return writev (fd, iov, iovcnt);
}


ssize_t
sys_read (int fd, void *buf, size_t count)
{
        return read (fd, buf, count);
}


ssize_t
sys_write (int fd, const void *buf, size_t count)
{
        return write (fd, buf, count);
}


off_t
sys_lseek (int fd, off_t offset, int whence)
{
        return lseek (fd, offset, whence);
}


int
sys_statvfs (const char *path, struct statvfs *buf)
{
        return statvfs (path, buf);
}


int
sys_close (int fd)
{
        int ret = -1;

        if (fd >= 0)
                ret = close (fd);

        return ret;
}


int
sys_fsync (int fd)
{
        return fsync (fd);
}


int
sys_fdatasync (int fd)
{
#ifdef HAVE_FDATASYNC
        return fdatasync (fd);
#else
        return 0;
#endif
}


int
sys_lsetxattr (const char *path, const char *name, const void *value,
               size_t size, int flags)
{

#if defined(GF_LINUX_HOST_OS) || defined(__NetBSD__)
        return lsetxattr (path, name, value, size, flags);
#endif

#ifdef GF_BSD_HOST_OS
        return extattr_set_link (path, EXTATTR_NAMESPACE_USER,
                                 name, value, size);
#endif

#ifdef GF_SOLARIS_HOST_OS
        return solaris_setxattr (path, name, value, size, flags);
#endif

#ifdef GF_DARWIN_HOST_OS
        return setxattr (path, name, value, size, 0,
                         flags|XATTR_NOFOLLOW);
#endif

}


ssize_t
sys_llistxattr (const char *path, char *list, size_t size)
{

#if defined(GF_LINUX_HOST_OS) || defined(__NetBSD__)
        return llistxattr (path, list, size);
#endif

#ifdef GF_BSD_HOST_OS
        return extattr_list_link (path, EXTATTR_NAMESPACE_USER, list, size);
#endif

#ifdef GF_SOLARIS_HOST_OS
        return solaris_listxattr (path, list, size);
#endif

#ifdef GF_DARWIN_HOST_OS
        return listxattr (path, list, size, XATTR_NOFOLLOW);
#endif

}


ssize_t
sys_lgetxattr (const char *path, const char *name, void *value, size_t size)
{

#if defined(GF_LINUX_HOST_OS) || defined(__NetBSD__)
        return lgetxattr (path, name, value, size);
#endif

#ifdef GF_BSD_HOST_OS
        return extattr_get_link (path, EXTATTR_NAMESPACE_USER, name, value,
                                 size);
#endif

#ifdef GF_SOLARIS_HOST_OS
        return solaris_getxattr (path, name, value, size);
#endif

#ifdef GF_DARWIN_HOST_OS
        return getxattr (path, name, value, size, 0, XATTR_NOFOLLOW);
#endif

}


ssize_t
sys_fgetxattr (int filedes, const char *name, void *value, size_t size)
{

#if defined(GF_LINUX_HOST_OS) || defined(__NetBSD__)
        return fgetxattr (filedes, name, value, size);
#endif

#ifdef GF_BSD_HOST_OS
        return extattr_get_fd (filedes, EXTATTR_NAMESPACE_USER, name,
                               value, size);
#endif

#ifdef GF_SOLARIS_HOST_OS
        return solaris_fgetxattr (filedes, name, value, size);
#endif

#ifdef GF_DARWIN_HOST_OS
        return fgetxattr (filedes, name, value, size, 0, 0);
#endif

}


int
sys_fremovexattr (int filedes, const char *name)
{

#if defined(GF_LINUX_HOST_OS) || defined(__NetBSD__)
        return fremovexattr (filedes, name);
#endif

        errno = ENOSYS;
        return -1;
#if 0 /* TODO: to port to other OSes, fill in each of below */
#ifdef GF_BSD_HOST_OS
        return extattr_remove_fd (filedes, EXTATTR_NAMESPACE_USER, name);
#endif

#ifdef GF_SOLARIS_HOST_OS
        return solaris_fremovexattr (filedes, name);
#endif

#ifdef GF_DARWIN_HOST_OS
        return fremovexattr (filedes, name, 0);
#endif
#endif
}


int
sys_fsetxattr (int filedes, const char *name, const void *value,
               size_t size, int flags)
{

#if defined(GF_LINUX_HOST_OS) || defined(__NetBSD__)
        return fsetxattr (filedes, name, value, size, flags);
#endif

#ifdef GF_BSD_HOST_OS
        return extattr_set_fd (filedes, EXTATTR_NAMESPACE_USER, name,
                               value, size);
#endif

#ifdef GF_SOLARIS_HOST_OS
        return solaris_fsetxattr (filedes, name, value, size, flags);
#endif

#ifdef GF_DARWIN_HOST_OS
        return fsetxattr (filedes, name, value, size, 0, flags);
#endif

}


ssize_t
sys_flistxattr (int filedes, char *list, size_t size)
{

#if defined(GF_LINUX_HOST_OS) || defined(__NetBSD__)
        return flistxattr (filedes, list, size);
#endif

#ifdef GF_BSD_HOST_OS
        return extattr_list_fd (filedes, EXTATTR_NAMESPACE_USER, list, size);
#endif

#ifdef GF_SOLARIS_HOST_OS
        return solaris_flistxattr (filedes, list, size);
#endif

#ifdef GF_DARWIN_HOST_OS
        return flistxattr (filedes, list, size, XATTR_NOFOLLOW);
#endif

}


int
sys_lremovexattr (const char *path, const char *name)
{

#if defined(GF_LINUX_HOST_OS) || defined(__NetBSD__)
        return lremovexattr (path, name);
#endif

#ifdef GF_BSD_HOST_OS
        return extattr_delete_link (path, EXTATTR_NAMESPACE_USER, name);
#endif

#ifdef GF_SOLARIS_HOST_OS
        return solaris_removexattr (path, name);
#endif

#ifdef GF_DARWIN_HOST_OS
        return removexattr (path, name, XATTR_NOFOLLOW);
#endif

}


int
sys_access (const char *pathname, int mode)
{
        return access (pathname, mode);
}


int
sys_fallocate(int fd, int mode, off_t offset, off_t len)
{
#ifdef HAVE_FALLOCATE
	return fallocate(fd, mode, offset, len);
#endif

#ifdef HAVE_POSIX_FALLOCATE
	if (mode) {
		/* keep size not supported */
		errno = EOPNOTSUPP;
		return -1;
	}

	return posix_fallocate(fd, offset, len);
#endif

	errno = ENOSYS;
	return -1;
}

