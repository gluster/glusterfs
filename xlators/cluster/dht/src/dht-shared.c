/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


/* TODO: add NS locking */
#include "statedump.h"
#include "dht-common.h"
#include "dht-messages.h"

#ifndef MAX
#define MAX(a, b) (((a) > (b))?(a):(b))
#endif

#define GF_DECIDE_DEFRAG_THROTTLE_COUNT(throttle_count, conf) {         \
                                                                        \
                pthread_mutex_lock (&conf->defrag->dfq_mutex);          \
                                                                        \
                if (!strcasecmp (conf->dthrottle, "lazy"))              \
                        conf->defrag->recon_thread_count = 1;           \
                                                                        \
                throttle_count =                                        \
                    MAX ((sysconf(_SC_NPROCESSORS_ONLN) - 4), 4);       \
                                                                        \
                if (!strcasecmp (conf->dthrottle, "normal"))            \
                        conf->defrag->recon_thread_count =              \
                                                 (throttle_count / 2);  \
                                                                        \
                if (!strcasecmp (conf->dthrottle, "aggressive"))        \
                        conf->defrag->recon_thread_count =              \
                                                 throttle_count;        \
                                                                        \
                pthread_mutex_unlock (&conf->defrag->dfq_mutex);        \
        }                                                               \

/* TODO:
   - use volumename in xattr instead of "dht"
   - use NS locks
   - handle all cases in self heal layout reconstruction
   - complete linkfile selfheal
*/
struct volume_options options[];

extern dht_methods_t dht_methods;

void
dht_layout_dump (dht_layout_t  *layout, const char *prefix)
{

        char    key[GF_DUMP_MAX_BUF_LEN];
        int     i = 0;

        if (!layout)
                goto out;
        if (!prefix)
                goto out;

        gf_proc_dump_build_key(key, prefix, "cnt");
        gf_proc_dump_write(key, "%d", layout->cnt);
        gf_proc_dump_build_key(key, prefix, "preset");
        gf_proc_dump_write(key, "%d", layout->preset);
        gf_proc_dump_build_key(key, prefix, "gen");
        gf_proc_dump_write(key, "%d", layout->gen);
        if (layout->type != IA_INVAL) {
                gf_proc_dump_build_key(key, prefix, "inode type");
                gf_proc_dump_write(key, "%d", layout->type);
        }

        if  (!IA_ISDIR (layout->type))
                goto out;

        for (i = 0; i < layout->cnt; i++) {
                gf_proc_dump_build_key(key, prefix,"list[%d].err", i);
                gf_proc_dump_write(key, "%d", layout->list[i].err);
                gf_proc_dump_build_key(key, prefix,"list[%d].start", i);
                gf_proc_dump_write(key, "%u", layout->list[i].start);
                gf_proc_dump_build_key(key, prefix,"list[%d].stop", i);
                gf_proc_dump_write(key, "%u", layout->list[i].stop);
                if (layout->list[i].xlator) {
                        gf_proc_dump_build_key(key, prefix,
                                               "list[%d].xlator.type", i);
                        gf_proc_dump_write(key, "%s",
                                           layout->list[i].xlator->type);
                        gf_proc_dump_build_key(key, prefix,
                                               "list[%d].xlator.name", i);
                        gf_proc_dump_write(key, "%s",
                                           layout->list[i].xlator->name);
                }
        }

out:
        return;
}


