/*
   Copyright (c) 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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
#include <sys/stat.h>
#include <sys/uio.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <inttypes.h>

#ifndef GF_SOLARIS_HOST_OS
#include <sys/xattr.h>
#endif

int
glusterfs_booster_bridge_pwritev (void *filep, const struct iovec *vector,
				  int count, off_t offset);
int
glusterfs_booster_bridge_preadv (void *filep, const struct iovec *vector,
				 int count, off_t offset);
void
glusterfs_booster_bridge_close (void *filep);

void *glusterfs_booster_bridge_init ();

void *
glusterfs_booster_bridge_open (void *ctx, char *options, int size,
			       char *handle);




/* open, open64, creat */
static int (*real_open) (const char *pathname, int flags, mode_t mode);
static int (*real_open64) (const char *pathname, int flags, mode_t mode);
static int (*real_creat) (const char *pathname, mode_t mode);

/* read, readv, pread, pread64 */
static ssize_t (*real_read) (int fd, void *buf, size_t count);
static ssize_t (*real_readv) (int fd, const struct iovec *vector, int count);
static ssize_t (*real_pread) (int fd, void *buf, size_t count, off_t offset);
static ssize_t (*real_pread64) (int fd, void *buf, size_t count, off_t offset);

/* write, writev, pwrite, pwrite64 */
static ssize_t (*real_write) (int fd, const void *buf, size_t count);
static ssize_t (*real_writev) (int fd, const struct iovec *vector, int count);
static ssize_t (*real_pwrite) (int fd, const void *buf, size_t count, off_t offset);
static ssize_t (*real_pwrite64) (int fd, const void *buf, size_t count, off_t offset);

/* lseek, llseek, lseek64 */
static off_t (*real_lseek) (int fildes, uint32_t offset, int whence);
static off_t (*real_lseek64) (int fildes, uint64_t offset, int whence);

/* close */
static int (*real_close) (int fd);

/* dup dup2 */
static int (*real_dup) (int fd);
static int (*real_dup2) (int oldfd, int newfd);

#define RESOLVE(sym) do {                     \
  if (!real_##sym)                            \
    real_##sym = dlsym (RTLD_NEXT, #sym);     \
} while (0)

static void *fdtable[65536];
void *ctx;

static void
do_open (int fd)
{
  char options[512], handle[20];
  int options_ret, handle_ret;
  void *filep;

  options_ret = fgetxattr (fd, "user.glusterfs-booster-transport-options",
			   options, 512);

  if (options_ret == -1)
    return;

  handle_ret = fgetxattr (fd, "user.glusterfs-booster-handle", handle, 20);

  if (handle_ret == -1)
    return;

  //  printf ("open on fd = %d, handle=%s\n", fd, handle);

  filep = glusterfs_booster_bridge_open (ctx, options, options_ret, handle);

  if (!filep)
    return;

  if (fdtable[fd])
    /* TODO: 
       - disconnect transport
    */
    free (fdtable[fd]);

  fdtable[fd] = filep;
}

int
open (const char *pathname, int flags, mode_t mode)
{
  int ret;

  ret = real_open (pathname, flags, mode);

  if (ret != -1)
    do_open (ret);

  return ret;
}

int
open64 (const char *pathname, int flags, mode_t mode)
{
  int ret;

  ret = real_open64 (pathname, flags, mode);

  if (ret != -1)
    do_open (ret);

  return ret;
}

int
creat (const char *pathname, mode_t mode)
{
  int ret;

  ret = real_creat (pathname, mode);

  if (ret != -1)
    //    do_open (ret, O_CREAT|O_WRONLY|O_TRUNC);
    do_open (ret);

  return ret;
}

/* preadv */

static ssize_t
do_preadv (int fd, const struct iovec *vector,
	   int count, off_t offset)
{
  ssize_t ret;

  //  printf ("doing read on fd=%d, offset=%"PRId64"\n", fd, offset);
  ret = glusterfs_booster_bridge_preadv (fdtable[fd], vector, count, offset);

  //  printf ("returning %d\n", ret);
  if (ret == -1) {
    //    printf ("disabling booster on fd=%d\n", fd);
    /* TODO: 
       - disconnect transport
       - retry with real_read
    */
    free (fdtable[fd]);
    fdtable[fd] = NULL;
  } else {
    //    printf ("setting seek to %ld\n", (offset + ret));
    real_lseek64 (fd, (offset + ret), SEEK_SET);
  }

  return ret;
}

