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

#include "afr.h"
#include "afr-self-heal.h"
#include "afr-self-heald.h"
#include "protocol-common.h"

#define SHD_INODE_LRU_LIMIT          2048
#define AFR_EH_SPLIT_BRAIN_LIMIT     1024
#define AFR_STATISTICS_HISTORY_SIZE    50


#define ASSERT_LOCAL(this, healer)				\
	if (!afr_shd_is_subvol_local(this, healer->subvol)) {	\
		healer->local = _gf_false;			\
		if (safe_break (healer)) {			\
			break;					\
		} else {					\
			continue;				\
		}						\
	} else {						\
		healer->local = _gf_true;			\
	}


#define NTH_INDEX_HEALER(this, n) &((((afr_private_t *)this->private))->shd.index_healers[n])
#define NTH_FULL_HEALER(this, n) &((((afr_private_t *)this->private))->shd.full_healers[n])

int afr_shd_gfid_to_path (xlator_t *this, xlator_t *subvol, uuid_t gfid, char **path_p);

char *
afr_subvol_name (xlator_t *this, int subvol)
{
	afr_private_t *priv = NULL;

	priv = this->private;
	if (subvol < 0 || subvol > priv->child_count)
		return NULL;

	return priv->children[subvol]->name;
}


void
afr_destroy_crawl_event_data (void *data)
{
        return;
}


void
afr_destroy_shd_event_data (void *data)
{
	shd_event_t *shd_event = data;

	if (!shd_event)
		return;
	GF_FREE (shd_event->path);

        return;
}


gf_boolean_t
afr_shd_is_subvol_local (xlator_t *this, int subvol)
{
	char *pathinfo = NULL;
	afr_private_t *priv = NULL;
	dict_t *xattr = NULL;
	int ret = 0;
	gf_boolean_t is_local = _gf_false;
	loc_t loc = {0, };

	priv = this->private;

	loc.inode = this->itable->root;
	uuid_copy (loc.gfid, loc.inode->gfid);

	ret = syncop_getxattr (priv->children[subvol], &loc, &xattr,
			       GF_XATTR_PATHINFO_KEY);
	if (ret) {
		is_local = _gf_false;
                goto out;
        }

	if (!xattr) {
		is_local = _gf_false;
                goto out;
        }

	ret = dict_get_str (xattr, GF_XATTR_PATHINFO_KEY, &pathinfo);
	if (ret) {
		is_local =  _gf_false;
                goto out;
        }

	afr_local_pathinfo (pathinfo, &is_local);

	gf_log (this->name, GF_LOG_DEBUG, "subvol %s is %slocal",
		priv->children[subvol]->name, is_local? "" : "not ");

out:
        if (xattr)
                dict_unref (xattr);

	return is_local;
}


int
__afr_shd_healer_wait (struct subvol_healer *healer)
{
	afr_private_t *priv = NULL;
	struct timespec wait_till = {0, };
	int ret = 0;

	priv = healer->this->private;

disabled_loop:
	wait_till.tv_sec = time (NULL) + 60;

	while (!healer->rerun) {
		ret = pthread_cond_timedwait (&healer->cond,
					      &healer->mutex,
					      &wait_till);
		if (ret == ETIMEDOUT)
			break;
	}

	ret = healer->rerun;
	healer->rerun = 0;

	if (!priv->shd.enabled)
		goto disabled_loop;

	return ret;
}


int
afr_shd_healer_wait (struct subvol_healer *healer)
{
	int ret = 0;

	pthread_mutex_lock (&healer->mutex);
	{
		ret = __afr_shd_healer_wait (healer);
	}
	pthread_mutex_unlock (&healer->mutex);

	return ret;
}


gf_boolean_t
safe_break (struct subvol_healer *healer)
{
	gf_boolean_t ret = _gf_false;

	pthread_mutex_lock (&healer->mutex);
	{
		if (healer->rerun)
			goto unlock;

		healer->running = _gf_false;
		ret = _gf_true;
	}
unlock:
	pthread_mutex_unlock (&healer->mutex);

	return ret;
}


