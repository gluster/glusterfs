/*
   Copyright (c) 2006-2016 Red Hat, Inc. <http://www.redhat.com>
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
#include <dlfcn.h>

#include <sys/utsname.h>

#include <stdint.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <semaphore.h>
#include <errno.h>
#include <pwd.h>

#ifdef GF_LINUX_HOST_OS
#ifdef HAVE_LINUX_OOM_H
#include <linux/oom.h>
#else
#define OOM_SCORE_ADJ_MIN (-1000)
#define OOM_SCORE_ADJ_MAX   1000
#define OOM_DISABLE       (-17)
#define OOM_ADJUST_MAX      15
#endif
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
#include "glusterfsd-messages.h"
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
#include "netgroups.h"
#include "exports.h"

#include "daemon.h"
#include "tw.h"


/* using argp for command line parsing */
static char gf_doc[] = "";
static char argp_doc[] = "--volfile-server=SERVER [MOUNT-POINT]\n"       \
        "--volfile=VOLFILE [MOUNT-POINT]";
const char *argp_program_version = ""                                         \
        PACKAGE_NAME" "PACKAGE_VERSION                                        \
        "\nRepository revision: " GLUSTERFS_REPOSITORY_REVISION "\n"          \
        "Copyright (c) 2006-2016 Red Hat, Inc. "                              \
        "<https://www.gluster.org/>\n"                                        \
        "GlusterFS comes with ABSOLUTELY NO WARRANTY.\n"                      \
        "It is licensed to you under your choice of the GNU Lesser\n"         \
        "General Public License, version 3 or any later version (LGPLv3\n"    \
        "or later), or the GNU General Public License, version 2 (GPLv2),\n"  \
        "in all cases as published by the Free Software Foundation.";
const char *argp_program_bug_address = "<" PACKAGE_BUGREPORT ">";

static error_t parse_opts (int32_t key, char *arg, struct argp_state *_state);

static struct argp_option gf_options[] = {
        {0, 0, 0, 0, "Basic options:"},
        {"volfile-server", ARGP_VOLFILE_SERVER_KEY, "SERVER", 0,
         "Server to get the volume file from. Unix domain socket path when "
         "transport type 'unix'. This option overrides --volfile option"},
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
        {"logger", ARGP_LOGGER, "LOGGER", 0, "Set which logging sub-system to "
        "log to, valid options are: gluster-log and syslog, "
        "[default: \"gluster-log\"]"},
        {"log-format", ARGP_LOG_FORMAT, "LOG-FORMAT", 0, "Set log format, valid"
         " options are: no-msg-id and with-msg-id, [default: \"with-msg-id\"]"},
        {"log-buf-size", ARGP_LOG_BUF_SIZE, "LOG-BUF-SIZE", 0, "Set logging "
         "buffer size, [default: 5]"},
        {"log-flush-timeout", ARGP_LOG_FLUSH_TIMEOUT, "LOG-FLUSH-TIMEOUT", 0,
         "Set log flush timeout, [default: 2 minutes]"},

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
         "Enable SELinux label (extended attributes) support on inodes"},
        {"capability", ARGP_CAPABILITY_KEY, 0, 0,
         "Enable Capability (extended attributes) support on inodes"},

        {"print-netgroups", ARGP_PRINT_NETGROUPS, "NETGROUP-FILE", 0,
         "Validate the netgroups file and print it out"},
        {"print-exports", ARGP_PRINT_EXPORTS, "EXPORTS-FILE", 0,
        "Validate the exports file and print it out"},

        {"volfile-max-fetch-attempts", ARGP_VOLFILE_MAX_FETCH_ATTEMPTS, "0",
         OPTION_HIDDEN, "Maximum number of attempts to fetch the volfile"},
        {"aux-gfid-mount", ARGP_AUX_GFID_MOUNT_KEY, 0, 0,
         "Enable access to filesystem through gfid directly"},
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
        {"global-timer-wheel", ARGP_GLOBAL_TIMER_WHEEL, "BOOL",
         OPTION_ARG_OPTIONAL, "Instantiate process global timer-wheel"},

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
	 "Set auxiliary group list timeout to SECONDS for fuse translator "
	 "[default: 300]"},
        {"resolve-gids", ARGP_RESOLVE_GIDS_KEY, 0, 0,
         "Resolve all auxiliary groups in fuse translator (max 32 otherwise)"},
	{"background-qlen", ARGP_FUSE_BACKGROUND_QLEN_KEY, "N", 0,
	 "Set fuse module's background queue length to N "
	 "[default: 64]"},
	{"congestion-threshold", ARGP_FUSE_CONGESTION_THRESHOLD_KEY, "N", 0,
	 "Set fuse module's congestion threshold to N "
	 "[default: 48]"},
#ifdef GF_LINUX_HOST_OS
        {"oom-score-adj", ARGP_OOM_SCORE_ADJ_KEY, "INTEGER", 0,
         "Set oom_score_adj value for process"
         "[default: 0]"},
