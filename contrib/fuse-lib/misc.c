/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU LGPLv2.
  See the file COPYING.LIB
*/

#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include "glusterfs.h"
#include "fuse_kernel.h"
#include "fuse-misc.h"

unsigned long
calc_timeout_sec (double t)
{
        if (t > (double) ULONG_MAX)
                return ULONG_MAX;
        else if (t < 0.0)
                return 0;
        else
                return (unsigned long) t;
}

unsigned int
calc_timeout_nsec (double t)
{
        double f = t - (double) calc_timeout_sec (t);
        if (f < 0.0)
                return 0;
        else if (f >= 0.999999999)
                return 999999999;
        else
                return (unsigned int) (f * 1.0e9);
}

void
convert_fuse_file_lock (struct fuse_file_lock *fl, struct gf_flock *flock,
                        uint64_t lk_owner)
{
        memset (flock, 0, sizeof (struct flock));
        flock->l_type = fl->type;
        flock->l_whence = SEEK_SET;
        flock->l_start = fl->start;
        if (fl->end == OFFSET_MAX)
                flock->l_len = 0;
        else
                flock->l_len = fl->end - fl->start + 1;
        flock->l_pid = fl->pid;
        set_lk_owner_from_uint64 (&flock->l_owner, lk_owner);
}
