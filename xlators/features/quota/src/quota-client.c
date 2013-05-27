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
#include "libgen.h"

gf_boolean_t resend_size = _gf_true;


/* Returns itable->root, also creates itable if not present */
inode_t *
qd_build_root_inode (xlator_t *this, qd_vols_conf_t *this_vol)
{
        if (!this_vol->itable) {
                this_vol->itable = inode_table_new (0, this);
                if (!this_vol->itable)
                        return NULL;
        }

        return inode_ref (this_vol->itable->root);
}

void
qd_build_root_loc (inode_t *inode, loc_t *loc)
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
qd_parse_limits (quota_priv_t *priv, xlator_t *this, char *limit_str,
                    struct list_head *old_list, qd_vols_conf_t *this_vol)
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
qd_get_subvol (xlator_t *this, qd_vols_conf_t *this_vol)
{
        xlator_list_t   *subvol        = NULL;

        for (subvol = this->children; subvol; subvol = subvol->next)
                if (0 == strcmp (subvol->xlator->name, this_vol->name))
                        return subvol->xlator;

        return NULL;
}


int
qd_build_loc (loc_t *loc, inode_t *par, char *compnt, int *reval, uuid_t gfid)
{
        int     ret     = 0;

	loc->name = compnt;

	loc->parent = inode_ref (par);
	uuid_copy (loc->pargfid, par->gfid);

        loc->inode = inode_grep (par->table, par, compnt);

	if (loc->inode) {
		uuid_copy (loc->gfid, loc->inode->gfid);
		*reval = 1;
	} else {
		uuid_generate (gfid);
		loc->inode = inode_new (par->table);
	}

	if (!loc->inode)
                ret = -1;
        return ret;
}

/*
int
qd_get_child (char *path, char **save_ptr, char *path_res)
{
        if (!path || !save_ptr || !*save_ptr)
                return -1;

        if (0 == strcmp (path, "/")) {
                return 1;
        }

        char    *base_name = NULL;

        base_name = strtok_r (path, "/", save_ptr);
        if (!base_name)
                return -1;

        strcat (path_res, "/");
        strcat (path_res, base_name);

        return (!strcmp (path_res, path))? 1: 0;
}
*/

int
qd_loc_touchup (loc_t *loc)
{
	char *path = NULL;
	int   ret = -1;
	char *bn = NULL;

	if (loc->parent)
		ret = inode_path (loc->parent, loc->name, &path);
	else
		ret = inode_path (loc->inode, 0, &path);

	loc->path = path;

	if (ret < 0 || !path) {
		ret = -1;
		errno = ENOMEM;
		goto out;
	}

	bn = strrchr (path, '/');
	if (bn)
		bn++;
	loc->name = bn;
	ret = 0;
out:
	return ret;
}

inode_t *
qd_resolve_component (xlator_t *this,xlator_t *subvol, inode_t *par,
                      char *component, struct iatt *iatt, dict_t *xattr_req,
                      dict_t **dict_rsp, int force_lookup)
{
	loc_t        loc        = {0, };
	inode_t     *inode      = NULL;
	int          reval      = 0;
	int          ret        = -1;
	struct iatt  ciatt      = {0, };
	uuid_t       gfid;


	loc.name = component;

	loc.parent = inode_ref (par);
	uuid_copy (loc.pargfid, par->gfid);

        loc.inode = inode_grep (par->table, par, component);

	if (loc.inode) {
		uuid_copy (loc.gfid, loc.inode->gfid);
		reval = 1;

                if (!force_lookup) {
                        inode = inode_ref (loc.inode);
                        ciatt.ia_type = inode->ia_type;
                        goto found;
                }
	} else {
		uuid_generate (gfid);
		loc.inode = inode_new (par->table);
	}

	if (!loc.inode)
                goto out;

	ret = qd_loc_touchup (&loc);
	if (ret < 0) {
		ret = -1;
		goto out;
	}

	ret = syncop_lookup (subvol, &loc, xattr_req, &ciatt, dict_rsp, NULL);
	if (ret && reval) {
		inode_unref (loc.inode);
		loc.inode = inode_new (par->table);
		if (!loc.inode) {
                        errno = ENOMEM;
			goto out;
                }

                if (!xattr_req) {
                        xattr_req = dict_new ();
                        if (!xattr_req) {
                                errno = ENOMEM;
                                goto out;
                        }
                }

		uuid_generate (gfid);

                ret = dict_set_static_bin (xattr_req, "gfid-req", gfid, 16);
                if (ret) {
                        errno = ENOMEM;
                        goto out;
                }

		ret = syncop_lookup (subvol, &loc, xattr_req, &ciatt,
				     dict_rsp, NULL);
	}
	if (ret)
		goto out;

	inode = inode_link (loc.inode, loc.parent, component, &ciatt);
found:
	if (inode)
		inode_lookup (inode);
	if (iatt)
		*iatt = ciatt;
out:
	if (xattr_req)
		dict_unref (xattr_req);

	loc_wipe (&loc);

	return inode;
}