int32_t
dht_priv_dump (xlator_t *this)
{
        char            key_prefix[GF_DUMP_MAX_BUF_LEN];
        char            key[GF_DUMP_MAX_BUF_LEN];
        int             i = 0;
        dht_conf_t      *conf = NULL;
        int             ret = -1;

        if (!this)
                goto out;

        conf = this->private;
        if (!conf)
                goto out;

        ret = TRY_LOCK(&conf->subvolume_lock);
        if (ret != 0) {
                return ret;
        }

        gf_proc_dump_add_section("xlator.cluster.dht.%s.priv", this->name);
        gf_proc_dump_build_key(key_prefix,"xlator.cluster.dht","%s.priv",
                               this->name);
        gf_proc_dump_write("subvol_cnt","%d", conf->subvolume_cnt);
        for (i = 0; i < conf->subvolume_cnt; i++) {
                snprintf (key, sizeof (key), "subvolumes[%d]", i);
                gf_proc_dump_write(key, "%s.%s", conf->subvolumes[i]->type,
                                   conf->subvolumes[i]->name);
                if (conf->file_layouts && conf->file_layouts[i]){
                        snprintf (key, sizeof (key), "file_layouts[%d]", i);
                        dht_layout_dump(conf->file_layouts[i], key);
                }
                if (conf->dir_layouts && conf->dir_layouts[i]) {
                        snprintf (key, sizeof (key), "dir_layouts[%d]", i);
                        dht_layout_dump(conf->dir_layouts[i], key);
                }
                if (conf->subvolume_status) {

                        snprintf (key, sizeof (key), "subvolume_status[%d]", i);
                        gf_proc_dump_write(key, "%d",
                                           (int)conf->subvolume_status[i]);
                }

        }

        gf_proc_dump_write("search_unhashed", "%d", conf->search_unhashed);
        gf_proc_dump_write("gen", "%d", conf->gen);
        gf_proc_dump_write("min_free_disk", "%lf", conf->min_free_disk);
        gf_proc_dump_write("min_free_inodes", "%lf", conf->min_free_inodes);
        gf_proc_dump_write("disk_unit", "%c", conf->disk_unit);
        gf_proc_dump_write("refresh_interval", "%d", conf->refresh_interval);
        gf_proc_dump_write("unhashed_sticky_bit", "%d", conf->unhashed_sticky_bit);
        gf_proc_dump_write("use-readdirp", "%d", conf->use_readdirp);

        if (conf->du_stats && conf->subvolume_status) {
                for (i = 0; i < conf->subvolume_cnt; i++) {
                        if (!conf->subvolume_status[i])
                                continue;

                        snprintf (key, sizeof (key), "subvolumes[%d]", i);
                        gf_proc_dump_write (key, "%s",
                                            conf->subvolumes[i]->name);

                        snprintf (key, sizeof (key),
                                  "du_stats[%d].avail_percent", i);
                        gf_proc_dump_write (key, "%lf",
                                            conf->du_stats[i].avail_percent);

                        snprintf (key, sizeof (key), "du_stats[%d].avail_space",
                                  i);
                        gf_proc_dump_write (key, "%lu",
                                            conf->du_stats[i].avail_space);

                        snprintf (key, sizeof (key),
                                  "du_stats[%d].avail_inodes", i);
                        gf_proc_dump_write (key, "%lf",
                                            conf->du_stats[i].avail_inodes);

                        snprintf (key, sizeof (key), "du_stats[%d].log", i);
                        gf_proc_dump_write (key, "%lu",
                                            conf->du_stats[i].log);
                }
        }

        if (conf->last_stat_fetch.tv_sec)
                gf_proc_dump_write("last_stat_fetch", "%s",
                                    ctime(&conf->last_stat_fetch.tv_sec));

        UNLOCK(&conf->subvolume_lock);

out:
        return ret;
}

int32_t
dht_inodectx_dump (xlator_t *this, inode_t *inode)
{
        int             ret = -1;
        dht_layout_t    *layout = NULL;

        if (!this)
                goto out;
        if (!inode)
                goto out;

        ret = dht_inode_ctx_layout_get (inode, this, &layout);

        if ((ret != 0) || !layout)
                return ret;

        gf_proc_dump_add_section("xlator.cluster.dht.%s.inode", this->name);
        dht_layout_dump(layout, "layout");

out:
        return ret;
}

void
dht_fini (xlator_t *this)
{
        int         i = 0;
        dht_conf_t *conf = NULL;

        GF_VALIDATE_OR_GOTO ("dht", this, out);

        conf = this->private;
        this->private = NULL;
        if (conf) {
                if (conf->file_layouts) {
                        for (i = 0; i < conf->subvolume_cnt; i++) {
                                GF_FREE (conf->file_layouts[i]);
                        }
                        GF_FREE (conf->file_layouts);
                }

                dict_unref(conf->leaf_to_subvol);

                GF_FREE (conf->subvolumes);

                GF_FREE (conf->subvolume_status);

                if (conf->lock_pool)
                        mem_pool_destroy (conf->lock_pool);

                GF_FREE (conf);
        }
out:
        return;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        GF_VALIDATE_OR_GOTO ("dht", this, out);

        ret = xlator_mem_acct_init (this, gf_dht_mt_end + 1);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_NO_MEMORY,
                        "Memory accounting init failed");
                return ret;
        }
