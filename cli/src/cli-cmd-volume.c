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
#include "cli1-xdr.h"

extern struct rpc_clnt *global_rpc;

extern rpc_clnt_prog_t *cli_rpc_prog;

int
cli_cmd_volume_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                      const char **words, int wordcount);

int
cli_cmd_volume_info_cbk (struct cli_state *state, struct cli_cmd_word *word,
                         const char **words, int wordcount)
{
        int                             ret = -1;
        rpc_clnt_procedure_t            *proc = NULL;
        call_frame_t                    *frame = NULL;
        cli_cmd_volume_get_ctx_t        ctx = {0,};
        cli_local_t                     *local = NULL;
        int                             sent = 0;
        int                             parse_error = 0;

        proc = &cli_rpc_prog->proctable[GF1_CLI_GET_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if ((wordcount == 2)  || (wordcount == 3 &&
                                  !strcmp (words[2], "all"))) {
                ctx.flags = GF_CLI_GET_NEXT_VOLUME;
                proc = &cli_rpc_prog->proctable[GF1_CLI_GET_NEXT_VOLUME];
        } else if (wordcount == 3) {
                ctx.flags = GF_CLI_GET_VOLUME;
                ctx.volname = (char *)words[2];
                if (strlen (ctx.volname) > 1024) {
                        cli_out ("Invalid volume name");
                        goto out;
                }
                proc = &cli_rpc_prog->proctable[GF1_CLI_GET_VOLUME];
        } else {
                cli_usage_out (word->pattern);
                parse_error = 1;
                return -1;
        }

        local = cli_local_get ();

        if (!local)
                goto out;

        local->u.get_vol.flags = ctx.flags;
        if (ctx.volname)
                local->u.get_vol.volname = gf_strdup (ctx.volname);

        frame->local = local;

        if (proc->fn) {
                ret = proc->fn (frame, THIS, &ctx);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Getting Volume information failed!");
        }

        return ret;

}

int
cli_cmd_sync_volume_cbk (struct cli_state *state, struct cli_cmd_word *word,
                         const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        gf1_cli_sync_volume_req req = {0,};
        int                     sent = 0;
        int                     parse_error = 0;

        if ((wordcount < 3) || (wordcount > 4)) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        if ((wordcount == 3) || !strcmp(words[3], "all")) {
                req.flags = GF_CLI_SYNC_ALL;
                req.volname = "";
        } else {
                req.volname = (char *)words[3];
        }

        req.hostname = (char *)words[2];

        proc = &cli_rpc_prog->proctable[GF1_CLI_SYNC_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if (proc->fn) {
                ret = proc->fn (frame, THIS, &req);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume sync failed");
        }

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
        int                     sent = 0;
        int                     parse_error = 0;

        proc = &cli_rpc_prog->proctable[GF1_CLI_CREATE_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_create_parse (words, wordcount, &options);

        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (options)
                dict_unref (options);
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume create failed");
        }

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
        gf_answer_t             answer = GF_ANSWER_NO;
        const char              *question = NULL;
        int                     sent = 0;
        int                     parse_error = 0;

        question = "Deleting volume will erase all information about the volume. "
                   "Do you want to continue?";
        proc = &cli_rpc_prog->proctable[GF1_CLI_DELETE_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if (wordcount != 3) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        answer = cli_cmd_get_confirmation (state, question);

        if (GF_ANSWER_NO == answer) {
                ret = 0;
                goto out;
        }

        volname = (char *)words[2];

        if (proc->fn) {
                ret = proc->fn (frame, THIS, volname);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume delete failed");
        }

        return ret;
}

int
cli_cmd_volume_start_cbk (struct cli_state *state, struct cli_cmd_word *word,
                          const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        gf1_cli_start_vol_req    req = {0,};
        int                     sent = 0;
        int                     parse_error = 0;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if (wordcount < 3 || wordcount > 4) {
               cli_usage_out (word->pattern);
                parse_error = 1;
               goto out;
        }

        req.volname = (char *)words[2];
        if (!req.volname)
                goto out;

        if (wordcount == 4) {
                if (!strcmp("force", words[3])) {
                        req.flags |= GF_CLI_FLAG_OP_FORCE;
                } else {
                        ret = -1;
                        cli_usage_out (word->pattern);
                        parse_error = 1;
                        goto out;
                }
        }

        proc = &cli_rpc_prog->proctable[GF1_CLI_START_VOLUME];

        if (proc->fn) {
                ret = proc->fn (frame, THIS, &req);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume start failed");
        }

        return ret;
}

gf_answer_t
cli_cmd_get_confirmation (struct cli_state *state, const char *question)
{
        char                    answer[5] = {'\0', };
        char                    flush = '\0';
	int			len = 0;

        if (state->mode & GLUSTER_MODE_SCRIPT)
                return GF_ANSWER_YES;

	printf ("%s (y/n) ", question);

	fgets (answer, 4, stdin);

	len = strlen (answer);

	if (answer [len - 1] == '\n'){
		answer [--len] = '\0';
	} else {
		do{
			flush = getchar ();
		}while (flush != '\n');
	}

	if (len > 3)
		goto out;

	if (!strcasecmp (answer, "y") || !strcasecmp (answer, "yes"))
		return GF_ANSWER_YES;

	else if (!strcasecmp (answer, "n") || !strcasecmp (answer, "no"))
		return GF_ANSWER_NO;

out:
	cli_out ("Invalid input, please enter y/n");

	return GF_ANSWER_NO;
}

int
cli_cmd_volume_stop_cbk (struct cli_state *state, struct cli_cmd_word *word,
                           const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        int                     flags   = 0;
        gf1_cli_stop_vol_req    req = {0,};
        gf_answer_t             answer = GF_ANSWER_NO;
        int                     sent = 0;
        int                     parse_error = 0;

        const char *question = "Stopping volume will make its data inaccessible. "
                               "Do you want to continue?";

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if (wordcount < 3 || wordcount > 4) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        req.volname = (char *)words[2];
        if (!req.volname)
                goto out;

        if (wordcount == 4) {
                if (!strcmp("force", words[3])) {
                        flags |= GF_CLI_FLAG_OP_FORCE;
                } else {
                        ret = -1;
                        cli_usage_out (word->pattern);
                        parse_error = 1;
                        goto out;
                }
        }

        answer = cli_cmd_get_confirmation (state, question);

        if (GF_ANSWER_NO == answer) {
                ret = 0;
                goto out;
        }

        req.flags = flags;
        proc = &cli_rpc_prog->proctable[GF1_CLI_STOP_VOLUME];

        if (proc->fn) {
                ret = proc->fn (frame, THIS, &req);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume stop failed", req.volname);
        }

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
        int                     sent = 0;
        int                     parse_error = 0;


        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        dict = dict_new ();
        if (!dict)
                goto out;

        if (wordcount != 4) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

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
        if (dict)
                dict_destroy (dict);

        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume rename failed", (char *)words[2]);
        }

        return ret;
}

int
cli_cmd_volume_defrag_cbk (struct cli_state *state, struct cli_cmd_word *word,
                           const char **words, int wordcount)
{
        int                   ret     = -1;
        rpc_clnt_procedure_t *proc    = NULL;
        call_frame_t         *frame   = NULL;
        dict_t               *dict = NULL;
        int                     sent = 0;
        int                     parse_error = 0;
#ifdef GF_SOLARIS_HOST_OS
        cli_out ("Command not supported on Solaris");
        goto out;
#endif

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        dict = dict_new ();
        if (!dict)
                goto out;

        if (wordcount != 4) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

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
        if (dict)
                dict_destroy (dict);

        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume rebalance failed");
        }

        return ret;
}

int
cli_cmd_volume_reset_cbk (struct cli_state *state, struct cli_cmd_word *word,
                          const char **words, int wordcount)
{
        int                     sent = 0;
        int                     parse_error = 0;

        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *options = NULL;

        proc = &cli_rpc_prog->proctable[GF1_CLI_RESET_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_reset_parse (words, wordcount, &options);

        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (options)
                dict_unref (options);

        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume reset failed");
        }

        return ret;

}


int
cli_cmd_volume_set_cbk (struct cli_state *state, struct cli_cmd_word *word,
                        const char **words, int wordcount)
{
        int                     sent = 0;
        int                     parse_error = 0;

	int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *options = NULL;

        proc = &cli_rpc_prog->proctable[GF1_CLI_SET_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_set_parse (words, wordcount, &options);

        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (options)
                dict_unref (options);

        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume set failed");
        }

        return ret;

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
        int                     sent = 0;
        int                     parse_error = 0;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_add_brick_parse (words, wordcount, &options);

        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GF1_CLI_ADD_BRICK];

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (options)
                dict_unref (options);

        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume add-brick failed");
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
        gf_answer_t             answer = GF_ANSWER_NO;
        int                     sent = 0;
        int                     parse_error = 0;

        const char *question = "Removing brick(s) can result in data loss. "
                               "Do you want to Continue?";

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_remove_brick_parse (words, wordcount, &options);

        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        answer = cli_cmd_get_confirmation (state, question);

        if (GF_ANSWER_NO == answer) {
                ret = 0;
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GF1_CLI_REMOVE_BRICK];

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume remove-brick failed");
        }

        if (options)
                dict_unref (options);
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
        int                     sent = 0;
        int                     parse_error = 0;

