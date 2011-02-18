/*
  Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
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

#include "xlator.h"
#include "glusterfs.h"
#include "compat.h"
#include "logging.h"
#include "dict.h"
#include "list.h"
#include "timer.h"
#include "glusterfsd.h"
#include "stack.h"
#include "revision.h"
#include "common-utils.h"
#include "event.h"
#include "globals.h"
#include "statedump.h"
#include "latency.h"
#include "glusterfsd-mem-types.h"
#include "syscall.h"
#include "call-stub.h"
#include <fnmatch.h>
#include "rpc-clnt.h"

#ifdef GF_DARWIN_HOST_OS
#include "daemon.h"
#else
#define os_daemon(u, v) daemon (u, v)
#endif


/* using argp for command line parsing */
static char gf_doc[] = "";
static char argp_doc[] = "--volfile-server=SERVER [MOUNT-POINT]\n"       \
        "--volfile=VOLFILE [MOUNT-POINT]";
const char *argp_program_version = "" \
        PACKAGE_NAME" "PACKAGE_VERSION" built on "__DATE__" "__TIME__ \
        "\nRepository revision: " GLUSTERFS_REPOSITORY_REVISION "\n"  \
        "Copyright (c) 2006-2010 Gluster Inc. "             \
        "<http://www.gluster.com>\n"                                \
        "GlusterFS comes with ABSOLUTELY NO WARRANTY.\n"              \
        "You may redistribute copies of GlusterFS under the terms of "\
        "the GNU Affero General Public License.";
const char *argp_program_bug_address = "<" PACKAGE_BUGREPORT ">";

static error_t parse_opts (int32_t key, char *arg, struct argp_state *_state);

static struct argp_option gf_options[] = {
        {0, 0, 0, 0, "Basic options:"},
        {"volfile-server", ARGP_VOLFILE_SERVER_KEY, "SERVER", 0,
         "Server to get the volume file from.  This option overrides "
         "--volfile option"},
        {"volfile-max-fetch-attempts", ARGP_VOLFILE_MAX_FETCH_ATTEMPTS,
         "MAX-ATTEMPTS", 0, "Maximum number of connect attempts to server. "
         "This option should be provided with --volfile-server option"
         "[default: 1]"},
        {"volfile", ARGP_VOLUME_FILE_KEY, "VOLFILE", 0,
         "File to use as VOLUME_FILE"},
        {"spec-file", ARGP_VOLUME_FILE_KEY, "VOLFILE", OPTION_HIDDEN,
         "File to use as VOLUME FILE"},
        {"log-server", ARGP_LOG_SERVER_KEY, "LOGSERVER", 0,
         "Server to use as the central log server"},

        {"log-level", ARGP_LOG_LEVEL_KEY, "LOGLEVEL", 0,
         "Logging severity.  Valid options are DEBUG, NORMAL, WARNING, ERROR, "
         "CRITICAL and NONE [default: NORMAL]"},
        {"log-file", ARGP_LOG_FILE_KEY, "LOGFILE", 0,
         "File to use for logging [default: "
         DEFAULT_LOG_FILE_DIRECTORY "/" PACKAGE_NAME ".log" "]"},

        {0, 0, 0, 0, "Advanced Options:"},
        {"volfile-server-port", ARGP_VOLFILE_SERVER_PORT_KEY, "PORT", 0,
         "Listening port number of volfile server"},
        {"volfile-server-transport", ARGP_VOLFILE_SERVER_TRANSPORT_KEY,
         "TRANSPORT", 0,
         "Transport type to get volfile from server [default: socket]"},
        {"volfile-id", ARGP_VOLFILE_ID_KEY, "KEY", 0,
         "'key' of the volfile to be fetched from server"},
        {"log-server-port", ARGP_LOG_SERVER_PORT_KEY, "PORT", 0,
         "Listening port number of log server"},
        {"pid-file", ARGP_PID_FILE_KEY, "PIDFILE", 0,
         "File to use as pid file"},
        {"no-daemon", ARGP_NO_DAEMON_KEY, 0, 0,
         "Run in foreground"},
        {"run-id", ARGP_RUN_ID_KEY, "RUN-ID", OPTION_HIDDEN,
         "Run ID for the process, used by scripts to keep track of process "
         "they started, defaults to none"},
        {"debug", ARGP_DEBUG_KEY, 0, 0,
         "Run in debug mode.  This option sets --no-daemon, --log-level "
         "to DEBUG and --log-file to console"},
        {"volume-name", ARGP_VOLUME_NAME_KEY, "VOLUME-NAME", 0,
         "Volume name to be used for MOUNT-POINT [default: top most volume "
         "in VOLFILE]"},
        {"xlator-option", ARGP_XLATOR_OPTION_KEY,"VOLUME-NAME.OPTION=VALUE", 0,
         "Add/override a translator option for a volume with specified value"},
        {"read-only", ARGP_READ_ONLY_KEY, 0, 0,
         "Mount the filesystem in 'read-only' mode"},
        {"mac-compat", ARGP_MAC_COMPAT_KEY, "BOOL", OPTION_ARG_OPTIONAL,
         "Provide stubs for attributes needed for seamless operation on Macs "
#ifdef GF_DARWIN_HOST_OS
         "[default: \"on\" on client side, else \"off\"]"
#else
         "[default: \"off\"]"
#endif
        },
        {"brick-name", ARGP_BRICK_NAME_KEY, "BRICK-NAME", OPTION_HIDDEN,
         "Brick name to be registered with Gluster portmapper" },
        {"brick-port", ARGP_BRICK_PORT_KEY, "BRICK-PORT", OPTION_HIDDEN,
         "Brick Port to be registered with Gluster portmapper" },

