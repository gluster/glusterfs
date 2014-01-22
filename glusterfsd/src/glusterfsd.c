/*
   Copyright (c) 2006-2013 Red Hat, Inc. <http://www.redhat.com>
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
#include <sys/wait.h>
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
#include <pwd.h>

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
#include "syncop.h"
#include "client_t.h"

#include "daemon.h"

/* process mode definitions */
#define GF_SERVER_PROCESS   0
#define GF_CLIENT_PROCESS   1
#define GF_GLUSTERD_PROCESS 2

/* using argp for command line parsing */
static char gf_doc[] = "";
static char argp_doc[] = "--volfile-server=SERVER [MOUNT-POINT]\n"       \
        "--volfile=VOLFILE [MOUNT-POINT]";
const char *argp_program_version = ""
        PACKAGE_NAME" "PACKAGE_VERSION" built on "__DATE__" "__TIME__
        "\nRepository revision: " GLUSTERFS_REPOSITORY_REVISION "\n"
        "Copyright (c) 2006-2013 Red Hat, Inc. <http://www.redhat.com/>\n"
        "GlusterFS comes with ABSOLUTELY NO WARRANTY.\n"
        "It is licensed to you under your choice of the GNU Lesser\n"
        "General Public License, version 3 or any later version (LGPLv3\n"
        "or later), or the GNU General Public License, version 2 (GPLv2),\n"
        "in all cases as published by the Free Software Foundation.";
const char *argp_program_bug_address = "<" PACKAGE_BUGREPORT ">";

static error_t parse_opts (int32_t key, char *arg, struct argp_state *_state);

static struct argp_option gf_options[] = {
        {0, 0, 0, 0, "Basic options:"},
        {"volfile-server", ARGP_VOLFILE_SERVER_KEY, "SERVER", 0,
         "Server to get the volume file from.  This option overrides "
         "--volfile option"},
        {"volfile", ARGP_VOLUME_FILE_KEY, "VOLFILE", 0,
         "File to use as VOLUME_FILE"},
        {"spec-file", ARGP_VOLUME_FILE_KEY, "VOLFILE", OPTION_HIDDEN,
         "File to use as VOLUME FILE"},

        {"log-level", ARGP_LOG_LEVEL_KEY, "LOGLEVEL", 0,
         "Logging severity.  Valid options are DEBUG, INFO, WARNING, ERROR, "
         "CRITICAL, TRACE and NONE [default: INFO]"},
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
        {"pid-file", ARGP_PID_FILE_KEY, "PIDFILE", 0,
         "File to use as pid file"},
        {"socket-file", ARGP_SOCK_FILE_KEY, "SOCKFILE", 0,
         "File to use as unix-socket"},
        {"no-daemon", ARGP_NO_DAEMON_KEY, 0, 0,
         "Run in foreground"},
        {"run-id", ARGP_RUN_ID_KEY, "RUN-ID", OPTION_HIDDEN,
         "Run ID for the process, used by scripts to keep track of process "
         "they started, defaults to none"},
        {"debug", ARGP_DEBUG_KEY, 0, 0,
         "Run in debug mode.  This option sets --no-daemon, --log-level "
         "to DEBUG and --log-file to console"},
        {"volume-name", ARGP_VOLUME_NAME_KEY, "XLATOR-NAME", 0,
         "Translator name to be used for MOUNT-POINT [default: top most volume "
         "definition in VOLFILE]"},
        {"xlator-option", ARGP_XLATOR_OPTION_KEY,"XLATOR-NAME.OPTION=VALUE", 0,
         "Add/override an option for a translator in volume file with specified"
         " value"},
        {"read-only", ARGP_READ_ONLY_KEY, 0, 0,
         "Mount the filesystem in 'read-only' mode"},
        {"acl", ARGP_ACL_KEY, 0, 0,
         "Mount the filesystem with POSIX ACL support"},
        {"selinux", ARGP_SELINUX_KEY, 0, 0,
         "Enable SELinux label (extened attributes) support on inodes"},
        {"volfile-max-fetch-attempts", ARGP_VOLFILE_MAX_FETCH_ATTEMPTS, "0",
         OPTION_HIDDEN, "Maximum number of attempts to fetch the volfile"},

#ifdef GF_LINUX_HOST_OS
        {"aux-gfid-mount", ARGP_AUX_GFID_MOUNT_KEY, 0, 0,
         "Enable access to filesystem through gfid directly"},
#endif
        {"enable-ino32", ARGP_INODE32_KEY, "BOOL", OPTION_ARG_OPTIONAL,
         "Use 32-bit inodes when mounting to workaround broken applications"
         "that don't support 64-bit inodes"},
        {"worm", ARGP_WORM_KEY, 0, 0,
         "Mount the filesystem in 'worm' mode"},
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
	{"fopen-keep-cache", ARGP_FOPEN_KEEP_CACHE_KEY, "BOOL", OPTION_ARG_OPTIONAL,
	 "Do not purge the cache on file open"},