#ifdef GF_SOLARIS_HOST_OS
        cli_out ("Command not supported on Solaris");
        goto out;
#endif
        proc = &cli_rpc_prog->proctable[GF1_CLI_REPLACE_BRICK];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_replace_brick_parse (words, wordcount, &options);

        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (options)
                dict_unref (options);

        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume replace-brick failed");
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

int
cli_cmd_log_filename_cbk (struct cli_state *state, struct cli_cmd_word *word,
                          const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *options = NULL;
        int                     sent = 0;
        int                     parse_error = 0;

        if (!((wordcount == 5) || (wordcount == 6))) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GF1_CLI_LOG_FILENAME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_log_filename_parse (words, wordcount, &options);
        if (ret)
                goto out;

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (options)
                dict_destroy (options);

        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume log filename failed");
        }

        return ret;
}


int
cli_cmd_log_locate_cbk (struct cli_state *state, struct cli_cmd_word *word,
                        const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *options = NULL;
        int                     sent = 0;
        int                     parse_error = 0;

        if (!((wordcount == 4) || (wordcount == 5))) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GF1_CLI_LOG_LOCATE];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_log_locate_parse (words, wordcount, &options);
        if (ret)
                goto out;

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (options)
                dict_destroy (options);

        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("getting log file location information failed");
        }

        return ret;
}

