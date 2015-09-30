/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
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

#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include "cli.h"
#include "cli-cmd.h"
#include "cli-mem-types.h"
#include "cli1-xdr.h"
#include "run.h"
#include "syscall.h"
#include "common-utils.h"

extern rpc_clnt_prog_t *cli_rpc_prog;

int
cli_cmd_global_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                         const char **words, int wordcount);
int cli_cmd_ganesha_cbk (struct cli_state *state, struct cli_cmd_word *word,
                                         const char **words, int wordcount);


struct cli_cmd global_cmds[] = {
        { "global help",
           cli_cmd_global_help_cbk,
           "list global commands",
        },
        { "nfs-ganesha {enable| disable} ",
           cli_cmd_ganesha_cbk,
          "Enable/disable NFS-Ganesha support",
        },
        {NULL,  NULL,  NULL}
};

int
cli_cmd_global_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                      const char **words, int wordcount)
{
        struct cli_cmd        *cmd = NULL;

        for (cmd = global_cmds; cmd->pattern; cmd++)
                if (_gf_false == cmd->disable)
                        cli_out ("%s - %s", cmd->pattern, cmd->desc);

        return 0;
}

int
cli_cmd_global_register (struct cli_state *state)
{
        int ret = 0;
        struct cli_cmd *cmd =  NULL;
        for (cmd = global_cmds; cmd->pattern; cmd++) {
                ret = cli_cmd_register (&state->tree, cmd);
                        if (ret)
                                goto out;
        }
out:
        return ret;

}

int cli_cmd_ganesha_cbk (struct cli_state *state, struct cli_cmd_word *word,
                         const char **words, int wordcount)

{
         int                     sent        =   0;
         int                     parse_error =   0;
         int                     ret         =  -1;
         rpc_clnt_procedure_t    *proc       =  NULL;
         call_frame_t            *frame      =  NULL;
         dict_t                  *options    =  NULL;
         cli_local_t             *local      =  NULL;
         char                    *op_errstr  =  NULL;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_GANESHA];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_ganesha_parse (state, words, wordcount,
                                     &options, &op_errstr);
        if (ret) {
                if (op_errstr) {
                    cli_err ("%s", op_errstr);
                    GF_FREE (op_errstr);
                } else
                    cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Setting global option failed");
        }

        CLI_STACK_DESTROY (frame);
        return ret;
}

