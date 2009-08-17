/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (c) 2009 Z RESEARCH, Inc. <http://www.zresearch.com>

  This program can be distributed under the terms of the GNU LGPLv2.
  See the file COPYING.LIB.
*/

void gf_fuse_unmount (const char *mountpoint, int fd);
int gf_fuse_mount (const char *mountpoint, char *fsname, char *mnt_param);