inode_t *
afr_shd_inode_find (xlator_t *this, xlator_t *subvol, uuid_t gfid)
{
	inode_t *inode = NULL;
	int ret = 0;
	loc_t loc = {0, };
	struct iatt iatt = {0, };

	inode = inode_find (this->itable, gfid);
	if (inode) {
                inode_lookup (inode);
		goto out;
        }

	loc.inode = inode_new (this->itable);
	if (!loc.inode)
		goto out;
	uuid_copy (loc.gfid, gfid);

	ret = syncop_lookup (subvol, &loc, NULL, &iatt, NULL, NULL);
	if (ret < 0)
		goto out;

	inode = inode_link (loc.inode, NULL, NULL, &iatt);
	if (inode)
		inode_lookup (inode);
out:
	loc_wipe (&loc);
	return inode;
}


fd_t *
afr_shd_index_opendir (xlator_t *this, int child)
{
	fd_t *fd = NULL;
	afr_private_t *priv = NULL;
	xlator_t *subvol = NULL;
	loc_t rootloc = {0, };
	inode_t *inode = NULL;
	int ret = 0;
	dict_t *xattr = NULL;
	void *index_gfid = NULL;
	loc_t loc = {0, };

	priv = this->private;
	subvol = priv->children[child];

	rootloc.inode = inode_ref (this->itable->root);
	uuid_copy (rootloc.gfid, rootloc.inode->gfid);

	ret = syncop_getxattr (subvol, &rootloc, &xattr,
			       GF_XATTROP_INDEX_GFID);
	if (ret || !xattr) {
		errno = -ret;
		goto out;
	}

	ret = dict_get_ptr (xattr, GF_XATTROP_INDEX_GFID, &index_gfid);
	if (ret)
		goto out;

	gf_log (this->name, GF_LOG_DEBUG, "index-dir gfid for %s: %s",
		subvol->name, uuid_utoa (index_gfid));

	inode = afr_shd_inode_find (this, subvol, index_gfid);
	if (!inode)
		goto out;

	fd = fd_create (inode, GF_CLIENT_PID_AFR_SELF_HEALD);
	if (!fd)
		goto out;

	uuid_copy (loc.gfid, index_gfid);
	loc.inode = inode;

	ret = syncop_opendir(this, &loc, fd);
	if (ret) {
	/*
	 * On Linux, if the brick was not updated, opendir will
	 * fail. We therefore use backward compatible code
	 * that violate the standards by reusing offsets
	 * in seekdir() from different DIR *, but it works on Linux.
	 *
	 * On other systems it never worked, hence we do not need
	 * to provide backward-compatibility.
	 */
#ifdef GF_LINUX_HOST_OS
		fd_unref (fd);
		fd = fd_anonymous (inode);
#else /* GF_LINUX_HOST_OS */
		gf_log(this->name, GF_LOG_ERROR,
		       "opendir of %s for %s failed: %s",
		       uuid_utoa (index_gfid), subvol->name, strerror(errno));
		fd_unref (fd);
		fd = NULL;
		goto out;
#endif /* GF_LINUX_HOST_OS */
	}

out:
	loc_wipe (&rootloc);

	if (inode)
		inode_unref (inode);

	if (xattr)
		dict_unref (xattr);
	return fd;
}


int
afr_shd_index_purge (xlator_t *subvol, inode_t *inode, char *name)
{
	loc_t loc = {0, };
	int ret = 0;

	loc.parent = inode_ref (inode);
	loc.name = name;

	ret = syncop_unlink (subvol, &loc);

	loc_wipe (&loc);
	return ret;
}


int
afr_shd_selfheal_name (struct subvol_healer *healer, int child, uuid_t parent,
		       const char *bname)
{
	int ret = -1;

	ret = afr_selfheal_name (THIS, parent, bname, NULL);

	return ret;
}

int
afr_shd_selfheal (struct subvol_healer *healer, int child, uuid_t gfid)
{
	int ret = 0;
	eh_t *eh = NULL;
	afr_private_t *priv = NULL;
	afr_self_heald_t *shd = NULL;
	shd_event_t *shd_event = NULL;
	char *path = NULL;
	xlator_t *subvol = NULL;
	xlator_t *this = NULL;
	crawl_event_t *crawl_event = NULL;

	this = healer->this;
	priv = this->private;
	shd = &priv->shd;
	crawl_event = &healer->crawl_event;

	subvol = priv->children[child];

        //If this fails with ENOENT/ESTALE index is stale
        ret = afr_shd_gfid_to_path (this, subvol, gfid, &path);
        if (ret < 0)
                return ret;

	ret = afr_selfheal (this, gfid);

	if (ret == -EIO) {
		eh = shd->split_brain;
		crawl_event->split_brain_count++;
	} else if (ret < 0) {
		crawl_event->heal_failed_count++;
	} else if (ret == 0) {
		crawl_event->healed_count++;
	}

	if (eh) {
		shd_event = GF_CALLOC (1, sizeof(*shd_event),
				       gf_afr_mt_shd_event_t);
		if (!shd_event)
                        goto out;

		shd_event->child = child;
		shd_event->path = path;

		if (eh_save_history (eh, shd_event) < 0)
                        goto out;

                shd_event = NULL;
                path = NULL;
	}
out:
        GF_FREE (shd_event);
        GF_FREE (path);
	return ret;
}


