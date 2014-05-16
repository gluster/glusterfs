/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif
#include "afr-common.c"

struct volume_options options[];

int32_t
notify (xlator_t *this, int32_t event,
        void *data, ...)
{
        int ret = -1;
        va_list         ap;
        void *data2 = NULL;

        va_start (ap, data);
        data2 = va_arg (ap, dict_t*);
        va_end (ap);
        ret = afr_notify (this, event, data, data2);

        return ret;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_afr_mt_end + 1);

        if (ret != 0) {
                gf_log(this->name, GF_LOG_ERROR, "Memory accounting init"
                       "failed");
                return ret;
        }

        return ret;
}


int
xlator_subvolume_index (xlator_t *this, xlator_t *subvol)
{
        int index = -1;
        int i = 0;
        xlator_list_t *list = NULL;

        list = this->children;

        while (list) {
                if (subvol == list->xlator ||
                    strcmp (subvol->name, list->xlator->name) == 0) {
                        index = i;
                        break;
                }
                list = list->next;
                i++;
        }

        return index;
}

void
fix_quorum_options (xlator_t *this, afr_private_t *priv, char *qtype)
{
        if (priv->quorum_count && strcmp(qtype,"fixed")) {
                gf_msg (this->name,GF_LOG_WARNING, 0, AFR_MSG_QUORUM_OVERRIDE,
                       "quorum-type %s overriding quorum-count %u",
                       qtype, priv->quorum_count);
        }
        if (!strcmp(qtype,"none")) {
                priv->quorum_count = 0;
        }
        else if (!strcmp(qtype,"auto")) {
                priv->quorum_count = AFR_QUORUM_AUTO;
        }
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        afr_private_t *priv        = NULL;
        xlator_t      *read_subvol = NULL;
        int            read_subvol_index = -1;
        int            ret         = -1;
        int            index       = -1;
        char          *qtype       = NULL;

        priv = this->private;

	GF_OPTION_RECONF ("afr-dirty-xattr",
			  priv->afr_dirty, options, str,
			  out);

	GF_OPTION_RECONF ("metadata-splitbrain-forced-heal",
			  priv->metadata_splitbrain_forced_heal, options, bool,
			  out);

        GF_OPTION_RECONF ("background-self-heal-count",
                          priv->background_self_heal_count, options, uint32,
                          out);

        GF_OPTION_RECONF ("metadata-self-heal",
                          priv->metadata_self_heal, options, bool, out);

        GF_OPTION_RECONF ("data-self-heal", priv->data_self_heal, options, str,
                          out);

        GF_OPTION_RECONF ("entry-self-heal", priv->entry_self_heal, options,
                          bool, out);

        GF_OPTION_RECONF ("data-self-heal-window-size",
                          priv->data_self_heal_window_size, options,
                          uint32, out);

        GF_OPTION_RECONF ("data-change-log", priv->data_change_log, options,
                          bool, out);

        GF_OPTION_RECONF ("metadata-change-log",
                          priv->metadata_change_log, options, bool, out);

        GF_OPTION_RECONF ("entry-change-log", priv->entry_change_log, options,
                          bool, out);

        GF_OPTION_RECONF ("data-self-heal-algorithm",
                          priv->data_self_heal_algorithm, options, str, out);

        GF_OPTION_RECONF ("read-subvolume", read_subvol, options, xlator, out);

        GF_OPTION_RECONF ("read-hash-mode", priv->hash_mode,
                          options, uint32, out);

        if (read_subvol) {
                index = xlator_subvolume_index (this, read_subvol);
                if (index == -1) {
                        gf_log (this->name, GF_LOG_ERROR, "%s not a subvolume",
                                read_subvol->name);
                        goto out;
                }
                priv->read_child = index;
        }

        GF_OPTION_RECONF ("read-subvolume-index",read_subvol_index, options,int32,out);

        if (read_subvol_index >-1) {
                index=read_subvol_index;
                if (index >= priv->child_count) {
                        gf_log (this->name, GF_LOG_ERROR, "%d not a subvolume-index",
                                index);
                        goto out;
                }
                priv->read_child = index;
        }

        GF_OPTION_RECONF ("pre-op-compat", priv->pre_op_compat, options, bool, out);

        GF_OPTION_RECONF ("eager-lock", priv->eager_lock, options, bool, out);
        GF_OPTION_RECONF ("quorum-type", qtype, options, str, out);
        GF_OPTION_RECONF ("quorum-count", priv->quorum_count, options,
                          uint32, out);
        fix_quorum_options(this,priv,qtype);
        if (priv->quorum_count && !afr_has_quorum (priv->child_up, this))
                gf_msg (this->name, GF_LOG_WARNING, 0, AFR_MSG_QUORUM_FAIL,
                        "Client-quorum is not met");


	GF_OPTION_RECONF ("post-op-delay-secs", priv->post_op_delay_secs, options,
			  uint32, out);

        GF_OPTION_RECONF (AFR_SH_READDIR_SIZE_KEY, priv->sh_readdir_size,
                          options, size_uint64, out);
        /* Reset this so we re-discover in case the topology changed.  */
        GF_OPTION_RECONF ("ensure-durability", priv->ensure_durability, options,
                          bool, out);

	GF_OPTION_RECONF ("self-heal-daemon", priv->shd.enabled, options,
			  bool, out);

	GF_OPTION_RECONF ("iam-self-heal-daemon", priv->shd.iamshd, options,
			  bool, out);

        priv->did_discovery = _gf_false;

        ret = 0;
out:
        return ret;

}


