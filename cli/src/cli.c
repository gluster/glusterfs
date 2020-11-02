/*
   Copyright (c) 2010-2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
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

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "cli.h"
#include "cli-quotad-client.h"
#include "cli-cmd.h"
#include "cli-mem-types.h"

#include <glusterfs/xlator.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/compat.h>
#include <glusterfs/logging.h>
#include <glusterfs/dict.h>
#include <glusterfs/list.h>
#include <glusterfs/timer.h>
#include <glusterfs/stack.h>
#include <glusterfs/revision.h>
#include <glusterfs/common-utils.h>
#include <glusterfs/gf-event.h>
#include <glusterfs/syscall.h>
#include <glusterfs/call-stub.h>
#include <fnmatch.h>

#include "xdr-generic.h"

/* using argp for command line parsing */

const char *argp_program_version =
    "" PACKAGE_NAME " " PACKAGE_VERSION
    "\nRepository revision: " GLUSTERFS_REPOSITORY_REVISION
    "\n"
    "Copyright (c) 2006-2016 Red Hat, Inc. "
    "<https://www.gluster.org/>\n"
    "GlusterFS comes with ABSOLUTELY NO WARRANTY.\n"
    "It is licensed to you under your choice of the GNU Lesser\n"
    "General Public License, version 3 or any later version (LGPLv3\n"
    "or later), or the GNU General Public License, version 2 (GPLv2),\n"
    "in all cases as published by the Free Software Foundation.";
const char *argp_program_bug_address = "<" PACKAGE_BUGREPORT ">";

struct rpc_clnt *global_quotad_rpc;

struct rpc_clnt *global_rpc;

rpc_clnt_prog_t *cli_rpc_prog;

extern struct rpc_clnt_program cli_prog;

int cli_default_conn_timeout = 120;
int cli_ten_minutes_timeout = 600;

