/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __RUN_H__
#define __RUN_H__

#define RUN_PIPE -1

struct runner {
        char **argv;
        unsigned argvlen;
        int runerr;
        pid_t chpid;
        int chfd[3];
        FILE *chio[3];
};

typedef struct runner runner_t;

/**
 * initialize runner_t instance.
 *
 * @param runner pointer to runner_t instance
 */
void runinit (runner_t *runner);

/**
 * get FILE pointer to which child's stdio is redirected.
 *
 * @param runner pointer to runner_t instance
 * @param fd specifies which standard file descriptor is
 *        is asked for
 *
 * @see runner_redir()
 */
FILE *runner_chio (runner_t *runner, int fd);

/**
 * add an argument.
 *
 * 'arg' is duplicated.
 *
 * Errors are deferred, no error handling is necessary.
 *
 * @param runner pointer to runner_t instance
 * @param arg    command line argument
 */
void runner_add_arg (runner_t *runner, const char *arg);

/**
 * add a sequence of arguments.
 *
 * Variadic function, calls runner_add_arg() on each vararg.
 * Argument sequence MUST be NULL terminated.
 *
 * Errors are deferred, no error handling is necessary.
 *
 * @param runner pointer to runner_t instance
 *
 * @see runner_add_arg()
 */
void runner_add_args (runner_t *runner, ...);

/**
 * add an argument with printf style formatting.
 *
 * Errors are deferred, no error handling is necessary.
 *
 * @param runner pointer to runner_t instance
 * @param format printf style format specifier
 */
void runner_argprintf (runner_t *runner, const char *format, ...);

/**
 * log a message about the command to be run.
 *
 * @param runner  pointer to runner_t instance
 *
 * @param dom  log domain
 * @param lvl  log level
 * @param msg  message with which the command is prefixed in log
 *
 * @see gf_log()
 */
void runner_log (runner_t *runner, const char *dom, gf_loglevel_t lvl,
                 const char *msg);

/**
 * set up redirection for child.
 *
 * @param runner  pointer to runner_t instance
 *
 * @param fd      fd of child to redirect (0, 1, or 2)
 * @param tgt_fd  fd on parent side to redirect to.
 *                Note that runner_end() will close tgt_fd,
 *                if user needs it in another context it should
 *                be dup'd beforehand.
 *                RUN_PIPE can be used for requiring a
 *                pipe from child to parent. The FILE
 *                created for this purpose will be
 *                accessible via runner_chio() (after
 *                runner_start() has been invoked).
 *
 * @see  runner_start(), dup(2), runner_chio(), runner_start()
 */
void
runner_redir (runner_t *runner, int fd, int tgt_fd);

/**
 * spawn child with accumulated arg list.
 *
 * @param runner  pointer to runner_t instance
 *
 * @return  0 on successful spawn
 *          -1 on failure (either due to earlier errors or execve(2) failing)
 *
 * @see runner_cout()
 */
int runner_start (runner_t *runner);

/**
 * complete operation and free resources.
 *
 * If child exists, waits for it. Redirections will be closed.
 * Dynamically allocated memory shall be freed.
 *
 * @param runner  pointer to runner_t instance
 *
 * @return  0 if child terminated successfully
 *          -1 if there is no running child
 *          n > 0 if child failed; value to be interpreted as status
 *                in waitpid(2)
 *
 * @see waitpid(2)
 */
int runner_end (runner_t *runner);

/**
 * variant of runner_end() which does not free internal data
 * so that the runner instance can be run again.
 *
 * @see runner_end()
 */
int runner_end_reuse (runner_t *runner);

/**
 * spawn and child, take it to completion and free resources.
 *
 * Essentially it's a concatenation of runner_start() and runner_end()
 * with simplified return semantics.
 *
 * @param runner  pointer to runner_t instance
 *
 * @return  0 on success
 *          -1 on failuire
 *
 * @see runner_start(), runner_end()
 */
int runner_run (runner_t *runner);

/**
 * variant for runner_run() which does not wait for acknowledgement
 * from child, and always assumes it succeeds.
 */
int runner_run_nowait (runner_t *runner);

/**
 * variant of runner_run() which does not free internal data
 * so that the runner instance can be run again.
 *
 * @see runner_run()
 */
int runner_run_reuse (runner_t *runner);

/**
 * run a command with args.
 *
 * Variadic function, child process is spawned with
 * the given sequence of args and waited for.
 * Argument sequence MUST be NULL terminated.
 *
 * @return 0 on success
 *         -1 on failure
 */
int runcmd (const char *arg, ...);

#endif
