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
#include "syscall.h"

extern struct rpc_clnt *global_rpc;
extern struct rpc_clnt *global_quotad_rpc;

extern rpc_clnt_prog_t *cli_rpc_prog;
extern rpc_clnt_prog_t cli_quotad_clnt;

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

        local->get_vol.flags = ctx.flags;
        if (ctx.volname)
                local->get_vol.volname = gf_strdup (ctx.volname);

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

        CLI_STACK_DESTROY (frame);

        return ret;

}

int
cli_cmd_sync_volume_cbk (struct cli_state *state, struct cli_cmd_word *word,
                         const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        int                     sent = 0;
        int                     parse_error = 0;
        dict_t                  *dict = NULL;
        cli_local_t             *local = NULL;
        gf_answer_t             answer = GF_ANSWER_NO;
        const char              *question = "Sync volume may make data "
                                            "inaccessible while the sync "
                                            "is in progress. Do you want "
                                            "to continue?";

        if ((wordcount < 3) || (wordcount > 4)) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        dict = dict_new ();
        if (!dict)
                goto out;

        if ((wordcount == 3) || !strcmp(words[3], "all")) {
                ret = dict_set_int32 (dict, "flags", (int32_t)
                                      GF_CLI_SYNC_ALL);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_ERROR, "failed to set"
                                "flag");
                        goto out;
                }
        } else {
                ret = dict_set_str (dict, "volname", (char *) words[3]);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_ERROR, "failed to set "
                                "volume");
                        goto out;
                }
        }

        ret = dict_set_str (dict, "hostname", (char *) words[2]);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to set hostname");
                goto out;
        }

        if (!(state->mode & GLUSTER_MODE_SCRIPT)) {
                answer = cli_cmd_get_confirmation (state, question);
                if (GF_ANSWER_NO == answer) {
                        ret = 0;
                        goto out;
                }
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_SYNC_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        CLI_LOCAL_INIT (local, words, frame, dict);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume sync failed");
        }

        CLI_STACK_DESTROY (frame);

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
        GF_FREE (brick_list_dup);
        list_for_each_entry (ai_list_tmp1, &ai_list->list, list) {
                if (ai_list_tmp1->info)
                        freeaddrinfo (ai_list_tmp1->info);
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
        cli_local_t             *local = NULL;
        char                    *trans_type = NULL;
        char                    *question = "RDMA transport is"
                                 " recommended only for testing purposes"
                                 " in this release. Do you want to continue?";
        gf_answer_t             answer = GF_ANSWER_NO;

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


        ret = dict_get_str (options, "transport", &trans_type);
        if (ret) {
                gf_log("cli", GF_LOG_ERROR, "Unable to get transport type");
                goto out;
        }

        if (strcasestr (trans_type, "rdma")) {
                answer =
                   cli_cmd_get_confirmation (state, question);
                if (GF_ANSWER_NO == answer) {
                        ret = 0;
                        goto out;
                }
        }

        if (state->mode & GLUSTER_MODE_WIGNORE) {
                ret = dict_set_int32 (options, "force", _gf_true);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to set force "
                                "option");
                        goto out;
                }
        }

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume create failed");
        }

        CLI_STACK_DESTROY (frame);

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
        cli_local_t             *local = NULL;
        dict_t                  *dict = NULL;

        question = "Deleting volume will erase all information about the volume. "
                   "Do you want to continue?";
        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_DELETE_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        dict = dict_new ();
        if (!dict)
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

        ret = dict_set_str (dict, "volname", volname);

        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING, "dict set failed");
                goto out;
        }

        CLI_LOCAL_INIT (local, words, frame, dict);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume delete failed");
        }

        CLI_STACK_DESTROY (frame);

        return ret;
}

int
cli_cmd_volume_start_cbk (struct cli_state *state, struct cli_cmd_word *word,
                          const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        int                     sent = 0;
        int                     parse_error = 0;
        dict_t                  *dict = NULL;
        int                     flags = 0;
        cli_local_t             *local = NULL;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if (wordcount < 3 || wordcount > 4) {
               cli_usage_out (word->pattern);
                parse_error = 1;
               goto out;
        }

        dict = dict_new ();
        if (!dict) {
                goto out;
        }

        if (!words[2])
                goto out;

        ret = dict_set_str (dict, "volname", (char *)words[2]);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "dict set failed");
                goto out;
        }

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
        ret = dict_set_int32 (dict, "flags", flags);
        if (ret) {
                 gf_log (THIS->name, GF_LOG_ERROR,
                         "dict set failed");
                 goto out;
        }

        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "failed to serialize dict");
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_START_VOLUME];

        CLI_LOCAL_INIT (local, words, frame, dict);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume start failed");
        }

        CLI_STACK_DESTROY (frame);

        return ret;
}

