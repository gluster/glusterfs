/*
   Copyright (c) 2010-2013 Red Hat, Inc. <http://www.redhat.com>
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
#include <fnmatch.h>
#include <time.h>

#include "cli.h"
#include "cli-cmd.h"
#include "cli-mem-types.h"
#include "dict.h"

#include "protocol-common.h"
#include "cli1-xdr.h"

#define MAX_SNAP_DESCRIPTION_LEN 1024

struct snap_config_opt_vals_ snap_confopt_vals[] = {
        {.op_name        = "snap-max-hard-limit",
         .question       = "Changing snapshot-max-hard-limit "
                           "will limit the creation of new snapshots "
                           "if they exceed the new limit.\n"
                           "Do you want to continue?"
        },
        {.op_name        = "snap-max-soft-limit",
         .question       = "If Auto-delete is enabled, snap-max-soft-limit will"
                           " trigger deletion of oldest snapshot, on the "
                           "creation of new snapshot, when the "
                           "snap-max-soft-limit is reached.\n"
                           "Do you want to change the snap-max-soft-limit?"
        },
        {.op_name        = "both",
        .question        = "Changing snapshot-max-hard-limit "
                           "will limit the creation of new snapshots "
                           "if they exceed the new snapshot-max-hard-limit.\n"
                           "If Auto-delete is enabled, snap-max-soft-limit will"
                           " trigger deletion of oldest snapshot, on the "
                           "creation of new snapshot, when the "
                           "snap-max-soft-limit is reached.\n"
                           "Do you want to continue?"
        },
        {.op_name        = NULL,
        }
};

enum cli_snap_config_set_types {
        GF_SNAP_CONFIG_SET_HARD = 0,
        GF_SNAP_CONFIG_SET_SOFT = 1,
        GF_SNAP_CONFIG_SET_BOTH = 2,
};
typedef enum cli_snap_config_set_types cli_snap_config_set_types;

int
cli_cmd_validate_volume (char *volname);

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
cli_cmd_create_disperse_check (struct cli_state *state, int *disperse,
                               int *redundancy, int *data, int count)
{
        int i = 0;
        int tmp = 0;
        gf_answer_t answer = GF_ANSWER_NO;
        char question[128];

        const char *question1 = "There isn't an optimal redundancy value "
                                "for this configuration. Do you want to "
                                "create the volume with redundancy 1 ?";

        const char *question2 = "The optimal redundancy for this "
                                "configuration is %d. Do you want to create "
                                "the volume with this value ?";

        const char *question3 = "This configuration is not optimal on most "
                                "workloads. Do you want to use it ?";

        const char *question4 = "Redundancy for this configuration is %d. "
                                "Do you want to create "
                                "the volume with this value ?";

        if (*data > 0) {
                if (*disperse > 0 && *redundancy > 0) {
                        if (*disperse != (*data + *redundancy)) {
                                cli_err ("Disperse count(%d) should be equal "
                                         "to sum of disperse-data count(%d) and "
                                         "redundancy count(%d)", *disperse,
                                         *data, *redundancy);
                                return -1;
                        }
                } else if (*redundancy > 0) {
                        *disperse = *data + *redundancy;
                } else if (*disperse > 0) {
                        *redundancy = *disperse - *data;
                } else {
                        if ((count - *data) >= *data) {
                                cli_err ("Please provide redundancy count "
                                         "along with disperse-data count");
                                return -1;
                        } else {
                                sprintf (question, question4, count - *data);
                                answer = cli_cmd_get_confirmation (state,
                                                                   question);
                                if (answer == GF_ANSWER_NO)
                                        return -1;
                                *redundancy = count - *data;
                                *disperse = count;
                        }
                }
        }

        if (*disperse <= 0) {
                if (count < 3) {
                        cli_err ("number of bricks must be greater "
                                 "than 2");

                        return -1;
                }
                *disperse = count;
        }

        if (*redundancy == -1) {
                tmp = *disperse - 1;
                for (i = tmp / 2;
                     (i > 0) && ((tmp & -tmp) != tmp);
                     i--, tmp--);

                if (i == 0) {
                        answer = cli_cmd_get_confirmation(state, question1);
                        if (answer == GF_ANSWER_NO)
                                return -1;

                        *redundancy = 1;
                }
                else
                {
                        *redundancy = *disperse - tmp;
                        if (*redundancy > 1) {
                                sprintf(question, question2, *redundancy);
                                answer = cli_cmd_get_confirmation(state,
                                                                  question);
                                if (answer == GF_ANSWER_NO)
                                        return -1;
                        }
                }

                tmp = 0;
        } else {
                tmp = *disperse - *redundancy;
        }

        if (*redundancy > (*disperse - 1) / 2) {
                cli_err ("redundancy must be less than %d for a "
                         "disperse %d volume",
                         (*disperse + 1) / 2, *disperse);

                return -1;
        }

        if ((tmp & -tmp) != tmp) {
                answer = cli_cmd_get_confirmation(state, question3);
                if (answer == GF_ANSWER_NO)
                        return -1;
        }

        return 0;
}

static int32_t
cli_validate_disperse_volume (char *word, gf1_cluster_type type,
                              const char **words, int32_t wordcount,
                              int32_t index, int32_t *disperse_count,
                              int32_t *redundancy_count,
                              int32_t *data_count)
{
        int     ret = -1;

        switch (type) {
        case GF_CLUSTER_TYPE_NONE:
        case GF_CLUSTER_TYPE_DISPERSE:
                if (strcmp (word, "disperse") == 0) {
                        if (*disperse_count >= 0) {
                                cli_err ("disperse option given twice");
                                goto out;
                        }
                        if (wordcount < (index+2)) {
                                goto out;
                        }
                        ret = gf_string2int (words[index + 1], disperse_count);
                        if (ret == -1 && errno == EINVAL) {
                                *disperse_count = 0;
                                ret = 1;
                        } else if (ret == -1) {
                                goto out;
                        } else {
                                if (*disperse_count < 3) {
                                        cli_err ("disperse count must "
                                                 "be greater than 2");
                                        goto out;
                                }
                                ret = 2;
                        }
                } else if (strcmp (word, "disperse-data") == 0) {
                        if (*data_count >= 0) {
                                cli_err ("disperse-data option given twice");
                                goto out;
                        }
                        if (wordcount < (index+2)) {
                                goto out;
                        }
                        ret = gf_string2int (words[index+1], data_count);
                        if (ret == -1 || *data_count < 2) {
                                cli_err ("disperse-data must be greater than 1");
                                goto out;
                        }
                        ret = 2;
                } else if (strcmp (word, "redundancy") == 0) {
                        if (*redundancy_count >= 0) {
                                cli_err ("redundancy option given twice");
                                goto out;
                        }
                        if (wordcount < (index+2)) {
                                goto out;
                        }
                        ret = gf_string2int (words[index+1], redundancy_count);
                        if (ret == -1 || *redundancy_count < 1) {
                                cli_err ("redundancy must be greater than 0");
                                goto out;
                        }
                        ret = 2;
                }
                break;
        case GF_CLUSTER_TYPE_STRIPE_REPLICATE:
                cli_err ("striped-replicated-dispersed volume "
                         "is not supported");
                goto out;
        case GF_CLUSTER_TYPE_TIER:
                cli_err ("tier-dispersed volume is not "
                         "supported");
                goto out;
        case GF_CLUSTER_TYPE_STRIPE:
                cli_err ("striped-dispersed volume is not "
                         "supported");
                goto out;
        case GF_CLUSTER_TYPE_REPLICATE:
                cli_err ("replicated-dispersed volume is not "
                         "supported");
                goto out;
        default:
                cli_err ("Invalid type given");
                break;
        }
out:
        return ret;
}

int32_t
cli_validate_volname (const char *volname)
{
        int32_t            ret                       = -1;
        int32_t            i                         = -1;
        static const char * const invalid_volnames[] = {
                                      "volume", "type", "subvolumes", "option",
                                      "end-volume", "all", "volume_not_in_ring",
                                      "description", "force",
                                      "snap-max-hard-limit",
                                      "snap-max-soft-limit", "auto-delete",
                                      "activate-on-create", NULL};

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

        if (strlen (volname) > GD_VOLUME_NAME_MAX) {
                cli_err("Volume name exceeds %d characters.",
                        GD_VOLUME_NAME_MAX);
                goto out;
        }

        for (i = 0; i < strlen (volname); i++) {
                if (!isalnum (volname[i]) && (volname[i] != '_') &&
                   (volname[i] != '-')) {
                        cli_err ("Volume name should not contain \"%c\""
                                 " character.\nVolume names can only"
                                 "contain alphanumeric, '-' and '_' "
                                 "characters.", volname[i]);
                        goto out;
                }
        }

        ret = 0;
out:
        return ret;
}

int32_t
cli_cmd_volume_create_parse (struct cli_state *state, const char **words,
                             int wordcount, dict_t **options)
{
        dict_t  *dict = NULL;
        char    *volname = NULL;
        int     ret = -1;
        gf1_cluster_type type = GF_CLUSTER_TYPE_NONE;
        int     sub_count = 1;
        int     brick_index = 0;
        char    *trans_type = NULL;
        int32_t index = 0;
        char    *bricks = NULL;
        int32_t brick_count = 0;
        char    *opwords[] = { "replica", "stripe", "transport", "disperse",
                               "redundancy", "disperse-data", "arbiter", NULL };

        char    *w = NULL;
        int      op_count = 0;
        int32_t  replica_count = 1;
        int32_t  arbiter_count = 0;
        int32_t  stripe_count = 1;
        int32_t  disperse_count = -1;
        int32_t  redundancy_count = -1;
        int32_t  disperse_data_count = -1;
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
        if (cli_validate_volname (volname) < 0)
                goto out;

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
                        case GF_CLUSTER_TYPE_TIER:
                                cli_err ("replicated-tiered volume is not "
                                         "supported");
                                goto out;
                                break;
                        case GF_CLUSTER_TYPE_DISPERSE:
                                cli_err ("replicated-dispersed volume is not "
                                         "supported");
                                goto out;
                        default:
                                cli_err ("Invalid type given");
                                goto out;
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
                        if (words[index]) {
                                if (!strcmp (words[index], "arbiter")) {
                                        ret = gf_string2int (words[index+1],
                                                             &arbiter_count);
                                        if (ret == -1 || arbiter_count != 1 ||
                                            replica_count != 3) {
                                                cli_err ("For arbiter "
                                                         "configuration, "
                                                         "replica count must be"
                                                         " 3 and arbiter count "
                                                         "must be 1. The 3rd "
                                                         "brick of the replica "
                                                         "will be the arbiter");
                                                ret = -1;
                                                goto out;
                                        }
                                        ret = dict_set_int32 (dict, "arbiter-count",
                                                              arbiter_count);
                                        if (ret)
                                                goto out;
                                        index += 2;
                                }
                        }

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
                        case GF_CLUSTER_TYPE_DISPERSE:
                                cli_err ("striped-dispersed volume is not "
                                         "supported");
                                goto out;
                        case GF_CLUSTER_TYPE_TIER:
                                cli_err ("striped-tier volume is not "
                                         "supported");
                                goto out;
                        default:
                                cli_err ("Invalid type given");
                                goto out;
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

                } else if ((strcmp (w, "disperse") == 0) ||
                           (strcmp (w, "redundancy") == 0) ||
                           (strcmp (w, "disperse-data") == 0)) {
                        ret = cli_validate_disperse_volume (w, type, words,
                                      wordcount, index, &disperse_count,
                                      &redundancy_count, &disperse_data_count);
                        if (ret < 0)
                                goto out;
                        index += ret;
                        type = GF_CLUSTER_TYPE_DISPERSE;
                } else if ((strcmp (w, "arbiter") == 0)) {
                        cli_err ("arbiter option must be preceded by replica "
                                 "option.");
                        ret = -1;
                        goto out;
                } else {
                        GF_ASSERT (!"opword mismatch");
                        ret = -1;
                        goto out;
                }
                op_count++;
        }

        if (!trans_type)
                trans_type = gf_strdup ("tcp");

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

        if (type == GF_CLUSTER_TYPE_DISPERSE) {
                ret = cli_cmd_create_disperse_check (state, &disperse_count,
                                                     &redundancy_count,
                                                     &disperse_data_count,
                                                     brick_count);
                if (!ret)
                        ret = dict_set_int32 (dict, "disperse-count",
                                              disperse_count);
                if (!ret)
                        ret = dict_set_int32 (dict, "redundancy-count",
                                              redundancy_count);
                if (ret)
                        goto out;

                sub_count = disperse_count;
        } else
                sub_count = stripe_count * replica_count;

        if (brick_count % sub_count) {
                if (type == GF_CLUSTER_TYPE_STRIPE)
                        cli_err ("number of bricks is not a multiple of "
                                 "stripe count");
                else if (type == GF_CLUSTER_TYPE_REPLICATE)
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

        /* Everything is parsed fine. start setting info in dict */
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
                        dict_unref (dict);
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
                dict_unref (dict);
        }

                return ret;
}

