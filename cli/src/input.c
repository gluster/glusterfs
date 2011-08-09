/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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
#include "cli-mem-types.h"

#define CMDBUFSIZ 1024

void *
cli_batch (void *d)
{
        struct cli_state *state = NULL;
        int               ret = 0;

        state = d;

        ret = cli_cmd_process (state, state->argc, state->argv);

        gf_log ("", GF_LOG_NORMAL, "Exiting with: %d", ret);
        exit (ret);

        return NULL;
}


void *
cli_input (void *d)
{
        struct cli_state *state = NULL;
        int               ret = 0;
        char              cmdbuf[CMDBUFSIZ];
        char             *cmd = NULL;
        size_t            len = 0;

        state = d;

        for (;;) {
                printf ("%s", state->prompt);

                cmd = fgets (cmdbuf, CMDBUFSIZ, stdin);
                if (!cmd)
                        break;
                len = strlen(cmd);
                if (len > 0 && cmd[len - 1] == '\n') //strip trailing \n
                        cmd[len - 1] = '\0';
                ret = cli_cmd_process_line (state, cmd);
                if (ret == -1 && state->mode & GLUSTER_MODE_ERR_FATAL)
                        break;
        }

        exit (ret);

        return NULL;
}


int
cli_input_init (struct cli_state *state)
{
        int  ret = 0;

        if (state->argc) {
                ret = pthread_create (&state->input, NULL, cli_batch, state);
                return ret;
        }

        if (isatty (STDIN_FILENO)) {
                state->prompt = "gluster> ";

                cli_rl_enable (state);
        } else {
                state->prompt = "";
                state->mode = GLUSTER_MODE_SCRIPT | GLUSTER_MODE_ERR_FATAL;
        }

        if (!state->rl_enabled)
                ret = pthread_create (&state->input, NULL, cli_input, state);

        return ret;
}
