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

extern struct rpc_clnt *global_rpc;

extern rpc_clnt_prog_t *cli_rpc_prog;

int
cli_cmd_volume_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                      const char **words, int wordcount);

void
cli_cmd_volume_start_usage ()
{
        cli_out ("Usage: volume start <volname>");
}

int
cli_cmd_volume_info_cbk (struct cli_state *state, struct cli_cmd_word *word,
                         const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;

        proc = &cli_rpc_prog->proctable[GF1_CLI_GET_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if (proc->fn) {
                ret = proc->fn (frame, THIS, NULL);
        }

out:
        if (ret)
                cli_out ("Getting Volume information failed!");
        return ret;

}

void
cli_cmd_volume_create_usage ()
{
        cli_out ("usage: volume create <NEW-VOLNAME> "
                 "[stripe <COUNT>] [replica <COUNT>] <NEW-BRICK> ...");
}

int
cli_cmd_volume_create_cbk (struct cli_state *state, struct cli_cmd_word *word,
                           const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *options = NULL;

        proc = &cli_rpc_prog->proctable[GF1_CLI_CREATE_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_create_parse (words, wordcount, &options);

        if (ret) {
                printf ("Command Parsing failed, ");
                cli_cmd_volume_create_usage ();
                goto out;
        }

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                if (wordcount > 2) {
                        char *volname = (char *) words[2];
                        cli_out ("Creating Volume %s failed",volname );
                }
        }
        if (options)
                dict_destroy (options);

        return ret;
}


int
cli_cmd_volume_delete_cbk (struct cli_state *state, struct cli_cmd_word *word,
                           const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        char                    *volname = NULL;

        proc = &cli_rpc_prog->proctable[GF1_CLI_DELETE_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        //TODO: Build validation here
        volname = (char *)words[2];
        GF_ASSERT (volname);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, volname);
        }

out:
        if (ret)
                cli_out ("Deleting Volume %s failed", volname);

        return ret;
}


int
cli_cmd_volume_start_cbk (struct cli_state *state, struct cli_cmd_word *word,
                          const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        char                    *volname = NULL;


        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        //TODO: Build validation here
        if (wordcount < 3) {
               cli_cmd_volume_start_usage ();
               goto out;
        }

        volname = (char *)words[2];
        GF_ASSERT (volname);

        proc = &cli_rpc_prog->proctable[GF1_CLI_START_VOLUME];

        if (proc->fn) {
                ret = proc->fn (frame, THIS, volname);
        }

out:
        if (!proc && ret && volname)
                cli_out ("Starting Volume %s failed", volname);

        return ret;
}


int
cli_cmd_volume_stop_cbk (struct cli_state *state, struct cli_cmd_word *word,
                           const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        char                    *volname = NULL;


        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        //TODO: Build validation here
        volname = (char *)words[2];
        GF_ASSERT (volname);

        proc = &cli_rpc_prog->proctable[GF1_CLI_STOP_VOLUME];

        if (proc->fn) {
                ret = proc->fn (frame, THIS, volname);
        }

out:
        if (!proc && ret)
                cli_out ("Stopping Volume %s failed", volname);

        return ret;
}


int
cli_cmd_volume_rename_cbk (struct cli_state *state, struct cli_cmd_word *word,
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

        GF_ASSERT (words[2]);
        GF_ASSERT (words[3]);

        //TODO: Build validation here
        ret = dict_set_str (dict, "old-volname", (char *)words[2]);

        if (ret)
                goto out;

        ret = dict_set_str (dict, "new-volname", (char *)words[3]);

        if (ret)
                goto out;

        proc = &cli_rpc_prog->proctable[GF1_CLI_RENAME_VOLUME];

        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (!proc && ret) {
                char *volname = (char *) words[2];
                if (dict)
                        dict_destroy (dict);
                cli_out ("Renaming Volume %s failed", volname );
        }

        return ret;
}

void
cli_cmd_volume_defrag_usage ()
{
        cli_out ("Usage: volume rebalance <volname> <start|stop|status>");
}

int
cli_cmd_volume_defrag_cbk (struct cli_state *state, struct cli_cmd_word *word,
                           const char **words, int wordcount)
{
        int                   ret     = -1;
        rpc_clnt_procedure_t *proc    = NULL;
        call_frame_t         *frame   = NULL;
        dict_t               *dict = NULL;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        dict = dict_new ();
        if (!dict)
                goto out;

        GF_ASSERT (words[2]);

        if (!(words[3])) {
                cli_cmd_volume_defrag_usage();
                goto out;
        }
        //TODO: Build validation here
        ret = dict_set_str (dict, "volname", (char *)words[2]);
        if (ret)
                goto out;

        ret = dict_set_str (dict, "command", (char *)words[3]);
        if (ret)
                goto out;

        proc = &cli_rpc_prog->proctable[GF1_CLI_DEFRAG_VOLUME];

        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (!proc && ret) {
                if (dict)
                        dict_destroy (dict);

                cli_out ("Defrag of Volume %s failed", (char *)words[2]);
        }

        return 0;
}


int
cli_cmd_volume_set_cbk (struct cli_state *state, struct cli_cmd_word *word,
                        const char **words, int wordcount)
{
        cli_cmd_broadcast_response (0);
        return 0;
}