/* Parsing global option for NFS-Ganesha config
 *  gluster nfs-ganesha enable/disable */

int32_t
cli_cmd_ganesha_parse (struct cli_state *state,
                       const char **words, int wordcount,
                       dict_t **options, char **op_errstr)
{
        dict_t  *dict     =       NULL;
        int     ret       =       -1;
        char    *key      =       NULL;
        char    *value    =       NULL;
        char    *w        =       NULL;
        char   *opwords[] =      { "enable", "disable", NULL };
        const char      *question       =       NULL;
        gf_answer_t     answer          =       GF_ANSWER_NO;


        GF_ASSERT (words);
        GF_ASSERT (options);

        dict = dict_new ();

        if (!dict)
                goto out;

        if (wordcount != 2)
                goto out;

        key   = (char *) words[0];
        value = (char *) words[1];

        if (!key || !value) {
                cli_out ("Usage : nfs-ganesha <enable/disable>");
                ret = -1;
                goto out;
        }

        ret = gf_strip_whitespace (value, strlen (value));
        if (ret == -1)
                goto out;

        if (strcmp (key, "nfs-ganesha")) {
                gf_asprintf (op_errstr, "Global option: error: ' %s '"
                          "is not a valid global option.", key);
                ret = -1;
                goto out;
        }

        w = str_getunamb (value, opwords);
        if (!w) {
                cli_out ("Invalid global option \n"
                         "Usage : nfs-ganesha <enable/disable>");
                ret = -1;
                goto out;
        }

        question = "Enabling NFS-Ganesha requires Gluster-NFS to be"
                   " disabled across the trusted pool. Do you "
                   "still want to continue?\n";

        if (strcmp (value, "enable") == 0) {
                answer = cli_cmd_get_confirmation (state, question);
                if (GF_ANSWER_NO == answer) {
                        gf_log ("cli", GF_LOG_ERROR, "Global operation "
                                "cancelled, exiting");
                        ret = -1;
                        goto out;
                }
        }
        cli_out ("This will take a few minutes to complete. Please wait ..");

        ret = dict_set_str (dict, "key", key);
        if (ret) {
               gf_log (THIS->name, GF_LOG_ERROR, "dict set on key failed");
                goto out;
        }

        ret = dict_set_str (dict, "value", value);
        if (ret) {
               gf_log (THIS->name, GF_LOG_ERROR, "dict set on value failed");
                goto out;
        }

        ret = dict_set_str (dict, "globalname", "All");
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "dict set on global"
                        " key failed.");
                goto out;
        }

        ret = dict_set_int32 (dict, "hold_global_locks", _gf_true);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "dict set on global key "
                        "failed.");
                goto out;
        }

        *options = dict;
out:
        if (ret)
                dict_unref (dict);

        return ret;
}

int32_t
cli_cmd_get_state_parse (struct cli_state *state,
                         const char **words, int wordcount,
                         dict_t **options, char **op_errstr)
{
        dict_t    *dict                 = NULL;
        int        ret                  = -1;
        char      *odir                 = NULL;
        char      *filename             = NULL;
        char      *daemon_name          = NULL;
        int        count                = 0;

        GF_VALIDATE_OR_GOTO ("cli", options, out);
        GF_VALIDATE_OR_GOTO ("cli", words, out);

        dict = dict_new ();
        if (!dict)
                goto out;

        if (wordcount < 1 || wordcount > 6) {
                *op_errstr = gf_strdup ("Problem parsing arguments."
                                        " Check usage.");
                goto out;
        }

        if (wordcount >= 1) {
                gf_asprintf (&daemon_name, "%s", "glusterd");

                for (count = 1; count < wordcount; count++) {
                        if (strcmp (words[count], "odir") == 0 ||
                                        strcmp (words[count], "file") == 0) {
                                if (strcmp (words[count], "odir") == 0) {
                                        if (++count < wordcount) {
                                                odir = (char *) words[count];
                                                continue;
                                        } else {
                                                ret = -1;
                                                goto out;
                                        }
                                } else if (strcmp (words[count], "file") == 0) {
                                        if (++count < wordcount) {
                                                filename = (char *) words[count];
                                                continue;
                                        } else {
                                                ret = -1;
                                                goto out;
                                        }
                                }
                        } else {
                                if (count > 1) {
                                        *op_errstr = gf_strdup ("Problem "
                                                        "parsing arguments. "
                                                        "Check usage.");
                                        ret = -1;
                                        goto out;

                                }
                                if (strcmp (words[count], "glusterd") == 0) {
                                        continue;
                                } else {
                                        *op_errstr = gf_strdup ("glusterd is "
                                                 "the only supported daemon.");
                                        ret = -1;
                                        goto out;
                                }
                        }
                }

                ret = dict_set_str (dict, "daemon", daemon_name);
                if (ret) {
                        *op_errstr = gf_strdup ("Command failed. Please check "
                                                " log file for more details.");
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "Setting daemon name to dictionary failed");
                        goto out;
                }

                if (odir) {
                        ret = dict_set_str (dict, "odir", odir);
                        if (ret) {
                                *op_errstr = gf_strdup ("Command failed. Please"
                                                        " check log file for"
                                                        " more details.");
                                gf_log (THIS->name, GF_LOG_ERROR,
                                        "Setting output directory to"
                                        "dictionary failed");
                                goto out;
                        }
                }

                if (filename) {
                        ret = dict_set_str (dict, "filename", filename);
                        if (ret) {
                                *op_errstr = gf_strdup ("Command failed. Please"
                                                        " check log file for"
                                                        " more  details.");
                                gf_log (THIS->name, GF_LOG_ERROR,
                                        "Setting filename to dictionary failed");
                                goto out;
                        }
                }
        }

 out:
        if (dict)
                *options = dict;

        if (ret && dict)
                dict_unref (dict);

        return ret;
}

int32_t
cli_cmd_inode_quota_parse (const char **words, int wordcount, dict_t **options)
{
        dict_t          *dict    = NULL;
        char            *volname = NULL;
        int              ret     = -1;

        GF_ASSERT (words);
        GF_ASSERT (options);

        dict = dict_new ();
        if (!dict) {
                gf_log ("cli", GF_LOG_ERROR, "dict_new failed");
                goto out;
        }

        if (wordcount != 4)
                goto out;

        volname = (char *)words[2];
        if (!volname) {
                ret = -1;
                goto out;
        }

        /* Validate the volume name here itself */
        if (cli_validate_volname (volname) < 0)
                goto out;

        ret = dict_set_str (dict, "volname", volname);
        if (ret < 0)
                goto out;

        if (strcmp (words[3], "enable") != 0) {
                cli_out ("Invalid quota option : %s", words[3]);
                ret = -1;
                goto out;
        }

        ret = dict_set_int32 (dict, "type",
                              GF_QUOTA_OPTION_TYPE_ENABLE_OBJECTS);
        if (ret < 0)
                goto out;

        *options = dict;
out:
        if (ret < 0) {
                if (dict)
                        dict_unref (dict);
        }

        return ret;
}

int32_t
cli_cmd_quota_parse (const char **words, int wordcount, dict_t **options)
{
        dict_t          *dict    = NULL;
        char            *volname = NULL;
        int              ret     = -1;
        int              i       = -1;
        char             key[20] = {0, };
        int64_t          value   = 0;
        gf_quota_type    type    = GF_QUOTA_OPTION_TYPE_NONE;
        char           *opwords[] = { "enable", "disable", "limit-usage",
                                      "remove", "list", "alert-time",
                                      "soft-timeout", "hard-timeout",
                                      "default-soft-limit", "limit-objects",
                                      "list-objects", "remove-objects", NULL};
        char            *w       = NULL;
        uint32_t         time    = 0;
        double           percent = 0;
        char            *end_ptr = NULL;
        int64_t          limit   = 0;

        GF_ASSERT (words);
        GF_ASSERT (options);

        dict = dict_new ();
        if (!dict) {
                gf_log ("cli", GF_LOG_ERROR, "dict_new failed");
                goto out;
        }

        if (wordcount < 4)
                goto out;

        volname = (char *)words[2];
        if (!volname) {
                ret = -1;
                goto out;
        }

        /* Validate the volume name here itself */
        if (cli_validate_volname (volname) < 0)
                goto out;

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
                type = GF_QUOTA_OPTION_TYPE_LIMIT_USAGE;
        } else if (strcmp (w, "limit-objects") == 0) {
                type = GF_QUOTA_OPTION_TYPE_LIMIT_OBJECTS;
        }

        if (type == GF_QUOTA_OPTION_TYPE_LIMIT_USAGE ||
            type == GF_QUOTA_OPTION_TYPE_LIMIT_OBJECTS) {

                if (wordcount < 6 || wordcount > 7) {
                        ret = -1;
                        goto out;
                }

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

                if (type == GF_QUOTA_OPTION_TYPE_LIMIT_USAGE) {
                        ret = gf_string2bytesize_int64 (words[5], &value);
                        if (ret != 0 || value <= 0) {
                                if (errno == ERANGE || value <= 0) {
                                        ret = -1;
                                        cli_err ("Please enter an integer "
                                                 "value in the range of "
                                                 "(1 - %"PRId64 ")",
                                                 INT64_MAX);
                                } else
                                        cli_err ("Please enter a correct "
                                                 "value");
                                goto out;
                        }
                } else {
                        errno = 0;
                        limit = strtol (words[5], &end_ptr, 10);
                        if (errno == ERANGE || errno == EINVAL || limit <= 0
                                            || strcmp (end_ptr, "") != 0) {
                                ret = -1;
                                cli_err ("Please enter an integer value in "
                                         "the range 1 - %"PRId64, INT64_MAX);
                                goto out;
                        }
                }

                ret  = dict_set_str (dict, "hard-limit", (char *) words[5]);
                if (ret < 0)
                        goto out;

                if (wordcount == 7) {

                        ret = gf_string2percent (words[6], &percent);
                        if (ret != 0 || percent > 100) {
                                ret = -1;
                                cli_err ("Please enter a correct value "
                                         "in the range of 0 to 100");
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

        if (strcmp (w, "remove-objects") == 0) {
                if (wordcount != 5) {
                        ret = -1;
                        goto out;
                }

                type = GF_QUOTA_OPTION_TYPE_REMOVE_OBJECTS;

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

                type = GF_QUOTA_OPTION_TYPE_LIST;

                if (words[4] && words[4][0] != '/') {
                        cli_err ("Please enter absolute path");
                        ret = -1;
                        goto out;
                }

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

        if (strcmp (w, "list-objects") == 0) {
                if (wordcount < 4) {
                        ret = -1;
                        goto out;
                }

                type = GF_QUOTA_OPTION_TYPE_LIST_OBJECTS;

                i = 4;
                while (i < wordcount) {
                        snprintf (key, 20, "path%d", i-4);

                        ret = dict_set_str (dict, key, (char *) words[i++]);
                        if (ret < 0) {
                                gf_log ("cli", GF_LOG_ERROR, "Failed to set "
                                        "quota patch in request dictionary");
                                goto out;
                        }
                }

                ret = dict_set_int32 (dict, "count", i - 4);
                if (ret < 0) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to set quota "
                                "limit count in request dictionary");
                        goto out;
                }

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
                        dict_unref (dict);
        }

        return ret;
}

static gf_boolean_t
cli_is_key_spl (char *key)
{
        return (strcmp (key, "group") == 0);
}

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

int32_t
cli_cmd_volume_set_parse (struct cli_state *state, const char **words,
                          int wordcount, dict_t **options, char **op_errstr)
{
        dict_t                 *dict      = NULL;
        char                   *volname   = NULL;
        int                     ret       = -1;
        int                     count     = 0;
        char                   *key       = NULL;
        char                   *value     = NULL;
        int                     i         = 0;
        char                    str[50]   = {0,};
        const char             *question  = NULL;
        gf_answer_t             answer    = GF_ANSWER_NO;

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

        if (!strcmp (volname, "all")) {
                ret = dict_set_str (dict, "globalname", "All");
                if (ret) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "dict set on global key failed.");
                        goto out;
                }

                ret = dict_set_int32 (dict, "hold_global_locks", _gf_true);
                if (ret) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "dict set on global key failed.");
                        goto out;
                }
        }

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

                if (fnmatch ("user.*", key, FNM_NOESCAPE) != 0) {
                        ret = gf_strip_whitespace (value, strlen (value));
                        if (ret == -1)
                                goto out;
                }

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

                if ((!strcmp (key, "cluster.enable-shared-storage")) &&
                    (!strcmp (value, "disable"))) {
                        question = "Disabling cluster.enable-shared-storage "
                                   "will delete the shared storage volume"
                                   "(gluster_shared_storage), which is used "
                                   "by snapshot scheduler, geo-replication "
                                   "and NFS-Ganesha. Do you still want to "
                                   "continue?";
                        answer = cli_cmd_get_confirmation (state, question);
                        if (GF_ANSWER_NO == answer) {
                                gf_log ("cli", GF_LOG_ERROR, "Operation "
                                        "cancelled, exiting");
                                *op_errstr = gf_strdup ("Aborted by user.");
                                ret = -1;
                                goto out;
                        }
                }
                if ((!strcmp (key, "nfs.disable")) &&
                            (!strcmp (value, "off"))) {
                        question = "Gluster NFS is being deprecated in favor "
                                   "of NFS-Ganesha Enter \"yes\" to continue "
                                   "using Gluster NFS";
                        answer = cli_cmd_get_confirmation (state, question);
                        if (GF_ANSWER_NO == answer) {
                                gf_log ("cli", GF_LOG_ERROR, "Operation "
                                        "cancelled, exiting");
                                *op_errstr = gf_strdup ("Aborted by user.");
                                ret = -1;
                                goto out;
                        }
                }
        }

        ret = dict_set_int32 (dict, "count", wordcount-3);

        if (ret)
                goto out;

        *options = dict;

