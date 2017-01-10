/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "syscall.h"
#include "compat.h"
#include "mem-pool.h"

#include <sys/types.h>
#include <utime.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

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


int
sys_fstatat(int dirfd, const char *pathname, struct stat *buf, int flags)
{
#ifdef GF_DARWIN_HOST_OS
        if (fchdir(dirfd) < 0)
                return -1;
        if(flags & AT_SYMLINK_NOFOLLOW)
                return lstat(pathname, buf);
        else
                return stat(pathname, buf);
#else
        return fstatat (dirfd, pathname, buf, flags);
#endif
}


int
sys_openat(int dirfd, const char *pathname, int flags, int mode)
{
        int fd;

#ifdef GF_DARWIN_HOST_OS
        if (fchdir(dirfd) < 0)
                return -1;
        fd = open (pathname, flags, mode);
        /* TODO: Shouldn't we restore the old current directory */
#else /* GF_DARWIN_HOST_OS */
        fd = openat (dirfd, pathname, flags, mode);
#ifdef __FreeBSD__
        /* On FreeBSD S_ISVTX flag is ignored for an open() with O_CREAT set.
         * We need to force the flag using fchmod(). */
        if ((fd >= 0) &&
            ((flags & O_CREAT) != 0) && ((mode & S_ISVTX) != 0)) {
                sys_fchmod(fd, mode);
                /* TODO: It's unlikely that fchmod could fail here. However,
                         if it fails we cannot always restore the old state
                         (if the file existed, we cannot recover it). We would
                         need many more system calls to correctly handle all
                         possible cases and it doesn't worth it. For now we
                         simply ignore the error. */
        }
#endif /* __FreeBSD__ */
#endif /* !GF_DARWIN_HOST_OS */

        return fd;
}


int
sys_open(const char *pathname, int flags, int mode)
{
        return sys_openat(AT_FDCWD, pathname, flags, mode);
}


DIR *
sys_opendir (const char *name)
{
        return opendir (name);
}

int sys_mkdirat(int dirfd, const char *pathname, mode_t mode)
{
#ifdef GF_DARWIN_HOST_OS
        if(fchdir(dirfd) < 0)
                return -1;
        return mkdir(pathname, mode);
#else
        return mkdirat (dirfd, pathname, mode);
#endif
}

