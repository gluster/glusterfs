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
#include "libglusterfs-messages.h"

#include <sys/types.h>
#include <utime.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

#define FS_ERROR_LOG(result)                                                   \
        do {                                                                   \
                gf_msg_callingfn ("FS", GF_LOG_CRITICAL, EIO,                  \
                                  LG_MSG_SYSCALL_RETURNS_WRONG,                \
                                  "returned %zd for the syscall",              \
                                  (ssize_t)result);                            \
        } while (0)


/*
 * Input to these macros is generally a function call, so capture the result
 * i.e. (_ret) in another variable and use that instead of using _ret again
 */
#define FS_RET_CHECK(_ret, err)                                        \
({                                                                     \
        typeof(_ret) _result = (_ret);                                 \
        if (_result < -1) {                                            \
                FS_ERROR_LOG (_result);                                \
                _result = -1;                                          \
                err = EIO;                                             \
        }                                                              \
        _result;                                                       \
 })

#define FS_RET_CHECK0(_ret, err)                                       \
({                                                                     \
        typeof(_ret) _result0 = (_ret);                                \
        if (_result0 < -1 || _result0 > 0) {                           \
                FS_ERROR_LOG (_result0);                               \
                _result0 = -1;                                         \
                err = EIO;                                             \
        }                                                              \
        _result0;                                                      \
})

#define FS_RET_CHECK_ERRNO(_ret, err)                                  \
({                                                                     \
        typeof(_ret) _result1 = (_ret);                                \
        if (_result1 < 0) {                                            \
                FS_ERROR_LOG (_result1);                               \
                _result1 = -1;                                         \
                err = EIO;                                             \
        } else if (_result1 > 0) {                                     \
                err = _result1;                                        \
                _result1 = -1;                                         \
        }                                                              \
        _result1;                                                      \
})

int
sys_lstat (const char *path, struct stat *buf)
{
        return FS_RET_CHECK0(lstat (path, buf), errno);
}


int
sys_stat (const char *path, struct stat *buf)
{
        return FS_RET_CHECK0(stat (path, buf), errno);
}


int
sys_fstat (int fd, struct stat *buf)
{
        return FS_RET_CHECK0(fstat (fd, buf), errno);
}


int
sys_fstatat(int dirfd, const char *pathname, struct stat *buf, int flags)
{
#ifdef GF_DARWIN_HOST_OS
        if (fchdir(dirfd) < 0)
                return -1;
        if(flags & AT_SYMLINK_NOFOLLOW)
                return FS_RET_CHECK0(lstat(pathname, buf), errno);
        else
                return FS_RET_CHECK0(stat(pathname, buf), errno);
#else
        return FS_RET_CHECK0(fstatat (dirfd, pathname, buf, flags), errno);
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

        return FS_RET_CHECK(fd, errno);
}


int
sys_open(const char *pathname, int flags, int mode)
{
        return FS_RET_CHECK(sys_openat(AT_FDCWD, pathname, flags, mode), errno);
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
        return FS_RET_CHECK0(mkdir(pathname, mode), errno);
#else
        return FS_RET_CHECK0(mkdirat (dirfd, pathname, mode), errno);
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
        return FS_RET_CHECK(readlink (path, buf, bufsiz), errno);
}


int
sys_closedir (DIR *dir)
{
        return FS_RET_CHECK0(closedir (dir), errno);
}


int
sys_mknod (const char *pathname, mode_t mode, dev_t dev)
{
        return FS_RET_CHECK0(mknod (pathname, mode, dev), errno);
}


int
sys_mkdir (const char *pathname, mode_t mode)
{
        return FS_RET_CHECK0(mkdir (pathname, mode), errno);
}


int
sys_unlink (const char *pathname)
{
#ifdef GF_SOLARIS_HOST_OS
        return FS_RET_CHECK0(solaris_unlink (pathname), errno);
#endif
        return FS_RET_CHECK0(unlink (pathname), errno);
}


int
sys_rmdir (const char *pathname)
{
        return FS_RET_CHECK0(rmdir (pathname), errno);
}