        {0, 0, 0, 0, "Fuse options:"},
        {"direct-io-mode", ARGP_DIRECT_IO_MODE_KEY, "BOOL", OPTION_ARG_OPTIONAL,
         "Use direct I/O mode in fuse kernel module"
         " [default: \"off\" if big writes are supported, else "
         "\"on\" for fds not opened with O_RDONLY]"},
        {"entry-timeout", ARGP_ENTRY_TIMEOUT_KEY, "SECONDS", 0,
         "Set entry timeout to SECONDS in fuse kernel module [default: 1]"},
        {"attribute-timeout", ARGP_ATTRIBUTE_TIMEOUT_KEY, "SECONDS", 0,
         "Set attribute timeout to SECONDS for inodes in fuse kernel module "
         "[default: 1]"},
        {"client-pid", ARGP_CLIENT_PID_KEY, "PID", OPTION_HIDDEN,
         "client will authenticate itself with process id PID to server"},
        {"dump-fuse", ARGP_DUMP_FUSE_KEY, "PATH", 0,
         "Dump fuse traffic to PATH"},
        {"volfile-check", ARGP_VOLFILE_CHECK_KEY, 0, 0,
         "Enable strict volume file checking"},
        {0, 0, 0, 0, "Miscellaneous Options:"},
        {0, }
};


static struct argp argp = { gf_options, parse_opts, argp_doc, gf_doc };

int glusterfs_pidfile_cleanup (glusterfs_ctx_t *ctx);
int glusterfs_volumes_init (glusterfs_ctx_t *ctx);
int glusterfs_mgmt_init (glusterfs_ctx_t *ctx);

int
create_fuse_mount (glusterfs_ctx_t *ctx)
{
        int              ret = 0;
        cmd_args_t      *cmd_args = NULL;
        xlator_t        *master = NULL;

        cmd_args = &ctx->cmd_args;

        if (!cmd_args->mount_point)
                return 0;

        master = GF_CALLOC (1, sizeof (*master),
                            gfd_mt_xlator_t);
        if (!master)
                goto err;

        master->name = gf_strdup ("fuse");
        if (!master->name)
                goto err;

        if (xlator_set_type (master, "mount/fuse") == -1) {
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "MOUNT-POINT %s initialization failed",
                        cmd_args->mount_point);
                goto err;
        }

        master->ctx      = ctx;
        master->options  = get_new_dict ();

        ret = dict_set_static_ptr (master->options, ZR_MOUNTPOINT_OPT,
                                   cmd_args->mount_point);
        if (ret < 0) {
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "failed to set mount-point to options dictionary");
                goto err;
        }

        if (cmd_args->fuse_attribute_timeout >= 0) {
                ret = dict_set_double (master->options, ZR_ATTR_TIMEOUT_OPT,
                                       cmd_args->fuse_attribute_timeout);

                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value.");
                        goto err;
                }
        }

        if (cmd_args->fuse_entry_timeout >= 0) {
                ret = dict_set_double (master->options, ZR_ENTRY_TIMEOUT_OPT,
                                       cmd_args->fuse_entry_timeout);
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value.");
                        goto err;
                }
        }

        if (cmd_args->client_pid_set) {
                ret = dict_set_int32 (master->options, "client-pid",
                                      cmd_args->client_pid);
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value.");
                        goto err;
                }
        }

        if (cmd_args->volfile_check) {
                ret = dict_set_int32 (master->options, ZR_STRICT_VOLFILE_CHECK,
                                      cmd_args->volfile_check);
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value.");
                        goto err;
                }
        }

        if (cmd_args->dump_fuse) {
                ret = dict_set_static_ptr (master->options, ZR_DUMP_FUSE,
                                           cmd_args->dump_fuse);
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value.");
                        goto err;
                }
        }

        switch (cmd_args->fuse_direct_io_mode) {
        case GF_OPTION_DISABLE: /* disable */
                ret = dict_set_static_ptr (master->options, ZR_DIRECT_IO_OPT,
                                           "disable");
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value.");
                        goto err;
                }
                break;
        case GF_OPTION_ENABLE: /* enable */
                ret = dict_set_static_ptr (master->options, ZR_DIRECT_IO_OPT,
                                           "enable");
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value.");
                        goto err;
                }
                break;
        case GF_OPTION_DEFERRED: /* default */
        default:
                break;
        }

        ret = xlator_init (master);
        if (ret)
                goto err;

        ctx->master = master;

        return 0;

