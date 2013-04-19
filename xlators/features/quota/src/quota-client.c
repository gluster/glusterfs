/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <fnmatch.h>

#include "quota.h"
#include "common-utils.h"
#include "defaults.h"
#include "syncop.h"

gf_boolean_t resend_size = _gf_true;


/* Returns itable->root, also creates itable if not present */
inode_t *
qc_build_root_inode (xlator_t *this, qc_vols_conf_t *this_vol)
{
        if (!this_vol->itable) {
                this_vol->itable = inode_table_new (0, this);
                if (!this_vol->itable)
                        return NULL;
        }

        return inode_ref (this_vol->itable->root);
}

void
qc_build_root_loc (inode_t *inode, loc_t *loc)
{
        loc->path = gf_strdup ("/");
        loc->inode = inode;
        memset (loc->gfid, 0, 16);
        loc->gfid[15] = 1;
}


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_quota_mt_end + 1);

        if (0 != ret) {
                gf_log (this->name, GF_LOG_WARNING, "Memory accounting "
                        "init failed");
                return ret;
        }

        return ret;
}

/**
 * Takes the limit string, parse it and push all the node into above-soft list
 *
 * Format for limit string
 * <limit-string> = <limit-on-single-dir>[,<limit-on-single-dir>]*
 * <limit-on-single-dir> = <absolute-path-from-the-volume-root>:<soft-limit>:<hard-limit>
 */
int
qc_parse_limits (quota_priv_t *priv, xlator_t *this, char *limit_str,
                    struct list_head *old_list, qc_vols_conf_t *this_vol)
{
        int32_t       ret       = -1;
        char         *str_val   = NULL;
        char         *path      = NULL, *saveptr = NULL;
        uint64_t      value     = 0;
        limits_t     *quota_lim = NULL, *old = NULL;
        char         *last_colon= NULL;
        char         *str       = NULL;

        str = gf_strdup (limit_str);

        if (str) {
                path = strtok_r (str, ",", &saveptr);

                while (path) {
                        QUOTA_ALLOC_OR_GOTO (quota_lim, limits_t, err);

                        last_colon = strrchr (path, ':');
                        *last_colon = '\0';
                        str_val = last_colon + 1;

                        ret = gf_string2bytesize (str_val, &value);
                        if (0 != ret)
                                goto err;

                        quota_lim->hard_lim = value;

                        last_colon = strrchr (path, ':');
                        *last_colon = '\0';
                        str_val = last_colon + 1;

                        ret = gf_string2bytesize (str_val, &value);
                        if (0 != ret)
                                goto err;

                        quota_lim->soft_lim = value;

                        quota_lim->path = gf_strdup (path);

                        quota_lim->prev_size = quota_lim->hard_lim;

                        gf_log (this->name, GF_LOG_INFO, "%s:%"PRId64":%"PRId64,
                                quota_lim->path, quota_lim->hard_lim,
                                quota_lim->soft_lim);

                        if (NULL != old_list) {
                                list_for_each_entry (old, old_list,
                                                     limit_list) {
                                        if (0 ==
                                            strcmp (old->path, quota_lim->path)) {
                                                uuid_copy (quota_lim->gfid,
                                                           old->gfid);
                                                break;
                                        }
                                }
                        }

                        LOCK (&priv->lock);
                        {
                                list_add_tail (&quota_lim->limit_list,
                                               &this_vol->above_soft.limit_head);
                        }
                        UNLOCK (&priv->lock);

                        path = strtok_r (NULL, ",", &saveptr);
                }
        } else {
                gf_log (this->name, GF_LOG_INFO,
                        "no \"limit-set\" option provided");
        }

        ret = 0;
err:
        GF_FREE (str);
        return ret;
}


xlator_t *
qc_get_subvol (xlator_t *this, qc_vols_conf_t *this_vol)
{
        xlator_list_t   *subvol        = NULL;

        for (subvol = this->children; subvol; subvol = subvol->next)
                if (0 == strcmp (subvol->xlator->name, this_vol->name))
                        return subvol->xlator;

        return NULL;
}