        {0, 0, 0, 0, "Fuse options:"},
        {"direct-io-mode", ARGP_DIRECT_IO_MODE_KEY, "BOOL", OPTION_ARG_OPTIONAL,
         "Use direct I/O mode in fuse kernel module"
         " [default: \"off\" if big writes are supported, else "
         "\"on\" for fds not opened with O_RDONLY]"},
        {"entry-timeout", ARGP_ENTRY_TIMEOUT_KEY, "SECONDS", 0,
         "Set entry timeout to SECONDS in fuse kernel module [default: 1]"},
        {"negative-timeout", ARGP_NEGATIVE_TIMEOUT_KEY, "SECONDS", 0,
         "Set negative timeout to SECONDS in fuse kernel module [default: 0]"},
        {"attribute-timeout", ARGP_ATTRIBUTE_TIMEOUT_KEY, "SECONDS", 0,
         "Set attribute timeout to SECONDS for inodes in fuse kernel module "
         "[default: 1]"},
	{"gid-timeout", ARGP_GID_TIMEOUT_KEY, "SECONDS", 0,
	 "Set auxilary group list timeout to SECONDS for fuse translator "
	 "[default: 0]"},
	{"background-qlen", ARGP_FUSE_BACKGROUND_QLEN_KEY, "N", 0,
	 "Set fuse module's background queue length to N "
	 "[default: 64]"},
	{"congestion-threshold", ARGP_FUSE_CONGESTION_THRESHOLD_KEY, "N", 0,
	 "Set fuse module's congestion threshold to N "
	 "[default: 48]"},
        {"client-pid", ARGP_CLIENT_PID_KEY, "PID", OPTION_HIDDEN,
         "client will authenticate itself with process id PID to server"},
        {"user-map-root", ARGP_USER_MAP_ROOT_KEY, "USER", OPTION_HIDDEN,
         "replace USER with root in messages"},
        {"dump-fuse", ARGP_DUMP_FUSE_KEY, "PATH", 0,
         "Dump fuse traffic to PATH"},
        {"volfile-check", ARGP_VOLFILE_CHECK_KEY, 0, 0,
         "Enable strict volume file checking"},
        {"mem-accounting", ARGP_MEM_ACCOUNTING_KEY, 0, OPTION_HIDDEN,
         "Enable internal memory accounting"},
        {"fuse-mountopts", ARGP_FUSE_MOUNTOPTS_KEY, "OPTIONS", OPTION_HIDDEN,
         "Extra mount options to pass to FUSE"},
        {"use-readdirp", ARGP_FUSE_USE_READDIRP_KEY, "BOOL", OPTION_ARG_OPTIONAL,
         "Use readdirp mode in fuse kernel module"
         " [default: \"off\"]"},
        {0, 0, 0, 0, "Miscellaneous Options:"},
        {0, }
};


static struct argp argp = { gf_options, parse_opts, argp_doc, gf_doc };

int glusterfs_pidfile_cleanup (glusterfs_ctx_t *ctx);
int glusterfs_volumes_init (glusterfs_ctx_t *ctx);
int glusterfs_mgmt_init (glusterfs_ctx_t *ctx);
int glusterfs_listener_init (glusterfs_ctx_t *ctx);
int glusterfs_listener_stop (glusterfs_ctx_t *ctx);


static int
set_fuse_mount_options (glusterfs_ctx_t *ctx, dict_t *options)
{
        int              ret = 0;
        cmd_args_t      *cmd_args = NULL;
        char            *mount_point = NULL;
        char            cwd[PATH_MAX] = {0,};

        cmd_args = &ctx->cmd_args;

        /* Check if mount-point is absolute path,
         * if not convert to absolute path by concating with CWD
         */
        if (cmd_args->mount_point[0] != '/') {
                if (getcwd (cwd, PATH_MAX) != NULL) {
                        ret = gf_asprintf (&mount_point, "%s/%s", cwd,
                                           cmd_args->mount_point);
                        if (ret == -1) {
                                gf_log ("glusterfsd", GF_LOG_ERROR,
                                        "Could not create absolute mountpoint "
                                        "path");
                                goto err;
                        }
                } else {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "Could not get current working directory");
                        goto err;
                }
        } else
                mount_point = gf_strdup (cmd_args->mount_point);

        ret = dict_set_dynstr (options, ZR_MOUNTPOINT_OPT, mount_point);
        if (ret < 0) {
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "failed to set mount-point to options dictionary");
                goto err;
        }

        if (cmd_args->fuse_attribute_timeout >= 0) {
                ret = dict_set_double (options, ZR_ATTR_TIMEOUT_OPT,
                                       cmd_args->fuse_attribute_timeout);

                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value for key %s",
                                ZR_ATTR_TIMEOUT_OPT);
                        goto err;
                }
        }

        if (cmd_args->fuse_entry_timeout >= 0) {
                ret = dict_set_double (options, ZR_ENTRY_TIMEOUT_OPT,
                                       cmd_args->fuse_entry_timeout);
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value for key %s",
                                ZR_ENTRY_TIMEOUT_OPT);
                        goto err;
                }
        }

        if (cmd_args->fuse_negative_timeout >= 0) {
                ret = dict_set_double (options, ZR_NEGATIVE_TIMEOUT_OPT,
                                       cmd_args->fuse_negative_timeout);
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value for key %s",
                                ZR_NEGATIVE_TIMEOUT_OPT);
                        goto err;
                }
        }

        if (cmd_args->client_pid_set) {
                ret = dict_set_int32 (options, "client-pid",
                                      cmd_args->client_pid);
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value for key %s",
                                "client-pid");
                        goto err;
                }
        }

        if (cmd_args->uid_map_root) {
                ret = dict_set_int32 (options, "uid-map-root",
                                      cmd_args->uid_map_root);
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value for key %s",
                                "uid-map-root");
                        goto err;
                }
        }

        if (cmd_args->volfile_check) {
                ret = dict_set_int32 (options, ZR_STRICT_VOLFILE_CHECK,
                                      cmd_args->volfile_check);
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value for key %s",
                                ZR_STRICT_VOLFILE_CHECK);
                        goto err;
                }
        }

        if (cmd_args->dump_fuse) {
                ret = dict_set_static_ptr (options, ZR_DUMP_FUSE,
                                           cmd_args->dump_fuse);
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value for key %s",
                                ZR_DUMP_FUSE);
                        goto err;
                }
        }

        if (cmd_args->acl) {
                ret = dict_set_static_ptr (options, "acl", "on");
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value for key acl");
                        goto err;
                }
        }

        if (cmd_args->selinux) {
                ret = dict_set_static_ptr (options, "selinux", "on");
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value for key selinux");
                        goto err;
                }
        }

        if (cmd_args->aux_gfid_mount) {
                ret = dict_set_static_ptr (options, "virtual-gfid-access",
                                           "on");
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value for key "
                                "aux-gfid-mount");
                        goto err;
                }
        }

        if (cmd_args->enable_ino32) {
                ret = dict_set_static_ptr (options, "enable-ino32", "on");
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value for key enable-ino32");
                        goto err;
                }
        }

        if (cmd_args->read_only) {
                ret = dict_set_static_ptr (options, "read-only", "on");
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value for key read-only");
                        goto err;
                }
        }

	switch (cmd_args->fopen_keep_cache) {
	case GF_OPTION_ENABLE:
		ret = dict_set_static_ptr(options, "fopen-keep-cache",
			"on");
		if (ret < 0) {
			gf_log("glusterfsd", GF_LOG_ERROR,
				"failed to set dict value for key "
				"fopen-keep-cache");
			goto err;
		}
		break;
	case GF_OPTION_DISABLE:
		ret = dict_set_static_ptr(options, "fopen-keep-cache",
			"off");
		if (ret < 0) {
			gf_log("glusterfsd", GF_LOG_ERROR,
				"failed to set dict value for key "
				"fopen-keep-cache");
			goto err;
		}
		break;
        case GF_OPTION_DEFERRED: /* default */
        default:
                gf_log ("glusterfsd", GF_LOG_DEBUG,
			"fopen-keep-cache mode %d",
                        cmd_args->fopen_keep_cache);
                break;
	}

	if (cmd_args->gid_timeout_set) {
		ret = dict_set_int32(options, "gid-timeout",
			cmd_args->gid_timeout);
		if (ret < 0) {
			gf_log("glusterfsd", GF_LOG_ERROR, "failed to set dict "
				"value for key gid-timeout");
			goto err;
		}
	}
	if (cmd_args->background_qlen) {
		ret = dict_set_int32 (options, "background-qlen",
                                      cmd_args->background_qlen);
		if (ret < 0) {
			gf_log("glusterfsd", GF_LOG_ERROR, "failed to set dict "
                               "value for key background-qlen");
			goto err;
		}
	}
	if (cmd_args->congestion_threshold) {
		ret = dict_set_int32 (options, "congestion-threshold",
                                      cmd_args->congestion_threshold);
		if (ret < 0) {
			gf_log("glusterfsd", GF_LOG_ERROR, "failed to set dict "
                               "value for key congestion-threshold");
			goto err;
		}
	}

        switch (cmd_args->fuse_direct_io_mode) {
        case GF_OPTION_DISABLE: /* disable */
                ret = dict_set_static_ptr (options, ZR_DIRECT_IO_OPT,
                                           "disable");
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set 'disable' for key %s",
                                ZR_DIRECT_IO_OPT);
                        goto err;
                }
                break;
        case GF_OPTION_ENABLE: /* enable */
                ret = dict_set_static_ptr (options, ZR_DIRECT_IO_OPT,
                                           "enable");
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set 'enable' for key %s",
                                ZR_DIRECT_IO_OPT);
                        goto err;
                }
                break;
        case GF_OPTION_DEFERRED: /* default */
        default:
                gf_log ("", GF_LOG_DEBUG, "fuse direct io type %d",
                        cmd_args->fuse_direct_io_mode);
                break;
        }

        if (!cmd_args->no_daemon_mode) {
                ret = dict_set_static_ptr (options, "sync-to-mount",
                                           "enable");
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value for key sync-mtab");
                        goto err;
                }
        }

        if (cmd_args->use_readdirp) {
                ret = dict_set_str (options, "use-readdirp",
                                    cmd_args->use_readdirp);
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR, "failed to set dict"
                                " value for key use-readdirp");
                        goto err;
                }
        }
        ret = 0;
