/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __CLI_H__
#define __CLI_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "rpc-clnt.h"
#include "glusterfs.h"
#include "protocol-common.h"
#include "logging.h"

#include "cli1-xdr.h"

#if (HAVE_LIB_XML)
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>
#endif

#define DEFAULT_EVENT_POOL_SIZE            16384
#define CLI_GLUSTERD_PORT                  24007
#define CLI_DEFAULT_CONN_TIMEOUT             120
#define CLI_DEFAULT_CMD_TIMEOUT              120
#define CLI_TEN_MINUTES_TIMEOUT              600 //Longer timeout for volume top
#define DEFAULT_CLI_LOG_FILE_DIRECTORY     DATADIR "/log/glusterfs"
#define CLI_VOL_STATUS_BRICK_LEN              55
#define CLI_TAB_LENGTH                         8
#define CLI_BRICK_STATUS_LINE_LEN             78

enum argp_option_keys {
	ARGP_DEBUG_KEY = 133,
	ARGP_PORT_KEY = 'p',
};

#define GLUSTER_MODE_SCRIPT    (1 << 0)
#define GLUSTER_MODE_ERR_FATAL (1 << 1)
#define GLUSTER_MODE_XML       (1 << 2)


#define GLUSTERD_GET_QUOTA_AUX_MOUNT_PATH(abspath, volname, path)      \
        snprintf (abspath, sizeof (abspath)-1,                          \
                  DEFAULT_VAR_RUN_DIRECTORY"/%s%s", volname, path);

#define GLUSTERFS_GET_AUX_MOUNT_PIDFILE(pidfile,volname) {               \
                snprintf (pidfile, PATH_MAX-1,                             \
                          DEFAULT_VAR_RUN_DIRECTORY"/%s.pid", volname);  \
        }

struct cli_state;
struct cli_cmd_word;
struct cli_cmd_tree;
struct cli_cmd;

extern char *cli_vol_type_str[];
extern char *cli_vol_status_str[];
extern char *cli_vol_task_status_str[];

typedef int (cli_cmd_cbk_t)(struct cli_state *state,
                            struct cli_cmd_word *word,
                            const char **words,
                            int wordcount);
typedef void (cli_cmd_reg_cbk_t)( struct cli_cmd *this);

typedef int (cli_cmd_match_t)(struct cli_cmd_word *word);
typedef int (cli_cmd_filler_t)(struct cli_cmd_word *word);

struct cli_cmd_word {
        struct cli_cmd_tree   *tree;
        const char            *word;
        cli_cmd_filler_t      *filler;
        cli_cmd_match_t       *match;
        cli_cmd_cbk_t         *cbkfn;
        const char            *desc;
        const char            *pattern;
        int                    nextwords_cnt;
        struct cli_cmd_word  **nextwords;
};


struct cli_cmd_tree {
        struct cli_state      *state;
        struct cli_cmd_word    root;
};


struct cli_state {
        int                   argc;
        char                **argv;

        char                  debug;

        /* for events dispatching */
        glusterfs_ctx_t      *ctx;

        /* registry of known commands */
        struct cli_cmd_tree   tree;

        /* the thread which "executes" the command in non-interactive mode */
        /* also the thread which reads from stdin in non-readline mode */
        pthread_t             input;

        /* terminal I/O */
        const char           *prompt;
        int                   rl_enabled;
        int                   rl_async;
        int                   rl_processing;

        /* autocompletion state */
        char                **matches;
        char                **matchesp;

        char                 *remote_host;
        int                   remote_port;
        int                   mode;
        int                   await_connected;

        char                 *log_file;
        gf_loglevel_t         log_level;

        char                 *glusterd_sock;
};

struct cli_local {
        struct {
                char    *volname;
                int     flags;
        } get_vol;

        dict_t          *dict;
        const char      **words;
        /* Marker for volume status all */
        gf_boolean_t    all;
#if (HAVE_LIB_XML)
        xmlTextWriterPtr        writer;
        xmlDocPtr               doc;
        int                     vol_count;
#endif
};

struct cli_volume_status {
        int            port;
        int            online;
        uint64_t       block_size;
        uint64_t       total_inodes;
        uint64_t       free_inodes;
        char          *brick;
        char          *pid_str;
        char          *free;
        char          *total;
#ifdef GF_LINUX_HOST_OS
        char          *fs_name;
        char          *mount_options;
        char          *device;
        char          *inode_size;
#endif
};

typedef struct cli_volume_status cli_volume_status_t;

typedef struct cli_local cli_local_t;

typedef ssize_t (*cli_serialize_t) (struct iovec outmsg, void *args);

extern struct cli_state *global_state; /* use only in readline callback */

typedef const char *(*cli_selector_t) (void *wcon);

void *cli_getunamb (const char *tok, void **choices, cli_selector_t sel);

int cli_cmd_register (struct cli_cmd_tree *tree, struct cli_cmd *cmd);
int cli_cmds_register (struct cli_state *state);

int cli_input_init (struct cli_state *state);

int cli_cmd_process (struct cli_state *state, int argc, char *argv[]);
int cli_cmd_process_line (struct cli_state *state, const char *line);

int cli_rl_enable (struct cli_state *state);
int cli_rl_out (struct cli_state *state, const char *fmt, va_list ap);
int cli_rl_err (struct cli_state *state, const char *fmt, va_list ap);

int cli_usage_out (const char *usage);

int _cli_out (const char *fmt, ...);
int _cli_err (const char *fmt, ...);

#define cli_out(fmt...) do {                       \
                FMT_WARN (fmt);                    \
                                                   \
                _cli_out(fmt);                     \
                                                   \
        } while (0)

