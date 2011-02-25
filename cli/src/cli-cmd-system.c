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
#include <stdint.h>
#include <pthread.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "cli.h"
#include "cli-cmd.h"
#include "cli-mem-types.h"
#include "protocol-common.h"

#define GETSPEC_SYNTAX "system:: getspec <VOLID>"
#define BRICKTOPORT_SYNTAX "system:: portmap brick2port <BRICK>"

extern struct rpc_clnt *global_rpc;

extern rpc_clnt_prog_t *cli_rpc_prog;

int cli_cmd_system_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                             const char **words, int wordcount);

int
cli_cmd_getspec_cbk (struct cli_state *state, struct cli_cmd_word *word,
                     const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *dict = NULL;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        dict = dict_new ();
        if (!dict)
                goto out;

        if (wordcount != 3) {
                cli_out ("Usage: " GETSPEC_SYNTAX);
                goto out;
        }

        ret = dict_set_str (dict, "volid", (char *)words[2]);
        if (ret)
                goto out;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_GETSPEC];
        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (!proc && ret) {
                if (dict)
                        dict_destroy (dict);
                if (wordcount > 1)
                        cli_out ("Fetching spec for volume %s failed",
                                 (char *)words[2]);
        }

        return ret;
}

int
cli_cmd_pmap_b2p_cbk (struct cli_state *state, struct cli_cmd_word *word,
                 const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *dict = NULL;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        dict = dict_new ();
        if (!dict)
                goto out;

        if (wordcount != 4) {
                cli_out ("Usage: " BRICKTOPORT_SYNTAX);
                goto out;
        }

        ret = dict_set_str (dict, "brick", (char *)words[3]);
        if (ret)
                goto out;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_PMAP_PORTBYBRICK];
        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (!proc && ret) {
                if (dict)
                        dict_destroy (dict);
                if (wordcount > 1)
                        cli_out ("Fetching spec for volume %s failed",
                                 (char *)words[3]);
        }

        return ret;
}

int
cli_cmd_fsm_log (struct cli_state *state, struct cli_cmd_word *word,
                 const char **words, int wordcount)
{
        int                             ret = -1;
        rpc_clnt_procedure_t            *proc = NULL;
        call_frame_t                    *frame = NULL;
        char                            *name = "";

        if ((wordcount != 3) && (wordcount != 2)) {
                cli_usage_out (word->pattern);
                goto out;
        }

        if (wordcount == 3)
                name = (char*)words[2];
        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_FSM_LOG];
        if (proc && proc->fn) {
                frame = create_frame (THIS, THIS->ctx->pool);
                if (!frame)
                        goto out;
                ret = proc->fn (frame, THIS, (void*)name);
        }
out:
        return ret;
}

struct cli_cmd cli_system_cmds[] = {
        { GETSPEC_SYNTAX,
          cli_cmd_getspec_cbk,
          "fetch spec for volume <VOLID>"},

        { BRICKTOPORT_SYNTAX,
          cli_cmd_pmap_b2p_cbk,
          "query which port <BRICK> listens on"},

        { "system:: help",
           cli_cmd_system_help_cbk,
           "display help for system commands"},

        { "fsm log [<peer-name>]",
           cli_cmd_fsm_log,
           "display fsm transitions"},

        { NULL, NULL, NULL }
};

int
cli_cmd_system_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                         const char **words, int wordcount)
{
        struct cli_cmd *cmd = NULL;

        for (cmd = cli_system_cmds; cmd->pattern; cmd++)
                cli_out ("%s - %s", cmd->pattern, cmd->desc);

        return 0;
}

int
cli_cmd_system_register (struct cli_state *state)
{
        int  ret = 0;
        struct cli_cmd *cmd = NULL;

        for (cmd = cli_system_cmds; cmd->pattern; cmd++) {
                ret = cli_cmd_register (&state->tree, cmd->pattern, cmd->cbk,
                                        cmd->desc);
                if (ret)
                        goto out;
        }
out:
        return ret;
}
