/*
  Copyright (c) 2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef __SYSCALL_H__
#define __SYSCALL_H__

int
sys_lstat (const char *path, struct stat *buf);

int
sys_stat (const char *path, struct stat *buf);

int 
sys_fstat (int fd, struct stat *buf);

DIR *
sys_opendir (const char *name);

struct dirent *
sys_readdir (DIR *dir);

ssize_t 
sys_readlink (const char *path, char *buf, size_t bufsiz);

int 
sys_closedir (DIR *dir);

int
sys_mknod (const char *pathname, mode_t mode, dev_t dev);

int 
sys_mkdir (const char *pathname, mode_t mode);

int 
sys_unlink (const char *pathname);

int 
sys_rmdir (const char *pathname);

int 
sys_symlink (const char *oldpath, const char *newpath);

int
sys_rename (const char *oldpath, const char *newpath);

int 
sys_link (const char *oldpath, const char *newpath);

int
sys_chmod (const char *path, mode_t mode);

int
sys_fchmod (int fd, mode_t mode);

int 
sys_chown (const char *path, uid_t owner, gid_t group);

int
sys_fchown (int fd, uid_t owner, gid_t group);

int
sys_lchown (const char *path, uid_t owner, gid_t group);

int 
sys_truncate (const char *path, off_t length);

int 
sys_ftruncate (int fd, off_t length);

int 
sys_utimes (const char *filename, const struct timeval times[2]);

int
sys_creat (const char *pathname, mode_t mode);

ssize_t
sys_readv (int fd, const struct iovec *iov, int iovcnt);

ssize_t
sys_writev (int fd, const struct iovec *iov, int iovcnt);

ssize_t
sys_read (int fd, void *buf, size_t count);

ssize_t 
sys_write (int fd, const void *buf, size_t count);

off_t
sys_lseek (int fd, off_t offset, int whence);

int
sys_statvfs (const char *path, struct statvfs *buf);

int 
sys_close (int fd);

int 
sys_fsync (int fd);

int 
sys_fdatasync (int fd);

int 
sys_lsetxattr (const char *path, const char *name, const void *value, 
               size_t size, int flags); 

ssize_t
sys_llistxattr (const char *path, char *list, size_t size); 

ssize_t
sys_lgetxattr (const char *path, const char *name, void *value, size_t size); 

ssize_t 
sys_fgetxattr (int filedes, const char *name, void *value, size_t size); 

int 
sys_fsetxattr (int filedes, const char *name, const void *value, 
               size_t size, int flags);

ssize_t 
sys_flistxattr (int filedes, char *list, size_t size); 

int 
sys_lremovexattr (const char *path, const char *name);

int 
sys_access (const char *pathname, int mode);

int 
sys_ftruncate (int fd, off_t length);

#endif /* __SYSCALL_H__ */