out:
        if (ret && dict)
                dict_unref (dict);

        return ret;
}

int32_t
cli_cmd_volume_add_brick_parse (const char **words, int wordcount,
                                dict_t **options, int *ret_type)
{
        dict_t  *dict = NULL;
        char    *volname = NULL;
        int     ret = -1;
        int     brick_count = 0, brick_index = 0;
        char    *bricks = NULL;
        char    *opwords_cl[] = { "replica", "stripe", NULL };
        gf1_cluster_type type = GF_CLUSTER_TYPE_NONE;
        int     count = 1;
        int     arbiter_count = 0;
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
                if (words[index] && !strcmp (words[index], "arbiter")) {
                        arbiter_count = strtol (words[6], NULL, 0);
                        if (arbiter_count != 1 || count != 3) {
                                cli_err ("For arbiter configuration, replica "
                                         "count must be 3 and arbiter count "
                                         "must be 1. The 3rd brick of the "
                                         "replica will be the arbiter");
                                ret = -1;
                                goto out;
                        }
                        ret = dict_set_int32 (dict, "arbiter-count",
                                              arbiter_count);
                        if (ret)
                                goto out;
                        index = 7;
                }
        } else if ((strcmp (w, "stripe")) == 0) {
                type = GF_CLUSTER_TYPE_STRIPE;
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
        if (ret_type)
                *ret_type = type;

        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to parse add-brick CLI");
                if (dict)
                        dict_unref (dict);
        }

        return ret;
}

int32_t
cli_cmd_volume_tier_parse (const char **words, int wordcount,
                           dict_t **options)
{
        dict_t  *dict    = NULL;
        char    *volname = NULL;
        int      ret     = -1;
        int32_t  command = GF_OP_CMD_NONE;
        int32_t  is_force    = 0;

        GF_ASSERT (words);
        GF_ASSERT (options);

        dict = dict_new ();

        if (!dict)
                goto out;

        if (!(wordcount == 4 || wordcount == 5)) {
                gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                ret = -1;
                goto out;
        }

        volname = (char *)words[2];

        GF_ASSERT (volname);

        ret = cli_cmd_validate_volume (volname);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to validate volume name");
                goto out;
        }

        ret = dict_set_str (dict, "volname", volname);

        if (ret)
                goto out;

        volname = (char *)words[2];
        if (wordcount == 4) {
                if (!strcmp(words[3], "status"))
                        command = GF_DEFRAG_CMD_STATUS_TIER;
                else if (!strcmp(words[3], "start"))
                        command = GF_DEFRAG_CMD_START_TIER;
                else if (!strcmp(words[3], "stop"))
                        command = GF_DEFRAG_CMD_STOP_TIER;
                else {
                        ret = -1;
                        goto out;
                }
        } else if (wordcount == 5) {
                if ((!strcmp (words[3], "start")) &&
                    (!strcmp (words[4], "force"))) {
                        command = GF_DEFRAG_CMD_START_TIER;
                        is_force = 1;
                        ret = dict_set_int32 (dict, "force", is_force);
                        if (ret)
                                goto out;
                } else {
                        ret = -1;
                        goto out;
                }
        }

        ret = dict_set_int32 (dict, "rebalance-command", command);
        if (ret)
                goto out;

        *options = dict;
out:

        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to parse tier CLI");
                if (dict)
                        dict_unref (dict);
        }

        return ret;
}

int32_t
cli_cmd_volume_detach_tier_parse (const char **words, int wordcount,
                                  dict_t **options, int *question)
{
        int      ret = -1;
        char    *word = NULL;
        dict_t  *dict = NULL;
        int32_t  command = GF_OP_CMD_NONE;

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_str (dict, "volname", (char *)words[2]);
        if (ret)
                goto out;

        if (wordcount == 3 && !strcmp ((char *)words[2], "help")) {
                return -1;
        }

        if (wordcount != 4) {
                ret = -1;
                goto out;
        }

        word = (char *)words[3];

        ret = -1;

        if (!strcmp(word, "start")) {
                command = GF_DEFRAG_CMD_DETACH_START;
        } else if (!strcmp(word, "commit")) {
                *question = 1;
                command = GF_DEFRAG_CMD_DETACH_COMMIT;
        } else if (!strcmp(word, "force")) {
                *question = 1;
                command = GF_DEFRAG_CMD_DETACH_COMMIT_FORCE;
        } else if (!strcmp(word, "stop"))
                command = GF_DEFRAG_CMD_DETACH_STOP;
        else if (!strcmp(word, "status"))
                command = GF_DEFRAG_CMD_DETACH_STATUS;
        else
                goto out;

        ret = dict_set_int32 (dict, "command", command);
        if (ret)
                goto out;

        *options = dict;
        ret = 0;
out:
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to parse detach tier CLI");
                if (dict)
                        dict_unref (dict);
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

        if (wordcount < 5)
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
                if (wordcount < 6) {
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
                ret = -1;
                goto out;
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

        if (command != GF_OP_CMD_STATUS && command != GF_OP_CMD_STOP) {
                ret = dict_set_int32 (dict, "count", brick_count);
                if (ret)
                        goto out;
        }

        *options = dict;

out:
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to parse remove-brick CLI");
                if (dict)
                        dict_unref (dict);
        }

        GF_FREE (tmp_brick);
        GF_FREE (tmp_brick1);

        return ret;
}

int32_t
cli_cmd_brick_op_validate_bricks (const char **words, dict_t *dict,
				  int src, int dst)
{
        int             ret = -1;
        char            *delimiter  = NULL;

        if (validate_brick_name ((char *)words[src])) {
                cli_err ("wrong brick type: %s, use "
                         "<HOSTNAME>:<export-dir-abs-path>", words[3]);
                ret = -1;
                goto out;
        } else {
                delimiter = strrchr ((char *)words[src], '/');
                ret = gf_canonicalize_path (delimiter);
                if (ret)
                        goto out;
        }

        ret = dict_set_str (dict, "src-brick", (char *)words[src]);
        if (ret)
                goto out;

        if (dst == -1) {
                ret = 0;
                goto out;
        }

        if (validate_brick_name ((char *)words[dst])) {
                cli_err ("wrong brick type: %s, use "
                         "<HOSTNAME>:<export-dir-abs-path>", words[dst]);
                ret = -1;
                goto out;
        } else {
                delimiter = strrchr ((char *)words[dst], '/');
                ret = gf_canonicalize_path (delimiter);
                if (ret)
                        goto out;
        }

        ret = dict_set_str (dict, "dst-brick", (char *)words[dst]);
        if (ret)
                goto out;
        ret = 0;
out:
	return ret;
}

int32_t
cli_cmd_volume_reset_brick_parse (const char **words, int wordcount,
                                  dict_t **options)
{
        int                   ret        = -1;
        char                 *volname    = NULL;
        dict_t               *dict       = NULL;

        if (wordcount < 5 || wordcount > 7)
                goto out;

        dict = dict_new ();

        if (!dict)
                goto out;

        volname = (char *)words[2];

        ret = dict_set_str (dict, "volname", volname);
        if (ret)
                goto out;

        if (wordcount == 5) {
                if (strcmp (words[4], "start")) {
                        cli_err ("Invalid option '%s' for reset-brick. Please "
                                 "enter valid reset-brick command", words[4]);
                        ret = -1;
                        goto out;
                }

                ret = cli_cmd_brick_op_validate_bricks (words, dict, 3, -1);
                if (ret)
                        goto out;

                ret = dict_set_str (dict, "operation", "GF_RESET_OP_START");
                if (ret)
                        goto out;
        } else if (wordcount == 6) {
                if (strcmp (words[5], "commit")) {
                        cli_err ("Invalid option '%s' for reset-brick. Please "
                                 "enter valid reset-brick command", words[5]);
                        ret = -1;
                        goto out;
                }

                ret = cli_cmd_brick_op_validate_bricks (words, dict, 3, 4);
                if (ret)
                        goto out;

                ret = dict_set_str (dict, "operation", "GF_RESET_OP_COMMIT");
                if (ret)
                        goto out;
        } else if (wordcount == 7) {
                if (strcmp (words[5], "commit") || strcmp (words[6], "force")) {
                        cli_err ("Invalid option '%s %s' for reset-brick. Please "
                                 "enter valid reset-brick command",
                                  words[5], words[6]);
                        ret = -1;
                        goto out;
                }

                ret = cli_cmd_brick_op_validate_bricks (words, dict, 3, 4);
                if (ret)
                        goto out;

                ret = dict_set_str (dict, "operation",
                                    "GF_RESET_OP_COMMIT_FORCE");
                if (ret)
                        goto out;
        }

        *options = dict;

out:
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR,
                        "Unable to parse reset-brick CLI");
                if (dict)
                        dict_unref (dict);
        }

        return ret;
}

int32_t
cli_cmd_volume_replace_brick_parse (const char **words, int wordcount,
                                    dict_t **options)
{
        int                   ret        = -1;
        char                 *volname    = NULL;
        dict_t               *dict       = NULL;

        GF_ASSERT (words);
        GF_ASSERT (options);

        if (wordcount != 7) {
                ret = -1;
                goto out;
        }

        dict = dict_new ();

        if (!dict) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to allocate dictionary");
                goto out;
        }

        volname = (char *)words[2];

        GF_ASSERT (volname);

        ret = dict_set_str (dict, "volname", volname);
        if (ret)
                goto out;

        ret = cli_cmd_brick_op_validate_bricks (words, dict, 3, 4);
        if (ret)
                goto out;

        /* commit force option */
        if (strcmp ("commit", words[5]) || strcmp ("force", words[6])) {
                cli_err ("Invalid option '%s' '%s' for replace-brick. Please "
                         "enter valid replace-brick command", words[5],
                         words[6]);
                ret = -1;
                goto out;
        }

        ret = dict_set_str (dict, "operation", "GF_REPLACE_OP_COMMIT_FORCE");
        if (ret)
                goto out;

        *options = dict;

