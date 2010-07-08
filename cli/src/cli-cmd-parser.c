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
#include "protocol-common.h"
#include "dict.h"
#include "gluster1.h"

int32_t
cli_cmd_volume_create_parse (const char **words, int wordcount, dict_t **options)
{
        dict_t  *dict = NULL;
        char    *volname = NULL;
        int     ret = -1;
        gf1_cluster_type type = GF_CLUSTER_TYPE_NONE;
        int     count = 0;
        int     brick_count = 0, brick_index = 0;
        char    brick_list[8192] = {0,};

        GF_ASSERT (words);
        GF_ASSERT (options);

        GF_ASSERT ((strcmp (words[0], "volume")) == 0);
        GF_ASSERT ((strcmp (words[1], "create")) == 0);

        dict = dict_new ();

        if (!dict)
                goto out;

        volname = (char *)words[2];

        GF_ASSERT (volname);

        ret = dict_set_str (dict, "volname", volname);

        if (ret)
                goto out;

        if ((strcasecmp (words[3], "replica")) == 0) {
                type = GF_CLUSTER_TYPE_REPLICATE;
                count = strtol (words[4], NULL, 0);
                if (!count) {
                        /* Wrong number of replica count */
                        ret = -1;
                        goto out;
                }
                ret = dict_set_int32 (dict, "replica-count", count);
                if (ret)
                        goto out;

                brick_index = 5;
        } else if ((strcasecmp (words[3], "stripe")) == 0) {
                type = GF_CLUSTER_TYPE_STRIPE;
                count = strtol (words[4], NULL, 0);
                if (!count) {
                        /* Wrong number of stripe count */
                        ret = -1;
                        goto out;
                }
                ret = dict_set_int32 (dict, "stripe-count", count);
                if (ret)
                        goto out;
                brick_index = 5;
        } else {
                type = GF_CLUSTER_TYPE_NONE;
                brick_index = 3;
        }

        ret = dict_set_int32 (dict, "type", type);
        if (ret)
                goto out;
        strcpy (brick_list, " ");
        while (brick_index < wordcount) {
                GF_ASSERT (words[brick_index]);
                if (!strchr (words[brick_index], ':')) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "wrong brick type, use <HOSTNAME>:<export-dir>");
                        ret = -1;
                        goto out;
                }

                strcat (brick_list, words[brick_index]);
                strcat (brick_list, " ");
                ++brick_count;
                ++brick_index;
                /*
                  char    key[50];
                snprintf (key, 50, "brick%d", ++brick_count);
                ret = dict_set_str (dict, key, (char *)words[brick_index++]);

                if (ret)
                        goto out;
                */
        }
        ret = dict_set_str (dict, "bricks", brick_list);
        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "count", brick_count);
        if (ret)
                goto out;

        *options = dict;

out:
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to parse create volume CLI");
                if (dict)
                        dict_destroy (dict);
        }

        return ret;
}


int32_t
cli_cmd_volume_set_parse (const char **words, int wordcount, dict_t **options)
{
        dict_t  *dict = NULL;
        char    *volname = NULL;
        int     ret = -1;
        int     count = 0;
        char    *key = NULL;
        char    *value = NULL;
        int     i = 0;
        char    str[50] = {0,};
        
        GF_ASSERT (words);
        GF_ASSERT (options);

        GF_ASSERT ((strcmp (words[0], "volume")) == 0);
        GF_ASSERT ((strcmp (words[1], "set")) == 0);

        dict = dict_new ();

        if (!dict)
                goto out;

        volname = (char *)words[2];

        GF_ASSERT (volname);

        ret = dict_set_str (dict, "volname", volname);

        if (ret)
                goto out;

        for (i = 3; i < wordcount; i++) {
                key = strtok ((char *)words[i], "=");
                value = strtok (NULL, "=");

                GF_ASSERT (key);
                GF_ASSERT (value);

                count++;

                sprintf (str, "key%d", count);
                ret = dict_set_str (dict, str, key);
                if (ret)
                        goto out;

                sprintf (str, "value%d", count);
                ret = dict_set_str (dict, str, value);

                if (ret)
                        goto out;
        }

        ret = dict_set_int32 (dict, "count", count);

        if (ret)
                goto out;

        *options = dict;

out:
        if (ret) {
                if (dict)
                        dict_destroy (dict);
        }

        return ret;
}

