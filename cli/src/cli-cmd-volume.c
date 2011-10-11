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

#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "cli.h"
#include "cli-cmd.h"
#include "cli-mem-types.h"
#include "cli1-xdr.h"
#include "run.h"

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

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_GET_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if ((wordcount == 2)  || (wordcount == 3 &&
                                  !strcmp (words[2], "all"))) {
                ctx.flags = GF_CLI_GET_NEXT_VOLUME;
                proc = &cli_rpc_prog->proctable[GLUSTER_CLI_GET_NEXT_VOLUME];
        } else if (wordcount == 3) {
                ctx.flags = GF_CLI_GET_VOLUME;
                ctx.volname = (char *)words[2];
                if (strlen (ctx.volname) > 1024) {
                        cli_out ("Invalid volume name");
                        goto out;
                }
                proc = &cli_rpc_prog->proctable[GLUSTER_CLI_GET_VOLUME];
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

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_SYNC_VOLUME];

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

gf_ai_compare_t
cli_cmd_compare_addrinfo (struct addrinfo *first, struct addrinfo *next)
{
        int             ret = -1;
        struct addrinfo *tmp1 = NULL;
        struct addrinfo *tmp2 = NULL;
        char            firstip[NI_MAXHOST] = {0.};
        char            nextip[NI_MAXHOST] = {0,};

        for (tmp1 = first; tmp1 != NULL; tmp1 = tmp1->ai_next) {
                ret = getnameinfo (tmp1->ai_addr, tmp1->ai_addrlen, firstip,
                                   NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
                if (ret)
                        return GF_AI_COMPARE_ERROR;
                for (tmp2 = next; tmp2 != NULL; tmp2 = tmp2->ai_next) {
                        ret = getnameinfo (tmp2->ai_addr, tmp2->ai_addrlen, nextip,
                                           NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
                        if (ret)
                                return GF_AI_COMPARE_ERROR;
                        if (!strcmp (firstip, nextip)) {
                                return GF_AI_COMPARE_MATCH;
                        }
                }
        }
        return GF_AI_COMPARE_NO_MATCH;
}

/* Check for non optimal brick order for replicate :
 * Checks if bricks belonging to a replicate volume
 * are present on the same server
 */
int32_t
cli_cmd_check_brick_order (struct cli_state *state, const char *bricks,
                           int brick_count, int sub_count)
{
        int             ret = -1;
        int             i = 0;
        int             j = 0;
        int             k = 0;
        addrinfo_list_t *ai_list = NULL;
        addrinfo_list_t *ai_list_tmp1 = NULL;
        addrinfo_list_t *ai_list_tmp2 = NULL;
        char            *brick = NULL;
        char            *brick_list = NULL;
        char            *brick_list_dup = NULL;
        char            *tmpptr = NULL;
        struct addrinfo *ai_info = NULL;
        gf_answer_t     answer = GF_ANSWER_NO;
        const char      *failed_question = NULL;
        const char      *found_question = NULL;
        failed_question = "Failed to perform brick order check. "
                          "Do you want to continue creating the volume? ";
        found_question =  "Multiple bricks of a replicate volume are present"
                          " on the same server. This setup is not optimal.\n"
                          "Do you still want to continue creating the volume? ";

        GF_ASSERT (bricks);
        GF_ASSERT (brick_count > 0);
        GF_ASSERT (sub_count > 0);

        ai_list = malloc (sizeof (addrinfo_list_t));
        ai_list->info = NULL;
        INIT_LIST_HEAD (&ai_list->list);
        brick_list = gf_strdup (bricks);
        if (brick_list == NULL) {
                gf_log ("cli", GF_LOG_DEBUG, "failed to allocate memory");
                goto check_failed;
        }
        brick_list_dup = brick_list;
        /* Resolve hostnames and get addrinfo */
        while (i < brick_count) {
                ++i;
                brick = strtok_r (brick_list, " \n", &tmpptr);
                brick_list = tmpptr;
                if (brick == NULL)
                        goto check_failed;
                brick = strtok_r (brick, ":", &tmpptr);
                if (brick == NULL)
                        goto check_failed;
                ret = getaddrinfo (brick, NULL, NULL, &ai_info);
                if (ret)
                        goto check_failed;
                ai_list_tmp1 = malloc (sizeof (addrinfo_list_t));
                if (ai_list_tmp1 == NULL)
                        goto check_failed;
                ai_list_tmp1->info = ai_info;
                list_add_tail (&ai_list_tmp1->list, &ai_list->list);
                ai_list_tmp1 = NULL;
        }

        i = 0;
        ai_list_tmp1 = list_entry (ai_list->list.next, addrinfo_list_t, list);

        /* Check for bad brick order */
        while (i < brick_count) {
                ++i;
                ai_info = ai_list_tmp1->info;
                ai_list_tmp1 = list_entry (ai_list_tmp1->list.next,
                                           addrinfo_list_t, list);
                if ( 0 == i % sub_count) {
                        j = 0;
                        continue;
                }
                ai_list_tmp2 = ai_list_tmp1;
                k = j;
                while (k < sub_count - 1) {
                        ++k;
                        ret = cli_cmd_compare_addrinfo (ai_info,
                                                        ai_list_tmp2->info);
                        if (GF_AI_COMPARE_ERROR == ret)
                                goto check_failed;
                        if (GF_AI_COMPARE_MATCH == ret)
                                goto found_bad_brick_order;
                        ai_list_tmp2 = list_entry (ai_list_tmp2->list.next,
                                                   addrinfo_list_t, list);
                }
                ++j;
        }
        gf_log ("cli", GF_LOG_INFO, "Brick order okay");
        ret = 0;
        goto out;

check_failed:
        gf_log ("cli", GF_LOG_INFO, "Failed bad brick order check");
        answer = cli_cmd_get_confirmation(state, failed_question);
        if (GF_ANSWER_YES == answer)
                ret = 0;
        goto out;

found_bad_brick_order:
        gf_log ("cli", GF_LOG_INFO, "Bad brick order found");
        answer = cli_cmd_get_confirmation (state, found_question);
        if (GF_ANSWER_YES == answer)
                ret = 0;
out:
        ai_list_tmp2 = NULL;
        i = 0;
        if (brick_list_dup)
                GF_FREE (brick_list_dup);
        list_for_each_entry (ai_list_tmp1, &ai_list->list, list) {
                if (ai_list_tmp1->info)
                        freeaddrinfo (ai_list_tmp1->info);
                if (ai_list_tmp2)
                        free (ai_list_tmp2);
                ai_list_tmp2 = ai_list_tmp1;
        }
        free (ai_list_tmp2);
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
        char                    *brick_list = NULL;
        int32_t                 brick_count = 0;
        int32_t                 sub_count = 0;
        int32_t                 type = GF_CLUSTER_TYPE_NONE;


        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_CREATE_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_create_parse (words, wordcount, &options);

        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }
        /*Check brick order if type is replicate*/
        ret = dict_get_int32 (options, "type", &type);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Could not get brick type");
                goto out;
        }
        if ((type == GF_CLUSTER_TYPE_REPLICATE) ||
            (type == GF_CLUSTER_TYPE_STRIPE_REPLICATE)) {
                if ((ret = dict_get_str (options, "bricks", &brick_list)) != 0) {
                        gf_log ("cli", GF_LOG_ERROR, "Replica bricks check : "
                                                     "Could not retrieve bricks list");
                        goto out;
                }
                if ((ret = dict_get_int32 (options, "count", &brick_count)) != 0) {
                        gf_log ("cli", GF_LOG_ERROR, "Replica bricks check : "
                                                     "Could not retrieve brick count");
                        goto out;
                }
                if ((ret = dict_get_int32 (options, "replica-count", &sub_count)) != 0) {
                        gf_log ("cli", GF_LOG_ERROR, "Replica bricks check : "
                                                    "Could not retrieve replica count");
                        goto out;
                }
                gf_log ("cli", GF_LOG_INFO, "Replicate cluster type found."
                                            " Checking brick order.");
                ret = cli_cmd_check_brick_order (state, brick_list, brick_count, sub_count);
                if (ret) {
                        gf_log("cli", GF_LOG_INFO, "Not creating volume because of bad brick order");
                        goto out;
                }
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
        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_DELETE_VOLUME];

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

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_START_VOLUME];

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

        if (fgets (answer, 4, stdin) == NULL) {
                cli_out("gluster cli read error");
                goto out;
        }

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
        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_STOP_VOLUME];

        if (proc->fn) {
                ret = proc->fn (frame, THIS, &req);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume stop on '%s' failed", req.volname);
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

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_RENAME_VOLUME];

        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (dict)
                dict_destroy (dict);

        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume rename on '%s' failed", (char *)words[2]);
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
        int                     index = 0;
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

        if (!((wordcount == 4) || (wordcount == 5) || (wordcount == 6))) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        if (wordcount == 4) {
                index = 3;
        } else {
                if (strcmp (words[3], "fix-layout") &&
                    strcmp (words[3], "migrate-data")) {
                        cli_usage_out (word->pattern);
                        parse_error = 1;
                        goto out;
                }
                index = 4;
        }

	if (strcmp (words[index], "start") && strcmp (words[index], "stop") &&
            strcmp (words[index], "status")) {
	        cli_usage_out (word->pattern);
		parse_error = 1;
		goto out;
	}

        ret = dict_set_str (dict, "volname", (char *)words[2]);
        if (ret)
                goto out;

        if (wordcount == 4) {
                ret = dict_set_str (dict, "command", (char *)words[3]);
                if (ret)
                        goto out;
        }
        if (wordcount == 5) {
                ret = dict_set_str (dict, "start-type", (char *)words[3]);
                if (ret)
                        goto out;
                ret = dict_set_str (dict, "command", (char *)words[4]);
                if (ret)
                        goto out;
        }

        /* 'force' option is valid only for the 'migrate-data' key */
        if (wordcount == 6) {
                if (strcmp (words[3], "migrate-data") ||
                    strcmp (words[4], "start") ||
                    strcmp (words[5], "force")) {
                        cli_usage_out (word->pattern);
                        parse_error = 1;
                        goto out;
                }
                ret = dict_set_str (dict, "start-type", "migrate-data-force");
                if (ret)
                        goto out;
                ret = dict_set_str (dict, "command", (char *)words[4]);
                if (ret)
                        goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_DEFRAG_VOLUME];

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

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_RESET_VOLUME];

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
cli_cmd_volume_profile_cbk (struct cli_state *state, struct cli_cmd_word *word,
                          const char **words, int wordcount)
{
        int                     sent = 0;
        int                     parse_error = 0;

        int                     ret      = -1;
        rpc_clnt_procedure_t    *proc    = NULL;
        call_frame_t            *frame   = NULL;
        dict_t                  *options = NULL;

        ret = cli_cmd_volume_profile_parse (words, wordcount, &options);

        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_PROFILE_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (options)
                dict_unref (options);

        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume profile failed");
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

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_SET_VOLUME];

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

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_ADD_BRICK];

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
cli_cmd_quota_cbk (struct cli_state *state, struct cli_cmd_word *word,
                   const char **words, int wordcount)
{

        int                      ret       = 0;
        int                      parse_err = 0;
        int32_t                  type      = 0;
        rpc_clnt_procedure_t    *proc      = NULL;
        call_frame_t            *frame     = NULL;
        dict_t                  *options   = NULL;
        gf_answer_t              answer    = GF_ANSWER_NO;
        const char *question = "Disabling quota will delete all the quota "
                               "configuration. Do you want to continue?";

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_QUOTA];
        if (proc == NULL) {
                ret = -1;
                goto out;
        }

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame) {
                ret = -1;
                goto out;
        }

        ret = cli_cmd_quota_parse (words, wordcount, &options);
        if (ret < 0) {
                cli_usage_out (word->pattern);
                parse_err = 1;
                goto out;
        } else if (dict_get_int32 (options, "type", &type) == 0 &&
                   type == GF_QUOTA_OPTION_TYPE_DISABLE) {
                answer = cli_cmd_get_confirmation (state, question);
                if (answer == GF_ANSWER_NO)
                        goto out;
        }

        if (proc->fn)
                ret = proc->fn (frame, THIS, options);

