/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
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

/* TODO: add NS locking */

#include "statedump.h"
#include "dht-common.h"

/* TODO:
   - use volumename in xattr instead of "dht"
   - use NS locks
   - handle all cases in self heal layout reconstruction
   - complete linkfile selfheal
*/
struct volume_options options[];

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
                sprintf (key, "subvolumes[%d]", i);
                gf_proc_dump_write(key, "%s.%s", conf->subvolumes[i]->type,
                                   conf->subvolumes[i]->name);
                if (conf->file_layouts && conf->file_layouts[i]){
                        sprintf (key, "file_layouts[%d]", i);
                        dht_layout_dump(conf->file_layouts[i], key);
                }
                if (conf->dir_layouts && conf->dir_layouts[i]) {
                        sprintf (key, "dir_layouts[%d]", i);
                        dht_layout_dump(conf->dir_layouts[i], key);
                }
                if (conf->subvolume_status) {

                        sprintf (key, "subvolume_status[%d]", i);
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
        if (conf ->du_stats) {
                gf_proc_dump_write("du_stats.avail_percent", "%lf",
                                   conf->du_stats->avail_percent);
                gf_proc_dump_write("du_stats.avail_space", "%lu",
                                   conf->du_stats->avail_space);
		gf_proc_dump_write("du_stats.avail_inodes", "%lf",
                                   conf->du_stats->avail_inodes);
                gf_proc_dump_write("du_stats.log", "%lu", conf->du_stats->log);
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

                GF_FREE (conf->subvolumes);

                GF_FREE (conf->subvolume_status);

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
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        "failed");
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
                                gf_log (this->name, GF_LOG_INFO,
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
                regex_t *re, gf_boolean_t *re_valid)
{
        char    *temp_str;

        if (dict_get_str (odict, name, &temp_str) != 0) {
                if (strcmp(name,"rsync-hash-regex")) {
                        return;
                }
                temp_str = "^\\.(.+)\\.[^.]+$";
        }

        if (*re_valid) {
                regfree(re);
                *re_valid = _gf_false;
        }

        if (!strcmp(temp_str,"none")) {
                return;
        }

        if (regcomp(re,temp_str,REG_EXTENDED) == 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "using regex %s = %s", name, temp_str);
                *re_valid = _gf_true;
        }
        else {
                gf_log (this->name, GF_LOG_WARNING,
                        "compiling regex %s failed", temp_str);
        }
}