gf_answer_t
cli_cmd_get_confirmation (struct cli_state *state, const char *question)
{
        char                    answer[5] = {'\0', };
        char                    flush = '\0';
        size_t			len;

        if (state->mode & GLUSTER_MODE_SCRIPT)
                return GF_ANSWER_YES;

	printf ("%s (y/n) ", question);

        if (fgets (answer, 4, stdin) == NULL) {
                cli_out("gluster cli read error");
                goto out;
        }

	len = strlen (answer);

	if (len && answer [len - 1] == '\n'){
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
        gf_answer_t             answer = GF_ANSWER_NO;
        int                     sent = 0;
        int                     parse_error = 0;
        dict_t                  *dict = NULL;
        char                    *volname = NULL;
        cli_local_t             *local = NULL;

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

        volname = (char*) words[2];

        dict = dict_new ();
        ret = dict_set_str (dict, "volname", volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "dict set failed");
                goto out;
        }

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
        ret = dict_set_int32 (dict, "flags", flags);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "dict set failed");
                goto out;
        }

        answer = cli_cmd_get_confirmation (state, question);

        if (GF_ANSWER_NO == answer) {
                ret = 0;
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_STOP_VOLUME];

        CLI_LOCAL_INIT (local, words, frame, dict);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume stop on '%s' failed", volname);
        }

        CLI_STACK_DESTROY (frame);

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

        CLI_STACK_DESTROY (frame);

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
        cli_local_t          *local = NULL;
#ifdef GF_SOLARIS_HOST_OS
        cli_out ("Command not supported on Solaris");
        goto out;
#endif

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_defrag_parse (words, wordcount, &dict);

        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_DEFRAG_VOLUME];

        CLI_LOCAL_INIT (local, words, frame, dict);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume rebalance failed");
        }

        CLI_STACK_DESTROY (frame);

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
        cli_local_t             *local = NULL;

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

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume reset failed");
        }

        CLI_STACK_DESTROY (frame);

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
        cli_local_t             *local = NULL;

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

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume profile failed");
        }

        CLI_STACK_DESTROY (frame);

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
        cli_local_t             *local = NULL;
        char                    *op_errstr = NULL;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_SET_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_set_parse (words, wordcount, &options, &op_errstr);
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
                        cli_out ("Volume set failed");
        }

        CLI_STACK_DESTROY (frame);

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
        gf_answer_t             answer = GF_ANSWER_NO;
        cli_local_t             *local = NULL;

        const char *question = "Changing the 'stripe count' of the volume is "
                "not a supported feature. In some cases it may result in data "
                "loss on the volume. Also there may be issues with regular "
                "filesystem operations on the volume after the change. Do you "
                "really want to continue with 'stripe' count option ? ";

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_add_brick_parse (words, wordcount, &options);
        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        /* TODO: there are challenges in supporting changing of
           stripe-count, untill it is properly supported give warning to user */
        if (dict_get (options, "stripe-count")) {
                answer = cli_cmd_get_confirmation (state, question);

                if (GF_ANSWER_NO == answer) {
                        ret = 0;
                        goto out;
                }
        }

        if (state->mode & GLUSTER_MODE_WIGNORE) {
                ret = dict_set_int32 (options, "force", _gf_true);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to set force "
                                "option");
                        goto out;
                }
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_ADD_BRICK];

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume add-brick failed");
        }

        CLI_STACK_DESTROY (frame);

        return ret;
}

