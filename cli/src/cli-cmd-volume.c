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
#include <sys/wait.h>
#include <netinet/in.h>

#include "cli.h"
#include "cli-cmd.h"
#include "cli-mem-types.h"
#include "cli1-xdr.h"
#include "run.h"
#include "syscall.h"
#include "common-utils.h"
#include "events.h"

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
                if (strlen (ctx.volname) > GD_VOLUME_NAME_MAX) {
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
        cli_local_t             *local = NULL;
        char                    *trans_type = NULL;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_CREATE_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_create_parse (state, words, wordcount, &options);

        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        ret = dict_get_str (options, "transport", &trans_type);
        if (ret) {
                gf_log("cli", GF_LOG_ERROR, "Unable to get transport type");
                goto out;
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
        if (ret == 0) {
                gf_event (EVENT_VOLUME_CREATE, "name=%s", (char *)words[2]);
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

        volname = (char *)words[2];

        ret = dict_set_str (dict, "volname", volname);
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING, "dict set failed");
                goto out;
        }

        if (!strcmp (volname, GLUSTER_SHARED_STORAGE)) {
                question = "Deleting the shared storage volume"
                           "(gluster_shared_storage), will affect features "
                           "like snapshot scheduler, geo-replication "
                           "and NFS-Ganesha. Do you still want to "
                           "continue?";
        }

        answer = cli_cmd_get_confirmation (state, question);
        if (GF_ANSWER_NO == answer) {
                ret = 0;
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

        if (ret == 0 && GF_ANSWER_YES == answer) {
                gf_event (EVENT_VOLUME_DELETE, "name=%s", (char *)words[2]);
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

        if (ret == 0) {
                gf_event (EVENT_VOLUME_START, "name=%s;force=%d",
                          (char *)words[2], (flags & GF_CLI_FLAG_OP_FORCE));
        }

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

        if (!strcmp (volname, GLUSTER_SHARED_STORAGE)) {
                question = "Stopping the shared storage volume"
                           "(gluster_shared_storage), will affect features "
                           "like snapshot scheduler, geo-replication "
                           "and NFS-Ganesha. Do you still want to "
                           "continue?";
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

        if (ret == 0 && GF_ANSWER_YES == answer) {
                gf_event (EVENT_VOLUME_STOP, "name=%s;force=%d",
                          (char *)words[2], (flags & GF_CLI_FLAG_OP_FORCE));
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
                dict_unref (dict);

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
#if (USE_EVENTS)
        eventtypes_t         event = EVENT_LAST;
#endif

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
        } else {

#if (USE_EVENTS)
                if (!(strcmp (words[wordcount-1], "start")) ||
                        !(strcmp (words[wordcount-1], "force")))  {
                        event = EVENT_VOLUME_REBALANCE_START;
                } else if (!strcmp (words[wordcount-1], "stop")) {
                        event = EVENT_VOLUME_REBALANCE_STOP;
                }

                if (event != EVENT_LAST)
                        gf_event (event, "volume=%s", (char *)words[2]);
#endif
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
#if (USE_EVENTS)
        int                      ret1    = -1;
        char                    *tmp_opt = NULL;
#endif

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

#if (USE_EVENTS)
        if (ret == 0) {
                ret1 = dict_get_str (options, "key", &tmp_opt);
                if (ret1)
                        tmp_opt = "";

                gf_event (EVENT_VOLUME_RESET, "name=%s;option=%s",
                          (char *)words[2],
                          tmp_opt);
        }
#endif

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

#if (USE_EVENTS)
        int                      ret1          = -1;
        int                      i             = 1;
        char                     dict_key[50]  = {0,};
        char                    *tmp_opt       = NULL;
        char                    *opts_str      = NULL;
        int                      num_options   = 0;
#endif

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_SET_VOLUME];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_set_parse (state, words, wordcount,
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
                        cli_out ("Volume set failed");
        }

#if (USE_EVENTS)
        if (ret == 0 && strcmp(words[2], "help") != 0) {
                ret1 = dict_get_int32 (options, "count", &num_options);
                if (ret1)
                        num_options = 0;
                else
                        num_options = num_options/2;

                /* Initialize opts_str */
                opts_str = gf_strdup ("");

                /* Prepare String in format options=KEY1,VALUE1,KEY2,VALUE2 */
                for (i = 1; i <= num_options; i++) {
                        sprintf (dict_key, "key%d", i);
                        ret1 = dict_get_str (options, dict_key, &tmp_opt);
                        if (ret1)
                                tmp_opt = "";

                        gf_asprintf (&opts_str, "%s,%s", opts_str, tmp_opt);

                        sprintf (dict_key, "value%d", i);
                        ret1 = dict_get_str (options, dict_key, &tmp_opt);
                        if (ret1)
                                tmp_opt = "";

                        gf_asprintf (&opts_str, "%s,%s", opts_str, tmp_opt);
                }

                gf_event (EVENT_VOLUME_SET, "name=%s;options=%s",
                          (char *)words[2],
                          opts_str);

                /* Allocated by gf_strdup and gf_asprintf */
                GF_FREE (opts_str);
        }
#endif

        CLI_STACK_DESTROY (frame);

        return ret;

}

static
int
cli_event_remove_brick_str (dict_t *options, char **event_str,
                            eventtypes_t *event)
{
        int           ret             = -1;
        char          *bricklist      = NULL;
        char          *brick          = NULL;
        char          *volname        = NULL;
        char           key[256]       = {0,};
        const char    *eventstrformat = "volume=%s;bricks=%s";
        int32_t        command        = 0;
        int32_t        i              = 1;
        int32_t        count          = 0;
        int32_t        eventstrlen    = 1;
        char          *tmp_ptr        = NULL;

        if (!options || !event_str || !event)
                goto out;

        ret = dict_get_str (options, "volname", &volname);
        if (ret || !volname) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to fetch volname");
                ret = -1;
                goto out;
        }
        /* Get the list of bricks for the event */
        ret = dict_get_int32 (options, "command", &command);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to fetch command");
                ret = -1;
                goto out;
        }

        switch (command) {
        case GF_OP_CMD_START:
                *event = EVENT_VOLUME_REMOVE_BRICK_START;
                break;
        case GF_OP_CMD_COMMIT:
                *event = EVENT_VOLUME_REMOVE_BRICK_COMMIT;
                break;
        case GF_OP_CMD_COMMIT_FORCE:
                *event = EVENT_VOLUME_REMOVE_BRICK_FORCE;
                break;
        case GF_OP_CMD_STOP:
                *event = EVENT_VOLUME_REMOVE_BRICK_STOP;
                break;
        default:
                *event = EVENT_LAST;
                break;
        }

        ret = -1;

        if (*event == EVENT_LAST) {
                goto out;
        }

        /* I could just get this from words[] but this is cleaner in case the
         * format changes  */
        while (i) {
                snprintf (key, sizeof (key), "brick%d", i);
                ret = dict_get_str (options, key, &brick);
                if (ret) {
                        break;
                }
                eventstrlen += strlen (brick) + 1;
                i++;
        }

        count = --i;

        eventstrlen += 1;

        bricklist = GF_CALLOC (eventstrlen, sizeof (char), gf_common_mt_char);
        if (!bricklist) {
                goto out;
        }

        tmp_ptr = bricklist;

        i = 1;
        while (i <= count) {
                snprintf (key, sizeof (key), "brick%d", i);
                ret = dict_get_str (options, key, &brick);
                if (ret) {
                        break;
                }
                snprintf (tmp_ptr, eventstrlen, "%s ", brick);
                eventstrlen -= (strlen (brick) + 1);
                tmp_ptr += (strlen (brick) + 1);
                i++;
        }

        if (!ret) {
                gf_asprintf (event_str, eventstrformat, volname,
                             bricklist);
        } else {
                gf_asprintf (event_str, eventstrformat, volname,
                             "<unavailable>");
        }

        ret = 0;
out:
        GF_FREE (bricklist);
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

#if (USE_EVENTS)
        char                    *event_str = NULL;
        char                    *bricks = NULL;
        const char              *eventstrformat = "volume=%s;bricks=%s";
#endif

        const char *question = "Changing the 'stripe count' of the volume is "
                "not a supported feature. In some cases it may result in data "
                "loss on the volume. Also there may be issues with regular "
                "filesystem operations on the volume after the change. Do you "
                "really want to continue with 'stripe' count option ? ";

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_add_brick_parse (words, wordcount, &options, 0);
        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        /* TODO: there are challenges in supporting changing of
           stripe-count, until it is properly supported give warning to user */
        if (dict_get (options, "stripe-count")) {
                answer = cli_cmd_get_confirmation (state, question);

                if (GF_ANSWER_NO == answer) {
                        ret = 0;
                        goto out;
                }
        }

#if (USE_EVENTS)
        /* Get the list of bricks for the event */

        ret = dict_get_str (options, "bricks", &bricks);

        if (!ret) {
                gf_asprintf (&event_str, eventstrformat, (char *)words[2],
                            &bricks[1] /*Skip leading space*/);
        } else {
                gf_asprintf (&event_str, eventstrformat, (char *)words[2],
                            "<unavailable>");
        }
#endif

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
        } else {
#if (USE_EVENTS)
                gf_event (EVENT_VOLUME_ADD_BRICK, "%s", event_str);
                GF_FREE (event_str);
#endif
        }

        CLI_STACK_DESTROY (frame);
        return ret;
}