out:
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to parse reset-brick CLI");
                if (dict)
                        dict_unref (dict);
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
                dict_unref (dict);

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
                dict_unref (dict);

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
                dict_unref (dict);

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

        if (strcmp ("rotate", words[3]) == 0)
                volname = (char *)words[2];
        else if (strcmp ("rotate", words[2]) == 0)
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
                dict_unref (dict);

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
        char               *ret_chkpt = NULL;
        struct tm           checkpoint_time;
        char                chkpt_buf[20] = "";

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
                        snprintf (append_str, 300, "%" GF_PRI_SECOND,
                                  tv.tv_sec);
                } else if ((strcmp (words[cmdi + 1], "checkpoint") == 0) &&
                           (strcmp (append_str, "now") != 0)) {
                        memset(&checkpoint_time, 0, sizeof(struct tm));
                        ret_chkpt = strptime(append_str, "%Y-%m-%d %H:%M:%S",
                                             &checkpoint_time);

                        if (ret_chkpt == NULL) {
                                ret = -1;
                                cli_err ("Invalid Checkpoint label. Use format "
                                         "\"Y-m-d H:M:S\", Example: 2016-10-25 15:30:45");
                                goto out;
                        }
                        GF_FREE (append_str);
                        append_str = GF_CALLOC (1, 300, cli_mt_append_str);
                        if (!append_str) {
                                ret = -1;
                                goto out;
                        }
                        strftime (chkpt_buf, sizeof(chkpt_buf), "%s",
                                  &checkpoint_time);
                        snprintf (append_str, 300, "%s", chkpt_buf);
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

/* ssh_port_parse: Parses and validates when ssh_port is given.
 *                 ssh_index refers to index of ssh_port and
 *                 type refers to either push-pem or no-verify
 */

static int32_t
parse_ssh_port (const char **words, int wordcount, dict_t *dict,
                unsigned *cmdi, int ssh_index, char *type) {

        int        ret         = 0;
        char      *end_ptr     = NULL;
        int64_t    limit       = 0;

        if (!strcmp ((char *)words[ssh_index], "ssh-port")) {
                if (strcmp ((char *)words[ssh_index-1], "create")) {
                        ret = -1;
                        goto out;
                }
                (*cmdi)++;
                limit = strtol (words[ssh_index+1], &end_ptr, 10);
                if (errno == ERANGE || errno == EINVAL || limit <= 0
                                    || strcmp (end_ptr, "") != 0) {
                        ret = -1;
                        cli_err ("Please enter an integer value for ssh_port ");
                        goto out;
                }

                ret = dict_set_int32 (dict, "ssh_port", limit);
                if (ret)
                        goto out;
                (*cmdi)++;
        } else if (strcmp ((char *)words[ssh_index+1], "create")) {
                ret = -1;
                goto out;
        }

        ret = dict_set_int32 (dict, type, 1);
        if (ret)
                goto out;
        (*cmdi)++;

 out:
        return ret;
}

static int32_t
force_push_pem_no_verify_parse (const char **words, int wordcount,
                      dict_t *dict, unsigned *cmdi)
{
        int32_t            ret     = 0;

        if (!strcmp ((char *)words[wordcount-1], "force")) {
                if ((strcmp ((char *)words[wordcount-2], "start")) &&
                    (strcmp ((char *)words[wordcount-2], "stop")) &&
                    (strcmp ((char *)words[wordcount-2], "create")) &&
                    (strcmp ((char *)words[wordcount-2], "no-verify")) &&
                    (strcmp ((char *)words[wordcount-2], "push-pem")) &&
                    (strcmp ((char *)words[wordcount-2], "pause")) &&
                    (strcmp ((char *)words[wordcount-2], "resume"))) {
                        ret = -1;
                        goto out;
                }
                ret = dict_set_uint32 (dict, "force",
                                       _gf_true);
                if (ret)
                        goto out;
                (*cmdi)++;

                if (!strcmp ((char *)words[wordcount-2], "push-pem")) {
                        ret = parse_ssh_port (words, wordcount, dict, cmdi,
                                              wordcount-4, "push_pem");
                        if (ret)
                                goto out;
                } else if (!strcmp ((char *)words[wordcount-2], "no-verify")) {
                        ret = parse_ssh_port (words, wordcount, dict, cmdi,
                                              wordcount-4, "no_verify");
                        if (ret)
                                goto out;
                }
        } else if (!strcmp ((char *)words[wordcount-1], "push-pem")) {
                ret = parse_ssh_port (words, wordcount, dict, cmdi, wordcount-3,
                                      "push_pem");
                if (ret)
                        goto out;
        } else if (!strcmp ((char *)words[wordcount-1], "no-verify")) {
                ret = parse_ssh_port (words, wordcount, dict, cmdi, wordcount-3,
                                      "no_verify");
                if (ret)
                        goto out;
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
                                          "ssh-port", "no-verify", "push-pem",
                                          "detail", "pause", "resume", NULL };
        char               *w = NULL;
        char               *save_ptr   = NULL;
        char               *slave_temp = NULL;
        char               *token      = NULL;

        GF_ASSERT (words);
        GF_ASSERT (options);

        dict = dict_new ();
        if (!dict)
                goto out;

        /* new syntax:
         *
         * volume geo-replication $m $s create [[ssh-port n] [[no-verify] | [push-pem]]] [force]
         * volume geo-replication [$m [$s]] status [detail]
         * volume geo-replication [$m] $s config [[!]$opt [$val]]
         * volume geo-replication $m $s start|stop [force]
         * volume geo-replication $m $s delete [reset-sync-time]
         * volume geo-replication $m $s pause [force]
         * volume geo-replication $m $s resume [force]
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
        } else if (i <= 4) {
                if (strtail ("detail", (char *)words[wordcount-1])) {
                        cmdi = wordcount - 2;
                        if (i == 4)
                                masteri = 2;
                } else {
                        /* no $s, can only be status cmd
                         * (with either a single $m before it or nothing)
                         * -- these conditions imply that i <= 3 after
                         * the iteration and that i is the successor of
                         * the (0 or 1 length) sequence of $m-s.
                         */
                        cmdi = i;
                        if (i == 3)
                                masteri = 2;
                }
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
        } else if (strcmp (w, "pause") == 0) {
                type = GF_GSYNC_OPTION_TYPE_PAUSE;

                if (!masteri || !slavei)
                        goto out;
        } else if (strcmp (w, "resume") == 0) {
                type = GF_GSYNC_OPTION_TYPE_RESUME;

                if (!masteri || !slavei)
                        goto out;
        } else
                GF_ASSERT (!"opword mismatch");

        ret = force_push_pem_no_verify_parse (words, wordcount, dict, &cmdi);
        if (ret)
                goto out;

        if (strtail ("detail", (char *)words[wordcount-1])) {
                if (!strtail ("status", (char *)words[wordcount-2])) {
                        ret = -1;
                        goto out;
                }

                ret = dict_set_uint32 (dict, "status-detail", _gf_true);
                if (ret)
                        goto out;
                cmdi++;
        }

        if (type == GF_GSYNC_OPTION_TYPE_DELETE &&
            !strcmp ((char *)words[wordcount-1], "reset-sync-time")) {
                if (strcmp ((char *)words[wordcount-2], "delete")) {
                        ret = -1;
                        goto out;
                }
                ret = dict_set_uint32 (dict, "reset-sync-time", _gf_true);
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
        if (!ret && slavei) {
                /* If geo-rep is created with root user using the syntax
                 * gluster vol geo-rep <mastervol> root@<slavehost> ...
                 * pass down only <slavehost> else pass as it is.
                 */
                slave_temp = gf_strdup (words[slavei]);
                token = strtok_r (slave_temp, "@", &save_ptr);
                if (token && !strcmp (token, "root")) {
                        ret = dict_set_str (dict, "slave",
                                            (char *)words[slavei]+5);
                } else {
                        ret = dict_set_str (dict, "slave",
                                            (char *)words[slavei]);
                }
        }
        if (!ret)
                ret = dict_set_int32 (dict, "type", type);
        if (!ret && type == GF_GSYNC_OPTION_TYPE_CONFIG)
                ret = config_parse (words, wordcount, dict, cmdi, glob);

out:
        if (slave_temp)
                GF_FREE (slave_temp);
        if (ret) {
                if (dict)
                        dict_unref (dict);
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
                dict_unref (dict);
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
        int32_t  blk_size       = 0;
        int      count          = 0;
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
                        ret = dict_set_uint32 (dict, "blk-size",
                                                        (uint32_t)blk_size);
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
                dict_unref (dict);
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
                                } else if (!strcmp (words[3], "snapd")) {
                                        cmd |= GF_CLI_STATUS_SNAPD;
                                } else if (!strcmp (words[3], "tierd")) {
                                        cmd |= GF_CLI_STATUS_TIERD;
                                } else if (!strcmp (words[3], "bitd")) {
                                        cmd |= GF_CLI_STATUS_BITD;
                                } else if (!strcmp (words[3], "scrub")) {
                                        cmd |= GF_CLI_STATUS_SCRUB;
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
                } else if  (!strcmp (words[3], "snapd")) {
                        if (cmd == GF_CLI_STATUS_FD ||
                            cmd == GF_CLI_STATUS_CLIENTS ||
                            cmd == GF_CLI_STATUS_DETAIL ||
                            cmd == GF_CLI_STATUS_INODE) {
                                cli_err ("Detail/FD/Clients/Inode status not "
                                         "available for snap daemon");
                                ret = -1;
                                goto out;
                        }
                        cmd |= GF_CLI_STATUS_SNAPD;
                } else if  (!strcmp (words[3], "tierd")) {
                        if (cmd == GF_CLI_STATUS_FD ||
                            cmd == GF_CLI_STATUS_CLIENTS ||
                            cmd == GF_CLI_STATUS_DETAIL ||
                            cmd == GF_CLI_STATUS_INODE) {
                                cli_err ("Detail/FD/Clients/Inode status not "
                                         "available for tier daemon");
                                ret = -1;
                                goto out;
                        }
                        cmd |= GF_CLI_STATUS_TIERD;
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
                dict_unref (dict);

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
        char    option_str[_POSIX_HOST_NAME_MAX + 100] = {0,};
        char    *tmp = NULL;
        char    *ip_addr = NULL;
        char    *pid = NULL;

        if ((wordcount >= 5) && ((strcmp (words[3], "client")) == 0)) {
                tmp = gf_strdup(words[4]);
                if (!tmp) {
                        ret = -1;
                        goto out;
                }
                ip_addr = strtok(tmp, ":");
                pid = strtok(NULL, ":");
                if (valid_internet_address (ip_addr, _gf_true)
                   && pid && gf_valid_pid (pid, strlen(pid))) {
                        strncat (option_str, words[3], strlen (words[3]));
                        strncat (option_str, " ", 1);
                        strncat (option_str, ip_addr, strlen (ip_addr));
                        strncat (option_str, " ", 1);
                        strncat (option_str, pid, strlen (pid));
                        option_cnt = 3;
                } else {
                        ret = -1;
                        goto out;
                }
        } else {
                for (i = 3; i < wordcount; i++, option_cnt++) {
                        if (!cli_cmd_validate_dumpoption (words[i], &option)) {
                                ret = -1;
                                goto out;
                        }
                        strncat (option_str, option, strlen (option));
                        strncat (option_str, " ", 1);
                }
                if ((strstr (option_str, "nfs")) && strstr (option_str, "quotad")) {
                        ret = -1;
                        goto out;
                }
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
        GF_FREE (tmp);
        if (ret && dict)
                dict_unref (dict);
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

static int
set_hostname_path_in_dict (const char *token, dict_t *dict, int heal_op)
{
        char *hostname = NULL;
        char *path     = NULL;
        int   ret      = 0;

        ret = extract_hostname_path_from_token (token, &hostname, &path);
        if (ret)
                goto out;

        switch (heal_op) {
        case GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK:
                ret = dict_set_dynstr (dict, "heal-source-hostname",
                                       hostname);
                if (ret)
                        goto out;
                ret = dict_set_dynstr (dict, "heal-source-brickpath",
                                       path);
                break;
        case GF_SHD_OP_STATISTICS_HEAL_COUNT_PER_REPLICA:
                ret = dict_set_dynstr (dict, "per-replica-cmd-hostname",
                                       hostname);
                if (ret)
                        goto out;
                ret = dict_set_dynstr (dict, "per-replica-cmd-path",
                                       path);
                break;
        default:
                ret = -1;
                break;
        }

out:
        return ret;
}

static int
heal_command_type_get (const char *command)
{
        int     i = 0;
        /* subcommands are set as NULL */
        char    *heal_cmds[GF_SHD_OP_HEAL_DISABLE + 1] = {
                [GF_SHD_OP_INVALID]                            = NULL,
                [GF_SHD_OP_HEAL_INDEX]                         = NULL,
                [GF_SHD_OP_HEAL_FULL]                          = "full",
                [GF_SHD_OP_INDEX_SUMMARY]                      = "info",
                [GF_SHD_OP_HEALED_FILES]                       = NULL,
                [GF_SHD_OP_HEAL_FAILED_FILES]                  = NULL,
                [GF_SHD_OP_SPLIT_BRAIN_FILES]                  = NULL,
                [GF_SHD_OP_STATISTICS]                         = "statistics",
                [GF_SHD_OP_STATISTICS_HEAL_COUNT]              = NULL,
                [GF_SHD_OP_STATISTICS_HEAL_COUNT_PER_REPLICA]  = NULL,
                [GF_SHD_OP_SBRAIN_HEAL_FROM_BIGGER_FILE]       = NULL,
                [GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK]             = NULL,
                [GF_SHD_OP_HEAL_ENABLE]                        = "enable",
                [GF_SHD_OP_HEAL_DISABLE]                       = "disable",
        };

        for (i = 0; i <= GF_SHD_OP_HEAL_DISABLE; i++) {
                if (heal_cmds[i] && (strcmp (heal_cmds[i], command) == 0))
                        return i;
        }

        return GF_SHD_OP_INVALID;
}

int
cli_cmd_volume_heal_options_parse (const char **words, int wordcount,
                                   dict_t **options)
{
        int     ret = 0;
        dict_t  *dict = NULL;
        gf_xl_afr_op_t op = GF_SHD_OP_INVALID;

        dict = dict_new ();
        if (!dict)
                goto out;

        ret = dict_set_str (dict, "volname", (char *) words[2]);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "failed to set volname");
                goto out;
        }

        if (wordcount == 3) {
                ret = dict_set_int32 (dict, "heal-op", GF_SHD_OP_HEAL_INDEX);
                goto done;
        }

        if (wordcount == 4) {
                op = heal_command_type_get (words[3]);
                if (op == GF_SHD_OP_INVALID) {
                        ret = -1;
                        goto out;
                }

                ret = dict_set_int32 (dict, "heal-op", op);
                goto done;
        }

        if (wordcount == 5) {
                if (strcmp (words[3], "info") &&
                    strcmp (words[3], "statistics") &&
                    strcmp (words[3], "granular-entry-heal")) {
                        ret = -1;
                        goto out;
                }

                if (!strcmp (words[3], "info")) {
                        if (!strcmp (words[4], "healed")) {
                                ret = dict_set_int32 (dict, "heal-op",
                                                      GF_SHD_OP_HEALED_FILES);
                                goto done;
                        }
                        if (!strcmp (words[4], "heal-failed")) {
                                ret = dict_set_int32 (dict, "heal-op",
                                                   GF_SHD_OP_HEAL_FAILED_FILES);
                                goto done;
                        }
                        if (!strcmp (words[4], "split-brain")) {
                                ret = dict_set_int32 (dict, "heal-op",
                                                   GF_SHD_OP_SPLIT_BRAIN_FILES);
                                goto done;
                        }
                }

                if (!strcmp (words[3], "statistics")) {
                        if (!strcmp (words[4], "heal-count")) {
                                ret = dict_set_int32 (dict, "heal-op",
                                               GF_SHD_OP_STATISTICS_HEAL_COUNT);
                                goto done;
                        }
                }

                if (!strcmp (words[3], "granular-entry-heal")) {
                        if (!strcmp (words[4], "enable")) {
                                ret = dict_set_int32 (dict, "heal-op",
                                          GF_SHD_OP_GRANULAR_ENTRY_HEAL_ENABLE);
                                goto done;
                        } else if (!strcmp (words[4], "disable")) {
                                ret = dict_set_int32 (dict, "heal-op",
                                         GF_SHD_OP_GRANULAR_ENTRY_HEAL_DISABLE);
                                goto done;
                        }
                }

                ret = -1;
                goto out;
        }
        if (wordcount == 6) {
                if (strcmp (words[3], "split-brain")) {
                        ret = -1;
                        goto out;
                }
                if (!strcmp (words[4], "bigger-file")) {
                        ret = dict_set_int32 (dict, "heal-op",
                                        GF_SHD_OP_SBRAIN_HEAL_FROM_BIGGER_FILE);
                        if (ret)
                                goto out;
                        ret = dict_set_str (dict, "file", (char *)words[5]);
                        if (ret)
                                goto out;
                        goto done;
                }
                if (!strcmp (words[4], "latest-mtime")) {
                        ret = dict_set_int32 (dict, "heal-op",
                                       GF_SHD_OP_SBRAIN_HEAL_FROM_LATEST_MTIME);
                        if (ret)
                                goto out;
                        ret = dict_set_str (dict, "file", (char *)words[5]);
                        if (ret)
                                goto out;
                        goto done;
                }
                if (!strcmp (words[4], "source-brick")) {
                        ret = dict_set_int32 (dict, "heal-op",
                                              GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK);
                        if (ret)
                                goto out;
                        ret = set_hostname_path_in_dict (words[5], dict,
                                              GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK);
                        if (ret)
                                goto out;
                        goto done;
                }
                ret = -1;
                goto out;
        }
        if (wordcount == 7) {
                if (!strcmp (words[3], "statistics")
                    && !strcmp (words[4], "heal-count")
                    && !strcmp (words[5], "replica")) {

                        ret = dict_set_int32 (dict, "heal-op",
                                   GF_SHD_OP_STATISTICS_HEAL_COUNT_PER_REPLICA);
                        if (ret)
                                goto out;
                        ret = set_hostname_path_in_dict (words[6], dict,
                                   GF_SHD_OP_STATISTICS_HEAL_COUNT_PER_REPLICA);
                        if (ret)
                                goto out;
                        goto done;

                }
                if (!strcmp (words[3], "split-brain") &&
                    !strcmp (words[4], "source-brick")) {
                        ret = dict_set_int32 (dict, "heal-op",
                                              GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK);
                        ret = set_hostname_path_in_dict (words[5], dict,
                                              GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK);
                        if (ret)
                                goto out;
                        ret = dict_set_str (dict, "file",
                                            (char *) words[6]);
                        if (ret)
                                goto out;
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
cli_cmd_volume_old_tier_parse (const char **words, int wordcount,
                             dict_t **options)
{
        dict_t                 *dict = NULL;
        int                      ret = -1;
        char                *volname = NULL;
        gf_cli_defrag_type       cmd = 0;

        GF_ASSERT (words);
        GF_ASSERT (options);

        dict = dict_new ();
        if (!dict)
                goto out;

        if (wordcount != 4)
                goto out;

        if ((strcmp (words[1], "tier") == 0) &&
            (strcmp (words[3], "start") == 0)) {
                cmd = GF_DEFRAG_CMD_START_TIER;
        } else
                goto out;

        volname = (char *) words[2];

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
                dict_unref (dict);

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
                dict_unref (dict);

        return ret;
}

int32_t
cli_snap_create_desc_parse (dict_t *dict, const char **words,
                            size_t wordcount, int32_t desc_opt_loc)
{
        int32_t        ret      = -1;
        char          *desc     = NULL;
        int32_t        desc_len = 0;

        desc = GF_CALLOC (MAX_SNAP_DESCRIPTION_LEN + 1, sizeof(char),
                          gf_common_mt_char);
        if (!desc) {
                ret = -1;
                goto out;
        }


        if (strlen (words[desc_opt_loc]) >= MAX_SNAP_DESCRIPTION_LEN) {
                cli_out ("snapshot create: description truncated: "
                         "Description provided is longer than 1024 characters");
                desc_len = MAX_SNAP_DESCRIPTION_LEN;
        } else {
                desc_len = strlen (words[desc_opt_loc]);
        }

        strncpy (desc, words[desc_opt_loc], desc_len);
        desc[desc_len] = '\0';
        /* Calculating the size of the description as given by the user */

        ret = dict_set_dynstr (dict, "description", desc);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to save snap "
                        "description");
                goto out;
        }

        ret = 0;
out:
        if (ret && desc)
                GF_FREE (desc);

        return ret;
}

/* Function to check whether the Volume name is repeated */
int
cli_check_if_volname_repeated (const char **words, unsigned int start_index,
                           uint64_t cur_index) {
        uint64_t        i       =       -1;
        int             ret     =        0;

        GF_ASSERT (words);

        for (i = start_index ; i < cur_index ; i++) {
                if (strcmp (words[i], words[cur_index]) == 0) {
                        ret = -1;
                        goto out;
                }
        }
out:
        return ret;
}

/* snapshot clone <clonename> <snapname>
 * @arg-0, dict     : Request Dictionary to be sent to server side.
 * @arg-1, words    : Contains individual words of CLI command.
 * @arg-2, wordcount: Contains number of words present in the CLI command.
 *
 * return value : -1 on failure
 *                 0 on success
 */
int
cli_snap_clone_parse (dict_t *dict, const char **words, int wordcount) {
        uint64_t        i               =       0;
        int             ret             =       -1;
        char            *clonename      =       NULL;
        unsigned int    cmdi            =       2;
        /* cmdi is command index, here cmdi is "2" (gluster snapshot clone)*/

        GF_ASSERT (words);
        GF_ASSERT (dict);

        if (wordcount == cmdi + 1) {
                cli_err ("Invalid Syntax.");
                gf_log ("cli", GF_LOG_ERROR,
                        "Invalid number of  words for snap clone command");
                goto out;
        }

        if (strlen(words[cmdi]) >= GLUSTERD_MAX_SNAP_NAME) {
                cli_err ("snapshot clone: failed: clonename cannot exceed "
                         "255 characters.");
                gf_log ("cli", GF_LOG_ERROR, "Clone name too long");

                goto out;
        }

        clonename = (char *) words[cmdi];
        for (i = 0 ; i < strlen (clonename); i++) {
                /* Following volume name convention */
                if (!isalnum (clonename[i]) && (clonename[i] != '_'
                                           && (clonename[i] != '-'))) {
                        /* TODO : Is this message enough?? */
                        cli_err ("Clonename can contain only alphanumeric, "
                                 "\"-\" and \"_\" characters");
                        goto out;
                }
        }

        ret = dict_set_int32 (dict, "volcount", 1);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Could not save volcount");
                goto out;
        }

        ret = dict_set_str (dict, "clonename", (char *)words[cmdi]);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Could not save clone "
                        "name(%s)", (char *)words[cmdi]);
                goto out;
        }

        /* Filling snap name in the dictionary */
        ret = dict_set_str (dict, "snapname", (char *)words[cmdi+1]);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Could not "
                        "save snap name(%s)", (char *)words[cmdi+1]);
                goto out;
        }


        ret = 0;