static int
gf_cli_create_auxiliary_mount (char *volname)
{
        int      ret                     = -1;
        char     mountdir[PATH_MAX]      = {0,};
        char     pidfile_path[PATH_MAX]  = {0,};
        char     logfile[PATH_MAX]       = {0,};

        GLUSTERFS_GET_AUX_MOUNT_PIDFILE (pidfile_path, volname);

        if (gf_is_service_running (pidfile_path, NULL)) {
                gf_log ("cli", GF_LOG_DEBUG, "Aux mount of volume %s is running"
                        " already", volname);
                ret = 0;
                goto out;
        }

        GLUSTERD_GET_QUOTA_AUX_MOUNT_PATH (mountdir, volname, "/");
        ret = mkdir (mountdir, 0777);
        if (ret && errno != EEXIST) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to create auxiliary mount "
                        "directory %s. Reason : %s", mountdir,
                        strerror (errno));
                goto out;
        }

        snprintf (logfile, PATH_MAX-1, "%s/quota-mount-%s.log",
                  DEFAULT_LOG_FILE_DIRECTORY, volname);

        ret = runcmd (SBIN_DIR"/glusterfs",
                      "-s", "localhost",
                      "--volfile-id", volname,
                      "-l", logfile,
                      "-p", pidfile_path,
                      mountdir,
                      "--client-pid", "-42", NULL);

        if (ret) {
                gf_log ("cli", GF_LOG_WARNING, "failed to mount glusterfs "
                        "client. Please check the log file %s for more details",
                        logfile);
                ret = -1;
                goto out;
        }

        ret = 0;

out:
        return ret;
}

static int
cli_stage_quota_op (char *volname, int op_code)
{
        int ret = -1;

        switch (op_code) {
                case GF_QUOTA_OPTION_TYPE_ENABLE:
                case GF_QUOTA_OPTION_TYPE_LIMIT_USAGE:
                case GF_QUOTA_OPTION_TYPE_REMOVE:
                case GF_QUOTA_OPTION_TYPE_LIST:
                        ret = gf_cli_create_auxiliary_mount (volname);
                        if (ret) {
                                cli_err ("quota: Could not start quota "
                                         "auxiliary mount");
                                goto out;
                        }
                        ret = 0;
                        break;

                default:
                        ret = 0;
                        break;
        }

out:
        return ret;
}

static void
print_quota_list_header (void)
{
        //Header
        cli_out ("                  Path                   Hard-limit "
                 "Soft-limit   Used  Available");
        cli_out ("-----------------------------------------------------"
                 "---------------------------");
}

int
cli_get_soft_limit (dict_t *options, const char **words, dict_t *xdata)
{
        call_frame_t            *frame          = NULL;
        cli_local_t             *local          = NULL;
        rpc_clnt_procedure_t    *proc           = NULL;
        char                    *default_sl     = NULL;
        char                    *default_sl_dup = NULL;
        int                      ret  = -1;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame) {
                ret = -1;
                goto out;
        }

        //We need a ref on @options to prevent CLI_STACK_DESTROY
        //from destroying it prematurely.
        dict_ref (options);
        CLI_LOCAL_INIT (local, words, frame, options);
        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_QUOTA];
        ret = proc->fn (frame, THIS, options);

        ret = dict_get_str (options, "default-soft-limit", &default_sl);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get default soft limit");
                goto out;
        }

        default_sl_dup = gf_strdup (default_sl);
        if (!default_sl_dup) {
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (xdata, "default-soft-limit", default_sl_dup);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to set default soft limit");
                GF_FREE (default_sl_dup);
                goto out;
        }

out:
        CLI_STACK_DESTROY (frame);
        return ret;
}

#define QUOTA_CONF_HEADER                                                \
        "GlusterFS Quota conf | version: v%d.%d\n"
int
cli_cmd_quota_conf_skip_header (int fd)
{
        char buf[PATH_MAX] = {0,};

        snprintf (buf, sizeof(buf)-1, QUOTA_CONF_HEADER, 1, 1);
        return gf_skip_header_section (fd, strlen (buf));
}

/* Checks if at least one limit has been set on the volume
 *
 * Returns true if at least one limit is set. Returns false otherwise.
 */
gf_boolean_t
_limits_set_on_volume (char *volname) {
        gf_boolean_t    limits_set = _gf_false;
        int             ret = -1;
        char            quota_conf_file[PATH_MAX] = {0,};
        int             fd = -1;
        char            buf[16] = {0,};

        /* TODO: fix hardcoding; Need to perform an RPC call to glusterd
         * to fetch working directory
         */
        sprintf (quota_conf_file, "/var/lib/glusterd/vols/%s/quota.conf",
                 volname);
        fd = open (quota_conf_file, O_RDONLY);
        if (fd == -1)
                goto out;

        ret = cli_cmd_quota_conf_skip_header (fd);
        if (ret)
                goto out;

        /* Try to read atleast one gfid */
        ret = read (fd, (void *)buf, 16);
        if (ret == 16)
                limits_set = _gf_true;
out:
        if (fd != -1)
                close (fd);
        return limits_set;
}

