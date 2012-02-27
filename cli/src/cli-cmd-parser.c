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

static const char *
id_sel (void *wcon)
{
        return (const char *)wcon;
}

static char *
str_getunamb (const char *tok, char **opwords)
{
        return (char *)cli_getunamb (tok, (void **)opwords, id_sel);
}

int32_t
cli_cmd_bricks_parse (const char **words, int wordcount, int brick_index,
                      char **bricks, int *brick_count)
{
        int                    ret = 0;
        char             *tmp_list = NULL;
        char    brick_list[120000] = {0,};
        char                *space = " ";
        char            *delimiter = NULL;
        char            *host_name = NULL;
        char        *free_list_ptr = NULL;
        char               *tmpptr = NULL;
        int                      j = 0;
        int         brick_list_len = 0;
        char             *tmp_host = NULL;

        GF_ASSERT (words);
        GF_ASSERT (wordcount);
        GF_ASSERT (bricks);
        GF_ASSERT (brick_index > 0);
        GF_ASSERT (brick_index < wordcount);

        strncpy (brick_list, space, strlen (space));
        brick_list_len++;
        while (brick_index < wordcount) {
                if (validate_brick_name ((char *)words[brick_index])) {
                        cli_out ("Wrong brick type: %s, use <HOSTNAME>:"
                                 "<export-dir-abs-path>", words[brick_index]);
                        ret = -1;
                        goto out;
                } else {
                        delimiter = strrchr (words[brick_index], ':');
                        ret = cli_canonicalize_path (delimiter + 1);
                        if (ret)
                                goto out;
                }

                if ((brick_list_len + strlen (words[brick_index]) + 1) > sizeof (brick_list)) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "total brick list is larger than a request "
                                "can take (brick_count %d)", *brick_count);
                        ret = -1;
                        goto out;
                }

                tmp_host = gf_strdup ((char *)words[brick_index]);
                if (!tmp_host) {
                        gf_log ("cli", GF_LOG_ERROR, "Out of memory");
                        ret = -1;
                        goto out;
                }
                get_host_name (tmp_host, &host_name);
                if (!host_name) {
                        ret = -1;
                        gf_log("cli",GF_LOG_ERROR, "Unable to allocate "
                               "memory");
                        goto out;
                }

                if (!(strcmp (host_name, "localhost") &&
                      strcmp (host_name, "127.0.0.1"))) {
                        cli_out ("Please provide a valid hostname/ip other "
                                 "than localhost or 127.0.0.1");
                        ret = -1;
                        GF_FREE (tmp_host);
                        goto out;
                }
                if (!valid_internet_address (host_name)) {
                        cli_out ("internet address '%s' does not comform to "
			         "standards", host_name);
                }
                GF_FREE (tmp_host);
                tmp_list = gf_strdup (brick_list + 1);
                if (free_list_ptr) {
                        GF_FREE (free_list_ptr);
                        free_list_ptr = NULL;
                }
                free_list_ptr = tmp_list;
                j = 0;
                while(j < *brick_count) {
                        strtok_r (tmp_list, " ", &tmpptr);
                        if (!(strcmp (tmp_list, words[brick_index]))) {
                                ret = -1;
                                cli_out ("Found duplicate"
                                         " exports %s",words[brick_index]);
                                goto out;
                       }
                       tmp_list = tmpptr;
                       j++;
                }
                strcat (brick_list, words[brick_index]);
                strcat (brick_list, " ");
                brick_list_len += (strlen (words[brick_index]) + 1);
                ++(*brick_count);
                ++brick_index;
        }

        *bricks = gf_strdup (brick_list);
        if (!*bricks)
                ret = -1;
out:
        if (free_list_ptr)
                GF_FREE (free_list_ptr);
        return ret;
}