out:
        return ret;
}


/* snapshot create <snapname> <vol-name(s)> [description <description>]
 *                                           [force]
 * @arg-0, dict     : Request Dictionary to be sent to server side.
 * @arg-1, words    : Contains individual words of CLI command.
 * @arg-2, wordcount: Contains number of words present in the CLI command.
 *
 * return value : -1 on failure
 *                 0 on success
 */
int
cli_snap_create_parse (dict_t *dict, const char **words, int wordcount) {
        uint64_t        i               =       0;
        int             ret             =       -1;
        uint64_t        volcount        =       0;
        char            key[PATH_MAX]   =       "";
        char            *snapname       =       NULL;
        unsigned int    cmdi            =       2;
        int             flags           =       0;
        /* cmdi is command index, here cmdi is "2" (gluster snapshot create)*/

        GF_ASSERT (words);
        GF_ASSERT (dict);

        if (wordcount <= cmdi + 1) {
                cli_err ("Invalid Syntax.");
                gf_log ("cli", GF_LOG_ERROR,
                        "Too less words for snap create command");
                goto out;
        }

        if (strlen(words[cmdi]) >= GLUSTERD_MAX_SNAP_NAME) {
                cli_err ("snapshot create: failed: snapname cannot exceed "
                         "255 characters.");
                gf_log ("cli", GF_LOG_ERROR, "Snapname too long");

                goto out;
        }

        snapname = (char *) words[cmdi];
        for (i = 0 ; i < strlen (snapname); i++) {
                /* Following volume name convention */
                if (!isalnum (snapname[i]) && (snapname[i] != '_'
                                           && (snapname[i] != '-'))) {
                        /* TODO : Is this message enough?? */
                        cli_err ("Snapname can contain only alphanumeric, "
                                 "\"-\" and \"_\" characters");
                        goto out;
                }
        }

        ret = dict_set_str (dict, "snapname", (char *)words[cmdi]);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Could not save snap "
                        "name(%s)", (char *)words[cmdi]);
                goto out;
        }

        /* Filling volume name in the dictionary */
        for (i = cmdi + 1 ; i < wordcount
                            && (strcmp (words[i], "description")) != 0
                            && (strcmp (words[i], "force") != 0)
                            && (strcmp (words[i], "no-timestamp") != 0);
                            i++) {
                volcount++;
                /* volume index starts from 1 */
                ret = snprintf (key, sizeof (key), "volname%"PRIu64, volcount);
                if (ret < 0) {
                        goto out;
                }

                ret = dict_set_str (dict, key, (char *)words[i]);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Could not "
                                "save volume name(%s)", (char *)words[i]);
                        goto out;
                }

                if (i >= cmdi + 2) {
                        ret = -1;
                        cli_err("Creating multiple volume snapshot is not "
                                "supported as of now");
                        goto out;
                }
                /* TODO : remove this above condition check once
                 * multiple volume snapshot is supported */
        }

        if (volcount == 0) {
                ret = -1;
                cli_err ("Please provide the volume name");
                gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                goto out;
        }

        ret = dict_set_int32 (dict, "volcount", volcount);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Could not save volcount");
                goto out;
        }

        /* Verify how we got out of "for" loop,
         * if it is by reaching wordcount limit then goto "out",
         * because we need not parse for "description","force" and
         * "no-timestamp" after this.
         */
        if (i == wordcount) {
                goto out;
        }

        if (strcmp (words[i], "no-timestamp") == 0) {
                ret = dict_set_str (dict, "no-timestamp", "true");
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Could not save "
                                "time-stamp option");
                }
                if (i == (wordcount-1))
                        goto out;
                i++;
        }

        if ((strcmp (words[i], "description")) == 0) {
                ++i;
                if (i > (wordcount - 1)) {
                        ret = -1;
                        cli_err ("Please provide a description");
                        gf_log ("cli", GF_LOG_ERROR,
                                "Description not provided");
                        goto out;
                }

                ret = cli_snap_create_desc_parse(dict, words, wordcount, i);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Could not save snap "
                                "description");
                        goto out;
                }

                if (i == (wordcount - 1))
                        goto out;
                i++;
                /* point the index to next word.
                 * As description might be follwed by force option.
                 * Before that, check if wordcount limit is reached
                 */
        }

        if (strcmp (words[i], "force") == 0) {
                flags = GF_CLI_FLAG_OP_FORCE;

        } else {
                ret = -1;
                cli_err ("Invalid Syntax.");
                gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                goto out;
        }

        /* Check if the command has anything after "force" keyword */
        if (++i < wordcount) {
                ret = -1;
                gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                goto out;
        }

        ret = 0;

