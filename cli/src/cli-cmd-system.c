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

int cli_cmd_system_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                             const char **words, int wordcount);

int cli_cmd_copy_file_cbk (struct cli_state *state, struct cli_cmd_word *word,
                           const char **words, int wordcount);

int cli_cmd_sys_exec_cbk (struct cli_state *state, struct cli_cmd_word *word,
                          const char **words, int wordcount);

int
cli_cmd_getspec_cbk (struct cli_state *state, struct cli_cmd_word *word,
                     const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *dict = NULL;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        dict = dict_new ();
        if (!dict)
                goto out;

        if (wordcount != 3) {
                cli_usage_out (word->pattern);
                goto out;
        }

        ret = dict_set_str (dict, "volid", (char *)words[2]);
        if (ret)
                goto out;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_GETSPEC];
        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (!proc && ret) {
                if (dict)
                        dict_unref (dict);
                if (wordcount > 1)
                        cli_out ("Fetching spec for volume %s failed",
                                 (char *)words[2]);
        }

        return ret;
}

int
cli_cmd_pmap_b2p_cbk (struct cli_state *state, struct cli_cmd_word *word,
                 const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *dict = NULL;

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        dict = dict_new ();
        if (!dict)
                goto out;

        if (wordcount != 4) {
                cli_usage_out (word->pattern);
                goto out;
        }

        ret = dict_set_str (dict, "brick", (char *)words[3]);
        if (ret)
                goto out;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_PMAP_PORTBYBRICK];
        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (!proc && ret) {
                if (dict)
                        dict_unref (dict);
                if (wordcount > 1)
                        cli_out ("Fetching spec for volume %s failed",
                                 (char *)words[3]);
        }

        return ret;
}

int
cli_cmd_fsm_log_cbk (struct cli_state *state, struct cli_cmd_word *word,
                     const char **words, int wordcount)
{
        int                             ret = -1;
        rpc_clnt_procedure_t            *proc = NULL;
        call_frame_t                    *frame = NULL;
        char                            *name = "";

        if ((wordcount != 4) && (wordcount != 3)) {
                cli_usage_out (word->pattern);
                goto out;
        }

        if (wordcount == 4)
                name = (char*)words[3];
        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_FSM_LOG];
        if (proc && proc->fn) {
                frame = create_frame (THIS, THIS->ctx->pool);
                if (!frame)
                        goto out;
                ret = proc->fn (frame, THIS, (void*)name);
        }
out:
        return ret;
}

int
cli_cmd_getwd_cbk (struct cli_state *state, struct cli_cmd_word *word,
                   const char **words, int wordcount)
{
        int                             ret = -1;
        rpc_clnt_procedure_t            *proc = NULL;
        call_frame_t                    *frame = NULL;

        if (wordcount != 2) {
                cli_usage_out (word->pattern);
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_GETWD];
        if (proc && proc->fn) {
                frame = create_frame (THIS, THIS->ctx->pool);
                if (!frame)
                        goto out;
                ret = proc->fn (frame, THIS, NULL);
        }
out:
        return ret;
}

static dict_t *
make_seq_dict (int argc, char **argv)
{
        char index[] = "4294967296"; // 1<<32
        int i        = 0;
        int ret      = 0;
        dict_t *dict = dict_new ();

        if (!dict)
                return NULL;

        for (i = 0; i < argc; i++) {
                snprintf(index, sizeof(index), "%d", i);
                ret = dict_set_str (dict, index, argv[i]);
                if (ret == -1)
                        break;
        }

        if (ret) {
                dict_unref (dict);
                dict = NULL;
        }

        return dict;
}

int
cli_cmd_mount_cbk (struct cli_state *state, struct cli_cmd_word *word,
                   const char **words, int wordcount)
{
        rpc_clnt_procedure_t *proc = NULL;
        call_frame_t *frame        = NULL;
        int ret                    = -1;
        dict_t *dict               = NULL;
        void *dataa[]              = {NULL, NULL};

        if (wordcount < 4) {
                cli_usage_out (word->pattern);
                goto out;
        }

        dict = make_seq_dict (wordcount - 3, (char **)words + 3);
        if (!dict)
                goto out;

        dataa[0] = (void *)words[2];
        dataa[1] = dict;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_MOUNT];
        if (proc && proc->fn) {
                frame = create_frame (THIS, THIS->ctx->pool);
                if (!frame)
                        goto out;
                ret = proc->fn (frame, THIS, dataa);
        }

 out:
        if (dict)
                dict_unref (dict);

        if (!proc && ret)
                cli_out ("Mount command failed");

        return ret;
}

