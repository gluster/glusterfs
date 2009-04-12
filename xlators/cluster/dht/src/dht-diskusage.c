/*
   Copyright (c) 2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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
	dht_local_t   *local = NULL;
        call_frame_t  *prev          = NULL;
	int            this_call_cnt = 0;
        int            i = 0;
        double         percent = 0;

	local = frame->local;
        conf = this->private;
        prev = cookie;

        if (op_ret == -1) 
                goto out;
        
        percent = (statvfs->f_bfree * 100) / statvfs->f_blocks;
        
        LOCK (&conf->subvolume_lock);
        {
                for (i = 0; i < conf->subvolume_cnt; i++)
                        if (prev->this == conf->subvolumes[i])
                                conf->du_stats[i].avail_percent = percent;
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

	conf = this->private;
        pool = this->ctx->pool;

        statfs_frame = create_frame (this, pool);
        if (!statfs_frame) {
                gf_log (this->name, GF_LOG_ERROR,
                        "memory allocation failed :(");
                goto err;
        }
        
        statfs_local = dht_local_init (statfs_frame);
        if (!statfs_local) {
                gf_log (this->name, GF_LOG_ERROR,
                        "memory allocation failed :(");
                goto err;
        }
        
        loc_t tmp_loc = { .inode = NULL,
                          .path = "/",
        };
        
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
        int            i = 0;
	dht_conf_t    *conf         = NULL;
	call_frame_t  *statfs_frame = NULL;
	dht_local_t   *statfs_local = NULL;
        struct timeval tv = {0,};

	conf  = this->private;

	gettimeofday (&tv, NULL);
	if (tv.tv_sec > (conf->refresh_interval 
			 + conf->last_stat_fetch.tv_sec)) {

                statfs_frame = copy_frame (frame);
                if (!statfs_frame) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "memory allocation failed :(");
                        goto err;
                }

                statfs_local = dht_local_init (statfs_frame);
                if (!statfs_local) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "memory allocation failed :(");
                        goto err;
                }

                loc_copy (&statfs_local->loc, loc);
                loc_t tmp_loc = { .inode = NULL,
                              .path = "/",
                };
                
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


int
dht_is_subvol_filled (xlator_t *this, xlator_t *subvol)
{
        int         i = 0;
        int         subvol_filled = 0;
	dht_conf_t *conf = NULL;

        conf = this->private;

        /* Check for values above 90% free disk */
        LOCK (&conf->subvolume_lock);
        {
                for (i = 0; i < conf->subvolume_cnt; i++) {
                        if ((subvol == conf->subvolumes[i]) &&
                            (conf->du_stats[i].avail_percent < 
                             conf->min_free_disk)) {
                                subvol_filled = 1;
                                break;
                        }
                }
        }
        UNLOCK (&conf->subvolume_lock);

        if (subvol_filled) {
                if (!(conf->du_stats[i].log++ % GF_UNIVERSAL_ANSWER)) {
                        gf_log (this->name, GF_LOG_CRITICAL,
                                "disk space on subvolume '%s' is getting "
                                "full (%.2f %%), consider adding more nodes", 
                                subvol->name, 
                                (100 - conf->du_stats[i].avail_percent));
                }
        }

        return subvol_filled;
}

xlator_t *
dht_free_disk_available_subvol (xlator_t *this, xlator_t *subvol) 
{
        int         i = 0;
        double      max_avail = 0;
        xlator_t   *avail_subvol = NULL;
	dht_conf_t *conf = NULL;

        conf = this->private;
        avail_subvol = subvol;

        LOCK (&conf->subvolume_lock);
        {
                for (i = 0; i < conf->subvolume_cnt; i++) {
                        if (conf->du_stats[i].avail_percent > max_avail) {
                                max_avail  = conf->du_stats[i].avail_percent;
                                avail_subvol = conf->subvolumes[i];
                        }
                }
        }
        UNLOCK (&conf->subvolume_lock);

        if (avail_subvol == subvol) {
                gf_log (this->name, GF_LOG_CRITICAL, 
                        "no node has enough free space to schedule create");
        }
                
        return avail_subvol;
}