err:
        if (master) {
                xlator_destroy (master);
        }

        return -1;
}


static FILE *
get_volfp (glusterfs_ctx_t *ctx)
{
        int          ret = 0;
        cmd_args_t  *cmd_args = NULL;
        FILE        *specfp = NULL;
        struct stat  statbuf;

        cmd_args = &ctx->cmd_args;

        ret = sys_lstat (cmd_args->volfile, &statbuf);
        if (ret == -1) {
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "%s: %s", cmd_args->volfile, strerror (errno));
                return NULL;
        }

        if ((specfp = fopen (cmd_args->volfile, "r")) == NULL) {
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "volume file %s: %s",
                        cmd_args->volfile,
                        strerror (errno));
                return NULL;
        }

        gf_log ("glusterfsd", GF_LOG_DEBUG,
                "loading volume file %s", cmd_args->volfile);

        return specfp;
}


static int
gf_remember_xlator_option (struct list_head *options, char *arg)
{
        glusterfs_ctx_t         *ctx = NULL;
        cmd_args_t              *cmd_args  = NULL;
        xlator_cmdline_option_t *option = NULL;
        int                      ret = -1;
        char                    *dot = NULL;
        char                    *equals = NULL;

        ctx = glusterfs_ctx_get ();
        cmd_args = &ctx->cmd_args;

        option = GF_CALLOC (1, sizeof (xlator_cmdline_option_t),
                            gfd_mt_xlator_cmdline_option_t);
        if (!option)
                goto out;

        INIT_LIST_HEAD (&option->cmd_args);

        dot = strchr (arg, '.');
        if (!dot)
                goto out;

        option->volume = GF_CALLOC ((dot - arg) + 1, sizeof (char),
                                    gfd_mt_char);
        strncpy (option->volume, arg, (dot - arg));

        equals = strchr (arg, '=');
        if (!equals)
                goto out;

        option->key = GF_CALLOC ((equals - dot) + 1, sizeof (char),
                                 gfd_mt_char);
        if (!option->key)
                goto out;

        strncpy (option->key, dot + 1, (equals - dot - 1));

        if (!*(equals + 1))
                goto out;

        option->value = gf_strdup (equals + 1);

        list_add (&option->cmd_args, &cmd_args->xlator_options);

        ret = 0;
out:
        if (ret == -1) {
                if (option) {
                        if (option->volume)
                                GF_FREE (option->volume);
                        if (option->key)
                                GF_FREE (option->key);
                        if (option->value)
                                GF_FREE (option->value);

                        GF_FREE (option);
                }
        }

        return ret;
}