out:
        if(ret == 0) {
                /*Adding force flag in either of the case i.e force set
                 * or unset*/
                ret = dict_set_int32 (dict, "flags", flags);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Could not save "
                                "snap force option");
                }
        }
        return ret;
}

/* snapshot list [volname]
 * @arg-0, dict     : Request Dictionary to be sent to server side.
 * @arg-1, words    : Contains individual words of CLI command.
 * @arg-2, wordcount: Contains number of words present in the CLI command.
 *
 * return value : -1 on failure
 *                 0 on success
 */
int
cli_snap_list_parse (dict_t *dict, const char **words, int wordcount) {
        int             ret     =       -1;

        GF_ASSERT (words);
        GF_ASSERT (dict);

        if (wordcount < 2 || wordcount > 3) {
                gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                goto out;
        }

        if (wordcount == 2) {
                ret = 0;
                goto out;
        }

        ret = dict_set_str (dict, "volname", (char *)words[2]);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR,
                        "Failed to save volname in dictionary");
                goto out;
        }
out:
        return ret;
}

/* snapshot info [(snapname |  volume <volname>)]
 * @arg-0, dict     : Request Dictionary to be sent to server side.
 * @arg-1, words    : Contains individual words of CLI command.
 * @arg-2, wordcount: Contains number of words present in the CLI command.
 *
 * return value : -1 on failure
 *                 0 on success
 */
int
cli_snap_info_parse (dict_t *dict, const char **words, int wordcount)
{

        int             ret             =       -1;
        int32_t         cmd             =       GF_SNAP_INFO_TYPE_ALL;
        unsigned int    cmdi            =        2;
        /* cmdi is command index, here cmdi is "2" (gluster snapshot info)*/

        GF_ASSERT (words);
        GF_ASSERT (dict);

        if (wordcount > 4 || wordcount < cmdi) {
                gf_log ("cli", GF_LOG_ERROR, "Invalid syntax");
                goto out;
        }

        if (wordcount == cmdi) {
                ret = 0;
                goto out;
        }

        /* If 3rd word is not "volume", then it must
         * be snapname.
         */
        if (strcmp (words[cmdi], "volume") != 0) {
                ret = dict_set_str (dict, "snapname",
                                   (char *)words[cmdi]);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Unable to save "
                                "snapname %s", words[cmdi]);
                        goto out;
                }

                /* Once snap name is parsed, if we encounter any other
                 * word then fail it. Invalid Syntax.
                 * example : snapshot info <snapname> word
                 */
                if ((cmdi + 1) != wordcount) {
                        ret = -1;
                        gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                        goto out;
                }

                cmd = GF_SNAP_INFO_TYPE_SNAP;
                ret = 0;
                goto out;
                /* No need to continue the parsing once we
                 * get the snapname
                 */
        }

        /* If 3rd word is "volume", then check if next word
         * is present. As, "snapshot info volume" is an
         * invalid command.
         */
        if ((cmdi + 1) == wordcount) {
                ret = -1;
                gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                goto out;
        }

        ret = dict_set_str (dict, "volname", (char *)words[wordcount - 1]);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Could not save "
                        "volume name %s", words[wordcount - 1]);
                goto out;
        }
        cmd = GF_SNAP_INFO_TYPE_VOL;
out:
        if (ret == 0) {
                ret = dict_set_int32 (dict, "sub-cmd", cmd);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Could not save "
                                "type of snapshot info");
                }
        }
        return ret;
}



/* snapshot restore <snapname>
 * @arg-0, dict     : Request Dictionary to be sent to server side.
 * @arg-1, words    : Contains individual words of CLI command.
 * @arg-2, wordcount: Contains number of words present in the CLI command.
 *
 * return value : -1 on failure
 *                 0 on success
 */
int
cli_snap_restore_parse (dict_t *dict, const char **words, int wordcount,
                        struct cli_state *state)
{

        int             ret             =       -1;
        const char      *question       =       NULL;
        gf_answer_t     answer          =       GF_ANSWER_NO;

        GF_ASSERT (words);
        GF_ASSERT (dict);

        if (wordcount != 3) {
                gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                goto out;
        }

        ret = dict_set_str (dict, "snapname", (char *)words[2]);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to save snap-name %s",
                        words[2]);
                goto out;
        }

        question = "Restore operation will replace the "
                   "original volume with the snapshotted volume. "
                   "Do you still want to continue?";

        answer = cli_cmd_get_confirmation (state, question);
        if (GF_ANSWER_NO == answer) {
                ret = 1;
                gf_log ("cli", GF_LOG_ERROR, "User cancelled a snapshot "
                        "restore operation for snap %s", (char *)words[2]);
                goto out;
        }
out:
        return ret;
}

/* snapshot activate <snapname> [force]
 * @arg-0, dict     : Request Dictionary to be sent to server side.
 * @arg-1, words    : Contains individual words of CLI command.
 * @arg-2, wordcount: Contains number of words present in the CLI command.
 *
 * return value : -1 on failure
 *                 0 on success
 */
int
cli_snap_activate_parse (dict_t *dict, const char **words, int wordcount)
{

        int ret = -1;
        int flags = 0;

        GF_ASSERT (words);
        GF_ASSERT (dict);

        if ((wordcount < 3) || (wordcount > 4)) {
                gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                goto out;
        }

        ret = dict_set_str (dict, "snapname", (char *)words[2]);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to save snap-name %s",
                        words[2]);
                goto out;
        }

        if (wordcount == 4) {
                if (!strcmp("force", (char *)words[3])) {
                        flags = GF_CLI_FLAG_OP_FORCE;
                } else {
                        gf_log ("cli", GF_LOG_ERROR, "Invalid option");
                        ret = -1;
                        goto out;
                }
        }
        ret = dict_set_int32 (dict, "flags", flags);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to save force option");
                goto out;
        }
out:
        return ret;
}

/* snapshot deactivate <snapname>
 * @arg-0, dict     : Request Dictionary to be sent to server side.
 * @arg-1, words    : Contains individual words of CLI command.
 * @arg-2, wordcount: Contains number of words present in the CLI command.
 *
 * return value : -1 on failure
 *                 0 on success
 *                 1 if user cancelled the request
 */
int
cli_snap_deactivate_parse (dict_t *dict, const char **words, int wordcount,
                        struct cli_state *state)
{

        int             ret             = -1;
        gf_answer_t     answer          = GF_ANSWER_NO;
        const char     *question        = "Deactivating snap will make its "
                                          "data inaccessible. Do you want to "
                                          "continue?";


        GF_ASSERT (words);
        GF_ASSERT (dict);

        if ((wordcount != 3)) {
                gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                goto out;
        }

        ret = dict_set_str (dict, "snapname", (char *)words[2]);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to save snap-name %s",
                        words[2]);
                goto out;
        }

        answer = cli_cmd_get_confirmation (state, question);
        if (GF_ANSWER_NO == answer) {
                ret = 1;
                gf_log ("cli", GF_LOG_DEBUG, "User cancelled "
                        "snapshot deactivate operation");
                goto out;
        }

out:
        return ret;
}

/* snapshot delete (all | snapname | volume <volname>)
 * @arg-0, dict     : Request Dictionary to be sent to server side.
 * @arg-1, words    : Contains individual words of CLI command.
 * @arg-2, wordcount: Contains number of words present in the CLI command.
 *
 * return value : -1 on failure
 *                 0 on success
 *                 1 if user cancel the operation
 */
int
cli_snap_delete_parse (dict_t *dict, const char **words, int wordcount,
                       struct cli_state *state) {

        int             ret             =       -1;
        const char      *question       =       NULL;
        int32_t         cmd             =       -1;
        unsigned int    cmdi            =       2;
        gf_answer_t     answer          =       GF_ANSWER_NO;

        GF_ASSERT (words);
        GF_ASSERT (dict);

        if (wordcount > 4 || wordcount <= cmdi) {
                gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                goto out;
        }

        question = "Deleting snap will erase all the information about "
                   "the snap. Do you still want to continue?";

        if (strcmp (words [cmdi], "all") == 0) {
                ret = 0;
                cmd = GF_SNAP_DELETE_TYPE_ALL;
        } else if (strcmp (words [cmdi], "volume") == 0) {
                if (++cmdi == wordcount) {
                        ret = -1;
                        gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                        goto out;
                }

                ret = dict_set_str (dict, "volname",
                                    (char *)words[cmdi]);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Could not save "
                                "volume name %s", words[wordcount - 1]);
                        goto out;
                }
                cmd = GF_SNAP_DELETE_TYPE_VOL;
        } else {
                ret = dict_set_str (dict, "snapname", (char *)words[cmdi]);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Unable to save "
                                "snapname %s", words[2]);
                        goto out;
                }
                cmd = GF_SNAP_DELETE_TYPE_SNAP;
        }

        if ((cmdi + 1) != wordcount) {
                ret = -1;
                gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                goto out;
        }

        if (cmd == GF_SNAP_DELETE_TYPE_SNAP) {
                answer = cli_cmd_get_confirmation (state, question);
                if (GF_ANSWER_NO == answer) {
                        ret = 1;
                        gf_log ("cli", GF_LOG_DEBUG, "User cancelled "
                                "snapshot delete operation for snap %s",
                                (char *)words[2]);
                        goto out;
                }
        }

        ret = dict_set_int32 (dict, "sub-cmd", cmd);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Could not save "
                        "type of snapshot delete");
        }
out:
        return ret;
}

/* snapshot status [(snapname | volume <volname>)]
 * @arg-0, dict     : Request Dictionary to be sent to server side.
 * @arg-1, words    : Contains individual words of CLI command.
 * @arg-2, wordcount: Contains number of words present in the CLI command.
 *
 * return value : -1 on failure
 *                 0 on success
 */
