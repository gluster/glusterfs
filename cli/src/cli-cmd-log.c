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

int
cli_cmd_log_cbk (struct cli_state *state, struct cli_cmd_word *word,
                 const char **words, int wordcount)
{
        cli_cmd_broadcast_response (0);
        return 0;
}

struct cli_cmd cli_log_cmds[] = {
        { "log [VOLNAME] ...",
          cli_cmd_log_cbk },

        { NULL, NULL }
};


int
cli_cmd_log_register (struct cli_state *state)
{
        int  ret = 0;
        struct cli_cmd *cmd = NULL;

        for (cmd = cli_log_cmds; cmd->pattern; cmd++) {
                ret = cli_cmd_register (&state->tree, cmd->pattern, cmd->cbk);
                if (ret)
                        goto out;
        }
out:
        return ret;
}
