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
#define OOM_SCORE_ADJ_MAX 1000
#define OOM_DISABLE (-17)
#define OOM_ADJUST_MAX 15
#endif
#endif

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include <glusterfs/xlator.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/compat.h>
#include <glusterfs/logging.h>
#include "glusterfsd-messages.h"
#include <glusterfs/dict.h>
#include <glusterfs/list.h>
#include <glusterfs/timer.h>
#include "glusterfsd.h"
#include <glusterfs/revision.h>
#include <glusterfs/common-utils.h>
#include <glusterfs/gf-event.h>
#include <glusterfs/statedump.h>
#include <glusterfs/latency.h>
#include "glusterfsd-mem-types.h"
#include <glusterfs/syscall.h>
#include <glusterfs/call-stub.h>
#include <fnmatch.h>
#include "rpc-clnt.h"
#include <glusterfs/syncop.h>
#include <glusterfs/client_t.h>
#include "netgroups.h"
#include "exports.h"
#include <glusterfs/monitoring.h>

#include <glusterfs/daemon.h>

/* using argp for command line parsing */
static char gf_doc[] = "";
static char argp_doc[] =
    "--volfile-server=SERVER [MOUNT-POINT]\n"
    "--volfile=VOLFILE [MOUNT-POINT]";
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

static error_t
parse_opts(int32_t key, char *arg, struct argp_state *_state);

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
     "File to use for logging [default: " DEFAULT_LOG_FILE_DIRECTORY
     "/" PACKAGE_NAME ".log"
     "]"},
    {"logger", ARGP_LOGGER, "LOGGER", 0,
     "Set which logging sub-system to "
     "log to, valid options are: gluster-log and syslog, "
     "[default: \"gluster-log\"]"},
    {"log-format", ARGP_LOG_FORMAT, "LOG-FORMAT", 0,
     "Set log format, valid"
     " options are: no-msg-id and with-msg-id, [default: \"with-msg-id\"]"},
    {"log-buf-size", ARGP_LOG_BUF_SIZE, "LOG-BUF-SIZE", 0,
     "Set logging "
     "buffer size, [default: 5]"},
    {"log-flush-timeout", ARGP_LOG_FLUSH_TIMEOUT, "LOG-FLUSH-TIMEOUT", 0,
     "Set log flush timeout, [default: 2 minutes]"},

    {0, 0, 0, 0, "Advanced Options:"},
    {"volfile-server-port", ARGP_VOLFILE_SERVER_PORT_KEY, "PORT", 0,
     "Listening port number of volfile server"},
    {"volfile-server-transport", ARGP_VOLFILE_SERVER_TRANSPORT_KEY, "TRANSPORT",
     0, "Transport type to get volfile from server [default: socket]"},
    {"volfile-id", ARGP_VOLFILE_ID_KEY, "KEY", 0,
     "'key' of the volfile to be fetched from server"},
    {"pid-file", ARGP_PID_FILE_KEY, "PIDFILE", 0, "File to use as pid file"},
    {"socket-file", ARGP_SOCK_FILE_KEY, "SOCKFILE", 0,
     "File to use as unix-socket"},
    {"no-daemon", ARGP_NO_DAEMON_KEY, 0, 0, "Run in foreground"},
    {"run-id", ARGP_RUN_ID_KEY, "RUN-ID", OPTION_HIDDEN,
     "Run ID for the process, used by scripts to keep track of process "
     "they started, defaults to none"},
    {"debug", ARGP_DEBUG_KEY, 0, 0,
     "Run in debug mode.  This option sets --no-daemon, --log-level "
     "to DEBUG and --log-file to console"},
    {"volume-name", ARGP_VOLUME_NAME_KEY, "XLATOR-NAME", 0,
     "Translator name to be used for MOUNT-POINT [default: top most volume "
     "definition in VOLFILE]"},
    {"xlator-option", ARGP_XLATOR_OPTION_KEY, "XLATOR-NAME.OPTION=VALUE", 0,
     "Add/override an option for a translator in volume file with specified"
     " value"},
    {"read-only", ARGP_READ_ONLY_KEY, 0, 0,
     "Mount the filesystem in 'read-only' mode"},
    {"acl", ARGP_ACL_KEY, 0, 0, "Mount the filesystem with POSIX ACL support"},
    {"selinux", ARGP_SELINUX_KEY, 0, 0,
     "Enable SELinux label (extended attributes) support on inodes"},
    {"capability", ARGP_CAPABILITY_KEY, 0, 0,
     "Enable Capability (extended attributes) support on inodes"},
    {"subdir-mount", ARGP_SUBDIR_MOUNT_KEY, "SUBDIR-PATH", 0,
     "Mount subdirectory given [default: NULL]"},

    {"print-netgroups", ARGP_PRINT_NETGROUPS, "NETGROUP-FILE", 0,
     "Validate the netgroups file and print it out"},
    {"print-exports", ARGP_PRINT_EXPORTS, "EXPORTS-FILE", 0,
     "Validate the exports file and print it out"},
    {"print-xlatordir", ARGP_PRINT_XLATORDIR_KEY, 0, OPTION_ARG_OPTIONAL,
     "Print xlator directory path"},
    {"print-statedumpdir", ARGP_PRINT_STATEDUMPDIR_KEY, 0, OPTION_ARG_OPTIONAL,
     "Print directory path in which statedumps shall be generated"},
    {"print-logdir", ARGP_PRINT_LOGDIR_KEY, 0, OPTION_ARG_OPTIONAL,
     "Print path of default log directory"},
    {"print-libexecdir", ARGP_PRINT_LIBEXECDIR_KEY, 0, OPTION_ARG_OPTIONAL,
     "Print path of default libexec directory"},

    {"volfile-max-fetch-attempts", ARGP_VOLFILE_MAX_FETCH_ATTEMPTS, "0",
     OPTION_HIDDEN, "Maximum number of attempts to fetch the volfile"},
    {"aux-gfid-mount", ARGP_AUX_GFID_MOUNT_KEY, 0, 0,
     "Enable access to filesystem through gfid directly"},
    {"enable-ino32", ARGP_INODE32_KEY, "BOOL", OPTION_ARG_OPTIONAL,
     "Use 32-bit inodes when mounting to workaround broken applications"
     "that don't support 64-bit inodes"},
    {"worm", ARGP_WORM_KEY, 0, 0, "Mount the filesystem in 'worm' mode"},
    {"mac-compat", ARGP_MAC_COMPAT_KEY, "BOOL", OPTION_ARG_OPTIONAL,
     "Provide stubs for attributes needed for seamless operation on Macs "
#ifdef GF_DARWIN_HOST_OS
     "[default: \"on\" on client side, else \"off\"]"
#else
     "[default: \"off\"]"
#endif
    },
    {"brick-name", ARGP_BRICK_NAME_KEY, "BRICK-NAME", OPTION_HIDDEN,
     "Brick name to be registered with Gluster portmapper"},
    {"brick-port", ARGP_BRICK_PORT_KEY, "BRICK-PORT", OPTION_HIDDEN,
     "Brick Port to be registered with Gluster portmapper"},
    {"fopen-keep-cache", ARGP_FOPEN_KEEP_CACHE_KEY, "BOOL", OPTION_ARG_OPTIONAL,
     "Do not purge the cache on file open [default: false]"},
    {"global-timer-wheel", ARGP_GLOBAL_TIMER_WHEEL, "BOOL", OPTION_ARG_OPTIONAL,
     "Instantiate process global timer-wheel"},
    {"thin-client", ARGP_THIN_CLIENT_KEY, 0, 0,
     "Enables thin mount and connects via gfproxyd daemon"},
    {"global-threading", ARGP_GLOBAL_THREADING_KEY, "BOOL", OPTION_ARG_OPTIONAL,
     "Use the global thread pool instead of io-threads"},
    {0, 0, 0, 0, "Fuse options:"},
    {"direct-io-mode", ARGP_DIRECT_IO_MODE_KEY, "BOOL|auto",
     OPTION_ARG_OPTIONAL, "Specify direct I/O strategy [default: \"auto\"]"},
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
    {"lru-limit", ARGP_FUSE_LRU_LIMIT_KEY, "N", 0,
     "Set fuse module's limit for number of inodes kept in LRU list to N "
     "[default: 65536]"},
    {"invalidate-limit", ARGP_FUSE_INVALIDATE_LIMIT_KEY, "N", 0,
     "Suspend inode invalidations implied by 'lru-limit' if the number of "
     "outstanding invalidations reaches N"},
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
     OPTION_ARG_OPTIONAL,
     "disable/enable root squashing for the trusted "
     "client"},
    {"user-map-root", ARGP_USER_MAP_ROOT_KEY, "USER", OPTION_HIDDEN,
     "replace USER with root in messages"},
    {"dump-fuse", ARGP_DUMP_FUSE_KEY, "PATH", 0, "Dump fuse traffic to PATH"},
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
    {"localtime-logging", ARGP_LOCALTIME_LOGGING_KEY, 0, 0,
     "Enable localtime logging"},
    {"process-name", ARGP_PROCESS_NAME_KEY, "PROCESS-NAME", OPTION_HIDDEN,
     "option to specify the process type"},
    {"event-history", ARGP_FUSE_EVENT_HISTORY_KEY, "BOOL", OPTION_ARG_OPTIONAL,
     "disable/enable fuse event-history"},
    {"reader-thread-count", ARGP_READER_THREAD_COUNT_KEY, "INTEGER",
     OPTION_ARG_OPTIONAL, "set fuse reader thread count"},
    {"kernel-writeback-cache", ARGP_KERNEL_WRITEBACK_CACHE_KEY, "BOOL",
     OPTION_ARG_OPTIONAL, "enable fuse in-kernel writeback cache"},
    {"attr-times-granularity", ARGP_ATTR_TIMES_GRANULARITY_KEY, "NS",
     OPTION_ARG_OPTIONAL,
     "declare supported granularity of file attribute"
     " times in nanoseconds"},
    {"fuse-flush-handle-interrupt", ARGP_FUSE_FLUSH_HANDLE_INTERRUPT_KEY,
     "BOOL", OPTION_ARG_OPTIONAL | OPTION_HIDDEN,
     "handle interrupt in fuse FLUSH handler"},
    {"auto-invalidation", ARGP_FUSE_AUTO_INVAL_KEY, "BOOL", OPTION_ARG_OPTIONAL,
     "controls whether fuse-kernel can auto-invalidate "
     "attribute, dentry and page-cache. "
     "Disable this only if same files/directories are not accessed across "
     "two different mounts concurrently [default: \"on\"]"},
    {"fuse-dev-eperm-ratelimit-ns", ARGP_FUSE_DEV_EPERM_RATELIMIT_NS_KEY,
     "OPTIONS", OPTION_HIDDEN,
     "rate limit reading from fuse device upon EPERM failure"},
    {"brick-mux", ARGP_BRICK_MUX_KEY, 0, 0, "Enable brick mux. "},
    {0, 0, 0, 0, "Miscellaneous Options:"},
    {
        0,
    }};

