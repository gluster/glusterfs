/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


/* TODO: add NS locking */

#include "glusterfs.h"
#include "xlator.h"
#include "dht-common.h"
#include "dht-messages.h"
#include "defaults.h"

#include <sys/time.h>
#include "events.h"


int
dht_du_info_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int op_ret, int op_errno, struct statvfs *statvfs,
                 dict_t *xdata)
{
	dht_conf_t    *conf         = NULL;
	xlator_t      *prev          = NULL;
	int            this_call_cnt = 0;
	int            i = 0;
	double         percent = 0;
	double         percent_inodes = 0;
	uint64_t       bytes = 0;
        uint32_t       bpc;     /* blocks per chunk */
        uint32_t       chunks   = 0;

	conf = this->private;
	prev = cookie;

	if (op_ret == -1 || !statvfs) {
		gf_msg (this->name, GF_LOG_WARNING, op_errno,
                        DHT_MSG_GET_DISK_INFO_ERROR,
			"failed to get disk info from %s", prev->name);
		goto out;
	}

	if (statvfs->f_blocks) {
		percent = (statvfs->f_bavail * 100) / statvfs->f_blocks;
		bytes = (statvfs->f_bavail * statvfs->f_frsize);
                /*
                 * A 32-bit count of 1MB chunks allows a maximum brick size of
                 * ~4PB.  It's possible that we could see a single local FS
                 * bigger than that some day, but this code is likely to be
                 * irrelevant by then.  Meanwhile, it's more important to keep
                 * the chunk size small so the layout-calculation code that
                 * uses this value can be tested on normal machines.
                 */
                bpc = (1 << 20) / statvfs->f_bsize;
                chunks = (statvfs->f_blocks + bpc - 1) / bpc;
	}

	if (statvfs->f_files) {
		percent_inodes = (statvfs->f_ffree * 100) / statvfs->f_files;
	} else {
                /*
                 * Set percent inodes to 100 for dynamically allocated inode
                 * filesystems. The rationale is that distribute need not
                 * worry about total inodes; rather, let the 'create()' be
                 * scheduled on the hashed subvol regardless of the total
                 * inodes.
		 */
		percent_inodes = 100;
	}

	LOCK (&conf->subvolume_lock);
	{
		for (i = 0; i < conf->subvolume_cnt; i++)
			if (prev == conf->subvolumes[i]) {
				conf->du_stats[i].avail_percent = percent;
				conf->du_stats[i].avail_space   = bytes;
				conf->du_stats[i].avail_inodes  = percent_inodes;
                                conf->du_stats[i].chunks        = chunks;
                                conf->du_stats[i].total_blocks  = statvfs->f_blocks;
                                conf->du_stats[i].avail_blocks  = statvfs->f_bavail;
                                conf->du_stats[i].frsize        = statvfs->f_frsize;

                                gf_msg_debug (this->name, 0,
				              "subvolume '%s': avail_percent "
					      "is: %.2f and avail_space "
                                              "is: %" PRIu64" and avail_inodes"
                                              " is: %.2f",
					      prev->name,
					      conf->du_stats[i].avail_percent,
					      conf->du_stats[i].avail_space,
					      conf->du_stats[i].avail_inodes);
                                break;  /* no point in looping further */
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
	STACK_WIND_COOKIE (statfs_frame, dht_du_info_cbk,
		           conf->subvolumes[subvol_idx],
                           conf->subvolumes[subvol_idx],
		           conf->subvolumes[subvol_idx]->fops->statfs,
		           &tmp_loc, NULL);

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
        int            ret          = -1;
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

                statfs_local->params = dict_new ();
                if (!statfs_local->params)
                        goto err;

                ret = dict_set_int8 (statfs_local->params,
                                     GF_INTERNAL_IGNORE_DEEM_STATFS, 1);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_DICT_SET_FAILED,
                                "Failed to set "
                                GF_INTERNAL_IGNORE_DEEM_STATFS" in dict");
                        goto err;
                }

		statfs_local->call_cnt = conf->subvolume_cnt;
		for (i = 0; i < conf->subvolume_cnt; i++) {
			STACK_WIND_COOKIE (statfs_frame, dht_du_info_cbk,
				           conf->subvolumes[i],
                                           conf->subvolumes[i],
				           conf->subvolumes[i]->fops->statfs,
				           &tmp_loc, statfs_local->params);
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
        char      vol_name[256];
	dht_conf_t *conf = NULL;
	gf_boolean_t subvol_filled_inodes = _gf_false;
	gf_boolean_t subvol_filled_space = _gf_false;
	gf_boolean_t is_subvol_filled = _gf_false;
        double usage = 0;

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
                        usage = 100 - conf->du_stats[i].avail_percent;

			gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_SUBVOL_INSUFF_SPACE,
				"disk space on subvolume '%s' is getting "
				"full (%.2f %%), consider adding more bricks",
				subvol->name, usage);

                        strncpy(vol_name, this->name, sizeof(vol_name));
                        vol_name[(strlen(this->name)-4)] = '\0';

                        gf_event(EVENT_DHT_DISK_USAGE,
                                 "volume=%s;subvol=%s;usage=%.2f %%",
                                 vol_name, subvol->name, usage);
		}
	}

	if (subvol_filled_inodes && conf->subvolume_status[i]) {
		if (!(conf->du_stats[i].log++ % (GF_UNIVERSAL_ANSWER * 10))) {
                        usage = 100 - conf->du_stats[i].avail_inodes;
			gf_msg (this->name, GF_LOG_CRITICAL, 0,
                                DHT_MSG_SUBVOL_INSUFF_INODES,
				"inodes on subvolume '%s' are at "
				"(%.2f %%), consider adding more bricks",
				subvol->name, usage);

                        strncpy(vol_name, this->name, sizeof(vol_name));
                        vol_name[(strlen(this->name)-4)] = '\0';

                        gf_event(EVENT_DHT_INODES_USAGE,
                                 "volume=%s;subvol=%s;usage=%.2f %%",
                                  vol_name, subvol->name, usage);
		}
	}

	is_subvol_filled = (subvol_filled_space || subvol_filled_inodes);

	return is_subvol_filled;
}


