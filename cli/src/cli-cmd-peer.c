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

extern struct rpc_clnt *global_rpc;

extern rpc_clnt_prog_t *cli_rpc_prog;

int cli_cmd_peer_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                      const char **words, int wordcount);

int
cli_cmd_peer_probe_cbk (struct cli_state *state, struct cli_cmd_word *word,
                   const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *dict = NULL;
        int                     sent = 0;
        int                     parse_error = 0;

        if (!(wordcount == 3)) {
                cli_usage_out (word->pattern);
                parse_error = 1;
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

        ret = valid_internet_address ((char *) words[2]);
        if (ret == 1) {
                ret = 0;
        } else {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }
/*        if (words[3]) {
                ret = dict_set_str (dict, "port", (char *)words[3]);
                if (ret)
                        goto out;
        }
*/
        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Peer probe failed");
        }
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
        int                  sent = 0;
        int                  parse_error = 0;

        if (!(wordcount == 3) ) {
                cli_usage_out (word->pattern);
                parse_error = 1;
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

/*        if (words[3]) {
                ret = dict_set_str (dict, "port", (char *)words[3]);
                if (ret)
                        goto out;
        }
*/
        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Peer detach failed");
        }

        return ret;
}

int
cli_cmd_peer_status_cbk (struct cli_state *state, struct cli_cmd_word *word,
                        const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        int                     sent = 0;
        int                     parse_error = 0;

        if (wordcount != 2) {
                cli_usage_out (word->pattern);
                parse_error = 1;
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
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Peer status failed");
        }
        return ret;
}

struct cli_cmd cli_probe_cmds[] = {
        { "peer probe <HOSTNAME>",
          cli_cmd_peer_probe_cbk,
          "probe peer specified by <HOSTNAME>"},

        { "peer detach <HOSTNAME>",
          cli_cmd_peer_deprobe_cbk,
          "detach peer specified by <HOSTNAME>"},

        { "peer status",
          cli_cmd_peer_status_cbk,
          "list status of peers"},

	{ "peer help",
           cli_cmd_peer_help_cbk,
           "Help command for peer "},

        { NULL, NULL, NULL }
};

int
cli_cmd_peer_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                      const char **words, int wordcount)
{
        struct cli_cmd        *cmd = NULL;



        for (cmd = cli_probe_cmds; cmd->pattern; cmd++)
                cli_out ("%s - %s", cmd->pattern, cmd->desc);

        return 0;
}

int
cli_cmd_probe_register (struct cli_state *state)
{
        int  ret = 0;
        struct cli_cmd *cmd = NULL;

        for (cmd = cli_probe_cmds; cmd->pattern; cmd++) {
                ret = cli_cmd_register (&state->tree, cmd->pattern, cmd->cbk,
                                        cmd->desc);
                if (ret)
                        goto out;
        }
out:
        return ret;
}