static struct argp argp = {gf_options, parse_opts, argp_doc, gf_doc};

int
glusterfs_pidfile_cleanup(glusterfs_ctx_t *ctx);
int
glusterfs_volumes_init(glusterfs_ctx_t *ctx);
int
glusterfs_mgmt_init(glusterfs_ctx_t *ctx);
int
glusterfs_listener_init(glusterfs_ctx_t *ctx);

#define DICT_SET_VAL(method, dict, key, val, msgid)                            \
    if (method(dict, key, val)) {                                              \
        gf_smsg("glusterfsd", GF_LOG_ERROR, 0, msgid, "key=%s", key);          \
        goto err;                                                              \
    }

static int
set_fuse_mount_options(glusterfs_ctx_t *ctx, dict_t *options)
{
    int ret = 0;
    cmd_args_t *cmd_args = NULL;
    char *mount_point = NULL;
    char cwd[PATH_MAX] = {
        0,
    };

    cmd_args = &ctx->cmd_args;

    /* Check if mount-point is absolute path,
     * if not convert to absolute path by concatenating with CWD
     */
    if (cmd_args->mount_point[0] != '/') {
        if (getcwd(cwd, PATH_MAX) != NULL) {
            ret = gf_asprintf(&mount_point, "%s/%s", cwd,
                              cmd_args->mount_point);
            if (ret == -1) {
                gf_smsg("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_1,
                        "gf_asprintf failed", NULL);
                goto err;
            }
        } else {
            gf_smsg("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_2,
                    "getcwd failed", NULL);
            goto err;
        }

    } else {
        mount_point = gf_strdup(cmd_args->mount_point);
    }
    DICT_SET_VAL(dict_set_dynstr_sizen, options, ZR_MOUNTPOINT_OPT, mount_point,
                 glusterfsd_msg_3);

    if (cmd_args->fuse_attribute_timeout >= 0) {
        DICT_SET_VAL(dict_set_double, options, ZR_ATTR_TIMEOUT_OPT,
                     cmd_args->fuse_attribute_timeout, glusterfsd_msg_3);
    }

    if (cmd_args->fuse_entry_timeout >= 0) {
        DICT_SET_VAL(dict_set_double, options, ZR_ENTRY_TIMEOUT_OPT,
                     cmd_args->fuse_entry_timeout, glusterfsd_msg_3);
    }

    if (cmd_args->fuse_negative_timeout >= 0) {
        DICT_SET_VAL(dict_set_double, options, ZR_NEGATIVE_TIMEOUT_OPT,
                     cmd_args->fuse_negative_timeout, glusterfsd_msg_3);
    }

    if (cmd_args->client_pid_set) {
        DICT_SET_VAL(dict_set_int32_sizen, options, "client-pid",
                     cmd_args->client_pid, glusterfsd_msg_3);
    }

    if (cmd_args->uid_map_root) {
        DICT_SET_VAL(dict_set_int32_sizen, options, "uid-map-root",
                     cmd_args->uid_map_root, glusterfsd_msg_3);
    }

    if (cmd_args->volfile_check) {
        DICT_SET_VAL(dict_set_int32_sizen, options, ZR_STRICT_VOLFILE_CHECK,
                     cmd_args->volfile_check, glusterfsd_msg_3);
    }

    if (cmd_args->dump_fuse) {
        DICT_SET_VAL(dict_set_static_ptr, options, ZR_DUMP_FUSE,
                     cmd_args->dump_fuse, glusterfsd_msg_3);
    }

    if (cmd_args->acl) {
        DICT_SET_VAL(dict_set_static_ptr, options, "acl", "on",
                     glusterfsd_msg_3);
    }

    if (cmd_args->selinux) {
        DICT_SET_VAL(dict_set_static_ptr, options, "selinux", "on",
                     glusterfsd_msg_3);
    }

    if (cmd_args->capability) {
        DICT_SET_VAL(dict_set_static_ptr, options, "capability", "on",
                     glusterfsd_msg_3);
    }

    if (cmd_args->aux_gfid_mount) {
        DICT_SET_VAL(dict_set_static_ptr, options, "virtual-gfid-access", "on",
                     glusterfsd_msg_3);
    }

    if (cmd_args->enable_ino32) {
        DICT_SET_VAL(dict_set_static_ptr, options, "enable-ino32", "on",
                     glusterfsd_msg_3);
    }

    if (cmd_args->read_only) {
        DICT_SET_VAL(dict_set_static_ptr, options, "read-only", "on",
                     glusterfsd_msg_3);
    }

    switch (cmd_args->fopen_keep_cache) {
        case GF_OPTION_ENABLE:

            DICT_SET_VAL(dict_set_static_ptr, options, "fopen-keep-cache", "on",
                         glusterfsd_msg_3);
            break;
        case GF_OPTION_DISABLE:
            DICT_SET_VAL(dict_set_static_ptr, options, "fopen-keep-cache",
                         "off", glusterfsd_msg_3);
            break;
        default:
            gf_msg_debug("glusterfsd", 0, "fopen-keep-cache mode %d",
                         cmd_args->fopen_keep_cache);
            break;
    }

    if (cmd_args->gid_timeout_set) {
        DICT_SET_VAL(dict_set_int32_sizen, options, "gid-timeout",
                     cmd_args->gid_timeout, glusterfsd_msg_3);
    }

    if (cmd_args->resolve_gids) {
        DICT_SET_VAL(dict_set_static_ptr, options, "resolve-gids", "on",
                     glusterfsd_msg_3);
    }

    if (cmd_args->lru_limit >= 0) {
        DICT_SET_VAL(dict_set_int32_sizen, options, "lru-limit",
                     cmd_args->lru_limit, glusterfsd_msg_3);
    }

    if (cmd_args->invalidate_limit >= 0) {
        DICT_SET_VAL(dict_set_int32_sizen, options, "invalidate-limit",
                     cmd_args->invalidate_limit, glusterfsd_msg_3);
    }

    if (cmd_args->background_qlen) {
        DICT_SET_VAL(dict_set_int32_sizen, options, "background-qlen",
                     cmd_args->background_qlen, glusterfsd_msg_3);
    }
    if (cmd_args->congestion_threshold) {
        DICT_SET_VAL(dict_set_int32_sizen, options, "congestion-threshold",
                     cmd_args->congestion_threshold, glusterfsd_msg_3);
    }

    switch (cmd_args->fuse_direct_io_mode) {
        case GF_OPTION_DISABLE: /* disable */
            DICT_SET_VAL(dict_set_static_ptr, options, ZR_DIRECT_IO_OPT,
                         "disable", glusterfsd_msg_3);
            break;
        case GF_OPTION_ENABLE: /* enable */
            DICT_SET_VAL(dict_set_static_ptr, options, ZR_DIRECT_IO_OPT,
                         "enable", glusterfsd_msg_3);
            break;
        default:
            gf_msg_debug("glusterfsd", 0, "fuse direct io type %d",
                         cmd_args->fuse_direct_io_mode);
            break;
    }

    switch (cmd_args->no_root_squash) {
        case GF_OPTION_ENABLE: /* enable */
            DICT_SET_VAL(dict_set_static_ptr, options, "no-root-squash",
                         "enable", glusterfsd_msg_3);
            break;
        default:
            DICT_SET_VAL(dict_set_static_ptr, options, "no-root-squash",
                         "disable", glusterfsd_msg_3);
            gf_msg_debug("glusterfsd", 0, "fuse no-root-squash mode %d",
                         cmd_args->no_root_squash);
            break;
    }

    if (!cmd_args->no_daemon_mode) {
        DICT_SET_VAL(dict_set_static_ptr, options, "sync-to-mount", "enable",
                     glusterfsd_msg_3);
    }

    if (cmd_args->use_readdirp) {
        DICT_SET_VAL(dict_set_static_ptr, options, "use-readdirp",
                     cmd_args->use_readdirp, glusterfsd_msg_3);
    }
    if (cmd_args->event_history) {
        ret = dict_set_str(options, "event-history", cmd_args->event_history);
        DICT_SET_VAL(dict_set_static_ptr, options, "event-history",
                     cmd_args->event_history, glusterfsd_msg_3);
    }
    if (cmd_args->thin_client) {
        DICT_SET_VAL(dict_set_static_ptr, options, "thin-client", "on",
                     glusterfsd_msg_3);
    }
    if (cmd_args->reader_thread_count) {
        DICT_SET_VAL(dict_set_uint32, options, "reader-thread-count",
                     cmd_args->reader_thread_count, glusterfsd_msg_3);
    }

    DICT_SET_VAL(dict_set_uint32, options, "auto-invalidation",
                 cmd_args->fuse_auto_inval, glusterfsd_msg_3);

    switch (cmd_args->kernel_writeback_cache) {
        case GF_OPTION_ENABLE:
            DICT_SET_VAL(dict_set_static_ptr, options, "kernel-writeback-cache",
                         "on", glusterfsd_msg_3);
            break;
        case GF_OPTION_DISABLE:
            DICT_SET_VAL(dict_set_static_ptr, options, "kernel-writeback-cache",
                         "off", glusterfsd_msg_3);
            break;
        default:
            gf_msg_debug("glusterfsd", 0, "kernel-writeback-cache mode %d",
                         cmd_args->kernel_writeback_cache);
            break;
    }
    if (cmd_args->attr_times_granularity) {
        DICT_SET_VAL(dict_set_uint32, options, "attr-times-granularity",
                     cmd_args->attr_times_granularity, glusterfsd_msg_3);
    }
    switch (cmd_args->fuse_flush_handle_interrupt) {
        case GF_OPTION_ENABLE:
            DICT_SET_VAL(dict_set_static_ptr, options, "flush-handle-interrupt",
                         "on", glusterfsd_msg_3);
            break;
        case GF_OPTION_DISABLE:
            DICT_SET_VAL(dict_set_static_ptr, options, "flush-handle-interrupt",
                         "off", glusterfsd_msg_3);
            break;
        default:
            gf_msg_debug("glusterfsd", 0, "fuse-flush-handle-interrupt mode %d",
                         cmd_args->fuse_flush_handle_interrupt);
            break;
    }
    if (cmd_args->global_threading) {
        DICT_SET_VAL(dict_set_static_ptr, options, "global-threading", "on",
                     glusterfsd_msg_3);
    }
    if (cmd_args->fuse_dev_eperm_ratelimit_ns) {
        DICT_SET_VAL(dict_set_uint32, options, "fuse-dev-eperm-ratelimit-ns",
                     cmd_args->fuse_dev_eperm_ratelimit_ns, glusterfsd_msg_3);
    }

    ret = 0;
err:
    return ret;
}