int
sys_symlink (const char *oldpath, const char *newpath)
{
        return FS_RET_CHECK0(symlink (oldpath, newpath), errno);
}


int
sys_rename (const char *oldpath, const char *newpath)
{
#ifdef GF_SOLARIS_HOST_OS
        return FS_RET_CHECK0(solaris_rename (oldpath, newpath), errno);
#endif
        return FS_RET_CHECK0(rename (oldpath, newpath), errno);
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
        return FS_RET_CHECK0(linkat (AT_FDCWD, oldpath, AT_FDCWD, newpath, 0),
                             errno);
#else
        return FS_RET_CHECK0(link (oldpath, newpath), errno);
#endif
}


int
sys_chmod (const char *path, mode_t mode)
{
        return FS_RET_CHECK0(chmod (path, mode), errno);
}


int
sys_fchmod (int fd, mode_t mode)
{
        return FS_RET_CHECK0(fchmod (fd, mode), errno);
}


int
sys_chown (const char *path, uid_t owner, gid_t group)
{
        return FS_RET_CHECK0(chown (path, owner, group), errno);
}


int
sys_fchown (int fd, uid_t owner, gid_t group)
{
        return FS_RET_CHECK0(fchown (fd, owner, group), errno);
}


int
sys_lchown (const char *path, uid_t owner, gid_t group)
{
        return FS_RET_CHECK0(lchown (path, owner, group), errno);
}


int
sys_truncate (const char *path, off_t length)
{
        return FS_RET_CHECK0(truncate (path, length), errno);
}


int
sys_ftruncate (int fd, off_t length)
{
        return FS_RET_CHECK0(ftruncate (fd, length), errno);
}


int
sys_utimes (const char *filename, const struct timeval times[2])
{
        return FS_RET_CHECK0(utimes (filename, times), errno);
}


#if defined(HAVE_UTIMENSAT)
int
sys_utimensat (int dirfd, const char *filename, const struct timespec times[2],
               int flags)
{
        return FS_RET_CHECK0(utimensat (dirfd, filename, times, flags), errno);
}
#endif


int
sys_futimes (int fd, const struct timeval times[2])
{
        return futimes (fd, times);
}


int
sys_creat (const char *pathname, mode_t mode)
{
        return FS_RET_CHECK(sys_open(pathname, O_CREAT | O_TRUNC | O_WRONLY,
                                     mode), errno);
}


ssize_t
sys_readv (int fd, const struct iovec *iov, int iovcnt)
{
        return FS_RET_CHECK(readv (fd, iov, iovcnt), errno);
}


ssize_t
sys_writev (int fd, const struct iovec *iov, int iovcnt)
{
        return FS_RET_CHECK(writev (fd, iov, iovcnt), errno);
}


ssize_t
sys_read (int fd, void *buf, size_t count)
{
        return FS_RET_CHECK(read (fd, buf, count), errno);
}


ssize_t
sys_write (int fd, const void *buf, size_t count)
{
        return FS_RET_CHECK(write (fd, buf, count), errno);
}


ssize_t
sys_preadv (int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
        return FS_RET_CHECK(preadv (fd, iov, iovcnt, offset), errno);
}


ssize_t
sys_pwritev (int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
        return FS_RET_CHECK(pwritev (fd, iov, iovcnt, offset), errno);
}


ssize_t
sys_pread (int fd, void *buf, size_t count, off_t offset)
{
        return FS_RET_CHECK(pread (fd, buf, count, offset), errno);
}


ssize_t
sys_pwrite (int fd, const void *buf, size_t count, off_t offset)
{
        return FS_RET_CHECK(pwrite (fd, buf, count, offset), errno);
}


off_t
sys_lseek (int fd, off_t offset, int whence)
{
        return FS_RET_CHECK(lseek (fd, offset, whence), errno);
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

        return FS_RET_CHECK0(ret, errno);
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

        return FS_RET_CHECK0(ret, errno);
}


