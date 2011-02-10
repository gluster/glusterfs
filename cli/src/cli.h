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

#ifndef __CLI_H__
#define __CLI_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "rpc-clnt.h"
#include "glusterfs.h"
#include "protocol-common.h"

#define DEFAULT_EVENT_POOL_SIZE            16384
#define CLI_GLUSTERD_PORT                  24007
#define CLI_DEFAULT_CONN_TIMEOUT             120
#define CLI_DEFAULT_CMD_TIMEOUT              120

enum argp_option_keys {
	ARGP_DEBUG_KEY = 133,
	ARGP_PORT_KEY = 'p',
};

#define GLUSTER_MODE_SCRIPT    (1 << 0)
#define GLUSTER_MODE_ERR_FATAL (1 << 1)

struct cli_state;
struct cli_cmd_word;
struct cli_cmd_tree;

typedef int (cli_cmd_cbk_t)(struct cli_state *state,
                            struct cli_cmd_word *word,
                            const char **words,
                            int wordcount);
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
};

struct cli_local {
        union {
                struct {
                        dict_t  *dict;
                } create_vol;

                struct {
                        char    *volname;
                        int     flags;
                } start_vol;

                struct {
                        char    *volname;
                        int     flags;
                } stop_vol;

                struct {
                        char    *volname;
                } delete_vol;

                struct {
                        char    *volname;
                        int      cmd;
                } defrag_vol;

                struct {
                        char    *volname;
                        dict_t  *dict;
                } replace_brick;

                struct {
                        char    *volname;
                        int     flags;
                } get_vol;
        } u;
};

typedef struct cli_local cli_local_t;

typedef ssize_t (*cli_serialize_t) (struct iovec outmsg, void *args);

extern struct cli_state *global_state; /* use only in readline callback */

int cli_cmd_register (struct cli_cmd_tree *tree, const char *template,
                      cli_cmd_cbk_t cbk, const char *desc);
int cli_cmds_register (struct cli_state *state);

int cli_input_init (struct cli_state *state);

int cli_cmd_process (struct cli_state *state, int argc, char *argv[]);
int cli_cmd_process_line (struct cli_state *state, const char *line);

int cli_rl_enable (struct cli_state *state);
int cli_rl_out (struct cli_state *state, const char *fmt, va_list ap);

int cli_usage_out (const char *usage);
int cli_out (const char *fmt, ...);

int
cli_submit_request (void *req, call_frame_t *frame,
                    rpc_clnt_prog_t *prog,
                    int procnum, struct iobref *iobref,
                    cli_serialize_t sfunc, xlator_t *this,
                    fop_cbk_fn_t cbkfn);

int32_t
cli_cmd_volume_create_parse (const char **words, int wordcount,
                             dict_t **options);

int32_t
cli_cmd_volume_reset_parse (const char **words, int wordcount, dict_t **opt);

int32_t
cli_cmd_gsync_set_parse (const char **words, int wordcount, dict_t **opt);

int32_t
cli_cmd_volume_set_parse (const char **words, int wordcount,
                          dict_t **options);

int32_t
cli_cmd_volume_add_brick_parse (const char **words, int wordcount,
                                dict_t **options);

int32_t
cli_cmd_volume_remove_brick_parse (const char **words, int wordcount,
                                   dict_t **options);

int32_t
cli_cmd_volume_replace_brick_parse (const char **words, int wordcount,
                                   dict_t **options);

int32_t
cli_cmd_log_rotate_parse (const char **words, int wordcount, dict_t **options);
int32_t
cli_cmd_log_locate_parse (const char **words, int wordcount, dict_t **options);
int32_t
cli_cmd_log_filename_parse (const char **words, int wordcount, dict_t **options);

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
void
cli_path_strip_trailing_slashes (char *path);
#endif /* __CLI_H__ */