int
cli_cmd_umount_cbk (struct cli_state *state, struct cli_cmd_word *word,
                   const char **words, int wordcount)
{
        rpc_clnt_procedure_t *proc = NULL;
        call_frame_t *frame        = NULL;
        int ret                    = -1;
        dict_t *dict               = NULL;

        if (!(wordcount == 3 ||
              (wordcount == 4 && strcmp (words[3], "lazy") == 0))) {
                cli_usage_out (word->pattern);
                goto out;
        }

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_str (dict, "path", (char *)words[2]);
        if (ret != 0)
                goto out;
        ret = dict_set_int32 (dict, "lazy", wordcount == 4);
        if (ret != 0)
                goto out;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_UMOUNT];
        if (proc && proc->fn) {
                frame = create_frame (THIS, THIS->ctx->pool);
                if (!frame)
                        goto out;
                ret = proc->fn (frame, THIS, dict);
        }

 out:
        if (dict)
                dict_unref (dict);

        if (!proc && ret)
                cli_out ("Umount command failed");

        return ret;
}

int
cli_cmd_uuid_get_cbk (struct cli_state *state, struct cli_cmd_word *word,
                      const char **words, int wordcount)
{
        int                     ret = -1;
        int                     sent = 0;
        int                     parse_error = 0;
        dict_t                  *dict  = NULL;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        cli_local_t             *local = NULL;
        xlator_t                *this  = NULL;

        this = THIS;
        if (wordcount != 3) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_UUID_GET];
        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                goto out;

        dict = dict_new ();
        if (!dict)
                goto out;

        CLI_LOCAL_INIT (local, words, frame, dict);
        if (proc->fn)
                ret = proc->fn (frame, this, dict);

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("uuid get failed");
        }

        CLI_STACK_DESTROY (frame);
        return ret;
}

int
cli_cmd_uuid_reset_cbk (struct cli_state *state, struct cli_cmd_word *word,
                        const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        int                     sent = 0;
        int                     parse_error = 0;
        gf_answer_t             answer = GF_ANSWER_NO;
        char                    *question = NULL;
        cli_local_t             *local = NULL;
        dict_t                  *dict  = NULL;
        xlator_t                *this  = NULL;

        question = "Resetting uuid changes the uuid of local glusterd. "
                   "Do you want to continue?";

        if (wordcount != 3) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_UUID_RESET];

        this = THIS;
        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                goto out;

        dict = dict_new ();
        if (!dict) {
                ret = -1;
                goto out;
        }
        CLI_LOCAL_INIT (local, words, frame, dict);
        answer = cli_cmd_get_confirmation (state, question);

        if (GF_ANSWER_NO == answer) {
                ret = 0;
                goto out;
        }

        //send NULL as argument since no dictionary is sent to glusterd
        if (proc->fn) {
                ret = proc->fn (frame, this, dict);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("uuid reset failed");
        }

        CLI_STACK_DESTROY (frame);

        return ret;
}

struct cli_cmd cli_system_cmds[] = {
        { "system:: getspec <VOLNAME>",
          cli_cmd_getspec_cbk,
          "fetch the volume file for the volume <VOLNAME>"},

        { "system:: portmap brick2port <BRICK>",
          cli_cmd_pmap_b2p_cbk,
          "query which port <BRICK> listens on"},

        { "system:: fsm log [<peer-name>]",
          cli_cmd_fsm_log_cbk,
          "display fsm transitions"},

        { "system:: getwd",
          cli_cmd_getwd_cbk,
          "query glusterd work directory"},

        { "system:: mount <label> <args...>",
          cli_cmd_mount_cbk,
          "request a mount"},

        { "system:: umount <path> [lazy]",
          cli_cmd_umount_cbk,
          "request an umount"},

        { "system:: uuid get",
          cli_cmd_uuid_get_cbk,
          "get uuid of glusterd"},

        { "system:: uuid reset",
          cli_cmd_uuid_reset_cbk,
          "reset the uuid of glusterd"},

        { "system:: help",
           cli_cmd_system_help_cbk,
           "display help for system commands"},

        { "system:: copy file [<filename>]",
           cli_cmd_copy_file_cbk,
           "Copy file from current node's $working_dir to "
           "$working_dir of all cluster nodes"},

        { "system:: execute <command> <args>",
           cli_cmd_sys_exec_cbk,
           "Execute the command on all the nodes "
           "in the cluster and display their output."},

        { NULL, NULL, NULL }
};

