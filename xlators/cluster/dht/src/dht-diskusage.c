/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

/* TODO: add NS locking */

#include "glusterfs.h"
#include "xlator.h"
#include "dht-common.h"
#include "defaults.h"

#include <sys/time.h>


int
dht_du_info_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int op_ret, int op_errno, struct statvfs *statvfs)
{
	dht_conf_t    *conf         = NULL;
	call_frame_t  *prev          = NULL;
	int            this_call_cnt = 0;
	int            i = 0;
	double         percent = 0;
	double         percent_inodes = 0;
	uint64_t       bytes = 0;

	conf = this->private;
	prev = cookie;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_WARNING,
			"failed to get disk info from %s", prev->this->name);
		goto out;
	}

	if (statvfs && statvfs->f_blocks) {
		percent = (statvfs->f_bavail * 100) / statvfs->f_blocks;
		bytes = (statvfs->f_bavail * statvfs->f_frsize);
	}

	if (statvfs && statvfs->f_files) {
		percent_inodes = (statvfs->f_ffree * 100) / statvfs->f_files;
	} else {
		/* set percent inodes to 100 for dynamically allocated inode filesystems
		   this logic holds good so that, distribute has nothing to worry about
		   total inodes rather let the 'create()' to be scheduled on the hashed
		   subvol regardless of the total inodes. since we have no awareness on
		   loosing inodes this logic fits well
		*/
		percent_inodes = 100;
	}

	LOCK (&conf->subvolume_lock);
	{
		for (i = 0; i < conf->subvolume_cnt; i++)
			if (prev->this == conf->subvolumes[i]) {
				conf->du_stats[i].avail_percent = percent;
				conf->du_stats[i].avail_space   = bytes;
				conf->du_stats[i].avail_inodes  = percent_inodes;
				gf_log (this->name, GF_LOG_DEBUG,
					"on subvolume '%s': avail_percent is: "
					"%.2f and avail_space is: %"PRIu64" "
					"and avail_inodes is: %.2f",
					prev->this->name,
					conf->du_stats[i].avail_percent,
					conf->du_stats[i].avail_space,
					conf->du_stats[i].avail_inodes);
			}
	}
	UNLOCK (&conf->subvolume_lock);

out:
	this_call_cnt = dht_frame_return (frame);
	if (is_last_call (this_call_cnt))
		DHT_STACK_DESTROY (frame);

	return 0;
}

int
dht_get_du_info_for_subvol (xlator_t *this, int subvol_idx)
{
	dht_conf_t    *conf         = NULL;
	call_frame_t  *statfs_frame = NULL;
	dht_local_t   *statfs_local = NULL;
	call_pool_t   *pool         = NULL;
	loc_t          tmp_loc      = {0,};

	conf = this->private;
	pool = this->ctx->pool;

	statfs_frame = create_frame (this, pool);
	if (!statfs_frame) {
		goto err;
	}

	/* local->fop value is not used in this case */
	statfs_local = dht_local_init (statfs_frame, NULL, NULL,
				       GF_FOP_MAXVALUE);
	if (!statfs_local) {
		goto err;
	}

        /* make it root gfid, should be enough to get the proper info back */
        tmp_loc.gfid[15] = 1;

	statfs_local->call_cnt = 1;
	STACK_WIND (statfs_frame, dht_du_info_cbk,
		    conf->subvolumes[subvol_idx],
		    conf->subvolumes[subvol_idx]->fops->statfs,
		    &tmp_loc);

	return 0;
err:
	if (statfs_frame)
		DHT_STACK_DESTROY (statfs_frame);

	return -1;
}

int
dht_get_du_info (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	int            i            = 0;
	dht_conf_t    *conf         = NULL;
	call_frame_t  *statfs_frame = NULL;
	dht_local_t   *statfs_local = NULL;
	struct timeval tv           = {0,};
        loc_t          tmp_loc      = {0,};

	conf  = this->private;

	gettimeofday (&tv, NULL);

        /* make it root gfid, should be enough to get the proper
           info back */
        tmp_loc.gfid[15] = 1;

	if (tv.tv_sec > (conf->refresh_interval
			 + conf->last_stat_fetch.tv_sec)) {

		statfs_frame = copy_frame (frame);
		if (!statfs_frame) {
			goto err;
		}

		/* In this case, 'local->fop' is not used */
		statfs_local = dht_local_init (statfs_frame, loc, NULL,
					       GF_FOP_MAXVALUE);
		if (!statfs_local) {
			goto err;
		}

		statfs_local->call_cnt = conf->subvolume_cnt;
		for (i = 0; i < conf->subvolume_cnt; i++) {
			STACK_WIND (statfs_frame, dht_du_info_cbk,
				    conf->subvolumes[i],
				    conf->subvolumes[i]->fops->statfs,
				    &tmp_loc);
		}

		conf->last_stat_fetch.tv_sec = tv.tv_sec;
	}
	return 0;
err:
	if (statfs_frame)
		DHT_STACK_DESTROY (statfs_frame);

	return -1;
}