err:
        return ret;
}

int
create_fuse_mount (glusterfs_ctx_t *ctx)
{
        int              ret = 0;
        cmd_args_t      *cmd_args = NULL;
        xlator_t        *master = NULL;

        cmd_args = &ctx->cmd_args;

        if (!cmd_args->mount_point) {
                gf_log ("", GF_LOG_TRACE,
                        "mount point not found, not a client process");
                return 0;
        }

        if (ctx->process_mode != GF_CLIENT_PROCESS) {
                gf_log("glusterfsd", GF_LOG_ERROR,
                       "Not a client process, not performing mount operation");
                return -1;
        }

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
        if (!master->options)
                goto err;

        ret = set_fuse_mount_options (ctx, master->options);
        if (ret)
                goto err;

        if (cmd_args->fuse_mountopts) {
                ret = dict_set_static_ptr (master->options, ZR_FUSE_MOUNTOPTS,
                                           cmd_args->fuse_mountopts);
                if (ret < 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "failed to set dict value for key %s",
                                ZR_FUSE_MOUNTOPTS);
                        goto err;
                }
        }

        ret = xlator_init (master);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "failed to initialize fuse translator");
                goto err;
        }

        ctx->master = master;

        return 0;

err:
        if (master) {
                xlator_destroy (master);
        }

        return 1;
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
gf_remember_backup_volfile_server (char *arg)
{
        glusterfs_ctx_t         *ctx = NULL;
        cmd_args_t              *cmd_args = NULL;
        int                      ret = -1;
        server_cmdline_t        *server = NULL;

        ctx = glusterfsd_ctx;
        if (!ctx)
                goto out;
        cmd_args = &ctx->cmd_args;

        if(!cmd_args)
                goto out;

        server = GF_CALLOC (1, sizeof (server_cmdline_t),
                            gfd_mt_server_cmdline_t);
        if (!server)
                goto out;

        INIT_LIST_HEAD(&server->list);

        server->volfile_server = gf_strdup(arg);

        if (!cmd_args->volfile_server) {
                cmd_args->volfile_server = server->volfile_server;
                cmd_args->curr_server = server;
        }

        if (!server->volfile_server) {
                gf_log ("", GF_LOG_WARNING,
                        "xlator option %s is invalid", arg);
                goto out;
        }

        list_add_tail (&server->list, &cmd_args->volfile_servers);

        ret = 0;
out:
        if (ret == -1) {
                if (server) {
                        GF_FREE (server->volfile_server);
                        GF_FREE (server);
                }
        }

        return ret;

}