void
afr_shd_sweep_prepare (struct subvol_healer *healer)
{
	crawl_event_t *event = NULL;

	event = &healer->crawl_event;

	event->healed_count = 0;
	event->split_brain_count = 0;
	event->heal_failed_count = 0;

	time (&event->start_time);
	event->end_time = 0;
}


void
afr_shd_sweep_done (struct subvol_healer *healer)
{
	crawl_event_t *event = NULL;
	crawl_event_t *history = NULL;
	afr_self_heald_t *shd = NULL;

	event = &healer->crawl_event;
	shd = &(((afr_private_t *)healer->this->private)->shd);

	time (&event->end_time);
	history = memdup (event, sizeof (*event));
	event->start_time = 0;

	if (!history)
		return;

	if (eh_save_history (shd->statistics[healer->subvol], history) < 0)
		GF_FREE (history);
}


int
afr_shd_index_sweep (struct subvol_healer *healer)
{
	xlator_t *this = NULL;
	int child = -1;
	fd_t *fd = NULL;
	xlator_t *subvol = NULL;
	afr_private_t *priv = NULL;
	off_t offset = 0;
	gf_dirent_t entries;
	gf_dirent_t *entry = NULL;
	uuid_t gfid;
	int ret = 0;
	int count = 0;

	this = healer->this;
	child = healer->subvol;
	priv = this->private;
	subvol = priv->children[child];

	fd = afr_shd_index_opendir (this, child);
	if (!fd) {
		gf_log (this->name, GF_LOG_WARNING,
			"unable to opendir index-dir on %s", subvol->name);
		return -errno;
	}

	INIT_LIST_HEAD (&entries.list);

	while ((ret = syncop_readdir (subvol, fd, 131072, offset, &entries))) {
		if (ret > 0)
			ret = 0;
		list_for_each_entry (entry, &entries.list, list) {
			offset = entry->d_off;

			if (!priv->shd.enabled) {
				ret = -EBUSY;
				break;
			}

			if (!strcmp (entry->d_name, ".") ||
			    !strcmp (entry->d_name, ".."))
				continue;

			gf_log (this->name, GF_LOG_DEBUG, "got entry: %s",
				entry->d_name);

			ret = uuid_parse (entry->d_name, gfid);
			if (ret)
				continue;

			ret = afr_shd_selfheal (healer, child, gfid);
			if (ret == 0)
				count++;

			if (ret == -ENOENT || ret == -ESTALE) {
				afr_shd_index_purge (subvol, fd->inode,
						     entry->d_name);
				ret = 0;
			}
		}

		gf_dirent_free (&entries);
		if (ret)
			break;
	}

	if (fd) {
                if (fd->inode)
                        inode_forget (fd->inode, 1);
		fd_unref (fd);
        }

	if (!ret)
		ret = count;
	return ret;
}


int
afr_shd_full_sweep (struct subvol_healer *healer, inode_t *inode)
{
	fd_t *fd = NULL;
	xlator_t *this = NULL;
	xlator_t *subvol = NULL;
	afr_private_t *priv = NULL;
	off_t offset = 0;
	gf_dirent_t entries;
	gf_dirent_t *entry = NULL;
	int ret = 0;

	this = healer->this;
	priv = this->private;
	subvol = priv->children[healer->subvol];

	fd = fd_anonymous (inode);
	if (!fd)
		return -errno;

	INIT_LIST_HEAD (&entries.list);

	while ((ret = syncop_readdirp (subvol, fd, 131072, offset, 0, &entries))) {
		if (ret < 0)
			break;

		ret = gf_link_inodes_from_dirent (this, fd->inode, &entries);
		if (ret)
			break;

		list_for_each_entry (entry, &entries.list, list) {
			offset = entry->d_off;

			if (!priv->shd.enabled) {
				ret = -EBUSY;
				break;
			}

			if (!strcmp (entry->d_name, ".") ||
			    !strcmp (entry->d_name, ".."))
				continue;

			afr_shd_selfheal_name (healer, healer->subvol,
					       inode->gfid, entry->d_name);

			afr_shd_selfheal (healer, healer->subvol,
					  entry->d_stat.ia_gfid);

			if (entry->d_stat.ia_type == IA_IFDIR) {
				ret = afr_shd_full_sweep (healer, entry->inode);
				if (ret)
					break;
			}
		}

		gf_dirent_free (&entries);
		if (ret)
			break;
	}

	if (fd)
		fd_unref (fd);
	return ret;
}