int
qd_resolve_root (xlator_t *this, xlator_t *subvol, loc_t *root_loc,
                 struct iatt *iatt, dict_t **dict_rsp)
{
        int     ret             = -1;
        dict_t  *dict_req       = NULL;

        dict_req = dict_new ();
        if (!dict_req) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Got ENOMEM while creating dict");
                goto out;
        }

        ret = syncop_lookup (subvol, root_loc, dict_req, iatt, dict_rsp, NULL);
        if (-1 == ret) {
                // handle the errors
        }
out:
        return ret;
}

int
qd_resolve_path (xlator_t *this, xlator_t *subvol, qd_vols_conf_t *this_vol,
                 limits_t *entry, loc_t *root_loc, loc_t *entry_loc,
                 dict_t **dict_rsp, int reval)
{
        char                    *component      = NULL;
        char                    *next_component = NULL;
        char                    *saveptr        = NULL;
        char                    *path           = NULL;
        int                      ret            = 0;
        dict_t                  *dict_req       = NULL;
        struct iatt              piatt          = {0,};
        inode_t                 *parent         = NULL;
        inode_t                 *inode          = root_loc->inode;

        ret = qd_resolve_root (this, subvol, root_loc, &piatt, dict_rsp);
        if (ret) {
                // handle errors
                goto out;
        }

        path = gf_strdup (entry->path);
	for (component = strtok_r (path, "/", &saveptr);
	     component; component = next_component) {

		next_component = strtok_r (NULL, "/", &saveptr);

		if (parent)
			inode_unref (parent);

		parent = inode;

                /* Get the xattrs in lookup for the last component */
                if (!next_component) {
                        ret = dict_reset (*dict_rsp);
                        if (ret)
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Couldn't reset dict");

                        dict_req = dict_new ();
                        if (!dict_req) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "ENOMEM while allocating dict");
                                ret = -1;
                                goto out;
                        }

                        ret = dict_set_uint64 (dict_req, QUOTA_SIZE_KEY, 0);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Couldn't set dict");
                }

		inode = qd_resolve_component (this, subvol, parent, component,
                                              &piatt, dict_req, dict_rsp,
                                              (reval || !next_component));
		if (!inode)
			break;

		if (!next_component)
			break;

		if (!IA_ISDIR (piatt.ia_type)) {
			/* next_component exists and this component is
			   not a directory
			*/
			inode_unref (inode);
			inode = NULL;
			ret = -1;
			errno = ENOTDIR;
			break;
		}
	}

	if (parent && next_component)
		goto out;

	entry_loc->parent = parent;
	if (parent) {
		uuid_copy (entry_loc->pargfid, parent->gfid);
		entry_loc->name = component;
	}

	entry_loc->inode = inode;
	if (inode) {
		uuid_copy (entry_loc->gfid, inode->gfid);
		ret = 0;
	}

	qd_loc_touchup (entry_loc);
out:
	GF_FREE (path);

	return ret;
}