out:
        if (options)
                dict_unref (options);

        if (ret && parse_err == 0)
                cli_out ("Quota command failed");

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
        int                     need_question = 0;

        const char *question = "Removing brick(s) can result in data loss. "
                               "Do you want to Continue?";

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_remove_brick_parse (words, wordcount, &options,
                                                 &need_question);

        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        if (!(state->mode & GLUSTER_MODE_SCRIPT) && need_question) {
                /* we need to ask question only in case of 'commit or force' */
                answer = cli_cmd_get_confirmation (state, question);
                if (GF_ANSWER_NO == answer) {
                        ret = 0;
                        goto out;
                }
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_REMOVE_BRICK];

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
        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_REPLACE_BRICK];

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

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_LOG_FILENAME];

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
cli_cmd_volume_top_cbk (struct cli_state *state, struct cli_cmd_word *word,
                          const char **words, int wordcount)
{

        int                     ret      = -1;
        rpc_clnt_procedure_t    *proc    = NULL;
        call_frame_t            *frame   = NULL;
        dict_t                  *options = NULL;
        int                     sent     = 0;
        int                     parse_error = 0;

        ret = cli_cmd_volume_top_parse (words, wordcount, &options);

        if (ret) {
                parse_error = 1;
                cli_usage_out (word->pattern);
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_TOP_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (options)
                dict_unref (options);

        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume top failed");
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

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_LOG_LOCATE];

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

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_LOG_ROTATE];

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