static int
gf_remember_xlator_option (char *arg)
{
        glusterfs_ctx_t         *ctx = NULL;
        cmd_args_t              *cmd_args  = NULL;
        xlator_cmdline_option_t *option = NULL;
        int                      ret = -1;
        char                    *dot = NULL;
        char                    *equals = NULL;

        ctx = glusterfsd_ctx;
        cmd_args = &ctx->cmd_args;

        option = GF_CALLOC (1, sizeof (xlator_cmdline_option_t),
                            gfd_mt_xlator_cmdline_option_t);
        if (!option)
                goto out;

        INIT_LIST_HEAD (&option->cmd_args);

        dot = strchr (arg, '.');
        if (!dot) {
                gf_log ("", GF_LOG_WARNING,
                        "xlator option %s is invalid", arg);
                goto out;
        }

        option->volume = GF_CALLOC ((dot - arg) + 1, sizeof (char),
                                    gfd_mt_char);
        if (!option->volume)
                goto out;

        strncpy (option->volume, arg, (dot - arg));

        equals = strchr (arg, '=');
        if (!equals) {
                gf_log ("", GF_LOG_WARNING,
                        "xlator option %s is invalid", arg);
                goto out;
        }

        option->key = GF_CALLOC ((equals - dot) + 1, sizeof (char),
                                 gfd_mt_char);
        if (!option->key)
                goto out;

        strncpy (option->key, dot + 1, (equals - dot - 1));

        if (!*(equals + 1)) {
                gf_log ("", GF_LOG_WARNING,
                        "xlator option %s is invalid", arg);
                goto out;
        }

        option->value = gf_strdup (equals + 1);

        list_add (&option->cmd_args, &cmd_args->xlator_options);

        ret = 0;
out:
        if (ret == -1) {
                if (option) {
                        GF_FREE (option->volume);
                        GF_FREE (option->key);
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
        char         *tmp_str       = NULL;
        char         *port_str      = NULL;
        struct passwd *pw           = NULL;
        int           ret           = 0;

        cmd_args = state->input;

        switch (key) {
        case ARGP_VOLFILE_SERVER_KEY:
                gf_remember_backup_volfile_server (arg);

                break;

        case ARGP_READ_ONLY_KEY:
                cmd_args->read_only = 1;
                break;

        case ARGP_ACL_KEY:
                cmd_args->acl = 1;
                gf_remember_xlator_option ("*-md-cache.cache-posix-acl=true");
                break;

        case ARGP_SELINUX_KEY:
                cmd_args->selinux = 1;
                gf_remember_xlator_option ("*-md-cache.cache-selinux=true");
                break;

        case ARGP_AUX_GFID_MOUNT_KEY:
                cmd_args->aux_gfid_mount = 1;
                break;

        case ARGP_INODE32_KEY:
                cmd_args->enable_ino32 = 1;
                break;

        case ARGP_WORM_KEY:
                cmd_args->worm = 1;
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
                if (strcasecmp (arg, ARGP_LOG_LEVEL_INFO_OPTION) == 0) {
                        cmd_args->log_level = GF_LOG_INFO;
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

        case ARGP_VOLFILE_SERVER_TRANSPORT_KEY:
                cmd_args->volfile_server_transport = gf_strdup (arg);
                break;

        case ARGP_VOLFILE_ID_KEY:
                cmd_args->volfile_id = gf_strdup (arg);
                break;

        case ARGP_PID_FILE_KEY:
                cmd_args->pid_file = gf_strdup (arg);
                break;

        case ARGP_SOCK_FILE_KEY:
                cmd_args->sock_file = gf_strdup (arg);
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
        case ARGP_VOLFILE_MAX_FETCH_ATTEMPTS:
                cmd_args->max_connect_attempts = 1;
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

        case ARGP_NEGATIVE_TIMEOUT_KEY:
                d = 0.0;

                ret = gf_string2double (arg, &d);
                if ((ret == 0) && !(d < 0.0)) {
                        cmd_args->fuse_negative_timeout = d;
                        break;
                }

                argp_failure (state, -1, 0, "unknown negative timeout %s", arg);
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

        case ARGP_USER_MAP_ROOT_KEY:
                pw = getpwnam (arg);
                if (pw)
                        cmd_args->uid_map_root = pw->pw_uid;
                else
                        argp_failure (state, -1, 0,
                                      "user %s does not exist", arg);
                break;

        case ARGP_VOLFILE_CHECK_KEY:
                cmd_args->volfile_check = 1;
                break;

        case ARGP_VOLUME_NAME_KEY:
                cmd_args->volume_name = gf_strdup (arg);
                break;

        case ARGP_XLATOR_OPTION_KEY:
                if (gf_remember_xlator_option (arg))
                        argp_failure (state, -1, 0, "invalid xlator option  %s",
                                      arg);

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

                port_str = strtok_r (arg, ",", &tmp_str);
                if (gf_string2uint_base10 (port_str, &n) == 0) {
                        cmd_args->brick_port = n;
                        port_str = strtok_r (NULL, ",", &tmp_str);
                        if (port_str) {
                                if (gf_string2uint_base10 (port_str, &n) == 0)
                                        cmd_args->brick_port2 = n;
                                break;

                                argp_failure (state, -1, 0,
                                              "wrong brick (listen) port %s", arg);
                        }
                        break;
                }

                argp_failure (state, -1, 0,
                              "unknown brick (listen) port %s", arg);
                break;

        case ARGP_MEM_ACCOUNTING_KEY:
                /* TODO: it should have got handled much earlier */
		//gf_mem_acct_enable_set (THIS->ctx);
                break;

	case ARGP_FOPEN_KEEP_CACHE_KEY:
                if (!arg)
                        arg = "on";

                if (gf_string2boolean (arg, &b) == 0) {
                        cmd_args->fopen_keep_cache = b;

                        break;
                }

                argp_failure (state, -1, 0,
                              "unknown cache setting \"%s\"", arg);

		break;

	case ARGP_GID_TIMEOUT_KEY:
		if (!gf_string2int(arg, &cmd_args->gid_timeout)) {
			cmd_args->gid_timeout_set = _gf_true;
			break;
		}

		argp_failure(state, -1, 0, "unknown group list timeout %s", arg);
		break;
        case ARGP_FUSE_BACKGROUND_QLEN_KEY:
                if (!gf_string2int (arg, &cmd_args->background_qlen))
                        break;

                argp_failure (state, -1, 0,
                              "unknown background qlen option %s", arg);
                break;
        case ARGP_FUSE_CONGESTION_THRESHOLD_KEY:
                if (!gf_string2int (arg, &cmd_args->congestion_threshold))
                        break;

                argp_failure (state, -1, 0,
                              "unknown congestion threshold option %s", arg);
                break;

        case ARGP_FUSE_MOUNTOPTS_KEY:
                cmd_args->fuse_mountopts = gf_strdup (arg);
                break;

        case ARGP_FUSE_USE_READDIRP_KEY:
                if (!arg)
                        arg = "yes";

                if (gf_string2boolean (arg, &b) == 0) {
                        if (b) {
                                cmd_args->use_readdirp = "yes";
                        } else {
                                cmd_args->use_readdirp = "no";
                        }

                        break;
                }

                argp_failure (state, -1, 0,
                              "unknown use-readdirp setting \"%s\"", arg);
                break;

	}

        return 0;
}


void
cleanup_and_exit (int signum)
{
        glusterfs_ctx_t *ctx      = NULL;
        xlator_t        *trav     = NULL;

        ctx = glusterfsd_ctx;

        if (!ctx)
                return;

        gf_log_callingfn ("", GF_LOG_WARNING,
                          "received signum (%d), shutting down", signum);

        if (ctx->cleanup_started)
                return;

        ctx->cleanup_started = 1;
        glusterfs_mgmt_pmap_signout (ctx);

        /* below part is a racy code where the rpcsvc object is freed.
         * But in another thread (epoll thread), upon poll error in the
         * socket the transports are cleaned up where again rpcsvc object
         * is accessed (which is already freed by the below function).
         * Since the process is about to be killed dont execute the function
         * below.
         */
        /* if (ctx->listener) { */
        /*         (void) glusterfs_listener_stop (ctx); */
        /* } */

        /* Call fini() of FUSE xlator first:
         * so there are no more requests coming and
         * 'umount' of mount point is done properly */
        trav = ctx->master;
        if (trav && trav->fini) {
                THIS = trav;
                trav->fini (trav);
        }

        glusterfs_pidfile_cleanup (ctx);

        exit (0);
#if 0
        /* TODO: Properly do cleanup_and_exit(), with synchronization */
        if (ctx->mgmt) {
                /* cleanup the saved-frames before last unref */
                rpc_clnt_connection_cleanup (&ctx->mgmt->conn);
                rpc_clnt_unref (ctx->mgmt);
        }

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

        ctx = glusterfsd_ctx;
        cmd_args = &ctx->cmd_args;

        if (cmd_args->volfile_server) {
                gf_log ("glusterfsd", GF_LOG_INFO,
                        "Fetching the volume file from server...");
                ret = glusterfs_volfile_fetch (ctx);
        } else {
                gf_log ("glusterfsd", GF_LOG_DEBUG,
                        "Not reloading volume specification file on SIGHUP");
        }

        /* Also, SIGHUP should do logrotate */
        gf_log_logrotate (1);

        if (ret < 0)
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "volume initialization failed.");

        return;
}

void
emancipate (glusterfs_ctx_t *ctx, int ret)
{
        /* break free from the parent */
        if (ctx->daemon_pipe[1] != -1) {
                write (ctx->daemon_pipe[1], (void *) &ret, sizeof (ret));
                close (ctx->daemon_pipe[1]);
                ctx->daemon_pipe[1] = -1;
        }
}

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
glusterfs_ctx_defaults_init (glusterfs_ctx_t *ctx)
{
        cmd_args_t          *cmd_args = NULL;
        struct rlimit        lim      = {0, };
        int                  ret      = -1;

        xlator_mem_acct_init (THIS, gfd_mt_end);

        ctx->process_uuid = generate_glusterfs_ctx_id ();
        if (!ctx->process_uuid) {
                gf_log ("", GF_LOG_CRITICAL,
                        "ERROR: glusterfs uuid generation failed");
                goto out;
        }

        ctx->page_size  = 128 * GF_UNIT_KB;

        ctx->iobuf_pool = iobuf_pool_new ();
        if (!ctx->iobuf_pool) {
                gf_log ("", GF_LOG_CRITICAL,
                        "ERROR: glusterfs iobuf pool creation failed");
                goto out;
        }

        ctx->event_pool = event_pool_new (DEFAULT_EVENT_POOL_SIZE);
        if (!ctx->event_pool) {
                gf_log ("", GF_LOG_CRITICAL,
                        "ERROR: glusterfs event pool creation failed");
                goto out;
        }

        ctx->pool = GF_CALLOC (1, sizeof (call_pool_t), gfd_mt_call_pool_t);
        if (!ctx->pool) {
                gf_log ("", GF_LOG_CRITICAL,
                        "ERROR: glusterfs call pool creation failed");
                goto out;
        }

        INIT_LIST_HEAD (&ctx->pool->all_frames);
        LOCK_INIT (&ctx->pool->lock);

        /* frame_mem_pool size 112 * 4k */
        ctx->pool->frame_mem_pool = mem_pool_new (call_frame_t, 4096);
        if (!ctx->pool->frame_mem_pool) {
                gf_log ("", GF_LOG_CRITICAL,
                        "ERROR: glusterfs frame pool creation failed");
                goto out;
        }
        /* stack_mem_pool size 256 * 1024 */
        ctx->pool->stack_mem_pool = mem_pool_new (call_stack_t, 1024);
        if (!ctx->pool->stack_mem_pool) {
                gf_log ("", GF_LOG_CRITICAL,
                        "ERROR: glusterfs stack pool creation failed");
                goto out;
        }

        ctx->stub_mem_pool = mem_pool_new (call_stub_t, 1024);
        if (!ctx->stub_mem_pool) {
                gf_log ("", GF_LOG_CRITICAL,
                        "ERROR: glusterfs stub pool creation failed");
                goto out;
        }

        ctx->dict_pool = mem_pool_new (dict_t, GF_MEMPOOL_COUNT_OF_DICT_T);
        if (!ctx->dict_pool)
                goto out;

        ctx->dict_pair_pool = mem_pool_new (data_pair_t,
                                            GF_MEMPOOL_COUNT_OF_DATA_PAIR_T);
        if (!ctx->dict_pair_pool)
                goto out;

        ctx->dict_data_pool = mem_pool_new (data_t, GF_MEMPOOL_COUNT_OF_DATA_T);
        if (!ctx->dict_data_pool)
                goto out;

        pthread_mutex_init (&(ctx->lock), NULL);

        ctx->clienttable = gf_clienttable_alloc();
        if (!ctx->clienttable)
                goto out;

        cmd_args = &ctx->cmd_args;

        /* parsing command line arguments */
        cmd_args->log_level = DEFAULT_LOG_LEVEL;

        cmd_args->mac_compat = GF_OPTION_DISABLE;
#ifdef GF_DARWIN_HOST_OS
        /* On Darwin machines, O_APPEND is not handled,
         * which may corrupt the data
         */
        cmd_args->fuse_direct_io_mode = GF_OPTION_DISABLE;
#else
        cmd_args->fuse_direct_io_mode = GF_OPTION_DEFERRED;
#endif
        cmd_args->fuse_attribute_timeout = -1;
        cmd_args->fuse_entry_timeout = -1;
	cmd_args->fopen_keep_cache = GF_OPTION_DEFERRED;

        INIT_LIST_HEAD (&cmd_args->xlator_options);
        INIT_LIST_HEAD (&cmd_args->volfile_servers);

        lim.rlim_cur = RLIM_INFINITY;
        lim.rlim_max = RLIM_INFINITY;
        setrlimit (RLIMIT_CORE, &lim);

        ret = 0;
out:

        if (ret && ctx) {
                if (ctx->pool) {
                        mem_pool_destroy (ctx->pool->frame_mem_pool);
                        mem_pool_destroy (ctx->pool->stack_mem_pool);
                }
                GF_FREE (ctx->pool);
                mem_pool_destroy (ctx->stub_mem_pool);
                mem_pool_destroy (ctx->dict_pool);
                mem_pool_destroy (ctx->dict_data_pool);
                mem_pool_destroy (ctx->dict_pair_pool);
        }

        return ret;
}

static int
logging_init (glusterfs_ctx_t *ctx, const char *progpath)
{
        cmd_args_t *cmd_args = NULL;
        int         ret = 0;
        char        ident[1024] = {0,};
        char       *progname = NULL;
        char       *ptr = NULL;

        cmd_args = &ctx->cmd_args;

        if (cmd_args->log_file == NULL) {
                ret = gf_set_log_file_path (cmd_args);
                if (ret == -1) {
                        fprintf (stderr, "ERROR: failed to set the log file path\n");
                        return -1;
                }
        }

#ifdef GF_USE_SYSLOG
        progname  = gf_strdup (progpath);
        snprintf (ident, 1024, "%s_%s", basename(progname),
                  basename(cmd_args->log_file));
        GF_FREE (progname);
        /* remove .log suffix */
        if (NULL != (ptr = strrchr(ident, '.'))) {
                if (strcmp(ptr, ".log") == 0) {
                        /* note: ptr points to location in ident only */
                        ptr[0] = '\0';
                }
        }
        ptr = ident;
#endif

        if (gf_log_init (ctx, cmd_args->log_file, ptr) == -1) {
                fprintf (stderr, "ERROR: failed to open logfile %s\n",
                         cmd_args->log_file);
                return -1;
        }

        gf_log_set_loglevel (cmd_args->log_level);

        return 0;
}

void
gf_check_and_set_mem_acct (glusterfs_ctx_t *ctx, int argc, char *argv[])
{
        int i = 0;
        for (i = 0; i < argc; i++) {
                if (strcmp (argv[i], "--mem-accounting") == 0) {
			gf_mem_acct_enable_set (ctx);
                        break;
                }
        }
}

int
parse_cmdline (int argc, char *argv[], glusterfs_ctx_t *ctx)
{
        int          process_mode = 0;
        int          ret = 0;
        struct stat  stbuf = {0, };
        char         timestr[32];
        char         tmp_logfile[1024] = { 0 };
        char        *tmp_logfile_dyn = NULL;
        char        *tmp_logfilebase = NULL;
        cmd_args_t  *cmd_args = NULL;

        cmd_args = &ctx->cmd_args;

        argp_parse (&argp, argc, argv, ARGP_IN_ORDER, NULL, cmd_args);

        if (ENABLE_DEBUG_MODE == cmd_args->debug_mode) {
                cmd_args->log_level = GF_LOG_DEBUG;
                cmd_args->log_file = "/dev/stderr";
                cmd_args->no_daemon_mode = ENABLE_NO_DAEMON_MODE;
        }

        process_mode = gf_get_process_mode (argv[0]);
        ctx->process_mode = process_mode;

        /* Make sure after the parsing cli, if '--volfile-server' option is
           given, then '--volfile-id' is mandatory */
        if (cmd_args->volfile_server && !cmd_args->volfile_id) {
                gf_log ("glusterfs", GF_LOG_CRITICAL,
                        "ERROR: '--volfile-id' is mandatory if '-s' OR "
                        "'--volfile-server' option is given");
                ret = -1;
                goto out;
        }

        if ((cmd_args->volfile_server == NULL)
            && (cmd_args->volfile == NULL)) {
                if (process_mode == GF_SERVER_PROCESS)
                        cmd_args->volfile = gf_strdup (DEFAULT_SERVER_VOLFILE);
                else if (process_mode == GF_GLUSTERD_PROCESS)
                        cmd_args->volfile = gf_strdup (DEFAULT_GLUSTERD_VOLFILE);
                else
                        cmd_args->volfile = gf_strdup (DEFAULT_CLIENT_VOLFILE);

                /* Check if the volfile exists, if not give usage output
                   and exit */
                ret = stat (cmd_args->volfile, &stbuf);
                if (ret) {
                        gf_log ("glusterfs", GF_LOG_CRITICAL,
                                "ERROR: parsing the volfile failed (%s)\n",
                                strerror (errno));
                        /* argp_usage (argp.) */
                        fprintf (stderr, "USAGE: %s [options] [mountpoint]\n",
                                 argv[0]);
                        goto out;
                }
        }

        if (cmd_args->run_id) {
                ret = sys_lstat (cmd_args->log_file, &stbuf);
                /* If its /dev/null, or /dev/stdout, /dev/stderr,
                 * let it use the same, no need to alter
                 */
                if (((ret == 0) &&
                     (S_ISREG (stbuf.st_mode) || S_ISLNK (stbuf.st_mode))) ||
                    (ret == -1)) {
                        /* Have separate logfile per run */
                        gf_time_fmt (timestr, sizeof timestr, time (NULL),
                                     gf_timefmt_FT);
                        sprintf (tmp_logfile, "%s.%s.%d",
                                 cmd_args->log_file, timestr, getpid ());

                        /* Create symlink to actual log file */
                        sys_unlink (cmd_args->log_file);

                        tmp_logfile_dyn = gf_strdup (tmp_logfile);
                        tmp_logfilebase = basename (tmp_logfile_dyn);
                        ret = sys_symlink (tmp_logfilebase,
                                           cmd_args->log_file);
                        if (ret == -1) {
                                fprintf (stderr, "ERROR: symlink of logfile failed\n");
                                goto out;
                        }

                        GF_FREE (cmd_args->log_file);
                        cmd_args->log_file = gf_strdup (tmp_logfile);

                        GF_FREE (tmp_logfile_dyn);
                }
        }

        /*
           This option was made obsolete but parsing it for backward
           compatibility with third party applications
         */
        if (cmd_args->max_connect_attempts) {
                gf_log ("glusterfs", GF_LOG_WARNING,
                        "obsolete option '--volfile-max-fetch-attempts"
                        " or fetch-attempts' was provided");
        }

#ifdef GF_DARWIN_HOST_OS
        if (cmd_args->mount_point)
                cmd_args->mac_compat = GF_OPTION_DEFERRED;
#endif

        ret = 0;
out:
        return ret;
}


int
glusterfs_pidfile_setup (glusterfs_ctx_t *ctx)
{
        cmd_args_t  *cmd_args = NULL;
        int          ret = -1;
        FILE        *pidfp = NULL;

        cmd_args = &ctx->cmd_args;

        if (!cmd_args->pid_file)
                return 0;

        pidfp = fopen (cmd_args->pid_file, "a+");
        if (!pidfp) {
                gf_log ("glusterfsd", GF_LOG_ERROR,
                        "pidfile %s error (%s)",
                        cmd_args->pid_file, strerror (errno));
                goto out;
        }

        ctx->pidfp = pidfp;

        ret = 0;
out:
        if (ret && pidfp)
                fclose (pidfp);

        return ret;
}


int
glusterfs_pidfile_cleanup (glusterfs_ctx_t *ctx)
{
        cmd_args_t      *cmd_args = NULL;

        cmd_args = &ctx->cmd_args;

        if (!ctx->pidfp)
                return 0;

        gf_log ("glusterfsd", GF_LOG_TRACE,
                "pidfile %s cleanup",
                cmd_args->pid_file);

        if (ctx->cmd_args.pid_file) {
                unlink (ctx->cmd_args.pid_file);
                ctx->cmd_args.pid_file = NULL;
        }

        lockf (fileno (ctx->pidfp), F_ULOCK, 0);
        fclose (ctx->pidfp);
        ctx->pidfp = NULL;

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


        sigemptyset (&set);
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
                        gf_proc_dump_info (sig, glusterfsd_ctx);
                        break;
                case SIGUSR2:
                        gf_latency_toggle (sig, glusterfsd_ctx);
                        break;
                default:

                        break;
                }
        }

        return NULL;
}


