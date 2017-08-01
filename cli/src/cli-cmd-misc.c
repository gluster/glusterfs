/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
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

#include "cli.h"
#include "cli-cmd.h"
#include "cli-mem-types.h"
#include "protocol-common.h"

extern struct rpc_clnt *global_rpc;

extern rpc_clnt_prog_t *cli_rpc_prog;

extern struct cli_cmd volume_cmds[];
extern struct cli_cmd bitrot_cmds[];
extern struct cli_cmd quota_cmds[];
extern struct cli_cmd tier_cmds[];
extern struct cli_cmd cli_probe_cmds[];
extern struct cli_cmd cli_log_cmds[];
extern struct cli_cmd cli_system_cmds[];
extern struct cli_cmd cli_bd_cmds[];
extern struct cli_cmd snapshot_cmds[];
extern struct cli_cmd global_cmds[];
struct cli_cmd cli_misc_cmds[];

int
cli_cmd_quit_cbk (struct cli_state *state, struct cli_cmd_word *word,
                   const char **words, int wordcount)
{
        exit (0);
}


static gf_boolean_t
cli_is_help_command (const char *pattern)
{
        /* FixFixFix
         * This is not the best way to determine whether
         * this is a help command
         */
        if (strstr (pattern, "help"))
                return _gf_true;

        return _gf_false;
}


int
cli_cmd_display_help (struct cli_state *state, struct cli_cmd_word *in_word,
                      const char **words, int wordcount)
{
        struct cli_cmd        *cmd[] = {cli_misc_cmds, cli_probe_cmds,
                                        volume_cmds, bitrot_cmds, quota_cmds,
#if !defined(__NetBSD__)
                                        tier_cmds,
#endif
                                        snapshot_cmds, global_cmds, NULL};
        struct cli_cmd        *cmd_ind      = NULL;
        int                    i            = 0;
        gf_boolean_t           list_all     = _gf_false;

         /* cli_system_cmds commands for internal usage
           they are not exposed
         */

        /* If "help all" */
        if (wordcount == 2)
                list_all = _gf_true;

        for (i = 0; cmd[i] != NULL; i++) {
                for (cmd_ind = cmd[i]; cmd_ind->pattern; cmd_ind++) {
                        if ((_gf_false == cmd_ind->disable) &&
                            cli_is_help_command (cmd_ind->pattern)) {
                                if (list_all && (cmd_ind->cbk)) {
                                        cmd_ind->cbk (state, in_word, words,
                                                      wordcount);
                                } else {
                                        cli_out (" %-25s- %s", cmd_ind->pattern,
                                                 cmd_ind->desc);
                                }
                        }
                }
        }

        cli_out ("\n");
        return 0;
}


struct cli_cmd cli_help_cmds[] = {
        { "help [all]",
           cli_cmd_display_help,
           "display help for command classes"},

        { NULL, NULL, NULL }
};


struct cli_cmd cli_misc_cmds[] = {
        { "quit",
          cli_cmd_quit_cbk,
          "quit"},
        { "exit",
          cli_cmd_quit_cbk,
          "exit"},

        { NULL, NULL, NULL }
};


int
cli_cmd_misc_register (struct cli_state *state)
{
        int  ret = 0;
        struct cli_cmd *cmd = NULL;

        for (cmd = cli_misc_cmds; cmd->pattern; cmd++) {
                ret = cli_cmd_register (&state->tree, cmd);
                if (ret)
                        goto out;
        }

        for (cmd = cli_help_cmds; cmd->pattern; cmd++) {
                ret = cli_cmd_register (&state->tree, cmd);
                if (ret)
                        goto out;
        }
out:
        return ret;
}