static error_t
parse_opts (int key, char *arg, struct argp_state *state)
{
        cmd_args_t   *cmd_args      = NULL;
        uint32_t      n             = 0;
        double        d             = 0.0;
        gf_boolean_t  b             = _gf_false;
        char         *pwd           = NULL;
        char          tmp_buf[2048] = {0,};

        cmd_args = state->input;

        switch (key) {
        case ARGP_VOLFILE_SERVER_KEY:
                cmd_args->volfile_server = gf_strdup (arg);
                break;

        case ARGP_VOLFILE_MAX_FETCH_ATTEMPTS:
                n = 0;

                if (gf_string2uint_base10 (arg, &n) == 0) {
                        cmd_args->max_connect_attempts = n;
                        break;
                }

                argp_failure (state, -1, 0,
                              "Invalid limit on connect attempts %s", arg);
                break;

        case ARGP_READ_ONLY_KEY:
                cmd_args->read_only = 1;
                break;

        case ARGP_MAC_COMPAT_KEY:
                if (!arg)
                        arg = "on";

                if (gf_string2boolean (arg, &b) == 0) {
                        cmd_args->mac_compat = b;

                        break;
                }

                argp_failure (state, -1, 0,
                              "invalid value \"%s\" for mac-compat", arg);
                break;

        case ARGP_VOLUME_FILE_KEY:
                if (cmd_args->volfile)
                        GF_FREE (cmd_args->volfile);

                if (arg[0] != '/') {
                        pwd = getcwd (NULL, PATH_MAX);
                        if (!pwd) {
                               argp_failure (state, -1, errno,
                                            "getcwd failed with error no %d",
                                             errno);
                               break;
                        }
                        snprintf (tmp_buf, 1024, "%s/%s", pwd, arg);
                        cmd_args->volfile = gf_strdup (tmp_buf);
                        free (pwd);
                } else {
                        cmd_args->volfile = gf_strdup (arg);
                }

                break;

        case ARGP_LOG_SERVER_KEY:
                if (cmd_args->log_server)
                        GF_FREE (cmd_args->log_server);

                cmd_args->log_server = gf_strdup (arg);
                break;

        case ARGP_LOG_LEVEL_KEY:
                if (strcasecmp (arg, ARGP_LOG_LEVEL_NONE_OPTION) == 0) {
                        cmd_args->log_level = GF_LOG_NONE;
                        break;
                }
                if (strcasecmp (arg, ARGP_LOG_LEVEL_CRITICAL_OPTION) == 0) {
                        cmd_args->log_level = GF_LOG_CRITICAL;
                        break;
                }
                if (strcasecmp (arg, ARGP_LOG_LEVEL_ERROR_OPTION) == 0) {
                        cmd_args->log_level = GF_LOG_ERROR;
                        break;
                }
                if (strcasecmp (arg, ARGP_LOG_LEVEL_WARNING_OPTION) == 0) {
                        cmd_args->log_level = GF_LOG_WARNING;
                        break;
                }
                if (strcasecmp (arg, ARGP_LOG_LEVEL_NORMAL_OPTION) == 0) {
                        cmd_args->log_level = GF_LOG_NORMAL;
                        break;
                }
                if (strcasecmp (arg, ARGP_LOG_LEVEL_DEBUG_OPTION) == 0) {
                        cmd_args->log_level = GF_LOG_DEBUG;
                        break;
                }
                if (strcasecmp (arg, ARGP_LOG_LEVEL_TRACE_OPTION) == 0) {
                        cmd_args->log_level = GF_LOG_TRACE;
                        break;
                }

                argp_failure (state, -1, 0, "unknown log level %s", arg);
                break;

        case ARGP_LOG_FILE_KEY:
                cmd_args->log_file = gf_strdup (arg);
                break;

        case ARGP_VOLFILE_SERVER_PORT_KEY:
                n = 0;

                if (gf_string2uint_base10 (arg, &n) == 0) {
                        cmd_args->volfile_server_port = n;
                        break;
                }

                argp_failure (state, -1, 0,
                              "unknown volfile server port %s", arg);
                break;

        case ARGP_LOG_SERVER_PORT_KEY:
                n = 0;

                if (gf_string2uint_base10 (arg, &n) == 0) {
                        cmd_args->log_server_port = n;
                        break;
                }

                argp_failure (state, -1, 0,
                              "unknown log server port %s", arg);
                break;

        case ARGP_VOLFILE_SERVER_TRANSPORT_KEY:
                cmd_args->volfile_server_transport = gf_strdup (arg);
                break;

        case ARGP_VOLFILE_ID_KEY:
                cmd_args->volfile_id = gf_strdup (arg);
                break;

        case ARGP_PID_FILE_KEY:
                cmd_args->pid_file = gf_strdup (arg);
                break;

        case ARGP_NO_DAEMON_KEY:
                cmd_args->no_daemon_mode = ENABLE_NO_DAEMON_MODE;
                break;

        case ARGP_RUN_ID_KEY:
                cmd_args->run_id = gf_strdup (arg);
                break;

        case ARGP_DEBUG_KEY:
                cmd_args->debug_mode = ENABLE_DEBUG_MODE;
                break;

        case ARGP_DIRECT_IO_MODE_KEY:
                if (!arg)
                        arg = "on";

                if (gf_string2boolean (arg, &b) == 0) {
                        cmd_args->fuse_direct_io_mode = b;

                        break;
                }

                argp_failure (state, -1, 0,
                              "unknown direct I/O mode setting \"%s\"", arg);
                break;

        case ARGP_ENTRY_TIMEOUT_KEY:
                d = 0.0;

                gf_string2double (arg, &d);
                if (!(d < 0.0)) {
                        cmd_args->fuse_entry_timeout = d;
                        break;
                }

                argp_failure (state, -1, 0, "unknown entry timeout %s", arg);
                break;

        case ARGP_ATTRIBUTE_TIMEOUT_KEY:
                d = 0.0;

                gf_string2double (arg, &d);
                if (!(d < 0.0)) {
                        cmd_args->fuse_attribute_timeout = d;
                        break;
                }

                argp_failure (state, -1, 0,
                              "unknown attribute timeout %s", arg);
                break;

        case ARGP_CLIENT_PID_KEY:
                if (gf_string2int (arg, &cmd_args->client_pid) == 0) {
                        cmd_args->client_pid_set = 1;
                        break;
                }

                argp_failure (state, -1, 0,
                              "unknown client pid %s", arg);
                break;

        case ARGP_VOLFILE_CHECK_KEY:
                cmd_args->volfile_check = 1;
                break;

        case ARGP_VOLUME_NAME_KEY:
                cmd_args->volume_name = gf_strdup (arg);
                break;

        case ARGP_XLATOR_OPTION_KEY:
                gf_remember_xlator_option (&cmd_args->xlator_options, arg);
                break;

        case ARGP_KEY_NO_ARGS:
                break;

        case ARGP_KEY_ARG:
                if (state->arg_num >= 1)
                        argp_usage (state);

                cmd_args->mount_point = gf_strdup (arg);
                break;

        case ARGP_DUMP_FUSE_KEY:
                cmd_args->dump_fuse = gf_strdup (arg);
                break;
        case ARGP_BRICK_NAME_KEY:
                cmd_args->brick_name = gf_strdup (arg);
                break;
        case ARGP_BRICK_PORT_KEY:
                n = 0;

                if (gf_string2uint_base10 (arg, &n) == 0) {
                        cmd_args->brick_port = n;
                        break;
                }

                argp_failure (state, -1, 0,
                              "unknown brick (listen) port %s", arg);
                break;
        }

        return 0;
}


