/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "glusterd-volgen.h"
#include "glusterd-utils.h"

#if USE_GFDB  /* no GFDB means tiering is disabled */

static int
get_tier_freq_threshold (glusterd_volinfo_t *volinfo, char *threshold_key) {
        int     threshold       = 0;
        char    *str_thresold   = NULL;
        int     ret             = -1;
        xlator_t *this          = NULL;

        this = THIS;
        GF_ASSERT (this);

        glusterd_volinfo_get (volinfo, threshold_key, &str_thresold);
        if (str_thresold) {
                ret = gf_string2int (str_thresold, &threshold);
                if (ret == -1) {
                        threshold = ret;
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INCOMPATIBLE_VALUE, "Failed to convert "
                        "string to integer");
                }
        }

        return threshold;
}

/*
 * Validation function for record-counters
 * if write-freq-threshold and read-freq-threshold both have non-zero values
 * record-counters cannot be set to off
 * if record-counters is set to on
 * check if both the frequency thresholds are zero, then pop
 * a note, but volume set is not failed.
 * */
static int
validate_tier_counters (glusterd_volinfo_t      *volinfo,
                        dict_t                  *dict,
                        char                    *key,
                        char                    *value,
                        char                    **op_errstr) {

        char            errstr[2048]    = "";
        int             ret             = -1;
        xlator_t        *this           = NULL;
        gf_boolean_t    origin_val      = -1;
        int             current_wt      = 0;
        int             current_rt      = 0;

        this = THIS;
        GF_ASSERT (this);

        if (volinfo->type != GF_CLUSTER_TYPE_TIER) {
                snprintf (errstr, sizeof (errstr), "Volume %s is not a tier "
                          "volume. Option %s is only valid for tier volume.",
                          volinfo->volname, key);
                goto out;
        }

        ret = gf_string2boolean (value, &origin_val);
        if (ret) {
                snprintf (errstr, sizeof (errstr), "%s is not a compatible "
                          "value. %s expects an boolean value", value, key);
                goto out;
        }

        current_rt = get_tier_freq_threshold (volinfo,
                                                "cluster.read-freq-threshold");
        if (current_rt == -1) {
                snprintf (errstr, sizeof (errstr), " Failed to retrieve value"
                        " of cluster.read-freq-threshold");
                goto out;
        }
        current_wt = get_tier_freq_threshold (volinfo,
                                                "cluster.write-freq-threshold");
        if (current_wt == -1) {
                snprintf (errstr, sizeof (errstr), " Failed to retrieve value "
                          "of cluster.write-freq-threshold");
                goto out;
        }
        /* If record-counters is set to off */
        if (!origin_val) {

                /* Both the thresholds should be zero to set
                 * record-counters to off*/
                if (current_rt || current_wt) {
                        snprintf (errstr, sizeof (errstr),
                                "Cannot set features.record-counters to \"%s\""
                                " as cluster.write-freq-threshold is %d"
                                " and cluster.read-freq-threshold is %d. Please"
                                " set both cluster.write-freq-threshold and "
                                " cluster.read-freq-threshold to 0, to set "
                                " features.record-counters to \"%s\".",
                                value, current_wt, current_rt, value);
                        ret = -1;
                        goto out;
                }
        }
        /* TODO give a warning message to the user. errstr without re = -1 will
         * not result in a warning on cli for now.
        else {
                if (!current_rt && !current_wt) {
                        snprintf (errstr, sizeof (errstr),
                                " Note : cluster.write-freq-threshold is %d"
                                " and cluster.read-freq-threshold is %d. Please"
                                " set both cluster.write-freq-threshold and "
                                " cluster.read-freq-threshold to"
                                " appropriate positive values.",
                                current_wt, current_rt);
                }
        }*/

        ret = 0;
out:

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INCOMPATIBLE_VALUE, "%s", errstr);
                *op_errstr = gf_strdup (errstr);
        }

        return ret;

}


/*
 * Validation function for ctr sql params
 *      features.ctr-sql-db-cachesize           (Range: 1000 to 262144 pages)
 *      features.ctr-sql-db-wal-autocheckpoint  (Range: 1000 to 262144 pages)
 * */
static int
validate_ctr_sql_params (glusterd_volinfo_t      *volinfo,
                        dict_t                  *dict,
                        char                    *key,
                        char                    *value,
                        char                    **op_errstr)
{
        int ret                         = -1;
        xlator_t        *this           = NULL;
        char            errstr[2048]    = "";
        int             origin_val      = -1;

        this = THIS;
        GF_ASSERT (this);


        ret = gf_string2int (value, &origin_val);
        if (ret) {
                snprintf (errstr, sizeof (errstr), "%s is not a compatible "
                          "value. %s expects an integer value.", value, key);
                ret = -1;
                goto out;
        }

        if (origin_val < 0) {
                snprintf (errstr, sizeof (errstr), "%s is not a "
                          "compatible value. %s expects a positive"
                          "integer value.", value, key);
                ret = -1;
                goto out;
        }

        if (strstr (key, "sql-db-cachesize") ||
                strstr (key, "sql-db-wal-autocheckpoint")) {
                if ((origin_val < 1000) || (origin_val > 262144)) {
                        snprintf (errstr, sizeof (errstr), "%s is not a "
                                  "compatible value. %s "
                                  "expects a value between : "
                                  "1000 to 262144.",
                                  value, key);
                        ret = -1;
                        goto out;
                }
        }


        ret = 0;
out:
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INCOMPATIBLE_VALUE, "%s", errstr);
                *op_errstr = gf_strdup (errstr);
        }
        return ret;
}


/* Validation for tiering frequency thresholds
 * If any of the frequency thresholds are set to a non-zero value,
 * switch record-counters on, if not already on
 * If both the frequency thresholds are set to zero,
 * switch record-counters off, if not already off
 * */
static int
validate_tier_thresholds (glusterd_volinfo_t    *volinfo,
                          dict_t                *dict,
                          char                  *key,
                          char                  *value,
                          char                  **op_errstr)
{
        char            errstr[2048]    = "";
        int             ret             = -1;
        xlator_t        *this           = NULL;
        int             origin_val      = -1;
        gf_boolean_t    current_rc      = _gf_false;
        int             current_wt      = 0;
        int             current_rt      = 0;
        gf_boolean_t    is_set_rc       = _gf_false;
        char            *proposed_rc    = NULL;


        this = THIS;
        GF_ASSERT (this);

        if (volinfo->type != GF_CLUSTER_TYPE_TIER) {
                snprintf (errstr, sizeof (errstr), "Volume %s is not a tier "
                          "volume. Option %s is only valid for tier volume.",
                          volinfo->volname, key);
                goto out;
        }


        ret = gf_string2int (value, &origin_val);
        if (ret) {
                snprintf (errstr, sizeof (errstr), "%s is not a compatible "
                          "value. %s expects an integer value.", value, key);
                ret = -1;
                goto out;
        }

        if (origin_val < 0) {
                snprintf (errstr, sizeof (errstr), "%s is not a "
                          "compatible value. %s expects a positive"
                          "integer value.", value, key);
                ret = -1;
                goto out;
        }

        /* Get the record-counters value */
        ret = glusterd_volinfo_get_boolean (volinfo,
                                        "features.record-counters");
        if (ret == -1) {
                snprintf (errstr, sizeof (errstr), "Failed to retrive value of"
                        "features.record-counters from volume info");
                goto out;
        }
        current_rc = ret;

        /* if any of the thresholds are set to a non-zero value
         * switch record-counters on, if not already on*/
        if (origin_val > 0) {
                if (!current_rc) {
                        is_set_rc = _gf_true;
                        current_rc = _gf_true;
                }
        } else {
                /* if the set is for write-freq-threshold */
                if (strstr (key, "write-freq-threshold")) {
                        current_rt = get_tier_freq_threshold (volinfo,
                                              "cluster.read-freq-threshold");
                         if (current_rt == -1) {
                                snprintf (errstr, sizeof (errstr),
                                        " Failed to retrive value of"
                                        "cluster.read-freq-threshold");
                                goto out;
                         }
                        current_wt = origin_val;
                }
                /* else it should be read-freq-threshold */
                else {
                        current_wt = get_tier_freq_threshold  (volinfo,
                                              "cluster.write-freq-threshold");
                         if (current_wt == -1) {
                                snprintf (errstr, sizeof (errstr),
                                        " Failed to retrive value of"
                                        "cluster.write-freq-threshold");
                                goto out;
                         }
                        current_rt = origin_val;
                }

                /* Since both the thresholds are zero, set record-counters
                 * to off, if not already off */
                if (current_rt == 0 && current_wt == 0) {
                        if (current_rc) {
                                is_set_rc = _gf_true;
                                current_rc = _gf_false;
                        }
                }
        }

        /* if record-counter has to be set to proposed value */
        if (is_set_rc) {
                if (current_rc) {
                        ret = gf_asprintf (&proposed_rc, "on");
                } else {
                        ret = gf_asprintf (&proposed_rc, "off");
                }
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_INCOMPATIBLE_VALUE,
                                "Failed to allocate memory to dict_value");
                        goto error;
                }
                ret = dict_set_str (volinfo->dict, "features.record-counters",
                                proposed_rc);
error:
                if (ret) {
                        snprintf (errstr, sizeof (errstr),
                                "Failed to set features.record-counters"
                                "to \"%s\" automatically."
                                "Please try to set features.record-counters "
                                "\"%s\" manually. The options "
                                "cluster.write-freq-threshold and "
                                "cluster.read-freq-threshold can only "
                                "be set to a non zero value, if "
                                "features.record-counters is "
                                "set to \"on\".", proposed_rc, proposed_rc);
                        goto out;
                }
        }
        ret = 0;
out:
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INCOMPATIBLE_VALUE, "%s", errstr);
                *op_errstr = gf_strdup (errstr);
                if (proposed_rc)
                        GF_FREE (proposed_rc);
        }
        return ret;
}