out:
        return ret;
}


int
dht_parse_decommissioned_bricks (xlator_t *this, dht_conf_t *conf,
                                 const char *bricks)
{
        int         i  = 0;
        int         ret  = -1;
        char       *tmpstr = NULL;
        char       *dup_brick = NULL;
        char       *node = NULL;

        if (!conf || !bricks)
                goto out;

        dup_brick = gf_strdup (bricks);
        node = strtok_r (dup_brick, ",", &tmpstr);
        while (node) {
                for (i = 0; i < conf->subvolume_cnt; i++) {
                        if (!strcmp (conf->subvolumes[i]->name, node)) {
                                conf->decommissioned_bricks[i] =
                                        conf->subvolumes[i];
                                        conf->decommission_subvols_cnt++;
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        DHT_MSG_SUBVOL_DECOMMISSION_INFO,
                                        "decommissioning subvolume %s",
                                        conf->subvolumes[i]->name);
                                break;
                        }
                }
                if (i == conf->subvolume_cnt) {
                        /* Wrong node given. */
                        goto out;
                }
                node = strtok_r (NULL, ",", &tmpstr);
        }

        ret = 0;
        conf->decommission_in_progress = 1;
out:
        GF_FREE (dup_brick);

        return ret;
}

int
dht_decommissioned_remove (xlator_t *this, dht_conf_t *conf)
{
        int         i  = 0;
        int         ret  = -1;

        if (!conf)
                goto out;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (conf->decommissioned_bricks[i]) {
                        conf->decommissioned_bricks[i] = NULL;
                        conf->decommission_subvols_cnt--;
                }
        }

        ret = 0;
out:

        return ret;
}
void
dht_init_regex (xlator_t *this, dict_t *odict, char *name,
                regex_t *re, gf_boolean_t *re_valid, dht_conf_t *conf)
{
        char       *temp_str = NULL;

        if (dict_get_str (odict, name, &temp_str) != 0) {
                if (strcmp(name,"rsync-hash-regex")) {
                        return;
                }
                temp_str = "^\\.(.+)\\.[^.]+$";
        }

        LOCK (&conf->lock);
        {
                if (*re_valid) {
                        regfree(re);
                        *re_valid = _gf_false;
                }

                if (!strcmp(temp_str, "none")) {
                        goto unlock;
                }

                if (regcomp(re, temp_str, REG_EXTENDED) == 0) {
                        gf_msg_debug (this->name, 0,
                                      "using regex %s = %s", name, temp_str);
                        *re_valid = _gf_true;
                } else {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_REGEX_INFO,
                                "compiling regex %s failed", temp_str);
                }
        }
unlock:
        UNLOCK (&conf->lock);
}

int
dht_set_subvol_range(xlator_t *this)
{
        int ret = -1;
        dht_conf_t *conf = NULL;

        conf = this->private;

        if (!conf)
                goto out;

        conf->leaf_to_subvol = dict_new();
        if (!conf->leaf_to_subvol)
                goto out;

        ret = glusterfs_reachable_leaves(this, conf->leaf_to_subvol);

out:
        return ret;
}