ssize_t
read (int fd, void *buf, size_t count)
{
  int ret;

  if (!fdtable[fd]) {
    ret = real_read (fd, buf, count);
  } else {
    struct iovec vector;
    off_t offset;

    vector.iov_base = buf;
    vector.iov_len = count;

    offset = real_lseek64 (fd, 0, SEEK_CUR);

    ret = do_preadv (fd, &vector, 1, offset);
  }

  return ret;
}

ssize_t
readv (int fd, const struct iovec *vector, int count)
{
  int ret;

  if (!fdtable[fd]) {
    ret = real_readv (fd, vector, count);
  } else {
    off_t offset;

    offset = real_lseek64 (fd, 0, SEEK_CUR);

    ret = do_preadv (fd, vector, count, offset);
  }

  return ret;
}

ssize_t
pread (int fd, void *buf, size_t count, off_t offset)
{
  int ret;

  if (!fdtable[fd]) {
    ret = real_pread (fd, buf, count, offset);
  } else {
    struct iovec vector;

    vector.iov_base = buf;
    vector.iov_len = count;

    ret = do_preadv (fd, &vector, 1, offset);
  }

  return ret;
}

ssize_t
pread64 (int fd, void *buf, size_t count, off_t offset)
{
  int ret;

  if (!fdtable[fd]) {
    ret = real_pread64 (fd, buf, count, offset);
  } else {
    struct iovec vector;

    vector.iov_base = buf;
    vector.iov_len = count;

    ret = do_preadv (fd, &vector, count, offset);
  }

  return ret;
}

/* pwritev */
static ssize_t
do_pwritev (int fd, const struct iovec *vector,
	   int count, off_t offset)
{
  ssize_t ret;

  //  printf ("doing pwritev on fd=%d\n", fd);
  ret = glusterfs_booster_bridge_pwritev (fdtable[fd], vector, count, offset);

  if (ret == -1) {
    //    printf ("disabling booster on fd=%d\n", fd);
    /* TODO:
       - disconnect transport
       - retry with real_writev
    */
    free (fdtable[fd]);
    fdtable[fd] = NULL;
  } else {
    real_lseek64 (fd, (offset + ret), SEEK_SET);
  }

  return ret;
}

ssize_t
write (int fd, const void *buf, size_t count)
{
  int ret;

  if (!fdtable[fd]) {
    ret = real_write (fd, buf, count);
  } else {
    struct iovec vector;
    off_t offset;

    vector.iov_base = (void *) buf;
    vector.iov_len = count;

    offset = real_lseek64 (fd, 0, SEEK_CUR);

    ret = do_pwritev (fd, &vector, 1, offset);
  }

  return ret;
}

ssize_t
writev (int fd, const struct iovec *vector, int count)
{
  int ret;

  if (!fdtable[fd]) {
    ret = real_writev (fd, vector, count);
  } else {
    off_t offset;

    offset = real_lseek64 (fd, 0, SEEK_CUR);

    ret = do_pwritev (fd, vector, count, offset);
  }

  return ret;
}

ssize_t
pwrite (int fd, const void *buf, size_t count, off_t offset)
{
  int ret;

  if (!fdtable[fd]) {
    ret = real_pwrite (fd, buf, count, offset);
  } else {
    struct iovec vector;

    vector.iov_base = (void *) buf;
    vector.iov_len = count;

    ret = do_pwritev (fd, &vector, 1, offset);
  }

  return ret;
}

ssize_t
pwrite64 (int fd, const void *buf, size_t count, off_t offset)
{
  int ret;

  if (!fdtable[fd]) {
    ret = real_pwrite64 (fd, buf, count, offset);
  } else {
    struct iovec vector;

    vector.iov_base = (void *) buf;
    vector.iov_len = count;

    ret = do_pwritev (fd, &vector, count, offset);
  }

  return ret;
}

/* close */
static ssize_t
do_close (int fd)
{
  ssize_t ret = 0;
  
  //  printf ("doing close on fd=%d\n", fd);
  glusterfs_booster_bridge_close (fdtable[fd]);
  
  free (fdtable[fd]);
  fdtable[fd] = NULL;

  return ret;
}
 
int
close (int fd)
{
  int ret;

  if (!fdtable[fd]) {
    ret = real_close (fd);
  } else {
    ret = do_close (fd);
  }

  return ret;
}

off_t
lseek (int fildes, uint32_t offset, int whence)
{
  int ret;

  ret = real_lseek (fildes, offset, whence);

  return ret;
}

off_t
lseek64 (int fildes, uint64_t offset, int whence)
{
  int ret;

  ret = real_lseek64 (fildes, offset, whence);

  return ret;
}

void
_init (void)
{
  RESOLVE (open);
  RESOLVE (open64);
  RESOLVE (creat);

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

  ctx = glusterfs_booster_bridge_init ();
}