int32_t
cli_cmd_volume_create_parse (const char **words, int wordcount, dict_t **options)
{
        dict_t  *dict = NULL;
        char    *volname = NULL;
        int     ret = -1;
        gf1_cluster_type type = GF_CLUSTER_TYPE_NONE;
        int     count = 1;
        int     sub_count = 1;
        int     brick_index = 0;
        int     i = 0;
        char    *trans_type = NULL;
        int32_t index = 0;
        char    *bricks = NULL;
        int32_t brick_count = 0;
        char    *opwords[] = { "replica", "stripe", "transport", NULL };
        char    *w = NULL;
        int      op_count = 0;
        int32_t  replica_count = 1;
        int32_t  stripe_count = 1;

        GF_ASSERT (words);
        GF_ASSERT (options);

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

        if (wordcount < 4) {
                ret = -1;
                goto out;
        }

        type = GF_CLUSTER_TYPE_NONE;
        index = 3;

        while (op_count < 3) {
                ret = -1;
                w = str_getunamb (words[index], opwords);
                if (!w) {
                        break;
                } else if ((strcmp (w, "replica")) == 0) {
                        switch (type) {
                        case GF_CLUSTER_TYPE_STRIPE_REPLICATE:
                        case GF_CLUSTER_TYPE_REPLICATE:
                                cli_out ("replica option given twice");
                                goto out;
                        case GF_CLUSTER_TYPE_NONE:
                                type = GF_CLUSTER_TYPE_REPLICATE;
                                break;
                        case GF_CLUSTER_TYPE_STRIPE:
                                type = GF_CLUSTER_TYPE_STRIPE_REPLICATE;
                                break;
                        }

                        if (wordcount < (index+2)) {
                                ret = -1;
                                goto out;
                        }
                        replica_count = strtol (words[index+1], NULL, 0);
                        if (replica_count < 2) {
                                cli_out ("replica count should be greater than 1");
                                ret = -1;
                                goto out;
                        }
                        ret = dict_set_int32 (dict, "replica-count", replica_count);
                        if (ret)
                                goto out;

                        index += 2;

                } else if ((strcmp (w, "stripe")) == 0) {
                        switch (type) {
                        case GF_CLUSTER_TYPE_STRIPE_REPLICATE:
                        case GF_CLUSTER_TYPE_STRIPE:
                                cli_out ("stripe option given twice");
                                goto out;
                        case GF_CLUSTER_TYPE_NONE:
                                type = GF_CLUSTER_TYPE_STRIPE;
                                break;
                        case GF_CLUSTER_TYPE_REPLICATE:
                                type = GF_CLUSTER_TYPE_STRIPE_REPLICATE;
                                break;
                        }
                        if (wordcount < (index + 2)) {
                                ret = -1;
                                goto out;
                        }
                        stripe_count = strtol (words[index+1], NULL, 0);
                        if (stripe_count < 2) {
                                cli_out ("stripe count should be greater than 1");
                                ret = -1;
                                goto out;
                        }
                        ret = dict_set_int32 (dict, "stripe-count", stripe_count);
                        if (ret)
                                goto out;

                        index += 2;

                } else if ((strcmp (w, "transport")) == 0) {
                        if (trans_type) {
                                cli_out ("'transport' option given more"
                                         " than one time");
                                goto out;
                        }
                        if ((strcasecmp (words[index+1], "tcp") == 0)) {
                                trans_type = gf_strdup ("tcp");
                        } else if ((strcasecmp (words[index+1], "rdma") == 0)) {
                                trans_type = gf_strdup ("rdma");
                        } else if ((strcasecmp (words[index+1], "tcp,rdma") == 0) ||
                                   (strcasecmp (words[index+1], "rdma,tcp") == 0)) {
                                trans_type = gf_strdup ("tcp,rdma");
                        } else {
                                gf_log ("", GF_LOG_ERROR, "incorrect transport"
                                        " protocol specified");
                                ret = -1;
                                goto out;
                        }
                        index += 2;
                } else {
                        GF_ASSERT (!"opword mismatch");
                        ret = -1;
                        goto out;
                }
                op_count++;
        }

        if (!trans_type)
                trans_type = gf_strdup ("tcp");

        sub_count = stripe_count * replica_count;

        /* reset the count value now */
        count = 1;

        if (index >= wordcount) {
                ret = -1;
                goto out;
        }

        brick_index = index;

        ret = cli_cmd_bricks_parse (words, wordcount, brick_index, &bricks,
                                    &brick_count);
        if (ret)
                goto out;

        /* If brick-count is not valid when replica or stripe is
           given, exit here */
        if (!brick_count) {
                cli_out ("No bricks specified");
                ret = -1;
                goto out;
        }

        if (brick_count % sub_count) {
                if (type == GF_CLUSTER_TYPE_STRIPE)
                        cli_out ("number of bricks is not a multiple of "
                                 "stripe count");
                else if (type == GF_CLUSTER_TYPE_REPLICATE)
                        cli_out ("number of bricks is not a multiple of "
                                 "replica count");
                else
                        cli_out ("number of bricks given doesn't match "
                                 "required count");

                ret = -1;
                goto out;
        }

        /* Everything if parsed fine. start setting info in dict */
        ret = dict_set_str (dict, "volname", volname);
        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "type", type);
        if (ret)
                goto out;

        ret = dict_set_dynstr (dict, "transport", trans_type);
        if (ret)
                goto out;


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

        dict = dict_new ();

        if (!dict)
                goto out;

        if (wordcount < 3)
                goto out;

        if (wordcount > 5)
                goto out;

        volname = (char *)words[2];

        if (!volname) {
                ret = -1;
                goto out;
        }

        ret = dict_set_str (dict, "volname", volname);
        if (ret)
                goto out;

        if (wordcount == 3) {
                ret = dict_set_str (dict, "key", "all");
                if (ret)
                        goto out;
        }

        if (wordcount >= 4) {
                if (!strcmp ("force", (char*)words[3])) {
                        ret = dict_set_int32 (dict, "force", 1);
                        if (ret)
                                goto out;
                        ret = dict_set_str (dict, "key", "all");
                        if (ret)
                                goto out;
                } else {
                        ret = dict_set_str (dict, "key", (char *)words[3]);
                        if (ret)
                                goto out;
                }
        }

        if (wordcount == 5) {
                if (strcmp ("force", (char*)words[4])) {
                        ret = -1;
                        goto out;
                } else {
                        ret = dict_set_int32 (dict, "force", 1);
                        if (ret)
                                goto out;
                }
        }

        *options = dict;

out:
        if (ret && dict) {
                dict_destroy (dict);
        }

                return ret;
}