int
cli_cmd_sys_exec_cbk (struct cli_state *state, struct cli_cmd_word *word,
                      const char **words, int wordcount)
{
        char                   cmd_arg_name[PATH_MAX] = "";
        char                  *command                = NULL;
        char                  *saveptr                = NULL;
        char                  *tmp                    = NULL;
        int                    ret                    = -1;
        int                    i                      = -1;
        int                    cmd_args_count         = 0;
        int                    in_cmd_args_count      = 0;
        rpc_clnt_procedure_t  *proc                   = NULL;
        call_frame_t          *frame                  = NULL;
        dict_t                *dict                   = NULL;
        cli_local_t           *local                  = NULL;

        if (wordcount < 3) {
                cli_usage_out (word->pattern);
                goto out;
        }

        dict = dict_new ();
        if (!dict)
                goto out;

        command = strtok_r ((char *)words[2], " ", &saveptr);
        do {
                tmp = strtok_r (NULL, " ", &saveptr);
                if (tmp) {
                        in_cmd_args_count++;
                        memset (cmd_arg_name, '\0', sizeof(cmd_arg_name));
                        snprintf (cmd_arg_name, sizeof(cmd_arg_name),
                                  "cmd_arg_%d", in_cmd_args_count);
                        ret = dict_set_str (dict, cmd_arg_name, tmp);
                        if (ret) {
                                gf_log ("", GF_LOG_ERROR, "Unable to set "
                                        "%s in dict", cmd_arg_name);
                                goto out;
                        }
                }
        } while (tmp);

        cmd_args_count = wordcount - 3;

        ret = dict_set_str (dict, "command", command);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set command in dict");
                goto out;
        }

        for (i=1; i <= cmd_args_count; i++) {
                in_cmd_args_count++;
                memset (cmd_arg_name, '\0', sizeof(cmd_arg_name));
                snprintf (cmd_arg_name, sizeof(cmd_arg_name),
                          "cmd_arg_%d", in_cmd_args_count);
                ret = dict_set_str (dict, cmd_arg_name,
                                    (char *)words[2+i]);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to set %s in dict",
                               cmd_arg_name);
                        goto out;
                }
        }

        ret = dict_set_int32 (dict, "cmd_args_count", in_cmd_args_count);
        if (ret) {
                gf_log ("", GF_LOG_ERROR,
                        "Unable to set cmd_args_count in dict");
                goto out;
        }

        ret = dict_set_str (dict, "volname", "N/A");
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set volname in dict");
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_SYS_EXEC];
        if (proc && proc->fn) {
                frame = create_frame (THIS, THIS->ctx->pool);
                if (!frame)
                        goto out;
                CLI_LOCAL_INIT (local, words, frame, dict);
                ret = proc->fn (frame, THIS, (void*)dict);
        }
out:
        return ret;
}

int
cli_cmd_copy_file_cbk (struct cli_state *state, struct cli_cmd_word *word,
                       const char **words, int wordcount)
{
        int                    ret      = -1;
        rpc_clnt_procedure_t  *proc     = NULL;
        call_frame_t          *frame    = NULL;
        char                  *filename = "";
        dict_t                *dict     = NULL;
        cli_local_t           *local    = NULL;

        if (wordcount != 4) {
                cli_usage_out (word->pattern);
                goto out;
        }

        dict = dict_new ();
        if (!dict)
                goto out;

        filename = (char*)words[3];
        ret = dict_set_str (dict, "source", filename);
        if (ret)
                 gf_log ("", GF_LOG_ERROR, "Unable to set filename in dict");

        ret = dict_set_str (dict, "volname", "N/A");
        if (ret)
                 gf_log ("", GF_LOG_ERROR, "Unable to set volname in dict");

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_COPY_FILE];
        if (proc && proc->fn) {
                frame = create_frame (THIS, THIS->ctx->pool);
                if (!frame)
                        goto out;
                CLI_LOCAL_INIT (local, words, frame, dict);
                ret = proc->fn (frame, THIS, (void*)dict);
        }
out:
        return ret;
}

int
cli_cmd_system_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                         const char **words, int wordcount)
{
        struct cli_cmd        *cmd = NULL;
        struct cli_cmd        *system_cmd = NULL;
        int                   count     = 0;

        cmd = GF_CALLOC (1, sizeof (cli_system_cmds), cli_mt_cli_cmd);
        memcpy (cmd, cli_system_cmds, sizeof (cli_system_cmds));
        count = (sizeof (cli_system_cmds) / sizeof (struct cli_cmd));
        cli_cmd_sort (cmd, count);

        for (system_cmd = cmd; system_cmd->pattern; system_cmd++)
                cli_out ("%s - %s", system_cmd->pattern, system_cmd->desc);

        GF_FREE (cmd);
        return 0;
}

int
cli_cmd_system_register (struct cli_state *state)
{
        int  ret = 0;
        struct cli_cmd *cmd = NULL;

        for (cmd = cli_system_cmds; cmd->pattern; cmd++) {

                ret = cli_cmd_register (&state->tree, cmd);
                if (ret)
                        goto out;
        }
out:
        return ret;
}