void
glusterfsd_print_trace (int signum)
{
	gf_print_trace (signum, glusterfsd_ctx);
}


int
glusterfs_signals_setup (glusterfs_ctx_t *ctx)
{
        sigset_t  set;
        int       ret = 0;

        sigemptyset (&set);

        /* common setting for all threads */
        signal (SIGSEGV, glusterfsd_print_trace);
        signal (SIGABRT, glusterfsd_print_trace);
        signal (SIGILL, glusterfsd_print_trace);
        signal (SIGTRAP, glusterfsd_print_trace);
        signal (SIGFPE, glusterfsd_print_trace);
        signal (SIGBUS, glusterfsd_print_trace);
        signal (SIGINT, cleanup_and_exit);
        signal (SIGPIPE, SIG_IGN);

        /* block these signals from non-sigwaiter threads */
        sigaddset (&set, SIGTERM);  /* cleanup_and_exit */
        sigaddset (&set, SIGHUP);   /* reincarnate */
        sigaddset (&set, SIGUSR1);  /* gf_proc_dump_info */
        sigaddset (&set, SIGUSR2);  /* gf_latency_toggle */

        ret = pthread_sigmask (SIG_BLOCK, &set, NULL);
        if (ret) {
                gf_log ("glusterfsd", GF_LOG_WARNING,
                        "failed to execute pthread_signmask  %s",
                        strerror (errno));
                return ret;
        }

        ret = pthread_create (&ctx->sigwaiter, NULL, glusterfs_sigwaiter,
                              (void *) &set);
        if (ret) {
                /*
                  TODO:
                  fallback to signals getting handled by other threads.
                  setup the signal handlers
                */
                gf_log ("glusterfsd", GF_LOG_WARNING,
                        "failed to create pthread  %s",
                        strerror (errno));
                return ret;
        }

        return ret;
}