static int
glusterfs_ctx_defaults_init(glusterfs_ctx_t *ctx)
{
    cmd_args_t *cmd_args = NULL;
    struct rlimit lim = {
        0,
    };
    call_pool_t *pool = NULL;
    int ret = -1;

    if (!ctx)
        return ret;

    ret = xlator_mem_acct_init(THIS, cli_mt_end);
    if (ret != 0) {
        gf_log("cli", GF_LOG_ERROR, "Memory accounting init failed.");
        return ret;
    }

    /* Resetting ret to -1 to so in case of failure
     * we can relese allocated resource.
     */
    ret = -1;

    ctx->process_uuid = generate_glusterfs_ctx_id();
    if (!ctx->process_uuid) {
        gf_log("cli", GF_LOG_ERROR, "Failed to generate uuid.");
        goto out;
    }

    ctx->page_size = 128 * GF_UNIT_KB;

    ctx->iobuf_pool = iobuf_pool_new();
    if (!ctx->iobuf_pool) {
        gf_log("cli", GF_LOG_ERROR, "Failed to create iobuf pool.");
        goto out;
    }

    ctx->event_pool = gf_event_pool_new(DEFAULT_EVENT_POOL_SIZE,
                                        STARTING_EVENT_THREADS);
    if (!ctx->event_pool) {
        gf_log("cli", GF_LOG_ERROR, "Failed to create event pool.");
        goto out;
    }

    pool = GF_CALLOC(1, sizeof(call_pool_t), cli_mt_call_pool_t);
    if (!pool) {
        gf_log("cli", GF_LOG_ERROR, "Failed to create call pool.");
        goto out;
    }

    /* frame_mem_pool size 112 * 64 */
    pool->frame_mem_pool = mem_pool_new(call_frame_t, 32);
    if (!pool->frame_mem_pool) {
        gf_log("cli", GF_LOG_ERROR, "Failed to create frame mem pool.");
        goto out;
    }

    /* stack_mem_pool size 256 * 128 */
    pool->stack_mem_pool = mem_pool_new(call_stack_t, 16);

    if (!pool->stack_mem_pool) {
        gf_log("cli", GF_LOG_ERROR, "Failed to create stack mem pool.");
        goto out;
    }

    ctx->stub_mem_pool = mem_pool_new(call_stub_t, 16);
    if (!ctx->stub_mem_pool) {
        gf_log("cli", GF_LOG_ERROR, "Failed to stub mem pool.");
        goto out;
    }

    ctx->dict_pool = mem_pool_new(dict_t, 32);
    if (!ctx->dict_pool) {
        gf_log("cli", GF_LOG_ERROR, "Failed to create dict pool.");
        goto out;
    }

    ctx->dict_pair_pool = mem_pool_new(data_pair_t, 512);
    if (!ctx->dict_pair_pool) {
        gf_log("cli", GF_LOG_ERROR, "Failed to create dict pair pool.");
        goto out;
    }

    ctx->dict_data_pool = mem_pool_new(data_t, 512);
    if (!ctx->dict_data_pool) {
        gf_log("cli", GF_LOG_ERROR, "Failed to create dict data pool.");
        goto out;
    }

    ctx->logbuf_pool = mem_pool_new(log_buf_t, 256);
    if (!ctx->logbuf_pool) {
        gf_log("cli", GF_LOG_ERROR, "Failed to create logbuf pool.");
        goto out;
    }

    INIT_LIST_HEAD(&pool->all_frames);
    LOCK_INIT(&pool->lock);
    ctx->pool = pool;

    cmd_args = &ctx->cmd_args;

    INIT_LIST_HEAD(&cmd_args->xlator_options);

    lim.rlim_cur = RLIM_INFINITY;
    lim.rlim_max = RLIM_INFINITY;
    setrlimit(RLIMIT_CORE, &lim);

    ret = 0;

out:
    if (ret != 0) {
        if (pool) {
            mem_pool_destroy(pool->frame_mem_pool);
            mem_pool_destroy(pool->stack_mem_pool);
        }
        GF_FREE(pool);
        pool = NULL;
        GF_FREE(ctx->process_uuid);
        mem_pool_destroy(ctx->stub_mem_pool);
        mem_pool_destroy(ctx->dict_pool);
        mem_pool_destroy(ctx->dict_pair_pool);
        mem_pool_destroy(ctx->dict_data_pool);
        mem_pool_destroy(ctx->logbuf_pool);
    }

    return ret;
}

static int
logging_init(glusterfs_ctx_t *ctx, struct cli_state *state)
{
    char *log_file = state->log_file ? state->log_file
                                     : DEFAULT_CLI_LOG_FILE_DIRECTORY
                         "/cli.log";

    /* passing ident as NULL means to use default ident for syslog */
    if (gf_log_init(ctx, log_file, NULL) == -1) {
        fprintf(stderr, "ERROR: failed to open logfile %s\n", log_file);
    }

    /* CLI should not have something to DEBUG after the release,
       hence defaulting to INFO loglevel */
    gf_log_set_loglevel(ctx, (state->log_level == GF_LOG_NONE)
                                 ? GF_LOG_INFO
                                 : state->log_level);

    return 0;
}

int
cli_submit_request(struct rpc_clnt *rpc, void *req, call_frame_t *frame,
                   rpc_clnt_prog_t *prog, int procnum, struct iobref *iobref,
                   xlator_t *this, fop_cbk_fn_t cbkfn, xdrproc_t xdrproc)
{
    int ret = -1;
    int count = 0;
    struct iovec iov = {
        0,
    };
    struct iobuf *iobuf = NULL;
    char new_iobref = 0;
    ssize_t xdr_size = 0;

    GF_ASSERT(this);

    if (req) {
        xdr_size = xdr_sizeof(xdrproc, req);
        iobuf = iobuf_get2(this->ctx->iobuf_pool, xdr_size);
        if (!iobuf) {
            goto out;
        };

        if (!iobref) {
            iobref = iobref_new();
            if (!iobref) {
                goto out;
            }

            new_iobref = 1;
        }

        iobref_add(iobref, iobuf);

        iov.iov_base = iobuf->ptr;
        iov.iov_len = iobuf_size(iobuf);

        /* Create the xdr payload */
        ret = xdr_serialize_generic(iov, req, xdrproc);
        if (ret == -1) {
            goto out;
        }
        iov.iov_len = ret;
        count = 1;
    }

    if (!rpc)
        rpc = global_rpc;
    /* Send the msg */
    ret = rpc_clnt_submit(rpc, prog, procnum, cbkfn, &iov, count, NULL, 0,
                          iobref, frame, NULL, 0, NULL, 0, NULL);
    ret = 0;

out:
    if (new_iobref)
        iobref_unref(iobref);
    if (iobuf)
        iobuf_unref(iobuf);
    return ret;
}