#endif
        {"client-pid", ARGP_CLIENT_PID_KEY, "PID", OPTION_HIDDEN,
         "client will authenticate itself with process id PID to server"},
        {"no-root-squash", ARGP_FUSE_NO_ROOT_SQUASH_KEY, "BOOL",
         OPTION_ARG_OPTIONAL, "disable/enable root squashing for the trusted "
         "client"},
        {"user-map-root", ARGP_USER_MAP_ROOT_KEY, "USER", OPTION_HIDDEN,
         "replace USER with root in messages"},
        {"dump-fuse", ARGP_DUMP_FUSE_KEY, "PATH", 0,
         "Dump fuse traffic to PATH"},
        {"volfile-check", ARGP_VOLFILE_CHECK_KEY, 0, 0,
         "Enable strict volume file checking"},
        {"no-mem-accounting", ARGP_MEM_ACCOUNTING_KEY, 0, OPTION_HIDDEN,
         "disable internal memory accounting"},
        {"fuse-mountopts", ARGP_FUSE_MOUNTOPTS_KEY, "OPTIONS", OPTION_HIDDEN,
         "Extra mount options to pass to FUSE"},
        {"use-readdirp", ARGP_FUSE_USE_READDIRP_KEY, "BOOL", OPTION_ARG_OPTIONAL,
         "Use readdirp mode in fuse kernel module"
         " [default: \"yes\"]"},
        {"secure-mgmt", ARGP_SECURE_MGMT_KEY, "BOOL", OPTION_ARG_OPTIONAL,
         "Override default for secure (SSL) management connections"},
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
                                gf_msg ("glusterfsd", GF_LOG_ERROR, errno,
                                        glusterfsd_msg_1);
                                goto err;
                        }
                } else {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, errno,
                                glusterfsd_msg_2);
                        goto err;
                }
        } else
                mount_point = gf_strdup (cmd_args->mount_point);

        ret = dict_set_dynstr (options, ZR_MOUNTPOINT_OPT, mount_point);
        if (ret < 0) {
                gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_3);
                goto err;
        }

        if (cmd_args->fuse_attribute_timeout >= 0) {
                ret = dict_set_double (options, ZR_ATTR_TIMEOUT_OPT,
                                       cmd_args->fuse_attribute_timeout);

                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, errno, 
                                glusterfsd_msg_4, ZR_ATTR_TIMEOUT_OPT);
                        goto err;
                }
        }

        if (cmd_args->fuse_entry_timeout >= 0) {
                ret = dict_set_double (options, ZR_ENTRY_TIMEOUT_OPT,
                                       cmd_args->fuse_entry_timeout);
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                ZR_ENTRY_TIMEOUT_OPT);
                        goto err;
                }
        }

        if (cmd_args->fuse_negative_timeout >= 0) {
                ret = dict_set_double (options, ZR_NEGATIVE_TIMEOUT_OPT,
                                       cmd_args->fuse_negative_timeout);
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                ZR_NEGATIVE_TIMEOUT_OPT);
                        goto err;
                }
        }

        if (cmd_args->client_pid_set) {
                ret = dict_set_int32 (options, "client-pid",
                                      cmd_args->client_pid);
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                "client-pid");
                        goto err;
                }
        }

        if (cmd_args->uid_map_root) {
                ret = dict_set_int32 (options, "uid-map-root",
                                      cmd_args->uid_map_root);
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                "uid-map-root");
                        goto err;
                }
        }

        if (cmd_args->volfile_check) {
                ret = dict_set_int32 (options, ZR_STRICT_VOLFILE_CHECK,
                                      cmd_args->volfile_check);
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                ZR_STRICT_VOLFILE_CHECK);
                        goto err;
                }
        }

        if (cmd_args->dump_fuse) {
                ret = dict_set_static_ptr (options, ZR_DUMP_FUSE,
                                           cmd_args->dump_fuse);
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                ZR_DUMP_FUSE);
                        goto err;
                }
        }

        if (cmd_args->acl) {
                ret = dict_set_static_ptr (options, "acl", "on");
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                "acl");
                        goto err;
                }
        }

        if (cmd_args->selinux) {
                ret = dict_set_static_ptr (options, "selinux", "on");
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                "selinux");
                        goto err;
                }
        }

        if (cmd_args->capability) {
                ret = dict_set_static_ptr (options, "capability", "on");
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                "capability");
                        goto err;
                }
        }

        if (cmd_args->aux_gfid_mount) {
                ret = dict_set_static_ptr (options, "virtual-gfid-access",
                                           "on");
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                "aux-gfid-mount");
                        goto err;
                }
        }

        if (cmd_args->enable_ino32) {
                ret = dict_set_static_ptr (options, "enable-ino32", "on");
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                "enable-ino32");
                        goto err;
                }
        }

        if (cmd_args->read_only) {
                ret = dict_set_static_ptr (options, "read-only", "on");
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                "read-only");
                        goto err;
                }
        }

	switch (cmd_args->fopen_keep_cache) {
	case GF_OPTION_ENABLE:
		ret = dict_set_static_ptr(options, "fopen-keep-cache",
			"on");
		if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
				"fopen-keep-cache");
			goto err;
		}
		break;
	case GF_OPTION_DISABLE:
		ret = dict_set_static_ptr(options, "fopen-keep-cache",
			"off");
		if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
				"fopen-keep-cache");
			goto err;
		}
		break;
        case GF_OPTION_DEFERRED: /* default */
        default:
                gf_msg_debug ("glusterfsd", 0, "fopen-keep-cache mode %d",
                              cmd_args->fopen_keep_cache);
                break;
	}

	if (cmd_args->gid_timeout_set) {
		ret = dict_set_int32(options, "gid-timeout",
			cmd_args->gid_timeout);
		if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                "gid-timeout");
			goto err;
		}
	}

        if (cmd_args->resolve_gids) {
                ret = dict_set_static_ptr (options, "resolve-gids", "on");
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                "resolve-gids");
                        goto err;
                }
        }

	if (cmd_args->background_qlen) {
		ret = dict_set_int32 (options, "background-qlen",
                                      cmd_args->background_qlen);
		if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                "background-qlen");
			goto err;
		}
	}
	if (cmd_args->congestion_threshold) {
		ret = dict_set_int32 (options, "congestion-threshold",
                                      cmd_args->congestion_threshold);
		if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                "congestion-threshold");
			goto err;
		}
	}

        switch (cmd_args->fuse_direct_io_mode) {
        case GF_OPTION_DISABLE: /* disable */
                ret = dict_set_static_ptr (options, ZR_DIRECT_IO_OPT,
                                           "disable");
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_5,
                                ZR_DIRECT_IO_OPT);
                        goto err;
                }
                break;
        case GF_OPTION_ENABLE: /* enable */
                ret = dict_set_static_ptr (options, ZR_DIRECT_IO_OPT,
                                           "enable");
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_6,
                                ZR_DIRECT_IO_OPT);
                        goto err;
                }
                break;
        case GF_OPTION_DEFERRED: /* default */
        default:
                gf_msg_debug ("glusterfsd", 0, "fuse direct io type %d",
                              cmd_args->fuse_direct_io_mode);
                break;
        }

        switch (cmd_args->no_root_squash) {
        case GF_OPTION_ENABLE: /* enable */
                ret = dict_set_static_ptr (options, "no-root-squash",
                                           "enable");
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_6,
                                "no-root-squash");
                        goto err;
                }
                break;
        case GF_OPTION_DISABLE: /* disable/default */
        default:
                ret = dict_set_static_ptr (options, "no-root-squash",
                                           "disable");
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_5,
                                "no-root-squash");
                        goto err;
                }
                gf_msg_debug ("glusterfsd", 0, "fuse no-root-squash mode %d",
                        cmd_args->no_root_squash);
                break;
        }

        if (!cmd_args->no_daemon_mode) {
                ret = dict_set_static_ptr (options, "sync-to-mount",
                                           "enable");
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                "sync-mtab");
                        goto err;
                }
        }

        if (cmd_args->use_readdirp) {
                ret = dict_set_str (options, "use-readdirp",
                                    cmd_args->use_readdirp);
                if (ret < 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                "use-readdirp");
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
                gf_msg_trace ("glusterfsd", 0,
                              "mount point not found, not a client process");
                return 0;
        }

        if (ctx->process_mode != GF_CLIENT_PROCESS) {
                gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_7);
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
                gf_msg ("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_8,
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
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_4,
                                ZR_FUSE_MOUNTOPTS);
                        goto err;
                }
        }

        ret = xlator_init (master);
        if (ret) {
                gf_msg_debug ("glusterfsd", 0,
                              "failed to initialize fuse translator");
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
                gf_msg ("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_9,
                        cmd_args->volfile);
                return NULL;
        }

        if ((specfp = fopen (cmd_args->volfile, "r")) == NULL) {
                gf_msg ("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_9,
                        cmd_args->volfile);
                return NULL;
        }

        gf_msg_debug ("glusterfsd", 0, "loading volume file %s",
                      cmd_args->volfile);

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
                gf_msg ("glusterfsd", GF_LOG_WARNING, 0, glusterfsd_msg_10,
                        arg);
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
                gf_msg ("", GF_LOG_WARNING, 0, glusterfsd_msg_10, arg);
                goto out;
        }

        option->volume = GF_CALLOC ((dot - arg) + 1, sizeof (char),
                                    gfd_mt_char);
        if (!option->volume)
                goto out;

        strncpy (option->volume, arg, (dot - arg));

        equals = strchr (arg, '=');
        if (!equals) {
                gf_msg ("", GF_LOG_WARNING, 0, glusterfsd_msg_10, arg);
                goto out;
        }

        option->key = GF_CALLOC ((equals - dot) + 1, sizeof (char),
                                 gfd_mt_char);
        if (!option->key)
                goto out;

        strncpy (option->key, dot + 1, (equals - dot - 1));

        if (!*(equals + 1)) {
                gf_msg ("", GF_LOG_WARNING, 0, glusterfsd_msg_10, arg);
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


#ifdef GF_LINUX_HOST_OS
static struct oom_api_info {
        char *oom_api_file;
        int32_t oom_min;
        int32_t oom_max;
} oom_api_info[] = {
        { "/proc/self/oom_score_adj", OOM_SCORE_ADJ_MIN, OOM_SCORE_ADJ_MAX },
        { "/proc/self/oom_adj",       OOM_DISABLE,       OOM_ADJUST_MAX },
        { NULL, 0, 0 }
};


static struct oom_api_info *
get_oom_api_info (void)
{
        struct oom_api_info *api = NULL;

        for (api = oom_api_info; api->oom_api_file; api++) {
                if (sys_access (api->oom_api_file, F_OK) != -1) {
                        return api;
                }
        }

        return NULL;
}
#endif

static error_t
parse_opts (int key, char *arg, struct argp_state *state)
{
        cmd_args_t          *cmd_args      = NULL;
        uint32_t             n             = 0;
#ifdef GF_LINUX_HOST_OS
        int32_t              k             = 0;
        struct oom_api_info *api           = NULL;
#endif
        double               d             = 0.0;
        gf_boolean_t         b             = _gf_false;
        char                *pwd           = NULL;
        char                 tmp_buf[2048] = {0,};
        char                *tmp_str       = NULL;
        char                *port_str      = NULL;
        struct passwd       *pw            = NULL;
        int                  ret           = 0;

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

        case ARGP_CAPABILITY_KEY:
                cmd_args->capability = 1;
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

        case ARGP_PRINT_NETGROUPS:
                cmd_args->print_netgroups = arg;
                break;

        case ARGP_PRINT_EXPORTS:
                cmd_args->print_exports = arg;
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

        case ARGP_FUSE_NO_ROOT_SQUASH_KEY:
                cmd_args->no_root_squash = _gf_true;
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

        case ARGP_GLOBAL_TIMER_WHEEL:
                cmd_args->global_timer_wheel = 1;
                break;

	case ARGP_GID_TIMEOUT_KEY:
		if (!gf_string2int(arg, &cmd_args->gid_timeout)) {
			cmd_args->gid_timeout_set = _gf_true;
			break;
		}

		argp_failure(state, -1, 0, "unknown group list timeout %s", arg);
		break;

        case ARGP_RESOLVE_GIDS_KEY:
                cmd_args->resolve_gids = 1;
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

#ifdef GF_LINUX_HOST_OS
        case ARGP_OOM_SCORE_ADJ_KEY:
                k = 0;

                api = get_oom_api_info();
                if (!api)
                        goto no_oom_api;

                if (gf_string2int (arg, &k) == 0 &&
                    k >= api->oom_min && k <= api->oom_max) {
                        cmd_args->oom_score_adj = gf_strdup (arg);
                        break;
                }

                argp_failure (state, -1, 0,
                              "unknown oom_score_adj value %s", arg);

no_oom_api:
                break;
#endif

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

        case ARGP_LOGGER:
                if (strcasecmp (arg, GF_LOGGER_GLUSTER_LOG) == 0)
                        cmd_args->logger = gf_logger_glusterlog;
                else if (strcasecmp (arg, GF_LOGGER_SYSLOG) == 0)
                        cmd_args->logger = gf_logger_syslog;
                else
                        argp_failure (state, -1, 0, "unknown logger %s", arg);

                break;

        case ARGP_LOG_FORMAT:
                if (strcasecmp (arg, GF_LOG_FORMAT_NO_MSG_ID) == 0)
                        cmd_args->log_format = gf_logformat_traditional;
                else if (strcasecmp (arg, GF_LOG_FORMAT_WITH_MSG_ID) == 0)
                        cmd_args->log_format = gf_logformat_withmsgid;
                else
                        argp_failure (state, -1, 0, "unknown log format %s",
                                      arg);

                break;

        case ARGP_LOG_BUF_SIZE:
                if (gf_string2uint32 (arg, &cmd_args->log_buf_size)) {
                        argp_failure (state, -1, 0,
                                      "unknown log buf size option %s", arg);
                } else if (cmd_args->log_buf_size > GF_LOG_LRU_BUFSIZE_MAX) {
                        argp_failure (state, -1, 0,
                                      "Invalid log buf size %s. "
                                      "Valid range: ["
                                      GF_LOG_LRU_BUFSIZE_MIN_STR","
                                      GF_LOG_LRU_BUFSIZE_MAX_STR"]", arg);
                }

                break;

        case ARGP_LOG_FLUSH_TIMEOUT:
                if (gf_string2uint32 (arg, &cmd_args->log_flush_timeout)) {
                        argp_failure (state, -1, 0,
                                "unknown log flush timeout option %s", arg);
                } else if ((cmd_args->log_flush_timeout <
                            GF_LOG_FLUSH_TIMEOUT_MIN) ||
                           (cmd_args->log_flush_timeout >
                            GF_LOG_FLUSH_TIMEOUT_MAX)) {
                            argp_failure (state, -1, 0,
                                          "Invalid log flush timeout %s. "
                                          "Valid range: ["
                                          GF_LOG_FLUSH_TIMEOUT_MIN_STR","
                                          GF_LOG_FLUSH_TIMEOUT_MAX_STR"]", arg);
                }

                break;

        case ARGP_SECURE_MGMT_KEY:
                if (!arg)
                        arg = "yes";

                if (gf_string2boolean (arg, &b) == 0) {
                        cmd_args->secure_mgmt = b ? 1 : 0;
                        break;
                }

                argp_failure (state, -1, 0,
                              "unknown secure-mgmt setting \"%s\"", arg);
                break;
	}

        return 0;
}