void
cleanup_and_exit (int signum)
{
        glusterfs_ctx_t *ctx      = NULL;
        xlator_t        *trav     = NULL;

        ctx = glusterfs_ctx_get ();

        /* TODO: is this the right place? */
        if (!ctx)
                return;
        if (ctx->cleanup_started)
                return;

        ctx->cleanup_started = 1;
        glusterfs_mgmt_pmap_signout (ctx);

        /* Call fini() of FUSE xlator first:
         * so there are no more requests coming and
         * 'umount' of mount point is done properly */
        trav = ctx->master;
        if (trav && trav->fini) {
                THIS = trav;
                trav->fini (trav);
        }

        gf_log ("glusterfsd", GF_LOG_NORMAL, "shutting down");

        glusterfs_pidfile_cleanup (ctx);

        exit (0);
#if 0
        /* TODO: Properly do cleanup_and_exit(), with synchronisations */
        if (ctx->mgmt)
                rpc_clnt_unref (ctx->mgmt);

        /* call fini() of each xlator */
        trav = NULL;
        if (ctx->active)
                trav = ctx->active->top;
        while (trav) {
                if (trav->fini) {
                        THIS = trav;
                        trav->fini (trav);
                }
                trav = trav->next;
        }
#endif
}


static void
reincarnate (int signum)
{
        int                 ret = 0;
        glusterfs_ctx_t    *ctx = NULL;
        cmd_args_t         *cmd_args = NULL;

        ctx = glusterfs_ctx_get ();
        cmd_args = &ctx->cmd_args;


        if (cmd_args->volfile_server) {
                gf_log ("glusterfsd", GF_LOG_NORMAL,
                        "Fetching the volume file from server...");
                ret = glusterfs_volfile_fetch (ctx);
        } else {
                gf_log ("glusterfsd", GF_LOG_NORMAL,
                        "Reloading volfile ...");
                ret = glusterfs_volumes_init (ctx);
        }

        if (ret < 0)
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "volume initialization failed.");

        /* Also, SIGHUP should do logroate */
        gf_log_logrotate (1);

        return;
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
        snprintf (tmp_str, 1024, "%s-%d-%s:%" GF_PRI_SUSECONDS,
                  hostname, getpid(), now_str, tv.tv_usec);

        return gf_strdup (tmp_str);
}

#define GF_SERVER_PROCESS   0
#define GF_CLIENT_PROCESS   1
#define GF_GLUSTERD_PROCESS 2

static uint8_t
gf_get_process_mode (char *exec_name)
{
        char *dup_execname = NULL, *base = NULL;
        uint8_t ret = 0;

        dup_execname = gf_strdup (exec_name);
        base = basename (dup_execname);

        if (!strncmp (base, "glusterfsd", 10)) {
                ret = GF_SERVER_PROCESS;
        } else if (!strncmp (base, "glusterd", 8)) {
                ret = GF_GLUSTERD_PROCESS;
        } else {
                ret = GF_CLIENT_PROCESS;
        }

        GF_FREE (dup_execname);

        return ret;
}