int
sys_close (int fd)
{
        int ret = -1;

        if (fd >= 0)
                ret = close (fd);

        return FS_RET_CHECK0(ret, errno);
}


int
sys_fsync (int fd)
{
        return FS_RET_CHECK0(fsync (fd), errno);
}


int
sys_fdatasync (int fd)
{
#ifdef GF_DARWIN_HOST_OS
        return FS_RET_CHECK0(fcntl (fd, F_FULLFSYNC), errno);
#elif __FreeBSD__
	return FS_RET_CHECK0(fsync (fd), errno);
#else
        return FS_RET_CHECK0(fdatasync (fd), errno);
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
        return FS_RET_CHECK0(lsetxattr (path, name, value, size, flags), errno);
#endif

#ifdef GF_BSD_HOST_OS
        return FS_RET_CHECK0(extattr_set_link (path, EXTATTR_NAMESPACE_USER,
                             name, value, size), errno);
#endif

#ifdef GF_SOLARIS_HOST_OS
        return FS_RET_CHECK0(solaris_setxattr (path, name, value, size, flags),
                             errno);
#endif

#ifdef GF_DARWIN_HOST_OS
        /* OS X clients will carry other flags, which will be used on a
           OS X host, but masked out on others. GF assume NOFOLLOW on Linux,
           enforcing  */
        return FS_RET_CHECK0(setxattr (path, name, value, size, 0,
                             (flags & ~XATTR_NOSECURITY) | XATTR_NOFOLLOW),
                             errno);
#endif

}


ssize_t
sys_llistxattr (const char *path, char *list, size_t size)
{

#if defined(GF_LINUX_HOST_OS) || defined(__NetBSD__)
        return FS_RET_CHECK(llistxattr (path, list, size), errno);
#endif

#ifdef GF_BSD_HOST_OS
        ssize_t ret = FS_RET_CHECK(extattr_list_link (path,
                                                      EXTATTR_NAMESPACE_USER,
                                                      list, size), errno);
        gf_extattr_list_reshape (list, ret);
        return ret;
#endif

#ifdef GF_SOLARIS_HOST_OS
        return FS_RET_CHECK(solaris_listxattr (path, list, size), errno);
#endif

#ifdef GF_DARWIN_HOST_OS
        return FS_RET_CHECK(listxattr (path, list, size, XATTR_NOFOLLOW),
                            errno);
#endif
}

ssize_t
sys_lgetxattr (const char *path, const char *name, void *value, size_t size)
{

#if defined(GF_LINUX_HOST_OS) || defined(__NetBSD__)
        return FS_RET_CHECK(lgetxattr (path, name, value, size), errno);
#endif

#ifdef GF_BSD_HOST_OS
        return FS_RET_CHECK(extattr_get_link (path, EXTATTR_NAMESPACE_USER,
                                              name, value, size), errno);
#endif

#ifdef GF_SOLARIS_HOST_OS
        return FS_RET_CHECK(solaris_getxattr (path, name, value, size), errno);
#endif

#ifdef GF_DARWIN_HOST_OS
         return FS_RET_CHECK(getxattr (path, name, value, size, 0,
                                       XATTR_NOFOLLOW), errno);
#endif

}


ssize_t
sys_fgetxattr (int filedes, const char *name, void *value, size_t size)
{

#if defined(GF_LINUX_HOST_OS) || defined(__NetBSD__)
        return FS_RET_CHECK(fgetxattr (filedes, name, value, size), errno);
#endif

#ifdef GF_BSD_HOST_OS
        return FS_RET_CHECK(extattr_get_fd (filedes, EXTATTR_NAMESPACE_USER,
                            name, value, size), errno);
#endif

#ifdef GF_SOLARIS_HOST_OS
        return FS_RET_CHECK(solaris_fgetxattr (filedes, name, value, size),
                            errno);
#endif

#ifdef GF_DARWIN_HOST_OS
        return FS_RET_CHECK(fgetxattr (filedes, name, value, size, 0, 0),
                            errno);
#endif

}

