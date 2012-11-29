/*
  CLI for BD translator

  Copyright IBM, Corp. 2012

  This file is part of GlusterFS.

  Author:
  M. Mohan Kumar <mohan@in.ibm.com>

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "cli.h"
#include "cli-cmd.h"
#include <string.h>

extern rpc_clnt_prog_t *cli_rpc_prog;

int
cli_cmd_bd_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                      const char **words, int wordcount);

int32_t
cli_cmd_bd_parse (dict_t *dict, const char **words)
{
        char          *volname  = NULL;
        char          *buff     = NULL;
        char          *buffp    = NULL;
        int            ret      = -1;
        char          *save     = NULL;
        char          *path     = NULL;
        char          *size     = NULL;
        char          *eptr     = NULL;
        gf_xl_bd_op_t  bd_op    = GF_BD_OP_INVALID;
        char          *dest_lv  = NULL;


        /* volname:/path */
        if (!strchr (words[2], ':') || !strchr (words[2], '/')) {
                cli_out ("invalid parameter %s, needs <volname:/path>",
                                words[2]);
                return -1;
        }
        buff = buffp = gf_strdup (words[2]);
        volname = strtok_r (buff, ":", &save);
        path = strtok_r (NULL, ":", &save);

        ret = dict_set_dynstr (dict, "volname", gf_strdup (volname));
        if (ret)
                goto out;

        ret = dict_set_dynstr (dict, "path", gf_strdup (path));
        if (ret)
                goto out;

        if (!strcasecmp (words[1], "create"))
                bd_op = GF_BD_OP_NEW_BD;
        else if (!strcasecmp (words[1], "delete"))
                bd_op = GF_BD_OP_DELETE_BD;
        else if (!strcasecmp (words[1], "clone"))
                bd_op = GF_BD_OP_CLONE_BD;
        else if (!strcasecmp (words[1], "snapshot"))
                bd_op = GF_BD_OP_SNAPSHOT_BD;
        else
                return -1;

        ret = dict_set_int32 (dict, "bd-op", bd_op);
        if (ret)
                goto out;

        if (bd_op == GF_BD_OP_NEW_BD) {
                /* If no suffix given we will treat it as MB */
                strtoull (words[3], &eptr, 0);
                /* no suffix */
                if (!eptr[0])
                        gf_asprintf (&size, "%sMB", words[3]);
                else
                        size = gf_strdup (words[3]);

                ret = dict_set_dynstr (dict, "size", size);
                if (ret)
                        goto out;
        } else if (bd_op == GF_BD_OP_SNAPSHOT_BD ||
                   bd_op == GF_BD_OP_CLONE_BD) {
                /*
                 * dest_lv should be just dest_lv, we don't support
                 * cloning/snapshotting to a different volume or vg
                 */
                if (strchr (words[3], ':') || strchr (words[3], '/')) {
                        cli_err ("invalid parameter %s, volname/vg not needed",
                                 words[3]);
                        ret = -1;
                        goto out;
                }
                dest_lv = gf_strdup (words[3]);
                ret = dict_set_dynstr (dict, "dest_lv", dest_lv);
                if (ret)
                        goto out;

                /* clone needs size as parameter */
                if (bd_op == GF_BD_OP_SNAPSHOT_BD) {
                        ret = dict_set_dynstr (dict, "size",
                                               gf_strdup (words[4]));
                        if (ret)
                                goto out;
                }
        }

        ret = 0;
out:
        GF_FREE (buffp);
        return ret;
}

/*
 * bd create <volname>:/path <size>
 * bd delete <volname>:/path
 * bd clone <volname>:/path <newbd>
 * bd snapshot <volname>:/<path> <newbd> <size>
 */
int32_t
cli_cmd_bd_validate (const char **words, int wordcount, dict_t **options)
{
        dict_t  *dict = NULL;
        int     ret   = -1;
        char    *op[] = { "create", "delete", "clone", "snapshot", NULL };
        int     index = 0;

        for (index = 0; op[index]; index++)
                if (!strcasecmp (words[1], op[index]))
                        break;

        if (!op[index])
                return -1;

        dict = dict_new ();
        if (!dict)
                goto out;

        if (!strcasecmp (words[1], "create")) {
                if (wordcount != 4)
                        goto out;
        } else if (!strcasecmp (words[1], "delete")) {
                if (wordcount != 3)
                        goto out;
        } else if (!strcasecmp (words[1], "clone")) {
                if (wordcount != 4)
                        goto out;
        } else if (!strcasecmp (words[1], "snapshot")) {
                if (wordcount != 5)
                        goto out;
        } else {
                ret = -1;
                goto out;
        }

        ret = cli_cmd_bd_parse (dict, words);
        if (ret < 0)
                goto out;

        *options = dict;
        ret = 0;
out:
        if (ret)
                dict_unref (dict);
        return ret;
}

int
cli_cmd_bd_cbk (struct cli_state *state, struct cli_cmd_word *word,
                         const char **words, int wordcount)
{
        int                     ret         = -1;
        rpc_clnt_procedure_t    *proc       = NULL;
        call_frame_t            *frame      = NULL;
        int                     sent        = 0;
        int                     parse_error = 0;
        dict_t                  *options    = NULL;
        cli_local_t             *local = NULL;

        proc = &cli_rpc_prog->proctable[GLUSTER_CLI_BD_OP];

        frame = create_frame (THIS, THIS->ctx->pool);
        if (!frame)
                goto out;

        ret = cli_cmd_bd_validate (words, wordcount, &options);
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
        if (options)
                dict_unref (options);

        if (ret) {
                cli_cmd_sent_status_get (&sent);
                if ((sent == 0) && (parse_error == 0))
                        cli_out ("BD op failed!");
        }

        CLI_STACK_DESTROY (frame);

        return ret;
}

struct cli_cmd cli_bd_cmds[] = {
        { "bd help",
           cli_cmd_bd_help_cbk,
           "display help for bd command"},
        { "bd create <volname>:<bd> <size>",
          cli_cmd_bd_cbk,
          "\n\tcreate a block device where size can be "
          "suffixed with KB, MB etc. Default size is in MB"},
        { "bd delete <volname>:<bd>",
          cli_cmd_bd_cbk,
          "Delete a block device"},
        { "bd clone <volname>:<bd> <newbd>",
          cli_cmd_bd_cbk,
          "clone device"},
        { "bd snapshot <volname>:<bd> <newbd> <size>",
          cli_cmd_bd_cbk,
          "\n\tsnapshot device where size can be "
          "suffixed with KB, MB etc. Default size is in MB"},
        { NULL, NULL, NULL }
};

int
cli_cmd_bd_help_cbk (struct cli_state *state, struct cli_cmd_word *in_word,
                      const char **words, int wordcount)
{
        struct cli_cmd *cmd = NULL;

        for (cmd = cli_bd_cmds; cmd->pattern; cmd++)
                if (_gf_false == cmd->disable)
                        cli_out ("%s - %s", cmd->pattern, cmd->desc);

        return 0;
}

int
cli_cmd_bd_register (struct cli_state *state)
{
        int  ret = 0;
        struct cli_cmd *cmd = NULL;

        for (cmd = cli_bd_cmds; cmd->pattern; cmd++) {
                ret = cli_cmd_register (&state->tree, cmd);
                if (ret)
                        goto out;
        }
out:
        return ret;
}