/*
int
qd_resolve_path (xlator_t *this, xlator_t *subvol, qd_vols_conf_t *this_vol,
                 limits_t *entry, loc_t *root_loc, loc_t *entry_loc,
                 dict_t *dict_rsp)
{
        int              ret            = 0;
        int              is_res_done    = 0;
        loc_t            par_loc        = {0,};
        char            *save_ptr       = NULL;
        char            *cur_path       = NULL;
        loc_t            cur_loc        = {0,};
        struct iatt      buf            = {0,};
        struct iatt      par_buf        = {0,};
        char            *path           = NULL;
        dict_t          *dict_req       = NULL;

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
                is_res_done = qd_get_child (path, &save_ptr, cur_path);
                if (-1 == is_res_done) {
                        gf_log (this->name, 4, "");
                        break;
                }
                if (1 == is_res_done) {
                        if (dict_req)
                                break;
                        dict_req = dict_new ();
                        ret = dict_set_uint64 (dict_req, QUOTA_SIZE_KEY, 0);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Couldn't set dict");
                }

                qd_build_loc (this, this_vol, cur_path, par_loc.inode,
                                 &cur_loc);

                ret = syncop_lookup (subvol, &cur_loc, dict_req, &buf,
                                     &dict_rsp, &par_buf);
                if (-1 == ret) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Lookup failed on %s:%d", cur_loc.path, errno);
                }

                if (1 == is_res_done)
                        break;
                par_loc = cur_loc;
        }
out:
        dict_destroy (dict_req);
        GF_FREE (path);
        loc_copy (entry_loc, &cur_loc);
        loc_wipe (&cur_loc);
        return ret;
}
*/

/* Loggs if
 *  i.   Usage crossed soft limit
 *  ii.  Usage above soft limit and log timed out
 */
void
qd_log_usage (xlator_t *this, qd_vols_conf_t *this_vol, limits_t *entry,
              int64_t cur_size)
{
        struct timeval           cur_time       = {0,};

        gettimeofday (&cur_time, NULL);

        if (DID_CROSS_SOFT_LIMIT (entry->soft_lim, entry->prev_size, cur_size)) {
                entry->prev_log_tv = cur_time;
                gf_log (this->name, GF_LOG_WARNING, "Usage crossed soft limit:"
                        " %ld for %s", entry->soft_lim, entry->path);
        } else if (cur_size > entry->soft_lim &&
                   quota_timeout (&entry->prev_log_tv, this_vol->log_timeout)) {
                entry->prev_log_tv = cur_time;
                gf_log (this->name, GF_LOG_WARNING, "Usage %ld is above %s"
                        " limit for %s", entry->soft_lim,
                        (cur_size > entry->hard_lim)? "hard": "soft",
                        entry->path);
        }
}

