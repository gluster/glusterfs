/*
Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
This file is part of GlusterFS.

This file is licensed to you under your choice of the GNU Lesser
General Public License, version 3 or any later version (LGPLv3 or
later), or the GNU General Public License, version 2 (GPLv2), in all
cases as published by the Free Software Foundation.
*/

#include <glusterfs/syscall.h>
#include "glusterd-volgen.h"
#include "glusterd-utils.h"

static int
validate_cache_max_min_size(glusterd_volinfo_t *volinfo, dict_t *dict,
                            char *key, char *value, char **op_errstr)
{
    char *current_max_value = NULL;
    char *current_min_value = NULL;
    char errstr[2048] = "";
    glusterd_conf_t *priv = NULL;
    int ret = 0;
    uint64_t max_value = 0;
    uint64_t min_value = 0;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    if ((!strcmp(key, "performance.cache-min-file-size")) ||
        (!strcmp(key, "cache-min-file-size"))) {
        glusterd_volinfo_get(volinfo, "performance.cache-max-file-size",
                             &current_max_value);
        if (current_max_value) {
            gf_string2bytesize_uint64(current_max_value, &max_value);
            gf_string2bytesize_uint64(value, &min_value);
            current_min_value = value;
        }
    } else if ((!strcmp(key, "performance.cache-max-file-size")) ||
               (!strcmp(key, "cache-max-file-size"))) {
        glusterd_volinfo_get(volinfo, "performance.cache-min-file-size",
                             &current_min_value);
        if (current_min_value) {
            gf_string2bytesize_uint64(current_min_value, &min_value);
            gf_string2bytesize_uint64(value, &max_value);
            current_max_value = value;
        }
    }

    if (min_value > max_value) {
        snprintf(errstr, sizeof(errstr),
                 "cache-min-file-size (%s) is greater than "
                 "cache-max-file-size (%s)",
                 current_min_value, current_max_value);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_CACHE_MINMAX_SIZE_INVALID,
               "%s", errstr);
        *op_errstr = gf_strdup(errstr);
        ret = -1;
        goto out;
    }

out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);

    return ret;
}

static int
validate_defrag_throttle_option(glusterd_volinfo_t *volinfo, dict_t *dict,
                                char *key, char *value, char **op_errstr)
{
    char errstr[2048] = "";
    int ret = 0;
    xlator_t *this = NULL;
    int thread_count = 0;
    long int cores_available = 0;

    this = THIS;
    GF_ASSERT(this);

    cores_available = sysconf(_SC_NPROCESSORS_ONLN);

    /* Throttle option should be one of lazy|normal|aggressive or a number
     * configured by user max up to the number of cores in the machine */

    if (!strcasecmp(value, "lazy") || !strcasecmp(value, "normal") ||
        !strcasecmp(value, "aggressive")) {
        ret = 0;
    } else if ((gf_string2int(value, &thread_count) == 0)) {
        if ((thread_count > 0) && (thread_count <= cores_available)) {
            ret = 0;
        } else {
            ret = -1;
            snprintf(errstr, sizeof(errstr),
                     "%s should be within"
                     " range of 0 and maximum number of cores "
                     "available (cores available - %ld)",
                     key, cores_available);

            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY, "%s",
                   errstr);

            *op_errstr = gf_strdup(errstr);
        }
    } else {
        ret = -1;
        snprintf(errstr, sizeof(errstr),
                 "%s should be "
                 "{lazy|normal|aggressive} or a number up to number of"
                 " cores available (cores available - %ld)",
                 key, cores_available);
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY, "%s",
               errstr);
        *op_errstr = gf_strdup(errstr);
    }

    gf_msg_debug(this->name, 0, "Returning %d", ret);

    return ret;
}

static int
validate_quota(glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
               char *value, char **op_errstr)
{
    char errstr[2048] = "";
    glusterd_conf_t *priv = NULL;
    int ret = 0;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    ret = glusterd_volinfo_get_boolean(volinfo, VKEY_FEATURES_QUOTA);
    if (ret == -1) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_QUOTA_GET_STAT_FAIL,
               "failed to get the quota status");
        goto out;
    }

    if (ret == _gf_false) {
        snprintf(errstr, sizeof(errstr), "Cannot set %s. Enable quota first.",
                 key);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_QUOTA_DISABLED, "%s",
               errstr);
        *op_errstr = gf_strdup(errstr);
        ret = -1;
        goto out;
    }

    ret = 0;
out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);

    return ret;
}

static int
validate_uss(glusterd_volinfo_t *volinfo, dict_t *dict, char *key, char *value,
             char **op_errstr)
{
    char errstr[2048] = "";
    int ret = 0;
    xlator_t *this = NULL;
    gf_boolean_t b = _gf_false;

    this = THIS;
    GF_ASSERT(this);

    ret = gf_string2boolean(value, &b);
    if (ret) {
        snprintf(errstr, sizeof(errstr),
                 "%s is not a valid boolean "
                 "value. %s expects a valid boolean value.",
                 value, key);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY, "%s", errstr);
        *op_errstr = gf_strdup(errstr);
        goto out;
    }
out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);

    return ret;
}

static int
validate_uss_dir(glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                 char *value, char **op_errstr)
{
    char errstr[2048] = "";
    int ret = -1;
    int i = 0;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);

    i = strlen(value);
    if (i > NAME_MAX) {
        snprintf(errstr, sizeof(errstr),
                 "value of %s exceedes %d "
                 "characters",
                 key, NAME_MAX);
        goto out;
    } else if (i < 2) {
        snprintf(errstr, sizeof(errstr),
                 "value of %s too short, "
                 "expects at least two characters",
                 key);
        goto out;
    }

    if (value[0] != '.') {
        snprintf(errstr, sizeof(errstr),
                 "%s expects value starting "
                 "with '.' ",
                 key);
        goto out;
    }

    for (i = 1; value[i]; i++) {
        if (isalnum(value[i]) || value[i] == '_' || value[i] == '-')
            continue;

        snprintf(errstr, sizeof(errstr),
                 "%s expects value to"
                 " contain only '0-9a-z-_'",
                 key);
        goto out;
    }

    ret = 0;
out:
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY, "%s",
               errstr);
        *op_errstr = gf_strdup(errstr);
    }

    gf_msg_debug(this->name, 0, "Returning %d", ret);

    return ret;
}

static int
validate_server_options(glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                        char *value, char **op_errstr)
{
    char errstr[2048] = "";
    xlator_t *this = NULL;
    int ret = -1;
    int origin_val = 0;

    this = THIS;
    GF_ASSERT(this);

    if (volinfo->status == GLUSTERD_STATUS_STARTED) {
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_VOL_SET_VALIDATION_INFO,
               "Please note that "
               "volume %s is started. This option will only get "
               "effected after a brick restart.",
               volinfo->volname);
    }

    ret = gf_string2int(value, &origin_val);
    if (ret) {
        snprintf(errstr, sizeof(errstr),
                 "%s is not a compatible "
                 "value. %s expects an integer value.",
                 value, key);
        ret = -1;
        goto out;
    }

    if (origin_val < 0) {
        snprintf(errstr, sizeof(errstr),
                 "%s is not a "
                 "compatible value. %s expects a positive"
                 "integer value.",
                 value, key);
        ret = -1;
        goto out;
    }

    ret = 0;
out:
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INCOMPATIBLE_VALUE,
               "%s", errstr);
        *op_errstr = gf_strdup(errstr);
    }

    return ret;
}

static int
validate_disperse(glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                  char *value, char **op_errstr)
{
    char errstr[2048] = "";
    int ret = -1;
    xlator_t *this = NULL;

    this = THIS;
    GF_VALIDATE_OR_GOTO("glusterd", this, out);

    if (volinfo->type != GF_CLUSTER_TYPE_DISPERSE) {
        snprintf(errstr, sizeof(errstr),
                 "Cannot set %s for a non-disperse volume.", key);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_DISPERSE, "%s",
               errstr);
        *op_errstr = gf_strdup(errstr);
        ret = -1;
        goto out;
    }
    ret = 0;

out:
    gf_msg_debug(ret == 0 ? THIS->name : "glusterd", 0, "Returning %d", ret);

    return ret;
}

static int
validate_replica(glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                 char *value, char **op_errstr)
{
    char errstr[2048] = "";
    int ret = 0;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);

    if (volinfo->replica_count == 1) {
        snprintf(errstr, sizeof(errstr),
                 "Cannot set %s for a non-replicate volume.", key);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_REPLICA, "%s",
               errstr);
        *op_errstr = gf_strdup(errstr);
        ret = -1;
        goto out;
    }

out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);

    return ret;
}

static int
validate_quorum_count(glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                      char *value, char **op_errstr)
{
    int ret = 0;
    xlator_t *this = NULL;
    int q_count = 0;

    this = THIS;
    GF_ASSERT(this);

    ret = gf_string2int(value, &q_count);
    if (ret) {
        gf_asprintf(op_errstr,
                    "%s is not an integer. %s expects a "
                    "valid integer value.",
                    value, key);
        goto out;
    }

    if (q_count < 1 || q_count > volinfo->replica_count) {
        gf_asprintf(op_errstr, "%d in %s %d is out of range [1 - %d]", q_count,
                    key, q_count, volinfo->replica_count);
        ret = -1;
    }

out:
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY, "%s",
               *op_errstr);
    }
    gf_msg_debug(this->name, 0, "Returning %d", ret);

    return ret;
}

static int
validate_subvols_per_directory(glusterd_volinfo_t *volinfo, dict_t *dict,
                               char *key, char *value, char **op_errstr)
{
    char errstr[2048] = "";
    glusterd_conf_t *priv = NULL;
    int ret = 0;
    int subvols = 0;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);
    priv = this->private;
    GF_ASSERT(priv);

    subvols = atoi(value);

    /* Checking if the subvols-per-directory exceed the total
       number of subvolumes. */
    if (subvols > volinfo->subvol_count) {
        snprintf(errstr, sizeof(errstr),
                 "subvols-per-directory(%d) is greater "
                 "than the number of subvolumes(%d).",
                 subvols, volinfo->subvol_count);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SUBVOLUMES_EXCEED, "%s.",
               errstr);
        *op_errstr = gf_strdup(errstr);
        ret = -1;
        goto out;
    }

out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);

    return ret;
}

static int
validate_replica_heal_enable_disable(glusterd_volinfo_t *volinfo, dict_t *dict,
                                     char *key, char *value, char **op_errstr)
{
    int ret = 0;

    if (!glusterd_is_volume_replicate(volinfo)) {
        gf_asprintf(op_errstr, "Volume %s is not of replicate type",
                    volinfo->volname);
        ret = -1;
    }

    return ret;
}

static int
validate_mandatory_locking(glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                           char *value, char **op_errstr)
{
    char errstr[2048] = "";
    int ret = 0;
    xlator_t *this = NULL;

    this = THIS;
    GF_ASSERT(this);

    if (strcmp(value, "off") != 0 && strcmp(value, "file") != 0 &&
        strcmp(value, "forced") != 0 && strcmp(value, "optimal") != 0) {
        snprintf(errstr, sizeof(errstr),
                 "Invalid option value '%s':"
                 " Available options are 'off', 'file', "
                 "'forced' or 'optimal'",
                 value);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY, "%s", errstr);
        *op_errstr = gf_strdup(errstr);
        ret = -1;
        goto out;
    }
out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);

    return ret;
}

static int
validate_disperse_heal_enable_disable(glusterd_volinfo_t *volinfo, dict_t *dict,
                                      char *key, char *value, char **op_errstr)
{
    int ret = 0;

    if (volinfo->type != GF_CLUSTER_TYPE_DISPERSE) {
        gf_asprintf(op_errstr, "Volume %s is not of disperse type",
                    volinfo->volname);
        ret = -1;
    }

    return ret;
}