int
cli_tier_validate_replica_type (dict_t *dict, int type)
{

        int             brick_count             = -1;
        int             replica_count           = 1;
        int             ret                     = -1;

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get brick count");
                goto out;
        }

        ret = dict_get_int32 (dict, "replica-count", &replica_count);
        if (ret) {
                gf_log ("cli", GF_LOG_DEBUG, "Failed to get replica count. "
                        "Defaulting to one");
                replica_count = 1;
        }

        /*
         * Change the calculation of sub_count once attach-tier support
         * disperse volume.
         * sub_count = disperse_count for disperse volume
         * */


        if (brick_count % replica_count) {
             if (type == GF_CLUSTER_TYPE_REPLICATE)
                     cli_err ("number of bricks is not a multiple of "
                              "replica count");
             else if (type == GF_CLUSTER_TYPE_DISPERSE)
                     cli_err ("number of bricks is not a multiple of "
                              "disperse count");
             else
                     cli_err ("number of bricks given doesn't match "
                              "required count");

             ret = -1;
             goto out;
     }
        ret = 0;
out:
        return ret;
}

int
do_cli_cmd_volume_attach_tier (struct cli_state *state,
                                struct cli_cmd_word *word, const char **words,
                                int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *options = NULL;
        int                     sent = 0;
        int                     parse_error = 0;
        cli_local_t             *local = NULL;
        int                     type = 0;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_add_brick_parse (words, wordcount, &options, &type);
        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        /*
         * Merge this check when attach-tier has it's own cli parse function.
         */
        ret = cli_tier_validate_replica_type (options, type);
        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        if (state->mode & GLUSTER_MODE_WIGNORE) {
                ret = dict_set_int32 (options, "force", _gf_true);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to set force "
                                "option");
                        goto out;
                }
        }

        ret = dict_set_int32 (options, "attach-tier", 1);
        if (ret)
                goto out;

        ret = dict_set_int32 (options, "hot-type", type);
        if (ret)
                goto out;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_ATTACH_TIER];

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("attach-tier failed");
        }

        CLI_STACK_DESTROY (frame);

        return ret;
}

int
do_cli_cmd_volume_detach_tier (struct cli_state *state,
                              struct cli_cmd_word *word, const char **words,
                              int wordcount, gf_boolean_t *aborted)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *options = NULL;
        int                     sent = 0;
        int                     parse_error = 0;
        gf_answer_t             answer = GF_ANSWER_NO;
        cli_local_t             *local = NULL;
        int                     need_question = 0;

        const char *question = "Removing tier can result in data loss. "
                               "Do you want to Continue?";

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_detach_tier_parse(words, wordcount, &options,
                                               &need_question);
        if (ret) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        ret = dict_set_int32 (options, "force", 1);
        if (ret)
                goto out;

        ret = dict_set_int32 (options, "count", 0);
        if (ret)
                goto out;

        *aborted = _gf_false;

        if (!(state->mode & GLUSTER_MODE_SCRIPT) && need_question) {
                /* we need to ask question only in case of 'commit or force' */
                answer = cli_cmd_get_confirmation (state, question);
                if (GF_ANSWER_NO == answer) {
                        ret = 0;
                        *aborted = _gf_true;
                        goto out;
                }
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_REMOVE_TIER_BRICK];

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume detach tier failed");
        }

        CLI_STACK_DESTROY (frame);

        return ret;
}