int32_t
cli_cmd_volume_add_brick_parse (const char **words, int wordcount, 
                                dict_t **options)
{
        dict_t  *dict = NULL;
        char    *volname = NULL;
        int     ret = -1;
        gf1_cluster_type type = GF_CLUSTER_TYPE_NONE;
        int     count = 0;
        char    key[50];
        int     brick_count = 0, brick_index = 0;
        
        GF_ASSERT (words);
        GF_ASSERT (options);

        GF_ASSERT ((strcmp (words[0], "volume")) == 0);
        GF_ASSERT ((strcmp (words[1], "add-brick")) == 0);

        dict = dict_new ();

        if (!dict)
                goto out;

        volname = (char *)words[2];

        GF_ASSERT (volname);

        ret = dict_set_str (dict, "volname", volname);

        if (ret)
                goto out;

        if ((strcasecmp (words[3], "replica")) == 0) {
                type = GF_CLUSTER_TYPE_REPLICATE;
                count = strtol (words[4], NULL, 0);
                brick_index = 5;
        } else if ((strcasecmp (words[3], "stripe")) == 0) {
                type = GF_CLUSTER_TYPE_STRIPE;
                count = strtol (words[4], NULL, 0);
                brick_index = 5;
        } else {
                brick_index = 3;
        }

        ret = dict_set_int32 (dict, "type", type);

        if (ret)
                goto out;

        while (brick_index < wordcount) {
                GF_ASSERT (words[brick_index]);

                snprintf (key, 50, "brick%d", ++brick_count);
                ret = dict_set_str (dict, key, (char *)words[brick_index++]);

                if (ret)
                        goto out;
        }

        ret = dict_set_int32 (dict, "count", brick_count);

        if (ret)
                goto out;

        *options = dict;

out:
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to parse add-brick CLI");
                if (dict)
                        dict_destroy (dict);
        }

        return ret;
}


int32_t
cli_cmd_volume_remove_brick_parse (const char **words, int wordcount, 
                                   dict_t **options)
{
        dict_t  *dict = NULL;
        char    *volname = NULL;
        int     ret = -1;
        gf1_cluster_type type = GF_CLUSTER_TYPE_NONE;
        int     count = 0;
        char    key[50];
        int     brick_count = 0, brick_index = 0;
        
        GF_ASSERT (words);
        GF_ASSERT (options);

        GF_ASSERT ((strcmp (words[0], "volume")) == 0);
        GF_ASSERT ((strcmp (words[1], "remove-brick")) == 0);

        dict = dict_new ();

        if (!dict)
                goto out;

        volname = (char *)words[2];

        GF_ASSERT (volname);

        ret = dict_set_str (dict, "volname", volname);

        if (ret)
                goto out;

        if ((strcasecmp (words[3], "replica")) == 0) {
                type = GF_CLUSTER_TYPE_REPLICATE;
                count = strtol (words[4], NULL, 0);
                brick_index = 5;
        } else if ((strcasecmp (words[3], "stripe")) == 0) {
                type = GF_CLUSTER_TYPE_STRIPE;
                count = strtol (words[4], NULL, 0);
                brick_index = 5;
        } else {
                brick_index = 3;
        }

        ret = dict_set_int32 (dict, "type", type);

        if (ret)
                goto out;

        while (brick_index < wordcount) {
                GF_ASSERT (words[brick_index]);

                snprintf (key, 50, "brick%d", ++brick_count);
                ret = dict_set_str (dict, key, (char *)words[brick_index++]);

                if (ret)
                        goto out;
        }

        ret = dict_set_int32 (dict, "count", brick_count);

        if (ret)
                goto out;

        *options = dict;

out:
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to parse remove-brick CLI");
                if (dict)
                        dict_destroy (dict);
        }

        return ret;
}


int32_t
cli_cmd_volume_replace_brick_parse (const char **words, int wordcount, 
                                   dict_t **options)
{
        dict_t  *dict = NULL;
        char    *volname = NULL;
        int     ret = -1;
        char    *op = NULL;
        int     op_index = 0;
        gf1_cli_replace_op replace_op = GF_REPLACE_OP_NONE;
        
        GF_ASSERT (words);
        GF_ASSERT (options);

        GF_ASSERT ((strcmp (words[0], "volume")) == 0);
        GF_ASSERT ((strcmp (words[1], "replace-brick")) == 0);

        dict = dict_new ();

        if (!dict)
                goto out;

        volname = (char *)words[2];

        GF_ASSERT (volname);

        ret = dict_set_str (dict, "volname", volname);

        if (ret)
                goto out;

        if (strchr ((char *)words[3], ':')) {
                ret = dict_set_str (dict, "src-brick", (char *)words[3]);

                if (ret)
                        goto out;

                GF_ASSERT (words[4]);

                ret = dict_set_str (dict, "dst-brick", (char *)words[4]);

                if (ret)
                        goto out;

                op_index = 5;
        } else {
                op_index = 3;
        }

        GF_ASSERT (words[op_index]);

        op = (char *) words[op_index];

        if (!strcasecmp ("start", op)) {
                replace_op = GF_REPLACE_OP_START;
        } else if (!strcasecmp ("stop", op)) {
                replace_op = GF_REPLACE_OP_STOP;
        } else if (!strcasecmp ("pause", op)) {
                replace_op = GF_REPLACE_OP_PAUSE;
        } else if (!strcasecmp ("abort", op)) {
                replace_op = GF_REPLACE_OP_ABORT;
        } else if (!strcasecmp ("status", op)) {
                replace_op = GF_REPLACE_OP_STATUS;
        }

        GF_ASSERT (replace_op != GF_REPLACE_OP_NONE);

        ret = dict_set_int32 (dict, "operation", (int32_t) replace_op);

        if (ret)
                goto out;
                
        *options = dict;

out:
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to parse remove-brick CLI");
                if (dict)
                        dict_destroy (dict);
        }

        return ret;
}
