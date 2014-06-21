/*
  Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _MNTENT_H
#define _MNTENT_H

#if !defined(GF_LINUX_HOST_OS)
#include <stdio.h>

struct mntent {
        char *mnt_fsname;
        char *mnt_dir;
        char *mnt_type;
        char *mnt_opts;
        int mnt_freq;
        int mnt_passno;
};

struct mntent *getmntent (FILE *fp);
struct mntent *getmntent_r (FILE *fp, struct mntent *result,
                            char *buffer, int bufsize);
FILE *setmntent (const char *filename, const char *type);
int endmntent(FILE *fp);
char * hasmntopt (const struct mntent *mnt, const char *option);

/* Dummy - /etc/mtab has no meaning on OSX platform */
#define _PATH_MOUNTED "/etc/mtab"

#endif /* GF_DARWIN_HOST_OS || __NetBSD__ */
#endif /* _MNTENT_H */