int
cli_cmd_volume_tier_cbk (struct cli_state *state,
                         struct cli_cmd_word *word, const char **words,
                         int wordcount)
{
        int                      ret     = -1;
        call_frame_t            *frame   = NULL;
        dict_t                  *options = NULL;
        rpc_clnt_procedure_t    *proc    = NULL;
        cli_local_t             *local   = NULL;
        int                      i       = 0;
        eventtypes_t            event    = EVENT_LAST;
        gf_boolean_t            aborted  = _gf_false;
        gf_answer_t             answer   = GF_ANSWER_NO;

        const char *detach_question = "gluster volume detach-tier <VOLNAME> "
                               "<start|stop|status|commit|force> is "
                               "deprecated. Use the new command \'"
                               "gluster volume tier <VOLNAME> detach <start|"
                               "stop|status|commit|force>\'\n"
                               "Do you want to Continue?";

        const char *attach_question = "gluster volume attach-tier <VOLNAME> "
                               "[<replica COUNT>] <NEW-BRICK>... is "
                               "deprecated. Use the new command \'"
                               "gluster volume tier <VOLNAME> attach [<replica"
                               " COUNT>] <NEW-BRICK>... [force]\'\n"
                               "Do you want to Continue?";


        if (wordcount < 4) {
                cli_usage_out (word->pattern);
                if (wordcount == 3 && !strcmp(words[2], "help"))
                        ret = 0;
                goto out;
        }

        if (!strcmp(words[1], "detach-tier")) {
                /* we need to ask question when older command is used */
                answer = cli_cmd_get_confirmation (state, detach_question);
                if (GF_ANSWER_NO == answer) {
                        ret = 0;
                        goto out;
                }
                ret = do_cli_cmd_volume_detach_tier (state, word,
                                                     words, wordcount,
                                                     &aborted);
                goto out;
        } else if (!strcmp(words[3], "detach")) {
                for (i = 3; i < wordcount; i++)
                        words[i] = words[i+1];

                ret = do_cli_cmd_volume_detach_tier (state, word,
                                                     words, wordcount-1,
                                                     &aborted);
                if (!aborted) {
                        if (!strcmp (words[wordcount-2], "commit")) {
                                event = EVENT_TIER_DETACH_COMMIT;
                        } else if (!strcmp (words[wordcount-2], "start")) {
                                event = EVENT_TIER_DETACH_START;
                        } else if (!strcmp (words[wordcount-2], "stop")) {
                                event = EVENT_TIER_DETACH_STOP;
                        } else if (!strcmp (words[wordcount-2], "force")) {
                                event = EVENT_TIER_DETACH_FORCE;
                        }
                }
                goto out;

        } else if (!strcmp(words[1], "attach-tier")) {
                /* we need to ask question when the older command is used */
                answer = cli_cmd_get_confirmation (state, attach_question);
                if (GF_ANSWER_NO == answer) {
                        ret = 0;
                        goto out;
                }
                ret = do_cli_cmd_volume_attach_tier (state, word,
                                                     words, wordcount);
                goto out;
        } else if (!strcmp(words[3], "attach")) {
                for (i = 3; i < wordcount; i++)
                        words[i] = words[i+1];

                ret = do_cli_cmd_volume_attach_tier (state, word,
                                                     words, wordcount-1);
                if (!strcmp (words[wordcount-2], "force")) {
                        event = EVENT_TIER_ATTACH_FORCE;
                } else {
                        event = EVENT_TIER_ATTACH;
                }
                goto out;
        }

        ret = cli_cmd_volume_tier_parse (words, wordcount, &options);
        if (ret) {
                cli_usage_out (word->pattern);
                goto out;
        }

        if (!strcmp (words[wordcount-1], "start")) {
                event = EVENT_TIER_START;
        } else {
                if (!strcmp (words[wordcount-2], "start") &&
                    !strcmp (words[wordcount-1], "force")) {
                        event = EVENT_TIER_START_FORCE;
                }
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_TIER];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_out ("Tier command failed");
        } else {
                if (event != EVENT_LAST) {
                        gf_event (event, "vol=%s", words[2]);
                }
        }
        if (options)
                dict_unref (options);

        return ret;
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

/* Checks if at least one limit has been set on the volume
 *
 * Returns true if at least one limit is set. Returns false otherwise.
 */
gf_boolean_t
_limits_set_on_volume (char *volname, int type) {
        gf_boolean_t    limits_set                = _gf_false;
        int             ret                       = -1;
        char            quota_conf_file[PATH_MAX] = {0,};
        int             fd                        = -1;
        char            buf[16]                   = {0,};
        float           version                   = 0.0f;
        char            gfid_type_stored          = 0;
        char            gfid_type                 = 0;

        /* TODO: fix hardcoding; Need to perform an RPC call to glusterd
         * to fetch working directory
         */
        snprintf (quota_conf_file, sizeof quota_conf_file,
                  "%s/vols/%s/quota.conf",
                  GLUSTERD_DEFAULT_WORKDIR,
                  volname);
        fd = open (quota_conf_file, O_RDONLY);
        if (fd == -1)
                goto out;

        ret = quota_conf_read_version (fd, &version);
        if (ret)
                goto out;

        if (type == GF_QUOTA_OPTION_TYPE_LIST)
                gfid_type = GF_QUOTA_CONF_TYPE_USAGE;
        else
                gfid_type = GF_QUOTA_CONF_TYPE_OBJECTS;

        /* Try to read atleast one gfid  of type 'gfid_type' */
        while (1) {
                ret = quota_conf_read_gfid (fd, buf, &gfid_type_stored,
                                            version);
                if (ret <= 0)
                        break;

                if (gfid_type_stored == gfid_type) {
                        limits_set = _gf_true;
                        break;
                }
        }
out:
        if (fd != -1)
                sys_close (fd);

        return limits_set;
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
        gf_boolean_t             xml_err_flag   = _gf_false;
        char                     err_str[NAME_MAX] = {0,};
        int32_t                  type       = 0;
        char                     gfid_type  = 0;
        float                    version    = 0.0f;
        int32_t                  max_count  = 0;

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

        ret = dict_get_int32 (options, "type", &type);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get quota option type");
                goto out;
        }

        ret = dict_set_int32 (xdata, "type", type);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to set type in xdata");
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
        if (!_limits_set_on_volume (volname, type)) {
                snprintf (err_str, sizeof (err_str), "No%s quota configured on"
                          " volume %s",
                          (type == GF_QUOTA_OPTION_TYPE_LIST) ? "" : " inode",
                          volname);
                if (global_state->mode & GLUSTER_MODE_XML) {
                        xml_err_flag = _gf_true;
                } else {
                        cli_out ("quota: %s", err_str);
                }
                ret = 0;
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
        snprintf (quota_conf_file, sizeof quota_conf_file,
                  "%s/vols/%s/quota.conf",
                  GLUSTERD_DEFAULT_WORKDIR,
                  volname);
        fd = open (quota_conf_file, O_RDONLY);
        if (fd == -1) {
                //This may because no limits were yet set on the volume
                gf_log ("cli", GF_LOG_TRACE, "Unable to open "
                        "quota.conf");
                ret = 0;
                goto out;
         }

        ret = quota_conf_read_version (fd, &version);
        if (ret)
                goto out;

        CLI_LOCAL_INIT (local, words, frame, xdata);
        proc = &cli_quotad_clnt.proctable[GF_AGGREGATOR_GETLIMIT];

        gfid_str = GF_CALLOC (1, gf_common_mt_char, 64);
        if (!gfid_str) {
                ret = -1;
                goto out;
        }

        for (count = 0;; count++) {
                ret = quota_conf_read_gfid (fd, buf, &gfid_type, version);
                if (ret == 0) {
                        break;
                } else if (ret < 0) {
                        gf_log (THIS->name, GF_LOG_CRITICAL, "Quota "
                                "configuration store may be corrupt.");
                        goto out;
                }

                if ((type == GF_QUOTA_OPTION_TYPE_LIST &&
                     gfid_type == GF_QUOTA_CONF_TYPE_OBJECTS) ||
                    (type == GF_QUOTA_OPTION_TYPE_LIST_OBJECTS &&
                     gfid_type == GF_QUOTA_CONF_TYPE_USAGE))
                        continue;

                max_count++;
        }
        ret = dict_set_int32 (xdata, "max_count", max_count);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to set max_count");
                goto out;
        }

        ret = sys_lseek (fd, 0L, SEEK_SET);
        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to move offset to "
                        "the beginning: %s", strerror (errno));
                goto out;
        }
        ret = quota_conf_read_version (fd, &version);
        if (ret)
                goto out;

        for (count = 0;; count++) {
                ret = quota_conf_read_gfid (fd, buf, &gfid_type, version);
                if (ret == 0) {
                        break;
                } else if (ret < 0) {
                        gf_log (THIS->name, GF_LOG_CRITICAL, "Quota "
                                "configuration store may be corrupt.");
                        goto out;
                }

                if ((type == GF_QUOTA_OPTION_TYPE_LIST &&
                     gfid_type == GF_QUOTA_CONF_TYPE_OBJECTS) ||
                    (type == GF_QUOTA_OPTION_TYPE_LIST_OBJECTS &&
                     gfid_type == GF_QUOTA_CONF_TYPE_USAGE))
                        continue;

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

        if (global_state->mode & GLUSTER_MODE_XML) {
                ret = cli_xml_output_vol_quota_limit_list_end (local);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Error in printing "
                                "xml output");
                        goto out;
                }
        }

        if (count > 0) {
                ret = all_failed? -1: 0;
        } else {
                ret = 0;
        }


