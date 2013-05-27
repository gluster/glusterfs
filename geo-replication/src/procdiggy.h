/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifdef __NetBSD__
#include <sys/syslimits.h>
#endif /* __NetBSD__ */

#define PROC "/proc"

pid_t pidinfo (pid_t pid, char **name);

int prociter (int (*proch) (pid_t pid, pid_t ppid, char *name, void *data),
              void *data);