static int
validate_tier (glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
               char *value, char **op_errstr)
{
        char                 errstr[2048]  = "";
        int                  ret           = 0;
        xlator_t            *this          = NULL;
        int                  origin_val    = -1;
        char                *current_wm_hi = NULL;
        char                *current_wm_low = NULL;
        uint64_t             wm_hi = 0;
        uint64_t             wm_low = 0;

        this = THIS;
        GF_ASSERT (this);

        if (volinfo->type != GF_CLUSTER_TYPE_TIER) {
                snprintf (errstr, sizeof (errstr), "Volume %s is not a tier "
                          "volume. Option %s is only valid for tier volume.",
                          volinfo->volname, key);
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INCOMPATIBLE_VALUE, "%s", errstr);
                *op_errstr = gf_strdup (errstr);
                ret = -1;
                goto out;
        }

        if (strstr (key, "cluster.tier-mode")) {
                if (strcmp(value, "test") &&
                    strcmp(value, "cache")) {
                        ret = -1;
                        goto out;
                }
                goto out;
        } else if (strstr (key, "tier-pause")) {
                if (strcmp(value, "off") &&
                    strcmp(value, "on")) {
                        ret = -1;
                        goto out;
                }
                goto out;
        } else if (strstr (key, "tier-compact")) {
                if (strcmp (value, "on") &&
                    strcmp (value, "off")) {
                        ret = -1;
                        goto out;
                }

                goto out;
        }

        /*
         * Rest of the volume set options for tier are expecting a positive
         * Integer. Change the function accordingly if this constraint is
         * changed.
         */
        ret = gf_string2int (value, &origin_val);
        if (ret) {
                snprintf (errstr, sizeof (errstr), "%s is not a compatible "
                          "value. %s expects an integer value.",
                          value, key);
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INCOMPATIBLE_VALUE, "%s", errstr);
                *op_errstr = gf_strdup (errstr);
                ret = -1;
                goto out;
        }

        if (strstr (key, "watermark-hi") ||
            strstr (key, "watermark-low")) {
                if ((origin_val < 1) || (origin_val > 99)) {
                        snprintf (errstr, sizeof (errstr), "%s is not a "
                                  "compatible value. %s expects a "
                                  "percentage from 1-99.",
                                  value, key);
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_INCOMPATIBLE_VALUE, "%s", errstr);
                        *op_errstr = gf_strdup (errstr);
                        ret = -1;
                        goto out;
                }

                if (strstr (key, "watermark-hi")) {
                        wm_hi = origin_val;
                } else {
                        glusterd_volinfo_get (volinfo,
                                              "cluster.watermark-hi",
                                              &current_wm_hi);
                        gf_string2bytesize_uint64 (current_wm_hi,
                                                   &wm_hi);
                }

                if (strstr (key, "watermark-low")) {
                        wm_low = origin_val;
                } else {
                        glusterd_volinfo_get (volinfo,
                                              "cluster.watermark-low",
                                              &current_wm_low);
                        gf_string2bytesize_uint64 (current_wm_low,
                                                   &wm_low);
                }
                if (wm_low > wm_hi) {
                        snprintf (errstr, sizeof (errstr), "lower watermark"
                                  " cannot exceed upper watermark.");
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_INCOMPATIBLE_VALUE, "%s", errstr);
                        *op_errstr = gf_strdup (errstr);
                        ret = -1;
                        goto out;
                }
        } else if (strstr (key, "tier-promote-frequency") ||
                   strstr (key, "tier-max-mb") ||
                   strstr (key, "tier-max-promote-file-size") ||
                   strstr (key, "tier-max-files") ||
                   strstr (key, "tier-demote-frequency") ||
                   strstr (key, "tier-hot-compact-frequency") ||
                   strstr (key, "tier-cold-compact-frequency") ||
                   strstr (key, "tier-query-limit")) {
                if (origin_val < 1) {
                        snprintf (errstr, sizeof (errstr), "%s is not a "
                                  " compatible value. %s expects a positive "
                                  "integer value greater than 0.",
                                  value, key);
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_INCOMPATIBLE_VALUE, "%s", errstr);
                        *op_errstr = gf_strdup (errstr);
                        ret = -1;
                        goto out;
                }
        }
out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}

#endif  /* End for USE_GFDB */

static int
validate_cache_max_min_size (glusterd_volinfo_t *volinfo, dict_t *dict,
                             char *key, char *value, char **op_errstr)
{
        char                *current_max_value = NULL;
        char                *current_min_value = NULL;
        char                 errstr[2048]  = "";
        glusterd_conf_t     *priv          = NULL;
        int                  ret           = 0;
        uint64_t             max_value     = 0;
        uint64_t             min_value     = 0;
        xlator_t            *this          = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        if ((!strcmp (key, "performance.cache-min-file-size")) ||
            (!strcmp (key, "cache-min-file-size"))) {
                glusterd_volinfo_get (volinfo,
                                      "performance.cache-max-file-size",
                                      &current_max_value);
                if (current_max_value) {
                        gf_string2bytesize_uint64 (current_max_value, &max_value);
                        gf_string2bytesize_uint64 (value, &min_value);
                        current_min_value = value;
                }
        } else  if ((!strcmp (key, "performance.cache-max-file-size")) ||
                    (!strcmp (key, "cache-max-file-size"))) {
                glusterd_volinfo_get (volinfo,
                                      "performance.cache-min-file-size",
                                      &current_min_value);
                if (current_min_value) {
                        gf_string2bytesize_uint64 (current_min_value, &min_value);
                        gf_string2bytesize_uint64 (value, &max_value);
                        current_max_value = value;
                }
        }

        if (min_value > max_value) {
                snprintf (errstr, sizeof (errstr),
                          "cache-min-file-size (%s) is greater than "
                          "cache-max-file-size (%s)",
                          current_min_value, current_max_value);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_CACHE_MINMAX_SIZE_INVALID,  "%s", errstr);
                *op_errstr = gf_strdup (errstr);
                ret = -1;
                goto out;
        }

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}

static int
validate_defrag_throttle_option (glusterd_volinfo_t *volinfo, dict_t *dict,
                                 char *key, char *value, char **op_errstr)
{
        char                 errstr[2048] = "";
        int                  ret          = 0;
        xlator_t            *this         = NULL;

        this = THIS;
        GF_ASSERT (this);

        if (!strcasecmp (value, "lazy") ||
            !strcasecmp (value, "normal") ||
            !strcasecmp (value, "aggressive")) {
                ret = 0;
        } else {
                ret = -1;
                snprintf (errstr, sizeof (errstr), "%s should be "
                          "{lazy|normal|aggressive}", key);
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INVALID_ENTRY, "%s", errstr);
                *op_errstr = gf_strdup (errstr);
        }

        gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}

static int
validate_quota (glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                char *value, char **op_errstr)
{
        char                 errstr[2048] = "";
        glusterd_conf_t     *priv         = NULL;
        int                  ret          = 0;
        xlator_t            *this         = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = glusterd_volinfo_get_boolean (volinfo, VKEY_FEATURES_QUOTA);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_QUOTA_GET_STAT_FAIL,
                        "failed to get the quota status");
                goto out;
        }

        if (ret == _gf_false) {
                snprintf (errstr, sizeof (errstr),
                          "Cannot set %s. Enable quota first.", key);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_QUOTA_DISABLED, "%s", errstr);
                *op_errstr = gf_strdup (errstr);
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}

static int
validate_uss (glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
              char *value, char **op_errstr)
{
        char                 errstr[2048]  = "";
        int                  ret           = 0;
        xlator_t            *this          = NULL;
        gf_boolean_t         b             = _gf_false;

        this = THIS;
        GF_ASSERT (this);

        ret = gf_string2boolean (value, &b);
        if (ret) {
                snprintf (errstr, sizeof (errstr), "%s is not a valid boolean "
                          "value. %s expects a valid boolean value.", value,
                          key);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_INVALID_ENTRY, "%s", errstr);
                *op_errstr = gf_strdup (errstr);
                goto out;
        }
out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}

static int
validate_uss_dir (glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                  char *value, char **op_errstr)
{
        char                 errstr[2048]  = "";
        int                  ret           = -1;
        int                  i             = 0;
        xlator_t            *this          = NULL;

        this = THIS;
        GF_ASSERT (this);

        i = strlen (value);
        if (i > NAME_MAX) {
                snprintf (errstr, sizeof (errstr), "value of %s exceedes %d "
                          "characters", key, NAME_MAX);
                goto out;
        } else if (i < 2) {
                snprintf (errstr, sizeof (errstr), "value of %s too short, "
                          "expects atleast two characters", key);
                goto out;
        }

        if (value[0] != '.') {
                snprintf (errstr, sizeof (errstr), "%s expects value starting "
                          "with '.' ", key);
                goto out;
        }

        for (i = 1; value[i]; i++) {
                if (isalnum (value[i]) || value[i] == '_' || value[i] == '-')
                        continue;

                snprintf (errstr, sizeof (errstr), "%s expects value to"
                          " contain only '0-9a-z-_'", key);
                goto out;
        }

        ret = 0;
out:
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INVALID_ENTRY, "%s", errstr);
                *op_errstr = gf_strdup (errstr);
        }

        gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}

static int
validate_stripe (glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                 char *value, char **op_errstr)
{
        char                 errstr[2048]  = "";
        glusterd_conf_t     *priv          = NULL;
        int                  ret           = 0;
        xlator_t            *this          = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        if (volinfo->stripe_count == 1) {
                snprintf (errstr, sizeof (errstr),
                          "Cannot set %s for a non-stripe volume.", key);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_NON_STRIPE_VOL, "%s", errstr);
                *op_errstr = gf_strdup (errstr);
                ret = -1;
               goto out;
        }

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}

static int
validate_replica (glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                 char *value, char **op_errstr)
{
        char                 errstr[2048]  = "";
        int                  ret           = 0;
        xlator_t            *this          = NULL;

        this = THIS;
        GF_ASSERT (this);

        if (volinfo->replica_count == 1) {
                snprintf (errstr, sizeof (errstr),
                          "Cannot set %s for a non-replicate volume.", key);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOL_NOT_REPLICA, "%s", errstr);
                *op_errstr = gf_strdup (errstr);
                ret = -1;
                goto out;
        }

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}

static int
validate_subvols_per_directory (glusterd_volinfo_t *volinfo, dict_t *dict,
                                char *key, char *value, char **op_errstr)
{
        char                 errstr[2048]  = "";
        glusterd_conf_t     *priv          = NULL;
        int                  ret           = 0;
        int                  subvols       = 0;
        xlator_t            *this          = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        subvols = atoi(value);

        /* Checking if the subvols-per-directory exceed the total
           number of subvolumes. */
        if (subvols > volinfo->subvol_count) {
                snprintf (errstr, sizeof(errstr),
                          "subvols-per-directory(%d) is greater "
                          "than the number of subvolumes(%d).",
                          subvols, volinfo->subvol_count);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SUBVOLUMES_EXCEED,
                        "%s.", errstr);
                *op_errstr = gf_strdup (errstr);
                ret = -1;
                goto out;
        }

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}

static int
validate_replica_heal_enable_disable (glusterd_volinfo_t *volinfo, dict_t *dict,
                                      char *key, char *value, char **op_errstr)
{
        int                  ret = 0;

        if (!glusterd_is_volume_replicate (volinfo)) {
                gf_asprintf (op_errstr, "Volume %s is not of replicate type",
                             volinfo->volname);
                ret = -1;
        }

        return ret;
}

static int
validate_mandatory_locking (glusterd_volinfo_t *volinfo, dict_t *dict,
                            char *key, char *value, char **op_errstr)
{
        char                 errstr[2048]  = "";
        int                  ret           = 0;
        xlator_t            *this          = NULL;

        this = THIS;
        GF_ASSERT (this);

        if (strcmp (value, "off") != 0 && strcmp (value, "file") != 0 &&
                        strcmp(value, "forced") != 0 &&
                        strcmp(value, "optimal") != 0) {
                snprintf (errstr, sizeof(errstr), "Invalid option value '%s':"
                          " Available options are 'off', 'file', "
                          "'forced' or 'optimal'", value);
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
                                "%s", errstr);
                *op_errstr = gf_strdup (errstr);
                ret = -1;
                goto out;
        }
out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);

        return ret;
}

