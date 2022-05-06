/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

#include "cli-cmd.h"
#include "cli-mem-types.h"

#include <fnmatch.h>

static int cmd_done;
static int cmd_sent;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t cond_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t conn = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t conn_mutex = PTHREAD_MUTEX_INITIALIZER;

int cli_op_ret = 0;
static gf_boolean_t connected = _gf_false;

static unsigned
cli_cmd_needs_connection(struct cli_state *state, struct cli_cmd_word *word)
{
    if (!strcasecmp("quit", word->word))
        return 0;

    if (!strcasecmp("help", word->word))
        return 0;

    if (!strcasecmp("getwd", word->word))
        return 1;

    if (!strcasecmp("exit", word->word))
        return 0;

    return state->default_conn_timeout;
}

int
cli_cmd_status_reset(void)
{
    int ret = 0;

    ret = cli_cmd_lock();
    {
        if (ret == 0) {
            cmd_sent = 0;
            cmd_done = 0;
        }
    }
    ret = cli_cmd_unlock();
    return ret;
}

int
cli_cmd_sent_status_get(int *status)
{
    int ret = 0;
    GF_ASSERT(status);

    ret = cli_cmd_lock();
    {
        if (ret == 0)
            *status = cmd_sent;
    }
    ret = cli_cmd_unlock();
    return ret;
}

int
cli_cmd_process(struct cli_state *state, int argc, char **argv)
{
    int ret = 0;
    struct cli_cmd_word *word = NULL;
    struct cli_cmd_word *next = NULL;
    int i = 0;

    word = &state->tree.root;

    if (!argc)
        return 0;

    for (i = 0; i < argc; i++) {
        next = cli_cmd_nextword(word, argv[i]);

        word = next;
        if (!word)
            break;

        if (word->cbkfn)
            break;
    }

    if (!word) {
        cli_out("unrecognized word: %s (position %d)\n", argv[i], i);
        usage();
        return -1;
    }

    if (!word->cbkfn) {
        cli_out("unrecognized command\n");
        usage();
        return -1;
    }

    if (strcmp(word->word, "help") == 0)
        goto callback;

    state->await_connected = cli_cmd_needs_connection(state, word);

    ret = cli_cmd_await_connected(state->await_connected);
    if (ret) {
        cli_out(
            "Connection failed. Please check if gluster "
            "daemon is operational.");
        gf_log("", GF_LOG_INFO, "Exiting with: %d", ret);
        exit(ret);
    }

callback:
    ret = word->cbkfn(state, word, (const char **)argv, argc);
    (void)cli_cmd_status_reset();
    return ret;
}

int
cli_cmd_input_token_count(const char *text)
{
    int count = 0;
    const char *trav = NULL;
    int is_spc = 1;

    for (trav = text; *trav; trav++) {
        if (*trav == ' ') {
            is_spc = 1;
        } else {
            if (is_spc) {
                count++;
                is_spc = 0;
            }
        }
    }

    return count;
}

int
cli_cmd_process_line(struct cli_state *state, const char *text)
{
    int count = 0;
    char **tokens = NULL;
    char **tokenp = NULL;
    char *token = NULL;
    char *copy = NULL;
    char *saveptr = NULL;
    int i = 0;
    int ret = -1;

    count = cli_cmd_input_token_count(text);

    tokens = calloc(count + 1, sizeof(*tokens));
    if (!tokens)
        return -1;

    copy = strdup(text);
    if (!copy)
        goto out;

    tokenp = tokens;

    for (token = strtok_r(copy, " \t\r\n", &saveptr); token;
         token = strtok_r(NULL, " \t\r\n", &saveptr)) {
        *tokenp = strdup(token);

        if (!*tokenp)
            goto out;
        tokenp++;
        i++;
    }

    ret = cli_cmd_process(state, count, tokens);
out:
    free(copy);

    if (tokens)
        cli_cmd_tokens_destroy(tokens);

    return ret;
}

int
cli_cmds_register(struct cli_state *state)
{
    int ret = 0;

    ret = cli_cmd_volume_register(state);
    if (ret)
        goto out;

    ret = cli_cmd_probe_register(state);
    if (ret)
        goto out;

    ret = cli_cmd_system_register(state);
    if (ret)
        goto out;

    ret = cli_cmd_misc_register(state);
    if (ret)
        goto out;

    ret = cli_cmd_snapshot_register(state);
    if (ret)
        goto out;
    ret = cli_cmd_global_register(state);
    if (ret)
        goto out;
out:
    return ret;
}