struct dirent *
sys_readdir (DIR *dir, struct dirent *de)
{
#if !defined(__GLIBC__)
        /*
         * World+Dog says glibc's readdir(3) is MT-SAFE as long as
         * two threads are not accessing the same DIR; there's a
         * potential buffer overflow in glibc's readdir_r(3); and
         * glibc's readdir_r(3) is deprecated after version 2.22
         * with presumed eventual removal.
         * Given all that, World+Dog says everyone should just use
         * readdir(3). But it's unknown, unclear whether the same
         * is also true for *BSD, MacOS, and, etc.
        */
        struct dirent *entry = NULL;

        (void) readdir_r (dir, de, &entry);
        return entry;
#else
        return readdir (dir);
#endif
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
        return sys_open(pathname, O_CREAT | O_TRUNC | O_WRONLY, mode);
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


ssize_t
sys_preadv (int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
        return preadv (fd, iov, iovcnt, offset);
}


ssize_t
sys_pwritev (int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
        return pwritev (fd, iov, iovcnt, offset);
}


ssize_t
sys_pread (int fd, void *buf, size_t count, off_t offset)
{
        return pread (fd, buf, count, offset);
}


ssize_t
sys_pwrite (int fd, const void *buf, size_t count, off_t offset)
{
        return pwrite (fd, buf, count, offset);
}


off_t
sys_lseek (int fd, off_t offset, int whence)
{
        return lseek (fd, offset, whence);
}


int
sys_statvfs (const char *path, struct statvfs *buf)
{
        int ret;

        ret = statvfs (path, buf);
#ifdef __FreeBSD__
        /* FreeBSD doesn't return the expected vaule in buf->f_bsize. It
         * contains the optimal I/O size instead of the file system block
         * size. Gluster expects that this field contains the block size.
         */
        if (ret == 0) {
                buf->f_bsize = buf->f_frsize;
        }
#endif /* __FreeBSD__ */

        return ret;
}


int
sys_fstatvfs (int fd, struct statvfs *buf)
{
        int ret;

        ret = fstatvfs (fd, buf);
#ifdef __FreeBSD__
        /* FreeBSD doesn't return the expected vaule in buf->f_bsize. It
         * contains the optimal I/O size instead of the file system block
         * size. Gluster expects this field to contain the block size.
         */
        if (ret == 0) {
                buf->f_bsize = buf->f_frsize;
        }
#endif /* __FreeBSD__ */

        return ret;
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
#ifdef GF_DARWIN_HOST_OS
        return fcntl (fd, F_FULLFSYNC);
#elif __FreeBSD__
	return fsync (fd);
#else
        return fdatasync (fd);
#endif
}

void
gf_add_prefix(const char *ns, const char *key, char **newkey)
{
        /* if we dont have any namespace, append USER NS */
        if (strncmp(key, XATTR_USER_PREFIX,     XATTR_USER_PREFIX_LEN) &&
            strncmp(key, XATTR_TRUSTED_PREFIX,  XATTR_TRUSTED_PREFIX_LEN) &&
            strncmp(key, XATTR_SECURITY_PREFIX, XATTR_SECURITY_PREFIX_LEN) &&
            strncmp(key, XATTR_SYSTEM_PREFIX,   XATTR_SYSTEM_PREFIX_LEN)) {
                int ns_length =  strlen(ns);
                *newkey = GF_MALLOC(ns_length + strlen(key) + 10,
                                    gf_common_mt_char);
                strcpy(*newkey, ns);
                strcat(*newkey, key);
        } else {
                *newkey = gf_strdup(key);
        }
}

void
gf_remove_prefix(const char *ns, const char *key, char **newkey)
{
        int ns_length =  strlen(ns);
        if (strncmp(key, ns, ns_length) == 0) {
                *newkey = GF_MALLOC(-ns_length + strlen(key) + 10,
                                    gf_common_mt_char);
                strcpy(*newkey, key + ns_length);
        } else {
                *newkey = gf_strdup(key);
        }
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
        /* OS X clients will carry other flags, which will be used on a
           OS X host, but masked out on others. GF assume NOFOLLOW on Linux,
           enforcing  */
        return setxattr (path, name, value, size, 0,
                         (flags & ~XATTR_NOSECURITY) | XATTR_NOFOLLOW);
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

#ifdef GF_BSD_HOST_OS
        return extattr_delete_fd (filedes, EXTATTR_NAMESPACE_USER, name);
#endif

#ifdef GF_SOLARIS_HOST_OS
        return solaris_fremovexattr (filedes, name);
#endif

#ifdef GF_DARWIN_HOST_OS
        return fremovexattr (filedes, name, 0);
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
        return fsetxattr (filedes, name, value, size, 0,
                          flags & ~XATTR_NOSECURITY);
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

#if defined(F_ALLOCATECONTIG) && defined(GF_DARWIN_HOST_OS)
        /* C conversion from C++ implementation for OSX by Mozilla Foundation */
        if (mode) {
                /* keep size not supported */
                errno = EOPNOTSUPP;
                return -1;
        }
        /*
         *   The F_PREALLOCATE command operates on the following structure:
         *
         *    typedef struct fstore {
         *    u_int32_t fst_flags;      // IN: flags word
         *    int       fst_posmode;    // IN: indicates offset field
         *    off_t     fst_offset;     // IN: start of the region
         *    off_t     fst_length;     // IN: size of the region
         *    off_t     fst_bytesalloc; // OUT: number of bytes allocated
         *    } fstore_t;
         *
         * The flags (fst_flags) for the F_PREALLOCATE command are as follows:
         *    F_ALLOCATECONTIG   Allocate contiguous space.
         *    F_ALLOCATEALL      Allocate all requested space or no space at all.
         *
         * The position modes (fst_posmode) for the F_PREALLOCATE command
         * indicate how to use the offset field.  The modes are as follows:
         *    F_PEOFPOSMODE   Allocate from the physical end of file.
         *    F_VOLPOSMODE    Allocate from the volume offset.
         *
         */

        int ret;
        fstore_t store = {F_ALLOCATECONTIG, F_PEOFPOSMODE, offset, len, 0};
        ret = fcntl (fd, F_PREALLOCATE, &store);
        if (ret == -1) {
                store.fst_flags = F_ALLOCATEALL;
                ret = fcntl (fd, F_PREALLOCATE, &store);
        }
        if (ret == -1)
                return ret;
        return ftruncate (fd, offset + len);
#endif
        errno = ENOSYS;
        return -1;
}