int
cli_rpc_notify(struct rpc_clnt *rpc, void *mydata, rpc_clnt_event_t event,
               void *data)
{
    xlator_t *this = NULL;
    int ret = 0;

    this = mydata;

    switch (event) {
        case RPC_CLNT_CONNECT: {
            cli_cmd_broadcast_connected(_gf_true);
            gf_log(this->name, GF_LOG_TRACE, "got RPC_CLNT_CONNECT");
            break;
        }

        case RPC_CLNT_DISCONNECT: {
            cli_cmd_broadcast_connected(_gf_false);
            gf_log(this->name, GF_LOG_TRACE, "got RPC_CLNT_DISCONNECT");
            if (!global_state->prompt && global_state->await_connected) {
                ret = 1;
                cli_out(
                    "Connection failed. Please check if gluster "
                    "daemon is operational.");
                exit(ret);
            }
            break;
        }

        default:
            gf_log(this->name, GF_LOG_TRACE, "got some other RPC event %d",
                   event);
            ret = 0;
            break;
    }

    return ret;
}

static gf_boolean_t
is_valid_int(char *str)
{
    if (*str == '-')
        ++str;

    /* Handle empty string or just "-".*/
    if (!*str)
        return _gf_false;

    /* Check for non-digit chars in the rest of the string */
    while (*str) {
        if (!isdigit(*str))
            return _gf_false;
        else
            ++str;
    }
    return _gf_true;
}

/*
 * ret: 0: option successfully processed
 *      1: signalling end of option list
 *     -1: unknown option
 *     -2: parsing issue (avoid unknown option error)
 */
int
cli_opt_parse(char *opt, struct cli_state *state)
{
    char *oarg = NULL;
    gf_boolean_t secure_mgmt_tmp = 0;

    if (strcmp(opt, "") == 0)
        return 1;
    if (strcmp(opt, "help") == 0) {
        cli_out(
            " peer help                - display help for peer commands\n"
            " volume help              - display help for volume commands\n"
            " volume bitrot help       - display help for volume"
            " bitrot commands\n"
            " volume quota help        - display help for volume"
            " quota commands\n"
            " snapshot help            - display help for snapshot commands\n"
            " global help              - list global commands\n");
        exit(0);
    }

    if (strcmp(opt, "version") == 0) {
        cli_out("%s", argp_program_version);
        exit(0);
    }

    if (strcmp(opt, "print-logdir") == 0) {
        cli_out("%s", DEFAULT_LOG_FILE_DIRECTORY);
        exit(0);
    }

    if (strcmp(opt, "print-statedumpdir") == 0) {
        cli_out("%s", DEFAULT_VAR_RUN_DIRECTORY);
        exit(0);
    }

    if (strcmp(opt, "xml") == 0) {
#if (HAVE_LIB_XML)
        state->mode |= GLUSTER_MODE_XML;
#else
        cli_err("XML output not supported. Ignoring '--xml' option");
#endif
        return 0;
    }

    if (strcmp(opt, "nolog") == 0) {
        state->mode |= GLUSTER_MODE_GLFSHEAL_NOLOG;
        return 0;
    }

    if (strcmp(opt, "wignore-partition") == 0) {
        state->mode |= GLUSTER_MODE_WIGNORE_PARTITION;
        return 0;
    }

    if (strcmp(opt, "wignore") == 0) {
        state->mode |= GLUSTER_MODE_WIGNORE;
        return 0;
    }

    oarg = strtail(opt, "mode=");
    if (oarg) {
        if (strcmp(oarg, "script") == 0) {
            state->mode |= GLUSTER_MODE_SCRIPT;
            return 0;
        }

        if (strcmp(oarg, "interactive") == 0)
            return 0;

        return -1;
    }

    oarg = strtail(opt, "remote-host=");
    if (oarg) {
        state->remote_host = oarg;
        return 0;
    }

    oarg = strtail(opt, "inet6");
    if (oarg) {
        state->address_family = "inet6";
        return 0;
    }

    oarg = strtail(opt, "log-file=");
    if (oarg) {
        state->log_file = oarg;
        return 0;
    }
    oarg = strtail(opt, "timeout=");
    if (oarg) {
        if (!is_valid_int(oarg) || atoi(oarg) <= 0) {
            cli_err("timeout value should be a positive integer");
            return -2; /* -2 instead of -1 to avoid unknown option
                          error */
        }
        cli_default_conn_timeout = atoi(oarg);
        return 0;
    }

    oarg = strtail(opt, "log-level=");
    if (oarg) {
        int log_level = glusterd_check_log_level(oarg);
        if (log_level == -1)
            return -1;
        state->log_level = (gf_loglevel_t)log_level;
        return 0;
    }

    oarg = strtail(opt, "glusterd-sock=");
    if (oarg) {
        state->glusterd_sock = oarg;
        return 0;
    }

    oarg = strtail(opt, "secure-mgmt=");
    if (oarg) {
        if (gf_string2boolean(oarg, &secure_mgmt_tmp) == 0) {
            if (secure_mgmt_tmp) {
                /* See declaration for why this is an int. */
                state->ctx->secure_mgmt = 1;
            }
        } else {
            cli_err("invalid secure-mgmt value (ignored)");
        }
        return 0;
    }

    return -1;
}

