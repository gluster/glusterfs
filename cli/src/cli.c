/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/file.h>
#include <netdb.h>
#include <signal.h>
#include <libgen.h>

#include <sys/utsname.h>

#include <stdint.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <semaphore.h>
#include <errno.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#ifdef HAVE_MALLOC_STATS
#ifdef DEBUG
#include <mcheck.h>
#endif
#endif

#include "cli.h"
#include "cli-cmd.h"
#include "cli-mem-types.h"

#include "xlator.h"
#include "glusterfs.h"
#include "compat.h"
#include "logging.h"
#include "dict.h"
#include "list.h"
#include "timer.h"
#include "stack.h"
#include "revision.h"
#include "common-utils.h"
#include "event.h"
#include "globals.h"
#include "syscall.h"
#include "call-stub.h"
#include <fnmatch.h>

extern int connected;
/* using argp for command line parsing */
static char gf_doc[] = "";

static char argp_doc[] = "";

const char *argp_program_version = ""                                 \
        PACKAGE_NAME" "PACKAGE_VERSION" built on "__DATE__" "__TIME__ \
        "\nRepository revision: " GLUSTERFS_REPOSITORY_REVISION "\n"  \
        "Copyright (c) 2006-2010 Gluster Inc. "                       \
        "<http://www.gluster.com>\n"                                  \
        "GlusterFS comes with ABSOLUTELY NO WARRANTY.\n"              \
        "You may redistribute copies of GlusterFS under the terms of "\
        "the GNU Affero General Public License.";

const char *argp_program_bug_address = "<" PACKAGE_BUGREPORT ">";

static struct argp_option gf_options[] = {
        {0, 0, 0, 0, "Basic options:"},
        {0, }
};

struct rpc_clnt *global_rpc;

rpc_clnt_prog_t *cli_rpc_prog;


extern struct rpc_clnt_program cli_prog;

static error_t
parse_opts (int key, char *arg, struct argp_state *argp_state)
{
        struct cli_state  *state = NULL;
        char             **argv = NULL;

        state = argp_state->input;

        switch (key) {
        case ARGP_KEY_ARG:
                if (!state->argc) {
                        argv = calloc (state->argc + 2,
                                       sizeof (*state->argv));
                } else {
                        argv = realloc (state->argv, (state->argc + 2) *
                                        sizeof (*state->argv));
                }
                if (!argv)
                        return -1;

                state->argv = argv;

                argv[state->argc] = strdup (arg);
                if (!argv[state->argc])
                        return -1;
                state->argc++;
                argv[state->argc] = NULL;

                break;
        }

        return 0;
}



static char *
generate_uuid ()
{
        char           tmp_str[1024] = {0,};
        char           hostname[256] = {0,};
        struct timeval tv = {0,};
        struct tm      now = {0, };
        char           now_str[32];

        if (gettimeofday (&tv, NULL) == -1) {
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "gettimeofday: failed %s",
                        strerror (errno));
        }

        if (gethostname (hostname, 256) == -1) {
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "gethostname: failed %s",
                        strerror (errno));
        }

        localtime_r (&tv.tv_sec, &now);
        strftime (now_str, 32, "%Y/%m/%d-%H:%M:%S", &now);
        snprintf (tmp_str, 1024, "%s-%d-%s:%"
#ifdef GF_DARWIN_HOST_OS
                  PRId32,
#else
                  "ld",
#endif
                  hostname, getpid(), now_str, tv.tv_usec);

        return gf_strdup (tmp_str);
}

static int
glusterfs_ctx_defaults_init (glusterfs_ctx_t *ctx)
{
        cmd_args_t    *cmd_args = NULL;
        struct rlimit  lim = {0, };
        call_pool_t   *pool = NULL;

        xlator_mem_acct_init (THIS, cli_mt_end);

        ctx->process_uuid = generate_uuid ();
        if (!ctx->process_uuid)
                return -1;

        ctx->page_size  = 128 * GF_UNIT_KB;

        ctx->iobuf_pool = iobuf_pool_new (8 * GF_UNIT_MB, ctx->page_size);
        if (!ctx->iobuf_pool)
                return -1;

        ctx->event_pool = event_pool_new (DEFAULT_EVENT_POOL_SIZE);
        if (!ctx->event_pool)
                return -1;

        pool = GF_CALLOC (1, sizeof (call_pool_t),
                          cli_mt_call_pool_t);
        if (!pool)
                return -1;

        /* frame_mem_pool size 112 * 16k */
        pool->frame_mem_pool = mem_pool_new (call_frame_t, 16384);

        if (!pool->frame_mem_pool)
                return -1;

        /* stack_mem_pool size 256 * 8k */
        pool->stack_mem_pool = mem_pool_new (call_stack_t, 8192); 

        if (!pool->stack_mem_pool)
                return -1;

        ctx->stub_mem_pool = mem_pool_new (call_stub_t, 1024);
        if (!ctx->stub_mem_pool)
                return -1;

        INIT_LIST_HEAD (&pool->all_frames);
        LOCK_INIT (&pool->lock);
        ctx->pool = pool;

        pthread_mutex_init (&(ctx->lock), NULL);

        cmd_args = &ctx->cmd_args;

        /* parsing command line arguments */
        cmd_args->log_file  = "/dev/null";
        cmd_args->log_level = GF_LOG_NONE;

        INIT_LIST_HEAD (&cmd_args->xlator_options);

        lim.rlim_cur = RLIM_INFINITY;
        lim.rlim_max = RLIM_INFINITY;
        setrlimit (RLIMIT_CORE, &lim);

        return 0;
}