#if (SYNCDAEMON_COMPILE)
static int
cli_check_gsync_present ()
{
        char                buff[PATH_MAX] = {0, };
        runner_t            runner = {0,};
        char                *ptr = NULL;
        int                 ret = 0;

        ret = setenv ("_GLUSTERD_CALLED_", "1", 1);
        if (-1 == ret) {
                gf_log ("", GF_LOG_WARNING, "setenv syscall failed, hence could"
                        "not assert if geo-replication is installed");
                goto out;
        }

        runinit (&runner);
        runner_add_args (&runner, GSYNCD_PREFIX"/gsyncd", "--version", NULL);
        runner_redir (&runner, STDOUT_FILENO, RUN_PIPE);
        ret = runner_start (&runner);
        if (ret == -1) {
                gf_log ("", GF_LOG_INFO, "geo-replication not installed");
                goto out;
        }

        ptr = fgets(buff, sizeof(buff), runner_chio (&runner, STDOUT_FILENO));
        if (ptr) {
                if (!strstr (buff, "gsyncd")) {
                        ret  = -1;
                        goto out;
                }
        } else {
                ret = -1;
                goto out;
        }

        ret = runner_end (&runner);

        if (ret)
                gf_log ("", GF_LOG_ERROR, "geo-replication not installed");

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret ? -1 : 0;

}