int
cli_cmd_log_rotate_cbk (struct cli_state *state, struct cli_cmd_word *word,
                        const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *options = NULL;
        int                     sent = 0;
        int                     parse_error = 0;

        if (!((wordcount == 4) || (wordcount == 5))) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GF1_CLI_LOG_ROTATE];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_log_rotate_parse (words, wordcount, &options);
        if (ret)
                goto out;

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (options)
                dict_destroy (options);

        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume log rotate failed");
        }

        return ret;
}

int
cli_cmd_volume_gsync_set_cbk (struct cli_state *state, struct cli_cmd_word *word,
                              const char **words, int wordcount)
{
        int                      ret     = 0;
        int                      parse_err = 0;
        dict_t                  *options = NULL;
        rpc_clnt_procedure_t    *proc    = NULL;
        call_frame_t            *frame   = NULL;

        proc = &cli_rpc_prog->proctable [GF1_CLI_GSYNC_SET];
        if (proc == NULL) {
                ret = -1;
                goto out;
        }

        frame = create_frame (THIS, THIS->ctx->pool);
        if (frame == NULL) {
                ret = -1;
                goto out;
        }

        ret = cli_cmd_gsync_set_parse (words, wordcount, &options);
        if (ret) {
                cli_usage_out (word->pattern);
                parse_err = 1;
                goto out;
        }

        if (proc->fn)
                ret = proc->fn (frame, THIS, options);

out:
        if (options)
                dict_unref (options);

        if (ret && parse_err == 0)
                cli_out ("Gsync command failed");

        return ret;
}