int
parse_cmdline(int argc, char *argv[], struct cli_state *state)
{
    int ret = 0;
    int i = 0;
    int j = 0;
    char *opt = NULL;

    state->argc = argc - 1;
    state->argv = &argv[1];

    /* Do this first so that an option can override. */
    if (sys_access(SECURE_ACCESS_FILE, F_OK) == 0) {
        state->ctx->secure_mgmt = 1;
        state->ctx->ssl_cert_depth = glusterfs_read_secure_access_file();
    }

    if (state->argc > GEO_REP_CMD_CONFIG_INDEX &&
        strtail(state->argv[GEO_REP_CMD_INDEX], "geo") &&
        strtail(state->argv[GEO_REP_CMD_CONFIG_INDEX], "co"))
        goto done;

    for (i = 0; i < state->argc; i++) {
        opt = strtail(state->argv[i], "--");
        if (!opt)
            continue;
        ret = cli_opt_parse(opt, state);
        if (ret == -1) {
            cli_out("unrecognized option --%s\n", opt);
            usage();
            return ret;
        } else if (ret == -2) {
            return ret;
        }
        for (j = i; j < state->argc - 1; j++)
            state->argv[j] = state->argv[j + 1];
        state->argc--;
        /* argv shifted, next check should be at i again */
        i--;
        if (ret == 1) {
            /* end of cli options */
            ret = 0;
            break;
        }
    }

done:
    state->argv[state->argc] = NULL;

    return ret;
}

int
cli_cmd_tree_init(struct cli_cmd_tree *tree)
{
    struct cli_cmd_word *root = NULL;
    int ret = 0;

    root = &tree->root;
    root->tree = tree;

    return ret;
}

int
cli_state_init(struct cli_state *state)
{
    struct cli_cmd_tree *tree = NULL;
    int ret = 0;

    state->log_level = GF_LOG_NONE;

    tree = &state->tree;
    tree->state = state;

    ret = cli_cmd_tree_init(tree);

    return ret;
}

int
cli_usage_out(const char *usage)
{
    GF_ASSERT(usage);

    if (!usage || usage[0] == '\0')
        return -1;

    cli_err("\nUsage:\n%s\n", usage);
    return 0;
}

