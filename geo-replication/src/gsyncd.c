/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "compat.h"
#include "syscall.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h> /* for PATH_MAX */

/* NOTE (USE_LIBGLUSTERFS):
 * ------------------------
 * When USE_LIBGLUSTERFS debugging sumbol is passed; perform
 * glusterfs translator like initialization so that glusterfs
 * globals, contexts are valid when glustefs api's are invoked.
 * We unconditionally pass then while building gsyncd binary.
 */
#ifdef USE_LIBGLUSTERFS
#include "glusterfs.h"
#include "globals.h"
#include "defaults.h"
#endif

#include "common-utils.h"
#include "run.h"
#include "procdiggy.h"

#define _GLUSTERD_CALLED_ "_GLUSTERD_CALLED_"
#define _GSYNCD_DISPATCHED_ "_GSYNCD_DISPATCHED_"
#define GSYNCD_CONF_TEMPLATE "geo-replication/gsyncd_template.conf"
#define GSYNCD_PY "gsyncd.py"
#define RSYNC "rsync"

int restricted = 0;

static int
duplexpand (void **buf, size_t tsiz, size_t *len)
{
        size_t osiz = tsiz * *len;
        char *p = realloc (*buf, osiz << 1);
        if (!p) {
                free(*buf);
                return -1;
        }

        memset (p + osiz, 0, osiz);
        *buf = p;
        *len <<= 1;

        return 0;
}

static int
str2argv (char *str, char ***argv)
{
        char *p         = NULL;
        char *savetok   = NULL;
        char *temp      = NULL;
        char *temp1     = NULL;
        int argc        = 0;
        size_t argv_len = 32;
        int ret         = 0;
        int i           = 0;

        assert (str);
        temp = str = strdup (str);
        if (!str)
                goto error;

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
                temp1 = strdup (p);
                if (!temp1)
                        goto error;
                (*argv)[argc - 1] = temp1;
        }

        free(temp);
        return argc;

 error:
        fprintf (stderr, "out of memory\n");
        free(temp);
        for (i = 0; i < argc - 1; i++)
                free((*argv)[i]);
        free(*argv);
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
        char *python = NULL;

        if (restricted) {
                size_t len;
                /* in restricted mode we forcibly use the system-wide config */
                runinit (&runner);
                runner_add_args (&runner, SBIN_DIR"/gluster",
                                 "--remote-host=localhost",
                                 "--log-file=-", "system::", "getwd",
                                 NULL);
                runner_redir (&runner, STDOUT_FILENO, RUN_PIPE);
                if (runner_start (&runner) == 0 &&
                    fgets (config_file, PATH_MAX,
                           runner_chio (&runner, STDOUT_FILENO)) != NULL &&
                    (len = strlen (config_file)) &&
                    config_file[len - 1] == '\n' &&
                    runner_end (&runner) == 0)
                        gluster_workdir_len = len - 1;

                if (gluster_workdir_len) {
                        if (gluster_workdir_len + 1 + strlen (GSYNCD_CONF_TEMPLATE) + 1 >
                            PATH_MAX)
                                goto error;
                        config_file[gluster_workdir_len] = '/';
                        strcat (config_file, GSYNCD_CONF_TEMPLATE);
                } else
                        goto error;

                if (setenv ("_GSYNCD_RESTRICTED_", "1", 1) == -1)
                        goto error;
        }

        if (chdir ("/") == -1)
                goto error;

        j = 0;
        python = getenv("PYTHON");
        if(!python)
                python = PYTHON;
        nargv[j++] = python;
        nargv[j++] = GSYNCD_PREFIX"/python/syncdaemon/"GSYNCD_PY;
        for (i = 1; i < argc; i++)
                nargv[j++] = argv[i];
        if (config_file[0]) {
                nargv[j++] = "-c";
                nargv[j++] = config_file;
        }
        nargv[j++] = NULL;

        execvp (python, nargv);

        fprintf (stderr, "exec of '%s' failed\n", python);
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

        snprintf (path, sizeof path, PROC"/%d/cmdline", pid);
        fd = open (path, O_RDONLY);
        if (fd == -1)
                return 0;
        ret = sys_read (fd, buf, sizeof (buf));
        sys_close (fd);
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
        snprintf (path, sizeof path, PROC"/%d/cwd", pida[1]);
        ret = sys_readlink (path, buf, sizeof (buf));
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

static int
invoke_gluster (int argc, char **argv)
{
        int i = 0;
        int j = 0;
        int optsover = 0;
        char *ov = NULL;

        for (i = 1; i < argc; i++) {
                ov = strtail (argv[i], "--");
                if (ov && !optsover) {
                        if (*ov == '\0')
                                optsover = 1;
                        continue;
                }
                switch (++j) {
                case 1:
                        if (strcmp (argv[i], "volume") != 0)
                                goto error;
                        break;
                case 2:
                        if (strcmp (argv[i], "info") != 0)
                                goto error;
                        break;
                case 3:
                        break;
                default:
                        goto error;
                }
        }

        argv[0] = "gluster";
        execvp (SBIN_DIR"/gluster", argv);
        fprintf (stderr, "exec of gluster failed\n");
        return 127;

 error:
        fprintf (stderr, "disallowed gluster invocation\n");
        return 1;
}

struct invocable {
        char *name;
        int (*invoker) (int argc, char **argv);
};

struct invocable invocables[] = {
        { "rsync",   invoke_rsync  },
        { "gsyncd",  invoke_gsyncd },
        { "gluster", invoke_gluster },
        { NULL, NULL}
};

int
main (int argc, char **argv)
{
        int               ret   = -1;
        char             *evas  = NULL;
        struct invocable *i     = NULL;
        char             *b     = NULL;
        char             *sargv = NULL;
        int               j     = 0;

#ifdef USE_LIBGLUSTERFS
        glusterfs_ctx_t *ctx = NULL;

        ctx = glusterfs_ctx_new ();
        if (!ctx)
                return ENOMEM;

        if (glusterfs_globals_init (ctx))
                return 1;

        THIS->ctx = ctx;
        ret = default_mem_acct_init (THIS);
        if (ret) {
                fprintf (stderr, "internal error: mem accounting failed\n");
                return 1;
        }
#endif

        evas = getenv (_GLUSTERD_CALLED_);
        if (evas && strcmp (evas, "1") == 0)
                /* OK, we know glusterd called us, no need to look for further config
                 *...although this conclusion should not inherit to our children
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

        if (argc == -1) {
                fprintf (stderr, "internal error\n");
                return 1;
        }

        if (setenv (_GSYNCD_DISPATCHED_, "1", 1) == -1) {
                fprintf (stderr, "internal error\n");
                goto out;
        }


        b = basename (argv[0]);
        for (i = invocables; i->name; i++) {
                if (strcmp (b, i->name) == 0)
                        return i->invoker (argc, argv);
        }

        fprintf (stderr, "invoking %s in restricted SSH session is not allowed\n",
                 b);

out:
        for (j = 1; j < argc; j++)
                free(argv[j]);
        free(argv);
        return 1;
}