out:
        if (xml_err_flag) {
                ret = cli_xml_output_str ("volQuota", NULL, -1, 0, err_str);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Error outputting in "
                                "xml format");
                }
        }

        if (fd != -1) {
                sys_close (fd);
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
cli_cmd_bitrot_cbk (struct cli_state *state, struct cli_cmd_word *word,
                    const char **words, int wordcount)
{

        int                     ret        = -1;
        int                     parse_err  = 0;
        call_frame_t            *frame     = NULL;
        dict_t                  *options   = NULL;
        cli_local_t             *local     = NULL;
        rpc_clnt_procedure_t    *proc      = NULL;
        int                     sent       = 0;
#if (USE_EVENTS)
        int                     cmd_type        = -1;
        int                     ret1            = -1;
        int                     event_type      = -1;
        char                    *tmp            = NULL;
        char                    *events_str     = NULL;
#endif

        ret = cli_cmd_bitrot_parse (words, wordcount, &options);
        if (ret < 0) {
                cli_usage_out (word->pattern);
                parse_err = 1;
                goto out;
        }

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame) {
                ret = -1;
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_BITROT];
        if (proc == NULL) {
                ret = -1;
                goto out;
        }

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_err == 0))
                    cli_err ("Bit rot command failed. Please check the cli "
                             "logs for more details");

        }

#if (USE_EVENTS)
        if (ret == 0) {
                ret1 = dict_get_int32 (options, "type", &cmd_type);
                if (ret1)
                        cmd_type = -1;
                else {
                        ret1 = dict_get_str (options, "volname", &tmp);
                        if (ret1)
                                tmp = "";
                        gf_asprintf (&events_str, "name=%s", tmp);
                }

                switch (cmd_type) {
                case GF_BITROT_OPTION_TYPE_ENABLE:
                        event_type = EVENT_BITROT_ENABLE;
                        break;
                case GF_BITROT_OPTION_TYPE_DISABLE:
                        event_type = EVENT_BITROT_DISABLE;
                        break;
                case GF_BITROT_CMD_SCRUB_ONDEMAND:
                        event_type = EVENT_BITROT_SCRUB_ONDEMAND;
                        break;
                case GF_BITROT_OPTION_TYPE_SCRUB_THROTTLE:
                        event_type = EVENT_BITROT_SCRUB_THROTTLE;
                        ret1 = dict_get_str (options, "scrub-throttle-value",
                                             &tmp);
                        if (ret1)
                                tmp = "";
                        gf_asprintf (&events_str, "%s;value=%s", events_str,
                                     tmp);
                        break;
                case GF_BITROT_OPTION_TYPE_SCRUB_FREQ:
                        event_type = EVENT_BITROT_SCRUB_FREQ;
                        ret1 = dict_get_str (options, "scrub-frequency-value",
                                             &tmp);
                        if (ret1)
                                tmp = "";
                        gf_asprintf (&events_str, "%s;value=%s", events_str,
                                     tmp);
                        break;
                case GF_BITROT_OPTION_TYPE_SCRUB:
                        event_type = EVENT_BITROT_SCRUB_OPTION;
                        ret1 = dict_get_str (options, "scrub-value", &tmp);
                        if (ret1)
                                tmp = "";
                        gf_asprintf (&events_str, "%s;value=%s", events_str,
                                     tmp);
                        break;
                default:
                        break;
                }

                if (event_type > -1)
                        gf_event (event_type, "%s", events_str);

                if (events_str)
                        GF_FREE (events_str);
        }
