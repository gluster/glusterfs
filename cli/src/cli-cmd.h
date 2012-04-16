/*
   Copyright (c) 2006-2011 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#ifndef __CLI_CMD_H__
#define __CLI_CMD_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <netdb.h>

#include "cli.h"
#include "list.h"

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

typedef struct addrinfo_list {
        struct list_head list;
        struct addrinfo *info;
} addrinfo_list_t;

typedef enum {
        GF_AI_COMPARE_NO_MATCH     = 0,
        GF_AI_COMPARE_MATCH        = 1,
        GF_AI_COMPARE_ERROR        = 2
} gf_ai_compare_t;

typedef struct cli_cmd_volume_get_ctx_ cli_cmd_volume_get_ctx_t;

int cli_cmd_volume_register (struct cli_state *state);

int cli_cmd_probe_register (struct cli_state *state);

int cli_cmd_system_register (struct cli_state *state);

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
cli_cmd_submit (void *req, call_frame_t *frame,
                rpc_clnt_prog_t *prog,
                int procnum, struct iobref *iobref,
                xlator_t *this, fop_cbk_fn_t cbkfn, xdrproc_t xdrproc);

gf_answer_t
cli_cmd_get_confirmation (struct cli_state *state, const char *question);
int cli_cmd_sent_status_get (int *status);
#endif /* __CLI_CMD_H__ */