int
dht_reconfigure (xlator_t *this, dict_t *options)
{
        dht_conf_t      *conf = NULL;
        char            *temp_str = NULL;
        gf_boolean_t     search_unhashed;
        int              ret = -1;
        int              throttle_count = 0;

        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", options, out);

        conf = this->private;
        if (!conf)
                return 0;

        if (dict_get_str (options, "lookup-unhashed", &temp_str) == 0) {
                /* If option is not "auto", other options _should_ be boolean*/
                if (strcasecmp (temp_str, "auto")) {
                        if (!gf_string2boolean (temp_str, &search_unhashed)) {
                                gf_msg_debug(this->name, 0, "Reconfigure: "
                                             "lookup-unhashed reconfigured(%s)",
                                             temp_str);
                                conf->search_unhashed = search_unhashed;
                        } else {
                                gf_msg(this->name, GF_LOG_ERROR, 0,
                                       DHT_MSG_INVALID_OPTION,
                                       "Invalid option: Reconfigure: "
                                       "lookup-unhashed should be boolean,"
                                       " not (%s), defaulting to (%d)",
                                       temp_str, conf->search_unhashed);
                                ret = -1;
                                goto out;
                        }
                } else {
                        gf_msg_debug(this->name, 0, "Reconfigure:"
                                     " lookup-unhashed reconfigured auto ");
                        conf->search_unhashed = GF_DHT_LOOKUP_UNHASHED_AUTO;
                }
        }

        GF_OPTION_RECONF ("lookup-optimize", conf->lookup_optimize, options,
                          bool, out);

        GF_OPTION_RECONF ("min-free-disk", conf->min_free_disk, options,
                          percent_or_size, out);
        /* option can be any one of percent or bytes */
        conf->disk_unit = 0;
        if (conf->min_free_disk < 100.0)
                conf->disk_unit = 'p';

        GF_OPTION_RECONF ("min-free-inodes", conf->min_free_inodes, options,
                          percent, out);

        GF_OPTION_RECONF ("directory-layout-spread", conf->dir_spread_cnt,
                          options, uint32, out);

        GF_OPTION_RECONF ("readdir-optimize", conf->readdir_optimize, options,
                          bool, out);
        GF_OPTION_RECONF ("randomize-hash-range-by-gfid",
                          conf->randomize_by_gfid,
                          options, bool, out);

        GF_OPTION_RECONF ("rebal-throttle", conf->dthrottle, options,
                          str, out);

        GF_OPTION_RECONF ("lock-migration", conf->lock_migration_enabled,
                          options, bool, out);

        if (conf->defrag) {
                conf->defrag->lock_migration_enabled =
                                        conf->lock_migration_enabled;

                GF_DECIDE_DEFRAG_THROTTLE_COUNT (throttle_count, conf);
                gf_msg ("DHT", GF_LOG_INFO, 0,
                        DHT_MSG_REBAL_THROTTLE_INFO,
                        "conf->dthrottle: %s, "
                        "conf->defrag->recon_thread_count: %d",
                         conf->dthrottle, conf->defrag->recon_thread_count);
        }

        if (conf->defrag) {
                GF_OPTION_RECONF ("rebalance-stats", conf->defrag->stats,
                                  options, bool, out);
        }

        if (dict_get_str (options, "decommissioned-bricks", &temp_str) == 0) {
                ret = dht_parse_decommissioned_bricks (this, conf, temp_str);
                if (ret == -1)
                        goto out;
        } else {
                ret = dht_decommissioned_remove (this, conf);
                if (ret == -1)
                        goto out;
        }

        dht_init_regex (this, options, "rsync-hash-regex",
                        &conf->rsync_regex, &conf->rsync_regex_valid, conf);
        dht_init_regex (this, options, "extra-hash-regex",
                        &conf->extra_regex, &conf->extra_regex_valid, conf);

        GF_OPTION_RECONF ("weighted-rebalance", conf->do_weighting, options,
                          bool, out);

        GF_OPTION_RECONF ("use-readdirp", conf->use_readdirp, options,
                          bool, out);
        ret = 0;
out:
        return ret;
}

