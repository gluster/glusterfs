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

extern struct rpc_clnt *global_rpc;

extern rpc_clnt_prog_t *cli_rpc_prog;

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

        if (ret)
                goto out;

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                char *volname = (char *) words[2];
                cli_out ("Creating Volume %s failed",volname );
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

        proc = &cli_rpc_prog->proctable[GF1_CLI_START_VOLUME];

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

        if (proc->fn) {
                ret = proc->fn (frame, THIS, volname);
        }

out:
        if (ret && volname)
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

        proc = &cli_rpc_prog->proctable[GF1_CLI_STOP_VOLUME];

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

        proc = &cli_rpc_prog->proctable[GF1_CLI_RENAME_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        dict = dict_new ();

        if (dict)
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

        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (ret) {
                char *volname = (char *) words[2];
                if (dict)
                        dict_destroy (dict);
                cli_out ("Renaming Volume %s failed", volname );
        }

        return ret;
}


int
cli_cmd_volume_defrag_cbk (struct cli_state *state, struct cli_cmd_word *word,
                           const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        char                    *volname = NULL;

        proc = &cli_rpc_prog->proctable[GF1_CLI_DEFRAG_VOLUME];

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
                cli_out ("Defrag of Volume %s failed", volname);

        return 0;
}


int
cli_cmd_volume_set_cbk (struct cli_state *state, struct cli_cmd_word *word,
                        const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        char                    *volname = NULL;
        dict_t                  *dict = NULL;

        proc = &cli_rpc_prog->proctable[GF1_CLI_SET_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        volname = (char *)words[2];
        GF_ASSERT (volname);

        GF_ASSERT (words[3]);

        ret = cli_cmd_volume_set_parse (words, wordcount, &dict);

        if (ret)
                goto out;

        //TODO: Build validation here
        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (ret) {
                if (dict)
                        dict_destroy (dict);
                cli_out ("Changing option on Volume %s failed", volname);
        }

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

        proc = &cli_rpc_prog->proctable[GF1_CLI_ADD_BRICK];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_add_brick_parse (words, wordcount, &options);

        if (ret)
                goto out;

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
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

        proc = &cli_rpc_prog->proctable[GF1_CLI_REMOVE_BRICK];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_remove_brick_parse (words, wordcount, &options);

        if (ret)
                goto out;

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
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
        cli_out ("volume set-transport not implemented");
        return 0;
}


struct cli_cmd volume_cmds[] = {
        { "volume info [all|<VOLNAME>]",
          cli_cmd_volume_info_cbk },

        { "volume create <NEW-VOLNAME> [stripe <COUNT>] [replicate <COUNT>] <NEW-BRICK> ...",
          cli_cmd_volume_create_cbk },

        { "volume delete <VOLNAME>",
          cli_cmd_volume_delete_cbk },

        { "volume start <VOLNAME>",
          cli_cmd_volume_start_cbk },

        { "volume stop <VOLNAME>",
          cli_cmd_volume_stop_cbk },

        { "volume rename <VOLNAME> <NEW-VOLNAME>",
          cli_cmd_volume_rename_cbk },

        { "volume add-brick <VOLNAME> [(replica <COUNT>)|(stripe <COUNT>)] <NEW-BRICK> ...",
          cli_cmd_volume_add_brick_cbk },

        { "volume remove-brick <VOLNAME> [(replica <COUNT>)|(stripe <COUNT>)] <BRICK> ...",
          cli_cmd_volume_remove_brick_cbk },

        { "volume defrag <VOLNAME>",
          cli_cmd_volume_defrag_cbk },

        { "volume replace-brick <VOLNAME> (<BRICK> <NEW-BRICK>)|pause|abort|start|status",
          cli_cmd_volume_replace_brick_cbk },

        { "volume set-transport <VOLNAME> <TRANSPORT-TYPE> [<TRANSPORT-TYPE>] ...",
          cli_cmd_volume_set_transport_cbk },

        { "volume set <VOLNAME> <KEY> <VALUE>",
          cli_cmd_volume_set_cbk },

        { NULL, NULL }
};


int
cli_cmd_volume_register (struct cli_state *state)
{
        int  ret = 0;
        struct cli_cmd *cmd = NULL;

        for (cmd = volume_cmds; cmd->pattern; cmd++) {
                ret = cli_cmd_register (&state->tree, cmd->pattern, cmd->cbk);
                if (ret)
                        goto out;
        }
out:
        return ret;
}