/* Checks if the mount is connected to the bricks
 *
 * Returns true if connected and false if not
 */
gf_boolean_t
_quota_aux_mount_online (char *volname)
{
        int         ret = 0;
        char        mount_path[PATH_MAX + 1] = {0,};
        struct stat buf = {0,};

        GF_ASSERT (volname);

        /* Try to create the aux mount before checking if bricks are online */
        ret = gf_cli_create_auxiliary_mount (volname);
        if (ret) {
                cli_err ("quota: Could not start quota auxiliary mount");
                return _gf_false;
        }

        GLUSTERD_GET_QUOTA_AUX_MOUNT_PATH (mount_path, volname, "/");

        ret = sys_stat (mount_path, &buf);
        if (ret) {
                if (ENOTCONN == errno) {
                        cli_err ("quota: Cannot connect to bricks. Check if "
                                 "bricks are online.");
                } else {
                        cli_err ("quota: Error on quota auxiliary mount (%s).",
                                 strerror (errno));
                }
                return _gf_false;
        }
        return _gf_true;
}

int
cli_cmd_quota_handle_list_all (const char **words, dict_t *options)
{
        int                      all_failed = 1;
        int                      count      = 0;
        int                      ret        = -1;
        rpc_clnt_procedure_t    *proc       = NULL;
        cli_local_t             *local      = NULL;
        call_frame_t            *frame      = NULL;
        dict_t                  *xdata      = NULL;
        char                    *gfid_str   = NULL;
        char                    *volname    = NULL;
        char                    *volname_dup = NULL;
        unsigned char            buf[16]   = {0};
        int                      fd        = -1;
        char                     quota_conf_file[PATH_MAX] = {0};

        xdata = dict_new ();
        if (!xdata) {
                ret = -1;
                goto out;
        }

        ret = dict_get_str (options, "volname", &volname);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get volume name");
                goto out;
        }

        ret = cli_get_soft_limit (options, words, xdata);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to fetch default "
                        "soft-limit");
                goto out;
        }

        /* Check if at least one limit is set on volume. No need to check for
         * quota enabled as cli_get_soft_limit() handles that
         */
        if (!_limits_set_on_volume (volname)) {
                cli_out ("quota: No quota configured on volume %s", volname);
                ret = 0;
                goto out;
        }

        /* Check if the mount is online before doing any listing */
        if (!_quota_aux_mount_online (volname)) {
                ret = -1;
                goto out;
        }

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame) {
                ret = -1;
                goto out;
        }

        volname_dup = gf_strdup (volname);
        if (!volname_dup) {
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (xdata, "volume-uuid", volname_dup);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to set volume-uuid");
                GF_FREE (volname_dup);
                goto out;
        }

        //TODO: fix hardcoding; Need to perform an RPC call to glusterd
        //to fetch working directory
        sprintf (quota_conf_file, "/var/lib/glusterd/vols/%s/quota.conf",
                 volname);
        fd = open (quota_conf_file, O_RDONLY);
        if (fd == -1) {
                //This may because no limits were yet set on the volume
                gf_log ("cli", GF_LOG_TRACE, "Unable to open "
                        "quota.conf");
                ret = 0;
                goto out;
         }

        ret = cli_cmd_quota_conf_skip_header (fd);
        if (ret) {
                goto out;
        }
        CLI_LOCAL_INIT (local, words, frame, xdata);
        proc = &cli_quotad_clnt.proctable[GF_AGGREGATOR_GETLIMIT];

        print_quota_list_header ();
        gfid_str = GF_CALLOC (1, gf_common_mt_char, 64);
        if (!gfid_str) {
                ret = -1;
                goto out;
        }
        for (count = 0;; count++) {
                ret = read (fd, (void*) buf, 16);
                if (ret <= 0) {
                        //Finished reading all entries in the conf file
                        break;
                }
                if (ret < 16) {
                        //This should never happen. We must have a multiple of
                        //entry_sz bytes in our configuration file.
                        gf_log (THIS->name, GF_LOG_CRITICAL, "Quota "
                                "configuration store may be corrupt.");
                        goto out;
                }
                uuid_utoa_r (buf, gfid_str);
                ret = dict_set_str (xdata, "gfid", gfid_str);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to set gfid");
                        goto out;
                }

                ret = proc->fn (frame, THIS, xdata);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to get quota "
                                "limits for %s", uuid_utoa ((unsigned char*)buf));
                }

                dict_del (xdata, "gfid");
                all_failed = all_failed && ret;
        }

        if (count > 0) {
                ret = all_failed? -1: 0;
        } else {
                ret = 0;
        }