static int
gf_defrag_pattern_list_fill (xlator_t *this, gf_defrag_info_t *defrag, char *data)
{
        int                    ret = -1;
        char                  *tmp_str = NULL;
        char                  *tmp_str1 = NULL;
        char                  *dup_str = NULL;
        char                  *num = NULL;
        char                  *pattern_str = NULL;
        char                  *pattern = NULL;
        gf_defrag_pattern_list_t *temp_list = NULL;
        gf_defrag_pattern_list_t *pattern_list = NULL;

        if (!this || !defrag || !data)
                goto out;

        /* Get the pattern for pattern list. "pattern:<optional-size>"
         * eg: *avi, *pdf:10MB, *:1TB
         */
        pattern_str = strtok_r (data, ",", &tmp_str);
        while (pattern_str) {
                dup_str = gf_strdup (pattern_str);
                pattern_list = GF_CALLOC (1, sizeof (gf_defrag_pattern_list_t),
                                        1);
                if (!pattern_list) {
                        goto out;
                }
                pattern = strtok_r (dup_str, ":", &tmp_str1);
                num = strtok_r (NULL, ":", &tmp_str1);
                if (!pattern)
                        goto out;
                if (!num) {
                        if (gf_string2bytesize_uint64(pattern, &pattern_list->size)
                             == 0) {
                                pattern = "*";
                        }
                } else if (gf_string2bytesize_uint64 (num, &pattern_list->size) != 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_INVALID_OPTION,
                                "Invalid option. Defrag pattern:"
                                " Invalid number format \"%s\"", num);
                        goto out;
                }
                memcpy (pattern_list->path_pattern, pattern, strlen (dup_str));

                if (!defrag->defrag_pattern)
                        temp_list = NULL;
                else
                        temp_list = defrag->defrag_pattern;

                pattern_list->next = temp_list;

                defrag->defrag_pattern = pattern_list;
                pattern_list = NULL;

                GF_FREE (dup_str);
                dup_str = NULL;

                pattern_str = strtok_r (NULL, ",", &tmp_str);
        }

        ret = 0;
out:
        if (ret)
                GF_FREE (pattern_list);
        GF_FREE (dup_str);

        return ret;
}



int
dht_init_methods (xlator_t *this)
{
        int ret                  = -1;
        dht_conf_t      *conf    = NULL;
        dht_methods_t   *methods = NULL;

        GF_VALIDATE_OR_GOTO ("dht", this, err);

        conf = this->private;
        methods = &(conf->methods);

        methods->migration_get_dst_subvol = dht_migration_get_dst_subvol;
        methods->migration_needed = dht_migration_needed;
        methods->migration_other  = NULL;
        methods->layout_search    = dht_layout_search;

        ret = 0;
err:
        return ret;
}