void
cli_cmd_check_gsync_exists_cbk (struct cli_cmd *this)
{

        int             ret = 0;

        ret = cli_check_gsync_present ();
        if (ret)
                this->disable = _gf_true;

}
#endif

int
cli_cmd_volume_gsync_set_cbk (struct cli_state *state, struct cli_cmd_word *word,
                              const char **words, int wordcount)
{
        int                      ret     = 0;
        int                      parse_err = 0;
        dict_t                  *options = NULL;
        rpc_clnt_procedure_t    *proc    = NULL;
        call_frame_t            *frame   = NULL;

        proc = &cli_rpc_prog->proctable [GLUSTER_CLI_GSYNC_SET];
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
                cli_out (GEOREP" command failed");

        return ret;
}

int
cli_cmd_log_level_cbk (struct cli_state *state, struct cli_cmd_word *word,
                       const char **words, int wordcount)
{
        int                   ret         = -1;
        rpc_clnt_procedure_t *proc        = NULL;
        call_frame_t         *frame       = NULL;
        dict_t               *dict        = NULL;

        if (wordcount != 6) {
          cli_usage_out (word->pattern);
          goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_LOG_LEVEL];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
          goto out;

        ret = cli_cmd_log_level_parse (words, wordcount, &dict);
        if (ret)
          goto out;

        if (proc->fn)
          ret = proc->fn (frame, THIS, dict);

 out:
        return ret;
}

