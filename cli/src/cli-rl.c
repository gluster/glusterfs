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

#include "event.h"

#include <fnmatch.h>

#ifdef HAVE_READLINE

#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>


int
cli_rl_out (struct cli_state *state, const char *fmt, va_list ap)
{
        int tmp_rl_point = rl_point;
        int            n = rl_end;
        int            ret = 0;

        if (rl_end >= 0 ) {
                rl_kill_text (0, rl_end);
                rl_redisplay ();
        }

        printf ("\r%*s\r", (int)strlen (state->prompt), "");

        ret = vprintf (fmt, ap);

        printf ("\n");
        fflush(stdout);

        if (n) {
                rl_do_undo ();
                rl_point = tmp_rl_point;
                rl_reset_line_state ();
        }

        return ret;
}

int
cli_rl_err (struct cli_state *state, const char *fmt, va_list ap)
{
        int tmp_rl_point = rl_point;
        int            n = rl_end;
        int            ret = 0;

        if (rl_end >= 0 ) {
                rl_kill_text (0, rl_end);
                rl_redisplay ();
        }

        fprintf (stderr, "\r%*s\r", (int)strlen (state->prompt), "");

        ret = vfprintf (stderr, fmt, ap);

        fprintf (stderr, "\n");
        fflush(stderr);

        if (n) {
                rl_do_undo ();
                rl_point = tmp_rl_point;
                rl_reset_line_state ();
        }

        return ret;
}


void
cli_rl_process_line (char *line)
{
        struct cli_state *state = NULL;
        int               ret = 0;

        state = global_state;

        state->rl_processing = 1;
        {
                ret = cli_cmd_process_line (state, line);
                if (ret)
                        gf_log (THIS->name, GF_LOG_WARNING,
                                "failed to process line");

                add_history (line);
        }
        state->rl_processing = 0;

}


int
cli_rl_stdin (int fd, int idx, void *data,
              int poll_out, int poll_in, int poll_err)
{
        rl_callback_read_char ();

        return 0;
}


char *
cli_rl_autocomplete_entry (const char *text, int times)
{
        struct cli_state  *state = NULL;
        char              *retp = NULL;

        state = global_state;

        if (!state->matchesp)
                return NULL;

        retp = *state->matchesp;

        state->matchesp++;

        return retp ? strdup (retp) : NULL;
}


int
cli_rl_token_count (const char *text)
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

        if (is_spc)
                /* what needs to be autocompleted is a full
                   new word, and not extend the last word
                */
                count++;

        return count;
}


char **
cli_rl_tokenize (const char *text)
{
        int     count = 0;
        char  **tokens = NULL;
        char  **tokenp = NULL;
        char   *token = NULL;
        char   *copy = NULL;
        char   *saveptr = NULL;
        int     i = 0;

        count = cli_rl_token_count (text);

        tokens = calloc (count + 1, sizeof (*tokens));
        if (!tokens)
                return NULL;

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

        if (i < count) {
                /* symbolize that what needs to be autocompleted is
                   the full set of possible nextwords, and not extend
                   the last word
                */
                *tokenp = strdup ("");
                if (!*tokenp)
                        goto out;
                tokenp++;
                i++;
        }

out:
        free (copy);

        if (i < count) {
                cli_cmd_tokens_destroy (tokens);
                tokens = NULL;
        }

        return tokens;
}


char **
cli_rl_get_matches (struct cli_state *state, struct cli_cmd_word *word,
                    const char *text)
{
        char                 **matches = NULL;
        char                 **matchesp = NULL;
        struct cli_cmd_word  **next = NULL;
        int                    count = 0;
        int                    len = 0;

        len = strlen (text);

        if (!word->nextwords)
                return NULL;

        for (next = word->nextwords; *next; next++)
                count++;

        matches = calloc (count + 1, sizeof (*matches));
        matchesp = matches;

        for (next = word->nextwords; *next; next++) {
                if ((*next)->match) {
                        continue;
                }

                if (strncmp ((*next)->word, text, len) == 0) {
                        *matchesp = strdup ((*next)->word);
                        matchesp++;
                }
        }

        return matches;
}


int
cli_rl_autocomplete_prepare (struct cli_state *state, const char *text)
{
        struct cli_cmd_word   *word = NULL;
        struct cli_cmd_word   *next = NULL;
        char                 **tokens = NULL;
        char                 **tokenp = NULL;
        char                  *token = NULL;
        char                 **matches = NULL;

        tokens = cli_rl_tokenize (text);
        if (!tokens)
                return 0;

        word = &state->tree.root;

        for (tokenp = tokens; (token = *tokenp); tokenp++) {
                if (!*(tokenp+1)) {
                        /* last word */
                        break;
                }

                next = cli_cmd_nextword (word, token);
                word = next;
                if (!word)
                        break;
        }

        if (!word)
                goto out;

        matches = cli_rl_get_matches (state, word, token);

        state->matches = matches;
        state->matchesp = matches;

out:
        cli_cmd_tokens_destroy (tokens);
        return 0;
}


int
cli_rl_autocomplete_cleanup (struct cli_state *state)
{
        if (state->matches)
                cli_cmd_tokens_destroy (state->matches);

        state->matches = NULL;
        state->matchesp = NULL;

        return 0;
}


char **
cli_rl_autocomplete (const char *text, int start, int end)
{
        struct cli_state  *state = NULL;
        char             **matches = NULL;
        char               save = 0;

        state = global_state;

        /* hack to make the autocompletion code neater */
        /* fake it as though the cursor is at the end of line */

        save = rl_line_buffer[rl_point];
        rl_line_buffer[rl_point] = 0;

        cli_rl_autocomplete_prepare (state, rl_line_buffer);

        matches = rl_completion_matches (text, cli_rl_autocomplete_entry);

        cli_rl_autocomplete_cleanup (state);

        rl_line_buffer[rl_point] = save;

        return matches;
}


static char *
complete_none (const char *txt, int times)
{
        return NULL;
}


void *
cli_rl_input (void *_data)
{
        struct cli_state *state = NULL;
        char             *line = NULL;

        state = _data;

        for (;;) {
                line = readline (state->prompt);
                if (!line)
                        exit(0);  //break;

                if (*line)
                        cli_rl_process_line (line);

                free (line);
        }

        return NULL;
}


int
cli_rl_enable (struct cli_state *state)
{
        int ret = 0;

        rl_pre_input_hook = NULL;
        rl_attempted_completion_function = cli_rl_autocomplete;
        rl_completion_entry_function = complete_none;

        if (!state->rl_async) {
                ret = pthread_create (&state->input, NULL,
                                      cli_rl_input, state);
                if (ret == 0)
                        state->rl_enabled = 1;
                goto out;
        }

        ret = event_register (state->ctx->event_pool, 0, cli_rl_stdin, state,
                              1, 0);
        if (ret == -1)
                goto out;

        state->rl_enabled = 1;
        rl_callback_handler_install (state->prompt, cli_rl_process_line);

out:
        return state->rl_enabled;
}

#else /* HAVE_READLINE */

int
cli_rl_enable (struct cli_state *state)
{
        return 0;
}

#endif /* HAVE_READLINE */