void *
afr_shd_index_healer (void *data)
{
	struct subvol_healer *healer = NULL;
	xlator_t *this = NULL;
	int ret = 0;

	healer = data;
	THIS = this = healer->this;

	for (;;) {
		afr_shd_healer_wait (healer);

		ASSERT_LOCAL(this, healer);

		do {
			gf_log (this->name, GF_LOG_DEBUG,
				"starting index sweep on subvol %s",
				afr_subvol_name (this, healer->subvol));

			afr_shd_sweep_prepare (healer);

			ret = afr_shd_index_sweep (healer);

			afr_shd_sweep_done (healer);
			/*
			  As long as at least one gfid was
			  healed, keep retrying. We may have
			  just healed a directory and thereby
			  created entries for other gfids which
			  could not be healed thus far.
			*/

			gf_log (this->name, GF_LOG_DEBUG,
				"finished index sweep on subvol %s",
				afr_subvol_name (this, healer->subvol));
			/*
			  Give a pause before retrying to avoid a busy loop
			  in case the only entry in index is because of
			  an ongoing I/O.
			*/
			sleep (1);
		} while (ret > 0);
	}

	return NULL;
}


void *
afr_shd_full_healer (void *data)
{
	struct subvol_healer *healer = NULL;
	xlator_t *this = NULL;
	int run = 0;

	healer = data;
	THIS = this = healer->this;

	for (;;) {
		pthread_mutex_lock (&healer->mutex);
		{
			run = __afr_shd_healer_wait (healer);
			if (!run)
				healer->running = _gf_false;
		}
		pthread_mutex_unlock (&healer->mutex);

		if (!run)
			break;

		ASSERT_LOCAL(this, healer);

		gf_log (this->name, GF_LOG_INFO,
			"starting full sweep on subvol %s",
			afr_subvol_name (this, healer->subvol));

		afr_shd_sweep_prepare (healer);

		afr_shd_full_sweep (healer, this->itable->root);

		afr_shd_sweep_done (healer);

		gf_log (this->name, GF_LOG_INFO,
			"finished full sweep on subvol %s",
			afr_subvol_name (this, healer->subvol));
	}

	return NULL;
}


int
afr_shd_healer_init (xlator_t *this, struct subvol_healer *healer)
{
	int ret = 0;

	ret = pthread_mutex_init (&healer->mutex, NULL);
	if (ret)
		goto out;

	ret = pthread_cond_init (&healer->cond, NULL);
	if (ret)
		goto out;

	healer->this = this;
	healer->running = _gf_false;
	healer->rerun = _gf_false;
	healer->local = _gf_false;
out:
	return ret;
}


int
afr_shd_healer_spawn (xlator_t *this, struct subvol_healer *healer,
		      void *(threadfn)(void *))
{
	int ret = 0;

	pthread_mutex_lock (&healer->mutex);
	{
		if (healer->running) {
			pthread_cond_signal (&healer->cond);
		} else {
			ret = gf_thread_create (&healer->thread, NULL,
						threadfn, healer);
			if (ret)
				goto unlock;
			healer->running = 1;
		}

		healer->rerun = 1;
	}
unlock:
	pthread_mutex_unlock (&healer->mutex);

	return ret;
}


int
afr_shd_full_healer_spawn (xlator_t *this, int subvol)
{
	return afr_shd_healer_spawn (this, NTH_FULL_HEALER (this, subvol),
				     afr_shd_full_healer);
}


int
afr_shd_index_healer_spawn (xlator_t *this, int subvol)
{
	return afr_shd_healer_spawn (this, NTH_INDEX_HEALER (this, subvol),
				     afr_shd_index_healer);
}


