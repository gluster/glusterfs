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

#include "common-utils.h"
#include "run.h"

#define _GLUSTERD_CALLED_ "_GLUSTERD_CALLED_"
#define GSYNCD_CONF "geo-replication/gsyncd.conf"

static int
config_wanted (int argc, char **argv)
{
        char *evas = NULL;
        char *oa = NULL;
        int i      = 0;
        int one_more_arg = 0;

        evas = getenv (_GLUSTERD_CALLED_);
        if (evas && strcmp (evas, "1") == 0) {
                /* OK, we know glusterd called us, no need to look for further config
                 * ... altough this conclusion should not inherit to our children
                 */
                unsetenv (_GLUSTERD_CALLED_);
                return 0;
        }

        for (i = 1; i < argc; i++) {
                /* -c found, see if it has an argument */
                if (one_more_arg) {
                        if (argv[i][0] != '-')
                                return 0;
                        one_more_arg = 0;
                }

                if ((strcmp (argv[i], "-c") && strcmp (argv[i], "--config-file")) == 0) {
                        one_more_arg = 1;
                        continue;
                }

                oa = strtail (argv[i], "-c");
                if (oa && !*oa)
                        oa = NULL;
                if (!oa)
                        oa = strtail (argv[i], "--config-file=");
                if (oa)
                        return 0;
        }

        return 1;
}

int
main(int argc, char **argv)
{
        char config_file[PATH_MAX] = {0,};
        size_t gluster_workdir_len = 0;
        runner_t runner            = {0,};
        int i                      = 0;
        int j                      = 0;
        char *nargv[argc + 4];

        if (config_wanted (argc, argv)) {
                runinit (&runner);
                runner_add_args (&runner, SBIN_DIR"/gluster",
                                 "--log-file=/dev/stderr", "system::", "getwd",
                                 NULL);
                runner_redir (&runner, STDOUT_FILENO, RUN_PIPE);
                if (runner_start (&runner) == 0 &&
                    fgets (config_file, PATH_MAX,
                           runner_chio (&runner, STDOUT_FILENO)) != NULL &&
                    config_file[strlen (config_file) - 1] == '\n' &&
                    runner_end (&runner) == 0)
                        gluster_workdir_len = strlen (config_file) - 1;

                if (gluster_workdir_len) {
                        if (gluster_workdir_len + 1 + strlen (GSYNCD_CONF) + 1 >
                            PATH_MAX)
                                goto error;
                        config_file[gluster_workdir_len] = '/';
                        strcat (config_file, GSYNCD_CONF);
                } else
                        config_file[0] = '\0';
        }

        j = 0;
        nargv[j++] = PYTHON;
        nargv[j++] = GSYNCD_PREFIX"/python/syncdaemon/gsyncd.py";
        if (config_file[0]) {
                nargv[j++] = "-c";
                nargv[j++] = config_file;
        }
        for (i = 1; i < argc; i++)
                nargv[j++] = argv[i];
        nargv[j++] = NULL;

        execvp (PYTHON, nargv);

        fprintf (stderr, "exec of "PYTHON" failed\n");
        return 127;

 error:
        fprintf (stderr, "gsyncd initializaion failed\n");
        return 1;
}