static int
set_log_file_path (cmd_args_t *cmd_args)
{
        int   i = 0;
        int   j = 0;
        int   ret = 0;
        int   port = 0;
        char *tmp_ptr = NULL;
        char  tmp_str[1024] = {0,};

        if (cmd_args->mount_point) {
                j = 0;
                i = 0;
                if (cmd_args->mount_point[0] == '/')
                        i = 1;
                for (; i < strlen (cmd_args->mount_point); i++,j++) {
                        tmp_str[j] = cmd_args->mount_point[i];
                        if (cmd_args->mount_point[i] == '/')
                                tmp_str[j] = '-';
                }

                ret = gf_asprintf (&cmd_args->log_file,
                                   DEFAULT_LOG_FILE_DIRECTORY "/%s.log",
                                   tmp_str);
                if (ret == -1) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "asprintf failed while setting up log-file");
                }
                goto done;
        }

        if (cmd_args->volfile) {
                j = 0;
                i = 0;
                if (cmd_args->volfile[0] == '/')
                        i = 1;
                for (; i < strlen (cmd_args->volfile); i++,j++) {
                        tmp_str[j] = cmd_args->volfile[i];
                        if (cmd_args->volfile[i] == '/')
                                tmp_str[j] = '-';
                }
                ret = gf_asprintf (&cmd_args->log_file,
                                DEFAULT_LOG_FILE_DIRECTORY "/%s.log",
                                tmp_str);
                if (ret == -1) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "asprintf failed while setting up log-file");
                }
                goto done;
        }

        if (cmd_args->volfile_server) {
                port = 1;
                tmp_ptr = "default";

                if (cmd_args->volfile_server_port)
                        port = cmd_args->volfile_server_port;
                if (cmd_args->volfile_id)
                        tmp_ptr = cmd_args->volfile_id;

                ret = gf_asprintf (&cmd_args->log_file,
                                   DEFAULT_LOG_FILE_DIRECTORY "/%s-%s-%d.log",
                                   cmd_args->volfile_server, tmp_ptr, port);
                if (-1 == ret) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "asprintf failed while setting up log-file");
                }
        }
done:
        return ret;
}


static int
glusterfs_ctx_defaults_init (glusterfs_ctx_t *ctx)
{
        cmd_args_t    *cmd_args = NULL;
        struct rlimit  lim = {0, };
        call_pool_t   *pool = NULL;

        xlator_mem_acct_init (THIS, gfd_mt_end);

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
                          gfd_mt_call_pool_t);
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
        cmd_args->log_level = DEFAULT_LOG_LEVEL;
#ifdef GF_DARWIN_HOST_OS
        cmd_args->mac_compat = GF_OPTION_DEFERRED;
        /* On Darwin machines, O_APPEND is not handled,
         * which may corrupt the data
         */
        cmd_args->fuse_direct_io_mode = GF_OPTION_DISABLE;
#else
        cmd_args->mac_compat = GF_OPTION_DISABLE;
        cmd_args->fuse_direct_io_mode = GF_OPTION_DEFERRED;
#endif
        cmd_args->fuse_attribute_timeout = -1;
        cmd_args->fuse_entry_timeout = -1;

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
        int         ret = 0;

        cmd_args = &ctx->cmd_args;

        if (cmd_args->log_file == NULL) {
                ret = set_log_file_path (cmd_args);
                if (ret == -1) {
                        fprintf (stderr, "failed to set the log file path.. "
                                 "exiting\n");
                        return -1;
                }
        }

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
parse_cmdline (int argc, char *argv[], glusterfs_ctx_t *ctx)
{
        int               process_mode = 0;
        int               ret = 0;
        struct stat       stbuf = {0, };
        struct tm        *tm = NULL;
        time_t            utime;
        char              timestr[256];
        char              tmp_logfile[1024] = { 0 };
        char              *tmp_logfile_dyn = NULL;
        char              *tmp_logfilebase = NULL;
        cmd_args_t        *cmd_args = NULL;

        cmd_args = &ctx->cmd_args;

        argp_parse (&argp, argc, argv, ARGP_IN_ORDER, NULL, cmd_args);

        if (ENABLE_DEBUG_MODE == cmd_args->debug_mode) {
                cmd_args->log_level = GF_LOG_DEBUG;
                cmd_args->log_file = "/dev/stderr";
                cmd_args->no_daemon_mode = ENABLE_NO_DAEMON_MODE;
        }

        process_mode = gf_get_process_mode (argv[0]);

        if ((cmd_args->volfile_server == NULL)
            && (cmd_args->volfile == NULL)) {
                if (process_mode == GF_SERVER_PROCESS)
                        cmd_args->volfile = gf_strdup (DEFAULT_SERVER_VOLFILE);
                else if (process_mode == GF_GLUSTERD_PROCESS)
                        cmd_args->volfile = gf_strdup (DEFAULT_GLUSTERD_VOLFILE);
                else
                        cmd_args->volfile = gf_strdup (DEFAULT_CLIENT_VOLFILE);
        }

        if (cmd_args->run_id) {
                ret = sys_lstat (cmd_args->log_file, &stbuf);
                /* If its /dev/null, or /dev/stdout, /dev/stderr,
                 * let it use the same, no need to alter
                 */
                if (((ret == 0) &&
                     (S_ISREG (stbuf.st_mode) || S_ISLNK (stbuf.st_mode))) ||
                    (ret == -1)) {
                        /* Have seperate logfile per run */
                        tm = localtime (&utime);
                        strftime (timestr, 256, "%Y%m%d.%H%M%S", tm);
                        sprintf (tmp_logfile, "%s.%s.%d",
                                 cmd_args->log_file, timestr, getpid ());

                        /* Create symlink to actual log file */
                        sys_unlink (cmd_args->log_file);

                        tmp_logfile_dyn = gf_strdup (tmp_logfile);
                        tmp_logfilebase = basename (tmp_logfile_dyn);
                        ret = sys_symlink (tmp_logfilebase,
                                           cmd_args->log_file);
                        if (ret == -1) {
                                fprintf (stderr, "symlink of logfile failed");
                        } else {
                                GF_FREE (cmd_args->log_file);
                                cmd_args->log_file = gf_strdup (tmp_logfile);
                        }

                        GF_FREE (tmp_logfile_dyn);
                }
        }

        return ret;
}