int
sys_fremovexattr (int filedes, const char *name)
{

#if defined(GF_LINUX_HOST_OS) || defined(__NetBSD__)
        return FS_RET_CHECK0(fremovexattr (filedes, name), errno);
#endif

#ifdef GF_BSD_HOST_OS
        return FS_RET_CHECK0(extattr_delete_fd (filedes, EXTATTR_NAMESPACE_USER,
                                                name), errno);
#endif

#ifdef GF_SOLARIS_HOST_OS
        return FS_RET_CHECK0(solaris_fremovexattr (filedes, name), errno);
#endif

#ifdef GF_DARWIN_HOST_OS
        return FS_RET_CHECK0(fremovexattr (filedes, name, 0), errno);
#endif
}


int
sys_fsetxattr (int filedes, const char *name, const void *value,
               size_t size, int flags)
{

#if defined(GF_LINUX_HOST_OS) || defined(__NetBSD__)
        return FS_RET_CHECK0(fsetxattr (filedes, name, value, size, flags),
                             errno);
#endif

#ifdef GF_BSD_HOST_OS
        return FS_RET_CHECK0(extattr_set_fd (filedes, EXTATTR_NAMESPACE_USER,
                                             name, value, size), errno);
#endif

#ifdef GF_SOLARIS_HOST_OS
        return FS_RET_CHECK0(solaris_fsetxattr (filedes, name, value, size,
                                                flags), errno);
#endif

#ifdef GF_DARWIN_HOST_OS
        return FS_RET_CHECK0(fsetxattr (filedes, name, value, size, 0,
                                        flags & ~XATTR_NOSECURITY), errno);
#endif

}


ssize_t
sys_flistxattr (int filedes, char *list, size_t size)
{

#if defined(GF_LINUX_HOST_OS) || defined(__NetBSD__)
        return FS_RET_CHECK(flistxattr (filedes, list, size), errno);
#endif

#ifdef GF_BSD_HOST_OS
        ssize_t ret = FS_RET_CHECK (extattr_list_fd (filedes,
                                                     EXTATTR_NAMESPACE_USER,
                                                     list, size), errno);
        gf_extattr_list_reshape (list, ret);
        return ret;
#endif

#ifdef GF_SOLARIS_HOST_OS
        return FS_RET_CHECK(solaris_flistxattr (filedes, list, size), errno);
#endif

#ifdef GF_DARWIN_HOST_OS
        return FS_RET_CHECK(flistxattr (filedes, list, size, XATTR_NOFOLLOW),
                            errno);
#endif

}


int
sys_lremovexattr (const char *path, const char *name)
{

#if defined(GF_LINUX_HOST_OS) || defined(__NetBSD__)
        return FS_RET_CHECK0(lremovexattr (path, name), errno);
#endif

#ifdef GF_BSD_HOST_OS
        return FS_RET_CHECK0(extattr_delete_link (path, EXTATTR_NAMESPACE_USER,
                                                  name), errno);
#endif

#ifdef GF_SOLARIS_HOST_OS
        return FS_RET_CHECK0(solaris_removexattr (path, name), errno);
#endif

#ifdef GF_DARWIN_HOST_OS
        return FS_RET_CHECK0(removexattr (path, name, XATTR_NOFOLLOW), errno);
#endif

}


int
sys_access (const char *pathname, int mode)
{
        return FS_RET_CHECK0(access (pathname, mode), errno);
}


int
sys_fallocate(int fd, int mode, off_t offset, off_t len)
{
#ifdef HAVE_FALLOCATE
        return FS_RET_CHECK0(fallocate(fd, mode, offset, len), errno);
#endif

#ifdef HAVE_POSIX_FALLOCATE
        if (mode) {
                /* keep size not supported */
                errno = EOPNOTSUPP;
                return -1;
        }

        return FS_RET_CHECK_ERRNO(posix_fallocate(fd, offset, len), errno);
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
        return FS_RET_CHECK0(ftruncate (fd, offset + len), errno);
#endif
        errno = ENOSYS;
        return -1;
}
