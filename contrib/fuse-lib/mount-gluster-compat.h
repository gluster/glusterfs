/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>

  This program can be distributed under the terms of the GNU LGPLv2.
  See the file COPYING.LIB.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#if defined(GF_LINUX_HOST_OS)
#include <mntent.h>
#endif /* GF_LINUX_HOST_OS */
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/mount.h>

#ifdef GF_LINUX_HOST_OS
typedef unsigned long mount_flag_t;
#endif

#if defined(__NetBSD__)
#include <perfuse.h>
#define umount2(dir, flags) unmount(dir, ((flags) != 0) ? MNT_FORCE : 0)
#define MS_RDONLY MNT_RDONLY
#define MS_NOSUID MNT_NOSUID
#define MS_NODEV  MNT_NODEV
#define MS_NOATIME MNT_NOATIME
#define MS_NOEXEC MNT_NOEXEC
typedef int mount_flag_t;
#endif

#if defined(GF_DARWIN_HOST_OS) || defined(__FreeBSD__)
#include <sys/param.h>
#include <sys/mount.h>
#define umount2(dir, flags) unmount(dir, ((flags) != 0) ? MNT_FORCE : 0)
#endif

#if defined(__FreeBSD__)
#define MS_RDONLY MNT_RDONLY
#define MS_NOSUID MNT_NOSUID
/* "nodev"/MNT_NODEV was removed from FreBSD, as it became unneeded because "As
 * of FreeBSD 6.0 device nodes may be created in regular file systems but such
 * nodes cannot be used to access devices." (See
 * https://freebsd.org/cgi/man.cgi?query=mknod&sektion=8 .
 * Also see:
 * - https://github.com/freebsd/freebsd/commit/266790a
 * - https://github.com/freebsd/freebsd/commit/a5e716d
 * - 700008 in
 *   https://www.freebsd.org/doc/en/books/porters-handbook/versions-7.html .)
 */
#if __FreeBSD_version < 700008
#define MS_NODEV  MNT_NODEV
#else
#define MS_NODEV  0
#endif
#define MS_NOATIME MNT_NOATIME
#define MS_NOEXEC MNT_NOEXEC
#if __FreeBSD_version < 1000715
typedef int  mount_flag_t;
#else
/* __FreeBSD_version was not bumped for this type change. Anyway, see
 * https://github.com/freebsd/freebsd/commit/e8d76f8
 * and respective __FreeBSD_version:
 * https://github.com/freebsd/freebsd/blob/e8d76f8/sys/sys/param.h#L61 .
 * We use the subsequent value, 1000715, to switch. (Also see:
 * https://www.freebsd.org/doc/en/books/porters-handbook/versions-10.html .)
 */
typedef long long mount_flag_t;
#endif
#endif

#ifdef GF_LINUX_HOST_OS
#define _PATH_MOUNT "/bin/mount"
#else /* FreeBSD, NetBSD, MacOS X */
#define _PATH_MOUNT "/sbin/mount"
#endif

#ifdef FUSE_UTIL
#define MALLOC(size) malloc (size)
#define FREE(ptr) free (ptr)
#define GFFUSE_LOGERR(...) fprintf (stderr, ## __VA_ARGS__)
#else /* FUSE_UTIL */
#include "glusterfs.h"
#include "logging.h"
#include "common-utils.h"

#define GFFUSE_LOGERR(...) \
        gf_log ("glusterfs-fuse", GF_LOG_ERROR, ## __VA_ARGS__)
#endif /* !FUSE_UTIL */