int
qc_build_loc (xlator_t *this, qc_vols_conf_t *this_vol, char *cur_path,
                 inode_t *par_inode, loc_t *loc)
{
        int     ret     = 0;

        loc->path = gf_strdup (cur_path);
        if (!loc->path) {
                ret = -1;
                goto out;
        }
        loc->name = strrchr (loc->path, '/');
        loc->name ++;

        loc->inode = inode_new (this_vol->itable);
        if (!loc->inode) {
                gf_log (this->name, GF_LOG_WARNING, "Couldn't create inode");
                ret = -1;
                goto out;
        }
        loc->parent = par_inode;
        uuid_copy (loc->pargfid, par_inode->gfid);
out:
        return ret;
}

int
qc_get_child (char *path, char **save_ptr, char *path_res)
{
        if (!path || !save_ptr || !*save_ptr || !**save_ptr)
                return -1;

        char    *base_name = NULL;

        base_name = strtok_r (path, "/", save_ptr);
        if (!base_name)
                return -1;

        strcat (path_res, "/");
        strcat (path_res, base_name);

        return 0;
}
int
qc_confirm_path_exists (xlator_t *this, xlator_t *subvol,
                           qc_vols_conf_t *this_vol, limits_t *entry,
                           loc_t *root_loc, loc_t *entry_loc)
{
        int     ret     = 0;
        loc_t   par_loc = {0,};
        char    *save_ptr       = NULL;
        char    *cur_path       = NULL;
        loc_t    cur_loc         = {0,};
        struct iatt     buf     = {0,};
        struct iatt     par_buf = {0,};
        char            *path   = NULL;

        if (0 == strcmp (entry->path, "/")) {
                loc_copy (entry_loc, root_loc);
                ret = syncop_lookup (subvol, entry_loc, NULL, &buf, NULL,
                                     &par_buf);
                if (-1 == ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "Lookup failed on root");

                return ret;
        }

        cur_path = GF_CALLOC (sizeof (char), strlen (entry->path) + 1,
                              gf_quota_mt_char);
        if (!cur_path) {
                ret = -1;
                goto out;
        }

        path = gf_strdup (entry->path);
        save_ptr = path;
        par_loc = *root_loc;
        while (1) {
                ret = qc_get_child (path, &save_ptr, cur_path);
                if (-1 == ret)
                        break;

                qc_build_loc (this, this_vol, cur_path, par_loc.inode,
                                 &cur_loc);

                ret = syncop_lookup (subvol, &cur_loc, NULL, &buf, NULL,
                                     &par_buf);
                if (-1 == ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Lookup failed on %s:%d", cur_loc.path, errno);
                }
                par_loc = cur_loc;
        }
out:
        GF_FREE (path);
        loc_copy (entry_loc, &cur_loc);
        return ret;
}