int32_t
cli_cmd_quota_parse (const char **words, int wordcount, dict_t **options)
{
        dict_t          *dict    = NULL;
        char            *volname = NULL;
        int              ret     = -1;
        int              i       = 0;
        char             key[20] = {0, };
        uint64_t         value   = 0;
        gf_quota_type    type    = GF_QUOTA_OPTION_TYPE_NONE;
        char           *opwords[] = { "enable", "disable", "limit-usage",
                                      "remove", "list", "version", NULL };
        char            *w       = NULL;

        GF_ASSERT (words);
        GF_ASSERT (options);

        dict = dict_new ();
        if (!dict)
                goto out;

        if (wordcount < 4)
                goto out;

        volname = (char *)words[2];
        if (!volname) {
                ret = -1;
                goto out;
        }

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
        if (ret < 0)
                goto out;

        w = str_getunamb (words[3], opwords);
        if (!w) {
                ret = - 1;
                goto out;
        }

        if ((strcmp (w, "enable")) == 0 && wordcount == 4) {
                type = GF_QUOTA_OPTION_TYPE_ENABLE;
                ret = 0;
                goto set_type;
        }

        if (strcmp (w, "disable") == 0 && wordcount == 4) {
                type = GF_QUOTA_OPTION_TYPE_DISABLE;
                ret = 0;
                goto set_type;
        }

        if (strcmp (w, "limit-usage") == 0) {
                if (wordcount != 6) {
                        ret = -1;
                        goto out;
                }

                type = GF_QUOTA_OPTION_TYPE_LIMIT_USAGE;

                if (words[4][0] != '/') {
                        cli_out ("Please enter absolute path");

                        return -2;
                }
                ret = dict_set_str (dict, "path", (char *) words[4]);
                if (ret)
                        goto out;

                if (!words[5]) {
                        cli_out ("Please enter the limit value to be set");

                        return -2;
                }

                ret = gf_string2bytesize (words[5], &value);
                if (ret != 0) {
                        cli_out ("Please enter a correct value");
                        return -1;
                }

                ret = dict_set_str (dict, "limit", (char *) words[5]);
                if (ret < 0)
                        goto out;

                goto set_type;
        }
        if (strcmp (w, "remove") == 0) {
                if (wordcount != 5) {
                        ret = -1;
                        goto out;
                }

                type = GF_QUOTA_OPTION_TYPE_REMOVE;

                if (words[4][0] != '/') {
                        cli_out ("Please enter absolute path");

                        return -2;
                }

                ret = dict_set_str (dict, "path", (char *) words[4]);
                if (ret < 0)
                        goto out;
                goto set_type;
        }

        if (strcmp (w, "list") == 0) {
                if (wordcount < 4) {
                        ret = -1;
                        goto out;
                }

                type = GF_QUOTA_OPTION_TYPE_LIST;

                i = 4;
                while (i < wordcount) {
                        snprintf (key, 20, "path%d", i-4);

                        ret = dict_set_str (dict, key, (char *) words [i++]);
                        if (ret < 0)
                                goto out;
                }

                ret = dict_set_int32 (dict, "count", i - 4);
                if (ret < 0)
                        goto out;

                goto set_type;
        }

        if (strcmp (w, "version") == 0) {
                type = GF_QUOTA_OPTION_TYPE_VERSION;
        } else {
                GF_ASSERT (!"opword mismatch");
        }

set_type:
        ret = dict_set_int32 (dict, "type", type);
        if (ret < 0)
                goto out;

        *options = dict;
out:
        if (ret < 0) {
                if (dict)
                        dict_destroy (dict);
        }

        return ret;
}