#define cli_err(fmt...) do {                       \
                FMT_WARN (fmt);                    \
                                                   \
                _cli_err(fmt);                     \
                                                   \
        } while (0)

int
cli_submit_request (struct rpc_clnt *rpc, void *req, call_frame_t *frame,
                    rpc_clnt_prog_t *prog,
                    int procnum, struct iobref *iobref,
                    xlator_t *this, fop_cbk_fn_t cbkfn, xdrproc_t xdrproc);

int32_t
cli_cmd_volume_create_parse (const char **words, int wordcount,
                             dict_t **options);

int32_t
cli_cmd_volume_reset_parse (const char **words, int wordcount, dict_t **opt);

int32_t
cli_cmd_gsync_set_parse (const char **words, int wordcount, dict_t **opt);

int32_t
cli_cmd_quota_parse (const char **words, int wordcount, dict_t **opt);

int32_t
cli_cmd_volume_set_parse (const char **words, int wordcount,
                          dict_t **options, char **op_errstr);

int32_t
cli_cmd_volume_add_brick_parse (const char **words, int wordcount,
                                dict_t **options);

int32_t
cli_cmd_volume_remove_brick_parse (const char **words, int wordcount,
                                   dict_t **options, int *question);

int32_t
cli_cmd_volume_replace_brick_parse (const char **words, int wordcount,
                                   dict_t **options);

int32_t
cli_cmd_log_rotate_parse (const char **words, int wordcount, dict_t **options);
int32_t
cli_cmd_log_locate_parse (const char **words, int wordcount, dict_t **options);
int32_t
cli_cmd_log_filename_parse (const char **words, int wordcount, dict_t **options);

int32_t
cli_cmd_volume_statedump_options_parse (const char **words, int wordcount,
                                        dict_t **options);
int32_t
cli_cmd_volume_clrlks_opts_parse (const char **words, int wordcount,
                                  dict_t **options);

cli_local_t * cli_local_get ();

void
cli_local_wipe (cli_local_t *local);

int32_t
cli_cmd_await_connected ();

int32_t
cli_cmd_broadcast_connected ();

int
cli_rpc_notify (struct rpc_clnt *rpc, void *mydata, rpc_clnt_event_t event,
                void *data);

int32_t
cli_cmd_volume_profile_parse (const char **words, int wordcount,
                              dict_t **options);
int32_t
cli_cmd_volume_top_parse (const char **words, int wordcount,
                              dict_t **options);

int32_t
cli_cmd_log_level_parse (const char **words, int wordcount,
                         dict_t **options);

int32_t
cli_cmd_volume_status_parse (const char **words, int wordcount,
                             dict_t **options);

int
cli_cmd_volume_heal_options_parse (const char **words, int wordcount,
                                   dict_t **options);

int
cli_cmd_volume_defrag_parse (const char **words, int wordcount,
                             dict_t **options);

int
cli_print_brick_status (cli_volume_status_t *status);

void
cli_print_detailed_status (cli_volume_status_t *status);

int
cli_get_detail_status (dict_t *dict, int i, cli_volume_status_t *status);

void
cli_print_line (int len);

int
cli_xml_output_str (char *op, char *str, int op_ret, int op_errno,
                    char *op_errstr);

int
cli_xml_output_dict (char *op, dict_t *dict, int op_ret, int op_errno,
                     char *op_errstr);

int
cli_xml_output_vol_top (dict_t *dict, int op_ret, int op_errno,
                        char *op_errstr);

int
cli_xml_output_vol_profile (dict_t *dict, int op_ret, int op_errno,
                            char *op_errstr);

int
cli_xml_output_vol_status_begin (cli_local_t *local, int op_ret, int op_errno,
                                 char *op_errstr);

int
cli_xml_output_vol_status_end (cli_local_t *local);

int
cli_xml_output_vol_status (cli_local_t *local, dict_t *dict);

int
cli_xml_output_vol_list (dict_t *dict, int op_ret, int op_errno,
                         char *op_errstr);

int
cli_xml_output_vol_info_begin (cli_local_t *local, int op_ret, int op_errno,
                               char *op_errstr);

int
cli_xml_output_vol_info_end (cli_local_t *local);

int
cli_xml_output_vol_info (cli_local_t *local, dict_t *dict);

int
cli_xml_output_vol_quota_limit_list (char *volname, char *limit_list,
                                      int op_ret, int op_errno,
                                      char *op_errstr);

int
cli_xml_output_peer_status (dict_t *dict, int op_ret, int op_errno,
                            char *op_errstr);

int
cli_xml_output_vol_rebalance (gf_cli_defrag_type op, dict_t *dict, int op_ret,
                              int op_errno, char *op_errstr);

int
cli_xml_output_vol_remove_brick (gf_boolean_t status_op, dict_t *dict,
                                 int op_ret, int op_errno, char *op_errstr);

int
cli_xml_output_vol_replace_brick (gf1_cli_replace_op op, dict_t *dict,
                                  int op_ret, int op_errno, char *op_errstr);

int
cli_xml_output_vol_create (dict_t *dict, int op_ret, int op_errno,
                           char *op_errstr);

int
cli_xml_output_generic_volume (char *op, dict_t *dict, int op_ret, int op_errno,
                               char *op_errstr);

int
cli_xml_output_vol_gsync (dict_t *dict, int op_ret, int op_errno,
                          char *op_errstr);
int
cli_xml_output_vol_status_tasks_detail (cli_local_t *local, dict_t *dict);

char *
is_server_debug_xlator (void *myframe);
#endif /* __CLI_H__ */