int
afr_shd_dict_add_crawl_event (xlator_t *this, dict_t *output,
			      crawl_event_t *crawl_event)
{
        int             ret = 0;
        uint64_t        count = 0;
        char            key[256] = {0};
        int             xl_id = 0;
        uint64_t        healed_count = 0;
        uint64_t        split_brain_count = 0;
        uint64_t        heal_failed_count = 0;
        char            *start_time_str = 0;
        char            *end_time_str = NULL;
        char            *crawl_type = NULL;
        int             progress = -1;
	int             child = -1;

	child = crawl_event->child;
        healed_count = crawl_event->healed_count;
        split_brain_count = crawl_event->split_brain_count;
        heal_failed_count = crawl_event->heal_failed_count;
        crawl_type = crawl_event->crawl_type;

	if (!crawl_event->start_time)
		goto out;

        start_time_str = gf_strdup (ctime (&crawl_event->start_time));

	if (crawl_event->end_time)
		end_time_str = gf_strdup (ctime (&crawl_event->end_time));

        ret = dict_get_int32 (output, this->name, &xl_id);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "xl does not have id");
                goto out;
        }

        snprintf (key, sizeof (key), "statistics-%d-%d-count", xl_id, child);
        ret = dict_get_uint64 (output, key, &count);


        snprintf (key, sizeof (key), "statistics_healed_cnt-%d-%d-%"PRIu64,
                  xl_id, child, count);
        ret = dict_set_uint64(output, key, healed_count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
			"Could not add statistics_healed_count to outout");
                goto out;
	}

        snprintf (key, sizeof (key), "statistics_sb_cnt-%d-%d-%"PRIu64,
                  xl_id, child, count);
        ret = dict_set_uint64 (output, key, split_brain_count);
	if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
			"Could not add statistics_split_brain_count to outout");
                goto out;
        }

        snprintf (key, sizeof (key), "statistics_crawl_type-%d-%d-%"PRIu64,
                  xl_id, child, count);
        ret = dict_set_str (output, key, crawl_type);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
			"Could not add statistics_crawl_type to output");
                goto out;
        }

        snprintf (key, sizeof (key), "statistics_heal_failed_cnt-%d-%d-%"PRIu64,
                  xl_id, child, count);
        ret = dict_set_uint64 (output, key, heal_failed_count);
	if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
			"Could not add statistics_healed_failed_count to outout");
                goto out;
        }

        snprintf (key, sizeof (key), "statistics_strt_time-%d-%d-%"PRIu64,
                  xl_id, child, count);
        ret = dict_set_dynstr (output, key, start_time_str);
	if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
			"Could not add statistics_crawl_start_time to outout");
                goto out;
        } else {
		start_time_str = NULL;
	}

	if (!end_time_str)
                progress = 1;
        else
                progress = 0;

        snprintf (key, sizeof (key), "statistics_end_time-%d-%d-%"PRIu64,
                  xl_id, child, count);
        if (!end_time_str)
                end_time_str = gf_strdup ("Could not determine the end time");
        ret = dict_set_dynstr (output, key, end_time_str);
	if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
			"Could not add statistics_crawl_end_time to outout");
                goto out;
        } else {
		end_time_str = NULL;
	}

        snprintf (key, sizeof (key), "statistics_inprogress-%d-%d-%"PRIu64,
                  xl_id, child, count);

        ret = dict_set_int32 (output, key, progress);
	if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
			"Could not add statistics_inprogress to outout");
                goto out;
        }

	snprintf (key, sizeof (key), "statistics-%d-%d-count", xl_id, child);
	ret = dict_set_uint64 (output, key, count + 1);
	if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
			"Could not increment the counter.");
                goto out;
	}
out:
	GF_FREE (start_time_str);
	GF_FREE (end_time_str);
        return ret;
}


int
afr_shd_dict_add_path (xlator_t *this, dict_t *output, int child, char *path,
		       struct timeval *tv)
{
        int             ret = -1;
        uint64_t        count = 0;
        char            key[256] = {0};
        int             xl_id = 0;