int
cli_cmd_volume_status_cbk (struct cli_state *state,
                              struct cli_cmd_word *word,
                              const char **words, int wordcount)
{
        int                   ret         = -1;
        rpc_clnt_procedure_t *proc        = NULL;
        call_frame_t         *frame       = NULL;
        dict_t               *dict        = NULL;

        if (wordcount != 3) {
                cli_usage_out (word->pattern);
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_STATUS_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_status_parse (words, wordcount, &dict);
        if (ret)
                goto out;

        if (proc->fn)
                ret = proc->fn (frame, THIS, dict);

 out:
        return ret;
}


int
cli_print_brick_status (char *brick, int port, int online, int pid)
{
        int  fieldlen = CLI_VOL_STATUS_BRICK_LEN;
        char buf[80] = {0,};
        int  bricklen = 0;
        int  i = 0;
        char *p = NULL;
        int  num_tabs = 0;

        bricklen = strlen (brick);
        p = brick;
        while (bricklen > 0) {
                if (bricklen > fieldlen) {
                        i++;
                        strncpy (buf, p, fieldlen);
                        buf[strlen(buf) + 1] = '\0';
                        cli_out ("%s", buf);
                        p = brick + i * fieldlen;
                        bricklen -= fieldlen;
                } else {
                        num_tabs = (fieldlen - bricklen) / CLI_TAB_LENGTH + 1;
                        printf ("%s", p);
                        while (num_tabs-- != 0)
                                printf ("\t");
                        cli_out ("%d\t%c\t%d", port, online?'Y':'N', pid);
                        bricklen = 0;
                }
        }

        return 0;
}

int
cli_cmd_volume_heal_cbk (struct cli_state *state, struct cli_cmd_word *word,
                          const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        gf1_cli_heal_vol_req    req = {0,};
        int                     sent = 0;
        int                     parse_error = 0;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if (wordcount != 3) {
               cli_usage_out (word->pattern);
                parse_error = 1;
               goto out;
        }

        req.volname = (char *)words[2];
        if (!req.volname)
                goto out;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_HEAL_VOLUME];

        if (proc->fn) {
                ret = proc->fn (frame, THIS, &req);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume heal failed");
        }

        return ret;
}

int
cli_cmd_volume_statedump_cbk (struct cli_state *state, struct cli_cmd_word *word,
                              const char **words, int wordcount)
{
        int                             ret = -1;
        rpc_clnt_procedure_t            *proc = NULL;
        call_frame_t                    *frame = NULL;
        dict_t                          *options = NULL;
        int                             sent = 0;
        int                             parse_error = 0;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if (wordcount < 3) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        if (wordcount > 3) {
               ret = cli_cmd_volume_statedump_options_parse (words, wordcount,
                                                              &options);
               if (ret) {
                       parse_error = 1;
                       gf_log ("cli", GF_LOG_ERROR, "Error parsing "
                               "statedump options");
                       cli_out ("Error parsing options");
                       cli_usage_out (word->pattern);
               }
        } else {
                options = dict_new ();
                if (!options) {
                        ret = -1;
                        gf_log ("cli", GF_LOG_ERROR, "Could not create dict");
                        goto out;
                }
                ret = dict_set_str (options, "options","");
                if (ret)
                        goto out;
                ret = dict_set_int32 (options, "option-cnt", 0);
                if (ret)
                        goto out;
        }

        ret = dict_set_str (options, "volname", (char *)words[2]);
        if (ret)
                goto out;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_STATEDUMP_VOLUME];
        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error = 0))
                        cli_out ("Volume statedump failed");
        }

        return ret;
}