out:
        if (fd != -1) {
                close (fd);
        }

        GF_FREE (gfid_str);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Could not fetch and display quota"
                        " limits");
        }
        CLI_STACK_DESTROY (frame);
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
        cli_local_t             *local     = NULL;
        int                      sent      = 0;
        char                    *volname   = NULL;
        const char *question = "Disabling quota will delete all the quota "
                               "configuration. Do you want to continue?";

        //parse **words into options dictionary
        ret = cli_cmd_quota_parse (words, wordcount, &options);
        if (ret < 0) {
                cli_usage_out (word->pattern);
                parse_err = 1;
                goto out;
        }

        ret = dict_get_int32 (options, "type", &type);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get opcode");
                goto out;
        }

        //handle quota-disable and quota-list-all different from others
        switch (type) {
        case GF_QUOTA_OPTION_TYPE_DISABLE:
                answer = cli_cmd_get_confirmation (state, question);
                if (answer == GF_ANSWER_NO)
                        goto out;
                break;
        case GF_QUOTA_OPTION_TYPE_LIST:
                if (wordcount != 4)
                        break;
                ret = cli_cmd_quota_handle_list_all (words, options);
                goto out;
        default:
                break;
        }

        ret = dict_get_str (options, "volname", &volname);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get volume name");
                goto out;
        }

        //create auxillary mount need for quota commands that operate on path
        ret = cli_stage_quota_op (volname, type);
        if (ret)
                goto out;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame) {
                ret = -1;
                goto out;
        }

        CLI_LOCAL_INIT (local, words, frame, options);
        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_QUOTA];
        if (proc == NULL) {
                ret = -1;
                goto out;
        }

        if (proc->fn)
                ret = proc->fn (frame, THIS, options);

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if (sent == 0 && parse_err == 0)
                        cli_out ("Quota command failed. Please check the cli "
                                 "logs for more details");
        }

        CLI_STACK_DESTROY (frame);
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
        cli_local_t             *local = NULL;

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

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume remove-brick failed");
        }

        CLI_STACK_DESTROY (frame);

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
        cli_local_t             *local = NULL;
        int                     replace_op = 0;
        char                    *q = "All replace-brick commands except "
                                     "commit force are deprecated. "
                                     "Do you want to continue?";
        gf_answer_t             answer = GF_ANSWER_NO;

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

        ret = dict_get_int32 (options, "operation", &replace_op);
        if (replace_op != GF_REPLACE_OP_COMMIT_FORCE) {
                answer = cli_cmd_get_confirmation (state, q);
                if (GF_ANSWER_NO == answer) {
                        ret = 0;
                        goto out;
                }
        }

        if (state->mode & GLUSTER_MODE_WIGNORE) {
                ret = dict_set_int32 (options, "force", _gf_true);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to set force"
                                "option");
                        goto out;
                }
        }

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume replace-brick failed");
        }

        CLI_STACK_DESTROY (frame);

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
cli_cmd_volume_top_cbk (struct cli_state *state, struct cli_cmd_word *word,
                          const char **words, int wordcount)
{

        int                     ret      = -1;
        rpc_clnt_procedure_t    *proc    = NULL;
        call_frame_t            *frame   = NULL;
        dict_t                  *options = NULL;
        int                     sent     = 0;
        int                     parse_error = 0;
        cli_local_t             *local = NULL;

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

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume top failed");
        }

        CLI_STACK_DESTROY (frame);

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
        cli_local_t             *local = NULL;

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

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume log rotate failed");
        }
        CLI_STACK_DESTROY (frame);

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
        cli_local_t             *local   = NULL;

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

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn)
                ret = proc->fn (frame, THIS, options);