int
_cli_err(const char *fmt, ...)
{
    va_list ap;
    int ret = 0;
#ifdef HAVE_READLINE
    struct cli_state *state = global_state;
#endif

    va_start(ap, fmt);

#ifdef HAVE_READLINE
    if (state->rl_enabled && !state->rl_processing) {
        ret = cli_rl_err(state, fmt, ap);
        va_end(ap);
        return ret;
    }
#endif

    ret = vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);

    return ret;
}

int
_cli_out(const char *fmt, ...)
{
    va_list ap;
    int ret = 0;
#ifdef HAVE_READLINE
    struct cli_state *state = global_state;
#endif

    va_start(ap, fmt);
#ifdef HAVE_READLINE
    if (state->rl_enabled && !state->rl_processing) {
        ret = cli_rl_out(state, fmt, ap);
        va_end(ap);
        return ret;
    }
#endif

    ret = vprintf(fmt, ap);
    printf("\n");
    va_end(ap);

    return ret;
}

struct rpc_clnt *
cli_quotad_clnt_rpc_init(void)
{
    struct rpc_clnt *rpc = NULL;
    dict_t *rpc_opts = NULL;
    int ret = -1;

    rpc_opts = dict_new();
    if (!rpc_opts) {
        ret = -1;
        goto out;
    }

    ret = dict_set_str(rpc_opts, "transport.address-family", "unix");
    if (ret)
        goto out;

    ret = dict_set_str(rpc_opts, "transport-type", "socket");
    if (ret)
        goto out;

    ret = dict_set_str(rpc_opts, "transport.socket.connect-path",
                       "/var/run/gluster/quotad.socket");
    if (ret)
        goto out;

    rpc = cli_quotad_clnt_init(THIS, rpc_opts);
    if (!rpc)
        goto out;

    global_quotad_rpc = rpc;
out:
    if (rpc_opts) {
        dict_unref(rpc_opts);
    }
    return rpc;
}

struct rpc_clnt *
cli_rpc_init(struct cli_state *state)
{
    struct rpc_clnt *rpc = NULL;
    dict_t *options = NULL;
    int ret = -1;
    int port = CLI_GLUSTERD_PORT;
    xlator_t *this = NULL;
#ifdef IPV6_DEFAULT
    char *addr_family = "inet6";
#else
    char *addr_family = "inet";
#endif

    this = THIS;
    cli_rpc_prog = &cli_prog;

    options = dict_new();
    if (!options)
        goto out;

    /* If address family specified in CLI */
    if (state->address_family) {
        addr_family = state->address_family;
    }

    /* Connect to glusterd using the specified method, giving preference
     * to a unix socket connection.  If nothing is specified, connect to
     * the default glusterd socket.
     */
    if (state->glusterd_sock) {
        gf_log("cli", GF_LOG_INFO,
               "Connecting to glusterd using "
               "sockfile %s",
               state->glusterd_sock);
        ret = rpc_transport_unix_options_build(options, state->glusterd_sock,
                                               0);
        if (ret)
            goto out;
    } else if (state->remote_host) {
        gf_log("cli", GF_LOG_INFO,
               "Connecting to remote glusterd at "
               "%s",
               state->remote_host);

        ret = dict_set_str(options, "remote-host", state->remote_host);
        if (ret)
            goto out;

        if (state->remote_port)
            port = state->remote_port;

        ret = dict_set_int32(options, "remote-port", port);
        if (ret)
            goto out;

        ret = dict_set_str(options, "transport.address-family", addr_family);
        if (ret)
            goto out;
    } else {
        gf_log("cli", GF_LOG_DEBUG,
               "Connecting to glusterd using "
               "default socket");
        ret = rpc_transport_unix_options_build(options,
                                               DEFAULT_GLUSTERD_SOCKFILE, 0);
        if (ret)
            goto out;
    }

    rpc = rpc_clnt_new(options, this, this->name, 16);
    if (!rpc)
        goto out;

    ret = rpc_clnt_register_notify(rpc, cli_rpc_notify, this);
    if (ret) {
        gf_log("cli", GF_LOG_ERROR, "failed to register notify");
        goto out;
    }

    ret = rpc_clnt_start(rpc);
out:
    if (options)
        dict_unref(options);

    if (ret) {
        if (rpc)
            rpc_clnt_unref(rpc);
        rpc = NULL;
    }
    return rpc;
}