#endif

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
        if (strcmp (words[1], "inode-quota") == 0) {
                ret = cli_cmd_inode_quota_parse (words, wordcount, &options);
                if (ret < 0) {
                        cli_usage_out (word->pattern);
                        parse_err = 1;
                        goto out;
                }
        } else {
                ret = cli_cmd_quota_parse (words, wordcount, &options);
                if (ret < 0) {
                        cli_usage_out (word->pattern);
                        parse_err = 1;
                        goto out;
                }
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
        case GF_QUOTA_OPTION_TYPE_LIST_OBJECTS:
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

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame) {
                ret = -1;
                goto out;
        }

        CLI_LOCAL_INIT (local, words, frame, options);
        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_QUOTA];

        if (proc->fn)
                ret = proc->fn (frame, THIS, options);

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if (sent == 0 && parse_err == 0)
                        cli_out ("Quota command failed. Please check the cli "
                                 "logs for more details");
        }

        /* Events for Quota */
         if (ret == 0) {
                switch (type) {
                case GF_QUOTA_OPTION_TYPE_ENABLE:
                        gf_event (EVENT_QUOTA_ENABLE, "volume=%s", volname);
                        break;
                case GF_QUOTA_OPTION_TYPE_DISABLE:
                        gf_event (EVENT_QUOTA_DISABLE, "volume=%s", volname);
                        break;
                case GF_QUOTA_OPTION_TYPE_LIMIT_USAGE:
                        gf_event (EVENT_QUOTA_SET_USAGE_LIMIT, "volume=%s;"
                                  "path=%s;limit=%s", volname, words[4],
                                  words[5]);
                        break;
                case GF_QUOTA_OPTION_TYPE_LIMIT_OBJECTS:
                        gf_event (EVENT_QUOTA_SET_OBJECTS_LIMIT, "volume=%s;"
                                  "path=%s;limit=%s", volname, words[4],
                                  words[5]);
                        break;
                case GF_QUOTA_OPTION_TYPE_REMOVE:
                        gf_event (EVENT_QUOTA_REMOVE_USAGE_LIMIT, "volume=%s;"
                                  "path=%s", volname, words[4]);
                        break;
                case GF_QUOTA_OPTION_TYPE_REMOVE_OBJECTS:
                        gf_event (EVENT_QUOTA_REMOVE_OBJECTS_LIMIT,
                                  "volume=%s;" "path=%s", volname, words[4]);
                        break;
                case GF_QUOTA_OPTION_TYPE_ALERT_TIME:
                        gf_event (EVENT_QUOTA_ALERT_TIME, "volume=%s;time=%s",
                                  volname, words[4]);
                        break;
                case GF_QUOTA_OPTION_TYPE_SOFT_TIMEOUT:
                        gf_event (EVENT_QUOTA_SOFT_TIMEOUT, "volume=%s;"
                                  "soft-timeout=%s", volname, words[4]);
                        break;
                case GF_QUOTA_OPTION_TYPE_HARD_TIMEOUT:
                        gf_event (EVENT_QUOTA_HARD_TIMEOUT, "volume=%s;"
                                  "hard-timeout=%s", volname, words[4]);
                        break;
                case GF_QUOTA_OPTION_TYPE_DEFAULT_SOFT_LIMIT:
                        gf_event (EVENT_QUOTA_DEFAULT_SOFT_LIMIT, "volume=%s;"
                                  "default-soft-limit=%s", volname, words[4]);
                        break;
                }
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
        char                    *volname = NULL;
#if (USE_EVENTS)
        eventtypes_t            event = EVENT_LAST;
        char                    *event_str = NULL;
        int                     event_ret = -1;
#endif
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

        ret = dict_get_str (options, "volname", &volname);
        if (ret || !volname) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to fetch volname");
                ret = -1;
                goto out;
        }

#if (USE_EVENTS)
        event_ret = cli_event_remove_brick_str (options, &event_str, &event);
#endif

        if (!strcmp (volname, GLUSTER_SHARED_STORAGE)) {
                question = "Removing brick from the shared storage volume"
                           "(gluster_shared_storage), will affect features "
                           "like snapshot scheduler, geo-replication "
                           "and NFS-Ganesha. Do you still want to "
                           "continue?";
                need_question = _gf_true;
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
        } else {
#if (USE_EVENTS)
                if (!event_ret) {
                        gf_event (event, "%s", event_str);
                        GF_FREE (event_str);
                }
#endif
        }

        CLI_STACK_DESTROY (frame);


        return ret;

}