int
cli_cmd_volume_add_brick_cbk (struct cli_state *state,
                              struct cli_cmd_word *word, const char **words,
                              int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *options = NULL;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_add_brick_parse (words, wordcount, &options);

        if (ret)
                goto out;

        proc = &cli_rpc_prog->proctable[GF1_CLI_ADD_BRICK];

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (!proc && ret) {
                char *volname = (char *) words[2];
                cli_out ("Adding brick to Volume %s failed",volname );
        }
        return ret;
}


int
cli_cmd_volume_remove_brick_cbk (struct cli_state *state,
                                 struct cli_cmd_word *word, const char **words,
                                 int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *options = NULL;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_remove_brick_parse (words, wordcount, &options);

        if (ret)
                goto out;

        proc = &cli_rpc_prog->proctable[GF1_CLI_REMOVE_BRICK];

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (!proc && ret) {
                char *volname = (char *) words[2];
                cli_out ("Removing brick from Volume %s failed",volname );
        }
        return ret;

}




int
cli_cmd_volume_replace_brick_cbk (struct cli_state *state,
                                  struct cli_cmd_word *word,
                                  const char **words,
                                  int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *options = NULL;

        proc = &cli_rpc_prog->proctable[GF1_CLI_REPLACE_BRICK];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_replace_brick_parse (words, wordcount, &options);

        if (ret)
                goto out;

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                char *volname = (char *) words[2];
                cli_out ("Replacing brick from Volume %s failed",volname );
        }
        return ret;

}


int
cli_cmd_volume_set_transport_cbk (struct cli_state *state,
                                  struct cli_cmd_word *word,
                                  const char **words, int wordcount)
{
        cli_cmd_broadcast_response (0);
        return 0;
}


struct cli_cmd volume_cmds[] = {
        { "volume info [all|<VOLNAME>]",
          cli_cmd_volume_info_cbk,
          "list information of all volumes"},

        { "volume create <NEW-VOLNAME> [stripe <COUNT>] [replica <COUNT>] <NEW-BRICK> ...",
          cli_cmd_volume_create_cbk,
          "create a new volume of specified type with mentioned bricks"},

        { "volume delete <VOLNAME>",
          cli_cmd_volume_delete_cbk,
          "delete volume specified by <VOLNAME>"},

        { "volume start <VOLNAME>",
          cli_cmd_volume_start_cbk,
          "start volume specified by <VOLNAME>"},

        { "volume stop <VOLNAME>",
          cli_cmd_volume_stop_cbk,
          "stop volume specified by <VOLNAME>"},

        { "volume rename <VOLNAME> <NEW-VOLNAME>",
          cli_cmd_volume_rename_cbk,
          "rename volume <VOLNAME> to <NEW-VOLNAME>"},

        { "volume add-brick <VOLNAME> [(replica <COUNT>)|(stripe <COUNT>)] <NEW-BRICK> ...",
          cli_cmd_volume_add_brick_cbk,
          "add brick to volume <VOLNAME>"},

        { "volume remove-brick <VOLNAME> [(replica <COUNT>)|(stripe <COUNT>)] <BRICK> ...",
          cli_cmd_volume_remove_brick_cbk,
          "remove brick from volume <VOLNAME>"},

        { "volume rebalance <VOLNAME> start",
          cli_cmd_volume_defrag_cbk,
          "start rebalance of volume <VOLNAME>"},

        { "volume rebalance <VOLNAME> stop",
          cli_cmd_volume_defrag_cbk,
          "stop rebalance of volume <VOLNAME>"},

        { "volume rebalance <VOLNAME> status",
          cli_cmd_volume_defrag_cbk,
          "rebalance status of volume <VOLNAME>"},

        { "volume replace-brick <VOLNAME> (<BRICK> <NEW-BRICK>)|pause|abort|start|status",
          cli_cmd_volume_replace_brick_cbk,
          "replace-brick operations"},

        { "volume set-transport <VOLNAME> <TRANSPORT-TYPE> [<TRANSPORT-TYPE>] ...",
          cli_cmd_volume_set_transport_cbk,
          "set transport type for volume <VOLNAME>"},

        { "volume set <VOLNAME> <KEY> <VALUE>",
          cli_cmd_volume_set_cbk,
         "set options for volume <VOLNAME>"},

        { "volume --help",
          cli_cmd_volume_help_cbk,
          "display help for the volume command"},


        { NULL, NULL, NULL }
};

int
cli_cmd_volume_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                      const char **words, int wordcount)
{
        struct cli_cmd        *cmd = NULL;

        for (cmd = volume_cmds; cmd->pattern; cmd++)
                cli_out ("%s - %s", cmd->pattern, cmd->desc);

        
        if (!state->rl_enabled)
                exit (0);

        return 0;
}

int
cli_cmd_volume_register (struct cli_state *state)
{
        int  ret = 0;
        struct cli_cmd *cmd = NULL;

        for (cmd = volume_cmds; cmd->pattern; cmd++) {
                ret = cli_cmd_register (&state->tree, cmd->pattern, cmd->cbk,
                                        cmd->desc);
                if (ret)
                        goto out;
        }
out:
        return ret;
}