static int
validate_disperse_heal_enable_disable (glusterd_volinfo_t *volinfo,
                                       dict_t *dict, char *key, char *value,
                                       char **op_errstr)
{
        int                  ret = 0;
        if (volinfo->type == GF_CLUSTER_TYPE_TIER) {
               if (volinfo->tier_info.cold_type != GF_CLUSTER_TYPE_DISPERSE &&
                   volinfo->tier_info.hot_type != GF_CLUSTER_TYPE_DISPERSE) {
                        gf_asprintf (op_errstr, "Volume %s is not containing "
                                     "disperse type", volinfo->volname);

                       return -1;
               } else
                       return 0;
        }

        if (volinfo->type != GF_CLUSTER_TYPE_DISPERSE) {
                gf_asprintf (op_errstr, "Volume %s is not of disperse type",
                             volinfo->volname);
                ret = -1;
        }

        return ret;
}

static int
validate_lock_migration_option (glusterd_volinfo_t *volinfo, dict_t *dict,
                                 char *key, char *value, char **op_errstr)
{
        char                 errstr[2048] = "";
        int                  ret          = 0;
        xlator_t            *this         = NULL;
        gf_boolean_t         b            = _gf_false;

        this = THIS;
        GF_ASSERT (this);

        if (volinfo->replica_count > 1 || volinfo->disperse_count ||
            volinfo->type == GF_CLUSTER_TYPE_TIER) {
                snprintf (errstr, sizeof (errstr), "Lock migration is "
                          "a experimental feature. Currently works with"
                          " pure distribute volume only");
                ret = -1;

                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                         GD_MSG_INVALID_ENTRY, "%s", errstr);

                *op_errstr = gf_strdup (errstr);
                goto out;
        }

        ret = gf_string2boolean (value, &b);
        if (ret) {
                snprintf (errstr, sizeof (errstr), "Invalid value"
                          " for volume set command. Use on/off only.");
                ret = -1;

                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                         GD_MSG_INVALID_ENTRY, "%s", errstr);

                *op_errstr = gf_strdup (errstr);

                goto out;
        }

        gf_msg_debug (this->name, 0, "Returning %d", ret);

out:
        return ret;
}


static int
validate_worm (glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
               char *value, char **op_errstr)
{
        xlator_t *this      =       NULL;
        gf_boolean_t b      =       _gf_false;
        int ret             =       -1;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);
        ret = gf_string2boolean (value, &b);
        if (ret) {
                gf_asprintf (op_errstr, "%s is not a valid boolean value. %s "
                             "expects a valid boolean value.", value, key);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_INVALID_ENTRY, "%s", *op_errstr);
        }
out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);

        return ret;
}


static int
validate_worm_period (glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
               char *value, char **op_errstr)
{
        xlator_t *this      =       NULL;
        uint64_t period     =       -1;
        int ret             =       -1;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);
        ret = gf_string2uint64 (value, &period);
        if (ret) {
                gf_asprintf (op_errstr, "%s is not a valid uint64_t value."
                          " %s expects a valid uint64_t value.", value, key);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_INVALID_ENTRY, "%s", *op_errstr);
        }
out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);

        return ret;
}


static int
validate_reten_mode (glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
               char *value, char **op_errstr)
{
        xlator_t *this      =       NULL;
        int ret             =       -1;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);
        if ((strcmp (value, "relax") &&
            strcmp (value, "enterprise"))) {
                gf_asprintf (op_errstr, "The value of retention mode should be "
                             "either relax or enterprise. But the value"
                             " of %s is %s", key, value);
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY,
                        "%s", *op_errstr);
                ret = -1;
                goto out;
        }
        ret = 0;
out:
        gf_msg_debug ("glusterd", 0, "Returning %d", ret);

        return ret;
}


/* dispatch table for VOLUME SET
 * -----------------------------
 *
 * Format of entries:
 *
 * First field is the <key>, for the purpose of looking it up
 * in volume dictionary. Each <key> is of the format "<domain>.<specifier>".
 *
 * Second field is <voltype>.
 *
 * Third field is <option>, if its unset, it's assumed to be
 * the same as <specifier>.
 *
 * Fourth field is <value>. In this context they are used to specify
 * a default. That is, even the volume dict doesn't have a value,
 * we procced as if the default value were set for it.
 *
 * Fifth field is <doctype>, which decides if the option is public and available
 * in "set help" or not. "NO_DOC" entries are not part of the public interface
 * and are subject to change at any time. This also decides if an option is
 * global (apllies to all volumes) or normal (applies to only specified volume).
 *
 * Sixth field is <flags>.
 *
 * Seventh field is <op-version>.
 *
 * Eight field is description of option: If NULL, tried to fetch from
 * translator code's xlator_options table.
 *
 * Nineth field is validation function: If NULL, xlator's option specific
 * validation will be tried, otherwise tried at glusterd code itself.
 *
 * There are two type of entries: basic and special.
 *
 * - Basic entries are the ones where the <option> does _not_ start with
 *   the bang! character ('!').
 *
 *   In their case, <option> is understood as an option for an xlator of
 *   type <voltype>. Their effect is to copy over the volinfo->dict[<key>]
 *   value to all graph nodes of type <voltype> (if such a value is set).
 *
 *   You are free to add entries of this type, they will become functional
 *   just by being present in the table.
 *
 * - Special entries where the <option> starts with the bang!.
 *
 *   They are not applied to all graphs during generation, and you cannot
 *   extend them in a trivial way which could be just picked up. Better
 *   not touch them unless you know what you do.
 *
 *
 * Another kind of grouping for options, according to visibility:
 *
 * - Exported: one which is used in the code. These are characterized by
 *   being used a macro as <key> (of the format VKEY_..., defined in
 *   glusterd-volgen.h
 *
 * - Non-exported: the rest; these have string literal <keys>.
 *
 * Adhering to this policy, option name changes shall be one-liners.
 *
 */

struct volopt_map_entry glusterd_volopt_map[] = {
        /* DHT xlator options */
        { .key        = "cluster.lookup-unhashed",
          .voltype    = "cluster/distribute",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.lookup-optimize",
          .voltype    = "cluster/distribute",
          .op_version  = GD_OP_VERSION_3_7_2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.min-free-disk",
          .voltype    = "cluster/distribute",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.min-free-inodes",
          .voltype    = "cluster/distribute",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.rebalance-stats",
          .voltype    = "cluster/distribute",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "cluster.subvols-per-directory",
          .voltype     = "cluster/distribute",
          .option      = "directory-layout-spread",
          .op_version  = 2,
          .validate_fn = validate_subvols_per_directory,
          .flags       = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.readdir-optimize",
          .voltype    = "cluster/distribute",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.rsync-hash-regex",
          .voltype    = "cluster/distribute",
          .type       = NO_DOC,
          .op_version = 3,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.extra-hash-regex",
          .voltype    = "cluster/distribute",
          .type       = NO_DOC,
          .op_version = 3,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.dht-xattr-name",
          .voltype    = "cluster/distribute",
          .option     = "xattr-name",
          .type       = NO_DOC,
          .op_version = 3,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.randomize-hash-range-by-gfid",
          .voltype    = "cluster/distribute",
          .option     = "randomize-hash-range-by-gfid",
          .type       = NO_DOC,
          .op_version = GD_OP_VERSION_3_6_0,
          .flags      = OPT_FLAG_CLIENT_OPT,
        },
        { .key         = "cluster.rebal-throttle",
          .voltype     = "cluster/distribute",
          .option      = "rebal-throttle",
          .op_version  = GD_OP_VERSION_3_7_0,
          .validate_fn = validate_defrag_throttle_option,
          .flags       = OPT_FLAG_CLIENT_OPT,
        },

        { .key         = "cluster.lock-migration",
          .voltype     = "cluster/distribute",
          .option      = "lock-migration",
          .value       = "off",
          .op_version  = GD_OP_VERSION_3_8_0,
          .validate_fn = validate_lock_migration_option,
          .flags       = OPT_FLAG_CLIENT_OPT,
        },

        /* NUFA xlator options (Distribute special case) */
        { .key        = "cluster.nufa",
          .voltype    = "cluster/distribute",
          .option     = "!nufa",
          .type       = NO_DOC,
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.local-volume-name",
          .voltype    = "cluster/nufa",
          .option     = "local-volume-name",
          .type       = NO_DOC,
          .op_version = 3,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.weighted-rebalance",
          .voltype    = "cluster/distribute",
          .op_version = GD_OP_VERSION_3_6_0,
        },

        /* Switch xlator options (Distribute special case) */
        { .key        = "cluster.switch",
          .voltype    = "cluster/distribute",
          .option     = "!switch",
          .type       = NO_DOC,
          .op_version = 3,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.switch-pattern",
          .voltype    = "cluster/switch",
          .option     = "pattern.switch.case",
          .type       = NO_DOC,
          .op_version = 3,
          .flags      = OPT_FLAG_CLIENT_OPT
        },

        /* AFR xlator options */
        { .key        = "cluster.entry-change-log",
          .voltype    = "cluster/replicate",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.read-subvolume",
          .voltype    = "cluster/replicate",
          .op_version = 1,
          .flags     = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.read-subvolume-index",
          .voltype    = "cluster/replicate",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.read-hash-mode",
          .voltype    = "cluster/replicate",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.background-self-heal-count",
          .voltype    = "cluster/replicate",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "cluster.metadata-self-heal",
          .voltype     = "cluster/replicate",
          .op_version  = 1,
          .validate_fn = validate_replica,
          .flags       = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "cluster.data-self-heal",
          .voltype     = "cluster/replicate",
          .op_version  = 1,
          .validate_fn = validate_replica,
          .flags       = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "cluster.entry-self-heal",
          .voltype     = "cluster/replicate",
          .op_version  = 1,
          .validate_fn = validate_replica,
          .flags       = OPT_FLAG_CLIENT_OPT
        },
        { .key           = "cluster.self-heal-daemon",
          .voltype       = "cluster/replicate",
          .option        = "!self-heal-daemon",
          .op_version    = 1,
          .validate_fn   = validate_replica_heal_enable_disable
        },
        { .key        = "cluster.heal-timeout",
          .voltype    = "cluster/replicate",
          .option     = "!heal-timeout",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.strict-readdir",
          .voltype    = "cluster/replicate",
          .type       = NO_DOC,
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.self-heal-window-size",
          .voltype    = "cluster/replicate",
          .option     = "data-self-heal-window-size",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.data-change-log",
          .voltype    = "cluster/replicate",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.metadata-change-log",
          .voltype    = "cluster/replicate",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.data-self-heal-algorithm",
          .voltype    = "cluster/replicate",
          .option     = "data-self-heal-algorithm",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.eager-lock",
          .voltype    = "cluster/replicate",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "disperse.eager-lock",
          .voltype    = "cluster/disperse",
          .op_version = GD_OP_VERSION_3_7_10,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.quorum-type",
          .voltype    = "cluster/replicate",
          .option     = "quorum-type",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.quorum-count",
          .voltype    = "cluster/replicate",
          .option     = "quorum-count",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.choose-local",
          .voltype    = "cluster/replicate",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.self-heal-readdir-size",
          .voltype    = "cluster/replicate",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.post-op-delay-secs",
          .voltype    = "cluster/replicate",
          .type       = NO_DOC,
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.readdir-failover",
          .voltype    = "cluster/replicate",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.ensure-durability",
          .voltype    = "cluster/replicate",
          .op_version = 3,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.consistent-metadata",
          .voltype    = "cluster/replicate",
          .type       = DOC,
          .op_version = GD_OP_VERSION_3_7_0,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.heal-wait-queue-length",
          .voltype    = "cluster/replicate",
          .type       = DOC,
          .op_version = GD_OP_VERSION_3_7_10,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.favorite-child-policy",
          .voltype    = "cluster/replicate",
          .type       = DOC,
          .op_version = GD_OP_VERSION_3_7_12,
          .flags      = OPT_FLAG_CLIENT_OPT
        },

