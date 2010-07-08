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
#include "cli-mem-types.h"

#define CMDBUFSIZ 1024

#define cli_out(fmt...) fprintf (stdout, ##fmt)

void *
cli_batch (void *d)
{
        struct cli_state *state = NULL;
        int               ret = 0;

        state = d;

        ret = cli_cmd_process (state, state->argc, state->argv);
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

        state = d;

        for (;;) {
                cli_out ("%s", state->prompt);

                cmd = fgets (cmdbuf, CMDBUFSIZ, stdin);
                if (!cmd)
                        break;

                printf ("processing command: '%s'\n", cmd);
                ret = cli_cmd_process_line (state, cmd);
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

        state->prompt = "gluster> ";

        cli_rl_enable (state);

        if (!state->rl_enabled)
                ret = pthread_create (&state->input, NULL, cli_input, state);

        return ret;
}