gf_boolean_t
should_call_fini (glusterfs_ctx_t *ctx, xlator_t *trav)
{
        /* There's nothing to call, so the other checks don't matter. */
        if (!trav->fini) {
                return _gf_false;
        }

        /* This preserves previous behavior in glusterd. */
        if (ctx->process_mode == GF_GLUSTERD_PROCESS) {
                return _gf_true;
        }

        /* This is the only one known to be safe in glusterfsd. */
        if (!strcmp(trav->type,"experimental/fdl")) {
                return _gf_true;
        }

        return _gf_false;
}

void
cleanup_and_exit (int signum)
{
        glusterfs_ctx_t *ctx      = NULL;
        xlator_t        *trav     = NULL;

        ctx = glusterfsd_ctx;

        if (!ctx)
                return;

        /* To take or not to take the mutex here and in the other
         * signal handler - gf_print_trace() - is the big question here.
         *
         * Taking mutex in signal handler would mean that if the process
         * receives a fatal signal while another thread is holding
         * ctx->log.log_buf_lock to perhaps log a message in _gf_msg_internal(),
         * the offending thread hangs on the mutex lock forever without letting
         * the process exit.
         *
         * On the other hand. not taking the mutex in signal handler would cause
         * it to modify the lru_list of buffered log messages in a racy manner,
         * corrupt the list and potentially give rise to an unending
         * cascade of SIGSEGVs and other re-entrancy issues.
         */

        gf_log_disable_suppression_before_exit (ctx);

        gf_msg_callingfn ("", GF_LOG_WARNING, 0, glusterfsd_msg_32, signum);

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

#if 0
        /* TODO: Properly do cleanup_and_exit(), with synchronization */
        if (ctx->mgmt) {
                /* cleanup the saved-frames before last unref */
                rpc_clnt_connection_cleanup (&ctx->mgmt->conn);
                rpc_clnt_unref (ctx->mgmt);
        }
#endif

        /* call fini() of each xlator */

        /*call fini for glusterd xlator */
        /* TODO : Invoke fini for rest of the xlators */
        trav = NULL;
        if (ctx->active)
                trav = ctx->active->top;
        while (trav) {
                if (should_call_fini(ctx,trav)) {
                        THIS = trav;
                        trav->fini (trav);
                }
                trav = trav->next;
        }

        exit(signum);
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
                gf_msg ("glusterfsd", GF_LOG_INFO, 0, glusterfsd_msg_11);
                ret = glusterfs_volfile_fetch (ctx);
        } else {
                gf_msg_debug ("glusterfsd", 0,
                              "Not reloading volume specification file"
                              " on SIGHUP");
        }

        /* Also, SIGHUP should do logrotate */
        gf_log_logrotate (1);

        if (ret < 0)
                gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_12);

        return;
}