int32_t
cli_cmd_valid_ip_list (char *iplist)
{
        int     ret = 0;
        char    *duplist = NULL;
        char    *addr = NULL;
        char    *saveptr = NULL;

        GF_ASSERT (iplist);
        duplist = gf_strdup (iplist);

        if (!duplist) {
                ret = -1;
                goto out;
        }

        addr = strtok_r (duplist, ",", &saveptr);
        if (!addr) {
                ret = -1;
                goto out;
        }
        while (addr) {
                if (!valid_internet_address (addr) &&
                    !valid_wildcard_internet_address (addr)) {
                        cli_out ("Invalid ip or wildcard : %s", addr);
                        ret= -1;
                        goto out;
                }
                addr = strtok_r (NULL, ",", &saveptr);
        }
out:
        if (duplist)
                GF_FREE (duplist);
        gf_log ("cli", GF_LOG_INFO, "Returning %d", ret);
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

        if (wordcount == 3) {
                if (!strcmp (volname, "help")) {
                        ret = dict_set_str (dict, "help", volname);
                        if (ret)
                                goto out;
                } else if (!strcmp (volname, "help-xml")) {
                        ret = dict_set_str (dict, "help-xml", volname);
                        if (ret)
                                goto out;
                } else {
                        ret = -1;
                        goto out;
                }
        }


        for (i = 3; i < wordcount; i+=2) {

		key = (char *) words[i];
		value = (char *) words[i+1];

		if ( !key || !value) {
			ret = -1;
			goto out;
	        }

                count++;
                if (!strncmp ("auth.allow", key, sizeof (key)) ||
                    !strncmp ("auth.reject", key, sizeof (key))) {
                        ret = cli_cmd_valid_ip_list (value);
                        if (ret) {
                                gf_log ("cli", GF_LOG_ERROR,
                                        "invalid ips given");
                                goto out;
                        }
                }
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
        int     ret = -1;
        int     brick_count = 0, brick_index = 0;
        char    *bricks = NULL;
        char    *opwords_cl[] = { "replica", "stripe", NULL };
        gf1_cluster_type type = GF_CLUSTER_TYPE_NONE;
        int     count = 1;
        char    *w = NULL;
        int     index;

        GF_ASSERT (words);
        GF_ASSERT (options);

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
        if (wordcount < 6) {
                /* seems no options are given, go directly to the parse_brick */
                brick_index = 3;
                type = GF_CLUSTER_TYPE_NONE;
                goto parse_bricks;
        }

        w = str_getunamb (words[3], opwords_cl);
        if (!w) {
                type = GF_CLUSTER_TYPE_NONE;
                index = 3;
        } else if ((strcmp (w, "replica")) == 0) {
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
                index = 5;
        } else if ((strcmp (w, "stripe")) == 0) {
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
                index = 5;
        } else {
                GF_ASSERT (!"opword mismatch");
                ret = -1;
                goto out;
        }

        brick_index = index;

parse_bricks:
        ret = cli_cmd_bricks_parse (words, wordcount, brick_index, &bricks,
                                    &brick_count);
        if (ret)
                goto out;

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
                                   dict_t **options, int *question)
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
        char    *type_opword[] = { "replica", NULL };
        char    *opwords[] = { "start", "commit", "pause", "abort", "status",
                               "force", NULL };
        char    *w = NULL;
        int32_t  command = GF_OP_CMD_NONE;
        long     count = 0;

        GF_ASSERT (words);
        GF_ASSERT (options);

        if (wordcount < 4)
                goto out;

        dict = dict_new ();
        if (!dict)
                goto out;

        volname = (char *)words[2];

        GF_ASSERT (volname);

        ret = dict_set_str (dict, "volname", volname);
        if (ret)
                goto out;

        brick_index = 3;
        w = str_getunamb (words[3], type_opword);
        if (w && !strcmp ("replica", w)) {
                if (wordcount < 5) {
                        ret = -1;
                        goto out;
                }
                count = strtol (words[4], NULL, 0);
                if (count < 1) {
                        cli_out ("replica count should be greater than 0 in "
                                 "case of remove-brick");
                        ret = -1;
                        goto out;
                }

                ret = dict_set_int32 (dict, "replica-count", count);
                if (ret)
                        goto out;
                brick_index = 5;
        } else if (w) {
                GF_ASSERT (!"opword mismatch");
        }

        w = str_getunamb (words[wordcount - 1], opwords);
        if (!w) {
                /* Should be default 'force' */
                command = GF_OP_CMD_COMMIT_FORCE;
                if (question)
                        *question = 1;
        } else {
                /* handled this option */
                wordcount--;
                if (!strcmp ("start", w)) {
                        command = GF_OP_CMD_START;
                } else if (!strcmp ("commit", w)) {
                        command = GF_OP_CMD_COMMIT;
                        if (question)
                                *question = 1;
                } else if (!strcmp ("pause", w)) {
                        command = GF_OP_CMD_PAUSE;
                } else if (!strcmp ("abort", w)) {
                        command = GF_OP_CMD_ABORT;
                } else if (!strcmp ("status", w)) {
                        command = GF_OP_CMD_STATUS;
                } else if (!strcmp ("force", w)) {
                        command = GF_OP_CMD_COMMIT_FORCE;
                        if (question)
                                *question = 1;
                } else {
                        GF_ASSERT (!"opword mismatch");
                        ret = -1;
                        goto out;
                }
        }

        if (wordcount < 4) {
                ret = -1;
                goto out;
        }

        ret = dict_set_int32 (dict, "command", command);
        if (ret)
                gf_log ("cli", GF_LOG_INFO, "failed to set 'command' %d",
                        command);


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
                if (validate_brick_name ((char *)words[brick_index])) {
                        cli_out ("wrong brick type: %s, use <HOSTNAME>:"
                                 "<export-dir-abs-path>", words[brick_index]);
                        ret = -1;
                        goto out;
                } else {
                        delimiter = strrchr(words[brick_index], ':');
                        ret = cli_canonicalize_path (delimiter + 1);
                        if (ret)
                                goto out;
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
        int     op_index = 0;
        char    *delimiter = NULL;
        gf1_cli_replace_op replace_op = GF_REPLACE_OP_NONE;
        char    *opwords[] = { "start", "commit", "pause", "abort", "status",
                                NULL };
        char    *w = NULL;

        GF_ASSERT (words);
        GF_ASSERT (options);

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

        if (validate_brick_name ((char *)words[3])) {
                cli_out ("wrong brick type: %s, use "
                         "<HOSTNAME>:<export-dir-abs-path>", words[3]);
                ret = -1;
                goto out;
        } else {
                delimiter = strrchr ((char *)words[3], ':');
                ret = cli_canonicalize_path (delimiter + 1);
                if (ret)
                        goto out;
        }
        ret = dict_set_str (dict, "src-brick", (char *)words[3]);

        if (ret)
                goto out;

        if (wordcount < 5) {
                ret = -1;
                goto out;
        }

        if (validate_brick_name ((char *)words[4])) {
                cli_out ("wrong brick type: %s, use "
                         "<HOSTNAME>:<export-dir-abs-path>", words[4]);
                ret = -1;
                goto out;
        } else {
                delimiter = strrchr ((char *)words[4], ':');
                ret = cli_canonicalize_path (delimiter + 1);
                if (ret)
                        goto out;
        }


        ret = dict_set_str (dict, "dst-brick", (char *)words[4]);

        if (ret)
                goto out;

        op_index = 5;
        if ((wordcount < (op_index + 1))) {
                ret = -1;
                goto out;
        }

        w = str_getunamb (words[op_index], opwords);

        if (!w) {
        } else if (!strcmp ("start", w)) {
                replace_op = GF_REPLACE_OP_START;
        } else if (!strcmp ("commit", w)) {
                replace_op = GF_REPLACE_OP_COMMIT;
        } else if (!strcmp ("pause", w)) {
                replace_op = GF_REPLACE_OP_PAUSE;
        } else if (!strcmp ("abort", w)) {
                replace_op = GF_REPLACE_OP_ABORT;
        } else if (!strcmp ("status", w)) {
                replace_op = GF_REPLACE_OP_STATUS;
        } else
                GF_ASSERT (!"opword mismatch");

        /* commit force option */

        op_index = 6;

        if (wordcount > (op_index + 1)) {
                ret = -1;
                goto out;
        }

        if (wordcount == (op_index + 1)) {
                if (replace_op != GF_REPLACE_OP_COMMIT) {
                        ret = -1;
                        goto out;
                }
                if (!strcmp ("force", words[op_index])) {
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
                gf_log ("cli", GF_LOG_ERROR, "Unable to parse replace-brick CLI");
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
                        ret = cli_canonicalize_path (delimiter + 1);
                        if (ret)
                                goto out;
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
cli_cmd_log_level_parse (const char **words, int worcount, dict_t **options)
{
        dict_t *dict            = NULL;
        int     ret             = -1;

        GF_ASSERT (words);
        GF_ASSERT (options);

        /*
         * loglevel command format:
         *  > volume log level <VOL> <XLATOR[*]> <LOGLEVEL>
         *  > volume log level colon-o posix WARNING
         *  > volume log level colon-o replicate* DEBUG
         *  > volume log level coon-o * TRACE
         */

        GF_ASSERT ((strncmp(words[0], "volume", 6) == 0));
        GF_ASSERT ((strncmp(words[1], "log", 3) == 0));
        GF_ASSERT ((strncmp(words[2], "level", 5) == 0));

        ret = glusterd_check_log_level(words[5]);
        if (ret == -1) {
                cli_out("Invalid log level [%s] specified", words[5]);
                cli_out("Valid values for loglevel: (DEBUG|WARNING|ERROR"
                        "|CRITICAL|NONE|TRACE)");
                goto out;
        }

        dict = dict_new ();
        if (!dict)
                goto out;

        GF_ASSERT(words[3]);
        GF_ASSERT(words[4]);

        ret = dict_set_str (dict, "volname", (char *)words[3]);
        if (ret)
                goto out;

        ret = dict_set_str (dict, "xlator", (char *)words[4]);
        if (ret)
                goto out;

        ret = dict_set_str (dict, "loglevel", (char *)words[5]);
        if (ret)
                goto out;

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
                        ret = cli_canonicalize_path (delimiter + 1);
                        if (ret)
                                goto out;
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
                        ret = cli_canonicalize_path (delimiter + 1);
                        if (ret)
                                goto out;
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

static gf_boolean_t
gsyncd_url_check (const char *w)
{
        return !!strpbrk (w, ":/");
}

static gf_boolean_t
gsyncd_glob_check (const char *w)
{
        return !!strpbrk (w, "*?[");
}

int32_t
cli_cmd_gsync_set_parse (const char **words, int wordcount, dict_t **options)
{
        int32_t            ret     = -1;
        dict_t             *dict   = NULL;
        gf1_cli_gsync_set  type    = GF_GSYNC_OPTION_TYPE_NONE;
        char               *append_str = NULL;
        size_t             append_len = 0;
        char               *subop = NULL;
        int                i       = 0;
        unsigned           masteri = 0;
        unsigned           slavei  = 0;
        unsigned           glob    = 0;
        unsigned           cmdi    = 0;
        char               *opwords[] = { "status", "start", "stop", "config",
                                          "log-rotate", NULL };
        char               *w = NULL;

        GF_ASSERT (words);
        GF_ASSERT (options);

        dict = dict_new ();
        if (!dict)
                goto out;

        /* new syntax:
         *
         * volume geo-replication [$m [$s]] status
         * volume geo-replication [$m] $s config [[!]$opt [$val]]
         * volume geo-replication $m $s start|stop
         * volume geo-replication $m [$s] log-rotate
         */

        if (wordcount < 3)
                goto out;

        for (i = 2; i <= 3 && i < wordcount - 1; i++) {
                if (gsyncd_glob_check (words[i]))
                        glob = i;
                if (gsyncd_url_check (words[i])) {
                        slavei = i;
                        break;
                }
        }

        if (glob && !slavei)
                /* glob is allowed only for config, thus it implies there is a
                 * slave argument; but that might have not been recognized on
                 * the first scan as it's url characteristics has been covered
                 * by the glob syntax.
                 *
                 * In this case, the slave is perforce the last glob-word -- the
                 * upcoming one is neither glob, nor url, so it's definitely not
                 * the slave.
                 */
                slavei = glob;
        if (slavei) {
                cmdi = slavei + 1;
                if (slavei == 3)
                        masteri = 2;
        } else if (i <= 3) {
                /* no $s, can only be status cmd
                 * (with either a single $m before it or nothing)
                 * -- these conditions imply that i <= 3 after
                 * the iteration and that i is the successor of
                 * the (0 or 1 length) sequence of $m-s.
                 */
                cmdi = i;
                if (i == 3)
                        masteri = 2;
        } else
                goto out;

        /* now check if input really complies syntax
         * (in a somewhat redundant way, in favor
         * transparent soundness)
         */

        if (masteri && gsyncd_url_check (words[masteri]))
                goto out;
        if (slavei && !glob && !gsyncd_url_check (words[slavei]))
                goto out;

        w = str_getunamb (words[cmdi], opwords);
        if (!w)
                goto out;

        if (strcmp (w, "status") == 0) {
                type = GF_GSYNC_OPTION_TYPE_STATUS;

                if (slavei && !masteri)
                        goto out;
        } else if (strcmp (w, "config") == 0) {
                type = GF_GSYNC_OPTION_TYPE_CONFIG;

                if (!slavei)
                        goto out;
        } else if (strcmp (w, "start") == 0) {
                type = GF_GSYNC_OPTION_TYPE_START;

                if (!masteri || !slavei)
                        goto out;
        } else if (strcmp (w, "stop") == 0) {
                type = GF_GSYNC_OPTION_TYPE_STOP;

                if (!masteri || !slavei)
                        goto out;
        } else if (strcmp(w, "log-rotate") == 0) {
                type = GF_GSYNC_OPTION_TYPE_ROTATE;

                if (slavei && !masteri)
                        goto out;
        } else
                GF_ASSERT (!"opword mismatch");

        if (type != GF_GSYNC_OPTION_TYPE_CONFIG &&
            (cmdi < wordcount - 1 || glob))
                goto out;

        /* If got so far, input is valid, assemble the message */

        ret = 0;

        if (masteri)
                ret = dict_set_str (dict, "master", (char *)words[masteri]);
        if (!ret && slavei)
                ret = dict_set_str (dict, "slave", (char *)words[slavei]);
        if (!ret)
                ret = dict_set_int32 (dict, "type", type);
        if (!ret && type == GF_GSYNC_OPTION_TYPE_CONFIG) {
                switch ((wordcount - 1) - cmdi) {
                case 0:
                        subop = gf_strdup ("get-all");
                        break;
                case 1:
                        if (words[cmdi + 1][0] == '!') {
                                (words[cmdi + 1])++;
                                if (gf_asprintf (&subop, "del%s", glob ? "-glob" : "") == -1)
                                        subop = NULL;
                        } else
                                subop = gf_strdup ("get");

                        ret = dict_set_str (dict, "op_name", ((char *)words[cmdi + 1]));
                        if (ret < 0)
                                goto out;
                        break;
                default:
                        if (gf_asprintf (&subop, "set%s", glob ? "-glob" : "") == -1)
                                subop = NULL;

                        ret = dict_set_str (dict, "op_name", ((char *)words[cmdi + 1]));
                        if (ret < 0)
                                goto out;

                        /* join the varargs by spaces to get the op_value */

                        for (i = cmdi + 2; i < wordcount; i++)
                                append_len += (strlen (words[i]) + 1);
                        /* trailing strcat will add two bytes, make space for that */
                        append_len++;

                        append_str = GF_CALLOC (1, append_len, cli_mt_append_str);
                        if (!append_str) {
                                ret = -1;
                                goto out;
                        }

                        for (i = cmdi + 2; i < wordcount; i++) {
                                strcat (append_str, words[i]);
                                strcat (append_str, " ");
                        }
                        append_str[append_len - 2] = '\0';

                        ret = dict_set_dynstr (dict, "op_value", append_str);
                }

                if (!subop || dict_set_dynstr (dict, "subop", subop) != 0)
                        ret = -1;
        }

out:
        if (ret) {
                if (dict)
                        dict_destroy (dict);
                if (append_str)
                        GF_FREE (append_str);
        } else
                *options = dict;

        return ret;
}

int32_t
cli_cmd_volume_profile_parse (const char **words, int wordcount,
                              dict_t **options)
{
        dict_t    *dict       = NULL;
        char      *volname    = NULL;
        int       ret         = -1;
        gf1_cli_stats_op op = GF_CLI_STATS_NONE;
        char      *opwords[] = { "start", "stop", "info", NULL };
        char      *w = NULL;

        GF_ASSERT (words);
        GF_ASSERT (options);

        dict = dict_new ();
        if (!dict)
                goto out;

        if (wordcount < 4 || wordcount >5)
                goto out;

        volname = (char *)words[2];

        ret = dict_set_str (dict, "volname", volname);
        if (ret)
                goto out;

        w = str_getunamb (words[3], opwords);
        if (!w) {
                ret = -1;
                goto out;
        }
        if (strcmp (w, "start") == 0) {
                op = GF_CLI_STATS_START;
        } else if (strcmp (w, "stop") == 0) {
                op = GF_CLI_STATS_STOP;
        } else if (strcmp (w, "info") == 0) {
                op = GF_CLI_STATS_INFO;
        } else
                GF_ASSERT (!"opword mismatch");

        ret = dict_set_int32 (dict, "op", (int32_t)op);
        if (ret)
                goto out;

        if (wordcount == 5) {
                if (!strcmp (words[4], "nfs")) {
                        ret = dict_set_int32 (dict, "nfs", _gf_true);
                        if (ret)
                                goto out;
                }
        }

        *options = dict;
out:
        if (ret && dict)
                dict_destroy (dict);
        return ret;
}

int32_t
cli_cmd_volume_top_parse (const char **words, int wordcount,
                              dict_t **options)
{
        dict_t  *dict           = NULL;
        char    *volname        = NULL;
        char    *value          = NULL;
        char    *key            = NULL;
        int      ret            = -1;
        gf1_cli_stats_op op = GF_CLI_STATS_NONE;
        gf1_cli_top_op    top_op = GF_CLI_TOP_NONE;
        int32_t  list_cnt       = -1;
        int      index          = 0;
        int      perf           = 0;
        uint32_t  blk_size      = 0;
        uint32_t  count         = 0;
        gf_boolean_t nfs        = _gf_false;
        char    *delimiter      = NULL;
        char    *opwords[]      = { "open", "read", "write", "opendir",
                                    "readdir", "read-perf", "write-perf",
                                    NULL };
        char    *w = NULL;

        GF_ASSERT (words);
        GF_ASSERT (options);

        dict = dict_new ();
        if (!dict)
                goto out;

        if (wordcount < 4)
                goto out;

        volname = (char *)words[2];

        ret = dict_set_str (dict, "volname", volname);
        if (ret)
                goto out;

        op = GF_CLI_STATS_TOP;
        ret = dict_set_int32 (dict, "op", (int32_t)op);
        if (ret)
                goto out;

        w = str_getunamb (words[3], opwords);
        if (!w) {
                ret = -1;
                goto out;
        }
        if (strcmp (w, "open") == 0) {
                top_op = GF_CLI_TOP_OPEN;
        } else if (strcmp (w, "read") == 0) {
                top_op = GF_CLI_TOP_READ;
        } else if (strcmp (w, "write") == 0) {
                top_op = GF_CLI_TOP_WRITE;
        } else if (strcmp (w, "opendir") == 0) {
                top_op = GF_CLI_TOP_OPENDIR;
        } else if (strcmp (w, "readdir") == 0) {
                top_op = GF_CLI_TOP_READDIR;
        } else if (strcmp (w, "read-perf") == 0) {
                top_op = GF_CLI_TOP_READ_PERF;
                perf = 1;
        } else if (strcmp (w, "write-perf") == 0) {
                top_op = GF_CLI_TOP_WRITE_PERF;
                perf = 1;
        } else
                GF_ASSERT (!"opword mismatch");
        ret = dict_set_int32 (dict, "top-op", (int32_t)top_op);
        if (ret)
                goto out;

        if ((wordcount > 4) && !strcmp (words[4], "nfs")) {
                nfs = _gf_true;
                ret = dict_set_int32 (dict, "nfs", nfs);
                if (ret)
                        goto out;
                index = 5;
        } else {
                index = 4;
        }

        for (; index < wordcount; index+=2) {

                key = (char *) words[index];
                value = (char *) words[index+1];

                if ( key && !value ) {
                        ret = -1;
                        goto out;
                }
                if (!strcmp (key, "brick")) {
                        delimiter = strchr (value, ':');
                        if (!delimiter || delimiter == value
                            || *(delimiter+1) != '/') {
                                cli_out ("wrong brick type: %s, use <HOSTNAME>:"
                                         "<export-dir-abs-path>", value);
                                ret = -1;
                                goto out;
                        } else {
                                ret = cli_canonicalize_path (delimiter + 1);
                                if (ret)
                                        goto out;
                        }
                        ret = dict_set_str (dict, "brick", value);

                } else if (!strcmp (key, "list-cnt")) {
                        ret = gf_is_str_int (value);
                        if (!ret)
                                list_cnt = atoi (value);
                        if (ret || (list_cnt < 0) || (list_cnt > 100)) {
                                cli_out ("list-cnt should be between 0 to 100");
                                ret = -1;
                                goto out;
                        }
                } else if (perf && !nfs && !strcmp (key, "bs")) {
                        ret = gf_is_str_int (value);
                        if (!ret)
                                blk_size = atoi (value);
                        if (ret || (blk_size <= 0)) {
                                if (blk_size < 0)
                                        cli_out ("block size is an invalid number");
                                else
                                        cli_out ("block size should be an integer "
                                         "greater than zero");
                                ret = -1;
                                goto out;
                        }
                        ret = dict_set_uint32 (dict, "blk-size", blk_size);
                } else if (perf && !nfs && !strcmp (key, "count")) {
                        ret = gf_is_str_int (value);
                        if (!ret)
                                count = atoi(value);
                        if (ret || (count <= 0)) {
                                if (count < 0)
                                        cli_out ("count is an invalid number");
                                else
                                        cli_out ("count should be an integer "
                                                 "greater than zero");

                                ret = -1;
                                goto out;
                        }
                        ret = dict_set_uint32 (dict, "blk-cnt", count);
                } else {
                        ret = -1;
                        goto out;
                }
                if (ret) {
                        gf_log ("", GF_LOG_WARNING, "Dict set failed for "
                                "key %s", key);
                        goto out;
                }
        }
        if (list_cnt == -1)
                list_cnt = 100;
        ret = dict_set_int32 (dict, "list-cnt", list_cnt);
        if (ret) {
                gf_log ("", GF_LOG_WARNING, "Dict set failed for list_cnt");
                goto out;
        }

        if ((blk_size > 0) ^ (count > 0)) {
                ret = -1;
                goto out;
        }
        *options = dict;
out:
        if (ret && dict)
                dict_destroy (dict);
        return ret;
}

uint32_t
cli_cmd_get_statusop (const char *arg)
{
        int        i         = 0;
        uint32_t   ret       = GF_CLI_STATUS_NONE;
        char      *w         = NULL;
        char      *opwords[] = {"detail", "mem", "clients", "fd",
                                "inode", "callpool", NULL};
        struct {
                char      *opname;
                uint32_t   opcode;
        } optable[] = {
                { "detail",   GF_CLI_STATUS_DETAIL   },
                { "mem",      GF_CLI_STATUS_MEM      },
                { "clients",  GF_CLI_STATUS_CLIENTS  },
                { "fd",       GF_CLI_STATUS_FD       },
                { "inode",    GF_CLI_STATUS_INODE    },
                { "callpool", GF_CLI_STATUS_CALLPOOL },
                { NULL }
        };

        w = str_getunamb (arg, opwords);
        if (!w) {
                gf_log ("cli", GF_LOG_DEBUG,
                        "Not a status op  %s", arg);
                goto out;
        }

        for (i = 0; optable[i].opname; i++) {
                if (!strcmp (w, optable[i].opname)) {
                        ret = optable[i].opcode;
                        break;
                }
        }

 out:
        return ret;
}

int
cli_cmd_volume_status_parse (const char **words, int wordcount,
                             dict_t **options)
{
        dict_t    *dict            = NULL;
        int        ret             = -1;
        uint32_t   cmd             = 0;

        GF_ASSERT (options);

        dict = dict_new ();
        if (!dict)
                goto out;

        switch (wordcount) {

        case 2:
                cmd = GF_CLI_STATUS_ALL;
                ret = 0;
                break;

        case 3:
                if (!strcmp (words[2], "all")) {
                        cmd = GF_CLI_STATUS_ALL;
                        ret = 0;

                } else {
                        cmd = GF_CLI_STATUS_VOL;
                        ret = dict_set_str (dict, "volname", (char *)words[2]);
                }

                break;

        case 4:
                cmd = cli_cmd_get_statusop (words[3]);

                if (!strcmp (words[2], "all")) {
                        if (cmd == GF_CLI_STATUS_NONE) {
                                cli_out ("%s is not a valid status option",
                                         words[3]);
                                ret = -1;
                                goto out;
                        }
                        cmd |= GF_CLI_STATUS_ALL;
                        ret  = 0;

                } else {
                        ret = dict_set_str (dict, "volname",
                                            (char *)words[2]);
                        if (ret)
                                goto out;

                        if (cmd == GF_CLI_STATUS_NONE) {
                                if (!strcmp (words[3], "nfs")) {
                                        cmd |= GF_CLI_STATUS_NFS;
                                } else {
                                        cmd = GF_CLI_STATUS_BRICK;
                                        ret = dict_set_str (dict, "brick",
                                                            (char *)words[3]);
                                }

                        } else {
                                cmd |= GF_CLI_STATUS_VOL;
                                ret  = 0;
                        }
                }

                break;

        case 5:
                if (!strcmp (words[2], "all")) {
                        cli_out ("Cannot specify brick/nfs for \"all\"");
                        ret = -1;
                        goto out;
                }

                cmd = cli_cmd_get_statusop (words[4]);
                if (cmd == GF_CLI_STATUS_NONE) {
                        cli_out ("%s is not a valid status option",
                                 words[4]);
                        ret = -1;
                        goto out;
                }


                ret = dict_set_str (dict, "volname", (char *)words[2]);
                if (ret)
                        goto out;

                if (!strcmp (words[3], "nfs")) {
                        if (cmd == GF_CLI_STATUS_FD) {
                                cli_out ("FD status not available for NFS");
                                ret = -1;
                                goto out;
                        }
                        cmd |= GF_CLI_STATUS_NFS;
                } else {
                        cmd |= GF_CLI_STATUS_BRICK;
                        ret = dict_set_str (dict, "brick", (char *)words[3]);
                }
                break;

        default:
                goto out;
        }

        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "cmd", cmd);
        if (ret)
                goto out;

        *options = dict;

 out:
        if (ret && dict)
                dict_destroy (dict);

        return ret;
}

gf_boolean_t
cli_cmd_validate_dumpoption (const char *arg, char **option)
{
        char    *opwords[] = {"all", "nfs", "mem", "iobuf", "callpool", "priv",
                              "fd", "inode", "history", NULL};
        char    *w = NULL;

        w = str_getunamb (arg, opwords);
        if (!w) {
                gf_log ("cli", GF_LOG_DEBUG, "Unknown statedump option  %s",
                        arg);
                return _gf_false;
        }
        *option = w;
        return _gf_true;
}

int
cli_cmd_volume_statedump_options_parse (const char **words, int wordcount,
                                        dict_t **options)
{
        int     ret = 0;
        int     i = 0;
        dict_t  *dict = NULL;
        int     option_cnt = 0;
        char    *option = NULL;
        char    option_str[100] = {0,};

        for (i = 3; i < wordcount; i++, option_cnt++) {
                if (!cli_cmd_validate_dumpoption (words[i], &option)) {
                        ret = -1;
                        goto out;
                }
                strncat (option_str, option, strlen (option));
                strncat (option_str, " ", 1);
        }

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_dynstr (dict, "options", gf_strdup (option_str));
        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "option_cnt", option_cnt);
        if (ret)
                goto out;

        *options = dict;
out:
        if (ret && dict)
                dict_destroy (dict);
        if (ret)
                gf_log ("cli", GF_LOG_ERROR, "Error parsing dumpoptions");
        return ret;
}

int
cli_cmd_volume_clrlks_opts_parse (const char **words, int wordcount,
                                  dict_t **options)
{
        int     ret = -1;
        int     i = 0;
        dict_t  *dict = NULL;
        char    *kind_opts[4] = {"blocked", "granted", "all", NULL};
        char    *types[4] = {"inode", "entry", "posix", NULL};
        char    *free_ptr = NULL;

        dict = dict_new ();
        if (!dict)
                goto out;

        if (strcmp (words[4], "kind"))
                goto out;

        for (i = 0; kind_opts[i]; i++) {
               if (!strcmp (words[5], kind_opts[i])) {
                       free_ptr = gf_strdup (words[5]);
                       ret = dict_set_dynstr (dict, "kind", free_ptr);
                       if (ret)
                               goto out;
                       free_ptr = NULL;
                       break;
               }
        }
        if (i == 3)
                goto out;

        ret = -1;
        for (i = 0; types[i]; i++) {
               if (!strcmp (words[6], types[i])) {
                       free_ptr = gf_strdup (words[6]);
                       ret = dict_set_dynstr (dict, "type", free_ptr);
                       if (ret)
                               goto out;
                       free_ptr = NULL;
                       break;
               }
        }
        if (i == 3)
                goto out;

        if (wordcount == 8) {
                free_ptr = gf_strdup (words[7]);
                ret = dict_set_dynstr (dict, "opts", free_ptr);
                if (ret)
                        goto out;
                free_ptr = NULL;
        }

        ret = 0;
        *options = dict;
out:
       if (ret) {
               GF_FREE (free_ptr);
               dict_unref (dict);
       }

       return ret;
}

int
cli_cmd_volume_heal_options_parse (const char **words, int wordcount,
                                   dict_t **options)
{
        int     ret = 0;
        dict_t  *dict = NULL;

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_str (dict, "volname", (char *) words[2]);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to set volname");
                goto out;
        }

        if (wordcount == 3) {
                ret = dict_set_int32 (dict, "heal-op", GF_AFR_OP_HEAL_INDEX);
                goto done;
        }

        if (wordcount == 4) {
                if (!strcmp (words[3], "full")) {
                        ret = dict_set_int32 (dict, "heal-op",
                                              GF_AFR_OP_HEAL_FULL);
                        goto done;
                } else if (!strcmp (words[3], "info")) {
                        ret = dict_set_int32 (dict, "heal-op",
                                              GF_AFR_OP_INDEX_SUMMARY);
                        goto done;
                } else {
                        ret = -1;
                        goto out;
                }
        }
        if (wordcount == 5) {
                if (strcmp (words[3], "info")) {
                        ret = -1;
                        goto out;
                }
                if (!strcmp (words[4], "healed")) {
                        ret = dict_set_int32 (dict, "heal-op",
                                              GF_AFR_OP_HEALED_FILES);
                        goto done;
                }
                if (!strcmp (words[4], "heal-failed")) {
                        ret = dict_set_int32 (dict, "heal-op",
                                              GF_AFR_OP_HEAL_FAILED_FILES);
                        goto done;
                }
                if (!strcmp (words[4], "split-brain")) {
                        ret = dict_set_int32 (dict, "heal-op",
                                              GF_AFR_OP_SPLIT_BRAIN_FILES);
                        goto done;
                }
                ret = -1;
                goto out;
        }
        ret = -1;
        goto out;
done:
        *options = dict;
out:
        if (ret && dict) {
                dict_unref (dict);
                *options = NULL;
        }

        return ret;
}
