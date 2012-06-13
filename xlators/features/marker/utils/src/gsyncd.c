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
#include <sys/param.h> /* for PATH_MAX */


#include "common-utils.h"
#include "run.h"
#include "procdiggy.h"

#define _GLUSTERD_CALLED_ "_GLUSTERD_CALLED_"
#define _GSYNCD_DISPATCHED_ "_GSYNCD_DISPATCHED_"
#define GSYNCD_CONF "geo-replication/gsyncd.conf"
#define GSYNCD_PY "gsyncd.py"
#define RSYNC "rsync"

int restricted = 0;

static int
duplexpand (void **buf, size_t tsiz, size_t *len)
{
        size_t osiz = tsiz * *len;

        *buf = realloc (*buf, osiz << 1);
        if (!buf)
                return -1;

        memset ((char *)*buf + osiz, 0, osiz);
        *len <<= 1;

        return 0;
}

static int
str2argv (char *str, char ***argv)
{
        char *p         = NULL;
        char *savetok   = NULL;
        int argc        = 0;
        size_t argv_len = 32;
        int ret         = 0;

        assert (str);
        str = strdup (str);
        if (!str)
                return -1;

        *argv = calloc (argv_len, sizeof (**argv));
        if (!*argv)
                goto error;

        while ((p = strtok_r (str, " ", &savetok))) {
                str = NULL;

                argc++;
                if (argc == argv_len) {
                        ret = duplexpand ((void *)argv,
                                          sizeof (**argv),
                                          &argv_len);
                        if (ret == -1)
                                goto error;
                }
                (*argv)[argc - 1] = p;
        }

        return argc;

 error:
        fprintf (stderr, "out of memory\n");
        return -1;
}