void
emancipate (glusterfs_ctx_t *ctx, int ret)
{
        /* break free from the parent */
        if (ctx->daemon_pipe[1] != -1) {
                sys_write (ctx->daemon_pipe[1], (void *) &ret, sizeof (ret));
                sys_close (ctx->daemon_pipe[1]);
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

        ret = xlator_mem_acct_init (THIS, gfd_mt_end);
        if (ret != 0) {
                gf_msg(THIS->name, GF_LOG_CRITICAL, 0, glusterfsd_msg_34);
                return ret;
        }

        /* reset ret to -1 so that we don't need to explicitly
         * set it in all error paths before "goto err"
         */
        ret = -1;

        ctx->process_uuid = generate_glusterfs_ctx_id ();
        if (!ctx->process_uuid) {
                gf_msg ("", GF_LOG_CRITICAL, 0, glusterfsd_msg_13);
                goto out;
        }

        ctx->page_size  = 128 * GF_UNIT_KB;

        ctx->iobuf_pool = iobuf_pool_new ();
        if (!ctx->iobuf_pool) {
                gf_msg ("", GF_LOG_CRITICAL, 0, glusterfsd_msg_14, "iobuf");
                goto out;
        }

        ctx->event_pool = event_pool_new (DEFAULT_EVENT_POOL_SIZE,
                                          STARTING_EVENT_THREADS);
        if (!ctx->event_pool) {
                gf_msg ("", GF_LOG_CRITICAL, 0, glusterfsd_msg_14, "event");
                goto out;
        }

        ctx->pool = GF_CALLOC (1, sizeof (call_pool_t), gfd_mt_call_pool_t);
        if (!ctx->pool) {
                gf_msg ("", GF_LOG_CRITICAL, 0, glusterfsd_msg_14, "call");
                goto out;
        }

        INIT_LIST_HEAD (&ctx->pool->all_frames);
        LOCK_INIT (&ctx->pool->lock);

        /* frame_mem_pool size 112 * 4k */
        ctx->pool->frame_mem_pool = mem_pool_new (call_frame_t, 4096);
        if (!ctx->pool->frame_mem_pool) {
                gf_msg ("", GF_LOG_CRITICAL, 0, glusterfsd_msg_14, "frame");
                goto out;
        }
        /* stack_mem_pool size 256 * 1024 */
        ctx->pool->stack_mem_pool = mem_pool_new (call_stack_t, 1024);
        if (!ctx->pool->stack_mem_pool) {
                gf_msg ("", GF_LOG_CRITICAL, 0, glusterfsd_msg_14, "stack");
                goto out;
        }

        ctx->stub_mem_pool = mem_pool_new (call_stub_t, 1024);
        if (!ctx->stub_mem_pool) {
                gf_msg ("", GF_LOG_CRITICAL, 0, glusterfsd_msg_14, "stub");
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

        ctx->logbuf_pool = mem_pool_new (log_buf_t,
                                         GF_MEMPOOL_COUNT_OF_LRU_BUF_T);
        if (!ctx->logbuf_pool)
                goto out;

        pthread_mutex_init (&ctx->notify_lock, NULL);
        pthread_cond_init (&ctx->notify_cond, NULL);

        ctx->clienttable = gf_clienttable_alloc();
        if (!ctx->clienttable)
                goto out;

        cmd_args = &ctx->cmd_args;

        /* parsing command line arguments */
        cmd_args->log_level = DEFAULT_LOG_LEVEL;
        cmd_args->logger    = gf_logger_glusterlog;
        cmd_args->log_format = gf_logformat_withmsgid;
        cmd_args->log_buf_size = GF_LOG_LRU_BUFSIZE_DEFAULT;
        cmd_args->log_flush_timeout = GF_LOG_FLUSH_TIMEOUT_DEFAULT;

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

        if (ctx->mem_acct_enable)
                cmd_args->mem_acct = 1;

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
                mem_pool_destroy (ctx->logbuf_pool);
        }

        return ret;
}

static int
logging_init (glusterfs_ctx_t *ctx, const char *progpath)
{
        cmd_args_t *cmd_args = NULL;
        int         ret = 0;

        cmd_args = &ctx->cmd_args;

        if (cmd_args->log_file == NULL) {
                ret = gf_set_log_file_path (cmd_args, ctx);
                if (ret == -1) {
                        fprintf (stderr, "ERROR: failed to set the log file "
                                         "path\n");
                        return -1;
                }
        }

        if (cmd_args->log_ident == NULL) {
                ret = gf_set_log_ident (cmd_args);
                if (ret == -1) {
                        fprintf (stderr, "ERROR: failed to set the log "
                                         "identity\n");
                        return -1;
                }
        }

        /* finish log set parameters before init */
        gf_log_set_loglevel (cmd_args->log_level);

        gf_log_set_logger (cmd_args->logger);

        gf_log_set_logformat (cmd_args->log_format);

        gf_log_set_log_buf_size (cmd_args->log_buf_size);

        gf_log_set_log_flush_timeout (cmd_args->log_flush_timeout);

        if (gf_log_init (ctx, cmd_args->log_file, cmd_args->log_ident) == -1) {
                fprintf (stderr, "ERROR: failed to open logfile %s\n",
                         cmd_args->log_file);
                return -1;
        }

        /* At this point, all the logging related parameters are initialised
         * except for the log flush timer, which will be injected post fork(2)
         * in daemonize() . During this time, any log message that is logged
         * will be kept buffered. And if the list that holds these messages
         * overflows, then the same lru policy is used to drive out the least
         * recently used message and displace it with the message just logged.
         */

        return 0;
}

void
gf_check_and_set_mem_acct (int argc, char *argv[])
{
        int i = 0;

        for (i = 0; i < argc; i++) {
                if (strcmp (argv[i], "--no-mem-accounting") == 0) {
			gf_global_mem_acct_enable_set (0);
                        break;
                }
        }
}

/**
 * print_exports_file - Print out & verify the syntax
 *                      of the exports file specified
 *                      in the parameter.
 *
 * @exports_file : Path of the exports file to print & verify
 *
 * @return : success: 0 when successfully parsed
 *           failure: 1 when failed to parse one or more lines
 *                   -1 when other critical errors (dlopen () etc)
 * Critical errors are treated differently than parse errors. Critical
 * errors terminate the program immediately here and print out different
 * error messages. Hence there are different return values.
 */
int
print_exports_file (const char *exports_file)
{
        void                   *libhandle = NULL;
        char                   *libpathfull = NULL;
        struct exports_file    *file = NULL;
        int                     ret = 0;

        int  (*exp_file_parse)(const char *filepath,
                               struct exports_file **expfile,
                               struct mount3_state *ms) = NULL;
        void (*exp_file_print)(const struct exports_file *file) = NULL;
        void (*exp_file_deinit)(struct exports_file *ptr) = NULL;

        /* XLATORDIR passed through a -D flag to GCC */
        ret = gf_asprintf (&libpathfull, "%s/%s/server.so", XLATORDIR,
                           "nfs");
        if (ret < 0) {
                gf_log ("glusterfs", GF_LOG_CRITICAL, "asprintf () failed.");
                ret = -1;
                goto out;
        }

        /* Load up the library */
        libhandle = dlopen (libpathfull, RTLD_NOW);
        if (!libhandle) {
                gf_log ("glusterfs", GF_LOG_CRITICAL,
                        "Error loading NFS server library : "
                        "%s\n", dlerror ());
                ret = -1;
                goto out;
        }

        /* Load up the function */
        exp_file_parse = dlsym (libhandle, "exp_file_parse");
        if (!exp_file_parse) {
                gf_log ("glusterfs", GF_LOG_CRITICAL,
                        "Error finding function exp_file_parse "
                        "in symbol.");
                ret = -1;
                goto out;
        }

        /* Parse the file */
        ret = exp_file_parse (exports_file, &file, NULL);
        if (ret < 0) {
                ret = 1;        /* This means we failed to parse */
                goto out;
        }

        /* Load up the function */
        exp_file_print = dlsym (libhandle, "exp_file_print");
        if (!exp_file_print) {
                gf_log ("glusterfs", GF_LOG_CRITICAL,
                        "Error finding function exp_file_print in symbol.");
                ret = -1;
                goto out;
        }

        /* Print it out to screen */
        exp_file_print (file);

        /* Load up the function */
        exp_file_deinit = dlsym (libhandle, "exp_file_deinit");
        if (!exp_file_deinit) {
                gf_log ("glusterfs", GF_LOG_CRITICAL,
                        "Error finding function exp_file_deinit in lib.");
                ret = -1;
                goto out;
        }

        /* Free the file */
        exp_file_deinit (file);

out:
        if (libhandle)
                dlclose(libhandle);
        GF_FREE (libpathfull);
        return ret;
}


/**
 * print_netgroups_file - Print out & verify the syntax
 *                        of the netgroups file specified
 *                        in the parameter.
 *
 * @netgroups_file : Path of the netgroups file to print & verify
 * @return : success: 0 when successfully parsed
 *           failure: 1 when failed to parse one more more lines
 *                   -1 when other critical errors (dlopen () etc)
 *
 * We have multiple returns here because for critical errors, we abort
 * operations immediately and exit. For example, if we can't load the
 * NFS server library, then we have a real bad problem so we don't continue.
 * Or if we cannot allocate anymore memory, we don't want to continue. Also,
 * we want to print out a different error messages based on the ret value.
 */
int
print_netgroups_file (const char *netgroups_file)
{
        void                   *libhandle = NULL;
        char                   *libpathfull = NULL;
        struct netgroups_file  *file = NULL;
        int                     ret = 0;

        struct netgroups_file  *(*ng_file_parse)(const char *file_path) = NULL;
        void         (*ng_file_print)(const struct netgroups_file *file) = NULL;
        void         (*ng_file_deinit)(struct netgroups_file *ptr) = NULL;

        /* XLATORDIR passed through a -D flag to GCC */
        ret = gf_asprintf (&libpathfull, "%s/%s/server.so", XLATORDIR,
                        "nfs");
        if (ret < 0) {
                gf_log ("glusterfs", GF_LOG_CRITICAL, "asprintf () failed.");
                ret = -1;
                goto out;
        }
        /* Load up the library */
        libhandle = dlopen (libpathfull, RTLD_NOW);
        if (!libhandle) {
                gf_log ("glusterfs", GF_LOG_CRITICAL,
                        "Error loading NFS server library : %s\n", dlerror ());
                ret = -1;
                goto out;
        }

        /* Load up the function */
        ng_file_parse = dlsym (libhandle, "ng_file_parse");
        if (!ng_file_parse) {
                gf_log ("glusterfs", GF_LOG_CRITICAL,
                        "Error finding function ng_file_parse in symbol.");
                ret = -1;
                goto out;
        }

        /* Parse the file */
        file = ng_file_parse (netgroups_file);
        if (!file) {
                ret = 1;        /* This means we failed to parse */
                goto out;
        }

        /* Load up the function */
        ng_file_print = dlsym (libhandle, "ng_file_print");
        if (!ng_file_print) {
                gf_log ("glusterfs", GF_LOG_CRITICAL,
                        "Error finding function ng_file_print in symbol.");
                ret = -1;
                goto out;
        }

        /* Print it out to screen */
        ng_file_print (file);

        /* Load up the function */
        ng_file_deinit = dlsym (libhandle, "ng_file_deinit");
        if (!ng_file_deinit) {
                gf_log ("glusterfs", GF_LOG_CRITICAL,
                        "Error finding function ng_file_deinit in lib.");
                ret = -1;
                goto out;
        }

        /* Free the file */
        ng_file_deinit (file);

out:
        if (libhandle)
                dlclose(libhandle);
        GF_FREE (libpathfull);
        return ret;
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

        /* Do this before argp_parse so it can be overridden. */
        if (sys_access (SECURE_ACCESS_FILE, F_OK) == 0) {
                cmd_args->secure_mgmt = 1;
        }

        argp_parse (&argp, argc, argv, ARGP_IN_ORDER, NULL, cmd_args);
        if (cmd_args->print_netgroups) {
                /* When this option is set we don't want to do anything else
                 * except for printing & verifying the netgroups file.
                 */
                ret = 0;
                goto out;
        }

        if (cmd_args->print_exports) {
                /* When this option is set we don't want to do anything else
                 * except for printing & verifying the exports file.
                  */
                ret = 0;
                goto out;
        }


        ctx->secure_mgmt = cmd_args->secure_mgmt;

        if (ENABLE_DEBUG_MODE == cmd_args->debug_mode) {
                cmd_args->log_level = GF_LOG_DEBUG;
                cmd_args->log_file = gf_strdup ("/dev/stderr");
                cmd_args->no_daemon_mode = ENABLE_NO_DAEMON_MODE;
        }

        process_mode = gf_get_process_mode (argv[0]);
        ctx->process_mode = process_mode;

        /* Make sure after the parsing cli, if '--volfile-server' option is
           given, then '--volfile-id' is mandatory */
        if (cmd_args->volfile_server && !cmd_args->volfile_id) {
                gf_msg ("glusterfs", GF_LOG_CRITICAL, 0, glusterfsd_msg_15);
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
                ret = sys_stat (cmd_args->volfile, &stbuf);
                if (ret) {
                        gf_msg ("glusterfs", GF_LOG_CRITICAL, errno,
                                glusterfsd_msg_16);
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
                gf_msg ("glusterfs", GF_LOG_WARNING, 0, glusterfsd_msg_33);
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
                gf_msg ("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_17,
                        cmd_args->pid_file);
                goto out;
        }

        ctx->pidfp = pidfp;

        ret = 0;
out:

        return ret;
}


int
glusterfs_pidfile_cleanup (glusterfs_ctx_t *ctx)
{
        cmd_args_t      *cmd_args = NULL;

        cmd_args = &ctx->cmd_args;

        if (!ctx->pidfp)
                return 0;

        gf_msg_trace ("glusterfsd", 0, "pidfile %s cleanup",
                      cmd_args->pid_file);

        if (ctx->cmd_args.pid_file) {
                sys_unlink (ctx->cmd_args.pid_file);
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
                gf_msg ("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_18,
                        cmd_args->pid_file);
                return ret;
        }

        ret = sys_ftruncate (fileno (pidfp), 0);
        if (ret) {
                gf_msg ("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_20,
                        cmd_args->pid_file);
                return ret;
        }

        ret = fprintf (pidfp, "%d\n", getpid ());
        if (ret <= 0) {
                gf_msg ("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_21,
                        cmd_args->pid_file);
                return ret;
        }

        ret = fflush (pidfp);
        if (ret) {
                gf_msg ("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_21,
                        cmd_args->pid_file);
                return ret;
        }

        gf_msg_debug ("glusterfsd", 0, "pidfile %s updated with pid %d",
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
                gf_msg ("glusterfsd", GF_LOG_WARNING, errno, glusterfsd_msg_22);
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
                gf_msg ("glusterfsd", GF_LOG_WARNING, errno, glusterfsd_msg_23);
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
        int            err = 1;

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
                        sys_close (ctx->daemon_pipe[0]);
                        sys_close (ctx->daemon_pipe[1]);
                }

                gf_msg ("daemonize", GF_LOG_ERROR, errno, glusterfsd_msg_24);
                goto out;
        case 0:
                /* child */
                /* close read */
                sys_close (ctx->daemon_pipe[0]);
                break;
        default:
                /* parent */
                /* close write */
                sys_close (ctx->daemon_pipe[1]);

                if (ctx->mnt_pid > 0) {
                        ret = waitpid (ctx->mnt_pid, &cstatus, 0);
                        if (!(ret == ctx->mnt_pid)) {
                                if (WIFEXITED(cstatus)) {
                                        err = WEXITSTATUS(cstatus);
                                } else {
                                        err = cstatus;
                                }
                                gf_msg ("daemonize", GF_LOG_ERROR, 0,
                                        glusterfsd_msg_25);
                                exit (err);
                        }
                }
                sys_read (ctx->daemon_pipe[0], (void *)&err, sizeof (err));
                _exit (err);
        }

postfork:
        ret = glusterfs_pidfile_update (ctx);
        if (ret)
                goto out;

        ret = gf_log_inject_timer_event (ctx);

        glusterfs_signals_setup (ctx);
out:
        return ret;
}


#ifdef GF_LINUX_HOST_OS
static int
set_oom_score_adj (glusterfs_ctx_t *ctx)
{
        int                  ret           = -1;
        cmd_args_t          *cmd_args      =  NULL;
        int                  fd            = -1;
        size_t               oom_score_len =  0;
        struct oom_api_info *api           =  NULL;

        cmd_args = &ctx->cmd_args;

        if (!cmd_args->oom_score_adj)
                goto success;

        api = get_oom_api_info();
        if (!api)
                goto out;

        fd = open (api->oom_api_file, O_WRONLY);
        if (fd < 0)
                goto out;

        oom_score_len = strlen (cmd_args->oom_score_adj);
        if (sys_write (fd,
                  cmd_args->oom_score_adj, oom_score_len) != oom_score_len) {
                sys_close (fd);
                goto out;
        }

        if (sys_close (fd) < 0)
                goto out;

success:
        ret = 0;

out:
        return ret;
}
#endif


int
glusterfs_process_volfp (glusterfs_ctx_t *ctx, FILE *fp)
{
        glusterfs_graph_t  *graph = NULL;
        int                 ret = -1;
        xlator_t           *trav = NULL;
        int                 err = 0;

        graph = glusterfs_graph_construct (fp);
        if (!graph) {
                gf_msg ("", GF_LOG_ERROR, 0, glusterfsd_msg_26);
                goto out;
        }

        for (trav = graph->first; trav; trav = trav->next) {
                if (strcmp (trav->type, "mount/fuse") == 0) {
                        gf_msg ("glusterfsd", GF_LOG_ERROR, 0,
                                glusterfsd_msg_27);
                        goto out;
                }
        }

        xlator_t *xl = graph->first;
        if (strcmp (xl->type, "protocol/server") == 0) {
                (void) copy_opts_to_child (xl, FIRST_CHILD (xl), "*auth*");
        }

        ret = glusterfs_graph_prepare (graph, ctx, ctx->cmd_args.volume_name);
        if (ret) {
                goto out;
        }

        ret = glusterfs_graph_activate (graph, ctx);

        if (ret) {
                goto out;
        }

        gf_log_dump_graph (fp, graph);

        ret = 0;
out:
        if (fp)
                fclose (fp);

        if (ret && !ctx->active) {
                glusterfs_graph_destroy (graph);
                /* there is some error in setting up the first graph itself */
                err = -ret;
                sys_write (ctx->daemon_pipe[1], (void *) &err, sizeof (err));
                cleanup_and_exit (err);
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
                gf_msg ("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_28);
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
        cmd_args_t       *cmd = NULL;

	gf_check_and_set_mem_acct (argc, argv);

	ctx = glusterfs_ctx_new ();
        if (!ctx) {
                gf_msg ("glusterfs", GF_LOG_CRITICAL, 0, glusterfsd_msg_29);
                return ENOMEM;
        }
	glusterfsd_ctx = ctx;

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
        cmd = &ctx->cmd_args;
        if (cmd->print_netgroups) {
                /* If this option is set we want to print & verify the file,
                 * set the return value (exit code in this case) and exit.
                 */
                ret =  print_netgroups_file (cmd->print_netgroups);
                goto out;
        }

        if (cmd->print_exports) {
                /* If this option is set we want to print & verify the file,
                 * set the return value (exit code in this case)
                 * and exit.
                 */
                ret = print_exports_file (cmd->print_exports);
                goto out;
        }

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
                        strncat (cmdlinestr, argv[i],
                                 (sizeof (cmdlinestr) - 1));
                }
                gf_msg (argv[0], GF_LOG_INFO, 0, glusterfsd_msg_30,
                        argv[0], PACKAGE_VERSION, cmdlinestr);

		ctx->cmdlinestr = gf_strdup (cmdlinestr);
        }

        gf_proc_dump_init();

        ret = create_fuse_mount (ctx);
        if (ret)
                goto out;

        ret = daemonize (ctx);
        if (ret)
                goto out;

        /*
         * If we do this before daemonize, the pool-sweeper thread dies with
         * the parent, but we want to do it as soon as possible after that in
         * case something else depends on pool allocations.
         */
        mem_pools_init ();

#ifdef GF_LINUX_HOST_OS
        ret = set_oom_score_adj (ctx);
        if (ret)
                goto out;
#endif

	ctx->env = syncenv_new (0, 0, 0);
        if (!ctx->env) {
                gf_msg ("", GF_LOG_ERROR, 0, glusterfsd_msg_31);
                goto out;
        }

        /* do this _after_ daemonize() */
        if (cmd->global_timer_wheel) {
                ret = glusterfs_global_timer_wheel_init (ctx);
                if (ret)
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