        ret = dict_get_int32 (output, this->name, &xl_id);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "xl does not have id");
                goto out;
        }

        snprintf (key, sizeof (key), "%d-%d-count", xl_id, child);
        ret = dict_get_uint64 (output, key, &count);

        snprintf (key, sizeof (key), "%d-%d-%"PRIu64, xl_id, child, count);
	ret = dict_set_dynstr (output, key, path);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "%s: Could not add to output",
                        path);
                goto out;
        }

	if (tv) {
		snprintf (key, sizeof (key), "%d-%d-%"PRIu64"-time", xl_id,
			  child, count);
		ret = dict_set_uint32 (output, key, tv->tv_sec);
		if (ret) {
			gf_log (this->name, GF_LOG_ERROR, "%s: Could not set time",
				path);
			goto out;
		}
	}

        snprintf (key, sizeof (key), "%d-%d-count", xl_id, child);

        ret = dict_set_uint64 (output, key, count + 1);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Could not increment count");
                goto out;
        }

        ret = 0;
out:
        return ret;
}


int
afr_shd_gfid_to_path (xlator_t *this, xlator_t *subvol, uuid_t gfid, char **path_p)
{
        int      ret   = 0;
        char    *path  = NULL;
        loc_t    loc   = {0,};
        dict_t  *xattr = NULL;

	uuid_copy (loc.gfid, gfid);
	loc.inode = inode_new (this->itable);

	ret = syncop_getxattr (subvol, &loc, &xattr, GFID_TO_PATH_KEY);
	if (ret)
		goto out;

	ret = dict_get_str (xattr, GFID_TO_PATH_KEY, &path);
	if (ret || !path) {
		ret = -EINVAL;
                goto out;
        }

	*path_p = gf_strdup (path);
	if (!*path_p) {
		ret = -ENOMEM;
                goto out;
        }

	ret = 0;

out:
        if (xattr)
                dict_unref (xattr);
	loc_wipe (&loc);

        return ret;
}


int
afr_shd_gather_index_entries (xlator_t *this, int child, dict_t *output)
{
	fd_t *fd = NULL;
	xlator_t *subvol = NULL;
	afr_private_t *priv = NULL;
	off_t offset = 0;
	gf_dirent_t entries;
	gf_dirent_t *entry = NULL;
	uuid_t gfid;
	int ret = 0;
	int count = 0;
	char *path = NULL;

	priv = this->private;
	subvol = priv->children[child];

	fd = afr_shd_index_opendir (this, child);
	if (!fd) {
		gf_log (this->name, GF_LOG_WARNING,
			"unable to opendir index-dir on %s", subvol->name);
		return -errno;
	}

	INIT_LIST_HEAD (&entries.list);

	while ((ret = syncop_readdir (subvol, fd, 131072, offset, &entries))) {
		if (ret > 0)
			ret = 0;
		list_for_each_entry (entry, &entries.list, list) {
			offset = entry->d_off;

			if (!strcmp (entry->d_name, ".") ||
			    !strcmp (entry->d_name, ".."))
				continue;

			gf_log (this->name, GF_LOG_DEBUG, "got entry: %s",
				entry->d_name);

			ret = uuid_parse (entry->d_name, gfid);
			if (ret)
				continue;

			path = NULL;
			ret = afr_shd_gfid_to_path (this, subvol, gfid, &path);

			if (ret == -ENOENT || ret == -ESTALE) {
				afr_shd_index_purge (subvol, fd->inode,
						     entry->d_name);
				ret = 0;
				continue;
			}

			ret = afr_shd_dict_add_path (this, output, child, path,
						     NULL);
		}

		gf_dirent_free (&entries);
		if (ret)
			break;
	}

	if (fd) {
                if (fd->inode)
                        inode_forget (fd->inode, 1);
		fd_unref (fd);
        }

	if (!ret)
		ret = count;
	return ret;
}


int
afr_add_shd_event (circular_buffer_t *cb, void *data)
{
	dict_t *output = NULL;
	xlator_t *this = THIS;
	afr_private_t *priv = NULL;
	afr_self_heald_t *shd = NULL;
	shd_event_t *shd_event = NULL;
	char *path = NULL;

	output = data;
	priv = this->private;
	shd = &priv->shd;
	shd_event = cb->data;

	if (!shd->index_healers[shd_event->child].local)
		return 0;

	path = gf_strdup (shd_event->path);
	if (!path)
		return -ENOMEM;

	afr_shd_dict_add_path (this, output, shd_event->child, path,
			       &cb->tv);
	return 0;
}

