/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __CLI_CMD_H__
#define __CLI_CMD_H__

#include <netdb.h>

#include "cli.h"
#include "list.h"

#define GLUSTER_SHARED_STORAGE      "gluster_shared_storage"

#define CLI_LOCAL_INIT(local, words, frame, dictionary) \
        do {                                                 \
                local = cli_local_get ();                    \
                                                             \
                if (local) {                                 \
                        local->words = words;                \
                        if (dictionary)                      \
                                local->dict = dictionary;    \
                        if (frame)                           \
                                frame->local = local;        \
                }                                            \
        } while (0)

#define CLI_STACK_DESTROY(_frame)                                       \
        do {                                                            \
                if (_frame) {                                           \
                        if (_frame->local) {                            \
                                gf_log ("cli", GF_LOG_DEBUG, "frame->local " \
                                        "is not NULL (%p)", _frame->local); \
                                cli_local_wipe (_frame->local);         \
                                _frame->local = NULL;                   \
                        }                                               \
                        STACK_DESTROY (_frame->root);                   \
                }                                                       \
        } while (0);

typedef enum {
        GF_ANSWER_YES = 1,
        GF_ANSWER_NO  = 2
} gf_answer_t;

struct cli_cmd {
        const char         *pattern;
        cli_cmd_cbk_t      *cbk;
        const char         *desc;
        cli_cmd_reg_cbk_t  *reg_cbk; /* callback to check in runtime if the   *
                                      * command should be enabled or disabled */
        gf_boolean_t       disable;
};

struct cli_cmd_volume_get_ctx_ {
        char            *volname;
        int             flags;
};

typedef struct cli_profile_info_ {
        uint64_t fop_hits;
        double min_latency;
        double max_latency;
        double avg_latency;
        char   *fop_name;
        double percentage_avg_latency;
} cli_profile_info_t;

typedef struct cli_cmd_volume_get_ctx_ cli_cmd_volume_get_ctx_t;

int cli_cmd_volume_register (struct cli_state *state);

int cli_cmd_probe_register (struct cli_state *state);

int cli_cmd_system_register (struct cli_state *state);

int cli_cmd_snapshot_register (struct cli_state *state);

int cli_cmd_global_register (struct cli_state *state);

int cli_cmd_misc_register (struct cli_state *state);

struct cli_cmd_word *cli_cmd_nextword (struct cli_cmd_word *word,
                                       const char *text);
void cli_cmd_tokens_destroy (char **tokens);

int cli_cmd_await_response (unsigned time);

int cli_cmd_broadcast_response (int32_t status);

int cli_cmd_cond_init ();

int cli_cmd_lock ();

int cli_cmd_unlock ();

int
cli_cmd_submit (struct rpc_clnt *rpc, void *req, call_frame_t *frame,
                rpc_clnt_prog_t *prog,
                int procnum, struct iobref *iobref,
                xlator_t *this, fop_cbk_fn_t cbkfn, xdrproc_t xdrproc);

int cli_cmd_pattern_cmp (void *a, void *b);

void cli_cmd_sort (struct cli_cmd *cmd, int count);

gf_answer_t
cli_cmd_get_confirmation (struct cli_state *state, const char *question);
int cli_cmd_sent_status_get (int *status);

gf_boolean_t
_limits_set_on_volume (char *volname, int type);

#endif /* __CLI_CMD_H__ */