struct cli_cmd volume_cmds[] = {
        { "volume info [all|<VOLNAME>]",
          cli_cmd_volume_info_cbk,
          "list information of all volumes"},

        { "volume create <NEW-VOLNAME> [stripe <COUNT>] [replica <COUNT>] [transport <tcp|rdma>] <NEW-BRICK> ...",
          cli_cmd_volume_create_cbk,
          "create a new volume of specified type with mentioned bricks"},

        { "volume delete <VOLNAME>",
          cli_cmd_volume_delete_cbk,
          "delete volume specified by <VOLNAME>"},

        { "volume start <VOLNAME> [force]",
          cli_cmd_volume_start_cbk,
          "start volume specified by <VOLNAME>"},

        { "volume stop <VOLNAME> [force]",
          cli_cmd_volume_stop_cbk,
          "stop volume specified by <VOLNAME>"},

        /*{ "volume rename <VOLNAME> <NEW-VOLNAME>",
          cli_cmd_volume_rename_cbk,
          "rename volume <VOLNAME> to <NEW-VOLNAME>"},*/

        { "volume add-brick <VOLNAME> <NEW-BRICK> ...",
          cli_cmd_volume_add_brick_cbk,
          "add brick to volume <VOLNAME>"},

        { "volume remove-brick <VOLNAME> <BRICK> ...",
          cli_cmd_volume_remove_brick_cbk,
          "remove brick from volume <VOLNAME>"},

        { "volume rebalance <VOLNAME> {start|stop|status}",
          cli_cmd_volume_defrag_cbk,
          "rebalance operations"},

        { "volume replace-brick <VOLNAME> <BRICK> <NEW-BRICK> {start|pause|abort|status|commit}",
          cli_cmd_volume_replace_brick_cbk,
          "replace-brick operations"},

        /*{ "volume set-transport <VOLNAME> <TRANSPORT-TYPE> [<TRANSPORT-TYPE>] ...",
          cli_cmd_volume_set_transport_cbk,
          "set transport type for volume <VOLNAME>"},*/

        { "volume set <VOLNAME> <KEY> <VALUE>",
          cli_cmd_volume_set_cbk,
         "set options for volume <VOLNAME>"},

        { "volume help",
          cli_cmd_volume_help_cbk,
          "display help for the volume command"},

        { "volume log filename <VOLNAME> [BRICK] <PATH>",
          cli_cmd_log_filename_cbk,
         "set the log file for corresponding volume/brick"},

        { "volume log locate <VOLNAME> [BRICK]",
          cli_cmd_log_locate_cbk,
         "locate the log file for corresponding volume/brick"},

        { "volume log rotate <VOLNAME> [BRICK]",
          cli_cmd_log_rotate_cbk,
         "rotate the log file for corresponding volume/brick"},

        { "volume sync <HOSTNAME> [all|<VOLNAME>]",
          cli_cmd_sync_volume_cbk,
         "sync the volume information from a peer"},

         { "volume reset <VOLNAME> ",
         cli_cmd_volume_reset_cbk,
         "reset all the reconfigured options"},

        {"volume gsync <start|stop|configure> <MASTER> <SLAVE> [options]",
         cli_cmd_volume_gsync_set_cbk,
         "Geo-sync operations"},

        { NULL, NULL, NULL }
};

int
cli_cmd_volume_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                      const char **words, int wordcount)
{
        struct cli_cmd        *cmd = NULL;

        for (cmd = volume_cmds; cmd->pattern; cmd++)
                cli_out ("%s - %s", cmd->pattern, cmd->desc);

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