cli_local_t *
cli_local_get()
{
    cli_local_t *local = NULL;

    local = GF_CALLOC(1, sizeof(*local), cli_mt_cli_local_t);
    LOCK_INIT(&local->lock);
    INIT_LIST_HEAD(&local->dict_list);

    return local;
}

void
cli_local_wipe(cli_local_t *local)
{
    if (local) {
        GF_FREE(local->get_vol.volname);
        if (local->dict)
            dict_unref(local->dict);
        GF_FREE(local);
    }

    return;
}

struct cli_state *global_state;

int
main(int argc, char *argv[])
{
    struct cli_state state = {
        0,
    };
    int ret = -1;
    glusterfs_ctx_t *ctx = NULL;

    mem_pools_init();

    ctx = glusterfs_ctx_new();
    if (!ctx)
        return ENOMEM;

#ifdef DEBUG
    gf_mem_acct_enable_set(ctx);
#endif

    ret = glusterfs_globals_init(ctx);
    if (ret)
        return ret;

    THIS->ctx = ctx;

    ret = glusterfs_ctx_defaults_init(ctx);
    if (ret)
        goto out;

    cli_default_conn_timeout = 120;
    cli_ten_minutes_timeout = 600;

    ret = cli_state_init(&state);
    if (ret)
        goto out;

    state.ctx = ctx;
    global_state = &state;

    ret = parse_cmdline(argc, argv, &state);
    if (ret)
        goto out;

    ret = logging_init(ctx, &state);
    if (ret)
        goto out;

    gf_log("cli", GF_LOG_INFO, "Started running %s with version %s", argv[0],
           PACKAGE_VERSION);

    global_rpc = cli_rpc_init(&state);
    if (!global_rpc)
        goto out;

    /*
     * Now, one doesn't need to initialize global rpc
     * for quota unless and until quota is enabled.
     * So why not put a check to save all the rpc related
     * ops here.
     */
    ret = sys_access(QUOTAD_PID_PATH, F_OK);
    if (!ret) {
        global_quotad_rpc = cli_quotad_clnt_rpc_init();
        if (!global_quotad_rpc)
            goto out;
    }

    ret = cli_cmds_register(&state);
    if (ret)
        goto out;

    ret = cli_input_init(&state);
    if (ret)
        goto out;

    ret = gf_event_dispatch(ctx->event_pool);

out:
    //        glusterfs_ctx_destroy (ctx);

    mem_pools_fini();

    return ret;
}

void
cli_print_line(int len)
{
    GF_ASSERT(len > 0);

    while (len--)
        printf("-");

    printf("\n");
}

void
print_quota_list_header(int type)
{
    if (type == GF_QUOTA_OPTION_TYPE_LIST) {
        cli_out(
            "                  Path                   Hard-limit "
            " Soft-limit      Used  Available  Soft-limit "
            "exceeded? Hard-limit exceeded?");
        cli_out(
            "-----------------------------------------------------"
            "-----------------------------------------------------"
            "---------------------");
    } else {
        cli_out(
            "                  Path                   Hard-limit  "
            " Soft-limit      Files       Dirs     Available  "
            "Soft-limit exceeded? Hard-limit exceeded?");
        cli_out(
            "-----------------------------------------------------"
            "-----------------------------------------------------"
            "-------------------------------------");
    }
}

void
print_quota_list_empty(char *path, int type)
{
    if (type == GF_QUOTA_OPTION_TYPE_LIST)
        cli_out("%-40s %7s %9s %10s %7s %15s %20s", path, "N/A", "N/A", "N/A",
                "N/A", "N/A", "N/A");
    else
        cli_out("%-40s %9s %9s %12s %10s %10s %15s %20s", path, "N/A", "N/A",
                "N/A", "N/A", "N/A", "N/A", "N/A");
}