int
qc_getsetxattr (xlator_t *this, struct limits_level *list,
                             loc_t *root_loc, xlator_t *subvol)
{
        limits_t        *entry          = NULL;
        limits_t        *next           = NULL;
        int32_t          ret            = -1;
        loc_t            entry_loc      = {0,};
        dict_t          *dict           = NULL;
        int32_t          op_errno       = -1;
        int64_t          cur_size       = 0;
        int64_t          prev_size      = 0;
        quota_priv_t    *priv           = NULL;
        int64_t         *size           = NULL;

        priv = this->private;

        list_for_each_entry_safe (entry, next, &list->limit_head, limit_list) {
                loc_wipe (&entry_loc);

                ret = qc_confirm_path_exists (this, subvol,
                                              GET_THIS_VOL (list),
                                              entry, root_loc, &entry_loc);
                if (ret)
                        //handle the errors
                        continue;

                ret = syncop_getxattr (subvol, &entry_loc, &dict,
                                       QUOTA_SIZE_KEY);
                if (-1 == ret) {
                        op_errno = errno;
                        if (ENOENT == op_errno)
                                goto free_and_continue;
                        /* Handle the other errors */
                }

                ret = dict_get_bin (dict, QUOTA_SIZE_KEY, (void **)&size);
                if (0 != ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Couldn't get size"
                                " from the dict");
                        goto free_and_continue;
                }
                prev_size = entry->prev_size;
                cur_size = ntoh64 (*size);

                if (!resend_size && prev_size == cur_size)
                        goto free_and_continue;

                LOCK (&priv->lock);
                {
                        entry->prev_size = cur_size;
                }
                UNLOCK (&priv->lock);

                QUOTA_ALLOC_OR_GOTO (size, int64_t, free_and_continue);
                *size = hton64 (cur_size);

                dict_del (dict, QUOTA_SIZE_KEY);
                ret = dict_set_bin (dict, QUOTA_UPDATE_USAGE_KEY, size,
                                    sizeof (int64_t));
                if (-1 == ret) {
                        // Handle the error
                }

                ret = syncop_setxattr (subvol, &entry_loc, dict, 0);
                if (-1 == ret) {
                        op_errno = errno;
                        /* handle the errors */
                }


                /* Move the node to the corresponding list, based on the usage
                 * shift */
                LOCK (&priv->lock);
                {
                        /* usage > soft_limit? */
                        if (prev_size < entry->soft_lim &&
                                        cur_size >= entry->soft_lim) {
                                list_move (&entry->limit_list,
                                           &(GET_THIS_VOL(list)->above_soft.limit_head));
                                gf_log (this->name, GF_LOG_INFO, "%s usage "
                                        "crossed soft limit.", entry_loc.path);
                        }
                        /* usage < soft_limit? */
                        else if (prev_size >= entry->soft_lim &&
                                        cur_size < entry->soft_lim)
                                list_move (&entry->limit_list,
                                           &(GET_THIS_VOL(list)->below_soft.limit_head));
                }
                UNLOCK (&priv->lock);

free_and_continue:
                ret = dict_reset (dict);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING, "dictionary reset "
                                "failed");
        }
        return ret;
}

int
qc_trigger_periodically (void *args)
{
        int                      ret            = -1;
        struct limits_level     *list           = NULL;
        xlator_t                *this           = NULL;
        xlator_t                *subvol         = NULL;
        inode_t                 *root_inode     = NULL;
        loc_t                    root_loc       = {0,};

        this = THIS;
        list = args;

        subvol = qc_get_subvol (this, GET_THIS_VOL (list));
        if (!subvol) {
                gf_log (this->name, GF_LOG_ERROR, "No subvol found");
                return -1;
        }

        root_inode = qc_build_root_inode (this, GET_THIS_VOL (list));
        if (!root_inode) {
                gf_log (this->name, GF_LOG_ERROR, "New itable create failed");
                return -1;
        }

        qc_build_root_loc (root_inode, &root_loc);

        while (GF_UNIVERSAL_ANSWER) {
                if (!list_empty (&list->limit_head)) {
                        ret = qc_getsetxattr (this, list, &root_loc, subvol);
                        if (-1 == ret)
                                gf_log ("quota-client", GF_LOG_WARNING,
                                        "Couldn't update the usage, frequent "
                                        "log is lead to usage beyond limit");
                }

                resend_size = _gf_false;

                sleep ((unsigned int) (list->time_out));
        }

        loc_wipe (&root_loc);
        return ret;
}

int
qc_trigger_periodically_try_again (int ret, call_frame_t *frame, void *args)
{
        gf_log ("quota-client", GF_LOG_ERROR, "Synctask stopped unexpectedly, "
                "trying to restart");

        ret = synctask_new (THIS->ctx->env,
                            qc_trigger_periodically,
                            qc_trigger_periodically_try_again,
                            frame, args);
        if (-1 == ret)
                gf_log ("quota-client", GF_LOG_ERROR, "Synctask creation "
                        "failed for %s",
                        (GET_THIS_VOL ((struct limits_level *)args))->name);

        return ret;
}


