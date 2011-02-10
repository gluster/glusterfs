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
#include "dict.h"

#include "protocol-common.h"
#include "cli1-xdr.h"

int32_t
cli_cmd_volume_create_parse (const char **words, int wordcount, dict_t **options)
{
        dict_t  *dict = NULL;
        char    *volname = NULL;
        char    *delimiter = NULL;
        int     ret = -1;
        gf1_cluster_type type = GF_CLUSTER_TYPE_NONE;
        int     count = 1;
        int     brick_count = 0, brick_index = 0;
        int     brick_list_size = 1;
        char    brick_list[120000] = {0,};
        int     i = 0;
        char    *tmp_list = NULL;
        char    *tmpptr = NULL;
        int     j = 0;
        char    *host_name = NULL;
        char    *tmp = NULL;
        char    *freeptr = NULL;
        char    *trans_type = NULL;
        int32_t index = 0;
        char    *free_list_ptr = NULL;
        char    *bricks = NULL;

        GF_ASSERT (words);
        GF_ASSERT (options);

        GF_ASSERT ((strcmp (words[0], "volume")) == 0);
        GF_ASSERT ((strcmp (words[1], "create")) == 0);

        dict = dict_new ();

        if (!dict)
                goto out;

        if (wordcount < 3)
                goto out;

        volname = (char *)words[2];

        GF_ASSERT (volname);

        /* Validate the volume name here itself */
        {
                if (volname[0] == '-')
                        goto out;

                if (!strcmp (volname, "all")) {
                        cli_out ("\"all\" cannot be the name of a volume.");
                        goto out;
                }

                if (strchr (volname, '/'))
                        goto out;

                if (strlen (volname) > 512)
                        goto out;

                for (i = 0; i < strlen (volname); i++)
                        if (!isalnum (volname[i]) && (volname[i] != '_') && (volname[i] != '-'))
                                goto out;
        }


        ret = dict_set_str (dict, "volname", volname);
        if (ret)
                goto out;

        if (wordcount < 4) {
                ret = -1;
                goto out;
        }
        if ((strcasecmp (words[3], "replica")) == 0) {
                type = GF_CLUSTER_TYPE_REPLICATE;
                if (wordcount < 5) {
                        ret = -1;
                        goto out;
                }
                count = strtol (words[4], NULL, 0);
                if (!count || (count < 2)) {
                        cli_out ("replica count should be greater than 1");
                        ret = -1;
                        goto out;
                }
                ret = dict_set_int32 (dict, "replica-count", count);
                if (ret)
                        goto out;
                brick_index = 5;
        } else if ((strcasecmp (words[3], "stripe")) == 0) {
                type = GF_CLUSTER_TYPE_STRIPE;
                if (wordcount < 5) {
                        ret = -1;
                        goto out;
                }
                count = strtol (words[4], NULL, 0);
                if (!count || (count < 2)) {
                        cli_out ("stripe count should be greater than 1");
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

        if (type)
                index = 5;
        else
                index = 3;

        if (wordcount < (index + 1)) {
                ret = -1;
                goto out;
        }

        if (strcasecmp(words[index], "transport") == 0) {
                brick_index = index+2;
                if (wordcount < (index + 2)) {
                        ret = -1;
                        goto out;
                }

                if ((strcasecmp (words[index+1], "tcp") == 0)) {
                        trans_type = gf_strdup ("tcp");
                } else if ((strcasecmp (words[index+1], "rdma") == 0)) {
                        trans_type = gf_strdup ("rdma");
                } else {
                        gf_log ("", GF_LOG_ERROR, "incorrect transport"
                                       " protocol specified");
                        ret = -1;
                        goto out;
                }
        } else {
                trans_type = gf_strdup ("tcp");
        }

        ret = dict_set_dynstr (dict, "transport", trans_type);
        if (ret)
                goto out;

        strcpy (brick_list, " ");
        while (brick_index < wordcount) {
                delimiter = strchr (words[brick_index], ':');
                if (!delimiter || delimiter == words[brick_index]
                    || *(delimiter+1) != '/') {
                        cli_out ("wrong brick type: %s, use <HOSTNAME>:"
                                 "<export-dir-abs-path>", words[brick_index]);
                        ret = -1;
                        goto out;
                } else {
                        cli_path_strip_trailing_slashes (delimiter + 1);
                }
                if ((brick_list_size + strlen (words[brick_index]) + 1) > 120000) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "total brick list is larger than a request "
                                "can take (brick_count %d)", brick_count);
                        ret = -1;
                        goto out;
                }

                host_name = gf_strdup(words[brick_index]);
                if (!host_name) {
                        ret = -1;
                        gf_log("cli",GF_LOG_ERROR, "Unable to allocate "
                               "memory");
                        goto out;
                }
                freeptr = host_name;

                strtok_r(host_name, ":", &tmp);
                if (!(strcmp(host_name, "localhost") &&
                      strcmp (host_name, "127.0.0.1"))) {
                        cli_out ("Please provide a valid hostname/ip other "
                                 "than localhost or 127.0.0.1");
                        ret = -1;
                        GF_FREE(freeptr);
                        goto out;
                }
                GF_FREE (freeptr);
                tmp_list = strdup(brick_list+1);
                free_list_ptr = tmp_list;
                j = 0;
                while(( brick_count != 0) && (j < brick_count)) {
                        strtok_r (tmp_list, " ", &tmpptr);
                        if (!(strcmp (tmp_list, words[brick_index]))) {
                                ret = -1;
                                cli_out ("Found duplicate"
                                         " exports %s",words[brick_index]);
                                if (free_list_ptr)
                                        free (free_list_ptr);
                                goto out;
                       }
                       tmp_list = tmpptr;
                       j++;
                }
                strcat (brick_list, words[brick_index]);
                strcat (brick_list, " ");
                brick_list_size += (strlen (words[brick_index]) + 1);
                ++brick_count;
                ++brick_index;
                /*
                  char    key[50];
                snprintf (key, 50, "brick%d", ++brick_count);
                ret = dict_set_str (dict, key, (char *)words[brick_index++]);

                if (ret)
                        goto out;
                */
                if (free_list_ptr)
                        free (free_list_ptr);
        }

        /* If brick-count is not valid when replica or stripe is
           given, exit here */
        if (!brick_count) {
                cli_out ("No bricks specified");
                ret = -1;
                goto out;
        }

        if (brick_count % count) {
                if (type == GF_CLUSTER_TYPE_STRIPE)
                        cli_out ("number of bricks is not a multiple of "
                                 "stripe count");
                else if (type == GF_CLUSTER_TYPE_REPLICATE)
                        cli_out ("number of bricks is not a multiple of "
                                 "replica count");
                ret = -1;
                goto out;
        }

        bricks = gf_strdup (brick_list);
        if (!bricks) {
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (dict, "bricks", bricks);
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
cli_cmd_volume_reset_parse (const char **words, int wordcount, dict_t **options)
{
        dict_t  *dict = NULL;
        char    *volname = NULL;
        int     ret = -1;

        GF_ASSERT (words);
        GF_ASSERT (options);

        GF_ASSERT ((strcmp (words[0], "volume")) == 0);
        GF_ASSERT ((strcmp (words[1], "reset")) == 0);

        dict = dict_new ();

        if (!dict)
                goto out;

        if (wordcount < 3)
                goto out;

        volname = (char *)words[2];

        if (!volname) {
                ret = -1;
                goto out;
        }

        ret = dict_set_str (dict, "volname", volname);

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

        if (wordcount < 4)
                goto out;

        volname = (char *)words[2];

        GF_ASSERT (volname);

        ret = dict_set_str (dict, "volname", volname);

        if (ret)
                goto out;


        for (i = 3; i < wordcount; i+=2) {

		key = (char *) words[i];
		value = (char *) words[i+1];

                if ( key && !value ) {
                        if ( !strcmp (key, "history")) {
                                ret = dict_set_str (dict, key, "history");
                                if (ret)
                                        goto out;
                                ret = dict_set_int32 (dict, "count", 1);
                                if (ret)
                                        goto out;
                                *options = dict;
                                goto out;
                        }
                }

		if ( !key || !value) {
			ret = -1;
			goto out;
        	}

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

        ret = dict_set_int32 (dict, "count", wordcount-3);

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
        char    *delimiter = NULL;
        int     ret = -1;
        int     brick_count = 0, brick_index = 0;
        int     brick_list_size = 1;
        char    brick_list[120000] = {0,};
        int     j = 0;
        char    *tmp_list = NULL;
        char    *tmpptr = NULL;
        char    *host_name = NULL;
        char    *tmp = NULL;
        char    *freeptr = NULL;
        char    *free_list_ptr = NULL;
        char    *bricks = NULL;

        GF_ASSERT (words);
        GF_ASSERT (options);

        GF_ASSERT ((strcmp (words[0], "volume")) == 0);
        GF_ASSERT ((strcmp (words[1], "add-brick")) == 0);

        dict = dict_new ();

        if (!dict)
                goto out;

        if (wordcount < 3)
                goto out;

        volname = (char *)words[2];

        GF_ASSERT (volname);

        ret = dict_set_str (dict, "volname", volname);

        if (ret)
                goto out;

        if (wordcount < 4) {
                ret = -1;
                goto out;
        }

        brick_index = 3;
        strcpy (brick_list, " ");
        while (brick_index < wordcount) {
                delimiter = strchr (words[brick_index], ':');
                if (!delimiter || delimiter == words[brick_index]
                    || *(delimiter+1) != '/') {
                        cli_out ("wrong brick type: %s, use <HOSTNAME>:"
                                 "<export-dir-abs-path>", words[brick_index]);
                        ret = -1;
                        goto out;
                } else {
                        cli_path_strip_trailing_slashes (delimiter + 1);
                }

                if ((brick_list_size + strlen (words[brick_index]) + 1) > 120000) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "total brick list is larger than a request "
                                "can take (brick_count %d)", brick_count);
                        ret = -1;
                        goto out;
                }

                host_name = gf_strdup(words[brick_index]);
                if (!host_name) {
                        ret = -1;
                        gf_log ("cli", GF_LOG_ERROR, "unable to allocate "
                                "memory");
                        goto out;
                }
                freeptr = host_name;
                strtok_r(host_name, ":", &tmp);
                if (!(strcmp(host_name, "localhost") &&
                      strcmp (host_name, "127.0.0.1"))) {
                        cli_out ("Please provide a valid hostname/ip other "
                                 "localhost or 127.0.0.1");
                        ret = -1;
                        GF_FREE (freeptr);
                        goto out;
                }
                GF_FREE (freeptr);

                tmp_list = strdup(brick_list+1);
                free_list_ptr = tmp_list;
                j = 0;
                while(( brick_count != 0) && (j < brick_count)) {
                        strtok_r (tmp_list, " ", &tmpptr);
                        if (!(strcmp (tmp_list, words[brick_index]))) {
                                ret = -1;
                                cli_out ("Found duplicate"
                                         " exports %s",words[brick_index]);
                                if (free_list_ptr)
                                        free (free_list_ptr);
                                goto out;
                       }
                       tmp_list = tmpptr;
                       j++;
                }

                strcat (brick_list, words[brick_index]);
                strcat (brick_list, " ");
                brick_list_size += (strlen (words[brick_index]) + 1);
                ++brick_count;
                ++brick_index;
                /*
                  char    key[50];
                snprintf (key, 50, "brick%d", ++brick_count);
                ret = dict_set_str (dict, key, (char *)words[brick_index++]);

                if (ret)
                        goto out;
                */
                if (free_list_ptr)
                        free (free_list_ptr);
        }

        bricks = gf_strdup (brick_list);
        if (!bricks) {
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (dict, "bricks", bricks);
        if (ret)
                goto out;

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
        char    *delimiter = NULL;
        int     ret = -1;
        char    key[50];
        int     brick_count = 0, brick_index = 0;
        int32_t tmp_index = 0;
        int32_t j = 0;
        char    *tmp_brick = NULL;
        char    *tmp_brick1 = NULL;

        GF_ASSERT (words);
        GF_ASSERT (options);

        GF_ASSERT ((strcmp (words[0], "volume")) == 0);
        GF_ASSERT ((strcmp (words[1], "remove-brick")) == 0);

        dict = dict_new ();

        if (!dict)
                goto out;

        if (wordcount < 3)
                goto out;

        volname = (char *)words[2];

        GF_ASSERT (volname);

        ret = dict_set_str (dict, "volname", volname);

        if (ret)
                goto out;

        if (wordcount < 4) {
                ret = -1;
                goto out;
        }

        brick_index = 3;

        if (ret)
                goto out;

        tmp_index = brick_index;
        tmp_brick = GF_MALLOC(2048 * sizeof(*tmp_brick), gf_common_mt_char);

        if (!tmp_brick) {
                gf_log ("",GF_LOG_ERROR,"cli_cmd_volume_remove_brick_parse: "
                        "Unable to get memory");
                ret = -1;
                goto out;
        }
 
        tmp_brick1 = GF_MALLOC(2048 * sizeof(*tmp_brick1), gf_common_mt_char);

        if (!tmp_brick1) {
                gf_log ("",GF_LOG_ERROR,"cli_cmd_volume_remove_brick_parse: "
                        "Unable to get memory");
                ret = -1;
                goto out;
        }

        while (brick_index < wordcount) {
                delimiter = strchr(words[brick_index], ':');
                if (!delimiter || delimiter == words[brick_index]
                    || *(delimiter+1) != '/') {
                        cli_out ("wrong brick type: %s, use <HOSTNAME>:"
                                 "<export-dir-abs-path>", words[brick_index]);
                        ret = -1;
                        goto out;
                } else {
                        cli_path_strip_trailing_slashes (delimiter + 1);
                }

                j = tmp_index;
                strcpy(tmp_brick, words[brick_index]);
                while ( j < brick_index) {
                        strcpy(tmp_brick1, words[j]);
                        if (!(strcmp (tmp_brick, tmp_brick1))) {
                                gf_log("",GF_LOG_ERROR, "Duplicate bricks"
                                       " found %s", words[brick_index]);
                                cli_out("Duplicate bricks found %s",
                                        words[brick_index]);
                                ret = -1;
                                goto out;
                        }
                        j++;
                }
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

        if (tmp_brick)
                GF_FREE (tmp_brick);
        if (tmp_brick1)
                GF_FREE (tmp_brick1);

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
        char    *delimiter = NULL;
        gf1_cli_replace_op replace_op = GF_REPLACE_OP_NONE;

        GF_ASSERT (words);
        GF_ASSERT (options);

        GF_ASSERT ((strcmp (words[0], "volume")) == 0);
        GF_ASSERT ((strcmp (words[1], "replace-brick")) == 0);

        dict = dict_new ();

        if (!dict)
                goto out;

        if (wordcount < 3)
                goto out;

        volname = (char *)words[2];

        GF_ASSERT (volname);

        ret = dict_set_str (dict, "volname", volname);

        if (ret)
                goto out;

        if (wordcount < 4) {
                ret = -1;
                goto out;
        }

        delimiter = strchr ((char *)words[3], ':');
        if (!delimiter || delimiter == words[3]
            || *(delimiter+1) != '/') {
                cli_out ("wrong brick type: %s, use "
                        "<HOSTNAME>:<export-dir-abs-path>", words[3]);
                ret = -1;
                goto out;
        } else {
                cli_path_strip_trailing_slashes (delimiter + 1);
        }
        ret = dict_set_str (dict, "src-brick", (char *)words[3]);

        if (ret)
                goto out;

        if (wordcount < 5) {
                ret = -1;
                goto out;
        }

        delimiter = strchr ((char *)words[4], ':');
        if (!delimiter || delimiter == words[4]
            || *(delimiter+1) != '/') {
                cli_out ("wrong brick type: %s, use "
                        "<HOSTNAME>:<export-dir-abs-path>", words[4]);
                ret = -1;
                goto out;
        } else {
                cli_path_strip_trailing_slashes (delimiter + 1);
        }


        ret = dict_set_str (dict, "dst-brick", (char *)words[4]);

        if (ret)
                goto out;

        op_index = 5;
        if ((wordcount < (op_index + 1))) {
                ret = -1;
                goto out;
        }

        op = (char *) words[op_index];

        if (!strcasecmp ("start", op)) {
                replace_op = GF_REPLACE_OP_START;
        } else if (!strcasecmp ("commit", op)) {
                replace_op = GF_REPLACE_OP_COMMIT;
        } else if (!strcasecmp ("pause", op)) {
                replace_op = GF_REPLACE_OP_PAUSE;
        } else if (!strcasecmp ("abort", op)) {
                replace_op = GF_REPLACE_OP_ABORT;
        } else if (!strcasecmp ("status", op)) {
                replace_op = GF_REPLACE_OP_STATUS;
        }

        /* commit force option */
        op_index = 6;

        if (wordcount > (op_index + 1)) {
                ret = -1;
                goto out;
        }

        if (wordcount == (op_index + 1)) {
                op = (char *) words[op_index];
                if (!strcasecmp ("force", op)) {
                        replace_op = GF_REPLACE_OP_COMMIT_FORCE;
                }
        }

        if (replace_op == GF_REPLACE_OP_NONE) {
                ret = -1;
                goto out;
        }

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

int32_t
cli_cmd_log_filename_parse (const char **words, int wordcount, dict_t **options)
{
        dict_t  *dict = NULL;
        char    *volname = NULL;
        char    *str = NULL;
        int     ret = -1;
        char    *delimiter = NULL;

        GF_ASSERT (words);
        GF_ASSERT (options);

        GF_ASSERT ((strcmp (words[0], "volume")) == 0);
        GF_ASSERT ((strcmp (words[1], "log")) == 0);
        GF_ASSERT ((strcmp (words[2], "filename")) == 0);

        dict = dict_new ();
        if (!dict)
                goto out;

        volname = (char *)words[3];
        GF_ASSERT (volname);

        ret = dict_set_str (dict, "volname", volname);
        if (ret)
                goto out;

        str = (char *)words[4];
        if (strchr (str, ':')) {
                delimiter = strchr (words[4], ':');
                if (!delimiter || delimiter == words[4]
                    || *(delimiter+1) != '/') {
                        cli_out ("wrong brick type: %s, use <HOSTNAME>:"
                                 "<export-dir-abs-path>", words[4]);
                        ret = -1;
                        goto out;
                } else {
                        cli_path_strip_trailing_slashes (delimiter + 1);
                }
                ret = dict_set_str (dict, "brick", str);
                if (ret)
                        goto out;
                /* Path */
                str = (char *)words[5];
                ret = dict_set_str (dict, "path", str);
                if (ret)
                        goto out;
        } else {
                ret = dict_set_str (dict, "path", str);
                if (ret)
                        goto out;
        }

        *options = dict;

out:
        if (ret && dict)
                dict_destroy (dict);

        return ret;
}

int32_t
cli_cmd_log_locate_parse (const char **words, int wordcount, dict_t **options)
{
        dict_t  *dict = NULL;
        char    *volname = NULL;
        char    *str = NULL;
        int     ret = -1;
        char    *delimiter = NULL;

        GF_ASSERT (words);
        GF_ASSERT (options);

        GF_ASSERT ((strcmp (words[0], "volume")) == 0);
        GF_ASSERT ((strcmp (words[1], "log")) == 0);
        GF_ASSERT ((strcmp (words[2], "locate")) == 0);

        dict = dict_new ();
        if (!dict)
                goto out;

        volname = (char *)words[3];
        GF_ASSERT (volname);

        ret = dict_set_str (dict, "volname", volname);
        if (ret)
                goto out;

        if (words[4]) {
                delimiter = strchr (words[4], ':');
                if (!delimiter || delimiter == words[4]
                    || *(delimiter+1) != '/') {
                        cli_out ("wrong brick type: %s, use <HOSTNAME>:"
                                 "<export-dir-abs-path>", words[4]);
                        ret = -1;
                        goto out;
                } else {
                        cli_path_strip_trailing_slashes (delimiter + 1);
                }
                str = (char *)words[4];
                ret = dict_set_str (dict, "brick", str);
                if (ret)
                        goto out;
        }

        *options = dict;

out:
        if (ret && dict)
                dict_destroy (dict);

        return ret;
}

int32_t
cli_cmd_log_rotate_parse (const char **words, int wordcount, dict_t **options)
{
        dict_t  *dict = NULL;
        char    *volname = NULL;
        char    *str = NULL;
        int     ret = -1;
        char    *delimiter = NULL;

        GF_ASSERT (words);
        GF_ASSERT (options);

        GF_ASSERT ((strcmp (words[0], "volume")) == 0);
        GF_ASSERT ((strcmp (words[1], "log")) == 0);
        GF_ASSERT ((strcmp (words[2], "rotate")) == 0);

        dict = dict_new ();
        if (!dict)
                goto out;

        volname = (char *)words[3];
        GF_ASSERT (volname);

        ret = dict_set_str (dict, "volname", volname);
        if (ret)
                goto out;

        if (words[4]) {
                delimiter = strchr (words[4], ':');
                if (!delimiter || delimiter == words[4]
                    || *(delimiter+1) != '/') {
                        cli_out ("wrong brick type: %s, use <HOSTNAME>:"
                                 "<export-dir-abs-path>", words[4]);
                        ret = -1;
                        goto out;
                } else {
                        cli_path_strip_trailing_slashes (delimiter + 1);
                }
                str = (char *)words[4];
                ret = dict_set_str (dict, "brick", str);
                if (ret)
                        goto out;
        }

        *options = dict;

out:
        if (ret && dict)
                dict_destroy (dict);

        return ret;
}

int32_t
cli_cmd_gsync_set_parse (const char **words, int wordcount, dict_t **options)
{
        int32_t            ret     = 0;
        int32_t            config_type = 0;
        dict_t            *dict   = NULL;
        gf1_cli_gsync_set type    = GF_GSYNC_OPTION_TYPE_NONE;

        GF_ASSERT (words);
        GF_ASSERT (options);

        GF_ASSERT ((strcmp (words[0], "volume")) == 0);
        GF_ASSERT ((strcmp (words[1], "gsync")) == 0);

        dict = dict_new ();
        if (!dict)
                goto out;

        if (wordcount < 5)
                goto out;

        ret = dict_set_str (dict, "master", (char *)words[3]);
        if (ret < 0)
                goto out;

        ret = dict_set_str (dict, "slave", (char *)words[4]);
        if (ret < 0)
                goto out;

        if ((strcmp (words[2], "start")) == 0) {
                type = GF_GSYNC_OPTION_TYPE_START;

                goto set_type;
        }

        if ((strcmp (words[2], "stop")) == 0) {
                type = GF_GSYNC_OPTION_TYPE_STOP;

                goto set_type;
        }

        if ((strcmp (words[2], "configure")) == 0) {
                type = GF_GSYNC_OPTION_TYPE_CONFIGURE;

                if (strcmp (words [5], "config-set") == 0) {
                        config_type = GF_GSYNC_OPTION_TYPE_CONFIG_SET;

                        ret = dict_set_str (dict, "op_name", (char *)words[6]);
                        if (ret < 0)
                                goto out;

                        ret = dict_set_str (dict, "op_value", (char *)words[7]);
                        if (ret < 0)
                                goto out;
                }

                if ((strcmp (words [5], "config-del")) == 0) {
                        config_type = GF_GSYNC_OPTION_TYPE_CONFIG_DEL;

                        ret = dict_set_str (dict, "op_name", (char *)words[6]);
                        if (ret < 0)
                                goto out;
                }

                if ((strcmp (words [5], "config-get")) == 0) {
                        config_type = GF_GSYNC_OPTION_TYPE_CONFIG_GET;

                        ret = dict_set_str (dict, "op_name", (char *)words[6]);
                        if (ret < 0)
                                goto out;
                }

                if ((strcmp (words [5], "config-get-all")) == 0) {
                        config_type = GF_GSYNC_OPTION_TYPE_CONFIG_GET_ALL;
                }

                ret = dict_set_int32 (dict, "config_type", config_type);
                if (ret < 0)
                        goto out;
        }

set_type:
        ret = dict_set_int32 (dict, "type", type);
        if (ret < 0)
                goto out;

        *options = dict;
out:
        if (ret)
                if (dict)
                        dict_destroy (dict);

        return ret;
}