struct cli_cmd volume_cmds[] = {
        { "volume info [all|<VOLNAME>]",
          cli_cmd_volume_info_cbk,
          "list information of all volumes"},

        { "volume create <NEW-VOLNAME> [stripe <COUNT>] [replica <COUNT>] [transport <tcp|rdma|tcp,rdma>] <NEW-BRICK> ...",
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

        { "volume add-brick <VOLNAME> [<stripe|replica> <COUNT>] <NEW-BRICK> ...",
          cli_cmd_volume_add_brick_cbk,
          "add brick to volume <VOLNAME>"},

        { "volume remove-brick <VOLNAME> [replica <COUNT>] <BRICK> ... {start|pause|abort|status|commit|force}",
          cli_cmd_volume_remove_brick_cbk,
          "remove brick from volume <VOLNAME>"},

        { "volume rebalance <VOLNAME> [fix-layout|migrate-data] {start|stop|status} [force]",
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

         { "volume reset <VOLNAME> [option] [force]",
         cli_cmd_volume_reset_cbk,
         "reset all the reconfigured options"},

#if (SYNCDAEMON_COMPILE)
        {"volume "GEOREP" [<VOLNAME>] [<SLAVE-URL>] {start|stop|config|status|log-rotate} [options...]",
         cli_cmd_volume_gsync_set_cbk,
         "Geo-sync operations",
         cli_cmd_check_gsync_exists_cbk},
#endif

         { "volume profile <VOLNAME> {start|info|stop}",
           cli_cmd_volume_profile_cbk,
           "volume profile operations"},

        { "volume quota <VOLNAME> <enable|disable|limit-usage|list|remove> [path] [value]",
          cli_cmd_quota_cbk,
          "quota translator specific operations"},

         { "volume top <VOLNAME> {[open|read|write|opendir|readdir] "
           "|[read-perf|write-perf bs <size> count <count>]} "
           " [brick <brick>] [list-cnt <count>]",
           cli_cmd_volume_top_cbk,
           "volume top operations"},

        {"volume log level <VOLNAME> <XLATOR[*]> <LOGLEVEL>",
         cli_cmd_log_level_cbk,
         "log level for translator"},

        { "volume status <VOLNAME>",
          cli_cmd_volume_status_cbk,
         "display status of specified volume"},

        { "volume heal <VOLNAME>",
          cli_cmd_volume_heal_cbk,
          "Start healing of volume specified by <VOLNAME>"},

        {"volume statedump <VOLNAME> [all|mem|iobuf|callpool|priv|fd|inode]...",
         cli_cmd_volume_statedump_cbk,
         "perform statedump on bricks"},

        { NULL, NULL, NULL }
};

int
cli_cmd_volume_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                      const char **words, int wordcount)
{
        struct cli_cmd        *cmd = NULL;

        for (cmd = volume_cmds; cmd->pattern; cmd++)
                if (_gf_false == cmd->disable)
                        cli_out ("%s - %s", cmd->pattern, cmd->desc);

        return 0;
}

int
cli_cmd_volume_register (struct cli_state *state)
{
        int  ret = 0;
        struct cli_cmd *cmd = NULL;

        for (cmd = volume_cmds; cmd->pattern; cmd++) {

                ret = cli_cmd_register (&state->tree, cmd);
                if (ret)
                        goto out;
        }
out:
        return ret;
}
