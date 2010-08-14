/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
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

extern struct rpc_clnt *global_rpc;

extern rpc_clnt_prog_t *cli_rpc_prog;

void
cli_cmd_probe_usage ()
{
        cli_out ("Usage: probe <hostname> [port]");
}

void
cli_cmd_deprobe_usage ()
{
        cli_out ("Usage: detach <hostname> [port]");
}

void
cli_cmd_peer_status_usage ()
{
        cli_out ("Usage: peer status <hostname> [port]");
}

int
cli_cmd_peer_probe_cbk (struct cli_state *state, struct cli_cmd_word *word,
                   const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *dict = NULL;

        if (!((wordcount == 4) || (wordcount == 3))) {
                cli_cmd_probe_usage ();
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GF1_CLI_PROBE];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_str (dict, "hostname", (char *)words[2]);
        if (ret)
                goto out;

        if (words[3]) {
                ret = dict_set_str (dict, "port", (char *)words[3]);
                if (ret)
                        goto out;
        }

        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (ret)
                cli_out ("Probe failed");
        return ret;
}


int
cli_cmd_peer_deprobe_cbk (struct cli_state *state, struct cli_cmd_word *word,
                          const char **words, int wordcount)
{
        int                   ret   = -1;
        rpc_clnt_procedure_t *proc  = NULL;
        call_frame_t         *frame = NULL;
        dict_t               *dict  = NULL;

        if (!((wordcount == 3) || (wordcount == 4))) {
                cli_cmd_deprobe_usage ();
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GF1_CLI_DEPROBE];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        dict = dict_new ();

        ret = dict_set_str (dict, "hostname", (char *)words[2]);
        if (ret)
                goto out;

        if (words[3]) {
                ret = dict_set_str (dict, "port", (char *)words[3]);
                if (ret)
                        goto out;
        }

        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (ret)
                cli_out ("Detach failed");

        return ret;
}

int
cli_cmd_peer_status_cbk (struct cli_state *state, struct cli_cmd_word *word,
                        const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;

        if (wordcount != 2) {
                cli_cmd_peer_status_usage ();
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GF1_CLI_LIST_FRIENDS];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if (proc->fn) {
                ret = proc->fn (frame, THIS, (char *)words[1] );
        }

out:
        if (ret)
                cli_out ("Command Execution failed");
        return ret;
}

struct cli_cmd cli_probe_cmds[] = {
        { "peer probe <HOSTNAME> [PORT]",
          cli_cmd_peer_probe_cbk },

        { "peer detach <HOSTNAME>",
          cli_cmd_peer_deprobe_cbk },

        { "peer status",
          cli_cmd_peer_status_cbk},

        { NULL, NULL }
};


int
cli_cmd_probe_register (struct cli_state *state)
{
        int  ret = 0;
        struct cli_cmd *cmd = NULL;

        for (cmd = cli_probe_cmds; cmd->pattern; cmd++) {
                ret = cli_cmd_register (&state->tree, cmd->pattern, cmd->cbk);
                if (ret)
                        goto out;
        }
out:
        return ret;
}