int
daemonize (glusterfs_ctx_t *ctx)
{
        int            ret = -1;
        cmd_args_t    *cmd_args = NULL;
        int            cstatus = 0;
        int            err = 0;

        cmd_args = &ctx->cmd_args;

        ret = glusterfs_pidfile_setup (ctx);
        if (ret)
                goto out;

        if (cmd_args->no_daemon_mode)
                goto postfork;

        if (cmd_args->debug_mode)
                goto postfork;

        ret = pipe (ctx->daemon_pipe);
        if (ret) {
                /* If pipe() fails, retain daemon_pipe[] = {-1, -1}
                   and parent will just not wait for child status
                */
                ctx->daemon_pipe[0] = -1;
                ctx->daemon_pipe[1] = -1;
        }

        ret = os_daemon_return (0, 0);
        switch (ret) {
        case -1:
                if (ctx->daemon_pipe[0] != -1) {
                        close (ctx->daemon_pipe[0]);
                        close (ctx->daemon_pipe[1]);
                }

                gf_log ("daemonize", GF_LOG_ERROR,
                        "Daemonization failed: %s", strerror(errno));
                goto out;
        case 0:
                /* child */
                /* close read */
                close (ctx->daemon_pipe[0]);
                break;
        default:
                /* parent */
                /* close write */
                close (ctx->daemon_pipe[1]);

                if (ctx->mnt_pid > 0) {
                        ret = waitpid (ctx->mnt_pid, &cstatus, 0);
                        if (!(ret == ctx->mnt_pid && cstatus == 0)) {
                                gf_log ("daemonize", GF_LOG_ERROR,
                                        "mount failed");
                                exit (1);
                        }
                }

                err = 1;
                read (ctx->daemon_pipe[0], (void *)&err, sizeof (err));
                _exit (err);
        }

postfork:
        ret = glusterfs_pidfile_update (ctx);
        if (ret)
                goto out;

        glusterfs_signals_setup (ctx);
out:
        return ret;
}


