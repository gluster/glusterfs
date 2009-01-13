/*
   Copyright (c) 2008, 2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include "dht-common.c"

/* TODO:
   - use volumename in xattr instead of "dht"
   - use NS locks
   - handle all cases in self heal layout reconstruction
   - complete linkfile selfheal
*/



int
notify (xlator_t *this, int event, void *data, ...)
{
	int ret = -1;

	ret = dht_notify (this, event, data);

	return ret;
}

void
fini (xlator_t *this)
{
        int         i = 0;
        dht_conf_t *conf = NULL;

	conf = this->private;

        if (conf) {
                if (conf->file_layouts) {
                        for (i = 0; i < conf->subvolume_cnt; i++) {
                                FREE (conf->file_layouts[i]);
                        }
                        FREE (conf->file_layouts);
                }

                if (conf->default_dir_layout)
                        FREE (conf->default_dir_layout);

                if (conf->subvolumes)
                        FREE (conf->subvolumes);

		if (conf->subvolume_status)
			FREE (conf->subvolume_status);

                FREE (conf);
        }

	return;
}

int
init (xlator_t *this)
{
        dht_conf_t    *conf = NULL;
	char          *lookup_unhashed_str = NULL;
        int            ret = -1;
        int            i = 0;

	if (!this->children) {
		gf_log (this->name, GF_LOG_ERROR,
			"DHT needs more than one child defined");
		return -1;
	}
  
	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

        conf = CALLOC (1, sizeof (*conf));
        if (!conf) {
                gf_log (this->name, GF_LOG_ERROR,
                        "memory allocation failed :(");
                goto err;
        }

	conf->search_unhashed = 0;

	if (dict_get_str (this->options, "lookup-unhashed",
			  &lookup_unhashed_str) == 0) {
		gf_string2boolean (lookup_unhashed_str,
				   &conf->search_unhashed);
	}

        ret = dht_init_subvolumes (this, conf);
        if (ret == -1) {
                goto err;
        }

        ret = dht_layouts_init (this, conf);
        if (ret == -1) {
                goto err;
        }

	LOCK_INIT (&conf->subvolume_lock);

	conf->gen = 1;

        this->private = conf;

        return 0;

err:
        if (conf) {
                if (conf->file_layouts) {
                        for (i = 0; i < conf->subvolume_cnt; i++) {
                                FREE (conf->file_layouts[i]);
                        }
                        FREE (conf->file_layouts);
                }

                if (conf->default_dir_layout)
                        FREE (conf->default_dir_layout);

                if (conf->subvolumes)
                        FREE (conf->subvolumes);

		if (conf->subvolume_status)
			FREE (conf->subvolume_status);

                FREE (conf);
        }

        return -1;
}


struct xlator_fops fops = {
	.lookup      = dht_lookup,
	.mknod       = dht_mknod,
	.create      = dht_create,

	.stat        = dht_stat,
	.chmod       = dht_chmod,
	.chown       = dht_chown,
	.fchown      = dht_fchown,
	.fchmod      = dht_fchmod,
	.fstat       = dht_fstat,
	.utimens     = dht_utimens,
	.truncate    = dht_truncate,
	.ftruncate   = dht_ftruncate,
	.access      = dht_access,
	.readlink    = dht_readlink,
	.setxattr    = dht_setxattr,
	.getxattr    = dht_getxattr,
	.removexattr = dht_removexattr,
	.open        = dht_open,
	.readv       = dht_readv,
	.writev      = dht_writev,
	.flush       = dht_flush,
	.fsync       = dht_fsync,
	.statfs      = dht_statfs,
	.lk          = dht_lk,
	.opendir     = dht_opendir,
	.readdir     = dht_readdir,
	.fsyncdir    = dht_fsyncdir,
	.symlink     = dht_symlink,
	.unlink      = dht_unlink,
	.link        = dht_link,
	.mkdir       = dht_mkdir,
	.rmdir       = dht_rmdir,
	.rename      = dht_rename,
	.inodelk     = dht_inodelk,
	.finodelk    = dht_finodelk,
	.entrylk     = dht_entrylk,
	.fentrylk    = dht_fentrylk,
	.xattrop     = dht_xattrop,
	.fxattrop    = dht_fxattrop,
#if 0
	.setdents    = dht_setdents,
	.getdents    = dht_getdents,
	.checksum    = dht_checksum,
#endif
};


struct xlator_mops mops = {
};


struct xlator_cbks cbks = {
//	.release    = dht_release,
//      .releasedir = dht_releasedir,
	.forget     = dht_forget
};


struct volume_options options[] = {
        { .key  = {"lookup-unhashed"}, 
	  .type = GF_OPTION_TYPE_BOOL 
	},
	{ .key  = {NULL} },
};
