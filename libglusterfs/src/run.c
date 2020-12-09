/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <assert.h>
#include <signal.h>
#include <sys/wait.h>
#include <spawn.h>
#include "glusterfs/syscall.h"

extern char **environ;
/*
 * Following defines are available for helping development:
 * RUN_STANDALONE and RUN_DO_DEMO.
 *
 * Compiling a standalone object file with no dependencies
 * on glusterfs:
 * $ cc -DRUN_STANDALONE -c run.c
 *
 * Compiling a demo program that exercises bits of run.c
 * functionality (linking to glusterfs):
 * $ cc -DRUN_DO_DEMO -orun run.c  `pkg-config --libs --cflags glusterfs-api`
 *
 * Compiling a demo program that exercises bits of run.c
 * functionality (with no dependence on glusterfs):
 *
 * $ cc -DRUN_DO_DEMO -DRUN_STANDALONE -orun run.c
 */
#if defined(RUN_STANDALONE) || defined(RUN_DO_DEMO)
int
close_fds_except_custom(int *fdv, size_t count, void *prm,
                        void closer(int fd, void *prm));
#define sys_read(f, b, c) read(f, b, c)
#define sys_write(f, b, c) write(f, b, c)
#define sys_close(f) close(f)
#define GF_CALLOC(n, s, t) calloc(n, s)
#define GF_ASSERT(cond) assert(cond)
#define GF_REALLOC(p, s) realloc(p, s)
#define GF_FREE(p) free(p)
#define gf_strdup(s) strdup(s)
#define gf_vasprintf(p, f, va) vasprintf(p, f, va)
#define gf_loglevel_t int
#define gf_msg_callingfn(dom, level, errnum, msgid, fmt, args...)              \
    printf("LOG: " fmt "\n", ##args)
#define LOG_DEBUG 0
#ifdef RUN_STANDALONE
#include <stdbool.h>
#include <sys/resource.h>
int
close_fds_except_custom(int *fdv, size_t count, void *prm,
                        void closer(int fd, void *prm))
{
    int i = 0;
    size_t j = 0;
    bool should_close = true;
    struct rlimit rl;
    int ret = -1;

    ret = getrlimit(RLIMIT_NOFILE, &rl);
    if (ret)
        return ret;

    for (i = 0; i < rl.rlim_cur; i++) {
        should_close = true;
        for (j = 0; j < count; j++) {
            if (i == fdv[j]) {
                should_close = false;
                break;
            }
        }
        if (should_close)
            closer(i, prm);
    }
    return 0;
}
#endif
#ifdef __linux__
#define GF_LINUX_HOST_OS
#endif
#else /* ! RUN_STANDALONE || RUN_DO_DEMO */
#include "glusterfs/glusterfs.h"
#include "glusterfs/common-utils.h"
#include "glusterfs/libglusterfs-messages.h"
#endif

#include "glusterfs/run.h"
/* Using a fake/temporary fd i.e, 0, to safely close the
   open fds within 3 to MAX_FD by remapping the
   target fd. Otherwise, it leads to undefined reference
   in memory while closing them.
*/

static void
closer_posix_spawnp(int fd, void *prm)
{
    posix_spawn_file_actions_t *file_actionsp = prm;
    posix_spawn_file_actions_adddup2(file_actionsp, 0, fd);
    posix_spawn_file_actions_addclose(file_actionsp, fd);
}
void
runinit(runner_t *runner)
{
    int i = 0;

    runner->argvlen = 64;
    runner->argv = GF_CALLOC(runner->argvlen, sizeof(*runner->argv),
                             gf_common_mt_run_argv);
    runner->runerr = runner->argv ? 0 : errno;
    runner->chpid = -1;
    for (i = 0; i < 3; i++) {
        runner->chfd[i] = -1;
        runner->chio[i] = NULL;
    }
}

FILE *
runner_chio(runner_t *runner, int fd)
{
    GF_ASSERT(fd > 0 && fd < 3);

    if ((fd > 0) && (fd < 3))
        return runner->chio[fd];

    return NULL;
}

static void
runner_insert_arg(runner_t *runner, char *arg)
{
    int i = 0;

    GF_ASSERT(arg);

    if (runner->runerr || !runner->argv)
        return;

    for (i = 0; i < runner->argvlen; i++) {
        if (runner->argv[i] == NULL)
            break;
    }
    GF_ASSERT(i < runner->argvlen);

    if (i == runner->argvlen - 1) {
        runner->argv = GF_REALLOC(runner->argv,
                                  runner->argvlen * 2 * sizeof(*runner->argv));
        if (!runner->argv) {
            runner->runerr = errno;
            return;
        }
        memset(/* "+" is aware of the type of its left side,
                * no need to multiply with type-size */
               runner->argv + runner->argvlen, 0,
               runner->argvlen * sizeof(*runner->argv));
        runner->argvlen *= 2;
    }

    runner->argv[i] = arg;
}

void
runner_add_arg(runner_t *runner, const char *arg)
{
    arg = gf_strdup(arg);
    if (!arg) {
        runner->runerr = errno;
        return;
    }

    runner_insert_arg(runner, (char *)arg);
}

static void
runner_va_add_args(runner_t *runner, va_list argp)
{
    const char *arg;

    while ((arg = va_arg(argp, const char *)))
        runner_add_arg(runner, arg);
}

void
runner_add_args(runner_t *runner, ...)
{
    va_list argp;

    va_start(argp, runner);
    runner_va_add_args(runner, argp);
    va_end(argp);
}

void
runner_argprintf(runner_t *runner, const char *format, ...)
{
    va_list argva;
    char *arg = NULL;
    int ret = 0;

    va_start(argva, format);
    ret = gf_vasprintf(&arg, format, argva);
    va_end(argva);

    if (ret < 0) {
        runner->runerr = errno;
        return;
    }

    runner_insert_arg(runner, arg);
}

void
runner_log(runner_t *runner, const char *dom, gf_loglevel_t lvl,
           const char *msg)
{
    char *buf = NULL;
    size_t len = 0;
    int i = 0;

    if (runner->runerr)
        return;

    for (i = 0;; i++) {
        if (runner->argv[i] == NULL)
            break;
        len += (strlen(runner->argv[i]) + 1);
    }

    buf = GF_CALLOC(1, len + 1, gf_common_mt_run_logbuf);
    if (!buf) {
        runner->runerr = errno;
        return;
    }
    for (i = 0;; i++) {
        if (runner->argv[i] == NULL)
            break;
        strcat(buf, runner->argv[i]);
        strcat(buf, " ");
    }
    if (len > 0)
        buf[len - 1] = '\0';

    gf_msg_callingfn(dom, lvl, 0, LG_MSG_RUNNER_LOG, "%s: %s", msg, buf);

    GF_FREE(buf);
}

void
runner_redir(runner_t *runner, int fd, int tgt_fd)
{
    GF_ASSERT(fd > 0 && fd < 3);

    if ((fd > 0) && (fd < 3))
        runner->chfd[fd] = (tgt_fd >= 0) ? tgt_fd : -2;
}

int
runner_start(runner_t *runner)
{
    int pi[3][2] = {{-1, -1}, {-1, -1}, {-1, -1}};
    int xpi[2];
    int ret = 0;
    int errno_priv = 0;
    int i = 0;
    int status = 0;
    sigset_t set;

    posix_spawnattr_t attr;
    posix_spawnattr_t *attrp = NULL;
    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_t *file_actionsp = NULL;

    if (runner->runerr || !runner->argv) {
        errno = (runner->runerr) ? runner->runerr : EINVAL;
        return -1;
    }

    GF_ASSERT(runner->argv[0]);

    /* set up a channel to child to communicate back
     * possible execve(2) failures
     */
    ret = pipe(xpi);
    if (ret != -1)
        ret = fcntl(xpi[1], F_SETFD, FD_CLOEXEC);

    for (i = 0; i < 3; i++) {
        if (runner->chfd[i] != -2)
            continue;
        ret = pipe(pi[i]);
        if (ret != -1) {
            runner->chio[i] = fdopen(pi[i][i ? 0 : 1], i ? "r" : "w");
            if (!runner->chio[i])
                ret = -1;
        }
    }

    if (ret == -1)
        return -1;

    ret = posix_spawn_file_actions_init(&file_actions);
    if (ret != 0)
        return -1;

    for (i = 0; i < 3; i++)
        posix_spawn_file_actions_addclose(&file_actions, pi[i][i ? 0 : 1]);

    posix_spawn_file_actions_addclose(&file_actions, xpi[0]);
    ret = 0;
    for (i = 0; i < 3; i++) {
        if (ret == -1)
            return -1;
        switch (runner->chfd[i]) {
            case -1:
                // no redir
                break;
            case -2:
                // redir to pipe
                ret = posix_spawn_file_actions_adddup2(&file_actions,
                                                       pi[i][i ? 1 : 0], i);
                break;
            default:
                // redir to file
                ret = posix_spawn_file_actions_adddup2(&file_actions,
                                                       runner->chfd[i], i);
        }
    }

    if (ret != -1) {
        int fdv[4] = {0, 1, 2, xpi[1]};

        ret = close_fds_except_custom(fdv, sizeof(fdv) / sizeof(*fdv),
                                      &file_actions, closer_posix_spawnp);
    }

    if (ret == -1)
        return -1;

    file_actionsp = &file_actions;

    ret = posix_spawnattr_init(&attr);
    if (ret != 0)
        return -1;
    ret = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);
    if (ret != 0)
        return -1;
    sigemptyset(&set);
    ret = posix_spawnattr_setsigmask(&attr, &set);
    if (ret != 0)
        return -1;

    attrp = &attr;

    status = posix_spawnp(&runner->chpid, runner->argv[0], file_actionsp, attrp,
                          runner->argv, environ);
    if (status != 0) {
        errno_priv = errno;
        sys_close(xpi[0]);
        sys_close(xpi[1]);
        for (i = 0; i < 3; i++) {
            sys_close(pi[i][0]);
            sys_close(pi[i][1]);
        }
        errno = errno_priv;
        return -1;
    }

    /* Destroy any objects that we created earlier */

    if (attrp != NULL) {
        ret = posix_spawnattr_destroy(attrp);
        if (ret != 0)
            return -1;
    }

    if (file_actionsp != NULL) {
        ret = posix_spawn_file_actions_destroy(file_actionsp);
        if (ret != 0)
            return -1;
    }

    ret = sys_write(xpi[1], &errno, sizeof(errno));
    errno_priv = errno;

    for (i = 0; i < 3; i++)
        sys_close(pi[i][i ? 1 : 0]);

    sys_close(xpi[1]);

    if (ret == -1) {
        for (i = 0; i < 3; i++) {
            if (runner->chio[i]) {
                fclose(runner->chio[i]);
                runner->chio[i] = NULL;
            }
        }
    } else {
        ret = sys_read(xpi[0], (char *)&errno_priv, sizeof(errno_priv));
        sys_close(xpi[0]);
        if (ret <= 0)
            return 0;
        GF_ASSERT(ret == sizeof(errno_priv));
    }
    errno = errno_priv;

    return status;
}