int
cli_snap_status_parse (dict_t *dict, const char **words, int wordcount)
{

        int             ret  =        -1;
        int32_t         cmd  =       GF_SNAP_STATUS_TYPE_ALL;
        unsigned int    cmdi =        2;
        /* cmdi is command index, here cmdi is "2" (gluster snapshot status)*/

        GF_ASSERT (words);
        GF_ASSERT (dict);

        if (wordcount > 4 || wordcount < cmdi) {
                gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                goto out;
        }

        if (wordcount == cmdi) {
                ret = 0;
                goto out;
        }

        /* if 3rd word is not "volume", then it must be "snapname"
        */
        if (strcmp (words[cmdi], "volume") != 0) {
                ret = dict_set_str (dict, "snapname",
                                   (char *)words[cmdi]);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Count not save "
                                "snap name %s", words[cmdi]);
                        goto out;
                }

                if ((cmdi + 1) != wordcount) {
                        ret = -1;
                        gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                        goto out;
                }

                ret = 0;
                cmd = GF_SNAP_STATUS_TYPE_SNAP;
                goto out;
        }

        /* If 3rd word is "volume", then check if next word is present.
         * As, "snapshot info volume" is an invalid command
         */
        if ((cmdi + 1) == wordcount) {
                ret = -1;
                gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                goto out;
        }

        ret = dict_set_str (dict, "volname", (char *)words [wordcount - 1]);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Count not save "
                        "volume name %s", words[wordcount - 1]);
                goto out;
        }
        cmd = GF_SNAP_STATUS_TYPE_VOL;

out:
        if (ret == 0) {
                ret = dict_set_int32 (dict, "sub-cmd", cmd);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Could not save cmd "
                                "of snapshot status");
                }
        }

        return ret;
}


/* return value:
 *      -1  in case of failure.
 *       0  in case of success.
 */
int32_t
cli_snap_config_limit_parse (const char **words, dict_t *dict,
                             unsigned int wordcount, unsigned int index,
                             char *key)
{
        int             ret             = -1;
        int             limit           = 0;
        char            *end_ptr        = NULL;

        GF_ASSERT (words);
        GF_ASSERT (dict);
        GF_ASSERT (key);

        if (index >= wordcount) {
                ret = -1;
                cli_err ("Please provide a value for %s.", key);
                gf_log ("cli", GF_LOG_ERROR, "Value not provided for %s", key);
                goto out;
        }

        limit = strtol (words[index], &end_ptr, 10);

        if (limit <= 0 || strcmp (end_ptr, "") != 0) {
                ret = -1;
                cli_err("Please enter an integer value "
                        "greater than zero for %s", key);
                goto out;
        }

        ret = dict_set_int32 (dict, key, limit);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Could not set "
                        "%s in dictionary", key);
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (dict, "globalname", "All");
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Could not set global key");
                goto out;
        }
        ret = dict_set_int32 (dict, "hold_global_locks", _gf_true);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Could not set global locks");
                goto out;
        }

out:
        return ret;
}

/* function cli_snap_config_parse
 * Config Syntax : gluster snapshot config [volname]
 *                                         [snap-max-hard-limit <count>]
 *                                         [snap-max-soft-limit <count>]
 *
   return value: <0  on failure
                  1  if user cancels the operation, or limit value is out of
                                                                      range
                  0  on success

  NOTE : snap-max-soft-limit can only be set for system.
*/
int32_t
cli_snap_config_parse (const char **words, int wordcount, dict_t *dict,
                       struct cli_state *state)
{
        int                            ret                 = -1;
        gf_answer_t                    answer              = GF_ANSWER_NO;
        gf_boolean_t                   vol_presence        = _gf_false;
        struct snap_config_opt_vals_  *conf_vals           = NULL;
        int8_t                         hard_limit          = 0;
        int8_t                         soft_limit          = 0;
        int8_t                         config_type         = -1;
        const char                    *question            = NULL;
        unsigned int                   cmdi                = 2;
        /* cmdi is command index, here cmdi is "2" (gluster snapshot config)*/

        GF_ASSERT (words);
        GF_ASSERT (dict);
        GF_ASSERT (state);

        if ((wordcount < 2) || (wordcount > 7)) {
                gf_log ("cli", GF_LOG_ERROR,
                        "Invalid wordcount(%d)", wordcount);
                goto out;
        }

        if (wordcount == 2) {
                config_type = GF_SNAP_CONFIG_DISPLAY;
                ret = 0;
                goto set;
        }

        /* auto-delete cannot be a volume name */
        /* Check whether the 3rd word is volname */
        if (strcmp (words[cmdi], "snap-max-hard-limit") != 0
             && strcmp (words[cmdi], "snap-max-soft-limit") != 0
             && strcmp (words[cmdi], "auto-delete") != 0
             && strcmp (words[cmdi], "activate-on-create") != 0) {
                ret = dict_set_str (dict, "volname", (char *)words[cmdi]);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to set volname");
                        goto out;
                }
                cmdi++;
                vol_presence = _gf_true;

                if (cmdi == wordcount) {
                        config_type = GF_SNAP_CONFIG_DISPLAY;
                        ret = 0;
                        goto set;
                }
        }

        config_type = GF_SNAP_CONFIG_TYPE_SET;

        if (strcmp (words[cmdi], "snap-max-hard-limit") == 0) {
                ret = cli_snap_config_limit_parse (words, dict, wordcount,
                                                ++cmdi, "snap-max-hard-limit");
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to parse snap "
                                "config hard limit");
                        goto out;
                }
                hard_limit = 1;

                if (++cmdi == wordcount) {
                        ret = 0;
                        goto set;
                }
        }

        if (strcmp (words[cmdi], "snap-max-soft-limit") == 0) {
                if (vol_presence == 1) {
                        ret = -1;
                        cli_err ("Soft limit cannot be set to individual "
                                  "volumes.");
                        gf_log ("cli", GF_LOG_ERROR, "Soft limit cannot be "
                                "set to volumes");
                        goto out;
                }

                ret = cli_snap_config_limit_parse (words, dict, wordcount,
                                                ++cmdi, "snap-max-soft-limit");
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to parse snap "
                                "config soft limit");
                        goto out;
                }

                if (++cmdi != wordcount) {
                        ret = -1;
                        gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                        goto out;
                }
                soft_limit = 1;
        }

        if (hard_limit || soft_limit)
                goto set;

        if (strcmp(words[cmdi], "auto-delete") == 0) {
                if (vol_presence == 1) {
                        ret = -1;
                        cli_err ("As of now, auto-delete option cannot be set "
                                "to volumes");
                        gf_log ("cli", GF_LOG_ERROR, "auto-delete option "
                                "cannot be set to volumes");
                        goto out;
                }

                if (++cmdi >= wordcount) {
                        ret = -1;
                        gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                        goto out;
                }

                ret = dict_set_str (dict, "auto-delete", (char *)words[cmdi]);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to set "
                                "value of auto-delete in request "
                                "dictionary");
                        goto out;
                }

                if (++cmdi != wordcount) {
                        ret = -1;
                        gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                        goto out;
                }
        } else if (strcmp(words[cmdi], "activate-on-create") == 0) {
                if (vol_presence == 1) {
                        ret = -1;
                        cli_err ("As of now, activate-on-create option "
                                 "cannot be set to volumes");
                        gf_log ("cli", GF_LOG_ERROR, "activate-on-create "
                                "option cannot be set to volumes");
                        goto out;
                }

                if (++cmdi >= wordcount) {
                        ret = -1;
                        gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                        goto out;
                }

                ret = dict_set_str (dict, "snap-activate-on-create",
                                    (char *)words[cmdi]);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to set value "
                                "of activate-on-create in request dictionary");
                        goto out;
                }

                if (++cmdi != wordcount) {
                        ret = -1;
                        gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                        goto out;
                }
        } else {
                ret = -1;
                gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                goto out;
        }

        ret = 0; /* Success */

set:
        ret = dict_set_int32 (dict, "config-command", config_type);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to set "
                        "config-command");
                goto out;
        }

        if (config_type == GF_SNAP_CONFIG_TYPE_SET &&
           (hard_limit || soft_limit)) {
                conf_vals = snap_confopt_vals;
                if (hard_limit && soft_limit) {
                        question = conf_vals[GF_SNAP_CONFIG_SET_BOTH].question;
                } else if (soft_limit) {
                        question = conf_vals[GF_SNAP_CONFIG_SET_SOFT].question;
                } else if (hard_limit) {
                        question = conf_vals[GF_SNAP_CONFIG_SET_HARD].question;
                }

                answer = cli_cmd_get_confirmation (state, question);
                if (GF_ANSWER_NO == answer) {
                        ret = 1;
                        gf_log ("cli", GF_LOG_DEBUG, "User cancelled "
                        "snapshot config operation");
                }
        }

out:
        return ret;
}

int
validate_op_name (const char *op, const char *opname, char **opwords) {
        int     ret     =       -1;
        int     i       =       0;

        GF_ASSERT (opname);
        GF_ASSERT (opwords);

        for (i = 0 ; opwords[i] != NULL; i++) {
                if (strcmp (opwords[i], opname) == 0) {
                        cli_out ("\"%s\" cannot be a %s", opname, op);
                        goto out;
                }
        }
        ret = 0;
out:
        return ret;
}

int32_t
cli_cmd_snapshot_parse (const char **words, int wordcount, dict_t **options,
                        struct cli_state *state)
{
        int32_t            ret        = -1;
        dict_t             *dict      = NULL;
        gf1_cli_snapshot   type       = GF_SNAP_OPTION_TYPE_NONE;
        char               *w         = NULL;
        char               *opwords[] = {"create", "delete", "restore",
                                        "activate", "deactivate", "list",
                                        "status", "config", "info", "clone",
                                        NULL};
        char               *invalid_snapnames[] = {"description", "force",
                                                  "volume", "all", NULL};
        char               *invalid_volnames[]  = {"volume", "type",
                                                   "subvolumes", "option",
                                                   "end-volume", "all",
                                                   "volume_not_in_ring",
                                                   "description", "force",
                                                   "snap-max-hard-limit",
                                                   "snap-max-soft-limit",
                                                   "auto-delete",
                                                   "activate-on-create", NULL};

        GF_ASSERT (words);
        GF_ASSERT (options);
        GF_ASSERT (state);

        dict = dict_new ();
        if (!dict)
                goto out;

        /* Lowest wordcount possible */
        if (wordcount < 2) {
                gf_log ("", GF_LOG_ERROR,
                        "Invalid command: Not enough arguments");
                goto out;
        }

        w = str_getunamb (words[1], opwords);
        if (!w) {
                /* Checks if the operation is a valid operation */
                gf_log ("", GF_LOG_ERROR, "Opword Mismatch");
                goto out;
        }

        if (!strcmp (w, "create")) {
                type = GF_SNAP_OPTION_TYPE_CREATE;
        } else if (!strcmp (w, "list")) {
                type = GF_SNAP_OPTION_TYPE_LIST;
        } else if (!strcmp (w, "info")) {
                type = GF_SNAP_OPTION_TYPE_INFO;
        } else if (!strcmp (w, "delete")) {
                type = GF_SNAP_OPTION_TYPE_DELETE;
        } else if (!strcmp (w, "config")) {
                type = GF_SNAP_OPTION_TYPE_CONFIG;
        } else if (!strcmp (w, "restore")) {
                type = GF_SNAP_OPTION_TYPE_RESTORE;
        } else if (!strcmp (w, "status")) {
                type = GF_SNAP_OPTION_TYPE_STATUS;
        } else if (!strcmp (w, "activate")) {
                type = GF_SNAP_OPTION_TYPE_ACTIVATE;
        } else if (!strcmp (w, "deactivate")) {
                type = GF_SNAP_OPTION_TYPE_DEACTIVATE;
        } else if (!strcmp(w, "clone")) {
                type = GF_SNAP_OPTION_TYPE_CLONE;
        }