static int
logging_init (glusterfs_ctx_t *ctx)
{
        cmd_args_t *cmd_args = NULL;

        cmd_args = &ctx->cmd_args;

        if (gf_log_init (cmd_args->log_file) == -1) {
                fprintf (stderr,
                         "failed to open logfile %s.  exiting\n",
                         cmd_args->log_file);
                return -1;
        }

        gf_log_set_loglevel (cmd_args->log_level);

        return 0;
}

int
cli_submit_request (void *req, call_frame_t *frame,
                    rpc_clnt_prog_t *prog,
                    int procnum, struct iobref *iobref,
                    cli_serialize_t sfunc, xlator_t *this,
                    fop_cbk_fn_t cbkfn)
{
        int                     ret         = -1;
        int                     count      = 0;
        char                    start_ping = 0;
        struct iovec            iov         = {0, };
        struct iobuf            *iobuf = NULL;
        char                    new_iobref = 0;

        GF_ASSERT (this);

        iobuf = iobuf_get (this->ctx->iobuf_pool);
        if (!iobuf) {
                goto out;
        };

        if (!iobref) {
                iobref = iobref_new ();
                if (!iobref) {
                        goto out;
                }

                new_iobref = 1;
        }

        iobref_add (iobref, iobuf);

        iov.iov_base = iobuf->ptr;
        iov.iov_len  = 128 * GF_UNIT_KB;


        /* Create the xdr payload */
        if (req && sfunc) {
                ret = sfunc (iov, req);
                if (ret == -1) {
                        goto out;
                }
                iov.iov_len = ret;
                count = 1;
        }

        /* Send the msg */
        ret = rpc_clnt_submit (global_rpc, prog, procnum, cbkfn,
                               &iov, count,
                               NULL, 0, iobref, frame, NULL, 0, NULL, 0, NULL);

        if (ret == 0) {
                pthread_mutex_lock (&global_rpc->conn.lock);
                {
                        if (!global_rpc->conn.ping_started) {
                                start_ping = 1;
                        }
                }
                pthread_mutex_unlock (&global_rpc->conn.lock);
        }

        ret = 0;

out:
        if (new_iobref)
                iobref_unref (iobref);
        return ret;
}

int
cli_rpc_notify (struct rpc_clnt *rpc, void *mydata, rpc_clnt_event_t event,
                void *data)
{
        xlator_t                *this = NULL;
        int                     ret = 0;

        this = mydata;

        switch (event) {
        case RPC_CLNT_CONNECT:
        {

                cli_cmd_broadcast_connected ();
                gf_log (this->name, GF_LOG_TRACE, "got RPC_CLNT_CONNECT");
               break;
        }

        case RPC_CLNT_DISCONNECT:
        {
                gf_log (this->name, GF_LOG_TRACE, "got RPC_CLNT_DISCONNECT");
                connected = 0;
                break;
        }

        default:
                gf_log (this->name, GF_LOG_TRACE,
                        "got some other RPC event %d", event);
                ret = 0;
                break;
        }

        return ret;
}

int
cli_opt_parse (char *opt, struct cli_state *state)
{
        char *oarg;

        oarg = strtail (opt, "mode=");
        if (oarg) {
                if (strcmp (oarg, "script") == 0) {
                        state->mode |= GLUSTER_MODE_SCRIPT;
                        return 0;
                }
                if (strcmp (oarg, "interactive") == 0)
                        return 0;
                return -1;
        }

        oarg = strtail (opt, "remote-host=");
        if (oarg) {
                state->remote_host = oarg;
                return 0;
        }

        return -1;
}

int
parse_cmdline (int argc, char *argv[], struct cli_state *state)
{
        int         ret = 0;
        int         i = 0;
        int         j = 0;
        char        *opt = NULL;
        struct argp argp = { 0,};

        argp.options    = gf_options;
        argp.parser     = parse_opts;
        argp.args_doc   = argp_doc;
        argp.doc        = gf_doc;

        for (i = 0; i < argc; i++) {
                opt = strtail (argv[i], "--");
                if (opt) {
                        ret = cli_opt_parse (opt, state);
                        if (ret == -1) {
                                break;
                        }
                        for (j = i; j < argc - 1; j++)
                                argv[j] = argv[j + 1];
                        argc--;
                }
        }

        ret = argp_parse (&argp, argc, argv,
                          ARGP_IN_ORDER, NULL, state);

        return ret;
}