int
runner_end_reuse(runner_t *runner)
{
    int i = 0;
    int ret = 1;
    int chstat = 0;

    if (runner->chpid > 0) {
        if (waitpid(runner->chpid, &chstat, 0) == runner->chpid) {
            if (WIFEXITED(chstat)) {
                ret = WEXITSTATUS(chstat);
            } else {
                ret = chstat;
            }
        }
    }

    for (i = 0; i < 3; i++) {
        if (runner->chio[i]) {
            fclose(runner->chio[i]);
            runner->chio[i] = NULL;
        }
    }

    return -ret;
}

int
runner_end(runner_t *runner)
{
    int i = 0;
    int ret = -1;
    char **p = NULL;

    ret = runner_end_reuse(runner);

    if (runner->argv) {
        for (p = runner->argv; *p; p++)
            GF_FREE(*p);
        GF_FREE(runner->argv);
    }
    for (i = 0; i < 3; i++)
        sys_close(runner->chfd[i]);

    return ret;
}

static int
runner_run_generic(runner_t *runner, int (*rfin)(runner_t *runner))
{
    int ret = 0;

    ret = runner_start(runner);
    if (ret)
        goto out;
    ret = rfin(runner);

out:
    return ret;
}

int
runner_run(runner_t *runner)
{
    return runner_run_generic(runner, runner_end);
}

