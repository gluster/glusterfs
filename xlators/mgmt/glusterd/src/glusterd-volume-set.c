/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterd-volgen.h"
#include "glusterd-utils.h"

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
                gf_log (this->name, GF_LOG_ERROR, "%s", errstr);
                *op_errstr = gf_strdup (errstr);
                ret = -1;
                goto out;
        }

out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

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
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to get the quota status");
                goto out;
        }

        if (ret == _gf_false) {
                snprintf (errstr, sizeof (errstr),
                          "Cannot set %s. Enable quota first.", key);
                gf_log (this->name, GF_LOG_ERROR, "%s", errstr);
                *op_errstr = gf_strdup (errstr);
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

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
                gf_log (this->name, GF_LOG_ERROR, "%s", errstr);
                *op_errstr = gf_strdup (errstr);
                goto out;
        }
out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

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
                gf_log (this->name, GF_LOG_ERROR, "%s", errstr);
                *op_errstr = gf_strdup (errstr);
                ret = -1;
               goto out;
        }

out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

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
                gf_log (this->name, GF_LOG_ERROR,
                        "%s.", errstr);
                *op_errstr = gf_strdup (errstr);
                ret = -1;
                goto out;
        }

out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

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
validate_disperse_heal_enable_disable (glusterd_volinfo_t *volinfo,
                                       dict_t *dict, char *key, char *value,
                                       char **op_errstr)
{
        int                  ret = 0;

        if (volinfo->type != GF_CLUSTER_TYPE_DISPERSE) {
                gf_asprintf (op_errstr, "Volume %s is not of disperse type",
                             volinfo->volname);
                ret = -1;
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
        { .key        = "cluster.metadata-self-heal",
          .voltype    = "cluster/replicate",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.data-self-heal",
          .voltype    = "cluster/replicate",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key        = "cluster.entry-self-heal",
          .voltype    = "cluster/replicate",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT
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

        /* Stripe xlator options */
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
          .option      = "!brick-log-level",
          .op_version  = 1
        },
        { .key        = "diagnostics.client-log-level",
          .voltype    = "debug/io-stats",
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
        { .key         = "performance.least-rate-limit",
          .voltype     = "performance/io-threads",
          .op_version  = 2
        },

        /* Other perf xlators' options */
        { .key        = "performance.cache-size",
          .voltype    = "performance/quick-read",
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
          .type       = NO_DOC,
          .op_version = 2,
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
        { .key         = "network.tcp-window-size",
          .voltype     = "protocol/server",
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
          .type        = NO_DOC,
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
          .value       = "off",
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
          .value       = "off",
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

	/* Feature translators */
        { .key         = "features.file-snapshot",
          .voltype     = "features/qemu-block",
          .option      = "!feat",
          .value       = "off",
          .op_version  = 3,
          .description = "enable/disable file-snapshot feature in the "
                         "volume.",
          .flags       = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT
        },

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
          .description = "Entry point directory for entering snapshot world"
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
          .flags       = OPT_FLAG_FORCE,
          .op_version  = 1
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
          .op_version  = 1
        },
        { .key         = "nfs-ganesha.enable",
          .voltype     = "nfs/server",
          .option      = "!nfs-ganesha.enable",
          .op_version  = GD_OP_VERSION_3_6_0,
        },
        { .key         = "nfs-ganesha.host",
          .voltype     = "nfs/server",
          .option      = "!nfs-ganesha.host",
          .op_version  = GD_OP_VERSION_3_6_0,
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

        /* Other options which don't fit any place above */
        { .key        = "features.read-only",
          .voltype    = "features/read-only",
          .option     = "!read-only",
          .value      = "off",
          .op_version = 1,
          .flags      = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT
        },
        { .key        = "features.worm",
          .voltype    = "features/worm",
          .option     = "!worm",
          .value      = "off",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT | OPT_FLAG_XLATOR_OPT
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
          .voltype    = "configuration",
          .option     = "!config",
          .op_version = 2,
          .flags      = OPT_FLAG_CLIENT_OPT
        },
        { .key         = "config.transport",
          .voltype     = "configuration",
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
        { .key         = "features.barrier",
          .voltype     = "features/barrier",
          .value       = "disable",
          .type        = NO_DOC,
          .op_version  = GD_OP_VERSION_3_6_0,
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
        { .key         = "locks.trace",
          .voltype     = "features/locks",
          .value       = "disable",
          .type        = NO_DOC,
          .op_version  = GD_OP_VERSION_3_7_0,
        },
        { .key           = "cluster.disperse-self-heal-daemon",
          .voltype       = "cluster/disperse",
          .value         = "enable",
          .type          = NO_DOC,
          .option        = "self-heal-daemon",
          .op_version    = GD_OP_VERSION_3_7_0,
          .validate_fn   = validate_disperse_heal_enable_disable
        },
        { .key        = "features.bitrot",
          .voltype    = "features/bitrot-stub",
          .value      = "disable",
          .op_version = GD_OP_VERSION_3_7_0,
          .type       = NO_DOC,
        },
        { .key         = NULL
        }
};