int
glusterfs_pidfile_setup (glusterfs_ctx_t *ctx)
{
        cmd_args_t  *cmd_args = NULL;
        int          ret = 0;
        FILE        *pidfp = NULL;

        cmd_args = &ctx->cmd_args;

        if (!cmd_args->pid_file)
                return 0;

        pidfp = fopen (cmd_args->pid_file, "a+");
        if (!pidfp) {
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "pidfile %s error (%s)",
                        cmd_args->pid_file, strerror (errno));
                return -1;
        }

        ret = lockf (fileno (pidfp), F_TLOCK, 0);
        if (ret) {
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "pidfile %s lock error (%s)",
                        cmd_args->pid_file, strerror (errno));
                return ret;
        }

        gf_log ("glusterfsd", GF_LOG_TRACE,
                "pidfile %s lock acquired",
                cmd_args->pid_file);

        ret = lockf (fileno (pidfp), F_ULOCK, 0);
        if (ret) {
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "pidfile %s unlock error (%s)",
                        cmd_args->pid_file, strerror (errno));
                return ret;
        }

        ctx->pidfp = pidfp;

        return 0;
}


int
glusterfs_pidfile_cleanup (glusterfs_ctx_t *ctx)
{
        cmd_args_t  *cmd_args = NULL;

        cmd_args = &ctx->cmd_args;

        if (!ctx->pidfp)
                return 0;

        gf_log ("glusterfsd", GF_LOG_TRACE,
                "pidfile %s unlocking",
                cmd_args->pid_file);

        lockf (fileno (ctx->pidfp), F_ULOCK, 0);
        fclose (ctx->pidfp);
        ctx->pidfp = NULL;

        if (ctx->cmd_args.pid_file) {
                unlink (ctx->cmd_args.pid_file);
                ctx->cmd_args.pid_file = NULL;
        }

        return 0;
}

int
glusterfs_pidfile_update (glusterfs_ctx_t *ctx)
{
        cmd_args_t  *cmd_args = NULL;
        int          ret = 0;
        FILE        *pidfp = NULL;

        cmd_args = &ctx->cmd_args;

        pidfp = ctx->pidfp;
        if (!pidfp)
                return 0;

        ret = lockf (fileno (pidfp), F_TLOCK, 0);
        if (ret) {
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "pidfile %s lock failed",
                        cmd_args->pid_file);
                return ret;
        }

        ret = ftruncate (fileno (pidfp), 0);
        if (ret) {
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "pidfile %s truncation failed",
                        cmd_args->pid_file);
                return ret;
        }

        ret = fprintf (pidfp, "%d\n", getpid ());
        if (ret <= 0) {
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "pidfile %s write failed",
                        cmd_args->pid_file);
                return ret;
        }

        ret = fflush (pidfp);
        if (ret) {
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "pidfile %s write failed",
                        cmd_args->pid_file);
                return ret;
        }

        gf_log ("glusterfsd", GF_LOG_DEBUG,
                "pidfile %s updated with pid %d",
                cmd_args->pid_file, getpid ());

        return 0;
}


void *
glusterfs_sigwaiter (void *arg)
{
        sigset_t  set;
        int       ret = 0;
        int       sig = 0;


        sigaddset (&set, SIGINT);   /* cleanup_and_exit */
        sigaddset (&set, SIGTERM);  /* cleanup_and_exit */
        sigaddset (&set, SIGHUP);   /* reincarnate */
        sigaddset (&set, SIGUSR1);  /* gf_proc_dump_info */
        sigaddset (&set, SIGUSR2);  /* gf_latency_toggle */

        for (;;) {
                ret = sigwait (&set, &sig);
                if (ret) 
                        continue;
                                

                switch (sig) {
                case SIGINT:
                case SIGTERM:
                        cleanup_and_exit (sig);
                        break;
                case SIGHUP:
                        reincarnate (sig);
                        break;
                case SIGUSR1:
                        gf_proc_dump_info (sig);
                        break;
                case SIGUSR2:
                        gf_latency_toggle (sig);
                        break;
                default:
                        
                        break;
                }
        }

        return NULL;
}