int
runner_run_nowait(runner_t *runner)
{
    int pid;

    pid = fork();

    if (!pid) {
        setsid();
        _exit(runner_start(runner));
    }

    if (pid > 0)
        runner->chpid = pid;
    return runner_end(runner);
}

int
runner_run_reuse(runner_t *runner)
{
    return runner_run_generic(runner, runner_end_reuse);
}

int
runcmd(const char *arg, ...)
{
    runner_t runner;
    va_list argp;

    runinit(&runner);
    /* ISO C requires a named argument before '...' */
    runner_add_arg(&runner, arg);

    va_start(argp, arg);
    runner_va_add_args(&runner, argp);
    va_end(argp);

    return runner_run(&runner);
}

#ifdef RUN_DO_DEMO
static void
TBANNER(const char *txt)
{
    printf("######\n### demoing %s\n", txt);
}

int
main(int argc, char **argv)
{
    runner_t runner;
    char buf[80];
    char *wdbuf;
    ;
    int ret;
    int fd;
    long pathmax = pathconf("/", _PC_PATH_MAX);
    struct timeval tv = {
        0,
    };
    struct timeval *tvp = NULL;
    char *tfile;

    wdbuf = malloc(pathmax);
    assert(wdbuf);
    getcwd(wdbuf, pathmax);

    TBANNER("basic functionality: running \"echo a b\"");
    runcmd("echo", "a", "b", NULL);

    TBANNER("argv extension: running \"echo 1 2 ... 100\"");
    runcmd("echo", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11",
           "12", "13", "14", "15", "16", "17", "18", "19", "20", "21", "22",
           "23", "24", "25", "26", "27", "28", "29", "30", "31", "32", "33",
           "34", "35", "36", "37", "38", "39", "40", "41", "42", "43", "44",
           "45", "46", "47", "48", "49", "50", "51", "52", "53", "54", "55",
           "56", "57", "58", "59", "60", "61", "62", "63", "64", "65", "66",
           "67", "68", "69", "70", "71", "72", "73", "74", "75", "76", "77",
           "78", "79", "80", "81", "82", "83", "84", "85", "86", "87", "88",
           "89", "90", "91", "92", "93", "94", "95", "96", "97", "98", "99",
           "100", NULL);

    TBANNER(
        "add_args, argprintf, log, and popen-style functionality:\n"
        "    running a multiline echo command, emit a log about it,\n"
        "    redirect it to a pipe, read output lines\n"
        "    and print them prefixed with \"got: \"");
    runinit(&runner);
    runner_add_args(&runner, "echo", "pid:", NULL);
    runner_argprintf(&runner, "%d\n", getpid());
    runner_add_arg(&runner, "wd:");
    runner_add_arg(&runner, wdbuf);
    runner_redir(&runner, 1, RUN_PIPE);
    runner_start(&runner);
    runner_log(&runner, "(x)", LOG_DEBUG, "starting program");
    while (fgets(buf, sizeof(buf), runner_chio(&runner, 1)))
        printf("got: %s", buf);
    runner_end(&runner);

    TBANNER("execve error reporting: running a non-existent command");
    ret = runcmd("bafflavvitty", NULL);
    printf("%d %d [%s]\n", ret, errno, strerror(errno));

    TBANNER(
        "output redirection: running \"echo foo\" redirected "
        "to a temp file");
    tfile = strdup("/tmp/foofXXXXXX");
    assert(tfile);
    fd = mkstemp(tfile);
    assert(fd != -1);
    printf("redirecting to %s\n", tfile);
    runinit(&runner);
    runner_add_args(&runner, "echo", "foo", NULL);
    runner_redir(&runner, 1, fd);
    ret = runner_run(&runner);
    printf("runner_run returned: %d", ret);
    if (ret != 0)
        printf(", with errno %d [%s]", errno, strerror(errno));
    putchar('\n');

    /* sleep for seconds given as argument (0 means forever)
     * to allow investigation of post-execution state to
     * cbeck for resource leaks (eg. zombies).
     */
    if (argc > 1) {
        tv.tv_sec = strtoul(argv[1], NULL, 10);
        printf("### %s", "sleeping for");
        if (tv.tv_sec > 0) {
            printf(" %d seconds\n", tv.tv_sec);
            tvp = &tv;
        } else
            printf("%s\n", "ever");
        select(0, 0, 0, 0, tvp);
    }

    return 0;
}
#endif