static int
validate_lock_migration_option(glusterd_volinfo_t *volinfo, dict_t *dict,
                               char *key, char *value, char **op_errstr)
{
    char errstr[2048] = "";
    int ret = 0;
    xlator_t *this = NULL;
    gf_boolean_t b = _gf_false;

    this = THIS;
    GF_ASSERT(this);

    if (volinfo->replica_count > 1 || volinfo->disperse_count) {
        snprintf(errstr, sizeof(errstr),
                 "Lock migration is "
                 "a experimental feature. Currently works with"
                 " pure distribute volume only");
        ret = -1;

        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY, "%s",
               errstr);

        *op_errstr = gf_strdup(errstr);
        goto out;
    }

    ret = gf_string2boolean(value, &b);
    if (ret) {
        snprintf(errstr, sizeof(errstr),
                 "Invalid value"
                 " for volume set command. Use on/off only.");
        ret = -1;

        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY, "%s",
               errstr);

        *op_errstr = gf_strdup(errstr);

        goto out;
    }

    gf_msg_debug(this->name, 0, "Returning %d", ret);

out:
    return ret;
}

static int
validate_mux_limit(glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                   char *value, char **op_errstr)
{
    xlator_t *this = NULL;
    uint val = 0;
    int ret = -1;

    this = THIS;
    GF_VALIDATE_OR_GOTO("glusterd", this, out);

    if (!is_brick_mx_enabled()) {
        gf_asprintf(op_errstr,
                    "Brick-multiplexing is not enabled. "
                    "Please enable brick multiplexing before trying "
                    "to set this option.");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_WRONG_OPTS_SETTING, "%s",
               *op_errstr);
        goto out;
    }

    ret = gf_string2uint(value, &val);
    if (ret) {
        gf_asprintf(op_errstr,
                    "%s is not a valid count. "
                    "%s expects an unsigned integer.",
                    value, key);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY, "%s",
               *op_errstr);
    }

    if (val == 1) {
        gf_asprintf(op_errstr,
                    "Brick-multiplexing is enabled. "
                    "Please set this option to a value other than 1 "
                    "to make use of the brick-multiplexing feature.");
        ret = -1;
        goto out;
    }
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);

    return ret;
}

static int
validate_volume_per_thread_limit(glusterd_volinfo_t *volinfo, dict_t *dict,
                                 char *key, char *value, char **op_errstr)
{
    xlator_t *this = NULL;
    uint val = 0;
    int ret = -1;

    this = THIS;
    GF_VALIDATE_OR_GOTO("glusterd", this, out);

    if (!is_brick_mx_enabled()) {
        gf_asprintf(op_errstr,
                    "Brick-multiplexing is not enabled. "
                    "Please enable brick multiplexing before trying "
                    "to set this option.");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_WRONG_OPTS_SETTING, "%s",
               *op_errstr);
        goto out;
    }

    ret = gf_string2uint(value, &val);
    if (ret) {
        gf_asprintf(op_errstr,
                    "%s is not a valid count. "
                    "%s expects an unsigned integer.",
                    value, key);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY, "%s",
               *op_errstr);
    }

    if ((val < 5) || (val > 200)) {
        gf_asprintf(
            op_errstr,
            "Please set this option to a value between 5 and 200 to"
            "optimize processing large numbers of volumes in parallel.");
        ret = -1;
        goto out;
    }
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);

    return ret;
}

static int
validate_boolean(glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                 char *value, char **op_errstr)
{
    xlator_t *this = NULL;
    gf_boolean_t b = _gf_false;
    int ret = -1;

    this = THIS;
    GF_VALIDATE_OR_GOTO("glusterd", this, out);
    ret = gf_string2boolean(value, &b);
    if (ret) {
        gf_asprintf(op_errstr,
                    "%s is not a valid boolean value. %s "
                    "expects a valid boolean value.",
                    value, key);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY, "%s",
               *op_errstr);
    }
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);

    return ret;
}

static int
validate_disperse_quorum_count(glusterd_volinfo_t *volinfo, dict_t *dict,
                               char *key, char *value, char **op_errstr)
{
    int ret = -1;
    int quorum_count = 0;
    int data_count = 0;

    ret = gf_string2int(value, &quorum_count);
    if (ret) {
        gf_asprintf(op_errstr,
                    "%s is not an integer. %s expects a "
                    "valid integer value.",
                    value, key);
        goto out;
    }

    if (volinfo->type != GF_CLUSTER_TYPE_DISPERSE) {
        gf_asprintf(op_errstr, "Cannot set %s for a non-disperse volume.", key);
        ret = -1;
        goto out;
    }

    data_count = volinfo->disperse_count - volinfo->redundancy_count;
    if (quorum_count < data_count || quorum_count > volinfo->disperse_count) {
        gf_asprintf(op_errstr, "%d for %s is out of range [%d - %d]",
                    quorum_count, key, data_count, volinfo->disperse_count);
        ret = -1;
        goto out;
    }

    ret = 0;
out:
    return ret;
}

static int
validate_parallel_readdir(glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                          char *value, char **op_errstr)
{
    int ret = -1;

    ret = validate_boolean(volinfo, dict, key, value, op_errstr);
    if (ret)
        goto out;

    ret = glusterd_is_defrag_on(volinfo);
    if (ret) {
        gf_asprintf(op_errstr,
                    "%s option should be set "
                    "after rebalance is complete",
                    key);
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY, "%s",
               *op_errstr);
    }
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);

    return ret;
}

static int
validate_rda_cache_limit(glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                         char *value, char **op_errstr)
{
    int ret = 0;
    uint64_t rda_cache_size = 0;

    ret = gf_string2bytesize_uint64(value, &rda_cache_size);
    if (ret < 0)
        goto out;

    if (rda_cache_size <= (1 * GF_UNIT_GB))
        goto out;

    /* With release 3.11 the max value of rda_cache_limit is changed from
     * 1GB to INFINITY. If there are clients older than 3.11 and the value
     * of rda-cache-limit is set to > 1GB, the older clients will stop
     * working. Hence if a user is setting rda-cache-limit to > 1GB
     * ensure that all the clients are 3.11 or greater.
     */
    ret = glusterd_check_client_op_version_support(
        volinfo->volname, GD_OP_VERSION_3_11_0, op_errstr);
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);

    return ret;
}

static int
validate_worm_period(glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                     char *value, char **op_errstr)
{
    xlator_t *this = NULL;
    uint64_t period = -1;
    int ret = -1;

    this = THIS;
    GF_VALIDATE_OR_GOTO("glusterd", this, out);
    ret = gf_string2uint64(value, &period);
    if (ret) {
        gf_asprintf(op_errstr,
                    "%s is not a valid uint64_t value."
                    " %s expects a valid uint64_t value.",
                    value, key);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY, "%s",
               *op_errstr);
    }
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);

    return ret;
}