static const char *favorite_child_warning_str = "You have specified subvolume '%s' "
        "as the 'favorite child'. This means that if a discrepancy in the content "
        "or attributes (ownership, permission, etc.) of a file is detected among "
        "the subvolumes, the file on '%s' will be considered the definitive "
        "version and its contents will OVERWRITE the contents of the file on other "
        "subvolumes. All versions of the file except that on '%s' "
        "WILL BE LOST.";


int32_t
init (xlator_t *this)
{
        afr_private_t *priv        = NULL;
        int            child_count = 0;
        xlator_list_t *trav        = NULL;
        int            i           = 0;
        int            ret         = -1;
        GF_UNUSED int  op_errno    = 0;
        xlator_t      *read_subvol = NULL;
        int            read_subvol_index = -1;
        xlator_t      *fav_child   = NULL;
        char          *qtype       = NULL;

        if (!this->children) {
                gf_log (this->name, GF_LOG_ERROR,
                        "replicate translator needs more than one "
                        "subvolume defined.");
                return -1;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Volume is dangling.");
        }

	this->private = GF_CALLOC (1, sizeof (afr_private_t),
                                   gf_afr_mt_afr_private_t);
        if (!this->private)
                goto out;

        priv = this->private;
        LOCK_INIT (&priv->lock);

        child_count = xlator_subvolume_count (this);

        priv->child_count = child_count;

        priv->read_child = -1;

	GF_OPTION_INIT ("afr-dirty-xattr", priv->afr_dirty, str, out);

	GF_OPTION_INIT ("metadata-splitbrain-forced-heal",
			priv->metadata_splitbrain_forced_heal, bool, out);

        GF_OPTION_INIT ("read-subvolume", read_subvol, xlator, out);
        if (read_subvol) {
                priv->read_child = xlator_subvolume_index (this, read_subvol);
                if (priv->read_child == -1) {
                        gf_log (this->name, GF_LOG_ERROR, "%s not a subvolume",
                                read_subvol->name);
                        goto out;
                }
        }
        GF_OPTION_INIT ("read-subvolume-index",read_subvol_index,int32,out);
        if (read_subvol_index > -1) {
                if (read_subvol_index >= priv->child_count) {
                        gf_log (this->name, GF_LOG_ERROR, "%d not a subvolume-index",
                                read_subvol_index);
                        goto out;
                }
                priv->read_child = read_subvol_index;
        }
        GF_OPTION_INIT ("choose-local", priv->choose_local, bool, out);

        GF_OPTION_INIT ("read-hash-mode", priv->hash_mode, uint32, out);

        priv->favorite_child = -1;
        GF_OPTION_INIT ("favorite-child", fav_child, xlator, out);
        if (fav_child) {
                priv->favorite_child = xlator_subvolume_index (this, fav_child);
                if (priv->favorite_child == -1) {
                        gf_log (this->name, GF_LOG_ERROR, "%s not a subvolume",
                                fav_child->name);
                        goto out;
                }
                gf_log (this->name, GF_LOG_WARNING,
                        favorite_child_warning_str, fav_child->name,
                        fav_child->name, fav_child->name);
        }


        GF_OPTION_INIT ("background-self-heal-count",
                        priv->background_self_heal_count, uint32, out);

        GF_OPTION_INIT ("data-self-heal", priv->data_self_heal, str, out);

        GF_OPTION_INIT ("data-self-heal-algorithm",
                        priv->data_self_heal_algorithm, str, out);

        GF_OPTION_INIT ("data-self-heal-window-size",
                        priv->data_self_heal_window_size, uint32, out);

        GF_OPTION_INIT ("metadata-self-heal", priv->metadata_self_heal, bool,
                        out);

        GF_OPTION_INIT ("entry-self-heal", priv->entry_self_heal, bool, out);

        GF_OPTION_INIT ("data-change-log", priv->data_change_log, bool, out);

        GF_OPTION_INIT ("metadata-change-log", priv->metadata_change_log, bool,
                        out);

        GF_OPTION_INIT ("entry-change-log", priv->entry_change_log, bool, out);

        GF_OPTION_INIT ("optimistic-change-log", priv->optimistic_change_log,
                        bool, out);

        GF_OPTION_INIT ("inodelk-trace", priv->inodelk_trace, bool, out);

        GF_OPTION_INIT ("entrylk-trace", priv->entrylk_trace, bool, out);

        GF_OPTION_INIT ("pre-op-compat", priv->pre_op_compat, bool, out);

        GF_OPTION_INIT ("eager-lock", priv->eager_lock, bool, out);
        GF_OPTION_INIT ("quorum-type", qtype, str, out);
        GF_OPTION_INIT ("quorum-count", priv->quorum_count, uint32, out);
        GF_OPTION_INIT (AFR_SH_READDIR_SIZE_KEY, priv->sh_readdir_size, size_uint64,
                        out);
        fix_quorum_options(this,priv,qtype);

	GF_OPTION_INIT ("post-op-delay-secs", priv->post_op_delay_secs, uint32, out);
        GF_OPTION_INIT ("ensure-durability", priv->ensure_durability, bool,
                        out);

	GF_OPTION_INIT ("self-heal-daemon", priv->shd.enabled, bool, out);

	GF_OPTION_INIT ("iam-self-heal-daemon", priv->shd.iamshd, bool, out);

        priv->wait_count = 1;

        priv->child_up = GF_CALLOC (sizeof (unsigned char), child_count,
                                    gf_afr_mt_char);
        if (!priv->child_up) {
                ret = -ENOMEM;
                goto out;
        }

        for (i = 0; i < child_count; i++)
                priv->child_up[i] = -1; /* start with unknown state.
                                           this initialization needed
                                           for afr_notify() to work
                                           reliably
                                        */

        priv->children = GF_CALLOC (sizeof (xlator_t *), child_count,
                                    gf_afr_mt_xlator_t);
        if (!priv->children) {
                ret = -ENOMEM;
                goto out;
        }

        priv->pending_key = GF_CALLOC (sizeof (*priv->pending_key),
                                       child_count,
                                       gf_afr_mt_char);
        if (!priv->pending_key) {
                ret = -ENOMEM;
                goto out;
        }

        trav = this->children;
        i = 0;
        while (i < child_count) {
                priv->children[i] = trav->xlator;

                ret = gf_asprintf (&priv->pending_key[i], "%s.%s",
                                   AFR_XATTR_PREFIX,
                                   trav->xlator->name);
                if (-1 == ret) {
                        ret = -ENOMEM;
                        goto out;
                }

                trav = trav->next;
                i++;
        }

        ret = gf_asprintf (&priv->sh_domain, AFR_SH_DATA_DOMAIN_FMT,
                           this->name);
        if (-1 == ret) {
                ret = -ENOMEM;
                goto out;
        }

        priv->last_event = GF_CALLOC (child_count, sizeof (*priv->last_event),
                                      gf_afr_mt_int32_t);
        if (!priv->last_event) {
                ret = -ENOMEM;
                goto out;
        }

	ret = afr_selfheal_daemon_init (this);
	if (ret) {
		ret = -ENOMEM;
		goto out;
	}

        /* keep more local here as we may need them for self-heal etc */
        this->local_pool = mem_pool_new (afr_local_t, 512);
        if (!this->local_pool) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create local_t's memory pool");
                goto out;
        }

        priv->root_inode = NULL;

        ret = 0;
