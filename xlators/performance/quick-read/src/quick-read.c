/*
  Copyright (c) 2009-2010 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include "quick-read.h"


int32_t 
init (xlator_t *this)
{
	char      *str = NULL;
        int32_t    ret = -1;
        qr_conf_t *conf = NULL;
 
        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "FATAL: volume (%s) not configured with exactly one "
			"child", this->name);
                return -1;
        }

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

        conf = CALLOC (1, sizeof (*conf));
        if (conf == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory");
                ret = -1;
                goto out;
        }

        ret = dict_get_str (this->options, "max-file-size", 
                            &str);
        if (ret == 0) {
                ret = gf_string2bytesize (str, &conf->max_file_size);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_ERROR, 
                                "invalid number format \"%s\" of \"option "
                                "max-file-size\"", 
                                str);
                        ret = -1;
                        goto out;
                }
        }

        conf->cache_timeout = -1;
        ret = dict_get_str (this->options, "cache-timeout", &str);
        if (ret == 0) {
                ret = gf_string2uint_base10 (str, 
                                             (unsigned int *)&conf->cache_timeout);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "invalid cache-timeout value %s", str);
                        ret = -1;
                        goto out;
                } 
        }

        this->private = conf;
out:
        if ((ret == -1) && conf) {
                FREE (conf);
        }

        return ret;
}


void
fini (xlator_t *this)
{
        return;
}


struct xlator_fops fops = {
};


struct xlator_mops mops = {
};


struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key  = {"cache-timeout"}, 
          .type = GF_OPTION_TYPE_INT,
          .min = 1,
          .max = 60
        },
        { .key  = {"max-file-size"}, 
          .type = GF_OPTION_TYPE_SIZET, 
          .min  = 0,
          .max  = 1 * GF_UNIT_MB 
        },
};