int
dht_reconfigure (xlator_t *this, dict_t *options)
{
        dht_conf_t      *conf = NULL;
        char            *temp_str = NULL;
        gf_boolean_t     search_unhashed;
        int              ret = -1;

        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", options, out);

        conf = this->private;
        if (!conf)
                return 0;

        if (dict_get_str (options, "lookup-unhashed", &temp_str) == 0) {
                /* If option is not "auto", other options _should_ be boolean*/
                if (strcasecmp (temp_str, "auto")) {
                        if (!gf_string2boolean (temp_str, &search_unhashed)) {
                                gf_log(this->name, GF_LOG_DEBUG, "Reconfigure:"
                                       " lookup-unhashed reconfigured (%s)",
                                       temp_str);
                                conf->search_unhashed = search_unhashed;
                        } else {
                                gf_log(this->name, GF_LOG_ERROR, "Reconfigure:"
                                       " lookup-unhashed should be boolean,"
                                       " not (%s), defaulting to (%d)",
                                       temp_str, conf->search_unhashed);
                                //return -1;
                                ret = -1;
                                goto out;
                        }
                } else {
                        gf_log(this->name, GF_LOG_DEBUG, "Reconfigure:"
                               " lookup-unhashed reconfigured auto ");
                        conf->search_unhashed = GF_DHT_LOOKUP_UNHASHED_AUTO;
                }
        }

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
                        &conf->rsync_regex, &conf->rsync_regex_valid);
        dht_init_regex (this, options, "extra-hash-regex",
                        &conf->extra_regex, &conf->extra_regex_valid);

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
                        if (gf_string2bytesize(pattern, &pattern_list->size)
                             == 0) {
                                pattern = "*";
                        }
                } else if (gf_string2bytesize (num, &pattern_list->size) != 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "invalid number format \"%s\"", num);
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
dht_init (xlator_t *this)
{
        dht_conf_t                      *conf           = NULL;
        char                            *temp_str       = NULL;
        int                              ret            = -1;
        int                              i              = 0;
        gf_defrag_info_t                *defrag         = NULL;
        int                              cmd            = 0;
        char                            *node_uuid      = NULL;


        GF_VALIDATE_OR_GOTO ("dht", this, err);

        if (!this->children) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Distribute needs more than one subvolume");
                return -1;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile");
        }

        conf = GF_CALLOC (1, sizeof (*conf), gf_dht_mt_dht_conf_t);
        if (!conf) {
                goto err;
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
                        gf_log (this->name, GF_LOG_ERROR, "node-uuid not "
                                "specified");
                        goto err;
                }

                if (uuid_parse (node_uuid, defrag->node_uuid)) {
                        gf_log (this->name, GF_LOG_ERROR, "Cannot parse "
                                "glusterd node uuid");
                        goto err;
                }

                defrag->cmd = cmd;

                defrag->stats = _gf_false;
        }

        conf->search_unhashed = GF_DHT_LOOKUP_UNHASHED_ON;
        if (dict_get_str (this->options, "lookup-unhashed", &temp_str) == 0) {
                /* If option is not "auto", other options _should_ be boolean */
                if (strcasecmp (temp_str, "auto"))
                        gf_string2boolean (temp_str, &conf->search_unhashed);
                else
                        conf->search_unhashed = GF_DHT_LOOKUP_UNHASHED_AUTO;
        }

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

        if (defrag) {
                GF_OPTION_INIT ("rebalance-stats", defrag->stats, bool, err);
                if (dict_get_str (this->options, "rebalance-filter", &temp_str)
                    == 0) {
                        if (gf_defrag_pattern_list_fill (this, defrag, temp_str)
                            == -1) {
                                gf_log (this->name, GF_LOG_ERROR, "Cannot parse"
                                        " rebalance-filter (%s)", temp_str);
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

        if (dict_get_str (this->options, "decommissioned-bricks", &temp_str) == 0) {
                ret = dht_parse_decommissioned_bricks (this, conf, temp_str);
                if (ret == -1)
                        goto err;
        }

        dht_init_regex (this, this->options, "rsync-hash-regex",
                        &conf->rsync_regex, &conf->rsync_regex_valid);
        dht_init_regex (this, this->options, "extra-hash-regex",
                        &conf->extra_regex, &conf->extra_regex_valid);

        ret = dht_layouts_init (this, conf);
        if (ret == -1) {
                goto err;
        }

        LOCK_INIT (&conf->subvolume_lock);
        LOCK_INIT (&conf->layout_lock);

        conf->gen = 1;

        this->local_pool = mem_pool_new (dht_local_t, 512);
        if (!this->local_pool) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create local_t's memory pool");
                goto err;
        }

        GF_OPTION_INIT ("xattr-name", conf->xattr_name, str, err);
        gf_asprintf (&conf->link_xattr_name, "%s."DHT_LINKFILE_STR,
                     conf->xattr_name);
        gf_asprintf (&conf->wild_xattr_name, "%s*", conf->xattr_name);
        if (!conf->link_xattr_name || !conf->wild_xattr_name) {
                goto err;
        }

        this->private = conf;

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

        /* NUFA option */
        { .key  = {"local-volume-name"},
          .type = GF_OPTION_TYPE_XLATOR
        },

        /* switch option */
        { .key  = {"pattern.switch.case"},
          .type = GF_OPTION_TYPE_ANY
        },

        { .key  = {NULL} },
};