        /* stripe xlator options */
        { .key         = "cluster.stripe-block-size",
          .voltype     = "cluster/stripe",
          .option      = "block-size",
          .op_version  = 1,
          .validate_fn = validate_stripe,
          .flags       = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.stripe-coalesce",
          .voltype    = "cluster/stripe",
          .option     = "coalesce",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },

        /* IO-stats xlator options */
        { .key         = VKEY_DIAG_LAT_MEASUREMENT,
          .voltype     = "debug/io-stats",
          .option      = "latency-measurement",
          .value       = "off",
          .op_version  = 1
        },
        { .key         = "diagnostics.dump-fd-stats",
          .voltype     = "debug/io-stats",
          .op_version  = 1
        },
        { .key         = VKEY_DIAG_CNT_FOP_HITS,
          .voltype     = "debug/io-stats",
          .option      = "count-fop-hits",
          .value       = "off",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "diagnostics.brick-log-level",
          .voltype     = "debug/io-stats",
          .value       = "INFO",
          .option      = "!brick-log-level",
          .op_version  = 1
        },
        { .key        = "diagnostics.client-log-level",
          .voltype    = "debug/io-stats",
          .value      = "INFO",
          .option     = "!client-log-level",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "diagnostics.brick-sys-log-level",
          .voltype     = "debug/io-stats",
          .option      = "!sys-log-level",
          .op_version  = 1
        },
        { .key        = "diagnostics.client-sys-log-level",
          .voltype    = "debug/io-stats",
          .option     = "!sys-log-level",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "diagnostics.brick-logger",
          .voltype     = "debug/io-stats",
          .option      = "!logger",
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key        = "diagnostics.client-logger",
          .voltype    = "debug/io-stats",
          .option     = "!logger",
          .op_version = GD_OP_VERSION_3_6_0,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "diagnostics.brick-log-format",
          .voltype     = "debug/io-stats",
          .option      = "!log-format",
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key        = "diagnostics.client-log-format",
          .voltype    = "debug/io-stats",
          .option     = "!log-format",
          .op_version = GD_OP_VERSION_3_6_0,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "diagnostics.brick-log-buf-size",
          .voltype     = "debug/io-stats",
          .option      = "!log-buf-size",
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key        = "diagnostics.client-log-buf-size",
          .voltype    = "debug/io-stats",
          .option     = "!log-buf-size",
          .op_version = GD_OP_VERSION_3_6_0,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "diagnostics.brick-log-flush-timeout",
          .voltype     = "debug/io-stats",
          .option      = "!log-flush-timeout",
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key        = "diagnostics.client-log-flush-timeout",
          .voltype    = "debug/io-stats",
          .option     = "!log-flush-timeout",
          .op_version = GD_OP_VERSION_3_6_0,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "diagnostics.stats-dump-interval",
          .voltype     = "debug/io-stats",
          .option      = "ios-dump-interval",
          .op_version  = 1
        },
        { .key         = "diagnostics.fop-sample-interval",
          .voltype     = "debug/io-stats",
          .option      = "ios-sample-interval",
          .op_version  = 1
        },
        { .key         = "diagnostics.fop-sample-buf-size",
          .voltype     = "debug/io-stats",
          .option      = "ios-sample-buf-size",
          .op_version  = 1
        },
        { .key         = "diagnostics.stats-dnscache-ttl-sec",
          .voltype     = "debug/io-stats",
          .option      = "ios-dnscache-ttl-sec",
          .op_version  = 1
        },

        /* IO-cache xlator options */
        { .key         = "performance.cache-max-file-size",
          .voltype     = "performance/io-cache",
          .option      = "max-file-size",
          .op_version  = 1,
          .validate_fn = validate_cache_max_min_size,
          .flags       = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "performance.cache-min-file-size",
          .voltype     = "performance/io-cache",
          .option      = "min-file-size",
          .op_version  = 1,
          .validate_fn = validate_cache_max_min_size,
          .flags       = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.cache-refresh-timeout",
          .voltype    = "performance/io-cache",
          .option     = "cache-timeout",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.cache-priority",
          .voltype    = "performance/io-cache",
          .option     = "priority",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.cache-size",
          .voltype    = "performance/io-cache",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },

        /* IO-threads xlator options */
        { .key         = "performance.io-thread-count",
          .voltype     = "performance/io-threads",
          .option      = "thread-count",
          .op_version  = 1
        },
        { .key         = "performance.high-prio-threads",
          .voltype     = "performance/io-threads",
          .op_version  = 1
        },
        { .key         = "performance.normal-prio-threads",
          .voltype     = "performance/io-threads",
          .op_version  = 1
        },
        { .key         = "performance.low-prio-threads",
          .voltype     = "performance/io-threads",
          .op_version  = 1
        },
        { .key         = "performance.least-prio-threads",
          .voltype     = "performance/io-threads",
          .op_version  = 1
        },
        { .key         = "performance.enable-least-priority",
          .voltype     = "performance/io-threads",
          .op_version  = 1
        },

        /* Other perf xlators' options */
        { .key        = "performance.cache-size",
          .voltype    = "performance/quick-read",
          .type       = NO_DOC,
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.flush-behind",
          .voltype    = "performance/write-behind",
          .option     = "flush-behind",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.nfs.flush-behind",
          .voltype    = "performance/write-behind",
          .option     = "flush-behind",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.write-behind-window-size",
          .voltype    = "performance/write-behind",
          .option     = "cache-size",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.resync-failed-syncs-after-fsync",
          .voltype    = "performance/write-behind",
          .option     = "resync-failed-syncs-after-fsync",
          .op_version = GD_OP_VERSION_3_7_7,
          .flags      = OPT_FLAG_CLIENT_OPT,
          .description = "If sync of \"cached-writes issued before fsync\" "
                         "(to backend) fails, this option configures whether "
                         "to retry syncing them after fsync or forget them. "
                         "If set to on, cached-writes are retried "
                         "till a \"flush\" fop (or a successful sync) on sync "
                         "failures. "
                         "fsync itself is failed irrespective of the value of "
                         "this option. ",
        },
        { .key        = "performance.nfs.write-behind-window-size",
          .voltype    = "performance/write-behind",
          .option     = "cache-size",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.strict-o-direct",
          .voltype    = "performance/write-behind",
          .option     = "strict-O_DIRECT",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.nfs.strict-o-direct",
          .voltype    = "performance/write-behind",
          .option     = "strict-O_DIRECT",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.strict-write-ordering",
          .voltype    = "performance/write-behind",
          .option     = "strict-write-ordering",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.nfs.strict-write-ordering",
          .voltype    = "performance/write-behind",
          .option     = "strict-write-ordering",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.lazy-open",
          .voltype    = "performance/open-behind",
          .option     = "lazy-open",
          .op_version = 3,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.read-after-open",
          .voltype    = "performance/open-behind",
          .option     = "read-after-open",
          .op_version = 3,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.read-ahead-page-count",
          .voltype    = "performance/read-ahead",
          .option     = "page-count",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.md-cache-timeout",
          .voltype    = "performance/md-cache",
          .option     = "md-cache-timeout",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.cache-swift-metadata",
          .voltype    = "performance/md-cache",
          .option     = "cache-swift-metadata",
          .op_version = GD_OP_VERSION_3_7_10,
          .description = "Cache swift metadata (user.swift.metadata xattr)",
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.cache-samba-metadata",
          .voltype    = "performance/md-cache",
          .option     = "cache-samba-metadata",
          .op_version = GD_OP_VERSION_3_9_0,
          .description = "Cache samba metadata (user.DOSATTRIB, security.NTACL"
                         " xattr)",
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.cache-capability-xattrs",
          .voltype    = "performance/md-cache",
          .option     = "cache-capability-xattrs",
          .op_version = GD_OP_VERSION_3_10_0,
          .description = "Cache xattrs required for capability based security",
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.cache-ima-xattrs",
          .voltype    = "performance/md-cache",
          .option     = "cache-ima-xattrs",
          .op_version = GD_OP_VERSION_3_10_0,
          .description = "Cache xattrs required for IMA "
                         "(Integrity Measurement Architecture)",
          .flags      = OPT_FLAG_CLIENT_OPT
        },

         /* Crypt xlator options */

        { .key         = "features.encryption",
          .voltype     = "encryption/crypt",
          .option      = "!feat",
          .value       = "off",
          .op_version  = 3,
          .description = "enable/disable client-side encryption for "
                         "the volume.",
          .flags       = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT
        },

        { .key         = "encryption.master-key",
          .voltype     = "encryption/crypt",
          .op_version  = 3,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "encryption.data-key-size",
          .voltype     = "encryption/crypt",
          .op_version  = 3,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "encryption.block-size",
          .voltype     = "encryption/crypt",
          .op_version  = 3,
          .flags      = OPT_FLAG_CLIENT_OPT
        },

        /* Client xlator options */
        { .key        = "network.frame-timeout",
          .voltype    = "protocol/client",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "network.ping-timeout",
          .voltype    = "protocol/client",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "network.tcp-window-size",
          .voltype    = "protocol/client",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "features.lock-heal",
          .voltype    = "protocol/client",
          .option     = "lk-heal",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "features.grace-timeout",
          .voltype    = "protocol/client",
          .option     = "grace-timeout",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "client.ssl",
          .voltype    = "protocol/client",
          .option     = "transport.socket.ssl-enabled",
          .op_version = 2,
          .description = "enable/disable client.ssl flag in the "
                         "volume.",
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "network.remote-dio",
          .voltype    = "protocol/client",
          .option     = "filter-O_DIRECT",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "client.own-thread",
          .voltype     = "protocol/client",
          .option      = "transport.socket.own-thread",
          .type        = NO_DOC,
          .op_version  = GD_OP_VERSION_3_7_0,
        },
        { .key         = "client.event-threads",
          .voltype     = "protocol/client",
          .op_version  = GD_OP_VERSION_3_7_0,
        },

