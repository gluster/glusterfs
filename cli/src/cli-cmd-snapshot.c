/*
   Copyright (c) 2013-2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
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

extern rpc_clnt_prog_t *cli_rpc_prog;

int
cli_cmd_snapshot_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                           const char **words, int wordcount);

int
cli_cmd_snapshot_cbk (struct cli_state *state, struct cli_cmd_word *word,
                      const char **words, int wordcount)
{
        int                      ret       = 0;
        int                      parse_err = 0;
        dict_t                  *options   = NULL;
        rpc_clnt_procedure_t    *proc      = NULL;
        call_frame_t            *frame     = NULL;
        cli_local_t             *local     = NULL;

        proc = &cli_rpc_prog->proctable [GLUSTER_CLI_SNAP];
        if (proc == NULL) {
               ret = -1;
                goto out;
        }

        frame = create_frame (THIS, THIS->ctx->pool);
        if (frame == NULL) {
                ret = -1;
                goto out;
        }

        /* Parses the command entered by the user */
        ret = cli_cmd_snapshot_parse (words, wordcount, &options, state);
        if (ret) {
                if (ret < 0) {
                        cli_usage_out (word->pattern);
                        parse_err = 1;
                } else {
                        /* User might have cancelled the snapshot operation */
                        ret = 0;
                }
                goto out;
        }

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn)
                ret = proc->fn (frame, THIS, options);

out:
        if (ret && parse_err == 0)
                cli_out ("Snapshot command failed");

        CLI_STACK_DESTROY (frame);

        return ret;
}

struct cli_cmd snapshot_cmds[] = {
        { "snapshot help",
          cli_cmd_snapshot_help_cbk,
          "display help for snapshot commands"
        },
        { "snapshot create <snapname> <volname(s)> [description <description>] [force]",
          cli_cmd_snapshot_cbk,
          "Snapshot Create."
        },
        { "snapshot restore <snapname>",
          cli_cmd_snapshot_cbk,
          "Snapshot Restore."
        },
        { "snapshot status [(snapname | volume <volname>)]",
          cli_cmd_snapshot_cbk,
          "Snapshot Status."
        },
        { "snapshot info [(snapname | volume <volname>)]",
          cli_cmd_snapshot_cbk,
          "Snapshot Info."
        },
        { "snapshot list [volname]",
          cli_cmd_snapshot_cbk,
          "Snapshot List."
        },
        {"snapshot config [volname] ([snap-max-hard-limit <count>] "
         "[snap-max-soft-limit <percent>]) "
         "| ([auto-delete <enable|disable>])",
          cli_cmd_snapshot_cbk,
          "Snapshot Config."
        },
        {"snapshot delete (all | snapname | volume <volname>)",
          cli_cmd_snapshot_cbk,
          "Snapshot Delete."
        },
        {"snapshot activate <snapname> [force]",
          cli_cmd_snapshot_cbk,
          "Activate snapshot volume."
        },
        {"snapshot deactivate <snapname>",
          cli_cmd_snapshot_cbk,
          "Deactivate snapshot volume."
        },
        { NULL, NULL, NULL }
};

int
cli_cmd_snapshot_help_cbk (struct cli_state *state,
                           struct cli_cmd_word *in_word,
                           const char **words,
                           int wordcount)
{
        struct cli_cmd        *cmd = NULL;

        for (cmd = snapshot_cmds; cmd->pattern; cmd++)
                if (_gf_false == cmd->disable)
                        cli_out ("%s - %s", cmd->pattern, cmd->desc);

        return 0;
}

int
cli_cmd_snapshot_register (struct cli_state *state)
{
        int  ret = 0;
        struct cli_cmd *cmd = NULL;

        for (cmd = snapshot_cmds; cmd->pattern; cmd++) {

                ret = cli_cmd_register (&state->tree, cmd);
                if (ret)
                        goto out;
        }
out:
        return ret;
}
