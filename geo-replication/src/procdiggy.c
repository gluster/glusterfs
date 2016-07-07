/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/param.h> /* for PATH_MAX */

#include "common-utils.h"
#include "syscall.h"
#include "procdiggy.h"

pid_t
pidinfo (pid_t pid, char **name)
{
        char buf[NAME_MAX * 2] = {0,};
        FILE *f                = NULL;
        char path[PATH_MAX]    = {0,};
        char *p                = NULL;
        int ret                = 0;

        snprintf (path, sizeof path, PROC"/%d/status", pid);

        f = fopen (path, "r");
        if (!f)
                return -1;

        if (name)
                *name = NULL;
        for (;;) {
                size_t len;
                memset (buf, 0, sizeof (buf));
                if (fgets (buf, sizeof (buf), f) == NULL ||
                    (len = strlen (buf)) == 0 ||
                    buf[len - 1] != '\n') {
                        pid = -1;
                        goto out;
                }
                buf[len - 1] = '\0';

                if (name && !*name) {
                        p = strtail (buf, "Name:");
                        if (p) {
                                while (isspace (*++p));
                                *name = gf_strdup (p);
                                if (!*name) {
                                        pid = -2;
                                        goto out;
                                }
                                continue;
                        }
                }

                p = strtail (buf, "PPid:");
                if (p)
                        break;
        }

        while (isspace (*++p));
        ret = gf_string2int (p, &pid);
        if (ret == -1)
                pid = -1;

 out:
        fclose (f);
        if (pid == -1 && name && *name)
                GF_FREE (name);
        if (pid == -2)
                fprintf (stderr, "out of memory\n");
        return pid;
}

int
prociter (int (*proch) (pid_t pid, pid_t ppid, char *tmpname, void *data),
          void *data)
{
        char *name        = NULL;
        DIR *d            = NULL;
        struct dirent *de = NULL;
        struct dirent scratch[2] = {{0,},};
        pid_t pid         = -1;
        pid_t ppid        = -1;
        int ret           = 0;

        d = sys_opendir (PROC);
        if (!d)
                return -1;

        for (;;) {
                errno = 0;
                de = sys_readdir (d, scratch);
                if (!de || errno != 0)
                        break;

                if (gf_string2int (de->d_name, &pid) != -1 && pid >= 0) {
                        ppid = pidinfo (pid, &name);
                        switch (ppid) {
                        case -1: continue;
                        case -2: ret = -1; break;
                        }
                        ret = proch (pid, ppid, name, data);
                        GF_FREE (name);
                        if (ret)
                                break;
                }
        }
        sys_closedir (d);
        if (!de && errno) {
                fprintf (stderr, "failed to traverse "PROC" (%s)\n",
                         strerror (errno));
                ret = -1;
        }

        return ret;
}