        /* Server xlator options */
        { .key         = "network.ping-timeout",
          .voltype     = "protocol/server",
          .option      = "transport.tcp-user-timeout",
          .op_version  = GD_OP_VERSION_3_7_0,
        },
        { .key         = "network.tcp-window-size",
          .voltype     = "protocol/server",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "network.inode-lru-limit",
          .voltype     = "protocol/server",
          .op_version  = 1
        },
        { .key         = AUTH_ALLOW_MAP_KEY,
          .voltype     = "protocol/server",
          .option      = "!server-auth",
          .value       = "*",
          .op_version  = 1
        },
        { .key         = AUTH_REJECT_MAP_KEY,
          .voltype     = "protocol/server",
          .option      = "!server-auth",
          .op_version  = 1
        },
        { .key         = "transport.keepalive",
          .voltype     = "protocol/server",
          .option      = "transport.socket.keepalive",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "server.allow-insecure",
          .voltype     = "protocol/server",
          .option      = "rpc-auth-allow-insecure",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "server.root-squash",
          .voltype     = "protocol/server",
          .option      = "root-squash",
          .op_version  = 2
        },
        { .key         = "server.anonuid",
          .voltype     = "protocol/server",
          .option      = "anonuid",
          .op_version  = 3
        },
        { .key         = "server.anongid",
          .voltype     = "protocol/server",
          .option      = "anongid",
          .op_version  = 3
        },
        { .key         = "server.statedump-path",
          .voltype     = "protocol/server",
          .option      = "statedump-path",
          .op_version  = 1
        },
        { .key         = "server.outstanding-rpc-limit",
          .voltype     = "protocol/server",
          .option      = "rpc.outstanding-rpc-limit",
          .type        = GLOBAL_DOC,
          .op_version  = 3
        },
        { .key         = "features.lock-heal",
          .voltype     = "protocol/server",
          .option      = "lk-heal",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "features.grace-timeout",
          .voltype     = "protocol/server",
          .option      = "grace-timeout",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "server.ssl",
          .voltype     = "protocol/server",
          .option      = "transport.socket.ssl-enabled",
          .description = "enable/disable server.ssl flag in the "
                         "volume.",
          .op_version  = 2
        },
        { .key         = "auth.ssl-allow",
          .voltype     = "protocol/server",
          .option      = "!ssl-allow",
          .value       = "*",
          .type        = NO_DOC,
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key         = "server.manage-gids",
          .voltype     = "protocol/server",
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key         = "server.dynamic-auth",
          .voltype     = "protocol/server",
          .op_version  = GD_OP_VERSION_3_7_5,
        },
        { .key         = "client.send-gids",
          .voltype     = "protocol/client",
          .type        = NO_DOC,
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key         = "server.gid-timeout",
          .voltype     = "protocol/server",
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key         = "server.own-thread",
          .voltype     = "protocol/server",
          .option      = "transport.socket.own-thread",
          .type        = NO_DOC,
          .op_version  = GD_OP_VERSION_3_7_0,
        },
        { .key         = "server.event-threads",
          .voltype     = "protocol/server",
          .op_version  = GD_OP_VERSION_3_7_0,
        },

        /* Generic transport options */
        { .key         = SSL_OWN_CERT_OPT,
          .voltype     = "rpc-transport/socket",
          .option      = "!ssl-own-cert",
          .op_version  = GD_OP_VERSION_3_7_4,
        },
        { .key         = SSL_PRIVATE_KEY_OPT,
          .voltype     = "rpc-transport/socket",
          .option      = "!ssl-private-key",
          .op_version  = GD_OP_VERSION_3_7_4,
        },
        { .key         = SSL_CA_LIST_OPT,
          .voltype     = "rpc-transport/socket",
          .option      = "!ssl-ca-list",
          .op_version  = GD_OP_VERSION_3_7_4,
        },
        { .key         = SSL_CRL_PATH_OPT,
          .voltype     = "rpc-transport/socket",
          .option      = "!ssl-crl-path",
          .op_version  = GD_OP_VERSION_3_7_4,
        },
        { .key         = SSL_CERT_DEPTH_OPT,
          .voltype     = "rpc-transport/socket",
          .option      = "!ssl-cert-depth",
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key         = SSL_CIPHER_LIST_OPT,
          .voltype     = "rpc-transport/socket",
          .option      = "!ssl-cipher-list",
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key        = SSL_DH_PARAM_OPT,
          .voltype     = "rpc-transport/socket",
          .option     = "!ssl-dh-param",
          .op_version = GD_OP_VERSION_3_7_4,
        },
        { .key        = SSL_EC_CURVE_OPT,
          .voltype     = "rpc-transport/socket",
          .option     = "!ssl-ec-curve",
          .op_version = GD_OP_VERSION_3_7_4,
        },
        { .key         = "transport.address-family",
          .voltype     = "protocol/server",
          .option      = "!address-family",
          .op_version  = GD_OP_VERSION_3_7_4,
          .type        = NO_DOC,
        },

        /* Performance xlators enable/disbable options */
        { .key         = "performance.write-behind",
          .voltype     = "performance/write-behind",
          .option      = "!perf",
          .value       = "on",
          .op_version  = 1,
          .description = "enable/disable write-behind translator in the "
                         "volume.",
          .flags       = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT
        },
        { .key         = "performance.read-ahead",
          .voltype     = "performance/read-ahead",
          .option      = "!perf",
          .value       = "on",
          .op_version  = 1,
          .description = "enable/disable read-ahead translator in the volume.",
          .flags       = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT
        },
        { .key         = "performance.readdir-ahead",
          .voltype     = "performance/readdir-ahead",
          .option      = "!perf",
          .value       = "on",
          .op_version  = 3,
          .description = "enable/disable readdir-ahead translator in the volume.",
          .flags       = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT
        },
        { .key         = "performance.io-cache",
          .voltype     = "performance/io-cache",
          .option      = "!perf",
          .value       = "on",
          .op_version  = 1,
          .description = "enable/disable io-cache translator in the volume.",
          .flags       = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "performance.quick-read",
          .voltype     = "performance/quick-read",
          .option      = "!perf",
          .value       = "on",
          .op_version  = 1,
          .description = "enable/disable quick-read translator in the volume.",
          .flags       = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT

        },
        { .key         = "performance.open-behind",
          .voltype     = "performance/open-behind",
          .option      = "!perf",
          .value       = "on",
          .op_version  = 2,
          .description = "enable/disable open-behind translator in the volume.",
          .flags       = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT

        },
        { .key         = "performance.stat-prefetch",
          .voltype     = "performance/md-cache",
          .option      = "!perf",
          .value       = "on",
          .op_version  = 1,
          .description = "enable/disable meta-data caching translator in the "
                         "volume.",
          .flags       = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT
        },
        { .key         = "performance.client-io-threads",
          .voltype     = "performance/io-threads",
          .option      = "!perf",
          .value       = "on",
          .op_version  = 1,
          .description = "enable/disable io-threads translator in the client "
                         "graph of volume.",
          .flags       = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT
        },
        { .key         = "performance.nfs.write-behind",
          .voltype     = "performance/write-behind",
          .option      = "!nfsperf",
          .value       = "on",
          .op_version  = 1,
          .description = "enable/disable write-behind translator in the volume",
          .flags       = OPT_FLAG_XLATOR_OPT
        },
        { .key        = "performance.nfs.read-ahead",
          .voltype    = "performance/read-ahead",
          .option     = "!nfsperf",
          .value      = "off",
          .type       = NO_DOC,
          .op_version = 1,
          .flags      = OPT_FLAG_XLATOR_OPT
        },
        { .key        = "performance.nfs.io-cache",
          .voltype    = "performance/io-cache",
          .option     = "!nfsperf",
          .value      = "off",
          .type       = NO_DOC,
          .op_version = 1,
          .flags      = OPT_FLAG_XLATOR_OPT
        },
        { .key        = "performance.nfs.quick-read",
          .voltype    = "performance/quick-read",
          .option     = "!nfsperf",
          .value      = "off",
          .type       = NO_DOC,
          .op_version = 1,
          .flags      = OPT_FLAG_XLATOR_OPT
        },
        { .key        = "performance.nfs.stat-prefetch",
          .voltype    = "performance/md-cache",
          .option     = "!nfsperf",
          .value      = "off",
          .type       = NO_DOC,
          .op_version = 1,
          .flags      = OPT_FLAG_XLATOR_OPT
        },
        { .key        = "performance.nfs.io-threads",
          .voltype    = "performance/io-threads",
          .option     = "!nfsperf",
          .value      = "off",
          .type       = NO_DOC,
          .op_version = 1,
          .flags      = OPT_FLAG_XLATOR_OPT
        },
        { .key        = "performance.force-readdirp",
          .voltype    = "performance/md-cache",
          .option     = "force-readdirp",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "performance.cache-invalidation",
          .voltype    = "performance/md-cache",
          .option     = "cache-invalidation",
          .op_version = GD_OP_VERSION_3_9_0,
          .flags      = OPT_FLAG_CLIENT_OPT
        },

        /* Feature translators */
        { .key         = "features.uss",
          .voltype     = "features/snapview-server",
          .op_version  = GD_OP_VERSION_3_6_0,
          .value       = "off",
          .flags       = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT,
          .validate_fn = validate_uss,
          .description = "enable/disable User Serviceable Snapshots on the "
                         "volume."
        },

        { .key         = "features.snapshot-directory",
          .voltype     = "features/snapview-client",
          .op_version  = GD_OP_VERSION_3_6_0,
          .value       = ".snaps",
          .flags       = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT,
          .validate_fn = validate_uss_dir,
          .description = "Entry point directory for entering snapshot world. "
                         "Value can have only [0-9a-z-_] and starts with "
                         "dot (.) and cannot exceed 255 character"
        },

        { .key         = "features.show-snapshot-directory",
          .voltype     = "features/snapview-client",
          .op_version  = GD_OP_VERSION_3_6_0,
          .value       = "off",
          .flags       = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT,
          .description = "show entry point in readdir output of "
                         "snapdir-entry-path which is set by samba"
        },

#ifdef HAVE_LIB_Z
        /* Compressor-decompressor xlator options
         * defaults used from xlator/features/compress/src/cdc.h
         */
        { .key         = "network.compression",
          .voltype     = "features/cdc",
          .option      = "!feat",
          .value       = "off",
          .op_version  = 3,
          .description = "enable/disable network compression translator",
          .flags       = OPT_FLAG_XLATOR_OPT
        },
        { .key         = "network.compression.window-size",
          .voltype     = "features/cdc",
          .option      = "window-size",
          .op_version  = 3
        },
        { .key         = "network.compression.mem-level",
          .voltype     = "features/cdc",
          .option      = "mem-level",
          .op_version  = 3
        },
        { .key         = "network.compression.min-size",
          .voltype     = "features/cdc",
          .option      = "min-size",
          .op_version  = 3
        },
        { .key         = "network.compression.compression-level",
          .voltype     = "features/cdc",
          .option      = "compression-level",
          .op_version  = 3
        },
        { .key         = "network.compression.debug",
          .voltype     = "features/cdc",
          .option      = "debug",
          .type        = NO_DOC,
          .op_version  = 3
        },
#endif

        /* Quota xlator options */
        { .key           = VKEY_FEATURES_LIMIT_USAGE,
          .voltype       = "features/quota",
          .option        = "limit-set",
          .type          = NO_DOC,
          .op_version    = 1,
        },
        {
          .key           = "features.quota-timeout",
          .voltype       = "features/quota",
          .option        = "timeout",
          .value         = "0",
          .op_version    = 1,
          .validate_fn   = validate_quota,
        },
        { .key           = "features.default-soft-limit",
          .voltype       = "features/quota",
          .option        = "default-soft-limit",
          .type          = NO_DOC,
          .op_version    = 3,
        },
        { .key           = "features.soft-timeout",
          .voltype       = "features/quota",
          .option        = "soft-timeout",
          .type          = NO_DOC,
          .op_version    = 3,
        },
        { .key           = "features.hard-timeout",
          .voltype       = "features/quota",
          .option        = "hard-timeout",
          .type          = NO_DOC,
          .op_version    = 3,
        },
        { .key           = "features.alert-time",
          .voltype       = "features/quota",
          .option        = "alert-time",
          .type          = NO_DOC,
          .op_version    = 3,
        },
        { .key           = "features.quota-deem-statfs",
          .voltype       = "features/quota",
          .option        = "deem-statfs",
          .value         = "off",
          .type          = DOC,
          .op_version    = 2,
          .validate_fn   = validate_quota,
        },