out:
        return ret;
}


int
fini (xlator_t *this)
{
        afr_private_t *priv = NULL;

        priv = this->private;
        this->private = NULL;
        afr_priv_destroy (priv);
        //if (this->itable);//I dont see any destroy func

        return 0;
}


struct xlator_fops fops = {
        .lookup      = afr_lookup,
        .open        = afr_open,
        .lk          = afr_lk,
        .flush       = afr_flush,
        .statfs      = afr_statfs,
        .fsync       = afr_fsync,
        .fsyncdir    = afr_fsyncdir,
        .xattrop     = afr_xattrop,
        .fxattrop    = afr_fxattrop,
        .inodelk     = afr_inodelk,
        .finodelk    = afr_finodelk,
        .entrylk     = afr_entrylk,
        .fentrylk    = afr_fentrylk,

        /* inode read */
        .access      = afr_access,
        .stat        = afr_stat,
        .fstat       = afr_fstat,
        .readlink    = afr_readlink,
        .getxattr    = afr_getxattr,
        .fgetxattr   = afr_fgetxattr,
        .readv       = afr_readv,

        /* inode write */
        .writev      = afr_writev,
        .truncate    = afr_truncate,
        .ftruncate   = afr_ftruncate,
        .setxattr    = afr_setxattr,
        .fsetxattr   = afr_fsetxattr,
        .setattr     = afr_setattr,
        .fsetattr    = afr_fsetattr,
        .removexattr = afr_removexattr,
        .fremovexattr = afr_fremovexattr,
        .fallocate   = afr_fallocate,
        .discard     = afr_discard,
        .zerofill    = afr_zerofill,