int
create_fuse_mount(glusterfs_ctx_t *ctx)
{
    int ret = 0;
    cmd_args_t *cmd_args = NULL;
    xlator_t *primary = NULL;

    cmd_args = &ctx->cmd_args;
    if (!cmd_args->mount_point) {
        gf_msg_trace("glusterfsd", 0,
                     "mount point not found, not a client process");
        return 0;
    }

    if (ctx->process_mode != GF_CLIENT_PROCESS) {
        gf_smsg("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_7, NULL);
        return -1;
    }

    primary = GF_CALLOC(1, sizeof(*primary), gfd_mt_xlator_t);
    if (!primary)
        goto err;

    primary->name = gf_strdup("fuse");
    if (!primary->name)
        goto err;

    if (xlator_set_type(primary, "mount/fuse") == -1) {
        gf_smsg("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_8,
                "MOUNT-POINT=%s", cmd_args->mount_point, NULL);
        goto err;
    }

    primary->ctx = ctx;
    primary->options = dict_new();
    if (!primary->options)
        goto err;

    ret = set_fuse_mount_options(ctx, primary->options);
    if (ret)
        goto err;

    if (cmd_args->fuse_mountopts) {
        ret = dict_set_static_ptr(primary->options, ZR_FUSE_MOUNTOPTS,
                                  cmd_args->fuse_mountopts);
        if (ret < 0) {
            gf_smsg("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_3,
                    ZR_FUSE_MOUNTOPTS, NULL);
            goto err;
        }
    }

    ret = xlator_init(primary);
    if (ret) {
        gf_msg_debug("glusterfsd", 0, "failed to initialize fuse translator");
        goto err;
    }

    ctx->primary = primary;

    return 0;

err:
    if (primary) {
        xlator_destroy(primary);
    }

    return 1;
}

static FILE *
get_volfp(glusterfs_ctx_t *ctx)
{
    cmd_args_t *cmd_args = NULL;
    FILE *specfp = NULL;

    cmd_args = &ctx->cmd_args;

    if ((specfp = fopen(cmd_args->volfile, "r")) == NULL) {
        gf_smsg("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_9,
                "volume_file=%s", cmd_args->volfile, NULL);
        return NULL;
    }

    gf_msg_debug("glusterfsd", 0, "loading volume file %s", cmd_args->volfile);

    return specfp;
}

static int
gf_remember_backup_volfile_server(char *arg)
{
    glusterfs_ctx_t *ctx = NULL;
    cmd_args_t *cmd_args = NULL;
    int ret = -1;

    ctx = glusterfsd_ctx;
    if (!ctx)
        goto out;
    cmd_args = &ctx->cmd_args;

    if (!cmd_args)
        goto out;

    ret = gf_set_volfile_server_common(
        cmd_args, arg, GF_DEFAULT_VOLFILE_TRANSPORT, GF_DEFAULT_BASE_PORT);
    if (ret) {
        gf_log("glusterfs", GF_LOG_ERROR, "failed to set volfile server: %s",
               strerror(errno));
    }
out:
    return ret;
}

static int
gf_remember_xlator_option(char *arg)
{
    glusterfs_ctx_t *ctx = NULL;
    cmd_args_t *cmd_args = NULL;
    xlator_cmdline_option_t *option = NULL;
    int ret = -1;
    char *dot = NULL;
    char *equals = NULL;

    ctx = glusterfsd_ctx;
    cmd_args = &ctx->cmd_args;

    option = GF_CALLOC(1, sizeof(xlator_cmdline_option_t),
                       gfd_mt_xlator_cmdline_option_t);
    if (!option)
        goto out;

    INIT_LIST_HEAD(&option->cmd_args);

    dot = strchr(arg, '.');
    if (!dot) {
        gf_smsg("", GF_LOG_WARNING, 0, glusterfsd_msg_10, "arg=%s", arg, NULL);
        goto out;
    }

    option->volume = GF_MALLOC((dot - arg) + 1, gfd_mt_char);
    if (!option->volume)
        goto out;

    strncpy(option->volume, arg, (dot - arg));
    option->volume[(dot - arg)] = '\0';

    equals = strchr(arg, '=');
    if (!equals) {
        gf_smsg("", GF_LOG_WARNING, 0, glusterfsd_msg_10, "arg=%s", arg, NULL);
        goto out;
    }

    option->key = GF_MALLOC((equals - dot) + 1, gfd_mt_char);
    if (!option->key)
        goto out;

    strncpy(option->key, dot + 1, (equals - dot - 1));
    option->key[(equals - dot - 1)] = '\0';

    if (!*(equals + 1)) {
        gf_smsg("", GF_LOG_WARNING, 0, glusterfsd_msg_10, "arg=%s", arg, NULL);
        goto out;
    }

    option->value = gf_strdup(equals + 1);

    list_add(&option->cmd_args, &cmd_args->xlator_options);

    ret = 0;
out:
    if (ret == -1) {
        if (option) {
            GF_FREE(option->volume);
            GF_FREE(option->key);
            GF_FREE(option->value);

            GF_FREE(option);
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
    {"/proc/self/oom_score_adj", OOM_SCORE_ADJ_MIN, OOM_SCORE_ADJ_MAX},
    {"/proc/self/oom_adj", OOM_DISABLE, OOM_ADJUST_MAX},
    {NULL, 0, 0}};

static struct oom_api_info *
get_oom_api_info(void)
{
    struct oom_api_info *api = NULL;

    for (api = oom_api_info; api->oom_api_file; api++) {
        if (sys_access(api->oom_api_file, F_OK) != -1) {
            return api;
        }
    }

    return NULL;
}
#endif

static error_t
parse_opts(int key, char *arg, struct argp_state *state)
{
    cmd_args_t *cmd_args = NULL;
    uint32_t n = 0;
#ifdef GF_LINUX_HOST_OS
    int32_t k = 0;
    struct oom_api_info *api = NULL;
#endif
    double d = 0.0;
    gf_boolean_t b = _gf_false;
    char *pwd = NULL;
    char *tmp_str = NULL;
    char *port_str = NULL;
    struct passwd *pw = NULL;
    int ret = 0;

    cmd_args = state->input;

    switch (key) {
        case ARGP_VOLFILE_SERVER_KEY:
            gf_remember_backup_volfile_server(arg);

            break;

        case ARGP_READ_ONLY_KEY:
            cmd_args->read_only = 1;
            break;

        case ARGP_ACL_KEY:
            cmd_args->acl = 1;
            gf_remember_xlator_option("*-md-cache.cache-posix-acl=true");
            break;

        case ARGP_SELINUX_KEY:
            cmd_args->selinux = 1;
            gf_remember_xlator_option("*-md-cache.cache-selinux=true");
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

        case ARGP_PRINT_XLATORDIR_KEY:
            cmd_args->print_xlatordir = _gf_true;
            break;

        case ARGP_PRINT_STATEDUMPDIR_KEY:
            cmd_args->print_statedumpdir = _gf_true;
            break;

        case ARGP_PRINT_LOGDIR_KEY:
            cmd_args->print_logdir = _gf_true;
            break;

        case ARGP_PRINT_LIBEXECDIR_KEY:
            cmd_args->print_libexecdir = _gf_true;
            break;

        case ARGP_MAC_COMPAT_KEY:
            if (!arg)
                arg = "on";

            if (gf_string2boolean(arg, &b) == 0) {
                cmd_args->mac_compat = b;

                break;
            }

            argp_failure(state, -1, 0, "invalid value \"%s\" for mac-compat",
                         arg);
            break;

        case ARGP_VOLUME_FILE_KEY:
            GF_FREE(cmd_args->volfile);

            if (arg[0] != '/') {
                pwd = getcwd(NULL, PATH_MAX);
                if (!pwd) {
                    argp_failure(state, -1, errno,
                                 "getcwd failed with error no %d", errno);
                    break;
                }
                char tmp_buf[1024];
                snprintf(tmp_buf, sizeof(tmp_buf), "%s/%s", pwd, arg);
                cmd_args->volfile = gf_strdup(tmp_buf);
                free(pwd);
            } else {
                cmd_args->volfile = gf_strdup(arg);
            }

            break;

        case ARGP_LOG_LEVEL_KEY:
            if (strcasecmp(arg, ARGP_LOG_LEVEL_NONE_OPTION) == 0) {
                cmd_args->log_level = GF_LOG_NONE;
                break;
            }
            if (strcasecmp(arg, ARGP_LOG_LEVEL_CRITICAL_OPTION) == 0) {
                cmd_args->log_level = GF_LOG_CRITICAL;
                break;
            }
            if (strcasecmp(arg, ARGP_LOG_LEVEL_ERROR_OPTION) == 0) {
                cmd_args->log_level = GF_LOG_ERROR;
                break;
            }
            if (strcasecmp(arg, ARGP_LOG_LEVEL_WARNING_OPTION) == 0) {
                cmd_args->log_level = GF_LOG_WARNING;
                break;
            }
            if (strcasecmp(arg, ARGP_LOG_LEVEL_INFO_OPTION) == 0) {
                cmd_args->log_level = GF_LOG_INFO;
                break;
            }
            if (strcasecmp(arg, ARGP_LOG_LEVEL_DEBUG_OPTION) == 0) {
                cmd_args->log_level = GF_LOG_DEBUG;
                break;
            }
            if (strcasecmp(arg, ARGP_LOG_LEVEL_TRACE_OPTION) == 0) {
                cmd_args->log_level = GF_LOG_TRACE;
                break;
            }

            argp_failure(state, -1, 0, "unknown log level %s", arg);
            break;

        case ARGP_LOG_FILE_KEY:
            cmd_args->log_file = gf_strdup(arg);
            break;

        case ARGP_VOLFILE_SERVER_PORT_KEY:
            n = 0;

            if (gf_string2uint_base10(arg, &n) == 0) {
                cmd_args->volfile_server_port = n;
                break;
            }

            argp_failure(state, -1, 0, "unknown volfile server port %s", arg);
            break;

        case ARGP_VOLFILE_SERVER_TRANSPORT_KEY:
            cmd_args->volfile_server_transport = gf_strdup(arg);
            break;

        case ARGP_VOLFILE_ID_KEY:
            cmd_args->volfile_id = gf_strdup(arg);
            break;

        case ARGP_THIN_CLIENT_KEY:
            cmd_args->thin_client = _gf_true;
            break;

        case ARGP_BRICK_MUX_KEY:
            cmd_args->brick_mux = _gf_true;
            break;

        case ARGP_PID_FILE_KEY:
            cmd_args->pid_file = gf_strdup(arg);
            break;

        case ARGP_SOCK_FILE_KEY:
            cmd_args->sock_file = gf_strdup(arg);
            break;

        case ARGP_NO_DAEMON_KEY:
            cmd_args->no_daemon_mode = ENABLE_NO_DAEMON_MODE;
            break;

        case ARGP_RUN_ID_KEY:
            cmd_args->run_id = gf_strdup(arg);
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

            if (gf_string2boolean(arg, &b) == 0) {
                cmd_args->fuse_direct_io_mode = b;

                break;
            }

            if (strcmp(arg, "auto") == 0)
                break;

            argp_failure(state, -1, 0, "unknown direct I/O mode setting \"%s\"",
                         arg);
            break;

        case ARGP_FUSE_NO_ROOT_SQUASH_KEY:
            cmd_args->no_root_squash = _gf_true;
            break;

        case ARGP_ENTRY_TIMEOUT_KEY:
            d = 0.0;

            gf_string2double(arg, &d);
            if (!(d < 0.0)) {
                cmd_args->fuse_entry_timeout = d;
                break;
            }

            argp_failure(state, -1, 0, "unknown entry timeout %s", arg);
            break;

        case ARGP_NEGATIVE_TIMEOUT_KEY:
            d = 0.0;

            ret = gf_string2double(arg, &d);
            if ((ret == 0) && !(d < 0.0)) {
                cmd_args->fuse_negative_timeout = d;
                break;
            }

            argp_failure(state, -1, 0, "unknown negative timeout %s", arg);
            break;

        case ARGP_ATTRIBUTE_TIMEOUT_KEY:
            d = 0.0;

            gf_string2double(arg, &d);
            if (!(d < 0.0)) {
                cmd_args->fuse_attribute_timeout = d;
                break;
            }

            argp_failure(state, -1, 0, "unknown attribute timeout %s", arg);
            break;

        case ARGP_CLIENT_PID_KEY:
            if (gf_string2int(arg, &cmd_args->client_pid) == 0) {
                cmd_args->client_pid_set = 1;
                break;
            }

            argp_failure(state, -1, 0, "unknown client pid %s", arg);
            break;

        case ARGP_USER_MAP_ROOT_KEY:
            pw = getpwnam(arg);
            if (pw)
                cmd_args->uid_map_root = pw->pw_uid;
            else
                argp_failure(state, -1, 0, "user %s does not exist", arg);
            break;

        case ARGP_VOLFILE_CHECK_KEY:
            cmd_args->volfile_check = 1;
            break;

        case ARGP_VOLUME_NAME_KEY:
            cmd_args->volume_name = gf_strdup(arg);
            break;

        case ARGP_XLATOR_OPTION_KEY:
            if (gf_remember_xlator_option(arg))
                argp_failure(state, -1, 0, "invalid xlator option  %s", arg);

            break;

        case ARGP_KEY_NO_ARGS:
            break;

        case ARGP_KEY_ARG:
            if (state->arg_num >= 1)
                argp_usage(state);
            cmd_args->mount_point = gf_strdup(arg);
            break;

        case ARGP_DUMP_FUSE_KEY:
            cmd_args->dump_fuse = gf_strdup(arg);
            break;
        case ARGP_BRICK_NAME_KEY:
            cmd_args->brick_name = gf_strdup(arg);
            break;
        case ARGP_BRICK_PORT_KEY:
            n = 0;

            if (arg != NULL) {
                port_str = strtok_r(arg, ",", &tmp_str);
                if (gf_string2uint_base10(port_str, &n) == 0) {
                    cmd_args->brick_port = n;
                    port_str = strtok_r(NULL, ",", &tmp_str);
                    if (port_str) {
                        if (gf_string2uint_base10(port_str, &n) == 0) {
                            cmd_args->brick_port2 = n;
                            break;
                        }
                        argp_failure(state, -1, 0,
                                     "wrong brick (listen) port %s", arg);
                    }
                    break;
                }
            }

            argp_failure(state, -1, 0, "unknown brick (listen) port %s", arg);
            break;

        case ARGP_MEM_ACCOUNTING_KEY:
            /* TODO: it should have got handled much earlier */
            // gf_mem_acct_enable_set (THIS->ctx);
            break;

        case ARGP_FOPEN_KEEP_CACHE_KEY:
            if (!arg)
                arg = "on";

            if (gf_string2boolean(arg, &b) == 0) {
                cmd_args->fopen_keep_cache = b;

                break;
            }

            argp_failure(state, -1, 0, "unknown cache setting \"%s\"", arg);

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

        case ARGP_FUSE_LRU_LIMIT_KEY:
            if (!gf_string2int32(arg, &cmd_args->lru_limit))
                break;

            argp_failure(state, -1, 0, "unknown LRU limit option %s", arg);
            break;

        case ARGP_FUSE_INVALIDATE_LIMIT_KEY:
            if (!gf_string2int32(arg, &cmd_args->invalidate_limit))
                break;

            argp_failure(state, -1, 0, "unknown invalidate limit option %s",
                         arg);
            break;

        case ARGP_FUSE_BACKGROUND_QLEN_KEY:
            if (!gf_string2int(arg, &cmd_args->background_qlen))
                break;

            argp_failure(state, -1, 0, "unknown background qlen option %s",
                         arg);
            break;
        case ARGP_FUSE_CONGESTION_THRESHOLD_KEY:
            if (!gf_string2int(arg, &cmd_args->congestion_threshold))
                break;

            argp_failure(state, -1, 0, "unknown congestion threshold option %s",
                         arg);
            break;

#ifdef GF_LINUX_HOST_OS
        case ARGP_OOM_SCORE_ADJ_KEY:
            k = 0;

            api = get_oom_api_info();
            if (!api)
                goto no_oom_api;

            if (gf_string2int(arg, &k) == 0 && k >= api->oom_min &&
                k <= api->oom_max) {
                cmd_args->oom_score_adj = gf_strdup(arg);
                break;
            }

            argp_failure(state, -1, 0, "unknown oom_score_adj value %s", arg);

        no_oom_api:
            break;
#endif

        case ARGP_FUSE_MOUNTOPTS_KEY:
            cmd_args->fuse_mountopts = gf_strdup(arg);
            break;

        case ARGP_FUSE_USE_READDIRP_KEY:
            if (!arg)
                arg = "yes";

            if (gf_string2boolean(arg, &b) == 0) {
                if (b) {
                    cmd_args->use_readdirp = "yes";
                } else {
                    cmd_args->use_readdirp = "no";
                }

                break;
            }

            argp_failure(state, -1, 0, "unknown use-readdirp setting \"%s\"",
                         arg);
            break;

        case ARGP_LOGGER:
            if (strcasecmp(arg, GF_LOGGER_GLUSTER_LOG) == 0)
                cmd_args->logger = gf_logger_glusterlog;
            else if (strcasecmp(arg, GF_LOGGER_SYSLOG) == 0)
                cmd_args->logger = gf_logger_syslog;
            else
                argp_failure(state, -1, 0, "unknown logger %s", arg);

            break;

        case ARGP_LOG_FORMAT:
            if (strcasecmp(arg, GF_LOG_FORMAT_NO_MSG_ID) == 0)
                cmd_args->log_format = gf_logformat_traditional;
            else if (strcasecmp(arg, GF_LOG_FORMAT_WITH_MSG_ID) == 0)
                cmd_args->log_format = gf_logformat_withmsgid;
            else
                argp_failure(state, -1, 0, "unknown log format %s", arg);

            break;

        case ARGP_LOG_BUF_SIZE:
            if (gf_string2uint32(arg, &cmd_args->log_buf_size)) {
                argp_failure(state, -1, 0, "unknown log buf size option %s",
                             arg);
            } else if (cmd_args->log_buf_size > GF_LOG_LRU_BUFSIZE_MAX) {
                argp_failure(state, -1, 0,
                             "Invalid log buf size %s. "
                             "Valid range: [" GF_LOG_LRU_BUFSIZE_MIN_STR
                             "," GF_LOG_LRU_BUFSIZE_MAX_STR "]",
                             arg);
            }

            break;

        case ARGP_LOG_FLUSH_TIMEOUT:
            if (gf_string2uint32(arg, &cmd_args->log_flush_timeout)) {
                argp_failure(state, -1, 0,
                             "unknown log flush timeout option %s", arg);
            } else if ((cmd_args->log_flush_timeout <
                        GF_LOG_FLUSH_TIMEOUT_MIN) ||
                       (cmd_args->log_flush_timeout >
                        GF_LOG_FLUSH_TIMEOUT_MAX)) {
                argp_failure(state, -1, 0,
                             "Invalid log flush timeout %s. "
                             "Valid range: [" GF_LOG_FLUSH_TIMEOUT_MIN_STR
                             "," GF_LOG_FLUSH_TIMEOUT_MAX_STR "]",
                             arg);
            }

            break;

        case ARGP_SECURE_MGMT_KEY:
            if (!arg)
                arg = "yes";

            if (gf_string2boolean(arg, &b) == 0) {
                cmd_args->secure_mgmt = b ? 1 : 0;
                break;
            }

            argp_failure(state, -1, 0, "unknown secure-mgmt setting \"%s\"",
                         arg);
            break;

        case ARGP_LOCALTIME_LOGGING_KEY:
            cmd_args->localtime_logging = 1;
            break;
        case ARGP_PROCESS_NAME_KEY:
            cmd_args->process_name = gf_strdup(arg);
            break;
        case ARGP_SUBDIR_MOUNT_KEY:
            if (arg[0] != '/') {
                argp_failure(state, -1, 0, "expect '/%s', provided just \"%s\"",
                             arg, arg);
                break;
            }
            cmd_args->subdir_mount = gf_strdup(arg);
            break;
        case ARGP_FUSE_EVENT_HISTORY_KEY:
            if (!arg)
                arg = "no";

            if (gf_string2boolean(arg, &b) == 0) {
                if (b) {
                    cmd_args->event_history = "yes";
                } else {
                    cmd_args->event_history = "no";
                }

                break;
            }

            argp_failure(state, -1, 0, "unknown event-history setting \"%s\"",
                         arg);
            break;
        case ARGP_READER_THREAD_COUNT_KEY:
            if (gf_string2uint32(arg, &cmd_args->reader_thread_count)) {
                argp_failure(state, -1, 0,
                             "unknown reader thread count option %s", arg);
            } else if ((cmd_args->reader_thread_count < 1) ||
                       (cmd_args->reader_thread_count > 64)) {
                argp_failure(state, -1, 0,
                             "Invalid reader thread count %s. "
                             "Valid range: [\"1, 64\"]",
                             arg);
            }

            break;

        case ARGP_KERNEL_WRITEBACK_CACHE_KEY:
            if (!arg)
                arg = "yes";

            if (gf_string2boolean(arg, &b) == 0) {
                cmd_args->kernel_writeback_cache = b;

                break;
            }

            argp_failure(state, -1, 0,
                         "unknown kernel writeback cache setting \"%s\"", arg);
            break;
        case ARGP_ATTR_TIMES_GRANULARITY_KEY:
            if (gf_string2uint32(arg, &cmd_args->attr_times_granularity)) {
                argp_failure(state, -1, 0,
                             "unknown attribute times granularity option %s",
                             arg);
            } else if (cmd_args->attr_times_granularity > 1000000000) {
                argp_failure(state, -1, 0,
                             "Invalid attribute times granularity value %s. "
                             "Valid range: [\"0, 1000000000\"]",
                             arg);
            }

            break;

        case ARGP_FUSE_FLUSH_HANDLE_INTERRUPT_KEY:
            if (!arg)
                arg = "yes";

            if (gf_string2boolean(arg, &b) == 0) {
                cmd_args->fuse_flush_handle_interrupt = b;

                break;
            }

            argp_failure(state, -1, 0,
                         "unknown fuse flush handle interrupt setting \"%s\"",
                         arg);
            break;

        case ARGP_FUSE_AUTO_INVAL_KEY:
            if (!arg)
                arg = "yes";

            if (gf_string2boolean(arg, &b) == 0) {
                cmd_args->fuse_auto_inval = b;
                break;
            }

            break;

        case ARGP_GLOBAL_THREADING_KEY:
            if (!arg || (*arg == 0)) {
                arg = "yes";
            }

            if (gf_string2boolean(arg, &b) == 0) {
                cmd_args->global_threading = b;
                break;
            }

            argp_failure(state, -1, 0,
                         "Invalid value for global threading \"%s\"", arg);
            break;

        case ARGP_FUSE_DEV_EPERM_RATELIMIT_NS_KEY:
            if (gf_string2uint32(arg, &cmd_args->fuse_dev_eperm_ratelimit_ns)) {
                argp_failure(state, -1, 0,
                             "Non-numerical value for "
                             "'fuse-dev-eperm-ratelimit-ns' option %s",
                             arg);
            } else if (cmd_args->fuse_dev_eperm_ratelimit_ns > 1000000000) {
                argp_failure(state, -1, 0,
                             "Invalid 'fuse-dev-eperm-ratelimit-ns' value %s. "
                             "Valid range: [\"0, 1000000000\"]",
                             arg);
            }

            break;
    }
    return 0;
}

gf_boolean_t
should_call_fini(glusterfs_ctx_t *ctx, xlator_t *trav)
{
    /* There's nothing to call, so the other checks don't matter. */
    if (!trav->fini) {
        return _gf_false;
    }

    /* This preserves previous behavior in glusterd. */
    if (ctx->process_mode == GF_GLUSTERD_PROCESS) {
        return _gf_true;
    }

    return _gf_false;
}

void
cleanup_and_exit(int signum)
{
    glusterfs_ctx_t *ctx = NULL;
    xlator_t *trav = NULL;
    xlator_t *top;
    xlator_t *victim;
    xlator_list_t **trav_p;

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

    gf_log_disable_suppression_before_exit(ctx);

    gf_msg_callingfn("", GF_LOG_WARNING, 0, glusterfsd_msg_32,
                     "received signum (%d), shutting down", signum);

    if (ctx->cleanup_started)
        return;
    pthread_mutex_lock(&ctx->cleanup_lock);
    {
        ctx->cleanup_started = 1;

        /* signout should be sent to all the bricks in case brick mux is enabled
         * and multiple brick instances are attached to this process
         */
        if (ctx->active) {
            top = ctx->active->first;
            for (trav_p = &top->children; *trav_p; trav_p = &(*trav_p)->next) {
                victim = (*trav_p)->xlator;
                rpc_clnt_mgmt_pmap_signout(ctx, victim->name);
            }
        } else {
            rpc_clnt_mgmt_pmap_signout(ctx, NULL);
        }

        /* below part is a racy code where the rpcsvc object is freed.
         * But in another thread (epoll thread), upon poll error in the
         * socket the transports are cleaned up where again rpcsvc object
         * is accessed (which is already freed by the below function).
         * Since the process is about to be killed don't execute the function
         * below.
         */
        /* if (ctx->listener) { */
        /*         (void) glusterfs_listener_stop (ctx); */
        /* } */

        /* Call fini() of FUSE xlator first:
         * so there are no more requests coming and
         * 'umount' of mount point is done properly */
        trav = ctx->primary;
        if (trav && trav->fini) {
            THIS = trav;
            trav->fini(trav);
        }

        glusterfs_pidfile_cleanup(ctx);

#if 0
        /* TODO: Properly do cleanup_and_exit(), with synchronization */
        if (ctx->mgmt) {
                /* cleanup the saved-frames before last unref */
                rpc_clnt_connection_cleanup (&ctx->mgmt->conn);
                rpc_clnt_unref (ctx->mgmt);
        }
#endif

        trav = NULL;

        /* previously we were releasing the cleanup mutex lock before the
           process exit. As we are releasing the cleanup mutex lock, before
           the process can exit some other thread which is blocked on
           cleanup mutex lock is acquiring the cleanup mutex lock and
           trying to acquire some resources which are already freed as a
           part of cleanup. To avoid this, we are exiting the process without
           releasing the cleanup mutex lock. This will not cause any lock
           related issues as the process which acquired the lock is going down
         */
        /* NOTE: Only the least significant 8 bits i.e (signum & 255)
           will be available to parent process on calling exit() */
        exit(abs(signum));
    }
}

static void
reincarnate(int signum)
{
    int ret = 0;
    glusterfs_ctx_t *ctx = NULL;
    cmd_args_t *cmd_args = NULL;

    ctx = glusterfsd_ctx;
    cmd_args = &ctx->cmd_args;

    gf_msg_trace("gluster", 0, "received reincarnate request (sig:HUP)");

    if (cmd_args->volfile_server) {
        gf_smsg("glusterfsd", GF_LOG_INFO, 0, glusterfsd_msg_11, NULL);
        ret = glusterfs_volfile_fetch(ctx);
    }

    /* Also, SIGHUP should do logrotate */
    gf_log_logrotate(1);

    if (ret < 0)
        gf_smsg("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_12, NULL);

    return;
}

void
emancipate(glusterfs_ctx_t *ctx, int ret)
{
    /* break free from the parent */
    if (ctx->daemon_pipe[1] != -1) {
        sys_write(ctx->daemon_pipe[1], (void *)&ret, sizeof(ret));
        sys_close(ctx->daemon_pipe[1]);
        ctx->daemon_pipe[1] = -1;
    }
}

static uint8_t
gf_get_process_mode(char *exec_name)
{
    char *dup_execname = NULL, *base = NULL;
    uint8_t ret = 0;

    dup_execname = gf_strdup(exec_name);
    base = basename(dup_execname);

    if (!strncmp(base, "glusterfsd", 10)) {
        ret = GF_SERVER_PROCESS;
    } else if (!strncmp(base, "glusterd", 8)) {
        ret = GF_GLUSTERD_PROCESS;
    } else {
        ret = GF_CLIENT_PROCESS;
    }

    GF_FREE(dup_execname);

    return ret;
}

static int
glusterfs_ctx_defaults_init(glusterfs_ctx_t *ctx)
{
    cmd_args_t *cmd_args = NULL;
    struct rlimit lim = {
        0,
    };
    int ret = -1;

    if (!ctx)
        return ret;

    ret = xlator_mem_acct_init(THIS, gfd_mt_end);
    if (ret != 0) {
        gf_smsg(THIS->name, GF_LOG_CRITICAL, 0, glusterfsd_msg_34, NULL);
        return ret;
    }

    /* reset ret to -1 so that we don't need to explicitly
     * set it in all error paths before "goto err"
     */
    ret = -1;

    /* monitoring should be enabled by default */
    ctx->measure_latency = true;

    ctx->process_uuid = generate_glusterfs_ctx_id();
    if (!ctx->process_uuid) {
        gf_smsg("", GF_LOG_CRITICAL, 0, glusterfsd_msg_13, NULL);
        goto out;
    }

    ctx->page_size = 128 * GF_UNIT_KB;

    ctx->iobuf_pool = iobuf_pool_new();
    if (!ctx->iobuf_pool) {
        gf_smsg("", GF_LOG_CRITICAL, 0, glusterfsd_msg_14, "iobuf", NULL);
        goto out;
    }

    ctx->event_pool = gf_event_pool_new(DEFAULT_EVENT_POOL_SIZE,
                                        STARTING_EVENT_THREADS);
    if (!ctx->event_pool) {
        gf_smsg("", GF_LOG_CRITICAL, 0, glusterfsd_msg_14, "event", NULL);
        goto out;
    }

    ctx->pool = GF_CALLOC(1, sizeof(call_pool_t), gfd_mt_call_pool_t);
    if (!ctx->pool) {
        gf_smsg("", GF_LOG_CRITICAL, 0, glusterfsd_msg_14, "call", NULL);
        goto out;
    }

    INIT_LIST_HEAD(&ctx->pool->all_frames);
    LOCK_INIT(&ctx->pool->lock);

    /* frame_mem_pool size 112 * 4k */
    ctx->pool->frame_mem_pool = mem_pool_new(call_frame_t, 4096);
    if (!ctx->pool->frame_mem_pool) {
        gf_smsg("", GF_LOG_CRITICAL, 0, glusterfsd_msg_14, "frame", NULL);
        goto out;
    }
    /* stack_mem_pool size 256 * 1024 */
    ctx->pool->stack_mem_pool = mem_pool_new(call_stack_t, 1024);
    if (!ctx->pool->stack_mem_pool) {
        gf_smsg("", GF_LOG_CRITICAL, 0, glusterfsd_msg_14, "stack", NULL);
        goto out;
    }

    ctx->stub_mem_pool = mem_pool_new(call_stub_t, 1024);
    if (!ctx->stub_mem_pool) {
        gf_smsg("", GF_LOG_CRITICAL, 0, glusterfsd_msg_14, "stub", NULL);
        goto out;
    }

    ctx->dict_pool = mem_pool_new(dict_t, GF_MEMPOOL_COUNT_OF_DICT_T);
    if (!ctx->dict_pool)
        goto out;

    ctx->dict_pair_pool = mem_pool_new(data_pair_t,
                                       GF_MEMPOOL_COUNT_OF_DATA_PAIR_T);
    if (!ctx->dict_pair_pool)
        goto out;

    ctx->dict_data_pool = mem_pool_new(data_t, GF_MEMPOOL_COUNT_OF_DATA_T);
    if (!ctx->dict_data_pool)
        goto out;

    ctx->logbuf_pool = mem_pool_new(log_buf_t, GF_MEMPOOL_COUNT_OF_LRU_BUF_T);
    if (!ctx->logbuf_pool)
        goto out;

    pthread_mutex_init(&ctx->notify_lock, NULL);
    pthread_mutex_init(&ctx->cleanup_lock, NULL);
    pthread_cond_init(&ctx->notify_cond, NULL);

    ctx->clienttable = gf_clienttable_alloc();
    if (!ctx->clienttable)
        goto out;

    cmd_args = &ctx->cmd_args;

    /* parsing command line arguments */
    cmd_args->log_level = DEFAULT_LOG_LEVEL;
    cmd_args->logger = gf_logger_glusterlog;
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
    cmd_args->kernel_writeback_cache = GF_OPTION_DEFERRED;
    cmd_args->fuse_flush_handle_interrupt = GF_OPTION_DEFERRED;

    if (ctx->mem_acct_enable)
        cmd_args->mem_acct = 1;

    INIT_LIST_HEAD(&cmd_args->xlator_options);
    INIT_LIST_HEAD(&cmd_args->volfile_servers);
    ctx->pxl_count = 0;
    ctx->diskxl_count = 0;
    pthread_mutex_init(&ctx->fd_lock, NULL);
    pthread_cond_init(&ctx->fd_cond, NULL);
    INIT_LIST_HEAD(&ctx->janitor_fds);
    pthread_mutex_init(&ctx->xl_lock, NULL);
    pthread_cond_init(&ctx->xl_cond, NULL);
    INIT_LIST_HEAD(&ctx->diskth_xl);

    lim.rlim_cur = RLIM_INFINITY;
    lim.rlim_max = RLIM_INFINITY;
    setrlimit(RLIMIT_CORE, &lim);

    ret = 0;
out:

    if (ret) {
        if (ctx->pool) {
            mem_pool_destroy(ctx->pool->frame_mem_pool);
            mem_pool_destroy(ctx->pool->stack_mem_pool);
        }
        GF_FREE(ctx->pool);
        mem_pool_destroy(ctx->stub_mem_pool);
        mem_pool_destroy(ctx->dict_pool);
        mem_pool_destroy(ctx->dict_data_pool);
        mem_pool_destroy(ctx->dict_pair_pool);
        mem_pool_destroy(ctx->logbuf_pool);
    }

    return ret;
}

static int
logging_init(glusterfs_ctx_t *ctx, const char *progpath)
{
    cmd_args_t *cmd_args = NULL;
    int ret = 0;

    cmd_args = &ctx->cmd_args;

    if (cmd_args->log_file == NULL) {
        ret = gf_set_log_file_path(cmd_args, ctx);
        if (ret == -1) {
            fprintf(stderr,
                    "ERROR: failed to set the log file "
                    "path\n");
            return -1;
        }
    }

    if (cmd_args->log_ident == NULL) {
        ret = gf_set_log_ident(cmd_args);
        if (ret == -1) {
            fprintf(stderr,
                    "ERROR: failed to set the log "
                    "identity\n");
            return -1;
        }
    }

    /* finish log set parameters before init */
    gf_log_set_loglevel(ctx, cmd_args->log_level);

    gf_log_set_localtime(cmd_args->localtime_logging);

    gf_log_set_logger(cmd_args->logger);

    gf_log_set_logformat(cmd_args->log_format);

    gf_log_set_log_buf_size(cmd_args->log_buf_size);

    gf_log_set_log_flush_timeout(cmd_args->log_flush_timeout);

    if (gf_log_init(ctx, cmd_args->log_file, cmd_args->log_ident) == -1) {
        fprintf(stderr, "ERROR: failed to open logfile %s\n",
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
gf_check_and_set_mem_acct(int argc, char *argv[])
{
    int i = 0;

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--no-mem-accounting") == 0) {
            gf_global_mem_acct_enable_set(0);
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
print_exports_file(const char *exports_file)
{
    void *libhandle = NULL;
    char *libpathfull = NULL;
    struct exports_file *file = NULL;
    int ret = 0;

    int (*exp_file_parse)(const char *filepath, struct exports_file **expfile,
                          struct mount3_state *ms) = NULL;
    void (*exp_file_print)(const struct exports_file *file) = NULL;
    void (*exp_file_deinit)(struct exports_file * ptr) = NULL;

    /* XLATORDIR passed through a -D flag to GCC */
    ret = gf_asprintf(&libpathfull, "%s/%s/server.so", XLATORDIR, "nfs");
    if (ret < 0) {
        gf_log("glusterfs", GF_LOG_CRITICAL, "asprintf () failed.");
        ret = -1;
        goto out;
    }

    /* Load up the library */
    libhandle = dlopen(libpathfull, RTLD_NOW);
    if (!libhandle) {
        gf_log("glusterfs", GF_LOG_CRITICAL,
               "Error loading NFS server library : "
               "%s\n",
               dlerror());
        ret = -1;
        goto out;
    }

    /* Load up the function */
    exp_file_parse = dlsym(libhandle, "exp_file_parse");
    if (!exp_file_parse) {
        gf_log("glusterfs", GF_LOG_CRITICAL,
               "Error finding function exp_file_parse "
               "in symbol.");
        ret = -1;
        goto out;
    }

    /* Parse the file */
    ret = exp_file_parse(exports_file, &file, NULL);
    if (ret < 0) {
        ret = 1; /* This means we failed to parse */
        goto out;
    }

    /* Load up the function */
    exp_file_print = dlsym(libhandle, "exp_file_print");
    if (!exp_file_print) {
        gf_log("glusterfs", GF_LOG_CRITICAL,
               "Error finding function exp_file_print in symbol.");
        ret = -1;
        goto out;
    }

    /* Print it out to screen */
    exp_file_print(file);

    /* Load up the function */
    exp_file_deinit = dlsym(libhandle, "exp_file_deinit");
    if (!exp_file_deinit) {
        gf_log("glusterfs", GF_LOG_CRITICAL,
               "Error finding function exp_file_deinit in lib.");
        ret = -1;
        goto out;
    }

    /* Free the file */
    exp_file_deinit(file);

out:
    if (libhandle)
        dlclose(libhandle);
    GF_FREE(libpathfull);
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
print_netgroups_file(const char *netgroups_file)
{
    void *libhandle = NULL;
    char *libpathfull = NULL;
    struct netgroups_file *file = NULL;
    int ret = 0;

    struct netgroups_file *(*ng_file_parse)(const char *file_path) = NULL;
    void (*ng_file_print)(const struct netgroups_file *file) = NULL;
    void (*ng_file_deinit)(struct netgroups_file * ptr) = NULL;

    /* XLATORDIR passed through a -D flag to GCC */
    ret = gf_asprintf(&libpathfull, "%s/%s/server.so", XLATORDIR, "nfs");
    if (ret < 0) {
        gf_log("glusterfs", GF_LOG_CRITICAL, "asprintf () failed.");
        ret = -1;
        goto out;
    }
    /* Load up the library */
    libhandle = dlopen(libpathfull, RTLD_NOW);
    if (!libhandle) {
        gf_log("glusterfs", GF_LOG_CRITICAL,
               "Error loading NFS server library : %s\n", dlerror());
        ret = -1;
        goto out;
    }

    /* Load up the function */
    ng_file_parse = dlsym(libhandle, "ng_file_parse");
    if (!ng_file_parse) {
        gf_log("glusterfs", GF_LOG_CRITICAL,
               "Error finding function ng_file_parse in symbol.");
        ret = -1;
        goto out;
    }

    /* Parse the file */
    file = ng_file_parse(netgroups_file);
    if (!file) {
        ret = 1; /* This means we failed to parse */
        goto out;
    }

    /* Load up the function */
    ng_file_print = dlsym(libhandle, "ng_file_print");
    if (!ng_file_print) {
        gf_log("glusterfs", GF_LOG_CRITICAL,
               "Error finding function ng_file_print in symbol.");
        ret = -1;
        goto out;
    }

    /* Print it out to screen */
    ng_file_print(file);

    /* Load up the function */
    ng_file_deinit = dlsym(libhandle, "ng_file_deinit");
    if (!ng_file_deinit) {
        gf_log("glusterfs", GF_LOG_CRITICAL,
               "Error finding function ng_file_deinit in lib.");
        ret = -1;
        goto out;
    }

    /* Free the file */
    ng_file_deinit(file);

out:
    if (libhandle)
        dlclose(libhandle);
    GF_FREE(libpathfull);
    return ret;
}

int
parse_cmdline(int argc, char *argv[], glusterfs_ctx_t *ctx)
{
    int process_mode = 0;
    int ret = 0;
    struct stat stbuf = {
        0,
    };
    char timestr[GF_TIMESTR_SIZE];
    char tmp_logfile[1024] = {0};
    char *tmp_logfile_dyn = NULL;
    char *tmp_logfilebase = NULL;
    cmd_args_t *cmd_args = NULL;
    int len = 0;
    char *thin_volfileid = NULL;

    cmd_args = &ctx->cmd_args;

    /* Do this before argp_parse so it can be overridden. */
    if (sys_access(SECURE_ACCESS_FILE, F_OK) == 0) {
        cmd_args->secure_mgmt = 1;
        ctx->ssl_cert_depth = glusterfs_read_secure_access_file();
    }

    /* Need to set lru_limit to below 0 to indicate there was nothing
       specified. This is needed as 0 is a valid option, and may not be
       default value. */
    cmd_args->lru_limit = -1;

    argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, cmd_args);

    if (cmd_args->print_xlatordir || cmd_args->print_statedumpdir ||
        cmd_args->print_logdir || cmd_args->print_libexecdir) {
        /* Just print, nothing else to do */
        goto out;
    }

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
        cmd_args->log_file = gf_strdup("/dev/stderr");
        cmd_args->no_daemon_mode = ENABLE_NO_DAEMON_MODE;
    }

    process_mode = gf_get_process_mode(argv[0]);
    ctx->process_mode = process_mode;

    if (cmd_args->process_name) {
        ctx->cmd_args.process_name = cmd_args->process_name;
    }
    /* Make sure after the parsing cli, if '--volfile-server' option is
       given, then '--volfile-id' is mandatory */
    if (cmd_args->volfile_server && !cmd_args->volfile_id) {
        gf_smsg("glusterfs", GF_LOG_CRITICAL, 0, glusterfsd_msg_15, NULL);
        ret = -1;
        goto out;
    }

    if ((cmd_args->volfile_server == NULL) && (cmd_args->volfile == NULL)) {
        if (process_mode == GF_SERVER_PROCESS)
            cmd_args->volfile = gf_strdup(DEFAULT_SERVER_VOLFILE);
        else if (process_mode == GF_GLUSTERD_PROCESS)
            cmd_args->volfile = gf_strdup(DEFAULT_GLUSTERD_VOLFILE);
        else
            cmd_args->volfile = gf_strdup(DEFAULT_CLIENT_VOLFILE);

        /* Check if the volfile exists, if not give usage output
           and exit */
        ret = sys_stat(cmd_args->volfile, &stbuf);
        if (ret) {
            gf_smsg("glusterfs", GF_LOG_CRITICAL, errno, glusterfsd_msg_16,
                    NULL);
            /* argp_usage (argp.) */
            fprintf(stderr, "USAGE: %s [options] [mountpoint]\n", argv[0]);
            goto out;
        }
    }

    if (cmd_args->thin_client) {
        len = strlen(cmd_args->volfile_id) + SLEN("gfproxy-client/");
        thin_volfileid = GF_MALLOC(len + 1, gf_common_mt_char);
        snprintf(thin_volfileid, len + 1, "gfproxy-client/%s",
                 cmd_args->volfile_id);
        GF_FREE(cmd_args->volfile_id);
        cmd_args->volfile_id = thin_volfileid;
    }

    if (cmd_args->run_id) {
        ret = sys_lstat(cmd_args->log_file, &stbuf);
        /* If its /dev/null, or /dev/stdout, /dev/stderr,
         * let it use the same, no need to alter
         */
        if (((ret == 0) &&
             (S_ISREG(stbuf.st_mode) || S_ISLNK(stbuf.st_mode))) ||
            (ret == -1)) {
            /* Have separate logfile per run. */
            gf_time_fmt(timestr, sizeof timestr, gf_time(), gf_timefmt_FT);
            sprintf(tmp_logfile, "%s.%s.%d", cmd_args->log_file, timestr,
                    getpid());

            /* Create symlink to actual log file */
            sys_unlink(cmd_args->log_file);

            tmp_logfile_dyn = gf_strdup(tmp_logfile);
            tmp_logfilebase = basename(tmp_logfile_dyn);
            ret = sys_symlink(tmp_logfilebase, cmd_args->log_file);
            if (ret == -1) {
                fprintf(stderr, "ERROR: symlink of logfile failed\n");
                goto out;
            }

            GF_FREE(cmd_args->log_file);
            cmd_args->log_file = gf_strdup(tmp_logfile);

            GF_FREE(tmp_logfile_dyn);
        }
    }

    /*
       This option was made obsolete but parsing it for backward
       compatibility with third party applications
     */
    if (cmd_args->max_connect_attempts) {
        gf_smsg("glusterfs", GF_LOG_WARNING, 0, glusterfsd_msg_33, NULL);
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
glusterfs_pidfile_setup(glusterfs_ctx_t *ctx)
{
    cmd_args_t *cmd_args = NULL;
    int ret = -1;
    FILE *pidfp = NULL;

    cmd_args = &ctx->cmd_args;

    if (!cmd_args->pid_file)
        return 0;

    pidfp = fopen(cmd_args->pid_file, "a+");
    if (!pidfp) {
        gf_smsg("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_17,
                "pidfile=%s", cmd_args->pid_file, NULL);
        goto out;
    }

    ctx->pidfp = pidfp;

    ret = 0;
out:

    return ret;
}

int
glusterfs_pidfile_cleanup(glusterfs_ctx_t *ctx)
{
    cmd_args_t *cmd_args = NULL;

    cmd_args = &ctx->cmd_args;

    if (!ctx->pidfp)
        return 0;

    gf_msg_trace("glusterfsd", 0, "pidfile %s cleanup", cmd_args->pid_file);

    if (ctx->cmd_args.pid_file) {
        GF_FREE(ctx->cmd_args.pid_file);
        ctx->cmd_args.pid_file = NULL;
    }

    lockf(fileno(ctx->pidfp), F_ULOCK, 0);
    fclose(ctx->pidfp);
    ctx->pidfp = NULL;

    return 0;
}

int
glusterfs_pidfile_update(glusterfs_ctx_t *ctx, pid_t pid)
{
    cmd_args_t *cmd_args = NULL;
    int ret = 0;
    FILE *pidfp = NULL;

    cmd_args = &ctx->cmd_args;

    pidfp = ctx->pidfp;
    if (!pidfp)
        return 0;

    ret = lockf(fileno(pidfp), F_TLOCK, 0);
    if (ret) {
        gf_smsg("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_18,
                "pidfile=%s", cmd_args->pid_file, NULL);
        return ret;
    }

    ret = sys_ftruncate(fileno(pidfp), 0);
    if (ret) {
        gf_smsg("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_20,
                "pidfile=%s", cmd_args->pid_file, NULL);
        return ret;
    }

    ret = fprintf(pidfp, "%d\n", pid);
    if (ret <= 0) {
        gf_smsg("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_21,
                "pidfile=%s", cmd_args->pid_file, NULL);
        return ret;
    }

    ret = fflush(pidfp);
    if (ret) {
        gf_smsg("glusterfsd", GF_LOG_ERROR, errno, glusterfsd_msg_21,
                "pidfile=%s", cmd_args->pid_file, NULL);
        return ret;
    }

    gf_msg_debug("glusterfsd", 0, "pidfile %s updated with pid %d",
                 cmd_args->pid_file, pid);

    return 0;
}

void *
glusterfs_sigwaiter(void *arg)
{
    sigset_t set;
    int ret = 0;
    int sig = 0;
    char *file = NULL;

    sigemptyset(&set);
    sigaddset(&set, SIGINT);  /* cleanup_and_exit */
    sigaddset(&set, SIGTERM); /* cleanup_and_exit */
    sigaddset(&set, SIGHUP);  /* reincarnate */
    sigaddset(&set, SIGUSR1); /* gf_proc_dump_info */
    sigaddset(&set, SIGUSR2);

    for (;;) {
        ret = sigwait(&set, &sig);
        if (ret)
            continue;

        switch (sig) {
            case SIGINT:
            case SIGTERM:
                cleanup_and_exit(sig);
                break;
            case SIGHUP:
                reincarnate(sig);
                break;
            case SIGUSR1:
                gf_proc_dump_info(sig, glusterfsd_ctx);
                break;
            case SIGUSR2:
                file = gf_monitor_metrics(glusterfsd_ctx);

                /* Nothing needed to be done here */
                GF_FREE(file);

                break;
            default:

                break;
        }
    }

    return NULL;
}

void
glusterfsd_print_trace(int signum)
{
    gf_print_trace(signum, glusterfsd_ctx);
}

int
glusterfs_signals_setup(glusterfs_ctx_t *ctx)
{
    sigset_t set;
    int ret = 0;

    sigemptyset(&set);

    /* common setting for all threads */
    signal(SIGSEGV, glusterfsd_print_trace);
    signal(SIGABRT, glusterfsd_print_trace);
    signal(SIGILL, glusterfsd_print_trace);
    signal(SIGTRAP, glusterfsd_print_trace);
    signal(SIGFPE, glusterfsd_print_trace);
    signal(SIGBUS, glusterfsd_print_trace);
    signal(SIGINT, cleanup_and_exit);
    signal(SIGPIPE, SIG_IGN);

    /* block these signals from non-sigwaiter threads */
    sigaddset(&set, SIGTERM); /* cleanup_and_exit */
    sigaddset(&set, SIGHUP);  /* reincarnate */
    sigaddset(&set, SIGUSR1); /* gf_proc_dump_info */
    sigaddset(&set, SIGUSR2);

    /* Signals needed for asynchronous framework. */
    sigaddset(&set, GF_ASYNC_SIGQUEUE);
    sigaddset(&set, GF_ASYNC_SIGCTRL);

    ret = pthread_sigmask(SIG_BLOCK, &set, NULL);
    if (ret) {
        gf_smsg("glusterfsd", GF_LOG_WARNING, errno, glusterfsd_msg_22, NULL);
        return ret;
    }

    ret = gf_thread_create(&ctx->sigwaiter, NULL, glusterfs_sigwaiter,
                           (void *)&set, "sigwait");
    if (ret) {
        /*
          TODO:
          fallback to signals getting handled by other threads.
          setup the signal handlers
        */
        gf_smsg("glusterfsd", GF_LOG_WARNING, errno, glusterfsd_msg_23, NULL);
        return ret;
    }

    return ret;
}

int
daemonize(glusterfs_ctx_t *ctx)
{
    int ret = -1;
    cmd_args_t *cmd_args = NULL;
    int cstatus = 0;
    int err = 1;
    int child_pid = 0;

    cmd_args = &ctx->cmd_args;

    ret = glusterfs_pidfile_setup(ctx);
    if (ret)
        goto out;

    if (cmd_args->no_daemon_mode) {
        goto postfork;
    }

    if (cmd_args->debug_mode)
        goto postfork;

    ret = pipe(ctx->daemon_pipe);
    if (ret) {
        /* If pipe() fails, retain daemon_pipe[] = {-1, -1}
           and parent will just not wait for child status
        */
        ctx->daemon_pipe[0] = -1;
        ctx->daemon_pipe[1] = -1;
    }

    ret = os_daemon_return(0, 0);
    switch (ret) {
        case -1:
            if (ctx->daemon_pipe[0] != -1) {
                sys_close(ctx->daemon_pipe[0]);
                sys_close(ctx->daemon_pipe[1]);
            }

            gf_smsg("daemonize", GF_LOG_ERROR, errno, glusterfsd_msg_24, NULL);
            goto out;
        case 0:
            /* child */
            /* close read */
            sys_close(ctx->daemon_pipe[0]);
            break;
        default:
            /* parent */
            /* close write */
            child_pid = ret;
            sys_close(ctx->daemon_pipe[1]);

            if (ctx->mnt_pid > 0) {
                ret = waitpid(ctx->mnt_pid, &cstatus, 0);
                if (!(ret == ctx->mnt_pid)) {
                    if (WIFEXITED(cstatus)) {
                        err = WEXITSTATUS(cstatus);
                    } else {
                        err = cstatus;
                    }
                    gf_smsg("daemonize", GF_LOG_ERROR, 0, glusterfsd_msg_25,
                            NULL);
                    exit(err);
                }
            }
            sys_read(ctx->daemon_pipe[0], (void *)&err, sizeof(err));
            /* NOTE: Only the least significant 8 bits i.e (err & 255)
               will be available to parent process on calling exit() */
            if (err)
                _exit(abs(err));

            /* Update pid in parent only for glusterd process */
            if (ctx->process_mode == GF_GLUSTERD_PROCESS) {
                ret = glusterfs_pidfile_update(ctx, child_pid);
                if (ret)
                    exit(1);
            }
            _exit(0);
    }

postfork:
    /* Update pid in child either process_mode is not belong to glusterd
       or process is spawned in no daemon mode
    */
    if ((ctx->process_mode != GF_GLUSTERD_PROCESS) ||
        (cmd_args->no_daemon_mode)) {
        ret = glusterfs_pidfile_update(ctx, getpid());
        if (ret)
            goto out;
    }
    gf_log("glusterfs", GF_LOG_INFO, "Pid of current running process is %d",
           getpid());
    ret = gf_log_inject_timer_event(ctx);

    glusterfs_signals_setup(ctx);
out:
    return ret;
}

#ifdef GF_LINUX_HOST_OS
static int
set_oom_score_adj(glusterfs_ctx_t *ctx)
{
    int ret = -1;
    cmd_args_t *cmd_args = NULL;
    int fd = -1;
    size_t oom_score_len = 0;
    struct oom_api_info *api = NULL;

    cmd_args = &ctx->cmd_args;

    if (!cmd_args->oom_score_adj)
        goto success;

    api = get_oom_api_info();
    if (!api)
        goto out;

    fd = open(api->oom_api_file, O_WRONLY);
    if (fd < 0)
        goto out;

    oom_score_len = strlen(cmd_args->oom_score_adj);
    if (sys_write(fd, cmd_args->oom_score_adj, oom_score_len) !=
        oom_score_len) {
        sys_close(fd);
        goto out;
    }

    if (sys_close(fd) < 0)
        goto out;

success:
    ret = 0;

out:
    return ret;
}
#endif

int
glusterfs_process_volfp(glusterfs_ctx_t *ctx, FILE *fp)
{
    glusterfs_graph_t *graph = NULL;
    int ret = -1;
    xlator_t *trav = NULL;

    if (!ctx)
        return -1;

    graph = glusterfs_graph_construct(fp);
    if (!graph) {
        gf_smsg("", GF_LOG_ERROR, 0, glusterfsd_msg_26, NULL);
        goto out;
    }

    for (trav = graph->first; trav; trav = trav->next) {
        if (strcmp(trav->type, "mount/fuse") == 0) {
            gf_smsg("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_27, NULL);
            goto out;
        }
    }

    xlator_t *xl = graph->first;
    if (xl && (strcmp(xl->type, "protocol/server") == 0)) {
        (void)copy_opts_to_child(xl, FIRST_CHILD(xl), "*auth*");
    }

    ret = glusterfs_graph_prepare(graph, ctx, ctx->cmd_args.volume_name);
    if (ret) {
        goto out;
    }

    ret = glusterfs_graph_activate(graph, ctx);

    if (ret) {
        goto out;
    }

    gf_log_dump_graph(fp, graph);

    ret = 0;
out:
    if (fp)
        fclose(fp);

    if (ret) {
        /* TODO This code makes to generic for all graphs
           client as well as servers.For now it destroys
           graph only for server-side xlators not for client-side
           xlators, before destroying a graph call xlator fini for
           xlators those call xlator_init to avoid leak
        */
        if (graph) {
            xl = graph->first;
            if ((ctx->active != graph) &&
                (xl && !strcmp(xl->type, "protocol/server"))) {
                /* Take dict ref for every graph xlator to avoid dict leak
                   at the time of graph destroying
                */
                glusterfs_graph_fini(graph);
                glusterfs_graph_destroy(graph);
            }
        }

        /* there is some error in setting up the first graph itself */
        if (!ctx->active) {
            emancipate(ctx, ret);
            cleanup_and_exit(ret);
        }
    }

    return ret;
}

int
glusterfs_volumes_init(glusterfs_ctx_t *ctx)
{
    FILE *fp = NULL;
    cmd_args_t *cmd_args = NULL;
    int ret = 0;

    cmd_args = &ctx->cmd_args;

    if (cmd_args->sock_file) {
        ret = glusterfs_listener_init(ctx);
        if (ret)
            goto out;
    }

    if (cmd_args->volfile_server) {
        ret = glusterfs_mgmt_init(ctx);
        /* return, do not emancipate() yet */
        return ret;
    }

    fp = get_volfp(ctx);

    if (!fp) {
        gf_smsg("glusterfsd", GF_LOG_ERROR, 0, glusterfsd_msg_28, NULL);
        ret = -1;
        goto out;
    }

    ret = glusterfs_process_volfp(ctx, fp);
    if (ret)
        goto out;

out:
    emancipate(ctx, ret);
    return ret;
}

/* This is the only legal global pointer  */
glusterfs_ctx_t *glusterfsd_ctx;

int
main(int argc, char *argv[])
{
    glusterfs_ctx_t *ctx = NULL;
    int ret = -1;
    char cmdlinestr[PATH_MAX] = {
        0,
    };
    cmd_args_t *cmd = NULL;

    gf_check_and_set_mem_acct(argc, argv);

    ctx = glusterfs_ctx_new();
    if (!ctx) {
        gf_smsg("glusterfs", GF_LOG_CRITICAL, 0, glusterfsd_msg_29, NULL);
        return ENOMEM;
    }
    glusterfsd_ctx = ctx;

    ret = glusterfs_globals_init(ctx);
    if (ret)
        return ret;

    THIS->ctx = ctx;

    ret = glusterfs_ctx_defaults_init(ctx);
    if (ret)
        goto out;

    ret = parse_cmdline(argc, argv, ctx);
    if (ret)
        goto out;
    cmd = &ctx->cmd_args;

    if (cmd->print_xlatordir) {
        /* XLATORDIR passed through a -D flag to GCC */
        printf("%s\n", XLATORDIR);
        goto out;
    }

    if (cmd->print_statedumpdir) {
        printf("%s\n", DEFAULT_VAR_RUN_DIRECTORY);
        goto out;
    }

    if (cmd->print_logdir) {
        printf("%s\n", DEFAULT_LOG_FILE_DIRECTORY);
        goto out;
    }

    if (cmd->print_libexecdir) {
        printf("%s\n", LIBEXECDIR);
        goto out;
    }

    if (cmd->print_netgroups) {
        /* If this option is set we want to print & verify the file,
         * set the return value (exit code in this case) and exit.
         */
        ret = print_netgroups_file(cmd->print_netgroups);
        goto out;
    }

    if (cmd->print_exports) {
        /* If this option is set we want to print & verify the file,
         * set the return value (exit code in this case)
         * and exit.
         */
        ret = print_exports_file(cmd->print_exports);
        goto out;
    }

    ret = logging_init(ctx, argv[0]);
    if (ret)
        goto out;

    /* set brick_mux mode only for server process */
    if ((ctx->process_mode != GF_SERVER_PROCESS) && cmd->brick_mux) {
        gf_smsg("glusterfs", GF_LOG_CRITICAL, 0, glusterfsd_msg_43, NULL);
        goto out;
    }

    /* log the version of glusterfs running here along with the actual
       command line options. */
    {
        int i = 0;
        int pos = 0;
        int len = snprintf(cmdlinestr, sizeof(cmdlinestr), "%s", argv[0]);
        for (i = 1; (i < argc) && (len > 0); i++) {
            pos += len;
            len = snprintf(cmdlinestr + pos, sizeof(cmdlinestr) - pos, " %s",
                           argv[i]);
            if ((len <= 0) || (len >= (sizeof(cmdlinestr) - pos))) {
                gf_smsg("glusterfs", GF_LOG_ERROR, 0, glusterfsd_msg_029, NULL);
                ret = -1;
                goto out;
            }
        }
        gf_smsg(argv[0], GF_LOG_INFO, 0, glusterfsd_msg_30, "arg=%s", argv[0],
                "version=%s", PACKAGE_VERSION, "cmdlinestr=%s", cmdlinestr,
                NULL);

        ctx->cmdlinestr = gf_strdup(cmdlinestr);
    }

    gf_proc_dump_init();

    ret = create_fuse_mount(ctx);
    if (ret)
        goto out;

    ret = daemonize(ctx);
    if (ret)
        goto out;

    /*
     * If we do this before daemonize, the pool-sweeper thread dies with
     * the parent, but we want to do it as soon as possible after that in
     * case something else depends on pool allocations.
     */
    mem_pools_init();

    ret = gf_async_init(ctx);
    if (ret < 0) {
        goto out;
    }

#ifdef GF_LINUX_HOST_OS
    ret = set_oom_score_adj(ctx);
    if (ret)
        goto out;
#endif

    ctx->env = syncenv_new(0, 0, 0);
    if (!ctx->env) {
        gf_smsg("", GF_LOG_ERROR, 0, glusterfsd_msg_31, NULL);
        goto out;
    }

    /* do this _after_ daemonize() */
    if (!glusterfs_ctx_tw_get(ctx)) {
        ret = -1;
        goto out;
    }

    ret = glusterfs_volumes_init(ctx);
    if (ret)
        goto out;

    ret = gf_event_dispatch(ctx->event_pool);

out:
    //    glusterfs_ctx_destroy (ctx);
    gf_async_fini();
    return ret;
}