out:
        if (ret && parse_err == 0)
                cli_out (GEOREP" command failed");

        CLI_STACK_DESTROY (frame);

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
        uint32_t              cmd         = 0;
        cli_local_t          *local       = NULL;

        ret = cli_cmd_volume_status_parse (words, wordcount, &dict);

        if (ret) {
                cli_usage_out (word->pattern);
                goto out;
        }

        ret = dict_get_uint32 (dict, "cmd", &cmd);
        if (ret)
                goto out;

        if (!(cmd & GF_CLI_STATUS_ALL)) {
                /* for one volume or brick */
                proc = &cli_rpc_prog->proctable[GLUSTER_CLI_STATUS_VOLUME];
        } else {
                /* volume status all or all detail */
                proc = &cli_rpc_prog->proctable[GLUSTER_CLI_STATUS_ALL];
        }

        if (!proc->fn)
                goto out;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        CLI_LOCAL_INIT (local, words, frame, dict);

        ret = proc->fn (frame, THIS, dict);

out:
        CLI_STACK_DESTROY (frame);

        return ret;
}


int
cli_get_detail_status (dict_t *dict, int i, cli_volume_status_t *status)
{
        uint64_t                   free            = 0;
        uint64_t                   total           = 0;
        char                       key[1024]       = {0};
        int                        ret             = 0;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.free", i);
        ret = dict_get_uint64 (dict, key, &free);

        status->free = gf_uint64_2human_readable (free);
        if (!status->free)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.total", i);
        ret = dict_get_uint64 (dict, key, &total);

        status->total = gf_uint64_2human_readable (total);
        if (!status->total)
                goto out;

#ifdef GF_LINUX_HOST_OS
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.device", i);
        ret = dict_get_str (dict, key, &(status->device));
        if (ret)
                status->device = NULL;
#endif

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.block_size", i);
        ret = dict_get_uint64 (dict, key, &(status->block_size));
        if (ret) {
                ret = 0;
                status->block_size = 0;
        }

#ifdef GF_LINUX_HOST_OS
        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.mnt_options", i);
        ret = dict_get_str (dict, key, &(status->mount_options));
        if (ret)
                status->mount_options = NULL;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.fs_name", i);
        ret = dict_get_str (dict, key, &(status->fs_name));
        if (ret) {
                ret = 0;
                status->fs_name = NULL;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.inode_size", i);
        ret = dict_get_str (dict, key, &(status->inode_size));
        if (ret)
                status->inode_size = NULL;
#endif /* GF_LINUX_HOST_OS */

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.total_inodes", i);
        ret = dict_get_uint64 (dict, key,
                        &(status->total_inodes));
        if (ret)
                status->total_inodes = 0;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.free_inodes", i);
        ret = dict_get_uint64 (dict, key, &(status->free_inodes));
        if (ret) {
                ret = 0;
                status->free_inodes = 0;
        }


 out:
        return ret;
}

void
cli_print_detailed_status (cli_volume_status_t *status)
{
        cli_out ("%-20s : %-20s", "Brick", status->brick);
        if (status->online)
                cli_out ("%-20s : %-20d", "Port", status->port);
        else
                cli_out ("%-20s : %-20s", "Port", "N/A");
        cli_out ("%-20s : %-20c", "Online", (status->online) ? 'Y' : 'N');
        cli_out ("%-20s : %-20s", "Pid", status->pid_str);

#ifdef GF_LINUX_HOST_OS
        if (status->fs_name)
                cli_out ("%-20s : %-20s", "File System", status->fs_name);
        else
                cli_out ("%-20s : %-20s", "File System", "N/A");

        if (status->device)
                cli_out ("%-20s : %-20s", "Device", status->device);
        else
                cli_out ("%-20s : %-20s", "Device", "N/A");

        if (status->mount_options) {
                cli_out ("%-20s : %-20s", "Mount Options",
                         status->mount_options);
        } else {
                cli_out ("%-20s : %-20s", "Mount Options", "N/A");
        }

        if (status->inode_size) {
                cli_out ("%-20s : %-20s", "Inode Size",
                         status->inode_size);
        } else {
                cli_out ("%-20s : %-20s", "Inode Size", "N/A");
        }
#endif
        if (status->free)
                cli_out ("%-20s : %-20s", "Disk Space Free", status->free);
        else
                cli_out ("%-20s : %-20s", "Disk Space Free", "N/A");

        if (status->total)
                cli_out ("%-20s : %-20s", "Total Disk Space", status->total);
        else
                cli_out ("%-20s : %-20s", "Total Disk Space", "N/A");


        if (status->total_inodes) {
                cli_out ("%-20s : %-20ld", "Inode Count",
                         status->total_inodes);
        } else {
                cli_out ("%-20s : %-20s", "Inode Count", "N/A");
        }

        if (status->free_inodes) {
                cli_out ("%-20s : %-20ld", "Free Inodes",
                         status->free_inodes);
        } else {
                cli_out ("%-20s : %-20s", "Free Inodes", "N/A");
        }
}

int
cli_print_brick_status (cli_volume_status_t *status)
{
        int  fieldlen = CLI_VOL_STATUS_BRICK_LEN;
        int  bricklen = 0;
        char *p = NULL;
        int  num_tabs = 0;

        p = status->brick;
        bricklen = strlen (p);
        while (bricklen > 0) {
                if (bricklen > fieldlen) {
                        cli_out ("%.*s", fieldlen, p);
                        p += fieldlen;
                        bricklen -= fieldlen;
                } else {
                        num_tabs = (fieldlen - bricklen) / CLI_TAB_LENGTH + 1;
                        printf ("%s", p);
                        while (num_tabs-- != 0)
                                printf ("\t");
                        if (status->port) {
                                if (status->online)
                                        cli_out ("%d\t%c\t%s",
                                                 status->port,
                                                 status->online?'Y':'N',
                                                 status->pid_str);
                                else
                                        cli_out ("%s\t%c\t%s",
                                                 "N/A",
                                                 status->online?'Y':'N',
                                                 status->pid_str);
                        }
                        else
                                cli_out ("%s\t%c\t%s",
                                         "N/A", status->online?'Y':'N',
                                         status->pid_str);
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
        int                     sent = 0;
        int                     parse_error = 0;
        dict_t                  *options = NULL;
        xlator_t                *this = NULL;
        cli_local_t             *local = NULL;

        this = THIS;
        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                goto out;

        if (wordcount < 3) {
               cli_usage_out (word->pattern);
               parse_error = 1;
               goto out;
        }

        ret = cli_cmd_volume_heal_options_parse (words, wordcount, &options);
        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_HEAL_VOLUME];

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume heal failed");
        }

        CLI_STACK_DESTROY (frame);

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
        cli_local_t                     *local = NULL;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if (wordcount < 3) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        if (wordcount >= 3) {
               ret = cli_cmd_volume_statedump_options_parse (words, wordcount,
                                                              &options);
               if (ret) {
                       parse_error = 1;
                       gf_log ("cli", GF_LOG_ERROR, "Error parsing "
                               "statedump options");
                       cli_out ("Error parsing options");
                       cli_usage_out (word->pattern);
               }
        }

        ret = dict_set_str (options, "volname", (char *)words[2]);
        if (ret)
                goto out;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_STATEDUMP_VOLUME];

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error = 0))
                        cli_out ("Volume statedump failed");
        }

        CLI_STACK_DESTROY (frame);

        return ret;
}