        /* Marker xlator options */
        { .key         = VKEY_MARKER_XTIME,
          .voltype     = "features/marker",
          .option      = "xtime",
          .value       = "off",
          .type        = NO_DOC,
          .flags       = OPT_FLAG_FORCE,
          .op_version  = 1
        },
        { .key         = VKEY_MARKER_XTIME,
          .voltype     = "features/marker",
          .option      = "!xtime",
          .value       = "off",
          .type        = NO_DOC,
          .flags       = OPT_FLAG_FORCE,
          .op_version  = 1
        },
        { .key         = VKEY_MARKER_XTIME_FORCE,
          .voltype     = "features/marker",
          .option      = "gsync-force-xtime",
          .value       = "off",
          .type        = NO_DOC,
          .flags       = OPT_FLAG_FORCE,
          .op_version  = 2
        },
        { .key         = VKEY_MARKER_XTIME_FORCE,
          .voltype     = "features/marker",
          .option      = "!gsync-force-xtime",
          .value       = "off",
          .type        = NO_DOC,
          .flags       = OPT_FLAG_FORCE,
          .op_version  = 2
        },
        { .key         = VKEY_FEATURES_QUOTA,
          .voltype     = "features/marker",
          .option      = "quota",
          .value       = "off",
          .type        = NO_DOC,
          .flags       = OPT_FLAG_NEVER_RESET,
          .op_version  = 1
        },
        { .key         = VKEY_FEATURES_INODE_QUOTA,
          .voltype     = "features/marker",
          .option      = "inode-quota",
          .value       = "off",
          .type        = NO_DOC,
          .flags       = OPT_FLAG_NEVER_RESET,
          .op_version  = 1
        },
        { .key         = VKEY_FEATURES_BITROT,
          .voltype     = "features/bit-rot",
          .option      = "bitrot",
          .value       = "disable",
          .type        = NO_DOC,
          .flags       = OPT_FLAG_FORCE,
          .op_version  = GD_OP_VERSION_3_7_0
        },

        /* Debug xlators options */
        { .key        = "debug.trace",
          .voltype    = "debug/trace",
          .option     = "!debug",
          .value      = "off",
          .type       = NO_DOC,
          .op_version = 1,
          .flags      = OPT_FLAG_XLATOR_OPT
        },
        { .key         = "debug.log-history",
          .voltype     = "debug/trace",
          .option      = "log-history",
          .type        = NO_DOC,
          .op_version  = 2
        },
        { .key         = "debug.log-file",
          .voltype     = "debug/trace",
          .option      = "log-file",
          .type        = NO_DOC,
          .op_version  = 2
        },
        { .key         = "debug.exclude-ops",
          .voltype     = "debug/trace",
          .option      = "exclude-ops",
          .type        = NO_DOC,
          .op_version  = 2
        },
        { .key         = "debug.include-ops",
          .voltype     = "debug/trace",
          .option      = "include-ops",
          .type        = NO_DOC,
          .op_version  = 2
        },
        { .key        = "debug.error-gen",
          .voltype    = "debug/error-gen",
          .option     = "!debug",
          .value      = "off",
          .type       = NO_DOC,
          .op_version = 1,
          .flags      = OPT_FLAG_XLATOR_OPT
        },
        { .key         = "debug.error-failure",
          .voltype     = "debug/error-gen",
          .option      = "failure",
          .type        = NO_DOC,
          .op_version  = 3
        },
        { .key         = "debug.error-number",
          .voltype     = "debug/error-gen",
          .option      = "error-no",
          .type        = NO_DOC,
          .op_version  = 3
        },
        { .key         = "debug.random-failure",
          .voltype     = "debug/error-gen",
          .option      = "random-failure",
          .type        = NO_DOC,
          .op_version  = 3
        },
        { .key         = "debug.error-fops",
          .voltype     = "debug/error-gen",
          .option      = "enable",
          .type        = NO_DOC,
          .op_version  = 3
        },


        /* NFS xlator options */
        { .key         = "nfs.enable-ino32",
          .voltype     = "nfs/server",
          .option      = "nfs.enable-ino32",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.mem-factor",
          .voltype     = "nfs/server",
          .option      = "nfs.mem-factor",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.export-dirs",
          .voltype     = "nfs/server",
          .option      = "nfs3.export-dirs",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.export-volumes",
          .voltype     = "nfs/server",
          .option      = "nfs3.export-volumes",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.addr-namelookup",
          .voltype     = "nfs/server",
          .option      = "rpc-auth.addr.namelookup",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.dynamic-volumes",
          .voltype     = "nfs/server",
          .option      = "nfs.dynamic-volumes",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.register-with-portmap",
          .voltype     = "nfs/server",
          .option      = "rpc.register-with-portmap",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.outstanding-rpc-limit",
          .voltype     = "nfs/server",
          .option      = "rpc.outstanding-rpc-limit",
          .type        = GLOBAL_DOC,
          .op_version  = 3
        },
        { .key         = "nfs.port",
          .voltype     = "nfs/server",
          .option      = "nfs.port",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.rpc-auth-unix",
          .voltype     = "nfs/server",
          .option      = "!rpc-auth.auth-unix.*",
          .op_version  = 1
        },
        { .key         = "nfs.rpc-auth-null",
          .voltype     = "nfs/server",
          .option      = "!rpc-auth.auth-null.*",
          .op_version  = 1
        },
        { .key         = "nfs.rpc-auth-allow",
          .voltype     = "nfs/server",
          .option      = "!rpc-auth.addr.*.allow",
          .op_version  = 1
        },
        { .key         = "nfs.rpc-auth-reject",
          .voltype     = "nfs/server",
          .option      = "!rpc-auth.addr.*.reject",
          .op_version  = 1
        },
        { .key         = "nfs.ports-insecure",
          .voltype     = "nfs/server",
          .option      = "!rpc-auth.ports.*.insecure",
          .op_version  = 1
        },
        { .key         = "nfs.transport-type",
          .voltype     = "nfs/server",
          .option      = "!nfs.transport-type",
          .op_version  = 1,
          .description = "Specifies the nfs transport type. Valid "
                         "transport types are 'tcp' and 'rdma'."
        },
        { .key         = "nfs.trusted-sync",
          .voltype     = "nfs/server",
          .option      = "!nfs3.*.trusted-sync",
          .op_version  = 1
        },
        { .key         = "nfs.trusted-write",
          .voltype     = "nfs/server",
          .option      = "!nfs3.*.trusted-write",
          .op_version  = 1
        },
        { .key         = "nfs.volume-access",
          .voltype     = "nfs/server",
          .option      = "!nfs3.*.volume-access",
          .op_version  = 1
        },
        { .key         = "nfs.export-dir",
          .voltype     = "nfs/server",
          .option      = "!nfs3.*.export-dir",
          .op_version  = 1
        },
        { .key         = NFS_DISABLE_MAP_KEY,
          .voltype     = "nfs/server",
          .option      = "!nfs-disable",
          .value       = "on",
          .op_version  = 1
        },
        { .key         = "nfs.nlm",
          .voltype     = "nfs/server",
          .option      = "nfs.nlm",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.acl",
          .voltype     = "nfs/server",
          .option      = "nfs.acl",
          .type        = GLOBAL_DOC,
          .op_version  = 3
        },
        { .key         = "nfs.mount-udp",
          .voltype     = "nfs/server",
          .option      = "nfs.mount-udp",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.mount-rmtab",
          .voltype     = "nfs/server",
          .option      = "nfs.mount-rmtab",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.rpc-statd",
          .voltype     = "nfs/server",
          .option      = "nfs.rpc-statd",
          .type        = NO_DOC,
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key         = "nfs.log-level",
          .voltype     = "nfs/server",
          .option      = "nfs.log-level",
          .type        = NO_DOC,
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key         = "nfs.server-aux-gids",
          .voltype     = "nfs/server",
          .option      = "nfs.server-aux-gids",
          .type        = NO_DOC,
          .op_version  = 2
        },
        { .key         = "nfs.drc",
          .voltype     = "nfs/server",
          .option      = "nfs.drc",
          .type        = GLOBAL_DOC,
          .op_version  = 3
        },
        { .key         = "nfs.drc-size",
          .voltype     = "nfs/server",
          .option      = "nfs.drc-size",
          .type        = GLOBAL_DOC,
          .op_version  = 3
        },
        { .key         = "nfs.read-size",
          .voltype     = "nfs/server",
          .option      = "nfs3.read-size",
          .type        = GLOBAL_DOC,
          .op_version  = 3
        },
        { .key         = "nfs.write-size",
          .voltype     = "nfs/server",
          .option      = "nfs3.write-size",
          .type        = GLOBAL_DOC,
          .op_version  = 3
        },
        { .key         = "nfs.readdir-size",
          .voltype     = "nfs/server",
          .option      = "nfs3.readdir-size",
          .type        = GLOBAL_DOC,
          .op_version  = 3
        },
        { .key         = "nfs.rdirplus",
          .voltype     = "nfs/server",
          .option      = "nfs.rdirplus",
          .type        = GLOBAL_DOC,
          .op_version  = GD_OP_VERSION_3_7_12,
          .description = "When this option is set to off NFS falls back to "
                         "standard readdir instead of readdirp"
        },

        /* Cli options for Export authentication on nfs mount */
        { .key         = "nfs.exports-auth-enable",
          .voltype     = "nfs/server",
          .option      = "nfs.exports-auth-enable",
          .type        = GLOBAL_DOC,
          .op_version  = GD_OP_VERSION_3_7_0
        },
        { .key         = "nfs.auth-refresh-interval-sec",
          .voltype     = "nfs/server",
          .option      = "nfs.auth-refresh-interval-sec",
          .type        = GLOBAL_DOC,
          .op_version  = GD_OP_VERSION_3_7_0
        },
        { .key         = "nfs.auth-cache-ttl-sec",
          .voltype     = "nfs/server",
          .option      = "nfs.auth-cache-ttl-sec",
          .type        = GLOBAL_DOC,
          .op_version  = GD_OP_VERSION_3_7_0
        },