int
cli_cmd_lock()
{
    pthread_mutex_lock(&cond_mutex);
    return 0;
}

int
cli_cmd_unlock()
{
    pthread_mutex_unlock(&cond_mutex);
    return 0;
}

static void
seconds_from_now(time_t secs, struct timespec *ts)
{
    timespec_now_realtime(ts);
    ts->tv_sec += secs;
}

int
cli_cmd_await_response(time_t time)
{
    struct timespec ts = {
        0,
    };
    int ret = 0;

    cli_op_ret = -1;

    seconds_from_now(time, &ts);
    while (!cmd_done && !ret) {
        ret = pthread_cond_timedwait(&cond, &cond_mutex, &ts);
    }

    if (!cmd_done) {
        if (ret == ETIMEDOUT)
            cli_out("Error : Request timed out");
        else
            cli_out("Error : Command returned with error code:%d", ret);
    }
    cmd_done = 0;

    return cli_op_ret;
}

/* This function must be called _only_ after all actions associated with
 * command processing is complete. Otherwise, gluster process may exit before
 * reporting results to stdout/stderr. */
int
cli_cmd_broadcast_response(int32_t status)
{
    pthread_mutex_lock(&cond_mutex);
    {
        if (!cmd_sent)
            goto out;
        cmd_done = 1;
        cli_op_ret = status;
        pthread_cond_broadcast(&cond);
    }

out:
    pthread_mutex_unlock(&cond_mutex);
    return 0;
}

int32_t
cli_cmd_await_connected(unsigned conn_timo)
{
    int32_t ret = 0;
    struct timespec ts = {
        0,
    };

    if (!conn_timo)
        return 0;

    pthread_mutex_lock(&conn_mutex);
    {
        seconds_from_now(conn_timo, &ts);
        while (!connected && !ret) {
            ret = pthread_cond_timedwait(&conn, &conn_mutex, &ts);
        }
    }
    pthread_mutex_unlock(&conn_mutex);

    return ret;
}

int32_t
cli_cmd_broadcast_connected(gf_boolean_t status)
{
    pthread_mutex_lock(&conn_mutex);
    {
        connected = status;
        pthread_cond_broadcast(&conn);
    }
    pthread_mutex_unlock(&conn_mutex);

    return 0;
}

gf_boolean_t
cli_cmd_connected(void)
{
    gf_boolean_t status;

    pthread_mutex_lock(&conn_mutex);
    {
        status = connected;
    }
    pthread_mutex_unlock(&conn_mutex);

    return status;
}

int
cli_cmd_submit(struct rpc_clnt *rpc, void *req, call_frame_t *frame,
               rpc_clnt_prog_t *prog, int procnum, xlator_t *this,
               fop_cbk_fn_t cbkfn, xdrproc_t xdrproc)
{
    int ret = -1;
    time_t timeout = 0;
    struct cli_state *state = frame->this->private;

    if ((GLUSTER_CLI_PROFILE_VOLUME == procnum) ||
        (GLUSTER_CLI_HEAL_VOLUME == procnum) ||
        (GLUSTER_CLI_GANESHA == procnum))
        timeout = CLI_TEN_MINUTES_TIMEOUT;
    else
        timeout = state->default_conn_timeout;

    cli_cmd_lock();
    cmd_sent = 0;
    ret = cli_submit_request(rpc, req, frame, prog, procnum, this, cbkfn,
                             xdrproc);

    if (!ret) {
        cmd_sent = 1;
        ret = cli_cmd_await_response(timeout);
    }

    cli_cmd_unlock();

    gf_log("cli", GF_LOG_DEBUG, "Returning %d", ret);
    return ret;
}

int
cli_cmd_pattern_cmp(const void *a, const void *b)
{
    const struct cli_cmd *ia = (const struct cli_cmd *)a;
    const struct cli_cmd *ib = (const struct cli_cmd *)b;

    return strcmp(ia->pattern, ib->pattern);
}

void
cli_cmd_sort(struct cli_cmd *cmd, int count)
{
    qsort(cmd, count - 1, sizeof(struct cli_cmd), cli_cmd_pattern_cmp);
}