int
cli_cmd_volume_list_cbk (struct cli_state *state, struct cli_cmd_word *word,
                         const char **words, int wordcount)
{
        int                     ret = -1;
        call_frame_t            *frame = NULL;
        rpc_clnt_procedure_t    *proc = NULL;
        int                     sent = 0;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_LIST_VOLUME];
        if (proc->fn) {
                ret = proc->fn (frame, THIS, NULL);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if (sent == 0)
                        cli_out ("Volume list failed");
        }

        CLI_STACK_DESTROY (frame);

        return ret;
}

int
cli_cmd_volume_clearlocks_cbk (struct cli_state *state,
                               struct cli_cmd_word *word,
                               const char **words, int wordcount)
{
        int                             ret = -1;
        rpc_clnt_procedure_t            *proc = NULL;
        call_frame_t                    *frame = NULL;
        dict_t                          *options = NULL;
        int                             sent = 0;
        int                             parse_error = 0;
        cli_local_t                     *local = NULL;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if (wordcount < 7 || wordcount > 8) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

       ret = cli_cmd_volume_clrlks_opts_parse (words, wordcount, &options);
       if (ret) {
               parse_error = 1;
               gf_log ("cli", GF_LOG_ERROR, "Error parsing "
                       "clear-locks options");
               cli_out ("Error parsing options");
               cli_usage_out (word->pattern);
       }

        ret = dict_set_str (options, "volname", (char *)words[2]);
        if (ret)
                goto out;

        ret = dict_set_str (options, "path", (char *)words[3]);
        if (ret)
                goto out;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_CLRLOCKS_VOLUME];

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error = 0))
                        cli_out ("Volume clear-locks failed");
        }

        CLI_STACK_DESTROY (frame);

        return ret;
}