static int
validate_reten_mode(glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                    char *value, char **op_errstr)
{
    xlator_t *this = NULL;
    int ret = -1;

    this = THIS;
    GF_VALIDATE_OR_GOTO("glusterd", this, out);
    if ((strcmp(value, "relax") && strcmp(value, "enterprise"))) {
        gf_asprintf(op_errstr,
                    "The value of retention mode should be "
                    "either relax or enterprise. But the value"
                    " of %s is %s",
                    key, value);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY, "%s",
               *op_errstr);
        ret = -1;
        goto out;
    }
    ret = 0;
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);

    return ret;
}
static int
is_directory(const char *path)
{
    struct stat statbuf;
    if (sys_stat(path, &statbuf) != 0)
        return 0;
    return S_ISDIR(statbuf.st_mode);
}
static int
validate_statedump_path(glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                        char *value, char **op_errstr)
{
    xlator_t *this = NULL;
    this = THIS;
    GF_ASSERT(this);

    int ret = 0;
    if (!is_directory(value)) {
        gf_asprintf(op_errstr, "Failed: %s is not a directory", value);
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_ENTRY, "%s",
               *op_errstr);
    }

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
 * global (applies to all volumes) or normal (applies to only specified volume).
 *
 * Sixth field is <flags>.
 *
 * Seventh field is <op-version>.
 *
 * Eight field is description of option: If NULL, tried to fetch from
 * translator code's xlator_options table.
 *
 * Ninth field is validation function: If NULL, xlator's option specific
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
    {.key = "cluster.lookup-unhashed",
     .voltype = "cluster/distribute",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.lookup-optimize",
     .voltype = "cluster/distribute",
     .op_version = GD_OP_VERSION_3_7_2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.min-free-disk",
     .voltype = "cluster/distribute",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.min-free-inodes",
     .voltype = "cluster/distribute",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.rebalance-stats",
     .voltype = "cluster/distribute",
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.subvols-per-directory",
     .voltype = "cluster/distribute",
     .option = "directory-layout-spread",
     .op_version = 2,
     .validate_fn = validate_subvols_per_directory,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.readdir-optimize",
     .voltype = "cluster/distribute",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.rsync-hash-regex",
     .voltype = "cluster/distribute",
     .type = NO_DOC,
     .op_version = 3,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.extra-hash-regex",
     .voltype = "cluster/distribute",
     .type = NO_DOC,
     .op_version = 3,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.dht-xattr-name",
     .voltype = "cluster/distribute",
     .option = "xattr-name",
     .type = NO_DOC,
     .op_version = 3,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .key = "cluster.randomize-hash-range-by-gfid",
        .voltype = "cluster/distribute",
        .option = "randomize-hash-range-by-gfid",
        .type = NO_DOC,
        .op_version = GD_OP_VERSION_3_6_0,
        .flags = VOLOPT_FLAG_CLIENT_OPT,
    },
    {
        .key = "cluster.rebal-throttle",
        .voltype = "cluster/distribute",
        .option = "rebal-throttle",
        .op_version = GD_OP_VERSION_3_7_0,
        .validate_fn = validate_defrag_throttle_option,
        .flags = VOLOPT_FLAG_CLIENT_OPT,
    },

    {
        .key = "cluster.lock-migration",
        .voltype = "cluster/distribute",
        .option = "lock-migration",
        .value = "off",
        .op_version = GD_OP_VERSION_3_8_0,
        .validate_fn = validate_lock_migration_option,
        .flags = VOLOPT_FLAG_CLIENT_OPT,
    },

    {
        .key = "cluster.force-migration",
        .voltype = "cluster/distribute",
        .option = "force-migration",
        .value = "off",
        .op_version = GD_OP_VERSION_4_0_0,
        .flags = VOLOPT_FLAG_CLIENT_OPT,
    },

    /* NUFA xlator options (Distribute special case) */
    {.key = "cluster.nufa",
     .voltype = "cluster/distribute",
     .option = "!nufa",
     .type = NO_DOC,
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.local-volume-name",
     .voltype = "cluster/nufa",
     .option = "local-volume-name",
     .type = NO_DOC,
     .op_version = 3,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .key = "cluster.weighted-rebalance",
        .voltype = "cluster/distribute",
        .op_version = GD_OP_VERSION_3_6_0,
    },

    /* Switch xlator options (Distribute special case) */
    {.key = "cluster.switch",
     .voltype = "cluster/distribute",
     .option = "!switch",
     .type = NO_DOC,
     .op_version = 3,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.switch-pattern",
     .voltype = "cluster/switch",
     .option = "pattern.switch.case",
     .type = NO_DOC,
     .op_version = 3,
     .flags = VOLOPT_FLAG_CLIENT_OPT},

    /* AFR xlator options */
    {.key = "cluster.entry-change-log",
     .voltype = "cluster/replicate",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.read-subvolume",
     .voltype = "cluster/replicate",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.read-subvolume-index",
     .voltype = "cluster/replicate",
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.read-hash-mode",
     .voltype = "cluster/replicate",
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.background-self-heal-count",
     .voltype = "cluster/replicate",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.metadata-self-heal",
     .voltype = "cluster/replicate",
     .op_version = 1,
     .validate_fn = validate_replica,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.data-self-heal",
     .voltype = "cluster/replicate",
     .op_version = 1,
     .validate_fn = validate_replica,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.entry-self-heal",
     .voltype = "cluster/replicate",
     .op_version = 1,
     .validate_fn = validate_replica,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.self-heal-daemon",
     .voltype = "cluster/replicate",
     .option = "!self-heal-daemon",
     .op_version = 1,
     .validate_fn = validate_replica_heal_enable_disable},
    {.key = "cluster.heal-timeout",
     .voltype = "cluster/replicate",
     .option = "!heal-timeout",
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.strict-readdir",
     .voltype = "cluster/replicate",
     .type = NO_DOC,
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.self-heal-window-size",
     .voltype = "cluster/replicate",
     .option = "data-self-heal-window-size",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.data-change-log",
     .voltype = "cluster/replicate",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.metadata-change-log",
     .voltype = "cluster/replicate",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.data-self-heal-algorithm",
     .voltype = "cluster/replicate",
     .option = "data-self-heal-algorithm",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.eager-lock",
     .voltype = "cluster/replicate",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "disperse.eager-lock",
     .voltype = "cluster/disperse",
     .op_version = GD_OP_VERSION_3_7_10,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "disperse.other-eager-lock",
     .voltype = "cluster/disperse",
     .op_version = GD_OP_VERSION_3_13_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "disperse.eager-lock-timeout",
     .voltype = "cluster/disperse",
     .op_version = GD_OP_VERSION_4_0_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "disperse.other-eager-lock-timeout",
     .voltype = "cluster/disperse",
     .op_version = GD_OP_VERSION_4_0_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.quorum-type",
     .voltype = "cluster/replicate",
     .option = "quorum-type",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.quorum-count",
     .voltype = "cluster/replicate",
     .option = "quorum-count",
     .op_version = 1,
     .validate_fn = validate_quorum_count,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.choose-local",
     .voltype = "cluster/replicate",
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.self-heal-readdir-size",
     .voltype = "cluster/replicate",
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.post-op-delay-secs",
     .voltype = "cluster/replicate",
     .type = NO_DOC,
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.readdir-failover",
     .voltype = "cluster/replicate",
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.ensure-durability",
     .voltype = "cluster/replicate",
     .op_version = 3,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.consistent-metadata",
     .voltype = "cluster/replicate",
     .type = DOC,
     .op_version = GD_OP_VERSION_3_7_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.heal-wait-queue-length",
     .voltype = "cluster/replicate",
     .type = DOC,
     .op_version = GD_OP_VERSION_3_7_10,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.favorite-child-policy",
     .voltype = "cluster/replicate",
     .type = DOC,
     .op_version = GD_OP_VERSION_3_7_12,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.full-lock",
     .voltype = "cluster/replicate",
     .type = NO_DOC,
     .op_version = GD_OP_VERSION_3_13_2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.optimistic-change-log",
     .voltype = "cluster/replicate",
     .type = NO_DOC,
     .op_version = GD_OP_VERSION_7_2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},

    /* IO-stats xlator options */
    {.key = VKEY_DIAG_LAT_MEASUREMENT,
     .voltype = "debug/io-stats",
     .option = "latency-measurement",
     .value = "off",
     .op_version = 1},
    {.key = "diagnostics.dump-fd-stats",
     .voltype = "debug/io-stats",
     .op_version = 1},
    {.key = VKEY_DIAG_CNT_FOP_HITS,
     .voltype = "debug/io-stats",
     .option = "count-fop-hits",
     .value = "off",
     .type = NO_DOC,
     .op_version = 1},
    {.key = "diagnostics.brick-log-level",
     .voltype = "debug/io-stats",
     .value = "INFO",
     .option = "!brick-log-level",
     .op_version = 1},
    {.key = "diagnostics.client-log-level",
     .voltype = "debug/io-stats",
     .value = "INFO",
     .option = "!client-log-level",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "diagnostics.brick-sys-log-level",
     .voltype = "debug/io-stats",
     .option = "!sys-log-level",
     .op_version = 1},
    {.key = "diagnostics.client-sys-log-level",
     .voltype = "debug/io-stats",
     .option = "!sys-log-level",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .key = "diagnostics.brick-logger",
        .voltype = "debug/io-stats",
        .option = "!logger",
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {.key = "diagnostics.client-logger",
     .voltype = "debug/io-stats",
     .option = "!logger",
     .op_version = GD_OP_VERSION_3_6_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .key = "diagnostics.brick-log-format",
        .voltype = "debug/io-stats",
        .option = "!log-format",
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {.key = "diagnostics.client-log-format",
     .voltype = "debug/io-stats",
     .option = "!log-format",
     .op_version = GD_OP_VERSION_3_6_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .key = "diagnostics.brick-log-buf-size",
        .voltype = "debug/io-stats",
        .option = "!log-buf-size",
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {.key = "diagnostics.client-log-buf-size",
     .voltype = "debug/io-stats",
     .option = "!log-buf-size",
     .op_version = GD_OP_VERSION_3_6_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .key = "diagnostics.brick-log-flush-timeout",
        .voltype = "debug/io-stats",
        .option = "!log-flush-timeout",
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {.key = "diagnostics.client-log-flush-timeout",
     .voltype = "debug/io-stats",
     .option = "!log-flush-timeout",
     .op_version = GD_OP_VERSION_3_6_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "diagnostics.stats-dump-interval",
     .voltype = "debug/io-stats",
     .option = "ios-dump-interval",
     .op_version = 1},
    {.key = "diagnostics.fop-sample-interval",
     .voltype = "debug/io-stats",
     .option = "ios-sample-interval",
     .op_version = 1},
    {
        .key = "diagnostics.stats-dump-format",
        .voltype = "debug/io-stats",
        .option = "ios-dump-format",
        .op_version = GD_OP_VERSION_3_12_0,
    },
    {.key = "diagnostics.fop-sample-buf-size",
     .voltype = "debug/io-stats",
     .option = "ios-sample-buf-size",
     .op_version = 1},
    {.key = "diagnostics.stats-dnscache-ttl-sec",
     .voltype = "debug/io-stats",
     .option = "ios-dnscache-ttl-sec",
     .op_version = 1},

    /* IO-cache xlator options */
    {.key = "performance.cache-max-file-size",
     .voltype = "performance/io-cache",
     .option = "max-file-size",
     .op_version = 1,
     .validate_fn = validate_cache_max_min_size,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.cache-min-file-size",
     .voltype = "performance/io-cache",
     .option = "min-file-size",
     .op_version = 1,
     .validate_fn = validate_cache_max_min_size,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.cache-refresh-timeout",
     .voltype = "performance/io-cache",
     .option = "cache-timeout",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.cache-priority",
     .voltype = "performance/io-cache",
     .option = "priority",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.io-cache-size",
     .voltype = "performance/io-cache",
     .option = "cache-size",
     .op_version = GD_OP_VERSION_8_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .key = "performance.cache-size",
        .voltype = "performance/io-cache",
        .op_version = 1,
        .flags = VOLOPT_FLAG_CLIENT_OPT,
        .description = "Deprecated option. Use performance.io-cache-size "
                       "to adjust the cache size of the io-cache translator, "
                       "and use performance.quick-read-cache-size to adjust "
                       "the cache size of the quick-read translator.",
    },

    /* IO-threads xlator options */
    {.key = "performance.io-thread-count",
     .voltype = "performance/io-threads",
     .option = "thread-count",
     .op_version = 1},
    {.key = "performance.high-prio-threads",
     .voltype = "performance/io-threads",
     .op_version = 1},
    {.key = "performance.normal-prio-threads",
     .voltype = "performance/io-threads",
     .op_version = 1},
    {.key = "performance.low-prio-threads",
     .voltype = "performance/io-threads",
     .op_version = 1},
    {.key = "performance.least-prio-threads",
     .voltype = "performance/io-threads",
     .op_version = 1},
    {.key = "performance.enable-least-priority",
     .voltype = "performance/io-threads",
     .op_version = 1},
    {.key = "performance.iot-watchdog-secs",
     .voltype = "performance/io-threads",
     .option = "watchdog-secs",
     .op_version = GD_OP_VERSION_4_1_0},
    {.key = "performance.iot-cleanup-disconnected-reqs",
     .voltype = "performance/io-threads",
     .option = "cleanup-disconnected-reqs",
     .op_version = GD_OP_VERSION_4_1_0},
    {.key = "performance.iot-pass-through",
     .voltype = "performance/io-threads",
     .option = "pass-through",
     .op_version = GD_OP_VERSION_4_1_0},

    /* Other perf xlators' options */
    {.key = "performance.io-cache-pass-through",
     .voltype = "performance/io-cache",
     .option = "pass-through",
     .op_version = GD_OP_VERSION_4_1_0},
    {.key = "performance.quick-read-cache-size",
     .voltype = "performance/quick-read",
     .option = "cache-size",
     .op_version = GD_OP_VERSION_8_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.cache-size",
     .voltype = "performance/quick-read",
     .type = NO_DOC,
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.quick-read-cache-timeout",
     .voltype = "performance/quick-read",
     .option = "cache-timeout",
     .op_version = GD_OP_VERSION_8_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.qr-cache-timeout",
     .voltype = "performance/quick-read",
     .option = "cache-timeout",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT,
     .description =
         "Deprecated option. Use performance.quick-read-cache-timeout "
         "instead."},
    {.key = "performance.quick-read-cache-invalidation",
     .voltype = "performance/quick-read",
     .option = "quick-read-cache-invalidation",
     .op_version = GD_OP_VERSION_4_0_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.ctime-invalidation",
     .voltype = "performance/quick-read",
     .option = "ctime-invalidation",
     .op_version = GD_OP_VERSION_5_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.flush-behind",
     .voltype = "performance/write-behind",
     .option = "flush-behind",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.nfs.flush-behind",
     .voltype = "performance/write-behind",
     .option = "flush-behind",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.write-behind-window-size",
     .voltype = "performance/write-behind",
     .option = "cache-size",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .key = "performance.resync-failed-syncs-after-fsync",
        .voltype = "performance/write-behind",
        .option = "resync-failed-syncs-after-fsync",
        .op_version = GD_OP_VERSION_3_7_7,
        .flags = VOLOPT_FLAG_CLIENT_OPT,
        .description = "If sync of \"cached-writes issued before fsync\" "
                       "(to backend) fails, this option configures whether "
                       "to retry syncing them after fsync or forget them. "
                       "If set to on, cached-writes are retried "
                       "till a \"flush\" fop (or a successful sync) on sync "
                       "failures. "
                       "fsync itself is failed irrespective of the value of "
                       "this option. ",
    },
    {.key = "performance.nfs.write-behind-window-size",
     .voltype = "performance/write-behind",
     .option = "cache-size",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.strict-o-direct",
     .voltype = "performance/write-behind",
     .option = "strict-O_DIRECT",
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.nfs.strict-o-direct",
     .voltype = "performance/write-behind",
     .option = "strict-O_DIRECT",
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.strict-write-ordering",
     .voltype = "performance/write-behind",
     .option = "strict-write-ordering",
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.nfs.strict-write-ordering",
     .voltype = "performance/write-behind",
     .option = "strict-write-ordering",
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.write-behind-trickling-writes",
     .voltype = "performance/write-behind",
     .option = "trickling-writes",
     .op_version = GD_OP_VERSION_3_13_1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.aggregate-size",
     .voltype = "performance/write-behind",
     .option = "aggregate-size",
     .op_version = GD_OP_VERSION_4_1_0,
     .flags = OPT_FLAG_CLIENT_OPT},
    {.key = "performance.nfs.write-behind-trickling-writes",
     .voltype = "performance/write-behind",
     .option = "trickling-writes",
     .op_version = GD_OP_VERSION_3_13_1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.lazy-open",
     .voltype = "performance/open-behind",
     .option = "lazy-open",
     .op_version = 3,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.read-after-open",
     .voltype = "performance/open-behind",
     .option = "read-after-open",
     .op_version = 3,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .key = "performance.open-behind-pass-through",
        .voltype = "performance/open-behind",
        .option = "pass-through",
        .op_version = GD_OP_VERSION_4_1_0,
    },
    {.key = "performance.read-ahead-page-count",
     .voltype = "performance/read-ahead",
     .option = "page-count",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .key = "performance.read-ahead-pass-through",
        .voltype = "performance/read-ahead",
        .option = "pass-through",
        .op_version = GD_OP_VERSION_4_1_0,
    },
    {
        .key = "performance.readdir-ahead-pass-through",
        .voltype = "performance/readdir-ahead",
        .option = "pass-through",
        .op_version = GD_OP_VERSION_4_1_0,
    },
    {.key = "performance.md-cache-pass-through",
     .voltype = "performance/md-cache",
     .option = "pass-through",
     .op_version = GD_OP_VERSION_4_1_0},
    {
        .key = "performance.write-behind-pass-through",
        .voltype = "performance/write-behind",
        .option = "pass-through",
        .op_version = GD_OP_VERSION_9_0
    },
    {.key = "performance.md-cache-timeout",
     .voltype = "performance/md-cache",
     .option = "md-cache-timeout",
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.cache-swift-metadata",
     .voltype = "performance/md-cache",
     .option = "cache-swift-metadata",
     .op_version = GD_OP_VERSION_3_7_10,
     .description = "Cache swift metadata (user.swift.metadata xattr)",
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.cache-samba-metadata",
     .voltype = "performance/md-cache",
     .option = "cache-samba-metadata",
     .op_version = GD_OP_VERSION_3_9_0,
     .description = "Cache samba metadata (user.DOSATTRIB, security.NTACL"
                    " xattr)",
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.cache-capability-xattrs",
     .voltype = "performance/md-cache",
     .option = "cache-capability-xattrs",
     .op_version = GD_OP_VERSION_3_10_0,
     .description = "Cache xattrs required for capability based security",
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.cache-ima-xattrs",
     .voltype = "performance/md-cache",
     .option = "cache-ima-xattrs",
     .op_version = GD_OP_VERSION_3_10_0,
     .description = "Cache xattrs required for IMA "
                    "(Integrity Measurement Architecture)",
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.md-cache-statfs",
     .voltype = "performance/md-cache",
     .option = "md-cache-statfs",
     .op_version = GD_OP_VERSION_4_0_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.xattr-cache-list",
     .voltype = "performance/md-cache",
     .option = "xattr-cache-list",
     .op_version = GD_OP_VERSION_4_0_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT,
     .description = "A comma separated list of xattrs that shall be "
                    "cached by md-cache. The only wildcard allowed is '*'"},
    {.key = "performance.nl-cache-pass-through",
     .voltype = "performance/nl-cache",
     .option = "pass-through",
     .op_version = GD_OP_VERSION_4_1_0},

    /* Client xlator options */
    {.key = "network.frame-timeout",
     .voltype = "protocol/client",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "network.ping-timeout",
     .voltype = "protocol/client",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "network.tcp-window-size",
     .voltype = "protocol/client",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "client.ssl",
     .voltype = "protocol/client",
     .option = "transport.socket.ssl-enabled",
     .value = "off",
     .op_version = 2,
     .description = "enable/disable client.ssl flag in the "
                    "volume.",
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "network.remote-dio",
     .voltype = "protocol/client",
     .option = "filter-O_DIRECT",
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .key = "client.own-thread",
        .voltype = "protocol/client",
        .option = "transport.socket.own-thread",
        .type = NO_DOC,
        .op_version = GD_OP_VERSION_3_7_0,
    },
    {
        .key = "client.event-threads",
        .voltype = "protocol/client",
        .op_version = GD_OP_VERSION_3_7_0,
    },
    {.key = "client.tcp-user-timeout",
     .voltype = "protocol/client",
     .option = "transport.tcp-user-timeout",
     .op_version = GD_OP_VERSION_3_10_2,
     .value = "0", /* 0 - implies "use system default" */
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "client.keepalive-time",
     .voltype = "protocol/client",
     .option = "transport.socket.keepalive-time",
     .op_version = GD_OP_VERSION_3_10_2,
     .value = "20",
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "client.keepalive-interval",
     .voltype = "protocol/client",
     .option = "transport.socket.keepalive-interval",
     .op_version = GD_OP_VERSION_3_10_2,
     .value = "2",
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "client.keepalive-count",
     .voltype = "protocol/client",
     .option = "transport.socket.keepalive-count",
     .op_version = GD_OP_VERSION_3_10_2,
     .value = "9",
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "client.strict-locks",
     .voltype = "protocol/client",
     .option = "strict-locks",
     .value = "off",
     .op_version = GD_OP_VERSION_8_0,
     .validate_fn = validate_boolean,
     .type = GLOBAL_DOC,
     .description = "When set, doesn't reopen saved fds after reconnect "
                    "if POSIX locks are held on them. Hence subsequent "
                    "operations on these fds will fail. This is "
                    "necessary for stricter lock complaince as bricks "
                    "cleanup any granted locks when a client "
                    "disconnects."},

    /* Although the following option is named ta-remote-port but it will be
     * added as remote-port in client volfile for ta-bricks only.
     */
    {.key = "client.ta-brick-port",
     .voltype = "protocol/client",
     .option = "ta-remote-port",
     .op_version = GD_OP_VERSION_7_0},

    /* Server xlator options */
    {.key = "network.tcp-window-size",
     .voltype = "protocol/server",
     .type = NO_DOC,
     .op_version = 1},
    {.key = "network.inode-lru-limit",
     .voltype = "protocol/server",
     .op_version = 1},
    {.key = AUTH_ALLOW_MAP_KEY,
     .voltype = "protocol/server",
     .option = "!server-auth",
     .value = "*",
     .op_version = 1},
    {.key = AUTH_REJECT_MAP_KEY,
     .voltype = "protocol/server",
     .option = "!server-auth",
     .op_version = 1},
    {.key = "transport.keepalive",
     .voltype = "protocol/server",
     .option = "transport.socket.keepalive",
     .type = NO_DOC,
     .value = "1",
     .op_version = 1},
    {.key = "server.allow-insecure",
     .voltype = "protocol/server",
     .option = "rpc-auth-allow-insecure",
     .type = DOC,
     .op_version = 1},
    {.key = "server.root-squash",
     .voltype = "protocol/server",
     .option = "root-squash",
     .op_version = 2},
    {.key = "server.all-squash",
     .voltype = "protocol/server",
     .option = "all-squash",
     .op_version = GD_OP_VERSION_6_0},
    {.key = "server.anonuid",
     .voltype = "protocol/server",
     .option = "anonuid",
     .op_version = 3},
    {.key = "server.anongid",
     .voltype = "protocol/server",
     .option = "anongid",
     .op_version = 3},
    {.key = "server.statedump-path",
     .voltype = "protocol/server",
     .option = "statedump-path",
     .op_version = 1,
     .validate_fn = validate_statedump_path},
    {.key = "server.outstanding-rpc-limit",
     .voltype = "protocol/server",
     .option = "rpc.outstanding-rpc-limit",
     .type = GLOBAL_DOC,
     .op_version = 3},
    {.key = "server.ssl",
     .voltype = "protocol/server",
     .value = "off",
     .option = "transport.socket.ssl-enabled",
     .description = "enable/disable server.ssl flag in the "
                    "volume.",
     .op_version = 2},
    {
        .key = "auth.ssl-allow",
        .voltype = "protocol/server",
        .option = "!ssl-allow",
        .value = "*",
        .type = DOC,
        .description = "Allow a comma separated list of common names (CN) of "
                       "the clients that are allowed to access the server."
                       "By default, all TLS authenticated clients are "
                       "allowed to access the server.",
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {
        .key = "server.manage-gids",
        .voltype = "protocol/server",
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {
        .key = "server.dynamic-auth",
        .voltype = "protocol/server",
        .op_version = GD_OP_VERSION_3_7_5,
    },
    {
        .key = "client.send-gids",
        .voltype = "protocol/client",
        .type = NO_DOC,
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {
        .key = "server.gid-timeout",
        .voltype = "protocol/server",
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {
        .key = "server.own-thread",
        .voltype = "protocol/server",
        .option = "transport.socket.own-thread",
        .type = NO_DOC,
        .op_version = GD_OP_VERSION_3_7_0,
    },
    {
        .key = "server.event-threads",
        .voltype = "protocol/server",
        .op_version = GD_OP_VERSION_3_7_0,
    },
    {
        .key = "server.tcp-user-timeout",
        .voltype = "protocol/server",
        .option = "transport.tcp-user-timeout",
        .op_version = GD_OP_VERSION_3_10_2,
    },
    {
        .key = "server.keepalive-time",
        .voltype = "protocol/server",
        .option = "transport.socket.keepalive-time",
        .op_version = GD_OP_VERSION_3_10_2,
        .value = "20",
    },
    {
        .key = "server.keepalive-interval",
        .voltype = "protocol/server",
        .option = "transport.socket.keepalive-interval",
        .op_version = GD_OP_VERSION_3_10_2,
        .value = "2",
    },
    {
        .key = "server.keepalive-count",
        .voltype = "protocol/server",
        .option = "transport.socket.keepalive-count",
        .op_version = GD_OP_VERSION_3_10_2,
        .value = "9",
    },
    {
        .key = "transport.listen-backlog",
        .voltype = "protocol/server",
        .option = "transport.listen-backlog",
        .op_version = GD_OP_VERSION_3_11_1,
        .validate_fn = validate_server_options,
        .description = "This option uses the value of backlog argument that "
                       "defines the maximum length to which the queue of "
                       "pending connections for socket fd may grow.",
        .value = "1024",
    },

    /* Generic transport options */
    {
        .key = SSL_OWN_CERT_OPT,
        .voltype = "rpc-transport/socket",
        .option = "!ssl-own-cert",
        .op_version = GD_OP_VERSION_3_7_4,
    },
    {
        .key = SSL_PRIVATE_KEY_OPT,
        .voltype = "rpc-transport/socket",
        .option = "!ssl-private-key",
        .op_version = GD_OP_VERSION_3_7_4,
    },
    {
        .key = SSL_CA_LIST_OPT,
        .voltype = "rpc-transport/socket",
        .option = "!ssl-ca-list",
        .op_version = GD_OP_VERSION_3_7_4,
    },
    {
        .key = SSL_CRL_PATH_OPT,
        .voltype = "rpc-transport/socket",
        .option = "!ssl-crl-path",
        .op_version = GD_OP_VERSION_3_7_4,
    },
    {
        .key = SSL_CERT_DEPTH_OPT,
        .voltype = "rpc-transport/socket",
        .option = "!ssl-cert-depth",
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {
        .key = SSL_CIPHER_LIST_OPT,
        .voltype = "rpc-transport/socket",
        .option = "!ssl-cipher-list",
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {
        .key = SSL_DH_PARAM_OPT,
        .voltype = "rpc-transport/socket",
        .option = "!ssl-dh-param",
        .op_version = GD_OP_VERSION_3_7_4,
    },
    {
        .key = SSL_EC_CURVE_OPT,
        .voltype = "rpc-transport/socket",
        .option = "!ssl-ec-curve",
        .op_version = GD_OP_VERSION_3_7_4,
    },
    {
        .key = "transport.address-family",
        .voltype = "protocol/server",
        .option = "!address-family",
        .op_version = GD_OP_VERSION_3_7_4,
        .type = NO_DOC,
    },

    /* Performance xlators enable/disbable options */
    {.key = "performance.write-behind",
     .voltype = "performance/write-behind",
     .option = "!perf",
     .value = "on",
     .op_version = 1,
     .description = "enable/disable write-behind translator in the "
                    "volume.",
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT},
    {.key = "performance.read-ahead",
     .voltype = "performance/read-ahead",
     .option = "!perf",
     .value = "off",
     .op_version = 1,
     .description = "enable/disable read-ahead translator in the volume.",
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT},
    {.key = "performance.readdir-ahead",
     .voltype = "performance/readdir-ahead",
     .option = "!perf",
     .value = "off",
     .op_version = 3,
     .description = "enable/disable readdir-ahead translator in the volume.",
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT},
    {.key = "performance.io-cache",
     .voltype = "performance/io-cache",
     .option = "!perf",
     .value = "off",
     .op_version = 1,
     .description = "enable/disable io-cache translator in the volume.",
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.open-behind",
     .voltype = "performance/open-behind",
     .option = "!perf",
     .value = "on",
     .op_version = 2,
     .description = "enable/disable open-behind translator in the volume.",
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT

    },
    {.key = "performance.quick-read",
     .voltype = "performance/quick-read",
     .option = "!perf",
     .value = "on",
     .op_version = 1,
     .description = "enable/disable quick-read translator in the volume.",
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT},
    {.key = "performance.nl-cache",
     .voltype = "performance/nl-cache",
     .option = "!perf",
     .value = "off",
     .op_version = GD_OP_VERSION_3_11_0,
     .description = "enable/disable negative entry caching translator in "
                    "the volume. Enabling this option improves performance"
                    " of 'create file/directory' workload",
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT},
    {.key = "performance.stat-prefetch",
     .voltype = "performance/md-cache",
     .option = "!perf",
     .value = "on",
     .op_version = 1,
     .description = "enable/disable meta-data caching translator in the "
                    "volume.",
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT},
    {.key = "performance.client-io-threads",
     .voltype = "performance/io-threads",
     .option = "!perf",
     .value = "on",
     .op_version = 1,
     .description = "enable/disable io-threads translator in the client "
                    "graph of volume.",
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT},
    {.key = "performance.nfs.write-behind",
     .voltype = "performance/write-behind",
     .option = "!nfsperf",
     .value = "on",
     .op_version = 1,
     .description = "enable/disable write-behind translator in the volume",
     .flags = VOLOPT_FLAG_XLATOR_OPT},
    {.key = "performance.nfs.read-ahead",
     .voltype = "performance/read-ahead",
     .option = "!nfsperf",
     .value = "off",
     .type = NO_DOC,
     .op_version = 1,
     .flags = VOLOPT_FLAG_XLATOR_OPT},
    {.key = "performance.nfs.io-cache",
     .voltype = "performance/io-cache",
     .option = "!nfsperf",
     .value = "off",
     .type = NO_DOC,
     .op_version = 1,
     .flags = VOLOPT_FLAG_XLATOR_OPT},
    {.key = "performance.nfs.quick-read",
     .voltype = "performance/quick-read",
     .option = "!nfsperf",
     .value = "off",
     .type = NO_DOC,
     .op_version = 1,
     .flags = VOLOPT_FLAG_XLATOR_OPT},
    {.key = "performance.nfs.stat-prefetch",
     .voltype = "performance/md-cache",
     .option = "!nfsperf",
     .value = "off",
     .type = NO_DOC,
     .op_version = 1,
     .flags = VOLOPT_FLAG_XLATOR_OPT},
    {.key = "performance.nfs.io-threads",
     .voltype = "performance/io-threads",
     .option = "!nfsperf",
     .value = "off",
     .type = NO_DOC,
     .op_version = 1,
     .flags = VOLOPT_FLAG_XLATOR_OPT},
    {.key = "performance.force-readdirp",
     .voltype = "performance/md-cache",
     .option = "force-readdirp",
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.cache-invalidation",
     .voltype = "performance/md-cache",
     .option = "cache-invalidation",
     .op_version = GD_OP_VERSION_3_9_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},

    {.key = "performance.global-cache-invalidation",
     .voltype = "performance/md-cache",
     .option = "global-cache-invalidation",
     .op_version = GD_OP_VERSION_6_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},

    /* Feature translators */
    {.key = "features.uss",
     .voltype = "features/snapview-server",
     .op_version = GD_OP_VERSION_3_6_0,
     .value = "off",
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT,
     .validate_fn = validate_uss,
     .description = "enable/disable User Serviceable Snapshots on the "
                    "volume."},

    {.key = "features.snapshot-directory",
     .voltype = "features/snapview-client",
     .op_version = GD_OP_VERSION_3_6_0,
     .value = ".snaps",
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT,
     .validate_fn = validate_uss_dir,
     .description = "Entry point directory for entering snapshot world. "
                    "Value can have only [0-9a-z-_] and starts with "
                    "dot (.) and cannot exceed 255 character"},

    {.key = "features.show-snapshot-directory",
     .voltype = "features/snapview-client",
     .op_version = GD_OP_VERSION_3_6_0,
     .value = "off",
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT,
     .description = "show entry point in readdir output of "
                    "snapdir-entry-path which is set by samba"},

    {.key = "features.tag-namespaces",
     .voltype = "features/namespace",
     .op_version = GD_OP_VERSION_4_1_0,
     .option = "tag-namespaces",
     .value = "off",
     .flags = OPT_FLAG_CLIENT_OPT,
     .description = "This option enables this translator's functionality "
                    "that tags every fop with a namespace hash for later "
                    "throttling, stats collection, logging, etc."},

#ifdef HAVE_LIB_Z
    /* Compressor-decompressor xlator options
     * defaults used from xlator/features/compress/src/cdc.h
     */
    {.key = "network.compression",
     .voltype = "features/cdc",
     .option = "!feat",
     .value = "off",
     .op_version = 3,
     .description = "enable/disable network compression translator",
     .flags = VOLOPT_FLAG_XLATOR_OPT},
    {.key = "network.compression.window-size",
     .voltype = "features/cdc",
     .option = "window-size",
     .op_version = 3},
    {.key = "network.compression.mem-level",
     .voltype = "features/cdc",
     .option = "mem-level",
     .op_version = 3},
    {.key = "network.compression.min-size",
     .voltype = "features/cdc",
     .option = "min-size",
     .op_version = 3},
    {.key = "network.compression.compression-level",
     .voltype = "features/cdc",
     .option = "compression-level",
     .op_version = 3},
    {.key = "network.compression.debug",
     .voltype = "features/cdc",
     .option = "debug",
     .type = NO_DOC,
     .op_version = 3},
#endif

    /* Quota xlator options */
    {
        .key = VKEY_FEATURES_LIMIT_USAGE,
        .voltype = "features/quota",
        .option = "limit-set",
        .type = NO_DOC,
        .op_version = 1,
    },
    {
        .key = "features.default-soft-limit",
        .voltype = "features/quota",
        .option = "default-soft-limit",
        .type = NO_DOC,
        .op_version = 3,
    },
    {
        .key = "features.soft-timeout",
        .voltype = "features/quota",
        .option = "soft-timeout",
        .type = NO_DOC,
        .op_version = 3,
    },
    {
        .key = "features.hard-timeout",
        .voltype = "features/quota",
        .option = "hard-timeout",
        .type = NO_DOC,
        .op_version = 3,
    },
    {
        .key = "features.alert-time",
        .voltype = "features/quota",
        .option = "alert-time",
        .type = NO_DOC,
        .op_version = 3,
    },
    {
        .key = "features.quota-deem-statfs",
        .voltype = "features/quota",
        .option = "deem-statfs",
        .value = "off",
        .type = DOC,
        .op_version = 2,
        .validate_fn = validate_quota,
    },

    /* Marker xlator options */
    {.key = VKEY_MARKER_XTIME,
     .voltype = "features/marker",
     .option = "xtime",
     .value = "off",
     .type = NO_DOC,
     .flags = VOLOPT_FLAG_FORCE,
     .op_version = 1},
    {.key = VKEY_MARKER_XTIME,
     .voltype = "features/marker",
     .option = "!xtime",
     .value = "off",
     .type = NO_DOC,
     .flags = VOLOPT_FLAG_FORCE,
     .op_version = 1},
    {.key = VKEY_MARKER_XTIME_FORCE,
     .voltype = "features/marker",
     .option = "gsync-force-xtime",
     .value = "off",
     .type = NO_DOC,
     .flags = VOLOPT_FLAG_FORCE,
     .op_version = 2},
    {.key = VKEY_MARKER_XTIME_FORCE,
     .voltype = "features/marker",
     .option = "!gsync-force-xtime",
     .value = "off",
     .type = NO_DOC,
     .flags = VOLOPT_FLAG_FORCE,
     .op_version = 2},
    {.key = VKEY_FEATURES_QUOTA,
     .voltype = "features/marker",
     .option = "quota",
     .value = "off",
     .type = NO_DOC,
     .flags = VOLOPT_FLAG_NEVER_RESET,
     .op_version = 1},
    {.key = VKEY_FEATURES_INODE_QUOTA,
     .voltype = "features/marker",
     .option = "inode-quota",
     .value = "off",
     .type = NO_DOC,
     .flags = VOLOPT_FLAG_NEVER_RESET,
     .op_version = 1},
    {.key = VKEY_FEATURES_BITROT,
     .voltype = "features/bit-rot",
     .option = "bitrot",
     .value = "disable",
     .type = NO_DOC,
     .flags = VOLOPT_FLAG_FORCE,
     .op_version = GD_OP_VERSION_3_7_0},

    /* Debug xlators options */
    {.key = "debug.trace",
     .voltype = "debug/trace",
     .option = "!debug",
     .value = "off",
     .type = NO_DOC,
     .op_version = 1,
     .flags = VOLOPT_FLAG_XLATOR_OPT},
    {.key = "debug.log-history",
     .voltype = "debug/trace",
     .option = "log-history",
     .type = NO_DOC,
     .op_version = 2},
    {.key = "debug.log-file",
     .voltype = "debug/trace",
     .option = "log-file",
     .type = NO_DOC,
     .op_version = 2},
    {.key = "debug.exclude-ops",
     .voltype = "debug/trace",
     .option = "exclude-ops",
     .type = NO_DOC,
     .op_version = 2},
    {.key = "debug.include-ops",
     .voltype = "debug/trace",
     .option = "include-ops",
     .type = NO_DOC,
     .op_version = 2},
    {.key = "debug.error-gen",
     .voltype = "debug/error-gen",
     .option = "!debug",
     .value = "off",
     .type = NO_DOC,
     .op_version = 1,
     .flags = VOLOPT_FLAG_XLATOR_OPT},
    {.key = "debug.error-failure",
     .voltype = "debug/error-gen",
     .option = "failure",
     .type = NO_DOC,
     .op_version = 3},
    {.key = "debug.error-number",
     .voltype = "debug/error-gen",
     .option = "error-no",
     .type = NO_DOC,
     .op_version = 3},
    {.key = "debug.random-failure",
     .voltype = "debug/error-gen",
     .option = "random-failure",
     .type = NO_DOC,
     .op_version = 3},
    {.key = "debug.error-fops",
     .voltype = "debug/error-gen",
     .option = "enable",
     .type = NO_DOC,
     .op_version = 3},

    /* NFS xlator options */
    {.key = "nfs.enable-ino32",
     .voltype = "nfs/server",
     .option = "nfs.enable-ino32",
     .type = GLOBAL_DOC,
     .op_version = 1},
    {.key = "nfs.mem-factor",
     .voltype = "nfs/server",
     .option = "nfs.mem-factor",
     .type = GLOBAL_DOC,
     .op_version = 1},
    {.key = "nfs.export-dirs",
     .voltype = "nfs/server",
     .option = "nfs3.export-dirs",
     .type = GLOBAL_DOC,
     .op_version = 1},
    {.key = "nfs.export-volumes",
     .voltype = "nfs/server",
     .option = "nfs3.export-volumes",
     .type = GLOBAL_DOC,
     .op_version = 1},
    {.key = "nfs.addr-namelookup",
     .voltype = "nfs/server",
     .option = "rpc-auth.addr.namelookup",
     .type = GLOBAL_DOC,
     .op_version = 1},
    {.key = "nfs.dynamic-volumes",
     .voltype = "nfs/server",
     .option = "nfs.dynamic-volumes",
     .type = GLOBAL_DOC,
     .op_version = 1},
    {.key = "nfs.register-with-portmap",
     .voltype = "nfs/server",
     .option = "rpc.register-with-portmap",
     .type = GLOBAL_DOC,
     .op_version = 1},
    {.key = "nfs.outstanding-rpc-limit",
     .voltype = "nfs/server",
     .option = "rpc.outstanding-rpc-limit",
     .type = GLOBAL_DOC,
     .op_version = 3},
    {.key = "nfs.port",
     .voltype = "nfs/server",
     .option = "nfs.port",
     .type = GLOBAL_DOC,
     .op_version = 1},
    {.key = "nfs.rpc-auth-unix",
     .voltype = "nfs/server",
     .option = "!rpc-auth.auth-unix.*",
     .op_version = 1},
    {.key = "nfs.rpc-auth-null",
     .voltype = "nfs/server",
     .option = "!rpc-auth.auth-null.*",
     .op_version = 1},
    {.key = "nfs.rpc-auth-allow",
     .voltype = "nfs/server",
     .option = "!rpc-auth.addr.*.allow",
     .op_version = 1},
    {.key = "nfs.rpc-auth-reject",
     .voltype = "nfs/server",
     .option = "!rpc-auth.addr.*.reject",
     .op_version = 1},
    {.key = "nfs.ports-insecure",
     .voltype = "nfs/server",
     .option = "!rpc-auth.ports.*.insecure",
     .op_version = 1},
    {.key = "nfs.transport-type",
     .voltype = "nfs/server",
     .option = "!nfs.transport-type",
     .op_version = 1,
     .description = "Specifies the nfs transport type. Valid "
                    "transport types are 'tcp' and 'rdma'."},
    {.key = "nfs.trusted-sync",
     .voltype = "nfs/server",
     .option = "!nfs3.*.trusted-sync",
     .op_version = 1},
    {.key = "nfs.trusted-write",
     .voltype = "nfs/server",
     .option = "!nfs3.*.trusted-write",
     .op_version = 1},
    {.key = "nfs.volume-access",
     .voltype = "nfs/server",
     .option = "!nfs3.*.volume-access",
     .op_version = 1},
    {.key = "nfs.export-dir",
     .voltype = "nfs/server",
     .option = "!nfs3.*.export-dir",
     .op_version = 1},
    {.key = NFS_DISABLE_MAP_KEY,
     .voltype = "nfs/server",
     .option = "!nfs-disable",
     .value = SITE_H_NFS_DISABLE,
     .op_version = 1},
    {.key = "nfs.nlm",
     .voltype = "nfs/server",
     .option = "nfs.nlm",
     .type = GLOBAL_DOC,
     .op_version = 1},
    {.key = "nfs.acl",
     .voltype = "nfs/server",
     .option = "nfs.acl",
     .type = GLOBAL_DOC,
     .op_version = 3},
    {.key = "nfs.mount-udp",
     .voltype = "nfs/server",
     .option = "nfs.mount-udp",
     .type = GLOBAL_DOC,
     .op_version = 1},
    {.key = "nfs.mount-rmtab",
     .voltype = "nfs/server",
     .option = "nfs.mount-rmtab",
     .type = GLOBAL_DOC,
     .op_version = 1},
    {
        .key = "nfs.rpc-statd",
        .voltype = "nfs/server",
        .option = "nfs.rpc-statd",
        .type = NO_DOC,
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {
        .key = "nfs.log-level",
        .voltype = "nfs/server",
        .option = "nfs.log-level",
        .type = NO_DOC,
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {.key = "nfs.server-aux-gids",
     .voltype = "nfs/server",
     .option = "nfs.server-aux-gids",
     .type = NO_DOC,
     .op_version = 2},
    {.key = "nfs.drc",
     .voltype = "nfs/server",
     .option = "nfs.drc",
     .type = GLOBAL_DOC,
     .op_version = 3},
    {.key = "nfs.drc-size",
     .voltype = "nfs/server",
     .option = "nfs.drc-size",
     .type = GLOBAL_DOC,
     .op_version = 3},
    {.key = "nfs.read-size",
     .voltype = "nfs/server",
     .option = "nfs3.read-size",
     .type = GLOBAL_DOC,
     .op_version = 3},
    {.key = "nfs.write-size",
     .voltype = "nfs/server",
     .option = "nfs3.write-size",
     .type = GLOBAL_DOC,
     .op_version = 3},
    {.key = "nfs.readdir-size",
     .voltype = "nfs/server",
     .option = "nfs3.readdir-size",
     .type = GLOBAL_DOC,
     .op_version = 3},
    {.key = "nfs.rdirplus",
     .voltype = "nfs/server",
     .option = "nfs.rdirplus",
     .type = GLOBAL_DOC,
     .op_version = GD_OP_VERSION_3_7_12,
     .description = "When this option is set to off NFS falls back to "
                    "standard readdir instead of readdirp"},
    {
        .key = "nfs.event-threads",
        .voltype = "nfs/server",
        .option = "nfs.event-threads",
        .type = NO_DOC,
        .op_version = GD_OP_VERSION_4_0_0,
    },

    /* Cli options for Export authentication on nfs mount */
    {.key = "nfs.exports-auth-enable",
     .voltype = "nfs/server",
     .option = "nfs.exports-auth-enable",
     .type = GLOBAL_DOC,
     .op_version = GD_OP_VERSION_3_7_0},
    {.key = "nfs.auth-refresh-interval-sec",
     .voltype = "nfs/server",
     .option = "nfs.auth-refresh-interval-sec",
     .type = GLOBAL_DOC,
     .op_version = GD_OP_VERSION_3_7_0},
    {.key = "nfs.auth-cache-ttl-sec",
     .voltype = "nfs/server",
     .option = "nfs.auth-cache-ttl-sec",
     .type = GLOBAL_DOC,
     .op_version = GD_OP_VERSION_3_7_0},

    /* Other options which don't fit any place above */
    {.key = "features.read-only",
     .voltype = "features/read-only",
     .option = "read-only",
     .op_version = 1,
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT},
    {.key = "features.worm",
     .voltype = "features/worm",
     .option = "worm",
     .value = "off",
     .validate_fn = validate_boolean,
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT},
    {.key = "features.worm-file-level",
     .voltype = "features/worm",
     .option = "worm-file-level",
     .value = "off",
     .validate_fn = validate_boolean,
     .op_version = GD_OP_VERSION_3_8_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT},
    {.key = "features.worm-files-deletable",
     .voltype = "features/worm",
     .option = "worm-files-deletable",
     .value = "on",
     .validate_fn = validate_boolean,
     .op_version = GD_OP_VERSION_3_13_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT},
    {
        .key = "features.default-retention-period",
        .voltype = "features/worm",
        .option = "default-retention-period",
        .validate_fn = validate_worm_period,
        .op_version = GD_OP_VERSION_3_8_0,
    },
    {
        .key = "features.retention-mode",
        .voltype = "features/worm",
        .option = "retention-mode",
        .validate_fn = validate_reten_mode,
        .op_version = GD_OP_VERSION_3_8_0,
    },
    {
        .key = "features.auto-commit-period",
        .voltype = "features/worm",
        .option = "auto-commit-period",
        .validate_fn = validate_worm_period,
        .op_version = GD_OP_VERSION_3_8_0,
    },
    {.key = "storage.linux-aio", .voltype = "storage/posix", .op_version = 1},
    {.key = "storage.linux-io_uring",
     .voltype = "storage/posix",
     .op_version = GD_OP_VERSION_9_0},
    {.key = "storage.batch-fsync-mode",
     .voltype = "storage/posix",
     .op_version = 3},
    {.key = "storage.batch-fsync-delay-usec",
     .voltype = "storage/posix",
     .op_version = 3},
    {
        .key = "storage.xattr-user-namespace-mode",
        .voltype = "storage/posix",
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {.key = "storage.owner-uid",
     .voltype = "storage/posix",
     .option = "brick-uid",
     .op_version = 1},
    {.key = "storage.owner-gid",
     .voltype = "storage/posix",
     .option = "brick-gid",
     .op_version = 1},
    {.key = "storage.node-uuid-pathinfo",
     .voltype = "storage/posix",
     .op_version = 3},
    {.key = "storage.health-check-interval",
     .voltype = "storage/posix",
     .op_version = 3},
    {
        .option = "update-link-count-parent",
        .key = "storage.build-pgfid",
        .voltype = "storage/posix",
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {
        .option = "gfid2path",
        .key = "storage.gfid2path",
        .type = NO_DOC,
        .voltype = "storage/posix",
        .op_version = GD_OP_VERSION_3_12_0,
    },
    {
        .option = "gfid2path-separator",
        .key = "storage.gfid2path-separator",
        .voltype = "storage/posix",
        .op_version = GD_OP_VERSION_3_12_0,
    },
    {
        .key = "storage.reserve",
        .voltype = "storage/posix",
        .op_version = GD_OP_VERSION_3_13_0,
    },
    {
        .option = "health-check-timeout",
        .key = "storage.health-check-timeout",
        .type = NO_DOC,
        .voltype = "storage/posix",
        .op_version = GD_OP_VERSION_4_0_0,
    },
    {
        .option = "fips-mode-rchecksum",
        .key = "storage.fips-mode-rchecksum",
        .type = NO_DOC,
        .voltype = "storage/posix",
        .op_version = GD_OP_VERSION_4_0_0,
    },
    {
        .option = "force-create-mode",
        .key = "storage.force-create-mode",
        .voltype = "storage/posix",
        .op_version = GD_OP_VERSION_4_0_0,
    },
    {
        .option = "force-directory-mode",
        .key = "storage.force-directory-mode",
        .voltype = "storage/posix",
        .op_version = GD_OP_VERSION_4_0_0,
    },
    {
        .option = "create-mask",
        .key = "storage.create-mask",
        .voltype = "storage/posix",
        .op_version = GD_OP_VERSION_4_0_0,
    },
    {
        .option = "create-directory-mask",
        .key = "storage.create-directory-mask",
        .voltype = "storage/posix",
        .op_version = GD_OP_VERSION_4_0_0,
    },
    {
        .option = "max-hardlinks",
        .key = "storage.max-hardlinks",
        .voltype = "storage/posix",
        .op_version = GD_OP_VERSION_4_0_0,
    },
    {
        .option = "ctime",
        .key = "features.ctime",
        .voltype = "storage/posix",
        .op_version = GD_OP_VERSION_4_1_0,
    },
    {.key = "config.memory-accounting",
     .voltype = "mgmt/glusterd",
     .option = "!config",
     .op_version = 2,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "config.transport",
     .voltype = "mgmt/glusterd",
     .option = "!config",
     .op_version = 2},
    {.key = VKEY_CONFIG_GFPROXY,
     .voltype = "configuration",
     .option = "gfproxyd",
     .value = "off",
     .type = DOC,
     .op_version = GD_OP_VERSION_3_13_0,
     .description = "If this option is enabled, the proxy client daemon "
                    "called gfproxyd will be started on all the trusted "
                    "storage pool nodes"},
    {.key = GLUSTERD_QUORUM_TYPE_KEY,
     .voltype = "mgmt/glusterd",
     .value = "off",
     .op_version = 2},
    {.key = GLUSTERD_QUORUM_RATIO_KEY,
     .voltype = "mgmt/glusterd",
     .value = "51",
     .op_version = 2},
    /* changelog translator - global tunables */
    {.key = "changelog.changelog",
     .voltype = "features/changelog",
     .type = NO_DOC,
     .op_version = 3},
    {.key = "changelog.changelog-dir",
     .voltype = "features/changelog",
     .type = NO_DOC,
     .op_version = 3},
    {.key = "changelog.encoding",
     .voltype = "features/changelog",
     .type = NO_DOC,
     .op_version = 3},
    {.key = "changelog.rollover-time",
     .voltype = "features/changelog",
     .type = NO_DOC,
     .op_version = 3},
    {.key = "changelog.fsync-interval",
     .voltype = "features/changelog",
     .type = NO_DOC,
     .op_version = 3},
    {
        .key = "changelog.changelog-barrier-timeout",
        .voltype = "features/changelog",
        .value = BARRIER_TIMEOUT,
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {.key = "changelog.capture-del-path",
     .voltype = "features/changelog",
     .type = NO_DOC,
     .op_version = 3},
    {
        .key = "features.barrier",
        .voltype = "features/barrier",
        .value = "disable",
        .type = NO_DOC,
        .op_version = GD_OP_VERSION_3_7_0,
    },
    {
        .key = "features.barrier-timeout",
        .voltype = "features/barrier",
        .value = BARRIER_TIMEOUT,
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {
        .key = GLUSTERD_GLOBAL_OP_VERSION_KEY,
        .voltype = "mgmt/glusterd",
        .op_version = GD_OP_VERSION_3_6_0,
    },
    {
        .key = GLUSTERD_MAX_OP_VERSION_KEY,
        .voltype = "mgmt/glusterd",
        .op_version = GD_OP_VERSION_3_10_0,
    },
    /*Trash translator options */
    {
        .key = "features.trash",
        .voltype = "features/trash",
        .op_version = GD_OP_VERSION_3_7_0,
    },
    {
        .key = "features.trash-dir",
        .voltype = "features/trash",
        .op_version = GD_OP_VERSION_3_7_0,
    },
    {
        .key = "features.trash-eliminate-path",
        .voltype = "features/trash",
        .op_version = GD_OP_VERSION_3_7_0,
    },
    {
        .key = "features.trash-max-filesize",
        .voltype = "features/trash",
        .op_version = GD_OP_VERSION_3_7_0,
    },
    {
        .key = "features.trash-internal-op",
        .voltype = "features/trash",
        .op_version = GD_OP_VERSION_3_7_0,
    },
    {.key = GLUSTERD_SHARED_STORAGE_KEY,
     .voltype = "mgmt/glusterd",
     .value = "disable",
     .type = GLOBAL_DOC,
     .op_version = GD_OP_VERSION_3_7_1,
     .description = "Create and mount the shared storage volume"
                    "(gluster_shared_storage) at "
                    "/var/run/gluster/shared_storage on enabling this "
                    "option. Unmount and delete the shared storage volume "
                    " on disabling this option."},
    {
        .key = "locks.trace",
        .voltype = "features/locks",
        .op_version = GD_OP_VERSION_3_7_0,
    },
    {
        .key = "locks.mandatory-locking",
        .voltype = "features/locks",
        .op_version = GD_OP_VERSION_3_8_0,
        .validate_fn = validate_mandatory_locking,
    },
    {.key = "cluster.disperse-self-heal-daemon",
     .voltype = "cluster/disperse",
     .type = NO_DOC,
     .option = "self-heal-daemon",
     .op_version = GD_OP_VERSION_3_7_0,
     .validate_fn = validate_disperse_heal_enable_disable},
    {.key = "cluster.quorum-reads",
     .voltype = "cluster/replicate",
     .op_version = GD_OP_VERSION_3_7_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "client.bind-insecure",
     .voltype = "protocol/client",
     .option = "client-bind-insecure",
     .type = NO_DOC,
     .op_version = GD_OP_VERSION_3_7_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "features.timeout",
     .voltype = "features/quiesce",
     .option = "timeout",
     .op_version = GD_OP_VERSION_4_0_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT,
     .description = "Specifies the number of seconds the "
                    "quiesce translator will wait "
                    "for a CHILD_UP event before "
                    "force-unwinding the frames it has "
                    "currently stored for retry."},
    {.key = "features.failover-hosts",
     .voltype = "features/quiesce",
     .option = "failover-hosts",
     .op_version = GD_OP_VERSION_4_0_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT,
     .description = "It is a comma separated list of hostname/IP "
                    "addresses. It Specifies the list of hosts where "
                    "the gfproxy daemons are running, to which the "
                    "the thin clients can failover to."},
    {.key = "features.shard",
     .voltype = "features/shard",
     .value = "off",
     .option = "!shard",
     .op_version = GD_OP_VERSION_3_7_0,
     .description = "enable/disable sharding translator on the volume.",
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT},
    {.key = "features.shard-block-size",
     .voltype = "features/shard",
     .op_version = GD_OP_VERSION_3_7_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .key = "features.shard-lru-limit",
        .voltype = "features/shard",
        .op_version = GD_OP_VERSION_5_0,
        .flags = VOLOPT_FLAG_CLIENT_OPT,
        .type = NO_DOC,
    },
    {.key = "features.shard-deletion-rate",
     .voltype = "features/shard",
     .op_version = GD_OP_VERSION_5_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .key = "features.scrub-throttle",
        .voltype = "features/bit-rot",
        .value = "lazy",
        .option = "scrub-throttle",
        .op_version = GD_OP_VERSION_3_7_0,
        .type = NO_DOC,
    },
    {
        .key = "features.scrub-freq",
        .voltype = "features/bit-rot",
        .value = "biweekly",
        .option = "scrub-frequency",
        .op_version = GD_OP_VERSION_3_7_0,
        .type = NO_DOC,
    },
    {
        .key = "features.scrub",
        .voltype = "features/bit-rot",
        .option = "scrubber",
        .op_version = GD_OP_VERSION_3_7_0,
        .flags = VOLOPT_FLAG_FORCE,
        .type = NO_DOC,
    },
    {
        .key = "features.expiry-time",
        .voltype = "features/bit-rot",
        .value = SIGNING_TIMEOUT,
        .option = "expiry-time",
        .op_version = GD_OP_VERSION_3_7_0,
        .type = NO_DOC,
    },
    {
        .key = "features.signer-threads",
        .voltype = "features/bit-rot",
        .value = BR_WORKERS,
        .option = "signer-threads",
        .op_version = GD_OP_VERSION_8_0,
        .type = NO_DOC,
    },
    /* Upcall translator options */
    /* Upcall translator options */
    {
        .key = "features.cache-invalidation",
        .voltype = "features/upcall",
        .value = "off",
        .op_version = GD_OP_VERSION_3_7_0,
    },
    {
        .key = "features.cache-invalidation-timeout",
        .voltype = "features/upcall",
        .op_version = GD_OP_VERSION_3_7_0,
    },
    {
        .key = "ganesha.enable",
        .voltype = "mgmt/ganesha",
        .value = "off",
        .option = "ganesha.enable",
        .op_version = GD_OP_VERSION_7_0,
    },
    /* Lease translator options */
    {
        .key = "features.leases",
        .voltype = "features/leases",
        .value = "off",
        .op_version = GD_OP_VERSION_3_8_0,
    },
    {
        .key = "features.lease-lock-recall-timeout",
        .voltype = "features/leases",
        .op_version = GD_OP_VERSION_3_8_0,
    },
    {.key = "disperse.background-heals",
     .voltype = "cluster/disperse",
     .op_version = GD_OP_VERSION_3_7_3,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "disperse.heal-wait-qlength",
     .voltype = "cluster/disperse",
     .op_version = GD_OP_VERSION_3_7_3,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .key = "cluster.heal-timeout",
        .voltype = "cluster/disperse",
        .option = "!heal-timeout",
        .op_version = GD_OP_VERSION_3_7_3,
        .type = NO_DOC,
    },
    {.key = "dht.force-readdirp",
     .voltype = "cluster/distribute",
     .option = "use-readdirp",
     .op_version = GD_OP_VERSION_3_7_5,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "disperse.read-policy",
     .voltype = "cluster/disperse",
     .op_version = GD_OP_VERSION_3_7_6,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.shd-max-threads",
     .voltype = "cluster/replicate",
     .op_version = GD_OP_VERSION_3_7_12,
     .flags = VOLOPT_FLAG_CLIENT_OPT,
     .validate_fn = validate_replica},
    {.key = "cluster.shd-wait-qlength",
     .voltype = "cluster/replicate",
     .op_version = GD_OP_VERSION_3_7_12,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.locking-scheme",
     .voltype = "cluster/replicate",
     .type = DOC,
     .op_version = GD_OP_VERSION_3_7_12,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.granular-entry-heal",
     .voltype = "cluster/replicate",
     .type = DOC,
     .op_version = GD_OP_VERSION_3_8_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .option = "revocation-secs",
        .key = "features.locks-revocation-secs",
        .voltype = "features/locks",
        .op_version = GD_OP_VERSION_3_9_0,
    },
    {
        .option = "revocation-clear-all",
        .key = "features.locks-revocation-clear-all",
        .voltype = "features/locks",
        .op_version = GD_OP_VERSION_3_9_0,
    },
    {
        .option = "revocation-max-blocked",
        .key = "features.locks-revocation-max-blocked",
        .voltype = "features/locks",
        .op_version = GD_OP_VERSION_3_9_0,
    },
    {
        .option = "monkey-unlocking",
        .key = "features.locks-monkey-unlocking",
        .voltype = "features/locks",
        .op_version = GD_OP_VERSION_3_9_0,
        .type = NO_DOC,
    },
    {
        .option = "notify-contention",
        .key = "features.locks-notify-contention",
        .voltype = "features/locks",
        .op_version = GD_OP_VERSION_4_0_0,
    },
    {
        .option = "notify-contention-delay",
        .key = "features.locks-notify-contention-delay",
        .voltype = "features/locks",
        .op_version = GD_OP_VERSION_4_0_0,
    },
    {.key = "disperse.shd-max-threads",
     .voltype = "cluster/disperse",
     .op_version = GD_OP_VERSION_3_9_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT,
     .validate_fn = validate_disperse},
    {.key = "disperse.shd-wait-qlength",
     .voltype = "cluster/disperse",
     .op_version = GD_OP_VERSION_3_9_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "disperse.cpu-extensions",
     .voltype = "cluster/disperse",
     .op_version = GD_OP_VERSION_3_9_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "disperse.self-heal-window-size",
     .voltype = "cluster/disperse",
     .op_version = GD_OP_VERSION_3_11_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.use-compound-fops",
     .voltype = "cluster/replicate",
     .value = "off",
     .type = DOC,
     .op_version = GD_OP_VERSION_3_8_4,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "performance.parallel-readdir",
     .voltype = "performance/readdir-ahead",
     .option = "parallel-readdir",
     .value = "off",
     .type = DOC,
     .op_version = GD_OP_VERSION_3_10_0,
     .validate_fn = validate_parallel_readdir,
     .description = "If this option is enabled, the readdir operation "
                    "is performed in parallel on all the bricks, thus "
                    "improving the performance of readdir. Note that "
                    "the performance improvement is higher in large "
                    "clusters"},
    {
        .key = "performance.rda-request-size",
        .voltype = "performance/readdir-ahead",
        .option = "rda-request-size",
        .value = "131072",
        .flags = VOLOPT_FLAG_CLIENT_OPT,
        .type = DOC,
        .op_version = GD_OP_VERSION_3_9_1,
    },
    {
        .key = "performance.rda-low-wmark",
        .voltype = "performance/readdir-ahead",
        .option = "rda-low-wmark",
        .type = NO_DOC,
        .flags = VOLOPT_FLAG_CLIENT_OPT,
        .op_version = GD_OP_VERSION_3_9_1,
    },
    {
        .key = "performance.rda-high-wmark",
        .voltype = "performance/readdir-ahead",
        .type = NO_DOC,
        .flags = VOLOPT_FLAG_CLIENT_OPT,
        .op_version = GD_OP_VERSION_3_9_1,
    },
    {.key = "performance.rda-cache-limit",
     .voltype = "performance/readdir-ahead",
     .value = "10MB",
     .type = DOC,
     .flags = VOLOPT_FLAG_CLIENT_OPT,
     .op_version = GD_OP_VERSION_3_9_1,
     .validate_fn = validate_rda_cache_limit},
    {
        .key = "performance.nl-cache-positive-entry",
        .voltype = "performance/nl-cache",
        .type = DOC,
        .flags = VOLOPT_FLAG_CLIENT_OPT,
        .op_version = GD_OP_VERSION_3_11_0,
        .description = "enable/disable storing of entries that were lookedup"
                       " and found to be present in the volume, thus lookup"
                       " on non existent file is served from the cache",
    },
    {
        .key = "performance.nl-cache-limit",
        .voltype = "performance/nl-cache",
        .value = "10MB",
        .flags = VOLOPT_FLAG_CLIENT_OPT,
        .op_version = GD_OP_VERSION_3_11_0,
    },
    {
        .key = "performance.nl-cache-timeout",
        .voltype = "performance/nl-cache",
        .flags = VOLOPT_FLAG_CLIENT_OPT,
        .op_version = GD_OP_VERSION_3_11_0,
    },

    /* Brick multiplexing options */
    {.key = GLUSTERD_BRICK_MULTIPLEX_KEY,
     .voltype = "mgmt/glusterd",
     .value = "disable",
     .op_version = GD_OP_VERSION_3_10_0,
     .validate_fn = validate_boolean,
     .type = GLOBAL_DOC,
     .description = "This global option can be used to enable/disable "
                    "brick multiplexing. Brick multiplexing ensures that "
                    "compatible brick instances can share one single "
                    "brick process."},
    {.key = GLUSTERD_VOL_CNT_PER_THRD,
     .voltype = "mgmt/glusterd",
     .value = GLUSTERD_VOL_CNT_PER_THRD_DEFAULT_VALUE,
     .op_version = GD_OP_VERSION_7_0,
     .validate_fn = validate_volume_per_thread_limit,
     .type = GLOBAL_NO_DOC,
     .description =
         "This option can be used to limit the number of volumes "
         "handled per thread to populate peer data.The option accepts "
         "values in the range of 5 to 200"},
    {.key = GLUSTERD_BRICKMUX_LIMIT_KEY,
     .voltype = "mgmt/glusterd",
     .value = GLUSTERD_BRICKMUX_LIMIT_DFLT_VALUE,
     .op_version = GD_OP_VERSION_3_12_0,
     .validate_fn = validate_mux_limit,
     .type = GLOBAL_DOC,
     .description = "This option can be used to limit the number of brick "
                    "instances per brick process when brick-multiplexing "
                    "is enabled. If not explicitly set, this tunable is "
                    "set to 0 which denotes that brick-multiplexing can "
                    "happen without any limit on the number of bricks per "
                    "process. Also this option can't be set when the "
                    "brick-multiplexing feature is disabled."},
    {.key = "disperse.optimistic-change-log",
     .voltype = "cluster/disperse",
     .type = NO_DOC,
     .op_version = GD_OP_VERSION_3_10_1,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "disperse.stripe-cache",
     .voltype = "cluster/disperse",
     .type = NO_DOC,
     .op_version = GD_OP_VERSION_4_0_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},

    /* Halo replication options */
    {.key = "cluster.halo-enabled",
     .voltype = "cluster/replicate",
     .op_version = GD_OP_VERSION_3_11_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.halo-shd-max-latency",
     .voltype = "cluster/replicate",
     .op_version = GD_OP_VERSION_3_11_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.halo-nfsd-max-latency",
     .voltype = "cluster/replicate",
     .op_version = GD_OP_VERSION_3_11_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.halo-max-latency",
     .voltype = "cluster/replicate",
     .op_version = GD_OP_VERSION_3_11_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.halo-max-replicas",
     .voltype = "cluster/replicate",
     .op_version = GD_OP_VERSION_3_11_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "cluster.halo-min-replicas",
     .voltype = "cluster/replicate",
     .op_version = GD_OP_VERSION_3_11_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = VKEY_FEATURES_SELINUX,
     .voltype = "features/selinux",
     .type = NO_DOC,
     .value = "on",
     .op_version = GD_OP_VERSION_3_11_0,
     .description = "Convert security.selinux xattrs to "
                    "trusted.gluster.selinux on the bricks. Recommended "
                    "to have enabled when clients and/or bricks support "
                    "SELinux."},
    {.key = GLUSTERD_LOCALTIME_LOGGING_KEY,
     .voltype = "mgmt/glusterd",
     .type = GLOBAL_DOC,
     .op_version = GD_OP_VERSION_3_12_0,
     .validate_fn = validate_boolean},
    {.key = GLUSTERD_DAEMON_LOG_LEVEL_KEY,
     .voltype = "mgmt/glusterd",
     .type = GLOBAL_NO_DOC,
     .value = "INFO",
     .op_version = GD_OP_VERSION_5_0},
    {.key = "debug.delay-gen",
     .voltype = "debug/delay-gen",
     .option = "!debug",
     .value = "off",
     .type = NO_DOC,
     .op_version = GD_OP_VERSION_3_13_0,
     .flags = VOLOPT_FLAG_XLATOR_OPT},
    {
        .key = "delay-gen.delay-percentage",
        .voltype = "debug/delay-gen",
        .type = NO_DOC,
        .op_version = GD_OP_VERSION_3_13_0,
    },
    {
        .key = "delay-gen.delay-duration",
        .voltype = "debug/delay-gen",
        .type = NO_DOC,
        .op_version = GD_OP_VERSION_3_13_0,
    },
    {
        .key = "delay-gen.enable",
        .voltype = "debug/delay-gen",
        .type = NO_DOC,
        .op_version = GD_OP_VERSION_3_13_0,
    },
    {.key = "disperse.parallel-writes",
     .voltype = "cluster/disperse",
     .type = NO_DOC,
     .op_version = GD_OP_VERSION_3_13_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "disperse.quorum-count",
     .voltype = "cluster/disperse",
     .type = NO_DOC,
     .op_version = GD_OP_VERSION_8_0,
     .validate_fn = validate_disperse_quorum_count,
     .description = "This option can be used to define how many successes on"
                    "the bricks constitute a success to the application. This"
                    " count should be in the range"
                    "[disperse-data-count,  disperse-count] (inclusive)",
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .key = "features.sdfs",
        .voltype = "features/sdfs",
        .value = "off",
        .option = "!features",
        .op_version = GD_OP_VERSION_4_0_0,
        .description = "enable/disable dentry serialization xlator in volume",
        .type = NO_DOC,
    },
    {.key = "features.cloudsync",
     .voltype = "features/cloudsync",
     .value = "off",
     .op_version = GD_OP_VERSION_4_1_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "features.ctime",
     .voltype = "features/utime",
     .validate_fn = validate_boolean,
     .value = "on",
     .option = "!utime",
     .op_version = GD_OP_VERSION_4_1_0,
     .description = "enable/disable utime translator on the volume.",
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT},
    {.key = "ctime.noatime",
     .voltype = "features/utime",
     .validate_fn = validate_boolean,
     .value = "on",
     .option = "noatime",
     .op_version = GD_OP_VERSION_5_0,
     .description = "enable/disable noatime option with ctime enabled.",
     .flags = VOLOPT_FLAG_CLIENT_OPT | VOLOPT_FLAG_XLATOR_OPT},
    {.key = "features.cloudsync-storetype",
     .voltype = "features/cloudsync",
     .op_version = GD_OP_VERSION_5_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "features.s3plugin-seckey",
     .voltype = "features/cloudsync",
     .op_version = GD_OP_VERSION_5_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "features.s3plugin-keyid",
     .voltype = "features/cloudsync",
     .op_version = GD_OP_VERSION_5_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "features.s3plugin-bucketid",
     .voltype = "features/cloudsync",
     .op_version = GD_OP_VERSION_5_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "features.s3plugin-hostname",
     .voltype = "features/cloudsync",
     .op_version = GD_OP_VERSION_5_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "features.enforce-mandatory-lock",
     .voltype = "features/locks",
     .value = "off",
     .type = NO_DOC,
     .op_version = GD_OP_VERSION_6_0,
     .validate_fn = validate_boolean,
     .description = "option to enforce mandatory lock on a file",
     .flags = VOLOPT_FLAG_XLATOR_OPT},
    {.key = VKEY_CONFIG_GLOBAL_THREADING,
     .voltype = "debug/io-stats",
     .option = "global-threading",
     .value = "off",
     .op_version = GD_OP_VERSION_6_0},
    {.key = VKEY_CONFIG_CLIENT_THREADS,
     .voltype = "debug/io-stats",
     .option = "!client-threads",
     .value = "16",
     .op_version = GD_OP_VERSION_6_0},
    {.key = VKEY_CONFIG_BRICK_THREADS,
     .voltype = "debug/io-stats",
     .option = "!brick-threads",
     .value = "16",
     .op_version = GD_OP_VERSION_6_0},
    {.key = "features.cloudsync-remote-read",
     .voltype = "features/cloudsync",
     .value = "off",
     .op_version = GD_OP_VERSION_7_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "features.cloudsync-store-id",
     .voltype = "features/cloudsync",
     .op_version = GD_OP_VERSION_7_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = "features.cloudsync-product-id",
     .voltype = "features/cloudsync",
     .op_version = GD_OP_VERSION_7_0,
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {
        .key = "features.acl",
        .voltype = "features/access-control",
        .value = "enable",
        .option = "!features",
        .op_version = GD_OP_VERSION_8_0,
        .description = "(WARNING: for debug purpose only) enable/disable "
                       "access-control xlator in volume",
        .type = NO_DOC,
    },

    {.key = "cluster.use-anonymous-inode",
     .voltype = "cluster/replicate",
     .op_version = GD_OP_VERSION_9_0,
     .value = "yes",
     .flags = VOLOPT_FLAG_CLIENT_OPT},
    {.key = NULL}};