int
cli_cmd_volume_reset_brick_cbk (struct cli_state *state,
                                struct cli_cmd_word *word,
                                const char **words,
                                int wordcount)
{
        int                      ret          = -1;
        rpc_clnt_procedure_t    *proc         = NULL;
        call_frame_t            *frame        = NULL;
        dict_t                  *options      = NULL;
        int                      sent         = 0;
        int                      parse_error  = 0;
        cli_local_t             *local        = NULL;

#ifdef GF_SOLARIS_HOST_OS
        cli_out ("Command not supported on Solaris");
        goto out;
#endif
        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_RESET_BRICK];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_volume_reset_brick_parse (words, wordcount, &options);

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
                        cli_out ("Volume reset-brick failed");
        } else {
                if (wordcount > 5) {
                        gf_event (EVENT_BRICK_RESET_COMMIT,
                                  "Volume=%s;source-brick=%s;"
                                  "destination-brick=%s",
                                  (char *)words[2], (char *)words[3],
                                  (char *)words[4]);
                } else {
                        gf_event (EVENT_BRICK_RESET_START,
                                  "Volume=%s;source-brick=%s",
                                  (char *)words[2], (char *)words[3]);
                }
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
        int                      ret          = -1;
        rpc_clnt_procedure_t    *proc         = NULL;
        call_frame_t            *frame        = NULL;
        dict_t                  *options = NULL;
        int                      sent = 0;
        int                      parse_error = 0;
        cli_local_t             *local = NULL;

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

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, options);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume replace-brick failed");
        } else {
                gf_event (EVENT_BRICK_REPLACE,
                          "Volume=%s;source-brick=%s;destination-brick=%s",
                          (char *)words[2], (char *)words[3], (char *)words[4]);
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

        if (!((strcmp ("rotate", words[2]) == 0) ||
              (strcmp ("rotate", words[3]) == 0))) {
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
#if (USE_EVENTS)
        int                      ret1       = -1;
        int                      cmd_type   = -1;
        int                      tmpi       = 0;
        char                    *tmp        = NULL;
        char                    *events_str = NULL;
        int                      event_type = -1;
#endif

        proc = &cli_rpc_prog->proctable [GLUSTER_CLI_GSYNC_SET];

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

#if (USE_EVENTS)
        if (ret == 0) {
                events_str = gf_strdup ("");

                /* Type of Geo-rep Action - Create, Start etc */
                ret1 = dict_get_int32 (options, "type", &cmd_type);
                if (ret1)
                        cmd_type = -1;

                /* Only capture Events for modification commands */
                switch (cmd_type) {
                case GF_GSYNC_OPTION_TYPE_CREATE:
                        event_type = EVENT_GEOREP_CREATE;
                        break;
                case GF_GSYNC_OPTION_TYPE_START:
                        event_type = EVENT_GEOREP_START;
                        break;
                case GF_GSYNC_OPTION_TYPE_STOP:
                        event_type = EVENT_GEOREP_STOP;
                        break;
                case GF_GSYNC_OPTION_TYPE_PAUSE:
                        event_type = EVENT_GEOREP_PAUSE;
                        break;
                case GF_GSYNC_OPTION_TYPE_RESUME:
                        event_type = EVENT_GEOREP_RESUME;
                        break;
                case GF_GSYNC_OPTION_TYPE_DELETE:
                        event_type = EVENT_GEOREP_DELETE;
                        break;
                case GF_GSYNC_OPTION_TYPE_CONFIG:
                        ret1 = dict_get_str (options, "subop", &tmp);
                        if (ret1)
                                tmp = "";

                        /* For Config Set additionally capture key and value */
                        /* For Config Reset capture key */
                        if (strcmp (tmp, "set") == 0) {
                                event_type = EVENT_GEOREP_CONFIG_SET;

                                ret1 = dict_get_str (options, "op_name", &tmp);
                                if (ret1)
                                        tmp = "";

                                gf_asprintf (&events_str, "%soption=%s;",
                                             events_str, tmp);

                                ret1 = dict_get_str (options, "op_value", &tmp);
                                if (ret1)
                                        tmp = "";

                                gf_asprintf (&events_str, "%svalue=%s;",
                                             events_str, tmp);
                        } else if (strcmp (tmp, "del") == 0) {
                                event_type = EVENT_GEOREP_CONFIG_RESET;

                                ret1 = dict_get_str (options, "op_name", &tmp);
                                if (ret1)
                                        tmp = "";

                                gf_asprintf (&events_str, "%soption=%s;",
                                             events_str, tmp);
                        }
                        break;
                default:
                        break;
                }

                if (event_type > -1) {
                        /* Capture all optional arguments used */
                        ret1 = dict_get_int32 (options, "force", &tmpi);
                        if (ret1 == 0)
                                gf_asprintf (&events_str, "%sforce=%d;",
                                             events_str, tmpi);

                        ret1 = dict_get_int32 (options, "push_pem", &tmpi);
                        if (ret1 == 0)
                                gf_asprintf (&events_str, "%spush_pem=%d;",
                                             events_str, tmpi);

                        ret1 = dict_get_int32 (options, "no_verify", &tmpi);
                        if (ret1 == 0)
                                gf_asprintf (&events_str, "%sno_verify=%d;",
                                             events_str, tmpi);

                        ret1 = dict_get_int32 (options, "ssh_port", &tmpi);
                        if (ret1 == 0)
                                gf_asprintf (&events_str, "%sssh_port=%d;",
                                             events_str, tmpi);

                        ret1 = dict_get_int32 (options, "reset-sync-time",
                                               &tmpi);
                        if (ret1 == 0)
                                gf_asprintf (&events_str,
                                             "%sreset_sync_time=%d;",
                                             events_str, tmpi);

                        /* Capture Master and Slave Info */
                        ret1 = dict_get_str (options, "master", &tmp);
                        if (ret1)
                                tmp = "";
                        gf_asprintf (&events_str, "%smaster=%s;",
                                     events_str, tmp);

                        ret1 = dict_get_str (options, "slave", &tmp);
                        if (ret1)
                                tmp = "";
                        gf_asprintf (&events_str, "%sslave=%s",
                                     events_str, tmp);

                        gf_event (event_type, "%s", events_str);
                }

                /* Allocated by gf_strdup and gf_asprintf */
                GF_FREE (events_str);
        }
#endif

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

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.device", i);
        ret = dict_get_str (dict, key, &(status->device));
        if (ret)
                status->device = NULL;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.block_size", i);
        ret = dict_get_uint64 (dict, key, &(status->block_size));
        if (ret) {
                ret = 0;
                status->block_size = 0;
        }

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

        if (status->online) {
                cli_out ("%-20s : %-20d", "TCP Port", status->port);
                cli_out ("%-20s : %-20d", "RDMA Port", status->rdma_port);
        } else {
                cli_out ("%-20s : %-20s", "TCP Port", "N/A");
                cli_out ("%-20s : %-20s", "RDMA Port", "N/A");
        }

        cli_out ("%-20s : %-20c", "Online", (status->online) ? 'Y' : 'N');
        cli_out ("%-20s : %-20s", "Pid", status->pid_str);

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
        if (status->free)
                cli_out ("%-20s : %-20s", "Disk Space Free", status->free);
        else
                cli_out ("%-20s : %-20s", "Disk Space Free", "N/A");

        if (status->total)
                cli_out ("%-20s : %-20s", "Total Disk Space", status->total);
        else
                cli_out ("%-20s : %-20s", "Total Disk Space", "N/A");


        if (status->total_inodes) {
                cli_out ("%-20s : %-20"GF_PRI_INODE, "Inode Count",
                         status->total_inodes);
        } else {
                cli_out ("%-20s : %-20s", "Inode Count", "N/A");
        }

        if (status->free_inodes) {
                cli_out ("%-20s : %-20"GF_PRI_INODE, "Free Inodes",
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
        int  num_spaces = 0;

        p = status->brick;
        bricklen = strlen (p);
        while (bricklen > 0) {
                if (bricklen > fieldlen) {
                        cli_out ("%.*s", fieldlen, p);
                        p += fieldlen;
                        bricklen -= fieldlen;
                } else {
                        num_spaces = (fieldlen - bricklen) + 1;
                        printf ("%s", p);
                        while (num_spaces-- != 0)
                                printf (" ");
                        if (status->port || status->rdma_port) {
                                if (status->online)
                                        cli_out ("%-10d%-11d%-8c%-5s",
                                                 status->port,
                                                 status->rdma_port,
                                                 status->online?'Y':'N',
                                                 status->pid_str);
                                else
                                        cli_out ("%-10s%-11s%-8c%-5s",
                                                 "N/A",
                                                 "N/A",
                                                 status->online?'Y':'N',
                                                 status->pid_str);
                        }
                        else
                                cli_out ("%-10s%-11s%-8c%-5s",
                                         "N/A", "N/A", status->online?'Y':'N',
                                         status->pid_str);
                        bricklen = 0;
                }
        }

        return 0;
}

#define NEEDS_GLFS_HEAL(op) ((op == GF_SHD_OP_SBRAIN_HEAL_FROM_BIGGER_FILE) || \
                             (op == GF_SHD_OP_SBRAIN_HEAL_FROM_LATEST_MTIME) ||\
                             (op == GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK) ||      \
                             (op == GF_SHD_OP_INDEX_SUMMARY) ||               \
                             (op == GF_SHD_OP_SPLIT_BRAIN_FILES) ||           \
                             (op == GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE))

int
cli_launch_glfs_heal (int heal_op, dict_t *options)
{
        char      buff[PATH_MAX] = {0};
        runner_t  runner         = {0};
        char      *filename      = NULL;
        char      *hostname      = NULL;
        char      *path          = NULL;
        char      *volname       = NULL;
        char      *out           = NULL;
        int        ret           = 0;

        runinit (&runner);
        ret = dict_get_str (options, "volname", &volname);
        runner_add_args (&runner, SBIN_DIR"/glfsheal", volname, NULL);
        runner_redir (&runner, STDOUT_FILENO, RUN_PIPE);

        switch (heal_op) {
        case GF_SHD_OP_INDEX_SUMMARY:
                if (global_state->mode & GLUSTER_MODE_XML) {
                        runner_add_args (&runner, "xml", NULL);
                }
                break;
        case GF_SHD_OP_SBRAIN_HEAL_FROM_BIGGER_FILE:
                ret = dict_get_str (options, "file", &filename);
                runner_add_args (&runner, "bigger-file", filename, NULL);
                break;
        case GF_SHD_OP_SBRAIN_HEAL_FROM_LATEST_MTIME:
                ret = dict_get_str (options, "file", &filename);
                runner_add_args (&runner, "latest-mtime", filename, NULL);
                break;
        case GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK:
                ret = dict_get_str (options, "heal-source-hostname",
                                    &hostname);
                ret = dict_get_str (options, "heal-source-brickpath",
                                    &path);
                runner_add_args (&runner, "source-brick", NULL);
                runner_argprintf (&runner, "%s:%s", hostname, path);
                if (dict_get_str (options, "file", &filename) == 0)
                        runner_argprintf (&runner, filename);
                break;
        case GF_SHD_OP_SPLIT_BRAIN_FILES:
                runner_add_args (&runner, "split-brain-info", NULL);
                if (global_state->mode & GLUSTER_MODE_XML) {
                        runner_add_args (&runner, "xml", NULL);
                }
                break;
        case GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE:
        case GF_SHD_OP_GRANULAR_ENTRY_HEAL_DISABLE:
                runner_add_args (&runner, "granular-entry-heal-op", NULL);
                break;
        default:
                ret = -1;
        }
        ret = runner_start (&runner);
        if (ret == -1)
                goto out;
        while ((out = fgets (buff, sizeof(buff),
                             runner_chio (&runner, STDOUT_FILENO)))) {
                printf ("%s", out);
        }
        ret = runner_end (&runner);

out:
        return ret;
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
        int                     heal_op = 0;

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
        ret = dict_get_int32 (options, "heal-op", &heal_op);
        if (ret < 0)
                goto out;
        if (NEEDS_GLFS_HEAL (heal_op)) {
                ret = cli_launch_glfs_heal (heal_op, options);
                if (ret < 0)
                        goto out;
                if (heal_op != GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE)
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
                if ((sent == 0) && (parse_error == 0) &&
                    !(global_state->mode & GLUSTER_MODE_XML)) {
                        cli_out ("Volume heal failed.");
                }
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
                if ((sent == 0) && (parse_error == 0))
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
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Volume clear-locks failed");
        }

        CLI_STACK_DESTROY (frame);

        return ret;
}

int
cli_cmd_volume_barrier_cbk (struct cli_state *state, struct cli_cmd_word *word,
                            const char **words, int wordcount)
{
        int ret = -1;
        rpc_clnt_procedure_t *proc = NULL;
        call_frame_t *frame = NULL;
        dict_t *options = NULL;
        int sent = 0;
        int parse_error = 0;
        cli_local_t *local = NULL;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if (wordcount != 4) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        options = dict_new();
        if (!options) {
                ret = -1;
                goto out;
        }
        ret = dict_set_str(options, "volname", (char *)words[2]);
        if (ret)
                goto out;

        ret = dict_set_str (options, "barrier", (char *)words[3]);
        if (ret)
                goto out;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_BARRIER_VOLUME];

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn)
                ret = proc->fn (frame, THIS, options);

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_err ("Volume barrier failed");
        }
        CLI_STACK_DESTROY (frame);

        return ret;
}