int
qc_start_threads (xlator_t *this)
{
        quota_priv_t            *priv           = NULL;
        int                      ret            = 0;
        qc_vols_conf_t          *this_vol       = NULL;
        int                      i              = 0;
        xlator_list_t           *subvols        = NULL;

        priv = this->private;

        for (i = 0, subvols = this->children;
             subvols;
             i++, subvols = subvols->next) {

                this_vol = priv->qc_vols_conf[i];

                if (list_empty (&this_vol->above_soft.limit_head)) {
                        gf_log (this->name, GF_LOG_DEBUG, "No limit is set on "
                                "volume %s", this_vol->name);
                        continue;
                }

                /* Create 2 threads for soft and hard limits */
                ret = synctask_new (this->ctx->env,
                                    qc_trigger_periodically,
                                    qc_trigger_periodically_try_again,
                                    NULL, (void *)&this_vol->below_soft);
                if (-1 == ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Synctask creation "
                                "failed for %s", this_vol->name);
                        goto err;
                }

                ret = synctask_new (this->ctx->env,
                                    qc_trigger_periodically,
                                    qc_trigger_periodically_try_again,
                                    NULL, (void *)&this_vol->above_soft);
                if (-1 == ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Synctask creation "
                                "failed for %s", this_vol->name);
                        goto err;
                }
        }
err:
        return ret;
}

int
qc_reconfigure (xlator_t *this, dict_t *options)
{
        int32_t           ret   = -1;
        quota_priv_t     *priv  = NULL;
        struct list_head  head  = {0, };
        int               i     = 0;
        xlator_list_t    *child_list = NULL;
        char             *limits        = NULL;
        qc_vols_conf_t   *this_vol      = NULL;
        char             *option_str    = NULL;
        limits_t         *limit         = NULL;
        limits_t         *next          = NULL;

        priv = this->private;

        INIT_LIST_HEAD (&head);

        LOCK (&priv->lock);
        {
                list_splice_init (&priv->limit_head, &head);
        }
        UNLOCK (&priv->lock);

        for (i=0, child_list = this->children;
             child_list;
             child_list = child_list->next, i++) {

                this_vol = priv->qc_vols_conf [i];

                gf_asprintf (&option_str, "%s.limit-set",
                             child_list->xlator->name);
                ret = dict_get_str (this->options, option_str, &limits);
                if (ret)
                        continue;

                ret = qc_parse_limits (priv, this, limits, &head,
                                       this_vol);
                if (-1 == ret) {
                        gf_log ("quota", GF_LOG_WARNING,
                                "quota reconfigure failed, "
                                "new changes will not take effect");
                        goto out;
                }

                list_for_each_entry_safe (limit, next, &head, limit_list) {
                        list_del_init (&limit->limit_list);
                        GF_FREE (limit);
                }

                GF_OPTION_RECONF ("*.hard-timeout",
                                  this_vol->above_soft.time_out, options,
                                  uint64, out);
                GF_OPTION_RECONF ("*.soft-timeout",
                                  this_vol->below_soft.time_out, options,
                                  uint64, out);
        }

        ret = qc_start_threads (this);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Couldn't start threads");
                goto out;
        }

        ret = 0;
out:
        return ret;
}


void
qc_fini (xlator_t *this)
{
        return;
}