        /* Other options which don't fit any place above */
        { .key        = "features.read-only",
          .voltype    = "features/read-only",
          .option     = "read-only",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT
        },
        { .key         = "features.worm",
          .voltype     = "features/worm",
          .option      = "worm",
          .value       = "off",
          .validate_fn = validate_worm,
          .op_version  = 2,
          .flags       = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT
        },
        { .key         = "features.worm-file-level",
          .voltype     = "features/worm",
          .option      = "worm-file-level",
          .value       = "off",
          .validate_fn = validate_worm,
          .op_version  = GD_OP_VERSION_3_8_0,
          .flags      = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT
        },
        { .key         = "features.default-retention-period",
          .voltype     = "features/worm",
          .option      = "default-retention-period",
          .validate_fn = validate_worm_period,
          .op_version  = GD_OP_VERSION_3_8_0,
        },
        { .key         = "features.retention-mode",
          .voltype     = "features/worm",
          .option      = "retention-mode",
          .validate_fn = validate_reten_mode,
          .op_version  = GD_OP_VERSION_3_8_0,
        },
        { .key         = "features.auto-commit-period",
          .voltype     = "features/worm",
          .option      = "auto-commit-period",
          .validate_fn = validate_worm_period,
          .op_version  = GD_OP_VERSION_3_8_0,
        },
        { .key         = "storage.linux-aio",
          .voltype     = "storage/posix",
          .op_version  = 1
        },
        { .key         = "storage.batch-fsync-mode",
          .voltype     = "storage/posix",
          .op_version  = 3
        },
        { .key         = "storage.batch-fsync-delay-usec",
          .voltype     = "storage/posix",
          .op_version  = 3
        },
        { .key         = "storage.xattr-user-namespace-mode",
          .voltype     = "storage/posix",
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key         = "storage.owner-uid",
          .voltype     = "storage/posix",
          .option      = "brick-uid",
          .op_version  = 1
        },
        { .key         = "storage.owner-gid",
          .voltype     = "storage/posix",
          .option      = "brick-gid",
          .op_version  = 1
        },
        { .key         = "storage.node-uuid-pathinfo",
          .voltype     = "storage/posix",
          .op_version  = 3
        },
        { .key         = "storage.health-check-interval",
          .voltype     = "storage/posix",
          .op_version  = 3
        },
        { .option      = "update-link-count-parent",
          .key         = "storage.build-pgfid",
          .voltype     = "storage/posix",
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key         = "storage.bd-aio",
          .voltype     = "storage/bd",
          .op_version  = 3
        },
        { .key        = "config.memory-accounting",
          .voltype    = "mgmt/glusterd",
          .option     = "!config",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "config.transport",
          .voltype     = "mgmt/glusterd",
          .option      = "!config",
          .op_version  = 2
        },
        { .key         = GLUSTERD_QUORUM_TYPE_KEY,
          .voltype     = "mgmt/glusterd",
          .value       = "off",
          .op_version  = 2
        },
        { .key         = GLUSTERD_QUORUM_RATIO_KEY,
          .voltype     = "mgmt/glusterd",
          .value       = "0",
          .op_version  = 2
        },
        /* changelog translator - global tunables */
        { .key         = "changelog.changelog",
          .voltype     = "features/changelog",
          .type        = NO_DOC,
          .op_version  = 3
        },
        { .key         = "changelog.changelog-dir",
          .voltype     = "features/changelog",
          .type        = NO_DOC,
          .op_version  = 3
        },
        { .key         = "changelog.encoding",
          .voltype     = "features/changelog",
          .type        = NO_DOC,
          .op_version  = 3
        },
        { .key         = "changelog.rollover-time",
          .voltype     = "features/changelog",
          .type        = NO_DOC,
          .op_version  = 3
        },
        { .key         = "changelog.fsync-interval",
          .voltype     = "features/changelog",
          .type        = NO_DOC,
          .op_version  = 3
        },
        { .key         = "changelog.changelog-barrier-timeout",
          .voltype     = "features/changelog",
          .value       = BARRIER_TIMEOUT,
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key         = "changelog.capture-del-path",
          .voltype     = "features/changelog",
          .type        = NO_DOC,
          .op_version  = 3
        },
        { .key         = "features.barrier",
          .voltype     = "features/barrier",
          .value       = "disable",
          .type        = NO_DOC,
          .op_version  = GD_OP_VERSION_3_7_0,
        },
        { .key         = "features.barrier-timeout",
          .voltype     = "features/barrier",
          .value       = BARRIER_TIMEOUT,
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key         = "cluster.op-version",
          .voltype     = "mgmt/glusterd",
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        {
          .key         = "cluster.max-op-version",
          .voltype     = "mgmt/glusterd",
          .op_version  = GD_OP_VERSION_3_10_0,
        },
        /*Trash translator options */
        { .key         = "features.trash",
          .voltype     = "features/trash",
          .op_version  = GD_OP_VERSION_3_7_0,
        },
        { .key         = "features.trash-dir",
          .voltype     = "features/trash",
          .op_version  = GD_OP_VERSION_3_7_0,
        },
        { .key         = "features.trash-eliminate-path",
          .voltype     = "features/trash",
          .op_version  = GD_OP_VERSION_3_7_0,
        },
        { .key         = "features.trash-max-filesize",
          .voltype     = "features/trash",
          .op_version  = GD_OP_VERSION_3_7_0,
        },
        { .key         = "features.trash-internal-op",
          .voltype     = "features/trash",
          .op_version  = GD_OP_VERSION_3_7_0,
        },
        { .key         = GLUSTERD_SHARED_STORAGE_KEY,
          .voltype     = "mgmt/glusterd",
          .value       = "disable",
          .type        = GLOBAL_DOC,
          .op_version  = GD_OP_VERSION_3_7_1,
          .description = "Create and mount the shared storage volume"
                         "(gluster_shared_storage) at "
                         "/var/run/gluster/shared_storage on enabling this "
                         "option. Unmount and delete the shared storage volume "
                         " on disabling this option."
        },
#if USE_GFDB /* no GFDB means tiering is disabled */
        /* tier translator - global tunables */
        { .key         = "cluster.write-freq-threshold",
          .voltype     = "cluster/tier",
          .value       = "0",
          .option      = "write-freq-threshold",
          .op_version  = GD_OP_VERSION_3_7_0,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .validate_fn = validate_tier_thresholds,
          .description = "Defines the number of writes, in a promotion/demotion"
                         " cycle, that would mark a file HOT for promotion. Any"
                         " file that has write hits less than this value will "
                         "be considered as COLD and will be demoted."
        },
        { .key         = "cluster.read-freq-threshold",
          .voltype     = "cluster/tier",
          .value       = "0",
          .option      = "read-freq-threshold",
          .op_version  = GD_OP_VERSION_3_7_0,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .validate_fn = validate_tier_thresholds,
          .description = "Defines the number of reads, in a promotion/demotion "
                         "cycle, that would mark a file HOT for promotion. Any "
                         "file that has read hits less than this value will be "
                         "considered as COLD and will be demoted."
        },
        { .key         = "cluster.tier-pause",
          .voltype     = "cluster/tier",
          .option      = "tier-pause",
          .op_version  = GD_OP_VERSION_3_7_6,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .validate_fn = validate_tier,
        },
        { .key         = "cluster.tier-promote-frequency",
          .voltype     = "cluster/tier",
          .value       = "120",
          .option      = "tier-promote-frequency",
          .op_version  = GD_OP_VERSION_3_7_0,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .validate_fn = validate_tier,
        },
        { .key         = "cluster.tier-demote-frequency",
          .voltype     = "cluster/tier",
          .value       = "3600",
          .option      = "tier-demote-frequency",
          .op_version  = GD_OP_VERSION_3_7_0,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .validate_fn = validate_tier,
        },
        { .key         = "cluster.watermark-hi",
          .voltype     = "cluster/tier",
          .value       = "90",
          .option      = "watermark-hi",
          .op_version  = GD_OP_VERSION_3_7_6,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .validate_fn = validate_tier,
          .description = "Upper % watermark for promotion. If hot tier fills"
          " above this percentage, no promotion will happen and demotion will "
          "happen with high probability."
        },
        { .key         = "cluster.watermark-low",
          .voltype     = "cluster/tier",
          .value       = "75",
          .option      = "watermark-low",
          .op_version  = GD_OP_VERSION_3_7_6,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .validate_fn = validate_tier,
          .description = "Lower % watermark. If hot tier is less "
          "full than this, promotion will happen and demotion will not happen. "
          "If greater than this, promotion/demotion will happen at a probability "
          "relative to how full the hot tier is."
        },
        { .key         = "cluster.tier-mode",
          .voltype     = "cluster/tier",
          .option      = "tier-mode",
          .value       = "cache",
          .op_version  = GD_OP_VERSION_3_7_6,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .validate_fn = validate_tier,
          .description = "Either 'test' or 'cache'. Test mode periodically"
          " demotes or promotes files automatically based on access."
          " Cache mode does so based on whether the cache is full or not,"
          " as specified with watermarks."
        },
        { .key         = "cluster.tier-max-promote-file-size",
          .voltype     = "cluster/tier",
          .option      = "tier-max-promote-file-size",
          .value       = "0",
          .op_version  = GD_OP_VERSION_3_7_10,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .validate_fn = validate_tier,
          .description = "The maximum file size in bytes that is promoted. If 0, there"
          " is no maximum size (default)."
        },
        { .key         = "cluster.tier-max-mb",
          .voltype     = "cluster/tier",
          .option      = "tier-max-mb",
          .value       = "4000",
          .op_version  = GD_OP_VERSION_3_7_6,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .validate_fn = validate_tier,
          .description = "The maximum number of MB that may be migrated"
          " in any direction in a given cycle by a single node."
        },
        { .key         = "cluster.tier-max-files",
          .voltype     = "cluster/tier",
          .option      = "tier-max-files",
          .value       = "10000",
          .op_version  = GD_OP_VERSION_3_7_6,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .validate_fn = validate_tier,
          .description = "The maximum number of files that may be migrated"
          " in any direction in a given cycle by a single node."
        },
        { .key         = "cluster.tier-query-limit",
          .voltype     = "cluster/tier",
          .option      = "tier-query-limit",
          .value       = "100",
          .op_version  = GD_OP_VERSION_3_9_1,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .validate_fn = validate_tier,
          .type        = NO_DOC,
          .description = "The maximum number of files that may be migrated "
                         "during an emergency demote. An emergency condition "
                         "is flagged when writes breach the hi-watermark."
        },
        { .key         = "cluster.tier-compact",
          .voltype     = "cluster/tier",
          .option      = "tier-compact",
          .value       = "on",
          .op_version  = GD_OP_VERSION_3_9_0,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .validate_fn = validate_tier,
          .description = "Activate or deactivate the compaction of the DB"
          " for the volume's metadata."
        },
        { .key         = "cluster.tier-hot-compact-frequency",
          .voltype     = "cluster/tier",
          .value       = "604800",
          .option      = "tier-hot-compact-frequency",
          .op_version  = GD_OP_VERSION_3_9_0,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .validate_fn = validate_tier,
        },
        { .key         = "cluster.tier-cold-compact-frequency",
          .voltype     = "cluster/tier",
          .value       = "604800",
          .option      = "tier-cold-compact-frequency",
          .op_version  = GD_OP_VERSION_3_9_0,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .validate_fn = validate_tier,
        },
        { .key         = "features.ctr-enabled",
          .voltype     = "features/changetimerecorder",
          .value       = "off",
          .option      = "ctr-enabled",
          .op_version  = GD_OP_VERSION_3_7_0,
          .description = "Enable CTR xlator"
        },
        { .key         = "features.record-counters",
          .voltype     = "features/changetimerecorder",
          .value       = "off",
          .option      = "record-counters",
          .op_version  = GD_OP_VERSION_3_7_0,
          .validate_fn = validate_tier_counters,
          .description = "Its a Change Time Recorder Xlator option to "
                         "enable recording write "
                         "and read heat counters. The default is disabled. "
                         "If enabled, \"cluster.write-freq-threshold\" and "
                         "\"cluster.read-freq-threshold\" defined the number "
                         "of writes (or reads) to a given file are needed "
                         "before triggering migration."
        },
        { .key         = "features.ctr-record-metadata-heat",
          .voltype     = "features/changetimerecorder",
          .value       = "off",
          .option      = "ctr-record-metadata-heat",
          .op_version  = GD_OP_VERSION_3_7_0,
          .type        = NO_DOC,
          .description = "Its a Change Time Recorder Xlator option to "
                         "enable recording write heat on metadata of the file. "
                         "The default is disabled. "
                         "Metadata is inode attributes like atime, mtime,"
                         " permissions etc and "
                         "extended attributes of a file ."
        },
        { .key         = "features.ctr_link_consistency",
          .voltype     = "features/changetimerecorder",
          .value       = "off",
          .option      = "ctr_link_consistency",
          .op_version  = GD_OP_VERSION_3_7_0,
          .type        = NO_DOC,
          .description = "Enable a crash consistent way of recording hardlink "
                         "updates by Change Time Recorder Xlator. "
                         "When recording in a crash "
                         "consistent way the data operations will "
                         "experience more latency."
        },
        { .key         = "features.ctr_lookupheal_link_timeout",
          .voltype     = "features/changetimerecorder",
          .value       = "300",
          .option      = "ctr_lookupheal_link_timeout",
          .op_version  = GD_OP_VERSION_3_7_2,
          .type        = NO_DOC,
          .description = "Defines the expiry period of in-memory "
                         "hardlink of an inode,"
                         "used by lookup heal in Change Time Recorder."
                         "Once the expiry period"
                         "hits an attempt to heal the database per "
                         "hardlink is done and the "
                         "in-memory hardlink period is reset"
        },
        { .key         = "features.ctr_lookupheal_inode_timeout",
          .voltype     = "features/changetimerecorder",
          .value       = "300",
          .option      = "ctr_lookupheal_inode_timeout",
          .op_version  = GD_OP_VERSION_3_7_2,
          .type        = NO_DOC,
          .description = "Defines the expiry period of in-memory inode,"
                         "used by lookup heal in Change Time Recorder. "
                         "Once the expiry period"
                         "hits an attempt to heal the database per "
                         "inode is done"
        },
        { .key         = "features.ctr-sql-db-cachesize",
          .voltype     = "features/changetimerecorder",
          .value       = "12500",
          .option      = "sql-db-cachesize",
          .validate_fn = validate_ctr_sql_params,
          .op_version  = GD_OP_VERSION_3_7_7,
          .description = "Defines the cache size of the sqlite database of "
                         "changetimerecorder xlator."
                         "The input to this option is in pages."
                         "Each page is 4096 bytes. Default value is 12500 "
                         "pages."
                         "The max value is 262144 pages i.e 1 GB and "
                         "the min value is 1000 pages i.e ~ 4 MB. "
        },
        { .key         = "features.ctr-sql-db-wal-autocheckpoint",
          .voltype     = "features/changetimerecorder",
          .value       = "25000",
          .option      = "sql-db-wal-autocheckpoint",
          .validate_fn = validate_ctr_sql_params,
          .op_version  = GD_OP_VERSION_3_7_7,
          .description = "Defines the autocheckpoint of the sqlite database of "
                         " changetimerecorder. "
                         "The input to this option is in pages. "
                         "Each page is 4096 bytes. Default value is 25000 "
                         "pages."
                         "The max value is 262144 pages i.e 1 GB and "
                         "the min value is 1000 pages i.e ~4 MB."
        },
#endif /* USE_GFDB */
        { .key         = "locks.trace",
          .voltype     = "features/locks",
          .op_version  = GD_OP_VERSION_3_7_0,
        },
        { .key         = "locks.mandatory-locking",
          .voltype     = "features/locks",
          .op_version  = GD_OP_VERSION_3_8_0,
          .validate_fn = validate_mandatory_locking,
        },
        { .key           = "cluster.disperse-self-heal-daemon",
          .voltype       = "cluster/disperse",
          .type          = NO_DOC,
          .option        = "self-heal-daemon",
          .op_version    = GD_OP_VERSION_3_7_0,
          .validate_fn   = validate_disperse_heal_enable_disable
        },
        { .key           = "cluster.quorum-reads",
          .voltype       = "cluster/replicate",
          .op_version    = GD_OP_VERSION_3_7_0,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "client.bind-insecure",
          .voltype    = "protocol/client",
          .option     = "client-bind-insecure",
          .type       = NO_DOC,
          .op_version = GD_OP_VERSION_3_7_0,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "ganesha.enable",
          .voltype     = "features/ganesha",
          .value       = "off",
          .option      = "ganesha.enable",
          .op_version  = GD_OP_VERSION_3_7_0,
        },
        { .key        = "features.shard",
          .voltype    = "features/shard",
          .value      = "off",
          .option     = "!shard",
          .op_version = GD_OP_VERSION_3_7_0,
          .description = "enable/disable sharding translator on the volume.",
          .flags      = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT
        },
        { .key        = "features.shard-block-size",
          .voltype    = "features/shard",
          .op_version = GD_OP_VERSION_3_7_0,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "features.scrub-throttle",
          .voltype    = "features/bit-rot",
          .value      = "lazy",
          .option     = "scrub-throttle",
          .op_version = GD_OP_VERSION_3_7_0,
          .type       = NO_DOC,
        },
        { .key        = "features.scrub-freq",
          .voltype    = "features/bit-rot",
          .value      = "biweekly",
          .option     = "scrub-frequency",
          .op_version = GD_OP_VERSION_3_7_0,
          .type       = NO_DOC,
        },
        { .key        = "features.scrub",
          .voltype    = "features/bit-rot",
          .option     = "scrubber",
          .op_version = GD_OP_VERSION_3_7_0,
          .flags      = OPT_FLAG_FORCE,
          .type       = NO_DOC,
        },
        { .key        = "features.expiry-time",
          .voltype    = "features/bit-rot",
          .value      = SIGNING_TIMEOUT,
          .option     = "expiry-time",
          .op_version = GD_OP_VERSION_3_7_0,
          .type       = NO_DOC,
        },
        /* Upcall translator options */
        { .key         = "features.cache-invalidation",
          .voltype     = "features/upcall",
          .value       = "off",
          .op_version  = GD_OP_VERSION_3_7_0,
        },
        { .key         = "features.cache-invalidation-timeout",
          .voltype     = "features/upcall",
          .op_version  = GD_OP_VERSION_3_7_0,
        },
        /* Lease translator options */
        { .key         = "features.leases",
          .voltype     = "features/leases",
          .value       = "off",
          .op_version  = GD_OP_VERSION_3_8_0,
        },
        { .key         = "features.lease-lock-recall-timeout",
          .voltype     = "features/leases",
          .op_version  = GD_OP_VERSION_3_8_0,
        },
        { .key         = "disperse.background-heals",
          .voltype     = "cluster/disperse",
          .op_version  = GD_OP_VERSION_3_7_3,
          .flags       = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "disperse.heal-wait-qlength",
          .voltype     = "cluster/disperse",
          .op_version  = GD_OP_VERSION_3_7_3,
          .flags       = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.heal-timeout",
          .voltype    = "cluster/disperse",
          .option     = "!heal-timeout",
          .op_version  = GD_OP_VERSION_3_7_3,
          .type       = NO_DOC,
        },
        {
          .key         = "dht.force-readdirp",
          .voltype     = "cluster/distribute",
          .option      = "use-readdirp",
          .op_version  = GD_OP_VERSION_3_7_5,
          .flags       = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "disperse.read-policy",
          .voltype     = "cluster/disperse",
          .op_version  = GD_OP_VERSION_3_7_6,
          .flags       = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.shd-max-threads",
          .voltype    = "cluster/replicate",
          .op_version = GD_OP_VERSION_3_7_12,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.shd-wait-qlength",
          .voltype    = "cluster/replicate",
          .op_version = GD_OP_VERSION_3_7_12,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.locking-scheme",
          .voltype    = "cluster/replicate",
          .type       = DOC,
          .op_version = GD_OP_VERSION_3_7_12,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.granular-entry-heal",
          .voltype    = "cluster/replicate",
          .type       = DOC,
          .op_version = GD_OP_VERSION_3_8_0,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .option      = "revocation-secs",
          .key         = "features.locks-revocation-secs",
          .voltype     = "features/locks",
          .op_version  = GD_OP_VERSION_3_9_0,
        },
        { .option      = "revocation-clear-all",
          .key         = "features.locks-revocation-clear-all",
          .voltype     = "features/locks",
          .op_version  = GD_OP_VERSION_3_9_0,
        },
        { .option      = "revocation-max-blocked",
          .key         = "features.locks-revocation-max-blocked",
          .voltype     = "features/locks",
          .op_version  = GD_OP_VERSION_3_9_0,
        },
        { .option      = "monkey-unlocking",
          .key         = "features.locks-monkey-unlocking",
          .voltype     = "features/locks",
          .op_version  = GD_OP_VERSION_3_9_0,
          .type        = NO_DOC,
        },
        { .key        = "disperse.shd-max-threads",
          .voltype    = "cluster/disperse",
          .op_version = GD_OP_VERSION_3_9_0,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "disperse.shd-wait-qlength",
          .voltype    = "cluster/disperse",
          .op_version = GD_OP_VERSION_3_9_0,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "disperse.cpu-extensions",
          .voltype     = "cluster/disperse",
          .op_version  = GD_OP_VERSION_3_9_0,
          .flags       = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.use-compound-fops",
          .voltype    = "cluster/replicate",
          .value      = "off",
          .type       = DOC,
          .op_version = GD_OP_VERSION_3_8_4,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "performance.parallel-readdir",
          .voltype     = "performance/readdir-ahead",
          .option      = "parallel-readdir",
          .value       = "off",
          .type        = DOC,
          .op_version  = GD_OP_VERSION_3_10_0,
          .description = "If this option is enabled, the readdir operation is "
                         "performed parallely on all the bricks, thus improving"
                         " the performance of readdir. Note that the performance"
                         "improvement is higher in large clusters"
        },
	{ .key         = "performance.rda-request-size",
	  .voltype     = "performance/readdir-ahead",
          .option      = "rda-request-size",
          .value       = "131072",
          .flags       = OPT_FLAG_CLIENT_OPT,
          .type        = DOC,
          .op_version  = GD_OP_VERSION_3_9_1,
	},
	{ .key         = "performance.rda-low-wmark",
          .voltype     = "performance/readdir-ahead",
          .option      = "rda-low-wmark",
          .type        = NO_DOC,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .op_version  = GD_OP_VERSION_3_9_1,
	},
	{ .key         = "performance.rda-high-wmark",
          .voltype     = "performance/readdir-ahead",
          .type        = NO_DOC,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .op_version  = GD_OP_VERSION_3_9_1,
	},
        { .key         = "performance.rda-cache-limit",
          .voltype     = "performance/readdir-ahead",
          .value       = "10MB",
          .type        = DOC,
          .flags       = OPT_FLAG_CLIENT_OPT,
          .op_version  = GD_OP_VERSION_3_9_1,
        },

        /* Brick multiplexing options */
        { .key         = GLUSTERD_BRICK_MULTIPLEX_KEY,
          .voltype     = "mgmt/glusterd",
          .value       = "off",
          .op_version  = GD_OP_VERSION_3_10_0
        },
        { .key         = NULL
        }
};
