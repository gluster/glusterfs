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
                        cli_err ("Wrong brick type: %s, use <HOSTNAME>:"
                                 "<export-dir-abs-path>", words[brick_index]);
                        ret = -1;
                        goto out;
                } else {
                        delimiter = strrchr (words[brick_index], ':');
                        ret = gf_canonicalize_path (delimiter + 1);
                        if (ret)
                                goto out;
                }

                if ((brick_list_len + strlen (words[brick_index]) + 1) > sizeof (brick_list)) {
                        cli_err ("Total brick list is larger than a request. "
                                 "Can take (brick_count %d)", *brick_count);
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
                      strcmp (host_name, "127.0.0.1") &&
                      strncmp (host_name, "0.", 2))) {
                        cli_err ("Please provide a valid hostname/ip other "
                                 "than localhost, 127.0.0.1 or loopback "
                                 "address (0.0.0.0 to 0.255.255.255).");
                        ret = -1;
                        GF_FREE (tmp_host);
                        goto out;
                }
                if (!valid_internet_address (host_name, _gf_false)) {
                        cli_err ("internet address '%s' does not conform to "
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
                                cli_err ("Found duplicate"
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

        char    *invalid_volnames[] = {"volume", "type", "subvolumes", "option",
                                       "end-volume", "all", "volume_not_in_ring",
                                       NULL};
        char    *w = NULL;
        int      op_count = 0;
        int32_t  replica_count = 1;
        int32_t  stripe_count = 1;
        gf_boolean_t is_force = _gf_false;
        int wc = wordcount;

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

                for (i = 0; invalid_volnames[i]; i++) {
                        if (!strcmp (volname, invalid_volnames[i])) {
                                cli_err ("\"%s\" cannot be the name of a volume.",
                                         volname);
                                goto out;
                        }
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
                                cli_err ("replica option given twice");
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
                                cli_err ("replica count should be greater"
                                         " than 1");
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
                                cli_err ("stripe option given twice");
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
                                cli_err ("stripe count should be greater"
                                         " than 1");
                                ret = -1;
                                goto out;
                        }
                        ret = dict_set_int32 (dict, "stripe-count", stripe_count);
                        if (ret)
                                goto out;

                        index += 2;

                } else if ((strcmp (w, "transport")) == 0) {
                        if (trans_type) {
                                cli_err ("'transport' option given more"
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
                }              else {
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

        if (strcmp (words[wordcount - 1], "force") == 0) {
                is_force = _gf_true;
                wc = wordcount - 1;
        }

        ret = cli_cmd_bricks_parse (words, wc, brick_index, &bricks,
                                    &brick_count);
        if (ret)
                goto out;

        /* If brick-count is not valid when replica or stripe is
           given, exit here */
        if (!brick_count) {
                cli_err ("No bricks specified");
                ret = -1;
                goto out;
        }

        if (brick_count % sub_count) {
                if (type == GF_CLUSTER_TYPE_STRIPE)
                        cli_err ("number of bricks is not a multiple of "
                                 "stripe count");
                else if (type == GF_CLUSTER_TYPE_REPLICATE)
                        cli_err ("number of bricks is not a multiple of "
                                 "replica count");
                else
                        cli_err ("number of bricks given doesn't match "
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
        trans_type = NULL;

        ret = dict_set_dynstr (dict, "bricks", bricks);
        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "count", brick_count);
        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "force", is_force);
        if (ret)
                goto out;

        *options = dict;

out:
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to parse create volume CLI");
                if (dict)
                        dict_destroy (dict);
        }

        GF_FREE (trans_type);

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
                                      "remove", "list", "alert-time",
                                      "soft-timeout", "hard-timeout",
                                      "default-soft-limit", NULL};
        char            *w       = NULL;
        uint32_t         time    = 0;
        double           percent = 0;

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
                        cli_err ("\"all\" cannot be the name of a volume.");
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
                cli_out ("Invalid quota option : %s", words[3]);
                ret = - 1;
                goto out;
        }

        if (strcmp (w, "enable") == 0) {
                if (wordcount == 4) {
                        type = GF_QUOTA_OPTION_TYPE_ENABLE;
                        ret = 0;
                        goto set_type;
                } else {
                        ret = -1;
                        goto out;
                }
        }

        if (strcmp (w, "disable") == 0) {
                if (wordcount == 4) {
                        type = GF_QUOTA_OPTION_TYPE_DISABLE;
                        ret = 0;
                        goto set_type;
                } else {
                        ret = -1;
                        goto out;
                }
        }

        if (strcmp (w, "limit-usage") == 0) {

                if (wordcount < 6 || wordcount > 7) {
                        ret = -1;
                        goto out;
                }

                type = GF_QUOTA_OPTION_TYPE_LIMIT_USAGE;

                if (words[4][0] != '/') {
                        cli_err ("Please enter absolute path");
                        ret = -1;
                        goto out;
                }
                ret = dict_set_str (dict, "path", (char *) words[4]);
                if (ret)
                        goto out;

                if (!words[5]) {
                        cli_err ("Please enter the limit value to be set");
                        ret = -1;
                        goto out;
                }

                ret = gf_string2bytesize (words[5], &value);
                if (ret != 0) {
                        if (errno == ERANGE)
                                cli_err ("Value too large: %s", words[5]);
                        else
                                cli_err ("Please enter a correct value");
                        goto out;
                }

                ret  = dict_set_str (dict, "hard-limit", (char *) words[5]);
                if (ret < 0)
                        goto out;

                if (wordcount == 7) {

                        ret = gf_string2percent (words[6], &percent);
                        if (ret != 0) {
                                cli_err ("Please enter a correct value");
                                goto out;
                        }

                        ret = dict_set_str (dict, "soft-limit",
                                            (char *) words[6]);
                        if (ret < 0)
                                goto out;
                }

                goto set_type;
        }
        if (strcmp (w, "remove") == 0) {
                if (wordcount != 5) {
                        ret = -1;
                        goto out;
                }

                type = GF_QUOTA_OPTION_TYPE_REMOVE;

                if (words[4][0] != '/') {
                        cli_err ("Please enter absolute path");
                        ret = -1;
                        goto out;
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


        if (strcmp (w, "alert-time") == 0) {
                if (wordcount != 5) {
                        ret = -1;
                        goto out;
                }
                type = GF_QUOTA_OPTION_TYPE_ALERT_TIME;

                ret = gf_string2time (words[4], &time);
                if (ret) {
                        cli_err ("Invalid argument %s. Please enter a valid "
                                 "string", words[4]);
                        goto out;
                }

                ret = dict_set_str (dict, "value", (char *)words[4]);
                if (ret < 0)
                        goto out;
                goto set_type;
        }
        if (strcmp (w, "soft-timeout") == 0) {
                if (wordcount != 5) {
                        ret = -1;
                        goto out;
                }
                type = GF_QUOTA_OPTION_TYPE_SOFT_TIMEOUT;

                ret = gf_string2time (words[4], &time);
                if (ret) {
                        cli_err ("Invalid argument %s. Please enter a valid "
                                 "string", words[4]);
                        goto out;
                }

                ret = dict_set_str (dict, "value", (char *)words[4]);
                if (ret < 0)
                        goto out;
                goto set_type;
        }
        if (strcmp (w, "hard-timeout") == 0) {
                if(wordcount != 5) {
                        ret = -1;
                        goto out;
                }
                type = GF_QUOTA_OPTION_TYPE_HARD_TIMEOUT;

                ret = gf_string2time (words[4], &time);
                if (ret) {
                        cli_err ("Invalid argument %s. Please enter a valid "
                                 "string", words[4]);
                        goto out;
                }

                ret = dict_set_str (dict, "value", (char *)words[4]);
                if (ret < 0)
                        goto out;
                goto set_type;
        }
        if (strcmp (w, "default-soft-limit") == 0) {
                if(wordcount != 5) {
                        ret = -1;
                        goto out;
                }
                type = GF_QUOTA_OPTION_TYPE_DEFAULT_SOFT_LIMIT;

                ret = dict_set_str (dict, "value", (char *)words[4]);
                if (ret < 0)
                        goto out;
                goto set_type;
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

static inline gf_boolean_t
cli_is_key_spl (char *key)
{
        return (strcmp (key, "group") == 0);
}

#define GLUSTERD_DEFAULT_WORKDIR "/var/lib/glusterd"
static int
cli_add_key_group (dict_t *dict, char *key, char *value, char **op_errstr)
{
        int             ret = -1;
        int             opt_count = 0;
        char            iter_key[1024] = {0,};
        char            iter_val[1024] = {0,};
        char            *saveptr = NULL;
        char            *tok_key = NULL;
        char            *tok_val = NULL;
        char            *dkey = NULL;
        char            *dval = NULL;
        char            *tagpath = NULL;
        char            *buf = NULL;
        char            line[PATH_MAX + 256] = {0,};
        char            errstr[2048] = "";
        FILE            *fp = NULL;

        ret = gf_asprintf (&tagpath, "%s/groups/%s",
                           GLUSTERD_DEFAULT_WORKDIR, value);
        if (ret == -1) {
                tagpath = NULL;
                goto out;
        }

        fp = fopen (tagpath, "r");
        if (!fp) {
                ret = -1;
                snprintf(errstr, sizeof(errstr), "Unable to open file '%s'."
                         " Error: %s", tagpath, strerror (errno));
                if (op_errstr)
                        *op_errstr = gf_strdup(errstr);
                goto out;
        }

        opt_count = 0;
        buf = line;
        while (fscanf (fp, "%s", buf) != EOF) {

                opt_count++;
                tok_key = strtok_r (line, "=", &saveptr);
                tok_val = strtok_r (NULL, "=", &saveptr);
                if (!tok_key || !tok_val) {
                        ret = -1;
                        snprintf(errstr, sizeof(errstr), "'%s' file format "
                                 "not valid.", tagpath);
                        if (op_errstr)
                                *op_errstr = gf_strdup(errstr);
                        goto out;
                }

                snprintf (iter_key, sizeof (iter_key), "key%d", opt_count);
                dkey = gf_strdup (tok_key);
                ret = dict_set_dynstr (dict, iter_key, dkey);
                if (ret)
                        goto out;
                dkey = NULL;

                snprintf (iter_val, sizeof (iter_val), "value%d", opt_count);
                dval = gf_strdup (tok_val);
                ret = dict_set_dynstr (dict, iter_val, dval);
                if (ret)
                        goto out;
                dval = NULL;

        }

        if (!opt_count) {
                ret = -1;
                snprintf(errstr, sizeof(errstr), "'%s' file format "
                         "not valid.", tagpath);
                if (op_errstr)
                        *op_errstr = gf_strdup(errstr);
                goto out;
        }
        ret = dict_set_int32 (dict, "count", opt_count);
out:

        GF_FREE (tagpath);

        if (ret) {
                GF_FREE (dkey);
                GF_FREE (dval);
        }

        if (fp)
                fclose (fp);

        return ret;
}
#undef GLUSTERD_DEFAULT_WORKDIR

int32_t
cli_cmd_volume_set_parse (const char **words, int wordcount, dict_t **options,
                          char **op_errstr)
{
        dict_t                  *dict = NULL;
        char                    *volname = NULL;
        int                     ret = -1;
        int                     count = 0;
        char                    *key = NULL;
        char                    *value = NULL;
        int                     i = 0;
        char                    str[50] = {0,};

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

        if ((!strcmp (volname, "help") || !strcmp (volname, "help-xml"))
            && wordcount == 3 ) {
                ret = dict_set_str (dict, volname, volname);
                if (ret)
                        goto out;

        } else if (wordcount < 5) {
                ret = -1;
                goto out;

        } else if (wordcount == 5  && cli_is_key_spl ((char *)words[3])) {
                key = (char *) words[3];
                value = (char *) words[4];
                if ( !key || !value) {
                        ret = -1;
                        goto out;
                }

                ret = gf_strip_whitespace (value, strlen (value));
                if (ret == -1)
                        goto out;

                if (strlen (value) == 0) {
                        ret = -1;
                        goto out;
                }

                ret = cli_add_key_group (dict, key, value, op_errstr);
                if (ret == 0)
                        *options = dict;
                goto out;
        }

        for (i = 3; i < wordcount; i+=2) {

                key = (char *) words[i];
                value = (char *) words[i+1];

                if ( !key || !value) {
                        ret = -1;
                        goto out;
                }

                count++;

                ret = gf_strip_whitespace (value, strlen (value));
                if (ret == -1)
                        goto out;

                if (strlen (value) == 0) {
                        ret = -1;
                        goto out;
                }

                if (cli_is_key_spl (key)) {
                        ret = -1;
                        goto out;
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
        if (ret)
                dict_destroy (dict);

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
        gf_boolean_t is_force = _gf_false;
        int wc = wordcount;

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
                        cli_err ("replica count should be greater than 1");
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
                        cli_err ("stripe count should be greater than 1");
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

        if (strcmp (words[wordcount - 1], "force") == 0) {
                is_force = _gf_true;
                wc = wordcount - 1;
        }

        ret = cli_cmd_bricks_parse (words, wc, brick_index, &bricks,
                                    &brick_count);
        if (ret)
                goto out;

        ret = dict_set_dynstr (dict, "bricks", bricks);
        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "count", brick_count);

        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "force", is_force);
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
        char    *opwords[] = { "start", "commit", "stop", "status",
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
                        cli_err ("replica count should be greater than 0 in "
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
                } else if (!strcmp ("stop", w)) {
                        command = GF_OP_CMD_STOP;
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
                        cli_err ("wrong brick type: %s, use <HOSTNAME>:"
                                 "<export-dir-abs-path>", words[brick_index]);
                        ret = -1;
                        goto out;
                } else {
                        delimiter = strrchr(words[brick_index], ':');
                        ret = gf_canonicalize_path (delimiter + 1);
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
                                cli_err("Duplicate bricks found %s",
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

        GF_FREE (tmp_brick);
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
        gf_boolean_t is_force = _gf_false;

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
                cli_err ("wrong brick type: %s, use "
                         "<HOSTNAME>:<export-dir-abs-path>", words[3]);
                ret = -1;
                goto out;
        } else {
                delimiter = strrchr ((char *)words[3], ':');
                ret = gf_canonicalize_path (delimiter + 1);
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
                cli_err ("wrong brick type: %s, use "
                         "<HOSTNAME>:<export-dir-abs-path>", words[4]);
                ret = -1;
                goto out;
        } else {
                delimiter = strrchr ((char *)words[4], ':');
                ret = gf_canonicalize_path (delimiter + 1);
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
                if ((replace_op != GF_REPLACE_OP_COMMIT) &&
                    (replace_op != GF_REPLACE_OP_START)) {
                        ret = -1;
                        goto out;
                }
                if (!strcmp ("force", words[op_index])) {
                        if (replace_op == GF_REPLACE_OP_COMMIT)
                                replace_op = GF_REPLACE_OP_COMMIT_FORCE;

                        else if (replace_op == GF_REPLACE_OP_START)
                                is_force = _gf_true;
                }
        }

        if (replace_op == GF_REPLACE_OP_NONE) {
                ret = -1;
                goto out;
        }

        ret = dict_set_int32 (dict, "operation", (int32_t) replace_op);

        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "force", is_force);
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
                        cli_err ("wrong brick type: %s, use <HOSTNAME>:"
                                 "<export-dir-abs-path>", words[4]);
                        ret = -1;
                        goto out;
                } else {
                        ret = gf_canonicalize_path (delimiter + 1);
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
                cli_err("Invalid log level [%s] specified", words[5]);
                cli_err("Valid values for loglevel: (DEBUG|WARNING|ERROR"
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
                        cli_err ("wrong brick type: %s, use <HOSTNAME>:"
                                 "<export-dir-abs-path>", words[4]);
                        ret = -1;
                        goto out;
                } else {
                        ret = gf_canonicalize_path (delimiter + 1);
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
                        cli_err ("wrong brick type: %s, use <HOSTNAME>:"
                                 "<export-dir-abs-path>", words[4]);
                        ret = -1;
                        goto out;
                } else {
                        ret = gf_canonicalize_path (delimiter + 1);
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

static int
config_parse (const char **words, int wordcount, dict_t *dict,
              unsigned cmdi, unsigned glob)
{
        int32_t            ret     = -1;
        int32_t            i       = -1;
        char               *append_str = NULL;
        size_t             append_len = 0;
        char               *subop = NULL;

        switch ((wordcount - 1) - cmdi) {
        case 0:
                subop = gf_strdup ("get-all");
                break;
        case 1:
                if (words[cmdi + 1][0] == '!') {
                        (words[cmdi + 1])++;
                        if (gf_asprintf (&subop, "del%s",
                                         glob ? "-glob" : "") == -1)
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
                /* "checkpoint now" is special: we resolve that "now" */
                if ((strcmp (words[cmdi + 1], "checkpoint") == 0) &&
                    (strcmp (append_str, "now") == 0)) {
                        struct timeval tv = {0,};

                        ret = gettimeofday (&tv, NULL);
                        if (ret == -1)
                                goto out;

                        GF_FREE (append_str);
                        append_str = GF_CALLOC (1, 300, cli_mt_append_str);
                        if (!append_str) {
                                ret = -1;
                                goto out;
                        }
                        snprintf (append_str, 300, "now:%ld.%06ld",
                                  tv.tv_sec, tv.tv_usec);
                }

                ret = dict_set_dynstr (dict, "op_value", append_str);
        }

        ret = -1;
        if (subop) {
                ret = dict_set_dynstr (dict, "subop", subop);
                if (!ret)
                      subop = NULL;
        }

out:
        if (ret && append_str)
                GF_FREE (append_str);

        GF_FREE (subop);

        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int32_t
force_push_pem_parse (const char **words, int wordcount,
                      dict_t *dict, unsigned *cmdi)
{
        int32_t            ret     = 0;

        if (!strcmp ((char *)words[wordcount-1], "force")) {
                if ((strcmp ((char *)words[wordcount-2], "start")) &&
                    (strcmp ((char *)words[wordcount-2], "stop")) &&
                    (strcmp ((char *)words[wordcount-2], "create")) &&
                    (strcmp ((char *)words[wordcount-2], "push-pem"))) {
                        ret = -1;
                        goto out;
                }
                ret = dict_set_uint32 (dict, "force",
                                       _gf_true);
                if (ret)
                        goto out;
                (*cmdi)++;

                if (!strcmp ((char *)words[wordcount-2], "push-pem")) {
                        if (strcmp ((char *)words[wordcount-3], "create")) {
                                ret = -1;
                                goto out;
                        }
                        ret = dict_set_int32 (dict, "push_pem", 1);
                        if (ret)
                                goto out;
                        (*cmdi)++;
                }
        } else if (!strcmp ((char *)words[wordcount-1], "push-pem")) {
                if (strcmp ((char *)words[wordcount-2], "create")) {
                        ret = -1;
                        goto out;
                }
                ret = dict_set_int32 (dict, "push_pem", 1);
                if (ret)
                        goto out;
                (*cmdi)++;
        }

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}


int32_t
cli_cmd_gsync_set_parse (const char **words, int wordcount, dict_t **options)
{
        int32_t            ret     = -1;
        dict_t             *dict   = NULL;
        gf1_cli_gsync_set  type    = GF_GSYNC_OPTION_TYPE_NONE;
        int                i       = 0;
        unsigned           masteri = 0;
        unsigned           slavei  = 0;
        unsigned           glob    = 0;
        unsigned           cmdi    = 0;
        char               *opwords[] = { "create", "status", "start", "stop",
                                          "config", "force", "delete",
                                          "push-pem", "detail", NULL };
        char               *w = NULL;

        GF_ASSERT (words);
        GF_ASSERT (options);

        dict = dict_new ();
        if (!dict)
                goto out;

        /* new syntax:
         *
         * volume geo-replication $m $s create [push-pem] [force]
         * volume geo-replication [$m [$s]] status [detail]
         * volume geo-replication [$m] $s config [[!]$opt [$val]]
         * volume geo-replication $m $s start|stop [force]
         * volume geo-replication $m $s delete
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
                if (!strcmp ((char *)words[wordcount-1], "detail")) {
                        /* For status detail it is mandatory to provide
                         * both master and slave */
                        ret = -1;
                        goto out;
                }

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

        if (strcmp (w, "create") == 0) {
                type = GF_GSYNC_OPTION_TYPE_CREATE;

                if (!masteri || !slavei)
                        goto out;
        } else if (strcmp (w, "status") == 0) {
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
        } else if (strcmp (w, "delete") == 0) {
                type = GF_GSYNC_OPTION_TYPE_DELETE;

                if (!masteri || !slavei)
                        goto out;
        } else
                GF_ASSERT (!"opword mismatch");

        ret = force_push_pem_parse (words, wordcount, dict, &cmdi);
        if (ret)
                goto out;

        if (!strcmp ((char *)words[wordcount-1], "detail")) {
                if (strcmp ((char *)words[wordcount-2], "status")) {
                        ret = -1;
                        goto out;
                }
                if (!slavei || !masteri) {
                        ret = -1;
                        goto out;
                }
                ret = dict_set_uint32 (dict, "status-detail", _gf_true);
                if (ret)
                        goto out;
                cmdi++;
        }

        if (type != GF_GSYNC_OPTION_TYPE_CONFIG &&
            (cmdi < wordcount - 1 || glob))
                goto out;

        /* If got so far, input is valid, assemble the message */

        ret = 0;

        if (masteri) {
                ret = dict_set_str (dict, "master", (char *)words[masteri]);
                if (!ret)
                        ret = dict_set_str (dict, "volname",
                                            (char *)words[masteri]);
        }
        if (!ret && slavei)
                ret = dict_set_str (dict, "slave", (char *)words[slavei]);
        if (!ret)
                ret = dict_set_int32 (dict, "type", type);
        if (!ret && type == GF_GSYNC_OPTION_TYPE_CONFIG)
                ret = config_parse (words, wordcount, dict, cmdi, glob);

out:
        if (ret) {
                if (dict)
                        dict_destroy (dict);
        } else
                *options = dict;


        return ret;
}

int32_t
cli_cmd_volume_profile_parse (const char **words, int wordcount,
                              dict_t **options)
{
        dict_t              *dict       = NULL;
        char                *volname    = NULL;
        int                 ret         = -1;
        gf1_cli_stats_op    op          = GF_CLI_STATS_NONE;
        gf1_cli_info_op     info_op     = GF_CLI_INFO_NONE;
        gf_boolean_t        is_peek     = _gf_false;

        char      *opwords[] = { "start", "stop", "info", NULL };
        char      *w = NULL;

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

        w = str_getunamb (words[3], opwords);
        if (!w) {
                ret = -1;
                goto out;
        }

        if ((strcmp (w, "start") == 0 || strcmp (w, "stop") == 0) &&
            wordcount > 5)
                goto out;

        if (strcmp (w, "info") == 0 && wordcount > 7)
                goto out;

        if (strcmp (w, "start") == 0) {
                op = GF_CLI_STATS_START;
        } else if (strcmp (w, "stop") == 0) {
                op = GF_CLI_STATS_STOP;
        } else if (strcmp (w, "info") == 0) {
                op = GF_CLI_STATS_INFO;
                info_op = GF_CLI_INFO_ALL;
                if (wordcount > 4) {
                        if (strcmp (words[4], "incremental") == 0) {
                                info_op = GF_CLI_INFO_INCREMENTAL;
                                if (wordcount > 5 &&
                                    strcmp (words[5], "peek") == 0) {
                                        is_peek = _gf_true;
                                }
                        } else if (strcmp (words[4], "cumulative") == 0) {
                                info_op = GF_CLI_INFO_CUMULATIVE;
                        } else if (strcmp (words[4], "clear") == 0) {
                                info_op = GF_CLI_INFO_CLEAR;
                        } else if (strcmp (words[4], "peek") == 0) {
                                is_peek = _gf_true;
                        }
                }
        } else
                GF_ASSERT (!"opword mismatch");

        ret = dict_set_int32 (dict, "op", (int32_t)op);
        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "info-op", (int32_t)info_op);
        if (ret)
                goto out;

        ret = dict_set_int32 (dict, "peek", is_peek);
        if (ret)
                goto out;

        if (!strcmp (words[wordcount - 1], "nfs")) {
                ret = dict_set_int32 (dict, "nfs", _gf_true);
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
                                    "clear", NULL };
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
        } else if (strcmp (w, "clear") == 0) {
                ret = dict_set_int32 (dict, "clear-stats", 1);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "Could not set clear-stats in dict");
                        goto out;
                }
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
                                cli_err ("wrong brick type: %s, use <HOSTNAME>:"
                                         "<export-dir-abs-path>", value);
                                ret = -1;
                                goto out;
                        } else {
                                ret = gf_canonicalize_path (delimiter + 1);
                                if (ret)
                                        goto out;
                        }
                        ret = dict_set_str (dict, "brick", value);

                } else if (!strcmp (key, "list-cnt")) {
                        ret = gf_is_str_int (value);
                        if (!ret)
                                list_cnt = atoi (value);
                        if (ret || (list_cnt < 0) || (list_cnt > 100)) {
                                cli_err ("list-cnt should be between 0 to 100");
                                ret = -1;
                                goto out;
                        }
                } else if (perf && !nfs && !strcmp (key, "bs")) {
                        ret = gf_is_str_int (value);
                        if (!ret)
                                blk_size = atoi (value);
                        if (ret || (blk_size <= 0)) {
                                if (blk_size < 0)
                                        cli_err ("block size is an invalid"
                                                 " number");
                                else
                                        cli_err ("block size should be an "
                                                 "integer greater than zero");
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
                                        cli_err ("count is an invalid number");
                                else
                                        cli_err ("count should be an integer "
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
                cli_err ("Need to give both 'bs' and 'count'");
                ret = -1;
                goto out;
        } else if (((uint64_t)blk_size * count) > (10 * GF_UNIT_GB)) {
                cli_err ("'bs * count' value %"PRIu64" is greater than "
                         "maximum allowed value of 10GB",
                         ((uint64_t)blk_size * count));
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
                                "inode", "callpool", "tasks", NULL};
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
                { "tasks",    GF_CLI_STATUS_TASKS    },
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
                                cli_err ("%s is not a valid status option",
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
                                } else if (!strcmp (words[3], "shd")) {
                                        cmd |= GF_CLI_STATUS_SHD;
                                } else if (!strcmp (words[3], "quotad")) {
                                        cmd |= GF_CLI_STATUS_QUOTAD;
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
                        cli_err ("Cannot specify brick/nfs for \"all\"");
                        ret = -1;
                        goto out;
                }

                cmd = cli_cmd_get_statusop (words[4]);
                if (cmd == GF_CLI_STATUS_NONE) {
                        cli_err ("%s is not a valid status option",
                                 words[4]);
                        ret = -1;
                        goto out;
                }


                ret = dict_set_str (dict, "volname", (char *)words[2]);
                if (ret)
                        goto out;

                if (!strcmp (words[3], "nfs")) {
                        if (cmd == GF_CLI_STATUS_FD ||
                            cmd == GF_CLI_STATUS_DETAIL ||
                            cmd == GF_CLI_STATUS_TASKS) {
                                cli_err ("Detail/FD/Tasks status not available"
                                         " for NFS Servers");
                                ret = -1;
                                goto out;
                        }
                        cmd |= GF_CLI_STATUS_NFS;
                } else if (!strcmp (words[3], "shd")){
                        if (cmd == GF_CLI_STATUS_FD ||
                            cmd == GF_CLI_STATUS_CLIENTS ||
                            cmd == GF_CLI_STATUS_DETAIL ||
                            cmd == GF_CLI_STATUS_TASKS) {
                                cli_err ("Detail/FD/Clients/Tasks status not "
                                         "available for Self-heal Daemons");
                                ret = -1;
                                goto out;
                        }
                        cmd |= GF_CLI_STATUS_SHD;
                } else if (!strcmp (words[3], "quotad")) {
                        if (cmd == GF_CLI_STATUS_FD ||
                            cmd == GF_CLI_STATUS_CLIENTS ||
                            cmd == GF_CLI_STATUS_DETAIL ||
                            cmd == GF_CLI_STATUS_INODE) {
                                cli_err ("Detail/FD/Clients/Inode status not "
                                         "available for Quota Daemon");
                                ret = -1;
                                goto out;
                        }
                        cmd |= GF_CLI_STATUS_QUOTAD;
                } else {
                        if (cmd == GF_CLI_STATUS_TASKS) {
                                cli_err ("Tasks status not available for "
                                         "bricks");
                                ret = -1;
                                goto out;
                        }
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
                              "fd", "inode", "history", "inodectx", "fdctx",
                              "quotad", NULL};
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
        if((strstr (option_str, "nfs")) && strstr (option_str, "quotad")) {
                ret = -1;
                goto out;
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

static int
extract_hostname_path_from_token (const char *tmp_words, char **hostname,
                                  char **path)
{
        int ret = 0;
        char *delimiter = NULL;
        char *tmp_host = NULL;
        char *host_name = NULL;
        char *words = NULL;

        *hostname = NULL;
        *path = NULL;

        words = GF_CALLOC (1, strlen (tmp_words) + 1, gf_common_mt_char);
        if (!words){
                ret = -1;
                goto out;
        }

        strncpy (words, tmp_words, strlen (tmp_words) + 1);

        if (validate_brick_name (words)) {
                cli_err ("Wrong brick type: %s, use <HOSTNAME>:"
                        "<export-dir-abs-path>", words);
                ret = -1;
                goto out;
        } else {
                delimiter = strrchr (words, ':');
                ret = gf_canonicalize_path (delimiter + 1);
                if (ret) {
                        goto out;
                } else {
                        *path = GF_CALLOC (1, strlen (delimiter+1) +1,
                                           gf_common_mt_char);
                        if (!*path) {
                           ret = -1;
                                goto out;

                        }
                        strncpy (*path, delimiter +1,
                                 strlen(delimiter + 1) + 1);
                }
        }

        tmp_host = gf_strdup (words);
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
            strcmp (host_name, "127.0.0.1") &&
            strncmp (host_name, "0.", 2))) {
                cli_err ("Please provide a valid hostname/ip other "
                         "than localhost, 127.0.0.1 or loopback "
                         "address (0.0.0.0 to 0.255.255.255).");
                ret = -1;
                goto out;
        }
        if (!valid_internet_address (host_name, _gf_false)) {
                cli_err ("internet address '%s' does not conform to "
                          "standards", host_name);
                ret = -1;
                goto out;
        }

        *hostname = GF_CALLOC (1, strlen (host_name) + 1,
                                       gf_common_mt_char);
        if (!*hostname) {
                ret = -1;
                goto out;
        }
        strncpy (*hostname, host_name, strlen (host_name) + 1);
        ret = 0;

out:
        GF_FREE (words);
        GF_FREE (tmp_host);
        return ret;
}


int
cli_cmd_volume_heal_options_parse (const char **words, int wordcount,
                                   dict_t **options)
{
        int     ret = 0;
        dict_t  *dict = NULL;
        char    *hostname = NULL;
        char    *path = NULL;

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
                } else if (!strcmp (words[3], "statistics")) {
                        ret = dict_set_int32 (dict, "heal-op",
                                              GF_AFR_OP_STATISTICS);
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
                if (strcmp (words[3], "info") &&
                    strcmp (words[3], "statistics")) {
                        ret = -1;
                        goto out;
                }

                if (!strcmp (words[3], "info")) {
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
                }

                if (!strcmp (words[3], "statistics")) {
                        if (!strcmp (words[4], "heal-count")) {
                                ret = dict_set_int32 (dict, "heal-op",
                                               GF_AFR_OP_STATISTICS_HEAL_COUNT);
                                goto done;
                        }
                }
                ret = -1;
                goto out;
        }
        if (wordcount == 7) {
                if (!strcmp (words[3], "statistics")
                    && !strcmp (words[4], "heal-count")
                    && !strcmp (words[5], "replica")) {

                        ret = dict_set_int32 (dict, "heal-op",
                                   GF_AFR_OP_STATISTICS_HEAL_COUNT_PER_REPLICA);
                        if (ret)
                                goto out;
                        ret = extract_hostname_path_from_token (words[6],
                                                              &hostname, &path);
                        if (ret)
                                goto out;
                        ret = dict_set_dynstr (dict, "per-replica-cmd-hostname",
                                               hostname);
                        if (ret)
                                goto out;
                        ret = dict_set_dynstr (dict, "per-replica-cmd-path",
                                               path);
                        if (ret)
                                goto out;
                        else
                                goto done;

                }
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

int
cli_cmd_volume_defrag_parse (const char **words, int wordcount,
                             dict_t **options)
{
        dict_t                 *dict = NULL;
        int                      ret = -1;
        char                *option  = NULL;
        char                *volname = NULL;
        char                *command = NULL;
        gf_cli_defrag_type       cmd = 0;

        GF_ASSERT (words);
        GF_ASSERT (options);

        dict = dict_new ();
        if (!dict)
                goto out;

        if (!((wordcount == 4) || (wordcount == 5)))
                goto out;

        if (wordcount == 4) {
                if (strcmp (words[3], "start") && strcmp (words[3], "stop") &&
                    strcmp (words[3], "status"))
                            goto out;
        } else {
                if (strcmp (words[3], "fix-layout") &&
                    strcmp (words[3], "start"))
                        goto out;
        }

        volname = (char *) words[2];

        if (wordcount == 4) {
                command = (char *) words[3];
        }
        if (wordcount == 5) {
               if ((strcmp (words[3], "fix-layout") ||
                    strcmp (words[4], "start")) &&
                    (strcmp (words[3], "start") ||
                    strcmp (words[4], "force"))) {
                        ret = -1;
                        goto out;
                }
                command = (char *) words[3];
                option = (char *) words[4];
        }

        if (strcmp (command, "start") == 0) {
                cmd = GF_DEFRAG_CMD_START;
                if (option && strcmp (option, "force") == 0) {
                                cmd = GF_DEFRAG_CMD_START_FORCE;
                        }
                goto done;
        }

        if (strcmp (command, "fix-layout") == 0) {
                cmd = GF_DEFRAG_CMD_START_LAYOUT_FIX;
                goto done;
        }
        if (strcmp (command, "stop") == 0) {
                cmd = GF_DEFRAG_CMD_STOP;
                goto done;
        }
        if (strcmp (command, "status") == 0) {
                cmd = GF_DEFRAG_CMD_STATUS;
        }

done:
        ret = dict_set_str (dict, "volname", volname);

        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to set dict");
                goto out;
        }

        ret = dict_set_int32 (dict, "rebalance-command", (int32_t) cmd);

        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to set dict");
                goto out;
        }

        *options = dict;

out:
        if (ret && dict)
                dict_destroy (dict);

        return ret;
}
