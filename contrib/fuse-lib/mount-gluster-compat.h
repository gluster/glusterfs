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

#if defined(__NetBSD__)
#include <perfuse.h>
#define umount2(dir, flags) unmount(dir, ((flags) != 0) ? MNT_FORCE : 0)
#define MS_RDONLY MNT_RDONLY
#endif

#if defined(GF_DARWIN_HOST_OS) || defined(__FreeBSD__)
#include <sys/param.h>
#include <sys/mount.h>
#define umount2(dir, flags) unmount(dir, ((flags) != 0) ? MNT_FORCE : 0)
#define MS_RDONLY MNT_RDONLY
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