static int
invoke_gsyncd (int argc, char **argv)
{
        char config_file[PATH_MAX] = {0,};
        size_t gluster_workdir_len = 0;
        runner_t runner            = {0,};
        int i                      = 0;
        int j                      = 0;
        char *nargv[argc + 4];

        if (restricted) {
                /* in restricted mode we forcibly use the system-wide config */
                runinit (&runner);
                runner_add_args (&runner, SBIN_DIR"/gluster",
                                 "--log-file=-", "system::", "getwd",
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
                        goto error;

                if (setenv ("_GSYNCD_RESTRICTED_", "1", 1) == -1)
                        goto error;
        }

        if (chdir ("/") == -1)
                goto error;

        j = 0;
        nargv[j++] = PYTHON;
        nargv[j++] = GSYNCD_PREFIX"/python/syncdaemon/"GSYNCD_PY;
        for (i = 1; i < argc; i++)
                nargv[j++] = argv[i];
        if (config_file[0]) {
                nargv[j++] = "-c";
                nargv[j++] = config_file;
        }
        nargv[j++] = NULL;

        execvp (PYTHON, nargv);

        fprintf (stderr, "exec of "PYTHON" failed\n");
        return 127;

 error:
        fprintf (stderr, "gsyncd initializaion failed\n");
        return 1;
}


static int
find_gsyncd (pid_t pid, pid_t ppid, char *name, void *data)
{
        char buf[NAME_MAX * 2] = {0,};
        char path[PATH_MAX]    = {0,};
        char *p                = NULL;
        int zeros              = 0;
        int ret                = 0;
        int fd                 = -1;
        pid_t *pida            = (pid_t *)data;

        if (ppid != pida[0])
                return 0;

        sprintf (path, PROC"/%d/cmdline", pid);
        fd = open (path, O_RDONLY);
        if (fd == -1)
                return 0;
        ret = read (fd, buf, sizeof (buf));
        close (fd);
        if (ret == -1)
                return 0;
        for (zeros = 0, p = buf; zeros < 2 && p < buf + ret; p++)
                zeros += !*p;

        ret = 0;
        switch (zeros) {
        case 2:
                if ((strcmp (basename (buf), basename (PYTHON)) ||
                     strcmp (basename (buf + strlen (buf) + 1), GSYNCD_PY)) == 0) {
                        ret = 1;
                        break;
                }
                /* fallthrough */
        case 1:
                if (strcmp (basename (buf), GSYNCD_PY) == 0)
                        ret = 1;
        }

        if (ret == 1) {
                if (pida[1] != -1) {
                        fprintf (stderr, GSYNCD_PY" sibling is not unique");
                        return -1;
                }
                pida[1] = pid;
        }

        return 0;
}

static int
invoke_rsync (int argc, char **argv)
{
        int i                  = 0;
        char path[PATH_MAX]    = {0,};
        pid_t pid              = -1;
        pid_t ppid             = -1;
        pid_t pida[]           = {-1, -1};
        char *name             = NULL;
        char buf[PATH_MAX + 1] = {0,};
        int ret                = 0;

        assert (argv[argc] == NULL);

        if (argc < 2 || strcmp (argv[1], "--server") != 0)
                goto error;

        for (i = 2; i < argc && argv[i][0] == '-'; i++);

        if (!(i == argc - 2 && strcmp (argv[i], ".") == 0 && argv[i + 1][0] == '/')) {
                fprintf (stderr, "need an rsync invocation without protected args\n");
                goto error;
        }

        /* look up sshd we are spawned from */
        for (pid = getpid () ;; pid = ppid) {
                ppid = pidinfo (pid, &name);
                if (ppid < 0) {
                        fprintf (stderr, "sshd ancestor not found\n");
                        goto error;
                }
                if (strcmp (name, "sshd") == 0) {
                        GF_FREE (name);
                        break;
                }
                GF_FREE (name);
        }
        /* look up "ssh-sibling" gsyncd */
        pida[0] = pid;
        ret = prociter (find_gsyncd, pida);
        if (ret == -1 || pida[1] == -1) {
                fprintf (stderr, "gsyncd sibling not found\n");
                goto error;
        }
        /* check if rsync target matches gsyncd target */
        sprintf (path, PROC"/%d/cwd", pida[1]);
        ret = readlink (path, buf, sizeof (buf));
        if (ret == -1 || ret == sizeof (buf))
                goto error;
        if (strcmp (argv[argc - 1], "/") == 0 /* root dir cannot be a target */ ||
            (strcmp (argv[argc - 1], path) /* match against gluster target */ &&
             strcmp (argv[argc - 1], buf) /* match against file target */) != 0) {
                fprintf (stderr, "rsync target does not match "GEOREP" session\n");
                goto error;
        }

        argv[0] = RSYNC;

        execvp (RSYNC, argv);

        fprintf (stderr, "exec of "RSYNC" failed\n");
        return 127;

 error:
        fprintf (stderr, "disallowed "RSYNC" invocation\n");
        return 1;
}


struct invocable {
        char *name;
        int (*invoker) (int argc, char **argv);
};

struct invocable invocables[] = {
        { "rsync",  invoke_rsync  },
        { "gsyncd", invoke_gsyncd },
        { NULL, NULL}
};

int
main (int argc, char **argv)
{
        char *evas          = NULL;
        struct invocable *i = NULL;
        char *b             = NULL;
        char *sargv         = NULL;

        evas = getenv (_GLUSTERD_CALLED_);
        if (evas && strcmp (evas, "1") == 0)
                /* OK, we know glusterd called us, no need to look for further config
                 * ... altough this conclusion should not inherit to our children
                 */
                unsetenv (_GLUSTERD_CALLED_);
        else {
                /* we regard all gsyncd invocations unsafe
                 * that do not come from glusterd and
                 * therefore restrict it
                 */
                restricted = 1;

                if (!getenv (_GSYNCD_DISPATCHED_)) {
                        evas = getenv ("SSH_ORIGINAL_COMMAND");
                        if (evas)
                                sargv = evas;
                        else {
                                evas = getenv ("SHELL");
                                if (evas && strcmp (basename (evas), "gsyncd") == 0 &&
                                    argc == 3 && strcmp (argv[1], "-c") == 0)
                                        sargv = argv[2];
                        }
                }

        }

        if (!(sargv && restricted))
                return invoke_gsyncd (argc, argv);

        argc = str2argv (sargv, &argv);
        if (argc == -1 || setenv (_GSYNCD_DISPATCHED_, "1", 1) == -1) {
                fprintf (stderr, "internal error\n");
                return 1;
        }

        b = basename (argv[0]);
        for (i = invocables; i->name; i++) {
                if (strcmp (b, i->name) == 0)
                        return i->invoker (argc, argv);
        }

        fprintf (stderr, "invoking %s in restricted SSH session is not allowed\n",
                 b);

        return 1;
}