int
cli_cmd_tree_init (struct cli_cmd_tree *tree)
{
        struct cli_cmd_word  *root = NULL;
        int                   ret = 0;

        root = &tree->root;
        root->tree = tree;

        return ret;
}


int
cli_state_init (struct cli_state *state)
{
        struct cli_cmd_tree  *tree = NULL;
        int                   ret = 0;


        state->remote_host = "localhost";

        tree = &state->tree;
        tree->state = state;

        ret = cli_cmd_tree_init (tree);

        return ret;
}

int
cli_usage_out (const char *usage)
{
        GF_ASSERT (usage);
        GF_ASSERT (usage[0] != '\0');

        if (!usage || usage[0] == '\0')
                return -1;

        cli_out ("Usage: %s", usage);
        return 0;
}

int
cli_out (const char *fmt, ...)
{
        struct cli_state *state = NULL;
        va_list           ap;
        int               ret = 0;

        state = global_state;

        va_start (ap, fmt);

#ifdef HAVE_READLINE
        if (state->rl_enabled && !state->rl_processing)
                return cli_rl_out(state, fmt, ap);
#endif

        ret = vprintf (fmt, ap);
        printf ("\n");
        va_end (ap);

        return ret;
}

struct rpc_clnt *
cli_rpc_init (struct cli_state *state)
{
        struct rpc_clnt         *rpc = NULL;
        struct rpc_clnt_config  rpc_cfg = {0,};
        dict_t                  *options = NULL;
        int                     ret = -1;
        int                     port = CLI_GLUSTERD_PORT;
        xlator_t                *this = NULL;


        this = THIS;
        cli_rpc_prog = &cli_prog;
        options = dict_new ();
        if (!options)
                goto out;

        ret = dict_set_str (options, "remote-host", state->remote_host);
        if (ret)
                goto out;

        if (state->remote_port)
                port = state->remote_port;

        rpc_cfg.remote_host = state->remote_host;
        rpc_cfg.remote_port = port;

        ret = dict_set_int32 (options, "remote-port", port);
        if (ret)
                goto out;

        ret = dict_set_str (options, "transport.address-family", "inet");
        if (ret)
                goto out;

        rpc = rpc_clnt_new (&rpc_cfg, options, this->ctx, this->name);

        if (!rpc)
                goto out;

        ret = rpc_clnt_register_notify (rpc, cli_rpc_notify, this);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "failed to register notify");
                goto out;
        }

        rpc_clnt_start (rpc);
out:
        return rpc;
}

cli_local_t *
cli_local_get ()
{
        cli_local_t     *local = NULL;

        local = GF_CALLOC (1, sizeof (*local), cli_mt_cli_local_t);

        return local;
}

void
cli_local_wipe (cli_local_t *local)
{
        if (local) {
                GF_FREE (local);
        }

        return;
}

void
cli_path_strip_trailing_slashes (char *path)
{
        int i = 0;
        int len = 0;

        if (!path)
                return;

        len = strlen (path);
        for (i = len - 1; i > 0 ; i--) {
                if (path[i] != '/')
                        break;

        }

        if (i < (len - 1))
                path[i + 1] = '\0';
}

struct cli_state *global_state;

int
main (int argc, char *argv[])
{
        struct cli_state   state = {0, };
        int                ret = -1;
        glusterfs_ctx_t   *ctx = NULL;

        ret = glusterfs_globals_init ();
        if (ret)
                return ret;

        ctx = glusterfs_ctx_get ();
        if (!ctx)
                return ENOMEM;

        ret = glusterfs_ctx_defaults_init (ctx);
        if (ret)
                goto out;

        ret = cli_state_init (&state);
        if (ret)
                goto out;

        state.ctx = ctx;
        global_state = &state;

        ret = parse_cmdline (argc, argv, &state);
        if (ret)
                goto out;

        if (geteuid ()) {
                printf ("Only super user can run this command\n");
                return EPERM;
        }

        global_rpc = cli_rpc_init (&state);
        if (!global_rpc)
                goto out;

        ret = logging_init (ctx);
        if (ret)
                goto out;

        ret = cli_cmds_register (&state);
        if (ret)
                goto out;

        ret = cli_cmd_cond_init ();
        if (ret)
                goto out;

        ret = cli_input_init (&state);
        if (ret)
                goto out;

        ret = event_dispatch (ctx->event_pool);

out:
//        glusterfs_ctx_destroy (ctx);

        return ret;
}
