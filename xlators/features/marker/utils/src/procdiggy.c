/*
  Copyright (c) 2011 Gluster, Inc. <http://www.gluster.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/param.h> /* for PATH_MAX */

#include "common-utils.h"
#include "procdiggy.h"

pid_t
pidinfo (pid_t pid, char **name)
{
        char buf[NAME_MAX * 2] = {0,};
        FILE *f                = NULL;
        char path[PATH_MAX]    = {0,};
        char *p                = NULL;
        int ret                = 0;

        sprintf (path, PROC"/%d/status", pid);

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
        pid_t pid         = -1;
        pid_t ppid        = -1;
        int ret           = 0;

        d = opendir (PROC);
        if (!d)
                return -1;
        while (errno = 0, de = readdir (d)) {
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
        closedir (d);
        if (!de && errno) {
                fprintf (stderr, "failed to traverse "PROC" (%s)\n",
                         strerror (errno));
                ret = -1;
        }

        return ret;
}
