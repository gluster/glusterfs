/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU LGPLv2.
  See the file COPYING.LIB
*/

#define OFFSET_MAX 0x7fffffffffffffffLL

unsigned long calc_timeout_sec (double t);
unsigned int calc_timeout_nsec (double t);
void convert_fuse_file_lock (struct fuse_file_lock *fl, struct gf_flock *flock,
                             uint64_t lk_owner);
