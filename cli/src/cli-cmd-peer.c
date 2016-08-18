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
#include "cli1-xdr.h"
#include "protocol-common.h"
#include "events.h"

extern struct rpc_clnt *global_rpc;

extern rpc_clnt_prog_t *cli_rpc_prog;

int cli_cmd_peer_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                      const char **words, int wordcount);

int
cli_cmd_peer_probe_cbk (struct cli_state *state, struct cli_cmd_word *word,
                   const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        dict_t                  *dict = NULL;
        int                     sent = 0;
        int                     parse_error = 0;
        cli_local_t             *local = NULL;

        if (!(wordcount == 3)) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_PROBE];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_str (dict, "hostname", (char *)words[2]);
        if (ret)
                goto out;

        ret = valid_internet_address ((char *) words[2], _gf_false);
        if (ret == 1) {
                ret = 0;
        } else {
                cli_out ("%s is an invalid address", words[2]);
                cli_usage_out (word->pattern);
                parse_error = 1;
                ret = -1;
                goto out;
        }
/*        if (words[3]) {
                ret = dict_set_str (dict, "port", (char *)words[3]);
                if (ret)
                        goto out;
        }
*/

        CLI_LOCAL_INIT (local, words, frame, dict);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Peer probe failed");
        }

        CLI_STACK_DESTROY (frame);

        if (ret == 0) {
                gf_event (EVENT_PEER_ATTACH, "host=%s", (char *)words[2]);
        }

        return ret;
}


int
cli_cmd_peer_deprobe_cbk (struct cli_state *state, struct cli_cmd_word *word,
                          const char **words, int wordcount)
{
        int                   ret   = -1;
        rpc_clnt_procedure_t *proc  = NULL;
        call_frame_t         *frame = NULL;
        dict_t               *dict  = NULL;
        int                  flags = 0;
        int                  sent = 0;
        int                  parse_error = 0;
        cli_local_t          *local = NULL;

        if ((wordcount < 3) || (wordcount > 4)) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_DEPROBE];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        dict = dict_new ();

        ret = dict_set_str (dict, "hostname", (char *)words[2]);
        if (ret)
                goto out;

/*        if (words[3]) {
                ret = dict_set_str (dict, "port", (char *)words[3]);
                if (ret)
                        goto out;
        }
*/
        if (wordcount == 4) {
                if (!strcmp("force", words[3]))
                        flags |= GF_CLI_FLAG_OP_FORCE;
                else {
                        ret = -1;
                        cli_usage_out (word->pattern);
                        parse_error = 1;
                        goto out;
                }
        }
        ret = dict_set_int32 (dict, "flags", flags);
        if (ret)
                goto out;

        CLI_LOCAL_INIT (local, words, frame, dict);

        if (proc->fn) {
                ret = proc->fn (frame, THIS, dict);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Peer detach failed");
        }

        CLI_STACK_DESTROY (frame);

        if (ret == 0) {
                gf_event (EVENT_PEER_DETACH, "host=%s", (char *)words[2]);
        }

        return ret;
}

int
cli_cmd_peer_status_cbk (struct cli_state *state, struct cli_cmd_word *word,
                        const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        int                     sent = 0;
        int                     parse_error = 0;

        if (wordcount != 2) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_LIST_FRIENDS];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if (proc->fn) {
                ret = proc->fn (frame, THIS, (void *)GF_CLI_LIST_PEERS);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("Peer status failed");
        }

        CLI_STACK_DESTROY (frame);

        return ret;
}

int
cli_cmd_pool_list_cbk (struct cli_state *state, struct cli_cmd_word *word,
                       const char **words, int wordcount)
{
        int                     ret = -1;
        rpc_clnt_procedure_t    *proc = NULL;
        call_frame_t            *frame = NULL;
        int                     sent = 0;
        int                     parse_error = 0;

        if (wordcount != 2) {
                cli_usage_out (word->pattern);
                parse_error = 1;
                goto out;
        }

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_LIST_FRIENDS];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        if (proc->fn) {
                ret = proc->fn (frame, THIS,
                                (void *)GF_CLI_LIST_POOL_NODES);
        }

out:
        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_err ("pool list: command execution failed");
        }

        CLI_STACK_DESTROY (frame);

        return ret;
}

struct cli_cmd cli_probe_cmds[] = {
        { "peer probe { <HOSTNAME> | <IP-address> }",
          cli_cmd_peer_probe_cbk,
          "probe peer specified by <HOSTNAME>"},

        { "peer detach { <HOSTNAME> | <IP-address> } [force]",
          cli_cmd_peer_deprobe_cbk,
          "detach peer specified by <HOSTNAME>"},

        { "peer status",
          cli_cmd_peer_status_cbk,
          "list status of peers"},

	{ "peer help",
           cli_cmd_peer_help_cbk,
           "Help command for peer "},

        { "pool list",
          cli_cmd_pool_list_cbk,
          "list all the nodes in the pool (including localhost)"},

        { NULL, NULL, NULL }
};

int
cli_cmd_peer_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                      const char **words, int wordcount)
{
        struct cli_cmd        *cmd = NULL;
        struct cli_cmd        *probe_cmd = NULL;
        int                   count     = 0;

        cmd = GF_CALLOC (1, sizeof (cli_probe_cmds), cli_mt_cli_cmd);
        memcpy (cmd, cli_probe_cmds, sizeof (cli_probe_cmds));
        count = (sizeof (cli_probe_cmds) / sizeof (struct cli_cmd));
        cli_cmd_sort (cmd, count);



        for (probe_cmd = cmd; probe_cmd->pattern; probe_cmd++)
                cli_out ("%s - %s", probe_cmd->pattern, probe_cmd->desc);

        GF_FREE (cmd);
        return 0;
}

int
cli_cmd_probe_register (struct cli_state *state)
{
        int  ret = 0;
        struct cli_cmd *cmd = NULL;

        for (cmd = cli_probe_cmds; cmd->pattern; cmd++) {

                ret = cli_cmd_register (&state->tree, cmd);
                if (ret)
                        goto out;
        }
out:
        return ret;
}