gf_boolean_t
dht_is_subvol_filled (xlator_t *this, xlator_t *subvol)
{
	int         i = 0;
	dht_conf_t *conf = NULL;
	gf_boolean_t subvol_filled_inodes = _gf_false;
	gf_boolean_t subvol_filled_space = _gf_false;
	gf_boolean_t is_subvol_filled = _gf_false;

	conf = this->private;

	/* Check for values above specified percent or free disk */
	LOCK (&conf->subvolume_lock);
	{
		for (i = 0; i < conf->subvolume_cnt; i++) {
			if (subvol == conf->subvolumes[i]) {
				if (conf->disk_unit == 'p') {
					if (conf->du_stats[i].avail_percent <
					    conf->min_free_disk) {
						subvol_filled_space = _gf_true;
						break;
					}

				} else {
					if (conf->du_stats[i].avail_space <
					    conf->min_free_disk) {
						subvol_filled_space = _gf_true;
						break;
					}
				}
				if (conf->du_stats[i].avail_inodes <
				    conf->min_free_inodes) {
					subvol_filled_inodes = _gf_true;
					break;
				}
			}
		}
	}
	UNLOCK (&conf->subvolume_lock);

	if (subvol_filled_space && conf->subvolume_status[i]) {
		if (!(conf->du_stats[i].log++ % (GF_UNIVERSAL_ANSWER * 10))) {
			gf_log (this->name, GF_LOG_WARNING,
				"disk space on subvolume '%s' is getting "
				"full (%.2f %%), consider adding more nodes",
				subvol->name,
				(100 - conf->du_stats[i].avail_percent));
		}
	}

	if (subvol_filled_inodes && conf->subvolume_status[i]) {
		if (!(conf->du_stats[i].log++ % (GF_UNIVERSAL_ANSWER * 10))) {
			gf_log (this->name, GF_LOG_CRITICAL,
				"inodes on subvolume '%s' are at "
				"(%.2f %%), consider adding more nodes",
				subvol->name,
				(100 - conf->du_stats[i].avail_inodes));
		}
	}

	is_subvol_filled = (subvol_filled_space || subvol_filled_inodes);

	return is_subvol_filled;
}

xlator_t *
dht_free_disk_available_subvol (xlator_t *this, xlator_t *subvol)
{
	int         i = 0;
	double      max = 0;
	double      max_inodes = 0;
	xlator_t   *avail_subvol = NULL;
	dht_conf_t *conf = NULL;

	conf = this->private;

	LOCK (&conf->subvolume_lock);
	{
		for (i = 0; i < conf->subvolume_cnt; i++) {
			if (conf->disk_unit == 'p') {
				if ((conf->du_stats[i].avail_percent > max)
				    && (conf->du_stats[i].avail_inodes > max_inodes)) {
					max = conf->du_stats[i].avail_percent;
					max_inodes = conf->du_stats[i].avail_inodes;
					avail_subvol = conf->subvolumes[i];
				}
			} else {
				if ((conf->du_stats[i].avail_space > max)
				    && (conf->du_stats[i].avail_inodes > max_inodes)) {
					max = conf->du_stats[i].avail_space;
					max_inodes = conf->du_stats[i].avail_inodes;
					avail_subvol = conf->subvolumes[i];
				}

			}
		}
	}
	UNLOCK (&conf->subvolume_lock);

	if (!avail_subvol) {
		gf_log (this->name, GF_LOG_DEBUG,
			"no subvolume has enough free space and inodes to create");
	}

	if ((max < conf->min_free_disk) && (max_inodes < conf->min_free_inodes))
		avail_subvol = subvol;

	if (!avail_subvol)
		avail_subvol = subvol;

	return avail_subvol;
}