int32_t
qc_init (xlator_t *this)
{
        int32_t       ret       = -1;
        quota_priv_t *priv      = NULL;
        int              i      = 0;
        char            *option_str     = NULL;
        xlator_list_t   *subvol         = NULL;
        xlator_list_t   *child_list     = NULL;
        char            *limits         = NULL;
        int              subvol_cnt     = 0;
        qc_vols_conf_t  *this_vol       = NULL;

        if (NULL == this->children) {
                gf_log (this->name, GF_LOG_ERROR,
                        "FATAL: quota (%s) not configured for min of 1 child",
                        this->name);
                ret = -1;
                goto err;
        }

        QUOTA_ALLOC_OR_GOTO (priv, quota_priv_t, err);

        LOCK_INIT (&priv->lock);

        this->private = priv;

        for (i = 0, child_list = this->children;
             child_list;
             i++, child_list = child_list->next);
        subvol_cnt = i;

        priv->qc_vols_conf = GF_CALLOC (sizeof (qc_vols_conf_t *),
                                        subvol_cnt, gf_quota_mt_qc_vols_conf_t);

        for (i = 0, subvol = this->children;
             subvol;
             subvol = subvol->next, i++) {
                priv->qc_vols_conf[i] = GF_CALLOC (sizeof (qc_vols_conf_t), 1,
                                                   gf_quota_mt_qc_vols_conf_t);
                INIT_LIST_HEAD (&priv->qc_vols_conf[i]->above_soft.limit_head);
                INIT_LIST_HEAD (&priv->qc_vols_conf[i]->below_soft.limit_head);
                //QUOTA_ALLOC_OR_GOTO (priv->qc_vols_conf [i],
                 //                    qc_vols_conf_t, err);
        }
        subvol_cnt = i;

        for (i=0, child_list = this->children;
             child_list;
             i++, child_list = child_list->next) {
                this_vol = priv->qc_vols_conf[i];

                this_vol->name = child_list->xlator->name;

                this_vol->below_soft.my_vol =
                this_vol->above_soft.my_vol = this_vol;

                ret = gf_asprintf (&option_str, "%s.limit-set",
                                   child_list->xlator->name);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "ENOMEM %s", child_list->xlator->name);
                        continue;
                }
                ret = dict_get_str (this->options, option_str, &limits);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "dict get failed, ENOMEM??");
                        continue;
                }

                ret = qc_parse_limits (priv, this, limits, NULL, this_vol);
                GF_FREE (option_str);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Couldn't parse limits for %s", this_vol->name);
                        continue;
                }

                gf_asprintf (&option_str, "%s.soft-timeout",
                             child_list->xlator->name);
                GF_OPTION_INIT (option_str,
                                this_vol->below_soft.time_out,
                                uint64, err);
                GF_FREE (option_str);

                gf_asprintf (&option_str, "%s.hard-timeout",
                             child_list->xlator->name);
                GF_OPTION_INIT (option_str,
                                this_vol->above_soft.time_out,
                                uint64, err);
                GF_FREE (option_str);
        }

        this->local_pool = mem_pool_new (quota_local_t, 64);
        if (!this->local_pool) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create local_t's memory pool");
                goto err;
        }

        ret = 0;
err:
        return ret;
}

int
qc_notify (xlator_t *this, int event, void *data, ...)
{
        xlator_list_t   *subvol                 = NULL;
        xlator_t        *subvol_rec             = NULL;
        quota_priv_t    *priv                   = NULL;
        int              i                      = 0;
        int              ret                    = 0;

        subvol_rec = data;
        priv = this->private;

        for (i=0, subvol = this->children; subvol; i++, subvol = subvol->next) {
                if (0 == strcmp (priv->qc_vols_conf[i]->name, subvol_rec->name))
                        break;
        }

        switch (event) {
        case GF_EVENT_CHILD_UP:
        {
                resend_size = _gf_true;

                ret = qc_start_threads (this);
                if (-1 == ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Couldn't start the threads for volumes");
                        goto out;
                }
                break;
        }
        case GF_EVENT_CHILD_DOWN:
        {
                gf_log (this->name, GF_LOG_ERROR, "vol %s down.",
                        priv->qc_vols_conf [i]->name);
                break;
        }
        default:
                default_notify (this, event, data);
        }


out:
        return ret;
}

class_methods_t class_methods = {
        .init           = qc_init,
        .fini           = qc_fini,
        .reconfigure    = qc_reconfigure,
        .notify         = qc_notify,
};

int32_t
qc_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, const char *name,
             dict_t *xdata)
{
        STACK_WIND (frame, default_getxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr, loc, name, xdata);
        return 0;
}

int32_t
qc_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
             int flags, dict_t *xdata)
{
        STACK_WIND (frame, default_setxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->setxattr, loc, dict,
                    flags, xdata);
        return 0;
}

int32_t
qc_forget (xlator_t *this, inode_t *inode)
{
        return 0;
}

struct xlator_fops fops = {
        .getxattr       = qc_getxattr,
        .setxattr       = qc_setxattr
};

struct xlator_cbks cbks = {
        .forget         = qc_forget
};

struct volume_options options[] = {
        {.key = {"*.limit-set"}},
        {.key = {"*.soft-timeout"},
         .type = GF_OPTION_TYPE_SIZET,
         .min = 0,
         .max = 60,
         .default_value = "10",
         .description = ""
        },
        {.key = {"*.hard-timeout"},
         .type = GF_OPTION_TYPE_SIZET,
         .min = 0,
         .max = 60,
         .default_value = "2",
         .description = ""
        },
        {.key = {NULL}}
};