struct cli_cmd volume_cmds[] = {
        { "volume info [all|<VOLNAME>]",
          cli_cmd_volume_info_cbk,
          "list information of all volumes"},

        { "volume create <NEW-VOLNAME> [stripe <COUNT>] [replica <COUNT>] "
          "[transport <tcp|rdma|tcp,rdma>] <NEW-BRICK>"
#ifdef HAVE_BD_XLATOR
          "?<vg_name>"
#endif
          "... [force]",

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

        { "volume add-brick <VOLNAME> [<stripe|replica> <COUNT>] <NEW-BRICK> ... [force]",
          cli_cmd_volume_add_brick_cbk,
          "add brick to volume <VOLNAME>"},

        { "volume remove-brick <VOLNAME> [replica <COUNT>] <BRICK> ... [start|stop|status|commit|force]",
          cli_cmd_volume_remove_brick_cbk,
          "remove brick from volume <VOLNAME>"},

        { "volume rebalance <VOLNAME> [fix-layout] {start|stop|status} [force]",
          cli_cmd_volume_defrag_cbk,
          "rebalance operations"},

        { "volume replace-brick <VOLNAME> <BRICK> <NEW-BRICK> {start [force]|pause|abort|status|commit [force]}",
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
        {"volume "GEOREP" [<VOLNAME>] [<SLAVE-URL>] {create [push-pem] [force]"
         "|start [force]|stop [force]|config|status [detail]|delete} [options...]",
         cli_cmd_volume_gsync_set_cbk,
         "Geo-sync operations",
         cli_cmd_check_gsync_exists_cbk},
#endif

         { "volume profile <VOLNAME> {start|info [peek|incremental [peek]|cumulative|clear]|stop} [nfs]",
           cli_cmd_volume_profile_cbk,
           "volume profile operations"},

        { "volume quota <VOLNAME> {enable|disable|list [<path> ...]|remove <path>| default-soft-limit <percent>} |\n"
          "volume quota <VOLNAME> {limit-usage <path> <size> [<percent>]} |\n"
          "volume quota <VOLNAME> {alert-time|soft-timeout|hard-timeout} {<time>}",
          cli_cmd_quota_cbk,
          "quota translator specific operations"},

         { "volume top <VOLNAME> {open|read|write|opendir|readdir|clear} [nfs|brick <brick>] [list-cnt <value>] |\n"
           "volume top <VOLNAME> {read-perf|write-perf} [bs <size> count <count>] [brick <brick>] [list-cnt <value>]",
           cli_cmd_volume_top_cbk,
           "volume top operations"},

        { "volume status [all | <VOLNAME> [nfs|shd|<BRICK>|quotad]]"
          " [detail|clients|mem|inode|fd|callpool|tasks]",
          cli_cmd_volume_status_cbk,
          "display status of all or specified volume(s)/brick"},

        { "volume heal <VOLNAME> [{full | statistics {heal-count {replica <hostname:brickname>}} |info {healed | heal-failed | split-brain}}]",
          cli_cmd_volume_heal_cbk,
          "self-heal commands on volume specified by <VOLNAME>"},

        {"volume statedump <VOLNAME> [nfs|quotad] [all|mem|iobuf|callpool|priv|fd|"
         "inode|history]...",
         cli_cmd_volume_statedump_cbk,
         "perform statedump on bricks"},

        {"volume list",
         cli_cmd_volume_list_cbk,
         "list all volumes in cluster"},

        {"volume clear-locks <VOLNAME> <path> kind {blocked|granted|all}"
          "{inode [range]|entry [basename]|posix [range]}",
          cli_cmd_volume_clearlocks_cbk,
          "Clear locks held on path"
        },

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