/*Get the best subvolume to create the file in*/
xlator_t *
dht_free_disk_available_subvol (xlator_t *this, xlator_t *subvol,
                                dht_local_t *local)
{
	xlator_t   *avail_subvol = NULL;
	dht_conf_t *conf = NULL;
        dht_layout_t *layout = NULL;
        loc_t      *loc = NULL;

	conf = this->private;
        if (!local)
                goto out;
        loc = &local->loc;
        if (!local->layout) {
                layout = dht_layout_get (this, loc->parent);

                if (!layout) {
                        gf_msg_debug (this->name, 0,
                                      "Missing layout. path=%s,"
                                      " parent gfid = %s", loc->path,
                                      uuid_utoa (loc->parent->gfid));
                        goto out;
                }
        } else {
                layout = dht_layout_ref (this, local->layout);
        }

        LOCK (&conf->subvolume_lock);
	{
                avail_subvol = dht_subvol_with_free_space_inodes(this, subvol, NULL,
                                                                 layout, 0);
                if(!avail_subvol)
                {
                        avail_subvol = dht_subvol_maxspace_nonzeroinode(this,
                                                                        subvol,
                                                                        layout);
                }

	}
	UNLOCK (&conf->subvolume_lock);
out:
	if (!avail_subvol) {
		gf_msg_debug (this->name, 0,
		              "No subvolume has enough free space \
                              and/or inodes to create");
                avail_subvol = subvol;
	}

        if (layout)
                dht_layout_unref (this, layout);
	return avail_subvol;
}

static inline
int32_t dht_subvol_has_err (dht_conf_t *conf, xlator_t *this, xlator_t *ignore,
                            dht_layout_t *layout)
{
        int ret = -1;
        int i   = 0;

        if (!this || !layout)
                goto out;

        /* this check is meant for rebalance process. The source of the file
         * should be ignored for space check */
        if (this == ignore) {
                goto out;
        }


        /* check if subvol has layout errors, before selecting it */
        for (i = 0; i < layout->cnt; i++) {
                if (!strcmp (layout->list[i].xlator->name, this->name) &&
                     (layout->list[i].err != 0)) {
                        ret = -1;
                        goto out;
                }
        }

        /* discard decommissioned subvol */
        if (conf->decommission_subvols_cnt) {
                for (i = 0; i < conf->subvolume_cnt; i++) {
                        if (conf->decommissioned_bricks[i] &&
                            conf->decommissioned_bricks[i] == this) {
                                ret = -1;
                                goto out;
                        }
                }
        }

        ret = 0;
out:
        return ret;
}

