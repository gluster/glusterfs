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

#include <fnmatch.h>

static int cmd_done;
static pthread_cond_t      cond  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t     cond_mutex = PTHREAD_MUTEX_INITIALIZER;

int
cli_cmd_process (struct cli_state *state, int argc, char **argv)
{
        int                  ret = 0;
        struct cli_cmd_word *word = NULL;
        struct cli_cmd_word *next = NULL;
        int                  i = 0;

        word = &state->tree.root;

        for (i = 0; i < argc; i++) {
                next = cli_cmd_nextword (word, argv[i]);

                word = next;
                if (!word)
                        break;

                if (word->cbkfn)
                        break;
        }

        if (!word) {
                cli_out ("unrecognized word: %s (position %d)\n",
                         argv[i], i);
                return -1;
        }

        if (!word->cbkfn) {
                cli_out ("unrecognized command\n");
                return -1;
        }

        ret = word->cbkfn (state, word, (const char **)argv, argc);

        return ret;
}


int
cli_cmd_input_token_count (const char *text)
{
        int          count = 0;
        const char  *trav = NULL;
        int          is_spc = 1;

        for (trav = text; *trav; trav++) {
                if (*trav == ' ') {
                        is_spc = 1;
                } else {
                        if (is_spc) {
                                count++;
                                is_spc = 0;
                        }
                }
        }

        return count;
}


int
cli_cmd_process_line (struct cli_state *state, const char *text)
{
        int     count = 0;
        char  **tokens = NULL;
        char  **tokenp = NULL;
        char   *token = NULL;
        char   *copy = NULL;
        char   *saveptr = NULL;
        int     i = 0;
        int     ret = -1;

        count = cli_cmd_input_token_count (text);

        tokens = calloc (count + 1, sizeof (*tokens));
        if (!tokens)
                return -1;

        copy = strdup (text);
        if (!copy)
                goto out;

        tokenp = tokens;

        for (token = strtok_r (copy, " \t\r\n", &saveptr); token;
             token = strtok_r (NULL, " \t\r\n", &saveptr)) {
                *tokenp = strdup (token);

                if (!*tokenp)
                        goto out;
                tokenp++;
                i++;

        }

        ret = cli_cmd_process (state, count, tokens);
out:
        if (copy)
                free (copy);

        if (tokens)
                cli_cmd_tokens_destroy (tokens);

        return ret;
}


int
cli_cmds_register (struct cli_state *state)
{
        int  ret = 0;

        ret = cli_cmd_volume_register (state);
        if (ret)
                goto out;

        ret = cli_cmd_probe_register (state);
        if (ret)
                goto out;

        ret = cli_cmd_misc_register (state);
        if (ret)
                goto out;

out:
        return ret;
}

int
cli_cmd_await_response ()
{
       pthread_mutex_init (&cond_mutex, NULL);
       pthread_cond_init (&cond, NULL);
       cmd_done = 0;

       pthread_mutex_lock (&cond_mutex);
        {
                while (!cmd_done) {
                        pthread_cond_wait (&cond, &cond_mutex);
                }
        }
        pthread_mutex_unlock (&cond_mutex);

        pthread_mutex_destroy (&cond_mutex);
        pthread_cond_destroy (&cond);

        return 0;
}

int
cli_cmd_broadcast_response ()
{
        pthread_mutex_lock (&cond_mutex);
        {
                cmd_done = 1;
                pthread_cond_broadcast (&cond);
        }

        pthread_mutex_unlock (&cond_mutex);

        return 0;
}