int
dht_init (xlator_t *this)
{
        dht_conf_t                      *conf           = NULL;
        char                            *temp_str       = NULL;
        int                              ret            = -1;
        int                              i              = 0;
        gf_defrag_info_t                *defrag         = NULL;
        int                              cmd            = 0;
        char                            *node_uuid      = NULL;
        int                              throttle_count = 0;
        uint32_t                         commit_hash    = 0;

        GF_VALIDATE_OR_GOTO ("dht", this, err);

        if (!this->children) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        DHT_MSG_INVALID_CONFIGURATION,
                        "Distribute needs more than one subvolume");
                return -1;
        }

        if (!this->parents) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_INVALID_CONFIGURATION,
                        "dangling volume. check volfile");
        }

        conf = GF_CALLOC (1, sizeof (*conf), gf_dht_mt_dht_conf_t);
        if (!conf) {
                goto err;
        }

        LOCK_INIT (&conf->subvolume_lock);
        LOCK_INIT (&conf->layout_lock);
        LOCK_INIT (&conf->lock);

        /* We get the commit-hash to set only for rebalance process */
        if (dict_get_uint32 (this->options,
                             "commit-hash", &commit_hash) == 0) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        DHT_MSG_COMMIT_HASH_INFO, "%s using commit hash %u",
                        __func__, commit_hash);
                conf->vol_commit_hash = commit_hash;
                conf->vch_forced = _gf_true;
        }

        ret = dict_get_int32 (this->options, "rebalance-cmd", &cmd);

        if (cmd) {
                defrag = GF_CALLOC (1, sizeof (gf_defrag_info_t),
                                    gf_defrag_info_mt);

                GF_VALIDATE_OR_GOTO (this->name, defrag, err);

                LOCK_INIT (&defrag->lock);

                defrag->is_exiting = 0;

                conf->defrag = defrag;

                ret = dict_get_str (this->options, "node-uuid", &node_uuid);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_INVALID_CONFIGURATION,
                                "Invalid volume configuration: "
                                "node-uuid not specified");
                        goto err;
                }

                if (gf_uuid_parse (node_uuid, defrag->node_uuid)) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_INVALID_OPTION, "Invalid option:"
                                " Cannot parse glusterd node uuid");
                        goto err;
                }

                defrag->cmd = cmd;

                defrag->stats = _gf_false;

                defrag->queue = NULL;

                defrag->crawl_done = 0;

                defrag->global_error = 0;

                defrag->q_entry_count = 0;

                defrag->wakeup_crawler = 0;

                synclock_init (&defrag->link_lock, SYNC_LOCK_DEFAULT);
                pthread_mutex_init (&defrag->dfq_mutex, 0);
                pthread_cond_init  (&defrag->parallel_migration_cond, 0);
                pthread_cond_init  (&defrag->rebalance_crawler_alarm, 0);
                pthread_cond_init  (&defrag->df_wakeup_thread, 0);

                defrag->global_error = 0;

        }

        conf->search_unhashed = GF_DHT_LOOKUP_UNHASHED_ON;
        if (dict_get_str (this->options, "lookup-unhashed", &temp_str) == 0) {
                /* If option is not "auto", other options _should_ be boolean */
                if (strcasecmp (temp_str, "auto")) {
                        ret = gf_string2boolean (temp_str,
                                                 &conf->search_unhashed);
                        if (ret == -1)
                                goto err;
                }
                else
                        conf->search_unhashed = GF_DHT_LOOKUP_UNHASHED_AUTO;
        }

        GF_OPTION_INIT ("lookup-optimize", conf->lookup_optimize, bool, err);

        GF_OPTION_INIT ("unhashed-sticky-bit", conf->unhashed_sticky_bit, bool,
                        err);

        GF_OPTION_INIT ("use-readdirp", conf->use_readdirp, bool, err);

        GF_OPTION_INIT ("min-free-disk", conf->min_free_disk, percent_or_size,
                        err);

        GF_OPTION_INIT ("min-free-inodes", conf->min_free_inodes, percent,
                        err);

        conf->dir_spread_cnt = conf->subvolume_cnt;
        GF_OPTION_INIT ("directory-layout-spread", conf->dir_spread_cnt,
                        uint32, err);

        GF_OPTION_INIT ("assert-no-child-down", conf->assert_no_child_down,
                        bool, err);

        GF_OPTION_INIT ("readdir-optimize", conf->readdir_optimize, bool, err);


        GF_OPTION_INIT ("lock-migration", conf->lock_migration_enabled,
                         bool, err);

        if (defrag) {
              defrag->lock_migration_enabled = conf->lock_migration_enabled;

              GF_OPTION_INIT ("rebalance-stats", defrag->stats, bool, err);
                if (dict_get_str (this->options, "rebalance-filter", &temp_str)
                    == 0) {
                        if (gf_defrag_pattern_list_fill (this, defrag, temp_str)
                            == -1) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        DHT_MSG_INVALID_OPTION,
                                        "Invalid option:"
                                        " Cannot parse rebalance-filter (%s)",
                                        temp_str);

                                goto err;
                        }
                }
        }

        /* option can be any one of percent or bytes */
        conf->disk_unit = 0;
        if (conf->min_free_disk < 100)
                conf->disk_unit = 'p';

        ret = dht_init_subvolumes (this, conf);
        if (ret == -1) {
                goto err;
        }

        if (cmd) {
                ret = dht_init_local_subvolumes (this, conf);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_INIT_LOCAL_SUBVOL_FAILED,
                                "dht_init_local_subvolumes failed");
                        goto err;
                }
        }

        if (dict_get_str (this->options, "decommissioned-bricks", &temp_str) == 0) {
                ret = dht_parse_decommissioned_bricks (this, conf, temp_str);
                if (ret == -1)
                        goto err;
        }

        dht_init_regex (this, this->options, "rsync-hash-regex",
                        &conf->rsync_regex, &conf->rsync_regex_valid, conf);
        dht_init_regex (this, this->options, "extra-hash-regex",
                        &conf->extra_regex, &conf->extra_regex_valid, conf);

        ret = dht_layouts_init (this, conf);
        if (ret == -1) {
                goto err;
        }


        conf->gen = 1;

        this->local_pool = mem_pool_new (dht_local_t, 512);
        if (!this->local_pool) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        DHT_MSG_NO_MEMORY,
                        " DHT initialisation failed. "
                        "failed to create local_t's memory pool");
                goto err;
        }

        GF_OPTION_INIT ("randomize-hash-range-by-gfid",
                        conf->randomize_by_gfid, bool, err);

        if (defrag) {
                GF_OPTION_INIT ("rebal-throttle",
                                 conf->dthrottle, str, err);

                GF_DECIDE_DEFRAG_THROTTLE_COUNT(throttle_count, conf);

                gf_msg_debug ("DHT", 0, "conf->dthrottle: %s, "
                              "conf->defrag->recon_thread_count: %d",
                              conf->dthrottle,
                              conf->defrag->recon_thread_count);
        }

        GF_OPTION_INIT ("xattr-name", conf->xattr_name, str, err);
        gf_asprintf (&conf->link_xattr_name, "%s."DHT_LINKFILE_STR,
                     conf->xattr_name);
        gf_asprintf (&conf->commithash_xattr_name, "%s."DHT_COMMITHASH_STR,
                     conf->xattr_name);
        gf_asprintf (&conf->wild_xattr_name, "%s*", conf->xattr_name);
        if (!conf->link_xattr_name || !conf->wild_xattr_name) {
                goto err;
        }

        GF_OPTION_INIT ("weighted-rebalance", conf->do_weighting, bool, err);

        conf->lock_pool = mem_pool_new (dht_lock_t, 512);
        if (!conf->lock_pool) {
                gf_msg (this->name, GF_LOG_ERROR, 0, DHT_MSG_INIT_FAILED,
                        "failed to create lock mem_pool, failing "
                        "initialization");
                goto err;
        }

        this->private = conf;

        if (dht_set_subvol_range(this))
                goto err;

        if (dht_init_methods (this))
                goto err;

        return 0;

