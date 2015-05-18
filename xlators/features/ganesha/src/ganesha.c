/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/


#include "ganesha.h"
#include "ganesha-mem-types.h"


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_ganesha_mt_end + 1);

        if (ret != 0)
                gf_log (this->name, GF_LOG_WARNING, "Memory accounting"
                        "init failed");

        return ret;
}

int32_t
init (xlator_t *this)
{
        int        ret         = -1;
        ganesha_priv_t   *priv = NULL;

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Need subvolume == 1");
                goto err;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Dangling volume. Check volfile");
                goto err;
        }

        priv = GF_CALLOC (1, sizeof (*priv), gf_ganesha_mt_priv_t);
        if (!priv)
                goto err;

        this->private = priv;
        ret = 0;

err:
        return ret;
}


void
fini (xlator_t *this)
{
        ganesha_priv_t *priv = this->private;

        this->private = NULL;
        if (priv)
                GF_FREE (priv);

        return;
}

struct xlator_fops fops = {
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {

        { .key  = {"ganesha.enable"},
          .default_value = "off",
          .type =  GF_OPTION_TYPE_BOOL,
          .description = "export volume via NFS-Ganesha"
        },
        { .key  = {NULL}
        },
};