int
qd_getsetxattr (xlator_t *this, struct limits_level *list, loc_t *root_loc,
                xlator_t *subvol)
{
        limits_t        *entry          = NULL;
        limits_t        *next           = NULL;
        int32_t          ret            = -1;
        loc_t            entry_loc      = {0,};
        dict_t          *dict           = NULL;
        int64_t          cur_size       = 0;
        int64_t          prev_size      = 0;
        quota_priv_t    *priv           = NULL;
        int64_t         *size           = NULL;
        gf_boolean_t     resend_size    = _gf_true;
        int              reval          = 0;

        priv = this->private;

        list_for_each_entry_safe (entry, next, &list->limit_head, limit_list) {
                loc_wipe (&entry_loc);

                ret = qd_resolve_path (this, subvol, GET_THIS_VOL (list),
                                       entry, root_loc, &entry_loc, &dict,
                                       0);
                if (-1 == ret && ESTALE == errno) {
                        if (reval < 1) {
                                reval ++;
                                ret = qd_resolve_path (this, subvol,
                                                 GET_THIS_VOL (list), entry,
                                                 root_loc, &entry_loc, &dict,
                                                 1);
                        }
                }

                if (ret) {
                        // handle errors
                }

                ret = dict_get_bin (dict, QUOTA_SIZE_KEY, (void **)&size);
                if (0 != ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Couldn't get size"
                                " from the dict");
                        goto free_and_continue;
                }

                cur_size = ntoh64 (*size);

                qd_log_usage (this, GET_THIS_VOL (list), entry, cur_size);

                if (!resend_size && entry->prev_size == cur_size)
                        goto free_and_continue;

                prev_size = entry->prev_size;
                LOCK (&priv->lock);
                {
                        entry->prev_size = cur_size;
                }
                UNLOCK (&priv->lock);

                QUOTA_ALLOC_OR_GOTO (size, int64_t, free_and_continue);
                *size = hton64 (cur_size);

                ret = dict_reset (dict);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING,
                                "Couldn't reset dict");

                ret = dict_set_bin (dict, QUOTA_UPDATE_USAGE_KEY, size,
                                    sizeof (int64_t));
                if (-1 == ret) {
                        // Handle the error
                }

                ret = syncop_setxattr (subvol, &entry_loc, dict, 0);
                if (-1 == ret) {
                        /* handle the errors */
                }


                /* Move the node to the corresponding list, based on the usage
                 * shift */
                LOCK (&priv->lock);
                {
                        /* usage > soft_limit? */
                        if (prev_size < entry->soft_lim &&
                                        cur_size >= entry->soft_lim)
                                list_move (&entry->limit_list,
                                           &(GET_THIS_VOL(list)->above_soft.limit_head));
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
qd_trigger_periodically (void *args)
{
        int                      ret            = -1;
        struct limits_level     *list           = NULL;
        xlator_t                *this           = NULL;
        xlator_t                *subvol         = NULL;
        inode_t                 *root_inode     = NULL;
        loc_t                    root_loc       = {0,};

        this = THIS;
        list = args;

        subvol = qd_get_subvol (this, GET_THIS_VOL (list));
        if (!subvol) {
                gf_log (this->name, GF_LOG_ERROR, "No subvol found");
                return -1;
        }

        root_inode = qd_build_root_inode (this, GET_THIS_VOL (list));
        if (!root_inode) {
                gf_log (this->name, GF_LOG_ERROR, "New itable create failed");
                return -1;
        }

        qd_build_root_loc (root_inode, &root_loc);

        while (GF_UNIVERSAL_ANSWER) {
                if (!list_empty (&list->limit_head)) {
                        ret = qd_getsetxattr (this, list, &root_loc, subvol);
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
qd_trigger_periodically_try_again (int ret, call_frame_t *frame, void *args)
{
        gf_log ("quota-client", GF_LOG_ERROR, "Synctask stopped unexpectedly, "
                "trying to restart");

        ret = synctask_new (THIS->ctx->env,
                            qd_trigger_periodically,
                            qd_trigger_periodically_try_again,
                            frame, args);
        if (-1 == ret)
                gf_log ("quota-client", GF_LOG_ERROR, "Synctask creation "
                        "failed for %s",
                        (GET_THIS_VOL ((struct limits_level *)args))->name);

        return ret;
}


int
qd_start_threads (xlator_t *this, int subvol_idx)
{
        quota_priv_t            *priv           = NULL;
        int                      ret            = 0;
        qd_vols_conf_t          *this_vol       = NULL;

        priv = this->private;

        this_vol = priv->qd_vols_conf[subvol_idx];

        if (list_empty (&this_vol->above_soft.limit_head)) {
                gf_log (this->name, GF_LOG_DEBUG, "No limit is set on "
                        "volume %s", this_vol->name);
                goto err;
        }

        /* Create 2 threads for soft and hard limits */
        ret = synctask_new (this->ctx->env,
                            qd_trigger_periodically,
                            qd_trigger_periodically_try_again,
                            NULL, (void *)&this_vol->below_soft);
        if (-1 == ret) {
                gf_log (this->name, GF_LOG_ERROR, "Synctask creation "
                        "failed for %s", this_vol->name);
                goto err;
        }

        ret = synctask_new (this->ctx->env,
                            qd_trigger_periodically,
                            qd_trigger_periodically_try_again,
                            NULL, (void *)&this_vol->above_soft);
        if (-1 == ret) {
                gf_log (this->name, GF_LOG_ERROR, "Synctask creation "
                        "failed for %s", this_vol->name);
                goto err;
        }
err:
        return ret;
}

int
qd_reconfigure (xlator_t *this, dict_t *options)
{
        int32_t           ret           = -1;
        quota_priv_t     *priv          = NULL;
        struct list_head  head          = {0, };
        int               i             = 0;
        xlator_list_t    *subvol        = NULL;
        char             *limits        = NULL;
        qd_vols_conf_t   *this_vol      = NULL;
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

        for (i=0, subvol = this->children;
             subvol;
             subvol = subvol->next, i++) {

                this_vol = priv->qd_vols_conf [i];

                gf_asprintf (&option_str, "%s.limit-set",
                             subvol->xlator->name);
                ret = dict_get_str (this->options, option_str, &limits);
                if (ret)
                        continue;

                ret = qd_parse_limits (priv, this, limits, &head,
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

        ret = 0;
out:
        return ret;
}


void
qd_fini (xlator_t *this)
{
        return;
}

int32_t
qd_init (xlator_t *this)
{
        int32_t          ret            = -1;
        quota_priv_t    *priv           = NULL;
        int              i              = 0;
        char            *option_str     = NULL;
        xlator_list_t   *subvol         = NULL;
        char            *limits         = NULL;
        int              subvol_cnt     = 0;
        qd_vols_conf_t  *this_vol       = NULL;

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

        for (i = 0, subvol = this->children;
             subvol;
             i++, subvol = subvol->next);
        subvol_cnt = i;

        priv->qd_vols_conf = GF_CALLOC (sizeof (qd_vols_conf_t *),
                                        subvol_cnt, gf_quota_mt_qd_vols_conf_t);

        for (i = 0, subvol = this->children;
             subvol;
             subvol = subvol->next, i++) {
                //priv->qd_vols_conf[i] = GF_CALLOC (sizeof (qd_vols_conf_t), 1,
                 //                                  gf_quota_mt_qd_vols_conf_t);
                QUOTA_ALLOC_OR_GOTO (priv->qd_vols_conf[i],
                                     qd_vols_conf_t, err);

                this_vol = priv->qd_vols_conf[i];

                INIT_LIST_HEAD (&this_vol->above_soft.limit_head);
                INIT_LIST_HEAD (&this_vol->below_soft.limit_head);

                this_vol->name = subvol->xlator->name;

                this_vol->below_soft.my_vol =
                this_vol->above_soft.my_vol = this_vol;

                ret = gf_asprintf (&option_str, "%s.limit-set", this_vol->name);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "ENOMEM %s", this_vol->name);
                        continue;
                }
                ret = dict_get_str (this->options, option_str, &limits);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "dict get failed, ENOMEM??");
                        continue;
                }

                ret = qd_parse_limits (priv, this, limits, NULL, this_vol);
                GF_FREE (option_str);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Couldn't parse limits for %s", this_vol->name);
                        continue;
                }

                gf_asprintf (&option_str, "%s.soft-timeout", this_vol->name);
                GF_OPTION_INIT (option_str, this_vol->below_soft.time_out,
                                uint64, err);
                GF_FREE (option_str);

                gf_asprintf (&option_str, "%s.hard-timeout", this_vol->name);
                GF_OPTION_INIT (option_str, this_vol->above_soft.time_out,
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
qd_notify (xlator_t *this, int event, void *data, ...)
{
        xlator_list_t   *subvol                 = NULL;
        xlator_t        *subvol_rec             = NULL;
        quota_priv_t    *priv                   = NULL;
        int              i                      = 0;
        int              ret                    = 0;

        subvol_rec = data;
        priv = this->private;

        for (i=0, subvol = this->children; subvol; i++, subvol = subvol->next) {
                if (0 == strcmp (priv->qd_vols_conf[i]->name, subvol_rec->name))
                        break;
        }

        switch (event) {
        case GF_EVENT_CHILD_UP:
        {
                resend_size = _gf_true;

                ret = qd_start_threads (this, i);
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
                        priv->qd_vols_conf [i]->name);
                break;
        }
        default:
                default_notify (this, event, data);
        }


out:
        return ret;
}

class_methods_t class_methods = {
        .init           = qd_init,
        .fini           = qd_fini,
        .reconfigure    = qd_reconfigure,
        .notify         = qd_notify,
};

int32_t
qd_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, const char *name,
             dict_t *xdata)
{
        STACK_WIND (frame, default_getxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr, loc, name, xdata);
        return 0;
}

int32_t
qd_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
             int flags, dict_t *xdata)
{
        STACK_WIND (frame, default_setxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->setxattr, loc, dict,
                    flags, xdata);
        return 0;
}

int32_t
qd_forget (xlator_t *this, inode_t *inode)
{
        return 0;
}

struct xlator_fops fops = {
        .getxattr       = qd_getxattr,
        .setxattr       = qd_setxattr
};

struct xlator_cbks cbks = {
        .forget         = qd_forget
};

struct volume_options options[] = {
        {.key = {"*.limit-set"}},
        {.key = {"*.soft-timeout"},
         .type = GF_OPTION_TYPE_SIZET,
         .min = 1,
         .default_value = "10",
         .description = ""
        },
        {.key = {"*.hard-timeout"},
         .type = GF_OPTION_TYPE_SIZET,
         .min = 0,
         .default_value = "2",
         .description = ""
        },
        {.key = {"*.log-timeout"},
         .type = GF_OPTION_TYPE_SIZET,
         .min = 0,
         .max = LONG_MAX,
         /* default weekly (7 * 24 * 60 *60) */
         .default_value = "604800",
         .description = ""
        },
        {.key = {NULL}}
};