        /* dir read */
        .opendir     = afr_opendir,
        .readdir     = afr_readdir,
        .readdirp    = afr_readdirp,

        /* dir write */
        .create      = afr_create,
        .mknod       = afr_mknod,
        .mkdir       = afr_mkdir,
        .unlink      = afr_unlink,
        .rmdir       = afr_rmdir,
        .link        = afr_link,
        .symlink     = afr_symlink,
        .rename      = afr_rename,
};


struct xlator_dumpops dumpops = {
        .priv       = afr_priv_dump,
};


struct xlator_cbks cbks = {
        .release     = afr_release,
        .releasedir  = afr_releasedir,
        .forget      = afr_forget,
};


struct volume_options options[] = {
        { .key  = {"read-subvolume" },
          .type = GF_OPTION_TYPE_XLATOR,
          .description = "inode-read fops happen only on one of the bricks in "
                         "replicate. Afr will prefer the one specified using "
                         "this option if it is not stale. Option value must be "
                         "one of the xlator names of the children. "
                         "Ex: <volname>-client-0 till "
                         "<volname>-client-<number-of-bricks - 1>"
        },
        { .key  = {"read-subvolume-index" },
          .type = GF_OPTION_TYPE_INT,
          .default_value = "-1",
          .description = "inode-read fops happen only on one of the bricks in "
                         "replicate. AFR will prefer the one specified using "
                         "this option if it is not stale. allowed options"
                         " include -1 till replica-count - 1"
        },
        { .key = {"read-hash-mode" },
          .type = GF_OPTION_TYPE_INT,
          .min = 0,
          .max = 2,
          .default_value = "1",
          .description = "inode-read fops happen only on one of the bricks in "
                         "replicate. AFR will prefer the one computed using "
                         "the method specified using this option"
                         "0 = first up server, "
                         "1 = hash by GFID of file (all clients use "
                                                    "same subvolume), "
                         "2 = hash by GFID of file and client PID",
        },
        { .key  = {"choose-local" },
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "true",
          .description = "Choose a local subvolume (i.e. Brick) to read from"
	                 " if read-subvolume is not explicitly set.",
        },
        { .key  = {"favorite-child"},
          .type = GF_OPTION_TYPE_XLATOR,
          .description = "If a split-brain happens choose subvol/brick set by "
                         "this option as source."
        },
        { .key  = {"background-self-heal-count"},
          .type = GF_OPTION_TYPE_INT,
          .min  = 0,
          .default_value = "16",
          .validate = GF_OPT_VALIDATE_MIN,
          .description = "This specifies the number of self-heals that can be "
                         " performed in background without blocking the fop"
        },
        { .key  = {"data-self-heal"},
          .type = GF_OPTION_TYPE_STR,
          .value = {"1", "on", "yes", "true", "enable",
                    "0", "off", "no", "false", "disable",
                    "open"},
          .default_value = "on",
          .description   = "Using this option we can enable/disable data "
                           "self-heal on the file. \"open\" means data "
                           "self-heal action will only be triggered by file "
                           "open operations."
        },
        { .key  = {"data-self-heal-algorithm"},
          .type = GF_OPTION_TYPE_STR,
          .description   = "Select between \"full\", \"diff\". The "
                           "\"full\" algorithm copies the entire file from "
                           "source to sink. The \"diff\" algorithm copies to "
                           "sink only those blocks whose checksums don't match "
                           "with those of source. If no option is configured "
                           "the option is chosen dynamically as follows: "
                           "If the file does not exist on one of the sinks "
                           "or empty file exists or if the source file size is "
                           "about the same as page size the entire file will "
                           "be read and written i.e \"full\" algo, "
                           "otherwise \"diff\" algo is chosen.",
          .value = { "diff", "full"}
        },
        { .key  = {"data-self-heal-window-size"},
          .type = GF_OPTION_TYPE_INT,
          .min  = 1,
          .max  = 1024,
          .default_value = "1",
          .description = "Maximum number blocks per file for which self-heal "
                         "process would be applied simultaneously."
        },
        { .key  = {"metadata-self-heal"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "Using this option we can enable/disable metadata "
                         "i.e. Permissions, ownerships, xattrs self-heal on "
                         "the file/directory."
        },
        { .key  = {"entry-self-heal"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "Using this option we can enable/disable entry "
                         "self-heal on the directory."
        },
        { .key  = {"data-change-log"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "Data fops like write/truncate will not perform "
                         "pre/post fop changelog operations in afr transaction "
                         "if this option is disabled"
        },
        { .key  = {"metadata-change-log"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "Metadata fops like setattr/setxattr will not perform "
                         "pre/post fop changelog operations in afr transaction "
                         "if this option is disabled"
        },
        { .key  = {"entry-change-log"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "Entry fops like create/unlink will not perform "
                         "pre/post fop changelog operations in afr transaction "
                         "if this option is disabled"
        },
        { .key  = {"optimistic-change-log"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "Entry/Metadata fops will not perform "
                         "pre fop changelog operations in afr transaction "
                         "if this option is enabled."
        },
        { .key = {"inodelk-trace"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "Enabling this option logs inode lock/unlocks"
        },
        { .key = {"entrylk-trace"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "Enabling this option logs entry lock/unlocks"
        },
	{ .key = {"pre-op-compat"},
	  .type = GF_OPTION_TYPE_BOOL,
	  .default_value = "on",
	  .description = "Use separate pre-op xattrop() FOP rather than "
	                 "overloading xdata of the OP"
	},
        { .key = {"eager-lock"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "Lock phase of a transaction has two sub-phases. "
                         "First is an attempt to acquire locks in parallel by "
                         "broadcasting non-blocking lock requests. If lock "
                         "acquisition fails on any server, then the held locks "
                         "are unlocked and revert to a blocking locked mode "
                         "sequentially on one server after another.  If this "
                         "option is enabled the initial broadcasting lock "
                         "request attempt to acquire lock on the entire file. "
                         "If this fails, we revert back to the sequential "
                         "\"regional\" blocking lock as before. In the case "
                         "where such an \"eager\" lock is granted in the "
                         "non-blocking phase, it gives rise to an opportunity "
                         "for optimization. i.e, if the next write transaction "
                         "on the same FD arrives before the unlock phase of "
                         "the first transaction, it \"takes over\" the full "
                         "file lock. Similarly if yet another data transaction "
                         "arrives before the unlock phase of the \"optimized\" "
                         "transaction, that in turn \"takes over\" the lock as "
                         "well. The actual unlock now happens at the end of "
                         "the last \"optimized\" transaction."

        },
        { .key = {"self-heal-daemon"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "This option applies to only self-heal-daemon. "
                         "Index directory crawl and automatic healing of files "
                         "will not be performed if this option is turned off."
        },
        { .key = {"iam-self-heal-daemon"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "This option differentiates if the replicate "
                         "translator is running as part of self-heal-daemon "
                         "or not."
        },
        { .key = {"quorum-type"},
          .type = GF_OPTION_TYPE_STR,
          .value = { "none", "auto", "fixed"},
          .default_value = "none",
          .description = "If value is \"fixed\" only allow writes if "
                         "quorum-count bricks are present.  If value is "
                         "\"auto\" only allow writes if more than half of "
                         "bricks, or exactly half including the first, are "
                         "present.",
        },
        { .key = {"quorum-count"},
          .type = GF_OPTION_TYPE_INT,
          .min = 1,
          .max = INT_MAX,
          .default_value = 0,
          .description = "If quorum-type is \"fixed\" only allow writes if "
                         "this many bricks or present.  Other quorum types "
                         "will OVERWRITE this value.",
        },
        { .key  = {"node-uuid"},
          .type = GF_OPTION_TYPE_STR,
          .description = "Local glusterd uuid string, used in starting "
                         "self-heal-daemon so that it can crawl only on "
                         "local index directories.",
        },
        { .key  = {"post-op-delay-secs"},
          .type = GF_OPTION_TYPE_INT,
          .min  = 0,
          .max  = INT_MAX,
          .default_value = "1",
          .description = "Time interval induced artificially before "
	                 "post-operation phase of the transaction to "
                         "enhance overlap of adjacent write operations.",
        },
        { .key = {AFR_SH_READDIR_SIZE_KEY},
          .type = GF_OPTION_TYPE_SIZET,
          .description = "readdirp size for performing entry self-heal",
          .min = 1024,
          .max = 131072,
          .default_value = "1KB",
        },
        { .key = {"ensure-durability"},
          .type = GF_OPTION_TYPE_BOOL,
          .description = "Afr performs fsyncs for transactions if this "
                         "option is on to make sure the changelogs/data is "
                         "written to the disk",
          .default_value = "on",
        },
	{ .key = {"afr-dirty-xattr"},
	  .type = GF_OPTION_TYPE_STR,
	  .default_value = AFR_DIRTY_DEFAULT,
	},
	{ .key = {"metadata-splitbrain-forced-heal"},
	  .type = GF_OPTION_TYPE_BOOL,
	  .default_value = "off",
	},
        { .key  = {NULL} },
};