        if (type != GF_SNAP_OPTION_TYPE_CONFIG &&
            type != GF_SNAP_OPTION_TYPE_STATUS) {
                ret = dict_set_int32 (dict, "hold_snap_locks", _gf_true);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "Unable to set hold-snap-locks value "
                                "as _gf_true");
                        goto out;
                }
        }

        /* Following commands does not require volume locks  */
        if (type == GF_SNAP_OPTION_TYPE_STATUS ||
            type == GF_SNAP_OPTION_TYPE_ACTIVATE ||
            type == GF_SNAP_OPTION_TYPE_DEACTIVATE) {
                ret = dict_set_int32 (dict, "hold_vol_locks", _gf_false);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Setting volume lock "
                                "flag failed");
                        goto out;
                }
        }

        /* Check which op is intended */
        switch (type) {
        case GF_SNAP_OPTION_TYPE_CREATE:
                /* Syntax :
                 * gluster snapshot create <snapname> <vol-name(s)>
                 *                         [no-timestamp]
                 *                         [description <description>]
                 *                         [force]
                 */
                /* In cases where the snapname is not given then
                 * parsing fails & snapname cannot be "description",
                 * "force" and "volume", that check is made here
                 */
                if (wordcount == 2){
                        ret = -1;
                        gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                        goto out;
                }

                ret = validate_op_name ("snapname", words[2],
                                        invalid_snapnames);
                if (ret) {
                        goto out;
                }

                ret = cli_snap_create_parse (dict, words, wordcount);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "create command parsing failed.");
                        goto out;
                }
                break;

        case GF_SNAP_OPTION_TYPE_CLONE:
                /* Syntax :
                 * gluster snapshot clone <clonename> <snapname>
                 */
                /* In cases where the clonename is not given then
                 * parsing fails & snapname cannot be "description",
                 * "force" and "volume", that check is made here
                 */
                if (wordcount == 2) {
                        ret = -1;
                        gf_log ("cli", GF_LOG_ERROR, "Invalid Syntax");
                        goto out;
                }

                ret = validate_op_name ("clonename", words[2],
                                        invalid_volnames);
                if (ret) {
                        goto out;
                }

                ret = cli_snap_clone_parse (dict, words, wordcount);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "clone command parsing failed.");
                        goto out;
                }
                break;


        case GF_SNAP_OPTION_TYPE_INFO:
                /* Syntax :
                 * gluster snapshot info [(snapname] | [vol <volname>)]
                 */
                ret = cli_snap_info_parse (dict, words, wordcount);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to parse "
                                "snapshot info command");
                        goto out;
                }
                break;

        case GF_SNAP_OPTION_TYPE_LIST:
                /* Syntax :
                 * gluster snaphsot list [volname]
                 */

                ret = cli_snap_list_parse (dict, words, wordcount);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to parse "
                                "snapshot list command");
                        goto out;
                }
                break;

        case GF_SNAP_OPTION_TYPE_DELETE:
                /* Syntax :
                 * snapshot delete (all | snapname | volume <volname>)
                 */
                ret = cli_snap_delete_parse (dict, words, wordcount, state);
                if (ret) {
                        /* A positive ret value means user cancelled
                        * the command */
                        if (ret < 0) {
                                gf_log ("cli", GF_LOG_ERROR, "Failed to parse "
                                        "snapshot delete command");
                        }
                        goto out;
                }
                break;

        case GF_SNAP_OPTION_TYPE_CONFIG:
                /* snapshot config [volname]  [snap-max-hard-limit <count>]
                 *                            [snap-max-soft-limit <percent>] */
                ret = cli_snap_config_parse (words, wordcount, dict, state);
                if (ret) {
                        if (ret < 0)
                                gf_log ("cli", GF_LOG_ERROR,
                                        "config command parsing failed.");
                        goto out;
                }

                ret = dict_set_int32 (dict, "type", GF_SNAP_OPTION_TYPE_CONFIG);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Unable to set "
                                "config type");
                        ret = -1;
                        goto out;
                }
                break;

        case GF_SNAP_OPTION_TYPE_STATUS:
                {
                        /* Syntax :
                         * gluster snapshot status [(snapname |
                         *                         volume <volname>)]
                         */
                        ret = cli_snap_status_parse (dict, words, wordcount);
                        if (ret) {
                                gf_log ("cli", GF_LOG_ERROR, "Failed to parse "
                                        "snapshot status command");
                                goto out;
                        }
                        break;
                }

        case GF_SNAP_OPTION_TYPE_RESTORE:
                /* Syntax:
                 * snapshot restore <snapname>
                 */
                ret = cli_snap_restore_parse (dict, words, wordcount, state);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to parse "
                                "restore command");
                        goto out;
                }
                break;

                case GF_SNAP_OPTION_TYPE_ACTIVATE:
                        /* Syntax:
                        * snapshot activate <snapname> [force]
                        */
                        ret = cli_snap_activate_parse (dict, words, wordcount);
                        if (ret) {
                                gf_log ("cli", GF_LOG_ERROR, "Failed to parse "
                                        "start command");
                                goto out;
                        }
                        break;
                case GF_SNAP_OPTION_TYPE_DEACTIVATE:
                        /* Syntax:
                        * snapshot deactivate <snapname>
                        */
                        ret = cli_snap_deactivate_parse (dict, words, wordcount,
                                state);
                        if (ret) {
                                /* A positive ret value means user cancelled
                                 * the command */
                                if (ret < 0) {
                                        gf_log ("cli", GF_LOG_ERROR,
                                                "Failed to parse deactivate "
                                                "command");
                                }
                                goto out;
                        }
                        break;

        default:
                gf_log ("", GF_LOG_ERROR, "Opword Mismatch");
                goto out;
        }

        ret = dict_set_int32 (dict, "type", type);
        if (ret) {
                gf_log ("", GF_LOG_ERROR,
                        "Failed to set type.");
                goto out;
        }
        /* If you got so far, input is valid */
        ret = 0;
out:
        if (ret) {
                if (dict)
                        dict_unref (dict);
        } else
                *options = dict;

        return ret;
}

int
cli_cmd_validate_volume (char *volname)
{
        int       i        =  0;
        int       ret      = -1;


        if (volname[0] == '-')
                return ret;

        if (!strcmp (volname, "all")) {
                cli_err ("\"all\" cannot be the name of a volume.");
                return ret;
        }

        if (strchr (volname, '/')) {
                cli_err ("Volume name should not contain \"/\" character.");
                return ret;
        }

        if (strlen (volname) > GD_VOLUME_NAME_MAX) {
                cli_err ("Volname can not exceed %d characters.",
                        GD_VOLUME_NAME_MAX);
                return ret;
        }

        for (i = 0; i < strlen (volname); i++)
                if (!isalnum (volname[i]) && (volname[i] != '_') &&
                    (volname[i] != '-')) {
                        cli_err ("Volume name should not contain \"%c\""
                                 " character.\nVolume names can only"
                                 "contain alphanumeric, '-' and '_' "
                                 "characters.", volname[i]);
                        return ret;
                }

        ret = 0;

        return ret;
}

int32_t
cli_cmd_bitrot_parse (const char **words, int wordcount, dict_t **options)
{
        int32_t            ret                    = -1;
        char               *w                     = NULL;
        char               *volname               = NULL;
        char               *opwords[]             = {"enable", "disable",
                                                     "scrub-throttle",
                                                     "scrub-frequency", "scrub",
                                                     "signing-time", NULL};
        char               *scrub_throt_values[]  = {"lazy", "normal",
                                                     "aggressive", NULL};
        char               *scrub_freq_values[]   = {"hourly",
                                                     "daily", "weekly",
                                                     "biweekly", "monthly",
                                                     "minute",  NULL};
        char               *scrub_values[]        = {"pause", "resume",
                                                     "status", "ondemand",
                                                     NULL};
        dict_t             *dict                  = NULL;
        gf_bitrot_type     type                   = GF_BITROT_OPTION_TYPE_NONE;
        int32_t            expiry_time            = 0;

        GF_ASSERT (words);
        GF_ASSERT (options);

        dict = dict_new ();
        if (!dict)
                goto out;

        if (wordcount < 4 || wordcount > 5) {
                gf_log ("cli", GF_LOG_ERROR, "Invalid syntax");
                goto out;
        }

        volname = (char *)words[2];
        if (!volname) {
                ret = -1;
                goto out;
        }

        ret = cli_cmd_validate_volume (volname);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to validate volume name");
                goto out;
        }

        ret = dict_set_str (dict, "volname", volname);
        if (ret) {
                cli_out ("Failed to set volume name in dictionary ");
                goto out;
        }

        w = str_getunamb (words[3], opwords);
        if (!w) {
                cli_out ("Invalid bit rot option : %s", words[3]);
                ret = -1;
                goto out;
        }

        if (strcmp (w, "enable") == 0) {
                if (wordcount == 4) {
                        type = GF_BITROT_OPTION_TYPE_ENABLE;
                        ret = 0;
                        goto set_type;
                } else {
                        ret = -1;
                        goto out;
                }
        }

        if (strcmp (w, "disable") == 0) {
                if (wordcount == 4) {
                        type = GF_BITROT_OPTION_TYPE_DISABLE;
                        ret = 0;
                        goto set_type;
                } else {
                        ret = -1;
                        goto out;
                }
        }

        if (!strcmp (w, "scrub-throttle")) {
                if (!words[4]) {
                        cli_err ("Missing scrub-throttle value for bitrot "
                                 "option");
                        ret = -1;
                        goto out;
                } else {
                        w = str_getunamb (words[4], scrub_throt_values);
                        if (!w) {
                                cli_err ("Invalid scrub-throttle option for "
                                         "bitrot");
                                ret = -1;
                                goto out;
                        } else {
                                type = GF_BITROT_OPTION_TYPE_SCRUB_THROTTLE;
                                ret =  dict_set_str (dict,
                                                     "scrub-throttle-value",
                                                     (char *) words[4]);
                                if (ret) {
                                        cli_out ("Failed to set scrub-throttle "
                                                 "value in the dict");
                                        goto out;
                                }
                                goto set_type;
                        }
                }
        }

        if (!strcmp (words[3], "scrub-frequency")) {
                if (!words[4]) {
                        cli_err ("Missing scrub-frequency value");
                        ret = -1;
                        goto out;
                } else {
                        w = str_getunamb (words[4], scrub_freq_values);
                        if (!w) {
                                cli_err ("Invalid frequency option for bitrot");
                                ret = -1;
                                goto out;
                        } else {
                                type = GF_BITROT_OPTION_TYPE_SCRUB_FREQ;
                                ret = dict_set_str (dict,
                                                    "scrub-frequency-value",
                                                    (char *) words[4]);
                                if (ret) {
                                        cli_out ("Failed to set dict for "
                                                 "bitrot");
                                        goto out;
                                }
                                goto set_type;
                        }
                }
        }

        if (!strcmp (words[3], "scrub")) {
                if (!words[4]) {
                        cli_err ("Missing scrub value for bitrot option");
                        ret = -1;
                        goto out;
                } else {
                        w = str_getunamb (words[4], scrub_values);
                        if (!w) {
                                cli_err ("Invalid scrub option for bitrot");
                                ret = -1;
                                goto out;
                        } else {
                                if (strcmp (words[4], "status") == 0) {
                                        type = GF_BITROT_CMD_SCRUB_STATUS;
                                } else if (strcmp (words[4], "ondemand") == 0) {
                                        type = GF_BITROT_CMD_SCRUB_ONDEMAND;
                                } else {
                                        type = GF_BITROT_OPTION_TYPE_SCRUB;
                                }
                                ret =  dict_set_str (dict, "scrub-value",
                                                    (char *) words[4]);
                                if (ret) {
                                        cli_out ("Failed to set dict for "
                                                 "bitrot");
                                        goto out;
                                }
                                goto set_type;
                        }
                }
        }

        if (!strcmp (words[3], "signing-time")) {
                if (!words[4]) {
                        cli_err ("Missing signing-time value for bitrot "
                                 "option");
                        ret = -1;
                        goto out;
                } else {
                        type = GF_BITROT_OPTION_TYPE_EXPIRY_TIME;

                        expiry_time = strtol (words[4], NULL, 0);
                        if (expiry_time < 1) {
                                cli_err ("Expiry time  value should not be less"
                                         " than 1");
                                ret = -1;
                                goto out;
                        }

                        ret = dict_set_uint32 (dict, "expiry-time",
                                               (unsigned int) expiry_time);
                        if (ret) {
                                cli_out ("Failed to set dict for bitrot");
                                goto out;
                        }
                        goto set_type;
                }
        } else {
                cli_err ("Invalid option %s for bitrot. Please enter valid "
                         "bitrot option", words[3]);
                ret = -1;
                goto out;
        }

set_type:
        ret = dict_set_int32 (dict, "type", type);
        if (ret < 0)
                goto out;

        *options = dict;

out:
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to parse bitrot command");
                if (dict)
                        dict_unref (dict);
        }

        return ret;
}