int
afr_add_crawl_event (circular_buffer_t *cb, void *data)
{
	dict_t *output = NULL;
	xlator_t *this = THIS;
	afr_private_t *priv = NULL;
	afr_self_heald_t *shd = NULL;
	crawl_event_t *crawl_event = NULL;

	output = data;
	priv = this->private;
	shd = &priv->shd;
	crawl_event = cb->data;

	if (!shd->index_healers[crawl_event->child].local)
		return 0;

	afr_shd_dict_add_crawl_event (this, output, crawl_event);

	return 0;
}


int
afr_selfheal_daemon_init (xlator_t *this)
{
	afr_private_t *priv = NULL;
	afr_self_heald_t *shd = NULL;
	int ret = -1;
	int i = 0;

	priv = this->private;
	shd = &priv->shd;

	this->itable = inode_table_new (SHD_INODE_LRU_LIMIT, this);
	if (!this->itable)
		goto out;

	shd->index_healers = GF_CALLOC (sizeof(*shd->index_healers),
					priv->child_count,
					gf_afr_mt_subvol_healer_t);
	if (!shd->index_healers)
		goto out;

	for (i = 0; i < priv->child_count; i++) {
		shd->index_healers[i].subvol = i;
		ret = afr_shd_healer_init (this, &shd->index_healers[i]);
		if (ret)
			goto out;
	}

	shd->full_healers = GF_CALLOC (sizeof(*shd->full_healers),
				       priv->child_count,
				       gf_afr_mt_subvol_healer_t);
	if (!shd->full_healers)
		goto out;
	for (i = 0; i < priv->child_count; i++) {
		shd->full_healers[i].subvol = i;
		ret = afr_shd_healer_init (this, &shd->full_healers[i]);
		if (ret)
			goto out;
	}

	shd->split_brain = eh_new (AFR_EH_SPLIT_BRAIN_LIMIT, _gf_false,
				   afr_destroy_shd_event_data);
	if (!shd->split_brain)
		goto out;

        shd->statistics = GF_CALLOC (sizeof(eh_t *), priv->child_count,
				     gf_common_mt_eh_t);
        if (!shd->statistics)
                goto out;

        for (i = 0; i < priv->child_count ; i++) {
                shd->statistics[i] = eh_new (AFR_STATISTICS_HISTORY_SIZE,
					     _gf_false,
					     afr_destroy_crawl_event_data);
                if (!shd->statistics[i])
                        goto out;
		shd->full_healers[i].crawl_event.child = i;
		shd->full_healers[i].crawl_event.crawl_type = "FULL";
		shd->index_healers[i].crawl_event.child = i;
		shd->index_healers[i].crawl_event.crawl_type = "INDEX";
        }

	ret = 0;
out:
	return ret;
}


int
afr_selfheal_childup (xlator_t *this, int subvol)
{
	afr_shd_index_healer_spawn (this, subvol);

	return 0;
}


int
afr_shd_get_index_count (xlator_t *this, int i, uint64_t *count)
{
	afr_private_t *priv = NULL;
	xlator_t *subvol = NULL;
	loc_t rootloc = {0, };
	dict_t *xattr = NULL;
	int ret = -1;

	priv = this->private;
	subvol = priv->children[i];

	rootloc.inode = inode_ref (this->itable->root);
	uuid_copy (rootloc.gfid, rootloc.inode->gfid);

	ret = syncop_getxattr (subvol, &rootloc, &xattr,
			       GF_XATTROP_INDEX_COUNT);
	if (ret < 0)
		goto out;

	ret = dict_get_uint64 (xattr, GF_XATTROP_INDEX_COUNT, count);
	if (ret)
		goto out;

        ret = 0;

out:
        if (xattr)
                dict_unref (xattr);
	loc_wipe (&rootloc);

	return ret;
}