/*Get subvolume which has both space and inodes more than the min criteria*/
xlator_t *
dht_subvol_with_free_space_inodes(xlator_t *this, xlator_t *subvol, xlator_t *ignore,
                                  dht_layout_t *layout, uint64_t filesize)
{
        int i = 0;
        double max = 0;
        double max_inodes = 0;
        int    ignore_subvol = 0;
        uint64_t total_blocks = 0;
        uint64_t avail_blocks = 0;
        uint64_t frsize = 0;
        double   post_availspace = 0;
        double   post_percent = 0;

        xlator_t *avail_subvol = NULL;
        dht_conf_t *conf = NULL;

        conf = this->private;

        for(i=0; i < conf->subvolume_cnt; i++) {
                /* check if subvol has layout errors and also it is not a
                 * decommissioned brick, before selecting it */
                ignore_subvol = dht_subvol_has_err (conf, conf->subvolumes[i],
                                                    ignore, layout);
                if (ignore_subvol)
                        continue;

                if ((conf->disk_unit == 'p') &&
                    (conf->du_stats[i].avail_percent > conf->min_free_disk) &&
                    (conf->du_stats[i].avail_inodes  > conf->min_free_inodes)) {
                        if ((conf->du_stats[i].avail_inodes > max_inodes) ||
                            (conf->du_stats[i].avail_percent > max)) {
                                max = conf->du_stats[i].avail_percent;
                                max_inodes = conf->du_stats[i].avail_inodes;
                                avail_subvol = conf->subvolumes[i];
                                total_blocks = conf->du_stats[i].total_blocks;
                                avail_blocks = conf->du_stats[i].avail_blocks;
                                frsize       = conf->du_stats[i].frsize;
                        }
                }

                if ((conf->disk_unit != 'p') &&
                    (conf->du_stats[i].avail_space > conf->min_free_disk) &&
                    (conf->du_stats[i].avail_inodes  > conf->min_free_inodes)) {
                        if ((conf->du_stats[i].avail_inodes > max_inodes) ||
                            (conf->du_stats[i].avail_space > max)) {
                                max = conf->du_stats[i].avail_space;
                                max_inodes = conf->du_stats[i].avail_inodes;
                                avail_subvol = conf->subvolumes[i];
                        }
                }
        }

        if (avail_subvol) {
                if (conf->disk_unit == 'p') {
                        post_availspace = (avail_blocks * frsize) - filesize;
                        post_percent = (post_availspace * 100) / (total_blocks * frsize);
                        if (post_percent < conf->min_free_disk)
                                avail_subvol = NULL;
                }
                if (conf->disk_unit != 'p') {
                        if ((max - filesize) < conf->min_free_disk)
                                avail_subvol = NULL;
                }
        }

        return avail_subvol;
}


/* Get subvol which has atleast one inode and maximum space */
xlator_t *
dht_subvol_maxspace_nonzeroinode (xlator_t *this, xlator_t *subvol,
                                  dht_layout_t *layout)
{
        int         i = 0;
        double      max = 0;
        int         ignore_subvol = 0;

        xlator_t   *avail_subvol = NULL;
        dht_conf_t *conf = NULL;

        conf = this->private;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                /* check if subvol has layout errors and also it is not a
                 * decommissioned brick, before selecting it*/

                ignore_subvol = dht_subvol_has_err (conf, conf->subvolumes[i], NULL,
                                                    layout);
                if (ignore_subvol)
                        continue;

                if (conf->disk_unit == 'p') {
                        if ((conf->du_stats[i].avail_percent > max)
                            && (conf->du_stats[i].avail_inodes > 0 )) {
                                max = conf->du_stats[i].avail_percent;
                                avail_subvol = conf->subvolumes[i];
                        }
               } else {
                         if ((conf->du_stats[i].avail_space > max)
                            && (conf->du_stats[i].avail_inodes > 0)) {
                                 max = conf->du_stats[i].avail_space;
                                 avail_subvol = conf->subvolumes[i];
                         }
               }
        }

        return avail_subvol;
}