int
glusterfs_signals_setup (glusterfs_ctx_t *ctx)
{
        sigset_t  set;
        int       ret = 0;

        sigemptyset (&set);

        /* common setting for all threads */
        signal (SIGSEGV, gf_print_trace);
        signal (SIGABRT, gf_print_trace);
        signal (SIGILL, gf_print_trace);
        signal (SIGTRAP, gf_print_trace);
        signal (SIGFPE, gf_print_trace);
        signal (SIGBUS, gf_print_trace);
        signal (SIGINT, cleanup_and_exit);
        signal (SIGPIPE, SIG_IGN);

        /* block these signals from non-sigwaiter threads */
        sigaddset (&set, SIGTERM);  /* cleanup_and_exit */
        sigaddset (&set, SIGHUP);   /* reincarnate */
        sigaddset (&set, SIGUSR1);  /* gf_proc_dump_info */
        sigaddset (&set, SIGUSR2);  /* gf_latency_toggle */

        ret = pthread_sigmask (SIG_BLOCK, &set, NULL);
        if (ret)
                return ret;

        ret = pthread_create (&ctx->sigwaiter, NULL, glusterfs_sigwaiter,
                              (void *) &set);
        if (ret) {
                /*
                  TODO:
                  fallback to signals getting handled by other threads.
                  setup the signal handlers
                */
                return ret;
        }

        return ret;
}


int
daemonize (glusterfs_ctx_t *ctx)
{
        int            ret = 0;
        cmd_args_t    *cmd_args = NULL;


        cmd_args = &ctx->cmd_args;

        ret = glusterfs_pidfile_setup (ctx);
        if (ret)
                return ret;

        if (cmd_args->no_daemon_mode)
                goto postfork;

        if (cmd_args->debug_mode)
                goto postfork;

        ret = os_daemon (0, 0);
        if (ret == -1) {
                gf_log ("daemonize", GF_LOG_ERROR,
                        "Daemonization failed: %s", strerror(errno));
                return ret;
        }

postfork:
        ret = glusterfs_pidfile_update (ctx);
        if (ret)
                return ret;

        glusterfs_signals_setup (ctx);

        return ret;
}


int
glusterfs_process_volfp (glusterfs_ctx_t *ctx, FILE *fp)
{
        glusterfs_graph_t  *graph = NULL;
        int                 ret = 0;
        xlator_t           *trav = NULL;

        graph = glusterfs_graph_construct (fp);

        if (!graph) {
                ret = -1;
                goto out;
        }

        for (trav = graph->first; trav; trav = trav->next) {
                if (strcmp (trav->type, "mount/fuse") == 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "fuse xlator cannot be specified "
                                "in volume file");
                        ret = -1;
                        goto out;
                }
        }

        ret = glusterfs_graph_prepare (graph, ctx);

        if (ret) {
                glusterfs_graph_destroy (graph);
                ret = -1;
                goto out;
        }

        ret = glusterfs_graph_activate (graph, ctx);

        if (ret) {
                glusterfs_graph_destroy (graph);
                ret = -1;
                goto out;
        }

        gf_log_volume_file (fp);

out:
        if (fp)
                fclose (fp);

        if (ret && !ctx->active) {
                /* there is some error in setting up the first graph itself */
                cleanup_and_exit (0);
        }

        return ret;
}


int
glusterfs_volumes_init (glusterfs_ctx_t *ctx)
{
        FILE               *fp = NULL;
        cmd_args_t         *cmd_args = NULL;
        int                 ret = 0;

        cmd_args = &ctx->cmd_args;

        if (cmd_args->volfile_server) {
                ret = glusterfs_mgmt_init (ctx);
                goto out;
        }

        fp = get_volfp (ctx);

        if (!fp) {
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "Cannot reach volume specification file");
                ret = -1;
                goto out;
        }

        ret = glusterfs_process_volfp (ctx, fp);
        if (ret)
                goto out;

out:
        return ret;
}


int
main (int argc, char *argv[])
{
        glusterfs_ctx_t  *ctx = NULL;
        int               ret = -1;

        ret = glusterfs_globals_init ();
        if (ret)
                return ret;

        ctx = glusterfs_ctx_get ();
        if (!ctx)
                return ENOMEM;

        ret = glusterfs_ctx_defaults_init (ctx);
        if (ret)
                goto out;

        ret = parse_cmdline (argc, argv, ctx);
        if (ret)
                goto out;

        ret = logging_init (ctx);
        if (ret)
                goto out;

        gf_proc_dump_init();

        ret = create_fuse_mount (ctx);
        if (ret)
                goto out;

        ret = daemonize (ctx);
        if (ret)
                goto out;

        ret = glusterfs_volumes_init (ctx);
        if (ret)
                goto out;

        ret = event_dispatch (ctx->event_pool);

out:
//        glusterfs_ctx_destroy (ctx);

        return ret;
}