int
afr_xl_op (xlator_t *this, dict_t *input, dict_t *output)
{
        gf_xl_afr_op_t   op = GF_AFR_OP_INVALID;
        int              ret = 0;
        int              xl_id = 0;
	afr_private_t   *priv = NULL;
	afr_self_heald_t *shd = NULL;
	struct subvol_healer *healer = NULL;
	int i = 0;
	char key[64];
	int op_ret = 0;
	uint64_t cnt = 0;

	priv = this->private;
	shd = &priv->shd;

	for (i = 0; i < priv->child_count; i++)
		if (priv->child_up[i] == -1)
			goto out;

        ret = dict_get_int32 (input, "xl-op", (int32_t*)&op);
        if (ret)
                goto out;
        ret = dict_get_int32 (input, this->name, &xl_id);
        if (ret)
                goto out;
        ret = dict_set_int32 (output, this->name, xl_id);
        if (ret)
                goto out;
        switch (op) {
        case GF_AFR_OP_HEAL_INDEX:
		op_ret = -1;

		for (i = 0; i < priv->child_count; i++) {
			healer = &shd->index_healers[i];
			snprintf (key, sizeof (key), "%d-%d-status", xl_id, i);

			if (!priv->child_up[i]) {
				ret = dict_set_str (output, key,
						    "Brick is not connected");
			} else if (AFR_COUNT (priv->child_up,
					      priv->child_count) < 2) {
				ret = dict_set_str (output, key,
						    "< 2 bricks in replica are up");
			} else if (!afr_shd_is_subvol_local (this, healer->subvol)) {
				ret = dict_set_str (output, key,
						    "Brick is remote");
			} else {
				ret = dict_set_str (output, key,
						    "Started self-heal");
				afr_shd_index_healer_spawn (this, i);
				op_ret = 0;
			}
		}
                break;
        case GF_AFR_OP_HEAL_FULL:
		op_ret = -1;

		for (i = 0; i < priv->child_count; i++) {
			healer = &shd->full_healers[i];
			snprintf (key, sizeof (key), "%d-%d-status", xl_id, i);

			if (!priv->child_up[i]) {
				ret = dict_set_str (output, key,
						    "Brick is not connected");
			} else if (AFR_COUNT (priv->child_up,
					      priv->child_count) < 2) {
				ret = dict_set_str (output, key,
						    "< 2 bricks in replica are up");
			} else if (!afr_shd_is_subvol_local (this, healer->subvol)) {
				ret = dict_set_str (output, key,
						    "Brick is remote");
			} else {
				ret = dict_set_str (output, key,
						    "Started self-heal");
				afr_shd_full_healer_spawn (this, i);
				op_ret = 0;
			}
		}
                break;
        case GF_AFR_OP_INDEX_SUMMARY:
		for (i = 0; i < priv->child_count; i++)
			if (shd->index_healers[i].local)
				afr_shd_gather_index_entries (this, i, output);
                break;
        case GF_AFR_OP_HEALED_FILES:
        case GF_AFR_OP_HEAL_FAILED_FILES:
                for (i = 0; i < priv->child_count; i++) {
                        snprintf (key, sizeof (key), "%d-%d-status", xl_id, i);
                        ret = dict_set_str (output, key, "Operation Not "
                                            "Supported");
                }
                break;
        case GF_AFR_OP_SPLIT_BRAIN_FILES:
		eh_dump (shd->split_brain, output, afr_add_shd_event);
                break;
        case GF_AFR_OP_STATISTICS:
		for (i = 0; i < priv->child_count; i++) {
			eh_dump (shd->statistics[i], output,
				 afr_add_crawl_event);
			afr_shd_dict_add_crawl_event (this, output,
						      &shd->index_healers[i].crawl_event);
			afr_shd_dict_add_crawl_event (this, output,
						      &shd->full_healers[i].crawl_event);
		}
                break;
        case GF_AFR_OP_STATISTICS_HEAL_COUNT:
        case GF_AFR_OP_STATISTICS_HEAL_COUNT_PER_REPLICA:
		op_ret = -1;

		for (i = 0; i < priv->child_count; i++) {
			if (!priv->child_up[i]) {
				snprintf (key, sizeof (key), "%d-%d-status",
                                          xl_id, i);
				ret = dict_set_str (output, key,
						    "Brick is not connected");
			} else {
				snprintf (key, sizeof (key), "%d-%d-hardlinks",
                                          xl_id, i);
				ret = afr_shd_get_index_count (this, i, &cnt);
				if (ret == 0) {
					ret = dict_set_uint64 (output, key, cnt);
				}
				op_ret = 0;
			}
		}

//                ret = _do_crawl_op_on_local_subvols (this, INDEX_TO_BE_HEALED,
//                                                     STATISTICS_TO_BE_HEALED,
//                                                     output);
                break;

        default:
                gf_log (this->name, GF_LOG_ERROR, "Unknown set op %d", op);
                break;
        }
out:
        dict_del (output, this->name);
        return op_ret;
}