int
cli_cmd_volume_getopt_cbk (struct cli_state *state, struct cli_cmd_word *word,
                        const char **words, int wordcount)
{
        int                   ret       = -1;
        rpc_clnt_procedure_t *proc      = NULL;
        call_frame_t         *frame     = NULL;
        dict_t               *options   = NULL;
        int                   sent      = 0;
        int                   parse_err = 0;
        cli_local_t          *local     = NULL;

        if (wordcount != 4) {
                cli_usage_out (word->pattern);
                parse_err = 1;
                goto out;
        }

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        options = dict_new ();
        if (!options)
                goto out;

        ret = dict_set_str (options, "volname", (char *)words[2]);
        if (ret)
                goto out;

        ret = dict_set_str (options, "key", (char *)words[3]);
        if (ret)
                goto out;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_GET_VOL_OPT];

        CLI_LOCAL_INIT (local, words, frame, options);

        if (proc->fn)
                ret = proc->fn (frame, THIS, options);

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_err == 0))
                        cli_err ("Volume get option failed");
        }
        CLI_STACK_DESTROY (frame);
        return ret;
}

struct cli_cmd volume_cmds[] = {
        { "volume info [all|<VOLNAME>]",
          cli_cmd_volume_info_cbk,
          "list information of all volumes"},