err:
        if (conf) {
                if (conf->file_layouts) {
                        for (i = 0; i < conf->subvolume_cnt; i++) {
                                GF_FREE (conf->file_layouts[i]);
                        }
                        GF_FREE (conf->file_layouts);
                }

                GF_FREE (conf->subvolumes);

                GF_FREE (conf->subvolume_status);

                GF_FREE (conf->du_stats);

                GF_FREE (conf->defrag);

                GF_FREE (conf->xattr_name);
                GF_FREE (conf->link_xattr_name);
                GF_FREE (conf->wild_xattr_name);

                if (conf->lock_pool)
                        mem_pool_destroy (conf->lock_pool);

                GF_FREE (conf);
        }

        return -1;
}


struct volume_options options[] = {
        { .key  = {"lookup-unhashed"},
          .value = {"auto", "yes", "no", "enable", "disable", "1", "0",
                    "on", "off"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "on",
          .description = "This option if set to ON, does a lookup through "
          "all the sub-volumes, in case a lookup didn't return any result "
          "from the hash subvolume. If set to OFF, it does not do a lookup "
          "on the remaining subvolumes."
        },
        { .key = {"lookup-optimize"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "This option if set to ON enables the optimization "
          "of -ve lookups, by not doing a lookup on non-hashed subvolumes for "
          "files, in case the hashed subvolume does not return any result. "
          "This option disregards the lookup-unhashed setting, when enabled."
        },
        { .key  = {"min-free-disk"},
          .type = GF_OPTION_TYPE_PERCENT_OR_SIZET,
          .default_value = "10%",
          .description = "Percentage/Size of disk space, after which the "
          "process starts balancing out the cluster, and logs will appear "
          "in log files",
        },
        { .key  = {"min-free-inodes"},
          .type = GF_OPTION_TYPE_PERCENT,
          .default_value = "5%",
          .description = "after system has only N% of inodes, warnings "
          "starts to appear in log files",
        },
        { .key = {"unhashed-sticky-bit"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
        },
        { .key = {"use-readdirp"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "This option if set to ON, forces the use of "
          "readdirp, and hence also displays the stats of the files."
        },
        { .key = {"assert-no-child-down"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "This option if set to ON, in the event of "
          "CHILD_DOWN, will call exit."
        },
        { .key  = {"directory-layout-spread"},
          .type = GF_OPTION_TYPE_INT,
          .min  = 1,
          .validate = GF_OPT_VALIDATE_MIN,
          .description = "Specifies the directory layout spread. Takes number "
                         "of subvolumes as default value."
        },
        { .key  = {"decommissioned-bricks"},
          .type = GF_OPTION_TYPE_ANY,
          .description = "This option if set to ON, decommissions "
          "the brick, so that no new data is allowed to be created "
          "on that brick."
        },
        { .key  = {"rebalance-cmd"},
          .type = GF_OPTION_TYPE_INT,
        },
        { .key = {"commit-hash"},
          .type = GF_OPTION_TYPE_INT,
        },
        { .key = {"node-uuid"},
          .type = GF_OPTION_TYPE_STR,
        },
        { .key = {"rebalance-stats"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "This option if set to ON displays and logs the "
          " time taken for migration of each file, during the rebalance "
          "process. If set to OFF, the rebalance logs will only display the "
          "time spent in each directory."
        },
        { .key = {"readdir-optimize"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "This option if set to ON enables the optimization "
          "that allows DHT to requests non-first subvolumes to filter out "
          "directory entries."
        },
        { .key = {"rsync-hash-regex"},
          .type = GF_OPTION_TYPE_STR,
          /* Setting a default here doesn't work.  See dht_init_regex. */
          .description = "Regular expression for stripping temporary-file "
          "suffix and prefix used by rsync, to prevent relocation when the "
          "file is renamed."
        },
        { .key = {"extra-hash-regex"},
          .type = GF_OPTION_TYPE_STR,
          /* Setting a default here doesn't work.  See dht_init_regex. */
          .description = "Regular expression for stripping temporary-file "
          "suffix and prefix used by an application, to prevent relocation when "
          "the file is renamed."
        },
        { .key = {"rebalance-filter"},
          .type = GF_OPTION_TYPE_STR,
        },

        { .key = {"xattr-name"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "trusted.glusterfs.dht",
          .description = "Base for extended attributes used by this "
          "translator instance, to avoid conflicts with others above or "
          "below it."
        },

        { .key = {"weighted-rebalance"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "When enabled, files will be allocated to bricks "
          "with a probability proportional to their size.  Otherwise, all "
          "bricks will have the same probability (legacy behavior)."
        },

        /* NUFA option */
        { .key  = {"local-volume-name"},
          .type = GF_OPTION_TYPE_XLATOR
        },

        /* tier options */
        { .key  = {"tier-pause"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
        },

        { .key  = {"tier-promote-frequency"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "120",
          .description = "Frequency to promote files to fast tier"
        },

        { .key  = {"tier-demote-frequency"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "3600",
          .description = "Frequency to demote files to slow tier"
        },

        { .key  = {"write-freq-threshold"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "0",
        },

        { .key  = {"read-freq-threshold"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "0",
        },
        { .key         = {"watermark-hi"},
          .type = GF_OPTION_TYPE_PERCENT,
          .default_value = "90",
        },
        { .key         = {"watermark-low"},
          .type = GF_OPTION_TYPE_PERCENT,
          .default_value = "75",
        },
        { .key         = {"tier-mode"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "test",
        },
        { .key         = {"tier-compact"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
        },
        { .key         = {"tier-hot-compact-frequency"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "604800",
          .description = "Frequency to compact DBs on hot tier in system"
        },
        { .key         = {"tier-cold-compact-frequency"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "604800",
          .description = "Frequency to compact DBs on cold tier in system"
        },
        { .key         = {"tier-max-mb"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "4000",
        },
        { .key         = {"tier-max-promote-file-size"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "0",
        },
        { .key         = {"tier-max-files"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "10000",
        },
        { .key         = {"tier-query-limit"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = "100",
        },
        /* switch option */
        { .key  = {"pattern.switch.case"},
          .type = GF_OPTION_TYPE_ANY
        },

        { .key =  {"randomize-hash-range-by-gfid"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "Use gfid of directory to determine the subvolume "
          "from which hash ranges are allocated starting with 0. "
          "Note that we still use a directory/file's name to determine the "
          "subvolume to which it hashes"
        },

        { .key =  {"rebal-throttle"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "normal",
          .description = " Sets the maximum number of parallel file migrations "
                         "allowed on a node during the rebalance operation. The"
                         " default value is normal and allows a max of "
                         "[($(processing units) - 4) / 2), 2]  files to be "
                         "migrated at a time. Lazy will allow only one file to "
                         "be migrated at a time and aggressive will allow "
                         "max of [($(processing units) - 4) / 2), 4]"
        },

        { .key =  {"lock-migration"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = " If enabled this feature will migrate the posix locks"
                         " associated with a file during rebalance"
        },

        { .key  = {NULL} },
};