int
glusterfs_process_volfp (glusterfs_ctx_t *ctx, FILE *fp)
{
        glusterfs_graph_t  *graph = NULL;
        int                 ret = -1;
        xlator_t           *trav = NULL;

        graph = glusterfs_graph_construct (fp);
        if (!graph) {
                gf_log ("", GF_LOG_ERROR, "failed to construct the graph");
                goto out;
        }

        for (trav = graph->first; trav; trav = trav->next) {
                if (strcmp (trav->type, "mount/fuse") == 0) {
                        gf_log ("glusterfsd", GF_LOG_ERROR,
                                "fuse xlator cannot be specified "
                                "in volume file");
                        goto out;
                }
        }

        ret = glusterfs_graph_prepare (graph, ctx);
        if (ret) {
                glusterfs_graph_destroy (graph);
                goto out;
        }

        ret = glusterfs_graph_activate (graph, ctx);

        if (ret) {
                glusterfs_graph_destroy (graph);
                goto out;
        }

        gf_log_dump_graph (fp, graph);

        ret = 0;
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

        if (cmd_args->sock_file) {
                ret = glusterfs_listener_init (ctx);
                if (ret)
                        goto out;
        }

        if (cmd_args->volfile_server) {
                ret = glusterfs_mgmt_init (ctx);
                /* return, do not emancipate() yet */
                return ret;
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
        emancipate (ctx, ret);
        return ret;
}


/* This is the only legal global pointer  */
glusterfs_ctx_t *glusterfsd_ctx;

int
main (int argc, char *argv[])
{
        glusterfs_ctx_t  *ctx = NULL;
        int               ret = -1;
        char              cmdlinestr[PATH_MAX] = {0,};

	ctx = glusterfs_ctx_new ();
        if (!ctx) {
                gf_log ("glusterfs", GF_LOG_CRITICAL,
                        "ERROR: glusterfs context not initialized");
                return ENOMEM;
        }
	glusterfsd_ctx = ctx;

#ifdef DEBUG
        gf_mem_acct_enable_set (ctx);
#else
        /* Enable memory accounting on the fly based on argument */
        gf_check_and_set_mem_acct (ctx, argc, argv);
#endif

        ret = glusterfs_globals_init (ctx);
        if (ret)
                return ret;

	THIS->ctx = ctx;

        ret = glusterfs_ctx_defaults_init (ctx);
        if (ret)
                goto out;

        ret = parse_cmdline (argc, argv, ctx);
        if (ret)
                goto out;

        ret = logging_init (ctx, argv[0]);
        if (ret)
                goto out;

        /* log the version of glusterfs running here along with the actual
           command line options. */
        {
                int i = 0;
                strcpy (cmdlinestr, argv[0]);
                for (i = 1; i < argc; i++) {
                        strcat (cmdlinestr, " ");
                        strcat (cmdlinestr, argv[i]);
                }
                gf_log (argv[0], GF_LOG_INFO,
                        "Started running %s version %s (%s)",
                        argv[0], PACKAGE_VERSION, cmdlinestr);
        }

        gf_proc_dump_init();

        ret = create_fuse_mount (ctx);
        if (ret)
                goto out;

        ret = daemonize (ctx);
        if (ret)
                goto out;

	ctx->env = syncenv_new (0, 0, 0);
        if (!ctx->env) {
                gf_log ("", GF_LOG_ERROR,
                        "Could not create new sync-environment");
                goto out;
        }

        ret = glusterfs_volumes_init (ctx);
        if (ret)
                goto out;

        ret = event_dispatch (ctx->event_pool);

out:
//        glusterfs_ctx_destroy (ctx);

        return ret;
}