        { "volume create <NEW-VOLNAME> [stripe <COUNT>] "
          "[replica <COUNT> [arbiter <COUNT>]] "
          "[disperse [<COUNT>]] [disperse-data <COUNT>] [redundancy <COUNT>] "
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

#if !defined(__NetBSD__)
        { "volume tier <VOLNAME> status\n"
        "volume tier <VOLNAME> start [force]\n"
        "volume tier <VOLNAME> stop\n"
        "volume tier <VOLNAME> attach [<replica COUNT>] <NEW-BRICK>... [force]\n"
        "volume tier <VOLNAME> detach <start|stop|status|commit|[force]>\n",
        cli_cmd_volume_tier_cbk,
        "Tier translator specific operations."},

        { "volume attach-tier <VOLNAME> [<replica COUNT>] <NEW-BRICK>...",
        cli_cmd_volume_tier_cbk,
          "NOTE: this is old syntax, will be depreciated in next release. "
          "Please use gluster volume tier <vol> attach "
          "[<replica COUNT>] <NEW-BRICK>..."},

        { "volume detach-tier <VOLNAME> "
          " <start|stop|status|commit|force>",
        cli_cmd_volume_tier_cbk,
          "NOTE: this is old syntax, will be depreciated in next release. "
          "Please use gluster volume tier <vol> detach "
          "{start|stop|commit} [force]"},
#endif

        { "volume add-brick <VOLNAME> [<stripe|replica> <COUNT> "
          "[arbiter <COUNT>]] <NEW-BRICK> ... [force]",
          cli_cmd_volume_add_brick_cbk,
          "add brick to volume <VOLNAME>"},

        { "volume remove-brick <VOLNAME> [replica <COUNT>] <BRICK> ..."
          " <start|stop|status|commit|force>",
          cli_cmd_volume_remove_brick_cbk,
          "remove brick from volume <VOLNAME>"},

        { "volume rebalance <VOLNAME> {{fix-layout start} | {start [force]|stop|status}}",
          cli_cmd_volume_defrag_cbk,
          "rebalance operations"},

        { "volume replace-brick <VOLNAME> <SOURCE-BRICK> <NEW-BRICK> "
          "{commit force}",
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

        { "volume log <VOLNAME> rotate [BRICK]",
          cli_cmd_log_rotate_cbk,
         "rotate the log file for corresponding volume/brick"},

        { "volume log rotate <VOLNAME> [BRICK]",
          cli_cmd_log_rotate_cbk,
         "rotate the log file for corresponding volume/brick"
         " NOTE: This is an old syntax, will be deprecated from next release."},

        { "volume sync <HOSTNAME> [all|<VOLNAME>]",
          cli_cmd_sync_volume_cbk,
         "sync the volume information from a peer"},

         { "volume reset <VOLNAME> [option] [force]",
         cli_cmd_volume_reset_cbk,
         "reset all the reconfigured options"},

#if (SYNCDAEMON_COMPILE)
        {"volume "GEOREP" [<VOLNAME>] [<SLAVE-URL>] {create [[ssh-port n] [[no-verify]|[push-pem]]] [force]"
         "|start [force]|stop [force]|pause [force]|resume [force]|config|status [detail]|delete [reset-sync-time]} [options...]",
         cli_cmd_volume_gsync_set_cbk,
         "Geo-sync operations",
         cli_cmd_check_gsync_exists_cbk},
#endif

         { "volume profile <VOLNAME> {start|info [peek|incremental [peek]|cumulative|clear]|stop} [nfs]",
           cli_cmd_volume_profile_cbk,
           "volume profile operations"},

        { "volume quota <VOLNAME> {enable|disable|list [<path> ...]| "
          "list-objects [<path> ...] | remove <path>| remove-objects <path> | "
          "default-soft-limit <percent>} |\n"
          "volume quota <VOLNAME> {limit-usage <path> <size> [<percent>]} |\n"
          "volume quota <VOLNAME> {limit-objects <path> <number> [<percent>]} |\n"
          "volume quota <VOLNAME> {alert-time|soft-timeout|hard-timeout} {<time>}",
          cli_cmd_quota_cbk,
          "quota translator specific operations"},

        { "volume inode-quota <VOLNAME> enable",
          cli_cmd_quota_cbk,
          "quota translator specific operations"},

         { "volume top <VOLNAME> {open|read|write|opendir|readdir|clear} [nfs|brick <brick>] [list-cnt <value>] |\n"
           "volume top <VOLNAME> {read-perf|write-perf} [bs <size> count <count>] [brick <brick>] [list-cnt <value>]",
           cli_cmd_volume_top_cbk,
           "volume top operations"},

        { "volume status [all | <VOLNAME> [nfs|shd|<BRICK>|quotad|tierd]]"
          " [detail|clients|mem|inode|fd|callpool|tasks]",
          cli_cmd_volume_status_cbk,
          "display status of all or specified volume(s)/brick"},

        { "volume heal <VOLNAME> [enable | disable | full |"
          "statistics [heal-count [replica <HOSTNAME:BRICKNAME>]] |"
          "info [healed | heal-failed | split-brain] |"
          "split-brain {bigger-file <FILE> | latest-mtime <FILE> |"
                       "source-brick <HOSTNAME:BRICKNAME> [<FILE>]} |"
          "granular-entry-heal {enable | disable}]",
          cli_cmd_volume_heal_cbk,
          "self-heal commands on volume specified by <VOLNAME>"},

        {"volume statedump <VOLNAME> [[nfs|quotad] [all|mem|iobuf|callpool|"
         "priv|fd|inode|history]... | [client <hostname:process-id>]]",
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
        {"volume barrier <VOLNAME> {enable|disable}",
         cli_cmd_volume_barrier_cbk,
         "Barrier/unbarrier file operations on a volume"
        },
        {"volume get <VOLNAME> <key|all>",
         cli_cmd_volume_getopt_cbk,
         "Get the value of the all options or given option for volume <VOLNAME>"
        },
        {"volume bitrot <VOLNAME> {enable|disable} |\n"
         "volume bitrot <volname> scrub-throttle {lazy|normal|aggressive} |\n"
         "volume bitrot <volname> scrub-frequency {hourly|daily|weekly|biweekly"
         "|monthly} |\n"
         "volume bitrot <volname> scrub {pause|resume|status|ondemand}",
         cli_cmd_bitrot_cbk,
         "Bitrot translator specific operation. For more information about "
         "bitrot command type  'man gluster'"
        },
        { "volume reset-brick <VOLNAME> <SOURCE-BRICK> {{start} |"
          " {<NEW-BRICK> commit}}",
          cli_cmd_volume_reset_brick_cbk,
          "reset-brick operations"},

        { NULL, NULL, NULL }
};

int
cli_cmd_volume_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                      const char **words, int wordcount)
{
        struct cli_cmd        *cmd     = NULL;
        struct cli_cmd        *vol_cmd = NULL;
        int                   count    = 0;

        cmd = GF_CALLOC (1, sizeof (volume_cmds), cli_mt_cli_cmd);
        memcpy (cmd, volume_cmds, sizeof (volume_cmds));
        count = (sizeof (volume_cmds) / sizeof (struct cli_cmd));
        cli_cmd_sort (cmd, count);

        for (vol_cmd = cmd; vol_cmd->pattern; vol_cmd++)
                if (_gf_false == vol_cmd->disable)
                        cli_out ("%s - %s", vol_cmd->pattern, vol_cmd->desc);

        GF_FREE (cmd);
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
