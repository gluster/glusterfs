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

#include "common-utils.h"
#include "procdiggy.h"

pid_t
pidinfo (pid_t pid, char **name)
{
        char buf[NAME_MAX * 2] = {0,};
        FILE *f                = NULL;
        char *p                = NULL;
        int ret                = 0;

        ret = gf_asprintf (&p, PROC"/%d/status", pid);
        if (ret == -1)
                goto oom;

        f = fopen (p, "r");
        if (!f)
                return -1;

        if (name)
                *name = NULL;
        for (;;) {
                memset (buf, 0, sizeof (buf));
                if (fgets (buf, sizeof (buf), f) == NULL ||
                    buf[strlen (buf) - 1] != '\n') {
                        pid = -1;
                        goto out;
                }
                buf[strlen (buf) -1] = '\0';

                if (name && !*name) {
                        p = strtail (buf, "Name:");
                        if (p) {
                                while (isspace (*++p));
                                *name = gf_strdup (p);
                                if (!*name)
                                        goto oom;
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
        return pid;

 oom:
        fclose (f);
        fprintf (stderr, "out of memory\n");
        return -2;
}

int
prociter (int (*proch) (pid_t pid, pid_t ppid, char *name, void *data),
          void *data)
{
        char *name        = NULL;
        DIR *d            = NULL;
        struct dirent *de = NULL;
        pid_t pid         = -1;
        pid_t ppid        = -1;
        int ret           = 0;

        d = opendir (PROC);
        while (errno = 0, de = readdir (d)) {
                if (gf_string2int (de->d_name, &pid) != -1 && pid >= 0) {
                        ppid = pidinfo (pid, &name);
                        switch (ppid) {
                        case -1: continue;
                        case -2: return -1;
                        }
                        ret = proch (pid, ppid, name, data);
                        if (ret)
                                return ret;
                }
        }
        if (errno) {
                fprintf (stderr, "failed to traverse "PROC" (%s)\n",
                         strerror (errno));
                return -1;
        }

        return 0;
}
